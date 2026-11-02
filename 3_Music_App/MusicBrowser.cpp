#include "MusicBrowser.h"
#include "Library.h"
#include "Playlists.h"
#include "MusicPlayer.h"
#include "Glass.h"
#include <Arduino.h>
#include <string.h>

namespace {

LandscapeGFX *g = nullptr;
MusicBrowser::Colors C;

// Layout (456 x 280 landscape; the status bar sits above y = 40).
const int16_t LIST_X = 24, LIST_W = 408;
const int16_t TITLE_Y = 46;
const int16_t ROW_Y0 = 70, ROW_H = 33;
const int ROWS = 6;
const int CHAR_W = 12; // text size 2

enum class Kind : uint8_t { Root, Artists, Albums, Playlists, Songs };
enum class SongSource : uint8_t { Artist, Album, Playlist };

struct Level {
  Kind kind;
  int16_t sel;
  int16_t top;
};

Level stack[4];
int depth = 0;

uint16_t songIds[Library::MAX_TRACKS];
uint16_t songN = 0;
SongSource songSource = SongSource::Artist;
int16_t songCtx = 0;
char songTitle[48];
uint16_t chosen = 0;

Level &cur() { return stack[depth]; }

void text(int16_t x, int16_t y, const char *s, uint8_t size, uint16_t color) {
  g->drawText(x, y, s, size, color, Glass::CLEAR);
}

// Copies s into out, cut to maxChars with "..." when it doesn't fit.
void clip(const char *s, int maxChars, char *out, size_t outLen) {
  if (maxChars < 4) maxChars = 4;
  size_t n = strlen(s);
  if ((int)n <= maxChars) {
    strlcpy(out, s, outLen);
    return;
  }
  size_t keep = (size_t)maxChars - 3;
  if (keep + 4 > outLen) keep = outLen - 4;
  memcpy(out, s, keep);
  // don't leave a dangling space before the dots
  while (keep && out[keep - 1] == ' ') keep--;
  memcpy(out + keep, "...", 4);
}

// --- rows for each kind of list ------------------------------------------------

// The top menu is a 2 x 2 grid of tiles, in this order.
enum RootItem { R_NOW, R_ARTISTS, R_ALBUMS, R_PLAYLISTS };
const int16_t TILE_X0 = 24, TILE_Y0 = 46, TILE_W = 200, TILE_H = 106, TILE_GAP = 8;

void tileRect(int i, int16_t &x, int16_t &y) {
  x = TILE_X0 + (i % 2) * (TILE_W + TILE_GAP);
  y = TILE_Y0 + (i / 2) * (TILE_H + TILE_GAP);
}

int countOf(Kind k) {
  switch (k) {
    case Kind::Root: return 4;
    case Kind::Artists: return Library::artistCount();
    case Kind::Albums: return Library::albumCount();
    case Kind::Playlists: return Playlists::count() + 1; // + "New playlist"
    case Kind::Songs: return songN;
  }
  return 0;
}

const char *titleOf(Kind k) {
  switch (k) {
    case Kind::Root: return "Music";
    case Kind::Artists: return "Artists";
    case Kind::Albums: return "Albums";
    case Kind::Playlists: return "Playlists";
    case Kind::Songs: return songTitle;
  }
  return "";
}

// Left text, right (dim) text, and whether the row is the song now playing.
void rowText(Kind k, int i, char *left, size_t ll, char *right, size_t rl, bool &playing) {
  left[0] = right[0] = '\0';
  playing = false;
  switch (k) {
    case Kind::Root: break; // drawn as tiles, see drawTiles()
    case Kind::Artists:
      strlcpy(left, Library::artistName(i), ll);
      snprintf(right, rl, "%u", Library::artistSongs(i));
      break;
    case Kind::Albums:
      strlcpy(left, Library::albumName(i), ll);
      strlcpy(right, Library::albumArtistName(i), rl);
      break;
    case Kind::Playlists:
      if (i == 0) {
        strlcpy(left, "+ New playlist", ll);
      } else {
        strlcpy(left, Playlists::name(i - 1), ll);
        snprintf(right, rl, "%u", Playlists::size(i - 1));
      }
      break;
    case Kind::Songs: {
      const Track &t = Library::track(songIds[i]);
      strlcpy(left, t.title, ll);
      // Inside an artist the album is useful; inside a playlist, the artist.
      if (songSource == SongSource::Artist) strlcpy(right, t.album, rl);
      else if (songSource == SongSource::Playlist) strlcpy(right, t.artist, rl);
      playing = MusicPlayer::queueLength() && songIds[i] == MusicPlayer::currentTrack();
      break;
    }
  }
}

void keepVisible(Level &l, int count) {
  if (l.sel >= count) l.sel = count - 1;
  if (l.sel < 0) l.sel = 0;
  if (l.sel < l.top) l.top = l.sel;
  if (l.sel >= l.top + ROWS) l.top = l.sel - ROWS + 1;
  if (l.top > count - ROWS) l.top = count - ROWS;
  if (l.top < 0) l.top = 0;
}

void loadSongs() {
  if (songSource == SongSource::Artist) {
    songN = Library::songsByArtist(songCtx, songIds, Library::MAX_TRACKS);
    strlcpy(songTitle, Library::artistName(songCtx), sizeof(songTitle));
  } else if (songSource == SongSource::Album) {
    songN = Library::songsOnAlbum(songCtx, songIds, Library::MAX_TRACKS);
    strlcpy(songTitle, Library::albumName(songCtx), sizeof(songTitle));
  } else {
    songN = Playlists::songs(songCtx, songIds, Library::MAX_TRACKS);
    strlcpy(songTitle, Playlists::name(songCtx), sizeof(songTitle));
  }
}

void push(Kind k) {
  if (depth >= 3) return;
  depth++;
  stack[depth] = {k, 0, 0};
}

void openSongs(SongSource src, int ctx) {
  songSource = src;
  songCtx = ctx;
  loadSongs();
  push(Kind::Songs);
}

// --- toast ------------------------------------------------------------------

char toastMsg[34]; // 33 characters fill the screen width
uint32_t toastUntil = 0;
bool toastShowing = false;

void toast(const char *fmt, const char *arg) {
  snprintf(toastMsg, sizeof(toastMsg), fmt, arg);
  toastUntil = millis() + 1800;
  toastShowing = true;
}

// --- picker -------------------------------------------------------------------

bool pickOpen = false;
uint16_t pickTrack = 0;
int pickFrom = -1; // playlist offered for removal, or -1
int pickSel = 0, pickTop = 0;
const int PICK_ROWS = 4;
const int16_t PK_X = 70, PK_Y = 46, PK_W = 316, PK_H = 200;
const int16_t PK_ROW_Y0 = PK_Y + 50, PK_ROW_H = 34;

int pickCount() { return (pickFrom >= 0 ? 1 : 0) + 1 + Playlists::count(); }

// Row i of the picker: -2 = remove, -1 = new playlist, >= 0 = that playlist.
int pickRow(int i) {
  if (pickFrom >= 0) {
    if (i == 0) return -2;
    i--;
  }
  return i == 0 ? -1 : i - 1;
}

MusicBrowser::Action pickChoose() {
  int r = pickRow(pickSel);
  pickOpen = false;
  if (r == -2) {
    Playlists::remove(pickFrom, pickTrack);
    toast("Removed from %s", Playlists::name(pickFrom));
    // Refresh the list being looked at if it was that playlist.
    if (cur().kind == Kind::Songs && songSource == SongSource::Playlist && songCtx == pickFrom) {
      loadSongs();
      keepVisible(cur(), songN);
    }
  } else {
    int p = (r == -1) ? Playlists::create() : r;
    if (p < 0) {
      toast("%s", "Couldn't save playlist");
    } else if (Playlists::add(p, pickTrack)) {
      toast("Added to %s", Playlists::name(p));
    } else {
      toast("Already in %s", Playlists::name(p));
    }
    if (cur().kind == Kind::Songs && songSource == SongSource::Playlist && songCtx == p) loadSongs();
  }
  return MusicBrowser::Action::Redraw;
}

// --- the top menu's tiles ----------------------------------------------------

void iconNowPlaying(int16_t cx, int16_t cy, uint16_t c, uint16_t cut, bool paused) {
  g->fillCircle(cx, cy, 20, c);
  if (paused) {
    g->fillTriangle(cx - 6, cy - 10, cx - 6, cy + 10, cx + 11, cy, cut);
  } else {
    g->fillRect(cx - 8, cy - 9, 6, 18, cut);
    g->fillRect(cx + 2, cy - 9, 6, 18, cut);
  }
}

void iconArtist(int16_t cx, int16_t cy, uint16_t c) {
  g->fillCircle(cx, cy - 9, 9, c);                  // head
  g->fillRoundRect(cx - 17, cy + 3, 34, 18, 9, c);  // shoulders
}

void iconAlbum(int16_t cx, int16_t cy, uint16_t c, uint16_t cut) {
  g->fillCircle(cx, cy, 20, c);
  g->drawCircle(cx, cy, 14, cut);
  g->fillCircle(cx, cy, 5, cut);
}

void iconPlaylist(int16_t cx, int16_t cy, uint16_t c) {
  for (int i = 0; i < 3; i++) g->fillRoundRect(cx - 20, cy - 14 + i * 10, i == 2 ? 16 : 24, 5, 2, c);
  g->fillCircle(cx + 12, cy + 12, 6, c); // a note at the end of the list
  g->fillRect(cx + 15, cy - 12, 3, 24, c);
  g->fillRect(cx + 15, cy - 12, 9, 3, c);
}

void drawTiles(int sel) {
  static const char *labels[4] = {"Now playing", "Artists", "Albums", "Playlists"};
  const uint16_t tints[4] = {C.pink, C.blue, C.green, C.neutral};
  uint16_t now = MusicPlayer::currentTrack();
  for (int i = 0; i < 4; i++) {
    int16_t x, y, w = TILE_W, h = TILE_H;
    tileRect(i, x, y);
    bool on = (i == sel);
    if (on) { x -= 4; y -= 4; w += 8; h += 8; } // the focused tile grows and glows, as on Home
    Glass::panel(x, y, w, h, 24, tints[i], on ? 150 : 70, on ? 140 : 0);

    int16_t cx = x + w / 2, cy = y + 34;
    uint16_t cut = Glass::blend(tints[i], 0x0000, 90);
    switch (i) {
      case R_NOW: iconNowPlaying(cx, cy, C.text, cut, MusicPlayer::isPaused() || now == Library::NONE); break;
      case R_ARTISTS: iconArtist(cx, cy, C.text); break;
      case R_ALBUMS: iconAlbum(cx, cy, C.text, cut); break;
      case R_PLAYLISTS: iconPlaylist(cx, cy, C.text); break;
    }
    int16_t lw = (int16_t)strlen(labels[i]) * CHAR_W;
    text(cx - lw / 2, y + 62, labels[i], 2, on ? C.text : C.dim);

    char sub[40], clipped[34];
    switch (i) {
      case R_NOW:
        if (now == Library::NONE) strlcpy(sub, "Nothing yet", sizeof(sub));
        else strlcpy(sub, Library::track(now).title, sizeof(sub));
        break;
      case R_ARTISTS: snprintf(sub, sizeof(sub), "%u artists", Library::artistCount()); break;
      case R_ALBUMS: snprintf(sub, sizeof(sub), "%u albums", Library::albumCount()); break;
      default: snprintf(sub, sizeof(sub), "%u playlists", Playlists::count()); break;
    }
    clip(sub, (w - 20) / 6, clipped, sizeof(clipped));
    int16_t sw = (int16_t)strlen(clipped) * 6;
    text(cx - sw / 2, y + 86, clipped, 1, C.dim);
  }
}

} // namespace

