#ifndef REGION_CMD_H
#define REGION_CMD_H

#include <stdint.h>
#include <stddef.h>
#include <nd/region-types.h>

extern unsigned owner_hd;
extern unsigned ban_hd;
extern unsigned player_hd;
extern unsigned presence_hd;

int st_have_hd(void);
void st_owner_name(unsigned owner_ref, char *buf, size_t len);
int st_row_get(uint64_t id, uint8_t plen, struct st_rec *out);
void st_row_put(uint64_t id, uint8_t plen, const struct st_rec *rec);
void st_row_del(uint64_t id, uint8_t plen);
int st_can(unsigned ref, uint64_t id, uint8_t plen);
unsigned player_lookup(const char *name);
int st_cmd_world(const char *tok, uint64_t *id, uint8_t *plen);

void st_ban_put(unsigned player_ref, uint64_t id, uint8_t plen, unsigned banner);
int st_ban_lookup(unsigned player_ref, uint64_t id, uint8_t plen, unsigned *banner);
void st_ban_del(unsigned player_ref, uint64_t id, uint8_t plen);
void st_ban_place(uint64_t id, uint8_t plen, char *buf, size_t len);

int st_mod_loaded(uint64_t id, uint8_t plen, const char *name);
int st_mod_add(uint64_t id, uint8_t plen, const char *name);
int st_mod_del(uint64_t id, uint8_t plen, const char *name);
int st_region_load(uint64_t id, uint8_t plen, char *name);
int st_region_unload(uint64_t id, uint8_t plen, char *name);
int st_xy_claim_at(uint64_t id, uint8_t plen);

void st_cmd_init_xy(struct xy_ctx *ctx);
void st_register_commands(void);

#endif /* REGION_CMD_H */
