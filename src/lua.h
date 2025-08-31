/*
** ============================================================================
** [核心] Lua 编程语言核心 API 头文件 (Lua Core API Header)
** ============================================================================
**
** 文件功能：
** 本文件是 Lua 编程语言的核心 API 定义头文件，提供了 C 语言与 Lua 虚拟机
** 交互的完整接口。定义了所有公开的数据类型、函数声明和常量，是使用 Lua
** C API 进行开发的基础依赖文件。
**
** [核心] API 功能模块：
** 1. [状态] Lua 状态机的创建、管理和销毁
** 2. [栈操作] 虚拟栈的基础操作和值传递
** 3. [类型系统] Lua 数据类型的检查和转换
** 4. [函数调用] Lua 函数的调用和 C 函数的注册
** 5. [表操作] Lua 表的创建、访问和修改
** 6. [协程] 协程的创建、恢复和挂起
** 7. [垃圾回收] 垃圾回收器的控制和配置
** 8. [调试] 调试钩子和运行时信息获取
**
** [设计] 架构特点：
** - 基于栈的数据交换机制，简化 C 与 Lua 的互操作
** - 类型安全的 API 设计，提供完整的类型检查机制
** - 异常安全的错误处理，支持保护调用和错误恢复
** - 内存安全的资源管理，自动垃圾回收和引用计数
** - 可嵌入的轻量级设计，最小化对宿主程序的影响
** - 跨平台的接口标准，支持多种操作系统和编译器
**
** [交互] C/Lua 交互模式：
** 1. [栈机制] 所有数据通过虚拟栈在 C 和 Lua 之间传递
** 2. [类型映射] C 基础类型与 Lua 类型的双向转换
** 3. [函数绑定] C 函数注册为 Lua 可调用函数
** 4. [错误处理] 统一的错误代码和异常传播机制
** 5. [内存共享] C 和 Lua 代码共享统一的内存分配器
** 6. [调试集成] C 代码可访问 Lua 的调试和性能信息
**
** [版本] 版本信息：
** - Lua 5.1.5: 稳定版本，广泛应用于生产环境
** - 向后兼容: 保持与早期版本的 API 兼容性
** - 标准实现: 符合 Lua 语言规范的参考实现
**
** [依赖] 系统依赖：
** - stdarg.h: 可变参数处理（用于格式化字符串）
** - stddef.h: 标准类型定义（size_t, ptrdiff_t 等）
** - luaconf.h: Lua 配置文件（编译时配置选项）
**
** [许可] 版权信息：
** $Id: lua.h,v 1.218.1.7 2012/01/13 20:36:20 roberto Exp $
** Lua - An Extensible Extension Language
** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef lua_h
#define lua_h

/*
** ============================================================================
** [依赖] 系统头文件包含区域
** ============================================================================
*/

// 可变参数处理头文件
// 提供 va_list、va_start、va_end 等宏定义
// 用于 lua_pushvfstring 等格式化函数的实现
#include <stdarg.h>

// 标准定义头文件
// 提供 size_t、ptrdiff_t、NULL 等基础类型定义
// 用于内存大小计算和指针运算
#include <stddef.h>

// Lua 配置头文件
// 包含编译时配置选项和平台特定定义
// 定义 LUA_API、LUA_NUMBER、LUA_INTEGER 等关键宏
#include "luaconf.h"


/*
** ============================================================================
** [版本] Lua 版本信息和标识常量
** ============================================================================
*/

// Lua 版本标识字符串
// 用于运行时版本检查和兼容性验证
#define LUA_VERSION    "Lua 5.1"

// Lua 完整版本发布字符串
// 包含主版本号和修订版本号信息
#define LUA_RELEASE    "Lua 5.1.5"

// Lua 版本数值表示
// 用于编译时版本比较和条件编译
// 格式：主版本*100 + 次版本（501 表示 5.1）
#define LUA_VERSION_NUM    501

// Lua 版权信息字符串
// 显示版权归属和年份范围
#define LUA_COPYRIGHT    "Copyright (C) 1994-2012 Lua.org, PUC-Rio"

// Lua 主要作者信息
// 列出核心开发团队成员
#define LUA_AUTHORS    "R. Ierusalimschy, L. H. de Figueiredo & W. Celes"


/*
** ============================================================================
** [标识] 字节码和调用约定常量
** ============================================================================
*/

// 预编译字节码文件标识符
// 字节码文件头部的魔术数字：ESC + "Lua" (0x1B + "Lua")
// 用于识别合法的 Lua 字节码文件
#define LUA_SIGNATURE    "\033Lua"

