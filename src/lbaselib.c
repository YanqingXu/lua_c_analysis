/*
** Lua 基础库实现 (lbaselib.c)
**
** 功能描述: 实现 Lua 语言的基础库函数
** 版权信息: 参见 lua.h 中的版权声明
**
** 模块概述:
** 本文件实现了 Lua 语言的基础库，包含了最常用的内置函数和协程库。
** 基础库提供了类型转换、元表操作、错误处理、文件加载、迭代器等核心功能。
**
** 主要功能模块:
** - 基本函数: print, type, tostring, tonumber 等
** - 元表操作: getmetatable, setmetatable
** - 错误处理: error, assert, pcall, xpcall
** - 文件操作: loadfile, dofile, loadstring, load
** - 迭代器: pairs, ipairs, next
** - 表操作: rawget, rawset, rawequal, unpack, select
** - 环境操作: getfenv, setfenv
** - 垃圾回收: collectgarbage, gcinfo
** - 协程库: create, resume, yield, status, wrap, running
**
** 依赖关系:
** - lua.h: Lua 核心 API 接口
** - lauxlib.h: Lua 辅助库接口
** - lualib.h: Lua 标准库接口
** - 标准 C 库: ctype.h, stdio.h, stdlib.h, string.h
**
** 注意事项:
** - 这些函数构成了 Lua 的核心运行时环境
** - 大部分函数会被自动注册到全局环境中
** - 协程相关函数注册在 coroutine 表中
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lbaselib_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"



/*
** [基础函数] print 函数实现
**
** 将参数转换为字符串并输出到标准输出
**
** 详细功能说明：
** - 调用 tostring 函数将所有参数转换为字符串
** - 参数之间用制表符分隔
** - 最后输出换行符
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
**
** 返回值：
** @return int：返回值数量（总是 0）
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 为参数数量
**
** 注意事项：
** - 如果系统不支持 stdout，可以移除此函数
** - 可以自定义输出目标（控制台窗口或日志文件等）
** - tostring 必须返回字符串，否则会报错
*/
static int luaB_print (lua_State *L)
{
    // 获取参数数量
    int n = lua_gettop(L);
    int i;
    lua_getglobal(L, "tostring");

    // 遍历所有参数进行转换和输出
    for (i = 1; i <= n; i++)
    {
        const char *s;

        // 压入要调用的函数
        lua_pushvalue(L, -1);
        // 压入要打印的值
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);

        // 获取结果
        s = lua_tostring(L, -1);

        if (s == NULL)
        {
            return luaL_error(L, LUA_QL("tostring") " must return a string to "
                               LUA_QL("print"));
        }

        // 输出分隔符和内容
        if (i > 1)
        {
            fputs("\t", stdout);
        }
        fputs(s, stdout);

        // 弹出结果
        lua_pop(L, 1);
    }

    // 输出换行符
    fputs("\n", stdout);
    return 0;
}

/*
** [基础函数] tonumber 函数实现
**
** 将字符串或其他类型转换为数字
**
** 详细功能说明：
** - 支持十进制和指定进制的字符串转换
** - 如果已经是数字类型，直接返回
** - 转换失败时返回 nil
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要转换的值
** - 参数2：进制（可选，默认为10，范围2-36）
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：转换后的数字或 nil
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 为字符串长度
**
** 注意事项：
** - 进制必须在 2-36 范围内
** - 会跳过字符串前后的空白字符
** - 部分有效数字也会被接受
*/
static int luaB_tonumber (lua_State *L)
{
    int base = luaL_optint(L, 2, 10);

    // 标准十进制转换
    if (base == 10)
    {
        luaL_checkany(L, 1);

        if (lua_isnumber(L, 1))
        {
            lua_pushnumber(L, lua_tonumber(L, 1));
            return 1;
        }
    }
    else
    {
        // 指定进制转换
        const char *s1 = luaL_checkstring(L, 1);
        char *s2;
        unsigned long n;

        luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
        n = strtoul(s1, &s2, base);

        // 至少有一个有效数字？
        if (s1 != s2)
        {
            // 跳过尾部空格
            while (isspace((unsigned char)(*s2)))
            {
                s2++;
            }

            // 没有无效的尾部字符？
            if (*s2 == '\0')
            {
                lua_pushnumber(L, (lua_Number)n);
                return 1;
            }
        }
    }

    // 转换失败，返回 nil
    lua_pushnil(L);
    return 1;
}


/*
** [错误处理] error 函数实现
**
** 抛出一个错误并终止当前函数的执行
**
** 详细功能说明：
** - 抛出指定的错误消息
** - 可以指定错误级别来添加调用位置信息
** - 错误级别大于0时会添加文件名和行号信息
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：错误消息
** - 参数2：错误级别（可选，默认为1）
**
** 返回值：
** @return int：此函数不会正常返回，总是抛出错误
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 此函数不会正常返回
** - 错误级别0表示不添加位置信息
** - 错误级别1表示调用error的位置
*/
static int luaB_error (lua_State *L)
{
    int level = luaL_optint(L, 2, 1);
    lua_settop(L, 1);

    // 添加位置信息到错误消息
    if (lua_isstring(L, 1) && level > 0)
    {
        luaL_where(L, level);
        lua_pushvalue(L, 1);
        lua_concat(L, 2);
    }

    return lua_error(L);
}

/*
** [元表操作] getmetatable 函数实现
**
** 获取对象的元表
**
** 详细功能说明：
** - 返回指定对象的元表
** - 如果对象没有元表，返回 nil
** - 如果元表有 __metatable 字段，返回该字段的值
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要获取元表的对象
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：元表或 __metatable 字段值或 nil
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - __metatable 字段用于隐藏真实的元表
** - 所有类型的对象都可以有元表
*/
static int luaB_getmetatable (lua_State *L)
{
    // 检查第一个参数存在（可以是任何类型）
    luaL_checkany(L, 1);

    // 尝试获取对象的元表
    if (!lua_getmetatable(L, 1))
    {
        // 对象没有元表：压入 nil 并返回
        lua_pushnil(L);
        return 1;
    }

    // 对象有元表：检查是否有 __metatable 字段
    // 如果有 __metatable 字段，返回该字段的值；否则返回元表本身
    luaL_getmetafield(L, 1, "__metatable");

    // 返回 __metatable 字段值或元表
    return 1;
}

/*
** [元表操作] setmetatable 函数实现
**
** 设置表的元表
**
** 详细功能说明：
** - 为指定的表设置元表
** - 元表必须是表或 nil
** - 如果原元表有 __metatable 字段，则不允许修改
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要设置元表的表
** - 参数2：新的元表（表或 nil）
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：第一个参数（设置了元表的表）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只能为表设置元表
** - 受保护的元表不能被修改
** - 返回设置了元表的表本身
*/
static int luaB_setmetatable (lua_State *L)
{
    // 获取第二个参数的类型
    int t = lua_type(L, 2);

    // 检查第一个参数必须是表类型
    luaL_checktype(L, 1, LUA_TTABLE);

    // 检查第二个参数必须是 nil 或表类型
    luaL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2,
                      "nil or table expected");

    // 检查表是否有 __metatable 字段（受保护的元表）
    if (luaL_getmetafield(L, 1, "__metatable"))
    {
        // 有 __metatable 字段：不允许修改受保护的元表
        luaL_error(L, "cannot change a protected metatable");
    }

    // 确保栈上只有两个参数
    lua_settop(L, 2);

    // 为第一个参数（表）设置元表（第二个参数）
    lua_setmetatable(L, 1);

    // 返回设置了元表的表本身
    return 1;
}


