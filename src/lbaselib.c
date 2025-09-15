/**
 * @file lbaselib.c
 * @brief Lua基础库实现：核心函数和基本操作
 *
 * 版权信息：
 * $Id: lbaselib.c,v 1.191.1.6 2008/02/14 16:46:22 roberto Exp $
 * Lua基础库实现
 * 版权声明见lua.h文件
 *
 * 模块概述：
 * 本模块实现了Lua的基础库函数，提供了最核心和最常用的标准库功能。
 * 这些函数构成了Lua编程的基础，包括输入输出、类型操作、元表管理、
 * 迭代器、错误处理、代码加载等核心功能。
 *
 * 主要功能分类：
 * 1. 输入输出函数：print等基本输出功能
 * 2. 类型操作：type、tonumber、tostring等类型转换
 * 3. 元表操作：getmetatable、setmetatable等元编程支持
 * 4. 迭代器：pairs、ipairs、next等表遍历功能
 * 5. 错误处理：error、assert、pcall、xpcall等异常处理
 * 6. 代码加载：load、loadfile、loadstring、dofile等动态执行
 * 7. 原始操作：rawget、rawset、rawequal等绕过元方法的操作
 * 8. 垃圾回收：collectgarbage等内存管理控制
 * 9. 协程支持：coroutine库的完整实现
 * 10. 实用工具：select、unpack等辅助函数
 *
 * 设计特点：
 * 1. 高效的C实现，最小化运行时开销
 * 2. 完整的错误检查和参数验证
 * 3. 灵活的参数处理，支持可选参数和默认值
 * 4. 统一的返回值约定，便于Lua代码使用
 * 5. 良好的内存管理，避免内存泄漏
 *
 * 实现原则：
 * - 严格的类型检查和参数验证
 * - 一致的错误处理和异常报告
 * - 高效的栈操作和内存使用
 * - 符合Lua语言规范的行为
 * - 良好的可移植性和兼容性
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2008-2011
 *
 * @note 本模块是Lua标准库的核心，提供了最基础和最重要的功能
 * @see lua.h, lauxlib.h, lualib.h
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

/**
 * @brief 打印函数实现
 *
 * 实现Lua的print函数，将参数转换为字符串并输出到标准输出。
 * 这是Lua中最常用的调试和输出函数，支持多个参数的同时输出。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（总是0，因为print不返回值）
 *
 * @note 如果系统不支持stdout，可以移除此函数
 * @note 可以通过修改fputs调用来重定向输出到其他位置
 *
 * @see lua_gettop, lua_getglobal, lua_call, lua_tostring
 *
 * 实现逻辑：
 * 1. 获取参数数量
 * 2. 获取全局tostring函数
 * 3. 对每个参数调用tostring进行类型转换
 * 4. 输出转换后的字符串，参数间用制表符分隔
 * 5. 最后输出换行符
 *
 * 输出格式：
 * - 多个参数用制表符(\t)分隔
 * - 最后自动添加换行符(\n)
 * - 使用tostring函数进行统一的类型转换
 *
 * 错误处理：
 * - 如果tostring返回非字符串值，抛出错误
 * - 确保输出的一致性和可靠性
 *
 * 使用场景：
 * - 调试信息输出和程序状态显示
 * - 简单的用户交互和结果展示
 * - 脚本执行过程中的信息反馈
 * - 开发和测试阶段的数据检查
 *
 * 可定制性：
 * - 可以修改输出目标（控制台、日志文件等）
 * - 可以调整输出格式和分隔符
 * - 可以添加时间戳等额外信息
 *
 * @example
 * print("Hello", "World", 123)  -- 输出: Hello	World	123
 * print()                       -- 输出: (空行)
 * print(nil, true, false)       -- 输出: nil	true	false
 */
static int luaB_print (lua_State *L) {
    int n = lua_gettop(L);  /* 参数数量 */
    int i;
    lua_getglobal(L, "tostring");
    for (i=1; i<=n; i++) {
        const char *s;
        lua_pushvalue(L, -1);  /* 要调用的函数 */
        lua_pushvalue(L, i);   /* 要打印的值 */
        lua_call(L, 1, 1);
        s = lua_tostring(L, -1);  /* 获取结果 */
        if (s == NULL)
            return luaL_error(L, LUA_QL("tostring") " must return a string to "
                               LUA_QL("print"));
        if (i>1) fputs("\t", stdout);
        fputs(s, stdout);
        lua_pop(L, 1);  /* 弹出结果 */
    }
    fputs("\n", stdout);
    return 0;
}


/**
 * @brief 字符串转数字函数
 *
 * 将字符串或其他类型转换为数字。支持不同进制的转换，
 * 是Lua中最重要的类型转换函数之一。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：转换后的数字或nil）
 *
 * 参数说明：
 * - 参数1：要转换的值（字符串或数字）
 * - 参数2：可选的进制基数（2-36，默认为10）
 *
 * @note 当base为10时，使用标准的数字转换
 * @note 当base为其他值时，只接受字符串输入
 *
 * @see lua_isnumber, lua_tonumber, luaL_checkstring, strtoul
 *
 * 转换规则：
 * 1. 如果输入已经是数字且base=10，直接返回
 * 2. 对于字符串输入，根据指定进制解析
 * 3. 忽略前导和尾随空白字符
 * 4. 转换失败时返回nil
 *
 * 进制支持：
 * - 2进制：只包含0和1
 * - 8进制：包含0-7
 * - 10进制：包含0-9（默认）
 * - 16进制：包含0-9和A-F
 * - 最高36进制：包含0-9和A-Z
 *
 * 错误处理：
 * - 进制超出范围（2-36）时抛出错误
 * - 字符串包含无效字符时返回nil
 * - 空字符串或纯空白字符串返回nil
 *
 * 性能优化：
 * - 对于已经是数字的值，避免不必要的转换
 * - 使用高效的strtoul函数进行字符串解析
 * - 最小化栈操作和内存分配
 *
 * 使用场景：
 * - 用户输入的字符串转换为数字
 * - 不同进制数字的解析和转换
 * - 数据验证和类型检查
 * - 配置文件和参数解析
 *
 * @example
 * tonumber("123")      -- 返回: 123
 * tonumber("123.45")   -- 返回: 123.45
 * tonumber("FF", 16)   -- 返回: 255
 * tonumber("1010", 2)  -- 返回: 10
 * tonumber("invalid")  -- 返回: nil
 */
static int luaB_tonumber (lua_State *L) {
    int base = luaL_optint(L, 2, 10);
    if (base == 10) {  /* 标准转换 */
        luaL_checkany(L, 1);
        if (lua_isnumber(L, 1)) {
            lua_pushnumber(L, lua_tonumber(L, 1));
            return 1;
        }
    }
    else {
        const char *s1 = luaL_checkstring(L, 1);
        char *s2;
        unsigned long n;
        luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
        n = strtoul(s1, &s2, base);
        if (s1 != s2) {  /* 至少有一个有效数字？ */
            while (isspace((unsigned char)(*s2))) s2++;  /* 跳过尾随空格 */
            if (*s2 == '\0') {  /* 没有无效的尾随字符？ */
                lua_pushnumber(L, (lua_Number)n);
                return 1;
            }
        }
    }
    lua_pushnil(L);  /* 否则不是数字 */
    return 1;
}


/**
 * @brief 错误抛出函数
 *
 * 抛出一个错误并终止当前函数的执行。这是Lua中标准的
 * 错误处理机制，支持错误级别和位置信息的自动添加。
 *
 * @param L Lua状态机指针
 * @return 不返回（函数会抛出错误）
 *
 * 参数说明：
 * - 参数1：错误消息（任意类型，通常是字符串）
 * - 参数2：可选的错误级别（默认为1）
 *
 * @note 错误级别决定了错误报告中显示的调用位置
 * @note 级别0表示不添加位置信息
 *
 * @see lua_error, luaL_where, lua_concat
 *
 * 错误级别说明：
 * - 0：不添加位置信息
 * - 1：当前函数的调用位置（默认）
 * - 2：调用当前函数的函数位置
 * - n：向上n级的调用位置
 *
 * 实现逻辑：
 * 1. 获取错误级别参数
 * 2. 如果是字符串且级别>0，添加位置信息
 * 3. 调用lua_error抛出错误
 *
 * 使用场景：
 * - 参数验证失败时抛出错误
 * - 运行时条件检查失败
 * - 自定义错误处理和异常报告
 * - 库函数的错误传播
 *
 * @example
 * error("Something went wrong")     -- 抛出错误并显示位置
 * error("Error message", 0)        -- 抛出错误不显示位置
 * error("Error at caller", 2)      -- 显示调用者的位置
 */
static int luaB_error (lua_State *L) {
    int level = luaL_optint(L, 2, 1);
    lua_settop(L, 1);
    if (lua_isstring(L, 1) && level > 0) {  /* 添加额外信息？ */
        luaL_where(L, level);
        lua_pushvalue(L, 1);
        lua_concat(L, 2);
    }
    return lua_error(L);
}

/**
 * @brief 获取元表函数
 *
 * 获取指定对象的元表。如果对象有__metatable字段，
 * 则返回该字段的值，否则返回实际的元表。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：元表或nil）
 *
 * 参数说明：
 * - 参数1：要获取元表的对象（任意类型）
 *
 * @note 如果对象没有元表，返回nil
 * @note __metatable字段用于隐藏真实的元表
 *
 * @see lua_getmetatable, luaL_getmetafield
 *
 * 获取逻辑：
 * 1. 尝试获取对象的元表
 * 2. 如果没有元表，返回nil
 * 3. 检查是否有__metatable字段
 * 4. 返回__metatable字段值或实际元表
 *
 * 元表保护机制：
 * - __metatable字段可以隐藏真实元表
 * - 防止外部代码直接访问元表
 * - 提供受控的元表访问接口
 *
 * 使用场景：
 * - 检查对象是否有元表
 * - 获取对象的元编程接口
 * - 实现类型检查和对象识别
 * - 调试和反射操作
 *
 * @example
 * getmetatable({})           -- 返回: nil
 * getmetatable("string")     -- 返回: string的元表
 * getmetatable(protected)    -- 返回: __metatable字段的值
 */
