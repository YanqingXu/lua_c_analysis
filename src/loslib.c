/**
 * @file loslib.c
 * @brief Lua操作系统库：系统调用和平台服务的完整封装
 *
 * 版权信息：
 * $Id: loslib.c,v 1.19.1.3 2008/01/18 16:38:18 roberto Exp $
 * 标准操作系统库
 * 版权声明见lua.h文件
 *
 * 程序概述：
 * 本文件实现了Lua的操作系统接口库，提供了对底层系统服务的
 * 高级封装。它是Lua与操作系统交互的主要桥梁，支持文件操作、
 * 进程控制、时间处理、环境变量管理等核心系统功能。
 *
 * 主要功能：
 * 1. 文件系统操作（删除、重命名、临时文件）
 * 2. 进程控制（程序执行、退出处理）
 * 3. 时间日期处理（获取、格式化、计算）
 * 4. 环境变量操作（读取系统环境）
 * 5. 本地化支持（区域设置管理）
 * 6. 系统时钟和性能计时
 * 7. 跨平台兼容性处理
 *
 * 设计特点：
 * 1. 跨平台兼容：统一的API隐藏平台差异
 * 2. 错误处理：完善的错误报告和异常管理
 * 3. 类型安全：严格的参数检查和类型转换
 * 4. 性能优化：高效的系统调用封装
 * 5. 标准遵循：遵循C标准库和POSIX规范
 *
 * 系统编程技术：
 * - C标准库函数的安全封装
 * - 系统调用的错误处理模式
 * - 跨平台的时间日期处理
 * - 环境变量和本地化支持
 * - 文件系统操作的抽象
 *
 * 应用场景：
 * - 系统管理脚本和工具
 * - 文件处理和批量操作
 * - 日志记录和时间戳生成
 * - 跨平台应用程序开发
 * - 系统监控和性能分析
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2008
 *
 * @note 这是Lua标准库的系统接口实现
 * @see lua.h, lauxlib.h, lualib.h
 */

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define loslib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/**
 * @defgroup SystemUtilities 系统工具函数
 * @brief 系统操作的基础工具和错误处理机制
 *
 * 系统工具函数提供了统一的错误处理、结果返回和
 * 状态管理功能，是所有系统操作函数的基础。
 * @{
 */

/**
 * @brief 推送系统操作结果到Lua栈
 *
 * 统一处理系统操作的返回值，提供一致的错误报告格式。
 * 成功时返回true，失败时返回nil、错误消息和错误码。
 *
 * @param L Lua状态机指针
 * @param i 操作结果（非0表示成功，0表示失败）
 * @param filename 相关的文件名（用于错误消息）
 * @return 返回值数量（成功时1个，失败时3个）
 *
 * @note 保存errno值避免被Lua API调用修改
 * @note 提供详细的错误信息和系统错误码
 *
 * @see strerror, lua_pushboolean, lua_pushfstring
 *
 * 返回值格式：
 * - 成功：true
 * - 失败：nil, "filename: error message", error_code
 *
 * 错误处理流程：
 * 1. 保存当前errno值
 * 2. 检查操作结果
 * 3. 成功时推送true并返回1
 * 4. 失败时推送nil、错误消息、错误码并返回3
 *
 * 使用场景：
 * - 文件操作结果处理
 * - 系统调用状态报告
 * - 统一的错误信息格式
 * - 跨平台的错误处理
 *
 * 错误消息示例：
 * ```
 * "test.txt: No such file or directory"
 * "output.dat: Permission denied"
 * ```
 */
static int os_pushresult (lua_State *L, int i, const char *filename) {
    int en = errno;  /* Lua API调用可能改变这个值 */
    if (i) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else {
        lua_pushnil(L);
        lua_pushfstring(L, "%s: %s", filename, strerror(en));
        lua_pushinteger(L, en);
        return 3;
    }
}

/** @} */ /* 结束系统工具函数文档组 */

/**
 * @defgroup ProcessControl 进程控制系统
 * @brief 进程执行、控制和管理功能
 *
 * 进程控制系统提供了执行外部程序、获取执行结果
 * 和管理进程生命周期的功能。
 * @{
 */

