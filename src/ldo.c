/**
 * @file ldo.c
 * @brief Lua执行控制和错误处理系统：函数调用、异常处理、协程管理
 *
 * 详细说明：
 * 本文件实现了Lua的执行控制系统，包括函数调用栈管理、异常处理机制、
 * 协程切换控制等核心功能。这是Lua虚拟机执行控制的核心模块。
 *
 * 核心功能：
 * 1. 异常处理：基于setjmp/longjmp的异常处理机制
 * 2. 栈管理：动态调整执行栈和调用信息栈
 * 3. 函数调用：管理函数调用的完整生命周期
 * 4. 协程控制：协程创建、切换和销毁
 * 5. 错误恢复：错误发生时的状态恢复和清理
 * 6. 保护执行：在受保护环境中执行代码
 *
 * 设计特色：
 * - 异常安全：完整的异常处理和状态恢复机制
 * - 栈动态管理：根据需要动态扩展执行栈
 * - 协程支持：轻量级协程的底层实现
 * - 错误传播：结构化的错误传播和处理
 *
 * 异常处理机制：
 * 使用C标准库的setjmp/longjmp实现结构化异常处理：
 * - setjmp：设置异常捕获点
 * - longjmp：跳转到异常处理点
 * - 异常链：支持嵌套的异常处理
 *
 * 栈管理策略：
 * - 执行栈：存储Lua值的栈空间
 * - 调用信息栈：存储函数调用信息
 * - 动态扩展：根据需要自动扩展栈空间
 * - 指针修正：栈重分配后的指针更新
 *
 * 技术亮点：
 * - 零开销异常：正常执行时无性能损失
 * - 栈溢出保护：防止无限递归导致的栈溢出
 * - 内存安全：严格的内存管理和边界检查
 * - 协程切换：高效的协程上下文切换
 *
 * 应用场景：
 * - 函数调用：管理所有Lua函数调用
 * - 错误处理：处理运行时错误和异常
 * - 协程操作：支持协程的创建和切换
 * - 保护执行：在安全环境中执行代码
 *
 * 性能考虑：
 * - 快速路径：正常执行的高效路径
 * - 异常路径：异常处理的完整性
 * - 内存局部性：优化栈访问模式
 * - 缓存友好：减少内存分配和释放
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2012-01-18
 * @since Lua 5.0
 * @see lstate.h, lvm.h, lfunc.h
 */

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define ldo_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"

// ============================================================================
// 异常处理数据结构
// ============================================================================

/**
 * @brief 长跳转缓冲区结构（异常处理链表节点）
 *
 * 详细说明：
 * 这个结构实现了异常处理的链表节点，支持嵌套的异常处理。
 * 每个节点包含跳转缓冲区和错误状态信息。
 *
 * 字段说明：
 * - previous：指向前一个异常处理节点，形成链表
 * - b：setjmp/longjmp使用的跳转缓冲区
 * - status：volatile修饰的错误状态码，防止编译器优化
 *
 * 链表结构：
 * 异常处理节点形成一个栈式的链表结构，支持嵌套的
 * 异常处理和正确的异常传播。
 *
 * volatile关键字：
 * status字段使用volatile修饰，确保在setjmp/longjmp
 * 跳转过程中值的正确性。
 */
struct lua_longjmp {
    struct lua_longjmp *previous;  // 前一个异常处理节点
    luai_jmpbuf b;                 // 跳转缓冲区
    volatile int status;           // 错误状态码（volatile防止优化）
};

/**
 * @brief 设置错误对象到栈上
 * @param L Lua状态机指针
 * @param errcode 错误代码
 * @param oldtop 原始栈顶位置
 *
 * 详细说明：
 * 这个函数根据错误类型在指定位置设置相应的错误对象。
 * 它是错误处理机制的重要组成部分，确保错误信息的正确传递。
 *
 * 错误类型处理：
 * 1. LUA_ERRMEM：内存错误，设置预定义的内存错误消息
 * 2. LUA_ERRERR：错误处理中的错误，设置特殊错误消息
 * 3. LUA_ERRSYNTAX/LUA_ERRRUN：语法/运行时错误，使用栈顶的错误消息
 *
 * 内存错误处理：
 * 对于内存错误，使用预定义的字面量字符串，避免在内存
 * 不足时再次分配内存导致的问题。
 *
 * 错误嵌套处理：
 * LUA_ERRERR表示在错误处理过程中又发生了错误，这种情况
 * 需要特殊处理以避免无限递归。
 *
 * 栈管理：
 * 函数会调整栈顶位置，确保错误对象位于正确的栈位置，
 * 便于后续的错误处理流程。
 *
 * 错误消息来源：
 * - 内存错误：使用MEMERRMSG常量
 * - 嵌套错误：使用固定的错误消息
 * - 运行时错误：使用当前栈顶的错误对象
 *
 * @pre L必须是有效的Lua状态机，oldtop必须是有效的栈位置
 * @post 错误对象被设置到oldtop位置，栈顶调整为oldtop+1
 *
 * @note 这是错误处理流程的关键函数
 * @see luaD_throw(), resetstack()
 */
void luaD_seterrorobj(lua_State *L, int errcode, StkId oldtop) {
    switch (errcode) {
        case LUA_ERRMEM: {
            // 内存错误：使用预定义字面量，避免内存分配
            setsvalue2s(L, oldtop, luaS_newliteral(L, MEMERRMSG));
            break;
        }
        case LUA_ERRERR: {
            // 错误处理中的错误：使用固定消息
            setsvalue2s(L, oldtop, luaS_newliteral(L, "error in error handling"));
            break;
        }
        case LUA_ERRSYNTAX:
        case LUA_ERRRUN: {
            // 语法/运行时错误：使用栈顶的错误消息
            setobjs2s(L, oldtop, L->top - 1);
            break;
        }
    }
    L->top = oldtop + 1;  // 调整栈顶位置
}


/**
 * @brief 恢复栈限制（处理栈溢出后的恢复）
 * @param L Lua状态机指针
 *
 * 详细说明：
 * 这个函数在栈溢出处理后恢复正常的栈限制。它检查调用信息栈
 * 是否发生了溢出，如果可能的话尝试恢复到正常大小。
 *
 * 栈溢出检测：
 * 通过比较当前调用信息栈大小与最大允许调用数来检测溢出。
 * 如果超过了LUAI_MAXCALLS，说明发生了栈溢出。
 *
 * 恢复策略：
 * 如果当前实际使用的调用信息数量加1小于最大限制，
 * 则可以安全地将栈大小恢复到正常限制。
 *
 * 安全检查：
 * 使用断言确保栈的一致性状态，验证栈大小计算的正确性。
 *
 * 内存管理：
 * 通过luaD_reallocCI重新分配调用信息栈到合适的大小，
 * 避免长期占用过多内存。
 *
 * @pre L必须是有效的Lua状态机
 * @post 如果可能，调用信息栈被恢复到正常大小
 *
 * @note 这是栈溢出恢复机制的一部分
 * @see luaD_reallocCI(), resetstack()
 */
static void restore_stack_limit(lua_State *L) {
    // 验证栈状态的一致性
    lua_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK - 1);

    if (L->size_ci > LUAI_MAXCALLS) {
        // 计算当前实际使用的调用信息数量
        int inuse = cast_int(L->ci - L->base_ci);
        if (inuse + 1 < LUAI_MAXCALLS) {
            // 可以安全恢复到正常大小
            luaD_reallocCI(L, LUAI_MAXCALLS);
        }
    }
}

/**
 * @brief 重置执行栈到初始状态
 * @param L Lua状态机指针
 * @param status 错误状态码
 *
 * 详细说明：
 * 这个函数将Lua状态机的执行栈重置到初始状态，用于错误恢复。
 * 它清理所有执行状态并设置错误对象。
 *
 * 重置操作：
 * 1. 调用信息：重置到基础调用信息
 * 2. 栈基址：重置到基础调用的栈基址
 * 3. 上值关闭：关闭所有待关闭的上值
 * 4. 错误对象：设置相应的错误对象
 * 5. 调用计数：重置C函数调用计数
 * 6. 钩子状态：重新允许钩子函数
 * 7. 栈限制：恢复正常的栈限制
 * 8. 错误函数：清除错误处理函数
 * 9. 异常跳转：清除异常跳转点
 *
 * 上值处理：
 * 调用luaF_close关闭所有在当前栈基址之上的上值，
 * 确保资源的正确清理。
 *
 * 状态一致性：
 * 重置所有相关的状态字段，确保虚拟机回到一致的状态，
 * 可以安全地继续执行或退出。
 *
 * 错误传播：
 * 通过luaD_seterrorobj设置错误对象，确保错误信息
 * 能够正确传递给上层处理代码。
 *
 * @pre L必须是有效的Lua状态机
 * @post 执行栈被重置到初始状态，错误对象被设置
 *
 * @note 这是错误恢复的核心函数
 * @see luaD_seterrorobj(), luaF_close(), restore_stack_limit()
 */
