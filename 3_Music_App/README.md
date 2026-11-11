# 3 · Music App

Plays the songs on the microSD card through the PCM5122 DAC.

| File | What it does |
|---|---|
| `Library.*` | Finds every song on the card (all folders, up to 2000) and sorts them into Artists and Albums. Songs whose album is "Singles" make one album across all artists (listed A-Z by artist), and an album name shared by three or more artists with a song or two each (a soundtrack, a compilation) is one album under "Various Artists"; each artist's own page still lists their songs. |
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

**Fixing song details automatically**: `tools/autotag.py` runs on a PC on the same
WiFi while **Add songs** is open. It looks up every song with a garbled or missing
title, artist or album on [MusicBrainz](https://musicbrainz.org), using its tags,
file name and folder name ("Artist - Album" folders help a lot). It only accepts
matches it's sure of, and writes Japanese, Chinese and Korean names in Latin letters
so the screen can show them.

```
pip install pykakasi pypinyin unidecode
python tools/autotag.py --console http://jigglypuff.local          # report only
python tools/autotag.py --console http://jigglypuff.local --apply  # send to the console
python tools/autotag.py --console http://jigglypuff.local --undo   # put back the last --apply
```

`--undo` only works until the library is next rescanned (song numbers change). The
changes are stored like hand edits, in `/.jiggly/edits.txt`. Songs it can't
identify are left alone; fix those in the web organizer.

**Sound quality notes**
- The audio decoder library (`../libraries/ESP32-audioI2S`) is patched (search for
  `JIGGLYPUFF PATCH`): the original halved every sample and ran tone filters on it.
  Now samples reach the DAC untouched.
- Volume 21 = −6 dB, the most wired earbuds want from this line-level output.
  2 dB per step.
- The DAC runs from 5 V that, on battery, comes from the charger's boost. That boost
  is switched on only while music plays.

Supported formats: MP3, WAV, M4A/AAC, FLAC.
