/**
 * @file loadlib.c
 * @brief Lua动态库加载模块：跨平台动态链接和模块系统实现
 *
 * 版权信息：
 * $Id: loadlib.c,v 1.52.1.4 2009/09/09 13:17:16 roberto Exp $
 * Lua动态库加载器
 * 版权声明见lua.h文件
 *
 * 模块概述：
 * 本模块实现了Lua的动态库加载系统，是Lua模块系统的核心组件。
 * 它提供了跨平台的动态链接库加载、符号解析、模块缓存和require
 * 函数的完整实现。这是Lua扩展性和模块化的基础设施。
 *
 * 主要功能：
 * 1. 跨平台动态库加载：支持Unix(dlfcn)、Windows(DLL)、macOS(dyld)
 * 2. 符号查找和解析：从动态库中获取函数指针
 * 3. 模块路径搜索：实现复杂的模块查找算法
 * 4. require函数实现：Lua模块加载的标准接口
 * 5. package系统管理：维护模块缓存和加载状态
 * 6. 错误处理机制：统一的错误报告和异常处理
 *
 * 平台支持：
 * - Unix系统：使用dlfcn接口（dlopen、dlsym、dlclose）
 * - Windows系统：使用Win32 API（LoadLibrary、GetProcAddress、FreeLibrary）
 * - macOS/Darwin：使用dyld接口（NSAddImage、NSLookupSymbolInImage）
 * - 其他系统：提供存根实现
 *
 * 核心设计理念：
 * 1. 平台抽象：统一的接口隐藏平台差异
 * 2. 延迟加载：按需加载模块，提高启动性能
 * 3. 缓存机制：避免重复加载，提高运行效率
 * 4. 错误恢复：优雅处理加载失败，提供详细错误信息
 * 5. 安全性：防止符号冲突和内存泄漏
 *
 * 模块系统架构：
 * - 底层：平台相关的动态库加载接口
 * - 中层：统一的加载和符号解析抽象
 * - 上层：require函数和package表管理
 * - 应用层：用户模块的加载和使用
 *
 * 技术特点：
 * 1. 条件编译：根据平台选择合适的实现
 * 2. 函数指针：动态获取和调用库函数
 * 3. 字符串处理：复杂的路径解析和模块名转换
 * 4. 内存管理：动态分配和自动释放
 * 5. 异常安全：确保资源正确释放
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2008-2009
 *
 * @note 本模块是Lua扩展性的核心，支持C和Lua模块的动态加载
 * @see lualib.h, lauxlib.h, package库
 */

#include <stdlib.h>
#include <string.h>

#define loadlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/**
 * @defgroup ModuleConstants 模块系统常量定义
 * @brief 动态库加载和模块系统使用的常量定义
 *
 * 这些常量定义了模块系统的核心参数，包括函数命名规范、
 * 错误代码、调试前缀等重要配置信息。
 * @{
 */

/**
 * @brief C库中开放函数的前缀
 *
 * 定义了C扩展库中导出函数的标准命名前缀。所有C模块的
 * 入口函数都应该以此前缀开头。
 *
 * @note 标准格式：luaopen_模块名
 * @example luaopen_socket, luaopen_math
 */
#define LUA_POF		"luaopen_"

/**
 * @brief C库中开放函数的分隔符
 *
 * 用于分隔模块名中的层次结构，将点号转换为下划线。
 *
 * @note 模块名转换：socket.core -> socket_core
 * @example require("socket.core") -> luaopen_socket_core
 */
#define LUA_OFSEP	"_"

/**
 * @brief 库加载错误消息前缀
 *
 * 用于标识来自动态库加载系统的错误消息，便于调试和
 * 错误追踪。
 */
#define LIBPREFIX	"LOADLIB: "

/**
 * @brief 开放函数前缀的简化定义
 *
 * LUA_POF的简化版本，用于内部代码的简洁性。
 */
#define POF		LUA_POF

/**
 * @brief 库加载失败的错误类型标识
 *
 * 用于标识动态库打开失败的错误类型。
 */
#define LIB_FAIL	"open"

/**
 * @brief 动态库加载错误代码
 *
 * 表示动态库文件加载失败，通常是文件不存在或
 * 格式不正确。
 */
#define ERRLIB		1

/**
 * @brief 函数符号查找错误代码
 *
 * 表示在动态库中找不到指定的函数符号，通常是
 * 函数名不正确或函数未导出。
 */
#define ERRFUNC		2

/**
 * @brief 程序目录设置函数的默认实现
 *
 * 在不支持程序目录获取的平台上，提供空实现。
 * Windows平台会重新定义此宏。
 *
 * @param L Lua状态机指针
 * @note 默认实现为空操作，Windows平台有特殊实现
 */
#define setprogdir(L)		((void)0)

/** @} */ /* 结束模块系统常量定义文档组 */

/**
 * @defgroup PlatformAbstraction 平台抽象接口
 * @brief 跨平台动态库操作的统一接口声明
 *
 * 这些函数提供了统一的动态库操作接口，隐藏了不同平台
 * 的实现差异。每个平台都会提供这些函数的具体实现。
 * @{
 */

/**
 * @brief 卸载动态库
 *
 * 释放已加载的动态库，清理相关资源。这是平台相关的
 * 操作，每个平台都有不同的实现。
 *
 * @param lib 动态库句柄
 *
 * @note 平台实现：Unix使用dlclose，Windows使用FreeLibrary
 * @see ll_load, ll_sym
 */
static void ll_unloadlib (void *lib);

/**
 * @brief 加载动态库
 *
 * 从指定路径加载动态库文件。如果加载失败，会将错误
 * 信息推送到Lua栈上。
 *
 * @param L Lua状态机指针
 * @param path 动态库文件路径
 * @return 成功时返回库句柄，失败时返回NULL
 *
 * @note 失败时错误信息在Lua栈顶
 * @note 平台实现：Unix使用dlopen，Windows使用LoadLibrary
 * @see ll_unloadlib, ll_sym
 */
static void *ll_load (lua_State *L, const char *path);

/**
 * @brief 从动态库中获取函数符号
 *
 * 在已加载的动态库中查找指定名称的函数，并返回函数指针。
 * 如果查找失败，会将错误信息推送到Lua栈上。
 *
 * @param L Lua状态机指针
 * @param lib 动态库句柄
 * @param sym 要查找的符号名称
 * @return 成功时返回函数指针，失败时返回NULL
 *
 * @note 失败时错误信息在Lua栈顶
 * @note 平台实现：Unix使用dlsym，Windows使用GetProcAddress
 * @see ll_load, ll_unloadlib
 */
static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym);

/** @} */ /* 结束平台抽象接口文档组 */



/**
 * @defgroup UnixDlfcnImplementation Unix dlfcn接口实现
 * @brief 基于POSIX dlfcn接口的动态库加载实现
 *
 * 这是针对Unix系统的动态库加载实现，使用标准的dlfcn接口。
 * dlfcn接口在大多数Unix系统上都可用，包括Linux、SunOS、Solaris、
 * IRIX、FreeBSD、NetBSD、AIX 4.2、HPUX 11等，至少作为本地函数
 * 之上的仿真层存在。
 *
 * 技术特点：
 * - 使用POSIX标准的动态链接接口
 * - 支持延迟符号解析（RTLD_LAZY）和立即解析（RTLD_NOW）
 * - 提供详细的错误信息通过dlerror()
 * - 线程安全的实现
 * - 支持符号可见性控制
 *
 * 接口优势：
 * - 标准化：POSIX标准，跨Unix平台兼容
 * - 功能完整：支持所有必要的动态链接操作
 * - 错误处理：提供详细的错误描述
 * - 性能优化：支持不同的加载策略
 *
 * 使用场景：
 * - Linux发行版的标准实现
 * - 服务器环境的模块加载
 * - 嵌入式Linux系统
 * - 跨平台应用的Unix版本
 * @{
 */

#if defined(LUA_DL_DLOPEN)

#include <dlfcn.h>

/**
 * @brief Unix平台动态库卸载实现
 *
 * 使用dlclose()函数释放已加载的动态库。这会减少库的引用计数，
 * 当引用计数为零时，系统会卸载库并清理相关资源。
 *
 * @param lib 动态库句柄，由dlopen()返回
 *
 * @note dlclose()是线程安全的
 * @note 多次加载同一库会增加引用计数
 * @note 只有当引用计数为零时才真正卸载
 *
 * @see dlopen, dlsym, dlerror
 *
 * 实现细节：
 * - 自动处理引用计数
 * - 调用库的析构函数（如果有）
 * - 清理符号表和内存映射
 * - 处理依赖库的卸载
 *
 * 错误处理：
 * - dlclose()很少失败
 * - 失败通常表示严重的系统问题
 * - 错误信息可通过dlerror()获取
 */
static void ll_unloadlib (void *lib) {
    dlclose(lib);
}

/**
 * @brief Unix平台动态库加载实现
 *
 * 使用dlopen()函数加载指定路径的动态库。采用RTLD_NOW模式，
 * 在加载时立即解析所有符号，确保库的完整性。
 *
 * @param L Lua状态机指针，用于错误报告
 * @param path 动态库文件的完整路径
 * @return 成功时返回库句柄，失败时返回NULL并在栈上推送错误信息
 *
 * @note 使用RTLD_NOW确保加载时发现所有符号问题
 * @note 失败时错误信息由dlerror()提供
 *
 * @see dlclose, dlsym, dlerror
 *
 * 加载模式说明：
 * - RTLD_NOW：立即解析所有符号
 * - 优点：早期发现符号问题
 * - 缺点：加载时间稍长
 * - 适合：生产环境和调试
 *
 * 路径处理：
 * - 支持绝对路径和相对路径
 * - 遵循系统的库搜索规则
 * - 支持LD_LIBRARY_PATH环境变量
 * - 处理符号链接和别名
 *
 * 错误情况：
 * - 文件不存在或无法访问
 * - 文件格式不正确（非ELF等）
 * - 架构不匹配（32位/64位）
 * - 依赖库缺失
 * - 符号解析失败
 * - 权限不足
 */