/**
 * @brief 执行系统命令
 *
 * 执行指定的系统命令并返回其退出状态码。
 * 这是os.execute函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（退出状态码）
 *
 * @note 参数1：要执行的命令字符串（可选，默认为NULL）
 * @note 返回系统命令的退出状态码
 *
 * @see system, luaL_optstring
 *
 * 执行机制：
 * - 使用C标准库的system函数
 * - 支持shell命令和可执行程序
 * - 返回程序的退出状态码
 * - NULL参数检查shell可用性
 *
 * 参数处理：
 * - 参数存在：执行指定命令
 * - 参数为NULL：检查命令处理器可用性
 * - 可选参数：默认为NULL
 *
 * 返回值含义：
 * - 0：命令成功执行
 * - 非0：命令执行失败或退出码
 * - 具体含义依赖于系统和命令
 *
 * 使用示例：
 * ```lua
 * local status = os.execute("ls -l")
 * local available = os.execute()  -- 检查shell可用性
 * ```
 *
 * 安全考虑：
 * - 命令注入风险
 * - 权限和安全策略
 * - 跨平台命令差异
 * - 错误处理和异常情况
 */
static int os_execute (lua_State *L) {
    lua_pushinteger(L, system(luaL_optstring(L, 1, NULL)));
    return 1;
}

/** @} */ /* 结束进程控制系统文档组 */

/**
 * @defgroup FileSystemOperations 文件系统操作
 * @brief 文件和目录的基本操作功能
 *
 * 文件系统操作提供了文件删除、重命名、临时文件生成
 * 等基础文件管理功能。
 * @{
 */

/**
 * @brief 删除文件
 *
 * 删除指定的文件。这是os.remove函数的实现。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（成功时1个，失败时3个）
 *
 * @note 参数1：要删除的文件名
 * @note 使用os_pushresult统一处理结果
 *
 * @see remove, os_pushresult, luaL_checkstring
 *
 * 删除机制：
 * - 使用C标准库的remove函数
 * - 支持文件和空目录删除
 * - 跨平台的统一接口
 * - 详细的错误报告
 *
 * 错误情况：
 * - 文件不存在
 * - 权限不足
 * - 文件正在使用
 * - 目录非空
 *
 * 返回值：
 * - 成功：true
 * - 失败：nil, error_message, error_code
 *
 * 使用示例：
 * ```lua
 * local success, err, code = os.remove("temp.txt")
 * if not success then
 *     print("删除失败:", err, code)
 * end
 * ```
 */
static int os_remove (lua_State *L) {
    const char *filename = luaL_checkstring(L, 1);
    return os_pushresult(L, remove(filename) == 0, filename);
}

/**
 * @brief 重命名文件
 *
 * 将文件从一个名称重命名为另一个名称。
 * 这是os.rename函数的实现。
 *
 * @param L Lua状态机指针
 * @return 返回值数量（成功时1个，失败时3个）
 *
 * @note 参数1：原文件名
 * @note 参数2：新文件名
 *
 * @see rename, os_pushresult, luaL_checkstring
 *
 * 重命名机制：
 * - 使用C标准库的rename函数
 * - 支持文件和目录重命名
 * - 可用于移动文件（同一文件系统内）
 * - 原子操作（在支持的系统上）
 *
 * 操作限制：
 * - 目标文件存在时可能覆盖
 * - 跨文件系统移动可能失败
 * - 权限检查和访问控制
 * - 平台特定的行为差异
 *
 * 错误情况：
 * - 源文件不存在
 * - 目标路径无效
 * - 权限不足
 * - 跨设备操作
 *
 * 使用示例：
 * ```lua
 * local success = os.rename("old.txt", "new.txt")
 * local moved = os.rename("file.txt", "backup/file.txt")
 * ```
 */
static int os_rename (lua_State *L) {
    const char *fromname = luaL_checkstring(L, 1);
    const char *toname = luaL_checkstring(L, 2);
    return os_pushresult(L, rename(fromname, toname) == 0, fromname);
}