static void resetstack(lua_State *L, int status) {
    L->ci = L->base_ci;                    // 重置到基础调用信息
    L->base = L->ci->base;                 // 重置栈基址
    luaF_close(L, L->base);                // 关闭待关闭的上值
    luaD_seterrorobj(L, status, L->base);  // 设置错误对象
    L->nCcalls = L->baseCcalls;            // 重置C调用计数
    L->allowhook = 1;                      // 重新允许钩子
    restore_stack_limit(L);                // 恢复栈限制
    L->errfunc = 0;                        // 清除错误处理函数
    L->errorJmp = NULL;                    // 清除异常跳转点
}


/**
 * @brief 抛出Lua异常
 * @param L Lua状态机指针
 * @param errcode 错误代码
 *
 * 详细说明：
 * 这个函数抛出Lua异常，是Lua异常处理机制的核心。它根据当前
 * 是否有异常处理器来决定异常的处理方式。
 *
 * 异常处理路径：
 * 1. 有异常处理器：使用longjmp跳转到异常处理点
 * 2. 无异常处理器：调用恐慌函数或直接退出程序
 *
 * 结构化异常：
 * 如果存在errorJmp（异常跳转点），设置错误状态并使用
 * LUAI_THROW宏执行longjmp跳转到异常处理代码。
 *
 * 恐慌处理：
 * 如果没有异常处理器但有恐慌函数，先重置栈状态，
 * 解锁状态机，然后调用恐慌函数进行最后的清理。
 *
 * 程序终止：
 * 如果既没有异常处理器也没有恐慌函数，直接调用
 * exit(EXIT_FAILURE)终止程序。
 *
 * 状态设置：
 * 在调用恐慌函数前设置状态机的错误状态，确保
 * 恐慌函数能够获得正确的错误信息。
 *
 * 线程安全：
 * 在调用恐慌函数前解锁状态机，避免死锁问题。
 *
 * @pre L必须是有效的Lua状态机，errcode必须是有效的错误代码
 * @post 函数不会正常返回，要么跳转到异常处理点，要么程序终止
 *
 * @note 这是Lua异常处理的核心机制
 * @see luaD_rawrunprotected(), resetstack()
 */