static void *ll_load (lua_State *L, const char *path) {
    void *lib = dlopen(path, RTLD_NOW);
    if (lib == NULL) lua_pushstring(L, dlerror());
    return lib;
}

/**
 * @brief Unix平台符号查找实现
 *
 * 使用dlsym()函数在已加载的动态库中查找指定的符号，
 * 并将其转换为Lua C函数指针。
 *
 * @param L Lua状态机指针，用于错误报告
 * @param lib 动态库句柄，由ll_load()返回
 * @param sym 要查找的符号名称（函数名）
 * @return 成功时返回函数指针，失败时返回NULL并在栈上推送错误信息
 *
 * @note 符号名必须完全匹配，区分大小写
 * @note 只能获取导出的符号
 *
 * @see dlopen, dlclose, dlerror
 *
 * 符号查找规则：
 * - 精确匹配符号名称
 * - 区分大小写
 * - 只查找导出的符号
 * - 支持C++名称修饰
 *
 * 类型转换：
 * - 将void*转换为lua_CFunction
 * - 假设函数签名兼容
 * - 依赖编译器的函数指针实现
 *
 * 错误处理：
 * - 符号不存在
 * - 符号不是函数
 * - 符号未导出
 * - 库句柄无效
 *
 * 性能考虑：
 * - dlsym()相对较快
 * - 符号表查找是O(log n)
 * - 结果可以缓存
 * - 避免重复查找
 */
static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
    lua_CFunction f = (lua_CFunction)dlsym(lib, sym);
    if (f == NULL) lua_pushstring(L, dlerror());
    return f;
}

/** @} */ /* 结束Unix dlfcn接口实现文档组 */



/**
 * @defgroup WindowsDllImplementation Windows DLL接口实现
 * @brief 基于Windows原生API的动态库加载实现
 *
 * 这是针对Windows系统的动态库加载实现，使用Windows原生的
 * DLL加载API。提供了与Unix dlfcn接口相同的功能，但使用
 * Windows特有的LoadLibrary、GetProcAddress和FreeLibrary函数。
 *
 * 技术特点：
 * - 使用Windows原生API，性能最优
 * - 支持完整的Windows DLL加载机制
 * - 提供详细的Windows错误信息
 * - 支持程序目录的自动检测
 * - 兼容Windows的模块搜索规则
 *
 * Windows特有功能：
 * - 程序目录检测：自动获取可执行文件目录
 * - 系统错误格式化：将Windows错误码转换为可读消息
 * - DLL搜索路径：遵循Windows DLL搜索顺序
 * - 安全加载：支持安全DLL加载选项
 *
 * 使用场景：
 * - Windows桌面应用程序
 * - Windows服务程序
 * - 嵌入式Windows系统
 * - 跨平台应用的Windows版本
 * @{
 */

#elif defined(LUA_DL_DLL)

#include <windows.h>

#undef setprogdir

/**
 * @brief Windows平台程序目录设置
 *
 * 获取当前可执行文件的目录路径，并将其设置为程序目录。
 * 这用于模块搜索时的路径解析。
 *
 * @param L Lua状态机指针
 *
 * @note 重新定义了默认的空实现
 * @note 使用Windows API获取模块文件名
 *
 * @see GetModuleFileNameA, luaL_gsub, luaL_error
 *
 * 实现步骤：
 * 1. 获取当前模块的完整路径
 * 2. 查找最后一个反斜杠位置
 * 3. 截取目录部分
 * 4. 替换Lua执行目录占位符
 * 5. 清理栈上的临时字符串
 *
 * 错误处理：
 * - GetModuleFileNameA失败
 * - 缓冲区大小不足
 * - 路径中没有反斜杠
 * - 内存分配失败
 *
 * 安全考虑：
 * - 使用固定大小缓冲区
 * - 检查API返回值
 * - 处理边界条件
 * - 防止缓冲区溢出
 */
static void setprogdir (lua_State *L) {
    char buff[MAX_PATH + 1];
    char *lb;
    DWORD nsize = sizeof(buff)/sizeof(char);
    DWORD n = GetModuleFileNameA(NULL, buff, nsize);
    if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == NULL)
        luaL_error(L, "unable to get ModuleFileName");
    else {
        *lb = '\0';
        luaL_gsub(L, lua_tostring(L, -1), LUA_EXECDIR, buff);
        lua_remove(L, -2);  /* 移除原始字符串 */
    }
}

/**
 * @brief Windows错误信息推送
 *
 * 获取最后的Windows系统错误，并将格式化的错误消息
 * 推送到Lua栈上。提供用户友好的错误描述。
 *
 * @param L Lua状态机指针
 *
 * @note 错误消息推送到栈顶
 * @note 优先使用系统格式化的消息
 *
 * @see GetLastError, FormatMessageA, lua_pushstring
 *
 * 错误处理策略：
 * 1. 获取最后的系统错误码
 * 2. 尝试格式化为可读消息
 * 3. 成功则推送格式化消息
 * 4. 失败则推送错误码
 *
 * FormatMessage标志：
 * - FORMAT_MESSAGE_FROM_SYSTEM：从系统获取消息
 * - FORMAT_MESSAGE_IGNORE_INSERTS：忽略插入参数
 * - 语言ID为0：使用默认语言
 *
 * 缓冲区管理：
 * - 固定128字节缓冲区
 * - 足够容纳大多数错误消息
 * - 避免动态内存分配
 * - 简化错误处理逻辑
 */
static void pusherror (lua_State *L) {
    int error = GetLastError();
    char buffer[128];
    if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, error, 0, buffer, sizeof(buffer), NULL))
        lua_pushstring(L, buffer);
    else
        lua_pushfstring(L, "system error %d\n", error);
}

/**
 * @brief Windows平台动态库卸载实现
 *
 * 使用FreeLibrary()函数释放已加载的DLL。这会减少DLL的
 * 引用计数，当引用计数为零时，系统会卸载DLL。
 *
 * @param lib DLL句柄，由LoadLibrary返回
 *
 * @note 自动处理引用计数
 * @note 调用DLL的DllMain(DLL_PROCESS_DETACH)
 *
 * @see LoadLibraryA, GetProcAddress
 *
 * 卸载过程：
 * 1. 减少DLL引用计数
 * 2. 如果计数为零，开始卸载
 * 3. 调用DllMain清理函数
 * 4. 释放DLL占用的内存
 * 5. 更新进程的模块列表
 *
 * 注意事项：
 * - 多次LoadLibrary需要多次FreeLibrary
 * - DLL可能被其他模块引用
 * - 卸载顺序影响依赖关系
 * - 静态变量的析构顺序
 */
static void ll_unloadlib (void *lib) {
    FreeLibrary((HINSTANCE)lib);
}

/**
 * @brief Windows平台动态库加载实现
 *
 * 使用LoadLibraryA()函数加载指定路径的DLL文件。
 * 如果加载失败，会调用pusherror()推送错误信息。
 *
 * @param L Lua状态机指针，用于错误报告
 * @param path DLL文件的完整路径
 * @return 成功时返回DLL句柄，失败时返回NULL并推送错误信息
 *
 * @note 使用ANSI版本的LoadLibrary
 * @note 失败时错误信息在栈顶
 *
 * @see FreeLibrary, GetProcAddress, pusherror
 *
 * 加载过程：
 * 1. 检查DLL文件是否存在
 * 2. 验证DLL格式和架构
 * 3. 加载DLL到进程地址空间
 * 4. 解析导入表和重定位
 * 5. 调用DllMain(DLL_PROCESS_ATTACH)
 * 6. 返回DLL句柄
 *
 * 搜索顺序：
 * 1. 应用程序目录
 * 2. 系统目录
 * 3. Windows目录
 * 4. 当前目录
 * 5. PATH环境变量目录
 *
 * 错误情况：
 * - 文件不存在
 * - 文件不是有效的DLL
 * - 架构不匹配
 * - 依赖DLL缺失
 * - 内存不足
 * - 权限不足
 */
static void *ll_load (lua_State *L, const char *path) {
    HINSTANCE lib = LoadLibraryA(path);
    if (lib == NULL) pusherror(L);
    return lib;
}

/**
 * @brief Windows平台符号查找实现
 *
 * 使用GetProcAddress()函数在已加载的DLL中查找指定的
 * 导出函数，并将其转换为Lua C函数指针。
 *
 * @param L Lua状态机指针，用于错误报告
 * @param lib DLL句柄，由ll_load()返回
 * @param sym 要查找的函数名称
 * @return 成功时返回函数指针，失败时返回NULL并推送错误信息
 *
 * @note 函数名区分大小写
 * @note 只能获取导出的函数
 *
 * @see LoadLibraryA, FreeLibrary, pusherror
 *
 * 查找过程：
 * 1. 在DLL的导出表中查找函数名
 * 2. 检查函数是否被导出
 * 3. 获取函数的内存地址
 * 4. 转换为函数指针类型
 * 5. 返回函数指针
 *
 * 导出方式：
 * - .def文件导出
 * - __declspec(dllexport)声明
 * - 模块定义文件
 * - 链接器命令行选项
 *
 * 名称修饰：
 * - C函数：通常无修饰或前缀下划线
 * - C++函数：复杂的名称修饰
 * - stdcall：后缀@参数字节数
 * - fastcall：前缀@，后缀@参数字节数
 *
 * 错误处理：
 * - 函数名不存在
 * - 函数未导出
 * - DLL句柄无效
 * - 内存访问错误
 */
