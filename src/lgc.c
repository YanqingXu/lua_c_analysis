/**
 * @file lgc.c
 * @brief Lua垃圾回收器实现：三色标记-清除算法的完整实现
 *
 * 详细说明：
 * 本文件实现了Lua虚拟机的核心垃圾回收器，采用三色标记-清除算法
 * 进行自动内存管理。垃圾回收器负责自动回收不再使用的对象，
 * 防止内存泄漏，是Lua虚拟机内存管理的核心组件。
 *
 * 系统架构定位：
 * 垃圾回收器位于Lua虚拟机的内存管理层，与对象系统、字符串系统、
 * 表系统等核心组件紧密协作，提供透明的自动内存管理服务。
 *
 * 技术特点：
 * - 采用三色标记算法：白色(未访问)、灰色(已访问未处理)、黑色(已处理)
 * - 增量式垃圾回收：避免长时间停顿，提升用户体验
 * - 弱引用支持：支持弱键和弱值表，避免循环引用问题
 * - 终结器支持：为用户数据提供析构函数机制
 * - 写屏障机制：维护增量回收的正确性
 *
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为可达对象数量
 * - 空间复杂度：O(1)，原地标记算法
 * - 增量性能：每次步进处理固定数量对象，避免长时间停顿
 *
 * 内存安全考虑：
 * 垃圾回收器确保只回收真正不可达的对象，通过严格的可达性分析
 * 和写屏障机制保证内存安全。支持终结器的安全执行和弱引用的
 * 正确处理。
 *
 * 性能特征：
 * 增量式设计使得垃圾回收的停顿时间可控，适合交互式应用。
 * 通过调整回收参数可以在内存使用和回收频率之间取得平衡。
 *
 * 线程安全性：
 * 垃圾回收器设计为单线程使用，在多线程环境中需要适当的同步机制。
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2011-03-18
 * @since Lua 5.0
 * @see lobject.h, lstate.h, lmem.h
 */

#include <string.h>

#define lgc_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"

// 垃圾回收器配置常量
#define GCSTEPSIZE      1024u    // 每次GC步进处理的字节数
#define GCSWEEPMAX      40       // 每次清除阶段处理的最大对象数
#define GCSWEEPCOST     10       // 清除操作的成本估算
#define GCFINALIZECOST  100      // 终结器执行的成本估算

// 对象标记位操作宏
#define maskmarks       cast_byte(~(bitmask(BLACKBIT) | WHITEBITS))

/**
 * @brief 将对象标记为白色（未访问状态）
 * @param g 全局状态指针
 * @param x 要标记的对象指针
 *
 * 白色表示对象尚未被垃圾回收器访问，是垃圾回收的候选对象。
 * 在标记阶段开始时，所有对象都应该是白色的。
 */
#define makewhite(g, x) \
    ((x)->gch.marked = cast_byte(((x)->gch.marked & maskmarks) | luaC_white(g)))

/**
 * @brief 将对象从白色转换为灰色（已访问未处理状态）
 * @param x 要转换的对象指针
 *
 * 灰色表示对象已被访问但其引用的对象尚未全部处理。
 * 灰色对象会被放入灰色列表等待进一步处理。
 */
#define white2gray(x)   reset2bits((x)->gch.marked, WHITE0BIT, WHITE1BIT)

/**
 * @brief 将对象从黑色转换为灰色
 * @param x 要转换的对象指针
 *
 * 在写屏障触发时，可能需要将黑色对象重新标记为灰色，
 * 以确保增量回收的正确性。
 */
#define black2gray(x)   resetbit((x)->gch.marked, BLACKBIT)

/**
 * @brief 标记字符串对象
 * @param s 字符串对象指针
 *
 * 字符串对象的标记比较特殊，因为字符串是不可变的，
 * 可以直接标记而无需进一步处理其内容。
 */
#define stringmark(s)   reset2bits((s)->tsv.marked, WHITE0BIT, WHITE1BIT)

// 终结器相关宏定义
#define isfinalized(u)  testbit((u)->marked, FINALIZEDBIT)    // 检查是否已终结
#define markfinalized(u) l_setbit((u)->marked, FINALIZEDBIT)  // 标记为已终结

// 弱引用类型标志
#define KEYWEAK         bitmask(KEYWEAKBIT)    // 弱键标志
#define VALUEWEAK       bitmask(VALUEWEAKBIT)  // 弱值标志



/**
 * @brief 标记值对象（如果需要的话）
 * @param g 全局状态指针
 * @param o 要标记的值指针
 *
 * 这个宏用于标记TValue类型的值。首先检查值的一致性，
 * 然后判断是否为可回收对象且为白色，如果是则进行标记。
 * 这是垃圾回收器标记阶段的核心操作之一。
 */
#define markvalue(g, o) { \
    checkconsistency(o); \
    if (iscollectable(o) && iswhite(gcvalue(o))) \
        reallymarkobject(g, gcvalue(o)); \
}

/**
 * @brief 标记对象（如果是白色的话）
 * @param g 全局状态指针
 * @param t 要标记的对象指针
 *
 * 这个宏用于标记具体的对象类型。如果对象是白色的，
 * 则调用reallymarkobject进行实际的标记操作。
 */
#define markobject(g, t) { \
    if (iswhite(obj2gco(t))) \
        reallymarkobject(g, obj2gco(t)); \
}

/**
 * @brief 设置垃圾回收阈值
 * @param g 全局状态指针
 *
 * 根据当前内存估算值和GC暂停参数计算新的GC触发阈值。
 * 这个阈值决定了何时触发下一次垃圾回收周期。
 */
#define setthreshold(g) (g->GCthreshold = (g->estimate / 100) * g->gcpause)

/**
 * @brief 移除表中的空条目
 * @param n 要处理的节点指针
 *
 * 详细说明：
 * 当表节点的值为nil时，需要清理该条目。如果键是可回收对象，
 * 则将其标记为死键（DEADKEY），这样在后续的表遍历中可以
 * 跳过这些无效条目，提高表操作的效率。
 *
 * 实现要点：
 * - 首先断言值确实为nil
 * - 检查键是否为可回收对象
 * - 将可回收的键标记为DEADKEY类型
 *
 * 性能影响：
 * 及时清理空条目可以减少表的内存占用，提高哈希表的查找效率。
 */
static void removeentry(Node *n) {
    lua_assert(ttisnil(gval(n)));
    if (iscollectable(gkey(n))) {
        setttype(gkey(n), LUA_TDEADKEY);
    }
}

/**
 * @brief 真正执行对象标记的核心函数
 * @param g 全局状态指针
 * @param o 要标记的垃圾回收对象指针
 *
 * 详细说明：
 * 这是垃圾回收器标记阶段的核心函数，负责将白色对象标记为灰色或黑色。
 * 根据对象类型的不同，采用不同的标记策略：
 * - 简单对象（如字符串）直接标记为黑色
 * - 复杂对象（如表、函数）先标记为灰色，加入灰色列表待处理
 *
 * 算法流程：
 * 1. 将对象从白色转换为灰色
 * 2. 根据对象类型进行分类处理
 * 3. 对于复杂对象，加入灰色列表等待遍历
 * 4. 对于简单对象，直接完成标记
 *
 * 对象类型处理策略：
 * - STRING: 字符串不包含引用，直接返回（隐式转为黑色）
 * - USERDATA: 标记元表和环境，直接转为黑色
 * - UPVAL: 标记值，如果是闭合的upvalue则转为黑色
 * - FUNCTION/TABLE/THREAD/PROTO: 加入灰色列表，延迟处理
 *
 * 性能特征：
 * - 时间复杂度：O(1)，每个对象只标记一次
 * - 空间复杂度：O(1)，使用原地标记算法
 *
 * 内存安全：
 * 函数确保只处理活跃对象，通过断言检查对象状态的正确性。
 *
 * @pre o必须是白色且非死亡对象
 * @post 对象被标记为灰色或黑色，复杂对象加入灰色列表
 *
 * @note 这个函数是垃圾回收器的性能关键路径
 * @see white2gray(), gray2black(), markobject()
 */
static void reallymarkobject(global_State *g, GCObject *o) {
    lua_assert(iswhite(o) && !isdead(g, o));
    white2gray(o);

    switch (o->gch.tt) {
        case LUA_TSTRING: {
            // 字符串对象不包含其他对象的引用，可以直接完成标记
            return;
        }

        case LUA_TUSERDATA: {
            // 用户数据：标记元表和环境，然后直接转为黑色
            Table *mt = gco2u(o)->metatable;
            gray2black(o);    // 用户数据永远不保持灰色状态
            if (mt) {
                markobject(g, mt);
            }
            markobject(g, gco2u(o)->env);
            return;
        }

        case LUA_TUPVAL: {
            // 上值：标记其引用的值
            UpVal *uv = gco2uv(o);
            markvalue(g, uv->v);
            if (uv->v == &uv->u.value) {
                // 如果是闭合的上值，直接转为黑色
                gray2black(o);
            }
            // 开放的上值保持灰色，因为其值可能会改变
            return;
        }

        case LUA_TFUNCTION: {
            // 函数对象：加入灰色列表，延迟处理其常量和上值
            gco2cl(o)->c.gclist = g->gray;
            g->gray = o;
            break;
        }

        case LUA_TTABLE: {
            // 表对象：加入灰色列表，延迟处理其元素和元表
            gco2h(o)->gclist = g->gray;
            g->gray = o;
            break;
        }

        case LUA_TTHREAD: {
            // 线程对象：加入灰色列表，延迟处理其栈和上值
            gco2th(o)->gclist = g->gray;
            g->gray = o;
            break;
        }

        case LUA_TPROTO: {
            // 函数原型：加入灰色列表，延迟处理其常量和嵌套函数
            gco2p(o)->gclist = g->gray;
            g->gray = o;
            break;
        }

        default:
            lua_assert(0);    // 不应该到达这里
    }
}

