"""Training text for Gaslight's chatter model: hand-written lines for each
place and kind of speaker, plus a small phrase grammar that recombines them.

Every line starts with a tag "<ts>" - t = town (f fogmouth, s sandmere,
g greenwold, k skjold, u ur-lumen, a anywhere), s = speaker kind (c crowd,
r rumour, b battle, v inner voice) - and ends with a newline. The console
asks for a tag and lets the model finish the line.

usage: python corpus.py out.txt
"""
import random
import sys

LINES = {
    "fc": [
        "Fog's thick as wool tonight. Mind the quay edge.",
        "My husband won't go past the point any more. None of them will.",
        "Herring's dear this week. Everything's dear since the wrecks.",
        "The lamps on the quay were lit early. Nobody wants to be in the dark here.",
        "You're the Professor's girl, aren't you? He used to buy my bread.",
        "Gulls stole my hat again. Third time this month.",
        "The chapel bell's rung more this month than all last year.",
        "If you hear singing on the water, put your fingers in your ears and run.",
        "Old Silas keeps the light like it's his own child.",
        "There's a draught in this town that comes straight off the Rock.",
        "My grandmother said the Sisters sang to save us. Nobody listens to grandmothers.",
        "Crewe's carriage went by at midnight. Where's a coal man going at midnight?",
        "The tavern's quiet since the singing started. Nobody wants to hear a tune.",
        "Keep your creature close in the fog. It knows the way home better than you.",
        "I saw a Wispen follow the lamplighter all the way to the church.",
    ],
    "sc": [
        "The heat this noon could melt a brass kettle.",
        "Dates, figs, cold water from the deep well. Best in Sandmere.",
        "The Sphinx winked at me once. My husband says it was the sun.",
        "Tourists ask the Sphinx riddles. She never answers tourists.",
        "The Hall's been closed since the gala. Nobody says why.",
        "Mind your step near the dunes. Sandsnaps bury themselves to the eyes.",
        "Imani's furnace burns day and night. You can see it glow from the oasis.",
        "My camel is smarter than my brother. Don't tell my brother.",
        "Lady Neferet bought half the bazaar's antiques last year.",
        "There are passages under the square. Everyone knows. Nobody goes.",
        "The night wind from the desert sounds like someone weeping.",
        "Gold leaf gets everywhere. My cat sparkles.",
    ],
    "gc": [
        "The deer won't go near the Chapel any more.",
        "Lord Bertram's trade game is the talk of the county. He cheats, you know.",
        "Something big walks in the wood after dark. Bigger than a horse.",
        "Morgana's tea will cure anything. Except Morgana.",
        "The moss is dying round the old stones. Never seen that before.",
        "My nan says the Green Knight is older than the oaks.",
        "Holly berries are early this year. That's a bad sign, or a good one.",
        "The Lodge kitchens send out venison every Sunday. Free, if you're quick.",
        "Don't take the north path alone. The trees close in behind you.",
        "There's a fox in the heath with a tail like a torch.",
    ],
    "kc": [
        "What's your name again? Sorry. I'm sure I knew it.",
        "The fjord froze early. The fish are sulking.",
        "I had a sister. I think. I wrote her name on my hand somewhere.",
        "The skald's sagas are lovely. Funny how none of us remember them.",
        "Only one raven at dawn again. Something's wrong in the sky.",
        "Asta's runes keep the wolves off. And the tax collector.",
        "The mead hall's fire never goes out. That's the rule.",
        "Hilde's hammer starts before the sun. You can set a clock by it.",
        "There's a light in the sea cave some nights. Nobody goes there.",
        "Midsummer... we did something at midsummer. I can't remember what.",
    ],
    "uc": [
        "The walls of Ur-Lumen were old when the world was young.",
        "Don't stare at the winged bulls. They stare back.",
        "The Museum of Last Things opens tonight. Invitation only, they say.",
        "The canals smell of lotus and coal smoke.",
        "Siduri's ale will make you forget your troubles. And your purse.",
        "There's an eclipse tonight. The priests are nervous.",
        "Clay soldiers guard the old gate. Nobody remembers who made them.",
        "A man in a clay mask buys two cups and drinks one. Every evening.",
        "The city remembers everything. That's its curse.",
        "The gardens bloom at night here. Mind the reeds; things live in them.",
    ],
    "fr": [
        "They say a rowboat goes out at night with no lamp.",
        "They say the harbourmaster's ledger has holes in it.",
        "They say the lighthouse went dark on purpose.",
        "They say Crewe insured his ships for double.",
        "They say the singer learned her song from a box.",
    ],
    "sr": [
        "They say the curator's had money troubles.",
        "They say a woman left by the camel gate at dawn.",
        "They say the Sphinx opens the old passage for a riddle answered.",
        "They say the Feather in the case wasn't the real one.",
        "They say the restorer's brother owes the wrong people.",
    ],
    "gr": [
        "They say the missing guest was seen buying bread.",
        "They say Lord Bertram has a green costume in a chest.",
        "They say the lady gave the guest a girdle.",
        "They say the Knight's axe has rusted through.",
        "They say a police carriage waited on the chapel road all night.",
    ],
    "kr": [
        "They say a police launch rows to the sea cave.",
        "They say the skald's sagas come from a penny book.",
        "They say Asta's cage has an open door.",
        "They say the smith made a cage for a man with a police seal.",
        "They say the girl from the capital is hunting her own father.",
    ],
    "ur": [
        "They say the Collector was once a teacher.",
        "They say there's a plant under the ziggurat that makes the old young.",
        "They say he walks with a shadow shaped like a great beast.",
        "They say the Curators wear clay masks because they have no faces.",
        "They say the stair down opens when the last mask falls.",
    ],
    "ab": [
        "Let's see what you're made of!",
        "My partner's never lost on a Tuesday.",
        "Don't blink. We won't.",
        "I trained all winter for this.",
        "You'll regret crossing my path.",
        "Steady now. Steady. Now!",
        "Partners out, and no tears after.",
        "I've seen your kind before. I wasn't impressed.",
        "Win or lose, we'll have a story.",
        "Come on, then. Show me the famous apprentice.",
    ],
    "av": [
        "OBSERVATION: Look at the hands. Hands never lie.",
        "LOGIC: Two facts that won't sit together. One of them is false.",
        "MEMORY: You've heard that phrase before. Where?",
        "CHARM: Smile. People tell smiling strangers everything.",
        "DISGUISE: A cap and a slouch, and you're no one at all.",
        "LORE: This happened before, in an older story.",
        "OBSERVATION: Mud on the left boot, not the right.",
        "LOGIC: The time doesn't work. Count it again.",
        "MEMORY: Two came. One returned.",
        "LORE: Every myth has a price, and someone always pays it.",
    ],
}