static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
    lua_CFunction f = (lua_CFunction)GetProcAddress((HINSTANCE)lib, sym);
    if (f == NULL) pusherror(L);
    return f;
}

/** @} */ /* 结束Windows DLL接口实现文档组 */



/**
 * @defgroup DarwinDyldImplementation macOS/Darwin dyld接口实现
 * @brief 基于macOS原生dyld接口的动态库加载实现
 *
 * 这是针对macOS/Darwin系统的动态库加载实现，使用macOS特有的
 * dyld（动态链接器）接口。dyld是macOS系统的动态链接器，负责
 * 在运行时加载和链接动态库。
 *
 * 技术特点：
 * - 使用Mach-O对象文件格式
 * - 支持bundle和dylib两种动态库格式
 * - 提供详细的加载错误分类
 * - 支持私有模块加载
 * - 处理macOS特有的符号命名规则
 *
 * macOS特有功能：
 * - 符号前缀：C函数名前自动添加下划线
 * - Bundle支持：支持macOS特有的bundle格式
 * - 架构检查：自动检查CPU架构兼容性
 * - 延迟引用：支持延迟符号引用重置
 *
 * 与其他平台的差异：
 * - 使用NSModule而不是void*作为库句柄
 * - 符号查找需要两步：查找符号，获取地址
 * - 错误处理更加细化和具体
 * - 支持更复杂的加载选项
 *
 * 使用场景：
 * - macOS桌面应用程序
 * - iOS应用程序（受限）
 * - macOS服务和守护进程
 * - 跨平台应用的macOS版本
 * @{
 */

#elif defined(LUA_DL_DYLD)

#include <mach-o/dyld.h>

/**
 * @brief macOS平台函数前缀重定义
 *
 * macOS系统在C函数名前自动添加下划线前缀，这是Mach-O
 * 对象文件格式的要求。因此需要重新定义POF常量。
 *
 * @note 这是macOS/Darwin系统的特有要求
 * @note 影响所有C函数的符号查找
 *
 * 符号命名规则：
 * - C函数：前缀下划线（_function_name）
 * - C++函数：复杂的名称修饰
 * - Objective-C：特殊的方法命名
 * - Swift：模块化的符号命名
 */
#undef POF
#define POF	"_" LUA_POF

/**
 * @brief macOS平台错误信息推送
 *
 * 获取dyld的链接编辑错误信息，并将其推送到Lua栈上。
 * 使用NSLinkEditError获取详细的错误描述。
 *
 * @param L Lua状态机指针
 *
 * @note 错误信息由dyld系统提供
 * @note 包含文件名和具体错误描述
 *
 * @see NSLinkEditError, lua_pushstring
 *
 * 错误信息包含：
 * - 错误类型枚举
 * - 错误编号
 * - 相关文件名
 * - 详细错误描述
 *
 * 常见错误类型：
 * - 符号未定义
 * - 库文件损坏
 * - 架构不匹配
 * - 权限问题
 */
static void pusherror (lua_State *L) {
    const char *err_str;
    const char *err_file;
    NSLinkEditErrors err;
    int err_num;
    NSLinkEditError(&err, &err_num, &err_file, &err_str);
    lua_pushstring(L, err_str);
}

/**
 * @brief 将对象文件错误代码转换为错误消息
 *
 * 将NSObjectFileImageReturnCode枚举值转换为用户友好的
 * 错误消息字符串。提供具体的错误原因描述。
 *
 * @param ret NSObjectFileImageReturnCode错误代码
 * @return 对应的错误消息字符串
 *
 * @note 涵盖所有可能的加载错误情况
 * @note 提供具体的问题诊断信息
 *
 * @see NSCreateObjectFileImageFromFile
 *
 * 错误代码映射：
 * - NSObjectFileImageInappropriateFile：文件不是bundle
 * - NSObjectFileImageArch：CPU架构不匹配
 * - NSObjectFileImageFormat：文件格式错误
 * - NSObjectFileImageAccess：文件访问权限问题
 * - NSObjectFileImageFailure：通用加载失败
 *
 * 诊断价值：
 * - 帮助开发者快速定位问题
 * - 区分不同类型的加载错误
 * - 提供针对性的解决建议
 * - 支持自动化错误处理
 */
static const char *errorfromcode (NSObjectFileImageReturnCode ret) {
    switch (ret) {
        case NSObjectFileImageInappropriateFile:
            return "file is not a bundle";
        case NSObjectFileImageArch:
            return "library is for wrong CPU type";
        case NSObjectFileImageFormat:
            return "bad format";
        case NSObjectFileImageAccess:
            return "cannot access file";
        case NSObjectFileImageFailure:
        default:
            return "unable to load library";
    }
}

/**
 * @brief macOS平台动态库卸载实现
 *
 * 使用NSUnLinkModule()函数卸载已加载的模块。设置
 * NSUNLINKMODULE_OPTION_RESET_LAZY_REFERENCES选项来
 * 重置延迟引用。
 *
 * @param lib 模块句柄，由NSLinkModule返回
 *
 * @note 使用NSModule类型而不是通用指针
 * @note 重置延迟引用以避免悬空指针
 *
 * @see NSLinkModule, NSUnLinkModule
 *
 * 卸载选项：
 * - NSUNLINKMODULE_OPTION_RESET_LAZY_REFERENCES：重置延迟引用
 * - 确保所有引用都被正确清理
 * - 防止访问已卸载模块的符号
 *
 * 安全考虑：
 * - 自动处理符号引用清理
 * - 防止悬空指针访问
 * - 确保内存正确释放
 * - 维护系统稳定性
 */
static void ll_unloadlib (void *lib) {
    NSUnLinkModule((NSModule)lib, NSUNLINKMODULE_OPTION_RESET_LAZY_REFERENCES);
}

/**
 * @brief macOS平台动态库加载实现
 *
 * 使用macOS的dyld接口加载动态库。首先创建对象文件映像，
 * 然后链接模块到当前进程。
 *
 * @param L Lua状态机指针，用于错误报告
 * @param path 动态库文件的完整路径
 * @return 成功时返回NSModule句柄，失败时返回NULL并推送错误信息
 *
 * @note 使用私有链接选项避免符号冲突
 * @note 支持错误时返回选项
 *
 * @see NSCreateObjectFileImageFromFile, NSLinkModule, NSDestroyObjectFileImage
 *
 * 加载过程：
 * 1. 检查dyld是否可用
 * 2. 从文件创建对象文件映像
 * 3. 链接模块到进程地址空间
 * 4. 销毁临时对象文件映像
 * 5. 处理错误和返回结果
 *
 * 链接选项：
 * - NSLINKMODULE_OPTION_PRIVATE：私有模块，避免符号冲突
 * - NSLINKMODULE_OPTION_RETURN_ON_ERROR：错误时返回而不是终止
 *
 * 错误处理：
 * - dyld不可用（罕见情况）
 * - 对象文件创建失败
 * - 模块链接失败
 * - 详细的错误分类和报告
 */
static void *ll_load (lua_State *L, const char *path) {
    NSObjectFileImage img;
    NSObjectFileImageReturnCode ret;
    /* 这是罕见情况，但可以防止崩溃 */
    if(!_dyld_present()) {
        lua_pushliteral(L, "dyld not present");
        return NULL;
    }
    ret = NSCreateObjectFileImageFromFile(path, &img);
    if (ret == NSObjectFileImageSuccess) {
        NSModule mod = NSLinkModule(img, path, NSLINKMODULE_OPTION_PRIVATE |
                           NSLINKMODULE_OPTION_RETURN_ON_ERROR);
        NSDestroyObjectFileImage(img);
        if (mod == NULL) pusherror(L);
        return mod;
    }
    lua_pushstring(L, errorfromcode(ret));
    return NULL;
}

/**
 * @brief macOS平台符号查找实现
 *
 * 在已加载的模块中查找指定符号，并获取其地址。
 * macOS需要两步操作：先查找符号，再获取地址。
 *
 * @param L Lua状态机指针，用于错误报告
 * @param lib 模块句柄，由ll_load()返回
 * @param sym 要查找的符号名称
 * @return 成功时返回函数指针，失败时返回NULL并推送错误信息
 *
 * @note macOS的符号查找是两步过程
 * @note 符号名已包含下划线前缀
 *
 * @see NSLookupSymbolInModule, NSAddressOfSymbol, lua_pushfstring
 *
 * 查找过程：
 * 1. 在模块中查找符号
 * 2. 检查符号是否存在
 * 3. 获取符号的内存地址
 * 4. 转换为函数指针类型
 *
 * 符号类型：
 * - 函数符号：可执行代码地址
 * - 数据符号：变量内存地址
 * - 常量符号：只读数据地址
 *
 * 错误处理：
 * - 符号不存在
 * - 符号未导出
 * - 模块句柄无效
 * - 提供具体的符号名信息
 */
static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
    NSSymbol nss = NSLookupSymbolInModule((NSModule)lib, sym);
    if (nss == NULL) {
        lua_pushfstring(L, "symbol " LUA_QS " not found", sym);
        return NULL;
    }
    return (lua_CFunction)NSAddressOfSymbol(nss);
}

/** @} */ /* 结束macOS/Darwin dyld接口实现文档组 */



