/**
 * @file lapi.c
 * @brief Lua 5.1.5 C API实现：所有Lua C API函数的具体实现
 *
 * 详细说明：
 * 本文件是Lua C API的核心实现文件，包含了lua.h中声明的所有API函数的具体实现。
 * 它是C程序与Lua虚拟机交互的桥梁，提供了完整的栈操作、类型检查、函数调用、
 * 错误处理等功能。这些实现确保了API的安全性、高效性和易用性。
 *
 * 系统架构定位：
 * - Lua C API的具体实现层，位于lua.h接口定义之下
 * - 虚拟机内核与外部C代码的接口适配层
 * - 提供类型安全的栈操作和数据转换机制
 * - 实现错误处理和异常安全的API调用模式
 * - 集成垃圾回收器，确保内存管理的正确性
 *
 * 主要功能模块：
 * 1. 栈管理：栈空间检查、栈顶操作、元素移动和复制
 * 2. 索引转换：栈索引到内部地址的转换和验证
 * 3. 类型系统：类型检查、类型转换、类型判断
 * 4. 数据访问：从栈中获取各种类型的数据
 * 5. 数据推入：向栈中推入各种类型的数据
 * 6. 表操作：表的创建、访问、修改和元表管理
 * 7. 函数调用：安全的函数调用和错误处理
 * 8. 环境管理：全局环境和函数环境的管理
 * 9. 垃圾回收：GC控制和内存管理接口
 * 10. 调试支持：调试信息访问和钩子管理
 *
 * 设计理念：
 * - 安全性：所有API调用都进行参数验证和边界检查
 * - 一致性：统一的错误处理和返回值约定
 * - 高效性：最小化不必要的数据复制和类型转换
 * - 简洁性：提供简单直观的接口，隐藏内部复杂性
 * - 可靠性：异常安全，确保虚拟机状态的一致性
 *
 * 栈操作模型：
 * - 基于栈的操作模型，简化C与Lua的数据交换
 * - 正索引：从栈底开始，1表示第一个元素
 * - 负索引：从栈顶开始，-1表示栈顶元素
 * - 伪索引：特殊索引，访问注册表、环境表等
 * - 自动栈管理：API调用自动维护栈的一致性
 *
 * 错误处理机制：
 * - 参数检查：所有API函数都进行严格的参数验证
 * - 边界检查：防止栈溢出和非法内存访问
 * - 异常安全：确保错误情况下虚拟机状态的正确性
 * - 错误传播：通过longjmp机制传播Lua错误
 * - 调试支持：提供详细的错误信息和调试钩子
 *
 * 性能特征：
 * - 栈操作：O(1)时间复杂度的栈访问和修改
 * - 类型检查：高效的类型标记检查
 * - 内存管理：与垃圾回收器紧密集成
 * - 函数调用：优化的调用约定和参数传递
 * - 缓存友好：局部性良好的数据访问模式
 *
 * 线程安全性：
 * - 非线程安全：单个lua_State不能被多线程同时访问
 * - 状态隔离：不同lua_State之间完全独立
 * - 锁机制：内部使用lua_lock/lua_unlock保护关键区域
 * - 协程支持：通过lua_newthread创建协程状态
 *
 * 内存管理：
 * - 自动GC：所有Lua对象都由垃圾回收器管理
 * - 栈管理：自动扩展和收缩栈空间
 * - 引用管理：正确处理C引用和Lua引用
 * - 内存限制：支持内存使用限制和监控
 *
 * 扩展性：
 * - 用户数据：支持自定义C数据类型
 * - C函数：支持注册C函数到Lua
 * - 元表：支持自定义类型行为
 * - 环境：支持自定义执行环境
 *
 * 调试和诊断：
 * - 调试钩子：支持行、调用、返回钩子
 * - 栈跟踪：提供完整的调用栈信息
 * - 错误报告：详细的错误位置和原因
 * - 性能分析：支持性能监控和分析
 *
 * 使用模式：
 * - 嵌入式：在C程序中嵌入Lua解释器
 * - 扩展：为Lua添加C函数和库
 * - 绑定：将C/C++库绑定到Lua
 * - 脚本化：为应用程序添加脚本支持
 *
 * 最佳实践：
 * - 栈平衡：确保API调用前后栈的平衡
 * - 错误检查：检查所有可能失败的API调用
 * - 资源管理：正确管理C资源的生命周期
 * - 类型安全：使用类型检查函数验证数据类型
 * - 异常安全：使用pcall保护可能出错的操作
 *
 * @author Roberto Ierusalimschy, Luiz Henrique de Figueiredo, Waldemar Celes
 * @version 5.1.5
 * @date 2008
 * @since C89
 * @see lua.h, lstate.h, lobject.h, lgc.h
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

/**
 * @brief Lua版本标识字符串
 *
 * 详细说明：
 * 包含Lua版本信息、版权信息、作者信息和官方网址的标识字符串。
 * 用于版本识别和调试信息输出。
 *
 * @since C89
 */
const char lua_ident[] =
    "$Lua: " LUA_RELEASE " " LUA_COPYRIGHT " $\n"
    "$Authors: " LUA_AUTHORS " $\n"
    "$URL: www.lua.org $\n";

/**
 * @name API内部宏定义
 * @brief 用于API实现的内部辅助宏
 * @{
 */

/**
 * @brief 检查栈中是否有足够的元素
 *
 * 详细说明：
 * 验证栈中至少有n个元素可供操作。这是一个安全检查宏，
 * 防止访问不存在的栈元素。
 *
 * @param L Lua状态机
 * @param n 需要的元素数量
 *
 * @since C89
 * @see api_check, lua_gettop
 */
#define api_checknelems(L, n)       api_check(L, (n) <= (L->top - L->base))

/**
 * @brief 检查索引是否指向有效的栈位置
 *
 * 详细说明：
 * 验证给定的索引是否指向一个有效的栈位置，而不是nil对象。
 * 用于确保API操作的安全性。
 *
 * @param L Lua状态机
 * @param i 要检查的索引对应的TValue指针
 *
 * @since C89
 * @see api_check, luaO_nilobject
 */
#define api_checkvalidindex(L, i)   api_check(L, (i) != luaO_nilobject)

/**
 * @brief 安全地增加栈顶指针
 *
 * 详细说明：
 * 在确保不会超出当前调用信息允许的栈顶限制的前提下，
 * 将栈顶指针向上移动一个位置。
 *
 * @param L Lua状态机
 *
 * @since C89
 * @see api_check, CallInfo
 */
#define api_incr_top(L)             {api_check(L, L->top < L->ci->top); L->top++;}

/** @} */



/**
 * @brief 将栈索引转换为内部TValue指针
 *
 * 详细说明：
 * 这是Lua API的核心函数之一，负责将用户提供的栈索引转换为指向实际
 * TValue对象的指针。它处理正索引、负索引和伪索引三种情况，是所有
 * 栈操作函数的基础。
 *
 * 索引类型处理：
 * 1. 正索引（idx > 0）：从栈底开始计数，1表示第一个元素
 * 2. 负索引（LUA_REGISTRYINDEX < idx < 0）：从栈顶开始计数，-1表示栈顶
 * 3. 伪索引（idx <= LUA_REGISTRYINDEX）：访问特殊位置如注册表、环境等
 *
 * 安全检查：
 * - 正索引：检查不超出当前函数的栈顶限制
 * - 负索引：检查不超出实际栈顶
 * - 边界检查：防止访问无效内存区域
 * - 返回nil对象：对于无效索引返回安全的nil对象指针
 *
 * 伪索引处理：
 * - LUA_REGISTRYINDEX：全局注册表，存储C代码的引用
 * - LUA_ENVIRONINDEX：当前函数的环境表
 * - LUA_GLOBALSINDEX：全局变量表
 * - 上值索引：当前C函数的上值（LUA_GLOBALSINDEX-n）
 *
 * 性能考虑：
 * - O(1)时间复杂度的地址计算
 * - 最小化内存访问和指针运算
 * - 内联友好的简单逻辑
 * - 缓存友好的顺序访问模式
 *
 * 错误处理：
 * - 无效索引返回luaO_nilobject而不是NULL
 * - 使用api_check进行调试时的断言检查
 * - 不抛出异常，保证调用安全性
 *
 * @param L Lua状态机指针
 * @param idx 栈索引，可以是正数、负数或伪索引
 * @return 指向对应TValue的指针，无效索引返回luaO_nilobject
 *
 * @note 这是一个内部函数，不直接暴露给用户
 * @note 返回的指针可能指向临时位置（如L->env），使用时需注意
 * @warning 调用者必须确保返回的指针在使用期间有效
 *
 * @since C89
 * @see luaO_nilobject, registry, gt, curr_func
 */
static TValue *index2adr(lua_State *L, int idx)
{
    if (idx > 0) {
        TValue *o = L->base + (idx - 1);
        api_check(L, idx <= L->ci->top - L->base);
        if (o >= L->top) return cast(TValue *, luaO_nilobject);
        else return o;
    }
    else if (idx > LUA_REGISTRYINDEX) {
        api_check(L, idx != 0 && -idx <= L->top - L->base);
        return L->top + idx;
    }
    else switch (idx) {
        case LUA_REGISTRYINDEX: return registry(L);
        case LUA_ENVIRONINDEX: {
            Closure *func = curr_func(L);
            sethvalue(L, &L->env, func->c.env);
            return &L->env;
        }
        case LUA_GLOBALSINDEX: return gt(L);
        default: {
            Closure *func = curr_func(L);
            idx = LUA_GLOBALSINDEX - idx;
            return (idx <= func->c.nupvalues)
                        ? &func->c.upvalue[idx-1]
                        : cast(TValue *, luaO_nilobject);
        }
    }
}


/**
 * @brief 获取当前函数的环境表
 *
 * 详细说明：
 * 获取当前执行上下文的环境表。如果当前没有活动的函数调用
 * （即在主线程的顶层），则返回全局表；否则返回当前函数的环境表。
 *
 * 环境表的作用：
 * - 存储函数可访问的变量和函数
 * - 实现词法作用域和闭包
 * - 支持沙箱和安全执行环境
 * - 提供模块化和命名空间机制
 *
 * 调用上下文判断：
 * - L->ci == L->base_ci：表示在主线程顶层，无函数调用
 * - 其他情况：有活动的函数调用，使用函数的环境表
 *
 * @param L Lua状态机指针
 * @return 当前环境表的指针
 *
 * @note 这是一个内部辅助函数
 * @since C89
 * @see gt, curr_func, hvalue
 */
static Table *getcurrenv(lua_State *L)
{
    if (L->ci == L->base_ci)
        return hvalue(gt(L));
    else {
        Closure *func = curr_func(L);
        return func->c.env;
    }
}

/**
 * @brief 将TValue对象推入栈顶
 *
 * 详细说明：
 * 这是一个内部辅助函数，用于将任意TValue对象安全地推入栈顶。
 * 它正确处理对象的复制和栈指针的更新，确保栈的一致性。
 *
 * 操作步骤：
 * 1. 使用setobj2s将对象复制到栈顶
 * 2. 使用api_incr_top安全地增加栈顶指针
 * 3. 保持对象的类型和值完整性
 *
 * 内存管理：
 * - 正确处理GC对象的引用
 * - 保持栈中对象的可达性
 * - 不影响原始对象的生命周期
 *
 * @param L Lua状态机指针
 * @param o 要推入的TValue对象指针
 *
 * @note 这是一个内部函数，由其他API函数调用
 * @note 调用前必须确保栈有足够空间
 * @warning 不进行栈空间检查，调用者负责确保安全性
 *
 * @since C89
 * @see setobj2s, api_incr_top
 */
void luaA_pushobject(lua_State *L, const TValue *o)
{
    setobj2s(L, L->top, o);
    api_incr_top(L);
}

/**
 * @brief 检查并确保栈有足够的空间
 *
 * 详细说明：
 * 检查栈是否有足够的空间容纳指定数量的新元素。如果空间不足，
 * 会尝试扩展栈空间。这是一个重要的安全函数，防止栈溢出。
 *
 * 安全检查：
 * - 检查请求的大小是否超过最大C栈限制
 * - 检查当前栈使用量加上请求大小是否超限
 * - 防止栈溢出导致的内存错误
 *
 * 栈扩展机制：
 * - 调用luaD_checkstack进行实际的栈扩展
 * - 更新当前调用信息的栈顶限制
 * - 保持栈的连续性和一致性
 *
 * 性能考虑：
 * - 只在必要时进行栈扩展
 * - 批量检查避免频繁的安全检查
 * - 预分配策略减少重复扩展
 *
 * 错误处理：
 * - 栈溢出时返回0，不抛出异常
 * - 调用者可以根据返回值决定后续操作
 * - 保持虚拟机状态的一致性
 *
 * @param L Lua状态机指针
 * @param size 需要的额外栈空间大小
 * @return 成功返回非零值，失败返回0
 *
 * @note 这是一个公共API函数
 * @note 建议在进行大量栈操作前调用此函数
 * @warning size为0时不进行任何操作但仍返回成功
 *
 * @since C89
 * @see luaD_checkstack, LUAI_MAXCSTACK
 */
LUA_API int lua_checkstack(lua_State *L, int size)
{
    int res = 1;
    lua_lock(L);
    if (size > LUAI_MAXCSTACK || (L->top - L->base + size) > LUAI_MAXCSTACK)
        res = 0;
    else if (size > 0) {
        luaD_checkstack(L, size);
        if (L->ci->top < L->top + size)
            L->ci->top = L->top + size;
    }
    lua_unlock(L);
    return res;
}


