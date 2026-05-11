#ifndef MPFD_H
#define MPFD_H

#include <ttypt/axil-ndx.h>

/* Parse & Lifecycle */
NDX_HOOK_DECL(int, mpfd_parse, socket_t, fd, char *, body);

/* Data Retrieval */
NDX_HOOK_DECL(int, mpfd_get, const char *, name, char *, buf, size_t, buf_len);

#endif
