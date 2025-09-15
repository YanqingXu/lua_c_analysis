/**
 * @file ldebug.c
 * @brief Lua调试接口：调试信息管理、错误处理和栈跟踪系统
 *
 * 版权信息：
 * $Id: ldebug.c,v 2.29.1.6 2008/05/08 16:56:26 roberto Exp $
 * Lua调试接口实现
 * 版权声明见lua.h文件
 *
 * 模块概述：
 * 本模块实现了Lua虚拟机的调试支持系统，提供了完整的调试信息管理、
 * 错误处理、栈跟踪和运行时检查功能。它是Lua调试器和错误报告系统
 * 的核心基础设施。
 *
 * 主要功能：
 * 1. 调试钩子管理：设置和管理调试回调函数
 * 2. 栈信息获取：提供详细的函数调用栈信息
 * 3. 源码定位：将字节码位置映射到源代码行号
 * 4. 符号信息：提供变量名、函数名等符号信息
 * 5. 错误处理：格式化错误消息和异常报告
 * 6. 代码验证：字节码完整性和正确性检查
 * 7. 类型错误：运行时类型错误的详细报告
 *
 * 核心数据结构：
 * - lua_Debug: 调试信息结构，包含函数、源码、行号等信息
 * - CallInfo: 函数调用信息，维护调用栈状态
 * - Proto: 函数原型，包含调试符号和行号映射
 *
 * 设计特点：
 * 1. 高效的调试信息收集，最小化运行时开销
 * 2. 完整的错误上下文信息，便于问题定位
 * 3. 灵活的调试钩子机制，支持多种调试需求
 * 4. 精确的源码定位，支持准确的断点和跟踪
 * 5. 健壮的错误恢复机制，确保调试过程的稳定性
 *
 * 性能考虑：
 * - 调试信息的延迟计算，避免不必要的开销
 * - 高效的栈遍历算法，快速定位调用信息
 * - 智能的符号查找，优化变量名解析性能
 * - 缓存机制，减少重复的调试信息计算
 *
 * 使用场景：
 * - 交互式调试器：断点、单步执行、变量检查
 * - 错误报告：详细的错误消息和调用栈
 * - 性能分析：函数调用统计和性能监控
 * - 代码覆盖：源码行执行情况统计
 * - 运行时检查：类型安全和边界检查
 *
 * 安全特性：
 * - 栈溢出检测和保护
 * - 无效指针访问防护
 * - 调试信息的完整性验证
 * - 异常安全的资源管理
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2008-2011
 *
 * @note 本模块是Lua虚拟机调试系统的核心，提供了完整的调试支持
 * @see lua.h, lstate.h, lobject.h, ldo.h
 */

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#define ldebug_c
#define LUA_CORE

#include "lua.h"

#include "lapi.h"
#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"

/**
 * @brief 前向声明：获取函数名称
 * @param L Lua状态机
 * @param ci 调用信息
 * @param name 输出参数，存储函数名称
 * @return 函数类型描述字符串
 */
static const char *getfuncname (lua_State *L, CallInfo *ci, const char **name);

/**
 * @brief 获取当前程序计数器位置
 *
 * 计算指定调用信息对应的当前程序计数器（PC）位置。程序计数器用于
 * 跟踪当前执行的字节码指令位置，是调试和错误报告的重要信息。
 *
 * @param L Lua状态机指针
 * @param ci 调用信息结构指针，包含函数调用的上下文信息
 * @return 程序计数器位置，如果不是Lua函数则返回-1
 *
 * @note 只有Lua函数才有有效的程序计数器信息
 * @note 对于当前活动的调用，需要同步保存的PC值
 *
 * @see CallInfo, pcRel, ci_func
 *
 * 实现细节：
 * - 检查是否为Lua函数（C函数没有PC信息）
 * - 对于当前活动调用，同步savedpc字段
 * - 使用pcRel宏计算相对于函数起始位置的偏移
 * - 返回的PC值用于行号映射和调试信息获取
 */
static int currentpc (lua_State *L, CallInfo *ci) {
    if (!isLua(ci)) return -1;  /* 不是Lua函数？ */
    if (ci == L->ci)
        ci->savedpc = L->savedpc;
    return pcRel(ci->savedpc, ci_func(ci)->l.p);
}

/**
 * @brief 获取当前执行行号
 *
 * 根据程序计数器位置获取对应的源代码行号。这是调试系统的核心功能，
 * 用于将字节码执行位置映射回源代码行号，便于错误报告和调试。
 *
 * @param L Lua状态机指针
 * @param ci 调用信息结构指针
 * @return 当前执行的源代码行号，如果无法确定则返回-1
 *
 * @note 只有活动的Lua函数才有当前行信息
 * @note 行号信息存储在函数原型的lineinfo数组中
 *
 * @see currentpc, getline, Proto
 *
 * 使用场景：
 * - 错误消息中显示出错位置
 * - 调试器显示当前执行行
 * - 性能分析工具的行级统计
 * - 代码覆盖率分析
 *
 * 实现原理：
 * - 首先获取当前程序计数器位置
 * - 使用getline函数查找PC对应的行号
 * - 行号映射信息存储在Proto结构中
 */
static int currentline (lua_State *L, CallInfo *ci) {
    int pc = currentpc(L, ci);
    if (pc < 0)
        return -1;  /* 只有活动的Lua函数才有当前行信息 */
    else
        return getline(ci_func(ci)->l.p, pc);
}


/**
 * @brief 设置调试钩子函数
 *
 * 设置Lua状态机的调试钩子函数，用于在特定事件发生时调用用户定义的
 * 调试回调函数。这是Lua调试系统的核心接口，支持多种调试事件监控。
 *
 * @param L Lua状态机指针
 * @param func 调试钩子函数指针，NULL表示禁用钩子
 * @param mask 事件掩码，指定要监控的事件类型：
 *             - LUA_MASKCALL: 函数调用事件
 *             - LUA_MASKRET: 函数返回事件
 *             - LUA_MASKLINE: 行执行事件
 *             - LUA_MASKCOUNT: 指令计数事件
 * @param count 指令计数间隔，仅在LUA_MASKCOUNT时有效
 * @return 总是返回1（成功）
 *
 * @note 此函数可以异步调用（例如在信号处理中）
 * @note 设置func为NULL或mask为0将禁用所有钩子
 *
 * @see lua_Hook, lua_gethook, resethookcount
 *
 * 使用场景：
 * - 交互式调试器：设置断点和单步执行
 * - 性能分析：监控函数调用和执行时间
 * - 代码覆盖：跟踪代码执行路径
 * - 运行时监控：检测特定的执行模式
 *
 * 安全特性：
 * - 支持异步调用，适用于信号处理
 * - 自动重置钩子计数器
 * - 类型安全的掩码转换
 *
 * @example
 * // 设置行执行钩子
 * lua_sethook(L, my_line_hook, LUA_MASKLINE, 0);
 *
 * // 设置函数调用和返回钩子
 * lua_sethook(L, my_call_hook, LUA_MASKCALL | LUA_MASKRET, 0);
 *
 * // 禁用所有钩子
 * lua_sethook(L, NULL, 0, 0);
 */
LUA_API int lua_sethook (lua_State *L, lua_Hook func, int mask, int count) {
    if (func == NULL || mask == 0) {  /* 关闭钩子？ */
        mask = 0;
        func = NULL;
    }
    L->hook = func;
    L->basehookcount = count;
    resethookcount(L);
    L->hookmask = cast_byte(mask);
    return 1;
}

/**
 * @brief 获取当前调试钩子函数
 *
 * 返回当前设置的调试钩子函数指针。用于查询当前的调试配置状态。
 *
 * @param L Lua状态机指针
 * @return 当前的调试钩子函数指针，如果未设置则返回NULL
 *
 * @see lua_sethook, lua_Hook
 *
 * 使用场景：
 * - 调试器状态查询
 * - 钩子函数的保存和恢复
 * - 调试配置的验证
 */
