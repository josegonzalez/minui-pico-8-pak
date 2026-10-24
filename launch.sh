#!/bin/sh
PAK_DIR="$(dirname "$0")"
PAK_NAME="$(basename "$PAK_DIR")"
PAK_NAME="${PAK_NAME%.*}"

architecture=arm
if uname -m | grep -q '64'; then
  architecture=arm64
fi

# pico-8 runs from the BIOS folder itself, by its full path rather than from
# PATH, so nothing else in that folder can be found as a command
export EMU_DIR="$SDCARD_PATH/Bios/PICO"
export HOME="$SHARED_USERDATA_PATH/Pico-8-native"
export PATH="$PAK_DIR/bin/$PLATFORM:$PAK_DIR/bin/$architecture:$PAK_DIR/bin:$PATH"
export XDG_CONFIG_HOME="$HOME/config"
export XDG_DATA_HOME="$HOME/data"

export GAMESETTINGS_DIR="$HOME/game-settings/$ROM_NAME"
export SCREENSHOT_DIR="$SDCARD_PATH/Screenshots"

# The platforms this pak supports. test/makefile.bats asserts this agrees with
# pak.json and with the Makefile.
SUPPORTED_PLATFORMS="h700 my355 rg35xxplus tg5040 tg5050"

# minui-power-control refuses to start on a platform its own launcher does not
# list, so calling it elsewhere only logs noise. Keep this in sync with the
# release the Makefile pins, which comes from a fork until upstream takes h700
# and NextUI on my355.
POWER_CONTROL_PLATFORMS="h700 miyoomini my355 rg35xxplus tg5040 tg5050"

# scaling_setspeed only takes effect under the userspace governor and the value
# is a per SoC frequency, so the write is opt in per platform. NextUI manages the
# clock itself, which is why h700 and tg5050 are absent and why set_cpu_speed
# also checks the governor on the platforms MinUI and NextUI share.
CPU_SETSPEED_PLATFORMS="rg35xxplus tg5040"

# A trailing 1.2, v3 or (v1.4.3) names an upload, not the game, and trailing
# dashes are decoration. A bare number such as the 2 in "celeste 2" stays.
tidy_title() {
  tidied="$(printf '%s\n' "$1" | sed -E \
    -e 's/[[:space:]]*-+[[:space:]]*$//' \
    -e 's/[[:space:]]+[-([]?[[:space:]]*([vV][0-9]+(\.[0-9]+)*|[0-9]+(\.[0-9]+)+)[a-zA-Z]?[])]?$//')"
  printf '%s\n' "${tidied:-$1}"
}

# First-line comments that name a section of code rather than the game, all seen
# in a Splore cache of 263 carts.
is_code_label() {
  case "$(printf '%s\n' "$1" | awk '{ sub(/[[:space:]-]+$/, ""); print tolower($0) }')" in
  main | init | grid | points | variables | "global variables" | "helper functions" | \
    "control functions" | "state machine" | "main loop" | "main game" | \
    "main structure" | "game title here")
    return 0
    ;;
  esac
  return 1
}

titlecase() {
  input="$1"
  exceptions="a an the and but or nor for so yet at by in of on to up off out over with as"
  acronyms="Pico-8=PICO-8 Rpg=RPG Ai=AI Ui=UI Os=OS Cpu=CPU Tmnt=TMNT"

  echo "$input" | awk -v IGNORECASE=1 -v exceptions="$exceptions" -v acronyms="$acronyms" '
	BEGIN {
		split(exceptions, exlist)
		for (e in exlist) exc[exlist[e]] = 1

		split(acronyms, acrlist)
		for (a in acrlist) {
			n = split(acrlist[a], kv, "=")
			acr[tolower(kv[1])] = kv[2]
		}
	}
	{
		gsub("_", " ")
		gsub("-", " - ")

		n = split($0, words, " ")
		for (i = 1; i <= n; i++) {
			word = words[i]
			orig = word
			trailing = ""

			# Capture trailing punctuation
			if (match(word, /[^[:alnum:]]+$/)) {
				trailing = substr(word, RSTART)
				orig = substr(word, 1, RSTART - 1)
			}

			lw = tolower(orig)
			prev_colon = (i > 1 && match(words[i - 1], /:$/))

			if (lw in acr) {
				words[i] = acr[lw] trailing
			} else if (i == 1 || prev_colon || !(lw in exc)) {
				words[i] = toupper(substr(orig,1,1)) substr(orig,2) trailing
			} else {
				words[i] = lw trailing
			}
		}

		out = words[1]
		for (i = 2; i <= n; i++) out = out " " words[i]
		gsub(" - ", "-", out)

		# The hyphen split above turns pico-8 into the three words pico, -
		# and 8, so an acronym spelled with a hyphen can only be matched
		# once the words have been joined back up.
		m = split(out, joined, " ")
		for (i = 1; i <= m; i++) {
			orig = joined[i]
			trailing = ""

			if (match(orig, /[^[:alnum:]]+$/)) {
				trailing = substr(orig, RSTART)
				orig = substr(orig, 1, RSTART - 1)
			}

			lw = tolower(orig)
			if (lw in acr) joined[i] = acr[lw] trailing
		}

		out = joined[1]
		for (i = 2; i <= m; i++) out = out " " joined[i]
		print out
	}'
}

# copy_carts builds its cart list and the replacement map.txt outside the roms
# folder so a half-written file is never visible to MinUI. cleanup removes the
# folder, and copy_carts rebuilds it, so neither a crash nor a stale run leaks.
get_copy_carts_tmp_dir() {
  echo "/tmp/$PAK_NAME-copy-carts"
}

# pico-8 is started with -home "$HOME", which makes that folder the data folder
# itself. A build that ignores the flag nests everything under .lexaloffle/pico-8
# instead - config/zero28.txt was captured from a run like that - so both layouts
# are searched rather than one being assumed.
list_bbs_carts() {
  for bbs_root in "$HOME/bbs" "$HOME/.lexaloffle/pico-8/bbs"; do
    [ -d "$bbs_root" ] || continue

    # downloads land in bbs/carts, and pico-8 shards numerically named ones into
    # bbs/1, bbs/4 and so on, so one level of nesting is covered as well
    for bbs_cart in "$bbs_root"/*.p8.png "$bbs_root"/*/*.p8.png; do
      [ -f "$bbs_cart" ] || continue

      # splore writes partial downloads and their sidecars under a temp- prefix
      case "${bbs_cart##*/}" in
      temp-*) continue ;;
      esac

      echo "$bbs_cart"
    done
  done
}

# favourites.txt has changed shape across pico-8 releases - some builds write a
# bare cart name, others a path such as bbs/1/15133.p8.png - so it is read as a
# set of cart names rather than by field position, which is what the parser this
# replaced got wrong. pico-8 also pads every field with spaces to line up the
# columns, so each field is trimmed before its path and extension come off.
list_favourite_names() {
  tr -d '\r' <"$1" |
    tr '|' '\n' |
    sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
      -e 's#.*/##' -e 's#\.p8\.png$##' -e 's#\.p8$##' -e '/^$/d'
}

# A BBS cart is named for its id or its slug, so a readable name has to come out
# of the cart itself. The BBS convention puts the title in a comment on the first
# line of code, and pico8-data-extractor --title applies that rule, trimming the
# decoration around it, and fails when that line holds no title. A comment
# further down is ordinary code commentary, which is what searching the whole
# program for one used to pick up.
get_cart_title() {
  cart_file="$1"

  if ! command -v pico8-data-extractor >/dev/null 2>&1; then
    return 1
  fi

  pico8-data-extractor --title "$cart_file"
}

