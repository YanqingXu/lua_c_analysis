/**
 * @file lua.c
 * @brief Lua独立解释器：命令行工具和交互式环境的完整实现
 *
 * 版权信息：
 * $Id: lua.c,v 1.160.1.2 2007/12/28 15:32:23 roberto Exp $
 * Lua独立解释器
 * 版权声明见lua.h文件
 *
 * 程序概述：
 * 本文件实现了Lua的独立解释器，提供了完整的命令行工具功能。
 * 它是用户与Lua语言交互的主要入口点，支持脚本执行、交互模式、
 * 命令行参数处理和错误管理等核心功能。
 *
 * 主要功能：
 * 1. 命令行参数解析和选项处理
 * 2. Lua脚本文件的加载和执行
 * 3. 交互式REPL（读取-求值-打印-循环）环境
 * 4. 错误处理和异常管理
 * 5. 信号处理和程序中断管理
 * 6. 库加载和模块管理
 * 7. 调试支持和错误追踪
 *
 * 设计特点：
 * 1. 简洁高效：核心功能集中，代码简洁明了
 * 2. 用户友好：提供清晰的错误信息和使用帮助
 * 3. 可扩展性：支持多种执行模式和选项组合
 * 4. 健壮性：完善的错误处理和资源管理
 * 5. 跨平台：使用标准C库，具有良好的可移植性
 *
 * 程序架构：
 * - 主程序：参数解析和执行流程控制
 * - 执行引擎：脚本加载、编译和运行
 * - 交互系统：REPL环境和用户输入处理
 * - 错误管理：异常捕获、报告和恢复
 * - 信号处理：中断管理和优雅退出
 *
 * 使用场景：
 * - 脚本执行：lua script.lua
 * - 交互模式：lua -i
 * - 代码执行：lua -e "print('Hello')"
 * - 库加载：lua -l module script.lua
 * - 调试模式：结合各种选项进行调试
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2007-2008
 *
 * @note 这是Lua语言的官方解释器实现
 * @see lua.h, lauxlib.h, lualib.h
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lua_c

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/**
 * @defgroup GlobalVariables 全局变量和程序状态
 * @brief 解释器的全局状态管理
 *
 * 这些全局变量维护解释器的运行状态，包括Lua状态机的全局引用
 * 和程序名称等基本信息。
 * @{
 */

/**
 * @brief 全局Lua状态机指针
 *
 * 用于信号处理函数中访问Lua状态机。当接收到中断信号时，
 * 信号处理函数需要通过这个全局指针来设置调试钩子。
 *
 * @note 只在信号处理上下文中使用
 * @note 在主程序开始时设置，程序结束时清理
 *
 * @see laction, lstop
 */
static lua_State *globalL = NULL;

/**
 * @brief 程序名称
 *
 * 存储程序的名称，用于错误消息和使用帮助的显示。
 * 默认值为LUA_PROGNAME（通常是"lua"）。
 *
 * @note 在错误报告中用于标识消息来源
 * @note 在使用帮助中显示正确的程序名
 *
 * @see print_usage, l_message
 */
static const char *progname = LUA_PROGNAME;

/** @} */ /* 结束全局变量和程序状态文档组 */

/**
 * @defgroup SignalHandling 信号处理系统
 * @brief 程序中断和信号管理
 *
 * 信号处理系统提供了优雅的程序中断机制，允许用户通过
 * Ctrl+C等方式中断正在运行的Lua脚本，而不会导致程序
 * 异常终止。
 *
 * 工作原理：
 * 1. 注册信号处理函数laction
 * 2. 接收到SIGINT时设置调试钩子
 * 3. 调试钩子lstop抛出Lua错误
 * 4. 错误被捕获并优雅处理
 *
 * 设计优势：
 * - 不会破坏Lua状态机的完整性
 * - 允许清理资源和保存状态
 * - 提供用户友好的中断体验
 * - 支持嵌套调用的中断处理
 * @{
 */

/**
 * @brief Lua调试钩子：处理程序中断
 *
 * 当接收到中断信号时，这个函数作为调试钩子被调用。
 * 它清除调试钩子并抛出一个Lua错误来中断执行。
 *
 * @param L Lua状态机指针
 * @param ar 调试信息结构（未使用）
 *
 * @note 这个函数不会正常返回，总是抛出错误
 * @note 清除调试钩子避免重复调用
 *
 * @see laction, lua_sethook, luaL_error
 *
 * 中断处理流程：
 * 1. 清除当前设置的调试钩子
 * 2. 抛出"interrupted!"错误
 * 3. 错误被上层的pcall捕获
 * 4. 程序优雅地处理中断
 *
 * 安全考虑：
 * - 不直接调用exit()，保持Lua状态完整性
 * - 通过错误机制实现中断，支持清理操作
 * - 避免在信号处理上下文中执行复杂操作
 */
static void lstop (lua_State *L, lua_Debug *ar) {
    (void)ar;  /* 未使用的参数 */
    lua_sethook(L, NULL, 0, 0);
    luaL_error(L, "interrupted!");
}

/**
 * @brief 信号处理函数：设置中断处理机制
 *
 * 当接收到SIGINT信号时调用，设置调试钩子来实现优雅的
 * 程序中断。同时重置信号处理为默认行为，防止无限循环。
 *
 * @param i 信号编号（通常是SIGINT）
 *
 * @note 使用全局变量globalL访问Lua状态机
 * @note 设置调试钩子在每个调用、返回和指令时触发
 *
 * @see lstop, globalL, signal
 *
 * 信号处理策略：
 * 1. 重置信号处理为默认行为（SIG_DFL）
 * 2. 设置调试钩子lstop
 * 3. 钩子在下一个Lua操作时被触发
 * 4. 如果再次收到SIGINT，程序直接终止
 *
 * 调试钩子设置：
 * - LUA_MASKCALL：函数调用时触发
 * - LUA_MASKRET：函数返回时触发
 * - LUA_MASKCOUNT：每执行1条指令触发
 *
 * 安全机制：
 * - 第一次SIGINT：优雅中断
 * - 第二次SIGINT：强制终止
 * - 避免无响应的程序状态
 */
static void laction (int i) {
    signal(i, SIG_DFL); /* 如果在lstop之前再次收到SIGINT，
                           终止进程（默认行为） */
    lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}

