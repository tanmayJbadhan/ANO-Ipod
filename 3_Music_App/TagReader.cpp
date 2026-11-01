#include "TagReader.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Text: every tag format ends up as Unicode code points, which are folded
// to ASCII one at a time into a fixed-size buffer.
// ---------------------------------------------------------------------------

namespace {

struct Sink {
  char *dst;
  size_t cap;
  size_t len;
  Sink(char *d, size_t c) : dst(d), cap(c), len(0) { if (cap) dst[0] = '\0'; }
  void put(char c) {
    if (len + 1 < cap) { dst[len++] = c; dst[len] = '\0'; }
  }
};

// Latin-1 Supplement letters U+00C0..U+00FF without their accents.
const char LATIN1[] =
    "AAAAAAACEEEEIIII"  // C0
    "DNOOOOOxOUUUUYTs"  // D0
    "aaaaaaaceeeeiiii"  // E0
    "dnooooo/ouuuuyty"; // F0

void putFolded(Sink &s, uint32_t cp) {
  if (cp == 0) return;
  if (cp < 0x20) { s.put(' '); return; }
  if (cp < 0x7F) { s.put((char)cp); return; }
  if (cp >= 0xC0 && cp <= 0xFF) { s.put(LATIN1[cp - 0xC0]); return; }
  switch (cp) {
    case 0x2018: case 0x2019: case 0x00B4: s.put('\''); return;
    case 0x201C: case 0x201D: s.put('"'); return;
    case 0x2013: case 0x2014: s.put('-'); return;
    case 0x2026: s.put('.'); s.put('.'); s.put('.'); return;
    case 0x00A0: s.put(' '); return;
    case 0x0152: s.put('O'); s.put('E'); return;
    case 0x0153: s.put('o'); s.put('e'); return;
  }
  // Latin Extended-A (Polish, Czech, Turkish...): close enough by range.
  if (cp >= 0x0100 && cp <= 0x017F) {
    static const char EXT_A[] =
        "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
    size_t i = cp - 0x0100;
    s.put(i < sizeof(EXT_A) - 1 ? EXT_A[i] : '?');
    return;
  }
  s.put('?');
}

void trimRight(char *s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ')) s[--n] = '\0';
}

// Decodes UTF-8 (tolerating junk bytes, which are treated as Latin-1).
void appendUtf8(Sink &s, const uint8_t *p, size_t n) {
  size_t i = 0;
  while (i < n && p[i]) {
    uint8_t c = p[i];
    uint32_t cp;
    size_t extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { putFolded(s, c); i++; continue; }
    if (i + extra >= n + (extra ? 0 : 1)) break;
    bool ok = true;
    for (size_t k = 1; k <= extra; k++) {
      if ((p[i + k] & 0xC0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if (!ok) { putFolded(s, c); i++; continue; }
    putFolded(s, cp);
    i += extra + 1;
  }
}

void appendUtf16(Sink &s, const uint8_t *p, size_t n, bool bigEndian) {
  for (size_t i = 0; i + 1 < n; i += 2) {
    uint32_t u = bigEndian ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
    if (u == 0) break;
    if (u == 0xFEFF) continue;
    if (u >= 0xD800 && u <= 0xDBFF && i + 3 < n) { // surrogate pair
      uint32_t lo = bigEndian ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
      u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
      i += 2;
    }
    putFolded(s, u);
  }
}

// ID3 text frame: first byte is the encoding.
void id3Text(const uint8_t *p, size_t n, char *dst, size_t cap) {
  Sink s(dst, cap);
  if (n < 1) return;
  uint8_t enc = p[0];
  p++; n--;
  if (enc == 0) {
    for (size_t i = 0; i < n && p[i]; i++) putFolded(s, p[i]);
  } else if (enc == 1) {
    bool be = !(n >= 2 && p[0] == 0xFF && p[1] == 0xFE);
    appendUtf16(s, p, n, be);
  } else if (enc == 2) {
    appendUtf16(s, p, n, true);
  } else {
    appendUtf8(s, p, n);
  }
  trimRight(dst);
}

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
uint32_t syncsafe(const uint8_t *p) { return (p[0] & 0x7F) << 21 | (p[1] & 0x7F) << 14 | (p[2] & 0x7F) << 7 | (p[3] & 0x7F); }

bool readAt(File &f, uint32_t pos, uint8_t *buf, size_t n) {
  if (!f.seek(pos)) return false;
  return f.read(buf, n) == (int)n;
}

// ---------------------------------------------------------------------------
// ID3v2 (start of file) and ID3v1 (last 128 bytes)
// ---------------------------------------------------------------------------

bool readId3v2(File &f, Tags &t) {
  uint8_t h[10];
  if (!readAt(f, 0, h, 10) || memcmp(h, "ID3", 3) != 0) return false;
  uint8_t ver = h[3];
  if (ver < 2 || ver > 4) return false;
  uint32_t end = 10 + syncsafe(h + 6);
  uint32_t pos = 10;

  if (ver >= 3 && (h[5] & 0x40)) { // extended header
    uint8_t e[4];
    if (!readAt(f, pos, e, 4)) return false;
    pos += (ver == 4) ? syncsafe(e) : be32(e) + 4;
  }

  bool any = false;
  uint8_t buf[260];
  const size_t hdr = (ver == 2) ? 6 : 10;
  while (pos + hdr < end) {
    uint8_t fh[10];
    if (!readAt(f, pos, fh, hdr)) break;
    if (fh[0] == 0) break; // padding
    char id[5] = {0};
    uint32_t size;
    uint32_t data = pos + hdr;
    if (ver == 2) {
      memcpy(id, fh, 3);
      size = (uint32_t)fh[3] << 16 | fh[4] << 8 | fh[5];
    } else {
      memcpy(id, fh, 4);
      size = (ver == 4) ? syncsafe(fh + 4) : be32(fh + 4);
      if (ver == 4 && (fh[9] & 0x01)) { data += 4; size = size >= 4 ? size - 4 : 0; } // data length indicator
    }
    if (size == 0 || data + size > end + 16) break;

    char *dst = nullptr;
    size_t cap = 0;
    bool isTrack = false;
    if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) { dst = t.title; cap = sizeof(t.title); }
    else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) { dst = t.artist; cap = sizeof(t.artist); }
    else if (!strcmp(id, "TPE2") || !strcmp(id, "TP2")) { dst = t.albumArtist; cap = sizeof(t.albumArtist); }
    else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) { dst = t.album; cap = sizeof(t.album); }
    else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK")) { isTrack = true; }

    if (dst || isTrack) {
      size_t n = size < sizeof(buf) ? size : sizeof(buf);
      if (readAt(f, data, buf, n)) {
        if (isTrack) {
          char num[16];
          id3Text(buf, n, num, sizeof(num));
          t.track = (uint16_t)atoi(num);
        } else {
          id3Text(buf, n, dst, cap);
        }
        any = true;
      }
    }
    pos = data + size;
  }
  return any;
}

bool readId3v1(File &f, Tags &t) {
  size_t sz = f.size();
  if (sz < 128) return false;
  uint8_t b[128];
  if (!readAt(f, sz - 128, b, 128) || memcmp(b, "TAG", 3) != 0) return false;
  auto field = [](const uint8_t *p, size_t n, char *dst, size_t cap) {
    if (dst[0]) return; // keep anything ID3v2 already found
    Sink s(dst, cap);
    for (size_t i = 0; i < n && p[i]; i++) putFolded(s, p[i]);
    trimRight(dst);
  };
  field(b + 3, 30, t.title, sizeof(t.title));
  field(b + 33, 30, t.artist, sizeof(t.artist));
  field(b + 63, 30, t.album, sizeof(t.album));
  if (!t.track && b[125] == 0 && b[126]) t.track = b[126];
  return true;
}

// ---------------------------------------------------------------------------
// MP4 / M4A: moov > udta > meta > ilst > (item) > data
// ---------------------------------------------------------------------------

// Finds the child box `type` inside [start, end). Returns its payload range.
bool findBox(File &f, uint32_t start, uint32_t end, const char *type, uint32_t &pStart, uint32_t &pEnd) {
  uint32_t pos = start;
  while (pos + 8 <= end) {
    uint8_t h[16];
    if (!readAt(f, pos, h, 8)) return false;
    uint64_t size = be32(h);
    uint32_t hdr = 8;
    if (size == 1) {
      if (!readAt(f, pos + 8, h + 8, 8)) return false;
      size = ((uint64_t)be32(h + 8) << 32) | be32(h + 12);
      hdr = 16;
    } else if (size == 0) {
      size = end - pos;
    }
    if (size < hdr || pos + size > (uint64_t)end + 0) return false;
    if (memcmp(h + 4, type, 4) == 0) {
      pStart = pos + hdr;
      pEnd = pos + (uint32_t)size;
      return true;
    }
    pos += (uint32_t)size;
  }
  return false;
}

bool readMp4(File &f, Tags &t) {
  uint32_t fileEnd = f.size();
  uint32_t ms, me, us, ue, mts, mte, is, ie;
  if (!findBox(f, 0, fileEnd, "moov", ms, me)) return false;
  bool haveMeta = false;
  if (findBox(f, ms, me, "udta", us, ue) && findBox(f, us, ue, "meta", mts, mte)) haveMeta = true;
  else if (findBox(f, ms, me, "meta", mts, mte)) haveMeta = true;
  if (!haveMeta) return false;
  if (!findBox(f, mts + 4, mte, "ilst", is, ie)) return false; // meta is a full box: skip version/flags

  bool any = false;
  uint32_t pos = is;
  uint8_t buf[260];
  while (pos + 8 <= ie) {
    uint8_t h[8];
    if (!readAt(f, pos, h, 8)) break;
    uint32_t size = be32(h);
    if (size < 8 || pos + size > ie) break;

    char *dst = nullptr;
    size_t cap = 0;
    bool isTrack = false;
    const uint8_t *k = h + 4;
    if (k[0] == 0xA9 && !memcmp(k + 1, "nam", 3)) { dst = t.title; cap = sizeof(t.title); }
    else if (k[0] == 0xA9 && !memcmp(k + 1, "ART", 3)) { dst = t.artist; cap = sizeof(t.artist); }
    else if (k[0] == 0xA9 && !memcmp(k + 1, "alb", 3)) { dst = t.album; cap = sizeof(t.album); }
    else if (!memcmp(k, "aART", 4)) { dst = t.albumArtist; cap = sizeof(t.albumArtist); }
    else if (!memcmp(k, "trkn", 4)) { isTrack = true; }

    uint32_t ds, de;
    if ((dst || isTrack) && findBox(f, pos + 8, pos + size, "data", ds, de) && de > ds + 8) {
      size_t n = de - ds - 8; // skip type + locale
      if (n > sizeof(buf)) n = sizeof(buf);
      if (readAt(f, ds + 8, buf, n)) {
        if (isTrack) {
          if (n >= 4) t.track = (uint16_t)(buf[2] << 8 | buf[3]);
        } else {
          Sink s(dst, cap);
          appendUtf8(s, buf, n);
          trimRight(dst);
        }
        any = true;
      }
    }
    pos += size;
  }
  return any;
}

// ---------------------------------------------------------------------------
// FLAC: metadata block 4 holds "KEY=value" Vorbis comments
// ---------------------------------------------------------------------------

bool readFlac(File &f, Tags &t) {
  uint8_t h[4];
  if (!readAt(f, 0, h, 4) || memcmp(h, "fLaC", 4) != 0) return false;
  uint32_t pos = 4;
  for (int guard = 0; guard < 64; guard++) {
    if (!readAt(f, pos, h, 4)) return false;
    bool last = h[0] & 0x80;
    uint8_t type = h[0] & 0x7F;
    uint32_t len = (uint32_t)h[1] << 16 | h[2] << 8 | h[3];
    uint32_t data = pos + 4;
    if (type == 4) {
      uint8_t b[4];
      if (!readAt(f, data, b, 4)) return false;
      uint32_t p = data + 4 + le32(b); // skip vendor string
      if (!readAt(f, p, b, 4)) return false;
      uint32_t count = le32(b);
      p += 4;
      bool any = false;
      uint8_t buf[260];
      for (uint32_t i = 0; i < count && i < 200 && p < data + len; i++) {
        if (!readAt(f, p, b, 4)) break;
        uint32_t clen = le32(b);
        size_t n = clen < sizeof(buf) - 1 ? clen : sizeof(buf) - 1;
        if (!readAt(f, p + 4, buf, n)) break;
        buf[n] = 0;
        const char *s = (const char *)buf;
        const char *eq = strchr(s, '=');
        if (eq) {
          size_t klen = eq - s;
          const uint8_t *v = (const uint8_t *)eq + 1;
          size_t vlen = n - klen - 1;
          char *dst = nullptr;
          size_t cap = 0;
          if (klen == 5 && !strncasecmp(s, "TITLE", 5)) { dst = t.title; cap = sizeof(t.title); }
          else if (klen == 6 && !strncasecmp(s, "ARTIST", 6)) { dst = t.artist; cap = sizeof(t.artist); }
          else if (klen == 11 && !strncasecmp(s, "ALBUMARTIST", 11)) { dst = t.albumArtist; cap = sizeof(t.albumArtist); }
          else if (klen == 5 && !strncasecmp(s, "ALBUM", 5)) { dst = t.album; cap = sizeof(t.album); }
          else if (klen == 11 && !strncasecmp(s, "TRACKNUMBER", 11)) t.track = (uint16_t)atoi((const char *)v);
          if (dst && !dst[0]) {
            Sink sk(dst, cap);
            appendUtf8(sk, v, vlen);
            trimRight(dst);
          }
          any = true;
        }
        p += 4 + clen;
      }
      return any;
    }
    if (last) break;
    pos = data + len;
  }
  return false;
}

} // namespace

