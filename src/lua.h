/**
 * @file lua.h
 * @brief Lua 5.1.5 核心API头文件：提供完整的Lua C API接口定义
 *
 * 详细说明：
 * 本文件是Lua脚本语言的核心C API头文件，定义了所有用于在C程序中
 * 嵌入和扩展Lua解释器的接口函数、数据类型和常量。Lua是一种轻量级、
 * 高性能的脚本语言，广泛应用于游戏开发、应用程序脚本化、配置管理等领域。
 *
 * 系统架构定位：
 * - 作为Lua解释器的公共接口层，隔离内部实现细节
 * - 提供栈式操作模型，简化C与Lua之间的数据交换
 * - 支持协程、垃圾回收、调试等高级特性
 * - 设计为可嵌入式，最小化对宿主程序的依赖
 *
 * 技术特点：
 * - 基于栈的虚拟机架构，操作简单高效
 * - 自动内存管理，支持增量垃圾回收
 * - 协程支持，实现轻量级并发编程
 * - 元表机制，提供强大的面向对象编程能力
 * - 动态类型系统，支持8种基本数据类型
 *
 * 依赖关系：
 * - 标准C库：stdarg.h（可变参数）、stddef.h（基本类型定义）
 * - luaconf.h：Lua配置文件，定义平台相关的配置选项
 * - 无其他外部依赖，保证最大的可移植性
 *
 * 编译要求：
 * - C标准版本：C89或更高版本（推荐C99）
 * - 编译器支持：GCC、Clang、MSVC等主流编译器
 * - 平台支持：Windows、Linux、macOS、嵌入式系统
 *
 * 使用示例：
 * @code
 * #include "lua.h"
 * #include "lauxlib.h"
 * #include "lualib.h"
 *
 * int main() {
 *     // 创建Lua状态机
 *     lua_State *L = luaL_newstate();
 *     if (L == NULL) {
 *         fprintf(stderr, "无法创建Lua状态机\n");
 *         return -1;
 *     }
 *
 *     // 加载标准库
 *     luaL_openlibs(L);
 *
 *     // 执行Lua脚本
 *     if (luaL_dostring(L, "print('Hello from Lua!')") != 0) {
 *         fprintf(stderr, "脚本执行失败: %s\n", lua_tostring(L, -1));
 *     }
 *
 *     // 清理资源
 *     lua_close(L);
 *     return 0;
 * }
 * @endcode
 *
 * 内存安全考虑：
 * - 所有API函数都进行参数有效性检查
 * - 自动垃圾回收机制防止内存泄漏
 * - 栈溢出保护，防止无限递归
 * - 提供内存分配器自定义接口
 *
 * 性能特征：
 * - 解释器启动时间：< 1ms（典型情况）
 * - 内存占用：核心解释器约150KB
 * - 函数调用开销：约为原生C函数调用的3-5倍
 * - 垃圾回收：增量式，单次暂停时间 < 1ms
 *
 * 线程安全性：
 * - lua_State不是线程安全的，每个线程需要独立的状态机
 * - 全局状态和常量是只读的，可以安全共享
 * - 提供lua_newthread创建协程，支持协作式多任务
 *
 * 注意事项：
 * - 必须正确管理Lua栈，避免栈溢出或下溢
 * - 字符串和userdata的生命周期由垃圾回收器管理
 * - 跨C函数调用边界时需要注意异常处理
 * - 在多线程环境中使用时需要适当的同步机制
 *
 * @author Roberto Ierusalimschy, Luiz Henrique de Figueiredo, Waldemar Celes
 * @version 5.1.5
 * @date 2012
 * @since C89
 * @see luaconf.h, lauxlib.h, lualib.h
 */

#ifndef lua_h
#define lua_h

#include <stdarg.h>
#include <stddef.h>

#include "luaconf.h"

/**
 * @name Lua版本信息常量
 * @brief 定义Lua解释器的版本标识和作者信息
 * @{
 */
#define LUA_VERSION         "Lua 5.1"                                    /**< Lua主版本号字符串 */
#define LUA_RELEASE         "Lua 5.1.5"                                  /**< Lua完整版本号字符串 */
#define LUA_VERSION_NUM     501                                          /**< Lua版本号数值形式，用于版本比较 */
#define LUA_COPYRIGHT       "Copyright (C) 1994-2012 Lua.org, PUC-Rio"  /**< Lua版权信息 */
#define LUA_AUTHORS         "R. Ierusalimschy, L. H. de Figueiredo & W. Celes" /**< Lua作者信息 */
/** @} */

/**
 * @name 预编译代码标识
 * @brief 用于标识Lua预编译字节码的魔数
 */
#define LUA_SIGNATURE       "\033Lua"                                    /**< 预编译代码文件头标识：ESC + "Lua" */

/**
 * @name 函数调用返回值控制
 * @brief 控制lua_call和lua_pcall函数的返回值数量
 */
#define LUA_MULTRET         (-1)                                         /**< 返回所有结果值的特殊标识 */

/**
 * @name 伪索引常量
 * @brief 用于访问特殊表的伪索引，这些索引不对应实际的栈位置
 * @{
 */
#define LUA_REGISTRYINDEX   (-10000)                                     /**< 注册表伪索引：全局唯一的键值存储 */
#define LUA_ENVIRONINDEX    (-10001)                                     /**< 环境表伪索引：当前函数的环境 */
#define LUA_GLOBALSINDEX    (-10002)                                     /**< 全局表伪索引：全局变量存储 */
#define lua_upvalueindex(i) (LUA_GLOBALSINDEX-(i))                       /**< 上值伪索引宏：访问闭包的上值 */
/** @} */

/**
 * @name 线程状态码
 * @brief 定义Lua线程（协程）的执行状态
 * @{
 */
#define LUA_YIELD           1                                            /**< 线程挂起状态：协程主动让出执行权 */
#define LUA_ERRRUN          2                                            /**< 运行时错误：脚本执行过程中发生错误 */
#define LUA_ERRSYNTAX       3                                            /**< 语法错误：脚本编译时发现语法问题 */
#define LUA_ERRMEM          4                                            /**< 内存错误：内存分配失败 */
#define LUA_ERRERR          5                                            /**< 错误处理错误：错误处理函数本身出错 */
/** @} */

/**
 * @brief Lua状态机结构体：封装了Lua解释器的完整运行时状态
 *
 * 详细说明：
 * lua_State是Lua解释器的核心数据结构，包含了虚拟机栈、全局状态、
 * 垃圾回收器状态、调试信息等所有运行时数据。每个lua_State实例
 * 代表一个独立的Lua执行环境，可以看作是一个轻量级的虚拟机实例。
 *
 * 设计理念：
 * - 封装性：隐藏内部实现细节，只通过API函数访问
 * - 独立性：每个状态机相互独立，支持多实例并存
 * - 轻量性：内存占用小，创建和销毁开销低
 *
 * 生命周期管理：
 * - 创建：通过lua_newstate()或luaL_newstate()创建
 * - 使用：通过各种lua_*函数操作状态机
 * - 销毁：通过lua_close()释放所有资源
 *
 * 内存管理：
 * - 自动垃圾回收：自动管理Lua对象的内存
 * - 自定义分配器：支持用户自定义内存分配策略
 * - 内存限制：可以设置内存使用上限
 *
 * 并发模型：
 * - 非线程安全：单个状态机不能被多线程同时访问
 * - 协程支持：通过lua_newthread创建协程
 * - 状态隔离：不同状态机之间完全隔离
 *
 * @since C89
 * @see lua_newstate(), lua_close(), lua_newthread()
 */
typedef struct lua_State lua_State;

/**
 * @brief C函数类型定义：可以被Lua调用的C函数签名
 *
 * 详细说明：
 * 这是所有可以从Lua脚本中调用的C函数必须遵循的函数签名。
 * 函数通过Lua栈与脚本进行参数传递和结果返回。
 *
 * 参数说明：
 * @param L Lua状态机指针，用于访问Lua栈和状态
 *
 * 返回值说明：
 * @return 返回推入栈中的结果数量，0表示无返回值
 *
 * 使用模式：
 * @code
 * // C函数实现示例
 * static int my_c_function(lua_State *L) {
 *     // 获取参数数量
 *     int argc = lua_gettop(L);
 *
 *     // 检查参数
 *     if (argc != 2) {
 *         return luaL_error(L, "期望2个参数，实际得到%d个", argc);
 *     }
 *
 *     // 获取参数
 *     double a = luaL_checknumber(L, 1);
 *     double b = luaL_checknumber(L, 2);
 *
 *     // 计算结果并推入栈
 *     lua_pushnumber(L, a + b);
 *
 *     // 返回结果数量
 *     return 1;
 * }
 * @endcode
 *
 * 错误处理：
 * - 可以通过lua_error()或luaL_error()抛出错误
 * - 错误会被转换为Lua异常，可以被pcall捕获
 * - 函数应该保证栈的一致性
 *
 * 性能考虑：
 * - 避免频繁的栈操作，批量处理数据
 * - 使用luaL_checkstack()确保栈空间充足
 * - 合理使用lua_rawget/lua_rawset提高性能
 *
 * @since C89
 * @see lua_pushcfunction(), lua_call(), lua_pcall()
 */
typedef int (*lua_CFunction) (lua_State *L);

/**
 * @brief 读取器函数类型：用于从数据源读取Lua代码块
 *
 * 详细说明：
 * 读取器函数用于lua_load()函数中，从各种数据源（文件、内存、网络等）
 * 读取Lua源代码或字节码。这种设计允许灵活的代码加载策略。
 *
 * 参数说明：
 * @param L Lua状态机指针
 * @param ud 用户数据指针，由lua_load()调用时传入
 * @param sz 输出参数，返回读取的数据长度
 *
 * 返回值说明：
 * @return 指向读取数据的指针，NULL表示读取结束
 *
 * 实现要求：
 * - 每次调用返回一个数据块
 * - 通过sz参数返回数据长度
 * - 返回NULL表示数据读取完毕
 * - 返回的内存在下次调用前必须保持有效
 *
 * @since C89
 * @see lua_load(), lua_dump()
 */
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);

/**
 * @brief 写入器函数类型：用于将Lua字节码写入目标位置
 *
 * 详细说明：
 * 写入器函数用于lua_dump()函数中，将编译后的Lua字节码写入到
 * 各种目标位置（文件、内存缓冲区、网络等）。
 *
 * 参数说明：
 * @param L Lua状态机指针
 * @param p 指向要写入数据的指针
 * @param sz 要写入的数据长度
 * @param ud 用户数据指针，由lua_dump()调用时传入
 *
 * 返回值说明：
 * @return 0表示写入成功，非0表示写入失败
 *
 * 实现要求：
 * - 必须写入所有sz字节的数据
 * - 写入失败时返回非0值
 * - 可以进行缓冲以提高性能
 *
 * @since C89
 * @see lua_dump(), lua_load()
 */
typedef int (*lua_Writer) (lua_State *L, const void* p, size_t sz, void* ud);

/**
 * @brief 内存分配器函数类型：自定义Lua的内存管理策略
 *
 * 详细说明：
 * 内存分配器函数允许用户完全控制Lua的内存分配和释放行为。
 * 这对于嵌入式系统、内存池管理、内存统计等场景非常有用。
 *
 * 参数说明：
 * @param ud 用户数据指针，在创建状态机时指定
 * @param ptr 要重新分配的内存指针，NULL表示新分配
 * @param osize 原始内存块大小，0表示新分配
 * @param nsize 新的内存块大小，0表示释放内存
 *
 * 返回值说明：
 * @return 新分配的内存指针，分配失败或释放内存时返回NULL
 *
 * 行为规范：
 * - nsize == 0：释放ptr指向的内存，返回NULL
 * - ptr == NULL：分配nsize字节的新内存
 * - 其他情况：重新分配ptr指向的内存为nsize字节
 *
 * 实现示例：
 * @code
 * static void* my_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
 *     (void)ud; (void)osize;  // 未使用的参数
 *
 *     if (nsize == 0) {
 *         free(ptr);
 *         return NULL;
 *     } else {
 *         return realloc(ptr, nsize);
 *     }
 * }
 * @endcode
 *
 * @since C89
 * @see lua_newstate(), lua_getallocf(), lua_setallocf()
 */
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);

/**
 * @name Lua数据类型常量
 * @brief 定义Lua中所有基本数据类型的标识符
 * @{
 */
#define LUA_TNONE           (-1)                                         /**< 无效类型：栈位置无效时返回 */

