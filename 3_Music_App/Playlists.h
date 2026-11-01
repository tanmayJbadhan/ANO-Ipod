#pragma once

#include <stdint.h>

// Playlists, stored on the card as /Playlists/<name>.m3u (one song path per
// line), so a computer's music player can open them too. New ones are named
// "Playlist 1", "Playlist 2", ... Songs are referred to by Library id, so
// call load() again after every Library::scan().
namespace Playlists {

static const uint8_t MAX_LISTS = 32;

void load();
uint8_t count();
const char *name(uint8_t i);
uint16_t size(uint8_t i);
uint16_t songs(uint8_t i, uint16_t *out, uint16_t max);
bool contains(uint8_t i, uint16_t trackId);

// Makes the next "Playlist N". Returns its index, or -1.
int create();
// Both save straight to the card. add() is false if the song was already in it.
bool add(uint8_t i, uint16_t trackId);
bool remove(uint8_t i, uint16_t trackId);

} // namespace Playlists
