#include "Playlists.h"
#include "Library.h"
#include "TagReader.h"
#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include <strings.h>

namespace {

const char *DIR_PATH = "/Playlists";

struct List {
  char name[40];  // shown on screen (ASCII)
  char file[96];  // its .m3u on the card
  std::vector<uint16_t> ids;
};

std::vector<List> lists;

// "Playlist 2" before "Playlist 10".
bool naturalLess(const List &a, const List &b) {
  const char *x = a.name, *y = b.name;
  while (*x && *y) {
    if (isdigit((uint8_t)*x) && isdigit((uint8_t)*y)) {
      long nx = strtol(x, (char **)&x, 10), ny = strtol(y, (char **)&y, 10);
      if (nx != ny) return nx < ny;
      continue;
    }
    int cx = tolower((uint8_t)*x), cy = tolower((uint8_t)*y);
    if (cx != cy) return cx < cy;
    x++;
    y++;
  }
  return *y != 0;
}

void readList(List &l) {
  File f = SD.open(l.file, FILE_READ);
  if (!f) return;
  char line[300];
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
    if (!n || line[0] == '#') continue;
    for (char *c = line; *c; c++) if (*c == '\\') *c = '/';
    char path[300];
    const char *p = line;
    while (!strncmp(p, "../", 3)) p += 2; // relative to /Playlists -> card root
    if (*p == '/') snprintf(path, sizeof(path), "%s", p);
    else snprintf(path, sizeof(path), "/%s", p);
    uint16_t id = Library::findByPath(path);
    if (id != Library::NONE) l.ids.push_back(id);
  }
  f.close();
}

bool save(const List &l) {
  SD.mkdir(DIR_PATH);
  File f = SD.open(l.file, FILE_WRITE);
  if (!f) return false;
  f.println("#EXTM3U");
  for (uint16_t id : l.ids) f.println(Library::track(id).path);
  f.close();
  return true;
}

} // namespace

namespace Playlists {

void load() {
  lists.clear();
  File dir = SD.open(DIR_PATH);
  if (!dir || !dir.isDirectory()) return;
  for (File e = dir.openNextFile(); e && lists.size() < MAX_LISTS; e = dir.openNextFile()) {
    const char *name = e.name();
    const char *slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    const char *dot = strrchr(name, '.');
    bool m3u = !e.isDirectory() && name[0] != '.' && dot &&
               (!strcasecmp(dot, ".m3u") || !strcasecmp(dot, ".m3u8"));
    if (m3u) {
      List l;
      snprintf(l.file, sizeof(l.file), "%s/%s", DIR_PATH, name);
      char raw[96];
      size_t n = dot - name;
      if (n >= sizeof(raw)) n = sizeof(raw) - 1;
      memcpy(raw, name, n);
      raw[n] = '\0';
      TagReader::foldUtf8(raw, l.name, sizeof(l.name));
      lists.push_back(l);
    }
    e.close();
  }
  dir.close();
  for (List &l : lists) readList(l);
  std::sort(lists.begin(), lists.end(), naturalLess);
  Serial.printf("Playlists: %u\n", (unsigned)lists.size());
}

uint8_t count() { return (uint8_t)lists.size(); }
const char *name(uint8_t i) { return i < lists.size() ? lists[i].name : ""; }
uint16_t size(uint8_t i) { return i < lists.size() ? (uint16_t)lists[i].ids.size() : 0; }

uint16_t songs(uint8_t i, uint16_t *out, uint16_t max) {
  if (i >= lists.size()) return 0;
  uint16_t n = 0;
  for (uint16_t id : lists[i].ids) {
    if (n >= max) break;
    out[n++] = id;
  }
  return n;
}

bool contains(uint8_t i, uint16_t id) {
  if (i >= lists.size()) return false;
  const auto &v = lists[i].ids;
  return std::find(v.begin(), v.end(), id) != v.end();
}

int create() {
  if (lists.size() >= MAX_LISTS) return -1;
  for (int n = 1; n < 1000; n++) {
    char nm[40];
    snprintf(nm, sizeof(nm), "Playlist %d", n);
    bool taken = false;
    for (const List &l : lists) {
      if (!strcasecmp(l.name, nm)) { taken = true; break; }
    }
    if (taken) continue;
    List l;
    strlcpy(l.name, nm, sizeof(l.name));
    snprintf(l.file, sizeof(l.file), "%s/%s.m3u", DIR_PATH, nm);
    if (!save(l)) return -1;
    lists.push_back(l);
    std::sort(lists.begin(), lists.end(), naturalLess);
    for (size_t i = 0; i < lists.size(); i++) {
      if (!strcmp(lists[i].name, nm)) return (int)i;
    }
  }
  return -1;
}

bool add(uint8_t i, uint16_t id) {
  if (i >= lists.size() || contains(i, id)) return false;
  lists[i].ids.push_back(id);
  save(lists[i]);
  return true;
}

bool remove(uint8_t i, uint16_t id) {
  if (i >= lists.size()) return false;
  auto &v = lists[i].ids;
  auto it = std::find(v.begin(), v.end(), id);
  if (it == v.end()) return false;
  v.erase(it);
  save(lists[i]);
  return true;
}

} // namespace Playlists
