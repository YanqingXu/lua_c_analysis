/*
** [核心] Lua C API 接口实现
**
** 功能概述：
** 本模块实现了完整的Lua C API接口，为C程序提供了与Lua虚拟机交互的
** 标准接口。包含栈操作、数据类型转换、函数调用、错误处理、垃圾回收
** 控制等功能。这是Lua引擎与外部C代码交互的核心桥梁。
**
** 主要组件：
** - 栈管理：Lua虚拟栈的操作和维护
** - 类型转换：C类型与Lua类型之间的转换
** - 函数调用：从C代码调用Lua函数的机制
** - 错误处理：异常传播和错误恢复机制
** - 内存管理：与Lua垃圾回收器的协调
** - 线程支持：多线程环境下的Lua状态管理
**
** API设计原则：
** - 类型安全：严格的类型检查和转换
** - 栈中心：基于虚拟栈的操作模型
** - 异常安全：自动的错误处理和资源清理
** - 线程安全：支持多线程环境下的并发访问
** - 简洁高效：最小化的接口复杂度和高性能
**
** 栈操作模型：
** - 索引系统：正数从底部计算，负数从顶部计算
** - 伪索引：注册表、环境表、全局表的特殊访问
** - 自动增长：根据需要自动扩展栈空间
** - 边界检查：防止栈溢出和越界访问
**
** 错误处理机制：
** - longjmp异常：使用C的setjmp/longjmp机制
** - 错误传播：自动的错误信息传递
** - 资源清理：异常安全的内存管理
** - 调试支持：详细的错误信息和调用栈
**
** 依赖模块：
** - lstate.c：Lua状态机管理，线程创建和销毁
** - ldo.c：函数调用和错误处理机制
** - lgc.c：垃圾回收器的协调和控制
** - lobject.c：Lua对象系统的类型操作
** - ltable.c：表操作和元表处理
** - lstring.c：字符串对象的创建和管理
*/

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#define lapi_c
#define LUA_CORE

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"


/*
** [公共] Lua版本标识字符串
**
** 功能说明：
** 包含Lua版本信息、版权声明、作者信息和官方网址的标识字符串。
** 用于程序识别和版本验证。
**
** 格式说明：
** - 版本和版权信息
** - 作者列表
** - 官方网址
*/
const char lua_ident[] =
    "$Lua: " LUA_RELEASE " " LUA_COPYRIGHT " $\n"
    "$Authors: " LUA_AUTHORS " $\n"
    "$URL: www.lua.org $\n";


/*
** [宏定义] API调试检查宏
**
** 功能说明：
** 提供API函数的参数验证和边界检查，在调试模式下启用详细的
** 运行时检查，确保API使用的正确性。
**
** 宏定义说明：
** - api_checknelems：检查栈中是否有足够的元素
** - api_checkvalidindex：检查索引是否指向有效对象
** - api_incr_top：安全地增加栈顶指针
*/
#define api_checknelems(L, n)     api_check(L, (n) <= (L->top - L->base))
#define api_checkvalidindex(L, i) api_check(L, (i) != luaO_nilobject)
#define api_incr_top(L)           {api_check(L, L->top < L->ci->top); L->top++;}


/*
** [内部] 索引到地址转换
**
** 详细功能说明：
** 将API函数的栈索引转换为实际的TValue指针。支持正索引、负索引
** 和伪索引（注册表、环境表等）。这是栈操作的核心转换函数。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈索引值
**
** 返回值：
** @return TValue*：指向栈元素的指针，无效索引返回nilobject
**
** 算法复杂度：O(1) 时间
**
** 索引转换规则：
** - 正索引：从栈底开始计算（1为第一个元素）
** - 负索引：从栈顶开始计算（-1为栈顶元素）
** - 伪索引：特殊的系统表访问
**
** 伪索引类型：
** - LUA_REGISTRYINDEX：注册表访问
** - LUA_ENVIRONINDEX：当前函数环境表
** - LUA_GLOBALSINDEX：全局表
** - 其他负值：upvalue访问
**
** 边界检查：
** - 验证索引在有效范围内
** - 防止栈溢出和越界访问
** - 返回安全的nil对象指针
*/
static TValue *index2adr(lua_State *L, int idx)
{
    /*
    ** [正索引处理] 从栈底开始的索引
    */
    if (idx > 0)
    {
        TValue *o = L->base + (idx - 1);
        api_check(L, idx <= L->ci->top - L->base);
        
        /*
        ** [边界检查] 确保不超出当前栈顶
        */
        if (o >= L->top)
        {
            return cast(TValue *, luaO_nilobject);
        }
        else
        {
            return o;
        }
    }
    /*
    ** [负索引处理] 从栈顶开始的索引
    */
    else if (idx > LUA_REGISTRYINDEX)
    {
        api_check(L, idx != 0 && -idx <= L->top - L->base);
        return L->top + idx;
    }
    /*
    ** [伪索引处理] 特殊系统表的访问
    */
    else
    {
        switch (idx)
        {
            /*
            ** [注册表访问] 全局注册表
            */
            case LUA_REGISTRYINDEX:
            {
                return registry(L);
            }
            
            /*
            ** [环境表访问] 当前函数的环境表
            */
            case LUA_ENVIRONINDEX:
            {
                Closure *func = curr_func(L);
                sethvalue(L, &L->env, func->c.env);
                return &L->env;
            }
            
            /*
            ** [全局表访问] 全局变量表
            */
            case LUA_GLOBALSINDEX:
            {
                return gt(L);
            }
            
            /*
            ** [Upvalue访问] 函数的upvalue
            */
            default:
            {
                Closure *func = curr_func(L);
                idx = LUA_GLOBALSINDEX - idx;
                return (idx <= func->c.nupvalues)
                    ? &func->c.upvalue[idx-1]
                    : cast(TValue *, luaO_nilobject);
            }
        }
    }
}


/*
** [内部] 获取当前环境表
**
** 详细功能说明：
** 获取当前执行上下文的环境表。如果在主线程中且没有封闭函数，
** 返回全局表；否则返回当前函数的环境表。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return Table*：当前环境表指针
**
** 算法复杂度：O(1) 时间
**
** 环境选择逻辑：
** - 主线程无函数：使用全局表
** - 有封闭函数：使用函数环境表
*/
static Table *getcurrenv(lua_State *L)
{
    /*
    ** [主线程检查] 是否在基础调用栈
    */
    if (L->ci == L->base_ci)
    {
        /*
        ** [全局环境] 没有封闭函数时使用全局表
        */
        return hvalue(gt(L));
    }
    else
    {
        /*
        ** [函数环境] 使用当前函数的环境表
        */
        Closure *func = curr_func(L);
        return func->c.env;
    }
}


/*
** [内部] 推送对象到栈
**
** 详细功能说明：
** 将指定的Lua值对象推送到栈顶。这是内部使用的核心栈操作函数，
** 被其他API函数广泛使用。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param o - const TValue*：要推送的值对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
**
** 操作步骤：
** 1. 复制对象到栈顶
** 2. 安全地增加栈顶指针
** 3. 更新栈状态
*/
void luaA_pushobject(lua_State *L, const TValue *o)
{
    setobj2s(L, L->top, o);
    api_incr_top(L);
}


/*
** [公共API] 检查栈空间
**
** 详细功能说明：
** 确保栈中有足够的空间来容纳指定数量的元素。如果空间不足，
** 尝试扩展栈；如果无法扩展，返回失败状态。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param size - int：需要的额外栈空间大小
**
** 返回值：
** @return int：1=成功，0=失败（栈溢出）
**
** 算法复杂度：O(1) 时间，可能触发O(n)的栈扩展
**
** 检查逻辑：
** 1. 验证请求大小不超过最大限制
** 2. 检查当前栈是否有足够空间
** 3. 必要时调用栈扩展函数
** 4. 更新调用栈信息
**
** 线程安全：
** - 使用lua_lock/lua_unlock保护
** - 原子性的栈扩展操作
*/
LUA_API int lua_checkstack(lua_State *L, int size)
{
    int res = 1;
    lua_lock(L);
    
    /*
    ** [溢出检查] 验证请求大小的合理性
    */
    if (size > LUAI_MAXCSTACK || (L->top - L->base + size) > LUAI_MAXCSTACK)
    {
        res = 0;  /* 栈溢出 */
    }
    else if (size > 0)
    {
        /*
        ** [栈扩展] 确保有足够的栈空间
        */
        luaD_checkstack(L, size);
        
        /*
        ** [调用栈更新] 更新当前调用的栈顶限制
        */
        if (L->ci->top < L->top + size)
        {
            L->ci->top = L->top + size;
        }
    }
    
    lua_unlock(L);
    return res;
}


