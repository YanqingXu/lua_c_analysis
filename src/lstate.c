/*
** Lua状态管理模块
** 负责Lua虚拟机状态的创建、初始化、销毁等核心功能
** 包含全局状态和线程状态的管理
*/

#include <stddef.h>

#define lstate_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"


/* 计算状态结构体的实际大小，包含额外空间 */
#define state_size(x) (sizeof(x) + LUAI_EXTRASPACE)
/* 从状态指针获取原始内存指针 */
#define fromstate(l) (cast(lu_byte *, (l)) - LUAI_EXTRASPACE)
/* 从原始内存指针获取状态指针 */
#define tostate(l) (cast(lua_State *, cast(lu_byte *, l) + LUAI_EXTRASPACE))


/*
** 主线程结构体，包含线程状态和全局状态
** 用于将线程状态和全局状态组合在一起
*/
typedef struct LG
{
    lua_State l;        /* 线程状态 */
    global_State g;     /* 全局状态 */
} LG;


/*
** 初始化Lua状态的栈结构
** L1: 要初始化的状态
** L: 用于内存分配的状态
*/
static void stack_init(lua_State *L1, lua_State *L)
{
    /* 分配并初始化调用信息数组 */
    L1->base_ci = luaM_newvector(L, BASIC_CI_SIZE, CallInfo);
    L1->ci = L1->base_ci;                    /* 当前调用信息指针 */
    L1->size_ci = BASIC_CI_SIZE;             /* 调用信息数组大小 */
    L1->end_ci = L1->base_ci + L1->size_ci - 1;  /* 调用信息数组末尾 */

    /* 分配并初始化栈数组 */
    L1->stack = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, TValue);
    L1->stacksize = BASIC_STACK_SIZE + EXTRA_STACK;  /* 栈总大小 */
    L1->top = L1->stack;                     /* 栈顶指针 */
    L1->stack_last = L1->stack + (L1->stacksize - EXTRA_STACK) - 1;  /* 栈的有效末尾 */

    /* 初始化第一个调用信息 */
    L1->ci->func = L1->top;                  /* 设置函数入口 */
    setnilvalue(L1->top++);                  /* 在栈顶放置nil值 */
    L1->base = L1->ci->base = L1->top;       /* 设置基址指针 */
    L1->ci->top = L1->top + LUA_MINSTACK;    /* 设置调用信息的栈顶 */
}


/*
** 释放Lua状态的栈内存
** L: 用于内存释放的状态
** L1: 要释放栈的状态
*/
static void freestack(lua_State *L, lua_State *L1)
{
    /* 释放调用信息数组 */
    luaM_freearray(L, L1->base_ci, L1->size_ci, CallInfo);
    /* 释放栈数组 */
    luaM_freearray(L, L1->stack, L1->stacksize, TValue);
}


/*
** 打开可能导致内存分配错误的部分
** 在保护模式下调用，用于初始化Lua状态的各个组件
*/
static void f_luaopen(lua_State *L, void *ud)
{
    global_State *g = G(L);  /* 获取全局状态 */
    UNUSED(ud);              /* 标记未使用的参数 */

    /* 初始化栈结构 */
    stack_init(L, L);
    
    /* 创建全局变量表，初始大小为2 */
    sethvalue(L, gt(L), luaH_new(L, 0, 2));
    
    /* 创建注册表，初始大小为2 */
    sethvalue(L, registry(L), luaH_new(L, 0, 2));
    
    /* 设置字符串表的初始大小 */
    luaS_resize(L, MINSTRTABSIZE);
    
    /* 初始化标签方法系统 */
    luaT_init(L);
    
    /* 初始化词法分析器 */
    luaX_init(L);
    
    /* 固定内存错误消息字符串，防止被垃圾回收 */
    luaS_fix(luaS_newliteral(L, MEMERRMSG));
    
    /* 设置垃圾回收阈值为当前内存使用量的4倍 */
    g->GCthreshold = 4 * g->totalbytes;
}


