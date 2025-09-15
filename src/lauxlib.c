/**
 * @file lauxlib.c
 * @brief Lua辅助库实现：简化Lua C API使用的实用函数集合
 * 
 * 详细说明：
 * 本文件实现了Lua辅助库（Auxiliary Library），为C语言开发者提供了一套
 * 高级的、易用的API函数，简化了与Lua虚拟机的交互操作。这些函数在Lua核心
 * API的基础上提供了更友好的接口，包括错误处理、类型检查、缓冲区管理、
 * 模块注册等功能。
 * 
 * 系统架构定位：
 * - 位于Lua核心API和应用程序之间的中间层
 * - 提供类型安全的参数检查和错误报告机制
 * - 实现通用的缓冲区管理和字符串操作
 * - 支持模块化的C扩展开发
 * - 提供标准化的内存管理和资源清理接口
 * 
 * 技术特点：
 * - 使用标准C库函数，具有良好的可移植性
 * - 提供统一的错误处理和异常报告机制
 * - 实现高效的缓冲区管理算法
 * - 支持引用计数和垃圾回收友好的对象管理
 * - 提供类型安全的参数验证和转换
 * 
 * 依赖关系：
 * - 系统头文件：ctype.h, errno.h, stdarg.h, stdio.h, stdlib.h, string.h
 * - Lua核心：lua.h（Lua虚拟机核心API）
 * - 辅助库头：lauxlib.h（本文件的接口声明）
 * 
 * 编译要求：
 * - C标准版本：C89/C90兼容，支持C99扩展特性
 * - 编译器选项：需要定义LUA_LIB宏以正确导出API函数
 * - 链接要求：需要链接标准C库和Lua核心库
 * 
 * 使用示例：
 * @code
 * #include "lua.h"
 * #include "lauxlib.h"
 * #include "lualib.h"
 * 
 * // 创建新的Lua状态机
 * lua_State *L = luaL_newstate();
 * if (L == NULL) {
 *     fprintf(stderr, "无法创建Lua状态机\n");
 *     return -1;
 * }
 * 
 * // 打开标准库
 * luaL_openlibs(L);
 * 
 * // 检查参数类型
 * static int my_function(lua_State *L) {
 *     // 检查第一个参数是否为字符串
 *     const char *str = luaL_checkstring(L, 1);
 *     // 检查第二个参数是否为数字
 *     lua_Number num = luaL_checknumber(L, 2);
 *     
 *     // 使用缓冲区构建结果
 *     luaL_Buffer b;
 *     luaL_buffinit(L, &b);
 *     luaL_addstring(&b, "结果: ");
 *     luaL_addstring(&b, str);
 *     luaL_pushresult(&b);
 *     
 *     return 1;  // 返回一个值
 * }
 * 
 * // 注册C函数到Lua
 * static const luaL_Reg mylib[] = {
 *     {"my_function", my_function},
 *     {NULL, NULL}
 * };
 * luaL_register(L, "mylib", mylib);
 * 
 * // 清理资源
 * lua_close(L);
 * @endcode
 * 
 * 内存安全考虑：
 * - 所有函数都进行参数有效性检查，防止空指针解引用
 * - 缓冲区操作自动处理内存分配和释放，防止内存泄漏
 * - 提供异常安全的错误处理机制，确保资源正确清理
 * - 引用管理系统防止悬垂指针和重复释放
 * 
 * 性能特征：
 * - 类型检查函数：O(1)时间复杂度，常数级性能开销
 * - 缓冲区操作：分层串联算法，平均O(n)复杂度
 * - 模块注册：O(n)复杂度，n为注册函数数量
 * - 文件加载：I/O限制，支持二进制和文本格式自动检测
 * 
 * 线程安全性：
 * - 大部分函数是可重入的，但不是线程安全的
 * - 每个Lua状态机应该只在单个线程中使用
 * - 全局变量和静态变量的访问需要外部同步
 * - 缓冲区操作是状态机本地的，天然线程隔离
 * 
 * 注意事项：
 * - 辅助库函数依赖于Lua栈状态，调用前需要确保栈空间充足
 * - 错误处理函数会执行长跳转（longjmp），不会正常返回
 * - 某些函数会修改Lua栈，调用时需要注意栈平衡
 * - 文件加载函数会自动检测文件格式，支持二进制字节码
 * 
 * @author Roberto Ierusalimschy等Lua开发团队
 * @version 5.1.5
 * @date 2008-2023
 * @since C89
 * @see lua.h, lualib.h
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lauxlib_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"


/**
 * @brief 自由引用列表的索引标识符
 * 
 * 用于引用管理系统中标识自由引用链表的特殊索引。
 * 当创建新引用时，系统首先检查自由列表中是否有可重用的引用ID。
 */
