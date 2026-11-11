#include "Library.h"
#include "TagReader.h"
#include <Arduino.h>
#include <SD.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <vector>

namespace Library {
const char *const UNKNOWN_ARTIST = "Unknown artist";
const char *const UNKNOWN_ALBUM = "Unknown album";
const char *const UNTITLED = "Untitled";
} // namespace Library

namespace {

using Library::UNKNOWN_ARTIST;
using Library::UNKNOWN_ALBUM;

const char *CACHE_DIR = "/.jiggly";
const char *CACHE_PATH = "/.jiggly/library.txt";
const char *EDITS_PATH = "/.jiggly/edits.txt";
const char *CACHE_MAGIC = "JLIB1";
const int MAX_DEPTH = 4;

// All strings live in one PSRAM pool, reset on every scan.
const size_t POOL_SIZE = 1024 * 1024;
char *pool = nullptr;
size_t poolUsed = 0;

Track *tracks = nullptr; // PSRAM, MAX_TRACKS
uint16_t trackCount = 0;
uint8_t seen[Library::MAX_TRACKS];

// path -> id (exact match), and case-insensitive string interning so that
// equal artist/album names share one pointer and group by pointer compare.
const uint16_t PATH_SLOTS = 4096;
uint16_t *pathTable = nullptr; // id + 1, 0 = empty
const uint16_t INTERN_SLOTS = 8192;
const char **internTable = nullptr;

const char **artists = nullptr; // unique album artists, sorted
uint16_t *artistCounts = nullptr;
uint16_t nArtists = 0;

struct AlbumKey { const char *album; const char *artist; uint16_t count; };
AlbumKey *albums = nullptr;
uint16_t nAlbums = 0;
// Who each song's album is filed under in the Albums list: its album
// artist, or VARIOUS for "Singles" and for compilations (see buildViews).
const char **albumOwner = nullptr;
const char *const VARIOUS = "Various Artists";

bool isReady = false;
bool isDirty = true;

std::vector<uint16_t> editedIds; // waiting for commitEdits()

uint32_t hashExact(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

uint32_t hashNoCase(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)tolower((uint8_t)*s)) * 16777619u;
  return h;
}

const char *poolCopy(const char *s) {
  size_t n = strlen(s) + 1;
  if (poolUsed + n > POOL_SIZE) return nullptr;
  char *p = pool + poolUsed;
  memcpy(p, s, n);
  poolUsed += n;
  return p;
}

const char *intern(const char *s) {
  uint32_t i = hashNoCase(s) & (INTERN_SLOTS - 1);
  for (uint32_t probe = 0; probe < INTERN_SLOTS; probe++) {
    const char *e = internTable[i];
    if (!e) {
      const char *c = poolCopy(s);
      if (c) internTable[i] = c;
      return c;
    }
    if (!strcasecmp(e, s)) return e;
    i = (i + 1) & (INTERN_SLOTS - 1);
  }
  return poolCopy(s); // table full: still works, just doesn't group
}

void pathInsert(uint16_t id) {
  uint32_t i = hashExact(tracks[id].path) & (PATH_SLOTS - 1);
  while (pathTable[i]) i = (i + 1) & (PATH_SLOTS - 1);
  pathTable[i] = id + 1;
}

uint16_t pathFind(const char *path) {
  uint32_t i = hashExact(path) & (PATH_SLOTS - 1);
  for (uint32_t probe = 0; probe < PATH_SLOTS && pathTable[i]; probe++) {
    uint16_t id = pathTable[i] - 1;
    if (!strcmp(tracks[id].path, path)) return id;
    i = (i + 1) & (PATH_SLOTS - 1);
  }
  return Library::NONE;
}

void rebuildPathTable() {
  memset(pathTable, 0, PATH_SLOTS * sizeof(uint16_t));
  for (uint16_t i = 0; i < trackCount; i++) pathInsert(i);
}

