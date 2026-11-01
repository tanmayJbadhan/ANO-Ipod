# 3 · Music App

Plays the songs on the microSD card through the PCM5122 DAC.

| File | What it does |
|---|---|
| `MusicPlayer.*` | Playlist from the card's root folder, play / pause / next / previous, auto-advance, and volume. Decoding runs on its own CPU core so the screen never makes it stutter. |
| `Pcm5122.*` | The DAC over I²C: setup, volume (0.5 dB steps, done inside the DAC so no audio detail is lost), and soft mute so track changes don't click. Re-applies its settings when the DAC loses power and comes back. |
| `AudioConfig.h` | I²S pins to the DAC (with wire colours). |
| `SdConfig.h` | SPI pins to the microSD card. |

**Sound quality notes**
- The audio decoder library (`../libraries/ESP32-audioI2S`) is patched (search for
  `JIGGLYPUFF PATCH`): the original halved every sample and ran tone filters on it.
  Now samples reach the DAC untouched.
- Volume 21 = −6 dB, the most wired earbuds want from this line-level output.
  2 dB per step.
- The DAC runs from 5 V that, on battery, comes from the charger's boost. That boost
  is switched on only while music plays.

Supported formats: MP3, WAV, M4A/AAC, FLAC.
