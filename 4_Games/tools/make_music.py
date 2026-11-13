"""Gaslight & Griffins - the score. Turns each piece (a melody in MML, a chord
chart and an accompaniment style) into compact note tables for the console's
tracker (Gaslight/Tune.cpp). Every part is checked to be exactly as long as
the chord chart, so the loops never drift.

usage: python make_music.py ../Gaslight/GLMusic.h

MML for melodies: o<n> octave, < > octave down/up, l<n> default length,
notes c d e f g a b with + or # (sharp) or - (flat), a length (1 2 4 8 16)
and an optional dot, r for a rest, ^<len> to tie more length onto the last note.
"""
import re
import sys

TPQ = 12                          # ticks per quarter note
EIGHTH = TPQ // 2

# instruments (must match Tune.cpp)
FLUTE, MBOX, HARP, PAD, BASS, REED, BELL, PIZZ, KICKS, HATS, LEAD, STRINGS, OUD = range(13)

NOTE = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def mml(src):
    """Parse a melody into [(midi or 0 for rest, ticks)]."""
    out = []
    octave, deflen = 5, 8
    i = 0
    s = src.replace("|", " ")

    def length(j):
        m = re.match(r"(\d+)(\.?)", s[j:])
        if not m:
            ln, dot, j2 = deflen, False, j
        else:
            ln, dot, j2 = int(m.group(1)), m.group(2) == ".", j + len(m.group(0))
        t = TPQ * 4 // ln
        if dot:
            t += t // 2
        return t, j2

    while i < len(s):
        c = s[i]
        if c.isspace():
            i += 1
        elif c == "o":
            m = re.match(r"o(\d)", s[i:]); octave = int(m.group(1)); i += 2
        elif c == "<":
            octave -= 1; i += 1
        elif c == ">":
            octave += 1; i += 1
        elif c == "l":
            m = re.match(r"l(\d+)", s[i:]); deflen = int(m.group(1)); i += len(m.group(0))
        elif c == "r":
            t, i = length(i + 1)
            out.append([0, t])
        elif c == "^":
            t, i = length(i + 1)
            out[-1][1] += t
        elif c in NOTE:
            n = NOTE[c]; i += 1
            while i < len(s) and s[i] in "+#-":
                n += 1 if s[i] in "+#" else -1; i += 1
            t, i = length(i)
            out.append([12 * (octave + 1) + n, t])
        else:
            raise ValueError("bad MML at %r" % s[i:i + 12])
    return out


# --- chords --------------------------------------------------------------------------------
QUAL = {"": (0, 4, 7), "m": (0, 3, 7), "7": (0, 4, 7, 10), "m7": (0, 3, 7, 10), "dim": (0, 3, 6),
        "aug": (0, 4, 8), "sus": (0, 5, 7), "maj7": (0, 4, 7, 11)}


def chord(name):
    m = re.match(r"([A-G])([#b]?)(.*)", name)
    pc = NOTE[m.group(1).lower()] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)
    return pc % 12, QUAL[m.group(3)]


def tone(pc, ivs, k, base):
    """The k-th chord tone (0 root, 1 third, 2 fifth, 3 octave...) at or above midi `base`."""
    octs, idx = divmod(k, len(ivs) if len(ivs) == 3 else 3)
    iv = ivs[idx] + 12 * octs
    n = pc + iv
    while n < base:
        n += 12
    return n


def near(pc, ivs, target):
    """The chord tone nearest a midi note (for smooth pad voices)."""
    best = None
    for o in range(1, 9):
        for iv in ivs:
            n = 12 * o + pc + iv
            if best is None or abs(n - target) < abs(best - target):
                best = n
    return best


