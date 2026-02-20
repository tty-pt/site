#include <stddef.h>

#include <ttypt/ndx.h>

#include "papi.h"

MODULE_API void
ndx_install(void)
{
}

MODULE_API void
ndx_open(void)
{
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	static ndx_t ndx;
	return &ndx;
}
