PAK_NAME := $(shell jq -r .name pak.json)
PAK_TYPE := $(shell jq -r .type pak.json)
PAK_FOLDER := $(shell echo $(PAK_TYPE) | cut -c1)$(shell echo $(PAK_TYPE) | tr '[:upper:]' '[:lower:]' | cut -c2-)s

PUSH_SDCARD_PATH ?= /mnt/SDCARD
PUSH_PLATFORM ?= tg5040

# the two values launch.sh derives from uname -m, and the two bin folders it
# puts on PATH: a 32-bit device finding no binary of its own falls back to
# naming copied carts after their files
ARCHITECTURES := arm arm64
PLATFORMS := h700 rg35xxplus tg5040 tg5050
MINUI_PRESENTER_VERSION := 0.13.0
MINUI_POWER_CONTROL_VERSION := 3.0.0
PICO8_DATA_EXTRACTOR_VERSION := 0.1.0

# The image the sdl sensor shim is compiled in. Pinned below glibc 2.34,
# which folded libdl into libc: a newer base records dlsym@GLIBC_2.34 and the
# shim then refuses to load on the device. See shim/Dockerfile.
SHIM_IMAGE := gcc:10-bullseye

# minui-presenter asset names stopped matching platform names in 0.13.0: h700
# and tg5050 are published only as NextUI builds, while tg5040 and rg35xxplus
# keep their plain MinUI ones. A platform with no entry here uses its own name.
MINUI_PRESENTER_ASSET_h700 := h700-nextui
MINUI_PRESENTER_ASSET_tg5050 := tg5050-nextui

SHELL_FILES := launch.sh bin/h700/wget bin/rg35xxplus/wget bin/tg5040/wget bin/tg5050/wget test/test_helper.bash

.PHONY: clean build release bump-version push lint format test print-%

clean:
	rm -f bin/*/minui-presenter || true
	rm -f bin/*/pico8-data-extractor || true
	rm -f bin/minui-power-control || true
	rm -f lib/h700/sdl-nosensor.so || true

build: $(foreach platform,$(PLATFORMS),bin/$(platform)/minui-presenter) $(foreach architecture,$(ARCHITECTURES),bin/$(architecture)/pico8-data-extractor) bin/minui-power-control lib/h700/sdl-nosensor.so

# unlike the downloads above this is built from source in this repo, so it has
# to name its prerequisites or a stale object survives every edit to them
lib/h700/sdl-nosensor.so: shim/sdl-nosensor.c shim/Dockerfile
	docker buildx build --build-arg BASE_IMAGE=$(SHIM_IMAGE) \
		--target export --file shim/Dockerfile --output "type=local,dest=lib" shim

bin/%/pico8-data-extractor:
	mkdir -p bin/$*
	curl -f -o bin/$*/pico8-data-extractor -sSL https://github.com/josegonzalez/pico8-data-extractor/releases/download/$(PICO8_DATA_EXTRACTOR_VERSION)/pico8-data-extractor-linux-$*
	chmod +x bin/$*/pico8-data-extractor

bin/%/minui-presenter:
	mkdir -p bin/$*
	curl -f -o bin/$*/minui-presenter -sSL https://github.com/josegonzalez/minui-presenter/releases/download/$(MINUI_PRESENTER_VERSION)/minui-presenter-$(or $(MINUI_PRESENTER_ASSET_$*),$*)
	chmod +x bin/$*/minui-presenter

bin/minui-power-control:
	mkdir -p bin
	curl -f -o bin/minui-power-control -sSL https://github.com/ben16w/minui-power-control/releases/download/$(MINUI_POWER_CONTROL_VERSION)/minui-power-control
	chmod +x bin/minui-power-control

release: build
	mkdir -p dist
	git archive --format=zip --output "dist/$(PAK_NAME).pak.zip" HEAD
	while IFS= read -r file; do zip -r "dist/$(PAK_NAME).pak.zip" "$$file"; done < .gitarchiveinclude
	$(MAKE) bump-version
	zip -r "dist/$(PAK_NAME).pak.zip" pak.json
	ls -lah dist

bump-version:
	jq '.version = "$(RELEASE_VERSION)"' pak.json > pak.json.tmp
	mv pak.json.tmp pak.json

lint:
	shellcheck $(SHELL_FILES)
	shfmt -l -d -i 2 .

format:
	shfmt -l -i 2 -w .

test:
	bats test

# lets test/makefile.bats read the build wiring without a toolchain
print-%:
	@echo "$*=$($*)"

push: release
	rm -rf "dist/$(PAK_NAME).pak"
	cd dist && unzip "$(PAK_NAME).pak.zip" -d "$(PAK_NAME).pak"
	adb push "dist/$(PAK_NAME).pak/." "$(PUSH_SDCARD_PATH)/$(PAK_FOLDER)/$(PUSH_PLATFORM)/$(PAK_NAME).pak"
