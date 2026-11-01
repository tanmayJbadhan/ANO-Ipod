#pragma once

#include <FS.h>
#include <stdint.h>
#include <stddef.h>

// Reads the title / artist / album / track number stored inside a song
// file: ID3v2 and ID3v1 (MP3, AAC), MP4 atoms (M4A) and Vorbis comments
// (FLAC). Text comes back folded to plain ASCII, since the screen font has
// nothing else - accents lose their marks, other scripts become '?'.
struct Tags {
  char title[96];
  char artist[64];
  char albumArtist[64];
  char album[80];
  uint16_t track; // 0 = unknown
};

namespace TagReader {

// Fills `out` from whatever tags the file carries; fields it can't find are
// left empty. Returns true if anything at all was found.
bool read(File &f, Tags &out);

// Folds UTF-8 text to ASCII the same way (used for file and folder names).
void foldUtf8(const char *src, char *dst, size_t dstLen);

// True when folding lost most of the text (e.g. a non-Latin title), so the
// caller should prefer some other name.
bool mostlyUnknown(const char *s);

} // namespace TagReader