namespace TagReader {

bool read(File &f, Tags &out) {
  memset(&out, 0, sizeof(out));
  bool any = false;
  uint8_t magic[8] = {0};
  readAt(f, 0, magic, 8);

  if (!memcmp(magic, "fLaC", 4)) any = readFlac(f, out);
  else if (!memcmp(magic + 4, "ftyp", 4)) any = readMp4(f, out);
  else {
    any = readId3v2(f, out);
    if (!out.title[0] || !out.artist[0]) any = readId3v1(f, out) || any;
  }

  // Tags that folded to mostly '?' are no use on this screen.
  if (mostlyUnknown(out.title)) out.title[0] = '\0';
  if (mostlyUnknown(out.artist)) out.artist[0] = '\0';
  if (mostlyUnknown(out.albumArtist)) out.albumArtist[0] = '\0';
  if (mostlyUnknown(out.album)) out.album[0] = '\0';
  return any;
}

void foldUtf8(const char *src, char *dst, size_t dstLen) {
  Sink s(dst, dstLen);
  appendUtf8(s, (const uint8_t *)src, strlen(src));
  trimRight(dst);
}

bool mostlyUnknown(const char *s) {
  size_t n = 0, q = 0;
  for (; *s; s++) {
    if (*s == ' ') continue;
    n++;
    if (*s == '?') q++;
  }
  return n > 0 && q * 2 > n;
}

} // namespace TagReader
