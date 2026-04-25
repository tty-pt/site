#ifndef PROXY_MOD_H
#define PROXY_MOD_H

/** Start proxying the request */
NDX_HOOK_DECL(int, proxy_init,
    const char *, method,
    const char *, path);

/** Set a header */
NDX_HOOK_DECL(int, proxy_header,
    const char *, name,
    const char *, val);

/** Streaming write body */
NDX_HOOK_DECL(int, proxy_write,
    const char *, data,
    size_t, len);

/** Full body write */
NDX_HOOK_DECL(int, proxy_body,
    int, fd,
    const char *, data,
    size_t, len);

/** Finalize bodyless request */
NDX_HOOK_DECL(int, proxy_head, int, fd);

/** Connect to a proxy
 * (use only once in entire server, for now)
 */
NDX_HOOK_DECL(int, proxy_connect,
    const char *, host,
    unsigned, port);

/** Add standard proxy headers (X-Modules, X-Forwarded-Host, X-Remote-User).
 *  Pulls session cookie & resolves user via auth. */
NDX_HOOK_DECL(int, proxy_add_standard_headers,
    int, fd, const char *, modules_header);

#endif
