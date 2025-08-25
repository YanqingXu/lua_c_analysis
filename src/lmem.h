/*
** $Id: lmem.h,v 1.31.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua内存管理器接口
** 版权声明见lua.h
*/

#ifndef lmem_h
#define lmem_h

#include <stddef.h>

#include "llimits.h"
#include "lua.h"

/*
** 内存不足错误消息
** 当内存分配失败时使用的标准错误消息
*/
#define MEMERRMSG	"not enough memory"

/*
** 核心内存管理宏定义
** 这些宏提供了Lua内存管理的高级接口，所有内存操作都通过这些宏进行
*/

/*
** luaM_reallocv - 重分配向量(数组)内存的安全宏
** 参数：
**   L: Lua状态机
**   b: 原内存块指针
**   on: 原元素数量
**   n: 新元素数量
**   e: 每个元素的大小
** 
** 功能：
** - 安全地重分配数组内存，防止整数溢出
** - 检查 (n+1) * e 是否超过 MAX_SIZET，避免溢出攻击
** - +1是为了避免编译器在n=0时的警告
** - 如果计算安全，调用luaM_realloc_进行实际分配
** - 如果会溢出，调用luaM_toobig抛出错误
*/
#define luaM_reallocv(L,b,on,n,e) \
	((cast(size_t, (n)+1) <= MAX_SIZET/(e)) ?  /* +1 to avoid warnings */ \
		luaM_realloc_(L, (b), (on)*(e), (n)*(e)) : \
		luaM_toobig(L))

/*
** 内存释放宏
** 这些宏提供了不同粒度的内存释放操作
*/

/*
** luaM_freemem - 释放指定大小的内存块
** 参数：
**   L: Lua状态机
**   b: 内存块指针
**   s: 内存块大小
** 
** 实现：通过将新大小设为0来释放内存
*/
#define luaM_freemem(L, b, s)	luaM_realloc_(L, (b), (s), 0)

/*
** luaM_free - 释放单个对象的内存
** 参数：
**   L: Lua状态机
**   b: 对象指针
** 
** 实现：自动计算对象大小并释放
** 使用sizeof(*(b))自动获取对象大小，提供类型安全
*/
#define luaM_free(L, b)		luaM_realloc_(L, (b), sizeof(*(b)), 0)

/*
** luaM_freearray - 释放数组内存
** 参数：
**   L: Lua状态机
**   b: 数组指针
**   n: 数组元素数量
**   t: 元素类型
** 
** 实现：计算数组总大小并释放
** 使用luaM_reallocv确保安全的大小计算
*/
#define luaM_freearray(L, b, n, t)   luaM_reallocv(L, (b), n, 0, sizeof(t))

/*
** 内存分配宏
** 这些宏提供了不同类型的内存分配操作
*/

/*
** luaM_malloc - 分配指定大小的内存
** 参数：
**   L: Lua状态机
**   t: 要分配的字节数
** 
** 实现：通过luaM_realloc_分配新内存(原指针为NULL，原大小为0)
*/
#define luaM_malloc(L,t)	luaM_realloc_(L, NULL, 0, (t))

/*
** luaM_new - 分配单个对象的内存
** 参数：
**   L: Lua状态机
**   t: 对象类型
** 
** 功能：
** - 分配一个类型为t的对象所需的内存
** - 自动计算对象大小sizeof(t)
** - 返回正确类型的指针cast(t *, ...)
** - 提供类型安全的内存分配
*/
#define luaM_new(L,t)		cast(t *, luaM_malloc(L, sizeof(t)))

/*
** luaM_newvector - 分配数组内存
** 参数：
**   L: Lua状态机
**   n: 数组元素数量
**   t: 元素类型
** 
** 功能：
** - 分配n个类型为t的元素组成的数组
** - 使用luaM_reallocv确保安全的大小计算
** - 返回正确类型的指针cast(t *, ...)
*/
#define luaM_newvector(L,n,t) \
		cast(t *, luaM_reallocv(L, NULL, 0, n, sizeof(t)))

/*
** luaM_growvector - 动态增长数组的宏
** 参数：
**   L: Lua状态机
**   v: 数组指针(会被修改)
**   nelems: 当前元素数量
**   size: 当前数组容量(会被修改)
**   t: 元素类型
**   limit: 数组大小限制
**   e: 错误消息
** 
** 功能：
** - 当数组容量不足时自动扩展数组
** - 只有当 nelems+1 > size 时才进行扩展
** - 调用luaM_growaux_进行实际的容量增长
** - 增长策略通常是指数增长(如容量翻倍)
** - 检查是否超过limit限制
** - 这是Lua中动态数组增长的核心机制
*/
#define luaM_growvector(L,v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=cast(t *, luaM_growaux_(L,v,&(size),sizeof(t),limit,e)))

/*
** luaM_reallocvector - 重分配数组到指定大小
** 参数：
**   L: Lua状态机
**   v: 数组指针(会被修改)
**   oldn: 原数组元素数量
**   n: 新数组元素数量
**   t: 元素类型
** 
** 功能：
** - 将数组从oldn个元素重分配为n个元素
** - 如果n > oldn，扩展数组；如果n < oldn，缩小数组
** - 保持现有数据(在重叠部分)
** - 返回新的数组指针(可能与原指针不同)
*/
#define luaM_reallocvector(L, v,oldn,n,t) \
   ((v)=cast(t *, luaM_reallocv(L, v, oldn, n, sizeof(t))))

/*
** 底层内存管理函数声明
** 这些函数提供了Lua内存管理的底层实现
*/

/*
** luaM_realloc_ - 底层内存重分配函数
** 参数：
**   L: Lua状态机
**   block: 原内存块指针(NULL表示分配新内存)
**   oldsize: 原内存块大小
**   size: 新内存块大小(0表示释放内存)
** 
** 功能：
** - Lua内存管理的核心函数
** - 统一处理分配、重分配、释放操作
** - 与垃圾收集器协作，更新内存使用统计
** - 在内存不足时触发垃圾收集
** - 调用用户定义的分配器函数(lua_Alloc)
** - 处理内存分配失败的情况
*/
LUAI_FUNC void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize,
                                                          size_t size);

/*
** luaM_toobig - 处理内存请求过大的情况
** 参数：
**   L: Lua状态机
** 
** 功能：
** - 当请求的内存大小超过系统限制时调用
** - 抛出"内存不足"错误
** - 防止整数溢出攻击
** - 永远不会返回(总是抛出错误)
*/
LUAI_FUNC void *luaM_toobig (lua_State *L);

/*
** luaM_growaux_ - 数组增长的辅助函数
** 参数：
**   L: Lua状态机
**   block: 原数组指针
**   size: 当前数组容量(会被修改为新容量)
**   size_elem: 每个元素的大小
**   limit: 数组大小限制
**   errormsg: 超过限制时的错误消息
** 
** 功能：
** - 实现数组的智能增长策略
** - 通常采用指数增长(容量翻倍)以摊销分配成本
** - 检查新容量是否超过limit限制
** - 在超过限制时抛出errormsg错误
** - 更新size参数为新的容量值
** - 返回重分配后的数组指针
** 
** 增长策略：
** - 小数组：快速增长以减少重分配次数
** - 大数组：适度增长以避免内存浪费
** - 考虑内存碎片和分配效率
*/
LUAI_FUNC void *luaM_growaux_ (lua_State *L, void *block, int *size,
                               size_t size_elem, int limit,
                               const char *errormsg);

#endif
