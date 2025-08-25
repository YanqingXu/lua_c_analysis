/*
** Lua执行和错误处理模块
** 负责Lua的栈管理、函数调用、协程处理和错误恢复
** 包含函数调用机制、栈操作、保护模式执行等核心功能
*/

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define ldo_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"


/*
** 长跳转缓冲区的链表结构
** 用于错误恢复和异常处理
*/
struct lua_longjmp
{
    struct lua_longjmp *previous;  /* 前一个跳转缓冲区 */
    luai_jmpbuf b;                 /* 跳转缓冲区 */
    volatile int status;           /* 错误代码 */
};


/*
** 设置错误对象
** L: Lua状态
** errcode: 错误代码
** oldtop: 原栈顶位置
*/
void luaD_seterrorobj(lua_State *L, int errcode, StkId oldtop)
{
    switch (errcode)
    {
        case LUA_ERRMEM:
        {
            /* 内存错误：设置内存错误消息 */
            setsvalue2s(L, oldtop, luaS_newliteral(L, MEMERRMSG));
            break;
        }
        
        case LUA_ERRERR:
        {
            /* 错误处理中的错误 */
            setsvalue2s(L, oldtop, luaS_newliteral(L, "error in error handling"));
            break;
        }
        
        case LUA_ERRSYNTAX:
        case LUA_ERRRUN:
        {
            /* 语法错误或运行时错误：使用当前栈顶的错误消息 */
            setobjs2s(L, oldtop, L->top - 1);
            break;
        }
    }
    
    /* 设置新的栈顶位置 */
    L->top = oldtop + 1;
}


/*
** 恢复栈限制
** 处理栈溢出后的恢复操作
*/
static void restore_stack_limit(lua_State *L)
{
    /* 确保栈大小一致性 */
    lua_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK - 1);
    
    /* 检查是否发生了调用信息溢出 */
    if (L->size_ci > LUAI_MAXCALLS)
    {
        int inuse = cast_int(L->ci - L->base_ci);  /* 当前使用的调用信息数量 */
        
        /* 如果可以撤销溢出，则重新分配到最大允许大小 */
        if (inuse + 1 < LUAI_MAXCALLS)
        {
            luaD_reallocCI(L, LUAI_MAXCALLS);
        }
    }
}


/*
** 重置栈状态
** 在错误发生后重置栈到安全状态
*/
static void resetstack(lua_State *L, int status)
{
    /* 重置调用信息到基础状态 */
    L->ci = L->base_ci;
    L->base = L->ci->base;
    
    /* 关闭可能挂起的闭包 */
    luaF_close(L, L->base);
    
    /* 设置错误对象 */
    luaD_seterrorobj(L, status, L->base);
    
    /* 重置C调用计数 */
    L->nCcalls = L->baseCcalls;
    
    /* 重新允许钩子 */
    L->allowhook = 1;
    
    /* 恢复栈限制 */
    restore_stack_limit(L);
    
    /* 清除错误处理状态 */
    L->errfunc = 0;
    L->errorJmp = NULL;
}


/*
** 抛出错误
** 使用长跳转机制处理错误
*/
void luaD_throw(lua_State *L, int errcode)
{
    if (L->errorJmp)
    {
        /* 有错误跳转缓冲区：设置状态并跳转 */
        L->errorJmp->status = errcode;
        LUAI_THROW(L, L->errorJmp);
    }
    else
    {
        /* 没有错误处理：设置状态并调用panic函数 */
        L->status = cast_byte(errcode);
        
        if (G(L)->panic)
        {
            /* 重置栈并调用panic函数 */
            resetstack(L, errcode);
            lua_unlock(L);
            G(L)->panic(L);
        }
        
        /* 如果没有panic函数，直接退出 */
        exit(EXIT_FAILURE);
    }
}


/*
** 在保护模式下运行函数
** 设置错误处理并执行函数
*/
int luaD_rawrunprotected(lua_State *L, Pfunc f, void *ud)
{
    struct lua_longjmp lj;
    
    /* 初始化跳转缓冲区 */
    lj.status = 0;
    lj.previous = L->errorJmp;  /* 链接到前一个错误处理器 */
    L->errorJmp = &lj;
    
    /* 在保护模式下执行函数 */
    LUAI_TRY(L, &lj, (*f)(L, ud););
    
    /* 恢复前一个错误处理器 */
    L->errorJmp = lj.previous;
    
    return lj.status;
}