/**
 * @brief 标记需要终结的用户数据
 * @param g 全局状态指针
 *
 * 详细说明：
 * 这个函数负责标记所有在tmudata列表中的用户数据对象。
 * 这些对象都是需要执行终结器（__gc元方法）的用户数据。
 *
 * 实现要点：
 * - 遍历tmudata循环链表中的所有对象
 * - 将每个对象重新标记为白色，然后立即标记
 * - 确保这些对象在当前GC周期中不会被回收
 *
 * 算法特点：
 * 使用do-while循环遍历循环链表，确保所有节点都被访问到。
 *
 * @note tmudata是一个循环链表，包含所有需要终结的用户数据
 * @see reallymarkobject(), luaC_separateudata()
 */
static void marktmu(global_State *g) {
    GCObject *u = g->tmudata;
    if (u) {
        do {
            u = u->gch.next;
            makewhite(g, u);        // 重置为白色
            reallymarkobject(g, u); // 立即标记，确保不被回收
        } while (u != g->tmudata);
    }
}

/**
 * @brief 分离需要终结的用户数据
 * @param L Lua状态机指针
 * @param all 是否处理所有用户数据（非零值）或仅处理白色对象（0）
 * @return 需要终结的用户数据的总内存大小
 *
 * 详细说明：
 * 这个函数扫描所有用户数据对象，将那些需要执行终结器的死亡对象
 * 从主对象列表中移除，并将它们添加到tmudata列表中等待终结。
 *
 * 算法流程：
 * 1. 遍历主线程的对象链表
 * 2. 对每个用户数据对象进行分类：
 *    - 活跃对象或已终结对象：跳过
 *    - 无终结器的死亡对象：标记为已终结，保留在原位置
 *    - 有终结器的死亡对象：移动到tmudata列表
 * 3. 统计需要终结的对象的内存大小
 *
 * 终结器处理策略：
 * - 检查对象的元表是否有__gc元方法
 * - 有终结器的对象需要特殊处理，不能立即释放
 * - 无终结器的对象可以直接标记为已终结
 *
 * 数据结构操作：
 * tmudata维护为循环链表，新对象插入到链表尾部。
 *
 * 内存管理：
 * 函数返回需要终结的对象的总内存大小，用于GC的内存估算。
 *
 * @pre L必须是有效的Lua状态机
 * @post 需要终结的用户数据被移动到tmudata列表
 *
 * @note 这个函数是垃圾回收原子阶段的重要组成部分
 * @see marktmu(), GCTM(), fasttm()
 */
size_t luaC_separateudata(lua_State *L, int all) {
    global_State *g = G(L);
    size_t deadmem = 0;                        // 需要终结的内存总量
    GCObject **p = &g->mainthread->next;      // 遍历指针
    GCObject *curr;

    while ((curr = *p) != NULL) {
        // 检查是否为用户数据且需要处理
        if (!(iswhite(curr) || all) || isfinalized(gco2u(curr))) {
            // 活跃对象或已终结对象，跳过
            p = &curr->gch.next;
        } else if (fasttm(L, gco2u(curr)->metatable, TM_GC) == NULL) {
            // 无终结器的死亡对象，标记为已终结但不移动
            markfinalized(gco2u(curr));
            p = &curr->gch.next;
        } else {
            // 有终结器的死亡对象，需要移动到tmudata列表
            deadmem += sizeudata(gco2u(curr));
            markfinalized(gco2u(curr));
            *p = curr->gch.next;               // 从原链表中移除

            // 插入到tmudata循环链表中
            if (g->tmudata == NULL) {
                // 创建新的循环链表
                g->tmudata = curr->gch.next = curr;
            } else {
                // 插入到现有循环链表的尾部
                curr->gch.next = g->tmudata->gch.next;
                g->tmudata->gch.next = curr;
                g->tmudata = curr;
            }
        }
    }
    return deadmem;
}


/**
 * @brief 遍历表对象并标记其内容
 * @param g 全局状态指针
 * @param h 要遍历的表指针
 * @return 如果表是弱引用表则返回1，否则返回0
 *
 * 详细说明：
 * 这是垃圾回收器中最复杂的遍历函数之一，负责处理Lua表的标记。
 * 表可能包含弱引用，需要特殊处理以避免阻止垃圾回收。
 *
 * 算法流程：
 * 1. 标记表的元表（如果存在）
 * 2. 检查元表的__mode元方法，确定弱引用类型
 * 3. 根据弱引用类型设置表的标记位
 * 4. 遍历并标记表的数组部分和哈希部分
 * 5. 清理空的表条目
 *
 * 弱引用处理：
 * - 'k': 弱键表，键不阻止垃圾回收
 * - 'v': 弱值表，值不阻止垃圾回收
 * - 'kv': 弱键值表，键和值都不阻止垃圾回收
 *
 * 性能优化：
 * - 对于弱键值表，直接返回而不标记任何内容
 * - 使用倒序遍历数组，提高缓存局部性
 * - 及时清理空条目，减少内存碎片
 *
 * 内存布局考虑：
 * 表由两部分组成：数组部分（连续存储）和哈希部分（节点存储）。
 * 遍历时需要分别处理这两个部分。
 *
 * 算法复杂度：
 * - 时间复杂度：O(n + m)，n为数组大小，m为哈希节点数
 * - 空间复杂度：O(1)，原地标记
 *
 * @pre h必须是有效的表对象
 * @post 表的可达内容被标记，弱引用表加入弱引用列表
 *
 * @note 弱引用表的处理是垃圾回收器的高级特性
 * @see removeentry(), markvalue(), gfasttm()
 */
static int traversetable(global_State *g, Table *h) {
    int i;
    int weakkey = 0;      // 是否为弱键表
    int weakvalue = 0;    // 是否为弱值表
    const TValue *mode;

    // 首先标记表的元表
    if (h->metatable) {
        markobject(g, h->metatable);
    }

    // 检查__mode元方法以确定弱引用类型
    mode = gfasttm(g, h->metatable, TM_MODE);
    if (mode && ttisstring(mode)) {
        // 解析模式字符串，检查是否包含'k'或'v'
        weakkey = (strchr(svalue(mode), 'k') != NULL);
        weakvalue = (strchr(svalue(mode), 'v') != NULL);

        if (weakkey || weakvalue) {
            // 设置弱引用标记位
            h->marked &= ~(KEYWEAK | VALUEWEAK);    // 清除旧标记
            h->marked |= cast_byte((weakkey << KEYWEAKBIT) |
                                   (weakvalue << VALUEWEAKBIT));

            // 将表加入弱引用列表，等待特殊处理
            h->gclist = g->weak;
            g->weak = obj2gco(h);
        }
    }

    // 如果是弱键值表，不标记任何内容
    if (weakkey && weakvalue) {
        return 1;
    }

    // 遍历数组部分（如果不是弱值表）
    if (!weakvalue) {
        i = h->sizearray;
        while (i--) {
            markvalue(g, &h->array[i]);
        }
    }

    // 遍历哈希部分
    i = sizenode(h);
    while (i--) {
        Node *n = gnode(h, i);
        lua_assert(ttype(gkey(n)) != LUA_TDEADKEY || ttisnil(gval(n)));

        if (ttisnil(gval(n))) {
            // 值为nil的条目，清理键
            removeentry(n);
        } else {
            // 有效条目，根据弱引用类型选择性标记
            lua_assert(!ttisnil(gkey(n)));
            if (!weakkey) {
                markvalue(g, gkey(n));    // 标记键（如果不是弱键）
            }
            if (!weakvalue) {
                markvalue(g, gval(n));    // 标记值（如果不是弱值）
            }
        }
    }

    return weakkey || weakvalue;
}


/**
 * @brief 遍历函数原型对象并标记其内容
 * @param g 全局状态指针
 * @param f 要遍历的函数原型指针
 *
 * 详细说明：
 * 函数原型（Proto）是Lua编译器生成的字节码函数的模板，包含了
 * 函数的所有静态信息：常量表、上值名称、嵌套函数、局部变量信息等。
 * 这个函数负责标记原型中引用的所有对象。
 *
 * 遍历内容：
 * 1. 源代码字符串：函数定义所在的源文件名
 * 2. 常量表：函数中使用的所有字面量常量
 * 3. 上值名称：函数访问的外部变量名称
 * 4. 嵌套函数：函数内部定义的子函数原型
 * 5. 局部变量：用于调试信息的变量名称
 *
 * 数据结构组织：
 * - k[]: 常量数组，存储数字、字符串、布尔值等常量
 * - upvalues[]: 上值名称数组，存储外部变量的名称字符串
 * - p[]: 嵌套函数数组，存储子函数的原型对象
 * - locvars[]: 局部变量数组，存储调试用的变量信息
 *
 * 性能考虑：
 * 函数原型通常在编译时创建，运行时共享，标记频率相对较低。
 * 但由于可能包含大量常量和嵌套函数，遍历成本可能较高。
 *
 * 内存管理：
 * 函数原型的生命周期通常较长，只有在函数不再被引用时才会被回收。
 * 嵌套函数形成的引用链可能较深，需要递归标记。
 *
 * 调试信息处理：
 * 局部变量名称主要用于调试，在生产环境中可能被优化掉。
 *
 * @pre f必须是有效的函数原型对象
 * @post 原型中的所有可达对象被标记
 *
 * @note 函数原型是Lua编译器输出的核心数据结构
 * @see stringmark(), markvalue(), markobject()
 */
static void traverseproto(global_State *g, Proto *f) {
    int i;

    // 标记源代码字符串
    if (f->source) {
        stringmark(f->source);
    }

    // 遍历常量表，标记所有常量值
    for (i = 0; i < f->sizek; i++) {
        markvalue(g, &f->k[i]);
    }

    // 遍历上值名称数组
    for (i = 0; i < f->sizeupvalues; i++) {
        if (f->upvalues[i]) {
            stringmark(f->upvalues[i]);
        }
    }

    // 遍历嵌套函数原型
    for (i = 0; i < f->sizep; i++) {
        if (f->p[i]) {
            markobject(g, f->p[i]);
        }
    }

    // 遍历局部变量信息（主要用于调试）
    for (i = 0; i < f->sizelocvars; i++) {
        if (f->locvars[i].varname) {
            stringmark(f->locvars[i].varname);
        }
    }
}