// 多返回值调用选项
// 在 lua_pcall 和 lua_call 中表示接受任意数量的返回值
// 值为 -1，与正常返回值数量区分
#define LUA_MULTRET    (-1)


/*
** ============================================================================
** [栈索引] 虚拟栈伪索引定义
** ============================================================================
*/

// 注册表伪索引
// 访问 Lua 注册表的特殊索引，注册表是全局唯一的键值存储
// 用于存储需要在 C 代码中持久化的 Lua 值
#define LUA_REGISTRYINDEX    (-10000)

// 环境表伪索引
// 访问当前函数环境表的特殊索引
// 用于访问函数的词法环境和全局变量
#define LUA_ENVIRONINDEX    (-10001)

// 全局表伪索引
// 访问全局变量表的特殊索引
// 用于直接操作全局变量命名空间
#define LUA_GLOBALSINDEX    (-10002)

// 上值索引计算宏
// 计算指定上值的伪索引位置
// @param i: 上值编号（从1开始）
// @return: 对应的伪索引值
#define lua_upvalueindex(i)    (LUA_GLOBALSINDEX - (i))


/*
** ============================================================================
** [状态码] 线程执行状态和错误代码
** ============================================================================
*/

// 协程让出状态码
// 协程执行 yield 操作后的返回状态
// 表示协程暂停执行，等待恢复
#define LUA_YIELD        1

// 运行时错误状态码
// Lua 代码执行过程中发生错误
// 包括访问 nil 值、调用非函数等运行时异常
#define LUA_ERRRUN       2

// 语法错误状态码
// Lua 源码编译过程中的语法错误
// 包括关键字拼写错误、语法结构不完整等
#define LUA_ERRSYNTAX    3

// 内存分配错误状态码
// 内存分配失败或内存不足错误
// 通常在创建大型对象或递归过深时发生
#define LUA_ERRMEM       4

// 错误处理函数错误状态码
// 错误处理函数本身发生错误的双重错误情况
// 表示错误处理机制失效，情况严重
#define LUA_ERRERR       5

// 注意：状态码 0 表示正常执行完成（LUA_OK）


/*
** ============================================================================
** [类型] 核心数据类型和函数指针定义
** ============================================================================
*/

// Lua 状态机结构体前向声明
// 代表一个完整的 Lua 虚拟机实例
// 包含执行栈、全局状态、垃圾回收器等所有运行时信息
typedef struct lua_State lua_State;

// C 函数类型定义
// 可以注册为 Lua 函数的 C 函数指针类型
// @param L: Lua 状态机指针
// @return: 返回值数量（推入栈的值的个数）
typedef int (*lua_CFunction) (lua_State *L);


/*
** ============================================================================
** [I/O] 字节码加载和转储的回调函数类型
** ============================================================================
*/

// 数据读取器函数类型
// 用于从外部数据源加载 Lua 字节码或源代码
// @param L: Lua 状态机指针
// @param ud: 用户数据指针（通常是文件句柄或缓冲区）
// @param sz: 输出参数，返回读取的数据大小
// @return: 指向读取数据的指针，NULL 表示数据结束
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);

// 数据写入器函数类型
// 用于将 Lua 字节码写入到外部数据目标
// @param L: Lua 状态机指针
// @param p: 要写入的数据指针
// @param sz: 要写入的数据大小
// @param ud: 用户数据指针（通常是文件句柄或缓冲区）
// @return: 0 表示成功，非 0 表示写入失败
typedef int (*lua_Writer) (lua_State *L, const void* p, size_t sz, void* ud);


/*
** ============================================================================
** [内存] 内存分配器函数类型定义
** ============================================================================
*/

// 内存分配器函数类型
// Lua 使用的统一内存管理接口，支持分配、重分配和释放
// @param ud: 用户数据指针，传递给分配器的上下文信息
// @param ptr: 原内存块指针，NULL 表示新分配
// @param osize: 原内存块大小，0 表示新分配
// @param nsize: 新内存块大小，0 表示释放内存
// @return: 新分配的内存指针，失败时返回 NULL
//
// 分配器行为规则：
// - ptr == NULL, nsize > 0: 新分配 nsize 字节内存
// - ptr != NULL, nsize > 0: 重分配 ptr 为 nsize 字节
// - ptr != NULL, nsize == 0: 释放 ptr 内存，返回 NULL
// - nsize == 0: 总是返回 NULL（释放操作）
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);


/*
** ============================================================================
** [类型系统] Lua 基础数据类型标识常量
** ============================================================================
*/

// 无效类型标识
// 表示栈索引位置无效或不存在值
#define LUA_TNONE            (-1)

