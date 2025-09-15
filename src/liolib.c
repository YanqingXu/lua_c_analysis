/**
 * @file liolib.c
 * @brief Lua标准I/O库：实现文件操作和标准输入输出功能
 * 
 * 详细说明：
 * 这个文件实现了Lua编程语言的标准I/O库，提供了完整的文件操作功能，
 * 包括文件打开、读写、关闭、以及标准输入输出重定向等功能。
 * 该库是Lua标准库的重要组成部分，为Lua程序提供了与操作系统文件系统
 * 交互的能力。
 * 
 * 系统架构定位：
 * liolib作为Lua的标准库模块，位于Lua核心引擎之上，为用户程序提供
 * 高级的文件I/O接口。它封装了C标准库的文件操作函数，并提供了
 * Lua风格的错误处理和内存管理。
 * 
 * 技术特点：
 * - 完整的文件操作支持：文本和二进制模式
 * - 自动资源管理：通过元表和垃圾回收自动关闭文件
 * - 错误处理机制：符合Lua惯例的错误处理模式
 * - 缓冲I/O优化：利用C标准库的缓冲机制提升性能
 * - 跨平台兼容：处理不同操作系统的文件系统差异
 * 
 * 依赖关系：
 * - 系统头文件：errno.h, stdio.h, stdlib.h, string.h
 * - Lua核心：lua.h, lauxlib.h, lualib.h
 * - C标准库：文件操作、内存管理、字符串处理函数
 * 
 * 编译要求：
 * - C标准：C99或更高版本
 * - 系统依赖：POSIX兼容的文件系统API
 * - 链接库：标准C运行时库
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local file = io.open("data.txt", "r")
 * if file then
 *     local content = file:read("*all")
 *     file:close()
 *     print("文件内容:", content)
 * else
 *     print("无法打开文件")
 * end
 * 
 * // 使用标准输入输出
 * io.write("请输入您的姓名: ")
 * local name = io.read()
 * io.write("您好, " .. name .. "!\n")
 * @endcode
 * 
 * 内存安全考虑：
 * - 文件句柄通过Lua的垃圾回收机制自动管理
 * - 缓冲区大小检查，防止缓冲区溢出
 * - 错误路径上的资源清理保证
 * - 文件描述符泄漏预防
 * 
 * 性能特征：
 * - 利用C标准库的缓冲I/O，提供良好的性能
 * - 大文件读取支持，避免内存耗尽
 * - 流式处理支持，适合处理大数据集
 * - 最小化系统调用次数
 * 
 * 线程安全性：
 * 文件操作本身不是线程安全的，多线程环境下需要适当的同步机制。
 * 每个Lua状态机应该在单独的线程中使用。
 * 
 * 注意事项：
 * - 文件路径的平台兼容性问题
 * - 文件编码和字符集处理
 * - 大文件操作的内存限制
 * - 网络文件系统的特殊考虑
 * 
 * @author Roberto Ierusalimschy
 * @version 5.1.5
 * @date 2010-05-14
 * @since Lua 5.0
 * @see lua.h, lauxlib.h, lualib.h
 */


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define liolib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"



#define IO_INPUT	1
#define IO_OUTPUT	2


static const char *const fnames[] = {"input", "output"};

/**
 * @brief 操作结果处理函数：标准化I/O操作的返回值处理
 * 
 * 详细说明：
 * 这个函数实现了Lua I/O库中统一的操作结果处理模式。根据操作是否成功，
 * 向Lua栈推送相应的返回值。成功时返回布尔值true，失败时返回nil和错误信息。
 * 
 * 返回值模式：
 * 成功时：返回1个值
 * - true（布尔值）
 * 
 * 失败时：返回3个值
 * - nil
 * - 错误消息字符串（包含文件名和系统错误描述）
 * - 错误代码（整数，对应errno值）
 * 
 * 错误处理策略：
 * 函数在进入时立即保存errno值，防止后续的Lua API调用修改errno。
 * 这确保了错误信息的准确性和一致性。
 * 
 * 使用场景：
 * - 文件操作函数（打开、读取、写入、关闭）
 * - 系统调用包装函数
 * - 需要统一错误处理的I/O操作
 * 
 * @param[in] L Lua状态机指针，用于栈操作和字符串格式化
 * @param[in] i 操作结果标志，非零表示成功，零表示失败
 * @param[in] filename 相关的文件名，用于错误消息；可以为NULL
 * 
 * @return 推送到栈上的值的数量
 * @retval 1 操作成功，推送了1个布尔值true
 * @retval 3 操作失败，推送了nil、错误消息和错误代码
 * 
 * @pre L != NULL
 * @post 栈上增加了1个或3个值，具体取决于操作结果
 * 
 * @note 函数会保存并使用调用时的errno值
 * @warning 必须在引起错误的系统调用之后立即调用此函数
 * 
 * @since Lua 5.0
 * @see fileerror(), strerror()
 */
static int pushresult(lua_State *L, int i, const char *filename) {
    int en = errno;  /* calls to Lua API may change this value */
    if (i) {
        lua_pushboolean(L, 1);
        return 1;
    }
    else {
        lua_pushnil(L);
        if (filename)
            lua_pushfstring(L, "%s: %s", filename, strerror(en));
        else
            lua_pushfstring(L, "%s", strerror(en));
        lua_pushinteger(L, en);
        return 3;
    }
}


/**
 * @brief 文件错误处理函数，生成带文件名的错误信息并抛出参数错误
 * @details 这是一个专用的错误处理函数，用于处理文件操作相关的错误。
 *          它会结合文件名和系统错误信息生成详细的错误描述，
 *          然后通过luaL_argerror抛出Lua参数错误。
 * 
 * @param L Lua虚拟机状态指针，用于错误处理和字符串操作
 * @param arg 发生错误的参数位置索引，用于错误定位
 * @param filename 相关的文件名，将包含在错误信息中
 * 
 * @return void 此函数不返回，会直接抛出Lua错误
 * 
 * @note 错误信息格式：
 *       - 格式："filename: error_message"
 *       - filename: 用户提供的文件名
 *       - error_message: 来自strerror(errno)的系统错误描述
 * 
 * @note 实现细节：
 *       - 使用lua_pushfstring格式化错误消息
 *       - 调用strerror(errno)获取系统错误描述
 *       - 通过luaL_argerror抛出参数错误，不会返回
 *       - 错误信息会包含具体的文件名和系统错误原因
 * 
 * @warning 注意事项：
 *          - 此函数会立即终止当前执行并抛出错误
 *          - 调用前应确保errno包含有效的错误代码
 *          - filename参数不应为NULL
 *          - 函数执行后不会返回到调用点
 * 
 * @see luaL_argerror() Lua参数错误函数
 * @see strerror() C标准库错误描述函数
 * @see lua_pushfstring() Lua格式化字符串函数
 * 
 * @example
 * // 使用示例：
 * FILE *f = fopen("nonexistent.txt", "r");
 * if (!f) {
 *     fileerror(L, 1, "nonexistent.txt");  // 抛出错误，不会返回
 * }
 */
static void fileerror(lua_State *L, int arg, const char *filename)
{
    lua_pushfstring(L, "%s: %s", filename, strerror(errno));
    luaL_argerror(L, arg, lua_tostring(L, -1));
}


#define tofilep(L)	((FILE **)luaL_checkudata(L, 1, LUA_FILEHANDLE))


/**
 * @brief 文件类型检测函数，判断对象是否为文件句柄及其状态
 * @details 实现Lua函数io.type()的功能，用于检测给定对象是否为文件句柄，
 *          以及文件句柄的当前状态（打开或关闭）。这对于类型安全的文件操作很重要。
 * 
 * @param L Lua虚拟机状态指针，栈位置1应包含要检测的对象
 * 
 * @return int 总是返回1（推送一个结果到栈）
 * 
 * @note 返回值说明：
 *       - nil: 对象不是文件句柄
 *       - "file": 对象是打开的文件句柄
 *       - "closed file": 对象是已关闭的文件句柄
 * 
 * @note 检测机制：
 *       - 首先检查对象是否为用户数据类型
 *       - 获取对象的元表并与文件句柄元表比较
 *       - 检查文件指针是否为NULL来判断文件状态
 *       - 使用lua_rawequal进行精确的元表比较
 * 
 * @note 实现细节：
 *       - 使用luaL_checkany确保至少有一个参数
 *       - 通过LUA_REGISTRYINDEX访问文件句柄元表
 *       - 元表比较确保类型安全性
 *       - 支持识别已关闭的文件句柄对象
 * 
 * @warning 注意事项：
 *          - 对于非文件对象返回nil
 *          - 不会抛出错误，即使参数类型不正确
 *          - 已关闭的文件句柄仍被识别为文件类型
 * 
 * @see lua_getmetatable() 获取对象元表
 * @see lua_rawequal() 原始相等性比较
 * @see LUA_FILEHANDLE 文件句柄类型常量
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("test.txt", "r")
 * print(io.type(file))     -- 输出: "file"
 * file:close()
 * print(io.type(file))     -- 输出: "closed file"
 * print(io.type("string")) -- 输出: nil
 */
static int io_type(lua_State *L)
{
    void *ud;
    luaL_checkany(L, 1);
    ud = lua_touserdata(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_FILEHANDLE);
    if (ud == NULL || !lua_getmetatable(L, 1) || !lua_rawequal(L, -2, -1))
        lua_pushnil(L);
    else if (*((FILE **)ud) == NULL)
        lua_pushliteral(L, "closed file");
    else
        lua_pushliteral(L, "file");
    return 1;
}


