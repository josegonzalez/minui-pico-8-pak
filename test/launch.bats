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

@test "get_controller_file returns the cube mapping" {
  PLATFORM=rg35xxplus DEVICE=cube run get_controller_file
  [ "$output" = "rg35xxplus-cube.txt" ]
}

@test "get_controller_file falls back to the platform mapping" {
  PLATFORM=rg35xxplus DEVICE= run get_controller_file
  [ "$output" = "rg35xxplus.txt" ]
  PLATFORM=tg5040 run get_controller_file
  [ "$output" = "tg5040.txt" ]
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