/*
** [公共API] 跨线程移动栈元素
**
** 详细功能说明：
** 在两个Lua线程之间移动栈顶的n个元素。源线程的栈顶元素被移动到
** 目标线程的栈顶。这用于协程间的数据传递。
**
** 参数说明：
** @param from - lua_State*：源线程状态
** @param to - lua_State*：目标线程状态
** @param n - int：要移动的元素数量
**
** 返回值：无
**
** 算法复杂度：O(n) 时间
**
** 前置条件：
** - 两个线程必须属于同一个Lua状态
** - 源线程必须有足够的元素
** - 目标线程必须有足够的空间
**
** 操作步骤：
** 1. 验证前置条件
** 2. 从源线程弹出元素
** 3. 推送到目标线程
** 4. 更新两个线程的栈状态
*/
LUA_API void lua_xmove(lua_State *from, lua_State *to, int n)
{
    int i;
    
    /*
    ** [同线程检查] 相同线程时无需操作
    */
    if (from == to)
    {
        return;
    }
    
    lua_lock(to);
    
    /*
    ** [参数验证] 检查移动的有效性
    */
    api_checknelems(from, n);
    api_check(from, G(from) == G(to));
    api_check(from, to->ci->top - to->top >= n);
    
    /*
    ** [元素移动] 逐个复制栈元素
    */
    from->top -= n;
    for (i = 0; i < n; i++)
    {
        setobj2s(to, to->top++, from->top + i);
    }
    
    lua_unlock(to);
}


/*
** [公共API] 设置调用层级
**
** 详细功能说明：
** 将一个线程的C调用层级设置为另一个线程的层级。这用于协程
** 创建时同步调用栈深度，防止栈溢出检查错误。
**
** 参数说明：
** @param from - lua_State*：源线程状态
** @param to - lua_State*：目标线程状态
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_setlevel(lua_State *from, lua_State *to)
{
    to->nCcalls = from->nCcalls;
}


/*
** [公共API] 设置恐慌函数
**
** 详细功能说明：
** 设置Lua状态的恐慌函数，当发生不可恢复的错误时调用。恐慌函数
** 是最后的错误处理机制，通常用于程序清理和错误记录。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param panicf - lua_CFunction：新的恐慌函数
**
** 返回值：
** @return lua_CFunction：原来的恐慌函数
**
** 算法复杂度：O(1) 时间
**
** 线程安全：
** - 使用lua_lock/lua_unlock保护
** - 原子性的函数指针更新
*/
LUA_API lua_CFunction lua_atpanic(lua_State *L, lua_CFunction panicf)
{
    lua_CFunction old;
    lua_lock(L);
    
    /*
    ** [函数更新] 原子性地更新恐慌函数
    */
    old = G(L)->panic;
    G(L)->panic = panicf;
    
    lua_unlock(L);
    return old;
}


/*
** [公共API] 创建新线程
**
** 详细功能说明：
** 在当前Lua状态中创建一个新的协程线程。新线程与主线程共享全局
** 状态，但有独立的栈和调用栈。新线程被推送到调用者的栈顶。
**
** 参数说明：
** @param L - lua_State*：主线程状态
**
** 返回值：
** @return lua_State*：新创建的线程状态
**
** 算法复杂度：O(1) 时间，可能触发垃圾回收
**
** 操作步骤：
** 1. 检查垃圾回收
** 2. 创建新线程状态
** 3. 将线程对象推送到栈
** 4. 增加栈顶指针
**
** 内存管理：
** - 自动触发垃圾回收检查
** - 新线程由垃圾回收器管理
** - 线程对象正确地添加到栈中
*/
LUA_API lua_State *lua_newthread(lua_State *L)
{
    lua_State *L1;
    lua_lock(L);
    
    /*
    ** [垃圾回收] 检查是否需要垃圾回收
    */
    luaC_checkGC(L);
    
    /*
    ** [线程创建] 创建新的线程状态
    */
    L1 = luaE_newthread(L);
    
    /*
    ** [栈操作] 将新线程推送到栈顶
    */
    setthvalue(L, L->top, L1);
    api_incr_top(L);
    
    lua_unlock(L);
    return L1;
}


/*
** [进阶] 基础栈操作函数集
**
** 功能概述：
** 提供对Lua虚拟栈的基本操作接口，包括获取栈大小、设置栈顶、
** 插入删除元素等。这些是栈操作的基础API。
*/


/*
** [公共API] 获取栈顶位置
**
** 详细功能说明：
** 返回当前栈中元素的数量。栈顶位置等于栈中有效元素的个数。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：栈中元素的数量
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_gettop(lua_State *L)
{
    return cast_int(L->top - L->base);
}


/*
** [公共API] 设置栈顶位置
**
** 详细功能说明：
** 设置栈顶到指定位置。如果新位置高于当前栈顶，用nil填充新位置；
** 如果新位置低于当前栈顶，丢弃多余的元素。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：新的栈顶位置
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是位置差距
**
** 操作逻辑：
** - 正数：设置绝对栈位置
** - 负数：相对于当前栈顶的位置
** - 自动填充nil或丢弃元素
*/
LUA_API void lua_settop(lua_State *L, int idx)
{
    lua_lock(L);
    
    /*
    ** [正索引处理] 设置绝对栈位置
    */
    if (idx >= 0)
    {
        api_check(L, idx <= L->stack_last - L->base);
        
        /*
        ** [栈扩展] 用nil填充新位置
        */
        while (L->top < L->base + idx)
        {
            setnilvalue(L->top++);
        }
        
        /*
        ** [栈顶设置] 更新栈顶指针
        */
        L->top = L->base + idx;
    }
    /*
    ** [负索引处理] 相对栈顶位置
    */
    else
    {
        api_check(L, -(idx+1) <= (L->top - L->base));
        L->top += idx+1;  /* 减去索引值（索引为负数） */
    }
    
    lua_unlock(L);
}


/*
** [公共API] 删除栈元素
**
** 详细功能说明：
** 删除指定位置的栈元素，并将其上方的所有元素下移填补空隙。
** 栈的总大小减1。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：要删除的元素位置
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是需要移动的元素数量
**
** 操作步骤：
** 1. 验证索引有效性
** 2. 向下移动上方所有元素
** 3. 减少栈顶指针
*/
LUA_API void lua_remove(lua_State *L, int idx)
{
    StkId p;
    lua_lock(L);
    
    /*
    ** [位置获取] 获取要删除的元素位置
    */
    p = index2adr(L, idx);
    api_checkvalidindex(L, p);
    
    /*
    ** [元素移动] 向下移动所有上方元素
    */
    while (++p < L->top)
    {
        setobjs2s(L, p-1, p);
    }
    
    /*
    ** [栈顶调整] 减少栈大小
    */
    L->top--;
    lua_unlock(L);
}


/*
** [公共API] 插入栈元素
**
** 详细功能说明：
** 将栈顶元素插入到指定位置，原位置及其上方的元素都向上移动。
** 栈的总大小保持不变。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：插入位置
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是需要移动的元素数量
**
** 操作步骤：
** 1. 验证插入位置有效性
** 2. 向上移动目标位置及以上的元素
** 3. 将栈顶元素复制到目标位置
*/
LUA_API void lua_insert(lua_State *L, int idx)
{
    StkId p;
    StkId q;
    lua_lock(L);
    
    /*
    ** [位置获取] 获取插入位置
    */
    p = index2adr(L, idx);
    api_checkvalidindex(L, p);
    
    /*
    ** [元素移动] 向上移动元素为插入腾出空间
    */
    for (q = L->top; q > p; q--)
    {
        setobjs2s(L, q, q-1);
    }
    
    /*
    ** [元素插入] 将栈顶元素复制到目标位置
    */
    setobjs2s(L, p, L->top);
    
    lua_unlock(L);
}


