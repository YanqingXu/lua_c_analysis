/*
** [核心] Lua 动态库加载器实现
**
** 功能概述：
** 本模块实现了 Lua 的动态库加载功能，提供跨平台的模块加载机制。
** 支持多种操作系统的动态链接接口，包括 Unix 系统的 dlfcn、Windows 的 DLL、
** macOS 的 dyld，以及不支持动态链接系统的回退实现。
**
** 主要功能模块：
** - 动态库加载：跨平台的动态库加载和卸载
** - 符号解析：从动态库中查找和获取函数符号
** - 模块管理：Lua 模块的注册、缓存和生命周期管理
** - require 机制：完整的模块加载和依赖解析系统
** - 路径搜索：灵活的模块文件搜索机制
** - 错误处理：统一的错误报告和异常处理
**
** 平台支持：
** - Unix/Linux：基于 dlfcn 接口（dlopen、dlsym、dlclose）
** - Windows：基于 Win32 API（LoadLibrary、GetProcAddress、FreeLibrary）
** - macOS/Darwin：基于 dyld 接口（NSLinkModule、NSLookupSymbol）
** - 其他系统：提供回退实现，报告不支持动态链接
**
** 设计特点：
** - 跨平台兼容性：统一的接口，平台特定的实现
** - 内存安全：自动垃圾回收，防止库句柄泄漏
** - 错误恢复：详细的错误信息和优雅的失败处理
** - 性能优化：库缓存机制，避免重复加载
** - 模块化设计：清晰的功能分离和接口抽象
**
** 版本信息：$Id: loadlib.c,v 1.52.1.4 2009/09/09 13:17:16 roberto Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

// 系统头文件包含
#include <stdlib.h>
#include <string.h>

// 模块标识定义
#define loadlib_c
#define LUA_LIB

// Lua 核心头文件
#include "lua.h"

// Lua 辅助库头文件
#include "lauxlib.h"
#include "lualib.h"

/*
** [常量定义] 模块加载相关常量
**
** 常量说明：
** - LUA_POF：C 库中打开函数的前缀，标准为 "luaopen_"
** - LUA_OFSEP：C 库中打开函数名的分隔符，用于处理子模块
** - LIBPREFIX：库注册表中的键前缀，用于标识已加载的库
** - POF：实际使用的函数前缀（可能因平台而异）
** - LIB_FAIL：库加载失败时的错误类型标识
**
** 设计说明：
** 这些常量定义了 Lua 模块加载的命名约定和内部标识符，
** 确保不同平台和不同类型的模块能够正确识别和加载。
*/
#define LUA_POF		"luaopen_"    // C 库打开函数前缀
#define LUA_OFSEP	"_"           // 函数名分隔符

#define LIBPREFIX	"LOADLIB: "   // 库注册表键前缀
#define POF		LUA_POF           // 默认函数前缀
#define LIB_FAIL	"open"        // 库加载失败标识

/*
** [错误代码] 动态库加载错误类型
**
** 错误类型说明：
** - ERRLIB：库加载错误，无法打开指定的动态库文件
** - ERRFUNC：函数查找错误，库已加载但找不到指定的函数符号
**
** 用途说明：
** 这些错误代码用于 ll_loadfunc 函数的返回值，
** 帮助调用者区分不同类型的加载失败原因。
*/
#define ERRLIB		1    // 库加载失败
#define ERRFUNC		2    // 函数查找失败

/*
** [平台适配] 程序目录设置函数
**
** 功能说明：
** 在大多数平台上，这是一个空操作。
** 在 Windows 平台上，会被重新定义为实际的实现。
**
** 设计目的：
** 提供平台无关的接口，具体实现根据编译时的平台宏决定。
*/
#define setprogdir(L)		((void)0)

/*
** [函数声明] 平台相关的动态库操作函数
**
** 函数说明：
** - ll_unloadlib：卸载动态库，释放库句柄
** - ll_load：加载动态库，返回库句柄
** - ll_sym：从库中查找符号，返回函数指针
**
** 实现说明：
** 这些函数的具体实现根据编译时的平台宏选择：
** - LUA_DL_DLOPEN：Unix/Linux 系统使用 dlfcn 接口
** - LUA_DL_DLL：Windows 系统使用 Win32 API
** - LUA_DL_DYLD：macOS 系统使用 dyld 接口
** - 其他：使用回退实现，报告不支持
*/
static void ll_unloadlib(void *lib);
static void *ll_load(lua_State *L, const char *path);
static lua_CFunction ll_sym(lua_State *L, void *lib, const char *sym);

/*
** ========================================================================
** [平台实现] Unix/Linux 系统 - 基于 dlfcn 接口
** ========================================================================
**
** 平台特点：
** dlfcn 接口是 POSIX 标准的一部分，广泛支持于各种 Unix 系统：
** - Linux、SunOS、Solaris、IRIX、FreeBSD、NetBSD、AIX、HPUX 等
** - 提供统一的动态链接接口，兼容性好
** - 支持延迟绑定和符号可见性控制
**
** 技术原理：
** - dlopen：打开动态库，支持多种加载模式
** - dlsym：查找符号，返回函数或变量地址
** - dlclose：关闭动态库，减少引用计数
** - dlerror：获取最后一次操作的错误信息
**
** 性能特点：
** - 高效的符号查找算法
** - 支持符号缓存和预加载
** - 最小化内存占用
*/
#if defined(LUA_DL_DLOPEN)

#include <dlfcn.h>

/*
** [动态链接] 卸载动态库
**
** 功能描述：
** 使用 dlclose 关闭动态库，减少引用计数。
** 当引用计数为零时，系统会卸载库并释放内存。
**
** 参数说明：
** @param lib - void*：动态库句柄
**
** 实现说明：
** dlclose 是线程安全的，可以在多线程环境中安全调用。
** 如果库中有全局析构函数，会在卸载时自动调用。
*/
static void ll_unloadlib(void *lib) 
{
    dlclose(lib);
}

/*
** [动态链接] 加载动态库
**
** 功能描述：
** 使用 dlopen 加载指定路径的动态库。
** 采用 RTLD_NOW 模式，立即解析所有符号。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param path - const char*：动态库文件路径
**
** 返回值：
** @return void*：成功返回库句柄，失败返回 NULL
**
** 错误处理：
** 加载失败时，将错误信息推送到 Lua 栈顶。
**
** 加载模式说明：
** - RTLD_NOW：立即解析所有符号，发现问题时立即失败
** - 相比 RTLD_LAZY，虽然启动稍慢，但避免运行时符号解析错误
*/
static void *ll_load(lua_State *L, const char *path) 
{
    void *lib = dlopen(path, RTLD_NOW);
    
    if (lib == NULL) 
    {
        lua_pushstring(L, dlerror());
    }
    
    return lib;
}