/**
 * @brief 在两个Lua状态机之间移动栈元素
 *
 * 详细说明：
 * 将指定数量的栈元素从一个Lua状态机移动到另一个状态机。
 * 这个函数主要用于协程之间的数据传递和线程间通信。
 *
 * 移动过程：
 * 1. 从源状态机的栈顶取出n个元素
 * 2. 将这些元素复制到目标状态机的栈顶
 * 3. 调整两个状态机的栈顶指针
 * 4. 保持对象的类型和值完整性
 *
 * 安全检查：
 * - 检查源状态机有足够的元素可移动
 * - 验证两个状态机属于同一个全局状态
 * - 确保目标状态机有足够的栈空间
 * - 防止在相同状态机间进行无意义的移动
 *
 * 使用场景：
 * - 协程间的参数传递
 * - 协程的返回值传递
 * - 线程间的数据共享
 * - 状态机的数据迁移
 *
 * 性能考虑：
 * - O(n)时间复杂度，n为移动的元素数量
 * - 使用setobj2s进行高效的对象复制
 * - 最小化锁的持有时间
 *
 * @param from 源Lua状态机指针
 * @param to 目标Lua状态机指针
 * @param n 要移动的元素数量
 *
 * @note 两个状态机必须属于同一个全局状态
 * @note 移动后源状态机的栈顶会下降n个位置
 * @warning 不检查目标状态机的栈空间，调用者需确保安全
 *
 * @since C89
 * @see lua_newthread, setobj2s, api_checknelems
 */
LUA_API void lua_xmove(lua_State *from, lua_State *to, int n)
{
    int i;
    if (from == to) return;
    lua_lock(to);
    api_checknelems(from, n);
    api_check(from, G(from) == G(to));
    api_check(from, to->ci->top - to->top >= n);
    from->top -= n;
    for (i = 0; i < n; i++) {
        setobj2s(to, to->top++, from->top + i);
    }
    lua_unlock(to);
}

/**
 * @brief 设置状态机的C调用层级
 *
 * 详细说明：
 * 将目标状态机的C调用嵌套层级设置为与源状态机相同。
 * 这主要用于协程的创建和恢复，确保调用栈的正确性。
 *
 * C调用层级的作用：
 * - 跟踪C函数的嵌套调用深度
 * - 防止C栈溢出
 * - 支持协程的正确挂起和恢复
 * - 维护调用上下文的一致性
 *
 * 使用场景：
 * - 协程的创建和初始化
 * - 协程的恢复操作
 * - 状态机的上下文同步
 *
 * @param from 源Lua状态机指针
 * @param to 目标Lua状态机指针
 *
 * @note 这是一个简单的赋值操作，无需锁保护
 * @since C89
 * @see lua_newthread, lua_resume
 */
LUA_API void lua_setlevel(lua_State *from, lua_State *to)
{
    to->nCcalls = from->nCcalls;
}

/**
 * @brief 设置panic函数
 *
 * 详细说明：
 * 设置当Lua遇到无法处理的错误时调用的panic函数。
 * panic函数是最后的错误处理机制，用于处理无保护的错误。
 *
 * panic函数的特征：
 * - 在无保护的错误发生时被调用
 * - 通常用于记录错误信息和清理资源
 * - 可以选择终止程序或进行其他处理
 * - 不应该返回到Lua代码中
 *
 * 使用场景：
 * - 设置全局错误处理策略
 * - 实现自定义的错误记录
 * - 集成应用程序的错误处理系统
 * - 调试和诊断支持
 *
 * 默认行为：
 * - 如果没有设置panic函数，Lua会调用exit()
 * - panic函数应该处理错误并决定程序的后续行为
 *
 * @param L Lua状态机指针
 * @param panicf 新的panic函数指针，可以为NULL
 * @return 之前设置的panic函数指针
 *
 * @note panic函数在无保护的上下文中被调用
 * @note 设置为NULL会恢复默认的panic行为
 * @warning panic函数不应该进行可能失败的操作
 *
 * @since C89
 * @see lua_CFunction, luaG_errormsg
 */
LUA_API lua_CFunction lua_atpanic(lua_State *L, lua_CFunction panicf)
{
    lua_CFunction old;
    lua_lock(L);
    old = G(L)->panic;
    G(L)->panic = panicf;
    lua_unlock(L);
    return old;
}


/**
 * @brief 创建新的协程线程
 *
 * 详细说明：
 * 创建一个新的Lua协程线程，该线程与主线程共享全局状态但拥有独立的栈。
 * 新线程被推入当前线程的栈顶，可用于实现协程和轻量级并发。
 *
 * 创建过程：
 * 1. 检查垃圾回收，确保有足够内存
 * 2. 调用luaE_newthread创建新的线程状态
 * 3. 将新线程作为thread类型值推入栈
 * 4. 调用用户状态钩子进行初始化
 *
 * 内存管理：
 * - 新线程由垃圾回收器管理
 * - 与主线程共享全局状态和内存分配器
 * - 拥有独立的栈空间和调用信息
 *
 * 协程特性：
 * - 支持yield和resume操作
 * - 独立的执行上下文
 * - 共享全局变量和函数
 * - 轻量级的创建和销毁
 *
 * @param L 父Lua状态机指针
 * @return 新创建的协程状态机指针
 *
 * @note 新线程会被推入父线程的栈顶
 * @note 新线程初始状态为挂起，需要通过resume启动
 * @warning 新线程与父线程共享全局状态，需注意并发安全
 *
 * @since C89
 * @see lua_resume, lua_yield, luaE_newthread
 */
LUA_API lua_State *lua_newthread(lua_State *L)
{
    lua_State *L1;
    lua_lock(L);
    luaC_checkGC(L);
    L1 = luaE_newthread(L);
    setthvalue(L, L->top, L1);
    api_incr_top(L);
    lua_unlock(L);
    luai_userstatethread(L, L1);
    return L1;
}

/**
 * @name 基础栈操作
 * @brief 栈的基本操作函数
 * @{
 */

/**
 * @brief 获取栈中元素的数量
 *
 * 详细说明：
 * 返回当前栈中元素的数量，即栈顶的索引值。这是一个O(1)的操作，
 * 通过简单的指针算术计算得出。
 *
 * 计算方法：
 * - 栈顶指针减去栈底指针
 * - 结果即为栈中元素的数量
 * - 空栈返回0，有n个元素返回n
 *
 * 使用场景：
 * - 检查函数参数数量
 * - 验证栈状态
 * - 实现栈平衡检查
 * - 调试和诊断
 *
 * 性能特征：
 * - O(1)时间复杂度
 * - 无内存分配
 * - 线程安全（只读操作）
 *
 * @param L Lua状态机指针
 * @return 栈中元素的数量（0表示空栈）
 *
 * @note 返回值总是非负数
 * @note 这是一个只读操作，不修改栈状态
 *
 * @since C89
 * @see lua_settop, lua_checkstack
 */
LUA_API int lua_gettop(lua_State *L)
{
    return cast_int(L->top - L->base);
}

/**
 * @brief 设置栈顶位置
 *
 * 详细说明：
 * 设置栈顶到指定的索引位置。可以用于扩展栈（填充nil）或收缩栈。
 * 这是一个强大的栈管理函数，需要谨慎使用。
 *
 * 索引处理：
 * - 正索引：设置栈顶到绝对位置，不足部分填充nil
 * - 负索引：相对于当前栈顶进行调整
 * - 0：清空栈
 *
 * 栈扩展：
 * - 当目标位置大于当前栈顶时，用nil填充中间位置
 * - 确保栈的连续性和一致性
 * - 检查不超出栈的最大容量
 *
 * 栈收缩：
 * - 当目标位置小于当前栈顶时，丢弃多余元素
 * - 被丢弃的元素可能被垃圾回收
 * - 保持剩余元素的完整性
 *
 * 安全检查：
 * - 正索引不超出栈的最大容量
 * - 负索引不超出当前栈的范围
 * - 防止栈指针越界
 *
 * @param L Lua状态机指针
 * @param idx 目标栈顶位置（正数为绝对位置，负数为相对位置）
 *
 * @note idx为0时清空栈
 * @note 扩展栈时新位置填充nil值
 * @warning 收缩栈会丢失数据，使用前确保不需要被丢弃的元素
 *
 * @since C89
 * @see lua_gettop, lua_pop, setnilvalue
 */
LUA_API void lua_settop(lua_State *L, int idx)
{
    lua_lock(L);
    if (idx >= 0) {
        api_check(L, idx <= L->stack_last - L->base);
        while (L->top < L->base + idx)
            setnilvalue(L->top++);
        L->top = L->base + idx;
    }
    else {
        api_check(L, -(idx+1) <= (L->top - L->base));
        L->top += idx+1;
    }
    lua_unlock(L);
}

/**
 * @brief 从栈中移除指定位置的元素
 *
 * 详细说明：
 * 移除栈中指定索引位置的元素，并将该位置之上的所有元素向下移动一位。
 * 这个操作保持栈的连续性，但会改变上层元素的索引。
 *
 * 移除过程：
 * 1. 将指定位置转换为内部地址
 * 2. 验证索引的有效性
 * 3. 将上层元素逐个向下移动
 * 4. 调整栈顶指针
 *
 * 性能考虑：
 * - 时间复杂度O(n)，n为移除位置之上的元素数量
 * - 需要移动内存中的对象
 * - 对于栈顶元素，性能最优
 *
 * 索引影响：
 * - 移除位置之上的元素索引都会减1
 * - 移除位置之下的元素索引不变
 * - 栈顶索引减1
 *
 * 使用场景：
 * - 清理临时变量
 * - 调整函数参数
 * - 实现栈操作算法
 *
 * @param L Lua状态机指针
 * @param idx 要移除的元素索引
 *
 * @note 移除后上层元素的索引会发生变化
 * @note 被移除的元素可能被垃圾回收
 * @warning 确保索引有效，无效索引会导致断言失败
 *
 * @since C89
 * @see lua_insert, lua_replace, setobjs2s
 */
LUA_API void lua_remove(lua_State *L, int idx)
{
    StkId p;
    lua_lock(L);
    p = index2adr(L, idx);
    api_checkvalidindex(L, p);
    while (++p < L->top) setobjs2s(L, p-1, p);
    L->top--;
    lua_unlock(L);
}


/**
 * @brief 将栈顶元素插入到指定位置
 *
 * 详细说明：
 * 将栈顶元素移动到指定的索引位置，并将该位置及其之上的所有元素
 * 向上移动一位。栈顶位置保持不变，但栈顶元素被移动到了新位置。
 *
 * 插入过程：
 * 1. 将指定位置及其之上的元素向上移动
 * 2. 将栈顶元素复制到指定位置
 * 3. 保持栈的大小不变
 *
 * 性能考虑：
 * - 时间复杂度O(n)，n为插入位置之上的元素数量
 * - 需要移动内存中的对象
 * - 对于栈顶位置，操作为空操作
 *
 * 索引影响：
 * - 插入位置之上的元素索引都会增1
 * - 插入位置之下的元素索引不变
 * - 栈大小保持不变
 *
 * 使用场景：
 * - 调整函数参数顺序
 * - 实现栈操作算法
 * - 重新组织栈中的数据
 *
 * @param L Lua状态机指针
 * @param idx 插入位置的索引
 *
 * @note 栈顶元素被移动到指定位置，栈大小不变
 * @note 插入位置之上的元素索引会增加
 * @warning 确保索引有效，无效索引会导致断言失败
 *
 * @since C89
 * @see lua_remove, lua_replace, setobjs2s
 */
LUA_API void lua_insert(lua_State *L, int idx)
{
    StkId p;
    StkId q;
    lua_lock(L);
    p = index2adr(L, idx);
    api_checkvalidindex(L, p);
    for (q = L->top; q>p; q--) setobjs2s(L, q, q-1);
    setobjs2s(L, p, L->top);
    lua_unlock(L);
}

/**
 * @brief 用栈顶元素替换指定位置的元素
 *
 * 详细说明：
 * 用栈顶元素替换指定索引位置的元素，然后弹出栈顶元素。
 * 这个操作会减少栈的大小，并可能触发垃圾回收屏障。
 *
 * 特殊处理：
 * - LUA_ENVIRONINDEX：替换当前函数的环境表
 * - 上值索引：替换函数的上值，需要GC屏障
 * - 普通索引：直接替换对象
 *
 * 垃圾回收屏障：
 * - 环境表替换时设置屏障
 * - 上值替换时设置屏障
 * - 确保引用关系的正确性
 *
 * 错误检查：
 * - 检查栈中至少有一个元素
 * - 验证目标索引的有效性
 * - 环境表必须是table类型
 * - 顶层调用不能设置环境
 *
 * 安全性：
 * - 自动处理内存管理
 * - 维护对象引用的一致性
 * - 防止悬挂指针
 *
 * @param L Lua状态机指针
 * @param idx 要替换的位置索引
 *
 * @note 栈顶元素被弹出，栈大小减1
 * @note 环境表替换需要table类型
 * @warning 顶层调用不能替换环境表
 *
 * @since C89
 * @see lua_insert, lua_remove, luaC_barrier
 */
LUA_API void lua_replace(lua_State *L, int idx)
{
    StkId o;
    lua_lock(L);
    if (idx == LUA_ENVIRONINDEX && L->ci == L->base_ci)
        luaG_runerror(L, "no calling environment");
    api_checknelems(L, 1);
    o = index2adr(L, idx);
    api_checkvalidindex(L, o);
    if (idx == LUA_ENVIRONINDEX) {
        Closure *func = curr_func(L);
        api_check(L, ttistable(L->top - 1));
        func->c.env = hvalue(L->top - 1);
        luaC_barrier(L, func, L->top - 1);
    }
    else {
        setobj(L, o, L->top - 1);
        if (idx < LUA_GLOBALSINDEX)
            luaC_barrier(L, curr_func(L), L->top - 1);
    }
    L->top--;
    lua_unlock(L);
}

/**
 * @brief 将指定位置的元素复制到栈顶
 *
 * 详细说明：
 * 将指定索引位置的元素复制一份并推入栈顶。原位置的元素保持不变，
 * 栈的大小增加1。这是一个安全的复制操作。
 *
 * 复制过程：
 * 1. 获取指定位置的元素
 * 2. 将元素复制到栈顶
 * 3. 增加栈顶指针
 * 4. 保持原元素不变
 *
 * 内存管理：
 * - 正确处理对象的引用计数
 * - 复制对象的类型和值
 * - 不影响原对象的生命周期
 *
 * 性能特征：
 * - O(1)时间复杂度
 * - 最小的内存开销
 * - 高效的对象复制
 *
 * 使用场景：
 * - 复制重要的值
 * - 实现栈操作算法
 * - 保存临时结果
 * - 函数参数准备
 *
 * @param L Lua状态机指针
 * @param idx 要复制的元素索引
 *
 * @note 原位置的元素保持不变
 * @note 栈大小增加1
 * @note 复制的是值，不是引用
 *
 * @since C89
 * @see setobj2s, api_incr_top, index2adr
 */
