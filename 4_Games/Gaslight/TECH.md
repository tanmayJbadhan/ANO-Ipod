# Gaslight & Griffins — Technical plan

## 1. 3D renderer

**Camera.** Fixed 3/4 view: about 35° pitch, a narrow 30° FOV perspective (almost isometric), following the player. The wheel can optionally snap the yaw in 90° steps. A fixed pitch keeps the triangle count low and lets us sort by depth cheaply.

**Pipeline.** This builds on Lamplighter's strip renderer.

1. **`prepare()` on core 1, once per frame:**
   - transform and clip visible meshes
   - light the vertices (baked colour × one directional lamp)
   - bin triangles into the **14 strips of 20 rows** by their screen y-range
   - freeze everything into a read-only frame state
2. **Both cores raster strips** from an atomic queue. Core 0 runs a worker at priority 2, as in Lamplighter. For each strip:
   - clear a **20×456 colour strip** (RGB565, 18 KB) and a **20×456 depth strip** (16-bit, 18 KB)
   - draw that strip's triangles: affine texture mapping, subdivided every 32 px for large triangles, 16-bit z test
   - overlay 2D sprites (creature billboards, effects, UI), then transpose to portrait and send with `draw16bitRGBBitmap`
3. **Port buffers:** 4, as in Lamplighter, so sending overlaps drawing.

**Content rules.**

| Item | Budget |
|---|---|
| Triangles on screen | about 1,500 (town) and about 600 (battle) |
| Texture size | 64×64 at 8-bit palette (4 KB + 512 B palette), shared across many meshes |
| Character animation | baked vertex keyframes at 10 fps, blended linearly |
| Detailed props | prefer billboards |

**fps targets.**

| Scene | Target |
|---|---|
| Overworld | **20 fps** (floor 15) |
| Battle | **30 fps** (2 creatures + arena) |
| Dialogue and board | 30 fps (mostly 2D; only changed strips are re-sent) |

**Fallback if the town view can't hold 15 fps.**

- Pre-render each town's **static scenery** to colour and depth tiles on the PC, stored per camera yaw.
- At runtime, copy the scenery from those tiles; only characters, doors and effects are real 3D, depth-tested against the baked depth.
- This is the classic trick from fixed-camera games. It looks the same but costs far less.

**Measuring.** Phase 2 adds a `T` bench key (frame ms, strip ms per core, tris) and a PC-simulator build that runs the same raster code.

---

## 2. Pack and streaming format

**Art sources.** Higgsfield credits are scarce (10 on the free plan), so:

- **Higgsfield:** only 3 grid "sheets" as style anchors
- **Local Stable Diffusion** on the RTX 4060 (separate venv, img2img / IP-Adapter from the sheets): the bulk of the art
- **Public-domain engravings:** the myth scene cards

**Pipeline:** PNG → `tools/gg_pack.py`

1. split grid sheets on their cream borders, auto-crop, then cut out the flat-grey background
2. palette-match to the town's 256-colour palette
3. compress
4. write `.qpk`

**`.qpk` layout.** One file per town, battle set or UI, all little-endian.

```
Header   : magic 'QPK1', u16 version, u16 count, u32 tocOffset
TOC[n]   : u32 id (FNV-1a of name), u8 type, u8 codec, u16 w, u16 h, u32 offset, u32 size, u32 rawSize
Blobs    : 4-byte aligned
```

| Field | Values |
|---|---|
| **type** | texture, portrait, sprite, mesh, anim, palette, text, sound, layout |
| **codec** | raw, LZ4-style (fast decode, about 1 GB/s on PC and about 40 MB/s on the S3), RLE for cut-outs with transparent runs |

Meshes use 16-bit quantised positions, 8-bit UVs and 16-bit indices.

**Where things live.**

| Storage | Contents |
|---|---|
| Flash | code, both model weights, the UI pack, fonts |
| SD (`/quest/`) | town packs, battle packs, portrait packs, scene cards; route-dressing cache (`/quest/cache/`) |

**Loading.**

1. On a town or battle transition, a fade-out plus a **scene card or title card** is drawn first.
2. A loader task on core 0 reads the pack into the PSRAM cache, in **LRU order by pack**.
3. Pieces that a strip needs are copied into internal RAM per strip:
   - textures are tiny, so they stay hot
   - portraits stream line by line

**Load-time rule.** No pack over **about 1.5 MB**, so loads fit inside a 1–1.5 s fade. The SD card shares SPI and reads at roughly 1–2 MB/s; this needs measuring in Phase 1. If one place needs more, split the pack and prefetch the next area's pack during dialogue.

---

## 3. Memory budget