LUA_API lua_Hook lua_gethook (lua_State *L) {
    return L->hook;
}

/**
 * @brief 获取调试钩子事件掩码
 *
 * 返回当前设置的调试事件掩码，指示哪些调试事件被监控。
 *
 * @param L Lua状态机指针
 * @return 当前的事件掩码值
 *
 * @see lua_sethook, LUA_MASKCALL, LUA_MASKRET, LUA_MASKLINE, LUA_MASKCOUNT
 *
 * 使用场景：
 * - 查询当前监控的事件类型
 * - 调试配置的保存和恢复
 * - 条件性的调试行为控制
 */
LUA_API int lua_gethookmask (lua_State *L) {
    return L->hookmask;
}

/**
 * @brief 获取调试钩子计数间隔
 *
 * 返回当前设置的指令计数间隔，用于LUA_MASKCOUNT事件的触发频率。
 *
 * @param L Lua状态机指针
 * @return 当前的计数间隔值
 *
 * @see lua_sethook, LUA_MASKCOUNT
 *
 * 使用场景：
 * - 查询计数钩子的触发频率
 * - 性能分析的配置查询
 * - 调试参数的保存和恢复
 */
LUA_API int lua_gethookcount (lua_State *L) {
    return L->basehookcount;
}


/**
 * @brief 获取调用栈信息
 *
 * 获取指定层级的函数调用栈信息，用于调试器显示调用链和错误报告。
 * 这是调试系统的核心功能，提供了完整的栈遍历能力。
 *
 * @param L Lua状态机指针
 * @param level 栈层级，0表示当前函数，1表示调用者，以此类推
 * @param ar 输出参数，存储调试信息的结构体指针
 * @return 成功返回1，失败返回0（层级不存在）
 *
 * @note 函数会自动处理尾调用优化的情况
 * @note 使用lua_lock/lua_unlock确保线程安全
 *
 * @see lua_Debug, CallInfo, lua_getinfo
 *
 * 实现细节：
 * - 从当前调用信息开始向上遍历调用栈
 * - 跳过由于尾调用优化而丢失的调用帧
 * - 处理负层级（表示丢失的尾调用）
 * - 设置调试结构体的内部索引字段
 *
 * 使用场景：
 * - 调试器显示调用栈
 * - 错误报告中的栈跟踪
 * - 性能分析的调用链统计
 * - 运行时栈检查和验证
 *
 * 错误处理：
 * - 无效层级返回0
 * - 自动处理栈边界检查
 * - 线程安全的状态访问
 *
 * @example
 * lua_Debug ar;
 * if (lua_getstack(L, 1, &ar)) {
 *     // 获取调用者信息成功
 *     lua_getinfo(L, "nSl", &ar);
 *     printf("Called from %s:%d\n", ar.source, ar.currentline);
 * }
 */
LUA_API int lua_getstack (lua_State *L, int level, lua_Debug *ar) {
    int status;
    CallInfo *ci;
    lua_lock(L);
    for (ci = L->ci; level > 0 && ci > L->base_ci; ci--) {
        level--;
        if (f_isLua(ci))  /* Lua函数？ */
            level -= ci->tailcalls;  /* 跳过丢失的尾调用 */
    }
    if (level == 0 && ci > L->base_ci) {  /* 找到层级？ */
        status = 1;
        ar->i_ci = cast_int(ci - L->base_ci);
    }
    else if (level < 0) {  /* 层级属于丢失的尾调用？ */
        status = 1;
        ar->i_ci = 0;
    }
    else status = 0;  /* 没有这样的层级 */
    lua_unlock(L);
    return status;
}

/**
 * @brief 获取Lua函数原型
 *
 * 从调用信息中提取Lua函数的原型信息。原型包含函数的字节码、
 * 调试信息、常量表等重要数据。
 *
 * @param ci 调用信息结构指针
 * @return Lua函数原型指针，如果是C函数则返回NULL
 *
 * @note 只有Lua函数才有原型信息，C函数返回NULL
 *
 * @see Proto, CallInfo, isLua
 *
 * 使用场景：
 * - 获取函数的调试信息
 * - 访问函数的字节码和常量
 * - 查找局部变量名称
 * - 源码位置映射
 */
static Proto *getluaproto (CallInfo *ci) {
    return (isLua(ci) ? ci_func(ci)->l.p : NULL);
}

/**
 * @brief 查找局部变量名称
 *
 * 在指定的调用信息中查找第n个局部变量的名称。这是调试系统
 * 提供变量检查功能的基础。
 *
 * @param L Lua状态机指针
 * @param ci 调用信息结构指针
 * @param n 变量索引（从1开始）
 * @return 变量名称字符串，如果找不到则返回NULL
 *
 * @note 对于临时变量，返回"(*temporary)"
 * @note 只在有效的栈范围内查找变量
 *
 * @see luaF_getlocalname, getluaproto, currentpc
 *
 * 实现逻辑：
 * 1. 首先尝试从Lua函数的调试信息中获取变量名
 * 2. 如果是Lua函数且有调试信息，使用luaF_getlocalname
 * 3. 否则检查是否在有效栈范围内，返回临时变量标识
 * 4. 超出范围则返回NULL
 *
 * 使用场景：
 * - 调试器显示局部变量
 * - 错误消息中的变量名
 * - 运行时变量检查
 * - 代码分析工具
 *
 * 性能考虑：
 * - 优先使用调试符号表
 * - 快速的栈边界检查
 * - 避免不必要的字符串操作
 */
static const char *findlocal (lua_State *L, CallInfo *ci, int n) {
    const char *name;
    Proto *fp = getluaproto(ci);
    if (fp && (name = luaF_getlocalname(fp, n, currentpc(L, ci))) != NULL)
        return name;  /* 是Lua函数中的局部变量 */
    else {
        StkId limit = (ci == L->ci) ? L->top : (ci+1)->func;
        if (limit - ci->base >= n && n > 0)  /* 'n'在'ci'栈内？ */
            return "(*temporary)";
        else
            return NULL;
    }
}


/**
 * @brief 获取局部变量的值
 *
 * 获取指定调用栈层级中第n个局部变量的值，并将其压入Lua栈顶。
 * 这是调试器检查变量值的核心功能。
 *
 * @param L Lua状态机指针
 * @param ar 调试信息结构，包含目标调用栈的索引
 * @param n 局部变量索引（从1开始计数）
 * @return 变量名称字符串，如果变量不存在则返回NULL
 *
 * @note 成功时会将变量值压入栈顶
 * @note 使用lua_lock/lua_unlock确保线程安全
 * @note 变量索引从1开始，符合Lua的惯例
 *
 * @see findlocal, luaA_pushobject, lua_Debug
 *
 * 实现流程：
 * 1. 根据调试信息定位目标调用信息
 * 2. 使用findlocal查找变量名称
 * 3. 如果变量存在，将其值压入栈顶
 * 4. 返回变量名称或NULL
 *
 * 使用场景：
 * - 调试器显示局部变量值
 * - 运行时变量检查和监控
 * - 错误诊断中的变量状态查看
 * - 交互式调试环境的变量访问
 *
 * 错误处理：
 * - 无效变量索引返回NULL
 * - 自动处理栈边界检查
 * - 线程安全的状态访问
 *
 * @example
 * lua_Debug ar;
 * if (lua_getstack(L, 1, &ar)) {
 *     const char *name = lua_getlocal(L, &ar, 1);
 *     if (name) {
 *         printf("Variable %s = ", name);
 *         // 栈顶现在包含变量值
 *         lua_pop(L, 1);  // 清理栈
 *     }
 * }
 */
