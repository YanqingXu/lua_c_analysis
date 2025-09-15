/**
 * @file lstate.c
 * @brief Lua状态管理系统：虚拟机状态、线程和调用栈的完整管理
 *
 * 详细说明：
 * 本文件实现了Lua虚拟机的状态管理系统，负责整个Lua运行时环境的
 * 创建、初始化、维护和销毁。这是Lua虚拟机的核心基础设施。
 *
 * 核心概念：
 * 1. 全局状态（Global State）：整个Lua虚拟机的全局信息
 * 2. 线程状态（Thread State）：每个Lua线程（协程）的执行状态
 * 3. 调用栈（Call Stack）：函数调用的栈式管理
 * 4. 调用信息（Call Info）：每个函数调用的上下文信息
 *
 * 设计架构：
 * - 主线程：包含全局状态和主线程状态的复合结构
 * - 子线程：共享全局状态，拥有独立的执行栈
 * - 状态共享：多个线程共享全局状态，实现数据共享
 * - 资源管理：统一的内存分配和垃圾回收管理
 *
 * 生命周期管理：
 * 1. 创建阶段：分配内存，初始化全局状态和线程状态
 * 2. 运行阶段：维护调用栈，管理线程切换
 * 3. 清理阶段：关闭上值，回收所有资源
 * 4. 销毁阶段：释放内存，调用用户清理函数
 *
 * 技术特色：
 * - 协程支持：轻量级线程的完整实现
 * - 调用栈管理：动态扩展的高效调用栈
 * - 错误处理：完整的异常处理和恢复机制
 * - 调试支持：丰富的调试钩子和状态信息
 *
 * 内存管理：
 * - 统一分配器：所有内存分配通过统一接口
 * - 垃圾回收集成：状态对象完全集成到GC系统
 * - 资源追踪：精确的内存使用统计和控制
 * - 异常安全：内存分配失败时的安全清理
 *
 * 应用场景：
 * - 虚拟机初始化：创建Lua运行时环境
 * - 协程管理：实现轻量级并发
 * - 嵌入式应用：在C程序中嵌入Lua
 * - 多线程环境：每个线程独立的Lua状态
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2008-01-03
 * @since Lua 5.0
 * @see ldo.h, lgc.h, lmem.h
 */

#include <stddef.h>

#define lstate_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"

// ============================================================================
// 状态管理宏定义：内存布局和指针转换
// ============================================================================

/**
 * @brief 计算状态对象的实际内存大小
 * @param x 状态对象类型
 *
 * 包含额外空间以支持用户自定义数据的存储。
 */
#define state_size(x)   (sizeof(x) + LUAI_EXTRASPACE)

/**
 * @brief 从状态指针获取实际内存起始地址
 * @param l 状态指针
 *
 * 用于内存释放时获取正确的起始地址。
 */
#define fromstate(l)    (cast(lu_byte *, (l)) - LUAI_EXTRASPACE)

/**
 * @brief 从内存地址获取状态指针
 * @param l 内存起始地址
 *
 * 用于内存分配后获取正确的状态指针。
 */
#define tostate(l)      (cast(lua_State *, cast(lu_byte *, l) + LUAI_EXTRASPACE))

/**
 * @brief 主线程复合结构
 *
 * 详细说明：
 * 这个结构将线程状态和全局状态组合在一起，用于主线程的创建。
 * 主线程是唯一包含全局状态的线程，其他线程都共享这个全局状态。
 *
 * 内存布局：
 * 将两个结构体放在连续的内存中，便于统一分配和释放。
 *
 * 设计优势：
 * - 内存效率：一次分配两个相关结构
 * - 访问效率：全局状态和主线程状态在相邻内存
 * - 管理简化：统一的生命周期管理
 */
typedef struct LG {
    lua_State l;        // 主线程状态
    global_State g;     // 全局状态
} LG;

