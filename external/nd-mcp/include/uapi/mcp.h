#ifndef UAPI_MCP_H
#define UAPI_MCP_H

#include <stddef.h>
#include <ttypt/xy.h>

#ifndef ND_WORLD_H
XY_DECL(int, mcp_content_out, unsigned, loc_ref, unsigned, thing_ref);
XY_DECL(int, mcp_content_in, unsigned, loc_ref, unsigned, thing_ref);
#endif

#ifndef ND_WORLD_H
XY_DECL(int, fbcp_item, unsigned, player_ref, unsigned, obj_ref, unsigned char, dynflags);
#endif
#ifndef ND_MCP_PROVIDER
static inline void eng_fbcp_item(unsigned player_ref, unsigned obj_ref, unsigned char dynflags) {
	fbcp_item(player_ref, obj_ref, dynflags);
}
#endif

typedef void fbcp_t(unsigned player_ref, size_t len, unsigned char iden, void *data);
fbcp_t eng_fbcp;

typedef void mcp_bar_t(unsigned char iden, unsigned player_ref, unsigned short val, unsigned short max);
mcp_bar_t eng_mcp_bar;

#endif
