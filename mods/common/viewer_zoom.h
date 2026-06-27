#ifndef VIEWER_ZOOM_H
#define VIEWER_ZOOM_H

#ifndef VIEWER_ZOOM_MIN
#define VIEWER_ZOOM_MIN 70
#define VIEWER_ZOOM_MAX 200
#define VIEWER_ZOOM_DEFAULT 100
#endif

/* Stringize macro for embedding numeric constants in HTML attrs */
#ifndef STR
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#endif

#endif
