/*
** [核心] Lua 虚拟机状态管理模块
**
** 详细功能说明：
** 本模块是 Lua 虚拟机的核心组件，负责管理 Lua 状态机的完整生命周期，
** 包括状态的创建、初始化、线程管理、资源分配和清理等关键功能。
**
** 主要职责：
** - 全局状态（global_State）的创建和管理
** - 线程状态（lua_State）的创建和销毁
** - 虚拟机栈的初始化和内存管理
** - 垃圾回收器的初始化和配置
** - 字符串表、注册表等核心数据结构的初始化
** - 错误处理和钩子系统的设置
**
** 设计架构：
** 采用主线程+全局状态的设计模式，所有协程共享同一个全局状态，
** 但拥有独立的执行栈和调用信息，确保线程安全和资源隔离。
**
** 内存管理策略：
** 使用用户提供的内存分配器，支持自定义内存管理策略，
** 所有内存操作都通过统一的接口进行，便于内存使用统计和调试。
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

// 计算状态结构体的实际大小，包含用户额外空间
// LUAI_EXTRASPACE 允许用户在状态结构前添加自定义数据
#define state_size(x) (sizeof(x) + LUAI_EXTRASPACE)

// 从状态指针获取原始内存指针（包含额外空间的起始地址）
// 用于内存释放时获取完整的内存块地址
#define fromstate(l) (cast(lu_byte *, (l)) - LUAI_EXTRASPACE)

// 从原始内存指针获取状态指针（跳过额外空间）
// 用于内存分配后获取实际的状态结构地址
#define tostate(l) (cast(lua_State *, cast(lu_byte *, l) + LUAI_EXTRASPACE))

/*
** [数据结构] 主线程复合结构体
**
** 详细功能说明：
** 将线程状态和全局状态组合在一个连续的内存块中，这样设计的优势：
** - 减少内存分配次数，提高分配效率
** - 保证主线程和全局状态的内存局部性
** - 简化主线程的创建和销毁逻辑
** - 便于内存使用统计和调试
**
** 内存布局：
** [LUAI_EXTRASPACE][lua_State l][global_State g]
**
** 注意事项：
** - 只有主线程使用此结构，其他协程只分配 lua_State
** - 全局状态在整个 Lua 虚拟机生命周期内保持唯一
** - 所有协程通过 G(L) 宏访问共享的全局状态
*/
typedef struct LG
{
    lua_State l;        // 主线程状态结构
    global_State g;     // 全局状态结构（所有线程共享）
} LG;


/*
** [核心] 初始化 Lua 状态的栈结构和调用信息
**
** 详细功能说明：
** 为新创建的 Lua 状态分配和初始化执行栈以及调用信息数组。
** 这是 Lua 虚拟机能够执行代码的基础设施，包括：
** - 分配调用信息数组用于管理函数调用链
** - 分配值栈用于存储局部变量、参数和临时值
** - 设置初始的调用环境和栈指针
**
** 参数说明：
** @param L1 - lua_State*：要初始化栈结构的目标状态
** @param L - lua_State*：用于内存分配的源状态（提供内存分配器）
**
** 返回值：无（通过修改 L1 的字段完成初始化）
**
** 算法复杂度：O(1) 时间，O(BASIC_STACK_SIZE + BASIC_CI_SIZE) 空间
**
** 注意事项：
** - 调用前 L1 的栈相关字段应为 NULL 或未初始化状态
** - 内存分配失败会抛出异常，调用者需要在保护模式下调用
** - 初始化后的栈已经包含一个 nil 值作为哨兵
*/
static void stack_init(lua_State *L1, lua_State *L)
{
    // 第一阶段：分配和初始化调用信息数组
    // 调用信息数组用于跟踪函数调用链和局部变量作用域
    L1->base_ci = luaM_newvector(L, BASIC_CI_SIZE, CallInfo);
    L1->ci = L1->base_ci;
    L1->size_ci = BASIC_CI_SIZE;
    L1->end_ci = L1->base_ci + L1->size_ci - 1;

    // 第二阶段：分配和初始化值栈
    // 值栈存储所有运行时值：局部变量、参数、临时计算结果等
    L1->stack = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, TValue);
    L1->stacksize = BASIC_STACK_SIZE + EXTRA_STACK;
    L1->top = L1->stack;

    // 计算栈的有效末尾位置（保留 EXTRA_STACK 作为安全缓冲区）
    // 这个缓冲区用于防止栈溢出时的紧急操作
    L1->stack_last = L1->stack + (L1->stacksize - EXTRA_STACK) - 1;

    // 第三阶段：初始化第一个调用信息（主函数的调用环境）
    // 在栈顶放置一个 nil 值作为主函数的占位符
    L1->ci->func = L1->top;
    setnilvalue(L1->top++);

    // 设置基址指针和调用信息的栈顶限制
    // base 指向当前函数的局部变量起始位置
    L1->base = L1->ci->base = L1->top;
    L1->ci->top = L1->top + LUA_MINSTACK;
}


