# 1 · Examples

LilyGo's original example sketches for the T-Display-S3-AMOLED-1.64. They're not
part of the console firmware. They're handy for testing one piece of hardware at a
time. LilyGo's own instructions are in [LilyGo_README.md](LilyGo_README.md)
([中文](LilyGo_README_CN.md)).

| Example | Tests |
|---|---|
| `Original_Test` | the factory demo: screen, touch, charger |
| `GFX`, `GFX_FT3168_Image` | drawing on the AMOLED, plus touch |
| `FT3168` | the touch controller alone |
| `SY6970`, `SY6970_OTG` | the charger chip, and its 5 V boost (what powers the DAC on battery) |
| `Deep_Sleep_Wake_Up`, `Light_Sleep_Wake_Up` | sleep modes |
| `Lvgl`, `Lvgl_benchmark` | the LVGL UI library |
| `Rotary_Encoder_Colors` | the ANO wheel and buttons (needs the Adafruit Seesaw and NeoPixel libraries) |

Open them in the Arduino IDE with the libraries from `../libraries` installed (see
LilyGo's README). `flash.bat` always builds the console, never these.