| Store | Budget | Use |
|---|---|---|
| **Flash: app** (6.25 MB partition) | ~2.0 MB today + **~0.9 MB** for the game | Code ~250 KB, text model 210 KB, world model 155 KB, UI pack + fonts ~250 KB, hand-written script and dialogue ~150 KB (compressed) |
| **SD** (`/quest/`) | ~40 MB total | 5 town packs × ~4 MB (split into 1.5 MB sub-packs), 62 creatures × 2 sizes ~3 MB, 15 myth beings + 20 scene cards ~6 MB, portraits ~3 MB, cache ~1 MB |
| **PSRAM** (~6 MB free) | **5 MB** cache | Current area pack 1.5 MB, next-area prefetch 1.5 MB, battle pack 0.8 MB, portraits on screen 0.4 MB, model scratch 0.2 MB, text ring buffers 16 KB, save + flags 64 KB, slack 0.6 MB |
| **Internal RAM** (~107 KB free) | **~90 KB** | 2 colour strips (36 KB) + 2 depth strips (36 KB) for the 2 cores, 4 port buffers sized to Lamplighter's, hot textures ~8 KB, model layer activations ~4 KB (int8, done in tiles), stack |

If 90 KB is too tight, the depth strips can drop to one per core with an 8-bit z (−18 KB), or move to PSRAM. Depth access is sequential per row, so PSRAM is fine there.

All big buffers are allocated in `enter()` with `heap_caps_calloc` (internal first, PSRAM fallback), bundled in `struct Mem`, and freed in `exit()`.

---

## 4. Model 1: text model (chatter and voices)

| Item | Plan |
|---|---|
| **Architecture** | Character-level **GRU**: embedding 80×48 → 1 GRU layer (hidden 224) → linear 224×80. Inputs start with a control prefix: `<town><speaker><mood>`. |
| **Parameters** | about **205K**, int8 per-row scales → **~210 KB flash** |
| **Vocabulary** | about 80 symbols: lowercase, uppercase, digits, punctuation, control tokens |
| **Training data** | ~3,000 hand-written lines (~250 KB), expanded 4× with the story grammar to ~1 MB. Tagged by town, speaker class (sailor, scholar, kid, Curator…) and mood. |
| **Training** | PyTorch on the PC. A few minutes on the RTX 4060 (or ~30 min on CPU). Quantisation-aware fine-tune at the end. Export to `GaslightTextW.h`. |
| **On-console runtime** | ~0.2 M MACs per char. Plain C with int8 dot products is about 5 ms per char, ~0.3 s per 60-char line. With ESP32-S3 SIMD (`dsps_dotprod`) we may reach 2× faster. |
| **Scheduling** | Low-priority task on core 0. Runs only when the strip worker is idle (dialogue screens, menus, fades). Fills ring buffers of ~24 lines per category. Temperature 0.7 with top-k 8. |
| **Quality guard** | Every word must be in a 6,000-word list, plus a length cap and a banned-word list. Failed lines are dropped silently. An empty buffer falls back to hand-written lines. |

---

## 5. Model 2: world model (variants, breeding, route dressing)

| Item | Plan |
|---|---|
| **Architecture** | VAE trained on the PC, **decoder only** on the console. Input: latent z (16) + condition (8: type one-hot bits, town, rarity). MLP 24→128 → ~3 heads. |
| **Head A: shape recipe** | 128→48 numbers: body-part ids, scales, hue shifts, marking type, prop id. Drives the creature's mesh and billboard build. |
| **Head B: texture** | 128→4×4×48, then 3 transposed conv layers (48→32→16→8 channels) to **32×32**, then a 1×1 conv to a **16-colour palette index map**. This becomes the creature's skin or markings over its base art. |
| **Head C: layout** | 128→8×8×4 grid: probabilities for prop, grass, spawn and path on each route chunk |
| **Parameters** | about **150K**, int8 → **~155 KB flash** |
| **Training data** | (1) Recipes and textures from a Python procedural generator plus hand-made base creatures. (2) Pattern crops from Poly Haven CC0 textures. (3) Hand-made route layouts (~60 chunks) with mirror and rotation augmentation. |
| **Breeding** | z = lerp(parent A, parent B, 0.5) + noise(0.3). Condition = the dominant parent's type. |
| **On-console runtime** | ~6 M MACs per creature ≈ **150 ms**. Run once, at encounter start (hidden by the battle transition) or on first chunk visit. The result is cached in PSRAM; route layouts are also written to `/quest/cache/`. |
| **Never** | Run per frame. |

---

## 6. Phases and testing

- Every phase builds in the **PC simulator** first (`pcsim/` with `ziglang c++`, stub headers), then on the console only when you say it's free.
- **Phase 1 deliverables:**
  - `tools/gg_pack.py`
  - `Gaslight/Pack.cpp`: reader, LRU PSRAM cache, strip streaming
  - a bench that reports SD read MB/s
