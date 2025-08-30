/*
** [核心] Lua 标准输入输出库实现
**
** 功能概述：
** 本模块实现了 Lua 的标准 I/O 库，提供文件操作、输入输出处理等功能。
** 包含文件打开、读写、定位、缓冲控制等完整的文件系统操作接口。
**
** 主要功能模块：
** - 文件句柄管理：创建、销毁、类型检查
** - 文件操作：打开、关闭、读取、写入
** - 流控制：定位、缓冲、刷新
** - 标准流：stdin、stdout、stderr 的封装
** - 错误处理：统一的错误报告和异常处理机制
**
** 设计特点：
** - 采用用户数据封装 FILE* 指针，提供垃圾回收支持
** - 实现了完整的元表机制，支持面向对象的文件操作
** - 提供了灵活的读取模式（按行、按字符数、按格式）
** - 统一的错误处理和状态管理
**
** 版本信息：$Id: liolib.c,v 2.73.1.4 2010/05/14 15:33:51 roberto Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

// 系统头文件包含
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 模块标识定义
#define liolib_c
#define LUA_LIB

// Lua 核心头文件
#include "lua.h"

// Lua 辅助库头文件
#include "lauxlib.h"
#include "lualib.h"

// I/O 流类型常量定义
#define IO_INPUT	1    // 标准输入流标识
#define IO_OUTPUT	2    // 标准输出流标识

/*
** [数据结构] 标准文件名称数组
**
** 用途说明：
** 存储标准输入输出流的名称字符串，用于错误消息和调试信息。
** 数组索引对应 IO_INPUT 和 IO_OUTPUT 常量减1的值。
*/
static const char *const fnames[] = {"input", "output"};

/*
** [工具函数] 推送操作结果到 Lua 栈
**
** 功能描述：
** 根据操作成功与否，向 Lua 栈推送相应的返回值。
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
*/
static int pushresult(lua_State *L, int i, const char *filename) 
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
        // 操作失败，推送错误信息
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
** [错误处理] 文件操作错误报告
**
** 功能描述：
** 生成包含文件名和系统错误信息的错误消息，并触发参数错误。
** 用于统一处理文件操作中的错误情况。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param arg - int：发生错误的参数位置
** @param filename - const char*：相关文件名
**
** 行为说明：
** 此函数不会返回，会直接触发 Lua 错误并进行长跳转
*/
static void fileerror(lua_State *L, int arg, const char *filename) 
{
    // 构造包含文件名和错误信息的消息
    lua_pushfstring(L, "%s: %s", filename, strerror(errno));
    
    // 触发参数错误，使用栈顶的错误消息
    luaL_argerror(L, arg, lua_tostring(L, -1));
}

/*
** [宏定义] 获取文件句柄指针
**
** 功能说明：
** 从 Lua 栈的第一个位置获取文件句柄用户数据，并转换为 FILE** 类型。
** 同时验证用户数据的类型是否为 LUA_FILEHANDLE。
*/
#define tofilep(L)	((FILE **)luaL_checkudata(L, 1, LUA_FILEHANDLE))

/*
** [类型检查] 检查 Lua 值的文件类型
**
** 功能描述：
** 检查栈上的值是否为文件句柄，并返回相应的类型字符串。
** 可以区分打开的文件、关闭的文件和非文件对象。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 推送以下字符串之一：
** - "file"：打开的文件句柄
** - "closed file"：已关闭的文件句柄  
** - nil：不是文件句柄
*/
static int io_type(lua_State *L) 
{
    void *ud;
    
    // 检查是否有参数
    luaL_checkany(L, 1);
    
    // 获取用户数据指针
    ud = lua_touserdata(L, 1);
    
    // 获取文件句柄的元表
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_FILEHANDLE);
    
    // 检查是否为用户数据且具有正确的元表
    if (ud == NULL || !lua_getmetatable(L, 1) || !lua_rawequal(L, -2, -1))
    {
        // 不是文件句柄
        lua_pushnil(L);
    }
    else if (*((FILE **)ud) == NULL)
    {
        // 是文件句柄但已关闭
        lua_pushliteral(L, "closed file");
    }
    else
    {
        // 是打开的文件句柄
        lua_pushliteral(L, "file");
    }
    
    return 1;
}

/*
** [工具函数] 获取有效的文件指针
**
** 功能描述：
** 从 Lua 栈获取文件句柄并验证其有效性。
** 如果文件已关闭，则抛出错误。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return FILE*：有效的文件指针
**
** 错误处理：
** 如果文件已关闭，会触发 Lua 错误
*/
static FILE *tofile(lua_State *L)
{
    FILE **f = tofilep(L);

    if (*f == NULL)
    {
        luaL_error(L, "attempt to use a closed file");
    }

    return *f;
}