/** @} */ /* 结束信号处理系统文档组 */

/**
 * @defgroup UserInterface 用户界面和帮助系统
 * @brief 用户交互和信息显示功能
 *
 * 用户界面系统提供了程序的使用帮助、错误消息显示和
 * 版本信息等功能，确保用户能够正确使用解释器。
 * @{
 */

/**
 * @brief 显示程序使用帮助
 *
 * 向标准错误输出显示程序的使用方法和可用选项。
 * 这是用户了解程序功能的主要途径。
 *
 * @note 输出到stderr而不是stdout
 * @note 使用全局变量progname显示正确的程序名
 *
 * @see progname, fprintf, fflush
 *
 * 显示的选项说明：
 * - -e stat：执行字符串代码
 * - -l name：加载指定库
 * - -i：进入交互模式
 * - -v：显示版本信息
 * - --：停止处理选项
 * - -：从标准输入执行并停止处理选项
 *
 * 使用格式：
 * ```
 * usage: lua [options] [script [args]].
 * ```
 *
 * 设计考虑：
 * - 简洁明了的选项说明
 * - 标准的Unix命令行格式
 * - 输出到stderr符合惯例
 * - 立即刷新输出确保显示
 */
static void print_usage (void) {
    fprintf(stderr,
    "usage: %s [options] [script [args]].\n"
    "Available options are:\n"
    "  -e stat  execute string " LUA_QL("stat") "\n"
    "  -l name  require library " LUA_QL("name") "\n"
    "  -i       enter interactive mode after executing " LUA_QL("script") "\n"
    "  -v       show version information\n"
    "  --       stop handling options\n"
    "  -        execute stdin and stop handling options\n"
    ,
    progname);
    fflush(stderr);
}


/**
 * @brief 显示错误消息
 *
 * 向标准错误输出显示格式化的错误消息。如果提供了程序名，
 * 会在消息前加上程序名作为前缀。
 *
 * @param pname 程序名（可以为NULL）
 * @param msg 错误消息内容
 *
 * @note 输出格式：程序名: 消息
 * @note 立即刷新输出确保消息显示
 *
 * @see fprintf, fflush
 *
 * 消息格式：
 * - 有程序名：lua: error message
 * - 无程序名：error message
 *
 * 使用场景：
 * - 脚本执行错误
 * - 命令行参数错误
 * - 文件加载错误
 * - 其他运行时错误
 */
static void l_message (const char *pname, const char *msg) {
    if (pname) fprintf(stderr, "%s: ", pname);
    fprintf(stderr, "%s\n", msg);
    fflush(stderr);
}

/**
 * @brief 报告执行状态和错误
 *
 * 检查Lua函数的执行状态，如果有错误则显示错误消息。
 * 这是统一的错误报告机制。
 *
 * @param L Lua状态机指针
 * @param status 执行状态码（0表示成功）
 * @return 返回传入的状态码
 *
 * @note 错误消息从栈顶获取
 * @note 处理后会弹出错误消息
 *
 * @see l_message, lua_tostring, lua_pop
 *
 * 错误处理流程：
 * 1. 检查状态码是否表示错误
 * 2. 检查栈顶是否有错误对象
 * 3. 尝试将错误对象转换为字符串
 * 4. 显示错误消息
 * 5. 清理栈上的错误对象
 *
 * 错误对象处理：
 * - 字符串：直接显示
 * - 其他类型：显示默认消息
 * - nil：不显示消息
 *
 * 返回值：
 * - 成功：0
 * - 错误：非零状态码
 */
static int report (lua_State *L, int status) {
    if (status && !lua_isnil(L, -1)) {
        const char *msg = lua_tostring(L, -1);
        if (msg == NULL) msg = "(error object is not a string)";
        l_message(progname, msg);
        lua_pop(L, 1);
    }
    return status;
}

/** @} */ /* 结束用户界面和帮助系统文档组 */

/**
 * @defgroup ErrorHandling 错误处理和调试支持
 * @brief 错误追踪和调试信息生成
 *
 * 错误处理系统提供了完整的错误追踪和调试支持，
 * 包括调用栈回溯、错误消息格式化和调试信息生成。
 * @{
 */

/**
 * @brief 生成调用栈回溯信息
 *
 * 这是一个Lua C函数，用于生成详细的调用栈回溯信息。
 * 它调用debug.traceback函数来生成格式化的错误追踪。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（栈上有一个返回值）
 *
 * @note 参数1：错误消息
 * @note 如果debug.traceback不可用，返回原始消息
 *
 * @see lua_getfield, lua_call, debug.traceback
 *
 * 回溯生成流程：
 * 1. 检查错误消息是否为字符串
 * 2. 获取全局debug表
 * 3. 获取debug.traceback函数
 * 4. 调用traceback函数生成回溯
 * 5. 返回格式化的错误信息
 *
 * 容错处理：
 * - 消息不是字符串：保持原样
 * - debug表不存在：保持原样
 * - traceback函数不存在：保持原样
 * - 确保在任何情况下都有返回值
 *
 * 调用参数：
 * - 参数1：错误消息
 * - 参数2：跳过的栈层数（2，跳过traceback和docall）
 *
 * 输出格式：
 * ```
 * error message
 * stack traceback:
 *     file:line: in function 'name'
 *     ...
 * ```
 */
static int traceback (lua_State *L) {
    if (!lua_isstring(L, 1))  /* 'message'不是字符串？ */
        return 1;  /* 保持原样 */
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return 1;
    }
    lua_pushvalue(L, 1);  /* 传递错误消息 */
    lua_pushinteger(L, 2);  /* 跳过这个函数和traceback */
    lua_call(L, 2, 1);  /* 调用debug.traceback */
    return 1;
}

