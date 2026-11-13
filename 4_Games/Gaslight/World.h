#pragma once
#include "Game.h"

namespace Gaslight {

struct NpcDef {
  uint8_t area, look;
  int8_t x, z;
  uint8_t face;
  const char *script, *showIf;
  uint8_t species, trainer;
};
extern const NpcDef NPCS[];
extern const NpcDef NPCS_MORE[];
extern const int N_NPCS_MORE;
extern const int N_NPCS;

struct SpotDef {
  uint8_t area;
  int8_t x, z;
  const char *script, *showIf;
  uint8_t glow; // 1 = a glowing story fragment lies here
};
extern const SpotDef SPOTS[];
extern const SpotDef SPOTS_MORE[];
extern const int N_SPOTS_MORE;
extern const int N_SPOTS;
// the Wilds' signposts, den and caches (glow 3, 4, 2), rewritten on each visit
static const int MAX_WILD_SPOTS = 12;
extern SpotDef g_wildSpots[MAX_WILD_SPOTS];
extern int g_nWildSpots;
inline int spotCount() { return N_SPOTS + N_SPOTS_MORE + g_nWildSpots; }
inline const SpotDef &spotAt(int i) {
  return i < N_SPOTS ? SPOTS[i] : i < N_SPOTS + N_SPOTS_MORE ? SPOTS_MORE[i - N_SPOTS] : g_wildSpots[i - N_SPOTS - N_SPOTS_MORE];
}
inline int npcCount() { return N_NPCS + N_NPCS_MORE; }
inline const NpcDef &npcAt(int i) { return i < N_NPCS ? NPCS[i] : NPCS_MORE[i - N_NPCS]; }

bool showIfOk(const char *cond);
char tileAt(int area, int x, int z);
bool walkable(int x, int z);

} // namespace Gaslight