/**
 * @brief 文件句柄转换函数，从Lua对象获取有效的FILE指针
 * @details 这是一个核心的类型转换函数，用于从Lua栈上的文件对象安全地获取
 *          底层的FILE指针。它会验证文件对象的有效性并确保文件处于打开状态。
 * 
 * @param L Lua虚拟机状态指针，栈位置1应包含文件对象
 * 
 * @return FILE* 有效的文件指针，可用于C标准库文件操作
 * 
 * @note 安全检查：
 *       - 使用tofilep()宏验证对象是否为文件句柄类型
 *       - 检查文件指针是否为NULL（文件是否已关闭）
 *       - 如果文件已关闭，抛出Lua错误
 * 
 * @note 实现细节：
 *       - tofilep()宏会调用luaL_checkudata进行类型检查
 *       - 类型检查失败时luaL_checkudata会自动抛出错误
 *       - 只有通过所有检查的文件才会返回有效指针
 * 
 * @warning 注意事项：
 *          - 如果文件已关闭，函数会抛出错误并不返回
 *          - 调用者可以安全地使用返回的FILE指针
 *          - 不应该对返回的指针调用fclose()，应使用Lua的关闭机制
 * 
 * @exception 抛出错误的情况：
 *            - 参数不是文件对象类型
 *            - 文件对象已被关闭
 * 
 * @see tofilep() 文件对象类型检查宏
 * @see luaL_checkudata() Lua用户数据类型检查函数
 * @see luaL_error() Lua错误抛出函数
 * 
 * @example
 * // 在C函数中使用：
 * static int my_file_function(lua_State *L) {
 *     FILE *f = tofile(L);  // 安全获取文件指针
 *     // 现在可以安全地使用f进行文件操作
 *     return some_result;
 * }
 */
static FILE *tofile(lua_State *L)
{
    FILE **f = tofilep(L);
    if (*f == NULL)
        luaL_error(L, "attempt to use a closed file");
    return *f;
}



/**
 * @brief 新文件对象创建函数，创建一个初始为关闭状态的文件句柄
 * @details 这个函数创建一个新的Lua文件对象，初始状态为关闭。这种设计模式
 *          确保了即使在文件打开过程中发生内存错误，也不会留下未关闭的文件。
 *          创建的文件对象具有正确的元表和垃圾回收行为。
 * 
 * @param L Lua虚拟机状态指针，用于创建用户数据和设置元表
 * 
 * @return FILE** 指向FILE指针的指针，初始值为NULL（关闭状态）
 * 
 * @note 安全设计：
 *       - 文件句柄初始化为NULL（关闭状态）
 *       - 先创建对象，后打开文件，避免资源泄漏
 *       - 即使内存分配失败，也不会留下未关闭的文件
 *       - 自动设置文件句柄的元表
 * 
 * @note 实现细节：
 *       - 使用lua_newuserdata分配FILE*大小的内存
 *       - 将文件指针初始化为NULL
 *       - 获取并设置LUA_FILEHANDLE元表
 *       - 返回的指针可用于后续的文件打开操作
 * 
 * @note 元表功能：
 *       - 提供文件对象的方法（read, write, close等）
 *       - 实现垃圾回收时的自动文件关闭
 *       - 支持tostring等元方法
 * 
 * @warning 注意事项：
 *          - 返回的文件句柄初始为关闭状态
 *          - 需要后续调用fopen等函数来实际打开文件
 *          - 对象会被推送到Lua栈顶
 * 
 * @see lua_newuserdata() Lua用户数据创建函数
 * @see luaL_getmetatable() 获取命名元表函数
 * @see lua_setmetatable() 设置对象元表函数
 * @see LUA_FILEHANDLE 文件句柄类型常量
 * 
 * @example
 * // 使用模式：
 * FILE **pf = newfile(L);    // 创建关闭状态的文件对象
 * *pf = fopen("file.txt", "r"); // 实际打开文件
 * if (*pf == NULL) {
 *     // 处理打开失败，对象已在栈上但处于关闭状态
 * }
 */
static FILE **newfile(lua_State *L)
{
    FILE **pf = (FILE **)lua_newuserdata(L, sizeof(FILE *));
    *pf = NULL;
    luaL_getmetatable(L, LUA_FILEHANDLE);
    lua_setmetatable(L, -2);
    return pf;
}


/**
 * @brief 标准文件禁止关闭函数，防止关闭stdin、stdout、stderr
 * @details 这是一个特殊的关闭函数，用于标准文件（stdin、stdout、stderr）。
 *          由于标准文件由系统管理，不应该被用户程序关闭，此函数会返回失败信息。
 * 
 * @param L Lua虚拟机状态指针
 * 
 * @return int 总是返回2（nil + 错误消息）
 * 
 * @note 返回值：
 *       - nil: 表示操作失败
 *       - "cannot close standard file": 错误消息
 * 
 * @note 适用场景：
 *       - 作为stdin、stdout、stderr的__close元方法
 *       - 防止用户意外关闭系统标准文件
 *       - 保持与其他文件关闭函数的接口一致性
 * 
 * @see createstdfile() 标准文件创建函数
 */
static int io_noclose(lua_State *L)
{
    lua_pushnil(L);
    lua_pushliteral(L, "cannot close standard file");
    return 2;
}


/**
 * @brief 管道文件关闭函数，关闭通过popen创建的文件
 * @details 专门用于关闭通过io.popen()创建的管道文件。使用lua_pclose()
 *          而不是标准的fclose()来正确处理管道文件的关闭和进程状态。
 * 
 * @param L Lua虚拟机状态指针，栈顶应为popen创建的文件对象
 * 
 * @return int pushresult()的返回值（成功返回1，失败返回3）
 * 
 * @note 处理流程：
 *       - 获取文件指针并验证类型
 *       - 调用lua_pclose()关闭管道并等待进程结束
 *       - 将文件指针设置为NULL标记为已关闭
 *       - 返回操作结果
 * 
 * @note 与普通文件的区别：
 *       - 使用lua_pclose()而非fclose()
 *       - 会等待子进程结束并获取退出状态
 *       - 处理管道特有的错误情况
 * 
 * @see lua_pclose() Lua管道关闭函数
 * @see io_popen() 管道打开函数
 * @see pushresult() 结果处理函数
 */
static int io_pclose(lua_State *L)
{
    FILE **p = tofilep(L);
    int ok = lua_pclose(L, *p);
    *p = NULL;
    return pushresult(L, ok, NULL);
}


/**
 * @brief 常规文件关闭函数，关闭普通文件
 * @details 用于关闭通过fopen()或io.open()创建的常规文件。使用标准的
 *          fclose()函数来关闭文件并释放相关资源。
 * 
 * @param L Lua虚拟机状态指针，栈顶应为常规文件对象
 * 
 * @return int pushresult()的返回值（成功返回1，失败返回3）
 * 
 * @note 处理流程：
 *       - 获取文件指针并验证类型
 *       - 调用fclose()关闭文件
 *       - 将文件指针设置为NULL标记为已关闭
 *       - 返回操作结果和可能的错误信息
 * 
 * @note 错误处理：
 *       - fclose()失败时会设置errno
 *       - pushresult()会包含具体的错误信息
 *       - 即使关闭失败，文件指针也会被设为NULL
 * 
 * @see fclose() C标准库文件关闭函数
 * @see io_open() 文件打开函数
 * @see pushresult() 结果处理函数
 */
static int io_fclose(lua_State *L)
{
    FILE **p = tofilep(L);
    int ok = (fclose(*p) == 0);
    *p = NULL;
    return pushresult(L, ok, NULL);
}


/**
 * @brief 辅助关闭函数，通过文件环境表调用相应的关闭方法
 * @details 这是一个通用的文件关闭辅助函数，它会从文件对象的环境表中
 *          获取适当的关闭函数（__close字段）并调用它。这种设计允许
 *          不同类型的文件（普通文件、管道文件、标准文件）使用不同的关闭方法。
 * 
 * @param L Lua虚拟机状态指针，栈位置1应为文件对象
 * 
 * @return int 相应关闭函数的返回值
 * 
 * @note 工作机制：
 *       - 获取文件对象的环境表（fenv）
 *       - 从环境表中获取"__close"字段
 *       - 将获取到的函数作为C函数调用
 *       - 返回关闭函数的执行结果
 * 
 * @note 环境表中的关闭函数：
 *       - 普通文件：io_fclose
 *       - 管道文件：io_pclose  
 *       - 标准文件：io_noclose
 * 
 * @note 设计优势：
 *       - 统一的关闭接口，支持多种文件类型
 *       - 通过环境表实现多态行为
 *       - 简化文件关闭逻辑的实现
 * 
 * @warning 注意事项：
 *          - 假定环境表中的__close字段是有效的C函数
 *          - 文件对象必须具有正确设置的环境表
 *          - 调用的关闭函数必须遵循相同的参数和返回值约定
 * 
 * @see lua_getfenv() 获取对象环境表
 * @see lua_getfield() 获取表字段
 * @see lua_tocfunction() 转换为C函数指针
 * @see io_fclose() 普通文件关闭函数
 * @see io_pclose() 管道文件关闭函数
 * @see io_noclose() 标准文件禁止关闭函数
 * 
 * @example
 * // 环境表设置示例：
 * // 对于普通文件，环境表包含：__close = io_fclose
 * // 对于管道文件，环境表包含：__close = io_pclose
 * // 对于标准文件，环境表包含：__close = io_noclose
 */
static int aux_close(lua_State *L)
{
    lua_getfenv(L, 1);
    lua_getfield(L, -1, "__close");
    return (lua_tocfunction(L, -1))(L);
}

/**
 * @brief 文件关闭函数：实现io.close()和file:close()功能
 * 
 * 这个函数提供了统一的文件关闭接口，支持两种调用方式：
 * 1. io.close() - 关闭当前输出文件
 * 2. file:close() - 关闭指定的文件对象
 * 
 * 处理逻辑：
 * - 如果没有提供参数，使用当前输出文件（从环境表获取）
 * - 验证参数确实是有效的文件对象
 * - 调用aux_close()执行实际的关闭操作
 * 
 * 安全特性：
 * - 自动类型检查：确保操作的是有效文件对象
 * - 重复关闭保护：已关闭的文件不会出错
 * - 统一错误处理：使用标准的错误返回格式
 * 
 * @param L Lua状态机，可选参数：文件对象
 * @return 关闭操作的结果（成功或错误信息）
 */