/*
** [内存管理] 创建新的文件句柄用户数据
**
** 功能描述：
** 创建一个新的文件句柄用户数据，并设置相应的元表。
** 初始状态下文件句柄为关闭状态，避免内存错误时文件泄漏。
**
** 设计思路：
** 采用"先关闭后打开"的策略：
** 1. 首先创建一个关闭状态的文件句柄
** 2. 设置正确的元表以支持垃圾回收
** 3. 然后再尝试打开实际文件
** 这样即使在打开文件时发生内存错误，也不会导致文件句柄泄漏
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return FILE**：指向文件指针的指针，初始值为 NULL
**
** 栈操作：
** 在栈顶创建一个新的用户数据，并设置文件句柄元表
*/
static FILE **newfile(lua_State *L)
{
    // 创建用户数据来存储 FILE* 指针
    FILE **pf = (FILE **)lua_newuserdata(L, sizeof(FILE *));

    // 初始化为关闭状态，防止内存错误时的文件泄漏
    *pf = NULL;

    // 获取并设置文件句柄元表
    luaL_getmetatable(L, LUA_FILEHANDLE);
    lua_setmetatable(L, -2);

    return pf;
}

/*
** [特殊处理] 标准文件流的关闭函数
**
** 功能描述：
** 用于标准文件流（stdin、stdout、stderr）的关闭操作。
** 由于标准流不应该被关闭，此函数总是返回失败。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是2）
**
** 栈操作：
** 推送 nil 和错误消息字符串
*/
static int io_noclose(lua_State *L)
{
    lua_pushnil(L);
    lua_pushliteral(L, "cannot close standard file");
    return 2;
}

/*
** [文件操作] 关闭 popen 创建的文件
**
** 功能描述：
** 专门用于关闭通过 popen 创建的管道文件。
** 使用 lua_pclose 而不是标准的 fclose。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：pushresult 的返回值
**
** 栈操作：
** 推送操作结果（成功或失败信息）
*/
static int io_pclose(lua_State *L)
{
    FILE **p = tofilep(L);
    int ok = lua_pclose(L, *p);
    *p = NULL;
    return pushresult(L, ok, NULL);
}

/*
** [文件操作] 关闭普通文件
**
** 功能描述：
** 关闭通过 fopen 创建的普通文件。
** 使用标准的 fclose 函数。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：pushresult 的返回值
**
** 栈操作：
** 推送操作结果（成功或失败信息）
*/
static int io_fclose(lua_State *L)
{
    FILE **p = tofilep(L);
    int ok = (fclose(*p) == 0);
    *p = NULL;
    return pushresult(L, ok, NULL);
}

/*
** [辅助函数] 执行文件关闭操作
**
** 功能描述：
** 通过文件环境中的 __close 函数执行实际的关闭操作。
** 支持不同类型文件的统一关闭接口。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：__close 函数的返回值
*/
static int aux_close(lua_State *L)
{
    // 获取文件的环境表
    lua_getfenv(L, 1);

    // 获取环境表中的 __close 函数
    lua_getfield(L, -1, "__close");

    // 调用 __close 函数
    return (lua_tocfunction(L, -1))(L);
}

/*
** [核心函数] 关闭文件句柄
**
** 功能描述：
** Lua io.close() 函数的实现。如果没有参数，关闭默认输出文件。
** 否则关闭指定的文件句柄。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：aux_close 的返回值
**
** 使用示例：
** io.close()        -- 关闭默认输出文件
** io.close(file)    -- 关闭指定文件
*/
static int io_close(lua_State *L)
{
    if (lua_isnone(L, 1))
    {
        // 没有参数时，获取默认输出文件
        lua_rawgeti(L, LUA_ENVIRONINDEX, IO_OUTPUT);
    }

    // 确保参数是一个有效的文件句柄
    tofile(L);

    // 执行关闭操作
    return aux_close(L);
}

/*
** [垃圾回收] 文件句柄的垃圾回收函数
**
** 功能描述：
** 当文件句柄被垃圾回收时自动调用的函数。
** 确保即使用户忘记显式关闭文件，也能正确释放资源。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：总是返回 0
**
** 设计说明：
** 只有在文件未关闭时才执行关闭操作，避免重复关闭
*/
static int io_gc(lua_State *L)
{
    FILE *f = *tofilep(L);

    // 忽略已关闭的文件
    if (f != NULL)
    {
        aux_close(L);
    }

    return 0;
}

/*
** [元方法] 文件句柄的字符串表示
**
** 功能描述：
** 实现文件句柄的 __tostring 元方法。
** 为打开和关闭的文件提供不同的字符串表示。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 推送文件句柄的字符串表示
*/
static int io_tostring(lua_State *L)
{
    FILE *f = *tofilep(L);

    if (f == NULL)
    {
        lua_pushliteral(L, "file (closed)");
    }
    else
    {
        lua_pushfstring(L, "file (%p)", f);
    }

    return 1;
}