namespace MusicBrowser {

void begin(LandscapeGFX *gfx, const Colors &colors) {
  g = gfx;
  C = colors;
  reset();
}

void reset() {
  depth = 0;
  // Start on "Now playing" when something is queued, otherwise Artists.
  stack[0] = {Kind::Root, (int16_t)(MusicPlayer::queueLength() ? R_NOW : R_ARTISTS), 0};
  pickOpen = false;
}

uint16_t playIndex() { return chosen; }

bool atTop() { return cur().kind == Kind::Root; }

void draw() {
  Level &l = cur();
  if (l.kind == Kind::Root) {
    drawTiles(l.sel);
    return;
  }
  int count = countOf(l.kind);
  keepVisible(l, count);

  char t[40];
  clip(titleOf(l.kind), 30, t, sizeof(t));
  text(LIST_X + 10, TITLE_Y, t, 2, C.pink);

  if (count == 0) {
    const char *msg = "Nothing here yet";
    if (l.kind == Kind::Songs && songSource == SongSource::Playlist)
      msg = "Empty - press DOWN on a song to add it";
    else if (l.kind == Kind::Artists || l.kind == Kind::Albums)
      msg = "No songs on the card";
    text(LIST_X + 10, ROW_Y0 + 20, msg, 1, C.dim);
    return;
  }

  for (int r = 0; r < ROWS && l.top + r < count; r++) {
    int i = l.top + r;
    int16_t y = ROW_Y0 + r * ROW_H;
    bool sel = (i == l.sel);
    if (sel) Glass::panel(LIST_X, y, LIST_W - 14, ROW_H - 2, 14, C.pink, 140, 70);

    char left[100], right[80], a[48], b[24];
    bool playing;
    rowText(l.kind, i, left, sizeof(left), right, sizeof(right), playing);

    int16_t x = LIST_X + 12;
    if (playing) {
      // small "playing" triangle
      g->fillTriangle(x, y + 10, x, y + 22, x + 9, y + 16, sel ? C.text : C.pink);
      x += 16;
    }
    int avail = (LIST_W - 14 - (x - LIST_X) - 12) / CHAR_W;
    int rightChars = right[0] ? (int)strlen(right) : 0;
    if (rightChars > 0) {
      // Right text is small (size 1, 6 px) and gets at most a third of the row.
      int maxRight = (avail * CHAR_W / 3) / 6;
      clip(right, maxRight, b, sizeof(b));
      rightChars = strlen(b);
      avail -= (rightChars * 6 + 14 + CHAR_W - 1) / CHAR_W;
    }
    clip(left, avail, a, sizeof(a));
    text(x, y + 9, a, 2, sel ? C.text : (playing ? C.pink : C.text));
    if (rightChars > 0) {
      int16_t rx = LIST_X + LIST_W - 14 - 12 - rightChars * 6;
      text(rx, y + 13, b, 1, sel ? C.text : C.dim);
    }
  }

  // Scroll bar.
  if (count > ROWS) {
    int16_t trackY = ROW_Y0, trackH = ROWS * ROW_H - 2;
    int16_t thumbH = trackH * ROWS / count;
    if (thumbH < 14) thumbH = 14;
    int16_t thumbY = trackY + (int32_t)(trackH - thumbH) * l.top / (count - ROWS);
    g->fillRoundRect(LIST_X + LIST_W - 6, trackY, 4, trackH, 2, Glass::blend(C.faint, 0x0000, 90));
    g->fillRoundRect(LIST_X + LIST_W - 6, thumbY, 4, thumbH, 2, C.dim);
  }

  // Where am I in a long list.
  if (count > ROWS) {
    char pos[16];
    snprintf(pos, sizeof(pos), "%d / %d", l.sel + 1, count);
    text(LIST_X + LIST_W - 16 - (int)strlen(pos) * 6, TITLE_Y + 4, pos, 1, C.dim);
  }
}

static Action choose() {
  Level &l = cur();
  int count = countOf(l.kind);
  if (count == 0) return Action::None;
  switch (l.kind) {
    case Kind::Root: {
      switch (l.sel) {
        case R_NOW: return Action::ShowPlayer;
        case R_ARTISTS: push(Kind::Artists); break;
        case R_ALBUMS: push(Kind::Albums); break;
        case R_PLAYLISTS: push(Kind::Playlists); break;
      }
      return Action::Redraw;
    }
    case Kind::Artists: openSongs(SongSource::Artist, l.sel); return Action::Redraw;
    case Kind::Albums: openSongs(SongSource::Album, l.sel); return Action::Redraw;
    case Kind::Playlists:
      if (l.sel == 0) {
        int p = Playlists::create();
        if (p < 0) toast("%s", "Couldn't save playlist");
        else {
          toast("Made %s", Playlists::name(p));
          l.sel = p + 1;
        }
      } else {
        openSongs(SongSource::Playlist, l.sel - 1);
      }
      return Action::Redraw;
    case Kind::Songs:
      MusicPlayer::setQueue(songIds, songN);
      chosen = l.sel;
      return Action::Play;
  }
  return Action::None;
}

Action key(Key k) {
  Level &l = cur();
  int count = countOf(l.kind);
  if (l.kind == Kind::Root && (k == Key::Left || k == Key::Right)) {
    // Tiles: LEFT / RIGHT step through them (across, then down).
    int s = l.sel + (k == Key::Right ? 1 : -1);
    if (s < 0 || s >= count) return Action::None;
    l.sel = s;
    return Action::Redraw;
  }
  switch (k) {
    case Key::Select: return choose();
    case Key::Up:
      if (depth == 0) return Action::Exit;
      depth--;
      return Action::Redraw;
    case Key::Left:
      l.sel -= ROWS;
      keepVisible(l, count);
      return Action::Redraw;
    case Key::Right:
      l.sel += ROWS;
      keepVisible(l, count);
      return Action::Redraw;
    default:
      return Action::None;
  }
}

Action wheel(int32_t clicks) {
  Level &l = cur();
  int count = countOf(l.kind);
  if (count == 0) return Action::None;
  if (l.kind == Kind::Root) {
    // Two clicks per tile, as on Home, so a nudge doesn't skip one.
    static int32_t acc = 0;
    acc += clicks;
    int s = l.sel;
    while (acc >= 2) { acc -= 2; if (s < count - 1) s++; }
    while (acc <= -2) { acc += 2; if (s > 0) s--; }
    if (s == l.sel) return Action::None;
    l.sel = s;
    return Action::Redraw;
  }
  l.sel += clicks;
  keepVisible(l, count);
  return Action::Redraw;
}

Action tap(int16_t x, int16_t y) {
  Level &l = cur();
  int count = countOf(l.kind);
  if (l.kind == Kind::Root) {
    for (int i = 0; i < count; i++) {
      int16_t tx, ty;
      tileRect(i, tx, ty);
      if (x >= tx && x < tx + TILE_W && y >= ty && y < ty + TILE_H) {
        l.sel = i;
        return choose();
      }
    }
    return Action::None;
  }
  if (y < ROW_Y0 || x < LIST_X || x > LIST_X + LIST_W) return Action::None;
  int r = (y - ROW_Y0) / ROW_H;
  if (r >= ROWS || l.top + r >= count) return Action::None;
  l.sel = l.top + r;
  return choose();
}

Action down() {
  Level &l = cur();
  if (l.kind != Kind::Songs || songN == 0) return Action::None;
  openPicker(songIds[l.sel], songSource == SongSource::Playlist ? songCtx : -1);
  return Action::Redraw;
}

void openPicker(uint16_t trackId, int inPlaylist) {
  if (trackId == Library::NONE) return;
  pickOpen = true;
  pickTrack = trackId;
  pickFrom = inPlaylist;
  pickSel = (inPlaylist >= 0) ? 0 : (Playlists::count() > 0 ? 1 : 0); // default to the first playlist
  pickTop = 0;
}

bool pickerOpen() { return pickOpen; }

void drawPicker() {
  if (!pickOpen) return;
  Glass::panel(PK_X, PK_Y, PK_W, PK_H, 24, C.neutral, 60, 60);
  text(PK_X + 20, PK_Y + 14, "Add to playlist", 2, C.text);
  char song[40];
  clip(Library::track(pickTrack).title, 46, song, sizeof(song));
  text(PK_X + 20, PK_Y + 34, song, 1, C.dim);

  int count = pickCount();
  if (pickSel < pickTop) pickTop = pickSel;
  if (pickSel >= pickTop + PICK_ROWS) pickTop = pickSel - PICK_ROWS + 1;
  for (int r = 0; r < PICK_ROWS && pickTop + r < count; r++) {
    int i = pickTop + r;
    int16_t y = PK_ROW_Y0 + r * PK_ROW_H;
    bool sel = (i == pickSel);
    if (sel) Glass::panel(PK_X + 12, y, PK_W - 24, PK_ROW_H - 2, 14, C.pink, 150, 70);
    int row = pickRow(i);
    char label[48], a[28];
    uint16_t color = C.text;
    if (row == -2) {
      snprintf(label, sizeof(label), "Remove from %s", Playlists::name(pickFrom));
      color = sel ? C.text : C.red;
    } else if (row == -1) {
      strlcpy(label, "+ New playlist", sizeof(label));
    } else {
      strlcpy(label, Playlists::name(row), sizeof(label));
    }
    clip(label, 21, a, sizeof(a));
    text(PK_X + 26, y + 9, a, 2, color);
    if (row >= 0 && Playlists::contains(row, pickTrack)) {
      text(PK_X + PK_W - 26 - 6 * 5, y + 13, "added", 1, sel ? C.text : C.green);
    }
  }
}

Action pickerKey(Key k) {
  int count = pickCount();
  switch (k) {
    case Key::Select: return pickChoose();
    case Key::Up:
    case Key::Left:
      pickOpen = false;
      return Action::Redraw;
    case Key::Down:
      if (pickSel < count - 1) pickSel++;
      return Action::Redraw;
    default:
      return Action::None;
  }
}

Action pickerWheel(int32_t clicks) {
  int count = pickCount();
  pickSel += clicks;
  if (pickSel < 0) pickSel = 0;
  if (pickSel >= count) pickSel = count - 1;
  return Action::Redraw;
}

Action pickerTap(int16_t x, int16_t y) {
  if (x < PK_X || x > PK_X + PK_W || y < PK_Y || y > PK_Y + PK_H) {
    pickOpen = false; // tap outside closes it
    return Action::Redraw;
  }
  if (y < PK_ROW_Y0) return Action::None;
  int r = (y - PK_ROW_Y0) / PK_ROW_H;
  if (r >= PICK_ROWS || pickTop + r >= pickCount()) return Action::None;
  pickSel = pickTop + r;
  return pickChoose();
}

void drawToast() {
  if (!toastShowing || millis() >= toastUntil) return;
  int16_t w = (int16_t)strlen(toastMsg) * CHAR_W + 40;
  int16_t x = (456 - w) / 2;
  Glass::panel(x, 228, w, 34, 17, C.green, 150, 60);
  text(x + 20, 237, toastMsg, 2, C.text);
}

bool toastEnded() {
  if (toastShowing && millis() >= toastUntil) {
    toastShowing = false;
    return true;
  }
  return false;
}

} // namespace MusicBrowser