void luaD_throw(lua_State *L, int errcode) {
    if (L->errorJmp) {
        // 有异常处理器：设置错误状态并跳转
        L->errorJmp->status = errcode;
        LUAI_THROW(L, L->errorJmp);
    } else {
        // 无异常处理器：设置状态并处理恐慌
        L->status = cast_byte(errcode);
        if (G(L)->panic) {
            // 有恐慌函数：重置栈并调用恐慌函数
            resetstack(L, errcode);
            lua_unlock(L);
            G(L)->panic(L);
        }
        // 最后手段：直接退出程序
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief 在受保护环境中执行函数
 * @param L Lua状态机指针
 * @param f 要执行的函数指针
 * @param ud 传递给函数的用户数据
 * @return 执行状态：0表示成功，非0表示错误代码
 *
 * 详细说明：
 * 这个函数在受保护的环境中执行指定函数，捕获可能发生的异常。
 * 它是Lua异常处理机制的基础，提供了结构化的异常捕获。
 *
 * 保护机制：
 * 1. 创建新的异常处理节点
 * 2. 将其链接到异常处理链表
 * 3. 设置setjmp跳转点
 * 4. 执行目标函数
 * 5. 恢复原异常处理器
 * 6. 返回执行状态
 *
 * 异常链表：
 * 通过previous字段维护异常处理器的链表结构，
 * 支持嵌套的保护执行和正确的异常传播。
 *
 * setjmp/longjmp：
 * 使用LUAI_TRY宏封装setjmp调用，如果函数执行过程中
 * 发生异常，longjmp会跳转回这里。
 *
 * 状态管理：
 * 初始化异常节点的状态为0（成功），如果发生异常，
 * luaD_throw会设置相应的错误代码。
 *
 * 异常传播：
 * 函数执行完毕后恢复原来的异常处理器，确保异常
 * 处理链表的正确性。
 *
 * 零开销异常：
 * 在正常执行路径上，异常处理的开销很小，主要是
 * 设置和恢复异常处理器。
 *
 * @pre L必须是有效的Lua状态机，f必须是有效的函数指针
 * @post 函数被执行，异常处理器被正确恢复
 *
 * @note 这是所有保护执行的基础函数
 * @see luaD_throw(), LUAI_TRY宏
 */
int luaD_rawrunprotected(lua_State *L, Pfunc f, void *ud) {
    struct lua_longjmp lj;
    lj.status = 0;                    // 初始化为成功状态
    lj.previous = L->errorJmp;        // 链接到异常处理链表
    L->errorJmp = &lj;                // 设置当前异常处理器

    LUAI_TRY(L, &lj,
        (*f)(L, ud);                  // 在保护环境中执行函数
    );

    L->errorJmp = lj.previous;        // 恢复原异常处理器
    return lj.status;                 // 返回执行状态
}




// ============================================================================
// 栈管理和动态调整
// ============================================================================

/**
 * @brief 修正栈重分配后的所有指针
 * @param L Lua状态机指针
 * @param oldstack 原始栈的起始地址
 *
 * 详细说明：
 * 当执行栈被重新分配后，所有指向栈内元素的指针都需要更新。
 * 这个函数负责修正所有相关的指针，确保它们指向新栈中的正确位置。
 *
 * 需要修正的指针：
 * 1. 栈顶指针：L->top
 * 2. 栈基址指针：L->base
 * 3. 开放上值：所有开放上值的值指针
 * 4. 调用信息：所有调用信息中的栈指针
 *
 * 指针修正算法：
 * 新指针 = (旧指针 - 旧栈基址) + 新栈基址
 * 这个公式计算出指针在栈中的偏移量，然后加上新栈的基址。
 *
 * 开放上值处理：
 * 遍历所有开放上值的链表，更新每个上值的值指针。
 * 开放上值是指向栈中变量的上值，栈重分配后必须更新。
 *
 * 调用信息处理：
 * 遍历从基础调用信息到当前调用信息的所有调用信息，
 * 更新其中的栈顶、栈基址和函数指针。
 *
 * 内存安全：
 * 这个函数确保栈重分配后所有指针的有效性，
 * 防止悬空指针导致的内存错误。
 *
 * 性能考虑：
 * 虽然需要遍历多个数据结构，但这是栈重分配的必要开销，
 * 相对于栈重分配的频率，这个开销是可接受的。
 *
 * @pre L必须是有效的Lua状态机，oldstack必须是有效的旧栈地址
 * @post 所有栈相关指针被正确更新到新栈位置
 *
 * @note 这是栈重分配的关键步骤
 * @see luaD_reallocstack(), luaD_reallocCI()
 */
static void correctstack(lua_State *L, TValue *oldstack) {
    CallInfo *ci;
    GCObject *up;

    // 修正栈顶指针
    L->top = (L->top - oldstack) + L->stack;

    // 修正所有开放上值的值指针
    for (up = L->openupval; up != NULL; up = up->gch.next) {
        gco2uv(up)->v = (gco2uv(up)->v - oldstack) + L->stack;
    }

    // 修正所有调用信息中的栈指针
    for (ci = L->base_ci; ci <= L->ci; ci++) {
        ci->top = (ci->top - oldstack) + L->stack;
        ci->base = (ci->base - oldstack) + L->stack;
        ci->func = (ci->func - oldstack) + L->stack;
    }

    // 修正当前栈基址指针
    L->base = (L->base - oldstack) + L->stack;
}


/**
 * @brief 重新分配执行栈
 * @param L Lua状态机指针
 * @param newsize 新的栈大小
 *
 * 详细说明：
 * 这个函数重新分配Lua的执行栈，用于栈空间不足时的动态扩展。
 * 它处理内存重分配和指针修正的完整流程。
 *
 * 大小计算：
 * 实际分配大小 = 请求大小 + 1 + EXTRA_STACK
 * - +1：为栈顶留出空间
 * - EXTRA_STACK：额外的安全空间，防止栈溢出
 *
 * 重分配过程：
 * 1. 保存旧栈地址：用于后续的指针修正
 * 2. 验证栈状态：确保栈的一致性
 * 3. 重新分配内存：使用luaM_reallocvector
 * 4. 更新栈信息：设置新的栈大小和栈末尾
 * 5. 修正指针：调用correctstack修正所有指针
 *
 * 内存管理：
 * 使用Lua的内存管理器进行重分配，支持自定义的内存分配器。
 * 如果内存分配失败，会抛出内存错误异常。
 *
 * 栈布局：
 * [栈底] ... [有效栈空间] ... [栈顶] [安全空间] [栈末尾]
 *
 * 安全检查：
 * 使用断言验证栈状态的一致性，确保重分配前的状态正确。
 *
 * 指针修正：
 * 栈重分配可能改变栈的内存地址，因此需要修正所有
 * 指向栈内元素的指针。
 *
 * 性能考虑：
 * - 内存复制：重分配可能涉及大量内存复制
 * - 指针修正：需要遍历多个数据结构
 * - 分配策略：通常采用倍增策略减少重分配频率
 *
 * @pre L必须是有效的Lua状态机，newsize > 0
 * @post 栈被重新分配到指定大小，所有指针被正确修正
 *
 * @note 这是栈动态管理的核心函数
 * @see correctstack(), luaD_growstack()
 */
void luaD_reallocstack(lua_State *L, int newsize) {
    TValue *oldstack = L->stack;                    // 保存旧栈地址
    int realsize = newsize + 1 + EXTRA_STACK;      // 计算实际分配大小

    // 验证栈状态的一致性
    lua_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK - 1);

    // 重新分配栈内存
    luaM_reallocvector(L, L->stack, L->stacksize, realsize, TValue);

    // 更新栈信息
    L->stacksize = realsize;                       // 设置新的栈大小
    L->stack_last = L->stack + newsize;            // 设置栈末尾位置

    // 修正所有栈相关指针
    correctstack(L, oldstack);
}


/**
 * @brief 重新分配调用信息栈
 * @param L Lua状态机指针
 * @param newsize 新的调用信息栈大小
 *
 * 详细说明：
 * 这个函数重新分配调用信息栈，用于函数调用深度超出当前容量时。
 * 调用信息栈存储每个函数调用的上下文信息。
 *
 * 重分配过程：
 * 1. 保存旧的调用信息基址
 * 2. 重新分配调用信息数组
 * 3. 更新调用信息栈大小
 * 4. 修正当前调用信息指针
 * 5. 设置调用信息栈末尾
 *
 * 指针修正：
 * 由于调用信息数组可能被重新分配到不同的内存位置，
 * 需要修正当前调用信息指针的位置。
 *
 * 计算公式：
 * 新指针 = (旧指针 - 旧基址) + 新基址
 *
 * 内存管理：
 * 使用luaM_reallocvector进行内存重分配，支持垃圾回收
 * 和自定义内存分配器。
 *
 * @pre L必须是有效的Lua状态机，newsize > 0
 * @post 调用信息栈被重新分配，相关指针被正确修正
 *
 * @note 这是函数调用栈管理的核心函数
 * @see growCI(), luaD_reallocstack()
 */
void luaD_reallocCI(lua_State *L, int newsize) {
    CallInfo *oldci = L->base_ci;                   // 保存旧的基址
    luaM_reallocvector(L, L->base_ci, L->size_ci, newsize, CallInfo);
    L->size_ci = newsize;                           // 更新大小
    L->ci = (L->ci - oldci) + L->base_ci;          // 修正当前指针
    L->end_ci = L->base_ci + L->size_ci - 1;       // 设置末尾指针
}

/**
 * @brief 增长执行栈
 * @param L Lua状态机指针
 * @param n 需要的额外栈空间
 *
 * 详细说明：
 * 这个函数根据需要增长执行栈，采用不同的增长策略来平衡
 * 内存使用和性能。
 *
 * 增长策略：
 * 1. 如果需要的空间不大：采用倍增策略（2倍当前大小）
 * 2. 如果需要的空间很大：采用按需分配（当前大小+需要的空间）
 *
 * 倍增策略优势：
 * - 减少重分配频率
 * - 摊销时间复杂度为O(1)
 * - 适合逐步增长的场景
 *
 * 按需分配优势：
 * - 避免过度分配内存
 * - 适合一次性需要大量空间的场景
 *
 * 阈值判断：
 * 以当前栈大小为阈值，小于等于阈值使用倍增，
 * 大于阈值使用按需分配。
 *
 * @pre L必须是有效的Lua状态机，n > 0
 * @post 栈被扩展到足够容纳n个额外元素
 *
 * @note 这是栈自动增长的入口函数
 * @see luaD_reallocstack(), luaD_checkstack()
 */
void luaD_growstack(lua_State *L, int n) {
    if (n <= L->stacksize) {
        // 需要的空间不大，使用倍增策略
        luaD_reallocstack(L, 2 * L->stacksize);
    } else {
        // 需要的空间很大，使用按需分配
        luaD_reallocstack(L, L->stacksize + n);
    }
}

/**
 * @brief 增长调用信息栈
 * @param L Lua状态机指针
 * @return 新的调用信息指针
 *
 * 详细说明：
 * 这个函数在调用信息栈空间不足时进行扩展，并返回新的
 * 调用信息指针供函数调用使用。
 *
 * 溢出检测：
 * 首先检查当前调用信息栈是否已经超过最大限制，
 * 如果超过则抛出错误异常。
 *
 * 扩展策略：
 * 使用倍增策略扩展调用信息栈，平衡内存使用和性能。
 *
 * 二次检查：
 * 扩展后再次检查是否超过限制，如果超过则抛出栈溢出错误。
 * 这种情况通常发生在接近限制时的扩展。
 *
 * 错误处理：
 * - LUA_ERRERR：调用信息栈已经溢出
 * - "stack overflow"：扩展后仍然超过限制
 *
 * 返回值：
 * 返回递增后的调用信息指针，指向新分配的调用信息。
 *
 * @pre L必须是有效的Lua状态机
 * @post 调用信息栈被扩展，返回新的调用信息指针
 *
 * @note 这是函数调用时的栈管理函数
 * @see luaD_reallocCI(), inc_ci宏
 */
static CallInfo *growCI(lua_State *L) {
    if (L->size_ci > LUAI_MAXCALLS) {
        // 调用信息栈已经溢出
        luaD_throw(L, LUA_ERRERR);
    } else {
        // 扩展调用信息栈
        luaD_reallocCI(L, 2 * L->size_ci);
        if (L->size_ci > LUAI_MAXCALLS) {
            // 扩展后仍然超过限制
            luaG_runerror(L, "stack overflow");
        }
    }
    return ++L->ci;  // 返回新的调用信息指针
}


// ============================================================================
// 调试钩子和函数调用管理
// ============================================================================

/**
 * @brief 调用调试钩子函数
 * @param L Lua状态机指针
 * @param event 钩子事件类型
 * @param line 当前行号
 *
 * 详细说明：
 * 这个函数负责调用用户设置的调试钩子函数，是Lua调试系统的核心。
 * 它在特定事件发生时被调用，为调试器提供执行状态信息。
 *
 * 钩子事件类型：
 * - LUA_HOOKCALL：函数调用事件
 * - LUA_HOOKRET：函数返回事件
 * - LUA_HOOKTAILRET：尾调用返回事件
 * - LUA_HOOKLINE：行执行事件
 * - LUA_HOOKCOUNT：指令计数事件
 *
 * 调用条件：
 * 只有在存在钩子函数且允许钩子调用时才执行。
 * allowhook标志防止钩子函数内部的递归调用。
 *
 * 状态保存：
 * 在调用钩子前保存栈状态，调用后恢复，确保钩子调用
 * 不会影响正常的执行状态。
 *
 * 调试信息构建：
 * - event：事件类型
 * - currentline：当前行号
 * - i_ci：调用信息索引（尾调用时为0）
 *
 * 栈管理：
 * 确保有足够的栈空间供钩子函数使用，设置临时的栈顶。
 *
 * 线程安全：
 * 在调用钩子前解锁状态机，调用后重新加锁，
 * 允许钩子函数进行某些操作。
 *
 * 递归保护：
 * 通过allowhook标志防止钩子函数内部再次触发钩子，
 * 避免无限递归。
 *
 * 状态恢复：
 * 钩子调用完成后恢复原始的栈状态，确保执行的连续性。
 *
 * @pre L必须是有效的Lua状态机
 * @post 如果存在钩子函数，则被调用，栈状态被正确恢复
 *
 * @note 这是Lua调试系统的核心接口
 * @see lua_sethook(), lua_Debug结构
 */
void luaD_callhook(lua_State *L, int event, int line) {
    lua_Hook hook = L->hook;
    if (hook && L->allowhook) {
        // 保存当前栈状态
        ptrdiff_t top = savestack(L, L->top);
        ptrdiff_t ci_top = savestack(L, L->ci->top);

        // 构建调试信息
        lua_Debug ar;
        ar.event = event;
        ar.currentline = line;
        if (event == LUA_HOOKTAILRET) {
            ar.i_ci = 0;  // 尾调用没有调试信息
        } else {
            ar.i_ci = cast_int(L->ci - L->base_ci);
        }

        // 确保栈空间并设置临时栈顶
        luaD_checkstack(L, LUA_MINSTACK);
        L->ci->top = L->top + LUA_MINSTACK;
        lua_assert(L->ci->top <= L->stack_last);

        // 禁用钩子递归并解锁状态机
        L->allowhook = 0;
        lua_unlock(L);

        // 调用用户钩子函数
        (*hook)(L, &ar);

        // 重新加锁并验证钩子状态
        lua_lock(L);
        lua_assert(!L->allowhook);

        // 恢复钩子允许状态和栈状态
        L->allowhook = 1;
        L->ci->top = restorestack(L, ci_top);
        L->top = restorestack(L, top);
    }
}


/**
 * @brief 调整可变参数函数的参数布局
 * @param L Lua状态机指针
 * @param p 函数原型指针
 * @param actual 实际传入的参数数量
 * @return 调整后的栈基址
 *
 * 详细说明：
 * 这个函数处理可变参数函数的参数布局调整，确保固定参数和
 * 可变参数的正确排列，支持旧式可变参数的兼容性。
 *
 * 参数调整过程：
 * 1. 补齐缺失的固定参数（设为nil）
 * 2. 处理旧式可变参数兼容性（创建arg表）
 * 3. 重新排列参数到正确位置
 * 4. 设置新的栈基址
 *
 * 固定参数处理：
 * 如果实际参数少于固定参数数量，用nil补齐缺失的参数。
 * 这确保函数能够正确访问所有声明的固定参数。
 *
 * 旧式可变参数兼容性：
 * 在LUA_COMPAT_VARARG模式下，为兼容旧版本创建arg表：
 * - 包含所有额外参数
 * - 设置n字段为额外参数数量
 * - 支持旧式的可变参数访问方式
 *
 * 参数重排算法：
 * 1. 计算固定参数的起始位置
 * 2. 将固定参数复制到新位置
 * 3. 清空原位置（设为nil）
 * 4. 如果有arg表，将其放在参数末尾
 *
 * 内存管理：
 * - 检查垃圾回收：创建arg表前触发GC检查
 * - 检查栈空间：确保有足够空间
 * - 新对象标记：确保arg表的GC标记正确
 *
 * 栈布局变化：
 * 调整前：[func][arg1][arg2]...[argN][extra1][extra2]...
 * 调整后：[func][param1][param2]...[paramM][arg_table]
 *
 * 性能考虑：
 * - 最小化参数复制：只复制必要的固定参数
 * - 延迟表创建：只在需要时创建arg表
 * - 栈空间优化：重用现有栈空间
 *
 * 兼容性设计：
 * 通过条件编译支持旧式可变参数，在保持新功能的同时
 * 维护向后兼容性。
 *
 * @pre L必须是有效的Lua状态机，p必须是有效的函数原型
 * @post 参数被正确调整，返回新的栈基址
 *
 * @note 这是可变参数函数调用的关键步骤
 * @see luaD_precall(), Proto结构
 */
static StkId adjust_varargs(lua_State *L, Proto *p, int actual) {
    int i;
    int nfixargs = p->numparams;    // 固定参数数量
    Table *htab = NULL;
    StkId base, fixed;

    // 补齐缺失的固定参数（设为nil）
    for (; actual < nfixargs; ++actual) {
        setnilvalue(L->top++);
    }

#if defined(LUA_COMPAT_VARARG)
    // 旧式可变参数兼容性处理
    if (p->is_vararg & VARARG_NEEDSARG) {
        int nvar = actual - nfixargs;    // 额外参数数量
        lua_assert(p->is_vararg & VARARG_HASARG);

        // 准备创建arg表
        luaC_checkGC(L);                 // 检查垃圾回收
        luaD_checkstack(L, p->maxstacksize);  // 检查栈空间

        // 创建arg表并填充额外参数
        htab = luaH_new(L, nvar, 1);
        for (i = 0; i < nvar; i++) {
            setobj2n(L, luaH_setnum(L, htab, i + 1), L->top - nvar + i);
        }
        // 设置n字段为额外参数数量
        setnvalue(luaH_setstr(L, htab, luaS_newliteral(L, "n")), cast_num(nvar));
    }
#endif

    // 重新排列参数
    fixed = L->top - actual;        // 固定参数的起始位置
    base = L->top;                  // 新的栈基址

    // 将固定参数复制到正确位置
    for (i = 0; i < nfixargs; i++) {
        setobjs2s(L, L->top++, fixed + i);
        setnilvalue(fixed + i);     // 清空原位置
    }

    // 如果有arg表，将其放在参数末尾
    if (htab) {
        sethvalue(L, L->top++, htab);
        lua_assert(iswhite(obj2gco(htab)));  // 验证GC标记
    }

    return base;
}


/**
 * @brief 尝试调用对象的__call元方法
 * @param L Lua状态机指针
 * @param func 要调用的对象（非函数）
 * @return 调整后的函数位置
 *
 * 详细说明：
 * 当尝试调用一个非函数对象时，这个函数查找并设置其__call元方法，
 * 实现Lua的元方法调用机制。
 *
 * 元方法查找：
 * 使用luaT_gettmbyobj查找对象的__call元方法。
 * 如果对象没有__call元方法或元方法不是函数，抛出类型错误。
 *
 * 栈调整过程：
 * 1. 保存函数位置：防止栈重分配导致指针失效
 * 2. 验证元方法：确保__call元方法是可调用的函数
 * 3. 插入空位：在函数位置插入一个空位
 * 4. 移动参数：将所有参数向后移动一位
 * 5. 设置元方法：将__call元方法放在函数位置
 * 6. 恢复指针：恢复可能失效的函数指针
 *
 * 栈布局变化：
 * 调用前：[obj][arg1][arg2]...[argN]
 * 调用后：[__call][obj][arg1][arg2]...[argN]
 *
 * 这样，原始对象成为__call元方法的第一个参数。
 *
 * 指针安全：
 * 由于栈操作可能触发栈重分配，使用savestack/restorestack
 * 机制保护函数指针的有效性。
 *
 * 错误处理：
 * 如果对象没有有效的__call元方法，抛出类型错误，
 * 提供清晰的错误信息。
 *
 * 元方法语义：
 * __call元方法的调用语义：
 * obj(arg1, arg2, ...) 等价于 obj.__call(obj, arg1, arg2, ...)
 *
 * 性能考虑：
 * - 元方法查找：使用高效的元方法查找机制
 * - 栈操作：最小化栈元素的移动
 * - 指针保护：避免不必要的指针重计算
 *
 * 使用场景：
 * - 可调用对象：表、用户数据等实现函数调用语义
 * - 函数代理：通过元方法实现函数调用的拦截
 * - 对象方法：实现面向对象的方法调用
 *
 * @pre L必须是有效的Lua状态机，func必须指向非函数对象
 * @post 栈被调整为元方法调用格式，返回新的函数位置
 *
 * @note 这是Lua元方法系统的重要组成部分
 * @see luaT_gettmbyobj(), luaD_precall()
 */
static StkId tryfuncTM(lua_State *L, StkId func) {
    // 查找对象的__call元方法
    const TValue *tm = luaT_gettmbyobj(L, func, TM_CALL);
    StkId p;
    ptrdiff_t funcr = savestack(L, func);    // 保存函数位置

    // 验证元方法是否为函数
    if (!ttisfunction(tm)) {
        luaG_typeerror(L, func, "call");
    }

    // 将所有参数向后移动一位，为原对象腾出空间
    for (p = L->top; p > func; p--) {
        setobjs2s(L, p, p - 1);
    }
    incr_top(L);                             // 增加栈顶

    // 恢复可能失效的函数指针
    func = restorestack(L, funcr);

    // 将__call元方法设置为新的函数
    setobj2s(L, func, tm);

    return func;
}



/**
 * @brief 递增调用信息指针的宏
 *
 * 详细说明：
 * 这个宏负责安全地递增调用信息指针，在需要时自动扩展
 * 调用信息栈。它是函数调用管理的核心机制。
 *
 * 工作原理：
 * 1. 检查是否到达调用信息栈末尾
 * 2. 如果到达末尾，调用growCI扩展栈
 * 3. 否则，直接递增调用信息指针
 *
 * 调试支持：
 * condhardstacktests宏在调试模式下强制重分配，
 * 用于测试栈重分配的正确性。
 *
 * 性能优化：
 * 大多数情况下只是简单的指针递增，开销很小。
 * 只有在栈满时才进行昂贵的扩展操作。
 */
#define inc_ci(L) \
  ((L->ci == L->end_ci) ? growCI(L) : \
   (condhardstacktests(luaD_reallocCI(L, L->size_ci)), ++L->ci))

/**
 * @brief 函数预调用处理
 * @param L Lua状态机指针
 * @param func 要调用的函数
 * @param nresults 期望的返回值数量
 * @return PCRLUA表示Lua函数，PCRC表示C函数
 *
 * 详细说明：
 * 这个函数处理函数调用的预备工作，包括参数调整、栈设置、
 * 调用信息初始化等。它是函数调用机制的核心。
 *
 * 调用类型检查：
 * 首先检查调用对象是否为函数，如果不是，尝试调用其
 * __call元方法。
 *
 * 函数类型分支：
 * 1. Lua函数：设置执行环境，准备字节码执行
 * 2. C函数：设置C调用环境，准备原生函数调用
 *
 * Lua函数调用准备：
 * - 检查栈空间：确保有足够空间执行函数
 * - 参数处理：处理固定参数和可变参数
 * - 调用信息：创建新的调用信息记录
 * - 执行环境：设置栈基址、栈顶、保存的PC
 * - 调试钩子：如果启用，调用函数调用钩子
 *
 * C函数调用准备：
 * - 栈空间检查：确保最小栈空间
 * - 调用信息：创建C函数调用信息
 * - 参数设置：设置函数和参数
 * - 直接调用：执行C函数并处理返回值
 *
 * 参数调整策略：
 * - 非可变参数：直接设置栈基址
 * - 可变参数：调用adjust_varargs调整参数布局
 *
 * 栈布局管理：
 * 精确控制栈的布局，确保函数能够正确访问参数
 * 和局部变量。
 *
 * 错误处理：
 * 处理各种调用错误，包括栈溢出、类型错误等。
 *
 * 性能考虑：
 * - 快速路径：常见情况的优化处理
 * - 延迟操作：只在需要时进行昂贵操作
 * - 栈管理：高效的栈空间管理
 *
 * @pre L必须是有效的Lua状态机，func必须指向有效对象
 * @post 函数调用环境被正确设置
 *
 * @note 这是函数调用机制的核心函数
 * @see luaD_call(), luaD_poscall(), adjust_varargs()
 */
int luaD_precall(lua_State *L, StkId func, int nresults) {
    LClosure *cl;
    ptrdiff_t funcr;

    // 检查调用对象是否为函数，如果不是则尝试元方法
    if (!ttisfunction(func)) {
        func = tryfuncTM(L, func);
    }

    funcr = savestack(L, func);                 // 保存函数位置
    cl = &clvalue(func)->l;                     // 获取闭包
    L->ci->savedpc = L->savedpc;                // 保存当前PC

    if (!cl->isC) {
        // Lua函数调用处理
        CallInfo *ci;
        StkId st, base;
        Proto *p = cl->p;                       // 获取函数原型

        luaD_checkstack(L, p->maxstacksize);    // 检查栈空间
        func = restorestack(L, funcr);          // 恢复函数指针

        if (!p->is_vararg) {
            // 非可变参数函数
            base = func + 1;
            // 调整栈顶，移除多余参数
            if (L->top > base + p->numparams) {
                L->top = base + p->numparams;
            }
        } else {
            // 可变参数函数
            int nargs = cast_int(L->top - func) - 1;
            base = adjust_varargs(L, p, nargs);
            func = restorestack(L, funcr);      // 恢复可能失效的指针
        }

        // 创建新的调用信息
        ci = inc_ci(L);
        ci->func = func;
        L->base = ci->base = base;
        ci->top = L->base + p->maxstacksize;
        lua_assert(ci->top <= L->stack_last);
        L->savedpc = p->code;                   // 设置程序计数器
        ci->tailcalls = 0;                      // 初始化尾调用计数
        ci->nresults = nresults;                // 设置期望返回值数量

        // 初始化局部变量为nil
        for (st = L->top; st < ci->top; st++) {
            setnilvalue(st);
        }
        L->top = ci->top;

        // 调用函数调用钩子
        if (L->hookmask & LUA_MASKCALL) {
            L->savedpc++;                       // 钩子假设PC已递增
            luaD_callhook(L, LUA_HOOKCALL, -1);
            L->savedpc--;                       // 恢复正确的PC
        }
        return PCRLUA;                          // 返回Lua函数标识
    } else {
        // C函数调用处理
        CallInfo *ci;
        int n;

        luaD_checkstack(L, LUA_MINSTACK);       // 确保最小栈空间
        ci = inc_ci(L);                         // 创建调用信息
        ci->func = restorestack(L, funcr);      // 设置函数
        L->base = ci->base = ci->func + 1;      // 设置栈基址
        ci->top = L->top + LUA_MINSTACK;        // 设置栈顶
        lua_assert(ci->top <= L->stack_last);
        ci->nresults = nresults;                // 设置期望返回值数量

        // 调用函数调用钩子
        if (L->hookmask & LUA_MASKCALL) {
            luaD_callhook(L, LUA_HOOKCALL, -1);
        }

        // 执行C函数
        lua_unlock(L);                          // 解锁状态机
        n = (*curr_func(L)->c.f)(L);           // 调用C函数
        lua_lock(L);                           // 重新加锁

        if (n < 0) {
            return PCRYIELD;                    // 函数让出
        } else {
            luaD_poscall(L, L->top - n);        // 处理返回值
            return PCRC;                        // 返回C函数标识
        }
    }
}


/**
 * @brief 调用返回钩子函数
 * @param L Lua状态机指针
 * @param firstResult 第一个返回值的位置
 * @return 调整后的第一个返回值位置
 *
 * 详细说明：
 * 这个函数在函数返回时调用相应的调试钩子，处理普通返回
 * 和尾调用返回的钩子调用。
 *
 * 钩子调用顺序：
 * 1. 调用普通返回钩子（LUA_HOOKRET）
 * 2. 如果是Lua函数且有尾调用，调用尾调用返回钩子
 *
 * 尾调用处理：
 * 对于每个尾调用，都会调用LUA_HOOKTAILRET钩子，
 * 确保调试器能够跟踪完整的调用链。
 *
 * 指针保护：
 * 使用savestack/restorestack保护返回值指针，
 * 防止钩子调用过程中的栈重分配影响。
 *
 * @pre L必须是有效的Lua状态机
 * @post 相应的返回钩子被调用，返回值指针被保护
 *
 * @note 这是调试系统的重要组成部分
 * @see luaD_callhook(), luaD_poscall()
 */
static StkId callrethooks(lua_State *L, StkId firstResult) {
    ptrdiff_t fr = savestack(L, firstResult);   // 保护返回值指针
    luaD_callhook(L, LUA_HOOKRET, -1);          // 调用返回钩子

    if (f_isLua(L->ci)) {
        // 处理Lua函数的尾调用返回钩子
        while ((L->hookmask & LUA_MASKRET) && L->ci->tailcalls--) {
            luaD_callhook(L, LUA_HOOKTAILRET, -1);
        }
    }

    return restorestack(L, fr);                 // 恢复返回值指针
}

/**
 * @brief 函数后调用处理（处理返回值）
 * @param L Lua状态机指针
 * @param firstResult 第一个返回值的位置
 * @return 是否需要调整栈顶（wanted - LUA_MULTRET）
 *
 * 详细说明：
 * 这个函数处理函数调用完成后的清理工作，包括返回值处理、
 * 调用信息清理、栈状态恢复等。
 *
 * 处理流程：
 * 1. 调用返回钩子（如果启用）
 * 2. 获取调用信息并递减调用栈
 * 3. 复制返回值到正确位置
 * 4. 补齐缺失的返回值（设为nil）
 * 5. 恢复调用者的执行状态
 *
 * 返回值处理：
 * - 复制实际返回值到函数位置开始的连续空间
 * - 如果返回值不足，用nil补齐
 * - 如果返回值过多，截断多余部分
 *
 * 状态恢复：
 * - 恢复调用者的栈基址
 * - 恢复调用者的程序计数器
 * - 调整栈顶到返回值末尾
 *
 * 调用信息管理：
 * 递减调用信息指针，返回到调用者的调用信息。
 *
 * 返回值语义：
 * - wanted > 0：期望固定数量的返回值
 * - wanted = LUA_MULTRET：接受所有返回值
 *
 * 栈布局变化：
 * 调用前：[caller_stack][func][args...]
 * 调用后：[caller_stack][result1][result2]...[resultN]
 *
 * @pre L必须是有效的Lua状态机，firstResult必须指向有效位置
 * @post 返回值被正确处理，调用者状态被恢复
 *
 * @note 这是函数调用机制的重要组成部分
 * @see luaD_precall(), luaD_call(), callrethooks()
 */
int luaD_poscall(lua_State *L, StkId firstResult) {
    StkId res;
    int wanted, i;
    CallInfo *ci;

    // 调用返回钩子（如果启用）
    if (L->hookmask & LUA_MASKRET) {
        firstResult = callrethooks(L, firstResult);
    }

    // 获取当前调用信息并递减调用栈
    ci = L->ci--;
    res = ci->func;                             // 返回值的目标位置
    wanted = ci->nresults;                      // 期望的返回值数量

    // 恢复调用者的执行状态
    L->base = (ci - 1)->base;                   // 恢复栈基址
    L->savedpc = (ci - 1)->savedpc;             // 恢复程序计数器

    // 复制返回值到正确位置
    for (i = wanted; i != 0 && firstResult < L->top; i--) {
        setobjs2s(L, res++, firstResult++);
    }

    // 补齐缺失的返回值（设为nil）
    while (i-- > 0) {
        setnilvalue(res++);
    }

    L->top = res;                               // 调整栈顶
    return (wanted - LUA_MULTRET);              // 返回调整标识
}


/**
 * @brief 执行函数调用（主要调用接口）
 * @param L Lua状态机指针
 * @param func 要调用的函数
 * @param nResults 期望的返回值数量
 *
 * 详细说明：
 * 这是Lua函数调用的主要接口，处理完整的函数调用流程，
 * 包括栈溢出检查、预调用处理、执行和垃圾回收。
 *
 * 调用流程：
 * 1. C栈溢出检查：防止无限递归
 * 2. 预调用处理：设置调用环境
 * 3. 函数执行：Lua函数或C函数
 * 4. 调用计数管理：维护调用深度
 * 5. 垃圾回收检查：调用后触发GC
 *
 * C栈溢出保护：
 * 通过nCcalls计数器跟踪C函数调用深度，防止：
 * - 普通栈溢出：达到LUAI_MAXCCALLS时报错
 * - 错误处理溢出：超过限制+12.5%时抛出错误异常
 *
 * 函数类型处理：
 * - Lua函数：调用luaV_execute执行字节码
 * - C函数：在luaD_precall中直接执行
 *
 * 调用计数管理：
 * 递增调用计数，执行完成后递减，确保调用深度的正确跟踪。
 *
 * 垃圾回收集成：
 * 函数调用完成后检查垃圾回收，及时回收不再使用的对象。
 *
 * 错误处理：
 * - C栈溢出：抛出运行时错误
 * - 错误处理溢出：抛出错误异常
 * - 其他错误：由底层函数处理
 *
 * 性能考虑：
 * - 快速路径：常见情况的优化处理
 * - 溢出检查：轻量级的计数器检查
 * - GC时机：在适当时机触发垃圾回收
 *
 * 递归保护：
 * 通过C调用计数器防止过深的递归调用，
 * 保护C栈不被耗尽。
 *
 * @pre L必须是有效的Lua状态机，func必须指向有效对象
 * @post 函数被执行，返回值在栈上，调用状态被正确维护
 *
 * @note 这是最常用的函数调用接口
 * @see luaD_precall(), luaD_poscall(), luaV_execute()
 */
void luaD_call(lua_State *L, StkId func, int nResults) {
    // C栈溢出检查
    if (++L->nCcalls >= LUAI_MAXCCALLS) {
        if (L->nCcalls == LUAI_MAXCCALLS) {
            luaG_runerror(L, "C stack overflow");
        } else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS >> 3))) {
            luaD_throw(L, LUA_ERRERR);          // 错误处理中的溢出
        }
    }

    // 执行函数调用
    if (luaD_precall(L, func, nResults) == PCRLUA) {
        luaV_execute(L, 1);                     // 执行Lua函数
    }
    // C函数在precall中已经执行完成

    L->nCcalls--;                               // 递减调用计数
    luaC_checkGC(L);                           // 检查垃圾回收
}