/*
** [动态链接] 查找库符号
**
** 功能描述：
** 使用 dlsym 在已加载的动态库中查找指定符号。
** 返回符号对应的函数指针。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param lib - void*：动态库句柄
** @param sym - const char*：要查找的符号名称
**
** 返回值：
** @return lua_CFunction：成功返回函数指针，失败返回 NULL
**
** 错误处理：
** 符号查找失败时，将错误信息推送到 Lua 栈顶。
**
** 符号查找说明：
** - 查找的是 C 函数符号，需要正确的函数签名
** - 符号名称区分大小写
** - 支持查找全局符号和局部符号
*/
static lua_CFunction ll_sym(lua_State *L, void *lib, const char *sym) 
{
    lua_CFunction f = (lua_CFunction)dlsym(lib, sym);
    
    if (f == NULL) 
    {
        lua_pushstring(L, dlerror());
    }
    
    return f;
}

#elif defined(LUA_DL_DLL)
/*
** ========================================================================
** [平台实现] Windows 系统 - 基于 Win32 API
** ========================================================================
**
** 平台特点：
** Windows 动态链接库（DLL）系统具有以下特点：
** - 基于 PE（Portable Executable）格式
** - 支持导入表和导出表机制
** - 提供丰富的错误信息和调试支持
** - 支持延迟加载和资源管理
**
** 技术原理：
** - LoadLibrary：加载 DLL 到进程地址空间
** - GetProcAddress：获取 DLL 中导出函数的地址
** - FreeLibrary：卸载 DLL，减少引用计数
** - GetLastError/FormatMessage：获取详细的错误信息
**
** Windows 特有功能：
** - 支持获取模块文件名和路径
** - 提供详细的系统错误信息格式化
** - 支持程序目录的动态设置
*/

#include <windows.h>

// 重新定义程序目录设置函数，Windows 平台需要实际实现
#undef setprogdir

/*
** [Windows 特有] 设置程序目录
**
** 功能描述：
** 获取当前可执行文件的目录路径，并替换 Lua 路径中的占位符。
** 这对于相对路径的模块搜索非常重要。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 实现原理：
** 1. 使用 GetModuleFileNameA 获取当前模块的完整路径
** 2. 提取目录部分（去掉文件名）
** 3. 替换 Lua 路径字符串中的 LUA_EXECDIR 占位符
**
** 错误处理：
** 如果无法获取模块文件名，会触发 Lua 错误
*/
static void setprogdir(lua_State *L)
{
    char buff[MAX_PATH + 1];
    char *lb;
    DWORD nsize = sizeof(buff) / sizeof(char);
    DWORD n = GetModuleFileNameA(NULL, buff, nsize);

    if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == NULL)
    {
        luaL_error(L, "unable to get ModuleFileName");
    }
    else
    {
        // 截断路径，只保留目录部分
        *lb = '\0';

        // 替换路径中的占位符
        luaL_gsub(L, lua_tostring(L, -1), LUA_EXECDIR, buff);

        // 移除原始字符串
        lua_remove(L, -2);
    }
}

/*
** [Windows 特有] 推送系统错误信息
**
** 功能描述：
** 获取最后一次 Windows API 调用的错误代码，
** 并将格式化的错误信息推送到 Lua 栈。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 实现原理：
** 1. 使用 GetLastError 获取错误代码
** 2. 使用 FormatMessageA 格式化错误信息
** 3. 如果格式化失败，使用错误代码作为备选信息
**
** 错误信息特点：
** - 提供本地化的错误描述
** - 包含详细的系统级错误信息
** - 便于调试和问题诊断
*/
static void pusherror(lua_State *L)
{
    int error = GetLastError();
    char buffer[128];

    if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
                       NULL, error, 0, buffer, sizeof(buffer), NULL))
    {
        lua_pushstring(L, buffer);
    }
    else
    {
        lua_pushfstring(L, "system error %d\n", error);
    }
}

/*
** [动态链接] 卸载动态库
**
** 功能描述：
** 使用 FreeLibrary 卸载 Windows DLL。
** 减少 DLL 的引用计数，当计数为零时卸载。
**
** 参数说明：
** @param lib - void*：DLL 句柄（HINSTANCE 类型）
**
** 实现说明：
** FreeLibrary 是线程安全的，支持多线程环境。
** 卸载时会调用 DLL 的 DllMain 函数进行清理。
*/
static void ll_unloadlib(void *lib)
{
    FreeLibrary((HINSTANCE)lib);
}

/*
** [动态链接] 加载动态库
**
** 功能描述：
** 使用 LoadLibraryA 加载指定路径的 Windows DLL。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param path - const char*：DLL 文件路径
**
** 返回值：
** @return void*：成功返回 DLL 句柄，失败返回 NULL
**
** 错误处理：
** 加载失败时，调用 pusherror 推送详细的错误信息。
**
** 加载特点：
** - 支持绝对路径和相对路径
** - 自动搜索系统目录和当前目录
** - 支持 DLL 依赖项的自动加载
*/
static void *ll_load(lua_State *L, const char *path)
{
    HINSTANCE lib = LoadLibraryA(path);

    if (lib == NULL)
    {
        pusherror(L);
    }

    return lib;
}

/*
** [动态链接] 查找库符号
**
** 功能描述：
** 使用 GetProcAddress 在已加载的 DLL 中查找导出函数。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param lib - void*：DLL 句柄
** @param sym - const char*：要查找的函数名称
**
** 返回值：
** @return lua_CFunction：成功返回函数指针，失败返回 NULL
**
** 错误处理：
** 符号查找失败时，调用 pusherror 推送错误信息。
**
** 查找特点：
** - 只能查找导出表中的符号
** - 函数名称区分大小写
** - 支持按序号查找（不常用）
*/
static lua_CFunction ll_sym(lua_State *L, void *lib, const char *sym)
{
    lua_CFunction f = (lua_CFunction)GetProcAddress((HINSTANCE)lib, sym);

    if (f == NULL)
    {
        pusherror(L);
    }

    return f;
}

#elif defined(LUA_DL_DYLD)
/*
** ========================================================================
** [平台实现] macOS/Darwin 系统 - 基于 dyld 接口
** ========================================================================
**
** 平台特点：
** macOS 的动态链接器（dyld）具有独特的特点：
** - 基于 Mach-O（Mach Object）文件格式
** - 支持 Bundle 和 Framework 两种动态库形式
** - 提供丰富的链接时和运行时选项
** - 支持延迟绑定和符号预绑定
**
** 技术原理：
** - NSCreateObjectFileImageFromFile：从文件创建对象文件映像
** - NSLinkModule：链接模块到当前进程
** - NSLookupSymbolInModule：在模块中查找符号
** - NSUnLinkModule：卸载模块
**
** macOS 特有功能：
** - 支持私有模块链接
** - 提供详细的链接错误信息
** - 支持符号名称修饰（C 函数前加下划线）
*/

#include <mach-o/dyld.h>

/*
** [macOS 特有] 函数名称前缀调整
**
** 说明：
** macOS 在 C 函数名前自动添加下划线前缀，
** 因此需要调整 Lua 模块打开函数的前缀。
*/
#undef POF
#define POF	"_" LUA_POF

