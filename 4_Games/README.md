# 4 · Games

Opening **Games** shows a tile per game. Every picture in play is drawn from
shapes and every sound synthesised, so the games need no image or audio files
(only the storybook cards use a few public-domain engravings). `HalfRes.*` is
shared by all of them: they draw at half resolution and it doubles the pixels on
the way to the screen.

## *Ariadne's Thread*

The myth of the Cretan labyrinth in ten levels, drawn like Greek black-figure
pottery (clay orange, glossy black figures with incised details, Greek-key
bands) and Knossos frescoes (red columns that taper downward, double axes,
dolphins, lilies, horns of consecration). The labyrinth itself is black and red.

| # | Level | What you do |
|---|---|---|
| Prologue | Ariadne's Loom | Press Select as the shuttle crosses the gold. Each true throw spins more thread for Act I. |
| Act I, 1-4 | The Bull Gate, The Hall of Frescoes, Daedalus's Doors, The Breathing Dark | Theseus walks a torch-lit maze, unrolling the red thread behind him. Walk back along it to wind it in; if it runs out, wind back and try another way. Double axes mark the true path in the Hall; bronze doors swing open and shut; in the Breathing Dark the Minotaur follows your scent, and you see only his eyes. |
| Act II | The Heart | A vase-painting arena. The Minotaur paws, then charges: leap over him, let him crash into the columns, and strike while he is dazed. The crashes shake stones loose. |
| Act III, 1-4 | The Thread Home, The Flooded Cistern, Minos's Guards, The Last Gate | You are the ball of thread, rolling a path out while Theseus and the Athenians follow. The thread can never cross itself, so a wrong turn means winding back (Select). Spindles add thread; water costs double; lantern light pulls the thread back; the last hall falls on a timer. |
| Finale | The Crown of Stars | Ariadne sails free and the last of her thread becomes Corona Borealis. |

**The generator** (comments in `Ariadne.cpp` give the details):
- **Mazes, Act I:** Wilson's algorithm (loop-erased random walks, so every
  perfect maze is equally likely), then braiding where the probability of
  opening a dead end follows a fractal noise field, `p = p0 (0.55 + 0.9
  max(0, fbm))`. Five candidates are made and scored by
  `F = 0.45 L/2N + 0.25 E/N + 0.20 H/2 + 0.10 (1 - |D - D*|/D*)`: solution
  length L, the cells E an A* agent expands (the agent-effort difficulty
  measure of Beukman, James & Cleghorn, arXiv:2201.10334), the entropy H of the
  junction degrees and the dead-end ratio D. The best is kept.
- **Yarn levels, Act III:** a Hamiltonian path through every cell, shuffled by
  ~3000 backbite moves (Mansfield 1982; shown to connect all Hamiltonian paths
  of a grid by Kazazi, arXiv:2601.06749), gives a long self-avoiding route from
  the centre to the edge. Every other cell is joined to it by Wilson's walks as
  dead-end branches.
- **Decoration:** Poisson-disk dart throwing for frescoes, torches and
  spindles; the double axes follow the BFS solution.
- **Light:** `I(d) = (1 - (d/R)^2)^2` with a flicker, only where a Bresenham
  ray reaches the tile; torches make a static light map.
- **The Minotaur:** follows a diffusing scent,
  `u' = u + D (sum of neighbours - n u) - lambda u`, with u = 1 at Theseus.
- **The thread:** drawn as a Catmull-Rom curve through the tile centres.
- **The title:** the classical seven-circuit Cretan labyrinth from the coins of
  Knossos, drawn as its path (circuits 3 2 1 4 7 6 5, then the centre).

| File | What it does |
|---|---|
| `Ariadne.*` | All ten levels, the generators, lighting, the Minotaur, the black-figure characters and the story cards. |

**How to play**: arrows walk or steer (the wheel steers the yarn and walks
Theseus in the arena). **Select** throws the shuttle, leaps or strikes, and
winds the yarn back. **Hold Down** pauses in the mazes; **Down** pauses in the
arena.