/*
** [内存] 释放 Lua 状态的栈相关内存
**
** 详细功能说明：
** 释放指定 Lua 状态的所有栈相关内存，包括调用信息数组和值栈。
** 这是状态清理过程的重要组成部分，确保没有内存泄漏。
**
** 参数说明：
** @param L - lua_State*：提供内存分配器的状态（通常是主线程）
** @param L1 - lua_State*：要释放栈内存的目标状态
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，释放 O(stacksize + size_ci) 空间
**
** 注意事项：
** - 调用前应确保栈上的所有对象已经被垃圾回收器处理
** - 调用后 L1 的栈相关字段将指向无效内存，不应再访问
** - 此函数不检查参数有效性，调用者需要保证参数正确
*/
static void freestack(lua_State *L, lua_State *L1)
{
    // 释放调用信息数组内存
    // 调用信息数组存储函数调用链和作用域信息
    luaM_freearray(L, L1->base_ci, L1->size_ci, CallInfo);

    // 释放值栈内存
    // 值栈存储所有运行时的 Lua 值
    luaM_freearray(L, L1->stack, L1->stacksize, TValue);
}


/*
** [核心] 在保护模式下初始化 Lua 状态的核心组件
**
** 详细功能说明：
** 这个函数在保护模式下执行所有可能导致内存分配错误的初始化操作。
** 如果任何步骤失败，会通过异常机制安全地回滚，避免部分初始化状态。
**
** 初始化的核心组件包括：
** - 执行栈和调用信息数组
** - 全局变量表和注册表
** - 字符串表和标签方法系统
** - 词法分析器和错误消息
** - 垃圾回收器的初始配置
**
** 参数说明：
** @param L - lua_State*：要初始化的 Lua 状态
** @param ud - void*：用户数据（在此函数中未使用）
**
** 返回值：无（失败时通过异常机制处理）
**
** 算法复杂度：O(1) 时间，O(BASIC_STACK_SIZE + 初始表大小) 空间
**
** 注意事项：
** - 必须在保护模式下调用，以处理内存分配失败
** - 任何步骤失败都会导致整个初始化过程回滚
** - 调用后垃圾回收器处于可用状态
*/
static void f_luaopen(lua_State *L, void *ud)
{
    // 获取全局状态指针，用于访问全局配置
    global_State *g = G(L);
    UNUSED(ud);

    // 第一阶段：初始化执行环境
    // 创建栈和调用信息数组，这是代码执行的基础
    stack_init(L, L);

    // 第二阶段：创建核心表结构
    // 创建全局变量表，用于存储全局变量和函数
    // 初始大小为 2，足够存储基本的全局对象
    sethvalue(L, gt(L), luaH_new(L, 0, 2));

    // 创建注册表，用于存储 C 代码和 Lua 代码之间的共享数据
    // 注册表是一个特殊的全局表，只能通过 C API 访问
    sethvalue(L, registry(L), luaH_new(L, 0, 2));

    // 第三阶段：初始化字符串管理系统
    // 设置字符串表的初始大小，字符串表用于字符串的内部化
    luaS_resize(L, MINSTRTABSIZE);

    // 第四阶段：初始化元编程系统
    // 初始化标签方法（元方法）系统，支持操作符重载等功能
    luaT_init(L);

    // 初始化词法分析器，设置关键字和操作符的内部表示
    luaX_init(L);

    // 第五阶段：设置错误处理
    // 创建并固定内存错误消息字符串，防止在内存不足时被回收
    // 这确保即使在内存紧张时也能报告内存错误
    luaS_fix(luaS_newliteral(L, MEMERRMSG));

    // 第六阶段：配置垃圾回收器
    // 设置垃圾回收阈值为当前内存使用量的 4 倍
    // 这个设置平衡了内存使用和垃圾回收频率
    g->GCthreshold = 4 * g->totalbytes;
}


