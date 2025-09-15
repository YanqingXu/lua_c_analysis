/**
 * @file luaconf.h
 * @brief Lua 5.1.5 配置文件：核心配置、平台适配和编译时选项
 *
 * 详细说明：
 * 本文件是Lua系统的核心配置文件，定义了Lua的编译时配置、平台适配、
 * 性能参数和兼容性选项。它是Lua可移植性和可配置性的基础，允许
 * 开发者根据目标平台和应用需求定制Lua的行为和性能特征。
 *
 * 系统架构定位：
 * - Lua编译系统的配置中心
 * - 跨平台适配的核心组件
 * - 性能调优的参数控制中心
 * - 兼容性管理的统一接口
 * - 内存管理和垃圾回收的配置基础
 *
 * 主要配置类别：
 * 1. 平台检测和适配：自动检测操作系统和编译器特性
 * 2. API导出控制：定义函数和数据的可见性
 * 3. 数据类型配置：核心数据类型的定义和选择
 * 4. 路径和环境：模块搜索路径和环境变量配置
 * 5. 交互式解释器：命令行界面的配置选项
 * 6. 垃圾回收器：GC算法的性能参数
 * 7. 兼容性选项：与旧版本Lua的兼容性控制
 * 8. 内存管理：内存分配和限制的配置
 * 9. 数值系统：数字类型和精度的选择
 * 10. 字符串系统：字符串处理的优化选项
 *
 * 设计理念：
 * - 可配置性：通过宏定义实现灵活的配置
 * - 可移植性：支持多种操作系统和编译器
 * - 性能优化：提供性能调优的参数接口
 * - 向后兼容：保持与旧版本的兼容性
 * - 模块化：配置选项按功能模块组织
 *
 * 配置方法：
 * - 编译时定义：通过-D编译器选项定义宏
 * - 条件编译：根据平台和编译器自动选择
 * - 环境检测：自动检测系统特性和能力
 * - 默认配置：提供合理的默认值
 *
 * 平台支持：
 * - Windows：完整的Windows API支持
 * - Linux：POSIX标准和Linux特性
 * - macOS：Darwin系统的特殊适配
 * - Unix：通用Unix系统支持
 * - 嵌入式：资源受限环境的优化
 *
 * 性能影响：
 * - 编译时配置：不影响运行时性能
 * - 内存布局：影响数据结构的内存使用
 * - 算法选择：影响核心算法的实现
 * - 优化级别：控制各种优化的启用
 *
 * 安全考虑：
 * - 功能限制：可以禁用潜在危险的功能
 * - 资源控制：限制内存和计算资源使用
 * - 沙箱支持：为安全环境提供配置选项
 * - 输入验证：配置输入数据的验证级别
 *
 * 开发和调试：
 * - 调试选项：控制调试信息的生成
 * - 断言控制：配置运行时断言的行为
 * - 错误处理：定制错误报告的详细程度
 * - 性能监控：启用性能分析功能
 *
 * 使用指南：
 * - 大多数用户使用默认配置即可
 * - 嵌入式应用可能需要调整内存限制
 * - 高性能应用可能需要优化GC参数
 * - 安全应用可能需要禁用某些功能
 *
 * 注意事项：
 * - 配置更改需要重新编译Lua
 * - 某些配置选项可能影响ABI兼容性
 * - 平台特定的配置可能不可移植
 * - 性能配置需要根据实际负载调优
 *
 * 扩展性：
 * - 支持自定义内存分配器
 * - 允许替换核心算法实现
 * - 提供钩子函数的配置接口
 * - 支持第三方库的集成
 *
 * 维护和升级：
 * - 配置选项保持向后兼容
 * - 新功能通过新的配置选项启用
 * - 废弃功能通过兼容性选项控制
 * - 文档化所有配置选项的影响
 *
 * @author Roberto Ierusalimschy, Luiz Henrique de Figueiredo, Waldemar Celes
 * @version 5.1.5
 * @date 2012
 * @since C89
 * @see lua.h, lauxlib.h, lualib.h
 */

#ifndef lconfig_h
#define lconfig_h

#include <limits.h>
#include <stddef.h>

/**
 * @name 平台检测和适配
 * @brief 自动检测操作系统和编译器特性，启用平台特定功能
 * @{
 */

/**
 * @brief ANSI C兼容性控制
 *
 * 详细说明：
 * 当定义了__STRICT_ANSI__时，自动启用LUA_ANSI模式，限制Lua只使用
 * 标准ANSI C功能，避免使用任何非标准的C库函数或系统调用。
 *
 * 影响范围：
 * - 禁用POSIX特性
 * - 禁用平台特定的优化
 * - 使用标准C库的替代实现
 * - 确保最大的可移植性
 *
 * 使用场景：
 * - 严格的ANSI C环境
 * - 嵌入式系统开发
 * - 跨平台兼容性要求
 * - 编译器兼容性测试
 *
 * @since C89
 * @see LUA_USE_POSIX
 */
#if defined(__STRICT_ANSI__)
#define LUA_ANSI
#endif

/**
 * @brief Windows平台检测
 *
 * 详细说明：
 * 在非ANSI模式下，如果检测到Windows平台（_WIN32宏），
 * 自动定义LUA_WIN宏，启用Windows特定的功能和优化。
 *
 * Windows特性：
 * - Windows API调用
 * - DLL导入/导出支持
 * - Windows路径处理
 * - 控制台I/O优化
 *
 * @since C89
 * @see LUA_BUILD_AS_DLL, LUA_API
 */
#if !defined(LUA_ANSI) && defined(_WIN32)
#define LUA_WIN
#endif

/**
 * @brief Linux平台配置
 *
 * 详细说明：
 * 当定义LUA_USE_LINUX时，启用Linux特定的功能和优化。
 * 这会自动启用POSIX支持和Linux特有的功能。
 *
 * 启用的功能：
 * - POSIX标准接口
 * - 动态库加载（dlopen）
 * - GNU Readline支持
 * - Linux特定的系统调用
 *
 * 依赖库：
 * - libdl：动态库加载
 * - libreadline：命令行编辑
 * - libhistory：命令历史
 *
 * @since C89
 * @see LUA_USE_POSIX, LUA_USE_DLOPEN, LUA_USE_READLINE
 */
#if defined(LUA_USE_LINUX)
#define LUA_USE_POSIX
#define LUA_USE_DLOPEN
#define LUA_USE_READLINE
#endif

/**
 * @brief macOS平台配置
 *
 * 详细说明：
 * 当定义LUA_USE_MACOSX时，启用macOS（Darwin）特定的功能。
 * macOS基于Unix，因此启用POSIX支持，但使用dyld而非dlopen。
 *
 * 启用的功能：
 * - POSIX标准接口
 * - dyld动态库加载
 * - macOS特定的系统调用
 * - Darwin内核特性
 *
 * 特殊处理：
 * - 使用dyld而非dlopen
 * - 不需要额外的动态库
 * - 支持Framework加载
 *
 * @since C89
 * @see LUA_USE_POSIX, LUA_DL_DYLD
 */
#if defined(LUA_USE_MACOSX)
#define LUA_USE_POSIX
#define LUA_DL_DYLD
#endif

/**
 * @brief POSIX功能启用
 *
 * 详细说明：
 * 当定义LUA_USE_POSIX时，启用所有POSIX标准功能。
 * 这包括X/Open System Interfaces Extension (XSI)中列出的功能。
 *
 * 启用的POSIX功能：
 * - mkstemp：安全的临时文件创建
 * - isatty：终端检测
 * - popen：进程管道
 * - setjmp/longjmp：非本地跳转的Unix版本
 *
 * 系统要求：
 * - 兼容POSIX.1标准的系统
 * - 支持XSI扩展
 * - Unix-like操作系统
 *
 * @since C89
 * @see LUA_USE_MKSTEMP, LUA_USE_ISATTY, LUA_USE_POPEN, LUA_USE_ULONGJMP
 */
#if defined(LUA_USE_POSIX)
#define LUA_USE_MKSTEMP
#define LUA_USE_ISATTY
#define LUA_USE_POPEN
#define LUA_USE_ULONGJMP
#endif

/** @} */

/**
 * @name 环境变量和路径配置
 * @brief 定义模块搜索路径和环境变量名称
 * @{
 */

/**
 * @brief Lua模块搜索路径环境变量名
 *
 * 详细说明：
 * 定义了Lua检查的环境变量名称，用于设置Lua模块的搜索路径。
 * 用户可以通过设置这个环境变量来自定义Lua模块的搜索位置。
 *
 * 使用方式：
 * - export LUA_PATH="/path/to/modules/?.lua;;"
 * - 路径中的"?"会被模块名替换
 * - 末尾的";;"表示追加默认路径
 *
 * @since C89
 * @see package.path, require()
 */
#define LUA_PATH        "LUA_PATH"

