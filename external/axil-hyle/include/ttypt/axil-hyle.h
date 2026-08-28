#ifndef AXIL_HYLE_H
#define AXIL_HYLE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic Axil-Hyle HTTP Bridge
 * Mounts standard REST CRUD endpoints (/api/dataset/...) and picker fragment
 * hot-swap endpoint (/pick/:id/options) for any Axil web application.
 */
void axil_hyle_install_routes(void);

#ifdef __cplusplus
}
#endif

#endif /* AXIL_HYLE_H */