/*
** 修正栈指针
** 在栈重新分配后更新所有相关指针
*/
static void correctstack(lua_State *L, TValue *oldstack)
{
    CallInfo *ci;
    GCObject *up;
    
    /* 更新栈顶指针 */
    L->top = (L->top - oldstack) + L->stack;
    
    /* 更新所有打开的上值指针 */
    for (up = L->openupval; up != NULL; up = up->gch.next)
    {
        gco2uv(up)->v = (gco2uv(up)->v - oldstack) + L->stack;
    }
    
    /* 更新所有调用信息中的指针 */
    for (ci = L->base_ci; ci <= L->ci; ci++)
    {
        ci->top = (ci->top - oldstack) + L->stack;
        ci->base = (ci->base - oldstack) + L->stack;
        ci->func = (ci->func - oldstack) + L->stack;
    }
    
    /* 更新基址指针 */
    L->base = (L->base - oldstack) + L->stack;
}


/*
** 重新分配栈空间
** 调整栈大小并更新相关指针
*/
void luaD_reallocstack(lua_State *L, int newsize)
{
    TValue *oldstack = L->stack;  /* 保存旧栈指针 */
    int realsize = newsize + 1 + EXTRA_STACK;  /* 计算实际大小 */
    
    /* 确保栈大小一致性 */
    lua_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK - 1);
    
    /* 重新分配栈内存 */
    luaM_reallocvector(L, L->stack, L->stacksize, realsize, TValue);
    L->stacksize = realsize;
    L->stack_last = L->stack + newsize;
    
    /* 修正所有栈相关指针 */
    correctstack(L, oldstack);
}


/*
** 重新分配调用信息数组
** 调整调用信息数组大小并更新指针
*/
void luaD_reallocCI(lua_State *L, int newsize)
{
    CallInfo *oldci = L->base_ci;  /* 保存旧调用信息指针 */
    
    /* 重新分配调用信息数组 */
    luaM_reallocvector(L, L->base_ci, L->size_ci, newsize, CallInfo);
    L->size_ci = newsize;
    
    /* 更新调用信息指针 */
    L->ci = (L->ci - oldci) + L->base_ci;
    L->end_ci = L->base_ci + L->size_ci - 1;
}


/*
** 增长栈空间
** 根据需要扩展栈大小
*/
void luaD_growstack(lua_State *L, int n)
{
    if (n <= L->stacksize)
    {
        /* 双倍大小足够：扩展到两倍大小 */
        luaD_reallocstack(L, 2 * L->stacksize);
    }
    else
    {
        /* 需要更大空间：扩展到所需大小 */
        luaD_reallocstack(L, L->stacksize + n);
    }
}


/*
** 增长调用信息数组
** 扩展调用信息数组并检查溢出
*/
static CallInfo *growCI(lua_State *L)
{
    if (L->size_ci > LUAI_MAXCALLS)
    {
        /* 处理溢出时发生溢出：抛出错误 */
        luaD_throw(L, LUA_ERRERR);
    }
    else
    {
        /* 扩展调用信息数组到两倍大小 */
        luaD_reallocCI(L, 2 * L->size_ci);
        
        /* 检查是否超过最大调用数 */
        if (L->size_ci > LUAI_MAXCALLS)
        {
            luaG_runerror(L, "stack overflow");
        }
    }
    
    return ++L->ci;
}


