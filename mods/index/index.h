#ifndef INDEX_MOD_H
#define INDEX_MOD_H

#include <ttypt/ndx-mod.h>

NDX_DECL(unsigned, index_open, const char *, path, unsigned, mask);

NDX_DECL(unsigned, index_put, unsigned, hd, char *, key, char *, value);

NDX_DECL(unsigned, index_get, unsigned, hd, char *, value, size_t, len, char *, key);

NDX_DECL(int, index_page, unsigned, fd, unsigned, hd, char *, path, char *, title);

#endif