bool isPlayable(const char *name) {
  if (name[0] == '.') return false; // hidden files and macOS "._" leftovers
  const char *dot = strrchr(name, '.');
  if (!dot) return false;
  static const char *exts[] = {".mp3", ".wav", ".m4a", ".aac", ".flac"};
  for (const char *e : exts) {
    if (!strcasecmp(dot, e)) return true;
  }
  return false;
}

bool skipDir(const char *name) {
  return name[0] == '.' || !strcasecmp(name, "System Volume Information") ||
         !strcasecmp(name, "LOST.DIR") || !strcasecmp(name, "Playlists");
}

// Fills a track's text from its tags, falling back on the file and folder
// names: "/Artist/Album/01 - Title.mp3" or "Artist - Title.mp3".
bool fill(Track &t, const char *path, const Tags &tags) {
  char title[96], artist[64], albumArtist[64], album[80];
  strlcpy(title, tags.title, sizeof(title));
  strlcpy(artist, tags.artist, sizeof(artist));
  strlcpy(albumArtist, tags.albumArtist, sizeof(albumArtist));
  strlcpy(album, tags.album, sizeof(album));

  // Folder names, innermost first.
  char folders[2][64] = {{0}, {0}};
  const char *slash = strrchr(path, '/');
  const char *end = slash;
  for (int k = 0; k < 2 && end && end > path; k++) {
    const char *start = end - 1;
    while (start > path && *start != '/') start--;
    if (*start == '/') start++;
    char raw[128];
    size_t n = end - start;
    if (n >= sizeof(raw)) n = sizeof(raw) - 1;
    memcpy(raw, start, n);
    raw[n] = '\0';
    TagReader::foldUtf8(raw, folders[k], sizeof(folders[k]));
    end = start - 1;
  }

  if (!title[0]) {
    char name[128];
    TagReader::foldUtf8(slash ? slash + 1 : path, name, sizeof(name));
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    char *p = name;
    // "01 - Title", "01. Title", "01 Title"
    if (isdigit((uint8_t)p[0])) {
      char *q = p;
      while (isdigit((uint8_t)*q)) q++;
      if (q - p <= 3 && (*q == ' ' || *q == '.' || *q == '-' || *q == '_')) {
        while (*q == ' ' || *q == '.' || *q == '-' || *q == '_') q++;
        if (*q) p = q;
      }
    }
    char *dash = strstr(p, " - ");
    if (dash && !artist[0]) {
      *dash = '\0';
      strlcpy(artist, p, sizeof(artist));
      p = dash + 3;
    }
    strlcpy(title, p, sizeof(title));
  }
  if (!album[0] && folders[0][0]) strlcpy(album, folders[0], sizeof(album));
  if (!artist[0] && folders[1][0]) strlcpy(artist, folders[1], sizeof(artist));
  if (!albumArtist[0]) strlcpy(albumArtist, artist, sizeof(albumArtist));

  t.path = poolCopy(path);
  t.title = poolCopy(title[0] ? title : Library::UNTITLED);
  t.artist = intern(artist[0] ? artist : UNKNOWN_ARTIST);
  t.albumArtist = intern(albumArtist[0] ? albumArtist : UNKNOWN_ARTIST);
  t.album = intern(album[0] ? album : UNKNOWN_ALBUM);
  t.trackNo = tags.track;
  return t.path && t.title && t.artist && t.albumArtist && t.album;
}

// --- cache: one line per song, tab separated ---------------------------------
// size, track, path, title, artist, albumArtist, album

