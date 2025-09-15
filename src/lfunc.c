/**
 * @file lfunc.c
 * @brief Lua函数系统实现：闭包、上值和原型的完整管理
 *
 * 详细说明：
 * 本文件实现了Lua语言的函数系统，包括闭包（Closure）、上值（UpValue）
 * 和函数原型（Prototype）的创建、管理和销毁。这是Lua实现函数式编程
 * 特性的核心模块。
 *
 * 核心概念：
 * 1. 闭包（Closure）：函数与其环境的组合，支持C函数和Lua函数两种类型
 * 2. 上值（UpValue）：闭包捕获的外部变量，实现词法作用域
 * 3. 函数原型（Prototype）：Lua函数的编译结果，包含字节码和元信息
 *
 * 设计特色：
 * - 统一抽象：C函数和Lua函数使用统一的闭包接口
 * - 上值共享：多个闭包可以共享同一个上值，实现变量共享
 * - 生命周期管理：上值的开放/关闭状态转换，优化内存使用
 * - 垃圾回收集成：所有对象完全集成到GC系统中
 *
 * 上值生命周期：
 * 1. 开放状态：上值指向栈上的变量
 * 2. 关闭状态：上值拥有变量的独立拷贝
 * 3. 共享机制：多个闭包可以共享同一个上值
 * 4. 垃圾回收：不再被引用的上值会被自动回收
 *
 * 技术亮点：
 * - 双向链表管理：高效的上值链表操作
 * - 栈变量捕获：将栈变量转换为堆变量
 * - 内存优化：按需分配，精确释放
 * - 调试支持：完整的局部变量信息
 *
 * 应用场景：
 * - 函数式编程：支持高阶函数和闭包
 * - 模块系统：实现模块的私有状态
 * - 回调机制：C函数与Lua函数的统一处理
 * - 协程实现：函数状态的保存和恢复
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2007-12-28
 * @since Lua 5.0
 * @see lobject.h, lgc.h, lstate.h
 */

#include <stddef.h>

#define lfunc_c
#define LUA_CORE

#include "lua.h"

#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"

/**
 * @brief 创建C函数闭包
 * @param L Lua状态机指针
 * @param nelems 上值数量
 * @param e 环境表
 * @return 新创建的C函数闭包指针
 *
 * 详细说明：
 * 这个函数创建一个C函数闭包，用于将C函数包装成Lua可调用的对象。
 * C函数闭包是Lua与C语言交互的重要机制。
 *
 * C函数闭包特点：
 * 1. 封装C函数：将C函数指针包装成Lua函数对象
 * 2. 上值支持：可以携带任意数量的上值作为闭包数据
 * 3. 环境表：拥有独立的环境表，控制全局变量访问
 * 4. 垃圾回收：完全集成到Lua的垃圾回收系统
 *
 * 内存分配：
 * 根据上值数量动态计算所需内存大小，确保内存使用的精确性。
 *
 * 初始化过程：
 * - isC标志：标记为C函数闭包
 * - env：设置环境表
 * - nupvalues：记录上值数量
 *
 * 使用场景：
 * - 注册C函数到Lua
 * - 创建带状态的C函数
 * - 实现Lua标准库函数
 * - C扩展模块的函数导出
 *
 * @pre L必须是有效的Lua状态机，nelems必须非负，e可以为NULL
 * @post 返回完全初始化的C函数闭包
 *
 * @note C函数闭包的上值需要后续手动设置
 * @see luaF_newLclosure(), sizeCclosure()
 */
Closure *luaF_newCclosure(lua_State *L, int nelems, Table *e) {
    Closure *c = cast(Closure *, luaM_malloc(L, sizeCclosure(nelems)));
    luaC_link(L, obj2gco(c), LUA_TFUNCTION);
    c->c.isC = 1;                           // 标记为C函数闭包
    c->c.env = e;                           // 设置环境表
    c->c.nupvalues = cast_byte(nelems);     // 记录上值数量
    return c;
}