/**
 * @brief C模块搜索路径环境变量名
 *
 * 详细说明：
 * 定义了Lua检查的环境变量名称，用于设置C扩展模块的搜索路径。
 * 用于加载动态链接库形式的Lua扩展。
 *
 * 使用方式：
 * - export LUA_CPATH="/path/to/libs/?.so;;"
 * - Windows上使用.dll扩展名
 * - Unix/Linux上使用.so扩展名
 *
 * @since C89
 * @see package.cpath, package.loadlib()
 */
#define LUA_CPATH       "LUA_CPATH"

/**
 * @brief Lua初始化代码环境变量名
 *
 * 详细说明：
 * 定义了Lua检查的环境变量名称，用于在启动时执行初始化代码。
 * 可以包含Lua代码或以@开头的文件名。
 *
 * 使用方式：
 * - export LUA_INIT="print('Hello from init')"
 * - export LUA_INIT="@/path/to/init.lua"
 *
 * @since C89
 * @see lua_State initialization
 */
#define LUA_INIT        "LUA_INIT"

/**
 * @brief Windows平台的默认路径配置
 *
 * 详细说明：
 * Windows平台的模块搜索路径配置。使用反斜杠作为路径分隔符，
 * 支持相对于可执行文件的路径（!符号）。
 *
 * 路径组成：
 * - 当前目录：".\\?.lua"
 * - 可执行文件目录下的lua子目录
 * - 可执行文件目录
 * - 支持init.lua模块格式
 *
 * 特殊符号：
 * - "!"：被替换为可执行文件所在目录
 * - "?"：被替换为模块名
 * - "\\"：Windows路径分隔符
 *
 * @since C89
 * @see LUA_EXECDIR, LUA_PATH_MARK
 */
#if defined(_WIN32)
#define LUA_LDIR        "!\\lua\\"
#define LUA_CDIR        "!\\"
#define LUA_PATH_DEFAULT  \
    ".\\?.lua;"  LUA_LDIR"?.lua;"  LUA_LDIR"?\\init.lua;" \
                 LUA_CDIR"?.lua;"  LUA_CDIR"?\\init.lua"
#define LUA_CPATH_DEFAULT \
    ".\\?.dll;"  LUA_CDIR"?.dll;" LUA_CDIR"loadall.dll"
#else
/**
 * @brief Unix/Linux平台的默认路径配置
 *
 * 详细说明：
 * Unix/Linux平台的模块搜索路径配置。使用标准的Unix路径约定，
 * 默认安装在/usr/local/下。
 *
 * 路径组成：
 * - 当前目录："./?.lua"
 * - 系统Lua库目录：/usr/local/share/lua/5.1/
 * - 系统C库目录：/usr/local/lib/lua/5.1/
 * - 支持init.lua模块格式
 *
 * 标准位置：
 * - /usr/local/share/lua/5.1/：纯Lua模块
 * - /usr/local/lib/lua/5.1/：C扩展模块
 * - loadall.so：通用加载器
 *
 * @since C89
 * @see LUA_ROOT, FHS (Filesystem Hierarchy Standard)
 */
#define LUA_ROOT        "/usr/local/"
#define LUA_LDIR        LUA_ROOT "share/lua/5.1/"
#define LUA_CDIR        LUA_ROOT "lib/lua/5.1/"
#define LUA_PATH_DEFAULT  \
    "./?.lua;"  LUA_LDIR"?.lua;"  LUA_LDIR"?/init.lua;" \
                LUA_CDIR"?.lua;"  LUA_CDIR"?/init.lua"
#define LUA_CPATH_DEFAULT \
    "./?.so;"  LUA_CDIR"?.so;" LUA_CDIR"loadall.so"
#endif

/** @} */

/**
 * @name 路径分隔符和标记符号
 * @brief 定义路径处理中使用的特殊字符和分隔符
 * @{
 */

/**
 * @brief 目录分隔符
 *
 * 详细说明：
 * 定义了用于子模块的目录分隔符。在Windows上使用反斜杠，
 * 在其他系统上使用正斜杠。这影响模块名到文件路径的转换。
 *
 * 平台差异：
 * - Windows："\\" (反斜杠)
 * - Unix/Linux/macOS："/" (正斜杠)
 *
 * 使用场景：
 * - 模块名"a.b.c"转换为路径"a/b/c"
 * - 子模块的目录结构映射
 * - 跨平台路径兼容性
 *
 * @since C89
 * @see require(), package.searchpath()
 */
#if defined(_WIN32)
#define LUA_DIRSEP      "\\"
#else
#define LUA_DIRSEP      "/"
#endif

/**
 * @brief 路径模板分隔符
 *
 * 详细说明：
 * 用于分隔路径模板中的多个搜索路径。在搜索模块时，
 * Lua会依次尝试分号分隔的每个路径模板。
 *
 * 使用示例：
 * - "./?.lua;/usr/local/share/lua/?.lua"
 * - 先搜索当前目录，再搜索系统目录
 *
 * @since C89
 * @see LUA_PATH, LUA_CPATH
 */
#define LUA_PATHSEP     ";"

/**
 * @brief 模块名替换标记
 *
 * 详细说明：
 * 在路径模板中标记模块名的替换位置。当搜索模块时，
 * 这个标记会被实际的模块名替换。
 *
 * 使用示例：
 * - 模板："./?.lua"
 * - 模块名："mymodule"
 * - 结果："./mymodule.lua"
 *
 * @since C89
 * @see require(), package.searchpath()
 */
#define LUA_PATH_MARK   "?"

/**
 * @brief 可执行文件目录标记
 *
 * 详细说明：
 * 在Windows路径中，这个标记会被当前进程可执行文件的
 * 目录路径替换。用于创建相对于程序位置的搜索路径。
 *
 * 使用示例：
 * - 模板："!\\lua\\?.lua"
 * - 程序位置："C:\\Program Files\\MyApp\\app.exe"
 * - 结果："C:\\Program Files\\MyApp\\lua\\?.lua"
 *
 * @since C89
 * @see LUA_PATH_DEFAULT (Windows)
 */
#define LUA_EXECDIR     "!"

/**
 * @brief 忽略标记
 *
 * 详细说明：
 * 在构建luaopen_函数名时，忽略这个标记之前的所有内容。
 * 用于处理复杂的模块名和路径结构。
 *
 * 使用场景：
 * - 模块名："myapp-core.utils"
 * - 函数名：luaopen_core_utils（忽略"myapp-"部分）
 *
 * @since C89
 * @see package.loadlib(), C模块加载
 */
#define LUA_IGMARK      "-"

/** @} */

/**
 * @name 数据类型配置
 * @brief 定义Lua核心数据类型
 * @{
 */

/**
 * @brief Lua整数类型定义
 *
 * 详细说明：
 * 定义了lua_pushinteger/lua_tointeger使用的整数类型。
 * 默认使用ptrdiff_t，在大多数机器上提供了int和long之间的良好选择。
 *
 * 类型特征：
 * - ptrdiff_t：指针差值类型，通常是有符号整数
 * - 32位系统：通常是32位整数
 * - 64位系统：通常是64位整数
 * - 保证能表示数组索引的完整范围
 *
 * 选择原因：
 * - 与指针运算兼容
 * - 足够大以处理大型数据结构
 * - 在不同平台上有一致的行为
 * - 与C标准库函数兼容
 *
 * 替代选项：
 * - int：较小但可能不够大
 * - long：可能过大，浪费内存
 * - long long：C99特性，兼容性问题
 *
 * @since C89
 * @see lua_Integer, lua_pushinteger(), lua_tointeger()
 */
#define LUA_INTEGER     ptrdiff_t

/** @} */

/**
 * @name API导出控制
 * @brief 控制函数和数据的可见性和链接方式
 * @{
 */

/**
 * @brief 核心API函数导出标记
 *
 * 详细说明：
 * 定义了所有核心API函数的导出方式。支持静态链接和动态链接，
 * 在Windows上支持DLL的导入/导出。
 *
 * DLL模式（LUA_BUILD_AS_DLL定义时）：
 * - LUA_CORE或LUA_LIB定义：__declspec(dllexport)
 * - 其他情况：__declspec(dllimport)
 *
 * 静态链接模式：
 * - 所有函数标记为extern
 *
 * 使用场景：
 * - Windows DLL开发
 * - 静态库链接
 * - 符号可见性控制
 *
 * @since C89
 * @see LUALIB_API, LUAI_FUNC
 */
#if defined(LUA_BUILD_AS_DLL)
#if defined(LUA_CORE) || defined(LUA_LIB)
#define LUA_API __declspec(dllexport)
#else
#define LUA_API __declspec(dllimport)
#endif
#else
#define LUA_API         extern
#endif

/**
 * @brief 标准库API函数导出标记
 *
 * 详细说明：
 * 定义了所有标准库函数的导出方式。通常与核心API使用相同的导出策略，
 * 因为标准库经常与核心一起分发。
 *
 * @since C89
 * @see LUA_API
 */
#define LUALIB_API      LUA_API