/*
** [辅助函数] 获取函数对象
**
** 从栈中获取函数对象或根据级别获取调用栈中的函数
**
** 详细功能说明：
** - 如果参数1是函数，直接使用
** - 否则将参数1作为调用级别，从调用栈中获取函数
** - 用于 getfenv 和 setfenv 函数的内部实现
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** @param opt - int：是否为可选参数（1为可选，0为必需）
**
** 返回值：
** @return void：无返回值，结果压入栈顶
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 调用级别必须非负
** - 无效级别会导致错误
** - 尾调用位置可能没有函数环境
*/
static void getfunc (lua_State *L, int opt)
{
    // 检查第一个参数是否直接是函数对象
    if (lua_isfunction(L, 1))
    {
        // 是函数：直接复制到栈顶
        lua_pushvalue(L, 1);
    }
    else
    {
        // 不是函数：将第一个参数作为调用级别，从调用栈中获取函数
        lua_Debug ar;

        // 根据 opt 参数决定如何获取级别值
        // opt=1: 允许可选参数，默认级别为1
        // opt=0: 必须提供级别参数
        int level = opt ? luaL_optint(L, 1, 1) : luaL_checkint(L, 1);

        // 检查级别值必须非负
        luaL_argcheck(L, level >= 0, 1, "level must be non-negative");

        // 尝试获取指定级别的调用栈信息
        if (lua_getstack(L, level, &ar) == 0)
        {
            // 级别无效：超出了调用栈的范围
            luaL_argerror(L, 1, "invalid level");
        }

        // 从调用栈信息中获取函数对象（"f" 表示获取函数）
        lua_getinfo(L, "f", &ar);

        // 检查是否成功获取到函数
        if (lua_isnil(L, -1))
        {
            // 获取失败：通常是因为尾调用优化导致无法获取函数环境
            luaL_error(L, "no function environment for tail call at level %d",
                          level);
        }
    }
}


/*
** [环境操作] getfenv 函数实现
**
** 获取函数的环境表
**
** 详细功能说明：
** - 获取指定函数或调用级别的环境表
** - C函数返回全局环境表
** - Lua函数返回其环境表
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：函数或调用级别
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：环境表
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - C函数使用全局环境
** - 调用级别从1开始计数
*/
static int luaB_getfenv (lua_State *L)
{
    // 调用 getfunc 获取函数对象
    // 参数1表示允许可选参数（如果是数字则作为调用级别）
    getfunc(L, 1);

    // 检查栈顶的函数是否为 C 函数
    // C 函数和 Lua 函数的环境处理方式不同
    if (lua_iscfunction(L, -1))
    {
        // 是 C 函数：C 函数没有独立的环境表
        // 返回当前线程的全局环境表
        lua_pushvalue(L, LUA_GLOBALSINDEX);
    }
    else
    {
        // 是 Lua 函数：获取该函数的环境表
        // lua_getfenv 从栈顶的函数对象获取其环境表
        lua_getfenv(L, -1);
    }

    // 返回1个值：函数的环境表或全局环境表
    return 1;
}

/*
** [环境操作] setfenv 函数实现
**
** 设置函数的环境表
**
** 详细功能说明：
** - 为指定函数或调用级别设置环境表
** - 环境表必须是表类型
** - 特殊值0表示设置当前线程的环境
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：函数或调用级别
** - 参数2：新的环境表
**
** 返回值：
** @return int：返回值数量（0或1）
** 栈返回：设置了环境的函数（如果适用）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - C函数的环境不能被修改
** - 级别0表示当前线程
** - 某些对象的环境不能被修改
*/
static int luaB_setfenv (lua_State *L)
{
    // 检查第二个参数必须是表类型（新的环境表）
    luaL_checktype(L, 2, LUA_TTABLE);

    // 调用 getfunc 获取要设置环境的函数对象
    // 参数0表示不允许可选参数（必须提供函数或级别）
    getfunc(L, 0);

    // 将新环境表压入栈顶，准备设置
    lua_pushvalue(L, 2);

    // 检查是否为特殊情况：级别0表示要修改当前线程的环境
    if (lua_isnumber(L, 1) && lua_tonumber(L, 1) == 0)
    {
        // 级别0：修改当前线程的全局环境
        // 获取当前线程对象
        lua_pushthread(L);
        // 调整栈顺序：将线程对象插入到环境表下面
        lua_insert(L, -2);
        // 为当前线程设置新的环境表
        lua_setfenv(L, -2);
        return 0;
    }
    else if (lua_iscfunction(L, -2) || lua_setfenv(L, -2) == 0)
    {
        // 设置失败的情况：
        // 1. 目标是 C 函数（C 函数不能设置环境）
        // 2. lua_setfenv 返回0（设置失败）
        luaL_error(L,
              LUA_QL("setfenv") " cannot change environment of given object");
    }

    // 设置成功：返回被修改的函数对象
    return 1;
}


/*
** [表操作] rawequal 函数实现
**
** 原始相等比较，不调用元方法
**
** 详细功能说明：
** - 比较两个值是否相等，不触发 __eq 元方法
** - 进行原始的值比较，绕过元表机制
** - 用于需要避免元方法干扰的场景
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要比较的第一个值
** - 参数2：要比较的第二个值
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：布尔值，表示是否相等
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 不会调用任何元方法
** - 比较的是值的原始相等性
** - 对于表和函数比较的是引用
*/
static int luaB_rawequal (lua_State *L)
{
    // 检查第一个参数存在（可以是任何类型）
    luaL_checkany(L, 1);

    // 检查第二个参数存在（可以是任何类型）
    luaL_checkany(L, 2);

    // 调用 lua_rawequal 进行原始相等比较（不触发元方法）
    // 将比较结果（布尔值）压入栈
    lua_pushboolean(L, lua_rawequal(L, 1, 2));

    // 返回1个值：比较结果（true或false）
    return 1;
}

/*
** [表操作] rawget 函数实现
**
** 原始获取表元素，不调用元方法
**
** 详细功能说明：
** - 从表中获取指定键的值，不触发 __index 元方法
** - 进行原始的表访问，绕过元表机制
** - 用于需要避免元方法干扰的场景
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要访问的表
** - 参数2：要获取的键
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：键对应的值或 nil
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 不会调用 __index 元方法
** - 第一个参数必须是表
** - 如果键不存在返回 nil
*/
static int luaB_rawget (lua_State *L)
{
    // 检查第一个参数必须是表类型
    luaL_checktype(L, 1, LUA_TTABLE);

    // 检查第二个参数（键）存在（可以是任何类型）
    luaL_checkany(L, 2);

    // 确保栈上只有表和键两个参数，丢弃多余参数
    lua_settop(L, 2);

    // 调用 lua_rawget 执行原始获取操作（不触发 __index 元方法）
    // 结果会替换栈上键的位置
    lua_rawget(L, 1);

    // 返回1个值：键对应的值（如果不存在则为nil）
    return 1;
}