static int luaB_getmetatable (lua_State *L) {
    luaL_checkany(L, 1);
    if (!lua_getmetatable(L, 1)) {
        lua_pushnil(L);
        return 1;  /* 没有元表 */
    }
    luaL_getmetafield(L, 1, "__metatable");
    return 1;  /* 返回__metatable字段（如果存在）或元表 */
}

/**
 * @brief 设置元表函数
 *
 * 为指定的表设置元表。只能为表类型的对象设置元表，
 * 并且不能修改受保护的元表。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：设置元表的表）
 *
 * 参数说明：
 * - 参数1：要设置元表的表
 * - 参数2：新的元表（表或nil）
 *
 * @note 只能为表类型设置元表
 * @note 如果原元表有__metatable字段，则不能修改
 *
 * @see lua_setmetatable, luaL_getmetafield, luaL_checktype
 *
 * 设置逻辑：
 * 1. 检查第一个参数是否为表
 * 2. 检查第二个参数是否为表或nil
 * 3. 检查原元表是否受保护
 * 4. 设置新的元表
 *
 * 保护机制：
 * - __metatable字段防止元表被修改
 * - 确保元编程的安全性和一致性
 * - 防止恶意代码破坏对象行为
 *
 * 使用场景：
 * - 为表添加元编程功能
 * - 实现面向对象编程
 * - 创建具有特殊行为的对象
 * - 实现运算符重载和访问控制
 *
 * @example
 * setmetatable({}, mt)       -- 为空表设置元表
 * setmetatable(t, nil)       -- 移除表的元表
 * setmetatable(protected, mt) -- 错误：受保护的元表
 */
static int luaB_setmetatable (lua_State *L) {
    int t = lua_type(L, 2);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2,
                      "nil or table expected");
    if (luaL_getmetafield(L, 1, "__metatable"))
        luaL_error(L, "cannot change a protected metatable");
    lua_settop(L, 2);
    lua_setmetatable(L, 1);
    return 1;
}


/**
 * @brief 获取函数对象的辅助函数
 *
 * 根据参数获取函数对象，支持直接传入函数或通过调用栈级别获取。
 * 这是getfenv和setfenv函数的共用辅助函数。
 *
 * @param L Lua状态机指针
 * @param opt 是否允许可选参数（1表示允许，0表示必须）
 *
 * @note 函数执行后，目标函数会被压入栈顶
 * @note 如果是尾调用，可能无法获取函数环境
 *
 * @see lua_isfunction, lua_getstack, lua_getinfo
 *
 * 获取逻辑：
 * 1. 如果第一个参数是函数，直接使用
 * 2. 否则将参数作为调用栈级别
 * 3. 通过调试接口获取指定级别的函数
 * 4. 检查是否为尾调用（无法获取环境）
 *
 * 调用栈级别：
 * - 0：当前函数
 * - 1：调用当前函数的函数
 * - n：向上n级的调用函数
 *
 * 错误处理：
 * - 级别必须非负
 * - 级别超出调用栈范围时报错
 * - 尾调用无法获取函数环境时报错
 */
static void getfunc (lua_State *L, int opt) {
    if (lua_isfunction(L, 1)) lua_pushvalue(L, 1);
    else {
        lua_Debug ar;
        int level = opt ? luaL_optint(L, 1, 1) : luaL_checkint(L, 1);
        luaL_argcheck(L, level >= 0, 1, "level must be non-negative");
        if (lua_getstack(L, level, &ar) == 0)
            luaL_argerror(L, 1, "invalid level");
        lua_getinfo(L, "f", &ar);
        if (lua_isnil(L, -1))
            luaL_error(L, "no function environment for tail call at level %d",
                          level);
    }
}

/**
 * @brief 获取函数环境
 *
 * 获取指定函数的环境表。对于C函数，返回全局环境；
 * 对于Lua函数，返回其特定的环境表。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：环境表）
 *
 * 参数说明：
 * - 参数1：函数对象或调用栈级别（可选，默认为1）
 *
 * @note C函数使用全局环境
 * @note Lua函数可以有独立的环境
 *
 * @see getfunc, lua_iscfunction, lua_getfenv
 *
 * 环境获取逻辑：
 * 1. 通过getfunc获取目标函数
 * 2. 检查是否为C函数
 * 3. C函数返回全局环境
 * 4. Lua函数返回其特定环境
 *
 * 环境表作用：
 * - 控制函数的全局变量访问
 * - 实现沙箱和安全执行
 * - 提供函数级别的命名空间
 * - 支持模块化和封装
 *
 * 使用场景：
 * - 检查函数的执行环境
 * - 实现安全的代码执行
 * - 调试和环境分析
 * - 模块系统的实现
 *
 * @example
 * getfenv(func)      -- 获取函数的环境
 * getfenv(1)         -- 获取当前函数的环境
 * getfenv(0)         -- 获取当前线程的环境
 */
static int luaB_getfenv (lua_State *L) {
    getfunc(L, 1);
    if (lua_iscfunction(L, -1))  /* 是C函数？ */
        lua_pushvalue(L, LUA_GLOBALSINDEX);  /* 返回线程的全局环境 */
    else
        lua_getfenv(L, -1);
    return 1;
}

/**
 * @brief 设置函数环境
 *
 * 为指定函数设置新的环境表。只能为Lua函数设置环境，
 * C函数的环境无法修改。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（0或1个：成功时返回函数）
 *
 * 参数说明：
 * - 参数1：函数对象或调用栈级别
 * - 参数2：新的环境表
 *
 * @note 只能为Lua函数设置环境
 * @note 参数1为0时，修改当前线程的环境
 *
 * @see getfunc, lua_setfenv, luaL_checktype
 *
 * 设置逻辑：
 * 1. 检查新环境必须是表
 * 2. 获取目标函数对象
 * 3. 特殊处理线程环境设置
 * 4. 检查是否可以设置环境
 * 5. 应用新的环境设置
 *
 * 特殊情况：
 * - 参数1为0：修改当前线程环境
 * - C函数：无法修改环境
 * - 某些Lua函数：可能禁止环境修改
 *
 * 安全考虑：
 * - 环境修改影响函数行为
 * - 可能破坏沙箱安全性
 * - 需要谨慎使用和权限控制
 *
 * 使用场景：
 * - 实现沙箱执行环境
 * - 创建隔离的执行上下文
 * - 模块系统的环境管理
 * - 安全代码执行框架
 *
 * @example
 * setfenv(func, env)  -- 为函数设置环境
 * setfenv(1, env)     -- 为当前函数设置环境
 * setfenv(0, env)     -- 为当前线程设置环境
 */
static int luaB_setfenv (lua_State *L) {
    luaL_checktype(L, 2, LUA_TTABLE);
    getfunc(L, 0);
    lua_pushvalue(L, 2);
    if (lua_isnumber(L, 1) && lua_tonumber(L, 1) == 0) {
        /* 修改当前线程的环境 */
        lua_pushthread(L);
        lua_insert(L, -2);
        lua_setfenv(L, -2);
        return 0;
    }
    else if (lua_iscfunction(L, -2) || lua_setfenv(L, -2) == 0)
        luaL_error(L,
              LUA_QL("setfenv") " cannot change environment of given object");
    return 1;
}


/**
 * @brief 原始相等比较函数
 *
 * 比较两个值是否相等，不调用任何元方法。这是绕过
 * __eq元方法的原始比较操作。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：布尔值）
 *
 * 参数说明：
 * - 参数1：第一个比较值（任意类型）
 * - 参数2：第二个比较值（任意类型）
 *
 * @note 不会调用__eq元方法
 * @note 使用Lua的原始相等语义
 *
 * @see lua_rawequal, lua_pushboolean
 *
 * 比较规则：
 * 1. 类型不同则不相等
 * 2. 数字按数值比较
 * 3. 字符串按内容比较
 * 4. 其他类型按引用比较
 *
 * 与普通==的区别：
 * - 不调用__eq元方法
 * - 不进行类型转换
 * - 严格按照原始语义比较
 *
 * 使用场景：
 * - 需要绕过元方法的比较
 * - 实现元方法时避免递归
 * - 调试和内部实现
 * - 性能敏感的比较操作
 *
 * @example
 * rawequal(1, 1)        -- 返回: true
 * rawequal("a", "a")    -- 返回: true
 * rawequal({}, {})      -- 返回: false（不同对象）
 * rawequal(obj1, obj2)  -- 不调用__eq元方法
 */
static int luaB_rawequal (lua_State *L) {
    luaL_checkany(L, 1);
    luaL_checkany(L, 2);
    lua_pushboolean(L, lua_rawequal(L, 1, 2));
    return 1;
}

