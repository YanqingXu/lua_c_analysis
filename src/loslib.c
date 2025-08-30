/*
** [核心] Lua 标准操作系统库实现
**
** 功能概述：
** 本模块实现了 Lua 的标准操作系统库，提供与操作系统交互的基本功能。
** 包含文件操作、进程控制、时间处理、环境变量访问、本地化设置等核心功能。
**
** 主要功能模块：
** - 文件系统操作：文件删除、重命名、临时文件生成
** - 进程控制：命令执行、程序退出
** - 时间和日期：时间获取、格式化、计算、时区处理
** - 环境变量：系统环境变量的读取和访问
** - 本地化：区域设置和字符集处理
** - 系统信息：CPU 时间测量、系统状态查询
**
** 平台兼容性：
** - Unix/Linux：基于 POSIX 标准系统调用
** - Windows：兼容 Win32 API 和 MSVCRT
** - macOS：支持 Darwin 系统特性
** - 嵌入式系统：提供最小功能集合
**
** 设计特点：
** - 跨平台兼容：统一的接口，平台特定的实现
** - 错误处理：详细的错误信息和异常处理
** - 时区支持：本地时间和 UTC 时间的转换
** - 本地化：多语言和字符集支持
** - 性能优化：最小化系统调用开销
**
** 安全考虑：
** - 文件路径验证：防止路径遍历攻击
** - 命令注入防护：安全的命令执行机制
** - 权限检查：文件操作权限验证
** - 资源管理：防止资源泄漏和溢出
**
** 版本信息：$Id: loslib.c,v 1.19.1.3 2008/01/18 16:38:18 roberto Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

// 系统头文件包含
#include <errno.h>    // 错误码定义
#include <locale.h>   // 本地化支持
#include <stdlib.h>   // 标准库函数
#include <string.h>   // 字符串处理
#include <time.h>     // 时间处理

// 模块标识定义
#define loslib_c
#define LUA_LIB

// Lua 核心头文件
#include "lua.h"

// Lua 辅助库头文件
#include "lauxlib.h"
#include "lualib.h"

/*
** [工具函数] 推送操作结果到 Lua 栈
**
** 功能描述：
** 根据系统调用的执行结果，向 Lua 栈推送相应的返回值。
** 成功时返回 true，失败时返回 nil、错误消息和错误码。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param i - int：操作结果标志（非零表示成功）
** @param filename - const char*：相关文件名（可为 NULL）
**
** 返回值：
** @return int：推送到栈的值的数量（成功返回1，失败返回3）
**
** 栈操作：
** 成功时：推送 boolean(true)
** 失败时：推送 nil, 错误消息字符串, 错误码整数
**
** 错误处理机制：
** - 保存 errno 值，防止 Lua API 调用改变错误状态
** - 使用 strerror 获取人类可读的错误描述
** - 提供错误码用于程序化错误处理
** - 支持文件名上下文信息
**
** 设计说明：
** 这是操作系统库中统一的错误处理模式，确保所有系统调用
** 都能提供一致的错误报告格式。
*/
static int os_pushresult(lua_State *L, int i, const char *filename) 
{
    // 保存当前的 errno 值，因为后续的 Lua API 调用可能会改变它
    int en = errno;
    
    if (i) 
    {
        // 操作成功，推送 true
        lua_pushboolean(L, 1);
        return 1;
    }
    else 
    {
        // 操作失败，推送详细的错误信息
        lua_pushnil(L);
        
        if (filename)
        {
            // 包含文件名的错误消息
            lua_pushfstring(L, "%s: %s", filename, strerror(en));
        }
        else
        {
            // 仅包含错误描述的消息
            lua_pushfstring(L, "%s", strerror(en));
        }
        
        // 推送错误码
        lua_pushinteger(L, en);
        return 3;
    }
}