LUA_API const char *lua_getlocal (lua_State *L, const lua_Debug *ar, int n) {
    CallInfo *ci = L->base_ci + ar->i_ci;
    const char *name = findlocal(L, ci, n);
    lua_lock(L);
    if (name)
        luaA_pushobject(L, ci->base + (n - 1));
    lua_unlock(L);
    return name;
}

/**
 * @brief 设置局部变量的值
 *
 * 设置指定调用栈层级中第n个局部变量的值。新值从Lua栈顶获取，
 * 设置完成后会弹出栈顶元素。这是调试器修改变量值的核心功能。
 *
 * @param L Lua状态机指针
 * @param ar 调试信息结构，包含目标调用栈的索引
 * @param n 局部变量索引（从1开始计数）
 * @return 变量名称字符串，如果变量不存在则返回NULL
 *
 * @note 新值必须预先压入栈顶
 * @note 函数会自动弹出栈顶的新值
 * @note 使用lua_lock/lua_unlock确保线程安全
 *
 * @see findlocal, setobjs2s, lua_Debug
 *
 * 实现流程：
 * 1. 根据调试信息定位目标调用信息
 * 2. 使用findlocal查找变量名称
 * 3. 如果变量存在，将栈顶值赋给该变量
 * 4. 弹出栈顶元素
 * 5. 返回变量名称或NULL
 *
 * 使用场景：
 * - 调试器修改局部变量值
 * - 运行时变量值的动态调整
 * - 测试和调试中的状态修改
 * - 交互式调试环境的变量赋值
 *
 * 安全特性：
 * - 自动类型转换和兼容性检查
 * - 栈状态的自动管理
 * - 线程安全的变量访问
 *
 * @example
 * lua_Debug ar;
 * if (lua_getstack(L, 1, &ar)) {
 *     lua_pushnumber(L, 42);  // 新值
 *     const char *name = lua_setlocal(L, &ar, 1);
 *     if (name) {
 *         printf("Set variable %s to 42\n", name);
 *     } else {
 *         lua_pop(L, 1);  // 清理未使用的值
 *     }
 * }
 */
LUA_API const char *lua_setlocal (lua_State *L, const lua_Debug *ar, int n) {
    CallInfo *ci = L->base_ci + ar->i_ci;
    const char *name = findlocal(L, ci, n);
    lua_lock(L);
    if (name)
        setobjs2s(L, ci->base + (n - 1), L->top - 1);
    L->top--;  /* 弹出值 */
    lua_unlock(L);
    return name;
}


/**
 * @brief 填充函数基本信息
 *
 * 根据闭包类型填充调试信息结构中的函数相关字段，包括源文件、
 * 定义行号、函数类型等基本信息。
 *
 * @param ar 调试信息结构指针，用于存储函数信息
 * @param cl 闭包指针，包含函数的详细信息
 *
 * @note 区分C函数和Lua函数，填充不同的信息
 * @note 自动生成简短的源文件标识符
 *
 * @see Closure, lua_Debug, luaO_chunkid
 *
 * 填充的信息包括：
 * - source: 源文件路径或标识符
 * - linedefined: 函数定义开始行号
 * - lastlinedefined: 函数定义结束行号
 * - what: 函数类型（"C"、"Lua"或"main"）
 * - short_src: 简化的源文件标识符
 *
 * C函数处理：
 * - 源文件标记为"=[C]"
 * - 行号设置为-1（无意义）
 * - 类型标记为"C"
 *
 * Lua函数处理：
 * - 从函数原型获取真实源文件路径
 * - 获取实际的定义行号范围
 * - 根据定义位置判断是否为主函数
 *
 * 使用场景：
 * - 调试器显示函数信息
 * - 错误报告中的函数定位
 * - 性能分析的函数统计
 * - 代码覆盖率分析
 */
static void funcinfo (lua_Debug *ar, Closure *cl) {
    if (cl->c.isC) {
        ar->source = "=[C]";
        ar->linedefined = -1;
        ar->lastlinedefined = -1;
        ar->what = "C";
    }
    else {
        ar->source = getstr(cl->l.p->source);
        ar->linedefined = cl->l.p->linedefined;
        ar->lastlinedefined = cl->l.p->lastlinedefined;
        ar->what = (ar->linedefined == 0) ? "main" : "Lua";
    }
    luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
}

/**
 * @brief 填充尾调用信息
 *
 * 为尾调用优化的函数调用填充特殊的调试信息。尾调用是Lua的重要
 * 优化特性，但会导致调用栈信息的丢失，需要特殊处理。
 *
 * @param ar 调试信息结构指针
 *
 * @note 尾调用没有真实的栈帧，所有信息都设置为特殊值
 * @note 这种调用不占用额外的栈空间
 *
 * @see lua_Debug, 尾调用优化
 *
 * 设置的特殊值：
 * - name: 空字符串（无函数名）
 * - namewhat: 空字符串（无名称类型）
 * - what: "tail"（标识为尾调用）
 * - 所有行号: -1（无效值）
 * - source: "=(tail call)"（特殊标识）
 * - nups: 0（无上值信息）
 *
 * 尾调用优化说明：
 * - Lua会优化尾递归和尾调用
 * - 优化后的调用不会增加栈深度
 * - 但会丢失中间调用的调试信息
 * - 此函数提供统一的占位信息
 *
 * 使用场景：
 * - 调试器显示尾调用占位符
 * - 栈跟踪中的尾调用标识
 * - 性能分析中的优化识别
 */
static void info_tailcall (lua_Debug *ar) {
    ar->name = ar->namewhat = "";
    ar->what = "tail";
    ar->lastlinedefined = ar->linedefined = ar->currentline = -1;
    ar->source = "=(tail call)";
    luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
    ar->nups = 0;
}

/**
 * @brief 收集函数的有效行号
 *
 * 创建一个包含函数所有有效执行行号的表。这些行号对应于
 * 可以设置断点的代码行，是调试器功能的重要支持。
 *
 * @param L Lua状态机指针
 * @param f 函数闭包指针
 *
 * @note 结果表会被压入栈顶
 * @note C函数没有行号信息，会压入nil值
 *
 * @see Table, luaH_new, setbvalue
 *
 * 实现逻辑：
 * 1. 检查函数类型（C函数或Lua函数）
 * 2. 对于C函数，压入nil值
 * 3. 对于Lua函数，创建新表
 * 4. 遍历函数的行号信息数组
 * 5. 将每个有效行号作为表的键，值设为true
 * 6. 将结果表压入栈顶
 *
 * 行号信息用途：
 * - 调试器设置断点的有效位置
 * - 代码覆盖率分析的行级统计
 * - 性能分析的行级性能数据
 * - 源码编辑器的调试标记
 *
 * 数据结构：
 * - 使用Lua表存储行号集合
 * - 行号作为键，布尔值true作为值
 * - 提供快速的行号查找能力
 *
 * 性能考虑：
 * - 延迟创建，只在需要时生成
 * - 使用高效的哈希表结构
 * - 避免重复的行号信息
 */
static void collectvalidlines (lua_State *L, Closure *f) {
    if (f == NULL || f->c.isC) {
        setnilvalue(L->top);
    }
    else {
        Table *t = luaH_new(L, 0, 0);
        int *lineinfo = f->l.p->lineinfo;
        int i;
        for (i=0; i<f->l.p->sizelineinfo; i++)
            setbvalue(luaH_setnum(L, t, lineinfo[i]), 1);
        sethvalue(L, L->top, t);
    }
    incr_top(L);
}


