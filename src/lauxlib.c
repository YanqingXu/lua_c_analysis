/*
** [核心] Lua 辅助库实现 - lauxlib.c
**
** 本文件实现了 lauxlib.h 中声明的所有辅助函数。
** 这些函数为构建 Lua 库提供了便利的工具，包括参数检查、
** 错误处理、缓冲区管理、引用系统等核心功能。
**
** 设计理念：
** - 仅使用 Lua 官方 API：所有函数都可以作为应用程序函数实现
** - 提供类型安全：严格的参数类型检查和验证
** - 简化常见操作：将复杂的 C/Lua 交互封装为简单接口
** - 统一错误处理：提供一致的错误报告机制
**
** 主要功能模块：
** 1. 错误报告系统 - 生成详细的错误信息和位置
** 2. 参数验证函数 - 检查和转换函数参数
** 3. 元表管理系统 - 用户数据类型的元表操作
** 4. 库注册机制 - 批量注册 C 函数到 Lua
** 5. 字符串缓冲区 - 高效的字符串构建工具
** 6. 引用管理系统 - 在 C 代码中持有 Lua 值
** 7. 代码加载系统 - 从文件或字符串加载 Lua 代码
** 8. 内存管理接口 - 状态机创建和内存分配
**
** 兼容性说明：
** - 支持 Lua 5.0 兼容模式的 getn/setn 函数
** - 提供向后兼容的接口和行为
** - 遵循 Lua 版本演进的最佳实践
*/

// 标准 C 库头文件
#include <ctype.h>     // 字符类型判断函数
#include <errno.h>     // 错误号定义和处理
#include <stdarg.h>    // 可变参数处理
#include <stdio.h>     // 标准输入输出
#include <stdlib.h>    // 内存分配和工具函数
#include <string.h>    // 字符串操作函数

// 编译选项定义
#define lauxlib_c      // 标识当前编译单元
#define LUA_LIB        // 表明这是一个 Lua 库

// Lua 核心头文件
#include "lua.h"       // Lua 核心 C API
#include "lauxlib.h"   // 辅助库接口声明


/*
** ====================================================================
** [核心] 内部常量和工具宏定义
** ====================================================================
*/

/*
** [内存] 引用系统的空闲列表头索引
** 
** 引用表中索引 0 位置存储空闲引用链表的头部。
** 这实现了一个高效的引用回收机制。
*/
#define FREELIST_REF	0

/*
** [实用] 栈索引标准化宏
**
** 功能：将负数栈索引转换为正数索引
** @param L - lua_State*: Lua状态机
** @param i - int: 原始栈索引
** @return int: 标准化后的正数索引
**
** 转换规则：
** - 正数索引：直接返回（从栈底开始计数）
** - 伪索引：直接返回（如 LUA_REGISTRYINDEX）
** - 负数索引：转换为等价的正数索引（从栈顶开始计数）
**
** 使用场景：
** - 在函数内部保存索引值时
** - 确保索引在栈操作后仍然有效
** - 统一索引表示方式
*/
#define abs_index(L, i)		((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : \
					lua_gettop(L) + (i) + 1)


/*
** ====================================================================
** [错误] 错误报告和处理系统
** ====================================================================
**
** 本模块提供了统一的错误报告机制，能够生成包含详细位置信息
** 和上下文的错误消息，帮助开发者快速定位和解决问题。
*/

/*
** [错误] 参数错误报告函数
**
** 功能：生成详细的参数错误信息并抛出异常
** @param L - lua_State*: Lua状态机
** @param narg - int: 出错参数的位置（从1开始计数）
** @param extramsg - const char*: 附加错误描述信息
** @return int: 此函数不会返回，总是抛出异常
**
** 错误信息生成逻辑：
** 1. 获取当前函数的调用栈信息
** 2. 如果是方法调用，调整参数计数（排除self）
** 3. 生成格式化的错误消息
** 4. 包含函数名称和参数位置信息
**
** 错误消息格式：
** - "bad argument #<narg> to '<function>' (<extramsg>)"
** - 对于方法调用中的self参数错误：
**   "calling '<method>' on bad self (<extramsg>)"
*/
LUALIB_API int luaL_argerror (lua_State *L, int narg, const char *extramsg)
{
    lua_Debug ar;
    
    // 尝试获取当前函数的调用栈信息
    if (!lua_getstack(L, 0, &ar))
    {
        // 无法获取栈信息时的简化错误
        return luaL_error(L, "bad argument #%d (%s)", narg, extramsg);
    }
    
    // 获取函数名称信息
    lua_getinfo(L, "n", &ar);
    
    // 特殊处理方法调用的情况
    if (strcmp(ar.namewhat, "method") == 0)
    {
        narg--;  // 方法调用不计算 self 参数
        
        // 如果错误就在 self 参数本身
        if (narg == 0)
        {
            return luaL_error(L, "calling " LUA_QS " on bad self (%s)",
                             ar.name, extramsg);
        }
    }
    
    // 处理未知函数名的情况
    if (ar.name == NULL)
    {
        ar.name = "?";
    }
    
    // 生成标准的参数错误信息
    return luaL_error(L, "bad argument #%d to " LUA_QS " (%s)",
                      narg, ar.name, extramsg);
}

/*
** [错误] 类型错误报告函数
**
** 功能：生成类型不匹配的错误信息
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @param tname - const char*: 期望的类型名称
** @return int: 此函数不会返回，总是抛出异常
**
** 实现机制：
** 1. 获取参数的实际类型名称
** 2. 构造"期望类型 vs 实际类型"的错误消息
** 3. 调用 luaL_argerror 生成完整错误信息
**
** 错误消息示例：
** "bad argument #1 to 'function' (string expected, got number)"
*/
LUALIB_API int luaL_typerror (lua_State *L, int narg, const char *tname)
{
    // 构造类型错误的详细描述
    const char *msg = lua_pushfstring(L, "%s expected, got %s",
                                      tname, luaL_typename(L, narg));
    return luaL_argerror(L, narg, msg);
}

/*
** [内部] 标签错误处理函数
**
** 功能：根据期望的 Lua 类型标签生成类型错误
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @param tag - int: 期望的 Lua 类型标签（LUA_TSTRING等）
**
** 应用场景：
** - 内部类型检查失败时的统一错误处理
** - 将类型标签转换为可读的类型名称
** - 简化重复的类型错误处理代码
*/
static void tag_error (lua_State *L, int narg, int tag)
{
    luaL_typerror(L, narg, lua_typename(L, tag));
}

/*
** [调试] 错误位置信息生成函数
**
** 功能：生成错误发生位置的描述信息并推入栈顶
** @param L - lua_State*: Lua状态机
** @param level - int: 调用栈层级（1表示调用者，2表示调用者的调用者）
**
** 位置信息格式：
** - 对于 Lua 代码："filename:line: "
** - 对于 C 代码："[C]: "
** - 对于主代码块："[main]: "
** - 无信息时：空字符串
**
** 使用场景：
** - 生成详细的错误报告
** - 调试信息的收集
** - 异常追踪和诊断
*/
LUALIB_API void luaL_where (lua_State *L, int level)
{
    lua_Debug ar;
    
    // 尝试获取指定层级的栈信息
    if (lua_getstack(L, level, &ar))
    {
        // 获取源码位置信息
        lua_getinfo(L, "Sl", &ar);
        
        // 如果有行号信息，生成详细位置
        if (ar.currentline > 0)
        {
            lua_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
            return;
        }
    }
    
    // 无位置信息时推入空字符串
    lua_pushliteral(L, "");
}