/**
 * @brief 安全调用Lua函数
 *
 * 在保护模式下调用Lua函数，提供错误处理和信号处理支持。
 * 这是执行Lua代码的核心函数。
 *
 * @param L Lua状态机指针
 * @param narg 参数数量
 * @param clear 是否清除返回值（1清除，0保留）
 * @return 执行状态码（0表示成功）
 *
 * @note 自动设置错误处理函数
 * @note 支持信号中断处理
 *
 * @see lua_pcall, traceback, signal
 *
 * 执行流程：
 * 1. 计算函数在栈中的位置
 * 2. 推入traceback错误处理函数
 * 3. 设置SIGINT信号处理
 * 4. 调用lua_pcall执行函数
 * 5. 恢复默认信号处理
 * 6. 清理错误处理函数
 * 7. 如果有错误，强制垃圾回收
 *
 * 信号处理：
 * - 执行前：设置laction处理SIGINT
 * - 执行后：恢复SIG_DFL默认处理
 * - 支持用户中断长时间运行的脚本
 *
 * 错误处理：
 * - 使用traceback函数生成详细错误信息
 * - 错误时强制垃圾回收释放内存
 * - 保护主程序不被错误终止
 *
 * 返回值处理：
 * - clear=1：清除所有返回值
 * - clear=0：保留所有返回值（LUA_MULTRET）
 *
 * 栈管理：
 * - 自动管理错误处理函数的栈位置
 * - 确保栈状态的一致性
 * - 处理各种异常情况
 */
static int docall (lua_State *L, int narg, int clear) {
    int status;
    int base = lua_gettop(L) - narg;  /* 函数索引 */
    lua_pushcfunction(L, traceback);  /* 推入traceback函数 */
    lua_insert(L, base);  /* 将其放在chunk和参数下面 */
    signal(SIGINT, laction);
    status = lua_pcall(L, narg, (clear ? 0 : LUA_MULTRET), base);
    signal(SIGINT, SIG_DFL);
    lua_remove(L, base);  /* 移除traceback函数 */
    /* 如果有错误，强制完整的垃圾回收 */
    if (status != 0) lua_gc(L, LUA_GCCOLLECT, 0);
    return status;
}

/** @} */ /* 结束错误处理和调试支持文档组 */


/**
 * @defgroup VersionInfo 版本信息显示
 * @brief 程序版本和版权信息管理
 * @{
 */

/**
 * @brief 显示版本信息
 *
 * 显示Lua解释器的版本号和版权信息。这是-v选项的实现。
 *
 * @note 使用l_message函数输出到stderr
 * @note 不显示程序名前缀
 *
 * @see l_message, LUA_RELEASE, LUA_COPYRIGHT
 *
 * 显示内容：
 * - Lua版本号（如：Lua 5.1.5）
 * - 版权信息
 * - 发布日期等信息
 *
 * 使用场景：
 * - lua -v
 * - 版本检查
 * - 调试信息收集
 */
static void print_version (void) {
    l_message(NULL, LUA_RELEASE "  " LUA_COPYRIGHT);
}

/** @} */ /* 结束版本信息显示文档组 */

/**
 * @defgroup ArgumentProcessing 参数处理和脚本执行
 * @brief 命令行参数处理和各种执行模式
 *
 * 参数处理系统负责解析命令行参数，创建arg表，
 * 并提供不同的脚本执行模式。
 * @{
 */

/**
 * @brief 处理脚本参数并创建arg表
 *
 * 解析命令行参数，创建Lua的arg表。arg表包含了传递给
 * 脚本的所有参数，索引从-n到argc-n-1。
 *
 * @param L Lua状态机指针
 * @param argv 命令行参数数组
 * @param n 脚本名在argv中的索引
 * @return 传递给脚本的参数数量
 *
 * @note 创建的arg表会被推入栈顶
 * @note 参数索引可能为负数
 *
 * @see lua_createtable, lua_rawseti, luaL_checkstack
 *
 * arg表结构：
 * ```
 * arg[-n] = argv[0]     -- 程序名
 * arg[-n+1] = argv[1]   -- 第一个选项
 * ...
 * arg[-1] = argv[n-1]   -- 脚本名前的最后一个选项
 * arg[0] = argv[n]      -- 脚本名
 * arg[1] = argv[n+1]    -- 第一个脚本参数
 * ...
 * arg[narg] = argv[argc-1] -- 最后一个脚本参数
 * ```
 *
 * 示例：
 * ```bash
 * lua -e "code" -l lib script.lua arg1 arg2
 * ```
 *
 * 对应的arg表：
 * ```lua
 * arg[-3] = "lua"
 * arg[-2] = "-e"
 * arg[-1] = "code"
 * arg[0] = "script.lua"
 * arg[1] = "arg1"
 * arg[2] = "arg2"
 * ```
 *
 * 栈管理：
 * - 检查栈空间是否足够
 * - 先推入所有脚本参数
 * - 再创建表并填充所有参数
 * - 确保索引计算正确
 *
 * 错误处理：
 * - 栈空间不足时报错
 * - 参数过多时给出明确提示
 */
static int getargs (lua_State *L, char **argv, int n) {
    int narg;
    int i;
    int argc = 0;
    while (argv[argc]) argc++;  /* 计算参数总数 */
    narg = argc - (n + 1);  /* 脚本参数数量 */
    luaL_checkstack(L, narg + 3, "too many arguments to script");
    for (i=n+1; i < argc; i++)
        lua_pushstring(L, argv[i]);
    lua_createtable(L, narg, n + 1);
    for (i=0; i < argc; i++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - n);
    }
    return narg;
}

/**
 * @brief 执行Lua脚本文件
 *
 * 加载并执行指定的Lua脚本文件。这是最常用的执行模式。
 *
 * @param L Lua状态机指针
 * @param name 脚本文件名
 * @return 执行状态码（0表示成功）
 *
 * @note 使用luaL_loadfile加载文件
 * @note 使用docall安全执行
 *
 * @see luaL_loadfile, docall, report
 *
 * 执行流程：
 * 1. 使用luaL_loadfile编译脚本文件
 * 2. 如果编译成功，使用docall执行
 * 3. 使用report报告执行结果
 * 4. 返回最终状态码
 *
 * 错误处理：
 * - 文件不存在：编译错误
 * - 语法错误：编译错误
 * - 运行时错误：执行错误
 * - 所有错误都通过report统一处理
 *
 * 使用场景：
 * - lua script.lua
 * - 批处理脚本执行
 * - 自动化任务运行
 */
static int dofile (lua_State *L, const char *name) {
    int status = luaL_loadfile(L, name) || docall(L, 0, 1);
    return report(L, status);
}