/*
** [macOS 特有] 推送链接错误信息
**
** 功能描述：
** 获取最后一次 dyld 操作的错误信息，
** 并将其推送到 Lua 栈。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 实现原理：
** 使用 NSLinkEditError 获取详细的链接错误信息，
** 包括错误类型、错误号、文件名和错误描述。
*/
static void pusherror(lua_State *L)
{
    const char *err_str;
    const char *err_file;
    NSLinkEditErrors err;
    int err_num;

    NSLinkEditError(&err, &err_num, &err_file, &err_str);
    lua_pushstring(L, err_str);
}

/*
** [macOS 特有] 错误代码转换
**
** 功能描述：
** 将 NSObjectFileImageReturnCode 错误代码转换为
** 可读的错误描述字符串。
**
** 参数说明：
** @param ret - NSObjectFileImageReturnCode：错误代码
**
** 返回值：
** @return const char*：错误描述字符串
**
** 错误类型说明：
** - NSObjectFileImageInappropriateFile：文件不是有效的 Bundle
** - NSObjectFileImageArch：库的 CPU 架构不匹配
** - NSObjectFileImageFormat：文件格式错误
** - NSObjectFileImageAccess：文件访问权限问题
** - NSObjectFileImageFailure：其他加载失败原因
*/
static const char *errorfromcode(NSObjectFileImageReturnCode ret)
{
    switch (ret)
    {
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

/*
** [动态链接] 卸载动态库
**
** 功能描述：
** 使用 NSUnLinkModule 卸载 macOS 动态模块。
** 支持延迟引用重置选项。
**
** 参数说明：
** @param lib - void*：模块句柄（NSModule 类型）
**
** 实现说明：
** 使用 NSUNLINKMODULE_OPTION_RESET_LAZY_REFERENCES 选项
** 确保延迟绑定的符号引用被正确重置。
*/
static void ll_unloadlib(void *lib)
{
    NSUnLinkModule((NSModule)lib, NSUNLINKMODULE_OPTION_RESET_LAZY_REFERENCES);
}

/*
** [动态链接] 加载动态库
**
** 功能描述：
** 使用 dyld 接口加载 macOS 动态库（Bundle）。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param path - const char*：库文件路径
**
** 返回值：
** @return void*：成功返回模块句柄，失败返回 NULL
**
** 加载流程：
** 1. 检查 dyld 是否可用
** 2. 从文件创建对象文件映像
** 3. 链接模块到当前进程
** 4. 清理临时映像资源
**
** 错误处理：
** 各个步骤失败时推送相应的错误信息。
*/
static void *ll_load(lua_State *L, const char *path)
{
    NSObjectFileImage img;
    NSObjectFileImageReturnCode ret;

    // 检查 dyld 是否可用（罕见情况，但防止崩溃）
    if (!_dyld_present())
    {
        lua_pushliteral(L, "dyld not present");
        return NULL;
    }

    // 从文件创建对象文件映像
    ret = NSCreateObjectFileImageFromFile(path, &img);

    if (ret == NSObjectFileImageSuccess)
    {
        // 链接模块，使用私有选项和错误返回选项
        NSModule mod = NSLinkModule(img, path,
                                   NSLINKMODULE_OPTION_PRIVATE |
                                   NSLINKMODULE_OPTION_RETURN_ON_ERROR);

        // 销毁临时映像
        NSDestroyObjectFileImage(img);

        if (mod == NULL)
        {
            pusherror(L);
        }

        return mod;
    }

    // 创建映像失败，推送错误信息
    lua_pushstring(L, errorfromcode(ret));
    return NULL;
}

/*
** [动态链接] 查找库符号
**
** 功能描述：
** 使用 NSLookupSymbolInModule 在已加载的模块中查找符号。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param lib - void*：模块句柄
** @param sym - const char*：要查找的符号名称
**
** 返回值：
** @return lua_CFunction：成功返回函数指针，失败返回 NULL
**
** 查找流程：
** 1. 在模块中查找符号
** 2. 获取符号的实际地址
** 3. 转换为函数指针类型
**
** 错误处理：
** 符号查找失败时推送格式化的错误信息。
*/
static lua_CFunction ll_sym(lua_State *L, void *lib, const char *sym)
{
    NSSymbol nss = NSLookupSymbolInModule((NSModule)lib, sym);

    if (nss == NULL)
    {
        lua_pushfstring(L, "symbol " LUA_QS " not found", sym);
        return NULL;
    }

    return (lua_CFunction)NSAddressOfSymbol(nss);
}

#else
/*
** ========================================================================
** [回退实现] 不支持动态链接的系统
** ========================================================================
**
** 适用场景：
** 某些嵌入式系统、特殊平台或编译配置可能不支持动态链接。
** 此实现提供统一的接口，但所有操作都会失败并返回错误信息。
**
** 设计目的：
** - 保持 API 兼容性
** - 提供清晰的错误信息
** - 避免编译错误
** - 支持静态链接的 Lua 构建
*/

// 重新定义失败类型标识
#undef LIB_FAIL
#define LIB_FAIL	"absent"

// 统一的错误消息
#define DLMSG	"dynamic libraries not enabled; check your Lua installation"

/*
** [回退实现] 卸载动态库（空操作）
**
** 功能描述：
** 在不支持动态链接的系统上，这是一个空操作。
** 参数被标记为未使用，避免编译器警告。
**
** 参数说明：
** @param lib - void*：库句柄（未使用）
*/
static void ll_unloadlib(void *lib)
{
    (void)lib;  // 避免未使用参数的编译器警告
}

/*
** [回退实现] 加载动态库（总是失败）
**
** 功能描述：
** 在不支持动态链接的系统上，总是返回失败。
** 推送统一的错误消息到 Lua 栈。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param path - const char*：库文件路径（未使用）
**
** 返回值：
** @return void*：总是返回 NULL
*/
static void *ll_load(lua_State *L, const char *path)
{
    (void)path;  // 避免未使用参数的编译器警告
    lua_pushliteral(L, DLMSG);
    return NULL;
}

/*
** [回退实现] 查找库符号（总是失败）
**
** 功能描述：
** 在不支持动态链接的系统上，总是返回失败。
** 推送统一的错误消息到 Lua 栈。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param lib - void*：库句柄（未使用）
** @param sym - const char*：符号名称（未使用）
**
** 返回值：
** @return lua_CFunction：总是返回 NULL
*/
static lua_CFunction ll_sym(lua_State *L, void *lib, const char *sym)
{
    (void)lib;   // 避免未使用参数的编译器警告
    (void)sym;   // 避免未使用参数的编译器警告
    lua_pushliteral(L, DLMSG);
    return NULL;
}

#endif

/*
** ========================================================================
** [核心功能] 库注册和管理
** ========================================================================
*/

/*
** [库管理] 注册库句柄
**
** 功能描述：
** 在 Lua 注册表中注册库句柄，实现库的缓存和生命周期管理。
** 避免重复加载同一个库，并确保库的正确卸载。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param path - const char*：库文件路径
**
** 返回值：
** @return void**：指向库句柄的指针
**
** 实现原理：
** 1. 构造注册表键（LIBPREFIX + path）
** 2. 检查注册表中是否已存在该库
** 3. 如果存在，返回现有的句柄指针
** 4. 如果不存在，创建新的用户数据并设置元表
** 5. 将新创建的用户数据存储到注册表中
**
** 内存管理：
** - 使用用户数据存储库句柄，支持垃圾回收
** - 设置 _LOADLIB 元表，提供 __gc 元方法
** - 确保库句柄在 Lua 对象被回收时正确释放
*/
static void **ll_register(lua_State *L, const char *path)
{
    void **plib;

    // 构造注册表键
    lua_pushfstring(L, "%s%s", LIBPREFIX, path);

    // 检查库是否已在注册表中
    lua_gettable(L, LUA_REGISTRYINDEX);

    if (!lua_isnil(L, -1))
    {
        // 库已存在，获取句柄指针
        plib = (void **)lua_touserdata(L, -1);
    }
    else
    {
        // 库不存在，创建新条目
        lua_pop(L, 1);  // 移除 nil 值

        // 创建用户数据存储库句柄
        plib = (void **)lua_newuserdata(L, sizeof(const void *));
        *plib = NULL;  // 初始化为 NULL

        // 设置元表以支持垃圾回收
        luaL_getmetatable(L, "_LOADLIB");
        lua_setmetatable(L, -2);

        // 将用户数据存储到注册表中
        lua_pushfstring(L, "%s%s", LIBPREFIX, path);
        lua_pushvalue(L, -2);
        lua_settable(L, LUA_REGISTRYINDEX);
    }

    return plib;
}

/*
** [垃圾回收] 库句柄的垃圾回收函数
**
** 功能描述：
** 当库句柄用户数据被垃圾回收时自动调用。
** 确保动态库被正确卸载，防止资源泄漏。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是0）
**
** 实现说明：
** 1. 检查库句柄是否有效（非 NULL）
** 2. 如果有效，调用平台相关的卸载函数
** 3. 将句柄标记为已关闭（设为 NULL）
**
** 安全性：
** - 支持重复调用，不会产生副作用
** - 线程安全，可在多线程环境中使用
** - 异常安全，不会抛出异常
*/
static int gctm(lua_State *L)
{
    void **lib = (void **)luaL_checkudata(L, 1, "_LOADLIB");

    if (*lib)
    {
        ll_unloadlib(*lib);
    }

    *lib = NULL;  // 标记库为已关闭
    return 0;
}

/*
** [核心函数] 加载库函数
**
** 功能描述：
** 从指定路径的动态库中加载指定的函数。
** 这是动态库加载的核心实现，处理库的加载、缓存和符号查找。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param path - const char*：动态库文件路径
** @param sym - const char*：要查找的函数符号名称
**
** 返回值：
** @return int：错误代码（0表示成功，ERRLIB表示库加载失败，ERRFUNC表示函数查找失败）
**
** 执行流程：
** 1. 在注册表中查找或创建库句柄条目
** 2. 如果库未加载，调用平台相关的加载函数
** 3. 如果库加载成功，查找指定的函数符号
** 4. 如果函数查找成功，将函数推送到 Lua 栈
**
** 缓存机制：
** - 已加载的库会被缓存，避免重复加载
** - 库句柄通过注册表管理，支持垃圾回收
** - 提高性能，减少系统调用开销
**
** 错误处理：
** - 库加载失败：返回 ERRLIB，错误信息在栈顶
** - 函数查找失败：返回 ERRFUNC，错误信息在栈顶
** - 成功：返回 0，函数对象在栈顶
*/
static int ll_loadfunc(lua_State *L, const char *path, const char *sym)
{
    void **reg = ll_register(L, path);

    // 如果库未加载，尝试加载
    if (*reg == NULL)
    {
        *reg = ll_load(L, path);
    }

    if (*reg == NULL)
    {
        // 库加载失败
        return ERRLIB;
    }
    else
    {
        // 库加载成功，查找函数符号
        lua_CFunction f = ll_sym(L, *reg, sym);

        if (f == NULL)
        {
            // 函数查找失败
            return ERRFUNC;
        }

        // 成功，将函数推送到栈
        lua_pushcfunction(L, f);
        return 0;
    }
}

/*
** [Lua 接口] loadlib 函数
**
** 功能描述：
** Lua 中 package.loadlib 函数的实现。
** 提供从动态库加载函数的 Lua 接口。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量
**
** Lua 调用格式：
** func = package.loadlib(libname, funcname)
**
** 返回值说明：
** - 成功：返回加载的函数
** - 失败：返回 nil, 错误消息, 错误类型
**
** 错误类型：
** - "open"：库文件无法打开或加载
** - "init"：库中找不到指定的函数
**
** 使用示例：
** -- 加载数学库中的 sin 函数
** sin_func = package.loadlib("libm.so", "sin")
** if sin_func then
**     result = sin_func(1.0)
** end
*/
static int ll_loadlib(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);  // 库路径
    const char *init = luaL_checkstring(L, 2);  // 函数名称
    int stat = ll_loadfunc(L, path, init);

    if (stat == 0)
    {
        // 成功，返回加载的函数
        return 1;
    }
    else
    {
        // 失败，返回 nil, 错误消息, 错误类型
        lua_pushnil(L);
        lua_insert(L, -2);  // 将 nil 插入到错误消息前面
        lua_pushstring(L, (stat == ERRLIB) ? LIB_FAIL : "init");
        return 3;
    }
}

/*
** ========================================================================
** [require 系统] 模块加载机制
** ========================================================================
*/

/*
** [工具函数] 检查文件可读性
**
** 功能描述：
** 检查指定文件是否存在且可读。
** 用于模块搜索过程中验证候选文件。
**
** 参数说明：
** @param filename - const char*：要检查的文件名
**
** 返回值：
** @return int：1表示可读，0表示不可读
**
** 实现原理：
** 尝试以只读模式打开文件，如果成功则表示文件可读。
** 立即关闭文件，不进行实际读取操作。
**
** 性能考虑：
** - 使用最小开销的检查方式
** - 避免读取文件内容
** - 快速失败，提高搜索效率
*/
static int readable(const char *filename)
{
    FILE *f = fopen(filename, "r");

    if (f == NULL)
    {
        return 0;  // 打开失败
    }

    fclose(f);
    return 1;  // 文件可读
}

/*
** [路径处理] 获取下一个路径模板
**
** 功能描述：
** 从路径字符串中提取下一个路径模板。
** 路径模板用分隔符分隔，支持多个搜索路径。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param path - const char*：路径字符串
**
** 返回值：
** @return const char*：指向下一个模板的指针，NULL表示没有更多模板
**
** 路径格式：
** 路径字符串由 LUA_PATHSEP（通常是分号）分隔的多个模板组成。
** 每个模板可以包含 LUA_PATH_MARK（通常是问号）作为模块名占位符。
**
** 处理逻辑：
** 1. 跳过开头的分隔符
** 2. 查找下一个分隔符位置
** 3. 提取当前模板并推送到栈
** 4. 返回下一个模板的起始位置
*/
static const char *pushnexttemplate(lua_State *L, const char *path)
{
    const char *l;

    // 跳过开头的分隔符
    while (*path == *LUA_PATHSEP)
    {
        path++;
    }

    if (*path == '\0')
    {
        return NULL;  // 没有更多模板
    }

    // 查找下一个分隔符
    l = strchr(path, *LUA_PATHSEP);
    if (l == NULL)
    {
        l = path + strlen(path);  // 指向字符串末尾
    }

    // 提取当前模板
    lua_pushlstring(L, path, l - path);

    return l;
}

/*
** [文件搜索] 查找模块文件
**
** 功能描述：
** 根据模块名称和搜索路径查找对应的文件。
** 支持路径模板和占位符替换。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param name - const char*：模块名称
** @param pname - const char*：路径变量名称（"path" 或 "cpath"）
**
** 返回值：
** @return const char*：找到的文件名，NULL表示未找到
**
** 搜索流程：
** 1. 将模块名中的点替换为目录分隔符
** 2. 获取指定的搜索路径
** 3. 遍历路径中的每个模板
** 4. 替换模板中的占位符为模块名
** 5. 检查生成的文件名是否可读
** 6. 如果可读，返回文件名；否则继续搜索
**
** 错误累积：
** 搜索过程中会累积错误信息，包含所有尝试过的文件路径。
** 这有助于调试模块加载问题。
*/
static const char *findfile(lua_State *L, const char *name, const char *pname)
{
    const char *path;

    // 将模块名中的点替换为目录分隔符
    name = luaL_gsub(L, name, ".", LUA_DIRSEP);

    // 获取搜索路径
    lua_getfield(L, LUA_ENVIRONINDEX, pname);
    path = lua_tostring(L, -1);

    if (path == NULL)
    {
        luaL_error(L, LUA_QL("package.%s") " must be a string", pname);
    }

    // 初始化错误累积器
    lua_pushliteral(L, "");

    // 遍历路径模板
    while ((path = pushnexttemplate(L, path)) != NULL)
    {
        const char *filename;

        // 替换模板中的占位符
        filename = luaL_gsub(L, lua_tostring(L, -1), LUA_PATH_MARK, name);
        lua_remove(L, -2);  // 移除路径模板

        // 检查文件是否存在且可读
        if (readable(filename))
        {
            return filename;  // 找到文件
        }

        // 添加到错误信息中
        lua_pushfstring(L, "\n\tno file " LUA_QS, filename);
        lua_remove(L, -2);  // 移除文件名
        lua_concat(L, 2);   // 连接错误信息
    }

    return NULL;  // 未找到文件
}

/*
** [错误处理] 加载错误报告
**
** 功能描述：
** 格式化并报告模块加载错误。
** 提供详细的错误信息，包括模块名、文件名和具体错误。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param filename - const char*：尝试加载的文件名
**
** 错误格式：
** "error loading module 'modname' from file 'filename': error_message"
**
** 注意：此函数不会返回，会直接触发 Lua 错误
*/
static void loaderror(lua_State *L, const char *filename)
{
    luaL_error(L, "error loading module " LUA_QS " from file " LUA_QS ":\n\t%s",
               lua_tostring(L, 1), filename, lua_tostring(L, -1));
}

/*
** [加载器] Lua 模块加载器
**
** 功能描述：
** 加载 Lua 源代码模块（.lua 文件）。
** 这是 require 系统中的第一个标准加载器。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量
**
** 加载流程：
** 1. 获取模块名称
** 2. 在 package.path 中搜索对应的 .lua 文件
** 3. 如果找到文件，使用 luaL_loadfile 加载
** 4. 如果加载成功，返回编译后的函数
** 5. 如果加载失败，报告详细错误
**
** 返回值说明：
** - 成功：返回编译后的 Lua 函数
** - 未找到：返回错误信息字符串
** - 加载失败：触发 Lua 错误
*/
static int loader_Lua(lua_State *L)
{
    const char *filename;
    const char *name = luaL_checkstring(L, 1);

    // 在 package.path 中搜索文件
    filename = findfile(L, name, "path");

    if (filename == NULL)
    {
        return 1;  // 库未在此路径中找到
    }

    // 加载 Lua 文件
    if (luaL_loadfile(L, filename) != 0)
    {
        loaderror(L, filename);
    }

    return 1;  // 库加载成功
}

/*
** [工具函数] 构造 C 函数名
**
** 功能描述：
** 根据模块名构造对应的 C 初始化函数名。
** 处理子模块和特殊字符。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param modname - const char*：模块名称
**
** 返回值：
** @return const char*：构造的函数名
**
** 构造规则：
** 1. 如果模块名包含忽略标记，只使用标记后的部分
** 2. 将模块名中的点替换为下划线
** 3. 添加 "luaopen_" 前缀（或平台特定前缀）
**
** 示例：
** - "math" -> "luaopen_math"
** - "socket.core" -> "luaopen_socket_core"
** - "mylib-1.0" -> "luaopen_1_0" (如果 "-" 是忽略标记)
*/
static const char *mkfuncname(lua_State *L, const char *modname)
{
    const char *funcname;
    const char *mark = strchr(modname, *LUA_IGMARK);

    // 如果有忽略标记，只使用标记后的部分
    if (mark)
    {
        modname = mark + 1;
    }

    // 将点替换为下划线
    funcname = luaL_gsub(L, modname, ".", LUA_OFSEP);

    // 添加前缀
    funcname = lua_pushfstring(L, POF"%s", funcname);

    // 移除 gsub 的结果
    lua_remove(L, -2);

    return funcname;
}

/*
** [加载器] C 模块加载器
**
** 功能描述：
** 加载 C 动态库模块（.so/.dll/.dylib 文件）。
** 这是 require 系统中的第二个标准加载器。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量
**
** 加载流程：
** 1. 获取模块名称
** 2. 在 package.cpath 中搜索对应的动态库文件
** 3. 根据模块名构造初始化函数名
** 4. 使用 ll_loadfunc 加载函数
** 5. 如果加载成功，返回初始化函数
**
** 函数命名约定：
** C 模块的初始化函数必须遵循 "luaopen_模块名" 的命名约定。
** 例如：模块 "socket.core" 对应函数 "luaopen_socket_core"。
**
** 返回值说明：
** - 成功：返回 C 初始化函数
** - 未找到：返回错误信息字符串
** - 加载失败：触发 Lua 错误
*/
static int loader_C(lua_State *L)
{
    const char *funcname;
    const char *name = luaL_checkstring(L, 1);

    // 在 package.cpath 中搜索文件
    const char *filename = findfile(L, name, "cpath");

    if (filename == NULL)
    {
        return 1;  // 库未在此路径中找到
    }

    // 构造函数名
    funcname = mkfuncname(L, name);

    // 加载 C 函数
    if (ll_loadfunc(L, filename, funcname) != 0)
    {
        loaderror(L, filename);
    }

    return 1;  // 库加载成功
}

/*
** [加载器] C 根模块加载器
**
** 功能描述：
** 尝试从根模块的动态库中加载子模块。
** 这是 require 系统中的第三个标准加载器。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量
**
** 加载原理：
** 当加载子模块（如 "a.b.c"）时，尝试从根模块（"a"）的动态库中
** 查找对应的初始化函数（"luaopen_a_b_c"）。
**
** 使用场景：
** - 大型 C 库包含多个子模块
** - 避免为每个子模块创建单独的动态库
** - 提高加载效率和减少文件数量
**
** 加载流程：
** 1. 检查模块名是否包含点（是否为子模块）
** 2. 提取根模块名
** 3. 在 package.cpath 中搜索根模块的动态库
** 4. 构造完整的子模块函数名
** 5. 尝试从根模块库中加载子模块函数
**
** 错误处理：
** - 如果是根模块，直接返回（不处理）
** - 如果根模块库不存在，返回错误信息
** - 如果库存在但函数不存在，返回函数未找到信息
** - 如果加载出现其他错误，触发 Lua 错误
*/
static int loader_Croot(lua_State *L)
{
    const char *funcname;
    const char *filename;
    const char *name = luaL_checkstring(L, 1);
    const char *p = strchr(name, '.');
    int stat;

    // 检查是否为子模块
    if (p == NULL)
    {
        return 0;  // 是根模块，不处理
    }

    // 提取根模块名
    lua_pushlstring(L, name, p - name);

    // 搜索根模块的动态库
    filename = findfile(L, lua_tostring(L, -1), "cpath");

    if (filename == NULL)
    {
        return 1;  // 根模块未找到
    }

    // 构造完整的子模块函数名
    funcname = mkfuncname(L, name);

    // 尝试加载子模块函数
    if ((stat = ll_loadfunc(L, filename, funcname)) != 0)
    {
        if (stat != ERRFUNC)
        {
            // 真正的错误（库加载失败）
            loaderror(L, filename);
        }

        // 函数未找到
        lua_pushfstring(L, "\n\tno module " LUA_QS " in file " LUA_QS,
                        name, filename);
        return 1;
    }

    return 1;  // 加载成功
}

/*
** [加载器] 预加载模块加载器
**
** 功能描述：
** 从 package.preload 表中加载预注册的模块。
** 这是 require 系统中的第四个（也是第一个被尝试的）标准加载器。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量
**
** 预加载机制：
** package.preload 是一个表，包含预注册的模块加载函数。
** 这允许在不依赖文件系统的情况下注册模块。
**
** 使用场景：
** - 嵌入式系统中的内置模块
** - 编译时静态链接的模块
** - 需要特殊初始化的模块
** - 测试和调试用的临时模块
**
** 加载流程：
** 1. 获取模块名称
** 2. 检查 package.preload 表是否存在且为表类型
** 3. 在 preload 表中查找对应的加载函数
** 4. 如果找到，返回加载函数；否则返回错误信息
**
** 返回值说明：
** - 成功：返回预注册的加载函数
** - 未找到：返回错误信息字符串
**
** 使用示例：
** package.preload["mymodule"] = function()
**     return { version = "1.0" }
** end
** local m = require("mymodule")  -- 使用预加载的模块
*/
static int loader_preload(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);

    // 获取 package.preload 表
    lua_getfield(L, LUA_ENVIRONINDEX, "preload");

    if (!lua_istable(L, -1))
    {
        luaL_error(L, LUA_QL("package.preload") " must be a table");
    }

    // 在 preload 表中查找模块
    lua_getfield(L, -1, name);

    if (lua_isnil(L, -1))
    {
        // 未找到，返回错误信息
        lua_pushfstring(L, "\n\tno field package.preload['%s']", name);
    }

    return 1;
}

