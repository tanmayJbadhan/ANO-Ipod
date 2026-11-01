# 5 · Web Uploader

Adds songs to the console from a phone, and keeps the clock right.

| File | What it does |
|---|---|
| `SongUpload.*` | While **Add songs** is open, serves a web page at `http://jigglypuff.local` (or the IP shown on screen). Pick songs on the phone and they're written straight to the SD card. |
| `TimeSync.*` | Turns WiFi on and off (it's only ever on inside Add songs, to save battery), and sets the clock from the internet while connected. |
| `WifiConfig.h` | Your WiFi name, password and time zone. **Private — never share or commit it.** |
| `WifiConfig.example.h` | Empty template for `WifiConfig.h`. |

Music stops while Add songs is open: the SD card can't be read for playback and
written by uploads at the same time.