/**
 * @brief 初始化线程的调用栈
 * @param L1 要初始化的线程状态
 * @param L 用于内存分配的线程状态
 *
 * 详细说明：
 * 这个函数为新线程初始化调用栈和调用信息数组，建立函数调用的
 * 基础设施。调用栈是Lua函数执行的核心数据结构。
 *
 * 调用信息数组：
 * - 存储每个函数调用的上下文信息
 * - 支持嵌套函数调用和递归
 * - 动态扩展以适应深度调用
 *
 * 值栈：
 * - 存储函数参数、局部变量和临时值
 * - 连续内存布局，支持高效访问
 * - 包含额外空间以防止栈溢出
 *
 * 初始调用信息：
 * - 设置虚拟的"函数"入口（nil值）
 * - 建立基础栈帧结构
 * - 预留最小栈空间
 *
 * 栈边界管理：
 * - stack_last：标记栈的安全边界
 * - 预留EXTRA_STACK空间用于C函数调用
 * - 防止栈溢出的保护机制
 *
 * @pre L1和L必须是有效的线程状态指针
 * @post L1的调用栈被完全初始化，可以开始执行函数
 *
 * @note 这是线程创建的关键步骤
 * @see CallInfo结构，TValue类型
 */
static void stack_init(lua_State *L1, lua_State *L) {
    // 初始化调用信息数组
    L1->base_ci = luaM_newvector(L, BASIC_CI_SIZE, CallInfo);
    L1->ci = L1->base_ci;
    L1->size_ci = BASIC_CI_SIZE;
    L1->end_ci = L1->base_ci + L1->size_ci - 1;

    // 初始化值栈
    L1->stack = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, TValue);
    L1->stacksize = BASIC_STACK_SIZE + EXTRA_STACK;
    L1->top = L1->stack;
    L1->stack_last = L1->stack + (L1->stacksize - EXTRA_STACK) - 1;

    // 初始化第一个调用信息
    L1->ci->func = L1->top;                 // 虚拟函数位置
    setnilvalue(L1->top++);                 // 设置虚拟函数为nil
    L1->base = L1->ci->base = L1->top;      // 设置栈基址
    L1->ci->top = L1->top + LUA_MINSTACK;   // 预留最小栈空间
}

/**
 * @brief 释放线程的调用栈
 * @param L 用于内存释放的线程状态
 * @param L1 要释放调用栈的线程状态
 *
 * 详细说明：
 * 这个函数释放线程的调用栈相关内存，包括调用信息数组和值栈。
 * 这是线程销毁过程的重要步骤。
 *
 * 释放顺序：
 * 1. 调用信息数组：释放函数调用上下文
 * 2. 值栈：释放所有栈上的值
 *
 * 内存安全：
 * 使用Lua的内存管理器确保正确释放，防止内存泄漏。
 *
 * @pre L和L1必须是有效的线程状态指针
 * @post L1的调用栈内存被完全释放
 *
 * @note 调用前应确保栈上没有活跃的上值引用
 * @see stack_init(), luaM_freearray()
 */
static void freestack(lua_State *L, lua_State *L1) {
    luaM_freearray(L, L1->base_ci, L1->size_ci, CallInfo);
    luaM_freearray(L, L1->stack, L1->stacksize, TValue);
}

/**
 * @brief Lua状态初始化的受保护函数
 * @param L 要初始化的Lua状态
 * @param ud 用户数据（未使用）
 *
 * 详细说明：
 * 这是一个受保护的初始化函数，在luaD_rawrunprotected中调用，
 * 负责完成Lua状态的核心初始化工作。如果初始化过程中发生内存
 * 分配错误，可以安全地回滚。
 *
 * 初始化步骤：
 * 1. 调用栈初始化：建立函数调用基础设施
 * 2. 全局表创建：创建全局变量存储表
 * 3. 注册表创建：创建内部对象注册表
 * 4. 字符串表初始化：设置字符串内部化表的初始大小
 * 5. 元方法初始化：初始化元方法系统
 * 6. 词法分析器初始化：初始化词法分析器状态
 * 7. 内存错误消息：创建并固定内存错误消息字符串
 * 8. 垃圾回收阈值：设置初始的GC触发阈值
 *
 * 全局表和注册表：
 * - 全局表：存储全局变量，用户可见
 * - 注册表：存储内部对象，用户不可见
 * - 初始大小：0个数组元素，2个哈希元素
 *
 * 内存错误处理：
 * 预先创建并固定内存错误消息，确保在内存不足时仍能报告错误。
 *
 * GC阈值设置：
 * 设置为当前内存使用量的4倍，给初始化过程足够的内存空间。
 *
 * @pre L必须是有效的Lua状态，基本结构已初始化
 * @post L的核心子系统被完全初始化
 *
 * @note 这个函数在受保护环境中运行，可以安全地抛出异常
 * @see luaD_rawrunprotected(), stack_init()
 */
