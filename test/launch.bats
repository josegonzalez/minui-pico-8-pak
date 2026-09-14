#!/usr/bin/env bats

setup() {
  load test_helper
  setup_launch
}

@test "verify_cart accepts a p8 cart" {
  run verify_cart "$(make_cart Game.p8)"
  [ "$status" -eq 0 ]
}

@test "verify_cart accepts a p8.png cart" {
  run verify_cart "$(make_cart Game.p8.png)"
  [ "$status" -eq 0 ]
}

@test "verify_cart accepts a plain png cart" {
  run verify_cart "$(make_cart Game.png)"
  [ "$status" -eq 0 ]
}

@test "verify_cart accepts an uppercase P8 extension" {
  run verify_cart "$(make_cart GAME.P8)"
  [ "$status" -eq 0 ]
}

@test "verify_cart accepts an uppercase PNG extension" {
  run verify_cart "$(make_cart Game.PNG)"
  [ "$status" -eq 0 ]
}

@test "verify_cart accepts a mixed case extension" {
  run verify_cart "$(make_cart Game.Png)"
  [ "$status" -eq 0 ]
}

@test "verify_cart accepts the splore cart" {
  run verify_cart "$(make_cart Splore.p8)"
  [ "$status" -eq 0 ]
}

@test "verify_cart accepts a sorted splore cart" {
  run verify_cart "$(make_cart "1) Splore.p8.png")"
  [ "$status" -eq 0 ]
}

@test "verify_cart accepts a splore cart with no extension" {
  run verify_cart "$(make_cart splore)"
  [ "$status" -eq 0 ]
}

@test "verify_cart rejects a txt file" {
  run verify_cart "$(make_cart Game.txt)"
  [ "$status" -eq 1 ]
  [[ "$output" == *"is not a supported filetype"* ]]
}

@test "verify_cart rejects a zip file" {
  run verify_cart "$(make_cart Game.zip)"
  [ "$status" -eq 1 ]
}

@test "verify_cart rejects an m3u file" {
  run verify_cart "$(make_cart Poom.m3u)"
  [ "$status" -eq 1 ]
}

@test "verify_cart rejects a file with no extension" {
  run verify_cart "$(make_cart README)"
  [ "$status" -eq 1 ]
}

@test "verify_cart rejects a directory" {
  mkdir -p "$CARTS/Poom"
  run verify_cart "$CARTS/Poom"
  [ "$status" -eq 1 ]
  [[ "$output" == *"does not exist"* ]]
}

@test "verify_cart rejects a path that does not exist" {
  run verify_cart "$CARTS/Missing.p8"
  [ "$status" -eq 1 ]
  [[ "$output" == *"does not exist"* ]]
}

@test "verify_cart rejects an empty argument" {
  run verify_cart ""
  [ "$status" -eq 1 ]
  [[ "$output" == *"No cart was specified"* ]]
}

@test "is_splore_cart matches both capitalizations" {
  run is_splore_cart "Splore.p8"
  [ "$status" -eq 0 ]
  run is_splore_cart "splore.p8"
  [ "$status" -eq 0 ]
}

@test "is_splore_cart does not match an ordinary cart" {
  run is_splore_cart "Freecell.p8"
  [ "$status" -eq 1 ]
}

@test "rom_folder_has_splore_cart finds an existing splore cart" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  : >"$rom_folder/1) Splore.p8.png"

  run rom_folder_has_splore_cart "$rom_folder"
  [ "$status" -eq 0 ]
}

@test "rom_folder_has_splore_cart ignores ordinary carts" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  : >"$rom_folder/Freecell.p8"

  run rom_folder_has_splore_cart "$rom_folder"
  [ "$status" -eq 1 ]
}

@test "rom_folder_has_splore_cart ignores an empty folder" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"

  run rom_folder_has_splore_cart "$rom_folder"
  [ "$status" -eq 1 ]
}

@test "rom_folder_has_splore_cart ignores a directory named splore" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  mkdir -p "$rom_folder/Splore"

  run rom_folder_has_splore_cart "$rom_folder"
  [ "$status" -eq 1 ]
}

@test "rom_folder_has_splore_cart matches the file name not the folder path" {
  rom_folder="$(make_rom_folder "Splore Carts (PICO)")"
  : >"$rom_folder/Freecell.p8"

  run rom_folder_has_splore_cart "$rom_folder"
  [ "$status" -eq 1 ]
}