/*
** 预初始化Lua状态
** 设置状态的基本字段为默认值
*/
static void preinit_state(lua_State *L, global_State *g)
{
    /* 设置全局状态指针 */
    G(L) = g;
    
    /* 初始化栈相关字段 */
    L->stack = NULL;
    L->stacksize = 0;
    
    /* 初始化错误处理相关字段 */
    L->errorJmp = NULL;
    L->errfunc = 0;
    
    /* 初始化钩子相关字段 */
    L->hook = NULL;
    L->hookmask = 0;
    L->basehookcount = 0;
    L->allowhook = 1;
    resethookcount(L);
    
    /* 初始化上值和调用信息相关字段 */
    L->openupval = NULL;
    L->size_ci = 0;
    L->base_ci = L->ci = NULL;
    
    /* 初始化C调用计数和状态 */
    L->nCcalls = L->baseCcalls = 0;
    L->status = 0;
    
    /* 初始化程序计数器 */
    L->savedpc = NULL;
    
    /* 初始化全局变量表为nil */
    setnilvalue(gt(L));
}


/*
** 关闭Lua状态
** 释放所有相关资源
*/
static void close_state(lua_State *L)
{
    global_State *g = G(L);  /* 获取全局状态 */

    /* 关闭所有打开的上值 */
    luaF_close(L, L->stack);
    
    /* 回收所有垃圾回收对象 */
    luaC_freeall(L);
    
    /* 确保只剩下主线程对象 */
    lua_assert(g->rootgc == obj2gco(L));
    /* 确保字符串表已清空 */
    lua_assert(g->strt.nuse == 0);
    
    /* 释放字符串表的哈希数组 */
    luaM_freearray(L, G(L)->strt.hash, G(L)->strt.size, TString *);
    
    /* 释放全局缓冲区 */
    luaZ_freebuffer(L, &g->buff);
    
    /* 释放栈内存 */
    freestack(L, L);
    
    /* 确保所有内存都已释放，只剩下LG结构体本身 */
    lua_assert(g->totalbytes == sizeof(LG));
    
    /* 释放状态本身的内存 */
    (*g->frealloc)(g->ud, fromstate(L), state_size(LG), 0);
}


/*
** 创建新的Lua线程
** 返回新创建的线程状态
*/
lua_State *luaE_newthread(lua_State *L)
{
    /* 分配新线程的内存 */
    lua_State *L1 = tostate(luaM_malloc(L, state_size(lua_State)));

    /* 将新线程链接到垃圾回收器 */
    luaC_link(L, obj2gco(L1), LUA_TTHREAD);
    
    /* 预初始化新线程状态，共享全局状态 */
    preinit_state(L1, G(L));
    
    /* 初始化新线程的栈 */
    stack_init(L1, L);
    
    /* 新线程共享主线程的全局变量表 */
    setobj2n(L, gt(L1), gt(L));
    
    /* 复制钩子设置到新线程 */
    L1->hookmask = L->hookmask;
    L1->basehookcount = L->basehookcount;
    L1->hook = L->hook;
    resethookcount(L1);
    
    /* 确保新线程被标记为白色（可回收） */
    lua_assert(iswhite(obj2gco(L1)));

    return L1;
}


/*
** 释放Lua线程
** 清理线程相关的所有资源
*/
void luaE_freethread(lua_State *L, lua_State *L1)
{
    /* 关闭线程的所有上值 */
    luaF_close(L1, L1->stack);
    /* 确保所有上值都已关闭 */
    lua_assert(L1->openupval == NULL);
    
    /* 调用用户状态释放函数 */
    luai_userstatefree(L1);
    
    /* 释放线程的栈内存 */
    freestack(L, L1);
    
    /* 释放线程本身的内存 */
    luaM_freemem(L, fromstate(L1), state_size(lua_State));
}