/**
 * @brief 生成临时文件名
 *
 * 生成一个唯一的临时文件名。这是os.tmpname函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（临时文件名字符串）
 *
 * @note 不创建实际文件，仅生成文件名
 * @note 使用系统特定的临时目录
 *
 * @see lua_tmpnam, luaL_error, lua_pushstring
 *
 * 生成机制：
 * - 使用lua_tmpnam宏生成唯一名称
 * - 基于系统时间和进程ID
 * - 遵循系统临时文件约定
 * - 跨平台的兼容性处理
 *
 * 安全考虑：
 * - 文件名唯一性保证
 * - 临时目录的权限
 * - 竞态条件的避免
 * - 清理和生命周期管理
 *
 * 错误处理：
 * - 生成失败时抛出Lua错误
 * - 提供明确的错误消息
 * - 不返回无效的文件名
 *
 * 使用示例：
 * ```lua
 * local tmpfile = os.tmpname()
 * local f = io.open(tmpfile, "w")
 * -- 使用临时文件
 * f:close()
 * os.remove(tmpfile)  -- 清理
 * ```
 *
 * 注意事项：
 * - 需要手动删除临时文件
 * - 文件名不保证在所有系统上相同格式
 * - 可能的安全漏洞（符号链接攻击等）
 */
static int os_tmpname (lua_State *L) {
    char buff[LUA_TMPNAMBUFSIZE];
    int err;
    lua_tmpnam(buff, err);
    if (err)
        return luaL_error(L, "unable to generate a unique filename");
    lua_pushstring(L, buff);
    return 1;
}

/** @} */ /* 结束文件系统操作文档组 */

/**
 * @defgroup EnvironmentAccess 环境变量访问
 * @brief 系统环境变量的读取和管理
 *
 * 环境变量访问提供了读取系统环境变量的功能，
 * 支持配置管理和系统信息获取。
 * @{
 */

/**
 * @brief 获取环境变量
 *
 * 获取指定环境变量的值。这是os.getenv函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（环境变量值或nil）
 *
 * @note 参数1：环境变量名
 * @note 变量不存在时返回nil
 *
 * @see getenv, luaL_checkstring, lua_pushstring
 *
 * 获取机制：
 * - 使用C标准库的getenv函数
 * - 返回环境变量的字符串值
 * - 不存在时自动推送nil
 * - 跨平台的统一接口
 *
 * 环境变量特性：
 * - 进程级别的配置信息
 * - 系统和用户设置
 * - 路径和配置参数
 * - 本地化和区域信息
 *
 * 常用环境变量：
 * - PATH：可执行文件搜索路径
 * - HOME：用户主目录
 * - TEMP/TMP：临时目录
 * - LANG：语言设置
 *
 * 使用示例：
 * ```lua
 * local path = os.getenv("PATH")
 * local home = os.getenv("HOME") or os.getenv("USERPROFILE")
 * local temp = os.getenv("TEMP") or "/tmp"
 * ```
 *
 * 安全考虑：
 * - 环境变量可能包含敏感信息
 * - 注入攻击的风险
 * - 跨平台的变量名差异
 * - 编码和字符集问题
 */
static int os_getenv (lua_State *L) {
    lua_pushstring(L, getenv(luaL_checkstring(L, 1)));  /* 如果NULL则推送nil */
    return 1;
}

/** @} */ /* 结束环境变量访问文档组 */

/**
 * @defgroup TimeAndClock 时间和时钟系统
 * @brief 时间获取、计算和性能测量功能
 *
 * 时间和时钟系统提供了CPU时间测量、性能分析
 * 和程序计时的基础功能。
 * @{
 */

/**
 * @brief 获取CPU时间
 *
 * 获取程序使用的CPU时间（秒）。这是os.clock函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（CPU时间数值）
 *
 * @note 返回程序开始执行以来的CPU时间
 * @note 用于性能测量和程序计时
 *
 * @see clock, CLOCKS_PER_SEC, lua_pushnumber
 *
 * 时间计算：
 * - 使用C标准库的clock函数
 * - 转换为秒为单位的浮点数
 * - 基于CLOCKS_PER_SEC常量
 * - 高精度的时间测量
 *
 * 测量特性：
 * - CPU时间而非墙钟时间
 * - 不包括等待和阻塞时间
 * - 适用于性能分析
 * - 跨平台的一致性
 *
 * 精度和限制：
 * - 精度依赖于系统实现
 * - 可能的溢出问题（长时间运行）
 * - 多线程环境的考虑
 * - 系统负载的影响
 *
 * 使用示例：
 * ```lua
 * local start = os.clock()
 * -- 执行一些操作
 * local elapsed = os.clock() - start
 * print("CPU时间:", elapsed, "秒")
 * ```
 *
 * 应用场景：
 * - 算法性能测试
 * - 代码优化分析
 * - 基准测试工具
 * - 程序性能监控
 */
