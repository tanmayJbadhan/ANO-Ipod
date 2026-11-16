// Gaslight & Griffins - hints. "Hint" in the pause menu starts #hint, which looks at
// how far the story has got and says what to do next, in Wren's own LOGIC voice.
// The checks run in story order: the first thing not yet done is the hint.
// hint_link is set by UI::hint() when two clues in hand would join on the board.
#include "Game.h"

namespace Gaslight {

const char *const STORY_HINTS = R"GGS(
#hint
? hint_link #h_board
? !case1_open #h_c1_start
? !case1_closed #h_c1
? !tide_seal #h_c1_tide
? !act1_done #h_c1_rock
? town>=5 #h_ur
? town>=4 #h_sk
? town>=3 #h_gr
? town>=2 #h_sa
@goto h_next

#h_board
LOGIC: Two of your clues fit together. Open the menu (Centre on empty ground), choose Deduction board, pick one clue, then the other.
@end

#h_c1_start
LOGIC: Mrs. Quill said a constable is waiting. Walk out of the lodgings (down, off the mat by the door) and talk to Constable Dobbs in the street.
@end

#h_c1
? deductions>=3 #h_c1_accuse
LOGIC: Dobbs gave three leads: the harbourmaster's ledger at the Harbour Office (west), Silas at the lighthouse (far north-east), and the singer at the Drowned Lantern tavern.
LOGIC: Talk to everyone and examine anything odd. When two clues fit, join them on the Deduction board. Three deductions and you're ready.
@end
#h_c1_accuse
LOGIC: You've pieced it together. Go to the Constabulary (south-west) and tell Dobbs you're ready to name who did it. Choose who, why, how, and the proof.
@end

#h_c1_tide
LOGIC: The singing came from out on the water. Captain Halloran at the Tide Lodge (south side of town) has a boat. Beat him in a battle and he'll take you to Wailing Rock.
LOGIC: If his creatures are too strong, train in the tall grass on Gull Cliffs (west of town) and rest at Mrs. Quill's first.
@end

#h_c1_rock
LOGIC: Talk to Captain Halloran at the Tide Lodge: he'll sail you to Wailing Rock. On the Rock, play the singing cylinder for the Sisters.
@end

#h_sa
? case2_closed #h_next
? !case2_open #h_sa_start
? ded_c2>=3 #h_sa_accuse
LOGIC: Search the Hall of Weighing (the temple at the north of the square): the sarcophagus room, the vent, the empty case.
LOGIC: Talk to Kamal Idris, Sefa Okon at Okon Restorations, Lady Neferet and Tomas the guide. Catch their lies with clues you hold, and join clues on the board. Three deductions and you're ready.
@end
#h_sa_start
LOGIC: The Hall of Weighing is the great temple at the north of the square. Talk to the guard inside about the stolen Feather.
@end
#h_sa_accuse
LOGIC: You've seen the whole shape of it. Tell the guard in the Hall of Weighing "I know who did it", then name who, why, how, and show the proof.
@end

#h_gr
? case3_closed #h_next
? ded_c3>=3 #h_gr_accuse
LOGIC: Gareth Lyle vanished a year after his bargain with the Green Knight. Look at the Green Chapel (the mound to the south-west), the costume chest in Hautdesert Lodge, and the Knight himself.
LOGIC: Talk to Lord Bertram, Lady Rosalind, and Morgana at her hut. If the Knight beat you, go back: he'll fight again. Join clues on the board: three deductions and you're ready.
@end
#h_gr_accuse
LOGIC: You know what happened to Gareth. Tell Lord Bertram (in the square) "I know who did it", and lay it out: who, why, how, and the proof.
@end

#h_sk
? case4_closed #h_next
? !clue:police_boat #h_sk_boat
? ded_c4>=3 #h_sk_accuse
LOGIC: The cage, the boat and the thread: join them on the Deduction board. When you have three deductions, go to the sea cave.
@end
#h_sk_boat
LOGIC: Skjold is forgetting its own days. Ask around: Sigga, the villagers, Asta at her workshop, the Forge, the sagas in the Skald's Hall. Someone has seen a boat at night.
@end
#h_sk_accuse
LOGIC: Go to the cleft in the cliff at the south-west of town, the sea cave, and say you know who's behind this.
@end

#h_ur
? collector_done #h_done
? !ziggurat_open #h_ur_gate
? !curators_beaten #h_ur_curators
LOGIC: The way down is open. Take the stair at the back of the Museum of Last Things into the Sanctum. The Collector is waiting. Rest and heal your team first.
@end
#h_ur_gate
LOGIC: The Museum of Last Things is the ziggurat at the north of the city. Talk to the curator at its door. Siduri at her alehouse (west) might know another way in.
@end
#h_ur_curators
LOGIC: Four masked Curators guard the museum's hall. Beat all four and the stair down to the Sanctum opens.
@end
#h_done
LOGIC: The case is over. You can still explore, train, find story pages and visit the Wilds behind each town.
@end

#h_next
? !case2_closed #h_go_south
? !case3_closed #h_go_south
? !case4_closed #h_go_north
LOGIC: Take the train to Ur-Lumen, the capital, from the conductor by the station. The last case is waiting.
@end
#h_go_south
LOGIC: Take the train from the conductor by the station. Sandmere's stolen Feather and Greenwold's missing guest are both waiting: solve whichever you haven't.
@end
#h_go_north
LOGIC: Both southern cases are closed. Take the train north to Skjold, on the fjord.
@end
)GGS";

} // namespace Gaslight