// nil 类型标识
// Lua 的空值类型，表示变量未初始化或值不存在
#define LUA_TNIL             0

// 布尔类型标识
// Lua 的布尔值类型，包含 true 和 false 两个值
#define LUA_TBOOLEAN         1

// 轻量用户数据类型标识
// C 指针的轻量级包装，不受垃圾回收管理
#define LUA_TLIGHTUSERDATA   2

// 数值类型标识
// Lua 的数值类型，通常为双精度浮点数
#define LUA_TNUMBER          3

// 字符串类型标识
// Lua 的字符串类型，内部进行字符串内化处理
#define LUA_TSTRING          4

// 表类型标识
// Lua 的表类型，关联数组和数组的混合数据结构
#define LUA_TTABLE           5

// 函数类型标识
// Lua 函数或 C 函数的类型标识
#define LUA_TFUNCTION        6

// 用户数据类型标识
// 由 C 代码分配的任意数据类型，受垃圾回收管理
#define LUA_TUSERDATA        7

// 线程类型标识
// Lua 协程（轻量级线程）的类型标识
#define LUA_TTHREAD          8



/*
** ============================================================================
** [栈管理] 虚拟栈配置常量
** ============================================================================
*/

// C 函数最小可用栈槽数量
// 保证每个 C 函数至少有 20 个栈槽可用
// 用于防止栈溢出和确保基本操作的栈空间
#define LUA_MINSTACK    20


/*
** ============================================================================
** [扩展] 用户自定义头文件包含机制
** ============================================================================
*/

// 条件包含用户自定义头文件
// 如果定义了 LUA_USER_H 宏，则包含指定的用户头文件
// 允许用户在不修改 lua.h 的情况下添加自定义定义
#if defined(LUA_USER_H)
#include LUA_USER_H
#endif


/*
** ============================================================================
** [数值] Lua 数值类型定义
** ============================================================================
*/

// Lua 数值类型
// 由 luaconf.h 中的 LUA_NUMBER 配置决定具体类型
// 通常为 double（双精度浮点数）
typedef LUA_NUMBER lua_Number;

// Lua 整数类型
// 由 luaconf.h 中的 LUA_INTEGER 配置决定具体类型
// 用于整数相关的函数和操作
typedef LUA_INTEGER lua_Integer;



/*
** ============================================================================
** [状态管理] Lua 状态机操作函数
** ============================================================================
*/

// 创建新的 Lua 状态机
// @param f: 内存分配器函数指针
// @param ud: 传递给分配器的用户数据
// @return: 新创建的状态机指针，失败时返回 NULL
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud);

// 关闭并销毁 Lua 状态机
// 释放状态机占用的所有内存资源，包括栈、全局状态等
// @param L: 要关闭的状态机指针
LUA_API void       (lua_close) (lua_State *L);

// 创建新的协程线程
// 在指定状态机中创建新的协程执行线程
// @param L: 父状态机指针
// @return: 新协程的状态机指针
LUA_API lua_State *(lua_newthread) (lua_State *L);

// 设置 panic 函数
// 设置当发生不可恢复错误时调用的 panic 函数
// @param L: 状态机指针
// @param panicf: panic 函数指针
// @return: 之前设置的 panic 函数指针
LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);


/*
** ============================================================================
** [栈操作] 基础虚拟栈操作函数
** ============================================================================
*/

// 获取栈顶元素索引
// @param L: 状态机指针
// @return: 栈顶元素的索引位置（栈中元素数量）
LUA_API int   (lua_gettop) (lua_State *L);

// 设置栈顶位置
// 调整栈的大小，可用于清空栈或调整栈大小
// @param L: 状态机指针
// @param idx: 新的栈顶索引位置
LUA_API void  (lua_settop) (lua_State *L, int idx);

// 复制栈元素到栈顶
// 将指定索引的值复制一份推入栈顶
// @param L: 状态机指针
// @param idx: 要复制的元素索引
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);

// 移除栈中指定元素
// 删除指定索引的元素，后续元素向下移动
// @param L: 状态机指针
// @param idx: 要移除的元素索引
LUA_API void  (lua_remove) (lua_State *L, int idx);

// 插入栈顶元素到指定位置
// 将栈顶元素插入到指定索引位置
// @param L: 状态机指针
// @param idx: 插入的目标索引
LUA_API void  (lua_insert) (lua_State *L, int idx);

// 替换栈中指定元素
// 用栈顶元素替换指定索引的元素，栈顶元素被移除
// @param L: 状态机指针
// @param idx: 要替换的元素索引
LUA_API void  (lua_replace) (lua_State *L, int idx);

