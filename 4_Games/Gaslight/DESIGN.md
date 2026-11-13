# Gaslight & Griffins — Design

A detective RPG for Jigglypuff. You're a young detective in a gaslamp world where everyone raises partner creatures. Each town is built on an ancient myth, and each town's crime echoes that myth. One mastermind, **the Collector**, ties every case together.

- **Code folder:** `4_Games/Gaslight/` (namespace `Gaslight`). The name matches the title and is shorter than `Detective`.
- **SD art folder:** `/quest/`
- **Length:** about 3.5 hours (prologue, then 4 town cases, then the finale)

---

## 1. Structure and pacing

| Part | Place | Myth | Time | Badge |
|---|---|---|---|---|
| Prologue | Calder Falls | — | 15 min | — |
| Act I | **Fogmouth** (port) | *Odyssey*: Sirens, Cyclops, Aeolus | 45 min | Tide Seal |
| Act II-a | **Sandmere** (desert) | *Book of the Dead*: Sphinx, Ma'at, Ammit | 40 min | Feather Seal |
| Act II-b | **Greenwold** (forest) | *Gawain and the Green Knight* | 40 min | Girdle Seal |
| Act III | **Skjold** (fjord) | Norse: Huginn and Muninn, the Norns | 40 min | Raven Seal |
| Finale | **Ur-Lumen** (capital) | *Epic of Gilgamesh* | 35 min | — |

- **Cut:** Cloudpeak (*Journey to the West*) and Lotusford (*Ramayana*). They're kept as named places on the map for a later expansion.
- **Order:** Sandmere and Greenwold can be played in either order. Their clues cross-reference each other, and the deduction board rewards spotting the link.
- **Inside each town:** arrive → informants and rumours → case → route trainers → myth fragments and favours → Warden battle (the gym leader) → accusation. The Warden battle is gated behind the case's midpoint, so story and battles interleave.
- **Level curve:** roughly levels 5, 15, 25, 35 and 45 at the four Wardens and the finale.

---

## 2. Cast

| Character | Role | Notes |
|---|---|---|
| **Wren Ashby** (renameable) | Hero, 17 | Apprentice detective. Was beside the mentor at the Falls. Partner: chosen starter. |
| **Professor Ambrose Vane** | Mentor; secretly **the Collector** | Famous consulting detective and myth scholar. "Fell" at Calder Falls a year ago, body never found. His partner creature **Kidu** died saving him years ago; grief became obsession (Gilgamesh losing Enkidu). |
| **Juno Castellane** | Rival | Sharp, proud detective, daughter of the Chief Inspector. Battles you at the edge of each town. Is she racing you, or covering for someone? |
| **Chief Inspector Bram Castellane** | Police head; secretly a **Curator** | Twist in Act III: he's on the Collector's payroll, out of debt and fear rather than malice. |
| **The Curators** | The Collector's agents | Masked collectors who wear clay-tablet pins. Each town has one embedded agent. |
| **The Gutter Post** | Street-kid informants | **Pip** (Fogmouth docks), **Tally** (Sandmere bazaar), **Moth** (Greenwold, sleeps in the hollow oak), **Sigga** (Skjold). Pay them in buns or favours; they bring rumours and tail suspects. |
| **Mrs. Odile Quill** | Landlady and medic | Heals your team, keeps your casebook, knits suspicious-looking scarves. Recurring home base. |
| **Dr. Hesper Lune** | Myth archivist | Collects your story fragments and reads them back as scene cards. Quietly knew Vane was alive. |

---

## 3. The overarching mystery

**What the Collector wants.** Vane is re-enacting Gilgamesh's search for eternal life. Each town's myth holds a fragment of an old power tied to death or renewal:

| Town | What's stolen |
|---|---|
| Fogmouth | The Sirens' **voice**, a song that calls the drowned back |
| Sandmere | The **Feather of Ma'at**, which weighs a soul |
| Greenwold | The Green Knight's **regrowth**: his head grows back |
| Skjold | **Memory**, the second raven |

Together with the **Plant of the Deep** under Ur-Lumen, they would bring Kidu back. The ritual would also drain the myth-beings of every town and leave them silent forever.

**Hooks that build the twist:**