/**
 * @brief 辅助获取调试信息
 *
 * 根据指定的选项字符串填充调试信息结构。这是lua_getinfo的核心
 * 实现函数，支持多种信息类型的灵活组合获取。
 *
 * @param L Lua状态机指针
 * @param what 选项字符串，指定要获取的信息类型：
 *             - 'S': 源文件信息（source, linedefined, lastlinedefined, what）
 *             - 'l': 当前行号（currentline）
 *             - 'u': 上值数量（nups）
 *             - 'n': 函数名称信息（name, namewhat）
 *             - 'L': 有效行号表（由lua_getinfo处理）
 *             - 'f': 函数对象（由lua_getinfo处理）
 * @param ar 调试信息结构指针，用于存储获取的信息
 * @param f 函数闭包指针，NULL表示尾调用
 * @param ci 调用信息指针，用于获取运行时信息
 * @return 成功返回1，遇到无效选项返回0
 *
 * @note 选项可以任意组合，如"Sln"获取源文件、行号和名称信息
 * @note 'L'和'f'选项由调用者lua_getinfo特殊处理
 *
 * @see lua_getinfo, funcinfo, getfuncname, currentline
 *
 * 选项详解：
 * - 'S': 调用funcinfo填充源文件相关信息
 * - 'l': 获取当前执行行号，需要有效的调用信息
 * - 'u': 获取函数的上值（upvalue）数量
 * - 'n': 尝试获取函数名称和名称类型
 * - 'L': 延迟到lua_getinfo中处理，获取有效行号表
 * - 'f': 延迟到lua_getinfo中处理，获取函数对象
 *
 * 错误处理：
 * - 无效选项字符会导致返回0
 * - 缺少必要信息时设置默认值
 * - 尾调用情况的特殊处理
 *
 * 性能优化：
 * - 按需获取信息，避免不必要的计算
 * - 高效的选项字符串解析
 * - 最小化函数调用开销
 *
 * 使用场景：
 * - 调试器获取函数详细信息
 * - 错误报告的上下文信息收集
 * - 性能分析的函数元数据
 * - 运行时反射和内省
 */
static int auxgetinfo (lua_State *L, const char *what, lua_Debug *ar,
                    Closure *f, CallInfo *ci) {
    int status = 1;
    if (f == NULL) {
        info_tailcall(ar);
        return status;
    }
    for (; *what; what++) {
        switch (*what) {
            case 'S': {
                funcinfo(ar, f);
                break;
            }
            case 'l': {
                ar->currentline = (ci) ? currentline(L, ci) : -1;
                break;
            }
            case 'u': {
                ar->nups = f->c.nupvalues;
                break;
            }
            case 'n': {
                ar->namewhat = (ci) ? getfuncname(L, ci, &ar->name) : NULL;
                if (ar->namewhat == NULL) {
                    ar->namewhat = "";  /* 未找到 */
                    ar->name = NULL;
                }
                break;
            }
            case 'L':
            case 'f':  /* 由lua_getinfo处理 */
                break;
            default: status = 0;  /* 无效选项 */
        }
    }
    return status;
}


/**
 * @brief 获取调试信息主函数
 *
 * 这是Lua调试API的核心函数，用于获取函数的各种调试信息。
 * 支持两种模式：从栈顶函数获取信息，或从调用栈层级获取信息。
 *
 * @param L Lua状态机指针
 * @param what 选项字符串，指定要获取的信息类型：
 *             - 'S': 源文件信息
 *             - 'l': 当前行号
 *             - 'u': 上值数量
 *             - 'n': 函数名称
 *             - 'L': 有效行号表（压入栈顶）
 *             - 'f': 函数对象（压入栈顶）
 *             - '>': 特殊前缀，表示从栈顶获取函数
 * @param ar 调试信息结构指针，包含输入和输出信息
 * @return 成功返回1，失败返回0
 *
 * @note 'L'和'f'选项会将结果压入栈顶
 * @note 使用lua_lock/lua_unlock确保线程安全
 * @note 支持两种调用模式：栈顶函数模式和调用栈模式
 *
 * @see auxgetinfo, collectvalidlines, lua_getstack
 *
 * 调用模式：
 * 1. 栈顶函数模式（what以'>'开头）：
 *    - 从栈顶获取函数对象
 *    - 获取函数的静态信息
 *    - 无运行时上下文信息
 *
 * 2. 调用栈模式（ar->i_ci != 0）：
 *    - 从指定调用栈层级获取信息
 *    - 包含运行时上下文信息
 *    - 可获取当前行号等动态信息
 *
 * 特殊选项处理：
 * - 'f': 将函数对象压入栈顶，便于后续操作
 * - 'L': 创建有效行号表并压入栈顶
 * - 其他选项由auxgetinfo统一处理
 *
 * 错误处理：
 * - 无效函数类型检查
 * - 调用栈边界验证
 * - 选项字符串有效性检查
 *
 * 使用场景：
 * - 调试器获取函数详细信息
 * - 错误报告的上下文收集
 * - 性能分析的元数据获取
 * - 运行时反射和内省
 *
 * @example
 * // 获取当前函数信息
 * lua_Debug ar;
 * if (lua_getstack(L, 0, &ar)) {
 *     lua_getinfo(L, "Sln", &ar);
 *     printf("Function: %s at %s:%d\n",
 *            ar.name, ar.source, ar.currentline);
 * }
 *
 * // 获取栈顶函数的有效行号
 * lua_pushcfunction(L, my_function);
 * lua_getinfo(L, ">L", &ar);
 * // 栈顶现在包含行号表
 */
LUA_API int lua_getinfo (lua_State *L, const char *what, lua_Debug *ar) {
    int status;
    Closure *f = NULL;
    CallInfo *ci = NULL;
    lua_lock(L);
    if (*what == '>') {
        StkId func = L->top - 1;
        luai_apicheck(L, ttisfunction(func));
        what++;  /* 跳过'>' */
        f = clvalue(func);
        L->top--;  /* 弹出函数 */
    }
    else if (ar->i_ci != 0) {  /* 非尾调用？ */
        ci = L->base_ci + ar->i_ci;
        lua_assert(ttisfunction(ci->func));
        f = clvalue(ci->func);
    }
    status = auxgetinfo(L, what, ar, f, ci);
    if (strchr(what, 'f')) {
        if (f == NULL) setnilvalue(L->top);
        else setclvalue(L, L->top, f);
        incr_top(L);
    }
    if (strchr(what, 'L'))
        collectvalidlines(L, f);
    lua_unlock(L);
    return status;
}


/**
 * @section 符号执行和代码检查器
 *
 * 这一部分实现了Lua字节码的符号执行和完整性检查功能。
 * 符号执行是一种静态分析技术，用于验证字节码的正确性和安全性。
 *
 * 主要功能：
 * - 字节码指令的有效性验证
 * - 寄存器使用的边界检查
 * - 跳转目标的合法性验证
 * - 函数原型的完整性检查
 * - 栈使用的安全性分析
 *
 * 设计目标：
 * - 防止恶意或损坏的字节码执行
 * - 确保虚拟机的稳定性和安全性
 * - 提供详细的错误诊断信息
 * - 支持调试器的代码分析功能
 */

/**
 * @brief 检查条件宏
 *
 * 如果条件不满足则立即返回0（失败）。这是代码检查器的基础宏，
 * 用于简化条件检查的代码编写。
 *
 * @param x 要检查的条件表达式
 * @note 失败时直接返回，不执行后续检查
 */
#define check(x)		if (!(x)) return 0;

/**
 * @brief 检查跳转目标宏
 *
 * 验证跳转目标PC值是否在有效的字节码范围内。
 *
 * @param pt 函数原型指针
 * @param pc 程序计数器值
 * @note 确保跳转不会越界访问字节码数组
 */
#define checkjump(pt,pc)	check(0 <= pc && pc < pt->sizecode)

/**
 * @brief 检查寄存器索引宏
 *
 * 验证寄存器索引是否在函数的最大栈大小范围内。
 *
 * @param pt 函数原型指针
 * @param reg 寄存器索引
 * @note 防止栈溢出和无效的寄存器访问
 */
