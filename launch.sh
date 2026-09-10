#!/bin/sh
PAK_DIR="$(dirname "$0")"
PAK_NAME="$(basename "$PAK_DIR")"
PAK_NAME="${PAK_NAME%.*}"

architecture=arm
if uname -m | grep -q '64'; then
  architecture=arm64
fi

export EMU_DIR="$PAK_DIR/pico8"
export HOME="$SHARED_USERDATA_PATH/Pico-8-native"
export PATH="$EMU_DIR:$PAK_DIR/bin/$PLATFORM:$PAK_DIR/bin/$architecture:$PAK_DIR/bin:$PATH"
export XDG_CONFIG_HOME="$HOME/config"
export XDG_DATA_HOME="$HOME/data"

export GAMESETTINGS_DIR="$HOME/game-settings/$ROM_NAME"
export SCREENSHOT_DIR="$SDCARD_PATH/Screenshots"

# The platforms this pak supports. test/makefile.bats asserts this agrees with
# pak.json and with the Makefile.
SUPPORTED_PLATFORMS="h700 rg35xxplus tg5040 tg5050"

# minui-power-control refuses to start on a platform its own launcher does not
# list, so calling it elsewhere only logs noise. Keep this in sync with the
# release the Makefile pins.
POWER_CONTROL_PLATFORMS="miyoomini my355 rg35xxplus tg5040 tg5050"

# scaling_setspeed only takes effect under the userspace governor and the value
# is a per SoC frequency, so the write is opt in per platform.
CPU_SETSPEED_PLATFORMS="rg35xxplus tg5040"

copy_carts() {
  ROM_FOLDER="$1"
  [ ! -f "$USERDATA_PATH/Pico-8-native/copy-carts" ] && return

  FAV_FILE="$HOME/favourites.txt"
  MAP_FILE="$ROM_FOLDER/map.txt"
  MEDIA_FOLDER="$ROM_FOLDER/.media"

  [ ! -f "$FAV_FILE" ] && return

  mkdir -p "$MEDIA_FOLDER"
  true >"$MAP_FILE"
  tr -d '\r' <"$FAV_FILE" >"$FAV_FILE.tmp" && mv "$FAV_FILE.tmp" "$FAV_FILE"

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

				if (acr[lw]) {
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
			print out
		}'
  }

  # read from the file rather than a pipe so the loop does not run in a subshell
  while IFS='|' read -r _ filename_raw _ _ _ _ full_title; do
    filename_raw="${filename_raw#"${filename_raw%%[![:space:]]*}"}"
    filename_raw="${filename_raw%"${filename_raw##*[![:space:]]}"}"

    if [ -z "$filename_raw" ]; then
      continue
    fi

    full_title="${full_title#"${full_title%%[![:space:]]*}"}"
    full_title="${full_title%"${full_title##*[![:space:]]}"}"

    full_title="$(titlecase "$full_title")"
    filename_png="$filename_raw.p8.png"

    case "$filename_raw" in
    [0-9]*) CART_PATH="$HOME/bbs/$(printf "%s" "$filename_raw" | cut -c1)/$filename_png" ;;
    *) CART_PATH="$HOME/bbs/carts/$filename_png" ;;
    esac

    if [ -f "$CART_PATH" ]; then
      [ ! -f "$ROM_FOLDER/$filename_png" ] && cp -f "$CART_PATH" "$ROM_FOLDER/$filename_png"
      [ ! -f "$MEDIA_FOLDER/$filename_png" ] && cp -f "$CART_PATH" "$MEDIA_FOLDER/$filename_png"
      printf "%s\t%s\n" "$filename_png" "$full_title" >>"$MAP_FILE"
    fi
  done <"$FAV_FILE"

  sync
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
    # both platforms are Allwinner H700 hardware and report the same joystick
    # GUIDs, so one file holds every mapping and SDL picks by GUID
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