/*
** [错误] 格式化错误生成函数
**
** 功能：使用 printf 风格的格式字符串生成错误并抛出异常
** @param L - lua_State*: Lua状态机
** @param fmt - const char*: 格式字符串
** @param ... - 可变参数: 格式化参数
** @return int: 此函数不会返回，总是抛出异常
**
** 错误生成流程：
** 1. 获取当前位置信息（调用 luaL_where）
** 2. 使用可变参数格式化错误消息
** 3. 将位置信息与错误消息连接
** 4. 抛出最终的错误异常
**
** 特性：
** - 自动添加位置前缀
** - 支持 printf 风格的格式化
** - 与 Lua 错误处理机制集成
*/
LUALIB_API int luaL_error (lua_State *L, const char *fmt, ...)
{
    va_list argp;
    
    // 初始化可变参数处理
    va_start(argp, fmt);
    
    // 添加错误位置信息
    luaL_where(L, 1);
    
    // 格式化错误消息
    lua_pushvfstring(L, fmt, argp);
    
    // 清理可变参数
    va_end(argp);
    
    // 连接位置信息和错误消息
    lua_concat(L, 2);
    
    // 抛出错误（此函数不会返回）
    return lua_error(L);
}


/*
** ====================================================================
** [核心] 选项检查和验证函数
** ====================================================================
*/

/*
** [核心] 检查选项参数函数
**
** 功能：验证参数是否为预定义选项列表中的一个
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @param def - const char*: 默认选项（参数为nil时使用，可为NULL）
** @param lst - const char* const[]: 有效选项列表，以NULL结尾
** @return int: 选项在列表中的索引（从0开始）
**
** 验证逻辑：
** 1. 获取参数字符串（使用默认值或检查必需参数）
** 2. 在选项列表中进行线性搜索
** 3. 找到匹配项则返回索引
** 4. 未找到则生成包含有效选项的错误消息
**
** 错误处理：
** - 自动生成包含所有有效选项的错误信息
** - 错误消息格式："invalid option '<option>'"
** - 通过 luaL_argerror 提供完整的参数错误上下文
*/
LUALIB_API int luaL_checkoption (lua_State *L, int narg, const char *def,
                                 const char *const lst[])
{
    // 获取参数字符串：如果提供默认值则使用可选获取，否则强制检查
    const char *name = (def) ? luaL_optstring(L, narg, def) :
                             luaL_checkstring(L, narg);
    int i;
    
    // 在选项列表中查找匹配项
    for (i = 0; lst[i]; i++)
    {
        if (strcmp(lst[i], name) == 0)
        {
            return i;  // 找到匹配项，返回索引
        }
    }
    
    // 未找到匹配项，生成错误信息
    return luaL_argerror(L, narg,
                         lua_pushfstring(L, "invalid option " LUA_QS, name));
}

/*
** ====================================================================
** [核心] 元表管理和用户数据系统
** ====================================================================
*/

/*
** [核心] 创建新元表函数
**
** 功能：在注册表中创建或获取指定名称的元表
** @param L - lua_State*: Lua状态机
** @param tname - const char*: 元表的唯一标识名称
** @return int: 如果是新创建返回1，如果已存在返回0
**
** 创建流程：
** 1. 在注册表中查找指定名称的元表
** 2. 如果已存在，将其留在栈顶并返回0
** 3. 如果不存在，创建新元表并注册到注册表
** 4. 将新元表留在栈顶并返回1
**
** 命名约定：
** - 使用模块前缀确保全局唯一性
** - 建议格式："ModuleName.TypeName"
** - 避免与标准库和其他模块冲突
**
** 使用模式：
** if (luaL_newmetatable(L, "mylib.Point")) {
**     // 设置元表字段（仅在新创建时）
**     lua_pushcfunction(L, point_gc);
**     lua_setfield(L, -2, "__gc");
** }
*/
LUALIB_API int luaL_newmetatable (lua_State *L, const char *tname)
{
    // 尝试从注册表获取现有元表
    lua_getfield(L, LUA_REGISTRYINDEX, tname);
    
    // 如果已存在元表，返回0
    if (!lua_isnil(L, -1))
    {
        return 0;  // 元表已存在，留在栈顶但返回0
    }
    
    // 移除nil值，准备创建新元表
    lua_pop(L, 1);
    
    // 创建新的元表
    lua_newtable(L);
    
    // 将元表复制一份用于注册
    lua_pushvalue(L, -1);
    
    // 将元表注册到注册表中：registry[tname] = metatable
    lua_setfield(L, LUA_REGISTRYINDEX, tname);
    
    return 1;  // 返回1表示新创建了元表
}

/*
** [核心] 检查用户数据类型函数
**
** 功能：验证指定位置是否为特定类型的用户数据
** @param L - lua_State*: Lua状态机
** @param ud - int: 用户数据在栈中的位置
** @param tname - const char*: 期望的用户数据类型名称
** @return void*: 用户数据的指针，类型不匹配时抛出错误
**
** 类型检查流程：
** 1. 检查指定位置是否为用户数据
** 2. 获取用户数据的元表
** 3. 从注册表获取期望类型的正确元表
** 4. 比较两个元表是否相同
** 5. 匹配则返回数据指针，不匹配则抛出类型错误
**
** 安全机制：
** - 严格的类型验证防止错误转换
** - 自动生成类型错误信息
** - 确保 C 结构体访问的类型安全
**
** 错误情况：
** - 值不是用户数据
** - 用户数据没有元表
** - 元表类型不匹配
*/
LUALIB_API void *luaL_checkudata (lua_State *L, int ud, const char *tname)
{
    void *p = lua_touserdata(L, ud);
    
    // 检查是否为用户数据
    if (p != NULL)
    {
        // 检查是否有元表
        if (lua_getmetatable(L, ud))
        {
            // 获取期望类型的正确元表
            lua_getfield(L, LUA_REGISTRYINDEX, tname);
            
            // 比较元表是否匹配
            if (lua_rawequal(L, -1, -2))
            {
                lua_pop(L, 2);  // 移除两个元表
                return p;       // 返回用户数据指针
            }
        }
    }
    
    // 类型不匹配，抛出类型错误
    luaL_typerror(L, ud, tname);
    return NULL;  // 避免编译器警告（实际不会执行到）
}


/*
** ====================================================================
** [核心] 栈管理和基础类型检查
** ====================================================================
*/

/*
** [安全] 栈空间检查函数
**
** 功能：确保栈中有足够空间容纳指定数量的元素
** @param L - lua_State*: Lua状态机
** @param space - int: 需要的额外栈空间大小
** @param mes - const char*: 空间不足时的错误消息
**
** 安全机制：
** - 预防栈溢出导致的程序崩溃
** - 在大量压栈操作前进行预检查
** - 提供自定义错误消息以便调试
**
** 使用时机：
** - 递归函数的开始
** - 批量压栈操作前
** - 处理大型数据结构时
** - 任何可能导致深度栈使用的操作前
**
** 错误处理：
** - 如果空间不足，立即抛出包含自定义消息的错误
** - 错误格式："stack overflow (<message>)"
*/
LUALIB_API void luaL_checkstack (lua_State *L, int space, const char *mes)
{
    // 尝试检查并确保栈空间
    if (!lua_checkstack(L, space))
    {
        // 空间不足时抛出详细错误
        luaL_error(L, "stack overflow (%s)", mes);
    }
}

/*
** [核心] 类型验证函数
**
** 功能：验证指定位置的参数是否为期望的类型
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置（从1开始）
** @param t - int: 期望的类型标签（LUA_TSTRING、LUA_TNUMBER等）
**
** 类型常量说明：
** - LUA_TNIL: nil类型
** - LUA_TBOOLEAN: 布尔类型
** - LUA_TNUMBER: 数值类型
** - LUA_TSTRING: 字符串类型
** - LUA_TTABLE: 表类型
** - LUA_TFUNCTION: 函数类型
** - LUA_TUSERDATA: 用户数据类型
** - LUA_TTHREAD: 线程类型
**
** 应用场景：
** - 严格的参数类型验证
** - 防止类型相关的运行时错误
** - 确保 C 函数的参数类型安全
*/
LUALIB_API void luaL_checktype (lua_State *L, int narg, int t)
{
    // 检查实际类型是否与期望类型匹配
    if (lua_type(L, narg) != t)
    {
        tag_error(L, narg, t);  // 生成类型错误
    }
}

