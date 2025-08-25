/*
** $Id: llimits.h,v 1.69.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua基础类型定义、限制和平台相关的定义
** 版权声明见lua.h
*/

#ifndef llimits_h
#define llimits_h

#include <limits.h>
#include <stddef.h>

#include "lua.h"

/*
** 基础整数类型定义
** 这些类型定义确保了Lua在不同平台上的一致性
*/

/* 32位无符号整数类型，用于虚拟机指令和哈希值 */
typedef LUAI_UINT32 lu_int32;

/* 内存大小类型，用于表示内存块的大小（无符号） */
typedef LUAI_UMEM lu_mem;

/* 内存差值类型，用于表示内存地址的差值（有符号） */
typedef LUAI_MEM l_mem;

/*
** 字节类型定义
** 使用unsigned char作为小的自然数类型，保留char用于字符处理
** 在Lua内部广泛用于标志位、类型标记等
*/
typedef unsigned char lu_byte;

/*
** 系统限制定义
** 这些宏定义了各种数据类型的最大值，减去2是为了安全边界
*/

/* size_t类型的最大值，减去2为安全预留 */
#define MAX_SIZET	((size_t)(~(size_t)0)-2)

/* lu_mem类型的最大值，减去2为安全预留 */
#define MAX_LUMEM	((lu_mem)(~(lu_mem)0)-2)

/* int类型的最大值，减去2为安全预留，防止溢出 */
#define MAX_INT (INT_MAX-2)

/*
** 指针到整数的转换宏
** 仅用于哈希计算，不要求整数能完整保存指针值
** 在哈希表实现中用于将指针转换为哈希键
*/
#define IntPoint(p)  ((unsigned int)(lu_mem)(p))

/*
** 内存对齐类型
** 确保最大对齐要求，用于内存分配时的对齐
*/
typedef LUAI_USER_ALIGNMENT_T L_Umaxalign;

/*
** lua_Number的标准参数转换结果类型
** 用于函数调用时的数值参数处理
*/
typedef LUAI_UACNUMBER l_uacNumber;

/*
** 内部断言机制
** 用于调试和开发时的内部一致性检查
*/
#ifdef lua_assert

/* 检查表达式：先断言条件c，然后返回表达式e的值 */
#define check_exp(c,e)		(lua_assert(c), (e))
/* API检查：在API函数中验证条件 */
#define api_check(l,e)		lua_assert(e)

#else

/* 发布版本中断言为空操作 */
#define lua_assert(c)		((void)0)
/* 发布版本中只返回表达式值，不做检查 */
#define check_exp(c,e)		(e)
/* 使用配置的API检查函数 */
#define api_check		luai_apicheck

#endif

/*
** 通用工具宏
*/

/* 标记未使用的变量，避免编译器警告 */
#ifndef UNUSED
#define UNUSED(x)	((void)(x))
#endif

/* 类型转换宏，提供更清晰的类型转换语法 */
#ifndef cast
#define cast(t, exp)	((t)(exp))
#endif

/* 常用类型转换的便捷宏 */
#define cast_byte(i)	cast(lu_byte, (i))    /* 转换为字节类型 */
#define cast_num(i)	cast(lua_Number, (i))  /* 转换为Lua数值类型 */
#define cast_int(i)	cast(int, (i))         /* 转换为整数类型 */

/*
** 虚拟机指令类型
** 必须是至少4字节的无符号整数（详见lopcodes.h）
** 用于存储Lua字节码指令
*/
typedef lu_int32 Instruction;

/*
** Lua函数的最大栈大小
** 限制单个Lua函数可以使用的栈槽数量
** 这个限制确保了栈不会无限增长
*/
#define MAXSTACK	250

/*
** 字符串表的最小大小（必须是2的幂）
** 字符串表用于字符串内部化，提高字符串比较和存储效率
*/
#ifndef MINSTRTABSIZE
#define MINSTRTABSIZE	32
#endif

/*
** 字符串缓冲区的最小大小
** 用于字符串构建和操作时的临时缓冲区
*/
#ifndef LUA_MINBUFFER
#define LUA_MINBUFFER	32
#endif

/*
** 线程同步机制
** 在多线程环境中保护Lua状态的锁定/解锁操作
** 默认实现为空操作，可在编译时重定义
*/
#ifndef lua_lock
#define lua_lock(L)     ((void) 0) 
#define lua_unlock(L)   ((void) 0)
#endif

/*
** 线程让步机制
** 在长时间运行的操作中允许其他线程获得执行机会
** 通过解锁再重新锁定来实现
*/
#ifndef luai_threadyield
#define luai_threadyield(L)     {lua_unlock(L); lua_lock(L);}
#endif

/*
** 栈重分配的硬测试控制宏
** 用于调试时强制进行栈重分配测试
** 在发布版本中通常关闭以提高性能
*/ 
#ifndef HARDSTACKTESTS
#define condhardstacktests(x)	((void)0)
#else
#define condhardstacktests(x)	x
#endif

#endif