/*
** [核心函数] 打开文件
**
** 功能描述：
** Lua io.open() 函数的实现。根据指定的文件名和模式打开文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：成功返回1（文件句柄），失败返回3（nil、错误消息、错误码）
**
** 栈操作：
** 成功时：推送新创建的文件句柄
** 失败时：推送 nil、错误消息和错误码
**
** 使用示例：
** file = io.open("test.txt", "r")    -- 只读模式
** file = io.open("test.txt", "w")    -- 写入模式
*/
static int io_open(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    const char *mode = luaL_optstring(L, 2, "r");
    FILE **pf = newfile(L);

    *pf = fopen(filename, mode);

    return (*pf == NULL) ? pushresult(L, 0, filename) : 1;
}

/*
** [系统调用] 创建管道文件
**
** 功能描述：
** Lua io.popen() 函数的实现。执行系统命令并创建管道连接。
** 此函数有独立的环境，定义了正确的 __close 函数用于 popen 文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：成功返回1（文件句柄），失败返回3（nil、错误消息、错误码）
**
** 栈操作：
** 成功时：推送新创建的管道文件句柄
** 失败时：推送 nil、错误消息和错误码
**
** 使用示例：
** pipe = io.popen("ls -l", "r")      -- 读取命令输出
** pipe = io.popen("sort", "w")       -- 向命令写入数据
*/
static int io_popen(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    const char *mode = luaL_optstring(L, 2, "r");
    FILE **pf = newfile(L);

    *pf = lua_popen(L, filename, mode);

    return (*pf == NULL) ? pushresult(L, 0, filename) : 1;
}

/*
** [文件操作] 创建临时文件
**
** 功能描述：
** Lua io.tmpfile() 函数的实现。创建一个临时文件，
** 文件在关闭时会自动删除。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：成功返回1（文件句柄），失败返回3（nil、错误消息、错误码）
**
** 栈操作：
** 成功时：推送新创建的临时文件句柄
** 失败时：推送 nil、错误消息和错误码
**
** 使用示例：
** tmpfile = io.tmpfile()    -- 创建临时文件
*/
static int io_tmpfile(lua_State *L)
{
    FILE **pf = newfile(L);
    *pf = tmpfile();
    return (*pf == NULL) ? pushresult(L, 0, NULL) : 1;
}

/*
** [工具函数] 获取标准 I/O 文件
**
** 功能描述：
** 从环境中获取指定的标准 I/O 文件（stdin 或 stdout）。
** 如果文件已关闭，则抛出错误。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param findex - int：文件索引（IO_INPUT 或 IO_OUTPUT）
**
** 返回值：
** @return FILE*：标准文件指针
**
** 错误处理：
** 如果标准文件已关闭，会触发 Lua 错误
*/
static FILE *getiofile(lua_State *L, int findex)
{
    FILE *f;

    // 从环境中获取指定的文件句柄
    lua_rawgeti(L, LUA_ENVIRONINDEX, findex);
    f = *(FILE **)lua_touserdata(L, -1);

    if (f == NULL)
    {
        luaL_error(L, "standard %s file is closed", fnames[findex - 1]);
    }

    return f;
}

/*
** [通用函数] I/O 文件设置和获取
**
** 功能描述：
** 通用的文件设置函数，用于实现 io.input() 和 io.output()。
** 可以设置新的文件或获取当前文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param f - int：文件索引（IO_INPUT 或 IO_OUTPUT）
** @param mode - const char*：文件打开模式
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 行为说明：
** - 如果有参数且为字符串：打开指定文件名的文件
** - 如果有参数且为文件句柄：设置为当前文件
** - 如果没有参数：返回当前文件
*/
static int g_iofile(lua_State *L, int f, const char *mode)
{
    if (!lua_isnoneornil(L, 1))
    {
        const char *filename = lua_tostring(L, 1);

        if (filename)
        {
            // 参数是文件名字符串，打开文件
            FILE **pf = newfile(L);
            *pf = fopen(filename, mode);

            if (*pf == NULL)
            {
                fileerror(L, 1, filename);
            }
        }
        else
        {
            // 参数是文件句柄，验证其有效性
            tofile(L);
            lua_pushvalue(L, 1);
        }

        // 设置为当前文件
        lua_rawseti(L, LUA_ENVIRONINDEX, f);
    }

    // 返回当前文件
    lua_rawgeti(L, LUA_ENVIRONINDEX, f);
    return 1;
}

