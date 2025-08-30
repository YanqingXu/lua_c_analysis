/*
** [核心] Lua 标准库头文件
**
** 功能概述：
** 本头文件定义了 Lua 标准库的公共接口，包含所有标准库的初始化函数声明、
** 库名称常量定义、以及库间依赖关系的管理。这是 Lua 标准库系统的核心接口文件。
**
** 主要功能模块：
** - 库名称定义：定义所有标准库的名称常量
** - 初始化函数声明：声明各个库的初始化函数
** - 统一初始化接口：提供一次性初始化所有库的函数
** - 调试支持：定义调试相关的宏和接口
** - 文件句柄类型：定义文件操作相关的类型标识
**
** 库接口设计原理：
** Lua 标准库采用模块化设计，每个库都是独立的模块：
** - 独立初始化：每个库都有自己的初始化函数
** - 按需加载：可以选择性地加载需要的库
** - 统一接口：所有库都遵循相同的初始化接口规范
** - 依赖管理：明确定义库之间的依赖关系
**
** 模块间依赖关系：
** - 基础库（base）：核心功能，其他库的基础
** - 表库（table）：依赖基础库，提供表操作
** - 字符串库（string）：依赖基础库，提供字符串操作
** - 数学库（math）：独立模块，提供数学计算
** - I/O库（io）：依赖操作系统库，提供文件操作
** - 操作系统库（os）：系统接口，提供系统调用
** - 包管理库（package）：模块加载系统
** - 调试库（debug）：调试和反射功能
**
** 编译时配置：
** - LUALIB_API：库函数的导出声明修饰符
** - lua_assert：调试断言宏，可在编译时配置
** - 条件编译：支持选择性编译特定库
**
** 接口规范：
** - 所有库初始化函数都返回 int 类型
** - 所有库初始化函数都接受 lua_State* 参数
** - 库名称使用统一的命名约定
** - 使用 LUALIB_API 进行函数导出声明
**
** 版本信息：$Id: lualib.h,v 1.36.1.1 2007/12/27 13:02:25 roberto Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

#ifndef lualib_h
#define lualib_h

// Lua 核心头文件依赖
#include "lua.h"

/*
** ========================================================================
** [类型定义] 标准库类型标识符
** ========================================================================
*/

/*
** [文件句柄] 文件句柄类型标识符
**
** 功能说明：
** 定义文件句柄在 Lua 中的类型标识符，用于类型检查和元表设置。
**
** 使用场景：
** - I/O 库中文件对象的类型标识
** - 用户数据的类型检查
** - 元表的设置和获取
** - 垃圾回收器的类型识别
**
** 实现原理：
** 使用字符串常量作为类型标识符，确保类型的唯一性和可读性。
** 在 I/O 库中，所有文件对象都使用此标识符进行类型标记。
**
** 内存管理：
** 此标识符用于垃圾回收器识别文件句柄对象，
** 确保文件在对象被回收时正确关闭。
*/
#define LUA_FILEHANDLE		"FILE*"

/*
** ========================================================================
** [库声明] 标准库初始化函数声明
** ========================================================================
**
** 库初始化系统概述：
** Lua 标准库采用统一的初始化接口设计，每个库都提供一个初始化函数。
** 这些函数负责创建库表、注册库函数、设置元表等初始化工作。
**
** 初始化函数规范：
** - 函数名：luaopen_<库名>
** - 参数：lua_State *L（Lua 状态机指针）
** - 返回值：int（通常返回1，表示库表在栈顶）
** - 副作用：在全局环境中注册库，设置相关元表
**
** 调用时机：
** - 程序启动时：通过 luaL_openlibs 一次性初始化所有库
** - 按需加载：通过 require 机制动态加载特定库
** - 手动初始化：在 C 代码中直接调用初始化函数
*/