/*
** [常量定义] 循环检测哨兵
**
** 说明：
** 使用静态变量的地址作为唯一标识符，用于检测模块加载循环。
** 这比使用字符串更高效且更安全。
*/
static const int sentinel_ = 0;
#define sentinel	((void *)&sentinel_)

/*
** [核心函数] require 函数实现
**
** 功能描述：
** 实现 Lua 的 require 函数，提供完整的模块加载和缓存机制。
** 这是 Lua 模块系统的核心函数。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 加载流程：
** 1. 检查模块是否已在 _LOADED 表中
** 2. 如果已加载且不是循环标记，直接返回
** 3. 如果是循环标记，报告循环加载错误
** 4. 如果未加载，遍历 package.loaders 中的加载器
** 5. 调用加载器尝试加载模块
** 6. 如果加载成功，执行模块并缓存结果
** 7. 处理模块返回值和默认值
**
** 循环检测：
** 使用哨兵值标记正在加载的模块，防止循环依赖导致的无限递归。
**
** 缓存机制：
** 成功加载的模块会被缓存在 _LOADED 表中，避免重复加载。
**
** 错误处理：
** - 循环依赖：报告循环加载错误
** - 模块未找到：汇总所有加载器的错误信息
** - 加载失败：传播具体的加载错误
**
** 返回值处理：
** - 如果模块返回非 nil 值，使用该值
** - 如果模块返回 nil 或无返回值，使用 true 作为默认值
*/
static int ll_require(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int i;

    // 设置栈，_LOADED 表将在索引 2
    lua_settop(L, 1);

    // 获取 _LOADED 表
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");

    // 检查模块是否已加载
    lua_getfield(L, 2, name);

    if (lua_toboolean(L, -1))
    {
        // 模块已存在，检查是否为循环标记
        if (lua_touserdata(L, -1) == sentinel)
        {
            luaL_error(L, "loop or previous error loading module " LUA_QS, name);
        }

        return 1;  // 模块已加载，直接返回
    }

    // 模块未加载，需要加载它
    // 获取加载器列表
    lua_getfield(L, LUA_ENVIRONINDEX, "loaders");

    if (!lua_istable(L, -1))
    {
        luaL_error(L, LUA_QL("package.loaders") " must be a table");
    }

    // 初始化错误消息累积器
    lua_pushliteral(L, "");

    // 遍历所有加载器
    for (i = 1; ; i++)
    {
        // 获取第 i 个加载器
        lua_rawgeti(L, -2, i);

        if (lua_isnil(L, -1))
        {
            // 没有更多加载器，报告模块未找到错误
            luaL_error(L, "module " LUA_QS " not found:%s",
                       name, lua_tostring(L, -2));
        }

        // 调用加载器
        lua_pushstring(L, name);
        lua_call(L, 1, 1);

        if (lua_isfunction(L, -1))
        {
            // 加载器找到了模块，跳出循环
            break;
        }
        else if (lua_isstring(L, -1))
        {
            // 加载器返回了错误消息，累积它
            lua_concat(L, 2);
        }
        else
        {
            // 加载器返回了其他值，忽略它
            lua_pop(L, 1);
        }
    }

    // 设置循环检测标记
    lua_pushlightuserdata(L, sentinel);
    lua_setfield(L, 2, name);  // _LOADED[name] = sentinel

    // 执行加载的模块
    lua_pushstring(L, name);  // 将模块名作为参数传递
    lua_call(L, 1, 1);        // 调用模块函数

    if (!lua_isnil(L, -1))
    {
        // 模块返回了非 nil 值，使用它
        lua_setfield(L, 2, name);  // _LOADED[name] = 返回值
    }

    // 获取最终的模块值
    lua_getfield(L, 2, name);

    if (lua_touserdata(L, -1) == sentinel)
    {
        // 模块没有设置值，使用 true 作为默认值
        lua_pushboolean(L, 1);
        lua_pushvalue(L, -1);      // 额外的副本用于返回
        lua_setfield(L, 2, name); // _LOADED[name] = true
    }

    return 1;
}