#define LUA_TNIL            0                                            /**< nil类型：表示空值或未定义 */
#define LUA_TBOOLEAN        1                                            /**< 布尔类型：true或false */
#define LUA_TLIGHTUSERDATA  2                                            /**< 轻量用户数据：C指针的简单包装 */
#define LUA_TNUMBER         3                                            /**< 数字类型：双精度浮点数 */
#define LUA_TSTRING         4                                            /**< 字符串类型：不可变字符串 */
#define LUA_TTABLE          5                                            /**< 表类型：关联数组，Lua的核心数据结构 */
#define LUA_TFUNCTION       6                                            /**< 函数类型：Lua函数或C函数 */
#define LUA_TUSERDATA       7                                            /**< 完整用户数据：带元表的C数据块 */
#define LUA_TTHREAD         8                                            /**< 线程类型：协程对象 */
/** @} */

/**
 * @name 栈管理常量
 * @brief 定义Lua栈的最小保证空间
 */
#define LUA_MINSTACK        20                                           /**< C函数可用的最小栈空间：保证至少20个栈位 */

/**
 * @name 用户自定义头文件包含
 * @brief 允许用户在编译时包含自定义的头文件
 */
#if defined(LUA_USER_H)
#include LUA_USER_H                                                      /**< 用户自定义头文件：通过LUA_USER_H宏指定 */
#endif

/**
 * @brief Lua数字类型定义：Lua中数字值的C类型表示
 *
 * 详细说明：
 * lua_Number是Lua中所有数字值在C代码中的表示类型。
 * 默认情况下是double类型，但可以通过luaconf.h配置为其他类型。
 *
 * 配置选项：
 * - 双精度浮点：double（默认，范围约±1.7e308）
 * - 单精度浮点：float（节省内存，范围约±3.4e38）
 * - 长双精度：long double（扩展精度，平台相关）
 *
 * 使用场景：
 * - 与Lua数字值进行转换时使用
 * - 在C函数中处理数字参数和返回值
 * - 进行数学运算和比较操作
 *
 * @since C89
 * @see lua_tonumber(), lua_pushnumber(), LUA_NUMBER
 */
typedef LUA_NUMBER lua_Number;

/**
 * @brief Lua整数类型定义：Lua中整数值的C类型表示
 *
 * 详细说明：
 * lua_Integer是Lua中整数值在C代码中的表示类型。
 * 通常用于数组索引、循环计数等需要整数语义的场合。
 *
 * 配置选项：
 * - ptrdiff_t：指针差值类型（默认，与平台指针大小匹配）
 * - long：长整型（32位或64位，平台相关）
 * - int：标准整型（通常32位）
 *
 * 使用场景：
 * - 数组索引和表键值操作
 * - 循环计数和迭代控制
 * - 位运算和整数数学运算
 *
 * @since C89
 * @see lua_tointeger(), lua_pushinteger(), LUA_INTEGER
 */
typedef LUA_INTEGER lua_Integer;

/**
 * @name 状态机管理函数
 * @brief 用于创建、销毁和管理Lua状态机的核心函数
 * @{
 */

/**
 * @brief 创建新的Lua状态机：使用自定义内存分配器
 *
 * 详细说明：
 * 创建一个全新的Lua状态机实例，使用用户提供的内存分配器。
 * 新创建的状态机包含空的栈和基本的运行时环境，但不包含标准库。
 *
 * 算法描述：
 * 1. 使用提供的分配器分配状态机内存
 * 2. 初始化虚拟机栈和基本数据结构
 * 3. 设置垃圾回收器的初始状态
 * 4. 创建注册表、全局表等基础表
 *
 * 内存管理：
 * - 所有内存分配都通过用户提供的分配器进行
 * - 分配失败时返回NULL，不会抛出异常
 * - 状态机的所有后续内存操作都使用相同的分配器
 *
 * 使用示例：
 * @code
 * // 使用标准分配器创建状态机
 * lua_State *L = lua_newstate(realloc, NULL);
 * if (L == NULL) {
 *     fprintf(stderr, "无法创建Lua状态机\n");
 *     return -1;
 * }
 *
 * // 使用状态机...
 *
 * // 清理资源
 * lua_close(L);
 * @endcode
 *
 * @param[in] f 内存分配器函数指针，不能为NULL
 * @param[in] ud 用户数据指针，传递给分配器函数
 *
 * @return 成功时返回新的状态机指针，失败时返回NULL
 * @retval NULL 内存分配失败或分配器函数为NULL
 *
 * @warning 必须使用lua_close()释放返回的状态机
 * @note 新状态机不包含标准库，需要手动加载
 *
 * @since C89
 * @see lua_close(), luaL_newstate(), lua_setallocf()
 */
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud);

/**
 * @brief 关闭并销毁Lua状态机：释放所有相关资源
 *
 * 详细说明：
 * 销毁指定的Lua状态机，释放其占用的所有内存资源。
 * 这包括栈内容、全局变量、注册表、垃圾回收器管理的对象等。
 *
 * 清理过程：
 * 1. 触发完整的垃圾回收，释放所有Lua对象
 * 2. 调用所有userdata的__gc元方法
 * 3. 关闭所有打开的文件和资源
 * 4. 释放状态机本身的内存
 *
 * 安全考虑：
 * - 调用后L指针变为无效，不能再使用
 * - 会自动处理循环引用和复杂对象图
 * - 确保所有C资源得到正确释放
 *
 * @param[in] L 要关闭的Lua状态机指针，不能为NULL
 *
 * @warning 调用后L指针立即失效，不能再使用
 * @warning 确保没有其他代码持有对该状态机的引用
 *
 * @since C89
 * @see lua_newstate(), luaL_newstate()
 */
LUA_API void       (lua_close) (lua_State *L);

/**
 * @brief 创建新的协程线程：在现有状态机中创建协程
 *
 * 详细说明：
 * 在指定的Lua状态机中创建一个新的协程（线程）。新协程与主线程
 * 共享全局状态，但拥有独立的执行栈，支持协作式多任务编程。
 *
 * 协程特性：
 * - 共享全局状态：访问相同的全局变量和注册表
 * - 独立执行栈：拥有自己的函数调用栈
 * - 协作式调度：通过yield/resume实现主动让出
 * - 轻量级：创建和切换开销很小
 *
 * 使用模式：
 * @code
 * // 创建协程
 * lua_State *co = lua_newthread(L);
 *
 * // 在协程中加载函数
 * lua_pushcfunction(co, my_coroutine_func);
 *
 * // 启动协程
 * int result = lua_resume(co, 0);
 * if (result == LUA_YIELD) {
 *     printf("协程挂起\n");
 * } else if (result == 0) {
 *     printf("协程完成\n");
 * } else {
 *     printf("协程错误: %s\n", lua_tostring(co, -1));
 * }
 * @endcode
 *
 * @param[in] L 父状态机指针，不能为NULL
 *
 * @return 新创建的协程状态机指针
 * @retval NULL 内存分配失败
 *
 * @note 新协程会被推入父状态机的栈顶
 * @note 协程的生命周期由垃圾回收器管理
 *
 * @since C89
 * @see lua_resume(), lua_yield(), lua_status()
 */
LUA_API lua_State *(lua_newthread) (lua_State *L);

/**
 * @brief 设置恐慌函数：处理无法恢复的错误
 *
 * 详细说明：
 * 设置当Lua遇到无法通过正常错误处理机制恢复的严重错误时
 * 调用的恐慌函数。这通常发生在内存不足或栈溢出等情况下。
 *
 * 恐慌情况：
 * - 内存分配失败且无法进行垃圾回收
 * - 栈溢出且无法扩展栈空间
 * - 错误处理函数本身出错
 * - 其他无法恢复的系统级错误
 *
 * 恐慌函数要求：
 * - 不应该返回到Lua（通常调用exit()或longjmp()）
 * - 可以进行清理工作，但要快速完成
 * - 不应该再调用Lua API函数
 *
 * 默认行为：
 * - 如果没有设置恐慌函数，程序会调用abort()终止
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] panicf 恐慌函数指针，NULL表示使用默认行为
 *
 * @return 之前设置的恐慌函数指针
 *
 * @warning 恐慌函数不应该返回到Lua
 * @note 恐慌函数应该尽快终止程序或进行非本地跳转
 *
 * @since C89
 * @see lua_error(), lua_pcall()
 */
LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);

/** @} */

/**
 * @name 基本栈操作函数
 * @brief 用于管理Lua虚拟栈的基础函数
 * @{
 */

/**
 * @brief 获取栈顶位置：返回栈中元素的数量
 *
 * 详细说明：
 * 返回当前栈中有效元素的数量，也就是栈顶的索引值。
 * 栈索引从1开始，所以返回值0表示栈为空。
 *
 * 栈索引规则：
 * - 正索引：1表示栈底，lua_gettop(L)表示栈顶
 * - 负索引：-1表示栈顶，-lua_gettop(L)表示栈底
 * - 0表示无效位置
 *
 * 使用场景：
 * - 检查函数参数数量
 * - 保存栈状态以便后续恢复
 * - 验证栈操作的正确性
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 栈中元素的数量（栈顶索引）
 * @retval 0 栈为空
 * @retval >0 栈中元素数量
 *
 * @note 这是一个O(1)操作，性能开销很小
 *
 * @since C89
 * @see lua_settop(), lua_checkstack()
 */
LUA_API int   (lua_gettop) (lua_State *L);

/**
 * @brief 设置栈顶位置：调整栈的大小
 *
 * 详细说明：
 * 设置栈顶到指定位置，可以用来压入nil值或弹出多个值。
 * 这是一个非常高效的栈大小调整操作。
 *
 * 操作行为：
 * - idx > 当前栈顶：压入(idx - 栈顶)个nil值
 * - idx < 当前栈顶：弹出(栈顶 - idx)个值
 * - idx == 当前栈顶：无操作
 * - idx == 0：清空整个栈
 *
 * 索引处理：
 * - 正索引：直接使用
 * - 负索引：相对于当前栈顶计算
 *
 * 使用示例：
 * @code
 * // 清空栈
 * lua_settop(L, 0);
 *
 * // 保留栈顶3个元素
 * lua_settop(L, 3);
 *
 * // 弹出1个元素（等价于lua_pop(L, 1)）
 * lua_settop(L, -2);
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 新的栈顶位置索引
 *
 * @warning 确保idx不会超出栈的有效范围
 * @note 这是实现lua_pop宏的基础函数
 *
 * @since C89
 * @see lua_gettop(), lua_pop(), lua_checkstack()
 */
LUA_API void  (lua_settop) (lua_State *L, int idx);

/**
 * @brief 复制栈中的值：将指定位置的值复制到栈顶
 *
 * 详细说明：
 * 将栈中指定位置的值复制一份并推入栈顶，原位置的值保持不变。
 * 这是一个非常常用的栈操作，用于复制值而不移动它们。
 *
 * 复制规则：
 * - 创建值的完整副本（对于字符串、数字等）
 * - 对于表、函数等引用类型，复制引用而不是内容
 * - 保持原值在栈中的位置不变
 *
 * 使用场景：
 * - 在不移动原值的情况下获取副本
 * - 为函数调用准备参数
 * - 实现栈值的重新排列
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 要复制的值的栈索引
 *
 * @warning 确保idx指向有效的栈位置
 * @note 栈会增长1个位置
 *
 * @since C89
 * @see lua_remove(), lua_insert(), lua_replace()
 */
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);

/**
 * @brief 移除栈中的值：删除指定位置的值并压缩栈
 *
 * 详细说明：
 * 移除栈中指定位置的值，并将其上方的所有值下移一位。
 * 这个操作会改变栈的大小和其他值的索引。
 *
 * 移除过程：
 * 1. 删除指定位置的值
 * 2. 将该位置上方的所有值下移一位
 * 3. 栈顶位置减1
 *
 * 索引影响：
 * - 被移除位置之上的所有正索引减1
 * - 负索引保持相对位置不变
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 要移除的值的栈索引
 *
 * @warning 确保idx指向有效的栈位置
 * @warning 移除操作会改变其他值的索引
 *
 * @since C89
 * @see lua_insert(), lua_replace(), lua_pushvalue()
 */
LUA_API void  (lua_remove) (lua_State *L, int idx);

/**
 * @brief 插入栈顶值：将栈顶值移动到指定位置
 *
 * 详细说明：
 * 将栈顶的值移动到指定位置，并将该位置及其上方的值上移一位。
 * 栈的总大小保持不变，但值的位置发生变化。
 *
 * 插入过程：
 * 1. 取出栈顶值
 * 2. 将指定位置及其上方的值上移一位
 * 3. 将栈顶值放入指定位置
 *
 * 使用场景：
 * - 重新排列栈中值的顺序
 * - 将新值插入到特定位置
 * - 实现复杂的栈操作模式
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 插入位置的栈索引
 *
 * @warning 确保栈不为空且idx有效
 * @note 栈大小保持不变，但值的位置会改变
 *
 * @since C89
 * @see lua_remove(), lua_replace(), lua_pushvalue()
 */