static void f_luaopen(lua_State *L, void *ud) {
    global_State *g = G(L);
    UNUSED(ud);

    stack_init(L, L);                                       // 初始化调用栈
    sethvalue(L, gt(L), luaH_new(L, 0, 2));                // 创建全局表
    sethvalue(L, registry(L), luaH_new(L, 0, 2));          // 创建注册表
    luaS_resize(L, MINSTRTABSIZE);                          // 初始化字符串表
    luaT_init(L);                                           // 初始化元方法
    luaX_init(L);                                           // 初始化词法分析器
    luaS_fix(luaS_newliteral(L, MEMERRMSG));               // 固定内存错误消息
    g->GCthreshold = 4 * g->totalbytes;                     // 设置GC阈值
}

/**
 * @brief 预初始化线程状态
 * @param L 要初始化的线程状态
 * @param g 全局状态指针
 *
 * 详细说明：
 * 这个函数对线程状态进行预初始化，设置所有字段为安全的初始值。
 * 这确保了在后续初始化过程中，即使发生错误也能安全清理。
 *
 * 初始化字段：
 * - 全局状态引用：建立与全局状态的连接
 * - 栈相关字段：初始化为NULL，等待后续分配
 * - 错误处理：清空错误跳转点和错误函数
 * - 调试钩子：初始化调试钩子系统
 * - 上值管理：清空开放上值列表
 * - 调用计数：重置C函数调用计数
 * - 执行状态：设置为正常状态
 * - 调用信息：清空调用信息指针
 * - 程序计数器：清空保存的程序计数器
 * - 全局表：初始化为nil
 *
 * 安全性考虑：
 * 所有指针字段初始化为NULL，所有计数字段初始化为0，
 * 确保在任何时候都可以安全地调用清理函数。
 *
 * 调试钩子初始化：
 * - allowhook：允许调试钩子
 * - 其他钩子字段：设置为默认值
 *
 * @pre L必须是有效的线程状态指针，g必须是有效的全局状态指针
 * @post L的所有字段被设置为安全的初始值
 *
 * @note 这是线程创建的第一步，确保状态的一致性
 * @see luaE_newthread(), lua_newstate()
 */
static void preinit_state(lua_State *L, global_State *g) {
    G(L) = g;                           // 设置全局状态引用
    L->stack = NULL;                    // 栈指针
    L->stacksize = 0;                   // 栈大小
    L->errorJmp = NULL;                 // 错误跳转点
    L->hook = NULL;                     // 调试钩子函数
    L->hookmask = 0;                    // 钩子掩码
    L->basehookcount = 0;               // 基础钩子计数
    L->allowhook = 1;                   // 允许钩子
    resethookcount(L);                  // 重置钩子计数
    L->openupval = NULL;                // 开放上值列表
    L->size_ci = 0;                     // 调用信息数组大小
    L->nCcalls = L->baseCcalls = 0;     // C函数调用计数
    L->status = 0;                      // 线程状态
    L->base_ci = L->ci = NULL;          // 调用信息指针
    L->savedpc = NULL;                  // 保存的程序计数器
    L->errfunc = 0;                     // 错误处理函数
    setnilvalue(gt(L));                 // 全局表初始化为nil
}


/**
 * @brief 关闭并清理Lua状态
 * @param L 要关闭的Lua状态（必须是主线程）
 *
 * 详细说明：
 * 这个函数执行Lua状态的完整清理过程，释放所有相关资源。
 * 这是Lua虚拟机生命周期的最后阶段。
 *
 * 清理步骤：
 * 1. 关闭上值：关闭所有开放的上值，确保数据一致性
 * 2. 垃圾回收：回收所有可回收对象
 * 3. 字符串表清理：释放字符串哈希表
 * 4. 缓冲区清理：释放全局缓冲区
 * 5. 调用栈清理：释放调用栈和调用信息
 * 6. 状态对象清理：释放状态对象本身
 *
 * 断言检查：
 * - rootgc应该只剩下主线程对象
 * - 字符串表应该为空（所有字符串已回收）
 * - 总内存应该只剩下LG结构的大小
 *
 * 内存管理：
 * 使用用户提供的分配器释放最后的内存块，
 * 确保所有内存都被正确归还。
 *
 * 安全性：
 * 函数确保即使在异常情况下也能正确清理资源，
 * 防止内存泄漏。
 *
 * @pre L必须是主线程状态，所有子线程已被释放
 * @post 所有资源被释放，L指针变为无效
 *
 * @note 这是虚拟机清理的最终步骤
 * @see lua_close(), luaC_freeall()
 */