- **Phase 2 deliverables:**
  - strip renderer with depth
  - test scene: a Fogmouth street block with 1,500 tris
  - fps on both cores

---

## 7. As built (November 2026)

Where the build ended up differing from the plan above.

### Renderer

- **Ground:** one textured plane at y = 0, drawn a whole screen row at a time.
  - Each place's ground is painted into a texture on arrival, at 12 texels per tile, with half- and quarter-size copies for the distance.
  - Painting takes about 0.8 s, hidden in the fade.
  - Past the map's edges the plane continues as sea or land.
- **Objects:** triangles with a 16-bit depth buffer per strip.
  - Flat-coloured triangles take a fast path.
  - Vertices go through a small direct-mapped cache in internal RAM.
  - Faces that can never face the camera are skipped early.
- **Camera:** the wheel turns it round Wren and a touch drag zooms it in close. Scenery between the camera and Wren is cut away.
- **Speed in Fogmouth:** about 14 fps.
  - Scene setup: about 28 ms on core 1.
  - Raster: about 33 ms, shared by both cores.

### Content

- **No packs on the SD card yet:**
  - Maps are built from tile text (`tools/make_towns.py`).
  - People and creatures are built in code.
  - The scene-card engravings are compiled into flash (`Engravings.h`, about 490 KB).
- **Flash used:** about 3.2 MB of the 6.25 MB app partition.

### The two models

| Model | Plan | As built |
|---|---|---|
| **Text** | ~205K-parameter GRU | ~105K-parameter character GRU (hidden 160), int8, input side folded per character |
| **World** | ~150K-parameter VAE (MLP + small CNN) | Conditional VAE decoder of ~5.9K parameters: species colours and size plus a 6-number code, out come three colours and a size |

- **Text model:**
  - Runs on core 0 between frames, a few characters at a time, and pauses while a place loads.
  - A line is kept only if every word is in the training text.
  - It writes crowd chatter, rumours and street kids' gossip, and is never used for the story.
- **World model:**
  - Decoded once per creature and cached.
  - Every wild creature gets its own colouring.

### The third model: land (the Wilds)

A small autoregressive tile network (`GLTiles.h`, trained by `tools/worldmodel/train_tiles.py`, about 16.8K parameters):

- **What it does:** writes a 28 × 30 map one tile at a time, left to right and top to bottom. Each tile is picked from:
  - the 20 tiles already written nearby (above and to the left);
  - the biome;
  - how close the tile is to the edge.
- **Vocabulary:** 11 tiles: grass, earth, sand, water, rock, tree, tall grass, boulder, planks, wall, column.
- **Network:** an embedding per (neighbour, tile) summed into 64 hidden units, then ReLU, then 11 outputs.
  - Embeddings and the output layer are int8; the rest is float.
- **Training data:**
  - The game's own outdoor maps (buildings become groves, town furniture becomes bare ground), mirrored.
  - 160 sketched wild scenes per biome (noise rules for groves, ponds, streams with fords, outcrops, reed beds, ruins).
  - Loss 0.50 after 14 epochs, about a minute on a PC.
- **On the console:** sampling at temperature 0.85 takes well under a tenth of a second. Then `Wilds.cpp` shapes the result into a place:
  - walls round the edge;
  - a worn trail from the way in to a den of standing stones, fording water on planks;
  - a reachability check (BFS) so that the five caches and the tall grass are all reachable.
- **Seeds:** each visit uses a new seed. A save made in the Wilds stores the seed and rebuilds the same land on load.

### Playtest bot and memory (November 2026)

- **Playtest bot** (`Bot.cpp`, PC simulator only, `gg.exe G`): plays from "A new case" through the same keys a player presses.
  - In the world, it walks breadth-first paths to every person and thing it hasn't tried since the story last moved on, then through doors to places it hasn't been.
  - In dialogue, it takes the option it has tried least.
  - On the board, in interrogations and in accusations, it presses toward the answers in the case tables.
  - In battle, it picks the strongest move, heals when low and catches up to three kinds.
  - It rests at Mrs. Quill's or an inn when worn out, and trains in the grass when it has nothing left to try.
- **Soft-locks the bot found and that are now fixed:**
  - Leaving Mrs. Quill's after the prologue led back to Calder Falls, which has no exit. A story warp indoors was remembered as the door in. Now only real doors set the way back out.
  - The locked trapdoor in the lighthouse pushed Wren north, into the wall. It now steps her back.
  - Sarcophagi and painted columns could be walked through.
  - Greenwold: once Morgana's herb favour was done, her girdle question (the only source of the Collector's charm clue) never came back. She now still asks it.
  - Greenwold: losing to the Green Knight stopped him ever fighting or talking again, so his clue was lost. He now offers a rematch.
  - The Sea Cave and the Sanctum, reached by the story, had no exits of their own. They now have real exits, and any interior no door leads to falls back to the last place Wren stood outside.
