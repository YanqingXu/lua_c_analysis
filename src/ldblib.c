/**
 * @file ldblib.c
 * @brief Lua调试库：完整的脚本调试和反射功能
 * 
 * 版权信息：
 * $Id: ldblib.c,v 1.104.1.4 2009/08/04 18:50:18 roberto Exp $
 * Lua调试API接口
 * 版权声明见lua.h文件
 * 
 * 程序概述：
 * 本文件实现了Lua的标准调试库，为Lua脚本提供了完整的调试和反射功能。
 * 调试库允许程序在运行时检查和修改执行环境、获取堆栈信息、设置断点
 * 和钩子函数，是开发调试器、性能分析器和代码检查工具的基础。
 * 
 * 系统架构定位：
 * 作为Lua虚拟机的核心库之一，调试库位于解释器内核之上，提供对虚拟机
 * 内部状态的安全访问接口。它与虚拟机的执行引擎紧密集成，能够实时监控
 * 代码执行过程，获取详细的运行时信息。
 * 
 * 主要功能模块：
 * 1. **堆栈检查**: 获取调用堆栈信息、函数源码位置、局部变量等
 * 2. **环境操作**: 读写函数环境表、元表、上值等运行时数据
 * 3. **钩子系统**: 设置函数调用、返回、行执行等事件的回调
 * 4. **调试控制**: 提供交互式调试控制台和错误跟踪功能
 * 5. **反射功能**: 运行时检查和修改程序结构与数据
 * 
 * 技术特点：
 * - 零性能开销：未设置钩子时对程序执行无影响
 * - 线程安全：支持多协程环境下的独立调试
 * - 完整性：提供从低级API到高级工具的完整调试支持
 * - 安全性：严格的权限控制，防止调试操作破坏程序稳定性
 * 
 * 应用场景：
 * - IDE集成调试器开发
 * - 代码覆盖率分析工具
 * - 性能剖析和监控系统
 * - 自动化测试框架
 * - 运行时代码检查工具
 * 
 * 注意事项：
 * - 调试功能仅应在开发和测试环境中使用
 * - 生产环境建议禁用调试库以提高安全性和性能
 * - 钩子函数的执行频率很高，应避免复杂计算
 * 
 * @author Roberto Ierusalimschy
 * @version 1.104.1.4
 * @date 2009-08-04
 * 
 * @see lua.h Lua核心API定义
 * @see lauxlib.h Lua辅助库接口
 * @see lualib.h Lua标准库声明
 * 
 * @note 本库为Lua解释器的标准组件，遵循MIT许可证
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ldblib_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"



/**
 * @brief 获取全局注册表：访问Lua虚拟机的全局注册表
 * 
 * 详细说明：
 * 这个函数提供对Lua虚拟机全局注册表的访问。注册表是Lua内部的一个特殊表，
 * 用于存储全局数据和内部状态信息。通过访问注册表，调试器可以检查和
 * 监控虚拟机的内部状态。
 * 
 * 实现机制：
 * 1. 直接将LUA_REGISTRYINDEX推入栈中
 * 2. 返回注册表的引用
 * 
 * 使用场景：
 * - 调试器检查虚拟机内部状态
 * - 全局数据的检查和监控
 * - 内存泄漏检测和分析
 * - 系统级调试和性能分析
 * 
 * 注意事项：
 * - 注册表包含敬感的系统数据，应谨慎使用
 * - 不应该修改注册表中的系统数据
 * - 仅限于调试和检查目的使用
 * 
 * @param L Lua状态机指针
 * 
 * @return int 返回值数量：1（注册表）
 * 
 * @see LUA_REGISTRYINDEX Lua注册表索引常量
 * @see lua_pushvalue() 栈操作函数
 * 
 * @note 这是一个调试用函数，不应在生产代码中使用
 */
static int db_getregistry(lua_State *L) {
    lua_pushvalue(L, LUA_REGISTRYINDEX);
    return 1;
}


/**
 * @brief 获取对象元表：检索指定对象的元表
 * 
 * 详细说明：
 * 这个函数获取指定对象的元表。元表是Lua中实现面向对象编程和操作符重载的
 * 核心机制。通过检查元表，调试器可以理解对象的行为和类型信息。
 * 
 * 实现机制：
 * 1. 检查第一个参数是否存在
 * 2. 尝试获取对象的元表
 * 3. 如果没有元表，则返回nil
 * 4. 否则返回元表对象
 * 
 * 支持的对象类型：
 * - 表(table)：用户数据类型的主要载体
 * - 用户数据(userdata)：封装C数据的对象
 * - 函数(function)：可执行的代码对象
 * - 线程(thread)：协程对象
 * 
 * 使用示例：
 * ```lua
 * local obj = {}
 * setmetatable(obj, {__index = function() return "hello" end})
 * local mt = debug.getmetatable(obj)  -- 获取元表
 * print(mt.__index)  -- 检查元方法
 * ```
 * 
 * 注意事项：
 * - 不是所有对象都有元表
 * - 元表本身也可以有元表（元元表）
 * - 修改元表可能影响对象行为
 * 
 * @param L Lua状态机指针
 * @param 1 要检查的对象（任意类型）
 * 
 * @return int 返回值数量：1（元表或nil）
 * 
 * @see lua_getmetatable() Lua元表获取API
 * @see luaL_checkany() 参数检查函数
 * @see setmetatable() Lua元表设置函数
 */
static int db_getmetatable(lua_State *L) {
    luaL_checkany(L, 1);
    if (!lua_getmetatable(L, 1)) {
        lua_pushnil(L);
    }
    return 1;
}


/**
 * @brief 设置对象元表：为指定对象设置元表
 * 
 * 详细说明：
 * 这个函数为指定对象设置元表，实现对象行为的动态修改。这是调试库的一个
 * 强大功能，允许在运行时动态修改对象的元表，从而改变对象的行为。
 * 
 * 实现机制：
 * 1. 检查第二个参数类型（必须是nil或table）
 * 2. 调用lua_setmetatable设置元表
 * 3. 返回设置操作的成功状态
 * 
 * 参数限制：
 * - 第一个参数：要设置元表的对象
 * - 第二个参数：nil（移除元表）或table（新元表）
 * 
 * 使用示例：
 * ```lua
 * local obj = {}
 * local mt = {__index = function() return "modified" end}
 * debug.setmetatable(obj, mt)  -- 设置元表
 * print(obj.any_key)  -- 输出 "modified"
 * debug.setmetatable(obj, nil)  -- 移除元表
 * ```
 * 
 * 安全性考虑：
 * - 修改元表可能影响程序逻辑
 * - 不当的元表可能导致安全问题
 * - 应该仅在调试环境中使用
 * 
 * 注意事项：
 * - 某些对象类型可能不允许设置元表
 * - 设置操作可能失败，需检查返回值
 * - 对基本类型设置元表将影响所有同类型对象
 * 
 * @param L Lua状态机指针
 * @param 1 目标对象
 * @param 2 新元表（nil或table类型）
 * 
 * @return int 返回值数量：1（成功标志）
 * 
 * @see lua_setmetatable() Lua元表设置API
 * @see luaL_argcheck() 参数检查函数
 * @see db_getmetatable() 元表获取函数
 */
static int db_setmetatable(lua_State *L) {
    int t = lua_type(L, 2);
    luaL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2,
                  "nil or table expected");
    lua_settop(L, 2);
    lua_pushboolean(L, lua_setmetatable(L, 1));
    return 1;
}


