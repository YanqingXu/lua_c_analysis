/**
 * @file lualib.h
 * @brief Lua 5.1.5 标准库API头文件：定义所有标准库的加载接口
 *
 * 详细说明：
 * 本文件是Lua标准库系统的核心头文件，定义了所有Lua标准库的打开函数
 * 和相关常量。它提供了模块化的库加载机制，允许应用程序选择性地加载
 * 所需的标准库，是构建定制化Lua环境的重要工具。
 *
 * 系统架构定位：
 * - 位于Lua核心API之上的标准库接口层
 * - 提供统一的库加载和初始化机制
 * - 定义标准库的命名空间和组织结构
 * - 支持模块化的库管理和按需加载
 *
 * 标准库组织结构：
 * 1. 基础库 (base)：核心函数和全局环境
 * 2. 协程库 (coroutine)：协程创建和控制
 * 3. 表库 (table)：表操作和算法
 * 4. 输入输出库 (io)：文件和流操作
 * 5. 操作系统库 (os)：系统调用和环境访问
 * 6. 字符串库 (string)：字符串处理和模式匹配
 * 7. 数学库 (math)：数学函数和常量
 * 8. 调试库 (debug)：调试和反射功能
 * 9. 包管理库 (package)：模块加载和管理
 *
 * 设计理念：
 * - 模块化：每个库独立加载，支持按需使用
 * - 标准化：统一的命名规范和接口设计
 * - 可扩展性：支持自定义库的集成
 * - 兼容性：保持向后兼容和跨平台支持
 *
 * 使用模式：
 * - 全量加载：使用luaL_openlibs加载所有标准库
 * - 选择性加载：根据需要加载特定库
 * - 定制化环境：构建专用的Lua运行环境
 * - 安全沙箱：限制可用库以提高安全性
 *
 * 典型使用场景：
 * - 嵌入式Lua环境的定制化配置
 * - 安全沙箱环境的库权限控制
 * - 性能优化的最小化库加载
 * - 特定领域应用的库组合
 *
 * 性能特征：
 * - 库加载开销：一次性初始化成本
 * - 内存使用：按需加载减少内存占用
 * - 启动时间：选择性加载提高启动速度
 * - 运行时性能：库加载后无额外开销
 *
 * 安全考虑：
 * - 库权限控制：限制危险操作的访问
 * - 沙箱环境：构建受限的执行环境
 * - 资源管理：控制系统资源的访问
 * - 代码注入防护：限制动态代码执行
 *
 * 最佳实践：
 * - 根据应用需求选择合适的库组合
 * - 在安全敏感环境中限制库的加载
 * - 使用统一的库加载策略
 * - 考虑库之间的依赖关系
 *
 * 注意事项：
 * - 某些库之间存在依赖关系
 * - 库的加载顺序可能影响功能
 * - 部分库需要特定的系统支持
 * - 调试库在生产环境中应谨慎使用
 *
 * 扩展性：
 * - 支持自定义库的注册
 * - 兼容第三方库的集成
 * - 提供库版本管理机制
 * - 支持动态库的加载和卸载
 *
 * @author Roberto Ierusalimschy, Luiz Henrique de Figueiredo, Waldemar Celes
 * @version 5.1.5
 * @date 2012
 * @since C89
 * @see lua.h, lauxlib.h
 */

#ifndef lualib_h
#define lualib_h

#include "lua.h"

/**
 * @name 系统常量定义
 * @brief 标准库使用的重要常量
 * @{
 */

/**
 * @brief 文件句柄类型标识符
 *
 * 详细说明：
 * 这是IO库中用于标识文件句柄类型的字符串常量。在Lua的类型系统中，
 * 用户数据需要通过类型名称来区分不同的数据类型，这个常量就是
 * 文件句柄的类型标识符。
 *
 * 使用场景：
 * - IO库中文件对象的类型检查
 * - 元表注册和查找
 * - 类型安全的文件操作
 * - 垃圾回收器的类型识别
 *
 * 技术细节：
 * - 作为元表名称存储在注册表中
 * - 用于luaL_checkudata的类型验证
 * - 确保文件句柄的类型安全
 * - 支持文件对象的正确垃圾回收
 *
 * @since C89
 * @see luaL_newmetatable(), luaL_checkudata()
 */
#define LUA_FILEHANDLE          "FILE*"

/** @} */

/**
 * @name 标准库加载函数
 * @brief 各个标准库的加载和初始化函数
 * @{
 */

/**
 * @brief 协程库名称常量
 *
 * 详细说明：
 * 协程库在Lua全局环境中的名称。协程库提供了创建、恢复、挂起
 * 和查询协程状态的功能，是Lua并发编程的核心组件。
 *
 * 库功能概述：
 * - coroutine.create(): 创建新协程
 * - coroutine.resume(): 恢复协程执行
 * - coroutine.yield(): 挂起当前协程
 * - coroutine.status(): 查询协程状态
 * - coroutine.wrap(): 创建协程包装器
 * - coroutine.running(): 获取当前运行的协程
 *
 * @since C89
 * @see luaopen_base()
 */