@test "install_splore_cart seeds a tagged roms folder" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"

  run install_splore_cart
  [ "$status" -eq 0 ]

  [ -f "$rom_folder/Splore.p8" ]
  [ -f "$rom_folder/.media/Splore.png" ]
  [ -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
  cmp -s "$REPO_ROOT/splore/Splore.p8.png" "$rom_folder/Splore.p8"
  cmp -s "$REPO_ROOT/splore/Splore.p8.png" "$rom_folder/.media/Splore.png"
}

@test "install_splore_cart seeds every tagged roms folder" {
  first="$(make_rom_folder "Pico-8 (PICO)")"
  second="$(make_rom_folder "Extra Pico (PICO)")"

  run install_splore_cart
  [ "$status" -eq 0 ]

  [ -f "$first/Splore.p8" ]
  [ -f "$second/Splore.p8" ]
  [ -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
}

@test "install_splore_cart skips a folder that already has a splore cart" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  : >"$rom_folder/1) Splore.p8.png"

  run install_splore_cart
  [ "$status" -eq 0 ]

  [ ! -f "$rom_folder/Splore.p8" ]
  [ ! -d "$rom_folder/.media" ]
  [ -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
}

@test "install_splore_cart does not recreate a cart the user deleted" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  : >"$USERDATA_PATH/Pico-8-native/splore-installed"

  run install_splore_cart
  [ "$status" -eq 0 ]

  [ ! -f "$rom_folder/Splore.p8" ]
  [ ! -d "$rom_folder/.media" ]
}