/**
 * @brief 获取对象环境：检索函数或线程的环境表
 * 
 * 详细说明：
 * 这个函数获取指定对象（通常是函数或线程）的环境表。环境表定义了函数执行时
 * 的全局变量可见性范围。通过检查环境表，调试器可以理解函数的作用域和
 * 可访问的全局变量。
 * 
 * 实现机制：
 * 1. 检查第一个参数是否存在
 * 2. 调用lua_getfenv获取对象的环境表
 * 3. 返回环境表对象
 * 
 * 适用对象类型：
 * - Lua函数：具有独立的环境表
 * - C函数：可能有关联的环境表
 * - 线程/协程：每个线程有独立环境
 * - 用户数据：可能有自定义环境
 * 
 * 使用场景：
 * - 调试器检查函数作用域
 * - 分析全局变量依赖关系
 * - 沙箱环境的检查和管理
 * - 动态代码分析和优化
 * 
 * 注意事项：
 * - 不是所有对象都有环境表
 * - 环境表影响函数的全局变量访问
 * - 修改环境表可能影响程序行为
 * 
 * @param L Lua状态机指针
 * @param 1 要检查的对象（任意类型）
 * 
 * @return int 返回值数量：1（环境表）
 * 
 * @see lua_getfenv() Lua环境获取API
 * @see luaL_checkany() 参数检查函数
 * @see db_setfenv() 环境设置函数
 */
static int db_getfenv(lua_State *L) {
    luaL_checkany(L, 1);
    lua_getfenv(L, 1);
    return 1;
}


/**
 * @brief 设置对象环境：为函数或线程设置新的环境表
 * 
 * 详细说明：
 * 这个函数为指定对象设置新的环境表，改变其全局变量的访问范围。这是一个
 * 非常强大的调试功能，允许在运行时动态修改函数的作用域，用于调试、
 * 沙箱化或动态代码修改。
 * 
 * 实现机制：
 * 1. 检查第二个参数必须是表类型
 * 2. 调用lua_setfenv设置环境表
 * 3. 如果设置失败，抛出错误
 * 4. 成功返回原对象
 * 
 * 参数要求：
 * - 第一个参数：目标对象（函数、线程等）
 * - 第二个参数：新的环境表（必须是table类型）
 * 
 * 使用场景：
 * - 沙箱环境的实现：限制函数的全局访问
 * - 动态代码修改：改变函数的执行环境
 * - 调试器功能：模拟不同的执行环境
 * - 可控执行环境：为测试提供可控环境
 * 
 * 安全性警告：
 * - 修改环境可能破坏程序逻辑
 * - 某些对象不允许修改环境
 * - 仅应在可控环境中使用
 * 
 * 错误处理：
 * - 如果目标对象不支持设置环境，会抛出错误
 * - 错误信息包含"setfenv cannot change environment"
 * 
 * @param L Lua状态机指针
 * @param 1 目标对象
 * @param 2 新环境表（必须是table类型）
 * 
 * @return int 返回值数量：1（修改后的对象）
 * 
 * @see lua_setfenv() Lua环境设置API
 * @see luaL_checktype() 类型检查函数
 * @see luaL_error() 错误抛出函数
 * @see db_getfenv() 环境获取函数
 */
static int db_setfenv(lua_State *L) {
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_settop(L, 2);
    if (lua_setfenv(L, 1) == 0)
        luaL_error(L, LUA_QL("setfenv")
                   " cannot change environment of given object");
    return 1;
}


/**
 * @brief 表字段设置函数（字符串值）：将字符串值设置到表字段
 * 
 * 详细说明：
 * 这是一个内部辅助函数，用于将字符串值设置到表的指定字段中。
 * 主要用于构建调试信息表，如debug.getinfo()返回的信息表。
 * 
 * 实现机制：
 * 1. 将字符串值推入栈中
 * 2. 使用lua_setfield设置到表的指定字段
 * 
 * 使用场景：
 * - 构建调试信息表的字符串字段
 * - 设置函数源码信息（source, short_src等）
 * - 设置函数类型信息（what, name, namewhat等）
 * 
 * @param L Lua状态机指针
 * @param i 字段名称（字符串）
 * @param v 要设置的字符串值
 * 
 * @return void 无返回值
 * 
 * @see lua_pushstring() 字符串推入函数
 * @see lua_setfield() 表字段设置函数
 * @see settabsi() 整数值设置函数
 * 
 * @note 这是一个内部辅助函数，不直接暴露给Lua代码
 */
static void settabss(lua_State *L, const char *i, const char *v) {
    lua_pushstring(L, v);
    lua_setfield(L, -2, i);
}


/**
 * @brief 表字段设置函数（整数值）：将整数值设置到表字段
 * 
 * 详细说明：
 * 这是一个内部辅助函数，用于将整数值设置到表的指定字段中。
 * 与settabss函数配合使用，用于构建包含数值和字符串的调试信息表。
 * 
 * 实现机制：
 * 1. 将整数值推入栈中
 * 2. 使用lua_setfield设置到表的指定字段
 * 
 * 使用场景：
 * - 设置函数定义行号（linedefined, lastlinedefined）
 * - 设置当前执行行号（currentline）
 * - 设置上值数量（nups）
 * - 构建其他数值型调试信息
 * 
 * 注意事项：
 * - 使用lua_pushinteger保证数值精度
 * - 适用于所有整数类型的调试信息
 * 
 * @param L Lua状态机指针
 * @param i 字段名称（字符串）
 * @param v 要设置的整数值
 * 
 * @return void 无返回值
 * 
 * @see lua_pushinteger() 整数推入函数
 * @see lua_setfield() 表字段设置函数
 * @see settabss() 字符串值设置函数
 * 
 * @note 这是一个内部辅助函数，不直接暴露给Lua代码
 */
static void settabsi(lua_State *L, const char *i, int v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, i);
}


/**
 * @brief 线程获取函数：从参数中获取目标线程
 * 
 * 详细说明：
 * 这是一个关键的辅助函数，用于处理调试函数中的线程参数。许多调试函数
 * 都支持对特定线程进行调试，这个函数负责解析第一个参数是否是线程对象，
 * 并返回相应的线程和参数偏移量。
 * 
 * 实现机制：
 * 1. 检查第一个参数是否为线程类型
 * 2. 如果是线程：设置参数偏移为1，返回该线程
 * 3. 如果不是：设置参数偏移为0，返回当前线程
 * 
 * 参数偏移意义：
 * - arg=0: 没有显式线程参数，后续参数从第1个开始
 * - arg=1: 有显式线程参数，后续参数从第2个开始
 * 
 * 使用场景：
 * - debug.getinfo()：获取指定线程的函数信息
 * - debug.getlocal()：获取指定线程的局部变量
 * - debug.sethook()：为指定线程设置鑳子
 * - 所有支持多线程调试的函数
 * 
 * 线程安全性：
 * - 支持Lua协程的独立调试
 * - 每个线程有独立的调用栈和环境
 * - 不同线程之间的调试操作相互独立
 * 
 * @param L Lua状态机指针
 * @param arg 输出参数：返回参数偏移量
 * 
 * @return lua_State* 目标线程的状态机指针
 * 
 * @see lua_isthread() 线程类型检查函数
 * @see lua_tothread() 线程对象获取函数
 * 
 * @note 这是调试库内部的关键辅助函数
 */
static lua_State *getthread(lua_State *L, int *arg) {
    if (lua_isthread(L, 1)) {
        *arg = 1;
        return lua_tothread(L, 1);
    }
    else {
        *arg = 0;
        return L;
    }
}


/**
 * @brief 处理栈选项：处理跨线程的栈数据传输
 * 
 * 详细说明：
 * 这个函数处理debug.getinfo()中的特殊选项（如'f'和'L'），这些选项需要
 * 将数据从目标线程传输到当前线程。函数处理了同线程和跨线程两种情况。
 * 
 * 实现机制：
 * - 同线程情况（L == L1）：
 *   1. 复制栈顶元素到新位置
 *   2. 移除原来的元素以避免重复
 * - 跨线程情况（L != L1）：
 *   1. 使用lua_xmove将数据从 L1 移动到 L
 * - 最后都使用lua_setfield设置到结果表中
 * 
 * 处理的选项类型：
 * - 'f' 选项：获取函数对象本身
 * - 'L' 选项：获取函数的有效行号表
 * 
 * 技术细节：
 * - lua_xmove实现了安全的跨线程数据传输
 * - 同线程优化避免了不必要的数据移动
 * - 正确处理了栈的清理和维护
 * 
 * 使用场景：
 * - debug.getinfo()中的'f'和'L'选项处理
 * - 跨线程调试信息获取
 * - 函数对象和活跃行号的安全传输
 * 
 * @param L 当前线程的Lua状态机
 * @param L1 目标线程的Lua状态机
 * @param fname 要设置的字段名称
 * 
 * @return void 无返回值，但修改栈状态
 * 
 * @see lua_xmove() 跨线程数据移动函数
 * @see lua_setfield() 表字段设置函数
 * @see db_getinfo() 调试信息获取函数
 * 
 * @note 这是处理调试信息的关键辅助函数
 */
