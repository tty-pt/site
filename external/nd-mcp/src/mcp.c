#include <nd/mcp.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/axil.h>
#include <ttypt/corm.h>
#include <nd/view-types.h>

#include <nd/entity.h>
#include <nd/type.h>
#include <nd/regions.h>
#include <ttypt/xy.h>

#define SUPERBIGSIZ 80000 * 8192

enum bcp_type {
	BCP_ITEM,
	BCP_VIEW,
	BCP_VIEW_BUFFER,
	BCP_ROOM,
	BCP_ENTITY,
	BCP_AUTH_FAILURE,
	BCP_AUTH_SUCCESS,
	BCP_OUT,
	BCP_TOD,
	BCP_ACTION,
	BCP_REGION = ND_BCP_REGION,
};

XY_DECL(struct icon, object_icon, unsigned, player_ref, unsigned, thing_ref);
void eng_nd_wwrite(unsigned player_ref, void *msg, size_t len);

int
strcmp_nocase(const char *s1, const char *s2)
{
	while (*s1 && tolower(*s1) == tolower(*s2))
		s1++, s2++;
	return (tolower(*s1) - tolower(*s2));
}

/* Used internally to escape and quote argument values. */
int
msgarg_escape(char* buf, int bufsize, const char* in)
{
	char *out = buf;
	int len = 0;

	if (bufsize < 3) {
		return 0;
	}
	*out++ = '"';
	len++;
	while (*in && len < bufsize - 3) {
		if (*in == '"' || *in == '\\') {
			*out++ = '\\';
			len++;
		}
		*out++ = *in++;
		len++;
	}
	*out++ = '"';
	*out = '\0';
	len++;

	return len;
}

void
eng_fbcp(unsigned player_ref, size_t len, unsigned char iden, void *data)
{
	char bcp_buf[2 + sizeof(iden) + len];
	char *p = bcp_buf;
	memcpy(p, "#b", 2);
	memcpy(p += 2, &iden, sizeof(iden));
	memcpy(p += sizeof(iden), data, len);
	eng_nd_wwrite(player_ref, bcp_buf, (p += len) - bcp_buf);
}

void
eng_mcp_action(unsigned player_ref, unsigned id,
		char *label, char *icon)
{
	unsigned char iden = BCP_ACTION;
	size_t label_len = strlen(label) + 1,
	       icon_len = strlen(icon) + 1,
	       pos = 2;
	char bcp_buf[2 + sizeof(iden) + sizeof(id) + label_len + icon_len];
	memcpy(bcp_buf, "#b", 2);
	memcpy(bcp_buf + pos, &iden, sizeof(iden));
	pos += sizeof(iden);
	memcpy(bcp_buf + pos, &id, sizeof(id));
	pos += sizeof(id);
	memcpy(bcp_buf + pos, label, label_len);
	pos += label_len;
	memcpy(bcp_buf + pos, icon, icon_len);
	pos += icon_len;
	eng_nd_wwrite(player_ref, bcp_buf, pos);
}

typedef int _fbcp_item_t(char *, unsigned, unsigned, unsigned char);

