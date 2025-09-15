/**
 * @file lmem.h
 * @brief Lua内存管理器接口：提供高效、安全的动态内存管理功能
 * 
 * 详细说明：
 * 这个头文件定义了Lua解释器的核心内存管理接口，实现了统一的内存分配、
 * 重分配和释放机制。它是Lua运行时系统的基础组件，负责管理所有动态
 * 分配的内存，包括字符串、表、函数、用户数据等Lua对象。
 * 
 * 系统架构定位：
 * - 位于Lua解释器的底层核心，为上层对象系统提供内存管理服务
 * - 与垃圾收集器(lgc)紧密配合，支持自动内存管理
 * - 与Lua状态机(lstate)集成，提供状态相关的内存管理
 * 
 * 技术特点：
 * - 统一的内存管理接口：所有内存操作都通过这个接口进行
 * - 智能的数组增长策略：支持动态数组的高效扩容
 * - 内存溢出保护：提供内存不足时的错误处理机制
 * - 类型安全的宏定义：使用强类型转换确保类型安全
 * - 高性能设计：最小化内存分配开销，优化内存使用模式
 * 
 * 依赖关系：
 * - stddef.h: 标准C库，提供size_t类型定义
 * - llimits.h: Lua限制定义，包含MAX_SIZET等常量
 * - lua.h: Lua核心头文件，定义lua_State和基础类型
 * 
 * 编译要求：
 * - C标准版本：C99或更高版本
 * - 编译器要求：支持可变参数宏和内联函数
 * - 平台要求：支持标准的malloc/realloc/free接口
 * 
 * 使用示例：
 * @code
 * #include "lmem.h"
 * #include "lua.h"
 * 
 * // 分配单个对象
 * MyStruct *obj = luaM_new(L, MyStruct);
 * if (obj == NULL) {
 *     // 内存分配失败，Lua会抛出异常
 * }
 * 
 * // 分配对象数组
 * int *array = luaM_newvector(L, 10, int);
 * 
 * // 动态增长数组
 * int current_size = 10;
 * int element_count = 15;
 * luaM_growvector(L, array, element_count, current_size, int, 
 *                 MAX_INT, "array too big");
 * 
 * // 释放内存
 * luaM_free(L, obj);
 * luaM_freearray(L, array, current_size, int);
 * @endcode
 * 
 * 内存安全考虑：
 * - 所有内存操作都与Lua状态机关联，支持错误恢复
 * - 内存分配失败时会触发Lua的错误处理机制
 * - 提供边界检查，防止整数溢出导致的内存分配错误
 * - 与垃圾收集器集成，自动管理对象生命周期
 * 
 * 性能特征：
 * - 时间复杂度：O(1)的内存分配和释放操作
 * - 空间复杂度：最小化内存碎片，优化内存使用效率
 * - 动态数组增长策略：按需扩容，减少重分配次数
 * - 类型转换优化：编译时类型检查，运行时零开销
 * 
 * 线程安全性：
 * - 非线程安全：每个lua_State实例独立管理内存
 * - 多线程使用：不同线程应使用不同的lua_State实例
 * - 同步要求：调用者负责多线程环境下的同步控制
 * 
 * 注意事项：
 * - 所有内存分配都可能触发垃圾收集，调用者需要保护C栈中的对象
 * - 内存分配失败会导致longjmp，不会返回NULL
 * - 数组增长操作可能会移动内存地址，调用后需要更新指针
 * - 释放操作必须提供正确的原始大小信息
 * 
 * @author Roberto Ierusalimschy
 * @version 1.31.1.1
 * @date 2007/12/27
 * @since Lua 5.1
 * @see lua.h, lgc.h, lstate.h
 */

#ifndef lmem_h
#define lmem_h

#include <stddef.h>

#include "llimits.h"
#include "lua.h"

/**
 * @brief 内存不足错误消息：当内存分配失败时显示的标准错误信息
 * 
 * 这个常量定义了Lua在内存分配失败时向用户显示的错误消息。
 * 它是一个标准的C字符串字面量，会被Lua的错误处理系统使用。
 */
#define MEMERRMSG    "not enough memory"

