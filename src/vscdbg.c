#include "vscdbg.h"
#include "dbgaux.h"
#include "lstate.h"

// 高度器脚本
static const char *LUA_DEBUGGER = "debugger.lua";
// 全局函数
static const char *ON_NEW_THREAD = "on_new_thread";
static const char *ON_FREE_THREAD = "on_free_thread";
static const char *ON_CALL = "on_call";
static const char *ON_RETURN = "on_return";
static const char *ON_LINE = "on_line";
static const char *HANDLE_REQUEST = "handle_request";
static const char *ON_OUTPUT = "on_output";

// 检查调用，返LUA_OK表示成功，其他表示失败，错误对象在栈顶
static void check_call(lua_State *L, int error, const char *func) {
    if (error) {
        fprintf(stderr, "call %s failed: %s\n", func, lua_tostring(L, -1));
    }
}

// 运行调试器的Lua脚本
static void vscdbg_run_luadebbuer(vscdbg_t *dbg) {
    int err = luaL_dofile(dbg->dL, LUA_DEBUGGER);
    if (err) {
        fprintf(stderr, "%s\n", lua_tostring(dbg->dL, -1));
    }
}

// 计算调用层级
static int get_call_level(lua_State *L) {
    int level = 0;
    CallInfo *ci = &L->base_ci;
    for (; ci && ci != L->ci; ci = ci->next) {
        level++;
    }
    return level;
}

static void on_new_thread(vscdbg_t *dbg, lua_State *L) {
    if (lua_getglobal(dbg->dL, ON_NEW_THREAD) == LUA_TFUNCTION) {
        lua_pushlightuserdata(dbg->dL, L);
        check_call(dbg->dL, lua_pcall(dbg->dL, 1, 0, 0), ON_NEW_THREAD);
    } else {
        fprintf(stderr, "%s must be a function\n", ON_NEW_THREAD);
    }
}

static void on_free_thread(vscdbg_t *dbg, lua_State *L) {
    if (lua_getglobal(dbg->dL, ON_FREE_THREAD) == LUA_TFUNCTION) {
        lua_pushlightuserdata(dbg->dL, L);
        check_call(dbg->dL, lua_pcall(dbg->dL, 1, 0, 0), ON_FREE_THREAD);
    } else {
        fprintf(stderr, "%s must be a function\n", ON_FREE_THREAD);
    }
}

static void on_call(vscdbg_t *dbg, lua_State *L, lua_Debug *ar) {
    if (lua_getglobal(dbg->dL, ON_CALL) == LUA_TFUNCTION) {
        lua_getinfo(L, "nSl", ar);
        lua_pushlightuserdata(dbg->dL, L);
        lua_pushstring(dbg->dL, ar->source);
        lua_pushstring(dbg->dL, ar->what);
        lua_pushstring(dbg->dL, ar->name);
        lua_pushinteger(dbg->dL, ar->currentline);
        lua_pushinteger(dbg->dL, get_call_level(L));
        check_call(dbg->dL, lua_pcall(dbg->dL, 6, 0, 0), ON_CALL);
    } else {
        fprintf(stderr, "%s must be a function\n", ON_CALL);
    }
}

static void on_return(vscdbg_t *dbg, lua_State *L, lua_Debug *ar) {
    if (lua_getglobal(dbg->dL, ON_RETURN) == LUA_TFUNCTION) {
        lua_getinfo(L, "nSl", ar);
        lua_pushlightuserdata(dbg->dL, L);
        lua_pushstring(dbg->dL, ar->source);
        lua_pushstring(dbg->dL, ar->what);
        lua_pushstring(dbg->dL, ar->name);
        lua_pushinteger(dbg->dL, ar->currentline);
        lua_pushinteger(dbg->dL, get_call_level(L));
        check_call(dbg->dL, lua_pcall(dbg->dL, 6, 0, 0), ON_RETURN);
    } else {
        fprintf(stderr, "%s must be a function\n", ON_RETURN);
    }
}