/**
 * @brief 遍历闭包对象并标记其内容
 * @param g 全局状态指针
 * @param cl 要遍历的闭包指针
 *
 * 详细说明：
 * 闭包是Lua中函数的运行时表示，包含函数代码和其捕获的外部变量。
 * Lua支持两种类型的闭包：C闭包和Lua闭包，它们的内存布局和
 * 上值处理方式不同。
 *
 * 闭包类型差异：
 * 1. C闭包（isC = true）：
 *    - 由C函数创建，上值直接存储为TValue数组
 *    - 上值数量在创建时确定，不能动态改变
 *    - 没有函数原型，只有函数指针
 *
 * 2. Lua闭包（isC = false）：
 *    - 由Lua函数创建，上值存储为UpVal对象指针数组
 *    - 上值可能是开放的（指向栈）或闭合的（独立存储）
 *    - 包含函数原型，定义了函数的字节码和元信息
 *
 * 标记策略：
 * - 环境表：所有闭包都有环境表，用于全局变量查找
 * - C闭包上值：直接标记TValue数组中的值
 * - Lua闭包上值：标记UpVal对象，由UpVal负责标记实际值
 * - Lua闭包原型：标记函数原型，包含常量和嵌套函数
 *
 * 内存管理考虑：
 * 闭包的生命周期与其上值密切相关。开放上值与栈帧绑定，
 * 闭合上值独立存在。垃圾回收器需要正确处理这种复杂关系。
 *
 * 性能影响：
 * 闭包遍历的成本主要取决于上值数量。深度嵌套的函数可能
 * 产生大量上值，影响垃圾回收性能。
 *
 * @pre cl必须是有效的闭包对象
 * @post 闭包的环境、原型和上值被正确标记
 *
 * @note 上值的处理是Lua闭包实现的核心机制
 * @see markobject(), markvalue(), UpVal结构
 */
static void traverseclosure(global_State *g, Closure *cl) {
    // 标记闭包的环境表（所有闭包都有环境）
    markobject(g, cl->c.env);

    if (cl->c.isC) {
        // C闭包：上值直接存储为TValue数组
        int i;
        for (i = 0; i < cl->c.nupvalues; i++) {
            markvalue(g, &cl->c.upvalue[i]);
        }
    } else {
        // Lua闭包：处理函数原型和UpVal对象
        int i;
        lua_assert(cl->l.nupvalues == cl->l.p->nups);

        // 标记函数原型（包含字节码、常量等）
        markobject(g, cl->l.p);

        // 标记所有上值对象
        for (i = 0; i < cl->l.nupvalues; i++) {
            markobject(g, cl->l.upvals[i]);
        }
    }
}


/**
 * @brief 检查并调整Lua栈的大小
 * @param L Lua状态机指针
 * @param max 栈的最大使用位置
 *
 * 详细说明：
 * 这个函数在垃圾回收期间检查Lua栈的使用情况，如果栈空间
 * 使用率过低，则缩减栈大小以节省内存。这是一种自适应的
 * 内存管理策略。
 *
 * 栈结构说明：
 * Lua维护两个栈：
 * 1. 数据栈（stack）：存储Lua值，用于函数调用和局部变量
 * 2. 调用信息栈（ci）：存储函数调用信息，用于调用链管理
 *
 * 收缩策略：
 * - 当使用量小于总容量的1/4时考虑收缩
 * - 收缩后的大小为当前大小的一半
 * - 保持最小大小限制，避免频繁的重新分配
 *
 * 安全检查：
 * - 检查是否处于栈溢出处理状态
 * - 确保收缩后仍有足够空间
 * - 使用条件编译的硬栈测试进行验证
 *
 * 性能考虑：
 * 栈收缩是一个相对昂贵的操作，涉及内存重新分配和数据复制。
 * 因此只在使用率显著降低时才执行，避免频繁的大小调整。
 *
 * 内存管理：
 * 及时收缩栈空间可以减少程序的内存占用，特别是在递归
 * 调用结束后或大量局部变量超出作用域后。
 *
 * 调试支持：
 * condhardstacktests宏在调试模式下会执行额外的栈完整性检查。
 *
 * @pre L必须是有效的Lua状态机，max必须是有效的栈位置
 * @post 栈大小可能被调整以优化内存使用
 *
 * @note 这是垃圾回收器内存优化的重要组成部分
 * @see luaD_reallocCI(), luaD_reallocstack()
 */
static void checkstacksizes(lua_State *L, StkId max) {
    // 计算当前使用的调用信息数量
    int ci_used = cast_int(L->ci - L->base_ci);

    // 计算当前使用的栈空间大小
    int s_used = cast_int(max - L->stack);

    // 如果正在处理栈溢出，不要调整栈大小
    if (L->size_ci > LUAI_MAXCALLS) {
        return;
    }

    // 检查调用信息栈是否可以收缩
    if (4 * ci_used < L->size_ci && 2 * BASIC_CI_SIZE < L->size_ci) {
        // 使用率低于25%且大于最小大小，收缩到一半
        luaD_reallocCI(L, L->size_ci / 2);
    }

    // 调试模式下的硬栈测试
    condhardstacktests(luaD_reallocCI(L, ci_used + 1));

    // 检查数据栈是否可以收缩
    if (4 * s_used < L->stacksize &&
        2 * (BASIC_STACK_SIZE + EXTRA_STACK) < L->stacksize) {
        // 使用率低于25%且大于最小大小，收缩到一半
        luaD_reallocstack(L, L->stacksize / 2);
    }

    // 调试模式下的硬栈测试
    condhardstacktests(luaD_reallocstack(L, s_used));
}


/**
 * @brief 遍历Lua线程的栈并标记其内容
 * @param g 全局状态指针
 * @param l 要遍历的Lua线程状态指针
 *
 * 详细说明：
 * 这个函数负责遍历Lua线程的整个执行栈，标记所有活跃的值。
 * 栈遍历是垃圾回收器确定对象可达性的关键步骤，必须准确
 * 标记所有可能被访问的值。
 *
 * 遍历策略：
 * 1. 标记全局表：线程的全局环境
 * 2. 计算栈的有效范围：考虑所有调用帧的栈顶
 * 3. 标记活跃区域：从栈底到当前栈顶的所有值
 * 4. 清理无效区域：将超出活跃范围的栈位置设为nil
 * 5. 优化栈大小：根据使用情况调整栈容量
 *
 * 栈布局理解：
 * - stack: 栈的起始地址
 * - top: 当前栈顶位置
 * - stack_last: 栈的最大可用位置
 * - 每个调用帧都有自己的栈顶位置
 *
 * 调用帧处理：
 * 遍历所有活跃的调用帧，找出最高的栈顶位置。这确保了
 * 即使某些调用帧暂时不活跃，其栈空间也不会被错误清理。
 *
 * 内存清理：
 * 将超出有效范围的栈位置设为nil，这样可以：
 * - 释放这些位置引用的对象
 * - 为后续的栈操作提供干净的环境
 * - 避免悬挂指针问题
 *
 * 性能优化：
 * 在遍历结束后调用checkstacksizes进行栈大小优化，
 * 这是一个合适的时机，因为此时已经确定了栈的实际使用情况。
 *
 * 线程安全：
 * 这个函数假设在单线程环境中运行，或者调用者已经确保
 * 适当的同步。
 *
 * @pre l必须是有效的Lua线程状态
 * @post 线程栈中的所有可达值被标记，无效区域被清理
 *
 * @note 栈遍历是垃圾回收器的核心操作之一
 * @see markvalue(), checkstacksizes(), CallInfo结构
 */
static void traversestack(global_State *g, lua_State *l) {
    StkId o, lim;
    CallInfo *ci;

    // 标记线程的全局表
    markvalue(g, gt(l));

    // 计算栈的有效范围：找出所有调用帧中最高的栈顶
    lim = l->top;
    for (ci = l->base_ci; ci <= l->ci; ci++) {
        lua_assert(ci->top <= l->stack_last);
        if (lim < ci->top) {
            lim = ci->top;
        }
    }

    // 标记栈中的活跃值（从栈底到当前栈顶）
    for (o = l->stack; o < l->top; o++) {
        markvalue(g, o);
    }

    // 清理超出活跃范围的栈位置
    for (; o <= lim; o++) {
        setnilvalue(o);
    }

    // 检查并优化栈大小
    checkstacksizes(l, lim);
}


/**
 * @brief 传播标记：处理一个灰色对象，将其转为黑色
 * @param g 全局状态指针
 * @return 遍历的对象大小（字节数）
 *
 * 详细说明：
 * 这是垃圾回收器标记阶段的核心函数，实现了三色标记算法的
 * "传播"步骤。它从灰色列表中取出一个对象，遍历其内容，
 * 然后将对象标记为黑色。
 *
 * 三色标记算法：
 * - 白色：未访问的对象，垃圾回收的候选
 * - 灰色：已访问但内容未完全处理的对象
 * - 黑色：已访问且内容完全处理的对象
 *
 * 算法不变式：
 * 黑色对象不能直接引用白色对象。这个不变式确保了
 * 标记的正确性和完整性。
 *
 * 对象类型处理策略：
 * 1. 表（TABLE）：遍历所有键值对，处理弱引用特殊情况
 * 2. 函数（FUNCTION）：遍历闭包的上值和环境
 * 3. 线程（THREAD）：遍历栈，加入grayagain列表处理写屏障
 * 4. 原型（PROTO）：遍历常量、嵌套函数等静态数据
 *
 * 弱引用处理：
 * 弱引用表在遍历后可能重新变为灰色，因为其内容可能
 * 在后续的垃圾回收过程中发生变化。
 *
 * 线程特殊处理：
 * 线程对象需要特殊处理，因为其栈内容可能在垃圾回收
 * 过程中发生变化（写屏障），所以加入grayagain列表。
 *
 * 内存计算：
 * 函数返回遍历对象的内存大小，用于垃圾回收器的
 * 进度估算和性能调优。
 *
 * 性能特征：
 * - 时间复杂度：O(n)，n为对象包含的引用数量
 * - 空间复杂度：O(1)，原地标记算法
 *
 * 增量回收支持：
 * 返回的大小信息用于控制增量回收的步进，确保
 * 每次处理的工作量相对均匀。
 *
 * @pre g->gray必须指向有效的灰色对象
 * @post 对象被标记为黑色，其引用的对象被适当标记
 *
 * @note 这是垃圾回收器性能的关键函数
 * @see traversetable(), traverseclosure(), traversestack(), traverseproto()
 */