/**
 * @brief 内部函数和数据导出控制
 *
 * 详细说明：
 * 控制不应导出到外部模块的内部函数和数据的可见性。
 * 使用编译器特定的特性来优化符号访问和减少符号表大小。
 *
 * 编译模式：
 * - luaall_c定义：所有函数为static，数据为空
 * - GCC 3.2+且ELF格式：使用hidden可见性
 * - 其他情况：标准extern声明
 *
 * 优化效果：
 * - 减少符号表大小
 * - 提高动态链接性能
 * - 防止符号冲突
 * - 启用编译器优化
 *
 * @since C89
 * @see __attribute__((visibility("hidden")))
 */
#if defined(luaall_c)
#define LUAI_FUNC       static
#define LUAI_DATA
#elif defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 302) && \
      defined(__ELF__)
#define LUAI_FUNC       __attribute__((visibility("hidden"))) extern
#define LUAI_DATA       LUAI_FUNC
#else
#define LUAI_FUNC       extern
#define LUAI_DATA       extern
#endif

/** @} */
/**
 * @name 错误消息格式化
 * @brief 控制错误消息中程序元素的引用格式
 * @{
 */

/**
 * @brief 程序元素引用格式
 *
 * 详细说明：
 * 定义了错误消息中如何引用程序元素（如变量名、函数名等）。
 * 默认使用单引号包围，提供清晰的视觉分隔。
 *
 * 使用示例：
 * - LUA_QL("myvar") -> "'myvar'"
 * - 错误消息："attempt to call 'myvar' (a nil value)"
 *
 * 自定义格式：
 * - 可以修改为其他格式，如双引号、括号等
 * - 影响所有错误消息的一致性
 *
 * @param x 要引用的程序元素名称
 * @since C89
 * @see LUA_QS, luaL_error()
 */
#define LUA_QL(x)       "'" x "'"

/**
 * @brief 字符串格式的程序元素引用
 *
 * 详细说明：
 * 用于printf风格格式化中的字符串参数引用。
 * 结合LUA_QL和%s格式说明符。
 *
 * 使用示例：
 * - luaL_error(L, "bad argument " LUA_QS, name)
 * - 结果："bad argument 'myfunction'"
 *
 * @since C89
 * @see LUA_QL, printf格式化
 */
#define LUA_QS          LUA_QL("%s")

/** @} */

/**
 * @name 调试信息配置
 * @brief 控制调试信息的大小和格式
 * @{
 */

/**
 * @brief 函数源码描述的最大长度
 *
 * 详细说明：
 * 定义了调试信息中函数源码描述的最大字符数。
 * 这影响错误消息和调试输出中源码位置信息的详细程度。
 *
 * 包含信息：
 * - 文件名或代码块描述
 * - 函数定义位置
 * - 源码片段标识
 *
 * 内存影响：
 * - 每个函数的调试信息占用
 * - 错误消息的长度
 * - 调试器显示的详细程度
 *
 * 调整建议：
 * - 嵌入式系统：可以减小以节省内存
 * - 开发环境：可以增大以获得更多信息
 * - 生产环境：平衡内存使用和调试需求
 *
 * @since C89
 * @see debug.getinfo(), lua_Debug结构体
 */
#define LUA_IDSIZE      60

/** @} */

/**
 * @name 交互式解释器配置
 * @brief 独立Lua解释器的配置选项
 * @{
 */

#if defined(lua_c) || defined(luaall_c)

/**
 * @brief 终端类型检测
 *
 * 详细说明：
 * 检测标准输入是否为终端（tty），用于确定是否运行在交互模式下。
 * 这影响Lua解释器的行为，如是否显示提示符、是否启用行编辑等。
 *
 * 平台实现：
 * - POSIX系统：使用isatty(0)检测stdin
 * - Windows：使用_isatty(_fileno(stdin))
 * - 其他系统：假设总是终端（返回1）
 *
 * 使用场景：
 * - 交互式vs批处理模式检测
 * - 提示符显示控制
 * - 行编辑功能启用
 * - 错误输出格式调整
 *
 * @return 是否为终端
 * @retval 1 标准输入是终端
 * @retval 0 标准输入不是终端（如管道、文件）
 *
 * @since C89
 * @see isatty(), _isatty()
 */
#if defined(LUA_USE_ISATTY)
#include <unistd.h>
#define lua_stdin_is_tty()      isatty(0)
#elif defined(LUA_WIN)
#include <io.h>
#include <stdio.h>
#define lua_stdin_is_tty()      _isatty(_fileno(stdin))
#else
#define lua_stdin_is_tty()      1
#endif

/**
 * @brief 主提示符
 *
 * 详细说明：
 * 定义了交互式Lua解释器的主提示符。在等待新的Lua语句时显示。
 * 可以通过修改全局变量_PROMPT在运行时动态改变。
 *
 * 默认值："> "
 *
 * 自定义示例：
 * - _PROMPT = "lua> "
 * - _PROMPT = "[" .. os.date("%H:%M") .. "] "
 *
 * @since C89
 * @see LUA_PROMPT2, _PROMPT全局变量
 */
#define LUA_PROMPT              "> "

/**
 * @brief 续行提示符
 *
 * 详细说明：
 * 定义了交互式Lua解释器的续行提示符。当输入的语句不完整，
 * 需要继续输入时显示。
 *
 * 使用场景：
 * - 多行函数定义
 * - 长表达式
 * - 未闭合的括号、引号
 * - 控制结构（if、for、while等）
 *
 * 默认值：">> "
 *
 * @since C89
 * @see LUA_PROMPT, _PROMPT2全局变量
 */
#define LUA_PROMPT2             ">> "

/**
 * @brief 程序名称
 *
 * 详细说明：
 * 定义了独立Lua解释器的默认程序名称。用于错误消息、
 * 使用说明和系统无法自动检测程序名时的回退值。
 *
 * 使用场景：
 * - 错误消息中的程序标识
 * - 命令行帮助信息
 * - 进程名称显示
 *
 * @since C89
 * @see argv[0], 程序名称检测
 */
#define LUA_PROGNAME            "lua"

/**
 * @brief 输入行最大长度
 *
 * 详细说明：
 * 定义了交互式解释器中单行输入的最大字符数。
 * 超过此长度的输入行会被截断或拒绝。
 *
 * 内存影响：
 * - 输入缓冲区大小
 * - 栈空间使用
 * - 临时字符串分配
 *
 * 调整建议：
 * - 嵌入式系统：可以减小以节省内存
 * - 开发环境：可以增大以支持长表达式
 * - 一般应用：512字节通常足够
 *
 * @since C89
 * @see fgets(), 输入缓冲区管理
 */
#define LUA_MAXINPUT            512

/**
 * @brief GNU Readline支持
 *
 * 详细说明：
 * 当启用GNU Readline时，提供高级的行编辑功能，包括：
 * - 命令历史记录
 * - 行编辑（光标移动、删除、插入）
 * - 自动补全（如果配置）
 * - Emacs/Vi键绑定
 *
 * 依赖库：
 * - libreadline：核心行编辑功能
 * - libhistory：命令历史管理
 *
 * 功能对比：
 * - 有Readline：高级编辑功能
 * - 无Readline：基本输入，无历史记录
 *
 * @since C89
 * @see readline(), add_history(), GNU Readline库
 */
#if defined(LUA_USE_READLINE)
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

/**
 * @brief 读取输入行（Readline版本）
 *
 * 使用GNU Readline读取用户输入，支持行编辑和历史记录。
 *
 * @param L Lua状态机（未使用）
 * @param b 输出缓冲区指针
 * @param p 提示符字符串
 * @return 是否成功读取
 */
#define lua_readline(L,b,p)     ((void)L, ((b)=readline(p)) != NULL)

/**
 * @brief 保存输入行到历史（Readline版本）
 *
 * 将非空的输入行添加到命令历史中，供后续回调使用。
 *
 * @param L Lua状态机
 * @param idx 输入行在栈中的索引
 */
#define lua_saveline(L,idx) \
    if (lua_strlen(L,idx) > 0) \
      add_history(lua_tostring(L, idx));

/**
 * @brief 释放输入行内存（Readline版本）
 *
 * 释放由readline()分配的内存。
 *
 * @param L Lua状态机（未使用）
 * @param b 要释放的缓冲区
 */
#define lua_freeline(L,b)       ((void)L, free(b))
#else
/**
 * @brief 读取输入行（标准版本）
 *
 * 使用标准C库函数读取用户输入，无行编辑功能。
 *
 * @param L Lua状态机（未使用）
 * @param b 输入缓冲区
 * @param p 提示符字符串
 * @return 是否成功读取
 */
#define lua_readline(L,b,p) \
    ((void)L, fputs(p, stdout), fflush(stdout), \
    fgets(b, LUA_MAXINPUT, stdin) != NULL)

/**
 * @brief 保存输入行到历史（标准版本）
 *
 * 标准版本不支持历史记录，此宏为空操作。
 *
 * @param L Lua状态机（未使用）
 * @param idx 输入行索引（未使用）
 */
#define lua_saveline(L,idx)     { (void)L; (void)idx; }