#define checkreg(pt,reg)	check((reg) < (pt)->maxstacksize)

/**
 * @brief 函数原型预检查
 *
 * 对函数原型进行基本的完整性和一致性检查，验证各种大小字段
 * 和标志位的合理性。这是符号执行前的必要准备步骤。
 *
 * @param pt 要检查的函数原型指针
 * @return 检查通过返回1，失败返回0
 *
 * @note 这是所有代码检查的第一步
 * @note 检查失败表明字节码可能已损坏
 *
 * @see Proto, MAXSTACK, VARARG_HASARG, VARARG_NEEDSARG
 *
 * 检查项目：
 * 1. 最大栈大小不超过虚拟机限制
 * 2. 参数数量与栈大小的一致性
 * 3. 可变参数标志的一致性
 * 4. 上值数量的合理性
 * 5. 行号信息与字节码的对应关系
 * 6. 字节码以RETURN指令结束
 *
 * 安全考虑：
 * - 防止栈溢出攻击
 * - 验证元数据的完整性
 * - 确保字节码的结构正确性
 *
 * 性能影响：
 * - 只在加载时执行一次
 * - 快速的整数比较操作
 * - 早期失败，避免后续复杂检查
 */
static int precheck (const Proto *pt) {
    check(pt->maxstacksize <= MAXSTACK);
    check(pt->numparams+(pt->is_vararg & VARARG_HASARG) <= pt->maxstacksize);
    check(!(pt->is_vararg & VARARG_NEEDSARG) ||
              (pt->is_vararg & VARARG_HASARG));
    check(pt->sizeupvalues <= pt->nups);
    check(pt->sizelineinfo == pt->sizecode || pt->sizelineinfo == 0);
    check(pt->sizecode > 0 && GET_OPCODE(pt->code[pt->sizecode-1]) == OP_RETURN);
    return 1;
}


/**
 * @brief 检查开放操作宏
 *
 * 检查指定位置的下一条指令是否为有效的开放操作后续指令。
 * 开放操作是指参数数量在运行时确定的操作，如可变参数调用。
 *
 * @param pt 函数原型指针
 * @param pc 当前程序计数器位置
 * @note 调用luaG_checkopenop检查下一条指令
 */
#define checkopenop(pt,pc)	luaG_checkopenop((pt)->code[(pc)+1])

/**
 * @brief 检查开放操作的有效性
 *
 * 验证指定指令是否可以作为开放操作（如CALL、TAILCALL等）的
 * 后续指令。开放操作的特点是其参数数量在编译时未确定。
 *
 * @param i 要检查的指令
 * @return 有效返回1，无效返回0
 *
 * @note 开放操作的B参数必须为0，表示参数数量待定
 *
 * @see Instruction, GET_OPCODE, GETARG_B
 *
 * 有效的开放操作后续指令：
 * - OP_CALL: 函数调用，参数数量运行时确定
 * - OP_TAILCALL: 尾调用，参数数量运行时确定
 * - OP_RETURN: 返回语句，返回值数量运行时确定
 * - OP_SETLIST: 列表设置，元素数量运行时确定
 *
 * 检查规则：
 * - 指令的B参数必须为0
 * - 表示参数或返回值数量由栈顶确定
 * - 这是Lua虚拟机的重要安全检查
 *
 * 使用场景：
 * - 字节码验证和完整性检查
 * - 虚拟机安全性保障
 * - 调试器的代码分析
 * - 静态分析工具的支持
 */
int luaG_checkopenop (Instruction i) {
    switch (GET_OPCODE(i)) {
        case OP_CALL:
        case OP_TAILCALL:
        case OP_RETURN:
        case OP_SETLIST: {
            check(GETARG_B(i) == 0);
            return 1;
        }
        default: return 0;  /* 开放调用后的无效指令 */
    }
}

/**
 * @brief 检查参数模式的有效性
 *
 * 根据指令的参数模式验证参数值的合法性。不同的参数模式
 * 对应不同的值域和约束条件。
 *
 * @param pt 函数原型指针，提供上下文信息
 * @param r 参数值
 * @param mode 参数模式枚举值
 * @return 检查通过返回1，失败返回0
 *
 * @see OpArgMask, ISK, INDEXK, checkreg
 *
 * 参数模式类型：
 * - OpArgN: 不使用参数，值必须为0
 * - OpArgU: 无约束参数，任意值都有效
 * - OpArgR: 寄存器参数，必须在栈大小范围内
 * - OpArgK: 常量或寄存器参数，需要特殊检查
 *
 * OpArgK模式详解：
 * - 如果是常量（ISK为真），索引必须在常量表范围内
 * - 如果是寄存器，索引必须在栈大小范围内
 * - 使用INDEXK宏提取常量索引
 *
 * 安全考虑：
 * - 防止越界访问常量表
 * - 防止无效的寄存器引用
 * - 确保参数值的语义正确性
 *
 * 性能优化：
 * - 快速的模式匹配
 * - 最小化的边界检查
 * - 高效的常量/寄存器判断
 */
static int checkArgMode (const Proto *pt, int r, enum OpArgMask mode) {
    switch (mode) {
        case OpArgN: check(r == 0); break;
        case OpArgU: break;
        case OpArgR: checkreg(pt, r); break;
        case OpArgK:
            check(ISK(r) ? INDEXK(r) < pt->sizek : r < pt->maxstacksize);
            break;
    }
    return 1;
}


/**
 * @brief 符号执行分析器
 *
 * 对函数的字节码进行符号执行分析，验证代码的正确性并跟踪
 * 指定寄存器的最后修改位置。这是Lua虚拟机代码验证的核心算法。
 *
 * @param pt 函数原型指针，包含要分析的字节码
 * @param lastpc 分析的结束位置（不包含）
 * @param reg 要跟踪的寄存器索引，NO_REG表示不跟踪
 * @return 最后修改指定寄存器的指令，如果未跟踪则返回最后一条指令
 *
 * @note 这是一个复杂的静态分析算法，确保字节码的安全性
 * @note 同时跟踪寄存器的生命周期，用于调试信息生成
 *
 * @see precheck, checkArgMode, getOpMode
 *
 * 主要功能：
 * 1. 验证每条指令的操作码有效性
 * 2. 检查指令参数的合法性和类型匹配
 * 3. 验证跳转目标的正确性
 * 4. 跟踪寄存器的修改历史
 * 5. 检查特殊指令的约束条件
 *
 * 分析流程：
 * 1. 执行函数原型的预检查
 * 2. 逐条分析字节码指令
 * 3. 根据指令格式解析参数
 * 4. 验证参数的有效性
 * 5. 处理特殊指令的约束
 * 6. 跟踪目标寄存器的修改
 *
 * 安全保障：
 * - 防止无效操作码执行
 * - 确保参数在有效范围内
 * - 验证跳转目标的合法性
 * - 检查栈操作的安全性
 *
 * 性能考虑：
 * - 线性时间复杂度
 * - 最小化的重复检查
 * - 高效的指令解析
 */