LUA_API void lua_pushvalue(lua_State *L, int idx)
{
    lua_lock(L);
    setobj2s(L, L->top, index2adr(L, idx));
    api_incr_top(L);
    lua_unlock(L);
}

/** @} */



/**
 * @name 数据访问函数（栈到C）
 * @brief 从Lua栈中获取数据到C代码的函数
 * @{
 */

/**
 * @brief 获取指定位置元素的类型
 *
 * 详细说明：
 * 返回栈中指定索引位置元素的类型标识符。这是Lua类型系统的核心函数，
 * 用于运行时类型检查和类型安全的操作。
 *
 * 类型系统：
 * - LUA_TNONE：无效索引，没有值
 * - LUA_TNIL：nil值
 * - LUA_TBOOLEAN：布尔值
 * - LUA_TLIGHTUSERDATA：轻量用户数据
 * - LUA_TNUMBER：数字
 * - LUA_TSTRING：字符串
 * - LUA_TTABLE：表
 * - LUA_TFUNCTION：函数
 * - LUA_TUSERDATA：完整用户数据
 * - LUA_TTHREAD：线程（协程）
 *
 * 实现原理：
 * - 通过index2adr获取元素地址
 * - 检查是否为无效索引
 * - 使用ttype宏提取类型标记
 * - 返回标准化的类型常量
 *
 * 性能特征：
 * - O(1)时间复杂度
 * - 无内存分配
 * - 高效的位操作
 * - 缓存友好的访问模式
 *
 * 使用场景：
 * - 函数参数类型检查
 * - 条件分支的类型判断
 * - 调试和错误报告
 * - 泛型算法的类型分发
 *
 * @param L Lua状态机指针
 * @param idx 要检查的元素索引
 * @return 类型标识符（LUA_T*常量）
 *
 * @note 无效索引返回LUA_TNONE
 * @note 这是一个只读操作，不修改栈
 * @note 类型检查是类型安全编程的基础
 *
 * @since C89
 * @see lua_typename, ttype, luaO_nilobject
 */
LUA_API int lua_type(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    return (o == luaO_nilobject) ? LUA_TNONE : ttype(o);
}

/**
 * @brief 获取类型的名称字符串
 *
 * 详细说明：
 * 将类型标识符转换为可读的类型名称字符串。这个函数主要用于
 * 错误报告、调试输出和用户界面显示。
 *
 * 类型名称映射：
 * - LUA_TNONE → "no value"
 * - LUA_TNIL → "nil"
 * - LUA_TBOOLEAN → "boolean"
 * - LUA_TLIGHTUSERDATA → "userdata"
 * - LUA_TNUMBER → "number"
 * - LUA_TSTRING → "string"
 * - LUA_TTABLE → "table"
 * - LUA_TFUNCTION → "function"
 * - LUA_TUSERDATA → "userdata"
 * - LUA_TTHREAD → "thread"
 *
 * 实现细节：
 * - 使用预定义的类型名称数组
 * - 特殊处理LUA_TNONE类型
 * - 返回静态字符串，无需释放
 * - 线程安全的只读操作
 *
 * 性能特征：
 * - O(1)时间复杂度
 * - 无内存分配
 * - 返回静态字符串
 * - 高效的数组访问
 *
 * 使用场景：
 * - 错误消息生成
 * - 调试信息输出
 * - 类型信息显示
 * - 日志记录
 *
 * @param L Lua状态机指针（未使用）
 * @param t 类型标识符
 * @return 类型名称的C字符串
 *
 * @note 返回的字符串是静态的，不需要释放
 * @note 无效类型标识符可能导致未定义行为
 * @warning 不要修改返回的字符串
 *
 * @since C89
 * @see lua_type, luaT_typenames
 */
LUA_API const char *lua_typename(lua_State *L, int t)
{
    UNUSED(L);
    return (t == LUA_TNONE) ? "no value" : luaT_typenames[t];
}

/**
 * @brief 检查指定位置是否为C函数
 *
 * 详细说明：
 * 检查栈中指定索引位置的元素是否为C函数。C函数是用C语言编写
 * 并注册到Lua中的函数，与Lua函数有不同的调用约定和实现方式。
 *
 * 检查逻辑：
 * - 首先检查是否为函数类型
 * - 然后检查是否为C函数（非Lua函数）
 * - 使用iscfunction宏进行类型判断
 *
 * C函数特征：
 * - 用C语言实现
 * - 遵循lua_CFunction签名
 * - 可以访问C运行时
 * - 性能通常更高
 *
 * @param L Lua状态机指针
 * @param idx 要检查的元素索引
 * @return 如果是C函数返回非零值，否则返回0
 *
 * @note 只有函数类型才可能是C函数
 * @note Lua函数和C函数有不同的内部表示
 *
 * @since C89
 * @see iscfunction, lua_CFunction
 */
LUA_API int lua_iscfunction(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    return iscfunction(o);
}

/**
 * @brief 检查指定位置是否为数字或可转换为数字
 *
 * 详细说明：
 * 检查栈中指定索引位置的元素是否为数字，或者是否可以转换为数字。
 * 这包括数字类型和可以解析为数字的字符串。
 *
 * 检查逻辑：
 * - 使用tonumber函数尝试转换
 * - 如果转换成功，则认为是数字
 * - 支持数字字面量和数字字符串
 *
 * 支持的格式：
 * - 整数：123, -456
 * - 浮点数：3.14, -2.5
 * - 科学记数法：1e10, 2.5e-3
 * - 十六进制：0xff, 0X1A
 *
 * 性能考虑：
 * - 需要进行实际的转换尝试
 * - 字符串解析有一定开销
 * - 数字类型检查很快
 *
 * @param L Lua状态机指针
 * @param idx 要检查的元素索引
 * @return 如果是数字或可转换为数字返回非零值，否则返回0
 *
 * @note 这个函数会尝试实际转换，有一定性能开销
 * @note 转换失败不会修改原值
 *
 * @since C89
 * @see lua_tonumber, tonumber
 */
LUA_API int lua_isnumber(lua_State *L, int idx)
{
    TValue n;
    const TValue *o = index2adr(L, idx);
    return tonumber(o, &n);
}

/**
 * @brief 检查指定位置是否为字符串或数字
 *
 * 详细说明：
 * 检查栈中指定索引位置的元素是否为字符串类型或数字类型。
 * 在Lua中，数字可以自动转换为字符串，因此这两种类型都被认为是"字符串"。
 *
 * 检查逻辑：
 * - 获取元素的类型
 * - 检查是否为LUA_TSTRING或LUA_TNUMBER
 * - 两种类型都可以用作字符串
 *
 * 自动转换：
 * - 数字在需要时自动转换为字符串
 * - 字符串操作可以接受数字参数
 * - 保持类型系统的灵活性
 *
 * 使用场景：
 * - 字符串操作的参数检查
 * - 输出函数的类型验证
 * - 模板和格式化系统
 *
 * @param L Lua状态机指针
 * @param idx 要检查的元素索引
 * @return 如果是字符串或数字返回非零值，否则返回0
 *
 * @note 数字类型也被认为是字符串类型
 * @note 这反映了Lua的自动类型转换特性
 *
 * @since C89
 * @see lua_tostring, lua_type
 */
LUA_API int lua_isstring(lua_State *L, int idx)
{
    int t = lua_type(L, idx);
    return (t == LUA_TSTRING || t == LUA_TNUMBER);
}

/**
 * @brief 检查指定位置是否为用户数据
 *
 * 详细说明：
 * 检查栈中指定索引位置的元素是否为用户数据类型。
 * 这包括完整用户数据和轻量用户数据两种类型。
 *
 * 用户数据类型：
 * - 完整用户数据：由Lua管理的C数据，支持元表和垃圾回收
 * - 轻量用户数据：简单的C指针，不支持元表，不被垃圾回收
 *
 * 检查逻辑：
 * - 使用ttisuserdata检查完整用户数据
 * - 使用ttislightuserdata检查轻量用户数据
 * - 两种类型都被认为是用户数据
 *
 * 使用场景：
 * - C库的参数类型检查
 * - 对象绑定系统
 * - 资源管理
 *
 * @param L Lua状态机指针
 * @param idx 要检查的元素索引
 * @return 如果是用户数据返回非零值，否则返回0
 *
 * @note 包括完整用户数据和轻量用户数据
 * @note 两种用户数据有不同的特性和用途
 *
 * @since C89
 * @see lua_newuserdata, lua_pushlightuserdata
 */
LUA_API int lua_isuserdata(lua_State *L, int idx)
{
    const TValue *o = index2adr(L, idx);
    return (ttisuserdata(o) || ttislightuserdata(o));
}

/**
 * @brief 原始相等比较
 *
 * 详细说明：
 * 比较两个栈位置的元素是否原始相等，不调用任何元方法。
 * 这是最基础的相等性检查，直接比较对象的值和类型。
 *
 * 比较规则：
 * - 类型必须相同
 * - 值必须完全相等
 * - 不调用__eq元方法
 * - 不进行任何类型转换
 *
 * 性能特征：
 * - O(1)时间复杂度（对于大多数类型）
 * - 字符串比较可能是O(n)
 * - 无元方法调用开销
 * - 高效的位级比较
 *
 * 使用场景：
 * - 需要精确相等性的场合
 * - 避免元方法副作用
 * - 性能敏感的比较操作
 * - 内部算法实现
 *
 * @param L Lua状态机指针
 * @param index1 第一个元素的索引
 * @param index2 第二个元素的索引
 * @return 如果原始相等返回非零值，否则返回0
 *
 * @note 不调用__eq元方法
 * @note 无效索引被认为不相等
 * @note 这是最严格的相等性检查
 *
 * @since C89
 * @see lua_equal, luaO_rawequalObj
 */
LUA_API int lua_rawequal(lua_State *L, int index1, int index2)
{
    StkId o1 = index2adr(L, index1);
    StkId o2 = index2adr(L, index2);
    return (o1 == luaO_nilobject || o2 == luaO_nilobject) ? 0
           : luaO_rawequalObj(o1, o2);
}

/**
 * @brief 相等比较（支持元方法）
 *
 * 详细说明：
 * 比较两个栈位置的元素是否相等，如果对象有__eq元方法，
 * 会调用元方法进行比较。这是Lua中标准的相等性检查。
 *
 * 比较过程：
 * 1. 首先进行原始相等性检查
 * 2. 如果不相等，检查是否有__eq元方法
 * 3. 如果有元方法，调用元方法进行比较
 * 4. 返回比较结果
 *
 * 元方法调用：
 * - 可能触发Lua代码执行
 * - 可能产生副作用
 * - 可能抛出错误
 * - 需要锁保护
 *
 * 性能考虑：
 * - 原始比较很快
 * - 元方法调用有开销
 * - 可能触发垃圾回收
 *
 * @param L Lua状态机指针
 * @param index1 第一个元素的索引
 * @param index2 第二个元素的索引
 * @return 如果相等返回非零值，否则返回0
 *
 * @note 可能调用__eq元方法
 * @note 元方法调用可能产生副作用
 * @warning 元方法可能抛出错误
 *
 * @since C89
 * @see lua_rawequal, equalobj
 */
LUA_API int lua_equal(lua_State *L, int index1, int index2)
{
    StkId o1, o2;
    int i;
    lua_lock(L);
    o1 = index2adr(L, index1);
    o2 = index2adr(L, index2);
    i = (o1 == luaO_nilobject || o2 == luaO_nilobject) ? 0 : equalobj(L, o1, o2);
    lua_unlock(L);
    return i;
}

/**
 * @brief 小于比较（支持元方法）
 *
 * 详细说明：
 * 比较第一个元素是否小于第二个元素，如果对象有__lt元方法，
 * 会调用元方法进行比较。这是Lua中标准的大小比较。
 *
 * 比较过程：
 * 1. 检查两个对象的类型
 * 2. 对于数字和字符串，进行直接比较
 * 3. 对于其他类型，查找__lt元方法
 * 4. 调用元方法或报告错误
 *
 * 支持的类型：
 * - 数字：数值大小比较
 * - 字符串：字典序比较
 * - 其他类型：通过__lt元方法
 *
 * 元方法调用：
 * - 可能触发Lua代码执行
 * - 可能产生副作用
 * - 可能抛出错误
 * - 需要锁保护
 *
 * @param L Lua状态机指针
 * @param index1 第一个元素的索引
 * @param index2 第二个元素的索引
 * @return 如果第一个小于第二个返回非零值，否则返回0
 *
 * @note 可能调用__lt元方法
 * @note 元方法调用可能产生副作用
 * @warning 元方法可能抛出错误
 *
 * @since C89
 * @see luaV_lessthan, lua_equal
 */
LUA_API int lua_lessthan(lua_State *L, int index1, int index2)
{
    StkId o1, o2;
    int i;
    lua_lock(L);
    o1 = index2adr(L, index1);
    o2 = index2adr(L, index2);
    i = (o1 == luaO_nilobject || o2 == luaO_nilobject) ? 0
         : luaV_lessthan(L, o1, o2);
    lua_unlock(L);
    return i;
}



/**
 * @name 数据转换函数
 * @brief 将栈中的值转换为C类型的函数
 * @{
 */

/**
 * @brief 将栈中的值转换为数字
 *
 * 详细说明：
 * 将指定索引位置的值转换为lua_Number类型。如果值不是数字且不能
 * 转换为数字，则返回0。这是一个安全的转换函数。
 *
 * 转换规则：
 * - 数字类型：直接返回数值
 * - 字符串类型：尝试解析为数字
 * - 其他类型：返回0
 *
 * 支持的数字格式：
 * - 整数：123, -456
 * - 浮点数：3.14, -2.5
 * - 科学记数法：1e10, 2.5e-3
 * - 十六进制：0xff, 0X1A
 *
 * 错误处理：
 * - 转换失败时返回0
 * - 不修改原始值
 * - 不抛出错误
 * - 提供默认值语义
 *
 * 性能考虑：
 * - 数字类型转换很快
 * - 字符串解析有一定开销
 * - 无内存分配
 *
 * @param L Lua状态机指针
 * @param idx 要转换的值的索引
 * @return 转换后的数字值，失败时返回0
 *
 * @note 转换失败时返回0，而不是抛出错误
 * @note 不修改栈中的原始值
 * @note 这是一个只读操作
 *
 * @since C89
 * @see lua_isnumber, tonumber, nvalue
 */
