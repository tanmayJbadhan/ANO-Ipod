# 2 · Firmware

The console's main program and the parts every feature depends on.

| File | What it does |
|---|---|
| `Jigglypuff.ino` | Start-up, the Home / Music / Games / Add songs screens, navigation, volume and brightness, screen sleep (triple-tap Up), power off (hold Select), and powering the DAC only while sound plays. |
| `Input.*` | The ANO wheel and its 5 buttons over I²C: debouncing, press/release events, wheel turns. The buttons are remapped because the wheel is mounted rotated 90°. |
| `TouchInput.*` | The FT3168 touch screen. |
| `BatteryMonitor.*` | The SY6970 charger: battery level, charging, and the 5 V boost that powers the DAC on battery. Includes the checks that stop the boost blocking charging when USB is plugged in. |
| `partitions.csv` | How the 16 MB flash is split up. |
| `hardware/` | Wiring diagram with wire colours, board schematic (PDF), chip datasheets, and the board's 3D model. |

Its sibling folders `3_Music_App` … `6_Theme_Graphics` are compiled together with
this one into a single firmware (see `../platformio.ini`).

**Testing from a PC:** with the serial monitor open (`flash.bat -Monitor`), typing
`g` opens the game, `h` goes Home, and `s` `u` `d` `l` `r` press Select / Up /
Down / Left / Right inside the game. While the game runs, its frame rate is
printed every 5 seconds.