# a small grammar to recombine the hand-written lines into more variety
GRAMMAR = {
    "c": ["{open} {mid}.", "{mid}. {close}", "{open} {mid}. {close}"],
    "open": ["Mind you,", "Honestly,", "Between us,", "Funny thing:", "Well,", "Oh,", "Listen,", "I tell you,"],
    "close": ["Mark my words.", "Not that anyone listens.", "Strange times.", "That's what I heard.", "You didn't hear it from me."],
}
MIDS = {
    "f": ["the fog's never been this thick", "the Sisters are quiet these days", "the lamps burn low by midnight",
          "the Rock looks closer every night", "the gulls are nervous", "nobody sails past the point"],
    "s": ["the sand's moving more than usual", "the Sphinx looks sad lately", "the Hall's guards are jumpy",
          "the oasis is cooler than it should be", "the bazaar's full of strangers", "the nights are colder every year"],
    "g": ["the wood is too quiet", "the old stones are cold to touch", "the deer are leaving",
          "the Lodge lights burn all night", "the mere has gone dark", "the moss is dying"],
    "k": ["half the town's forgotten its own name", "the ice is early", "only one raven flies now",
          "the sagas sound wrong", "the sea cave glows at night", "the fjord is louder than it was"],
    "u": ["the walls hum at night", "the eclipse is coming", "the canals are too still",
          "the bulls at the gate have moved", "the Museum's doors were open last night", "the priests are counting coins"],
}


def generate(n, seed=1):
    rnd = random.Random(seed)
    out = []
    for tag, lines in LINES.items():
        for l in lines:
            out.extend(["<%s>%s" % (tag, l)] * 6)
    towns = "fsgku"
    for _ in range(n):
        t = rnd.choice(towns)
        form = rnd.choice(GRAMMAR["c"])
        line = form.format(open=rnd.choice(GRAMMAR["open"]), close=rnd.choice(GRAMMAR["close"]),
                           mid=rnd.choice(MIDS[t]))
        line = line[0].upper() + line[1:]
        if not line.endswith((".", "!", "?")):
            line += "."
        out.append("<%sc>%s" % (t, line))
    rnd.shuffle(out)
    return out


if __name__ == "__main__":
    lines = generate(6000)
    open(sys.argv[1] if len(sys.argv) > 1 else "corpus.txt", "w", newline="\n").write("\n".join(lines) + "\n")
    print(len(lines), "lines")
