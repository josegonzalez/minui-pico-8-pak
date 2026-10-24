# minui-pico-8-pak

A pak wrapping PICO-8, a fantasy video game console.

## Requirements

This pak is designed and tested on the following MinUI Platforms and devices:

- `h700`: Anbernic RG28XX, RG34XX, RG34XX SP, RG35XX (Plus and 2024), RG35XX H, RG35XX Pro, RG35XX SP, RG40XX H, RG40XX V, RG CubeXX and RGSP, running NextUI
- `my355`: Miyoo Flip, running NextUI
- `rg35xxplus`: the same Anbernic handhelds, running MinUI
- `tg5040`: Trimui Brick (formerly `tg3040`) and Trimui Smart Pro
- `tg5050`: Trimui Smart Pro S

Use the correct platform for your device.

The Anbernic handhelds are listed twice because MinUI and NextUI give the same hardware different platform names. Install the `h700` build on NextUI and the `rg35xxplus` build on MinUI. NextUI runs a 64-bit userland and uses the `pico8_64` binary, while MinUI's is 32-bit and uses `pico8_dyn`.

## Installation

> [!IMPORTANT]
> This emulator pack requires a paid copy of PICO-8. Please purchase this from [the official PICO-8 page](https://www.lexaloffle.com/pico-8.php). No PICO-8 binaries will be provided otherwise.

To being, make sure your MinUI SD card is mounted.

### Installing the Emulator

> [!NOTE]
> The word `$PLATFORM` should be replaced with your platform of choice. Please see the [Requirements](#requirements) section to find the platform for your device.

1. Download the latest release from GitHub. It will be named `PICO.pak.zip`.
2. Copy the zip file to `/Emus/$PLATFORM/PICO.pak.zip` on your SD card.
3. Extract the zip in place, then delete the zip file.
4. Confirm that there is a `/Emus/$PLATFORM/PICO.pak/launch.sh` file on your SD card.

### Installing the BIOS

> [!WARNING]
> This step is required or the PICO-8 emulator will fail to load.

1. Download the `Raspberry PI` Pico-8 zip. As of this time of writing, it will be `pico-8_0.2.7_raspi.zip`, available from the [Lexaloffle site](https://www.lexaloffle.com/pico-8.php).
2. Extract the `pico-8_0.2.7_raspi.zip` zip and place the `pico8`, `pico8_dyn`, `pico8_64`, and`pico8.dat` in the `/Bios/PICO` folder on your SD card.

The following are the sha256sum of known working pico8 files:

```shell
443e9553a6aa81dd2bbb26cbe9a6ea00cad4a16f8cd93be765811792833ffffc  pico8_dyn
91212d55b540ef2abf9d5df7bb46fb87f41c35f3e9d108ebd8680debab020be2  pico8.dat
ad56e8ed1ad812cab57a7e2679b6731eba367cdd626cbf40a12b514060b597aa  pico8_64
cee03b8ffd89bd3881a04ab51a21b0f47655c70d20bcec44cd7203274b24a817  pico8
```

### Downloading Roms

1. Create a folder at `/Roms/Pico-8 (PICO)` on your SD card.
2. Place your roms in this directory.
   1. See [this itch.io link](https://itch.io/games/downloadable/free/tag-pico-8) for free downloadable Pico-8 games.

> [!NOTE]
> A `Splore.p8` cart is created for you the first time you launch a game. See [Splore](#splore) for details.

### Finishing up

1. Unmount your SD Card
2. Insert it into your MinUI device.

## Usage

Browse to `Pico-8` and press `A` to play a game.

The following filetypes are supported:

- Native: `.p8`, `.p8.png`, `.png`

Choosing a file with any other extension, or one that is missing from the SD card, displays an error and exits without starting PICO-8.

### Controls

On `h700`, NextUI v6.14.0-rc11 or newer is needed for the buttons to map correctly. From that release, the buttons match the TrimUI devices on every Anbernic model.

`Start` opens PICO-8's own pause menu on every platform, and `Menu` opens the pak's [in-game menu](#in-game-menu). On `tg5040` and `tg5050` the `Menu` button used to open PICO-8's pause menu, while `Start` did nothing.

### In-game menu

Press and release `Menu` to pause the game and open the same menu MinUI and NextUI show over their own emulators. It works from a game, Splore, or PICO-8's command prompt. The game and its sound stay paused until the menu closes. On `h700` this needs NextUI v6.14.0-rc11 or newer, the first release whose controller mapping includes the `Menu` button.

`Menu` only opens the menu when it is pressed on its own. Holding it with a volume button, as MinUI and NextUI do to change the brightness, or with any other button, leaves the menu closed.

The menu has these entries:

- `Continue` closes the menu and carries on with the game. `B` and a tap of `Menu` close it too.
- `Save` and `Load` are shown but cannot be chosen yet, as the pak has no save states.
- `Options` opens the settings below.
- `Quit` fades the screen to black, closes PICO-8 and returns to the game list. The screen stays black, and the game stays silent, until the launcher has drawn its menu again.

The menu takes its look from the launcher, using NextUI's theme colours and font, or MinUI's font. If neither font is on the SD card, it draws with a small built-in font instead.

### Volume and brightness

Changing the volume or brightness while a game is running, or in the menu, shows the same bar MinUI and NextUI show over their own emulators, in the top right of the screen. It goes away half a second after the last change. In the menu it takes the battery's place until then.

The pak reads the volume and brightness from the launcher's own settings library, `libmsettings.so`, so the bar only appears while the launcher's settings are running, as they always are under MinUI and NextUI.

#### Options

Use `Up` and `Down` to pick a setting, and `Left`, `Right` or `A` to change it. Each change is saved to the SD card straight away, and `B` goes back.

| Setting            | Values                  | Takes effect                |
| ------------------ | ----------------------- | --------------------------- |
| Screen Mode        | Standard, Stretched     | When the menu closes        |
| Overlay            | None, or any overlay    | When the menu closes        |
| Copy Splore Carts  | Off, All, Favourites    | When you exit Splore        |
| Splore Artwork     | Standard, Fancy         | Straight away               |
| Show FPS           | Off, On                 | The next time a game starts |
| Volume             | 0%, 25%, 50%, 75%, 100% | The next time a game starts |
| Battery Saver      | Off, On                 | The next time a game starts |
| Hide Mature Carts  | Off, On                 | The next time a game starts |
| Check Cart Updates | Off, On                 | The next time a game starts |

See [Screen Mode](#screen-mode), [Overlay](#overlay), [Copying Splore carts into your roms folder](#copying-splore-carts-into-your-roms-folder) and [Splore](#splore) for the first four. The rest are PICO-8's own settings:

- `Show FPS` draws the frame rate in the corner of the screen.
- `Volume` is PICO-8's own volume, which applies under the device's volume.
- `Battery Saver` has PICO-8 rest for 10 milliseconds between frames rather than 4, to save power.
- `Hide Mature Carts` hides carts the community has tagged as mature from Splore.
- `Check Cart Updates` checks for a newer version of a cart from the BBS each time it runs.

These are saved to a file named `pico8-config` in the `/.userdata/$PLATFORM/Pico-8-native` folder on your SD card, and written into PICO-8's configuration when a game starts. Delete the file to go back to the defaults.

### Exiting a game

To exit a game, press `Menu` and choose `Quit`.

You can also exit from PICO-8's own pause menu:

- press the `Start` button
- select `Options`
- select `shutdown Pico-8`

### Game switcher

NextUI's game switcher lists every game you have played recently, but only shows a picture for games with a save state. PICO-8 has no save states, so on NextUI the pak gives the switcher a picture of its own.

When you choose `Quit` in the [in-game menu](#in-game-menu), the picture is the game as it was when you opened the menu, without any overlay. Until then, and when a game ends any other way, it is the cart's artwork: its image in the `.media` folder if there is one, and otherwise the cart itself, since most carts are PNG images whether they are named `.p8.png` or `.p8`. The artwork is written as soon as PICO-8 starts.

The picture and a file naming its slot are written to `/.userdata/shared/.minui/PICO` on your SD card.

Because NextUI treats that as a save state, the switcher labels the `A` button `RESUME` and the game list offers `X` to resume the cart. Both start the cart from the beginning. Press `Y` in the switcher to remove a cart from it.

A cart saved as plain text, rather than as an image, still shows no picture unless it has an image in the `.media` folder. See [Artwork](#artwork) for where to put one. MinUI reads the same files to offer resuming a game, so nothing is written there.

### Deep Sleep & Shutdown

Deep sleep is supported on compatible devices, including the `h700` devices and the Miyoo Flip running NextUI. Click the power button to enter deep sleep. Click again to resume the game. To shut down, hold the power button for 2 seconds. **Note:** Shutdown does not save the game and any unsaved progress will be lost.

On the Miyoo Flip, closing the lid also puts the device to sleep, and opening it resumes the game.

On devices without deep sleep, which includes every device running MinUI, clicking the power button pauses the game and turns the screen off instead, and clicking again resumes it. A device left like that for two minutes, while not charging, shuts down, and starts the same cart again the next time it is turned on. The cart starts from the beginning, as PICO-8 has no save states.

The power button is handled by [MinUI Power Control](https://github.com/ben16w/minui-power-control). The pak uses [a fork](https://github.com/josegonzalez/minui-power-control) that adds `h700` support, turns the Miyoo Flip's screen off during deep sleep under NextUI, and sleeps when the Flip's lid closes, until those changes are part of the original. Its messages, such as "Powering off", are shown with the pak's own copy of minui-presenter. See either for more information and known issues.

Power control is released as a single file that checks and unpacks itself every time it starts, which took seconds on a device while the game was starting. The pak ships it already unpacked, in its `bin/power-control` folder, so it starts at once.

### CPU speed

On `rg35xxplus` and `tg5040`, the pak raises the CPU clock to 1.6 GHz while PICO-8 runs, but only when the OS uses the `userspace` CPU governor, as MinUI does. NextUI already switches to its `performance` governor before starting any pak, which runs the CPU at full speed, so the pak leaves the clock alone there and notes the governor in its log.

### SDL compatibility

PICO-8 asks SDL to start every subsystem it knows about and refuses to run when one of them will not start. NextUI builds SDL2 without sensor support, so on `h700` PICO-8 used to stop before drawing anything, leaving only this in the log:

```
SDL Error: SDL not built with sensor support

** FATAL ERROR: Unable to initialize SDL
```

The pak ships a small library at `lib/h700/sdl-nosensor.so` and loads it ahead of SDL2. It drops that one request on its way past and hands everything else to the device's own SDL2 unchanged, which matters because NextUI's SDL2 is the build that sees the built-in controller and no replacement would. PICO-8 never reads a sensor, so nothing is lost. The source is in `shim/sdl-nosensor.c` in this pak.

The library is only loaded on `h700`, and only when it is present. A copy the device cannot load is skipped with a line in the log rather than stopping the launch.

Overlays, switching the screen mode in game, the in-game menu and NextUI's game switcher preview come from a second library, loaded on every platform in the same way: `lib/arm64/sdl-display.so` for PICO-8's 64-bit binary, and `lib/arm/sdl-display.so` for the 32-bit one `rg35xxplus` uses. Its source is in `shim/sdl-display.c`, `shim/menu.c`, `shim/gfx.c` and `shim/cart.c` in this pak.

### In-Game saves

Any game that creates in-game saves will save these to `/Saves/PICO` on your SD card, the same folder MinUI and NextUI keep the saves for their built-in emulators in. The pak fills that path into the PICO-8 configuration each time a game starts, so it is correct whatever your device mounts the SD card as.

Earlier versions of this pak saved to `/.userdata/shared/Pico-8-native/cdata`. The first launch after upgrading moves those saves into `/Saves/PICO` and records that it has done so by writing a file named `saves-migrated` in the `/.userdata/$PLATFORM/Pico-8-native` folder on your SD card. A save whose name is already present in `/Saves/PICO` is left in the old folder rather than overwriting the copy there, and the log names it.

### Splore

> [!NOTE]
> Splore requires an internet connection to browse and download new carts. The [Wifi.pak](https://github.com/josegonzalez/minui-wifi-pak/) can be used to connect to your network to provide Splore with an internet connection.

> [!NOTE]
> On `h700`, NextUI ships `curl` but no `libcurl`, so PICO-8 falls back to downloading through a `wget` script the pak provides, which wraps that `curl`. Uploading a cart to the BBS is not supported through this route.
>
> On `my355`, the Miyoo Flip's `wget` cannot make HTTPS requests, so the pak provides a `wget` script backed by `curl` for the connection check before Splore.

The first time you launch any game, the pak creates a `Splore.p8` cart, along with matching artwork at `.media/Splore.png` for NextUI and `.res/Splore.p8.png` for MinUI, in every roms folder tagged `(PICO)` on your SD card. Choosing this game in MinUI will launch the Splore UI.

Before Splore starts, the pak checks that the device has a network connection and then downloads the `lexaloffle.com` homepage to confirm the Splore servers are reachable. If either check fails, a message is displayed and waits for you to choose:

- `CONTINUE` starts Splore anyway. Carts you have already downloaded stay browsable, but new carts cannot be fetched.
- `EXIT` returns to MinUI without starting Splore.

If the download could not be attempted at all, for example because the `wget` the pak ships could not start, the message says the check could not run and points to the log file instead of reporting the servers as unreachable.

This check only runs for the Splore cart. Ordinary carts start without touching the network.

The cart is created once and only once. The pak records this by writing a file named `splore-installed` in the `/.userdata/$PLATFORM/Pico-8-native` folder on your SD card, so deleting the cart will not bring it back. To have it recreated, delete `splore-installed` and launch a game again.

A roms folder that already contains a cart with `Splore` or `splore` in its name is left alone.

> [!NOTE]
> The cart is created while a game is starting, so it will not appear until you leave the folder and enter it again.

To exit Splore, choose a game and follow the normal process of exiting a game.

You may wish to rename the cart to `1) Splore.p8` to move it to the top of the game list. Renaming it will not cause a new one to be created. Rename the artwork to match if you do, so `.media/1) Splore.png` and `.res/1) Splore.p8.png`.

The `splore` folder of this pak also contains `Splore-fancy.p8.png`, an alternative cover. To use it, set `Splore Artwork` in the [in-game menu](#in-game-menu)'s options to `Fancy`, and set it back to `Standard` to return to the usual one. The choice is saved to a file named `splore-artwork` in the `/.userdata/$PLATFORM/Pico-8-native` folder on your SD card. As soon as you change it, the cover of every Splore cart in a roms folder tagged `(PICO)` is changed to match, renamed carts included. Only covers that are still one of the pak's two are changed, so artwork of your own, or a cart with no artwork, is left alone. A Splore cart the pak creates later gets the chosen cover from the start.

### Copying Splore carts into your roms folder

Carts downloaded via Splore are only available via Splore. To copy them into your roms folder as well, set `Copy Splore Carts` in the [in-game menu](#in-game-menu)'s options to `All` or `Favourites`. The menu does this by writing the files described below, and removes any of them it finds when you change the setting.

You can also create a file named `copy-carts` yourself, in either the `/.userdata/$PLATFORM/Pico-8-native` or the `/.userdata/shared/Pico-8-native` folder on your SD card. The file can be empty, and it needs no extension - if your computer hides extensions, check on a terminal that it is not really named `copy-carts.txt`.

Every cart Splore has downloaded is copied, whether or not you favourited it. To copy only your favourites, name the file `copy-carts-favourites` instead, or `copy-carts-favorites` if you prefer that spelling. Either file on its own turns the copying on. Favourites are read from the `favourites.txt` PICO-8 writes in `/.userdata/shared/Pico-8-native`, and from `.lexaloffle/pico-8/favourites.txt` under that folder if it exists. If no cart is favourited, nothing is copied.

The copy runs when you exit Splore, into the roms folder it was launched from. Exiting any other cart copies nothing, as only Splore downloads carts. A message is shown while carts are being copied - do not turn the device off until it goes away. Like the Splore cart itself, the new games will not appear until you leave the folder and enter it again.

Carts keep the filename Splore gave them, which is usually an id or a slug. Their names in the game list come from a `map.txt` file the pak writes in the roms folder. A favourited cart is named with the title Splore shows for it, from `favourites.txt`. Otherwise the name is the title comment on the first line of the cart's code - the same title PICO-8 shows on the cart's label - unless that comment is a code label such as `main` or `global variables`. A cart with neither is named after its filename, without the revision number Splore adds to the end, so `kalikan_menu-6.p8.png` is listed as `Kalikan Menu`. A version number or dashes at the end of a title are dropped, so `pico world race 1.1` is listed as `Pico World Race`. Rows in `map.txt` for your own games are left alone, and so is any name you edit yourself - the pak reuses what is already there rather than overwriting it. That also means a cart keeps the name it was first given, so to have the pak name a cart again, delete its row from `map.txt`, or the whole file, and exit a cart again. Cover art is written for both NextUI and MinUI, in the `.media` and `.res` folders.

If nothing is copied, the log at `/.userdata/$PLATFORM/logs/PICO.txt` records the reason.

### Artwork

MinUI and NextUI both support artwork, but they look in different folders and name the image differently:

- NextUI reads from a `.media` folder, and the image drops the cart's last extension.
- MinUI reads from a `.res` folder, and the image keeps the cart's full name.

PICO-8 carts are PNG images whatever they are named, so the artwork is a copy of the cart itself. Carts ending in `.p8.png` can be copied across as they are. Carts ending in `.p8` need to be renamed to end in `png`.

For a cart at `/Roms/Pico-8 (PICO)/Freecell.p8`:

| CFW    | Path                                      |
| ------ | ----------------------------------------- |
| NextUI | `Roms/Pico-8 (PICO)/.media/Freecell.png`  |
| MinUI  | `Roms/Pico-8 (PICO)/.res/Freecell.p8.png` |

For a cart at `/Roms/Pico-8 (PICO)/Freecell.p8.png`:

| CFW    | Path                                          |
| ------ | --------------------------------------------- |
| NextUI | `Roms/Pico-8 (PICO)/.media/Freecell.p8.png`   |
| MinUI  | `Roms/Pico-8 (PICO)/.res/Freecell.p8.png.png` |

### Multi-cart Game Support

The Pico-8 pak supports multi-cart games via a MinUI's M3U functionality. This requires that all game files - including the `p8` files and any supporting scripts and resources - are in the folder containing the `.m3u` file.

As an example, [Poom](https://freds72.itch.io/poom) - a Doom clone written for Pico-8 - comes with 28 different `p8` files. To properly load Poom (1.9 as of this writing):

1. Create the folder `/Roms/Pico-8 (PICO)/Poom` on your SD Card.
2. Place all the files from the `poom_1.9_standalone.zip` download in that folder. There should be 32 files in total, 28 `.p8` files and 4 `.lua` files.
3. Create a file named `Poom.m3u` in `/Roms/Pico-8 (PICO)/Poom`. The contents of this file are the `p8` files in load order, and should be as follows:

   <details>
   <summary><b>Poom.m3u</b></summary>

   ```shell
   poom_0.p8
   poom_1.p8
   poom_2.p8
   poom_3.p8
   poom_4.p8
   poom_5.p8
   poom_6.p8
   poom_7.p8
   poom_8.p8
   poom_9.p8
   poom_10.p8
   poom_11.p8
   poom_12.p8
   poom_13.p8
   poom_14.p8
   poom_15.p8
   poom_16.p8
   poom_17.p8
   poom_18.p8
   poom_19.p8
   poom_20.p8
   poom_21.p8
   poom_22.p8
   poom_23.p8
   poom_24.p8
   poom_25.p8
   poom_e1.p8
   poom_e2.p8
   ```

   </details>

4. Unmount your SD Card.
5. Insert your SD Card into your MinUI device.
6. In MinUI, Browse to `Pico-8 > Poom` and press the `A` button to start the game.

> [!WARNING]
> If any resources are missing from the folder containing your `m3u` file, the game may fail to load.

> [!NOTE]
> MinUI resolves the `m3u` file to the first cart listed inside it before starting the pak, so the `m3u` file itself is never loaded by PICO-8.

### Screen Mode

By default, PICO-8 is launched in as a centered square resolution. For some games, it may be desirable to have the screen drawn such that it stretches to cover the entire screen instead of being 1x1 width to height, in a "stretched" mode that simulates widescreen functionality.

The screen mode is held in a file named `screen-mode` in the `/.userdata/$PLATFORM/Pico-8-native` folder on your SD card. The pak creates it set to `standard` the first time you launch a game, and you can edit it yourself. The contents can be either of the following:

- `standard`: the standard screen display, showing the game centered as a square.
- `stretched`: display the screen stretched to match the width of the device's screen.

Please note that using `stretched` mode may look unnatural for games with circular objects or on 16:9 resolution screens.

`stretched` mode needs to know the size of your screen. It reads that from `fbset` where the device provides one, and otherwise falls back to the panel size for your model, which is known for every `h700` device. On the RG28XX, NextUI rotates the display for applications, so both screen modes look the same there as they do on the other models.

You can also switch between the two while a game is running, with `Screen Mode` in the [in-game menu](#in-game-menu)'s options. The mode you switch to is saved to the `screen-mode` file, so the next game starts in it too.

### Overlay

A PNG in the `/Overlays/PICO` folder on your SD card is drawn over the game, for a bezel or border. This follows the layout NextUI uses for its own emulators' overlays, a folder under `/Overlays` named for the tag at the end of the roms folder, which is `(PICO)` for this pak. The pak creates the folder the first time you launch a game.

While a game is running, choose one with `Overlay` in the [in-game menu](#in-game-menu)'s options, which steps through `None` and then every PNG in the folder, in name order. Your choice is remembered for the next game, in a file named `overlay` in the `/.userdata/$PLATFORM/Pico-8-native` folder on your SD card. Until you choose one, no overlay is drawn.

The PNG is decoded by the pak itself, so overlays work the same on every platform. The image is scaled to fit the screen without changing its shape and centred, leaving any space around it uncovered, and it needs a transparent window where the game shows through. On the Trimui Brick's 1024x768 screen, `standard` mode draws the game as a 768x768 square starting 128 pixels from the left, so a 4:3 image fills the screen. The Trimui Smart Pro also uses the `tg5040` build, and its 1280x720 screen is 16:9, so a 4:3 image there has bars at the sides and a 16:9 image fills it. No overlay is drawn in `stretched` mode, where the game fills the screen.

### Debug Logging

Logs will be written to the`/.userdata/$PLATFORM/logs/` folder on your SD card.

The log records the library search path PICO-8 is started with and the `ldd` output for your PICO-8 binary, so a library it cannot find or resolves to the wrong copy is visible. A PICO-8 that fails to start also says so on screen rather than only in the log.

Each step of starting and exiting a game is marked in the log with the number of seconds since the device was turned on, such as `[1234.56] syncing the SD card`. The lines from the in-game menu and the rest of the display library, such as `[1240.03] sdl-display: Quit chosen in the menu, quitting pico-8`, are marked the same way. Comparing the marks shows how long each step took, from PICO-8 starting until it drew its first frame, to the time between choosing `Quit` and returning to the launcher.

The log lists every command the pak runs, except inside the steps that would otherwise make starting and exiting slow, such as checking the cart, preparing the menu and copying carts, as each listed command is written to the SD card on its own. Those steps log what they find instead. Moving older saves and creating the Splore cart run alongside checking the cart, so their lines can be interleaved in the log.

## Development

The launcher is POSIX shell, and is linted and formatted with [shellcheck](https://www.shellcheck.net/) and [shfmt](https://github.com/mvdan/sh). Tests are written with [bats](https://github.com/bats-core/bats-core).

```shell
make lint
make format
make test
```

`make build` and `make release` additionally need Docker, which is what the SDL sensor shim and the display shim are cross-compiled in, the display shim once for `arm` and once for `arm64`. The display shim decodes overlays with [stb_image](https://github.com/nothings/stb) and draws the menu's text with stb_truetype from the same project, both of which the build fetches at a pinned commit and checks against a checksum. Its base image is pinned to Debian bullseye on purpose: glibc 2.34 folded `libdl` into `libc`, so a newer image produces an object that will not load on these devices. See `shim/Dockerfile`.

`make build` also unpacks the pinned minui-power-control release into `bin/power-control` with `scripts/unpack-makeself.sh`. That script checks the archive against the MD5 it records for itself, and never runs any part of it.

The build also tests the display shim on both architectures, by loading it into a small program in `shim/test` that stands in for PICO-8, linked against a fake `libSDL2` that records what the shim does to each frame, the controller, the audio and the event queue. The menu's pages, the files its settings write and its drawing are tested on their own by `shim/test/menu-test.c`, with the fonts MinUI and NextUI ship, which the build fetches at a pinned commit for the tests alone. A shim that fails these tests is not written to `lib/`.

Eight environment variables exist for the test suites only. `PICO_PAK_SOURCE_ONLY` sources `launch.sh` without running `main`, `PICO_PAK_NET_DIR` overrides the `/sys/class/net` directory the network check reads, and `PICO_PAK_CPUFREQ_DIR` overrides the cpufreq directory the clock speed is written to. `PICO_PAK_UPTIME_FILE` overrides the `/proc/uptime` file the log's time marks are read from, in both `launch.sh` and the display shim. In the display shim, `PICO_PAK_INPUT_DIR` overrides the `/dev/input` directory watched for the volume buttons, `PICO_PAK_POWER_SUPPLY_DIR` overrides the `/sys/class/power_supply` directory the menu reads the battery from, `PICO_PAK_MSETTINGS` names the settings library to open in place of `libmsettings.so`, and `PICO_PAK_SHARED_SETTINGS` overrides the `/dev/shm/SharedSettings` file that has to exist before it is opened.
