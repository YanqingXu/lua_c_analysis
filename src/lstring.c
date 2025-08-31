/*
** [字符串] Lua 字符串管理和驻留系统
**
** 详细功能说明：
** 本模块实现了 Lua 的字符串驻留（String Interning）机制，这是 Lua
** 性能优化的核心组件之一。通过字符串驻留，相同内容的字符串在内存中
** 只存储一份，大大节省了内存使用并加速了字符串比较操作。
**
** 核心设计理念：
** - 字符串驻留：相同内容的字符串共享同一个对象
** - 哈希表管理：使用开放寻址法处理哈希冲突
** - 动态扩容：根据负载因子自动调整哈希表大小
** - 垃圾回收集成：字符串对象完全集成到 GC 系统中
**
** 哈希算法特点：
** - 对短字符串：哈希所有字符，确保分布均匀
** - 对长字符串：采用采样策略，平衡性能和分布质量
** - 混合哈希函数：结合位移和异或操作，减少冲突
**
** 内存管理策略：
** - 字符串对象和内容存储在连续内存中
** - 支持用户数据的环境表和元表机制
** - 与垃圾回收器紧密集成，支持增量回收
*/

#include <string.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"

/*
** [核心] 动态调整字符串表的哈希表大小
**
** 详细功能说明：
** 当字符串表的负载因子过高时，扩展哈希表以减少冲突链的长度，
** 提高字符串查找和插入的性能。这是一个关键的性能优化操作。
**
** 重哈希过程：
** 1. 分配新的更大的哈希表
** 2. 重新计算所有现有字符串的哈希位置
** 3. 将字符串重新分布到新的哈希表中
** 4. 释放旧哈希表的内存
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param newsize - int：新哈希表的大小（必须是2的幂）
**
** 返回值：无
**
** 算法复杂度：O(n) 时间（n 为字符串总数），O(newsize) 空间
**
** 注意事项：
** - 在垃圾回收的字符串清理阶段不能调整大小
** - 新大小通常是当前大小的2倍，保持2的幂特性
** - 重哈希过程中可能触发垃圾回收
*/
void luaS_resize(lua_State *L, int newsize)
{
    // 变量声明和初始化
    GCObject **newhash;
    stringtable *tb;
    int i;

    // 第一阶段：垃圾回收状态检查
    // 如果垃圾回收器正在清理字符串，不能进行表重构
    if (G(L)->gcstate == GCSsweepstring)
    {
        return;
    }

    // 第二阶段：分配新的哈希表
    // 分配新的指针数组，每个元素指向一个冲突链的头部
    newhash = luaM_newvector(L, newsize, GCObject *);

    // 获取全局字符串表的引用
    tb = &G(L)->strt;

    // 第三阶段：初始化新哈希表
    // 将所有槽位初始化为 NULL，表示空链表
    for (i = 0; i < newsize; i++)
    {
        newhash[i] = NULL;
    }

    // 第四阶段：重新哈希现有字符串
    // 遍历旧哈希表的每个槽位，将字符串重新分布到新表中
    for (i = 0; i < tb->size; i++)
    {
        // 获取当前槽位的冲突链头部
        GCObject *p = tb->hash[i];

        // 遍历整个冲突链
        while (p)
        {
            // 保存下一个节点指针，防止链表断裂
            GCObject *next = p->gch.next;

            // 获取字符串对象的预计算哈希值
            unsigned int h = gco2ts(p)->hash;

            // 计算在新表中的位置
            int h1 = lmod(h, newsize);

            // 调试断言：验证模运算的正确性
            lua_assert(cast_int(h % newsize) == lmod(h, newsize));

            // 将当前节点插入到新位置的链表头部
            p->gch.next = newhash[h1];
            newhash[h1] = p;

            // 移动到下一个节点
            p = next;
        }
    }

    // 第五阶段：更新字符串表结构
    // 释放旧哈希表的内存
    luaM_freearray(L, tb->hash, tb->size, TString *);

    // 更新字符串表的大小和哈希表指针
    tb->size = newsize;
    tb->hash = newhash;
}