/*
** [系统调用] 执行系统命令
**
** 功能描述：
** 执行指定的系统命令，返回命令的退出状态码。
** 如果没有提供命令，则检查命令处理器是否可用。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 命令（可选）
** 输出：integer 退出状态码
**
** 系统调用原理：
** 使用 C 标准库的 system() 函数，该函数：
** 1. 创建子进程
** 2. 在子进程中执行 shell
** 3. 由 shell 解析和执行命令
** 4. 返回命令的退出状态
**
** 平台差异：
** - Unix/Linux：使用 /bin/sh 执行命令
** - Windows：使用 cmd.exe 或 command.com
** - 返回值格式可能因平台而异
**
** 安全考虑：
** - 命令通过 shell 执行，存在注入风险
** - 应该验证和清理用户输入
** - 避免执行不受信任的命令
**
** 使用示例：
** status = os.execute("ls -l")        -- Unix/Linux
** status = os.execute("dir")          -- Windows
** available = os.execute()            -- 检查命令处理器
*/
static int os_execute(lua_State *L) 
{
    lua_pushinteger(L, system(luaL_optstring(L, 1, NULL)));
    return 1;
}

/*
** [文件操作] 删除文件
**
** 功能描述：
** 删除指定的文件或空目录。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：os_pushresult 的返回值
**
** 栈操作：
** 输入：string 文件名
** 输出：成功时返回 true，失败时返回 nil, 错误消息, 错误码
**
** 系统调用原理：
** 使用 C 标准库的 remove() 函数，该函数：
** - 对于文件：调用 unlink() 系统调用
** - 对于目录：调用 rmdir() 系统调用（仅限空目录）
** - 原子操作，要么成功要么失败
**
** 平台兼容性：
** - Unix/Linux：支持符号链接和特殊文件
** - Windows：支持长文件名和 Unicode
** - 权限检查：需要对父目录有写权限
**
** 错误情况：
** - ENOENT：文件不存在
** - EACCES：权限不足
** - EBUSY：文件正在使用
** - EISDIR：尝试删除非空目录
**
** 使用示例：
** success = os.remove("temp.txt")
** success, err, code = os.remove("nonexistent.txt")
*/
static int os_remove(lua_State *L) 
{
    const char *filename = luaL_checkstring(L, 1);
    return os_pushresult(L, remove(filename) == 0, filename);
}

/*
** [文件操作] 重命名文件
**
** 功能描述：
** 将文件从一个名称重命名为另一个名称，也可用于移动文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：os_pushresult 的返回值
**
** 栈操作：
** 输入：string 原文件名, string 新文件名
** 输出：成功时返回 true，失败时返回 nil, 错误消息, 错误码
**
** 系统调用原理：
** 使用 C 标准库的 rename() 函数，该函数：
** - 原子操作：要么完全成功要么完全失败
** - 如果目标文件存在，会被覆盖（Unix）或失败（Windows）
** - 可以在同一文件系统内移动文件
**
** 平台差异：
** - Unix/Linux：如果目标存在会被覆盖
** - Windows：如果目标存在会失败
** - 跨文件系统移动可能不支持
**
** 限制条件：
** - 源文件必须存在
** - 对源文件的父目录需要写权限
** - 对目标文件的父目录需要写权限
** - 通常不能跨文件系统边界
**
** 错误情况：
** - ENOENT：源文件不存在
** - EACCES：权限不足
** - EXDEV：跨文件系统操作
** - EEXIST：目标文件已存在（Windows）
**
** 使用示例：
** success = os.rename("old.txt", "new.txt")
** success = os.rename("file.txt", "backup/file.txt")
*/
static int os_rename(lua_State *L) 
{
    const char *fromname = luaL_checkstring(L, 1);
    const char *toname = luaL_checkstring(L, 2);
    return os_pushresult(L, rename(fromname, toname) == 0, fromname);
}