/**
 * @defgroup FallbackImplementation 其他系统的存根实现
 * @brief 不支持动态库加载的系统的存根实现
 *
 * 这是针对不支持动态库加载的系统的存根实现。在这些系统上，
 * 动态库功能被禁用，所有加载操作都会返回错误信息。
 *
 * 适用系统：
 * - 嵌入式系统
 * - 静态链接的环境
 * - 不支持动态链接的平台
 * - 安全受限的环境
 *
 * 设计目的：
 * - 提供统一的接口
 * - 避免编译错误
 * - 给出明确的错误信息
 * - 保持代码的可移植性
 *
 * 使用场景：
 * - 嵌入式Lua应用
 * - 静态编译的程序
 * - 安全敏感的环境
 * - 简化的Lua发行版
 * @{
 */

#else

#undef LIB_FAIL
#define LIB_FAIL	"absent"

/**
 * @brief 动态库功能禁用消息
 *
 * 当动态库功能未启用时显示的错误消息，提示用户检查
 * Lua安装配置。
 */
#define DLMSG	"dynamic libraries not enabled; check your Lua installation"

/**
 * @brief 存根动态库卸载实现
 *
 * 在不支持动态库的系统上，这是一个空操作。
 * 使用(void)转换避免编译器警告。
 *
 * @param lib 库句柄（未使用）
 *
 * @note 这是一个空操作，不执行任何实际功能
 * @note 用于保持接口一致性
 */
static void ll_unloadlib (void *lib) {
    (void)lib;  /* 避免警告 */
}

/**
 * @brief 存根动态库加载实现
 *
 * 在不支持动态库的系统上，总是返回失败并推送
 * 错误消息到Lua栈。
 *
 * @param L Lua状态机指针
 * @param path 库路径（未使用）
 * @return 总是返回NULL
 *
 * @note 总是失败，推送标准错误消息
 * @note 用于保持接口一致性
 */
static void *ll_load (lua_State *L, const char *path) {
    (void)path;  /* 避免警告 */
    lua_pushliteral(L, DLMSG);
    return NULL;
}

/**
 * @brief 存根符号查找实现
 *
 * 在不支持动态库的系统上，总是返回失败并推送
 * 错误消息到Lua栈。
 *
 * @param L Lua状态机指针
 * @param lib 库句柄（未使用）
 * @param sym 符号名（未使用）
 * @return 总是返回NULL
 *
 * @note 总是失败，推送标准错误消息
 * @note 用于保持接口一致性
 */
static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
    (void)lib; (void)sym;  /* 避免警告 */
    lua_pushliteral(L, DLMSG);
    return NULL;
}

/** @} */ /* 结束其他系统的存根实现文档组 */
#endif



/**
 * @defgroup CoreLoadingFunctions 核心加载函数
 * @brief 动态库加载的核心实现函数
 *
 * 这些函数实现了动态库加载的核心逻辑，包括库注册、
 * 垃圾回收、函数加载等关键功能。它们构成了Lua模块
 * 系统的基础架构。
 *
 * 核心功能：
 * - 库注册和缓存管理
 * - 自动垃圾回收
 * - 函数加载和符号解析
 * - 错误处理和状态管理
 *
 * 设计特点：
 * - 使用Lua注册表进行缓存
 * - 自动内存管理
 * - 统一的错误处理
 * - 线程安全的实现
 * @{
 */

/**
 * @brief 注册动态库到Lua注册表
 *
 * 在Lua注册表中注册动态库，实现库的缓存和重用。
 * 如果库已经注册，返回现有的句柄；否则创建新的注册项。
 *
 * @param L Lua状态机指针
 * @param path 动态库的路径，用作注册键
 * @return 指向库句柄的指针
 *
 * @note 使用LIBPREFIX前缀避免键名冲突
 * @note 创建的userdata具有_LOADLIB元表
 *
 * @see lua_pushfstring, lua_gettable, lua_newuserdata, luaL_getmetatable
 *
 * 注册过程：
 * 1. 构造注册表键名（LIBPREFIX + path）
 * 2. 在注册表中查找现有条目
 * 3. 如果存在，返回现有的库句柄指针
 * 4. 如果不存在，创建新的userdata
 * 5. 设置_LOADLIB元表用于垃圾回收
 * 6. 将新条目存储到注册表
 * 7. 返回库句柄指针
 *
 * 缓存机制：
 * - 避免重复加载同一库
 * - 提高加载性能
 * - 确保库的单例性
 * - 支持引用计数管理
 *
 * 内存管理：
 * - 使用userdata存储库句柄
 * - 自动垃圾回收支持
 * - 元表控制生命周期
 * - 防止内存泄漏
 *
 * 线程安全：
 * - 依赖Lua状态机的线程模型
 * - 注册表操作是原子的
 * - 避免竞态条件
 * - 支持多线程环境
 */
static void **ll_register (lua_State *L, const char *path) {
    void **plib;
    lua_pushfstring(L, "%s%s", LIBPREFIX, path);
    lua_gettable(L, LUA_REGISTRYINDEX);  /* 检查注册表中的库？ */
    if (!lua_isnil(L, -1))  /* 是否有条目？ */
        plib = (void **)lua_touserdata(L, -1);
    else {  /* 还没有条目；创建一个 */
        lua_pop(L, 1);
        plib = (void **)lua_newuserdata(L, sizeof(const void *));
        *plib = NULL;
        luaL_getmetatable(L, "_LOADLIB");
        lua_setmetatable(L, -2);
        lua_pushfstring(L, "%s%s", LIBPREFIX, path);
        lua_pushvalue(L, -2);
        lua_settable(L, LUA_REGISTRYINDEX);
    }
    return plib;
}

/**
 * @brief 动态库垃圾回收元方法
 *
 * 当动态库的userdata被垃圾回收时调用，负责卸载动态库
 * 并清理相关资源。这是Lua的__gc元方法实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回0（无返回值）
 *
 * @note 这是__gc元方法的实现
 * @note 确保库句柄被正确清理
 *
 * @see luaL_checkudata, ll_unloadlib
 *
 * 垃圾回收过程：
 * 1. 检查userdata类型是否为_LOADLIB
 * 2. 获取库句柄指针
 * 3. 如果库句柄有效，调用ll_unloadlib卸载
 * 4. 将库句柄设置为NULL，标记为已关闭
 * 5. 返回0表示无返回值
 *
 * 安全特性：
 * - 类型检查确保操作安全
 * - 空指针检查避免重复卸载
 * - 标记已关闭状态
 * - 防止悬空指针访问
 *
 * 自动化管理：
 * - 无需手动调用卸载函数
 * - 与Lua的GC系统集成
 * - 确保资源及时释放
 * - 简化内存管理
 *
 * 错误处理：
 * - luaL_checkudata提供类型安全
 * - 空指针检查防止崩溃
 * - 静默处理已关闭的库
 * - 保证操作的幂等性
 */
static int gctm (lua_State *L) {
    void **lib = (void **)luaL_checkudata(L, 1, "_LOADLIB");
    if (*lib) ll_unloadlib(*lib);
    *lib = NULL;  /* 标记库为已关闭 */
    return 0;
}

/**
 * @brief 加载动态库函数的核心实现
 *
 * 这是动态库函数加载的核心函数，负责库的注册、加载和
 * 符号解析。它整合了整个加载流程。
 *
 * @param L Lua状态机指针
 * @param path 动态库文件路径
 * @param sym 要加载的函数符号名
 * @return 成功返回0，失败返回错误代码（ERRLIB或ERRFUNC）
 *
 * @note 成功时函数被推送到栈顶
 * @note 失败时错误信息在栈顶
 *
 * @see ll_register, ll_load, ll_sym, lua_pushcfunction
 *
 * 加载流程：
 * 1. 注册库到Lua注册表（获取缓存）
 * 2. 如果库未加载，调用ll_load加载
 * 3. 如果加载失败，返回ERRLIB错误
 * 4. 在已加载的库中查找符号
 * 5. 如果符号查找失败，返回ERRFUNC错误
 * 6. 将函数推送到Lua栈
 * 7. 返回成功状态
 *
 * 缓存优化：
 * - 重用已加载的库
 * - 避免重复加载开销
 * - 提高性能和效率
 * - 确保库的一致性
 *
 * 错误分类：
 * - ERRLIB：库加载失败
 * - ERRFUNC：函数查找失败
 * - 0：成功加载
 * - 详细错误信息在栈上
 *
 * 状态管理：
 * - 库句柄存储在注册表
 * - 自动垃圾回收管理
 * - 线程安全的操作
 * - 异常安全的实现
 */
static int ll_loadfunc (lua_State *L, const char *path, const char *sym) {
    void **reg = ll_register(L, path);
    if (*reg == NULL) *reg = ll_load(L, path);
    if (*reg == NULL)
        return ERRLIB;  /* 无法加载库 */
    else {
        lua_CFunction f = ll_sym(L, *reg, sym);
        if (f == NULL)
            return ERRFUNC;  /* 无法找到函数 */
        lua_pushcfunction(L, f);
        return 0;  /* 返回函数 */
    }
}

/** @} */ /* 结束核心加载函数文档组 */


/**
 * @brief 动态库加载的Lua接口函数
 *
 * 这是package.loadlib函数的C实现，提供了从Lua脚本中
 * 加载动态库和获取函数的接口。
 *
 * @param L Lua状态机指针
 * @return 成功时返回1（函数），失败时返回3（nil, 错误信息, 错误类型）
 *
 * @note 参数1：动态库文件路径
 * @note 参数2：要加载的函数名
 *
 * @see ll_loadfunc, luaL_checkstring
 *
 * 函数签名：
 * ```lua
 * function, err, where = package.loadlib(path, funcname)
 * ```
 *
 * 返回值说明：
 * - 成功：返回加载的C函数
 * - 失败：返回nil, 错误消息, 错误位置
 *
 * 错误类型：
 * - "open"：库文件加载失败
 * - "init"：函数符号查找失败
 *
 * 使用示例：
 * ```lua
 * local func, err, where = package.loadlib("mylib.so", "luaopen_mylib")
 * if func then
 *     func()  -- 调用加载的函数
 * else
 *     print("Error:", err, "at", where)
 * end
 * ```
 */