/**
 * @brief 创建Lua函数闭包
 * @param L Lua状态机指针
 * @param nelems 上值数量
 * @param e 环境表
 * @return 新创建的Lua函数闭包指针
 *
 * 详细说明：
 * 这个函数创建一个Lua函数闭包，用于表示编译后的Lua函数。
 * Lua函数闭包是Lua函数式编程的核心实现。
 *
 * Lua函数闭包特点：
 * 1. 字节码执行：包含编译后的Lua字节码
 * 2. 词法作用域：通过上值实现词法作用域的变量捕获
 * 3. 动态创建：在运行时根据需要创建
 * 4. 共享原型：多个闭包可以共享同一个函数原型
 *
 * 上值初始化：
 * 所有上值槽位初始化为NULL，需要在后续过程中设置具体的上值对象。
 *
 * 内存管理：
 * 根据上值数量动态分配内存，实现内存使用的最优化。
 *
 * 初始化过程：
 * - isC标志：标记为Lua函数闭包（0）
 * - env：设置环境表
 * - nupvalues：记录上值数量
 * - upvals：初始化上值数组为NULL
 *
 * 使用场景：
 * - 函数定义：每个function关键字都会创建闭包
 * - 闭包创建：捕获外部变量的函数
 * - 高阶函数：作为参数或返回值的函数
 * - 模块系统：模块的私有函数
 *
 * @pre L必须是有效的Lua状态机，nelems必须非负，e可以为NULL
 * @post 返回完全初始化的Lua函数闭包，上值数组已清零
 *
 * @note 闭包的函数原型需要后续设置
 * @see luaF_newCclosure(), sizeLclosure()
 */
Closure *luaF_newLclosure(lua_State *L, int nelems, Table *e) {
    Closure *c = cast(Closure *, luaM_malloc(L, sizeLclosure(nelems)));
    luaC_link(L, obj2gco(c), LUA_TFUNCTION);
    c->l.isC = 0;                           // 标记为Lua函数闭包
    c->l.env = e;                           // 设置环境表
    c->l.nupvalues = cast_byte(nelems);     // 记录上值数量

    // 初始化所有上值槽位为NULL
    while (nelems--) {
        c->l.upvals[nelems] = NULL;
    }

    return c;
}

/**
 * @brief 创建新的上值对象
 * @param L Lua状态机指针
 * @return 新创建的上值对象指针
 *
 * 详细说明：
 * 这个函数创建一个新的上值对象，用于实现闭包的变量捕获机制。
 * 上值是Lua实现词法作用域的关键技术。
 *
 * 上值对象特点：
 * 1. 变量引用：可以引用栈上或堆上的变量
 * 2. 共享机制：多个闭包可以共享同一个上值
 * 3. 生命周期：从开放状态到关闭状态的转换
 * 4. 垃圾回收：完全集成到GC系统中
 *
 * 初始状态：
 * 新创建的上值处于"关闭"状态，即拥有自己的值拷贝。
 * 值初始化为nil，等待后续设置。
 *
 * 内存布局：
 * 上值对象包含值存储空间和链表指针，支持高效的链表操作。
 *
 * 使用场景：
 * - 闭包创建：为闭包提供上值存储
 * - 变量捕获：将外部变量转换为上值
 * - 状态保持：在函数调用间保持状态
 *
 * @pre L必须是有效的Lua状态机
 * @post 返回初始化为nil的关闭状态上值
 *
 * @note 新上值默认为关闭状态，需要通过其他函数转换为开放状态
 * @see luaF_findupval(), luaF_close()
 */
UpVal *luaF_newupval(lua_State *L) {
    UpVal *uv = luaM_new(L, UpVal);
    luaC_link(L, obj2gco(uv), LUA_TUPVAL);
    uv->v = &uv->u.value;                   // 指向自己的值存储
    setnilvalue(uv->v);                     // 初始化为nil
    return uv;
}