/**
 * @brief 安全的向量重分配宏：提供溢出检查的动态数组重分配功能
 * 
 * 详细说明：
 * 这个宏实现了安全的向量（数组）重分配操作，包含整数溢出检查。
 * 它在重分配前会验证新大小是否会导致整数溢出，如果安全则调用
 * 底层的重分配函数，否则触发"内存过大"错误。
 * 
 * 算法描述：
 * 1. 检查 (n+1) * e 是否会超过MAX_SIZET（防止整数溢出）
 * 2. 如果安全，调用luaM_realloc_进行实际的内存重分配
 * 3. 如果不安全，调用luaM_toobig触发内存过大错误
 * 
 * 安全机制：
 * - 整数溢出检查：防止size_t类型的算术溢出
 * - 边界验证：确保分配大小在合理范围内
 * - 错误处理：溢出时自动触发Lua错误处理机制
 * 
 * 性能优化：
 * - 编译时宏展开：零运行时开销的类型检查
 * - 条件表达式：避免函数调用的开销
 * - 内联计算：直接计算所需的内存大小
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param b 原始内存块指针，可以为NULL（表示新分配）
 * @param on 原始元素数量，用于计算原始大小
 * @param n 新的元素数量，用于计算新大小
 * @param e 单个元素的大小（字节数）
 * 
 * @return 重分配后的内存块指针，失败时不返回（会longjmp）
 * 
 * @note +1是为了避免编译器关于可能溢出的警告
 * @warning 这个宏可能会触发longjmp，调用者需要适当保护C栈
 */
#define luaM_reallocv(L, b, on, n, e) \
    ((cast(size_t, (n) + 1) <= MAX_SIZET / (e)) ?  /* +1 to avoid warnings */ \
        luaM_realloc_(L, (b), (on) * (e), (n) * (e)) : \
        luaM_toobig(L))

/**
 * @brief 释放指定大小的内存块：安全地释放已知大小的内存区域
 * 
 * 这个宏通过将新大小设置为0来释放内存块。它是luaM_realloc_的
 * 特殊用法，利用realloc(ptr, 0)等价于free(ptr)的标准行为。
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param b 要释放的内存块指针
 * @param s 内存块的原始大小（字节数）
 */
#define luaM_freemem(L, b, s)    luaM_realloc_(L, (b), (s), 0)

/**
 * @brief 释放单个对象：自动计算对象大小并释放内存
 * 
 * 这个宏自动计算指针所指向对象的大小，然后释放相应的内存。
 * 它使用sizeof运算符在编译时确定对象大小，提供类型安全的释放操作。
 * 
 * @param L lua_State指针，Lua虚拟机状态  
 * @param b 指向要释放对象的指针
 */
#define luaM_free(L, b)        luaM_realloc_(L, (b), sizeof(*(b)), 0)

/**
 * @brief 释放类型化数组：释放指定类型和数量的数组内存
 * 
 * 这个宏释放一个由指定数量的特定类型元素组成的数组。
 * 它通过luaM_reallocv宏实现，将新数量设置为0来释放内存。
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param b 指向数组的指针
 * @param n 数组中元素的数量
 * @param t 数组元素的类型
 */
#define luaM_freearray(L, b, n, t)    luaM_reallocv(L, (b), n, 0, sizeof(t))

/**
 * @brief 分配指定大小的内存：分配指定字节数的原始内存块
 * 
 * 这个宏分配一块指定大小的原始内存。它是luaM_realloc_的特殊用法，
 * 通过传递NULL指针和0作为原始大小来实现新的内存分配。
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param t 要分配的内存大小（字节数）
 * @return 指向新分配内存的void*指针
 */
#define luaM_malloc(L, t)    luaM_realloc_(L, NULL, 0, (t))

/**
 * @brief 分配并创建新对象：类型安全的单对象内存分配
 * 
 * 这个宏分配足够存储指定类型对象的内存，并返回正确类型的指针。
 * 它结合了内存分配和类型转换，提供了类型安全的对象创建接口。
 * 
 * 使用示例：
 * @code
 * typedef struct {
 *     int x, y;
 * } Point;
 * 
 * Point *p = luaM_new(L, Point);
 * p->x = 10;
 * p->y = 20;
 * @endcode
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param t 要创建的对象类型
 * @return 指向新对象的类型化指针
 */