# config.txt has no command line equivalent for cdata_path, and pico-8 writes
# cart saves there, so it has to name this device's SD card rather than the
# mount the checked in template happened to be captured on. root_path is
# rewritten alongside it so the file agrees with the -root_path already passed
# on the command line.
install_config() {
  rom_folder="$1"

  # ENVIRON rather than awk -v, which would process backslash escapes in a path
  PICO_CDATA_PATH="$HOME/cdata/" PICO_ROOT_PATH="$rom_folder/" awk '
    BEGIN {
      cdata = ENVIRON["PICO_CDATA_PATH"]
      root = ENVIRON["PICO_ROOT_PATH"]
    }
    /^cdata_path / { print "cdata_path " cdata; next }
    /^root_path / { print "root_path " root; next }
    { print }' "$PAK_DIR/config/$PLATFORM.txt" >"$HOME/config.txt"
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

is_internet_reachable() {
  probe_url="https://www.lexaloffle.com/robots.txt"
  probe_file="$(get_network_probe_file)"
  rm -f "$probe_file"

  # every platform ships its own wget shim, and two of them are curl wrappers
  # that read only the url and the output path, so no timeout flag survives
  if command -v timeout >/dev/null 2>&1; then
    timeout 10s wget "$probe_url" -q -O "$probe_file" || true
  else
    wget "$probe_url" -q -O "$probe_file" || true
  fi

  # a failed fetch leaves an empty file behind on every platform
  if [ -s "$probe_file" ]; then
    rm -f "$probe_file"
    return 0
  fi

  rm -f "$probe_file"
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
    mkdir -p "$rom_folder/.media"
    if [ ! -f "$rom_folder/.media/Splore.png" ]; then
      cp -f "$source_cart" "$rom_folder/.media/Splore.png"
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
  ROM_FOLDER="$(dirname "$ROM_PATH")"
  ROM_NAME="$(basename "$ROM_PATH")"

  cp -f "$PAK_DIR/controllers/$(get_controller_file)" "$HOME/sdl_controllers.txt"
  install_config "$ROM_FOLDER"
  set_cpu_speed

  pico_bin="$(get_pico_bin)"

  # only set LD_LIBRARY_PATH for pico8
  export LD_LIBRARY_PATH="$EMU_DIR/lib:$PAK_DIR/lib/$PLATFORM:$PAK_DIR/lib/$architecture:$LD_LIBRARY_PATH"

  draw_rect=""
  screen_mode="$(get_screen_mode)"
  if [ "$screen_mode" = "stretched" ]; then
    resolution="$(get_screen_resolution)"
    # a platform with neither fbset nor a known panel falls back to unstretched
    # rather than passing pico-8 a draw rect with empty dimensions
    if [ -n "$resolution" ]; then
      width="$(echo "$resolution" | awk '{print $1}')"
      height="$(echo "$resolution" | awk '{print $2}')"
      draw_rect="-draw_rect 0,0,${width},${height}"
    fi
  fi

  if is_splore_cart "$ROM_NAME"; then
    # draw_rect is left unquoted so it splits into separate arguments
    # shellcheck disable=SC2086
    "$pico_bin" \
      -desktop "$SDCARD_PATH/Screenshots" \
      -home "$HOME" \
      -joystick 0 \
      -root_path "$ROM_FOLDER" \
      -splore $draw_rect
  else
    # draw_rect is left unquoted so it splits into separate arguments
    # shellcheck disable=SC2086
    "$pico_bin" \
      -desktop "$SDCARD_PATH/Screenshots" \
      -home "$HOME" \
      -joystick 0 \
      -root_path "$ROM_FOLDER" \
      -run "$ROM_PATH" $draw_rect
  fi

  sync
  copy_carts "$ROM_FOLDER"

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

  cart_name="$(basename "$cart_path")"

  if [ ! -f "$cart_path" ]; then
    show_message "Cart file $cart_name does not exist." 4
    return 1
  fi

  if is_splore_cart "$cart_name"; then
    return 0
  fi

  cart_extension="$(printf "%s" "${cart_name##*.}" | tr '[:upper:]' '[:lower:]')"
  case "$cart_extension" in
  p8 | png)
    return 0
    ;;
  esac

  show_message "Cart file $cart_name is not a supported filetype. Only .p8, .p8.png and .png carts can be loaded." 4
  return 1
}

verify_splore_connection() {
  splore_cart_name="$(basename "$1")"

  if ! is_splore_cart "$splore_cart_name"; then
    return 0
  fi

  # show_message and show_confirmation both assign to a variable named message,
  # and this shell has no locals, so the prompt text needs its own name
  splore_message=""
  if ! is_network_link_up; then
    splore_message="No network connection was detected. Use Wifi.pak to connect. Splore can still browse the carts already downloaded to this device."
  else
    show_message "Checking the connection to the Splore servers." forever
    if ! is_internet_reachable; then
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

install_pico_files() {
  pico_bin="$(get_pico_bin)"

  mkdir -p "$EMU_DIR"
  if [ ! -f "$EMU_DIR/$pico_bin" ] && [ -f "$SDCARD_PATH/Bios/PICO/$pico_bin" ]; then
    show_message "Copying $pico_bin to $EMU_DIR" forever
    cp -f "$SDCARD_PATH/Bios/PICO/$pico_bin" "$EMU_DIR/$pico_bin"
  fi

  if [ ! -f "$EMU_DIR/pico8.dat" ] && [ -f "$SDCARD_PATH/Bios/PICO/pico8.dat" ]; then
    show_message "Copying pico8.dat to $EMU_DIR" forever
    cp -f "$SDCARD_PATH/Bios/PICO/pico8.dat" "$EMU_DIR/pico8.dat"
  fi

  if [ ! -f "$EMU_DIR/$pico_bin" ] || [ ! -f "$EMU_DIR/pico8.dat" ]; then
    show_message "Missing $pico_bin or pico8.dat. Please copy them to the Bios/PICO directory at the root of your SD card." 4
    return 1
  fi

  # pico-8 is supplied by the user and links against whatever the device has, so
  # record that in the log: a library it cannot find is otherwise a silent
  # failure to launch that no bug report can describe
  if command -v ldd >/dev/null 2>&1; then
    ldd "$EMU_DIR/$pico_bin" || true
  fi

  killall minui-presenter >/dev/null 2>&1 || true
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

start_power_control() {
  if ! platform_in_list "$PLATFORM" "$POWER_CONTROL_PLATFORMS"; then
    echo "minui-power-control does not support $PLATFORM, deep sleep is unavailable" 1>&2
    return 0
  fi

  minui-power-control "$(get_pico_bin)" &
}

cleanup() {
  rm -f /tmp/stay_awake
  rm -f "$(get_network_probe_file)"
  killall minui-presenter >/dev/null 2>&1 || true
}

main() {
  set -x

  rm -f "$LOGS_PATH/$PAK_NAME.txt"
  exec >>"$LOGS_PATH/$PAK_NAME.txt"
  exec 2>&1

  echo "$0" "$*"
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

  if ! verify_platform; then
    return 1
  fi

  # seeding is best-effort and must never block a launch
  install_splore_cart

  if ! verify_cart "$ROM_PATH"; then
    return 1
  fi

  if ! install_pico_files; then
    return 1
  fi

  if ! verify_splore_connection "$ROM_PATH"; then
    return 1
  fi

  start_power_control

  if ! launch_cart "$ROM_PATH"; then
    return 1
  fi

  # handle the power-button pressed event
  if [ -f /tmp/shutdown_from_pak ]; then
    AUTO_RESUME_FILE="$SHARED_USERDATA_PATH/.minui/auto_resume.txt"
    echo "$ROM_PATH" >"$AUTO_RESUME_FILE"
    sync
    rm /tmp/minui_exec
    shutdown
    while :; do
      sleep 1
    done
  fi
}

if [ -z "$PICO_PAK_SOURCE_ONLY" ]; then
  main "$@"
fi