static int io_close(lua_State *L) {
    if (lua_isnone(L, 1))
        lua_rawgeti(L, LUA_ENVIRONINDEX, IO_OUTPUT);
    tofile(L);  /* make sure argument is a file */
    return aux_close(L);
}

/**
 * @brief 垃圾回收处理函数：文件对象的自动清理
 * 
 * 这个函数是文件对象的__gc元方法，当文件对象被垃圾回收时自动调用。
 * 它确保即使程序员忘记显式关闭文件，文件也能被正确关闭，防止资源泄漏。
 * 
 * 处理机制：
 * 1. 检查文件是否仍然打开
 * 2. 如果打开，调用aux_close()关闭文件
 * 3. 忽略已经关闭的文件，避免重复操作
 * 
 * 安全设计：
 * - 静默处理：GC过程中不抛出错误
 * - 幂等操作：多次调用不会产生副作用
 * - 资源保护：确保系统资源得到释放
 * 
 * @param L Lua状态机，第一个参数是文件对象
 * @return 总是返回0（GC方法不需要返回值）
 */
static int io_gc(lua_State *L) {
    FILE *f = *tofilep(L);
    /* ignore closed files */
    if (f != NULL)
        aux_close(L);
    return 0;
}


/**
 * @brief 文件对象字符串转换函数，实现文件对象的__tostring元方法
 * @details 实现文件对象转换为字符串的功能，用于调试和显示目的。
 *          该函数会根据文件的状态（打开或关闭）生成相应的字符串表示。
 * 
 * @param L Lua虚拟机状态指针，栈位置1应为文件对象
 * 
 * @return int 总是返回1（推送一个字符串到栈）
 * 
 * @note 输出格式：
 *       - 关闭的文件："file (closed)"
 *       - 打开的文件："file (0x...)" 其中0x...是文件指针的十六进制地址
 * 
 * @note 使用场景：
 *       - Lua中调用tostring(file_object)时自动调用
 *       - 字符串连接操作涉及文件对象时调用
 *       - 调试和日志记录中显示文件状态
 * 
 * @note 实现细节：
 *       - 使用tofilep()获取文件指针
 *       - 检查文件指针是否为NULL判断状态
 *       - 对于打开的文件，显示内存地址便于调试
 *       - 使用lua_pushfstring格式化字符串
 * 
 * @warning 注意事项：
 *          - 显示的内存地址仅用于调试，不应用于程序逻辑
 *          - 关闭的文件仍然是有效的文件对象，只是处于关闭状态
 * 
 * @see tofilep() 文件对象指针获取宏
 * @see lua_pushfstring() Lua格式化字符串函数
 * @see lua_pushliteral() Lua字面量字符串函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("test.txt", "r")
 * print(file)           -- 输出: file (0x...)
 * file:close()
 * print(file)           -- 输出: file (closed)
 */
static int io_tostring(lua_State *L)
{
    FILE *f = *tofilep(L);
    if (f == NULL)
        lua_pushliteral(L, "file (closed)");
    else
        lua_pushfstring(L, "file (%p)", f);
    return 1;
}

/**
 * @brief 文件打开函数：实现io.open()的核心功能
 * 
 * 这个函数实现了Lua的io.open()功能，用于打开文件并返回文件句柄。
 * 它封装了C标准库的fopen()函数，提供了Lua风格的错误处理。
 * 
 * 参数处理：
 * - filename：必需参数，要打开的文件路径
 * - mode：可选参数，文件打开模式，默认为"r"（只读）
 * 
 * 支持的打开模式：
 * - "r"：只读模式（默认）
 * - "w"：写入模式，会清空现有内容
 * - "a"：追加模式，在文件末尾写入
 * - "r+"："读写模式，文件必须存在
 * - "w+"：读写模式，会清空现有内容
 * - "a+"：读写模式，在文件末尾追加
 * - 添加"b"后缀表示二进制模式（如"rb"、"wb"）
 * 
 * 返回值：
 * - 成功：返回文件句柄对象
 * - 失败：返回nil、错误消息和错误代码
 * 
 * 实现机制：
 * 1. 使用newfile()创建文件句柄对象
 * 2. 调用fopen()尝试打开文件
 * 3. 根据结果返回文件句柄或错误信息
 * 
 * @param L Lua状态机，参数：filename, [mode]
 * @return 1个值（成功时为文件句柄）或3个值（失败时为nil+错误信息+错误码）
 */
static int io_open(lua_State *L) {
    const char *filename = luaL_checkstring(L, 1);
    const char *mode = luaL_optstring(L, 2, "r");
    FILE **pf = newfile(L);
    *pf = fopen(filename, mode);
    return (*pf == NULL) ? pushresult(L, 0, filename) : 1;
}


/**
 * @brief 管道文件打开函数：实现io.popen()功能
 * 
 * 详细说明：
 * 这个函数通过创建子进程并建立管道来执行外部命令，允许Lua程序与外部程序进行数据交换。
 * popen创建的文件对象具有特殊的关闭语义，需要等待子进程结束并获取退出状态。
 * 
 * 实现机制：
 * 1. 检查命令字符串参数的有效性
 * 2. 获取可选的模式参数（默认为"r"读取模式）
 * 3. 创建新的文件对象来管理管道
 * 4. 调用系统相关的lua_popen函数创建管道
 * 5. 根据创建结果返回文件对象或错误信息
 * 
 * 注意事项：
 * - 这个函数有独立的环境，定义了popen文件的正确__close方法
 * - 管道文件的关闭需要等待子进程结束
 * - 不同平台的popen实现可能有所不同
 * 
 * @param L Lua状态机指针
 * @param 1 要执行的命令字符串
 * @param 2 可选的模式字符串（"r"读取/"w"写入），默认"r"
 * 
 * @return int 返回值数量：成功返回1（文件对象），失败返回2（nil+错误信息）
 * 
 * @see lua_popen() 系统相关的管道创建函数
 * @see newfile() 文件对象创建函数
 * @see pushresult() 结果推送函数
 */
static int io_popen(lua_State *L) {
    const char *filename = luaL_checkstring(L, 1);
    const char *mode = luaL_optstring(L, 2, "r");
    FILE **pf = newfile(L);
    *pf = lua_popen(L, filename, mode);
    return (*pf == NULL) ? pushresult(L, 0, filename) : 1;
}

/**
 * @brief 临时文件创建函数：实现io.tmpfile()功能
 * 
 * 详细说明：
 * 这个函数创建一个临时文件，该文件在程序结束或显式关闭时会自动删除。
 * 临时文件通常用于存储中间处理结果或缓存数据，不需要永久保存。
 * 
 * 实现机制：
 * 1. 使用newfile()创建Lua文件对象
 * 2. 调用C标准库的tmpfile()创建临时文件
 * 3. tmpfile()自动选择合适的临时目录和文件名
 * 4. 返回文件句柄或错误信息
 * 
 * 临时文件特性：
 * - 自动删除：程序结束时自动删除
 * - 唯一性：文件名保证不会冲突
 * - 可读写：以"w+b"模式打开，支持读写和二进制数据
 * - 系统管理：由操作系统管理存储位置
 * 
 * 使用场景：
 * - 大数据处理的中间结果存储
 * - 临时缓存文件
 * - 进程间数据交换
 * - 算法中的临时工作空间
 * 
 * 安全考虑：
 * - 文件权限：通常只有创建进程可以访问
 * - 自动清理：防止临时文件累积
 * - 空间限制：受系统临时目录空间限制
 * 
 * @param[in] L Lua状态机指针
 * 
 * @return 操作结果
 * @retval 1 成功创建临时文件，返回文件句柄
 * @retval 3 创建失败，返回nil、错误消息和错误代码
 * 
 * @pre L != NULL
 * @post 成功时栈顶有新的临时文件对象
 * 
 * @note 临时文件在关闭后会自动删除
 * @warning 不要依赖临时文件的持久性
 * 
 * @since Lua 5.0
 * @see io_open(), newfile(), tmpfile()
 */
static int io_tmpfile(lua_State *L) {
    FILE **pf = newfile(L);
    *pf = tmpfile();
    return (*pf == NULL) ? pushresult(L, 0, NULL) : 1;
}


/**
 * @brief 默认I/O文件获取函数，获取当前的默认输入或输出文件
 * @details 从Lua环境表中获取默认的I/O文件（输入或输出）。这些默认文件
 *          用于io.read()、io.write()等全局I/O操作。如果默认文件已关闭，
 *          会抛出相应的错误信息。
 * 
 * @param L Lua虚拟机状态指针，用于访问环境表和错误处理
 * @param findex 文件索引，IO_INPUT(1)或IO_OUTPUT(2)
 * 
 * @return FILE* 有效的文件指针，用于后续I/O操作
 * 
 * @note 文件索引说明：
 *       - IO_INPUT (1): 默认输入文件，通常为stdin
 *       - IO_OUTPUT (2): 默认输出文件，通常为stdout
 * 
 * @note 实现机制：
 *       - 使用lua_rawgeti从环境表获取文件对象
 *       - 从用户数据中提取FILE指针
 *       - 检查文件指针有效性
 *       - 文件关闭时抛出描述性错误
 * 
 * @note 错误处理：
 *       - 如果默认文件已关闭，抛出Lua错误
 *       - 错误消息包含具体的文件类型（input/output）
 *       - 使用fnames数组提供友好的错误信息
 * 
 * @warning 注意事项：
 *          - 函数可能抛出错误，调用者需要处理
 *          - 返回的文件指针保证有效且打开
 *          - findex必须是有效的索引值
 * 
 * @see lua_rawgeti() 原始表索引获取函数
 * @see lua_touserdata() 用户数据转换函数
 * @see luaL_error() Lua错误抛出函数
 * @see fnames 文件名称数组
 * 
 * @example
 * // 使用示例：
 * FILE *input = getiofile(L, IO_INPUT);   // 获取默认输入文件
 * FILE *output = getiofile(L, IO_OUTPUT); // 获取默认输出文件
 * // 现在可以安全地使用这些文件指针进行I/O操作
 */