/*
** [核心函数] 设置或获取标准输入文件
**
** 功能描述：
** Lua io.input() 函数的实现。设置或获取当前的标准输入文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 使用示例：
** io.input("data.txt")    -- 设置输入文件
** file = io.input()       -- 获取当前输入文件
*/
static int io_input(lua_State *L)
{
    return g_iofile(L, IO_INPUT, "r");
}

/*
** [核心函数] 设置或获取标准输出文件
**
** 功能描述：
** Lua io.output() 函数的实现。设置或获取当前的标准输出文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 使用示例：
** io.output("result.txt")  -- 设置输出文件
** file = io.output()       -- 获取当前输出文件
*/
static int io_output(lua_State *L)
{
    return g_iofile(L, IO_OUTPUT, "w");
}

// 前向声明：行读取迭代器函数
static int io_readline(lua_State *L);

/*
** [辅助函数] 创建行迭代器
**
** 功能描述：
** 创建一个用于逐行读取文件的迭代器函数。
** 支持控制是否在迭代完成后自动关闭文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param idx - int：文件句柄在栈中的位置
** @param toclose - int：是否在完成后关闭文件
**
** 栈操作：
** 创建一个闭包，包含文件句柄和关闭标志作为上值
*/
static void aux_lines(lua_State *L, int idx, int toclose)
{
    // 复制文件句柄到栈顶
    lua_pushvalue(L, idx);

    // 推送关闭标志
    lua_pushboolean(L, toclose);

    // 创建闭包，包含2个上值
    lua_pushcclosure(L, io_readline, 2);
}

/*
** [文件方法] 文件的行迭代器
**
** 功能描述：
** 文件对象的 lines 方法实现。返回一个迭代器函数，
** 用于逐行读取文件内容。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 使用示例：
** for line in file:lines() do
**     print(line)
** end
*/
static int f_lines(lua_State *L)
{
    // 确保参数是有效的文件句柄
    tofile(L);

    // 创建迭代器，不自动关闭文件
    aux_lines(L, 1, 0);

    return 1;
}

/*
** [核心函数] 创建文件行迭代器
**
** 功能描述：
** Lua io.lines() 函数的实现。创建用于逐行读取的迭代器。
** 可以指定文件名或使用默认输入文件。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 行为说明：
** - 无参数：迭代默认输入文件
** - 有文件名参数：打开文件并迭代，完成后自动关闭
**
** 使用示例：
** for line in io.lines() do          -- 读取标准输入
**     print(line)
** end
**
** for line in io.lines("data.txt") do -- 读取指定文件
**     print(line)
** end
*/
static int io_lines(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
    {
        // 没有参数，迭代默认输入文件
        lua_rawgeti(L, LUA_ENVIRONINDEX, IO_INPUT);
        return f_lines(L);
    }
    else
    {
        // 有文件名参数，打开文件进行迭代
        const char *filename = luaL_checkstring(L, 1);
        FILE **pf = newfile(L);

        *pf = fopen(filename, "r");

        if (*pf == NULL)
        {
            fileerror(L, 1, filename);
        }

        // 创建迭代器，完成后自动关闭文件
        aux_lines(L, lua_gettop(L), 1);

        return 1;
    }
}

/*
** ======================================================
** [读取模块] 文件读取功能实现
** ======================================================
*/

/*
** [读取函数] 读取数字
**
** 功能描述：
** 从文件中读取一个数字（整数或浮点数）。
** 使用 fscanf 和 LUA_NUMBER_SCAN 格式进行解析。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param f - FILE*：要读取的文件指针
**
** 返回值：
** @return int：成功返回1，失败返回0
**
** 栈操作：
** 成功时：推送读取到的数字
** 失败时：推送 nil
*/
static int read_number(lua_State *L, FILE *f)
{
    lua_Number d;

    if (fscanf(f, LUA_NUMBER_SCAN, &d) == 1)
    {
        // 成功读取数字
        lua_pushnumber(L, d);
        return 1;
    }
    else
    {
        // 读取失败，推送 nil 作为占位符（稍后会被移除）
        lua_pushnil(L);
        return 0;
    }
}

/*
** [读取函数] 测试文件结束
**
** 功能描述：
** 检查文件是否已到达末尾。通过读取一个字符然后放回来实现。
** 如果到达 EOF，返回空字符串；否则返回非零值。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param f - FILE*：要测试的文件指针
**
** 返回值：
** @return int：未到达 EOF 返回非零值，到达 EOF 返回0
**
** 栈操作：
** 总是推送一个空字符串
*/
static int test_eof(lua_State *L, FILE *f)
{
    int c = getc(f);
    ungetc(c, f);

    // 推送空字符串
    lua_pushlstring(L, NULL, 0);

    return (c != EOF);
}