/**
 * @brief 执行Lua代码字符串
 *
 * 编译并执行给定的Lua代码字符串。这是-e选项的实现。
 *
 * @param L Lua状态机指针
 * @param s 要执行的Lua代码字符串
 * @param name 代码块的名称（用于错误报告）
 * @return 执行状态码（0表示成功）
 *
 * @note 使用luaL_loadbuffer编译字符串
 * @note 代码块名称用于错误追踪
 *
 * @see luaL_loadbuffer, docall, report
 *
 * 执行流程：
 * 1. 使用luaL_loadbuffer编译代码字符串
 * 2. 如果编译成功，使用docall执行
 * 3. 使用report报告执行结果
 * 4. 返回最终状态码
 *
 * 代码块命名：
 * - 通常使用"=(command line)"
 * - 帮助用户识别错误来源
 * - 在错误追踪中显示
 *
 * 使用场景：
 * - lua -e "print('Hello')"
 * - 快速代码测试
 * - 一行脚本执行
 * - 配置和初始化代码
 */
static int dostring (lua_State *L, const char *s, const char *name) {
    int status = luaL_loadbuffer(L, s, strlen(s), name) || docall(L, 0, 1);
    return report(L, status);
}

/**
 * @brief 加载Lua库
 *
 * 使用require函数加载指定的Lua库。这是-l选项的实现。
 *
 * @param L Lua状态机指针
 * @param name 要加载的库名
 * @return 执行状态码（0表示成功）
 *
 * @note 调用全局require函数
 * @note 库加载结果会被丢弃
 *
 * @see lua_getglobal, docall, report
 *
 * 执行流程：
 * 1. 获取全局require函数
 * 2. 推入库名作为参数
 * 3. 使用docall调用require
 * 4. 使用report报告加载结果
 * 5. 返回最终状态码
 *
 * 库加载：
 * - 支持Lua库和C库
 * - 遵循package.path和package.cpath
 * - 库只加载一次（缓存机制）
 * - 加载后的库可在脚本中使用
 *
 * 使用场景：
 * - lua -l socket script.lua
 * - 预加载常用库
 * - 设置脚本运行环境
 * - 依赖库的自动加载
 */
static int dolibrary (lua_State *L, const char *name) {
    lua_getglobal(L, "require");
    lua_pushstring(L, name);
    return report(L, docall(L, 1, 1));
}

/** @} */ /* 结束参数处理和脚本执行文档组 */

/**
 * @defgroup InteractiveMode 交互式REPL模式
 * @brief 交互式读取-求值-打印-循环环境的完整实现
 *
 * 交互式模式是Lua解释器的核心功能之一，提供了用户友好的
 * 命令行交互环境。它支持多行输入、语法检查、自动补全和
 * 结果显示等功能。
 *
 * 核心特性：
 * - 智能提示符显示
 * - 多行输入支持
 * - 语法错误检测
 * - 自动表达式求值
 * - 历史记录管理
 * - 优雅的错误处理
 *
 * 设计理念：
 * - 用户友好的交互体验
 * - 智能的输入处理
 * - 灵活的命令执行
 * - 完善的错误反馈
 * @{
 */

/**
 * @brief 获取交互式提示符
 *
 * 根据输入状态获取相应的提示符。支持用户自定义提示符，
 * 通过全局变量_PROMPT和_PROMPT2进行配置。
 *
 * @param L Lua状态机指针
 * @param firstline 是否为首行输入（1为首行，0为续行）
 * @return 提示符字符串
 *
 * @note 首行使用_PROMPT或LUA_PROMPT
 * @note 续行使用_PROMPT2或LUA_PROMPT2
 *
 * @see lua_getfield, lua_tostring, LUA_PROMPT
 *
 * 提示符获取流程：
 * 1. 根据firstline参数选择全局变量名
 * 2. 从全局环境获取自定义提示符
 * 3. 如果自定义提示符不存在，使用默认值
 * 4. 清理栈上的全局变量值
 * 5. 返回最终的提示符字符串
 *
 * 默认提示符：
 * - 首行："> "（LUA_PROMPT）
 * - 续行：">> "（LUA_PROMPT2）
 *
 * 自定义示例：
 * ```lua
 * _PROMPT = "lua> "
 * _PROMPT2 = "... "
 * ```
 *
 * 使用场景：
 * - 交互式命令行显示
 * - 多行输入的视觉提示
 * - 用户体验个性化
 */
static const char *get_prompt (lua_State *L, int firstline) {
    const char *p;
    lua_getfield(L, LUA_GLOBALSINDEX, firstline ? "_PROMPT" : "_PROMPT2");
    p = lua_tostring(L, -1);
    if (p == NULL) p = (firstline ? LUA_PROMPT : LUA_PROMPT2);
    lua_pop(L, 1);  /* 移除全局变量 */
    return p;
}

/**
 * @brief 检查语法错误是否为不完整输入
 *
 * 分析语法错误消息，判断是否因为输入不完整导致的错误。
 * 这是实现多行输入的关键函数。
 *
 * @param L Lua状态机指针
 * @param status 编译状态码
 * @return 1表示输入不完整，0表示其他错误
 *
 * @note 只处理LUA_ERRSYNTAX类型的错误
 * @note 通过检查"<eof>"标记判断不完整
 *
 * @see lua_tolstring, strstr, LUA_QL
 *
 * 检测算法：
 * 1. 检查状态码是否为语法错误
 * 2. 获取错误消息字符串和长度
 * 3. 计算"<eof>"标记的预期位置
 * 4. 检查错误消息是否以"<eof>"结尾
 * 5. 如果是，清理错误消息并返回1
 * 6. 否则返回0表示真正的语法错误
 *
 * 不完整输入示例：
 * ```lua
 * > function test()
 * >>   print("hello")
 * >> end
 * ```
 *
 * 错误消息格式：
 * - 不完整：'<eof>' expected near '<eof>'
 * - 语法错误：具体的语法错误描述
 *
 * 应用场景：
 * - 多行函数定义
 * - 复杂表达式输入
 * - 控制结构的跨行输入
 */
static int incomplete (lua_State *L, int status) {
    if (status == LUA_ERRSYNTAX) {
        size_t lmsg;
        const char *msg = lua_tolstring(L, -1, &lmsg);
        const char *tp = msg + lmsg - (sizeof(LUA_QL("<eof>")) - 1);
        if (strstr(msg, LUA_QL("<eof>")) == tp) {
            lua_pop(L, 1);
            return 1;
        }
    }
    return 0;  /* 否则... */
}