1. **Prologue:** the Falls. Vane gives Wren his notebook "in case". His stick is found at the edge; there's no body.
2. **Fogmouth:** the culprit was paid with a clay tablet in cuneiform. The handwriting on its sleeve note uses Vane's odd "Greek e".
3. **Sandmere:** the Sphinx's riddle mentions "a teacher who taught you to read footprints, who left none at the Falls".
4. **Greenwold:** Juno's father is seen meeting a Curator. The green girdle carries a Vane-style knot.
5. **Skjold, the big reveal:** Muninn (Memory) shows Wren the real Falls. Vane stepped onto a ledge and Castellane helped stage it. You confront the Chief Inspector; Juno must choose a side.
6. **Ur-Lumen:** Vane, as the Collector, explains himself. The final "case" is an interrogation of your mentor, using everything he taught you.

**Midgame false lead.** For Acts I–II the board points at **Juno** as the Curator inside the police. She keeps arriving at scenes before you. She's actually investigating her own father. Accusing her in Act II costs her trust, and she won't help in the finale.

---

## 4. Town cases

Each case has four suspects, about eight clues (two of them hidden behind skill checks), one or two red herrings, a myth echo, and a twist.

### Act I — Fogmouth: *The Drowned Choir*

Three ships have run onto Wailing Rock in a month. The crews swear they heard singing.

- **Myth echo:** Odysseus had his crew stop their ears with wax and tied himself to the mast. The Cyclops's cave becomes a lighthouse with one great lamp.
- **Suspects:**
  - **Marisol Vey**, tavern singer. She sings the haunting tune every night.
    - *Red herring:* she learned it from a gramophone cylinder a sailor sold her.
  - **Silas Penhallow**, the one-eyed lighthouse keeper. His lamp was dark on the night of the third wreck.
    - *Red herring:* he was down in the cave tending the injured Cyclops, the town's old guardian.
  - **Ottilie Brack**, harbourmaster. She altered the ship ledger.
    - *False lead:* she's covering her own small brandy smuggling.
  - **Augustin Crewe**, shipowner. **The culprit.** He played the stolen siren song on a waterproof phonograph from a rowboat to wreck his own insured ships.
- **Key clues:**
  - Candle-wax plugs in Crewe's coat pocket (Observation 8). He knew not to listen.
  - His insurance was raised a week before the first wreck.
  - Phonograph needle shavings at the rowboat jetty.
  - Pip saw a rowboat with no lamp.
  - The Sirens are silent. Their rock is empty.
- **Twist:** the song is real. The Collector extracted the Sirens' voice and sold a copy to Crewe as a field test. Crewe was paid with a cuneiform tablet: the first link to the conspiracy.
- **Wrong accusation:**
  - Marisol → she's jailed, and the Sirens refuse their favour.
  - Silas → the lamp goes dark for good, and the night route gets harder.
  - Ottilie → the port closes for a day, and the shop is shut.
- **Warden:** Captain Rook Halloran (Water). His lodge is a drydocked ship.

### Act II-a — Sandmere: *The Featherless Scale*

The Hall of Weighing museum is robbed during a gala. Its night curator, Kamal Idris, is found asleep, drugged, inside a sealed sarcophagus room.

- **Myth echo:** the weighing of the heart. Here it's the **interrogation mechanic**: each lie you catch adds weight to a suspect's scale. Three tips and Ammit the Devourer "eats" their composure, forcing a confession or a slip.
- **Suspects:**
  - **Kamal Idris**, the "victim".
    - *Red herring:* he arranged a fake robbery to cover lost artifacts he'd pawned. The fake was then hijacked by a real theft.
  - **Lady Neferet Ashwood**, patron and collector, who wanted the Feather.
    - *Red herring:* she has an alibi from the gala photographer, and you can only verify it with Memory.
  - **Tomas Reyes**, guide and Sphinx-lore expert who knows every secret passage.
  - **Sefa Okon**, a quiet restorer. **The culprit**, a Curator agent. She was blackmailed after her brother's debt was bought by the Collector.
- **Key clues:**
  - Restorer's gold-leaf dust in the vent (Observation).
  - The sarcophagus was sealed from outside with fresh resin, so the "locked room" isn't locked.
  - The Sphinx's three riddles: answering them with Lore unlocks the hidden passage.
  - Tally spotted a woman leaving by the camel-gate at dawn.