## *Cat Nap*

Mini mines, with cats. Cats are asleep under some of the cushions; lift all the
others without waking any. A number tells how many cats sleep on the cushions
around it. Mark a cat's cushion with a ball of yarn. The first cushion is never a
cat, and an empty one opens its whole quiet patch. Three households: **Kitten**
(14 cats), **Cat cafe** (22) and **Cat lady** (30), on a 16 x 9 board. Best
times are remembered.

| File | What it does |
|---|---|
| `CatNap.*` | The board, the cushions, the sleeping (and one very awake) cats, yarn marks, the timer and best times. |

**How to play**: arrows move the paw, **Select** lifts the cushion, and turning the
wheel puts yarn on or takes it off. Select on a number whose cats are all marked
lifts the rest around it. **Hold Down** to pause. Tapping a cushion lifts it.

## *Raise the Emerald City*

A tower-stacking game from *The Wonderful Wizard of Oz* (L. Frank Baum, 1900).
The cyclone scattered the Emerald City, and the Wizard runs a brass crane to
build it again. Each floor swings from the crane's hook. Drop it square onto the
one below: the better it lines up, the more people move in.

- **Perfect drop:** it snaps into place, sparkles, and counts double. Three in a
  row bring the **Winged Monkeys** with 25 more people.
- **Sloppy drop:** the tower leans and starts to sway, which makes the next drop
  harder.
- **Hearts:** a floor that misses the edge falls, and so does one that tips the
  tower too far off balance. Each costs one of three hearts.
- **Roof:** after the last floor comes the roof. Then the camera glides down the
  finished tower as every window lights up.

Six lands, each with its own floors, roof and twist:

| # | Land | Floors and roof | Twist |
|---|---|---|---|
| 1 | Munchkin Country | blue, with scalloped awnings and round windows; a blue dome | none: learn the swing |
| 2 | The Scarecrow's Field | a wooden barn with green shutters and corn cobs; thatch with a crow weathervane | the Witch's crows land on the swinging floor and rock it |
| 3 | The Great Forest | riveted tin with portholes and the Tin Woodman's heart; a tin cone with a heart weathervane | heavy floors: slow swing, more sway |
| 4 | The Poppy Field | cream with red arches and poppy window boxes; a red onion dome | the crane grows drowsy, then wakes |
| 5 | The Winkie Country | yellow stone with purple banners; battlements and a purple turret | the west wind pushes the crane and the falling floor |
| 6 | The Emerald City | emerald glass, gold pilasters and gems, at night; the great green spire | fast, with a breeze |

Dorothy and Toto wait at the foot of the Yellow Brick Road, the Wizard sits in
the crane's cab, his green silk balloon drifts by, and the Emerald City glints on
the horizon until you reach it. Finishing a land unlocks the next; each land's
best population is remembered. The ending card shows how many people came home
to Oz.

| File | What it does |
|---|---|
| `EmeraldCity.*` | The crane, the six lands (floors, roofs, scenery, skies with Perlin-noise clouds), the swing and drop, balance and sway, scoring, and the story cards. |
| `Denslow.h` | W. W. Denslow's 1900 illustrations (Dorothy with the Tin Woodman and the Scarecrow; Toto at the Emerald City; the Wizard found out) for the title and story cards. They are public domain, from Wikimedia Commons. Generated by `tools/make_denslow.py`. |

**How to play**: **Select** (or a tap) drops the floor. The wheel nudges the
crane's trolley a little to fine-tune the swing. **Down** pauses. On the title
screen, Left/Right chooses a land.

## *Late for Tea*

A 3D dash behind the White Rabbit, down winding paths through ten places from
Wonderland, to reach the Mad Hatter and the March Hare's tea party before four
o'clock:

1. The Garden of Roses (white, red, and white painted red)
2. The Cricket Pitch (flamingo bats)
3. The Firefly Forest
4. The Caterpillar's Mushrooms
5. The Pool of Tears
6. The Cheshire Woods
7. The Duchess's Kitchen
8. The Looking-Glass Board
9. The Lobster Quadrille
10. The Mad Tea Party