static FILE *getiofile(lua_State *L, int findex)
{
    FILE *f;
    lua_rawgeti(L, LUA_ENVIRONINDEX, findex);
    f = *(FILE **)lua_touserdata(L, -1);
    if (f == NULL)
        luaL_error(L, "standard %s file is closed", fnames[findex - 1]);
    return f;
}


/**
 * @brief 通用I/O文件设置函数，设置或获取默认I/O文件
 * @details 这是io.input()和io.output()函数的底层实现，用于设置或获取
 *          默认的输入/输出文件。支持通过文件名或文件对象来指定新的默认文件。
 * 
 * @param L Lua虚拟机状态指针，可选参数：文件名字符串或文件对象
 * @param f 文件索引，IO_INPUT或IO_OUTPUT
 * @param mode 文件打开模式，如"r"用于输入，"w"用于输出
 * 
 * @return int 总是返回1（当前的默认文件对象）
 * 
 * @note 参数处理：
 *       - 无参数：返回当前默认文件
 *       - 字符串参数：作为文件名打开并设为默认文件
 *       - 文件对象参数：直接设为默认文件
 * 
 * @note 实现逻辑：
 *       - 检查是否提供了参数
 *       - 如果是字符串，创建新文件对象并打开文件
 *       - 如果是文件对象，验证其有效性
 *       - 将新文件设置到环境表中
 *       - 返回当前（可能是新设置的）默认文件
 * 
 * @note 错误处理：
 *       - 文件打开失败时调用fileerror()抛出错误
 *       - 无效文件对象会通过tofile()检查并抛出错误
 *       - 错误信息包含具体的文件名和失败原因
 * 
 * @warning 注意事项：
 *          - 更改默认文件会影响后续的io.read()和io.write()操作
 *          - 原有的默认文件不会自动关闭
 *          - 打开失败会抛出错误而不是返回nil
 * 
 * @see newfile() 新文件对象创建函数
 * @see tofile() 文件对象验证函数
 * @see fileerror() 文件错误处理函数
 * @see lua_rawseti() 原始表索引设置函数
 * @see lua_rawgeti() 原始表索引获取函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * io.input("data.txt")        -- 设置默认输入文件
 * local current = io.input()  -- 获取当前默认输入文件
 * io.output(io.stderr)        -- 设置默认输出为错误输出
 */
static int g_iofile(lua_State *L, int f, const char *mode)
{
    if (!lua_isnoneornil(L, 1)) {
        const char *filename = lua_tostring(L, 1);
        if (filename) {
            FILE **pf = newfile(L);
            *pf = fopen(filename, mode);
            if (*pf == NULL)
                fileerror(L, 1, filename);
        }
        else {
            tofile(L);
            lua_pushvalue(L, 1);
        }
        lua_rawseti(L, LUA_ENVIRONINDEX, f);
    }
    lua_rawgeti(L, LUA_ENVIRONINDEX, f);
    return 1;
}


/**
 * @brief 默认输入文件设置函数，实现io.input()功能
 * @details 设置或获取默认的输入文件。这是g_iofile()函数的简单包装，
 *          专门用于处理输入文件，使用只读模式("r")打开文件。
 * 
 * @param L Lua虚拟机状态指针，可选参数：文件名或文件对象
 * 
 * @return int 返回1（当前的默认输入文件对象）
 * 
 * @note 使用说明：
 *       - 无参数时返回当前默认输入文件
 *       - 提供文件名时打开该文件并设为默认输入
 *       - 提供文件对象时直接设为默认输入
 *       - 影响后续io.read()操作的数据源
 * 
 * @see g_iofile() 通用I/O文件设置函数
 * @see IO_INPUT 输入文件索引常量
 */
static int io_input(lua_State *L)
{
    return g_iofile(L, IO_INPUT, "r");
}


/**
 * @brief 默认输出文件设置函数，实现io.output()功能
 * @details 设置或获取默认的输出文件。这是g_iofile()函数的简单包装，
 *          专门用于处理输出文件，使用写入模式("w")打开文件。
 * 
 * @param L Lua虚拟机状态指针，可选参数：文件名或文件对象
 * 
 * @return int 返回1（当前的默认输出文件对象）
 * 
 * @note 使用说明：
 *       - 无参数时返回当前默认输出文件
 *       - 提供文件名时打开该文件并设为默认输出
 *       - 提供文件对象时直接设为默认输出
 *       - 影响后续io.write()操作的目标文件
 * 
 * @warning 注意事项：
 *          - 使用"w"模式会清空现有文件内容
 *          - 如需追加内容，应直接使用文件对象
 * 
 * @see g_iofile() 通用I/O文件设置函数
 * @see IO_OUTPUT 输出文件索引常量
 */
static int io_output(lua_State *L)
{
    return g_iofile(L, IO_OUTPUT, "w");
}


static int io_readline (lua_State *L);


/**
 * @brief 行迭代器辅助函数：为文件行迭代准备闭包
 * 
 * 详细说明：
 * 这是一个内部辅助函数，用于创建文件行迭代器的闭包。它将文件对象和关闭标志
 * 作为上值封装到io_readline函数中，形成一个可以逐行读取文件的迭代器。
 * 
 * 实现机制：
 * 1. 将指定索引的文件对象推入栈顶
 * 2. 推入关闭标志（指示迭代完成后是否关闭文件）
 * 3. 创建包含两个上值的C闭包，绑定到io_readline函数
 * 
 * 使用场景：
 * - 为file:lines()创建迭代器（不关闭文件）
 * - 为io.lines(filename)创建迭代器（自动关闭文件）
 * 
 * 注意事项：
 * - toclose参数决定迭代结束后是否自动关闭文件
 * - 创建的闭包包含文件对象和关闭标志作为上值
 * - io_readline函数将使用这些上值进行实际的行读取操作
 * 
 * @param L Lua状态机指针
 * @param idx 文件对象在栈中的索引位置
 * @param toclose 迭代完成后是否关闭文件（1关闭/0不关闭）
 * 
 * @return void 无返回值，但在栈顶留下创建的闭包
 * 
 * @see io_readline() 实际的行读取函数
 * @see f_lines() 文件对象的lines方法
 * @see io_lines() 全局io.lines函数
 */
static void aux_lines(lua_State *L, int idx, int toclose) {
    lua_pushvalue(L, idx);
    lua_pushboolean(L, toclose);
    lua_pushcclosure(L, io_readline, 2);
}


/**
 * @brief 文件对象行迭代器：实现file:lines()方法
 * 
 * 详细说明：
 * 这个函数为文件对象提供lines()方法，返回一个可以逐行遍历文件内容的迭代器。
 * 与io.lines()不同，这个方法不会在迭代完成后自动关闭文件，文件的生命周期
 * 由原文件对象管理。
 * 
 * 实现机制：
 * 1. 验证第一个参数是有效的文件句柄
 * 2. 调用aux_lines创建行迭代器闭包
 * 3. 传入toclose=0表示不自动关闭文件
 * 4. 返回创建的迭代器函数
 * 
 * 使用示例：
 * ```lua
 * local file = io.open("test.txt", "r")
 * for line in file:lines() do
 *     print(line)
 * end
 * file:close()  -- 需要手动关闭
 * ```
 * 
 * 注意事项：
 * - 不会自动关闭文件，需要手动管理文件生命周期
 * - 适用于需要多次操作同一文件的场景
 * - 迭代器会保持对文件对象的引用
 * 
 * @param L Lua状态机指针
 * @param 1 文件对象（userdata类型）
 * 
 * @return int 返回值数量：1（迭代器函数）
 * 
 * @see tofile() 文件对象验证函数
 * @see aux_lines() 迭代器创建辅助函数
 * @see io_lines() 全局行迭代器函数
 */
static int f_lines(lua_State *L) {
    tofile(L);
    aux_lines(L, 1, 0);
    return 1;
}


/**
 * @brief 全局行迭代器函数：实现io.lines()功能
 * 
 * 详细说明：
 * 这个函数提供全局的文件行迭代功能，支持两种使用模式：
 * 1. 无参数调用：迭代标准输入流
 * 2. 传入文件名：打开文件并创建自动关闭的迭代器
 * 
 * 实现机制：
 * - 无参数模式：
 *   1. 从环境中获取默认输入流(IO_INPUT)
 *   2. 调用f_lines创建迭代器（不自动关闭）
 * - 文件名模式：
 *   1. 验证文件名参数的有效性
 *   2. 创建新文件对象并打开指定文件
 *   3. 检查文件打开是否成功，失败时报错
 *   4. 创建自动关闭的迭代器（toclose=1）
 * 
 * 使用示例：
 * ```lua
 * -- 迭代标准输入
 * for line in io.lines() do
 *     print(line)
 * end
 * 
 * -- 迭代文件（自动关闭）
 * for line in io.lines("test.txt") do
 *     print(line)
 * end
 * ```
 * 
 * 注意事项：
 * - 传入文件名时会自动关闭文件
 * - 无参数时使用当前输入流，不会关闭
 * - 文件打开失败会抛出错误
 * 
 * @param L Lua状态机指针
 * @param 1 可选的文件名字符串
 * 
 * @return int 返回值数量：1（迭代器函数）
 * 
 * @see f_lines() 文件对象行迭代器
 * @see aux_lines() 迭代器创建辅助函数
 * @see newfile() 文件对象创建函数
 * @see fileerror() 文件错误处理函数
 */
static int io_lines(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        lua_rawgeti(L, LUA_ENVIRONINDEX, IO_INPUT);
        return f_lines(L);
    }
    else {
        const char *filename = luaL_checkstring(L, 1);
        FILE **pf = newfile(L);
        *pf = fopen(filename, "r");
        if (*pf == NULL)
            fileerror(L, 1, filename);
        aux_lines(L, lua_gettop(L), 1);
        return 1;
    }
}