static void on_line(vscdbg_t *dbg, lua_State *L, lua_Debug *ar) {
    if (lua_getglobal(dbg->dL, ON_LINE) == LUA_TFUNCTION) {
        lua_getinfo(L, "nSl", ar);
        lua_pushlightuserdata(dbg->dL, L);
        lua_pushstring(dbg->dL, ar->source);
        lua_pushstring(dbg->dL, ar->what);
        lua_pushstring(dbg->dL, ar->name);
        lua_pushinteger(dbg->dL, ar->currentline);
        check_call(dbg->dL, lua_pcall(dbg->dL, 5, 0, 0), ON_LINE);
    } else {
        fprintf(stderr, "%s must be a function\n", ON_LINE);
    }
}

static void dbg_hook(lua_State *L, lua_Debug *ar) {
    vscdbg_t *dbg = vscdbg_get_from_state(L);
    if (dbg) {
        if (ar->event == LUA_HOOKCALL || ar->event == LUA_HOOKTAILCALL) {
            on_call(dbg, L, ar);
        } else if (ar->event == LUA_HOOKLINE) {
            on_line(dbg, L, ar);
        } else if (ar->event == LUA_HOOKRET) {
            on_return(dbg, L, ar);
        }
    }
}

// 将调试器挂到一个状态机上
void vscdbg_attach_state(lua_State *L, vscdbg_t *dbg) {
    memcpy(lua_getextraspace(L), &dbg, sizeof(void*));
}

// 从状态机取调试器
vscdbg_t* vscdbg_get_from_state(lua_State *L) {
    return *((vscdbg_t**)lua_getextraspace(L));
}

// 开始Hook一个线程
void vscdbg_new_thread(lua_State *L, lua_State *L1) {
    vscdbg_t *dbg = vscdbg_get_from_state(L);
    if (dbg) {
        // 表示主线程，需要设置Hook函数
        if (L == L1) lua_sethook(L, dbg_hook, LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 0);
        on_new_thread(dbg, L1);
    }
}

// 结束Hook一个线程
void vscdbg_free_thread(lua_State *L, lua_State *L1) {
    vscdbg_t *dbg = vscdbg_get_from_state(L);
    if (dbg) on_free_thread(dbg, L1);
}

// 处理客户端请求
void vscdbg_handle_request(vscdbg_t *dbg, lua_State *L) {
    if (lua_getglobal(dbg->dL, HANDLE_REQUEST) == LUA_TFUNCTION) {
        // lua_pushlightuserdata(dbg->dL, L);
        check_call(dbg->dL, lua_pcall(dbg->dL, 0, 0, 0), HANDLE_REQUEST);
    } else {
        fprintf(stderr, "%s must be a function\n", HANDLE_REQUEST);
    }
}

// 输出日志
void vscdbg_on_output(vscdbg_t *dbg, const char *str, size_t sz, const char *source, int line) {
    if (lua_getglobal(dbg->dL, ON_OUTPUT) == LUA_TFUNCTION) {
        lua_pushlstring(dbg->dL, str, sz);
        lua_pushstring(dbg->dL, source);
        lua_pushinteger(dbg->dL, line);
        check_call(dbg->dL, lua_pcall(dbg->dL, 3, 0, 0), ON_OUTPUT);
    } else {
        fprintf(stderr, "%s must be a function\n", ON_OUTPUT);
    }
}

// 打开我自己的库
void open_mylibs(lua_State *dL) {
    static const luaL_Reg mylibs[] = {
        {"dbgaux", luaopen_dbgaux},
        {NULL, NULL}
    };
    const luaL_Reg *lib;
    for (lib = mylibs; lib->func; lib++) {
        luaL_requiref(dL, lib->name, lib->func, 1);
        lua_pop(dL, 1);  /* remove lib */
    }
}

// 新建DBG
vscdbg_t* vscdbg_new(lua_State *L) {
    vscdbg_t *dbg = malloc(sizeof(vscdbg_t));
    memset(dbg, 0, sizeof(vscdbg_t));

    dbg->L = L;
    dbg->dL = luaL_newstate();
    luaL_openlibs(dbg->dL);
    open_mylibs(dbg->dL);
    
    vscdbg_attach_state(dbg->dL, dbg);
    vscdbg_run_luadebbuer(dbg);
    return dbg;
}

// 释放DBG
void* vscdbg_free(vscdbg_t *dbg) {
    lua_close(dbg->dL);
    free(dbg);
    return NULL;
}
