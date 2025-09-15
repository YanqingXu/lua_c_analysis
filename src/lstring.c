/**
 * @file lstring.c
 * @brief Lua字符串系统实现：字符串内部化和高效管理
 *
 * 详细说明：
 * 本文件实现了Lua语言的字符串管理系统，采用了字符串内部化
 * （String Interning）技术，确保相同内容的字符串在内存中
 * 只存在一份拷贝，大大提高了字符串比较和内存使用的效率。
 *
 * 核心设计理念：
 * 1. 字符串内部化：相同内容的字符串共享同一个对象
 * 2. 哈希表管理：使用哈希表快速查找和存储字符串
 * 3. 垃圾回收集成：字符串对象完全集成到GC系统中
 * 4. 动态调整：字符串表可以根据负载动态调整大小
 *
 * 技术优势：
 * - 内存效率：避免重复字符串的内存浪费
 * - 比较效率：字符串比较退化为指针比较（O(1)）
 * - 哈希效率：预计算哈希值，避免重复计算
 * - 缓存友好：字符串数据紧凑存储，提高缓存命中率
 *
 * 字符串哈希算法：
 * 采用改进的字符串哈希算法，对于长字符串采用采样策略，
 * 在哈希质量和计算效率之间取得平衡。
 *
 * 应用场景：
 * - 变量名和函数名的存储
 * - 字符串常量的管理
 * - 表键的高效比较
 * - 源代码解析中的标识符处理
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2007-12-27
 * @since Lua 5.0
 * @see lobject.h, lmem.h, lgc.h
 */

#include <string.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"

/**
 * @brief 调整字符串表的大小
 * @param L Lua状态机指针
 * @param newsize 新的哈希表大小
 *
 * 详细说明：
 * 这个函数负责调整全局字符串表的大小，当字符串数量增长导致
 * 哈希冲突过多时，通过扩展哈希表来维持查找性能。
 *
 * 调整时机：
 * 字符串表的负载因子过高时自动触发，通常在插入新字符串时
 * 检查并决定是否需要扩展。
 *
 * 垃圾回收安全：
 * 在垃圾回收的字符串清扫阶段不能进行调整，因为此时字符串
 * 对象的状态可能不稳定。
 *
 * 重哈希过程：
 * 1. 分配新的哈希表
 * 2. 初始化所有槽位为NULL
 * 3. 遍历旧表，将所有字符串重新哈希到新表
 * 4. 释放旧表内存
 * 5. 更新表结构
 *
 * 哈希保持：
 * 字符串的哈希值是预计算的，重哈希时只需要重新计算槽位，
 * 不需要重新计算字符串的哈希值。
 *
 * 性能考虑：
 * - 时间复杂度：O(字符串总数)
 * - 空间复杂度：O(新表大小)
 * - 调整频率：随着字符串数量指数增长而对数增长
 *
 * 内存安全：
 * 使用Lua的内存管理器，确保在内存不足时能正确处理异常。
 *
 * @pre L必须是有效的Lua状态机，newsize必须是合理的大小
 * @post 字符串表被调整到新大小，所有字符串重新分布
 *
 * @note 这是字符串系统性能维护的关键函数
 * @see luaM_newvector(), luaM_freearray()
 */
void luaS_resize(lua_State *L, int newsize) {
    GCObject **newhash;
    stringtable *tb;
    int i;

    // 垃圾回收安全检查：在字符串清扫阶段不能调整大小
    if (G(L)->gcstate == GCSsweepstring) {
        return;
    }

    // 分配新的哈希表
    newhash = luaM_newvector(L, newsize, GCObject *);
    tb = &G(L)->strt;

    // 初始化新表的所有槽位
    for (i = 0; i < newsize; i++) {
        newhash[i] = NULL;
    }

    // 重哈希：将旧表中的所有字符串迁移到新表
    for (i = 0; i < tb->size; i++) {
        GCObject *p = tb->hash[i];
        while (p) {
            GCObject *next = p->gch.next;           // 保存下一个节点
            unsigned int h = gco2ts(p)->hash;       // 获取预计算的哈希值
            int h1 = lmod(h, newsize);              // 计算新的槽位
            lua_assert(cast_int(h % newsize) == lmod(h, newsize));

            // 将字符串插入到新表的对应槽位（头插法）
            p->gch.next = newhash[h1];
            newhash[h1] = p;
            p = next;
        }
    }

    // 释放旧表内存并更新表结构
    luaM_freearray(L, tb->hash, tb->size, TString *);
    tb->size = newsize;
    tb->hash = newhash;
}