#define luaM_new(L, t)        cast(t *, luaM_malloc(L, sizeof(t)))

/**
 * @brief 分配类型化数组：创建指定类型和数量的新数组
 * 
 * 这个宏分配一个包含指定数量元素的类型化数组，并返回正确类型的指针。
 * 它自动计算所需的总内存大小，并进行适当的类型转换。
 * 
 * 使用示例：
 * @code
 * int *numbers = luaM_newvector(L, 100, int);
 * for (int i = 0; i < 100; i++) {
 *     numbers[i] = i * i;
 * }
 * @endcode
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param n 数组中元素的数量
 * @param t 数组元素的类型
 * @return 指向新数组的类型化指针
 */
#define luaM_newvector(L, n, t) \
        cast(t *, luaM_reallocv(L, NULL, 0, n, sizeof(t)))

/**
 * @brief 智能数组增长宏：按需扩展动态数组的容量
 * 
 * 详细说明：
 * 这个宏实现了动态数组的智能增长策略。当当前元素数量超过数组容量时，
 * 它会自动扩展数组容量，避免频繁的重分配操作。这是Lua内部广泛使用的
 * 动态数据结构增长模式。
 * 
 * 算法策略：
 * 1. 检查是否需要扩容：(nelems)+1 > (size)
 * 2. 如果需要，调用luaM_growaux_计算新容量并重分配
 * 3. 更新数组指针和容量变量
 * 
 * 增长策略：
 * - 容量不足时触发扩容
 * - 通常采用倍增策略（具体由luaM_growaux_实现）
 * - 考虑最大容量限制，防止无限制增长
 * 
 * 性能优化：
 * - 摊销时间复杂度：O(1)的平均插入时间
 * - 减少重分配次数：批量扩容策略
 * - 内存局部性：连续内存分配提高缓存效率
 * 
 * 使用示例：
 * @code
 * int *array = NULL;
 * int capacity = 0;
 * int count = 0;
 * 
 * // 添加元素，自动扩容
 * for (int i = 0; i < 1000; i++) {
 *     luaM_growvector(L, array, count, capacity, int, 
 *                     INT_MAX, "too many elements");
 *     array[count++] = i;
 * }
 * @endcode
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param v 数组指针变量（会被修改）
 * @param nelems 当前元素数量
 * @param size 当前数组容量变量（会被修改）
 * @param t 数组元素类型
 * @param limit 最大允许的元素数量
 * @param e 超出限制时的错误消息
 * 
 * @warning 这个宏会修改v和size变量的值
 * @warning 扩容后原指针可能失效，必须使用新的v值
 */
#define luaM_growvector(L, v, nelems, size, t, limit, e) \
          if ((nelems) + 1 > (size)) \
            ((v) = cast(t *, luaM_growaux_(L, v, &(size), sizeof(t), limit, e)))

/**
 * @brief 重分配类型化数组：调整现有数组的大小
 * 
 * 这个宏重新分配一个类型化数组的大小，可以扩大或缩小数组。
 * 它保持数组中现有数据的完整性，只改变数组的总容量。
 * 
 * 使用场景：
 * - 精确调整数组大小以节省内存
 * - 根据实际需求缩减过大的数组
 * - 预分配已知大小的数组空间
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param v 数组指针变量（会被修改）
 * @param oldn 原始元素数量
 * @param n 新的元素数量
 * @param t 数组元素类型
 */
#define luaM_reallocvector(L, v, oldn, n, t) \
   ((v) = cast(t *, luaM_reallocv(L, v, oldn, n, sizeof(t))))