static int os_clock (lua_State *L) {
    lua_pushnumber(L, ((lua_Number)clock())/(lua_Number)CLOCKS_PER_SEC);
    return 1;
}

/** @} */ /* 结束时间和时钟系统文档组 */


/**
 * @defgroup DateTimeOperations 时间日期操作系统
 * @brief 时间日期的处理、格式化和计算功能
 *
 * 时间日期操作系统提供了完整的时间处理功能，包括
 * 时间获取、格式化、解析和计算等核心操作。
 *
 * 日期表格式：
 * - year: 年份（如2023）
 * - month: 月份（1-12）
 * - day: 日期（1-31）
 * - hour: 小时（0-23）
 * - min: 分钟（0-59）
 * - sec: 秒（0-59）
 * - wday: 星期几（1-7，1为星期日）
 * - yday: 一年中的第几天（1-366）
 * - isdst: 是否夏令时（true/false/nil）
 * @{
 */

/**
 * @brief 设置日期表的整数字段
 *
 * 在日期表中设置指定键的整数值。这是构建日期表的
 * 基础工具函数。
 *
 * @param L Lua状态机指针
 * @param key 字段名
 * @param value 整数值
 *
 * @note 栈顶必须是目标表
 * @note 操作后栈状态不变
 *
 * @see lua_pushinteger, lua_setfield
 *
 * 操作流程：
 * 1. 将整数值推入栈
 * 2. 设置表的指定字段
 * 3. 自动弹出值，保持栈平衡
 *
 * 使用场景：
 * - 构建os.date("*t")返回的表
 * - 设置年、月、日等数值字段
 * - 时间表的标准化构建
 */
static void setfield (lua_State *L, const char *key, int value) {
    lua_pushinteger(L, value);
    lua_setfield(L, -2, key);
}

/**
 * @brief 设置日期表的布尔字段
 *
 * 在日期表中设置指定键的布尔值。支持未定义状态的处理。
 *
 * @param L Lua状态机指针
 * @param key 字段名
 * @param value 布尔值（负数表示未定义）
 *
 * @note 负值时不设置字段（保持nil）
 * @note 用于处理可选的布尔属性
 *
 * @see lua_pushboolean, lua_setfield
 *
 * 值处理：
 * - value < 0：不设置字段（未定义状态）
 * - value == 0：设置为false
 * - value > 0：设置为true
 *
 * 使用场景：
 * - 设置isdst（夏令时）字段
 * - 处理可选的布尔属性
 * - 支持三态逻辑（true/false/nil）
 */
static void setboolfield (lua_State *L, const char *key, int value) {
    if (value < 0)  /* 未定义？ */
        return;  /* 不设置字段 */
    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
}

/**
 * @brief 获取日期表的布尔字段
 *
 * 从日期表中获取指定键的布尔值，支持未定义状态。
 *
 * @param L Lua状态机指针
 * @param key 字段名
 * @return 布尔值（-1表示nil/未定义，0表示false，1表示true）
 *
 * @note 栈顶必须是源表
 * @note 操作后栈状态不变
 *
 * @see lua_getfield, lua_isnil, lua_toboolean
 *
 * 返回值含义：
 * - -1：字段为nil或不存在
 * - 0：字段为false
 * - 1：字段为true
 *
 * 操作流程：
 * 1. 获取表的指定字段
 * 2. 检查是否为nil
 * 3. 转换为布尔值或返回-1
 * 4. 弹出字段值，保持栈平衡
 *
 * 使用场景：
 * - 解析os.time参数表
 * - 获取isdst字段状态
 * - 处理可选布尔参数
 */
static int getboolfield (lua_State *L, const char *key) {
    int res;
    lua_getfield(L, -1, key);
    res = lua_isnil(L, -1) ? -1 : lua_toboolean(L, -1);
    lua_pop(L, 1);
    return res;
}