# Splore records each favourite's BBS title in favourites.txt. Builds that write
# the |name-rev |slug |id |author |... |title layout put it in the seventh field;
# the other layouts list_favourite_names handles have no title and yield nothing.
list_splore_titles() {
  for fav_file in "$HOME/favourites.txt" "$HOME/.lexaloffle/pico-8/favourites.txt"; do
    [ -f "$fav_file" ] || continue

    tr -d '\r' <"$fav_file" | awk -F'|' '
      function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
      NF >= 7 {
        slug = trim($2)
        sub(/.*\//, "", slug)
        sub(/\.p8(\.png)?$/, "", slug)
        if (match(slug, /-[0-9]+$/) && RSTART > 1) slug = substr(slug, 1, RSTART - 1)
        title = trim($7)
        if (slug != "" && title != "") print slug "\t" title
      }'
  done
}

# The BBS title beats the cart's own first-line comment, which is often a code
# label or an older version. Splore shows the slug itself when a cart has no
# title, though, and the cart's comment is better than that.
get_splore_title() {
  awk -F'\t' -v slug="$1" '$1 == slug && tolower($2) != tolower(slug) { print $2; exit }' "$2"
}

# Splore saves a cart named by slug as <slug>-<revision>.p8.png, so a cart with
# no title is named from the slug alone - suika_demake-9.p8.png becomes
# suika_demake. A cart named by its numeric id has no revision and keeps it all.
get_cart_filename_title() {
  cart_base="${1%.p8.png}"
  cart_revision="${cart_base##*-}"
  cart_slug="${cart_base%-*}"

  case "$cart_revision" in
  "$cart_base" | "" | *[!0-9]*) ;;
  *)
    if [ -n "$cart_slug" ]; then
      cart_base="$cart_slug"
    fi
    ;;
  esac

  printf '%s\n' "$cart_base"
}

copy_carts() {
  ROM_FOLDER="$1"

  # the marker was only ever read from the platform userdata folder, but every
  # other piece of pico-8 data lives in the shared one, so both are accepted
  copy_marker=""
  favourites_only=false
  for marker_dir in "$USERDATA_PATH/Pico-8-native" "$SHARED_USERDATA_PATH/Pico-8-native"; do
    if [ -f "$marker_dir/copy-carts" ]; then
      copy_marker="$marker_dir/copy-carts"
    fi

    # the favourites marker turns the copy on by itself, so creating only that
    # file is not another silent no-op. A marker spelled the American way used
    # to be ignored as well, so both spellings are accepted.
    for favourites_marker in copy-carts-favourites copy-carts-favorites; do
      if [ -f "$marker_dir/$favourites_marker" ]; then
        copy_marker="$marker_dir/$favourites_marker"
        favourites_only=true
      fi
    done
  done

  if [ -z "$copy_marker" ]; then
    echo "No copy-carts marker in $USERDATA_PATH/Pico-8-native or $SHARED_USERDATA_PATH/Pico-8-native, not copying carts" 1>&2
    return 0
  fi

  # scratch is rebuilt from empty so a run that died before its cleanup cannot
  # feed stale cart names into this one
  COPY_CARTS_TMP="$(get_copy_carts_tmp_dir)"
  rm -rf "$COPY_CARTS_TMP"
  mkdir -p "$COPY_CARTS_TMP"

  CARTS_FILE="$COPY_CARTS_TMP/carts.txt"
  COPIED_FILE="$COPY_CARTS_TMP/copied.txt"
  REPLACED_FILE="$COPY_CARTS_TMP/replaced.txt"
  KEPT_FILE="$COPY_CARTS_TMP/kept.txt"
  FAV_FILE_NAMES="$COPY_CARTS_TMP/favourites.txt"
  MAP_FILE="$ROM_FOLDER/map.txt"
  MEDIA_FOLDER="$ROM_FOLDER/.media"
  RES_FOLDER="$ROM_FOLDER/.res"

  # the same cart can sit under both bbs roots, and deduplicating the list here
  # keeps the loop below from spawning a lookup per cart to notice it. Splore
  # also keeps the old revision when it downloads an update (issue #39), so only
  # the newest revision of each cart goes on, in the order they were listed.
  list_bbs_carts | awk -F/ '!seen[$NF]++' >"$CARTS_FILE.all"
  awk -F/ '
    {
      slug = $NF; revision = -1
      if (match($NF, /-[0-9]+\.p8\.png$/)) {
        slug = substr($NF, 1, RSTART - 1)
        revision = substr($NF, RSTART + 1, RLENGTH - 8) + 0
      }
    }
    NR == FNR { if (!(slug in newest) || revision > newest[slug]) newest[slug] = revision; next }
    revision == newest[slug] && !printed[slug]++' "$CARTS_FILE.all" "$CARTS_FILE.all" >"$CARTS_FILE"
  if [ ! -s "$CARTS_FILE" ]; then
    echo "No downloaded carts under $HOME/bbs, nothing to copy" 1>&2
    rm -rf "$COPY_CARTS_TMP"
    return 0
  fi

  loop_file="$CARTS_FILE"
  if [ "$favourites_only" = "true" ]; then
    # favourites.txt sits beside bbs, so it is looked for under the same two
    # roots, and both are read because taking the first one that existed let an
    # empty or stale copy hide the one pico-8 was actually writing to. They are
    # read into scratch rather than rewriting the file pico-8 owns, which is what
    # the line stripping carriage returns here used to do.
    fav_found=false
    true >"$FAV_FILE_NAMES"
    for FAV_FILE in "$HOME/favourites.txt" "$HOME/.lexaloffle/pico-8/favourites.txt"; do
      [ -f "$FAV_FILE" ] || continue
      fav_found=true
      echo "Reading favourites from $FAV_FILE" 1>&2
      list_favourite_names "$FAV_FILE" >>"$FAV_FILE_NAMES"
    done

    if [ "$fav_found" = "false" ]; then
      echo "$copy_marker asks for favourites only but no favourites.txt exists, nothing to copy" 1>&2
      rm -rf "$COPY_CARTS_TMP"
      return 0
    fi

    if [ ! -s "$FAV_FILE_NAMES" ]; then
      echo "$copy_marker asks for favourites only but favourites.txt lists no carts, nothing to copy" 1>&2
      rm -rf "$COPY_CARTS_TMP"
      return 0
    fi

    # a cart favourited before an update is listed under its old revision, so
    # favourites are matched by slug and the update is still copied
    sed 's/-[0-9][0-9]*$//' "$FAV_FILE_NAMES" >"$FAV_FILE_NAMES.slugs"

    # Splore caches every cart it shows, so the cache is filtered in one pass
    # here rather than spending two commands per cached cart in the loop
    awk -F/ '
      NR == FNR { fav[$0] = 1; next }
      {
        slug = $NF
        sub(/\.p8\.png$/, "", slug)
        if (match(slug, /-[0-9]+$/) && RSTART > 1) slug = substr(slug, 1, RSTART - 1)
      }
      slug in fav' "$FAV_FILE_NAMES.slugs" "$CARTS_FILE" >"$CARTS_FILE.favourites"
    loop_file="$CARTS_FILE.favourites"
  fi

  true >"$COPIED_FILE"
  true >"$REPLACED_FILE"
  SPLORE_TITLES="$COPY_CARTS_TMP/splore-titles.txt"
  list_splore_titles >"$SPLORE_TITLES"
  copy_message_shown=false

  # read from the file rather than a pipe so the loop does not run in a subshell
  while IFS= read -r bbs_cart; do
    cart_name="${bbs_cart##*/}"
    cart_slug="$(get_cart_filename_title "$cart_name")"

    # a row already in map.txt is both the name the user sees and the reason the
    # extractor does not run again for a cart that was named on an earlier launch
    cart_title=""
    if [ -f "$MAP_FILE" ]; then
      cart_title="$(awk -F'\t' -v name="$cart_name" '$1 == name { print $2; exit }' "$MAP_FILE")"
    fi

    # an older revision copied on an earlier exit is replaced by this one, and
    # its name carries over so one the user edited is kept. Only files with a row
    # in map.txt are touched, since those are the ones this function wrote.
    if [ "$cart_slug" != "${cart_name%.p8.png}" ] && [ -f "$MAP_FILE" ]; then
      for old_cart in "$ROM_FOLDER/$cart_slug"-*.p8.png; do
        old_name="${old_cart##*/}"
        if [ ! -f "$old_cart" ] || [ "$old_name" = "$cart_name" ] ||
          [ "$(get_cart_filename_title "$old_name")" != "$cart_slug" ]; then
          continue
        fi

        old_title="$(awk -F'\t' -v name="$old_name" '$1 == name { print $2; exit }' "$MAP_FILE")"
        if [ -z "$old_title" ]; then
          continue
        fi

        if [ -z "$cart_title" ]; then
          cart_title="$old_title"
        fi
        rm -f "$old_cart" "$MEDIA_FOLDER/${old_name%.*}.png" "$RES_FOLDER/$old_name.png"
        printf '%s\n' "$old_name" >>"$REPLACED_FILE"
        echo "Replaced $old_name with $cart_name in $ROM_FOLDER" 1>&2
      done
    fi

    # NextUI drops the cart's last extension and MinUI keeps the whole name, so
    # the two artwork folders need different filenames for the same cart
    media_name="${cart_name%.*}.png"
    res_name="$cart_name.png"

    # extracting titles and writing to the SD card can take a while after pico-8
    # exits, and a blank screen in the meantime looks like a hang worth powering
    # off, which loses both the copy and the log. A cart copied on an earlier
    # exit needs neither, so the message waits for one that does.
    if [ "$copy_message_shown" = "false" ] &&
      { [ -z "$cart_title" ] || [ ! -f "$ROM_FOLDER/$cart_name" ] ||
        [ ! -f "$MEDIA_FOLDER/$media_name" ] || [ ! -f "$RES_FOLDER/$res_name" ]; }; then
      show_message "Copying downloaded carts into the roms folder." forever
      copy_message_shown=true
    fi

    if [ -z "$cart_title" ]; then
      cart_title="$(get_splore_title "$cart_slug" "$SPLORE_TITLES")"
      if [ -z "$cart_title" ]; then
        cart_title="$(get_cart_title "$bbs_cart")" || cart_title=""
        if [ -z "$cart_title" ] || is_code_label "$cart_title"; then
          cart_title="$cart_slug"
        fi
      fi
      cart_title="$(titlecase "$(tidy_title "$cart_title")")"
    fi

    mkdir -p "$MEDIA_FOLDER" "$RES_FOLDER"

    if [ ! -f "$ROM_FOLDER/$cart_name" ]; then
      cp -f "$bbs_cart" "$ROM_FOLDER/$cart_name"
    fi
    if [ ! -f "$MEDIA_FOLDER/$media_name" ]; then
      cp -f "$bbs_cart" "$MEDIA_FOLDER/$media_name"
    fi
    if [ ! -f "$RES_FOLDER/$res_name" ]; then
      cp -f "$bbs_cart" "$RES_FOLDER/$res_name"
    fi

    printf "%s\t%s\n" "$cart_name" "$cart_title" >>"$COPIED_FILE"
  done <"$loop_file"

  killall minui-presenter >/dev/null 2>&1 || true

  if [ ! -s "$COPIED_FILE" ]; then
    # the favourite names are not counted because list_favourite_names emits
    # every field of a row, not one name per favourite
    if [ "$favourites_only" = "true" ]; then
      echo "No favourite in favourites.txt matches any of the $(grep -c '' "$CARTS_FILE") downloaded carts, nothing to copy into $ROM_FOLDER" 1>&2
    else
      echo "No downloaded carts matched, nothing to copy into $ROM_FOLDER" 1>&2
    fi
    rm -rf "$COPY_CARTS_TMP"
    return 0
  fi

  echo "Copied $(grep -c '' "$COPIED_FILE") carts into $ROM_FOLDER" 1>&2

  # map.txt is the display name file MinUI and NextUI read, and a user may have
  # written rows for carts of their own, so only the rows for the carts copied
  # or replaced here are rewritten instead of the whole file being truncated
  if [ -f "$MAP_FILE" ]; then
    # ENVIRON rather than awk -v, which would process backslash escapes in a path
    MAP_PATH="$MAP_FILE" awk -F'\t' 'FILENAME != ENVIRON["MAP_PATH"] { drop[$1] = 1; next } !($1 in drop)' \
      "$COPIED_FILE" "$REPLACED_FILE" "$MAP_FILE" >"$KEPT_FILE"
    cat "$KEPT_FILE" >>"$COPIED_FILE"
  fi

  cp -f "$COPIED_FILE" "$MAP_FILE"
  rm -rf "$COPY_CARTS_TMP"

  sync
  return 0
}

