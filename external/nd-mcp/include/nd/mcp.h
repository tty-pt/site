#ifndef ND_MCP_H
#define ND_MCP_H

#include "mcp-types.h"
#include <nd/bcp-types.h>
#include <ttypt/xy.h>

XY_DECL(int, mcp_auth_fail, int, descr, int, reason);
XY_DECL(int, mcp_auth_success, unsigned, player_ref);
XY_DECL(int, mcp_tod, unsigned, player_ref, unsigned, tod);
XY_DECL(int, fbcp_view, unsigned, player_ref, const nd_map_data_t *, map);
XY_DECL(int, fbcp_view_buf, unsigned, player_ref, char *, view);
XY_DECL(int, mcp_action, unsigned, player_ref, unsigned, id, char *, label, char *, icon);
XY_DECL(int, fbcp_region, unsigned, player_ref, const char *, stage, const char *, map, uint8_t, layout, uint8_t, flags);

#endif /* ND_MCP_H */