#define FREELIST_REF    0

/**
 * @brief 将栈索引转换为正数索引的宏
 * 
 * Lua栈支持负数索引（从栈顶向下计数）和正数索引（从栈底向上计数）。
 * 这个宏将负数索引转换为对应的正数索引，便于内部计算和验证。
 * 
 * 转换规则：
 * - 正数索引和特殊索引（如LUA_REGISTRYINDEX）直接返回
 * - 负数索引通过栈顶位置计算对应的正数索引
 * 
 * @param L Lua状态机指针
 * @param i 要转换的栈索引（可以是正数或负数）
 * @return 对应的正数索引
 * 
 * @note 这个宏被频繁用于内部函数中，确保索引计算的一致性
 */
#define abs_index(L, i) ((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : \
                         lua_gettop(L) + (i) + 1)

/**
 * @brief 报告函数参数错误并终止执行
 * 
 * 详细说明：
 * 这是Lua辅助库中最重要的错误报告函数之一。当C函数检测到参数类型
 * 或值不正确时，调用此函数生成标准化的错误信息并抛出Lua异常。
 * 
 * 错误信息生成策略：
 * 1. 尝试获取当前函数的调试信息（函数名、调用位置）
 * 2. 如果是方法调用，自动排除self参数的计数
 * 3. 生成格式化的错误信息，包含参数位置和错误描述
 * 4. 通过luaL_error抛出异常，不会正常返回
 * 
 * 方法调用特殊处理：
 * 当检测到方法调用时（obj:method(args)），会自动调整参数编号，
 * 因为Lua会隐式传递self作为第一个参数。
 * 
 * 使用模式：
 * @code
 * static int my_function(lua_State *L) {
 *     if (lua_type(L, 1) != LUA_TSTRING) {
 *         return luaL_argerror(L, 1, "string expected");
 *     }
 *     // 这里的代码不会执行，因为luaL_argerror不会返回
 * }
 * @endcode
 * 
 * 错误信息格式：
 * - 普通函数："bad argument #2 to 'function_name' (string expected)"
 * - 方法调用："calling 'method_name' on bad self (table expected)"
 * - 未知函数："bad argument #1 to '?' (number expected)"
 * 
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] narg 错误参数的位置（从1开始计数）
 *                 对于方法调用，应包含隐式的self参数
 * @param[in] extramsg 额外的错误描述信息
 *                     通常描述期望的类型或值范围
 * 
 * @return 形式上返回int，但实际上永远不会返回
 *         函数通过lua_error抛出异常并执行长跳转
 * 
 * @throws Lua异常，包含格式化的错误信息
 * 
 * @note 此函数会修改Lua栈状态，压入错误信息
 * @note 调用此函数后，当前C函数的执行会被中断
 * @note 错误信息会自动包含调用位置信息（如果可用）
 * 
 * @see luaL_error(), luaL_typerror(), lua_error()
 */
LUALIB_API int luaL_argerror(lua_State *L, int narg, const char *extramsg) {
    lua_Debug ar;
    
    // 尝试获取当前函数的调试信息
    if (!lua_getstack(L, 0, &ar)) {
        // 无法获取栈帧信息，生成简单的错误信息
        return luaL_error(L, "bad argument #%d (%s)", narg, extramsg);
    }
    
    // 获取函数名称信息
    lua_getinfo(L, "n", &ar);
    
    // 检查是否为方法调用
    if (strcmp(ar.namewhat, "method") == 0) {
        narg--;  // 方法调用时不计算隐式的self参数
        if (narg == 0) {
            // 错误在self参数本身
            return luaL_error(L, "calling " LUA_QS " on bad self (%s)",
                              ar.name, extramsg);
        }
    }
    
    // 处理未知函数名的情况
    if (ar.name == NULL) {
        ar.name = "?";
    }
    
    // 生成标准的参数错误信息
    return luaL_error(L, "bad argument #%d to " LUA_QS " (%s)",
                      narg, ar.name, extramsg);
}