get_screen_mode() {
  if [ ! -f "$USERDATA_PATH/Pico-8-native/screen-mode" ]; then
    echo "standard" >"$USERDATA_PATH/Pico-8-native/screen-mode"
  fi

  cat "$USERDATA_PATH/Pico-8-native/screen-mode"
}

get_pico_bin() {
  pico_bin="pico8_64"
  if [ "$architecture" = "arm" ]; then
    pico_bin="pico8"
  fi
  if [ "$PLATFORM" = "rg35xxplus" ]; then
    pico_bin="pico8_dyn"
  fi
  echo "$pico_bin"
}

# NextUI builds SDL2 without sensor support, and pico-8 asks SDL to start every
# subsystem and aborts when one of them will not, so it dies with "SDL not built
# with sensor support" before drawing anything. The shim drops that one request
# on its way past and hands the rest to the device's own SDL2, which on h700 is
# the only build that enumerates the gpio-keys pad controllers/h700.txt maps.
# Replacing SDL2 would fix the crash and kill the pad.
get_sdl_shim() {
  if [ "$PLATFORM" != "h700" ]; then
    return 0
  fi

  echo "$PAK_DIR/lib/h700/sdl-nosensor.so"
}

# Split from get_sdl_shim so that one stays a plain value: a helper that both
# echoes a path and logs would put the log line in its own output.
setup_sdl_preload() {
  sdl_shim="$(get_sdl_shim)"
  if [ -z "$sdl_shim" ]; then
    return 0
  fi

  if [ ! -f "$sdl_shim" ]; then
    echo "No SDL shim at $sdl_shim, starting pico-8 without one" 1>&2
    return 0
  fi

  # ours goes first because preload objects are searched in order, and the
  # inherited value is appended conditionally so nothing leaves an empty element
  export LD_PRELOAD="$sdl_shim${LD_PRELOAD:+:$LD_PRELOAD}"
}

# The display shim is preloaded into pico-8, so it has to match the binary
# rather than uname -m: rg35xxplus reports its 64-bit kernel while MinUI's
# userland, and the pico8_dyn get_pico_bin picks for it, are 32-bit.
get_pico_architecture() {
  if [ "$(get_pico_bin)" = "pico8_64" ]; then
    echo "arm64"
    return 0
  fi

  echo "arm"
}