/*
** ========================================================================
** [module 系统] 模块定义机制
** ========================================================================
*/

/*
** [辅助函数] 设置函数环境
**
** 功能描述：
** 将指定的表设置为调用函数的环境。
** 用于 module 函数实现模块的私有环境。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 实现原理：
** 1. 获取调用栈信息，找到调用 module 的函数
** 2. 验证调用者是 Lua 函数（不是 C 函数）
** 3. 将栈顶的表设置为调用函数的环境
**
** 安全检查：
** 只能从 Lua 函数中调用 module，不能从 C 函数中调用。
** 这确保了环境设置的安全性和正确性。
*/
static void setfenv(lua_State *L)
{
    lua_Debug ar;

    // 获取调用栈信息
    if (lua_getstack(L, 1, &ar) == 0 ||
        lua_getinfo(L, "f", &ar) == 0 ||  // 获取调用函数
        lua_iscfunction(L, -1))
    {
        luaL_error(L, LUA_QL("module") " not called from a Lua function");
    }

    // 设置函数环境
    lua_pushvalue(L, -2);  // 复制模块表
    lua_setfenv(L, -2);    // 设置为函数环境
    lua_pop(L, 1);         // 移除函数引用
}

/*
** [辅助函数] 执行模块选项
**
** 功能描述：
** 执行传递给 module 函数的选项函数。
** 选项函数用于自定义模块的行为。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param n - int：参数总数
**
** 执行流程：
** 从第 2 个参数开始，依次调用每个选项函数，
** 将模块表作为参数传递给选项函数。
**
** 常见选项：
** - package.seeall：使模块能够访问全局变量
** - 自定义初始化函数
*/
static void dooptions(lua_State *L, int n)
{
    int i;

    // 从第 2 个参数开始执行选项
    for (i = 2; i <= n; i++)
    {
        lua_pushvalue(L, i);   // 获取选项函数
        lua_pushvalue(L, -2);  // 复制模块表作为参数
        lua_call(L, 1, 0);     // 调用选项函数
    }
}