/*
** [表操作] rawset 函数实现
**
** 原始设置表元素，不调用元方法
**
** 详细功能说明：
** - 向表中设置指定键的值，不触发 __newindex 元方法
** - 进行原始的表赋值，绕过元表机制
** - 用于需要避免元方法干扰的场景
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要修改的表
** - 参数2：要设置的键
** - 参数3：要设置的值
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：被修改的表
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 不会调用 __newindex 元方法
** - 第一个参数必须是表
** - 返回被修改的表本身
*/
static int luaB_rawset (lua_State *L)
{
    // 检查第一个参数必须是表类型
    luaL_checktype(L, 1, LUA_TTABLE);

    // 检查第二个参数（键）存在（可以是任何类型）
    luaL_checkany(L, 2);

    // 检查第三个参数（值）存在（可以是任何类型）
    luaL_checkany(L, 3);

    // 确保栈上只有表、键、值三个参数，丢弃多余参数
    lua_settop(L, 3);

    // 调用 lua_rawset 执行原始设置操作（不触发 __newindex 元方法）
    // 操作完成后栈上只剩下表（第一个参数）
    lua_rawset(L, 1);

    // 返回1个值：被修改的表本身
    return 1;
}


/*
** [垃圾回收] gcinfo 函数实现
**
** 获取垃圾回收器的内存使用信息
**
** 详细功能说明：
** - 返回当前 Lua 使用的内存量（以KB为单位）
** - 这是一个简化的垃圾回收信息函数
** - 主要用于兼容性，建议使用 collectgarbage("count")
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：内存使用量（整数，单位KB）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 返回值是整数，不包含小数部分
** - 建议使用 collectgarbage("count") 获取更精确的值
** - 这是一个遗留函数，保留用于兼容性
*/
static int luaB_gcinfo (lua_State *L)
{
    // 调用 lua_getgccount 获取当前内存使用量（以KB为单位）
    // 将结果作为整数压入栈
    lua_pushinteger(L, lua_getgccount(L));

    // 返回1个值：内存使用量（整数KB）
    return 1;
}

/*
** [垃圾回收] collectgarbage 函数实现
**
** 控制垃圾回收器的行为
**
** 详细功能说明：
** - 提供对垃圾回收器的完整控制接口
** - 支持多种操作：停止、重启、收集、计数、步进、设置参数
** - 根据不同操作返回相应的结果
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：操作类型字符串（可选，默认"collect"）
** - 参数2：额外参数（可选，默认0）
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：根据操作类型返回不同的值
**
** 算法复杂度：O(n) 时间（收集操作），O(1) 空间
**
** 支持的操作：
** - "stop": 停止垃圾回收器
** - "restart": 重启垃圾回收器
** - "collect": 执行完整的垃圾回收
** - "count": 返回内存使用量（KB，包含小数）
** - "step": 执行一步垃圾回收
** - "setpause": 设置垃圾回收暂停参数
** - "setstepmul": 设置垃圾回收步进倍数
**
** 注意事项：
** - count 操作返回精确的内存使用量
** - step 操作返回是否完成了一个回收周期
** - 其他操作返回之前的设置值
*/
static int luaB_collectgarbage (lua_State *L)
{
    // 支持的操作选项字符串数组
    static const char *const opts[] = {"stop", "restart", "collect",
        "count", "step", "setpause", "setstepmul", NULL};
    // 对应的 Lua 垃圾回收操作码数组
    static const int optsnum[] = {LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT,
        LUA_GCCOUNT, LUA_GCSTEP, LUA_GCSETPAUSE, LUA_GCSETSTEPMUL};

    // 检查并获取操作选项，默认为 "collect"
    int o = luaL_checkoption(L, 1, "collect", opts);
    // 获取可选的额外参数，默认为 0
    int ex = luaL_optint(L, 2, 0);

    // 执行垃圾回收操作
    int res = lua_gc(L, optsnum[o], ex);

    // 根据不同的操作类型处理返回值
    switch (optsnum[o])
    {
        case LUA_GCCOUNT:
        {
            // count 操作需要返回精确的内存使用量（包含小数部分）
            int b = lua_gc(L, LUA_GCCOUNTB, 0);
            // 将字节数转换为 KB 并加上整数部分
            lua_pushnumber(L, res + ((lua_Number)b/1024));
            return 1;
        }

        case LUA_GCSTEP:
        {
            // step 操作返回是否完成了一个垃圾回收周期
            lua_pushboolean(L, res);
            return 1;
        }

        default:
        {
            // 其他操作返回数值结果（通常是之前的设置值）
            lua_pushnumber(L, res);
            return 1;
        }
    }
}


/*
** [基础函数] type 函数实现
**
** 获取值的类型名称
**
** 详细功能说明：
** - 返回指定值的类型名称字符串
** - 支持所有 Lua 数据类型的识别
** - 返回标准的类型名称
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要检查类型的值
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：类型名称字符串
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 可能的返回值：
** - "nil": 空值
** - "boolean": 布尔值
** - "number": 数字
** - "string": 字符串
** - "table": 表
** - "function": 函数
** - "userdata": 用户数据
** - "thread": 线程（协程）
**
** 注意事项：
** - 返回的是类型的字符串表示
** - 不区分不同种类的函数或用户数据
** - 协程被识别为 "thread" 类型
*/
static int luaB_type (lua_State *L)
{
    // 检查第一个参数存在（可以是任何类型包括 nil）
    luaL_checkany(L, 1);

    // 获取参数的类型名称字符串并压入栈
    // luaL_typename 返回类型的标准名称（如 "nil", "number", "string" 等）
    lua_pushstring(L, luaL_typename(L, 1));

    // 返回1个值：类型名称字符串
    return 1;
}


/*
** [迭代器] next 函数实现
**
** 返回表的下一个键值对
**
** 详细功能说明：
** - 用于遍历表中的所有键值对
** - 如果没有更多元素，返回 nil
** - 是 pairs 函数的核心实现
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要遍历的表
** - 参数2：当前键（可选）
**
** 返回值：
** @return int：返回值数量（1或2）
** 栈返回：下一个键值对或 nil
**
** 算法复杂度：O(1) 时间，O(1) 空间
*/
static int luaB_next (lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    // 如果没有第二个参数则创建一个
    lua_settop(L, 2);

    if (lua_next(L, 1))
    {
        return 2;
    }
    else
    {
        lua_pushnil(L);
        return 1;
    }
}

/*
** [迭代器] pairs 函数实现
**
** 返回用于遍历表的迭代器函数
**
** 详细功能说明：
** - 返回 next 函数、表和初始键值
** - 用于 for 循环中遍历表的所有键值对
** - 遍历顺序不确定
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要遍历的表
**
** 返回值：
** @return int：返回值数量（总是 3）
** 栈返回：迭代器函数、状态、初始值
**
** 算法复杂度：O(1) 时间，O(1) 空间
*/
static int luaB_pairs (lua_State *L)
{
    // 检查第一个参数必须是表类型
    luaL_checktype(L, 1, LUA_TTABLE);

    // 压入迭代器生成器函数（next函数，存储在上值索引1中）
    lua_pushvalue(L, lua_upvalueindex(1));

    // 压入状态参数（要遍历的表，即第一个参数）
    lua_pushvalue(L, 1);

    // 压入初始控制变量（nil表示从表的第一个键开始）
    lua_pushnil(L);

    // 返回3个值：迭代器函数、状态、初始控制变量
    // 这些值将被 for 循环使用：for k, v in pairs(t) do ... end
    return 3;
}