# The menu dresses as the launcher pico-8 was started from. NextUI keeps its
# theme in minuisettings.txt and ships font1.ttf and font2.ttf, while MinUI has
# neither and draws everything with BPreplay.
get_menu_style() {
  if [ -f "$SHARED_USERDATA_PATH/minuisettings.txt" ] && [ -f "$SDCARD_PATH/.system/res/font1.ttf" ]; then
    echo "nextui"
    return 0
  fi

  echo "minui"
}

# Prints one key of NextUI's minuisettings.txt, the last if it is repeated, as
# NextUI reads it, without a Windows line ending
read_nextui_setting() {
  SETTING_KEY="$1" awk -F= '
    $1 == ENVIRON["SETTING_KEY"] { value = substr($0, length($1) + 2); sub(/\r$/, "", value) }
    END { print value }' "$SHARED_USERDATA_PATH/minuisettings.txt"
}

# Takes the style get_menu_style printed. Nothing is printed when the device
# has no font, and the menu then draws with a small one of its own.
get_menu_font() {
  menu_res_dir="$SDCARD_PATH/.system/res"

  if [ "$1" = "nextui" ]; then
    # NextUI's own reading of font=, where 0 is font2 and 1 is font1, the
    # numbers older releases wrote, and anything else names a file
    menu_font="$(read_nextui_setting font)"
    case "$menu_font" in
    0) menu_font="font2.ttf" ;;
    "" | 1) menu_font="font1.ttf" ;;
    esac
    if [ ! -f "$menu_res_dir/$menu_font" ]; then
      menu_font="font1.ttf"
    fi
    echo "$menu_res_dir/$menu_font"
    return 0
  fi

  if [ -f "$menu_res_dir/BPreplayBold-unhinted.otf" ]; then
    echo "$menu_res_dir/BPreplayBold-unhinted.otf"
  fi
}

# Takes the style get_menu_style printed. Prints NextUI's seven theme colours
# in order, joined with commas, with an empty field for one it has not
# written, all from one read of the file. MinUI's look is fixed, so it has none.
get_menu_colors() {
  if [ "$1" != "nextui" ]; then
    return 0
  fi

  awk -F= '
    $1 ~ /^color[1-7]$/ {
      value = substr($0, length($1) + 2)
      sub(/\r$/, "", value)
      color[substr($1, 6)] = value
    }
    END {
      for (i = 1; i <= 7; i++) {
        printf "%s%s", (i > 1 ? "," : ""), color[i]
      }
      print ""
    }' "$SHARED_USERDATA_PATH/minuisettings.txt"
}

# pico-8 cannot draw a bezel itself, so the shim draws one over every frame.
# Overlays live where NextUI keeps them for its libretro cores, a folder named
# for the emulator tag under /Overlays, which is the name the pak folder carries.
get_overlay_dir() {
  echo "$SDCARD_PATH/Overlays/$PAK_NAME"
}