/*
** [辅助函数] 模块初始化
**
** 功能描述：
** 初始化模块表，设置标准的模块字段。
** 包括 _M、_NAME、_PACKAGE 等标准字段。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param modname - const char*：模块名称
**
** 设置的字段：
** - _M：指向模块表自身的引用
** - _NAME：模块的完整名称
** - _PACKAGE：模块的包名（去掉最后一部分的名称）
**
** 包名计算：
** 对于模块名 "a.b.c"，包名为 "a.b."
** 对于模块名 "simple"，包名为空字符串
*/
static void modinit(lua_State *L, const char *modname)
{
    const char *dot;

    // 设置 _M 字段
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_M");  // module._M = module

    // 设置 _NAME 字段
    lua_pushstring(L, modname);
    lua_setfield(L, -2, "_NAME");

    // 查找最后一个点，计算包名
    dot = strrchr(modname, '.');
    if (dot == NULL)
    {
        dot = modname;
    }
    else
    {
        dot++;
    }

    // 设置 _PACKAGE 字段（包名，包含最后的点）
    lua_pushlstring(L, modname, dot - modname);
    lua_setfield(L, -2, "_PACKAGE");
}

/*
** [核心函数] module 函数实现
**
** 功能描述：
** 实现 Lua 的 module 函数，用于定义模块。
** 创建或获取模块表，设置模块环境，执行初始化选项。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是0）
**
** 执行流程：
** 1. 获取模块名称
** 2. 检查 _LOADED 表中是否已有模块
** 3. 如果没有，尝试从全局表中获取或创建
** 4. 检查模块是否已初始化（有 _NAME 字段）
** 5. 如果未初始化，执行模块初始化
** 6. 设置调用函数的环境为模块表
** 7. 执行传递的选项函数
**
** 模块创建策略：
** - 优先使用 _LOADED 表中的现有模块
** - 其次尝试全局表中的同名表
** - 最后创建新的模块表
**
** 环境设置：
** 将调用 module 的函数的环境设置为模块表，
** 使得函数中定义的所有变量都成为模块的成员。
*/
static int ll_module(lua_State *L)
{
    const char *modname = luaL_checkstring(L, 1);
    int loaded = lua_gettop(L) + 1;  // _LOADED 表的索引

    // 获取 _LOADED 表
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");

    // 检查模块是否已在 _LOADED 中
    lua_getfield(L, loaded, modname);

    if (!lua_istable(L, -1))
    {
        // 模块不在 _LOADED 中，尝试全局变量
        lua_pop(L, 1);  // 移除之前的结果

        // 在全局表中查找或创建模块表
        if (luaL_findtable(L, LUA_GLOBALSINDEX, modname, 1) != NULL)
        {
            return luaL_error(L, "name conflict for module " LUA_QS, modname);
        }

        // 将新表存储到 _LOADED 中
        lua_pushvalue(L, -1);
        lua_setfield(L, loaded, modname);  // _LOADED[modname] = new table
    }

    // 检查模块是否已初始化
    lua_getfield(L, -1, "_NAME");

    if (!lua_isnil(L, -1))
    {
        // 模块已初始化
        lua_pop(L, 1);
    }
    else
    {
        // 模块未初始化，执行初始化
        lua_pop(L, 1);
        modinit(L, modname);
    }

    // 设置调用函数的环境
    lua_pushvalue(L, -1);
    setfenv(L);

    // 执行选项函数
    dooptions(L, loaded - 1);

    return 0;
}