static void close_state(lua_State *L) {
    global_State *g = G(L);
    luaF_close(L, L->stack);                                    // 关闭所有上值
    luaC_freeall(L);                                            // 回收所有对象
    lua_assert(g->rootgc == obj2gco(L));                        // 只剩主线程
    lua_assert(g->strt.nuse == 0);                              // 字符串表为空
    luaM_freearray(L, G(L)->strt.hash, G(L)->strt.size, TString *); // 释放字符串表
    luaZ_freebuffer(L, &g->buff);                               // 释放全局缓冲区
    freestack(L, L);                                            // 释放调用栈
    lua_assert(g->totalbytes == sizeof(LG));                    // 内存计数正确
    (*g->frealloc)(g->ud, fromstate(L), state_size(LG), 0);    // 释放状态对象
}

/**
 * @brief 创建新的Lua线程（协程）
 * @param L 父线程状态
 * @return 新创建的线程状态指针
 *
 * 详细说明：
 * 这个函数创建一个新的Lua线程（协程），新线程共享父线程的
 * 全局状态，但拥有独立的执行栈和调用上下文。
 *
 * 协程特性：
 * - 轻量级：只分配线程状态，不分配全局状态
 * - 共享全局状态：访问相同的全局变量和函数
 * - 独立执行栈：拥有独立的调用栈和局部变量
 * - 调试钩子继承：继承父线程的调试设置
 *
 * 创建过程：
 * 1. 分配线程状态内存
 * 2. 链接到垃圾回收器
 * 3. 预初始化线程状态
 * 4. 初始化调用栈
 * 5. 共享全局表
 * 6. 继承调试钩子设置
 *
 * 内存管理：
 * 新线程被标记为白色，参与垃圾回收。
 * 当不再被引用时会自动回收。
 *
 * 调试支持：
 * 新线程继承父线程的调试钩子设置，
 * 支持统一的调试体验。
 *
 * @pre L必须是有效的Lua状态
 * @post 返回完全初始化的新线程状态
 *
 * @note 新线程初始状态为白色，需要适当的引用管理
 * @see luaE_freethread(), preinit_state()
 */
lua_State *luaE_newthread(lua_State *L) {
    lua_State *L1 = tostate(luaM_malloc(L, state_size(lua_State)));
    luaC_link(L, obj2gco(L1), LUA_TTHREAD);         // 链接到GC
    preinit_state(L1, G(L));                        // 预初始化
    stack_init(L1, L);                              // 初始化调用栈
    setobj2n(L, gt(L1), gt(L));                     // 共享全局表
    L1->hookmask = L->hookmask;                     // 继承钩子掩码
    L1->basehookcount = L->basehookcount;           // 继承钩子计数
    L1->hook = L->hook;                             // 继承钩子函数
    resethookcount(L1);                             // 重置钩子计数
    lua_assert(iswhite(obj2gco(L1)));               // 确保为白色
    return L1;
}

/**
 * @brief 释放Lua线程
 * @param L 用于内存管理的线程状态
 * @param L1 要释放的线程状态
 *
 * 详细说明：
 * 这个函数释放一个Lua线程及其所有相关资源。
 * 这通常由垃圾回收器调用，当线程不再被引用时。
 *
 * 释放步骤：
 * 1. 关闭上值：关闭线程的所有开放上值
 * 2. 用户状态清理：调用用户定义的清理函数
 * 3. 调用栈释放：释放调用栈和调用信息
 * 4. 线程对象释放：释放线程状态对象本身
 *
 * 安全检查：
 * 确保所有开放上值都已关闭，防止悬挂引用。
 *
 * 用户扩展：
 * 支持用户定义的状态清理函数，允许清理用户数据。
 *
 * 内存管理：
 * 精确释放线程占用的内存，防止内存泄漏。
 *
 * @pre L和L1必须是有效的线程状态指针
 * @post L1的所有资源被释放，L1指针变为无效
 *
 * @note 通常由垃圾回收器自动调用
 * @see luaE_newthread(), luaF_close()
 */