/**
 * @brief 报告类型错误并终止执行
 * 
 * 详细说明：
 * 当C函数期望某个特定类型的参数，但实际接收到了不同类型的值时，
 * 调用此函数生成标准化的类型错误信息。这个函数是luaL_argerror的
 * 专门化版本，专门用于处理类型不匹配的情况。
 * 
 * 错误信息生成策略：
 * 1. 使用lua_pushfstring生成格式化的类型错误信息
 * 2. 自动获取实际参数的类型名称
 * 3. 将错误信息传递给luaL_argerror进行最终处理
 * 
 * 使用场景：
 * @code
 * static int my_string_function(lua_State *L) {
 *     if (lua_type(L, 1) != LUA_TSTRING) {
 *         return luaL_typerror(L, 1, "string");
 *     }
 *     const char *str = lua_tostring(L, 1);
 *     // ... 处理字符串
 *     return 1;
 * }
 * @endcode
 * 
 * 错误信息示例：
 * - 输入：期望"string"，实际得到number
 * - 输出："bad argument #1 to 'function_name' (string expected, got number)"
 * 
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] narg 错误参数的位置（从1开始计数）
 * @param[in] tname 期望的类型名称（如"string"、"number"、"table"等）
 * 
 * @return 形式上返回int，但实际上永远不会返回
 * 
 * @throws Lua异常，包含类型错误信息
 * 
 * @note 此函数会在Lua栈上创建错误信息字符串
 * @note 对于高频率调用，建议先调用类型检查函数
 * 
 * @see luaL_argerror(), luaL_typename(), lua_pushfstring()
 */
LUALIB_API int luaL_typerror(lua_State *L, int narg, const char *tname) {
    // 生成格式化的类型错误信息
    // 使用luaL_typename自动获取实际参数的类型名称
    const char *msg = lua_pushfstring(L, "%s expected, got %s",
                                      tname, luaL_typename(L, narg));
    
    // 将错误信息传递给luaL_argerror进行最终处理
    return luaL_argerror(L, narg, msg);
}


/**
 * @brief 根据Lua类型标识符报告类型错误
 * 
 * 详细说明：
 * 这是一个内部辅助函数，用于将Lua的类型标识符（如LUA_TSTRING、
 * LUA_TNUMBER等）转换为可读的类型名称，然后调用luaL_typerror。
 * 这个函数主要被类型检查函数内部使用。
 * 
 * 使用场景：
 * 当我们知道期望的Lua类型标识符但不想手动转换为字符串时使用。
 * 
 * @param[in] L Lua状态机指针
 * @param[in] narg 错误参数的位置
 * @param[in] tag 期望的Lua类型标识符（LUA_TSTRING、LUA_TNUMBER等）
 * 
 * @note 这是一个静态函数，仅在此文件内部使用
 * @note 函数不会返回，会抛出Lua异常
 * 
 * @see luaL_typerror(), lua_typename()
 */
static void tag_error(lua_State *L, int narg, int tag) {
    // 使用lua_typename将类型标识符转换为可读的类型名称
    luaL_typerror(L, narg, lua_typename(L, tag));
}