/**
 * @brief 原始表访问函数
 *
 * 从表中获取指定键的值，不调用__index元方法。
 * 这是绕过元方法的原始表访问操作。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：键对应的值或nil）
 *
 * 参数说明：
 * - 参数1：要访问的表
 * - 参数2：要获取的键（任意类型）
 *
 * @note 第一个参数必须是表
 * @note 不会调用__index元方法
 *
 * @see lua_rawget, luaL_checktype
 *
 * 访问逻辑：
 * 1. 检查第一个参数是表
 * 2. 直接从表中获取键值
 * 3. 不调用任何元方法
 * 4. 返回实际存储的值
 *
 * 与普通访问的区别：
 * - 不调用__index元方法
 * - 不进行继承查找
 * - 直接访问表的原始内容
 *
 * 使用场景：
 * - 需要访问表的原始内容
 * - 实现__index元方法时避免递归
 * - 调试和内部实现
 * - 性能敏感的表访问
 *
 * @example
 * rawget(t, "key")      -- 直接获取t["key"]
 * rawget(t, 1)          -- 直接获取t[1]
 * rawget(proxy, "data") -- 绕过代理的__index
 */
static int luaB_rawget (lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checkany(L, 2);
    lua_settop(L, 2);
    lua_rawget(L, 1);
    return 1;
}

/**
 * @brief 原始表设置函数
 *
 * 在表中设置指定键的值，不调用__newindex元方法。
 * 这是绕过元方法的原始表设置操作。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：被设置的表）
 *
 * 参数说明：
 * - 参数1：要设置的表
 * - 参数2：要设置的键（任意类型，不能是nil）
 * - 参数3：要设置的值（任意类型）
 *
 * @note 第一个参数必须是表
 * @note 不会调用__newindex元方法
 *
 * @see lua_rawset, luaL_checktype
 *
 * 设置逻辑：
 * 1. 检查第一个参数是表
 * 2. 检查键和值参数存在
 * 3. 直接在表中设置键值对
 * 4. 不调用任何元方法
 *
 * 与普通设置的区别：
 * - 不调用__newindex元方法
 * - 不进行只读检查
 * - 直接修改表的原始内容
 *
 * 使用场景：
 * - 需要直接修改表内容
 * - 实现__newindex元方法时避免递归
 * - 绕过只读保护机制
 * - 性能敏感的表修改
 *
 * @example
 * rawset(t, "key", value)  -- 直接设置t["key"] = value
 * rawset(t, 1, "first")    -- 直接设置t[1] = "first"
 * rawset(readonly, k, v)   -- 绕过只读保护
 */
static int luaB_rawset (lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checkany(L, 2);
    luaL_checkany(L, 3);
    lua_settop(L, 3);
    lua_rawset(L, 1);
    return 1;
}


/**
 * @brief 垃圾回收信息函数（已废弃）
 *
 * 返回当前内存使用量（以KB为单位）。这是一个向后兼容的函数，
 * 在新代码中应该使用collectgarbage("count")。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：内存使用量整数）
 *
 * @deprecated 建议使用collectgarbage("count")替代
 * @note 返回值是整数，精度较低
 *
 * @see luaB_collectgarbage, lua_getgccount
 *
 * 实现说明：
 * - 直接调用lua_getgccount获取内存计数
 * - 返回整数形式的KB数量
 * - 不包含字节级别的精确信息
 *
 * 使用场景：
 * - 兼容旧版本代码
 * - 简单的内存监控
 * - 快速的内存使用检查
 *
 * @example
 * gcinfo()  -- 返回: 当前内存使用量（KB）
 */
static int luaB_gcinfo (lua_State *L) {
    lua_pushinteger(L, lua_getgccount(L));
    return 1;
}

/**
 * @brief 垃圾回收控制函数
 *
 * 控制垃圾回收器的行为，支持多种操作模式。这是Lua中
 * 管理内存和垃圾回收的主要接口。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：操作结果）
 *
 * 参数说明：
 * - 参数1：操作类型字符串（默认为"collect"）
 * - 参数2：可选的额外参数（默认为0）
 *
 * @note 不同操作返回不同类型的结果
 * @note 某些操作可能影响程序性能
 *
 * @see lua_gc, luaL_checkoption, luaL_optint
 *
 * 支持的操作：
 * - "stop"：停止垃圾回收器
 * - "restart"：重启垃圾回收器
 * - "collect"：执行完整的垃圾回收
 * - "count"：返回内存使用量（KB，包含小数）
 * - "step"：执行一步增量垃圾回收
 * - "setpause"：设置垃圾回收暂停参数
 * - "setstepmul"：设置垃圾回收步长倍数
 *
 * 返回值说明：
 * - "count"：返回精确的内存使用量（KB）
 * - "step"：返回布尔值（是否完成一个回收周期）
 * - 其他：返回操作相关的数值
 *
 * 性能调优：
 * - "setpause"控制回收触发时机
 * - "setstepmul"控制回收执行速度
 * - "step"允许手动控制回收进度
 *
 * 使用场景：
 * - 内存使用监控和分析
 * - 性能敏感应用的内存管理
 * - 实时系统的垃圾回收控制
 * - 内存泄漏检测和调试
 *
 * @example
 * collectgarbage()              -- 执行完整垃圾回收
 * collectgarbage("count")       -- 获取内存使用量
 * collectgarbage("stop")        -- 停止自动垃圾回收
 * collectgarbage("setpause", 200) -- 设置暂停参数
 */
static int luaB_collectgarbage (lua_State *L) {
    static const char *const opts[] = {"stop", "restart", "collect",
        "count", "step", "setpause", "setstepmul", NULL};
    static const int optsnum[] = {LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT,
        LUA_GCCOUNT, LUA_GCSTEP, LUA_GCSETPAUSE, LUA_GCSETSTEPMUL};
    int o = luaL_checkoption(L, 1, "collect", opts);
    int ex = luaL_optint(L, 2, 0);
    int res = lua_gc(L, optsnum[o], ex);
    switch (optsnum[o]) {
        case LUA_GCCOUNT: {
            int b = lua_gc(L, LUA_GCCOUNTB, 0);
            lua_pushnumber(L, res + ((lua_Number)b/1024));
            return 1;
        }
        case LUA_GCSTEP: {
            lua_pushboolean(L, res);
            return 1;
        }
        default: {
            lua_pushnumber(L, res);
            return 1;
        }
    }
}


/**
 * @brief 类型获取函数
 *
 * 返回指定值的类型名称字符串。这是Lua中获取值类型的
 * 标准方法，返回的是人类可读的类型名称。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：类型名称字符串）
 *
 * 参数说明：
 * - 参数1：要检查类型的值（任意类型）
 *
 * @note 返回的是字符串形式的类型名称
 * @note 不同于lua_type返回的数字常量
 *
 * @see luaL_typename, luaL_checkany
 *
 * 支持的类型：
 * - "nil"：空值类型
 * - "boolean"：布尔类型
 * - "number"：数字类型
 * - "string"：字符串类型
 * - "table"：表类型
 * - "function"：函数类型
 * - "userdata"：用户数据类型
 * - "thread"：线程类型
 *
 * 使用场景：
 * - 类型检查和验证
 * - 调试和错误报告
 * - 动态类型处理
 * - 反射和元编程
 *
 * @example
 * type(nil)        -- 返回: "nil"
 * type(true)       -- 返回: "boolean"
 * type(42)         -- 返回: "number"
 * type("hello")    -- 返回: "string"
 * type({})         -- 返回: "table"
 * type(print)      -- 返回: "function"
 */
static int luaB_type (lua_State *L) {
    luaL_checkany(L, 1);
    lua_pushstring(L, luaL_typename(L, 1));
    return 1;
}

/**
 * @brief 表迭代器的下一个函数
 *
 * 获取表中下一个键值对。这是pairs迭代器的核心函数，
 * 用于遍历表中的所有键值对。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（2个：键和值，或1个：nil）
 *
 * 参数说明：
 * - 参数1：要遍历的表
 * - 参数2：当前键（可选，nil表示从头开始）
 *
 * @note 遍历顺序是不确定的
 * @note 遍历结束时返回nil
 *
 * @see lua_next, luaL_checktype
 *
 * 迭代逻辑：
 * 1. 检查第一个参数是表
 * 2. 确保有第二个参数（键）
 * 3. 调用lua_next获取下一个键值对
 * 4. 返回键值对或nil（结束）
 *
 * 遍历特性：
 * - 遍历所有键值对（包括非整数键）
 * - 顺序不保证（哈希表特性）
 * - 在遍历过程中修改表可能导致未定义行为
 *
 * 使用场景：
 * - 实现通用表遍历
 * - pairs函数的底层实现
 * - 自定义迭代器开发
 * - 表内容的完整检查
 *
 * @example
 * next(t)        -- 获取第一个键值对
 * next(t, key)   -- 获取key之后的键值对
 * next(t, lastkey) -- 继续遍历
 */
static int luaB_next (lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);  /* 如果没有第二个参数则创建一个 */
    if (lua_next(L, 1))
        return 2;
    else {
        lua_pushnil(L);
        return 1;
    }
}

/**
 * @brief 通用表迭代器
 *
 * 返回用于遍历表的迭代器函数。这是Lua中遍历表的
 * 标准方法，适用于所有类型的键。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（3个：迭代器函数、状态、初始值）
 *
 * 参数说明：
 * - 参数1：要遍历的表
 *
 * @note 使用next函数作为迭代器
 * @note 遍历顺序不确定
 *
 * @see luaB_next, lua_upvalueindex
 *
 * 返回值说明：
 * - 迭代器函数：next函数
 * - 状态：要遍历的表
 * - 初始值：nil（从头开始）
 *
 * 迭代器协议：
 * - for循环会重复调用迭代器函数
 * - 传入状态和当前值
 * - 返回新的值或nil（结束）
 *
 * 使用场景：
 * - for循环遍历表
 * - 通用表处理算法
 * - 键值对的完整遍历
 * - 表内容的分析和处理
 *
 * @example
 * for k, v in pairs(t) do
 *     print(k, v)  -- 遍历所有键值对
 * end
 */
