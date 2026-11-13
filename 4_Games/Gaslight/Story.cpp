// Gaslight & Griffins - the story: the script, the clues, what links to
// what, the testimonies and the accusations. Hand-written; nothing here is
// generated. See Script.cpp for the little language.
#include "Game.h"
#include <stdio.h>

namespace Gaslight {

// --- clues ----------------------------------------------------------------------------------------
const ClueDef CLUES[] = {
    {"wreck_list", "Three wrecks", "The Kestrel, the Mary Ann and the Good Hope, all on {Wailing Rock}, all this month. Every crew swears they heard singing.", 1},
    {"song_cylinder", "The singing cylinder", "Marisol's song came off a {wax phonograph cylinder} a deckhand sold her. Faint stamp on the box: {C&Co.}", 1},
    {"phono_crate", "Marine phonographs", "In Crewe's office: a crate of {phonographs in sealed marine cases}, the kind that play in the rain.", 1},
    {"insurance", "Insured last month", "All three wrecked ships belonged to {Crewe & Co.} Each was insured for double, {a week before} the first wreck.", 1},
    {"needle", "Brass shavings", "Tiny curls of {brass needle} on the planks of the east jetty. Phonograph needles wear down like that.", 1},
    {"dark_rowboat", "A rowboat with no lamp", "Pip saw a rowboat slip out from the east jetty on the night of the third wreck. {No lamp.} A man in a {top hat}.", 1},
    {"wax_pocket", "Wax on a cuff", "A smear of {candle wax} on Augustin Crewe's cuff, rolled into little plugs in his pocket. The kind you put in your ears.", 1},
    {"wax_ears", "The wax of Odysseus", "Dr. Lune: Odysseus had his crew {stop their ears with wax} so the Sirens' song couldn't reach them.", 1},
    {"sisters_warned", "The Sisters' warning", "Grandmother Ysolde: the Sisters of Wailing Rock {sang to warn ships away}, never to lure them. Their song meant 'turn back'.", 1},
    {"ledger_altered", "A scraped ledger", "The harbour ledger has entries {scraped and rewritten} in fresher ink, on the nights of all three wrecks.", 1},
    {"brandy", "Brandy in the ledger", "Ottilie Brack let {French brandy} in without duty on wreck nights, when no one was watching the harbour.", 1},
    {"lamp_log", "A blank in the log", "The lighthouse log has {no entry} for the night of the third wreck. Silas never misses an entry.", 1},
    {"cave_truth", "The keeper's secret", "Silas let the lamp go dark to tend {Old One-Eye}, the cave's wounded guardian. He was below, not above.", 1},
    {"sirens_silent", "Silent sisters", "The Sisters no longer sing. Old One-Eye says {a man took their voices in a jar}.", 1},
    {"clay_mask", "A clay mask", "Old One-Eye saw the thief: {a grey coat, a pale clay mask}, and a sadness 'like a dog that lost its master'.", 1},
    {"tablet", "A clay tablet", "Crewe was paid with {a clay tablet} pressed with wedge-shaped writing. The note with it had an odd, {curled 'e'}.", 1},
    {"old_salt", "A light with no lighthouse", "Old Salt Morrow, on the cliffs: 'a little light on the water, low down, where no boat should be.'", 1},
    // Sandmere
    {"feather_gone", "The Feather is gone", "The Feather of Ma'at was stolen during the gala. Kamal Idris was found {drugged inside the sarcophagus room}, the door sealed.", 2},
    {"resin_seal", "Sealed from outside", "The resin round the sarcophagus lid is {fresh}, and scraped smooth {from the outside}. Someone sealed Kamal in.", 2},
    {"gold_leaf", "Gold in the vent", "Flecks of {gold leaf} in the Hall's air duct, and the scrape of someone slim crawling through.", 2},
    {"replica_case", "A cradle for a copy", "The Feather's velvet cradle has held {a short, light copy} for a year at least, not the long true plume.", 2},
    {"pawn_ticket", "A pawn ticket", "In Kamal's desk: 'One antique feather, gilt mount', {pawned a year ago}. Due last week.", 2},
    {"brother_debt", "A letter signed C.", "Hidden on Sefa's bench: 'Your brother's debts are ours. {Bring the Feather} and he goes free. - C.' A clay seal.", 2},
    {"resin_pot", "Fresh resin", "In Sefa's workshop, a pot of {sealing resin}, the same amber as the sarcophagus seal, used in the last day.", 2},
    {"photo_plate", "A gala photograph", "The gala photographer's plate shows {Lady Neferet in the crowd} at midnight, when the Feather was taken.", 2},
    {"sphinx_passage", "The Sphinx's passage", "Under the Sphinx, {a passage} runs to the Hall's air ducts. 'A slim one crawled it, carrying a copy.'", 2},
    {"camel_gate", "Out by the camel gate", "Tally saw a woman leave by {the camel gate} at dawn with a satchel, smelling of {varnish}.", 2},
    // Greenwold
    {"missing_guest", "A year and a day", "A year ago Gareth Lyle cut off a green giant's head on a wager. The giant picked it up. Now {Gareth has vanished}.", 3},
    {"costume", "A green costume", "In Lord Bertram's chest: a green costume and papier-mache head, {far too small} for the real Knight.", 3},
    {"girdle", "The green girdle", "Lady Rosalind secretly gave Gareth {a green silk girdle} 'that no blade can cut through'.", 3},
    {"kept_back", "Things kept back", "In the trade-game book, Gareth kept back {keys, bread and a spade} the week before he vanished.", 3},
    {"bread_seen", "A ghost buying bread", "Moth saw Gareth {alive, in a hood, buying bread} two days after he 'vanished'.", 3},
    {"collector_charm", "A clay charm", "Gareth now wears {a clay disc pressed with wedges}: a real charm against death, made with stolen power.", 3},
    {"empty_chapel", "A rusted axe", "At the Green Chapel the Knight's axe has {rusted} and the moss {died in a ring} around it.", 3},
    {"knight_weak", "The Knight's wound", "The Green Knight's head won't grow back: {his regrowth was taken} from the Chapel moss.", 3},
    // Skjold
    {"one_raven", "One raven", "Over Skjold at dawn, {Huginn flies alone}. Muninn, Memory, is gone.", 4},
    {"forgotten_days", "Forgotten days", "The townsfolk are {losing names and days}, as if the town's memory is draining away.", 4},
    {"police_boat", "A police launch", "Sigga saw {a police launch} row to the sea cave every night without a lamp.", 4},
    {"rune_cage", "A rune shelter", "Asta's cage is cut with runes that {keep harm out}. Its door doesn't even lock.", 4},
    {"fake_sagas", "Penny sagas", "Eirik's 'forgotten sagas' come from {a children's book} printed last year.", 4},
    {"blue_fibre", "A blue thread", "In the Norns' loom, a {police-blue fibre} snagged where the town's memory was cut.", 4},
    {"requisition", "An order for a cage", "Hilde forged 'one cage, iron, bird-sized, no runes' for {Chief Inspector Castellane}, under his own seal.", 4},
    {"juno_note", "Juno's notebook", "Juno has been {investigating her own father}: dates, payments, a debt cleared by 'A.V.'.", 4},
    {"falls_memory", "The Falls, remembered", "Muninn's gift: the Professor {climbed down a rope} at the Falls. Castellane threw his stick after him.", 4},
    // Ur-Lumen
    {"siduri_words", "Siduri's words", "'Fill your belly. Dance. {Love the child who holds your hand.} This is the lot of mortals.'", 5},
};
const int N_CLUES = sizeof(CLUES) / sizeof(CLUES[0]);

const LinkDef LINKS[] = {
    {"song_cylinder", "phono_crate", "phonograph", "The 'singing' was a {phonograph}. And Crewe & Co. keeps marine phonographs that play in the rain."},
    {"insurance", "wreck_list", "motive", "Every wrecked ship was Crewe's, and every one was insured a week before. {Someone profits from drowning them.}"},
    {"needle", "dark_rowboat", "rowboat", "Someone sat in a {dark rowboat} by the east jetty and played the song out over the water."},
    {"wax_ears", "wax_pocket", "earplugs", "Whoever carried wax plugs {knew not to listen}. Just like Odysseus's crew."},
    {"lamp_log", "cave_truth", "silas_clear", "Silas's lamp went dark for {mercy, not murder}. He was in the cave with Old One-Eye."},
    {"ledger_altered", "brandy", "ottilie_clear", "Ottilie scraped her ledger to hide {brandy}, not wrecks. A smuggler, not a killer."},
    {"sirens_silent", "song_cylinder", "real_voice", "The song on the cylinder is {real}: the Sisters' own voices, stolen and put to work."},
    {"sisters_warned", "wreck_list", "wrong_song", "The Sisters' song meant 'turn back'. Played from {the wrong place}, it would steer ships straight onto the Rock."},
    {"old_salt", "dark_rowboat", "low_light", "Morrow's 'low light on the water' was the rowboat's {hooded lantern}, lit only to read the gauge."},
    {"resin_seal", "resin_pot", "sealed_in", "Kamal was sealed in {from outside}, with resin from {Sefa's own workshop}."},
    {"gold_leaf", "sphinx_passage", "vent_route", "Someone came up the Sphinx's passage and through the vent, {trailing gold leaf}."},
    {"replica_case", "pawn_ticket", "kamal_swap", "The case held a copy because {Kamal pawned the real Feather} a year ago."},
    {"brother_debt", "camel_gate", "sefa_forced", "Sefa was {forced}. She left by the camel gate at dawn with whatever she took."},
    {"photo_plate", "feather_gone", "neferet_clear", "Lady Neferet was {in the gala photograph} when the Feather went. She wanted it; she didn't take it."},
    {"pawn_ticket", "brother_debt", "two_thieves", "{Two thefts}: Kamal's, a year ago, and Sefa's, last night. And the Collector bought the real Feather from the pawnshop."},
    {"costume", "missing_guest", "prank_only", "Bertram's costume could fool a vicar, not a hall full of guests. {The real Knight is a giant.}"},
    {"kept_back", "bread_seen", "hiding", "Gareth {packed for a long stay} and is hiding in the wood."},
    {"girdle", "collector_charm", "two_charms", "Two charms against death: Rosalind's silk, which was kindness, and {the Collector's clay}, which was a price."},
    {"empty_chapel", "knight_weak", "power_stolen", "The Knight's power to grow back {was stolen from the Chapel itself}."},
    {"collector_charm", "empty_chapel", "sold_power", "Gareth {traded the Knight's regrowth} for a charm to save his own neck."},
    {"one_raven", "forgotten_days", "memory_gone", "No Muninn, no memory. The town forgets {because Memory herself was taken}."},
    {"fake_sagas", "forgotten_days", "skald_fraud", "Eirik is a {fraud, but a small one}: he sells the town back its stories, he didn't steal them."},
    {"rune_cage", "one_raven", "asta_shelter", "Asta's cage is {a shelter} for Muninn's return, not a trap."},
    {"blue_fibre", "requisition", "police_hand", "The thread in the Norns' loom is {police blue}, and the cage order carries a police seal."},
    {"police_boat", "requisition", "cave_cage", "A police cage, a police launch, the sea cave: {Muninn is caged in the sea cave}."},
    {"juno_note", "requisition", "father_guilty", "Juno was right about her father. {Castellane serves the Collector}."},
};
const int N_LINKS = sizeof(LINKS) / sizeof(LINKS[0]);

const Testimony TESTIMONIES[] = {
    {"crewe", L_CREWE, "Augustin Crewe",
     {{"I never set foot near the water after dark. Ask anyone.", "dark_rowboat", "crewe_b1"},
      {"Phonographs? I ship coal and timber, detective. Nothing that sings.", "phono_crate", "crewe_b2"},
      {"Sink my own ships? It would ruin me. Where's the sense in that?", "insurance", "crewe_b3"},
      {"Wax? What would a man like me want with wax?", "wax_pocket", "crewe_b4"}},
     4, "crewe_broken"},
    {"ottilie", L_OTTILIE, "Ottilie Brack",
     {{"My ledger is exactly as the ships reported it. Every line.", "ledger_altered", "ottilie_b1"},
      {"Nothing came into this harbour on those nights. Nothing.", "brandy", "ottilie_b2"}},
     2, "ottilie_broken"},
    {"silas", L_SILAS, "Silas Penhallow",
     {{"I was up in the lamp room all that night, same as every night.", "lamp_log", "silas_b1"}},
     1, "silas_broken"},
    {"marisol", L_MARISOL, "Marisol Vey",
     {{"That song? I wrote it myself, years ago. It's mine.", "song_cylinder", "marisol_b1"}},
     1, "marisol_broken"},
    {"kamal", L_KAMAL, "Kamal Idris",
     {{"The Feather was genuine, in its case until last night.", "replica_case", "kamal_b1"},
      {"I have no idea who sealed me in.", "resin_pot", "kamal_b2"}},
     2, "kamal_b2"},
    {"sefa", L_SEFA, "Sefa Okon",
     {{"I was home all night, varnishing.", "camel_gate", "sefa_b1"},
      {"I've never been near the Hall's vents.", "gold_leaf", "sefa_b2"},
      {"Nobody is making me do anything.", "brother_debt", "sefa_b3"}},
     3, "sefa_broken"},
    {"bertram", L_BERTRAM, "Lord Bertram",
     {{"I haven't seen a green man in these woods in years. Not one.", "costume", "bertram_b1"}},
     1, "bertram_b1"},
    {"rosalind", L_ROSALIND, "Lady Rosalind",
     {{"Gareth left without a word to anyone. I knew nothing.", "kept_back", "rosalind_b1"}},
     1, "rosalind_b1"},
    {"gareth", L_GARETH, "Gareth Lyle",
     {{"The green man took me! I barely escaped with my life!", "bread_seen", "gareth_b1"},
      {"Charm? I've never seen any clay charm.", "collector_charm", "gareth_b2"}},
     2, "gareth_broken"},
    {"asta", L_ASTA, "Asta Brun",
     {{"The skald is your thief, not me. Ask him where his sagas come from.", "fake_sagas", "asta_b1"}},
     1, "asta_b1"},
    {"eirik", L_EIRIK, "Eirik Hallsson",
     {{"These sagas are ancient. I simply remember them, that's all.", "fake_sagas", "eirik_b1"}},
     1, "eirik_b1"},
    {"vane", L_VANEMASK, "Professor Vane",
     {{"No one was hurt by what I did. Not really.", "wreck_list", "vane_b1"},
      {"The myths were only stories. They didn't need their power.", "sisters_warned", "vane_b2"},
      {"I had to let you think I was dead. There was no other way.", "falls_memory", "vane_b3"},
      {"Kidu would want to come back. Anyone would.", "siduri_words", "vane_b4"}},
     4, "vane_broken"},
};
const int N_TESTIMONIES = sizeof(TESTIMONIES) / sizeof(TESTIMONIES[0]);

const AccuseDef ACCUSES[] = {
    {"fog",
     {"Marisol Vey, the singer", "Silas Penhallow, the keeper", "Ottilie Brack, the harbourmaster", "Augustin Crewe, the shipowner"}, 4, 3,
     {"Jealousy over a sailor who left her", "The insurance money", "To hide her smuggling", "Revenge on the sea"}, 4, 1,
     {"Sang from the rocks herself", "Let the lighthouse go dark", "Played a stolen song from a dark rowboat", "Changed the harbour charts"}, 4, 2,
     "phono_crate",
     {"acc_marisol", "acc_silas", "acc_ottilie", "acc_weak"},
     "acc_right"},
    {"sand",
     {"Lady Neferet Ashwood", "Tomas Reyes, the guide", "Kamal Idris, alone", "Sefa Okon, alone", "Kamal Idris and Sefa Okon, together"}, 5, 4,
     {"Greed for a priceless relic", "To hide a pawned Feather - and a debt the Collector bought", "Revenge on the museum", "A dare at the gala"}, 4, 1,
     {"Smashed the case during the gala", "Staged a robbery from inside, then crawled out with a copy", "Bribed the night guard", "Swapped it during restoration"}, 4, 1,
     "pawn_ticket",
     {"acc_s_neferet", "acc_s_tomas", "acc_s_kamal", "acc_s_sefa", "acc_s_weak"},
     "acc_s_right"},
    {"green",
     {"Lord Bertram Hautdesert", "Lady Rosalind", "Morgana Fay", "Gareth Lyle"}, 4, 3,
     {"Jealousy of the Lodge", "Fear of the return blow: he sold the Knight's power for a charm", "A prank that went too far", "Hatred of the old ways"}, 4, 1,
     {"Wore a green costume to frighten the guests", "Faked his vanishing and led the Collector to the Chapel", "Poisoned the trade game", "Hid the Knight's axe"}, 4, 1,
     "collector_charm",
     {"acc_g_bertram", "acc_g_rosalind", "acc_g_morgana", "acc_g_weak"},
     "acc_g_right"},
    {"skjold",
     {"Eirik Hallsson, the skald", "Asta Brun, the rune-carver", "Juno Castellane", "Chief Inspector Bram Castellane"}, 4, 3,
     {"Fame and fees", "To keep Muninn safe", "To silence the town's memory for the Collector", "Revenge on Skjold"}, 4, 2,
     {"Sold false sagas", "Caged Muninn in the sea cave with police iron", "Poisoned the well", "Bound her with runes"}, 4, 1,
     "requisition",
     {"acc_k_eirik", "acc_k_asta", "acc_k_juno", "acc_k_weak"},
     "acc_k_right"},
};
const int N_ACCUSES = sizeof(ACCUSES) / sizeof(ACCUSES[0]);

// --- the script ------------------------------------------------------------------------------------
const char *const STORY = R"GGS(
#prologue
@fade out
@chapter Calder Falls|A year ago
@fade in
-: Mist off the falls, cold enough to taste. Professor Vane walks ahead, as he always does, without looking back.
-: You are Wren Ashby, seventeen, and for two years his apprentice. Today he said only: "Come. Bring your eyes."
@walk player N 10
vane: There you are. Look down, Wren. Tell me what the ground says.
> [check obs 6] Study the mud by the ledge #pro_obs_ok #pro_obs_fail
> "It says it's wet, Professor." #pro_joke

#pro_joke
vane(1): Ha. It does. It also says two people came this way before us, and only one walked back. Look again, properly.
@goto pro_obs_ok

#pro_obs_fail
OBSERVATION: Mud. Leaves. Your own boots. It all swims together.
vane: You're looking at the mud. Look at the edges of things. Two sets of prints. One goes back. One doesn't.
@goto pro_obs_after

#pro_obs_ok
OBSERVATION: Two sets of prints. A heavy boot and a light one. The heavy one turns back. The light one {stops at the edge}.
vane: Good. Two came. One returned. Remember that: what the ground says is never all of it.
#pro_obs_after
vane: I've brought you something. Three young ones from my study. A detective needs a partner who sees what she misses.
vane: Choose. And choose for yourself, not for me.
> Embrit, a soot salamander with a candle tail (Fire) #pro_ember
> Puddock, a sleepy barnacled toad (Water) #pro_pud
> Sprigling, a shy fawn of moss and twig (Fae) #pro_sprig

#pro_ember
@mon 0 5
@goto pro_chosen
#pro_pud
@mon 3 5
@goto pro_chosen
#pro_sprig
@mon 6 5
@goto pro_chosen

#pro_chosen
+has_partner
-: $p blinks up at you, then trots to your boot as if it has always lived there.
vane(1): There. Now you're two. Here, take my notebook too. In case.
wren: In case of what?
vane(3): In case I'm wrong about something, Wren. I so often am, lately.
-: He turns to the falls. Something on the ledge below has caught his eye.
vane: Wait here. There's something on the ledge.
@walk vane E 1
@wait 0.6
@shake
@flash
+falls_done
-: The ground gives. A crack like a gunshot, a rush of white water, and the Professor is gone.
-: You run to the edge. Nothing but spray. His walking stick lies where he stood, {its ferrule pointing back the way you came.}
@wait 0.5
@fade out
@chapter One year later|Fogmouth, on the sea
@warp quill 5 3 S
@fade in
quill(1): Wren, love, up you get. There's a constable on the step asking for "the Professor's apprentice", and he's dripping on my mat.
quill: He left you this. Third ship on Wailing Rock this month. They say the drowned heard singing.
quill: I've packed you lanterns for catching, and two tonics. Mind you come home for supper.
@give lantern 5
@give tonic 2
@set composure 3
+juno_town_show
@save
@end

#quill
? !has_partner #quill_plain
quill(1): Home again. Sit down a minute, love. Your $p looks done in.
> Rest (heal your team) #quill_rest
> Save the case #quill_save
> Ask about the Professor #quill_vane
> Never mind #quill_bye
#quill_plain
quill: Off you go, love.
@end
#quill_rest
@heal
-: Mrs. Quill feeds everyone. Your team is fully rested. You feel steadier too.
@end
#quill_save
@save
@end
#quill_vane
quill(3): He used to sit just there, with his feet on my good stool. He'd say a case is a knot. Find the end, and pull gently.
quill: He'd not want you moping. He'd want you working.
@end
#quill_bye
quill(1): Mind the wet cobbles.
@end
#quill_table
-: The Professor's old notebook, open where you left it. One line, underlined twice: {"Two came. One returned."}
@end

#blackout
quill(5): Wren! They carried you in off the street. Your creatures are fine now. I've seen to them. You lie still.
quill: Don't go picking fights you can't finish. The Professor never did.
@end

#door_locked
-: The door is locked.
@end
#cave_locked
-: A trapdoor, bolted from this side. Silas's things are piled on it. It isn't your business to open it. Yet.
@walk player S 1
@end

#juno_town
? juno_beaten #juno_after
juno(4): So you're the Professor's apprentice. I expected someone taller.
juno: Juno Castellane. My father is Chief Inspector. He sent me to look at these wrecks, and I don't need help.
juno: But I'd like to see what the famous apprentice can do. Partners out.
@battle 9 #juno_win #juno_lose
#juno_win
+juno_beaten
juno(3): ...Fine. You're not hopeless. That doesn't mean you'll solve it first.
juno(4): A word of advice, apprentice: everyone in this town is lying about something. Don't fall for the first lie you catch.
@end
#juno_lose
+juno_beaten
juno(4): As I thought. Go home, apprentice. Leave this to the Castellanes.
@heal
-: Mrs. Quill would have words about that. Your team picks itself up.
@end
#juno_after
juno(4): Still here? Talk to the keeper. Or don't. I'm not your tutor.
@end

#dobbs_street
dobbs(5): Oh! You came! Constable Dobbs, miss. Sir. Detective. The Inspector's away in the capital and I'm, well, I'm all there is.
dobbs: Three ships on Wailing Rock this month. The Kestrel, the Mary Ann, the Good Hope. Twenty-one men drowned.
dobbs: The ones who lived all say the same thing: they heard {singing}, and steered toward it like men in a dream.
@clue wreck_list
+case1_open
dobbs: People are saying it's the Sisters, the old Sirens of the Rock, come back to drown us. I don't believe in that. Mostly.
> "Who should I talk to first?" #dobbs_leads
> "I'll find my own way." #dobbs_bye
#dobbs_leads
dobbs: The harbourmaster keeps the ship ledger. Silas at the lighthouse says his lamp went out on the third night.
dobbs: And there's a singer at the Drowned Lantern. She sings a song every night that the sailors say sounds just like it.
#dobbs_bye
dobbs(5): I'll be at the station. Please. Before there's a fourth.
@save
@end

#dobbs
? case1_closed #dobbs_closed
? !case1_open #dobbs_early
dobbs(5): Any luck? I've written down everything you've told me. Well. Most of it.
> "I'm ready to name who did it." #dobbs_accuse
> "Show me your notes on the wrecks." #dobbs_notes
> "Nothing yet." #dobbs_bye2
#dobbs_early
dobbs: Detective! I left a note with your landlady. I'm on the street outside.
@end
#dobbs_notes
dobbs: All three ships went down between midnight and two. All in fog. All Crewe & Co. ships, now I look at it.
@end
#dobbs_bye2
dobbs(5): Right. Yes. I'll keep the kettle on.
@end
#dobbs_accuse
? deductions<3 #dobbs_notyet
dobbs: Right. Here's how it goes, then. You say it, I write it, and we go and get them.
@accuse fog
@end
#dobbs_notyet
LOGIC: Not yet. You have threads, not a rope. Pin more of them together on the board first. (Three deductions.)
@end
#dobbs_closed
dobbs(1): Crewe's in the cells and singing like a, well, like a bird. Thank you, detective. Fogmouth owes you.
@end

#acc_marisol
dobbs(5): Marisol? The singer? ...If you say so.
-: They take her that evening, still in her red dress, still humming. The tavern falls quiet.
+marisol_jailed
+sirens_lost
-: The next night, in the fog, a fourth ship hears the song. It turns away at the last moment. Barely.
LOGIC: She was in a cell. And the singing went on. You were wrong.
@composure -1
@end
#acc_silas
dobbs(5): The keeper? He's been on that light thirty years...
-: Silas goes with them without a word. The lamp goes dark, and stays dark.
+silas_jailed
+light_dark
-: That night the song comes again over the water, and nobody is watching the Rock.
LOGIC: The song didn't need the lamp out. Silas was never the singer.
@composure -1
@end
#acc_ottilie
dobbs: The harbourmaster? Well, she has been, er, creative with that ledger...
-: Ottilie is charged. The port closes for a day while they count every barrel.
+ottilie_jailed
-: And that night, the singing, again. Her ledger hid brandy, not murder.
@composure -1
@end
#acc_weak
dobbs(5): Crewe? But... why, though? And how? The magistrate will want more than a feeling.
-: Crewe laughs in the station doorway. His lawyer is already there. He walks free by morning.
LOGIC: Right man. Wrong story. Pin it properly: why, how, and the proof that ties his hands.
@composure -1
@end
#acc_right
@chapter The Drowned Choir|Accusation
dobbs: Augustin Crewe. Insurance. A stolen song played from a rowboat. And phonographs in his own office.
-: You lay it out. The policies. The jetty. The wax in his pocket, so he wouldn't hear his own murder.
-: When you reach his door, Crewe is already in his coat, with a carpet bag and a creature at his heel.
@warp crewe 4 4 N
crewe(4): Clever little bird. The Professor taught you well. Shame he never taught you when to stop.
@battle 8 #crewe_win #crewe_lose
#crewe_lose
-: Crewe's creatures drive you back, but Dobbs's whistle brings half the street. They pile on him in the doorway.
#crewe_win
+crewe_arrested
+case1_closed
@set case1 1
crewe(3): All right! All right. I didn't make the song. I bought it. A man came, in a clay mask, with a jar that sang.
crewe: He said: "Try it on the Rock. If it works, keep the ships' money. I only want to know it works."
crewe: He paid me in this. A clay tablet. Worth more than three ships, he said. It's just mud with scratches.
@clue tablet
LORE: Wedge-shaped writing, pressed into clay. Older than any language spoken in Fogmouth.
MEMORY: The note pinned to it. That curled 'e'. You've seen that hand before. In a notebook in your pocket.
-: You don't say it out loud. Not yet.
@money 300
@save
-: The Sisters' voices are still trapped in that cylinder. Someone should take it back to the Rock.
@end

#crewe
? case1_open #crewe_case
crewe(4): Augustin Crewe, of Crewe & Co. Coal, timber, and fair dealing. What can I do for you, young person?
@end
#crewe_case
crewe(4): The apprentice! Terrible business, these wrecks. Terrible. My poor ships.
> [clue:insurance] "Your poor insured ships." #crewe_ins
> [check obs 9] Look him over while he talks #crewe_obs_ok #crewe_obs_fail
> "Tell me again where you were." #crewe_interro
> "Good day." #desk_bye
#crewe_ins
crewe(2): Prudence, detective. A careful man insures what he loves. Now, I'm very busy.
@end
#crewe_obs_ok
OBSERVATION: His left cuff. A grey smear of {candle wax}, and something in his waistcoat pocket that rattles softly. Little rolled plugs.
@clue wax_pocket
crewe(4): Is something on my sleeve?
@end
#crewe_obs_fail
OBSERVATION: A good coat. A gold chain. A smile like a shop window. You can't see past it.
@end
#crewe_interro
? clue:wreck_list #crewe_go
@end
#crewe_go
crewe(4): Ask whatever you like. I have nothing to hide.
@interro crewe
@end
#crewe_b1
crewe(5): ...A child saw a rowboat. A child. And there are a hundred top hats in this town.
@interro crewe
@end
#crewe_b2
crewe(2): Those are... for a customer. A gift. Phonographs aren't a crime.
@interro crewe
@end
#crewe_b3
crewe(5): Double? Who told you double? That's... confidential business.
@interro crewe
@end
#crewe_b4
crewe(2): Wax. For my... it's for sealing letters!
CHARM: He's sweating now. He had to sit in that boat with the song playing. He had to not hear it.
@interro crewe
@end
#crewe_broken
crewe(2): Get out of my office. Get out! You've nothing a magistrate would take.
LOGIC: He's wrong. Tell Constable Dobbs you're ready to name him.
+crewe_broken
@end
#crewe_desk
-: Shipping papers, neatly stacked. Policies. You run a finger down them.
> [check logic 7] Compare the dates #desk_ok #desk_fail
> Leave them #desk_bye
#desk_ok
LOGIC: Three ships. Three policies, each doubled on the same day, {a week before the first wreck}. That's not prudence. That's a plan.
@clue insurance
@end
#desk_fail
LOGIC: Numbers. Dates. Your eyes slide off them. Maybe you'd see more if you knew what to look for.
@end
#desk_bye
@end
#crewe_barrel
-: Crates and barrels. One crate is new, its lid only lightly nailed.
> [check obs 7] Peer through the slats #crate_ok #crate_fail
> Leave it #desk_bye
#crate_ok
OBSERVATION: Brass horns. Waxed seals. Stencilled on the wood: {MARINE PHONOGRAPH - PLAYS IN ALL WEATHERS}.
@clue phono_crate
@end
#crate_fail
OBSERVATION: Dark in there. Something brass. It could be anything.
@end

#ottilie
? clue:brandy #ottilie_after
ottilie(2): Ottilie Brack, harbourmaster. If you're here about the wrecks, my records are in order, and my time is short.
> "Then you won't mind me asking about them." #ottilie_interro
> [check charm 8] "You look tired. It's been a hard month." #ottilie_charm_ok #ottilie_charm_fail
> "Good day." #desk_bye
#ottilie_interro
@interro ottilie
@end
#ottilie_charm_ok
ottilie(3): ...It has. Three ships. I knew men on all of them. I keep the harbour. I'm supposed to keep them safe.
ottilie: Whatever you think of me, detective, I didn't drown anyone.
@end
#ottilie_charm_fail
ottilie(2): Flattery won't get you into my ledger.
@end
#ottilie_b1
ottilie(5): ...Scraped. Yes. I scraped it. Not because of the wrecks.
@interro ottilie
@end
#ottilie_b2
ottilie(3): Brandy. French. On the nights of the fog, when no one watches the harbour. That's all. I swear on my mother.
@clue brandy
@interro ottilie
@end
#ottilie_broken
ottilie(3): So now you know. Tell Dobbs if you must. But I never touched a ship.
LOGIC: A smuggler. Not a killer. Pin that on the board, and move on.
@end
#ottilie_after
ottilie(3): I'll pay the duty. All of it. Don't look at me like that.
@end
#ledger
-: The great harbour ledger, every ship in and out for forty years.
> [check obs 8] Look closely at this month's pages #ledger_ok #ledger_fail
> Leave it #desk_bye
#ledger_ok
OBSERVATION: On the nights of all three wrecks, entries {scraped away} with a blade and written over in fresher ink.
@clue ledger_altered
@end
#ledger_fail
OBSERVATION: Columns and columns of neat handwriting. Nothing jumps out at you.
@end

#marisol
? clue:song_cylinder #marisol_after
marisol(3): You'll be the detective. Everyone's been in to ask me about my song. As if a song ever drowned a man.
> "Sing it for me." #marisol_sing
> "Where did you learn it?" #marisol_interro
> "Never mind." #desk_bye
#marisol_sing
-: She sings. It is beautiful and it is wrong, a tune that tugs at the back of your skull and points you somewhere.
-: The room goes very still. An old sailor by the fire puts his hands over his ears.
marisol(3): You see? They all do that now.
@end
#marisol_interro
@interro marisol
@end
#marisol_b1
marisol(3): ...No. I didn't write it. A deckhand sold me a cylinder last month. I play it after closing. I learned it by ear.
marisol: I told everyone it was mine. It's the only beautiful thing I've ever owned.
@clue song_cylinder
@end
#marisol_after
marisol(3): Take the cylinder, if it helps. I don't want it any more. It sounds like someone crying underneath.
@end
#gramophone
? clue:song_cylinder #gram_ok
-: An old gramophone with a cylinder attachment. The cylinder's box is under the counter.
> [check obs 9] Turn the box to the light #gram_obs_ok #gram_obs_fail
> Leave it #desk_bye
#gram_obs_ok
OBSERVATION: Pressed faintly into the cardboard: {C&Co.} Crewe & Company.
@clue song_cylinder
@end
#gram_obs_fail
OBSERVATION: Dusty cardboard. A torn label. Nothing you can read.
@end
#gram_ok
-: The cylinder sits in its box. Faint as breath, even silent, you'd swear it hums.
@end
#tavern_sailor
? clue:wreck_list #ts2
salt(3): Leave me be. I'm drinking.
@end
#ts2
salt(3): I was on the Kestrel. I heard it. Like my mother calling. I'd have walked off the deck if the mate hadn't knocked me down.
salt: Funny thing. It came from low on the water. Not from the Rock. From {beside} us.
@end
#tavern_gent
gent: Terrible business. Mind you, Crewe's done well out of it, they say. Very well.
> [check charm 7] "Done well? From losing ships?" #gent_ok #gent_fail
> "Good evening." #desk_bye
#gent_ok
gent: Insured to the gunwales, all three. My cousin writes policies. Crewe doubled them a week before the first went down.
@clue insurance
@end
#gent_fail
gent: Oh, I couldn't possibly say. Gossip is so unbecoming.
@end

#silas
? cave_known #silas_after
silas(3): Silas Penhallow. Keeper of this light, thirty years. If you're here to tell me I killed those men, get in line.
> "Tell me about the night the lamp went dark." #silas_interro
> [check obs 8] Look at his hands and coat #silas_obs_ok #silas_obs_fail
> "I'll come back." #desk_bye
#silas_interro
@interro silas
@end
#silas_obs_ok
OBSERVATION: Strips of torn sailcloth in his pocket. Fish oil on his sleeves, not lamp oil. And a bloodied harpoon head on the table, {far too big for any fish}.
silas(3): You've sharp eyes. Like the Professor's.
@end
#silas_obs_fail
OBSERVATION: An old man in an old coat. Tired. That's all you see.
@end
#silas_b1
silas(3): ...No. I wasn't. All right. I'll show you. But you swear you won't tell the town.
+cave_known
@clue cave_truth
silas: There's a trapdoor by the stairs. Something lives under this light. It always has. It's hurt.
@end
#silas_broken
@end
#silas_after
silas(3): Go down and see him. Gently, mind. He doesn't like strangers.
@end
#lamp_log
-: The lighthouse log. Thirty years of careful entries: weather, oil, ships seen.
> [check memory 6] Find the night of the third wreck #log_ok #log_fail
> Leave it #desk_bye
#log_ok
MEMORY: The Good Hope went down on the 14th. And the 14th in Silas's log is {blank}. The only blank page in thirty years.
@clue lamp_log
@end
#log_fail
MEMORY: Which night was it, again? The dates blur.
@end

#cyclops
? cyclops_healed #cyc_after
-: A hill that breathes. Then one great eye opens, amber as a lamp, and looks at you.
cyclops: ...SMALL ONE. YOU SMELL OF SALT AND SORROW. LIKE THE OTHER.
> "What other?" #cyc_other
> [item:tonic>=1] Offer a tonic for his wound #cyc_heal
> Back away slowly #desk_bye
#cyc_other
cyclops: A GREY COAT. A FACE OF PALE CLAY. HE CAME IN A BOAT TO THE SISTERS' ROCK WITH A JAR THAT DRINKS SOUND.
cyclops: I THREW STONES. HIS HARPOONER THREW BACK. NOW THE SISTERS ARE SILENT, AND I AM SLOW.
@clue sirens_silent
@clue clay_mask
cyclops: HE WAS SAD, THAT ONE. SAD LIKE A DOG WHOSE MASTER DID NOT COME HOME.
@end
#cyc_heal
@give tonic -1
-: You pour the tonic into the wound. The great eye closes, then opens, clearer.
+cyclops_healed
@favour cyclops obs
@teach boulder_toss
cyclops: GOOD. YOU MEND WHAT OTHERS BREAK. TAKE MY THROW, FOR YOUR LITTLE ONE. AND MY EYE, FOR YOURS.
@end
#cyc_after
cyclops: THE SISTERS. GIVE THEM BACK THEIR SONG, SMALL ONE.
@end

#lune
? clue:wax_ears #lune_after
lune: Dr. Hesper Lune. Archivist. You're Ambrose Vane's apprentice. I knew him, a little. He borrowed my books and never brought them back.
lune: Sirens, is it? The Odyssey. Read it. Odysseus wanted to hear their song and live, so he was tied to the mast.
lune: And his crew {stopped their ears with wax}, so they could row past and never hear a note.
@clue wax_ears
lune: Pages of old stories wash up all over Fogmouth, you know. If you find any, bring them to me. I'll read them to you.
@end
#lune_after
lune(1): Found any pages? They glow a little, at dusk. Old stories don't like to be lost.
? fragments>=1 #lune_frags
@end
#lune_frags
lune: You've found some. Each one is a scene, a moment from the old story. They remember things the town forgot.
@end
#lune_desk
-: Dr. Lune's desk. A translation of the Odyssey, a magnifying glass, and a postcard from Ur-Lumen, the capital, unsigned.
@end

#pip
? clue:dark_rowboat #pip_after
? pip_tailing #pip_report
pip(1): Oi! Detective! Pip's the name, eyes is the game. Nothing moves in this town I don't see. For a bun.
> [item:bun>=1] Give Pip an iced bun #pip_bun
> "Where do I get a bun?" #pip_where
> "Not now." #desk_bye
#pip_where
pip: Fishwife on the quay sells 'em. Iced ones. Ten bob. Don't get the plain ones, they're an insult.
@end
#pip_bun
@give bun -1
pip(1): Ta! Right. What d'you want watched?
> "The east jetty. At night." #pip_jetty
> "Anything strange on the water." #pip_jetty
#pip_jetty
pip: The jetty? Ha. You're cleverer than you look. Come find me after you've done some poking about.
+pip_tailing
@end
#pip_report
? deductions<1 #pip_wait
pip(1): Here, I remembered something. The night the Good Hope went down? A rowboat came out of the east jetty.
pip: No lamp. Bloke in a top hat. Rowed out toward the Rock, slow, like he was listening to something.
@clue dark_rowboat
@end
#pip_wait
pip: Still watching. Go poke about some more. Try the jetty planks, maybe.
@end
#pip_after
@chatter fr pip Top hats. Can't trust 'em. Everyone knows that.
@end

#fishwife
fishwife(1): Fresh herring! Iced buns! Lanterns for the young creature-catcher!
> [money>=10] Iced bun - 10 shillings #buy_bun
> [money>=30] Tonic - 30 shillings #buy_tonic
> [money>=40] Brass lantern - 40 shillings #buy_lantern
> Nothing today #fw_bye
#buy_bun
@money -10
@give bun 1
@goto fishwife_more
#buy_tonic
@money -30
@give tonic 1
@goto fishwife_more
#buy_lantern
@money -40
@give lantern 1
@goto fishwife_more
#fishwife_more
fishwife(1): Anything else, my lovely?
> [money>=10] Iced bun - 10 shillings #buy_bun
> [money>=30] Tonic - 30 shillings #buy_tonic
> [money>=40] Brass lantern - 40 shillings #buy_lantern
> That's all #fw_bye
#fw_bye
fishwife(1): Mind the gulls, they'll have your hat.
@end

#ysolde
? clue:sisters_warned #ys_after
ysolde: Sit, child. You want to hear about the Sisters? Everyone wants the Sisters this month.
ysolde: The town says they sing to drown men. The town has it backwards. My grandmother told me true.
ysolde: The Sisters sang from the Rock to {warn} ships off it. In fog, you steered away from the song, not toward it.
@clue sisters_warned
ysolde(3): And now they're silent. And something else is singing their song. From the wrong side of the Rock.
@end
#ys_after
ysolde: Find whoever's singing in their place, child. Then give the Sisters back their voice.
@end

#sailor_quay
@chatter fc sailor Won't go out past the point any more. None of us will.
@end
#gent_chapel
@chatter fc gent Twenty-one graves this month. The chapel bell's worn out its rope.
@end
#urchin_gate
urchin(1): Going out on Gull Cliffs? Wild ones in the long grass. Your $p will want to be strong. Catch some friends!
@end
#lady_walk
lady: A lady shouldn't say, but that Crewe fellow bought every phonograph in the catalogue. What does a coal man want with music?
@end
#silas_out
silas: Fine evening. I watch the water from up here now. In case it sings again.
@end

#statue
-: The Three Sisters of Wailing Rock, in green old bronze. Their mouths are open. Someone has tied a black ribbon round each throat.
@end
#pier_east
-: The end of the pier. Out in the fog, the black shape of {Wailing Rock}.
@end
#jetty
? clue:needle #jetty_done
-: The east jetty. Wet planks, a mooring ring, coiled rope.
> [check obs 8] Get down and look at the planks #jetty_ok #jetty_fail
> Leave it #desk_bye
#jetty_ok
OBSERVATION: In the cracks between the planks: tiny bright curls of {brass}. Phonograph needles wear down like that, when they play and play.
@clue needle
@end
#jetty_fail
OBSERVATION: Salt, weed, a dead crab. Nothing that means anything.
@end
#jetty_done
-: The brass shavings glint in the cracks.
@end
#noticeboard
-: A notice: "REWARD for information on the SINGING. Apply Constabulary." Someone has drawn a fish with a top hat underneath.
@end
#grave
-: A new grave. "Tom Abernethy, of the Good Hope. He heard the song." The earth is still soft.
@end

#watcher
? clue:old_salt #watcher_after
morrow: Old Salt Morrow. I watch the sea. Fifty years. The sea talks, if you listen.
morrow: The night the Good Hope went down? There was a little light on the water. Low down. Where no boat ought to be.
@clue old_salt
morrow(1): Fancy a scrap, youngster? Old Salt still has teeth.
@battle 7 #morrow_win #morrow_win
#morrow_win
morrow(1): Good. Good. You'll do.
@end
#watcher_after
morrow: Low light on the water. Remember it.
@end
#view_rock
-: From the cliff edge you can see Wailing Rock clearly. Three stone shapes on its crown, like women waiting.
@end

#aeolus
? fav_aeolus #ae_after
-: The wind up here moves strangely. It circles you, then a voice comes out of it, old and grumbling.
aeolus: A gull. A GULL. Has taken my bag of winds. The storms are all in a muddle. Get it back, small one.
> "I'll find your gull." #ae_fight
> "Not now." #desk_bye
#ae_fight
-: A fat gull with a leather bag in its beak lands on the cliff edge, looks at you, and laughs.
@battle 10 #ae_won #ae_lost
#ae_won
-: The gull drops the bag and flees. The winds sort themselves out with a sigh.
@favour aeolus lore
@teach fair_wind
aeolus: Good. Good. Take a fair wind for your little one. And remember: whoever opens a bag of storms gets the storm.
@end
#ae_lost
aeolus: Hmph. Try again when you're bigger.
@end
#ae_after
aeolus: The winds are tidy. For now.
@end

#tr_cliff1
? beat_tr_cliff1 #tr1_after
tam(1): Climber Tam! I've climbed every cliff on this coast. Let's see how you climb out of this!
@battle 0 #tr1_win
#tr1_win
+beat_tr_cliff1
tam: Ha! Fair. Watch the long grass, it's full of thimble rats.
@end
#tr1_after
tam: Rope's good, knots are good. Life is good.
@end
#tr_cliff2
? beat_tr_cliff2 #tr2_after
noor(4): Birder Noor. You're standing in my sightline. That means we battle.
@battle 1 #tr2_win
#tr2_win
+beat_tr_cliff2
noor: Mm. The Inkmoths around here have writing on their wings. No one can read it. I've tried.
@end
#tr2_after
noor: Shh. There's a Stormskua out there.
@end
#tr_cliff3
? beat_tr_cliff3 #tr3_after
fenn(2): Scholar Fenn. I study ghost-lights. You'll do as a test.
@battle 2 #tr3_win
#tr3_win
+beat_tr_cliff3
fenn: Wispens follow lamplighters home. And lately, they've been following something out on the water.
@end
#tr3_after
fenn: Fascinating. Truly.
@end

#halloran
? tide_seal #hal_after
? deductions<2 #hal_notyet
halloran(1): Captain Rook Halloran, Warden of the Tide Lodge. You've been turning this town over. I like that.
halloran: Every warden tests whoever comes through. Beat me, and you'll have the Tide Seal, and the use of my boat.
> "Let's go." #hal_fight
> "Not yet." #desk_bye
#hal_notyet
halloran: The apprentice. Come back when you've got a few threads tied together. Then we'll see what you're made of.
@end
#hal_fight
@battle 5 #hal_win #hal_lose
#hal_win
+tide_seal
@money 200
halloran(1): Ha! Good. Here, the Tide Seal. And when you're ready to go out to the Rock, my boat is yours.
@toast The Tide Seal is yours.
@save
@end
#hal_lose
halloran(1): Not today. Go and rest, and come back sharper.
@end
#hal_after
? !crewe_arrested #hal_wait
halloran: The Rock? Aye. Hold on to something.
@fade out
@warp rock 5 6 N
@fade in
@end
#hal_wait
halloran: Find whoever's singing first. Then I'll take you out to the Rock.
@end

#sirens
? sirens_freed #sir_after
-: Three tall shapes on the crown of the Rock, grey as the stone. Bird-wings folded. Mouths open. No sound comes out.
? sirens_lost #sir_lost
> [clue:song_cylinder] Play the cylinder for them #sir_free
> Wait #desk_bye
#sir_free
-: You set the cylinder spinning. The song pours out, and the Sisters lean toward it like flowers to the sun.
@flash
-: The song leaves the wax. You see it go: a thread of silver light, into three throats at once.
+sirens_freed
@favour sirens charm
@teach siren_call
sirens: WE THOUGHT NO ONE WOULD COME. WE SANG 'TURN BACK' FOR A THOUSAND YEARS, AND THEY CALLED US MURDERERS.
sirens: THE THIEF WEPT WHILE HE TOOK OUR VOICES. HE SAID: 'I AM SORRY. I NEED THEM. I NEED TO CALL SOMEONE BACK.'
sirens: HE WALKS WITH A SHADOW THAT WAS ONCE A GREAT BEAST. HE IS GOING TO THE SANDS NEXT. TO THE ONE WHO WEIGHS HEARTS.
@clue clay_mask
@goto sir_home
#sir_lost
-: Without the song, they don't know you. And while Marisol sits in a cell, the cylinder is in an evidence box you can't reach.
@goto sir_home
#sir_after
sirens: SAIL WELL, SMALL ONE. WE WILL SING YOU HOME.
#sir_home
@fade out
@warp lodge 6 4 N
@fade in
? sirens_freed #act1_end
@end

#act1_end
lune: You came back from the Rock. And the Sisters are singing again. Listen.
-: Faint, through the fog: three voices, singing 'turn back' to the ships, the way they always did.
lune: That tablet of Crewe's. I've read it. It's the first line of the oldest story there is.
lune: "He who saw the deep." The Epic of Gilgamesh. A king who lost his friend, and went looking for a way to never die.
juno(3): I heard. Good work, apprentice. My father's men will take Crewe to the capital.
juno(5): ...My father. He knew about this case before anyone told him. I don't know how.
MEMORY: A curled 'e'. A walking stick pointing back the way you came. Two came. One returned.
+act1_done
+town_sand_open
+town_green_open
lune: There's a train at the east road now the fog's lifted. Sandmere and Greenwold, both. He'll be in one of them. Go, Wren.
@chapter End of the first case|The Grand Southern awaits
@save
@end

#frag_fog1
+frag_fog1
@add fragments 1
-: A page of old paper, glowing faintly in the cobbles. As you touch it, the fog seems to part.
@card 0
@end
#frag_fog2
+frag_fog2
@add fragments 1
-: A glowing page, pinned under a stone by the wind.
@card 2
@end
#frag_fog3
+frag_fog3
@add fragments 1
-: A page in the cave, dry despite the drip, glowing like a coal.
@card 1
@end
#frag_fog4
+frag_fog4
@add fragments 1
-: A page on the Sisters' Rock, held down by a single grey feather.
@card 3
@end

#falls_edge
-: The edge. The water goes over in one white sheet, and the sound fills everything.
@end
#vane_falls
vane: Come and look, Wren.
@end

#debug_skip
@mon 0 12
@mon 9 10
@give lantern 5
@give tonic 5
@give bun 3
+has_partner
+falls_done
+case1_open
+juno_town_show
@set composure 3
@set town 1
@warp town 19 13 N
@fade in
@end
)GGS";