/**
 * @brief 获取日期表的整数字段
 *
 * 从日期表中获取指定键的整数值，支持默认值和必需字段检查。
 *
 * @param L Lua状态机指针
 * @param key 字段名
 * @param d 默认值（负数表示必需字段）
 * @return 字段的整数值
 *
 * @note 栈顶必须是源表
 * @note 必需字段缺失时抛出错误
 *
 * @see lua_getfield, lua_isnumber, lua_tointeger, luaL_error
 *
 * 参数处理：
 * - 字段存在且为数字：返回整数值
 * - 字段不存在且d >= 0：返回默认值d
 * - 字段不存在且d < 0：抛出错误
 *
 * 错误处理：
 * - 必需字段缺失时提供清晰的错误消息
 * - 包含字段名的详细错误信息
 * - 帮助用户诊断日期表问题
 *
 * 操作流程：
 * 1. 获取表的指定字段
 * 2. 检查是否为数字类型
 * 3. 转换为整数或使用默认值
 * 4. 弹出字段值，保持栈平衡
 * 5. 返回最终结果
 *
 * 使用场景：
 * - 解析os.time参数表
 * - 获取年、月、日等数值字段
 * - 支持可选和必需字段
 * - 提供合理的默认值
 */
static int getfield (lua_State *L, const char *key, int d) {
    int res;
    lua_getfield(L, -1, key);
    if (lua_isnumber(L, -1))
        res = (int)lua_tointeger(L, -1);
    else {
        if (d < 0)
            return luaL_error(L, "field " LUA_QS " missing in date table", key);
        res = d;
    }
    lua_pop(L, 1);
    return res;
}

/** @} */ /* 结束时间日期操作系统文档组 */

/**
 * @brief 格式化日期和时间
 *
 * 根据指定格式字符串格式化日期和时间，或返回日期表。
 * 这是os.date函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（格式化字符串或日期表）
 *
 * @note 参数1：格式字符串（可选，默认为"%c"）
 * @note 参数2：时间戳（可选，默认为当前时间）
 *
 * @see strftime, gmtime, localtime, time
 *
 * 格式字符串特殊值：
 * - "*t"：返回日期表而不是字符串
 * - "!..."：使用UTC时间而不是本地时间
 * - 其他：使用strftime格式化
 *
 * 日期表字段：
 * - sec：秒（0-59）
 * - min：分钟（0-59）
 * - hour：小时（0-23）
 * - day：日期（1-31）
 * - month：月份（1-12）
 * - year：年份（如2023）
 * - wday：星期几（1-7，1为星期日）
 * - yday：一年中的第几天（1-366）
 * - isdst：是否夏令时
 *
 * 处理流程：
 * 1. **参数解析**：
 *    - 获取格式字符串（默认"%c"）
 *    - 获取时间戳（默认当前时间）
 *
 * 2. **时区处理**：
 *    - 检查格式字符串是否以"!"开头
 *    - "!"开头使用gmtime（UTC时间）
 *    - 否则使用localtime（本地时间）
 *
 * 3. **格式处理**：
 *    - 无效时间：返回nil
 *    - "*t"格式：创建并返回日期表
 *    - 其他格式：使用strftime格式化
 *
 * 4. **字符串格式化**：
 *    - 逐字符处理格式字符串
 *    - 非%字符直接添加
 *    - %字符调用strftime处理
 *    - 使用缓冲区构建结果
 *
 * strftime格式说明符：
 * - %Y：四位年份
 * - %m：月份（01-12）
 * - %d：日期（01-31）
 * - %H：小时（00-23）
 * - %M：分钟（00-59）
 * - %S：秒（00-59）
 * - %c：完整日期时间
 * - %x：日期
 * - %X：时间
 *
 * 使用示例：
 * ```lua
 * local now = os.date()           -- 默认格式
 * local iso = os.date("%Y-%m-%d") -- ISO日期格式
 * local utc = os.date("!%c")      -- UTC时间
 * local table = os.date("*t")     -- 日期表
 * ```
 *
 * 错误处理：
 * - 无效时间戳：返回nil
 * - 格式化失败：可能返回空字符串
 * - 缓冲区溢出：自动处理
 *
 * 性能考虑：
 * - 缓冲区大小优化（200字节）
 * - 逐字符处理避免重复解析
 * - 高效的字符串构建
 */
