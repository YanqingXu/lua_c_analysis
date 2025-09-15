/**
 * @file ltable.c
 * @brief Lua表系统实现：高性能混合数组+哈希表数据结构
 *
 * 详细说明：
 * 本文件实现了Lua语言的核心数据结构——表（Table）。Lua表是一种
 * 革命性的数据结构设计，将数组和哈希表完美融合，提供了极高的
 * 性能和灵活性。
 *
 * 核心设计理念：
 * Lua表采用混合存储策略，将元素分为两部分：
 * 1. 数组部分：存储非负整数键，提供O(1)的直接访问
 * 2. 哈希部分：存储其他类型的键，使用改进的散列算法
 *
 * 技术创新点：
 * - 自适应大小调整：根据使用模式动态调整数组和哈希部分的大小
 * - Brent变种散列：使用改进的开放寻址法，即使在100%负载因子下也保持高性能
 * - 内存优化：通过精确的大小计算，最小化内存浪费
 * - 缓存友好：数组部分的连续存储提供了优秀的缓存局部性
 *
 * 算法特点：
 * - 数组部分查找：O(1)时间复杂度
 * - 哈希部分查找：平均O(1)，最坏O(n)
 * - 动态重构：O(n)时间复杂度，但频率很低
 * - 空间效率：接近理论最优的内存使用率
 *
 * 性能保证：
 * 表的设计确保了即使在极端情况下（如100%哈希冲突）也能保持
 * 良好的性能。这是通过Brent变种算法实现的，该算法保证了
 * 主位置不变性：如果元素不在其主位置，那么冲突元素必在其主位置。
 *
 * 应用场景：
 * - 数组：高效的整数索引访问
 * - 字典：任意类型键值对存储
 * - 对象：面向对象编程的基础
 * - 模块：命名空间和作用域管理
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2007-12-28
 * @since Lua 5.0
 * @see lobject.h, lmem.h, lgc.h
 */

#include <math.h>
#include <string.h>

#define ltable_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"

/**
 * @brief 数组部分的最大位数限制
 *
 * 为了避免整数溢出和过度的内存分配，限制数组部分的最大大小。
 * 在32位系统上限制为30位（1GB），在64位系统上限制为26位（64MB）。
 */
#if LUAI_BITSINT > 26
#define MAXBITS     26
#else
#define MAXBITS     (LUAI_BITSINT - 2)
#endif

/**
 * @brief 数组部分的最大大小
 *
 * 通过位移操作计算最大数组大小，确保不会超出系统限制。
 */
#define MAXASIZE    (1 << MAXBITS)

// ============================================================================
// 哈希函数宏定义：针对不同数据类型的优化哈希策略
// ============================================================================

/**
 * @brief 2的幂次哈希函数
 * @param t 表指针
 * @param n 哈希值
 *
 * 当哈希表大小为2的幂次时使用，通过位运算实现快速取模。
 */
#define hashpow2(t, n)      (gnode(t, lmod((n), sizenode(t))))

/**
 * @brief 字符串哈希函数
 * @param t 表指针
 * @param str 字符串指针
 *
 * 利用字符串对象预计算的哈希值，避免重复计算。
 */
#define hashstr(t, str)     hashpow2(t, (str)->tsv.hash)

/**
 * @brief 布尔值哈希函数
 * @param t 表指针
 * @param p 布尔值
 *
 * 布尔值只有两个可能值，直接使用值作为哈希。
 */
#define hashboolean(t, p)   hashpow2(t, p)

/**
 * @brief 通用取模哈希函数
 * @param t 表指针
 * @param n 哈希值
 *
 * 对于非2的幂次大小，使用改进的取模运算。通过|1确保除数为奇数，
 * 避免某些类型（如指针）的2因子偏向问题。
 */
#define hashmod(t, n)       (gnode(t, ((n) % ((sizenode(t) - 1) | 1))))

/**
 * @brief 指针哈希函数
 * @param t 表指针
 * @param p 指针值
 *
 * 将指针转换为整数后进行哈希，适用于轻量用户数据和其他对象。
 */
#define hashpointer(t, p)   hashmod(t, IntPoint(p))

/**
 * @brief lua_Number中包含的int数量
 *
 * 用于数值哈希算法，将浮点数分解为多个整数进行哈希计算。
 */
#define numints             cast_int(sizeof(lua_Number) / sizeof(int))

/**
 * @brief 虚拟节点指针
 *
 * 指向全局唯一的虚拟节点，用于表示空的哈希表状态。
 */
#define dummynode           (&dummynode_)

/**
 * @brief 全局虚拟节点实例
 *
 * 详细说明：
 * 这是一个特殊的节点，用于表示空的哈希表状态。当表没有哈希部分时，
 * 所有哈希操作都会指向这个虚拟节点，避免了空指针检查的开销。
 *
 * 结构说明：
 * - value: 设置为nil，表示无效值
 * - key: 设置为nil，表示无效键
 * - next: 设置为NULL，表示链表结束
 *
 * 设计优势：
 * 使用虚拟节点可以简化代码逻辑，所有哈希操作都可以统一处理，
 * 无需特殊判断空表的情况。
 */
static const Node dummynode_ = {
    {{NULL}, LUA_TNIL},         // 值部分：nil值
    {{{NULL}, LUA_TNIL, NULL}}  // 键部分：nil键，无下一个节点
};

/**
 * @brief 计算数值的哈希位置
 * @param t 表指针
 * @param n 要哈希的数值
 * @return 对应的哈希节点指针
 *
 * 详细说明：
 * 数值哈希是表系统中最复杂的哈希算法之一，需要处理浮点数的
 * 特殊性质，如-0和+0的等价性、NaN的处理等。
 *
 * 算法步骤：
 * 1. 特殊处理0值：将-0和+0都映射到位置0
 * 2. 位级分解：将浮点数按字节复制到整数数组
 * 3. 累加混合：将所有整数部分累加到第一个元素
 * 4. 取模映射：使用改进的取模算法映射到哈希表
 *
 * 设计考虑：
 * - 处理IEEE 754浮点数的特殊值
 * - 确保相等的数值产生相同的哈希值
 * - 尽可能减少哈希冲突
 *
 * 性能特征：
 * - 时间复杂度：O(1)
 * - 空间复杂度：O(1)
 * - 哈希质量：良好的分布特性
 */
static Node *hashnum(const Table *t, lua_Number n) {
    unsigned int a[numints];
    int i;

    // 特殊处理：将-0和+0都映射到位置0
    if (luai_numeq(n, 0)) {
        return gnode(t, 0);
    }

    // 将浮点数按位复制到整数数组
    memcpy(a, &n, sizeof(a));

    // 将所有整数部分累加到第一个元素，增加哈希的随机性
    for (i = 1; i < numints; i++) {
        a[0] += a[i];
    }

    // 使用改进的取模算法计算最终位置
    return hashmod(t, a[0]);
}