static int luaB_pairs (lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, lua_upvalueindex(1));  /* 返回生成器函数 */
    lua_pushvalue(L, 1);  /* 状态 */
    lua_pushnil(L);  /* 初始值 */
    return 3;
}

/**
 * @brief 数组迭代器的辅助函数
 *
 * ipairs迭代器的核心实现，按数字索引顺序遍历表。
 * 只遍历连续的整数键，从1开始。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（2个：索引和值，或0个：结束）
 *
 * 参数说明：
 * - 参数1：要遍历的表
 * - 参数2：当前索引（整数）
 *
 * @note 只遍历连续的整数键
 * @note 遇到nil值时停止遍历
 *
 * @see lua_rawgeti, luaL_checkint
 *
 * 遍历逻辑：
 * 1. 获取当前索引并加1
 * 2. 检查表中该索引的值
 * 3. 如果是nil则结束遍历
 * 4. 否则返回索引和值
 *
 * 数组特性：
 * - 从索引1开始遍历
 * - 按顺序遍历连续整数键
 * - 遇到第一个nil值时停止
 * - 不遍历非整数键
 *
 * 使用场景：
 * - 数组风格的表遍历
 * - 有序数据的处理
 * - 列表和序列的迭代
 * - 性能敏感的顺序遍历
 */
static int ipairsaux (lua_State *L) {
    int i = luaL_checkint(L, 2);
    luaL_checktype(L, 1, LUA_TTABLE);
    i++;  /* 下一个值 */
    lua_pushinteger(L, i);
    lua_rawgeti(L, 1, i);
    return (lua_isnil(L, -1)) ? 0 : 2;
}

/**
 * @brief 数组迭代器
 *
 * 返回用于按数字索引遍历表的迭代器。只遍历连续的
 * 整数键，适用于数组风格的表。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（3个：迭代器函数、状态、初始值）
 *
 * 参数说明：
 * - 参数1：要遍历的表
 *
 * @note 使用ipairsaux作为迭代器函数
 * @note 只遍历连续的整数键
 *
 * @see ipairsaux, lua_upvalueindex
 *
 * 返回值说明：
 * - 迭代器函数：ipairsaux函数
 * - 状态：要遍历的表
 * - 初始值：0（从索引1开始）
 *
 * 遍历特点：
 * - 按索引顺序遍历（1, 2, 3, ...）
 * - 只遍历连续的整数键
 * - 遇到nil值时停止
 * - 性能优于pairs（对于数组）
 *
 * 使用场景：
 * - 数组和列表的遍历
 * - 有序数据的处理
 * - 序列的顺序访问
 * - 性能敏感的数组操作
 *
 * @example
 * for i, v in ipairs(arr) do
 *     print(i, v)  -- 按索引顺序遍历
 * end
 */
static int luaB_ipairs (lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, lua_upvalueindex(1));  /* 返回生成器函数 */
    lua_pushvalue(L, 1);  /* 状态 */
    lua_pushinteger(L, 0);  /* 初始值 */
    return 3;
}


/**
 * @brief 代码加载辅助函数
 *
 * 处理代码加载操作的结果，统一返回格式。成功时返回编译后的函数，
 * 失败时返回nil和错误消息。
 *
 * @param L Lua状态机指针
 * @param status 加载操作的状态码（0表示成功）
 * @return 返回值数量（1个：函数，或2个：nil和错误消息）
 *
 * @note 这是所有load函数的共用辅助函数
 * @note 统一了错误处理的返回格式
 *
 * @see luaL_loadbuffer, luaL_loadfile, lua_load
 *
 * 返回格式：
 * - 成功：返回编译后的函数
 * - 失败：返回nil和错误消息
 *
 * 使用场景：
 * - loadstring、loadfile、load函数的内部实现
 * - 统一的错误处理和返回格式
 * - 简化代码加载函数的实现
 */
static int load_aux (lua_State *L, int status) {
    if (status == 0)  /* 成功？ */
        return 1;
    else {
        lua_pushnil(L);
        lua_insert(L, -2);  /* 将nil放在错误消息前面 */
        return 2;  /* 返回nil和错误消息 */
    }
}

/**
 * @brief 从字符串加载代码
 *
 * 将字符串编译为Lua函数。这是动态代码执行的基础，
 * 允许在运行时编译和执行Lua代码。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：函数，或2个：nil和错误消息）
 *
 * 参数说明：
 * - 参数1：要编译的Lua代码字符串
 * - 参数2：可选的代码块名称（用于错误报告）
 *
 * @note 只编译代码，不执行
 * @note 代码块名称用于调试和错误报告
 *
 * @see luaL_loadbuffer, load_aux
 *
 * 编译过程：
 * 1. 检查输入字符串的有效性
 * 2. 获取可选的代码块名称
 * 3. 调用luaL_loadbuffer进行编译
 * 4. 通过load_aux处理结果
 *
 * 错误处理：
 * - 语法错误：返回nil和错误消息
 * - 内存不足：抛出错误
 * - 无效输入：参数检查失败
 *
 * 使用场景：
 * - 动态代码生成和执行
 * - 配置文件的解析和执行
 * - 脚本引擎的代码加载
 * - 模板系统的代码编译
 *
 * @example
 * loadstring("return 1 + 2")()     -- 返回: 3
 * loadstring("print('hello')")()   -- 输出: hello
 * loadstring("invalid syntax")     -- 返回: nil, 错误消息
 */
static int luaB_loadstring (lua_State *L) {
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);
    const char *chunkname = luaL_optstring(L, 2, s);
    return load_aux(L, luaL_loadbuffer(L, s, l, chunkname));
}

/**
 * @brief 从文件加载代码
 *
 * 从文件中读取并编译Lua代码。这是加载外部脚本文件的
 * 标准方法，支持相对路径和绝对路径。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：函数，或2个：nil和错误消息）
 *
 * 参数说明：
 * - 参数1：可选的文件名（nil表示从stdin读取）
 *
 * @note 只编译代码，不执行
 * @note 文件名为nil时从标准输入读取
 *
 * @see luaL_loadfile, load_aux
 *
 * 加载过程：
 * 1. 获取文件名参数（可选）
 * 2. 调用luaL_loadfile读取并编译文件
 * 3. 通过load_aux处理结果
 *
 * 文件处理：
 * - 自动检测文件编码
 * - 支持UTF-8 BOM标记
 * - 处理文件读取错误
 *
 * 错误类型：
 * - 文件不存在：返回nil和错误消息
 * - 权限不足：返回nil和错误消息
 * - 语法错误：返回nil和错误消息
 * - 内存不足：抛出错误
 *
 * 使用场景：
 * - 加载外部脚本文件
 * - 模块系统的实现
 * - 配置文件的加载
 * - 插件系统的代码加载
 *
 * @example
 * loadfile("script.lua")()         -- 加载并执行脚本
 * loadfile()                       -- 从stdin加载代码
 * loadfile("nonexistent.lua")      -- 返回: nil, 错误消息
 */
static int luaB_loadfile (lua_State *L) {
    const char *fname = luaL_optstring(L, 1, NULL);
    return load_aux(L, luaL_loadfile(L, fname));
}

/**
 * @brief 通用load函数的读取器
 *
 * 为通用load函数提供数据读取功能。由于lua_load使用栈进行内部操作，
 * 读取器不能改变栈顶，而是将结果字符串保存在栈的保留位置。
 *
 * @param L Lua状态机指针
 * @param ud 用户数据（未使用）
 * @param size 输出参数：返回字符串的长度
 * @return 读取到的字符串指针，或NULL表示结束
 *
 * @note 这是lua_load的回调函数
 * @note 使用栈位置3作为字符串缓存
 *
 * @see lua_load, luaL_checkstack, lua_tolstring
 *
 * 读取逻辑：
 * 1. 调用栈位置1的读取器函数
 * 2. 检查返回值类型
 * 3. nil表示读取结束
 * 4. 字符串保存到栈位置3
 * 5. 返回字符串指针和长度
 *
 * 栈管理：
 * - 位置1：读取器函数
 * - 位置2：代码块名称
 * - 位置3：字符串缓存（保留位置）
 *
 * 错误处理：
 * - 读取器必须返回字符串或nil
 * - 返回其他类型时抛出错误
 * - 栈溢出时抛出错误
 *
 * 使用场景：
 * - load函数的内部实现
 * - 自定义数据源的代码加载
 * - 流式代码读取和编译
 */
static const char *generic_reader (lua_State *L, void *ud, size_t *size) {
    (void)ud;  /* 避免警告 */
    luaL_checkstack(L, 2, "too many nested functions");
    lua_pushvalue(L, 1);  /* 获取函数 */
    lua_call(L, 0, 1);  /* 调用它 */
    if (lua_isnil(L, -1)) {
        *size = 0;
        return NULL;
    }
    else if (lua_isstring(L, -1)) {
        lua_replace(L, 3);  /* 将字符串保存在保留的栈位置 */
        return lua_tolstring(L, 3, size);
    }
    else luaL_error(L, "reader function must return a string");
    return NULL;  /* 避免警告 */
}


