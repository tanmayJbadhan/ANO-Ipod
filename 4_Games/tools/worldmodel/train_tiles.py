"""Trains Gaslight's land model: a small autoregressive network that writes a
tile map one tile at a time (left to right, top to bottom), each tile chosen
from the twenty tiles already written above and to the left of it, the biome,
and how close it is to the edge. On the console it lays out "the Wilds" behind
each town - a fresh stretch of country every visit.

Training data:
  * the game's own hand-built outdoor maps (Fogmouth, Gull Cliffs, the Falls,
    Sandmere, Greenwold, Skjold, Ur-Lumen), with buildings turned into groves
    and town furniture into bare ground, mirrored left-right;
  * a few hundred sketched wild scenes per biome (groves, ponds, streams,
    outcrops, tall grass, ruins) made by noise rules below.
The network learns the local grammar of both: what grows by water, how rock
meets grass, how groves clump, how reeds line a canal.

usage: python train_tiles.py ../../Gaslight ../../Gaslight/GLTiles.h
"""
import math
import random
import re
import sys
import numpy as np
import torch
import torch.nn as nn

VOCAB = ",.:~^TvR=wO"          # the tiles the Wilds are made of
OUT = len(VOCAB)               # token for "off the map"
NTOK = OUT + 1
# neighbours already written: (dx, dz)
CTX = [(-1, 0), (-2, 0), (-3, 0)] + [(dx, -1) for dx in range(-3, 4)] + [(dx, -2) for dx in range(-3, 4)] + [(0, -3), (-2, -3), (2, -3)]
NCTX = len(CTX)
NPOS = 6
NBIO = 5
H = 64
W, D = 28, 30                  # the size the console asks for


# --- the game's own maps ------------------------------------------------------------------
def real_maps(gdir):
    src = open(gdir + "/Areas.cpp").read() + open(gdir + "/Towns.cpp").read()
    maps = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
            for m in re.finditer(r'static const char \*const (\w+)\[\] = \{(.*?)\};', src, re.S)}
    use = {"FALLS": 0, "TOWN": 0, "CLIFFS": 0, "MAP_A_SAND": 1, "MAP_A_GREEN": 2, "MAP_A_SKJOLD": 3, "MAP_A_UR": 4}
    out = []
    for k, bio in use.items():
        rows = []
        for r in maps[k]:
            s = ""
            for c in r:
                if c == "#": c = "T" if bio != 4 else "w"
                elif c == "M": c = "R"
                elif c not in VOCAB: c = ":" if bio in (1,) else "."
                s += c
            rows.append(s)
        out.append((bio, rows))
    return out


# --- sketched wild scenes -------------------------------------------------------------------
def vnoise(w, d, cell, rng):
    gw, gd = w // cell + 2, d // cell + 2
    g = rng.random((gd, gw))
    out = np.zeros((d, w))
    for z in range(d):
        for x in range(w):
            fx, fz = x / cell, z / cell
            ix, iz = int(fx), int(fz)
            tx, tz = fx - ix, fz - iz
            tx, tz = tx * tx * (3 - 2 * tx), tz * tz * (3 - 2 * tz)
            a = g[iz, ix] * (1 - tx) + g[iz, ix + 1] * tx
            b = g[iz + 1, ix] * (1 - tx) + g[iz + 1, ix + 1] * tx
            out[z, x] = a * (1 - tz) + b * tz
    return out