// 检查栈空间是否足够
// 确保栈中至少有指定数量的空闲槽位
// @param L: 状态机指针
// @param sz: 需要的空闲槽位数量
// @return: 1 表示空间足够，0 表示空间不足
LUA_API int   (lua_checkstack) (lua_State *L, int sz);

// 跨状态机移动值
// 将值从一个状态机的栈转移到另一个状态机的栈
// @param from: 源状态机指针
// @param to: 目标状态机指针
// @param n: 要转移的值的数量
LUA_API void  (lua_xmove) (lua_State *from, lua_State *to, int n);


/*
** ============================================================================
** [类型检查] 栈值类型检查和访问函数 (栈 -> C)
** ============================================================================
*/

// 检查值是否为数值类型
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 1 表示是数值，0 表示不是
LUA_API int             (lua_isnumber) (lua_State *L, int idx);

// 检查值是否为字符串类型
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 1 表示是字符串，0 表示不是
LUA_API int             (lua_isstring) (lua_State *L, int idx);

// 检查值是否为 C 函数
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 1 表示是 C 函数，0 表示不是
LUA_API int             (lua_iscfunction) (lua_State *L, int idx);

// 检查值是否为用户数据
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 1 表示是用户数据，0 表示不是
LUA_API int             (lua_isuserdata) (lua_State *L, int idx);

// 获取值的类型标识
// @param L: 状态机指针
// @param idx: 栈索引
// @return: LUA_T* 类型常量之一
LUA_API int             (lua_type) (lua_State *L, int idx);

// 获取类型名称字符串
// @param L: 状态机指针
// @param tp: 类型标识
// @return: 类型名称的 C 字符串
LUA_API const char     *(lua_typename) (lua_State *L, int tp);

// 比较两个值是否相等
// 使用 Lua 的相等比较规则，可能调用元方法
// @param L: 状态机指针
// @param idx1: 第一个值的索引
// @param idx2: 第二个值的索引
// @return: 1 表示相等，0 表示不等
LUA_API int            (lua_equal) (lua_State *L, int idx1, int idx2);

// 原始相等比较
// 不调用元方法的直接相等比较
// @param L: 状态机指针
// @param idx1: 第一个值的索引
// @param idx2: 第二个值的索引
// @return: 1 表示相等，0 表示不等
LUA_API int            (lua_rawequal) (lua_State *L, int idx1, int idx2);

// 比较两个值大小关系
// 使用 Lua 的小于比较规则，可能调用元方法
// @param L: 状态机指针
// @param idx1: 第一个值的索引
// @param idx2: 第二个值的索引
// @return: 1 表示 idx1 < idx2，0 表示不成立
LUA_API int            (lua_lessthan) (lua_State *L, int idx1, int idx2);

/*
** ============================================================================
** [值转换] 栈值到 C 类型转换函数
** ============================================================================
*/

// 转换为数值类型
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 转换后的数值，无法转换时返回 0
LUA_API lua_Number      (lua_tonumber) (lua_State *L, int idx);

// 转换为整数类型
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 转换后的整数，无法转换时返回 0
LUA_API lua_Integer     (lua_tointeger) (lua_State *L, int idx);

// 转换为布尔值
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 1 表示真值，0 表示假值（nil 和 false）
LUA_API int             (lua_toboolean) (lua_State *L, int idx);

// 转换为 C 字符串
// @param L: 状态机指针
// @param idx: 栈索引
// @param len: 输出参数，返回字符串长度（可为 NULL）
// @return: C 字符串指针，无法转换时返回 NULL
LUA_API const char     *(lua_tolstring) (lua_State *L, int idx, size_t *len);

// 获取对象长度
// 返回字符串、表或用户数据的长度
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 对象长度
LUA_API size_t          (lua_objlen) (lua_State *L, int idx);

// 转换为 C 函数指针
// @param L: 状态机指针
// @param idx: 栈索引
// @return: C 函数指针，不是 C 函数时返回 NULL
LUA_API lua_CFunction   (lua_tocfunction) (lua_State *L, int idx);

// 转换为用户数据指针
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 用户数据指针，不是用户数据时返回 NULL
LUA_API void           *(lua_touserdata) (lua_State *L, int idx);

// 转换为线程状态机
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 线程状态机指针，不是线程时返回 NULL
LUA_API lua_State      *(lua_tothread) (lua_State *L, int idx);

// 转换为通用指针
// 获取任意 Lua 值的内部指针表示
// @param L: 状态机指针
// @param idx: 栈索引
// @return: 通用指针，用于唯一标识对象
LUA_API const void     *(lua_topointer) (lua_State *L, int idx);


