// Gaslight & Griffins - creatures: types, moves, the 62 species, items.
#include "Game.h"
#include <stdio.h>

namespace Gaslight {

const char *const TYPE_NAMES[T_COUNT] = {"Fire", "Water", "Stone", "Air", "Beast", "Fae", "Spirit", "Iron", "Arcane"};
const uint32_t TYPE_COLS[T_COUNT] = {0xE0702A, 0x3A86C8, 0x9A8A6A, 0x9AC8D8, 0xB07A4A, 0x7AC86A, 0x8A6AC8, 0x8A9AA8, 0xC86AB0};
const char *const STATUS_NAMES[8] = {"", "BRN", "SOAK", "CHRM", "HNT", "RUST", "SLP", "STUN"};

// attacker row, defender column: 4 = x2, 2 = x1, 1 = x0.5, 0 = no effect
static const uint8_t CHART[T_COUNT][T_COUNT] = {
    //        Fi Wa St Ai Be Fa Sp Ir Ar
    /*Fire*/ {1, 1, 1, 2, 4, 4, 2, 4, 2},
    /*Water*/{4, 1, 4, 2, 2, 1, 2, 2, 2},
    /*Stone*/{4, 1, 2, 4, 1, 2, 2, 1, 2},
    /*Air*/  {2, 4, 1, 2, 4, 2, 2, 1, 2},
    /*Beast*/{2, 2, 4, 2, 2, 2, 0, 1, 4},
    /*Fae*/  {1, 4, 2, 2, 2, 2, 2, 1, 4},
    /*Spirit*/{2, 2, 2, 2, 0, 4, 4, 1, 2},
    /*Iron*/ {1, 1, 2, 2, 2, 4, 4, 1, 2},
    /*Arcane*/{2, 2, 2, 2, 1, 2, 4, 4, 1},
};
float typeMult(uint8_t a, uint8_t d) {
  if (a >= T_COUNT || d >= T_COUNT) return 1;
  return CHART[a][d] * 0.5f;
}

// effects: see Battle.cpp
enum Fx : uint8_t { E_NONE, E_BURN, E_SOAK, E_CHARM, E_HAUNT, E_RUST, E_SLEEP, E_STUN, E_ATKUP, E_DEFUP, E_SPDUP,
                    E_ATKDN, E_DEFDN, E_SPDDN, E_HEAL, E_DRAIN, E_FIRST, E_RECOIL, E_CRIT, E_LURE, E_LOCK, E_TRUTH,
                    E_ATKUP2, E_DEFUP2, E_SPDUP2, E_FATE };

enum MoveId : uint8_t {
  M_EMBER, M_CINDER, M_FLARE, M_FURNACE, M_STOKE,
  M_SPLASH, M_BRINE, M_TIDE, M_UNDERTOW, M_MISTVEIL,
  M_PEBBLE, M_ROCKSLAM, M_LANDSLIDE, M_STONESKIN, M_QUAKE,
  M_GUST, M_WINGSLICE, M_SQUALL, M_GALE, M_TAILWIND,
  M_SCRATCH, M_BITE, M_POUNCE, M_MAUL, M_GROWL, M_HOWL,
  M_THORN, M_GLAMOUR, M_BRAMBLE, M_FAIRYRING, M_MOONDEW,
  M_CHILL, M_WAIL, M_SOULDRAIN, M_GRAVETOLL, M_FADE,
  M_RIVET, M_IRONBASH, M_CHAINLASH, M_ANVIL, M_BRACE,
  M_INKBOLT, M_RUNESPARK, M_SIGIL, M_STUDY, M_MINDLOCK,
  M_SIRENCALL, M_BOULDER, M_FAIRWIND, M_RIDDLE, M_TRUTH, M_DEVOUR, M_REGROWTH, M_FLOOD, M_FATE, M_TACKLE,
  M_COUNT
};

const MoveDef MOVES[] = {
    {"Ember Flick", T_FIRE, 40, 100, E_BURN, 10, "A flick of hot ash. May burn."},
    {"Cinder Bite", T_FIRE, 60, 100, E_BURN, 10, "Teeth like coals. May burn."},
    {"Flare Lash", T_FIRE, 80, 95, E_BURN, 20, "A whip of flame. May burn."},
    {"Furnace Roar", T_FIRE, 105, 85, E_NONE, 0, "Everything it has, at once."},
    {"Stoke", T_FIRE, 0, 100, E_ATKUP2, 100, "Banks its fire: attack up sharply."},
    {"Splash Jab", T_WATER, 40, 100, E_NONE, 0, "A quick slap of water."},
    {"Brine Spit", T_WATER, 55, 100, E_SOAK, 25, "Salt water in the eyes. May soak."},
    {"Tide Crash", T_WATER, 80, 100, E_NONE, 0, "Comes in like the tide."},
    {"Undertow", T_WATER, 95, 90, E_SPDDN, 30, "Drags the foe down. May slow it."},
    {"Mist Veil", T_WATER, 0, 100, E_DEFUP2, 100, "Wraps itself in sea mist: defence up sharply."},
    {"Pebble Toss", T_STONE, 40, 100, E_NONE, 0, "A thrown stone."},
    {"Rock Slam", T_STONE, 65, 95, E_NONE, 0, "A body like a boulder."},
    {"Landslide", T_STONE, 90, 85, E_SPDDN, 20, "Half a hill. May slow the foe."},
    {"Stone Skin", T_STONE, 0, 100, E_DEFUP2, 100, "Hardens: defence up sharply."},
    {"Quake", T_STONE, 105, 85, E_NONE, 0, "The ground itself bucks."},
    {"Gust", T_AIR, 40, 100, E_NONE, 0, "A shove of wind."},
    {"Wing Slice", T_AIR, 60, 100, E_CRIT, 0, "Edge-first. Often lands hard."},
    {"Squall", T_AIR, 80, 95, E_SPDDN, 20, "A wet, cold blast. May slow."},
    {"Gale Burst", T_AIR, 100, 85, E_NONE, 0, "A storm in a moment."},
    {"Tailwind", T_AIR, 0, 100, E_SPDUP2, 100, "Rides the wind: speed up sharply."},
    {"Scratch", T_BEAST, 40, 100, E_NONE, 0, "Claws."},
    {"Bite", T_BEAST, 60, 100, E_CRIT, 0, "Finds the soft spot. Often lands hard."},
    {"Pounce", T_BEAST, 50, 100, E_FIRST, 0, "Always strikes first."},
    {"Maul", T_BEAST, 90, 90, E_NONE, 0, "All teeth and weight."},
    {"Growl", T_BEAST, 0, 100, E_ATKDN, 100, "A low warning: foe's attack down."},
    {"Howl", T_BEAST, 0, 100, E_ATKUP, 100, "Calls up its courage: attack up."},
    {"Thorn Prick", T_FAE, 40, 100, E_NONE, 0, "A bramble's needle."},
    {"Glamour Dust", T_FAE, 0, 80, E_CHARM, 100, "Shining dust that muddles. Charms."},
    {"Bramble Whip", T_FAE, 70, 100, E_NONE, 0, "A lash of briars."},
    {"Fairy Ring", T_FAE, 90, 95, E_CHARM, 15, "Mushrooms rise in a circle. May charm."},
    {"Moon Dew", T_FAE, 0, 100, E_HEAL, 50, "Drinks the dew: heals half."},
    {"Chill Touch", T_SPIRIT, 45, 100, E_HAUNT, 20, "A cold hand. May haunt."},
    {"Wail", T_SPIRIT, 65, 100, E_ATKDN, 20, "A sound from under the sea. May weaken."},
    {"Soul Drain", T_SPIRIT, 75, 100, E_DRAIN, 50, "Takes, and keeps half."},
    {"Grave Toll", T_SPIRIT, 95, 90, E_HAUNT, 30, "A bell for the drowned. May haunt."},
    {"Fade", T_SPIRIT, 0, 100, E_DEFUP2, 100, "Thins to mist: defence up sharply."},
    {"Rivet Shot", T_IRON, 45, 100, E_NONE, 0, "A hot rivet, fired."},
    {"Iron Bash", T_IRON, 70, 100, E_NONE, 0, "Plate on bone."},
    {"Chain Lash", T_IRON, 80, 95, E_RUST, 30, "A swung chain. May rust."},
    {"Anvil Drop", T_IRON, 105, 80, E_NONE, 0, "Heavy, and from a height."},
    {"Brace", T_IRON, 0, 100, E_DEFUP2, 100, "Locks its plates: defence up sharply."},
    {"Ink Bolt", T_ARCANE, 45, 100, E_NONE, 0, "A spat glyph."},
    {"Rune Spark", T_ARCANE, 65, 100, E_STUN, 15, "A crackling letter. May stun."},
    {"Sigil Blast", T_ARCANE, 90, 95, E_NONE, 0, "A word of power, spoken all at once."},
    {"Study", T_ARCANE, 0, 100, E_ATKUP, 100, "Reads the foe: attack up."},
    {"Mind Lock", T_ARCANE, 0, 75, E_SLEEP, 100, "A riddle with no end. Puts to sleep."},
    {"Siren Call", T_WATER, 0, 90, E_LURE, 100, "The Sisters' song: charms and weakens."},
    {"Boulder Toss", T_STONE, 115, 85, E_NONE, 0, "Old One-Eye's own throw."},
    {"Fair Wind", T_AIR, 0, 100, E_SPDUP2, 100, "Aeolus's gift: speed up sharply."},
    {"Riddle Lock", T_ARCANE, 70, 100, E_LOCK, 100, "The Sphinx's question: foe can't repeat."},
    {"Truth Weight", T_ARCANE, 80, 100, E_TRUTH, 100, "Ignores any boasting (stat changes)."},
    {"Devour", T_SPIRIT, 85, 100, E_DRAIN, 50, "Ammit's hunger. Heals by half."},
    {"Regrowth", T_FAE, 0, 100, E_HEAL, 50, "The Green Knight's gift: heals half."},
    {"Flood", T_WATER, 115, 90, E_NONE, 0, "The old flood, remembered."},
    {"Fate Thread", T_ARCANE, 0, 100, E_FATE, 100, "The Norns' thread: next hits land hard."},
    {"Tackle", T_BEAST, 35, 100, E_NONE, 0, "Runs at it."},
};
const int N_MOVES = M_COUNT;

#define LS(a, b, c, d, e, f, g, h, i, j, k, l) {{a, b}, {c, d}, {e, f}, {g, h}, {i, j}, {k, l}}
#define TN T_NONE

//  name, t1, t2, hp, atk, def, spd, evoAt, evoTo, catch, xp, learnset, shape{body,head,ears,tail,wings,legs,c1,c2,c3,size}, dex
const Species SPECIES[] = {
    // 0-2 Embrit line (starter)
    {"Embrit", T_FIRE, TN, 44, 56, 40, 62, 16, 1, 45, 60, LS(1, M_SCRATCH, 1, M_EMBER, 7, M_HOWL, 11, M_CINDER, 20, M_FLARE, 30, M_STOKE),
     {1, 1, 1, 2, 0, 4, 0x2A2420, 0xE8702A, 0x3A302A, 0.85f}, "A soot salamander kitten. Its tail is a candle that never goes out."},
    {"Cindrake", T_FIRE, T_BEAST, 60, 74, 56, 80, 34, 2, 45, 140, LS(1, M_CINDER, 16, M_BITE, 22, M_FLARE, 28, M_STOKE, 34, M_MAUL, 40, M_FURNACE),
     {1, 1, 4, 2, 0, 4, 0x3A2420, 0xF08A3A, 0x5A3A2A, 1.1f}, "Sleeps in chimneys. Fogmouth's sweeps leave it a saucer of lamp oil."},
    {"Pyrewyrm", T_FIRE, T_BEAST, 80, 100, 74, 92, 0, 0, 45, 220, LS(1, M_FLARE, 34, M_FURNACE, 38, M_MAUL, 42, M_STOKE, 46, M_POUNCE, 50, M_FURNACE),
     {1, 1, 4, 2, 3, 4, 0x4A2018, 0xFFA040, 0x2A1A14, 1.4f}, "When it stretches its wings the whole street goes warm."},
    // 3-5 Puddock line (starter)
    {"Puddock", T_WATER, TN, 50, 48, 56, 44, 16, 4, 45, 60, LS(1, M_TACKLE, 1, M_SPLASH, 7, M_MISTVEIL, 11, M_BRINE, 20, M_TIDE, 30, M_PEBBLE),
     {0, 0, 2, 0, 0, 4, 0x3A5A7A, 0x9AB8C8, 0x5A6A6A, 0.85f}, "A sleepy toad with a barnacle back. Never in a hurry."},
    {"Brineback", T_WATER, T_STONE, 70, 64, 80, 52, 34, 5, 45, 140, LS(1, M_BRINE, 16, M_ROCKSLAM, 22, M_TIDE, 28, M_STONESKIN, 34, M_UNDERTOW, 40, M_LANDSLIDE),
     {0, 0, 4, 0, 0, 4, 0x2E4A5A, 0x8AA8A8, 0x6A6A5A, 1.15f}, "Carries a little reef on its shell. Crabs live there, rent free."},
    {"Tidewarden", T_WATER, T_STONE, 96, 84, 104, 60, 0, 0, 45, 220, LS(1, M_TIDE, 34, M_UNDERTOW, 38, M_QUAKE, 42, M_MISTVEIL, 46, M_LANDSLIDE, 50, M_FLOOD),
     {0, 0, 4, 4, 0, 4, 0x24384A, 0x7A98A0, 0x7A7A6A, 1.5f}, "Old harbours were built around sleeping Tidewardens."},
    // 6-8 Sprigling line (starter)
    {"Sprigling", T_FAE, TN, 46, 50, 46, 58, 16, 7, 45, 60, LS(1, M_TACKLE, 1, M_THORN, 7, M_GLAMOUR, 11, M_BRAMBLE, 20, M_MOONDEW, 30, M_FAIRYRING),
     {1, 0, 3, 1, 0, 4, 0x5A7A3A, 0xC8D8A0, 0x7A5A3A, 0.85f}, "A fawn of moss and twig. Dandelion seeds follow it around."},
    {"Thornhart", T_FAE, T_BEAST, 64, 70, 62, 76, 34, 8, 45, 140, LS(1, M_BRAMBLE, 16, M_BITE, 22, M_FAIRYRING, 28, M_MOONDEW, 34, M_MAUL, 40, M_GLAMOUR),
     {1, 1, 3, 1, 0, 4, 0x4A6A2A, 0xB8C890, 0x6A4A2A, 1.15f}, "Its antlers bloom in spring and drop berries in autumn."},
    {"Elderstag", T_FAE, T_BEAST, 86, 94, 84, 90, 0, 0, 45, 220, LS(1, M_FAIRYRING, 34, M_MAUL, 38, M_MOONDEW, 42, M_HOWL, 46, M_BRAMBLE, 50, M_FAIRYRING),
     {1, 1, 3, 1, 0, 4, 0x3A5A24, 0xD8E0B0, 0xC8C8A0, 1.5f}, "Woods grow back wherever an Elderstag has slept."},
    // 9-10 Gutterat
    {"Gutterat", T_BEAST, TN, 38, 52, 36, 60, 18, 10, 220, 40, LS(1, M_SCRATCH, 1, M_GROWL, 5, M_BITE, 9, M_POUNCE, 14, M_HOWL, 22, M_MAUL),
     {1, 1, 2, 1, 0, 4, 0x6A5A4A, 0x9A8A7A, 0xC8A8A0, 0.6f}, "Wears a stolen thimble as a helmet. Very proud of it."},
    {"Alleyking", T_BEAST, TN, 62, 80, 54, 86, 0, 0, 90, 120, LS(1, M_BITE, 18, M_MAUL, 24, M_POUNCE, 28, M_HOWL, 32, M_SCRATCH, 36, M_MAUL),
     {1, 1, 2, 1, 0, 4, 0x4A3E34, 0x8A7A6A, 0xD8B040, 0.85f}, "Rules three streets. Wears a bottle-cap crown."},
    // 11-13 Wispen
    {"Wispen", T_SPIRIT, T_FIRE, 36, 50, 40, 62, 14, 12, 190, 45, LS(1, M_CHILL, 1, M_EMBER, 6, M_FADE, 10, M_WAIL, 16, M_CINDER, 24, M_SOULDRAIN),
     {5, 3, 0, 0, 0, 0, 0x8AB8E8, 0xC8E8FF, 0x5A7AA8, 0.6f}, "A lost candle-flame. Follows lamplighters home."},
    {"Lanternghast", T_SPIRIT, T_FIRE, 54, 70, 56, 76, 30, 13, 90, 120, LS(1, M_SOULDRAIN, 14, M_CINDER, 20, M_WAIL, 26, M_FLARE, 30, M_FADE, 36, M_GRAVETOLL),
     {5, 3, 0, 0, 0, 0, 0x6A9AD8, 0xFFD27A, 0x3A4A6A, 0.85f}, "Hangs from bare hooks where lanterns used to be."},
    {"Gravelamp", T_SPIRIT, T_FIRE, 70, 96, 70, 92, 0, 0, 45, 210, LS(1, M_GRAVETOLL, 30, M_FLARE, 34, M_SOULDRAIN, 38, M_FURNACE, 42, M_FADE, 46, M_GRAVETOLL),
     {5, 3, 0, 0, 0, 0, 0x4A6AB8, 0xFFE6A0, 0x2A3A5A, 1.1f}, "Lights the way for the drowned, so they can find their beds."},
    // 14-15 Pebblet
    {"Pebblet", T_STONE, TN, 44, 50, 66, 30, 20, 15, 200, 45, LS(1, M_TACKLE, 1, M_PEBBLE, 6, M_STONESKIN, 11, M_ROCKSLAM, 18, M_QUAKE, 26, M_LANDSLIDE),
     {6, 3, 0, 0, 0, 0, 0x8A8A80, 0xA8A898, 0xC8B8A0, 0.6f}, "Three sea pebbles who agreed to be one creature."},
    {"Cairnback", T_STONE, TN, 72, 76, 100, 36, 0, 0, 75, 130, LS(1, M_ROCKSLAM, 20, M_LANDSLIDE, 26, M_STONESKIN, 32, M_QUAKE, 38, M_BRACE, 44, M_QUAKE),
     {6, 3, 0, 0, 0, 2, 0x6A6A62, 0x9A9A8A, 0xC8B8A0, 0.95f}, "Walkers add a stone to its back for luck. It doesn't mind."},
    // 16-17 Gullet
    {"Gullet", T_AIR, T_WATER, 40, 50, 38, 64, 18, 17, 220, 40, LS(1, M_GUST, 1, M_SPLASH, 5, M_GROWL, 9, M_WINGSLICE, 15, M_BRINE, 22, M_SQUALL),
     {4, 2, 0, 3, 1, 2, 0xE8E8E0, 0xF4F0E8, 0xE8A040, 0.6f}, "Eats anything. Has eaten a pocket-watch. It still ticks."},
    {"Stormskua", T_AIR, T_WATER, 62, 78, 56, 90, 0, 0, 90, 120, LS(1, M_WINGSLICE, 18, M_SQUALL, 24, M_TIDE, 30, M_TAILWIND, 34, M_GALE, 40, M_GALE),
     {4, 2, 0, 3, 1, 2, 0x8A9AA8, 0xE8E8E0, 0xE8A040, 0.9f}, "Rides the storms in. Fishermen read the weather by it."},
    // 18-19 Fogeel
    {"Fogeel", T_WATER, T_SPIRIT, 42, 52, 44, 58, 22, 19, 160, 50, LS(1, M_SPLASH, 1, M_CHILL, 6, M_MISTVEIL, 12, M_BRINE, 18, M_WAIL, 26, M_UNDERTOW),
     {3, 0, 0, 4, 0, 0, 0xA8C8C8, 0xD8F0F0, 0x4A9A9A, 0.7f}, "Half eel, half mist. You can see the harbour through it."},
    {"Mistmoray", T_WATER, T_SPIRIT, 68, 82, 64, 78, 0, 0, 60, 140, LS(1, M_BRINE, 22, M_UNDERTOW, 28, M_SOULDRAIN, 32, M_GRAVETOLL, 38, M_FADE, 44, M_TIDE),
     {3, 1, 0, 4, 0, 0, 0x7AA8A8, 0xC8E8E0, 0x2A6A6A, 1.0f}, "Coils round wrecks. Some say it keeps them company."},
    // 20-21 Inkmoth
    {"Inkmoth", T_ARCANE, T_AIR, 38, 54, 38, 66, 20, 21, 150, 50, LS(1, M_GUST, 1, M_INKBOLT, 6, M_STUDY, 11, M_RUNESPARK, 16, M_WINGSLICE, 24, M_MINDLOCK),
     {4, 0, 5, 0, 2, 2, 0x3A3A3A, 0xF0E8D0, 0xE8DCC0, 0.6f}, "Its wings are covered in a script no one has read."},
    {"Scriptwing", T_ARCANE, T_AIR, 60, 82, 56, 92, 0, 0, 60, 140, LS(1, M_RUNESPARK, 20, M_SIGIL, 26, M_SQUALL, 30, M_MINDLOCK, 36, M_STUDY, 40, M_GALE),
     {4, 0, 5, 0, 2, 2, 0x2A2A34, 0xF8F0D8, 0xD8C8A0, 0.9f}, "Libraries keep one. The books behave better."},
    // 22-23 Bolthound
    {"Bolthound", T_IRON, T_BEAST, 50, 58, 60, 52, 24, 23, 120, 55, LS(1, M_TACKLE, 1, M_RIVET, 7, M_GROWL, 12, M_IRONBASH, 18, M_BITE, 26, M_CHAINLASH),
     {1, 1, 2, 1, 0, 4, 0x6A7A8A, 0xA8B0B8, 0x3A4048, 0.7f}, "A police pup with riveted plates. Never loses a scent."},
    {"Ironmastiff", T_IRON, T_BEAST, 78, 90, 92, 62, 0, 0, 45, 150, LS(1, M_IRONBASH, 24, M_CHAINLASH, 30, M_MAUL, 34, M_BRACE, 40, M_ANVIL, 46, M_MAUL),
     {1, 1, 2, 1, 0, 4, 0x4A5A6A, 0x8A98A8, 0x2A3038, 1.1f}, "The constabulary's pride. Sits outside the station like a statue."},
    // 24-26 Scarabit
    {"Scarabit", T_STONE, T_FIRE, 40, 52, 58, 44, 18, 25, 190, 45, LS(1, M_PEBBLE, 1, M_EMBER, 6, M_STONESKIN, 11, M_ROCKSLAM, 16, M_CINDER, 24, M_LANDSLIDE),
     {0, 0, 5, 0, 0, 6, 0xB88A3A, 0xE8C060, 0x3A2A1A, 0.55f}, "Rolls tiny suns across the dunes at dawn."},
    {"Dunebeetle", T_STONE, T_FIRE, 60, 72, 82, 50, 32, 26, 90, 120, LS(1, M_ROCKSLAM, 18, M_CINDER, 24, M_LANDSLIDE, 28, M_FLARE, 32, M_STONESKIN, 38, M_QUAKE),
     {0, 0, 4, 0, 0, 6, 0x9A6A2A, 0xE8B050, 0x2A1A10, 0.85f}, "Its shell holds the day's heat all night."},
    {"Sunscarab", T_STONE, T_FIRE, 80, 96, 104, 66, 0, 0, 45, 210, LS(1, M_LANDSLIDE, 32, M_FLARE, 36, M_QUAKE, 40, M_FURNACE, 44, M_STONESKIN, 48, M_QUAKE),
     {0, 0, 4, 0, 1, 6, 0xD8A030, 0xFFE080, 0x4A2A10, 1.15f}, "Carries a small sun between its horns."},
    // 27-28 Jackalet
    {"Jackalet", T_SPIRIT, T_BEAST, 44, 60, 42, 64, 26, 28, 120, 55, LS(1, M_SCRATCH, 1, M_CHILL, 7, M_GROWL, 12, M_BITE, 18, M_WAIL, 24, M_SOULDRAIN),
     {1, 1, 1, 1, 0, 4, 0x2A2A2A, 0xB89A5A, 0xD8B040, 0.7f}, "Waits by tomb doors. Nobody knows whom for."},
    {"Duatjackal", T_SPIRIT, T_BEAST, 70, 92, 66, 90, 0, 0, 45, 150, LS(1, M_SOULDRAIN, 26, M_MAUL, 30, M_GRAVETOLL, 36, M_HOWL, 40, M_POUNCE, 46, M_GRAVETOLL),
     {1, 1, 1, 1, 0, 4, 0x1A1A1E, 0xD8B040, 0xE8C860, 1.05f}, "Walks between the lit world and the other one."},
    // 29-30 Sandsnap
    {"Sandsnap", T_BEAST, T_STONE, 46, 60, 52, 54, 24, 30, 150, 50, LS(1, M_BITE, 1, M_PEBBLE, 6, M_GROWL, 11, M_ROCKSLAM, 16, M_POUNCE, 22, M_LANDSLIDE),
     {3, 1, 0, 0, 0, 0, 0xC8A870, 0xE8D8A0, 0x8A6A3A, 0.7f}, "Buried to the eyes, waiting for ankles."},
    {"Asplord", T_BEAST, T_STONE, 70, 92, 76, 74, 0, 0, 60, 140, LS(1, M_ROCKSLAM, 24, M_MAUL, 28, M_LANDSLIDE, 34, M_POUNCE, 38, M_QUAKE, 44, M_MAUL),
     {3, 1, 4, 0, 0, 0, 0xA8884A, 0xE8D090, 0x5A3A1A, 1.1f}, "Wears a hood of scales like a crown."},
    // 31-32 Ibisling
    {"Ibisling", T_ARCANE, T_AIR, 42, 54, 44, 60, 26, 32, 140, 50, LS(1, M_GUST, 1, M_INKBOLT, 7, M_STUDY, 12, M_WINGSLICE, 18, M_RUNESPARK, 24, M_SIGIL),
     {4, 2, 0, 0, 1, 2, 0xE8E0D8, 0xF8F0E8, 0x2A2A2A, 0.7f}, "Writes in the mud with its beak. The marks are always true."},
    {"Quillibis", T_ARCANE, T_AIR, 66, 84, 64, 86, 0, 0, 50, 140, LS(1, M_SIGIL, 26, M_SQUALL, 30, M_MINDLOCK, 36, M_GALE, 40, M_STUDY, 46, M_SIGIL),
     {4, 2, 5, 3, 1, 2, 0xE8E8E0, 0xFFFFF0, 0x1A1A1A, 1.05f}, "Keeper of the old scrolls. Hates being rushed."},
    // 33-34 Dustmote
    {"Dustmote", T_ARCANE, T_STONE, 40, 50, 56, 48, 28, 34, 180, 50, LS(1, M_INKBOLT, 1, M_PEBBLE, 7, M_STONESKIN, 12, M_RUNESPARK, 18, M_ROCKSLAM, 26, M_SIGIL),
     {5, 3, 0, 0, 0, 0, 0xD8C8A0, 0xF0E8C8, 0xA8986A, 0.55f}, "A tiny whirl of sand that knows things."},
    {"Glassdjinn", T_ARCANE, T_STONE, 64, 88, 74, 80, 0, 0, 50, 140, LS(1, M_SIGIL, 28, M_LANDSLIDE, 32, M_MINDLOCK, 36, M_QUAKE, 40, M_STUDY, 46, M_SIGIL),
     {5, 3, 0, 0, 0, 0, 0xA8D8E8, 0xE8F8FF, 0x6AA8C8, 0.95f}, "Lives in the glassworks' hottest furnace. Grants no wishes."},
    // 35-36 Pixmote
    {"Pixmote", T_FAE, TN, 38, 50, 40, 72, 22, 36, 170, 45, LS(1, M_THORN, 1, M_GLAMOUR, 6, M_GUST, 11, M_BRAMBLE, 16, M_MOONDEW, 22, M_FAIRYRING),
     {2, 0, 1, 0, 2, 2, 0xE8A8D8, 0xFFE8F8, 0xA8E8C8, 0.5f}, "Thinks your hat is its house."},
    {"Thistlequeen", T_FAE, TN, 60, 80, 62, 94, 0, 0, 60, 140, LS(1, M_FAIRYRING, 22, M_GLAMOUR, 28, M_MOONDEW, 32, M_BRAMBLE, 38, M_FAIRYRING, 44, M_FAIRYRING),
     {2, 0, 3, 0, 2, 2, 0xA86AC8, 0xF0D8F8, 0x7AC86A, 0.85f}, "Holds court in thistle patches. Expects a bow."},
    // 37-38 Mossgob
    {"Mossgob", T_STONE, T_FAE, 50, 54, 62, 36, 26, 38, 160, 50, LS(1, M_TACKLE, 1, M_PEBBLE, 7, M_THORN, 12, M_ROCKSLAM, 18, M_BRAMBLE, 24, M_STONESKIN),
     {2, 0, 1, 0, 0, 2, 0x6A7A4A, 0x9AA87A, 0x5A6A3A, 0.65f}, "A goblin made of barrow moss. Hoards buttons."},
    {"Barrowtroll", T_STONE, T_FAE, 86, 92, 96, 40, 0, 0, 45, 150, LS(1, M_ROCKSLAM, 26, M_FAIRYRING, 30, M_QUAKE, 36, M_STONESKIN, 40, M_LANDSLIDE, 46, M_QUAKE),
     {2, 0, 4, 0, 0, 2, 0x5A6A3A, 0x8A9A6A, 0x8A8A7A, 1.25f}, "Sleeps under old barrows. Snores like rockfall."},
    // 39-40 Hollyhob
    {"Hollyhob", T_FAE, T_BEAST, 46, 58, 48, 60, 28, 40, 140, 55, LS(1, M_SCRATCH, 1, M_THORN, 7, M_HOWL, 12, M_BRAMBLE, 18, M_BITE, 24, M_FAIRYRING),
     {2, 1, 1, 1, 0, 2, 0x3A6A3A, 0xC83A3A, 0x2A4A2A, 0.65f}, "Wears holly berries like buttons. Knows every path in the wood."},
    {"Greencloak", T_FAE, T_BEAST, 72, 90, 72, 84, 0, 0, 45, 150, LS(1, M_FAIRYRING, 28, M_MAUL, 32, M_MOONDEW, 36, M_HOWL, 40, M_BRAMBLE, 46, M_MAUL),
     {2, 1, 3, 1, 0, 2, 0x2A5A2A, 0xD8C890, 0x1A3A1A, 1.15f}, "Stands so still in the wood you walk past it twice."},
    // 41-42 Acornet
    {"Acornet", T_FAE, T_STONE, 42, 48, 60, 50, 24, 42, 180, 45, LS(1, M_TACKLE, 1, M_THORN, 6, M_STONESKIN, 11, M_PEBBLE, 16, M_BRAMBLE, 22, M_ROCKSLAM),
     {0, 0, 0, 0, 0, 2, 0x8A6A3A, 0xC8A870, 0x5A4A2A, 0.5f}, "An acorn with legs and a very serious face."},
    {"Oakwarden", T_FAE, T_STONE, 84, 80, 100, 40, 0, 0, 50, 140, LS(1, M_BRAMBLE, 24, M_ROCKSLAM, 28, M_STONESKIN, 32, M_FAIRYRING, 38, M_QUAKE, 44, M_MOONDEW),
     {2, 0, 3, 0, 0, 2, 0x5A4A2A, 0x7A9A4A, 0x3A6A2A, 1.3f}, "An old oak that decided to walk. Slowly."},
    // 43-44 Fennix
    {"Fennix", T_FAE, T_FIRE, 40, 58, 40, 70, 26, 44, 140, 50, LS(1, M_SCRATCH, 1, M_EMBER, 7, M_GLAMOUR, 12, M_CINDER, 18, M_BRAMBLE, 24, M_FLARE),
     {1, 1, 1, 3, 0, 4, 0xE88A3A, 0xF8E0C0, 0x8A3A1A, 0.65f}, "A heath fox whose brush leaves little fires that don't spread."},
    {"Emberbrush", T_FAE, T_FIRE, 64, 88, 62, 94, 0, 0, 50, 140, LS(1, M_FLARE, 26, M_FAIRYRING, 30, M_STOKE, 34, M_POUNCE, 38, M_FURNACE, 44, M_FLARE),
     {1, 1, 1, 3, 0, 4, 0xD86A2A, 0xFFE8C8, 0x6A2A10, 1.0f}, "Paints the heath gold in autumn with a swish of its tail."},
    // 45-46 Rimecub
    {"Rimecub", T_BEAST, T_WATER, 52, 58, 52, 46, 30, 46, 140, 55, LS(1, M_SCRATCH, 1, M_SPLASH, 7, M_GROWL, 12, M_BITE, 18, M_BRINE, 26, M_MAUL),
     {0, 1, 2, 0, 0, 4, 0xE8F0F8, 0xC8D8E8, 0x8AA8C8, 0.7f}, "A snowball that grew a nose and an appetite."},
    {"Frostbear", T_BEAST, T_WATER, 96, 98, 82, 54, 0, 0, 45, 160, LS(1, M_MAUL, 30, M_TIDE, 34, M_HOWL, 38, M_UNDERTOW, 42, M_MAUL, 48, M_FLOOD),
     {0, 1, 2, 0, 0, 4, 0xD8E8F0, 0xB8C8D8, 0x6A8AA8, 1.4f}, "Fishes the fjord through holes it punches in the ice."},
    // 47-48 Corvel
    {"Corvel", T_AIR, T_SPIRIT, 42, 58, 40, 70, 28, 48, 150, 55, LS(1, M_GUST, 1, M_CHILL, 7, M_GROWL, 12, M_WINGSLICE, 18, M_WAIL, 24, M_SQUALL),
     {4, 2, 0, 3, 1, 2, 0x1E1E26, 0x3A3A48, 0x8A8AA0, 0.6f}, "A young crow that remembers every face it has ever seen."},
    {"Rookshade", T_AIR, T_SPIRIT, 66, 88, 60, 98, 0, 0, 50, 150, LS(1, M_SQUALL, 28, M_SOULDRAIN, 32, M_GALE, 36, M_GRAVETOLL, 40, M_TAILWIND, 46, M_GALE),
     {4, 2, 0, 3, 1, 2, 0x14141C, 0x2A2A38, 0x6A6A88, 0.95f}, "Its shadow arrives a moment before it does."},
    // 49-50 Runeseal
    {"Runeseal", T_WATER, T_ARCANE, 54, 52, 56, 50, 32, 50, 140, 55, LS(1, M_SPLASH, 1, M_INKBOLT, 7, M_MISTVEIL, 12, M_BRINE, 18, M_RUNESPARK, 26, M_TIDE),
     {3, 0, 0, 4, 0, 0, 0x8A9AA8, 0xC8D0D8, 0x3A4A5A, 0.8f}, "Its spots are runes. It reads them to its pups."},
    {"Skaldwalrus", T_WATER, T_ARCANE, 96, 82, 90, 52, 0, 0, 45, 160, LS(1, M_TIDE, 32, M_SIGIL, 36, M_UNDERTOW, 40, M_STUDY, 44, M_FLOOD, 50, M_SIGIL),
     {0, 1, 0, 4, 0, 0, 0x7A6A5A, 0xA89A8A, 0xF0E8D8, 1.4f}, "Sings sagas to the fjord. Very long sagas."},
    // 51-52 Cogwren
    {"Cogwren", T_IRON, T_AIR, 42, 54, 56, 68, 30, 52, 140, 55, LS(1, M_GUST, 1, M_RIVET, 7, M_BRACE, 12, M_WINGSLICE, 18, M_IRONBASH, 24, M_SQUALL),
     {4, 2, 0, 3, 1, 2, 0xB8853A, 0xD8B070, 0x5A4A3A, 0.55f}, "A clockwork wren. Nobody wound it. It ticks anyway."},
    {"Clockkite", T_IRON, T_AIR, 64, 82, 84, 96, 0, 0, 50, 150, LS(1, M_WINGSLICE, 30, M_CHAINLASH, 34, M_GALE, 38, M_BRACE, 42, M_ANVIL, 46, M_TAILWIND),
     {4, 2, 0, 3, 1, 2, 0x9A6A2A, 0xE8C880, 0x3A2A1A, 0.95f}, "Its wings are clock hands. It is never late."},
    // 53-54 Clayling
    {"Clayling", T_STONE, T_ARCANE, 54, 54, 66, 36, 34, 54, 140, 60, LS(1, M_TACKLE, 1, M_PEBBLE, 7, M_STUDY, 12, M_ROCKSLAM, 18, M_RUNESPARK, 26, M_SIGIL),
     {2, 0, 0, 0, 0, 2, 0xB87A5A, 0xD89A7A, 0x5A3A2A, 0.7f}, "Shaped from river clay, with a word pressed into its forehead."},
    {"Golemark", T_STONE, T_ARCANE, 96, 92, 110, 34, 0, 0, 45, 170, LS(1, M_ROCKSLAM, 34, M_SIGIL, 38, M_QUAKE, 42, M_STONESKIN, 46, M_ANVIL, 50, M_QUAKE),
     {2, 0, 4, 0, 0, 2, 0x9A6A4A, 0xC88A6A, 0x3A2A1A, 1.45f}, "Guards the walls of Ur-Lumen. Has never been told to stop."},
    // 55-56 Bullwing
    {"Bullwing", T_BEAST, T_AIR, 58, 66, 56, 58, 36, 56, 120, 60, LS(1, M_TACKLE, 1, M_GUST, 7, M_GROWL, 12, M_WINGSLICE, 18, M_MAUL, 26, M_SQUALL),
     {1, 1, 4, 1, 1, 4, 0x8A6A4A, 0xC8A87A, 0xD8B040, 0.85f}, "A winged calf with a curled beard. Very solemn."},
    {"Wingbull", T_BEAST, T_AIR, 92, 100, 84, 74, 0, 0, 45, 170, LS(1, M_MAUL, 36, M_GALE, 40, M_HOWL, 44, M_QUAKE, 48, M_TAILWIND, 52, M_MAUL),
     {1, 1, 4, 1, 1, 4, 0x6A4A2A, 0xB89A6A, 0xE8C040, 1.45f}, "Stood at the city gates before the city did."},
    // 57-58 Reedsnake
    {"Reedsnake", T_WATER, T_FAE, 50, 58, 50, 62, 36, 58, 120, 60, LS(1, M_SPLASH, 1, M_THORN, 7, M_GLAMOUR, 12, M_BRINE, 18, M_BRAMBLE, 26, M_TIDE),
     {3, 1, 0, 0, 0, 0, 0x5A8A5A, 0xA8C88A, 0x2A4A3A, 0.8f}, "Hides in the canal reeds. Steals bathers' clothes, not their lives."},
    {"Deepwyrm", T_WATER, T_FAE, 88, 96, 80, 82, 0, 0, 45, 170, LS(1, M_TIDE, 36, M_FAIRYRING, 40, M_UNDERTOW, 44, M_MOONDEW, 48, M_FLOOD, 52, M_FAIRYRING),
     {3, 1, 4, 4, 0, 0, 0x3A6A5A, 0x8AC8A8, 0x1A3A2A, 1.4f}, "Sheds its skin and is young again. Every time."},
    // 59-61 legendaries
    {"Kidu", T_BEAST, T_SPIRIT, 110, 120, 96, 104, 0, 0, 3, 250, LS(1, M_MAUL, 1, M_GRAVETOLL, 1, M_HOWL, 1, M_SOULDRAIN, 50, M_DEVOUR, 60, M_MAUL),
     {1, 1, 4, 1, 0, 4, 0xC8A870, 0xF0E0C0, 0x8AA8E8, 1.5f}, "A wild-maned beast of the steppe. It died once, for a friend."},
    {"Veilwyrm", T_SPIRIT, T_WATER, 100, 110, 96, 104, 0, 0, 3, 250, LS(1, M_GRAVETOLL, 1, M_FLOOD, 1, M_FADE, 1, M_UNDERTOW, 60, M_SOULDRAIN, 60, M_FLOOD),
     {3, 1, 3, 4, 0, 0, 0x6A8AC8, 0xD8E8FF, 0x2A3A6A, 1.7f}, "Seen only at midnight off Wailing Rock, singing the Sisters' song."},
    {"Chronoth", T_ARCANE, T_IRON, 96, 112, 110, 96, 0, 0, 3, 250, LS(1, M_SIGIL, 1, M_ANVIL, 1, M_MINDLOCK, 1, M_BRACE, 60, M_FATE, 60, M_SIGIL),
     {2, 0, 5, 3, 2, 2, 0xB89A4A, 0xF0E0A0, 0x3A2A1A, 1.5f}, "Winds the clocks of Ur-Lumen. If it stops, so does the hour."},
};
const int N_SPECIES = sizeof(SPECIES) / sizeof(SPECIES[0]);

// --- creatures in play ----------------------------------------------------------------------
Mon g_party[PARTY];
int g_nParty = 0;
Mon g_box[BOX];
int g_nBox = 0;

int xpForLevel(int lvl) { return lvl * lvl * lvl * 4 / 5; }

MonStats statsOf(const Mon &m) {
  const Species &s = SPECIES[m.sp];
  MonStats o;
  int l = m.lvl;
  o.hp = s.hp * 2 * l / 100 + l + 10;
  o.atk = s.atk * 2 * l / 100 + 5;
  o.def = s.def * 2 * l / 100 + 5;
  o.spd = s.spd * 2 * l / 100 + 5;
  return o;
}

void monInit(Mon &m, int sp, int lvl, uint32_t seed) {
  memset(&m, 0, sizeof(m));
  m.sp = (uint8_t)sp;
  m.lvl = (uint8_t)lvl;
  m.seed = seed;
  m.xp = (uint16_t)(xpForLevel(lvl) > 65535 ? 65535 : xpForLevel(lvl));
  for (int i = 0; i < 4; i++) m.moves[i] = 255;
  // the last four moves it would know by now
  int n = 0;
  const Species &s = SPECIES[sp];
  for (int i = 0; i < 6; i++) {
    if (s.learn[i][0] > lvl) continue;
    uint8_t mv = s.learn[i][1];
    bool have = false;
    for (int k = 0; k < 4; k++) have = have || m.moves[k] == mv;
    if (have) continue;
    if (n < 4) m.moves[n++] = mv;
    else { m.moves[0] = m.moves[1]; m.moves[1] = m.moves[2]; m.moves[2] = m.moves[3]; m.moves[3] = mv; }
  }
  m.hp = (uint16_t)statsOf(m).hp;
}

// --- items ------------------------------------------------------------------------------------
const char *const ITEM_NAMES[I_COUNT] = {"Brass Lantern", "Silver Lantern", "Moonglass", "Tonic", "Strong Draught",
                                          "Smelling Salts", "Iced Bun"};
const char *const ITEM_DESC[I_COUNT] = {
    "Catches a weakened creature. A brass cage with a wick.",
    "A better lantern: catches more easily.",
    "Never fails. There are very few.",
    "Heals 30 HP.",
    "Heals a creature completely.",
    "Wakes a fainted creature with half its health.",
    "Street kids will do a lot for an iced bun.",
};
uint8_t g_items[I_COUNT];
int g_money = 0;

const char *const SKILL_NAMES[SK_COUNT] = {"Observation", "Logic", "Memory", "Charm", "Disguise", "Lore"};
const char *const SKILL_VOICE[SK_COUNT] = {"OBSERVATION", "LOGIC", "MEMORY", "CHARM", "DISGUISE", "LORE"};
int skill(int s) { return getVar(SKILL_NAMES[s]); }

// --- wild creatures and trainers' teams ---------------------------------------------------------
struct WildEntry { uint8_t sp, lo, hi, weight; };
static const WildEntry WILD1[] = {{9, 3, 5, 30}, {16, 3, 5, 30}, {14, 4, 6, 18}, {20, 4, 6, 10}, {11, 4, 6, 8}, {18, 4, 6, 4}};
static const WildEntry WILD2[] = {{24, 14, 17, 28}, {29, 14, 17, 24}, {27, 15, 17, 18}, {31, 15, 17, 16}, {33, 15, 18, 14}};
static const WildEntry WILD3[] = {{35, 20, 24, 24}, {37, 20, 24, 20}, {39, 21, 24, 18}, {41, 20, 23, 20}, {43, 21, 24, 18}};
static const WildEntry WILD4[] = {{45, 28, 32, 26}, {47, 28, 32, 26}, {49, 29, 32, 22}, {51, 29, 32, 18}, {18, 28, 31, 8}};
static const WildEntry WILD5[] = {{53, 36, 40, 26}, {55, 36, 40, 22}, {57, 37, 40, 22}, {12, 37, 40, 16}, {21, 37, 40, 14}};

int wildFor(int table, int &lvl) {
  const WildEntry *t = WILD1;
  int n = sizeof(WILD1) / sizeof(WILD1[0]);
  if (table == 2) { t = WILD2; n = 5; }
  if (table == 3) { t = WILD3; n = 5; }
  if (table == 4) { t = WILD4; n = 5; }
  if (table == 5) { t = WILD5; n = 5; }
  int tot = 0;
  for (int i = 0; i < n; i++) tot += t[i].weight;
  int r = (int)(rnd() % tot);
  for (int i = 0; i < n; i++) {
    if (r < t[i].weight) {
      int bump = table == 1 && getVar("tide_seal") ? 3 : 0;
      lvl = t[i].lo + (int)(rnd() % (t[i].hi - t[i].lo + 1)) + bump;
      return t[i].sp;
    }
    r -= t[i].weight;
  }
  lvl = 3;
  return 9;
}

} // namespace Gaslight