// ============================================================================
// 协程管理和控制
// ============================================================================

/**
 * @brief 协程恢复执行的内部函数
 * @param L Lua状态机指针
 * @param ud 用户数据（第一个参数的位置）
 *
 * 详细说明：
 * 这个函数处理协程的恢复执行，支持两种情况：
 * 新启动的协程和从yield恢复的协程。
 *
 * 协程状态分支：
 * 1. status == 0：新启动的协程
 * 2. status == LUA_YIELD：从yield恢复的协程
 *
 * 新协程启动：
 * - 验证协程状态：确保在基础调用信息且有参数
 * - 预调用处理：设置函数调用环境
 * - 如果是C函数：直接返回（C函数不能yield）
 *
 * 从yield恢复：
 * - 重置状态：将status从LUA_YIELD改为0
 * - C函数恢复：完成被中断的OP_CALL或OP_TAILCALL
 * - Lua函数恢复：恢复栈基址，继续执行
 *
 * 执行恢复：
 * 调用luaV_execute继续或开始执行，传入调用深度参数。
 *
 * C函数yield处理：
 * 当从C函数的yield恢复时，需要完成被中断的调用操作，
 * 这通过luaD_poscall处理返回值完成。
 *
 * Lua函数yield处理：
 * 对于Lua函数，只需恢复栈基址，执行引擎会从
 * 正确的位置继续执行。
 *
 * 调用深度：
 * 传递给执行引擎的调用深度确保正确的执行上下文。
 *
 * @pre L必须是有效的协程状态机
 * @post 协程被正确恢复执行
 *
 * @note 这是协程系统的核心恢复函数
 * @see lua_resume(), luaV_execute()
 */