static int ll_loadlib (lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    const char *init = luaL_checkstring(L, 2);
    int stat = ll_loadfunc(L, path, init);
    if (stat == 0)  /* 没有错误？ */
        return 1;  /* 返回加载的函数 */
    else {  /* 错误；错误消息在栈顶 */
        lua_pushnil(L);
        lua_insert(L, -2);
        lua_pushstring(L, (stat == ERRLIB) ?  LIB_FAIL : "init");
        return 3;  /* 返回nil、错误消息和错误位置 */
    }
}

/**
 * @defgroup RequireSystem require函数系统
 * @brief Lua模块加载系统的核心实现
 *
 * require函数系统是Lua模块化的核心，提供了统一的模块加载
 * 接口和多种加载策略。它支持Lua模块、C模块、预加载模块
 * 等多种类型的模块加载。
 *
 * 核心功能：
 * - 模块搜索和定位
 * - 多种加载器支持
 * - 模块缓存管理
 * - 循环依赖检测
 * - 错误处理和报告
 *
 * 加载器类型：
 * - Lua加载器：加载.lua文件
 * - C加载器：加载动态库
 * - C根加载器：加载子模块
 * - 预加载器：从预加载表获取
 *
 * 设计特点：
 * - 可扩展的加载器机制
 * - 灵活的路径搜索
 * - 自动缓存和重用
 * - 详细的错误报告
 * @{
 */

/**
 * @brief 检查文件是否可读
 *
 * 尝试以只读模式打开文件，检查文件是否存在且可读。
 * 这是文件搜索过程中的基本检查函数。
 *
 * @param filename 要检查的文件名
 * @return 文件可读返回1，否则返回0
 *
 * @note 使用fopen进行简单的可读性测试
 * @note 立即关闭文件，不保持打开状态
 *
 * @see fopen, fclose
 *
 * 检查逻辑：
 * 1. 尝试以只读模式打开文件
 * 2. 如果打开失败，返回0
 * 3. 如果打开成功，立即关闭文件
 * 4. 返回1表示文件可读
 *
 * 适用场景：
 * - 模块文件搜索
 * - 路径有效性验证
 * - 文件存在性检查
 * - 权限验证
 *
 * 限制：
 * - 只检查读权限
 * - 不检查文件内容
 * - 可能受到文件锁影响
 * - 不处理网络文件系统延迟
 */
static int readable (const char *filename) {
    FILE *f = fopen(filename, "r");  /* 尝试打开文件 */
    if (f == NULL) return 0;  /* 打开失败 */
    fclose(f);
    return 1;
}

/**
 * @brief 从路径字符串中提取下一个模板
 *
 * 解析由分隔符分隔的路径字符串，提取下一个路径模板
 * 并将其推送到Lua栈上。
 *
 * @param L Lua状态机指针
 * @param path 当前路径字符串位置
 * @return 指向下一个模板后的位置，如果没有更多模板则返回NULL
 *
 * @note 跳过连续的路径分隔符
 * @note 将提取的模板推送到栈顶
 *
 * @see strchr, lua_pushlstring, LUA_PATHSEP
 *
 * 解析过程：
 * 1. 跳过开头的所有分隔符
 * 2. 检查是否到达字符串末尾
 * 3. 查找下一个分隔符位置
 * 4. 如果没有分隔符，使用字符串末尾
 * 5. 提取模板并推送到栈
 * 6. 返回下一个位置指针
 *
 * 路径格式：
 * - 使用LUA_PATHSEP作为分隔符（通常是分号）
 * - 支持空模板（连续分隔符）
 * - 自动处理字符串末尾
 *
 * 使用场景：
 * - package.path解析
 * - package.cpath解析
 * - 模块搜索路径遍历
 * - 配置字符串处理
 *
 * 错误处理：
 * - 空路径返回NULL
 * - 自动跳过无效模板
 * - 处理边界条件
 * - 保证字符串安全
 */
static const char *pushnexttemplate (lua_State *L, const char *path) {
    const char *l;
    while (*path == *LUA_PATHSEP) path++;  /* 跳过分隔符 */
    if (*path == '\0') return NULL;  /* 没有更多模板 */
    l = strchr(path, *LUA_PATHSEP);  /* 查找下一个分隔符 */
    if (l == NULL) l = path + strlen(path);
    lua_pushlstring(L, path, l - path);  /* 模板 */
    return l;
}


/**
 * @brief 在指定路径中查找模块文件
 *
 * 根据模块名和路径配置查找对应的文件。这是模块搜索的
 * 核心算法，支持路径模板和文件名转换。
 *
 * @param L Lua状态机指针
 * @param name 模块名（使用点号分隔）
 * @param pname 路径配置名（"path"或"cpath"）
 * @return 找到文件时返回文件名，否则返回NULL
 *
 * @note 模块名中的点号被转换为目录分隔符
 * @note 在栈上累积错误信息用于报告
 *
 * @see luaL_gsub, pushnexttemplate, readable, lua_getfield
 *
 * 搜索算法：
 * 1. 将模块名中的点号转换为目录分隔符
 * 2. 获取指定的路径配置（package.path或package.cpath）
 * 3. 遍历路径模板列表
 * 4. 对每个模板，替换占位符为模块名
 * 5. 检查生成的文件名是否可读
 * 6. 找到则返回文件名，否则累积错误信息
 * 7. 所有模板都失败则返回NULL
 *
 * 路径模板格式：
 * - 使用LUA_PATH_MARK（通常是?）作为占位符
 * - 支持多个路径用分隔符连接
 * - 自动处理目录分隔符转换
 *
 * 模块名转换：
 * ```
 * socket.core -> socket/core (Unix)
 * socket.core -> socket\core (Windows)
 * ```
 *
 * 路径模板示例：
 * ```
 * ./?.lua;/usr/local/share/lua/5.1/?.lua;/usr/share/lua/5.1/?.lua
 * ```
 *
 * 错误累积：
 * - 收集所有尝试过的文件名
 * - 生成详细的错误报告
 * - 帮助用户诊断问题
 * - 支持调试和故障排除
 */
static const char *findfile (lua_State *L, const char *name,
                                           const char *pname) {
    const char *path;
    name = luaL_gsub(L, name, ".", LUA_DIRSEP);
    lua_getfield(L, LUA_ENVIRONINDEX, pname);
    path = lua_tostring(L, -1);
    if (path == NULL)
        luaL_error(L, LUA_QL("package.%s") " must be a string", pname);
    lua_pushliteral(L, "");  /* 错误累积器 */
    while ((path = pushnexttemplate(L, path)) != NULL) {
        const char *filename;
        filename = luaL_gsub(L, lua_tostring(L, -1), LUA_PATH_MARK, name);
        lua_remove(L, -2);  /* 移除路径模板 */
        if (readable(filename))  /* 文件存在且可读？ */
            return filename;  /* 返回该文件名 */
        lua_pushfstring(L, "\n\tno file " LUA_QS, filename);
        lua_remove(L, -2);  /* 移除文件名 */
        lua_concat(L, 2);  /* 添加条目到可能的错误消息 */
    }
    return NULL;  /* 未找到 */
}

/**
 * @brief 模块加载错误报告
 *
 * 生成详细的模块加载错误信息，包括模块名、文件名和
 * 具体的错误原因。
 *
 * @param L Lua状态机指针
 * @param filename 尝试加载的文件名
 *
 * @note 错误信息在栈顶
 * @note 模块名在栈位置1
 *
 * @see luaL_error, lua_tostring
 *
 * 错误信息格式：
 * ```
 * error loading module 'module_name' from file 'filename':
 *     具体错误信息
 * ```
 *
 * 信息来源：
 * - 模块名：从栈位置1获取
 * - 文件名：从参数获取
 * - 错误详情：从栈顶获取
 *
 * 使用场景：
 * - Lua文件编译错误
 * - C库加载错误
 * - 符号查找错误
 * - 权限问题
 */
static void loaderror (lua_State *L, const char *filename) {
    luaL_error(L, "error loading module " LUA_QS " from file " LUA_QS ":\n\t%s",
                lua_tostring(L, 1), filename, lua_tostring(L, -1));
}

/**
 * @brief Lua模块加载器
 *
 * 在package.path中搜索并加载Lua模块文件。这是require
 * 函数使用的第一个加载器。
 *
 * @param L Lua状态机指针
 * @return 成功时返回1（加载的函数），失败时返回1（错误信息）
 *
 * @note 参数1：模块名
 * @note 使用luaL_loadfile加载Lua文件
 *
 * @see findfile, luaL_loadfile, loaderror
 *
 * 加载过程：
 * 1. 获取模块名参数
 * 2. 在package.path中查找对应的.lua文件
 * 3. 如果未找到，返回失败（错误信息在栈上）
 * 4. 如果找到，使用luaL_loadfile编译文件
 * 5. 编译成功返回编译后的函数
 * 6. 编译失败调用loaderror报告错误
 *
 * 搜索路径：
 * - 使用package.path配置
 * - 支持多个搜索目录
 * - 自动处理文件扩展名
 * - 遵循Lua文件命名规范
 *
 * 文件处理：
 * - 编译Lua源代码为字节码
 * - 检查语法错误
 * - 返回可执行的函数
 * - 保持源文件信息用于调试
 *
 * 错误处理：
 * - 文件未找到：返回搜索错误信息
 * - 编译错误：报告详细的语法错误
 * - 权限错误：报告文件访问问题
 * - 其他错误：统一的错误格式
 */