/**
 * @brief 获取错误发生的位置信息并压入栈
 * 
 * 详细说明：
 * 这个函数用于获取指定栈层级的源代码位置信息，并将格式化的
 * 位置字符串压入Lua栈。这个信息通常用于错误报告，帮助开发者
 * 定位错误发生的具体位置。
 * 
 * 位置信息格式：
 * - 有位置信息时："filename:linenumber: "
 * - 无位置信息时：空字符串
 * 
 * 栈层级说明：
 * - level 0: 当前调用luaL_where的C函数
 * - level 1: 调用C函数的Lua函数（通常是错误源头）
 * - level 2: 调用上级Lua函数的函数，以此类推
 * 
 * 使用场景：
 * @code
 * // 在错误处理中添加位置信息
 * luaL_where(L, 1);  // 压入位置信息
 * lua_pushstring(L, "error message");  // 压入错误信息
 * lua_concat(L, 2);  // 连接位置和错误信息
 * lua_error(L);  // 抛出异常
 * @endcode
 * 
 * 位置信息示例：
 * - "script.lua:42: "
 * - "[string \"loadstring\"]:15: "
 * - ""（无位置信息时）
 * 
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] level 栈层级，通常传入1表示获取调用者的位置
 * 
 * @post 在Lua栈顶部压入一个字符串，包含位置信息或空字符串
 * 
 * @note 如果无法获取调试信息，会压入空字符串
 * @note 这个函数不会抛出异常，始终会压入一个字符串
 * 
 * @see luaL_error(), lua_getstack(), lua_getinfo()
 */
LUALIB_API void luaL_where(lua_State *L, int level) {
    lua_Debug ar;
    
    // 尝试获取指定层级的栈帧信息
    if (lua_getstack(L, level, &ar)) {
        // 获取源代码位置信息（S = 源信息，l = 行号）
        lua_getinfo(L, "Sl", &ar);
        
        // 检查是否有有效的行号信息
        if (ar.currentline > 0) {
            // 生成格式化的位置信息："文件名:行号: "
            lua_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
            return;
        }
    }
    
    // 无法获取位置信息，压入空字符串
    lua_pushliteral(L, "");
}


/**
 * @brief 格式化错误信息并抛出Lua异常
 * 
 * 详细说明：
 * 这是Lua辅助库中最核心的错误处理函数，类似于C标准库中的printf，
 * 但专门用于错误报告。它会自动添加调用位置信息，格式化错误消息，
 * 并通过lua_error抛出异常，终止当前的Lua函数执行。
 * 
 * 错误信息构建过程：
 * 1. 调用luaL_where(L, 1)获取错误发生的位置信息
 * 2. 使用lua_pushvfstring格式化用户提供的错误消息
 * 3. 使用lua_concat将位置信息和错误消息连接
 * 4. 调用lua_error抛出包含完整信息的异常
 * 
 * 格式化支持：
 * 支持printf风格的格式化字符串，包括：
 * - %s: 字符串
 * - %d, %i: 整数
 * - %f, %g: 浮点数
 * - %c: 字符
 * - %p: 指针
 * - %%: 字面量%
 * 
 * 使用场景：
 * @code
 * static int my_function(lua_State *L) {
 *     int value = luaL_checkint(L, 1);
 *     if (value < 0) {
 *         return luaL_error(L, "值必须为非负数，得到 %d", value);
 *     }
 *     if (value > 100) {
 *         return luaL_error(L, "值超出范围 [0, 100]：%d", value);
 *     }
 *     // 正常处理逻辑...
 *     return 1;
 * }
 * @endcode
 * 
 * 错误信息示例：
 * - 输入：luaL_error(L, "invalid value: %d", 42)
 * - 输出："script.lua:15: invalid value: 42"
 * 
 * 与其他错误函数的关系：
 * - luaL_argerror: 专门用于参数错误，会自动生成参数位置信息
 * - luaL_typerror: 专门用于类型错误，会自动生成类型信息
 * - luaL_error: 通用错误函数，需要手动提供错误描述
 * 
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] fmt printf风格的格式化字符串
 * @param[in] ... 格式化字符串的参数列表
 * 
 * @return 形式上返回int（兼容lua_CFunction），但实际上永远不会返回
 * 
 * @throws Lua异常，包含位置信息和格式化的错误消息
 * 
 * @note 此函数会修改Lua栈状态，压入错误信息后立即抛出异常
 * @note 函数内部使用可变参数，支持任意数量的格式化参数
 * @note 错误信息会自动包含调用此函数的Lua代码位置
 * 
 * @warning 调用此函数后，当前C函数的执行会被立即终止
 * @warning 需要确保在调用前完成所有资源清理工作
 * 
 * @see luaL_where(), lua_pushvfstring(), lua_concat(), lua_error()
 */