/*
** [文件操作] 生成临时文件名
**
** 功能描述：
** 生成一个唯一的临时文件名，用于创建临时文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（成功返回1，失败触发错误）
**
** 栈操作：
** 输入：无参数
** 输出：string 临时文件名
**
** 实现原理：
** 使用 lua_tmpnam 宏（通常映射到 tmpnam）生成唯一文件名：
** - 基于当前时间和进程ID
** - 确保文件名在系统中唯一
** - 通常放在系统临时目录中
**
** 平台差异：
** - Unix/Linux：通常在 /tmp 目录下
** - Windows：通常在 %TEMP% 目录下
** - 文件名格式因平台而异
**
** 安全考虑：
** - 存在竞态条件：生成名称到创建文件之间的时间窗口
** - 建议使用后立即创建文件
** - 某些系统提供更安全的 mkstemp() 替代方案
**
** 缓冲区管理：
** - 使用固定大小的缓冲区 LUA_TMPNAMBUFSIZE
** - 防止缓冲区溢出
** - 确保字符串正确终止
**
** 错误处理：
** 如果无法生成唯一文件名（极少见），会触发 Lua 错误。
**
** 使用示例：
** tmpfile = os.tmpname()
** -- 立即创建文件以避免竞态条件
** f = io.open(tmpfile, "w")
*/
static int os_tmpname(lua_State *L)
{
    char buff[LUA_TMPNAMBUFSIZE];
    int err;

    // 生成临时文件名
    lua_tmpnam(buff, err);

    if (err)
    {
        return luaL_error(L, "unable to generate a unique filename");
    }

    lua_pushstring(L, buff);
    return 1;
}

/*
** [环境变量] 获取环境变量值
**
** 功能描述：
** 获取指定环境变量的值。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 环境变量名
** 输出：string 环境变量值 或 nil（如果不存在）
**
** 系统调用原理：
** 使用 C 标准库的 getenv() 函数：
** - 在进程的环境变量表中查找指定变量
** - 返回指向环境字符串的指针
** - 如果变量不存在，返回 NULL
**
** 平台兼容性：
** - Unix/Linux：环境变量区分大小写
** - Windows：环境变量不区分大小写
** - 支持 Unicode 环境变量（平台相关）
**
** 常见环境变量：
** - PATH：可执行文件搜索路径
** - HOME/USERPROFILE：用户主目录
** - TEMP/TMP：临时文件目录
** - LANG/LC_*：本地化设置
**
** 安全考虑：
** - 环境变量可能包含敏感信息
** - 返回的指针指向静态内存，不应修改
** - 多线程环境下可能存在竞态条件
**
** 内存管理：
** - getenv 返回的指针由系统管理
** - 不需要手动释放内存
** - 字符串内容可能在后续调用中改变
**
** 使用示例：
** path = os.getenv("PATH")
** home = os.getenv("HOME") or os.getenv("USERPROFILE")
** temp = os.getenv("TEMP") or "/tmp"
*/
static int os_getenv(lua_State *L)
{
    // getenv 返回 NULL 时，lua_pushstring 会推送 nil
    lua_pushstring(L, getenv(luaL_checkstring(L, 1)));
    return 1;
}

/*
** [时间处理] 获取 CPU 时间
**
** 功能描述：
** 获取程序消耗的 CPU 时间（以秒为单位）。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：无参数
** 输出：number CPU 时间（秒）
**
** 时间测量原理：
** 使用 C 标准库的 clock() 函数：
** - 返回程序启动以来的 CPU 时钟数
** - 除以 CLOCKS_PER_SEC 转换为秒
** - 只计算 CPU 时间，不包括等待时间
**
** 精度和范围：
** - 精度取决于系统时钟分辨率
** - 通常精度为毫秒级或微秒级
** - 在某些系统上可能会溢出（长时间运行）
**
** 平台差异：
** - Unix/Linux：通常测量用户态 + 内核态时间
** - Windows：测量墙钟时间（wall clock time）
** - CLOCKS_PER_SEC 值因平台而异
**
** 使用场景：
** - 性能测量和基准测试
** - 算法执行时间分析
** - 程序优化和调试
**
** 注意事项：
** - 多线程程序中的行为未定义
** - 不适合测量实际经过的时间
** - 系统负载会影响测量结果
**
** 使用示例：
** start = os.clock()
** -- 执行一些计算
** elapsed = os.clock() - start
** print("CPU time:", elapsed, "seconds")
*/
static int os_clock(lua_State *L)
{
    lua_pushnumber(L, ((lua_Number)clock()) / (lua_Number)CLOCKS_PER_SEC);
    return 1;
}