static int loader_Lua (lua_State *L) {
    const char *filename;
    const char *name = luaL_checkstring(L, 1);
    filename = findfile(L, name, "path");
    if (filename == NULL) return 1;  /* 在此路径中未找到库 */
    if (luaL_loadfile(L, filename) != 0)
        loaderror(L, filename);
    return 1;  /* 库加载成功 */
}


/**
 * @brief 生成C模块的函数名
 *
 * 根据模块名生成对应的C函数名。处理模块名中的特殊标记
 * 和层次结构，生成符合C命名规范的函数名。
 *
 * @param L Lua状态机指针
 * @param modname 模块名
 * @return 生成的C函数名
 *
 * @note 处理LUA_IGMARK标记（忽略前缀）
 * @note 将点号转换为下划线
 *
 * @see strchr, luaL_gsub, lua_pushfstring, POF
 *
 * 转换规则：
 * 1. 检查是否有忽略标记（LUA_IGMARK，通常是'-'）
 * 2. 如果有标记，使用标记后的部分作为模块名
 * 3. 将模块名中的点号替换为下划线
 * 4. 添加函数前缀（POF，通常是"luaopen_"）
 * 5. 返回完整的函数名
 *
 * 转换示例：
 * ```
 * socket.core -> luaopen_socket_core
 * -mymodule -> luaopen_mymodule (忽略前缀)
 * math -> luaopen_math
 * ```
 *
 * 忽略标记用途：
 * - 允许模块名与函数名不完全对应
 * - 支持复杂的模块组织结构
 * - 处理命名冲突
 * - 提供灵活的命名策略
 *
 * 函数名规范：
 * - 必须是有效的C标识符
 * - 遵循luaopen_前缀约定
 * - 使用下划线分隔层次
 * - 区分大小写
 */
static const char *mkfuncname (lua_State *L, const char *modname) {
    const char *funcname;
    const char *mark = strchr(modname, *LUA_IGMARK);
    if (mark) modname = mark + 1;
    funcname = luaL_gsub(L, modname, ".", LUA_OFSEP);
    funcname = lua_pushfstring(L, POF"%s", funcname);
    lua_remove(L, -2);  /* 移除'gsub'结果 */
    return funcname;
}

/**
 * @brief C模块加载器
 *
 * 在package.cpath中搜索并加载C动态库模块。这是require
 * 函数使用的第二个加载器。
 *
 * @param L Lua状态机指针
 * @return 成功时返回1（加载的函数），失败时返回1（错误信息）
 *
 * @note 参数1：模块名
 * @note 使用ll_loadfunc加载C函数
 *
 * @see findfile, mkfuncname, ll_loadfunc, loaderror
 *
 * 加载过程：
 * 1. 获取模块名参数
 * 2. 在package.cpath中查找对应的动态库文件
 * 3. 如果未找到，返回失败（错误信息在栈上）
 * 4. 生成对应的C函数名
 * 5. 加载动态库并查找函数
 * 6. 成功返回函数，失败报告错误
 *
 * 搜索路径：
 * - 使用package.cpath配置
 * - 支持多个搜索目录
 * - 自动处理库文件扩展名
 * - 遵循系统动态库命名规范
 *
 * 函数查找：
 * - 根据模块名生成函数名
 * - 查找luaopen_前缀的函数
 * - 处理模块层次结构
 * - 支持忽略标记
 *
 * 错误处理：
 * - 库文件未找到：返回搜索错误信息
 * - 库加载失败：报告动态库错误
 * - 函数未找到：报告符号查找错误
 * - 其他错误：统一的错误格式
 */
static int loader_C (lua_State *L) {
    const char *funcname;
    const char *name = luaL_checkstring(L, 1);
    const char *filename = findfile(L, name, "cpath");
    if (filename == NULL) return 1;  /* 在此路径中未找到库 */
    funcname = mkfuncname(L, name);
    if (ll_loadfunc(L, filename, funcname) != 0)
        loaderror(L, filename);
    return 1;  /* 库加载成功 */
}

/**
 * @brief C根模块加载器
 *
 * 尝试从根模块的动态库中加载子模块。这是require函数
 * 使用的第三个加载器，用于处理模块层次结构。
 *
 * @param L Lua状态机指针
 * @return 成功时返回1（加载的函数），失败时返回1（错误信息）
 *
 * @note 参数1：子模块名（包含点号）
 * @note 只处理有层次结构的模块名
 *
 * @see strchr, lua_pushlstring, findfile, mkfuncname, ll_loadfunc
 *
 * 加载策略：
 * 1. 检查模块名是否包含点号（层次结构）
 * 2. 如果是根模块，直接返回（不处理）
 * 3. 提取根模块名（点号前的部分）
 * 4. 在package.cpath中查找根模块的动态库
 * 5. 使用完整模块名生成函数名
 * 6. 尝试从根模块库中加载子模块函数
 *
 * 使用场景：
 * ```
 * require("socket.core")
 * 1. 提取根模块名：socket
 * 2. 查找：socket.so 或 socket.dll
 * 3. 生成函数名：luaopen_socket_core
 * 4. 从socket库中加载luaopen_socket_core函数
 * ```
 *
 * 错误分类：
 * - 根模块未找到：返回搜索错误
 * - 库加载失败：报告动态库错误
 * - 函数未找到：返回函数查找错误（不是致命错误）
 * - 其他错误：报告详细错误信息
 *
 * 设计优势：
 * - 支持模块的层次组织
 * - 减少动态库文件数量
 * - 提高加载效率
 * - 简化部署和分发
 */
static int loader_Croot (lua_State *L) {
    const char *funcname;
    const char *filename;
    const char *name = luaL_checkstring(L, 1);
    const char *p = strchr(name, '.');
    int stat;
    if (p == NULL) return 0;  /* 是根模块 */
    lua_pushlstring(L, name, p - name);
    filename = findfile(L, lua_tostring(L, -1), "cpath");
    if (filename == NULL) return 1;  /* 未找到根模块 */
    funcname = mkfuncname(L, name);
    if ((stat = ll_loadfunc(L, filename, funcname)) != 0) {
        if (stat != ERRFUNC) loaderror(L, filename);  /* 真正的错误 */
        lua_pushfstring(L, "\n\tno module " LUA_QS " in file " LUA_QS,
                           name, filename);
        return 1;  /* 未找到函数 */
    }
    return 1;
}


/**
 * @brief 预加载模块加载器
 *
 * 从package.preload表中查找预加载的模块。这是require函数
 * 使用的第四个（最后一个）加载器，用于加载预先注册的模块。
 *
 * @param L Lua状态机指针
 * @return 成功时返回1（预加载的函数），失败时返回1（错误信息）
 *
 * @note 参数1：模块名
 * @note 从package.preload表中查找模块
 *
 * @see lua_getfield, lua_isnil, lua_pushfstring
 *
 * 预加载机制：
 * - package.preload是一个表，键为模块名，值为加载函数
 * - 允许在运行时预先注册模块
 * - 避免文件系统访问，提高加载速度
 * - 支持内嵌模块和动态注册
 *
 * 查找过程：
 * 1. 获取模块名参数
 * 2. 检查package.preload表是否存在且为表类型
 * 3. 在preload表中查找指定模块名
 * 4. 如果找到，返回对应的加载函数
 * 5. 如果未找到，返回错误信息
 *
 * 使用场景：
 * - 内嵌Lua模块到C程序
 * - 预编译的Lua字节码
 * - 动态生成的模块
 * - 测试和调试环境
 *
 * 注册示例：
 * ```lua
 * package.preload["mymodule"] = function()
 *     return { version = "1.0" }
 * end
 * ```
 *
 * 错误处理：
 * - preload表不存在或不是表：抛出错误
 * - 模块未找到：返回格式化的错误信息
 * - 错误信息包含具体的模块名
 */
static int loader_preload (lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    lua_getfield(L, LUA_ENVIRONINDEX, "preload");
    if (!lua_istable(L, -1))
        luaL_error(L, LUA_QL("package.preload") " must be a table");
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1))  /* 未找到？ */
        lua_pushfstring(L, "\n\tno field package.preload['%s']", name);
    return 1;
}

/**
 * @brief 循环依赖检测哨兵值
 *
 * 用于检测模块加载过程中的循环依赖。这是一个唯一的轻量用户数据，
 * 在模块加载期间临时存储在_LOADED表中。
 *
 * @note 使用静态变量的地址作为唯一标识
 * @note 轻量用户数据，不占用额外内存
 */
static const int sentinel_ = 0;
#define sentinel	((void *)&sentinel_)

/**
 * @brief require函数的核心实现
 *
 * 这是Lua模块系统的核心函数，实现了完整的模块加载流程，
 * 包括缓存检查、循环依赖检测、加载器链执行和结果缓存。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（加载的模块或错误）
 *
 * @note 参数1：模块名
 * @note 实现了完整的require语义
 *
 * @see lua_getfield, lua_call, lua_setfield, luaL_error
 *
 * 完整加载流程：
 * 1. **缓存检查**：在_LOADED表中查找已加载的模块
 * 2. **循环检测**：检查是否存在循环依赖
 * 3. **加载器链**：依次尝试所有可用的加载器
 * 4. **模块执行**：运行加载的模块函数
 * 5. **结果缓存**：将模块结果存储到_LOADED表
 *
 * 缓存机制：
 * - _LOADED表存储所有已加载的模块
 * - 避免重复加载同一模块
 * - 支持模块的单例模式
 * - 提高加载性能
 *
 * 循环依赖检测：
 * - 使用哨兵值标记正在加载的模块
 * - 检测A->B->A类型的循环依赖
 * - 防止无限递归和栈溢出
 * - 提供清晰的错误信息
 *
 * 加载器链执行：
 * 1. loader_preload：预加载表
 * 2. loader_Lua：Lua文件加载器
 * 3. loader_C：C动态库加载器
 * 4. loader_Croot：C根模块加载器
 *
 * 错误累积：
 * - 收集所有加载器的错误信息
 * - 生成详细的失败报告
 * - 帮助用户诊断问题
 * - 支持调试和故障排除
 *
 * 模块执行：
 * - 将模块名作为参数传递给模块函数
 * - 支持模块的自我识别
 * - 处理模块的返回值
 * - 管理模块的生命周期
 *
 * 结果处理：
 * - 非nil返回值：直接缓存
 * - nil返回值：使用true作为默认值
 * - 确保_LOADED表中总有有效值
 * - 支持模块的重新加载
 */