/*
** [核心] 预初始化 Lua 状态的基本字段
**
** 详细功能说明：
** 将 Lua 状态的所有字段设置为安全的默认值，确保状态处于一致的
** 初始状态。这是状态创建过程的第一步，为后续的完整初始化做准备。
**
** 初始化的字段类别：
** - 全局状态关联和栈管理字段
** - 错误处理和异常跳转字段
** - 调试钩子和性能监控字段
** - 上值管理和调用信息字段
** - C 调用计数和执行状态字段
**
** 参数说明：
** @param L - lua_State*：要初始化的 Lua 状态
** @param g - global_State*：关联的全局状态指针
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 此函数只设置字段为默认值，不分配内存
** - 调用后状态尚未完全可用，需要后续的完整初始化
** - 所有指针字段都被设置为 NULL，计数字段设置为 0
*/
static void preinit_state(lua_State *L, global_State *g)
{
    // 第一阶段：建立全局状态关联
    // 设置全局状态指针，使状态能够访问共享的全局数据
    G(L) = g;

    // 第二阶段：初始化栈管理字段
    // 栈相关字段设置为 NULL，表示尚未分配栈内存
    L->stack = NULL;
    L->stacksize = 0;

    // 第三阶段：初始化错误处理字段
    // 错误跳转和错误函数字段，用于异常处理机制
    L->errorJmp = NULL;
    L->errfunc = 0;

    // 第四阶段：初始化调试钩子字段
    // 钩子系统用于调试、性能分析和代码监控
    L->hook = NULL;
    L->hookmask = 0;
    L->basehookcount = 0;
    L->allowhook = 1;
    resethookcount(L);

    // 第五阶段：初始化上值和调用信息字段
    // 上值用于闭包，调用信息用于函数调用栈管理
    L->openupval = NULL;
    L->size_ci = 0;
    L->base_ci = L->ci = NULL;

    // 第六阶段：初始化执行状态字段
    // C 调用计数用于防止 C 栈溢出，状态字段表示执行状态
    L->nCcalls = L->baseCcalls = 0;
    L->status = 0;

    // 第七阶段：初始化程序计数器
    // 程序计数器指向当前执行的字节码位置
    L->savedpc = NULL;

    // 第八阶段：初始化全局变量表
    // 将全局变量表设置为 nil，后续会创建实际的表对象
    setnilvalue(gt(L));
}


/*
** [核心] 关闭 Lua 状态并释放所有相关资源
**
** 详细功能说明：
** 执行 Lua 状态的完整清理过程，按照正确的顺序释放所有资源，
** 确保没有内存泄漏和悬空指针。这是 Lua 虚拟机生命周期的最后阶段。
**
** 清理顺序的重要性：
** 1. 先关闭上值，避免访问已释放的栈内存
** 2. 再执行垃圾回收，清理所有 Lua 对象
** 3. 然后释放系统级资源（字符串表、缓冲区等）
** 4. 最后释放栈和状态本身的内存
**
** 参数说明：
** @param L - lua_State*：要关闭的 Lua 状态（必须是主线程）
**
** 返回值：无
**
** 算法复杂度：O(n) 时间（n 为对象总数），释放所有已分配空间
**
** 注意事项：
** - 只能对主线程调用，协程通过 luaE_freethread 释放
** - 调用后 L 指针无效，不应再访问
** - 内存分配器本身不会被释放，由调用者管理
*/
static void close_state(lua_State *L)
{
    // 获取全局状态指针，用于访问全局资源
    global_State *g = G(L);

    // 第一阶段：关闭所有打开的上值
    // 上值可能引用栈上的变量，必须在释放栈之前关闭
    luaF_close(L, L->stack);

    // 第二阶段：执行完整的垃圾回收
    // 释放所有 Lua 对象：表、函数、字符串、用户数据等
    luaC_freeall(L);

    // 第三阶段：验证清理完整性
    // 确保垃圾回收器已经清理了除主线程外的所有对象
    lua_assert(g->rootgc == obj2gco(L));

    // 确保字符串表中的所有字符串都已被释放
    lua_assert(g->strt.nuse == 0);

    // 第四阶段：释放系统级数据结构
    // 释放字符串表的哈希数组（字符串对象本身已在垃圾回收中释放）
    luaM_freearray(L, G(L)->strt.hash, G(L)->strt.size, TString *);

    // 释放全局缓冲区，用于字符串操作和 I/O 的临时存储
    luaZ_freebuffer(L, &g->buff);

    // 第五阶段：释放栈相关内存
    // 释放执行栈和调用信息数组
    freestack(L, L);

    // 第六阶段：最终验证和清理
    // 验证所有内存都已正确释放，只剩下 LG 结构体本身
    lua_assert(g->totalbytes == sizeof(LG));

    // 释放主线程和全局状态的内存块
    // 这是最后一次内存操作，之后 L 指针无效
    (*g->frealloc)(g->ud, fromstate(L), state_size(LG), 0);
}


