# Gaslight & Griffins — Higgsfield prompt pack (town 1: Fogmouth)

## Credit plan (read first)

The Higgsfield account has **10 credits** on the free plan, so it can't pay for 33 images. Art comes from three sources instead:

| Source | Cost | Used for |
|---|---|---|
| **Higgsfield: 3 "sheet" images** (prompts H1–H3 below) | your 10 credits | The style anchors: main cast, Fogmouth townsfolk, first creatures. Several characters per image; my script cuts the grid into separate files. |
| **Local Stable Diffusion on your RTX 4060** | free, unlimited | Everything else (prompts 07–30), using the H-sheets as style references (img2img / IP-Adapter) so it all matches. I'll set this up in a separate venv in Phase 1. |
| **Public-domain engravings** (Wikimedia Commons) | free | Myth scene cards 31–33, e.g. John Flaxman's *Odyssey* plates (1793), tinted by script to our palette |

**Higgsfield tips:**

- Use a cheap model first: **Nano Banana** or **Z Image** (both tagged budget). Check the credit cost in the Higgsfield UI before you press generate.
- Use **1k** resolution. 1k is plenty: each grid cell is still about 340–500 px, and the screen shows 120×160 at most.
- **One try per sheet.** If a sheet is a bit off, keep it anyway; the local model fixes details later.
- The Higgsfield connector here can only read your balance and history; it can't generate. Generate in the website and download into the inbox.

### H1 — `sheet_cast.png` (1:1, 2×2 grid)
> …[prefix] character sheet, a 2×2 grid of four separate head-and-shoulders portraits, each in its own panel divided by thin cream borders, each on a flat plain light grey background, all facing screen-left in 3/4 view. Top-left: Wren Ashby, a 17-year-old apprentice detective of ambiguous gender, short messy auburn hair, freckles, keen grey eyes, oversized tweed coat, mustard scarf, brass magnifying glass on a cord. Top-right: Professor Ambrose Vane, gaunt detective in his late fifties, silver swept-back hair, sharp nose, grieving brilliant eyes, high collar, dark green velvet waistcoat, pipe. Bottom-left: Juno Castellane, proud 18-year-old rival detective, sharp black bob, dark brown skin, police-blue double-breasted coat with brass buttons, smirk. Bottom-right: the same professor as a masked villain, smooth pale clay mask with cuneiform pattern pushed up on his forehead, dark coat with lapis-blue lining, lit coldly from below.

### H2 — `sheet_fogmouth.png` (1:1, 3×3 grid)
> …[prefix] character sheet, a 3×3 grid of nine separate head-and-shoulders portraits, each in its own panel divided by thin cream borders, flat plain light grey backgrounds, all facing screen-left. Row 1: a tavern singer woman with dark curls and a sea-green shawl; an old one-eyed lighthouse keeper with an eyepatch and grey beard in oilskins; a stern harbourmistress with grey bun, pince-nez and navy uniform. Row 2: a slick shipowner with oiled moustache, top hat and gold watch chain; a scrappy 11-year-old street kid in a flat cap holding a bun; an ancient net-mender grandmother with a white braid and fish-bone pins. Row 3: a broad sea-captain woman with silver-streaked crop and chin scar; a plump kindly landlady with half-moon glasses and lace cap; a nervous young constable in an oversized helmet.

### H3 — `sheet_creatures.png` (1:1, 3×3 grid)
> …[prefix] creature design sheet, a 3×3 grid of nine separate original fantasy creatures, each full body in 3/4 view in its own panel divided by thin cream borders, flat plain light grey backgrounds, all different species. Row 1: a soot-black salamander kitten with ember belly and candle-flame tail; a chubby slate-blue toad with a barnacle-plated back; a fawn made of moss and twigs with leaf antlers and glowing spots. Row 2: a cat-sized rat wearing a thimble helmet; a floating teardrop of pale blue ghost-flame inside a small lantern cage; three stacked sea pebbles with limpet feet. Row 3: a stout gull with a big pelican pouch; a translucent eel fading into mist; a moth with wings patterned like manuscript pages.

These three sheets cover everything in prompts 01, 03–05 and 07–27. Prompts 02, 06 and 28–30 (full bodies, Bolthound, backdrops) go to the local model.

---

## Individual prompts (local model)