static int ll_require (lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    int i;
    lua_settop(L, 1);  /* _LOADED表将在索引2 */
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, 2, name);
    if (lua_toboolean(L, -1)) {  /* 已存在？ */
        if (lua_touserdata(L, -1) == sentinel)  /* 检查循环 */
            luaL_error(L, "loop or previous error loading module " LUA_QS, name);
        return 1;  /* 包已经加载 */
    }
    /* 否则必须加载它；遍历可用的加载器 */
    lua_getfield(L, LUA_ENVIRONINDEX, "loaders");
    if (!lua_istable(L, -1))
        luaL_error(L, LUA_QL("package.loaders") " must be a table");
    lua_pushliteral(L, "");  /* 错误消息累积器 */
    for (i=1; ; i++) {
        lua_rawgeti(L, -2, i);  /* 获取一个加载器 */
        if (lua_isnil(L, -1))
            luaL_error(L, "module " LUA_QS " not found:%s",
                        name, lua_tostring(L, -2));
        lua_pushstring(L, name);
        lua_call(L, 1, 1);  /* 调用它 */
        if (lua_isfunction(L, -1))  /* 找到模块了吗？ */
            break;  /* 模块加载成功 */
        else if (lua_isstring(L, -1))  /* 加载器返回错误消息？ */
            lua_concat(L, 2);  /* 累积它 */
        else
            lua_pop(L, 1);
    }
    lua_pushlightuserdata(L, sentinel);
    lua_setfield(L, 2, name);  /* _LOADED[name] = sentinel */
    lua_pushstring(L, name);  /* 将名称作为参数传递给模块 */
    lua_call(L, 1, 1);  /* 运行加载的模块 */
    if (!lua_isnil(L, -1))  /* 非nil返回？ */
        lua_setfield(L, 2, name);  /* _LOADED[name] = 返回值 */
    lua_getfield(L, 2, name);
    if (lua_touserdata(L, -1) == sentinel) {   /* 模块没有设置值？ */
        lua_pushboolean(L, 1);  /* 使用true作为结果 */
        lua_pushvalue(L, -1);  /* 额外的副本用于返回 */
        lua_setfield(L, 2, name);  /* _LOADED[name] = true */
    }
    return 1;
}

/** @} */ /* 结束require函数系统文档组 */



/**
 * @defgroup ModuleSystem module函数系统
 * @brief Lua模块定义和环境管理系统
 *
 * module函数系统提供了Lua模块的定义和环境管理功能。
 * 它支持模块的创建、初始化、环境设置和选项处理。
 *
 * 核心功能：
 * - 模块表的创建和初始化
 * - 模块环境的设置和隔离
 * - 模块元信息的管理
 * - 模块选项的处理
 * - 全局访问的控制
 *
 * 设计特点：
 * - 自动模块表创建
 * - 环境隔离机制
 * - 灵活的选项系统
 * - 向后兼容性支持
 * @{
 */

/**
 * @brief 设置调用函数的环境
 *
 * 将指定的表设置为调用module函数的Lua函数的环境。
 * 这实现了模块的环境隔离。
 *
 * @param L Lua状态机指针
 *
 * @note 栈顶-1：新环境表
 * @note 栈顶：调用函数
 *
 * @see lua_getstack, lua_getinfo, lua_setfenv
 *
 * 环境设置过程：
 * 1. 获取调用栈信息
 * 2. 获取调用函数的信息
 * 3. 检查调用者是否为Lua函数
 * 4. 设置函数的环境为指定表
 * 5. 清理栈
 *
 * 安全检查：
 * - 确保有有效的调用栈
 * - 确保调用者是Lua函数而不是C函数
 * - 防止在不当上下文中调用
 *
 * 环境隔离：
 * - 模块内的全局变量访问被重定向
 * - 提供模块的私有命名空间
 * - 避免全局命名空间污染
 * - 支持模块的封装性
 */
static void setfenv (lua_State *L) {
    lua_Debug ar;
    if (lua_getstack(L, 1, &ar) == 0 ||
        lua_getinfo(L, "f", &ar) == 0 ||  /* 获取调用函数 */
        lua_iscfunction(L, -1))
        luaL_error(L, LUA_QL("module") " not called from a Lua function");
    lua_pushvalue(L, -2);
    lua_setfenv(L, -2);
    lua_pop(L, 1);
}

/**
 * @brief 处理模块选项
 *
 * 执行传递给module函数的所有选项函数。选项函数用于
 * 配置模块的行为和属性。
 *
 * @param L Lua状态机指针
 * @param n 参数总数
 *
 * @note 从参数2开始处理选项
 * @note 每个选项都是一个函数
 *
 * @see lua_pushvalue, lua_call
 *
 * 选项处理：
 * 1. 遍历从参数2到参数n的所有选项
 * 2. 每个选项都是一个函数
 * 3. 调用选项函数，传递模块表作为参数
 * 4. 选项函数可以修改模块表
 *
 * 常见选项：
 * - package.seeall：允许访问全局变量
 * - 自定义初始化函数
 * - 模块配置函数
 * - 兼容性处理函数
 *
 * 使用示例：
 * ```lua
 * module("mymodule", package.seeall)
 * ```
 */
static void dooptions (lua_State *L, int n) {
    int i;
    for (i = 2; i <= n; i++) {
        lua_pushvalue(L, i);  /* 获取选项（一个函数） */
        lua_pushvalue(L, -2);  /* 模块 */
        lua_call(L, 1, 0);
    }
}

/**
 * @brief 初始化模块表
 *
 * 为模块表设置标准的元信息字段，包括模块引用、
 * 模块名和包名。
 *
 * @param L Lua状态机指针
 * @param modname 模块名
 *
 * @note 栈顶：模块表
 * @note 设置_M、_NAME、_PACKAGE字段
 *
 * @see strrchr, lua_pushlstring, lua_setfield
 *
 * 初始化字段：
 * - _M：指向模块表自身的引用
 * - _NAME：完整的模块名
 * - _PACKAGE：包名（模块名去掉最后一部分）
 *
 * 包名计算：
 * ```
 * socket.core -> _PACKAGE = "socket."
 * math -> _PACKAGE = ""
 * a.b.c -> _PACKAGE = "a.b."
 * ```
 *
 * 用途：
 * - 模块自我识别
 * - 相对模块加载
 * - 调试和诊断
 * - 工具和框架支持
 */
static void modinit (lua_State *L, const char *modname) {
    const char *dot;
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_M");  /* module._M = module */
    lua_pushstring(L, modname);
    lua_setfield(L, -2, "_NAME");
    dot = strrchr(modname, '.');  /* 在模块名中查找最后一个点 */
    if (dot == NULL) dot = modname;
    else dot++;
    /* 设置_PACKAGE为包名（完整模块名减去最后部分） */
    lua_pushlstring(L, modname, dot - modname);
    lua_setfield(L, -2, "_PACKAGE");
}

/**
 * @brief module函数的实现
 *
 * 创建或获取模块表，设置模块环境，并处理模块选项。
 * 这是Lua模块系统的核心函数之一。
 *
 * @param L Lua状态机指针
 * @return 总是返回0（无返回值）
 *
 * @note 参数1：模块名
 * @note 参数2+：模块选项函数
 *
 * @see luaL_findtable, setfenv, dooptions, modinit
 *
 * 模块创建流程：
 * 1. 检查_LOADED表中是否已有模块
 * 2. 如果没有，尝试创建全局变量表
 * 3. 检查模块表是否已初始化
 * 4. 如果未初始化，调用modinit初始化
 * 5. 设置调用函数的环境为模块表
 * 6. 处理所有模块选项
 *
 * 模块查找优先级：
 * 1. _LOADED表中的已加载模块
 * 2. 全局变量中的现有表
 * 3. 新创建的模块表
 *
 * 环境设置：
 * - 将模块表设置为调用函数的环境
 * - 模块内的全局变量访问被重定向
 * - 实现模块的命名空间隔离
 *
 * 错误处理：
 * - 模块名冲突检测
 * - 调用上下文验证
 * - 参数类型检查
 * - 详细的错误报告
 */
static int ll_module (lua_State *L) {
    const char *modname = luaL_checkstring(L, 1);
    int loaded = lua_gettop(L) + 1;  /* _LOADED表的索引 */
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, loaded, modname);  /* 获取_LOADED[modname] */
    if (!lua_istable(L, -1)) {  /* 未找到？ */
        lua_pop(L, 1);  /* 移除之前的结果 */
        /* 尝试全局变量（如果不存在则创建一个） */
        if (luaL_findtable(L, LUA_GLOBALSINDEX, modname, 1) != NULL)
            return luaL_error(L, "name conflict for module " LUA_QS, modname);
        lua_pushvalue(L, -1);
        lua_setfield(L, loaded, modname);  /* _LOADED[modname] = 新表 */
    }
    /* 检查表是否已有_NAME字段 */
    lua_getfield(L, -1, "_NAME");
    if (!lua_isnil(L, -1))  /* 表是已初始化的模块？ */
        lua_pop(L, 1);
    else {  /* 否；初始化它 */
        lua_pop(L, 1);
        modinit(L, modname);
    }
    lua_pushvalue(L, -1);
    setfenv(L);
    dooptions(L, loaded - 1);
    return 0;
}