static l_mem propagatemark(global_State *g) {
    GCObject *o = g->gray;    // 获取灰色列表的第一个对象
    lua_assert(isgray(o));
    gray2black(o);            // 将对象标记为黑色

    switch (o->gch.tt) {
        case LUA_TTABLE: {
            Table *h = gco2h(o);
            g->gray = h->gclist;    // 更新灰色列表头

            if (traversetable(g, h)) {
                // 如果是弱引用表，重新标记为灰色
                black2gray(o);
            }

            // 返回表的内存大小
            return sizeof(Table) + sizeof(TValue) * h->sizearray +
                                   sizeof(Node) * sizenode(h);
        }

        case LUA_TFUNCTION: {
            Closure *cl = gco2cl(o);
            g->gray = cl->c.gclist;    // 更新灰色列表头
            traverseclosure(g, cl);

            // 根据闭包类型返回相应的内存大小
            return (cl->c.isC) ? sizeCclosure(cl->c.nupvalues) :
                                 sizeLclosure(cl->l.nupvalues);
        }

        case LUA_TTHREAD: {
            lua_State *th = gco2th(o);
            g->gray = th->gclist;      // 更新灰色列表头

            // 将线程加入grayagain列表，处理可能的写屏障
            th->gclist = g->grayagain;
            g->grayagain = o;
            black2gray(o);             // 重新标记为灰色

            traversestack(g, th);

            // 返回线程的内存大小
            return sizeof(lua_State) + sizeof(TValue) * th->stacksize +
                                       sizeof(CallInfo) * th->size_ci;
        }

        case LUA_TPROTO: {
            Proto *p = gco2p(o);
            g->gray = p->gclist;       // 更新灰色列表头
            traverseproto(g, p);

            // 返回函数原型的内存大小
            return sizeof(Proto) + sizeof(Instruction) * p->sizecode +
                                   sizeof(Proto *) * p->sizep +
                                   sizeof(TValue) * p->sizek +
                                   sizeof(int) * p->sizelineinfo +
                                   sizeof(LocVar) * p->sizelocvars +
                                   sizeof(TString *) * p->sizeupvalues;
        }

        default:
            lua_assert(0);    // 不应该到达这里
            return 0;
    }
}


/**
 * @brief 传播所有标记：处理完整个灰色列表
 * @param g 全局状态指针
 * @return 总共遍历的内存大小
 *
 * 详细说明：
 * 这个函数执行完整的标记传播过程，持续处理灰色列表中的对象
 * 直到列表为空。这是标记阶段的主要驱动函数。
 *
 * 算法特点：
 * - 循环调用propagatemark直到没有灰色对象
 * - 累计所有遍历的内存大小
 * - 确保所有可达对象都被正确标记
 *
 * 性能考虑：
 * 这个函数可能处理大量对象，执行时间取决于对象图的复杂度。
 * 在增量垃圾回收中，通常不会一次性调用这个函数。
 *
 * @note 主要用于原子标记阶段
 * @see propagatemark()
 */
static size_t propagateall(global_State *g) {
    size_t m = 0;
    while (g->gray) {
        m += propagatemark(g);
    }
    return m;
}

/**
 * @brief 判断弱引用表中的键或值是否应该被清除
 * @param o 要检查的值指针
 * @param iskey 非零表示检查的是键，零表示检查的是值
 * @return 如果应该清除则返回非零值，否则返回0
 *
 * 详细说明：
 * 弱引用表允许其键或值被垃圾回收，即使表本身仍然可达。
 * 这个函数实现了弱引用的核心语义，决定哪些条目应该被清除。
 *
 * 清除规则：
 * 1. 非可回收对象（如数字、布尔值）：永不清除
 * 2. 字符串对象：永不清除（字符串被视为"值"语义）
 * 3. 其他可回收对象：
 *    - 如果已被回收（白色），则清除
 *    - 用户数据特殊规则：正在终结的用户数据在值位置清除，键位置保留
 *
 * 字符串特殊处理：
 * 字符串在Lua中具有特殊地位，被视为不可变的值类型。
 * 即使在弱引用表中，字符串也不会被清除，并且会被重新标记。
 *
 * 用户数据终结处理：
 * 正在执行终结器的用户数据在弱值表中会被清除，但在弱键表中
 * 会被保留，这确保了终结器执行期间的一致性。
 *
 * 实现细节：
 * - 使用iswhite检查对象是否已被标记为垃圾
 * - 使用isfinalized检查用户数据是否正在终结
 * - 对字符串进行重新标记以确保其不被回收
 *
 * @pre o必须是有效的TValue指针
 * @post 字符串对象可能被重新标记
 *
 * @note 这是弱引用语义实现的核心函数
 * @see cleartable(), stringmark()
 */
static int iscleared(const TValue *o, int iskey) {
    // 非可回收对象永不清除
    if (!iscollectable(o)) {
        return 0;
    }

    // 字符串特殊处理：永不清除，并重新标记
    if (ttisstring(o)) {
        stringmark(rawtsvalue(o));    // 字符串被视为"值"，永不弱化
        return 0;
    }

    // 其他可回收对象的清除条件
    return iswhite(gcvalue(o)) ||
           (ttisuserdata(o) && (!iskey && isfinalized(uvalue(o))));
}


/**
 * @brief 清理弱引用表中已被回收的条目
 * @param l 弱引用表链表的头指针
 *
 * 详细说明：
 * 这是弱引用机制实现的核心函数，负责在垃圾回收完成后清理
 * 弱引用表中指向已被回收对象的条目。弱引用允许表中的键或值
 * 被垃圾回收，即使表本身仍然可达。
 *
 * 弱引用语义：
 * - 弱键表：键被回收时，整个键值对被移除
 * - 弱值表：值被回收时，该条目的值被设为nil
 * - 弱键值表：键或值任一被回收时，整个条目被移除
 *
 * 清理策略：
 * 1. 数组部分：仅当表是弱值表时才清理
 * 2. 哈希部分：根据键和值的回收状态决定清理方式
 *
 * 算法流程：
 * 1. 遍历弱引用表链表中的每个表
 * 2. 对于弱值表，清理数组部分中已回收的值
 * 3. 对于哈希部分，检查每个非空条目：
 *    - 如果键被回收（弱键表）或值被回收（弱值表），清理条目
 *    - 使用removeentry清理键，确保表的一致性
 *
 * 内存管理考虑：
 * - 及时清理死亡引用，避免内存泄漏
 * - 保持表结构的完整性和哈希一致性
 * - 减少表中的无效条目，提高访问效率
 *
 * 性能特征：
 * - 时间复杂度：O(n)，n为所有弱引用表的条目总数
 * - 空间复杂度：O(1)，原地清理
 *
 * 实现细节：
 * - 使用倒序遍历数组，提高缓存局部性
 * - 对哈希节点进行完整性检查
 * - 通过gclist链接遍历所有弱引用表
 *
 * 弱引用的应用场景：
 * - 缓存系统：避免缓存阻止对象回收
 * - 观察者模式：避免观察者列表阻止对象回收
 * - 反向映射：建立不影响生命周期的反向引用
 *
 * @pre l指向的链表中的所有表都必须是弱引用表
 * @post 所有弱引用表中的死亡引用被清理
 *
 * @note 这是弱引用语义实现的关键函数
 * @see iscleared(), removeentry(), traversetable()
 */
static void cleartable(GCObject *l) {
    while (l) {
        Table *h = gco2h(l);
        int i = h->sizearray;

        // 断言：表必须是弱引用表
        lua_assert(testbit(h->marked, VALUEWEAKBIT) ||
                   testbit(h->marked, KEYWEAKBIT));

        // 清理数组部分（仅对弱值表）
        if (testbit(h->marked, VALUEWEAKBIT)) {
            while (i--) {
                TValue *o = &h->array[i];
                if (iscleared(o, 0)) {    // 值被回收了吗？
                    setnilvalue(o);       // 移除值
                }
            }
        }

        // 清理哈希部分
        i = sizenode(h);
        while (i--) {
            Node *n = gnode(h, i);

            // 检查非空条目
            if (!ttisnil(gval(n)) &&
                (iscleared(key2tval(n), 1) || iscleared(gval(n), 0))) {
                // 键或值被回收，清理整个条目
                setnilvalue(gval(n));    // 移除值
                removeentry(n);          // 清理键，维护表结构
            }
        }

        // 移动到链表中的下一个弱引用表
        l = h->gclist;
    }
}


