#ifndef MPFD_H
#define MPFD_H

#include <ttypt/ndc-ndx.h>

/* Parse & Lifecycle */
NDX_DECL(int, mpfd_parse, socket_t, fd, char *, body);

/* Field Inspection - All O(1) */
NDX_DECL(int, mpfd_exists, const char *, name);
NDX_DECL(int, mpfd_len, const char *, name);
NDX_DECL(int, mpfd_filename, const char *, name, char *, buf, size_t, buf_len);

/* Data Retrieval */
NDX_DECL(int, mpfd_get, const char *, name, char *, buf, size_t, buf_len);
NDX_DECL(int, mpfd_save, const char *, name, const char *, path);

/* Configuration */
NDX_DECL(int, mpfd_set_limits, size_t, max_field_size, size_t, max_total_size);

#endif
