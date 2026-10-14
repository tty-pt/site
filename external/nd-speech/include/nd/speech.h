#ifndef ND_SPEECH_H
#define ND_SPEECH_H

#include "speech-types.h"
#include <ttypt/xy.h>

XY_DECL(unsigned, ematch_here, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_near, unsigned, player_ref, char *, str);
XY_DECL(unsigned, ematch_all, unsigned, player_ref, char *, name);

#endif /* ND_SPEECH_H */