LUA_API void  (lua_insert) (lua_State *L, int idx);

/**
 * @brief 替换栈中的值：用栈顶值替换指定位置的值
 *
 * 详细说明：
 * 用栈顶的值替换指定位置的值，然后弹出栈顶值。
 * 这是一个原子操作，栈大小减1。
 *
 * 替换过程：
 * 1. 将栈顶值复制到指定位置
 * 2. 弹出栈顶值
 * 3. 栈大小减1
 *
 * 使用场景：
 * - 更新栈中特定位置的值
 * - 实现赋值操作
 * - 优化栈空间使用
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 要替换的位置的栈索引
 *
 * @warning 确保栈不为空且idx有效
 * @note 栈大小减1
 *
 * @since C89
 * @see lua_insert(), lua_remove(), lua_pushvalue()
 */
LUA_API void  (lua_replace) (lua_State *L, int idx);

/**
 * @brief 检查栈空间：确保栈有足够的空间
 *
 * 详细说明：
 * 检查栈是否有足够的空间容纳指定数量的新元素。
 * 如果空间不足，会尝试扩展栈；如果扩展失败，返回0。
 *
 * 栈管理：
 * - Lua栈会根据需要自动增长
 * - 但在某些情况下可能达到内存限制
 * - 建议在大量压栈操作前检查空间
 *
 * 性能考虑：
 * - 栈扩展可能涉及内存重新分配
 * - 提前检查可以避免操作中途失败
 * - 对于已知大小的操作，一次性检查更高效
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] sz 需要的额外栈空间数量
 *
 * @return 是否有足够的栈空间
 * @retval 1 栈空间充足或扩展成功
 * @retval 0 栈空间不足且无法扩展
 *
 * @note 每个C函数至少保证有LUA_MINSTACK个栈位可用
 *
 * @since C89
 * @see LUA_MINSTACK, lua_gettop(), lua_settop()
 */
LUA_API int   (lua_checkstack) (lua_State *L, int sz);

/**
 * @brief 跨状态机移动值：在两个状态机之间传递值
 *
 * 详细说明：
 * 将值从一个Lua状态机的栈顶移动到另一个状态机的栈顶。
 * 这主要用于在主线程和协程之间传递数据。
 *
 * 移动规则：
 * - 从源状态机弹出n个值
 * - 将这些值推入目标状态机
 * - 保持值的相对顺序
 * - 两个状态机必须属于同一个Lua universe
 *
 * 使用场景：
 * - 主线程与协程之间的数据传递
 * - 不同执行上下文间的值共享
 * - 实现复杂的协程通信模式
 *
 * 限制条件：
 * - 两个状态机必须共享相同的全局状态
 * - 不能在不相关的状态机之间使用
 *
 * @param[in] from 源状态机指针，不能为NULL
 * @param[in] to 目标状态机指针，不能为NULL
 * @param[in] n 要移动的值的数量
 *
 * @warning 确保两个状态机相互兼容
 * @warning 确保源状态机有足够的值可移动
 *
 * @since C89
 * @see lua_newthread(), lua_resume(), lua_yield()
 */
LUA_API void  (lua_xmove) (lua_State *from, lua_State *to, int n);

/** @} */

/**
 * @name 类型检查函数
 * @brief 用于检查栈中值的类型的函数组
 * @{
 */

/**
 * @brief 检查指定位置的值是否为数字类型
 *
 * 详细说明：
 * 检查栈中指定位置的值是否可以转换为数字。在Lua中，数字和
 * 可以转换为数字的字符串都被认为是"数字类型"。
 *
 * 转换规则：
 * - 数字类型：直接返回1
 * - 数字字符串：可以转换，返回1
 * - 其他类型：无法转换，返回0
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 是否为数字类型
 * @retval 1 值可以转换为数字
 * @retval 0 值无法转换为数字或索引无效
 *
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_tonumber(), lua_type(), LUA_TNUMBER
 */
LUA_API int             (lua_isnumber) (lua_State *L, int idx);

/**
 * @brief 检查指定位置的值是否为字符串类型
 *
 * 详细说明：
 * 检查栈中指定位置的值是否为字符串或数字（数字可以转换为字符串）。
 * 在Lua中，字符串和数字在需要时可以相互转换。
 *
 * 转换规则：
 * - 字符串类型：直接返回1
 * - 数字类型：可以转换为字符串，返回1
 * - 其他类型：无法转换，返回0
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 是否为字符串类型
 * @retval 1 值可以转换为字符串
 * @retval 0 值无法转换为字符串或索引无效
 *
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_tolstring(), lua_type(), LUA_TSTRING
 */
LUA_API int             (lua_isstring) (lua_State *L, int idx);

/**
 * @brief 检查指定位置的值是否为C函数类型
 *
 * 详细说明：
 * 检查栈中指定位置的值是否为C函数。C函数是用C语言编写的
 * 函数，通过lua_pushcfunction等函数推入栈中。
 *
 * 函数类型区分：
 * - C函数：用C语言编写，返回1
 * - Lua函数：用Lua语言编写，返回0
 * - 其他类型：非函数类型，返回0
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 是否为C函数类型
 * @retval 1 值是C函数
 * @retval 0 值不是C函数或索引无效
 *
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_tocfunction(), lua_pushcfunction(), LUA_TFUNCTION
 */
LUA_API int             (lua_iscfunction) (lua_State *L, int idx);

/**
 * @brief 检查指定位置的值是否为用户数据类型
 *
 * 详细说明：
 * 检查栈中指定位置的值是否为用户数据（userdata）。
 * 用户数据包括完整用户数据和轻量用户数据两种类型。
 *
 * 用户数据类型：
 * - 完整用户数据：带有元表的C数据块，返回1
 * - 轻量用户数据：简单的C指针包装，返回1
 * - 其他类型：非用户数据，返回0
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 是否为用户数据类型
 * @retval 1 值是用户数据（完整或轻量）
 * @retval 0 值不是用户数据或索引无效
 *
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_touserdata(), lua_newuserdata(), LUA_TUSERDATA, LUA_TLIGHTUSERDATA
 */
LUA_API int             (lua_isuserdata) (lua_State *L, int idx);

/**
 * @brief 获取指定位置值的类型标识符
 *
 * 详细说明：
 * 返回栈中指定位置值的类型标识符。类型标识符是预定义的常量，
 * 用于区分Lua中的8种基本数据类型。
 *
 * 返回的类型常量：
 * - LUA_TNIL: nil类型
 * - LUA_TBOOLEAN: 布尔类型
 * - LUA_TLIGHTUSERDATA: 轻量用户数据
 * - LUA_TNUMBER: 数字类型
 * - LUA_TSTRING: 字符串类型
 * - LUA_TTABLE: 表类型
 * - LUA_TFUNCTION: 函数类型
 * - LUA_TUSERDATA: 完整用户数据
 * - LUA_TTHREAD: 线程类型
 * - LUA_TNONE: 无效索引
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 类型标识符常量
 * @retval LUA_TNONE 索引无效或超出栈范围
 * @retval 其他 对应的类型常量
 *
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_typename(), LUA_TNIL, LUA_TBOOLEAN, LUA_TNUMBER
 */
LUA_API int             (lua_type) (lua_State *L, int idx);

/**
 * @brief 获取类型标识符对应的类型名称字符串
 *
 * 详细说明：
 * 将类型标识符转换为可读的类型名称字符串。这主要用于
 * 错误报告、调试信息和用户界面显示。
 *
 * 类型名称映射：
 * - LUA_TNIL → "nil"
 * - LUA_TBOOLEAN → "boolean"
 * - LUA_TLIGHTUSERDATA → "userdata"
 * - LUA_TNUMBER → "number"
 * - LUA_TSTRING → "string"
 * - LUA_TTABLE → "table"
 * - LUA_TFUNCTION → "function"
 * - LUA_TUSERDATA → "userdata"
 * - LUA_TTHREAD → "thread"
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] tp 类型标识符，通常由lua_type()返回
 *
 * @return 类型名称字符串指针
 * @retval 非NULL 指向类型名称的常量字符串
 * @retval NULL 无效的类型标识符
 *
 * @note 返回的字符串是常量，不需要释放
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_type(), LUA_TNIL, LUA_TBOOLEAN
 */
LUA_API const char     *(lua_typename) (lua_State *L, int tp);

/** @} */

/**
 * @name 值比较函数
 * @brief 用于比较栈中值的函数组
 * @{
 */

/**
 * @brief 比较两个值是否相等（调用元方法）
 *
 * 详细说明：
 * 比较栈中两个位置的值是否相等，遵循Lua的相等性语义。
 * 如果值有__eq元方法，会调用该元方法进行比较。
 *
 * 比较规则：
 * - 相同类型：按类型特定规则比较
 * - 不同类型：通常不相等，除非有元方法
 * - 有__eq元方法：调用元方法决定结果
 * - 数字和字符串：不会自动转换比较
 *
 * 元方法调用：
 * - 如果任一值有__eq元方法，调用该元方法
 * - 元方法返回真值表示相等，假值表示不等
 * - 元方法可能抛出错误
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx1 第一个值的栈索引
 * @param[in] idx2 第二个值的栈索引
 *
 * @return 比较结果
 * @retval 1 两个值相等
 * @retval 0 两个值不等或索引无效
 *
 * @warning 可能调用元方法，因此可能抛出错误
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_rawequal(), lua_lessthan()
 */
LUA_API int            (lua_equal) (lua_State *L, int idx1, int idx2);

/**
 * @brief 原始比较两个值是否相等（不调用元方法）
 *
 * 详细说明：
 * 比较栈中两个位置的值是否相等，不调用任何元方法。
 * 这是最基础的相等性比较，只基于值的原始内容。
 *
 * 比较规则：
 * - nil: 只与nil相等
 * - 布尔值: 值必须相同
 * - 数字: 数值必须相等
 * - 字符串: 内容必须完全相同
 * - 表/函数/用户数据/线程: 必须是同一个对象
 * - 轻量用户数据: 指针值必须相同
 *
 * 性能特征：
 * - 不调用元方法，性能更高
 * - 不会抛出错误
 * - 适用于需要快速比较的场景
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx1 第一个值的栈索引
 * @param[in] idx2 第二个值的栈索引
 *
 * @return 比较结果
 * @retval 1 两个值原始相等
 * @retval 0 两个值不等或索引无效
 *
 * @note 此函数不会修改栈，不会调用元方法
 * @note 比lua_equal()更快，但语义不同
 * @since C89
 * @see lua_equal(), lua_lessthan()
 */
LUA_API int            (lua_rawequal) (lua_State *L, int idx1, int idx2);

/**
 * @brief 比较第一个值是否小于第二个值
 *
 * 详细说明：
 * 比较栈中两个位置的值，判断第一个值是否小于第二个值。
 * 遵循Lua的比较语义，可能调用__lt元方法。
 *
 * 比较规则：
 * - 数字: 按数值大小比较
 * - 字符串: 按字典序比较
 * - 其他类型: 通常不可比较，除非有元方法
 * - 不同类型: 通常不可比较，除非有元方法
 *
 * 元方法调用：
 * - 如果有__lt元方法，调用该元方法
 * - 元方法返回真值表示小于，假值表示不小于
 * - 如果没有__lt但有__le，可能使用__le进行比较
 *
 * 错误情况：
 * - 比较不可比较的类型会抛出错误
 * - 元方法执行失败会抛出错误
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx1 第一个值的栈索引
 * @param[in] idx2 第二个值的栈索引
 *
 * @return 比较结果
 * @retval 1 第一个值小于第二个值
 * @retval 0 第一个值不小于第二个值或索引无效
 *
 * @warning 可能调用元方法，因此可能抛出错误
 * @warning 比较不兼容类型会抛出错误
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_equal(), lua_rawequal()
 */
LUA_API int            (lua_lessthan) (lua_State *L, int idx1, int idx2);

/** @} */

/**
 * @name 值转换函数
 * @brief 将栈中的值转换为C类型的函数组
 * @{
 */

/**
 * @brief 将栈中的值转换为数字类型
 *
 * 详细说明：
 * 将栈中指定位置的值转换为lua_Number类型。如果值无法转换为数字，
 * 返回0。转换遵循Lua的数字转换规则。
 *
 * 转换规则：
 * - 数字类型：直接返回数值
 * - 数字字符串：解析为数字返回
 * - 其他类型：返回0（转换失败）
 *
 * 字符串转换：
 * - 支持整数和浮点数格式
 * - 支持科学计数法（如1.5e10）
 * - 忽略前导和尾随空白字符
 * - 部分匹配：只要开头是有效数字即可
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 转换后的数字值
 * @retval 0 转换失败或索引无效
 * @retval 其他 转换成功的数字值
 *
 * @note 此函数不会修改栈
 * @note 转换失败时返回0，无法区分真正的0值
 * @since C89
 * @see lua_isnumber(), lua_tointeger(), lua_pushnumber()
 */