/*
** [读取函数] 读取一行
**
** 功能描述：
** 从文件中读取一行文本，不包括行尾的换行符。
** 使用 luaL_Buffer 进行高效的字符串构建。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param f - FILE*：要读取的文件指针
**
** 返回值：
** @return int：成功读取返回1，EOF且无内容返回0
**
** 栈操作：
** 推送读取到的行内容（字符串）
**
** 算法说明：
** 1. 使用缓冲区分块读取，提高效率
** 2. 检查每个缓冲区是否包含换行符
** 3. 如果包含换行符，截断并返回
** 4. 如果不包含，继续读取下一块
*/
static int read_line(lua_State *L, FILE *f)
{
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    for (;;)
    {
        size_t l;
        char *p = luaL_prepbuffer(&b);

        if (fgets(p, LUAL_BUFFERSIZE, f) == NULL)
        {
            // 到达文件末尾
            luaL_pushresult(&b);

            // 检查是否读取了任何内容
            return (lua_objlen(L, -1) > 0);
        }

        l = strlen(p);

        if (l == 0 || p[l-1] != '\n')
        {
            // 没有换行符，添加整个缓冲区
            luaL_addsize(&b, l);
        }
        else
        {
            // 找到换行符，不包括换行符本身
            luaL_addsize(&b, l - 1);
            luaL_pushresult(&b);
            return 1;
        }
    }
}

/*
** [读取函数] 读取指定数量的字符
**
** 功能描述：
** 从文件中读取指定数量的字符。使用缓冲区分块读取以提高效率。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param f - FILE*：要读取的文件指针
** @param n - size_t：要读取的字符数
**
** 返回值：
** @return int：成功读取返回1，失败返回0
**
** 栈操作：
** 推送读取到的字符串
**
** 算法说明：
** 1. 分块读取，每次最多读取 LUAL_BUFFERSIZE 字符
** 2. 累计读取直到达到要求的字符数或文件结束
** 3. 返回实际读取的内容
*/
static int read_chars(lua_State *L, FILE *f, size_t n)
{
    size_t rlen;  // 每次读取的长度
    size_t nr;    // 实际读取的字符数
    luaL_Buffer b;

    luaL_buffinit(L, &b);
    rlen = LUAL_BUFFERSIZE;  // 尝试每次读取这么多

    do
    {
        char *p = luaL_prepbuffer(&b);

        if (rlen > n)
        {
            rlen = n;  // 不能读取超过要求的数量
        }

        nr = fread(p, sizeof(char), rlen, f);
        luaL_addsize(&b, nr);
        n -= nr;  // 还需要读取的字符数

    } while (n > 0 && nr == rlen);  // 直到读完或遇到 EOF

    luaL_pushresult(&b);

    return (n == 0 || lua_objlen(L, -1) > 0);
}

/*
** [核心函数] 通用文件读取函数
**
** 功能描述：
** 实现灵活的文件读取功能，支持多种读取模式：
** - 数字：读取指定数量的字符
** - "*n"：读取一个数字
** - "*l"：读取一行
** - "*a"：读取整个文件
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param f - FILE*：要读取的文件指针
** @param first - int：第一个参数在栈中的位置
**
** 返回值：
** @return int：读取结果的数量
**
** 栈操作：
** 根据读取模式推送相应的结果
**
** 算法复杂度：O(n) 其中 n 是读取的字符数
*/
static int g_read(lua_State *L, FILE *f, int first)
{
    int nargs = lua_gettop(L) - 1;  // 参数数量
    int success;
    int n;

    // 清除文件错误标志
    clearerr(f);

    if (nargs == 0)
    {
        // 没有参数，默认读取一行
        success = read_line(L, f);
        n = first + 1;  // 返回1个结果
    }
    else
    {
        // 确保栈空间足够存放所有结果和辅助库的缓冲区
        luaL_checkstack(L, nargs + LUA_MINSTACK, "too many arguments");
        success = 1;

        // 处理每个读取参数
        for (n = first; nargs-- && success; n++)
        {
            if (lua_type(L, n) == LUA_TNUMBER)
            {
                // 参数是数字，读取指定数量的字符
                size_t l = (size_t)lua_tointeger(L, n);
                success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
            }
            else
            {
                // 参数是字符串，检查读取模式
                const char *p = lua_tostring(L, n);
                luaL_argcheck(L, p && p[0] == '*', n, "invalid option");

                switch (p[1])
                {
                    case 'n':  // "*n" - 读取数字
                        success = read_number(L, f);
                        break;

                    case 'l':  // "*l" - 读取一行
                        success = read_line(L, f);
                        break;

                    case 'a':  // "*a" - 读取整个文件
                        read_chars(L, f, ~((size_t)0));  // 读取最大可能的字符数
                        success = 1;  // 总是成功
                        break;

                    default:
                        return luaL_argerror(L, n, "invalid format");
                }
            }
        }
    }

    // 检查文件错误
    if (ferror(f))
    {
        return pushresult(L, 0, NULL);
    }

    if (!success)
    {
        // 移除最后一个失败的结果，推送 nil
        lua_pop(L, 1);
        lua_pushnil(L);
    }

    return n - first;
}