void loadCache() {
  if (!SD.exists(CACHE_PATH)) return;
  File f = SD.open(CACHE_PATH, FILE_READ);
  if (!f) return;
  // Read it whole: line-by-line Stream reads go a byte at a time and took
  // seconds for a thousand songs.
  size_t total = f.size();
  char *buf = (char *)ps_malloc(total + 1);
  if (!buf) { f.close(); return; }
  total = f.read((uint8_t *)buf, total);
  f.close();
  buf[total] = '\0';

  bool first = true;
  char *next = buf;
  while (next && *next && trackCount < Library::MAX_TRACKS) {
    char *line = next;
    char *nl = strchr(line, '\n');
    if (nl) { *nl = '\0'; next = nl + 1; } else next = nullptr;
    size_t n = strlen(line);
    if (n && line[n - 1] == '\r') line[--n] = '\0';
    if (first) {
      first = false;
      if (strcmp(line, CACHE_MAGIC) != 0) break; // old or foreign format
      continue;
    }
    char *field[7];
    int k = 0;
    char *p = line;
    while (k < 7) {
      field[k++] = p;
      char *tab = strchr(p, '\t');
      if (!tab) break;
      *tab = '\0';
      p = tab + 1;
    }
    if (k < 7) continue;
    Track &t = tracks[trackCount];
    t.size = strtoul(field[0], nullptr, 10);
    t.trackNo = (uint16_t)atoi(field[1]);
    t.path = poolCopy(field[2]);
    t.title = poolCopy(field[3]);
    t.artist = intern(field[4]);
    t.albumArtist = intern(field[5]);
    t.album = intern(field[6]);
    if (!t.path || !t.title || !t.artist || !t.albumArtist || !t.album) break;
    seen[trackCount] = 0;
    pathInsert(trackCount);
    trackCount++;
  }
  free(buf);
}

void saveCache() {
  SD.mkdir(CACHE_DIR);
  File f = SD.open(CACHE_PATH, FILE_WRITE);
  if (!f) return;
  f.printf("%s\n", CACHE_MAGIC);
  for (uint16_t i = 0; i < trackCount; i++) {
    const Track &t = tracks[i];
    f.printf("%lu\t%u\t%s\t%s\t%s\t%s\t%s\n", (unsigned long)t.size, t.trackNo, t.path, t.title,
             t.artist, t.albumArtist, t.album);
  }
  f.close();
}

// --- hand edits: one line per edit, path \t title \t artist \t album ----------
// Later lines win, so an edit is just appended.

void applyText(Track &t, const char *title, const char *artist, const char *album) {
  if (title && *title) {
    const char *p = poolCopy(title);
    if (p) t.title = p;
  }
  if (artist && *artist) {
    const char *p = intern(artist);
    if (p) t.artist = t.albumArtist = p; // the artist is also who it's filed under
  }
  if (album && *album) {
    const char *p = intern(album);
    if (p) t.album = p;
  }
}

void applyEdits() {
  if (!SD.exists(EDITS_PATH)) return;
  File f = SD.open(EDITS_PATH, FILE_READ);
  if (!f) return;
  size_t total = f.size();
  char *buf = (char *)ps_malloc(total + 1);
  if (!buf) { f.close(); return; }
  total = f.read((uint8_t *)buf, total);
  f.close();
  buf[total] = '\0';

  uint16_t applied = 0;
  for (char *line = buf; line && *line;) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    size_t n = strlen(line);
    if (n && line[n - 1] == '\r') line[n - 1] = '\0';
    char *field[4] = {line, nullptr, nullptr, nullptr};
    for (int k = 1; k < 4 && field[k - 1]; k++) {
      char *tab = strchr(field[k - 1], '\t');
      if (tab) { *tab = '\0'; field[k] = tab + 1; }
    }
    if (field[3]) {
      uint16_t id = pathFind(field[0]);
      if (id != Library::NONE) {
        applyText(tracks[id], field[1], field[2], field[3]);
        applied++;
      }
    }
    line = nl ? nl + 1 : nullptr;
  }
  free(buf);
  if (applied) Serial.printf("Library: %u hand edits applied\n", applied);
}

// --- walking the card ---------------------------------------------------------

struct ScanState {
  Library::ProgressFn progress;
  uint32_t found;
  uint32_t tagged;
  bool changed;
  uint32_t lastReport;
  uint32_t lastLog;
};