/**
 * @brief package.seeall函数的实现
 *
 * 为模块表设置元表，使其能够访问全局变量。这是一个
 * 常用的模块选项函数。
 *
 * @param L Lua状态机指针
 * @return 总是返回0（无返回值）
 *
 * @note 参数1：模块表
 * @note 设置__index元方法指向全局表
 *
 * @see luaL_checktype, lua_getmetatable, lua_setfield
 *
 * 实现机制：
 * 1. 检查参数是否为表
 * 2. 获取或创建模块表的元表
 * 3. 设置__index元方法指向全局表(_G)
 * 4. 实现全局变量的透明访问
 *
 * 访问机制：
 * - 模块表中不存在的键会查找全局表
 * - 保持对全局函数和变量的访问
 * - 不影响模块表的写入操作
 * - 提供向后兼容性
 *
 * 使用效果：
 * ```lua
 * module("mymodule", package.seeall)
 * -- 现在可以直接使用print、table等全局函数
 * print("Hello from module")
 * ```
 *
 * 注意事项：
 * - 可能导致意外的全局变量访问
 * - 影响模块的封装性
 * - 在新代码中不推荐使用
 * - 主要用于向后兼容
 */
static int ll_seeall (lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!lua_getmetatable(L, 1)) {
        lua_createtable(L, 0, 1); /* 创建新元表 */
        lua_pushvalue(L, -1);
        lua_setmetatable(L, 1);
    }
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfield(L, -2, "__index");  /* mt.__index = _G */
    return 0;
}

/** @} */ /* 结束module函数系统文档组 */



/**
 * @defgroup PackageInitialization package库初始化
 * @brief package库的初始化和配置管理
 *
 * package库初始化系统负责设置Lua模块系统的运行环境，
 * 包括路径配置、加载器注册、元表创建和全局函数导出。
 *
 * 核心功能：
 * - 环境变量处理和路径配置
 * - 加载器链的初始化
 * - 模块缓存表的创建
 * - 全局函数的注册
 * - 兼容性支持
 *
 * 配置管理：
 * - 自动处理环境变量
 * - 默认路径的设置
 * - 平台相关的配置
 * - 用户自定义扩展
 * @{
 */

/**
 * @brief 内部使用的辅助标记
 *
 * 用于路径处理中的临时标记，帮助实现默认路径的插入。
 */
#define AUXMARK		"\1"

/**
 * @brief 设置package路径配置
 *
 * 从环境变量或默认值设置package.path或package.cpath。
 * 支持默认路径的自动插入和程序目录的处理。
 *
 * @param L Lua状态机指针
 * @param fieldname 字段名（"path"或"cpath"）
 * @param envname 环境变量名（如"LUA_PATH"）
 * @param def 默认路径字符串
 *
 * @note 支持";;"语法插入默认路径
 * @note 自动处理程序目录替换
 *
 * @see getenv, luaL_gsub, setprogdir
 *
 * 路径处理算法：
 * 1. 尝试从环境变量获取路径
 * 2. 如果环境变量不存在，使用默认路径
 * 3. 如果环境变量存在，处理";;"语法
 * 4. 将";;"替换为";AUXMARK;"
 * 5. 将AUXMARK替换为默认路径
 * 6. 调用setprogdir处理程序目录
 * 7. 设置到package表的指定字段
 *
 * 默认路径插入：
 * ```
 * 环境变量：/custom/path;;/another/path
 * 处理后：/custom/path;/default/path;/another/path
 * ```
 *
 * 程序目录处理：
 * - 替换LUA_EXECDIR占位符
 * - 支持相对于可执行文件的路径
 * - 提高可移植性
 * - 简化部署配置
 *
 * 使用场景：
 * - package.path的初始化
 * - package.cpath的初始化
 * - 自定义搜索路径的设置
 * - 环境相关的配置管理
 */
static void setpath (lua_State *L, const char *fieldname, const char *envname,
                                   const char *def) {
    const char *path = getenv(envname);
    if (path == NULL)  /* 没有环境变量？ */
        lua_pushstring(L, def);  /* 使用默认值 */
    else {
        /* 将";;"替换为";AUXMARK;"，然后将AUXMARK替换为默认路径 */
        path = luaL_gsub(L, path, LUA_PATHSEP LUA_PATHSEP,
                                  LUA_PATHSEP AUXMARK LUA_PATHSEP);
        luaL_gsub(L, path, AUXMARK, def);
        lua_remove(L, -2);
    }
    setprogdir(L);
    lua_setfield(L, -2, fieldname);
}

/**
 * @brief package表的函数注册表
 *
 * 定义了package表中的所有函数，包括loadlib和seeall。
 */
static const luaL_Reg pk_funcs[] = {
    {"loadlib", ll_loadlib},
    {"seeall", ll_seeall},
    {NULL, NULL}
};

/**
 * @brief 全局函数注册表
 *
 * 定义了要注册到全局环境的函数，包括module和require。
 */
static const luaL_Reg ll_funcs[] = {
    {"module", ll_module},
    {"require", ll_require},
    {NULL, NULL}
};

/**
 * @brief 预定义的加载器数组
 *
 * 定义了require函数使用的所有加载器，按优先级顺序排列。
 */
static const lua_CFunction loaders[] =
    {loader_preload, loader_Lua, loader_C, loader_Croot, NULL};

/**
 * @brief package库的初始化函数
 *
 * 初始化整个package库，设置所有必要的表、函数和配置。
 * 这是Lua模块系统的入口点。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（package表）
 *
 * @note 创建完整的模块加载环境
 * @note 设置所有必要的配置和缓存
 *
 * @see luaL_newmetatable, luaL_register, setpath
 *
 * 初始化流程：
 * 1. **元表创建**：创建_LOADLIB元表用于垃圾回收
 * 2. **package表**：创建并注册package表和函数
 * 3. **兼容性**：处理向后兼容性（如果启用）
 * 4. **环境设置**：将package表设置为环境表
 * 5. **加载器链**：创建并填充loaders数组
 * 6. **路径配置**：设置path和cpath搜索路径
 * 7. **配置信息**：存储平台相关的配置常量
 * 8. **缓存表**：创建loaded和preload表
 * 9. **全局函数**：注册require和module到全局环境
 *
 * 创建的表和字段：
 * - package.loaders：加载器函数数组
 * - package.path：Lua文件搜索路径
 * - package.cpath：C库搜索路径
 * - package.config：平台配置信息
 * - package.loaded：已加载模块缓存
 * - package.preload：预加载模块表
 *
 * 配置信息格式：
 * ```
 * 目录分隔符\n路径分隔符\n路径标记\n可执行目录\n忽略标记
 * ```
 *
 * 兼容性处理：
 * - LUA_COMPAT_LOADLIB：将loadlib导出为全局函数
 * - 支持旧版本的使用方式
 * - 保持API的向后兼容
 *
 * 内存管理：
 * - 自动垃圾回收支持
 * - 元表控制资源生命周期
 * - 防止内存泄漏
 * - 异常安全的初始化
 *
 * 扩展性：
 * - 支持自定义加载器
 * - 允许路径配置修改
 * - 提供配置信息访问
 * - 支持第三方扩展
 */
LUALIB_API int luaopen_package (lua_State *L) {
    int i;
    /* 创建新类型_LOADLIB */
    luaL_newmetatable(L, "_LOADLIB");
    lua_pushcfunction(L, gctm);
    lua_setfield(L, -2, "__gc");
    /* 创建`package'表 */
    luaL_register(L, LUA_LOADLIBNAME, pk_funcs);
#if defined(LUA_COMPAT_LOADLIB)
    lua_getfield(L, -1, "loadlib");
    lua_setfield(L, LUA_GLOBALSINDEX, "loadlib");
#endif
    lua_pushvalue(L, -1);
    lua_replace(L, LUA_ENVIRONINDEX);
    /* 创建`loaders'表 */
    lua_createtable(L, sizeof(loaders)/sizeof(loaders[0]) - 1, 0);
    /* 用预定义的加载器填充它 */
    for (i=0; loaders[i] != NULL; i++) {
        lua_pushcfunction(L, loaders[i]);
        lua_rawseti(L, -2, i+1);
    }
    lua_setfield(L, -2, "loaders");  /* 放入字段`loaders' */
    setpath(L, "path", LUA_PATH, LUA_PATH_DEFAULT);  /* 设置字段`path' */
    setpath(L, "cpath", LUA_CPATH, LUA_CPATH_DEFAULT); /* 设置字段`cpath' */
    /* 存储配置信息 */
    lua_pushliteral(L, LUA_DIRSEP "\n" LUA_PATHSEP "\n" LUA_PATH_MARK "\n"
                       LUA_EXECDIR "\n" LUA_IGMARK);
    lua_setfield(L, -2, "config");
    /* 设置字段`loaded' */
    luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 2);
    lua_setfield(L, -2, "loaded");
    /* 设置字段`preload' */
    lua_newtable(L);
    lua_setfield(L, -2, "preload");
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    luaL_register(L, NULL, ll_funcs);  /* 将库打开到全局表 */
    lua_pop(L, 1);
    return 1;  /* 返回'package'表 */
}

/** @} */ /* 结束package库初始化文档组 */