/**
 * @brief 读取并处理一行用户输入
 *
 * 从标准输入读取一行，进行预处理并推入Lua栈。
 * 支持特殊语法如"="前缀的表达式求值。
 *
 * @param L Lua状态机指针
 * @param firstline 是否为首行输入
 * @return 1表示成功读取，0表示输入结束
 *
 * @note 自动处理换行符
 * @note 支持"="前缀的快速求值
 *
 * @see get_prompt, lua_readline, lua_pushfstring
 *
 * 输入处理流程：
 * 1. 获取相应的提示符
 * 2. 调用lua_readline读取用户输入
 * 3. 如果没有输入，返回0
 * 4. 移除行尾的换行符
 * 5. 处理"="前缀的特殊语法
 * 6. 将处理后的内容推入栈
 * 7. 释放输入缓冲区
 * 8. 返回成功标志
 *
 * 特殊语法处理：
 * - "=expression" -> "return expression"
 * - 方便快速查看表达式的值
 * - 类似于其他解释器的求值功能
 *
 * 使用示例：
 * ```
 * > =2+3
 * 5
 * > =math.pi
 * 3.1415926535898
 * ```
 *
 * 缓冲区管理：
 * - 使用固定大小的缓冲区
 * - 自动处理内存分配和释放
 * - 支持长输入行的处理
 */
static int pushline (lua_State *L, int firstline) {
    char buffer[LUA_MAXINPUT];
    char *b = buffer;
    size_t l;
    const char *prmt = get_prompt(L, firstline);
    if (lua_readline(L, b, prmt) == 0)
        return 0;  /* 没有输入 */
    l = strlen(b);
    if (l > 0 && b[l-1] == '\n')  /* 行以换行符结尾？ */
        b[l-1] = '\0';  /* 移除它 */
    if (firstline && b[0] == '=')  /* 首行以'='开始？ */
        lua_pushfstring(L, "return %s", b+1);  /* 改为'return' */
    else
        lua_pushstring(L, b);
    lua_freeline(L, b);
    return 1;
}

/** @} */ /* 结束交互式REPL模式文档组 */


/**
 * @defgroup REPLCore REPL核心实现
 * @brief 读取-求值-打印-循环的核心逻辑
 *
 * REPL核心实现提供了完整的交互式环境，包括多行输入处理、
 * 代码编译、执行和结果显示等功能。
 * @{
 */

/**
 * @brief 加载完整的输入行
 *
 * 读取用户输入并编译为Lua代码块。支持多行输入，
 * 自动检测输入是否完整。
 *
 * @param L Lua状态机指针
 * @return 编译状态码，-1表示输入结束
 *
 * @note 自动处理多行输入的拼接
 * @note 保存输入历史记录
 *
 * @see pushline, incomplete, luaL_loadbuffer
 *
 * 加载流程：
 * 1. 清空栈，准备新的输入
 * 2. 读取第一行输入
 * 3. 如果没有输入，返回-1
 * 4. 尝试编译当前输入
 * 5. 如果编译失败且是不完整输入：
 *    - 读取下一行输入
 *    - 在两行之间插入换行符
 *    - 拼接所有输入行
 *    - 重新尝试编译
 * 6. 重复步骤5直到输入完整或出错
 * 7. 保存输入到历史记录
 * 8. 清理栈上的输入字符串
 * 9. 返回编译状态
 *
 * 多行输入示例：
 * ```
 * > function factorial(n)
 * >>   if n <= 1 then
 * >>     return 1
 * >>   else
 * >>     return n * factorial(n-1)
 * >>   end
 * >> end
 * ```
 *
 * 错误处理：
 * - 输入结束：返回-1
 * - 语法错误：返回错误状态码
 * - 编译成功：返回0
 *
 * 历史记录：
 * - 自动保存完整的输入
 * - 支持历史记录回调
 * - 便于用户重复使用
 */
static int loadline (lua_State *L) {
    int status;
    lua_settop(L, 0);
    if (!pushline(L, 1))
        return -1;  /* 没有输入 */
    for (;;) {  /* 重复直到获得完整行 */
        status = luaL_loadbuffer(L, lua_tostring(L, 1), lua_strlen(L, 1), "=stdin");
        if (!incomplete(L, status)) break;  /* 不能尝试添加行？ */
        if (!pushline(L, 0))  /* 没有更多输入？ */
            return -1;
        lua_pushliteral(L, "\n");  /* 添加新行... */
        lua_insert(L, -2);  /* ...在两行之间 */
        lua_concat(L, 3);  /* 连接它们 */
    }
    lua_saveline(L, 1);
    lua_remove(L, 1);  /* 移除行 */
    return status;
}

/**
 * @brief 交互式模式主循环
 *
 * 实现完整的REPL（读取-求值-打印-循环）功能。
 * 这是交互式模式的核心函数。
 *
 * @param L Lua状态机指针
 *
 * @note 临时清除程序名以改善错误显示
 * @note 自动打印表达式结果
 *
 * @see loadline, docall, report
 *
 * REPL循环流程：
 * 1. 保存原程序名并清除（改善错误显示）
 * 2. 进入主循环：
 *    a. 调用loadline读取并编译输入
 *    b. 如果输入结束（-1），退出循环
 *    c. 如果编译成功，执行代码
 *    d. 报告执行状态和错误
 *    e. 如果有返回值，自动打印
 * 3. 清理栈状态
 * 4. 输出最终换行符
 * 5. 恢复原程序名
 *
 * 自动打印机制：
 * - 检查栈上是否有返回值
 * - 调用全局print函数打印结果
 * - 处理print函数调用的错误
 * - 提供用户友好的结果显示
 *
 * 错误处理：
 * - 编译错误：显示语法错误信息
 * - 运行时错误：显示执行错误和调用栈
 * - print错误：显示print函数调用错误
 * - 所有错误都不会终止REPL循环
 *
 * 用户体验：
 * - 清除程序名避免冗余的错误前缀
 * - 自动打印表达式结果
 * - 优雅的错误恢复
 * - 持续的交互体验
 *
 * 退出条件：
 * - 用户输入EOF（Ctrl+D或Ctrl+Z）
 * - 输入流结束
 * - 系统信号中断
 */