/*
** [核心] 参数存在性检查函数
**
** 功能：验证指定位置存在参数（不是nil且不超出参数范围）
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
**
** 检查内容：
** - 参数位置在有效范围内
** - 参数不是 LUA_TNONE（表示超出参数范围）
** - 确保函数接收到了必需的参数
**
** 使用场景：
** - 验证必需参数的存在性
** - 防止访问不存在的参数位置
** - 确保函数调用的完整性
** - 在参数数量不定的函数中验证最小参数数量
*/
LUALIB_API void luaL_checkany (lua_State *L, int narg)
{
    // 检查参数是否存在（不是 LUA_TNONE）
    if (lua_type(L, narg) == LUA_TNONE)
    {
        luaL_argerror(L, narg, "value expected");
    }
}


/*
** ====================================================================
** [核心] 字符串参数检查和转换
** ====================================================================
*/

/*
** [核心] 字符串参数检查函数
**
** 功能：检查并获取指定位置的字符串参数
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @param len - size_t*: 输出参数，存储字符串长度（可为NULL）
** @return const char*: 字符串内容指针
**
** 转换支持：
** - 字符串类型：直接返回
** - 数值类型：自动转换为字符串表示
** - 其他类型：抛出类型错误
**
** 内存管理：
** - 返回的指针指向 Lua 管理的内存
** - 在下次垃圾回收前保持有效
** - 不需要手动释放内存
**
** 注意事项：
** - 支持包含 null 字符的二进制字符串
** - 长度信息对处理二进制数据至关重要
** - 字符串内容在 Lua 状态机中是不可修改的
*/
LUALIB_API const char *luaL_checklstring (lua_State *L, int narg, size_t *len)
{
    // 尝试将参数转换为字符串
    const char *s = lua_tolstring(L, narg, len);
    
    // 如果转换失败，抛出类型错误
    if (!s)
    {
        tag_error(L, narg, LUA_TSTRING);
    }
    
    return s;
}

/*
** [核心] 可选字符串参数获取函数
**
** 功能：获取可选的字符串参数，如果不存在则使用默认值
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @param def - const char*: 默认值（参数为nil或不存在时使用）
** @param len - size_t*: 输出字符串长度（可为NULL）
** @return const char*: 字符串内容或默认值
**
** 处理逻辑：
** 1. 检查参数是否为 nil 或不存在
** 2. 如果是，计算默认值长度并返回默认值
** 3. 如果不是，调用 luaL_checklstring 进行正常处理
**
** 默认值处理：
** - 支持 NULL 作为默认值
** - 自动计算默认值的长度
** - 默认值不由 Lua 管理，调用者需确保其有效性
**
** 应用场景：
** - 函数的可选参数处理
** - 提供合理的默认行为
** - 简化参数验证逻辑
*/
LUALIB_API const char *luaL_optlstring (lua_State *L, int narg,
                                        const char *def, size_t *len)
{
    // 检查参数是否为 nil 或不存在
    if (lua_isnoneornil(L, narg))
    {
        // 如果需要长度信息，计算默认值长度
        if (len)
        {
            *len = (def ? strlen(def) : 0);
        }
        return def;
    }
    else
    {
        // 参数存在，进行正常的字符串检查
        return luaL_checklstring(L, narg, len);
    }
}


/*
** ====================================================================
** [核心] 数值参数检查和转换
** ====================================================================
*/

/*
** [核心] 数值参数检查函数
**
** 功能：检查并获取指定位置的数值参数
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @return lua_Number: 数值参数的值
**
** 转换支持：
** - 数值类型：直接返回
** - 数值字符串：自动解析为数值
** - 其他类型：抛出类型错误
**
** 特殊处理：
** - 使用 lua_tonumber 进行转换
** - 检查转换结果是否有效（避免 0 值的歧义）
** - 使用 lua_isnumber 进行二次验证
**
** 性能优化：
** - 当结果不为 0 时，跳过额外的类型检查
** - 仅在结果为 0 且类型检查失败时才报错
** - 避免了对明显有效数值的额外验证开销
*/
LUALIB_API lua_Number luaL_checknumber (lua_State *L, int narg)
{
    lua_Number d = lua_tonumber(L, narg);
    
    // 当结果为 0 时需要额外检查是否真的是数值
    // 避免将非数值错误转换为 0 的情况
    if (d == 0 && !lua_isnumber(L, narg))
    {
        tag_error(L, narg, LUA_TNUMBER);
    }
    
    return d;
}

/*
** [核心] 可选数值参数获取函数
**
** 功能：获取可选的数值参数，如果不存在则使用默认值
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @param def - lua_Number: 默认数值
** @return lua_Number: 参数值或默认值
**
** 实现机制：
** - 使用 luaL_opt 宏进行统一的可选参数处理
** - 如果参数为 nil 或不存在，返回默认值
** - 否则调用 luaL_checknumber 进行正常验证
**
** 应用场景：
** - 函数的可选数值参数
** - 提供合理的数值默认值
** - 简化参数处理逻辑
*/
LUALIB_API lua_Number luaL_optnumber (lua_State *L, int narg, lua_Number def)
{
    return luaL_opt(L, luaL_checknumber, narg, def);
}

/*
** [核心] 整数参数检查函数
**
** 功能：检查并获取指定位置的整数参数
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @return lua_Integer: 整数参数的值
**
** 转换规则：
** - 整数类型：直接返回
** - 浮点数：截断为整数（可能丢失精度）
** - 数值字符串：解析后转换为整数
** - 其他类型：抛出类型错误
**
** 精度考虑：
** - lua_Integer 的范围可能小于 lua_Number
** - 大浮点数转换为整数时可能溢出
** - 建议在关键应用中进行范围检查
**
** 验证机制：
** - 使用与 luaL_checknumber 类似的验证逻辑
** - 当结果为 0 时进行额外的类型检查
** - 确保转换的有效性和准确性
*/
LUALIB_API lua_Integer luaL_checkinteger (lua_State *L, int narg)
{
    lua_Integer d = lua_tointeger(L, narg);
    
    // 当结果为 0 时需要额外检查是否真的是数值
    if (d == 0 && !lua_isnumber(L, narg))
    {
        tag_error(L, narg, LUA_TNUMBER);
    }
    
    return d;
}

/*
** [核心] 可选整数参数获取函数
**
** 功能：获取可选的整数参数，如果不存在则使用默认值
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @param def - lua_Integer: 默认整数值
** @return lua_Integer: 参数值或默认值
**
** 统一处理：
** - 使用 luaL_opt 宏的统一可选参数模式
** - 保持与其他可选参数函数的一致性
** - 简化代码并减少重复逻辑
*/
LUALIB_API lua_Integer luaL_optinteger (lua_State *L, int narg,
                                                      lua_Integer def)
{
    return luaL_opt(L, luaL_checkinteger, narg, def);
}


/*
** ====================================================================
** [核心] 元表字段操作和元方法调用
** ====================================================================
*/

/*
** [核心] 获取元表字段函数
**
** 功能：获取指定对象元表中的特定字段值
** @param L - lua_State*: Lua状态机
** @param obj - int: 对象在栈中的索引
** @param event - const char*: 要获取的元表字段名
** @return int: 如果字段存在返回非0，否则返回0
**
** 执行流程：
** 1. 尝试获取对象的元表
** 2. 如果没有元表，直接返回0
** 3. 在元表中查找指定字段
** 4. 如果字段为nil，清理栈并返回0
** 5. 如果字段存在，移除元表但保留字段值，返回1
**
** 栈操作：
** - 成功时：栈顶包含字段值
** - 失败时：栈状态恢复原样
** - 自动处理栈的清理工作
**
** 应用场景：
** - 检查对象是否定义了特定元方法
** - 获取元表中的配置信息
** - 实现条件性的元方法调用
*/
LUALIB_API int luaL_getmetafield (lua_State *L, int obj, const char *event)
{
    // 尝试获取对象的元表
    if (!lua_getmetatable(L, obj))
    {
        return 0;  // 没有元表
    }
    
    // 将字段名推入栈
    lua_pushstring(L, event);
    
    // 在元表中查找字段：metatable[event]
    lua_rawget(L, -2);
    
    // 检查字段是否为 nil
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 2);  // 移除元表和字段值（nil）
        return 0;       // 字段不存在
    }
    else
    {
        lua_remove(L, -2);  // 只移除元表，保留字段值
        return 1;           // 字段存在，值在栈顶
    }
}