has_overlays() {
  for overlay_file in "$(get_overlay_dir)"/*.png; do
    if [ -f "$overlay_file" ]; then
      return 0
    fi
  done

  return 1
}

# Takes the style get_menu_style printed. 1 when a screenshot should show a
# notice: NextUI's own notifyScreenshot setting, which is on unless set to 0,
# and always under MinUI, which has no such setting.
get_notify_screenshot() {
  if [ "$1" = "nextui" ] && [ "$(read_nextui_setting notifyScreenshot)" = "0" ]; then
    echo "0"
    return 0
  fi

  echo "1"
}

# The folder NextUI's game switcher and MinUI's game list read a cart's slot
# to resume from, where the display shim names the slot each save state goes
# to and writes its preview.
get_switcher_dir() {
  echo "$SHARED_USERDATA_PATH/.minui/$PAK_NAME"
}

# 1 under NextUI, whose switcher shows a cart's artwork, or its last frame,
# before the cart has any state. MinUI offers a resume whenever the switcher
# folder names a slot, so there the shim names one only once it holds a state.
get_stateless_previews() {
  if [ "$IS_NEXT" = "yes" ]; then
    echo "1"
    return 0
  fi

  echo "0"
}

# Save states are kept per platform, unlike the shared HOME, as a state only
# loads under the build of PICO-8 that saved it. Splore has none.
get_states_dir() {
  if [ "$SPLORE_SESSION" != true ]; then
    echo "$USERDATA_PATH/Pico-8-native/states"
  fi
}

# Both launchers name the slot to resume in this file before starting a pak,
# and power control removes it as it starts, so main reads it before then. The
# path is overridable so the test suite can supply one.
get_resume_slot_file() {
  echo "${PICO_PAK_RESUME_SLOT_FILE:-/tmp/resume_slot.txt}"
}

# Exports the slot the launcher asked to resume, for the display shim to put
# back once the cart starts. The file is removed either way, as minarch would
# otherwise load the slot the next time it starts a game. A normal launch names
# slot 8, minarch's hidden default, which the pak never saves, so the cart
# starts afresh.
read_resume_slot() {
  resume_slot_file="$(get_resume_slot_file)"
  unset PICO_PAK_RESUME_SLOT
  if [ ! -f "$resume_slot_file" ]; then
    return 0
  fi

  resume_slot="$(cat "$resume_slot_file")"
  rm -f "$resume_slot_file"
  case "$resume_slot" in
  [0-9]) ;;
  *)
    echo "Ignoring the resume slot \"$resume_slot\"" 1>&2
    return 0
    ;;
  esac
  if [ "$SPLORE_SESSION" = true ]; then
    return 0
  fi

  echo "Resuming slot $resume_slot if it holds a state" 1>&2
  export PICO_PAK_RESUME_SLOT="$resume_slot"
}

# The shim also draws the in-game menu, so it is loaded whenever the pak ships
# it, overlays or not. Takes the cart being started, which the shim works out
# the menu's title and the switcher's preview from. Returns non-zero when it is
# not loaded, so the caller knows to stretch pico-8 itself.
setup_display_preload() {
  overlay_dir="$(get_overlay_dir)"
  display_shim="$PAK_DIR/lib/$(get_pico_architecture)/sdl-display.so"
  if [ ! -f "$display_shim" ]; then
    if has_overlays; then
      echo "No display shim at $display_shim, starting pico-8 without overlays from $overlay_dir" 1>&2
    fi
    return 1
  fi

  # NextUI's own skeleton creates the overlay folder for each of its cores, and
  # nothing creates one for this pak, so it is made here for the user to fill
  mkdir -p "$overlay_dir"

  # the menu saves its settings to these files itself, the same ones the rest
  # of this script reads
  export PICO_PAK_OVERLAY_DIR="$overlay_dir"
  export PICO_PAK_OVERLAY_CHOICE="$USERDATA_PATH/Pico-8-native/overlay"
  export PICO_PAK_SCREEN_MODE="$USERDATA_PATH/Pico-8-native/screen-mode"
  export PICO_PAK_COPY_CARTS_DIR="$USERDATA_PATH/Pico-8-native"
  export PICO_PAK_SHARED_COPY_CARTS_DIR="$SHARED_USERDATA_PATH/Pico-8-native"
  PICO_PAK_SPLORE_ARTWORK="$(get_splore_artwork_file)"
  export PICO_PAK_SPLORE_ARTWORK
  PICO_PAK_PICO8_CONFIG="$(get_pico8_config_overrides)"
  export PICO_PAK_PICO8_CONFIG
  export PICO_PAK_INSTALLED_CONFIG="$HOME/config.txt"

  # NextUI's shaders and presets, where its skeleton puts them, and the ones
  # chosen, saved in minarch's own form. The folder is NextUI's to make
  export PICO_PAK_SHADERS_DIR="$SDCARD_PATH/Shaders"
  export PICO_PAK_SHADER_SETTINGS="$USERDATA_PATH/Pico-8-native/shaders"

  # the shortcuts set in the menu, and where the screenshot one saves to
  export PICO_PAK_SHORTCUTS="$USERDATA_PATH/Pico-8-native/shortcuts"
  # the button each of PICO-8's is remapped to in the menu
  export PICO_PAK_CONTROLS="$USERDATA_PATH/Pico-8-native/controls"
  PICO_PAK_SCREENSHOTS_DIR="$(get_screenshots_dir)"
  export PICO_PAK_SCREENSHOTS_DIR

  # the menu gives every Splore cart the cover it is set to, as it changes
  export PICO_PAK_ROMS_DIR="$SDCARD_PATH/Roms"
  export PICO_PAK_TAG="$PAK_NAME"
  export PICO_PAK_SPLORE_COVERS="$PAK_DIR/splore"

  export PICO_PAK_CART_PATH="$1"
  PICO_PAK_SWITCHER_DIR="$(get_switcher_dir)"
  PICO_PAK_STATELESS_PREVIEWS="$(get_stateless_previews)"
  export PICO_PAK_STATELESS_PREVIEWS

  # save states, and the cart a shutdown has the launcher start again at boot,
  # named as power control names it, from the root of the SD card
  PICO_PAK_STATES_DIR="$(get_states_dir)"
  if [ -n "$PICO_PAK_STATES_DIR" ]; then
    export PICO_PAK_STATES_DIR
  else
    unset PICO_PAK_STATES_DIR
  fi
  export PICO_PAK_AUTO_RESUME_PATH="$SHARED_USERDATA_PATH/.minui/auto_resume.txt"
  # whether Quit also saves the slot to resume, which only shutting down does unless it is on
  export PICO_PAK_AUTO_SAVE="$USERDATA_PATH/Pico-8-native/auto-save-on-exit"
  export PICO_PAK_AUTO_RESUME_ROM="${1#"$SDCARD_PATH"}"
  PICO_PAK_MENU_STYLE="$(get_menu_style)"
  PICO_PAK_MENU_FONT="$(get_menu_font "$PICO_PAK_MENU_STYLE")"
  PICO_PAK_MENU_COLORS="$(get_menu_colors "$PICO_PAK_MENU_STYLE")"
  PICO_PAK_NOTIFY_SCREENSHOT="$(get_notify_screenshot "$PICO_PAK_MENU_STYLE")"
  export PICO_PAK_SWITCHER_DIR PICO_PAK_MENU_STYLE PICO_PAK_MENU_FONT PICO_PAK_MENU_COLORS
  export PICO_PAK_NOTIFY_SCREENSHOT
  echo "In-game menu: $PICO_PAK_MENU_STYLE look, font ${PICO_PAK_MENU_FONT:-built-in}" 1>&2

  export LD_PRELOAD="$display_shim${LD_PRELOAD:+:$LD_PRELOAD}"
}

# Whole word match against a space separated list. The platform checks used to
# be `echo "$list" | grep -q "$PLATFORM"`, which also accepted every substring,
# so rg35xx and tg50 both passed as supported platforms.
platform_in_list() {
  case " $2 " in
  *" $1 "*)
    return 0
    ;;
  esac

  return 1
}

get_controller_file() {
  case "$PLATFORM" in
  h700 | rg35xxplus)
    # both platforms are Allwinner H700 hardware, so one file holds the pads
    # either one reports and SDL picks the line by GUID
    echo "h700.txt"
    ;;
  *)
    echo "$PLATFORM.txt"
    ;;
  esac
}

# the sysfs directory is overridable so the test suite can observe the write
set_cpu_speed() {
  cpufreq_dir="${PICO_PAK_CPUFREQ_DIR:-/sys/devices/system/cpu/cpu0/cpufreq}"

  if ! platform_in_list "$PLATFORM" "$CPU_SETSPEED_PLATFORMS"; then
    return 0
  fi

  if [ ! -w "$cpufreq_dir/scaling_setspeed" ]; then
    return 0
  fi

  # the node is writable whatever the governor, but the kernel refuses the write
  # with EINVAL unless the userspace governor is active, as it is under NextUI
  # on the TrimUI Brick. Switching governor is not ours to do: it was never put
  # back when the pak tried that before.
  governor="$(cat "$cpufreq_dir/scaling_governor" 2>/dev/null || true)"
  if [ "$governor" != "userspace" ]; then
    echo "cpufreq governor is ${governor:-unknown}, leaving the clock speed to the OS" 1>&2
    return 0
  fi

  echo 1600000 >"$cpufreq_dir/scaling_setspeed"
}

get_screen_resolution() {
  if command -v fbset >/dev/null 2>&1; then
    fbset | grep 'geometry' | awk '{print $2,$3}'
    return 0
  fi

  # NextUI ships no fbset. DEVICE is a poor way to name the pad but it is
  # exactly what NextUI exports to describe the panel. The RG28XX panel is
  # portrait and SDL_ROTATION presents it to applications as 640x480 landscape.
  if [ "$PLATFORM" = "h700" ]; then
    case "$DEVICE" in
    rgcubexx)
      echo "720 720"
      ;;
    rg34xx | rg34xxsp | rgsp)
      echo "720 480"
      ;;
    *)
      echo "640 480"
      ;;
    esac
  fi
}

# MinUI and NextUI both keep saves under /Saves, in a folder named for the
# emulator tag, which is the name the pak folder carries. PAK_NAME is read
# lazily because the test suite overrides it after sourcing this file.
get_saves_dir() {
  echo "$SDCARD_PATH/Saves/$PAK_NAME"
}

# NextUI saves its screenshots here, and the pak saves its screenshots and GIFs
# alongside them
get_screenshots_dir() {
  echo "$SDCARD_PATH/Screenshots"
}

# config.txt has no command line equivalent for cdata_path, and pico-8 writes
# cart saves there, so it has to name the Saves folder on this device's SD card
# rather than the mount the checked in template happened to be captured on.
# root_path is rewritten alongside it so the file agrees with the -root_path
# already passed on the command line. desktop_path is where pico-8 saves the
# GIFs the Save GIF shortcut asks for, so it names the screenshots folder.
install_config() {
  rom_folder="$1"
  overrides="$(get_pico8_config_overrides)"
  if [ ! -f "$overrides" ]; then
    overrides="/dev/null"
  fi

  # ENVIRON rather than awk -v, which would process backslash escapes in a path.
  # Only the settings the menu offers are taken from its file, and only as
  # numbers, so a stray line can never move cdata_path or root_path.
  PICO_CDATA_PATH="$(get_saves_dir)/" PICO_ROOT_PATH="$rom_folder/" \
  PICO_DESKTOP_PATH="$(get_screenshots_dir)/" PICO_OVERRIDES="$overrides" awk '
    BEGIN {
      cdata = ENVIRON["PICO_CDATA_PATH"]
      root = ENVIRON["PICO_ROOT_PATH"]
      desktop = ENVIRON["PICO_DESKTOP_PATH"]
      split("show_fps volume foreground_sleep_ms splore_filter check_for_cart_updates", keys, " ")
      for (i in keys) {
        offered[keys[i]] = 1
      }
    }
    FILENAME == ENVIRON["PICO_OVERRIDES"] {
      if (($1 in offered) && $2 ~ /^[0-9]+$/) {
        value[$1] = $2
      }
      next
    }
    /^cdata_path / { print "cdata_path " cdata; next }
    /^root_path / { print "root_path " root; next }
    /^desktop_path( |$)/ { print "desktop_path " desktop; next }
    ($1 in value) { print $1 " " value[$1]; next }
    { print }' "$overrides" "$PAK_DIR/config/$PLATFORM.txt" >"$(get_working_config)"

  install_if_changed "$(get_working_config)" "$HOME/config.txt"
}

# config.txt is written here first, in memory, so the copy on the SD card is
# only rewritten when it has to be
get_working_config() {
  echo "/tmp/$PAK_NAME-config.txt"
}

# Puts $1 in place at $2 only when they differ, as every write to the SD card
# waits for the card. pico-8 saves its own config.txt on exit, so that one
# usually differs, but the controller mapping rarely does.
install_if_changed() {
  if [ -f "$2" ] && command -v cmp >/dev/null 2>&1 && cmp -s "$1" "$2"; then
    return 0
  fi

  cp -f "$1" "$2"
}

# The PICO-8 settings changed in the in-game menu, which install_config puts in
# config.txt on the next launch
get_pico8_config_overrides() {
  echo "$USERDATA_PATH/Pico-8-native/pico8-config"
}

# Saves used to live beside the pak's other shared userdata. Move them into the
# folder MinUI and NextUI keep saves in, once, and record that it happened: were
# pico-8 to ignore cdata_path and write back to the old folder, an ungated
# migration would move those saves out from under it on every launch.
migrate_saves() {
  saves_dir="$(get_saves_dir)"
  mkdir -p "$saves_dir"

  # install_splore_cart has a marker_file of its own and this shell has no
  # locals, so the two markers need separate names
  saves_marker="$USERDATA_PATH/Pico-8-native/saves-migrated"
  if [ -f "$saves_marker" ]; then
    return 0
  fi

  legacy_dir="$HOME/cdata"
  if [ ! -d "$legacy_dir" ]; then
    true >"$saves_marker"
    return 0
  fi

  for legacy_save in "$legacy_dir"/*; do
    [ -e "$legacy_save" ] || continue

    save_name="${legacy_save##*/}"
    if [ -e "$saves_dir/$save_name" ]; then
      echo "$save_name already exists in $saves_dir, leaving the copy in $legacy_dir" 1>&2
      continue
    fi

    echo "Moving $save_name to $saves_dir" 1>&2
    mv -f "$legacy_save" "$saves_dir/$save_name" || true
  done

  # only removes an empty folder, so a save left behind keeps the old one around
  rmdir "$legacy_dir" 2>/dev/null || true

  true >"$saves_marker"
  sync
}