void luaE_freethread(lua_State *L, lua_State *L1) {
    luaF_close(L1, L1->stack);                      // 关闭所有上值
    lua_assert(L1->openupval == NULL);              // 确保上值已关闭
    luai_userstatefree(L1);                         // 用户状态清理
    freestack(L, L1);                               // 释放调用栈
    luaM_freemem(L, fromstate(L1), state_size(lua_State)); // 释放线程对象
}


/**
 * @brief 创建新的Lua状态（主要API函数）
 * @param f 内存分配函数
 * @param ud 用户数据，传递给分配函数
 * @return 新创建的Lua状态指针，失败时返回NULL
 *
 * 详细说明：
 * 这是Lua虚拟机的主要创建函数，负责分配和初始化一个完整的
 * Lua运行时环境。这包括主线程状态和全局状态的创建。
 *
 * 创建过程：
 * 1. 内存分配：分配LG结构（主线程+全局状态）
 * 2. 基础初始化：设置线程类型和垃圾回收标记
 * 3. 全局状态初始化：设置所有全局状态字段
 * 4. 受保护初始化：在保护环境中完成核心初始化
 * 5. 用户扩展：调用用户定义的初始化函数
 *
 * 全局状态初始化：
 * - 内存管理：设置分配器和用户数据
 * - 垃圾回收：初始化GC系统的所有字段
 * - 字符串表：初始化字符串内部化表
 * - 元方法表：清空所有类型的元方法表
 * - 上值管理：初始化上值双向链表头
 * - 缓冲区：初始化全局字符串缓冲区
 *
 * 垃圾回收初始化：
 * - 设置初始的白色标记
 * - 初始化各种GC链表为空
 * - 设置GC参数为默认值
 * - 将主线程作为GC根对象
 *
 * 错误处理：
 * 如果初始化过程中发生错误（通常是内存不足），
 * 会自动清理已分配的资源并返回NULL。
 *
 * @pre f必须是有效的内存分配函数
 * @post 返回完全初始化的Lua状态或NULL
 *
 * @note 这是Lua C API的入口点
 * @see lua_close(), luaL_newstate()
 */
LUA_API lua_State *lua_newstate(lua_Alloc f, void *ud) {
    int i;
    lua_State *L;
    global_State *g;
    void *l = (*f)(ud, NULL, 0, state_size(LG));

    if (l == NULL) {
        return NULL;    // 内存分配失败
    }

    // 设置基本指针
    L = tostate(l);
    g = &((LG *)L)->g;

    // 初始化线程基本属性
    L->next = NULL;
    L->tt = LUA_TTHREAD;
    g->currentwhite = bit2mask(WHITE0BIT, FIXEDBIT);
    L->marked = luaC_white(g);
    set2bits(L->marked, FIXEDBIT, SFIXEDBIT);
    preinit_state(L, g);

    // 初始化全局状态
    g->frealloc = f;                            // 内存分配器
    g->ud = ud;                                 // 用户数据
    g->mainthread = L;                          // 主线程引用
    g->uvhead.u.l.prev = &g->uvhead;           // 上值链表头
    g->uvhead.u.l.next = &g->uvhead;
    g->GCthreshold = 0;                         // GC阈值（标记为未完成）
    g->strt.size = 0;                           // 字符串表大小
    g->strt.nuse = 0;                           // 字符串表使用数
    g->strt.hash = NULL;                        // 字符串表哈希数组
    setnilvalue(registry(L));                   // 注册表初始化为nil
    luaZ_initbuffer(L, &g->buff);               // 初始化全局缓冲区
    g->panic = NULL;                            // 恐慌函数

    // 初始化垃圾回收状态
    g->gcstate = GCSpause;                      // GC状态：暂停
    g->rootgc = obj2gco(L);                     // GC根对象
    g->sweepstrgc = 0;                          // 字符串清扫位置
    g->sweepgc = &g->rootgc;                    // 对象清扫位置
    g->gray = NULL;                             // 灰色对象链表
    g->grayagain = NULL;                        // 重新标记的灰色对象
    g->weak = NULL;                             // 弱引用表链表
    g->tmudata = NULL;                          // 有终结器的用户数据
    g->totalbytes = sizeof(LG);                 // 总内存使用量
    g->gcpause = LUAI_GCPAUSE;                  // GC暂停参数
    g->gcstepmul = LUAI_GCMUL;                  // GC步进倍数
    g->gcdept = 0;                              // GC债务

    // 初始化元方法表
    for (i = 0; i < NUM_TAGS; i++) {
        g->mt[i] = NULL;
    }

    // 受保护的初始化
    if (luaD_rawrunprotected(L, f_luaopen, NULL) != 0) {
        close_state(L);                         // 初始化失败，清理资源
        L = NULL;
    } else {
        luai_userstateopen(L);                  // 用户状态初始化
    }

    return L;
}