/*
** [核心] 创建新的 Lua 协程线程
**
** 详细功能说明：
** 创建一个新的 Lua 协程，与主线程共享全局状态但拥有独立的执行栈。
** 新线程继承创建者的调试钩子设置，并被自动注册到垃圾回收器中。
**
** 协程特性：
** - 独立的执行栈和调用信息
** - 共享全局状态和全局变量表
** - 继承调试钩子配置
** - 自动垃圾回收管理
**
** 参数说明：
** @param L - lua_State*：创建协程的父线程状态
**
** 返回值：
** @return lua_State*：新创建的协程状态指针
**
** 算法复杂度：O(1) 时间，O(BASIC_STACK_SIZE) 空间
**
** 注意事项：
** - 新线程的生命周期由垃圾回收器管理
** - 内存分配失败会抛出异常
** - 新线程初始状态为可执行状态
*/
lua_State *luaE_newthread(lua_State *L)
{
    // 第一阶段：分配新线程的内存
    // 只分配 lua_State 结构，不包含全局状态（与主线程共享）
    lua_State *L1 = tostate(luaM_malloc(L, state_size(lua_State)));

    // 第二阶段：注册到垃圾回收器
    // 将新线程作为可回收对象链接到垃圾回收器的管理链表中
    luaC_link(L, obj2gco(L1), LUA_TTHREAD);

    // 第三阶段：基本状态初始化
    // 预初始化新线程状态，与父线程共享同一个全局状态
    preinit_state(L1, G(L));

    // 第四阶段：创建独立的执行环境
    // 为新线程分配独立的栈和调用信息数组
    stack_init(L1, L);

    // 第五阶段：共享全局环境
    // 新线程共享父线程的全局变量表，确保全局变量的一致性
    setobj2n(L, gt(L1), gt(L));

    // 第六阶段：继承调试配置
    // 复制父线程的钩子设置，保持调试行为的一致性
    L1->hookmask = L->hookmask;
    L1->basehookcount = L->basehookcount;
    L1->hook = L->hook;
    resethookcount(L1);

    // 第七阶段：验证初始化状态
    // 确保新线程被正确标记为白色（可被垃圾回收）
    lua_assert(iswhite(obj2gco(L1)));

    return L1;
}


/*
** [内存] 释放 Lua 协程线程及其所有资源
**
** 详细功能说明：
** 安全地释放一个协程线程的所有资源，包括上值、栈内存和线程本身。
** 这个函数通常由垃圾回收器调用，确保协程的完整清理。
**
** 清理步骤：
** 1. 关闭所有打开的上值，避免悬空引用
** 2. 调用用户自定义的状态清理函数
** 3. 释放栈和调用信息数组的内存
** 4. 释放线程结构本身的内存
**
** 参数说明：
** @param L - lua_State*：提供内存分配器的主线程状态
** @param L1 - lua_State*：要释放的协程线程状态
**
** 返回值：无
**
** 算法复杂度：O(n) 时间（n 为打开的上值数量），释放 O(stacksize) 空间
**
** 注意事项：
** - 只能释放协程线程，不能释放主线程
** - 调用后 L1 指针无效，不应再访问
** - 必须确保没有其他地方引用此线程
*/
void luaE_freethread(lua_State *L, lua_State *L1)
{
    // 第一阶段：关闭所有上值
    // 上值可能引用栈上的变量，必须在释放栈之前关闭
    luaF_close(L1, L1->stack);

    // 验证所有上值都已正确关闭
    lua_assert(L1->openupval == NULL);

    // 第二阶段：调用用户清理函数
    // 允许用户在线程销毁前执行自定义清理逻辑
    luai_userstatefree(L1);

    // 第三阶段：释放栈相关内存
    // 释放执行栈和调用信息数组
    freestack(L, L1);

    // 第四阶段：释放线程结构内存
    // 释放线程本身的内存，包括用户额外空间
    luaM_freemem(L, fromstate(L1), state_size(lua_State));
}