static void treatstackoption(lua_State *L, lua_State *L1, const char *fname) {
    if (L == L1) {
        lua_pushvalue(L, -2);
        lua_remove(L, -3);
    }
    else
        lua_xmove(L1, L, 1);
    lua_setfield(L, -2, fname);
}


/**
 * @brief 获取调试信息：获取函数或调用栈的详细调试信息
 * 
 * 详细说明：
 * 这是Lua调试库的核心函数之一，用于获取函数或调用栈帧的详细信息。支持多种信息选项，
 * 可以获取函数源码位置、参数信息、局部变量、活跃行号等。这个函数是调试器、性能分析器
 * 和代码检查工具的基础。
 * 
 * 实现机制：
 * 1. 解析线程参数（如果有的话）
 * 2. 获取信息选项字符串（默认"flnSu"）
 * 3. 根据第二个参数类型进行不同处理：
 *    - 数字：获取指定层级的栈帧信息
 *    - 函数：获取函数对象的静态信息
 * 4. 调用lua_getinfo获取原始调试信息
 * 5. 根据选项解析并构建结果表
 * 
 * 信息选项说明：
 * - 'S': 源码信息（source, short_src, linedefined, lastlinedefined, what）
 * - 'l': 当前行号（currentline）
 * - 'n': 名称信息（name, namewhat）
 * - 'u': 上值数量（nups）
 * - 'f': 函数对象（func）
 * - 'L': 活跃行号表（activelines）
 * 
 * 参数类型处理：
 * - 数字参数：表示调用栈层级（0=当前函数，1=调用者，以此类推）
 * - 函数参数：直接分析函数对象的信息
 * - 层级超出范围时返回nil
 * 
 * 返回信息结构：
 * ```lua
 * {
 *   source = "@filename.lua",        -- 源文件名
 *   short_src = "filename.lua",      -- 短文件名
 *   linedefined = 10,                -- 函数定义起始行
 *   lastlinedefined = 20,            -- 函数定义结束行
 *   what = "Lua",                    -- 函数类型
 *   currentline = 15,                -- 当前执行行
 *   name = "myfunction",             -- 函数名称
 *   namewhat = "global",             -- 名称类型
 *   nups = 2,                        -- 上值数量
 *   func = function(...),            -- 函数对象
 *   activelines = {10,11,12,...}     -- 有效行号
 * }
 * ```
 * 
 * 使用场景：
 * - 调试器获取栈帧信息
 * - 错误追踪和日志记录
 * - 性能分析和代码覆盖率
 * - 动态代码分析工具
 * - IDE智能提示和代码导航
 * 
 * 线程安全性：
 * - 支持跨线程调试信息获取
 * - 每个线程有独立的调用栈
 * - 安全的跨线程数据传输
 * 
 * 注意事项：
 * - 'f'和'L'选项会传输实际对象，需要特殊处理
 * - C函数的信息有限，主要包含基本标识
 * - 尾调用优化可能影响栈层级计算
 * - 某些选项组合可能产生大量数据
 * 
 * @param L Lua状态机指针
 * @param 1 可选的线程对象
 * @param 2 栈层级（数字）或函数对象
 * @param 3 可选的信息选项字符串（默认"flnSu"）
 * 
 * @return int 返回值数量：1（信息表或nil）
 * 
 * @see lua_getstack() 栈帧获取函数
 * @see lua_getinfo() 调试信息获取API
 * @see getthread() 线程解析函数
 * @see treatstackoption() 栈选项处理函数
 * 
 * @note 这是调试库最重要的函数之一，被广泛用于调试工具开发
 */
static int db_getinfo(lua_State *L) {
    lua_Debug ar;
    int arg;
    lua_State *L1 = getthread(L, &arg);
    const char *options = luaL_optstring(L, arg+2, "flnSu");
    if (lua_isnumber(L, arg+1)) {
        if (!lua_getstack(L1, (int)lua_tointeger(L, arg+1), &ar)) {
            lua_pushnil(L);
            return 1;
        }
    }
    else if (lua_isfunction(L, arg+1)) {
        lua_pushfstring(L, ">%s", options);
        options = lua_tostring(L, -1);
        lua_pushvalue(L, arg+1);
        lua_xmove(L, L1, 1);
    }
    else
        return luaL_argerror(L, arg+1, "function or level expected");
    if (!lua_getinfo(L1, options, &ar))
        return luaL_argerror(L, arg+2, "invalid option");
    lua_createtable(L, 0, 2);
    if (strchr(options, 'S')) {
        settabss(L, "source", ar.source);
        settabss(L, "short_src", ar.short_src);
        settabsi(L, "linedefined", ar.linedefined);
        settabsi(L, "lastlinedefined", ar.lastlinedefined);
        settabss(L, "what", ar.what);
    }
    if (strchr(options, 'l'))
        settabsi(L, "currentline", ar.currentline);
    if (strchr(options, 'u'))
        settabsi(L, "nups", ar.nups);
    if (strchr(options, 'n')) {
        settabss(L, "name", ar.name);
        settabss(L, "namewhat", ar.namewhat);
    }
    if (strchr(options, 'L'))
        treatstackoption(L, L1, "activelines");
    if (strchr(options, 'f'))
        treatstackoption(L, L1, "func");
    return 1;
}
    

/**
 * @brief 获取局部变量：获取指定栈帧的局部变量值
 * 
 * 详细说明：
 * 这个函数获取指定调用栈层级的局部变量信息。它是调试器实现变量检查功能的
 * 基础，允许在不中断程序执行的情况下检查任何函数的局部变量状态。
 * 
 * 实现机制：
 * 1. 解析线程参数（如果有）
 * 2. 检查指定的栈层级是否有效
 * 3. 调用lua_getlocal获取指定索引的局部变量
 * 4. 如果变量存在，返回变量值和名称
 * 5. 如果不存在，返回nil
 * 
 * 参数说明：
 * - 第一个参数：可选的线程对象
 * - 第二个参数：栈层级（0=当前函数，1=调用者）
 * - 第三个参数：局部变量索引（1基于的索引）
 * 
 * 返回值类型：
 * - 成功：返回2个值（变量值, 变量名）
 * - 失败：返回1个值（nil）
 * 
 * 使用场景：
 * - 调试器变量检查窗口
 * - 错误追踪中的变量状态记录
 * - 动态代码分析和检查
 * - 自动化测试中的状态验证
 * 
 * 注意事项：
 * - 变量索引是1基于的（第一个变量索引为1）
 * - 临时变量和编译器优化可能影响可见性
 * - 不同的Lua版本可能有不同的局部变量管理
 * - 需要在有效的栈帧范围内调用
 * 
 * 错误处理：
 * - 栈层级超出范围时抛出"level out of range"错误
 * - 变量索引无效时返回nil而不抛出错误
 * 
 * @param L Lua状态机指针
 * @param 1 可选的线程对象
 * @param 2 栈层级（整数）
 * @param 3 局部变量索引（整数）
 * 
 * @return int 返回值数量：1（nil）或2（值+名称）
 * 
 * @see lua_getstack() 栈帧获取函数
 * @see lua_getlocal() 局部变量获取API
 * @see db_setlocal() 局部变量设置函数
 * @see getthread() 线程解析函数
 */