- **Result:** the bot played from a new game to ending 102 ("The Arrest").
  - All four cases solved, with 22 deductions, 14 lies caught and 19 of 20 fragments found.
  - It took about 4.4 million decisions and 105,000 scene starts over 291 different scenes.
  - The Collector's final team was eased from levels 44–48 to 42–45: wild creatures in Ur-Lumen top out at 40.
- **Memory on the console** (measured with temporary bench keys, since removed):

  | Measure | Result |
  |---|---|
  | Internal RAM free | 21.3 KB in every one of the 33 places, the Wilds included (largest block 8.7 KB) |
  | Internal RAM, lowest ever | 11.0 KB, over several enter/exit cycles |
  | PSRAM free | 6.10 MB in the world; 5.99 MB once a battle has run (its 112 KB mesh is kept and reused) |
  | Leaks over three enter/battle/exit cycles | None: identical numbers each time |
  | Stack headroom | Worker task at least 6.1 KB of 8 KB; main loop at least 4.2 KB |
  | Static RAM | 153 KB |
  | Flash | 3.42 MB of 6.25 MB |

### Music

- **The score:** 11 original pieces in `tools/make_music.py`, each a melody in MML plus a chord chart and accompaniment styles. The tool turns them into note tables in `GLMusic.h` (about 7.6 KB) and checks that every part lasts exactly as long as its chord chart, so loops never drift.

  | Piece | Played in | Sound |
  |---|---|---|
  | Fogmouth | Fogmouth, the cliffs, Mrs. Quill's, the tavern | Accordion shanty over harp, 6/8 |
  | Sandmere | Sandmere | Flute over oud and hand drum, E phrygian dominant |
  | Greenwold | Greenwold | Recorder waltz with harp and pizzicato |
  | Skjold | Skjold | Slow strings, bell and drone |
  | Ur-Lumen | Ur-Lumen | Strings, bells and pizzicato, C harmonic minor |
  | The Wilds | The Wilds | Bright walking lead with light drums |
  | Battle / Boss | Battles; boss music for trainers worth 250+ shillings, the Curators, the Knight and the Collector | Driving lead, bass and drums |
  | Mystery | Other interiors, interrogations, accusations | Music box, walking pizzicato, a ticking clock |
  | Calder Falls | The prologue; the ending screen for now | A lament for harp and strings |
  | Victory | After beating a trainer, then back to what was playing | A short fanfare |

- **The tracker** (`Tune.cpp`): 8 channels, 13 small synthesised instruments (flute, music box, harp, pad, bass, reed, bell, pizzicato, drum kit, lead, strings, oud).
  - It renders at 22.05 kHz, smoothed up to 44.1 kHz. `RunAudio` mixes it under the sound effects through `RunAudio::setMusic()`.
  - Changes between pieces fade over about half a second.
  - Its sine table lives in PSRAM.
  - The PC simulator writes any piece to a WAV file (`gg.exe M<piece>,<seconds>`).
- **Cost on the console:** under 1 fps.
  - Fogmouth street: 13.5 fps without music, 12.6–13.3 with it.
  - Mrs. Quill's: 18.6 without, 17.5–17.9 with.
- **Not done yet:** recorded CC-licensed tracks from the SD card for the title and ending screens.

### Display bus fix (November 2026)

- **The bug:** Arduino_ESP32QSPI's object holds an 8 KB pixel staging buffer, so a plain `new` put it in PSRAM, which can't do DMA. The SPI driver then allocated and copied an 8 KB internal buffer on every transfer. When internal RAM was fragmented that allocation failed (`ESP_ERR_NO_MEM`), `polling_start` failed, and `polling_end` asserted, rebooting the console now and then as the game started.
- **The fix:**
  - The firmware builds the bus object in internal DMA-capable memory (`makeBus()` in `Jigglypuff.ino`; the boot log says where it went).
  - The driver now skips the end of a transfer whose start failed, and logs why.
- **Result:** no failures over repeated launches, and the lowest free internal RAM in the game rose from 21.0 KB to 24.2 KB.

### Wren

Wren is drawn in her own "plush" style (`buildWrenChibi`, style 7 in `Art.cpp`):
- a big round head with big shiny eyes, rosy cheeks and a small smile, the face tilted up to read from the game's high camera;
- a soft beret worn back, a mustard jumper, red mitts, a long red scarf that trails as she walks, and round boots.

`g_wrenStyle` still selects the older figure (0) or the other trial styles (1–6). The dialogue portrait is still the old drawing.