static void resume(lua_State *L, void *ud) {
    StkId firstArg = cast(StkId, ud);           // 第一个参数位置
    CallInfo *ci = L->ci;

    if (L->status == 0) {
        // 新启动的协程
        lua_assert(ci == L->base_ci && firstArg > L->base);
        if (luaD_precall(L, firstArg - 1, LUA_MULTRET) != PCRLUA) {
            return;                             // C函数已执行完成
        }
    } else {
        // 从yield恢复的协程
        lua_assert(L->status == LUA_YIELD);
        L->status = 0;                          // 重置状态

        if (!f_isLua(ci)) {
            // 从C函数的yield恢复
            lua_assert(GET_OPCODE(*((ci - 1)->savedpc - 1)) == OP_CALL ||
                      GET_OPCODE(*((ci - 1)->savedpc - 1)) == OP_TAILCALL);
            if (luaD_poscall(L, firstArg)) {
                L->top = L->ci->top;            // 调整栈顶
            }
        } else {
            // 从Lua函数的yield恢复
            L->base = L->ci->base;              // 恢复栈基址
        }
    }

    // 继续或开始执行
    luaV_execute(L, cast_int(L->ci - L->base_ci));
}

/**
 * @brief 协程恢复错误处理
 * @param L Lua状态机指针
 * @param msg 错误消息
 * @return LUA_ERRRUN错误代码
 *
 * 详细说明：
 * 这个函数处理协程恢复时的错误情况，设置错误消息并
 * 返回相应的错误代码。
 *
 * 错误处理：
 * 1. 重置栈顶到调用基址
 * 2. 创建错误消息字符串
 * 3. 将错误消息推入栈
 * 4. 解锁状态机
 * 5. 返回运行时错误代码
 *
 * 栈状态：
 * 确保栈处于一致状态，错误消息位于栈顶。
 *
 * @pre L必须是有效的Lua状态机，msg必须是有效字符串
 * @post 错误消息被设置，返回错误代码
 *
 * @note 这是协程错误处理的辅助函数
 * @see lua_resume()
 */