get_network_probe_file() {
  echo "/tmp/$PAK_NAME-network-probe"
}

# the sysfs directory is overridable so the test suite can fake a link state
is_network_link_up() {
  net_dir="${PICO_PAK_NET_DIR:-/sys/class/net}"

  for interface_dir in "$net_dir"/*; do
    [ -d "$interface_dir" ] || continue

    interface="${interface_dir##*/}"
    if [ "$interface" = "lo" ]; then
      continue
    fi

    if [ "$(cat "$interface_dir/operstate" 2>/dev/null)" = "up" ]; then
      return 0
    fi
  done

  return 1
}

# Returns 0 when the servers responded, 1 when they could not be reached, and 2
# when the check itself could not run, such as a wget shim whose libraries are
# missing, so the two failures can be told apart on screen.
is_internet_reachable() {
  probe_url="https://www.lexaloffle.com/"
  probe_file="$(get_network_probe_file)"
  rm -f "$probe_file"

  # every platform ships its own wget shim, and two of them are curl wrappers
  # that read only the url and the output path, so no timeout flag survives
  if command -v timeout >/dev/null 2>&1; then
    timeout 10s wget "$probe_url" -q -O "$probe_file"
  else
    wget "$probe_url" -q -O "$probe_file"
  fi
  probe_status=$?

  # a failed fetch leaves an empty file behind on every platform
  if [ -s "$probe_file" ]; then
    rm -f "$probe_file"
    return 0
  fi

  rm -f "$probe_file"
  echo "Connectivity probe of $probe_url exited with status $probe_status" 1>&2

  # 126 and 127 are the shell's and the loader's codes for a program that could
  # not be executed at all, and timeout passes them through unchanged
  if [ "$probe_status" -eq 126 ] || [ "$probe_status" -eq 127 ]; then
    return 2
  fi

  return 1
}

is_splore_cart() {
  case "$1" in
  *"Splore"* | *"splore"*)
    return 0
    ;;
  esac

  return 1
}

# Whether the cart being started is Splore, decided once by main from the
# cart's file name. Every step that differs for it reads SPLORE_SESSION rather
# than asking again: the cart check, the connection check, how pico-8 is
# started and the carts copied after.
SPLORE_SESSION=false

set_splore_session() {
  SPLORE_SESSION=false
  if is_splore_cart "${1##*/}"; then
    SPLORE_SESSION=true
  fi
}

rom_folder_has_splore_cart() {
  splore_folder="$1"

  for splore_entry in "$splore_folder"/*; do
    [ -f "$splore_entry" ] || continue
    if is_splore_cart "${splore_entry##*/}"; then
      return 0
    fi
  done

  return 1
}

# The in-game menu saves the Splore cart's cover here, as standard or fancy
get_splore_artwork_file() {
  echo "$USERDATA_PATH/Pico-8-native/splore-artwork"
}

get_splore_artwork() {
  splore_artwork_file="$(get_splore_artwork_file)"
  if [ -f "$splore_artwork_file" ] && [ "$(tr -d '\r' <"$splore_artwork_file")" = "fancy" ]; then
    echo "$PAK_DIR/splore/Splore-fancy.p8.png"
    return 0
  fi

  echo "$PAK_DIR/splore/Splore.p8.png"
}

install_splore_cart() {
  marker_file="$USERDATA_PATH/Pico-8-native/splore-installed"
  if [ -f "$marker_file" ]; then
    return 0
  fi

  source_cart="$PAK_DIR/splore/Splore.p8.png"
  if [ ! -f "$source_cart" ]; then
    echo "Splore cart $source_cart is missing, skipping" 1>&2
    return 0
  fi
  source_artwork="$(get_splore_artwork)"

  seeded=false
  for rom_folder in "$SDCARD_PATH/Roms/"*"($PAK_NAME)"; do
    [ -d "$rom_folder" ] || continue

    if rom_folder_has_splore_cart "$rom_folder"; then
      seeded=true
      continue
    fi

    echo "Creating Splore.p8 in $rom_folder" 1>&2
    if ! cp -f "$source_cart" "$rom_folder/Splore.p8"; then
      echo "Unable to create Splore.p8 in $rom_folder" 1>&2
      continue
    fi

    seeded=true

    # NextUI drops the cart's last extension and MinUI keeps the whole name, so
    # the same artwork is written under both folders and both spellings
    mkdir -p "$rom_folder/.media" "$rom_folder/.res"
    if [ ! -f "$rom_folder/.media/Splore.png" ]; then
      cp -f "$source_artwork" "$rom_folder/.media/Splore.png"
    fi
    if [ ! -f "$rom_folder/.res/Splore.p8.png" ]; then
      cp -f "$source_artwork" "$rom_folder/.res/Splore.p8.png"
    fi
  done

  if [ "$seeded" = "true" ]; then
    true >"$marker_file"
    sync
  fi

  return 0
}