/*
** {======================================================
** READ
** =======================================================
*/

/**
 * @brief 数值读取函数，从文件中读取一个数值
 * @details 这个函数实现了Lua文件读取中的"*number"模式，从文件当前位置
 *          读取一个数值（整数或浮点数）。使用C标准库的fscanf()函数
 *          进行数值解析，支持多种数值格式。
 * 
 * @param L Lua虚拟机状态指针，用于推送结果到栈
 * @param f 要读取的文件流指针，必须是已打开的可读文件
 * 
 * @return int 读取结果状态
 *         - 1: 成功读取到数值，数值已推送到栈顶
 *         - 0: 读取失败，nil值已推送到栈顶
 * 
 * @note 读取机制：
 *       - 使用fscanf()和LUA_NUMBER_SCAN格式串解析数值
 *       - 自动跳过前导空白字符
 *       - 支持整数和浮点数格式
 *       - 支持科学记数法（如1.5e-3）
 *       - 支持十六进制数值格式
 * 
 * @note 处理规则：
 *       - 成功：返回解析到的数值
 *       - 失败：返回nil（文件结束或格式错误）
 *       - 格式错误包括：非数字字符、空文件等
 * 
 * @warning 注意事项：
 *          - 文件指针必须指向有效的可读文件
 *          - 数值解析失败时文件指针位置不确定
 *          - 对于非数值内容会导致读取失败
 * 
 * @see fscanf() C标准库格式化输入函数
 * @see LUA_NUMBER_SCAN 数值扫描格式宏
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("numbers.txt", "r")
 * local num = file:read("*n")  -- 内部调用此函数
 * if num then print("读取到数值:", num) end
 * file:close()
 */
static int read_number(lua_State *L, FILE *f)
{
    lua_Number d;
    if (fscanf(f, LUA_NUMBER_SCAN, &d) == 1) {
        lua_pushnumber(L, d);
        return 1;
    }
    else {
        lua_pushnil(L);
        return 0;
    }
}


/**
 * @brief 文件结束测试函数，检测文件是否到达末尾
 * @details 这个函数用于测试文件流是否已经到达末尾，同时不消耗任何字符。
 *          它通过读取一个字符然后立即放回的方式来检测EOF状态。
 *          主要用于实现读取0个字符的特殊情况处理。
 * 
 * @param L Lua虚拟机状态指针，用于推送空字符串结果
 * @param f 要测试的文件流指针，必须是已打开的可读文件
 * 
 * @return int 测试结果状态
 *         - 1: 文件未到达末尾，还有内容可读
 *         - 0: 文件已到达末尾，没有更多内容
 * 
 * @note 实现机制：
 *       - 使用getc()尝试读取一个字符
 *       - 立即使用ungetc()将字符放回流中
 *       - 推送空字符串到Lua栈作为结果
 *       - 通过比较读取的字符与EOF来判断状态
 * 
 * @note 使用场景：
 *       - file:read(0) 操作的底层实现
 *       - 在不消耗字符的情况下测试文件状态
 *       - 配合其他读取函数判断文件结束
 * 
 * @warning 注意事项：
 *          - 文件流必须支持ungetc()操作
 *          - 对于某些特殊流类型可能不可靠
 *          - 不会改变文件的当前读取位置
 * 
 * @see getc() C标准库字符读取函数
 * @see ungetc() C标准库字符回退函数
 * @see lua_pushlstring() Lua字符串推送函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("test.txt", "r")
 * local result = file:read(0)  -- 内部调用此函数
 * if result then print("文件有内容") else print("文件为空或已结束") end
 * file:close()
 */
static int test_eof(lua_State *L, FILE *f)
{
    int c = getc(f);
    ungetc(c, f);
    lua_pushlstring(L, NULL, 0);
    return (c != EOF);
}


/**
 * @brief 从文件流中读取一行文本
 * @details 从给定的文件流中读取一行文本，自动处理不同的换行符情况。
 *          使用Lua缓冲区系统实现高效的字符串构建，避免频繁的内存分配。
 *          该函数是Lua I/O库中实现file:read("*l")和io.read("*l")功能的核心函数。
 * 
 * @param L Lua虚拟机状态指针，用于缓冲区操作和结果返回
 * @param f 要读取的文件流指针，必须是已打开的可读文件
 * 
 * @return int 读取结果状态
 *         - 1: 成功读取到完整的一行（包含换行符的情况）
 *         - 0: 到达文件末尾且没有读取到任何内容
 *         - >0: 到达文件末尾但读取到了部分内容（最后一行没有换行符）
 * 
 * @note 实现细节：
 *       - 使用luaL_Buffer缓冲区系统，避免频繁的字符串拼接操作
 *       - 通过fgets()函数按块读取，每次最多读取LUAL_BUFFERSIZE个字符
 *       - 自动检测并移除行末的换行符('\n')，保持跨平台兼容性
 *       - 处理文件末尾没有换行符的特殊情况
 *       - 循环读取直到遇到换行符或文件结束
 * 
 * @warning 注意事项：
 *          - 文件流f必须是有效的已打开文件，否则行为未定义
 *          - 读取的内容会被推送到Lua栈顶，调用者需要适当处理栈状态
 *          - 对于二进制文件，换行符的处理可能不符合预期
 * 
 * @see luaL_Buffer 缓冲区系统文档
 * @see fgets() C标准库函数
 * @see lua_objlen() 获取Lua对象长度
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("test.txt", "r")
 * local line = file:read("*l")  -- 内部调用此函数
 * file:close()
 */
/**
 * @brief 从文件流中读取一行文本
 * @details 从给定的文件流中读取一行文本，自动处理不同的换行符情况。
 *          使用Lua缓冲区系统实现高效的字符串构建，避免频繁的内存分配。
 *          该函数是Lua I/O库中实现file:read("*l")和io.read("*l")功能的核心函数。
 * 
 * @param L Lua虚拟机状态指针，用于缓冲区操作和结果返回
 * @param f 要读取的文件流指针，必须是已打开的可读文件
 * 
 * @return int 读取结果状态
 *         - 1: 成功读取到完整的一行（包含换行符的情况）
 *         - 0: 到达文件末尾且没有读取到任何内容
 *         - >0: 到达文件末尾但读取到了部分内容（最后一行没有换行符）
 * 
 * @note 实现细节：
 *       - 使用luaL_Buffer缓冲区系统，避免频繁的字符串拼接操作
 *       - 通过fgets()函数按块读取，每次最多读取LUAL_BUFFERSIZE个字符
 *       - 自动检测并移除行末的换行符('\n')，保持跨平台兼容性
 *       - 处理文件末尾没有换行符的特殊情况
 *       - 循环读取直到遇到换行符或文件结束
 * 
 * @warning 注意事项：
 *          - 文件流f必须是有效的已打开文件，否则行为未定义
 *          - 读取的内容会被推送到Lua栈顶，调用者需要适当处理栈状态
 *          - 对于二进制文件，换行符的处理可能不符合预期
 * 
 * @see luaL_Buffer 缓冲区系统文档
 * @see fgets() C标准库函数
 * @see lua_objlen() 获取Lua对象长度
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("test.txt", "r")
 * local line = file:read("*l")  -- 内部调用此函数
 * file:close()
 */
static int read_line(lua_State *L, FILE *f)
{
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (;;) {
        size_t l;
        char *p = luaL_prepbuffer(&b);
        if (fgets(p, LUAL_BUFFERSIZE, f) == NULL) {
            luaL_pushresult(&b);
            return (lua_objlen(L, -1) > 0);
        }
        l = strlen(p);
        if (l == 0 || p[l-1] != '\n')
            luaL_addsize(&b, l);
        else {
            luaL_addsize(&b, l - 1);
            luaL_pushresult(&b);
            return 1;
        }
    }
}


/**
 * @brief 从文件流中读取指定数量的字符
 * @details 从给定的文件流中读取指定数量的字符，实现Lua I/O库中的数值读取功能。
 *          使用分块读取策略，通过缓冲区系统高效处理大量数据的读取操作。
 *          该函数是实现file:read(n)和io.read(n)功能的核心实现。
 * 
 * @param L Lua虚拟机状态指针，用于缓冲区操作和结果返回
 * @param f 要读取的文件流指针，必须是已打开的可读文件
 * @param n 要读取的字符数量，为0时读取所有剩余内容
 * 
 * @return int 读取结果状态
 *         - 1: 成功读取到请求的字符数量
 *         - 1: 读取到部分字符但遇到文件结束（读取的内容长度>0）
 *         - 0: 没有读取到任何字符（已在文件末尾）
 * 
 * @note 实现细节：
 *       - 采用分块读取策略，每次读取LUAL_BUFFERSIZE大小的数据块
 *       - 使用luaL_Buffer缓冲区系统，避免频繁的内存重新分配
 *       - 通过fread()函数实现底层的字符读取操作
 *       - 自动调整每次读取的大小，避免超出请求的字符数量
 *       - 循环读取直到达到请求数量或遇到文件结束
 * 
 * @warning 注意事项：
 *          - 文件流f必须是有效的已打开文件，否则行为未定义
 *          - 参数n为0时会读取整个文件剩余内容，可能消耗大量内存
 *          - 对于二进制文件，字符的概念与文本文件可能不同
 *          - 读取的内容会被推送到Lua栈顶，调用者需要管理栈状态
 * 
 * @see luaL_Buffer 缓冲区系统文档
 * @see fread() C标准库函数
 * @see lua_objlen() 获取Lua对象长度
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("data.bin", "rb")
 * local data = file:read(1024)  -- 读取1024个字符，内部调用此函数
 * file:close()
 */
