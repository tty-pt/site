/* mods/song/schema.h */
#ifndef SONG_SCHEMA_H
#define SONG_SCHEMA_H

#define SONG_SCHEMA(F) \
    F(STR, id,        "ID",     .max = 128, .pk = 1) \
    F(STR, title,     "Title",  .max = 256, .req = 1, .sort = 1, .search = 1) \
    F(STR, author,    "Author", .max = 256, .sort = 1, .search = 1) \
    F(STR, type,      "Type",   .max = 128, .sort = 1) \
    F(INT, transpose, "Key",    .def = 0) \
    F(INT, zoom,      "Zoom",   .def = 100) \
    F(STR, yt,        "Video",  .max = 512) \
    F(STR, audio,     "Audio",  .max = 512) \
    F(STR, pdf,       "PDF",    .max = 512)

#include <site/schema_dsl.h>
SITE_DECLARE_MODULE_SCHEMA(song, SONG_SCHEMA)

#endif
