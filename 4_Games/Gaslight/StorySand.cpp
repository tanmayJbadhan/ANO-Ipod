// Gaslight & Griffins - Sandmere: "The Featherless Scale" (the Book of the
// Dead), plus the conductor and the innkeepers every town shares.
#include "Game.h"

namespace Gaslight {

const char *const STORY_SAND = R"GGS(
#travel
? !act1_done #travel_closed
conductor: All aboard the Grand Southern! Where to, detective?
> [!town==1] Fogmouth, on the sea #go_fog
> [town_sand_open] Sandmere, in the desert #go_sand
> [town_green_open] Greenwold, in the forest #go_green
> [town_skjold_open] Skjold, on the fjord #go_skjold
> [town_ur_open] Ur-Lumen, the capital #go_ur
> Not today #travel_bye
#travel_closed
conductor: No trains till the line's clear, miss. Fog on the points.
@end
#travel_bye
conductor: Right you are.
@end
#go_fog
@fade out
@town 1
@warp town 32 14 W
@fade in
@end
#go_sand
@fade out
@town 2
@warp sand 26 14 W
@fade in
? !sand_arrived #sand_arrive
@end
#go_green
@fade out
@town 3
@warp green 28 15 W
@fade in
? !green_arrived #green_arrive
@end
#go_skjold
@fade out
@town 4
@warp skjold 28 8 W
@fade in
? !skjold_arrived #skjold_arrive
@end
#go_ur
@fade out
@town 5
@warp ur 19 20 W
@fade in
? !ur_arrived #ur_arrive
@end

#innkeeper
innkeeper(1): Welcome, traveller. A bed, a meal, a bath for your creatures?
> Rest (heal your team) #inn_rest
> Save the case #inn_save
> Never mind #inn_bye
#inn_rest
@heal
-: You sleep like the dead, and wake feeling alive. Your team is rested.
@end
#inn_save
@save
@end
#inn_bye
innkeeper: Mind how you go.
@end

#sand_arrive
+sand_arrived
+juno_sand_show
@chapter Sandmere|The Featherless Scale
-: Heat like a hand on your neck. Sandmere glitters: sandstone, brass, and the Sphinx in the square, older than anything.
-: A telegram was waiting at the station. From Dr. Lune: "Hall of Weighing robbed last night. They took the Feather of Ma'at. He's here, Wren."
@save
@end

#juno_sand
? juno_sand_beaten #juno_sand_after
juno(4): You again. My father sent me a telegram this morning. "Leave the Feather to the locals. Come home."
juno(5): He's never told me to leave a case before. Not once.
> "Then why are you still here?" #juno_sand_why
> "Maybe he's protecting you." #juno_sand_protect
#juno_sand_why
juno(2): Because I don't take orders from telegrams. Partners out, apprentice. I need to hit something.
@goto juno_sand_fight
#juno_sand_protect
+juno_trust1
juno(3): ...Maybe. Or protecting someone else. Never mind. Partners out.
#juno_sand_fight
@battle 14 #juno_sand_win #juno_sand_lose
#juno_sand_win
+juno_sand_beaten
juno(3): You've got better. Don't tell anyone I said that.
@end
#juno_sand_lose
+juno_sand_beaten
@heal
juno(4): Still slow. Go on, then. The Hall's that way.
@end
#juno_sand_after
juno: The guard at the Hall knows more than he's saying. They always do.
@end

#museum_guard
? case2_open #mg_after
@set town 2
+case2_open
museum: You're the Fogmouth detective? Thank the gods. The Feather of Ma'at is gone. Stolen at the gala last night.
museum: We found the curator, Kamal Idris, drugged, inside the sarcophagus room. The door was sealed from the inside. The inside!
@clue feather_gone
museum(5): And the Chief Inspector telegraphed from the capital. "Leave it to my daughter." His daughter's twelve years old, I thought.
> "She's eighteen, and she's good. So am I." #mg_after
> "Who else was at the gala?" #mg_who
#mg_who
museum: Lady Neferet Ashwood, who wanted to buy the Feather. Tomas Reyes, the guide. And Sefa Okon, who restores our gold.
@end
#mg_after
? case2_closed #sa_closed
museum: Any progress? The sarcophagus room is at the back. Mind the vent: it whistles.
> "I know who did it." #mg_accuse
> "Still looking." #sand_bye
#mg_accuse
? ded_c2<3 #mg_notyet
@goto sand_accuse
#mg_notyet
LOGIC: Not yet. Pin more of this on the board first. You need the whole shape of it.
@end

#sarcophagus
? clue:resin_seal #sarc_done
-: Two painted sarcophagi. The lid of one is scored where it was levered open to free Kamal.
> [check obs 10] Run a finger round the seal #sarc_ok #sarc_fail
> Leave it #sand_bye
#sarc_ok
OBSERVATION: The resin round the lid is soft. Fresh. And the scrape marks are on the outside. Someone sealed him in from out here.
@clue resin_seal
@end
#sarc_fail
OBSERVATION: Old paint, older stone. You can't tell new from ancient here.
@end
#sarc_done
-: Fresh resin, sealed from outside. The locked room isn't locked at all.
@end
#sand_bye
@end
#vent
? clue:gold_leaf #vent_done
-: A bronze grille high in the wall. Cold air sighs out of it, which is strange in a desert.
> [check obs 10] Climb up and look inside #vent_ok #vent_fail
> Leave it #sand_bye
#vent_ok
OBSERVATION: Flecks of {gold leaf} on the lip of the duct, and a long scrape in the dust. Someone slim crawled through here.
@clue gold_leaf
@end
#vent_fail
OBSERVATION: Dark. Dusty. You sneeze, and something skitters away.
@end
#vent_done
-: Gold leaf, and the scrape of someone crawling.
@end
#feather_case
? clue:replica_case #case_done
-: The empty case. A velvet cradle where the Feather of Ma'at lay for a hundred years.
> [check lore 9] Read the cradle's shape #case_ok #case_fail
> Leave it #sand_bye
#case_ok
LORE: The cradle was cut for a long ostrich plume. But the dents in the velvet are short and stiff: {a copy}, light and gilded, has lain here for a year at least.
@clue replica_case
@end
#case_fail
LORE: A feather. A case. A great deal of velvet. You don't know enough to read it.
@end
#case_done
-: The cradle remembers a copy, not the real Feather.
@end

#kamal
? case2_closed #kamal_after
? !case2_open #kamal_closed
kamal(5): Kamal Idris, curator. Forgive me, I'm still unsteady. They drugged my tea, and when I woke I was in the dark, with the dead.
> "Tell me about the Feather." #kamal_interro
> [check charm 9] "You must have been terrified." #kamal_charm_ok #kamal_charm_fail
> "Rest. I'll come back." #sand_bye
#kamal_closed
kamal: The Hall is closed today. Some trouble. Good day.
@end
#kamal_interro
@interro kamal
@end
#kamal_charm_ok
kamal(3): I was. But not of the dark. Of what they'll find, when they look properly at the case.
CHARM: He's afraid of the case, not the thief. Interesting.
@end
#kamal_charm_fail
kamal(2): I'm quite well, thank you.
@end
#kamal_b1
kamal(5): Genuine. Of course it was genuine. What are you suggesting?
@interro kamal
@end
#kamal_b2
kamal(3): ...Sefa. Sefa sealed me in. I asked her to. It was meant to look like a robbery, so no one would look at the case too closely.
kamal: I pawned the real Feather a year ago. My daughter was ill. I meant to buy it back. I swear I meant to.
+kamal_confessed
@end
#kamal_desk
? clue:pawn_ticket #kd_done
-: Kamal's desk. Bills, more bills, a photograph of a girl in a hospital bed.
> [check logic 10] Sort the papers by date #kd_ok #kd_fail
> Leave it #sand_bye
#kd_ok
LOGIC: Under the bills: a {pawn ticket}. "One antique feather, gilt mount. Redeemable in twelve months." Dated a year ago. Due last week.
@clue pawn_ticket
@end
#kd_fail
LOGIC: Debts upon debts. You can't make a pattern of them.
@end
#kd_done
-: A pawn ticket for one antique feather, a year old.
@end

#sefa
? case2_closed #sefa_after
? !case2_open #sefa_closed
sefa(3): Sefa Okon. I restore gold. I wasn't at the Hall last night, if that's what you've come to ask.
> "Then where were you?" #sefa_interro
> [clue:brother_debt] "Your brother. Where is he?" #sefa_brother
> "Good day." #sand_bye
#sefa_closed
sefa: Workshop's closed.
@end
#sefa_interro
@interro sefa
@end
#sefa_brother
sefa(3): Somewhere safe, if I did as I was told. Please. Don't push this.
@end
#sefa_b1
sefa(5): ...The camel gate. Yes. I went out by the camel gate at dawn. Is that a crime?
@interro sefa
@end
#sefa_b2
sefa(3): Gold leaf. Of course it was gold leaf. It's on everything I touch.
@interro sefa
@end
#sefa_b3
sefa(3): A man bought my brother's debts. He said: bring me the Feather, and your brother walks free. He wore a clay mask.
@interro sefa
@end
#sefa_broken
sefa(3): Kamal asked me to seal him in, for his staged robbery. I used the chance. I took the feather from the case and crawled out through the vent.
sefa: But when I gave it to the masked man, he laughed. "A copy," he said. "The real one's at the pawnbroker. Never mind. I'll fetch it myself."
+sefa_confessed
@add lies_caught 0
@end
#sefa_bench
? clue:brother_debt #sb_done
-: Sefa's bench. Brushes, gold leaf in tissue books, a magnifying lamp.
> [check obs 9] Look under the tissue #sb_ok #sb_fail
> Leave it #sand_bye
#sb_ok
OBSERVATION: A letter, folded small. "Your brother's debts are ours now. Bring the Feather and he goes free. - C." Sealed with a {clay stamp} of wedge-shaped marks.
@clue brother_debt
MEMORY: That curled "e" again. You've seen it in a notebook you carry everywhere.
@end
#sb_fail
OBSERVATION: Gold everywhere. Your eyes ache.
@end
#sb_done
-: The letter: "Bring the Feather and he goes free. - C."
@end
#sefa_barrel
? clue:resin_pot #sbr_done
-: A barrel of sand, and a pot sitting in it to keep cool.
> [check obs 8] Look at the pot #sbr_ok #sbr_fail
> Leave it #sand_bye
#sbr_ok
OBSERVATION: Sealing {resin}, the same amber as the sarcophagus seal. The pot's still tacky. Used in the last day.
@clue resin_pot
@end
#sbr_fail
OBSERVATION: Sand. A pot. Thrilling.
@end
#sbr_done
-: Fresh resin, like the sarcophagus seal.
@end

#neferet
? clue:photo_plate #nef_after
neferet(4): Lady Neferet Ashwood. Yes, I wanted the Feather. I've wanted it for twenty years. Wanting isn't stealing, little detective.
> [check charm 10] "Then prove you were at the gala all night." #nef_ok #nef_fail
> "Good day, my lady." #sand_bye
#nef_ok
neferet(1): Clever thing. The gala photographer took a plate at midnight. There I am, in the middle, with a glass of terrible wine.
@clue photo_plate
@end
#nef_fail
neferet(4): Prove I wasn't. That's how it works, isn't it? Good day.
@end
#nef_after
neferet(1): Find my Feather, and I'll fund your little detective agency for a year.
@end
#tomas
? clue:sphinx_passage #tomas_after
tomas(1): Tomas Reyes, best guide in Sandmere! The Sphinx, the tombs, the secret passages, only two shillings.
tomas: Ha, don't look like that. There ARE passages. Under the square. But the Sphinx keeps the door, and she only opens for a riddle answered.
@end
#tomas_after
tomas(1): You answered her riddles? Then you're cleverer than every tourist I've ever had.
@end
#sand_merchant
merchant(1): Dates! Water! Lanterns! Iced buns, even, though they melt.
> [money>=10] Iced bun - 10 shillings #sm_bun
> [money>=30] Tonic - 30 shillings #sm_tonic
> [money>=80] Silver lantern - 80 shillings #sm_silver
> [money>=90] Strong draught - 90 shillings #sm_draught
> Nothing #sm_bye
#sm_bun
@money -10
@give bun 1
@goto sm_bye
#sm_tonic
@money -30
@give tonic 1
@goto sm_bye
#sm_silver
@money -80
@give silver 1
@goto sm_bye
#sm_draught
@money -90
@give draught 1
@goto sm_bye
#sm_bye
merchant(1): May the sun be kind.
@end

#tally
? clue:camel_gate #tally_after
tally(1): You're the one from the coast? I'm Tally. I see everything in this town. Everything. For a bun.
> [item:bun>=1] Give Tally an iced bun #tally_bun
> "Not now." #sand_bye
#tally_bun
@give bun -1
tally(1): Mm. Melting already. Right: dawn, this morning. A woman went out by the camel gate with a satchel. She smelled of {varnish}. Walked like her feet hurt.
@clue camel_gate
@end
#tally_after
@chatter sr tally Varnish. Feet. That's all I've got. Unless you've got more buns.
@end
#crowd_s
@chatter sc merchant The heat this noon could melt a brass kettle.
@end

#sphinx
? fav_sphinx #sphinx_after
-: The Sphinx of Sandmere. Lion's body, calm stone face. The air around her hums, and then she speaks.
@set riddle 0
sphinx: SMALL ONE. ANSWER THREE, AND I OPEN WHAT LIES BENEATH. FAIL, AND GO AWAY.
sphinx: FIRST. WHAT WALKS ON FOUR IN THE MORNING, TWO AT NOON, THREE IN THE EVENING?
> "A creature with a broken leg." #sph1_no
> "A person: crawling, walking, then with a stick." #sph1_yes
> [check lore 8] Wait. Think about the old story. #sph1_yes #sph1_no
#sph1_yes
@add riddle 1
#sph1_no
sphinx: SECOND. THE MORE YOU TAKE, THE MORE YOU LEAVE BEHIND. WHAT ARE THEY?
> "Footprints." #sph2_yes
> "Years." #sph2_no
> [check logic 9] Work it out #sph2_yes #sph2_no
#sph2_yes
@add riddle 1
#sph2_no
sphinx: THIRD. WHO TAUGHT YOU TO READ FOOTPRINTS, AND LEFT NONE OF HIS OWN AT THE FALLS?
> "Professor Vane." #sph3_yes
> "No one. I taught myself." #sph3_no
#sph3_yes
@add riddle 1
#sph3_no
? riddle>=2 #sph_pass
sphinx: NOT ENOUGH. GO AWAY, AND COME BACK WISER.
@end
#sph_pass
-: Stone grinds on stone. At the Sphinx's feet a slab slides back on a stair going down into the cool dark.
@favour sphinx logic
@teach riddle_lock
@clue sphinx_passage
sphinx: THE PASSAGE RUNS TO THE HALL. A SLIM ONE CRAWLED IT LAST NIGHT, CARRYING A COPY. AND A MAN IN A MASK STOOD WHERE YOU STAND, AND WEPT.
@end
#sphinx_after
sphinx: THE MAN WHO WEPT WILL WEIGH HIS OWN HEART SOON. YOU WILL HOLD THE SCALE.
@end

#imani
? feather_seal #imani_after
? ded_c2<2 #imani_notyet
imani(1): Imani Sato, Warden of the Glassworks. I make glass from sand and fire. Let's see what you make from pressure.
> "Let's battle." #imani_fight
> "Not yet." #sand_bye
#imani_notyet
imani(1): Come back when you've got this case half solved. The heat makes some people sloppy.
@end
#imani_fight
@battle 13 #imani_win #imani_lose
#imani_win
+feather_seal
@money 300
imani(1): Beautiful. Take the Feather Seal. It's glass, but don't drop it.
@save
@end
#imani_lose
imani: Cool down. Come back.
@end
#imani_after
imani(1): The furnaces never sleep, and neither do I.
@end

#tr_sand1
? beat_tr_sand1 #trs1_after
-: A runner in a sand-coloured scarf blocks the way.
@battle 11 #trs1_win
#trs1_win
+beat_tr_sand1
-: "Fast as the dunes," she says, grinning. "Faster, even."
@end
#trs1_after
-: The runner waves and jogs off into the heat shimmer.
@end
#tr_sand2
? beat_tr_sand2 #trs2_after
-: A scholar with a sun hat and a notebook full of beetles squints at you.
@battle 12 #trs2_win
#trs2_win
+beat_tr_sand2
-: "Fascinating technique. Dreadful manners. I'll write that down."
@end
#trs2_after
-: The scholar is sketching a beetle.
@end

#sand_accuse
? case2_closed #sa_closed
? !case2_open #sa_closed
museum: You know who did it? Say it, and I'll fetch the constables.
@accuse sand
@end
#sa_closed
museum: The Hall is quiet again. Thank you.
@end

#acc_s_neferet
-: Lady Neferet laughs in your face and produces her photograph. Half of Sandmere was in it. You weren't.
@composure -1
@end
#acc_s_tomas
-: Tomas is arrested at the Sphinx in front of forty tourists. He's released by evening, ruined. The real thief left days ago.
+tomas_ruined
@composure -1
@end
#acc_s_kamal
-: Kamal confesses to pawning the Feather and is taken away weeping. But the thief who crawled through the vent is never named.
LOGIC: Half a truth. Somebody else went through that vent.
@composure -1
@end
#acc_s_sefa
-: Sefa is taken in. She tells them about the masked man, and nobody believes her. Kamal keeps his secret, and his job.
LOGIC: She didn't do it alone. Who asked her to seal the room?
@composure -1
@end
#acc_s_weak
-: You name them both, but you can't say why or how. The magistrate sends everyone home. The masked man smiles somewhere.
@composure -1
@end
#acc_s_right
@chapter The Featherless Scale|Accusation
-: Two thieves. Kamal, who pawned the Feather and staged a robbery to hide it. Sefa, who was forced to steal it, and stole a copy.
-: And a third, who walked into the pawnbroker's and paid for the real Feather with a clay tablet.
+case2_closed
@set case2 1
@money 400
-: As the constables come, a figure in a clay mask watches from the Hall's roof. Then it drops down, and its creatures come with it.
@battle 15 #sand_curator_win #sand_curator_win
#sand_curator_win
-: The Curator flees across the rooftops, but drops a scrap of paper: "Greenwold. The Knight's head. Then the ravens."
-: In the empty Hall, a woman made of white feathers stands beside the empty case and holds out a scale.
-: Ma'at's Feather-Bearer. "You weighed them truly," she says. "Weigh the masked man truly too, when you meet him."
@favour feather memory
@teach truth_weight
? lies_caught>=5 #sand_ammit
@goto sand_done
#sand_ammit
-: Something with a crocodile's head and a lion's mane sits up in the shadows, licking its lips, then bows.
-: Ammit, the Devourer of hearts. It has been enjoying your interrogations. It gives you its hunger.
@favour ammit obs
@teach devour
#sand_done
? case3_closed #open_skjold
@save
@end

#open_skjold
+town_skjold_open
-: A telegram from Dr. Lune: "Two towns, two powers taken. The next is memory itself. Go north, Wren: Skjold, on the fjord."
@save
@end

#frag_sand1
+frag_sand1
@add fragments 1
-: A glowing page lies at the oasis edge, dry as bone.
@card 4
@end
#frag_sand2
+frag_sand2
@add fragments 1
-: A page caught in a palm's roots, warm from the sun.
@card 5
@end
#frag_sand3
+frag_sand3
@add fragments 1
-: A page tucked behind a display case, glowing gold.
@card 6
@end
#frag_sand4
+frag_sand4
@add fragments 1
-: A page beside the furnace that the heat can't burn.
@card 7
@end
)GGS";

} // namespace Gaslight