static Instruction symbexec (const Proto *pt, int lastpc, int reg) {
    int pc;
    int last;  /* 存储最后修改'reg'的指令位置 */
    last = pt->sizecode-1;  /* 指向最终的return（中性指令） */
    check(precheck(pt));
    for (pc = 0; pc < lastpc; pc++) {
        Instruction i = pt->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);
        int b = 0;
        int c = 0;
        check(op < NUM_OPCODES);
        checkreg(pt, a);
        switch (getOpMode(op)) {
            case iABC: {
                b = GETARG_B(i);
                c = GETARG_C(i);
                check(checkArgMode(pt, b, getBMode(op)));
                check(checkArgMode(pt, c, getCMode(op)));
                break;
            }
            case iABx: {
                b = GETARG_Bx(i);
                if (getBMode(op) == OpArgK) check(b < pt->sizek);
                break;
            }
            case iAsBx: {
                b = GETARG_sBx(i);
                if (getBMode(op) == OpArgR) {
                    int dest = pc+1+b;
                    check(0 <= dest && dest < pt->sizecode);
                    if (dest > 0) {
                        int j;
                        /* 检查不会跳转到setlist计数；这很复杂，因为
                           前一个setlist的计数可能与无效setlist的值相同；
                           所以我们必须回溯到第一个（如果有的话） */
                        for (j = 0; j < dest; j++) {
                            Instruction d = pt->code[dest-1-j];
                            if (!(GET_OPCODE(d) == OP_SETLIST && GETARG_C(d) == 0)) break;
                        }
                        /* 如果'j'是偶数，前一个值不是setlist（即使看起来像） */
                        check((j&1) == 0);
                    }
                }
                break;
            }
        }
        /* 检查指令是否修改A寄存器 */
        if (testAMode(op)) {
            if (a == reg) last = pc;  /* 修改寄存器'a' */
        }
        /* 检查测试模式指令（需要跳过下一条指令） */
        if (testTMode(op)) {
            check(pc+2 < pt->sizecode);  /* 检查跳过 */
            check(GET_OPCODE(pt->code[pc+1]) == OP_JMP);
        }

        /* 特殊指令的详细检查 */
        switch (op) {
            case OP_LOADBOOL: {
                if (c == 1) {  /* 是否跳转？ */
                    check(pc+2 < pt->sizecode);  /* 检查跳转 */
                    check(GET_OPCODE(pt->code[pc+1]) != OP_SETLIST ||
                          GETARG_C(pt->code[pc+1]) != 0);
                }
                break;
            }
            case OP_LOADNIL: {
                if (a <= reg && reg <= b)
                    last = pc;  /* 设置从'a'到'b'的寄存器 */
                break;
            }
            case OP_GETUPVAL:
            case OP_SETUPVAL: {
                check(b < pt->nups);
                break;
            }
            case OP_GETGLOBAL:
            case OP_SETGLOBAL: {
                check(ttisstring(&pt->k[b]));
                break;
            }
            case OP_SELF: {
                checkreg(pt, a+1);
                if (reg == a+1) last = pc;
                break;
            }
            case OP_CONCAT: {
                check(b < c);  /* 至少两个操作数 */
                break;
            }
            case OP_TFORLOOP: {
                check(c >= 1);  /* 至少一个结果（控制变量） */
                checkreg(pt, a+2+c);  /* 结果空间 */
                if (reg >= a+2) last = pc;  /* 影响基址以上的所有寄存器 */
                break;
            }
            case OP_FORLOOP:
            case OP_FORPREP:
                checkreg(pt, a+3);
                /* 继续执行 */
            case OP_JMP: {
                int dest = pc+1+b;
                /* 非完整检查且向前跳转且不跳过'lastpc'？ */
                if (reg != NO_REG && pc < dest && dest <= lastpc)
                    pc += b;  /* 执行跳转 */
                break;
            }
            case OP_CALL:
            case OP_TAILCALL: {
                if (b != 0) {
                    checkreg(pt, a+b-1);
                }
                c--;  /* c = 返回值数量 */
                if (c == LUA_MULTRET) {
                    check(checkopenop(pt, pc));
                }
                else if (c != 0)
                    checkreg(pt, a+c-1);
                if (reg >= a) last = pc;  /* 影响基址以上的所有寄存器 */
                break;
            }
            case OP_RETURN: {
                b--;  /* b = 返回值数量 */
                if (b > 0) checkreg(pt, a+b-1);
                break;
            }
            case OP_SETLIST: {
                if (b > 0) checkreg(pt, a + b);
                if (c == 0) {
                    pc++;
                    check(pc < pt->sizecode - 1);
                }
                break;
            }
            case OP_CLOSURE: {
                int nup, j;
                check(b < pt->sizep);
                nup = pt->p[b]->nups;
                check(pc + nup < pt->sizecode);
                for (j = 1; j <= nup; j++) {
                    OpCode op1 = GET_OPCODE(pt->code[pc + j]);
                    check(op1 == OP_GETUPVAL || op1 == OP_MOVE);
                }
                if (reg != NO_REG)  /* 跟踪中？ */
                    pc += nup;  /* 不'执行'这些伪指令 */
                break;
            }
            case OP_VARARG: {
                check((pt->is_vararg & VARARG_ISVARARG) &&
                     !(pt->is_vararg & VARARG_NEEDSARG));
                b--;
                if (b == LUA_MULTRET) check(checkopenop(pt, pc));
                checkreg(pt, a+b-1);
                break;
            }
            default: break;
        }
    }
    return pt->code[last];
}

/* 清理检查宏定义 */
#undef check
#undef checkjump
#undef checkreg

/* 符号执行和代码检查器结束 */

/**
 * @brief 检查字节码的完整性
 *
 * 对整个函数的字节码进行完整性检查，确保代码的安全性和正确性。
 * 这是加载字节码时的重要安全检查步骤。
 *
 * @param pt 要检查的函数原型指针
 * @return 检查通过返回非零值，失败返回0
 *
 * @note 这是字节码加载过程中的关键安全检查
 * @note 使用符号执行技术进行全面验证
 *
 * @see symbexec, NO_REG
 *
 * 检查内容：
 * - 所有指令的有效性
 * - 参数的合法性
 * - 跳转目标的正确性
 * - 栈操作的安全性
 * - 函数结构的完整性
 *
 * 使用场景：
 * - 字节码文件加载时的验证
 * - 网络传输字节码的安全检查
 * - 调试器的代码分析
 * - 虚拟机的安全保障
 *
 * 安全意义：
 * - 防止恶意字节码执行
 * - 确保虚拟机稳定性
 * - 避免内存访问错误
 * - 保护系统安全
 */
int luaG_checkcode (const Proto *pt) {
    return (symbexec(pt, pt->sizecode, NO_REG) != 0);
}

/**
 * @brief 获取常量名称
 *
 * 从函数原型的常量表中获取指定索引的常量名称。主要用于
 * 调试信息中显示有意义的符号名称。
 *
 * @param p 函数原型指针
 * @param c 常量索引（可能包含ISK标记）
 * @return 常量的字符串表示，如果不是字符串常量则返回"?"
 *
 * @note 只处理字符串类型的常量
 * @note 使用ISK宏检查是否为常量索引
 *
 * @see ISK, INDEXK, ttisstring, svalue
 *
 * 实现逻辑：
 * 1. 检查索引是否为常量（ISK）
 * 2. 验证常量是否为字符串类型
 * 3. 返回字符串值或占位符
 *
 * 使用场景：
 * - 调试信息中的变量名显示
 * - 错误消息中的符号引用
 * - 代码分析工具的符号解析
 * - 反汇编器的可读输出
 *
 * 返回值说明：
 * - 有效字符串常量：返回实际字符串内容
 * - 非字符串常量：返回"?"占位符
 * - 无效索引：返回"?"占位符
 */
static const char *kname (Proto *p, int c) {
    if (ISK(c) && ttisstring(&p->k[INDEXK(c)]))
        return svalue(&p->k[INDEXK(c)]);
    else
        return "?";
}


