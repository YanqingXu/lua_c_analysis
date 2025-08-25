/*
** $Id: lmem.c,v 1.70.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua内存管理器实现
** 版权声明见lua.h
*/

#include <stddef.h>

#define lmem_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"

/*
** 关于realloc函数的说明：
** void * frealloc (void *ud, void *ptr, size_t osize, size_t nsize);
** (osize是旧大小，nsize是新大小)
**
** Lua确保 (ptr == NULL) 当且仅当 (osize == 0)
**
** * frealloc(ud, NULL, 0, x) 创建大小为x的新内存块
**
** * frealloc(ud, p, x, 0) 释放内存块p
** (在这种特定情况下，frealloc必须返回NULL)
** 特别地，frealloc(ud, NULL, 0, 0) 什么都不做
** (这等价于ANSI C中的free(NULL))
**
** 如果frealloc无法创建或重新分配区域，则返回NULL
** (任何重新分配到相等或更小大小的操作都不能失败！)
**
** 这个设计统一了malloc、realloc、free三种操作：
** - malloc: frealloc(ud, NULL, 0, size)
** - realloc: frealloc(ud, ptr, oldsize, newsize)  
** - free: frealloc(ud, ptr, oldsize, 0)
*/

/*
** 数组的最小初始大小
** 当创建新数组或数组增长时，确保至少有这么多元素的空间
** 这避免了频繁的小幅重分配，提高性能
*/
#define MINSIZEARRAY	4

/*
** luaM_growaux_ - 数组增长的核心实现函数
** 
** 参数：
**   L: Lua状态机
**   block: 当前数组指针
**   size: 当前数组容量的指针(会被修改)
**   size_elems: 每个元素的字节大小
**   limit: 数组大小的上限
**   errormsg: 超过限制时的错误消息
**
** 返回值：
**   重新分配后的数组指针
**
** 功能：
**   实现智能的数组增长策略，平衡内存使用和性能
*/
void *luaM_growaux_(lua_State *L, void *block, int *size, size_t size_elems,
                    int limit, const char *errormsg)
{
    void *newblock;
    int newsize;
    
    /*
    ** 增长策略的核心逻辑：
    ** 1. 如果当前大小已经超过限制的一半，不能简单翻倍
    ** 2. 否则采用翻倍策略以获得摊销的O(1)性能
    */
    if (*size >= limit / 2)
    {
        /* 不能翻倍吗？ */
        if (*size >= limit)
        {
            /* 连一点都不能增长吗？ */
            luaG_runerror(L, errormsg);  /* 抛出错误 */
        }
        newsize = limit;  /* 设置为最大允许大小，至少还有一个空闲位置 */
    }
    else
    {
        /*
        ** 标准的翻倍增长策略
        ** 这提供了摊销的O(1)插入性能：
        ** - 每次翻倍，之前的所有元素只需要复制一次
        ** - 总的复制成本是O(n)，摊销到n次插入就是O(1)
        */
        newsize = (*size) * 2;
        if (newsize < MINSIZEARRAY)
        {
            newsize = MINSIZEARRAY;  /* 确保最小大小 */
        }
    }
    
    /*
    ** 执行实际的内存重分配
    ** 使用luaM_reallocv确保安全的大小计算(防止溢出)
    */
    newblock = luaM_reallocv(L, block, *size, newsize, size_elems);
  
    /*
    ** 只有在一切都成功后才更新大小
    ** 这确保了异常安全性：如果分配失败，原始状态保持不变
    */
    *size = newsize;
    return newblock;
}

/*
** luaM_toobig - 处理内存请求过大的情况
**
** 参数：
**   L: Lua状态机
**
** 功能：
**   当请求的内存大小会导致整数溢出时调用
**   这是一个安全机制，防止恶意的大内存请求
**   
** 注意：
**   这个函数永远不会正常返回，总是抛出错误
**   返回NULL只是为了避免编译器警告
*/
void *luaM_toobig(lua_State *L)
{
    luaG_runerror(L, "memory allocation error: block too big");
    return NULL;  /* 避免编译器警告 */
}

/*
** luaM_realloc_ - 通用内存分配例程
**
** 参数：
**   L: Lua状态机
**   block: 原内存块指针(NULL表示新分配)
**   osize: 原内存块大小
**   nsize: 新内存块大小(0表示释放)
**
** 返回值：
**   新分配的内存块指针，失败时不返回(抛出错误)
**
** 功能：
**   这是Lua内存管理的核心函数，统一处理所有内存操作
**   与垃圾收集器紧密集成，维护内存使用统计
*/
void *luaM_realloc_(lua_State *L, void *block, size_t osize, size_t nsize)
{
    global_State *g = G(L);  /* 获取全局状态 */
    
    /*
    ** 断言检查Lua的内存管理不变式：
    ** (osize == 0) 当且仅当 (block == NULL)
    ** 这确保了内存管理的一致性
    */
    lua_assert((osize == 0) == (block == NULL));
    
    /*
    ** 调用用户定义的分配器函数
    ** g->frealloc: 用户提供的分配器函数指针
    ** g->ud: 用户数据，传递给分配器函数
    ** 
    ** 这种设计允许用户完全控制内存分配策略：
    ** - 可以使用系统malloc/realloc/free
    ** - 可以使用内存池
    ** - 可以添加调试和统计功能
    ** - 可以实现特殊的内存管理策略
    */
    block = (*g->frealloc)(g->ud, block, osize, nsize);
    
    /*
    ** 处理内存分配失败的情况
    ** 如果请求的大小大于0但返回NULL，说明分配失败
    ** 抛出内存不足错误，触发Lua的错误处理机制
    */
    if (block == NULL && nsize > 0)
    {
        luaD_throw(L, LUA_ERRMEM);
    }
    
    /*
    ** 再次断言检查不变式：
    ** (nsize == 0) 当且仅当 (block == NULL)
    ** 确保分配器正确实现了协议
    */
    lua_assert((nsize == 0) == (block == NULL));
    
    /*
    ** 更新全局内存使用统计
    ** 这个统计信息被垃圾收集器用来决定何时运行GC
    ** 
    ** 计算方式：
    ** - 减去旧的内存大小(osize)
    ** - 加上新的内存大小(nsize)
    ** - 净效果是内存使用量的变化
    **
    ** 这种增量更新方式高效且准确：
    ** - 分配: totalbytes += nsize (osize = 0)
    ** - 释放: totalbytes -= osize (nsize = 0)  
    ** - 重分配: totalbytes += (nsize - osize)
    */
    g->totalbytes = (g->totalbytes - osize) + nsize;
    
    return block;
}
