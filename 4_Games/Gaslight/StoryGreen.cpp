// Gaslight & Griffins - Greenwold: "A Year and a Day" (Sir Gawain and the
// Green Knight).
#include "Game.h"

namespace Gaslight {

const char *const STORY_GREEN = R"GGS(
#green_arrive
+green_arrived
+juno_green_show
+case3_open
@chapter Greenwold|A Year and a Day
-: Oaks older than the railway, and a quiet that listens back. Somewhere deeper in, an axe is being sharpened. Slowly.
-: A year ago, at Hautdesert Lodge, a guest named Gareth Lyle took a stranger's bargain: "Strike me now, and I strike you in a year."
-: The stranger was all green, and when Gareth cut off his head, he picked it up and rode away. The year was up yesterday. Gareth is gone.
@clue missing_guest
@save
@end

#bertram
? case3_closed #bert_closed
bertram(1): Lord Bertram Hautdesert, welcome, welcome! You're here about poor Gareth. Terrible thing. Come up to the Lodge, my wife will feed you.
> "Tell me about the bargain." #bert_bargain
> "I know who did it." #bert_accuse
> "Later." #green_bye
#bert_bargain
bertram: Last Yule a green giant rode into my hall and offered his game. Gareth swung. The head came off. The giant laughed and picked it up.
bertram(1): Since then we've played my little game of trade every night: what you win in the day, you give me at supper. Gareth kept it well. Or so I thought.
@end
#bert_accuse
? ded_c3<3 #bert_notyet
bertram: You've found him? Say it, then, in front of the hearth.
@accuse green
@end
#bert_notyet
LOGIC: Not yet. You've got a giant, a girdle and a missing man. Pin them together on the board first.
@end
#bert_closed
bertram(1): A guest who sells a giant's head! I'll never live it down at the club.
@end
#bertram_hall
? case3_closed #bert_closed
bertram(1): Sit, sit! The trade game tonight: you tell me what you found today, and I'll tell you what I hunted. Fair's fair.
> [clue:empty_chapel] "I found a rusted axe at the Green Chapel." #bh_axe
> "I'd rather ask you a few things." #bh_interro
> "Not tonight." #green_bye
#bh_axe
bertram(5): Rusted? His axe? Then he's... weaker. Oh dear. Oh, that's not good at all.
@end
#bh_interro
@interro bertram
@end
#bertram_b1
bertram(5): ...All right. The costume's mine. I wore it once, in the summer, to frighten the vicar. It was a JOKE. The real one is twice my size.
@clue costume
@end
#green_bye
@end

#rosalind
? case3_closed #ros_closed
rosalind(4): Lady Rosalind. My husband likes his games. I like mine quieter.
> "You and Gareth were close." #ros_interro
> [check charm 11] "You're frightened for him." #ros_ok #ros_fail
> "Good evening." #green_bye
#ros_interro
@interro rosalind
@end
#ros_ok
rosalind(3): ...I gave him a girdle. Green silk, with a charm sewn into it: no blade can kill the one who wears it. I didn't tell Bertram. It wasn't part of the game.
@clue girdle
> "Your secret's safe with me." #ros_keep
> "Your husband should know." #ros_tell
#ros_keep
+ros_kept
-: Something in her face unknots. "Thank you," she says. "It's an old kindness, keeping a lady's secret. Older than you know."
@favour lady disguise
@end
#ros_tell
rosalind(2): Then tell him. And see how long you're welcome here.
@end
#ros_fail
rosalind(4): I'm quite well. Do have some soup.
@end
#rosalind_b1
rosalind(3): Courtesy, and a girdle. Green silk. It was meant to save his life.
@clue girdle
@end
#ros_closed
rosalind(1): He's alive. That's more than he deserves, and exactly what I hoped.
@end

#trade_ledger
? clue:kept_back #tl_done
-: Lord Bertram's trade-game book. Every evening, in his big hand: "Gareth gave me..." and "I gave Gareth..."
> [check logic 10] Compare what Gareth won with what he gave #tl_ok #tl_fail
> Leave it #green_bye
#tl_ok
LOGIC: Three evenings running, the week before he vanished, Gareth "won" one thing more than he gave. A ring of keys. Bread. A spade. He was {keeping things back}. Provisions.
@clue kept_back
@end
#tl_fail
LOGIC: Pheasants. A hare. Another pheasant. You lose the thread.
@end
#tl_done
-: Keys, bread, a spade. He was packing.
@end
#costume_chest
? clue:costume #cc_done
-: A cedar chest, padlocked. The padlock is cheap.
> [check obs 9] Look through the keyhole with your lamp #cc_ok #cc_fail
> Leave it #green_bye
#cc_ok
OBSERVATION: Green felt. A holly crown. A papier-mache {green head} with a hinged jaw. A costume, and not a big one.
@clue costume
@end
#cc_fail
OBSERVATION: Dark wood, dark inside. Mothballs.
@end
#cc_done
-: A green costume. Too small for a giant.
@end

#moth
? clue:bread_seen #moth_after
moth(1): You're the coast detective! I'm Moth. I live in the hollow oak, mostly. Got a bun?
> [item:bun>=1] Give Moth an iced bun #moth_bun
> "Not now." #green_bye
#moth_bun
@give bun -1
moth(1): Ooh. Right. That "missing" man? Gareth? I saw him two days ago. Buying bread off the baker's cart, in a hood. Thought he was a ghost.
@clue bread_seen
+gareth_found
moth: He's hiding up in the north-east wood, by the old stones. Don't say I told you.
moth(5): And there was a carriage on the chapel road last week. Black, with a police crest. It waited all night.
+castellane_carriage
@end
#moth_after
@chatter gr moth A ghost who buys bread. That's not how ghosts work.
@end
#crowd_g
@chatter gc villager Something big walks in the wood after dark.
@end

#morgana
? fav_morgan #morg_after
morgana(4): Morgana Fay. I know every root in Greenwold, and most of the secrets above them.
> [clue:girdle] "Lady Rosalind's girdle. Did you make its charm?" #morg_girdle
> "Is there anything I can do for you?" #morg_herbs
> "Good day." #green_bye
#morg_girdle
morgana(2): That silk? Pretty rubbish. No, the charm Gareth carries now isn't mine, and it isn't silk. He showed it me: {a clay disc}, pressed with wedges. Real power in it. Stolen power.
@clue collector_charm
LORE: Wedge-shaped writing again. Fogmouth's tablet. Sandmere's seal. Now a charm against death.
@end
#morg_herbs
? herb1 #morg_check
morgana: Bring me three things the wood keeps: mere-mint from the north wood, black bryony from the east, and moonwort by the far trees in the south.
@end
#morg_check
? !herb2 #morg_more
? !herb3 #morg_more
-: You hand over three small bundles. Morgana sniffs each one, then smiles like a cat.
@favour morgan lore
@teach glamour_dust
morgana(1): Good hands. Take the old glamour for your creature: let your foes see what you want them to see.
@end
#morg_more
morgana: Two more to find. The wood is patient. Be patient too.
@end
#morg_after
morgana(1): The Knight grows weaker every day his head doesn't grow back. Hurry.
> [clue:girdle] "Lady Rosalind's girdle. Did you make its charm?" #morg_girdle
> "Good day." #green_bye
@end
#herb1
+herb1
-: Mere-mint, crushed under your boot. It smells of cold water. You pick a bundle.
@end
#herb2
+herb2
-: Black bryony, curling round a dead branch. You cut a length.
@end
#herb3
+herb3
-: Moonwort, in the shade where the light never reaches. You lift it gently.
@end

#green_chapel
? clue:empty_chapel #gc_done
-: The Green Chapel: a mound with a mouth. Inside, a stone shelf where the Knight's axe should hang.
-: The axe is there, but it's gone red with rust, and the moss inside has died in a ring around it.
@clue empty_chapel
LORE: In the old poem the Green Knight's head grew back because the green wood fed him. Something has cut him off from it.
@end
#gc_done
-: The rusted axe. The dead moss.
@end

#greenknight
? fav_knight #gk_after
? gk_fought #gk_fought
knight(2): YOU. SMALL ONE FROM THE SEA. HAVE YOU COME TO STRIKE, OR TO BE STRUCK?
knight(3): MY YEAR IS UP AND MY HEAD WON'T GROW. THE MAN WHO STRUCK ME SOLD WHAT WAS MINE. I WILL HAVE IT BACK, OR HIS NECK.
> [check charm 12] "Let me find him. A trial, not an axe." #gk_talk #gk_angry
> "Then fight me first." #gk_fight
#gk_talk
knight(1): ...A TRIAL. HA. THE OLD WAYS WERE KINDER THAN YOU'D THINK, SMALL ONE. VERY WELL. BRING HIM TO THE CHAPEL, AND THE CHARM WITH HIM.
@clue knight_weak
@favour knight charm
@teach regrowth
@end
#gk_angry
knight(2): WORDS. ALWAYS WORDS. COME BACK WITH SOMETHING STRONGER.
@end
#gk_fight
+gk_fought
@battle 19 #gk_won #gk_lost
#gk_won
@clue knight_weak
knight(3): STRONG. BUT STRENGTH ISN'T WHAT I NEED. FIND THE MAN.
@end
#gk_lost
-gk_fought
knight(2): GO HOME, SMALL ONE.
@end
#gk_fought
? !clue:knight_weak #greenknight_again
knight(3): FIND THE MAN.
@end
#greenknight_again
knight(2): BACK AGAIN, SMALL ONE?
> [check charm 12] "Let me find him. A trial, not an axe." #gk_talk #gk_angry
> "Then fight me again." #gk_fight
> "Not yet." #green_bye
@end
#gk_after
knight(1): MY HEAD ITCHES. THAT IS A GOOD SIGN.
@end

#gareth
? case3_closed #gar_closed
gareth(5): Please. Don't tell him where I am. He'll take my head. He'll take it this time, I know it.
> "Tell me what happened." #gar_interro
> "Come with me to the Chapel." #green_bye
#gar_interro
@interro gareth
@end
#gareth_b1
gareth(5): ...Bread. Yes. I hid. I've been hiding in the north wood for three days.
@interro gareth
@end
#gareth_b2
gareth(3): A man in a clay mask came to me last month. He said: "Give me the green wood's gift, and wear this charm, and no blade will ever reach you."
gareth: I led him to the Chapel at night. He took something from the moss. A light. Green. He put it in a jar.
@interro gareth
@end
#gareth_broken
gareth(3): I was afraid. That's all. I'm a coward, and I sold a giant's life to save mine.
+gareth_confessed
@end
#gar_closed
gareth(3): The Knight let me live. He said a man who's honest at the end is worth more than his head.
@end

#acc_g_bertram
-: Lord Bertram is led away protesting about jokes and summer. The green figure still walks the wood that night.
@composure -1
@end
#acc_g_rosalind
-: Lady Rosalind's girdle is held up in court. It was a kindness, not a crime, and everyone knows it but the magistrate.
@composure -1
@end
#acc_g_morgana
-: They come for the herb-wife with torches. She's gone before they reach the hut, and so is Greenwold's medicine.
+morgana_gone
@composure -1
@end
#acc_g_weak
-: You name Gareth, but you can't show what he traded or why. He shrugs and disappears into the wood again.
@composure -1
@end
#acc_g_right
@chapter A Year and a Day|Accusation
-: Gareth Lyle, who took a giant's bargain and then couldn't face the return blow. Who faked his own vanishing, and paid the Collector in green light.
+case3_closed
+gareth_found
@set case3 1
@money 400
-: At the Green Chapel the Knight waits with his rusty axe. Gareth kneels. The axe comes down. And stops, a hair from his neck.
-: "A nick," says the Knight, "for the lie. Your head, for the truth." He laughs, and the moss around the Chapel turns green again.
? case2_closed #open_skjold
@save
@end

#ivy
? girdle_seal #ivy_after
? ded_c3<2 #ivy_notyet
ivy(2): Ser Ivy Marchbank, gamekeeper, Warden. I don't talk much. I battle.
> "So do I." #ivy_fight
> "Another time." #green_bye
#ivy_notyet
ivy(2): Come back when you've done some proper tracking.
@end
#ivy_fight
@battle 18 #ivy_win #ivy_lose
#ivy_win
+girdle_seal
@money 400
ivy(1): Good. The Girdle Seal. Wear it like you mean it.
@save
@end
#ivy_lose
ivy: Rest. Then again.
@end
#ivy_after
ivy: The deer are back near the Chapel. Something's mending.
@end

#juno_green
? juno_green_beaten #jg_after
juno(5): Before you ask: yes, I've been at the Chapel road. Yes, there was a police carriage. No, I didn't send for it.
? castellane_carriage #jg_carriage
juno: Partners out. I want to see if you've got better or just luckier.
@goto jg_fight
#jg_carriage
juno(3): It was my father's. I recognised the horses. He was HERE, Wren, and he told me he was in the capital.
> "I believe you. We'll find out why." #jg_trust
> "Maybe you're covering for him." #jg_doubt
#jg_trust
+juno_trust2
juno(3): ...Thanks. Don't make it weird. Partners out.
@goto jg_fight
#jg_doubt
+juno_doubted
juno(2): Of course you'd think that. Partners out, apprentice.
#jg_fight
@battle 20 #jg_win #jg_lose
#jg_win
+juno_green_beaten
juno(4): Fine. You win this round.
@end
#jg_lose
+juno_green_beaten
@heal
juno: Better luck next time.
@end
#jg_after
juno(5): If my father is part of this, I want to be the one who proves it.
@end

#tr_green1
? beat_tr_green1 #trg1_after
-: A forester with a long bow and a longer stare.
@battle 16 #trg1_win
#trg1_win
+beat_tr_green1
-: "The wood likes you. That's rare."
@end
#trg1_after
-: The forester nods.
@end
#tr_green2
? beat_tr_green2 #trg2_after
-: A huntress with bramble in her hair blocks the path.
@battle 17 #trg2_win
#trg2_win
+beat_tr_green2
-: "Swift. Go on, then."
@end
#trg2_after
-: The huntress is mending her snare.
@end

#frag_green1
+frag_green1
@add fragments 1
-: A glowing page floats on the mere like a lily pad.
@card 8
@end
#frag_green2
+frag_green2
@add fragments 1
-: A page caught in a hollow oak, green-gold.
@card 9
@end
#frag_green3
+frag_green3
@add fragments 1
-: A page tucked behind the Lodge hearth.
@card 10
@end
#frag_green4
+frag_green4
@add fragments 1
-: A page on Ser Ivy's floor, glowing under the straw.
@card 11
@end
)GGS";

} // namespace Gaslight