/*
** [公共API] 替换栈元素
**
** 详细功能说明：
** 用栈顶元素替换指定位置的元素，然后弹出栈顶元素。栈大小减1。
** 对于特殊索引（环境表、upvalue），执行相应的设置操作。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：要替换的位置
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
**
** 特殊处理：
** - LUA_ENVIRONINDEX：设置函数环境表
** - upvalue索引：设置函数upvalue
** - 普通索引：直接替换值
*/
LUA_API void lua_replace(lua_State *L, int idx)
{
    StkId o;
    lua_lock(L);
    
    /*
    ** [环境表检查] 验证环境设置的合法性
    */
    if (idx == LUA_ENVIRONINDEX && L->ci == L->base_ci)
    {
        luaG_runerror(L, "no calling environment");
    }
    
    api_checknelems(L, 1);
    o = index2adr(L, idx);
    api_checkvalidindex(L, o);
    
    /*
    ** [环境表设置] 特殊处理环境表替换
    */
    if (idx == LUA_ENVIRONINDEX)
    {
        Closure *func = curr_func(L);
        api_check(L, ttistable(L->top - 1));
        func->c.env = hvalue(L->top - 1);
        luaC_barrier(L, func, L->top - 1);
    }
    /*
    ** [普通替换] 直接设置值
    */
    else
    {
        setobj(L, o, L->top - 1);
        
        /*
        ** [垃圾回收屏障] upvalue设置时的写屏障
        */
        if (idx < LUA_GLOBALSINDEX)
        {
            luaC_barrier(L, curr_func(L), L->top - 1);
        }
    }
    
    /*
    ** [栈顶调整] 弹出栈顶元素
    */
    L->top--;
    lua_unlock(L);
}


/*
** [公共API] 推送栈元素副本
**
** 详细功能说明：
** 将指定位置的栈元素复制一份并推送到栈顶。原元素保持不变。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：要复制的元素位置
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_pushvalue(lua_State *L, int idx)
{
    lua_lock(L);
    setobj2s(L, L->top, index2adr(L, idx));
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [进阶] 访问函数集（栈到C）
**
** 功能概述：
** 提供从Lua栈读取数据到C程序的接口函数，包括类型检查、
** 类型转换和值提取等操作。
*/


/*
** [公共API] 获取元素类型
**
** 详细功能说明：
** 返回指定栈位置元素的类型标识符。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return int：类型标识符（LUA_TNONE、LUA_TNIL等）
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_type(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    return (o == luaO_nilobject) ? LUA_TNONE : ttype(o);
}


/*
** [公共API] 获取类型名称
**
** 详细功能说明：
** 返回指定类型标识符对应的类型名称字符串。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针（未使用）
** @param t - int：类型标识符
**
** 返回值：
** @return const char*：类型名称字符串
**
** 算法复杂度：O(1) 时间
*/
LUA_API const char *lua_typename(lua_State *L, int t)
{
    UNUSED(L);
    return (t == LUA_TNONE) ? "no value" : luaT_typenames[t];
}


/*
** [公共API] 检查是否为C函数
**
** 详细功能说明：
** 检查指定位置的元素是否为C函数。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return int：1=是C函数，0=不是
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_iscfunction(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    return iscfunction(o);
}


/*
** [公共API] 检查是否为数值
**
** 详细功能说明：
** 检查指定位置的元素是否可以转换为数值。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return int：1=可转换为数值，0=不可转换
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_isnumber(lua_State *L, int idx)
{
    TValue n;
    const TValue *o = index2adr(L, idx);
    return tonumber(o, &n);
}


/*
** [公共API] 检查是否为字符串
**
** 详细功能说明：
** 检查指定位置的元素是否为字符串或可转换为字符串的数值。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return int：1=是字符串或数值，0=不是
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_isstring(lua_State *L, int idx)
{
    int t = lua_type(L, idx);
    return (t == LUA_TSTRING || t == LUA_TNUMBER);
}


/*
** [公共API] 检查是否为用户数据
**
** 详细功能说明：
** 检查指定位置的元素是否为用户数据（完整或轻量级）。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return int：1=是用户数据，0=不是
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_isuserdata(lua_State *L, int idx)
{
    const TValue *o = index2adr(L, idx);
    return (ttisuserdata(o) || ttislightuserdata(o));
}


/*
** [公共API] 原始相等比较
**
** 详细功能说明：
** 比较两个栈元素是否原始相等（不调用元方法）。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param index1 - int：第一个元素位置
** @param index2 - int：第二个元素位置
**
** 返回值：
** @return int：1=相等，0=不相等
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_rawequal(lua_State *L, int index1, int index2)
{
    StkId o1 = index2adr(L, index1);
    StkId o2 = index2adr(L, index2);
    return (o1 == luaO_nilobject || o2 == luaO_nilobject) ? 0
        : luaO_rawequalObj(o1, o2);
}


/*
** [公共API] 相等比较
**
** 详细功能说明：
** 比较两个栈元素是否相等，可能调用__eq元方法。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param index1 - int：第一个元素位置
** @param index2 - int：第二个元素位置
**
** 返回值：
** @return int：1=相等，0=不相等
**
** 算法复杂度：O(1) 时间，可能触发元方法
**
** 线程安全：
** - 可能调用元方法，需要加锁保护
*/
LUA_API int lua_equal(lua_State *L, int index1, int index2)
{
    StkId o1, o2;
    int i;
    lua_lock(L);  /* 可能调用元方法 */
    
    o1 = index2adr(L, index1);
    o2 = index2adr(L, index2);
    i = (o1 == luaO_nilobject || o2 == luaO_nilobject) ? 0 : equalobj(L, o1, o2);
    
    lua_unlock(L);
    return i;
}


/*
** [公共API] 小于比较
**
** 详细功能说明：
** 比较第一个元素是否小于第二个元素，可能调用__lt元方法。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param index1 - int：第一个元素位置
** @param index2 - int：第二个元素位置
**
** 返回值：
** @return int：1=小于，0=不小于
**
** 算法复杂度：O(1) 时间，可能触发元方法
*/
LUA_API int lua_lessthan(lua_State *L, int index1, int index2)
{
    StkId o1, o2;
    int i;
    lua_lock(L);  /* 可能调用元方法 */
    
    o1 = index2adr(L, index1);
    o2 = index2adr(L, index2);
    i = (o1 == luaO_nilobject || o2 == luaO_nilobject) ? 0
        : luaV_lessthan(L, o1, o2);
        
    lua_unlock(L);
    return i;
}


/*
** [公共API] 转换为数值
**
** 详细功能说明：
** 将指定位置的栈元素转换为数值类型。如果不能转换则返回0。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return lua_Number：转换后的数值，失败时为0
**
** 算法复杂度：O(1) 时间
*/
LUA_API lua_Number lua_tonumber(lua_State *L, int idx)
{
    TValue n;
    const TValue *o = index2adr(L, idx);
    
    if (tonumber(o, &n))
    {
        return nvalue(o);
    }
    else
    {
        return 0;
    }
}


/*
** [公共API] 转换为整数
**
** 详细功能说明：
** 将指定位置的栈元素转换为整数类型。先转换为数值，再转为整数。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return lua_Integer：转换后的整数，失败时为0
**
** 算法复杂度：O(1) 时间
*/
LUA_API lua_Integer lua_tointeger(lua_State *L, int idx)
{
    TValue n;
    const TValue *o = index2adr(L, idx);
    
    if (tonumber(o, &n))
    {
        lua_Integer res;
        lua_Number num = nvalue(o);
        lua_number2integer(res, num);
        return res;
    }
    else
    {
        return 0;
    }
}