static int os_date (lua_State *L) {
    const char *s = luaL_optstring(L, 1, "%c");
    time_t t = luaL_opt(L, (time_t)luaL_checknumber, 2, time(NULL));
    struct tm *stm;
    if (*s == '!') {  /* UTC？ */
        stm = gmtime(&t);
        s++;  /* 跳过'!' */
    }
    else
        stm = localtime(&t);
    if (stm == NULL)  /* 无效日期？ */
        lua_pushnil(L);
    else if (strcmp(s, "*t") == 0) {
        lua_createtable(L, 0, 9);  /* 9 = 字段数量 */
        setfield(L, "sec", stm->tm_sec);
        setfield(L, "min", stm->tm_min);
        setfield(L, "hour", stm->tm_hour);
        setfield(L, "day", stm->tm_mday);
        setfield(L, "month", stm->tm_mon+1);
        setfield(L, "year", stm->tm_year+1900);
        setfield(L, "wday", stm->tm_wday+1);
        setfield(L, "yday", stm->tm_yday+1);
        setboolfield(L, "isdst", stm->tm_isdst);
    }
    else {
        char cc[3];
        luaL_Buffer b;
        cc[0] = '%'; cc[2] = '\0';
        luaL_buffinit(L, &b);
        for (; *s; s++) {
            if (*s != '%' || *(s + 1) == '\0')  /* 没有转换说明符？ */
                luaL_addchar(&b, *s);
            else {
                size_t reslen;
                char buff[200];  /* 应该足够大以容纳任何转换结果 */
                cc[1] = *(++s);
                reslen = strftime(buff, sizeof(buff), cc, stm);
                luaL_addlstring(&b, buff, reslen);
            }
        }
        luaL_pushresult(&b);
    }
    return 1;
}

/**
 * @brief 计算时间戳或转换日期表为时间戳
 *
 * 获取当前时间戳或将日期表转换为时间戳。
 * 这是os.time函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（时间戳数值或nil）
 *
 * @note 无参数：返回当前时间戳
 * @note 参数1：日期表，转换为时间戳
 *
 * @see time, mktime, getfield, getboolfield
 *
 * 调用模式：
 * 1. **无参数调用**：
 *    - 使用time(NULL)获取当前时间
 *    - 返回Unix时间戳（秒）
 *
 * 2. **日期表调用**：
 *    - 参数必须是表类型
 *    - 从表中提取日期时间字段
 *    - 使用mktime转换为时间戳
 *
 * 日期表字段处理：
 * - sec：秒（默认0）
 * - min：分钟（默认0）
 * - hour：小时（默认12）
 * - day：日期（必需）
 * - month：月份（必需，1-12）
 * - year：年份（必需）
 * - isdst：夏令时标志（可选）
 *
 * 字段转换：
 * - month：Lua使用1-12，C使用0-11
 * - year：Lua使用实际年份，C使用1900年起
 * - wday和yday：由mktime自动计算
 *
 * 处理流程：
 * 1. **参数检查**：
 *    - 无参数或nil：获取当前时间
 *    - 有参数：检查是否为表类型
 *
 * 2. **字段提取**：
 *    - 使用getfield获取数值字段
 *    - 使用getboolfield获取布尔字段
 *    - 应用必要的转换和默认值
 *
 * 3. **时间转换**：
 *    - 调用mktime进行转换
 *    - 处理转换失败的情况
 *    - 返回时间戳或nil
 *
 * 错误处理：
 * - 无效日期：mktime返回-1，推送nil
 * - 缺少必需字段：getfield抛出错误
 * - 类型错误：luaL_checktype抛出错误
 *
 * 使用示例：
 * ```lua
 * local now = os.time()  -- 当前时间戳
 * local custom = os.time({
 *     year = 2023, month = 12, day = 25,
 *     hour = 10, min = 30, sec = 0
 * })
 * ```
 *
 * 时间戳特性：
 * - Unix时间戳（1970年1月1日起的秒数）
 * - 跨平台的标准时间表示
 * - 适用于时间计算和比较
 * - 精度为秒级
 */
static int os_time (lua_State *L) {
    time_t t;
    if (lua_isnoneornil(L, 1))  /* 无参数调用？ */
        t = time(NULL);  /* 获取当前时间 */
    else {
        struct tm ts;
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_settop(L, 1);  /* 确保表在栈顶 */
        ts.tm_sec = getfield(L, "sec", 0);
        ts.tm_min = getfield(L, "min", 0);
        ts.tm_hour = getfield(L, "hour", 12);
        ts.tm_mday = getfield(L, "day", -1);
        ts.tm_mon = getfield(L, "month", -1) - 1;
        ts.tm_year = getfield(L, "year", -1) - 1900;
        ts.tm_isdst = getboolfield(L, "isdst");
        t = mktime(&ts);
    }
    if (t == (time_t)(-1))
        lua_pushnil(L);
    else
        lua_pushnumber(L, (lua_Number)t);
    return 1;
}