/*
** [工具函数] seeall 函数实现
**
** 功能描述：
** 实现 package.seeall 函数，使模块能够访问全局变量。
** 通过设置模块表的 __index 元方法指向全局表实现。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是0）
**
** 实现原理：
** 1. 检查参数是否为表
** 2. 获取或创建表的元表
** 3. 将全局表设置为元表的 __index 字段
**
** 使用效果：
** 设置后，模块中可以直接访问全局变量，如 print、type 等。
** 这是 module 函数的常用选项之一。
**
** 使用示例：
** module("mymodule", package.seeall)
** -- 现在可以在模块中直接使用 print 等全局函数
*/
static int ll_seeall(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    // 获取或创建元表
    if (!lua_getmetatable(L, 1))
    {
        // 创建新的元表
        lua_createtable(L, 0, 1);
        lua_pushvalue(L, -1);
        lua_setmetatable(L, 1);
    }

    // 设置 __index 为全局表
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfield(L, -2, "__index");  // mt.__index = _G

    return 0;
}

/*
** [辅助常量] 路径处理辅助标记
**
** 说明：
** 用于路径字符串处理的内部标记，不应在外部使用。
*/
#define AUXMARK		"\1"

/*
** [辅助函数] 设置搜索路径
**
** 功能描述：
** 设置 package.path 或 package.cpath 的值。
** 支持环境变量和默认值的组合。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param fieldname - const char*：字段名（"path" 或 "cpath"）
** @param envname - const char*：环境变量名
** @param def - const char*：默认路径值
**
** 路径处理规则：
** 1. 如果环境变量存在，使用环境变量的值
** 2. 将路径中的 ";;" 替换为 ";默认路径;"
** 3. 调用 setprogdir 处理程序目录占位符
** 4. 将最终路径设置到指定字段
**
** 环境变量支持：
** - LUA_PATH：Lua 模块搜索路径
** - LUA_CPATH：C 模块搜索路径
**
** 默认值合并：
** 支持在环境变量中使用 ";;" 来包含默认路径。
** 例如：LUA_PATH="/my/path/?.lua;;" 会展开为
** "/my/path/?.lua;默认路径"
*/
static void setpath(lua_State *L, const char *fieldname, const char *envname,
                    const char *def)
{
    const char *path = getenv(envname);

    if (path == NULL)
    {
        // 没有环境变量，使用默认值
        lua_pushstring(L, def);
    }
    else
    {
        // 处理环境变量中的 ";;" 占位符
        // 将 ";;" 替换为 ";AUXMARK;"
        path = luaL_gsub(L, path, LUA_PATHSEP LUA_PATHSEP,
                         LUA_PATHSEP AUXMARK LUA_PATHSEP);

        // 将 AUXMARK 替换为默认路径
        luaL_gsub(L, path, AUXMARK, def);
        lua_remove(L, -2);  // 移除中间结果
    }

    // 处理程序目录占位符
    setprogdir(L);

    // 设置到指定字段
    lua_setfield(L, -2, fieldname);
}

