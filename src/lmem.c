/**
 * @file lmem.c
 * @brief Lua内存管理系统：统一内存接口和动态数组管理
 *
 * 详细说明：
 * 本文件实现了Lua的内存管理抽象层，提供统一的内存分配接口
 * 和动态数组扩展机制。这是Lua系统的基础设施，为所有其他
 * 模块提供内存管理服务。
 *
 * 核心设计理念：
 * 1. 统一接口：所有内存操作通过统一的接口进行
 * 2. 用户可控：支持用户自定义内存分配器
 * 3. 错误集成：内存分配失败与Lua错误处理系统集成
 * 4. 使用统计：精确跟踪内存使用量，支持垃圾回收决策
 *
 * 内存分配器接口规范：
 * void *frealloc(void *ud, void *ptr, size_t osize, size_t nsize)
 *
 * 参数说明：
 * - ud：用户数据，传递给分配器的上下文信息
 * - ptr：要重新分配的内存块指针
 * - osize：原始内存块大小
 * - nsize：新的内存块大小
 *
 * 行为规范：
 * - frealloc(ud, NULL, 0, x)：分配大小为x的新内存块
 * - frealloc(ud, p, x, 0)：释放内存块p（必须返回NULL）
 * - frealloc(ud, NULL, 0, 0)：无操作（等价于free(NULL)）
 * - frealloc(ud, p, x, y)：将内存块p从大小x调整为大小y
 *
 * 错误处理：
 * - 分配失败时返回NULL
 * - 缩小内存块不能失败
 * - 释放内存必须成功
 *
 * 技术特色：
 * - 动态数组倍增策略：高效的数组扩展算法
 * - 内存使用统计：精确的内存使用量跟踪
 * - 异常安全：内存分配失败时的安全错误处理
 * - 抽象层设计：隔离具体的内存分配实现
 *
 * 应用场景：
 * - 对象分配：为Lua对象分配内存
 * - 数组扩展：动态扩展表、字符串等数据结构
 * - 内存统计：为垃圾回收提供内存使用信息
 * - 自定义分配器：支持特殊的内存管理需求
 *
 * 性能考虑：
 * - 最小化分配次数：通过倍增策略减少重分配
 * - 内存对齐：遵循系统的内存对齐要求
 * - 统计开销：最小化内存统计的性能影响
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2007-12-27
 * @since Lua 5.0
 * @see lstate.h, lgc.h, lobject.h
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

// ============================================================================
// 内存管理常量定义
// ============================================================================

/**
 * @brief 动态数组的最小大小
 *
 * 详细说明：
 * 这个常量定义了动态数组的最小大小，用于防止频繁的小幅扩展。
 * 当数组大小小于这个值时，会直接扩展到这个大小。
 *
 * 设计考虑：
 * - 减少小数组的频繁重分配
 * - 为后续增长提供合理的起始大小
 * - 平衡内存使用和性能
 */
#define MINSIZEARRAY    4

/**
 * @brief 动态数组扩展函数（倍增策略）
 * @param L Lua状态机指针
 * @param block 要扩展的内存块指针
 * @param size 指向当前大小的指针，会被更新为新大小
 * @param size_elems 每个元素的大小（字节）
 * @param limit 数组大小的上限
 * @param errormsg 超出限制时的错误消息
 * @return 扩展后的内存块指针
 *
 * 详细说明：
 * 这是Lua中动态数组扩展的核心函数，实现了经典的倍增策略。
 * 该算法在内存使用和性能之间取得了良好的平衡。
 *
 * 倍增策略算法：
 * 1. 如果当前大小 >= 限制/2，则设置为限制值（最后一次扩展）
 * 2. 否则，将大小翻倍
 * 3. 如果翻倍后小于最小数组大小，则设置为最小大小
 * 4. 如果已达到限制，抛出错误
 *
 * 算法优势：
 * - 摊销时间复杂度：O(1)的平均插入时间
 * - 内存效率：避免过度的内存浪费
 * - 性能优化：减少重分配的频率
 * - 可预测性：增长模式可预测，便于调试
 *
 * 边界处理：
 * - 最小大小保证：防止过小的数组频繁扩展
 * - 上限检查：防止内存使用失控
 * - 最后扩展：接近上限时的优雅处理
 *
 * 错误处理：
 * 当数组大小达到限制时，抛出运行时错误，
 * 错误消息由调用者提供，便于定位问题。
 *
 * 内存安全：
 * 只有在新内存分配成功后才更新大小，
 * 确保在分配失败时原数组仍然有效。
 *
 * 使用场景：
 * - 表的数组部分扩展
 * - 字符串缓冲区扩展
 * - 函数原型的常量数组扩展
 * - 调用信息数组扩展
 *
 * 性能分析：
 * - 时间复杂度：O(n)单次操作，O(1)摊销
 * - 空间复杂度：O(n)
 * - 内存利用率：50%-100%
 *
 * @pre L、block、size必须是有效指针，size_elems > 0，limit > 0
 * @post *size被更新为新大小，返回新的内存块指针
 *
 * @note 这是动态数据结构的基础算法
 * @see luaM_reallocv(), MINSIZEARRAY
 */