/**
 * @brief 计算两个时间戳的差值
 *
 * 计算两个时间戳之间的秒数差值。这是os.difftime函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（时间差的秒数）
 *
 * @note 参数1：结束时间戳
 * @note 参数2：开始时间戳（可选，默认为0）
 *
 * @see difftime, luaL_checknumber, luaL_optnumber
 *
 * 计算公式：
 * ```
 * 结果 = 参数1 - 参数2
 * ```
 *
 * 参数处理：
 * - 参数1：必需的结束时间戳
 * - 参数2：可选的开始时间戳（默认0）
 * - 返回：时间差（秒，可为负数）
 *
 * 使用场景：
 * - 计算时间间隔
 * - 测量执行时间
 * - 时间比较和排序
 * - 日期计算
 *
 * 使用示例：
 * ```lua
 * local start = os.time()
 * -- 执行一些操作
 * local finish = os.time()
 * local elapsed = os.difftime(finish, start)
 * print("耗时:", elapsed, "秒")
 * ```
 *
 * 技术特点：
 * - 使用C标准库的difftime函数
 * - 处理跨平台的时间差计算
 * - 支持负数结果（时间倒退）
 * - 精度为秒级
 *
 * 注意事项：
 * - 结果可能为负数
 * - 精度限制在秒级
 * - 跨时区计算需要注意
 * - 夏令时变化的影响
 */
static int os_difftime (lua_State *L) {
    lua_pushnumber(L, difftime((time_t)(luaL_checknumber(L, 1)),
                               (time_t)(luaL_optnumber(L, 2, 0))));
    return 1;
}

/** @} */ /* 结束时间日期操作系统文档组 */

/**
 * @defgroup LocalizationAndExit 本地化和程序控制
 * @brief 本地化设置和程序退出控制功能
 *
 * 本地化和程序控制提供了区域设置管理和程序
 * 生命周期控制的功能。
 * @{
 */

/**
 * @brief 设置程序的本地化区域
 *
 * 设置或查询程序的本地化区域设置。这是os.setlocale函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（当前区域设置字符串）
 *
 * @note 参数1：区域设置字符串（可选，NULL查询当前设置）
 * @note 参数2：区域类别（可选，默认"all"）
 *
 * @see setlocale, luaL_optstring, luaL_checkoption
 *
 * 支持的区域类别：
 * - "all"：所有区域设置
 * - "collate"：字符串排序规则
 * - "ctype"：字符分类和转换
 * - "monetary"：货币格式
 * - "numeric"：数字格式
 * - "time"：时间日期格式
 *
 * 区域设置字符串：
 * - NULL：查询当前设置
 * - ""：使用环境变量设置
 * - "C"：使用标准C区域
 * - "POSIX"：使用POSIX区域
 * - 具体区域：如"en_US.UTF-8"
 *
 * 处理流程：
 * 1. **参数解析**：
 *    - 获取区域设置字符串（可选）
 *    - 获取区域类别（默认"all"）
 *
 * 2. **类别映射**：
 *    - 将字符串类别映射为LC_*常量
 *    - 使用静态数组进行快速查找
 *
 * 3. **区域设置**：
 *    - 调用setlocale函数
 *    - 返回实际设置的区域字符串
 *
 * 使用示例：
 * ```lua
 * local current = os.setlocale()        -- 查询当前设置
 * local old = os.setlocale("C")         -- 设置为C区域
 * local utf8 = os.setlocale("", "all")  -- 使用环境变量
 * ```
 *
 * 影响范围：
 * - 字符串比较和排序
 * - 数字和货币格式化
 * - 时间日期显示格式
 * - 字符分类函数行为
 *
 * 跨平台考虑：
 * - 不同系统支持的区域不同
 * - 区域名称格式可能不同
 * - 某些区域可能不可用
 * - 返回值表示实际设置结果
 */
static int os_setlocale (lua_State *L) {
    static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                              LC_NUMERIC, LC_TIME};
    static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
                                           "numeric", "time", NULL};
    const char *l = luaL_optstring(L, 1, NULL);
    int op = luaL_checkoption(L, 2, "all", catnames);
    lua_pushstring(L, setlocale(cat[op], l));
    return 1;
}