/**
 * @brief 从文件流中读取指定数量的字符
 * @details 从给定的文件流中读取指定数量的字符，实现Lua I/O库中的数值读取功能。
 *          使用分块读取策略，通过缓冲区系统高效处理大量数据的读取操作。
 *          该函数是实现file:read(n)和io.read(n)功能的核心实现。
 * 
 * @param L Lua虚拟机状态指针，用于缓冲区操作和结果返回
 * @param f 要读取的文件流指针，必须是已打开的可读文件
 * @param n 要读取的字符数量，为0时读取所有剩余内容
 * 
 * @return int 读取结果状态
 *         - 1: 成功读取到请求的字符数量
 *         - 1: 读取到部分字符但遇到文件结束（读取的内容长度>0）
 *         - 0: 没有读取到任何字符（已在文件末尾）
 * 
 * @note 实现细节：
 *       - 采用分块读取策略，每次读取LUAL_BUFFERSIZE大小的数据块
 *       - 使用luaL_Buffer缓冲区系统，避免频繁的内存重新分配
 *       - 通过fread()函数实现底层的字符读取操作
 *       - 自动调整每次读取的大小，避免超出请求的字符数量
 *       - 循环读取直到达到请求数量或遇到文件结束
 * 
 * @warning 注意事项：
 *          - 文件流f必须是有效的已打开文件，否则行为未定义
 *          - 参数n为0时会读取整个文件剩余内容，可能消耗大量内存
 *          - 对于二进制文件，字符的概念与文本文件可能不同
 *          - 读取的内容会被推送到Lua栈顶，调用者需要管理栈状态
 * 
 * @see luaL_Buffer 缓冲区系统文档
 * @see fread() C标准库函数
 * @see lua_objlen() 获取Lua对象长度
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("data.bin", "rb")
 * local data = file:read(1024)  -- 读取1024个字符，内部调用此函数
 * file:close()
 */
static int read_chars(lua_State *L, FILE *f, size_t n)
{
    size_t rlen;
    size_t nr;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    rlen = LUAL_BUFFERSIZE;
    do {
        char *p = luaL_prepbuffer(&b);
        if (rlen > n) rlen = n;
        nr = fread(p, sizeof(char), rlen, f);
        luaL_addsize(&b, nr);
        n -= nr;
    } while (n > 0 && nr == rlen);
    luaL_pushresult(&b);
    return (n == 0 || lua_objlen(L, -1) > 0);
}


/**
 * @brief 通用文件读取函数，处理多种读取格式和参数
 * @details 这是Lua I/O库中的核心读取函数，支持多种读取模式和参数组合。
 *          能够处理数字、字符串格式说明符，并支持同时读取多个值。
 *          该函数是file:read()和io.read()方法的底层实现。
 * 
 * @param L Lua虚拟机状态指针，用于参数获取和结果返回
 * @param f 要读取的文件流指针，必须是已打开的可读文件
 * @param first 参数在Lua栈中的起始位置索引
 * 
 * @return int 返回值的数量（推送到Lua栈的值的个数）
 *         - 0: 读取失败或遇到错误
 *         - n: 成功读取n个值
 * 
 * @note 支持的读取格式：
 *       - 无参数: 读取一行（默认行为）
 *       - 数字n: 读取n个字符，0表示测试文件结束
 *       - "*n": 读取一个数字
 *       - "*l": 读取一行（不包含换行符）
 *       - "*a": 读取整个文件剩余内容
 * 
 * @note 实现细节：
 *       - 使用clearerr()清除文件流的错误状态
 *       - 支持多参数读取，一次调用读取多个值
 *       - 自动管理Lua栈空间，确保有足够空间存储结果
 *       - 处理读取失败的情况，将失败的结果替换为nil
 *       - 检查文件流错误状态，及时报告I/O错误
 * 
 * @warning 注意事项：
 *          - 文件流f必须是有效的已打开文件
 *          - 对于无效的格式说明符会抛出Lua错误
 *          - 多参数读取时，如果中间某个读取失败，后续读取会停止
 *          - 读取"*a"格式可能消耗大量内存
 * 
 * @see read_line() 行读取函数
 * @see read_chars() 字符读取函数
 * @see read_number() 数字读取函数
 * @see test_eof() 文件结束测试函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("data.txt", "r")
 * local num, line = file:read("*n", "*l")  -- 读取数字和一行，内部调用此函数
 * local data = file:read("*a")             -- 读取全部内容
 * file:close()
 */
static int g_read(lua_State *L, FILE *f, int first)
{
    int nargs = lua_gettop(L) - 1;
    int success;
    int n;
    clearerr(f);
    if (nargs == 0) {
        success = read_line(L, f);
        n = first + 1;
    }
    else {
        luaL_checkstack(L, nargs + LUA_MINSTACK, "too many arguments");
        success = 1;
        for (n = first; nargs-- && success; n++) {
            if (lua_type(L, n) == LUA_TNUMBER) {
                size_t l = (size_t)lua_tointeger(L, n);
                success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
            }
            else {
                const char *p = lua_tostring(L, n);
                luaL_argcheck(L, p && p[0] == '*', n, "invalid option");
                switch (p[1]) {
                    case 'n':
                        success = read_number(L, f);
                        break;
                    case 'l':
                        success = read_line(L, f);
                        break;
                    case 'a':
                        read_chars(L, f, ~((size_t)0));
                        success = 1;
                        break;
                    default:
                        return luaL_argerror(L, n, "invalid format");
                }
            }
        }
    }
    if (ferror(f))
        return pushresult(L, 0, NULL);
    if (!success) {
        lua_pop(L, 1);
        lua_pushnil(L);
    }
    return n - first;
}


/**
 * @brief 全局I/O读取函数，从默认输入文件读取数据
 * @details 实现Lua全局函数io.read()的功能，从当前默认输入文件读取数据。
 *          这是对g_read()函数的简单包装，使用默认输入文件作为数据源。
 * 
 * @param L Lua虚拟机状态指针，用于参数传递和结果返回
 * 
 * @return int 返回值的数量（推送到Lua栈的值的个数）
 *         - 返回g_read()的执行结果
 * 
 * @note 使用说明：
 *       - 从getiofile(L, IO_INPUT)获取的默认输入文件读取
 *       - 支持所有g_read()函数支持的读取格式
 *       - 参数从栈位置1开始处理
 * 
 * @see g_read() 通用读取函数
 * @see getiofile() 获取默认I/O文件函数
 */
static int io_read (lua_State *L) {
    return g_read(L, getiofile(L, IO_INPUT), 1);
}


/**
 * @brief 文件对象读取方法，从特定文件对象读取数据
 * @details 实现Lua文件对象的read()方法功能，从特定的文件对象读取数据。
 *          这是对g_read()函数的包装，使用文件对象作为数据源。
 * 
 * @param L Lua虚拟机状态指针，栈顶应该是文件对象
 * 
 * @return int 返回值的数量（推送到Lua栈的值的个数）
 *         - 返回g_read()的执行结果
 * 
 * @note 使用说明：
 *       - 从tofile(L)获取的文件对象读取
 *       - 支持所有g_read()函数支持的读取格式
 *       - 参数从栈位置2开始处理（位置1是文件对象）
 * 
 * @see g_read() 通用读取函数
 * @see tofile() 获取文件对象函数
 */
static int f_read (lua_State *L) {
    return g_read(L, tofile(L), 2);
}


/**
 * @brief 行迭代器函数，用于文件的逐行读取迭代
 * @details 这是一个特殊的函数，作为Lua闭包的一部分实现文件的逐行迭代功能。
 *          通常由io.lines()或file:lines()方法创建，用于在for循环中逐行遍历文件。
 *          该函数使用upvalue存储文件句柄和关闭标志。
 * 
 * @param L Lua虚拟机状态指针，用于访问upvalue和返回结果
 * 
 * @return int 返回值数量
 *         - 1: 成功读取到一行，返回该行内容
 *         - 0: 到达文件末尾或出现错误，结束迭代
 * 
 * @note 实现细节：
 *       - upvalue[1]: 存储文件句柄指针(FILE**)
 *       - upvalue[2]: 存储是否由迭代器创建文件的布尔标志
 *       - 使用read_line()函数实现实际的行读取逻辑
 *       - 自动处理文件错误和EOF情况
 *       - 在迭代结束时自动关闭由迭代器创建的文件
 * 
 * @warning 注意事项：
 *          - 如果文件已关闭会抛出Lua错误
 *          - 文件I/O错误会导致Lua错误并终止迭代
 *          - 只有由迭代器本身打开的文件才会在结束时自动关闭
 *          - 该函数通常不直接调用，而是作为迭代器使用
 * 
 * @see read_line() 行读取函数
 * @see aux_close() 文件关闭函数
 * @see io.lines() Lua全局行迭代函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * for line in io.lines("file.txt") do  -- 内部使用此函数作为迭代器
 *     print(line)
 * end
 */
/**
 * @brief 行迭代器函数，用于文件的逐行读取迭代
 * @details 这是一个特殊的函数，作为Lua闭包的一部分实现文件的逐行迭代功能。
 *          通常由io.lines()或file:lines()方法创建，用于在for循环中逐行遍历文件。
 *          该函数使用upvalue存储文件句柄和关闭标志。
 * 
 * @param L Lua虚拟机状态指针，用于访问upvalue和返回结果
 * 
 * @return int 返回值数量
 *         - 1: 成功读取到一行，返回该行内容
 *         - 0: 到达文件末尾或出现错误，结束迭代
 * 
 * @note 实现细节：
 *       - upvalue[1]: 存储文件句柄指针(FILE**)
 *       - upvalue[2]: 存储是否由迭代器创建文件的布尔标志
 *       - 使用read_line()函数实现实际的行读取逻辑
 *       - 自动处理文件错误和EOF情况
 *       - 在迭代结束时自动关闭由迭代器创建的文件
 * 
 * @warning 注意事项：
 *          - 如果文件已关闭会抛出Lua错误
 *          - 文件I/O错误会导致Lua错误并终止迭代
 *          - 只有由迭代器本身打开的文件才会在结束时自动关闭
 *          - 该函数通常不直接调用，而是作为迭代器使用
 * 
 * @see read_line() 行读取函数
 * @see aux_close() 文件关闭函数
 * @see io.lines() Lua全局行迭代函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * for line in io.lines("file.txt") do  -- 内部使用此函数作为迭代器
 *     print(line)
 * end
 */