/*
** [核心函数] 从标准输入读取
**
** 功能描述：
** Lua io.read() 函数的实现。从当前的标准输入文件读取数据。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：g_read 的返回值
**
** 使用示例：
** line = io.read()        -- 读取一行
** num = io.read("*n")     -- 读取一个数字
** chars = io.read(10)     -- 读取10个字符
*/
static int io_read(lua_State *L)
{
    return g_read(L, getiofile(L, IO_INPUT), 1);
}

/*
** [文件方法] 从文件读取
**
** 功能描述：
** 文件对象的 read 方法实现。从指定文件读取数据。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：g_read 的返回值
**
** 使用示例：
** line = file:read()      -- 读取一行
** num = file:read("*n")   -- 读取一个数字
** chars = file:read(10)   -- 读取10个字符
*/
static int f_read(lua_State *L)
{
    return g_read(L, tofile(L), 2);
}

/*
** [迭代器] 行读取迭代器函数
**
** 功能描述：
** 用于 io.lines() 和 file:lines() 的迭代器实现。
** 每次调用返回文件的下一行，直到文件结束。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：成功返回1（行内容），EOF返回0
**
** 上值说明：
** - 上值1：文件句柄用户数据
** - 上值2：是否在完成后关闭文件的布尔值
**
** 错误处理：
** 如果文件已关闭或读取出错，会触发 Lua 错误
*/
static int io_readline(lua_State *L)
{
    FILE *f = *(FILE **)lua_touserdata(L, lua_upvalueindex(1));
    int sucess;

    if (f == NULL)
    {
        // 文件已关闭
        luaL_error(L, "file is already closed");
    }

    sucess = read_line(L, f);

    if (ferror(f))
    {
        return luaL_error(L, "%s", strerror(errno));
    }

    if (sucess)
    {
        return 1;
    }
    else
    {
        // 到达 EOF
        if (lua_toboolean(L, lua_upvalueindex(2)))
        {
            // 如果迭代器创建了文件，需要关闭它
            lua_settop(L, 0);
            lua_pushvalue(L, lua_upvalueindex(1));
            aux_close(L);
        }

        return 0;
    }
}

/* ====================================================== */

/*
** [核心函数] 通用文件写入函数
**
** 功能描述：
** 实现灵活的文件写入功能，支持写入多个值：
** - 数字：格式化后写入
** - 字符串：直接写入
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param f - FILE*：要写入的文件指针
** @param arg - int：第一个要写入的参数在栈中的位置
**
** 返回值：
** @return int：pushresult 的返回值
**
** 栈操作：
** 推送写入操作的结果
**
** 性能优化：
** 对数字类型进行了特殊优化，直接使用 fprintf 格式化输出
*/
static int g_write(lua_State *L, FILE *f, int arg)
{
    int nargs = lua_gettop(L) - 1;  // 参数数量
    int status = 1;  // 写入状态

    // 遍历所有要写入的参数
    for (; nargs--; arg++)
    {
        if (lua_type(L, arg) == LUA_TNUMBER)
        {
            // 数字类型：使用格式化输出（性能优化）
            status = status &&
                fprintf(f, LUA_NUMBER_FMT, lua_tonumber(L, arg)) > 0;
        }
        else
        {
            // 字符串类型：直接写入
            size_t l;
            const char *s = luaL_checklstring(L, arg, &l);
            status = status && (fwrite(s, sizeof(char), l, f) == l);
        }
    }

    return pushresult(L, status, NULL);
}

/*
** [核心函数] 写入到标准输出
**
** 功能描述：
** Lua io.write() 函数的实现。向当前的标准输出文件写入数据。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：g_write 的返回值
**
** 使用示例：
** io.write("Hello")           -- 写入字符串
** io.write(123)               -- 写入数字
** io.write("Hello", " ", 123) -- 写入多个值
*/
static int io_write(lua_State *L)
{
    return g_write(L, getiofile(L, IO_OUTPUT), 1);
}

/*
** [文件方法] 写入到文件
**
** 功能描述：
** 文件对象的 write 方法实现。向指定文件写入数据。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：g_write 的返回值
**
** 使用示例：
** file:write("Hello")           -- 写入字符串
** file:write(123)               -- 写入数字
** file:write("Hello", " ", 123) -- 写入多个值
*/
static int f_write(lua_State *L)
{
    return g_write(L, tofile(L), 2);
}