/*
** [公共API] 转换为布尔值
**
** 详细功能说明：
** 将指定位置的栈元素转换为布尔值。只有nil和false为假，其他都为真。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return int：1=真，0=假
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_toboolean(lua_State *L, int idx)
{
    const TValue *o = index2adr(L, idx);
    return !l_isfalse(o);
}


/*
** [公共API] 转换为字符串
**
** 详细功能说明：
** 将指定位置的栈元素转换为字符串。如果元素不是字符串，尝试转换；
** 如果无法转换则返回NULL。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
** @param len - size_t*：输出参数，字符串长度
**
** 返回值：
** @return const char*：字符串指针，失败时为NULL
**
** 算法复杂度：O(1) 时间，可能触发字符串转换
**
** 注意事项：
** - 可能修改栈元素（数值转字符串）
** - 需要加锁保护转换过程
*/
LUA_API const char *lua_tolstring(lua_State *L, int idx, size_t *len)
{
    StkId o = index2adr(L, idx);
    
    if (!ttisstring(o))
    {
        lua_lock(L);  /* luaV_tostring可能创建新字符串 */
        
        if (!luaV_tostring(L, o))  /* 转换失败？ */
        {
            if (len != NULL)
            {
                *len = 0;
            }
            lua_unlock(L);
            return NULL;
        }
        
        /*
        ** [垃圾回收] 转换可能创建新对象
        */
        luaC_checkGC(L);
        
        /*
        ** [地址重新获取] 垃圾回收可能移动栈
        */
        o = index2adr(L, idx);
        lua_unlock(L);
    }
    
    /*
    ** [长度设置] 返回字符串长度
    */
    if (len != NULL)
    {
        *len = tsvalue(o)->len;
    }
    
    return svalue(o);
}


/*
** [公共API] 获取对象长度
**
** 详细功能说明：
** 获取指定位置对象的长度。对于不同类型使用不同的长度计算方法。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return size_t：对象长度，无法计算时返回0
**
** 算法复杂度：O(1) 时间，表长度可能为O(n)
**
** 长度计算规则：
** - 字符串：字节长度
** - 用户数据：分配的字节数
** - 表：数组部分长度
** - 数值：转换为字符串后的长度
*/
LUA_API size_t lua_objlen(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    
    switch (ttype(o))
    {
        case LUA_TSTRING:
        {
            return tsvalue(o)->len;
        }
        
        case LUA_TUSERDATA:
        {
            return uvalue(o)->len;
        }
        
        case LUA_TTABLE:
        {
            return luaH_getn(hvalue(o));
        }
        
        case LUA_TNUMBER:
        {
            size_t l;
            lua_lock(L);  /* luaV_tostring可能创建新字符串 */
            l = (luaV_tostring(L, o) ? tsvalue(o)->len : 0);
            lua_unlock(L);
            return l;
        }
        
        default:
        {
            return 0;
        }
    }
}


/*
** [公共API] 转换为C函数
**
** 详细功能说明：
** 将指定位置的栈元素转换为C函数指针。只有C函数类型才能转换。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return lua_CFunction：C函数指针，失败时为NULL
**
** 算法复杂度：O(1) 时间
*/
LUA_API lua_CFunction lua_tocfunction(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    return (!iscfunction(o)) ? NULL : clvalue(o)->c.f;
}


/*
** [公共API] 转换为用户数据
**
** 详细功能说明：
** 将指定位置的栈元素转换为用户数据指针。支持完整用户数据和轻量用户数据。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return void*：用户数据指针，失败时为NULL
**
** 算法复杂度：O(1) 时间
*/
LUA_API void *lua_touserdata(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    
    switch (ttype(o))
    {
        case LUA_TUSERDATA:
        {
            return (rawuvalue(o) + 1);
        }
        
        case LUA_TLIGHTUSERDATA:
        {
            return pvalue(o);
        }
        
        default:
        {
            return NULL;
        }
    }
}


/*
** [公共API] 转换为线程
**
** 详细功能说明：
** 将指定位置的栈元素转换为Lua线程状态指针。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return lua_State*：线程状态指针，失败时为NULL
**
** 算法复杂度：O(1) 时间
*/
LUA_API lua_State *lua_tothread(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    return (!ttisthread(o)) ? NULL : thvalue(o);
}


/*
** [公共API] 转换为通用指针
**
** 详细功能说明：
** 将指定位置的栈元素转换为通用的void指针。主要用于对象标识和调试。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：栈位置
**
** 返回值：
** @return const void*：对象指针，失败时为NULL
**
** 算法复杂度：O(1) 时间
*/
LUA_API const void *lua_topointer(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    
    switch (ttype(o))
    {
        case LUA_TTABLE:
        {
            return hvalue(o);
        }
        
        case LUA_TFUNCTION:
        {
            return clvalue(o);
        }
        
        case LUA_TTHREAD:
        {
            return thvalue(o);
        }
        
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
        {
            return lua_touserdata(L, idx);
        }
        
        default:
        {
            return NULL;
        }
    }
}


/*
** [进阶] 推送函数集（C到栈）
**
** 功能概述：
** 提供将C数据推送到Lua栈的接口函数，包括基础类型、字符串、
** 函数等各种Lua值类型。
*/


/*
** [公共API] 推送nil值
**
** 详细功能说明：
** 将一个nil值推送到栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_pushnil(lua_State *L)
{
    lua_lock(L);
    setnilvalue(L->top);
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 推送数值
**
** 详细功能说明：
** 将一个Lua数值推送到栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param n - lua_Number：要推送的数值
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_pushnumber(lua_State *L, lua_Number n)
{
    lua_lock(L);
    setnvalue(L->top, n);
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 推送整数
**
** 详细功能说明：
** 将一个整数推送到栈顶，内部转换为Lua数值类型。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param n - lua_Integer：要推送的整数
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_pushinteger(lua_State *L, lua_Integer n)
{
    lua_lock(L);
    setnvalue(L->top, cast_num(n));
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 推送指定长度字符串
**
** 详细功能说明：
** 将指定长度的字符串推送到栈顶。支持包含空字符的二进制字符串。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param s - const char*：字符串数据
** @param len - size_t：字符串长度
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是字符串长度
**
** 内存管理：
** - 可能触发垃圾回收
** - 字符串被内部化管理
*/
LUA_API void lua_pushlstring(lua_State *L, const char *s, size_t len)
{
    lua_lock(L);
    luaC_checkGC(L);
    setsvalue2s(L, L->top, luaS_newlstr(L, s, len));
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 推送C字符串
**
** 详细功能说明：
** 将以null结尾的C字符串推送到栈顶。如果字符串为NULL则推送nil。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param s - const char*：C字符串
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是字符串长度
*/
LUA_API void lua_pushstring(lua_State *L, const char *s)
{
    if (s == NULL)
    {
        lua_pushnil(L);
    }
    else
    {
        lua_pushlstring(L, s, strlen(s));
    }
}


/*
** [公共API] 推送格式化字符串（va_list版本）
**
** 详细功能说明：
** 使用printf风格的格式化推送字符串到栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param fmt - const char*：格式化字符串
** @param argp - va_list：参数列表
**
** 返回值：
** @return const char*：生成的字符串指针
**
** 算法复杂度：O(n) 时间，其中n是生成字符串长度
*/
LUA_API const char *lua_pushvfstring(lua_State *L, const char *fmt, va_list argp)
{
    const char *ret;
    lua_lock(L);
    luaC_checkGC(L);
    ret = luaO_pushvfstring(L, fmt, argp);
    lua_unlock(L);
    return ret;
}


/*
** [公共API] 推送格式化字符串
**
** 详细功能说明：
** 使用printf风格的格式化推送字符串到栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param fmt - const char*：格式化字符串
** @param ... - 可变参数：格式化参数
**
** 返回值：
** @return const char*：生成的字符串指针
**
** 算法复杂度：O(n) 时间，其中n是生成字符串长度
*/
LUA_API const char *lua_pushfstring(lua_State *L, const char *fmt, ...)
{
    const char *ret;
    va_list argp;
    lua_lock(L);
    luaC_checkGC(L);
    va_start(argp, fmt);
    ret = luaO_pushvfstring(L, fmt, argp);
    va_end(argp);
    lua_unlock(L);
    return ret;
}


/*
** [公共API] 推送C闭包
**
** 详细功能说明：
** 创建一个C闭包并推送到栈顶。闭包包含指定数量的upvalue，
** 这些upvalue从栈顶取得。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param fn - lua_CFunction：C函数指针
** @param n - int：upvalue数量
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是upvalue数量
**
** 操作步骤：
** 1. 检查栈中有足够的upvalue
** 2. 创建C闭包对象
** 3. 设置函数指针和upvalue
** 4. 将闭包推送到栈顶
*/
LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n)
{
    Closure *cl;
    lua_lock(L);
    luaC_checkGC(L);
    api_checknelems(L, n);
    
    /*
    ** [闭包创建] 创建具有n个upvalue的C闭包
    */
    cl = luaF_newCclosure(L, n, getcurrenv(L));
    cl->c.f = fn;
    
    /*
    ** [Upvalue设置] 从栈顶获取upvalue
    */
    L->top -= n;
    while (n--)
    {
        setobj2n(L, &cl->c.upvalue[n], L->top+n);
    }
    
    /*
    ** [闭包推送] 将闭包推送到栈顶
    */
    setclvalue(L, L->top, cl);
    lua_assert(iswhite(obj2gco(cl)));
    api_incr_top(L);
    
    lua_unlock(L);
}