/**
 * @brief 释放输入行内存（标准版本）
 *
 * 标准版本使用栈分配的缓冲区，无需释放。
 *
 * @param L Lua状态机（未使用）
 * @param b 缓冲区（未使用）
 */
#define lua_freeline(L,b)       { (void)L; (void)b; }
#endif

#endif

/** @} */

/**
 * @name 垃圾回收器配置
 * @brief 控制垃圾回收器的性能参数
 * @{
 */

/**
 * @brief 垃圾回收暂停百分比
 *
 * 详细说明：
 * 定义了垃圾回收周期之间的默认暂停时间，以百分比表示。
 * 这个值控制GC的触发频率，影响内存使用和性能的平衡。
 *
 * 工作原理：
 * - 200%表示等待内存使用量翻倍后才开始下次GC
 * - 值越大，GC频率越低，内存使用越高
 * - 值越小，GC频率越高，CPU使用越高
 *
 * 性能影响：
 * - 高值（如300%）：更少的GC暂停，但更高的内存峰值
 * - 低值（如100%）：更频繁的GC，但更低的内存使用
 * - 默认200%：在性能和内存间的良好平衡
 *
 * 动态调整：
 * - 可以通过collectgarbage("setpause", value)运行时修改
 * - 可以根据应用特性进行调优
 * - 内存受限环境可能需要更低的值
 *
 * 使用场景：
 * - 游戏：可能需要更高的值以减少暂停
 * - 服务器：可能需要更低的值以控制内存
 * - 嵌入式：通常需要更低的值
 *
 * @since C89
 * @see collectgarbage(), lua_gc(), LUAI_GCMUL
 */
#define LUAI_GCPAUSE            200

/**
 * @brief 垃圾回收速度倍数
 *
 * 详细说明：
 * 定义了垃圾回收相对于内存分配的默认速度，以百分比表示。
 * 这个值控制每次内存分配时GC工作的量，影响GC的粒度。
 *
 * 工作原理：
 * - 200%表示GC以内存分配速度的2倍运行
 * - 值越大，每次分配时GC工作越多
 * - 值越小，GC工作分散到更多的分配操作中
 * - 0表示无限大，每步执行完整的GC周期
 *
 * 性能特征：
 * - 高值（如400%）：更粗粒度的GC，可能有明显暂停
 * - 低值（如100%）：更细粒度的GC，暂停时间更短
 * - 默认200%：在暂停时间和总开销间平衡
 *
 * 实时性考虑：
 * - 实时应用可能需要更高的值以减少暂停频率
 * - 交互应用可能需要更低的值以保持响应性
 * - 批处理应用对此参数不太敏感
 *
 * 动态调整：
 * - 可以通过collectgarbage("setstepmul", value)运行时修改
 * - 可以根据负载特性动态调整
 * - 可以在关键代码段临时调整
 *
 * 调优建议：
 * - 监控GC暂停时间和频率
 * - 根据应用的内存分配模式调整
 * - 考虑用户体验和性能要求
 * - 在不同负载下测试效果
 *
 * @since C89
 * @see collectgarbage(), lua_gc(), LUAI_GCPAUSE
 */
#define LUAI_GCMUL              200

/** @} */

/**
 * @name 兼容性选项
 * @brief 控制与旧版本Lua的兼容性
 * @{
 */

/**
 * @brief 禁用getn/setn兼容性
 *
 * 详细说明：
 * 控制与Lua 5.0中getn/setn行为的兼容性。在Lua 5.1中，
 * 表的长度通过#操作符或lua_objlen获取，不再需要getn/setn。
 *
 * 影响：
 * - 未定义：不提供table.getn/table.setn函数
 * - 定义：提供向后兼容的getn/setn函数
 *
 * 迁移建议：
 * - 使用#操作符替代table.getn
 * - 移除table.setn调用（在5.1中无效果）
 *
 * @since C89
 * @see table.getn, table.setn, #操作符
 */
#undef LUA_COMPAT_GETN

/**
 * @brief 禁用全局loadlib兼容性
 *
 * 详细说明：
 * 控制是否提供全局loadlib函数。在Lua 5.1中，
 * 此功能已移至package.loadlib。
 *
 * 影响：
 * - 未定义：不提供全局loadlib函数
 * - 定义：提供全局loadlib函数作为package.loadlib的别名
 *
 * 迁移建议：
 * - 使用package.loadlib替代全局loadlib
 * - 更新代码以使用新的包管理系统
 *
 * @since C89
 * @see package.loadlib, 动态库加载
 */
#undef LUA_COMPAT_LOADLIB

/**
 * @brief 启用vararg兼容性
 *
 * 详细说明：
 * 控制对旧式可变参数功能的兼容性。在Lua 5.0中，
 * 可变参数通过arg表访问，5.1引入了...语法。
 *
 * 影响：
 * - 定义：支持旧的arg表访问方式
 * - 未定义：只支持新的...语法
 *
 * 迁移建议：
 * - 使用...替代arg表
 * - 使用select()函数处理可变参数
 *
 * @since C89
 * @see ..., select(), arg表
 */
#define LUA_COMPAT_VARARG

/**
 * @brief 启用math.mod兼容性
 *
 * 详细说明：
 * 控制对旧的math.mod函数的兼容性。在Lua 5.1中，
 * 引入了%操作符和math.fmod函数。
 *
 * 影响：
 * - 定义：提供math.mod函数
 * - 未定义：只提供%操作符和math.fmod
 *
 * 迁移建议：
 * - 使用%操作符替代math.mod
 * - 使用math.fmod进行浮点数取模
 *
 * @since C89
 * @see %操作符, math.fmod, math.mod
 */
#define LUA_COMPAT_MOD

/**
 * @brief 长字符串嵌套兼容性级别
 *
 * 详细说明：
 * 控制对长字符串嵌套的兼容性处理。在Lua 5.1中，
 * 改进了长字符串的嵌套规则。
 *
 * 值含义：
 * - 1：提供兼容性警告
 * - 2：完全兼容旧行为
 * - 未定义：关闭兼容性检查
 *
 * 影响：
 * - 嵌套[[...]]字符串的处理
 * - 错误消息和警告的生成
 *
 * @since C89
 * @see 长字符串语法, [[...]]
 */
#define LUA_COMPAT_LSTR         1

/**
 * @brief 启用string.gfind兼容性
 *
 * 详细说明：
 * 控制对旧的string.gfind函数名的兼容性。
 * 在Lua 5.1中，此函数重命名为string.gmatch。
 *
 * 影响：
 * - 定义：提供string.gfind作为string.gmatch的别名
 * - 未定义：只提供string.gmatch
 *
 * 迁移建议：
 * - 使用string.gmatch替代string.gfind
 * - 更新所有相关代码
 *
 * @since C89
 * @see string.gmatch, string.gfind
 */
#define LUA_COMPAT_GFIND

/** @} */

/**
 * @brief 启用luaL_openlib兼容性
 *
 * 详细说明：
 * 控制对旧的luaL_openlib函数行为的兼容性。
 * 在Lua 5.1中，推荐使用luaL_register替代luaL_openlib。
 *
 * 影响：
 * - 定义：保持luaL_openlib的旧行为
 * - 未定义：使用新的luaL_register行为
 *
 * 迁移建议：
 * - 将luaL_openlib调用替换为luaL_register
 * - 更新库注册代码以使用新API
 *
 * @since C89
 * @see luaL_openlib(), luaL_register()
 */
#define LUA_COMPAT_OPENLIB

/**
 * @name 调试和检查配置
 * @brief 控制调试检查和断言的行为
 * @{
 */

/**
 * @brief Lua C API断言宏
 *
 * 详细说明：
 * 定义了Lua C API中使用的断言宏。用于在调试时检查
 * API调用的参数有效性，可以帮助发现C代码中的错误。
 *
 * 调试模式（LUA_USE_APICHECK定义时）：
 * - 启用完整的参数检查
 * - 使用标准assert宏
 * - 参数错误时程序终止
 *
 * 发布模式（默认）：
 * - 禁用所有检查
 * - 零运行时开销
 * - 最大性能
 *
 * 性能影响：
 * - 调试模式：显著的性能开销
 * - 发布模式：无性能影响
 * - 建议只在开发阶段启用
 *
 * 使用场景：
 * - C扩展开发和调试
 * - API使用错误检测
 * - 参数有效性验证
 * - 内存安全检查
 *
 * @param L Lua状态机指针
 * @param o 要检查的条件
 *
 * @since C89
 * @see assert(), LUA_USE_APICHECK
 */
#if defined(LUA_USE_APICHECK)
#include <assert.h>
#define luai_apicheck(L,o)      { (void)L; assert(o); }
#else
#define luai_apicheck(L,o)      { (void)L; }
#endif

/** @} */

/**
 * @name 平台特性检测
 * @brief 自动检测平台的数值特性
 * @{
 */

