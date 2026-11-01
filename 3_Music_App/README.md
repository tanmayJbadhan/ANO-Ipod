# 3 · Music App

Plays the songs on the microSD card through the PCM5122 DAC.

| File | What it does |
|---|---|
| `Library.*` | Finds every song on the card (all folders, up to 2000) and sorts them into Artists and Albums. |
| `TagReader.*` | Reads title / artist / album / track number from inside each file (MP3 and AAC ID3 tags, M4A, FLAC). Songs without tags are named from the file and folder: `Artist/Album/01 - Title.mp3` or `Artist - Title.mp3`. |
| `Playlists.*` | Playlists, saved on the card as `/Playlists/Playlist 1.m3u` etc. A computer's music player can open them too. |
| `MusicBrowser.*` | The Music screens: the Artists / Albums / Playlists menu, the lists, and the "Add to playlist" picker. |
| `MusicPlayer.*` | Plays the chosen list (play / pause / next / previous / seek), auto-advances, and sets the volume. Decoding runs on its own CPU core so the screen never makes it stutter. |
| `Pcm5122.*` | The DAC over I²C: setup, volume (0.5 dB steps, done inside the DAC so no audio detail is lost), and soft mute so track changes don't click. Re-applies its settings when the DAC loses power and comes back. |
| `AudioConfig.h` | I²S pins to the DAC (with wire colours). |
| `SdConfig.h` | SPI pins to the microSD card. |

**Using it**

| Button | In the lists | In Now playing |
|---|---|---|
| Wheel | scroll | volume |
| Select | open / play | play / pause |
| Left / Right | page up / down | tap: previous / next song · hold: rewind / fast-forward |
| Down | on a song: add to playlist (inside a playlist: also remove) | add this song to a playlist |
| Up | back | back to the lists |

**The library cache**: reading tags from hundreds of songs takes a while, so the
results are saved in `/.jiggly/library.txt` on the card. Only the first visit to
Music is slow. After that, only new songs are read. After **Add songs** the next
visit picks up the new files automatically. If you change a song's tags on a
computer, delete that file to make the console read everything again.

**Sound quality notes**
- The audio decoder library (`../libraries/ESP32-audioI2S`) is patched (search for
  `JIGGLYPUFF PATCH`): the original halved every sample and ran tone filters on it.
  Now samples reach the DAC untouched.
- Volume 21 = −6 dB, the most wired earbuds want from this line-level output.
  2 dB per step.
- The DAC runs from 5 V that, on battery, comes from the charger's boost. That boost
  is switched on only while music plays.

Supported formats: MP3, WAV, M4A/AAC, FLAC.
