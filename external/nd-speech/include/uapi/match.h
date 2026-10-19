/* $Header$ */

#ifndef _MATCH_H
#define _MATCH_H

#include <ttypt/xy.h>

#define NOMATCH_MESSAGE "I don't know what you mean.\n"
#define CANTDO_MESSAGE "You can't do that.\n"

#ifndef ND_WORLD_H
XY_DECL(unsigned, ematch_at, unsigned, player_ref, unsigned, where_ref, char *, name);
XY_DECL(unsigned, ematch_player, char *, name);
XY_DECL(unsigned, ematch_absolute, char *, name);
XY_DECL(unsigned, ematch_me, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_here, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_mine, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_near, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_all, unsigned, player_ref, char *, name);
#endif

#endif /* _MATCH_H */