launch_cart() {
  ROM_PATH="$1"
  ROM_FOLDER="${ROM_PATH%/*}"
  ROM_NAME="${ROM_PATH##*/}"

  log_step "setting up pico-8"
  install_if_changed "$PAK_DIR/controllers/$(get_controller_file)" "$HOME/sdl_controllers.txt"
  untraced install_config "$ROM_FOLDER"
  untraced set_cpu_speed

  pico_bin="$(get_pico_bin)"

  # only set LD_LIBRARY_PATH for pico8: minui-presenter is an SDL application
  # too, and a library path set for the whole launcher would have it load the
  # pak's own SDL2 instead of the one the device shipped with.
  #
  # The inherited value is appended conditionally because an empty element means
  # the current directory, which main has already cd'd to $PAK_DIR.
  inherited_library_path="${LD_LIBRARY_PATH:-}"
  export LD_LIBRARY_PATH="$PAK_DIR/lib/$PLATFORM:$PAK_DIR/lib/$architecture${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  inherited_preload="${LD_PRELOAD:-}"
  untraced setup_sdl_preload
  display_shim_loaded=false
  if untraced setup_display_preload "$ROM_PATH"; then
    display_shim_loaded=true
  fi
  echo "Starting pico-8 with LD_LIBRARY_PATH=$LD_LIBRARY_PATH LD_PRELOAD=${LD_PRELOAD:-}" 1>&2

  # pico-8 is supplied by the user and links against whatever the device has, so
  # record that in the log: a library it cannot find is otherwise a silent
  # failure to launch that no bug report can describe. This has to run after the
  # two variables above are set or it reports a resolution pico-8 never sees.
  if command -v ldd >/dev/null 2>&1; then
    ldd "$EMU_DIR/$pico_bin" || true
  else
    echo "No ldd on this device, cannot record what $pico_bin links against" 1>&2
  fi

  draw_rect=""
  screen_mode="$(get_screen_mode)"
  # pico-8 only reads its draw rect at startup, so with the shim loaded it
  # always starts in standard mode and the shim does any stretching
  if [ "$screen_mode" = "stretched" ] && [ "$display_shim_loaded" = false ]; then
    resolution="$(get_screen_resolution)"
    # a platform with neither fbset nor a known panel falls back to unstretched
    # rather than passing pico-8 a draw rect with empty dimensions
    if [ -n "$resolution" ]; then
      width="$(echo "$resolution" | awk '{print $1}')"
      height="$(echo "$resolution" | awk '{print $2}')"
      draw_rect="-draw_rect 0,0,${width},${height}"
    fi
  fi

  log_step "starting pico-8"
  save_log
  if [ "$SPLORE_SESSION" = true ]; then
    # draw_rect is left unquoted so it splits into separate arguments
    # shellcheck disable=SC2086
    "$EMU_DIR/$pico_bin" \
      -desktop "$SDCARD_PATH/Screenshots" \
      -home "$HOME" \
      -joystick 0 \
      -root_path "$ROM_FOLDER" \
      -splore $draw_rect
  else
    # draw_rect is left unquoted so it splits into separate arguments
    # shellcheck disable=SC2086
    "$EMU_DIR/$pico_bin" \
      -desktop "$SDCARD_PATH/Screenshots" \
      -home "$HOME" \
      -joystick 0 \
      -root_path "$ROM_FOLDER" \
      -run "$ROM_PATH" $draw_rect
  fi
  pico_status=$?
  log_step "pico-8 exited with status $pico_status"

  # the library path and preloads are for pico-8 alone: minui-presenter is SDL
  # too and shows its messages after this, so it needs the device's SDL2 back
  if [ -n "$inherited_library_path" ]; then
    export LD_LIBRARY_PATH="$inherited_library_path"
  else
    unset LD_LIBRARY_PATH
  fi
  if [ -n "$inherited_preload" ]; then
    export LD_PRELOAD="$inherited_preload"
  else
    unset LD_PRELOAD
  fi

  # the saves pico-8 wrote on its way out reach the card before anything else
  log_step "syncing the SD card"
  sync

  # Splore is what downloads carts, so after any other cart there is nothing
  # new to copy and the scan of its cache is skipped. copy_carts syncs what it
  # copies itself.
  if [ "$SPLORE_SESSION" = true ]; then
    log_step "copying Splore carts"
    untraced copy_carts "$ROM_FOLDER"
  else
    echo "Not a Splore session, so no carts to copy" 1>&2
  fi

  # A status above 128 is the signal that ended pico-8, plus 128.
  # minui-power-control ends a session with SIGTERM or SIGKILL, which is the
  # power button working, not a failure worth putting on screen. A signal pico-8
  # raised against itself is a crash, which the display shim describes in the log.
  if [ "$pico_status" -gt 128 ]; then
    crash_signal="$(get_crash_signal "$pico_status")"
    if [ -z "$crash_signal" ]; then
      return 0
    fi
    show_message "PICO-8 crashed with $crash_signal. See $LOGS_PATH/$PAK_NAME.txt for details." 4
    return "$pico_status"
  fi

  # a pico-8 that could not start at all - an SDL subsystem it could not
  # initialise, a library it could not find - otherwise drops the user back to
  # the game list with nothing on screen and no reason given
  if [ "$pico_status" -ne 0 ]; then
    show_message "PICO-8 exited with an error. See $LOGS_PATH/$PAK_NAME.txt for details." 4
  fi

  return "$pico_status"
}

# Names the signal in an exit status that is a crash: an illegal instruction, an
# abort, a floating point error, a bus error or a segmentation fault. Anything
# else, such as power control's SIGTERM and SIGKILL, prints nothing.
get_crash_signal() {
  case "$1" in
  132) echo "SIGILL" ;;
  134) echo "SIGABRT" ;;
  135) echo "SIGBUS" ;;
  136) echo "SIGFPE" ;;
  139) echo "SIGSEGV" ;;
  esac
}

verify_platform() {
  if ! platform_in_list "$PLATFORM" "$SUPPORTED_PLATFORMS"; then
    show_message "$PLATFORM is not a supported platform" 2
    return 1
  fi

  if ! command -v minui-presenter >/dev/null 2>&1; then
    show_message "minui-presenter not found" 2
    return 1
  fi

  if ! command -v wget >/dev/null 2>&1; then
    show_message "wget not found" 2
    return 1
  fi
}

verify_cart() {
  cart_path="$1"

  if [ -z "$cart_path" ]; then
    show_message "No cart was specified." 4
    return 1
  fi

  # the shell's own string handling, as each command started costs time here
  cart_name="${cart_path##*/}"

  if [ ! -f "$cart_path" ]; then
    show_message "Cart file $cart_name does not exist." 4
    return 1
  fi

  if [ "$SPLORE_SESSION" = true ]; then
    return 0
  fi

  case "$cart_name" in
  *.[pP]8 | *.[pP][nN][gG])
    return 0
    ;;
  esac

  show_message "Cart file $cart_name is not a supported filetype. Only .p8, .p8.png and .png carts can be loaded." 4
  return 1
}