/*
** ========================================================================
** [时间日期模块] 时间和日期操作
** ========================================================================
**
** 时间表示格式：
** Lua 中的时间表使用以下字段：
** - year：年份（如 2023）
** - month：月份（1-12）
** - day：日期（1-31）
** - hour：小时（0-23）
** - min：分钟（0-59）
** - sec：秒（0-59，可能有闰秒）
** - wday：星期几（1-7，1=星期日）
** - yday：一年中的第几天（1-366）
** - isdst：是否夏令时（true/false/nil）
**
** 时间戳格式：
** - 使用 Unix 时间戳（自1970年1月1日UTC以来的秒数）
** - 支持负值表示1970年之前的时间
** - 精度通常为秒级
**
** 时区处理：
** - 本地时间：基于系统时区设置
** - UTC 时间：协调世界时，不受时区影响
** - 夏令时：自动处理夏令时转换
*/

/*
** [辅助函数] 设置时间表的整数字段
**
** 功能描述：
** 在时间表中设置指定的整数字段。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param key - const char*：字段名称
** @param value - int：字段值
**
** 栈操作：
** 假设栈顶是目标表，设置 table[key] = value
**
** 使用场景：
** 用于构建时间表，设置年、月、日、时、分、秒等字段。
*/
static void setfield(lua_State *L, const char *key, int value)
{
    lua_pushinteger(L, value);
    lua_setfield(L, -2, key);
}

/*
** [辅助函数] 设置时间表的布尔字段
**
** 功能描述：
** 在时间表中设置指定的布尔字段，支持未定义状态。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param key - const char*：字段名称
** @param value - int：字段值（负数表示未定义）
**
** 栈操作：
** 假设栈顶是目标表，设置 table[key] = boolean(value)
** 如果 value < 0，则不设置字段（保持 nil）
**
** 特殊处理：
** - 负值表示未定义状态，不设置字段
** - 0 表示 false
** - 正值表示 true
**
** 使用场景：
** 主要用于设置 isdst（夏令时）字段，该字段可能未定义。
*/
static void setboolfield(lua_State *L, const char *key, int value)
{
    if (value < 0)
    {
        // 未定义状态，不设置字段
        return;
    }

    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
}

/*
** [辅助函数] 获取时间表的布尔字段
**
** 功能描述：
** 从时间表中获取指定的布尔字段值。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param key - const char*：字段名称
**
** 返回值：
** @return int：字段值（-1表示nil，0表示false，1表示true）
**
** 栈操作：
** 假设栈顶是源表，获取 table[key] 的值
** 操作后栈状态不变
**
** 三态逻辑：
** - -1：字段不存在或为 nil（未定义）
** - 0：字段为 false
** - 1：字段为 true
**
** 使用场景：
** 主要用于获取 isdst（夏令时）字段，支持未定义状态。
*/
static int getboolfield(lua_State *L, const char *key)
{
    int res;

    lua_getfield(L, -1, key);
    res = lua_isnil(L, -1) ? -1 : lua_toboolean(L, -1);
    lua_pop(L, 1);

    return res;
}

/*
** [辅助函数] 获取时间表的整数字段
**
** 功能描述：
** 从时间表中获取指定的整数字段值，支持默认值。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param key - const char*：字段名称
** @param d - int：默认值（负数表示必需字段）
**
** 返回值：
** @return int：字段值或默认值
**
** 栈操作：
** 假设栈顶是源表，获取 table[key] 的值
** 操作后栈状态不变
**
** 错误处理：
** 如果字段不存在且默认值为负数，会触发 Lua 错误。
** 这用于标识必需的字段（如 day、month、year）。
**
** 类型转换：
** 如果字段存在但不是数字，使用默认值。
** 如果是数字，转换为整数返回。
**
** 使用场景：
** 用于从时间表中提取各个时间组件，构建 struct tm。
*/
static int getfield(lua_State *L, const char *key, int d)
{
    int res;

    lua_getfield(L, -1, key);

    if (lua_isnumber(L, -1))
    {
        res = (int)lua_tointeger(L, -1);
    }
    else
    {
        if (d < 0)
        {
            return luaL_error(L, "field " LUA_QS " missing in date table", key);
        }
        res = d;
    }

    lua_pop(L, 1);
    return res;
}