/*
** [辅助函数] ipairs 迭代器辅助函数
**
** ipairs 的实际迭代器实现
**
** 详细功能说明：
** - 按数字索引顺序遍历数组部分
** - 从索引1开始，遇到 nil 值时停止
** - 返回索引和对应的值
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要遍历的表
** - 参数2：当前索引
**
** 返回值：
** @return int：返回值数量（0或2）
** 栈返回：下一个索引和值，或无返回值
**
** 算法复杂度：O(1) 时间，O(1) 空间
*/
static int ipairsaux (lua_State *L)
{
    // 获取当前索引值
    int i = luaL_checkint(L, 2);
    // 确保第一个参数是表
    luaL_checktype(L, 1, LUA_TTABLE);

    // 递增到下一个索引
    i++;
    // 将新索引压入栈
    lua_pushinteger(L, i);
    // 获取表中该索引对应的值
    lua_rawgeti(L, 1, i);

    // 如果值为 nil 则迭代结束，返回 0；否则返回索引和值，返回 2
    return (lua_isnil(L, -1)) ? 0 : 2;
}

/*
** [迭代器] ipairs 函数实现
**
** 返回用于按索引遍历数组的迭代器函数
**
** 详细功能说明：
** - 返回迭代器函数、表和初始索引0
** - 用于 for 循环中按数字索引遍历数组
** - 从索引1开始，遇到 nil 值时停止
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要遍历的表
**
** 返回值：
** @return int：返回值数量（总是 3）
** 栈返回：迭代器函数、状态、初始值
**
** 算法复杂度：O(1) 时间，O(1) 空间
*/
static int luaB_ipairs (lua_State *L)
{
    // 检查第一个参数必须是表类型
    luaL_checktype(L, 1, LUA_TTABLE);

    // 压入迭代器生成器函数（ipairsaux，存储在上值索引1中）
    lua_pushvalue(L, lua_upvalueindex(1));

    // 压入状态参数（要遍历的表，即第一个参数）
    lua_pushvalue(L, 1);

    // 压入初始控制变量（索引从0开始，ipairsaux会递增到1）
    lua_pushinteger(L, 0);

    // 返回3个值：迭代器函数、状态、初始控制变量
    // 这些值将被 for 循环使用：for i, v in ipairs(t) do ... end
    return 3;
}


/*
** [辅助函数] 加载辅助函数
**
** 处理加载函数的返回结果
**
** 详细功能说明：
** - 如果加载成功，返回加载的函数
** - 如果加载失败，返回 nil 和错误消息
** - 用于 loadfile、loadstring、load 等函数
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** @param status - int：加载状态（0表示成功）
**
** 返回值：
** @return int：返回值数量（1或2）
** 栈返回：加载的函数或 nil + 错误消息
**
** 算法复杂度：O(1) 时间，O(1) 空间
*/
static int load_aux (lua_State *L, int status)
{
    // 检查加载是否成功
    if (status == 0)
    {
        return 1;
    }
    else
    {
        // 加载失败，准备返回 nil + 错误消息
        lua_pushnil(L);
        // 将 nil 放在错误消息前面
        lua_insert(L, -2);

        // 返回 nil 加错误消息
        return 2;
    }
}


/*
** [文件操作] loadstring 函数实现
**
** 从字符串加载 Lua 代码块
**
** 详细功能说明：
** - 将字符串编译为 Lua 函数
** - 支持自定义代码块名称用于错误报告
** - 不执行代码，只进行编译
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：包含 Lua 代码的字符串
** - 参数2：代码块名称（可选，默认使用字符串本身）
**
** 返回值：
** @return int：返回值数量（1或2）
** 栈返回：编译后的函数或 nil + 错误消息
**
** 算法复杂度：O(n) 时间，O(n) 空间，其中 n 为字符串长度
**
** 注意事项：
** - 只编译不执行代码
** - 代码块名称用于错误消息和调试信息
** - 编译错误会返回 nil 和错误消息
*/
static int luaB_loadstring (lua_State *L)
{
    size_t l;

    // 检查第一个参数必须是字符串，获取字符串指针和长度
    const char *s = luaL_checklstring(L, 1, &l);

    // 获取可选的代码块名称（用于错误报告），默认使用字符串本身
    const char *chunkname = luaL_optstring(L, 2, s);

    // 调用 luaL_loadbuffer 编译字符串为 Lua 函数
    // 然后调用 load_aux 处理加载结果
    return load_aux(L, luaL_loadbuffer(L, s, l, chunkname));
}

/*
** [文件操作] loadfile 函数实现
**
** 从文件加载 Lua 代码块
**
** 详细功能说明：
** - 从指定文件读取并编译 Lua 代码
** - 支持从标准输入读取（文件名为 nil）
** - 不执行代码，只进行编译
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：文件名（可选，nil 表示标准输入）
**
** 返回值：
** @return int：返回值数量（1或2）
** 栈返回：编译后的函数或 nil + 错误消息
**
** 算法复杂度：O(n) 时间，O(n) 空间，其中 n 为文件大小
**
** 注意事项：
** - 只编译不执行代码
** - 文件不存在或读取错误会返回 nil 和错误消息
** - nil 文件名表示从标准输入读取
*/
static int luaB_loadfile (lua_State *L)
{
    // 获取可选的文件名参数，默认为 NULL（表示从标准输入读取）
    const char *fname = luaL_optstring(L, 1, NULL);

    // 调用 luaL_loadfile 从文件加载并编译 Lua 代码
    // 然后调用 load_aux 处理加载结果
    return load_aux(L, luaL_loadfile(L, fname));
}


/*
** [辅助函数] 通用加载函数的读取器
**
** 为 lua_load 函数提供读取器接口
**
** 详细功能说明：
** - lua_load 使用栈进行内部操作，所以读取器不能改变栈顶
** - 将结果字符串保存在栈的保留位置中
** - 通过调用用户提供的函数来获取代码块
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** @param ud - void*：用户数据（未使用）
** @param size - size_t*：返回字符串长度的指针
**
** 返回值：
** @return const char*：代码块字符串或 NULL
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 读取器函数必须返回字符串
** - 返回 nil 表示没有更多代码
** - 使用栈位置3作为保留槽
*/
static const char *generic_reader (lua_State *L, void *ud, size_t *size)
{
    // 避免编译器警告
    (void)ud;
    luaL_checkstack(L, 2, "too many nested functions");

    // 获取读取器函数并调用
    lua_pushvalue(L, 1);
    lua_call(L, 0, 1);

    if (lua_isnil(L, -1))
    {
        // 没有更多数据
        *size = 0;
        return NULL;
    }
    else if (lua_isstring(L, -1))
    {
        // 将字符串保存在保留的栈槽中
        lua_replace(L, 3);
        return lua_tolstring(L, 3, size);
    }
    else
    {
        luaL_error(L, "reader function must return a string");
    }

    // 避免编译器警告
    return NULL;
}


/*
** [文件操作] load 函数实现
**
** 从读取器函数加载 Lua 代码块
**
** 详细功能说明：
** - 使用用户提供的读取器函数逐步读取代码
** - 将读取的代码编译为 Lua 函数
** - 支持自定义代码块名称
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：读取器函数
** - 参数2：代码块名称（可选，默认为"=(load)"）
**
** 返回值：
** @return int：返回值数量（1或2）
** 栈返回：编译后的函数或 nil + 错误消息
**
** 算法复杂度：O(n) 时间，O(n) 空间，其中 n 为代码长度
*/
static int luaB_load (lua_State *L)
{
    int status;

    // 获取可选的代码块名称，默认为 "=(load)"
    const char *cname = luaL_optstring(L, 2, "=(load)");

    // 检查第一个参数必须是函数（读取器函数）
    luaL_checktype(L, 1, LUA_TFUNCTION);

    // 确保栈上有3个位置：读取器函数、代码块名称、保留槽
    lua_settop(L, 3);

    // 调用 lua_load 使用 generic_reader 从读取器函数加载代码
    status = lua_load(L, generic_reader, NULL, cname);

    // 调用 load_aux 处理加载结果
    return load_aux(L, status);
}