static int db_getlocal(lua_State *L) {
    int arg;
    lua_State *L1 = getthread(L, &arg);
    lua_Debug ar;
    const char *name;
    if (!lua_getstack(L1, luaL_checkint(L, arg+1), &ar))
        return luaL_argerror(L, arg+1, "level out of range");
    name = lua_getlocal(L1, &ar, luaL_checkint(L, arg+2));
    if (name) {
        lua_xmove(L1, L, 1);
        lua_pushstring(L, name);
        lua_pushvalue(L, -2);
        return 2;
    }
    else {
        lua_pushnil(L);
        return 1;
    }
}


/**
 * @brief 设置局部变量：修改指定栈帧的局部变量值
 * 
 * 详细说明：
 * 这个函数允许在运行时修改指定调用栈层级的局部变量值。这是调试器实现
 * 变量修改功能的基础，允许在调试过程中动态修改程序状态来测试不同的
 * 执行路径和数据值。
 * 
 * 实现机制：
 * 1. 解析线程参数（如果有）
 * 2. 检查指定的栈层级是否有效
 * 3. 验证第三个参数（新值）的有效性
 * 4. 将新值移动到目标线程的栈中
 * 5. 调用lua_setlocal设置局部变量
 * 6. 返回变量名称（或nil）
 * 
 * 参数说明：
 * - 第一个参数：可选的线程对象
 * - 第二个参数：栈层级（0=当前函数，1=调用者）
 * - 第三个参数：局部变量索引（1基于的索引）
 * - 第四个参数：新的变量值（任意类型）
 * 
 * 返回值含义：
 * - 成功：返回变量名称字符串
 * - 失败：返回nil（变量不存在或不可修改）
 * 
 * 使用场景：
 * - 调试器变量编辑功能
 * - 单元测试中的状态模拟
 * - 动态代码修改和调试
 * - 错误模拟和异常测试
 * 
 * 安全性考虑：
 * - 修改局部变量可能影响程序逻辑
 * - 某些变量可能是只读的（如循环变量）
 * - 类型不兼容的赋值可能导致意外的行为
 * - 仅应在可控的调试环境中使用
 * 
 * 技术细节：
 * - 使用lua_xmove实现安全的跨线程数据传输
 * - lua_setlocal返回变量名称或NULL
 * - 支持所有Lua数据类型的赋值
 * 
 * 错误处理：
 * - 栈层级超出范围时抛出"level out of range"错误
 * - 变量索引无效时返回nil名称
 * 
 * @param L Lua状态机指针
 * @param 1 可选的线程对象
 * @param 2 栈层级（整数）
 * @param 3 局部变量索引（整数）
 * @param 4 新的变量值（任意类型）
 * 
 * @return int 返回值数量：1（变量名称或nil）
 * 
 * @see lua_getstack() 栈帧获取函数
 * @see lua_setlocal() 局部变量设置API
 * @see db_getlocal() 局部变量获取函数
 * @see lua_xmove() 跨线程数据移动函数
 */
static int db_setlocal(lua_State *L) {
    int arg;
    lua_State *L1 = getthread(L, &arg);
    lua_Debug ar;
    if (!lua_getstack(L1, luaL_checkint(L, arg+1), &ar))
        return luaL_argerror(L, arg+1, "level out of range");
    luaL_checkany(L, arg+3);
    lua_settop(L, arg+3);
    lua_xmove(L, L1, 1);
    lua_pushstring(L, lua_setlocal(L1, &ar, luaL_checkint(L, arg+2)));
    return 1;
}


/**
 * @brief 上值操作辅助函数：统一处理上值的获取和设置操作
 * 
 * 详细说明：
 * 这是一个内部辅助函数，为db_getupvalue和db_setupvalue提供通用的上值操作逻辑。
 * 上值是闭包函数中捕获的外部变量，这个函数提供了对这些变量的访问和修改能力。
 * 
 * 实现机制：
 * 1. 检查函数类型（不支持C函数的上值操作）
 * 2. 根据get参数决定操作类型：
 *    - get=1: 获取上值（调用lua_getupvalue）
 *    - get=0: 设置上值（调用lua_setupvalue）
 * 3. 检查操作是否成功（通过返回的名称判断）
 * 4. 构建返回结果（名称+值或只有名称）
 * 
 * 参数说明：
 * - L: Lua状态机指针
 * - get: 操作类型（1=获取，0=设置）
 * - 栈参数：
 *   - 位置1: 函数对象
 *   - 位置2: 上值索引（1基于的索引）
 *   - 位置3: 新值（仅设置操作需要）
 * 
 * 返回值类型：
 * - 获取操作：2个值（上值值, 上值名）
 * - 设置操作：1个值（上值名）
 * - 失败：0个值
 * 
 * 限制条件：
 * - 只支持Lua函数，不支持C函数
 * - 上值索引必须在有效范围内
 * - 设置操作需要提供新值
 * 
 * 安全性考虑：
 * - 修改上值可能影响多个闭包实例
 * - C函数的上值从 Lua 中不可访问
 * - 需要理解闭包的生命周期和作用域
 * 
 * @param L Lua状态机指针
 * @param get 操作类型（1=获取，0=设置）
 * 
 * @return int 返回值数量（0/1/2）
 * 
 * @see lua_getupvalue() Lua上值获取API
 * @see lua_setupvalue() Lua上值设置API
 * @see lua_iscfunction() C函数检查函数
 * 
 * @note 这是内部辅助函数，不直接暴露给Lua代码
 */
static int auxupvalue(lua_State *L, int get) {
    const char *name;
    int n = luaL_checkint(L, 2);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (lua_iscfunction(L, 1)) return 0;
    name = get ? lua_getupvalue(L, 1, n) : lua_setupvalue(L, 1, n);
    if (name == NULL) return 0;
    lua_pushstring(L, name);
    lua_insert(L, -(get+1));
    return get + 1;
}


/**
 * @brief 获取函数上值：获取指定函数的上值信息
 * 
 * 详细说明：
 * 这个函数提供Lua代码访问函数上值的能力。上值是闭包函数中捕获的外部变量，
 * 通过这个函数可以检查闭包的内部状态和数据。这对于调试器理解闭包的
 * 行为和数据流至关重要。
 * 
 * 实现机制：
 * 1. 调用auxupvalue辅助函数进行实际操作
 * 2. 传入get=1参数表示这是获取操作
 * 3. 返回上值的值和名称
 * 
 * 使用示例：
 * ```lua
 * local function createCounter()
 *     local count = 0
 *     return function()
 *         count = count + 1
 *         return count
 *     end
 * end
 * 
 * local counter = createCounter()
 * local value, name = debug.getupvalue(counter, 1)
 * print(name, value)  -- 输出: count 0
 * ```
 * 
 * 参数要求：
 * - 第一个参数：函数对象（必须是Lua函数）
 * - 第二个参数：上值索引（1基于的索引）
 * 
 * 返回值说明：
 * - 成功：2个值（上值的值, 上值的名称）
 * - 失败：0个值（索引无效或C函数）
 * 
 * 限制条件：
 * - 只支持Lua函数，不支持C函数
 * - 上值索引必须在有效范围内
 * - 上值数量可以通过debug.getinfo获取
 * 
 * 技术细节：
 * - 上值是共享的，多个闭包可能共享同一个上值
 * - 上值的修改会影响所有共享该上值的闭包
 * - 上值名称可能为空（匿名上值）
 * 
 * @param L Lua状态机指针
 * @param 1 函数对象（Lua函数）
 * @param 2 上值索引（整数）
 * 
 * @return int 返回值数量：0（失败）或2（成功）
 * 
 * @see auxupvalue() 上值操作辅助函数
 * @see db_setupvalue() 上值设置函数
 * @see lua_getupvalue() Lua上值获取API
 */
static int db_getupvalue(lua_State *L) {
    return auxupvalue(L, 1);
}


