/**
 * 提供给调试器脚本的辅助函数库
 */
#include "dbgaux.h"
#include "vscdbg.h"
#include "lstate.h"
#include "lobject.h"

// 增加path, cpath
// (path, cpath) => void
static int addpath (lua_State *dL) {
    luaL_checktype(dL, 1, LUA_TLIGHTUSERDATA);
    lua_State *L = lua_touserdata(dL, 1);

    size_t pathsz, cpathsz;
    const char *path = luaL_checklstring(dL, 2, &pathsz);
    const char *cpath = luaL_checklstring(dL, 3, &cpathsz);

    luaL_checkstack(L, 10, NULL);
    lua_getglobal(L, "package");    // <pkg>
    lua_getfield(L, -1, "path");    // <pkg|path>
    size_t opathsz, ocpathsz;
    const char *opath = lua_tolstring(L, -1, &opathsz);
    lua_getfield(L, -2, "cpath");   // <pkg|path|cpath>
    const char *ocpath = lua_tolstring(L, -1, &ocpathsz);
    lua_pop(L, 2);      // <pkg>

    if (!strstr(opath, path)) {
        luaL_Buffer b;
        luaL_buffinit(L, &b);
            luaL_addlstring(&b, opath, opathsz);
            luaL_addstring(&b, ";");
            luaL_addlstring(&b, path, pathsz);
        luaL_pushresult(&b);            // <pkg|path>
        lua_setfield(L, -2, "path");    // <pkg>
    }

    if (!strstr(ocpath, cpath)) {
        luaL_Buffer b;
        luaL_buffinit(L, &b);
            luaL_addlstring(&b, ocpath, ocpathsz);
            luaL_addstring(&b, ";");
            luaL_addlstring(&b, cpath, cpathsz);
        luaL_pushresult(&b);            // <pkg|cpath>
        lua_setfield(L, -2, "cpath");   // <pkg>
    }

    lua_pop(L, 1);  // <>
    return 0;
}

// 运行脚本
// (fn, args) => succ
static int runscript(lua_State *dL) {
    vscdbg_t *dbg = vscdbg_get_from_state(dL);
    const char *fn = luaL_checkstring(dL, 1);
    luaL_checktype(dL, 2, LUA_TTABLE);
    lua_State *L = dbg->L;

    // 加载脚本
    int err = luaL_loadfile(L, fn);     // <f>
    if (err) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_pushboolean(dL, 0);
        lua_pushstring(dL, lua_tostring(L, -1));
        lua_pop(L, 1);  // <>
        return 2;
    }

    // 加入参数
    int narg = luaL_len(dL, 2);
    int i;
    for (i = 1; i <= narg; ++i) {
        lua_geti(dL, 2, i);
        lua_pushstring(L, lua_tostring(dL, -1));
        lua_pop(dL, 1);
    }

    // 调用  <f|a1|a2..>
    err = lua_pcall(L, narg, LUA_MULTRET, 0);
    if (err) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_pushboolean(dL, 0);
        lua_pushstring(dL, lua_tostring(L, -1));
        return 2;
    }

    lua_pushboolean(dL, 1);
    return 1;
}

// 取栈帧信息
// (lua_State) => frames
static int getstackframes(lua_State *dL) {
    luaL_checktype(dL, 1, LUA_TLIGHTUSERDATA);
    lua_State *L = lua_touserdata(dL, 1);

    lua_newtable(dL);    // [t]
    int level = 0;
    lua_Debug ar;
    while (level < 99 && lua_getstack(L, level, &ar)) {
        lua_getinfo(L, "Slnt", &ar);
        lua_newtable(dL);  // [t|t2]
        // id
        lua_pushinteger(dL, level); // [t|t2|level]
        lua_setfield(dL, -2, "id");  // [t|t2]
        // name
        bool ismain = strcmp(ar.what, "main") == 0;
        bool islua = strcmp(ar.what, "Lua") == 0;
        if (ar.name)
            lua_pushstring(dL, ar.name);
        else if (ismain)
            lua_pushstring(dL, "main chunk");
        else
            lua_pushstring(dL, "?");
        lua_setfield(dL, -2, "name");  // [t|t2]
        // source
        if (islua || ismain) {
            if (ar.source[0] == '@') {
                lua_pushstring(dL, ar.source+1);
                lua_setfield(dL, -2, "source");  // [t|t2]
            }
        }
        // line
        if (ar.currentline > 0) {
            lua_pushinteger(dL, ar.currentline);
            lua_setfield(dL, -2, "line");  // [t|t2]
        }
        
        level++;
        lua_seti(dL, -2, level);        // [t]
    }
    return 1;
}