/**
 * @brief int类型的位数
 *
 * 详细说明：
 * 定义了int类型包含的位数。Lua会自动检测机器的int大小，
 * 但在某些特殊平台上可能需要手动指定。
 *
 * 检测逻辑：
 * - INT_MAX-20 < 32760：16位int
 * - INT_MAX > 2147483640L：至少32位int
 * - 其他情况：使用默认值
 *
 * 影响范围：
 * - 数值转换的精度
 * - 位操作的行为
 * - 内存布局优化
 * - 跨平台兼容性
 *
 * 平台示例：
 * - 16位系统：LUAI_BITSINT = 16
 * - 32位系统：LUAI_BITSINT = 32
 * - 64位系统：LUAI_BITSINT = 32（int通常仍是32位）
 *
 * @since C89
 * @see INT_MAX, limits.h
 */
#if INT_MAX-20 < 32760
#define LUAI_BITSINT            16
#elif INT_MAX > 2147483640L
#define LUAI_BITSINT            32
#else
#error "you must define LUA_BITSINT with number of bits in an integer"
#endif

/**
 * @brief 32位整数类型定义
 *
 * 详细说明：
 * 定义了Lua内部使用的32位整数类型。根据平台的int大小，
 * 选择最合适的C类型来表示32位整数。
 *
 * 32位平台（LUAI_BITSINT >= 32）：
 * - LUAI_UINT32: unsigned int
 * - LUAI_INT32: int
 * - LUAI_MAXINT32: INT_MAX
 *
 * 16位平台：
 * - LUAI_UINT32: unsigned long
 * - LUAI_INT32: long
 * - LUAI_MAXINT32: LONG_MAX
 *
 * 内存类型：
 * - LUAI_UMEM: 无符号内存大小类型
 * - LUAI_MEM: 有符号内存大小类型
 *
 * 设计考虑：
 * - 保证至少32位的精度
 * - 在64位系统上避免不必要的空间浪费
 * - 与标准C类型保持兼容
 * - 支持内存大小的完整范围
 *
 * @since C89
 * @see size_t, ptrdiff_t
 */
#if LUAI_BITSINT >= 32
#define LUAI_UINT32             unsigned int
#define LUAI_INT32              int
#define LUAI_MAXINT32           INT_MAX
#define LUAI_UMEM               size_t
#define LUAI_MEM                ptrdiff_t
#else
#define LUAI_UINT32             unsigned long
#define LUAI_INT32              long
#define LUAI_MAXINT32           LONG_MAX
#define LUAI_UMEM               unsigned long
#define LUAI_MEM                long
#endif

/** @} */

/**
 * @name 运行时限制参数
 * @brief 控制Lua运行时的各种限制
 * @{
 */

/**
 * @brief 最大嵌套调用深度
 *
 * 详细说明：
 * 限制Lua函数的最大嵌套调用深度。这是一个安全限制，
 * 用于防止无限递归导致的栈溢出和内存耗尽。
 *
 * 限制目的：
 * - 防止无限递归
 * - 保护C栈不溢出
 * - 避免内存耗尽
 * - 提供可预测的行为
 *
 * 调整考虑：
 * - 深度递归算法可能需要更大的值
 * - 嵌入式系统可能需要更小的值
 * - 默认值20000对大多数应用足够
 *
 * 错误处理：
 * - 超过限制时抛出"stack overflow"错误
 * - 错误可以被pcall捕获
 * - 提供清晰的错误信息
 *
 * @since C89
 * @see lua_call(), pcall(), 递归限制
 */
#define LUAI_MAXCALLS           20000

/**
 * @brief C函数可使用的最大Lua栈槽数
 *
 * 详细说明：
 * 限制单个C函数可以使用的Lua栈空间大小。这防止C函数
 * 消耗过多的栈空间，保护系统稳定性。
 *
 * 限制范围：
 * - 必须小于-LUA_REGISTRYINDEX
 * - 影响lua_checkstack的行为
 * - 控制C函数的栈使用
 *
 * 使用场景：
 * - 大量局部变量的C函数
 * - 复杂数据结构的构建
 * - 批量数据处理
 *
 * 调整建议：
 * - 需要大量栈空间的C函数可能需要更大的值
 * - 内存受限环境可能需要更小的值
 * - 默认值8000对大多数情况足够
 *
 * 错误处理：
 * - 超过限制时lua_checkstack失败
 * - 可以通过返回值检测
 * - 提供适当的错误处理
 *
 * @since C89
 * @see lua_checkstack(), LUA_REGISTRYINDEX
 */
#define LUAI_MAXCSTACK          8000

/** @} */



/*
** {==================================================================
** CHANGE (to smaller values) the following definitions if your system
** has a small C stack. (Or you may want to change them to larger
** values if your system has a large C stack and these limits are
** too rigid for you.) Some of these constants control the size of
** stack-allocated arrays used by the compiler or the interpreter, while
** others limit the maximum number of recursive calls that the compiler
** or the interpreter can perform. Values too large may cause a C stack
** overflow for some forms of deep constructs.
** ===================================================================
*/


/**
 * @brief C调用和语法嵌套的最大深度
 *
 * 详细说明：
 * 定义了嵌套C调用和程序中语法嵌套非终结符的最大深度。
 * 这是一个重要的安全限制，防止深度嵌套导致的栈溢出。
 *
 * 限制范围：
 * - C函数的嵌套调用深度
 * - 语法分析中的嵌套结构深度
 * - 表达式的嵌套层次
 * - 控制结构的嵌套层次
 *
 * 安全考虑：
 * - 防止C栈溢出
 * - 避免无限递归
 * - 保护解析器稳定性
 * - 提供可预测的行为
 *
 * 调整建议：
 * - 复杂表达式较多：可以适当增大
 * - 嵌入式环境：可能需要减小
 * - 默认200对大多数应用足够
 *
 * 影响场景：
 * - 深度嵌套的函数调用
 * - 复杂的表达式计算
 * - 多层嵌套的控制结构
 * - 递归算法的实现
 *
 * @since C89
 * @see LUAI_MAXCALLS, C栈管理, 语法分析
 */
#define LUAI_MAXCCALLS          200


/**
 * @brief 函数最大局部变量数
 *
 * 详细说明：
 * 定义了单个函数可以拥有的最大局部变量数量。
 * 这个限制必须小于250，以确保内部编码的正确性。
 *
 * 限制原因：
 * - Lua使用单字节编码变量索引
 * - 保留部分编码空间给特殊用途
 * - 防止过度的内存使用
 * - 维护合理的编译性能
 *
 * 调整考虑：
 * - 增大值：支持更复杂的函数，但增加内存使用
 * - 减小值：节省内存，但限制函数复杂度
 * - 默认200对大多数应用足够
 *
 * @since C89
 * @see 局部变量, 函数编译
 */
#define LUAI_MAXVARS            200

/**
 * @brief 函数最大上值数
 *
 * 详细说明：
 * 定义了单个函数可以访问的最大上值（upvalue）数量。
 * 上值是闭包中引用的外部局部变量。
 *
 * 限制原因：
 * - 与局部变量共享编码空间
 * - 控制闭包的内存开销
 * - 维护合理的访问性能
 * - 防止过度复杂的闭包结构
 *
 * 使用场景：
 * - 闭包函数的外部变量引用
 * - 嵌套函数的变量捕获
 * - 函数式编程模式
 *
 * @since C89
 * @see 上值, 闭包, 嵌套函数
 */
#define LUAI_MAXUPVALUES        60

/**
 * @brief 辅助库缓冲区大小
 *
 * 详细说明：
 * 定义了lauxlib缓冲区系统使用的缓冲区大小。
 * 默认使用系统的BUFSIZ，通常针对I/O操作优化。
 *
 * 影响范围：
 * - luaL_Buffer的内部缓冲区大小
 * - 字符串构建的性能
 * - 内存使用模式
 * - I/O操作的效率
 *
 * 调整建议：
 * - 大量字符串操作：可以增大
 * - 内存受限环境：可以减小
 * - 默认BUFSIZ通常是最优选择
 *
 * @since C89
 * @see luaL_Buffer, BUFSIZ, 字符串缓冲区
 */
#define LUAL_BUFFERSIZE         BUFSIZ

/* }================================================================== */




/**
 * @name 数值系统配置
 * @brief 定义Lua中数字的类型和操作
 * @{
 */

/**
 * @brief Lua数字类型定义
 *
 * 详细说明：
 * 定义了Lua中数字的基础类型。默认使用double类型，
 * 提供IEEE 754双精度浮点数的精度和范围。
 *
 * 类型特征：
 * - LUA_NUMBER_DOUBLE：标识使用double类型
 * - LUA_NUMBER：实际的C类型定义
 * - 64位IEEE 754双精度浮点数
 * - 约15-17位十进制精度
 * - 范围：±1.7E±308
 *
 * 替代选项：
 * - float：较小内存占用，但精度降低
 * - long double：更高精度，但可移植性差
 * - 自定义数值类型：需要修改相关转换函数
 *
 * 修改影响：
 * - 需要同时修改lua_number2int和lua_number2integer
 * - 影响数值精度和范围
 * - 可能影响性能和内存使用
 * - 需要测试数值运算的正确性
 *
 * @since C89
 * @see lua_Number, lua_pushnumber(), lua_tonumber()
 */