/*
** [文件操作] dofile 函数实现
**
** 加载并执行 Lua 文件
**
** 详细功能说明：
** - 加载指定的 Lua 文件
** - 立即执行加载的代码
** - 返回执行结果
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：文件名（可选，默认从标准输入读取）
**
** 返回值：
** @return int：执行结果的数量
** 栈返回：文件执行的返回值
**
** 算法复杂度：O(n) 时间，O(n) 空间，其中 n 为文件大小
**
** 注意事项：
** - 如果加载失败会抛出错误
** - 支持多返回值
*/
static int luaB_dofile (lua_State *L)
{
    // 获取可选的文件名，默认为 NULL（标准输入）
    const char *fname = luaL_optstring(L, 1, NULL);
    // 记录当前栈顶位置，用于计算返回值数量
    int n = lua_gettop(L);

    // 尝试加载文件
    if (luaL_loadfile(L, fname) != 0)
    {
        // 加载失败，抛出错误
        lua_error(L);
    }
    // 执行加载的函数，接受任意数量的返回值
    lua_call(L, 0, LUA_MULTRET);
    // 返回执行结果的数量（当前栈顶 - 原始栈顶）
    return lua_gettop(L) - n;
}


/*
** [错误处理] assert 函数实现
**
** 断言检查，条件为假时抛出错误
**
** 详细功能说明：
** - 检查第一个参数的真假性
** - 如果为真，返回所有参数
** - 如果为假，抛出错误并显示错误消息
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要检查的条件值
** - 参数2：错误消息（可选，默认为"assertion failed!"）
** - 参数3+：其他参数（条件为真时会被返回）
**
** 返回值：
** @return int：参数数量（条件为真时）或不返回（抛出错误）
** 栈返回：所有输入参数（条件为真时）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只有 nil 和 false 被视为假值
** - 条件为假时函数不会正常返回
** - 可以用于调试和参数验证
** - 错误消息可以自定义
*/
static int luaB_assert (lua_State *L)
{
    // 检查第一个参数存在（可以是任何类型包括 nil）
    luaL_checkany(L, 1);

    // 将第一个参数转换为布尔值进行真假性检查
    // 在 Lua 中，只有 nil 和 false 被视为假值，其他都是真值
    if (!lua_toboolean(L, 1))
    {
        // 断言失败：获取可选的错误消息（第二个参数）
        // 如果没有提供错误消息，使用默认的 "assertion failed!"
        // 调用 luaL_error 抛出错误并终止函数执行
        return luaL_error(L, "%s", luaL_optstring(L, 2, "assertion failed!"));
    }

    // 断言成功：返回栈上所有参数的数量
    // 这样调用者可以获得传入 assert 的所有参数
    return lua_gettop(L);
}


/*
** [表操作] unpack 函数实现
**
** 将表的数组部分解包为多个返回值
**
** 详细功能说明：
** - 将表中指定范围的元素作为多个返回值返回
** - 支持指定起始和结束索引
** - 默认从索引1开始到表的长度结束
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要解包的表
** - 参数2：起始索引（可选，默认为1）
** - 参数3：结束索引（可选，默认为表长度）
**
** 返回值：
** @return int：返回值数量（范围内的元素个数）
** 栈返回：表中指定范围的所有元素
**
** 算法复杂度：O(n) 时间，O(n) 空间，其中 n 为元素个数
**
** 注意事项：
** - 如果范围为空则返回0个值
** - 元素过多时会检查栈空间
** - 使用 rawget 避免元方法调用
*/
static int luaB_unpack (lua_State *L)
{
    int i, e, n;
    luaL_checktype(L, 1, LUA_TTABLE);
    i = luaL_optint(L, 2, 1);
    e = luaL_opt(L, luaL_checkint, 3, luaL_getn(L, 1));

    // 检查范围是否有效
    if (i > e)
    {
        // 空范围
        return 0;
    }

    // 计算元素数量并检查栈空间
    n = e - i + 1;
    // n <= 0 表示算术溢出
    if (n <= 0 || !lua_checkstack(L, n))
    {
        return luaL_error(L, "too many results to unpack");
    }

    // 压入所有元素到栈
    // 压入 arg[i]（避免溢出问题）
    lua_rawgeti(L, 1, i);
    // 压入 arg[i + 1...e]
    while (i++ < e)
    {
        lua_rawgeti(L, 1, i);
    }

    return n;
}


/*
** [参数处理] select 函数实现
**
** 选择和返回指定的参数
**
** 详细功能说明：
** - 如果第一个参数是字符串"#"，返回参数总数
** - 否则从指定索引开始返回所有后续参数
** - 支持负数索引（从末尾开始计数）
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：索引号或字符串"#"
** - 参数2+：要选择的参数
**
** 返回值：
** @return int：返回值数量（取决于操作类型）
** 栈返回：参数数量（"#"操作）或选中的参数（索引操作）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** - select("#", a, b, c) 返回 3
** - select(2, a, b, c) 返回 b, c
** - select(-1, a, b, c) 返回 c
**
** 注意事项：
** - 索引从1开始计数
** - 负数索引从末尾开始计数
** - 索引超出范围会被调整到有效范围
** - "#"操作不计算第一个参数本身
*/
static int luaB_select (lua_State *L)
{
    // 获取参数总数
    int n = lua_gettop(L);

    // 检查第一个参数是否为字符串 "#"
    if (lua_type(L, 1) == LUA_TSTRING && *lua_tostring(L, 1) == '#')
    {
        // 返回参数数量（不包括 "#" 本身）
        lua_pushinteger(L, n-1);
        return 1;
    }
    else
    {
        // 第一个参数是索引号
        int i = luaL_checkint(L, 1);
        if (i < 0)
        {
            // 负数索引：从末尾开始计数
            i = n + i;
        }
        else if (i > n)
        {
            // 索引超出范围：调整到最大值
            i = n;
        }
        // 检查调整后的索引是否有效
        luaL_argcheck(L, 1 <= i, 1, "index out of range");
        // 返回从索引 i 开始的所有参数（参数已在栈上）
        return n - i;
    }
}


/*
** [错误处理] pcall 函数实现
**
** 在保护模式下调用函数
**
** 详细功能说明：
** - 在保护模式下调用指定函数
** - 如果调用成功，返回 true 和所有结果
** - 如果调用失败，返回 false 和错误消息
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要调用的函数
** - 参数2+：传递给函数的参数
**
** 返回值：
** @return int：返回值数量（1 + 函数返回值数量）
** 栈返回：状态（true/false）+ 结果或错误消息
**
** 算法复杂度：O(1) 时间，O(1) 空间（不包括被调用函数）
**
** 注意事项：
** - 不使用错误处理函数
** - 捕获所有类型的错误
** - 第一个返回值总是布尔状态
*/
static int luaB_pcall (lua_State *L)
{
    int status;
    luaL_checkany(L, 1);

    // 在保护模式下调用函数
    status = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);

    // 准备返回值：状态 + 结果
    lua_pushboolean(L, (status == 0));
    lua_insert(L, 1);

    // 返回状态 + 所有结果
    return lua_gettop(L);
}

