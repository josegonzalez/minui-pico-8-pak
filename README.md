# minui-pico-8-pak

A pak wrapping PICO-8, a fantasy video game console.

## Requirements

This pak is designed and tested on the following MinUI Platforms and devices:

- `rg35xxplus`: RG-CubeXX
- `tg5040`: Trimui Brick (formerly `tg3040`) and Trimui Smart Pro

Use the correct platform for your device.

## Installation

> [!IMPORTANT]
> This emulator pack requires a paid copy of Pico-8. Please purchase this from [the official Pico-8 page](https://www.lexaloffle.com/pico-8.php). No Pico-8 binaries will be provided otherwise.

To being, make sure your MinUI SD card is mounted.

### Installing the Emulator

> [!NOTE]
> The word `$PLATFORM` should be replaced with your platform of choice. Please see the [Requirements](#requirements) section to find the platform for your device.

1. Download the latest release from Github. It will be named `PICO.pak.zip`.
2. Copy the zip file to `/Emus/$PLATFORM/PICO.pak.zip` on your SD card.
3. Extract the zip in place, then delete the zip file.
4. Confirm that there is a `/Emus/$PLATFORM/PICO.pak/launch.sh` file on your SD card.

### Installing the BIOS

> [!WARNING]
> This step is required or the Pico-8 emulator will fail to load.

1. Download the `Raspberry PI` Pico-8 zip. As of this time of writing, it will be `pico-8_0.2.6b_raspi.zip`, available from the [Lexaloffle site](https://www.lexaloffle.com/pico-8.php).
2. Extract the `pico-8_0.2.6b_raspi.zip` zip and place the `pico8`, `pico8_dyn`, `pico8_64`, and`pico8.dat` in the `/Bios/PICO` folder on your SD card.

### Downloading Roms

1. Create a folder at `/Roms/Pico-8 (PICO)` on your SD card.
2. Create an empty file named `Splore.p8` in `/Roms/Pico-8 (PICO)` for Splore support.
3. Place your roms in this directory.
    1. See [this itch.io link](https://itch.io/games/downloadable/free/tag-pico-8) for free downloadable Pico-8 games.

### Finishing up

1. Unmount your SD Card
2. Insert it into your MinUI device.

## Usage

Browse to `Pico-8` and press `A` to play a game.

The following filetypes are supported:

- Native: `.p8`, `.p8.png`

### Exiting a game

To exit a game:

- press the `Start` button
- select `Options`
- select `shutdown Pico-8`

### Deep Sleep & Shutdown

Deep sleep is supported on compatible devices. Click the power button to enter deep sleep. Click again to resume the game. To shut down, hold the power button for 2 seconds. **Note:** Shutdown does not save or resume the game and any unsaved progress will be lost. For more information and issues, see [MinUI Power Control](https://github.com/ben16w/minui-power-control).

### In-Game saves

Any game that creates in-game saves will save these to `/.userdata/shared/Pico-8-native` on your SD Card.

### Splore

> [!NOTE]
> Splore requires an internet connection. The [Wifi.pak](https://github.com/josegonzalez/minui-wifi-pak/) can be used to connect to your network to provide Splore with an internet connection.

To run splore, create a `Splore.p8` file in `/Roms/Pico-8 (PICO)`. This can be an empty file _or_ it can be one of the two png files in the `splore` directory of this repository. Choosing this game in MinUI will launch the Splore UI. If no wifi connection is available, Splore will fail to start.

Carts downloaded via Splore will only be available via Splore. To copy them to the `/Roms/Pico-8 (PICO)` folder of your SD card, create a file named `copy-carts` in `/.userdata/$PLATFORM/Pico-8-native` folder on your SD card.

To exit Splore, choose a game and follow the normal process of exiting a game.

A sample `Splore.p8` file is included in the base of this repository. You may wish to name the file `1) Splore.p8` to move it to the top of the game list.

### Artwork

MinUI and NextUI both support artwork. P8 files are regular PNG files with the `p8` extension. To display artwork, copy the `p8` files and rename them to `png` and place them in the appropriate folder for your CFW.

For example, if you have the following file:

```shell
/Roms/Pico-8 (PICO)/Freecell.p8
```

Copy it over to the correct folder for each CFW.

For nextui:

- Folder: `.media`
- Path: `Roms/Pico-8 (PICO)/.media/Freecell.png` (omit the `p8` extension)

- Folder: `.res`
- Path: `Roms/Pico-8 (PICO)/.res/Freecell.p8.png` (include the `p8` extension)

If your pico-8 files end in `.p8.png`, then you can copy the files over to the respective image folder as is. For example, if you have the following file:

```shell
/Roms/Pico-8 (PICO)/Freecell.p8.png
```

Copy it over to the correct folder for each CFW.

For nextui:

- Folder: `.media`
- Path: `Roms/Pico-8 (PICO)/.media/Freecell.p8.png` (omit the `p8` extension)

- Folder: `.res`
- Path: `Roms/Pico-8 (PICO)/.res/Freecell.p8.png` (include the `p8` extension)

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

### Screen Mode

By default, Pico-8 is launched in as a centered square resolution. For some games, it may be desirable to have the screen drawn such that it stretches to cover the entire screen instead of being 1x1 width to height, in a "stretched" mode that simulates widescreen functionality.

To set the screen mode to `stretched`, create a file named `screen-mode` in `/.userdata/$PLATFORM/Pico-8-native` folder on your SD card. The contents of this can be either of the following:

- `standard`: the standard screen display, showing the game centered as a square.
- `stretched`: display the screen stretched to match the width of the device's screen.

Please note that using `stretched` mode may look unnatural for games with circular objects or on 16:9 resolution screens.

### Sleep Mode

Built-in MinUI cores have support for turning off the display and eventually shutting down when the power button is pressed. Standalone emulators do not have this functionality due to needing support inside of the console for this. At this time, this pak does not implement sleep mode.

### Debug Logging

Logs will be written to the`/.userdata/$PLATFORM/logs/` folder on your SD card.