LUA_API lua_Number      (lua_tonumber) (lua_State *L, int idx);

/**
 * @brief 将栈中的值转换为整数类型
 *
 * 详细说明：
 * 将栈中指定位置的值转换为lua_Integer类型。转换规则与
 * lua_tonumber类似，但结果会截断为整数。
 *
 * 转换规则：
 * - 整数：直接返回
 * - 浮点数：截断小数部分
 * - 数字字符串：先转换为数字再截断
 * - 其他类型：返回0
 *
 * 截断行为：
 * - 正数：向零截断（如3.7 → 3）
 * - 负数：向零截断（如-3.7 → -3）
 * - 不进行四舍五入
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 转换后的整数值
 * @retval 0 转换失败或索引无效
 * @retval 其他 转换成功的整数值
 *
 * @note 此函数不会修改栈
 * @note 浮点数会被截断，不是四舍五入
 * @since C89
 * @see lua_tonumber(), lua_isnumber(), lua_pushinteger()
 */
LUA_API lua_Integer     (lua_tointeger) (lua_State *L, int idx);

/**
 * @brief 将栈中的值转换为布尔类型
 *
 * 详细说明：
 * 将栈中指定位置的值转换为C的int类型表示的布尔值。
 * 遵循Lua的真值判断规则。
 *
 * 真值规则：
 * - false和nil：返回0（假）
 * - 其他所有值：返回1（真）
 * - 包括0、空字符串、空表等都是真值
 *
 * 特殊情况：
 * - 无效索引：返回0
 * - 数字0：返回1（在Lua中0是真值）
 * - 空字符串：返回1（在Lua中空字符串是真值）
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 布尔值
 * @retval 0 值为false、nil或索引无效
 * @retval 1 值为真值
 *
 * @note 此函数不会修改栈
 * @note Lua的真值规则与C不同，只有false和nil是假值
 * @since C89
 * @see lua_pushboolean(), LUA_TBOOLEAN
 */
LUA_API int             (lua_toboolean) (lua_State *L, int idx);

/**
 * @brief 将栈中的值转换为字符串并返回长度
 *
 * 详细说明：
 * 将栈中指定位置的值转换为字符串，并可选地返回字符串长度。
 * 这是获取字符串表示的主要方法。
 *
 * 转换规则：
 * - 字符串：直接返回
 * - 数字：转换为字符串表示
 * - 其他类型：返回NULL
 *
 * 重要行为：
 * - 如果值是数字，会在栈中将其转换为字符串
 * - 转换后栈中的值变为字符串类型
 * - 返回的指针指向Lua内部字符串，不需要释放
 * - 字符串以null结尾，可以安全用于C函数
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 * @param[out] len 可选的长度输出参数，可以为NULL
 *
 * @return 字符串指针
 * @retval NULL 转换失败或索引无效
 * @retval 非NULL 指向字符串内容的指针
 *
 * @warning 可能修改栈中的值（数字转字符串）
 * @warning 返回的指针在下次Lua操作后可能失效
 * @note 字符串可能包含嵌入的null字符，使用len参数获取真实长度
 * @since C89
 * @see lua_tostring(), lua_pushstring(), lua_isstring()
 */
LUA_API const char     *(lua_tolstring) (lua_State *L, int idx, size_t *len);

/**
 * @brief 获取对象的长度（字符串、表、用户数据）
 *
 * 详细说明：
 * 获取栈中指定位置对象的长度。不同类型的对象有不同的长度定义，
 * 可能调用__len元方法。
 *
 * 长度定义：
 * - 字符串：字节数（不是字符数）
 * - 表：序列部分的长度（连续整数索引）
 * - 用户数据：分配的字节数
 * - 其他类型：返回0
 *
 * 元方法调用：
 * - 表和用户数据可能有__len元方法
 * - 如果有__len元方法，调用该元方法
 * - 元方法应该返回数字类型的长度值
 *
 * 表长度特殊性：
 * - 只计算序列部分（从1开始的连续整数索引）
 * - 遇到nil值时停止计算
 * - 稀疏数组的长度可能不符合直觉
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 对象的长度
 * @retval 0 对象无长度概念或索引无效
 * @retval >0 对象的实际长度
 *
 * @warning 可能调用元方法，因此可能抛出错误
 * @note 字符串长度是字节数，对于UTF-8字符串可能与字符数不同
 * @since C89
 * @see lua_strlen(), lua_tolstring()
 */
LUA_API size_t          (lua_objlen) (lua_State *L, int idx);

/**
 * @brief 将栈中的值转换为C函数指针
 *
 * 详细说明：
 * 如果栈中指定位置的值是C函数，返回对应的函数指针。
 * 只有通过lua_pushcfunction等方式推入的C函数才能转换。
 *
 * 转换规则：
 * - C函数：返回函数指针
 * - Lua函数：返回NULL
 * - 其他类型：返回NULL
 *
 * 使用场景：
 * - 检查函数是否为C函数
 * - 获取C函数指针进行直接调用
 * - 实现函数类型的运行时检查
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return C函数指针
 * @retval NULL 不是C函数或索引无效
 * @retval 非NULL 有效的C函数指针
 *
 * @note 此函数不会修改栈
 * @note 返回的函数指针可以直接调用，但需要正确设置参数
 * @since C89
 * @see lua_iscfunction(), lua_pushcfunction(), lua_CFunction
 */
LUA_API lua_CFunction   (lua_tocfunction) (lua_State *L, int idx);

/**
 * @brief 将栈中的值转换为用户数据指针
 *
 * 详细说明：
 * 如果栈中指定位置的值是用户数据（完整或轻量），返回对应的指针。
 * 这是访问用户数据内容的主要方法。
 *
 * 转换规则：
 * - 完整用户数据：返回数据块指针
 * - 轻量用户数据：返回存储的指针值
 * - 其他类型：返回NULL
 *
 * 用户数据类型：
 * - 完整用户数据：Lua管理的内存块，可以有元表
 * - 轻量用户数据：简单的C指针包装，无元表
 *
 * 安全考虑：
 * - 返回的指针可能指向已释放的内存
 * - 需要确保用户数据的生命周期
 * - 类型转换需要应用程序自行保证正确性
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 用户数据指针
 * @retval NULL 不是用户数据或索引无效
 * @retval 非NULL 指向用户数据的指针
 *
 * @note 此函数不会修改栈
 * @warning 返回的指针需要转换为正确的类型才能使用
 * @since C89
 * @see lua_isuserdata(), lua_newuserdata(), lua_pushlightuserdata()
 */
LUA_API void           *(lua_touserdata) (lua_State *L, int idx);

/**
 * @brief 将栈中的值转换为线程（协程）指针
 *
 * 详细说明：
 * 如果栈中指定位置的值是线程（协程），返回对应的lua_State指针。
 * 这用于获取协程的状态机以进行协程操作。
 *
 * 转换规则：
 * - 线程类型：返回协程的lua_State指针
 * - 主线程：返回主线程的lua_State指针
 * - 其他类型：返回NULL
 *
 * 协程操作：
 * - 返回的指针可以用于lua_resume、lua_yield等协程函数
 * - 协程与主线程共享全局状态
 * - 协程有独立的执行栈
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 线程状态机指针
 * @retval NULL 不是线程类型或索引无效
 * @retval 非NULL 有效的lua_State指针
 *
 * @note 此函数不会修改栈
 * @note 返回的指针指向协程的状态机，与原状态机共享全局状态
 * @since C89
 * @see lua_newthread(), lua_resume(), lua_yield(), LUA_TTHREAD
 */
LUA_API lua_State      *(lua_tothread) (lua_State *L, int idx);

/**
 * @brief 将栈中的值转换为通用指针（用于调试）
 *
 * 详细说明：
 * 将栈中指定位置的值转换为通用指针，主要用于调试和唯一性检查。
 * 每个Lua对象都有唯一的指针表示。
 *
 * 转换规则：
 * - 所有类型都返回唯一的指针值
 * - 相同对象总是返回相同指针
 * - 不同对象返回不同指针
 * - nil和无效索引返回NULL
 *
 * 使用场景：
 * - 调试信息显示
 * - 对象唯一性检查
 * - 实现对象哈希表
 * - 日志记录和跟踪
 *
 * 注意事项：
 * - 指针值不能用于访问对象内容
 * - 指针值可能在垃圾回收后重用
 * - 仅用于比较和调试，不用于解引用
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 栈索引，可以是正数、负数或伪索引
 *
 * @return 对象的唯一指针表示
 * @retval NULL nil值或索引无效
 * @retval 非NULL 对象的唯一指针标识
 *
 * @warning 返回的指针仅用于比较，不能解引用
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_type(), lua_equal()
 */
LUA_API const void     *(lua_topointer) (lua_State *L, int idx);

/** @} */

/**
 * @name 值压栈函数
 * @brief 将C值推入Lua栈的函数组
 * @{
 */

/**
 * @brief 将nil值推入栈
 *
 * 详细说明：
 * 将Lua的nil值推入栈顶。nil是Lua中表示"无值"或"空值"的特殊类型，
 * 类似于其他语言中的null或None。
 *
 * 使用场景：
 * - 表示变量未初始化或不存在
 * - 删除表中的键值对（设置为nil）
 * - 函数参数的默认值
 * - 条件判断中的假值
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @note 栈增长1个位置
 * @note nil是Lua中的假值之一（另一个是false）
 * @since C89
 * @see LUA_TNIL, lua_isnil(), lua_type()
 */
LUA_API void  (lua_pushnil) (lua_State *L);

/**
 * @brief 将数字值推入栈
 *
 * 详细说明：
 * 将一个lua_Number类型的数字值推入栈顶。lua_Number通常是double类型，
 * 但可以通过配置改为其他数字类型。
 *
 * 数字特性：
 * - 支持整数和浮点数
 * - 精度取决于lua_Number的定义
 * - 可以与字符串自动转换
 * - 支持所有数学运算
 *
 * 特殊值处理：
 * - 支持正无穷、负无穷和NaN
 * - 特殊值的行为遵循IEEE 754标准
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] n 要推入的数字值
 *
 * @note 栈增长1个位置
 * @since C89
 * @see lua_tonumber(), lua_pushinteger(), LUA_TNUMBER
 */
LUA_API void  (lua_pushnumber) (lua_State *L, lua_Number n);

/**
 * @brief 将整数值推入栈
 *
 * 详细说明：
 * 将一个lua_Integer类型的整数值推入栈顶。这是推入整数的优化版本，
 * 在某些配置下可能比lua_pushnumber更高效。
 *
 * 整数特性：
 * - 精确表示整数值
 * - 范围取决于lua_Integer的定义
 * - 可以与浮点数和字符串转换
 * - 适用于数组索引和计数
 *
 * 性能考虑：
 * - 在整数优化的Lua版本中更高效
 * - 避免浮点数的精度问题
 * - 适合大整数的精确计算
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] n 要推入的整数值
 *
 * @note 栈增长1个位置
 * @since C89
 * @see lua_tointeger(), lua_pushnumber(), LUA_TNUMBER
 */
LUA_API void  (lua_pushinteger) (lua_State *L, lua_Integer n);

/**
 * @brief 将指定长度的字符串推入栈
 *
 * 详细说明：
 * 将指定长度的字符串推入栈顶。这个函数可以处理包含嵌入null字符的字符串，
 * 因为长度是显式指定的。
 *
 * 字符串特性：
 * - 支持任意二进制数据
 * - 可以包含嵌入的null字符
 * - 字符串在Lua中是不可变的
 * - 相同内容的字符串会被内部化（共享存储）
 *
 * 内存管理：
 * - Lua会复制字符串内容
 * - 原始字符串可以在调用后立即释放
 * - 字符串由垃圾回收器管理
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] s 指向字符串数据的指针，不能为NULL
 * @param[in] l 字符串的长度（字节数）
 *
 * @note 栈增长1个位置
 * @note 字符串内容会被复制，原始指针可以释放
 * @since C89
 * @see lua_pushstring(), lua_tolstring(), LUA_TSTRING
 */
LUA_API void  (lua_pushlstring) (lua_State *L, const char *s, size_t l);