static void decode_varref(lua_Integer ref, int *type, int *level, int *id) {
    *id = ref % 10000000;
    int v = ref / 10000000;
    *type = v / 100;
    *level = v % 100;
}

static void push_value_string(lua_State *dL, lua_State *L, int stkidx) {
    int type = lua_type(L, stkidx);
    switch (type) {
    case LUA_TNIL: {
        lua_pushstring(dL, "nil");
        break;
    }
    case LUA_TBOOLEAN: {
        lua_pushstring(dL, (lua_toboolean(L, stkidx) ? "true" : "false"));
        break;
    }
    case LUA_TLIGHTUSERDATA: {
        lua_pushfstring(dL, "lightuserdata: %p", lua_topointer(L, stkidx));
        break;
    }
    case LUA_TNUMBER: {
        if (lua_isinteger(L, stkidx)) {
            lua_Integer i = lua_tointeger(L, stkidx);
            lua_pushfstring(dL, "%I", i);
        } else {
            lua_pushfstring(dL, "%f", lua_tonumber(L, stkidx));
        }
        break;
    }
    case LUA_TSTRING: {
        size_t len;
        const char *s = lua_tolstring(L, stkidx, &len);
        if (len > 1024) len = 1024;
        lua_pushlstring(dL, s, len);
        break;
    }
    case LUA_TTABLE: {
        lua_pushfstring(dL, "table: %p", lua_topointer(L, stkidx));
        break;
    }
    case LUA_TFUNCTION: {
        lua_pushfstring(dL, "function: %p", lua_topointer(L, stkidx));
        break;
    }
    case LUA_TUSERDATA: {
        lua_pushfstring(dL, "userdata: %p", lua_topointer(L, stkidx));
        break;
    }
    case LUA_TTHREAD: {
        lua_pushfstring(dL, "thread: %p", lua_topointer(L, stkidx));
        break;
    }
    }
}

static void push_value_type(lua_State *dL, lua_State *L, int stkidx) {
    const char *type = lua_typename(L, lua_type(L, stkidx));
    lua_pushstring(dL, type);
}

static int cachekey = 0;
static int idkey = 0;
static void cache_var_value(lua_State *L, int id, int stkidx) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &cachekey);   // <ct>
    if (lua_isnoneornil(L, -1)) {
        lua_newtable(L);  // <nil|ct>
        lua_copy(L, -1, -2);    // <ct|ct>
        lua_rawsetp(L, LUA_REGISTRYINDEX, &cachekey);  // <ct>
    }
    /*  t = {
            id = v,
            v = id,
        }
    */
    lua_pushvalue(L, stkidx);   // <ct|v>
    lua_seti(L, -2, id);    // <ct>
    lua_pushvalue(L, stkidx);   // <ct|v>
    lua_pushinteger(L, id); // <ct|v|id>
    lua_settable(L, -3);   // <ct>
    lua_pop(L, 1);  // <>
}

static bool get_val_from_cache(lua_State *L, int id) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &cachekey);   // <ct>
    if (lua_isnoneornil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_geti(L, -1, id);  // <ct|v>
    if (lua_isnoneornil(L, -1)) {
        lua_pop(L, 2);
        return false;
    } else {
        lua_replace(L, -2); // <v>
        return true;
    }
}

