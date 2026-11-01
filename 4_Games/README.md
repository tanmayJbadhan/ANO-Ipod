# 4 · Games — *Lullaby Run*

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