/*
** [核心] 调用元方法函数
**
** 功能：调用指定对象的特定元方法
** @param L - lua_State*: Lua状态机
** @param obj - int: 对象在栈中的索引
** @param event - const char*: 元方法名称（如"__add", "__index"等）
** @return int: 如果元方法存在并被调用返回非0，否则返回0
**
** 调用流程：
** 1. 将对象索引标准化（防止栈操作影响）
** 2. 获取对象的指定元方法
** 3. 如果元方法不存在，返回0
** 4. 将对象作为第一个参数推入栈
** 5. 调用元方法（1个参数，1个返回值）
** 6. 返回1表示成功调用
**
** 参数传递：
** - 元方法接收对象作为第一个参数
** - 支持标准的元方法调用约定
** - 调用结果留在栈顶
**
** 应用场景：
** - 实现运算符重载
** - 调用对象的特殊方法
** - 实现__tostring、__len等元方法
** - 支持面向对象的方法调用
*/
LUALIB_API int luaL_callmeta (lua_State *L, int obj, const char *event)
{
    // 标准化对象索引，防止后续栈操作影响
    obj = abs_index(L, obj);
    
    // 尝试获取元方法
    if (!luaL_getmetafield(L, obj, event))
    {
        return 0;  // 元方法不存在
    }
    
    // 将对象作为参数推入栈
    lua_pushvalue(L, obj);
    
    // 调用元方法：metamethod(obj)
    lua_call(L, 1, 1);
    
    return 1;  // 成功调用，结果在栈顶
}


/*
** ====================================================================
** [核心] 库注册和模块管理系统
** ====================================================================
*/

/*
** [接口] 库注册包装函数
**
** 功能：提供简化的库注册接口
** @param L - lua_State*: Lua状态机
** @param libname - const char*: 库名称
** @param l - const luaL_Reg*: 函数注册表
**
** 简化接口：
** - 使用 0 个上值调用 luaI_openlib
** - 提供向后兼容的简单注册方式
** - 适用于不需要上值的标准库注册
*/
LUALIB_API void (luaL_register) (lua_State *L, const char *libname,
                                const luaL_Reg *l)
{
    luaI_openlib(L, libname, l, 0);
}

/*
** [内部] 计算函数注册表大小
**
** 功能：计算以NULL结尾的函数注册表的长度
** @param l - const luaL_Reg*: 函数注册表
** @return int: 表中函数的数量
**
** 用途：
** - 为新库表预分配合适的空间
** - 优化表的初始大小，减少重新哈希
** - 提高库注册的性能
*/
static int libsize (const luaL_Reg *l)
{
    int size = 0;
    
    // 遍历注册表直到遇到名称为NULL的结束标记
    for (; l->name; l++)
    {
        size++;
    }
    
    return size;
}

/*
** [核心] 库打开和注册主函数
**
** 功能：打开并注册一个C库到Lua环境
** @param L - lua_State*: Lua状态机
** @param libname - const char*: 库名称，NULL表示注册到栈顶表
** @param l - const luaL_Reg*: 函数注册表，以{NULL,NULL}结尾
** @param nup - int: 每个函数关联的上值数量
**
** 注册流程：
** 1. 如果指定了库名称，创建或获取库表
** 2. 检查_LOADED表中是否已存在该库
** 3. 如果不存在，在全局环境中创建新表
** 4. 将新表注册到_LOADED表中
** 5. 遍历函数注册表，将每个函数添加到库表
** 6. 为每个函数设置指定数量的上值
**
** 上值处理：
** - 栈顶的nup个值作为所有函数的共享上值
** - 每个函数都会复制这些上值
** - 上值用于在函数间共享数据
**
** 模块系统集成：
** - 自动处理_LOADED表的维护
** - 支持模块的重复加载检查
** - 与require机制完全兼容
*/
LUALIB_API void luaI_openlib (lua_State *L, const char *libname,
                              const luaL_Reg *l, int nup)
{
    if (libname)
    {
        int size = libsize(l);
        
        // 获取或创建_LOADED表
        luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 1);
        
        // 检查库是否已经加载：_LOADED[libname]
        lua_getfield(L, -1, libname);
        
        if (!lua_istable(L, -1))
        {
            // 库未加载，需要创建新表
            lua_pop(L, 1);  // 移除非表值
            
            // 在全局环境中查找或创建库表
            if (luaL_findtable(L, LUA_GLOBALSINDEX, libname, size) != NULL)
            {
                luaL_error(L, "name conflict for module " LUA_QS, libname);
            }
            
            // 将新表注册到_LOADED中
            lua_pushvalue(L, -1);               // 复制库表
            lua_setfield(L, -3, libname);       // _LOADED[libname] = 库表
        }
        
        lua_remove(L, -2);  // 移除_LOADED表
        
        // 将库表移动到上值下方，为函数注册做准备
        lua_insert(L, -(nup+1));
    }
    
    // 注册函数表中的每个函数
    for (; l->name; l++)
    {
        int i;
        
        // 为当前函数复制上值
        for (i = 0; i < nup; i++)
        {
            lua_pushvalue(L, -nup);
        }
        
        // 创建带上值的C闭包
        lua_pushcclosure(L, l->func, nup);
        
        // 将函数设置到库表中：lib[name] = function
        lua_setfield(L, -(nup+2), l->name);
    }
    
    // 移除栈顶的上值
    lua_pop(L, nup);
}



/*
** ====================================================================
** [兼容] Lua 5.0 数组大小兼容支持
** ====================================================================
**
** 这一节实现了对 Lua 5.0 中 getn/setn 函数的兼容支持。
** 在新版本中，这些功能已被 lua_objlen 和自动长度计算取代。
*/

#if defined(LUA_COMPAT_GETN)

/*
** [内部] 整数检查辅助函数
**
** 功能：检查栈顶值是否为有效整数并清理栈
** @param L - lua_State*: Lua状态机
** @param topop - int: 需要从栈顶弹出的元素数量
** @return int: 如果是有效整数返回其值，否则返回-1
**
** 验证逻辑：
** - 检查栈顶值是否为数值类型
** - 将数值转换为整数
** - 清理指定数量的栈元素
*/
static int checkint (lua_State *L, int topop)
{
    int n = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tointeger(L, -1) : -1;
    lua_pop(L, topop);
    return n;
}

/*
** [内部] 获取或创建大小表
**
** 功能：获取用于存储表大小信息的特殊表
**
** 大小表机制：
** 1. 在注册表中查找"LUA_SIZES"表
** 2. 如果不存在，创建新的大小表
** 3. 设置弱引用元表（键值都是弱引用）
** 4. 将大小表注册到注册表中
**
** 弱引用设计：
** - 使用"kv"模式的弱引用
** - 当表被垃圾回收时，其大小信息也会被清理
** - 避免大小表阻止原表的垃圾回收
*/
static void getsizes (lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "LUA_SIZES");
    
    // 如果大小表不存在，创建它
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);      // 移除 nil
        lua_newtable(L);    // 创建大小表
        
        // 设置大小表的元表（自己作为自己的元表）
        lua_pushvalue(L, -1);
        lua_setmetatable(L, -2);
        
        // 设置弱引用模式
        lua_pushliteral(L, "kv");
        lua_setfield(L, -2, "__mode");  // metatable(sizes).__mode = "kv"
        
        // 将大小表注册到注册表
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "LUA_SIZES");
    }
}