/**
 * @brief 通用代码加载函数
 *
 * 使用读取器函数从任意数据源加载Lua代码。这是最灵活的
 * 代码加载方式，支持自定义的数据源和读取逻辑。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：函数，或2个：nil和错误消息）
 *
 * 参数说明：
 * - 参数1：读取器函数（每次调用返回一段代码字符串）
 * - 参数2：可选的代码块名称（默认为"=(load)"）
 *
 * @note 读取器函数返回nil表示读取结束
 * @note 使用generic_reader作为lua_load的回调
 *
 * @see lua_load, generic_reader, load_aux
 *
 * 加载过程：
 * 1. 检查读取器函数参数
 * 2. 设置栈布局（函数、名称、保留位置）
 * 3. 调用lua_load进行编译
 * 4. 通过load_aux处理结果
 *
 * 读取器协议：
 * - 每次调用返回一段代码字符串
 * - 返回nil表示没有更多数据
 * - 返回空字符串表示当前没有数据但未结束
 *
 * 使用场景：
 * - 从网络流加载代码
 * - 从数据库读取脚本
 * - 分块读取大型脚本文件
 * - 实现自定义的代码源
 *
 * @example
 * function reader()
 *     return file:read("*l")  -- 逐行读取
 * end
 * load(reader, "myfile")()
 */
static int luaB_load (lua_State *L) {
    int status;
    const char *cname = luaL_optstring(L, 2, "=(load)");
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 3);  /* 函数、可能的名称，加上一个保留位置 */
    status = lua_load(L, generic_reader, NULL, cname);
    return load_aux(L, status);
}

/**
 * @brief 执行文件函数
 *
 * 加载并立即执行指定的Lua文件。这是运行外部脚本的
 * 便捷方法，相当于loadfile后立即调用。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（文件执行的返回值数量）
 *
 * 参数说明：
 * - 参数1：可选的文件名（nil表示从stdin读取）
 *
 * @note 加载失败时会抛出错误
 * @note 返回文件执行的所有返回值
 *
 * @see luaL_loadfile, lua_call, lua_error
 *
 * 执行过程：
 * 1. 获取文件名参数
 * 2. 记录当前栈大小
 * 3. 加载文件，失败时抛出错误
 * 4. 执行加载的函数
 * 5. 返回执行结果
 *
 * 错误处理：
 * - 文件加载失败：抛出错误（不返回nil）
 * - 文件执行错误：传播执行时的错误
 * - 与loadfile的区别：失败时抛出错误而非返回nil
 *
 * 使用场景：
 * - 执行配置脚本
 * - 运行初始化文件
 * - 加载和执行插件
 * - 脚本文件的直接执行
 *
 * @example
 * dofile("config.lua")      -- 执行配置文件
 * dofile()                  -- 从stdin执行代码
 * dofile("script.lua")      -- 执行脚本并获取返回值
 */
static int luaB_dofile (lua_State *L) {
    const char *fname = luaL_optstring(L, 1, NULL);
    int n = lua_gettop(L);
    if (luaL_loadfile(L, fname) != 0) lua_error(L);
    lua_call(L, 0, LUA_MULTRET);
    return lua_gettop(L) - n;
}

/**
 * @brief 断言函数
 *
 * 检查条件是否为真，如果为假则抛出错误。这是程序中
 * 进行条件检查和调试的标准方法。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（所有输入参数，如果断言成功）
 *
 * 参数说明：
 * - 参数1：要检查的条件（任意类型）
 * - 参数2：可选的错误消息（默认为"assertion failed!"）
 * - 参数3+：其他参数（断言成功时原样返回）
 *
 * @note 条件为假值（nil或false）时抛出错误
 * @note 断言成功时返回所有输入参数
 *
 * @see lua_toboolean, luaL_error, luaL_optstring
 *
 * 检查逻辑：
 * 1. 检查第一个参数的真假性
 * 2. 如果为假，使用第二个参数作为错误消息
 * 3. 如果为真，返回所有输入参数
 *
 * 真假性判断：
 * - nil和false被认为是假
 * - 其他所有值（包括0和空字符串）都是真
 *
 * 使用场景：
 * - 参数有效性检查
 * - 前置条件验证
 * - 调试和开发阶段的状态检查
 * - 合约式编程的实现
 *
 * @example
 * assert(x > 0, "x must be positive")
 * assert(file, "file not found")
 * local result = assert(func())  -- 确保func()返回非假值
 */
static int luaB_assert (lua_State *L) {
    luaL_checkany(L, 1);
    if (!lua_toboolean(L, 1))
        return luaL_error(L, "%s", luaL_optstring(L, 2, "assertion failed!"));
    return lua_gettop(L);
}

/**
 * @brief 表解包函数
 *
 * 将表中的元素作为多个返回值返回。支持指定起始和结束位置，
 * 用于将数组风格的表转换为多个独立的值。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（解包的元素数量）
 *
 * 参数说明：
 * - 参数1：要解包的表
 * - 参数2：可选的起始索引（默认为1）
 * - 参数3：可选的结束索引（默认为表长度）
 *
 * @note 只解包指定范围内的元素
 * @note 使用rawgeti避免元方法调用
 *
 * @see lua_rawgeti, luaL_getn, lua_checkstack
 *
 * 解包逻辑：
 * 1. 检查表参数的有效性
 * 2. 获取起始和结束索引
 * 3. 检查栈空间是否足够
 * 4. 逐个将表元素压入栈
 * 5. 返回元素数量
 *
 * 索引处理：
 * - 起始索引默认为1
 * - 结束索引默认为表的长度
 * - 支持空范围（起始>结束）
 *
 * 性能考虑：
 * - 使用rawgeti避免元方法开销
 * - 检查栈溢出防止内存问题
 * - 支持大表的高效解包
 *
 * 使用场景：
 * - 函数参数的批量传递
 * - 表数据的展开操作
 * - 数组到多值的转换
 * - 可变参数的处理
 *
 * @example
 * unpack({1, 2, 3})        -- 返回: 1, 2, 3
 * unpack({1, 2, 3}, 2)     -- 返回: 2, 3
 * unpack({1, 2, 3}, 2, 2)  -- 返回: 2
 * print(unpack(args))      -- 打印表中的所有元素
 */
static int luaB_unpack (lua_State *L) {
    int i, e, n;
    luaL_checktype(L, 1, LUA_TTABLE);
    i = luaL_optint(L, 2, 1);
    e = luaL_opt(L, luaL_checkint, 3, luaL_getn(L, 1));
    if (i > e) return 0;  /* 空范围 */
    n = e - i + 1;  /* 元素数量 */
    if (n <= 0 || !lua_checkstack(L, n))  /* n <= 0 表示算术溢出 */
        return luaL_error(L, "too many results to unpack");
    lua_rawgeti(L, 1, i);  /* 压入 arg[i]（避免溢出问题） */
    while (i++ < e)  /* 压入 arg[i + 1...e] */
        lua_rawgeti(L, 1, i);
    return n;
}


/**
 * @brief 参数选择函数
 *
 * 从参数列表中选择指定位置开始的参数，或返回参数总数。
 * 这是处理可变参数的重要工具函数。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：参数数量，或n个：选择的参数）
 *
 * 参数说明：
 * - 参数1：选择器（"#"表示返回参数数量，数字表示起始位置）
 * - 参数2+：要处理的参数列表
 *
 * @note 支持负数索引（从末尾开始计算）
 * @note "#"返回参数数量（不包括选择器本身）
 *
 * @see lua_gettop, luaL_checkint, luaL_argcheck
 *
 * 选择模式：
 * 1. 字符串"#"：返回参数数量
 * 2. 正数索引：从指定位置开始返回所有参数
 * 3. 负数索引：从末尾开始计算位置
 * 4. 超出范围：自动调整到有效范围
 *
 * 索引计算：
 * - 正数：直接使用（1表示第一个参数）
 * - 负数：n + i（从末尾开始计算）
 * - 超出范围：调整到边界值
 *
 * 使用场景：
 * - 可变参数函数的参数处理
 * - 获取参数数量
 * - 参数列表的切片操作
 * - 函数参数的动态处理
 *
 * @example
 * select("#", a, b, c)     -- 返回: 3
 * select(2, a, b, c)       -- 返回: b, c
 * select(-1, a, b, c)      -- 返回: c
 * select(10, a, b, c)      -- 返回: (无参数)
 */
static int luaB_select (lua_State *L) {
    int n = lua_gettop(L);
    if (lua_type(L, 1) == LUA_TSTRING && *lua_tostring(L, 1) == '#') {
        lua_pushinteger(L, n-1);
        return 1;
    }
    else {
        int i = luaL_checkint(L, 1);
        if (i < 0) i = n + i;
        else if (i > n) i = n;
        luaL_argcheck(L, 1 <= i, 1, "index out of range");
        return n - i;
    }
}

/**
 * @brief 保护调用函数
 *
 * 在保护模式下调用函数，捕获所有错误。这是Lua中进行
 * 安全函数调用的标准方法，不会导致程序崩溃。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个状态 + 函数返回值或错误消息）
 *
 * 参数说明：
 * - 参数1：要调用的函数
 * - 参数2+：传递给函数的参数
 *
 * @note 第一个返回值是布尔状态（成功/失败）
 * @note 成功时返回true和函数的所有返回值
 * @note 失败时返回false和错误消息
 *
 * @see lua_pcall, lua_pushboolean, lua_insert
 *
 * 调用过程：
 * 1. 检查函数参数的存在
 * 2. 在保护模式下调用函数
 * 3. 根据调用结果设置状态
 * 4. 返回状态和结果/错误
 *
 * 返回值格式：
 * - 成功：true, result1, result2, ...
 * - 失败：false, error_message
 *
 * 错误处理：
 * - 运行时错误：捕获并返回错误消息
 * - 内存错误：捕获并返回错误消息
 * - 语法错误：不适用（函数已编译）
 *
 * 使用场景：
 * - 不确定的函数调用
 * - 错误恢复和处理
 * - 安全的代码执行
 * - 插件和扩展的调用
 *
 * @example
 * local ok, result = pcall(func, arg1, arg2)
 * if ok then
 *     print("Success:", result)
 * else
 *     print("Error:", result)
 * end
 */