- **Twist:** the Feather was never in the case. Kamal had swapped it with a replica long ago. Sefa stole the replica; the Curators then took the real Feather from Kamal's pawnbroker. Both "thieves" are partly guilty. The board lets you accuse **two** people, and the best result needs both.
- **Warden:** Imani Sato (Stone/Fire), sand-glass sculptor.

### Act II-b — Greenwold: *A Year and a Day*

A year ago, a guest at Hautdesert Lodge accepted a stranger's game: "strike me now, and I strike you in a year". Today the guest, Sir Gareth Lyle, has vanished, and a green-cloaked figure stalks the woods.

- **Myth echo:**
  - Gawain's exchange of winnings becomes the **trade game**. For three evenings the host and guests swap what they "won" that day.
  - The green girdle that saves a life, worn in secret, is the lie to catch.
- **Suspects:**
  - **Lord Bertram Hautdesert**, the jolly host who set the trade game.
    - *Red herring:* he loves theatrical pranks and plays the green figure himself, once.
  - **Lady Rosalind Hautdesert**. Her gift of a girdle to Gareth was hidden from her husband. She hid a gift, not a crime.
  - **Old Morgana Fay**, herbalist who knows every root.
  - **Gareth Lyle**, the "missing" man. **The culprit is the victim.** Terrified of the return blow, he took the Collector's offer: hand over the Green Knight's regrowth power in exchange for a protective charm. He faked his own vanishing.
- **Key clues:**
  - Gareth's missing boots are still muddy and freshly cleaned (Observation).
  - Inconsistencies in the trade game: someone kept back one item each night (Logic 9).
  - The Green Knight's chapel is empty and his axe is rusted, so the regrowth is gone.
  - Moth saw "the missing man" buying bread.
- **Twist:** the green figure in the woods is the real Green Knight, weakened and looking for Gareth to get his power back. He isn't the villain. Fight him (hard) or talk him down (Charm). Talking earns his favour.
- **Midgame hook:** Chief Inspector Castellane's carriage was at the chapel road. Juno arrives first again and acts guilty.
- **Warden:** Ser Ivy Marchbank (Fae/Beast), a gamekeeper-knight.

### Act III — Skjold: *Thought and Memory*

Skjold is forgetting. Townsfolk lose names and days. The two ravens that fly over the fjord every dawn are down to one: Huginn (Thought) still flies, and Muninn (Memory) is gone.

- **Myth echo:** Odin feared Memory would not return. Here the town's memory physically drains into the Collector's tablet.
- **Suspects:**
  - **Skald Eirik Hallsson**, a poet who now "recites" new old sagas no one remembers.
    - *Red herring:* he's just a fraud exploiting the confusion.
  - **Asta Brun**, rune-carver who carved a strange cage.
    - *Red herring:* she built it to catch and protect Muninn, not trap it.
  - **Juno Castellane.** She's here alone, and the evidence lines up against her.
  - **Chief Inspector Bram Castellane. The culprit,** working for the Collector.
- **Key clues:**
  - The Norns' loom shows an unpicked thread with a police-blue fibre.
  - A requisition for "one cage, iron, bird-sized" signed with police seals.
  - Sigga tailed a police boat to the sea cave.
- **Twist and confrontation:**
  - You rescue Muninn. It returns Wren's memory of the Falls: Vane is alive, and Castellane staged it.
  - **Juno's choice:**
    - If you never accused her and helped her in Greenwold → she arrests her own father.
    - Otherwise → she flees, and your finale ally slot is empty.
- **Warden:** Hilde Ravnsdóttir (Air/Spirit).

### Finale — Ur-Lumen: *The Last King's Garden*