/*
** [错误处理] xpcall 函数实现
**
** 在保护模式下调用函数，使用自定义错误处理函数
**
** 详细功能说明：
** - 在保护模式下调用指定函数
** - 使用用户提供的错误处理函数
** - 如果调用成功，返回 true 和所有结果
** - 如果调用失败，返回 false 和错误处理函数的结果
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要调用的函数
** - 参数2：错误处理函数
**
** 返回值：
** @return int：返回值数量（1 + 函数返回值数量）
** 栈返回：状态（true/false）+ 结果或处理后的错误消息
**
** 算法复杂度：O(1) 时间，O(1) 空间（不包括被调用函数）
**
** 注意事项：
** - 错误处理函数会在错误发生时被调用
** - 错误处理函数接收原始错误消息作为参数
** - 可以用于添加调用栈信息等
*/
static int luaB_xpcall (lua_State *L)
{
    int status;
    luaL_checkany(L, 2);
    lua_settop(L, 2);

    // 将错误处理函数放在要调用的函数下面
    lua_insert(L, 1);

    // 在保护模式下调用函数，使用错误处理函数
    status = lua_pcall(L, 0, LUA_MULTRET, 1);

    // 准备返回值：状态 + 结果
    lua_pushboolean(L, (status == 0));
    lua_replace(L, 1);

    // 返回状态 + 所有结果
    return lua_gettop(L);
}


/*
** [基础函数] tostring 函数实现
**
** 将任意值转换为字符串表示
**
** 详细功能说明：
** - 首先尝试调用对象的 __tostring 元方法
** - 如果没有元方法，根据类型进行默认转换
** - 数字和字符串直接转换，布尔值转为 "true"/"false"
** - nil 转为 "nil"，其他类型显示类型名和地址
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要转换的值
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：值的字符串表示
**
** 算法复杂度：O(1) 时间，O(n) 空间，其中 n 为字符串长度
**
** 注意事项：
** - 优先使用 __tostring 元方法
** - 数字转换可能涉及格式化
** - 对象地址用于区分不同实例
*/
static int luaB_tostring (lua_State *L)
{
    // 检查参数存在（可以是任何类型包括 nil）
    luaL_checkany(L, 1);
    // 首先尝试调用 __tostring 元方法
    if (luaL_callmeta(L, 1, "__tostring"))
    {
        // 如果元方法存在并成功调用，使用其返回值
        return 1;
    }

    // 根据值的类型进行默认的字符串转换
    switch (lua_type(L, 1))
    {
        case LUA_TNUMBER:
            // 数字类型：使用 Lua 的数字到字符串转换
            lua_pushstring(L, lua_tostring(L, 1));
            break;
        case LUA_TSTRING:
            // 字符串类型：直接返回原字符串
            lua_pushvalue(L, 1);
            break;
        case LUA_TBOOLEAN:
            // 布尔类型：转换为 "true" 或 "false"
            lua_pushstring(L, (lua_toboolean(L, 1) ? "true" : "false"));
            break;
        case LUA_TNIL:
            // nil 类型：返回字符串 "nil"
            lua_pushliteral(L, "nil");
            break;
        default:
            // 其他类型：显示类型名和内存地址
            lua_pushfstring(L, "%s: %p", luaL_typename(L, 1), lua_topointer(L, 1));
            break;
    }
    return 1;
}


/*
** [特殊函数] newproxy 函数实现
**
** 创建一个用户数据代理对象
**
** 详细功能说明：
** - 创建一个空的用户数据作为代理
** - 可以为代理设置元表
** - 支持元表的复制和验证
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：布尔值、代理对象或 nil
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：新创建的代理对象
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - false 或 nil 创建无元表的代理
** - true 创建新元表的代理
** - 传入代理对象则复制其元表
*/
static int luaB_newproxy (lua_State *L)
{
    // 确保只有一个参数
    lua_settop(L, 1);
    // 创建一个空的用户数据作为代理对象
    lua_newuserdata(L, 0);

    // 检查第一个参数的值来决定如何处理元表
    if (lua_toboolean(L, 1) == 0)
    {
        // 参数为 false 或 nil：创建没有元表的代理
        return 1;
    }
    else if (lua_isboolean(L, 1))
    {
        // 参数为 true：创建新的元表
        lua_newtable(L);
        // 将新元表标记为有效（在弱表中记录）
        lua_pushvalue(L, -1);
        lua_pushboolean(L, 1);
        // 在上值弱表中设置 weaktable[metatable] = true
        lua_rawset(L, lua_upvalueindex(1));
    }
    else
    {
        // 参数是其他代理对象：复制其元表
        int validproxy = 0;
        if (lua_getmetatable(L, 1))
        {
            // 检查该元表是否在弱表中被标记为有效
            lua_rawget(L, lua_upvalueindex(1));
            validproxy = lua_toboolean(L, -1);
            // 清理栈上的检查结果
            lua_pop(L, 1);
        }
        // 确保传入的是有效的代理对象
        luaL_argcheck(L, validproxy, 1, "boolean or proxy expected");
        // 获取有效的元表用于新代理
        lua_getmetatable(L, 1);
    }
    // 为新创建的代理对象设置元表
    lua_setmetatable(L, 2);
    return 1;
}


// 基础库函数注册表
// 包含所有要注册到全局环境的基础函数
static const luaL_Reg base_funcs[] = {
  {"assert", luaB_assert},                    // 断言函数
  {"collectgarbage", luaB_collectgarbage},    // 垃圾回收控制
  {"dofile", luaB_dofile},                    // 执行文件
  {"error", luaB_error},                      // 抛出错误
  {"gcinfo", luaB_gcinfo},                    // 垃圾回收信息
  {"getfenv", luaB_getfenv},                  // 获取函数环境
  {"getmetatable", luaB_getmetatable},        // 获取元表
  {"loadfile", luaB_loadfile},                // 加载文件
  {"load", luaB_load},                        // 从读取器加载
  {"loadstring", luaB_loadstring},            // 从字符串加载
  {"next", luaB_next},                        // 表迭代器
  {"pcall", luaB_pcall},                      // 保护调用
  {"print", luaB_print},                      // 打印函数
  {"rawequal", luaB_rawequal},                // 原始相等比较
  {"rawget", luaB_rawget},                    // 原始获取
  {"rawset", luaB_rawset},                    // 原始设置
  {"select", luaB_select},                    // 参数选择
  {"setfenv", luaB_setfenv},                  // 设置函数环境
  {"setmetatable", luaB_setmetatable},        // 设置元表
  {"tonumber", luaB_tonumber},                // 转换为数字
  {"tostring", luaB_tostring},                // 转换为字符串
  {"type", luaB_type},                        // 获取类型
  {"unpack", luaB_unpack},                    // 解包表
  {"xpcall", luaB_xpcall},                    // 扩展保护调用
  {NULL, NULL}                                // 结束标记
};


/*
** {======================================================
** Coroutine library
** =======================================================
*/

// 协程状态常量定义
#define CO_RUN	0	// 运行中
#define CO_SUS	1	// 挂起
#define CO_NOR	2	// 正常（它恢复了另一个协程）
#define CO_DEAD	3	// 死亡

// 协程状态名称数组
static const char *const statnames[] =
    {"running", "suspended", "normal", "dead"};

