#ifndef MPFD_H
#define MPFD_H

#include <ttypt/axil-xy.h>

/* Parse & Lifecycle */
XY_DECL(int, mpfd_parse, socket_t, fd, char *, body);

/* Data Retrieval */
XY_DECL(int, mpfd_get, const char *, name, char *, buf, size_t, buf_len);

#endif
