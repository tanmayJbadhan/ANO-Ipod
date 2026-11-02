# Jigglypuff

A handmade pocket music console, built as a gift. It plays your music library
from a microSD card through a hi-fi DAC, has three games, and lets you add and
organise songs from a phone. It's all wrapped in a "liquid glass" theme in
Jigglypuff pink, with Jigglypuff on the Home screen.

Built on a LilyGo **T-Display-S3-AMOLED-1.64** with an Adafruit ANO click wheel.

<p align="center">
  <img src="07_Images/home-screen.png" width="45%" alt="The console on its Home screen">
  <img src="07_Images/music-player.png" width="42%" alt="The console playing music">
</p>

<p align="center">
  <img src="07_Images/screen-music-menu.png" width="32%" alt="Music menu">
  <img src="07_Images/screen-games-menu.png" width="32%" alt="Games menu">
  <img src="07_Images/screen-late-for-tea-title.png" width="32%" alt="Late for Tea title">
</p>
<p align="center">
  <img src="07_Images/screen-late-for-tea-roses.png" width="32%" alt="Late for Tea: the Garden of Roses">
  <img src="07_Images/screen-late-for-tea-chessboard.png" width="32%" alt="Late for Tea: the Looking-Glass Board">
  <img src="07_Images/screen-cypress-night.png" width="32%" alt="Cypress Night">
</p>

## What it does

**Music**
- **Library:** reads every song on the card (MP3, M4A/AAC, FLAC, WAV, in any
  folder) and sorts them by artist, album and playlist, using the tags inside
  each file.
- **Menu:** four tiles, **Now playing / Artists / Albums / Playlists**. Any song
  can be added to a playlist.
- **Player:** tap Left/Right to skip songs, or hold them to scrub through a song.
  The wheel sets the volume.

**Games**
- **Lullaby Run:** a night-time city racer across 11 districts.
- **Cypress Night:** a maze chase in a Persian garden. A writer in round glasses
  gathers his manuscript pages while owls hunt him among the cypress trees.
- **Late for Tea:** a 3D dash through ten places from Wonderland. The White Rabbit
  races tortoises on skateboards for the Linzer cookies, with Alice chasing him,
  to reach the Mad Hatter's tea party by four o'clock.

**Add songs and organise them from a phone**
- **Upload:** open **Add songs** on the console, then `jigglypuff.local` in a
  browser. Drop music files on the page and they go straight onto the card.
- **Organize:** `jigglypuff.local/organize` lets you fix missing artists, albums
  and titles, merge or rename artists and albums, build playlists, and play any
  song in the browser while you edit.
- **Auto-tagger:** [`3_Music_App/tools/autotag.py`](3_Music_App/tools/autotag.py)
  looks songs up on MusicBrainz and iTunes and fills in their details
  automatically.

## Flashing it

1. Plug the console into the PC with a USB-C data cable and switch it on.
2. Double-click **`flash.bat`**.

The script finds PlatformIO and the console's USB port, then builds the firmware
and writes it. You need PlatformIO; the VS Code extension is the easiest way to
get it. If flashing can't connect: hold **BOOT**, tap **RST**, release **BOOT**,
and run it again.

| Command | What it does |
|---|---|
| `flash.bat` | build and flash, port found automatically |
| `flash.bat -Port COM12` | use a specific port |
| `flash.bat -Monitor` | flash, then show the console's log |
| `flash.bat -BuildOnly` | only check that it compiles |
| `flash.bat -Clean` | rebuild everything from scratch |

## Controls

| Control | Home and menus | Music lists | Now playing |
|---|---|---|---|
| Wheel | choose a tile | scroll | volume |
| Left / Right | choose a tile | page up / down | tap: previous / next song · hold: rewind / fast-forward |
| Select (centre) | open | open / play | play / pause |
| Up | back | back | back to the lists |
| Down | — | on a song: add to playlist | add to playlist |

Anywhere:
- **Brightness:** hold **Down** and turn the wheel. The setting is remembered.
- **Screen off:** triple-tap **Up**. The music keeps playing. To wake the screen,
  double-tap any arrow or turn the wheel about 45°.
- **Power:** hold **Select** about 1.2 s to switch off, and about 2 s to switch
  back on.
- **Touch:** works too. Tap the tiles, the player buttons or "< Home".

Each game shows its own controls on its title screen; they're also listed in
[`4_Games/`](4_Games/).

## How the repo is laid out

| Folder | What's in it |
|---|---|
| [`1_Examples/`](1_Examples/) | LilyGo's original example sketches for the board. Not part of the console. |
| [`2_Firmware/`](2_Firmware/) | The main program: screens, menus, buttons, touch, battery and charger, screen sleep, power off. Plus the board's schematic and datasheets. |
| [`3_Music_App/`](3_Music_App/) | The music library, tag reader, playlists, the Music screens, the player and the PCM5122 DAC driver. Plus the auto-tagger tool. |
| [`4_Games/`](4_Games/) | *Lullaby Run*, *Cypress Night* and *Late for Tea*, with their drawing, rules and synthesised sound. |
| [`5_Web_Uploader/`](5_Web_Uploader/) | The phone pages: song upload and the organizer. Also clock sync. |
| [`6_Theme_Graphics/`](6_Theme_Graphics/) | The glass look, the Jigglypuff-pink theme, the landscape screen rotation and the Home artwork. |
| [`07_Images/`](07_Images/) | Photos of the console and screenshots from it. |
| [`08_BOM/`](08_BOM/) | Bill of materials, wiring diagram with wire colours, and the 3D-printable case (STL). |
| `libraries/` | Third-party libraries the build uses: display, touch/charger driver, and the audio decoder (patched; see `3_Music_App`). |
| `boards/` | PlatformIO board definition for the 16 MB ESP32-S3. |
| `platformio.ini` | Build settings; it stitches folders 2–6 into one firmware. |
| `flash.bat`, `flash.ps1` | The flashing script. |

Each folder has its own README explaining its files.

## Hardware

- LilyGo T-Display-S3-AMOLED-1.64: ESP32-S3, 16 MB flash, 8 MB PSRAM, a 280×456
  AMOLED used sideways, FT3168 touch, SY6970 charger
- Adafruit ANO rotary navigation encoder (I²C)
- Waveshare PCM5122 DAC, for wired earbuds
- Adafruit microSD breakout and a 128 GB card
- 2700 mAh LiPo battery
- 3D-printed case with M2 inserts and screws

The full parts list, the pin map with wire colours, and the case files are in
[`08_BOM/`](08_BOM/).

## Credits

The title and story cards in *Late for Tea* use John Tenniel's 1865 illustrations
for *Alice's Adventures in Wonderland*. They're public domain, from Wikimedia
Commons.