/*
** [辅助函数] 获取协程状态
**
** 确定协程的当前状态
**
** 详细功能说明：
** - 检查协程是否正在运行、挂起、正常或死亡
** - 根据协程的内部状态和调用栈情况判断
** - 用于协程状态查询和恢复操作
**
** 参数说明：
** @param L - lua_State*：主 Lua 状态机指针
** @param co - lua_State*：要检查的协程状态机指针
**
** 返回值：
** @return int：协程状态常量（CO_RUN/CO_SUS/CO_NOR/CO_DEAD）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 如果是同一个状态机则为运行状态
** - 通过调用栈判断是否有活动帧
** - 错误状态的协程被视为死亡
*/
static int costatus (lua_State *L, lua_State *co)
{
    // 检查是否为同一个状态机（主线程调用自己）
    if (L == co)
    {
        return CO_RUN;
    }

    // 根据协程的内部状态判断其当前状态
    switch (lua_status(co))
    {
        case LUA_YIELD:
            // 协程处于让出状态（被 yield 挂起）
            return CO_SUS;

        case 0:
        {
            // 协程没有错误，需要进一步判断具体状态
            lua_Debug ar;

            // 检查协程是否有活动的调用帧
            if (lua_getstack(co, 0, &ar) > 0)
            {
                // 有活动帧：协程正在运行（恢复了另一个协程）
                return CO_NOR;
            }
            else if (lua_gettop(co) == 0)
            {
                // 没有活动帧且栈为空：协程已经执行完毕
                return CO_DEAD;
            }
            else
            {
                // 没有活动帧但栈不为空：协程处于初始状态（尚未开始执行）
                return CO_SUS;
            }
        }

        default:
            // 协程发生了错误：视为死亡状态
            return CO_DEAD;
    }
}


/*
** [协程库] coroutine.status 函数实现
**
** 获取协程的状态字符串
**
** 详细功能说明：
** - 返回协程当前状态的字符串表示
** - 状态包括：running、suspended、normal、dead
** - 用于检查协程的执行状态
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要检查状态的协程对象
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：状态字符串
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 可能的返回值：
** - "running": 协程正在运行
** - "suspended": 协程被挂起
** - "normal": 协程恢复了另一个协程
** - "dead": 协程已结束或出错
**
** 注意事项：
** - 参数必须是协程对象
** - 返回的是状态的字符串表示
** - 可用于协程状态的条件判断
*/
static int luaB_costatus (lua_State *L)
{
    // 将第一个参数转换为协程对象
    lua_State *co = lua_tothread(L, 1);

    // 检查参数确实是协程对象
    luaL_argcheck(L, co, 1, "coroutine expected");

    // 调用 costatus 获取协程状态，然后从状态名称数组中获取对应字符串
    // 将状态字符串压入栈
    lua_pushstring(L, statnames[costatus(L, co)]);

    // 返回1个值：协程状态字符串（"running", "suspended", "normal", "dead"）
    return 1;
}


/*
** [辅助函数] 协程恢复辅助函数
**
** 恢复协程执行的核心实现
**
** 详细功能说明：
** - 检查协程状态是否可以恢复
** - 将参数从主线程移动到协程
** - 恢复协程执行并处理结果
** - 处理成功和错误两种情况
**
** 参数说明：
** @param L - lua_State*：主 Lua 状态机指针
** @param co - lua_State*：要恢复的协程状态机指针
** @param narg - int：传递给协程的参数数量
**
** 返回值：
** @return int：结果数量（成功时）或 -1（错误时）
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 为参数/结果数量
**
** 注意事项：
** - 只能恢复挂起状态的协程
** - 需要检查栈空间是否足够
** - 返回 -1 表示错误，非负数表示结果数量
*/
static int auxresume (lua_State *L, lua_State *co, int narg)
{
    // 获取协程当前状态
    int status = costatus(L, co);

    // 检查协程栈是否有足够空间容纳参数
    if (!lua_checkstack(co, narg))
    {
        luaL_error(L, "too many arguments to resume");
    }

    // 只有挂起状态的协程才能被恢复
    if (status != CO_SUS)
    {
        lua_pushfstring(L, "cannot resume %s coroutine", statnames[status]);
        // 返回错误标志
        return -1;
    }

    // 准备恢复协程执行
    // 将参数从主线程移动到协程
    lua_xmove(L, co, narg);
    // 设置协程的调用级别
    lua_setlevel(L, co);

    // 恢复协程执行
    status = lua_resume(co, narg);

    // 处理协程执行结果
    if (status == 0 || status == LUA_YIELD)
    {
        // 协程正常结束或让出：获取返回值数量
        int nres = lua_gettop(co);
        if (!lua_checkstack(L, nres + 1))
        {
            luaL_error(L, "too many results to resume");
        }

        // 将协程的返回值移动到主线程
        lua_xmove(co, L, nres);
        return nres;
    }
    else
    {
        // 协程执行出错：移动错误消息到主线程
        lua_xmove(co, L, 1);
        // 返回错误标志
        return -1;
    }
}


/*
** [协程库] coroutine.resume 函数实现
**
** 恢复协程执行
**
** 详细功能说明：
** - 恢复指定协程的执行
** - 将额外参数传递给协程
** - 返回执行状态和结果
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：要恢复的协程
** - 参数2+：传递给协程的参数
**
** 返回值：
** @return int：返回值数量（至少2个）
** 栈返回：状态（true/false）+ 结果或错误消息
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 为参数数量
**
** 注意事项：
** - 第一个返回值总是布尔状态
** - 成功时返回 true + 协程的返回值
** - 失败时返回 false + 错误消息
*/
static int luaB_coresume (lua_State *L)
{
    // 获取第一个参数（应该是协程对象）
    lua_State *co = lua_tothread(L, 1);
    int r;

    // 确保第一个参数确实是协程
    luaL_argcheck(L, co, 1, "coroutine expected");

    // 调用辅助函数恢复协程，传递除第一个参数外的所有参数
    r = auxresume(L, co, lua_gettop(L) - 1);

    // 检查恢复操作的结果
    if (r < 0)
    {
        // 恢复失败：准备返回 false + 错误消息
        lua_pushboolean(L, 0);
        lua_insert(L, -2);
        // 返回 false + 错误消息（2个值）
        return 2;
    }
    else
    {
        // 恢复成功：准备返回 true + 协程的返回值
        lua_pushboolean(L, 1);
        lua_insert(L, -(r + 1));
        // 返回 true + 协程的所有返回值（1 + r 个值）
        return r + 1;
    }
}


/*
** [辅助函数] 协程包装器辅助函数
**
** coroutine.wrap 返回的函数的实际实现
**
** 详细功能说明：
** - 恢复协程执行并处理结果
** - 如果出错则抛出错误而不是返回状态
** - 成功时直接返回协程的结果
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：传递给协程的所有参数
**
** 返回值：
** @return int：协程返回值的数量
** 栈返回：协程的返回值
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 为参数数量
**
** 注意事项：
** - 协程对象存储在上值中
** - 错误时会抛出异常而不是返回状态
** - 会添加额外的错误信息
*/
static int luaB_auxwrap (lua_State *L)
{
    // 从上值中获取协程对象
    lua_State *co = lua_tothread(L, lua_upvalueindex(1));

    // 恢复协程执行，传递所有参数
    int r = auxresume(L, co, lua_gettop(L));

    // 检查执行结果
    if (r < 0)
    {
        // 执行出错：处理错误信息
        if (lua_isstring(L, -1))
        {
            // 如果错误对象是字符串，添加调用位置信息
            luaL_where(L, 1);
            lua_insert(L, -2);
            lua_concat(L, 2);
        }

        // 抛出错误（不返回状态，直接传播错误）
        lua_error(L);
    }

    // 执行成功：直接返回协程的返回值
    return r;
}