#define LUA_COLIBNAME           "coroutine"

/**
 * @brief 打开基础库和协程库
 *
 * 详细说明：
 * 初始化Lua的基础库，包括核心函数和协程支持。基础库是Lua环境的
 * 核心组件，提供了最基本的语言功能和协程操作。
 *
 * 基础库包含的功能：
 * - 全局函数：print, type, tostring, tonumber等
 * - 类型转换：数字、字符串、布尔值转换
 * - 元表操作：getmetatable, setmetatable
 * - 环境管理：getfenv, setfenv
 * - 模块加载：require, module
 * - 协程操作：coroutine.*系列函数
 * - 错误处理：error, pcall, xpcall
 * - 迭代器：pairs, ipairs, next
 *
 * 初始化过程：
 * 1. 注册所有基础全局函数
 * 2. 设置默认的元表和环境
 * 3. 初始化协程子系统
 * 4. 配置错误处理机制
 * 5. 设置标准的迭代器
 *
 * 依赖关系：
 * - 无外部依赖，是其他库的基础
 * - 必须在其他库之前加载
 * - 提供其他库需要的核心功能
 *
 * 性能考虑：
 * - 加载时间：中等，包含较多函数
 * - 内存使用：基础内存占用
 * - 运行时开销：无额外开销
 * - 协程切换：高效的上下文切换
 *
 * 使用示例：
 * @code
 * // 在C中初始化基础库
 * lua_State *L = luaL_newstate();
 * luaopen_base(L);
 *
 * // 现在可以使用基础函数
 * luaL_dostring(L, "print('Hello, World!')");
 * luaL_dostring(L, "co = coroutine.create(function() print('In coroutine') end)");
 * luaL_dostring(L, "coroutine.resume(co)");
 * @endcode
 *
 * 安全考虑：
 * - 包含require函数，可能加载任意代码
 * - 提供环境操作，可能影响全局状态
 * - 协程可能导致复杂的执行流程
 * - 在沙箱环境中需要谨慎使用
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 库加载结果
 * @retval 1 成功加载，库表在栈顶
 * @retval 0 加载失败（极少发生）
 *
 * @warning 必须在使用其他Lua功能前调用
 * @warning 包含可能不安全的函数，沙箱环境需要限制
 *
 * @note 这是最重要的库，几乎所有Lua程序都需要
 * @note 包含协程功能，支持并发编程
 * @note 提供了Lua语言的核心功能
 *
 * @since C89
 * @see luaL_openlibs(), luaopen_package(), lua_State
 */
LUALIB_API int (luaopen_base) (lua_State *L);

/**
 * @brief 表库名称常量
 *
 * 详细说明：
 * 表库在Lua全局环境中的名称。表库提供了对Lua表进行操作的
 * 高级函数，包括排序、插入、删除等算法。
 *
 * 库功能概述：
 * - table.insert(): 在表中插入元素
 * - table.remove(): 从表中删除元素
 * - table.sort(): 对表进行排序
 * - table.concat(): 连接表中的字符串元素
 * - table.maxn(): 获取表的最大数字索引
 * - table.getn(): 获取表的长度（兼容性）
 *
 * @since C89
 * @see luaopen_table()
 */
#define LUA_TABLIBNAME          "table"

/**
 * @brief 打开表操作库
 *
 * 详细说明：
 * 初始化Lua的表操作库，提供对表进行高级操作的函数集合。
 * 表库是处理Lua表数据结构的重要工具，提供了常用的算法和操作。
 *
 * 表库包含的功能：
 * - 动态数组操作：insert, remove支持数组语义
 * - 排序算法：高效的快速排序实现
 * - 字符串连接：优化的字符串拼接
 * - 长度计算：兼容不同版本的长度语义
 *
 * 算法特性：
 * - table.sort(): 稳定的快速排序，支持自定义比较函数
 * - table.insert(): O(n)时间复杂度，支持任意位置插入
 * - table.remove(): O(n)时间复杂度，自动调整索引
 * - table.concat(): 高效的字符串连接，避免多次内存分配
 *
 * 性能考虑：
 * - 排序性能：O(n log n)平均时间复杂度
 * - 插入删除：线性时间复杂度
 * - 内存使用：原地操作，内存效率高
 * - 字符串连接：优化的内存分配策略
 *
 * 使用示例：
 * @code
 * // 在C中初始化表库
 * luaopen_table(L);
 *
 * // Lua中的使用示例
 * luaL_dostring(L, "t = {3, 1, 4, 1, 5}");
 * luaL_dostring(L, "table.sort(t)");
 * luaL_dostring(L, "table.insert(t, 2, 2)");
 * luaL_dostring(L, "print(table.concat(t, ', '))");
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 库加载结果
 * @retval 1 成功加载，库表在栈顶
 *
 * @note 表库函数是纯函数，无副作用
 * @note 排序算法是稳定的
 * @note 支持自定义比较函数
 *
 * @since C89
 * @see luaL_openlibs(), LUA_TABLIBNAME
 */