LUA_API lua_Number lua_tonumber(lua_State *L, int idx)
{
    TValue n;
    const TValue *o = index2adr(L, idx);
    if (tonumber(o, &n))
        return nvalue(o);
    else
        return 0;
}

/**
 * @brief 将栈中的值转换为整数
 *
 * 详细说明：
 * 将指定索引位置的值转换为lua_Integer类型。首先尝试转换为数字，
 * 然后将数字转换为整数。转换失败时返回0。
 *
 * 转换过程：
 * 1. 尝试将值转换为lua_Number
 * 2. 如果成功，将数字转换为整数
 * 3. 使用lua_number2integer进行高效转换
 * 4. 失败时返回0
 *
 * 转换规则：
 * - 整数：直接转换
 * - 浮点数：截断小数部分
 * - 字符串：先解析为数字再转换
 * - 其他类型：返回0
 *
 * 精度考虑：
 * - 浮点数转整数可能丢失精度
 * - 超出整数范围的值行为未定义
 * - 使用平台优化的转换算法
 *
 * 性能优化：
 * - 使用lua_number2integer宏
 * - 在某些平台上有汇编优化
 * - 避免标准库的转换开销
 *
 * @param L Lua状态机指针
 * @param idx 要转换的值的索引
 * @return 转换后的整数值，失败时返回0
 *
 * @note 浮点数转换时会截断小数部分
 * @note 超出整数范围的行为是平台相关的
 * @warning 大数值转换可能导致溢出
 *
 * @since C89
 * @see lua_tonumber, lua_number2integer
 */
LUA_API lua_Integer lua_tointeger(lua_State *L, int idx)
{
    TValue n;
    const TValue *o = index2adr(L, idx);
    if (tonumber(o, &n)) {
        lua_Integer res;
        lua_Number num = nvalue(o);
        lua_number2integer(res, num);
        return res;
    }
    else
        return 0;
}

/**
 * @brief 将栈中的值转换为布尔值
 *
 * 详细说明：
 * 将指定索引位置的值转换为C的int类型布尔值。在Lua中，
 * 只有nil和false被认为是假值，其他所有值都是真值。
 *
 * 真值判断：
 * - nil：假值
 * - false：假值
 * - 其他所有值：真值（包括0、空字符串等）
 *
 * 这与许多其他语言不同：
 * - 数字0是真值
 * - 空字符串是真值
 * - 空表是真值
 * - 只有nil和false是假值
 *
 * 实现细节：
 * - 使用l_isfalse宏进行判断
 * - 返回C风格的布尔值（0或非0）
 * - 高效的位操作实现
 *
 * 性能特征：
 * - O(1)时间复杂度
 * - 无内存分配
 * - 简单的类型检查
 *
 * @param L Lua状态机指针
 * @param idx 要转换的值的索引
 * @return 非零值表示真，0表示假
 *
 * @note 只有nil和false是假值
 * @note 数字0和空字符串都是真值
 * @note 这是Lua特有的真值语义
 *
 * @since C89
 * @see l_isfalse
 */
LUA_API int lua_toboolean(lua_State *L, int idx)
{
    const TValue *o = index2adr(L, idx);
    return !l_isfalse(o);
}

/**
 * @brief 将栈中的值转换为字符串
 *
 * 详细说明：
 * 将指定索引位置的值转换为C字符串。如果值不是字符串，
 * 会尝试进行转换。转换可能会修改栈中的值。
 *
 * 转换规则：
 * - 字符串：直接返回
 * - 数字：转换为字符串表示
 * - 其他类型：转换失败，返回NULL
 *
 * 重要特性：
 * - 可能修改栈中的原始值
 * - 数字会被转换为字符串对象
 * - 转换后的字符串由Lua管理
 * - 可能触发垃圾回收
 *
 * 内存管理：
 * - 返回的字符串由Lua拥有
 * - 不要释放返回的指针
 * - 字符串在GC时可能被回收
 * - 栈重分配可能使指针失效
 *
 * 安全考虑：
 * - 转换可能失败并返回NULL
 * - 栈重分配后需要重新获取地址
 * - 垃圾回收可能在转换过程中触发
 *
 * 长度信息：
 * - 如果len不为NULL，会设置字符串长度
 * - 长度不包括终止的null字符
 * - 支持包含null字符的字符串
 *
 * @param L Lua状态机指针
 * @param idx 要转换的值的索引
 * @param len 如果不为NULL，用于返回字符串长度
 * @return 指向字符串的指针，失败时返回NULL
 *
 * @note 可能修改栈中的原始值
 * @note 返回的指针由Lua拥有，不要释放
 * @warning 栈重分配可能使返回的指针失效
 * @warning 垃圾回收可能回收返回的字符串
 *
 * @since C89
 * @see lua_tostring, luaV_tostring, tsvalue
 */
LUA_API const char *lua_tolstring(lua_State *L, int idx, size_t *len)
{
    StkId o = index2adr(L, idx);
    if (!ttisstring(o)) {
        lua_lock(L);
        if (!luaV_tostring(L, o)) {
            if (len != NULL) *len = 0;
            lua_unlock(L);
            return NULL;
        }
        luaC_checkGC(L);
        o = index2adr(L, idx);
        lua_unlock(L);
    }
    if (len != NULL) *len = tsvalue(o)->len;
    return svalue(o);
}

/** @} */


/**
 * @brief 获取对象的长度
 *
 * 详细说明：
 * 获取栈中指定索引位置对象的长度。不同类型的对象有不同的长度定义，
 * 这个函数提供了统一的长度获取接口。
 *
 * 长度定义：
 * - 字符串：字符串的字节长度（不包括终止符）
 * - 用户数据：用户数据的字节大小
 * - 表：表的数组部分长度（#操作符的结果）
 * - 数字：转换为字符串后的长度
 * - 其他类型：返回0
 *
 * 特殊处理：
 * - 数字类型需要先转换为字符串
 * - 转换过程可能创建新的字符串对象
 * - 需要锁保护以防止并发问题
 * - 可能触发垃圾回收
 *
 * 性能考虑：
 * - 字符串和用户数据：O(1)
 * - 表：O(log n)，取决于数组部分的大小
 * - 数字：需要字符串转换开销
 *
 * 使用场景：
 * - 实现#操作符
 * - 获取字符串长度
 * - 检查用户数据大小
 * - 遍历表的数组部分
 *
 * @param L Lua状态机指针
 * @param idx 要获取长度的对象索引
 * @return 对象的长度，不支持的类型返回0
 *
 * @note 表的长度是数组部分的长度，不包括哈希部分
 * @note 数字转换为字符串可能创建新对象
 * @warning 数字转换可能触发垃圾回收
 *
 * @since C89
 * @see luaH_getn, luaV_tostring, tsvalue
 */
LUA_API size_t lua_objlen(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    switch (ttype(o)) {
        case LUA_TSTRING: return tsvalue(o)->len;
        case LUA_TUSERDATA: return uvalue(o)->len;
        case LUA_TTABLE: return luaH_getn(hvalue(o));
        case LUA_TNUMBER: {
            size_t l;
            lua_lock(L);
            l = (luaV_tostring(L, o) ? tsvalue(o)->len : 0);
            lua_unlock(L);
            return l;
        }
        default: return 0;
    }
}

/**
 * @brief 将栈中的值转换为C函数指针
 *
 * 详细说明：
 * 如果指定索引位置的值是C函数，返回对应的C函数指针；
 * 否则返回NULL。这用于获取注册到Lua中的C函数。
 *
 * 检查过程：
 * 1. 获取指定位置的对象
 * 2. 检查是否为C函数类型
 * 3. 如果是，返回函数指针
 * 4. 否则返回NULL
 *
 * C函数特征：
 * - 遵循lua_CFunction签名
 * - 可以访问C运行时环境
 * - 通过lua_register等函数注册
 * - 与Lua函数有不同的调用约定
 *
 * 使用场景：
 * - 检查函数类型
 * - 获取已注册的C函数
 * - 实现函数调用优化
 * - 调试和诊断
 *
 * @param L Lua状态机指针
 * @param idx 要转换的值的索引
 * @return C函数指针，如果不是C函数则返回NULL
 *
 * @note 只有C函数才会返回非NULL值
 * @note Lua函数会返回NULL
 * @note 返回的函数指针可以直接调用
 *
 * @since C89
 * @see lua_CFunction, iscfunction, clvalue
 */
LUA_API lua_CFunction lua_tocfunction(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    return (!iscfunction(o)) ? NULL : clvalue(o)->c.f;
}

/**
 * @brief 将栈中的值转换为用户数据指针
 *
 * 详细说明：
 * 如果指定索引位置的值是用户数据，返回指向用户数据的指针；
 * 否则返回NULL。支持完整用户数据和轻量用户数据。
 *
 * 用户数据类型：
 * - 完整用户数据：由Lua分配和管理，支持元表和垃圾回收
 * - 轻量用户数据：简单的C指针，不支持元表，不被垃圾回收
 *
 * 指针计算：
 * - 完整用户数据：跳过Udata头部，返回实际数据指针
 * - 轻量用户数据：直接返回存储的指针值
 *
 * 内存布局：
 * - 完整用户数据：[Udata头部][用户数据]
 * - 轻量用户数据：直接存储指针值
 *
 * 使用场景：
 * - 获取C对象指针
 * - 实现对象绑定
 * - 访问C数据结构
 * - 资源管理
 *
 * @param L Lua状态机指针
 * @param idx 要转换的值的索引
 * @return 用户数据指针，如果不是用户数据则返回NULL
 *
 * @note 完整用户数据返回实际数据指针，不是Udata结构指针
 * @note 轻量用户数据直接返回存储的指针值
 * @warning 返回的指针生命周期由用户管理
 *
 * @since C89
 * @see lua_newuserdata, lua_pushlightuserdata, rawuvalue
 */
LUA_API void *lua_touserdata(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    switch (ttype(o)) {
        case LUA_TUSERDATA: return (rawuvalue(o) + 1);
        case LUA_TLIGHTUSERDATA: return pvalue(o);
        default: return NULL;
    }
}

/**
 * @brief 将栈中的值转换为线程状态机指针
 *
 * 详细说明：
 * 如果指定索引位置的值是线程（协程），返回对应的lua_State指针；
 * 否则返回NULL。用于获取协程的状态机。
 *
 * 线程特征：
 * - 每个线程都有独立的栈
 * - 共享全局状态和内存管理器
 * - 支持yield和resume操作
 * - 可以在线程间传递数据
 *
 * 使用场景：
 * - 协程管理
 * - 线程间通信
 * - 状态机操作
 * - 并发控制
 *
 * 安全考虑：
 * - 返回的状态机指针有效性由Lua保证
 * - 线程可能处于不同的执行状态
 * - 需要正确处理线程生命周期
 *
 * @param L Lua状态机指针
 * @param idx 要转换的值的索引
 * @return 线程状态机指针，如果不是线程则返回NULL
 *
 * @note 返回的是协程的lua_State指针
 * @note 可以用于lua_resume等协程操作
 * @warning 确保线程在使用期间保持有效
 *
 * @since C89
 * @see lua_newthread, lua_resume, thvalue
 */
LUA_API lua_State *lua_tothread(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    return (!ttisthread(o)) ? NULL : thvalue(o);
}


/**
 * @brief 将栈中的值转换为通用指针
 *
 * 详细说明：
 * 对于引用类型的值，返回指向内部对象的指针；对于其他类型返回NULL。
 * 这个函数主要用于调试、比较和哈希计算。
 *
 * 支持的类型：
 * - 表：返回Table结构指针
 * - 函数：返回Closure结构指针
 * - 线程：返回lua_State指针
 * - 用户数据：返回用户数据指针
 * - 其他类型：返回NULL
 *
 * 用途和限制：
 * - 主要用于对象身份比较
 * - 可用于实现哈希表
 * - 不应该解引用返回的指针
 * - 指针值在GC后可能失效
 *
 * 安全考虑：
 * - 返回的指针仅用于比较
 * - 不要尝试修改指向的内容
 * - 指针在垃圾回收后可能无效
 * - 不同类型的指针不可比较
 *
 * @param L Lua状态机指针
 * @param idx 要转换的值的索引
 * @return 对象指针，如果不是引用类型则返回NULL
 *
 * @note 返回的指针仅用于身份比较，不要解引用
 * @note 垃圾回收可能使指针失效
 * @warning 不要修改指针指向的内容
 *
 * @since C89
 * @see hvalue, clvalue, thvalue, lua_touserdata
 */
LUA_API const void *lua_topointer(lua_State *L, int idx)
{
    StkId o = index2adr(L, idx);
    switch (ttype(o)) {
        case LUA_TTABLE: return hvalue(o);
        case LUA_TFUNCTION: return clvalue(o);
        case LUA_TTHREAD: return thvalue(o);
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            return lua_touserdata(L, idx);
        default: return NULL;
    }
}

/**
 * @name 推入函数（C到栈）
 * @brief 将C值推入Lua栈的函数
 * @{
 */

/**
 * @brief 推入nil值
 *
 * 详细说明：
 * 将一个nil值推入栈顶。nil是Lua中表示"无值"的特殊值，
 * 用于表示变量未初始化或表中不存在的键。
 *
 * nil的特性：
 * - 表示"无值"或"空值"
 * - 在布尔上下文中为假值
 * - 删除表中的键值对
 * - 函数默认返回值
 *
 * 实现细节：
 * - 使用setnilvalue设置栈顶为nil
 * - 安全地增加栈顶指针
 * - 使用锁保护操作的原子性
 *
 * 性能特征：
 * - O(1)时间复杂度
 * - 无内存分配
 * - 最轻量的推入操作
 *
 * 使用场景：
 * - 初始化变量
 * - 删除表中的键
 * - 函数返回空值
 * - 占位符值
 *
 * @param L Lua状态机指针
 *
 * @note nil是Lua中的特殊值，不同于C的NULL
 * @note 推入nil不会分配任何内存
 *
 * @since C89
 * @see setnilvalue, api_incr_top
 */
LUA_API void lua_pushnil(lua_State *L)
{
    lua_lock(L);
    setnilvalue(L->top);
    api_incr_top(L);
    lua_unlock(L);
}