/*
** [协程库] coroutine.create 函数实现
**
** 创建新的协程
**
** 详细功能说明：
** - 创建一个新的协程线程
** - 将指定的 Lua 函数作为协程的主函数
** - 返回协程对象
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：作为协程主函数的 Lua 函数
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：新创建的协程对象
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只接受 Lua 函数，不接受 C 函数
** - 协程创建后处于挂起状态
** - 需要使用 resume 来启动协程
*/
static int luaB_cocreate (lua_State *L)
{
    // 创建新的协程线程
    lua_State *NL = lua_newthread(L);

    // 检查参数必须是 Lua 函数（不能是 C 函数）
    luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1), 1,
        "Lua function expected");

    // 将函数移动到新协程中
    lua_pushvalue(L, 1);
    lua_xmove(L, NL, 1);

    return 1;
}

/*
** [协程库] coroutine.wrap 函数实现
**
** 创建协程并返回包装函数
**
** 详细功能说明：
** - 创建新协程并返回一个函数来操作它
** - 返回的函数调用时会恢复协程执行
** - 比 create/resume 更简单的协程使用方式
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：
** - 参数1：作为协程主函数的 Lua 函数
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：用于操作协程的包装函数
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 包装函数出错时会抛出异常
** - 不返回状态，只返回结果
** - 协程对象作为闭包的上值存储
*/
static int luaB_cowrap (lua_State *L)
{
    // 首先创建协程（协程对象会在栈顶）
    luaB_cocreate(L);

    // 创建闭包，将协程对象作为上值，luaB_auxwrap 作为函数体
    lua_pushcclosure(L, luaB_auxwrap, 1);

    return 1;
}

/*
** [协程库] coroutine.yield 函数实现
**
** 挂起当前协程
**
** 详细功能说明：
** - 挂起当前协程的执行
** - 将参数作为 resume 的返回值
** - 控制权返回给调用 resume 的线程
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** 栈参数：要返回给 resume 调用者的值
**
** 返回值：
** @return int：下次 resume 时传入的参数数量
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只能在协程内部调用
** - 在主线程中调用会出错
** - 参数会成为 resume 的返回值
*/
static int luaB_yield (lua_State *L)
{
    // 获取栈上参数的数量
    // 调用 lua_yield 挂起当前协程，将所有参数作为让出值
    // 这些参数将成为对应 resume 调用的返回值
    return lua_yield(L, lua_gettop(L));
}

/*
** [协程库] coroutine.running 函数实现
**
** 返回当前运行的协程
**
** 详细功能说明：
** - 如果在协程中调用，返回协程对象
** - 如果在主线程中调用，返回 nil
** - 用于检查当前是否在协程中执行
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
**
** 返回值：
** @return int：返回值数量（总是 1）
** 栈返回：当前协程对象或 nil
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 主线程不被视为协程
** - 可用于判断代码是否在协程中运行
** - 协程可以通过此函数获取自身引用
*/
static int luaB_corunning (lua_State *L)
{
    // 调用 lua_pushthread 将当前线程对象压入栈
    // 如果是主线程，函数返回 true；如果是协程，返回 false
    if (lua_pushthread(L))
    {
        // 是主线程：主线程不被视为协程，弹出线程对象并压入 nil
        lua_pop(L, 1);
        lua_pushnil(L);
    }
    // 否则栈顶已经是当前协程对象，直接返回

    // 返回1个值：当前协程对象或 nil（如果在主线程中）
    return 1;
}


// 协程库函数注册表
// 包含所有要注册到 coroutine 表的协程函数
static const luaL_Reg co_funcs[] = {
  {"create", luaB_cocreate},      // 创建协程
  {"resume", luaB_coresume},      // 恢复协程
  {"running", luaB_corunning},    // 获取当前协程
  {"status", luaB_costatus},      // 获取协程状态
  {"wrap", luaB_cowrap},          // 包装协程
  {"yield", luaB_yield},          // 让出协程
  {NULL, NULL}                    // 结束标记
};

// 协程库结束标记

/*
** [辅助函数] 辅助打开函数
**
** 为需要上值的函数创建闭包并注册
**
** 详细功能说明：
** - 将辅助函数作为上值创建主函数的闭包
** - 将闭包注册到指定名称
** - 用于 pairs、ipairs 等需要辅助函数的情况
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
** @param name - const char*：函数名称
** @param f - lua_CFunction：主函数
** @param u - lua_CFunction：辅助函数（作为上值）
**
** 返回值：
** @return void：无返回值
**
** 算法复杂度：O(1) 时间，O(1) 空间
*/
static void auxopen (lua_State *L, const char *name,
                     lua_CFunction f, lua_CFunction u)
{
    // 将辅助函数压入栈作为上值
    lua_pushcfunction(L, u);

    // 创建闭包：主函数 f 以辅助函数 u 作为上值
    lua_pushcclosure(L, f, 1);

    // 将闭包注册到当前表中，使用指定的名称
    lua_setfield(L, -2, name);
}

/*
** [初始化函数] 基础库打开函数
**
** 初始化并注册基础库的所有函数
**
** 详细功能说明：
** - 设置全局变量 _G 和 _VERSION
** - 注册所有基础库函数到全局表
** - 为特殊函数设置必要的上值
** - 创建 newproxy 所需的弱表
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
**
** 返回值：
** @return void：无返回值
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 为函数数量
**
** 注意事项：
** - _G 指向全局表本身
** - ipairs 和 pairs 需要辅助函数作为上值
** - newproxy 需要弱表来跟踪有效的代理
*/
static void base_open (lua_State *L)
{
    // 设置全局变量 _G 指向全局表本身
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setglobal(L, "_G");

    // 将基础库函数注册到全局表中
    luaL_register(L, "_G", base_funcs);

    // 设置全局变量 _VERSION 为 Lua 版本字符串
    lua_pushliteral(L, LUA_VERSION);
    lua_setglobal(L, "_VERSION");

    // 注册需要辅助函数的迭代器
    // ipairs 需要 ipairsaux 作为上值
    auxopen(L, "ipairs", luaB_ipairs, ipairsaux);
    // pairs 需要 luaB_next 作为上值
    auxopen(L, "pairs", luaB_pairs, luaB_next);

    // 为 newproxy 函数创建弱表上值
    // 创建一个新表作为弱表
    lua_createtable(L, 0, 1);
    // 将表设置为自己的元表
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2);

    // 设置弱表模式为键值都弱引用
    lua_pushliteral(L, "kv");
    lua_setfield(L, -2, "__mode");

    // 创建 newproxy 闭包，弱表作为上值
    lua_pushcclosure(L, luaB_newproxy, 1);
    // 注册 newproxy 函数到全局环境
    lua_setglobal(L, "newproxy");
}


/*
** [库入口] 基础库主入口函数
**
** Lua 基础库的主要入口点
**
** 详细功能说明：
** - 打开基础库，注册所有基础函数
** - 打开协程库，注册协程相关函数
** - 返回打开的库的数量
**
** 参数说明：
** @param L - lua_State*：Lua 虚拟机状态指针
**
** 返回值：
** @return int：打开的库数量（总是 2）
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 为函数数量
**
** 注意事项：
** - 这是 Lua 加载基础库时调用的函数
** - 基础库函数注册到全局环境
** - 协程库函数注册到 coroutine 表
** - 返回值表示栈上留下的库表数量
*/
LUALIB_API int luaopen_base (lua_State *L)
{
    // 打开基础库，注册所有基础函数到全局环境
    base_open(L);

    // 注册协程库函数到 coroutine 表
    luaL_register(L, LUA_COLIBNAME, co_funcs);

    // 返回 2 表示在栈上留下了两个库表（全局表和协程表）
    return 2;
}

