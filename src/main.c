#include "defines.h"
#include "vscdbg.h"

static void init_debugger(lua_State *L, const char *curpath) {
    vscdbg_t *dbg = vscdbg_new(L, curpath);
    vscdbg_attach_state(L, dbg);
    vscdbg_new_thread(L, L);
}

static void start_debugger(lua_State *L) {
    vscdbg_t *dbg = vscdbg_get_from_state(L);
    vscdbg_handle_request(dbg, L);
}

static void free_debugger(lua_State *L) {
    vscdbg_free(vscdbg_get_from_state(L));
}

//-------------------------------------------------------------
// lua 自定义函数
void on_userstateopen(lua_State *L) {
}

void on_userstateclose(lua_State *L) {
}

void on_userstatethread(lua_State *L, lua_State *L1) {
    vscdbg_new_thread(L, L1);
}

void on_userstatefree(lua_State *L, lua_State *L1) {
    vscdbg_free_thread(L, L1);
}

void do_writestring(lua_State *L, const void *ptr, size_t sz) {
    if (!L) return;
    vscdbg_t* dbg = vscdbg_get_from_state(L);
    if (dbg) {
        if (L == dbg->dL) {
            // 调试器的Lua状态机，由于stdout被重定向到VSCode去了，所以只能通过stderr输出
            fwrite(ptr, sizeof(char), sz, stderr);
        } else {
            // 交给调试器的Lua处理
            lua_Debug ar;
            if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar)) {
                vscdbg_on_output(dbg, ptr, sz, ar.source, ar.currentline);
            } else {
                vscdbg_on_output(dbg, ptr, sz, NULL, -1);
            }
        }
    }
}

void do_writeline(lua_State *L) {
    if (!L) return;
    do_writestring(L, "\n", 1);
}

//-------------------------------------------------------------

int main(int argc, char const *argv[]) {
    const char *curpath = argv[0];
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    init_debugger(L, curpath);
    start_debugger(L);

    free_debugger(L);
    lua_close(L);
    return 0;  
}