/**
 * @brief 推入数字值
 *
 * 详细说明：
 * 将一个lua_Number类型的数字推入栈顶。lua_Number通常是double类型，
 * 提供IEEE 754双精度浮点数的精度和范围。
 *
 * 数字特性：
 * - 双精度浮点数（通常64位）
 * - 支持整数和小数
 * - 支持特殊值（NaN、无穷大）
 * - 自动类型转换
 *
 * 实现细节：
 * - 使用setnvalue设置数字值
 * - 直接存储在TValue中
 * - 无需内存分配
 * - 高效的值复制
 *
 * 精度考虑：
 * - 大整数可能丢失精度
 * - 浮点运算的舍入误差
 * - 特殊值的处理
 *
 * @param L Lua状态机指针
 * @param n 要推入的数字值
 *
 * @note 大整数可能丢失精度
 * @note 支持NaN和无穷大等特殊值
 *
 * @since C89
 * @see lua_Number, setnvalue
 */
LUA_API void lua_pushnumber(lua_State *L, lua_Number n)
{
    lua_lock(L);
    setnvalue(L->top, n);
    api_incr_top(L);
    lua_unlock(L);
}

/**
 * @brief 推入整数值
 *
 * 详细说明：
 * 将一个lua_Integer类型的整数推入栈顶。整数会被转换为lua_Number
 * 类型存储，但保持整数的语义。
 *
 * 整数特性：
 * - 通常是ptrdiff_t类型
 * - 转换为double存储
 * - 保持整数语义
 * - 支持完整的整数范围
 *
 * 转换过程：
 * - 使用cast_num进行类型转换
 * - 保持数值的精确性
 * - 在支持范围内无精度损失
 *
 * 性能考虑：
 * - 转换开销很小
 * - 存储效率与数字相同
 * - 运算时可能需要类型检查
 *
 * 使用场景：
 * - 数组索引
 * - 计数器
 * - 标识符
 * - 整数运算
 *
 * @param L Lua状态机指针
 * @param n 要推入的整数值
 *
 * @note 整数转换为浮点数存储
 * @note 在支持范围内保持精确性
 * @warning 超大整数可能丢失精度
 *
 * @since C89
 * @see lua_Integer, cast_num, setnvalue
 */
LUA_API void lua_pushinteger(lua_State *L, lua_Integer n)
{
    lua_lock(L);
    setnvalue(L->top, cast_num(n));
    api_incr_top(L);
    lua_unlock(L);
}

/**
 * @brief 推入指定长度的字符串
 *
 * 详细说明：
 * 将指定长度的字符串推入栈顶。这个函数支持包含null字符的字符串，
 * 因为长度是显式指定的。
 *
 * 字符串特性：
 * - 支持任意字节序列
 * - 可以包含null字符
 * - 自动进行字符串内化
 * - 由垃圾回收器管理
 *
 * 内存管理：
 * - 创建新的字符串对象
 * - 可能触发垃圾回收
 * - 字符串内化和去重
 * - 自动内存管理
 *
 * 实现细节：
 * - 使用luaS_newlstr创建字符串
 * - 检查垃圾回收需求
 * - 设置字符串值到栈顶
 * - 安全地增加栈指针
 *
 * 性能考虑：
 * - 需要内存分配
 * - 字符串复制开销
 * - 可能的GC触发
 * - 字符串内化的查找
 *
 * 安全性：
 * - 复制输入字符串
 * - 不依赖输入指针的生命周期
 * - 自动内存管理
 *
 * @param L Lua状态机指针
 * @param s 指向字符串数据的指针
 * @param len 字符串的字节长度
 *
 * @note 支持包含null字符的字符串
 * @note 会复制字符串内容，不依赖原指针
 * @warning 可能触发垃圾回收
 *
 * @since C89
 * @see luaS_newlstr, setsvalue2s, luaC_checkGC
 */
LUA_API void lua_pushlstring(lua_State *L, const char *s, size_t len)
{
    lua_lock(L);
    luaC_checkGC(L);
    setsvalue2s(L, L->top, luaS_newlstr(L, s, len));
    api_incr_top(L);
    lua_unlock(L);
}


/**
 * @brief 推入C字符串
 *
 * 详细说明：
 * 将一个以null结尾的C字符串推入栈顶。如果字符串指针为NULL，
 * 则推入nil值。这是最常用的字符串推入函数。
 *
 * 空指针处理：
 * - NULL指针被转换为nil值
 * - 提供安全的空指针处理
 * - 避免strlen对NULL的调用
 *
 * 实现细节：
 * - 使用strlen计算字符串长度
 * - 调用lua_pushlstring进行实际推入
 * - 自动处理字符串复制和内化
 *
 * 性能考虑：
 * - 需要计算字符串长度
 * - 对于已知长度的字符串，lua_pushlstring更高效
 * - 字符串复制和内化开销
 *
 * 使用场景：
 * - 推入C字符串字面量
 * - 处理可能为NULL的字符串
 * - 简单的字符串推入操作
 *
 * @param L Lua状态机指针
 * @param s 指向C字符串的指针，可以为NULL
 *
 * @note NULL指针会被转换为nil值
 * @note 字符串必须以null结尾
 * @note 会复制字符串内容
 *
 * @since C89
 * @see lua_pushlstring, lua_pushnil, strlen
 */
LUA_API void lua_pushstring(lua_State *L, const char *s)
{
    if (s == NULL)
        lua_pushnil(L);
    else
        lua_pushlstring(L, s, strlen(s));
}

/**
 * @brief 推入格式化字符串（va_list版本）
 *
 * 详细说明：
 * 使用printf风格的格式字符串和va_list参数列表创建格式化字符串，
 * 并将结果推入栈顶。这是lua_pushfstring的底层实现。
 *
 * 格式化特性：
 * - 支持Lua特定的格式说明符
 * - 自动内存管理
 * - 高效的字符串构建
 * - 类型安全的格式化
 *
 * 支持的格式：
 * - %s：字符串
 * - %d：整数
 * - %f：浮点数
 * - %c：字符
 * - %%：字面量%
 *
 * 内存管理：
 * - 自动分配结果字符串
 * - 可能触发垃圾回收
 * - 字符串自动内化
 * - 返回指针由Lua管理
 *
 * 性能考虑：
 * - 格式化开销
 * - 内存分配
 * - 字符串内化
 * - 可能的GC触发
 *
 * @param L Lua状态机指针
 * @param fmt 格式字符串
 * @param argp 参数列表
 * @return 指向格式化结果字符串的指针
 *
 * @note 返回的指针指向栈顶的字符串
 * @note 可能触发垃圾回收
 * @warning 返回的指针在栈操作后可能失效
 *
 * @since C89
 * @see lua_pushfstring, luaO_pushvfstring
 */
LUA_API const char *lua_pushvfstring(lua_State *L, const char *fmt,
                                     va_list argp)
{
    const char *ret;
    lua_lock(L);
    luaC_checkGC(L);
    ret = luaO_pushvfstring(L, fmt, argp);
    lua_unlock(L);
    return ret;
}

/**
 * @brief 推入格式化字符串
 *
 * 详细说明：
 * 使用printf风格的格式字符串和可变参数创建格式化字符串，
 * 并将结果推入栈顶。这是一个便利函数，广泛用于错误消息和调试输出。
 *
 * 格式化能力：
 * - 类似printf的格式化
 * - 支持Lua特定的类型
 * - 自动类型转换
 * - 安全的格式化操作
 *
 * 常用格式：
 * - "%s"：字符串格式化
 * - "%d"：整数格式化
 * - "%f"：浮点数格式化
 * - 组合格式：复杂的格式化模式
 *
 * 使用场景：
 * - 错误消息生成
 * - 调试信息输出
 * - 动态字符串构建
 * - 日志记录
 *
 * 示例用法：
 * @code
 * lua_pushfstring(L, "error at line %d: %s", line, msg);
 * lua_pushfstring(L, "value = %f", number);
 * @endcode
 *
 * @param L Lua状态机指针
 * @param fmt 格式字符串
 * @param ... 格式化参数
 * @return 指向格式化结果字符串的指针
 *
 * @note 返回的指针指向栈顶的字符串
 * @note 支持printf风格的格式化
 * @warning 返回的指针在栈操作后可能失效
 *
 * @since C89
 * @see lua_pushvfstring, printf格式化
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

/**
 * @brief 推入C闭包
 *
 * 详细说明：
 * 创建一个C闭包并推入栈顶。C闭包是带有上值的C函数，
 * 可以访问创建时栈顶的n个值作为上值。
 *
 * 闭包特性：
 * - C函数与上值的组合
 * - 上值在闭包创建时确定
 * - 支持词法作用域
 * - 高效的数据封装
 *
 * 创建过程：
 * 1. 检查栈中有足够的上值
 * 2. 创建新的C闭包对象
 * 3. 设置C函数指针
 * 4. 复制栈顶n个值作为上值
 * 5. 调整栈顶指针
 * 6. 推入闭包到栈顶
 *
 * 上值管理：
 * - 上值按逆序存储（栈顶的值成为第一个上值）
 * - 上值由垃圾回收器管理
 * - 支持任意类型的上值
 * - 上值在函数调用时可访问
 *
 * 内存管理：
 * - 创建新的Closure对象
 * - 可能触发垃圾回收
 * - 自动设置GC标记
 * - 正确处理引用关系
 *
 * 性能考虑：
 * - 需要内存分配
 * - 上值复制开销
 * - GC对象创建
 * - 环境表设置
 *
 * 使用场景：
 * - 创建带状态的C函数
 * - 实现回调函数
 * - 封装C数据
 * - 实现模块化
 *
 * @param L Lua状态机指针
 * @param fn C函数指针
 * @param n 上值数量（栈顶n个值将成为上值）
 *
 * @note 栈顶n个值会被消耗作为上值
 * @note 上值按逆序存储
 * @warning 确保栈中有足够的元素作为上值
 *
 * @since C89
 * @see lua_CFunction, luaF_newCclosure, lua_upvalueindex
 */
LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n)
{
    Closure *cl;
    lua_lock(L);
    luaC_checkGC(L);
    api_checknelems(L, n);
    cl = luaF_newCclosure(L, n, getcurrenv(L));
    cl->c.f = fn;
    L->top -= n;
    while (n--)
        setobj2n(L, &cl->c.upvalue[n], L->top+n);
    setclvalue(L, L->top, cl);
    lua_assert(iswhite(obj2gco(cl)));
    api_incr_top(L);
    lua_unlock(L);
}

/**
 * @brief 推入布尔值
 *
 * 详细说明：
 * 将一个布尔值推入栈顶。在Lua中，布尔值只有true和false两个值，
 * 与C的非零/零约定略有不同。
 *
 * 布尔值特性：
 * - 只有true和false两个值
 * - true在条件判断中为真
 * - false在条件判断中为假
 * - 与nil不同（nil也是假值）
 *
 * 转换规则：
 * - 非零值转换为true
 * - 零值转换为false
 * - 确保true的内部表示为1
 *
 * 实现细节：
 * - 使用setbvalue设置布尔值
 * - 标准化true为1，false为0
 * - 高效的值设置
 *
 * 性能特征：
 * - O(1)时间复杂度
 * - 无内存分配
 * - 简单的值复制
 *
 * 使用场景：
 * - 条件判断结果
 * - 标志位设置
 * - 逻辑运算结果
 * - 配置选项
 *
 * @param L Lua状态机指针
 * @param b 布尔值（非零为true，零为false）
 *
 * @note 非零值都被转换为true
 * @note Lua的true/false与C的非零/零不完全相同
 *
 * @since C89
 * @see setbvalue
 */
LUA_API void lua_pushboolean(lua_State *L, int b)
{
    lua_lock(L);
    setbvalue(L->top, (b != 0));
    api_incr_top(L);
    lua_unlock(L);
}


/**
 * @brief 推入轻量用户数据
 *
 * 详细说明：
 * 将一个C指针作为轻量用户数据推入栈顶。轻量用户数据是简单的指针值，
 * 不支持元表，也不被垃圾回收器管理。
 *
 * 轻量用户数据特性：
 * - 存储C指针值
 * - 不支持元表
 * - 不被垃圾回收
 * - 占用空间小
 * - 比较基于指针值
 *
 * 与完整用户数据的区别：
 * - 轻量：只存储指针，无额外开销
 * - 完整：有元表支持，被GC管理
 * - 轻量：不能设置元方法
 * - 完整：支持完整的面向对象特性
 *
 * 使用场景：
 * - 传递C对象指针
 * - 简单的标识符
 * - 回调函数的上下文
 * - 不需要GC的C数据
 *
 * 性能特征：
 * - O(1)时间复杂度
 * - 无内存分配
 * - 最小的存储开销
 * - 高效的指针传递
 *
 * 安全考虑：
 * - 指针生命周期由用户管理
 * - 不进行指针有效性检查
 * - 可能出现悬挂指针
 * - 类型安全由用户保证
 *
 * @param L Lua状态机指针
 * @param p 要推入的C指针
 *
 * @note 指针生命周期由用户管理
 * @note 不支持元表和垃圾回收
 * @warning 确保指针在使用期间有效
 *
 * @since C89
 * @see lua_newuserdata, setpvalue
 */
LUA_API void lua_pushlightuserdata(lua_State *L, void *p)
{
    lua_lock(L);
    setpvalue(L->top, p);
    api_incr_top(L);
    lua_unlock(L);
}

/**
 * @brief 推入当前线程
 *
 * 详细说明：
 * 将当前的lua_State作为线程对象推入栈顶，并返回该线程是否为主线程。
 * 这用于线程间通信和协程管理。
 *
 * 线程对象特性：
 * - 代表一个执行上下文
 * - 拥有独立的栈
 * - 共享全局状态
 * - 支持协程操作
 *
 * 主线程检测：
 * - 比较当前线程与全局主线程
 * - 主线程是最初创建的线程
 * - 主线程通常不会被垃圾回收
 *
 * 使用场景：
 * - 协程管理
 * - 线程间通信
 * - 获取当前执行上下文
 * - 实现调度器
 *
 * 返回值意义：
 * - 非零：当前线程是主线程
 * - 零：当前线程是协程
 *
 * @param L Lua状态机指针
 * @return 如果是主线程返回非零值，否则返回0
 *
 * @note 推入的是当前线程本身
 * @note 返回值指示是否为主线程
 *
 * @since C89
 * @see lua_newthread, setthvalue
 */