/**
 * @brief 底层内存重分配函数：Lua内存管理的核心实现
 * 
 * 详细说明：
 * 这是Lua内存管理系统的核心函数，实现了统一的内存分配、重分配和释放。
 * 所有的内存操作最终都会通过这个函数来执行。它与Lua的垃圾收集器
 * 紧密集成，支持内存压力检测和自动垃圾收集触发。
 * 
 * 功能特性：
 * - 统一接口：分配、重分配、释放都通过这一个函数
 * - 垃圾收集集成：内存分配时可能触发垃圾收集
 * - 内存统计：跟踪内存使用情况，支持内存限制
 * - 错误处理：分配失败时触发Lua异常机制
 * 
 * 参数语义：
 * - block=NULL, oldsize=0, size>0: 分配新内存
 * - block!=NULL, oldsize>0, size>0: 重分配现有内存
 * - block!=NULL, oldsize>0, size=0: 释放内存
 * 
 * 内存管理策略：
 * - 实际分配大小可能大于请求大小（内存对齐）
 * - 支持内存池和缓存机制
 * - 与系统malloc/realloc/free集成
 * 
 * 性能考虑：
 * - 最小化系统调用次数
 * - 优化小对象分配
 * - 减少内存碎片
 * 
 * @param L lua_State指针，Lua虚拟机状态，用于错误处理和内存统计
 * @param block 指向现有内存块的指针，新分配时为NULL
 * @param oldsize 现有内存块的大小，新分配时为0
 * @param size 请求的新内存大小，释放时为0
 * 
 * @return 指向分配或重分配内存的指针，释放时返回NULL
 * 
 * @note 分配失败时不返回NULL，而是触发Lua错误处理机制
 * @warning 这个函数可能触发垃圾收集，调用者需要保护C栈中的对象
 * @see luaM_toobig(), luaC_step()
 */
LUAI_FUNC void *luaM_realloc_(lua_State *L, void *block, size_t oldsize,
                                                          size_t size);

/**
 * @brief 内存过大错误处理：处理内存分配请求过大的情况
 * 
 * 详细说明：
 * 当内存分配请求的大小超过系统能够处理的范围时，这个函数会被调用。
 * 它负责生成适当的错误消息并触发Lua的错误处理机制。这是内存安全
 * 保护机制的一部分，防止整数溢出导致的安全问题。
 * 
 * 触发条件：
 * - 请求的内存大小超过MAX_SIZET
 * - 数组元素数量乘以元素大小导致溢出
 * - 系统无法分配如此大的连续内存
 * 
 * 错误处理：
 * - 生成标准的"内存不足"错误消息
 * - 通过luaG_runerror触发Lua异常
 * - 支持错误堆栈回溯和调试信息
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @return 此函数不会正常返回（会执行longjmp）
 * 
 * @warning 这个函数会执行longjmp，调用者无法从此函数正常返回
 * @see luaM_reallocv(), luaG_runerror()
 */
LUAI_FUNC void *luaM_toobig(lua_State *L);

/**
 * @brief 辅助数组增长函数：实现智能的数组容量增长策略
 * 
 * 详细说明：
 * 这个函数实现了Lua动态数组的智能增长算法。它根据当前大小、增长需求
 * 和系统限制来计算最优的新容量，并执行实际的内存重分配操作。
 * 这是luaM_growvector宏的底层实现。
 * 
 * 增长算法：
 * 1. 计算所需的最小新容量
 * 2. 应用增长策略（通常是倍增）
 * 3. 检查容量限制和内存限制
 * 4. 执行内存重分配
 * 5. 更新容量信息
 * 
 * 增长策略：
 * - 小数组：线性增长，避免过度分配
 * - 大数组：指数增长，减少重分配次数
 * - 容量限制：不超过指定的最大值
 * - 内存限制：考虑可用内存和系统限制
 * 
 * 性能优化：
 * - 摊销分析：平均O(1)的插入时间复杂度
 * - 内存预取：批量分配减少系统调用
 * - 对齐优化：考虑内存对齐要求
 * 
 * 错误处理：
 * - 容量超限：触发特定的错误消息
 * - 内存不足：触发标准的内存错误
 * - 参数验证：检查输入参数的有效性
 * 
 * @param L lua_State指针，Lua虚拟机状态
 * @param block 指向现有数组的指针，可能为NULL
 * @param size 指向当前容量的指针（会被修改为新容量）
 * @param size_elem 单个数组元素的大小（字节）
 * @param limit 数组的最大允许元素数量
 * @param errormsg 超出限制时显示的错误消息
 * 
 * @return 指向重分配后数组的指针
 * 
 * @warning 这个函数会修改size指向的值
 * @warning 返回的指针可能与输入的block不同
 * @see luaM_growvector(), luaM_realloc_()
 */
LUAI_FUNC void *luaM_growaux_(lua_State *L, void *block, int *size,
                               size_t size_elem, int limit,
                               const char *errormsg);

#endif