static int resume_error(lua_State *L, const char *msg) {
    L->top = L->ci->base;                       // 重置栈顶
    setsvalue2s(L, L->top, luaS_new(L, msg));   // 创建错误消息
    incr_top(L);                                // 推入栈
    lua_unlock(L);                              // 解锁状态机
    return LUA_ERRRUN;                          // 返回错误代码
}


/**
 * @brief 恢复协程执行（Lua C API）
 * @param L 要恢复的协程状态机指针
 * @param nargs 传递给协程的参数数量
 * @return 协程执行状态：0(成功)、LUA_YIELD(让出)、错误代码
 *
 * 详细说明：
 * 这是Lua C API中恢复协程执行的主要接口。它处理协程的
 * 状态验证、参数传递、执行恢复和错误处理。
 *
 * 协程状态验证：
 * 1. 检查协程是否可恢复：
 *    - 状态为LUA_YIELD（从yield恢复）
 *    - 状态为0且在基础调用信息（新协程）
 * 2. 检查C栈是否溢出
 *
 * 恢复过程：
 * 1. 状态机锁定：确保线程安全
 * 2. 状态验证：检查协程是否可恢复
 * 3. 栈溢出检查：防止C栈溢出
 * 4. 用户状态通知：调用用户定义的恢复钩子
 * 5. 调用计数设置：设置基础调用计数
 * 6. 保护执行：在保护环境中恢复协程
 * 7. 错误处理：处理执行过程中的错误
 * 8. 状态更新：更新协程的最终状态
 *
 * 参数传递：
 * 栈顶的nargs个值作为参数传递给协程：
 * - 新协程：作为主函数的参数
 * - 恢复协程：作为yield的返回值
 *
 * 错误处理：
 * 如果执行过程中发生错误：
 * - 设置协程状态为错误状态
 * - 设置错误对象到栈顶
 * - 调整调用信息的栈顶
 *
 * 返回值语义：
 * - 0：协程正常结束
 * - LUA_YIELD：协程让出，可以再次恢复
 * - LUA_ERRRUN：运行时错误
 * - LUA_ERRMEM：内存错误
 * - LUA_ERRERR：错误处理中的错误
 *
 * 调用计数管理：
 * 维护正确的C调用计数，确保yield边界检查的正确性。
 *
 * 线程安全：
 * 通过lua_lock/lua_unlock确保操作的原子性。
 *
 * 用户扩展：
 * 通过luai_userstateresume允许用户在协程恢复时
 * 执行自定义操作。
 *
 * @pre L必须是有效的协程状态机，nargs >= 0
 * @post 协程被恢复执行，返回执行状态
 *
 * @note 这是协程系统的主要C API接口
 * @see lua_yield(), lua_newthread(), resume()
 */