/*
** [核心] 创建新的 Lua 虚拟机状态
**
** 详细功能说明：
** 创建一个完整的 Lua 虚拟机实例，包括主线程和全局状态。
** 这是 Lua 虚拟机的入口点，负责初始化所有核心组件。
**
** 初始化的主要组件：
** - 主线程状态和全局状态的内存分配
** - 垃圾回收器的初始化和配置
** - 字符串表、注册表等核心数据结构
** - 标签方法系统和词法分析器
** - 错误处理机制和调试钩子系统
**
** 参数说明：
** @param f - lua_Alloc：用户提供的内存分配函数
** @param ud - void*：传递给内存分配函数的用户数据
**
** 返回值：
** @return lua_State*：新创建的 Lua 状态，失败时返回 NULL
**
** 算法复杂度：O(1) 时间，O(初始内存大小) 空间
**
** 注意事项：
** - 内存分配失败会返回 NULL，不会抛出异常
** - 返回的状态已完全初始化，可以立即使用
** - 调用者负责最终调用 lua_close 释放资源
*/
LUA_API lua_State *lua_newstate(lua_Alloc f, void *ud)
{
    int i;
    lua_State *L;
    global_State *g;

    // 第一阶段：分配主线程和全局状态的内存
    // 使用 LG 结构将主线程和全局状态分配在连续内存中
    void *l = (*f)(ud, NULL, 0, state_size(LG));

    // 内存分配失败检查
    if (l == NULL)
    {
        return NULL;
    }

    // 第二阶段：初始化基本指针和结构
    // 从分配的内存中提取主线程和全局状态指针
    L = tostate(l);
    g = &((LG *)L)->g;

    // 第三阶段：初始化线程基本属性
    // 设置线程链表和类型标记
    L->next = NULL;
    L->tt = LUA_TTHREAD;

    // 第四阶段：初始化垃圾回收器标记
    // 设置当前白色标记和主线程的垃圾回收标记
    g->currentwhite = bit2mask(WHITE0BIT, FIXEDBIT);
    L->marked = luaC_white(g);
    set2bits(L->marked, FIXEDBIT, SFIXEDBIT);

    // 第五阶段：预初始化状态字段
    // 将所有状态字段设置为安全的默认值
    preinit_state(L, g);

    // 第六阶段：设置内存管理
    // 保存用户提供的内存分配器和用户数据
    g->frealloc = f;
    g->ud = ud;

    // 第七阶段：设置主线程引用
    // 全局状态保存主线程的引用，用于状态管理
    g->mainthread = L;

    // 第八阶段：初始化上值管理链表
    // 创建双向循环链表用于管理所有打开的上值
    g->uvhead.u.l.prev = &g->uvhead;
    g->uvhead.u.l.next = &g->uvhead;
    
    // 第九阶段：初始化垃圾回收器状态
    // 设置垃圾回收器的初始状态和各种链表指针
    g->GCthreshold = 0;  // 标记为未完成状态，完整初始化后会重新设置
    g->gcstate = GCSpause;
    g->rootgc = obj2gco(L);
    g->sweepstrgc = 0;
    g->sweepgc = &g->rootgc;
    g->gray = NULL;
    g->grayagain = NULL;
    g->weak = NULL;
    g->tmudata = NULL;

    // 第十阶段：初始化字符串表
    // 字符串表用于字符串的内部化，初始为空
    g->strt.size = 0;
    g->strt.nuse = 0;
    g->strt.hash = NULL;

    // 第十一阶段：初始化注册表
    // 注册表是 C 代码和 Lua 代码之间的共享存储空间
    setnilvalue(registry(L));

    // 第十二阶段：初始化全局缓冲区
    // 全局缓冲区用于字符串操作和 I/O 的临时存储
    luaZ_initbuffer(L, &g->buff);

    // 第十三阶段：初始化错误处理
    // panic 函数在无法恢复的错误时被调用
    g->panic = NULL;

    // 第十四阶段：初始化内存统计和垃圾回收参数
    // 设置内存使用统计和垃圾回收的调优参数
    g->totalbytes = sizeof(LG);
    g->gcpause = LUAI_GCPAUSE;
    g->gcstepmul = LUAI_GCMUL;
    g->gcdept = 0;

    // 第十五阶段：初始化基本类型的元表
    // 为所有基本类型（数字、字符串等）的元表设置为 NULL
    for (i = 0; i < NUM_TAGS; i++)
    {
        g->mt[i] = NULL;
    }

    // 第十六阶段：在保护模式下完成初始化
    // 执行可能失败的初始化操作，如内存分配
    if (luaD_rawrunprotected(L, f_luaopen, NULL) != 0)
    {
        // 初始化失败：清理已分配的资源
        close_state(L);
        L = NULL;
    }
    else
    {
        // 初始化成功：调用用户自定义的状态打开函数
        luai_userstateopen(L);
    }

    return L;
}