LUALIB_API int (luaopen_table) (lua_State *L);

/**
 * @brief IO库名称常量
 *
 * 详细说明：
 * IO库在Lua全局环境中的名称。IO库提供了文件和流操作的
 * 完整功能，是处理输入输出的核心组件。
 *
 * 库功能概述：
 * - 文件操作：open, close, read, write
 * - 流管理：stdin, stdout, stderr
 * - 格式化IO：格式化读写支持
 * - 文件系统：文件属性和目录操作
 * - 临时文件：临时文件的创建和管理
 *
 * @since C89
 * @see luaopen_io()
 */
#define LUA_IOLIBNAME           "io"

/**
 * @brief 打开输入输出库
 *
 * 详细说明：
 * 初始化Lua的输入输出库，提供文件操作和流处理的完整功能。
 * IO库是处理文件系统和数据流的核心组件，支持多种IO模式和格式。
 *
 * IO库包含的功能：
 * - 文件操作：io.open, io.close, file:read, file:write
 * - 标准流：io.stdin, io.stdout, io.stderr
 * - 格式化IO：支持printf风格的格式化
 * - 文件定位：file:seek支持随机访问
 * - 临时文件：io.tmpfile创建临时文件
 * - 管道操作：io.popen支持进程管道
 *
 * 文件模式支持：
 * - "r": 只读模式
 * - "w": 只写模式（截断）
 * - "a": 追加模式
 * - "r+": 读写模式
 * - "w+": 读写模式（截断）
 * - "a+": 读写追加模式
 * - "b": 二进制模式修饰符
 *
 * 错误处理：
 * - 文件操作失败返回nil和错误信息
 * - 支持errno错误码
 * - 提供详细的错误描述
 * - 自动资源清理和错误恢复
 *
 * 性能考虑：
 * - 缓冲IO：提高读写性能
 * - 内存映射：大文件的高效处理
 * - 异步IO：非阻塞操作支持
 * - 资源管理：自动文件句柄清理
 *
 * 安全考虑：
 * - 文件权限检查
 * - 路径遍历防护
 * - 资源泄漏防护
 * - 沙箱环境中需要限制文件访问
 *
 * 使用示例：
 * @code
 * // 在C中初始化IO库
 * luaopen_io(L);
 *
 * // Lua中的使用示例
 * luaL_dostring(L, "file = io.open('test.txt', 'w')");
 * luaL_dostring(L, "file:write('Hello, World!')");
 * luaL_dostring(L, "file:close()");
 * luaL_dostring(L, "content = io.open('test.txt'):read('*a')");
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 库加载结果
 * @retval 1 成功加载，库表在栈顶
 *
 * @warning 提供文件系统访问，安全环境需要限制
 * @warning 文件操作可能失败，需要错误处理
 *
 * @note 支持多种文件模式和格式化操作
 * @note 自动管理文件句柄的生命周期
 * @note 提供标准流的访问接口
 *
 * @since C89
 * @see luaL_openlibs(), LUA_IOLIBNAME, LUA_FILEHANDLE
 */
LUALIB_API int (luaopen_io) (lua_State *L);

/**
 * @brief 操作系统库名称常量
 *
 * 详细说明：
 * 操作系统库在Lua全局环境中的名称。OS库提供了与操作系统
 * 交互的功能，包括时间、环境变量、进程控制等。
 *
 * 库功能概述：
 * - 时间操作：os.time, os.date, os.clock
 * - 环境变量：os.getenv, os.setenv
 * - 进程控制：os.execute, os.exit
 * - 文件系统：os.remove, os.rename, os.tmpname
 * - 随机数：os.randomseed (在某些实现中)
 *
 * @since C89
 * @see luaopen_os()
 */
#define LUA_OSLIBNAME           "os"