/**
 * @brief 将以null结尾的字符串推入栈
 *
 * 详细说明：
 * 将一个以null字符结尾的C字符串推入栈顶。这是最常用的字符串推入函数，
 * 适用于标准的C字符串。
 *
 * 字符串处理：
 * - 自动计算字符串长度（直到遇到null字符）
 * - 不能包含嵌入的null字符
 * - 支持UTF-8编码的字符串
 * - 字符串会被内部化以节省内存
 *
 * 使用限制：
 * - 字符串必须以null字符结尾
 * - 不能处理二进制数据
 * - 对于包含null的数据，使用lua_pushlstring
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] s 指向null结尾字符串的指针，不能为NULL
 *
 * @note 栈增长1个位置
 * @note 等价于lua_pushlstring(L, s, strlen(s))
 * @since C89
 * @see lua_pushlstring(), lua_tolstring(), LUA_TSTRING
 */
LUA_API void  (lua_pushstring) (lua_State *L, const char *s);

/**
 * @brief 将格式化字符串推入栈（使用va_list）
 *
 * 详细说明：
 * 使用printf风格的格式化字符串创建新字符串并推入栈顶。
 * 这个版本使用va_list参数，适用于包装函数。
 *
 * 格式化支持：
 * - %s: 字符串参数
 * - %d: 整数参数
 * - %f: 浮点数参数
 * - %c: 字符参数
 * - %%: 字面量%字符
 *
 * 内存管理：
 * - 格式化结果由Lua管理
 * - 返回的指针指向Lua内部字符串
 * - 字符串会被自动内部化
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] fmt 格式化字符串，不能为NULL
 * @param[in] argp 参数列表
 *
 * @return 指向格式化结果字符串的指针
 *
 * @note 栈增长1个位置
 * @note 返回的指针与栈顶字符串相同
 * @since C89
 * @see lua_pushfstring(), lua_pushstring()
 */
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt,
                                                      va_list argp);

/**
 * @brief 将格式化字符串推入栈（使用可变参数）
 *
 * 详细说明：
 * 使用printf风格的格式化字符串创建新字符串并推入栈顶。
 * 这是最常用的格式化字符串函数。
 *
 * 格式化特性：
 * - 支持标准printf格式说明符
 * - 自动类型转换和格式化
 * - 高效的字符串构建
 * - 结果自动内部化
 *
 * 使用示例：
 * @code
 * lua_pushfstring(L, "Error in %s at line %d", filename, line_number);
 * lua_pushfstring(L, "Value: %.2f", double_value);
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] fmt 格式化字符串，不能为NULL
 * @param[in] ... 格式化参数
 *
 * @return 指向格式化结果字符串的指针
 *
 * @note 栈增长1个位置
 * @note 返回的指针与栈顶字符串相同
 * @since C89
 * @see lua_pushvfstring(), lua_pushstring()
 */
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);

/**
 * @brief 将C闭包推入栈
 *
 * 详细说明：
 * 创建一个C闭包并推入栈顶。C闭包是带有上值的C函数，
 * 上值是闭包可以访问的外部变量。
 *
 * 闭包创建：
 * - 从栈顶取n个值作为上值
 * - 创建包含这些上值的闭包
 * - 将闭包推入栈顶
 * - 栈的净变化是减少n-1个位置
 *
 * 上值访问：
 * - 在C函数中通过lua_upvalueindex(i)访问
 * - 上值索引从1开始
 * - 上值在闭包生命周期内保持有效
 *
 * 使用示例：
 * @code
 * // 推入上值
 * lua_pushstring(L, "config");
 * lua_pushnumber(L, 42);
 *
 * // 创建带2个上值的闭包
 * lua_pushcclosure(L, my_function, 2);
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] fn C函数指针，不能为NULL
 * @param[in] n 上值数量，必须 >= 0
 *
 * @note 栈变化：移除n个值，推入1个闭包（净变化：1-n）
 * @note n为0时等价于lua_pushcfunction
 * @since C89
 * @see lua_pushcfunction(), lua_upvalueindex(), lua_CFunction
 */
LUA_API void  (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);

/**
 * @brief 将布尔值推入栈
 *
 * 详细说明：
 * 将一个布尔值推入栈顶。在Lua中，布尔类型只有true和false两个值。
 *
 * 布尔值转换：
 * - 0: 转换为false
 * - 非0: 转换为true
 * - 遵循C语言的真值约定
 *
 * Lua布尔语义：
 * - 只有false和nil是假值
 * - 所有其他值（包括0和空字符串）都是真值
 * - 布尔值可以用于条件判断和逻辑运算
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] b 布尔值（0为false，非0为true）
 *
 * @note 栈增长1个位置
 * @since C89
 * @see lua_toboolean(), LUA_TBOOLEAN
 */
LUA_API void  (lua_pushboolean) (lua_State *L, int b);

/**
 * @brief 将轻量用户数据推入栈
 *
 * 详细说明：
 * 将一个C指针作为轻量用户数据推入栈顶。轻量用户数据是
 * 对C指针的简单包装，不由Lua的垃圾回收器管理。
 *
 * 轻量用户数据特性：
 * - 只是C指针的包装
 * - 不由垃圾回收器管理
 * - 不能有元表
 * - 内存开销很小
 * - 相同指针值的轻量用户数据相等
 *
 * 使用场景：
 * - 传递C对象的句柄
 * - 存储回调函数的上下文
 * - 实现轻量级的对象引用
 * - 与C库的接口交互
 *
 * 安全考虑：
 * - 指针的有效性由应用程序保证
 * - 不会自动释放指向的内存
 * - 需要确保指针在使用期间有效
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] p 要包装的C指针，可以为NULL
 *
 * @note 栈增长1个位置
 * @note 指针的生命周期由应用程序管理
 * @since C89
 * @see lua_touserdata(), lua_newuserdata(), LUA_TLIGHTUSERDATA
 */
LUA_API void  (lua_pushlightuserdata) (lua_State *L, void *p);

/**
 * @brief 将当前线程推入栈
 *
 * 详细说明：
 * 将当前的Lua线程（状态机）作为线程对象推入栈顶。
 * 这主要用于协程编程和线程间通信。
 *
 * 线程对象特性：
 * - 表示一个Lua执行线程
 * - 可以是主线程或协程
 * - 支持协程的挂起和恢复
 * - 与其他线程共享全局状态
 *
 * 使用场景：
 * - 实现协程调度器
 * - 线程间的引用传递
 * - 调试和监控工具
 * - 实现复杂的控制流
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 是否为主线程
 * @retval 1 当前线程是主线程
 * @retval 0 当前线程是协程
 *
 * @note 栈增长1个位置
 * @since C89
 * @see lua_newthread(), lua_tothread(), LUA_TTHREAD
 */
LUA_API int   (lua_pushthread) (lua_State *L);

/** @} */

/**
 * @name 表访问函数
 * @brief 从Lua表中获取值的函数组
 * @{
 */

/**
 * @brief 从表中获取值（调用元方法）
 *
 * 详细说明：
 * 从栈中指定位置的表中获取值，键从栈顶获取。如果表有__index元方法，
 * 会调用该元方法。这等价于Lua中的t[k]操作。
 *
 * 操作过程：
 * 1. 从栈顶弹出键
 * 2. 在指定表中查找该键
 * 3. 将找到的值推入栈顶
 * 4. 如果键不存在且有__index元方法，调用元方法
 *
 * 元方法调用：
 * - 如果键不存在且表有__index元方法，调用该元方法
 * - __index可以是函数或表
 * - 如果是函数，调用__index(table, key)
 * - 如果是表，在该表中递归查找
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 *
 * @warning 可能调用元方法，因此可能抛出错误
 * @note 栈变化：弹出1个键，推入1个值（净变化：0）
 * @since C89
 * @see lua_rawget(), lua_getfield(), lua_settable()
 */
LUA_API void  (lua_gettable) (lua_State *L, int idx);

/**
 * @brief 从表中获取指定字段的值
 *
 * 详细说明：
 * 从栈中指定位置的表中获取指定名称的字段值。这是lua_gettable的
 * 便利版本，直接使用字符串作为键。
 *
 * 操作等价于：
 * @code
 * lua_pushstring(L, k);
 * lua_gettable(L, idx);
 * @endcode
 *
 * 字段访问：
 * - 支持任意字符串键
 * - 会调用__index元方法（如果存在）
 * - 常用于访问对象的方法和属性
 *
 * 使用场景：
 * - 访问全局变量
 * - 调用对象方法
 * - 读取配置参数
 * - 实现属性访问
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 * @param[in] k 字段名称，不能为NULL
 *
 * @warning 可能调用元方法，因此可能抛出错误
 * @note 栈增长1个位置
 * @since C89
 * @see lua_gettable(), lua_setfield(), lua_getglobal()
 */
LUA_API void  (lua_getfield) (lua_State *L, int idx, const char *k);

/**
 * @brief 从表中原始获取值（不调用元方法）
 *
 * 详细说明：
 * 从栈中指定位置的表中获取值，键从栈顶获取。不会调用任何元方法，
 * 只进行原始的表查找操作。
 *
 * 原始访问特性：
 * - 不调用__index元方法
 * - 只查找表本身的键
 * - 性能更高，不会抛出错误
 * - 适用于内部实现和性能关键代码
 *
 * 使用场景：
 * - 实现元方法时避免递归
 * - 性能关键的表访问
 * - 调试和内省工具
 * - 绕过元方法的直接访问
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 *
 * @note 栈变化：弹出1个键，推入1个值（净变化：0）
 * @note 不会调用元方法，不会抛出错误
 * @since C89
 * @see lua_gettable(), lua_rawgeti(), lua_rawset()
 */
LUA_API void  (lua_rawget) (lua_State *L, int idx);

/**
 * @brief 从表中获取指定整数索引的值
 *
 * 详细说明：
 * 从栈中指定位置的表中获取指定整数索引的值。这是lua_rawget的
 * 优化版本，专门用于整数键的快速访问。
 *
 * 操作等价于：
 * @code
 * lua_pushinteger(L, n);
 * lua_rawget(L, idx);
 * @endcode
 *
 * 优化特性：
 * - 专门针对整数键优化
 * - 不调用元方法
 * - 性能比通用版本更高
 * - 适用于数组式访问
 *
 * 使用场景：
 * - 访问数组元素
 * - 遍历序列
 * - 实现高性能的数据结构
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 * @param[in] n 整数索引
 *
 * @note 栈增长1个位置
 * @note 不会调用元方法，不会抛出错误
 * @since C89
 * @see lua_rawget(), lua_rawseti(), lua_gettable()
 */
LUA_API void  (lua_rawgeti) (lua_State *L, int idx, int n);

/**
 * @brief 创建新表并推入栈
 *
 * 详细说明：
 * 创建一个新的空表并推入栈顶。可以预先指定表的数组部分和
 * 哈希部分的大小，以优化内存分配和性能。
 *
 * 表结构优化：
 * - narr: 数组部分的预期大小
 * - nrec: 哈希部分的预期大小
 * - 预分配可以避免后续的重新分配
 * - 提高插入操作的性能
 *
 * 内存分配：
 * - 根据提示预分配内存
 * - 如果预估不准确，表会自动调整大小
 * - 0值表示不进行预分配
 *
 * 使用建议：
 * - 如果知道表的大致大小，提供准确的提示
 * - 对于小表，可以都设为0
 * - 数组密集的表应该设置narr
 * - 键值对密集的表应该设置nrec
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] narr 数组部分的预期大小
 * @param[in] nrec 哈希部分的预期大小
 *
 * @note 栈增长1个位置
 * @note 参数只是提示，不是限制
 * @since C89
 * @see lua_newtable(), lua_settable(), lua_gettable()
 */
LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);

/**
 * @brief 创建新的用户数据并推入栈
 *
 * 详细说明：
 * 分配指定大小的用户数据块并推入栈顶。用户数据是Lua中用于
 * 存储C数据的机制，由垃圾回收器管理。
 *
 * 用户数据特性：
 * - 由Lua的垃圾回收器管理
 * - 可以有元表和元方法
 * - 支持__gc元方法进行清理
 * - 内存对齐保证适合任何C类型
 *
 * 内存管理：
 * - 内存由Lua分配和管理
 * - 垃圾回收时自动释放
 * - 可以通过__gc元方法进行清理
 * - 不需要手动释放
 *
 * 使用模式：
 * @code
 * // 创建用户数据
 * MyStruct *data = (MyStruct*)lua_newuserdata(L, sizeof(MyStruct));
 *
 * // 初始化数据
 * data->field1 = value1;
 * data->field2 = value2;
 *
 * // 设置元表（可选）
 * luaL_getmetatable(L, "MyStruct");
 * lua_setmetatable(L, -2);
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] sz 要分配的字节数
 *
 * @return 指向分配内存的指针
 * @retval 非NULL 指向用户数据的指针
 *
 * @note 栈增长1个位置
 * @note 返回的指针已经适当对齐
 * @since C89
 * @see lua_touserdata(), lua_pushlightuserdata(), LUA_TUSERDATA
 */
