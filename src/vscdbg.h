#ifndef __VSCDBG_H__
#define __VSCDBG_H__
#include "defines.h"

typedef struct vscdbg {
    lua_State *dL;          // 调试器虚拟机
    lua_State *L;           // 被调试的虚拟机
    char curpath[512];          // 当前路径
} vscdbg_t;

vscdbg_t* vscdbg_new(lua_State *L, const char *curpath);
void* vscdbg_free(vscdbg_t *vscdbg);

void vscdbg_attach_state(lua_State *L, vscdbg_t *dbg);
vscdbg_t* vscdbg_get_from_state(lua_State *L);

void vscdbg_new_thread(lua_State *L, lua_State *L1);
void vscdbg_free_thread(lua_State *L, lua_State *L1);

void vscdbg_handle_request(vscdbg_t *dbg, lua_State *L);
void vscdbg_on_output(vscdbg_t *dbg, const char *str, size_t sz, const char *source, int line);
void vscdbg_debuglog(vscdbg_t *dbg, const char *fmt, ...);

#endif  // __VSCDBG_H__