/**
 * @brief 调用所有垃圾回收元方法的受保护函数
 * @param L Lua状态机指针
 * @param ud 用户数据（未使用）
 *
 * 详细说明：
 * 这是一个受保护的辅助函数，用于在lua_close过程中安全地
 * 调用所有用户数据的垃圾回收元方法。
 *
 * 安全调用：
 * 在受保护环境中调用，即使元方法抛出错误也不会中断关闭过程。
 *
 * 使用场景：
 * 在虚拟机关闭时，确保所有资源得到正确清理。
 *
 * @note 这是lua_close的辅助函数
 * @see lua_close(), luaC_callGCTM()
 */
static void callallgcTM(lua_State *L, void *ud) {
    UNUSED(ud);
    luaC_callGCTM(L);    // 调用所有GC元方法
}

/**
 * @brief 关闭Lua状态（主要API函数）
 * @param L 要关闭的Lua状态
 *
 * 详细说明：
 * 这是Lua虚拟机的主要关闭函数，负责安全地关闭Lua状态并
 * 释放所有相关资源。这是虚拟机生命周期的最后阶段。
 *
 * 关闭过程：
 * 1. 确保操作主线程：只有主线程可以被关闭
 * 2. 线程锁定：防止并发访问
 * 3. 关闭上值：关闭所有开放的上值
 * 4. 分离用户数据：分离有GC元方法的用户数据
 * 5. 重复调用GC元方法：直到没有错误为止
 * 6. 用户状态清理：调用用户定义的清理函数
 * 7. 状态关闭：执行最终的资源释放
 *
 * 安全性保证：
 * - 重复执行GC元方法直到成功，确保所有资源被清理
 * - 重置执行状态，防止元方法执行时的状态不一致
 * - 使用受保护调用，防止元方法错误中断关闭过程
 *
 * 状态重置：
 * 在每次尝试调用GC元方法前，重置线程的执行状态：
 * - 调用信息：重置到基础调用信息
 * - 栈指针：重置栈顶和基址
 * - 调用计数：重置C函数调用计数
 *
 * 错误处理：
 * 即使在关闭过程中发生错误，函数也会继续执行，
 * 确保资源得到最大程度的清理。
 *
 * 线程安全：
 * 函数会锁定状态，防止在关闭过程中的并发访问。
 *
 * @pre L必须是有效的Lua状态
 * @post L的所有资源被释放，L指针变为无效
 *
 * @note 这是Lua C API的重要函数，与lua_newstate配对使用
 * @see lua_newstate(), close_state()
 */
LUA_API void lua_close(lua_State *L) {
    L = G(L)->mainthread;                       // 确保操作主线程
    lua_lock(L);                                // 锁定状态
    luaF_close(L, L->stack);                    // 关闭所有上值
    luaC_separateudata(L, 1);                   // 分离有GC元方法的用户数据
    L->errfunc = 0;                             // 清除错误处理函数

    // 重复调用GC元方法直到没有错误
    do {
        L->ci = L->base_ci;                     // 重置调用信息
        L->base = L->top = L->ci->base;         // 重置栈指针
        L->nCcalls = L->baseCcalls = 0;         // 重置调用计数
    } while (luaD_rawrunprotected(L, callallgcTM, NULL) != 0);

    lua_assert(G(L)->tmudata == NULL);          // 确保所有用户数据已处理
    luai_userstateclose(L);                     // 用户状态清理
    close_state(L);                             // 最终状态关闭
}

