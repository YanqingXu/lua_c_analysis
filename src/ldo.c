/*
** [核心] Lua 执行和错误处理模块
**
** 功能概述：
** 负责Lua的栈管理、函数调用、协程处理和错误恢复。
** 包含函数调用机制、栈操作、保护模式执行等核心功能。
**
** 主要组件：
** - 栈管理：动态调整栈大小，处理栈溢出
** - 函数调用：支持Lua函数和C函数的调用机制
** - 协程支持：实现协程的创建、恢复和挂起
** - 错误处理：提供保护模式执行和错误恢复
** - 调试支持：配合调试模块提供钩子机制
**
** 文件标识：ldo.c - Lua执行控制核心模块
** 版权声明：参见 lua.h 中的版权说明
*/

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

// [系统] 模块标识宏定义
#define ldo_c       // 标识当前编译单元为ldo模块
#define LUA_CORE    // 标识这是Lua核心模块的一部分

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
** [数据结构] 长跳转缓冲区链表
**
** 功能说明：
** 用于错误恢复和异常处理的长跳转缓冲区链式结构。
** 支持嵌套的错误处理，通过链表管理多层的跳转点。
**
** 字段说明：
** @field previous - struct lua_longjmp*：指向前一个跳转缓冲区的指针
** @field b - luai_jmpbuf：实际的跳转缓冲区
** @field status - volatile int：错误代码，volatile确保异常安全
*/
struct lua_longjmp
{
    /* 前一个跳转缓冲区 */
    struct lua_longjmp *previous;
    /* 跳转缓冲区 */
    luai_jmpbuf b;
    /* 错误代码 */
    volatile int status;
};


/*
** [进阶] 设置错误对象
**
** 详细功能说明：
** 根据错误代码在栈上设置相应的错误对象。处理不同类型的错误，
** 包括内存错误、错误处理中的错误、语法错误和运行时错误。
**
** 参数说明：
** @param L - lua_State*：Lua状态机
** @param errcode - int：错误代码
** @param oldtop - StkId：原栈顶位置
**
** 错误类型：
** - LUA_ERRMEM：内存分配错误
** - LUA_ERRERR：错误处理器自身发生错误
** - LUA_ERRSYNTAX：语法分析错误
** - LUA_ERRRUN：运行时错误
**
** 算法复杂度：O(1) 时间
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
** [进阶] 恢复栈限制
**
** 详细功能说明：
** 处理栈溢出后的恢复操作，检查调用信息是否溢出，
** 如果可能则将调用信息重新分配到合理大小。
**
** 参数说明：
** @param L - lua_State*：Lua状态机
**
** 处理逻辑：
** 1. 检查栈大小一致性
** 2. 如果调用信息溢出，尝试恢复到最大允许大小
** 3. 释放多余的调用信息内存
**
** 算法复杂度：O(1) 时间，可能触发O(n)的内存重分配
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
** [核心] 重置栈状态
**
** 详细功能说明：
** 在错误发生后重置栈到安全状态，清理所有中间状态。
** 这是错误恢复机制的核心部分，确保虚拟机状态一致性。
**
** 参数说明：
** @param L - lua_State*：Lua状态机
** @param status - int：错误状态码
**
** 重置操作：
** 1. 重置调用信息链到基础状态
** 2. 关闭挂起的闭包和上值
** 3. 设置错误对象到栈上
** 4. 重置C调用计数器
** 5. 重新启用调试钩子
** 6. 恢复栈大小限制
**
** 算法复杂度：O(n) 时间，n为需要关闭的闭包数量
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


/**
 * [专家] 错误抛出机制
 * 使用长跳转机制处理错误，支持异常传播
 * 
 * @param L Lua状态机
 * @param errcode 错误代码
 * 
 * 时间复杂度: O(1) 或 O(程序终止)
 * 空间复杂度: O(1)
 * 
 * 错误处理策略：
 * 1. 有错误跳转缓冲区：设置状态并长跳转
 * 2. 无错误处理：调用panic函数或程序退出
 * 
 * 错误传播：
 * - 有errorJmp: 使用LUAI_THROW跳转到错误处理点
 * - 无errorJmp: 重置栈，调用全局panic函数
 * - 无panic: 直接调用exit()终止程序
 * 
 * 这是Lua错误处理的底层实现
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


/**
 * [专家] 原始保护模式执行
 * 设置错误处理机制并在保护模式下执行函数
 * 
 * @param L Lua状态机
 * @param f 要执行的函数指针
 * @param ud 传递给函数的用户数据
 * @return 执行状态码（0=成功，其他=错误类型）
 * 
 * 时间复杂度: O(函数执行时间)
 * 空间复杂度: O(1)
 * 
 * 保护机制：
 * 1. 创建新的长跳转结构体
 * 2. 链接到错误处理器链表
 * 3. 使用LUAI_TRY宏执行函数
 * 4. 捕获异常并恢复错误处理器链
 * 
 * 错误传播：通过longjmp/setjmp机制实现异常处理
 * 嵌套支持：维护错误处理器栈，支持嵌套保护调用
 */