/**
 * @brief 释放垃圾回收对象的内存
 * @param L Lua状态机指针
 * @param o 要释放的垃圾回收对象指针
 *
 * 详细说明：
 * 这是垃圾回收器的最终步骤，负责释放已确定为垃圾的对象。
 * 不同类型的对象需要不同的释放策略，以确保所有相关资源
 * 都被正确清理。
 *
 * 对象类型释放策略：
 * 1. PROTO（函数原型）：释放字节码、常量表、调试信息等
 * 2. FUNCTION（闭包）：释放上值数组和相关结构
 * 3. UPVAL（上值）：释放上值对象，处理开放/闭合状态
 * 4. TABLE（表）：释放数组和哈希部分的内存
 * 5. THREAD（线程）：释放栈空间和调用信息
 * 6. STRING（字符串）：更新字符串表统计，释放字符串内存
 * 7. USERDATA（用户数据）：释放用户数据块
 *
 * 特殊处理：
 * - 线程释放：确保不释放当前线程或主线程
 * - 字符串释放：需要更新全局字符串表的使用计数
 * - 其他对象：直接调用相应的专用释放函数
 *
 * 内存管理：
 * 每种对象类型都有专门的释放函数，这些函数了解对象的
 * 内部结构，能够正确释放所有相关的内存块。
 *
 * 安全考虑：
 * - 线程释放前检查是否为当前线程或主线程
 * - 使用断言确保对象类型的有效性
 * - 调用专用释放函数，避免内存泄漏
 *
 * 性能特征：
 * - 时间复杂度：O(1)对于简单对象，O(n)对于复杂对象
 * - 空间复杂度：O(1)，直接释放内存
 *
 * 调试支持：
 * 在调试模式下，释放函数可能执行额外的完整性检查，
 * 确保对象状态的正确性。
 *
 * @pre o必须是有效的垃圾回收对象且已确定为垃圾
 * @post 对象的所有内存被释放，相关统计信息被更新
 *
 * @note 这是垃圾回收器的最终清理步骤
 * @see luaF_freeproto(), luaF_freeclosure(), luaH_free(), luaE_freethread()
 */
static void freeobj(lua_State *L, GCObject *o) {
    switch (o->gch.tt) {
        case LUA_TPROTO:
            luaF_freeproto(L, gco2p(o));
            break;

        case LUA_TFUNCTION:
            luaF_freeclosure(L, gco2cl(o));
            break;

        case LUA_TUPVAL:
            luaF_freeupval(L, gco2uv(o));
            break;

        case LUA_TTABLE:
            luaH_free(L, gco2h(o));
            break;

        case LUA_TTHREAD: {
            // 安全检查：不能释放当前线程或主线程
            lua_assert(gco2th(o) != L && gco2th(o) != G(L)->mainthread);
            luaE_freethread(L, gco2th(o));
            break;
        }

        case LUA_TSTRING: {
            // 更新字符串表的使用计数
            G(L)->strt.nuse--;
            luaM_freemem(L, o, sizestring(gco2ts(o)));
            break;
        }

        case LUA_TUSERDATA: {
            luaM_freemem(L, o, sizeudata(gco2u(o)));
            break;
        }

        default:
            lua_assert(0);    // 不应该到达这里
    }
}



/**
 * @brief 清扫整个对象列表的宏定义
 * @param L Lua状态机指针
 * @param p 对象列表指针的指针
 *
 * 这个宏用于清扫整个对象列表，不限制处理的对象数量。
 * 主要用于非增量的完整清扫操作。
 */
#define sweepwholelist(L, p) sweeplist(L, p, MAX_LUMEM)

/**
 * @brief 清扫对象列表，释放死亡对象
 * @param L Lua状态机指针
 * @param p 对象列表指针的指针
 * @param count 最大处理对象数量（用于增量清扫）
 * @return 指向下一个未处理对象的指针的指针
 *
 * 详细说明：
 * 这是垃圾回收器清扫阶段的核心函数，负责遍历对象列表，
 * 识别并释放死亡对象，同时为存活对象准备下一轮垃圾回收。
 *
 * 清扫算法：
 * 1. 遍历对象链表，检查每个对象的标记状态
 * 2. 对于存活对象：重新标记为白色，为下一轮GC做准备
 * 3. 对于死亡对象：从链表中移除并释放内存
 * 4. 特殊处理线程对象：递归清扫其开放上值列表
 *
 * 三色标记的清扫语义：
 * - 当前白色：死亡对象，需要被回收
 * - 其他白色：存活对象，标记为新的白色
 * - 灰色/黑色：存活对象，重新标记为白色
 *
 * 增量清扫支持：
 * count参数限制了单次清扫的对象数量，支持增量垃圾回收。
 * 函数返回下一个未处理对象的位置，便于后续继续清扫。
 *
 * 线程特殊处理：
 * 线程对象包含开放上值列表，这些上值也需要被清扫。
 * 使用递归调用确保所有相关对象都被正确处理。
 *
 * 链表维护：
 * - 存活对象：保留在链表中，更新其标记
 * - 死亡对象：从链表中移除，调整链表指针
 * - 根节点处理：如果根节点被删除，需要更新根指针
 *
 * 内存管理：
 * 死亡对象通过freeobj函数释放，该函数根据对象类型
 * 调用相应的专用释放函数。
 *
 * 性能特征：
 * - 时间复杂度：O(min(n, count))，n为列表长度
 * - 空间复杂度：O(1)，原地操作
 *
 * 安全考虑：
 * - 使用断言检查对象状态的一致性
 * - 正确处理固定对象（FIXEDBIT）
 * - 维护链表结构的完整性
 *
 * @pre p指向有效的对象列表，count为非负数
 * @post 最多count个对象被处理，死亡对象被释放
 *
 * @note 这是垃圾回收器清扫阶段的核心实现
 * @see freeobj(), makewhite(), otherwhite()
 */
static GCObject **sweeplist(lua_State *L, GCObject **p, lu_mem count) {
    GCObject *curr;
    global_State *g = G(L);
    int deadmask = otherwhite(g);    // 当前周期的死亡白色标记

    while ((curr = *p) != NULL && count-- > 0) {
        // 特殊处理：线程对象需要清扫其开放上值列表
        if (curr->gch.tt == LUA_TTHREAD) {
            sweepwholelist(L, &gco2th(curr)->openupval);
        }

        // 检查对象是否存活
        if ((curr->gch.marked ^ WHITEBITS) & deadmask) {
            // 对象存活：重新标记为白色，准备下一轮GC
            lua_assert(!isdead(g, curr) || testbit(curr->gch.marked, FIXEDBIT));
            makewhite(g, curr);
            p = &curr->gch.next;
        } else {
            // 对象死亡：从链表中移除并释放
            lua_assert(isdead(g, curr) || deadmask == bitmask(SFIXEDBIT));
            *p = curr->gch.next;

            // 如果删除的是根节点，需要更新根指针
            if (curr == g->rootgc) {
                g->rootgc = curr->gch.next;
            }

            freeobj(L, curr);
        }
    }

    return p;    // 返回下一个未处理对象的位置
}


/**
 * @brief 检查并调整全局数据结构的大小
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 这个函数在垃圾回收完成后检查全局数据结构的使用情况，
 * 如果发现某些结构的使用率过低，则缩减其大小以节省内存。
 * 这是一种自适应的内存管理策略。
 *
 * 检查的数据结构：
 * 1. 字符串哈希表（strt）：Lua的全局字符串池
 * 2. 全局缓冲区（buff）：用于字符串操作和I/O的临时缓冲区
 *
 * 字符串表优化：
 * - 检查条件：使用量 < 总容量/4 且 总容量 > 最小大小*2
 * - 优化策略：将表大小缩减为当前大小的一半
 * - 目的：减少内存占用，提高缓存效率
 *
 * 缓冲区优化：
 * - 检查条件：缓冲区大小 > 最小缓冲区大小*2
 * - 优化策略：将缓冲区大小缩减为当前大小的一半
 * - 目的：释放不必要的缓冲区内存
 *
 * 自适应策略：
 * 这种策略允许数据结构在需要时增长，在不需要时收缩，
 * 实现了内存使用的动态平衡。
 *
 * 性能考虑：
 * - 缩减操作相对昂贵，只在使用率显著降低时执行
 * - 保持最小大小限制，避免频繁的重新分配
 * - 在垃圾回收后执行，此时系统处于相对稳定状态
 *
 * 内存管理效益：
 * - 减少长期内存占用
 * - 提高内存局部性
 * - 避免内存碎片化
 *
 * 时机选择：
 * 在垃圾回收完成后调用此函数是合适的，因为：
 * - 此时内存使用情况相对稳定
 * - 可以准确评估数据结构的实际需求
 * - 不会干扰正常的程序执行
 *
 * @pre L必须是有效的Lua状态机
 * @post 全局数据结构的大小可能被优化
 *
 * @note 这是垃圾回收器内存优化的重要组成部分
 * @see luaS_resize(), luaZ_resizebuffer()
 */
static void checkSizes(lua_State *L) {
    global_State *g = G(L);

    // 检查字符串哈希表的大小
    if (g->strt.nuse < cast(lu_int32, g->strt.size / 4) &&
        g->strt.size > MINSTRTABSIZE * 2) {
        // 使用率低于25%且大于最小大小，缩减到一半
        luaS_resize(L, g->strt.size / 2);
    }

    // 检查全局缓冲区的大小
    if (luaZ_sizebuffer(&g->buff) > LUA_MINBUFFER * 2) {
        // 缓冲区过大，缩减到一半
        size_t newsize = luaZ_sizebuffer(&g->buff) / 2;
        luaZ_resizebuffer(L, &g->buff, newsize);
    }
}


/**
 * @brief 执行垃圾回收终结器（__gc元方法）
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 这个函数负责执行用户数据的终结器（__gc元方法）。终结器
 * 是Lua提供的一种析构机制，允许用户在对象被回收前执行
 * 清理操作。
 *
 * 终结器执行流程：
 * 1. 从tmudata循环链表中取出第一个用户数据
 * 2. 将用户数据从tmudata列表移除
 * 3. 将用户数据重新加入主对象列表
 * 4. 重新标记为白色，使其可以在下次GC中被回收
 * 5. 查找并执行__gc元方法
 *
 * 链表操作：
 * tmudata是一个循环链表，包含所有需要执行终结器的用户数据。
 * 执行完终结器后，用户数据被移回主对象列表，可以正常参与
 * 下一轮垃圾回收。
 *
 * 终结器调用环境：
 * - 禁用调试钩子：避免终结器执行期间的调试干扰
 * - 提高GC阈值：防止终结器执行期间触发新的GC
 * - 恢复原始设置：确保终结器执行不影响正常运行
 *
 * 安全考虑：
 * - 终结器可能分配新对象或触发错误
 * - 需要防止终结器执行期间的递归GC
 * - 保护调试状态不被终结器影响
 *
 * 执行语义：
 * - 终结器以用户数据作为唯一参数调用
 * - 终结器的返回值被忽略
 * - 终结器中的错误会被传播到调用者
 *
 * 内存管理：
 * 执行终结器后，用户数据重新变为白色，可以在下次GC中
 * 被正常回收（如果没有新的引用）。
 *
 * 性能影响：
 * 终结器的执行可能比较昂贵，因为它涉及：
 * - Lua函数调用的开销
 * - 可能的内存分配
 * - 用户定义的清理逻辑
 *
 * 使用场景：
 * - 文件句柄的关闭
 * - 网络连接的断开
 * - 外部资源的释放
 * - 清理操作的执行
 *
 * @pre g->tmudata必须非空，包含需要终结的用户数据
 * @post 一个用户数据的终结器被执行，对象重新进入正常GC流程
 *
 * @note 终结器是Lua资源管理的重要机制
 * @see luaC_separateudata(), fasttm(), luaD_call()
 */
