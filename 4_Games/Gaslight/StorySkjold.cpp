// Gaslight & Griffins - Skjold: "Thought and Memory" (Odin's ravens).
#include "Game.h"

namespace Gaslight {

const char *const STORY_SKJOLD = R"GGS(
#skjold_arrive
+skjold_arrived
+juno_skjold_show
+case4_open
@chapter Skjold|Thought and Memory
-: Snow on the longhouse roofs, black water in the fjord. Every dawn two ravens fly over Skjold. This morning there was only one.
-: And the town is forgetting. Names. Days. Faces. A woman at the station asks you who she is, then laughs as if it's a joke. It isn't.
@clue one_raven
@save
@end

#forgetful
? clue:forgotten_days #fg_after
villager(5): Sorry, sorry, have we met? I've lost... Tuesday. And my wife's name. I'm sure I had both on Monday.
villager: It started when the second raven stopped coming. Muninn. Memory. Without her the town just... lets go.
@clue forgotten_days
@end
#fg_after
villager(5): Ingrid! Her name's Ingrid. I wrote it on my hand.
@end

#sigga
? clue:police_boat #sig_after
sigga(1): You're the detective everyone forgets they met? I'm Sigga. I don't forget. I write everything down. For a bun.
> [item:bun>=1] Give Sigga an iced bun #sig_bun
> "Not now." #sk_bye
#sig_bun
@give bun -1
sigga(1): Nights, a boat goes out from the long jetty. No lamp. A {police launch}, from the capital, the brass on it says so. It goes round the cliff to the sea cave and comes back empty.
@clue police_boat
@end
#sig_after
@chatter kr sigga Police launch, sea cave, every night. I wrote it down twice.
@end
#crowd_k
@chatter kc villager What's your name again? Sorry. I'm sure I knew it.
@end
#sk_bye
@end

#asta_out
? case4_closed #asta_closed
asta: Asta Brun. I carve runes. I'd talk more, but I've a cage to finish, and the cold gets into the iron.
> "A cage? For what?" #asta_cage
> "Good day." #sk_bye
#asta_cage
asta(3): For Muninn. When she comes back. Whoever took her will want her again, and a raven in a rune-cage can't be stolen.
+asta_home
@end
#asta_closed
asta(1): Muninn sleeps on my roof now. She doesn't need the cage. I'm keeping it anyway.
@end
#asta
? case4_closed #asta_closed
asta: You came. Look at the cage, if you like. Then tell me I'm a thief like the skald does.
@interro asta
@end
#asta_b1
asta(3): Yes, I built a cage. Read the runes on it, detective. They're for keeping harm out, not birds in.
@clue rune_cage
@end
#rune_cage
? clue:rune_cage #rc_done
-: An iron cage, beautifully made, every bar cut with tiny runes.
> [check lore 11] Read the runes #rc_ok #rc_fail
> Leave it #sk_bye
#rc_ok
LORE: "Let nothing that hunts come in. Let the guest go out when she likes." A {shelter}, not a trap. The door doesn't even lock.
@clue rune_cage
@end
#rc_fail
LORE: Runes, runes, runes. You only know a few.
@end
#rc_done
-: A shelter with an open door.
@end

#eirik
? case4_closed #eirik_closed
eirik(4): Eirik Hallsson, skald! Shall I sing you a saga? An OLD saga, that no one in Skjold remembers? I remember them all.
> "How convenient." #eirik_interro
> "Sing, then." #eirik_sing
> "No thank you." #sk_bye
#eirik_sing
-: He sings. It's grand and stirring and you're fairly sure you read it in a penny book on the train.
@end
#eirik_interro
@interro eirik
@end
#eirik_b1
eirik(3): ...Yes. They're from a book. Printed in the capital last year. The town forgot, so I... remembered for them. For a small fee.
@clue fake_sagas
@end
#eirik_closed
eirik(1): Real sagas now, I promise. Mostly.
@end
#saga_shelf
? clue:fake_sagas #ss_done
-: A shelf of handwritten "ancient sagas". And one printed book, its spine cracked.
> [check obs 8] Pull out the book #ss_ok #ss_fail
> Leave it #sk_bye
#ss_ok
OBSERVATION: "Tales of the North, for Young Readers." Every one of Eirik's "forgotten sagas" is in it, word for word. A fraud. A small, cheerful one.
@clue fake_sagas
@end
#ss_fail
OBSERVATION: Old paper and smoke.
@end
#ss_done
-: Tales of the North, for Young Readers.
@end

#norns_tree
? clue:blue_fibre #nt_done
-: The great ash at the heart of Skjold. Among its roots three women sit at a loom, weaving. Their faces are bark.
norns: WE WEAVE WHAT WAS. SOMEONE HAS BEEN UNPICKING IT. LOOK AT THE CUT THREAD.
> [check obs 11] Look closely at the loom #nt_ok #nt_fail
> [fav_norns] Thank them #nt_thanks
#nt_ok
OBSERVATION: Among the grey and gold threads, one {police-blue} fibre, snagged where someone cut the cloth of the town's memory.
@clue blue_fibre
norns: THREE MEMORIES WERE CUT LOOSE. FIND THEM AND WEAVE THEM BACK.
@end
#nt_fail
OBSERVATION: Threads and threads. Your eyes swim.
@end
#nt_thanks
norns: GO WELL.
@end
#nt_done
? fav_norns #nt_after
? memories>=3 #nt_reweave
norns: THE CUT THREADS. BRING THEM BACK TO US.
@end
#nt_reweave
-: You hold out the three memories you've gathered: a name, a song, a day. The Norns weave them back, quick as rain.
@favour norns lore
@teach fate_thread
norns: THE PAST IS MENDED. THE FUTURE STILL HAS LOOSE ENDS. ONE OF THEM IS YOURS.
@end
#nt_after
norns: WE SEE A WATERFALL IN YOUR THREAD, SMALL ONE.
@end

#hilde
? raven_seal #hilde_after
? ded_c4<2 #hilde_notyet
hilde(2): Hilde Ravnsdottir. Smith and Warden. I also made a cage last month, for a man with a police seal on his order. Battle me, and I'll show you the order.
> "Deal." #hilde_fight
> "Not yet." #sk_bye
#hilde_notyet
hilde(2): Not yet. You don't know enough to ask me the right question.
@end
#hilde_fight
@battle 23 #hilde_win #hilde_lose
#hilde_win
+raven_seal
@money 500
hilde(1): Good. The Raven Seal. And here, the order.
@clue requisition
hilde: "One cage, iron, bird-sized, no runes." Signed with the seal of the Chief Inspector of the capital. Castellane.
@save
@end
#hilde_lose
hilde(2): Hot iron bends. Come back cooler.
@end
#hilde_after
hilde(1): Muninn's back? Then my cage was the wrong one. Good.
@end

#juno_skjold
? case4_closed #js_closed
juno(5): Don't. Whatever you've heard about me being here alone, don't.
> [juno_trust2] "I'm not here to accuse you." #js_trust
> "Then explain the carriage at Greenwold." #js_hard
> "Later." #sk_bye
#js_trust
+juno_trust3
juno(3): ...Here. My notebook. Everything I've found on my father. The dates. The money. Read it before you decide anything about me.
@clue juno_note
@end
#js_hard
juno(2): I don't have to explain myself to you. But here. Read it, and then tell me I'm guilty.
@clue juno_note
@end
#js_closed
juno(3): I'll take him to the capital myself. In irons. It's what he'd have done to anyone else.
@end

#sea_cave
? !case4_closed #cave_shut
-: A cleft in the cliff, sea-noise inside, and a path down to black water.
@warp seacave 7 7 N
@end
#cave_shut
? clue:police_boat #cave_go
-: A cleft in the cliff. You can hear the sea inside. Without knowing why, you'd only be guessing.
@end
#cave_go
-: A cleft in the cliff, with the scrape of a keel on the stones below. Someone comes here by boat.
> "I know who's behind this." #cave_accuse
> Wait and think #sk_bye
#cave_accuse
? ded_c4<3 #cave_notyet
@goto sk_accuse
#cave_notyet
LOGIC: Not yet. You'd be guessing. Tie the cage, the boat and the thread together on the board first.
@end

#bram_jetty
bram: ...
@end

#sk_accuse
@accuse skjold
@end

#acc_k_eirik
-: Eirik is locked in his own hall. The next dawn, still one raven. His only crime was a penny book.
@composure -1
@end
#acc_k_asta
-: They take Asta's cage as evidence. Without it, when Muninn comes home, there is nothing to keep her safe.
+asta_ruined
@composure -1
@end
#acc_k_juno
-: Juno looks at you for a long moment. Then she walks to the station and gets on the first train, without a word.
+juno_lost
@composure -1
@end
#acc_k_weak
-: You name the Chief Inspector, but you can't show what he did or why. He smiles, and has you thrown out of his office.
@composure -1
@end
#acc_k_right
@chapter Thought and Memory|Accusation
-: Chief Inspector Bram Castellane. Who ordered an iron cage with his own seal. Who rowed a police launch to the sea cave every night. To silence a town's memory, for the Collector.
+case4_closed
+bram_in_cave
@set case4 1
-: You go down to the sea cave. He's there, with the cage, and with his creatures.
@warp seacave 7 7 N
@end

#bram_cave
? bram_beaten #bram_after
bram(2): Little apprentice. Ambrose said you'd come. He was always proud of you. It made me sick.
@battle 24 #bram_win #bram_lose
#bram_win
+bram_beaten
-bram_in_cave
bram(3): ...I owed money. Bad money. Ambrose paid it. All he wanted was a favour at the Falls, a year ago. A man who looked the other way.
@goto muninn_free
#bram_lose
bram(2): Go home. Forget. Everyone here does.
@end
#bram_after
@end

#muninn_cage
? muninn_free #mun_after
? !bram_beaten #bram_cave
-: An iron cage, plain and cold. Inside, a raven, so still you think she's stone.
-: The lock is police iron. The Inspector's key opens it.
@goto muninn_free
#muninn_free
+muninn_free
@flash
-: Muninn hops out, shakes herself, and flies to your shoulder. She is heavier than she looks, and she smells of old snow.
muninn: KRAAA. YOU FORGOT SOMETHING TOO, SMALL ONE. HERE.
@card 14
-: The Falls. Again. But this time you see it: the Professor steps off the path onto a ledge you couldn't see. A rope. A second man in a police coat, coiling it.
-: He didn't fall. He climbed down, and Castellane threw the walking stick after him.
@clue falls_memory
@favour ravens memory
@teach recall
MEMORY: Two came. One returned. He meant you to remember.
? juno_lost #mun_juno_lost
? juno_trust3 #mun_juno_good
? juno_trust2 #mun_juno_good
juno(3): I heard. All of it. ...Take him, Wren. I can't look at him.
@goto mun_end
#mun_juno_good
+juno_allied
juno(3): Father. Stand up. You're under arrest. I'll read you your rights myself, and I'll do it properly, like you taught me.
juno: Wren. When you go to Ur-Lumen... I'm coming with you.
@goto mun_end
#mun_juno_lost
-: Juno isn't here. You arrest him yourself.
#mun_end
-: Huginn, Thought, lands beside Muninn on the cave's rock. "WE SEE FAR," he says. "THE MASKED MAN WAITS IN THE CITY OF WALLS. THE LAST THING HE NEEDS GROWS UNDER IT."
@favour huginn logic
+town_ur_open
@money 600
@chapter The Last King's Garden|Ur-Lumen awaits
@save
@warp skjold 5 18 S
@end
#mun_after
muninn: KRAA. MEMORY IS HEAVY. CARRY IT ANYWAY.
@end

#tr_skjold1
? beat_tr_skjold1 #trk1_after
-: A skier with frost in her eyebrows skids to a stop in front of you.
@battle 21 #trk1_win
#trk1_win
+beat_tr_skjold1
-: "Ha! Quick in the snow. Quicker than you look."
@end
#trk1_after
-: The skier waves a pole.
@end
#tr_skjold2
? beat_tr_skjold2 #trk2_after
-: The smith's apprentice has soot on his face and a clockwork bird on his arm.
@battle 22 #trk2_win
#trk2_win
+beat_tr_skjold2
-: "My bird's still ticking. Good battle."
@end
#trk2_after
-: The apprentice is oiling his bird.
@end

#frag_skjold1
+frag_skjold1
@add fragments 1
@add memories 1
-: A glowing page in the snow. Touching it, you hear a woman's name, spoken with love: Ingrid.
@card 12
@end
#frag_skjold2
+frag_skjold2
@add fragments 1
@add memories 1
-: A page frozen into the ice, humming a lullaby nobody in Skjold can remember the words of. Now you can.
@card 13
@end
#frag_skjold3
+frag_skjold3
@add fragments 1
@add memories 1
-: A page under Asta's bench, warm, smelling of a midsummer day the town lost.
@card 15
@end
#frag_skjold4
+frag_skjold4
@add fragments 1
-: A page in the sea cave, dry above the tide line.
@card 14
@end
)GGS";

} // namespace Gaslight
