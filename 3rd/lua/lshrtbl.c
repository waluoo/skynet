#include "lshrtbl.h"
#include "rwlock.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

// 128K
#define SHRSTR_SLOT 0x10000
#define HASH_NODE(h) ((h) % SHRSTR_SLOT)

struct shrmap_slot {
	struct rwlock lock;
	TString *str;
};

struct shrmap {
	struct shrmap_slot h[SHRSTR_SLOT];
	unsigned int seed;
	int version;
};

// TString in shrmap don't need CommonHeader,
// So convert TString to VString to get version
typedef struct VString {
	int version;
} VString;

static struct shrmap *SSM = NULL;

LUA_API void
luaS_initshr() {
	struct shrmap * s = malloc(sizeof(*s));
	memset(s, 0, sizeof(*s));
	int i;
	for (i=0;i<SHRSTR_SLOT;i++) {
		rwlock_init(&s->h[i].lock);
	}
	s->seed = (unsigned int)time(NULL);
	s->version = 1;

	SSM = s;
}

LUA_API void
luaS_exitshr() {
	int i;
	for (i=0;i<SHRSTR_SLOT;i++) {
		TString *str = SSM->h[i].str;
		while (str) {
			TString * next = str->u.hnext;
			free(str);
			str = next;
		}
	}
	free(SSM);
}

static TString *
query_string(lua_State *L, unsigned int h, const char *str, lu_byte l) {
	struct shrmap_slot *s = &SSM->h[HASH_NODE(h)];
	rwlock_rlock(&s->lock);
	TString *ts = s->str;
	while (ts) {
		if (ts->hash == h && 
			ts->shrlen == l &&
			memcmp(str, ts+1, l) == 0) {
			VString *v = (VString *)ts;
			if (v->version > 0) {
				if (G(L)->ssversion) {
					v->version = SSM->version;
				} else {
					v->version = 0;
				}
				break;
			}
		}
		ts = ts->u.hnext;
	}
	rwlock_runlock(&s->lock);
	return ts;
}

static TString *
new_string(lua_State *L, unsigned int h, const char *str, lu_byte l) {
	size_t sz = sizelstring(l);
	TString *ts = malloc(sz);
	memset(ts, 0, sz);
	ts->marked = 0xff;	// force gc mark it
	ts->tt = LUA_TSHRSTR;
	ts->hash = h;
	ts->shrlen = l;
	memcpy(ts+1, str, l);
	VString *v = (VString *)ts;
	if (G(L)->ssversion) {
		v->version = SSM->version;
	}
	return ts;
}

static TString *
add_string(lua_State *L, unsigned int h, const char *str, lu_byte l) {
	TString * tmp = new_string(L, h, str, l);
	struct shrmap_slot *s = &SSM->h[HASH_NODE(h)];
	rwlock_wlock(&s->lock);
	TString **last = &s->str;
	TString *ts = s->str;
	while (ts) {
		if (ts->hash == h && 
			ts->shrlen == l &&
			memcmp(str, ts+1, l) == 0)
				break;
		last= &ts->u.hnext;
		ts = ts->u.hnext;
	}
	if (ts == NULL) {
		ts = tmp;
		*last = ts;
		tmp = NULL;
	}
	rwlock_wunlock(&s->lock);
	if (tmp) {
		// string is create by other thread, so free tmp
		free(tmp);
	}
	return ts;
}

LUAI_FUNC TString *
luaS_newshr(lua_State *L, const char *str, lu_byte l) {
	unsigned int h = luaS_hash(str, l, SSM->seed);
	TString * ts = query_string(L, h, str, l);
	if (ts) {
		return ts;
	}
	return add_string(L, h, str, l);
}

LUAI_FUNC TString *
luaS_markshr(TString * ts) {
	VString *v = (VString *)ts;
	if (v->version == 0) {
		return ts;
	}
	v->version = SSM->version;
	return ts;
}

LUA_API void
luaS_initshrversion(lua_State *L) {
	G(L)->ssversion = SSM->version;
}

LUA_API int
luaS_shrgen() {
	return ++SSM->version;
}

static int
collect(struct shrmap_slot *s, int gen) {
	rwlock_wlock(&s->lock);
	TString **last = &s->str;
	TString *ts = s->str;
	int n = 0;
	while(ts) {
		VString *vs = (VString *)ts;
		if (vs->version > 0 && vs->version < gen) {
			++n;
			TString *next = ts->u.hnext;
			free(ts);
			ts = next;
			*last = ts;
		} else {
			last= &ts->u.hnext;
			ts = ts->u.hnext;
		}
	}
	rwlock_wunlock(&s->lock);
	return n;
}

LUA_API int
luaS_collectshr(int gen) {
	int i;
	int n = 0;
	for (i=0;i<SHRSTR_SLOT;i++) {
		struct shrmap_slot *s = &SSM->h[i];
		n += collect(s, gen);
	}
	return n;
}

struct slotinfo {
	int len;
	int minversion;
	int size;
};

static void
getslot(struct shrmap_slot *s, struct slotinfo *info) {
	memset(info, 0, sizeof(*info));
	rwlock_rlock(&s->lock);
	TString *ts = s->str;
	int minversion = SSM->version;
	while (ts) {
		VString *v = (VString *)ts;
		++info->len;
		int version = v->version;
		if (version > 0 && version < minversion) {
			minversion = version;
		}
		info->size += ts->shrlen;
		ts = ts->u.hnext;
	}
	rwlock_runlock(&s->lock);
	info->minversion = minversion;
}

LUA_API int
luaS_shrinfo(lua_State *L) {
	struct slotinfo total;
	struct slotinfo tmp;
	memset(&total, 0, sizeof(total));
	int i;
	int len = 0;
	total.minversion = SSM->version;
	for (i=0;i<SHRSTR_SLOT;i++) {
		struct shrmap_slot *s = &SSM->h[i];
		getslot(s, &tmp);
		len += tmp.len;
		if (tmp.len > total.len) {
			total.len = tmp.len;
		}
		if (tmp.minversion < total.minversion) {
			total.minversion = tmp.minversion;
		}
		total.size += tmp.size;
	}
	lua_pushinteger(L, len);
	lua_pushinteger(L, total.size);
	lua_pushinteger(L, total.len);
	lua_pushinteger(L, total.minversion);
	return 4;
}
