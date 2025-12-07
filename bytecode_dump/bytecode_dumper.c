// Bytecode dumper for Lua 5.1.5 (lua_c_analysis)
//
// Usage:
//   bytecode_dumper.exe <script.lua> [full]
// - Compile Lua source with Lua C API (luaL_loadfile)
// - Print bytecode using luaU_print() (format similar to luac -l)
// - Recursively prints all nested functions

#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lstate.h"   /* access lua_State internals */
#include "lobject.h"  /* clvalue macro, Proto struct */

/*
 * Define luac_c so that lundump.h exposes luaU_print:
 *   #ifdef luac_c
 *     LUAI_FUNC void luaU_print(const Proto* f, int full);
 *   #endif
 */
#define luac_c
#include "lundump.h"  /* luaU_print */

static void dump_proto(const Proto *f, int full)
{
    luaU_print(f, full);
}

int main(int argc, char **argv)
{
     if (argc < 2) {
         fprintf(stderr, "Usage: %s <script.lua> [full]\n", argv[0]);
         return 1;
     }

    const char *filename = argv[1];
    // const char *filename = (argc >= 2) ? argv[1] : "E:\\Programming2\\lua_in_cpp\\lua_c_analysis\\test_factorial.lua";
    int full = (argc > 2) ? 1 : 0;  /* non-zero means full output */

    lua_State *L = lua_open();  /* create Lua state */
    if (L == NULL) {
        fprintf(stderr, "Error: cannot create Lua state.\n");
        return 2;
    }

    luaL_openlibs(L);  /* open standard libraries */

    /* compile Lua source file but do not execute it */
    if (luaL_loadfile(L, filename) != 0) {
        fprintf(stderr, "Error loading %s: %s\n", filename, lua_tostring(L, -1));
        lua_close(L);
        return 3;
    }

    /* stack top now holds a Lua closure; get its Proto */
    const Proto *f = clvalue(L->top - 1)->l.p;
    if (f == NULL) {
        fprintf(stderr, "Error: failed to get Proto from compiled chunk.\n");
        lua_close(L);
        return 4;
    }

    /* print bytecode (recursively including all sub-functions) */
    dump_proto(f, full);

    lua_close(L);
    return 0;
}