int
_fbcp_item(char *bcp_buf, unsigned player_ref, unsigned obj_ref, unsigned char dynflags)
{
	int aux, aux1;
	OBJ obj = {
		.location = -1,
		.name = "**NOTHING!**",
	};
	nd_get(HD_OBJ, &obj, &obj_ref);
	struct icon ico = object_icon(player_ref, obj_ref);
	unsigned char iden = BCP_ITEM;
	char *p = bcp_buf, *aux2;
	memcpy(p, "#b", 2);
	memcpy(p += 2, &iden, sizeof(iden));
	aux = obj_ref;
	memcpy(p += sizeof(iden), &aux, sizeof(aux));
	aux = obj.location;
	memcpy(p += sizeof(aux), &aux, sizeof(aux));
	memcpy(p += sizeof(aux), &dynflags, sizeof(dynflags));
	memcpy(p += sizeof(dynflags), &obj.type, sizeof(obj.type));
	memcpy(p += sizeof(obj.type), &ico.actions, sizeof(ico.actions));
	memcpy(p += sizeof(ico.actions), obj.name, aux = strlen(obj.name) + 1);
	aux2 = (char *) unparse(obj_ref);
	memcpy(p += aux, aux2, aux1 = strlen(aux2) + 1);
	memcpy(p += aux1, &ico.pi.fg, sizeof(ico.pi.fg));
	memcpy(p += sizeof(ico.pi.fg), &ico.pi.flags, sizeof(ico.pi.flags));
	memcpy(p += sizeof(ico.pi.flags), &ico.ch, sizeof(ico.ch));
	aux2 = object_art(obj_ref);
	memcpy(p += sizeof(ico.ch), aux2, aux1 = strlen(aux2) + 1);
	p += aux1;
	switch (obj.type) {
	case TYPE_ROOM: {
		ROO *roo = (ROO *) &obj.data;
		memcpy(p, &roo->exits, sizeof(roo->exits));
		p += sizeof(roo->exits);
		break;
	}
	case TYPE_ENTITY: {
		unsigned flags = ent_get(obj_ref).flags;
		memcpy(p, &flags, sizeof(flags));
		p += sizeof(flags);
		break;
	}
	}

	return p - bcp_buf;
}

void
eng_fbcp_item(unsigned player_ref, unsigned obj_ref, unsigned char dynflags)
{
	OBJ obj = {
		.location = -1,
		.name = "**NOTHING!**",
	};
	nd_get(HD_OBJ, &obj, &obj_ref);
	struct icon ico = object_icon(player_ref, obj_ref);

	nd_entity_packet_t pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.ref = obj_ref;
	pkt.location = obj.location;
	pkt.type = obj.type;
	pkt.actions = ico.actions;
	pkt.fg = (uint16_t)ico.pi.fg;
	pkt.flags = (uint16_t)ico.pi.flags;
	pkt.dynflags = dynflags;
	pkt.ch = ico.ch;
	pkt.side = (uint8_t)obj.data[0];
	snprintf(pkt.name, sizeof(pkt.name), "%s", obj.name);
	char *u = (char *)unparse(obj_ref);
	if (u)
		snprintf(pkt.unparse, sizeof(pkt.unparse), "%s", u);
	char *a = object_art(obj_ref);
	if (a)
		snprintf(pkt.art, sizeof(pkt.art), "%s", a);

	eng_fbcp(player_ref, sizeof(pkt), BCP_ITEM, &pkt);
}

static int // 2 + sizeof(iden) + sizeof(int) * 2
_fbcp_out(char *bcp_buf,
		unsigned player_ref __attribute__((unused)),
		unsigned obj_ref,
		unsigned char dynflags __attribute__((unused)))
{
	int aux;
	unsigned char iden = BCP_OUT;
	char *p = bcp_buf;
	memcpy(p, "#b", 2);
	memcpy(p += 2, &iden, sizeof(iden));
	aux = obj_ref;
	memcpy(p += sizeof(iden), &aux, sizeof(aux));
	OBJ obj;
	nd_get(HD_OBJ, &obj, &obj_ref);
	aux = obj.location;
	memcpy(p += sizeof(aux), &aux, sizeof(aux));
	p += sizeof(aux);
	return p - bcp_buf;
}

static void
fbcp_auth_success(unsigned player_ref)
{
	unsigned char iden = BCP_AUTH_SUCCESS;
	static char bcp_buf[2 + sizeof(iden) + sizeof(player_ref)];
	char *p = bcp_buf;
	memcpy(p, "#b", 2);
	memcpy(p += 2, &iden, sizeof(iden));
	memcpy(p += sizeof(iden), &player_ref, sizeof(player_ref));
	p += sizeof(player_ref);
	eng_nd_wwrite(player_ref, bcp_buf, p - bcp_buf);
}

static void
fbcp_auth_failure(int fd, int reason)
{
	unsigned char iden = BCP_AUTH_FAILURE;
	static char bcp_buf[2 + sizeof(iden) + sizeof(reason)];
	char *p = bcp_buf;
	memcpy(p, "#b", 2);
	memcpy(p += 2, &iden, sizeof(iden));
	memcpy(p += sizeof(iden), &reason, sizeof(reason));
	p += sizeof(reason);
	axil_write(fd, bcp_buf, p - bcp_buf);
}