/*
** [文件方法] 文件定位操作
**
** 功能描述：
** 文件对象的 seek 方法实现。改变文件的读写位置。
** 支持三种定位模式：set（绝对位置）、cur（相对当前位置）、end（相对文件末尾）。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：成功返回1（新位置），失败返回pushresult的结果
**
** 栈操作：
** 成功时：推送新的文件位置（整数）
** 失败时：推送 nil、错误消息和错误码
**
** 使用示例：
** pos = file:seek()           -- 获取当前位置
** pos = file:seek("set", 0)   -- 定位到文件开头
** pos = file:seek("end")      -- 定位到文件末尾
** pos = file:seek("cur", 10)  -- 向前移动10字节
*/
static int f_seek(lua_State *L)
{
    // 定位模式常量数组
    static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};

    // 定位模式名称数组
    static const char *const modenames[] = {"set", "cur", "end", NULL};

    FILE *f = tofile(L);

    // 获取定位模式，默认为 "cur"
    int op = luaL_checkoption(L, 2, "cur", modenames);

    // 获取偏移量，默认为 0
    long offset = luaL_optlong(L, 3, 0);

    // 执行定位操作
    op = fseek(f, offset, mode[op]);

    if (op)
    {
        // 定位失败
        return pushresult(L, 0, NULL);
    }
    else
    {
        // 定位成功，返回新位置
        lua_pushinteger(L, ftell(f));
        return 1;
    }
}

/*
** [文件方法] 设置文件缓冲模式
**
** 功能描述：
** 文件对象的 setvbuf 方法实现。设置文件的缓冲模式和缓冲区大小。
** 支持三种缓冲模式：no（无缓冲）、full（全缓冲）、line（行缓冲）。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：pushresult 的返回值
**
** 栈操作：
** 推送操作结果（成功或失败信息）
**
** 使用示例：
** file:setvbuf("no")          -- 设置为无缓冲
** file:setvbuf("full", 1024)  -- 设置为全缓冲，缓冲区1024字节
** file:setvbuf("line")        -- 设置为行缓冲
*/
static int f_setvbuf(lua_State *L)
{
    // 缓冲模式常量数组
    static const int mode[] = {_IONBF, _IOFBF, _IOLBF};

    // 缓冲模式名称数组
    static const char *const modenames[] = {"no", "full", "line", NULL};

    FILE *f = tofile(L);

    // 获取缓冲模式
    int op = luaL_checkoption(L, 2, NULL, modenames);

    // 获取缓冲区大小，默认为 LUAL_BUFFERSIZE
    lua_Integer sz = luaL_optinteger(L, 3, LUAL_BUFFERSIZE);

    // 设置缓冲模式
    int res = setvbuf(f, NULL, mode[op], sz);

    return pushresult(L, res == 0, NULL);
}

/*
** [核心函数] 刷新标准输出缓冲区
**
** 功能描述：
** Lua io.flush() 函数的实现。刷新当前标准输出文件的缓冲区。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：pushresult 的返回值
**
** 使用示例：
** io.flush()  -- 刷新标准输出
*/
static int io_flush(lua_State *L)
{
    return pushresult(L, fflush(getiofile(L, IO_OUTPUT)) == 0, NULL);
}

/*
** [文件方法] 刷新文件缓冲区
**
** 功能描述：
** 文件对象的 flush 方法实现。刷新指定文件的缓冲区。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：pushresult 的返回值
**
** 使用示例：
** file:flush()  -- 刷新文件缓冲区
*/
static int f_flush(lua_State *L)
{
    return pushresult(L, fflush(tofile(L)) == 0, NULL);
}