/**
 * @brief 设置函数上值：修改指定函数的上值
 * 
 * 详细说明：
 * 这个函数允许在运行时修改函数的上值。这是一个非常强大的调试功能，
 * 允许动态修改闭包的内部状态，用于调试、测试或专门的代码分析工具。
 * 
 * 实现机制：
 * 1. 验证第三个参数（新值）的存在
 * 2. 调用auxupvalue辅助函数进行实际操作
 * 3. 传入get=0参数表示这是设置操作
 * 4. 返回上值名称（或空如果设置失败）
 * 
 * 使用示例：
 * ```lua
 * local function createCounter()
 *     local count = 0
 *     return function()
 *         count = count + 1
 *         return count
 *     end
 * end
 * 
 * local counter = createCounter()
 * debug.setupvalue(counter, 1, 100)  -- 设置 count = 100
 * print(counter())  -- 输出: 101
 * ```
 * 
 * 参数要求：
 * - 第一个参数：函数对象（必须是Lua函数）
 * - 第二个参数：上值索引（1基于的索引）
 * - 第三个参数：新的上值（任意类型）
 * 
 * 返回值说明：
 * - 成功：1个值（上值名称）
 * - 失败：0个值（索引无效或C函数）
 * 
 * 安全性警告：
 * - 修改上值会影响所有共享该上值的闭包
 * - 可能破坏程序的逻辑一致性和数据完整性
 * - 仅应在可控的调试环境中使用
 * - 需要理解闭包的生命周期和作用域
 * 
 * 限制条件：
 * - 只支持Lua函数，不支持C函数
 * - 上值索引必须在有效范围内
 * - 新值必须提供（不能为nil或缺失）
 * 
 * 技术细节：
 * - 上值的修改立即生效
 * - 修改后的上值会被所有共享的闭包看到
 * - 支持所有Lua数据类型的赋值
 * 
 * @param L Lua状态机指针
 * @param 1 函数对象（Lua函数）
 * @param 2 上值索引（整数）
 * @param 3 新的上值（任意类型）
 * 
 * @return int 返回值数量：0（失败）或1（成功）
 * 
 * @see auxupvalue() 上值操作辅助函数
 * @see db_getupvalue() 上值获取函数
 * @see lua_setupvalue() Lua上值设置API
 * @see luaL_checkany() 参数存在性检查函数
 */
static int db_setupvalue(lua_State *L) {
    luaL_checkany(L, 3);
    return auxupvalue(L, 0);
}



/**
 * @brief 鑳子注册表键值：用于在注册表中标识鑳子表
 * 
 * 详细说明：
 * 这个常量用作注册表中的键，用于存储和检索所有线程的鑳子函数。
 * 每个线程都可以有自己的鑳子函数，这些函数被存储在一个全局表中。
 */
static const char KEY_HOOK = 'h';

/**
 * @brief 鑳子函数调用器：实际的鑳子事件处理函数
 * 
 * 详细说明：
 * 这是Lua虚拟机调用的实际鑳子函数，当设置的鑳子事件发生时，虚拟机会
 * 调用这个函数。该函数负责查找用户设置的Lua鑳子函数并调用它。
 * 
 * 实现机制：
 * 1. 从注册表中获取鑳子表
 * 2. 根据线程指针查找对应的鑳子函数
 * 3. 如果找到函数，准备参数并调用
 * 4. 传递事件类型和当前行号信息
 * 
 * 事件类型映射：
 * - "call": 函数调用事件
 * - "return": 函数返回事件
 * - "line": 行执行事件
 * - "count": 指令计数事件
 * - "tail return": 尾调用返回事件
 * 
 * 参数传递：
 * - 第一个参数：事件类型字符串
 * - 第二个参数：当前行号（如果有）或nil
 * 
 * 性能考虑：
 * - 这个函数会被频繁调用，必须保持高效
 * - 如果没有设置鑳子函数，会快速返回
 * - 鑳子函数中的错误不会传播到主程序
 * 
 * 安全性：
 * - 使用lua_pcall避免鑳子函数中的错误影响主程序
 * - 正确处理栈操作和内存管理
 * 
 * @param L 当前线程的Lua状态机
 * @param ar 调试信息结构，包含事件类型和行号信息
 * 
 * @return void 无返回值
 * 
 * @see lua_sethook() 鑳子设置函数
 * @see db_sethook() 用户鑳子设置接口
 * @see lua_call() Lua函数调用API
 * 
 * @note 这是系统内部函数，由Lua虚拟机直接调用
 */
static void hookf(lua_State *L, lua_Debug *ar) {
    static const char *const hooknames[] =
        {"call", "return", "line", "count", "tail return"};
    lua_pushlightuserdata(L, (void *)&KEY_HOOK);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushlightuserdata(L, L);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, hooknames[(int)ar->event]);
        if (ar->currentline >= 0)
            lua_pushinteger(L, ar->currentline);
        else lua_pushnil(L);
        lua_assert(lua_getinfo(L, "lS", ar));
        lua_call(L, 2, 0);
    }
}


/**
 * @brief 构建鑳子掩码：将字符串格式的鑳子选项转换为位掩码
 * 
 * 详细说明：
 * 这个函数将用户友好的字符串格式鑳子选项转换为Lua API需要的位掩码格式。
 * 这种转换使得用户可以使用简单的字符来指定复杂的鑳子配置。
 * 
 * 支持的选项字符：
 * - 'c': 函数调用事件 (LUA_MASKCALL)
 * - 'r': 函数返回事件 (LUA_MASKRET)
 * - 'l': 行执行事件 (LUA_MASKLINE)
 * - count > 0: 指令计数事件 (LUA_MASKCOUNT)
 * 
 * 实现机制：
 * 1. 初始化掩码为0
 * 2. 逐个检查字符串中的字符
 * 3. 根据字符设置相应的位标志
 * 4. 如果count>0，设置LUA_MASKCOUNT标志
 * 
 * 使用示例：
 * ```c
 * int mask = makemask("clr", 100);  // 调用+行+返回+计数
 * int mask2 = makemask("l", 0);     // 只有行事件
 * ```
 * 
 * 注意事项：
 * - 不识别的字符会被忽略
 * - count参数只影响是否启用计数事件
 * - 同一字符出现多次不会产生副作用
 * 
 * @param smask 鑳子选项字符串（包含'c','r','l'等）
 * @param count 指令计数阈值（>0时启用计数事件）
 * 
 * @return int 组合后的位掩码
 * 
 * @see unmakemask() 掩码到字符串转换函数
 * @see lua_sethook() Lua鑳子设置API
 * @see LUA_MASKCALL/LUA_MASKRET/LUA_MASKLINE/LUA_MASKCOUNT 鑳子掩码常量
 * 
 * @note 这是内部辅助函数，用于简化鑳子设置操作
 */
static int makemask(const char *smask, int count) {
    int mask = 0;
    if (strchr(smask, 'c')) mask |= LUA_MASKCALL;
    if (strchr(smask, 'r')) mask |= LUA_MASKRET;
    if (strchr(smask, 'l')) mask |= LUA_MASKLINE;
    if (count > 0) mask |= LUA_MASKCOUNT;
    return mask;
}


/**
 * @brief 解析鑳子掩码：将位掩码格式的鑳子选项转换为字符串
 * 
 * 详细说明：
 * 这个函数是makemask的逆操作，将Lua API使用的位掩码格式转换为用户友好的
 * 字符串格式。这主要用于debug.gethook()函数，向用户返回当前的鑳子配置。
 * 
 * 转换规则：
 * - LUA_MASKCALL → 'c'
 * - LUA_MASKRET → 'r'
 * - LUA_MASKLINE → 'l'
 * - LUA_MASKCOUNT 不会在字符串中显示（另由count参数表示）
 * 
 * 实现机制：
 * 1. 初始化字符串索引为0
 * 2. 逐个检查掩码的位标志
 * 3. 根据设置的标志添加相应字符
 * 4. 以\0结尾字符串
 * 
 * 使用示例：
 * ```c
 * char buffer[4];
 * char *result = unmakemask(LUA_MASKCALL | LUA_MASKLINE, buffer);
 * // result 将包含 "cl"
 * ```
 * 
 * 注意事项：
 * - 输出缓冲区必须至少昄4字节（包括\0）
 * - 输出字符串没有固定顺序，取决于检查顺序
 * - LUA_MASKCOUNT不会出现在结果中
 * 
 * @param mask 要转换的位掩码
 * @param smask 输出缓冲区（至少4字节）
 * 
 * @return char* 返回输入的smask指针（为了方便链式调用）
 * 
 * @see makemask() 字符串到掩码转换函数
 * @see lua_gethookmask() Lua鑳子掩码获取API
 * @see db_gethook() 鑳子信息获取函数
 * 
 * @note 这是内部辅助函数，用于简化鑳子查询操作
 */