def sketch(bio, rng):
    w, d = W, D
    base = {0: ",", 1: ":", 2: ",", 3: ",", 4: "."}[bio]
    m = [[base] * w for _ in range(d)]
    n1 = vnoise(w, d, rng.choice([4, 5, 6]), rng)
    n2 = vnoise(w, d, rng.choice([3, 4]), rng)
    n3 = vnoise(w, d, 7, rng)
    for z in range(d):
        for x in range(w):
            a, b, c = n1[z, x], n2[z, x], n3[z, x]
            t = base
            if bio == 0:      # coast: sea on one flank, dunes, rocks, gorse
                if c > 0.72: t = "~"
                elif c > 0.66: t = "."
                elif a > 0.74: t = "^"
                elif b > 0.7: t = "v"
                elif a < 0.18: t = "T"
                elif b < 0.12: t = "R"
            elif bio == 1:    # desert: dunes, oasis pools ringed by palms, scrub, rock
                if c > 0.78: t = "~"
                elif c > 0.70: t = "T" if b > 0.4 else "v"
                elif a > 0.76: t = "^"
                elif a > 0.70 or b < 0.1: t = "R"
                elif b > 0.75: t = "."
            elif bio == 2:    # forest: thick groves, clearings, ferns, a pond
                if a > 0.58: t = "T"
                elif c > 0.76: t = "~"
                elif b > 0.62: t = "v"
                elif a < 0.12: t = "R"
                elif b < 0.2: t = "."
            elif bio == 3:    # fjord: crags, pines, meltwater, boulders
                if a > 0.7: t = "^"
                elif c > 0.74: t = "~"
                elif a > 0.6: t = "T"
                elif b > 0.7: t = "v"
                elif b < 0.14: t = "R"
            else:             # Ur: marsh canals, reed beds, palms, ruined walls
                if c > 0.7: t = "~"
                elif c > 0.62: t = "v"
                elif a > 0.78: t = "w"
                elif a > 0.74: t = "O"
                elif b > 0.76: t = "T"
                elif b < 0.16: t = ","
            m[z][x] = t
    # a stream across, sometimes, with a ford of planks
    if rng.random() < 0.45:
        z0 = rng.integers(6, d - 8)
        z = float(z0)
        for x in range(w):
            z += rng.normal(0, 0.6)
            zi = int(np.clip(z, 3, d - 4))
            m[zi][x] = "~"
            if rng.random() < 0.5: m[zi + 1][x] = "~"
        fx = rng.integers(4, w - 4)
        for z in range(d):
            if m[z][fx] == "~": m[z][fx] = "="
    # Ur: straight canal with a bridge
    if bio == 4 and rng.random() < 0.5:
        zc = rng.integers(6, d - 6)
        for x in range(w): m[zc][x] = "~"
        m[zc][rng.integers(4, w - 4)] = "="
    # edges: hemmed in by the biome's wall
    edge = {0: "^", 1: "^", 2: "T", 3: "^", 4: "w"}[bio]
    for z in range(d):
        for x in range(w):
            if x == 0 or x == w - 1 or z == 0 or z == d - 1:
                if rng.random() < 0.9: m[z][x] = edge
    return ["".join(r) for r in m]


# --- features -------------------------------------------------------------------------------
def pos_feats(x, z, w, d):
    return [x == 0, x == w - 1, z == 0, z == d - 1, min(x, w - 1 - x, z, d - 1 - z) / 4.0 if min(x, w - 1 - x, z, d - 1 - z) < 4 else 1.0, z / d]


def samples(bio, rows):
    d, w = len(rows), len(rows[0])
    idx = [[VOCAB.index(c) for c in r] for r in rows]
    ctx, pos, tgt = [], [], []
    for z in range(d):
        for x in range(w):
            c = []
            for dx, dz in CTX:
                xx, zz = x + dx, z + dz
                c.append(idx[zz][xx] if 0 <= xx < w and 0 <= zz < d else OUT)
            ctx.append(c)
            pos.append(pos_feats(x, z, w, d))
            tgt.append(idx[z][x])
    return ctx, [bio] * len(tgt), pos, tgt


class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.emb = nn.Parameter(torch.randn(NCTX, NTOK, H) * 0.1)
        self.bio = nn.Parameter(torch.zeros(NBIO, H))
        self.pos = nn.Linear(NPOS, H)
        self.out = nn.Linear(H, len(VOCAB))

    def forward(self, ctx, bio, pos):
        h = self.emb[torch.arange(NCTX), ctx].sum(1) + self.bio[bio] + self.pos(pos)
        return self.out(torch.relu(h))


