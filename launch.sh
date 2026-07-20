#!/bin/sh
PAK_DIR="$(dirname "$0")"
PAK_NAME="$(basename "$PAK_DIR")"
PAK_NAME="${PAK_NAME%.*}"
set -x

rm -f "$LOGS_PATH/$PAK_NAME.txt"
exec >>"$LOGS_PATH/$PAK_NAME.txt"
exec 2>&1

echo "$0" "$*"
cd "$PAK_DIR" || exit 1
mkdir -p "$USERDATA_PATH/Pico-8-native"
mkdir -p "$SHARED_USERDATA_PATH/Pico-8-native"

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

copy_carts() {
    ROM_FOLDER="$1"
    [ ! -f "$USERDATA_PATH/Pico-8-native/copy-carts" ] && return

    FAV_FILE="$HOME/favourites.txt"
    MAP_FILE="$ROM_FOLDER/map.txt"
    MEDIA_FOLDER="$ROM_FOLDER/.media"

    [ ! -f "$FAV_FILE" ] && return

    mkdir -p "$MEDIA_FOLDER"
    true > "$MAP_FILE"
    tr -d '\r' < "$FAV_FILE" > "$FAV_FILE.tmp" && mv "$FAV_FILE.tmp" "$FAV_FILE"

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

    grep -v '^$' "$FAV_FILE" | while IFS='|' read -r _ filename_raw _ _ _ _ full_title; do
        filename_raw="${filename_raw#"${filename_raw%%[![:space:]]*}"}"
        filename_raw="${filename_raw%"${filename_raw##*[![:space:]]}"}"

        full_title="${full_title#"${full_title%%[![:space:]]*}"}"
        full_title="${full_title%"${full_title##*[![:space:]]}"}"

        full_title="$(titlecase "$full_title")"
        filename_png="$filename_raw.p8.png"

        case "$filename_raw" in
            [0-9]*) CART_PATH="$HOME/bbs/${filename_raw:0:1}/$filename_png" ;;
            *)      CART_PATH="$HOME/bbs/carts/$filename_png" ;;
        esac

        if [ -f "$CART_PATH" ]; then
            [ ! -f "$ROM_FOLDER/$filename_png" ] && cp -f "$CART_PATH" "$ROM_FOLDER/$filename_png"
            [ ! -f "$MEDIA_FOLDER/$filename_png" ] && cp -f "$CART_PATH" "$MEDIA_FOLDER/$filename_png"
            printf "%s\t%s\n" "$filename_png" "$full_title" >> "$MAP_FILE"
        fi
    done

    sync
}

get_screen_mode() {
    if [ ! -f "$USERDATA_PATH/Pico-8-native/screen-mode" ]; then
        echo "normal" >"$USERDATA_PATH/Pico-8-native/screen-mode"
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

get_controller_file() {
    if [ "$PLATFORM" = "rg35xxplus" ]; then
        case "$DEVICE" in
        "cube")
            echo "rg35xxplus-cube.txt"
            ;;
        *)
            echo "rg35xxplus.txt"
            ;;
        esac
    else
        echo "$PLATFORM.txt"
    fi
}

launch_cart() {
    ROM_PATH="$1"
    cp -f "$PAK_DIR/controllers/$(get_controller_file)" "$HOME/sdl_controllers.txt"
    cp -f "$PAK_DIR/config/$PLATFORM.txt" "$HOME/config.txt"

    if [ "$PLATFORM" != "tg5050" ]; then
        echo 1600000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
    fi

    pico_bin="$(get_pico_bin)"

    ROM_FOLDER="$(dirname "$ROM_PATH")"
    ROM_NAME="$(basename "$ROM_PATH")"

    # only set LD_LIBRARY_PATH for pico8
    export LD_LIBRARY_PATH="$EMU_DIR/lib:$PAK_DIR/lib/$PLATFORM:$PAK_DIR/lib/$architecture:$LD_LIBRARY_PATH"

    draw_rect=""
    screen_mode="$(get_screen_mode)"
    if [ "$screen_mode" = "stretched" ] && command -v fbset >/dev/null 2>&1; then
        resolution="$(fbset | grep 'geometry' | awk '{print $2,$3}')"
        width="$(echo "$resolution" | awk '{print $1}')"
        height="$(echo "$resolution" | awk '{print $2}')"
        draw_rect="-draw_rect 0,0,${width},${height}"
    fi

    case "$ROM_NAME" in
    *"Splore"* | *"splore"*)
        enabled="$(cat /sys/class/net/wlan0/operstate 2>/dev/null)"
        if [ "$enabled" != "up" ]; then
            show_message "Required wifi connection is not available." 2
            return 1
        fi

        "$pico_bin" \
            -desktop "$SDCARD_PATH/Screenshots" \
            -home "$HOME" \
            -joystick 0 \
            -root_path "$ROM_FOLDER" \
            -splore $draw_rect

        ;;
    *)
        "$pico_bin" \
            -desktop "$SDCARD_PATH/Screenshots" \
            -home "$HOME" \
            -joystick 0 \
            -root_path "$ROM_FOLDER" \
            -run "$ROM_PATH" $draw_rect
        ;;
    esac

    sync
    copy_carts "$ROM_FOLDER"

}

verify_platform() {
    allowed_platforms="rg35xxplus tg5040 tg5050 trimuismart"
    if ! echo "$allowed_platforms" | grep -q "$PLATFORM"; then
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

cleanup() {
    rm -f /tmp/stay_awake
    killall minui-presenter >/dev/null 2>&1 || true
}

main() {
    echo "1" >/tmp/stay_awake
    trap "cleanup" EXIT INT TERM HUP QUIT

    if [ "$PLATFORM" = "tg3040" ] && [ -z "$DEVICE" ]; then
        export DEVICE="brick"
        export PLATFORM="tg5040"
    fi

    if ! verify_platform; then
        return 1
    fi

    if ! install_pico_files; then
        return 1
    fi

    minui-power-control "$(get_pico_bin)" &

    ROM_PATH="$1"
    launch_cart "$ROM_PATH"

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

main "$@"