#define LUA_NUMBER_DOUBLE
#define LUA_NUMBER              double

/**
 * @brief 常规参数转换后的数字类型
 *
 * 详细说明：
 * 定义了数字经过"常规参数转换"(usual argument conversion)后的类型。
 * 在C语言中，float参数会自动提升为double。
 *
 * 用途：
 * - 函数参数传递时的类型
 * - 可变参数函数中的数字类型
 * - 确保类型一致性
 *
 * @since C89
 * @see C语言类型提升规则
 */
#define LUAI_UACNUMBER          double

/**
 * @brief 数字格式化和转换配置
 *
 * 详细说明：
 * 定义了数字与字符串之间转换的格式和函数。
 * 这些配置影响数字的输入输出格式和精度。
 *
 * 格式说明：
 * - LUA_NUMBER_SCAN: "%lf" - 读取数字的scanf格式
 * - LUA_NUMBER_FMT: "%.14g" - 输出数字的printf格式
 * - 14位精度确保double的完整表示
 * - g格式自动选择最佳表示方式
 *
 * 转换函数：
 * - lua_number2str: 数字转字符串，使用sprintf
 * - lua_str2number: 字符串转数字，使用strtod
 * - LUAI_MAXNUMBER2STR: 转换缓冲区的最大大小
 *
 * 缓冲区大小计算：
 * - 16位数字 + 符号 + 小数点 + 指数 + 终止符
 * - 32字节足够容纳最长的数字表示
 *
 * @since C89
 * @see sprintf(), strtod(), printf格式化
 */
#define LUA_NUMBER_SCAN         "%lf"
#define LUA_NUMBER_FMT          "%.14g"
#define lua_number2str(s,n)     sprintf((s), LUA_NUMBER_FMT, (n))
#define LUAI_MAXNUMBER2STR      32
#define lua_str2number(s,p)     strtod((s), (p))

/**
 * @brief 数字基本运算操作
 *
 * 详细说明：
 * 定义了Lua数字的基本算术和比较操作。这些宏只在LUA_CORE
 * 编译时定义，用于Lua内核的数值计算。
 *
 * 算术运算：
 * - luai_numadd: 加法运算
 * - luai_numsub: 减法运算
 * - luai_nummul: 乘法运算
 * - luai_numdiv: 除法运算
 * - luai_nummod: 取模运算（使用floor除法）
 * - luai_numpow: 幂运算（使用pow函数）
 * - luai_numunm: 负号运算
 *
 * 比较运算：
 * - luai_numeq: 相等比较
 * - luai_numlt: 小于比较
 * - luai_numle: 小于等于比较
 * - luai_numisnan: NaN检测
 *
 * 特殊处理：
 * - 取模运算使用floor除法，确保结果符号与除数一致
 * - NaN检测利用NaN不等于自身的特性
 * - 所有运算都基于IEEE 754标准
 *
 * 性能考虑：
 * - 大多数运算直接映射到C操作符
 * - pow函数可能较慢，但提供完整的幂运算
 * - floor函数用于取模，确保数学正确性
 *
 * @since C89
 * @see math.h, IEEE 754标准, 浮点运算
 */
#if defined(LUA_CORE)
#include <math.h>
#define luai_numadd(a,b)        ((a)+(b))
#define luai_numsub(a,b)        ((a)-(b))
#define luai_nummul(a,b)        ((a)*(b))
#define luai_numdiv(a,b)        ((a)/(b))
#define luai_nummod(a,b)        ((a) - floor((a)/(b))*(b))
#define luai_numpow(a,b)        (pow(a,b))
#define luai_numunm(a)          (-(a))
#define luai_numeq(a,b)         ((a)==(b))
#define luai_numlt(a,b)         ((a)<(b))
#define luai_numle(a,b)         ((a)<=(b))
#define luai_numisnan(a)        (!luai_numeq((a), (a)))
#endif

/** @} */


/**
 * @name 数字转换优化
 * @brief 高性能的数字到整数转换
 * @{
 */

/**
 * @brief 数字到整数的转换宏
 *
 * 详细说明：
 * 定义了将lua_Number转换为int和lua_Integer的宏。
 * 这些转换在Lua中频繁使用，因此性能优化非常重要。
 *
 * 性能问题：
 * - 在Pentium处理器上，简单的double到int类型转换极其缓慢
 * - 标准C转换需要改变FPU舍入模式，开销很大
 * - 任何替代方案都值得尝试
 *
 * 优化策略：
 * - 使用平台特定的优化技术
 * - 避免FPU模式切换
 * - 利用硬件特性加速转换
 *
 * 转换要求：
 * - 支持任何舍入方法
 * - 不抛出异常或错误
 * - 保持数值转换的正确性
 *
 * @since C89
 * @see lua_Number, lua_Integer, 性能优化
 */

/**
 * @brief Pentium处理器优化版本
 *
 * 详细说明：
 * 在老式Pentium处理器上使用特殊技巧加速转换。
 * 仅在特定条件下启用：double类型、非ANSI模式、非SSE2、x86架构。
 *
 * 启用条件：
 * - LUA_NUMBER_DOUBLE已定义
 * - 非LUA_ANSI模式
 * - 非__SSE2__环境
 * - x86架构（__i386、_M_IX86、__i386__）
 *
 * Microsoft编译器版本：
 * - 使用内联汇编指令
 * - fld: 加载double到FPU栈
 * - fistp: 转换并弹出到整数
 * - 直接利用FPU的转换指令
 *
 * 通用Pentium版本：
 * - 使用"魔数"技巧：6755399441055744.0
 * - 利用IEEE 754双精度格式的特性
 * - 通过联合体进行位级转换
 * - volatile防止编译器优化
 *
 * 注意事项：
 * - 可能与DirectX的某些特性冲突
 * - 仅适用于特定的数值范围
 * - 依赖于IEEE 754格式的实现细节
 *
 * @since C89
 * @see IEEE 754, FPU指令, 内联汇编
 */
#if defined(LUA_NUMBER_DOUBLE) && !defined(LUA_ANSI) && !defined(__SSE2__) && \
    (defined(__i386) || defined (_M_IX86) || defined(__i386__))

#if defined(_MSC_VER)
#define lua_number2int(i,d)     __asm fld d   __asm fistp i
#define lua_number2integer(i,n) lua_number2int(i, n)
#else
union luai_Cast { double l_d; long l_l; };
#define lua_number2int(i,d) \
  { volatile union luai_Cast u; u.l_d = (d) + 6755399441055744.0; (i) = u.l_l; }
#define lua_number2integer(i,n) lua_number2int(i, n)
#endif

#else
/**
 * @brief 通用转换版本
 *
 * 详细说明：
 * 使用标准C类型转换的通用版本。虽然在某些平台上可能较慢，
 * 但保证在所有系统上都能正确工作。
 *
 * 特点：
 * - 完全可移植
 * - 符合C标准
 * - 在现代处理器上性能可接受
 * - 不依赖平台特定特性
 *
 * 使用场景：
 * - 非x86架构
 * - 现代处理器（支持SSE2）
 * - ANSI C兼容模式
 * - 不使用double的配置
 *
 * @since C89
 * @see C类型转换, 可移植性
 */
#define lua_number2int(i,d)     ((i)=(int)(d))
#define lua_number2integer(i,d) ((i)=(lua_Integer)(d))
#endif

/** @} */

/**
 * @name 内存对齐配置
 * @brief 定义最大对齐要求的类型
 * @{
 */

/**
 * @brief 用户定义的最大对齐类型
 *
 * 详细说明：
 * 定义了一个联合体类型，包含需要最大对齐的数据类型。
 * 这确保Lua的内存分配满足所有数据类型的对齐要求。
 *
 * 包含的类型：
 * - double: 通常需要8字节对齐
 * - void*: 指针对齐，通常4或8字节
 * - long: 长整数对齐，通常4或8字节
 *
 * 对齐重要性：
 * - 确保数据访问的正确性
 * - 避免对齐错误导致的崩溃
 * - 优化内存访问性能
 * - 满足硬件对齐要求
 *
 * 自定义建议：
 * - 如果系统支持long double且需要16字节对齐，应添加到联合体中
 * - 某些SIMD类型可能需要更大的对齐
 * - 大多数系统不需要修改此定义
 *
 * 使用场景：
 * - 内存分配器的对齐计算
 * - 数据结构的内存布局
 * - 跨平台兼容性保证
 *
 * @since C89
 * @see 内存对齐, 数据结构布局, SIMD指令
 */
#define LUAI_USER_ALIGNMENT_T   union { double u; void *s; long l; }


/**
 * @name 异常处理机制
 * @brief 定义Lua的错误处理和异常机制
 * @{
 */