/*
** ============================================================================
** [值推入] C 值到栈的推入函数 (C -> 栈)
** ============================================================================
*/

// 推入 nil 值
// @param L: 状态机指针
LUA_API void  (lua_pushnil) (lua_State *L);

// 推入数值
// @param L: 状态机指针
// @param n: 要推入的数值
LUA_API void  (lua_pushnumber) (lua_State *L, lua_Number n);

// 推入整数
// @param L: 状态机指针
// @param n: 要推入的整数
LUA_API void  (lua_pushinteger) (lua_State *L, lua_Integer n);

// 推入指定长度的字符串
// @param L: 状态机指针
// @param s: 字符串指针
// @param l: 字符串长度
LUA_API void  (lua_pushlstring) (lua_State *L, const char *s, size_t l);

// 推入 C 字符串
// @param L: 状态机指针
// @param s: 以 null 结尾的 C 字符串
LUA_API void  (lua_pushstring) (lua_State *L, const char *s);

// 推入格式化字符串（使用 va_list）
// @param L: 状态机指针
// @param fmt: 格式化字符串
// @param argp: 可变参数列表
// @return: 推入的字符串指针
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt,
                                                      va_list argp);

// 推入格式化字符串
// @param L: 状态机指针
// @param fmt: 格式化字符串
// @param ...: 可变参数
// @return: 推入的字符串指针
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);

// 推入 C 闭包
// @param L: 状态机指针
// @param fn: C 函数指针
// @param n: 上值数量
LUA_API void  (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);

// 推入布尔值
// @param L: 状态机指针
// @param b: 布尔值（0 为 false，非 0 为 true）
LUA_API void  (lua_pushboolean) (lua_State *L, int b);

// 推入轻量用户数据
// @param L: 状态机指针
// @param p: C 指针
LUA_API void  (lua_pushlightuserdata) (lua_State *L, void *p);

// 推入当前线程
// @param L: 状态机指针
// @return: 1 表示这是主线程，0 表示这是协程
LUA_API int   (lua_pushthread) (lua_State *L);


/*
** ============================================================================
** [表访问] Lua 表访问函数 (Lua -> 栈)
** ============================================================================
*/

// 表索引访问
// 执行 t[k] 操作，其中 t 在索引 idx，k 在栈顶
// @param L: 状态机指针
// @param idx: 表的栈索引
LUA_API void  (lua_gettable) (lua_State *L, int idx);

// 字段访问
// 执行 t[k] 操作，其中 t 在索引 idx，k 为字符串
// @param L: 状态机指针
// @param idx: 表的栈索引
// @param k: 字段名字符串
LUA_API void  (lua_getfield) (lua_State *L, int idx, const char *k);

// 原始表索引访问
// 不调用元方法的直接表访问
// @param L: 状态机指针
// @param idx: 表的栈索引
LUA_API void  (lua_rawget) (lua_State *L, int idx);

// 原始整数索引访问
// 直接通过整数索引访问表元素
// @param L: 状态机指针
// @param idx: 表的栈索引
// @param n: 整数索引
LUA_API void  (lua_rawgeti) (lua_State *L, int idx, int n);

// 创建新表
// @param L: 状态机指针
// @param narr: 数组部分的预分配大小
// @param nrec: 哈希部分的预分配大小
LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);

// 创建新的用户数据
// @param L: 状态机指针
// @param sz: 用户数据大小
// @return: 分配的用户数据指针
LUA_API void *(lua_newuserdata) (lua_State *L, size_t sz);

// 获取元表
// @param L: 状态机指针
// @param objindex: 对象的栈索引
// @return: 1 表示有元表，0 表示无元表
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);

// 获取环境表
// @param L: 状态机指针
// @param idx: 对象的栈索引
LUA_API void  (lua_getfenv) (lua_State *L, int idx);


/*
** ============================================================================
** [表设置] Lua 表设置函数 (栈 -> Lua)
** ============================================================================
*/

// 表索引设置
// 执行 t[k] = v 操作，其中 t 在索引 idx，k 和 v 在栈顶
// @param L: 状态机指针
// @param idx: 表的栈索引
LUA_API void  (lua_settable) (lua_State *L, int idx);

// 字段设置
// 执行 t[k] = v 操作，其中 t 在索引 idx，k 为字符串，v 在栈顶
// @param L: 状态机指针
// @param idx: 表的栈索引
// @param k: 字段名字符串
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);