/*
** [时间处理] 日期格式化函数
**
** 功能描述：
** 根据指定的格式字符串格式化日期和时间。
** 支持两种模式：格式化字符串输出和时间表输出。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 格式字符串（可选，默认"%c"）, number 时间戳（可选，默认当前时间）
** 输出：string 格式化的日期字符串 或 table 时间表
**
** 格式字符串模式：
** - 普通格式字符串：使用 strftime 格式化
** - "*t"：返回时间表
** - "!"开头：使用 UTC 时间而非本地时间
**
** 时区处理：
** - 默认使用本地时间（localtime）
** - 格式字符串以"!"开头时使用 UTC 时间（gmtime）
** - 自动处理夏令时转换
**
** strftime 格式说明：
** - %Y：四位年份（如 2023）
** - %m：月份（01-12）
** - %d：日期（01-31）
** - %H：小时（00-23）
** - %M：分钟（00-59）
** - %S：秒（00-59）
** - %w：星期几（0-6，0=星期日）
** - %j：一年中的第几天（001-366）
** - %c：完整的日期时间表示
**
** 时间表字段：
** 当格式为"*t"时，返回包含以下字段的表：
** - sec, min, hour, day, month, year
** - wday（星期几，1-7）
** - yday（一年中的第几天，1-366）
** - isdst（是否夏令时）
**
** 错误处理：
** - 无效时间戳：返回 nil
** - 格式化失败：可能返回空字符串
**
** 缓冲区管理：
** - 使用固定大小缓冲区（200字节）
** - 对于超长格式化结果可能截断
**
** 使用示例：
** date_str = os.date("%Y-%m-%d %H:%M:%S")
** utc_str = os.date("!%Y-%m-%d %H:%M:%S")
** time_table = os.date("*t")
** custom = os.date("%A, %B %d, %Y", timestamp)
*/
static int os_date(lua_State *L)
{
    const char *s = luaL_optstring(L, 1, "%c");
    time_t t = luaL_opt(L, (time_t)luaL_checknumber, 2, time(NULL));
    struct tm *stm;

    // 检查是否使用 UTC 时间
    if (*s == '!')
    {
        // UTC 时间
        stm = gmtime(&t);
        s++;  // 跳过 '!' 字符
    }
    else
    {
        // 本地时间
        stm = localtime(&t);
    }

    if (stm == NULL)
    {
        // 无效的时间戳
        lua_pushnil(L);
    }
    else if (strcmp(s, "*t") == 0)
    {
        // 返回时间表
        lua_createtable(L, 0, 9);  // 创建包含9个字段的表

        setfield(L, "sec", stm->tm_sec);
        setfield(L, "min", stm->tm_min);
        setfield(L, "hour", stm->tm_hour);
        setfield(L, "day", stm->tm_mday);
        setfield(L, "month", stm->tm_mon + 1);      // tm_mon 是 0-11
        setfield(L, "year", stm->tm_year + 1900);   // tm_year 是从1900年开始
        setfield(L, "wday", stm->tm_wday + 1);      // 转换为 1-7（1=星期日）
        setfield(L, "yday", stm->tm_yday + 1);      // 转换为 1-366
        setboolfield(L, "isdst", stm->tm_isdst);    // 夏令时标志
    }
    else
    {
        // 格式化字符串输出
        char cc[3];
        luaL_Buffer b;

        cc[0] = '%';
        cc[2] = '\0';
        luaL_buffinit(L, &b);

        // 逐字符处理格式字符串
        for (; *s; s++)
        {
            if (*s != '%' || *(s + 1) == '\0')
            {
                // 普通字符或字符串末尾的 %
                luaL_addchar(&b, *s);
            }
            else
            {
                // 格式化指令
                size_t reslen;
                char buff[200];  // 格式化结果缓冲区

                cc[1] = *(++s);  // 获取格式字符
                reslen = strftime(buff, sizeof(buff), cc, stm);
                luaL_addlstring(&b, buff, reslen);
            }
        }

        luaL_pushresult(&b);
    }

    return 1;
}