static void dotty (lua_State *L) {
    int status;
    const char *oldprogname = progname;
    progname = NULL;
    while ((status = loadline(L)) != -1) {
        if (status == 0) status = docall(L, 0, 0);
        report(L, status);
        if (status == 0 && lua_gettop(L) > 0) {  /* 有结果要打印？ */
            lua_getglobal(L, "print");
            lua_insert(L, 1);
            if (lua_pcall(L, lua_gettop(L)-1, 0, 0) != 0)
                l_message(progname, lua_pushfstring(L,
                                   "error calling " LUA_QL("print") " (%s)",
                                   lua_tostring(L, -1)));
        }
    }
    lua_settop(L, 0);  /* 清空栈 */
    fputs("\n", stdout);
    fflush(stdout);
    progname = oldprogname;
}

/** @} */ /* 结束REPL核心实现文档组 */


/**
 * @defgroup ScriptHandling 脚本处理和执行
 * @brief 脚本文件的加载、参数处理和执行管理
 *
 * 脚本处理系统负责加载和执行Lua脚本文件，管理脚本参数，
 * 并提供标准输入执行支持。
 * @{
 */

/**
 * @brief 处理脚本执行
 *
 * 加载并执行指定的Lua脚本文件，同时设置脚本参数。
 * 这是脚本模式的核心函数。
 *
 * @param L Lua状态机指针
 * @param argv 命令行参数数组
 * @param n 脚本名在argv中的索引
 * @return 执行状态码（0表示成功）
 *
 * @note 自动创建arg全局变量
 * @note 支持标准输入执行（文件名为"-"）
 *
 * @see getargs, luaL_loadfile, docall
 *
 * 执行流程：
 * 1. 调用getargs处理命令行参数
 * 2. 将arg表设置为全局变量
 * 3. 获取脚本文件名
 * 4. 处理特殊文件名"-"（标准输入）
 * 5. 加载脚本文件
 * 6. 调整栈，将脚本函数放在参数下面
 * 7. 如果加载成功，执行脚本并传递参数
 * 8. 如果加载失败，清理参数
 * 9. 报告执行结果
 *
 * 参数传递：
 * - 脚本参数作为函数参数传递
 * - arg表作为全局变量提供
 * - 支持脚本内部访问命令行参数
 *
 * 标准输入处理：
 * - 文件名为"-"且前一个参数不是"--"
 * - 从标准输入读取脚本内容
 * - 支持管道和重定向操作
 *
 * 错误处理：
 * - 文件不存在：加载错误
 * - 语法错误：编译错误
 * - 运行时错误：执行错误
 * - 所有错误通过report统一处理
 *
 * 栈管理：
 * - 自动调整脚本函数和参数的栈位置
 * - 确保参数正确传递给脚本
 * - 处理加载失败时的栈清理
 */
static int handle_script (lua_State *L, char **argv, int n) {
    int status;
    const char *fname;
    int narg = getargs(L, argv, n);  /* 收集参数 */
    lua_setglobal(L, "arg");
    fname = argv[n];
    if (strcmp(fname, "-") == 0 && strcmp(argv[n-1], "--") != 0)
        fname = NULL;  /* 标准输入 */
    status = luaL_loadfile(L, fname);
    lua_insert(L, -(narg+1));
    if (status == 0)
        status = docall(L, narg, 0);
    else
        lua_pop(L, narg);
    return report(L, status);
}

/** @} */ /* 结束脚本处理和执行文档组 */

/**
 * @defgroup CommandLineProcessing 命令行处理系统
 * @brief 命令行参数解析和选项处理
 *
 * 命令行处理系统负责解析和验证命令行参数，识别各种选项，
 * 并为后续的执行流程提供配置信息。
 * @{
 */

/**
 * @brief 检查参数末尾无多余字符的宏
 *
 * 验证单字符选项后面没有额外的字符。如果有额外字符，
 * 返回-1表示无效选项。
 *
 * @param x 要检查的参数字符串
 *
 * 使用示例：
 * - "-v" -> 有效
 * - "-vv" -> 无效（返回-1）
 * - "-i" -> 有效
 * - "-ix" -> 无效（返回-1）
 */
#define notail(x)	{if ((x)[2] != '\0') return -1;}

/**
 * @brief 收集和解析命令行参数
 *
 * 解析命令行参数，识别各种选项并设置相应的标志。
 * 这是命令行处理的核心函数。
 *
 * @param argv 命令行参数数组
 * @param pi 指向交互模式标志的指针
 * @param pv 指向版本显示标志的指针
 * @param pe 指向代码执行标志的指针
 * @return 脚本名的索引，0表示无脚本，-1表示错误
 *
 * @note 使用状态机方式解析选项
 * @note 支持选项组合和参数验证
 *
 * @see notail
 *
 * 支持的选项：
 * - "--"：停止选项处理
 * - "-"：从标准输入执行
 * - "-i"：交互模式
 * - "-v"：显示版本
 * - "-e stat"：执行代码字符串
 * - "-l name"：加载库
 *
 * 解析算法：
 * 1. 遍历所有命令行参数
 * 2. 跳过程序名（argv[0]）
 * 3. 对每个参数：
 *    a. 如果不以"-"开头，返回当前索引（脚本名）
 *    b. 根据第二个字符识别选项类型
 *    c. 验证选项格式和参数
 *    d. 设置相应的标志
 * 4. 处理特殊情况和错误
 * 5. 返回脚本位置或状态码
 *
 * 选项处理规则：
 * - "--"：停止处理后续选项
 * - "-"：表示从标准输入读取
 * - "-i"和"-v"：可以组合使用
 * - "-e"和"-l"：需要参数
 * - 无效选项：返回-1
 *
 * 返回值含义：
 * - >0：脚本名在argv中的索引
 * - 0：没有脚本文件
 * - -1：参数错误
 *
 * 错误情况：
 * - 无效的选项字符
 * - 选项后有多余字符
 * - 需要参数的选项缺少参数
 */