static int io_readline(lua_State *L)
{
    FILE *f = *(FILE **)lua_touserdata(L, lua_upvalueindex(1));
    int sucess;
    if (f == NULL)
        luaL_error(L, "file is already closed");
    sucess = read_line(L, f);
    if (ferror(f))
        return luaL_error(L, "%s", strerror(errno));
    if (sucess) return 1;
    else {
        if (lua_toboolean(L, lua_upvalueindex(2))) {
            lua_settop(L, 0);
            lua_pushvalue(L, lua_upvalueindex(1));
            aux_close(L);
        }
        return 0;
    }
}

/* }====================================================== */


/**
 * @brief 通用文件写入函数，处理多种数据类型的写入操作
 * @details 这是Lua I/O库中的核心写入函数，支持将多个参数写入到指定文件。
 *          能够处理数字和字符串类型的数据，自动进行类型转换和格式化。
 *          该函数是file:write()和io.write()方法的底层实现。
 * 
 * @param L Lua虚拟机状态指针，用于参数获取和结果返回
 * @param f 要写入的文件流指针，必须是已打开的可写文件
 * @param arg 参数在Lua栈中的起始位置索引
 * 
 * @return int 写入操作的结果
 *         - 1: 所有参数成功写入，返回true
 *         - 3: 写入失败，返回nil、错误消息和错误代码
 * 
 * @note 支持的数据类型：
 *       - 数字：使用LUA_NUMBER_FMT格式化输出
 *       - 字符串：直接二进制写入，保持原始内容
 *       - 混合参数：支持数字和字符串混合写入
 * 
 * @note 实现细节：
 *       - 遍历所有参数，按照在栈中的顺序写入
 *       - 数字类型使用fprintf()进行格式化输出
 *       - 字符串类型使用fwrite()进行二进制写入
 *       - 任何一个参数写入失败都会导致整体失败
 *       - 使用pushresult()统一处理返回值格式
 * 
 * @warning 注意事项：
 *          - 文件流f必须是有效的已打开可写文件
 *          - 写入失败时不会回滚已写入的内容
 *          - 对于大量数据写入，建议考虑缓冲策略
 *          - 混合数据类型写入时要注意格式一致性
 * 
 * @see pushresult() 结果处理函数
 * @see fprintf() C标准库格式化输出函数
 * @see fwrite() C标准库二进制写入函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("output.txt", "w")
 * file:write("Number: ", 42, "\nString: ", "Hello")  -- 内部调用此函数
 * file:close()
 */
static int g_write(lua_State *L, FILE *f, int arg)
{
    int nargs = lua_gettop(L) - 1;
    int status = 1;
    for (; nargs--; arg++) {
        if (lua_type(L, arg) == LUA_TNUMBER) {
            status = status &&
                fprintf(f, LUA_NUMBER_FMT, lua_tonumber(L, arg)) > 0;
        }
        else {
            size_t l;
            const char *s = luaL_checklstring(L, arg, &l);
            status = status && (fwrite(s, sizeof(char), l, f) == l);
        }
    }
    return pushresult(L, status, NULL);
}


/**
 * @brief 全局I/O写入函数，向默认输出文件写入数据
 * @details 实现Lua全局函数io.write()的功能，向当前默认输出文件写入数据。
 *          这是对g_write()函数的简单包装，使用默认输出文件作为目标。
 * 
 * @param L Lua虚拟机状态指针，用于参数传递和结果返回
 * 
 * @return int 写入操作的结果
 *         - 返回g_write()的执行结果
 * 
 * @note 使用说明：
 *       - 向getiofile(L, IO_OUTPUT)获取的默认输出文件写入
 *       - 支持所有g_write()函数支持的数据类型
 *       - 参数从栈位置1开始处理
 * 
 * @see g_write() 通用写入函数
 * @see getiofile() 获取默认I/O文件函数
 */
static int io_write(lua_State *L)
{
    return g_write(L, getiofile(L, IO_OUTPUT), 1);
}


/**
 * @brief 文件对象写入方法，向特定文件对象写入数据
 * @details 实现Lua文件对象的write()方法功能，向特定的文件对象写入数据。
 *          这是对g_write()函数的包装，使用文件对象作为目标。
 * 
 * @param L Lua虚拟机状态指针，栈顶应该是文件对象
 * 
 * @return int 写入操作的结果
 *         - 返回g_write()的执行结果
 * 
 * @note 使用说明：
 *       - 向tofile(L)获取的文件对象写入
 *       - 支持所有g_write()函数支持的数据类型
 *       - 参数从栈位置2开始处理（位置1是文件对象）
 * 
 * @see g_write() 通用写入函数
 * @see tofile() 获取文件对象函数
 */
static int f_write(lua_State *L)
{
    return g_write(L, tofile(L), 2);
}


/**
 * @brief 文件定位函数，设置或获取文件的读写位置
 * @details 实现Lua文件对象的seek()方法功能，用于在文件中移动读写指针。
 *          支持相对和绝对定位，可以移动到文件的任意位置进行读写操作。
 *          该函数封装了C标准库的fseek()和ftell()函数。
 * 
 * @param L Lua虚拟机状态指针，参数：[whence], [offset]
 * 
 * @return int 操作结果
 *         - 1: 成功，返回新的文件位置
 *         - 3: 失败，返回nil、错误消息和错误代码
 * 
 * @note 支持的定位模式：
 *       - "set": 相对于文件开头的绝对位置
 *       - "cur": 相对于当前位置的偏移（默认）
 *       - "end": 相对于文件末尾的偏移
 * 
 * @note 参数说明：
 *       - whence: 定位模式字符串，默认为"cur"
 *       - offset: 偏移量，默认为0
 * 
 * @note 实现细节：
 *       - 使用静态数组映射Lua模式名到C常量
 *       - 调用fseek()执行实际的文件定位操作
 *       - 定位成功后使用ftell()获取新位置
 *       - 使用pushresult()统一处理错误情况
 * 
 * @warning 注意事项：
 *          - 只能用于可定位的文件（不支持管道、终端等）
 *          - 对于文本文件，某些位置可能无效
 *          - 超出文件边界的定位可能导致未定义行为
 * 
 * @see fseek() C标准库文件定位函数
 * @see ftell() C标准库位置获取函数
 * @see pushresult() 结果处理函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("data.txt", "r+")
 * file:seek("set", 100)    -- 移动到文件开头后100字节
 * local pos = file:seek()  -- 获取当前位置
 * file:seek("end", -10)    -- 移动到文件末尾前10字节
 * file:close()
 */
static int f_seek(lua_State *L)
{
    static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    static const char *const modenames[] = {"set", "cur", "end", NULL};
    FILE *f = tofile(L);
    int op = luaL_checkoption(L, 2, "cur", modenames);
    long offset = luaL_optlong(L, 3, 0);
    op = fseek(f, offset, mode[op]);
    if (op)
        return pushresult(L, 0, NULL);
    else {
        lua_pushinteger(L, ftell(f));
        return 1;
    }
}


/**
 * @brief 文件缓冲模式设置函数，配置文件流的缓冲行为
 * @details 实现Lua文件对象的setvbuf()方法功能，用于设置文件流的缓冲模式和大小。
 *          不同的缓冲模式可以影响I/O操作的性能和实时性。
 *          该函数封装了C标准库的setvbuf()函数。
 * 
 * @param L Lua虚拟机状态指针，参数：mode, [size]
 * 
 * @return int 操作结果
 *         - 1: 成功设置缓冲模式，返回true
 *         - 3: 设置失败，返回nil、错误消息和错误代码
 * 
 * @note 支持的缓冲模式：
 *       - "no": 无缓冲模式(_IONBF)，立即写入
 *       - "full": 全缓冲模式(_IOFBF)，缓冲区满时写入
 *       - "line": 行缓冲模式(_IOLBF)，遇到换行符时写入
 * 
 * @note 参数说明：
 *       - mode: 缓冲模式字符串，必需参数
 *       - size: 缓冲区大小，可选，默认为LUAL_BUFFERSIZE
 * 
 * @note 实现细节：
 *       - 使用静态数组映射Lua模式名到C常量
 *       - 调用setvbuf()设置文件流缓冲模式
 *       - 缓冲区由系统自动分配管理
 *       - 使用pushresult()统一处理返回值
 * 
 * @warning 注意事项：
 *          - 必须在文件打开后、第一次I/O操作前调用
 *          - 某些文件类型可能不支持特定缓冲模式
 *          - 缓冲区大小建议为系统页面大小的倍数
 *          - 改变缓冲模式可能影响程序性能
 * 
 * @see setvbuf() C标准库缓冲设置函数
 * @see pushresult() 结果处理函数
 * 
 * @example
 * // 使用示例（在Lua中）：
 * local file = io.open("log.txt", "w")
 * file:setvbuf("line")     -- 设置行缓冲模式
 * file:setvbuf("full", 8192) -- 设置全缓冲，8KB缓冲区
 * file:close()
 */
static int f_setvbuf(lua_State *L)
{
    static const int mode[] = {_IONBF, _IOFBF, _IOLBF};
    static const char *const modenames[] = {"no", "full", "line", NULL};
    FILE *f = tofile(L);
    int op = luaL_checkoption(L, 2, NULL, modenames);
    lua_Integer sz = luaL_optinteger(L, 3, LUAL_BUFFERSIZE);
    int res = setvbuf(f, NULL, mode[op], sz);
    return pushresult(L, res == 0, NULL);
}



