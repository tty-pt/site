#ifndef VIEWER_ZOOM_H
#define VIEWER_ZOOM_H

#ifndef VIEWER_ZOOM_MIN
#define VIEWER_ZOOM_MIN 70
#define VIEWER_ZOOM_MAX 200
#define VIEWER_ZOOM_DEFAULT 100
#endif

/* Stringize macro for embedding numeric constants in HTML attrs */
#ifndef VZ_STR
#define VZ_STR_HELPER(x) #x
#define VZ_STR(x) VZ_STR_HELPER(x)
#endif

#endif
