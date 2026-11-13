// Gaslight & Griffins - the places: tile maps, buildings, exits, who stands
// where. North is up the screen (away from the camera).
//
// Tiles:  .  cobbles        ,  grass         v  long grass (wild creatures)
//         ~  water          =  planks        :  shingle / sand    _  floor (indoors)
//         ^  rock wall      #  building      L  street lamp       T  tree
//         b  crates         o  bollard       B  bench             N  net rack
//         g  gravestone     w  low wall      M  statue (2x2)      P  boat
//         R  sea rock       E  exit          f  fence             S  stairs down
//   indoors: t table  c chair  K shelf  C counter  F hearth  Y bed  X barrel  G globe/desk
#include "Game.h"
#include "World.h"

namespace Gaslight {

// --- Calder Falls (prologue) -------------------------------------------------------
static const char *const FALLS[] = {
    "~~~~~~~~~~~~~~",
    "^^^^~~~~~^^^^^",
    "^^^,~~~~~,^^^^",
    "^^,,~~~~~,,^^^",
    "^,,,:~~~:,,,^^",
    "^,T,:~~~:,T,,^",
    "^,,,:~~~:,,,,^",
    "^,,,,~~~,,,T,^",
    "^T,,,~~~:,,,,^",
    "^,,,:~~~:,,,,^",
    "^,,,:~~~:,T,,^",
    "^,T,,~~~,,,,,^",
    "^,,,,=====,,,^",
    "^,,,,:~~~:,,,^",
    "^,,,,:~~~:,,T^",
    "^^^^^^~~~^^^^^",
};

// --- Fogmouth -------------------------------------------------------------------------
static const char *const TOWN[] = {
    "~~~~~~~~==~~~~~~~~~~==~~~~~~~~~~^,,,,,,,",  // 0
    "~~~~~~P~==~~~~~~~~~~==~P~~~~~~~~^,,###,,",  // 1
    "~~~~~~~~==~~~~~~~~~~==~~~~~~~~~~^,,###,,",  // 2
    "~~~~~~~~==~~~~~~P~~~==~~~~~~~~~~^,,###,,",  // 3
    "~~~~~~~~==~~~~~~~~~~==~~~~~~~~~~^,,,.,,L",  // 4
    "o===o===========o=========o====o.,,,.,,,",  // 5
    "..b..L....N..b...L...b..N...L....b..,,,T",  // 6
    "..........#######...........####...,,,,,",  // 7
    "..######..#######..######...####..L,T,,T",  // 8
    "..######..#######..######...####...,,,,,",  // 9
    "..######..#######..######...####..,,,,T,",  // 10
    "L.######..#######..######...####..,,,,,,",  // 11
    "................................L...,,,,",  // 12
    "....L.......L......................,,,,,",  // 13
    ",,......B.......L.MM.L.......B....,,,,,,",  // 14
    ",,................MM..............#####,",  // 15
    "T,######..#####.......########....#####,",  // 16
    ",,######..#####.......########....#####T",  // 17
    ",,######..#####...L...########....#####,",  // 18
    "T,######..#####.......########..L.#####,",  // 19
    ",,........L...........########..........",  // 20
    "E...................L.............gg.g.,",  // 21
    "E.......,,,,,,,..............L...g.gg.g,",  // 22
    "E..L....,T,,T,,..,,,,,,,.........gg.g.g,",  // 23
    ",.......,,,,,,,..,T,,,,T.........w..w..,",  // 24
    ",,......,,B,,,,..,,,,,,,..L......,,,,,,T",  // 25
    "T,,.....,,,,,,,..,,,T,,,.........,,T,,,,",  // 26
    ",,,,ww...........,,,,,,,......L..,,,,,,,",  // 27
    "T,,,ww..T,,,T,,,,,,,T,,,,,,T,,,,,,T,,,,,",  // 28
    "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww",  // 29
};
static const Building TOWN_B[] = {
    {35, 1, 3, 3, 7.5f, 0xE8E2D0, 0x7A2430, 36, A_LIGHTHOUSE, 2, "Fogmouth Light"},
    {2, 8, 6, 4, 2.6f, 0x8C8F86, 0x37465A, 4, A_HARBOUR, 1, "Harbour Office"},
    {10, 7, 7, 5, 3.0f, 0x9A7452, 0x4A3328, 13, A_TAVERN, 5, "The Drowned Lantern"},
    {19, 8, 6, 4, 2.8f, 0x6E5E70, 0x2C2F3A, 21, A_CREWE, 1, "Crewe & Co. Shipping"},
    {28, 7, 4, 5, 3.4f, 0xB8A98C, 0x4F6B62, 29, A_LIBRARY, 4, "Fogmouth Library"},
    {2, 16, 6, 4, 2.6f, 0x5E6E82, 0x283548, 4, A_POLICE, 1, "Constabulary"},
    {10, 16, 5, 4, 2.5f, 0xC7B08A, 0x7A4A3A, 12, A_QUILL, 0, "Mrs. Quill's Lodgings"},
    {22, 16, 8, 5, 2.2f, 0x5B4636, 0x3B2E26, 26, A_LODGE, 3, "The Tide Lodge"},
    {34, 15, 5, 5, 3.2f, 0x8A8578, 0x434750, 36, A_COUNT, 4, "Chapel of St. Brine"},
};
static const Exit TOWN_X[] = {
    {0, 21, A_CLIFFS, 29, 12, F_W}, {0, 22, A_CLIFFS, 29, 13, F_W}, {0, 23, A_CLIFFS, 29, 14, F_W},
};

// --- Gull Cliffs (the road west) ----------------------------------------------------------
static const char *const CLIFFS[] = {
    "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",  // 0
    "~~~~~~~~~R~~~~~~~~~~~~~~~~R~~~~",  // 1
    "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",  // 2
    "^^^^^^^^~~~~~^^^^^^^^^^^^^^^^^^",  // 3
    "^,,,,,,^^::::^^,,,,,,,,,,,,,,,^",  // 4
    "^,T,,,,,,,::,,,,vvvv,,,T,,,,,,^",  // 5
    "^,,,vvv,,,,,,,,vvvvvv,,,,,,T,,^",  // 6
    "^,,vvvvv,,T,,,,vvvvv,,,,,,,,,,^",  // 7
    "^T,vvvv,,,,,,,,,,,,,,,,,,,vvv,^",  // 8
    "^,,,vv,,,,..........,,,,vvvvv,^",  // 9
    "^,,,,,,,,,.,,,,,,,,.,,,,,vvv,,^",  // 10
    "^,T,,,,,,,.,,T,,,,,.,,,,,,,,,,^",  // 11
    "^,,,,,,....,,,,,,,,.........,,E",  // 12
    "^,,,,,,.,,,,,,vvvv,,,,,,,,..,,E",  // 13
    "^,,T,,,.,,,,,vvvvvv,,,T,,,,,..E",  // 14
    "^,,,,,,.,,,,,,vvvv,,,,,,,,,,,,^",  // 15
    "^,vvv,,.,,,T,,,,,,,,,vvvv,,,,T^",  // 16
    "^vvvvv,.,,,,,,,,,,,,vvvvvv,,,,^",  // 17
    "^,vvv,,.,,T,,,,T,,,,,vvvv,,,,,^",  // 18
    "^,,,,,,..........,,,,,,,,,,T,,^",  // 19
    "^,T,,,,,,,,,,,,,,,,T,,,,,,,,,,^",  // 20
    "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^",  // 21
};
static const Exit CLIFFS_X[] = {
    {30, 12, A_TOWN, 1, 21, F_E}, {30, 13, A_TOWN, 1, 22, F_E}, {30, 14, A_TOWN, 1, 23, F_E},
};

// --- interiors -----------------------------------------------------------------------------
static const char *const QUILL[] = {
    "^^^^^^^^^^",
    "^KK_F__Y_^",
    "^________^",
    "^_tc___Y_^",
    "^_c______^",
    "^____X___^",
    "^________^",
    "^^^^E^^^^^",
};
static const char *const TAVERN[] = {
    "^^^^^^^^^^^^",
    "^XXCCCCCGKF^",
    "^__________^",
    "^_tc__tc_t_^",
    "^_c___c__c_^",
    "^__________^",
    "^_tc____tc_^",
    "^__________^",
    "^^^^^E^^^^^^",
};
static const char *const HARBOUR[] = {
    "^^^^^^^^^^",
    "^KKK_G_KK^",
    "^________^",
    "^_CCCC___^",
    "^________^",
    "^X_____X_^",
    "^^^^E^^^^^",
};
static const char *const CREWE[] = {
    "^^^^^^^^^^",
    "^KK_G__KK^",
    "^________^",
    "^__tc____^",
    "^________^",
    "^X____X__^",
    "^^^^E^^^^^",
};
static const char *const POLICE[] = {
    "^^^^^^^^^^",
    "^K_^_^_G_^",
    "^__^_^___^",
    "^_CCCC___^",
    "^________^",
    "^X______c^",
    "^^^^E^^^^^",
};
static const char *const LIGHTHOUSE[] = {
    "^^^^^^^^",
    "^_S__X_^",
    "^______^",
    "^_t____^",
    "^______^",
    "^^^E^^^^",
};
static const char *const CAVE[] = {
    "^^^^^^^^^^^^^^",
    "^^^~~~~~~^^^^^",
    "^^~~~~~~~~:^^^",
    "^:~~~~~~~::::^",
    "^::::~~~:::::^",
    "^:::::::::::^^",
    "^^:::::::::::^",
    "^^^:::::::S::^",
    "^^^^^^^^^^^^^^",
};
static const char *const LIBRARY[] = {
    "^^^^^^^^^^",
    "^KKKKKKKK^",
    "^________^",
    "^_K_K_K__^",
    "^________^",
    "^_tc__G__^",
    "^________^",
    "^^^^E^^^^^",
};
static const char *const LODGE[] = {
    "^^^^^^^^^^^^",
    "^X_______KX^",
    "^__________^",
    "^__======__^",
    "^__======__^",
    "^__======__^",
    "^__________^",
    "^X________X^",
    "^^^^^E^^^^^^",
};
static const char *const ROCK[] = {
    "~~~~~~~~~~~~",
    "~~~~^^^^~~~~",
    "~~~^^::^^~~~",
    "~~^::::::^~~",
    "~~^::::::^~~",
    "~~~^::::^~~~",
    "~~~~~==~~~~~",
    "~~~~~==~~~~~",
    "~~~~~~~~~~~~",
};
static const Exit LIGHT_X[] = {{2, 1, A_CAVE, 10, 6, F_W}};
static const Exit CAVE_X[] = {{10, 7, A_LIGHTHOUSE, 2, 2, F_S}};

#define N(a) (uint8_t)(sizeof(a) / sizeof(a[0]))
#define ROWS(a) a, nullptr, 0

static const AreaDef AREAS_FOG[] = {
    {"Calder Falls", 14, N(FALLS), FALLS, nullptr, 0, nullptr, 0, 0, 0x3B4A63, 0xB7C4C6, 0x8DA0A8, 11, 30, 0, 0},
    {"Fogmouth", 40, N(TOWN), TOWN, TOWN_B, N(TOWN_B), TOWN_X, N(TOWN_X), 0, 0x232F45, 0xA9B6B4, 0x627A88, 9, 27, 0, 0},
    {"Gull Cliffs", 31, N(CLIFFS), CLIFFS, nullptr, 0, CLIFFS_X, N(CLIFFS_X), 0, 0x2F3C55, 0xB4BDB4, 0x6F8590, 9, 26, 1, 0},
    {"Mrs. Quill's Lodgings", 10, N(QUILL), QUILL, nullptr, 0, nullptr, 0, 1, 0x1A1410, 0x2A2018, 0x1A1410, 12, 30, 0, 0},
    {"The Drowned Lantern", 12, N(TAVERN), TAVERN, nullptr, 0, nullptr, 0, 1, 0x1A1410, 0x2A2018, 0x1A1410, 12, 30, 0, 0},
    {"Harbour Office", 10, N(HARBOUR), HARBOUR, nullptr, 0, nullptr, 0, 1, 0x101418, 0x1C222A, 0x101418, 12, 30, 0, 0},
    {"Crewe & Co.", 10, N(CREWE), CREWE, nullptr, 0, nullptr, 0, 1, 0x161018, 0x241C26, 0x161018, 12, 30, 0, 0},
    {"Constabulary", 10, N(POLICE), POLICE, nullptr, 0, nullptr, 0, 1, 0x101418, 0x1C222A, 0x101418, 12, 30, 0, 0},
    {"Fogmouth Light", 8, N(LIGHTHOUSE), LIGHTHOUSE, nullptr, 0, LIGHT_X, N(LIGHT_X), 1, 0x14100C, 0x221A12, 0x14100C, 12, 30, 0, 0},
    {"The Keeper's Cave", 14, N(CAVE), CAVE, nullptr, 0, CAVE_X, N(CAVE_X), 2, 0x06080C, 0x0E1218, 0x0B0F14, 7, 20, 0, 0},
    {"Fogmouth Library", 10, N(LIBRARY), LIBRARY, nullptr, 0, nullptr, 0, 1, 0x14100C, 0x221A12, 0x14100C, 12, 30, 0, 0},
    {"The Tide Lodge", 12, N(LODGE), LODGE, nullptr, 0, nullptr, 0, 1, 0x101820, 0x1C2630, 0x101820, 12, 30, 0, 0},
    {"Wailing Rock", 12, N(ROCK), ROCK, nullptr, 0, nullptr, 0, 0, 0x101828, 0x5A6A80, 0x3A4A60, 6, 22, 0, 0},
};

extern const AreaDef AREAS_MORE[];
extern const int N_AREAS_MORE;
AreaDef AREAS[A_COUNT];
void areasInit() {
  int n = 0;
  for (const AreaDef &a : AREAS_FOG) AREAS[n++] = a;
  for (int i = 0; i < N_AREAS_MORE && n < A_COUNT; i++) AREAS[n++] = AREAS_MORE[i];
  wildsInit();
}

// --- who stands where -----------------------------------------------------------------------
// {area, look, x, z, facing, script, showIf, species, trainer}
const NpcDef NPCS[] = {
    // the Falls
    {A_FALLS, L_VANE, 3, 2, F_N, "vane_falls", "!falls_done", 0, 0},
    // Fogmouth streets
    {A_TOWN, L_PIP, 9, 13, F_S, "pip", "", 0, 0},
    {A_TOWN, L_DOBBS, 5, 20, F_S, "dobbs_street", "!case1_open", 0, 0},
    {A_TOWN, L_SAILOR, 18, 6, F_N, "sailor_quay", "", 0, 0},
    {A_TOWN, L_FISHWIFE, 11, 6, F_S, "fishwife", "", 0, 0},
    {A_TOWN, L_YSOLDE, 26, 13, F_W, "ysolde", "", 0, 0},
    {A_TOWN, L_JUNO, 22, 13, F_W, "juno_town", "juno_town_show", 0, 0},
    {A_TOWN, L_GENT, 31, 22, F_W, "gent_chapel", "", 0, 0},
    {A_TOWN, L_URCHIN, 3, 24, F_E, "urchin_gate", "!has_partner", 0, 0},
    {A_TOWN, L_SILAS, 36, 6, F_S, "silas_out", "silas_outside", 0, 0},
    {A_TOWN, L_LADY, 14, 21, F_N, "lady_walk", "", 0, 0},
    {A_TOWN, L_CONDUCTOR, 33, 13, F_W, "travel", "act1_done", 0, 0},
    // Gull Cliffs: trainers and a watcher
    {A_CLIFFS, L_TRAINER_A, 21, 12, F_W, "tr_cliff1", "", 0, 1},
    {A_CLIFFS, L_TRAINER_B, 8, 16, F_E, "tr_cliff2", "", 0, 1},
    {A_CLIFFS, L_TRAINER_C, 12, 9, F_S, "tr_cliff3", "", 0, 1},
    {A_CLIFFS, L_SAILOR2, 11, 4, F_N, "watcher", "", 0, 0},
    // interiors
    {A_QUILL, L_QUILL, 6, 2, F_S, "quill", "", 0, 0},
    {A_TAVERN, L_MARISOL, 9, 2, F_S, "marisol", "!marisol_jailed", 0, 0},
    {A_TAVERN, L_SAILOR2, 3, 4, F_E, "tavern_sailor", "", 0, 0},
    {A_TAVERN, L_GENT, 7, 6, F_W, "tavern_gent", "", 0, 0},
    {A_HARBOUR, L_OTTILIE, 4, 2, F_S, "ottilie", "", 0, 0},
    {A_CREWE, L_CREWE, 4, 2, F_S, "crewe", "!crewe_arrested", 0, 0},
    {A_POLICE, L_DOBBS, 7, 2, F_S, "dobbs", "", 0, 0},
    {A_POLICE, L_MARISOL, 4, 1, F_S, "marisol_cell", "marisol_jailed", 0, 0},
    {A_LIGHTHOUSE, L_SILAS, 4, 3, F_W, "silas", "!silas_outside", 0, 0},
    {A_LIBRARY, L_LUNE, 6, 4, F_S, "lune", "", 0, 0},
    {A_LODGE, L_HALLORAN, 6, 2, F_S, "halloran", "", 0, 0},
    {A_CAVE, L_COUNT, 5, 4, F_S, "cyclops", "", 0, 0},
    {A_ROCK, L_COUNT, 6, 3, F_S, "sirens", "", 0, 0},
};
const int N_NPCS = sizeof(NPCS) / sizeof(NPCS[0]);

// Things to look at: {area, x, z, script, showIf, glow}
const SpotDef SPOTS[] = {
    {A_TOWN, 18, 15, "statue", "", 0},
    {A_TOWN, 19, 15, "statue", "", 0},
    {A_TOWN, 9, 0, "pier_east", "", 0},
    {A_TOWN, 21, 4, "jetty", "", 0},
    {A_TOWN, 31, 11, "noticeboard", "", 0},
    {A_TOWN, 36, 23, "grave", "", 0},
    {A_TOWN, 6, 6, "frag_fog1", "!frag_fog1", 1},
    {A_CLIFFS, 3, 5, "frag_fog2", "!frag_fog2", 1},
    {A_CLIFFS, 26, 4, "aeolus", "", 0},
    {A_CLIFFS, 12, 4, "view_rock", "", 0},
    {A_CAVE, 3, 4, "frag_fog3", "!frag_fog3", 1},
    {A_LIBRARY, 6, 5, "lune_desk", "", 0},
    {A_CREWE, 4, 1, "crewe_desk", "", 0},
    {A_CREWE, 1, 5, "crewe_barrel", "", 0},
    {A_HARBOUR, 5, 1, "ledger", "", 0},
    {A_LIGHTHOUSE, 2, 3, "lamp_log", "", 0},
    {A_TAVERN, 8, 1, "gramophone", "", 0},
    {A_ROCK, 5, 2, "frag_fog4", "!frag_fog4", 1},
    {A_QUILL, 2, 3, "quill_table", "", 0},
    {A_FALLS, 4, 2, "falls_edge", "", 0},
};
const int N_SPOTS = sizeof(SPOTS) / sizeof(SPOTS[0]);

} // namespace Gaslight