static int get_id_from_cache(lua_State *L, int stkidx) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &cachekey);   // <ct>
    if (lua_isnoneornil(L, -1)) {
        lua_pop(L, 1);  // <>
        return 0;
    }
    lua_pushvalue(L, stkidx);   // <ct|v>
    lua_gettable(L, -2);    // <ct|id>
    int id = 0;
    if (!lua_isnoneornil(L, -1)) {
        id = lua_tointeger(L, -1);
    }
    lua_pop(L, 2);  // <>
    return id;
}

static void clear_var_cache(lua_State *L) {
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &cachekey);
    lua_pushinteger(L, 0);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &idkey);
}

static int gen_next_id(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &idkey); // <id>
    int id = 0;
    if (!lua_isnoneornil(L, -1))
        id = lua_tointeger(L, -1);
    lua_pop(L, 1);  // <>
    id++;
    lua_pushinteger(L, id);     // <id>
    lua_rawsetp(L, LUA_REGISTRYINDEX, &idkey);  // <>
    return id;
}

static int push_varref(lua_State *dL, lua_State *L, int stkidx) {
    if (lua_type(L, stkidx) == LUA_TTABLE) {
        // 先看能不能从缓存中取出ID
        int id = get_id_from_cache(L, stkidx);
        if (!id) {
            // 不行再生成新的ID
            id = gen_next_id(L);
            lua_pushinteger(dL, id);  // [id]
        } else {
            lua_pushinteger(dL, id);
        }
        return id;
    } else {
        lua_pushinteger(dL, 0);  // [id]
        return 0;
    }
}

static int startframe(lua_State *dL) {
    luaL_checktype(dL, 1, LUA_TLIGHTUSERDATA);
    lua_State *L = lua_touserdata(dL, 1);
    clear_var_cache(L);
    return 0;
}

static void add_var_info(lua_State *dL, lua_State *L, const char *name, int idx, int stkidx) {
    lua_newtable(dL);    // [t|t1]
    lua_pushstring(dL, name); // [t|t1|name]
    lua_setfield(dL, -2, "name"); // [t|t1]
    
    push_value_string(dL, L, stkidx);       // [t|t1|value]
    lua_setfield(dL, -2, "value"); // [t|t1]

    push_value_type(dL, L, stkidx); // [t|t1|type]
    lua_setfield(dL, -2, "type"); // [t|t1]

    int id = push_varref(dL, L, stkidx);    // [t|t1|varref]
    lua_setfield(dL, -2, "variablesReference");   // [t|t1]

    lua_seti(dL, -2, idx);        // [t]

    // 缓存对象
    if (id) cache_var_value(L, id, stkidx);
}

static void get_func_params(lua_State *dL, lua_State *L, lua_Debug *ar) {
    int i;
    lua_newtable(dL);    // [t]
    if (isLua(ar->i_ci)) {
        Proto *p = clLvalue(ar->i_ci->func)->p;
        // 固定参数
        int idx = 1;
        for (i = 1; i <= p->numparams; i++) {
            const char *name = lua_getlocal(L, ar, i);
            if (name) {     // <a>
                add_var_info(dL, L, name, idx++, lua_gettop(L));   // [t]
            } else {
                break;
            }
        }
        // 可变参数
        for (i = -1; ;--i) {
            const char *name = lua_getlocal(L, ar, i);
            if (name) {     // <a>
                add_var_info(dL, L, name, idx++, lua_gettop(L));   // [t]
            } else {
                break;
            }
        }
    }
    lua_pushboolean(dL, 1);     // [t|true]
    lua_insert(dL, -2);         // [true|t]
}