/**
 * @brief 创建新的字符串对象（内部函数）
 * @param L Lua状态机指针
 * @param str 源字符串数据
 * @param l 字符串长度
 * @param h 预计算的哈希值
 * @return 新创建的字符串对象指针
 *
 * 详细说明：
 * 这是字符串创建的核心函数，负责分配内存、初始化字符串对象
 * 并将其插入到全局字符串表中。
 *
 * 内存布局：
 * 字符串对象采用紧凑的内存布局：
 * [TString结构][字符串数据]['\0']
 * 这种布局提高了缓存局部性和内存效率。
 *
 * 溢出检查：
 * 在分配内存前检查字符串长度是否会导致整数溢出，
 * 确保内存分配的安全性。
 *
 * 对象初始化：
 * - len：字符串长度
 * - hash：预计算的哈希值
 * - marked：垃圾回收标记（初始为白色）
 * - tt：类型标记（LUA_TSTRING）
 * - reserved：保留字段（用于关键字标记）
 *
 * 字符串数据复制：
 * 使用memcpy高效复制字符串数据，并添加null终止符。
 *
 * 哈希表插入：
 * 将新字符串插入到对应的哈希槽中，使用头插法。
 *
 * 动态调整：
 * 插入后检查负载因子，如果过高则触发表扩展。
 *
 * 性能特征：
 * - 时间复杂度：O(字符串长度) + O(1)哈希插入
 * - 空间复杂度：O(字符串长度)
 *
 * @pre L、str必须是有效指针，l必须是正确的长度，h必须是有效哈希值
 * @post 返回完全初始化的字符串对象，已插入字符串表
 *
 * @note 这是字符串内部化的核心实现
 * @see luaS_newlstr(), luaS_resize()
 */
static TString *newlstr(lua_State *L, const char *str, size_t l,
                        unsigned int h) {
    TString *ts;
    stringtable *tb;

    // 溢出检查：确保内存分配不会溢出
    if (l + 1 > (MAX_SIZET - sizeof(TString)) / sizeof(char)) {
        luaM_toobig(L);
    }

    // 分配内存：TString结构 + 字符串数据 + null终止符
    ts = cast(TString *, luaM_malloc(L, (l + 1) * sizeof(char) + sizeof(TString)));

    // 初始化字符串对象
    ts->tsv.len = l;                        // 字符串长度
    ts->tsv.hash = h;                       // 预计算的哈希值
    ts->tsv.marked = luaC_white(G(L));      // 垃圾回收标记（白色）
    ts->tsv.tt = LUA_TSTRING;               // 类型标记
    ts->tsv.reserved = 0;                   // 保留字段（非关键字）

    // 复制字符串数据（字符串数据紧跟在TString结构后面）
    memcpy(ts + 1, str, l * sizeof(char));
    ((char *)(ts + 1))[l] = '\0';           // 添加null终止符

    // 插入到字符串表
    tb = &G(L)->strt;
    h = lmod(h, tb->size);                  // 计算哈希槽位
    ts->tsv.next = tb->hash[h];             // 头插法
    tb->hash[h] = obj2gco(ts);
    tb->nuse++;                             // 增加使用计数

    // 检查是否需要扩展哈希表
    if (tb->nuse > cast(lu_int32, tb->size) && tb->size <= MAX_INT / 2) {
        luaS_resize(L, tb->size * 2);
    }

    return ts;
}

/**
 * @brief 创建或查找指定长度的字符串（字符串内部化的主接口）
 * @param L Lua状态机指针
 * @param str 源字符串数据
 * @param l 字符串长度
 * @return 字符串对象指针（新创建或已存在的）
 *
 * 详细说明：
 * 这是Lua字符串内部化的主要接口函数，实现了字符串的查找或创建。
 * 如果相同内容的字符串已存在，直接返回现有对象；否则创建新对象。
 *
 * 字符串内部化优势：
 * 1. 内存节省：相同内容的字符串只存储一份
 * 2. 比较高效：字符串比较退化为指针比较
 * 3. 哈希高效：预计算哈希值，避免重复计算
 *
 * 哈希算法：
 * 采用改进的字符串哈希算法，具有以下特点：
 * - 初始种子：使用字符串长度作为初始哈希值
 * - 采样策略：对于长字符串，每32个字符采样一个，平衡质量和效率
 * - 位运算优化：使用位移和异或操作，计算效率高
 * - 字符分布：考虑字符在不同位置的贡献
 *
 * 哈希公式：
 * h = h ^ ((h << 5) + (h >> 2) + char)
 * 这个公式在哈希质量和计算速度之间取得了良好平衡。
 *
 * 查找过程：
 * 1. 计算字符串的哈希值
 * 2. 在对应的哈希槽中查找
 * 3. 逐个比较长度和内容
 * 4. 处理垃圾回收中的"死"字符串
 *
 * 垃圾回收处理：
 * 如果找到的字符串在垃圾回收中被标记为"死"，
 * 需要将其重新标记为"活"，因为现在又有引用了。
 *
 * 性能特征：
 * - 哈希计算：O(字符串长度/32)，对长字符串友好
 * - 查找时间：平均O(1)，最坏O(冲突链长度)
 * - 内存使用：最优（无重复）
 *
 * 使用场景：
 * - 源代码解析中的标识符处理
 * - 字符串常量的创建
 * - 动态字符串的内部化
 *
 * @pre L和str必须是有效指针，l必须是正确的字符串长度
 * @post 返回内部化的字符串对象
 *
 * @note 这是字符串内部化的主要入口点
 * @see newlstr(), 字符串哈希算法
 */
