#ifndef MOD_AUTH_OAUTH_HOOK_H
#define MOD_AUTH_OAUTH_HOOK_H

/*
 * Optional XY token-minting hook dispatched by mods/auth after RFC 7636 PKCE
 * S256 verification succeeds on POST /auth/oauth/token.
 *
 * Caller-only header: included exclusively by mods/auth/auth.c.
 * External token-minter modules implement XY_IMPL(int, on_oauth_mint_token, ...)
 * and must NOT include this header.
 */

#include <ttypt/xy-mod.h>
#include <stddef.h>
#include <stdint.h>

XY_DECL(int, on_oauth_mint_token,
	const char *, username,
	const char *, client_id,
	const char *, app_id,
	char *, out_token,
	size_t, out_token_cap,
	uint64_t *, out_expires_in);

#endif /* MOD_AUTH_OAUTH_HOOK_H */