/*
** [基础库] 协程库声明
**
** 库名称：coroutine
** 功能概述：提供协程（coroutine）的创建、控制和管理功能
**
** 主要功能：
** - 协程创建：coroutine.create
** - 协程恢复：coroutine.resume
** - 协程挂起：coroutine.yield
** - 协程状态：coroutine.status
** - 协程包装：coroutine.wrap
**
** 依赖关系：依赖 Lua 核心的协程实现
** 初始化顺序：通常与基础库一起初始化
*/
#define LUA_COLIBNAME	"coroutine"
LUALIB_API int (luaopen_base) (lua_State *L);

/*
** [表库] 表操作库声明
**
** 库名称：table
** 功能概述：提供表的操作、遍历、排序等功能
**
** 主要功能：
** - 表遍历：table.foreach, table.foreachi
** - 数组操作：table.insert, table.remove
** - 字符串连接：table.concat
** - 表排序：table.sort
** - 表属性：table.getn, table.maxn
**
** 依赖关系：依赖基础库的表实现
** 性能特点：提供高效的表操作算法
*/
#define LUA_TABLIBNAME	"table"
LUALIB_API int (luaopen_table) (lua_State *L);

/*
** [I/O库] 输入输出库声明
**
** 库名称：io
** 功能概述：提供文件和标准输入输出的操作功能
**
** 主要功能：
** - 文件操作：io.open, io.close, io.read, io.write
** - 标准流：io.stdin, io.stdout, io.stderr
** - 文件模式：读取、写入、追加等模式
** - 缓冲控制：io.flush, io.setvbuf
**
** 依赖关系：依赖操作系统的文件系统接口
** 平台兼容性：跨平台的文件操作抽象
*/
#define LUA_IOLIBNAME	"io"
LUALIB_API int (luaopen_io) (lua_State *L);

/*
** [操作系统库] 系统调用库声明
**
** 库名称：os
** 功能概述：提供操作系统相关的功能接口
**
** 主要功能：
** - 时间操作：os.time, os.date, os.clock
** - 文件操作：os.remove, os.rename, os.tmpname
** - 系统调用：os.execute, os.exit
** - 环境变量：os.getenv
** - 本地化：os.setlocale
**
** 依赖关系：依赖 C 标准库和操作系统 API
** 平台差异：某些功能在不同平台上行为可能不同
*/
#define LUA_OSLIBNAME	"os"
LUALIB_API int (luaopen_os) (lua_State *L);

/*
** [字符串库] 字符串处理库声明
**
** 库名称：string
** 功能概述：提供字符串操作和模式匹配功能
**
** 主要功能：
** - 基本操作：string.len, string.sub, string.rep
** - 大小写转换：string.upper, string.lower
** - 字符编码：string.byte, string.char
** - 模式匹配：string.find, string.match, string.gsub
** - 字符串格式化：string.format
**
** 依赖关系：依赖基础库的字符串实现
** 特殊功能：强大的模式匹配引擎
*/
#define LUA_STRLIBNAME	"string"
LUALIB_API int (luaopen_string) (lua_State *L);

/*
** [数学库] 数学计算库声明
**
** 库名称：math
** 功能概述：提供数学计算和数学常量
**
** 主要功能：
** - 基本运算：math.abs, math.min, math.max
** - 三角函数：math.sin, math.cos, math.tan
** - 指数对数：math.exp, math.log, math.pow
** - 取整函数：math.floor, math.ceil
** - 随机数：math.random, math.randomseed
** - 数学常量：math.pi, math.huge
**
** 依赖关系：依赖 C 标准库的数学函数
** 精度特点：提供双精度浮点数计算
*/
#define LUA_MATHLIBNAME	"math"
LUALIB_API int (luaopen_math) (lua_State *L);