# Only run for Splore, which main decides, as no other cart needs the network.
verify_splore_connection() {
  # show_message and show_confirmation both assign to a variable named message,
  # and this shell has no locals, so the prompt text needs its own name
  splore_message=""
  if ! is_network_link_up; then
    splore_message="No network connection was detected. Use Wifi.pak to connect. Splore can still browse the carts already downloaded to this device."
  else
    show_message "Checking the connection to the Splore servers." forever
    is_internet_reachable
    splore_probe_status=$?
    if [ "$splore_probe_status" -eq 2 ]; then
      splore_message="The Splore connection check could not run. See $LOGS_PATH/$PAK_NAME.txt for details. Splore can still browse the carts already downloaded to this device."
    elif [ "$splore_probe_status" -ne 0 ]; then
      splore_message="The Lexaloffle servers could not be reached. Check your connection. Splore can still browse the carts already downloaded to this device."
    fi
    killall minui-presenter >/dev/null 2>&1 || true
  fi

  if [ -z "$splore_message" ]; then
    return 0
  fi

  # normalise the presenter's exit code so the gate matches its verify_ siblings
  if show_confirmation "$splore_message"; then
    return 0
  fi

  return 1
}

# pico-8 finds pico8.dat beside its own binary and writes nothing there, so it
# runs from the BIOS folder as it is. Copying both into the pak made the first
# start after every install slow, and kept a copy a newer BIOS never replaced.
verify_pico_files() {
  pico_bin="$(get_pico_bin)"

  if [ ! -f "$EMU_DIR/$pico_bin" ] || [ ! -f "$EMU_DIR/pico8.dat" ]; then
    show_message "Missing $pico_bin or pico8.dat. Please copy them to the Bios/PICO directory at the root of your SD card." 4
    return 1
  fi
}

show_message() {
  message="$1"
  seconds="$2"

  if [ -z "$seconds" ]; then
    seconds="forever"
  fi

  killall minui-presenter >/dev/null 2>&1 || true
  echo "$message" 1>&2
  if [ "$seconds" = "forever" ]; then
    minui-presenter --message "$message" --timeout -1 &
  else
    minui-presenter --message "$message" --timeout "$seconds"
  fi
}

show_confirmation() {
  message="$1"
  confirm_text="${2:-CONTINUE}"
  cancel_text="${3:-EXIT}"

  killall minui-presenter >/dev/null 2>&1 || true
  echo "$message" 1>&2
  minui-presenter \
    --cancel-show \
    --cancel-text "$cancel_text" \
    --confirm-show \
    --confirm-text "$confirm_text" \
    --message "$message" \
    --timeout 0
}

# minui-power-control as `make build` unpacks it from its release archive, so it
# starts at once rather than checking and unpacking itself into /tmp first. It
# gives up when pico-8 has not started within ten seconds, which a slow start
# used to come close to.
get_power_control() {
  echo "$PAK_DIR/bin/power-control/launch"
}

# The cart lets a device without deep sleep, which powers off when left asleep,
# start the cart again on its next boot. Power control bundles no presenter of
# its own and shows its messages with the one in bin/$PLATFORM, which PATH
# already names and verify_platform has already required.
start_power_control() {
  if ! platform_in_list "$PLATFORM" "$POWER_CONTROL_PLATFORMS"; then
    echo "minui-power-control does not support $PLATFORM, deep sleep is unavailable" 1>&2
    return 0
  fi

  power_control="$(get_power_control)"
  if [ ! -f "$power_control" ]; then
    echo "No power control at $power_control, deep sleep is unavailable" 1>&2
    return 0
  fi

  "$power_control" "$(get_pico_bin)" "$1" &
}

# Marks a step of starting or tearing down in the log with the seconds since
# boot, the clock the display shim stamps its own lines with. /proc/uptime has
# it to the hundredth on every device, where busybox date has no sub-second
# format. The file is overridable so the test suite can fix the time.
log_step() {
  step_time="$(cut -d' ' -f1 "${PICO_PAK_UPTIME_FILE:-/proc/uptime}" 2>/dev/null)"
  echo "[${step_time:-?}] $*" 1>&2
}

# Runs a command with tracing off and puts it back as it was. The log is on the
# SD card, where every traced line is its own write costing tens of
# milliseconds, which added seconds to starting and exiting a game. The steps
# run this way log what matters themselves. Not for nesting: the shell has no
# locals, so an inner call would overwrite the outer one's saved state.
untraced() {
  case $- in *x*) untraced_was_traced=true ;; *) untraced_was_traced=false ;; esac
  set +x
  "$@"
  untraced_status=$?
  if [ "$untraced_was_traced" = "true" ]; then
    set -x
  fi
  return "$untraced_status"
}

# The steps main runs in the background while the cart is checked, which have
# to finish before pico-8 starts, or before the pak exits when it never does
background_pids=""

wait_for_background() {
  if [ -n "$background_pids" ]; then
    # word splitting is wanted, the list is pids alone
    # shellcheck disable=SC2086
    wait $background_pids
    background_pids=""
  fi
}

# The log is written to /tmp while the pak runs and copied to the SD card, as
# each write to the card is synchronous on these devices: every logged line
# cost tens of milliseconds, which added seconds to starting and exiting a game.
get_working_log() {
  echo "/tmp/$PAK_NAME-log.txt"
}

get_log() {
  echo "$LOGS_PATH/$PAK_NAME.txt"
}

# Copies the log so far to the SD card in one write. It runs before pico-8
# starts, so a device that loses power mid-game keeps how the game started,
# and once more at the very end.
save_log() {
  cp -f "$(get_working_log)" "$(get_log)" 2>/dev/null || true
}

cleanup() {
  log_step "cleaning up"
  wait_for_background
  rm -f /tmp/stay_awake
  rm -f "$(get_network_probe_file)"
  rm -rf "$(get_copy_carts_tmp_dir)"
  rm -f "$(get_working_config)"
  killall minui-presenter >/dev/null 2>&1 || true
  log_step "done, returning to the launcher"
  save_log
}

main() {
  set -x

  rm -f "$(get_working_log)" "$(get_log)"
  exec >>"$(get_working_log)"
  exec 2>&1

  echo "$0" "$*"
  log_step "starting"
  cd "$PAK_DIR" || exit 1
  mkdir -p "$USERDATA_PATH/Pico-8-native"
  mkdir -p "$SHARED_USERDATA_PATH/Pico-8-native"

  echo "1" >/tmp/stay_awake
  trap "cleanup" EXIT INT TERM HUP QUIT

  if [ "$PLATFORM" = "tg3040" ] && [ -z "$DEVICE" ]; then
    export DEVICE="brick"
    export PLATFORM="tg5040"
  fi

  ROM_PATH="$1"
  set_splore_session "$ROM_PATH"
  read_resume_slot

  if ! verify_platform; then
    return 1
  fi

  # Neither of these depends on the other or on the checks below, so they run
  # alongside them. Saves move to the folder MinUI and NextUI use before
  # anything writes to either one, which is pico-8 alone, and seeding is
  # best-effort and must never block a launch. Both are waited for before
  # pico-8 starts, and by cleanup when a check stops the launch.
  log_step "migrating saves and seeding the Splore cart"
  untraced migrate_saves &
  background_pids="$!"
  untraced install_splore_cart &
  background_pids="$background_pids $!"

  log_step "checking the cart"
  if ! untraced verify_cart "$ROM_PATH"; then
    return 1
  fi

  log_step "checking the PICO-8 files"
  if ! untraced verify_pico_files; then
    return 1
  fi

  if [ "$SPLORE_SESSION" = true ]; then
    log_step "checking the Splore connection"
    if ! untraced verify_splore_connection; then
      return 1
    fi
  fi

  log_step "waiting for the saves and the Splore cart"
  wait_for_background

  # minui-power-control also powers the device off on a long press, so there is
  # no shutdown left for the pak to handle once pico-8 exits
  log_step "starting power control"
  start_power_control "$ROM_PATH"

  launch_cart "$ROM_PATH"
}

if [ -z "$PICO_PAK_SOURCE_ONLY" ]; then
  main "$@"
fi