// Songs already in the cache are trusted by path alone (checking sizes
// would mean opening every file - minutes on a big card). New paths have
// their tags read.
void addFile(ScanState &st, const char *path) {
  st.found++;
  uint16_t id = pathFind(path);
  if (id != Library::NONE) {
    seen[id] = 1;
  } else {
    if (trackCount >= Library::MAX_TRACKS) return;
    id = trackCount;
    Tags tags;
    uint32_t size = 0;
    File f = SD.open(path, FILE_READ);
    if (f) {
      size = f.size();
      TagReader::read(f, tags);
      f.close();
    } else {
      memset(&tags, 0, sizeof(tags));
    }
    Track t;
    t.size = size;
    if (!fill(t, path, tags)) return; // pool full
    tracks[id] = t;
    seen[id] = 1;
    trackCount++;
    pathInsert(id);
    st.tagged++;
    st.changed = true;
    // Save as we go, so a first scan cut short (switched off, unplugged)
    // carries on where it stopped next time instead of starting over.
    if (st.tagged % 100 == 0) saveCache();
  }
  if (st.progress && millis() - st.lastReport > 150) {
    st.lastReport = millis();
    st.progress(st.found, st.tagged);
  }
  if (millis() - st.lastLog > 3000) {
    st.lastLog = millis();
    Serial.printf("Library: %lu songs found, %lu tags read so far\n", (unsigned long)st.found,
                  (unsigned long)st.tagged);
  }
}

// Lists folders with POSIX readdir() on the SD mount ("/sd") rather than
// File::openNextFile(), which opens every entry (and stats it) just to name
// it: ~110 ms a song, over two minutes for this card, versus milliseconds.
void walk(ScanState &st, const char *dirPath, int depth) {
  char vfs[300];
  snprintf(vfs, sizeof(vfs), "/sd%s", dirPath);
  DIR *d = opendir(vfs);
  if (!d) {
    Serial.printf("Library: can't open folder %s (errno %d)\n", vfs, errno);
    return;
  }
  char path[256];
  struct dirent *e;
  while ((e = readdir(d)) != nullptr) {
    const char *name = e->d_name;
    if (strcmp(dirPath, "/") == 0) snprintf(path, sizeof(path), "/%s", name);
    else snprintf(path, sizeof(path), "%s/%s", dirPath, name);

    if (e->d_type == DT_DIR) {
      if (depth < MAX_DEPTH && !skipDir(name)) walk(st, path, depth + 1);
    } else if (isPlayable(name)) {
      addFile(st, path);
    }
  }
  closedir(d);
}

// --- sorted views ---------------------------------------------------------------

