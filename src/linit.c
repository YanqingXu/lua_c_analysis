/*
** [核心] Lua 标准库初始化模块
**
** 功能概述：
** 本模块负责 Lua 解释器标准库的初始化工作，提供了统一的库加载接口。
** 通过预定义的库注册表，批量加载所有 Lua 标准库模块。
**
** 主要功能：
** - 定义标准库注册表，包含所有内置库的名称和初始化函数
** - 提供 luaL_openlibs 函数，一次性加载所有标准库
** - 确保库的加载顺序和依赖关系正确处理
**
** 设计思路：
** 采用函数指针数组的方式管理库初始化函数，便于扩展和维护。
** 每个库都有对应的名称和初始化函数，通过统一的循环机制进行加载。
**
** 版本信息：$Id: linit.c,v 1.14.1.1 2007/12/27 13:02:25 roberto Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

// 模块标识定义
#define linit_c
#define LUA_LIB

// 核心头文件包含
#include "lua.h"

// 标准库相关头文件
#include "lualib.h"
#include "lauxlib.h"

/*
** [数据结构] Lua 标准库注册表
**
** 数据结构说明：
** 这是一个静态常量数组，包含了所有 Lua 标准库的注册信息。
** 每个元素都是 luaL_Reg 结构体，包含库名称和对应的初始化函数。
**
** 数组元素说明：
** - {"", luaopen_base}：基础库，提供核心函数（print、type等）
** - {LUA_LOADLIBNAME, luaopen_package}：包管理库，处理模块加载
** - {LUA_TABLIBNAME, luaopen_table}：表操作库，提供表相关函数
** - {LUA_IOLIBNAME, luaopen_io}：输入输出库，文件和IO操作
** - {LUA_OSLIBNAME, luaopen_os}：操作系统库，系统调用接口
** - {LUA_STRLIBNAME, luaopen_string}：字符串库，字符串处理函数
** - {LUA_MATHLIBNAME, luaopen_math}：数学库，数学计算函数
** - {LUA_DBLIBNAME, luaopen_debug}：调试库，调试和反射功能
** - {NULL, NULL}：数组结束标记
**
** 注意事项：
** - 数组必须以 {NULL, NULL} 结尾，作为遍历终止条件
** - 库的加载顺序可能影响某些功能的可用性
** - 基础库使用空字符串作为名称，表示全局可用
*/
static const luaL_Reg lualibs[] =
{
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

/*
** [核心] 打开所有 Lua 标准库
**
** 功能描述：
** 实现一次性加载所有 Lua 标准库的功能。遍历预定义的库注册表，
** 依次调用每个库的初始化函数，将库函数注册到 Lua 状态机中。
**
** 详细功能说明：
** - 遍历 lualibs 数组中的所有库注册信息
** - 为每个库创建对应的 C 函数闭包
** - 将库名称作为参数传递给初始化函数
** - 调用库的初始化函数完成注册过程
**
** 参数说明：
** @param L - lua_State*：目标 Lua 状态机指针，库将被注册到此状态机中
**
** 返回值：
** @return void：无返回值，通过修改 Lua 状态机完成库注册
**
** 执行流程：
** 1. 获取库注册表的起始指针
** 2. 循环遍历直到遇到 NULL 终止符
** 3. 对每个库执行以下操作：
**    a) 将库的初始化函数压入栈顶
**    b) 将库名称字符串压入栈顶
**    c) 调用初始化函数（1个参数，0个返回值）
**
** 算法复杂度：O(n) 时间，其中 n 是标准库的数量
**
** 注意事项：
** - 调用前确保 Lua 状态机已正确初始化
** - 库的加载可能会修改全局环境
** - 某些库之间可能存在依赖关系
** - 加载过程中可能触发内存分配
*/
LUALIB_API void luaL_openlibs(lua_State *L)
{
    // 获取库注册表的起始指针
    const luaL_Reg *lib = lualibs;

    // 遍历所有库注册信息，直到遇到终止标记
    for (; lib->func; lib++)
    {
        // 将库的初始化函数压入栈顶
        lua_pushcfunction(L, lib->func);

        // 将库名称字符串压入栈顶作为参数
        lua_pushstring(L, lib->name);

        // 调用库初始化函数：1个参数（库名），0个返回值
        lua_call(L, 1, 0);
    }
}