static int collectargs (char **argv, int *pi, int *pv, int *pe) {
    int i;
    for (i = 1; argv[i] != NULL; i++) {
        if (argv[i][0] != '-')  /* 不是选项？ */
            return i;
        switch (argv[i][1]) {  /* 选项 */
            case '-':
                notail(argv[i]);
                return (argv[i+1] != NULL ? i+1 : 0);
            case '\0':
                return i;
            case 'i':
                notail(argv[i]);
                *pi = 1;  /* 继续执行 */
            case 'v':
                notail(argv[i]);
                *pv = 1;
                break;
            case 'e':
                *pe = 1;  /* 继续执行 */
            case 'l':
                if (argv[i][2] == '\0') {
                    i++;
                    if (argv[i] == NULL) return -1;
                }
                break;
            default: return -1;  /* 无效选项 */
        }
    }
    return 0;
}

/** @} */ /* 结束命令行处理系统文档组 */


/**
 * @defgroup OptionExecution 选项执行系统
 * @brief 命令行选项的具体执行和处理
 *
 * 选项执行系统负责执行通过命令行指定的各种操作，
 * 包括代码执行、库加载和环境初始化。
 * @{
 */

/**
 * @brief 执行命令行选项
 *
 * 按顺序执行命令行中指定的-e和-l选项。这些选项在
 * 脚本执行之前处理，用于设置环境和执行初始化代码。
 *
 * @param L Lua状态机指针
 * @param argv 命令行参数数组
 * @param n 处理到的参数索引（不包括）
 * @return 0表示成功，1表示有错误
 *
 * @note 按参数顺序执行选项
 * @note 任何选项失败都会停止处理
 *
 * @see dostring, dolibrary
 *
 * 处理的选项：
 * - "-e code"：执行Lua代码字符串
 * - "-l library"：加载Lua库
 *
 * 执行流程：
 * 1. 遍历从1到n-1的所有参数
 * 2. 跳过NULL参数（已处理的参数）
 * 3. 确认参数是选项（以"-"开头）
 * 4. 根据选项类型执行相应操作：
 *    - 'e'：执行代码字符串
 *    - 'l'：加载指定库
 * 5. 如果任何操作失败，返回1
 * 6. 所有操作成功，返回0
 *
 * 参数格式：
 * - "-e code"：代码在同一参数中
 * - "-e" "code"：代码在下一参数中
 * - "-l lib"：库名在同一参数中
 * - "-l" "lib"：库名在下一参数中
 *
 * 执行顺序：
 * - 严格按照命令行参数顺序
 * - 先执行的选项可以影响后执行的选项
 * - 支持复杂的初始化序列
 *
 * 错误处理：
 * - 代码执行错误：停止处理
 * - 库加载错误：停止处理
 * - 错误信息已通过dostring/dolibrary报告
 *
 * 使用示例：
 * ```bash
 * lua -l socket -e "print('loaded')" -l http script.lua
 * ```
 */
static int runargs (lua_State *L, char **argv, int n) {
    int i;
    for (i = 1; i < n; i++) {
        if (argv[i] == NULL) continue;
        lua_assert(argv[i][0] == '-');
        switch (argv[i][1]) {  /* 选项 */
            case 'e': {
                const char *chunk = argv[i] + 2;
                if (*chunk == '\0') chunk = argv[++i];
                lua_assert(chunk != NULL);
                if (dostring(L, chunk, "=(command line)") != 0)
                    return 1;
                break;
            }
            case 'l': {
                const char *filename = argv[i] + 2;
                if (*filename == '\0') filename = argv[++i];
                lua_assert(filename != NULL);
                if (dolibrary(L, filename))
                    return 1;  /* 如果文件失败则停止 */
                break;
            }
            default: break;
        }
    }
    return 0;
}

/** @} */ /* 结束选项执行系统文档组 */

/**
 * @defgroup Initialization 程序初始化系统
 * @brief 程序启动时的环境初始化和配置
 *
 * 初始化系统负责处理程序启动时的环境配置，
 * 包括LUA_INIT环境变量的处理。
 * @{
 */

/**
 * @brief 处理LUA_INIT环境变量
 *
 * 检查并执行LUA_INIT环境变量指定的初始化代码或文件。
 * 这是Lua程序启动时的标准初始化机制。
 *
 * @param L Lua状态机指针
 * @return 0表示成功或无初始化，非0表示初始化失败
 *
 * @note 支持代码字符串和文件两种格式
 * @note 以"@"开头表示文件，否则为代码
 *
 * @see getenv, dofile, dostring
 *
 * LUA_INIT格式：
 * - 代码字符串：直接的Lua代码
 * - 文件路径：以"@"开头，后跟文件路径
 *
 * 处理流程：
 * 1. 获取LUA_INIT环境变量
 * 2. 如果环境变量不存在，返回成功
 * 3. 如果以"@"开头，作为文件处理：
 *    - 去掉"@"前缀
 *    - 调用dofile执行文件
 * 4. 否则作为代码字符串处理：
 *    - 调用dostring执行代码
 * 5. 返回执行结果
 *
 * 使用示例：
 * ```bash
 * export LUA_INIT="print('Lua initialized')"
 * export LUA_INIT="@/path/to/init.lua"
 * ```
 *
 * 应用场景：
 * - 设置全局变量和配置
 * - 加载常用库和模块
 * - 定义辅助函数和工具
 * - 配置搜索路径和环境
 *
 * 错误处理：
 * - 文件不存在：返回错误状态
 * - 语法错误：返回错误状态
 * - 运行时错误：返回错误状态
 * - 错误信息通过dofile/dostring报告
 *
 * 安全考虑：
 * - 环境变量可能包含恶意代码
 * - 在受限环境中应谨慎使用
 * - 建议验证环境变量来源
 */
static int handle_luainit (lua_State *L) {
    const char *init = getenv(LUA_INIT);
    if (init == NULL) return 0;  /* 状态OK */
    else if (init[0] == '@')
        return dofile(L, init+1);
    else
        return dostring(L, init, "=" LUA_INIT);
}

/** @} */ /* 结束程序初始化系统文档组 */


/**
 * @defgroup MainProgram 主程序和程序入口
 * @brief 程序的主要执行流程和入口点管理
 *
 * 主程序系统负责协调整个解释器的执行流程，包括初始化、
 * 参数处理、脚本执行和资源清理等核心功能。
 * @{
 */