/**
 * @brief 查找或创建指向栈位置的上值
 * @param L Lua状态机指针
 * @param level 栈位置指针
 * @return 指向该栈位置的上值对象
 *
 * 详细说明：
 * 这是上值管理的核心函数，实现了上值的共享机制。多个闭包可以
 * 共享指向同一栈位置的上值，确保变量语义的正确性。
 *
 * 上值共享原理：
 * 当多个闭包需要捕获同一个外部变量时，它们应该共享同一个上值对象，
 * 这样对变量的修改能够在所有闭包间同步。
 *
 * 查找策略：
 * 1. 遍历当前线程的开放上值链表
 * 2. 链表按栈位置降序排列，便于快速查找
 * 3. 如果找到匹配的栈位置，返回现有上值
 * 4. 如果没找到，创建新的上值并插入链表
 *
 * 垃圾回收处理：
 * 如果找到的上值在垃圾回收中被标记为"死"，需要重新激活它，
 * 因为现在又有新的引用了。
 *
 * 双向链表管理：
 * 新创建的上值会被插入到两个链表中：
 * 1. 线程的开放上值链表（单向，按栈位置排序）
 * 2. 全局的上值链表（双向，用于垃圾回收遍历）
 *
 * 开放状态特征：
 * - v指针指向栈上的位置
 * - 在线程的openupval链表中
 * - 在全局的uvhead双向链表中
 *
 * 性能优化：
 * - 链表有序：按栈位置降序，提高查找效率
 * - 早期退出：一旦栈位置小于目标，立即停止查找
 * - 插入位置：在正确位置插入，保持链表有序
 *
 * 内存安全：
 * 使用断言确保双向链表的完整性，防止链表损坏。
 *
 * @pre L必须是有效的Lua状态机，level必须是有效的栈位置
 * @post 返回指向该栈位置的上值，确保上值共享的正确性
 *
 * @note 这是实现闭包变量共享的关键函数
 * @see luaF_close(), unlinkupval()
 */
UpVal *luaF_findupval(lua_State *L, StkId level) {
    global_State *g = G(L);
    GCObject **pp = &L->openupval;
    UpVal *p;
    UpVal *uv;

    // 在开放上值链表中查找（链表按栈位置降序排列）
    while (*pp != NULL && (p = ngcotouv(*pp))->v >= level) {
        lua_assert(p->v != &p->u.value);   // 确保是开放状态
        if (p->v == level) {
            // 找到匹配的上值
            if (isdead(g, obj2gco(p))) {
                changewhite(obj2gco(p));    // 重新激活"死"上值
            }
            return p;
        }
        pp = &p->next;
    }

    // 没找到，创建新的开放上值
    uv = luaM_new(L, UpVal);
    uv->tt = LUA_TUPVAL;
    uv->marked = luaC_white(g);
    uv->v = level;                          // 指向栈位置（开放状态）

    // 插入到线程的开放上值链表（保持有序）
    uv->next = *pp;
    *pp = obj2gco(uv);

    // 插入到全局的双向上值链表
    uv->u.l.prev = &g->uvhead;
    uv->u.l.next = g->uvhead.u.l.next;
    uv->u.l.next->u.l.prev = uv;
    g->uvhead.u.l.next = uv;
    lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);

    return uv;
}

/**
 * @brief 从全局双向链表中移除上值
 * @param uv 要移除的上值对象
 *
 * 详细说明：
 * 这是一个内部辅助函数，用于从全局的上值双向链表中安全地
 * 移除指定的上值对象。
 *
 * 双向链表操作：
 * 执行标准的双向链表节点删除操作：
 * 1. 将前驱节点的next指针指向后继节点
 * 2. 将后继节点的prev指针指向前驱节点
 * 3. 被删除节点从链表中完全脱离
 *
 * 安全性检查：
 * 使用断言确保链表的完整性，防止链表损坏导致的内存错误。
 *
 * 使用场景：
 * - 上值关闭：将开放上值转换为关闭上值时
 * - 上值释放：释放不再使用的上值时
 * - 垃圾回收：清理死亡上值时
 *
 * @pre uv必须在全局双向链表中，且链表结构完整
 * @post uv从全局双向链表中被移除，但对象本身未被释放
 *
 * @note 这是双向链表操作的标准实现
 * @see luaF_freeupval(), luaF_close()
 */
static void unlinkupval(UpVal *uv) {
    lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
    uv->u.l.next->u.l.prev = uv->u.l.prev;
    uv->u.l.prev->u.l.next = uv->u.l.next;
}