LUA_API int lua_resume(lua_State *L, int nargs) {
    int status;
    lua_lock(L);                                // 锁定状态机

    // 验证协程是否可恢复
    if (L->status != LUA_YIELD && (L->status != 0 || L->ci != L->base_ci)) {
        return resume_error(L, "cannot resume non-suspended coroutine");
    }

    // 检查C栈溢出
    if (L->nCcalls >= LUAI_MAXCCALLS) {
        return resume_error(L, "C stack overflow");
    }

    luai_userstateresume(L, nargs);             // 用户状态恢复钩子
    lua_assert(L->errfunc == 0);                // 确保没有错误函数
    L->baseCcalls = ++L->nCcalls;               // 设置基础调用计数

    // 在保护环境中恢复协程执行
    status = luaD_rawrunprotected(L, resume, L->top - nargs);

    if (status != 0) {
        // 处理执行错误
        L->status = cast_byte(status);          // 设置错误状态
        luaD_seterrorobj(L, status, L->top);    // 设置错误对象
        L->ci->top = L->top;                    // 调整栈顶
    } else {
        // 正常执行完成
        lua_assert(L->nCcalls == L->baseCcalls);
        status = L->status;                     // 获取最终状态
    }

    --L->nCcalls;                               // 递减调用计数
    lua_unlock(L);                              // 解锁状态机
    return status;
}


/**
 * @brief 协程让出执行（Lua C API）
 * @param L 要让出的协程状态机指针
 * @param nresults 返回给恢复者的结果数量
 * @return 总是返回-1（表示让出）
 *
 * 详细说明：
 * 这是Lua C API中协程让出执行的接口。它暂停当前协程的执行，
 * 将控制权返回给调用lua_resume的代码。
 *
 * 让出机制：
 * 协程让出是协作式多任务的核心机制，允许协程主动放弃
 * 执行权，稍后可以从让出点恢复执行。
 *
 * 边界检查：
 * 检查当前调用深度是否超过基础调用深度，防止在以下
 * 情况下让出：
 * - 元方法调用中
 * - C函数调用中
 * - 其他不安全的调用边界
 *
 * 状态设置：
 * 1. 调整栈基址：指向返回值的起始位置
 * 2. 设置状态：标记协程为LUA_YIELD状态
 * 3. 保护返回值：确保返回值在正确位置
 *
 * 返回值处理：
 * 栈顶的nresults个值将作为lua_resume的返回值：
 * - 这些值会传递给恢复协程的代码
 * - 栈基址被调整到返回值的起始位置
 *
 * 调用边界安全：
 * 通过比较nCcalls和baseCcalls确保让出发生在安全的
 * 调用边界，防止破坏C调用栈的一致性。
 *
 * 用户扩展：
 * 通过luai_userstateyield允许用户在协程让出时
 * 执行自定义操作。
 *
 * 线程安全：
 * 通过lua_lock/lua_unlock确保操作的原子性。
 *
 * 执行流程：
 * 1. 用户状态通知：调用用户定义的让出钩子
 * 2. 状态机锁定：确保线程安全
 * 3. 边界检查：验证让出的安全性
 * 4. 状态设置：设置协程为让出状态
 * 5. 栈调整：调整栈基址到返回值位置
 * 6. 状态解锁：释放状态机锁
 *
 * 错误处理：
 * 如果在不安全的调用边界尝试让出，抛出运行时错误。
 *
 * 返回值语义：
 * 函数总是返回-1，这是yield的标识，表示函数不会
 * 正常返回，而是通过longjmp跳转。
 *
 * @pre L必须是有效的协程状态机，nresults >= 0
 * @post 协程被设置为让出状态，控制权返回给调用者
 *
 * @note 这个函数不会正常返回，而是通过异常机制跳转
 * @see lua_resume(), luaD_throw()
 */
LUA_API int lua_yield(lua_State *L, int nresults) {
    luai_userstateyield(L, nresults);           // 用户状态让出钩子
    lua_lock(L);                                // 锁定状态机

    // 检查让出边界的安全性
    if (L->nCcalls > L->baseCcalls) {
        luaG_runerror(L, "attempt to yield across metamethod/C-call boundary");
    }

    L->base = L->top - nresults;                // 调整栈基址到返回值
    L->status = LUA_YIELD;                      // 设置让出状态
    lua_unlock(L);                              // 解锁状态机
    return -1;                                  // 让出标识（不会正常返回）
}


/**
 * @brief 保护调用函数（内部接口）
 * @param L Lua状态机指针
 * @param func 要执行的函数指针
 * @param u 传递给函数的用户数据
 * @param old_top 原始栈顶位置
 * @param ef 错误处理函数位置
 * @return 执行状态：0表示成功，非0表示错误代码
 *
 * 详细说明：
 * 这个函数在保护环境中执行指定函数，提供完整的错误恢复机制。
 * 它是Lua内部保护执行的核心实现。
 *
 * 保护机制：
 * 1. 状态保存：保存所有关键的执行状态
 * 2. 保护执行：在异常安全环境中执行函数
 * 3. 错误恢复：发生错误时恢复到原始状态
 * 4. 状态恢复：无论成功失败都恢复错误函数
 *
 * 状态保存内容：
 * - C调用计数：oldnCcalls
 * - 调用信息：old_ci
 * - 钩子允许状态：old_allowhooks
 * - 错误处理函数：old_errfunc
 *
 * 错误恢复过程：
 * 1. 恢复栈顶：回到调用前的栈状态
 * 2. 关闭上值：关闭可能打开的上值
 * 3. 设置错误对象：在指定位置设置错误信息
 * 4. 恢复调用状态：恢复所有保存的状态
 * 5. 恢复栈限制：处理可能的栈溢出状态
 *
 * 错误处理函数：
 * 临时设置错误处理函数，允许在执行过程中使用
 * 自定义的错误处理逻辑。
 *
 * 栈管理：
 * 精确控制栈的状态，确保错误发生时能够正确
 * 恢复到调用前的状态。
 *
 * 上值处理：
 * 在错误恢复时关闭可能打开的上值，防止资源泄漏。
 *
 * 调用信息恢复：
 * 恢复调用信息指针、栈基址、程序计数器等，
 * 确保执行状态的完整恢复。
 *
 * 钩子状态：
 * 保存和恢复钩子允许状态，确保调试状态的一致性。
 *
 * 异常安全：
 * 提供强异常安全保证，即使发生错误也能保证
 * 状态的一致性和资源的正确清理。
 *
 * 使用场景：
 * - lua_pcall的内部实现
 * - 需要错误恢复的内部操作
 * - 保护执行的通用机制
 *
 * @pre L必须是有效的Lua状态机，func必须是有效的函数指针
 * @post 函数被执行，错误时状态被正确恢复
 *
 * @note 这是保护执行的核心实现
 * @see luaD_rawrunprotected(), lua_pcall()
 */