/**
 * @brief 计算键的主位置（哈希位置）
 * @param t 表指针
 * @param key 键值指针
 * @return 对应的主哈希节点指针
 *
 * 详细说明：
 * 这是表系统的核心函数之一，负责根据键的类型选择合适的哈希算法。
 * 不同类型的键使用不同的哈希策略，以获得最佳的性能和分布。
 *
 * 类型特化哈希：
 * - 数值：使用专门的浮点数哈希算法
 * - 字符串：利用预计算的哈希值
 * - 布尔值：直接使用值作为哈希
 * - 轻量用户数据：使用指针哈希
 * - 其他对象：使用垃圾回收对象的指针哈希
 *
 * 设计原则：
 * 每种类型都有针对性的优化，确保哈希值的均匀分布和计算效率。
 *
 * 性能保证：
 * 所有哈希计算都是O(1)时间复杂度，为表操作提供了坚实的性能基础。
 */
static Node *mainposition(const Table *t, const TValue *key) {
    switch (ttype(key)) {
        case LUA_TNUMBER:
            return hashnum(t, nvalue(key));

        case LUA_TSTRING:
            return hashstr(t, rawtsvalue(key));

        case LUA_TBOOLEAN:
            return hashboolean(t, bvalue(key));

        case LUA_TLIGHTUSERDATA:
            return hashpointer(t, pvalue(key));

        default:
            return hashpointer(t, gcvalue(key));
    }
}

/**
 * @brief 判断键是否适合存储在数组部分
 * @param key 键值指针
 * @return 如果适合数组部分则返回数组索引，否则返回-1
 *
 * 详细说明：
 * 这个函数实现了Lua表的核心优化策略：将合适的键存储在数组部分
 * 以获得更好的性能和内存效率。
 *
 * 数组部分条件：
 * 1. 键必须是数值类型
 * 2. 数值必须是正整数（包括1）
 * 3. 数值必须在合理的范围内（不超过MAXASIZE）
 * 4. 数值转换为整数后必须相等（排除小数）
 *
 * 优化效果：
 * 数组部分提供O(1)的直接访问，比哈希查找更快，且内存使用更少。
 *
 * 边界处理：
 * 仔细处理浮点数到整数的转换，确保只有真正的整数才被接受。
 */
static int arrayindex(const TValue *key) {
    if (ttisnumber(key)) {
        lua_Number n = nvalue(key);
        int k;
        lua_number2int(k, n);
        // 检查转换后的整数是否与原数值相等
        if (luai_numeq(cast_num(k), n)) {
            return k;
        }
    }
    return -1;    // 键不满足数组部分的条件
}

/**
 * @brief 查找键在表遍历中的索引位置
 * @param L Lua状态机指针
 * @param t 表指针
 * @param key 要查找的键
 * @return 键的遍历索引，如果未找到则抛出错误
 *
 * 详细说明：
 * 这个函数实现了Lua表遍历的核心逻辑，为next()函数提供支持。
 * 表遍历按照特定的顺序：先遍历数组部分，再遍历哈希部分。
 *
 * 遍历顺序：
 * 1. 数组部分：按索引从1到sizearray的顺序
 * 2. 哈希部分：按哈希表中节点的物理顺序
 *
 * 索引编码：
 * - 数组部分：索引为0到sizearray-1（C风格索引）
 * - 哈希部分：索引为sizearray到sizearray+sizenode-1
 * - 特殊值-1：表示遍历开始（对应nil键）
 *
 * 死键处理：
 * 在垃圾回收过程中，键可能变为"死键"，但在遍历中仍然有效。
 * 函数会正确处理这种情况，确保遍历的连续性。
 *
 * 错误处理：
 * 如果提供的键在表中不存在，函数会抛出运行时错误，
 * 这通常表示程序逻辑错误。
 *
 * 性能特征：
 * - 数组部分查找：O(1)
 * - 哈希部分查找：O(链长度)，平均O(1)
 */
static int findindex(lua_State *L, Table *t, StkId key) {
    int i;

    // 特殊情况：nil键表示遍历开始
    if (ttisnil(key)) {
        return -1;
    }

    // 首先检查是否在数组部分
    i = arrayindex(key);
    if (0 < i && i <= t->sizearray) {
        // 在数组部分，返回C风格索引（从0开始）
        return i - 1;
    } else {
        // 在哈希部分，需要遍历冲突链查找
        Node *n = mainposition(t, key);
        do {
            // 检查当前节点是否匹配
            // 注意：键可能已经"死亡"，但在遍历中仍然有效
            if (luaO_rawequalObj(key2tval(n), key) ||
                (ttype(gkey(n)) == LUA_TDEADKEY && iscollectable(key) &&
                 gcvalue(gkey(n)) == gcvalue(key))) {

                // 计算节点在哈希表中的索引
                i = cast_int(n - gnode(t, 0));

                // 哈希元素的索引在数组元素之后
                return i + t->sizearray;
            } else {
                n = gnext(n);
            }
        } while (n);

        // 键未找到，这是一个错误
        luaG_runerror(L, "invalid key to " LUA_QL("next"));
        return 0;    // 避免编译器警告
    }
}


/**
 * @brief 表遍历函数：获取表中的下一个键值对
 * @param L Lua状态机指针
 * @param t 要遍历的表
 * @param key 栈上的键位置，输入当前键，输出下一个键
 * @return 如果找到下一个元素返回1，遍历结束返回0
 *
 * 详细说明：
 * 这是Lua表遍历的核心实现，为pairs()和next()函数提供支持。
 * 函数实现了有序遍历：先遍历数组部分，再遍历哈希部分。
 *
 * 遍历策略：
 * 1. 使用findindex找到当前键的位置
 * 2. 从下一个位置开始查找非nil值
 * 3. 数组部分：按索引顺序遍历
 * 4. 哈希部分：按物理存储顺序遍历
 *
 * 输出格式：
 * - key位置：存储找到的键
 * - key+1位置：存储对应的值
 *
 * 数组部分处理：
 * 数组索引从1开始（Lua风格），在栈上表示为数值类型。
 * 只返回非nil的值，跳过nil元素。
 *
 * 哈希部分处理：
 * 按哈希表的物理存储顺序遍历，不保证键的顺序。
 * 同样只返回非nil的值。
 *
 * 性能特征：
 * - 时间复杂度：O(1)平均情况，O(n)最坏情况
 * - 空间复杂度：O(1)
 * - 遍历顺序：确定性但不保证键的逻辑顺序
 *
 * 使用场景：
 * - pairs()函数的底层实现
 * - 表的序列化和调试
 * - 通用的表遍历操作
 *
 * @note 遍历过程中修改表可能导致未定义行为
 * @see findindex(), pairs()
 */
int luaH_next(lua_State *L, Table *t, StkId key) {
    // 找到当前键的索引位置
    int i = findindex(L, t, key);

    // 首先遍历数组部分
    for (i++; i < t->sizearray; i++) {
        if (!ttisnil(&t->array[i])) {
            // 找到非nil值，设置键（Lua索引从1开始）
            setnvalue(key, cast_num(i + 1));
            // 设置对应的值
            setobj2s(L, key + 1, &t->array[i]);
            return 1;
        }
    }

    // 然后遍历哈希部分
    for (i -= t->sizearray; i < sizenode(t); i++) {
        if (!ttisnil(gval(gnode(t, i)))) {
            // 找到非nil值，设置键
            setobj2s(L, key, key2tval(gnode(t, i)));
            // 设置对应的值
            setobj2s(L, key + 1, gval(gnode(t, i)));
            return 1;
        }
    }

    return 0;    // 没有更多元素
}