LUA_API int lua_pushthread(lua_State *L)
{
    lua_lock(L);
    setthvalue(L, L->top, L);
    api_incr_top(L);
    lua_unlock(L);
    return (G(L)->mainthread == L);
}

/** @} */

/**
 * @name 获取函数（Lua到栈）
 * @brief 从Lua对象中获取值并推入栈的函数
 * @{
 */

/**
 * @brief 从表中获取值
 *
 * 详细说明：
 * 使用栈顶的值作为键，从指定索引的表中获取对应的值，
 * 并将结果替换栈顶的键。支持元方法调用。
 *
 * 操作过程：
 * 1. 获取指定位置的表对象
 * 2. 使用栈顶值作为键
 * 3. 调用luaV_gettable进行查找
 * 4. 将结果替换栈顶的键
 *
 * 元方法支持：
 * - 如果表有__index元方法，会被调用
 * - 支持元表链式查找
 * - 可能触发Lua代码执行
 * - 可能产生副作用
 *
 * 查找顺序：
 * 1. 直接在表中查找
 * 2. 如果没找到且有__index元方法，调用元方法
 * 3. 如果__index是表，递归查找
 * 4. 如果__index是函数，调用函数
 *
 * 性能考虑：
 * - 哈希表查找：平均O(1)
 * - 元方法调用：可能较慢
 * - 字符串键：需要哈希计算
 * - 数字键：可能使用数组部分
 *
 * @param L Lua状态机指针
 * @param idx 表的索引位置
 *
 * @note 栈顶的键会被结果值替换
 * @note 可能调用__index元方法
 * @warning 元方法调用可能抛出错误
 *
 * @since C89
 * @see lua_rawget, luaV_gettable
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

/**
 * @brief 从表中获取指定字段
 *
 * 详细说明：
 * 使用给定的字符串作为键，从指定索引的表中获取对应的值，
 * 并将结果推入栈顶。这是lua_gettable的便利版本。
 *
 * 操作过程：
 * 1. 获取指定位置的表对象
 * 2. 将字符串转换为Lua字符串对象
 * 3. 使用字符串作为键进行查找
 * 4. 将结果推入栈顶
 *
 * 字符串处理：
 * - 自动创建Lua字符串对象
 * - 字符串会被内化
 * - 支持字符串键的优化
 *
 * 元方法支持：
 * - 完全支持__index元方法
 * - 与lua_gettable行为一致
 * - 可能触发元方法调用
 *
 * 使用场景：
 * - 访问对象的属性
 * - 获取模块的函数
 * - 读取配置值
 * - 访问全局变量
 *
 * 性能优化：
 * - 字符串键的特殊优化
 * - 避免手动字符串创建
 * - 内化字符串的重用
 *
 * @param L Lua状态机指针
 * @param idx 表的索引位置
 * @param k 字段名（C字符串）
 *
 * @note 结果被推入栈顶，栈大小增加1
 * @note 字符串会被自动内化
 * @note 支持__index元方法
 *
 * @since C89
 * @see lua_gettable, luaS_new
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

/**
 * @brief 原始获取表中的值
 *
 * 详细说明：
 * 使用栈顶的值作为键，从指定索引的表中直接获取对应的值，
 * 不调用任何元方法。这是最基础的表访问操作。
 *
 * 原始访问特性：
 * - 不调用__index元方法
 * - 直接访问表的内容
 * - 性能最优
 * - 行为可预测
 *
 * 操作过程：
 * 1. 获取指定位置的表对象
 * 2. 验证确实是表类型
 * 3. 使用栈顶值作为键直接查找
 * 4. 将结果替换栈顶的键
 *
 * 查找机制：
 * - 直接哈希表查找
 * - 数组部分优化
 * - 无元方法干扰
 * - 确定性行为
 *
 * 使用场景：
 * - 需要避免元方法的场合
 * - 性能敏感的操作
 * - 实现元方法本身
 * - 调试和诊断
 *
 * 性能特征：
 * - 最快的表访问方式
 * - 无函数调用开销
 * - 直接内存访问
 * - 缓存友好
 *
 * @param L Lua状态机指针
 * @param idx 表的索引位置
 *
 * @note 栈顶的键会被结果值替换
 * @note 不调用任何元方法
 * @note 要求目标必须是表类型
 *
 * @since C89
 * @see lua_gettable, luaH_get
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


/**
 * @brief 原始获取表中指定数字索引的值
 *
 * 详细说明：
 * 使用数字索引从表中直接获取值，不调用任何元方法。
 * 这是针对数组部分优化的高效访问方式。
 *
 * 数组访问优化：
 * - 直接访问表的数组部分
 * - 使用luaH_getnum进行优化查找
 * - 避免哈希计算开销
 * - 最高效的数字索引访问
 *
 * 实现细节：
 * - 验证目标是表类型
 * - 使用专门的数字索引查找函数
 * - 将结果推入栈顶
 * - 不调用__index元方法
 *
 * 性能特征：
 * - 数组部分：O(1)访问
 * - 哈希部分：O(1)平均访问
 * - 无元方法调用开销
 * - 缓存友好的访问模式
 *
 * 使用场景：
 * - 数组遍历
 * - 性能敏感的数字索引访问
 * - 避免元方法干扰的场合
 * - 实现数组算法
 *
 * @param L Lua状态机指针
 * @param idx 表的索引位置
 * @param n 数字索引
 *
 * @note 结果被推入栈顶，栈大小增加1
 * @note 不调用任何元方法
 * @note 针对数字索引进行了优化
 *
 * @since C89
 * @see lua_rawget, luaH_getnum
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

/**
 * @brief 创建新表
 *
 * 详细说明：
 * 创建一个新的空表并推入栈顶。可以预先指定数组部分和哈希部分的大小，
 * 以优化内存分配和性能。
 *
 * 预分配优化：
 * - narray：预分配的数组部分大小
 * - nrec：预分配的哈希部分大小
 * - 减少后续的内存重分配
 * - 提高插入性能
 *
 * 内存管理：
 * - 创建新的Table对象
 * - 可能触发垃圾回收
 * - 自动设置GC标记
 * - 由垃圾回收器管理生命周期
 *
 * 性能考虑：
 * - 预分配避免重复扩容
 * - 合理的初始大小提高性能
 * - 过大的预分配浪费内存
 * - 过小的预分配导致频繁扩容
 *
 * 使用场景：
 * - 创建数据结构
 * - 实现对象和模块
 * - 临时数据存储
 * - 配置和参数传递
 *
 * 优化建议：
 * - 根据预期用途设置合理的初始大小
 * - 主要用作数组时设置narray
 * - 主要用作字典时设置nrec
 * - 不确定时使用0让Lua自动管理
 *
 * @param L Lua状态机指针
 * @param narray 预分配的数组部分大小
 * @param nrec 预分配的哈希部分大小
 *
 * @note 新表被推入栈顶
 * @note 预分配大小是提示，不是限制
 * @warning 可能触发垃圾回收
 *
 * @since C89
 * @see lua_newtable, luaH_new, luaC_checkGC
 */
LUA_API void lua_createtable(lua_State *L, int narray, int nrec)
{
    lua_lock(L);
    luaC_checkGC(L);
    sethvalue(L, L->top, luaH_new(L, narray, nrec));
    api_incr_top(L);
    lua_unlock(L);
}

/**
 * @brief 获取对象的元表
 *
 * 详细说明：
 * 获取指定对象的元表并推入栈顶。如果对象没有元表，
 * 则不推入任何值并返回0。
 *
 * 元表查找规则：
 * - 表和用户数据：使用对象自身的元表
 * - 其他类型：使用全局类型元表
 * - 没有元表：返回0，不推入值
 *
 * 支持的类型：
 * - 表：个体元表或类型元表
 * - 用户数据：个体元表或类型元表
 * - 其他类型：全局类型元表
 *
 * 元表的作用：
 * - 定义对象的行为
 * - 实现运算符重载
 * - 提供面向对象特性
 * - 控制访问和修改
 *
 * 返回值意义：
 * - 非零：找到元表，已推入栈
 * - 零：没有元表，栈不变
 *
 * 使用场景：
 * - 检查对象是否有元表
 * - 获取元表进行操作
 * - 实现面向对象系统
 * - 调试和诊断
 *
 * @param L Lua状态机指针
 * @param objindex 对象的索引位置
 * @return 如果有元表返回非零值，否则返回0
 *
 * @note 只有在有元表时才推入栈
 * @note 不同类型的元表查找规则不同
 * @note 返回值指示是否找到元表
 *
 * @since C89
 * @see lua_setmetatable, 元表机制
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


LUA_API void lua_getfenv (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  o = index2adr(L, idx);
  api_checkvalidindex(L, o);
  switch (ttype(o)) {
    case LUA_TFUNCTION:
      sethvalue(L, L->top, clvalue(o)->c.env);
      break;
    case LUA_TUSERDATA:
      sethvalue(L, L->top, uvalue(o)->env);
      break;
    case LUA_TTHREAD:
      setobj2s(L, L->top,  gt(thvalue(o)));
      break;
    default:
      setnilvalue(L->top);
      break;
  }
  api_incr_top(L);
  lua_unlock(L);
}


/*
** set functions (stack -> Lua)
*/


LUA_API void lua_settable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 2);
  t = index2adr(L, idx);
  api_checkvalidindex(L, t);
  luaV_settable(L, t, L->top - 2, L->top - 1);
  L->top -= 2;  /* pop index and value */
  lua_unlock(L);
}


LUA_API void lua_setfield (lua_State *L, int idx, const char *k) {
  StkId t;
  TValue key;
  lua_lock(L);
  api_checknelems(L, 1);
  t = index2adr(L, idx);
  api_checkvalidindex(L, t);
  setsvalue(L, &key, luaS_new(L, k));
  luaV_settable(L, t, &key, L->top - 1);
  L->top--;  /* pop value */
  lua_unlock(L);
}