def main():
    gdir, outp = sys.argv[1], sys.argv[2]
    rng = np.random.default_rng(7)
    data = []
    for bio, rows in real_maps(gdir):
        for _ in range(6):   # the real maps count for more
            data.append((bio, rows))
            data.append((bio, [r[::-1] for r in rows]))
    for bio in range(NBIO):
        for _ in range(160):
            data.append((bio, sketch(bio, rng)))
    C, B, P, T = [], [], [], []
    for bio, rows in data:
        c, b, p, t = samples(bio, rows)
        C += c; B += b; P += p; T += t
    C = torch.tensor(C); B = torch.tensor(B); P = torch.tensor(np.array(P, dtype=np.float32)); T = torch.tensor(T)
    print("samples", len(T))
    torch.manual_seed(1)
    net = Net()
    opt = torch.optim.Adam(net.parameters(), lr=3e-3)
    n = len(T)
    for ep in range(14):
        perm = torch.randperm(n)
        tot = 0
        for i in range(0, n, 2048):
            j = perm[i:i + 2048]
            loss = nn.functional.cross_entropy(net(C[j], B[j], P[j]), T[j])
            opt.zero_grad(); loss.backward(); opt.step()
            tot += loss.item() * len(j)
        if ep == 9:
            for g in opt.param_groups: g["lr"] = 1e-3
        print("epoch", ep, "loss %.3f" % (tot / n))

    # --- export: int8 with one scale per tensor --------------------------------------------------
    def q(t):
        a = t.detach().numpy().astype(np.float32)
        s = float(np.abs(a).max()) / 127 or 1.0
        return np.clip(np.round(a / s), -127, 127).astype(np.int8), s
    emb, se = q(net.emb)
    w2, s2 = q(net.out.weight)
    bio = net.bio.detach().numpy()
    pw = net.pos.weight.detach().numpy()   # H x NPOS
    pb = net.pos.bias.detach().numpy()
    b2 = net.out.bias.detach().numpy()
    def arr(name, typ, a):
        flat = a.flatten()
        body = ",".join(("%d" % v) if typ == "int8_t" else ("%.5ff" % v) for v in flat)
        return "static const %s %s[%d] = {%s};\n" % (typ, name, len(flat), body)
    with open(outp, "w", newline="\n") as f:
        f.write("// Gaslight's land model - generated by tools/worldmodel/train_tiles.py. Do not edit.\n")
        f.write("// An autoregressive tile network: %d neighbour tiles + biome + edge -> next tile.\n" % NCTX)
        f.write("#pragma once\n#include <stdint.h>\nnamespace Gaslight { namespace LandNet {\n")
        f.write('static const char VOCAB[] = "%s";\n' % VOCAB)
        f.write("static const int NV = %d, NTOK = %d, NCTX = %d, NH = %d, NPOS = %d;\n" % (len(VOCAB), NTOK, NCTX, H, NPOS))
        f.write("static const int8_t CTX[NCTX][2] = {%s};\n" % ",".join("{%d,%d}" % c for c in CTX))
        f.write("static const float SE = %.8ff, S2 = %.8ff;\n" % (se, s2))
        f.write(arr("EMB", "int8_t", emb))
        f.write(arr("W2", "int8_t", w2))
        f.write(arr("BIO", "float", bio))
        f.write(arr("PW", "float", pw.T))   # NPOS x H
        f.write(arr("PB", "float", pb))
        f.write(arr("B2", "float", b2))
        f.write("}} // namespace\n")
    print("wrote", outp, "params", emb.size + w2.size + bio.size + pw.size + pb.size + b2.size)

    # a preview of what it writes, one per biome
    net.eval()
    g = torch.Generator().manual_seed(3)
    for bio in range(NBIO):
        m = [[None] * W for _ in range(D)]
        for z in range(D):
            for x in range(W):
                c = []
                for dx, dz in CTX:
                    xx, zz = x + dx, z + dz
                    c.append(m[zz][xx] if 0 <= xx < W and 0 <= zz < D else OUT)
                with torch.no_grad():
                    lg = net(torch.tensor([c]), torch.tensor([bio]), torch.tensor([pos_feats(x, z, W, D)], dtype=torch.float32))[0] / 0.85
                m[z][x] = int(torch.multinomial(torch.softmax(lg, 0), 1, generator=g))
        print("biome", bio)
        print("\n".join("".join(VOCAB[i] for i in r) for r in m))


if __name__ == "__main__":
    main()