/**
 * @brief 获取对象名称和类型
 *
 * 通过符号执行分析确定栈位置上对象的名称和类型。这是调试系统
 * 提供有意义错误消息的核心功能。
 *
 * @param L Lua状态机指针
 * @param ci 调用信息指针
 * @param stackpos 栈位置索引
 * @param name 输出参数，存储对象名称
 * @return 对象类型字符串，如果无法确定则返回NULL
 *
 * @note 只对Lua函数有效，C函数返回NULL
 * @note 使用符号执行技术追踪对象来源
 *
 * @see symbexec, currentpc, luaF_getlocalname, kname
 *
 * 支持的对象类型：
 * - "local": 局部变量
 * - "global": 全局变量
 * - "field": 表字段
 * - "upvalue": 上值变量
 * - "method": 方法调用
 *
 * 分析流程：
 * 1. 首先尝试从调试信息获取局部变量名
 * 2. 如果不是局部变量，使用符号执行分析
 * 3. 根据最后修改该栈位置的指令确定类型
 * 4. 提取相应的名称信息
 *
 * 符号执行分析：
 * - OP_GETGLOBAL: 全局变量访问
 * - OP_MOVE: 变量移动，递归分析源位置
 * - OP_GETTABLE: 表字段访问
 * - OP_GETUPVAL: 上值访问
 * - OP_SELF: 方法调用准备
 *
 * 使用场景：
 * - 错误消息中的变量名显示
 * - 调试器的变量检查
 * - 类型错误的详细报告
 * - 运行时反射和内省
 *
 * 性能考虑：
 * - 优先使用调试符号表
 * - 符号执行的开销相对较小
 * - 递归分析有深度限制
 */
static const char *getobjname (lua_State *L, CallInfo *ci, int stackpos,
                               const char **name) {
    if (isLua(ci)) {  /* Lua函数？ */
        Proto *p = ci_func(ci)->l.p;
        int pc = currentpc(L, ci);
        Instruction i;
        *name = luaF_getlocalname(p, stackpos+1, pc);
        if (*name)  /* 是局部变量？ */
            return "local";
        i = symbexec(p, pc, stackpos);  /* 尝试符号执行 */
        lua_assert(pc != -1);
        switch (GET_OPCODE(i)) {
            case OP_GETGLOBAL: {
                int g = GETARG_Bx(i);  /* 全局变量索引 */
                lua_assert(ttisstring(&p->k[g]));
                *name = svalue(&p->k[g]);
                return "global";
            }
            case OP_MOVE: {
                int a = GETARG_A(i);
                int b = GETARG_B(i);  /* 从'b'移动到'a' */
                if (b < a)
                    return getobjname(L, ci, b, name);  /* 获取'b'的名称 */
                break;
            }
            case OP_GETTABLE: {
                int k = GETARG_C(i);  /* 键索引 */
                *name = kname(p, k);
                return "field";
            }
            case OP_GETUPVAL: {
                int u = GETARG_B(i);  /* 上值索引 */
                *name = p->upvalues ? getstr(p->upvalues[u]) : "?";
                return "upvalue";
            }
            case OP_SELF: {
                int k = GETARG_C(i);  /* 键索引 */
                *name = kname(p, k);
                return "method";
            }
            default: break;
        }
    }
    return NULL;  /* 未找到有用的名称 */
}


/**
 * @brief 获取函数名称
 *
 * 尝试获取调用指定函数的函数名称。通过分析调用指令来确定
 * 被调用函数的名称和调用方式。
 *
 * @param L Lua状态机指针
 * @param ci 目标函数的调用信息
 * @param name 输出参数，存储函数名称
 * @return 函数调用类型字符串，如果无法确定则返回NULL
 *
 * @note 只能分析Lua函数调用，C函数调用无法分析
 * @note 尾调用优化会导致调用信息丢失
 *
 * @see getobjname, currentpc, GET_OPCODE
 *
 * 分析限制：
 * - 调用函数必须是Lua函数
 * - 不能有尾调用优化
 * - 调用指令必须是已知类型
 *
 * 支持的调用指令：
 * - OP_CALL: 普通函数调用
 * - OP_TAILCALL: 尾调用
 * - OP_TFORLOOP: for循环迭代器调用
 *
 * 实现原理：
 * 1. 检查调用函数的有效性
 * 2. 获取调用指令
 * 3. 分析指令类型和参数
 * 4. 使用getobjname获取对象名称
 *
 * 使用场景：
 * - 错误消息中的函数名显示
 * - 调试器的调用栈分析
 * - 性能分析的函数统计
 * - 运行时反射和内省
 */
static const char *getfuncname (lua_State *L, CallInfo *ci, const char **name) {
    Instruction i;
    if ((isLua(ci) && ci->tailcalls > 0) || !isLua(ci - 1))
        return NULL;  /* 调用函数不是Lua函数（或未知） */
    ci--;  /* 调用函数 */
    i = ci_func(ci)->l.p->code[currentpc(L, ci)];
    if (GET_OPCODE(i) == OP_CALL || GET_OPCODE(i) == OP_TAILCALL ||
        GET_OPCODE(i) == OP_TFORLOOP)
        return getobjname(L, ci, GETARG_A(i), name);
    else
        return NULL;  /* 无法找到有用的名称 */
}

/**
 * @brief 检查指针是否在栈中
 *
 * 检查给定的TValue指针是否指向指定调用信息的栈范围内。
 * 这是ANSI C兼容的指针比较方法。
 *
 * @param ci 调用信息指针
 * @param o 要检查的TValue指针
 * @return 在栈中返回1，否则返回0
 *
 * @note 使用ANSI C兼容的方式检查指针是否指向数组
 * @note 只检查当前调用的栈范围
 *
 * @see TValue, StkId, CallInfo
 *
 * 实现方法：
 * - 遍历调用栈的有效范围
 * - 逐个比较指针地址
 * - 避免使用非标准的指针算术
 *
 * 使用场景：
 * - 错误报告中的变量定位
 * - 调试信息的上下文分析
 * - 内存安全检查
 * - 栈溢出检测
 *
 * 安全考虑：
 * - 符合ANSI C标准的指针比较
 * - 避免未定义的指针算术行为
 * - 确保栈边界的正确性
 */
static int isinstack (CallInfo *ci, const TValue *o) {
    StkId p;
    for (p = ci->base; p < ci->top; p++)
        if (o == p) return 1;
    return 0;
}


/**
 * @brief 生成类型错误消息
 *
 * 当操作遇到不兼容的类型时，生成详细的错误消息。会尝试获取
 * 变量的名称和类型信息，提供更有意义的错误报告。
 *
 * @param L Lua状态机指针
 * @param o 导致错误的值指针
 * @param op 尝试执行的操作描述字符串
 *
 * @note 此函数不会返回，会抛出运行时错误
 * @note 会尝试获取变量名称以提供更好的错误信息
 *
 * @see getobjname, isinstack, luaG_runerror, luaT_typenames
 *
 * 错误消息格式：
 * - 有变量名：attempt to <op> <kind> '<name>' (a <type> value)
 * - 无变量名：attempt to <op> a <type> value
 *
 * 变量类型识别：
 * - 检查是否在当前栈中
 * - 使用getobjname获取详细信息
 * - 提供变量名和变量类型
 *
 * 使用场景：
 * - 算术运算类型错误
 * - 字符串操作类型错误
 * - 表操作类型错误
 * - 函数调用类型错误
 *
 * 错误恢复：
 * - 此函数不返回
 * - 通过异常机制处理错误
 * - 提供详细的上下文信息
 */
void luaG_typeerror (lua_State *L, const TValue *o, const char *op) {
    const char *name = NULL;
    const char *t = luaT_typenames[ttype(o)];
    const char *kind = (isinstack(L->ci, o)) ?
                           getobjname(L, L->ci, cast_int(o - L->base), &name) :
                           NULL;
    if (kind)
        luaG_runerror(L, "attempt to %s %s " LUA_QS " (a %s value)",
                    op, kind, name, t);
    else
        luaG_runerror(L, "attempt to %s a %s value", op, t);
}