LUA_API void lua_rawset (lua_State *L, int idx) {
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


LUA_API void lua_rawseti (lua_State *L, int idx, int n) {
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


LUA_API int lua_setmetatable (lua_State *L, int objindex) {
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


LUA_API int lua_setfenv (lua_State *L, int idx) {
  StkId o;
  int res = 1;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2adr(L, idx);
  api_checkvalidindex(L, o);
  api_check(L, ttistable(L->top - 1));
  switch (ttype(o)) {
    case LUA_TFUNCTION:
      clvalue(o)->c.env = hvalue(L->top - 1);
      break;
    case LUA_TUSERDATA:
      uvalue(o)->env = hvalue(L->top - 1);
      break;
    case LUA_TTHREAD:
      sethvalue(L, gt(thvalue(o)), hvalue(L->top - 1));
      break;
    default:
      res = 0;
      break;
  }
  if (res) luaC_objbarrier(L, gcvalue(o), hvalue(L->top - 1));
  L->top--;
  lua_unlock(L);
  return res;
}


/**
 * @name 加载和调用函数（运行Lua代码）
 * @brief 执行Lua代码的核心函数
 * @{
 */

/**
 * @brief 调整函数调用结果的栈状态
 *
 * 详细说明：
 * 当函数返回多个结果（LUA_MULTRET）且栈顶超过当前调用信息的栈顶时，
 * 调整调用信息的栈顶以匹配实际的栈状态。
 *
 * @param L Lua状态机指针
 * @param nres 期望的结果数量
 *
 * @since C89
 * @see lua_call, lua_pcall
 */
#define adjustresults(L,nres) \
    { if (nres == LUA_MULTRET && L->top >= L->ci->top) L->ci->top = L->top; }

/**
 * @brief 检查函数调用的参数和结果空间
 *
 * 详细说明：
 * 验证栈中有足够的空间容纳函数调用的结果。确保调用的安全性。
 *
 * @param L Lua状态机指针
 * @param na 参数数量
 * @param nr 期望的结果数量
 *
 * @since C89
 * @see lua_call, lua_pcall
 */
#define checkresults(L,na,nr) \
     api_check(L, (nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)))

/**
 * @brief 调用Lua函数
 *
 * 详细说明：
 * 调用栈中的函数，传递指定数量的参数，并期望指定数量的返回值。
 * 这是一个无保护的调用，如果函数中发生错误，会通过longjmp传播。
 *
 * 调用过程：
 * 1. 验证栈中有足够的参数和函数
 * 2. 检查结果空间是否足够
 * 3. 定位函数在栈中的位置
 * 4. 调用luaD_call执行函数
 * 5. 调整结果的栈状态
 *
 * 栈布局（调用前）：
 * - 栈底到栈顶：[...][函数][参数1][参数2]...[参数n]
 * - 函数位置：L->top - (nargs + 1)
 *
 * 栈布局（调用后）：
 * - 栈底到栈顶：[...][结果1][结果2]...[结果m]
 * - 函数和参数被结果替换
 *
 * 参数处理：
 * - nargs：参数数量，必须与栈中实际参数匹配
 * - nresults：期望的结果数量
 * - LUA_MULTRET：接受所有返回值
 *
 * 错误处理：
 * - 无保护调用，错误会传播到上层
 * - 使用lua_pcall进行保护调用
 * - 错误会导致longjmp跳转
 *
 * 性能特征：
 * - 直接调用，无错误处理开销
 * - 最快的函数调用方式
 * - 适合确定不会出错的场合
 *
 * 使用场景：
 * - 调用已知安全的函数
 * - 性能敏感的函数调用
 * - 在保护环境中的调用
 * - 实现其他API函数
 *
 * @param L Lua状态机指针
 * @param nargs 参数数量
 * @param nresults 期望的结果数量（LUA_MULTRET表示接受所有结果）
 *
 * @note 这是无保护的调用，错误会传播
 * @note 函数和参数会被结果替换
 * @warning 错误会导致longjmp，确保在保护环境中调用
 *
 * @since C89
 * @see lua_pcall, luaD_call, LUA_MULTRET
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



/**
 * @brief 保护调用的数据结构
 *
 * 详细说明：
 * 用于传递给f_call函数的参数结构，包含要调用的函数位置和期望的结果数量。
 *
 * @since C89
 * @see lua_pcall, f_call
 */
struct CallS {
    StkId func;         /**< 要调用的函数在栈中的位置 */
    int nresults;       /**< 期望的结果数量 */
};

/**
 * @brief 保护调用的内部函数
 *
 * 详细说明：
 * 这是lua_pcall的内部实现函数，在保护环境中执行实际的函数调用。
 * 通过luaD_pcall调用，可以捕获和处理错误。
 *
 * @param L Lua状态机指针
 * @param ud 指向CallS结构的用户数据
 *
 * @since C89
 * @see lua_pcall, luaD_call
 */
static void f_call(lua_State *L, void *ud)
{
    struct CallS *c = cast(struct CallS *, ud);
    luaD_call(L, c->func, c->nresults);
}

/**
 * @brief 保护调用Lua函数
 *
 * 详细说明：
 * 在保护模式下调用Lua函数，可以捕获函数执行过程中的错误。
 * 如果发生错误，不会通过longjmp传播，而是返回错误状态码。
 *
 * 保护机制：
 * - 使用setjmp/longjmp捕获错误
 * - 错误不会传播到调用者
 * - 返回状态码指示调用结果
 * - 可以指定错误处理函数
 *
 * 错误处理函数：
 * - errfunc为0：使用默认错误处理
 * - errfunc非0：调用指定的错误处理函数
 * - 错误处理函数接收错误消息作为参数
 * - 可以修改错误消息或进行清理工作
 *
 * 状态码含义：
 * - 0 (LUA_OK)：调用成功
 * - LUA_ERRRUN：运行时错误
 * - LUA_ERRMEM：内存分配错误
 * - LUA_ERRERR：错误处理函数中的错误
 *
 * 栈状态：
 * - 成功：栈包含函数的返回值
 * - 失败：栈包含错误消息
 * - 函数和参数总是被移除
 *
 * 使用场景：
 * - 调用可能出错的函数
 * - 需要错误处理的场合
 * - 用户输入的代码执行
 * - 插件和扩展的安全调用
 *
 * 性能考虑：
 * - 比lua_call稍慢（保护开销）
 * - 错误处理的额外开销
 * - 推荐用于不确定的函数调用
 *
 * @param L Lua状态机指针
 * @param nargs 参数数量
 * @param nresults 期望的结果数量（LUA_MULTRET表示接受所有结果）
 * @param errfunc 错误处理函数的栈索引（0表示无错误处理函数）
 * @return 状态码（0表示成功，非0表示错误）
 *
 * @note 这是推荐的函数调用方式
 * @note 错误不会传播，通过返回值指示
 * @note 可以指定自定义错误处理函数
 *
 * @since C89
 * @see lua_call, luaD_pcall, 错误处理
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
        func = 0;
    else {
        StkId o = index2adr(L, errfunc);
        api_checkvalidindex(L, o);
        func = savestack(L, o);
    }
    c.func = L->top - (nargs+1);
    c.nresults = nresults;
    status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);
    adjustresults(L, nresults);
    lua_unlock(L);
    return status;
}

/**
 * @brief 保护C调用的数据结构
 *
 * 详细说明：
 * 用于传递给f_Ccall函数的参数结构，包含要调用的C函数和用户数据。
 *
 * @since C89
 * @see lua_cpcall, f_Ccall
 */
struct CCallS {
    lua_CFunction func; /**< 要调用的C函数 */
    void *ud;          /**< 传递给C函数的用户数据 */
};

/**
 * @brief 保护C调用的内部函数
 *
 * 详细说明：
 * 这是lua_cpcall的内部实现函数，在保护环境中执行C函数调用。
 * 创建一个临时的C闭包来包装C函数，然后调用它。
 *
 * 实现过程：
 * 1. 创建一个无上值的C闭包
 * 2. 设置C函数指针
 * 3. 推入闭包到栈
 * 4. 推入用户数据作为参数
 * 5. 调用闭包
 *
 * @param L Lua状态机指针
 * @param ud 指向CCallS结构的用户数据
 *
 * @since C89
 * @see lua_cpcall, luaF_newCclosure
 */
static void f_Ccall(lua_State *L, void *ud)
{
    struct CCallS *c = cast(struct CCallS *, ud);
    Closure *cl;
    cl = luaF_newCclosure(L, 0, getcurrenv(L));
    cl->c.f = c->func;
    setclvalue(L, L->top, cl);
    api_incr_top(L);
    setpvalue(L->top, c->ud);
    api_incr_top(L);
    luaD_call(L, L->top - 2, 0);
}


/**
 * @brief 保护调用C函数
 *
 * 详细说明：
 * 在保护模式下调用C函数，可以捕获C函数执行过程中的错误。
 * C函数接收用户数据作为唯一参数，不返回值。
 *
 * 调用机制：
 * - 创建临时的C闭包包装C函数
 * - 将用户数据作为轻量用户数据传递
 * - 在保护环境中调用闭包
 * - 捕获可能的错误
 *
 * C函数签名：
 * - int (*lua_CFunction)(lua_State *L)
 * - 函数可以通过lua_touserdata(L, 1)获取用户数据
 * - 函数应该返回结果数量（通常为0）
 *
 * 错误处理：
 * - 使用默认错误处理（无自定义错误函数）
 * - 错误消息会被推入栈顶
 * - 返回状态码指示调用结果
 *
 * 使用场景：
 * - 调用可能出错的C函数
 * - 需要传递上下文数据的C函数
 * - 插件和扩展的安全调用
 * - 回调函数的保护执行
 *
 * @param L Lua状态机指针
 * @param func 要调用的C函数
 * @param ud 传递给C函数的用户数据
 * @return 状态码（0表示成功，非0表示错误）
 *
 * @note C函数通过lua_touserdata(L, 1)获取用户数据
 * @note 这是调用C函数的安全方式
 * @note 错误不会传播，通过返回值指示
 *
 * @since C89
 * @see lua_pcall, lua_CFunction, f_Ccall
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

/**
 * @brief 加载Lua代码块
 *
 * 详细说明：
 * 从指定的读取器加载Lua代码，编译为函数并推入栈顶。
 * 这是加载和编译Lua代码的核心函数。
 *
 * 加载过程：
 * 1. 初始化输入流（ZIO）
 * 2. 调用保护解析器编译代码
 * 3. 将编译结果（函数）推入栈
 * 4. 返回编译状态
 *
 * 读取器接口：
 * - const char* (*lua_Reader)(lua_State *L, void *data, size_t *size)
 * - 返回数据块指针和大小
 * - 返回NULL表示输入结束
 * - 可以从文件、内存、网络等读取
 *
 * 代码块名称：
 * - 用于错误报告和调试信息
 * - 通常是文件名或描述性名称
 * - NULL时使用默认名称"?"
 *
 * 编译结果：
 * - 成功：函数被推入栈顶
 * - 失败：错误消息被推入栈顶
 * - 函数可以通过lua_call或lua_pcall调用
 *
 * 状态码：
 * - 0 (LUA_OK)：编译成功
 * - LUA_ERRSYNTAX：语法错误
 * - LUA_ERRMEM：内存不足
 *
 * 使用场景：
 * - 从文件加载Lua脚本
 * - 动态编译Lua代码
 * - 实现require函数
 * - 插件系统的代码加载
 *
 * @param L Lua状态机指针
 * @param reader 读取器函数
 * @param data 传递给读取器的用户数据
 * @param chunkname 代码块名称（用于错误报告）
 * @return 状态码（0表示成功，非0表示错误）
 *
 * @note 成功时函数被推入栈顶
 * @note 失败时错误消息被推入栈顶
 * @note 代码块名称用于调试和错误报告
 *
 * @since C89
 * @see lua_Reader, luaD_protectedparser, ZIO
 */
LUA_API int lua_load(lua_State *L, lua_Reader reader, void *data,
                     const char *chunkname)
{
    ZIO z;
    int status;
    lua_lock(L);
    if (!chunkname) chunkname = "?";
    luaZ_init(L, &z, reader, data);
    status = luaD_protectedparser(L, &z, chunkname);
    lua_unlock(L);
    return status;
}

/**
 * @brief 转储Lua函数为字节码
 *
 * 详细说明：
 * 将栈顶的Lua函数转储为字节码，通过写入器输出。
 * 这用于保存编译后的Lua函数，实现预编译和缓存。
 *
 * 转储过程：
 * 1. 检查栈顶是否为Lua函数
 * 2. 获取函数的原型（Proto）
 * 3. 调用luaU_dump序列化字节码
 * 4. 通过写入器输出数据
 *
 * 写入器接口：
 * - int (*lua_Writer)(lua_State *L, const void *p, size_t sz, void *ud)
 * - 接收数据块指针、大小和用户数据
 * - 返回0表示成功，非0表示错误
 * - 可以写入文件、内存、网络等
 *
 * 支持的函数类型：
 * - 只支持Lua函数（不支持C函数）
 * - 函数必须在栈顶
 * - C函数会导致转储失败
 *
 * 字节码格式：
 * - Lua特定的二进制格式
 * - 包含指令、常量、调试信息
 * - 平台相关（字节序、指针大小）
 * - 版本相关（不同Lua版本不兼容）
 *
 * 使用场景：
 * - 预编译Lua脚本
 * - 缓存编译结果
 * - 代码保护和混淆
 * - 减少启动时间
 *
 * @param L Lua状态机指针
 * @param writer 写入器函数
 * @param data 传递给写入器的用户数据
 * @return 状态码（0表示成功，非0表示错误）
 *
 * @note 只能转储Lua函数，不能转储C函数
 * @note 字节码是平台和版本相关的
 * @note 栈顶必须是Lua函数
 *
 * @since C89
 * @see lua_Writer, lua_load, luaU_dump
 */
LUA_API int lua_dump(lua_State *L, lua_Writer writer, void *data)
{
    int status;
    TValue *o;
    lua_lock(L);
    api_checknelems(L, 1);
    o = L->top - 1;
    if (isLfunction(o))
        status = luaU_dump(L, clvalue(o)->l.p, writer, data, 0);
    else
        status = 1;
    lua_unlock(L);
    return status;
}

/**
 * @brief 获取线程状态
 *
 * 详细说明：
 * 返回线程的当前状态，用于协程管理和错误检测。
 * 状态值指示线程是否正常、挂起或出错。
 *
 * 状态值含义：
 * - 0 (LUA_OK)：正常状态或挂起状态
 * - LUA_YIELD：线程被挂起（yield）
 * - LUA_ERRRUN：运行时错误
 * - LUA_ERRSYNTAX：语法错误
 * - LUA_ERRMEM：内存错误
 * - LUA_ERRERR：错误处理函数中的错误
 *
 * 使用场景：
 * - 协程状态检查
 * - 错误诊断
 * - 线程管理
 * - 调试支持
 *
 * @param L Lua状态机指针
 * @return 线程状态码
 *
 * @note 主线程通常返回0
 * @note 协程可能返回LUA_YIELD
 * @note 错误状态需要适当处理
 *
 * @since C89
 * @see lua_resume, lua_yield, 协程状态
 */
LUA_API int lua_status(lua_State *L)
{
    return L->status;
}


/**
 * @name 垃圾回收函数
 * @brief 控制垃圾回收器的函数
 * @{
 */

/**
 * @brief 垃圾回收控制函数
 *
 * 详细说明：
 * 这是Lua垃圾回收器的统一控制接口，提供启动、停止、配置和查询等功能。
 * 通过不同的操作码可以精确控制垃圾回收的行为和参数。
 *
 * 垃圾回收器特性：
 * - 增量式垃圾回收
 * - 三色标记算法
 * - 自动内存管理
 * - 可配置的回收策略
 *
 * 支持的操作：
 *
 * LUA_GCSTOP：
 * - 停止垃圾回收器
 * - 设置阈值为最大值
 * - 不会触发任何回收
 * - 用于性能敏感的操作
 *
 * LUA_GCRESTART：
 * - 重启垃圾回收器
 * - 设置阈值为当前内存使用量
 * - 恢复正常的回收行为
 * - 与LUA_GCSTOP配对使用
 *
 * LUA_GCCOLLECT：
 * - 执行完整的垃圾回收
 * - 回收所有可回收的对象
 * - 可能耗时较长
 * - 用于内存清理
 *
 * LUA_GCCOUNT：
 * - 返回当前内存使用量（KB）
 * - 值为totalbytes >> 10
 * - 用于内存监控
 * - 不包括C分配的内存
 *
 * LUA_GCCOUNTB：
 * - 返回内存使用量的余数部分
 * - 值为totalbytes & 0x3ff
 * - 与LUA_GCCOUNT组合得到精确值
 * - 精确内存计算：count*1024 + countb
 *
 * LUA_GCSTEP：
 * - 执行增量垃圾回收
 * - data参数指定回收的内存量（KB）
 * - 返回1表示完成一个回收周期
 * - 用于控制回收的粒度
 *
 * LUA_GCSETPAUSE：
 * - 设置回收暂停参数
 * - 控制回收的触发频率
 * - 值越大，回收越不频繁
 * - 返回之前的设置值
 *
 * LUA_GCSETSTEPMUL：
 * - 设置回收步长倍数
 * - 控制每次回收的工作量
 * - 值越大，每次回收越多
 * - 返回之前的设置值
 *
 * 性能调优：
 * - pause参数：控制回收频率，影响内存使用
 * - stepmul参数：控制回收强度，影响CPU使用
 * - 根据应用特性调整参数
 * - 监控内存使用情况
 *
 * 使用场景：
 * - 内存使用监控
 * - 性能优化
 * - 内存泄漏检测
 * - 实时系统的内存控制
 *
 * 注意事项：
 * - 停止GC可能导致内存泄漏
 * - 完整回收可能影响实时性
 * - 参数调整需要测试验证
 * - 不当使用可能降低性能
 *
 * @param L Lua状态机指针
 * @param what 操作码（LUA_GC*常量）
 * @param data 操作参数（某些操作需要）
 * @return 操作结果（依操作而定，-1表示无效操作）
 *
 * @note 不同操作的返回值含义不同
 * @note 内存计数以KB为单位
 * @note 参数调整影响性能和内存使用
 *
 * @since C89
 * @see 垃圾回收器, luaC_fullgc, luaC_step
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
                if (g->gcstate == GCSpause) {
                    res = 1;
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
        default: res = -1;
    }
    lua_unlock(L);
    return res;
}

/** @} */



/**
 * @name 杂项函数
 * @brief 其他实用工具函数
 * @{
 */

/**
 * @brief 抛出错误
 *
 * 详细说明：
 * 使用栈顶的值作为错误消息抛出错误。这个函数不会返回，
 * 而是通过longjmp跳转到最近的错误处理点。
 *
 * 错误处理机制：
 * - 使用栈顶值作为错误消息
 * - 调用luaG_errormsg处理错误
 * - 通过longjmp传播错误
 * - 不会正常返回
 *
 * 错误消息：
 * - 通常是字符串类型
 * - 可以是任何Lua值
 * - 会被传递给错误处理函数
 * - 用于错误报告和调试
 *
 * 使用场景：
 * - C函数中抛出错误
 * - 参数验证失败
 * - 运行时错误报告
 * - 实现assert等函数
 *
 * 注意事项：
 * - 函数不会正常返回
 * - 必须在保护环境中调用
 * - 错误会传播到上层
 * - 可能导致资源泄漏
 *
 * @param L Lua状态机指针
 * @return 永不返回（通过longjmp跳转）
 *
 * @note 使用栈顶值作为错误消息
 * @note 函数不会正常返回
 * @warning 必须在保护环境中调用
 *
 * @since C89
 * @see luaG_errormsg, 错误处理
 */
LUA_API int lua_error(lua_State *L)
{
    lua_lock(L);
    api_checknelems(L, 1);
    luaG_errormsg(L);
    lua_unlock(L);
    return 0;
}

/**
 * @brief 遍历表的下一个元素
 *
 * 详细说明：
 * 用于遍历表中的所有键值对。使用栈顶的键查找下一个键值对，
 * 并将结果推入栈。这是实现pairs()函数的基础。
 *
 * 遍历过程：
 * 1. 使用栈顶的键作为起始点
 * 2. 查找表中的下一个键值对
 * 3. 如果找到，推入键和值
 * 4. 如果没找到，移除键
 *
 * 遍历顺序：
 * - 数组部分：按索引顺序
 * - 哈希部分：按内部存储顺序
 * - 顺序不保证，但一致
 * - 不受表修改影响（快照）
 *
 * 栈操作：
 * - 输入：[表索引] [当前键]
 * - 成功：[表索引] [下一个键] [对应值]
 * - 失败：[表索引]（键被移除）
 *
 * 初始调用：
 * - 推入nil作为初始键
 * - 从表的第一个元素开始
 * - 遍历所有键值对
 *
 * 使用模式：
 * @code
 * lua_pushnil(L);  // 初始键
 * while (lua_next(L, table_index)) {
 *     // 栈：[键] [值]
 *     // 处理键值对
 *     lua_pop(L, 1);  // 移除值，保留键
 * }
 * @endcode
 *
 * 安全考虑：
 * - 遍历期间不要修改表
 * - 确保正确管理栈
 * - 处理遍历中断的情况
 *
 * @param L Lua状态机指针
 * @param idx 表的索引位置
 * @return 如果有下一个元素返回非零值，否则返回0
 *
 * @note 遍历顺序不保证，但一致
 * @note 遍历期间不要修改表
 * @note 正确管理栈是调用者的责任
 *
 * @since C89
 * @see luaH_next, pairs函数
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
    else
        L->top -= 1;
    lua_unlock(L);
    return more;
}

/**
 * @brief 连接栈顶的多个值
 *
 * 详细说明：
 * 将栈顶的n个值连接成一个字符串，并将结果替换这些值。
 * 支持字符串和数字的自动转换，实现Lua的..操作符。
 *
 * 连接规则：
 * - 字符串：直接连接
 * - 数字：转换为字符串后连接
 * - 其他类型：调用__tostring元方法
 * - 结果是单个字符串
 *
 * 特殊情况：
 * - n=0：推入空字符串
 * - n=1：不做任何操作
 * - n>=2：执行连接操作
 *
 * 性能优化：
 * - 预计算结果长度
 * - 一次性分配内存
 * - 避免多次内存重分配
 * - 高效的字符串构建
 *
 * 内存管理：
 * - 可能触发垃圾回收
 * - 创建新的字符串对象
 * - 自动管理临时对象
 * - 字符串内化和去重
 *
 * 元方法支持：
 * - 调用__tostring进行类型转换
 * - 支持自定义的字符串转换
 * - 可能触发Lua代码执行
 *
 * 使用场景：
 * - 实现..操作符
 * - 字符串构建
 * - 格式化输出
 * - 模板系统
 *
 * @param L Lua状态机指针
 * @param n 要连接的值的数量
 *
 * @note 栈顶n个值被单个字符串替换
 * @note 支持数字到字符串的自动转换
 * @warning 可能触发垃圾回收和元方法调用
 *
 * @since C89
 * @see luaV_concat, __tostring元方法
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
    else if (n == 0) {
        setsvalue2s(L, L->top, luaS_newlstr(L, "", 0));
        api_incr_top(L);
    }
    lua_unlock(L);
}


/**
 * @brief 获取内存分配器
 *
 * 详细说明：
 * 获取当前Lua状态机使用的内存分配器函数和用户数据。
 * 这用于内存管理的监控和自定义。
 *
 * 分配器接口：
 * - void* (*lua_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize)
 * - ud：用户数据
 * - ptr：要重新分配的指针
 * - osize：原始大小
 * - nsize：新大小
 *
 * 分配器行为：
 * - nsize=0：释放内存
 * - ptr=NULL：分配新内存
 * - 其他：重新分配内存
 * - 失败时返回NULL
 *
 * 使用场景：
 * - 内存使用监控
 * - 自定义内存管理
 * - 调试内存问题
 * - 性能分析
 *
 * @param L Lua状态机指针
 * @param ud 如果不为NULL，用于返回用户数据指针
 * @return 当前的内存分配器函数
 *
 * @note 用户数据通过ud参数返回
 * @note 分配器函数不能为NULL
 *
 * @since C89
 * @see lua_setallocf, lua_Alloc
 */
LUA_API lua_Alloc lua_getallocf(lua_State *L, void **ud)
{
    lua_Alloc f;
    lua_lock(L);
    if (ud) *ud = G(L)->ud;
    f = G(L)->frealloc;
    lua_unlock(L);
    return f;
}

/**
 * @brief 设置内存分配器
 *
 * 详细说明：
 * 设置Lua状态机使用的内存分配器函数和用户数据。
 * 这允许自定义内存管理策略。
 *
 * 分配器要求：
 * - 必须实现lua_Alloc接口
 * - 必须正确处理所有分配情况
 * - 必须是线程安全的（如果需要）
 * - 失败时返回NULL
 *
 * 设置时机：
 * - 通常在lua_newstate之后立即设置
 * - 运行时更改需要谨慎
 * - 确保没有未释放的内存
 *
 * 安全考虑：
 * - 新分配器必须能处理现有内存
 * - 避免在分配器切换时泄漏内存
 * - 确保分配器的一致性
 *
 * 使用场景：
 * - 内存池管理
 * - 内存使用限制
 * - 调试内存问题
 * - 性能优化
 *
 * @param L Lua状态机指针
 * @param f 新的内存分配器函数
 * @param ud 传递给分配器的用户数据
 *
 * @note 分配器函数不能为NULL
 * @note 运行时更改需要谨慎
 * @warning 确保新分配器能处理现有内存
 *
 * @since C89
 * @see lua_getallocf, lua_Alloc
 */
LUA_API void lua_setallocf(lua_State *L, lua_Alloc f, void *ud)
{
    lua_lock(L);
    G(L)->ud = ud;
    G(L)->frealloc = f;
    lua_unlock(L);
}

/**
 * @brief 创建新的用户数据
 *
 * 详细说明：
 * 分配指定大小的用户数据并推入栈顶。用户数据是Lua管理的C数据块，
 * 支持元表和垃圾回收。
 *
 * 用户数据特性：
 * - 由Lua分配和管理
 * - 支持元表和元方法
 * - 被垃圾回收器管理
 * - 可以有环境表
 *
 * 内存布局：
 * - [Udata头部][用户数据]
 * - 返回指向用户数据部分的指针
 * - 头部包含元表、环境表等信息
 *
 * 生命周期：
 * - 创建时推入栈
 * - 由垃圾回收器管理
 * - 可以设置__gc元方法
 * - 自动内存管理
 *
 * 使用场景：
 * - 封装C对象
 * - 实现用户定义类型
 * - 资源管理
 * - 对象绑定
 *
 * 与轻量用户数据的区别：
 * - 完整：支持元表，被GC管理
 * - 轻量：只是指针，不被GC管理
 * - 完整：有额外开销
 * - 轻量：开销最小
 *
 * @param L Lua状态机指针
 * @param size 用户数据的字节大小
 * @return 指向用户数据的指针
 *
 * @note 用户数据被推入栈顶
 * @note 返回指向实际数据的指针，不是Udata结构
 * @warning 可能触发垃圾回收
 *
 * @since C89
 * @see lua_pushlightuserdata, luaS_newudata
 */
LUA_API void *lua_newuserdata(lua_State *L, size_t size)
{
    Udata *u;
    lua_lock(L);
    luaC_checkGC(L);
    u = luaS_newudata(L, size, getcurrenv(L));
    setuvalue(L, L->top, u);
    api_incr_top(L);
    lua_unlock(L);
    return u + 1;
}




/**
 * @brief 上值访问的辅助函数
 *
 * 详细说明：
 * 这是一个内部辅助函数，用于获取函数的上值信息。
 * 支持C函数和Lua函数的上值访问。
 *
 * 上值处理：
 * - C函数：直接访问upvalue数组
 * - Lua函数：通过UpVal结构访问
 * - 返回上值的名称和值指针
 *
 * 参数验证：
 * - 检查是否为函数类型
 * - 验证上值索引范围
 * - 确保上值存在
 *
 * @param fi 函数在栈中的位置
 * @param n 上值索引（从1开始）
 * @param val 用于返回上值指针
 * @return 上值名称，失败时返回NULL
 *
 * @since C89
 * @see lua_getupvalue, lua_setupvalue
 */
static const char *aux_upvalue(StkId fi, int n, TValue **val)
{
    Closure *f;
    if (!ttisfunction(fi)) return NULL;
    f = clvalue(fi);
    if (f->c.isC) {
        if (!(1 <= n && n <= f->c.nupvalues)) return NULL;
        *val = &f->c.upvalue[n-1];
        return "";
    }
    else {
        Proto *p = f->l.p;
        if (!(1 <= n && n <= p->sizeupvalues)) return NULL;
        *val = f->l.upvals[n-1]->v;
        return getstr(p->upvalues[n-1]);
    }
}

/**
 * @brief 获取函数的上值
 *
 * 详细说明：
 * 获取指定函数的第n个上值，并将其值推入栈顶。
 * 返回上值的名称，用于调试和反射。
 *
 * 上值概念：
 * - 闭包捕获的外部变量
 * - 实现词法作用域
 * - 支持数据封装
 * - 函数的私有状态
 *
 * 函数类型：
 * - C函数：上值存储在闭包中
 * - Lua函数：上值通过UpVal管理
 * - 不同类型有不同的访问方式
 *
 * 上值名称：
 * - Lua函数：返回变量名
 * - C函数：返回空字符串
 * - 用于调试和工具
 *
 * 使用场景：
 * - 调试器实现
 * - 代码分析工具
 * - 反射和内省
 * - 函数状态检查
 *
 * 索引规则：
 * - 从1开始计数
 * - 超出范围返回NULL
 * - 不存在的上值返回NULL
 *
 * @param L Lua状态机指针
 * @param funcindex 函数的栈索引
 * @param n 上值索引（从1开始）
 * @return 上值名称，失败时返回NULL
 *
 * @note 成功时上值被推入栈顶
 * @note Lua函数返回变量名，C函数返回空字符串
 * @note 索引从1开始
 *
 * @since C89
 * @see lua_setupvalue, aux_upvalue
 */
LUA_API const char *lua_getupvalue(lua_State *L, int funcindex, int n)
{
    const char *name;
    TValue *val;
    lua_lock(L);
    name = aux_upvalue(index2adr(L, funcindex), n, &val);
    if (name) {
        setobj2s(L, L->top, val);
        api_incr_top(L);
    }
    lua_unlock(L);
    return name;
}

/**
 * @brief 设置函数的上值
 *
 * 详细说明：
 * 使用栈顶的值设置指定函数的第n个上值。
 * 这允许修改闭包的状态。
 *
 * 设置过程：
 * 1. 获取上值的位置
 * 2. 将栈顶值复制到上值
 * 3. 设置垃圾回收屏障
 * 4. 弹出栈顶值
 *
 * 垃圾回收屏障：
 * - 确保引用关系正确
 * - 防止过早回收
 * - 维护GC的一致性
 *
 * 上值修改：
 * - C函数：直接修改upvalue数组
 * - Lua函数：修改UpVal指向的值
 * - 影响所有共享该上值的闭包
 *
 * 共享上值：
 * - 多个闭包可能共享同一个上值
 * - 修改会影响所有共享者
 * - 实现变量的共享语义
 *
 * 使用场景：
 * - 调试器功能
 * - 动态修改闭包状态
 * - 实现热更新
 * - 测试和诊断
 *
 * 安全考虑：
 * - 修改上值可能影响程序行为
 * - 确保类型兼容性
 * - 注意共享上值的影响
 *
 * @param L Lua状态机指针
 * @param funcindex 函数的栈索引
 * @param n 上值索引（从1开始）
 * @return 上值名称，失败时返回NULL
 *
 * @note 栈顶值被弹出并用于设置上值
 * @note 修改可能影响其他共享该上值的闭包
 * @note 自动设置垃圾回收屏障
 *
 * @since C89
 * @see lua_getupvalue, luaC_barrier
 */
LUA_API const char *lua_setupvalue(lua_State *L, int funcindex, int n)
{
    const char *name;
    TValue *val;
    StkId fi;
    lua_lock(L);
    fi = index2adr(L, funcindex);
    api_checknelems(L, 1);
    name = aux_upvalue(fi, n, &val);
    if (name) {
        L->top--;
        setobj(L, val, L->top);
        luaC_barrier(L, clvalue(fi), L->top);
    }
    lua_unlock(L);
    return name;
}

/** @} */