/*
** [公共API] 推送布尔值
**
** 详细功能说明：
** 将一个布尔值推送到栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param b - int：布尔值（0=false，非0=true）
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_pushboolean(lua_State *L, int b)
{
    lua_lock(L);
    setbvalue(L->top, (b != 0));  /* 确保true为1 */
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 推送轻量用户数据
**
** 详细功能说明：
** 将一个轻量用户数据（void指针）推送到栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param p - void*：用户数据指针
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_pushlightuserdata(lua_State *L, void *p)
{
    lua_lock(L);
    setpvalue(L->top, p);
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 推送当前线程
**
** 详细功能说明：
** 将当前线程推送到栈顶，并返回是否为主线程。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：1=主线程，0=协程
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_pushthread(lua_State *L)
{
    lua_lock(L);
    setthvalue(L, L->top, L);
    api_incr_top(L);
    lua_unlock(L);
    return (G(L)->mainthread == L);
}


/*
** [进阶] 获取函数集（Lua到栈）
**
** 功能概述：
** 提供从Lua对象中读取值并推送到栈的接口函数，包括表访问、
** 字段获取、元表操作等。
*/


/*
** [公共API] 获取表元素
**
** 详细功能说明：
** 获取表中指定键的值并推送到栈顶。键从栈顶取得，可能调用__index元方法。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，可能触发元方法
*/
LUA_API void lua_gettable(lua_State *L, int idx)
{
    StkId t;
    lua_lock(L);
    t = index2adr(L, idx);
    api_checkvalidindex(L, t);
    luaV_gettable(L, t, L->top - 1, L->top - 1);
    lua_unlock(L);
}


/*
** [公共API] 获取表字段
**
** 详细功能说明：
** 获取表中指定字符串字段的值并推送到栈顶。可能调用__index元方法。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
** @param k - const char*：字段名
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，可能触发元方法
*/
LUA_API void lua_getfield(lua_State *L, int idx, const char *k)
{
    StkId t;
    TValue key;
    lua_lock(L);
    t = index2adr(L, idx);
    api_checkvalidindex(L, t);
    setsvalue(L, &key, luaS_new(L, k));
    luaV_gettable(L, t, &key, L->top);
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 原始获取表元素
**
** 详细功能说明：
** 直接获取表中指定键的值，不调用元方法。键从栈顶取得。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_rawget(lua_State *L, int idx)
{
    StkId t;
    lua_lock(L);
    t = index2adr(L, idx);
    api_check(L, ttistable(t));
    setobj2s(L, L->top - 1, luaH_get(hvalue(t), L->top - 1));
    lua_unlock(L);
}


/*
** [公共API] 原始获取表数组元素
**
** 详细功能说明：
** 直接获取表中指定数字索引的值，不调用元方法。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
** @param n - int：数组索引
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_rawgeti(lua_State *L, int idx, int n)
{
    StkId o;
    lua_lock(L);
    o = index2adr(L, idx);
    api_check(L, ttistable(o));
    setobj2s(L, L->top, luaH_getnum(hvalue(o), n));
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 创建表
**
** 详细功能说明：
** 创建一个新表并推送到栈顶。可以预分配数组和哈希部分的大小。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param narray - int：数组部分预分配大小
** @param nrec - int：哈希部分预分配大小
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是预分配大小
*/
LUA_API void lua_createtable(lua_State *L, int narray, int nrec)
{
    lua_lock(L);
    luaC_checkGC(L);
    sethvalue(L, L->top, luaH_new(L, narray, nrec));
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [公共API] 获取元表
**
** 详细功能说明：
** 获取指定对象的元表并推送到栈顶。如果对象没有元表则推送nil。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param objindex - int：对象的栈位置
**
** 返回值：
** @return int：1=有元表，0=无元表
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_getmetatable(lua_State *L, int objindex)
{
    const TValue *obj;
    Table *mt = NULL;
    int res;
    lua_lock(L);
    
    obj = index2adr(L, objindex);
    switch (ttype(obj))
    {
        case LUA_TTABLE:
        {
            mt = hvalue(obj)->metatable;
            break;
        }
        
        case LUA_TUSERDATA:
        {
            mt = uvalue(obj)->metatable;
            break;
        }
        
        default:
        {
            mt = G(L)->mt[ttype(obj)];
            break;
        }
    }
    
    if (mt == NULL)
    {
        res = 0;
    }
    else
    {
        sethvalue(L, L->top, mt);
        api_incr_top(L);
        res = 1;
    }
    
    lua_unlock(L);
    return res;
}


/*
** [公共API] 获取环境表
**
** 详细功能说明：
** 获取指定对象的环境表并推送到栈顶。不同类型对象有不同的环境表。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：对象的栈位置
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_getfenv(lua_State *L, int idx)
{
    StkId o;
    lua_lock(L);
    o = index2adr(L, idx);
    api_checkvalidindex(L, o);
    
    switch (ttype(o))
    {
        case LUA_TFUNCTION:
        {
            sethvalue(L, L->top, clvalue(o)->c.env);
            break;
        }
        
        case LUA_TUSERDATA:
        {
            sethvalue(L, L->top, uvalue(o)->env);
            break;
        }
        
        case LUA_TTHREAD:
        {
            setobj2s(L, L->top, gt(thvalue(o)));
            break;
        }
        
        default:
        {
            setnilvalue(L->top);
            break;
        }
    }
    
    api_incr_top(L);
    lua_unlock(L);
}


/*
** [进阶] 设置函数集（栈到Lua）
**
** 功能概述：
** 提供将栈顶值设置到Lua对象的接口函数，包括表设置、
** 字段设置、元表设置等。
*/


/*
** [公共API] 设置表元素
**
** 详细功能说明：
** 设置表中指定键的值。键和值都从栈顶取得，可能调用__newindex元方法。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，可能触发元方法
*/
LUA_API void lua_settable(lua_State *L, int idx)
{
    StkId t;
    lua_lock(L);
    api_checknelems(L, 2);
    t = index2adr(L, idx);
    api_checkvalidindex(L, t);
    luaV_settable(L, t, L->top - 2, L->top - 1);
    L->top -= 2;  /* 弹出键和值 */
    lua_unlock(L);
}


/*
** [公共API] 设置表字段
**
** 详细功能说明：
** 设置表中指定字符串字段的值。值从栈顶取得，可能调用__newindex元方法。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
** @param k - const char*：字段名
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，可能触发元方法
*/
LUA_API void lua_setfield(lua_State *L, int idx, const char *k)
{
    StkId t;
    TValue key;
    lua_lock(L);
    api_checknelems(L, 1);
    t = index2adr(L, idx);
    api_checkvalidindex(L, t);
    setsvalue(L, &key, luaS_new(L, k));
    luaV_settable(L, t, &key, L->top - 1);
    L->top--;  /* 弹出值 */
    lua_unlock(L);
}


/*
** [公共API] 原始设置表元素
**
** 详细功能说明：
** 直接设置表中指定键的值，不调用元方法。键和值都从栈顶取得。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_rawset(lua_State *L, int idx)
{
    StkId t;
    lua_lock(L);
    api_checknelems(L, 2);
    t = index2adr(L, idx);
    api_check(L, ttistable(t));
    setobj2t(L, luaH_set(L, hvalue(t), L->top-2), L->top-1);
    luaC_barriert(L, hvalue(t), L->top-1);
    L->top -= 2;
    lua_unlock(L);
}


/*
** [公共API] 原始设置表数组元素
**
** 详细功能说明：
** 直接设置表中指定数字索引的值，不调用元方法。值从栈顶取得。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
** @param n - int：数组索引
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_rawseti(lua_State *L, int idx, int n)
{
    StkId o;
    lua_lock(L);
    api_checknelems(L, 1);
    o = index2adr(L, idx);
    api_check(L, ttistable(o));
    setobj2t(L, luaH_setnum(L, hvalue(o), n), L->top-1);
    luaC_barriert(L, hvalue(o), L->top-1);
    L->top--;
    lua_unlock(L);
}


/*
** [公共API] 设置元表
**
** 详细功能说明：
** 设置指定对象的元表。元表从栈顶取得，只有表和用户数据可以设置元表。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param objindex - int：对象的栈位置
**
** 返回值：
** @return int：1=成功，0=失败
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_setmetatable(lua_State *L, int objindex)
{
    TValue *obj;
    Table *mt;
    lua_lock(L);
    api_checknelems(L, 1);
    obj = index2adr(L, objindex);
    api_checkvalidindex(L, obj);
    
    /*
    ** [元表验证] 元表必须是nil或table
    */
    if (ttisnil(L->top - 1))
    {
        mt = NULL;
    }
    else
    {
        api_check(L, ttistable(L->top - 1));
        mt = hvalue(L->top - 1);
    }
    
    /*
    ** [类型检查] 根据对象类型设置元表
    */
    switch (ttype(obj))
    {
        case LUA_TTABLE:
        {
            hvalue(obj)->metatable = mt;
            if (mt)
            {
                luaC_objbarriert(L, hvalue(obj), mt);
            }
            break;
        }
        
        case LUA_TUSERDATA:
        {
            uvalue(obj)->metatable = mt;
            if (mt)
            {
                luaC_objbarrier(L, rawuvalue(obj), mt);
            }
            break;
        }
        
        default:
        {
            G(L)->mt[ttype(obj)] = mt;
            break;
        }
    }
    
    L->top--;
    lua_unlock(L);
    return 1;
}


/*
** [公共API] 设置环境表
**
** 详细功能说明：
** 设置指定对象的环境表。环境表从栈顶取得，只有函数、用户数据和线程可以设置环境表。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：对象的栈位置
**
** 返回值：
** @return int：1=成功，0=失败
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_setfenv(lua_State *L, int idx)
{
    StkId o;
    int res = 1;
    lua_lock(L);
    api_checknelems(L, 1);
    o = index2adr(L, idx);
    api_checkvalidindex(L, o);
    api_check(L, ttistable(L->top - 1));
    
    switch (ttype(o))
    {
        case LUA_TFUNCTION:
        {
            clvalue(o)->c.env = hvalue(L->top - 1);
            break;
        }
        
        case LUA_TUSERDATA:
        {
            uvalue(o)->env = hvalue(L->top - 1);
            break;
        }
        
        case LUA_TTHREAD:
        {
            sethvalue(L, gt(thvalue(o)), hvalue(L->top - 1));
            break;
        }
        
        default:
        {
            res = 0;
            break;
        }
    }
    
    if (res)
    {
        luaC_objbarrier(L, gcvalue(o), hvalue(L->top - 1));
    }
    
    L->top--;
    lua_unlock(L);
    return res;
}


/*
** [进阶] 函数调用和执行控制
**
** 功能概述：
** 提供函数调用、协程控制、错误处理等高级功能的接口。
*/


/*
** [宏定义] 结果检查宏
**
** 功能说明：
** 检查调用结果数量的合理性，确保栈有足够空间容纳结果。
*/
#define checkresults(L,na,nr) \
    api_check(L, (nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)))


/*
** [宏定义] 结果调整宏
**
** 功能说明：
** 根据期望的结果数量调整栈顶位置。
*/
#define adjustresults(L,nres) \
    { if (nres == LUA_MULTRET && L->top >= L->ci->top) L->ci->top = L->top; }


/*
** [公共API] 调用函数
**
** 详细功能说明：
** 调用栈顶的函数，传入指定数量的参数，返回指定数量的结果。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param nargs - int：参数数量
** @param nresults - int：期望的返回值数量
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是函数复杂度
**
** 调用约定：
** - 函数在参数下方
** - 参数按顺序在栈顶
** - 调用后函数和参数被替换为结果
*/
LUA_API void lua_call(lua_State *L, int nargs, int nresults)
{
    StkId func;
    lua_lock(L);
    api_checknelems(L, nargs+1);
    checkresults(L, nargs, nresults);
    func = L->top - (nargs+1);
    luaD_call(L, func, nresults);
    adjustresults(L, nresults);
    lua_unlock(L);
}


/*
** [内部] 保护调用数据结构
**
** 功能说明：
** 传递给f_call函数的数据结构，包含要调用的函数和期望的结果数量。
*/
struct CallS {  /* f_call的数据 */
    StkId func;      /* 要调用的函数 */
    int nresults;    /* 期望的结果数量 */
};


/*
** [内部] 保护调用包装函数
**
** 详细功能说明：
** 在保护模式下执行函数调用的包装函数。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param ud - void*：CallS结构指针
**
** 返回值：无
*/
static void f_call(lua_State *L, void *ud)
{
    struct CallS *c = cast(struct CallS *, ud);
    luaD_call(L, c->func, c->nresults);
}


/*
** [公共API] 保护调用函数
**
** 详细功能说明：
** 在保护模式下调用函数，如果发生错误不会传播，而是返回错误码。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param nargs - int：参数数量
** @param nresults - int：期望的返回值数量
** @param errfunc - int：错误处理函数位置
**
** 返回值：
** @return int：0=成功，非0=错误码
**
** 算法复杂度：O(n) 时间，其中n是函数复杂度
*/
LUA_API int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc)
{
    struct CallS c;
    int status;
    ptrdiff_t func;
    lua_lock(L);
    api_checknelems(L, nargs+1);
    checkresults(L, nargs, nresults);
    
    if (errfunc == 0)
    {
        func = 0;
    }
    else
    {
        StkId o = index2adr(L, errfunc);
        api_checkvalidindex(L, o);
        func = savestack(L, o);
    }
    
    c.func = L->top - (nargs+1);  /* 要调用的函数 */
    c.nresults = nresults;
    status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);
    adjustresults(L, nresults);
    lua_unlock(L);
    return status;
}