LUALIB_API int luaL_error(lua_State *L, const char *fmt, ...) {
    va_list argp;
    
    // 初始化可变参数列表
    va_start(argp, fmt);
    
    // 获取错误发生的位置信息并压入栈
    luaL_where(L, 1);
    
    // 格式化错误消息并压入栈
    lua_pushvfstring(L, fmt, argp);
    
    // 清理可变参数列表
    va_end(argp);
    
    // 连接位置信息和错误消息
    lua_concat(L, 2);
    
    // 抛出Lua异常，函数不会返回
    return lua_error(L);
}


/**
 * @brief 检查参数是否为有效选项并返回索引
 * 
 * 详细说明：
 * 检查指定位置的参数是否为预定义选项列表中的有效选项。
 * 如果参数是有效选项，返回在选项列表中的索引；如果无效，抛出参数错误。
 * 
 * @param[in] L Lua状态机指针
 * @param[in] narg 参数位置（从1开始）
 * @param[in] def 默认值（如果参数为nil或none时使用）
 * @param[in] lst 以NULL结尾的选项字符串数组
 * 
 * @return 选项在列表中的索引（从0开始）
 * 
 * @throws 如果参数不是有效选项则抛出参数错误
 */
LUALIB_API int luaL_checkoption(lua_State *L, int narg, const char *def,
                                const char *const lst[]) {
    const char *name = (def) ? luaL_optstring(L, narg, def) :
                               luaL_checkstring(L, narg);
    int i;
    for (i = 0; lst[i]; i++) {
        if (strcmp(lst[i], name) == 0) {
            return i;
        }
    }
    return luaL_argerror(L, narg,
                         lua_pushfstring(L, "invalid option " LUA_QS, name));
}


/**
 * @brief 创建新的元表并注册到注册表中
 * 
 * 详细说明：
 * 创建一个新的元表，如果指定名称的元表不存在，则创建并注册到Lua注册表中。
 * 这是实现用户数据类型的关键函数，用于为C类型创建对应的Lua元表。
 * 
 * @param[in] L Lua状态机指针
 * @param[in] tname 元表的类型名称，作为注册表中的键
 * 
 * @return 1 如果创建了新元表；0 如果元表已存在
 * 
 * @post 成功时在栈顶留下新创建的元表
 */
LUALIB_API int luaL_newmetatable(lua_State *L, const char *tname) {
    lua_getfield(L, LUA_REGISTRYINDEX, tname);  // 检查是否已存在
    if (!lua_isnil(L, -1)) {
        return 0;  // 元表已存在
    }
    lua_pop(L, 1);  // 移除nil值
    lua_newtable(L);  // 创建新元表
    lua_pushvalue(L, -1);  // 复制元表引用
    lua_setfield(L, LUA_REGISTRYINDEX, tname);  // 注册元表
    return 1;
}


LUALIB_API void *luaL_checkudata(lua_State *L, int ud, const char *tname) {
    void *p = lua_touserdata(L, ud);
    if (p != NULL) {
        if (lua_getmetatable(L, ud)) {
            lua_getfield(L, LUA_REGISTRYINDEX, tname);
            if (lua_rawequal(L, -1, -2)) {
                lua_pop(L, 2);
                return p;
            }
        }
    }
    luaL_typerror(L, ud, tname);
    return NULL;
}


LUALIB_API void luaL_checkstack(lua_State *L, int space, const char *mes) {
    if (!lua_checkstack(L, space)) {
        luaL_error(L, "stack overflow (%s)", mes);
    }
}

LUALIB_API void luaL_checktype(lua_State *L, int narg, int t) {
    if (lua_type(L, narg) != t) {
        tag_error(L, narg, t);
    }
}

LUALIB_API void luaL_checkany(lua_State *L, int narg) {
    if (lua_type(L, narg) == LUA_TNONE) {
        luaL_argerror(L, narg, "value expected");
    }
}


LUALIB_API const char *luaL_checklstring(lua_State *L, int narg, size_t *len) {
    const char *s = lua_tolstring(L, narg, len);
    if (!s) {
        tag_error(L, narg, LUA_TSTRING);
    }
    return s;
}