static int luaB_pcall (lua_State *L) {
    int status;
    luaL_checkany(L, 1);
    status = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);
    lua_pushboolean(L, (status == 0));
    lua_insert(L, 1);
    return lua_gettop(L);  /* 返回状态 + 所有结果 */
}

/**
 * @brief 扩展保护调用函数
 *
 * 在保护模式下调用函数，并使用自定义的错误处理函数。
 * 这是pcall的增强版本，支持错误消息的自定义处理。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个状态 + 函数返回值或处理后的错误）
 *
 * 参数说明：
 * - 参数1：要调用的函数
 * - 参数2：错误处理函数
 *
 * @note 错误处理函数接收原始错误消息作为参数
 * @note 可以在错误处理函数中添加调试信息
 *
 * @see lua_pcall, lua_pushboolean, lua_replace
 *
 * 调用过程：
 * 1. 检查函数和错误处理函数
 * 2. 调整栈布局（错误处理函数在底部）
 * 3. 在保护模式下调用函数
 * 4. 错误时调用错误处理函数
 * 5. 返回状态和结果
 *
 * 错误处理：
 * - 错误处理函数可以修改错误消息
 * - 可以添加栈跟踪信息
 * - 可以进行错误分类和处理
 *
 * 与pcall的区别：
 * - 支持自定义错误处理
 * - 可以增强错误信息
 * - 更适合复杂的错误处理需求
 *
 * 使用场景：
 * - 需要详细错误信息的场合
 * - 错误日志和调试
 * - 复杂的错误恢复逻辑
 * - 框架级别的错误处理
 *
 * @example
 * local function error_handler(err)
 *     return debug.traceback(err, 2)
 * end
 * local ok, result = xpcall(func, error_handler)
 */
static int luaB_xpcall (lua_State *L) {
    int status;
    luaL_checkany(L, 2);
    lua_settop(L, 2);
    lua_insert(L, 1);  /* 将错误函数放在要调用的函数下面 */
    status = lua_pcall(L, 0, LUA_MULTRET, 1);
    lua_pushboolean(L, (status == 0));
    lua_replace(L, 1);
    return lua_gettop(L);  /* 返回状态 + 所有结果 */
}


/**
 * @brief 转换为字符串函数
 *
 * 将任意类型的值转换为字符串表示。这是Lua中最重要的
 * 类型转换函数，支持元方法和默认转换规则。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：字符串表示）
 *
 * 参数说明：
 * - 参数1：要转换的值（任意类型）
 *
 * @note 优先使用__tostring元方法
 * @note 不同类型有不同的默认转换规则
 *
 * @see luaL_callmeta, lua_pushstring, lua_pushfstring
 *
 * 转换规则：
 * 1. 首先检查__tostring元方法
 * 2. 数字：转换为数字字符串
 * 3. 字符串：直接返回
 * 4. 布尔：转换为"true"或"false"
 * 5. nil：转换为"nil"
 * 6. 其他：显示类型和地址
 *
 * 元方法支持：
 * - __tostring元方法优先级最高
 * - 允许自定义对象的字符串表示
 * - 支持复杂对象的格式化输出
 *
 * 默认格式：
 * - 数字：标准数字格式
 * - 布尔：英文true/false
 * - nil：字符串"nil"
 * - 对象：类型名 + 内存地址
 *
 * 使用场景：
 * - print函数的内部实现
 * - 字符串连接操作
 * - 调试和日志输出
 * - 用户界面的数据显示
 *
 * @example
 * tostring(123)        -- 返回: "123"
 * tostring(true)       -- 返回: "true"
 * tostring(nil)        -- 返回: "nil"
 * tostring({})         -- 返回: "table: 0x..."
 * tostring(obj)        -- 调用obj的__tostring元方法
 */
static int luaB_tostring (lua_State *L) {
    luaL_checkany(L, 1);
    if (luaL_callmeta(L, 1, "__tostring"))  /* 有元字段吗？ */
        return 1;  /* 使用它的值 */
    switch (lua_type(L, 1)) {
        case LUA_TNUMBER:
            lua_pushstring(L, lua_tostring(L, 1));
            break;
        case LUA_TSTRING:
            lua_pushvalue(L, 1);
            break;
        case LUA_TBOOLEAN:
            lua_pushstring(L, (lua_toboolean(L, 1) ? "true" : "false"));
            break;
        case LUA_TNIL:
            lua_pushliteral(L, "nil");
            break;
        default:
            lua_pushfstring(L, "%s: %p", luaL_typename(L, 1), lua_topointer(L, 1));
            break;
    }
    return 1;
}


/**
 * @brief 新建代理对象函数
 *
 * 创建一个空的用户数据对象，可选择性地设置元表。
 * 这是一个特殊的函数，用于创建轻量级的代理对象。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：新创建的用户数据）
 *
 * 参数说明：
 * - 参数1：可选参数（false/nil：无元表，true：新元表，代理对象：复制元表）
 *
 * @note 使用弱表跟踪有效的元表
 * @note 只能复制已验证的代理对象的元表
 *
 * @see lua_newuserdata, lua_setmetatable, lua_upvalueindex
 *
 * 创建模式：
 * 1. 参数为false/nil：创建无元表的代理
 * 2. 参数为true：创建新元表并设置
 * 3. 参数为代理对象：复制其元表
 *
 * 元表验证：
 * - 使用弱表记录有效的元表
 * - 防止恶意代码创建无效代理
 * - 确保元表的安全性和一致性
 *
 * 安全机制：
 * - 弱表防止元表泄漏
 * - 验证机制防止伪造代理
 * - 类型检查确保参数正确性
 *
 * 使用场景：
 * - 创建轻量级的占位对象
 * - 实现代理模式和装饰器模式
 * - 元编程和反射操作
 * - 特殊用途的用户数据对象
 *
 * @example
 * local proxy1 = newproxy()        -- 无元表代理
 * local proxy2 = newproxy(true)    -- 有新元表的代理
 * local proxy3 = newproxy(proxy2)  -- 复制proxy2的元表
 */
static int luaB_newproxy (lua_State *L) {
    lua_settop(L, 1);
    lua_newuserdata(L, 0);  /* 创建代理 */
    if (lua_toboolean(L, 1) == 0)
        return 1;  /* 无元表 */
    else if (lua_isboolean(L, 1)) {
        lua_newtable(L);  /* 创建新元表 `m' ... */
        lua_pushvalue(L, -1);  /* ... 并将 `m' 标记为有效元表 */
        lua_pushboolean(L, 1);
        lua_rawset(L, lua_upvalueindex(1));  /* weaktable[m] = true */
    }
    else {
        int validproxy = 0;  /* 检查 weaktable[metatable(u)] == true */
        if (lua_getmetatable(L, 1)) {
            lua_rawget(L, lua_upvalueindex(1));
            validproxy = lua_toboolean(L, -1);
            lua_pop(L, 1);  /* 移除值 */
        }
        luaL_argcheck(L, validproxy, 1, "boolean or proxy expected");
        lua_getmetatable(L, 1);  /* 元表有效；获取它 */
    }
    lua_setmetatable(L, 2);
    return 1;
}

/**
 * @brief 基础库函数注册表
 *
 * 定义了所有基础库函数的名称和对应的C函数指针。
 * 这个表用于将C函数注册到Lua的全局环境中。
 *
 * @note 表的最后一个元素必须是{NULL, NULL}
 * @note 函数名称对应Lua中的全局函数名
 *
 * 包含的函数：
 * - assert：断言检查函数
 * - collectgarbage：垃圾回收控制函数
 * - dofile：文件执行函数
 * - error：错误抛出函数
 * - gcinfo：垃圾回收信息函数（已废弃）
 * - getfenv：获取函数环境
 * - getmetatable：获取元表函数
 * - loadfile：文件加载函数
 * - load：通用加载函数
 * - loadstring：字符串加载函数
 * - next：表迭代器函数
 * - pcall：保护调用函数
 * - print：打印函数
 * - rawequal：原始相等比较函数
 * - rawget：原始表访问函数
 * - rawset：原始表设置函数
 * - select：参数选择函数
 * - setfenv：设置函数环境
 * - setmetatable：设置元表函数
 * - tonumber：转换为数字函数
 * - tostring：转换为字符串函数
 * - type：类型获取函数
 * - unpack：表解包函数
 * - xpcall：扩展保护调用函数
 *
 * 注册过程：
 * 1. 通过luaL_register函数批量注册
 * 2. 函数被添加到全局环境中
 * 3. 可以通过函数名直接调用
 */
static const luaL_Reg base_funcs[] = {
    {"assert", luaB_assert},
    {"collectgarbage", luaB_collectgarbage},
    {"dofile", luaB_dofile},
    {"error", luaB_error},
    {"gcinfo", luaB_gcinfo},
    {"getfenv", luaB_getfenv},
    {"getmetatable", luaB_getmetatable},
    {"loadfile", luaB_loadfile},
    {"load", luaB_load},
    {"loadstring", luaB_loadstring},
    {"next", luaB_next},
    {"pcall", luaB_pcall},
    {"print", luaB_print},
    {"rawequal", luaB_rawequal},
    {"rawget", luaB_rawget},
    {"rawset", luaB_rawset},
    {"select", luaB_select},
    {"setfenv", luaB_setfenv},
    {"setmetatable", luaB_setmetatable},
    {"tonumber", luaB_tonumber},
    {"tostring", luaB_tostring},
    {"type", luaB_type},
    {"unpack", luaB_unpack},
    {"xpcall", luaB_xpcall},
    {NULL, NULL}
};