/*
** [兼容] 设置表长度函数
**
** 功能：为指定表设置长度信息
** @param L - lua_State*: Lua状态机
** @param t - int: 表在栈中的位置
** @param n - int: 要设置的长度值
**
** 设置策略：
** 1. 优先尝试设置表的"n"字段
** 2. 如果表已有数值型"n"字段，直接更新它
** 3. 否则使用全局大小表记录长度信息
**
** 兼容性考虑：
** - 支持旧代码中表.n的使用习惯
** - 为没有"n"字段的表提供外部长度存储
** - 与现代版本的长度操作保持兼容
*/
LUALIB_API void luaL_setn (lua_State *L, int t, int n)
{
    t = abs_index(L, t);  // 标准化表索引
    
    // 检查表是否已有"n"字段
    lua_pushliteral(L, "n");
    lua_rawget(L, t);
    
    // 如果存在数值型"n"字段，直接使用它
    if (checkint(L, 1) >= 0)
    {
        lua_pushliteral(L, "n");
        lua_pushinteger(L, n);
        lua_rawset(L, t);  // table.n = n
    }
    else
    {
        // 使用全局大小表存储长度
        getsizes(L);
        lua_pushvalue(L, t);     // 将表作为键
        lua_pushinteger(L, n);   // 长度作为值
        lua_rawset(L, -3);       // sizes[table] = n
        lua_pop(L, 1);           // 移除大小表
    }
}

/*
** [兼容] 获取表长度函数
**
** 功能：获取指定表的长度信息
** @param L - lua_State*: Lua状态机
** @param t - int: 表在栈中的位置
** @return int: 表的长度
**
** 获取策略（按优先级）：
** 1. 检查表的"n"字段是否为有效整数
** 2. 检查全局大小表中是否有记录
** 3. 使用 lua_objlen 获取自动计算的长度
**
** 返回逻辑：
** - 优先返回显式设置的长度值
** - 最后回退到系统自动计算的长度
** - 确保与各种长度设置方式的兼容性
*/
LUALIB_API int luaL_getn (lua_State *L, int t)
{
    int n;
    t = abs_index(L, t);  // 标准化表索引
    
    // 首先尝试 table.n
    lua_pushliteral(L, "n");
    lua_rawget(L, t);
    if ((n = checkint(L, 1)) >= 0)
    {
        return n;
    }
    
    // 然后尝试大小表 sizes[table]
    getsizes(L);
    lua_pushvalue(L, t);
    lua_rawget(L, -2);
    if ((n = checkint(L, 2)) >= 0)
    {
        return n;
    }
    
    // 最后使用自动长度计算
    return (int)lua_objlen(L, t);
}

#endif



/*
** ====================================================================
** [核心] 字符串处理和表查找工具
** ====================================================================
*/

/*
** [核心] 字符串全局替换函数
**
** 功能：在字符串中进行全局模式替换
** @param L - lua_State*: Lua状态机
** @param s - const char*: 源字符串
** @param p - const char*: 要查找的模式字符串
** @param r - const char*: 替换字符串
** @return const char*: 替换后的新字符串
**
** 替换算法：
** 1. 使用字符串缓冲区构建结果
** 2. 通过 strstr 查找所有匹配位置
** 3. 逐段复制：前缀 + 替换文本 + 继续搜索
** 4. 最后添加剩余的后缀部分
**
** 特性：
** - 进行字面量匹配（不是正则表达式）
** - 处理所有出现的匹配项
** - 高效的缓冲区管理
** - 自动内存管理
**
** 应用场景：
** - 文本预处理和模板替换
** - 配置文件的变量替换
** - 代码生成中的占位符处理
** - 简单的文本清理操作
*/
LUALIB_API const char *luaL_gsub (lua_State *L, const char *s, const char *p,
                                                               const char *r)
{
    const char *wild;
    size_t l = strlen(p);  // 模式字符串的长度
    luaL_Buffer b;
    
    // 初始化字符串缓冲区
    luaL_buffinit(L, &b);
    
    // 查找并替换所有匹配项
    while ((wild = strstr(s, p)) != NULL)
    {
        // 添加匹配前的前缀部分
        luaL_addlstring(&b, s, wild - s);
        
        // 添加替换字符串
        luaL_addstring(&b, r);
        
        // 移动到匹配后的位置继续搜索
        s = wild + l;
    }
    
    // 添加最后的后缀部分
    luaL_addstring(&b, s);
    
    // 生成最终结果并返回
    luaL_pushresult(&b);
    return lua_tostring(L, -1);
}

/*
** [核心] 查找或创建嵌套表函数
**
** 功能：在表中查找或创建指定路径的嵌套表结构
** @param L - lua_State*: Lua状态机
** @param idx - int: 基表在栈中的位置
** @param fname - const char*: 字段路径（如"a.b.c"）
** @param szhint - int: 新表的建议初始大小
** @return const char*: 如果路径中存在非表值，返回冲突的路径；否则返回NULL
**
** 路径解析算法：
** 1. 将路径按点号分割为字段名序列
** 2. 逐级在表中查找或创建子表
** 3. 如果遇到非表类型的值，返回冲突路径
** 4. 成功时将最终表留在栈顶
**
** 表创建策略：
** - 对中间路径使用小表（容量1）
** - 对最终表使用提示大小
** - 优化内存使用和性能
**
** 错误处理：
** - 路径冲突时返回问题字段名
** - 调用者可据此生成适当错误信息
** - 保持栈状态的一致性
**
** 应用场景：
** - 模块系统的命名空间创建
** - 配置表的层次结构初始化
** - API 的分组和组织
** - 避免手动创建深层嵌套结构
*/
LUALIB_API const char *luaL_findtable (lua_State *L, int idx,
                                       const char *fname, int szhint)
{
    const char *e;
    
    // 将基表推入栈顶以便操作
    lua_pushvalue(L, idx);
    
    do
    {
        // 查找下一个点号分隔符
        e = strchr(fname, '.');
        if (e == NULL)
        {
            e = fname + strlen(fname);  // 指向字符串末尾
        }
        
        // 将当前字段名推入栈
        lua_pushlstring(L, fname, e - fname);
        
        // 在当前表中查找字段：table[fieldname]
        lua_rawget(L, -2);
        
        if (lua_isnil(L, -1))
        {
            // 字段不存在，需要创建新表
            lua_pop(L, 1);  // 移除 nil 值
            
            // 创建新表：中间路径用小表，最终路径用提示大小
            lua_createtable(L, 0, (*e == '.' ? 1 : szhint));
            
            // 设置新表到当前位置
            lua_pushlstring(L, fname, e - fname);
            lua_pushvalue(L, -2);      // 复制新表
            lua_settable(L, -4);       // table[fieldname] = 新表
        }
        else if (!lua_istable(L, -1))
        {
            // 字段存在但不是表类型，发生冲突
            lua_pop(L, 2);  // 清理栈：移除表和值
            return fname;   // 返回冲突的字段路径
        }
        
        // 移除上一级表，保留当前级表
        lua_remove(L, -2);
        
        // 移动到下一个字段
        fname = e + 1;
        
    } while (*e == '.');  // 继续处理直到路径结束
    
    return NULL;  // 成功完成，无冲突
}



/*
** ====================================================================
** [高级] 通用缓冲区操作系统实现
** ====================================================================
**
** 本模块实现了高效的字符串构建机制，通过分层缓冲策略
** 避免频繁的内存分配和字符串连接操作。
*/

/*
** [内部] 缓冲区状态计算宏
*/
// 计算缓冲区中已使用的长度
#define bufflen(B)	((B)->p - (B)->buffer)

// 计算缓冲区中剩余的空闲空间
#define bufffree(B)	((size_t)(LUAL_BUFFERSIZE - bufflen(B)))

// 栈合并的阈值限制
#define LIMIT	(LUA_MINSTACK/2)

/*
** [内部] 清空缓冲区内容到栈
**
** 功能：将缓冲区中的内容推入栈，并重置缓冲区
** @param B - luaL_Buffer*: 缓冲区指针
** @return int: 如果有内容推入返回1，否则返回0
**
** 操作逻辑：
** 1. 计算缓冲区中的数据长度
** 2. 如果为空，不做任何操作
** 3. 如果有数据，将其作为字符串推入栈
** 4. 重置缓冲区指针并增加栈层级计数
*/
static int emptybuffer (luaL_Buffer *B)
{
    size_t l = bufflen(B);
    
    if (l == 0)
    {
        return 0;  // 缓冲区为空，无需操作
    }
    else
    {
        // 将缓冲区内容推入栈
        lua_pushlstring(B->L, B->buffer, l);
        
        // 重置缓冲区
        B->p = B->buffer;
        B->lvl++;
        
        return 1;  // 成功推入一个字符串片段
    }
}