static void GCTM(lua_State *L) {
    global_State *g = G(L);
    GCObject *o = g->tmudata->gch.next;    // 获取第一个元素
    Udata *udata = rawgco2u(o);
    const TValue *tm;

    // 从tmudata循环链表中移除用户数据
    if (o == g->tmudata) {
        // 这是最后一个元素
        g->tmudata = NULL;
    } else {
        g->tmudata->gch.next = udata->uv.next;
    }

    // 将用户数据重新加入主对象列表
    udata->uv.next = g->mainthread->next;
    g->mainthread->next = o;
    makewhite(g, o);    // 重新标记为白色

    // 查找__gc元方法
    tm = fasttm(L, udata->uv.metatable, TM_GC);
    if (tm != NULL) {
        // 保存当前状态
        lu_byte oldah = L->allowhook;
        lu_mem oldt = g->GCthreshold;

        // 设置终结器执行环境
        L->allowhook = 0;                    // 禁用调试钩子
        g->GCthreshold = 2 * g->totalbytes;  // 避免GC步进

        // 准备函数调用：终结器函数 + 用户数据参数
        setobj2s(L, L->top, tm);
        setuvalue(L, L->top + 1, udata);
        L->top += 2;

        // 调用终结器
        luaD_call(L, L->top - 2, 0);

        // 恢复原始状态
        L->allowhook = oldah;    // 恢复调试钩子
        g->GCthreshold = oldt;   // 恢复GC阈值
    }
}


/**
 * @brief 调用所有垃圾回收终结器
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 这个函数执行所有待处理的垃圾回收终结器。它会持续调用GCTM
 * 直到tmudata列表为空，确保所有需要终结的用户数据都得到
 * 适当的清理。
 *
 * 执行策略：
 * - 循环调用GCTM，每次处理一个用户数据
 * - 直到tmudata列表完全为空
 * - 确保所有终结器都被执行
 *
 * 使用场景：
 * - Lua状态机关闭时的最终清理
 * - 强制执行所有待处理的终结器
 * - 确保资源的完整释放
 *
 * @note 这是终结器执行的批量接口
 * @see GCTM()
 */
void luaC_callGCTM(lua_State *L) {
    while (G(L)->tmudata) {
        GCTM(L);
    }
}

/**
 * @brief 释放所有可回收对象
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 这个函数在Lua状态机关闭时被调用，负责释放所有可回收对象。
 * 它通过设置特殊的白色标记来强制回收所有对象，包括通常
 * 不会被回收的固定对象。
 *
 * 释放策略：
 * 1. 设置特殊的白色标记，使所有对象都被视为垃圾
 * 2. 清扫主对象列表，释放所有对象
 * 3. 清扫字符串哈希表的所有桶，释放所有字符串
 *
 * 特殊标记：
 * 使用WHITEBITS | SFIXEDBIT作为死亡标记，这样即使是
 * 固定对象也会被回收。这是安全的，因为状态机即将被销毁。
 *
 * 完整清理：
 * - 主对象列表：包含大部分Lua对象
 * - 字符串表：包含所有内部化的字符串
 *
 * 安全考虑：
 * 这个函数只应该在状态机关闭时调用，因为它会破坏
 * 所有对象的完整性。
 *
 * @pre L即将被销毁，不再需要保持对象的完整性
 * @post 所有可回收对象被释放
 *
 * @note 这是Lua状态机销毁过程的一部分
 * @see sweepwholelist()
 */
void luaC_freeall(lua_State *L) {
    global_State *g = G(L);
    int i;

    // 设置特殊标记，使所有对象都被视为垃圾
    g->currentwhite = WHITEBITS | bitmask(SFIXEDBIT);

    // 清扫主对象列表
    sweepwholelist(L, &g->rootgc);

    // 清扫字符串表的所有桶
    for (i = 0; i < g->strt.size; i++) {
        sweepwholelist(L, &g->strt.hash[i]);
    }
}

/**
 * @brief 标记所有基本类型的元表
 * @param g 全局状态指针
 *
 * 详细说明：
 * Lua允许为基本类型（如数字、字符串等）设置元表。这个函数
 * 负责标记所有这些元表，确保它们在垃圾回收中不被错误回收。
 *
 * 基本类型元表：
 * - 数字元表
 * - 字符串元表
 * - 布尔值元表
 * - 等等
 *
 * 重要性：
 * 基本类型的元表是全局共享的，必须在垃圾回收的根集中
 * 被标记，否则可能导致元方法失效。
 *
 * @note 这是垃圾回收根集标记的一部分
 * @see markobject()
 */
static void markmt(global_State *g) {
    int i;
    for (i = 0; i < NUM_TAGS; i++) {
        if (g->mt[i]) {
            markobject(g, g->mt[i]);
        }
    }
}

/**
 * @brief 标记垃圾回收的根集
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 这个函数标记垃圾回收的根集，即那些总是可达的对象。
 * 根集是垃圾回收算法的起点，从这些对象开始进行可达性分析。
 *
 * 根集包含：
 * 1. 主线程：Lua状态机的主执行线程
 * 2. 全局表：主线程的全局环境
 * 3. 注册表：Lua的全局注册表
 * 4. 基本类型元表：所有基本类型的元表
 *
 * 初始化工作：
 * - 清空灰色列表：准备新的标记周期
 * - 清空grayagain列表：准备处理写屏障
 * - 清空弱引用列表：准备收集弱引用表
 *
 * 标记顺序：
 * 全局表在主栈之前被遍历，这确保了全局变量的优先处理。
 *
 * 状态转换：
 * 完成根集标记后，垃圾回收器进入传播阶段（GCSpropagate）。
 *
 * @pre 垃圾回收器处于标记准备状态
 * @post 根集被标记，垃圾回收器进入传播阶段
 *
 * @note 这是垃圾回收标记阶段的起点
 * @see markobject(), markvalue(), markmt()
 */
static void markroot(lua_State *L) {
    global_State *g = G(L);

    // 初始化标记列表
    g->gray = NULL;
    g->grayagain = NULL;
    g->weak = NULL;

    // 标记主线程
    markobject(g, g->mainthread);

    // 标记全局表（在主栈之前遍历）
    markvalue(g, gt(g->mainthread));

    // 标记注册表
    markvalue(g, registry(L));

    // 标记基本类型元表
    markmt(g);

    // 转换到传播阶段
    g->gcstate = GCSpropagate;
}


/**
 * @brief 重新标记上值对象
 * @param g 全局状态指针
 *
 * 详细说明：
 * 这个函数在垃圾回收的原子阶段被调用，负责重新标记所有灰色的
 * 上值对象。上值是Lua闭包机制的核心，可能在垃圾回收过程中
 * 由于写屏障而变为灰色，需要重新处理。
 *
 * 上值链表结构：
 * 所有上值通过双向循环链表连接，uvhead作为哨兵节点。
 * 这种结构便于快速遍历和插入/删除操作。
 *
 * 重新标记的必要性：
 * 在增量垃圾回收过程中，上值可能因为以下原因变为灰色：
 * - 写屏障触发：当黑色对象引用白色对象时
 * - 线程栈变化：开放上值指向的栈位置发生变化
 * - 上值关闭：开放上值变为闭合上值
 *
 * 处理策略：
 * 只处理灰色的上值，对其引用的值进行标记。这确保了
 * 上值引用的对象不会被错误回收。
 *
 * 双向链表完整性：
 * 使用断言检查链表的双向连接完整性，确保数据结构的正确性。
 *
 * 性能考虑：
 * 遍历所有上值的成本相对较低，因为上值的数量通常不会很大。
 *
 * @pre g->uvhead必须是有效的上值链表头
 * @post 所有灰色上值引用的对象被重新标记
 *
 * @note 这是原子阶段的重要组成部分
 * @see markvalue(), atomic()
 */
static void remarkupvals(global_State *g) {
    UpVal *uv;

    // 遍历上值双向循环链表
    for (uv = g->uvhead.u.l.next; uv != &g->uvhead; uv = uv->u.l.next) {
        // 检查双向链表的完整性
        lua_assert(uv->u.l.next->u.l.prev == uv &&
                   uv->u.l.prev->u.l.next == uv);

        // 如果上值是灰色的，重新标记其引用的值
        if (isgray(obj2gco(uv))) {
            markvalue(g, uv->v);
        }
    }
}