void
eng_mcp_bar(unsigned char iden, unsigned player_ref, unsigned short val, unsigned short max)
{
	static char bcp_buf[2 + sizeof(iden) + sizeof(int) * 4];
	char *p = bcp_buf;
	unsigned short aux;
	memcpy(p, "#b", 2);
	memcpy(p += 2, &iden, sizeof(iden));
	memcpy(p += sizeof(iden), &val, sizeof(val));
	aux = max;
	memcpy(p += sizeof(val), &aux, sizeof(aux));
	p += sizeof(aux);
	eng_nd_wwrite(player_ref, bcp_buf, p - bcp_buf);
}

void
eng_fbcp_view(unsigned player_ref, const nd_map_data_t *map)
{
	eng_fbcp(player_ref, sizeof(nd_map_data_t), BCP_VIEW, (void *)map);
}

void
eng_fbcp_view_buf(unsigned player_ref, char *view_buf)
{
	eng_fbcp(player_ref, strlen(view_buf), BCP_VIEW_BUFFER, view_buf);
	nd_twrites(player_ref, view_buf, strlen(view_buf));
}

extern unsigned fds_hd;

static void
fbcp_observers(unsigned loc_ref, unsigned thing_ref, unsigned dynflags)
{
	unsigned tmp_ref = 0, c;
	_fbcp_item_t *callback = &_fbcp_item;

	if (dynflags & 4) {
		dynflags &= ~4;
		callback = &_fbcp_out;
	}

	c = nd_iter(HD_OBS, &loc_ref);

	const void *kp, *vp;
	while (corm_next(&kp, &vp, c)) {
		static char bcp_buf[SUPERBIGSIZ];
		OBJ tmp;
		size_t len;

		loc_ref = *(const unsigned *)kp;
		tmp_ref = *(const unsigned *)vp;
		nd_get(HD_OBJ, &tmp, &tmp_ref);
		if (tmp.type != TYPE_ENTITY)
			continue;

		len = callback(bcp_buf, tmp_ref, thing_ref, dynflags);
		eng_nd_wwrite(tmp_ref, bcp_buf, len);
	}
}

void
eng_mcp_content_out(unsigned loc_ref, unsigned thing_ref) {
	fbcp_observers(loc_ref, thing_ref, 2 | 4);
}

void
eng_fbcp_region(unsigned player_ref, const char *stage, const char *map, uint8_t layout, uint8_t flags)
{
	const char *s = (stage && *stage) ? stage : "tty";
	const char *m = (map && *map) ? map : "ascii-bud";
	uint8_t buf[64];
	size_t pos = 0;
	size_t slen = strlen(s) + 1;
	size_t mlen = strlen(m) + 1;

	if (pos + slen + mlen + 2 > sizeof(buf))
		return;

	memcpy(buf + pos, s, slen);
	pos += slen;
	memcpy(buf + pos, m, mlen);
	pos += mlen;
	buf[pos++] = layout;
	buf[pos++] = flags;

	eng_fbcp(player_ref, pos, ND_BCP_REGION, buf);
}

void
eng_mcp_send_region_driver(unsigned player_ref)
{
	char stage[16] = {0};
	char map[16] = {0};
	uint8_t layout = 0;
	uint8_t flags = 0;
	if (st_player_resolve_presentation(player_ref, stage, sizeof(stage),
	                                   map, sizeof(map), &layout, &flags) == 0) {
		eng_fbcp_region(player_ref, stage, map, layout, flags);
	}
}

void
eng_mcp_content_in(unsigned loc_ref, unsigned thing_ref) {
	fbcp_observers(loc_ref, thing_ref, 2);
	eng_mcp_send_region_driver(thing_ref);
}

void
eng_mcp_auth_fail(int descr, int reason) {
	fbcp_auth_failure(descr, reason);
}

void
eng_mcp_auth_success(unsigned player_ref) {
	fbcp_auth_success(player_ref);
	eng_mcp_send_region_driver(player_ref);
}

void
eng_mcp_tod(unsigned player_ref, unsigned tod) {
	eng_fbcp(player_ref, sizeof(tod), BCP_TOD, &tod);
}