static void get_func_locals(lua_State *dL, lua_State *L, lua_Debug *ar) {
    lua_newtable(dL);    // [t]
    if (isLua(ar->i_ci)) {
        Proto *p = clLvalue(ar->i_ci->func)->p;
        int i;
        int idx = 1;
        for (i = p->numparams+1; ; i++) {
            const char *name = lua_getlocal(L, ar, i);
            if (name) {     // <a>
                if (strcmp(name , "(*temporary)"))
                    add_var_info(dL, L, name, idx++, lua_gettop(L));   // [t]
                lua_pop(L, 1);  // <>
            } else {
                break;
            }
        }
    }
    lua_pushboolean(dL, 1);     // [t|true]
    lua_insert(dL, -2);         // [true|t]
}

static void get_func_upvalue(lua_State *dL, lua_State *L, lua_Debug *ar) {
    lua_newtable(dL);    // [t]
    if (isLua(ar->i_ci)) {
        lua_getinfo(L, "f", ar);   // <f>
        int idx = 1;
        int i;
        for (i = 1; ; ++i) {
            const char *name = lua_getupvalue(L, -1, i);
            if (name) { // <f|uv>
                add_var_info(dL, L, name, idx++, lua_gettop(L));
                lua_pop(L, 1);  // <f>
            } else {
                break;
            }
        }
        lua_pop(L, 1);  // <>
    }
    lua_pushboolean(dL, 1);     // [t|true]
    lua_insert(dL, -2);         // [true|t]
}

static void get_table_fields(lua_State *dL, lua_State *L, int id) {
    if (!get_val_from_cache(L, id)) {
        lua_pushboolean(dL, 0);
        lua_pushstring(dL, "variable invalid");
        return;
    } // <t>

    lua_pushnil(dL);     // [nil]
    lua_newtable(dL);    // [nil|t]

    int idx = 1;
    lua_pushnil(L); // <t|nil>
    while (idx < 100 && lua_next(L, -2)) {  // <t|k|v>
        push_value_string(dL, L, -2);  // [nil|t|key]
        lua_replace(dL, -3);    // [key|t]
        add_var_info(dL, L, lua_tostring(dL, -2), idx++, lua_gettop(L));  
        lua_pop(L, 1);  // <t|k>
    }
    lua_pop(L, 1);  // <>

    lua_pushboolean(dL, 1);     // [key|t|true]
    lua_replace(dL, -3);         // [true|t]
}

// 取变量信息
// (lua_State, ref) => ok, vars
static int getvars(lua_State *dL) {
    luaL_checktype(dL, 1, LUA_TLIGHTUSERDATA);
    lua_State *L = lua_touserdata(dL, 1);
    lua_Integer ref = luaL_checkinteger(dL, 2);
    int type, level, id;
    decode_varref(ref, &type, &level, &id); 
    if (id == 0) {
        // 取level层栈帧的变量
        lua_Debug ar;
        if (lua_getstack(L, level, &ar)) {
            if (type == 1) {    // 参数
                get_func_params(dL, L, &ar);
            } else if (type == 2) {
                get_func_locals(dL, L, &ar);
            } else if (type == 3) {
                get_func_upvalue(dL, L, &ar);
            } else {
                lua_pushboolean(dL, 0);
                lua_pushstring(dL, "scope invalid");
            }
        } else {
            lua_pushboolean(dL, 0);
            lua_pushstring(dL, "frameId invalid");
        } 
    } else {
        // 取变量的内部成员
        get_table_fields(dL, L, id);
    }
    return 2;
}

// 停止
static int exitprogram(lua_State *dL) {
    exit(0);
}

static const luaL_Reg lib[] = {
    {"addpath", addpath},
    {"runscript", runscript},
    {"getstackframes", getstackframes},
    {"startframe", startframe},
    {"getvars", getvars},
    {"exit", exitprogram},
    {NULL, NULL},
};

int luaopen_dbgaux(lua_State *dL) {
    luaL_newlibtable(dL, lib);  // [lib]
    luaL_setfuncs(dL, lib, 0);  // [lib]
    return 1;
}