/*
** [调试库] 调试和反射库声明
**
** 库名称：debug
** 功能概述：提供调试、反射和元编程功能
**
** 主要功能：
** - 调用栈：debug.traceback, debug.getinfo
** - 变量访问：debug.getlocal, debug.setlocal
** - 上值操作：debug.getupvalue, debug.setupvalue
** - 元表操作：debug.getmetatable, debug.setmetatable
** - 钩子函数：debug.sethook, debug.gethook
**
** 依赖关系：依赖 Lua 核心的调试接口
** 安全考虑：提供强大但潜在危险的反射功能
*/
#define LUA_DBLIBNAME	"debug"
LUALIB_API int (luaopen_debug) (lua_State *L);

/*
** [包管理库] 模块加载库声明
**
** 库名称：package
** 功能概述：提供模块加载和包管理功能
**
** 主要功能：
** - 模块加载：require 机制的实现
** - 路径管理：package.path, package.cpath
** - 加载器：package.loaders
** - 已加载模块：package.loaded
** - 预加载：package.preload
** - 动态库加载：package.loadlib
**
** 依赖关系：依赖动态链接库和文件系统
** 核心地位：是 Lua 模块系统的基础
*/
#define LUA_LOADLIBNAME	"package"
LUALIB_API int (luaopen_package) (lua_State *L);

/*
** ========================================================================
** [库初始化] 统一库初始化接口
** ========================================================================
*/

/*
** [统一初始化] 打开所有标准库
**
** 功能描述：
** 一次性初始化所有 Lua 标准库，这是最常用的库初始化方式。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return void：无返回值
**
** 初始化顺序：
** 1. 基础库（base）- 核心功能和协程
** 2. 包管理库（package）- 模块加载系统
** 3. 表库（table）- 表操作功能
** 4. I/O库（io）- 文件和输入输出
** 5. 操作系统库（os）- 系统调用
** 6. 字符串库（string）- 字符串处理
** 7. 数学库（math）- 数学计算
** 8. 调试库（debug）- 调试和反射
**
** 依赖关系处理：
** 按照依赖关系的拓扑顺序进行初始化，确保被依赖的库先于依赖它的库初始化。
**
** 内存管理：
** 所有库的初始化都在同一个 Lua 状态机中进行，共享内存管理和垃圾回收。
**
** 错误处理：
** 如果任何库的初始化失败，整个初始化过程会中断并报告错误。
**
** 使用场景：
** - 标准 Lua 解释器启动时
** - 嵌入式应用需要完整 Lua 功能时
** - 快速原型开发和脚本执行
**
** 性能考虑：
** 一次性初始化所有库会增加启动时间和内存占用，
** 对于资源受限的环境，可以考虑按需初始化特定库。
**
** 使用示例：
** lua_State *L = luaL_newstate();
** luaL_openlibs(L);  // 初始化所有标准库
** // 现在可以使用所有标准库功能
*/
LUALIB_API void (luaL_openlibs) (lua_State *L);

/*
** ========================================================================
** [调试支持] 调试相关宏定义
** ========================================================================
*/

/*
** [调试断言] 条件断言宏
**
** 功能说明：
** 提供调试时的条件断言功能，用于验证程序的正确性假设。
**
** 编译时配置：
** - 调试模式：断言被启用，条件为假时触发错误
** - 发布模式：断言被禁用，编译为空操作
**
** 使用原则：
** - 用于验证不应该发生的条件
** - 用于检查函数参数的有效性
** - 用于验证内部状态的一致性
** - 不应该有副作用（因为可能被禁用）
**
** 性能影响：
** 在发布版本中，断言被编译为空操作，不会影响性能。
** 在调试版本中，断言会增加少量的运行时开销。
**
** 自定义实现：
** 如果需要自定义断言行为，可以在包含此头文件之前定义 lua_assert 宏。
**
** 使用示例：
** lua_assert(L != NULL);           // 验证状态机指针有效
** lua_assert(index > 0);           // 验证索引为正数
** lua_assert(lua_gettop(L) >= 2);  // 验证栈中有足够元素
*/
#ifndef lua_assert
#define lua_assert(x)	((void)0)
#endif

#endif