/*
** [内存] 在保护模式下调用所有垃圾回收元方法
**
** 详细功能说明：
** 为所有具有 __gc 元方法的用户数据对象调用其垃圾回收元方法。
** 这个函数在 lua_close 过程中被调用，确保用户数据能够正确清理。
**
** 执行特点：
** - 在保护模式下执行，避免元方法中的错误影响关闭过程
** - 可能被多次调用，直到没有新的需要处理的对象
** - 元方法执行期间可能创建新的需要回收的对象
**
** 参数说明：
** @param L - lua_State*：Lua 状态指针
** @param ud - void*：用户数据（在此函数中未使用）
**
** 返回值：无
**
** 算法复杂度：O(n) 时间（n 为有 __gc 元方法的对象数量）
**
** 注意事项：
** - 元方法执行可能产生新的垃圾回收对象
** - 必须在保护模式下调用以处理元方法中的错误
** - 调用期间不应使用错误函数，避免递归错误
*/
static void callallgcTM(lua_State *L, void *ud)
{
    // 标记未使用的参数，避免编译器警告
    UNUSED(ud);

    // 调用垃圾回收器处理所有待处理的 __gc 元方法
    // 这可能触发用户定义的清理代码
    luaC_callGCTM(L);
}


/*
** [核心] 关闭 Lua 虚拟机状态并释放所有资源
**
** 详细功能说明：
** 安全地关闭整个 Lua 虚拟机，执行完整的清理过程。这包括调用所有
** 垃圾回收元方法、释放所有内存、清理所有数据结构等操作。
**
** 关闭过程的关键步骤：
** 1. 确保只有主线程被关闭（协程不能直接关闭）
** 2. 关闭所有上值，避免悬空引用
** 3. 分离并处理有 __gc 元方法的用户数据
** 4. 重复调用垃圾回收元方法直到全部处理完成
** 5. 调用用户自定义的关闭函数
** 6. 执行最终的状态清理和内存释放
**
** 参数说明：
** @param L - lua_State*：要关闭的 Lua 状态（可以是任何线程）
**
** 返回值：无
**
** 算法复杂度：O(n) 时间（n 为所有对象总数），释放所有内存
**
** 注意事项：
** - 调用后所有相关的 lua_State 指针都无效
** - 元方法执行期间可能产生新的需要处理的对象
** - 此函数是 Lua 虚拟机生命周期的终点
*/
LUA_API void lua_close(lua_State *L)
{
    // 第一阶段：确保操作主线程
    // 只有主线程可以被关闭，协程会被自动清理
    L = G(L)->mainthread;
    lua_lock(L);

    // 第二阶段：关闭所有上值
    // 上值可能引用栈上的变量，必须在清理前关闭
    luaF_close(L, L->stack);

    // 第三阶段：分离有元方法的用户数据
    // 将有 __gc 元方法的用户数据分离到特殊链表中
    luaC_separateudata(L, 1);

    // 第四阶段：准备元方法调用环境
    // 元方法执行期间不使用错误函数，避免递归错误
    L->errfunc = 0;

    // 第五阶段：重复调用垃圾回收元方法
    // 循环执行直到所有 __gc 元方法都被成功调用
    do
    {
        // 重置执行环境到初始状态
        L->ci = L->base_ci;
        L->base = L->top = L->ci->base;
        L->nCcalls = L->baseCcalls = 0;

        // 在保护模式下调用所有垃圾回收元方法
        // 如果元方法出错，会重置状态并重试
    } while (luaD_rawrunprotected(L, callallgcTM, NULL) != 0);

    // 第六阶段：验证清理完整性
    // 确保所有用户数据的 __gc 元方法都已被处理
    lua_assert(G(L)->tmudata == NULL);

    // 第七阶段：调用用户清理函数
    // 允许用户在最终清理前执行自定义逻辑
    luai_userstateclose(L);

    // 第八阶段：执行最终清理
    // 释放所有剩余资源和内存
    close_state(L);
}