// 原始表索引设置
// 不调用元方法的直接表设置
// @param L: 状态机指针
// @param idx: 表的栈索引
LUA_API void  (lua_rawset) (lua_State *L, int idx);

// 原始整数索引设置
// 直接通过整数索引设置表元素
// @param L: 状态机指针
// @param idx: 表的栈索引
// @param n: 整数索引
LUA_API void  (lua_rawseti) (lua_State *L, int idx, int n);

// 设置元表
// @param L: 状态机指针
// @param objindex: 对象的栈索引
// @return: 1 表示成功，0 表示失败
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);

// 设置环境表
// @param L: 状态机指针
// @param idx: 对象的栈索引
// @return: 1 表示成功，0 表示失败
LUA_API int   (lua_setfenv) (lua_State *L, int idx);


/*
** ============================================================================
** [函数调用] Lua 代码加载和执行函数
** ============================================================================
*/

// 调用 Lua 函数
// 不保护的函数调用，错误会向上传播
// @param L: 状态机指针
// @param nargs: 参数数量
// @param nresults: 期望的返回值数量（LUA_MULTRET 表示全部）
LUA_API void  (lua_call) (lua_State *L, int nargs, int nresults);

// 保护调用 Lua 函数
// 受保护的函数调用，捕获错误
// @param L: 状态机指针
// @param nargs: 参数数量
// @param nresults: 期望的返回值数量
// @param errfunc: 错误处理函数的栈索引（0 表示无错误处理）
// @return: 0 表示成功，其他值表示错误类型
LUA_API int   (lua_pcall) (lua_State *L, int nargs, int nresults, int errfunc);

// 保护调用 C 函数
// 在保护模式下调用 C 函数
// @param L: 状态机指针
// @param func: 要调用的 C 函数
// @param ud: 传递给函数的用户数据
// @return: 0 表示成功，其他值表示错误类型
LUA_API int   (lua_cpcall) (lua_State *L, lua_CFunction func, void *ud);

// 加载 Lua 代码块
// @param L: 状态机指针
// @param reader: 数据读取器函数
// @param dt: 传递给读取器的数据
// @param chunkname: 代码块名称（用于错误报告）
// @return: 0 表示成功，其他值表示错误类型
LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt,
                                        const char *chunkname);

// 转储函数为字节码
// @param L: 状态机指针
// @param writer: 数据写入器函数
// @param data: 传递给写入器的数据
// @return: 0 表示成功，其他值表示错误
LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data);


/*
** ============================================================================
** [协程] 协程控制函数
** ============================================================================
*/

// 让出协程执行
// 暂停当前协程的执行，返回到调用者
// @param L: 状态机指针
// @param nresults: 返回给调用者的值的数量
// @return: 不返回（函数不会正常返回）
LUA_API int  (lua_yield) (lua_State *L, int nresults);

// 恢复协程执行
// 继续执行暂停的协程
// @param L: 协程状态机指针
// @param narg: 传递给协程的参数数量
// @return: LUA_YIELD 表示协程让出，0 表示正常结束，其他表示错误
LUA_API int  (lua_resume) (lua_State *L, int narg);

// 获取协程状态
// @param L: 状态机指针
// @return: 0 表示正常状态，LUA_YIELD 表示挂起状态，其他表示错误状态
LUA_API int  (lua_status) (lua_State *L);

/*
** ============================================================================
** [垃圾回收] 垃圾回收器控制函数和选项
** ============================================================================
*/

// 垃圾回收控制选项常量
// 停止垃圾回收器
#define LUA_GCSTOP        0
// 重启垃圾回收器
#define LUA_GCRESTART     1
// 执行完整的垃圾回收周期
#define LUA_GCCOLLECT     2
// 获取当前内存使用量（KB）
#define LUA_GCCOUNT       3
// 获取当前内存使用量的余数（字节）
#define LUA_GCCOUNTB      4
// 执行增量垃圾回收步骤
#define LUA_GCSTEP        5
// 设置垃圾回收暂停参数
#define LUA_GCSETPAUSE    6
// 设置垃圾回收步长乘数参数
#define LUA_GCSETSTEPMUL  7

// 垃圾回收器控制函数
// @param L: 状态机指针
// @param what: 控制选项（LUA_GC* 常量之一）
// @param data: 选项相关的数据参数
// @return: 操作结果，具体含义取决于选项类型
LUA_API int (lua_gc) (lua_State *L, int what, int data);


/*
** ============================================================================
** [杂项] 其他实用函数
** ============================================================================
*/

// 抛出错误
// 抛出栈顶值作为错误消息
// @param L: 状态机指针
// @return: 不返回（函数会抛出错误）
LUA_API int   (lua_error) (lua_State *L);