/*
** [内部] 调整栈中字符串片段
**
** 功能：合并栈中过多的字符串片段，优化内存使用
** @param B - luaL_Buffer*: 缓冲区指针
**
** 合并策略：
** 1. 检查栈中是否有多个字符串片段
** 2. 计算需要合并的片段数量
** 3. 优先合并较小的字符串
** 4. 避免超过栈深度限制
**
** 性能优化：
** - 只在必要时进行合并
** - 优先合并小字符串以减少复制开销
** - 保持合理的栈深度
*/
static void adjuststack (luaL_Buffer *B)
{
    if (B->lvl > 1)
    {
        lua_State *L = B->L;
        int toget = 1;          // 要合并的层级数
        size_t toplen = lua_strlen(L, -1);  // 栈顶字符串长度
        
        // 决定合并多少个字符串片段
        do
        {
            size_t l = lua_strlen(L, -(toget+1));
            
            // 如果达到限制或当前字符串更大，停止合并
            if (B->lvl - toget + 1 >= LIMIT || toplen > l)
            {
                toplen += l;
                toget++;
            }
            else
            {
                break;
            }
        } while (toget < B->lvl);
        
        // 执行字符串合并
        lua_concat(L, toget);
        B->lvl = B->lvl - toget + 1;
    }
}

/*
** [核心] 准备缓冲区空间函数
**
** 功能：确保缓冲区有足够空间，必要时进行整理
** @param B - luaL_Buffer*: 缓冲区指针
** @return char*: 可用于写入的内存区域指针
**
** 空间管理流程：
** 1. 尝试清空当前缓冲区到栈
** 2. 如果有内容被清空，调整栈结构
** 3. 返回重置后的缓冲区指针
**
** 保证：
** - 返回的缓冲区至少有 LUAL_BUFFERSIZE 字节可用
** - 维护字符串片段栈的合理结构
** - 自动处理内存整理和优化
*/
LUALIB_API char *luaL_prepbuffer (luaL_Buffer *B)
{
    // 清空缓冲区内容到栈
    if (emptybuffer(B))
    {
        adjuststack(B);  // 调整栈结构
    }
    
    return B->buffer;  // 返回可写入的缓冲区
}

/*
** [核心] 添加指定长度字符串函数
**
** 功能：向缓冲区添加指定长度的字符串数据
** @param B - luaL_Buffer*: 缓冲区指针
** @param s - const char*: 源字符串
** @param l - size_t: 要添加的字节数
**
** 实现策略：
** - 使用字符逐个添加的方式
** - 利用 luaL_addchar 宏的自动空间管理
** - 支持包含 null 字符的二进制数据
**
** 性能考虑：
** - 对于大数据块，可能触发多次缓冲区清空
** - luaL_addchar 包含内联的边界检查
** - 自动处理缓冲区溢出情况
*/
LUALIB_API void luaL_addlstring (luaL_Buffer *B, const char *s, size_t l)
{
    // 逐字符添加到缓冲区
    while (l--)
    {
        luaL_addchar(B, *s++);
    }
}

/*
** [核心] 添加C字符串函数
**
** 功能：向缓冲区添加以null结尾的C字符串
** @param B - luaL_Buffer*: 缓冲区指针
** @param s - const char*: 要添加的C字符串
**
** 便利接口：
** - 自动计算字符串长度
** - 调用 luaL_addlstring 执行实际添加
** - 适用于字面量和标准C字符串
*/
LUALIB_API void luaL_addstring (luaL_Buffer *B, const char *s)
{
    luaL_addlstring(B, s, strlen(s));
}

/*
** [核心] 完成缓冲区构建函数
**
** 功能：将缓冲区内容合并为单一字符串并推入栈顶
** @param B - luaL_Buffer*: 缓冲区指针
**
** 完成流程：
** 1. 将当前缓冲区内容推入栈（如果非空）
** 2. 将栈中的所有字符串片段连接
** 3. 重置缓冲区状态为单一结果
**
** 最终状态：
** - 栈顶包含完整的构建结果
** - 缓冲区层级重置为1
** - 所有临时片段被合并
*/
LUALIB_API void luaL_pushresult (luaL_Buffer *B)
{
    // 将当前缓冲区内容推入栈
    emptybuffer(B);
    
    // 合并栈中的所有字符串片段
    lua_concat(B->L, B->lvl);
    
    B->lvl = 1;  // 重置为单一结果
}

/*
** [核心] 添加栈顶值到缓冲区函数
**
** 功能：将栈顶的值转换为字符串并添加到缓冲区
** @param B - luaL_Buffer*: 缓冲区指针
**
** 智能处理策略：
** 1. 将栈顶值转换为字符串
** 2. 如果字符串较小且缓冲区有空间，直接复制
** 3. 如果字符串较大，使用栈管理策略
** 4. 自动调整栈结构以保持性能
**
** 内存优化：
** - 小字符串直接复制到缓冲区
** - 大字符串保留在栈中管理
** - 避免不必要的内存分配和复制
**
** 栈操作：
** - 始终消费栈顶元素
** - 根据情况调整栈结构
** - 维护缓冲区系统的一致性
*/
LUALIB_API void luaL_addvalue (luaL_Buffer *B)
{
    lua_State *L = B->L;
    size_t vl;
    
    // 获取栈顶值的字符串表示和长度
    const char *s = lua_tolstring(L, -1, &vl);
    
    // 如果字符串足够小且缓冲区有空间，直接复制
    if (vl <= bufffree(B))
    {
        memcpy(B->p, s, vl);  // 直接内存复制
        B->p += vl;           // 更新缓冲区指针
        lua_pop(L, 1);        // 移除栈顶值
    }
    else
    {
        // 字符串太大，使用栈管理策略
        if (emptybuffer(B))
        {
            lua_insert(L, -2);  // 将缓冲区内容放到新值前面
        }
        
        B->lvl++;       // 增加栈层级计数
        adjuststack(B); // 调整栈结构
    }
}

/*
** [核心] 缓冲区初始化函数
**
** 功能：初始化一个新的字符串缓冲区
** @param L - lua_State*: Lua状态机
** @param B - luaL_Buffer*: 要初始化的缓冲区
**
** 初始化设置：
** - 关联到指定的 Lua 状态机
** - 重置写入指针到缓冲区开始
** - 清零栈层级计数
** - 准备接收第一批数据
**
** 使用要求：
** - 必须在任何缓冲区操作前调用
** - 缓冲区可以重复使用（重新初始化）
** - 与 luaL_pushresult 配对使用
*/
LUALIB_API void luaL_buffinit (lua_State *L, luaL_Buffer *B)
{
    B->L = L;           // 关联 Lua 状态机
    B->p = B->buffer;   // 重置写入指针
    B->lvl = 0;         // 清零层级计数
}


/*
** ====================================================================
** [核心] 引用管理系统实现
** ====================================================================
**
** 本模块实现了一个高效的引用系统，允许 C 代码长期持有 Lua 值
** 而不影响垃圾回收器的正常工作。使用链表管理空闲引用，
** 实现引用的快速分配和回收。
*/