/*
** [内部] C函数保护调用数据结构
**
** 功能说明：
** 传递给f_Ccall函数的数据结构，包含C函数指针和用户数据。
*/
struct CCallS {  /* f_Ccall的数据 */
    lua_CFunction func;  /* C函数指针 */
    void *ud;           /* 用户数据 */
};


/*
** [内部] C函数保护调用包装函数
**
** 详细功能说明：
** 在保护模式下执行C函数调用的包装函数。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param ud - void*：CCallS结构指针
**
** 返回值：无
*/
static void f_Ccall(lua_State *L, void *ud)
{
    struct CCallS *c = cast(struct CCallS *, ud);
    Closure *cl;
    cl = luaF_newCclosure(L, 0, getcurrenv(L));
    cl->c.f = c->func;
    setclvalue(L, L->top, cl);  /* 推送函数 */
    api_incr_top(L);
    setpvalue(L->top, c->ud);  /* 推送唯一参数 */
    api_incr_top(L);
    luaD_call(L, L->top - 2, 0);
}


/*
** [公共API] C函数保护调用
**
** 详细功能说明：
** 在保护模式下调用C函数，主要用于调用没有参数的C函数。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param func - lua_CFunction：C函数指针
** @param ud - void*：用户数据
**
** 返回值：
** @return int：0=成功，非0=错误码
**
** 算法复杂度：O(n) 时间，其中n是函数复杂度
*/
LUA_API int lua_cpcall(lua_State *L, lua_CFunction func, void *ud)
{
    struct CCallS c;
    int status;
    lua_lock(L);
    c.func = func;
    c.ud = ud;
    status = luaD_pcall(L, f_Ccall, &c, savestack(L, L->top), 0);
    lua_unlock(L);
    return status;
}