The numbered prompts below are now for **local Stable Diffusion**, not Higgsfield. They also document what each character should look like.

Save each result into `4_Games/Gaslight/art/inbox/` under the file name given. PNG or JPG are both fine. My scripts crop, cut out the background, palette-match and pack the images.

- **Generate big:** the screen is small, but cut-outs and palette matching work better from a large source. Aim for **1024 px or more on the short side**.
- **Pick for silhouette:** at 120–160 px on screen, a strong, readable shape matters more than fine detail. Choose the clearest silhouette over the prettiest render.

---

## Style guide

### Prefix

Paste this before **every** prompt:

> Hand-painted storybook illustration, Victorian gaslamp fantasy, gouache and ink with visible brush texture, crisp dark ink outlines, simplified shapes and bold readable silhouette, warm lamplight against cool sea fog, limited palette, Art Nouveau framing touches, no text, no letters, no watermark,

### Palette

Ask for "limited palette" and name 3–4 of these colours in the prompt.

| Role | Colour | Hex |
|---|---|---|
| Fog | slate blue | `#3E5566` |
| Fog | pale grey-teal | `#9DB4B8` |
| Lamplight | amber | `#E8A33D` |
| Lamplight | brass | `#B9853A` |
| Accents | oxblood red | `#7A2430` |
| Accents | verdigris green | `#4F8A78` |
| Shadow | deep ink navy | `#141B26` |
| Highlight | candle cream | `#F3E3C0` |

### Lighting

- One warm key light from a lamp or window.
- A cool rim light from the fog.
- Soft shadows, no harsh studio light.

### Framing by asset type