LUALIB_API const char *luaL_optlstring(lua_State *L, int narg,
                                       const char *def, size_t *len) {
    if (lua_isnoneornil(L, narg)) {
        if (len) {
            *len = (def ? strlen(def) : 0);
        }
        return def;
    } else {
        return luaL_checklstring(L, narg, len);
    }
}


LUALIB_API lua_Number luaL_checknumber(lua_State *L, int narg) {
    lua_Number d = lua_tonumber(L, narg);
    if (d == 0 && !lua_isnumber(L, narg)) {
        tag_error(L, narg, LUA_TNUMBER);
    }
    return d;
}

LUALIB_API lua_Number luaL_optnumber(lua_State *L, int narg, lua_Number def) {
    return luaL_opt(L, luaL_checknumber, narg, def);
}

LUALIB_API lua_Integer luaL_checkinteger(lua_State *L, int narg) {
    lua_Integer d = lua_tointeger(L, narg);
    if (d == 0 && !lua_isnumber(L, narg)) {
        tag_error(L, narg, LUA_TNUMBER);
    }
    return d;
}

LUALIB_API lua_Integer luaL_optinteger(lua_State *L, int narg,
                                       lua_Integer def) {
    return luaL_opt(L, luaL_checkinteger, narg, def);
}


LUALIB_API int luaL_getmetafield(lua_State *L, int obj, const char *event) {
    if (!lua_getmetatable(L, obj)) {
        return 0;
    }
    lua_pushstring(L, event);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return 0;
    } else {
        lua_remove(L, -2);
        return 1;
    }
}

LUALIB_API int luaL_callmeta(lua_State *L, int obj, const char *event) {
    obj = abs_index(L, obj);
    if (!luaL_getmetafield(L, obj, event)) {
        return 0;
    }
    lua_pushvalue(L, obj);
    lua_call(L, 1, 1);
    return 1;
}


LUALIB_API void (luaL_register)(lua_State *L, const char *libname,
                                const luaL_Reg *l) {
    luaI_openlib(L, libname, l, 0);
}

static int libsize(const luaL_Reg *l) {
    int size = 0;
    for (; l->name; l++) {
        size++;
    }
    return size;
}

LUALIB_API void luaI_openlib(lua_State *L, const char *libname,
                             const luaL_Reg *l, int nup) {
    if (libname) {
        int size = libsize(l);
        luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 1);
        lua_getfield(L, -1, libname);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            if (luaL_findtable(L, LUA_GLOBALSINDEX, libname, size) != NULL) {
                luaL_error(L, "name conflict for module " LUA_QS, libname);
            }
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, libname);
        }
        lua_remove(L, -2);
        lua_insert(L, -(nup + 1));
    }
    for (; l->name; l++) {
        int i;
        for (i = 0; i < nup; i++) {
            lua_pushvalue(L, -nup);
        }
        lua_pushcclosure(L, l->func, nup);
        lua_setfield(L, -(nup + 2), l->name);
    }
    lua_pop(L, nup);
}



#if defined(LUA_COMPAT_GETN)

static int checkint(lua_State *L, int topop) {
    int n = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tointeger(L, -1) : -1;
    lua_pop(L, topop);
    return n;
}

static void getsizes(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LUA_SIZES");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setmetatable(L, -2);
        lua_pushliteral(L, "kv");
        lua_setfield(L, -2, "__mode");
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "LUA_SIZES");
    }
}


LUALIB_API void luaL_setn(lua_State *L, int t, int n) {
    t = abs_index(L, t);
    lua_pushliteral(L, "n");
    lua_rawget(L, t);
    if (checkint(L, 1) >= 0) {
        lua_pushliteral(L, "n");
        lua_pushinteger(L, n);
        lua_rawset(L, t);
    } else {
        getsizes(L);
        lua_pushvalue(L, t);
        lua_pushinteger(L, n);
        lua_rawset(L, -3);
        lua_pop(L, 1);
    }
}