/*
** [公共API] 获取线程状态
**
** 详细功能说明：
** 获取Lua线程的当前状态（正常、挂起、错误等）。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：线程状态码
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_status(lua_State *L)
{
    return L->status;
}


/*
** [核心] 挂起协程
**
** 详细功能说明：
** 挂起当前协程的执行，可用于实现协程的暂停和恢复机制。
** 该函数会保存当前执行状态，并将控制权返回给调用者。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param nresults - int：返回结果数量
**
** 返回值：
** @return int：总是返回-1表示挂起
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_yield(lua_State *L, int nresults)
{
    lua_lock(L);
    if (L->ci == L->base_ci)
        luaG_runerror(L, "attempt to yield outside a coroutine");
    L->base = L->top - nresults;  /* 保护结果 */
    L->status = LUA_YIELD;
    lua_unlock(L);
    return -1;
}


/*
** [核心] 恢复协程执行
**
** 详细功能说明：
** 恢复之前挂起的协程执行。该函数会从协程上次挂起的地方
** 继续执行，并可以传递参数给恢复的协程。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param from - lua_State*：恢复协程的调用者状态
** @param narg - int：传递给协程的参数数量
**
** 返回值：
** @return int：协程执行状态
**
** 算法复杂度：O(n) 时间，n为执行指令数量
*/
LUA_API int lua_resume(lua_State *L, lua_State *from, int narg)
{
    int status;
    lua_lock(L);
    if (L->status != LUA_YIELD && (L->status != 0 || L->ci != L->base_ci))
        luaG_runerror(L, "cannot resume non-suspended coroutine");
    L->base = L->top - narg;  /* 设置参数基址 */
    status = luaD_resume(L, from, narg);
    if (status != 0) {
        L->status = cast_byte(status);  /* 标记线程为"死亡" */
    }
    lua_unlock(L);
    return status;
}


/*
** [进阶] 产生错误并终止执行
**
** 详细功能说明：
** 抛出一个Lua错误，类似于Lua中的error()函数。
** 该函数会从栈顶取一个值作为错误消息，永不返回。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：该函数永不返回
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_error(lua_State *L)
{
    lua_lock(L);
    api_checknelems(L, 1);
    luaG_errormsg(L);
    lua_unlock(L);
    return 0;  /* 永不返回，为避免警告 */
}


/*
** [进阶] 遍历表的下一个键值对
**
** 详细功能说明：
** 用于遍历表的所有键值对。当键为nil时开始遍历，
** 返回下一个键值对，如果没有更多元素则返回0。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表在栈中的索引
**
** 返回值：
** @return int：如果有更多元素返回非零值，否则返回0
**
** 算法复杂度：O(1) 平均时间
*/
LUA_API int lua_next(lua_State *L, int idx)
{
    StkId t;
    int more;
    lua_lock(L);
    t = index2adr(L, idx);
    api_check(L, ttistable(t));
    more = luaH_next(L, hvalue(t), L->top - 1);
    if (more) {
        api_incr_top(L);
    }
    else {  /* 没有更多元素 */
        L->top -= 1;  /* 移除键（但保持表） */
    }
    lua_unlock(L);
    return more;
}


/*
** [进阶] 连接栈顶的多个值
**
** 详细功能说明：
** 连接栈顶的n个值（它们必须是字符串或数字），弹出这些值，
** 并将结果字符串压入栈。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param n - int：要连接的值的数量
**
** 返回值：
** @return void
**
** 算法复杂度：O(n) 时间，n为总字符长度
*/
LUA_API void lua_concat(lua_State *L, int n)
{
    lua_lock(L);
    api_checknelems(L, n);
    if (n >= 2) {
        luaC_checkGC(L);
        luaV_concat(L, n, cast_int(L->top - L->base) - 1);
        L->top -= (n-1);
    }
    else if (n == 0) {  /* 连接零个值产生空字符串 */
        setsvalue2s(L, L->top, luaS_newlstr(L, "", 0));
        api_incr_top(L);
    }
    /* else n == 1; 不做任何操作 */
    lua_unlock(L);
}


/*
** [核心] 垃圾回收控制
**
** 详细功能说明：
** 执行垃圾回收操作或设置垃圾回收参数。
** 支持多种操作：停止、重启、收集、计数、设置参数等。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param what - int：操作类型
** @param data - int：操作参数
**
** 返回值：
** @return int：操作结果
**
** 算法复杂度：取决于具体操作
*/
LUA_API int lua_gc(lua_State *L, int what, int data)
{
    int res = 0;
    global_State *g;
    lua_lock(L);
    g = G(L);
    switch (what) {
        case LUA_GCSTOP: {
            g->GCthreshold = MAX_LUMEM;
            break;
        }
        case LUA_GCRESTART: {
            g->GCthreshold = g->totalbytes;
            break;
        }
        case LUA_GCCOLLECT: {
            luaC_fullgc(L);
            break;
        }
        case LUA_GCCOUNT: {
            /* GC值以K字节为单位返回 */
            res = cast_int(g->totalbytes >> 10);
            break;
        }
        case LUA_GCCOUNTB: {
            res = cast_int(g->totalbytes & 0x3ff);
            break;
        }
        case LUA_GCSTEP: {
            lu_mem a = (cast(lu_mem, data) << 10);
            if (a <= g->totalbytes)
                g->GCthreshold = g->totalbytes - a;
            else
                g->GCthreshold = 0;
            while (g->GCthreshold <= g->totalbytes) {
                luaC_step(L);
                if (g->gcstate == GCSpause) {  /* 结束周期？ */
                    res = 1;  /* 信号表示 */
                    break;
                }
            }
            break;
        }
        case LUA_GCSETPAUSE: {
            res = g->gcpause;
            g->gcpause = data;
            break;
        }
        case LUA_GCSETSTEPMUL: {
            res = g->gcstepmul;
            g->gcstepmul = data;
            break;
        }
        default: res = -1;  /* 无效选项 */
    }
    lua_unlock(L);
    return res;
}


/*
** [进阶] 设置表的元表
**
** 详细功能说明：
** 为栈顶的表设置元表，元表也必须在栈上。
** 该函数会弹出栈顶的元表并设置给下面的表。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param objindex - int：表在栈中的索引
**
** 返回值：
** @return int：成功返回1，失败返回0
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_setmetatable(lua_State *L, int objindex)
{
    TValue *obj;
    Table *mt;
    lua_lock(L);
    api_checknelems(L, 1);
    obj = index2adr(L, objindex);
    api_checkvalidindex(L, obj);
    if (ttisnil(L->top - 1))
        mt = NULL;
    else {
        api_check(L, ttistable(L->top - 1));
        mt = hvalue(L->top - 1);
    }
    switch (ttype(obj)) {
        case LUA_TTABLE: {
            hvalue(obj)->metatable = mt;
            if (mt)
                luaC_objbarriert(L, hvalue(obj), mt);
            break;
        }
        case LUA_TUSERDATA: {
            uvalue(obj)->metatable = mt;
            if (mt)
                luaC_objbarrier(L, rawuvalue(obj), mt);
            break;
        }
        default: {
            G(L)->mt[ttype(obj)] = mt;
            break;
        }
    }
    L->top--;
    lua_unlock(L);
    return 1;
}


/*
** [进阶] 获取表的元表
**
** 详细功能说明：
** 获取指定对象的元表并压入栈。如果对象没有元表
** 或元表不可访问，返回0且不压入任何值。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param objindex - int：对象在栈中的索引
**
** 返回值：
** @return int：有元表返回1，否则返回0
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_getmetatable(lua_State *L, int objindex)
{
    const TValue *obj;
    Table *mt = NULL;
    int res;
    lua_lock(L);
    obj = index2adr(L, objindex);
    switch (ttype(obj)) {
        case LUA_TTABLE:
            mt = hvalue(obj)->metatable;
            break;
        case LUA_TUSERDATA:
            mt = uvalue(obj)->metatable;
            break;
        default:
            mt = G(L)->mt[ttype(obj)];
            break;
    }
    if (mt == NULL)
        res = 0;
    else {
        sethvalue(L, L->top, mt);
        api_incr_top(L);
        res = 1;
    }
    lua_unlock(L);
    return res;
}


/*
** [进阶] 原始获取表元素
**
** 详细功能说明：
** 类似于lua_gettable，但不触发__index元方法。
** 直接从表中获取键对应的值。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表在栈中的索引
**
** 返回值：
** @return void
**
** 算法复杂度：O(1) 平均时间
*/
LUA_API void lua_rawget(lua_State *L, int idx)
{
    StkId t;
    lua_lock(L);
    t = index2adr(L, idx);
    api_check(L, ttistable(t));
    setobj2s(L, L->top - 1, luaH_get(hvalue(t), L->top - 1));
    lua_unlock(L);
}