/**
 * @brief 退出程序
 *
 * 立即终止程序执行。这是os.exit函数的实现。
 *
 * @param L Lua状态机指针
 * @return 永不返回（程序终止）
 *
 * @note 参数1：退出码（可选，默认EXIT_SUCCESS）
 * @note 函数不返回，程序直接终止
 *
 * @see exit, luaL_optint, EXIT_SUCCESS
 *
 * 退出码含义：
 * - 0或EXIT_SUCCESS：成功退出
 * - 非0或EXIT_FAILURE：错误退出
 * - 具体值的含义依赖于系统和约定
 *
 * 程序终止过程：
 * 1. 调用atexit注册的清理函数
 * 2. 刷新并关闭所有打开的流
 * 3. 删除tmpfile创建的临时文件
 * 4. 返回控制给主机环境
 *
 * 使用示例：
 * ```lua
 * os.exit()        -- 成功退出
 * os.exit(0)       -- 明确指定成功退出
 * os.exit(1)       -- 错误退出
 * ```
 *
 * 注意事项：
 * - 不会执行Lua的清理代码
 * - 不会调用__gc元方法
 * - 立即终止，无法恢复
 * - 可能丢失未保存的数据
 *
 * 替代方案：
 * - 使用return从主程序返回
 * - 抛出错误让上层处理
 * - 使用信号处理优雅退出
 */
static int os_exit (lua_State *L) {
    exit(luaL_optint(L, 1, EXIT_SUCCESS));
}

/** @} */ /* 结束本地化和程序控制文档组 */

/**
 * @defgroup LibraryRegistration 库注册和初始化
 * @brief 操作系统库的注册表和初始化机制
 *
 * 库注册和初始化系统定义了操作系统库的函数映射
 * 和标准的Lua库初始化流程。
 * @{
 */

/**
 * @brief 操作系统库函数注册表
 *
 * 定义了操作系统库中所有导出函数的名称和实现映射。
 * 这是Lua库注册的标准模式。
 *
 * 注册的函数：
 * - clock：CPU时间测量
 * - date：日期时间格式化
 * - difftime：时间差计算
 * - execute：系统命令执行
 * - exit：程序退出
 * - getenv：环境变量获取
 * - remove：文件删除
 * - rename：文件重命名
 * - setlocale：本地化设置
 * - time：时间戳获取
 * - tmpname：临时文件名生成
 *
 * 数组结构：
 * - 每个元素包含函数名和函数指针
 * - 以{NULL, NULL}结尾标记数组结束
 * - 使用luaL_Reg结构体类型
 *
 * 设计模式：
 * - 静态常量数组，编译时确定
 * - 标准的Lua库注册格式
 * - 便于维护和扩展
 * - 支持自动化工具处理
 */
static const luaL_Reg syslib[] = {
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

/**
 * @brief 操作系统库初始化函数
 *
 * Lua操作系统库的标准初始化入口点。由Lua虚拟机
 * 调用以加载和注册操作系统库。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（库表）
 *
 * @note 函数名遵循luaopen_<libname>约定
 * @note 使用LUALIB_API导出声明
 *
 * @see luaL_register, LUA_OSLIBNAME
 *
 * 初始化流程：
 * 1. **库注册**：
 *    - 使用luaL_register注册函数表
 *    - 创建名为"os"的全局表
 *    - 将所有函数添加到表中
 *
 * 2. **返回库表**：
 *    - 将库表留在栈顶
 *    - 返回1表示有一个返回值
 *    - 符合Lua库加载约定
 *
 * 调用方式：
 * - 静态链接：直接调用luaopen_os
 * - 动态加载：通过require "os"
 * - 自动加载：Lua启动时自动加载
 *
 * 库名称：
 * - 使用LUA_OSLIBNAME宏定义
 * - 通常为"os"
 * - 可在编译时配置
 *
 * 使用示例：
 * ```c
 * // 在C代码中手动加载
 * luaopen_os(L);
 *
 * // 在Lua中使用
 * local os = require "os"
 * print(os.date())
 * ```
 *
 * 标准约定：
 * - 函数名格式：luaopen_<库名>
 * - 返回值：库表（栈顶）
 * - 副作用：设置全局变量
 * - 错误处理：通过Lua错误机制
 */
LUALIB_API int luaopen_os (lua_State *L) {
    luaL_register(L, LUA_OSLIBNAME, syslib);
    return 1;
}

/** @} */ /* 结束库注册和初始化文档组 */