/*
** [核心] 创建新的字符串对象并插入字符串表
**
** 详细功能说明：
** 这是字符串驻留系统的核心函数，负责实际创建字符串对象并将其
** 插入到全局字符串表中。该函数假设字符串在表中不存在，直接创建新对象。
**
** 内存布局设计：
** [TString结构体][字符串内容]['\0'终止符]
** 这种连续内存布局提高了缓存局部性和内存使用效率。
**
** 哈希表插入策略：
** - 使用链地址法处理哈希冲突
** - 新字符串总是插入到冲突链的头部（O(1)插入）
** - 根据负载因子自动触发表扩容
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param str - const char*：源字符串数据指针
** @param l - size_t：字符串长度（不包括终止符）
** @param h - unsigned int：预计算的哈希值
**
** 返回值：
** @return TString*：新创建的字符串对象指针
**
** 算法复杂度：O(1) 平均时间，O(l) 空间（l 为字符串长度）
**
** 注意事项：
** - 调用前必须确保字符串在表中不存在
** - 可能触发字符串表扩容操作
** - 新对象自动注册到垃圾回收器
*/
static TString *newlstr(lua_State *L, const char *str, size_t l, unsigned int h)
{
    // 变量声明
    TString *ts;
    stringtable *tb;

    // 第一阶段：内存溢出检查
    // 检查字符串长度是否会导致内存分配溢出
    if (l + 1 > (MAX_SIZET - sizeof(TString)) / sizeof(char))
    {
        luaM_toobig(L);
    }

    // 第二阶段：分配字符串对象内存
    // 分配连续内存：TString 结构体 + 字符串内容 + 终止符
    ts = cast(TString *, luaM_malloc(L, (l + 1) * sizeof(char) + sizeof(TString)));

    // 第三阶段：初始化字符串对象字段
    // 设置字符串长度
    ts->tsv.len = l;

    // 设置预计算的哈希值，避免重复计算
    ts->tsv.hash = h;

    // 设置垃圾回收标记为白色（新创建的对象）
    ts->tsv.marked = luaC_white(G(L));

    // 设置对象类型标记为字符串
    ts->tsv.tt = LUA_TSTRING;

    // 保留字段初始化为0（用于关键字标记等）
    ts->tsv.reserved = 0;

    // 第四阶段：复制字符串内容
    // 将源字符串内容复制到 TString 对象后面的内存区域
    memcpy(ts + 1, str, l * sizeof(char));

    // 添加 C 字符串终止符，确保兼容性
    ((char *)(ts + 1))[l] = '\0';

    // 第五阶段：插入字符串表
    // 获取全局字符串表的引用
    tb = &G(L)->strt;

    // 计算在哈希表中的实际索引位置
    h = lmod(h, tb->size);

    // 使用头插法将新字符串插入到冲突链中
    ts->tsv.next = tb->hash[h];
    tb->hash[h] = obj2gco(ts);

    // 增加字符串表中的字符串计数
    tb->nuse++;

    // 第六阶段：负载因子检查和扩容
    // 如果负载因子过高且表大小未达到上限，则扩容
    if (tb->nuse > cast(lu_int32, tb->size) && tb->size <= MAX_INT / 2)
    {
        // 扩容为原来的2倍，保持2的幂特性
        luaS_resize(L, tb->size * 2);
    }

    return ts;
}