LUA_API void *(lua_newuserdata) (lua_State *L, size_t sz);

/**
 * @brief 获取对象的元表
 *
 * 详细说明：
 * 获取栈中指定位置对象的元表。如果对象有元表，将元表推入栈顶；
 * 如果没有元表，不推入任何值。
 *
 * 元表支持：
 * - 表：可以有元表
 * - 用户数据：可以有元表
 * - 其他类型：共享类型元表
 *
 * 元表用途：
 * - 定义对象的行为
 * - 实现运算符重载
 * - 提供面向对象编程支持
 * - 控制垃圾回收行为
 *
 * 返回值含义：
 * - 1: 对象有元表，元表已推入栈
 * - 0: 对象没有元表，栈不变
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] objindex 对象的栈索引
 *
 * @return 是否有元表
 * @retval 1 对象有元表，元表已推入栈
 * @retval 0 对象没有元表
 *
 * @note 只有在返回1时栈才会增长
 * @since C89
 * @see lua_setmetatable(), luaL_getmetatable()
 */
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);

/**
 * @brief 获取对象的环境表
 *
 * 详细说明：
 * 获取栈中指定位置对象的环境表并推入栈顶。环境表定义了
 * 对象可以访问的全局变量集合。
 *
 * 环境表支持：
 * - 函数：有独立的环境表
 * - 线程：有独立的环境表
 * - 用户数据：可以有环境表
 *
 * 环境表用途：
 * - 控制函数的全局变量访问
 * - 实现沙箱环境
 * - 提供模块化支持
 * - 隔离不同的执行上下文
 *
 * 默认环境：
 * - 新函数默认使用全局环境
 * - 可以通过lua_setfenv修改
 * - 环境表通常包含标准库函数
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 对象的栈索引
 *
 * @note 栈增长1个位置
 * @note 如果对象没有环境表，推入全局表
 * @since C89
 * @see lua_setfenv(), lua_getglobal()
 */
LUA_API void  (lua_getfenv) (lua_State *L, int idx);

/** @} */

/**
 * @name 表设置函数
 * @brief 向Lua表中设置值的函数组
 * @{
 */

/**
 * @brief 向表中设置值（调用元方法）
 *
 * 详细说明：
 * 向栈中指定位置的表中设置键值对。键和值都从栈顶获取。
 * 如果表有__newindex元方法，会调用该元方法。
 *
 * 操作过程：
 * 1. 从栈顶弹出值
 * 2. 从栈顶弹出键
 * 3. 在指定表中设置键值对
 * 4. 如果键不存在且有__newindex元方法，调用元方法
 *
 * 元方法调用：
 * - 如果键不存在且表有__newindex元方法，调用该元方法
 * - __newindex可以是函数或表
 * - 如果是函数，调用__newindex(table, key, value)
 * - 如果是表，在该表中设置键值对
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 *
 * @warning 可能调用元方法，因此可能抛出错误
 * @note 栈减少2个位置（弹出键和值）
 * @since C89
 * @see lua_rawset(), lua_setfield(), lua_gettable()
 */
LUA_API void  (lua_settable) (lua_State *L, int idx);

/**
 * @brief 向表中设置指定字段的值
 *
 * 详细说明：
 * 向栈中指定位置的表中设置指定名称的字段值。值从栈顶获取。
 * 这是lua_settable的便利版本，直接使用字符串作为键。
 *
 * 操作等价于：
 * @code
 * lua_pushstring(L, k);
 * lua_insert(L, -2);  // 将键移到值下面
 * lua_settable(L, idx);
 * @endcode
 *
 * 字段设置：
 * - 支持任意字符串键
 * - 会调用__newindex元方法（如果存在）
 * - 常用于设置对象的属性和方法
 *
 * 使用场景：
 * - 设置全局变量
 * - 定义对象方法
 * - 配置参数设置
 * - 实现属性赋值
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 * @param[in] k 字段名称，不能为NULL
 *
 * @warning 可能调用元方法，因此可能抛出错误
 * @note 栈减少1个位置（弹出值）
 * @since C89
 * @see lua_settable(), lua_getfield(), lua_setglobal()
 */
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);

/**
 * @brief 向表中原始设置值（不调用元方法）
 *
 * 详细说明：
 * 向栈中指定位置的表中设置键值对，键和值都从栈顶获取。
 * 不会调用任何元方法，只进行原始的表设置操作。
 *
 * 原始设置特性：
 * - 不调用__newindex元方法
 * - 直接在表本身设置键值对
 * - 性能更高，不会抛出错误
 * - 适用于内部实现和性能关键代码
 *
 * 使用场景：
 * - 实现元方法时避免递归
 * - 性能关键的表操作
 * - 初始化表结构
 * - 绕过元方法的直接设置
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 *
 * @note 栈减少2个位置（弹出键和值）
 * @note 不会调用元方法，不会抛出错误
 * @since C89
 * @see lua_settable(), lua_rawseti(), lua_rawget()
 */
LUA_API void  (lua_rawset) (lua_State *L, int idx);

/**
 * @brief 向表中设置指定整数索引的值
 *
 * 详细说明：
 * 向栈中指定位置的表中设置指定整数索引的值。值从栈顶获取。
 * 这是lua_rawset的优化版本，专门用于整数键的快速设置。
 *
 * 操作等价于：
 * @code
 * lua_pushinteger(L, n);
 * lua_insert(L, -2);  // 将键移到值下面
 * lua_rawset(L, idx);
 * @endcode
 *
 * 优化特性：
 * - 专门针对整数键优化
 * - 不调用元方法
 * - 性能比通用版本更高
 * - 适用于数组式操作
 *
 * 使用场景：
 * - 设置数组元素
 * - 构建序列
 * - 实现高性能的数据结构
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 * @param[in] n 整数索引
 *
 * @note 栈减少1个位置（弹出值）
 * @note 不会调用元方法，不会抛出错误
 * @since C89
 * @see lua_rawset(), lua_rawgeti(), lua_settable()
 */
LUA_API void  (lua_rawseti) (lua_State *L, int idx, int n);

/**
 * @brief 设置对象的元表
 *
 * 详细说明：
 * 为栈中指定位置的对象设置元表。元表从栈顶获取。
 * 元表定义了对象的行为和操作符重载。
 *
 * 元表设置规则：
 * - 表：可以设置任意元表
 * - 用户数据：可以设置任意元表
 * - 其他类型：只能设置类型共享的元表
 *
 * 元表功能：
 * - 运算符重载（__add、__sub等）
 * - 索引控制（__index、__newindex）
 * - 垃圾回收（__gc）
 * - 字符串转换（__tostring）
 * - 函数调用（__call）
 *
 * 安全限制：
 * - 某些类型的元表设置可能被限制
 * - 保护模式下可能有额外限制
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] objindex 对象的栈索引
 *
 * @return 是否设置成功
 * @retval 1 元表设置成功
 * @retval 0 元表设置失败（类型不支持或被保护）
 *
 * @note 栈减少1个位置（弹出元表）
 * @since C89
 * @see lua_getmetatable(), luaL_newmetatable()
 */
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);

/**
 * @brief 设置对象的环境表
 *
 * 详细说明：
 * 为栈中指定位置的对象设置环境表。环境表从栈顶获取。
 * 环境表控制对象可以访问的全局变量集合。
 *
 * 环境表设置：
 * - 函数：设置函数的全局变量访问环境
 * - 线程：设置线程的全局环境
 * - 用户数据：设置用户数据的环境
 *
 * 环境表用途：
 * - 实现沙箱环境
 * - 模块化编程
 * - 安全控制
 * - 上下文隔离
 *
 * 设置效果：
 * - 影响全局变量的查找
 * - 影响require等函数的行为
 * - 不影响局部变量和上值
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 对象的栈索引
 *
 * @return 是否设置成功
 * @retval 1 环境表设置成功
 * @retval 0 环境表设置失败（对象类型不支持）
 *
 * @note 栈减少1个位置（弹出环境表）
 * @since C89
 * @see lua_getfenv(), lua_setglobal()
 */
LUA_API int   (lua_setfenv) (lua_State *L, int idx);

/** @} */

/**
 * @name 函数调用和代码执行
 * @brief 用于调用函数和执行Lua代码的函数组
 * @{
 */

/**
 * @brief 调用函数（不保护，错误会传播）
 *
 * 详细说明：
 * 调用栈中的函数，不进行错误保护。如果函数执行过程中发生错误，
 * 错误会直接传播到调用者，可能导致程序终止。
 *
 * 调用过程：
 * 1. 函数必须在栈中，参数在函数之上
 * 2. 调用完成后，函数和参数被移除
 * 3. 结果被推入栈中
 * 4. 如果发生错误，执行longjmp
 *
 * 栈布局（调用前）：
 * @code
 * ... | function | arg1 | arg2 | ... | argN |
 * @endcode
 *
 * 栈布局（调用后）：
 * @code
 * ... | result1 | result2 | ... | resultM |
 * @endcode
 *
 * 参数说明：
 * - nargs: 参数数量
 * - nresults: 期望的结果数量，LUA_MULTRET表示所有结果
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] nargs 参数数量
 * @param[in] nresults 期望的结果数量，LUA_MULTRET表示所有结果
 *
 * @warning 错误会导致longjmp，可能不会返回
 * @note 栈变化：移除函数和参数，推入结果
 * @since C89
 * @see lua_pcall(), lua_cpcall(), LUA_MULTRET
 */
LUA_API void  (lua_call) (lua_State *L, int nargs, int nresults);

/**
 * @brief 保护模式调用函数（捕获错误）
 *
 * 详细说明：
 * 在保护模式下调用栈中的函数。如果函数执行过程中发生错误，
 * 错误会被捕获并返回错误码，不会导致程序终止。
 *
 * 错误处理：
 * - 如果指定了错误处理函数，会调用该函数处理错误
 * - 错误处理函数接收错误消息作为参数
 * - 错误处理函数的返回值会被推入栈顶
 *
 * 返回值含义：
 * - 0: 调用成功
 * - LUA_ERRRUN: 运行时错误
 * - LUA_ERRMEM: 内存分配错误
 * - LUA_ERRERR: 错误处理函数本身出错
 *
 * 使用示例：
 * @code
 * lua_pushcfunction(L, my_function);
 * lua_pushstring(L, "argument");
 * int result = lua_pcall(L, 1, 1, 0);
 * if (result != 0) {
 *     printf("Error: %s\n", lua_tostring(L, -1));
 *     lua_pop(L, 1);  // 移除错误消息
 * }
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] nargs 参数数量
 * @param[in] nresults 期望的结果数量，LUA_MULTRET表示所有结果
 * @param[in] errfunc 错误处理函数的栈索引，0表示无错误处理函数
 *
 * @return 调用结果状态码
 * @retval 0 调用成功
 * @retval LUA_ERRRUN 运行时错误
 * @retval LUA_ERRMEM 内存分配错误
 * @retval LUA_ERRERR 错误处理函数出错
 *
 * @note 这是最常用的函数调用方式
 * @since C89
 * @see lua_call(), lua_cpcall(), LUA_ERRRUN, LUA_ERRMEM
 */
LUA_API int   (lua_pcall) (lua_State *L, int nargs, int nresults, int errfunc);

/**
 * @brief 保护模式调用C函数
 *
 * 详细说明：
 * 在保护模式下调用指定的C函数。这是一个便利函数，用于安全地
 * 调用C函数而不需要先将其推入栈中。
 *
 * 调用特性：
 * - 不需要预先将函数推入栈
 * - 自动进行错误保护
 * - 可以传递用户数据给C函数
 * - 适用于回调和工具函数
 *
 * 函数签名：
 * - C函数必须符合lua_CFunction签名
 * - 函数可以通过lua_touserdata(L, 1)获取用户数据
 * - 函数应该返回结果数量
 *
 * 错误处理：
 * - 如果C函数调用lua_error，错误会被捕获
 * - 返回值指示调用是否成功
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] func 要调用的C函数，不能为NULL
 * @param[in] ud 传递给C函数的用户数据，可以为NULL
 *
 * @return 调用结果状态码
 * @retval 0 调用成功
 * @retval LUA_ERRRUN 运行时错误
 * @retval LUA_ERRMEM 内存分配错误
 *
 * @note 用户数据会作为轻量用户数据推入栈顶供C函数使用
 * @since C89
 * @see lua_pcall(), lua_call(), lua_CFunction
 */
LUA_API int   (lua_cpcall) (lua_State *L, lua_CFunction func, void *ud);