static char *unmakemask(int mask, char *smask) {
    int i = 0;
    if (mask & LUA_MASKCALL) smask[i++] = 'c';
    if (mask & LUA_MASKRET) smask[i++] = 'r';
    if (mask & LUA_MASKLINE) smask[i++] = 'l';
    smask[i] = '\0';
    return smask;
}


/**
 * @brief 获取鑳子表：获取或创建全局鑳子表
 * 
 * 详细说明：
 * 这个内部函数负责管理全局鑳子表，该表存储了所有线程的鑳子函数。
 * 如果表不存在，会自动创建一个新表。这保证了鑳子系统的正常运行。
 * 
 * 实现机制：
 * 1. 使用KEY_HOOK作为键从注册表中获取鑳子表
 * 2. 检查获取的对象是否为表类型
 * 3. 如果不是表，则：
 *    a. 弹出旧的对象
 *    b. 创建一个新表
 *    c. 将新表存储到注册表中
 * 4. 确保表在栈顶
 * 
 * 数据结构：
 * 鑳子表是一个全局表，以线程指针为键，鑳子函数为值：
 * ```
 * hook_table = {
 *   [thread1_ptr] = hook_function1,
 *   [thread2_ptr] = hook_function2,
 *   ...
 * }
 * ```
 * 
 * 用途：
 * - db_sethook()中设置鑳子函数
 * - db_gethook()中获取鑳子函数
 * - hookf()中查找和调用鑳子函数
 * 
 * 内存管理：
 * - 使用注册表保证表不会被垃圾回收
 * - 线程指针作为轻量级用户数据使用
 * - 自动创建机制简化了初始化过程
 * 
 * 注意事项：
 * - 该函数会修改栈状态（在栈顶留下鑳子表）
 * - 只在需要时才创建表，优化了内存使用
 * - 线程安全：每个线程有独立的鑳子函数
 * 
 * @param L Lua状态机指针
 * 
 * @return void 无返回值，但在栈顶留下鑳子表
 * 
 * @see KEY_HOOK 鑳子表在注册表中的键
 * @see lua_createtable() Lua表创建函数
 * @see lua_rawget()/lua_rawset() 原始表操作函数
 * 
 * @note 这是鑳子系统的内部辅助函数
 */
static void gethooktable(lua_State *L) {
    lua_pushlightuserdata(L, (void *)&KEY_HOOK);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_createtable(L, 0, 1);
        lua_pushlightuserdata(L, (void *)&KEY_HOOK);
        lua_pushvalue(L, -2);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }
}


/**
 * @brief 设置调试钩子：为指定线程设置事件钩子函数
 * 
 * 详细说明：
 * 这是Lua调试库的核心函数之一，用于设置调试钩子。钩子函数会在特定事件发生时被调用，
 * 如函数调用、返回、行执行等。这是实现调试器、性能分析器和代码覆盖率工具的基础。
 * 
 * 实现机制：
 * 1. 解析线程参数（如果有的话）
 * 2. 检查是否要禁用钩子（nil参数）
 * 3. 如果启用钩子：
 *    a. 验证钩子函数参数
 *    b. 解析事件掩码字符串
 *    c. 获取可选的计数参数
 * 4. 获取全局钩子表并更新
 * 5. 调用lua_sethook设置底层钩子
 * 
 * 参数格式：
 * - 参数1：可选的线程对象
 * - 参数2：钩子函数（function）或nil（禁用）
 * - 参数3：事件掩码字符串（如"clr"表示调用+行+返回）
 * - 参数4：可选的指令计数（用于'count'事件）
 * 
 * 事件类型：
 * - 'c': 函数调用事件 (call)
 * - 'r': 函数返回事件 (return)
 * - 'l': 行执行事件 (line)
 * - count > 0: 指令计数事件 (count)
 * 
 * 钩子函数签名：
 * ```lua
 * function hook(event, line)
 *   -- event: "call", "return", "line", "count", "tail return"
 *   -- line: 当前行号（如果可用）或nil
 * end
 * ```
 * 
 * 使用示例：
 * ```lua
 * -- 设置行执行钩子
 * debug.sethook(function(event, line)
 *   print(event, line)
 * end, "l")
 * 
 * -- 设置每100条指令的计数钩子
 * debug.sethook(function(event)
 *   print("100 instructions executed")
 * end, "", 100)
 * 
 * -- 禁用钩子
 * debug.sethook()
 * ```
 * 
 * 性能考虑：
 * - 钩子函数的执行频率可能很高，应保持高效
 * - 行钩子在每行代码执行时都会触发
 * - 调用/返回钩子在函数边界触发
 * - 计数钩子按指令数量触发
 * 
 * 线程安全性：
 * - 每个线程可以有独立的钩子配置
 * - 跨线程钩子设置是安全的
 * - 钩子函数在目标线程中执行
 * 
 * 注意事项：
 * - 钩子函数中的错误会被忽略（使用lua_call）
 * - 过于频繁的钩子可能显著影响性能
 * - 递归调用可能导致钩子嵌套
 * - 钩子函数应避免复杂计算和I/O操作
 * 
 * 内存管理：
 * - 钩子函数会被存储在全局钩子表中
 * - 线程销毁时钩子引用会自动清理
 * - 使用轻量级用户数据作为线程标识
 * 
 * @param L Lua状态机指针
 * @param 1 可选的线程对象
 * @param 2 钩子函数（function）或nil（禁用钩子）
 * @param 3 事件掩码字符串（如"clr"）
 * @param 4 可选的指令计数（整数）
 * 
 * @return int 返回值数量：0
 * 
 * @see hookf() 实际的钩子处理函数
 * @see makemask() 掩码字符串转换函数
 * @see gethooktable() 钩子表管理函数
 * @see lua_sethook() Lua钩子设置API
 * @see db_gethook() 钩子信息获取函数
 * 
 * @note 这是调试库的核心功能，需要谨慎使用以避免性能问题
 */
static int db_sethook(lua_State *L) {
    int arg, mask, count;
    lua_Hook func;
    lua_State *L1 = getthread(L, &arg);
    if (lua_isnoneornil(L, arg+1)) {
        lua_settop(L, arg+1);
        func = NULL; mask = 0; count = 0;
    }
    else {
        const char *smask = luaL_checkstring(L, arg+2);
        luaL_checktype(L, arg+1, LUA_TFUNCTION);
        count = luaL_optint(L, arg+3, 0);
        func = hookf; mask = makemask(smask, count);
    }
    gethooktable(L);
    lua_pushlightuserdata(L, L1);
    lua_pushvalue(L, arg+1);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    lua_sethook(L1, func, mask, count);
    return 0;
}