// 表遍历函数
// 遍历表中的键值对
// @param L: 状态机指针
// @param idx: 表的栈索引
// @return: 1 表示还有下一对，0 表示遍历结束
LUA_API int   (lua_next) (lua_State *L, int idx);

// 字符串连接
// 连接栈顶的 n 个值为一个字符串
// @param L: 状态机指针
// @param n: 要连接的值的数量
LUA_API void  (lua_concat) (lua_State *L, int n);

// 获取内存分配器
// @param L: 状态机指针
// @param ud: 输出参数，返回分配器的用户数据
// @return: 当前的内存分配器函数
LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);

// 设置内存分配器
// @param L: 状态机指针
// @param f: 新的内存分配器函数
// @param ud: 传递给分配器的用户数据
LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud);



/*
** ============================================================================
** [宏定义] 常用操作的便利宏
** ============================================================================
*/

// 弹出栈顶元素
// 移除栈顶的 n 个元素
#define lua_pop(L,n)        lua_settop(L, -(n)-1)

// 创建空表
// 创建一个新的空表
#define lua_newtable(L)     lua_createtable(L, 0, 0)

// 注册全局函数
// 将 C 函数注册为全局 Lua 函数
#define lua_register(L,n,f) (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))

// 推入 C 函数
// 推入没有上值的 C 函数
#define lua_pushcfunction(L,f)    lua_pushcclosure(L, (f), 0)

// 获取字符串长度
// 获取字符串对象的长度
#define lua_strlen(L,i)     lua_objlen(L, (i))

// 类型检查宏定义
// 检查指定位置的值是否为特定类型
#define lua_isfunction(L,n)    (lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_istable(L,n)       (lua_type(L, (n)) == LUA_TTABLE)
#define lua_islightuserdata(L,n)    (lua_type(L, (n)) == LUA_TLIGHTUSERDATA)
#define lua_isnil(L,n)         (lua_type(L, (n)) == LUA_TNIL)
#define lua_isboolean(L,n)     (lua_type(L, (n)) == LUA_TBOOLEAN)
#define lua_isthread(L,n)      (lua_type(L, (n)) == LUA_TTHREAD)
#define lua_isnone(L,n)        (lua_type(L, (n)) == LUA_TNONE)
#define lua_isnoneornil(L, n)  (lua_type(L, (n)) <= 0)

// 推入字面量字符串
// 推入编译时已知的字符串字面量
#define lua_pushliteral(L, s)    \
    lua_pushlstring(L, "" s, (sizeof(s)/sizeof(char))-1)

// 全局变量操作宏
// 设置和获取全局变量的便利宏
#define lua_setglobal(L,s)    lua_setfield(L, LUA_GLOBALSINDEX, (s))
#define lua_getglobal(L,s)    lua_getfield(L, LUA_GLOBALSINDEX, (s))

// 转换为字符串
// 将栈中的值转换为 C 字符串
#define lua_tostring(L,i)    lua_tolstring(L, (i), NULL)



/*
** ============================================================================
** [兼容性] 向后兼容的宏定义和函数
** ============================================================================
*/

// 旧版本兼容的状态机创建宏
// 提供与早期版本兼容的状态机创建接口
#define lua_open()    luaL_newstate()

// 获取注册表
// 将注册表推入栈顶
#define lua_getregistry(L)    lua_pushvalue(L, LUA_REGISTRYINDEX)

// 获取垃圾回收统计（旧版本兼容）
// 获取当前内存使用量的 KB 数
#define lua_getgccount(L)    lua_gc(L, LUA_GCCOUNT, 0)

// 旧版本的类型别名
// 为了向后兼容而保留的类型别名
#define lua_Chunkreader      lua_Reader
#define lua_Chunkwriter      lua_Writer

// 内部调试用函数（非标准 API）
// 设置线程的调试级别，通常用于内部调试
LUA_API void lua_setlevel    (lua_State *from, lua_State *to);


/*
** ============================================================================
** [调试 API] Lua 调试接口和事件处理
** ============================================================================
*/

/*
** 调试事件代码
** 定义调试钩子可以捕获的各种执行事件
*/

// 函数调用事件
#define LUA_HOOKCALL    0

// 函数返回事件
#define LUA_HOOKRET     1

// 行执行事件
#define LUA_HOOKLINE    2

// 指令计数事件
#define LUA_HOOKCOUNT   3

// 尾调用返回事件
#define LUA_HOOKTAILRET 4

/*
** 事件掩码
** 用于指定要监听的事件类型的位掩码
*/

