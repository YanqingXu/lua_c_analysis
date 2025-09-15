/**
 * @file linit.c
 * @brief Lua标准库初始化：统一管理所有标准库的加载
 * 
 * 版权信息：
 * $Id: linit.c,v 1.14.1.1 2007/12/27 13:02:25 roberto Exp $
 * lua.c的库初始化模块
 * 版权声明见lua.h文件
 * 
 * 程序概述：
 * 本文件是Lua标准库的统一初始化模块，提供了一站式的标准库加载功能。
 * 通过调用luaL_openlibs()一个函数，就可以加载所有的Lua标准库，
 * 包括基础库、字符串库、表库、I/O库、数学库等。
 * 
 * 系统架构定位：
 * 作为Lua解释器的标准库加载器，本模块位于解释器初始化层，
 * 为上层Lua脚本提供完整的标准库环境。所有标准库都通过
 * 统一的接口进行加载，保证了加载顺序和依赖关系的正确性。
 * 
 * 核心功能：
 * 1. **库注册表管理**: 统一管理所有标准库的名称和加载函数
 * 2. **批量加载机制**: 提供一键加载所有标准库的功能
 * 3. **加载顺序控制**: 确保库的加载顺序满足依赖关系
 * 4. **错误处理**: 安全地处理库加载过程中的错误
 * 
 * 包含的标准库：
 * - 基础库 (base): 核心函数和基本操作
 * - 包管理库 (package): 模块加载和管理
 * - 表库 (table): 表操作和算法
 * - I/O库 (io): 文件和输入输出操作
 * - 操作系统库 (os): 系统相关功能
 * - 字符串库 (string): 字符串处理和模式匹配
 * - 数学库 (math): 数学函数和常量
 * - 调试库 (debug): 调试和反射功能
 * 
 * 技术特点：
 * - 表驱动设计：使用统一的注册表结构
 * - 自动化加载：递归遍历注册表自动加载
 * - 安全性：每个库都独立加载，互不影响
 * - 可扩展性：易于添加新的标准库
 * 
 * 使用场景：
 * - Lua解释器启动初始化
 * - 嵌入式应用的Lua环境初始化
 * - 沙箱环境的标准库配置
 * - 清理环境的Lua运行时搭建
 * 
 * 性能考虑：
 * - 初始化开销低：仅在程序启动时执行一次
 * - 内存使用效率：静态表结构，无额外内存开销
 * - 加载速度快：统一的加载机制避免重复初始化
 * 
 * @author Roberto Ierusalimschy
 * @version 1.14.1.1
 * @date 2007-12-27
 * 
 * @see lua.h Lua核心API定义
 * @see lualib.h Lua标准库声明
 * @see lauxlib.h Lua辅助库接口
 * 
 * @note 本模块是Lua解释器的标准组件，遵循MIT许可证
 */

#define linit_c
#define LUA_LIB

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"


/**
 * @brief Lua标准库注册表：定义所有标准库的名称和加载函数
 * 
 * 详细说明：
 * 这个静态数组定义了Lua解释器的所有标准库及其初始化函数。每个条目
 * 包含库名称和对应的加载函数指针。这种表驱动的设计使得添加新库
 * 或修改现有库变得非常简单。
 * 
 * 库加载顺序（重要）：
 * 1. **基础库 ("")**: 必须首先加载，提供基本函数和全局变量
 * 2. **包管理库 (package)**: 提供模块加载机制，其他库可能依赖
 * 3. **表库 (table)**: 表操作函数，广泛被其他库使用
 * 4. **I/O库 (io)**: 文件和输入输出功能
 * 5. **操作系统库 (os)**: 系统相关功能
 * 6. **字符串库 (string)**: 字符串处理函数
 * 7. **数学库 (math)**: 数学计算函数
 * 8. **调试库 (debug)**: 调试和反射功能（可选）
 * 
 * 特殊设计：
 * - 基础库使用空字符串作为名称，表示不创建命名空间
 * - 每个库都有对应的luaopen_*函数
 * - 数组以{NULL, NULL}结尾，用于循环终止条件
 * 
 * 内存和性能：
 * - 静态存储，无额外内存开销
 * - 编译时就确定，运行时效率高
 * - 简单的线性遍历，复杂度O(n)
 * 
 * 安全性考虑：
 * - 所有函数指针都是静态编译时确定的
 * - 不存在函数指针被修改的风险
 * - 每个库的加载都是独立的，不会相互影响
 * 
 * 扩展方法：
 * 如果需要添加新的标准库，只需：
 * 1. 在数组中添加新条目
 * 2. 确保新库有对应的luaopen_*函数
 * 3. 考虑加载顺序和依赖关系
 * 
 * @see luaL_Reg 库注册结构定义
 * @see luaopen_base() 基础库初始化函数
 * @see luaopen_package() 包管理库初始化函数
 * @see LUA_*LIBNAME 各库名称常量定义
 * 
 * @note 修改此表需要谨慎，可能影响整个Lua环境
 */
