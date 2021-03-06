/**
 * 提供给调试器脚本的辅助函数库
 * by code
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

    lua_getglobal(L, "package");    // <pkg>
    lua_getfield(L, -1, "path");    // <pkg|path>
    const char *opath = lua_tolstring(L, -1, NULL);
    if (!strstr(opath, path)) {
        lua_pushfstring(L, "%s;%s", opath, path); // <pkg|path|npath>
        lua_setfield(L, -3, "path");    // <pkg|path>
    }
    lua_pop(L, 1); // <pkg>

    lua_getfield(L, -1, "cpath");   // <pkg|cpath>
    const char *ocpath = lua_tolstring(L, -1, NULL);
    if (!strstr(ocpath, cpath)) {
        lua_pushfstring(L, "%s;%s", ocpath, cpath); // <pkg|cpath|npath>
        lua_setfield(L, -3, "cpath");    // <pkg|cpath>
    }
    lua_pop(L, 2);  // <>

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
        lua_pushboolean(dL, 0);
        lua_pushstring(dL, luaL_tolstring(L, -1, NULL));
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
    int maxlv = luaL_checkinteger(dL, 2);

    lua_newtable(dL);    // [t]
    int level = 0;
    lua_Debug ar;
    while (level < maxlv && lua_getstack(L, level, &ar)) {
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
        if ((islua || ismain) && ar.source[0] == '@') {
            lua_newtable(dL);  // [t|t2|t3]
            lua_pushstring(dL, ar.source+1);  // [t|t2|t3|path]
            lua_setfield(dL, -2, "path");     // [t|t2|t3]
            lua_setfield(dL, -2, "source");   // [t|t2]
            lua_pushinteger(dL, 1);           // [t|t2|1]
            lua_setfield(dL, -2, "column");   // [t|t2]

        } else {
            lua_newtable(dL);  // [t|t2|t3]
            lua_pushstring(dL, "deemphasize");          // [t|t2|t3|path]
            lua_setfield(dL, -2, "presentationHint");   // [t|t2|t3]
            lua_setfield(dL, -2, "source");             // [t|t2]
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
    size_t len;
    const char *val = luaL_tolstring(L, stkidx, &len);  // <str>
    lua_pushlstring(dL, val, len);  // [val]
    lua_pop(L, 1);  // <>
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

static int clearvarcache(lua_State *dL) {
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
        char varname[20] = {0};
        for (i = -1; ;--i) {
            const char *name = lua_getlocal(L, ar, i);
            if (name) {     // <a>
                sprintf(varname, "vararg%d", -i);
                add_var_info(dL, L, varname, idx++, lua_gettop(L));   // [t]
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

// 表达式求值
// (co, expr, level) => result
static int evalkey = 0;
static const char *LUA_EVAL = "/../injectcode.lua";
static int evaluate(lua_State *dL) {
    luaL_checktype(dL, 1, LUA_TLIGHTUSERDATA);
    lua_State *L = lua_touserdata(dL, 1);
    luaL_checktype(dL, 2, LUA_TSTRING);
    int level = luaL_checkinteger(dL, 3);
    vscdbg_t *dbg = vscdbg_get_from_state(L);

    lua_rawgetp(L, LUA_REGISTRYINDEX, &evalkey);  // <eval>
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);  // <>
        char path[512];
        strcpy(path, dbg->curpath);
        strcat(path, LUA_EVAL);
        int error = luaL_dofile(L, path);
        if (error) {    // <err>
            lua_pushboolean(dL, 0); // [false]
            lua_pushstring(dL, luaL_tolstring(L, -1, NULL));    // [false|estr]
            lua_pop(L, 1);  // <>
            return 2;
        } else {    // <eval>
            lua_pushvalue(L, -1); // <eval|eval>
            lua_rawsetp(L, LUA_REGISTRYINDEX, &evalkey);    // <eval>
        }
    }
    lua_pushvalue(L, -1); // <eval|eval>

    // first, try: "return <expr>"
    lua_pushfstring(L, "return %s", lua_tostring(dL, 2));   // <eval|eval|expr>
    lua_pushthread(L);  // <eval|eval|expr|co>
    lua_pushinteger(L, level); // <eval|eval|expr|co|evel>
    lua_call(L, 3, 2);  // <eval|boolean|res>
    if (lua_toboolean(L, -2)) {
        lua_pushboolean(dL, 1); // [true]
        lua_pushstring(dL, luaL_tolstring(L, -1, NULL));    // [true|res]
        lua_pop(L, 3);  // <>
        return 2;
    } else {
        lua_pop(L, 2);  // <eval>
    }

    // second, try: "expr"
    lua_pushfstring(L, "%s", lua_tostring(dL, 2));   // <eval|expr>
    lua_pushthread(L);  // <eval|expr|co>
    lua_pushinteger(L, level); // <eval|expr|co|evel>
    lua_call(L, 3, 2);  // <eval|boolean|res>
    if (lua_toboolean(L, -2)) {
        lua_pushboolean(dL, 1); // [true]
        lua_pushstring(dL, luaL_tolstring(L, -1, NULL));    // [true|res]
        lua_pop(L, 3);  // <>
        return 2;
    } else {
        lua_pushboolean(dL, 0); // [false]
        lua_pushstring(dL, luaL_tolstring(L, -1, NULL));    // [false|estr]
        lua_pop(L, 3);  // <>
        return 2;
    }
}

static const luaL_Reg lib[] = {
    {"addpath", addpath},
    {"runscript", runscript},
    {"getstackframes", getstackframes},
    {"clearvarcache", clearvarcache},
    {"getvars", getvars},
    {"evaluate", evaluate},
    {NULL, NULL},
};

int luaopen_dbgaux(lua_State *dL) {
    luaL_newlibtable(dL, lib);  // [lib]
    luaL_setfuncs(dL, lib, 0);  // [lib]
    return 1;
}