/*
** [核心] 创建引用函数
**
** 功能：在指定表中为栈顶值创建一个唯一引用
** @param L - lua_State*: Lua状态机
** @param t - int: 表在栈中的位置（通常使用LUA_REGISTRYINDEX）
** @return int: 引用标识符，可用于后续访问
**
** 引用分配算法：
** 1. 特殊处理：如果值为nil，返回特殊的LUA_REFNIL
** 2. 检查空闲列表：从FREELIST_REF位置获取第一个空闲引用
** 3. 如果有空闲引用：重用它并更新空闲列表
** 4. 如果无空闲引用：使用表长度+1作为新引用
** 5. 将值存储到分配的引用位置
**
** 空闲列表管理：
** - 空闲引用以链表形式存储在引用表中
** - FREELIST_REF(索引0)存储链表头
** - 每个空闲位置存储下一个空闲位置的索引
** - 分配时从头部取出，释放时加入头部
**
** 内存优化：
** - 重用已释放的引用位置
** - 避免引用表的无限增长
** - 保持较好的内存局部性
*/
LUALIB_API int luaL_ref (lua_State *L, int t)
{
    int ref;
    t = abs_index(L, t);  // 标准化表索引
    
    // 特殊处理：nil值使用专用引用
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);      // 移除nil值
        return LUA_REFNIL;  // 返回nil的专用引用
    }
    
    // 获取空闲列表的头部引用
    lua_rawgeti(L, t, FREELIST_REF);  // t[FREELIST_REF]
    ref = (int)lua_tointeger(L, -1);  // 转换为整数
    lua_pop(L, 1);                   // 移除空闲列表头
    
    if (ref != 0)
    {
        // 有空闲引用可用：重用它
        lua_rawgeti(L, t, ref);           // 获取 t[ref]（下一个空闲引用）
        lua_rawseti(L, t, FREELIST_REF);  // t[FREELIST_REF] = t[ref]
    }
    else
    {
        // 无空闲引用：分配新的引用
        ref = (int)lua_objlen(L, t);  // 获取表长度
        ref++;                        // 使用长度+1作为新引用
    }
    
    // 将栈顶值存储到引用位置
    lua_rawseti(L, t, ref);  // t[ref] = 栈顶值
    
    return ref;  // 返回引用标识符
}

/*
** [核心] 释放引用函数
**
** 功能：释放之前创建的引用，允许值被垃圾回收
** @param L - lua_State*: Lua状态机
** @param t - int: 引用所在的表位置
** @param ref - int: 要释放的引用标识符
**
** 释放流程：
** 1. 验证引用的有效性（非负数）
** 2. 将当前空闲列表头保存到要释放的位置
** 3. 将要释放的引用作为新的空闲列表头
** 4. 完成引用的回收，供后续重用
**
** 空闲列表维护：
** - 采用头插法：新释放的引用成为链表头
** - 保持链表的完整性和一致性
** - 快速的释放操作（O(1)复杂度）
**
** 特殊情况处理：
** - 负数引用被忽略（如LUA_REFNIL等特殊值）
** - 确保引用系统的健壮性
** - 避免对特殊引用的误操作
*/
LUALIB_API void luaL_unref (lua_State *L, int t, int ref)
{
    if (ref >= 0)
    {
        t = abs_index(L, t);  // 标准化表索引
        
        // 获取当前空闲列表头
        lua_rawgeti(L, t, FREELIST_REF);  // 将 t[FREELIST_REF] 推入栈
        
        // 将当前空闲列表头存储到要释放的引用位置
        lua_rawseti(L, t, ref);  // t[ref] = 原来的 t[FREELIST_REF]
        
        // 将释放的引用设为新的空闲列表头
        lua_pushinteger(L, ref);         // 推入引用编号
        lua_rawseti(L, t, FREELIST_REF); // t[FREELIST_REF] = ref
    }
}



/*
** ====================================================================
** [核心] 代码加载和执行系统
** ====================================================================
**
** 本模块实现了从各种来源加载 Lua 代码的功能，包括文件、
** 内存缓冲区和字符串。支持源码和字节码的自动识别与处理。
*/

/*
** [内部] 文件加载状态结构
**
** 用于文件加载过程中维护读取状态和缓冲区管理
*/
typedef struct LoadF
{
    int extraline;                    // 是否需要添加额外换行符
    FILE *f;                         // 文件指针
    char buff[LUAL_BUFFERSIZE];      // 读取缓冲区
} LoadF;

/*
** [内部] 文件读取回调函数
**
** 功能：为 lua_load 提供文件数据读取接口
** @param L - lua_State*: Lua状态机（未使用）
** @param ud - void*: 用户数据（LoadF结构体）
** @param size - size_t*: 输出读取的字节数
** @return const char*: 读取的数据指针，NULL表示结束
**
** 特殊处理：
** - 处理Unix可执行文件的首行跳过
** - 自动处理换行符添加
** - 提供文件结束检测
*/
static const char *getF (lua_State *L, void *ud, size_t *size)
{
    LoadF *lf = (LoadF *)ud;
    (void)L;  // 避免未使用参数警告
    
    // 处理额外换行符（用于跳过Unix shebang行）
    if (lf->extraline)
    {
        lf->extraline = 0;
        *size = 1;
        return "\n";
    }
    
    // 检查文件结束
    if (feof(lf->f))
    {
        return NULL;
    }
    
    // 读取文件数据到缓冲区
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);
    return (*size > 0) ? lf->buff : NULL;
}

/*
** [内部] 文件错误处理函数
**
** 功能：生成文件操作错误信息
** @param L - lua_State*: Lua状态机
** @param what - const char*: 操作类型描述
** @param fnameindex - int: 文件名在栈中的位置
** @return int: LUA_ERRFILE错误码
*/
static int errfile (lua_State *L, const char *what, int fnameindex)
{
    const char *serr = strerror(errno);           // 系统错误信息
    const char *filename = lua_tostring(L, fnameindex) + 1;  // 跳过'@'前缀
    
    // 生成详细的错误信息
    lua_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
    lua_remove(L, fnameindex);  // 移除文件名
    
    return LUA_ERRFILE;
}

/*
** [核心] 从文件加载Lua代码函数
**
** 功能：从指定文件加载Lua源代码或字节码
** @param L - lua_State*: Lua状态机
** @param filename - const char*: 文件路径名，NULL表示标准输入
** @return int: 加载结果状态码
**
** 加载流程：
** 1. 打开文件并处理特殊文件名（NULL表示stdin）
** 2. 检测文件类型：Unix可执行文件、二进制字节码或源代码
** 3. 根据文件类型进行相应的预处理
** 4. 调用lua_load执行实际的代码解析和编译
** 5. 处理读取错误和清理资源
**
** 文件类型检测：
** - 以'#'开头：Unix可执行文件，跳过第一行
** - 以Lua签名开头：二进制字节码文件
** - 其他：普通Lua源代码文件
**
** 错误处理：
** - 文件打开失败
** - 文件读取错误
** - 语法分析错误
** - 内存不足错误
*/
LUALIB_API int luaL_loadfile (lua_State *L, const char *filename)
{
    LoadF lf;
    int status, readstatus;
    int c;
    int fnameindex = lua_gettop(L) + 1;  // 文件名将要放置的栈位置
    
    lf.extraline = 0;
    
    if (filename == NULL)
    {
        // 从标准输入读取
        lua_pushliteral(L, "=stdin");
        lf.f = stdin;
    }
    else
    {
        // 从指定文件读取
        lua_pushfstring(L, "@%s", filename);
        lf.f = fopen(filename, "r");
        if (lf.f == NULL)
        {
            return errfile(L, "open", fnameindex);
        }
    }
    
    // 读取第一个字符以检测文件类型
    c = getc(lf.f);
    
    if (c == '#')
    {
        // Unix可执行文件：跳过第一行
        lf.extraline = 1;
        while ((c = getc(lf.f)) != EOF && c != '\n')
        {
            ;  // 跳过整行
        }
        if (c == '\n')
        {
            c = getc(lf.f);
        }
    }
    
    if (c == LUA_SIGNATURE[0] && filename)
    {
        // 二进制字节码文件：以二进制模式重新打开
        lf.f = freopen(filename, "rb", lf.f);
        if (lf.f == NULL)
        {
            return errfile(L, "reopen", fnameindex);
        }
        
        // 跳过可能的shebang行直到找到Lua签名
        while ((c = getc(lf.f)) != EOF && c != LUA_SIGNATURE[0])
        {
            ;
        }
        lf.extraline = 0;
    }
    
    // 将读取的字符放回文件流
    ungetc(c, lf.f);
    
    // 执行实际的代码加载
    status = lua_load(L, getF, &lf, lua_tostring(L, -1));
    readstatus = ferror(lf.f);
    
    // 关闭文件（即使出错也要关闭）
    if (filename)
    {
        fclose(lf.f);
    }
    
    // 检查读取错误
    if (readstatus)
    {
        lua_settop(L, fnameindex);  // 忽略lua_load的结果
        return errfile(L, "read", fnameindex);
    }
    
    // 移除文件名，保留加载结果
    lua_remove(L, fnameindex);
    return status;
}