/**
 * @brief 垃圾回收的原子阶段
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 原子阶段是垃圾回收标记阶段的最后步骤，必须一次性完成，
 * 不能被中断。这个阶段确保所有可达对象都被正确标记，
 * 并为清扫阶段做好准备。
 *
 * 原子阶段的必要性：
 * 在增量垃圾回收中，标记过程可能被程序执行中断，导致
 * 写屏障触发和新的灰色对象产生。原子阶段确保所有这些
 * 对象都被处理完毕。
 *
 * 执行步骤：
 * 1. 重新标记上值：处理可能死亡线程的上值
 * 2. 传播写屏障对象：处理增量过程中产生的灰色对象
 * 3. 处理弱引用表：重新标记弱引用表中的对象
 * 4. 标记当前线程：确保正在运行的线程不被回收
 * 5. 重新标记元表：确保基本类型元表的可达性
 * 6. 处理grayagain列表：处理写屏障产生的对象
 * 7. 分离用户数据：将需要终结的用户数据分离出来
 * 8. 标记保留对象：标记需要保留的用户数据
 * 9. 清理弱引用表：移除已回收对象的引用
 * 10. 准备清扫阶段：设置清扫状态和参数
 *
 * 白色翻转：
 * 改变当前白色的定义，使得当前周期的存活对象变为新的白色，
 * 而未标记的对象变为死亡白色，准备被清扫。
 *
 * 内存估算：
 * 计算需要终结的用户数据大小，用于垃圾回收器的内存估算。
 * 这有助于调整后续的回收策略。
 *
 * 状态转换：
 * 完成后转换到字符串清扫状态（GCSsweepstring），开始清扫阶段。
 *
 * 性能特征：
 * 原子阶段的执行时间相对较短，但必须一次性完成，
 * 可能造成短暂的停顿。
 *
 * @pre 垃圾回收器处于传播阶段且灰色列表为空
 * @post 所有可达对象被标记，垃圾回收器进入清扫阶段
 *
 * @note 这是增量垃圾回收的关键同步点
 * @see remarkupvals(), propagateall(), cleartable()
 */
static void atomic(lua_State *L) {
    global_State *g = G(L);
    size_t udsize;    // 需要终结的用户数据总大小

    // 步骤1：重新标记可能死亡线程的上值
    remarkupvals(g);

    // 步骤2：传播写屏障和remarkupvals产生的对象
    propagateall(g);

    // 步骤3：重新标记弱引用表
    g->gray = g->weak;
    g->weak = NULL;
    lua_assert(!iswhite(obj2gco(g->mainthread)));
    markobject(g, L);    // 标记当前运行的线程
    markmt(g);           // 重新标记基本类型元表
    propagateall(g);

    // 步骤4：处理grayagain列表（写屏障产生的对象）
    g->gray = g->grayagain;
    g->grayagain = NULL;
    propagateall(g);

    // 步骤5：分离需要终结的用户数据
    udsize = luaC_separateudata(L, 0);
    marktmu(g);          // 标记保留的用户数据
    udsize += propagateall(g);    // 传播保留性

    // 步骤6：清理弱引用表中的已回收对象
    cleartable(g->weak);

    // 步骤7：翻转当前白色，准备清扫阶段
    g->currentwhite = cast_byte(otherwhite(g));
    g->sweepstrgc = 0;
    g->sweepgc = &g->rootgc;
    g->gcstate = GCSsweepstring;

    // 步骤8：设置内存估算（减去需要终结的用户数据）
    g->estimate = g->totalbytes - udsize;
}


/**
 * @brief 执行垃圾回收的单个步骤
 * @param L Lua状态机指针
 * @return 本步骤的工作量估算
 *
 * 详细说明：
 * 这是增量垃圾回收的核心函数，根据当前的垃圾回收状态
 * 执行相应的操作。增量回收将垃圾回收工作分散到程序
 * 执行过程中，避免长时间的停顿。
 *
 * 垃圾回收状态机：
 * 1. GCSpause：暂停状态，开始新的回收周期
 * 2. GCSpropagate：传播状态，处理灰色对象
 * 3. GCSsweepstring：字符串清扫状态
 * 4. GCSsweep：对象清扫状态
 * 5. GCSfinalize：终结状态，执行终结器
 *
 * 各状态的处理：
 *
 * GCSpause（暂停状态）：
 * - 标记根集，开始新的回收周期
 * - 转换到传播状态
 * - 工作量为0（标记根集很快）
 *
 * GCSpropagate（传播状态）：
 * - 如果有灰色对象，处理一个灰色对象
 * - 如果没有灰色对象，执行原子阶段
 * - 工作量为处理对象的大小
 *
 * GCSsweepstring（字符串清扫）：
 * - 清扫字符串表的一个桶
 * - 更新内存估算
 * - 完成后转到对象清扫状态
 *
 * GCSsweep（对象清扫）：
 * - 清扫有限数量的对象
 * - 更新内存估算
 * - 完成后检查大小并转到终结状态
 *
 * GCSfinalize（终结状态）：
 * - 如果有待终结的用户数据，执行一个终结器
 * - 如果没有，结束回收周期，转到暂停状态
 *
 * 工作量估算：
 * 返回值表示本步骤的工作量，用于控制增量回收的节奏。
 * 不同操作有不同的成本估算。
 *
 * 内存估算维护：
 * 在清扫阶段，根据实际释放的内存更新估算值，
 * 用于后续的回收调度。
 *
 * 性能特征：
 * 每个步骤的执行时间相对较短且可预测，
 * 支持真正的增量回收。
 *
 * @pre L必须是有效的Lua状态机
 * @post 垃圾回收状态可能发生转换，部分工作被完成
 *
 * @note 这是增量垃圾回收的核心实现
 * @see markroot(), propagatemark(), atomic(), sweeplist(), GCTM()
 */
static l_mem singlestep(lua_State *L) {
    global_State *g = G(L);
    /*lua_checkmemory(L);*/    // 可选的内存检查（调试用）

    switch (g->gcstate) {
        case GCSpause: {
            // 暂停状态：开始新的回收周期
            markroot(L);
            return 0;
        }

        case GCSpropagate: {
            // 传播状态：处理灰色对象
            if (g->gray) {
                return propagatemark(g);
            } else {
                // 没有更多灰色对象，完成标记阶段
                atomic(L);
                return 0;
            }
        }

        case GCSsweepstring: {
            // 字符串清扫状态：清扫字符串表
            lu_mem old = g->totalbytes;
            sweepwholelist(L, &g->strt.hash[g->sweepstrgc++]);

            if (g->sweepstrgc >= g->strt.size) {
                // 字符串清扫完成，转到对象清扫
                g->gcstate = GCSsweep;
            }

            lua_assert(old >= g->totalbytes);
            g->estimate -= old - g->totalbytes;
            return GCSWEEPCOST;
        }

        case GCSsweep: {
            // 对象清扫状态：清扫主对象列表
            lu_mem old = g->totalbytes;
            g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX);

            if (*g->sweepgc == NULL) {
                // 对象清扫完成，检查大小并转到终结状态
                checkSizes(L);
                g->gcstate = GCSfinalize;
            }

            lua_assert(old >= g->totalbytes);
            g->estimate -= old - g->totalbytes;
            return GCSWEEPMAX * GCSWEEPCOST;
        }

        case GCSfinalize: {
            // 终结状态：执行终结器
            if (g->tmudata) {
                GCTM(L);
                if (g->estimate > GCFINALIZECOST) {
                    g->estimate -= GCFINALIZECOST;
                }
                return GCFINALIZECOST;
            } else {
                // 终结完成，结束回收周期
                g->gcstate = GCSpause;
                g->gcdept = 0;
                return 0;
            }
        }

        default:
            lua_assert(0);    // 不应该到达这里
            return 0;
    }
}


/**
 * @brief 执行增量垃圾回收步进
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 这是Lua增量垃圾回收的主要接口函数，在内存分配时被调用。
 * 它根据内存分配的情况和用户设置的参数，执行适量的垃圾
 * 回收工作，实现内存分配与垃圾回收的平衡。
 *
 * 增量回收原理：
 * 将垃圾回收工作分散到程序执行过程中，每次内存分配时
 * 执行少量的回收工作，避免集中回收造成的长时间停顿。
 *
 * 工作量计算：
 * - 基础工作量：GCSTEPSIZE / 100 * gcstepmul
 * - gcstepmul是用户可调的步进倍数
 * - 如果计算结果为0，则设置为最大值（无限制）
 *
 * 债务机制：
 * - gcdept记录了"垃圾回收债务"
 * - 当内存使用超过阈值时，债务增加
 * - 通过执行回收工作来偿还债务
 *
 * 执行策略：
 * 1. 计算本次的工作量限制
 * 2. 累积内存分配债务
 * 3. 循环执行单步回收，直到工作量用完或回收周期结束
 * 4. 根据剩余债务调整下次触发的阈值
 *
 * 阈值调整：
 * - 如果回收未完成且债务较少：提高阈值，延迟下次回收
 * - 如果回收未完成且债务较多：降低阈值，加快回收节奏
 * - 如果回收完成：重新设置标准阈值
 *
 * 自适应特性：
 * 根据内存分配的速度和垃圾产生的速度，动态调整
 * 回收的频率和强度，实现最佳的性能平衡。
 *
 * 性能考虑：
 * - 每次调用的执行时间相对固定
 * - 避免了传统stop-the-world回收的长时间停顿
 * - 总体回收效率可能略低于集中回收
 *
 * 用户控制：
 * 通过gcstepmul参数，用户可以控制回收的积极程度：
 * - 较大的值：更积极的回收，更少的内存使用
 * - 较小的值：更少的回收开销，更多的内存使用
 *
 * @pre L必须是有效的Lua状态机
 * @post 执行了适量的垃圾回收工作，阈值被适当调整
 *
 * @note 这是增量垃圾回收的用户接口
 * @see singlestep(), setthreshold()
 */
void luaC_step(lua_State *L) {
    global_State *g = G(L);

    // 计算本次步进的工作量限制
    l_mem lim = (GCSTEPSIZE / 100) * g->gcstepmul;
    if (lim == 0) {
        lim = (MAX_LUMEM - 1) / 2;    // 无限制
    }

    // 累积垃圾回收债务
    g->gcdept += g->totalbytes - g->GCthreshold;

    // 执行垃圾回收步骤，直到工作量用完或回收周期结束
    do {
        lim -= singlestep(L);
        if (g->gcstate == GCSpause) {
            break;    // 回收周期结束
        }
    } while (lim > 0);

    // 根据回收状态和债务情况调整阈值
    if (g->gcstate != GCSpause) {
        // 回收未完成，根据债务情况调整阈值
        if (g->gcdept < GCSTEPSIZE) {
            // 债务较少，提高阈值，延迟下次回收
            g->GCthreshold = g->totalbytes + GCSTEPSIZE;
        } else {
            // 债务较多，降低阈值，加快回收节奏
            g->gcdept -= GCSTEPSIZE;
            g->GCthreshold = g->totalbytes;
        }
    } else {
        // 回收周期完成，重新设置标准阈值
        setthreshold(g);
    }
}


