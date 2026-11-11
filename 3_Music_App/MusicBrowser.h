#pragma once

#include <stdint.h>
#include "Input.h"
#include "Landscape.h"

// The Music app's screens: the top menu (four tiles - Now playing / Artists
// / Albums / Playlists), the artist, album and playlist lists, and the songs
// inside each. Also the "Add to playlist" picker, which opens over any screen.
//
//   wheel         move up / down          SELECT  open / play
//   LEFT / RIGHT  page up / down          UP      back
//                 (on the tiles: previous / next tile)
//   DOWN          on a song: add to playlist (or remove, inside a playlist)
//
// The main program draws the background and status bar, then calls draw();
// it owns the player screen and starts playback when key() returns Play.
namespace MusicBrowser {

struct Colors {
  uint16_t text, dim, faint, pink, green, blue, red, neutral;
};

enum class Action {
  None,
  Redraw,
  Exit,       // UP on the top menu: back to Home
  Play,       // a song was chosen: the queue is set, start at playIndex()
  ShowPlayer, // "Now playing" was chosen
};

void begin(LandscapeGFX *gfx, const Colors &colors);
void reset(); // back to the top menu (also after a rescan)

void draw();
bool atTop(); // showing the tiles (no list panel behind them)

Action key(Key k);
Action wheel(int32_t clicks);
Action tap(int16_t x, int16_t y);
// DOWN released (and not used for brightness).
Action down();
uint16_t playIndex();

// "Add to playlist" for one song, over whatever screen is showing.
// inPlaylist >= 0 also offers "Remove from" that playlist.
void openPicker(uint16_t trackId, int inPlaylist = -1);
bool pickerOpen();
void drawPicker();
Action pickerKey(Key k);
Action pickerWheel(int32_t clicks);
Action pickerTap(int16_t x, int16_t y);

// Short confirmations ("Added to Playlist 2"). drawToast() goes last.
void drawToast();
// True once when a toast has just disappeared and the screen needs redrawing.
bool toastEnded();

} // namespace MusicBrowser