int luaD_rawrunprotected(lua_State *L, Pfunc f, void *ud)
{
    struct lua_longjmp lj;    // 长跳转缓冲区结构
    
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


/**
 * [专家] 栈重分配后指针校正
 * 在栈重新分配后更新所有相关指针，确保数据一致性
 * 
 * @param L Lua状态机
 * @param oldstack 旧栈的起始地址
 * 
 * 时间复杂度: O(n + m + k) 其中n=打开的上值数，m=调用深度，k=栈深度
 * 空间复杂度: O(1)
 * 
 * 校正范围：
 * 1. 栈顶指针(L->top)
 * 2. 所有打开的上值指针(openupval链表)
 * 3. 所有调用信息中的指针(base_ci到ci)
 * 4. 当前基址指针(L->base)
 * 
 * 指针计算公式: new_ptr = (old_ptr - oldstack) + L->stack
 */
static void correctstack(lua_State *L, TValue *oldstack)
{
    CallInfo *ci;        // 调用信息遍历指针
    GCObject *up;        // 上值对象遍历指针
    
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


/**
 * [专家] 栈内存重新分配
 * 调整栈大小并更新所有相关指针，保证内存一致性
 * 
 * @param L Lua状态机
 * @param newsize 新的栈大小
 * 
 * 时间复杂度: O(n) 其中n为当前栈内容量
 * 空间复杂度: O(newsize)
 * 
 * 处理步骤：
 * 1. 计算实际所需内存大小（包含额外空间）
 * 2. 重新分配栈内存块
 * 3. 更新栈边界指针
 * 4. 调用correctstack修正所有相关指针
 * 
 * 内存管理：
 * - 使用luaM_reallocvector确保异常安全
 * - 包含EXTRA_STACK预留空间
 * - 自动处理指针重定位
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


/**
 * [进阶] 调用信息数组重分配
 * 调整调用信息数组大小并更新相关指针
 * 
 * @param L Lua状态机
 * @param newsize 新的数组大小
 * 
 * 时间复杂度: O(n) 其中n为当前数组大小
 * 空间复杂度: O(newsize)
 * 
 * 处理步骤：
 * 1. 保存旧调用信息数组指针
 * 2. 重新分配内存到新大小
 * 3. 更新数组大小记录
 * 4. 重新计算当前调用信息指针
 * 5. 设置数组结束边界
 * 
 * 指针更新：基于地址差值重新计算L->ci位置
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


/**
 * [进阶] 栈空间动态扩展
 * 根据需要智能扩展栈大小，支持深层递归
 * 
 * @param L Lua状态机
 * @param n 需要的最小栈空间
 * 
 * 时间复杂度: O(当前栈大小)
 * 空间复杂度: O(新栈大小)
 * 
 * 扩展策略：
 * 1. 双倍大小足够：扩展到2倍当前大小
 * 2. 需要更大空间：扩展到当前大小+需要量
 * 
 * 自适应机制：
 * - 避免频繁的小量扩展
 * - 平衡内存使用和性能
 * - 支持突发的大量栈需求
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


/**
 * [进阶] 调用信息数组扩展
 * 扩展调用信息数组并检查栈溢出，支持深层递归调用
 * 
 * @param L Lua状态机
 * @return 返回新分配的CallInfo指针
 * 
 * 时间复杂度: O(n) 其中n为当前调用信息数组大小
 * 空间复杂度: O(n) 
 * 
 * 扩展策略：
 * 1. 检查是否已超过最大调用数限制
 * 2. 将数组大小扩展为原来的2倍
 * 3. 再次检查扩展后是否超过限制
 * 4. 返回下一个可用的调用信息槽
 * 
 * 错误处理：
 * - LUA_ERRERR: 在处理溢出时再次溢出
 * - "stack overflow": 超过最大调用深度
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


/**
 * [进阶] 调试钩子调用处理
 * 在特定事件发生时调用用户定义的调试钩子
 * 
 * @param L Lua状态机
 * @param event 钩子事件类型（调用、返回、行、计数）
 * @param line 当前行号（行钩子时有效）
 * 
 * 时间复杂度: O(钩子函数执行时间)
 * 空间复杂度: O(1)
 * 
 * 钩子类型：
 * - LUA_HOOKCALL: 函数调用
 * - LUA_HOOKRET: 函数返回  
 * - LUA_HOOKTAILRET: 尾调用返回
 * - LUA_HOOKLINE: 行执行
 * - LUA_HOOKCOUNT: 指令计数
 * 
 * 安全措施：
 * 1. 防止钩子递归调用
 * 2. 保存/恢复栈状态
 * 3. 填充调试信息结构
 */
void luaD_callhook(lua_State *L, int event, int line)
{
    lua_Hook hook = L->hook;          // 获取钩子函数指针
    
    if (hook && L->allowhook)
    {
        // 保存当前栈状态
        ptrdiff_t top = savestack(L, L->top);         // 保存栈顶位置
        ptrdiff_t ci_top = savestack(L, L->ci->top);  // 保存调用信息栈顶
        lua_Debug ar;                                 // 调试信息结构
        
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


/**
 * [进阶] 可变参数调整处理
 * 处理函数的可变参数，创建arg表并调整栈布局
 * 
 * @param L Lua状态机
 * @param p 函数原型，包含参数信息
 * @param actual 实际传入的参数数量
 * @return 返回调整后的栈基址
 * 
 * 时间复杂度: O(n + m) 其中n=参数数量，m=栈移动量
 * 空间复杂度: O(k) 其中k=额外参数数量
 * 
 * 处理步骤：
 * 1. 用nil填充缺失的固定参数
 * 2. 在兼容模式下创建arg表存储额外参数
 * 3. 调整栈布局，将参数移动到正确位置
 * 4. 返回新的栈基址供函数使用
 * 
 * 兼容性：支持Lua 5.0风格的arg表访问
 */
static StkId adjust_varargs(lua_State *L, Proto *p, int actual)
{
    int i;                                // 循环变量
    int nfixargs = p->numparams;          // 固定参数数量
    Table *htab = NULL;                   // arg表（兼容模式）
    StkId base, fixed;                    // 栈位置指针
    
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


/**
 * [进阶] 函数调用元方法处理
 * 当对象不是函数时，尝试调用其__call元方法实现函数调用
 * 
 * @param L Lua状态机
 * @param func 指向被调用对象的栈位置
 * @return 返回调整后的栈位置，或抛出错误
 * 
 * 时间复杂度: O(n) 其中n为参数数量（栈移动）
 * 空间复杂度: O(1)
 * 
 * 处理流程：
 * 1. 获取对象的__call元方法
 * 2. 检查元方法是否存在且可调用
 * 3. 将原对象作为第一个参数插入栈中
 * 4. 将元方法函数放到调用位置
 * 5. 调整所有参数位置
 * 
 * 错误条件：
 * - 对象没有__call元方法
 * - __call元方法本身不可调用
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


/**
 * [进阶] 函数返回钩子处理
 * 处理函数返回时的钩子调用，维护调试状态
 * 
 * @param L Lua状态机
 * @param firstResult 指向第一个返回值的栈位置
 * @return 返回调整后的第一个返回值位置
 * 
 * 时间复杂度: O(1) + O(钩子函数执行时间)
 * 空间复杂度: O(1)
 * 
 * 处理步骤：
 * 1. 保存第一个返回值的栈位置
 * 2. 调用LUA_HOOKRET钩子
 * 3. 恢复栈位置（钩子可能改变栈）
 * 4. 返回调整后的位置
 * 
 * 注意事项：
 * - 钩子调用可能触发栈重分配
 * - 必须在钩子前后保存/恢复关键位置
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


/**
 * [核心] 函数调用后处理
 * 处理函数返回值并恢复调用状态，完成调用流程
 * 
 * @param L Lua状态机
 * @param firstResult 指向第一个返回值的栈位置
 * @return 1表示有固定数量的返回值，0表示多返回值
 * 
 * 时间复杂度: O(返回值数量 + 钩子时间)
 * 空间复杂度: O(1)
 * 
 * 处理流程：
 * 1. 调用返回钩子（如果启用）
 * 2. 计算和移动返回值到正确位置
 * 3. 调整栈顶位置
 * 4. 恢复前一个调用信息
 * 
 * 返回值处理：
 * - wanted >= 0: 固定数量返回值
 * - wanted == LUA_MULTRET: 保留所有返回值
 * - 自动调整栈布局适应调用者需求
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
** [专家] 调用函数（C或Lua）
**
** 详细功能说明：
** 这是Lua中所有函数调用的核心入口点，支持C函数和Lua函数的调用。
** 管理调用栈、参数传递和返回值处理。
**
** 参数说明：
** @param L - lua_State*：Lua状态机
** @param func - StkId：要调用的函数在栈上的位置
** @param nResults - int：期望的返回值数量（-1表示全部返回值）
**
** 调用约定：
** - 被调用的函数在*func位置
** - 参数在栈上紧跟函数之后
** - 返回时，所有结果都在栈上，从原函数位置开始
**
** 处理机制：
** 1. 检查C调用栈溢出
** 2. 根据函数类型选择调用方式
** 3. 管理调用信息和栈状态
** 4. 处理返回值和栈清理
**
** 算法复杂度：O(1) 调度，具体复杂度取决于被调用函数
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


/**
 * [专家] 恢复协程执行
 * 从挂起状态恢复协程的执行，处理启动和恢复两种情况
 * 
 * @param L Lua状态机（协程）
 * @param ud 用户数据，指向第一个参数的栈索引
 * 
 * 时间复杂度: O(1) + O(执行代码复杂度)
 * 空间复杂度: O(1)
 * 
 * 算法说明：
 * 1. 检查协程状态（全新启动 vs 从yield恢复）
 * 2. 全新启动：调用luaD_precall准备函数调用
 * 3. 从yield恢复：根据调用类型进行相应处理
 * 4. 最终调用luaV_execute执行Lua字节码
 */
static void resume(lua_State *L, void *ud)
{
    StkId firstArg = cast(StkId, ud);    // 第一个参数的栈位置
    CallInfo *ci = L->ci;                // 当前调用信息
    
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


/**
 * [进阶] 协程恢复错误处理
 * 处理协程恢复时发生的错误，设置错误消息
 * 
 * @param L Lua状态机
 * @param msg 错误消息字符串
 * @return 返回LUA_ERRRUN错误码
 * 
 * 时间复杂度: O(strlen(msg))
 * 空间复杂度: O(strlen(msg))
 * 
 * 算法说明：
 * 1. 重置栈顶到调用信息基址
 * 2. 创建错误消息字符串对象
 * 3. 将错误消息压入栈顶
 * 4. 解锁状态机并返回错误码
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


/**
 * [核心] Lua协程恢复API
 * 恢复挂起的协程执行，这是Lua协程系统的核心API
 * 
 * @param L 要恢复的Lua协程状态机
 * @param nargs 传递给协程的参数数量
 * @return 协程执行状态码（0=成功，LUA_YIELD=让出，其他=错误）
 * 
 * 时间复杂度: O(协程执行时间)
 * 空间复杂度: O(1)
 * 
 * 状态转换：
 * - 初始状态 -> 运行状态
 * - 挂起状态(LUA_YIELD) -> 运行状态
 * - 运行状态 -> 完成/错误/让出状态
 * 
 * 安全检查：
 * 1. 协程必须处于可恢复状态
 * 2. C栈深度不能超过限制
 * 3. 在保护模式下执行防止崩溃
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


/**
 * [核心] Lua协程让出API
 * 挂起当前协程的执行，将控制权交还给调用者
 * 
 * @param L 当前Lua协程状态机
 * @param nresults 返回给调用者的结果数量
 * @return 总是返回-1（表示协程被挂起）
 * 
 * 时间复杂度: O(1)
 * 空间复杂度: O(1)
 * 
 * 让出条件：
 * - 不能跨越元方法或C函数边界让出
 * - 必须在Lua函数内部调用
 * - C调用深度必须等于基础调用深度
 * 
 * 状态保存：
 * 1. 调整栈基址指向返回值
 * 2. 设置状态为LUA_YIELD
 * 3. 保护栈下方的数据
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


/**
 * [专家] 保护模式调用执行
 * 在保护模式下执行函数并处理错误，提供异常安全保证
 * 
 * @param L Lua状态机
 * @param func 要执行的函数指针
 * @param u 传递给函数的用户数据
 * @param old_top 旧栈顶位置（用于错误恢复）
 * @param ef 错误处理函数位置
 * @return 执行状态码（0=成功，其他=错误）
 * 
 * 时间复杂度: O(函数执行时间)
 * 空间复杂度: O(1)
 * 
 * 保护机制：
 * 1. 保存当前状态（调用信息、钩子状态等）
 * 2. 在保护模式下执行目标函数
 * 3. 错误时恢复所有保存的状态
 * 4. 关闭挂起的上值，设置错误对象
 * 
 * 状态恢复：C调用数、调用信息、栈基址、PC、钩子权限
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


/**
 * [进阶] 语法解析器数据结构
 * 传递给f_parser函数的参数封装
 * 
 * 成员说明：
 * - z: 输入流对象，支持字节码和源码
 * - buff: 词法分析器使用的动态缓冲区
 * - name: 源文件名，用于错误报告和调试信息
 */
struct SParser
{
    ZIO *z;              /* 输入流 */
    Mbuffer buff;        /* 扫描器使用的缓冲区 */
    const char *name;    /* 源文件名 */
};


/**
 * [专家] 保护模式解析器函数
 * 在保护模式下执行的解析函数，支持字节码和源码
 * 
 * @param L Lua状态机
 * @param ud 用户数据，实际是SParser结构体指针
 * 
 * 时间复杂度: O(源码长度) 或 O(字节码长度)
 * 空间复杂度: O(语法树大小)
 * 
 * 解析流程：
 * 1. 检查输入类型（通过首字节判断）
 * 2. 字节码：调用luaU_undump反序列化
 * 3. 源码：调用luaY_parser语法分析
 * 4. 创建Lua闭包包装函数原型
 * 5. 初始化所有上值为nil
 * 6. 将闭包压入栈顶
 * 
 * 输入识别：LUA_SIGNATURE[0]标识字节码文件
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


/**
 * [核心] 保护模式解析器入口
 * 在保护模式下执行解析操作，为luaL_loadbuffer等提供支持
 * 
 * @param L Lua状态机
 * @param z 输入流对象
 * @param name 源文件名称
 * @return 解析状态码（0=成功，其他=错误）
 * 
 * 时间复杂度: O(输入长度 + 语法树构建)
 * 空间复杂度: O(语法树大小 + 缓冲区大小)
 * 
 * 功能特性：
 * 1. 支持字节码和源码两种输入格式
 * 2. 提供异常安全保证，错误时自动清理
 * 3. 自动管理词法分析器缓冲区
 * 4. 解析成功后在栈顶留下闭包对象
 * 
 * 错误处理：使用luaD_pcall确保异常安全
 * 资源管理：自动释放分析缓冲区
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