@test "install_splore_cart does not write the marker without a tagged roms folder" {
  run install_splore_cart
  [ "$status" -eq 0 ]
  [ ! -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
}

@test "install_splore_cart ignores folders for other emulators" {
  rom_folder="$(make_rom_folder "Game Boy (GB)")"

  run install_splore_cart
  [ "$status" -eq 0 ]

  [ ! -f "$rom_folder/Splore.p8" ]
  [ ! -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
}

@test "install_splore_cart ignores a tagged path that is not a directory" {
  : >"$SDCARD_PATH/Roms/Pico-8 (PICO)"

  run install_splore_cart
  [ "$status" -eq 0 ]
  [ ! -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
}

@test "install_splore_cart tolerates a missing source cart" {
  make_rom_folder "Pico-8 (PICO)" >/dev/null

  PAK_DIR="$BATS_TEST_TMPDIR/no-pak" run install_splore_cart
  [ "$status" -eq 0 ]
  [[ "$output" == *"is missing, skipping"* ]]
  [ ! -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
}

@test "install_splore_cart does not mark a folder it could not write to" {
  if [ "$(id -u)" -eq 0 ]; then
    skip "chmod does not restrict root"
  fi

  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  chmod 555 "$rom_folder"

  run install_splore_cart
  chmod 755 "$rom_folder"

  [ "$status" -eq 0 ]
  [[ "$output" == *"Unable to create"* ]]
  [ ! -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
}

@test "get_pico_bin returns the dynamic binary on rg35xxplus" {
  PLATFORM=rg35xxplus run get_pico_bin
  [ "$output" = "pico8_dyn" ]
}

@test "get_pico_bin returns the 32 bit binary on arm" {
  PLATFORM=tg5040 architecture=arm run get_pico_bin
  [ "$output" = "pico8" ]
}

@test "get_pico_bin returns the 64 bit binary on arm64" {
  PLATFORM=tg5040 architecture=arm64 run get_pico_bin
  [ "$output" = "pico8_64" ]
}

@test "get_controller_file returns one mapping for every h700 device" {
  for device in '' rg28xx rg34xx rg34xxsp rg35xxh rg35xxplus rg35xxpro \
    rg35xxsp rg40xxh rg40xxv rgcubexx rgsp; do
    PLATFORM=h700 DEVICE="$device" run get_controller_file
    [ "$output" = "h700.txt" ]
  done
}

@test "get_controller_file returns the same mapping on minui h700 hardware" {
  # MinUI reports DEVICE=hdmi whenever a cable is attached, which hides the
  # model, so the mapping must not depend on it
  for device in '' cube wide hdmi; do
    PLATFORM=rg35xxplus DEVICE="$device" run get_controller_file
    [ "$output" = "h700.txt" ]
  done
}

@test "get_controller_file falls back to the platform mapping" {
  PLATFORM=tg5040 run get_controller_file
  [ "$output" = "tg5040.txt" ]
  PLATFORM=tg5050 run get_controller_file
  [ "$output" = "tg5050.txt" ]
}

# controllers/rg35xxplus.txt was named by get_controller_file for months without
# ever existing, so the cp in launch_cart failed and the pad went unmapped.
@test "every controller file get_controller_file can name exists" {
  for platform in $SUPPORTED_PLATFORMS; do
    for device in '' cube wide hdmi brick smartpro rg28xx rg34xx rg34xxsp \
      rg35xxh rg35xxplus rg35xxpro rg35xxsp rg40xxh rg40xxv rgcubexx rgsp; do
      PLATFORM="$platform" DEVICE="$device"
      [ -f "$REPO_ROOT/controllers/$(get_controller_file)" ]
    done
  done
}

# config/tg5050.txt was missing for that platform's first release
@test "every supported platform ships a config and a wget shim" {
  for platform in $SUPPORTED_PLATFORMS; do
    [ -f "$REPO_ROOT/config/$platform.txt" ]
    [ -x "$REPO_ROOT/bin/$platform/wget" ]
  done
}

@test "the h700 mapping carries both pads with self consistent crcs" {
  grep -q "^1900d60b010000000100000000010000,Deeplay-keys,.*crc:0bd6," \
    "$REPO_ROOT/controllers/h700.txt"
  grep -q "^19002cb4010000000100000000010000,Anbernic-RGCubeXX,.*crc:b42c," \
    "$REPO_ROOT/controllers/h700.txt"
}

@test "platform_in_list matches whole names only" {
  for platform in $SUPPORTED_PLATFORMS; do
    run platform_in_list "$platform" "$SUPPORTED_PLATFORMS"
    [ "$status" -eq 0 ]
  done

  for platform in rg35xx tg50 h70 700 plus tg3040 ''; do
    run platform_in_list "$platform" "$SUPPORTED_PLATFORMS"
    [ "$status" -ne 0 ]
  done
}

@test "verify_platform accepts h700" {
  PLATFORM=h700 run verify_platform
  [ "$status" -eq 0 ]
}

@test "verify_platform rejects a platform that only looks supported" {
  PLATFORM=rg35xx run verify_platform
  [ "$status" -eq 1 ]
  [[ "$output" == *"is not a supported platform"* ]]
}

@test "launch.sh exits non-zero for an unsupported filetype" {
  pak_dir="$BATS_TEST_TMPDIR/PICO.pak"
  mkdir -p "$pak_dir"
  ln -s "$REPO_ROOT/launch.sh" "$pak_dir/launch.sh"

  cart="$(make_cart Game.txt)"
  run env PATH="$STUB_BIN:$PATH" PLATFORM=tg5050 DEVICE= \
    PICO_PAK_SOURCE_ONLY= \
    SDCARD_PATH="$SDCARD_PATH" USERDATA_PATH="$USERDATA_PATH" \
    SHARED_USERDATA_PATH="$SHARED_USERDATA_PATH" LOGS_PATH="$LOGS_PATH" \
    sh "$pak_dir/launch.sh" "$cart"

  [ "$status" -eq 1 ]
  grep -q "is not a supported filetype" "$LOGS_PATH/PICO.txt"
}

@test "launch.sh exits non-zero when the cart does not exist" {
  pak_dir="$BATS_TEST_TMPDIR/PICO.pak"
  mkdir -p "$pak_dir"
  ln -s "$REPO_ROOT/launch.sh" "$pak_dir/launch.sh"

  run env PATH="$STUB_BIN:$PATH" PLATFORM=tg5050 DEVICE= \
    PICO_PAK_SOURCE_ONLY= \
    SDCARD_PATH="$SDCARD_PATH" USERDATA_PATH="$USERDATA_PATH" \
    SHARED_USERDATA_PATH="$SHARED_USERDATA_PATH" LOGS_PATH="$LOGS_PATH" \
    sh "$pak_dir/launch.sh" "$CARTS/Missing.p8"

  [ "$status" -eq 1 ]
  grep -q "does not exist" "$LOGS_PATH/PICO.txt"
}

@test "launch.sh seeds the splore cart even when the cart is invalid" {
  pak_dir="$BATS_TEST_TMPDIR/PICO.pak"
  mkdir -p "$pak_dir"
  ln -s "$REPO_ROOT/launch.sh" "$pak_dir/launch.sh"
  ln -s "$REPO_ROOT/splore" "$pak_dir/splore"

  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  cart="$(make_cart Game.txt)"

  run env PATH="$STUB_BIN:$PATH" PLATFORM=tg5050 DEVICE= \
    PICO_PAK_SOURCE_ONLY= \
    SDCARD_PATH="$SDCARD_PATH" USERDATA_PATH="$USERDATA_PATH" \
    SHARED_USERDATA_PATH="$SHARED_USERDATA_PATH" LOGS_PATH="$LOGS_PATH" \
    sh "$pak_dir/launch.sh" "$cart"

  [ "$status" -eq 1 ]
  [ -f "$rom_folder/Splore.p8" ]
  [ -f "$rom_folder/.media/Splore.png" ]
  [ -f "$USERDATA_PATH/Pico-8-native/splore-installed" ]
  grep -q "is not a supported filetype" "$LOGS_PATH/PICO.txt"
}

@test "get_pico_bin returns the 64 bit binary on h700" {
  PLATFORM=h700 architecture=arm64 run get_pico_bin
  [ "$output" = "pico8_64" ]
}

@test "set_cpu_speed writes the frequency on the platforms that use it" {
  for platform in rg35xxplus tg5040; do
    stub_cpufreq
    PLATFORM="$platform" set_cpu_speed
    [ "$(cat "$PICO_PAK_CPUFREQ_DIR/scaling_setspeed")" = "1600000" ]
  done
}

@test "set_cpu_speed leaves the trimui and nextui governors alone" {
  for platform in h700 tg5050; do
    stub_cpufreq
    PLATFORM="$platform" set_cpu_speed
    [ ! -s "$PICO_PAK_CPUFREQ_DIR/scaling_setspeed" ]
  done
}

@test "set_cpu_speed tolerates a device with no cpufreq node" {
  PICO_PAK_CPUFREQ_DIR="$BATS_TEST_TMPDIR/absent" PLATFORM=tg5040 run set_cpu_speed
  [ "$status" -eq 0 ]
  [ -z "$output" ]
}

@test "get_screen_resolution prefers fbset when the device has one" {
  stub_fbset 1024 768

  PLATFORM=h700 DEVICE=rgcubexx run get_screen_resolution
  [ "$output" = "1024 768" ]
}

@test "get_screen_resolution knows the h700 panels without fbset" {
  PLATFORM=h700 DEVICE=rgcubexx run get_screen_resolution
  [ "$output" = "720 720" ]

  for device in rg34xx rg34xxsp rgsp; do
    PLATFORM=h700 DEVICE="$device" run get_screen_resolution
    [ "$output" = "720 480" ]
  done

  # the rg28xx panel is portrait but SDL_ROTATION presents it as landscape
  for device in '' rg28xx rg35xxplus rg35xxh rg35xxpro rg35xxsp rg40xxh rg40xxv; do
    PLATFORM=h700 DEVICE="$device" run get_screen_resolution
    [ "$output" = "640 480" ]
  done
}

@test "get_screen_resolution reports nothing without fbset or a known panel" {
  PLATFORM=tg5040 run get_screen_resolution
  [ "$status" -eq 0 ]
  [ -z "$output" ]
}

@test "install_config points cart saves at this device's sd card" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"

  PLATFORM=tg5040 install_config "$rom_folder"

  grep -q "^cdata_path $HOME/cdata/$" "$HOME/config.txt"
  grep -q "^root_path $rom_folder/$" "$HOME/config.txt"
  ! grep -q "/mnt/" "$HOME/config.txt"
}

@test "install_config keeps every other key from the template" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"

  PLATFORM=tg5040 install_config "$rom_folder"

  [ "$(wc -l <"$HOME/config.txt")" -eq "$(wc -l <"$REPO_ROOT/config/tg5040.txt")" ]
  grep -q "^use_wget 0 " "$HOME/config.txt"
  grep -q "^transform_screen 0$" "$HOME/config.txt"
}

@test "install_config rewrites the same keys on every supported platform" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"

  for platform in $SUPPORTED_PLATFORMS; do
    PLATFORM="$platform" install_config "$rom_folder"
    grep -q "^cdata_path $HOME/cdata/$" "$HOME/config.txt"
    grep -q "^root_path $rom_folder/$" "$HOME/config.txt"
  done
}

@test "the h700 config matches the tg5040 one it was taken from" {
  cmp -s "$REPO_ROOT/config/h700.txt" "$REPO_ROOT/config/tg5040.txt"
}

@test "start_power_control skips the platforms upstream does not support" {
  PLATFORM=h700 run start_power_control
  [ "$status" -eq 0 ]
  [[ "$output" == *"does not support h700"* ]]
}

@test "start_power_control runs on the platforms upstream supports" {
  for platform in rg35xxplus tg5040 tg5050; do
    PLATFORM="$platform" run start_power_control
    [ "$status" -eq 0 ]
    [ -z "$output" ]
  done
}

@test "get_screen_mode defaults to the documented standard mode" {
  run get_screen_mode
  [ "$status" -eq 0 ]
  [ "$output" = "standard" ]
  [ "$(cat "$USERDATA_PATH/Pico-8-native/screen-mode")" = "standard" ]
}

@test "get_screen_mode keeps a mode the user already chose" {
  echo "stretched" >"$USERDATA_PATH/Pico-8-native/screen-mode"

  run get_screen_mode
  [ "$output" = "stretched" ]
}

@test "copy_carts does nothing without the copy-carts flag" {
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  mkdir -p "$HOME/bbs/carts"
  : >"$HOME/bbs/carts/freecell.p8.png"
  printf 'x|freecell|x|x|x|x|freecell classic\n' >"$HOME/favourites.txt"

  run copy_carts "$rom_folder"
  [ "$status" -eq 0 ]
  [ ! -f "$rom_folder/freecell.p8.png" ]
  [ ! -f "$rom_folder/map.txt" ]
}

@test "copy_carts copies a favourited cart and titles it" {
  : >"$USERDATA_PATH/Pico-8-native/copy-carts"
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  mkdir -p "$HOME/bbs/carts"
  : >"$HOME/bbs/carts/freecell.p8.png"
  printf 'x|freecell|x|x|x|x|freecell classic\n' >"$HOME/favourites.txt"

  run copy_carts "$rom_folder"
  [ "$status" -eq 0 ]

  [ -f "$rom_folder/freecell.p8.png" ]
  [ -f "$rom_folder/.media/freecell.p8.png" ]
  grep -q "^freecell.p8.png	Freecell Classic$" "$rom_folder/map.txt"
}

@test "copy_carts resolves a numerically named cart to its bbs subfolder" {
  : >"$USERDATA_PATH/Pico-8-native/copy-carts"
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  mkdir -p "$HOME/bbs/4"
  : >"$HOME/bbs/4/42000.p8.png"
  printf 'x|42000|x|x|x|x|the answer\n' >"$HOME/favourites.txt"

  run copy_carts "$rom_folder"
  [ "$status" -eq 0 ]

  [ -f "$rom_folder/42000.p8.png" ]
  grep -q "^42000.p8.png	The Answer$" "$rom_folder/map.txt"
}

@test "copy_carts skips blank lines in the favourites file" {
  : >"$USERDATA_PATH/Pico-8-native/copy-carts"
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  mkdir -p "$HOME/bbs/carts"
  : >"$HOME/bbs/carts/freecell.p8.png"
  printf '\nx|freecell|x|x|x|x|freecell classic\n\n' >"$HOME/favourites.txt"

  run copy_carts "$rom_folder"
  [ "$status" -eq 0 ]

  [ -f "$rom_folder/freecell.p8.png" ]
  [ "$(wc -l <"$rom_folder/map.txt")" -eq 1 ]
}

@test "copy_carts ignores a favourite with no downloaded cart" {
  : >"$USERDATA_PATH/Pico-8-native/copy-carts"
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  mkdir -p "$HOME/bbs/carts"
  printf 'x|missing|x|x|x|x|missing cart\n' >"$HOME/favourites.txt"

  run copy_carts "$rom_folder"
  [ "$status" -eq 0 ]

  [ ! -f "$rom_folder/missing.p8.png" ]
  [ ! -s "$rom_folder/map.txt" ]
}

@test "copy_carts runs its loop in the current shell" {
  : >"$USERDATA_PATH/Pico-8-native/copy-carts"
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  mkdir -p "$HOME/bbs/carts"
  : >"$HOME/bbs/carts/freecell.p8.png"
  printf 'x|freecell|x|x|x|x|freecell classic\n' >"$HOME/favourites.txt"

  filename_png=""
  copy_carts "$rom_folder"

  [ "$filename_png" = "freecell.p8.png" ]
}

@test "is_network_link_up detects an interface that is up" {
  stub_network up

  run is_network_link_up
  [ "$status" -eq 0 ]
}

@test "is_network_link_up rejects an interface that is down" {
  stub_network down

  run is_network_link_up
  [ "$status" -eq 1 ]
}

@test "is_network_link_up ignores the loopback interface" {
  stub_network up lo

  run is_network_link_up
  [ "$status" -eq 1 ]
}

@test "is_network_link_up accepts a tethered interface" {
  stub_network up eth0

  run is_network_link_up
  [ "$status" -eq 0 ]
}

@test "is_network_link_up accepts any interface that is up" {
  stub_network up lo
  stub_network down wlan0
  stub_network up eth0

  run is_network_link_up
  [ "$status" -eq 0 ]
}

@test "is_network_link_up ignores an interface with no operstate file" {
  stub_network "" wlan0

  run is_network_link_up
  [ "$status" -eq 1 ]
}

@test "is_network_link_up handles a device with no interfaces" {
  run is_network_link_up
  [ "$status" -eq 1 ]
}

@test "is_network_link_up handles a missing sysfs directory" {
  PICO_PAK_NET_DIR="$BATS_TEST_TMPDIR/absent" run is_network_link_up
  [ "$status" -eq 1 ]
  [ -z "$output" ]
}

@test "get_network_probe_file is namespaced by the pak name" {
  run get_network_probe_file
  [ "$output" = "/tmp/PICO-network-probe" ]
}

@test "is_internet_reachable succeeds when the probe downloads a body" {
  stub_wget reachable

  run is_internet_reachable
  [ "$status" -eq 0 ]
}

@test "is_internet_reachable fails when the probe leaves an empty file" {
  stub_wget unreachable

  run is_internet_reachable
  [ "$status" -eq 1 ]
}

@test "is_internet_reachable fails when the body is empty" {
  stub_wget empty

  run is_internet_reachable
  [ "$status" -eq 1 ]
}

@test "is_internet_reachable fails when the probe writes nothing" {
  stub_wget silent

  run is_internet_reachable
  [ "$status" -eq 1 ]
}

@test "is_internet_reachable requests the lexaloffle probe url" {
  stub_wget reachable

  is_internet_reachable
  grep -q "https://www.lexaloffle.com/robots.txt" "$WGET_LOG"
}

@test "is_internet_reachable obeys the positional shim contract" {
  stub_wget reachable

  is_internet_reachable
  grep -q -- "-q -O /tmp/PICO-network-probe" "$WGET_LOG"
}

@test "is_internet_reachable removes the probe file" {
  stub_wget reachable
  is_internet_reachable
  [ ! -f "$(get_network_probe_file)" ]

  stub_wget unreachable
  ! is_internet_reachable
  [ ! -f "$(get_network_probe_file)" ]
}

@test "is_internet_reachable bounds the probe with timeout" {
  stub_wget reachable
  stub_timeout

  run is_internet_reachable
  [ "$status" -eq 0 ]
  grep -q "^10s wget https://www.lexaloffle.com/robots.txt" "$TIMEOUT_LOG"
}

@test "show_confirmation returns zero when the user confirms" {
  stub_presenter 0

  run show_confirmation "are you sure"
  [ "$status" -eq 0 ]
}

@test "show_confirmation returns non-zero when the user cancels" {
  stub_presenter 2

  run show_confirmation "are you sure"
  [ "$status" -ne 0 ]
}

@test "show_confirmation blocks with continue and exit buttons" {
  stub_presenter 0

  show_confirmation "are you sure"
  grep -q -- "--confirm-show --confirm-text CONTINUE" "$PRESENTER_CONFIRM_LOG"
  grep -q -- "--cancel-show --cancel-text EXIT" "$PRESENTER_CONFIRM_LOG"
  grep -q -- "--timeout 0" "$PRESENTER_CONFIRM_LOG"
}

@test "show_confirmation uses the button text it is given" {
  stub_presenter 0

  show_confirmation "are you sure" YES NO
  grep -q -- "--confirm-text YES" "$PRESENTER_CONFIRM_LOG"
  grep -q -- "--cancel-text NO" "$PRESENTER_CONFIRM_LOG"
}

@test "verify_splore_connection ignores an ordinary cart" {
  stub_presenter 0
  stub_wget reachable

  run verify_splore_connection "$(make_cart Game.p8)"
  [ "$status" -eq 0 ]
  [ ! -f "$WGET_LOG" ]
  [ ! -f "$PRESENTER_CONFIRM_LOG" ]
}

@test "verify_splore_connection ignores a cart inside a splore named folder" {
  stub_presenter 0
  stub_wget reachable

  run verify_splore_connection "$SDCARD_PATH/Roms/Splore Carts (PICO)/Game.p8"
  [ "$status" -eq 0 ]
  [ ! -f "$PRESENTER_CONFIRM_LOG" ]
}

@test "verify_splore_connection allows splore when the servers respond" {
  stub_network up
  stub_presenter 0
  stub_wget reachable

  run verify_splore_connection "$(make_cart Splore.p8)"
  [ "$status" -eq 0 ]
  [ ! -f "$PRESENTER_CONFIRM_LOG" ]
}

@test "verify_splore_connection warns when there is no network link" {
  stub_presenter 0
  stub_wget reachable

  run verify_splore_connection "$(make_cart Splore.p8)"
  [ "$status" -eq 0 ]
  grep -q "No network connection was detected" "$PRESENTER_CONFIRM_LOG"
  [ ! -f "$WGET_LOG" ]
}

@test "verify_splore_connection aborts when the user exits the link warning" {
  stub_presenter 2
  stub_wget reachable

  run verify_splore_connection "$(make_cart Splore.p8)"
  [ "$status" -eq 1 ]
}

@test "verify_splore_connection warns when the servers are unreachable" {
  stub_network up
  stub_presenter 0
  stub_wget unreachable

  run verify_splore_connection "$(make_cart Splore.p8)"
  [ "$status" -eq 0 ]
  grep -q "Lexaloffle servers could not be reached" "$PRESENTER_CONFIRM_LOG"
}

@test "verify_splore_connection aborts when the user exits the server warning" {
  stub_network up
  stub_presenter 2
  stub_wget unreachable

  run verify_splore_connection "$(make_cart Splore.p8)"
  [ "$status" -eq 1 ]
}

@test "verify_splore_connection matches a renamed splore cart" {
  stub_presenter 0
  stub_wget reachable

  run verify_splore_connection "$(make_cart "1) Splore.p8.png")"
  [ "$status" -eq 0 ]
  grep -q "No network connection was detected" "$PRESENTER_CONFIRM_LOG"
}

@test "launch.sh exits non-zero when the user exits the splore warning" {
  pak_dir="$BATS_TEST_TMPDIR/PICO.pak"
  mkdir -p "$pak_dir"
  ln -s "$REPO_ROOT/launch.sh" "$pak_dir/launch.sh"
  ln -s "$REPO_ROOT/splore" "$pak_dir/splore"

  make_bios
  stub_presenter 2
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  cart="$rom_folder/Splore.p8"
  : >"$cart"

  run env PATH="$STUB_BIN:$PATH" PLATFORM=tg5050 DEVICE= \
    PICO_PAK_SOURCE_ONLY= PICO_PAK_NET_DIR="$PICO_PAK_NET_DIR" \
    SDCARD_PATH="$SDCARD_PATH" USERDATA_PATH="$USERDATA_PATH" \
    SHARED_USERDATA_PATH="$SHARED_USERDATA_PATH" LOGS_PATH="$LOGS_PATH" \
    sh "$pak_dir/launch.sh" "$cart"

  [ "$status" -eq 1 ]
  grep -q "No network connection was detected" "$LOGS_PATH/PICO.txt"
}

@test "launch.sh does not start power control when the warning is exited" {
  pak_dir="$BATS_TEST_TMPDIR/PICO.pak"
  mkdir -p "$pak_dir"
  ln -s "$REPO_ROOT/launch.sh" "$pak_dir/launch.sh"
  ln -s "$REPO_ROOT/splore" "$pak_dir/splore"

  make_bios
  stub_presenter 2
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  cart="$rom_folder/Splore.p8"
  : >"$cart"

  run env PATH="$STUB_BIN:$PATH" PLATFORM=tg5050 DEVICE= \
    PICO_PAK_SOURCE_ONLY= PICO_PAK_NET_DIR="$PICO_PAK_NET_DIR" \
    SDCARD_PATH="$SDCARD_PATH" USERDATA_PATH="$USERDATA_PATH" \
    SHARED_USERDATA_PATH="$SHARED_USERDATA_PATH" LOGS_PATH="$LOGS_PATH" \
    sh "$pak_dir/launch.sh" "$cart"

  [ "$status" -eq 1 ]
  ! grep -q "minui-power-control" "$LOGS_PATH/PICO.txt"
}

@test "launch.sh starts splore when the user continues past the warning" {
  pak_dir="$BATS_TEST_TMPDIR/PICO.pak"
  mkdir -p "$pak_dir/pico8"
  ln -s "$REPO_ROOT/launch.sh" "$pak_dir/launch.sh"
  ln -s "$REPO_ROOT/splore" "$pak_dir/splore"
  ln -s "$REPO_ROOT/controllers" "$pak_dir/controllers"
  ln -s "$REPO_ROOT/config" "$pak_dir/config"
  printf '#!/bin/sh\nexit 0\n' >"$pak_dir/pico8/pico8_64"
  chmod +x "$pak_dir/pico8/pico8_64"
  : >"$pak_dir/pico8/pico8.dat"

  make_bios
  stub_presenter 0
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  cart="$rom_folder/Splore.p8"
  : >"$cart"

  run env PATH="$STUB_BIN:$PATH" PLATFORM=tg5050 DEVICE= \
    PICO_PAK_SOURCE_ONLY= PICO_PAK_NET_DIR="$PICO_PAK_NET_DIR" \
    SDCARD_PATH="$SDCARD_PATH" USERDATA_PATH="$USERDATA_PATH" \
    SHARED_USERDATA_PATH="$SHARED_USERDATA_PATH" LOGS_PATH="$LOGS_PATH" \
    sh "$pak_dir/launch.sh" "$cart"

  [ "$status" -eq 0 ]
  grep -q "No network connection was detected" "$LOGS_PATH/PICO.txt"
  grep -q -- "-splore" "$LOGS_PATH/PICO.txt"
}

@test "launch.sh runs a cart on h700 without starting power control" {
  pak_dir="$BATS_TEST_TMPDIR/PICO.pak"
  mkdir -p "$pak_dir/pico8"
  ln -s "$REPO_ROOT/launch.sh" "$pak_dir/launch.sh"
  ln -s "$REPO_ROOT/splore" "$pak_dir/splore"
  ln -s "$REPO_ROOT/controllers" "$pak_dir/controllers"
  ln -s "$REPO_ROOT/config" "$pak_dir/config"
  printf '#!/bin/sh\nexit 0\n' >"$pak_dir/pico8/pico8_64"
  chmod +x "$pak_dir/pico8/pico8_64"
  : >"$pak_dir/pico8/pico8.dat"

  make_bios
  stub_presenter 0
  rom_folder="$(make_rom_folder "Pico-8 (PICO)")"
  cart="$rom_folder/Freecell.p8"
  : >"$cart"

  run env PATH="$STUB_BIN:$PATH" PLATFORM=h700 DEVICE=rg35xxplus \
    PICO_PAK_SOURCE_ONLY= PICO_PAK_NET_DIR="$PICO_PAK_NET_DIR" \
    PICO_PAK_CPUFREQ_DIR="$BATS_TEST_TMPDIR/absent" \
    SDCARD_PATH="$SDCARD_PATH" USERDATA_PATH="$USERDATA_PATH" \
    SHARED_USERDATA_PATH="$SHARED_USERDATA_PATH" LOGS_PATH="$LOGS_PATH" \
    sh "$pak_dir/launch.sh" "$cart"

  [ "$status" -eq 0 ]
  # set -x quotes the cart path because the roms folder name has a space in it
  grep -q -- "-run" "$LOGS_PATH/PICO.txt"
  grep -q "Freecell.p8" "$LOGS_PATH/PICO.txt"
  grep -q "does not support h700" "$LOGS_PATH/PICO.txt"
  ! grep -q "minui-power-control pico8_64" "$LOGS_PATH/PICO.txt"

  # the h700 mapping and a config naming this device's card both land in $HOME
  cmp -s "$REPO_ROOT/controllers/h700.txt" "$SHARED_USERDATA_PATH/Pico-8-native/sdl_controllers.txt"
  grep -q "^cdata_path $SHARED_USERDATA_PATH/Pico-8-native/cdata/$" \
    "$SHARED_USERDATA_PATH/Pico-8-native/config.txt"
}