/**
 * @brief 主程序参数结构
 *
 * 用于在保护模式下传递主程序参数的结构体。
 * 通过lua_cpcall传递给pmain函数。
 */
struct Smain {
    int argc;       /**< 命令行参数数量 */
    char **argv;    /**< 命令行参数数组 */
    int status;     /**< 程序执行状态 */
};

/**
 * @brief 保护模式下的主程序
 *
 * 在Lua保护模式下执行的主程序逻辑。处理所有可能抛出
 * Lua错误的操作，确保程序的稳定性。
 *
 * @param L Lua状态机指针
 * @return 总是返回0（通过s->status传递实际状态）
 *
 * @note 在保护模式下运行，捕获所有Lua错误
 * @note 通过Smain结构传递参数和状态
 *
 * @see lua_cpcall, collectargs, runargs, handle_script
 *
 * 执行流程：
 * 1. **初始化阶段**：
 *    - 获取传入的参数结构
 *    - 设置全局状态和程序名
 *    - 停止垃圾回收器
 *    - 打开标准库
 *    - 重启垃圾回收器
 *
 * 2. **环境初始化**：
 *    - 处理LUA_INIT环境变量
 *    - 如果初始化失败，返回错误
 *
 * 3. **参数解析**：
 *    - 解析命令行参数
 *    - 识别各种选项和脚本位置
 *    - 如果参数无效，显示使用帮助
 *
 * 4. **选项处理**：
 *    - 显示版本信息（如果需要）
 *    - 执行-e和-l选项
 *    - 如果选项执行失败，返回错误
 *
 * 5. **脚本执行**：
 *    - 如果有脚本文件，执行脚本
 *    - 如果脚本执行失败，返回错误
 *
 * 6. **交互模式**：
 *    - 如果指定了-i选项，进入交互模式
 *    - 如果没有脚本且没有其他选项：
 *      * 如果是TTY，显示版本并进入交互模式
 *      * 否则从标准输入执行
 *
 * 执行优先级：
 * 1. LUA_INIT初始化
 * 2. 命令行选项（-e, -l）
 * 3. 脚本文件执行
 * 4. 交互模式或标准输入
 *
 * 错误处理：
 * - 任何阶段的错误都会停止后续处理
 * - 错误状态通过s->status传递
 * - 保护模式确保不会崩溃
 *
 * 垃圾回收管理：
 * - 初始化期间停止GC提高性能
 * - 库加载完成后重启GC
 * - 避免初始化期间的GC干扰
 */
static int pmain (lua_State *L) {
    struct Smain *s = (struct Smain *)lua_touserdata(L, 1);
    char **argv = s->argv;
    int script;
    int has_i = 0, has_v = 0, has_e = 0;
    globalL = L;
    if (argv[0] && argv[0][0]) progname = argv[0];
    lua_gc(L, LUA_GCSTOP, 0);  /* 初始化期间停止收集器 */
    luaL_openlibs(L);  /* 打开库 */
    lua_gc(L, LUA_GCRESTART, 0);
    s->status = handle_luainit(L);
    if (s->status != 0) return 0;
    script = collectargs(argv, &has_i, &has_v, &has_e);
    if (script < 0) {  /* 无效参数？ */
        print_usage();
        s->status = 1;
        return 0;
    }
    if (has_v) print_version();
    s->status = runargs(L, argv, (script > 0) ? script : s->argc);
    if (s->status != 0) return 0;
    if (script)
        s->status = handle_script(L, argv, script);
    if (s->status != 0) return 0;
    if (has_i)
        dotty(L);
    else if (script == 0 && !has_e && !has_v) {
        if (lua_stdin_is_tty()) {
            print_version();
            dotty(L);
        }
        else dofile(L, NULL);  /* 将标准输入作为文件执行 */
    }
    return 0;
}

/**
 * @brief 程序主入口点
 *
 * Lua解释器的主函数，负责创建Lua状态机、调用主程序逻辑
 * 和处理最终的清理工作。
 *
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return EXIT_SUCCESS表示成功，EXIT_FAILURE表示失败
 *
 * @note 使用保护模式调用主程序逻辑
 * @note 确保Lua状态机的正确创建和销毁
 *
 * @see lua_open, lua_cpcall, lua_close, pmain
 *
 * 程序生命周期：
 * 1. **状态机创建**：
 *    - 调用lua_open创建新的Lua状态机
 *    - 如果创建失败，显示错误并退出
 *
 * 2. **参数准备**：
 *    - 创建Smain结构
 *    - 设置argc和argv
 *    - 初始化状态为0
 *
 * 3. **主程序执行**：
 *    - 使用lua_cpcall在保护模式下调用pmain
 *    - 传递Smain结构作为参数
 *    - 捕获所有可能的Lua错误
 *
 * 4. **结果处理**：
 *    - 报告执行状态和错误
 *    - 检查pmain的返回状态
 *    - 检查Smain中的执行状态
 *
 * 5. **资源清理**：
 *    - 关闭Lua状态机
 *    - 释放所有相关资源
 *    - 返回适当的退出码
 *
 * 错误处理：
 * - 状态机创建失败：内存不足
 * - 保护模式调用失败：Lua内部错误
 * - 主程序执行失败：用户代码错误
 * - 所有错误都有相应的错误消息
 *
 * 退出码：
 * - EXIT_SUCCESS (0)：程序成功执行
 * - EXIT_FAILURE (1)：程序执行失败
 *
 * 内存管理：
 * - 自动管理Lua状态机的生命周期
 * - 确保在任何情况下都正确清理
 * - 防止内存泄漏和资源泄漏
 *
 * 异常安全：
 * - 使用保护模式防止程序崩溃
 * - 即使用户代码有错误也能优雅退出
 * - 提供清晰的错误信息和诊断
 */
int main (int argc, char **argv) {
    int status;
    struct Smain s;
    lua_State *L = lua_open();  /* 创建状态 */
    if (L == NULL) {
        l_message(argv[0], "cannot create state: not enough memory");
        return EXIT_FAILURE;
    }
    s.argc = argc;
    s.argv = argv;
    status = lua_cpcall(L, &pmain, &s);
    report(L, status);
    lua_close(L);
    return (status || s.status) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/** @} */ /* 结束主程序和程序入口文档组 */

