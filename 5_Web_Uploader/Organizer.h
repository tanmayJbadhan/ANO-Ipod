#pragma once

#include <WebServer.h>
#include <stdint.h>

// The music organizer at http://jigglypuff.local/organize (same web server
// as Add songs, only while Add songs is open): search the library, fix
// missing artists / albums / titles one at a time or many at once, and
// make, rename, fill and delete playlists. Edits are stored on the card by
// Library::setText - the song files themselves are never rewritten.
namespace Organizer {

// Adds the organizer's page and its /api/... routes to the server.
void attach(WebServer *server);

// Uploads counted (SongUpload::filesAdded) when the organizer last
// rescanned, so leaving Add songs only rescans if songs arrived since.
uint32_t uploadsAtLastScan();

} // namespace Organizer