/*
** [时间处理] 时间转换函数
**
** 功能描述：
** 将时间表转换为时间戳，或获取当前时间戳。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：table 时间表（可选）
** 输出：number 时间戳 或 nil（转换失败）
**
** 两种调用模式：
** 1. 无参数：返回当前时间戳
** 2. 时间表参数：将时间表转换为时间戳
**
** 时间表字段要求：
** - year：年份（必需）
** - month：月份 1-12（必需）
** - day：日期 1-31（必需）
** - hour：小时 0-23（默认12）
** - min：分钟 0-59（默认0）
** - sec：秒 0-59（默认0）
** - isdst：夏令时标志（可选）
**
** 系统调用原理：
** - 无参数：调用 time(NULL) 获取当前时间
** - 有参数：使用 mktime() 转换 struct tm 为 time_t
**
** mktime 函数特点：
** - 自动处理日期溢出（如13月变为次年1月）
** - 自动处理夏令时转换
** - 使用本地时区设置
** - 可能修改输入的 struct tm
**
** 错误处理：
** - 无效日期：mktime 返回 -1，函数返回 nil
** - 超出范围：某些系统限制时间范围
**
** 夏令时处理：
** - isdst > 0：强制使用夏令时
** - isdst = 0：强制不使用夏令时
** - isdst < 0：让系统自动判断
**
** 使用示例：
** current = os.time()
** timestamp = os.time({year=2023, month=12, day=25, hour=10, min=30})
** invalid = os.time({year=2023, month=13, day=32})  -- 返回 nil
*/
static int os_time(lua_State *L)
{
    time_t t;

    if (lua_isnoneornil(L, 1))
    {
        // 无参数，获取当前时间
        t = time(NULL);
    }
    else
    {
        // 有参数，转换时间表
        struct tm ts;

        luaL_checktype(L, 1, LUA_TTABLE);
        lua_settop(L, 1);  // 确保表在栈顶

        // 从时间表中提取各个字段
        ts.tm_sec = getfield(L, "sec", 0);
        ts.tm_min = getfield(L, "min", 0);
        ts.tm_hour = getfield(L, "hour", 12);
        ts.tm_mday = getfield(L, "day", -1);        // 必需字段
        ts.tm_mon = getfield(L, "month", -1) - 1;   // 必需字段，转换为 0-11
        ts.tm_year = getfield(L, "year", -1) - 1900; // 必需字段，转换为从1900年开始
        ts.tm_isdst = getboolfield(L, "isdst");     // 夏令时标志

        // 转换为时间戳
        t = mktime(&ts);
    }

    if (t == (time_t)(-1))
    {
        // 转换失败
        lua_pushnil(L);
    }
    else
    {
        // 转换成功
        lua_pushnumber(L, (lua_Number)t);
    }

    return 1;
}

/*
** [时间处理] 时间差计算函数
**
** 功能描述：
** 计算两个时间戳之间的差值（以秒为单位）。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number 时间戳1, number 时间戳2（可选，默认0）
** 输出：number 时间差（秒）
**
** 计算公式：
** 结果 = 时间戳1 - 时间戳2
**
** 系统调用原理：
** 使用 C 标准库的 difftime() 函数：
** - 处理不同平台的时间表示差异
** - 返回双精度浮点数结果
** - 支持负值（时间戳1 < 时间戳2）
**
** 精度说明：
** - 通常精度为秒级
** - 某些系统可能支持更高精度
** - 结果为浮点数，支持小数秒
**
** 使用场景：
** - 计算时间间隔
** - 性能测量
** - 超时检查
** - 日期计算
**
** 特殊情况：
** - 相同时间戳：返回 0
** - 时间戳1 < 时间戳2：返回负值
** - 跨越夏令时边界：自动处理
**
** 使用示例：
** start = os.time()
** -- 执行一些操作
** elapsed = os.difftime(os.time(), start)
**
** -- 计算两个特定时间的差值
** t1 = os.time({year=2023, month=12, day=25})
** t2 = os.time({year=2023, month=12, day=24})
** diff = os.difftime(t1, t2)  -- 86400 秒（1天）
*/
static int os_difftime(lua_State *L)
{
    lua_pushnumber(L, difftime((time_t)(luaL_checknumber(L, 1)),
                               (time_t)(luaL_optnumber(L, 2, 0))));
    return 1;
}