// ============================================================================
// 重哈希算法：Lua表动态调整的核心实现
// ============================================================================

/**
 * @brief 计算最优的数组大小
 * @param nums 各个2的幂次区间的元素计数数组
 * @param narray 输入：候选数组大小，输出：最优数组大小
 * @return 将要放入数组部分的元素数量
 *
 * 详细说明：
 * 这是Lua表自适应大小调整的核心算法，实现了著名的"50%规则"：
 * 数组的实际大小应该是最大的2的幂次n，使得至少有一半的位置
 * （从1到n）被使用。
 *
 * 算法原理：
 * 1. 遍历所有可能的2的幂次大小
 * 2. 对每个大小，计算会被使用的位置数量
 * 3. 如果使用率超过50%，则更新最优大小
 * 4. 选择满足条件的最大大小作为最终结果
 *
 * 50%规则的优势：
 * - 内存效率：避免过度的内存浪费
 * - 性能保证：确保数组访问的高效性
 * - 自适应性：根据实际使用模式调整大小
 *
 * 输入数据结构：
 * nums[i]表示在区间[2^(i-1)+1, 2^i]中的整数键数量
 * 这种分组方式便于快速计算不同数组大小的使用率
 *
 * 算法复杂度：
 * - 时间复杂度：O(log(max_key))
 * - 空间复杂度：O(1)
 *
 * 设计考虑：
 * 算法确保了数组部分的高效利用，同时避免了频繁的重新分配。
 *
 * @pre nums数组包含有效的元素计数
 * @post narray包含最优的数组大小
 *
 * @note 这是Lua表性能优化的关键算法
 * @see luaH_resize(), countint()
 */
static int computesizes(int nums[], int *narray) {
    int i;
    int twotoi;    // 当前考虑的2的幂次：2^i
    int a = 0;     // 小于2^i的元素总数
    int na = 0;    // 将要放入数组部分的元素数
    int n = 0;     // 当前最优的数组大小

    // 遍历所有可能的2的幂次大小
    for (i = 0, twotoi = 1; twotoi / 2 < *narray; i++, twotoi *= 2) {
        if (nums[i] > 0) {
            a += nums[i];

            // 检查50%规则：使用的位置是否超过一半
            if (a > twotoi / 2) {
                n = twotoi;     // 更新最优大小
                na = a;         // 更新将要放入数组的元素数
            }
        }

        // 优化：如果所有元素都已计算，提前退出
        if (a == *narray) {
            break;
        }
    }

    *narray = n;
    lua_assert(*narray / 2 <= na && na <= *narray);
    return na;
}


/**
 * @brief 统计整数键并分类到相应的2的幂次区间
 * @param key 要检查的键
 * @param nums 各个2的幂次区间的计数数组
 * @return 如果是有效的数组索引返回1，否则返回0
 *
 * 详细说明：
 * 这个函数是重哈希算法的重要组成部分，负责将整数键分类到
 * 不同的2的幂次区间中，为后续的大小计算提供数据。
 *
 * 分类策略：
 * 对于有效的数组索引k，将其分类到区间[2^(i-1)+1, 2^i]，
 * 其中i = ceil(log2(k))。这种分类方式便于后续计算不同
 * 数组大小的使用率。
 *
 * 有效性检查：
 * - 键必须是正整数
 * - 键必须在合理范围内（不超过MAXASIZE）
 * - 键必须能精确转换为整数（排除小数）
 *
 * 计数更新：
 * 使用ceillog2(k)计算键k所属的区间索引，然后增加
 * 对应区间的计数。
 *
 * 算法效率：
 * - 时间复杂度：O(1)
 * - 空间复杂度：O(1)
 *
 * @pre key是有效的TValue指针，nums是有效的计数数组
 * @post 如果key是有效数组索引，相应区间的计数增加1
 *
 * @note 这是重哈希算法数据收集的关键步骤
 * @see arrayindex(), computesizes(), ceillog2()
 */