/*
** 调用钩子函数
** 在特定事件发生时调用用户定义的钩子
*/
void luaD_callhook(lua_State *L, int event, int line)
{
    lua_Hook hook = L->hook;  /* 获取钩子函数 */
    
    if (hook && L->allowhook)
    {
        /* 保存当前栈状态 */
        ptrdiff_t top = savestack(L, L->top);
        ptrdiff_t ci_top = savestack(L, L->ci->top);
        lua_Debug ar;
        
        /* 设置调试信息 */
        ar.event = event;
        ar.currentline = line;
        
        if (event == LUA_HOOKTAILRET)
        {
            /* 尾调用：没有调试信息 */
            ar.i_ci = 0;
        }
        else
        {
            /* 设置调用信息索引 */
            ar.i_ci = cast_int(L->ci - L->base_ci);
        }
        
        /* 确保最小栈空间 */
        luaD_checkstack(L, LUA_MINSTACK);
        L->ci->top = L->top + LUA_MINSTACK;
        lua_assert(L->ci->top <= L->stack_last);
        
        /* 禁止在钩子内调用钩子 */
        L->allowhook = 0;
        lua_unlock(L);
        
        /* 调用钩子函数 */
        (*hook)(L, &ar);
        
        lua_lock(L);
        lua_assert(!L->allowhook);
        
        /* 恢复钩子调用权限和栈状态 */
        L->allowhook = 1;
        L->ci->top = restorestack(L, ci_top);
        L->top = restorestack(L, top);
    }
}


/*
** 调整可变参数
** 处理函数的可变参数，创建arg表（兼容模式）
*/
static StkId adjust_varargs(lua_State *L, Proto *p, int actual)
{
    int i;
    int nfixargs = p->numparams;  /* 固定参数数量 */
    Table *htab = NULL;
    StkId base, fixed;
    
    /* 如果实际参数少于固定参数，用nil填充 */
    for (; actual < nfixargs; ++actual)
    {
        setnilvalue(L->top++);
    }
    
#if defined(LUA_COMPAT_VARARG)
    /* 兼容旧式可变参数 */
    if (p->is_vararg & VARARG_NEEDSARG)
    {
        int nvar = actual - nfixargs;  /* 额外参数数量 */
        lua_assert(p->is_vararg & VARARG_HASARG);
        
        /* 检查垃圾回收和栈空间 */
        luaC_checkGC(L);
        luaD_checkstack(L, p->maxstacksize);
        
        /* 创建arg表 */
        htab = luaH_new(L, nvar, 1);
        
        /* 将额外参数放入arg表 */
        for (i = 0; i < nvar; i++)
        {
            setobj2n(L, luaH_setnum(L, htab, i + 1), L->top - nvar + i);
        }
        
        /* 在字段n中存储计数 */
        setnvalue(luaH_setstr(L, htab, luaS_newliteral(L, "n")), cast_num(nvar));
    }
#endif
    
    /* 将固定参数移动到最终位置 */
    fixed = L->top - actual;  /* 第一个固定参数 */
    base = L->top;            /* 第一个参数的最终位置 */
    
    for (i = 0; i < nfixargs; i++)
    {
        setobjs2s(L, L->top++, fixed + i);
        setnilvalue(fixed + i);
    }
    
    /* 添加arg参数 */
    if (htab)
    {
        sethvalue(L, L->top++, htab);
        lua_assert(iswhite(obj2gco(htab)));
    }
    
    return base;
}


/*
** 尝试函数标签方法
** 当对象不是函数时，尝试调用其__call元方法
*/
static StkId tryfuncTM(lua_State *L, StkId func)
{
    const TValue *tm = luaT_gettmbyobj(L, func, TM_CALL);  /* 获取__call元方法 */
    StkId p;
    ptrdiff_t funcr = savestack(L, func);
    
    /* 检查元方法是否为函数 */
    if (!ttisfunction(tm))
    {
        luaG_typeerror(L, func, "call");
    }
    
    /* 在栈中func位置打开一个空洞 */
    for (p = L->top; p > func; p--)
    {
        setobjs2s(L, p, p - 1);
    }
    
    incr_top(L);
    func = restorestack(L, funcr);  /* 前一个调用可能改变栈 */
    
    /* 标签方法成为新的被调用函数 */
    setobj2s(L, func, tm);
    
    return func;
}


/* 增加调用信息的宏定义 */
#define inc_ci(L) \
    ((L->ci == L->end_ci) ? growCI(L) : \
     (condhardstacktests(luaD_reallocCI(L, L->size_ci)), ++L->ci))


