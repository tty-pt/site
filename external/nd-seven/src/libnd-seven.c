#include <ttypt/xy-mod.h>
#include <nd/seven.h>
#include <stdio.h>

XY_MODULE_API void
xy_install(void)
{
	printf("nd-seven: 2D Zelda-like room action module loaded into region\n");
}

XY_MODULE_API void
xy_uninstall(void)
{
	printf("nd-seven: 2D Zelda-like room action module unloaded from region\n");
}
