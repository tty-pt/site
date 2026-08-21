#ifndef SITE_UI_C
#define SITE_UI_C

#include "site_paths.c"
#include "site_layout.c"
#include "site_forms.c"
#include "site_media.c"

#ifndef __wasm__
#include "site_chrome.c"
#include "site_page.c"
#endif

#endif