/**
 * @brief Lua异常处理配置
 *
 * 详细说明：
 * 定义了Lua如何处理异常和错误。根据编译环境选择最合适的机制：
 * C++环境使用异常，Unix环境可选择高效的_longjmp，其他情况使用标准longjmp。
 *
 * 异常处理策略：
 * - C++编译：使用C++异常机制
 * - Unix优化：使用_longjmp/_setjmp（更高效）
 * - 标准模式：使用longjmp/setjmp
 *
 * 自定义选项：
 * - 可以强制在C++中使用longjmp/setjmp
 * - 可以选择是否使用_longjmp/_setjmp
 * - 根据性能需求和兼容性要求调整
 *
 * @since C89
 * @see 错误处理, 异常安全, longjmp/setjmp
 */

/**
 * @brief C++异常处理版本
 *
 * 详细说明：
 * 在C++环境中使用标准的异常处理机制。
 * 提供了与C++异常模型的完整集成。
 *
 * 实现细节：
 * - LUAI_THROW: 抛出C++异常
 * - LUAI_TRY: 使用try-catch捕获异常
 * - luai_jmpbuf: 虚拟变量，不需要实际缓冲区
 *
 * 异常处理：
 * - 捕获所有类型的异常
 * - 自动设置错误状态
 * - 与C++异常安全代码兼容
 *
 * @since C++98
 * @see C++异常, try-catch, 异常安全
 */
#if defined(__cplusplus)
#define LUAI_THROW(L,c)         throw(c)
#define LUAI_TRY(L,c,a)         try { a } catch(...) \
    { if ((c)->status == 0) (c)->status = -1; }
#define luai_jmpbuf             int

/**
 * @brief Unix优化版本
 *
 * 详细说明：
 * 在Unix系统中使用_longjmp/_setjmp，这比标准版本更高效。
 * 不保存和恢复信号掩码，减少系统调用开销。
 *
 * 性能优势：
 * - 避免信号掩码的保存和恢复
 * - 减少系统调用次数
 * - 更快的上下文切换
 *
 * 使用条件：
 * - 定义了LUA_USE_ULONGJMP
 * - Unix或类Unix系统
 * - 不需要信号掩码保护
 *
 * @since POSIX.1
 * @see _longjmp, _setjmp, 信号处理
 */
#elif defined(LUA_USE_ULONGJMP)
#define LUAI_THROW(L,c)         _longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)         if (_setjmp((c)->b) == 0) { a }
#define luai_jmpbuf             jmp_buf

#else
/**
 * @brief 标准longjmp版本
 *
 * 详细说明：
 * 使用标准的longjmp/setjmp机制处理异常。
 * 这是最通用的版本，在所有C环境中都可用。
 *
 * 特点：
 * - 完全可移植
 * - 符合C89标准
 * - 保存和恢复信号掩码
 * - 在所有平台上都能工作
 *
 * 性能考虑：
 * - 可能比_longjmp稍慢
 * - 但提供更完整的上下文保存
 * - 对大多数应用性能影响可忽略
 *
 * @since C89
 * @see longjmp, setjmp, jmp_buf
 */
#define LUAI_THROW(L,c)         longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)         if (setjmp((c)->b) == 0) { a }
#define luai_jmpbuf             jmp_buf
#endif

/** @} */

/**
 * @name 模式匹配配置
 * @brief 字符串模式匹配的限制参数
 * @{
 */

/**
 * @brief 模式匹配最大捕获数
 *
 * 详细说明：
 * 定义了字符串模式匹配中可以进行的最大捕获数量。
 * 这个限制是任意的，可以根据需要调整。
 *
 * 使用场景：
 * - string.match()函数的捕获组
 * - string.gsub()的替换模式
 * - string.gmatch()的迭代捕获
 * - 模式匹配的括号分组
 *
 * 内存影响：
 * - 每个捕获需要存储起始和结束位置
 * - 影响模式匹配的栈空间使用
 * - 32个捕获对大多数应用足够
 *
 * 调整建议：
 * - 复杂模式匹配：可以增大
 * - 内存受限环境：可以减小
 * - 考虑实际使用的捕获数量
 *
 * 性能考虑：
 * - 更多捕获会增加匹配开销
 * - 影响模式编译和执行时间
 * - 通常不是性能瓶颈
 *
 * @since C89
 * @see string.match, string.gsub, 模式匹配, 正则表达式
 */
#define LUA_MAXCAPTURES         32

/** @} */


/**
 * @name 临时文件管理
 * @brief 操作系统库创建临时文件名的配置
 * @{
 */

/**
 * @brief 临时文件名生成配置
 *
 * 详细说明：
 * 定义了os库用于创建临时文件名的函数和缓冲区大小。
 * 提供了安全和不安全两种实现方式。
 *
 * 安全性考虑：
 * - tmpnam函数被认为是不安全的（竞态条件）
 * - mkstemp提供更安全的临时文件创建
 * - 默认在POSIX系统上使用mkstemp
 *
 * 实现选择：
 * - POSIX系统：优先使用mkstemp（安全）
 * - 其他系统：使用tmpnam（兼容性）
 * - 可以根据需要自定义实现
 *
 * @since C89
 * @see os.tmpname(), 临时文件安全, 竞态条件
 */
#if defined(loslib_c) || defined(luaall_c)

/**
 * @brief POSIX mkstemp版本（安全）
 *
 * 详细说明：
 * 使用POSIX mkstemp函数创建临时文件，避免了tmpnam的安全问题。
 *
 * 安全特性：
 * - 原子性创建文件
 * - 避免竞态条件
 * - 使用安全的文件权限
 * - 防止符号链接攻击
 *
 * 实现细节：
 * - 使用固定的模板"/tmp/lua_XXXXXX"
 * - mkstemp创建文件并返回文件描述符
 * - 立即关闭文件描述符（只需要文件名）
 * - 32字节缓冲区足够存储路径
 *
 * @since POSIX.1
 * @see mkstemp(), 临时文件安全
 */
#if defined(LUA_USE_MKSTEMP)
#include <unistd.h>
#define LUA_TMPNAMBUFSIZE       32
#define lua_tmpnam(b,e) { \
    strcpy(b, "/tmp/lua_XXXXXX"); \
    e = mkstemp(b); \
    if (e != -1) close(e); \
    e = (e == -1); }

#else
/**
 * @brief 标准tmpnam版本（兼容性）
 *
 * 详细说明：
 * 使用标准C库的tmpnam函数。虽然存在安全风险，
 * 但在不支持mkstemp的系统上提供基本功能。
 *
 * 安全风险：
 * - 存在竞态条件
 * - 可能的符号链接攻击
 * - 文件名可预测
 *
 * 兼容性：
 * - 所有C89系统都支持
 * - 使用系统定义的L_tmpnam缓冲区大小
 * - 简单的错误检查
 *
 * @since C89
 * @see tmpnam(), L_tmpnam
 */
#define LUA_TMPNAMBUFSIZE       L_tmpnam
#define lua_tmpnam(b,e)         { e = (tmpnam(b) == NULL); }
#endif

#endif

/** @} */

/**
 * @name 进程管理配置
 * @brief 进程创建和管道通信的配置
 * @{
 */

/**
 * @brief 进程管道配置
 *
 * 详细说明：
 * 定义了lua_popen如何创建与当前进程通过文件流连接的新进程。
 * 根据平台提供不同的实现。
 *
 * 功能用途：
 * - io.popen()函数的底层实现
 * - 执行外部命令并获取输出
 * - 向外部进程发送输入
 * - 进程间通信
 *
 * 平台差异：
 * - POSIX系统：使用popen/pclose
 * - Windows系统：使用_popen/_pclose
 * - 不支持的系统：返回错误
 *
 * @since C89
 * @see io.popen(), 进程间通信, 管道
 */

/**
 * @brief POSIX popen版本
 *
 * 详细说明：
 * 在支持POSIX popen的系统上使用标准popen/pclose函数。
 *
 * 实现细节：
 * - fflush(NULL)确保所有输出被刷新
 * - 使用标准popen创建进程管道
 * - pclose返回值检查进程退出状态
 *
 * 功能特性：
 * - 支持读写模式
 * - 自动处理进程生命周期
 * - 标准的错误处理
 *
 * @since POSIX.1
 * @see popen(), pclose()
 */
#if defined(LUA_USE_POPEN)
#define lua_popen(L,c,m)        ((void)L, fflush(NULL), popen(c,m))
#define lua_pclose(L,file)      ((void)L, (pclose(file) != -1))

/**
 * @brief Windows版本
 *
 * 详细说明：
 * 在Windows系统上使用_popen/_pclose函数。
 *
 * Windows特性：
 * - 使用Windows特定的_popen
 * - 支持Windows命令行语法
 * - 处理Windows进程模型
 *
 * @since Windows
 * @see _popen(), _pclose()
 */
#elif defined(LUA_WIN)
#define lua_popen(L,c,m)        ((void)L, _popen(c,m))
#define lua_pclose(L,file)      ((void)L, (_pclose(file) != -1))

