# Jigglypuff

A pocket music console built on the LilyGo **T-Display-S3-AMOLED-1.64**. It plays
music from a microSD card through a hi-fi DAC, has a racing game, and lets you add
songs from your phone over WiFi — all wrapped in a "liquid glass" theme with
Jigglypuff on the home screen.

<p align="center">
  <img src="07_Images/home-screen.png" width="45%" alt="Home screen">
  <img src="07_Images/music-player.png" width="42%" alt="Music player">
</p>

## Flashing it

1. Plug the console into the PC with a USB-C data cable and switch it on.
2. Double-click **`flash.bat`**.

That's all. The script finds PlatformIO, finds the console's USB port, builds the
firmware and writes it. Options (run from a terminal):

| Command | What it does |
|---|---|
| `flash.bat` | build + flash, port found automatically |
| `flash.bat -Port COM12` | use a specific port |
| `flash.bat -Monitor` | flash, then show the console's log |
| `flash.bat -BuildOnly` | only check that it compiles |
| `flash.bat -Clean` | rebuild everything from scratch |

Needs PlatformIO (the VS Code extension is easiest). If flashing can't connect:
hold **BOOT**, tap **RST**, release **BOOT**, and run it again.

## Using the console

| Control | Home | Music lists | Now playing | Game |
|---|---|---|---|---|
| Wheel | choose tile | scroll | volume | steer / pick district |
| Left / Right | choose tile | page up / down | tap: previous / next song · hold: rewind / fast-forward | steer / pick district |
| Select (centre) | open | open / play | play / pause | boost / start race |
| Up | — | back | back to the lists | pause / back |
| Down | — | on a song: add to playlist | add to playlist | brake / switch car |

Music opens on **Artists / Albums / Playlists**. Under Playlists, **+ New playlist**
makes "Playlist 1", "Playlist 2" and so on.

Anywhere:
- **Hold Down + turn the wheel**: screen brightness (remembered).
- **Triple-tap Up**: screen off, music keeps playing. To wake it, double-tap any
  arrow or turn the wheel about 45°.
- **Hold Select ~1.2 s**: switch off. **Hold ~2 s** to switch back on.
- Touch works too: tap the tiles, the player buttons or "< Home".

**Songs**: copy MP3 / WAV / M4A / AAC / FLAC files anywhere on the microSD card
(folders are fine), or use **Add songs** on the console and open `jigglypuff.local` on a phone
on the same WiFi.

## How the repo is laid out

| Folder | What's in it |
|---|---|
| [`1_Examples/`](1_Examples/) | LilyGo's original example sketches for the board (display, touch, charger, sleep, LVGL) and their documentation. Not part of the console. |
| [`2_Firmware/`](2_Firmware/) | The main program: screens, navigation, buttons, touch, battery/charger, screen sleep, power off. Plus the hardware reference (wiring, schematic, datasheets). |
| [`3_Music_App/`](3_Music_App/) | The SD-card music player and the PCM5122 DAC driver (volume, mute, recovery). |
| [`4_Games/`](4_Games/) | *Lullaby Run*, an 11-district racer: renderer, game logic and procedural engine sound, plus a browser preview. |
| [`5_Web_Uploader/`](5_Web_Uploader/) | WiFi, the phone upload page, and clock sync. |
| [`6_Theme_Graphics/`](6_Theme_Graphics/) | The glass look, the landscape screen rotation, and the Jigglypuff artwork with the tool that converts it. |
| [`07_Images/`](07_Images/) | Photos of the finished console. |
| [`08_BOM/`](08_BOM/) | Bill of materials, wiring diagram and the 3D-printable case (STL). |
| `libraries/` | Third-party libraries the build uses (display, touch/charger driver, audio decoder — patched, see 3_Music_App, and LVGL for the examples). |
| `boards/` | PlatformIO board definition for the 16 MB ESP32-S3. |
| `platformio.ini` | Build settings; stitches folders 2–6 into one firmware. |
| `flash.bat`, `flash.ps1` | The flashing script. |
| `deleted/` | Old files no longer used (old games, LilyGo's prebuilt binaries and tools, test files). Safe to delete once you're sure. |

Each folder has its own README explaining its files.

## Hardware

- LilyGo T-Display-S3-AMOLED-1.64 (ESP32-S3, 16 MB flash, 8 MB PSRAM, 280×456 AMOLED
  used sideways, FT3168 touch, SY6970 charger)
- Adafruit ANO rotary navigation encoder (I²C, seesaw)
- Waveshare PCM5122 DAC HAT (I²S) — wired earbuds
- microSD card on SPI

- 2700 mAh LiPo, M2 inserts and screws, 3D-printed case

Full parts list, pin map with wire colours, and case files: [`08_BOM/`](08_BOM/).

## WiFi setup

WiFi details live in `5_Web_Uploader/WifiConfig.h`. That file holds the password, so
it's kept out of git. On a fresh copy, `flash.bat` creates it from
`WifiConfig.example.h`. Fill in the network name and password, then flash again.
