import sys
from PIL import Image

src, dst, target_h = sys.argv[1], sys.argv[2], int(sys.argv[3])
im = Image.open(src).convert("RGBA")
im = im.crop(im.getbbox())
w = round(im.width * target_h / im.height)
im = im.resize((w, target_h), Image.LANCZOS)
px = im.load()

rgb, alpha = [], []
for y in range(im.height):
    for x in range(im.width):
        r, g, b, a = px[x, y]
        rgb.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
        alpha.append(a)

def rows(vals, fmt, per):
    return ",\n".join("  " + ", ".join(fmt % v for v in vals[i:i + per]) for i in range(0, len(vals), per))

with open(dst, "w") as f:
    f.write("#pragma once\n// Generated from the Jigglypuff artwork by make_art.py - do not edit.\n")
    f.write("#include <stdint.h>\n\n")
    f.write(f"static const int16_t JIGGLY_W = {im.width};\nstatic const int16_t JIGGLY_H = {im.height};\n\n")
    f.write("static const uint16_t JIGGLY_RGB[] = {\n" + rows(rgb, "0x%04X", 16) + "\n};\n\n")
    f.write("static const uint8_t JIGGLY_ALPHA[] = {\n" + rows(alpha, "%d", 24) + "\n};\n")
print(im.width, im.height)