TString *luaS_newlstr(lua_State *L, const char *str, size_t l) {
    GCObject *o;
    unsigned int h = cast(unsigned int, l);     // 使用长度作为哈希种子
    size_t step = (l >> 5) + 1;                 // 采样步长：长字符串每32字符采样一个
    size_t l1;

    // 计算字符串哈希值（采样策略）
    for (l1 = l; l1 >= step; l1 -= step) {
        h = h ^ ((h << 5) + (h >> 2) + cast(unsigned char, str[l1 - 1]));
    }

    // 在字符串表中查找现有的字符串
    for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
         o != NULL;
         o = o->gch.next) {
        TString *ts = rawgco2ts(o);

        // 比较长度和内容
        if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0)) {
            // 找到匹配的字符串

            // 垃圾回收处理：如果字符串被标记为"死"，重新激活它
            if (isdead(G(L), o)) {
                changewhite(o);
            }

            return ts;  // 返回现有的字符串对象
        }
    }

    // 没有找到现有字符串，创建新的
    return newlstr(L, str, l, h);
}

/**
 * @brief 创建新的用户数据对象
 * @param L Lua状态机指针
 * @param s 用户数据的大小（字节）
 * @param e 环境表指针
 * @return 新创建的用户数据对象指针
 *
 * 详细说明：
 * 这个函数创建Lua的用户数据对象，用户数据是Lua中表示
 * C数据结构的机制，允许C代码将任意数据暴露给Lua。
 *
 * 用户数据特性：
 * 1. 任意大小：可以存储任意大小的C数据结构
 * 2. 垃圾回收：完全集成到Lua的垃圾回收系统
 * 3. 元表支持：可以设置元表定义操作行为
 * 4. 环境表：拥有独立的环境表
 *
 * 内存布局：
 * [Udata结构][用户数据空间]
 * 用户数据紧跟在Udata结构后面，提供紧凑的内存布局。
 *
 * 溢出检查：
 * 在分配内存前检查大小是否会导致整数溢出，
 * 确保内存分配的安全性。
 *
 * 对象初始化：
 * - marked：垃圾回收标记（初始为白色）
 * - tt：类型标记（LUA_TUSERDATA）
 * - len：用户数据大小
 * - metatable：元表（初始为NULL）
 * - env：环境表
 *
 * 垃圾回收链接：
 * 用户数据被链接到主线程的对象链表中，确保垃圾回收器
 * 能够正确管理其生命周期。
 *
 * 与字符串的区别：
 * - 用户数据不进行内部化（每次创建新对象）
 * - 用户数据可以有元表和环境表
 * - 用户数据主要用于封装C数据结构
 *
 * 使用场景：
 * - 封装C库的数据结构
 * - 实现Lua扩展模块
 * - 在Lua中表示复杂的C对象
 * - 提供自定义的数据类型
 *
 * 性能考虑：
 * - 时间复杂度：O(1)
 * - 空间复杂度：O(用户数据大小)
 * - 内存对齐：遵循系统的内存对齐要求
 *
 * @pre L必须是有效的Lua状态机，s必须是合理的大小，e可以为NULL
 * @post 返回完全初始化的用户数据对象
 *
 * @note 用户数据是Lua与C交互的重要机制
 * @see luaM_malloc(), luaC_white()
 */
Udata *luaS_newudata(lua_State *L, size_t s, Table *e) {
    Udata *u;

    // 溢出检查：确保内存分配不会溢出
    if (s > MAX_SIZET - sizeof(Udata)) {
        luaM_toobig(L);
    }

    // 分配内存：Udata结构 + 用户数据空间
    u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));

    // 初始化用户数据对象
    u->uv.marked = luaC_white(G(L));        // 垃圾回收标记（白色）
    u->uv.tt = LUA_TUSERDATA;               // 类型标记
    u->uv.len = s;                          // 用户数据大小
    u->uv.metatable = NULL;                 // 元表（初始为空）
    u->uv.env = e;                          // 环境表

    // 链接到垃圾回收器的对象链表（插入到主线程后面）
    u->uv.next = G(L)->mainthread->next;
    G(L)->mainthread->next = obj2gco(u);

    return u;
}