/**
 * @brief 释放上值对象
 * @param L Lua状态机指针
 * @param uv 要释放的上值对象
 *
 * 详细说明：
 * 这个函数负责完全释放一个上值对象，包括从链表中移除和
 * 释放内存。根据上值的状态执行不同的清理操作。
 *
 * 状态检查：
 * 通过检查v指针的值来判断上值的状态：
 * - 开放状态：v指向栈上的位置（v != &uv->u.value）
 * - 关闭状态：v指向自己的值存储（v == &uv->u.value）
 *
 * 清理策略：
 * - 开放上值：需要先从全局双向链表中移除
 * - 关闭上值：直接释放内存即可
 *
 * 内存管理：
 * 使用Lua的内存管理器释放上值对象占用的内存。
 *
 * 使用场景：
 * - 垃圾回收：回收不再被引用的上值
 * - 上值关闭：处理死亡的开放上值
 * - 错误处理：清理异常情况下的上值
 *
 * @pre L必须是有效的Lua状态机，uv必须是有效的上值对象
 * @post 上值对象被完全释放，相关链表结构被正确维护
 *
 * @note 调用此函数后uv指针变为无效
 * @see unlinkupval(), luaM_free()
 */
void luaF_freeupval(lua_State *L, UpVal *uv) {
    if (uv->v != &uv->u.value) {           // 检查是否为开放状态
        unlinkupval(uv);                    // 从全局双向链表中移除
    }
    luaM_free(L, uv);                      // 释放内存
}

/**
 * @brief 关闭指定栈层级及以上的所有开放上值
 * @param L Lua状态机指针
 * @param level 栈层级阈值
 *
 * 详细说明：
 * 这是上值生命周期管理的核心函数，负责将开放状态的上值转换为
 * 关闭状态。当函数返回或栈收缩时，需要关闭相应的上值。
 *
 * 上值关闭原理：
 * 当栈上的变量即将被销毁时，所有指向这些变量的上值必须转换为
 * 关闭状态，即将变量的值复制到上值的内部存储中。
 *
 * 处理策略：
 * 1. 遍历线程的开放上值链表
 * 2. 找到所有栈位置 >= level的上值
 * 3. 根据上值的生存状态进行不同处理
 *
 * 垃圾回收处理：
 * - 死亡上值：直接释放，因为没有引用者
 * - 活跃上值：转换为关闭状态，保持值的可访问性
 *
 * 关闭过程：
 * 1. 从线程的开放上值链表中移除
 * 2. 从全局的双向上值链表中移除
 * 3. 复制栈上的值到上值的内部存储
 * 4. 更新v指针指向内部存储
 * 5. 重新链接到垃圾回收器的根集合
 *
 * 链表维护：
 * 开放上值链表按栈位置降序排列，可以高效地处理批量关闭。
 *
 * 内存安全：
 * 使用断言确保上值的状态正确性，防止状态不一致。
 *
 * 性能优化：
 * - 有序链表：利用链表的有序性提前终止遍历
 * - 批量处理：一次调用处理多个上值
 * - 状态检查：避免重复处理已关闭的上值
 *
 * 使用场景：
 * - 函数返回：关闭函数作用域的上值
 * - 块结束：关闭块作用域的上值
 * - 异常处理：确保异常时的上值一致性
 * - 协程切换：保存协程的上值状态
 *
 * @pre L必须是有效的Lua状态机，level必须是有效的栈位置
 * @post 所有栈位置>=level的开放上值被转换为关闭状态
 *
 * @note 这是实现词法作用域的关键函数
 * @see luaF_findupval(), unlinkupval(), luaC_linkupval()
 */
void luaF_close(lua_State *L, StkId level) {
    UpVal *uv;
    global_State *g = G(L);

    // 处理所有栈位置 >= level的开放上值
    while (L->openupval != NULL && (uv = ngcotouv(L->openupval))->v >= level) {
        GCObject *o = obj2gco(uv);
        lua_assert(!isblack(o) && uv->v != &uv->u.value);
        L->openupval = uv->next;            // 从开放链表中移除

        if (isdead(g, o)) {
            // 死亡上值：直接释放
            luaF_freeupval(L, uv);
        } else {
            // 活跃上值：转换为关闭状态
            unlinkupval(uv);                // 从全局双向链表中移除
            setobj(L, &uv->u.value, uv->v); // 复制栈上的值
            uv->v = &uv->u.value;           // 指向内部存储（关闭状态）
            luaC_linkupval(L, uv);          // 重新链接到GC根集合
        }
    }
}

