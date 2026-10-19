#ifndef ND_SESSION_H
#define ND_SESSION_H

#include "session-types.h"
#include <ttypt/xy.h>

XY_DECL(unsigned, nd_connect, int, fd);
XY_DECL(int, nd_disconnect, int, fd);

#endif /* ND_SESSION_H */