extern const char *const STORY_SAND;
extern const char *const STORY_GREEN;
extern const char *const STORY_SKJOLD;
extern const char *const STORY_UR;
extern const char *const STORY_WILDS;
const char *const STORY_PARTS[] = {STORY, STORY_SAND, STORY_GREEN, STORY_SKJOLD, STORY_UR, STORY_WILDS};
const int N_STORY_PARTS = sizeof(STORY_PARTS) / sizeof(STORY_PARTS[0]);

// --- scene cards --------------------------------------------------------------------------------------
const char *const CARD_TEXT[] = {
    "THE SIRENS' SONG\nOdysseus wanted to hear the Sirens and live. He had his crew tie him to the mast and stop their own ears with wax. "
    "He heard everything. They heard nothing. They rowed on past the island, and he wept at what he couldn't reach.",
    "THE CYCLOPS' CAVE\nIn a cave by the sea lived a giant with one eye, who kept sheep and trusted no one. Strangers came and ate his food. "
    "He was not a kind host. But the story was told by the strangers.",
    "THE BAG OF WINDS\nThe keeper of the winds gave Odysseus every storm tied in a leather bag, so he could sail home on a fair breeze. "
    "His crew opened it, thinking it held gold. Every storm got out at once.",
    "THE SISTERS' WARNING\nFogmouth's own story: three sisters sat on the Rock and sang 'turn back' to every ship in the fog. "
    "The ships that listened lived. The ones that drowned blamed the singers.",
    "THE WEIGHING OF THE HEART\nIn the hall of judgement the heart of the dead is laid on a scale against a single feather, the feather of Ma'at, of truth. "
    "If the heart is light, the dead walk on into the fields of reeds. If it is heavy with lies, the Devourer is waiting.",
    "AMMIT, THE DEVOURER\nPart crocodile, part lion, part hippopotamus, Ammit crouches beside the scale. She eats only one thing: a heart "
    "made heavy by wrongdoing. To be eaten by her is to be gone for ever.",
    "OSIRIS ENTHRONED\nThe king of the dead sits on his throne at the end of the hall, wrapped like a mummy, green-skinned like new grain. "
    "He was killed once and brought back. He knows what returning costs.",
    "THE SPHINX'S RIDDLE\nThe Sphinx sat outside the city and asked every traveller one question. Those who failed she ate. "
    "When at last a man answered, 'a human being', she threw herself from her rock. Riddles are dangerous to the one who asks them too.",
    "THE GREEN KNIGHT AT CAMELOT\nAt the Christmas feast a giant rode into the hall, green from head to toe, and offered a game: "
    "strike me now, and I strike you in a year and a day. Gawain took the axe. The head rolled. The Knight picked it up, and it spoke.",
    "THE EXCHANGE OF WINNINGS\nOn the road to his doom Gawain stayed at a lord's castle. Each night they traded what they had won that day. "
    "Gawain gave back every kiss, but kept a green girdle that would save his life. That one secret was his only fault.",
    "GAWAIN RIDES OUT\nIn winter Gawain rode north to find the Green Chapel, alone, through forests full of wolves and wild men, "
    "to keep a promise that would kill him. He kept it anyway.",
    "THE NICK OF THE AXE\nAt the Green Chapel the axe came down three times. Twice it stopped. The third time it only nicked his neck: "
    "a mark for the girdle he had hidden. Gawain wore the green belt ever after, to remember he had been afraid.",
    "ODIN'S RAVENS\nTwo ravens sit on the All-Father's shoulders: Huginn, Thought, and Muninn, Memory. Every dawn they fly over the world, "
    "and every evening they come back and tell him all they saw. He fears for Thought, but he fears more for Memory.",
    "THE NORNS\nAt the roots of the world-ash three women sit by a well and weave the lives of everyone who lives. "
    "What was, what is, what shall be. Not even the gods can unpick their cloth.",
    "THE FALLS, REMEMBERED\nThe ledge you couldn't see from the path. A rope. A man in a police coat, coiling it. And the Professor, "
    "climbing down into the spray, looking back once, as if to make sure you'd remember.",
    "THE LOST MIDSUMMER\nSkjold's own story: one summer night the whole town danced on the shore until dawn, and the ravens danced with them. "
    "Everyone swore they'd remember it for ever. Then the memory was taken, and they forgot they had ever been that happy.",
    "GILGAMESH AND ENKIDU\nThe proud king of Uruk met his match in Enkidu, a wild man raised by beasts. They fought, then laughed, "
    "then were brothers. Together they killed the Bull of Heaven. For that, the gods took Enkidu.",
    "ENKIDU OF THE STEPPE\nBefore he knew any city, Enkidu ran with the gazelles and drank at the waterholes and freed animals from the hunters' traps. "
    "When he died, Gilgamesh would not let him be buried, until a worm fell from his nose.",
    "SIDURI'S ADVICE\nAt the edge of the world, a woman who brewed ale told the grieving king: you will not find the life you seek. "
    "Fill your belly. Dance. Love the child who holds your hand. That is the whole of it.",
    "THE PLANT OF YOUTH\nAt the bottom of the sea grew a plant that made the old young. Gilgamesh dived and found it. On the way home he bathed in a pool, "
    "and a serpent came and ate the plant, and shed its skin. The king sat down and wept. Then he went home, and built walls that are still standing.",
};
const int N_CARDS = sizeof(CARD_TEXT) / sizeof(CARD_TEXT[0]);

} // namespace Gaslight
