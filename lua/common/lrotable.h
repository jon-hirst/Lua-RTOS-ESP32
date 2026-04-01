/* Read-only tables for Lua */

#ifndef lrotable_h
#define lrotable_h

#include "lua.h"
#include "llimits.h"
#include "lobject.h"
#include "lrodefs.h"

#include <stdio.h>
#include "luaconf_custom.h"

// TO DO: ??
#define luaS_newro(L, s)  (luaS_newlstr(L, s, strlen(s)))

  
  
/*
 * Lua 5.5 renamed internal type-tag constants to LUA_V* variant form.
 * Provide backward-compatible aliases so the rest of the code compiles.
 */
#ifndef LUA_TNUMINT
#define LUA_TNUMINT  LUA_VNUMINT   /* integer number variant */
#endif
#ifndef LUA_TNUMFLT
#define LUA_TNUMFLT  LUA_VNUMFLT   /* float number variant */
#endif

/*
 * LUA_TROTABLE: custom type tag for read-only (ROM) tables.
 * Use a LUA_TTABLE variant (bit 4 set) that is NOT GC-collectable,
 * so novariant() returns LUA_TTABLE and lua_type() returns LUA_TTABLE.
 */
#ifndef LUA_TROTABLE
#define LUA_TROTABLE  makevariant(LUA_TTABLE, 1)
#endif

/* Type-check and value accessors for rotable TValues */
#ifndef ttisrotable
#define ttisrotable(o)    checktag((o), LUA_TROTABLE)
#define rvalue(o)         (check_exp(ttisrotable(o), val_(o).p))
#define setrvalue(obj,x)  { TValue *io_=(obj); val_(io_).p=(void*)(x); \
                            settt_(io_, LUA_TROTABLE); }
/* ttnov(o): full variant tag (used to distinguish rotable from plain table) */
#define ttnov(o)          ttypetag(o)
#endif

/* Macros one can use to define rotable entries */
#ifndef LUA_PACK_VALUE
/* Lua 5.5: functions stored in .f (not .p); type tags renamed to LUA_V* */
#define LRO_FUNCVAL(v)  {{.f = v}, LUA_VLCF}
#define LRO_LUDATA(v)   {{.p = v}, LUA_VLIGHTUSERDATA}
#define LRO_NUMVAL(v)   {{.n = v}, LUA_VNUMFLT}
#define LRO_INTVAL(v)   {{.i = v}, LUA_VNUMINT}
#define LRO_ROVAL(v)    {{.p = (void*)v}, LUA_TROTABLE}
#define LRO_NILVAL      {{.p = NULL}, LUA_VNIL}
#define LRO_STRVAL(v)   {{.p = v}, LUA_TSTRING}
#else // #ifndef LUA_PACK_VALUE
#define LRO_NUMVAL(v)   {.value.n = v}
#define LRO_INTVAL(v)   {.value.i = v}
#ifdef ELUA_ENDIAN_LITTLE
#define LRO_FUNCVAL(v)  {{(int)v, add_sig(LUA_TLCF)}}
#define LRO_LUDATA(v)   {{(int)v, add_sig(LUA_TLIGHTUSERDATA)}}
#define LRO_ROVAL(v)    {{(int)v, add_sig(LUA_TROTABLE)}}
#define LRO_NILVAL      {{0, add_sig(LUA_TNIL)}}
#else // #ifdef ELUA_ENDIAN_LITTLE
#define LRO_FUNCVAL(v)  {{add_sig(LUA_TLCF), (int)v}}
#define LRO_LUDATA(v)   {{add_sig(LUA_TLIGHTUSERDATA), (int)v}}
#define LRO_ROVAL(v)    {{add_sig(LUA_TROTABLE), (int)v}}
#define LRO_NILVAL      {{add_sig(LUA_TNIL), 0}}
#endif // #ifdef ELUA_ENDIAN_LITTLE
#endif // #ifndef LUA_PACK_VALUE

#define LRO_STRKEY(k)   {LUA_TSTRING, sizeof(k) - 1, {.strkey = k}, __COUNTER__}
#define LRO_NUMKEY(k)   {LUA_TNUMINT, -1, {.numkey = k}}
#define LRO_NILKEY      {LUA_TNIL,    -1, {.strkey=NULL}, __COUNTER__}

/* Maximum length of a rotable name and of a string key*/
#define LUA_MAX_ROTABLE_NAME      32

/* Type of a numeric key in a rotable */
typedef int luaR_numkey;

/* The next structure defines the type of a key */
typedef struct
{
  int type;
  int len;
  union
  {
    const char*   strkey;
    luaR_numkey   numkey;
  } id;
  uint32_t count;
} luaR_key;

/* An entry in the read only table */
typedef struct
{
  const luaR_key key;
  const TValue value;
} luaR_entry;

const TValue* luaR_findglobal(const char *key);
int luaR_findfunction(lua_State *L, const luaR_entry *ptable);
const TValue* luaR_findentry(const void *pentry, const char *strkey, luaR_numkey numkey, unsigned *ppos);
void luaR_getcstr(char *dest, const TString *src, size_t maxsize);
void luaR_next(lua_State *L, void *data, TValue *key, TValue *val);
int luaR_isrotable(const void *p);
LUA_API void lua_pushrotable (lua_State *L, void *p);
const TValue *luaL_rometatable(const void *data);
int luaH_getn_ro (void *t);
void luaR_next(lua_State *L, void *data, TValue *key, TValue *val);
int luaH_next_ro (lua_State *L, void *t, StkId key);

int luaR_index(lua_State *L, const void *funcs, const void *consts);
int luaR_error(lua_State *L);
LUALIB_API int luaL_newmetarotable (lua_State *L, const char* tname, void *p);
void luaR_push_as_table (lua_State *L, const luaR_entry *entries);

#endif