/**
 * @brief 打开操作系统库
 *
 * 详细说明：
 * 初始化Lua的操作系统库，提供与底层操作系统交互的功能。
 * OS库是系统编程的重要组件，但在安全环境中需要谨慎使用。
 *
 * OS库包含的功能：
 * - 时间和日期：os.time(), os.date(), os.clock()
 * - 环境访问：os.getenv()获取环境变量
 * - 进程控制：os.execute()执行系统命令
 * - 文件系统：os.remove(), os.rename()文件操作
 * - 临时文件：os.tmpname()生成临时文件名
 * - 程序退出：os.exit()终止程序
 *
 * 时间功能详解：
 * - os.time(): 获取当前时间戳或转换时间表
 * - os.date(): 格式化时间显示，支持多种格式
 * - os.clock(): 获取CPU时间，用于性能测量
 * - 支持UTC和本地时间转换
 *
 * 系统交互：
 * - os.execute(): 执行shell命令，返回退出状态
 * - os.getenv(): 安全地访问环境变量
 * - 跨平台兼容性，自动适配不同操作系统
 *
 * 安全考虑：
 * - os.execute()可以执行任意系统命令，存在安全风险
 * - 文件操作可能影响系统文件
 * - 环境变量访问可能泄露敏感信息
 * - 在沙箱环境中应该禁用或限制使用
 *
 * 性能特征：
 * - 系统调用开销：涉及内核态切换
 * - 时间函数：高精度时间测量
 * - 文件操作：依赖文件系统性能
 * - 进程创建：os.execute有较大开销
 *
 * 跨平台支持：
 * - Windows: 支持Windows API
 * - Unix/Linux: 支持POSIX标准
 * - 自动处理路径分隔符差异
 * - 时间格式的平台适配
 *
 * 使用示例：
 * @code
 * // 在C中初始化OS库
 * luaopen_os(L);
 *
 * // Lua中的使用示例
 * luaL_dostring(L, "print('Current time:', os.date())");
 * luaL_dostring(L, "print('Home directory:', os.getenv('HOME'))");
 * luaL_dostring(L, "start_time = os.clock()");
 * luaL_dostring(L, "-- do some work --");
 * luaL_dostring(L, "print('Elapsed:', os.clock() - start_time)");
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 库加载结果
 * @retval 1 成功加载，库表在栈顶
 *
 * @warning 包含潜在危险的系统操作，安全环境需要限制
 * @warning os.execute可以执行任意系统命令
 * @warning 文件操作可能影响系统安全
 *
 * @note 提供跨平台的系统操作接口
 * @note 时间函数支持高精度测量
 * @note 在受限环境中应谨慎使用
 *
 * @since C89
 * @see luaL_openlibs(), LUA_OSLIBNAME
 */
LUALIB_API int (luaopen_os) (lua_State *L);

/**
 * @brief 字符串库名称常量
 *
 * 详细说明：
 * 字符串库在Lua全局环境中的名称。字符串库提供了强大的
 * 字符串处理功能，包括模式匹配、格式化、转换等。
 *
 * 库功能概述：
 * - 模式匹配：string.find, string.match, string.gmatch
 * - 字符串替换：string.gsub支持模式替换
 * - 格式化：string.format提供printf风格格式化
 * - 大小写转换：string.upper, string.lower
 * - 字符串分割：string.sub子字符串提取
 * - 字符操作：string.char, string.byte字符转换
 *
 * @since C89
 * @see luaopen_string()
 */
#define LUA_STRLIBNAME          "string"

/**
 * @brief 打开字符串处理库
 *
 * 详细说明：
 * 初始化Lua的字符串处理库，提供强大的字符串操作和模式匹配功能。
 * 字符串库是文本处理的核心组件，支持正则表达式风格的模式匹配。
 *
 * 字符串库包含的功能：
 * - 模式匹配：find, match, gmatch支持复杂模式
 * - 字符串替换：gsub支持模式替换和函数替换
 * - 格式化输出：format提供printf风格的格式化
 * - 大小写转换：upper, lower支持Unicode
 * - 字符串操作：sub, len, rep等基础操作
 * - 字符转换：char, byte在字符和ASCII码间转换
 * - 字符串反转：reverse反转字符串
 *
 * 模式匹配系统：
 * - 字符类：%a(字母), %d(数字), %s(空白)等
 * - 量词：*, +, -, ?控制匹配次数
 * - 捕获：()创建捕获组
 * - 边界：^(开始), $(结束)
 * - 转义：%转义特殊字符
 *
 * 性能特征：
 * - 模式匹配：高效的NFA实现
 * - 字符串操作：优化的内存管理
 * - 格式化：缓存格式解析结果
 * - Unicode支持：正确处理多字节字符
 *
 * 内存管理：
 * - 字符串内部化：相同字符串共享内存
 * - 垃圾回收：自动管理字符串生命周期
 * - 缓冲区优化：减少内存分配次数
 * - 大字符串处理：支持任意长度字符串
 *
 * 国际化支持：
 * - UTF-8兼容：正确处理UTF-8编码
 * - 本地化：支持本地化的大小写转换
 * - 字符集：支持不同字符集的处理
 *
 * 使用示例：
 * @code
 * // 在C中初始化字符串库
 * luaopen_string(L);
 *
 * // Lua中的使用示例
 * luaL_dostring(L, "text = 'Hello, World!'");
 * luaL_dostring(L, "print(string.upper(text))");
 * luaL_dostring(L, "print(string.find(text, 'World'))");
 * luaL_dostring(L, "print(string.gsub(text, 'World', 'Lua'))");
 * luaL_dostring(L, "print(string.format('Number: %d', 42))");
 * @endcode
 *
 * 高级模式匹配：
 * @code
 * // 复杂模式匹配示例
 * luaL_dostring(L, "email = 'user@example.com'");
 * luaL_dostring(L, "user, domain = string.match(email, '([^@]+)@(.+)')");
 * luaL_dostring(L, "print('User:', user, 'Domain:', domain)");
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 库加载结果
 * @retval 1 成功加载，库表在栈顶
 *
 * @note 提供强大的模式匹配功能
 * @note 支持Unicode和多字节字符
 * @note 字符串操作是纯函数，无副作用
 * @note 模式匹配性能优秀
 *
 * @since C89
 * @see luaL_openlibs(), LUA_STRLIBNAME
 */