/*
** 创建新的Lua状态
** f: 内存分配函数
** ud: 用户数据
** 返回新创建的Lua状态，失败时返回NULL
*/
LUA_API lua_State *lua_newstate(lua_Alloc f, void *ud)
{
    int i;
    lua_State *L;
    global_State *g;
    
    /* 分配主线程和全局状态的内存 */
    void *l = (*f)(ud, NULL, 0, state_size(LG));

    /* 内存分配失败 */
    if (l == NULL)
    {
        return NULL;
    }

    /* 初始化状态指针 */
    L = tostate(l);
    g = &((LG *)L)->g;
    
    /* 初始化线程链表 */
    L->next = NULL;
    L->tt = LUA_TTHREAD;
    
    /* 初始化垃圾回收相关字段 */
    g->currentwhite = bit2mask(WHITE0BIT, FIXEDBIT);
    L->marked = luaC_white(g);
    set2bits(L->marked, FIXEDBIT, SFIXEDBIT);
    
    /* 预初始化状态 */
    preinit_state(L, g);
    
    /* 设置内存分配器 */
    g->frealloc = f;
    g->ud = ud;
    
    /* 设置主线程 */
    g->mainthread = L;
    
    /* 初始化上值链表 */
    g->uvhead.u.l.prev = &g->uvhead;
    g->uvhead.u.l.next = &g->uvhead;
    
    /* 初始化垃圾回收器状态 */
    g->GCthreshold = 0;  /* 标记为未完成状态 */
    g->gcstate = GCSpause;
    g->rootgc = obj2gco(L);
    g->sweepstrgc = 0;
    g->sweepgc = &g->rootgc;
    g->gray = NULL;
    g->grayagain = NULL;
    g->weak = NULL;
    g->tmudata = NULL;
    
    /* 初始化字符串表 */
    g->strt.size = 0;
    g->strt.nuse = 0;
    g->strt.hash = NULL;
    
    /* 初始化注册表为nil */
    setnilvalue(registry(L));
    
    /* 初始化全局缓冲区 */
    luaZ_initbuffer(L, &g->buff);
    
    /* 初始化错误处理 */
    g->panic = NULL;
    
    /* 初始化内存统计和垃圾回收参数 */
    g->totalbytes = sizeof(LG);
    g->gcpause = LUAI_GCPAUSE;
    g->gcstepmul = LUAI_GCMUL;
    g->gcdept = 0;

    /* 初始化所有基本类型的元表为NULL */
    for (i = 0; i < NUM_TAGS; i++)
    {
        g->mt[i] = NULL;
    }

    /* 在保护模式下初始化Lua状态 */
    if (luaD_rawrunprotected(L, f_luaopen, NULL) != 0)
    {
        /* 内存分配错误：释放部分状态 */
        close_state(L);
        L = NULL;
    }
    else
    {
        /* 调用用户状态打开函数 */
        luai_userstateopen(L);
    }

    return L;
}


/*
** 调用所有垃圾回收元方法
** 在保护模式下调用，用于处理用户数据的垃圾回收
*/
static void callallgcTM(lua_State *L, void *ud)
{
    UNUSED(ud);  /* 标记未使用的参数 */
    
    /* 为所有用户数据调用垃圾回收元方法 */
    luaC_callGCTM(L);
}


/*
** 关闭Lua状态
** 清理所有资源并释放内存
*/
LUA_API void lua_close(lua_State *L)
{
    /* 只有主线程可以被关闭 */
    L = G(L)->mainthread;
    lua_lock(L);
    
    /* 关闭所有打开的上值 */
    luaF_close(L, L->stack);
    
    /* 分离有垃圾回收元方法的用户数据 */
    luaC_separateudata(L, 1);
    
    /* 垃圾回收元方法期间不使用错误函数 */
    L->errfunc = 0;

    /* 重复调用垃圾回收元方法直到没有更多错误 */
    do
    {
        /* 重置调用信息和栈状态 */
        L->ci = L->base_ci;
        L->base = L->top = L->ci->base;
        L->nCcalls = L->baseCcalls = 0;
        
        /* 在保护模式下调用所有垃圾回收元方法 */
    } while (luaD_rawrunprotected(L, callallgcTM, NULL) != 0);

    /* 确保所有用户数据的元方法都已处理 */
    lua_assert(G(L)->tmudata == NULL);
    
    /* 调用用户状态关闭函数 */
    luai_userstateclose(L);
    
    /* 最终关闭状态 */
    close_state(L);
}