static int countint(const TValue *key, int *nums) {
    int k = arrayindex(key);

    // 检查是否是有效的数组索引
    if (0 < k && k <= MAXASIZE) {
        // 计算所属的2的幂次区间并增加计数
        nums[ceillog2(k)]++;
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief 统计表的数组部分中的有效元素
 * @param t 要统计的表
 * @param nums 各个2的幂次区间的计数数组
 * @return 数组部分中非nil元素的总数
 *
 * 详细说明：
 * 这个函数遍历表的数组部分，统计所有非nil元素，并将它们
 * 按照索引值分类到不同的2的幂次区间中。
 *
 * 统计策略：
 * 1. 遍历数组部分的所有位置
 * 2. 对每个非nil元素，将其索引分类到相应区间
 * 3. 累计总的非nil元素数量
 *
 * 区间分类：
 * 数组索引i+1（Lua风格）被分类到区间[2^(j-1)+1, 2^j]，
 * 其中j = ceil(log2(i+1))。
 *
 * 性能考虑：
 * 函数只遍历一次数组，时间复杂度为O(数组大小)。
 *
 * @pre t是有效的表指针，nums是有效的计数数组
 * @post nums包含数组部分元素的区间分布统计
 *
 * @note 这是重哈希算法数据收集的重要组成部分
 * @see countint(), computesizes()
 */
static int numusearray(const Table *t, int *nums) {
    int lg;
    int ttlg;       // 2^lg：当前区间的上界
    int ause = 0;   // 数组部分的有效元素总数
    int i = 1;      // 遍历数组的索引（Lua风格，从1开始）

    // 按2的幂次区间遍历数组
    for (lg = 0, ttlg = 1; lg <= MAXBITS; lg++, ttlg *= 2) {
        int lc = 0;     // 当前区间的元素计数
        int lim = ttlg; // 当前区间的上界

        // 调整上界，不超过实际数组大小
        if (lim > t->sizearray) {
            lim = t->sizearray;
            if (i > lim) {
                break;  // 没有更多元素需要统计
            }
        }

        // 统计区间(2^(lg-1), 2^lg]中的元素
        for (; i <= lim; i++) {
            if (!ttisnil(&t->array[i - 1])) {
                lc++;
            }
        }

        nums[lg] += lc;     // 更新区间计数
        ause += lc;         // 更新总计数
    }

    return ause;
}

/**
 * @brief 统计表的哈希部分中的有效元素
 * @param t 要统计的表
 * @param nums 各个2的幂次区间的计数数组
 * @param pnasize 指向候选数组大小的指针，会被更新
 * @return 哈希部分中非nil元素的总数
 *
 * 详细说明：
 * 这个函数遍历表的哈希部分，统计所有非nil元素。对于那些
 * 可以作为数组索引的整数键，将它们分类到相应的2的幂次区间中。
 *
 * 双重统计：
 * 1. totaluse：统计哈希部分的所有非nil元素
 * 2. ause：统计哈希部分中可以移到数组部分的整数键元素
 *
 * 重分类逻辑：
 * 哈希部分中的整数键可能在重哈希后移动到数组部分，
 * 这些键需要被计入数组大小的计算中。
 *
 * 性能考虑：
 * 函数遍历整个哈希表，时间复杂度为O(哈希表大小)。
 *
 * 输出更新：
 * pnasize会增加哈希部分中可以移到数组部分的元素数量。
 *
 * @pre t是有效的表指针，nums和pnasize是有效指针
 * @post nums包含哈希部分整数键的区间分布，pnasize被更新
 *
 * @note 这是重哈希算法中处理哈希部分的关键函数
 * @see countint(), numusearray()
 */
static int numusehash(const Table *t, int *nums, int *pnasize) {
    int totaluse = 0;   // 哈希部分的总元素数
    int ause = 0;       // 可移到数组部分的元素数
    int i = sizenode(t);

    // 遍历哈希表的所有节点
    while (i--) {
        Node *n = &t->node[i];
        if (!ttisnil(gval(n))) {
            // 检查键是否可以作为数组索引
            ause += countint(key2tval(n), nums);
            totaluse++;
        }
    }

    *pnasize += ause;   // 更新候选数组大小
    return totaluse;
}


/**
 * @brief 设置表的数组部分大小
 * @param L Lua状态机指针
 * @param t 要调整的表
 * @param size 新的数组大小
 *
 * 详细说明：
 * 这个函数负责调整表的数组部分大小，包括内存重新分配和
 * 新增位置的初始化。这是表动态调整的核心操作之一。
 *
 * 操作步骤：
 * 1. 重新分配数组内存到指定大小
 * 2. 将新增的位置初始化为nil
 * 3. 更新表的数组大小记录
 *
 * 内存管理：
 * 使用luaM_reallocvector进行安全的内存重新分配，
 * 该函数会处理内存不足等异常情况。
 *
 * 初始化策略：
 * 只初始化新增的位置，保持原有数据不变。
 * 这样可以在扩展时保持数据的完整性。
 *
 * 性能考虑：
 * - 时间复杂度：O(新增大小)
 * - 空间复杂度：O(总大小)
 *
 * @pre L和t必须是有效指针，size必须是合理的大小
 * @post 表的数组部分被调整到指定大小，新位置被初始化
 *
 * @note 这是表内存管理的关键函数
 * @see luaM_reallocvector(), setnodevector()
 */
static void setarrayvector(lua_State *L, Table *t, int size) {
    int i;

    // 重新分配数组内存
    luaM_reallocvector(L, t->array, t->sizearray, size, TValue);

    // 初始化新增的位置为nil
    for (i = t->sizearray; i < size; i++) {
        setnilvalue(&t->array[i]);
    }

    // 更新数组大小
    t->sizearray = size;
}

/**
 * @brief 设置表的哈希部分大小
 * @param L Lua状态机指针
 * @param t 要调整的表
 * @param size 新的哈希表大小（实际大小会调整为2的幂次）
 *
 * 详细说明：
 * 这个函数负责设置表的哈希部分，包括特殊的空表处理和
 * 正常哈希表的创建。哈希表大小总是2的幂次，以优化哈希计算。
 *
 * 空表优化：
 * 当size为0时，使用全局的dummynode，避免内存分配开销。
 * 这是一个重要的内存优化，因为很多表可能不需要哈希部分。
 *
 * 大小调整：
 * 将请求的大小向上调整到最近的2的幂次，这样可以使用
 * 高效的位运算进行哈希计算。
 *
 * 初始化过程：
 * 1. 计算实际需要的大小（2的幂次）
 * 2. 分配内存并创建节点数组
 * 3. 初始化所有节点为空状态
 *
 * 溢出检查：
 * 检查计算出的大小是否超过系统限制，防止内存溢出。
 *
 * 性能特征：
 * - 时间复杂度：O(实际大小)
 * - 空间复杂度：O(实际大小)
 *
 * @pre L和t必须是有效指针，size必须是非负数
 * @post 表的哈希部分被设置为指定大小，所有节点被初始化
 *
 * @note 哈希表大小总是2的幂次，以优化性能
 * @see setarrayvector(), luaM_newvector(), dummynode
 */
static void setnodevector(lua_State *L, Table *t, int size) {
    int lsize;

    if (size == 0) {
        // 空哈希表：使用共享的虚拟节点
        t->node = cast(Node *, dummynode);
        lsize = 0;
    } else {
        int i;

        // 计算实际大小（向上调整到2的幂次）
        lsize = ceillog2(size);
        if (lsize > MAXBITS) {
            luaG_runerror(L, "table overflow");
        }

        size = twoto(lsize);    // 2^lsize

        // 分配哈希表内存
        t->node = luaM_newvector(L, size, Node);

        // 初始化所有节点
        for (i = 0; i < size; i++) {
            Node *n = gnode(t, i);
            gnext(n) = NULL;        // 无下一个节点
            setnilvalue(gkey(n));   // 键为nil
            setnilvalue(gval(n));   // 值为nil
        }
    }
    t->lsizenode = cast_byte(lsize);
    t->lastfree = gnode(t, size);    // 所有位置都是空闲的
}

/**
 * @brief 调整表的大小（数组部分和哈希部分）
 * @param L Lua状态机指针
 * @param t 要调整的表
 * @param nasize 新的数组部分大小
 * @param nhsize 新的哈希部分大小
 *
 * 详细说明：
 * 这是表系统最复杂的函数之一，负责完整的表重构。它需要
 * 处理数组和哈希部分的同时调整，并确保所有数据的正确迁移。
 *
 * 调整策略：
 * 1. 如果数组需要扩展，先扩展数组部分
 * 2. 创建新的哈希部分
 * 3. 如果数组需要收缩，将多余元素迁移到哈希部分
 * 4. 将旧哈希部分的所有元素重新插入
 * 5. 释放旧的哈希部分内存
 *
 * 数据迁移：
 * - 数组收缩：将超出新大小的元素移到哈希部分
 * - 哈希重建：所有哈希元素需要重新计算位置并插入
 *
 * 内存管理：
 * 函数确保在整个过程中不会丢失数据，即使在内存分配
 * 失败的情况下也能保持表的一致性。
 *
 * 性能考虑：
 * - 时间复杂度：O(总元素数)
 * - 空间复杂度：O(新表大小)
 * - 这是一个昂贵的操作，但频率相对较低
 *
 * 顺序重要性：
 * 操作顺序经过精心设计，确保在任何时刻表都处于有效状态。
 *
 * @pre L和t必须是有效指针，nasize和nhsize必须是合理大小
 * @post 表被调整到新大小，所有数据正确迁移
 *
 * @note 这是表动态调整的核心实现
 * @see setarrayvector(), setnodevector(), luaH_set()
 */
static void resize(lua_State *L, Table *t, int nasize, int nhsize) {
    int i;
    int oldasize = t->sizearray;
    int oldhsize = t->lsizenode;
    Node *nold = t->node;    // 保存旧的哈希表

    // 如果数组部分需要扩展，先扩展
    if (nasize > oldasize) {
        setarrayvector(L, t, nasize);
    }

    // 创建新的哈希部分
    setnodevector(L, t, nhsize);

    // 如果数组部分需要收缩
    if (nasize < oldasize) {
        t->sizearray = nasize;

        // 将消失部分的元素重新插入到哈希部分
        for (i = nasize; i < oldasize; i++) {
            if (!ttisnil(&t->array[i])) {
                setobjt2t(L, luaH_setnum(L, t, i + 1), &t->array[i]);
            }
        }

        // 收缩数组
        luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
    }

    // 重新插入旧哈希部分的所有元素
    for (i = twoto(oldhsize) - 1; i >= 0; i--) {
        Node *old = nold + i;
        if (!ttisnil(gval(old))) {
            setobjt2t(L, luaH_set(L, t, key2tval(old)), gval(old));
        }
    }

    // 释放旧的哈希表内存
    if (nold != dummynode) {
        luaM_freearray(L, nold, twoto(oldhsize), Node);
    }
}


/**
 * @brief 仅调整表的数组部分大小
 * @param L Lua状态机指针
 * @param t 要调整的表
 * @param nasize 新的数组大小
 *
 * 详细说明：
 * 这是resize函数的简化版本，只调整数组部分的大小，
 * 保持哈希部分不变。主要用于数组操作的优化。
 *
 * 使用场景：
 * - 表主要用作数组时的大小调整
 * - 避免不必要的哈希部分重建
 * - 提供更精确的内存控制
 *
 * 实现策略：
 * 保持哈希部分的当前大小，只调整数组部分。
 *
 * @note 这是数组优化的专用接口
 * @see resize()
 */
void luaH_resizearray(lua_State *L, Table *t, int nasize) {
    int nsize = (t->node == dummynode) ? 0 : sizenode(t);
    resize(L, t, nasize, nsize);
}

/**
 * @brief 重哈希表以容纳新的键
 * @param L Lua状态机指针
 * @param t 要重哈希的表
 * @param ek 触发重哈希的额外键
 *
 * 详细说明：
 * 这是表自适应调整的核心函数，当表需要插入新键但空间不足时
 * 被调用。它会分析当前表的使用模式，计算最优的新大小。
 *
 * 重哈希触发条件：
 * - 哈希表已满，需要插入新的非整数键
 * - 数组部分使用模式发生变化
 * - 需要优化内存使用
 *
 * 算法步骤：
 * 1. 统计当前表中各类型键的分布
 * 2. 包含即将插入的新键进行计算
 * 3. 使用50%规则计算最优数组大小
 * 4. 计算哈希部分所需大小
 * 5. 执行表重构
 *
 * 优化策略：
 * 函数会尝试将尽可能多的整数键放入数组部分，
 * 以获得更好的性能和内存效率。
 *
 * 性能影响：
 * 重哈希是一个昂贵的操作，但通过精确的大小计算，
 * 可以最小化重哈希的频率。
 *
 * @pre L、t和ek必须是有效指针
 * @post 表被重构为最优大小，能够容纳新键
 *
 * @note 这是表性能优化的关键函数
 * @see computesizes(), numusearray(), numusehash()
 */
static void rehash(lua_State *L, Table *t, const TValue *ek) {
    int nasize, na;
    int nums[MAXBITS + 1];    // nums[i] = 区间[2^(i-1), 2^i]中的键数量
    int i;
    int totaluse;

    // 重置计数数组
    for (i = 0; i <= MAXBITS; i++) {
        nums[i] = 0;
    }

    // 统计数组部分的键
    nasize = numusearray(t, nums);
    totaluse = nasize;    // 所有这些键都是整数键

    // 统计哈希部分的键
    totaluse += numusehash(t, nums, &nasize);

    // 统计额外的键
    nasize += countint(ek, nums);
    totaluse++;

    // 计算数组部分的新大小
    na = computesizes(nums, &nasize);

    // 调整表到新的计算大小
    resize(L, t, nasize, totaluse - na);
}

// ============================================================================
// 表创建和管理：公共接口函数
// ============================================================================

/**
 * @brief 创建新的Lua表
 * @param L Lua状态机指针
 * @param narray 数组部分的初始大小提示
 * @param nhash 哈希部分的初始大小提示
 * @return 新创建的表指针
 *
 * 详细说明：
 * 这是Lua表的主要创建函数，根据提供的大小提示创建优化的表结构。
 * 大小提示帮助避免初期的重哈希操作。
 *
 * 创建过程：
 * 1. 分配表结构内存
 * 2. 链接到垃圾回收器
 * 3. 初始化表的基本属性
 * 4. 根据提示设置初始大小
 *
 * 初始化状态：
 * - 元表：NULL（无元表）
 * - 标志：全部设置（表示所有元方法都可能存在）
 * - 数组和哈希部分：根据提示分配
 *
 * 内存安全：
 * 如果内存分配失败，函数会抛出异常，确保不会返回无效的表。
 *
 * 性能优化：
 * 通过合理的初始大小设置，可以显著减少后续的重哈希操作。
 *
 * @pre L必须是有效的Lua状态机指针
 * @post 返回完全初始化的新表
 *
 * @note 这是表创建的主要接口
 * @see luaC_link(), setarrayvector(), setnodevector()
 */
Table *luaH_new(lua_State *L, int narray, int nhash) {
    Table *t = luaM_new(L, Table);
    luaC_link(L, obj2gco(t), LUA_TTABLE);
    t->metatable = NULL;
    t->flags = cast_byte(~0);

    // 临时值（只有在某些malloc失败时才保留）
    t->array = NULL;
    t->sizearray = 0;
    t->lsizenode = 0;
    t->node = cast(Node *, dummynode);

    // 设置初始大小
    setarrayvector(L, t, narray);
    setnodevector(L, t, nhash);

    return t;
}

/**
 * @brief 释放表的所有内存
 * @param L Lua状态机指针
 * @param t 要释放的表
 *
 * 详细说明：
 * 这个函数负责完全释放表占用的所有内存，包括数组部分、
 * 哈希部分和表结构本身。
 *
 * 释放顺序：
 * 1. 释放哈希部分（如果不是虚拟节点）
 * 2. 释放数组部分
 * 3. 释放表结构本身
 *
 * 特殊处理：
 * 检查哈希部分是否为dummynode，避免释放共享的虚拟节点。
 *
 * 内存安全：
 * 确保所有动态分配的内存都被正确释放，防止内存泄漏。
 *
 * @pre L和t必须是有效指针
 * @post 表的所有内存被释放
 *
 * @note 这是表生命周期的最后阶段
 * @see luaM_freearray(), luaM_free()
 */
void luaH_free(lua_State *L, Table *t) {
    // 释放哈希部分（如果不是虚拟节点）
    if (t->node != dummynode) {
        luaM_freearray(L, t->node, sizenode(t), Node);
    }

    // 释放数组部分
    luaM_freearray(L, t->array, t->sizearray, TValue);

    // 释放表结构本身
    luaM_free(L, t);
}

/**
 * @brief 在哈希表中查找空闲位置
 * @param t 要搜索的表
 * @return 找到的空闲节点指针，如果没有空闲位置则返回NULL
 *
 * 详细说明：
 * 这个函数实现了高效的空闲位置查找策略。它从哈希表的末尾
 * 开始向前搜索，利用了大多数插入操作的局部性特征。
 *
 * 搜索策略：
 * 从lastfree指针开始向前搜索，lastfree总是指向已知的
 * 最后一个空闲位置，这样可以避免重复搜索已知的非空区域。
 *
 * 优化原理：
 * - 新插入的元素通常在表的后部
 * - 向前搜索可以快速找到空闲位置
 * - lastfree指针避免了重复搜索
 *
 * 性能特征：
 * - 平均情况：O(1)
 * - 最坏情况：O(表大小)
 * - 实际性能通常很好，因为表的负载因子控制
 *
 * 返回值：
 * - 成功：返回空闲节点指针
 * - 失败：返回NULL，表示需要重哈希
 *
 * @pre t必须是有效的表指针
 * @post lastfree指针被更新到新的位置
 *
 * @note 这是哈希表空间管理的关键函数
 * @see newkey(), rehash()
 */
static Node *getfreepos(Table *t) {
    // 从lastfree开始向前搜索空闲位置
    while (t->lastfree-- > t->node) {
        if (ttisnil(gkey(t->lastfree))) {
            return t->lastfree;
        }
    }

    return NULL;    // 找不到空闲位置
}



/**
 * @brief 在哈希表中插入新键（Brent变种散列算法的核心实现）
 * @param L Lua状态机指针
 * @param t 目标表
 * @param key 要插入的键
 * @return 指向新键对应值位置的指针
 *
 * 详细说明：
 * 这是Lua表系统最复杂和最重要的函数之一，实现了著名的Brent变种
 * 散列算法。该算法确保了即使在100%负载因子下也能保持良好性能。
 *
 * Brent变种算法原理：
 * 核心不变式：如果一个元素不在其主位置，那么占据其主位置的元素
 * 必须在自己的主位置。这个不变式确保了查找性能的上界。
 *
 * 插入策略：
 * 1. 计算键的主位置
 * 2. 如果主位置空闲，直接插入
 * 3. 如果主位置被占用：
 *    a) 检查占用者是否在其主位置
 *    b) 如果不在，将占用者移到空闲位置，新键占据主位置
 *    c) 如果在，新键使用空闲位置，通过链表连接
 *
 * 冲突解决：
 * - 优先保证主位置的正确性
 * - 使用链表处理冲突
 * - 动态调整链表结构以维护不变式
 *
 * 空间不足处理：
 * 如果找不到空闲位置，触发重哈希操作，然后重新插入。
 *
 * 性能保证：
 * - 平均查找时间：O(1)
 * - 最坏查找时间：O(链长度)，但链长度有上界
 * - 即使在高负载因子下也保持良好性能
 *
 * 内存屏障：
 * 使用luaC_barriert确保垃圾回收的正确性。
 *
 * 算法复杂度：
 * - 时间复杂度：平均O(1)，最坏O(n)
 * - 空间复杂度：O(1)
 *
 * @pre L、t和key必须是有效指针
 * @post 键被插入表中，返回值位置的指针
 *
 * @note 这是Brent变种散列算法的经典实现
 * @see mainposition(), getfreepos(), rehash()
 */
static TValue *newkey(lua_State *L, Table *t, const TValue *key) {
    Node *mp = mainposition(t, key);    // 计算键的主位置

    // 检查主位置是否被占用或者是虚拟节点
    if (!ttisnil(gval(mp)) || mp == dummynode) {
        Node *othern;
        Node *n = getfreepos(t);    // 获取空闲位置

        if (n == NULL) {
            // 没有空闲位置，需要重哈希
            rehash(L, t, key);
            return luaH_set(L, t, key);    // 在扩展后的表中重新插入
        }

        lua_assert(n != dummynode);

        // 检查占据主位置的元素是否在其正确的主位置
        othern = mainposition(t, key2tval(mp));

        if (othern != mp) {
            // 占用者不在其主位置，应用Brent算法
            // 将占用者移到空闲位置，新键占据主位置

            // 在冲突链中找到指向mp的前驱节点
            while (gnext(othern) != mp) {
                othern = gnext(othern);
            }

            // 重新连接链表：用n替换mp
            gnext(othern) = n;

            // 将mp的内容复制到空闲位置n
            *n = *mp;

            // 现在mp是空闲的，清理它
            gnext(mp) = NULL;
            setnilvalue(gval(mp));
        } else {
            // 占用者在其主位置，新键使用空闲位置
            // 将新节点插入到冲突链的头部
            gnext(n) = gnext(mp);
            gnext(mp) = n;
            mp = n;    // 新键将使用节点n
        }
    }

    // 设置新键
    gkey(mp)->value = key->value;
    gkey(mp)->tt = key->tt;

    // 垃圾回收屏障
    luaC_barriert(L, t, key);

    lua_assert(ttisnil(gval(mp)));
    return gval(mp);    // 返回值的位置
}


/**
 * @brief 整数键的专用查找函数
 * @param t 要搜索的表
 * @param key 整数键
 * @return 找到的值指针，如果未找到则返回nil对象
 *
 * 详细说明：
 * 这是针对整数键优化的查找函数，利用了Lua表的混合结构特性。
 * 整数键可能存储在数组部分或哈希部分，函数会智能地选择
 * 最优的查找策略。
 *
 * 查找策略：
 * 1. 首先检查是否在数组部分的有效范围内
 * 2. 如果在数组范围内，直接进行O(1)数组访问
 * 3. 如果不在数组范围内，在哈希部分搜索
 *
 * 数组部分优化：
 * 使用无符号整数比较技巧：cast(unsigned int, key-1) < sizearray
 * 这个技巧同时检查了key >= 1和key <= sizearray两个条件。
 *
 * 哈希部分搜索：
 * 使用专门的数值哈希函数，然后遍历冲突链查找匹配的键。
 *
 * 性能特征：
 * - 数组部分：O(1)
 * - 哈希部分：平均O(1)，最坏O(链长度)
 *
 * 边界处理：
 * 正确处理边界情况，如key=0、负数等。
 *
 * @pre t必须是有效的表指针
 * @post 返回找到的值或nil对象
 *
 * @note 这是整数索引访问的高性能实现
 * @see hashnum(), luaH_get()
 */
const TValue *luaH_getnum(Table *t, int key) {
    // 检查是否在数组部分：1 <= key <= sizearray
    if (cast(unsigned int, key - 1) < cast(unsigned int, t->sizearray)) {
        return &t->array[key - 1];    // 直接数组访问
    } else {
        // 在哈希部分搜索
        lua_Number nk = cast_num(key);
        Node *n = hashnum(t, nk);

        do {
            // 检查键是否匹配
            if (ttisnumber(gkey(n)) && luai_numeq(nvalue(gkey(n)), nk)) {
                return gval(n);    // 找到了
            } else {
                n = gnext(n);
            }
        } while (n);

        return luaO_nilobject;    // 未找到
    }
}

/**
 * @brief 字符串键的专用查找函数
 * @param t 要搜索的表
 * @param key 字符串键
 * @return 找到的值指针，如果未找到则返回nil对象
 *
 * 详细说明：
 * 这是针对字符串键优化的查找函数。字符串在Lua中是内部化的，
 * 可以使用指针比较而不是内容比较，大大提高了查找效率。
 *
 * 优化策略：
 * 1. 利用字符串的预计算哈希值
 * 2. 使用指针相等性比较而不是字符串内容比较
 * 3. 遍历冲突链查找匹配的字符串
 *
 * 字符串内部化：
 * Lua中的字符串是内部化的，相同内容的字符串共享同一个对象，
 * 因此可以使用快速的指针比较。
 *
 * 哈希优化：
 * 字符串对象包含预计算的哈希值，避免了重复的哈希计算。
 *
 * 性能特征：
 * - 时间复杂度：平均O(1)，最坏O(链长度)
 * - 空间复杂度：O(1)
 * - 比较操作：O(1)指针比较
 *
 * @pre t必须是有效的表指针，key必须是有效的字符串指针
 * @post 返回找到的值或nil对象
 *
 * @note 利用了字符串内部化的优势
 * @see hashstr(), luaH_get()
 */
const TValue *luaH_getstr(Table *t, TString *key) {
    Node *n = hashstr(t, key);    // 使用字符串的预计算哈希值

    do {
        // 检查是否为字符串且指针相等（内部化字符串的优势）
        if (ttisstring(gkey(n)) && rawtsvalue(gkey(n)) == key) {
            return gval(n);    // 找到了
        } else {
            n = gnext(n);
        }
    } while (n);

    return luaO_nilobject;    // 未找到
}


/**
 * @brief 通用表查找函数
 * @param t 要搜索的表
 * @param key 要查找的键
 * @return 找到的值指针，如果未找到则返回nil对象
 *
 * 详细说明：
 * 这是Lua表查找的主要接口函数，根据键的类型智能地选择
 * 最优的查找策略。它是所有表访问操作的基础。
 *
 * 类型特化优化：
 * 1. nil键：直接返回nil（nil不能作为有效键）
 * 2. 字符串键：使用专用的字符串查找函数
 * 3. 数值键：检查是否为整数，如果是则使用整数专用函数
 * 4. 其他类型：使用通用的哈希查找
 *
 * 整数优化：
 * 对于数值键，首先检查是否为整数。如果是整数，使用
 * 高效的luaH_getnum函数，否则使用通用哈希查找。
 *
 * 通用哈希查找：
 * 对于其他类型（布尔值、轻量用户数据、对象等），
 * 计算主位置并遍历冲突链进行查找。
 *
 * 性能层次：
 * 1. 数组访问（整数键）：最快，O(1)
 * 2. 字符串哈希：快速，利用预计算哈希和指针比较
 * 3. 通用哈希：标准速度，需要完整的键比较
 *
 * 错误处理：
 * nil键被视为无效，直接返回nil对象。
 *
 * @pre t必须是有效的表指针，key必须是有效的值指针
 * @post 返回找到的值或nil对象
 *
 * @note 这是表查找的统一接口
 * @see luaH_getnum(), luaH_getstr(), mainposition()
 */
const TValue *luaH_get(Table *t, const TValue *key) {
    switch (ttype(key)) {
        case LUA_TNIL:
            return luaO_nilobject;    // nil不能作为键

        case LUA_TSTRING:
            return luaH_getstr(t, rawtsvalue(key));    // 使用字符串专用函数

        case LUA_TNUMBER: {
            int k;
            lua_Number n = nvalue(key);
            lua_number2int(k, n);

            // 检查是否为整数
            if (luai_numeq(cast_num(k), nvalue(key))) {
                return luaH_getnum(t, k);    // 使用整数专用函数
            }
            // 否则继续到default分支
        }

        default: {
            // 通用哈希查找
            Node *n = mainposition(t, key);
            do {
                // 检查键是否匹配
                if (luaO_rawequalObj(key2tval(n), key)) {
                    return gval(n);    // 找到了
                } else {
                    n = gnext(n);
                }
            } while (n);

            return luaO_nilobject;    // 未找到
        }
    }
}

/**
 * @brief 表设置函数：获取或创建键对应的值位置
 * @param L Lua状态机指针
 * @param t 目标表
 * @param key 要设置的键
 * @return 指向键对应值位置的指针
 *
 * 详细说明：
 * 这是Lua表设置操作的核心函数，负责获取键对应的值位置。
 * 如果键已存在，返回现有位置；如果不存在，创建新的键值对。
 *
 * 操作流程：
 * 1. 首先尝试查找现有的键
 * 2. 如果找到，直接返回值的位置
 * 3. 如果未找到，验证键的有效性
 * 4. 创建新的键值对并返回值位置
 *
 * 元方法缓存：
 * 设置t->flags = 0清除元方法缓存，因为表结构可能发生变化。
 *
 * 键有效性检查：
 * - nil键：不允许作为表键
 * - NaN：不允许作为表键（NaN != NaN的特性会破坏表的一致性）
 *
 * 新键创建：
 * 如果键不存在，调用newkey函数创建新的键值对。
 * newkey可能触发表的重哈希操作。
 *
 * 返回值特性：
 * 返回的是值位置的指针，调用者可以直接修改该位置的内容。
 *
 * 性能考虑：
 * - 现有键：查找性能，通常O(1)
 * - 新键：可能触发重哈希，O(n)，但频率较低
 *
 * @pre L和t必须是有效指针，key必须是有效的值指针
 * @post 返回键对应的值位置指针
 *
 * @note 这是表修改操作的基础函数
 * @see luaH_get(), newkey(), luaG_runerror()
 */
TValue *luaH_set(lua_State *L, Table *t, const TValue *key) {
    const TValue *p = luaH_get(t, key);    // 首先尝试查找现有键

    t->flags = 0;    // 清除元方法缓存

    if (p != luaO_nilobject) {
        // 键已存在，返回现有位置
        return cast(TValue *, p);
    } else {
        // 键不存在，需要创建新键

        // 检查键的有效性
        if (ttisnil(key)) {
            luaG_runerror(L, "table index is nil");
        } else if (ttisnumber(key) && luai_numisnan(nvalue(key))) {
            luaG_runerror(L, "table index is NaN");
        }

        // 创建新键
        return newkey(L, t, key);
    }
}


/**
 * @brief 整数键的专用设置函数
 * @param L Lua状态机指针
 * @param t 目标表
 * @param key 整数键
 * @return 指向键对应值位置的指针
 *
 * 详细说明：
 * 这是针对整数键优化的设置函数，避免了通用设置函数中的
 * 类型检查开销，提供更高的性能。
 *
 * 优化策略：
 * 1. 直接使用luaH_getnum进行查找
 * 2. 如果键存在，直接返回位置
 * 3. 如果不存在，构造TValue并创建新键
 *
 * 性能优势：
 * - 避免了类型分发的开销
 * - 利用了整数查找的优化路径
 * - 减少了函数调用层次
 *
 * 使用场景：
 * - 数组风格的表操作
 * - 已知键为整数的高频操作
 * - 性能敏感的代码路径
 *
 * @pre L和t必须是有效指针
 * @post 返回键对应的值位置指针
 *
 * @note 这是整数键设置的高性能接口
 * @see luaH_getnum(), newkey()
 */
TValue *luaH_setnum(lua_State *L, Table *t, int key) {
    const TValue *p = luaH_getnum(t, key);

    if (p != luaO_nilobject) {
        return cast(TValue *, p);    // 键已存在
    } else {
        // 创建新键
        TValue k;
        setnvalue(&k, cast_num(key));
        return newkey(L, t, &k);
    }
}

/**
 * @brief 字符串键的专用设置函数
 * @param L Lua状态机指针
 * @param t 目标表
 * @param key 字符串键
 * @return 指向键对应值位置的指针
 *
 * 详细说明：
 * 这是针对字符串键优化的设置函数，利用字符串的内部化特性
 * 和预计算哈希值，提供高效的字符串键设置操作。
 *
 * 优化策略：
 * 1. 直接使用luaH_getstr进行查找
 * 2. 利用字符串的指针比较优势
 * 3. 避免通用函数的类型检查开销
 *
 * 字符串优势：
 * - 预计算的哈希值
 * - 指针相等性比较
 * - 内部化保证的唯一性
 *
 * 使用场景：
 * - 对象属性访问
 * - 字典风格的表操作
 * - 符号表和命名空间操作
 *
 * @pre L和t必须是有效指针，key必须是有效的字符串指针
 * @post 返回键对应的值位置指针
 *
 * @note 这是字符串键设置的高性能接口
 * @see luaH_getstr(), newkey()
 */
TValue *luaH_setstr(lua_State *L, Table *t, TString *key) {
    const TValue *p = luaH_getstr(t, key);

    if (p != luaO_nilobject) {
        return cast(TValue *, p);    // 键已存在
    } else {
        // 创建新键
        TValue k;
        setsvalue(L, &k, key);
        return newkey(L, t, &k);
    }
}


/**
 * @brief 无界搜索：在哈希部分查找表的边界
 * @param t 要搜索的表
 * @param j 搜索的起始位置
 * @return 找到的边界位置
 *
 * 详细说明：
 * 这个函数实现了一个巧妙的算法来查找表在哈希部分的边界。
 * 由于哈希部分的大小是未知的，需要使用特殊的搜索策略。
 *
 * 算法策略：
 * 1. 指数搜索阶段：通过倍增找到包含边界的区间
 * 2. 二分搜索阶段：在确定的区间内精确定位边界
 *
 * 指数搜索：
 * 从位置j开始，每次将搜索位置翻倍，直到找到一个nil值。
 * 这样可以快速确定边界的大致范围。
 *
 * 溢出处理：
 * 如果搜索位置超过MAX_INT，说明表可能被恶意构造，
 * 此时回退到线性搜索以确保算法的正确性。
 *
 * 二分搜索：
 * 在确定的区间[i, j]内使用二分搜索精确定位边界。
 *
 * 算法复杂度：
 * - 时间复杂度：O(log n)，其中n是边界位置
 * - 空间复杂度：O(1)
 *
 * 边界定义：
 * 边界是最大的整数i，使得t[i]非nil且t[i+1]为nil。
 *
 * @pre t必须是有效的表指针，j必须是有效的起始位置
 * @post 返回找到的边界位置
 *
 * @note 这是表长度计算的关键算法
 * @see luaH_getn(), luaH_getnum()
 */
static int unbound_search(Table *t, unsigned int j) {
    unsigned int i = j;    // i是0或一个存在的索引
    j++;

    // 指数搜索：找到i和j使得i存在而j不存在
    while (!ttisnil(luaH_getnum(t, j))) {
        i = j;
        j *= 2;

        // 检查溢出
        if (j > cast(unsigned int, MAX_INT)) {
            // 表可能被恶意构造：回退到线性搜索
            i = 1;
            while (!ttisnil(luaH_getnum(t, i))) {
                i++;
            }
            return i - 1;
        }
    }

    // 现在在[i, j]区间内进行二分搜索
    while (j - i > 1) {
        unsigned int m = (i + j) / 2;
        if (ttisnil(luaH_getnum(t, m))) {
            j = m;
        } else {
            i = m;
        }
    }

    return i;
}


/**
 * @brief 计算表的长度（查找边界）
 * @param t 要计算长度的表
 * @return 表的长度（边界位置）
 *
 * 详细说明：
 * 这个函数实现了Lua表长度操作符#的核心算法。表的长度定义为
 * 边界位置：最大的整数i使得t[i]非nil且t[i+1]为nil。
 *
 * 边界定义：
 * - 如果t[1]为nil，则长度为0
 * - 否则，长度是最大的i使得t[i]非nil且t[i+1]为nil
 *
 * 搜索策略：
 * 1. 首先检查数组部分是否包含边界
 * 2. 如果数组部分的最后一个元素为nil，在数组部分二分搜索
 * 3. 如果哈希部分为空，数组大小就是长度
 * 4. 否则，在哈希部分进行无界搜索
 *
 * 数组部分搜索：
 * 如果array[sizearray-1]为nil，说明边界在数组部分，
 * 使用二分搜索快速定位。
 *
 * 哈希部分搜索：
 * 如果边界不在数组部分，需要在哈希部分搜索。
 * 使用unbound_search函数处理这种情况。
 *
 * 性能特征：
 * - 数组部分边界：O(log(数组大小))
 * - 哈希部分边界：O(log(边界位置))
 * - 空哈希部分：O(1)
 *
 * 语义注意：
 * 表的长度在有"洞"（nil值）的情况下可能不唯一，
 * 但算法会找到一个有效的边界。
 *
 * @pre t必须是有效的表指针
 * @post 返回表的长度（边界位置）
 *
 * @note 这是#操作符的核心实现
 * @see unbound_search(), 二分搜索算法
 */
int luaH_getn(Table *t) {
    unsigned int j = t->sizearray;

    if (j > 0 && ttisnil(&t->array[j - 1])) {
        // 数组部分存在边界：二分搜索定位
        unsigned int i = 0;
        while (j - i > 1) {
            unsigned int m = (i + j) / 2;
            if (ttisnil(&t->array[m - 1])) {
                j = m;
            } else {
                i = m;
            }
        }
        return i;
    }
    // 否则必须在哈希部分查找边界
    else if (t->node == dummynode) {
        // 哈希部分为空，很简单
        return j;
    } else {
        return unbound_search(t, j);
    }
}

// ============================================================================
// 调试接口：仅在调试模式下编译
// ============================================================================

#if defined(LUA_DEBUG)

/**
 * @brief 调试接口：获取键的主位置
 * @param t 表指针
 * @param key 键指针
 * @return 键的主位置节点指针
 *
 * 这是mainposition函数的调试接口，用于测试和调试目的。
 */
Node *luaH_mainposition(const Table *t, const TValue *key) {
    return mainposition(t, key);
}

/**
 * @brief 调试接口：检查节点是否为虚拟节点
 * @param n 节点指针
 * @return 如果是虚拟节点返回非零值，否则返回0
 *
 * 这个函数用于调试和测试，检查节点是否为全局虚拟节点。
 */
int luaH_isdummy(Node *n) {
    return n == dummynode;
}

#endif