| Asset | Framing | Background | Aspect | On-screen size |
|---|---|---|---|---|
| **Portrait** (`p_`) | Bust, head and shoulders, 3/4 view, facing **screen-left** (dialogue box sits right) | **flat plain light grey (#C8C8C8)** for easy cut-out | **3:4** | 120×160 |
| **Full body** (`b_`) | Whole figure, standing, facing camera 3/4 | flat plain light grey | **3:4** | sprite reference / scene card |
| **Creature** (`c_`) | Full body, 3/4 view, centred, small cast shadow | flat plain light grey | **1:1** | 96×96 battle, 48×48 menu |
| **Myth being** (`m_`) | Full figure, dramatic, filling frame | painted scene background allowed | **3:4** | 200×280 |
| **Backdrop** (`bg_`) | Wide establishing view | full painted scene | **16:9** | 456×280 (cropped) |
| **Scene card** (`s_`) | Engraving-meets-painting, framed composition, myth moment | full scene | **16:9** | 456×280 |

### Negative prompt

Use this where Higgsfield allows one:

> photo, 3D render, anime, chibi, text, logo, watermark, extra fingers, blurry, cropped head, busy background, modern clothing, Pokémon, cartoon network style

### Consistency

- Generate the hero, mentor and rival first.
- Reuse the **same seed or character reference** for the full-body and portrait versions of each person.

---

## Prompts

### Main cast

**01 — `p_wren.png` (3:4)**
> …[prefix] portrait of Wren Ashby, a 17-year-old apprentice detective of ambiguous gender, short messy auburn hair, freckles, keen grey eyes, oversized tweed coat with too-long sleeves, mustard scarf, brass magnifying glass on a cord, curious determined half-smile, amber and slate blue palette, plain light grey background

**02 — `b_wren.png` (3:4)**
> …[prefix] full body of Wren Ashby, same character as reference, standing in a confident pose with notebook in one hand, knee-high scuffed boots, satchel full of papers, plain light grey background

**03 — `p_vane.png` (3:4)**
> …[prefix] portrait of Professor Ambrose Vane, a tall gaunt consulting detective in his late fifties, silver swept-back hair, sharp nose, deep-set tired eyes full of grief and brilliance, high collar, dark green velvet waistcoat, pipe, faint clay dust on his fingers, oxblood and ink navy palette, plain light grey background

**04 — `p_vane_collector.png` (3:4)**
> …[prefix] portrait of the same man as the professor, now as the masked Collector, a smooth pale clay mask with a cuneiform pattern pushed up on his forehead, long dark coat with lapis-blue lining, cold lantern light from below, haunted expression, plain light grey background

**05 — `p_juno.png` (3:4)**
> …[prefix] portrait of Juno Castellane, a proud 18-year-old rival detective, sharp black bob haircut, dark brown skin, police-blue double-breasted coat with brass buttons, one raised eyebrow, confident smirk, verdigris and brass palette, plain light grey background

**06 — `b_juno.png` (3:4)**
> …[prefix] full body of Juno Castellane, same character as reference, arms crossed, police whistle on a chain, polished boots, plain light grey background

### Fogmouth townsfolk

**07 — `p_marisol.png` (3:4)**
> …[prefix] portrait of Marisol Vey, a tavern singer in her thirties, tumbling dark curls, a sea-green shawl over a faded red dress, a shell earring, melancholy eyes, mouth slightly open as if mid-song, warm candlelight, plain light grey background

**08 — `p_silas.png` (3:4)**
> …[prefix] portrait of Silas Penhallow, an old lighthouse keeper with one eye covered by a leather patch, big grey beard, oilskin coat, gentle sad expression, lamp-oil smudges on his cheek, amber light from below, plain light grey background

**09 — `p_ottilie.png` (3:4)**
> …[prefix] portrait of Ottilie Brack, a stern harbourmaster in her fifties, tight grey bun, pince-nez spectacles, navy uniform jacket with a brass anchor badge, ledger tucked under her arm, faint guilty sideways glance, plain light grey background

**10 — `p_crewe.png` (3:4)**
> …[prefix] portrait of Augustin Crewe, a polished shipowner in his forties, oiled moustache, top hat, expensive charcoal coat, gold watch chain, a too-friendly smile that doesn't reach his eyes, a tiny smear of candle wax on his cuff, plain light grey background

**11 — `p_pip.png` (3:4)**
> …[prefix] portrait of Pip, a scrappy 11-year-old street kid informant, oversized flat cap, soot on cheek, gap-toothed grin, patched jacket, holding a half-eaten bun, cheeky look, plain light grey background

**12 — `p_ysolde.png` (3:4)**
> …[prefix] portrait of Grandmother Ysolde, an ancient net-mender and keeper of old sea stories, deeply wrinkled weathered face, white braid, knitted shawl with fish-bone pins, cloudy wise eyes, holding a net needle, plain light grey background

**13 — `p_halloran.png` (3:4)**
> …[prefix] portrait of Captain Rook Halloran, the Water-type warden of Fogmouth, broad-shouldered woman in her forties with a short silver-streaked crop, sea captain's coat with tide-blue lining, salt-stained, a scar across her chin, bold grin, plain light grey background

**14 — `p_quill.png` (3:4)**
> …[prefix] portrait of Mrs. Odile Quill, a plump kindly landlady and medic in her sixties, rosy cheeks, half-moon glasses, lace cap, knitting needles stuck in her bun, a medicine satchel, warm motherly smile, plain light grey background

**15 — `p_constable.png` (3:4)**
> …[prefix] portrait of Constable Dobbs, a nervous young policeman, tall custodian helmet slightly too big, thin moustache attempt, police-blue tunic, notebook clutched, earnest worried look, plain light grey background

### Fogmouth myth beings

**16 — `m_sirens.png` (3:4)**
> …[prefix] the three Sirens of Wailing Rock, elegant bird-women with great grey-white wings and long tangled sea-green hair, sitting on a black wet rock in moonlit sea fog, their mouths closed in sorrow as if their voices were stolen, broken lyre beside them, verdigris and slate blue palette

**17 — `m_cyclops.png` (3:4)**
> …[prefix] Old One-Eye, a huge gentle ancient cyclops sitting in a sea cave beneath a lighthouse, single great amber eye glowing like a lamp, mossy stone-grey skin, wrapped in old sailcloth bandages, holding a tiny lantern with surprising care, warm amber against dark cave

**18 — `m_aeolus.png` (3:4)**
> …[prefix] Aeolus, Keeper of the Winds, a tall old figure made partly of swirling cloud, long beard blowing sideways, holding an ox-hide bag tied with silver cord, standing on a cliff top above a stormy harbour, gulls circling, pale grey-teal and cream palette

### Fogmouth creatures

Original creatures. They must **not** resemble any existing franchise creature.

**19 — `c_embrit.png` (1:1)**
> …[prefix] Embrit, a small fire creature, a round soot-black salamander kitten with ember-orange belly, a tail tipped by a candle flame, big amber eyes, tiny brass collar, cute but scrappy, plain light grey background

**20 — `c_puddock.png` (1:1)**
> …[prefix] Puddock, a small water creature, a chubby slate-blue toad with a shell-plated back like barnacles, a little seaweed tuft on its head, sleepy kind eyes, puddle at its feet, plain light grey background

**21 — `c_sprigling.png` (1:1)**
> …[prefix] Sprigling, a small fae creature, a fawn-like baby deer made of moss and twigs, glowing pale green spots, tiny leaf antlers, a dandelion seed floating near it, shy pose, plain light grey background

**22 — `c_gutterat.png` (1:1)**
> …[prefix] Gutterat, a scruffy beast creature, a cat-sized rat with a ragged ear, a stolen thimble worn as a helmet, long tail with a knot, clever mischievous eyes, cobblestone brown and grey, plain light grey background

**23 — `c_wispen.png` (1:1)**
> …[prefix] Wispen, a small spirit-fire creature, a floating teardrop of pale blue ghost-flame with a tiny lantern-cage around its middle, two dot eyes, trailing smoke wisps, plain light grey background

**24 — `c_pebblet.png` (1:1)**
> …[prefix] Pebblet, a small stone creature, a stack of three smooth sea pebbles forming a little body with limpet shells as feet and a barnacle crown, sleepy smile, plain light grey background

**25 — `c_gullet.png` (1:1)**
> …[prefix] Gullet, an air-water creature, a stout seagull-like bird with an oversized pelican pouch, a sailor's neckerchief pattern on its feathers, greedy eye, mid-squawk, plain light grey background

**26 — `c_fogeel.png` (1:1)**
> …[prefix] Fogeel, a water-spirit creature, a translucent pale eel whose body fades into mist at the tail, glowing teal fins, curled in an S shape, floating, plain light grey background

**27 — `c_inkmoth.png` (1:1)**
> …[prefix] Inkmoth, an arcane-air creature, a large moth with wings patterned like old manuscript pages with ink blots, feathery quill antennae, dusty cream and ink-black colours, plain light grey background

**28 — `c_bolthound.png` (1:1)**
> …[prefix] Bolthound, an iron-beast creature, a lean bloodhound pup with riveted iron plates on its back and long ears like steel shavings, a police whistle on its collar, sniffing pose, plain light grey background

### Fogmouth backdrop

**29 — `bg_fogmouth.png` (16:9)**
> …[prefix] wide establishing view of Fogmouth, a crooked Victorian harbour town on steep cliffs at dusk, gas lamps glowing amber through thick sea fog, tall lighthouse on the headland, fishing boats and a three-masted ship at the quay, a black jagged rock out at sea, slate blue fog, warm windows, no people in focus

**30 — `bg_fogmouth_night.png` (16:9)**
> …[same as 29], but deep night, the lighthouse dark, the moon behind fog, a single rowboat without a lantern on the black water, eerie atmosphere

### Myth scene cards (the *Odyssey*)

**31 — `s_odyssey_mast.png` (16:9)**
> …[prefix] in the style of an old engraving painted with gouache, Odysseus bound to the mast of his ship, straining against the ropes, his crew rowing with wax in their ears, the Sirens on a rocky island singing, dramatic waves, framed composition with an ornate border

**32 — `s_odyssey_cyclops.png` (16:9)**
> …[prefix] in the style of an old engraving painted with gouache, the giant one-eyed Cyclops in his cave by firelight, sheep around him, small figures of sailors hiding in the shadows, warm amber firelight, framed composition with an ornate border

**33 — `s_odyssey_winds.png` (16:9)**
> …[prefix] in the style of an old engraving painted with gouache, a ship's crew opening a leather bag of winds while their captain sleeps, storm winds bursting out as swirling faces, the ship flung back to sea, framed composition with an ornate border

---

## After generating

1. Drop the three sheets (`sheet_cast.png`, `sheet_fogmouth.png`, `sheet_creatures.png`) into `4_Games/Gaslight/art/inbox/`. My script splits each grid into the `p_` and `c_` file names above.
2. Everything else comes from the local model and the public-domain engravings. No more Higgsfield credits needed for town 1.
3. Later batches will cover:
   - Sandmere, Greenwold, Skjold and Ur-Lumen
   - grown-up creature forms
   - UI frames