# --- accompaniment styles: each returns [(midi, ticks)] for one bar ---------------------------------
def bar_events(style, pc, ivs, bar, state):
    E = EIGHTH
    root2 = 36 + (pc - 0) % 12             # bass root in octave 2
    if root2 > 45: root2 -= 12
    fifth2 = root2 + 7
    if style == "bass_whole":  return [(root2, bar)]
    if style == "bass_half":   return [(root2, bar // 2), (fifth2, bar // 2)]
    if style == "bass_68":     return [(root2, 3 * E), (fifth2, 3 * E)]
    if style == "bass_waltz":  return [(root2, 2 * E), (0, E), (fifth2, 2 * E), (0, E)]
    if style == "bass_8":      return [(root2 if i % 2 == 0 else root2 + 12, E) for i in range(bar // E)]
    if style == "bass_drive":  return [(root2, E)] * (bar // E)
    if style == "bass_walk":
        steps = [root2, fifth2, root2 + 12, fifth2]
        return [(n, 2 * E) for n in steps[: bar // (2 * E)]]
    if style == "arp_68":
        ks = [0, 2, 3, 4, 3, 2]
        return [(tone(pc, ivs, k, 55), E) for k in ks]
    if style == "arp_8":
        ks = [0, 2, 3, 4, 5, 4, 3, 2]
        return [(tone(pc, ivs, k, 53), E) for k in ks[: bar // E]]
    if style == "arp_q":
        ks = [0, 1, 2, 3, 2, 1]
        return [(tone(pc, ivs, k, 55), 2 * E) for k in ks[: bar // (2 * E)]]
    if style == "oud":
        r = tone(pc, ivs, 0, 52)
        pat = [r, 0, r + 12, r, 0, r + 7, r + 12, r + 7]
        return [(n, E) for n in pat[: bar // E]]
    if style.startswith("pad"):            # pad0 / pad1 / pad2: one voice of the chord, held
        v = int(style[3])
        target = state.get(style, [57, 62, 66][v])
        n = near(pc, ivs, target)
        state[style] = n
        return [(n, bar)]
    if style.startswith("waltz"):          # waltz0/1: the chord on beats two and three
        v = int(style[5])
        n = near(pc, ivs, state.get(style, [60, 64][v]))
        state[style] = n
        return [(0, 2 * E), (n, 2 * E), (n, 2 * E)]
    if style.startswith("stab"):           # stab0/1: off-beat chord stabs
        v = int(style[4])
        n = near(pc, ivs, state.get(style, [62, 66][v]))
        state[style] = n
        return [(0, E), (n, E)] * (bar // (2 * E))
    if style.startswith("bellroot"):       # a bell on the root each bar
        return [(tone(pc, ivs, 0, 72), bar)]
    raise ValueError(style)


def drums(pattern, bar):
    """A drum line from a string per bar: K kick, S snare, H hat, O open hat, T tick, D hand drum, . rest;
    each character is one sixteenth."""
    m = {"K": 36, "S": 38, "H": 42, "O": 46, "T": 37, "D": 39}
    step = TPQ // 4
    ev = []
    for c in pattern:
        if c == ".":
            if ev: ev[-1][1] += step
            else: ev.append([0, step])
        else:
            ev.append([m[c], step])
    assert sum(t for _, t in ev) == bar, ("drum bar length", pattern)
    return ev


# --- the pieces -------------------------------------------------------------------------------
SONGS = []


def song(name, bpm, meter, chords, melody, parts, drum=None, loop=True, mel=(FLUTE, 11)):
    bar = {"44": 4 * TPQ, "34": 3 * TPQ, "68": 6 * EIGHTH}[meter]
    total = bar * len(chords)
    chs = []
    m = mml(melody)
    got = sum(t for _, t in m)
    assert got == total, "%s melody is %d ticks, chart is %d (%.2f bars)" % (name, got, total, got / bar)
    chs.append((mel[0], mel[1], m))
    for style, inst, vol in parts:
        state, ev = {}, []
        for c in chords:
            halves = c.split("/")
            for h in halves:
                pc, ivs = chord(h)
                ev += [list(e) for e in bar_events(style, pc, ivs, bar // len(halves), state)]
        chs.append((inst, vol, ev))
    if drum:
        for line in drum:   # one or more drum lines, each a list of per-bar patterns (cycled)
            pats, vol = line
            ev = []
            for b in range(len(chords)):
                ev += drums(pats[b % len(pats)], bar)
            chs.append((KICKS if "K" in "".join(pats) or "S" in "".join(pats) or "D" in "".join(pats) else HATS, vol, ev))
    for inst, vol, ev in chs:
        assert sum(t for _, t in ev) == total, (name, inst)
    assert len(chs) <= 8, name
    SONGS.append((name, bpm, loop, chs))


# 1. Fogmouth - a sea shanty in D minor, accordion over a harp, 6/8
song("fogmouth", 98, "68",
     "Dm Dm Gm A Dm C F F Bb F Gm A Dm Gm A Dm".split(),
     "o5 d8 e8 f8 a4 g8 | o5 f8 e8 d8 o4 a4. | o4 g8 a8 b-8 o5 d4 c8 | o4 b-8 a8 g8 a4. |"
     "o5 d8 e8 f8 a4 o6 d8 | o6 c8 o5 b-8 a8 g4. | o5 a8 g8 f8 c4. | o5 f4. e8 f8 g8 |"
     "o5 b-4. a8 b-8 o6 c8 | o6 d4 c8 o5 a4. | o5 b-8 a8 g8 d8 e8 f8 | o5 e4. c+4. |"
     "o5 d8 f8 a8 o6 d4 c8 | o5 b-8 a8 g8 b-4 a8 | o5 g8 f8 e8 c+4 e8 | o5 d4. r4.",
     [("bass_68", BASS, 10), ("arp_68", HARP, 8), ("pad0", PAD, 4), ("pad1", PAD, 4)],
     drum=[(["T.....T.....", "T.....T.....", "T.....T.....", "T..T..T....."], 3)],
     mel=(REED, 10))

# 2. Sandmere - E phrygian dominant, flute over an oud and a hand drum
song("sandmere", 92, "44",
     "E E F E E Am F E Dm E F E Am G F E".split(),
     "o5 e4. f8 g+4 f8 e8 | o5 f8 g+8 a8 g+8 f4 e4 | o5 a4. b8 o6 c4 o5 b8 a8 | o5 g+2 r4 e8 f8 |"
     "o5 g+4. a8 b4 a8 g+8 | o5 a8 b8 o6 c8 o5 b8 a4 e4 | o5 f8 g+8 a8 g+8 f4. e8 | o5 e2. r4 |"
     "o5 d8 e8 f8 a8 o6 d4. c8 | o5 b8 a8 g+8 f8 e4 g+4 | o5 a8 g+8 a8 b8 o6 c4 d4 | o6 e2 r4 o5 b8 o6 c8 |"
     "o6 c8 o5 b8 a8 g+8 a4 e4 | o5 d8 e8 f8 g8 b4 a8 g8 | o5 a4 g+8 f8 e4 f4 | o5 e2. r4",
     [("bass_half", BASS, 9), ("oud", OUD, 8), ("pad0", PAD, 3)],
     drum=[(["D...T.T.D.T.T...", "D...T.T.D..DT.T."], 6)],
     mel=(FLUTE, 11))

# 3. Greenwold - a G major waltz, recorder over harp and pizzicato
song("greenwold", 132, "34",
     "G G C G Em C D D G G C G Em Am D G".split(),
     "o5 d4 g4 a4 | o5 b4. a8 g4 | o5 e4 g4 o6 c4 | o5 b2 d4 |"
     "o5 e4 g4 b4 | o6 c4. o5 b8 a4 | o5 f+4 a4 o6 d4 | o5 a2. |"
     "o5 b4 o6 d4 c4 | o5 b4. a8 g4 | o5 g4 e4 o6 c4 | o5 b2 g4 |"
     "o5 g4 b4 e4 | o5 a4. b8 o6 c4 | o5 b4 a4 f+4 | o5 g2.",
     [("bass_waltz", PIZZ, 10), ("waltz0", HARP, 7), ("waltz1", HARP, 7), ("pad0", PAD, 3)],
     mel=(FLUTE, 11))

# 4. Skjold - A minor, slow and wide: strings, a bell, a drone
song("skjold", 76, "44",
     "Am Am G G F F Em Em Am Am G G F G Am Am".split(),
     "o5 a2 e4 g4 | o5 a4 b4 o6 c2 | o5 b2 g4 d4 | o5 g1 |"
     "o5 a2 f4 c4 | o5 a4 b4 o6 c4 d4 | o5 b2. g4 | o5 e1 |"
     "o6 e2 d4 c4 | o5 b4 a4 b4 o6 c4 | o6 d2 o5 b4 g4 | o5 a4 b4 g2 |"
     "o5 a2 o6 c4 o5 a4 | o5 g4 a4 b4 d4 | o5 e2 a2 | o5 a1",
     [("bass_whole", BASS, 8), ("arp_q", HARP, 6), ("pad0", PAD, 5), ("pad1", PAD, 5), ("pad2", PAD, 4), ("bellroot", BELL, 4)],
     mel=(STRINGS, 10))

# 5. Ur-Lumen - C harmonic minor, grand and strange: strings, bells, pizzicato
song("urlumen", 84, "44",
     "Cm Cm Ab G Cm Fm G G Ab Ab Fm G Cm Fm G Cm".split(),
     "o5 c4 e-4 g4. f8 | o5 e-8 d8 c8 d8 e-2 | o5 c4 e-4 a-4. g8 | o5 f8 e-8 d8 e-8 o4 b2 |"
     "o5 c4 g4 o6 c4. o5 b-8 | o5 a-8 g8 f8 e-8 f2 | o5 d4 g4 o4 b4 o5 d4 | o5 g2. r4 |"
     "o5 a-4. g8 f4 e-4 | o5 c4 e-4 a-2 | o5 a-4. g8 f4 a-4 | o5 g2 b4 o6 d4 |"
     "o6 e-4. d8 c4 o5 g4 | o5 a-4 g4 f4 a-4 | o5 g4 f4 e-4 d4 | o5 c2. r4",
     [("bass_half", BASS, 9), ("arp_8", PIZZ, 7), ("pad0", PAD, 4), ("pad1", PAD, 4), ("bellroot", BELL, 5)],
     drum=[(["K.......K.......", "K.......K...K..."], 4)],
     mel=(STRINGS, 10))

# 6. The Wilds - F major, bright and walking
song("wilds", 118, "44",
     "F C Dm Bb F C Bb C Dm Bb F C Dm Bb C F".split(),
     "o5 c8 f8 a8 o6 c8 o5 a4 f4 | o5 g8 e8 c8 e8 g2 | o5 a8 f8 d8 f8 a4 o6 d4 | o6 c4 o5 b-8 a8 b-2 |"
     "o5 c8 f8 a8 o6 c8 f4 e4 | o6 d8 c8 o5 b-8 a8 g2 | o5 b-8 a8 g8 a8 b-4 o6 d4 | o6 c2. r4 |"
     "o5 d4 f4 a4 o6 d4 | o6 d8 c8 o5 b-8 a8 b-2 | o5 a4 o6 c4 f4 e4 | o6 e8 d8 c8 o5 b-8 g2 |"
     "o5 f4 a4 o6 d4 c4 | o5 b-4 o6 d4 f4 d4 | o6 e8 d8 c8 d8 e4 g4 | o6 f2. r4",
     [("bass_8", BASS, 8), ("stab0", PIZZ, 6), ("stab1", PIZZ, 6), ("pad0", PAD, 3)],
     drum=[(["K.......S.......", "K.....K.S......."], 5), (["H.H.H.H.H.H.H.H."], 3)],
     mel=(LEAD, 9))

# 7. Battle - A minor, driving
song("battle", 152, "44",
     "Am Am F G Am Am F E F G Am Am F G E E".split(),
     "o5 a8 a8 o6 c8 o5 a8 e8 a8 o6 c8 e8 | o6 d8 c8 o5 b8 a8 b4 g4 | o5 a8 a8 o6 c8 o5 a8 f8 a8 o6 c8 f8 | o6 e8 d8 c8 o5 b8 o6 d4 o5 b4 |"
     "o6 e4 o5 a8 o6 e8 d4 c4 | o6 c8 o5 b8 a8 b8 o6 c4 e4 | o6 f4. e8 d4 c4 | o5 b4 g+4 e4 g+4 |"
     "o6 c8 o5 a8 f8 a8 o6 c8 f8 e8 c8 | o5 b8 g8 d8 g8 b8 o6 d8 c8 o5 b8 | o5 a4 o6 c4 e4 a4 | o6 g8 f8 e8 d8 e2 |"
     "o6 f8 e8 d8 c8 d4 f4 | o6 e8 d8 c8 o5 b8 o6 d4 g4 | o6 g+4 e4 o5 b4 g+4 | o5 e8 g+8 b8 o6 e8 g+4 r4",
     [("bass_drive", BASS, 10), ("stab0", PAD, 5), ("stab1", PAD, 5)],
     drum=[(["K...S...K.K.S...", "K...S...K.K.S.S."], 7), (["H.H.H.H.H.H.H.HO"], 4)],
     mel=(LEAD, 10))

# 8. Boss - E minor, faster and heavier
song("boss", 162, "44",
     "Em Em C D Em Em C B Am C Em Em Am B Em B".split(),
     "o5 e8 e8 g8 e8 b8 e8 o6 c8 o5 b8 | o5 a8 g8 f+8 g8 e4 r4 | o5 e8 e8 g8 e8 o6 c8 o5 e8 o6 e8 d8 | o6 c8 o5 b8 a8 b8 f+4 d4 |"
     "o6 e4 d4 o5 b4 g4 | o5 a8 b8 g8 a8 f+8 g8 e4 | o6 c4 o5 b4 a4 g4 | o5 f+4 d+4 o4 b4 o5 d+4 |"
     "o5 a8 o6 c8 e8 c8 o5 a8 o6 c8 e8 a8 | o6 g8 e8 c8 e8 g4 e4 | o6 e8 d8 o5 b8 g8 e4 g4 | o5 b2 a4 g4 |"
     "o6 c4. o5 b8 a4 o6 c4 | o5 b4. a8 f+4 d+4 | o5 e8 g8 b8 o6 e8 g4 f+4 | o6 f+8 e8 d+8 o5 b8 f+4 d+4",
     [("bass_drive", BASS, 11), ("stab0", PAD, 6), ("stab1", PAD, 6), ("bellroot", BELL, 4)],
     drum=[(["K.K.S...K.K.S...", "K.K.S...K.KKS.S."], 8), (["H.H.H.H.H.H.H.H."], 4)],
     mel=(LEAD, 10))

# 9. Mystery - C minor, tiptoeing: a music box over pizzicato and a ticking clock
song("mystery", 96, "44",
     "Cm Cm Ab G Cm Cm Db G Fm Fm Cm Cm Ab Db G G".split(),
     "o5 g4 r8 g8 a-4 g4 | o5 e-4 r4 c4 r4 | o5 c4 r8 c8 e-4 c4 | o4 b4 r4 o5 d4 r4 |"
     "o5 g4 r8 g8 o6 c4 o5 b-4 | o5 a-8 g8 f8 e-8 d4 c4 | o5 d-4 f4 a-4 f4 | o5 d2 r2 |"
     "o5 a-4 r8 a-8 o6 c4 o5 a-4 | o5 g8 f8 e-8 f8 g4 r4 | o5 e-4 r8 e-8 g4 e-4 | o5 c2 r2 |"
     "o5 c4 e-4 a-4 g4 | o5 f4 a-4 o6 d-4 c4 | o5 b4 d4 f4 a-4 | o5 g2 r2",
     [("bass_walk", PIZZ, 9), ("pad0", PAD, 3), ("pad1", PAD, 3)],
     drum=[(["....T.......T..."], 4)],
     mel=(MBOX, 10))

# 10. Calder Falls - E minor, a slow lament for harp and strings
song("falls", 66, "34",
     "Em C G D Em C B B C D G Em Am B Em Em".split(),
     "o5 b2 g4 | o5 a4 g4 e4 | o5 d2 g4 | o5 f+2. |"
     "o5 b2 o6 d4 | o6 c4 o5 b4 a4 | o5 f+2 d+4 | o5 b2. |"
     "o5 e4 g4 o6 c4 | o6 d4 c4 o5 a4 | o5 b4 a4 g4 | o5 e2. |"
     "o5 a4 o6 c4 e4 | o6 d+4 o5 b4 f+4 | o5 g4 f+4 e4 | o5 e2.",
     [("bass_whole", BASS, 7), ("arp_68", HARP, 8), ("pad0", PAD, 4), ("pad1", PAD, 4)],
     mel=(STRINGS, 9))

# 11. Victory - a short fanfare, then back to whatever was playing
song("victory", 140, "44", "C C".split(),
     "o5 c8 e8 g8 o6 c4 o5 g8 o6 c4 | o6 e2. r4",
     [("bass_half", BASS, 9), ("pad0", PAD, 5), ("pad1", PAD, 5)],
     loop=False, mel=(LEAD, 11))


def main():
    out = sys.argv[1]
    lines = ["// Gaslight & Griffins - the music. Generated by tools/make_music.py; edit that, not this.",
             "#pragma once", "#include <stdint.h>", "namespace Gaslight { namespace Songs {",
             "struct Ch { uint8_t inst, vol; const uint8_t *ev; uint16_t n; }; // ev: (note, ticks) pairs, note 0 = rest",
             "struct Song { const char *name; uint16_t bpm; uint8_t loop, nch; Ch ch[8]; };"]
    total = 0
    for si, (name, bpm, loop, chs) in enumerate(SONGS):
        for ci, (inst, vol, ev) in enumerate(chs):
            flat = []
            for n, t in ev:
                while t > 240:
                    flat += [n, 240]; t -= 240
                    # a long note continues as the same pitch only for rests; split notes re-strike, so keep pads short of that
                flat += [n, t]
            total += len(flat)
            lines.append("static const uint8_t S%d_%d[] = {%s};" % (si, ci, ",".join(str(v) for v in flat)))
    lines.append("static const Song SONGS[] = {")
    for si, (name, bpm, loop, chs) in enumerate(SONGS):
        cs = ", ".join("{%d, %d, S%d_%d, %d}" % (inst, vol, si, ci, len(ev_flat(ev)) // 2) for ci, (inst, vol, ev) in enumerate(chs))
        lines.append('    {"%s", %d, %d, %d, {%s}},' % (name, bpm, 1 if loop else 0, len(chs), cs))
    lines.append("};")
    lines.append("enum SongId { " + ", ".join("M_" + n.upper() for n, *_ in SONGS) + ", M_COUNT };")
    lines.append("}} // namespace")
    open(out, "w", newline="\n").write("\n".join(lines) + "\n")
    print("wrote", out, len(SONGS), "pieces,", total, "bytes of notes")


def ev_flat(ev):
    flat = []
    for n, t in ev:
        while t > 240:
            flat += [n, 240]; t -= 240
        flat += [n, t]
    return flat


if __name__ == "__main__":
    main()