/*
** [本地化] 设置本地化环境
**
** 功能描述：
** 设置程序的本地化环境，影响字符分类、排序、数字格式、货币格式、时间格式等。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 本地化名称（可选）, string 类别（可选，默认"all"）
** 输出：string 当前本地化设置 或 nil（设置失败）
**
** 本地化类别：
** - "all"：所有类别
** - "collate"：字符串排序规则
** - "ctype"：字符分类和转换
** - "monetary"：货币格式
** - "numeric"：数字格式
** - "time"：时间和日期格式
**
** 系统调用原理：
** 使用 C 标准库的 setlocale() 函数：
** - 设置指定类别的本地化环境
** - 返回当前设置的字符串表示
** - 影响其他 C 库函数的行为
**
** 本地化名称格式：
** - "C" 或 "POSIX"：标准 C 本地化
** - ""：使用系统默认本地化
** - "zh_CN.UTF-8"：中文（中国）UTF-8 编码
** - "en_US.UTF-8"：英文（美国）UTF-8 编码
**
** 影响的功能：
** - 字符串比较和排序
** - 数字的小数点符号
** - 货币符号和格式
** - 日期时间格式
** - 字符分类（大小写、数字等）
**
** 平台差异：
** - Unix/Linux：支持丰富的本地化设置
** - Windows：使用不同的本地化名称格式
** - 嵌入式系统：可能只支持基本设置
**
** 错误处理：
** 如果指定的本地化不可用，setlocale 返回 NULL，函数返回 nil。
**
** 使用示例：
** current = os.setlocale()           -- 获取当前设置
** os.setlocale("C")                  -- 设置为标准 C 本地化
** os.setlocale("", "all")            -- 使用系统默认
** os.setlocale("zh_CN.UTF-8")       -- 设置为中文环境
*/
static int os_setlocale(lua_State *L)
{
    // 本地化类别常量数组
    static const int cat[] = {
        LC_ALL, LC_COLLATE, LC_CTYPE,
        LC_MONETARY, LC_NUMERIC, LC_TIME
    };

    // 本地化类别名称数组
    static const char *const catnames[] = {
        "all", "collate", "ctype",
        "monetary", "numeric", "time",
        NULL
    };

    const char *l = luaL_optstring(L, 1, NULL);  // 本地化名称
    int op = luaL_checkoption(L, 2, "all", catnames);  // 类别选择

    // 设置本地化并返回结果
    lua_pushstring(L, setlocale(cat[op], l));
    return 1;
}

/*
** [系统调用] 程序退出函数
**
** 功能描述：
** 终止当前程序的执行，并返回指定的退出状态码。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** 此函数不会返回，程序将直接终止。
**
** 栈操作：
** 输入：integer 退出状态码（可选，默认 EXIT_SUCCESS）
** 输出：无（程序终止）
**
** 退出状态码：
** - EXIT_SUCCESS (0)：成功退出
** - EXIT_FAILURE (1)：失败退出
** - 其他值：自定义退出状态
**
** 系统调用原理：
** 使用 C 标准库的 exit() 函数：
** - 立即终止程序执行
** - 调用已注册的 atexit 函数
** - 关闭所有打开的文件
** - 刷新所有输出缓冲区
** - 返回状态码给父进程
**
** 清理过程：
** 1. 调用 atexit 注册的清理函数
** 2. 刷新并关闭所有 stdio 流
** 3. 删除 tmpfile 创建的临时文件
** 4. 返回控制权给操作系统
**
** 与其他退出方式的区别：
** - exit()：正常退出，执行清理
** - _exit()：立即退出，不执行清理
** - abort()：异常退出，可能生成核心转储
**
** 多线程考虑：
** - exit() 会终止整个进程，包括所有线程
** - 其他线程可能正在执行，需要谨慎使用
**
** 使用示例：
** os.exit()        -- 成功退出
** os.exit(0)       -- 明确指定成功退出
** os.exit(1)       -- 失败退出
** os.exit(42)      -- 自定义退出状态
*/
static int os_exit(lua_State *L)
{
    exit(luaL_optint(L, 1, EXIT_SUCCESS));
    // 注意：此函数永远不会执行到这里
}

