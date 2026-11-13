"""Builds Gaslight/Towns.cpp: the maps of Sandmere, Greenwold, Skjold and
Ur-Lumen and their interiors, with their buildings, people and things to look
at. Checks that every door, person and spot can be reached, so a typo can't
leave a door in a wall.

usage: python make_towns.py ../Gaslight/Towns.cpp

Tiles are the same as Areas.cpp (see its header). Biomes: 1 desert,
2 forest, 3 fjord, 4 city.
"""
import sys

BLOCK = set("~^#LTbBNgwMPRotcKCFYXGZO")


class Map:
    def __init__(self, cid, name, w, d, base, indoor=0, biome=0, sky=(0, 0, 0), fog=(10, 28), enc=0):
        self.cid, self.name, self.w, self.d = cid, name, w, d
        self.g = [[base] * w for _ in range(d)]
        self.indoor, self.biome, self.sky, self.fog, self.enc = indoor, biome, sky, fog, enc
        self.b, self.ex, self.npc, self.spot = [], [], [], []

    def rect(self, x0, z0, x1, z1, ch):
        for z in range(z0, z1 + 1):
            for x in range(x0, x1 + 1):
                self.g[z][x] = ch

    def put(self, x, z, ch):
        self.g[z][x] = ch

    def border(self, ch):
        for x in range(self.w):
            self.g[0][x] = ch; self.g[self.d - 1][x] = ch
        for z in range(self.d):
            self.g[z][0] = ch; self.g[z][self.w - 1] = ch

    def building(self, x, z, w, d, h, wall, roof, doorX, dest, style, sign, path="."):
        self.rect(x, z, x + w - 1, z + d - 1, "#")
        if doorX >= 0:
            self.g[z + d][doorX] = path
        self.b.append((x, z, w, d, h, wall, roof, doorX, dest, style, sign))

    def person(self, look, x, z, face, script, show="", trainer=0):
        self.npc.append((look, x, z, face, script, show, trainer))

    def look(self, x, z, script, show="", glow=0):
        self.spot.append((x, z, script, show, glow))

    def check(self):
        def walk(x, z):
            if not (0 <= x < self.w and 0 <= z < self.d):
                return False
            return self.g[z][x] not in BLOCK
        problems = []
        for (x, z, w, d, h, wall, roof, doorX, dest, style, sign) in self.b:
            if doorX >= 0 and not walk(doorX, z + d):
                problems.append("door of %s blocked" % sign)
        for n in self.npc:
            if not walk(n[1], n[2]) and n[0] != "L_COUNT":
                problems.append("person %s stands on %r" % (n[4], self.g[n[2]][n[1]]))
        for s in self.spot:
            x, z = s[0], s[1]
            if not any(walk(x + dx, z + dz) for dx, dz in ((0, 1), (0, -1), (1, 0), (-1, 0))):
                problems.append("spot %s can't be reached" % s[2])
        for p in problems:
            print(self.name, ":", p)
        return not problems