/**
 * @brief 获取调试钩子：获取指定线程的当前钩子配置
 * 
 * 详细说明：
 * 这个函数获取指定线程当前的调试钩子配置信息，包括钩子函数、事件掩码和计数设置。
 * 它与db_sethook()配对使用，允许程序查询当前的调试配置状态。
 * 
 * 实现机制：
 * 1. 解析线程参数（如果有的话）
 * 2. 获取线程的当前钩子配置
 * 3. 检查钩子类型（内部钩子 vs 外部钩子）
 * 4. 构建返回结果：
 *    a. 钩子函数（或"external hook"标识）
 *    b. 事件掩码字符串
 *    c. 指令计数值
 * 
 * 返回值格式：
 * - 返回值1：钩子函数或"external hook"字符串
 * - 返回值2：事件掩码字符串（如"clr"）
 * - 返回值3：指令计数（整数）
 * 
 * 特殊情况处理：
 * - 如果检测到外部钩子（非通过debug.sethook设置）
 *   会返回"external hook"字符串而不是函数
 * - 如果没有设置钩子，返回nil
 * 
 * 使用示例：
 * ```lua
 * -- 设置钩子
 * debug.sethook(function(event, line)
 *   print(event, line)
 * end, "clr", 100)
 * 
 * -- 获取钩子信息
 * local func, mask, count = debug.gethook()
 * print(type(func))  -- "function"
 * print(mask)        -- "clr"
 * print(count)       -- 100
 * 
 * -- 临时禁用钩子
 * debug.sethook()
 * local func2, mask2, count2 = debug.gethook()
 * print(func2)       -- nil
 * 
 * -- 恢复钩子
 * debug.sethook(func, mask, count)
 * ```
 * 
 * 外部钩子检测：
 * - 如果钩子函数不是内部的hookf，则认为是外部钩子
 * - 外部钩子通常由C扩展或其他调试工具设置
 * - 外部钩子无法获取具体的Lua函数引用
 * 
 * 线程支持：
 * - 支持查询指定线程的钩子状态
 * - 每个线程的钩子配置是独立的
 * - 跨线程查询是安全的
 * 
 * 技术细节：
 * - 使用lua_gethook()获取底层钩子信息
 * - 使用unmakemask()转换掩码格式
 * - 通过钩子表查找对应的Lua函数
 * 
 * 注意事项：
 * - 外部钩子的详细信息无法完全获取
 * - 返回的函数引用与原设置的函数相同
 * - 掩码字符串不包含计数信息（由单独参数返回）
 * 
 * @param L Lua状态机指针
 * @param 1 可选的线程对象
 * 
 * @return int 返回值数量：3（钩子函数, 掩码字符串, 计数）
 * 
 * @see db_sethook() 钩子设置函数
 * @see unmakemask() 掩码转换函数
 * @see gethooktable() 钩子表管理函数
 * @see lua_gethook() Lua钩子获取API
 * @see lua_gethookmask() 钩子掩码获取API
 * @see lua_gethookcount() 钩子计数获取API
 * 
 * @note 这个函数提供了完整的钩子状态查询能力
 */
static int db_gethook(lua_State *L) {
    int arg;
    lua_State *L1 = getthread(L, &arg);
    char buff[5];
    int mask = lua_gethookmask(L1);
    lua_Hook hook = lua_gethook(L1);
    if (hook != NULL && hook != hookf)
        lua_pushliteral(L, "external hook");
    else {
        gethooktable(L);
        lua_pushlightuserdata(L, L1);
        lua_rawget(L, -2);
        lua_remove(L, -2);
    }
    lua_pushstring(L, unmakemask(mask, buff));
    lua_pushinteger(L, lua_gethookcount(L1));
    return 3;
}


/**
 * @brief 交互式调试控制台：提供简单的命令行调试环境
 * 
 * 详细说明：
 * 这个函数实现了一个简单的交互式调试控制台，允许用户在程序执行过程中
 * 输入和执行Lua代码。这对于快速调试和检查程序状态非常有用。
 * 
 * 实现机制：
 * 1. 进入无限循环，等待用户输入
 * 2. 显示"lua_debug> "提示符
 * 3. 从标准输入读取命令行
 * 4. 检查退出条件：
 *    - EOF（Ctrl+D或文件结束）
 *    - "cont\n"（继续执行命令）
 * 5. 编译和执行用户输入的Lua代码
 * 6. 显示错误信息（如果有）
 * 7. 清理栈并继续循环
 * 
 * 支持的操作：
 * - 执行任意Lua表达式和语句
 * - 访问当前作用域中的变量
 * - 调用函数和检查返回值
 * - 使用所有可用的调试函数
 * 
 * 使用示例：
 * ```lua
 * function test()
 *   local x = 10
 *   debug.debug()  -- 进入调试控制台
 *   print(x)
 * end
 * 
 * test()
 * -- 在控制台中：
 * -- lua_debug> print(x)     -- 输出: 10
 * -- lua_debug> x = 20       -- 修改变量
 * -- lua_debug> print(x)     -- 输出: 20
 * -- lua_debug> cont         -- 继续执行
 * ```
 * 
 * 退出方式：
 * - 输入"cont"并回车
 * - 按Ctrl+D（Unix/Linux）或Ctrl+Z（Windows）
 * - 文件结束（如果重定向输入）
 * 
 * 错误处理：
 * - 编译错误会显示在stderr
 * - 运行时错误会显示在stderr
 * - 错误不会终止调试会话
 * 
 * 技术细节：
 * - 使用luaL_loadbuffer编译用户输入
 * - 使用lua_pcall安全执行代码
 * - 保持栈平衡，清理执行结果
 * - 缓冲区大小限制为250字符
 * 
 * 安全性考虑：
 * - 只应在开发和调试环境中使用
 * - 可能被恶意代码利用来执行任意命令
 * - 生产环境应该禁用或限制访问
 * 
 * 性能影响：
 * - 会阻塞程序执行直到用户退出
 * - I/O操作可能影响性能
 * - 不适合在生产环境或高性能要求的地方使用
 * 
 * @param L Lua状态机指针
 * 
 * @return int 返回值数量：0
 * 
 * @see luaL_loadbuffer() Lua代码编译函数
 * @see lua_pcall() Lua代码安全执行函数
 * @see fgets() C标准输入函数
 * 
 * @note 这是一个交互式调试工具，主要用于开发阶段
 */
static int db_debug(lua_State *L) {
    for (;;) {
        char buffer[250];
        fputs("lua_debug> ", stderr);
        if (fgets(buffer, sizeof(buffer), stdin) == 0 ||
            strcmp(buffer, "cont\n") == 0)
            return 0;
        if (luaL_loadbuffer(L, buffer, strlen(buffer), "=(debug command)") ||
            lua_pcall(L, 0, 0, 0)) {
            fputs(lua_tostring(L, -1), stderr);
            fputs("\n", stderr);
        }
        lua_settop(L, 0);
    }
}


/**
 * @brief 错误回溯生成器：生成详细的调用栈回溯信息
 * 
 * 详细说明：
 * 这个函数是Lua错误处理系统的核心组件，用于生成详细的调用栈回溯信息。当程序出现错误时，
 * 它可以显示错误发生的位置以及完整的函数调用链，这对于调试和错误定位至关重要。
 * 
 * 功能特性：
 * - 生成完整的调用栈信息
 * - 显示函数名称、文件位置、行号
 * - 自动处理过长的调用栈（分层显示）
 * - 支持多线程/协程的栈回溯
 * - 提供用户友好的错误信息格式
 * 
 * 实现机制：
 * 1. 解析线程和起始层级参数
 * 2. 检查原始错误消息的有效性
 * 3. 添加"stack traceback:"标题
 * 4. 遍历调用栈的每一层：
 *    a. 获取函数调试信息
 *    b. 格式化函数位置信息
 *    c. 添加函数名称或描述
 * 5. 处理过长栈的简化显示
 * 6. 连接所有信息为最终字符串
 * 
 * 栈层级管理：
 * - LEVELS1 (12): 完整显示的前半部分层数
 * - LEVELS2 (10): 保留显示的后半部分层数
 * - 中间部分用"..."省略表示
 * 
 * 显示格式示例：
 * ```
 * error_message
 * stack traceback:
 *     filename.lua:25: in function 'foo'
 *     filename.lua:10: in function 'bar'
 *     filename.lua:5: in main chunk
 *     [C]: in function 'pcall'
 *     stdin:1: in main chunk
 * ```
 * 
 * 特殊情况处理：
 * - 主程序块显示为"in main chunk"
 * - C函数显示为"?"或具体的C函数信息
 * - 尾调用显示为"?"
 * - 无名函数显示位置信息
 * 
 * 参数处理：
 * - 参数1：可选的线程对象
 * - 参数2：可选的起始层级（数字）
 * - 参数N：原始错误消息（字符串）
 * 
 * 错误消息处理：
 * - 如果没有提供错误消息，使用空字符串
 * - 如果消息不是字符串类型，直接返回原消息
 * - 在原消息后添加换行符和回溯信息
 * 
 * 性能优化：
 * - 限制显示的栈层数避免过长输出
 * - 使用高效的字符串连接方法
 * - 只在需要时获取详细的调试信息
 * 
 * 线程安全性：
 * - 支持跨线程的栈回溯
 * - 每个线程有独立的调用栈
 * - 安全处理线程间的信息传递
 * 
 * 使用场景：
 * - 作为lua_pcall的错误处理函数
 * - 在xpcall中提供详细的错误信息
 * - 调试工具中的错误分析
 * - 自动化测试的错误报告
 * 
 * 配置说明：
 * - LEVELS1=12：前半部分完整显示的层数
 * - LEVELS2=10：后半部分保留显示的层数
 * - 总显示层数在深度调用时被限制
 * 
 * @param L Lua状态机指针
 * @param 1 可选的线程对象
 * @param 2 可选的起始层级（整数）
 * @param N 原始错误消息（任意类型，通常是字符串）
 * 
 * @return int 返回值数量：1（完整的错误消息+回溯信息）
 * 
 * @see lua_getstack() 栈帧获取API
 * @see lua_getinfo() 调试信息获取API
 * @see getthread() 线程解析函数
 * @see lua_pushfstring() 格式化字符串函数
 * @see lua_concat() 字符串连接函数
 * 
 * @note 这是Lua错误处理的标准实现，被广泛用于错误报告
 */
