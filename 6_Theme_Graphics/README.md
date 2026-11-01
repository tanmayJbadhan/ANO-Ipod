# 6 · Theme Graphics

The console's look.

| File | What it does |
|---|---|
| `Glass.*` | The "liquid glass" style: a soft colour background computed once at start-up, with see-through panels blended over it (bright rim, sheen). On Home the background is black with Jigglypuff faded in and the glass is flat. |
| `Landscape.*` | Turns the portrait 280×456 screen sideways into 456×280 (and upside down, to match how the board sits in the case), including touch. The display chip can't rotate by itself, so this is done in software. |
| `JigglypuffArt.h` | The Home-screen Jigglypuff as pixel data (colours + transparency). Generated — don't edit by hand. |
| `source/jigglypuff.png` | The original artwork (licensed for this project). |
| `tools/make_art.py` | Converts a PNG into `JigglypuffArt.h`. |

**Changing the Home image**

```
python tools/make_art.py source/your-image.png JigglypuffArt.h 264
```

The last number is the height in pixels (264 fits the screen). Then run `flash.bat`.
How strongly it shows through is `ART_OPACITY` in `Glass.cpp` (0–256, currently 125).
Needs Python with Pillow (`pip install pillow`).