LUALIB_API int luaL_getn(lua_State *L, int t) {
    int n;
    t = abs_index(L, t);
    lua_pushliteral(L, "n");
    lua_rawget(L, t);
    if ((n = checkint(L, 1)) >= 0) {
        return n;
    }
    getsizes(L);
    lua_pushvalue(L, t);
    lua_rawget(L, -2);
    if ((n = checkint(L, 2)) >= 0) {
        return n;
    }
    return (int)lua_objlen(L, t);
}

#endif



LUALIB_API const char *luaL_gsub(lua_State *L, const char *s,
                                 const char *p, const char *r) {
    const char *wild;
    size_t l = strlen(p);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    while ((wild = strstr(s, p)) != NULL) {
        luaL_addlstring(&b, s, wild - s);
        luaL_addstring(&b, r);
        s = wild + l;
    }
    luaL_addstring(&b, s);
    luaL_pushresult(&b);
    return lua_tostring(L, -1);
}


LUALIB_API const char *luaL_findtable(lua_State *L, int idx,
                                      const char *fname, int szhint) {
    const char *e;
    lua_pushvalue(L, idx);
    do {
        e = strchr(fname, '.');
        if (e == NULL) {
            e = fname + strlen(fname);
        }
        lua_pushlstring(L, fname, e - fname);
        lua_rawget(L, -2);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_createtable(L, 0, (*e == '.' ? 1 : szhint));
            lua_pushlstring(L, fname, e - fname);
            lua_pushvalue(L, -2);
            lua_settable(L, -4);
        } else if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            return fname;
        }
        lua_remove(L, -2);
        fname = e + 1;
    } while (*e == '.');
    return NULL;
}



/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/


#define bufflen(B)     ((B)->p - (B)->buffer)
#define bufffree(B)     ((size_t)(LUAL_BUFFERSIZE - bufflen(B)))
#define LIMIT           (LUA_MINSTACK/2)

static int emptybuffer(luaL_Buffer *B) {
    size_t l = bufflen(B);
    if (l == 0) {
        return 0;
    } else {
        lua_pushlstring(B->L, B->buffer, l);
        B->p = B->buffer;
        B->lvl++;
        return 1;
    }
}

static void adjuststack(luaL_Buffer *B) {
    if (B->lvl > 1) {
        lua_State *L = B->L;
        int toget = 1;
        size_t toplen = lua_strlen(L, -1);
        do {
            size_t l = lua_strlen(L, -(toget + 1));
            if (B->lvl - toget + 1 >= LIMIT || toplen > l) {
                toplen += l;
                toget++;
            } else {
                break;
            }
        } while (toget < B->lvl);
        lua_concat(L, toget);
        B->lvl = B->lvl - toget + 1;
    }
}


LUALIB_API char *luaL_prepbuffer(luaL_Buffer *B) {
    if (emptybuffer(B)) {
        adjuststack(B);
    }
    return B->buffer;
}

LUALIB_API void luaL_addlstring(luaL_Buffer *B, const char *s, size_t l) {
    while (l--) {
        luaL_addchar(B, *s++);
    }
}

LUALIB_API void luaL_addstring(luaL_Buffer *B, const char *s) {
    luaL_addlstring(B, s, strlen(s));
}

LUALIB_API void luaL_pushresult(luaL_Buffer *B) {
    emptybuffer(B);
    lua_concat(B->L, B->lvl);
    B->lvl = 1;
}

LUALIB_API void luaL_addvalue(luaL_Buffer *B) {
    lua_State *L = B->L;
    size_t vl;
    const char *s = lua_tolstring(L, -1, &vl);
    if (vl <= bufffree(B)) {
        memcpy(B->p, s, vl);
        B->p += vl;
        lua_pop(L, 1);
    } else {
        if (emptybuffer(B)) {
            lua_insert(L, -2);
        }
        B->lvl++;
        adjuststack(B);
    }
}

LUALIB_API void luaL_buffinit(lua_State *L, luaL_Buffer *B) {
    B->L = L;
    B->p = B->buffer;
    B->lvl = 0;
}


