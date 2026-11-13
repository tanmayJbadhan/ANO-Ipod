# Gaslight & Griffins: credits

Everything in the game is original, apart from the following. Myths retold in the game are public domain.

## Font

- **DejaVu Serif and DejaVu Sans**
  - **Licence:** Bitstream Vera licence, with DejaVu changes in the public domain.
  - **Source:** the copies bundled with matplotlib.
  - **Converted by:** `tools/make_glfont.py`.

## Scene-card engravings

All from Wikimedia Commons, converted by `tools/make_cards.py`. Every one is public domain except one, which is CC0.

| Card | Picture | Artist / source | Licence |
|---|---|---|---|
| 0 | Ulysses and the Sirens | John Flaxman (1805) | Public domain |
| 1 | Polyphemus | John Flaxman (1805) | Public domain |
| 2 | Ulysses receiving the winds from Aeolus | John Flaxman, Metropolitan Museum of Art | CC0 |
| 4 | The weighing of the heart | Papyrus of Hunefer (c. 1275 BC) | Public domain |
| 5 | Ammit | Book of the Dead | Public domain |
| 6 | Osiris enthroned | Papyrus of Hunefer | Public domain |
| 7 | The Sphinx and the Pyramids | 1890 engraving | Public domain |
| 8 | The Green Knight at Camelot | Cotton Nero A.x (c. 1400) | Public domain |
| 9 | Gawain illustration | Cotton Nero A.x (c. 1400) | Public domain |
| 10 | Sir Gawain sets out | Frank T. Merrill (1902) | Public domain |
| 12 | Odin with Huginn and Muninn | Lorenz Frølich (1895) | Public domain |
| 13 | The Norns | H. L. M. (1882) | Public domain |
| 16 | Gilgamesh and Enkidu (cylinder seal) | George Smith, *The Chaldean Account of Genesis* (1876) | Public domain |
| 17 | Heabani (Enkidu) | L. Le Cenci Hamilton, *Ishtar and Izdubar* (1884) | Public domain |
| 18 | Izdubar and Siduri | *Ishtar and Izdubar* (1884) | Public domain |
| 19 | Izdubar's battle | *Ishtar and Izdubar* (1884) | Public domain |

Cards 3, 11, 14 and 15 are drawn in code.

## Models

- **Chatter model** (`tools/textmodel`): trained only on lines written for this game.
- **Creature-variant model** (`tools/worldmodel`): trained on palette rules written for this game.

Neither uses any outside data.