/**
 * @defgroup CoroutineLibrary 协程库
 * @brief Lua协程（coroutine）库的完整实现
 *
 * 协程是Lua中实现协作式多任务的机制，允许函数在执行过程中
 * 暂停和恢复，实现非抢占式的并发编程模式。
 *
 * 协程状态：
 * - running：正在运行
 * - suspended：已暂停（可以恢复）
 * - normal：正常状态（恢复了另一个协程）
 * - dead：已结束（正常结束或出错）
 *
 * 核心概念：
 * - 协程是独立的执行线程，有自己的栈
 * - 通过yield暂停执行，通过resume恢复执行
 * - 支持值的传递和返回
 * - 错误处理和状态管理
 *
 * 使用场景：
 * - 生成器和迭代器实现
 * - 异步编程和事件处理
 * - 状态机和流程控制
 * - 协作式任务调度
 * @{
 */

/** @brief 协程状态常量：运行中 */
#define CO_RUN	0	/* running */
/** @brief 协程状态常量：已暂停 */
#define CO_SUS	1	/* suspended */
/** @brief 协程状态常量：正常（恢复了其他协程） */
#define CO_NOR	2	/* 'normal' (it resumed another coroutine) */
/** @brief 协程状态常量：已结束 */
#define CO_DEAD	3

/**
 * @brief 协程状态名称数组
 *
 * 将协程状态常量映射为可读的字符串名称，
 * 用于状态查询和调试输出。
 */
static const char *const statnames[] =
    {"running", "suspended", "normal", "dead"};

/**
 * @brief 获取协程状态
 *
 * 检查指定协程的当前状态，返回对应的状态常量。
 * 这是协程库内部使用的核心状态检查函数。
 *
 * @param L 主Lua状态机指针
 * @param co 要检查的协程状态机指针
 * @return 协程状态常量（CO_RUN/CO_SUS/CO_NOR/CO_DEAD）
 *
 * @note 这是内部函数，不直接暴露给Lua代码
 * @note 状态判断基于协程的内部状态和栈信息
 *
 * @see lua_status, lua_getstack, lua_gettop
 *
 * 状态判断逻辑：
 * 1. 如果是同一个状态机，返回运行状态
 * 2. 检查协程的内部状态码
 * 3. LUA_YIELD表示已暂停
 * 4. 状态0需要进一步检查栈帧
 * 5. 其他状态表示已结束
 *
 * 详细状态分析：
 * - 有栈帧且状态0：正常状态（恢复了其他协程）
 * - 无栈帧且栈空：已结束状态
 * - 无栈帧但栈非空：初始暂停状态
 * - 错误状态：已结束状态
 */
static int costatus (lua_State *L, lua_State *co) {
    if (L == co) return CO_RUN;
    switch (lua_status(co)) {
        case LUA_YIELD:
            return CO_SUS;
        case 0: {
            lua_Debug ar;
            if (lua_getstack(co, 0, &ar) > 0)  /* 有栈帧吗？ */
                return CO_NOR;  /* 它正在运行 */
            else if (lua_gettop(co) == 0)
                return CO_DEAD;
            else
                return CO_SUS;  /* 初始状态 */
        }
        default:  /* 发生了某些错误 */
            return CO_DEAD;
    }
}


/**
 * @brief 获取协程状态函数
 *
 * 返回指定协程的当前状态字符串。这是Lua代码中
 * 查询协程状态的标准接口。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：状态字符串）
 *
 * 参数说明：
 * - 参数1：要查询的协程对象
 *
 * @note 参数必须是有效的协程对象
 * @note 返回的状态字符串是人类可读的
 *
 * @see costatus, lua_tothread, statnames
 *
 * 可能的返回值：
 * - "running"：协程正在运行
 * - "suspended"：协程已暂停，可以恢复
 * - "normal"：协程处于正常状态（恢复了其他协程）
 * - "dead"：协程已结束（正常结束或出错）
 *
 * 使用场景：
 * - 协程状态的调试和监控
 * - 条件判断和流程控制
 * - 协程管理和调度
 *
 * @example
 * local co = coroutine.create(function() end)
 * print(coroutine.status(co))  -- 输出: "suspended"
 */
static int luaB_costatus (lua_State *L) {
    lua_State *co = lua_tothread(L, 1);
    luaL_argcheck(L, co, 1, "coroutine expected");
    lua_pushstring(L, statnames[costatus(L, co)]);
    return 1;
}

/**
 * @brief 协程恢复的辅助函数
 *
 * 执行协程恢复操作的核心逻辑，处理参数传递、状态检查、
 * 执行恢复和结果返回。这是resume和wrap函数的共用实现。
 *
 * @param L 主Lua状态机指针
 * @param co 要恢复的协程状态机指针
 * @param narg 传递给协程的参数数量
 * @return 成功时返回结果数量，失败时返回-1
 *
 * @note 这是内部辅助函数，不直接暴露给Lua代码
 * @note 返回-1表示恢复失败，错误消息在栈顶
 *
 * @see costatus, lua_resume, lua_xmove
 *
 * 恢复过程：
 * 1. 检查协程当前状态
 * 2. 验证栈空间是否足够
 * 3. 只能恢复暂停状态的协程
 * 4. 传递参数到协程
 * 5. 执行协程恢复操作
 * 6. 处理恢复结果和错误
 *
 * 状态处理：
 * - 只有暂停状态的协程可以恢复
 * - 运行中、正常状态、已结束的协程不能恢复
 * - 恢复后可能变为运行、暂停或结束状态
 *
 * 参数和结果传递：
 * - 参数从主线程传递到协程
 * - 结果从协程传递回主线程
 * - 使用lua_xmove进行高效的值移动
 *
 * 错误处理：
 * - 状态错误：返回错误标志和消息
 * - 栈溢出：抛出Lua错误
 * - 执行错误：返回错误标志和消息
 */
static int auxresume (lua_State *L, lua_State *co, int narg) {
    int status = costatus(L, co);
    if (!lua_checkstack(co, narg))
        luaL_error(L, "too many arguments to resume");
    if (status != CO_SUS) {
        lua_pushfstring(L, "cannot resume %s coroutine", statnames[status]);
        return -1;  /* 错误标志 */
    }
    lua_xmove(L, co, narg);
    lua_setlevel(L, co);
    status = lua_resume(co, narg);
    if (status == 0 || status == LUA_YIELD) {
        int nres = lua_gettop(co);
        if (!lua_checkstack(L, nres + 1))
            luaL_error(L, "too many results to resume");
        lua_xmove(co, L, nres);  /* 移动yield的值 */
        return nres;
    }
    else {
        lua_xmove(co, L, 1);  /* 移动错误消息 */
        return -1;  /* 错误标志 */
    }
}


/**
 * @brief 协程恢复函数
 *
 * 恢复指定的协程执行，传递参数并返回结果。这是协程库中
 * 最重要的函数，用于控制协程的执行流程。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个状态 + 协程返回值或错误消息）
 *
 * 参数说明：
 * - 参数1：要恢复的协程对象
 * - 参数2+：传递给协程的参数
 *
 * @note 第一个返回值是布尔状态（成功/失败）
 * @note 成功时返回true和协程的返回值
 * @note 失败时返回false和错误消息
 *
 * @see auxresume, lua_tothread, lua_pushboolean
 *
 * 返回值格式：
 * - 成功：true, result1, result2, ...
 * - 失败：false, error_message
 *
 * 使用场景：
 * - 启动新创建的协程
 * - 恢复暂停的协程执行
 * - 向协程传递数据
 * - 从协程接收结果
 *
 * @example
 * local co = coroutine.create(function(x) return x * 2 end)
 * local ok, result = coroutine.resume(co, 5)
 * if ok then print(result) else print("Error:", result) end
 */
static int luaB_coresume (lua_State *L) {
    lua_State *co = lua_tothread(L, 1);
    int r;
    luaL_argcheck(L, co, 1, "coroutine expected");
    r = auxresume(L, co, lua_gettop(L) - 1);
    if (r < 0) {
        lua_pushboolean(L, 0);
        lua_insert(L, -2);
        return 2;  /* 返回 false + 错误消息 */
    }
    else {
        lua_pushboolean(L, 1);
        lua_insert(L, -(r + 1));
        return r + 1;  /* 返回 true + `resume' 的返回值 */
    }
}

/**
 * @brief 协程包装器的辅助函数
 *
 * 为wrap函数创建的包装器提供执行逻辑。与resume不同，
 * 这个函数在出错时直接抛出错误而不是返回错误状态。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（协程的返回值）
 *
 * @note 这是内部函数，作为闭包的实现
 * @note 协程对象存储在upvalue中
 * @note 出错时直接抛出错误，不返回状态
 *
 * @see auxresume, lua_upvalueindex, lua_error
 *
 * 与resume的区别：
 * - 不返回状态布尔值
 * - 出错时抛出错误而非返回false
 * - 使用更简洁，但错误处理较少
 *
 * 错误处理：
 * - 如果错误对象是字符串，添加位置信息
 * - 直接传播错误，不进行状态包装
 * - 适合不需要复杂错误处理的场景
 */
static int luaB_auxwrap (lua_State *L) {
    lua_State *co = lua_tothread(L, lua_upvalueindex(1));
    int r = auxresume(L, co, lua_gettop(L));
    if (r < 0) {
        if (lua_isstring(L, -1)) {  /* 错误对象是字符串吗？ */
            luaL_where(L, 1);  /* 添加额外信息 */
            lua_insert(L, -2);
            lua_concat(L, 2);
        }
        lua_error(L);  /* 传播错误 */
    }
    return r;
}