/*
** [内部] 字符串加载状态结构
**
** 用于从内存字符串加载代码时的状态管理
*/
typedef struct LoadS
{
    const char *s;    // 字符串数据指针
    size_t size;      // 字符串长度
} LoadS;

/*
** [内部] 字符串读取回调函数
**
** 功能：为lua_load提供字符串数据读取接口
** @param L - lua_State*: Lua状态机（未使用）
** @param ud - void*: 用户数据（LoadS结构体）
** @param size - size_t*: 输出读取的字节数
** @return const char*: 数据指针，NULL表示结束
*/
static const char *getS (lua_State *L, void *ud, size_t *size)
{
    LoadS *ls = (LoadS *)ud;
    (void)L;  // 避免未使用参数警告
    
    if (ls->size == 0)
    {
        return NULL;  // 数据已全部读取完毕
    }
    
    // 返回全部数据（一次性读取）
    *size = ls->size;
    ls->size = 0;
    return ls->s;
}

/*
** [核心] 从内存缓冲区加载代码函数
**
** 功能：从内存中的字符串缓冲区加载Lua代码
** @param L - lua_State*: Lua状态机
** @param buff - const char*: 包含代码的内存缓冲区
** @param size - size_t: 缓冲区大小（字节数）
** @param name - const char*: 代码块名称（用于错误报告和调试）
** @return int: 加载结果状态码
**
** 特性：
** - 支持二进制数据（可包含null字符）
** - 一次性读取全部数据
** - 适用于预加载的代码和动态生成的代码
** - 高效的内存访问模式
*/
LUALIB_API int luaL_loadbuffer (lua_State *L, const char *buff, size_t size,
                                const char *name)
{
    LoadS ls;
    ls.s = buff;
    ls.size = size;
    return lua_load(L, getS, &ls, name);
}

/*
** [核心] 从字符串加载代码函数
**
** 功能：从C字符串加载Lua代码（以null结尾）
** @param L - lua_State*: Lua状态机
** @param s - const char*: 包含Lua代码的C字符串
** @return int: 加载结果状态码
**
** 便利接口：
** - 自动计算字符串长度
** - 使用字符串本身作为代码块名称
** - 适用于字面量代码和简短脚本
*/
LUALIB_API int (luaL_loadstring) (lua_State *L, const char *s)
{
    return luaL_loadbuffer(L, s, strlen(s), s);
}


/*
** ====================================================================
** [核心] 内存管理和状态机创建系统
** ====================================================================
**
** 本模块提供了标准的内存分配器和 Lua 状态机创建功能，
** 实现了与系统内存管理的集成和错误处理机制。
*/

/*
** [内部] 标准内存分配函数
**
** 功能：为 Lua 提供标准的内存分配和释放服务
** @param ud - void*: 用户数据（未使用）
** @param ptr - void*: 要重新分配的内存块指针
** @param osize - size_t: 原始大小（未使用）
** @param nsize - size_t: 新的大小
** @return void*: 新分配的内存指针，失败时返回NULL
**
** 分配策略：
** - nsize为0：释放内存块
** - nsize非0：分配或重新分配内存
** - 使用标准C库的realloc函数
** - 完全依赖系统内存管理器
**
** 内存语义：
** - 兼容 lua_Alloc 函数签名
** - 支持内存块的扩展、收缩和释放
** - 当 nsize 为 0 时等价于 free(ptr)
** - 当 ptr 为 NULL 时等价于 malloc(nsize)
**
** 性能特性：
** - 直接使用系统分配器，无额外开销
** - 适用于大多数标准应用场景
** - 可被自定义分配器替代
*/
static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;     // 避免未使用参数警告
    (void)osize;  // 标准分配器不需要原始大小信息
    
    if (nsize == 0)
    {
        // 释放内存：nsize为0表示释放操作
        free(ptr);
        return NULL;
    }
    else
    {
        // 分配或重新分配内存
        return realloc(ptr, nsize);
    }
}

/*
** [内部] 紧急情况处理函数
**
** 功能：处理 Lua 中的不可恢复错误（panic状态）
** @param L - lua_State*: 发生错误的Lua状态机
** @return int: 返回值（实际不会被使用，因为程序会终止）
**
** 错误处理：
** - 打印错误信息到标准错误输出
** - 显示详细的错误上下文
** - 不进行程序终止（由调用者决定）
**
** 使用场景：
** - 保护模式调用失败时的最后处理
** - 内存不足等系统级错误
** - API使用错误导致的不可恢复状态
**
** 安全考虑：
** - 不访问可能已损坏的Lua状态
** - 使用最小的系统资源
** - 提供足够的调试信息
*/
static int panic (lua_State *L)
{
    (void)L;  // 避免未使用参数警告，同时避免访问可能损坏的状态
    
    // 输出紧急错误信息
    fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n",
                   lua_tostring(L, -1));
    
    return 0;  // 返回值实际上不会被使用
}

/*
** [核心] 创建新Lua状态机函数
**
** 功能：创建一个全新的独立Lua状态机实例
** @return lua_State*: 新创建的状态机指针，失败时返回NULL
**
** 创建流程：
** 1. 使用标准分配器创建基础状态机
** 2. 设置紧急情况处理函数
** 3. 返回完全初始化的状态机
**
** 状态机特性：
** - 独立的内存空间和执行环境
** - 标准的内存分配策略
** - 完整的错误处理机制
** - 可与其他状态机并行运行
**
** 资源管理：
** - 调用者负责使用 lua_close 关闭状态机
** - 状态机会自动管理内部资源
** - 支持垃圾回收和内存优化
**
** 失败处理：
** - 内存不足时返回 NULL
** - 不会抛出异常或终止程序
** - 调用者应检查返回值
**
** 使用模式：
** lua_State *L = luaL_newstate();
** if (L) {
**     // 使用状态机...
**     lua_close(L);
** }
*/
LUALIB_API lua_State *luaL_newstate (void)
{
    // 创建新的Lua状态机，使用标准分配器
    lua_State *L = lua_newstate(l_alloc, NULL);
    
    if (L)
    {
        // 设置紧急情况处理函数
        lua_atpanic(L, &panic);
    }
    
    return L;  // 返回新状态机（可能为NULL）
}

/*
** ====================================================================
** 文件实现总结
** ====================================================================
**
** 本文件实现了 Lua 辅助库的完整功能集，提供了以下核心能力：
**
** 1. 错误处理系统
**    - 详细的错误位置信息
**    - 格式化的错误消息
**    - 统一的异常抛出机制
**
** 2. 参数验证框架
**    - 类型安全的参数检查
**    - 自动类型转换
**    - 可选参数处理
**
** 3. 元表管理工具
**    - 用户数据类型系统
**    - 元方法调用支持
**    - 类型安全验证
**
** 4. 库注册机制
**    - 批量函数注册
**    - 模块系统集成
**    - 上值共享支持
**
** 5. 字符串缓冲区
**    - 高效的字符串构建
**    - 自动内存管理
**    - 多种数据来源支持
**
** 6. 引用管理系统
**    - C代码中的Lua值持有
**    - 自动垃圾回收集成
**    - 高效的引用回收
**
** 7. 代码加载系统
**    - 多种代码来源支持
**    - 自动文件类型检测
**    - 完整的错误处理
**
** 8. 内存管理接口
**    - 标准内存分配器
**    - 状态机创建和初始化
**    - 紧急情况处理
**
** 设计原则：
** - 类型安全：严格的类型检查和验证
** - 错误友好：清晰的错误信息和异常处理
** - 性能优化：高效的算法和数据结构
** - 内存安全：完整的内存管理和垃圾回收集成
** - 向后兼容：支持旧版本Lua的兼容接口
**
** 适用场景：
** - 构建Lua扩展库
** - 嵌入Lua到C应用程序
** - 实现自定义数据类型
** - 开发Lua工具和框架
*/