/**
 * @brief 执行完整的垃圾回收周期
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 这个函数强制执行一个完整的垃圾回收周期，不管当前的
 * 回收状态如何。它主要用于用户显式调用collectgarbage()
 * 或在某些特殊情况下需要立即回收内存。
 *
 * 执行策略：
 * 1. 如果当前处于标记阶段，跳过到清扫阶段
 * 2. 完成任何待处理的清扫工作
 * 3. 开始新的完整回收周期
 * 4. 执行到回收周期完全结束
 *
 * 状态处理：
 * - 如果在暂停或传播状态：重置到清扫状态，跳过标记
 * - 如果在清扫状态：继续完成清扫
 * - 如果在终结状态：完成终结后开始新周期
 *
 * 跳过标记的原理：
 * 如果当前处于标记阶段，说明已经有部分对象被标记，
 * 直接进入清扫可以回收未标记的对象，然后开始新的
 * 完整周期。
 *
 * 两阶段执行：
 * 1. 第一阶段：完成当前周期的清扫和终结
 * 2. 第二阶段：执行完整的新周期（标记→清扫→终结）
 *
 * 与增量回收的区别：
 * - 增量回收：分散执行，避免停顿
 * - 完整回收：集中执行，彻底清理
 *
 * 使用场景：
 * - 用户显式调用collectgarbage("collect")
 * - 内存紧张时的强制回收
 * - 程序关键点的内存清理
 * - 性能测试和调试
 *
 * 性能特征：
 * - 执行时间较长，可能造成明显停顿
 * - 回收效果最彻底，内存使用最少
 * - 适合在程序空闲时或关键点调用
 *
 * 安全性：
 * 函数确保回收周期的完整性，不会留下不一致的状态。
 *
 * @pre L必须是有效的Lua状态机
 * @post 完成一个完整的垃圾回收周期，内存得到彻底清理
 *
 * @note 这是垃圾回收的强制完整接口
 * @see singlestep(), markroot(), setthreshold()
 */
void luaC_fullgc(lua_State *L) {
    global_State *g = G(L);

    // 如果当前在标记阶段，跳过到清扫阶段
    if (g->gcstate <= GCSpropagate) {
        // 重置清扫标记，准备清扫所有元素
        g->sweepstrgc = 0;
        g->sweepgc = &g->rootgc;

        // 重置收集器列表
        g->gray = NULL;
        g->grayagain = NULL;
        g->weak = NULL;
        g->gcstate = GCSsweepstring;
    }

    lua_assert(g->gcstate != GCSpause && g->gcstate != GCSpropagate);

    // 第一阶段：完成任何待处理的清扫工作
    while (g->gcstate != GCSfinalize) {
        lua_assert(g->gcstate == GCSsweepstring || g->gcstate == GCSsweep);
        singlestep(L);
    }

    // 第二阶段：开始新的完整回收周期
    markroot(L);
    while (g->gcstate != GCSpause) {
        singlestep(L);
    }

    // 重新设置垃圾回收阈值
    setthreshold(g);
}


/**
 * @brief 前向写屏障（Forward Write Barrier）
 * @param L Lua状态机指针
 * @param o 黑色对象（引用者）
 * @param v 白色对象（被引用者）
 *
 * 详细说明：
 * 写屏障是增量垃圾回收的核心机制，用于维护三色不变式：
 * "黑色对象不能直接引用白色对象"。当程序试图让黑色对象
 * 引用白色对象时，写屏障被触发。
 *
 * 三色不变式的重要性：
 * 在增量垃圾回收中，如果黑色对象引用白色对象，而白色对象
 * 没有其他引用路径，那么白色对象可能被错误回收。
 *
 * 前向屏障策略：
 * 将被引用的白色对象标记为灰色或黑色，确保其不被回收。
 * 这种策略适用于大多数对象类型。
 *
 * 处理策略：
 * - 传播阶段：立即标记白色对象，恢复不变式
 * - 其他阶段：将黑色对象标记为白色，避免后续屏障开销
 *
 * 适用对象：
 * 除表对象外的所有对象类型。表对象使用后向屏障，
 * 因为表的修改模式更适合后向策略。
 *
 * 性能考虑：
 * 前向屏障的开销相对较小，但在大量赋值操作时
 * 可能产生累积影响。
 *
 * @pre o必须是黑色且活跃，v必须是白色且活跃
 * @post 三色不变式得到维护
 *
 * @note 这是增量垃圾回收正确性的关键保证
 * @see luaC_barrierback(), reallymarkobject()
 */
void luaC_barrierf(lua_State *L, GCObject *o, GCObject *v) {
    global_State *g = G(L);
    lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
    lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
    lua_assert(ttype(&o->gch) != LUA_TTABLE);

    // 根据垃圾回收状态选择策略
    if (g->gcstate == GCSpropagate) {
        // 传播阶段：标记白色对象，恢复不变式
        reallymarkobject(g, v);
    } else {
        // 其他阶段：将黑色对象标记为白色，避免后续屏障
        makewhite(g, o);
    }
}

/**
 * @brief 后向写屏障（Backward Write Barrier）
 * @param L Lua状态机指针
 * @param t 表对象
 *
 * 详细说明：
 * 后向写屏障专门用于表对象。当表被修改时，将表重新标记
 * 为灰色，加入grayagain列表，在原子阶段重新处理。
 *
 * 后向屏障的优势：
 * 对于频繁修改的表，后向屏障比前向屏障更高效，
 * 因为它避免了对每个新值的单独处理。
 *
 * 处理策略：
 * 1. 将黑色表重新标记为灰色
 * 2. 加入grayagain列表
 * 3. 在原子阶段重新遍历表的所有内容
 *
 * 适用场景：
 * - 表的键值对修改
 * - 表的元表设置
 * - 表的大量批量操作
 *
 * 与前向屏障的区别：
 * - 前向屏障：标记被引用对象
 * - 后向屏障：重新标记引用者对象
 *
 * @pre t必须是黑色且活跃的表对象
 * @post 表被加入grayagain列表，等待重新处理
 *
 * @note 表对象的专用写屏障机制
 * @see luaC_barrierf(), black2gray()
 */
void luaC_barrierback(lua_State *L, Table *t) {
    global_State *g = G(L);
    GCObject *o = obj2gco(t);
    lua_assert(isblack(o) && !isdead(g, o));
    lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);

    // 将表重新标记为灰色
    black2gray(o);

    // 加入grayagain列表，等待原子阶段重新处理
    t->gclist = g->grayagain;
    g->grayagain = o;
}

/**
 * @brief 将新创建的对象链接到垃圾回收器
 * @param L Lua状态机指针
 * @param o 新创建的对象
 * @param tt 对象类型
 *
 * 详细说明：
 * 当创建新的可回收对象时，必须将其链接到垃圾回收器的
 * 主对象列表中，以便垃圾回收器能够管理其生命周期。
 *
 * 链接操作：
 * 1. 将对象插入到rootgc列表的头部
 * 2. 设置对象的标记为当前白色
 * 3. 设置对象的类型标记
 *
 * 新对象的初始状态：
 * 新对象总是标记为白色，表示它是当前回收周期的候选对象。
 * 如果对象在创建后立即被引用，相关的写屏障会确保其正确性。
 *
 * 链表插入：
 * 使用头插法，新对象成为链表的第一个元素，这样可以
 * 保证O(1)的插入时间复杂度。
 *
 * @pre o必须是新创建的有效对象，tt必须是有效的类型标记
 * @post 对象被链接到垃圾回收器，可以参与垃圾回收
 *
 * @note 所有可回收对象创建时都必须调用此函数
 * @see luaC_white()
 */
void luaC_link(lua_State *L, GCObject *o, lu_byte tt) {
    global_State *g = G(L);

    // 将对象插入到主对象列表的头部
    o->gch.next = g->rootgc;
    g->rootgc = o;

    // 设置对象的初始标记和类型
    o->gch.marked = luaC_white(g);
    o->gch.tt = tt;
}

/**
 * @brief 将上值对象链接到垃圾回收器
 * @param L Lua状态机指针
 * @param uv 上值对象
 *
 * 详细说明：
 * 上值对象的链接需要特殊处理，因为上值可能在创建时
 * 就是灰色的（如果它引用了已标记的值）。
 *
 * 特殊处理：
 * 如果上值在链接时是灰色的，需要根据当前的垃圾回收
 * 状态进行适当的处理：
 * - 传播阶段：转为黑色并应用写屏障
 * - 清扫阶段：转为白色
 *
 * 闭合上值的写屏障：
 * 闭合的上值需要写屏障保护，因为它们包含独立的值引用。
 *
 * @pre uv必须是有效的上值对象
 * @post 上值被正确链接，其颜色状态符合当前GC阶段
 *
 * @note 上值对象的专用链接函数
 * @see luaC_link(), luaC_barrier()
 */
void luaC_linkupval(lua_State *L, UpVal *uv) {
    global_State *g = G(L);
    GCObject *o = obj2gco(uv);

    // 将上值链接到主对象列表
    o->gch.next = g->rootgc;
    g->rootgc = o;

    // 如果上值是灰色的，需要特殊处理
    if (isgray(o)) {
        if (g->gcstate == GCSpropagate) {
            // 传播阶段：转为黑色，闭合上值需要屏障
            gray2black(o);
            luaC_barrier(L, uv, uv->v);
        } else {
            // 清扫阶段：转为白色
            makewhite(g, o);
            lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
        }
    }
}