/*
** 函数调用前的准备工作
** 设置调用环境并准备参数
*/
int luaD_precall(lua_State *L, StkId func, int nresults)
{
    LClosure *cl;
    ptrdiff_t funcr;
    
    /* 检查func是否为函数 */
    if (!ttisfunction(func))
    {
        /* 不是函数：检查__call标签方法 */
        func = tryfuncTM(L, func);
    }
    
    funcr = savestack(L, func);
    cl = &clvalue(func)->l;
    L->ci->savedpc = L->savedpc;
    
    if (!cl->isC)
    {
        /* Lua函数：准备其调用 */
        CallInfo *ci;
        StkId st, base;
        Proto *p = cl->p;
        
        /* 检查栈空间 */
        luaD_checkstack(L, p->maxstacksize);
        func = restorestack(L, funcr);
        
        if (!p->is_vararg)
        {
            /* 非可变参数函数 */
            base = func + 1;
            
            /* 调整参数数量 */
            if (L->top > base + p->numparams)
            {
                L->top = base + p->numparams;
            }
        }
        else
        {
            /* 可变参数函数 */
            int nargs = cast_int(L->top - func) - 1;
            base = adjust_varargs(L, p, nargs);
            func = restorestack(L, funcr);  /* 前一个调用可能改变栈 */
        }
        
        /* 进入新函数 */
        ci = inc_ci(L);
        ci->func = func;
        L->base = ci->base = base;
        ci->top = L->base + p->maxstacksize;
        lua_assert(ci->top <= L->stack_last);
        
        /* 设置程序计数器和调用信息 */
        L->savedpc = p->code;  /* 起始点 */
        ci->tailcalls = 0;
        ci->nresults = nresults;
        
        /* 初始化局部变量为nil */
        for (st = L->top; st < ci->top; st++)
        {
            setnilvalue(st);
        }
        
        L->top = ci->top;
        
        /* 调用钩子 */
        if (L->hookmask & LUA_MASKCALL)
        {
            L->savedpc++;  /* 钩子假设pc已经递增 */
            luaD_callhook(L, LUA_HOOKCALL, -1);
            L->savedpc--;  /* 修正pc */
        }
        
        return PCRLUA;
    }
    else
    {
        /* C函数：直接调用 */
        CallInfo *ci;
        int n;
        
        /* 确保最小栈空间 */
        luaD_checkstack(L, LUA_MINSTACK);
        
        /* 进入新函数 */
        ci = inc_ci(L);
        ci->func = restorestack(L, funcr);
        L->base = ci->base = ci->func + 1;
        ci->top = L->top + LUA_MINSTACK;
        lua_assert(ci->top <= L->stack_last);
        ci->nresults = nresults;
        
        /* 调用钩子 */
        if (L->hookmask & LUA_MASKCALL)
        {
            luaD_callhook(L, LUA_HOOKCALL, -1);
        }
        
        lua_unlock(L);
        
        /* 执行实际调用 */
        n = (*curr_func(L)->c.f)(L);
        
        lua_lock(L);
        
        if (n < 0)
        {
            /* 让出：返回让出状态 */
            return PCRYIELD;
        }
        else
        {
            /* 正常返回：处理返回值 */
            luaD_poscall(L, L->top - n);
            return PCRC;
        }
    }
}


/*
** 调用返回钩子
** 处理函数返回时的钩子调用
*/
static StkId callrethooks(lua_State *L, StkId firstResult)
{
    ptrdiff_t fr = savestack(L, firstResult);  /* 下一个调用可能改变栈 */
    
    /* 调用返回钩子 */
    luaD_callhook(L, LUA_HOOKRET, -1);
    
    if (f_isLua(L->ci))
    {
        /* Lua函数：处理尾调用 */
        while ((L->hookmask & LUA_MASKRET) && L->ci->tailcalls--)
        {
            luaD_callhook(L, LUA_HOOKTAILRET, -1);
        }
    }
    
    return restorestack(L, fr);
}


/*
** 函数调用后的处理
** 处理返回值并恢复调用状态
*/
int luaD_poscall(lua_State *L, StkId firstResult)
{
    StkId res;
    int wanted, i;
    CallInfo *ci;
    
    /* 调用返回钩子 */
    if (L->hookmask & LUA_MASKRET)
    {
        firstResult = callrethooks(L, firstResult);
    }
    
    /* 获取调用信息并退出当前调用 */
    ci = L->ci--;
    res = ci->func;  /* 第一个结果的最终位置 */
    wanted = ci->nresults;
    
    /* 恢复基址和程序计数器 */
    L->base = (ci - 1)->base;
    L->savedpc = (ci - 1)->savedpc;
    
    /* 将结果移动到正确位置 */
    for (i = wanted; i != 0 && firstResult < L->top; i--)
    {
        setobjs2s(L, res++, firstResult++);
    }
    
    /* 用nil填充剩余位置 */
    while (i-- > 0)
    {
        setnilvalue(res++);
    }
    
    L->top = res;
    
    return (wanted - LUA_MULTRET);  /* 当wanted == LUA_MULTRET时为0 */
}