The capital is ringed by great walls (Uruk's walls). The Collector's ziggurat-museum opens under a solar eclipse.

- **Myth echo:**
  - Gilgamesh dove for the plant of youth, and a serpent stole it while he bathed.
  - Siduri the alewife told him to "fill your belly, dance, love the child who holds your hand".
- **Structure:**
  1. **Infiltration** with a Disguise check, as a Curator.
  2. **The Collector's Gauntlet:** four Curators with themed teams.
  3. **The interrogation of Vane.** Every case's evidence becomes an argument against his logic. The Siduri, Utnapishtim and Serpent favours give special lines.
  4. **The battle:** Vane's team, including a half-revived **Kidu** that flickers between life and spirit.
- **Endings:**

| Ending | Conditions | Outcome |
|---|---|---|
| **The Long Night** (bad) | Two or more wrong accusations, or fewer than 4 favours | Vane completes the ritual. Kidu wakes hollow and the myth-beings go silent. Wren becomes the new Collector's hunter. Bleak epilogue. |
| **The Arrest** | All 4 cases solved correctly | Vane is defeated and taken in. The myths are restored. Juno (if allied) becomes Chief Inspector. |
| **The Letting Go** (good) | All cases solved, 8+ favours, Siduri's favour, and Juno allied | You talk Vane down. He gives the Plant of the Deep to the Serpent as in the myth, says goodbye to Kidu's spirit, and turns himself in. Kidu's spirit joins your team as a legendary. |
| **The Serpent's Ending** (secret) | All 20 story fragments | After the Letting Go, the Serpent sheds its skin. The stolen myth-powers return to each town, Sirens sing again, and a final scene card shows Vane teaching street kids. |

---

## 5. Detective systems

**Skills (Wren).** There are six, each rated 1–6 at the start. Each favour or level milestone gives +1.

| Skill | Used for |
|---|---|
| **Observation** | Spotting details on people and in scenes, like Holmes reading a tan line or a frayed cuff |
| **Logic** | Linking clues on the board and catching contradictions in alibis |
| **Memory** | Recalling earlier testimony word for word (a lie detector across scenes) |
| **Charm** | Persuading, calming and bribing |
| **Disguise** | Getting into closed places as a sailor, priest, groom or Curator |
| **Lore** | Myth knowledge, riddles and reading engravings |

**Skill checks.** Roll 2d6 + skill against a target number (DC) of 7–14. Checks come in three kinds:

- **White checks** can be retried after you find a new clue.
- **Red checks** are one-shot.
- **Passive checks** fire automatically as inner-voice interjections, Disco Elysium style. Each skill has a voice: Observation is twitchy, Logic is dry, Memory is melancholy, Charm is flirty, Disguise is theatrical, Lore is pompous.

Dice are shown rolling. Hidden modifiers include the disguise you're wearing, a held clue and favours.

**Casebook and deduction board.**

- Clues are cards. On the board you pick two clues to make a link: the wheel scrolls through them and Center joins them.
- A correct link produces a deduction card. A wrong link costs **Composure**, the detective's own HP-like stat for the case.
- At zero Composure you must sleep (a time skip), and the culprit may destroy one clue.
- **Accusation:** pick suspect + motive + method + key evidence. Wrong pieces have story consequences (see each case). Correct ones raise your **Reputation**, which lowers DCs with officials.

**Interrogation, the weighing.** Ask about topics, then **Present** evidence when a suspect lies. A correct present tips their scale. A wrong present tips **yours**. Three tips on their side means a confession or slip.

**Observation mode.** Freeze-frame a person's portrait, then scroll the wheel across highlighted regions (hands, shoes, collar). Each region rolls a hidden Observation check.

**Disguises.** You buy or earn costumes. Each has a "cover" meter that drains when you talk to someone who would know better.

**Informants (the Gutter Post).** Spend buns (bought cheaply) to send a kid to:

- tail a suspect, which returns a clue after 2–3 scenes
- gather rumours, from the text model
- fetch an item

---

## 6. Battles

The Nintendo rules, with our own names.

- **Basics:** turn-based, a team of 6, 4 moves each, levels and XP, growing into new forms at a set level.
- **Status effects:** Burn, Soak (Water: speed down, takes extra Stone damage), Charm-struck (Fae confusion), Haunt (Spirit damage over time), Rust (Iron defence down), Sleep and Paralysis (called Stun).
- **Held items:** Lucky Penny, Smelling Salts, Iron Nail, Silver Bell, and others.
- **Catching:** with **Lanterns**: Brass, Silver, and the special Moonglass.
- **Encounters:** trainers on routes, a Warden in each town (the gym leader), the rival at each town edge, and the Curators.
- **Legendaries:** three (Kidu, Veilwyrm and Chronoth); see the creature list.

### Types (9)

The draft had 8. I split **Iron** out of **Arcane**, so "cold iron beats fae" can stand alone and Arcane can be study and alchemy.

| Attacker | 2× against | ½× against | 0× against |
|---|---|---|---|
| **Fire** | Beast, Fae, Iron | Fire, Water, Stone | — |
| **Water** | Fire, Stone | Water, Fae | — |
| **Stone** | Fire, Air | Water, Iron, Beast | — |
| **Air** | Beast, Water | Stone, Iron | — |
| **Beast** | Arcane, Stone | Iron | Spirit |
| **Fae** | Water, Arcane | Fire, Iron | — |
| **Spirit** | Spirit, Fae | Iron | Beast |
| **Iron** | Fae, Spirit | Fire, Water, Iron | — |
| **Arcane** | Spirit, Iron | Arcane, Beast | — |

- **Status immunities:** Iron can't be Charm-struck, Spirit can't be Stunned, Fire can't be Burned, and Water can't be Soaked.
- **Balance check:** every type has at least one weakness and at least one 2× target.

### Creatures (62)

Starters are chosen at the Falls. Arrows show growth with levels.

| # | Line | Type | Where |
|---|---|---|---|
| 1–3 | **Embrit** →16 Cindrake →34 Pyrewyrm | Fire → Fire/Beast | Starter |
| 4–6 | **Puddock** →16 Brineback →34 Tidewarden | Water → Water/Stone | Starter |
| 7–9 | **Sprigling** →16 Thornhart →34 Elderstag | Fae → Fae/Beast | Starter |
| 10–11 | Gutterat →18 Alleyking | Beast | Fogmouth streets |
| 12–14 | Wispen →14 Lanternghast →30 Gravelamp | Spirit/Fire | Fogmouth graveyard |
| 15–16 | Pebblet →20 Cairnback | Stone | Fogmouth cliffs |
| 17–18 | Gullet →18 Stormskua | Air/Water | Fogmouth docks |
| 19–20 | Fogeel →22 Mistmoray | Water/Spirit | Fogmouth bay |
| 21–22 | Inkmoth →20 Scriptwing | Arcane/Air | Fogmouth library |
| 23–24 | Bolthound →24 Ironmastiff | Iron/Beast | Fogmouth police |
| 25–27 | Scarabit →18 Dunebeetle →32 Sunscarab | Stone/Fire | Sandmere dunes |
| 28–29 | Jackalet →26 Duatjackal | Spirit/Beast | Sandmere tombs |
| 30–31 | Sandsnap →24 Asplord | Beast/Stone | Sandmere oasis |
| 32–33 | Ibisling →26 Quillibis | Arcane/Air | Sandmere museum |
| 34–35 | Dustmote →28 Glassdjinn | Arcane/Stone | Sandmere glassworks |
| 36–37 | Pixmote →22 Thistlequeen | Fae | Greenwold meadows |
| 38–39 | Mossgob →26 Barrowtroll | Stone/Fae | Greenwold barrows |
| 40–41 | Hollyhob →28 Greencloak | Fae/Beast | Greenwold deep wood |
| 42–43 | Acornet →24 Oakwarden | Fae/Stone | Greenwold oaks |
| 44–45 | Fennix →26 Emberbrush | Fae/Fire | Greenwold heath |
| 46–47 | Rimecub →30 Frostbear | Beast/Water | Skjold glacier |
| 48–49 | Corvel →28 Rookshade | Air/Spirit | Skjold cliffs |
| 50–51 | Runeseal →32 Skaldwalrus | Water/Arcane | Skjold fjord |
| 52–53 | Cogwren →30 Clockkite | Iron/Air | Skjold forge |
| 54–55 | Clayling →34 Golemark | Stone/Arcane | Ur-Lumen walls |
| 56–57 | Bullwing →36 Wingbull | Beast/Air | Ur-Lumen gates |
| 58–59 | Reedsnake →36 Deepwyrm | Water/Fae | Ur-Lumen canals |
| 60 | **Kidu** (legendary) | Beast/Spirit | Vane's partner; joins only on the Letting Go ending |
| 61 | **Veilwyrm** (legendary) | Spirit/Water | Post-game, Wailing Rock at midnight |
| 62 | **Chronoth** (legendary) | Arcane/Iron | Post-game, Ur-Lumen clock tower |

**World-model variants.** Each wild creature gets a small variant (colour, markings, a size of ±10%, one prop such as a scarf or horn chip) from the world model. A shiny-like **"Storied"** variant (1 in 200) has a myth-pattern texture. Breeding at Mrs. Quill's (post-Act II) blends the parents' latent codes.

---

## 7. Myth beings and favours

Myth beings can't be caught. You earn their favour by helping people.

Each favour grants:

- **+1 to one of Wren's skills**
- **a creature boon**: a teachable move or a passive

| Town | Being | Earned by | Wren | Creature boon |
|---|---|---|---|---|
| Fogmouth | The Sirens | Clear Marisol and return the song cylinder | Charm | *Siren Call* (Water/Fae, lure, Charm-struck) |
| Fogmouth | Old One-Eye (Cyclops) | Help Silas heal him, and don't accuse Silas | Observation | *Boulder Toss* (Stone, high power) |
| Fogmouth | Aeolus, Keeper of Winds | Side quest: recover his wind-bag from a gull thief | Lore | *Fair Wind* (team speed up, 3 turns) |
| Sandmere | The Sphinx | Solve all 3 riddles | Logic | *Riddle Lock* (target can't repeat a move) |
| Sandmere | Ma'at's Feather-Bearer | Accuse both thieves correctly | Memory | *Truth Weight* (ignores stat boosts) |
| Sandmere | Ammit | Catch 5 lies in interrogations | Observation | *Devour* (Spirit, heals by damage) |
| Greenwold | The Green Knight | Talk him down, don't fight | Charm | *Regrowth* (heal 50%, once per battle) |
| Greenwold | The Lady of the Lodge | Keep her secret from her husband | Disguise | *Green Girdle* (held item: survive one KO hit) |
| Greenwold | Morgan of the Mere | Side quest: the herb triad | Lore | *Glamour* (Fae, evasion up) |
| Skjold | Huginn (Thought) | Solve the case | Logic | *Foresight* (see enemy's next move) |
| Skjold | Muninn (Memory) | Free it without breaking the cage | Memory | *Recall* (reuse last move free) |
| Skjold | The Norns | Re-weave 3 lost town memories (side quest) | Lore | *Fate Thread* (crits guaranteed for 1 turn) |
| Ur-Lumen | Siduri the Alewife | Share a drink, answer her honestly (Charm) | Charm | Unlocks the Letting Go line |
| Ur-Lumen | Utnapishtim | All 4 seals | Memory | *Flood* (Water, hits all) |
| Ur-Lumen | The Serpent | All story fragments | Lore | Unlocks the Serpent's Ending |

**Story fragments.** There are 20: 4 per town. Each is a short illustrated retelling, shown as a scene card with 3–5 text panels. Each unlocks:

- one hidden clue or a DC −2 on a related check
- or a favour step

---

## 8. Randomness and the two models

| Where | System | What it does |
|---|---|---|
| Skill checks | Dice: 2d6 + skill | Shown rolling; outcomes are hand-written |
| Wild encounters, crits, catch odds | RNG | Standard creature-RPG rules |
| Rumours and Gutter Post reports | **Text model** + story grammar | Text model writes the flavour line; the grammar inserts the real hint (a name, place or item) so hints are always correct |
| NPC idle chatter, crowd lines | **Text model** | Conditioned on town, speaker type and mood |
| Inner voices (passive skill pings) | **Text model** for flavour | Plot-critical lines stay hand-written |
| Trainer battle taunts and victory lines | **Text model** | Conditioned on trainer class and type |
| Random street events (pickpocket, lost pet, argument) | Event table + dice | 1 per 5 minutes on average, small rewards |
| Wild creature variants, Storied variants, breeding | **World model** | Run once per new creature, at encounter start |
| Route dressing (props, grass patches, spawn points) | **World model** layout head | Run once per route chunk on first visit, then cached to SD |
| Case story, clues, accusations, endings | **Hand-written** | Never generated |

**Safety net for the text model.** Every generated line passes a word-list filter (only known words) and a length cap. If it fails, the game picks a hand-written line. The player never waits: core 0 fills a ring buffer of about 24 lines per category in idle time.

---

## 9. Controls

| Input | Overworld | Menus and dialogue |
|---|---|---|
| Arrows | Walk (8-way by combining) | Up/Down move cursor |
| Wheel | Rotate camera in 90° steps (optional) | Scroll choices and the clue board |
| Center | Talk, examine, confirm | Confirm |
| Down (tap) | — | Back |
| Hold Center | Open casebook | — |

Holding Down doesn't lock the screen inside the game, as with the other games.

---

## 10. Save

- One save slot plus an autosave on town entry, stored in NVS/LittleFS (about 4 KB).
- Saved state: flags, clue states, team, favours and fragments.
