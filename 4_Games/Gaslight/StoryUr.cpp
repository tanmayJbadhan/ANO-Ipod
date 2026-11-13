// Gaslight & Griffins - Ur-Lumen: "The Last King's Garden" (the Epic of
// Gilgamesh), the Collector, and the endings.
#include "Game.h"

namespace Gaslight {

const char *const STORY_UR = R"GGS(
#ur_arrive
+ur_arrived
@chapter Ur-Lumen|The Last King's Garden
-: The capital. Walls older than any empire, canals full of evening sky, and over it all a ziggurat lit from inside: the Museum of Last Things.
-: Tonight, under an eclipse, it opens for one guest. The Collector's invitation was waiting at the station. It's addressed to "my best student".
? juno_allied #ur_juno
@save
@end
#ur_juno
juno(3): Juno Castellane, reporting for duty. Unofficially. My father's in a cell in Skjold and I'm not a police officer any more. Let's go.
@save
@end

#juno_ur
juno(3): The front door's watched. Talk your way in, or fight your way in. I'll cover the canal side.
@end

#crowd_u
@chatter uc villager The walls of Ur-Lumen were old when the world was young.
@end
#lamassu
-: A winged bull of the old gate, with a man's bearded face. Pressed into its flank, worn almost smooth: "HE WHO SAW THE DEEP".
@end

#siduri
? fav_siduri #sid_after
siduri(1): Welcome to the edge of the world, little traveller. Everyone who comes to my door is looking for a way not to die. Are you?
> [check charm 13] "No. I'm looking for a way to let someone go." #sid_ok #sid_fail
> "I'm looking for a man in a clay mask." #sid_mask
#sid_ok
siduri(1): Ah. Then sit, and drink, and listen. I said this to a king once, and he didn't hear me.
siduri: "Fill your belly. Dance and be merry. Love the child who holds your hand. This is the lot of mortals." That's all. That's the whole secret.
@clue siduri_words
@favour siduri charm
siduri(3): Your teacher sat where you sit. He didn't hear me either. Maybe he'll hear it from you.
@end
#sid_fail
siduri(4): Everyone says that, little one. Come back when you mean it.
@end
#sid_mask
siduri(3): He comes in the evenings. He orders two cups and drinks one. The other's for a friend who isn't there.
@end
#sid_after
siduri(1): Fill your belly. Dance. Love whoever's beside you. Go on.
@end

#gate_curator
? ziggurat_open #gc_open
curator(2): The Museum of Last Things is closed to the public. Invitation only.
> [check disguise 12] Pull up your collar and flash the clay pin from Crewe's tablet #gc_disguise #gc_caught
> "I have an invitation. It's addressed to his best student." #gc_invite
#gc_disguise
curator(4): Ah. Brother. Late again. Go on up.
+ziggurat_open
@end
#gc_caught
curator(2): That's not one of ours. Guards!
@battle 26 #gc_beat #gc_lose
#gc_beat
+ziggurat_open
-: The Curator flees up the steps. The door's open.
@end
#gc_lose
curator(2): Come back when you've grown up, child.
@end
#gc_invite
curator(1): ...So it is. He said you'd come the honest way. Go up. He's been waiting a year.
+ziggurat_open
@end
#gc_open
-: The doors stand open.
@end

#tr_ur1
? beat_tr_ur1 #tru_after
-: A canal guard with a bronze helmet bars the garden path.
@battle 25 #tru_win
#tru_win
+beat_tr_ur1
-: "You'll do. Mind the eclipse."
@end
#tru_after
-: The guard salutes.
@end

#curator1
? beat_curator1 #cur_after
curator(2): The Curator of Sound. I kept the Sisters' song. Shall I play it for you, on the rocks of your heart?
@battle 26 #cur1_win
#cur1_win
+beat_curator1
@goto cur_check
#curator2
? beat_curator2 #cur_after
curator(2): The Curator of Scales. I kept the Feather. It weighs nothing now.
@battle 27 #cur2_win
#cur2_win
+beat_curator2
@goto cur_check
#curator3
? beat_curator3 #cur_after
curator(2): The Curator of Thorns. I kept the green light from the Chapel. It's dying in its jar.
@battle 28 #cur3_win
#cur3_win
+beat_curator3
@goto cur_check
#curator4
? beat_curator4 #cur_after
curator(2): The Curator of Wings. I kept the cage. The raven got out. I kept the cage anyway.
@battle 29 #cur4_win
#cur4_win
+beat_curator4
#cur_check
? !beat_curator1 #cur_more
? !beat_curator2 #cur_more
? !beat_curator3 #cur_more
? !beat_curator4 #cur_more
+curators_beaten
-: The last Curator falls back. Behind the altar, the stair down to the Sanctum stands open. Something below is breathing.
@save
@end
#cur_more
-: More masks in the dark. The stair down won't open while they stand.
@end
#cur_after
-: The Curator kneels, mask in hand. Under it: an ordinary, tired face.
@end
#sanctum_locked
-: The stair is barred with a gate of clay and bronze. The Curators hold its keys.
@walk player S 1
@end

#exhibit_song
-: A glass jar with a silver thread of light inside, singing very faintly: "turn back, turn back". The Sisters' stolen song, or what's left of a copy.
@end
#exhibit_feather
-: A white feather on a cushion, lighter than air. The real Feather of Ma'at, bought from a Sandmere pawnshop with a clay tablet.
@end

#collector
? collector_done #col_after
? !tide_seal #col_start
? !feather_seal #col_start
? !girdle_seal #col_start
? !raven_seal #col_start
? fav_utnapishtim #col_start
-: As you step into the Sanctum the four Warden seals in your pocket grow warm. An old man rises from the pool, dripping, and smiles.
-: Utnapishtim, who survived the flood and was given forever, and found it lonely. "Remember the flood," he says, and is gone.
@favour utnapishtim memory
@teach flood
#col_start
@chapter The Collector|The interrogation
-: A pool under the ziggurat, lit from below. Growing in it, a single plant with leaves like knives: the Plant of the Deep.
-: And beside it, a shape you'd know anywhere, even masked. Behind him, flickering like a candle, a great maned beast made of light.
collector(2): Wren. You came the long way round. I hoped you would.
-: He pushes the clay mask up onto his forehead. Professor Ambrose Vane, a year older and ten years thinner.
vane(3): This is Kidu. You never met him. He died for me in the steppe, before you were born. I've been trying to bring him home ever since.
vane: The Sisters' voice to call him. The Feather to weigh his heart and find it worthy. The green wood's regrowth for his body. And Memory, so he'd know me.
vane(2): Tonight the plant blooms. Tell me why I shouldn't let it.
#col_q
@interro vane
vane(2): Nothing more to say? Then we finish it the old way.
> Keep questioning him #col_q
> "Partners out." #vane_fight
#vane_fight
@battle 30 #final_win #final_lose
@end
#vane_b1
vane(3): ...Twenty-one sailors. I told Crewe it was a test. I didn't think he'd... No. I knew. I didn't let myself know.
@interro vane
@end
#vane_b2
vane(3): Warning, not luring. Then I stole the only thing keeping ships off the Rock. I'm a scholar. I should have read my own sources.
@interro vane
@end
#vane_b3
vane(5): You remembered. Muninn gave it back. Two came, one returned. I wanted you to know I wasn't dead. I just couldn't let you stop me.
@interro vane
@end
#vane_b4
vane(3): Siduri said that to me too. Fill your belly, dance, love the child who holds your hand.
vane(3): And I had a child who held my hand. You. And I left you at a waterfall.
@interro vane
@end
#vane_broken
+vane_broken
vane(3): ...Then show me, Wren. Show me I'm wrong in the only way I still believe. Partners out.
@battle 30 #final_win #final_lose
#final_lose
vane(3): Not yet, then. Rest. Come back. I'll wait. I'm very good at waiting.
@end
#final_win
+collector_done
@goto ending
#col_after
@goto ending

#ending
? wrong_accusations>=2 #end_dark
? favours<4 #end_dark
? !case1 #end_arrest
? !case2 #end_arrest
? !case3 #end_arrest
? !case4 #end_arrest
? favours<8 #end_arrest
? !fav_siduri #end_arrest
? !juno_allied #end_arrest
? fragments>=20 #end_serpent
@goto end_letgo

#end_dark
-: Vane's creatures fall. But you've lost too much on the way: wrong names, broken favours, friends who walked away.
-: He steps past you into the pool. The plant blooms. Kidu opens his eyes, and they are empty. Every myth-being in four towns goes quiet.
vane(3): ...This isn't him. This isn't him at all.
@ending 101
@end
#end_arrest
-: Vane sits down on the edge of the pool, very carefully, like an old man.
vane(3): You win, Wren. Juno, or whoever, take me in. I'll go quietly. I'm tired.
-: The powers return to their towns one by one: a song over Fogmouth, a feather in Sandmere, green moss in Greenwold, a raven over Skjold.
@ending 102
@end
#end_letgo
-: You don't arrest him yet. You sit beside him, by the pool.
wren: Siduri told me what she told you. Fill your belly. Dance. Love the one beside you. He's beside you, Professor. He always was.
-: Vane looks at Kidu for a long time. Then he picks the Plant of the Deep, and holds it out over the water.
-: A serpent rises from the pool, takes the plant gently in its mouth, and sinks. Kidu's light turns to look at Vane, and then it's gone. Not dark. Just gone.
vane(3): Goodbye, old friend.
-: He holds out his wrists to Juno. A small light, warm as a candle, settles on your shoulder: Kidu's spirit, who has decided to stay with someone who needs looking after.
@mon 59 50
@ending 103
@end
#end_serpent
@goto end_letgo2
#end_letgo2
-: You sit beside him by the pool. You tell him Siduri's words, and he listens this time. He gives the plant to the serpent, and says goodbye to Kidu.
-: Then the serpent rises again. It sheds its old skin on the stones, bright as new, and every stolen power pours out of it like light out of a lamp.
-: Song over Fogmouth. A feather in Sandmere. Green moss in Greenwold. Two ravens over Skjold. And twenty pages of old stories, glowing in your casebook, all at once.
@mon 59 50
@ending 104
@end

#frag_ur1
+frag_ur1
@add fragments 1
-: A page on the city wall, warm from the day's sun.
@card 16
@end
#frag_ur2
+frag_ur2
@add fragments 1
-: A page in the gardens, under a cypress.
@card 17
@end
#frag_ur3
+frag_ur3
@add fragments 1
-: A page in the Museum, pinned under glass like an exhibit.
@card 18
@end
#frag_ur4
+frag_ur4
@add fragments 1
-: A page by the pool, damp but glowing.
@card 19
@end
)GGS";

} // namespace Gaslight