#define LEVELS1	12	/* size of the first part of the stack */
#define LEVELS2	10	/* size of the second part of the stack */

static int db_errorfb(lua_State *L) {
    int level;
    int firstpart = 1;
    int arg;
    lua_State *L1 = getthread(L, &arg);
    lua_Debug ar;
    if (lua_isnumber(L, arg+2)) {
        level = (int)lua_tointeger(L, arg+2);
        lua_pop(L, 1);
    }
    else
        level = (L == L1) ? 1 : 0;
    if (lua_gettop(L) == arg)
        lua_pushliteral(L, "");
    else if (!lua_isstring(L, arg+1)) return 1;
    else lua_pushliteral(L, "\n");
    lua_pushliteral(L, "stack traceback:");
    while (lua_getstack(L1, level++, &ar)) {
        if (level > LEVELS1 && firstpart) {
            if (!lua_getstack(L1, level+LEVELS2, &ar))
                level--;
            else {
                lua_pushliteral(L, "\n\t...");
                while (lua_getstack(L1, level+LEVELS2, &ar))
                    level++;
            }
            firstpart = 0;
            continue;
        }
        lua_pushliteral(L, "\n\t");
        lua_getinfo(L1, "Snl", &ar);
        lua_pushfstring(L, "%s:", ar.short_src);
        if (ar.currentline > 0)
            lua_pushfstring(L, "%d:", ar.currentline);
        if (*ar.namewhat != '\0')
            lua_pushfstring(L, " in function " LUA_QS, ar.name);
        else {
            if (*ar.what == 'm')
                lua_pushfstring(L, " in main chunk");
            else if (*ar.what == 'C' || *ar.what == 't')
                lua_pushliteral(L, " ?");
            else
                lua_pushfstring(L, " in function <%s:%d>",
                               ar.short_src, ar.linedefined);
        }
        lua_concat(L, lua_gettop(L) - arg);
    }
    lua_concat(L, lua_gettop(L) - arg);
    return 1;
}


/**
 * @brief 调试库函数注册表：定义所有可供Lua代码调用的调试函数
 * 
 * 详细说明：
 * 这个数组定义了Lua调试库中所有对外提供的函数及其对应的C实现。当调试库被加载时，
 * 这些函数会被注册到Lua的全局命名空间中，使得Lua代码可以调用这些强大的调试功能。
 * 
 * 函数列表：
 * - debug: 交互式调试控制台
 * - getfenv: 获取对象环境表
 * - gethook: 获取当前钩子配置
 * - getinfo: 获取函数或栈帧的详细信息
 * - getlocal: 获取局部变量
 * - getregistry: 获取全局注册表
 * - getmetatable: 获取对象元表
 * - getupvalue: 获取函数上值
 * - setfenv: 设置对象环境表
 * - sethook: 设置调试钩子
 * - setlocal: 设置局部变量
 * - setmetatable: 设置对象元表
 * - setupvalue: 设置函数上值
 * - traceback: 生成错误回溯信息
 * 
 * 安全性考虑：
 * 调试库提供了非常强大的内省和修改能力，在生产环境中使用时需要谨慎：
 * - 可以访问和修改程序的任何部分
 * - 可能被恶意代码利用
 * - 可能影响程序性能
 * - 建议在生产环境中禁用或限制访问
 * 
 * 性能影响：
 * - 大部分函数性能开销较低
 * - 钩子函数可能显著影响性能
 * - 频繁的调试操作可能影响程序执行速度
 * 
 * @see luaL_Reg 函数注册结构定义
 * @see luaL_register() 函数注册机制
 */
static const luaL_Reg dblib[] = {
    {"debug", db_debug},
    {"getfenv", db_getfenv},
    {"gethook", db_gethook},
    {"getinfo", db_getinfo},
    {"getlocal", db_getlocal},
    {"getregistry", db_getregistry},
    {"getmetatable", db_getmetatable},
    {"getupvalue", db_getupvalue},
    {"setfenv", db_setfenv},
    {"sethook", db_sethook},
    {"setlocal", db_setlocal},
    {"setmetatable", db_setmetatable},
    {"setupvalue", db_setupvalue},
    {"traceback", db_errorfb},
    {NULL, NULL}
};

/**
 * @brief Lua调试库开放函数：初始化并注册调试库
 * 
 * 详细说明：
 * 这是Lua调试库的入口点函数，负责将调试库的所有功能注册到Lua虚拟机中。
 * 按照Lua库的标准约定，这个函数名为luaopen_debug，可以被require()自动调用。
 * 
 * 实现机制：
 * 1. 调用luaL_register注册所有调试函数
 * 2. 使用LUA_DBLIBNAME作为库名称（通常是"debug"）
 * 3. 将dblib数组中的所有函数注册到debug表中
 * 4. 返回1表示在栈上留下了一个返回值（debug表）
 * 
 * 注册后的使用方式：
 * ```lua
 * -- 直接调用（如果调试库已加载）
 * debug.getinfo(1)
 * debug.sethook(my_hook, "clr")
 * 
 * -- 通过require加载（如果未预加载）
 * local debug = require("debug")
 * debug.traceback()
 * ```
 * 
 * 标准库集成：
 * - 通常作为Lua标准库的一部分预加载
 * - 可以通过luaL_openlibs()一次性加载所有标准库
 * - 也可以单独加载：luaopen_debug(L)
 * 
 * 安全性配置：
 * 在某些环境中，可能需要：
 * - 不加载调试库（安全考虑）
 * - 加载后删除某些危险函数
 * - 替换为受限版本的实现
 * - 在沙箱环境中禁用访问
 * 
 * 兼容性说明：
 * - 符合Lua 5.1标准库规范
 * - 与其他版本的调试库API基本兼容
 * - 返回值为库表，符合标准约定
 * 
 * 初始化影响：
 * - 创建全局debug表
 * - 注册所有调试函数
 * - 不会自动设置钩子或修改程序行为
 * - 为调试功能提供必要的基础设施
 * 
 * @param L Lua状态机指针
 * 
 * @return int 返回值数量：1（debug库表）
 * 
 * @see luaL_register() Lua库注册函数
 * @see LUA_DBLIBNAME 调试库名称常量
 * @see dblib 调试函数注册表
 * @see luaL_openlibs() 标准库批量加载函数
 * 
 * @note 这是标准的Lua库加载函数，遵循Lua库开发规范
 */
LUALIB_API int luaopen_debug(lua_State *L) {
    luaL_register(L, LUA_DBLIBNAME, dblib);
    return 1;
}