LUALIB_API int (luaopen_string) (lua_State *L);

/**
 * @brief 数学库名称常量
 *
 * 详细说明：
 * 数学库在Lua全局环境中的名称。数学库提供了完整的数学函数集合，
 * 包括三角函数、对数函数、随机数生成等。
 *
 * 库功能概述：
 * - 三角函数：sin, cos, tan, asin, acos, atan, atan2
 * - 指数对数：exp, log, log10, pow, sqrt
 * - 取整函数：floor, ceil, modf
 * - 随机数：random, randomseed
 * - 数学常量：pi, huge
 * - 其他函数：abs, min, max, fmod, frexp, ldexp
 *
 * @since C89
 * @see luaopen_math()
 */
#define LUA_MATHLIBNAME         "math"

/**
 * @brief 打开数学函数库
 *
 * 详细说明：
 * 初始化Lua的数学函数库，提供完整的数学计算功能。数学库基于
 * C标准库的数学函数，提供高精度的数值计算能力。
 *
 * 数学库包含的功能：
 * - 基础运算：abs(), min(), max()绝对值和极值
 * - 三角函数：sin(), cos(), tan()及其反函数
 * - 指数对数：exp(), log(), log10(), pow(), sqrt()
 * - 取整函数：floor(), ceil(), modf()不同的取整方式
 * - 随机数生成：random(), randomseed()伪随机数
 * - 数学常量：math.pi, math.huge无穷大
 * - 浮点操作：frexp(), ldexp()浮点数分解和组合
 * - 模运算：fmod()浮点数取模
 *
 * 数学常量：
 * - math.pi: 圆周率π (3.14159...)
 * - math.huge: 正无穷大 (HUGE_VAL)
 * - 高精度常量，适用于科学计算
 *
 * 随机数系统：
 * - math.random(): 生成[0,1)或指定范围的随机数
 * - math.randomseed(): 设置随机数种子
 * - 基于线性同余生成器
 * - 可重现的伪随机序列
 *
 * 精度和范围：
 * - 基于IEEE 754双精度浮点数
 * - 支持特殊值：NaN, ±Infinity
 * - 错误处理：域错误返回NaN
 * - 溢出处理：返回±HUGE_VAL
 *
 * 性能特征：
 * - 硬件优化：利用FPU硬件加速
 * - 查表优化：三角函数使用查表法
 * - 内联优化：简单函数可能被内联
 * - 向量化：支持SIMD指令集
 *
 * 数值稳定性：
 * - 算法选择：使用数值稳定的算法
 * - 精度保证：最小化舍入误差
 * - 边界处理：正确处理边界情况
 * - 特殊值：正确处理NaN和无穷大
 *
 * 使用示例：
 * @code
 * // 在C中初始化数学库
 * luaopen_math(L);
 *
 * // Lua中的使用示例
 * luaL_dostring(L, "print('π =', math.pi)");
 * luaL_dostring(L, "print('sin(π/2) =', math.sin(math.pi/2))");
 * luaL_dostring(L, "print('√2 =', math.sqrt(2))");
 * luaL_dostring(L, "math.randomseed(os.time())");
 * luaL_dostring(L, "print('Random:', math.random())");
 * @endcode
 *
 * 科学计算示例：
 * @code
 * // 复杂数学计算
 * luaL_dostring(L, "function distance(x1, y1, x2, y2)");
 * luaL_dostring(L, "  return math.sqrt((x2-x1)^2 + (y2-y1)^2)");
 * luaL_dostring(L, "end");
 * luaL_dostring(L, "print('Distance:', distance(0, 0, 3, 4))");
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 库加载结果
 * @retval 1 成功加载，库表在栈顶
 *
 * @note 基于C标准库的数学函数
 * @note 提供高精度的数值计算
 * @note 支持IEEE 754浮点数标准
 * @note 随机数生成器是确定性的
 *
 * @since C89
 * @see luaL_openlibs(), LUA_MATHLIBNAME
 */