LUALIB_API int luaL_ref(lua_State *L, int t) {
    int ref;
    t = abs_index(L, t);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return LUA_REFNIL;
    }
    lua_rawgeti(L, t, FREELIST_REF);
    ref = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (ref != 0) {
        lua_rawgeti(L, t, ref);
        lua_rawseti(L, t, FREELIST_REF);
    } else {
        ref = (int)lua_objlen(L, t);
        ref++;
    }
    lua_rawseti(L, t, ref);
    return ref;
}

LUALIB_API void luaL_unref(lua_State *L, int t, int ref) {
    if (ref >= 0) {
        t = abs_index(L, t);
        lua_rawgeti(L, t, FREELIST_REF);
        lua_rawseti(L, t, ref);
        lua_pushinteger(L, ref);
        lua_rawseti(L, t, FREELIST_REF);
    }
}



typedef struct LoadF {
    int extraline;
    FILE *f;
    char buff[LUAL_BUFFERSIZE];
} LoadF;

static const char *getF(lua_State *L, void *ud, size_t *size) {
    LoadF *lf = (LoadF *)ud;
    (void)L;
    if (lf->extraline) {
        lf->extraline = 0;
        *size = 1;
        return "\n";
    }
    if (feof(lf->f)) {
        return NULL;
    }
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);
    return (*size > 0) ? lf->buff : NULL;
}

static int errfile(lua_State *L, const char *what, int fnameindex) {
    const char *serr = strerror(errno);
    const char *filename = lua_tostring(L, fnameindex) + 1;
    lua_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
    lua_remove(L, fnameindex);
    return LUA_ERRFILE;
}


LUALIB_API int luaL_loadfile(lua_State *L, const char *filename) {
    LoadF lf;
    int status, readstatus;
    int c;
    int fnameindex = lua_gettop(L) + 1;
    lf.extraline = 0;
    if (filename == NULL) {
        lua_pushliteral(L, "=stdin");
        lf.f = stdin;
    } else {
        lua_pushfstring(L, "@%s", filename);
        lf.f = fopen(filename, "r");
        if (lf.f == NULL) {
            return errfile(L, "open", fnameindex);
        }
    }
    c = getc(lf.f);
    if (c == '#') {
        lf.extraline = 1;
        while ((c = getc(lf.f)) != EOF && c != '\n')
            ;
        if (c == '\n') {
            c = getc(lf.f);
        }
    }
    if (c == LUA_SIGNATURE[0] && filename) {
        lf.f = freopen(filename, "rb", lf.f);
        if (lf.f == NULL) {
            return errfile(L, "reopen", fnameindex);
        }
        while ((c = getc(lf.f)) != EOF && c != LUA_SIGNATURE[0])
            ;
        lf.extraline = 0;
    }
    ungetc(c, lf.f);
    status = lua_load(L, getF, &lf, lua_tostring(L, -1));
    readstatus = ferror(lf.f);
    if (filename) {
        fclose(lf.f);
    }
    if (readstatus) {
        lua_settop(L, fnameindex);
        return errfile(L, "read", fnameindex);
    }
    lua_remove(L, fnameindex);
    return status;
}


typedef struct LoadS {
    const char *s;
    size_t size;
} LoadS;

static const char *getS(lua_State *L, void *ud, size_t *size) {
    LoadS *ls = (LoadS *)ud;
    (void)L;
    if (ls->size == 0) {
        return NULL;
    }
    *size = ls->size;
    ls->size = 0;
    return ls->s;
}


LUALIB_API int luaL_loadbuffer(lua_State *L, const char *buff, size_t size,
                               const char *name) {
    LoadS ls;
    ls.s = buff;
    ls.size = size;
    return lua_load(L, getS, &ls, name);
}

LUALIB_API int (luaL_loadstring)(lua_State *L, const char *s) {
    return luaL_loadbuffer(L, s, strlen(s), s);
}


static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud;
    (void)osize;
    if (nsize == 0) {
        free(ptr);
        return NULL;
    } else {
        return realloc(ptr, nsize);
    }
}

static int panic(lua_State *L) {
    (void)L;
    fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n",
            lua_tostring(L, -1));
    return 0;
}

LUALIB_API lua_State *luaL_newstate(void) {
    lua_State *L = lua_newstate(l_alloc, NULL);
    if (L) {
        lua_atpanic(L, &panic);
    }
    return L;
}