void *luaM_growaux_(lua_State *L, void *block, int *size, size_t size_elems,
                    int limit, const char *errormsg) {
    void *newblock;
    int newsize;

    // 检查是否接近上限
    if (*size >= limit / 2) {
        if (*size >= limit) {
            // 已达到上限，抛出错误
            luaG_runerror(L, errormsg);
        }
        // 最后一次扩展，设置为上限
        newsize = limit;
    } else {
        // 标准倍增策略
        newsize = (*size) * 2;
        if (newsize < MINSIZEARRAY) {
            // 确保最小大小
            newsize = MINSIZEARRAY;
        }
    }

    // 重新分配内存
    newblock = luaM_reallocv(L, block, *size, newsize, size_elems);

    // 只有在成功分配后才更新大小
    *size = newsize;
    return newblock;
}

/**
 * @brief 内存块过大错误处理函数
 * @param L Lua状态机指针
 * @return 永远不会返回（函数会抛出异常）
 *
 * 详细说明：
 * 这个函数用于处理内存分配请求过大的情况。当请求的内存块
 * 大小超过系统限制时，调用此函数抛出运行时错误。
 *
 * 使用场景：
 * - 数组大小计算溢出检查
 * - 内存分配大小验证
 * - 防止恶意的大内存请求
 * - 系统资源保护
 *
 * 错误处理：
 * 函数会抛出Lua运行时错误，错误消息明确指出是内存分配
 * 大小问题，便于用户理解和调试。
 *
 * 安全性：
 * 通过及早检测和报告过大的内存请求，防止系统资源耗尽
 * 或整数溢出导致的安全问题。
 *
 * 返回值：
 * 函数永远不会正常返回，返回NULL只是为了满足编译器的
 * 类型检查要求。
 *
 * @pre L必须是有效的Lua状态机指针
 * @post 函数抛出运行时错误，不会正常返回
 *
 * @note 这是内存安全检查的重要组成部分
 * @see luaG_runerror(), 内存分配宏
 */
void *luaM_toobig(lua_State *L) {
    luaG_runerror(L, "memory allocation error: block too big");
    return NULL;    // 永远不会执行到这里
}



/**
 * @brief 通用内存重分配函数（Lua内存管理的核心）
 * @param L Lua状态机指针
 * @param block 要重新分配的内存块指针
 * @param osize 原始内存块大小
 * @param nsize 新的内存块大小
 * @return 重新分配后的内存块指针
 *
 * 详细说明：
 * 这是Lua内存管理系统的核心函数，所有内存分配操作最终都会
 * 通过这个函数进行。它提供了统一的内存管理接口，集成了
 * 错误处理和内存使用统计。
 *
 * 功能特性：
 * 1. 统一接口：封装用户提供的内存分配器
 * 2. 错误集成：内存分配失败时抛出Lua异常
 * 3. 使用统计：精确跟踪内存使用量
 * 4. 断言检查：确保调用约定的正确性
 *
 * 调用约定：
 * - (osize == 0) 当且仅当 (block == NULL)
 * - (nsize == 0) 当且仅当 (返回值 == NULL)
 * - 这些约定确保了接口的一致性和安全性
 *
 * 操作类型：
 * - 分配：block=NULL, osize=0, nsize>0
 * - 释放：block!=NULL, osize>0, nsize=0
 * - 重分配：block!=NULL, osize>0, nsize>0
 * - 无操作：block=NULL, osize=0, nsize=0
 *
 * 错误处理：
 * 当内存分配失败（返回NULL）且请求大小大于0时，
 * 抛出LUA_ERRMEM异常，触发Lua的错误处理机制。
 *
 * 内存统计：
 * 精确更新全局内存使用量统计，为垃圾回收器提供
 * 决策信息。计算公式：新总量 = 旧总量 - 旧大小 + 新大小
 *
 * 性能考虑：
 * - 最小化函数调用开销
 * - 直接调用用户分配器，避免额外抽象层
 * - 高效的内存统计更新
 *
 * 安全性：
 * - 断言检查确保调用约定
 * - 异常处理防止内存分配失败的传播
 * - 统计更新的原子性
 *
 * 垃圾回收集成：
 * 内存使用量统计直接影响垃圾回收的触发时机，
 * 实现了内存压力的自动感知。
 *
 * 用户分配器要求：
 * - 必须遵循realloc语义
 * - 缩小内存块不能失败
 * - 释放操作必须成功
 * - 分配失败时返回NULL
 *
 * @pre L必须是有效的Lua状态机，调用约定必须满足
 * @post 内存被重新分配，统计信息被更新
 *
 * @note 这是所有Lua内存操作的最终实现
 * @see luaM_malloc, luaM_free, luaM_realloc宏
 */
void *luaM_realloc_(lua_State *L, void *block, size_t osize, size_t nsize) {
    global_State *g = G(L);

    // 验证调用约定：空指针当且仅当大小为0
    lua_assert((osize == 0) == (block == NULL));

    // 调用用户提供的内存分配器
    block = (*g->frealloc)(g->ud, block, osize, nsize);

    // 检查分配失败的情况
    if (block == NULL && nsize > 0) {
        luaD_throw(L, LUA_ERRMEM);    // 抛出内存错误异常
    }

    // 验证返回值约定：空指针当且仅当新大小为0
    lua_assert((nsize == 0) == (block == NULL));

    // 更新全局内存使用统计
    g->totalbytes = (g->totalbytes - osize) + nsize;

    return block;
}