How it plays:
- **Rivals:** tortoises on skateboards race him for the **Linzer cookies**. If one
  gets there first, they eat them all.
- **Alice:** she chases him. If she catches him three times, she shares the
  cookies with the seagulls, the tortoises and him.
- **Geese:** they block the paths. Hop over them.
- **Boosts:** candy and Easter eggs speed him up.
- **Seagulls:** they swoop down. A **yellow** one carries him ahead, a
  **purple** one carries him back.
- **The clock:** if four o'clock comes first, the Queen is furious.
- **Progress:** winning a place unlocks the next. Win all ten and the cookies
  are his, but he shares them anyway.

| File | What it does |
|---|---|
| `LateForTea.*` | The 3D path (perspective ground, winding curve, scenery both sides), the ten places, the characters (drawn from shapes, inspired by Tenniel), Perlin-noise clouds and hills, the race rules, and the story cards. |
| `Tenniel.h` | John Tenniel's 1865 illustrations (the White Rabbit, the Mad Tea Party, the Queen of Hearts) for the title and story cards. They are public domain, from Wikimedia Commons ("Alice par John Tenniel 02 / 25 / 29.png"). Generated by `tools/make_tenniel.py`. |

**How to play**: **Left/Right** (or the wheel) changes path, **Select** hops,
**hold Up** to sprint (it uses his puff, which refills), and **Down** pauses.
On the title screen, Left/Right chooses a place.

## *Cypress Night*

A maze chase in a Persian garden at night. A writer in round glasses gathers
the pages of his manuscript from the paths between rows of cypress trees, around
a central pool. Four owls hunt him from their roosts in the corners and wake one
after another. Each has its own habit: one heads straight for him, one cuts him
off, one circles the pool until he comes near, and one wanders. A **lantern**
dazzles them for a few seconds: they turn pale and flee, and catching one sends
it back to its roost (200, 400, 800, 1600 points). A **pomegranate** appears
below the pool twice a night. Clear every page and dawn breaks; the next night is
faster. Extra life every 10,000 points. Best score is remembered.

| File | What it does |
|---|---|
| `CypressNight.*` | The garden, the writer, the owls and their habits, lanterns, scoring, the title / pause / game-over screens. |

**How to play**: arrows walk. Turning the wheel turns him left or right. Tapping
the screen beside him heads that way. **Select** starts and pauses; while
paused, **Down** quits the run. Up only means "back" on the title screen, since
it walks during play.

## *Lullaby Run*

A pseudo-3D arcade racer in 11 districts. Puff races rivals across the city to win
the Golden Hubcap. Every picture is drawn from shapes and every sound is synthesised
while it plays, so the game needs no image or audio files.

| File | What it does |
|---|---|
| `LullabyRun.*` | The game: the 11 districts (colours, skyline, props, rival, track), road and car physics, rivals and traffic, pickups (candy = boost, music note = sends cars to sleep, 3 hidden ducks), the district picker, HUD, pause and finish screens, unlocks and best times. |
| `Raster.*` | A small shape renderer (rectangles, polygons, circles, lines) that draws into the game's frame buffer. |
| `RunAudio.*` | The engine sound, built live: an **EV** motor whine or a petrol (**ICE**) engine with gears and exhaust pops, plus tyre/wind noise and effects. Streams straight to the DAC. |
| `preview/lullaby-run.html` | The same game in a web browser, used to design the look. Open it in any browser. |

**How to play**: the car accelerates by itself. Steer with the wheel or Left/Right,
**Down** brakes, **Select** boosts, **Up** pauses (then Down quits). Finishing in the
top 3 unlocks the next district. Marshmallow (EV) recharges its boost when braking.
Grumbler (ICE) charges it on near misses.

**Performance**: the game draws at half resolution (228×140) in fast internal memory
and doubles every pixel on the way to the screen, at about 17–20 frames per second.
Music stops while the game is open, because the engine sound uses the DAC.