int cmpStr(const void *a, const void *b) {
  return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

int cmpAlbumKey(const void *a, const void *b) {
  const AlbumKey *x = (const AlbumKey *)a, *y = (const AlbumKey *)b;
  int c = strcasecmp(x->album, y->album);
  return c ? c : strcasecmp(x->artist, y->artist);
}

// The Singles album runs across every artist: A-Z by artist, then title.
int cmpTrackInSingles(const void *a, const void *b) {
  const Track &x = tracks[*(const uint16_t *)a], &y = tracks[*(const uint16_t *)b];
  int c = strcasecmp(x.albumArtist, y.albumArtist);
  return c ? c : strcasecmp(x.title, y.title);
}

int cmpTrackInAlbum(const void *a, const void *b) {
  const Track &x = tracks[*(const uint16_t *)a], &y = tracks[*(const uint16_t *)b];
  if (x.album != y.album) return strcasecmp(x.album, y.album);
  uint16_t tx = x.trackNo ? x.trackNo : 0xFFFF, ty = y.trackNo ? y.trackNo : 0xFFFF;
  if (tx != ty) return tx < ty ? -1 : 1;
  return strcasecmp(x.title, y.title);
}

void buildViews() {
  // Artists
  const char **tmp = (const char **)ps_malloc(sizeof(char *) * (trackCount + 1));
  nArtists = 0;
  if (tmp) {
    for (uint16_t i = 0; i < trackCount; i++) tmp[i] = tracks[i].albumArtist;
    qsort(tmp, trackCount, sizeof(char *), cmpStr);
    for (uint16_t i = 0; i < trackCount; i++) {
      if (nArtists && artists[nArtists - 1] == tmp[i]) {
        artistCounts[nArtists - 1]++;
      } else {
        artists[nArtists] = tmp[i];
        artistCounts[nArtists] = 1;
        nArtists++;
      }
    }
    free(tmp);
  }

  // Albums: (album, album artist) pairs - then a second pass that files
  // "Singles" and compilations under one owner, so each is a single album
  // rather than one per artist. A compilation is an album name shared by
  // three or more artists with at most two songs each (a soundtrack, a
  // various-artists collection); a common name like "Greatest Hits" held
  // by bands with whole albums of it is left alone.
  AlbumKey *keys = (AlbumKey *)ps_malloc(sizeof(AlbumKey) * (trackCount + 1));
  nAlbums = 0;
  if (keys) {
    auto collapse = [&]() {
      qsort(keys, trackCount, sizeof(AlbumKey), cmpAlbumKey);
      nAlbums = 0;
      for (uint16_t i = 0; i < trackCount; i++) {
        if (nAlbums && albums[nAlbums - 1].album == keys[i].album && albums[nAlbums - 1].artist == keys[i].artist) {
          albums[nAlbums - 1].count++;
        } else {
          albums[nAlbums++] = keys[i];
        }
      }
    };
    for (uint16_t i = 0; i < trackCount; i++) keys[i] = {tracks[i].album, tracks[i].albumArtist, 1};
    collapse();
    for (uint16_t i = 0; i < trackCount; i++) albumOwner[i] = tracks[i].albumArtist;
    for (uint16_t a = 0; a < nAlbums;) {
      uint16_t b = a, most = 0;
      while (b < nAlbums && albums[b].album == albums[a].album) { most = max(most, albums[b].count); b++; }
      const char *name = albums[a].album;
      bool singles = !strcasecmp(name, "Singles");
      bool compilation = b - a >= 3 && most <= 2 && name != UNKNOWN_ALBUM;
      if (singles || compilation)
        for (uint16_t i = 0; i < trackCount; i++)
          if (tracks[i].album == name) albumOwner[i] = VARIOUS;
      a = b;
    }
    for (uint16_t i = 0; i < trackCount; i++) keys[i] = {tracks[i].album, albumOwner[i], 1};
    collapse();
    free(keys);
  }
}

bool allocate() {
  if (pool) return true;
  pool = (char *)ps_malloc(POOL_SIZE);
  tracks = (Track *)ps_malloc(sizeof(Track) * Library::MAX_TRACKS);
  pathTable = (uint16_t *)ps_malloc(sizeof(uint16_t) * PATH_SLOTS);
  internTable = (const char **)ps_malloc(sizeof(char *) * INTERN_SLOTS);
  artists = (const char **)ps_malloc(sizeof(char *) * Library::MAX_TRACKS);
  artistCounts = (uint16_t *)ps_malloc(sizeof(uint16_t) * Library::MAX_TRACKS);
  albums = (AlbumKey *)ps_malloc(sizeof(AlbumKey) * Library::MAX_TRACKS);
  albumOwner = (const char **)ps_malloc(sizeof(char *) * Library::MAX_TRACKS);
  if (!pool || !tracks || !pathTable || !internTable || !artists || !artistCounts || !albums || !albumOwner) {
    Serial.println("Library: out of PSRAM");
    pool = nullptr;
    return false;
  }
  return true;
}

} // namespace