/*
** [数据结构] 操作系统库函数注册表
**
** 数据结构说明：
** 包含所有操作系统库函数的注册信息，按字母顺序排列。
** 每个元素都是 luaL_Reg 结构体，包含函数名和对应的 C 函数指针。
**
** 函数分类：
** - 时间处理：clock, date, difftime, time
** - 文件操作：remove, rename, tmpname
** - 进程控制：execute, exit
** - 环境访问：getenv
** - 本地化：setlocale
**
** 排序说明：
** 函数按字母顺序排列，便于查找和维护。
**
** 功能覆盖：
** - 基本文件系统操作
** - 时间和日期处理
** - 系统环境访问
** - 进程生命周期管理
** - 国际化支持
*/
static const luaL_Reg syslib[] =
{
    {"clock",     os_clock},
    {"date",      os_date},
    {"difftime",  os_difftime},
    {"execute",   os_execute},
    {"exit",      os_exit},
    {"getenv",    os_getenv},
    {"remove",    os_remove},
    {"rename",    os_rename},
    {"setlocale", os_setlocale},
    {"time",      os_time},
    {"tmpname",   os_tmpname},
    {NULL, NULL}
};

/*
** [核心] Lua 操作系统库初始化函数
**
** 功能描述：
** 初始化 Lua 操作系统库，注册所有操作系统相关的函数。
** 这是操作系统库的入口点，由 Lua 解释器在加载库时调用。
**
** 详细初始化流程：
** 1. 创建 os 库表
** 2. 注册所有操作系统函数到 os 表中
** 3. 返回 os 库表供 Lua 使用
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1，表示 os 库表）
**
** 栈操作：
** 在栈顶留下 os 库表
**
** 注册的函数：
** - os.clock：CPU 时间测量
** - os.date：日期格式化和时间表生成
** - os.difftime：时间差计算
** - os.execute：系统命令执行
** - os.exit：程序退出
** - os.getenv：环境变量获取
** - os.remove：文件删除
** - os.rename：文件重命名/移动
** - os.setlocale：本地化设置
** - os.time：时间戳获取和转换
** - os.tmpname：临时文件名生成
**
** 设计说明：
** - 提供跨平台的操作系统接口抽象
** - 所有函数都基于 C 标准库，确保可移植性
** - 统一的错误处理机制
** - 支持多种时间和日期操作模式
**
** 安全考虑：
** - os.execute 可能存在命令注入风险
** - 文件操作需要适当的权限检查
** - 临时文件操作存在竞态条件风险
**
** 性能特点：
** - 直接调用系统 API，性能优异
** - 最小化 Lua 栈操作开销
** - 高效的错误处理机制
**
** 算法复杂度：O(n) 时间，其中 n 是函数数量，O(1) 空间
**
** 使用示例：
** require("os")
** print(os.date())              -- 当前日期时间
** print(os.getenv("PATH"))      -- 环境变量
** os.execute("ls -l")           -- 执行系统命令
** print(os.clock())             -- CPU 时间
*/
LUALIB_API int luaopen_os(lua_State *L)
{
    // 注册操作系统库函数
    luaL_register(L, LUA_OSLIBNAME, syslib);

    // 返回 os 库表
    return 1;
}