/**
 * @brief 创建新的函数原型对象
 * @param L Lua状态机指针
 * @return 新创建的函数原型对象指针
 *
 * 详细说明：
 * 这个函数创建一个新的函数原型对象，用于存储Lua函数的编译结果。
 * 函数原型包含了执行Lua函数所需的所有信息。
 *
 * 函数原型内容：
 * 1. 字节码：编译后的Lua指令序列
 * 2. 常量表：函数中使用的常量值
 * 3. 子函数：嵌套函数的原型
 * 4. 调试信息：行号、局部变量、上值名称
 * 5. 元信息：参数数量、栈大小、变参标志
 *
 * 初始化状态：
 * 所有字段都被初始化为空或零值，等待编译器填充具体内容。
 * 这确保了原型对象的一致性和安全性。
 *
 * 内存管理：
 * 原型对象完全集成到垃圾回收系统中，当不再被引用时会自动回收。
 *
 * 字段说明：
 * - code/sizecode：字节码数组及其大小
 * - k/sizek：常量数组及其大小
 * - p/sizep：子函数数组及其大小
 * - upvalues/sizeupvalues：上值名称数组及其大小
 * - lineinfo/sizelineinfo：行号信息数组及其大小
 * - locvars/sizelocvars：局部变量信息数组及其大小
 * - numparams：参数数量
 * - is_vararg：是否为变参函数
 * - maxstacksize：最大栈大小
 * - nups：上值数量
 * - linedefined/lastlinedefined：函数定义的行号范围
 * - source：源文件名
 *
 * 共享机制：
 * 多个闭包可以共享同一个函数原型，实现内存的高效利用。
 *
 * 使用场景：
 * - 函数编译：每个Lua函数都对应一个原型
 * - 代码生成：编译器的输出目标
 * - 调试支持：提供丰富的调试信息
 * - 序列化：支持函数的序列化和反序列化
 *
 * @pre L必须是有效的Lua状态机
 * @post 返回完全初始化的空函数原型
 *
 * @note 原型的具体内容需要编译器后续填充
 * @see luaF_freeproto(), Closure结构
 */
Proto *luaF_newproto(lua_State *L) {
    Proto *f = luaM_new(L, Proto);
    luaC_link(L, obj2gco(f), LUA_TPROTO);

    // 初始化字节码相关字段
    f->code = NULL;
    f->sizecode = 0;

    // 初始化常量表
    f->k = NULL;
    f->sizek = 0;

    // 初始化子函数表
    f->p = NULL;
    f->sizep = 0;

    // 初始化上值信息
    f->upvalues = NULL;
    f->sizeupvalues = 0;
    f->nups = 0;

    // 初始化调试信息
    f->lineinfo = NULL;
    f->sizelineinfo = 0;
    f->locvars = NULL;
    f->sizelocvars = 0;

    // 初始化函数元信息
    f->numparams = 0;               // 参数数量
    f->is_vararg = 0;               // 变参标志
    f->maxstacksize = 0;            // 最大栈大小

    // 初始化源码信息
    f->linedefined = 0;             // 定义开始行号
    f->lastlinedefined = 0;         // 定义结束行号
    f->source = NULL;               // 源文件名

    return f;
}


/**
 * @brief 释放函数原型对象
 * @param L Lua状态机指针
 * @param f 要释放的函数原型对象
 *
 * 详细说明：
 * 这个函数负责完全释放一个函数原型对象及其所有关联的数据结构。
 * 函数原型包含多个动态分配的数组，需要逐一释放。
 *
 * 释放顺序：
 * 1. 字节码数组：包含编译后的指令
 * 2. 子函数数组：嵌套函数的原型指针
 * 3. 常量数组：函数中使用的常量值
 * 4. 行号信息数组：调试用的行号映射
 * 5. 局部变量数组：调试用的变量信息
 * 6. 上值名称数组：调试用的上值名称
 * 7. 原型对象本身：最后释放结构体
 *
 * 内存安全：
 * 使用Lua的内存管理器确保所有内存都被正确释放，
 * 防止内存泄漏。
 *
 * 垃圾回收集成：
 * 这个函数通常由垃圾回收器调用，当原型不再被任何
 * 闭包引用时自动释放。
 *
 * 使用场景：
 * - 垃圾回收：自动回收不再使用的原型
 * - 错误处理：清理编译失败的原型
 * - 程序结束：清理所有资源
 *
 * @pre L必须是有效的Lua状态机，f必须是有效的函数原型
 * @post 函数原型及其所有关联数据被完全释放
 *
 * @note 调用此函数后f指针变为无效
 * @see luaF_newproto(), luaM_freearray()
 */