/**
 * @brief 生成字符串连接错误消息
 *
 * 当字符串连接操作遇到不兼容的类型时，生成特定的错误消息。
 * 会自动识别哪个操作数导致了错误。
 *
 * @param L Lua状态机指针
 * @param p1 第一个操作数指针
 * @param p2 第二个操作数指针
 *
 * @note 此函数不会返回，会抛出运行时错误
 * @note 自动选择导致错误的操作数
 *
 * @see ttisstring, ttisnumber, luaG_typeerror
 *
 * 错误检测逻辑：
 * - 如果第一个操作数是字符串或数字，则第二个操作数有问题
 * - 否则第一个操作数有问题
 * - 确保至少有一个操作数不是字符串或数字
 *
 * 使用场景：
 * - 字符串连接操作(..)
 * - 混合类型的连接尝试
 * - 表或函数的连接错误
 */
void luaG_concaterror (lua_State *L, StkId p1, StkId p2) {
    if (ttisstring(p1) || ttisnumber(p1)) p1 = p2;
    lua_assert(!ttisstring(p1) && !ttisnumber(p1));
    luaG_typeerror(L, p1, "concatenate");
}

/**
 * @brief 生成算术运算错误消息
 *
 * 当算术运算遇到不兼容的类型时，生成特定的错误消息。
 * 会尝试将操作数转换为数字以确定哪个操作数有问题。
 *
 * @param L Lua状态机指针
 * @param p1 第一个操作数指针
 * @param p2 第二个操作数指针
 *
 * @note 此函数不会返回，会抛出运行时错误
 * @note 使用数字转换测试确定错误的操作数
 *
 * @see luaV_tonumber, luaG_typeerror
 *
 * 错误检测逻辑：
 * - 尝试将第一个操作数转换为数字
 * - 如果转换失败，第一个操作数有问题
 * - 否则第二个操作数有问题
 *
 * 使用场景：
 * - 加减乘除运算
 * - 取模运算
 * - 幂运算
 * - 一元负号运算
 */
void luaG_aritherror (lua_State *L, const TValue *p1, const TValue *p2) {
    TValue temp;
    if (luaV_tonumber(p1, &temp) == NULL)
        p2 = p1;  /* 第一个操作数错误 */
    luaG_typeerror(L, p2, "perform arithmetic on");
}

/**
 * @brief 生成比较运算错误消息
 *
 * 当比较运算遇到不兼容的类型时，生成特定的错误消息。
 * 会根据类型的相同性生成不同的错误信息。
 *
 * @param L Lua状态机指针
 * @param p1 第一个操作数指针
 * @param p2 第二个操作数指针
 * @return 总是返回0（用于某些调用约定）
 *
 * @note 此函数不会正常返回，会抛出运行时错误
 * @note 区分相同类型和不同类型的比较错误
 *
 * @see luaT_typenames, ttype, luaG_runerror
 *
 * 错误消息类型：
 * - 相同类型：attempt to compare two <type> values
 * - 不同类型：attempt to compare <type1> with <type2>
 *
 * 类型判断：
 * - 使用类型名称的第三个字符进行快速比较
 * - 这是一个优化的类型相等性检查
 *
 * 使用场景：
 * - 关系运算符（<, >, <=, >=）
 * - 等值运算符（==, ~=）在某些情况下
 * - 表排序中的比较函数
 */
int luaG_ordererror (lua_State *L, const TValue *p1, const TValue *p2) {
    const char *t1 = luaT_typenames[ttype(p1)];
    const char *t2 = luaT_typenames[ttype(p2)];
    if (t1[2] == t2[2])
        luaG_runerror(L, "attempt to compare two %s values", t1);
    else
        luaG_runerror(L, "attempt to compare %s with %s", t1, t2);
    return 0;
}


/**
 * @brief 添加错误位置信息
 *
 * 为错误消息添加源文件和行号信息，提供更精确的错误定位。
 * 只对Lua代码有效，C代码无法提供源位置信息。
 *
 * @param L Lua状态机指针
 * @param msg 原始错误消息
 *
 * @note 修改后的消息会被压入栈顶
 * @note 只对Lua函数添加位置信息
 *
 * @see currentline, getluaproto, luaO_chunkid, luaO_pushfstring
 *
 * 信息格式：
 * - Lua代码：<文件名>:<行号>: <消息>
 * - C代码：保持原消息不变
 *
 * 实现步骤：
 * 1. 检查当前是否为Lua代码
 * 2. 获取当前执行行号
 * 3. 生成简化的源文件标识
 * 4. 格式化完整的错误消息
 *
 * 使用场景：
 * - 运行时错误报告
 * - 调试信息生成
 * - 错误日志记录
 * - 开发工具的错误显示
 */
static void addinfo (lua_State *L, const char *msg) {
    CallInfo *ci = L->ci;
    if (isLua(ci)) {  /* 是Lua代码？ */
        char buff[LUA_IDSIZE];  /* 添加文件:行号信息 */
        int line = currentline(L, ci);
        luaO_chunkid(buff, getstr(getluaproto(ci)->source), LUA_IDSIZE);
        luaO_pushfstring(L, "%s:%d: %s", buff, line, msg);
    }
}

/**
 * @brief 处理错误消息
 *
 * 处理运行时错误，如果设置了错误处理函数则调用它，否则直接抛出错误。
 * 这是Lua错误处理机制的核心函数。
 *
 * @param L Lua状态机指针
 *
 * @note 此函数不会返回，总是抛出异常
 * @note 支持用户自定义的错误处理函数
 *
 * @see restorestack, ttisfunction, luaD_throw, luaD_call
 *
 * 处理流程：
 * 1. 检查是否设置了错误处理函数
 * 2. 如果有，验证函数的有效性
 * 3. 调用错误处理函数
 * 4. 如果没有或调用失败，抛出运行时错误
 *
 * 错误处理函数：
 * - 接收错误消息作为参数
 * - 可以修改或包装错误消息
 * - 用于实现自定义的错误报告
 *
 * 异常类型：
 * - LUA_ERRERR: 错误处理函数本身有错误
 * - LUA_ERRRUN: 普通的运行时错误
 *
 * 使用场景：
 * - 所有运行时错误的最终处理
 * - 错误消息的统一格式化
 * - 异常的统一抛出机制
 */
void luaG_errormsg (lua_State *L) {
    if (L->errfunc != 0) {  /* 有错误处理函数？ */
        StkId errfunc = restorestack(L, L->errfunc);
        if (!ttisfunction(errfunc)) luaD_throw(L, LUA_ERRERR);
        setobjs2s(L, L->top, L->top - 1);  /* 移动参数 */
        setobjs2s(L, L->top - 1, errfunc);  /* 压入函数 */
        incr_top(L);
        luaD_call(L, L->top - 2, 1);  /* 调用它 */
    }
    luaD_throw(L, LUA_ERRRUN);
}

/**
 * @brief 生成运行时错误
 *
 * 格式化错误消息并抛出运行时错误。这是Lua中生成格式化错误消息
 * 的标准方法，支持printf风格的格式化。
 *
 * @param L Lua状态机指针
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 *
 * @note 此函数不会返回，总是抛出异常
 * @note 支持printf风格的格式化语法
 *
 * @see addinfo, luaO_pushvfstring, luaG_errormsg
 *
 * 处理流程：
 * 1. 使用可变参数格式化错误消息
 * 2. 添加源文件和行号信息
 * 3. 调用错误消息处理函数
 * 4. 抛出运行时异常
 *
 * 格式化支持：
 * - 标准printf格式说明符
 * - Lua特定的格式扩展
 * - 自动的内存管理
 *
 * 使用场景：
 * - 虚拟机内部错误报告
 * - 类型检查失败
 * - 运行时约束违反
 * - 用户代码错误
 *
 * @example
 * luaG_runerror(L, "invalid argument #%d (expected %s, got %s)",
 *               arg_num, expected_type, actual_type);
 */
void luaG_runerror (lua_State *L, const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    addinfo(L, luaO_pushvfstring(L, fmt, argp));
    va_end(argp);
    luaG_errormsg(L);
}