/*
** 调用函数（C或Lua）
** 被调用的函数在*func位置，参数在栈上紧跟函数之后
** 返回时，所有结果都在栈上，从原函数位置开始
*/
void luaD_call(lua_State *L, StkId func, int nResults)
{
    /* 检查C调用栈溢出 */
    if (++L->nCcalls >= LUAI_MAXCCALLS)
    {
        if (L->nCcalls == LUAI_MAXCCALLS)
        {
            luaG_runerror(L, "C stack overflow");
        }
        else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS >> 3)))
        {
            /* 处理栈错误时发生错误 */
            luaD_throw(L, LUA_ERRERR);
        }
    }
    
    /* 预调用并检查是否为Lua函数 */
    if (luaD_precall(L, func, nResults) == PCRLUA)
    {
        /* 是Lua函数：执行它 */
        luaV_execute(L, 1);
    }
    
    /* 减少C调用计数并检查垃圾回收 */
    L->nCcalls--;
    luaC_checkGC(L);
}


/*
** 恢复协程执行
** 从挂起状态恢复协程的执行
*/
static void resume(lua_State *L, void *ud)
{
    StkId firstArg = cast(StkId, ud);
    CallInfo *ci = L->ci;
    
    if (L->status == 0)
    {
        /* 启动协程 */
        lua_assert(ci == L->base_ci && firstArg > L->base);
        
        if (luaD_precall(L, firstArg - 1, LUA_MULTRET) != PCRLUA)
        {
            return;
        }
    }
    else
    {
        /* 从前一个yield恢复 */
        lua_assert(L->status == LUA_YIELD);
        L->status = 0;
        
        if (!f_isLua(ci))
        {
            /* 普通yield：完成被中断的OP_CALL执行 */
            lua_assert(GET_OPCODE(*((ci - 1)->savedpc - 1)) == OP_CALL ||
                      GET_OPCODE(*((ci - 1)->savedpc - 1)) == OP_TAILCALL);
            
            if (luaD_poscall(L, firstArg))
            {
                /* 完成调用并修正栈顶（如果不是多返回值） */
                L->top = L->ci->top;
            }
        }
        else
        {
            /* 在钩子内yield：继续执行 */
            L->base = L->ci->base;
        }
    }
    
    /* 执行Lua代码 */
    luaV_execute(L, cast_int(L->ci - L->base_ci));
}


/*
** 恢复错误处理
** 处理协程恢复时的错误
*/
static int resume_error(lua_State *L, const char *msg)
{
    /* 设置错误消息 */
    L->top = L->ci->base;
    setsvalue2s(L, L->top, luaS_new(L, msg));
    incr_top(L);
    lua_unlock(L);
    
    return LUA_ERRRUN;
}


/*
** 恢复协程
** 恢复挂起的协程执行
*/
LUA_API int lua_resume(lua_State *L, int nargs)
{
    int status;
    lua_lock(L);
    
    /* 检查协程状态 */
    if (L->status != LUA_YIELD && (L->status != 0 || L->ci != L->base_ci))
    {
        return resume_error(L, "cannot resume non-suspended coroutine");
    }
    
    /* 检查C栈溢出 */
    if (L->nCcalls >= LUAI_MAXCCALLS)
    {
        return resume_error(L, "C stack overflow");
    }
    
    /* 调用用户状态恢复函数 */
    luai_userstateresume(L, nargs);
    lua_assert(L->errfunc == 0);
    
    /* 设置基础C调用计数 */
    L->baseCcalls = ++L->nCcalls;
    
    /* 在保护模式下恢复执行 */
    status = luaD_rawrunprotected(L, resume, L->top - nargs);
    
    if (status != 0)
    {
        /* 发生错误：标记线程为死亡状态 */
        L->status = cast_byte(status);
        luaD_seterrorobj(L, status, L->top);
        L->ci->top = L->top;
    }
    else
    {
        /* 成功：获取状态 */
        lua_assert(L->nCcalls == L->baseCcalls);
        status = L->status;
    }
    
    --L->nCcalls;
    lua_unlock(L);
    
    return status;
}


