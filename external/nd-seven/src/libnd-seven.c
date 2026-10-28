#include <ttypt/xy-mod.h>
#include <nd/seven.h>
#include <stdio.h>

XY_MODULE_API void
xy_install(void)
{
	printf("nd-seven: 2D continuous world module loaded into region\n");
}

XY_MODULE_API void
xy_uninstall(void)
{
	printf("nd-seven: 2D continuous world module unloaded from region\n");
}