/*
** [数据结构] package 库函数注册表
**
** 包含 package 库的公开函数：
** - loadlib：动态库加载函数
** - seeall：模块全局访问函数
*/
static const luaL_Reg pk_funcs[] =
{
    {"loadlib", ll_loadlib},
    {"seeall", ll_seeall},
    {NULL, NULL}
};

/*
** [数据结构] 全局函数注册表
**
** 包含注册到全局环境的函数：
** - module：模块定义函数
** - require：模块加载函数
*/
static const luaL_Reg ll_funcs[] =
{
    {"module", ll_module},
    {"require", ll_require},
    {NULL, NULL}
};

/*
** [数据结构] 标准加载器数组
**
** 定义了 require 系统使用的标准加载器，按优先级排序：
** 1. loader_preload：预加载模块加载器
** 2. loader_Lua：Lua 源文件加载器
** 3. loader_C：C 动态库加载器
** 4. loader_Croot：C 根模块加载器
*/
static const lua_CFunction loaders[] =
{
    loader_preload,
    loader_Lua,
    loader_C,
    loader_Croot,
    NULL
};

/*
** [核心] Lua 包管理库初始化函数
**
** 功能描述：
** 初始化 Lua 的包管理系统，设置所有必要的表、函数和配置。
** 这是包管理库的入口点，由 Lua 解释器在加载库时调用。
**
** 详细初始化流程：
** 1. 创建 _LOADLIB 元表，用于动态库句柄的垃圾回收
** 2. 注册 package 库的函数（loadlib、seeall）
** 3. 设置向后兼容性支持（如果启用）
** 4. 创建并初始化 loaders 表，包含所有标准加载器
** 5. 设置搜索路径（path 和 cpath）
** 6. 配置系统信息（config 字段）
** 7. 初始化 loaded 和 preload 表
** 8. 注册全局函数（module 和 require）
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1，表示 package 库表）
**
** 创建的表和字段：
** - package.loaders：加载器函数数组
** - package.path：Lua 模块搜索路径
** - package.cpath：C 模块搜索路径
** - package.config：系统配置信息
** - package.loaded：已加载模块缓存
** - package.preload：预加载模块表
**
** 全局函数：
** - require：模块加载函数
** - module：模块定义函数
**
** 兼容性处理：
** 如果定义了 LUA_COMPAT_LOADLIB，会将 loadlib 函数也注册为全局函数。
**
** 配置信息格式：
** config 字段包含系统相关的配置信息，用换行符分隔：
** - 目录分隔符（通常是 / 或 \）
** - 路径分隔符（通常是 ; 或 :）
** - 路径占位符（通常是 ?）
** - 可执行目录占位符
** - 忽略标记
**
** 算法复杂度：O(n) 时间，其中 n 是加载器数量，O(1) 空间
*/
LUALIB_API int luaopen_package(lua_State *L)
{
    int i;

    // 步骤1：创建 _LOADLIB 元表用于动态库句柄管理
    luaL_newmetatable(L, "_LOADLIB");
    lua_pushcfunction(L, gctm);
    lua_setfield(L, -2, "__gc");

    // 步骤2：创建并注册 package 表
    luaL_register(L, LUA_LOADLIBNAME, pk_funcs);

    // 步骤3：向后兼容性处理
#if defined(LUA_COMPAT_LOADLIB)
    // 将 loadlib 函数也注册为全局函数
    lua_getfield(L, -1, "loadlib");
    lua_setfield(L, LUA_GLOBALSINDEX, "loadlib");
#endif

    // 步骤4：设置 package 表为环境表
    lua_pushvalue(L, -1);
    lua_replace(L, LUA_ENVIRONINDEX);

    // 步骤5：创建并初始化 loaders 表
    lua_createtable(L, sizeof(loaders)/sizeof(loaders[0]) - 1, 0);

    // 填充预定义的加载器
    for (i = 0; loaders[i] != NULL; i++)
    {
        lua_pushcfunction(L, loaders[i]);
        lua_rawseti(L, -2, i + 1);
    }

    // 将 loaders 表设置到 package.loaders
    lua_setfield(L, -2, "loaders");

    // 步骤6：设置搜索路径
    setpath(L, "path", LUA_PATH, LUA_PATH_DEFAULT);    // Lua 模块路径
    setpath(L, "cpath", LUA_CPATH, LUA_CPATH_DEFAULT); // C 模块路径

    // 步骤7：存储配置信息
    lua_pushliteral(L, LUA_DIRSEP "\n" LUA_PATHSEP "\n" LUA_PATH_MARK "\n"
                       LUA_EXECDIR "\n" LUA_IGMARK);
    lua_setfield(L, -2, "config");

    // 步骤8：设置 loaded 表
    luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 2);
    lua_setfield(L, -2, "loaded");

    // 步骤9：设置 preload 表
    lua_newtable(L);
    lua_setfield(L, -2, "preload");

    // 步骤10：注册全局函数
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    luaL_register(L, NULL, ll_funcs);  // 注册 module 和 require
    lua_pop(L, 1);  // 移除全局表引用

    // 返回 package 表
    return 1;
}