LUALIB_API int (luaopen_math) (lua_State *L);

/**
 * @brief 调试库名称常量
 *
 * 详细说明：
 * 调试库在Lua全局环境中的名称。调试库提供了强大的调试和
 * 反射功能，主要用于开发和调试阶段。
 *
 * 库功能概述：
 * - 调用栈：debug.traceback, debug.getinfo
 * - 变量访问：debug.getlocal, debug.setlocal
 * - 上值操作：debug.getupvalue, debug.setupvalue
 * - 环境操作：debug.getfenv, debug.setfenv
 * - 钩子函数：debug.sethook调试钩子
 * - 元表操作：debug.getmetatable, debug.setmetatable
 *
 * @since C89
 * @see luaopen_debug()
 */
#define LUA_DBLIBNAME           "debug"

/**
 * @brief 打开调试库
 *
 * 详细说明：
 * 初始化Lua的调试库，提供强大的调试、反射和内省功能。
 * 调试库主要用于开发阶段，在生产环境中应谨慎使用。
 *
 * 调试库包含的功能：
 * - 调用栈信息：getinfo()获取函数信息，traceback()生成堆栈跟踪
 * - 局部变量：getlocal(), setlocal()访问和修改局部变量
 * - 上值操作：getupvalue(), setupvalue()操作闭包上值
 * - 环境操作：getfenv(), setfenv()访问函数环境
 * - 调试钩子：sethook()设置调试回调
 * - 元表操作：getmetatable(), setmetatable()绕过元表保护
 * - 注册表访问：getregistry()访问Lua注册表
 *
 * 调试钩子系统：
 * - 'call': 函数调用时触发
 * - 'return': 函数返回时触发
 * - 'line': 执行新行时触发
 * - 'count': 执行指定指令数后触发
 * - 支持组合使用多种钩子类型
 *
 * 性能影响：
 * - 调试钩子：显著影响执行性能
 * - 栈遍历：有一定的性能开销
 * - 变量访问：比直接访问慢
 * - 生产环境：建议禁用调试功能
 *
 * 安全考虑：
 * - 内存访问：可能访问敏感内存区域
 * - 权限提升：绕过正常的访问控制
 * - 信息泄露：可能暴露内部实现细节
 * - 沙箱环境：应该完全禁用调试库
 *
 * 调试应用：
 * - 断点调试：实现交互式调试器
 * - 性能分析：分析函数调用性能
 * - 错误诊断：生成详细的错误报告
 * - 代码覆盖：统计代码执行覆盖率
 *
 * 使用示例：
 * @code
 * // 在C中初始化调试库
 * luaopen_debug(L);
 *
 * // Lua中的使用示例
 * luaL_dostring(L, "function test() local x = 42; debug.getlocal(1, 1) end");
 * luaL_dostring(L, "print(debug.traceback())");
 * luaL_dostring(L, "info = debug.getinfo(test)");
 * luaL_dostring(L, "print('Function name:', info.name)");
 * @endcode
 *
 * 调试钩子示例：
 * @code
 * // 设置调试钩子
 * luaL_dostring(L, "debug.sethook(function(event, line)");
 * luaL_dostring(L, "  print('Event:', event, 'Line:', line)");
 * luaL_dostring(L, "end, 'l')");
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 库加载结果
 * @retval 1 成功加载，库表在栈顶
 *
 * @warning 提供强大的内省能力，可能破坏封装性
 * @warning 调试钩子会显著影响性能
 * @warning 在生产环境中应谨慎使用
 * @warning 安全环境应禁用调试库
 *
 * @note 主要用于开发和调试阶段
 * @note 提供完整的反射和内省功能
 * @note 支持动态调试和性能分析
 *
 * @since C89
 * @see luaL_openlibs(), LUA_DBLIBNAME
 */
LUALIB_API int (luaopen_debug) (lua_State *L);

/**
 * @brief 包管理库名称常量
 *
 * 详细说明：
 * 包管理库在Lua全局环境中的名称。包管理库提供了模块加载、
 * 搜索和管理的完整机制，是Lua模块系统的核心。
 *
 * 库功能概述：
 * - 模块加载：require函数的实现
 * - 搜索路径：package.path, package.cpath
 * - 加载器：package.loaders自定义加载器
 * - 模块缓存：package.loaded已加载模块
 * - 预加载：package.preload预定义模块
 *
 * @since C89
 * @see luaopen_package()
 */
#define LUA_LOADLIBNAME         "package"