#else
/**
 * @brief 不支持版本
 *
 * 详细说明：
 * 在不支持进程创建的系统上返回错误。
 *
 * 错误处理：
 * - 抛出Lua错误
 * - 明确说明功能不支持
 * - 返回NULL文件指针
 *
 * 适用场景：
 * - 嵌入式系统
 * - 受限环境
 * - 安全沙箱
 *
 * @since C89
 * @see luaL_error(), 功能限制
 */
#define lua_popen(L,c,m) \
    ((void)((void)c, m), \
    luaL_error(L, LUA_QL("popen") " not supported"), (FILE*)0)
#define lua_pclose(L,file)      ((void)((void)L, file), 0)
#endif

/** @} */

/**
 * @name 动态库系统配置
 * @brief 配置动态库加载机制
 * @{
 */

/**
 * @brief 动态库系统选择
 *
 * 详细说明：
 * 定义Lua应该使用哪种动态库系统。如果Lua在为您的平台选择
 * 合适的动态库系统时遇到问题，可以在此处修改配置。
 *
 * 平台支持：
 * - Windows: LoadLibrary, GetProcAddress, FreeLibrary (LUA_DL_DLL)
 * - Unix/Linux: dlopen, dlsym, dlclose (LUA_DL_DLOPEN)
 * - macOS: dyld系统调用 (LUA_DL_DYLD)
 *
 * 编译要求：
 * - Unix系统使用dlopen需要链接-ldl
 * - 需要在Makefile中添加-DLUA_USE_DLOPEN
 * - Lua不会自动选择dlopen，需要手动配置
 *
 * 默认配置：
 * - Windows (_WIN32): LUA_DL_DLL
 * - macOS: LUA_DL_DYLD
 * - 其他Unix系统: 需要手动启用LUA_DL_DLOPEN
 *
 * 禁用动态库：
 * - 取消定义所有LUA_DL_*选项
 * - 适用于静态链接环境
 * - 提高安全性，减少依赖
 *
 * @since C89
 * @see package.loadlib(), require()
 */
#if defined(LUA_USE_DLOPEN)
#define LUA_DL_DLOPEN
#endif

#if defined(LUA_WIN)
#define LUA_DL_DLL
#endif

/** @} */

/**
 * @name 用户状态扩展
 * @brief 允许用户在lua_State中添加自定义数据和钩子
 * @{
 */

/**
 * @brief 用户额外空间大小
 *
 * 详细说明：
 * 允许在lua_State指针之前添加用户特定的数据。
 * 这个值必须是机器最大对齐要求的倍数。
 *
 * 内存布局：
 * [用户数据(LUAI_EXTRASPACE字节)][lua_State结构体]
 *
 * 使用场景：
 * - 嵌入应用的上下文数据
 * - 性能监控和统计信息
 * - 安全检查和审计数据
 * - 调试和诊断信息
 * - 线程本地存储
 *
 * 对齐要求：
 * - 必须满足平台的最大对齐要求
 * - 通常是sizeof(void*)或sizeof(double)的倍数
 * - 错误的对齐可能导致性能下降或崩溃
 *
 * 访问方法：
 * - 用户数据位于lua_State指针之前
 * - 可以通过指针运算访问：((char*)L - LUAI_EXTRASPACE)
 *
 * @since C89
 * @see luai_userstate*宏, 内存对齐
 */
#define LUAI_EXTRASPACE         0

/**
 * @brief 用户状态钩子函数
 *
 * 详细说明：
 * 这些宏允许用户在线程生命周期的关键点执行自定义操作。
 * 当定义了LUAI_EXTRASPACE时特别有用，可以管理用户数据的生命周期。
 *
 * 钩子时机和用途：
 * - luai_userstateopen: lua_State创建时调用，初始化用户数据
 * - luai_userstateclose: lua_State关闭时调用，清理用户数据
 * - luai_userstatethread: 新线程创建时调用，设置线程特定数据
 * - luai_userstatefree: 线程释放时调用，释放线程资源
 * - luai_userstateresume: 协程恢复时调用，恢复上下文
 * - luai_userstateyield: 协程挂起时调用，保存上下文
 *
 * 实现注意事项：
 * - 这些宏在Lua内部的关键路径上调用，应该高效
 * - 不应该抛出Lua错误或进行复杂操作
 * - 应该是异常安全的
 * - 可能在错误处理过程中被调用
 *
 * 使用示例：
 * @code
 * typedef struct {
 *     int request_count;
 *     clock_t start_time;
 * } UserData;
 *
 * #define LUAI_EXTRASPACE sizeof(UserData)
 *
 * #define luai_userstateopen(L) do { \
 *     UserData *ud = (UserData*)((char*)L - LUAI_EXTRASPACE); \
 *     ud->request_count = 0; \
 *     ud->start_time = clock(); \
 * } while(0)
 *
 * #define luai_userstateclose(L) do { \
 *     UserData *ud = (UserData*)((char*)L - LUAI_EXTRASPACE); \
 *     printf("Processed %d requests in %ld ms\n", \
 *            ud->request_count, \
 *            (clock() - ud->start_time) * 1000 / CLOCKS_PER_SEC); \
 * } while(0)
 * @endcode
 *
 * @param L Lua状态机指针
 * @param L1 新线程指针（仅luai_userstatethread使用）
 * @param n 参数数量（仅resume/yield使用）
 *
 * @since C89
 * @see LUAI_EXTRASPACE, 协程管理, 线程管理
 */
#define luai_userstateopen(L)           ((void)L)
#define luai_userstateclose(L)          ((void)L)
#define luai_userstatethread(L,L1)      ((void)L)
#define luai_userstatefree(L)           ((void)L)
#define luai_userstateresume(L,n)       ((void)L)
#define luai_userstateyield(L,n)        ((void)L)

/** @} */

/**
 * @name 字符串格式化配置
 * @brief 配置string.format中的整数转换
 * @{
 */

/**
 * @brief 整数格式化长度修饰符和类型
 *
 * 详细说明：
 * 定义了string.format中整数转换使用的长度修饰符和对应的C类型。
 * 根据系统对long long的支持情况选择合适的类型。
 *
 * long long支持（LUA_USELONGLONG定义时）：
 * - LUA_INTFRMLEN: "ll" - printf中的长度修饰符
 * - LUA_INTFRM_T: long long - 对应的C类型
 * - 支持更大的整数范围（通常64位）
 * - 需要C99或更新的标准
 *
 * 标准long支持（默认）：
 * - LUA_INTFRMLEN: "l" - printf中的长度修饰符
 * - LUA_INTFRM_T: long - 对应的C类型
 * - 兼容性更好，支持C89
 * - 整数范围较小（通常32位）
 *
 * 影响范围：
 * - string.format的%d, %i, %o, %x, %X格式说明符
 * - 整数的最大表示范围和精度
 * - 与C标准库printf函数的兼容性
 * - 数值转换的行为
 *
 * 选择建议：
 * - 需要大整数范围：启用LUA_USELONGLONG
 * - 最大兼容性：使用默认的long
 * - 嵌入式系统：考虑内存和性能影响
 * - 跨平台应用：测试不同平台的行为
 *
 * 使用示例：
 * @code
 * // 使用long long时
 * string.format("%d", 9223372036854775807)  // 64位最大值
 *
 * // 使用long时
 * string.format("%d", 2147483647)           // 32位最大值
 * @endcode
 *
 * @since C89
 * @see string.format(), printf格式化, C99 long long
 */
#if defined(LUA_USELONGLONG)
#define LUA_INTFRMLEN           "ll"
#define LUA_INTFRM_T            long long
#else
#define LUA_INTFRMLEN           "l"
#define LUA_INTFRM_T            long
#endif

/** @} */

/**
 * @name 本地配置区域
 * @brief 用户自定义配置的预留空间
 * @{
 */

/**
 * @brief 本地配置空间
 *
 * 详细说明：
 * 这个区域专门为用户添加自定义的重定义而保留，
 * 无需修改文件的主要部分。这是推荐的自定义配置方式。
 *
 * 使用建议：
 * - 在此处添加项目特定的配置重定义
 * - 覆盖默认的宏和常量定义
 * - 添加平台特定的适配代码
 * - 保持主配置区域的整洁和可维护性
 *
 * 常见用法：
 * @code
 * // 自定义内存分配器
 * #undef luai_userstateopen
 * #define luai_userstateopen(L) my_init_function(L)
 *
 * // 自定义错误消息格式
 * #undef LUA_QL
 * #define LUA_QL(x) "[" x "]"
 *
 * // 调整性能参数
 * #undef LUAI_GCPAUSE
 * #define LUAI_GCPAUSE 150
 *
 * // 平台特定配置
 * #ifdef MY_EMBEDDED_PLATFORM
 * #undef LUAI_MAXCALLS
 * #define LUAI_MAXCALLS 1000
 * #endif
 * @endcode
 *
 * 维护建议：
 * - 记录所有自定义配置的原因和影响
 * - 定期检查配置与新版本Lua的兼容性
 * - 使用条件编译支持多种配置
 * - 提供配置的文档和测试
 *
 * @since C89
 */

/** @} */

#endif

