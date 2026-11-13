#pragma once
// Gaslight & Griffins - game state shared between the world, the story
// script, battles and the menus.

#include "GL.h"

namespace Gaslight {

// --- input, as the game sees it ---------------------------------------------
enum Btn : uint8_t { B_NONE, B_UP, B_DOWN, B_LEFT, B_RIGHT, B_OK };
extern uint8_t g_held;           // bit per Btn
inline bool held(Btn b) { return g_held & (1 << b); }
extern float g_time;             // seconds since the game started (animation clock)

// --- appearance: one description drives the 3D figure and the portrait ------
enum Hat : uint8_t { HAT_NONE, HAT_TOP, HAT_CAP, HAT_HELMET, HAT_BONNET, HAT_CAPTAIN, HAT_HOOD, HAT_BUN, HAT_CROWN };
enum Hair : uint8_t { HAIR_SHORT, HAIR_LONG, HAIR_BOB, HAIR_BALD, HAIR_CURLS, HAIR_SWEPT, HAIR_BRAID, HAIR_MESSY, HAIR_BUN };
struct Look {
  uint32_t coat, legs, skin, hair, accent;
  uint8_t hat, hairStyle;
  uint8_t beard;      // 0 none, 1 moustache, 2 full
  uint8_t eyepatch;   // 0/1
  uint8_t glasses;    // 0/1
  uint8_t dress;      // 0 trousers, 1 long skirt
  float height;       // 1 = adult
  float build;        // 1 = average
  uint8_t age;        // 0 child, 1 young, 2 adult, 3 old
  uint8_t mood;       // portrait expression: 0 calm 1 smile 2 stern 3 sad 4 sly 5 worried
};
enum LookId : uint8_t {
  L_WREN, L_VANE, L_VANEMASK, L_JUNO, L_MARISOL, L_SILAS, L_OTTILIE, L_CREWE, L_PIP, L_YSOLDE, L_HALLORAN,
  L_QUILL, L_DOBBS, L_LUNE, L_SAILOR, L_SAILOR2, L_FISHWIFE, L_URCHIN, L_GENT, L_LADY, L_CURATOR, L_BRAM,
  L_TRAINER_A, L_TRAINER_B, L_TRAINER_C,
  L_TALLY, L_CONDUCTOR, L_NEFERET, L_TOMAS, L_MERCHANT, L_ASSISTANT, L_KAMAL, L_SEFA, L_IMANI, L_INNKEEP_S, L_INNKEEP_G, L_INNKEEP_K, L_INNKEEP_U, L_MOTH, L_BERTRAM, L_GREENKNIGHT, L_GARETH, L_ROSALIND, L_MORGANA, L_IVY, L_SIGGA, L_ASTA, L_VILLAGER, L_EIRIK, L_HILDE, L_RAVEN, L_SIDURI,
  L_COUNT
};
extern const Look LOOKS[L_COUNT];
extern const char *const LOOK_NAMES[L_COUNT];

// --- areas --------------------------------------------------------------------
enum AreaId : uint8_t {
  A_FALLS, A_TOWN, A_CLIFFS, A_QUILL, A_TAVERN, A_HARBOUR, A_CREWE, A_POLICE, A_LIGHTHOUSE, A_CAVE, A_LIBRARY,
  A_LODGE, A_ROCK,
  A_SAND, A_MUSEUM, A_KAMAL, A_SEFA, A_GLASS, A_INN, A_GREEN, A_HALL, A_HUT, A_IVY, A_SKJOLD, A_SKALD, A_RUNEHUT, A_FORGE, A_SEACAVE, A_UR, A_ZIGGURAT, A_SANCTUM, A_SIDURI,
  A_WILDS, // made fresh by the land model on each visit
  A_COUNT
};
struct Building {
  int8_t x, z, w, d;      // footprint in tiles
  float h;                // wall height
  uint32_t wall, roof;
  int8_t doorX;           // door column (absolute), on the south face; -1 none
  uint8_t dest;           // area the door leads to
  uint8_t style;          // 0 house, 1 shop (big windows), 2 lighthouse, 3 ship, 4 chapel, 5 tavern
  const char *sign;       // shown on approach (may be null)
};
struct Exit { int8_t x, z; uint8_t dest; int8_t tx, tz; uint8_t face; };
struct AreaDef {
  const char *name;
  uint8_t w, d;
  const char *const *rows;      // d strings of w chars
  const Building *b; uint8_t nb;
  const Exit *ex; uint8_t nex;
  uint8_t indoor;               // interiors: walls on three sides, warm light
  uint32_t sky0, sky1, fog;
  float fogStart, fogEnd;
  uint8_t encounter;            // wild table id for '"' tiles (0 none)
  uint8_t biome;                // 0 coast, 1 desert, 2 forest, 3 fjord, 4 city
};
extern AreaDef AREAS[A_COUNT];   // filled by areasInit() from the per-town tables
void areasInit();
// the Wilds (Wilds.cpp)
void wildsInit();
void wildsEnter();
void wildsRestore();
void wildsLoot();
void wildsBoss();
int wildsStartX();
int wildsStartZ();
extern const AreaDef AREAS_MORE[];
extern const int N_AREAS_MORE;

// An actor in the world: the player, people, a follower creature.
enum Facing : uint8_t { F_N, F_E, F_S, F_W };
struct Actor {
  uint8_t look;
  uint8_t area;
  float x, z;           // tile coordinates (centre of tile = +0.5)
  float tx, tz;         // walking toward
  uint8_t face;
  bool moving;
  float walkT;          // animation phase
  const char *script;   // label run when talked to
  const char *showIf;   // flag that must be set ("" always, "!flag" for unset)
  uint8_t species;      // >0: a creature, not a person
  bool trainer;         // battles on sight
  float sightT;
};
static const int MAX_ACTORS = 40;
extern Actor g_actor[MAX_ACTORS];
extern int g_nActors;
extern Actor g_player;
extern uint8_t g_area;

// --- creatures ------------------------------------------------------------------
enum Type : uint8_t { T_FIRE, T_WATER, T_STONE, T_AIR, T_BEAST, T_FAE, T_SPIRIT, T_IRON, T_ARCANE, T_COUNT, T_NONE = 255 };
extern const char *const TYPE_NAMES[T_COUNT];
extern const uint32_t TYPE_COLS[T_COUNT];
float typeMult(uint8_t atk, uint8_t def);

enum Status : uint8_t { ST_OK, ST_BURN, ST_SOAK, ST_CHARM, ST_HAUNT, ST_RUST, ST_SLEEP, ST_STUN };
extern const char *const STATUS_NAMES[8];

struct MoveDef {
  const char *name;
  uint8_t type;
  uint8_t power;       // 0 = status move
  uint8_t acc;         // percent
  uint8_t effect;      // see Battle.cpp
  uint8_t chance;      // percent for the effect
  const char *desc;
};
extern const MoveDef MOVES[];
extern const int N_MOVES;

// Body plan for building a creature in 3D (and drawing it small in 2D).
struct Shape {
  uint8_t body;        // 0 round, 1 long (lizard/dog), 2 upright (biped), 3 serpent, 4 bird, 5 floating wisp, 6 rock-stack
  uint8_t head;        // 0 round, 1 snout, 2 beak, 3 none (merged)
  uint8_t ears;        // 0 none, 1 pointed, 2 round, 3 antlers, 4 horns, 5 antennae
  uint8_t tail;        // 0 none, 1 thin, 2 flame, 3 fan, 4 fish
  uint8_t wings;       // 0 none, 1 bird, 2 moth, 3 bat
  uint8_t legs;        // 0, 2, 4, 6
  uint32_t c1, c2, c3; // main, belly/accent, detail
  float size;          // scale
};
struct Species {
  const char *name;
  uint8_t t1, t2;
  uint8_t hp, atk, def, spd;   // base stats
  uint8_t evolveAt, evolveTo;  // 0 = none
  uint8_t catchRate;           // 1..255
  uint8_t xpYield;
  uint8_t learn[6][2];         // {level, move}
  Shape shape;
  const char *dex;
};
extern const Species SPECIES[];
extern const int N_SPECIES;

struct Mon {
  uint8_t sp, lvl;
  uint16_t hp, xp;
  uint8_t status, statusT;
  uint8_t moves[4];
  uint8_t held;
  uint32_t seed;     // variant
};
struct MonStats { int hp, atk, def, spd; };
MonStats statsOf(const Mon &m);
void monInit(Mon &m, int sp, int lvl, uint32_t seed);
int xpForLevel(int lvl);

static const int PARTY = 6, BOX = 30;
extern Mon g_party[PARTY];
extern int g_nParty;
extern Mon g_box[BOX];
extern int g_nBox;

// --- items ------------------------------------------------------------------------
enum Item : uint8_t { I_LANTERN, I_SILVER, I_MOON, I_TONIC, I_DRAUGHT, I_SALTS, I_BUN, I_COUNT };
extern const char *const ITEM_NAMES[I_COUNT];
extern const char *const ITEM_DESC[I_COUNT];
extern uint8_t g_items[I_COUNT];
extern int g_money;

// --- story state --------------------------------------------------------------------
// Flags and counters live in one small table keyed by name hash.
int getVar(const char *name);
void setVar(const char *name, int v);
inline bool flag(const char *name) { return getVar(name) != 0; }
inline void setFlag(const char *name, bool on = true) { setVar(name, on ? 1 : 0); }
uint32_t nameHash(const char *s, int n = -1);
int getVarH(uint32_t h);
void setVarH(uint32_t h, int v);
void varsClear();

enum Skill : uint8_t { SK_OBS, SK_LOGIC, SK_MEMORY, SK_CHARM, SK_DISGUISE, SK_LORE, SK_COUNT };
extern const char *const SKILL_NAMES[SK_COUNT];
extern const char *const SKILL_VOICE[SK_COUNT];
int skill(int s);

// Clues and the detective's casebook
struct ClueDef { const char *id; const char *title; const char *text; uint8_t caseNo; };
extern const ClueDef CLUES[];
extern const int N_CLUES;
struct LinkDef { const char *a, *b, *deduction, *text; };
extern const LinkDef LINKS[];
extern const int N_LINKS;
int clueIndex(const char *id);
bool hasClue(int i);

// Interrogations: statements a suspect makes, and the clue that breaks each.
struct Statement { const char *text; const char *breaker; const char *onBreak; };
struct Testimony { const char *id; uint8_t look; const char *who; Statement s[4]; uint8_t n; const char *onDone; };
extern const Testimony TESTIMONIES[];
extern const int N_TESTIMONIES;

// Accusations: who, why, how, proof - each a short list.
struct AccuseDef {
  const char *id;
  const char *who[5]; uint8_t nWho, rightWho;
  const char *why[4]; uint8_t nWhy, rightWhy;
  const char *how[4]; uint8_t nHow, rightHow;
  const char *proof;      // clue id that must be shown
  const char *wrongLabel[5]; // script label per wrong suspect
  const char *rightLabel;
};
extern const AccuseDef ACCUSES[];
extern const int N_ACCUSES;

// The story script (Story.cpp): one big text, run by Script.cpp.
extern const char *const STORY;
extern const char *const STORY_PARTS[];
extern const char *const STORY_SAND, *const STORY_GREEN, *const STORY_SKJOLD, *const STORY_UR, *const STORY_WILDS;
extern const int N_STORY_PARTS;

// --- modes and screens ---------------------------------------------------------------
enum class Mode : uint8_t { Title, World, Battle, Card, Ending };
extern Mode g_mode;

// script runner
namespace Script {
void start(const char *label);
bool active();
void update(float dt);
void key(Btn b);
void wheel(int d);
void draw();             // dialogue / choices / dice, into the current strip
void drawOver();         // chapter cards, drawn over any fade
bool blocksWorld();      // the world shouldn't move or take input
void resume(const char *label); // after a battle or a panel
}

// world
void worldLoad(int area, int x, int z, int face);
void worldUpdate(float dt);
void worldPrepare();     // builds this frame's 3D scene
void worldOverlay();     // 2D things over the 3D (lamp glows, prompts)
void worldKey(Btn b);
bool worldBusy();
Actor *actorNamed(const char *script);
void worldFree();
void worldTurn(float rad);     // the wheel / a drag turns the camera round Wren
void worldZoom(float d);       // a drag pulls it in close (0..1)
void worldCameraReset();
void worldGoInn();         // wake at the inn of the town you're in
void worldCameraToggle();  // the usual view <-> close beside Wren
float worldYaw();
bool worldAlloc();

// battle
void battleStart(int trainerTeam, const char *winLabel, const char *loseLabel);
void battleWild(int sp, int lvl);
bool battleIsBoss();
void battleUpdate(float dt);
void battlePrepare();
void battleOverlay();
void battleKey(Btn b);
void battleWheel(int d);
void battleDebug();
int wildFor(int table, int &lvl);

// menus and detective panels (UI.cpp)
namespace UI {
enum Panel : uint8_t { P_NONE, P_PAUSE, P_TEAM, P_BAG, P_CASEBOOK, P_BOARD, P_ACCUSE, P_INTERRO, P_SKILLS, P_SAVE,
                       P_OBSERVE, P_MAP };
void open(Panel p, const char *arg = nullptr);
bool active();
Panel current();
void update(float dt);
void key(Btn b);
void wheel(int d);
void draw();
void toast(const char *msg);
void drawToast();
}

// pictures
void drawPortrait(int look, float x, float y, float w, float h, int mood = -1);
void drawMon2D(int sp, float cx, float cy, float size, uint32_t seed = 0);
void buildMon3D(MeshBuf &m, int sp, float x, float y, float z, float face, float t, float scale, uint32_t seed,
                float flash = 0);
void buildFigure(MeshBuf &m, int look, float x, float z, float face, float walkT, bool moving, float bob = 0);
void drawCard(int id, float t);       // a myth scene card (full screen)
extern const char *const CARD_TEXT[];
extern int g_cardPage;        // which page of the card's story is showing
bool cardHasMore(int id);
extern const int N_CARDS;

// sound
void sfx(int id);

// the chatter model (TextModel.cpp)
bool chatterStart();
void chatterStop();
void chatterPause(bool p);   // hold off while a place is being built
bool chatterTake(const char *tag, char *out, int cap);

// save / load
bool saveGame();
bool loadGame();
bool hasSave();

// screen effects
extern float g_fade;         // 0 clear .. 1 black
extern float g_flash;        // white flash
extern float g_shake;
extern float g_cardT0;       // when the scene card on screen appeared

// fonts
#define F_BODY FONT_BODY
#define F_BOLD FONT_BOLD
#define F_TITLE FONT_TITLE
#define F_SMALL FONT_SMALL

// colours used across the UI
namespace Col {
static const uint16_t INK = 0x10C4, PAPER = 0xEEF8, CREAM = 0xF71A, AMBER = 0xE507, BRASS = 0xBC27, OXRED = 0x7926,
                      TEAL = 0x4C4F, DIM = 0x8C71, SNOW = 0xFFFF, GOOD = 0x4E68, BAD = 0xD1A6, FOGC = 0x3AAC;
}

} // namespace Gaslight