int luaD_pcall(lua_State *L, Pfunc func, void *u,
               ptrdiff_t old_top, ptrdiff_t ef) {
    int status;
    unsigned short oldnCcalls = L->nCcalls;    // 保存C调用计数
    ptrdiff_t old_ci = saveci(L, L->ci);        // 保存调用信息
    lu_byte old_allowhooks = L->allowhook;      // 保存钩子状态
    ptrdiff_t old_errfunc = L->errfunc;         // 保存错误函数

    L->errfunc = ef;                            // 设置新的错误函数

    // 在保护环境中执行函数
    status = luaD_rawrunprotected(L, func, u);

    if (status != 0) {
        // 错误恢复过程
        StkId oldtop = restorestack(L, old_top);    // 恢复栈顶
        luaF_close(L, oldtop);                      // 关闭上值
        luaD_seterrorobj(L, status, oldtop);        // 设置错误对象
        L->nCcalls = oldnCcalls;                    // 恢复调用计数
        L->ci = restoreci(L, old_ci);               // 恢复调用信息
        L->base = L->ci->base;                      // 恢复栈基址
        L->savedpc = L->ci->savedpc;                // 恢复程序计数器
        L->allowhook = old_allowhooks;              // 恢复钩子状态
        restore_stack_limit(L);                     // 恢复栈限制
    }

    L->errfunc = old_errfunc;                   // 恢复原错误函数
    return status;
}



// ============================================================================
// 解析器和编译系统
// ============================================================================

/**
 * @brief 解析器参数结构
 *
 * 详细说明：
 * 这个结构包含解析器所需的所有参数，用于在保护环境中
 * 传递解析参数。
 *
 * 字段说明：
 * - z：输入流，可以是文件或字符串
 * - buff：解析过程中使用的缓冲区
 * - name：源代码的名称，用于错误报告和调试
 *
 * 设计目的：
 * 将解析参数封装在结构中，便于在保护执行环境中传递。
 */
struct SParser {
    ZIO *z;                     // 输入流
    Mbuffer buff;               // 解析缓冲区
    const char *name;           // 源代码名称
};

/**
 * @brief 解析器执行函数（保护环境中执行）
 * @param L Lua状态机指针
 * @param ud 用户数据（SParser结构指针）
 *
 * 详细说明：
 * 这个函数在保护环境中执行Lua代码的解析或反序列化，
 * 创建相应的闭包对象。
 *
 * 解析类型检测：
 * 通过检查输入流的第一个字符判断输入类型：
 * - LUA_SIGNATURE[0]：预编译的字节码
 * - 其他：Lua源代码文本
 *
 * 解析过程：
 * 1. 前瞻检查：确定输入类型
 * 2. 垃圾回收检查：确保有足够内存
 * 3. 解析/反序列化：根据类型调用相应函数
 * 4. 创建闭包：为函数原型创建闭包
 * 5. 初始化上值：为所有上值创建空的上值对象
 * 6. 推入栈：将闭包推入栈顶
 *
 * 字节码处理：
 * 如果输入是预编译的字节码，调用luaU_undump进行反序列化。
 *
 * 源码处理：
 * 如果输入是源代码，调用luaY_parser进行语法分析和编译。
 *
 * 闭包创建：
 * 为解析得到的函数原型创建Lua闭包：
 * - 分配上值数组
 * - 设置全局环境
 * - 初始化所有上值为空
 *
 * 上值初始化：
 * 为函数的每个上值创建新的上值对象，初始状态为关闭状态。
 *
 * 内存管理：
 * 在解析前检查垃圾回收，确保有足够内存进行解析操作。
 *
 * 错误处理：
 * 在保护环境中执行，解析错误会被捕获并转换为Lua异常。
 *
 * 全局环境：
 * 新创建的闭包使用当前线程的全局环境作为其环境。
 *
 * @pre L必须是有效的Lua状态机，ud必须指向有效的SParser结构
 * @post 解析完成的闭包被推入栈顶
 *
 * @note 这是解析系统的核心执行函数
 * @see luaY_parser(), luaU_undump(), luaF_newLclosure()
 */
static void f_parser(lua_State *L, void *ud) {
    int i;
    Proto *tf;
    Closure *cl;
    struct SParser *p = cast(struct SParser *, ud);

    // 检查输入类型（字节码 vs 源代码）
    int c = luaZ_lookahead(p->z);
    luaC_checkGC(L);                            // 检查垃圾回收

    // 根据输入类型选择解析方法
    tf = ((c == LUA_SIGNATURE[0]) ? luaU_undump : luaY_parser)(L, p->z,
                                                               &p->buff, p->name);

    // 为函数原型创建闭包
    cl = luaF_newLclosure(L, tf->nups, hvalue(gt(L)));
    cl->l.p = tf;                               // 设置函数原型

    // 初始化所有上值
    for (i = 0; i < tf->nups; i++) {
        cl->l.upvals[i] = luaF_newupval(L);
    }

    // 将闭包推入栈顶
    setclvalue(L, L->top, cl);
    incr_top(L);
}


/**
 * @brief 保护模式下的解析器执行
 * @param L Lua状态机指针
 * @param z 输入流
 * @param name 源代码名称
 * @return 解析状态：0表示成功，非0表示错误代码
 *
 * 详细说明：
 * 这个函数在保护模式下执行Lua代码的解析，提供完整的
 * 错误处理和资源管理。
 *
 * 解析流程：
 * 1. 参数准备：设置解析器参数结构
 * 2. 缓冲区初始化：初始化解析缓冲区
 * 3. 保护执行：在保护环境中执行解析
 * 4. 资源清理：释放解析缓冲区
 * 5. 返回状态：返回解析结果状态
 *
 * 参数设置：
 * 将输入流和源代码名称设置到SParser结构中，
 * 为解析器提供必要的输入信息。
 *
 * 缓冲区管理：
 * - 初始化：为解析过程分配缓冲区
 * - 清理：无论成功失败都释放缓冲区
 * - 异常安全：确保资源不泄漏
 *
 * 保护执行：
 * 使用luaD_pcall在保护环境中执行解析，捕获可能的错误：
 * - 语法错误
 * - 内存错误
 * - I/O错误
 * - 其他运行时错误
 *
 * 错误处理：
 * 如果解析过程中发生错误，错误信息会被设置到栈上，
 * 调用者可以获取详细的错误信息。
 *
 * 栈管理：
 * 保存当前栈顶位置，确保错误恢复时能够正确
 * 恢复栈状态。
 *
 * 资源安全：
 * 使用RAII模式管理解析缓冲区，确保无论解析
 * 成功还是失败都能正确释放资源。
 *
 * 返回值语义：
 * - 0：解析成功，闭包在栈顶
 * - LUA_ERRSYNTAX：语法错误
 * - LUA_ERRMEM：内存不足
 * - 其他：其他类型的错误
 *
 * 使用场景：
 * - lua_load的内部实现
 * - 动态代码编译
 * - 模块加载
 * - 交互式解释器
 *
 * 线程安全：
 * 函数本身是线程安全的，但需要确保输入流的
 * 线程安全性。
 *
 * @pre L必须是有效的Lua状态机，z必须是有效的输入流
 * @post 解析完成，成功时闭包在栈顶，失败时错误信息在栈顶
 *
 * @note 这是Lua代码加载的主要接口
 * @see lua_load(), f_parser(), luaD_pcall()
 */
int luaD_protectedparser(lua_State *L, ZIO *z, const char *name) {
    struct SParser p;
    int status;

    // 设置解析器参数
    p.z = z;
    p.name = name;

    // 初始化解析缓冲区
    luaZ_initbuffer(L, &p.buff);

    // 在保护环境中执行解析
    status = luaD_pcall(L, f_parser, &p, savestack(L, L->top), L->errfunc);

    // 清理解析缓冲区（异常安全）
    luaZ_freebuffer(L, &p.buff);

    return status;
}