/**
 * @brief 打开包管理库
 *
 * 详细说明：
 * 初始化Lua的包管理库，提供模块加载和管理的完整功能。
 * 包管理库是Lua模块系统的核心，实现了require函数和相关机制。
 *
 * 包管理库包含的功能：
 * - require()函数：模块加载的主要接口
 * - package.path：Lua模块的搜索路径
 * - package.cpath：C模块的搜索路径
 * - package.loaded：已加载模块的缓存表
 * - package.preload：预加载模块的注册表
 * - package.loaders：模块加载器的数组
 * - package.loadlib()：动态库加载函数
 * - package.seeall()：模块环境设置
 *
 * 模块加载机制：
 * 1. 检查package.loaded缓存
 * 2. 检查package.preload预加载
 * 3. 使用package.loaders中的加载器
 * 4. 搜索package.path中的Lua文件
 * 5. 搜索package.cpath中的C库
 * 6. 缓存加载结果到package.loaded
 *
 * 搜索路径格式：
 * - "?": 模块名占位符
 * - ";": 路径分隔符
 * - 支持相对和绝对路径
 * - 环境变量：LUA_PATH, LUA_CPATH
 *
 * C模块加载：
 * - 动态链接库：.so (Unix), .dll (Windows)
 * - 入口函数：luaopen_<modulename>
 * - 符号解析：自动查找入口函数
 * - 错误处理：加载失败的详细错误信息
 *
 * 安全考虑：
 * - 代码执行：require可以执行任意Lua代码
 * - 动态库：C模块可以执行任意系统操作
 * - 路径遍历：恶意模块名可能导致路径遍历
 * - 沙箱环境：需要限制模块加载路径
 *
 * 性能优化：
 * - 模块缓存：避免重复加载
 * - 预加载：减少文件系统访问
 * - 延迟加载：按需加载模块
 * - 路径优化：优化搜索路径顺序
 *
 * 使用示例：
 * @code
 * // 在C中初始化包管理库
 * luaopen_package(L);
 *
 * // Lua中的使用示例
 * luaL_dostring(L, "json = require('json')");
 * luaL_dostring(L, "print('Search path:', package.path)");
 * luaL_dostring(L, "print('Loaded modules:')");
 * luaL_dostring(L, "for k,v in pairs(package.loaded) do print(k) end");
 * @endcode
 *
 * 自定义加载器：
 * @code
 * // 添加自定义加载器
 * luaL_dostring(L, "table.insert(package.loaders, function(name)");
 * luaL_dostring(L, "  if name == 'mymodule' then");
 * luaL_dostring(L, "    return function() return {version = '1.0'} end");
 * luaL_dostring(L, "  end");
 * luaL_dostring(L, "end)");
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 库加载结果
 * @retval 1 成功加载，库表在栈顶
 *
 * @warning require可以执行任意代码，存在安全风险
 * @warning C模块加载可能导致系统级安全问题
 * @warning 在受限环境中需要控制模块加载路径
 *
 * @note 实现了完整的模块系统
 * @note 支持Lua和C模块的统一加载
 * @note 提供灵活的搜索和缓存机制
 * @note 是构建大型Lua应用的基础
 *
 * @since C89
 * @see luaL_openlibs(), LUA_LOADLIBNAME, require
 */
LUALIB_API int (luaopen_package) (lua_State *L);

