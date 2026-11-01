#pragma once

#include <stdint.h>
#include <stddef.h>

// Every song on the SD card (all folders), with the title / artist / album
// read from its tags, plus the sorted Artist and Album lists the browser
// shows. Reading tags is slow, so the results are kept in
// /.jiggly/library.txt on the card; later scans only list the folders and
// read tags from songs that weren't there before.
//
// Songs are referred to by id (index into the library). Ids change when
// the library is rescanned, so anything holding ids (the play queue,
// playlists) is rebuilt after a scan.
struct Track {
  const char *path;        // absolute path on the card
  const char *title;
  const char *artist;      // the song's own artist
  const char *albumArtist; // who the album is filed under (album artist, else artist)
  const char *album;
  uint32_t size;
  uint16_t trackNo;        // 0 = unknown
};

namespace Library {

static const uint16_t MAX_TRACKS = 2000;
static const uint16_t NONE = 0xFFFF;

// Called now and then during a scan so the screen can show progress.
typedef void (*ProgressFn)(uint32_t songsFound, uint32_t tagsRead);

// Walks the card (which must be mounted). Returns false if out of memory.
bool scan(ProgressFn progress = nullptr);
bool ready();
// Songs were added or removed: scan again next time Music opens.
void markDirty();
bool dirty();

uint16_t count();
const Track &track(uint16_t id);
uint16_t findByPath(const char *path); // NONE if not in the library

// Artists (by album artist), A-Z.
uint16_t artistCount();
const char *artistName(uint16_t i);
uint16_t artistSongs(uint16_t i);
// The artist's songs, ordered by album then track number. Returns how many.
uint16_t songsByArtist(uint16_t artist, uint16_t *out, uint16_t max);

// Albums, A-Z.
uint16_t albumCount();
const char *albumName(uint16_t i);
const char *albumArtistName(uint16_t i);
uint16_t albumSongs(uint16_t i);
uint16_t songsOnAlbum(uint16_t album, uint16_t *out, uint16_t max);

} // namespace Library