void luaF_freeproto(lua_State *L, Proto *f) {
    luaM_freearray(L, f->code, f->sizecode, Instruction);
    luaM_freearray(L, f->p, f->sizep, Proto *);
    luaM_freearray(L, f->k, f->sizek, TValue);
    luaM_freearray(L, f->lineinfo, f->sizelineinfo, int);
    luaM_freearray(L, f->locvars, f->sizelocvars, struct LocVar);
    luaM_freearray(L, f->upvalues, f->sizeupvalues, TString *);
    luaM_free(L, f);
}

/**
 * @brief 释放闭包对象
 * @param L Lua状态机指针
 * @param c 要释放的闭包对象
 *
 * 详细说明：
 * 这个函数负责释放闭包对象占用的内存。闭包的大小取决于
 * 其类型（C函数或Lua函数）和上值数量。
 *
 * 大小计算：
 * - C函数闭包：基础大小 + 上值数量 * 上值槽大小
 * - Lua函数闭包：基础大小 + 上值数量 * 上值指针大小
 *
 * 内存管理：
 * 使用luaM_freemem精确释放闭包占用的内存，避免内存碎片。
 *
 * 垃圾回收集成：
 * 通常由垃圾回收器调用，当闭包不再被引用时自动释放。
 *
 * 注意事项：
 * 这个函数只释放闭包对象本身，不处理其引用的上值对象。
 * 上值对象有独立的生命周期管理。
 *
 * @pre L必须是有效的Lua状态机，c必须是有效的闭包对象
 * @post 闭包对象被释放，但其引用的上值不受影响
 *
 * @note 调用此函数后c指针变为无效
 * @see luaF_newCclosure(), luaF_newLclosure()
 */
void luaF_freeclosure(lua_State *L, Closure *c) {
    int size = (c->c.isC) ? sizeCclosure(c->c.nupvalues) :
                            sizeLclosure(c->l.nupvalues);
    luaM_freemem(L, c, size);
}

/**
 * @brief 获取指定位置的局部变量名称
 * @param f 函数原型对象
 * @param local_number 局部变量编号（从1开始）
 * @param pc 程序计数器位置
 * @return 变量名称字符串，如果未找到则返回NULL
 *
 * 详细说明：
 * 这个函数用于调试支持，根据程序计数器位置和变量编号
 * 查找对应的局部变量名称。
 *
 * 查找策略：
 * 1. 遍历函数的局部变量信息数组
 * 2. 检查变量的生存期是否包含指定的pc位置
 * 3. 按顺序计数活跃的变量，找到第local_number个
 * 4. 返回对应变量的名称
 *
 * 变量生存期：
 * 每个局部变量都有startpc和endpc，定义了变量的有效范围。
 * 只有在有效范围内的变量才会被计入。
 *
 * 编号规则：
 * 局部变量按照在源代码中的声明顺序编号，从1开始。
 *
 * 调试应用：
 * - 错误报告：显示出错位置的变量名
 * - 调试器：实现变量查看功能
 * - 栈跟踪：提供详细的调用信息
 * - 反射机制：运行时获取变量信息
 *
 * 性能考虑：
 * 这是一个调试功能，通常不在性能关键路径上，
 * 因此优先考虑正确性而非性能。
 *
 * @pre f必须是有效的函数原型，local_number必须大于0
 * @post 返回变量名称或NULL
 *
 * @note 返回的字符串指针指向内部存储，不需要释放
 * @see LocVar结构，调试接口
 */
const char *luaF_getlocalname(const Proto *f, int local_number, int pc) {
    int i;

    // 遍历局部变量信息数组
    for (i = 0; i < f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
        // 检查变量是否在指定pc位置活跃
        if (pc < f->locvars[i].endpc) {
            local_number--;
            if (local_number == 0) {
                return getstr(f->locvars[i].varname);
            }
        }
    }

    return NULL;    // 未找到对应的局部变量
}