/**
 * @brief 打开所有标准库
 *
 * 详细说明：
 * 一次性加载所有Lua标准库的便利函数。这是最常用的库初始化方式，
 * 为Lua环境提供完整的标准功能集合。函数会按照正确的顺序加载
 * 所有标准库，确保库之间的依赖关系得到满足。
 *
 * 加载的库列表：
 * 1. 基础库 (base) - 核心函数和协程
 * 2. 包管理库 (package) - 模块加载系统
 * 3. 表库 (table) - 表操作函数
 * 4. 输入输出库 (io) - 文件和流操作
 * 5. 操作系统库 (os) - 系统调用接口
 * 6. 字符串库 (string) - 字符串处理
 * 7. 数学库 (math) - 数学函数
 * 8. 调试库 (debug) - 调试和反射
 *
 * 加载顺序：
 * - 基础库必须首先加载，提供核心功能
 * - 包管理库其次，支持其他库的模块化
 * - 其他库的顺序相对灵活
 * - 调试库通常最后加载
 *
 * 内存和性能：
 * - 内存使用：加载所有库会占用较多内存
 * - 启动时间：完整加载需要更多初始化时间
 * - 运行时性能：库加载后无额外性能开销
 * - 适用场景：通用Lua环境和开发环境
 *
 * 替代方案：
 * - 选择性加载：只加载需要的库
 * - 延迟加载：按需加载特定库
 * - 定制组合：根据应用需求组合库
 * - 安全配置：在受限环境中排除危险库
 *
 * 安全考虑：
 * - 包含所有库，包括潜在危险的功能
 * - OS库和IO库提供系统访问能力
 * - 调试库可能暴露内部信息
 * - 包管理库允许加载任意代码
 * - 在安全敏感环境中需要谨慎使用
 *
 * 使用场景：
 * - 通用Lua脚本环境
 * - 开发和测试环境
 * - 教学和学习环境
 * - 原型开发和快速验证
 * - 不需要严格安全控制的应用
 *
 * 使用示例：
 * @code
 * // 创建新的Lua状态机并加载所有标准库
 * lua_State *L = luaL_newstate();
 * luaL_openlibs(L);
 *
 * // 现在可以使用所有标准库功能
 * luaL_dostring(L, "print('Hello, World!')");           // 基础库
 * luaL_dostring(L, "t = {3,1,4}; table.sort(t)");       // 表库
 * luaL_dostring(L, "print(string.upper('hello'))");     // 字符串库
 * luaL_dostring(L, "print(math.sin(math.pi/2))");       // 数学库
 * luaL_dostring(L, "print(os.date())");                 // OS库
 *
 * lua_close(L);
 * @endcode
 *
 * 选择性加载对比：
 * @code
 * // 选择性加载（更安全的方式）
 * lua_State *L = luaL_newstate();
 * luaopen_base(L);      // 基础功能
 * luaopen_table(L);     // 表操作
 * luaopen_string(L);    // 字符串处理
 * luaopen_math(L);      // 数学函数
 * // 不加载IO、OS、调试库以提高安全性
 * @endcode
 *
 * 错误处理：
 * - 库加载失败会抛出Lua错误
 * - 内存不足可能导致加载失败
 * - 系统限制可能影响某些库的功能
 * - 错误信息会指示具体的失败原因
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @warning 加载所有库，包括潜在危险的系统访问功能
 * @warning 在安全敏感环境中应使用选择性加载
 * @warning 增加内存使用和启动时间
 *
 * @note 这是最常用的库初始化方式
 * @note 提供完整的Lua标准功能
 * @note 适用于大多数通用应用场景
 * @note 库加载顺序已经过优化
 *
 * @since C89
 * @see luaopen_base(), luaopen_package(), luaopen_table(), luaopen_io(),
 *      luaopen_os(), luaopen_string(), luaopen_math(), luaopen_debug()
 */
LUALIB_API void (luaL_openlibs) (lua_State *L);

/** @} */

/**
 * @name 调试和断言宏
 * @brief 用于调试和开发的辅助宏
 * @{
 */

/**
 * @brief Lua断言宏
 *
 * 详细说明：
 * 这是一个条件编译的断言宏，用于在调试版本中进行运行时检查。
 * 在发布版本中，这个宏被定义为空操作，不会产生任何代码。
 *
 * 设计目的：
 * - 调试支持：在开发阶段进行运行时检查
 * - 性能优化：发布版本中完全移除断言代码
 * - 条件编译：根据编译配置决定是否包含断言
 * - 代码文档：断言本身就是代码的文档
 *
 * 使用模式：
 * - 通常在lua.h中会有实际的断言实现
 * - 调试版本：lua_assert(x) -> assert(x)
 * - 发布版本：lua_assert(x) -> ((void)0)
 * - 可以通过定义NDEBUG来控制行为
 *
 * 典型用法：
 * - 参数有效性检查
 * - 内部状态一致性验证
 * - 前置条件和后置条件检查
 * - 不变量验证
 *
 * 性能影响：
 * - 调试版本：有运行时检查开销
 * - 发布版本：零开销，完全优化掉
 * - 编译时：不影响编译时间
 * - 代码大小：发布版本不增加代码大小
 *
 * 最佳实践：
 * - 用于检查程序员错误，不是用户错误
 * - 断言条件应该是快速检查
 * - 断言不应该有副作用
 * - 断言失败表示程序逻辑错误
 *
 * 使用示例：
 * @code
 * // 在Lua内部代码中的典型用法
 * void some_function(lua_State *L, int index) {
 *     lua_assert(L != NULL);                    // 参数检查
 *     lua_assert(index > 0);                    // 索引有效性
 *     lua_assert(index <= lua_gettop(L));       // 栈边界检查
 *
 *     // 函数实现...
 *
 *     lua_assert(lua_gettop(L) == expected);    // 后置条件
 * }
 * @endcode
 *
 * 条件编译示例：
 * @code
 * // 在lua.h中可能的实际定义
 * #ifdef NDEBUG
 * #define lua_assert(x)    ((void)0)
 * #else
 * #define lua_assert(x)    assert(x)
 * #endif
 * @endcode
 *
 * @param x 要检查的条件表达式
 *
 * @note 这是一个空实现，实际定义通常在lua.h中
 * @note 发布版本中不产生任何代码
 * @note 主要用于Lua内部开发，用户代码很少直接使用
 *
 * @since C89
 * @see assert(), NDEBUG
 */
#ifndef lua_assert
#define lua_assert(x)           ((void)0)
#endif

/** @} */

#endif