/**
 * @brief 全局I/O刷新函数，刷新默认输出文件缓冲区
 * @details 实现Lua全局函数io.flush()的功能，强制将默认输出文件缓冲区的内容
 *          立即写入到物理存储设备。确保所有待写入的数据得到及时处理。
 * 
 * @param L Lua虚拟机状态指针
 * 
 * @return int 刷新操作的结果
 *         - 1: 刷新成功，返回true
 *         - 3: 刷新失败，返回nil、错误消息和错误代码
 * 
 * @note 使用说明：
 *       - 对getiofile(L, IO_OUTPUT)获取的默认输出文件执行刷新
 *       - 适用于需要确保数据立即可见的场景
 *       - 对于行缓冲和全缓冲模式特别有用
 * 
 * @see fflush() C标准库缓冲区刷新函数
 * @see getiofile() 获取默认I/O文件函数
 * @see pushresult() 结果处理函数
 */
static int io_flush(lua_State *L)
{
    return pushresult(L, fflush(getiofile(L, IO_OUTPUT)) == 0, NULL);
}


/**
 * @brief 文件对象刷新方法，刷新特定文件对象的缓冲区
 * @details 实现Lua文件对象的flush()方法功能，强制将指定文件对象缓冲区的内容
 *          立即写入到物理存储设备。确保文件数据的及时同步。
 * 
 * @param L Lua虚拟机状态指针，栈顶应该是文件对象
 * 
 * @return int 刷新操作的结果
 *         - 1: 刷新成功，返回true  
 *         - 3: 刷新失败，返回nil、错误消息和错误代码
 * 
 * @note 使用说明：
 *       - 对tofile(L)获取的文件对象执行刷新
 *       - 只对输出流有效，输入流调用无实际效果
 *       - 适用于关键数据的及时保存
 * 
 * @see fflush() C标准库缓冲区刷新函数
 * @see tofile() 获取文件对象函数
 * @see pushresult() 结果处理函数
 */
static int f_flush(lua_State *L)
{
    return pushresult(L, fflush(tofile(L)) == 0, NULL);
}


static const luaL_Reg iolib[] = {
  {"close", io_close},
  {"flush", io_flush},
  {"input", io_input},
  {"lines", io_lines},
  {"open", io_open},
  {"output", io_output},
  {"popen", io_popen},
  {"read", io_read},
  {"tmpfile", io_tmpfile},
  {"type", io_type},
  {"write", io_write},
  {NULL, NULL}
};


static const luaL_Reg flib[] = {
  {"close", io_close},
  {"flush", f_flush},
  {"lines", f_lines},
  {"read", f_read},
  {"seek", f_seek},
  {"setvbuf", f_setvbuf},
  {"write", f_write},
  {"__gc", io_gc},
  {"__tostring", io_tostring},
  {NULL, NULL}
};


/**
 * @brief 创建文件句柄元表：为文件对象设置元方法
 * 
 * 详细说明：
 * 这个函数创建文件句柄的元表(metatable)，为文件对象定义各种方法和行为。
 * 元表可以为用户数据类型提供操作符重载、方法调用等高级特性。
 * 
 * 实现机制：
 * 1. 使用luaL_newmetatable创建名为LUA_FILEHANDLE的元表
 * 2. 将元表本身设置为__index元方法，实现方法查找
 * 3. 注册文件操作相关的方法到元表中
 * 
 * 功能作用：
 * - 使得文件对象可以调用file:read()、file:write()等方法
 * - 提供文件对象的统一接口和行为
 * - 实现面向对象的文件操作模式
 * 
 * 注意事项：
 * - 该函数是库初始化的关键步骤
 * - flib数组包含所有文件对象的方法定义
 * - __index元方法允许文件对象访问其方法
 * 
 * @param L Lua状态机指针
 * 
 * @return void 无返回值，但在栈中留下创建的元表
 * 
 * @see LUA_FILEHANDLE 文件句柄的元表名称
 * @see flib 文件方法注册表
 * @see luaL_newmetatable() Lua辅助库元表创建函数
 */
static void createmeta(lua_State *L) {
    luaL_newmetatable(L, LUA_FILEHANDLE);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_register(L, NULL, flib);
}


/**
 * @brief 创建标准文件对象：初始化stdin/stdout/stderr
 * 
 * 详细说明：
 * 这个函数为系统的标准输入输出流创建Lua文件对象，并将它们注册到全局环境中。
 * 这些标准文件对象允许Lua程序直接访问系统的输入输出流。
 * 
 * 实现机制：
 * 1. 使用newfile()创建新的Lua文件对象
 * 2. 将系统文件指针(FILE*)赋值给Lua文件对象
 * 3. 如果k>0，将文件对象注册到环境表中的指定索引
 * 4. 设置文件对象的环境和名称
 * 
 * 参数含义：
 * - f: 系统文件指针（stdin/stdout/stderr）
 * - k: 环境表索引（IO_INPUT=1, IO_OUTPUT=2, stderr=0）
 * - fname: 文件名称字符串（"stdin"/"stdout"/"stderr"）
 * 
 * 使用场景：
 * - 初始化io库时创建标准文件对象
 * - 为系统文件流提供Lua接口封装
 * 
 * 注意事项：
 * - 标准文件对象不应该被关闭
 * - k=0的文件（stderr）不会注册到环境表
 * - 这些对象会继承当前的环境设置
 * 
 * @param L Lua状态机指针
 * @param f 系统文件指针（stdin/stdout/stderr）
 * @param k 环境表索引（>0为有效索引，0为不注册）
 * @param fname 文件名称字符串
 * 
 * @return void 无返回值，但修改全局环境和栈状态
 * 
 * @see newfile() 文件对象创建函数
 * @see IO_INPUT/IO_OUTPUT 环境表索引常量
 */
static void createstdfile(lua_State *L, FILE *f, int k, const char *fname) {
    *newfile(L) = f;
    if (k > 0) {
        lua_pushvalue(L, -1);
        lua_rawseti(L, LUA_ENVIRONINDEX, k);
    }
    lua_pushvalue(L, -2);
    lua_setfenv(L, -2);
    lua_setfield(L, -3, fname);
}


/**
 * @brief 创建新环境表：为特定文件类型设置关闭函数
 * 
 * 详细说明：
 * 这个函数创建一个新的环境表，并为其设置特定的__close元方法。
 * 不同类型的文件（标准文件、普通文件、管道文件）需要不同的关闭方式。
 * 
 * 实现机制：
 * 1. 创建一个新的空表作为环境
 * 2. 将指定的关闭函数设置为__close元方法
 * 3. 这个环境可以被赋予给文件对象
 * 
 * 应用场景：
 * - 为标准文件设置io_noclose（不允许关闭）
 * - 为普通文件设置io_fclose（标准关闭）
 * - 为管道文件设置io_pclose（管道关闭）
 * 
 * 技术细节：
 * - 环境表的__close元方法会在文件对象被垃圾回收时调用
 * - 不同的关闭函数实现不同的清理逻辑
 * - 环境表提供了类型安全的文件操作机制
 * 
 * 注意事项：
 * - 该函数是Lua I/O库的内部实现细节
 * - 正确的环境设置对文件安全性至关重要
 * - 不同类型文件的混用可能导致问题
 * 
 * @param L Lua状态机指针
 * @param cls 关闭函数指针（io_fclose/io_noclose/io_pclose）
 * 
 * @return void 无返回值，但在栈顶留下创建的环境表
 * 
 * @see io_fclose() 标准文件关闭函数
 * @see io_noclose() 禁止关闭函数
 * @see io_pclose() 管道文件关闭函数
 */
static void newfenv(lua_State *L, lua_CFunction cls) {
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, cls);
    lua_setfield(L, -2, "__close");
}


/**
 * @brief Lua I/O库主初始化函数：完整初始化I/O子系统
 * 
 * 详细说明：
 * 这是Lua I/O库的入口点，负责初始化整个I/O子系统的所有组件，包括：
 * 元表创建、环境设置、函数注册、标准文件初始化等。
 * 该函数确保所有I/O操作都能正常工作。
 * 
 * 初始化步骤：
 * 1. **元表创建**: 调用createmeta()为文件对象创建元表
 * 2. **环境设置**: 创建私有环境，设置默认关闭函数
 * 3. **库注册**: 注册所有io.*函数到全局名空间
 * 4. **标准文件**: 初始化stdin/stdout/stderr文件对象
 * 5. **特殊环境**: 为popen函数设置专用环境
 * 
 * 环境管理：
 * - 主环境: 包含IO_INPUT, IO_OUTPUT, __close字段
 * - 标准文件环境: 使用io_noclose禁止关闭
 * - popen环境: 使用io_pclose处理管道关闭
 * 
 * 注册内容：
 * - io.input, io.output, io.open, io.close, io.read, io.write等函数
 * - io.lines, io.flush, io.type, io.tmpfile, io.popen等工具函数
 * - file:read, file:write, file:seek, file:close等方法
 * 
 * 技术细节：
 * - 使用LUA_ENVIRONINDEX管理私有环境
 * - 通过lua_setfenv为不同函数设置不同环境
 * - 保证线程安全和内存安全
 * 
 * 注意事项：
 * - 该函数只应该被调用一次
 * - 初始化失败可能导致整个I/O系统不可用
 * - 所有环境和元表设置都是必需的
 * 
 * @param L Lua状态机指针
 * 
 * @return int 返回值总是1，表示成功初始化并在栈上留下io表
 * 
 * @see createmeta() 元表创建函数
 * @see newfenv() 环境创建函数  
 * @see createstdfile() 标准文件创建函数
 * @see iolib/flib 函数注册表
 * 
 * @note 该函数符合Lua库打开函数的标准模式
 */
LUALIB_API int luaopen_io(lua_State *L) {
    createmeta(L);
    newfenv(L, io_fclose);
    lua_replace(L, LUA_ENVIRONINDEX);
    luaL_register(L, LUA_IOLIBNAME, iolib);
    newfenv(L, io_noclose);
    createstdfile(L, stdin, IO_INPUT, "stdin");
    createstdfile(L, stdout, IO_OUTPUT, "stdout");
    createstdfile(L, stderr, 0, "stderr");
    lua_pop(L, 1);
    lua_getfield(L, -1, "popen");
    newfenv(L, io_pclose);
    lua_setfenv(L, -2);
    lua_pop(L, 1);
    return 1;
}