/**
 * @brief 创建协程函数
 *
 * 创建一个新的协程，以指定的Lua函数作为协程的主体。
 * 新创建的协程处于暂停状态，需要通过resume启动。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：新创建的协程对象）
 *
 * 参数说明：
 * - 参数1：协程的主体函数（必须是Lua函数）
 *
 * @note 只接受Lua函数，不接受C函数
 * @note 新协程初始状态为暂停
 *
 * @see lua_newthread, lua_xmove, lua_iscfunction
 *
 * 创建过程：
 * 1. 创建新的Lua线程（协程）
 * 2. 验证参数是Lua函数
 * 3. 将函数移动到新协程中
 * 4. 返回协程对象
 *
 * 使用场景：
 * - 创建生成器函数
 * - 实现异步操作
 * - 构建状态机
 * - 协作式任务处理
 *
 * @example
 * local co = coroutine.create(function()
 *     for i = 1, 3 do
 *         coroutine.yield(i)
 *     end
 * end)
 */
static int luaB_cocreate (lua_State *L) {
    lua_State *NL = lua_newthread(L);
    luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1), 1,
        "Lua function expected");
    lua_pushvalue(L, 1);  /* 将函数移到栈顶 */
    lua_xmove(L, NL, 1);  /* 将函数从L移到NL */
    return 1;
}

/**
 * @brief 协程包装函数
 *
 * 创建协程并返回一个包装器函数。包装器函数调用时会自动
 * 恢复协程，提供比resume更简洁的使用方式。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：包装器函数）
 *
 * 参数说明：
 * - 参数1：协程的主体函数
 *
 * @note 返回的函数出错时会抛出错误
 * @note 比resume使用更简洁，但错误处理较少
 *
 * @see luaB_cocreate, luaB_auxwrap, lua_pushcclosure
 *
 * 包装器特点：
 * - 自动处理协程恢复
 * - 不返回状态布尔值
 * - 出错时直接抛出错误
 * - 适合简单的协程使用场景
 *
 * 使用场景：
 * - 简化的协程调用
 * - 生成器函数的实现
 * - 不需要复杂错误处理的场合
 *
 * @example
 * local gen = coroutine.wrap(function()
 *     for i = 1, 3 do
 *         coroutine.yield(i)
 *     end
 * end)
 * print(gen())  -- 输出: 1
 * print(gen())  -- 输出: 2
 */
static int luaB_cowrap (lua_State *L) {
    luaB_cocreate(L);
    lua_pushcclosure(L, luaB_auxwrap, 1);
    return 1;
}

/**
 * @brief 协程让出函数
 *
 * 暂停当前协程的执行，将控制权返回给调用者。
 * 可以传递值给调用者，这些值会作为resume的返回值。
 *
 * @param L Lua状态机指针
 * @return 不返回（函数会暂停协程）
 *
 * 参数说明：
 * - 参数1+：要传递给调用者的值
 *
 * @note 只能在协程内部调用
 * @note 传递的值会成为resume的返回值
 *
 * @see lua_yield, lua_gettop
 *
 * 让出过程：
 * 1. 收集所有参数作为返回值
 * 2. 暂停当前协程执行
 * 3. 将值传递给resume调用者
 * 4. 等待下次resume恢复执行
 *
 * 使用场景：
 * - 生成器中产生值
 * - 异步操作的暂停点
 * - 协作式任务切换
 * - 状态机的状态转换
 *
 * @example
 * function generator()
 *     for i = 1, 3 do
 *         coroutine.yield(i)  -- 产生值并暂停
 *     end
 * end
 */
static int luaB_yield (lua_State *L) {
    return lua_yield(L, lua_gettop(L));
}

/**
 * @brief 获取当前运行协程函数
 *
 * 返回当前正在运行的协程对象。如果在主线程中调用，
 * 则返回nil，因为主线程不是协程。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（1个：当前协程或nil）
 *
 * @note 主线程不是协程，返回nil
 * @note 在协程中调用返回协程对象本身
 *
 * @see lua_pushthread
 *
 * 返回值：
 * - 在协程中：返回当前协程对象
 * - 在主线程中：返回nil
 *
 * 使用场景：
 * - 检查是否在协程中执行
 * - 获取当前协程的引用
 * - 协程管理和调试
 *
 * @example
 * local co = coroutine.running()
 * if co then
 *     print("在协程中")
 * else
 *     print("在主线程中")
 * end
 */
static int luaB_corunning (lua_State *L) {
    if (lua_pushthread(L))
        lua_pushnil(L);  /* 主线程不是协程 */
    return 1;
}


/**
 * @brief 协程库函数注册表
 *
 * 定义了协程库中所有函数的名称和对应的C函数指针。
 * 这些函数会被注册到coroutine表中。
 *
 * @note 表的最后一个元素必须是{NULL, NULL}
 * @note 函数名称对应coroutine表中的函数名
 *
 * 包含的函数：
 * - create：创建新协程
 * - resume：恢复协程执行
 * - running：获取当前运行的协程
 * - status：获取协程状态
 * - wrap：创建协程包装器
 * - yield：暂停协程执行
 */
static const luaL_Reg co_funcs[] = {
    {"create", luaB_cocreate},
    {"resume", luaB_coresume},
    {"running", luaB_corunning},
    {"status", luaB_costatus},
    {"wrap", luaB_cowrap},
    {"yield", luaB_yield},
    {NULL, NULL}
};

/** @} */ /* 结束协程库文档组 */

/**
 * @brief 辅助函数注册器
 *
 * 将需要upvalue的函数注册到指定表中。某些函数需要辅助函数
 * 作为upvalue，这个函数简化了这种注册过程。
 *
 * @param L Lua状态机指针
 * @param name 函数名称
 * @param f 主函数指针
 * @param u 辅助函数指针（作为upvalue）
 *
 * @note 辅助函数会作为主函数的upvalue
 * @note 用于pairs、ipairs等需要辅助函数的情况
 *
 * @see lua_pushcclosure, lua_setfield
 *
 * 注册过程：
 * 1. 将辅助函数压入栈
 * 2. 创建主函数的闭包（包含辅助函数）
 * 3. 将闭包设置为指定名称的字段
 */
static void auxopen (lua_State *L, const char *name,
                     lua_CFunction f, lua_CFunction u) {
    lua_pushcfunction(L, u);
    lua_pushcclosure(L, f, 1);
    lua_setfield(L, -2, name);
}

/**
 * @brief 基础库初始化函数
 *
 * 初始化Lua的基础库，设置全局变量和函数。这个函数
 * 负责建立Lua的基本运行环境。
 *
 * @param L Lua状态机指针
 *
 * @note 这是内部初始化函数，由luaopen_base调用
 * @note 设置了_G、_VERSION等重要全局变量
 *
 * @see luaL_register, auxopen, lua_setglobal
 *
 * 初始化内容：
 * 1. 设置全局变量_G指向全局表
 * 2. 注册所有基础库函数到全局表
 * 3. 设置_VERSION全局变量
 * 4. 特殊处理需要upvalue的函数
 * 5. 为newproxy创建弱表upvalue
 *
 * 特殊处理：
 * - pairs和ipairs需要辅助函数作为upvalue
 * - newproxy需要弱表来跟踪有效的元表
 * - 这些函数不能通过简单的注册表处理
 *
 * 全局环境设置：
 * - _G：指向全局表本身
 * - _VERSION：Lua版本字符串
 * - 所有基础库函数：print、type、pairs等
 */
static void base_open (lua_State *L) {
    /* 设置全局变量 _G */
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setglobal(L, "_G");
    /* 将库函数注册到全局表 */
    luaL_register(L, "_G", base_funcs);
    lua_pushliteral(L, LUA_VERSION);
    lua_setglobal(L, "_VERSION");  /* 设置全局变量 _VERSION */
    /* `ipairs' 和 `pairs' 需要辅助函数作为upvalue */
    auxopen(L, "ipairs", luaB_ipairs, ipairsaux);
    auxopen(L, "pairs", luaB_pairs, luaB_next);
    /* `newproxy' 需要弱表作为upvalue */
    lua_createtable(L, 0, 1);  /* 新建表 `w' */
    lua_pushvalue(L, -1);  /* `w' 将成为自己的元表 */
    lua_setmetatable(L, -2);
    lua_pushliteral(L, "kv");
    lua_setfield(L, -2, "__mode");  /* metatable(w).__mode = "kv" */
    lua_pushcclosure(L, luaB_newproxy, 1);
    lua_setglobal(L, "newproxy");  /* 设置全局变量 `newproxy' */
}

/**
 * @brief 基础库开放函数
 *
 * Lua基础库的主要入口点，负责初始化基础库和协程库。
 * 这是Lua加载基础库时调用的标准函数。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（2个：基础库表和协程库表）
 *
 * @note 这是标准的Lua库开放函数
 * @note 返回2个表：全局表和协程表
 *
 * @see base_open, luaL_register, LUA_COLIBNAME
 *
 * 初始化过程：
 * 1. 调用base_open初始化基础库
 * 2. 注册协程库函数到coroutine表
 * 3. 返回两个库表
 *
 * 返回值：
 * - 第一个：全局表（包含基础库函数）
 * - 第二个：协程表（包含协程库函数）
 *
 * 使用场景：
 * - Lua虚拟机初始化时自动调用
 * - 嵌入式应用中手动加载基础库
 * - 自定义Lua环境的构建
 *
 * @example
 * // C代码中加载基础库
 * lua_State *L = luaL_newstate();
 * luaopen_base(L);  // 加载基础库
 */
LUALIB_API int luaopen_base (lua_State *L) {
    base_open(L);
    luaL_register(L, LUA_COLIBNAME, co_funcs);
    return 2;
}

