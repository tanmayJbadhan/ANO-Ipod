// Gaslight & Griffins - the Wilds: signposts, caches and the den.
// The land itself is written by the land model (Wilds.cpp).
#include "Game.h"

namespace Gaslight {

const char *const STORY_WILDS = R"GGS(
#wilds_sign
? wilds_visits #wilds_sign_again
-: A weathered signpost. One arm says "Town". The other is scratched blank, and under it: {never the same land twice}.
-: Out there, they say, the Wilds rearrange themselves every night. Wild creatures, and things people left behind.
> Head into the Wilds #wilds_go
> Not now #wilds_no
#wilds_sign_again
-: The signpost into the Wilds. The blank arm creaks in the wind.
> Head into the Wilds #wilds_go
> Not now #wilds_no
#wilds_go
@wilds
@end
#wilds_no
@end

#wcache0
+wc0
-: Something glints between the roots: a traveller's cache, wrapped in oilcloth.
@wildloot
@end
#wcache1
+wc1
-: A shard of light turns over a cache someone buried and forgot.
@wildloot
@end
#wcache2
+wc2
-: Under a flat stone: a tin box, a little rusted, still sealed.
@wildloot
@end
#wcache3
+wc3
-: A satchel hangs from a branch, its owner long gone.
@wildloot
@end
#wcache4
+wc4
-: Half-buried and glowing faintly: a cache left for whoever came next.
@wildloot
@end

#wild_heart
? wh #wild_heart_done
-: A ring of old stones around one tall one. It hums. The grass here lies flat, as if something very large sleeps on it.
-: Something does. It wakes.
@wildboss
@end
#wild_heart_done
-: The stones are quiet now. The way back is south, the way you came.
@end
)GGS";

} // namespace Gaslight