// 函数调用事件掩码
#define LUA_MASKCALL    (1 << LUA_HOOKCALL)

// 函数返回事件掩码
#define LUA_MASKRET     (1 << LUA_HOOKRET)

// 行执行事件掩码
#define LUA_MASKLINE    (1 << LUA_HOOKLINE)

// 指令计数事件掩码
#define LUA_MASKCOUNT   (1 << LUA_HOOKCOUNT)

// 调试信息结构体前向声明
// 包含函数执行的调试和堆栈信息
typedef struct lua_Debug lua_Debug;

// 调试钩子函数类型
// 在特定事件发生时被调用的回调函数
// @param L: 状态机指针
// @param ar: 包含事件信息的调试记录
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);


/*
** ============================================================================
** [调试函数] 调试信息获取和控制函数
** ============================================================================
*/

// 获取调用栈信息
// @param L: 状态机指针
// @param level: 栈层级（0 为当前函数，1 为调用者等）
// @param ar: 输出参数，填充调试信息
// @return: 1 表示成功，0 表示层级无效
LUA_API int lua_getstack (lua_State *L, int level, lua_Debug *ar);

// 获取详细调试信息
// @param L: 状态机指针
// @param what: 请求的信息类型字符串
// @param ar: 输入/输出参数，包含调试信息
// @return: 1 表示成功，0 表示失败
LUA_API int lua_getinfo (lua_State *L, const char *what, lua_Debug *ar);

// 获取局部变量信息
// @param L: 状态机指针
// @param ar: 调试记录指针
// @param n: 局部变量编号
// @return: 变量名称，NULL 表示无效编号
LUA_API const char *lua_getlocal (lua_State *L, const lua_Debug *ar, int n);

// 设置局部变量
// @param L: 状态机指针
// @param ar: 调试记录指针
// @param n: 局部变量编号
// @return: 变量名称，NULL 表示无效编号
LUA_API const char *lua_setlocal (lua_State *L, const lua_Debug *ar, int n);

// 获取上值信息
// @param L: 状态机指针
// @param funcindex: 函数的栈索引
// @param n: 上值编号
// @return: 上值名称，NULL 表示无效编号
LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n);

// 设置上值
// @param L: 状态机指针
// @param funcindex: 函数的栈索引
// @param n: 上值编号
// @return: 上值名称，NULL 表示无效编号
LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n);

// 设置调试钩子
// @param L: 状态机指针
// @param func: 钩子函数指针
// @param mask: 事件掩码
// @param count: 指令计数间隔
// @return: 1 表示成功，0 表示失败
LUA_API int lua_sethook (lua_State *L, lua_Hook func, int mask, int count);

// 获取当前调试钩子
// @param L: 状态机指针
// @return: 当前的钩子函数指针
LUA_API lua_Hook lua_gethook (lua_State *L);

// 获取调试钩子掩码
// @param L: 状态机指针
// @return: 当前的事件掩码
LUA_API int lua_gethookmask (lua_State *L);

// 获取调试钩子计数
// @param L: 状态机指针
// @return: 当前的指令计数间隔
LUA_API int lua_gethookcount (lua_State *L);


/*
** ============================================================================
** [调试结构] 调试信息结构体定义
** ============================================================================
*/

// Lua 调试信息结构体
// 包含函数执行时的各种调试和堆栈信息
struct lua_Debug
{
    // 触发的调试事件类型
    int event;
    
    // 函数或变量名称（通过 'n' 选项获取）
    const char *name;
    
    // 名称的类型：'global'、'local'、'field'、'method'
    // 通过 'n' 选项获取
    const char *namewhat;
    
    // 函数类型：'Lua'、'C'、'main'、'tail'
    // 通过 'S' 选项获取
    const char *what;
    
    // 源代码文件名或代码块名称
    // 通过 'S' 选项获取
    const char *source;
    
    // 当前执行的行号
    // 通过 'l' 选项获取
    int currentline;
    
    // 函数的上值数量
    // 通过 'u' 选项获取
    int nups;
    
    // 函数定义开始的行号
    // 通过 'S' 选项获取
    int linedefined;
    
    // 函数定义结束的行号
    // 通过 'S' 选项获取
    int lastlinedefined;
    
    // 源代码文件名的简化版本
    // 自动填充，长度限制为 LUA_IDSIZE
    char short_src[LUA_IDSIZE];
    
    /*
    ** 私有部分
    ** 仅供 Lua 内部使用，外部代码不应直接访问
    */
    
    // 活动函数的内部索引
    int i_ci;
};

/*
** 调试 API 部分结束
*/


#endif