def room(cid, name, w, d, furniture, indoor=1, sky=(0x1A1410, 0x2A2018, 0x1A1410)):
    m = Map(cid, name, w, d, "_", indoor=indoor, sky=sky, fog=(12, 30))
    m.border("^")
    m.put(w // 2 - 1 if w % 2 == 0 else w // 2, d - 1, "E")
    for (x, z, ch) in furniture:
        m.put(x, z, ch)
    return m


M = []

# ======================================================================== Sandmere
s = Map("A_SAND", "Sandmere", 30, 22, ":", biome=1, sky=(0x5A7AA8, 0xF0D8A8, 0xD8C090), fog=(12, 34), enc=2)
s.border("w")
s.rect(1, 6, 28, 7, ".")      # the Avenue of Scales
s.rect(13, 6, 16, 20, ".")    # the processional way south
s.rect(1, 13, 28, 13, ".")    # the bazaar street
s.building(10, 1, 10, 5, 3.6, 0xD8C8A0, 0xB89A60, 14, "A_MUSEUM", 7, "The Hall of Weighing")
s.building(2, 8, 5, 4, 2.6, 0xD8B890, 0xC8A070, 4, "A_INN", 6, "The Oasis Inn")
s.building(23, 8, 5, 4, 2.6, 0xC8A880, 0xB08860, 25, "A_KAMAL", 6, "House of Kamal Idris")
s.building(2, 15, 5, 4, 2.4, 0xD0B088, 0xA88058, 4, "A_SEFA", 6, "Okon Restorations")
s.building(22, 15, 6, 4, 3.0, 0xB89870, 0x8A6A48, 24, "A_GLASS", 6, "The Glassworks")
for z in (6, 7, 13):
    s.put(4, 12, "."); s.put(25, 12, "."); s.put(4, 19, "."); s.put(24, 19, ".")
s.rect(14, 9, 15, 10, "M")   # the Sphinx of Sandmere
s.rect(8, 16, 11, 19, "~")   # the oasis
for (x, z) in ((7, 15), (12, 16), (7, 20), (12, 20), (3, 2), (25, 2), (8, 3), (21, 3), (27, 19)):
    s.put(x, z, "T")
s.rect(17, 15, 20, 20, "v")  # dry scrub: wild creatures
for (x, z) in ((5, 6), (12, 6), (18, 6), (25, 6), (5, 13), (11, 13), (18, 13), (26, 13), (14, 19)):
    s.put(x, z, "L")
for (x, z) in ((9, 12), (10, 12), (19, 12), (20, 12), (9, 14), (20, 14)):
    s.put(x, z, "b")
s.person("L_TALLY", 17, 12, 2, "tally")
s.person("L_CONDUCTOR", 27, 14, 3, "travel")
s.person("L_NEFERET", 12, 11, 2, "neferet")
s.person("L_TOMAS", 16, 11, 3, "tomas")
s.person("L_MERCHANT", 8, 13, 2, "sand_merchant")
s.person("L_TRAINER_A", 18, 16, 2, "tr_sand1", trainer=1)
s.person("L_TRAINER_B", 20, 19, 3, "tr_sand2", trainer=1)
s.person("L_JUNO", 21, 7, 3, "juno_sand", "juno_sand_show")
s.person("L_MERCHANT", 6, 7, 2, "crowd_s")
s.look(14, 10, "sphinx"); s.look(15, 10, "sphinx")
s.look(12, 18, "frag_sand1", "!frag_sand1", 1)
s.look(26, 20, "frag_sand2", "!frag_sand2", 1)
M.append(s)

mu = room("A_MUSEUM", "The Hall of Weighing", 14, 9,
          [(2, 2, "O"), (5, 2, "O"), (8, 2, "O"), (11, 2, "O"), (2, 5, "O"), (11, 5, "O"),
           (6, 1, "Z"), (7, 1, "Z"), (3, 4, "C"), (10, 4, "C"), (1, 1, "K"), (12, 1, "K")])
mu.person("L_ASSISTANT", 9, 5, 2, "museum_guard")
mu.look(6, 1, "sarcophagus"); mu.look(7, 1, "sarcophagus")
mu.look(1, 1, "vent")
mu.look(10, 4, "feather_case")
mu.look(3, 4, "frag_sand3", "!frag_sand3", 1)
M.append(mu)
ka = room("A_KAMAL", "House of Kamal Idris", 10, 7, [(1, 1, "K"), (2, 1, "K"), (5, 1, "G"), (8, 1, "Y"), (3, 3, "t"), (4, 3, "c")])
ka.person("L_KAMAL", 6, 3, 2, "kamal")
ka.look(5, 1, "kamal_desk")
M.append(ka)
se = room("A_SEFA", "Okon Restorations", 10, 7, [(1, 1, "K"), (2, 1, "t"), (3, 1, "t"), (6, 1, "G"), (8, 1, "X"), (8, 4, "t")])
se.person("L_SEFA", 4, 3, 2, "sefa")
se.look(6, 1, "sefa_bench")
se.look(8, 1, "sefa_barrel")
M.append(se)
gl = room("A_GLASS", "The Glassworks", 12, 9, [(1, 1, "F"), (10, 1, "F"), (5, 1, "C"), (6, 1, "C"), (1, 7, "X"), (10, 7, "X")])
gl.rect(3, 3, 8, 5, "=")
gl.person("L_IMANI", 6, 2, 2, "imani")
gl.look(9, 2, "frag_sand4", "!frag_sand4", 1)
M.append(gl)
inn = room("A_INN", "The Inn", 10, 7, [(1, 1, "C"), (2, 1, "C"), (3, 1, "C"), (8, 1, "Y"), (6, 3, "t"), (7, 3, "c"), (2, 4, "t"), (3, 4, "c")])
for t, look in ((2, "L_INNKEEP_S"), (3, "L_INNKEEP_G"), (4, "L_INNKEEP_K"), (5, "L_INNKEEP_U")):
    inn.person(look, 2, 2, 2, "innkeeper", "=%d" % t)
M.append(inn)

# ======================================================================== Greenwold
g = Map("A_GREEN", "Greenwold", 32, 22, ",", biome=2, sky=(0x4A6A5A, 0xC8D8B0, 0x7A9A80), fog=(9, 26), enc=3)
g.border("T")
g.rect(1, 7, 30, 7, ".")
g.rect(15, 7, 16, 20, ".")
g.rect(1, 14, 30, 14, ".")
g.building(11, 1, 10, 5, 3.2, 0x8A7050, 0x2E4A2E, 15, "A_HALL", 0, "Hautdesert Lodge")
g.building(2, 9, 5, 4, 2.4, 0x9A8060, 0x5A3A2A, 4, "A_INN", 0, "The Holly Bush Inn")
g.building(25, 9, 4, 3, 2.0, 0x6A5A3A, 0x3A4A2A, 26, "A_HUT", 0, "Morgana's Hut")
g.building(23, 16, 6, 4, 2.6, 0x7A6A4A, 0x2A3A2A, 25, "A_IVY", 0, "The Gamekeeper's Lodge")
for x, z in ((4, 13), (26, 12), (25, 20)):
    g.put(x, z, ".")
g.rect(6, 17, 9, 19, "~")    # the mere
g.rect(1, 1, 8, 5, "v")      # the old wood: wild creatures
g.rect(22, 1, 30, 5, "v")
for (x, z) in ((3, 3), (6, 2), (9, 4), (24, 3), (28, 2), (12, 10), (20, 10), (11, 17), (20, 18), (29, 11), (1, 18), (13, 19), (18, 16)):
    g.put(x, z, "T")
g.rect(2, 17, 3, 18, "M")    # the Green Chapel's mound
for (x, z) in ((10, 7), (21, 7), (8, 14), (22, 14), (15, 11)):
    g.put(x, z, "L")
g.person("L_MOTH", 13, 9, 2, "moth")
g.person("L_CONDUCTOR", 29, 15, 3, "travel")
g.person("L_BERTRAM", 17, 9, 2, "bertram")
g.person("L_GREENKNIGHT", 5, 19, 1, "greenknight")
g.person("L_GARETH", 28, 4, 2, "gareth", "gareth_found")
g.person("L_TRAINER_C", 5, 4, 2, "tr_green1", trainer=1)
g.person("L_TRAINER_A", 24, 2, 3, "tr_green2", trainer=1)
g.person("L_JUNO", 18, 13, 3, "juno_green", "juno_green_show")
g.person("L_VILLAGER", 20, 8, 2, "crowd_g")
g.look(2, 18, "green_chapel"); g.look(3, 18, "green_chapel")
g.look(10, 18, "frag_green1", "!frag_green1", 1)
g.look(29, 3, "frag_green2", "!frag_green2", 1)
g.look(7, 3, "herb1", "!herb1"); g.look(27, 5, "herb2", "!herb2"); g.look(12, 20, "herb3", "!herb3")
M.append(g)

ha = room("A_HALL", "Hautdesert Lodge", 14, 9, [(1, 1, "F"), (12, 1, "F"), (4, 3, "t"), (5, 3, "t"), (6, 3, "t"), (7, 3, "t"),
                                               (8, 3, "t"), (9, 3, "t"), (4, 4, "c"), (6, 4, "c"), (8, 4, "c"), (1, 6, "X"), (12, 6, "K")])
ha.person("L_ROSALIND", 3, 2, 2, "rosalind")
ha.person("L_BERTRAM", 10, 2, 2, "bertram_hall")
ha.look(12, 6, "trade_ledger")
ha.look(1, 6, "costume_chest")
ha.look(11, 4, "frag_green3", "!frag_green3", 1)
M.append(ha)
hu = room("A_HUT", "Morgana's Hut", 8, 6, [(1, 1, "F"), (3, 1, "K"), (6, 1, "X"), (5, 3, "t")])
hu.person("L_MORGANA", 3, 3, 2, "morgana")
M.append(hu)
iv = room("A_IVY", "The Gamekeeper's Lodge", 12, 9, [(1, 1, "K"), (10, 1, "K"), (1, 7, "X"), (10, 7, "X")])
iv.rect(3, 3, 8, 5, "=")
iv.person("L_IVY", 6, 2, 2, "ivy")
iv.look(9, 6, "frag_green4", "!frag_green4", 1)
M.append(iv)

# ======================================================================== Skjold
k = Map("A_SKJOLD", "Skjold", 32, 22, ",", biome=3, sky=(0x2A3A5A, 0xC8D8E8, 0x8AA0B8), fog=(9, 26), enc=4)
k.rect(0, 0, 31, 3, "~")     # the fjord
k.rect(0, 4, 31, 4, "=")     # the quay
k.rect(12, 0, 13, 3, "=")    # the long jetty
k.rect(0, 5, 0, 21, "^"); k.rect(31, 5, 31, 21, "^"); k.rect(0, 21, 31, 21, "^")
k.rect(1, 9, 30, 9, ".")
k.rect(15, 5, 16, 20, ".")
k.building(3, 5, 8, 3, 2.2, 0x6A5040, 0x4A3A30, 6, "A_INN", 8, "The Mead Hall")
k.building(20, 5, 7, 3, 2.2, 0x5A4A3A, 0x3A3028, 23, "A_SKALD", 8, "The Skald's Hall")
k.building(2, 12, 5, 4, 2.0, 0x6A5A4A, 0x3A3A3A, 4, "A_RUNEHUT", 8, "Asta's Workshop")
k.building(23, 12, 6, 4, 2.4, 0x4A4A4A, 0x2A2A2E, 25, "A_FORGE", 8, "The Forge")
for x, z in ((6, 8), (23, 8), (4, 16), (25, 16)):
    k.put(x, z, ".")
k.rect(15, 12, 16, 13, "M")  # the ash tree of the Norns
k.rect(18, 15, 27, 20, "v")  # snowfield: wild creatures
k.rect(1, 17, 4, 20, "^")    # the cliff with the sea cave
for (x, z) in ((10, 12), (12, 18), (8, 19), (19, 11), (29, 10)):
    k.put(x, z, "T")
for (x, z) in ((5, 9), (11, 9), (20, 9), (27, 9), (14, 15)):
    k.put(x, z, "L")
k.person("L_SIGGA", 13, 10, 2, "sigga")
k.person("L_CONDUCTOR", 29, 8, 3, "travel")
k.person("L_ASTA", 7, 10, 2, "asta_out")
k.person("L_VILLAGER", 18, 10, 2, "forgetful")
k.person("L_JUNO", 14, 6, 2, "juno_skjold", "juno_skjold_show")
k.person("L_VILLAGER", 24, 10, 2, "crowd_k")
k.person("L_BRAM", 12, 3, 2, "bram_jetty", "bram_on_jetty")
k.person("L_TRAINER_B", 20, 16, 2, "tr_skjold1", trainer=1)
k.person("L_TRAINER_C", 26, 19, 3, "tr_skjold2", trainer=1)
k.look(15, 13, "norns_tree"); k.look(16, 13, "norns_tree")
k.look(5, 17, "sea_cave")
k.look(9, 20, "frag_skjold1", "!frag_skjold1", 1)
k.look(28, 18, "frag_skjold2", "!frag_skjold2", 1)
M.append(k)
sk = room("A_SKALD", "The Skald's Hall", 12, 7, [(1, 1, "F"), (10, 1, "K"), (4, 2, "t"), (5, 2, "t"), (6, 2, "t"), (4, 3, "c"), (6, 3, "c")])
sk.person("L_EIRIK", 8, 3, 2, "eirik")
sk.look(10, 1, "saga_shelf")
M.append(sk)
ru = room("A_RUNEHUT", "Asta's Workshop", 10, 7, [(1, 1, "K"), (5, 1, "G"), (8, 1, "X"), (3, 3, "t")])
ru.person("L_ASTA", 6, 3, 2, "asta", "asta_home")
ru.look(5, 1, "rune_cage")
ru.look(8, 4, "frag_skjold3", "!frag_skjold3", 1)
M.append(ru)
fo = room("A_FORGE", "The Forge", 12, 9, [(1, 1, "F"), (2, 1, "F"), (9, 1, "X"), (10, 1, "K")])
fo.rect(3, 3, 8, 5, "=")
fo.person("L_HILDE", 6, 2, 2, "hilde")
M.append(fo)
sc = Map("A_SEACAVE", "The Sea Cave", 14, 9, ":", indoor=2, sky=(0x06080C, 0x0E1218, 0x0B0F14), fog=(7, 20))
sc.border("^")
sc.rect(1, 1, 6, 3, "~")
sc.put(7, 8, "E")
sc.ex.append((7, 8, "A_SKJOLD", 5, 18, 2))  # out to the cleft in the cliff
sc.person("L_RAVEN", 10, 3, 2, "muninn_cage")
sc.person("L_BRAM", 9, 5, 2, "bram_cave", "bram_in_cave")
sc.look(3, 5, "frag_skjold4", "!frag_skjold4", 1)
M.append(sc)

# ======================================================================== Ur-Lumen
u = Map("A_UR", "Ur-Lumen", 34, 24, ".", biome=4, sky=(0x2A1A3A, 0xE8A870, 0x8A6A7A), fog=(10, 30), enc=5)
u.border("w")
u.rect(1, 12, 32, 12, "~")   # the canal
u.rect(16, 12, 17, 12, "=")  # its bridge
u.rect(2, 15, 9, 21, ",")    # the gardens
u.rect(3, 17, 8, 20, "v")
u.building(11, 1, 12, 6, 4.0, 0xC8A070, 0x8A5A3A, 16, "A_ZIGGURAT", 9, "The Museum of Last Things")
u.building(2, 4, 6, 4, 2.6, 0xB89878, 0x7A5A3A, 4, "A_SIDURI", 6, "Siduri's Alehouse")
u.building(25, 15, 6, 4, 2.8, 0xC0A080, 0x8A6A48, 27, "A_INN", 6, "The Gate Inn")
u.put(16, 22, "."); u.put(17, 22, ".")
u.rect(15, 21, 15, 22, "M"); u.rect(18, 21, 18, 22, "M")  # the winged bulls at the gate
for (x, z) in ((3, 15), (9, 16), (5, 21), (24, 3), (28, 5), (10, 9), (23, 9)):
    u.put(x, z, "T")
for (x, z) in ((14, 8), (19, 8), (14, 15), (19, 15), (8, 10), (26, 10), (12, 19), (21, 19)):
    u.put(x, z, "L")
u.person("L_CONDUCTOR", 20, 20, 3, "travel")
u.person("L_JUNO", 17, 14, 2, "juno_ur", "juno_allied")
u.person("L_CURATOR", 16, 8, 2, "gate_curator", "!ziggurat_open")
u.person("L_TRAINER_A", 6, 18, 1, "tr_ur1", trainer=1)
u.person("L_INNKEEP_U", 26, 8, 2, "crowd_u")
u.look(15, 22, "lamassu"); u.look(18, 22, "lamassu")
u.look(30, 3, "frag_ur1", "!frag_ur1", 1)
u.look(4, 20, "frag_ur2", "!frag_ur2", 1)
M.append(u)
zi = room("A_ZIGGURAT", "The Museum of Last Things", 14, 12,
          [(2, 2, "O"), (11, 2, "O"), (2, 6, "O"), (11, 6, "O"), (5, 1, "C"), (8, 1, "C"), (1, 9, "Z"), (12, 9, "Z")])
zi.put(7, 1, "S")
zi.person("L_CURATOR", 4, 8, 1, "curator1", "!beat_curator1", 1)
zi.person("L_CURATOR", 10, 6, 3, "curator2", "!beat_curator2", 1)
zi.person("L_CURATOR", 3, 4, 1, "curator3", "!beat_curator3", 1)
zi.person("L_CURATOR", 9, 3, 2, "curator4", "!beat_curator4", 1)
zi.look(5, 1, "exhibit_song"); zi.look(8, 1, "exhibit_feather")
zi.look(12, 5, "frag_ur3", "!frag_ur3", 1)
zi.ex.append((7, 1, "A_SANCTUM", 6, 7, 0))
M.append(zi)
sa = Map("A_SANCTUM", "The Sanctum", 12, 9, "_", indoor=2, sky=(0x0A0610, 0x1A1028, 0x120A1A), fog=(8, 22))
sa.border("^")
sa.rect(4, 1, 7, 2, "~")     # the pool where the plant grows
sa.put(5, 8, "E")
sa.ex.append((5, 8, "A_ZIGGURAT", 7, 2, 2))  # back down the stair
sa.person("L_VANEMASK", 6, 3, 2, "collector")
sa.look(4, 3, "frag_ur4", "!frag_ur4", 1)
M.append(sa)
si = room("A_SIDURI", "Siduri's Alehouse", 10, 7, [(1, 1, "C"), (2, 1, "C"), (3, 1, "C"), (7, 1, "X"), (8, 1, "X"), (5, 3, "t"), (6, 3, "c")])
si.person("L_SIDURI", 2, 2, 2, "siduri")
M.append(si)

# ------------------------------------------------------------------------- write
def cstr(row):
    return '"' + "".join(row).replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(out):
    ok = all(m.check() for m in M)
    if not ok:
        sys.exit(1)
    L = ["// Gaslight & Griffins - Sandmere, Greenwold, Skjold and Ur-Lumen.",
         "// Generated by tools/make_towns.py - edit that, not this.",
         '#include "World.h"', "", "namespace Gaslight {", ""]
    for m in M:
        L.append("static const char *const MAP_%s[] = {" % m.cid)
        for row in m.g:
            L.append("    " + cstr(row) + ",")
        L.append("};")
        if m.b:
            L.append("static const Building BLD_%s[] = {" % m.cid)
            for (x, z, w, d, h, wall, roof, doorX, dest, style, sign) in m.b:
                L.append('    {%d, %d, %d, %d, %.2ff, 0x%06X, 0x%06X, %d, %s, %d, "%s"},' % (x, z, w, d, h, wall, roof, doorX, dest, style, sign))
            L.append("};")
        if m.ex:
            L.append("static const Exit EX_%s[] = {" % m.cid)
            for (x, z, dest, tx, tz, face) in m.ex:
                L.append("    {%d, %d, %s, %d, %d, %d}," % (x, z, dest, tx, tz, face))
            L.append("};")
    L.append("")
    L.append("// in AreaId order, from A_SAND on")
    L.append("const AreaDef AREAS_MORE[] = {")
    for m in M:
        sky0, sky1, fog = m.sky
        L.append('    {"%s", %d, %d, MAP_%s, %s, %d, %s, %d, %d, 0x%06X, 0x%06X, 0x%06X, %d, %d, %d, %d},' % (
            m.name, m.w, m.d, m.cid, "BLD_" + m.cid if m.b else "nullptr", len(m.b),
            "EX_" + m.cid if m.ex else "nullptr", len(m.ex), m.indoor, sky0, sky1, fog, m.fog[0], m.fog[1], m.enc,
            m.biome if not m.indoor else (M[0].biome if False else 0)))
    L.append("};")
    L.append("const int N_AREAS_MORE = sizeof(AREAS_MORE) / sizeof(AREAS_MORE[0]);")
    L.append("")
    L.append("const NpcDef NPCS_MORE[] = {")
    for m in M:
        for (look, x, z, face, script, show, trainer) in m.npc:
            L.append('    {%s, %s, %d, %d, %d, "%s", "%s", 0, %d},' % (m.cid, look, x, z, face, script, show, trainer))
    L.append("};")
    L.append("const int N_NPCS_MORE = sizeof(NPCS_MORE) / sizeof(NPCS_MORE[0]);")
    L.append("")
    L.append("const SpotDef SPOTS_MORE[] = {")
    for m in M:
        for (x, z, script, show, glow) in m.spot:
            L.append('    {%s, %d, %d, "%s", "%s", %d},' % (m.cid, x, z, script, show, glow))
    L.append("};")
    L.append("const int N_SPOTS_MORE = sizeof(SPOTS_MORE) / sizeof(SPOTS_MORE[0]);")
    L.append("")
    L.append("} // namespace Gaslight")
    open(out, "w", newline="\n").write("\n".join(L) + "\n")
    print("areas:", [m.cid for m in M])


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "../Gaslight/Towns.cpp")