/*
** [进阶] 原始设置表元素
**
** 详细功能说明：
** 类似于lua_settable，但不触发__newindex元方法。
** 直接在表中设置键值对。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表在栈中的索引
**
** 返回值：
** @return void
**
** 算法复杂度：O(1) 平均时间
*/
LUA_API void lua_rawset(lua_State *L, int idx)
{
    StkId t;
    lua_lock(L);
    api_checknelems(L, 2);
    t = index2adr(L, idx);
    api_check(L, ttistable(t));
    setobj2t(L, luaH_set(L, hvalue(t), L->top-2), L->top-1);
    luaC_barriert(L, hvalue(t), L->top-1);
    L->top -= 2;
    lua_unlock(L);
}


/*
** [公共API] 加载Lua代码块
**
** 详细功能说明：
** 从reader函数加载Lua代码块，编译但不执行。编译后的函数被推送到栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param reader - lua_Reader：读取函数
** @param data - void*：传递给reader的用户数据
** @param chunkname - const char*：代码块名称
**
** 返回值：
** @return int：0=成功，非0=错误码
**
** 算法复杂度：O(n) 时间，其中n是代码大小
*/
LUA_API int lua_load(lua_State *L, lua_Reader reader, void *data, const char *chunkname)
{
    ZIO z;
    int status;
    lua_lock(L);
    
    if (!chunkname)
    {
        chunkname = "?";
    }
    
    luaZ_init(L, &z, reader, data);
    status = luaD_protectedparser(L, &z, chunkname);
    lua_unlock(L);
    return status;
}


/*
** [公共API] 转储函数为字节码
**
** 详细功能说明：
** 将栈顶的Lua函数转储为字节码，通过writer函数输出。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param writer - lua_Writer：输出函数
** @param data - void*：传递给writer的用户数据
**
** 返回值：
** @return int：0=成功，非0=错误码
**
** 算法复杂度：O(n) 时间，其中n是函数大小
*/
LUA_API int lua_dump(lua_State *L, lua_Writer writer, void *data)
{
    int status;
    TValue *o;
    lua_lock(L);
    api_checknelems(L, 1);
    o = L->top - 1;
    
    if (isLfunction(o))
    {
        status = luaU_dump(L, clvalue(o)->l.p, writer, data, 0);
    }
    else
    {
        status = 1;
    }
    
    lua_unlock(L);
    return status;
}


/*
** [公共API] 垃圾回收控制
**
** 详细功能说明：
** 控制垃圾回收器的行为，包括停止、重启、执行步骤、设置参数等。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param what - int：操作类型
** @param data - int：操作参数
**
** 返回值：
** @return int：操作结果
**
** 算法复杂度：取决于具体操作
*/
LUA_API int lua_gc(lua_State *L, int what, int data)
{
    int res = 0;
    global_State *g;
    lua_lock(L);
    g = G(L);
    
    switch (what)
    {
        case LUA_GCSTOP:
        {
            g->GCthreshold = MAX_LUMEM;
            break;
        }
        
        case LUA_GCRESTART:
        {
            g->GCthreshold = g->totalbytes;
            break;
        }
        
        case LUA_GCCOLLECT:
        {
            luaC_fullgc(L);
            break;
        }
        
        case LUA_GCCOUNT:
        {
            res = cast_int(g->totalbytes >> 10);
            break;
        }
        
        case LUA_GCCOUNTB:
        {
            res = cast_int(g->totalbytes & 0x3ff);
            break;
        }
        
        case LUA_GCSTEP:
        {
            lu_mem a = (cast(lu_mem, data) << 10);
            if (a <= g->totalbytes)
            {
                g->GCthreshold = g->totalbytes - a;
            }
            else
            {
                g->GCthreshold = 0;
            }
            
            while (g->GCthreshold <= g->totalbytes)
            {
                luaC_step(L);
                
                if (g->gcstate == GCSpause)
                {
                    res = 1;
                    break;
                }
            }
            break;
        }
        
        case LUA_GCSETPAUSE:
        {
            res = g->gcpause;
            g->gcpause = data;
            break;
        }
        
        case LUA_GCSETSTEPMUL:
        {
            res = g->gcstepmul;
            g->gcstepmul = data;
            break;
        }
        
        default:
        {
            res = -1;
            break;
        }
    }
    
    lua_unlock(L);
    return res;
}


/*
** [进阶] 其他实用函数
**
** 功能概述：
** 提供错误处理、内存分配、迭代等其他有用的API函数。
*/


/*
** [公共API] 抛出错误
**
** 详细功能说明：
** 抛出一个Lua错误，错误信息从栈顶取得。这个函数不会返回。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：永不返回
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_error(lua_State *L)
{
    lua_lock(L);
    api_checknelems(L, 1);
    luaG_errormsg(L);
    lua_unlock(L);
    return 0;  /* 永不到达 */
}


/*
** [公共API] 表迭代器
**
** 详细功能说明：
** 实现表的通用迭代，用于pairs()函数的实现。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param idx - int：表的栈位置
**
** 返回值：
** @return int：1=有下一个元素，0=迭代结束
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_next(lua_State *L, int idx)
{
    StkId t;
    int more;
    lua_lock(L);
    t = index2adr(L, idx);
    api_check(L, ttistable(t));
    more = luaH_next(L, hvalue(t), L->top - 1);
    
    if (more)
    {
        api_incr_top(L);
    }
    else
    {
        L->top -= 1;
    }
    
    lua_unlock(L);
    return more;
}


/*
** [公共API] 连接栈顶字符串
**
** 详细功能说明：
** 连接栈顶的n个字符串，结果推送到栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param n - int：要连接的字符串数量
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是总字符串长度
*/
LUA_API void lua_concat(lua_State *L, int n)
{
    lua_lock(L);
    api_checknelems(L, n);
    
    if (n >= 2)
    {
        luaC_checkGC(L);
        luaV_concat(L, n, cast_int(L->top - L->base) - 1);
        L->top -= n-1;
    }
    else if (n == 0)
    {
        setsvalue2s(L, L->top, luaS_newlstr(L, "", 0));
        api_incr_top(L);
    }
    
    lua_unlock(L);
}


/*
** [公共API] 获取内存分配函数
**
** 详细功能说明：
** 获取当前Lua状态使用的内存分配函数和用户数据。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param ud - void**：输出参数，用户数据指针
**
** 返回值：
** @return lua_Alloc：内存分配函数指针
**
** 算法复杂度：O(1) 时间
*/
LUA_API lua_Alloc lua_getallocf(lua_State *L, void **ud)
{
    lua_Alloc f;
    lua_lock(L);
    
    if (ud)
    {
        *ud = G(L)->ud;
    }
    
    f = G(L)->frealloc;
    lua_unlock(L);
    return f;
}


/*
** [公共API] 设置内存分配函数
**
** 详细功能说明：
** 设置Lua状态使用的内存分配函数和用户数据。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param f - lua_Alloc：新的内存分配函数
** @param ud - void*：用户数据
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
LUA_API void lua_setallocf(lua_State *L, lua_Alloc f, void *ud)
{
    lua_lock(L);
    G(L)->ud = ud;
    G(L)->frealloc = f;
    lua_unlock(L);
}