/*
** [数据结构] I/O 库函数注册表
**
** 数据结构说明：
** 包含所有 io 模块的全局函数注册信息。
** 每个元素都是 luaL_Reg 结构体，包含函数名和对应的 C 函数指针。
**
** 函数列表：
** - close：关闭文件句柄
** - flush：刷新输出缓冲区
** - input：设置或获取标准输入文件
** - lines：创建行迭代器
** - open：打开文件
** - output：设置或获取标准输出文件
** - popen：创建管道文件
** - read：从标准输入读取
** - tmpfile：创建临时文件
** - type：检查文件类型
** - write：写入到标准输出
*/
static const luaL_Reg iolib[] =
{
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

/*
** [数据结构] 文件对象方法注册表
**
** 数据结构说明：
** 包含文件句柄对象的所有方法注册信息。
** 这些方法会被设置到文件句柄的元表中。
**
** 方法列表：
** - close：关闭文件
** - flush：刷新文件缓冲区
** - lines：创建行迭代器
** - read：从文件读取
** - seek：文件定位
** - setvbuf：设置缓冲模式
** - write：写入到文件
** - __gc：垃圾回收时的清理函数
** - __tostring：字符串表示方法
*/
static const luaL_Reg flib[] =
{
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

/*
** [初始化函数] 创建文件句柄元表
**
** 功能描述：
** 创建并设置文件句柄的元表，注册所有文件方法。
** 设置 __index 元方法指向元表自身，实现方法查找。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 栈操作：
** 在注册表中创建 LUA_FILEHANDLE 元表并设置相关方法
*/
static void createmeta(lua_State *L)
{
    // 创建文件句柄元表
    luaL_newmetatable(L, LUA_FILEHANDLE);

    // 复制元表到栈顶
    lua_pushvalue(L, -1);

    // 设置 __index 元方法指向元表自身
    lua_setfield(L, -2, "__index");

    // 注册文件方法到元表
    luaL_register(L, NULL, flib);
}

/*
** [初始化函数] 创建标准文件句柄
**
** 功能描述：
** 为标准文件（stdin、stdout、stderr）创建 Lua 文件句柄对象。
** 设置适当的环境和名称。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param f - FILE*：标准文件指针
** @param k - int：环境索引（IO_INPUT、IO_OUTPUT 或 0）
** @param fname - const char*：文件名称
**
** 栈操作：
** 创建文件句柄并设置到相应的位置
*/
static void createstdfile(lua_State *L, FILE *f, int k, const char *fname)
{
    // 创建文件句柄并设置文件指针
    *newfile(L) = f;

    if (k > 0)
    {
        // 如果有环境索引，设置到环境中
        lua_pushvalue(L, -1);
        lua_rawseti(L, LUA_ENVIRONINDEX, k);
    }

    // 复制环境表
    lua_pushvalue(L, -2);

    // 设置文件句柄的环境
    lua_setfenv(L, -2);

    // 设置到全局表中
    lua_setfield(L, -3, fname);
}

/*
** [初始化函数] 创建新的函数环境
**
** 功能描述：
** 为特定类型的文件创建专用的函数环境。
** 设置相应的 __close 函数。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param cls - lua_CFunction：关闭函数指针
**
** 栈操作：
** 在栈顶创建一个新的环境表，包含 __close 字段
*/
static void newfenv(lua_State *L, lua_CFunction cls)
{
    // 创建新的环境表
    lua_createtable(L, 0, 1);

    // 设置关闭函数
    lua_pushcfunction(L, cls);
    lua_setfield(L, -2, "__close");
}

/*
** [核心] Lua I/O 库初始化函数
**
** 功能描述：
** 初始化整个 Lua I/O 库，设置所有必要的元表、环境和标准文件。
** 这是 I/O 库的入口点，由 Lua 解释器在加载库时调用。
**
** 详细初始化流程：
** 1. 创建文件句柄元表并注册文件方法
** 2. 创建私有环境，包含 IO_INPUT、IO_OUTPUT 和 __close 字段
** 3. 注册 I/O 库的全局函数
** 4. 创建并设置标准文件句柄（stdin、stdout、stderr）
** 5. 为 popen 函数设置专用环境
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1，表示库表）
**
** 栈操作：
** 在栈顶留下 I/O 库表
**
** 设计说明：
** - 使用环境表来管理不同类型文件的关闭函数
** - 标准文件使用 io_noclose 防止意外关闭
** - popen 文件使用 io_pclose 进行正确的管道关闭
** - 普通文件使用 io_fclose 进行标准关闭
**
** 算法复杂度：O(1) 时间和空间复杂度
*/
LUALIB_API int luaopen_io(lua_State *L)
{
    // 步骤1：创建文件句柄元表
    createmeta(L);

    // 步骤2：创建私有环境（包含 IO_INPUT、IO_OUTPUT、__close 字段）
    newfenv(L, io_fclose);
    lua_replace(L, LUA_ENVIRONINDEX);

    // 步骤3：注册 I/O 库函数
    luaL_register(L, LUA_IOLIBNAME, iolib);

    // 步骤4：创建并设置默认文件句柄

    // 为默认文件创建环境（使用 io_noclose 关闭函数）
    newfenv(L, io_noclose);

    // 创建标准文件句柄
    createstdfile(L, stdin, IO_INPUT, "stdin");    // 标准输入
    createstdfile(L, stdout, IO_OUTPUT, "stdout"); // 标准输出
    createstdfile(L, stderr, 0, "stderr");         // 标准错误（不设置环境索引）

    // 弹出默认文件的环境
    lua_pop(L, 1);

    // 步骤5：为 popen 设置专用环境

    // 获取 popen 函数
    lua_getfield(L, -1, "popen");

    // 为 popen 创建专用环境（使用 io_pclose 关闭函数）
    newfenv(L, io_pclose);

    // 设置 popen 函数的环境
    lua_setfenv(L, -2);

    // 弹出 popen 函数
    lua_pop(L, 1);

    // 返回 I/O 库表
    return 1;
}