static const luaL_Reg lualibs[] = {
    {"", luaopen_base},
    {LUA_LOADLIBNAME, luaopen_package},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_IOLIBNAME, luaopen_io},
    {LUA_OSLIBNAME, luaopen_os},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_DBLIBNAME, luaopen_debug},
    {NULL, NULL}
};


/**
 * @brief Lua标准库批量加载函数：一键初始化所有标准库
 * 
 * 详细说明：
 * 这是Lua标准库的主要入口点，提供了一键加载所有标准库的功能。
 * 该函数遍历lualibs数组，逐个调用每个标准库的初始化函数，
 * 确保所有常用的Lua功能都可用。
 * 
 * 实现机制：
 * 1. 获取lualibs数组的起始指针
 * 2. 循环遍历数组直到遇到NULL终止条件
 * 3. 对每个库执行以下操作：
 *    a. 将库的加载函数推入栈中作为C函数
 *    b. 将库名称推入栈中作为参数
 *    c. 调用库的加载函数（1个参数，0个返回值）
 * 4. 重复直到所有库都加载完成
 * 
 * 加载结果：
 * 加载后，Lua环境将包含以下全局表和函数：
 * - 基础函数：print, type, pairs, ipairs, next, tostring等
 * - package表：require, module, package.path等
 * - table表：table.insert, table.remove, table.sort等
 * - io表：io.open, io.read, io.write等
 * - os表：os.time, os.date, os.execute等
 * - string表：string.find, string.sub, string.gsub等
 * - math表：math.sin, math.cos, math.pi等
 * - debug表：debug.getinfo, debug.traceback等
 * 
 * 使用场景：
 * - Lua解释器启动时的环境初始化
 * - 嵌入式应用中初始化Lua环境
 * - 需要完整Lua功能的应用程序
 * - 脚本执行环境的搭建
 * 
 * 性能特点：
 * - 初始化开销：仅在程序启动时执行一次
 * - 内存使用：每个库都会占用一定的内存空间
 * - 加载时间：与库的数量和复杂度成正比
 * - 无运行时开销：加载后不影响程序性能
 * 
 * 错误处理：
 * - 如果某个库加载失败，会抛出Lua错误
 * - 加载错误会中断整个初始化过程
 * - 部分加载的库可能已经可用，部分不可用
 * 
 * 替代方案：
 * 如果不需要所有标准库，可以：
 * - 单独加载需要的库：luaopen_base(L), luaopen_string(L)等
 * - 修改lualibs数组，移除不需要的库
 * - 使用条件编译控制库的包含
 * 
 * 安全性考虑：
 * - 调试库在生产环境中可能存在安全风险
 * - os库可能被恶意使用执行系统命令
 * - io库可能被用于读写敏感文件
 * - 在沙箱环境中应谨慎加载或禁用某些库
 * 
 * @param L Lua状态机指针，必须是有效的已初始化状态
 * 
 * @return void 无返回值，但会修改Lua状态机的全局环境
 * 
 * @see lualibs 标准库注册表
 * @see lua_pushcfunction() C函数推入栈
 * @see lua_pushstring() 字符串推入栈
 * @see lua_call() Lua函数调用
 * @see luaopen_base() 等各库初始化函数
 * 
 * @note 这是初始化Lua环境的标准做法，广泛被使用
 * 
 * @warning 只应在程序初始化阶段调用一次，重复调用可能导致问题
 */
LUALIB_API void luaL_openlibs(lua_State *L) {
    const luaL_Reg *lib = lualibs;
    for (; lib->func; lib++) {
        lua_pushcfunction(L, lib->func);
        lua_pushstring(L, lib->name);
        lua_call(L, 1, 0);
    }
}