/*
** 让出协程
** 挂起当前协程的执行
*/
LUA_API int lua_yield(lua_State *L, int nresults)
{
    /* 调用用户状态让出函数 */
    luai_userstateyield(L, nresults);
    lua_lock(L);
    
    /* 检查是否可以让出 */
    if (L->nCcalls > L->baseCcalls)
    {
        luaG_runerror(L, "attempt to yield across metamethod/C-call boundary");
    }
    
    /* 保护栈下方的槽位 */
    L->base = L->top - nresults;
    L->status = LUA_YIELD;
    lua_unlock(L);
    
    return -1;
}


/*
** 保护模式调用
** 在保护模式下执行函数并处理错误
*/
int luaD_pcall(lua_State *L, Pfunc func, void *u, ptrdiff_t old_top, ptrdiff_t ef)
{
    int status;
    unsigned short oldnCcalls = L->nCcalls;
    ptrdiff_t old_ci = saveci(L, L->ci);
    lu_byte old_allowhooks = L->allowhook;
    ptrdiff_t old_errfunc = L->errfunc;
    
    /* 设置错误函数 */
    L->errfunc = ef;
    
    /* 在保护模式下执行函数 */
    status = luaD_rawrunprotected(L, func, u);
    
    if (status != 0)
    {
        /* 发生错误：恢复状态 */
        StkId oldtop = restorestack(L, old_top);
        
        /* 关闭可能挂起的闭包 */
        luaF_close(L, oldtop);
        
        /* 设置错误对象 */
        luaD_seterrorobj(L, status, oldtop);
        
        /* 恢复调用状态 */
        L->nCcalls = oldnCcalls;
        L->ci = restoreci(L, old_ci);
        L->base = L->ci->base;
        L->savedpc = L->ci->savedpc;
        L->allowhook = old_allowhooks;
        
        /* 恢复栈限制 */
        restore_stack_limit(L);
    }
    
    /* 恢复错误函数 */
    L->errfunc = old_errfunc;
    
    return status;
}


/*
** 解析器数据结构
** 传递给f_parser的数据
*/
struct SParser
{
    ZIO *z;              /* 输入流 */
    Mbuffer buff;        /* 扫描器使用的缓冲区 */
    const char *name;    /* 源文件名 */
};


/*
** 解析器函数
** 在保护模式下执行的解析函数
*/
static void f_parser(lua_State *L, void *ud)
{
    int i;
    Proto *tf;
    Closure *cl;
    struct SParser *p = cast(struct SParser *, ud);
    
    /* 检查输入类型（字节码或源码） */
    int c = luaZ_lookahead(p->z);
    luaC_checkGC(L);
    
    /* 根据输入类型选择解析器 */
    tf = ((c == LUA_SIGNATURE[0]) ? luaU_undump : luaY_parser)(L, p->z, &p->buff, p->name);
    
    /* 创建闭包 */
    cl = luaF_newLclosure(L, tf->nups, hvalue(gt(L)));
    cl->l.p = tf;
    
    /* 初始化上值 */
    for (i = 0; i < tf->nups; i++)
    {
        cl->l.upvals[i] = luaF_newupval(L);
    }
    
    /* 将闭包放到栈顶 */
    setclvalue(L, L->top, cl);
    incr_top(L);
}


/*
** 保护模式解析器
** 在保护模式下执行解析操作
*/
int luaD_protectedparser(lua_State *L, ZIO *z, const char *name)
{
    struct SParser p;
    int status;
    
    /* 初始化解析器数据 */
    p.z = z;
    p.name = name;
    luaZ_initbuffer(L, &p.buff);
    
    /* 在保护模式下执行解析 */
    status = luaD_pcall(L, f_parser, &p, savestack(L, L->top), L->errfunc);
    
    /* 释放缓冲区 */
    luaZ_freebuffer(L, &p.buff);
    
    return status;
}