/**
 * @brief 加载Lua代码块
 *
 * 详细说明：
 * 从数据源加载Lua代码（源代码或字节码）并编译为函数。
 * 编译后的函数会被推入栈顶，但不会执行。
 *
 * 加载过程：
 * 1. 通过reader函数读取代码数据
 * 2. 解析并编译代码
 * 3. 将编译后的函数推入栈顶
 * 4. 返回编译结果状态
 *
 * 数据源类型：
 * - Lua源代码：文本格式的Lua脚本
 * - Lua字节码：预编译的二进制格式
 * - 自动检测：根据数据头部自动识别
 *
 * 错误类型：
 * - 语法错误：代码语法不正确
 * - 内存错误：编译过程中内存不足
 * - 读取错误：reader函数返回错误
 *
 * 使用示例：
 * @code
 * // 从字符串加载
 * int result = luaL_loadstring(L, "return 1 + 2");
 * if (result == 0) {
 *     lua_call(L, 0, 1);  // 执行函数
 *     int value = lua_tointeger(L, -1);
 * }
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] reader 读取器函数，用于获取代码数据
 * @param[in] dt 传递给读取器的用户数据
 * @param[in] chunkname 代码块名称，用于错误报告和调试
 *
 * @return 加载结果状态码
 * @retval 0 加载成功，函数已推入栈
 * @retval LUA_ERRSYNTAX 语法错误
 * @retval LUA_ERRMEM 内存分配错误
 *
 * @note 成功时栈增长1个位置（推入函数）
 * @note 失败时错误消息会被推入栈顶
 * @since C89
 * @see lua_Reader, lua_dump(), luaL_loadstring()
 */
LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt,
                                        const char *chunkname);

/**
 * @brief 将函数转储为字节码
 *
 * 详细说明：
 * 将栈顶的Lua函数转储为字节码格式。字节码通过writer函数
 * 写入到目标位置，可以用于保存预编译的代码。
 *
 * 转储特性：
 * - 只能转储Lua函数，不能转储C函数
 * - 生成的字节码与平台相关
 * - 字节码可以通过lua_load重新加载
 * - 转储过程不会修改栈
 *
 * 字节码格式：
 * - 二进制格式，包含函数的完整信息
 * - 包含指令序列、常量表、调试信息等
 * - 格式版本与Lua版本相关
 *
 * 使用场景：
 * - 预编译Lua脚本以提高加载速度
 * - 保护源代码（一定程度上）
 * - 减少分发包的大小
 * - 实现代码缓存机制
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] writer 写入器函数，用于输出字节码数据
 * @param[in] data 传递给写入器的用户数据
 *
 * @return 转储结果状态码
 * @retval 0 转储成功
 * @retval 非0 转储失败（通常是写入器返回的错误码）
 *
 * @note 栈顶必须是Lua函数
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_Writer, lua_load(), LUA_SIGNATURE
 */
LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data);

/** @} */

/**
 * @name 协程控制函数
 * @brief 用于控制协程执行的函数组
 * @{
 */

/**
 * @brief 挂起协程
 *
 * 详细说明：
 * 挂起当前协程的执行，将控制权返回给调用者。协程可以稍后通过
 * lua_resume恢复执行。这是实现协作式多任务的核心机制。
 *
 * 挂起过程：
 * 1. 保存当前执行状态
 * 2. 将指定数量的返回值传递给调用者
 * 3. 暂停协程执行
 * 4. 返回到lua_resume的调用点
 *
 * 返回值传递：
 * - nresults指定要返回的值的数量
 * - 返回值从栈顶获取
 * - 这些值会成为lua_resume的返回值
 *
 * 使用场景：
 * - 实现生成器模式
 * - 异步I/O操作
 * - 状态机实现
 * - 协作式任务调度
 *
 * 注意事项：
 * - 只能在协程中调用
 * - 不能在C函数中直接调用（需要通过lua_yieldk）
 * - 挂起后协程状态变为LUA_YIELD
 *
 * @param[in] L 协程状态机指针，不能为NULL
 * @param[in] nresults 要返回的值的数量
 *
 * @return 永远不会返回（协程被挂起）
 *
 * @warning 只能在协程中调用，不能在主线程中调用
 * @note 栈顶的nresults个值会被传递给调用者
 * @since C89
 * @see lua_resume(), lua_status(), lua_newthread()
 */
LUA_API int  (lua_yield) (lua_State *L, int nresults);

/**
 * @brief 恢复协程执行
 *
 * 详细说明：
 * 恢复指定协程的执行。协程从上次挂起的位置继续执行，
 * 可以传递参数给协程。
 *
 * 恢复过程：
 * 1. 将栈顶的参数传递给协程
 * 2. 恢复协程执行
 * 3. 协程执行直到完成或再次挂起
 * 4. 返回执行结果状态
 *
 * 参数传递：
 * - narg指定传递给协程的参数数量
 * - 参数从栈顶获取
 * - 首次启动时，参数传递给协程的主函数
 * - 恢复时，参数成为lua_yield的返回值
 *
 * 返回值状态：
 * - 0: 协程正常完成
 * - LUA_YIELD: 协程再次挂起
 * - LUA_ERRRUN: 协程执行错误
 * - LUA_ERRMEM: 内存分配错误
 * - LUA_ERRERR: 错误处理函数出错
 *
 * 使用示例：
 * @code
 * // 创建协程
 * lua_State *co = lua_newthread(L);
 * lua_pushcfunction(co, my_coroutine);
 *
 * // 启动协程
 * int status = lua_resume(co, 0);
 * while (status == LUA_YIELD) {
 *     // 处理协程返回的值
 *     process_yield_values(co);
 *
 *     // 恢复协程
 *     status = lua_resume(co, 0);
 * }
 * @endcode
 *
 * @param[in] L 协程状态机指针，不能为NULL
 * @param[in] narg 传递给协程的参数数量
 *
 * @return 协程执行状态
 * @retval 0 协程正常完成
 * @retval LUA_YIELD 协程挂起
 * @retval LUA_ERRRUN 运行时错误
 * @retval LUA_ERRMEM 内存分配错误
 * @retval LUA_ERRERR 错误处理函数出错
 *
 * @note 协程的返回值会被推入调用者的栈中
 * @since C89
 * @see lua_yield(), lua_status(), lua_newthread()
 */
LUA_API int  (lua_resume) (lua_State *L, int narg);

/**
 * @brief 获取协程状态
 *
 * 详细说明：
 * 获取指定协程的当前执行状态。状态反映了协程是否正在运行、
 * 挂起、完成或发生错误。
 *
 * 状态类型：
 * - 0: 协程正常完成或尚未启动
 * - LUA_YIELD: 协程已挂起
 * - LUA_ERRRUN: 协程发生运行时错误
 * - LUA_ERRSYNTAX: 协程发生语法错误
 * - LUA_ERRMEM: 协程发生内存错误
 * - LUA_ERRERR: 协程的错误处理函数出错
 *
 * 状态判断：
 * - 新创建的协程状态为0
 * - 正常完成的协程状态为0
 * - 挂起的协程状态为LUA_YIELD
 * - 出错的协程状态为相应的错误码
 *
 * 使用场景：
 * - 检查协程是否可以恢复
 * - 实现协程调度器
 * - 错误处理和调试
 * - 状态机管理
 *
 * @param[in] L 协程状态机指针，不能为NULL
 *
 * @return 协程的当前状态
 * @retval 0 协程正常或尚未启动
 * @retval LUA_YIELD 协程已挂起
 * @retval LUA_ERRRUN 运行时错误
 * @retval LUA_ERRSYNTAX 语法错误
 * @retval LUA_ERRMEM 内存错误
 * @retval LUA_ERRERR 错误处理函数错误
 *
 * @note 此函数不会修改栈
 * @since C89
 * @see lua_resume(), lua_yield(), LUA_YIELD
 */
LUA_API int  (lua_status) (lua_State *L);

/** @} */

/**
 * @name 垃圾回收控制常量
 * @brief 用于控制垃圾回收器行为的选项
 * @{
 */
#define LUA_GCSTOP          0    /**< 停止垃圾回收器 */
#define LUA_GCRESTART       1    /**< 重启垃圾回收器 */
#define LUA_GCCOLLECT       2    /**< 执行完整的垃圾回收 */
#define LUA_GCCOUNT         3    /**< 获取内存使用量（KB） */
#define LUA_GCCOUNTB        4    /**< 获取内存使用量的余数（字节） */
#define LUA_GCSTEP          5    /**< 执行一步增量垃圾回收 */
#define LUA_GCSETPAUSE      6    /**< 设置垃圾回收暂停参数 */
#define LUA_GCSETSTEPMUL    7    /**< 设置垃圾回收步长倍数 */
/** @} */

/**
 * @name 垃圾回收和其他函数
 * @brief 垃圾回收控制和其他实用函数
 * @{
 */

/**
 * @brief 控制垃圾回收器
 *
 * 详细说明：
 * 控制Lua垃圾回收器的行为。可以启动、停止、配置垃圾回收器，
 * 或者获取内存使用信息。
 *
 * 控制选项：
 * - LUA_GCSTOP: 停止垃圾回收器
 * - LUA_GCRESTART: 重启垃圾回收器
 * - LUA_GCCOLLECT: 执行完整的垃圾回收
 * - LUA_GCCOUNT: 获取内存使用量（KB）
 * - LUA_GCCOUNTB: 获取内存使用量的余数（字节）
 * - LUA_GCSTEP: 执行一步增量垃圾回收
 * - LUA_GCSETPAUSE: 设置垃圾回收暂停参数
 * - LUA_GCSETSTEPMUL: 设置垃圾回收步长倍数
 *
 * 参数说明：
 * - data参数的含义取决于what参数
 * - 对于设置操作，data是新的参数值
 * - 对于查询操作，data通常被忽略
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] what 垃圾回收操作类型
 * @param[in] data 操作参数，含义取决于what
 *
 * @return 操作结果，含义取决于what参数
 *
 * @since C89
 * @see LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT
 */
LUA_API int (lua_gc) (lua_State *L, int what, int data);

/**
 * @brief 抛出错误
 *
 * 详细说明：
 * 抛出一个Lua错误。错误消息从栈顶获取。这个函数永远不会返回，
 * 而是执行longjmp到最近的保护调用点。
 *
 * 错误处理：
 * - 错误消息必须在栈顶
 * - 函数执行longjmp，不会返回
 * - 错误会被最近的lua_pcall捕获
 * - 如果没有保护调用，会调用恐慌函数
 *
 * 使用场景：
 * - C函数中报告错误
 * - 参数验证失败
 * - 资源分配失败
 * - 业务逻辑错误
 *
 * @param[in] L Lua状态机指针，不能为NULL
 *
 * @return 永远不会返回
 *
 * @warning 此函数永远不会返回，执行longjmp
 * @note 栈顶必须有错误消息
 * @since C89
 * @see lua_pcall(), luaL_error()
 */
LUA_API int   (lua_error) (lua_State *L);

/**
 * @brief 遍历表的下一个键值对
 *
 * 详细说明：
 * 遍历表中的键值对。这是实现表迭代的基础函数，
 * 用于实现pairs()函数和for循环。
 *
 * 遍历过程：
 * 1. 将键推入栈顶（nil表示开始遍历）
 * 2. 调用lua_next
 * 3. 如果返回非0，栈顶是值，下面是键
 * 4. 处理键值对
 * 5. 弹出值，保留键，继续下一次迭代
 *
 * 使用示例：
 * @code
 * lua_pushnil(L);  // 第一个键
 * while (lua_next(L, -2) != 0) {
 *     // 栈：... table key value
 *     printf("key: %s, value: %s\n",
 *            lua_tostring(L, -2), lua_tostring(L, -1));
 *     lua_pop(L, 1);  // 移除值，保留键
 * }
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 表的栈索引
 *
 * @return 是否还有下一个键值对
 * @retval 0 遍历结束，栈顶的键被弹出
 * @retval 非0 找到下一个键值对，键和值在栈顶
 *
 * @warning 遍历过程中不要修改表结构
 * @since C89
 * @see lua_gettable(), lua_settable()
 */
LUA_API int   (lua_next) (lua_State *L, int idx);

/**
 * @brief 连接栈顶的n个值
 *
 * 详细说明：
 * 将栈顶的n个值连接成一个字符串。所有值都会被转换为字符串，
 * 然后按顺序连接。结果字符串推入栈顶。
 *
 * 连接规则：
 * - 数字自动转换为字符串
 * - 字符串直接使用
 * - 其他类型调用__tostring元方法
 * - 如果无法转换，抛出错误
 *
 * 性能考虑：
 * - 使用高效的字符串连接算法
 * - 避免多次内存分配
 * - 对于大量字符串连接，比手动连接更高效
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] n 要连接的值的数量
 *
 * @note 栈变化：移除n个值，推入1个连接结果
 * @warning 可能调用元方法，因此可能抛出错误
 * @since C89
 * @see lua_tolstring(), lua_pushstring()
 */