namespace Library {

bool scan(ProgressFn progress) {
  if (!allocate()) return false;
  uint32_t t0 = millis();
  poolUsed = 0;
  trackCount = 0;
  memset(pathTable, 0, PATH_SLOTS * sizeof(uint16_t));
  memset(internTable, 0, INTERN_SLOTS * sizeof(char *));

  loadCache();
  uint16_t cached = trackCount;

  ScanState st = {progress, 0, 0, false, 0, millis()};
  walk(st, "/", 0);

  // A card that suddenly lists nothing is a read failure, not an empty
  // card: don't let it wipe the cache (that would mean minutes of
  // re-reading tags next time).
  if (st.found == 0 && cached > 0) {
    Serial.printf("Library: card listed no songs (cache has %u) - keeping the cache\n", cached);
    for (uint16_t i = 0; i < trackCount; i++) seen[i] = 1;
  }

  // Drop songs that are no longer on the card.
  uint16_t kept = 0;
  for (uint16_t i = 0; i < trackCount; i++) {
    if (seen[i]) tracks[kept++] = tracks[i];
  }
  if (kept != trackCount) st.changed = true;
  trackCount = kept;
  rebuildPathTable();

  if (st.changed) saveCache();
  applyEdits(); // after saving, so the cache always holds what the tags say
  buildViews();
  isReady = true;
  isDirty = false;
  Serial.printf("Library: %u songs (%u cached, %lu tags read), %u artists, %u albums, %lu KB text, %lu ms\n",
                trackCount, cached, (unsigned long)st.tagged, nArtists, nAlbums,
                (unsigned long)(poolUsed / 1024), (unsigned long)(millis() - t0));
  return true;
}

bool ready() { return isReady; }
void markDirty() { isDirty = true; }
bool dirty() { return isDirty; }

uint16_t count() { return trackCount; }

const Track &track(uint16_t id) {
  static const Track empty = {"", "", "", "", "", 0, 0};
  return id < trackCount ? tracks[id] : empty;
}

uint16_t findByPath(const char *path) { return isReady ? pathFind(path) : NONE; }

bool setText(uint16_t id, const char *title, const char *artist, const char *album) {
  if (!isReady || id >= trackCount) return false;
  char t[96], ar[64], al[80];
  t[0] = ar[0] = al[0] = '\0';
  if (title) TagReader::foldUtf8(title, t, sizeof(t));
  if (artist) TagReader::foldUtf8(artist, ar, sizeof(ar));
  if (album) TagReader::foldUtf8(album, al, sizeof(al));
  for (char *s : {t, ar, al}) {
    for (char *c = s; *c; c++) if (*c == '\t') *c = ' ';
  }
  applyText(tracks[id], t, ar, al);
  editedIds.push_back(id);
  return true;
}

void commitEdits() {
  if (editedIds.empty()) return;
  SD.mkdir(CACHE_DIR);
  File f = SD.open(EDITS_PATH, FILE_APPEND);
  if (f) {
    for (uint16_t id : editedIds) {
      const Track &t = tracks[id];
      f.printf("%s\t%s\t%s\t%s\n", t.path, t.title, t.artist, t.album);
    }
    f.close();
  }
  Serial.printf("Library: %u songs edited\n", (unsigned)editedIds.size());
  editedIds.clear();
  buildViews();
}

uint16_t artistCount() { return nArtists; }
const char *artistName(uint16_t i) { return i < nArtists ? artists[i] : ""; }
uint16_t artistSongs(uint16_t i) { return i < nArtists ? artistCounts[i] : 0; }

uint16_t songsByArtist(uint16_t a, uint16_t *out, uint16_t max) {
  if (a >= nArtists) return 0;
  const char *name = artists[a];
  uint16_t n = 0;
  for (uint16_t i = 0; i < trackCount && n < max; i++) {
    if (tracks[i].albumArtist == name) out[n++] = i;
  }
  qsort(out, n, sizeof(uint16_t), cmpTrackInAlbum);
  return n;
}

uint16_t albumCount() { return nAlbums; }
const char *albumName(uint16_t i) { return i < nAlbums ? albums[i].album : ""; }
const char *albumArtistName(uint16_t i) { return i < nAlbums ? albums[i].artist : ""; }
uint16_t albumSongs(uint16_t i) { return i < nAlbums ? albums[i].count : 0; }

uint16_t songsOnAlbum(uint16_t a, uint16_t *out, uint16_t max) {
  if (a >= nAlbums) return 0;
  uint16_t n = 0;
  for (uint16_t i = 0; i < trackCount && n < max; i++) {
    if (tracks[i].album == albums[a].album && albumOwner[i] == albums[a].artist) out[n++] = i;
  }
  bool singles = !strcasecmp(albums[a].album, "Singles");
  qsort(out, n, sizeof(uint16_t), singles ? cmpTrackInSingles : cmpTrackInAlbum);
  return n;
}

} // namespace Library