/*
** [字符串] 字符串驻留的主入口函数
**
** 详细功能说明：
** 这是 Lua 字符串驻留系统的核心函数，实现了字符串的查找或创建逻辑。
** 通过驻留机制，相同内容的字符串在内存中只存储一份，这带来了：
** - 内存使用优化：避免重复字符串的内存浪费
** - 比较性能优化：字符串比较只需比较指针，而非内容
** - 哈希表键优化：字符串可以直接用作高效的哈希表键
**
** 哈希算法设计：
** - 对于短字符串：哈希所有字符，确保良好的分布
** - 对于长字符串：采用采样策略，平衡计算成本和分布质量
** - 混合哈希函数：结合位移和异或，减少哈希冲突
**
** 查找策略：
** - 首先计算哈希值并定位到对应的冲突链
** - 遍历冲突链，比较长度和内容
** - 处理垃圾回收中的"死亡"对象复活
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param str - const char*：源字符串数据指针
** @param l - size_t：字符串长度（不包括终止符）
**
** 返回值：
** @return TString*：字符串对象指针（已存在或新创建）
**
** 算法复杂度：O(l/step + k) 时间（l为长度，k为冲突链长度）
**
** 注意事项：
** - 长字符串使用采样哈希，可能增加冲突概率
** - 可能复活垃圾回收中的"死亡"字符串对象
** - 新字符串创建可能触发字符串表扩容
*/
TString *luaS_newlstr(lua_State *L, const char *str, size_t l)
{
    // 变量声明和初始化
    GCObject *o;
    unsigned int h = cast(unsigned int, l);
    size_t step = (l >> 5) + 1;
    size_t l1;

    // 第一阶段：计算字符串哈希值
    // 对于长字符串采用采样策略，避免哈希计算成为性能瓶颈
    // 采样步长 = (长度 >> 5) + 1，确保至少采样一个字符
    for (l1 = l; l1 >= step; l1 -= step)
    {
        // 混合哈希算法：结合左移、右移和异或操作
        // 这种设计能够很好地分散相似字符串的哈希值
        h = h ^ ((h << 5) + (h >> 2) + cast(unsigned char, str[l1 - 1]));
    }

    // 第二阶段：在字符串表中查找已存在的字符串
    // 定位到哈希表中对应的冲突链，然后遍历整个链表
    for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
         o != NULL;
         o = o->gch.next)
    {
        // 将垃圾回收对象转换为字符串对象
        TString *ts = rawgco2ts(o);

        // 第三阶段：字符串内容比较
        // 首先比较长度（快速筛选），然后比较内容
        if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0))
        {
            // 第四阶段：处理垃圾回收状态
            // 如果找到的字符串被标记为"死亡"，需要复活它
            if (isdead(G(L), o))
            {
                // 改变对象的颜色标记，使其重新变为"活跃"状态
                changewhite(o);
            }

            // 返回已存在的字符串对象
            return ts;
        }
    }

    // 第五阶段：创建新字符串
    // 字符串表中不存在该字符串，调用内部函数创建新对象
    return newlstr(L, str, l, h);
}


/*
** [核心] 创建新的用户数据对象
**
** 详细功能说明：
** 用户数据（Userdata）是 Lua 中用于包装 C 数据结构的特殊对象类型。
** 它允许 C 代码将任意数据结构暴露给 Lua，同时保持类型安全和内存管理。
**
** 用户数据特性：
** - 任意大小的数据存储：可以存储任何 C 数据结构
** - 元表支持：可以定义自定义操作和行为
** - 环境表：提供访问控制和命名空间隔离
** - 垃圾回收集成：自动内存管理和终结器支持
**
** 内存布局：
** [Udata结构体][用户数据内容]
** 用户数据内容紧跟在 Udata 结构体之后，提供高效的内存访问。
**
** 垃圾回收集成：
** - 新对象自动注册到垃圾回收器
** - 支持 __gc 元方法作为终结器
** - 与环境表的生命周期管理
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param s - size_t：用户数据的大小（字节数）
** @param e - Table*：环境表指针（用于访问控制）
**
** 返回值：
** @return Udata*：新创建的用户数据对象指针
**
** 算法复杂度：O(1) 时间，O(s) 空间（s 为用户数据大小）
**
** 注意事项：
** - 用户数据大小有上限限制，防止内存溢出
** - 新对象自动链接到垃圾回收器的管理链表
** - 环境表用于实现沙箱和访问控制机制
*/
Udata *luaS_newudata(lua_State *L, size_t s, Table *e)
{
    // 变量声明
    Udata *u;

    // 第一阶段：大小溢出检查
    // 检查用户数据大小是否会导致内存分配溢出
    if (s > MAX_SIZET - sizeof(Udata))
    {
        luaM_toobig(L);
    }

    // 第二阶段：分配用户数据对象内存
    // 分配连续内存：Udata 结构体 + 用户数据内容
    u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));

    // 第三阶段：初始化用户数据对象字段
    // 设置垃圾回收标记为白色（新创建的对象）
    u->uv.marked = luaC_white(G(L));

    // 设置对象类型标记为用户数据
    u->uv.tt = LUA_TUSERDATA;

    // 设置用户数据的大小
    u->uv.len = s;

    // 初始化元表为空，后续可通过 setmetatable 设置
    u->uv.metatable = NULL;

    // 设置环境表，用于访问控制和命名空间隔离
    u->uv.env = e;

    // 第四阶段：链接到垃圾回收器
    // 将新用户数据对象插入到垃圾回收器的管理链表中
    // 使用头插法插入到主线程节点之后
    u->uv.next = G(L)->mainthread->next;
    G(L)->mainthread->next = obj2gco(u);

    return u;
}