LUA_API void  (lua_concat) (lua_State *L, int n);

/**
 * @brief 获取当前的内存分配器
 *
 * 详细说明：
 * 获取当前Lua状态机使用的内存分配器函数和用户数据。
 * 这用于检查或替换内存分配策略。
 *
 * 返回信息：
 * - 返回值：当前的分配器函数指针
 * - ud参数：输出当前的用户数据指针
 *
 * 使用场景：
 * - 内存使用监控
 * - 自定义内存管理
 * - 调试内存问题
 * - 实现内存池
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[out] ud 输出用户数据指针，可以为NULL
 *
 * @return 当前的内存分配器函数指针
 *
 * @since C89
 * @see lua_setallocf(), lua_newstate(), lua_Alloc
 */
LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);

/**
 * @brief 设置内存分配器
 *
 * 详细说明：
 * 设置Lua状态机使用的内存分配器函数和用户数据。
 * 新的分配器将用于所有后续的内存分配操作。
 *
 * 分配器要求：
 * - 必须符合lua_Alloc函数签名
 * - 必须正确处理所有分配、重分配、释放操作
 * - 必须在失败时返回NULL
 * - 必须是线程安全的（如果在多线程环境中使用）
 *
 * 注意事项：
 * - 设置后立即生效
 * - 不会影响已分配的内存
 * - 新分配器负责所有后续内存操作
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] f 新的内存分配器函数，不能为NULL
 * @param[in] ud 传递给分配器的用户数据，可以为NULL
 *
 * @since C89
 * @see lua_getallocf(), lua_newstate(), lua_Alloc
 */
LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud);

/** @} */


/**
 * @name 便利宏定义
 * @brief 提供常用操作的简化宏定义
 * @{
 */

/** @brief 弹出栈顶的n个元素 */
#define lua_pop(L,n)            lua_settop(L, -(n)-1)

/** @brief 创建空表 */
#define lua_newtable(L)         lua_createtable(L, 0, 0)

/** @brief 注册C函数为全局函数 */
#define lua_register(L,n,f)     (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))

/** @brief 推入C函数（无上值的闭包） */
#define lua_pushcfunction(L,f)  lua_pushcclosure(L, (f), 0)

/** @brief 获取字符串长度（兼容性宏） */
#define lua_strlen(L,i)         lua_objlen(L, (i))

/** @brief 检查是否为函数类型 */
#define lua_isfunction(L,n)     (lua_type(L, (n)) == LUA_TFUNCTION)
/** @brief 检查是否为表类型 */
#define lua_istable(L,n)        (lua_type(L, (n)) == LUA_TTABLE)
/** @brief 检查是否为轻量用户数据类型 */
#define lua_islightuserdata(L,n) (lua_type(L, (n)) == LUA_TLIGHTUSERDATA)
/** @brief 检查是否为nil类型 */
#define lua_isnil(L,n)          (lua_type(L, (n)) == LUA_TNIL)
/** @brief 检查是否为布尔类型 */
#define lua_isboolean(L,n)      (lua_type(L, (n)) == LUA_TBOOLEAN)
/** @brief 检查是否为线程类型 */
#define lua_isthread(L,n)       (lua_type(L, (n)) == LUA_TTHREAD)
/** @brief 检查是否为无效类型 */
#define lua_isnone(L,n)         (lua_type(L, (n)) == LUA_TNONE)
/** @brief 检查是否为无效或nil类型 */
#define lua_isnoneornil(L, n)   (lua_type(L, (n)) <= 0)

/** @brief 推入字符串字面量 */
#define lua_pushliteral(L, s)   \
    lua_pushlstring(L, "" s, (sizeof(s)/sizeof(char))-1)

/** @brief 设置全局变量 */
#define lua_setglobal(L,s)      lua_setfield(L, LUA_GLOBALSINDEX, (s))
/** @brief 获取全局变量 */
#define lua_getglobal(L,s)      lua_getfield(L, LUA_GLOBALSINDEX, (s))

/** @brief 转换为字符串（不返回长度） */
#define lua_tostring(L,i)       lua_tolstring(L, (i), NULL)

/** @} */

/**
 * @name 兼容性宏和函数
 * @brief 提供向后兼容性的宏定义
 * @{
 */

/** @brief 创建新状态机（兼容性宏） */
#define lua_open()              luaL_newstate()

/** @brief 获取注册表 */
#define lua_getregistry(L)      lua_pushvalue(L, LUA_REGISTRYINDEX)

/** @brief 获取垃圾回收器内存使用量 */
#define lua_getgccount(L)       lua_gc(L, LUA_GCCOUNT, 0)

/** @brief 读取器类型别名（兼容性） */
#define lua_Chunkreader         lua_Reader
/** @brief 写入器类型别名（兼容性） */
#define lua_Chunkwriter         lua_Writer

/** @brief 设置调试级别（内部函数） */
LUA_API void lua_setlevel (lua_State *from, lua_State *to);

/** @} */


/**
 * @name 调试API - 事件类型常量
 * @brief 定义调试钩子函数可以捕获的事件类型
 * @{
 */
#define LUA_HOOKCALL        0    /**< 函数调用事件 */
#define LUA_HOOKRET         1    /**< 函数返回事件 */
#define LUA_HOOKLINE        2    /**< 行执行事件 */
#define LUA_HOOKCOUNT       3    /**< 指令计数事件 */
#define LUA_HOOKTAILRET     4    /**< 尾调用返回事件 */
/** @} */

/**
 * @name 调试API - 事件掩码常量
 * @brief 用于设置调试钩子的事件掩码
 * @{
 */
#define LUA_MASKCALL        (1 << LUA_HOOKCALL)   /**< 函数调用掩码 */
#define LUA_MASKRET         (1 << LUA_HOOKRET)    /**< 函数返回掩码 */
#define LUA_MASKLINE        (1 << LUA_HOOKLINE)   /**< 行执行掩码 */
#define LUA_MASKCOUNT       (1 << LUA_HOOKCOUNT)  /**< 指令计数掩码 */
/** @} */

/**
 * @brief 调试信息结构体：包含函数调用的详细调试信息
 *
 * 详细说明：
 * lua_Debug结构体包含了Lua函数调用的完整调试信息，
 * 用于调试器、性能分析器和错误报告系统。
 *
 * @since C89
 * @see lua_getstack(), lua_getinfo()
 */
typedef struct lua_Debug lua_Debug;

/**
 * @brief 调试钩子函数类型：处理调试事件的回调函数
 *
 * 详细说明：
 * 调试钩子函数在特定事件发生时被调用，可以用于实现
 * 调试器、代码覆盖率分析、性能监控等功能。
 *
 * @param L Lua状态机指针
 * @param ar 调试信息结构体指针
 *
 * @since C89
 * @see lua_sethook(), lua_gethook()
 */
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);

/**
 * @name 调试API函数
 * @brief 用于获取和设置调试信息的函数组
 * @{
 */

/**
 * @brief 获取调用栈信息
 *
 * 详细说明：
 * 获取指定层级的调用栈信息。这是调试API的入口点，
 * 用于获取函数调用的基本信息。
 *
 * 层级说明：
 * - 0: 当前函数
 * - 1: 调用当前函数的函数
 * - 2: 调用上一级函数的函数
 * - 以此类推
 *
 * 获取的信息：
 * - 填充lua_Debug结构的基本字段
 * - 为后续lua_getinfo调用做准备
 * - 不包含详细信息，需要进一步查询
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] level 调用栈层级，0表示当前函数
 * @param[out] ar 调试信息结构体指针，不能为NULL
 *
 * @return 是否成功获取栈信息
 * @retval 1 成功获取栈信息
 * @retval 0 指定层级无效或超出栈范围
 *
 * @since C89
 * @see lua_getinfo(), lua_Debug
 */
LUA_API int lua_getstack (lua_State *L, int level, lua_Debug *ar);

/**
 * @brief 获取详细的调试信息
 *
 * 详细说明：
 * 根据指定的选项获取详细的调试信息。必须先调用lua_getstack
 * 或将函数推入栈中。
 *
 * 信息选项：
 * - 'n': 获取函数名称信息（name, namewhat）
 * - 'S': 获取源代码信息（source, what, linedefined等）
 * - 'l': 获取当前行号（currentline）
 * - 'u': 获取上值数量（nups）
 * - 'f': 将函数推入栈顶
 *
 * 组合使用：
 * - 可以组合多个选项，如"nSl"
 * - 按需获取信息，提高性能
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] what 信息选项字符串，不能为NULL
 * @param[in,out] ar 调试信息结构体指针，不能为NULL
 *
 * @return 是否成功获取信息
 * @retval 1 成功获取信息
 * @retval 0 获取信息失败
 *
 * @note 'f'选项会修改栈（推入函数）
 * @since C89
 * @see lua_getstack(), lua_Debug
 */
LUA_API int lua_getinfo (lua_State *L, const char *what, lua_Debug *ar);

/**
 * @brief 获取局部变量信息
 *
 * 详细说明：
 * 获取指定函数中指定索引的局部变量名称和值。
 * 这用于调试器显示局部变量信息。
 *
 * 变量索引：
 * - 从1开始计数
 * - 按声明顺序排列
 * - 包括参数和局部变量
 *
 * 返回信息：
 * - 返回值：变量名称字符串
 * - 栈顶：变量的值
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] ar 调试信息结构体指针，不能为NULL
 * @param[in] n 局部变量索引，从1开始
 *
 * @return 变量名称字符串指针
 * @retval 非NULL 变量名称，变量值已推入栈
 * @retval NULL 指定索引无效或超出范围
 *
 * @note 成功时栈增长1个位置（变量值）
 * @since C89
 * @see lua_setlocal(), lua_getstack()
 */
LUA_API const char *lua_getlocal (lua_State *L, const lua_Debug *ar, int n);

/**
 * @brief 设置局部变量值
 *
 * 详细说明：
 * 设置指定函数中指定索引的局部变量的值。
 * 新值从栈顶获取。这用于调试器修改变量值。
 *
 * 设置过程：
 * - 从栈顶获取新值
 * - 设置到指定的局部变量
 * - 弹出栈顶的值
 *
 * 限制条件：
 * - 只能设置存在的局部变量
 * - 不能创建新的局部变量
 * - 变量类型可以改变
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] ar 调试信息结构体指针，不能为NULL
 * @param[in] n 局部变量索引，从1开始
 *
 * @return 变量名称字符串指针
 * @retval 非NULL 变量名称，设置成功
 * @retval NULL 指定索引无效或设置失败
 *
 * @note 栈减少1个位置（弹出新值）
 * @since C89
 * @see lua_getlocal(), lua_getstack()
 */
LUA_API const char *lua_setlocal (lua_State *L, const lua_Debug *ar, int n);

/** @brief 获取上值信息 */
LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n);

/** @brief 设置上值 */
LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n);

/** @brief 设置调试钩子函数 */
LUA_API int lua_sethook (lua_State *L, lua_Hook func, int mask, int count);

/** @brief 获取当前的调试钩子函数 */
LUA_API lua_Hook lua_gethook (lua_State *L);

/** @brief 获取调试钩子的事件掩码 */
LUA_API int lua_gethookmask (lua_State *L);

/** @brief 获取调试钩子的指令计数 */
LUA_API int lua_gethookcount (lua_State *L);

/** @} */

/**
 * @brief 调试信息结构体定义：包含函数执行的详细信息
 *
 * 结构体成员说明：
 * - event: 触发的事件类型
 * - name: 函数名称（如果可用）
 * - namewhat: 名称类型（global、local、field、method等）
 * - what: 函数类型（Lua、C、main、tail）
 * - source: 源代码位置
 * - currentline: 当前执行行号
 * - nups: 上值数量
 * - linedefined: 函数定义开始行号
 * - lastlinedefined: 函数定义结束行号
 * - short_src: 简短的源代码标识
 * - i_ci: 内部调用信息索引
 *
 * @since C89
 */
struct lua_Debug {
    int event;                      /**< 事件类型 */
    const char *name;               /**< 函数名称 */
    const char *namewhat;           /**< 名称类型 */
    const char *what;               /**< 函数类型 */
    const char *source;             /**< 源代码位置 */
    int currentline;                /**< 当前行号 */
    int nups;                       /**< 上值数量 */
    int linedefined;                /**< 定义开始行号 */
    int lastlinedefined;            /**< 定义结束行号 */
    char short_src[LUA_IDSIZE];     /**< 简短源标识 */
    int i_ci;                       /**< 内部调用信息 */
};

#endif
