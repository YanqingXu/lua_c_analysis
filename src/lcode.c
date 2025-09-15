/**
 * @file lcode.c
 * @brief Lua代码生成器：编译器后端核心模块
 *
 * 版权信息：
 * $Id: lcode.c,v 2.25.1.5 2011/01/31 14:53:16 roberto Exp $
 * Lua代码生成器
 * 版权声明见lua.h文件
 *
 * 模块概述：
 * 本模块是Lua编译器的代码生成器，负责将语法分析器产生的抽象语法树
 * 转换为Lua虚拟机可执行的字节码。这是编译器后端的核心组件，实现了
 * 从高级语言结构到低级字节码指令的转换。
 *
 * 主要功能：
 * 1. 表达式代码生成：算术、逻辑、比较、函数调用等表达式的字节码生成
 * 2. 语句代码生成：赋值、控制流、循环等语句的字节码生成
 * 3. 寄存器分配：虚拟机寄存器的分配和管理
 * 4. 跳转处理：条件跳转、循环跳转、函数调用跳转的生成和修正
 * 5. 常量管理：常量池的维护和常量引用的生成
 * 6. 局部变量：局部变量的寄存器分配和生命周期管理
 * 7. 代码优化：窥孔优化、死代码消除等基本优化技术
 *
 * 核心数据结构：
 * - FuncState：函数编译状态，包含寄存器分配、跳转列表等信息
 * - expdesc：表达式描述符，记录表达式的类型、值、跳转信息
 * - Instruction：字节码指令，Lua虚拟机的基本执行单元
 *
 * 设计特点：
 * 1. 单遍代码生成：在语法分析的同时生成代码，提高编译效率
 * 2. 寄存器虚拟机：针对寄存器架构优化的代码生成策略
 * 3. 延迟跳转修正：使用跳转链表延迟处理跳转目标
 * 4. 表达式优化：常量折叠、短路求值等表达式级优化
 * 5. 紧凑字节码：生成高效紧凑的字节码指令序列
 *
 * 编译器架构中的地位：
 * lcode.c是Lua编译器的核心后端，与lparser.c（语法分析器）紧密配合，
 * 将语法结构转换为可执行的字节码。它是连接高级语言语义和虚拟机
 * 执行模型的关键桥梁。
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2008-2011
 *
 * @note 本模块实现了完整的代码生成功能，是理解编译器后端设计的重要参考
 * @see lparser.h, lopcodes.h, lvm.c
 */

#include <stdlib.h>

#define lcode_c
#define LUA_CORE

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "ltable.h"

/**
 * @brief 检查表达式是否有跳转的宏
 *
 * 判断表达式是否包含条件跳转，即真跳转列表和假跳转列表是否不同。
 * 这是代码生成中判断表达式复杂度的重要标准。
 *
 * @param e 表达式描述符指针
 * @return 如果有跳转返回非零值，否则返回0
 *
 * @note 有跳转的表达式需要特殊的代码生成处理
 * @note 用于优化简单表达式的代码生成
 */
#define hasjumps(e)	((e)->t != (e)->f)

/**
 * @brief 检查表达式是否为纯数值常量
 *
 * 判断表达式是否为不包含任何跳转的纯数值常量。这种表达式
 * 可以进行常量折叠优化，无需生成运行时代码。
 *
 * @param e 表达式描述符指针
 * @return 如果是纯数值常量返回1，否则返回0
 *
 * @note 纯数值常量是最简单的表达式形式
 * @note 用于常量折叠和编译时计算优化
 *
 * @see expdesc, VKNUM, NO_JUMP
 *
 * 判断条件：
 * 1. 表达式类型为VKNUM（数值常量）
 * 2. 没有真跳转（t == NO_JUMP）
 * 3. 没有假跳转（f == NO_JUMP）
 *
 * 使用场景：
 * - 常量折叠优化
 * - 编译时表达式求值
 * - 简化代码生成逻辑
 * - 优化算术运算
 */
static int isnumeral(expdesc *e) {
    return (e->k == VKNUM && e->t == NO_JUMP && e->f == NO_JUMP);
}


/**
 * @brief 生成nil值初始化代码
 *
 * 为指定范围的寄存器生成nil值初始化的字节码。这个函数实现了
 * 重要的窥孔优化，能够合并相邻的LOADNIL指令以减少代码大小。
 *
 * @param fs 函数编译状态指针
 * @param from 起始寄存器索引
 * @param n 要初始化的寄存器数量
 *
 * @note 这是代码生成器中的重要优化函数
 * @note 能够显著减少生成的字节码数量
 *
 * @see luaK_codeABC, OP_LOADNIL, FuncState
 *
 * 优化策略：
 * 1. 检查是否可以与前一条LOADNIL指令合并
 * 2. 如果可以合并，扩展前一条指令的范围
 * 3. 否则生成新的LOADNIL指令
 *
 * 合并条件：
 * - 当前位置没有跳转目标
 * - 前一条指令是LOADNIL
 * - 寄存器范围可以连接
 *
 * 特殊情况处理：
 * - 函数开始时，某些寄存器可能已经是nil
 * - 有跳转目标的位置不能进行优化
 *
 * 性能影响：
 * - 减少字节码指令数量
 * - 提高虚拟机执行效率
 * - 降低内存使用
 *
 * 使用场景：
 * - 局部变量声明初始化
 * - 函数参数默认值设置
 * - 临时寄存器清理
 * - 数组/表的nil元素初始化
 *
 * @example
 * // Lua代码: local a, b, c
 * // 可能生成: LOADNIL 0 2 0  (初始化寄存器0-2为nil)
 * // 而不是三条单独的LOADNIL指令
 */
void luaK_nil (FuncState *fs, int from, int n) {
    Instruction *previous;
    if (fs->pc > fs->lasttarget) {  /* 当前位置没有跳转？ */
        if (fs->pc == 0) {  /* 函数开始？ */
            if (from >= fs->nactvar)
                return;  /* 位置已经是干净的 */
        }
        else {
            previous = &fs->f->code[fs->pc-1];
            if (GET_OPCODE(*previous) == OP_LOADNIL) {
                int pfrom = GETARG_A(*previous);
                int pto = GETARG_B(*previous);
                if (pfrom <= from && from <= pto+1) {  /* 可以连接两者？ */
                    if (from+n-1 > pto)
                        SETARG_B(*previous, from+n-1);
                    return;
                }
            }
        }
    }
    luaK_codeABC(fs, OP_LOADNIL, from, from+n-1, 0);  /* 否则无优化 */
}


/**
 * @brief 生成无条件跳转指令
 *
 * 生成一个无条件跳转指令，并管理跳转链表。这是控制流生成的
 * 核心函数，用于实现循环、条件语句等控制结构。
 *
 * @param fs 函数编译状态指针
 * @return 新生成的跳转指令的程序计数器位置
 *
 * @note 使用跳转链表延迟处理跳转目标
 * @note 返回值用于后续的跳转目标修正
 *
 * @see luaK_codeAsBx, luaK_concat, OP_JMP
 *
 * 实现逻辑：
 * 1. 保存当前的跳转链表
 * 2. 清空跳转链表
 * 3. 生成JMP指令（目标暂时未知）
 * 4. 将新跳转加入保存的链表
 * 5. 返回跳转指令位置
 *
 * 跳转链表管理：
 * - jpc：跳转到当前位置的指令链表
 * - 新跳转指令暂时指向NO_JUMP
 * - 通过luaK_concat管理跳转链表
 *
 * 使用场景：
 * - break语句的跳转
 * - continue语句的跳转
 * - 函数结尾的跳转
 * - 条件语句的无条件分支
 */
int luaK_jump (FuncState *fs) {
    int jpc = fs->jpc;  /* 保存跳转到这里的列表 */
    int j;
    fs->jpc = NO_JUMP;
    j = luaK_codeAsBx(fs, OP_JMP, 0, NO_JUMP);
    luaK_concat(fs, &j, jpc);  /* 保持它们等待 */
    return j;
}

/**
 * @brief 生成函数返回指令
 *
 * 生成RETURN指令，用于函数返回。处理返回值的寄存器范围
 * 和返回值数量的编码。
 *
 * @param fs 函数编译状态指针
 * @param first 第一个返回值所在的寄存器
 * @param nret 返回值数量（-1表示返回到栈顶的所有值）
 *
 * @note nret+1编码：0表示无返回值，1表示1个返回值，以此类推
 * @note -1表示返回可变数量的值（到栈顶）
 *
 * @see luaK_codeABC, OP_RETURN
 *
 * 返回值编码：
 * - nret = 0: 无返回值
 * - nret = 1: 一个返回值
 * - nret = -1: 返回到栈顶的所有值
 *
 * 使用场景：
 * - return语句
 * - 函数结尾的隐式返回
 * - 表达式函数的返回
 */
void luaK_ret (FuncState *fs, int first, int nret) {
    luaK_codeABC(fs, OP_RETURN, first, nret+1, 0);
}

/**
 * @brief 生成条件跳转指令
 *
 * 生成一个条件跳转指令，然后立即生成一个无条件跳转。
 * 这是实现复杂条件表达式的基础函数。
 *
 * @param fs 函数编译状态指针
 * @param op 条件跳转操作码
 * @param A 第一个操作数（寄存器）
 * @param B 第二个操作数（寄存器或常量）
 * @param C 第三个操作数（寄存器或常量）
 * @return 无条件跳转指令的位置
 *
 * @note 这是内部辅助函数，用于复杂条件表达式
 * @note 条件跳转后紧跟无条件跳转是常见模式
 *
 * @see luaK_codeABC, luaK_jump
 *
 * 指令序列：
 * 1. 条件跳转指令（如TEST、EQ等）
 * 2. 无条件跳转指令（JMP）
 *
 * 使用场景：
 * - 逻辑运算符（and、or）
 * - 比较运算符（==、<、>等）
 * - 条件表达式的短路求值
 */
static int condjump (FuncState *fs, OpCode op, int A, int B, int C) {
    luaK_codeABC(fs, op, A, B, C);
    return luaK_jump(fs);
}

/**
 * @brief 修正跳转指令的目标地址
 *
 * 将跳转指令的目标地址从占位符修改为实际的目标位置。
 * 这是跳转指令生成的最后步骤。
 *
 * @param fs 函数编译状态指针
 * @param pc 跳转指令的位置
 * @param dest 跳转目标位置
 *
 * @note 检查跳转距离是否超出指令编码范围
 * @note 跳转偏移是相对于下一条指令的
 *
 * @see SETARG_sBx, MAXARG_sBx, luaX_syntaxerror
 *
 * 偏移计算：
 * - offset = dest - (pc + 1)
 * - 相对于跳转指令的下一条指令
 * - 支持正向和反向跳转
 *
 * 错误检查：
 * - 跳转距离不能超过MAXARG_sBx
 * - 超出范围时报告语法错误
 *
 * 使用场景：
 * - 循环结构的跳转修正
 * - 条件语句的跳转修正
 * - break/continue的跳转修正
 */
static void fixjump (FuncState *fs, int pc, int dest) {
    Instruction *jmp = &fs->f->code[pc];
    int offset = dest-(pc+1);
    lua_assert(dest != NO_JUMP);
    if (abs(offset) > MAXARG_sBx)
        luaX_syntaxerror(fs->ls, "control structure too long");
    SETARG_sBx(*jmp, offset);
}


/**
 * @brief 获取当前标签位置
 *
 * 返回当前程序计数器位置并将其标记为跳转目标。这样可以避免
 * 对不在同一基本块中的连续指令进行错误的优化。
 *
 * @param fs 函数编译状态指针
 * @return 当前程序计数器位置
 *
 * @note 标记跳转目标是为了防止窥孔优化破坏控制流
 * @note lasttarget用于跟踪最后一个跳转目标位置
 *
 * @see FuncState, luaK_nil
 *
 * 基本块概念：
 * - 基本块是没有跳转进入和跳出的指令序列
 * - 跳转目标标记基本块的边界
 * - 优化不能跨越基本块边界
 *
 * 使用场景：
 * - 循环开始位置
 * - 条件语句的分支目标
 * - break/continue的跳转目标
 * - 函数调用后的返回点
 */
int luaK_getlabel (FuncState *fs) {
    fs->lasttarget = fs->pc;
    return fs->pc;
}

/**
 * @brief 获取跳转指令的目标位置
 *
 * 从跳转指令中提取目标位置，将相对偏移转换为绝对位置。
 * 这是跳转链表遍历的基础函数。
 *
 * @param fs 函数编译状态指针
 * @param pc 跳转指令的位置
 * @return 跳转目标的绝对位置，或NO_JUMP表示链表结束
 *
 * @note NO_JUMP偏移表示跳转链表的结束
 * @note 偏移是相对于跳转指令下一条指令的
 *
 * @see GETARG_sBx, NO_JUMP
 *
 * 偏移转换：
 * - 相对偏移：存储在指令中的sBx字段
 * - 绝对位置：(pc+1) + offset
 * - NO_JUMP：特殊值，表示链表结束
 *
 * 链表结构：
 * - 跳转指令形成单向链表
 * - 每个节点指向下一个跳转
 * - NO_JUMP标记链表结束
 */
static int getjump (FuncState *fs, int pc) {
    int offset = GETARG_sBx(fs->f->code[pc]);
    if (offset == NO_JUMP)  /* 指向自己表示列表结束 */
        return NO_JUMP;  /* 列表结束 */
    else
        return (pc+1)+offset;  /* 将偏移转换为绝对位置 */
}

/**
 * @brief 获取跳转控制指令
 *
 * 获取控制跳转行为的指令。某些指令（如TEST模式指令）会影响
 * 后续跳转指令的行为，需要特殊处理。
 *
 * @param fs 函数编译状态指针
 * @param pc 跳转指令的位置
 * @return 控制跳转的指令指针
 *
 * @note TEST模式指令会影响跳转行为
 * @note 返回实际控制跳转的指令
 *
 * @see testTMode, GET_OPCODE
 *
 * TEST模式处理：
 * - 某些指令有TEST模式变体
 * - TEST模式指令影响后续跳转
 * - 需要返回TEST指令而不是跳转指令
 *
 * 使用场景：
 * - 条件表达式的优化
 * - 跳转指令的修补
 * - 值产生检查
 */
static Instruction *getjumpcontrol (FuncState *fs, int pc) {
    Instruction *pi = &fs->f->code[pc];
    if (pc >= 1 && testTMode(GET_OPCODE(*(pi-1))))
        return pi-1;
    else
        return pi;
}

/**
 * @brief 检查跳转列表是否需要产生值
 *
 * 检查跳转列表中是否有不产生值（或产生反转值）的跳转。
 * 这用于优化条件表达式的代码生成。
 *
 * @param fs 函数编译状态指针
 * @param list 跳转列表的头节点
 * @return 如果需要产生值返回1，否则返回0
 *
 * @note TESTSET指令会产生值，其他跳转指令不会
 * @note 用于优化逻辑表达式的代码生成
 *
 * @see getjump, getjumpcontrol, OP_TESTSET
 *
 * 值产生分析：
 * - TESTSET：测试并设置值
 * - 其他跳转：只跳转，不产生值
 * - 影响寄存器分配和代码生成
 *
 * 优化意义：
 * - 避免不必要的值复制
 * - 优化逻辑表达式求值
 * - 减少寄存器使用
 */
static int need_value (FuncState *fs, int list) {
    for (; list != NO_JUMP; list = getjump(fs, list)) {
        Instruction i = *getjumpcontrol(fs, list);
        if (GET_OPCODE(i) != OP_TESTSET) return 1;
    }
    return 0;  /* 未找到 */
}

/**
 * @brief 修补测试寄存器指令
 *
 * 修改TESTSET指令的目标寄存器，或将其转换为TEST指令。
 * 这是条件表达式优化的重要函数。
 *
 * @param fs 函数编译状态指针
 * @param node 跳转节点位置
 * @param reg 新的目标寄存器（NO_REG表示不需要值）
 * @return 如果成功修补返回1，否则返回0
 *
 * @note 只能修补TESTSET指令
 * @note reg为NO_REG时转换为TEST指令
 *
 * @see getjumpcontrol, OP_TESTSET, OP_TEST
 *
 * 修补逻辑：
 * 1. 检查是否为TESTSET指令
 * 2. 如果需要新寄存器且不同，修改目标
 * 3. 如果不需要值，转换为TEST指令
 *
 * 指令转换：
 * - TESTSET：测试并设置值到寄存器
 * - TEST：只测试，不设置值
 * - 根据是否需要值选择指令类型
 */
static int patchtestreg (FuncState *fs, int node, int reg) {
    Instruction *i = getjumpcontrol(fs, node);
    if (GET_OPCODE(*i) != OP_TESTSET)
        return 0;  /* 不能修补其他指令 */
    if (reg != NO_REG && reg != GETARG_B(*i))
        SETARG_A(*i, reg);
    else  /* 没有寄存器放值或寄存器已经有值 */
        *i = CREATE_ABC(OP_TEST, GETARG_B(*i), 0, GETARG_C(*i));

    return 1;
}

/**
 * @brief 移除跳转列表中的值产生
 *
 * 遍历跳转列表，将所有TESTSET指令转换为TEST指令，
 * 移除值产生行为。
 *
 * @param fs 函数编译状态指针
 * @param list 跳转列表的头节点
 *
 * @note 用于不需要值的条件表达式
 * @note 通过patchtestreg实现指令转换
 *
 * @see patchtestreg, getjump
 *
 * 使用场景：
 * - if语句的条件（不需要值）
 * - while循环的条件
 * - 逻辑表达式的短路求值
 *
 * 优化效果：
 * - 减少不必要的值复制
 * - 简化指令序列
 * - 提高执行效率
 */
static void removevalues (FuncState *fs, int list) {
    for (; list != NO_JUMP; list = getjump(fs, list))
        patchtestreg(fs, list, NO_REG);
}


/**
 * @defgroup JumpListManagement 跳转链表管理
 * @brief 跳转链表的核心操作函数集合
 *
 * 跳转链表是代码生成器中的关键数据结构，用于管理需要延迟修正的跳转指令。
 * 在编译过程中，很多跳转的目标位置在生成跳转指令时还未确定，需要使用
 * 链表结构将这些跳转串联起来，等到目标位置确定后再统一修正。
 *
 * 核心概念：
 * - 跳转链表：将相同目标的跳转指令链接成单向链表
 * - 延迟修正：跳转目标未知时先生成占位符，后续统一修正
 * - 链表合并：将多个跳转链表合并为一个
 * - 条件修补：根据指令类型选择不同的跳转目标
 *
 * 数据结构：
 * - 每个跳转指令的sBx字段存储到下一个跳转的偏移
 * - NO_JUMP表示链表结束
 * - 链表头存储在各种跳转列表变量中
 * @{
 */

/**
 * @brief 跳转链表修补的辅助函数
 *
 * 遍历跳转链表，根据指令类型将跳转修正到不同的目标位置。
 * 这是跳转链表管理的核心算法，支持条件跳转的双目标修正。
 *
 * @param fs 函数编译状态指针
 * @param list 跳转链表的头节点
 * @param vtarget 值目标位置（用于TESTSET指令）
 * @param reg 目标寄存器（用于TESTSET指令的修补）
 * @param dtarget 默认目标位置（用于其他跳转指令）
 *
 * @note 这是内部辅助函数，实现跳转链表的批量修正
 * @note 支持条件跳转的双目标处理
 *
 * @see patchtestreg, fixjump, getjump
 *
 * 算法逻辑：
 * 1. 遍历整个跳转链表
 * 2. 对每个跳转节点尝试修补测试寄存器
 * 3. 如果修补成功，跳转到值目标
 * 4. 否则跳转到默认目标
 * 5. 继续处理链表中的下一个节点
 *
 * 双目标机制：
 * - vtarget：用于需要产生值的跳转（TESTSET）
 * - dtarget：用于不产生值的跳转（TEST、其他）
 * - 根据指令类型自动选择合适的目标
 *
 * 性能特点：
 * - 单次遍历完成所有修正
 * - 避免重复的链表遍历
 * - 支持复杂的条件跳转优化
 */
static void patchlistaux (FuncState *fs, int list, int vtarget, int reg,
                          int dtarget) {
    while (list != NO_JUMP) {
        int next = getjump(fs, list);
        if (patchtestreg(fs, list, reg))
            fixjump(fs, list, vtarget);
        else
            fixjump(fs, list, dtarget);  /* 跳转到默认目标 */
        list = next;
    }
}

/**
 * @brief 释放待处理跳转链表
 *
 * 将函数状态中的待处理跳转链表（jpc）中的所有跳转修正到当前位置，
 * 然后清空链表。这通常在代码生成的关键点调用。
 *
 * @param fs 函数编译状态指针
 *
 * @note jpc存储跳转到当前位置的指令链表
 * @note 调用后jpc被清空，准备接收新的跳转
 *
 * @see patchlistaux, FuncState::jpc
 *
 * 使用时机：
 * - 基本块边界处
 * - 标签定义处
 * - 控制流汇合点
 *
 * 实现细节：
 * - 所有跳转都指向当前pc位置
 * - 不需要寄存器修补（NO_REG）
 * - 值目标和默认目标都是当前位置
 */
static void dischargejpc (FuncState *fs) {
    patchlistaux(fs, fs->jpc, fs->pc, NO_REG, fs->pc);
    fs->jpc = NO_JUMP;
}

/**
 * @brief 修补跳转链表到指定目标
 *
 * 将跳转链表中的所有跳转修正到指定的目标位置。这是跳转链表
 * 管理的主要接口函数。
 *
 * @param fs 函数编译状态指针
 * @param list 跳转链表的头节点
 * @param target 跳转目标位置
 *
 * @note 如果目标是当前位置，使用更高效的patchtohere
 * @note 目标位置必须小于当前pc（向后跳转）
 *
 * @see luaK_patchtohere, patchlistaux
 *
 * 优化策略：
 * - 目标为当前位置时，使用patchtohere优化
 * - 其他情况使用通用的patchlistaux
 * - 自动选择最高效的修补方式
 *
 * 使用场景：
 * - 循环结构的跳转修正
 * - 条件语句的分支修正
 * - break/continue语句的跳转修正
 *
 * 安全检查：
 * - 确保目标位置已经生成（target < fs->pc）
 * - 防止向前跳转到未生成的代码
 */
void luaK_patchlist (FuncState *fs, int list, int target) {
    if (target == fs->pc)
        luaK_patchtohere(fs, list);
    else {
        lua_assert(target < fs->pc);
        patchlistaux(fs, list, target, NO_REG, target);
    }
}

/**
 * @brief 修补跳转链表到当前位置
 *
 * 将跳转链表修正到当前位置，并将其合并到待处理跳转链表中。
 * 这是处理跳转到当前位置的优化函数。
 *
 * @param fs 函数编译状态指针
 * @param list 跳转链表的头节点
 *
 * @note 自动获取当前标签位置
 * @note 将跳转链表合并到jpc中延迟处理
 *
 * @see luaK_getlabel, luaK_concat
 *
 * 优化原理：
 * - 跳转到当前位置的指令可以延迟修正
 * - 合并到jpc中统一处理，提高效率
 * - 避免立即修正，支持进一步优化
 *
 * 使用场景：
 * - 循环开始位置的跳转
 * - 条件语句汇合点的跳转
 * - 标签定义处的跳转修正
 *
 * 处理流程：
 * 1. 获取当前位置作为标签
 * 2. 将输入链表合并到jpc
 * 3. 等待后续统一处理
 */
void luaK_patchtohere (FuncState *fs, int list) {
    luaK_getlabel(fs);
    luaK_concat(fs, &fs->jpc, list);
}

/**
 * @brief 连接两个跳转链表
 *
 * 将两个跳转链表连接成一个链表。这是跳转链表管理的基础操作，
 * 用于合并具有相同目标的跳转。
 *
 * @param fs 函数编译状态指针
 * @param l1 第一个链表的头指针（输入输出参数）
 * @param l2 第二个链表的头节点
 *
 * @note l1是指针，函数会修改其值
 * @note l2为NO_JUMP时不进行任何操作
 *
 * @see getjump, fixjump
 *
 * 连接算法：
 * 1. 如果l2为空，直接返回
 * 2. 如果l1为空，l1指向l2
 * 3. 否则找到l1的尾节点
 * 4. 将尾节点连接到l2
 *
 * 链表结构：
 * - 单向链表，通过跳转指令的sBx字段连接
 * - NO_JUMP表示链表结束
 * - 连接后形成更长的链表
 *
 * 性能考虑：
 * - 需要遍历l1找到尾节点
 * - 时间复杂度O(n)，n为l1的长度
 * - 空间复杂度O(1)，原地操作
 *
 * 使用场景：
 * - 合并条件表达式的跳转
 * - 组合循环和分支的跳转
 * - 构建复杂控制流的跳转链表
 */
void luaK_concat (FuncState *fs, int *l1, int l2) {
    if (l2 == NO_JUMP) return;
    else if (*l1 == NO_JUMP)
        *l1 = l2;
    else {
        int list = *l1;
        int next;
        while ((next = getjump(fs, list)) != NO_JUMP)  /* 找到最后一个元素 */
            list = next;
        fixjump(fs, list, l2);
    }
}

/** @} */ /* 结束跳转链表管理文档组 */


/**
 * @defgroup RegisterManagement 寄存器管理
 * @brief 虚拟机寄存器的分配和管理系统
 *
 * Lua虚拟机采用寄存器架构，需要高效的寄存器分配和管理。
 * 寄存器管理系统负责跟踪寄存器的使用状态，分配临时寄存器，
 * 释放不再使用的寄存器，并确保栈空间的充足性。
 *
 * 核心概念：
 * - 寄存器栈：虚拟机使用栈式寄存器模型
 * - 自由寄存器：当前可分配的第一个寄存器索引
 * - 活跃变量：当前作用域中的局部变量数量
 * - 栈检查：确保寄存器使用不超过虚拟机限制
 *
 * 分配策略：
 * - 栈式分配：寄存器按栈顺序分配和释放
 * - 临时寄存器：表达式计算使用的临时存储
 * - 局部变量：固定分配的寄存器，不能释放
 * - 常量优化：常量不占用寄存器空间
 * @{
 */

/**
 * @brief 检查栈空间是否充足
 *
 * 检查当前函数是否有足够的栈空间来分配指定数量的寄存器。
 * 如果空间不足，更新函数的最大栈大小；如果超出虚拟机限制，
 * 报告语法错误。
 *
 * @param fs 函数编译状态指针
 * @param n 需要的额外寄存器数量
 *
 * @note 虚拟机栈大小有硬限制MAXSTACK
 * @note 自动更新函数的maxstacksize字段
 *
 * @see MAXSTACK, luaX_syntaxerror, FuncState::freereg
 *
 * 检查逻辑：
 * 1. 计算新的栈大小需求
 * 2. 检查是否超出函数当前最大栈大小
 * 3. 检查是否超出虚拟机硬限制
 * 4. 更新函数的最大栈大小记录
 *
 * 错误处理：
 * - 超出MAXSTACK时报告语法错误
 * - 错误消息："function or expression too complex"
 * - 防止生成无法执行的字节码
 *
 * 性能考虑：
 * - 预先检查避免运行时栈溢出
 * - 准确跟踪栈使用情况
 * - 支持虚拟机的栈管理优化
 */
void luaK_checkstack (FuncState *fs, int n) {
    int newstack = fs->freereg + n;
    if (newstack > fs->f->maxstacksize) {
        if (newstack >= MAXSTACK)
            luaX_syntaxerror(fs->ls, "function or expression too complex");
        fs->f->maxstacksize = cast_byte(newstack);
    }
}

/**
 * @brief 预留指定数量的寄存器
 *
 * 分配指定数量的连续寄存器供后续使用。这是寄存器分配的
 * 主要接口函数，确保有足够的栈空间并更新自由寄存器指针。
 *
 * @param fs 函数编译状态指针
 * @param n 要预留的寄存器数量
 *
 * @note 预留的寄存器从当前freereg开始
 * @note 调用者负责在使用完毕后释放寄存器
 *
 * @see luaK_checkstack, FuncState::freereg
 *
 * 分配过程：
 * 1. 检查栈空间是否充足
 * 2. 更新自由寄存器指针
 * 3. 返回分配的寄存器范围
 *
 * 使用场景：
 * - 函数调用参数准备
 * - 复杂表达式的临时存储
 * - 多返回值的接收
 * - 表构造的元素存储
 *
 * 分配策略：
 * - 连续分配：保证分配的寄存器是连续的
 * - 栈式管理：按栈顺序分配和释放
 * - 自动扩展：根据需要扩展栈大小
 */
void luaK_reserveregs (FuncState *fs, int n) {
    luaK_checkstack(fs, n);
    fs->freereg += n;
}

/**
 * @brief 释放单个寄存器
 *
 * 释放指定的寄存器，使其可以被重新分配。只有临时寄存器
 * 可以被释放，局部变量和常量不能释放。
 *
 * @param fs 函数编译状态指针
 * @param reg 要释放的寄存器索引
 *
 * @note 只能释放临时寄存器（非常量且大于等于nactvar）
 * @note 必须按栈顺序释放（LIFO）
 *
 * @see ISK, FuncState::nactvar, FuncState::freereg
 *
 * 释放条件：
 * - 不是常量（!ISK(reg)）
 * - 不是活跃局部变量（reg >= fs->nactvar）
 * - 是最后分配的寄存器（reg == fs->freereg - 1）
 *
 * 安全检查：
 * - 确保按正确顺序释放寄存器
 * - 防止释放仍在使用的寄存器
 * - 维护寄存器分配的一致性
 *
 * 设计原理：
 * - 栈式释放：只能释放栈顶寄存器
 * - 局部变量保护：防止误释放局部变量
 * - 常量优化：常量不占用寄存器空间
 */
static void freereg (FuncState *fs, int reg) {
    if (!ISK(reg) && reg >= fs->nactvar) {
        fs->freereg--;
        lua_assert(reg == fs->freereg);
    }
}

/**
 * @brief 释放表达式占用的寄存器
 *
 * 根据表达式的类型，释放其占用的临时寄存器。这是表达式
 * 求值后的清理操作，避免寄存器泄漏。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 只有VNONRELOC类型的表达式占用临时寄存器
 * @note 其他类型的表达式不需要释放操作
 *
 * @see freereg, expdesc, VNONRELOC
 *
 * 表达式类型处理：
 * - VNONRELOC：占用临时寄存器，需要释放
 * - VLOCAL：局部变量，不能释放
 * - VK：常量，不占用寄存器
 * - 其他：不占用临时寄存器
 *
 * 使用场景：
 * - 表达式求值完成后
 * - 临时结果不再需要时
 * - 函数调用参数传递后
 * - 赋值操作完成后
 *
 * 内存管理：
 * - 防止寄存器泄漏
 * - 提高寄存器利用率
 * - 支持复杂表达式的嵌套求值
 */
static void freeexp (FuncState *fs, expdesc *e) {
    if (e->k == VNONRELOC)
        freereg(fs, e->u.s.info);
}

/** @} */ /* 结束寄存器管理文档组 */


/**
 * @defgroup ConstantManagement 常量管理
 * @brief 函数常量池的管理和优化系统
 *
 * 常量管理系统负责维护函数的常量池，实现常量的去重、索引分配
 * 和高效访问。常量池是Lua字节码的重要组成部分，存储函数中
 * 使用的所有常量值。
 *
 * 核心概念：
 * - 常量池：存储函数中所有常量的数组
 * - 常量去重：相同的常量只存储一份
 * - 索引映射：使用哈希表快速查找常量索引
 * - 类型支持：支持数字、字符串、布尔值、nil等类型
 *
 * 优化策略：
 * - 哈希表查找：O(1)时间复杂度的常量查找
 * - 内存共享：相同常量共享存储空间
 * - 动态扩展：根据需要动态扩展常量池
 * - 垃圾回收：与Lua垃圾回收器集成
 * @{
 */

/**
 * @brief 添加常量到常量池
 *
 * 将常量添加到函数的常量池中，如果常量已存在则返回现有索引，
 * 否则创建新的常量条目。这是常量管理的核心函数。
 *
 * @param fs 函数编译状态指针
 * @param k 用作哈希表键的TValue指针
 * @param v 要存储的常量值TValue指针
 * @return 常量在常量池中的索引
 *
 * @note 使用哈希表实现常量去重
 * @note 自动扩展常量池大小
 *
 * @see luaH_set, luaM_growvector, luaC_barrier
 *
 * 算法流程：
 * 1. 在哈希表中查找常量
 * 2. 如果找到，返回现有索引
 * 3. 如果未找到，分配新索引
 * 4. 扩展常量池数组（如需要）
 * 5. 存储常量值并设置垃圾回收屏障
 *
 * 去重机制：
 * - 使用哈希表fs->h进行快速查找
 * - 键值对映射：常量值 -> 常量池索引
 * - 避免重复存储相同的常量
 *
 * 内存管理：
 * - 动态扩展常量池数组
 * - 新分配的位置初始化为nil
 * - 设置垃圾回收屏障保护常量
 *
 * 错误处理：
 * - 常量池大小限制：MAXARG_Bx
 * - 超出限制时报告"constant table overflow"
 *
 * 性能特点：
 * - 查找时间：O(1)平均情况
 * - 空间效率：避免重复存储
 * - 缓存友好：连续存储常量
 */
static int addk (FuncState *fs, TValue *k, TValue *v) {
    lua_State *L = fs->L;
    TValue *idx = luaH_set(L, fs->h, k);
    Proto *f = fs->f;
    int oldsize = f->sizek;
    if (ttisnumber(idx)) {
        lua_assert(luaO_rawequalObj(&fs->f->k[cast_int(nvalue(idx))], v));
        return cast_int(nvalue(idx));
    }
    else {  /* 常量未找到；创建新条目 */
        setnvalue(idx, cast_num(fs->nk));
        luaM_growvector(L, f->k, fs->nk, f->sizek, TValue,
                        MAXARG_Bx, "constant table overflow");
        while (oldsize < f->sizek) setnilvalue(&f->k[oldsize++]);
        setobj(L, &f->k[fs->nk], v);
        luaC_barrier(L, f, v);
        return fs->nk++;
    }
}

/**
 * @brief 添加字符串常量
 *
 * 将字符串常量添加到常量池中，返回其索引。这是字符串
 * 常量处理的专用接口。
 *
 * @param fs 函数编译状态指针
 * @param s 字符串对象指针
 * @return 字符串常量在常量池中的索引
 *
 * @note 字符串在Lua中是不可变的，可以安全共享
 * @note 使用相同的TValue作为键和值
 *
 * @see addk, setsvalue, TString
 *
 * 实现细节：
 * - 创建TValue包装字符串
 * - 调用通用的addk函数
 * - 键和值使用相同的TValue
 *
 * 使用场景：
 * - 字符串字面量
 * - 标识符名称
 * - 表的字符串键
 * - 函数名和变量名
 *
 * 优化考虑：
 * - 字符串去重由addk自动处理
 * - 利用Lua字符串的内部化特性
 * - 减少内存使用和比较开销
 */
int luaK_stringK (FuncState *fs, TString *s) {
    TValue o;
    setsvalue(fs->L, &o, s);
    return addk(fs, &o, &o);
}

/**
 * @brief 添加数字常量
 *
 * 将数字常量添加到常量池中，返回其索引。这是数字
 * 常量处理的专用接口。
 *
 * @param fs 函数编译状态指针
 * @param r 数字值
 * @return 数字常量在常量池中的索引
 *
 * @note 支持整数和浮点数
 * @note 相同数值的常量会被去重
 *
 * @see addk, setnvalue, lua_Number
 *
 * 实现细节：
 * - 创建TValue包装数字
 * - 调用通用的addk函数
 * - 键和值使用相同的TValue
 *
 * 使用场景：
 * - 数字字面量
 * - 数组索引常量
 * - 算术运算的常量操作数
 * - 循环计数器的初值和步长
 *
 * 数值处理：
 * - 整数和浮点数统一处理
 * - 精确的数值比较和去重
 * - 支持特殊值（NaN、无穷大等）
 */
int luaK_numberK (FuncState *fs, lua_Number r) {
    TValue o;
    setnvalue(&o, r);
    return addk(fs, &o, &o);
}

/**
 * @brief 添加布尔常量
 *
 * 将布尔常量添加到常量池中，返回其索引。这是布尔
 * 常量处理的内部函数。
 *
 * @param fs 函数编译状态指针
 * @param b 布尔值（0为false，非0为true）
 * @return 布尔常量在常量池中的索引
 *
 * @note 只有true和false两个可能的布尔常量
 * @note 布尔常量会被自动去重
 *
 * @see addk, setbvalue
 *
 * 实现细节：
 * - 创建TValue包装布尔值
 * - 调用通用的addk函数
 * - 键和值使用相同的TValue
 *
 * 使用场景：
 * - 布尔字面量（true、false）
 * - 条件表达式的默认值
 * - 逻辑运算的常量操作数
 * - 表的布尔值字段
 *
 * 优化效果：
 * - 全局只需要两个布尔常量
 * - 减少重复的布尔值存储
 * - 提高布尔运算的效率
 */
static int boolK (FuncState *fs, int b) {
    TValue o;
    setbvalue(&o, b);
    return addk(fs, &o, &o);
}

/**
 * @brief 添加nil常量
 *
 * 将nil常量添加到常量池中，返回其索引。由于nil不能作为
 * 哈希表的键，使用特殊的键值对表示。
 *
 * @param fs 函数编译状态指针
 * @return nil常量在常量池中的索引
 *
 * @note nil不能作为哈希表键，使用表本身作为键
 * @note 全局只需要一个nil常量
 *
 * @see addk, setnilvalue, sethvalue
 *
 * 特殊处理：
 * - 键：使用常量哈希表fs->h本身
 * - 值：nil值
 * - 避免nil作为键的问题
 *
 * 实现原理：
 * - nil不能作为Lua表的键
 * - 使用表对象本身作为唯一键
 * - 保证nil常量的唯一性
 *
 * 使用场景：
 * - nil字面量
 * - 变量的默认初值
 * - 表元素的删除操作
 * - 函数参数的缺省值
 *
 * 设计考虑：
 * - 绕过nil作为键的限制
 * - 保持常量去重的一致性
 * - 支持nil的高效处理
 */
static int nilK (FuncState *fs) {
    TValue k, v;
    setnilvalue(&v);
    /* 不能使用nil作为键；使用表本身表示nil */
    sethvalue(fs->L, &k, fs->h);
    return addk(fs, &k, &v);
}

/** @} */ /* 结束常量管理文档组 */


/**
 * @defgroup ExpressionHandling 表达式处理
 * @brief 表达式求值和代码生成的核心函数集合
 *
 * 表达式处理系统负责将各种类型的表达式转换为可执行的字节码。
 * 这包括变量访问、函数调用、常量加载、类型转换等操作的代码生成。
 *
 * 核心概念：
 * - 表达式描述符：记录表达式的类型、值、位置等信息
 * - 代码生成：将表达式转换为字节码指令
 * - 寄存器分配：为表达式结果分配合适的寄存器
 * - 类型转换：在不同表达式类型间进行转换
 *
 * 表达式类型：
 * - VLOCAL：局部变量
 * - VGLOBAL：全局变量
 * - VUPVAL：上值变量
 * - VINDEXED：表索引访问
 * - VCALL：函数调用
 * - VVARARG：可变参数
 * - VK/VKNUM：常量
 * - VNONRELOC：固定寄存器中的值
 * - VRELOCABLE：可重定位的指令结果
 * @{
 */

/**
 * @brief 设置多返回值表达式的返回数量
 *
 * 调整函数调用或可变参数表达式的返回值数量。这用于处理
 * 多返回值的上下文，如赋值语句和函数参数。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 * @param nresults 期望的返回值数量
 *
 * @note 只处理VCALL和VVARARG类型的表达式
 * @note nresults+1编码：0表示所有返回值，1表示1个返回值
 *
 * @see VCALL, VVARARG, SETARG_C, SETARG_B
 *
 * 处理类型：
 * - VCALL：函数调用，修改C参数（返回值数量）
 * - VVARARG：可变参数，修改B参数并分配寄存器
 *
 * 编码规则：
 * - nresults = 0：返回所有可用值
 * - nresults > 0：返回指定数量的值
 * - 指令中存储nresults+1
 *
 * 使用场景：
 * - 多重赋值的右侧表达式
 * - 函数调用的参数列表
 * - 表构造器的元素列表
 * - return语句的返回值
 */
void luaK_setreturns (FuncState *fs, expdesc *e, int nresults) {
    if (e->k == VCALL) {  /* 表达式是开放的函数调用？ */
        SETARG_C(getcode(fs, e), nresults+1);
    }
    else if (e->k == VVARARG) {
        SETARG_B(getcode(fs, e), nresults+1);
        SETARG_A(getcode(fs, e), fs->freereg);
        luaK_reserveregs(fs, 1);
    }
}

/**
 * @brief 设置表达式返回单个值
 *
 * 将多返回值表达式转换为单返回值表达式。这是setreturns
 * 的特化版本，专门处理只需要一个返回值的情况。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 将VCALL转换为VNONRELOC
 * @note 将VVARARG转换为VRELOCABLE
 *
 * @see luaK_setreturns, VCALL, VVARARG
 *
 * 转换逻辑：
 * - VCALL：固定返回1个值，转为VNONRELOC
 * - VVARARG：固定返回1个值，转为VRELOCABLE
 *
 * 类型转换：
 * - VCALL -> VNONRELOC：结果在固定寄存器中
 * - VVARARG -> VRELOCABLE：结果可重定位
 *
 * 使用场景：
 * - 表达式作为单个操作数
 * - 算术运算的操作数
 * - 条件表达式的测试值
 * - 单个变量的赋值
 */
void luaK_setoneret (FuncState *fs, expdesc *e) {
    if (e->k == VCALL) {  /* 表达式是开放的函数调用？ */
        e->k = VNONRELOC;
        e->u.s.info = GETARG_A(getcode(fs, e));
    }
    else if (e->k == VVARARG) {
        SETARG_B(getcode(fs, e), 2);
        e->k = VRELOCABLE;  /* 可以重定位其简单结果 */
    }
}

/**
 * @brief 释放变量表达式并生成访问代码
 *
 * 将各种类型的变量表达式转换为可直接使用的值。这是表达式
 * 求值的核心函数，处理变量访问的代码生成。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 不同类型的变量需要不同的访问指令
 * @note 转换后的表达式可以直接参与运算
 *
 * @see OP_GETUPVAL, OP_GETGLOBAL, OP_GETTABLE
 *
 * 处理类型：
 * - VLOCAL：局部变量，直接转为VNONRELOC
 * - VUPVAL：上值变量，生成GETUPVAL指令
 * - VGLOBAL：全局变量，生成GETGLOBAL指令
 * - VINDEXED：表索引，生成GETTABLE指令
 * - VCALL/VVARARG：多返回值，转为单返回值
 *
 * 代码生成：
 * - 局部变量：无需指令，直接使用寄存器
 * - 上值变量：GETUPVAL指令加载到寄存器
 * - 全局变量：GETGLOBAL指令加载到寄存器
 * - 表索引：GETTABLE指令执行索引操作
 *
 * 寄存器管理：
 * - 释放表索引操作中的临时寄存器
 * - 为结果分配新的寄存器
 * - 更新表达式的寄存器信息
 *
 * 类型转换：
 * - 大多数转换为VRELOCABLE（可重定位）
 * - 局部变量转换为VNONRELOC（固定位置）
 * - 保持表达式的语义不变
 */
void luaK_dischargevars (FuncState *fs, expdesc *e) {
    switch (e->k) {
        case VLOCAL: {
            e->k = VNONRELOC;
            break;
        }
        case VUPVAL: {
            e->u.s.info = luaK_codeABC(fs, OP_GETUPVAL, 0, e->u.s.info, 0);
            e->k = VRELOCABLE;
            break;
        }
        case VGLOBAL: {
            e->u.s.info = luaK_codeABx(fs, OP_GETGLOBAL, 0, e->u.s.info);
            e->k = VRELOCABLE;
            break;
        }
        case VINDEXED: {
            freereg(fs, e->u.s.aux);
            freereg(fs, e->u.s.info);
            e->u.s.info = luaK_codeABC(fs, OP_GETTABLE, 0, e->u.s.info, e->u.s.aux);
            e->k = VRELOCABLE;
            break;
        }
        case VVARARG:
        case VCALL: {
            luaK_setoneret(fs, e);
            break;
        }
        default: break;  /* 有一个值可用（在某处） */
    }
}

/** @} */ /* 结束表达式处理文档组 */


/**
 * @defgroup ExpressionCodeGeneration 表达式代码生成
 * @brief 表达式求值和寄存器分配的核心算法
 *
 * 表达式代码生成系统是编译器后端的核心组件，负责将各种类型的表达式
 * 转换为高效的字节码指令序列。这包括寄存器分配、指令选择、代码优化
 * 和跳转处理等关键功能。
 *
 * 核心算法：
 * - 表达式到寄存器转换：将表达式结果存储到指定寄存器
 * - 寄存器分配优化：最小化寄存器使用和数据移动
 * - 条件表达式处理：短路求值和跳转链表管理
 * - 指令选择：为不同表达式类型选择最优指令
 *
 * 优化技术：
 * - 寄存器复用：避免不必要的MOVE指令
 * - 常量优化：直接使用常量池索引
 * - 跳转优化：合并和简化跳转指令
 * - 指令重定位：动态调整指令的目标寄存器
 * @{
 */

/**
 * @brief 生成带标签的布尔加载指令
 *
 * 生成LOADBOOL指令并标记当前位置为跳转目标。这用于条件表达式
 * 中需要加载特定布尔值的情况。
 *
 * @param fs 函数编译状态指针
 * @param A 目标寄存器
 * @param b 布尔值（0为false，1为true）
 * @param jump 跳转标志（1表示跳过下一条指令）
 * @return 生成的指令位置
 *
 * @note 自动标记当前位置为跳转目标
 * @note jump参数用于实现条件跳转优化
 *
 * @see luaK_getlabel, luaK_codeABC, OP_LOADBOOL
 *
 * 指令语义：
 * - LOADBOOL A b jump：将布尔值b加载到寄存器A
 * - 如果jump为1，跳过下一条指令
 * - 用于条件表达式的真假分支
 *
 * 使用场景：
 * - 条件表达式的真假值加载
 * - 逻辑运算的短路求值
 * - 比较运算的结果生成
 * - 三元运算符的分支处理
 *
 * 优化考虑：
 * - 标记跳转目标防止错误优化
 * - jump标志减少跳转指令数量
 * - 支持条件表达式的高效实现
 */
static int code_label (FuncState *fs, int A, int b, int jump) {
    luaK_getlabel(fs);  /* 这些指令可能是跳转目标 */
    return luaK_codeABC(fs, OP_LOADBOOL, A, b, jump);
}

/**
 * @brief 将表达式结果存储到指定寄存器
 *
 * 这是表达式求值的核心函数，将任意类型的表达式转换为存储在
 * 指定寄存器中的值。处理所有表达式类型的代码生成。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 * @param reg 目标寄存器索引
 *
 * @note 处理完成后表达式类型变为VNONRELOC
 * @note 自动选择最优的指令序列
 *
 * @see luaK_dischargevars, luaK_nil, OP_LOADBOOL, OP_LOADK, OP_MOVE
 *
 * 表达式类型处理：
 * - VNIL：生成LOADNIL指令
 * - VFALSE/VTRUE：生成LOADBOOL指令
 * - VK：生成LOADK指令加载常量
 * - VKNUM：将数字加入常量池后生成LOADK
 * - VRELOCABLE：重定位指令的目标寄存器
 * - VNONRELOC：必要时生成MOVE指令
 * - VVOID/VJMP：无需处理
 *
 * 优化策略：
 * - 避免不必要的MOVE指令
 * - 数字常量自动加入常量池
 * - 可重定位指令直接修改目标
 * - 最小化指令数量和寄存器使用
 *
 * 指令选择：
 * - 根据表达式类型选择最优指令
 * - 利用指令的特殊功能减少代码
 * - 支持常量和寄存器的统一处理
 *
 * 寄存器管理：
 * - 确保结果存储在指定寄存器
 * - 更新表达式的寄存器信息
 * - 转换表达式类型为VNONRELOC
 */
static void discharge2reg (FuncState *fs, expdesc *e, int reg) {
    luaK_dischargevars(fs, e);
    switch (e->k) {
        case VNIL: {
            luaK_nil(fs, reg, 1);
            break;
        }
        case VFALSE:  case VTRUE: {
            luaK_codeABC(fs, OP_LOADBOOL, reg, e->k == VTRUE, 0);
            break;
        }
        case VK: {
            luaK_codeABx(fs, OP_LOADK, reg, e->u.s.info);
            break;
        }
        case VKNUM: {
            luaK_codeABx(fs, OP_LOADK, reg, luaK_numberK(fs, e->u.nval));
            break;
        }
        case VRELOCABLE: {
            Instruction *pc = &getcode(fs, e);
            SETARG_A(*pc, reg);
            break;
        }
        case VNONRELOC: {
            if (reg != e->u.s.info)
                luaK_codeABC(fs, OP_MOVE, reg, e->u.s.info, 0);
            break;
        }
        default: {
            lua_assert(e->k == VVOID || e->k == VJMP);
            return;  /* 无需处理... */
        }
    }
    e->u.s.info = reg;
    e->k = VNONRELOC;
}

/**
 * @brief 将表达式结果存储到任意可用寄存器
 *
 * 如果表达式还未存储在寄存器中，分配一个新寄存器并存储结果。
 * 这是discharge2reg的便利包装函数。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 只在表达式不是VNONRELOC时才分配新寄存器
 * @note 使用当前可用的第一个寄存器
 *
 * @see luaK_reserveregs, discharge2reg
 *
 * 优化逻辑：
 * - 如果表达式已在寄存器中，无需操作
 * - 否则分配新寄存器并转换表达式
 * - 避免不必要的寄存器分配
 *
 * 使用场景：
 * - 表达式需要寄存器但位置不重要
 * - 临时结果的存储
 * - 函数调用参数的准备
 * - 复杂表达式的中间结果
 *
 * 寄存器分配：
 * - 使用栈式分配策略
 * - 分配当前freereg-1位置
 * - 自动更新寄存器分配状态
 */
static void discharge2anyreg (FuncState *fs, expdesc *e) {
    if (e->k != VNONRELOC) {
        luaK_reserveregs(fs, 1);
        discharge2reg(fs, e, fs->freereg-1);
    }
}


/**
 * @brief 将表达式转换为寄存器值并处理跳转
 *
 * 这是处理复杂表达式（特别是条件表达式）的核心函数。它不仅将
 * 表达式转换为寄存器值，还处理相关的跳转链表和条件分支。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 * @param reg 目标寄存器索引
 *
 * @note 处理条件表达式的跳转链表
 * @note 生成必要的LOADBOOL指令
 *
 * @see discharge2reg, luaK_concat, patchlistaux, code_label
 *
 * 算法流程：
 * 1. 基本的表达式到寄存器转换
 * 2. 处理VJMP类型的跳转合并
 * 3. 检查是否需要处理跳转链表
 * 4. 生成条件分支的真假值加载
 * 5. 修正所有跳转到最终位置
 *
 * 跳转处理：
 * - 合并VJMP到真跳转链表
 * - 为需要值的跳转生成LOADBOOL
 * - 修正真假跳转链表到最终位置
 * - 清空跳转链表信息
 *
 * 条件表达式优化：
 * - 只在需要时生成LOADBOOL指令
 * - 使用跳转优化减少指令数量
 * - 支持短路求值的高效实现
 *
 * 代码生成模式：
 * ```
 * 表达式代码
 * [跳转到真分支]
 * LOADBOOL reg 0 1  ; 加载false并跳过
 * LOADBOOL reg 1 0  ; 加载true
 * [最终位置]
 * ```
 *
 * 使用场景：
 * - 条件表达式的求值
 * - 逻辑运算符的实现
 * - 比较运算的结果生成
 * - 三元运算符的处理
 */
static void exp2reg (FuncState *fs, expdesc *e, int reg) {
    discharge2reg(fs, e, reg);
    if (e->k == VJMP)
        luaK_concat(fs, &e->t, e->u.s.info);  /* 将此跳转放入`t'列表 */
    if (hasjumps(e)) {
        int final;  /* 整个表达式后的位置 */
        int p_f = NO_JUMP;  /* 可能的LOAD false的位置 */
        int p_t = NO_JUMP;  /* 可能的LOAD true的位置 */
        if (need_value(fs, e->t) || need_value(fs, e->f)) {
            int fj = (e->k == VJMP) ? NO_JUMP : luaK_jump(fs);
            p_f = code_label(fs, reg, 0, 1);
            p_t = code_label(fs, reg, 1, 0);
            luaK_patchtohere(fs, fj);
        }
        final = luaK_getlabel(fs);
        patchlistaux(fs, e->f, final, reg, p_f);
        patchlistaux(fs, e->t, final, reg, p_t);
    }
    e->f = e->t = NO_JUMP;
    e->u.s.info = reg;
    e->k = VNONRELOC;
}

/**
 * @brief 将表达式转换到下一个可用寄存器
 *
 * 这是表达式求值的标准接口，将表达式转换为存储在下一个
 * 可用寄存器中的值。包含完整的资源管理。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 自动处理寄存器分配和释放
 * @note 使用下一个可用寄存器作为目标
 *
 * @see luaK_dischargevars, freeexp, luaK_reserveregs, exp2reg
 *
 * 处理步骤：
 * 1. 释放变量表达式（生成访问代码）
 * 2. 释放表达式占用的临时寄存器
 * 3. 预留一个新寄存器
 * 4. 将表达式转换到新寄存器
 *
 * 资源管理：
 * - 释放不再需要的寄存器
 * - 分配新的目标寄存器
 * - 确保寄存器使用的正确性
 *
 * 使用场景：
 * - 表达式语句的求值
 * - 函数调用参数的准备
 * - 赋值语句的右侧表达式
 * - 复杂表达式的子表达式
 *
 * 优化考虑：
 * - 最小化寄存器使用
 * - 避免不必要的数据移动
 * - 支持表达式的链式求值
 */
void luaK_exp2nextreg (FuncState *fs, expdesc *e) {
    luaK_dischargevars(fs, e);
    freeexp(fs, e);
    luaK_reserveregs(fs, 1);
    exp2reg(fs, e, fs->freereg - 1);
}

/**
 * @brief 将表达式转换到任意寄存器并返回寄存器索引
 *
 * 智能的表达式求值函数，尽可能复用现有寄存器，只在必要时
 * 分配新寄存器。这是性能优化的关键函数。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 * @return 存储表达式结果的寄存器索引
 *
 * @note 优先复用现有寄存器
 * @note 只在必要时分配新寄存器
 *
 * @see luaK_dischargevars, hasjumps, exp2reg, luaK_exp2nextreg
 *
 * 优化策略：
 * 1. 如果表达式已在寄存器且无跳转，直接返回
 * 2. 如果寄存器不是局部变量，可以复用
 * 3. 否则分配新寄存器
 *
 * 寄存器复用条件：
 * - 表达式类型为VNONRELOC
 * - 表达式没有跳转
 * - 寄存器不是活跃局部变量
 *
 * 性能优化：
 * - 避免不必要的MOVE指令
 * - 减少寄存器分配开销
 * - 提高代码生成效率
 *
 * 使用场景：
 * - 算术运算的操作数
 * - 函数调用的参数
 * - 表索引的键值
 * - 任何需要寄存器值的地方
 *
 * 返回值用途：
 * - 作为其他指令的操作数
 * - 寄存器分配的参考
 * - 代码生成的输入
 */
int luaK_exp2anyreg (FuncState *fs, expdesc *e) {
    luaK_dischargevars(fs, e);
    if (e->k == VNONRELOC) {
        if (!hasjumps(e)) return e->u.s.info;  /* 表达式已在寄存器中 */
        if (e->u.s.info >= fs->nactvar) {  /* 寄存器不是局部变量？ */
            exp2reg(fs, e, e->u.s.info);  /* 将值放入其中 */
            return e->u.s.info;
        }
    }
    luaK_exp2nextreg(fs, e);  /* 默认处理 */
    return e->u.s.info;
}


/**
 * @brief 将表达式转换为可用的值
 *
 * 确保表达式可以作为值使用，处理跳转和变量访问。这是表达式
 * 求值的轻量级版本，不强制分配寄存器。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 有跳转时强制转换为寄存器值
 * @note 无跳转时只处理变量访问
 *
 * @see hasjumps, luaK_exp2anyreg, luaK_dischargevars
 *
 * 处理策略：
 * - 有跳转：必须转换为寄存器值
 * - 无跳转：只需处理变量访问
 *
 * 优化考虑：
 * - 避免不必要的寄存器分配
 * - 保持表达式的原始形式
 * - 只在必要时进行转换
 *
 * 使用场景：
 * - 表达式作为操作数前的准备
 * - 轻量级的表达式求值
 * - 保持表达式灵活性的场合
 * - 延迟寄存器分配的优化
 */
void luaK_exp2val (FuncState *fs, expdesc *e) {
    if (hasjumps(e))
        luaK_exp2anyreg(fs, e);
    else
        luaK_dischargevars(fs, e);
}

/**
 * @brief 将表达式转换为RK操作数
 *
 * 将表达式转换为Lua虚拟机指令的RK操作数格式。RK操作数可以是
 * 寄存器索引或常量索引，这是虚拟机指令的重要优化。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 * @return RK格式的操作数（寄存器索引或RKASK(常量索引)）
 *
 * @note RK操作数：寄存器(0-255)或常量(256+常量索引)
 * @note 常量优先：能用常量就不用寄存器
 *
 * @see luaK_exp2val, RKASK, MAXINDEXRK
 *
 * RK操作数格式：
 * - 0-255：寄存器索引
 * - 256+：常量池索引（通过RKASK编码）
 * - 提供统一的操作数接口
 *
 * 常量处理：
 * - VKNUM/VTRUE/VFALSE/VNIL：转换为常量
 * - VK：已经是常量
 * - 检查常量索引是否在范围内
 *
 * 优化策略：
 * 1. 优先使用常量（减少寄存器压力）
 * 2. 检查常量索引范围限制
 * 3. 超出范围时使用寄存器
 *
 * 指令兼容性：
 * - 大多数指令支持RK操作数
 * - 提供常量和寄存器的统一接口
 * - 简化指令设计和实现
 *
 * 性能优势：
 * - 减少LOADK指令的生成
 * - 直接在指令中引用常量
 * - 提高指令执行效率
 *
 * 使用场景：
 * - 算术运算的操作数
 * - 比较运算的操作数
 * - 表索引的键值
 * - 函数调用的参数
 */
int luaK_exp2RK (FuncState *fs, expdesc *e) {
    luaK_exp2val(fs, e);
    switch (e->k) {
        case VKNUM:
        case VTRUE:
        case VFALSE:
        case VNIL: {
            if (fs->nk <= MAXINDEXRK) {  /* 常量适合RK操作数？ */
                e->u.s.info = (e->k == VNIL)  ? nilK(fs) :
                              (e->k == VKNUM) ? luaK_numberK(fs, e->u.nval) :
                                                boolK(fs, (e->k == VTRUE));
                e->k = VK;
                return RKASK(e->u.s.info);
            }
            else break;
        }
        case VK: {
            if (e->u.s.info <= MAXINDEXRK)  /* 常量适合argC？ */
                return RKASK(e->u.s.info);
            else break;
        }
        default: break;
    }
    /* 不是合适范围内的常量：放入寄存器 */
    return luaK_exp2anyreg(fs, e);
}


/**
 * @brief 存储值到变量
 *
 * 根据变量类型生成相应的存储指令。这是赋值操作的核心实现，
 * 处理所有类型变量的赋值代码生成。
 *
 * @param fs 函数编译状态指针
 * @param var 目标变量的表达式描述符
 * @param ex 要存储的值的表达式描述符
 *
 * @note 根据变量类型选择不同的存储指令
 * @note 自动处理表达式求值和寄存器管理
 *
 * @see exp2reg, luaK_exp2anyreg, luaK_exp2RK
 * @see OP_SETUPVAL, OP_SETGLOBAL, OP_SETTABLE
 *
 * 变量类型处理：
 * - VLOCAL：局部变量，直接存储到指定寄存器
 * - VUPVAL：上值变量，生成SETUPVAL指令
 * - VGLOBAL：全局变量，生成SETGLOBAL指令
 * - VINDEXED：表索引，生成SETTABLE指令
 *
 * 指令生成：
 * - 局部变量：无需指令，直接寄存器赋值
 * - 上值变量：SETUPVAL reg upval_idx
 * - 全局变量：SETGLOBAL reg global_idx
 * - 表索引：SETTABLE table_reg key_rk value_rk
 *
 * 优化策略：
 * - 局部变量赋值最高效（无指令开销）
 * - 表索引使用RK操作数优化
 * - 自动选择最优的操作数格式
 *
 * 资源管理：
 * - 释放表达式占用的临时寄存器
 * - 确保寄存器使用的正确性
 * - 避免寄存器泄漏
 *
 * 使用场景：
 * - 赋值语句的实现
 * - 变量初始化
 * - 表元素的设置
 * - 函数参数的传递
 */
void luaK_storevar (FuncState *fs, expdesc *var, expdesc *ex) {
    switch (var->k) {
        case VLOCAL: {
            freeexp(fs, ex);
            exp2reg(fs, ex, var->u.s.info);
            return;
        }
        case VUPVAL: {
            int e = luaK_exp2anyreg(fs, ex);
            luaK_codeABC(fs, OP_SETUPVAL, e, var->u.s.info, 0);
            break;
        }
        case VGLOBAL: {
            int e = luaK_exp2anyreg(fs, ex);
            luaK_codeABx(fs, OP_SETGLOBAL, e, var->u.s.info);
            break;
        }
        case VINDEXED: {
            int e = luaK_exp2RK(fs, ex);
            luaK_codeABC(fs, OP_SETTABLE, var->u.s.info, var->u.s.aux, e);
            break;
        }
        default: {
            lua_assert(0);  /* 无效的变量类型 */
            break;
        }
    }
    freeexp(fs, ex);
}

/**
 * @brief 生成方法调用的self参数
 *
 * 实现Lua的冒号语法（obj:method()）的代码生成。生成SELF指令
 * 来准备方法调用的对象和方法。
 *
 * @param fs 函数编译状态指针
 * @param e 对象表达式描述符（输入输出参数）
 * @param key 方法名表达式描述符
 *
 * @note 生成的SELF指令同时获取方法和设置self参数
 * @note 预留两个寄存器：一个给方法，一个给self参数
 *
 * @see luaK_exp2anyreg, luaK_exp2RK, luaK_reserveregs, OP_SELF
 *
 * SELF指令语义：
 * - SELF A B C：等价于 A+1 := B; A := B[C]
 * - A：方法存储寄存器
 * - A+1：self参数寄存器（对象本身）
 * - B：对象寄存器
 * - C：方法名（RK操作数）
 *
 * 代码生成：
 * ```
 * SELF func obj_reg method_rk
 * ; func寄存器 = obj_reg[method_rk]
 * ; func+1寄存器 = obj_reg
 * ```
 *
 * 寄存器分配：
 * - 预留两个连续寄存器
 * - func：存储方法函数
 * - func+1：存储self参数（对象）
 *
 * 优化特点：
 * - 一条指令完成两个操作
 * - 避免额外的MOVE指令
 * - 直接准备函数调用的参数
 *
 * 使用场景：
 * - 方法调用语法（obj:method()）
 * - 面向对象编程支持
 * - 简化方法调用的代码生成
 *
 * 语法转换：
 * - obj:method(args) -> method(obj, args)
 * - 自动插入self参数
 * - 保持方法调用的语义
 */
void luaK_self (FuncState *fs, expdesc *e, expdesc *key) {
    int func;
    luaK_exp2anyreg(fs, e);
    freeexp(fs, e);
    func = fs->freereg;
    luaK_reserveregs(fs, 2);
    luaK_codeABC(fs, OP_SELF, func, e->u.s.info, luaK_exp2RK(fs, key));
    freeexp(fs, key);
    e->u.s.info = func;
    e->k = VNONRELOC;
}

/** @} */ /* 结束表达式代码生成文档组 */


/**
 * @defgroup ConditionalJumps 条件跳转和逻辑运算
 * @brief 条件表达式和逻辑运算的代码生成系统
 *
 * 条件跳转系统实现了Lua中所有逻辑运算和条件表达式的代码生成。
 * 这包括短路求值、跳转优化、条件反转等高级编译技术。
 *
 * 核心概念：
 * - 短路求值：逻辑运算的提前终止
 * - 跳转链表：管理条件分支的跳转
 * - 条件反转：优化跳转指令的生成
 * - 真假列表：分别管理真假条件的跳转
 *
 * 优化技术：
 * - 常量折叠：编译时确定的条件
 * - 跳转合并：减少跳转指令数量
 * - 指令优化：NOT指令的特殊处理
 * - 死代码消除：永真/永假条件的处理
 * @{
 */

/**
 * @brief 反转跳转条件
 *
 * 将跳转指令的条件反转，用于优化条件表达式的代码生成。
 * 这是实现逻辑运算优化的重要技术。
 *
 * @param fs 函数编译状态指针
 * @param e 包含跳转信息的表达式描述符
 *
 * @note 只能反转TEST模式的指令
 * @note 不能反转TESTSET和TEST指令
 *
 * @see getjumpcontrol, testTMode, SETARG_A
 *
 * 反转机制：
 * - 修改指令的A参数（条件标志）
 * - 0变为1，1变为0
 * - 实现条件的逻辑反转
 *
 * 适用指令：
 * - 比较指令（EQ、LT、LE等）
 * - 其他TEST模式指令
 * - 不包括TESTSET和TEST
 *
 * 使用场景：
 * - 逻辑NOT运算的优化
 * - 条件表达式的反转
 * - 跳转优化的实现
 * - 短路求值的支持
 */
static void invertjump (FuncState *fs, expdesc *e) {
    Instruction *pc = getjumpcontrol(fs, e->u.s.info);
    lua_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != OP_TESTSET &&
                                             GET_OPCODE(*pc) != OP_TEST);
    SETARG_A(*pc, !(GETARG_A(*pc)));
}

/**
 * @brief 根据条件生成跳转
 *
 * 为表达式生成条件跳转指令。这是条件表达式代码生成的核心函数，
 * 实现了多种优化技术。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 * @param cond 跳转条件（0为假时跳转，1为真时跳转）
 * @return 生成的跳转指令位置
 *
 * @note 自动优化NOT指令的处理
 * @note 根据表达式类型选择最优指令
 *
 * @see discharge2anyreg, condjump, OP_TEST, OP_TESTSET
 *
 * 优化策略：
 * 1. 检测并优化NOT指令
 * 2. 移除冗余的NOT指令
 * 3. 直接生成反转的TEST指令
 * 4. 其他情况使用TESTSET指令
 *
 * 指令选择：
 * - OP_TEST：只测试，不设置值
 * - OP_TESTSET：测试并设置值
 * - 根据是否需要值选择指令
 *
 * NOT优化：
 * ```
 * NOT reg1 reg2    ; 原指令
 * TESTSET ... reg1 ; 后续测试
 * 优化为：
 * TEST ... reg2    ; 直接测试，条件反转
 * ```
 *
 * 使用场景：
 * - if语句的条件测试
 * - while循环的条件测试
 * - 逻辑运算的短路求值
 * - 三元运算符的条件分支
 */
static int jumponcond (FuncState *fs, expdesc *e, int cond) {
    if (e->k == VRELOCABLE) {
        Instruction ie = getcode(fs, e);
        if (GET_OPCODE(ie) == OP_NOT) {
            fs->pc--;  /* 移除前一个OP_NOT */
            return condjump(fs, OP_TEST, GETARG_B(ie), 0, !cond);
        }
        /* 否则继续执行 */
    }
    discharge2anyreg(fs, e);
    freeexp(fs, e);
    return condjump(fs, OP_TESTSET, NO_REG, e->u.s.info, cond);
}

/**
 * @brief 生成为真时跳转的代码
 *
 * 为表达式生成在值为真时跳转的代码。这是实现逻辑AND运算和
 * 条件语句的基础函数。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 处理常量表达式的优化
 * @note 管理真假跳转链表
 *
 * @see luaK_dischargevars, invertjump, jumponcond, luaK_concat
 *
 * 处理策略：
 * - 常量真值：无需跳转
 * - VJMP表达式：反转跳转条件
 * - 其他表达式：生成条件跳转
 *
 * 常量优化：
 * - VK/VKNUM/VTRUE：永远为真，无需跳转
 * - 编译时确定结果，避免运行时测试
 *
 * 跳转链表管理：
 * - 将新跳转加入假跳转链表（f）
 * - 修正真跳转链表到当前位置（t）
 * - 清空真跳转链表
 *
 * 使用场景：
 * - 逻辑AND运算（a and b）
 * - if语句的条件测试
 * - while循环的条件测试
 * - 条件表达式的短路求值
 *
 * 短路求值：
 * - 真值时继续执行后续代码
 * - 假值时跳转到表达式结束
 * - 实现高效的逻辑运算
 */
void luaK_goiftrue (FuncState *fs, expdesc *e) {
    int pc;  /* 最后跳转的pc */
    luaK_dischargevars(fs, e);
    switch (e->k) {
        case VK: case VKNUM: case VTRUE: {
            pc = NO_JUMP;  /* 永远为真；无需操作 */
            break;
        }
        case VJMP: {
            invertjump(fs, e);
            pc = e->u.s.info;
            break;
        }
        default: {
            pc = jumponcond(fs, e, 0);
            break;
        }
    }
    luaK_concat(fs, &e->f, pc);  /* 将最后跳转插入`f'列表 */
    luaK_patchtohere(fs, e->t);
    e->t = NO_JUMP;
}


/**
 * @brief 生成为假时跳转的代码
 *
 * 为表达式生成在值为假时跳转的代码。这是实现逻辑OR运算和
 * 条件语句的基础函数。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 处理常量表达式的优化
 * @note 管理真假跳转链表
 *
 * @see luaK_dischargevars, jumponcond, luaK_concat
 *
 * 处理策略：
 * - 常量假值：无需跳转
 * - VJMP表达式：直接使用跳转
 * - 其他表达式：生成条件跳转
 *
 * 常量优化：
 * - VNIL/VFALSE：永远为假，无需跳转
 * - 编译时确定结果，避免运行时测试
 *
 * 跳转链表管理：
 * - 将新跳转加入真跳转链表（t）
 * - 修正假跳转链表到当前位置（f）
 * - 清空假跳转链表
 *
 * 使用场景：
 * - 逻辑OR运算（a or b）
 * - 条件表达式的短路求值
 * - 默认值设置（a or default）
 * - 错误处理的条件分支
 *
 * 短路求值：
 * - 假值时继续执行后续代码
 * - 真值时跳转到表达式结束
 * - 实现高效的逻辑运算
 */
static void luaK_goiffalse (FuncState *fs, expdesc *e) {
    int pc;  /* 最后跳转的pc */
    luaK_dischargevars(fs, e);
    switch (e->k) {
        case VNIL: case VFALSE: {
            pc = NO_JUMP;  /* 永远为假；无需操作 */
            break;
        }
        case VJMP: {
            pc = e->u.s.info;
            break;
        }
        default: {
            pc = jumponcond(fs, e, 1);
            break;
        }
    }
    luaK_concat(fs, &e->t, pc);  /* 将最后跳转插入`t'列表 */
    luaK_patchtohere(fs, e->f);
    e->f = NO_JUMP;
}

/**
 * @brief 生成逻辑NOT运算的代码
 *
 * 实现Lua的逻辑NOT运算符（not）的代码生成。这包括常量折叠、
 * 跳转优化和指令生成等多种技术。
 *
 * @param fs 函数编译状态指针
 * @param e 表达式描述符指针
 *
 * @note 优先使用常量折叠
 * @note 交换真假跳转链表
 *
 * @see luaK_dischargevars, invertjump, discharge2anyreg, removevalues
 *
 * 处理策略：
 * - 常量表达式：编译时计算结果
 * - 跳转表达式：反转跳转条件
 * - 寄存器表达式：生成NOT指令
 *
 * 常量折叠：
 * - VNIL/VFALSE -> VTRUE
 * - VK/VKNUM/VTRUE -> VFALSE
 * - 编译时确定结果，无运行时开销
 *
 * 跳转优化：
 * - VJMP：直接反转跳转条件
 * - 避免生成额外的NOT指令
 * - 保持跳转的高效性
 *
 * 指令生成：
 * - VRELOCABLE/VNONRELOC：生成NOT指令
 * - NOT A B：A = not B
 * - 转换为VRELOCABLE类型
 *
 * 跳转链表处理：
 * - 交换真假跳转链表
 * - 移除跳转链表中的值产生
 * - 保持逻辑语义的正确性
 *
 * 优化技术：
 * - 常量折叠减少运行时计算
 * - 跳转反转避免额外指令
 * - 链表交换实现逻辑反转
 *
 * 使用场景：
 * - not表达式的实现
 * - 条件表达式的反转
 * - 逻辑运算的组合
 * - 布尔值的取反操作
 */
static void codenot (FuncState *fs, expdesc *e) {
    luaK_dischargevars(fs, e);
    switch (e->k) {
        case VNIL: case VFALSE: {
            e->k = VTRUE;
            break;
        }
        case VK: case VKNUM: case VTRUE: {
            e->k = VFALSE;
            break;
        }
        case VJMP: {
            invertjump(fs, e);
            break;
        }
        case VRELOCABLE:
        case VNONRELOC: {
            discharge2anyreg(fs, e);
            freeexp(fs, e);
            e->u.s.info = luaK_codeABC(fs, OP_NOT, 0, e->u.s.info, 0);
            e->k = VRELOCABLE;
            break;
        }
        default: {
            lua_assert(0);  /* 不可能发生 */
            break;
        }
    }
    /* 交换真假列表 */
    { int temp = e->f; e->f = e->t; e->t = temp; }
    removevalues(fs, e->f);
    removevalues(fs, e->t);
}

/**
 * @brief 设置表索引表达式
 *
 * 将表达式标记为表索引访问，准备后续的GETTABLE或SETTABLE操作。
 * 这是实现Lua表访问语法的基础函数。
 *
 * @param fs 函数编译状态指针
 * @param t 表表达式描述符（输入输出参数）
 * @param k 键表达式描述符
 *
 * @note 将键转换为RK操作数格式
 * @note 设置表达式类型为VINDEXED
 *
 * @see luaK_exp2RK, VINDEXED
 *
 * 设置过程：
 * 1. 将键表达式转换为RK操作数
 * 2. 存储键的RK值到aux字段
 * 3. 设置表达式类型为VINDEXED
 *
 * RK操作数优化：
 * - 键可以是寄存器或常量
 * - 常量键直接编码在指令中
 * - 减少LOADK指令的生成
 *
 * 表达式状态：
 * - t->k = VINDEXED：标记为表索引
 * - t->u.s.info：表的寄存器索引
 * - t->u.s.aux：键的RK操作数
 *
 * 后续操作：
 * - GETTABLE：读取表元素
 * - SETTABLE：设置表元素
 * - 使用存储的表和键信息
 *
 * 使用场景：
 * - 表元素访问（t[k]）
 * - 表元素赋值（t[k] = v）
 * - 方法调用的准备（t:m()）
 * - 数组和哈希表的操作
 */
void luaK_indexed (FuncState *fs, expdesc *t, expdesc *k) {
    t->u.s.aux = luaK_exp2RK(fs, k);
    t->k = VINDEXED;
}

/** @} */ /* 结束条件跳转和逻辑运算文档组 */


/**
 * @defgroup ArithmeticOperators 算术运算符处理
 * @brief 算术运算符和比较运算符的代码生成系统
 *
 * 算术运算符处理系统实现了Lua中所有数值运算和比较运算的代码生成。
 * 这包括常量折叠优化、指令选择、操作数处理和结果类型管理等核心功能。
 *
 * 核心技术：
 * - 常量折叠：编译时计算常量表达式
 * - 指令选择：为不同运算选择最优指令
 * - 操作数优化：RK操作数的智能使用
 * - 寄存器管理：最小化寄存器使用和数据移动
 *
 * 支持的运算：
 * - 算术运算：加减乘除、取模、幂运算、取负
 * - 比较运算：等于、不等、小于、小于等于、大于、大于等于
 * - 逻辑运算：与、或、非
 * - 字符串运算：连接、长度
 *
 * 优化策略：
 * - 常量表达式在编译时计算
 * - 避免生成不必要的运行时指令
 * - 智能选择操作数格式
 * - 最小化寄存器分配开销
 * @{
 */

/**
 * @brief 常量折叠优化
 *
 * 在编译时计算常量表达式的值，避免生成运行时计算指令。
 * 这是编译器优化的重要技术，能显著提高代码执行效率。
 *
 * @param op 运算操作码
 * @param e1 第一个操作数表达式描述符
 * @param e2 第二个操作数表达式描述符
 * @return 如果成功折叠返回1，否则返回0
 *
 * @note 只处理纯数值常量表达式
 * @note 避免产生NaN和除零错误
 *
 * @see isnumeral, luai_numadd, luai_numsub, luai_numisnan
 *
 * 支持的运算：
 * - OP_ADD：加法运算
 * - OP_SUB：减法运算
 * - OP_MUL：乘法运算
 * - OP_DIV：除法运算（检查除零）
 * - OP_MOD：取模运算（检查除零）
 * - OP_POW：幂运算
 * - OP_UNM：取负运算
 * - OP_LEN：长度运算（不支持折叠）
 *
 * 安全检查：
 * - 除法和取模检查除零错误
 * - 检查结果是否为NaN
 * - 只处理有效的数值运算
 *
 * 优化效果：
 * - 消除运行时计算开销
 * - 减少生成的字节码数量
 * - 提高程序执行效率
 * - 支持复杂常量表达式
 *
 * 使用场景：
 * - 数值字面量的运算
 * - 编译时可确定的表达式
 * - 常量定义和初始化
 * - 数组大小和循环边界
 *
 * @example
 * // Lua代码: local x = 2 + 3 * 4
 * // 编译时计算: x = 14
 * // 生成: LOADK x 14（而不是ADD和MUL指令）
 */
static int constfolding (OpCode op, expdesc *e1, expdesc *e2) {
    lua_Number v1, v2, r;
    if (!isnumeral(e1) || !isnumeral(e2)) return 0;
    v1 = e1->u.nval;
    v2 = e2->u.nval;
    switch (op) {
        case OP_ADD: r = luai_numadd(v1, v2); break;
        case OP_SUB: r = luai_numsub(v1, v2); break;
        case OP_MUL: r = luai_nummul(v1, v2); break;
        case OP_DIV:
            if (v2 == 0) return 0;  /* 不尝试除零 */
            r = luai_numdiv(v1, v2); break;
        case OP_MOD:
            if (v2 == 0) return 0;  /* 不尝试除零 */
            r = luai_nummod(v1, v2); break;
        case OP_POW: r = luai_numpow(v1, v2); break;
        case OP_UNM: r = luai_numunm(v1); break;
        case OP_LEN: return 0;  /* 'len'不进行常量折叠 */
        default: lua_assert(0); r = 0; break;
    }
    if (luai_numisnan(r)) return 0;  /* 不尝试产生NaN */
    e1->u.nval = r;
    return 1;
}

/**
 * @brief 生成算术运算指令
 *
 * 为算术运算生成字节码指令。首先尝试常量折叠优化，
 * 如果不能折叠则生成运行时计算指令。
 *
 * @param fs 函数编译状态指针
 * @param op 运算操作码
 * @param e1 第一个操作数表达式描述符
 * @param e2 第二个操作数表达式描述符
 *
 * @note 优先使用常量折叠优化
 * @note 智能管理操作数的求值顺序
 *
 * @see constfolding, luaK_exp2RK, freeexp, luaK_codeABC
 *
 * 代码生成策略：
 * 1. 尝试常量折叠优化
 * 2. 如果成功，直接返回（无需生成指令）
 * 3. 否则将操作数转换为RK格式
 * 4. 生成相应的算术指令
 *
 * 操作数处理：
 * - 一元运算：只处理第一个操作数
 * - 二元运算：处理两个操作数
 * - 使用RK格式优化常量操作数
 *
 * 寄存器管理：
 * - 按操作数索引顺序释放寄存器
 * - 避免寄存器使用冲突
 * - 最小化寄存器分配开销
 *
 * 指令格式：
 * - ABC格式：op A B C
 * - A：结果寄存器（由调用者设置）
 * - B：第一个操作数（RK格式）
 * - C：第二个操作数（RK格式）
 *
 * 优化技术：
 * - 常量折叠消除运行时计算
 * - RK操作数减少LOADK指令
 * - 智能寄存器释放顺序
 * - 结果类型设置为VRELOCABLE
 *
 * 使用场景：
 * - 所有算术表达式的实现
 * - 数值运算的代码生成
 * - 表达式求值的核心组件
 * - 编译器后端的基础功能
 */
static void codearith (FuncState *fs, OpCode op, expdesc *e1, expdesc *e2) {
    if (constfolding(op, e1, e2))
        return;
    else {
        int o2 = (op != OP_UNM && op != OP_LEN) ? luaK_exp2RK(fs, e2) : 0;
        int o1 = luaK_exp2RK(fs, e1);
        if (o1 > o2) {
            freeexp(fs, e1);
            freeexp(fs, e2);
        }
        else {
            freeexp(fs, e2);
            freeexp(fs, e1);
        }
        e1->u.s.info = luaK_codeABC(fs, op, 0, o1, o2);
        e1->k = VRELOCABLE;
    }
}


/**
 * @brief 生成比较运算指令
 *
 * 为比较运算生成条件跳转指令。比较运算的结果是条件跳转，
 * 而不是直接的值，这支持短路求值和条件表达式优化。
 *
 * @param fs 函数编译状态指针
 * @param op 比较操作码（OP_EQ、OP_LT、OP_LE）
 * @param cond 条件标志（1为真条件，0为假条件）
 * @param e1 第一个操作数表达式描述符
 * @param e2 第二个操作数表达式描述符
 *
 * @note 生成条件跳转而不是值
 * @note 自动优化操作数顺序
 *
 * @see luaK_exp2RK, condjump, freeexp
 *
 * 比较运算优化：
 * - 只有三个基本比较指令：EQ、LT、LE
 * - 其他比较通过参数交换和条件反转实现
 * - 减少虚拟机指令集的复杂度
 *
 * 操作数交换：
 * - GT (>) -> LT (<) 交换操作数
 * - GE (>=) -> LE (<=) 交换操作数
 * - NE (!=) -> EQ (==) 反转条件
 *
 * 指令生成：
 * - 生成条件跳转指令
 * - 设置表达式类型为VJMP
 * - 支持后续的跳转链表处理
 *
 * 条件处理：
 * - cond=1：条件为真时跳转
 * - cond=0：条件为假时跳转
 * - 支持逻辑运算的短路求值
 *
 * 使用场景：
 * - 所有比较表达式（==、!=、<、<=、>、>=）
 * - if语句的条件测试
 * - while循环的条件测试
 * - 三元运算符的条件分支
 *
 * @example
 * // Lua代码: if a > b then
 * // 生成: LT 0 b a（交换操作数，a > b 变为 b < a）
 * // 跳转: 如果b < a为假则跳转
 */
static void codecomp (FuncState *fs, OpCode op, int cond, expdesc *e1,
                                                          expdesc *e2) {
    int o1 = luaK_exp2RK(fs, e1);
    int o2 = luaK_exp2RK(fs, e2);
    freeexp(fs, e2);
    freeexp(fs, e1);
    if (cond == 0 && op != OP_EQ) {
        int temp;  /* 交换参数以替换为`<'或`<=' */
        temp = o1; o1 = o2; o2 = temp;  /* o1 <==> o2 */
        cond = 1;
    }
    e1->u.s.info = condjump(fs, op, cond, o1, o2);
    e1->k = VJMP;
}

/**
 * @brief 处理一元前缀运算符
 *
 * 生成一元前缀运算符的代码。包括算术取负、逻辑非和长度运算符。
 * 每种运算符都有特定的处理策略和优化技术。
 *
 * @param fs 函数编译状态指针
 * @param op 一元运算符类型
 * @param e 操作数表达式描述符
 *
 * @note 不同运算符有不同的操作数要求
 * @note 某些运算符支持常量优化
 *
 * @see codearith, codenot, luaK_exp2anyreg, isnumeral
 *
 * 支持的运算符：
 * - OPR_MINUS：算术取负（-）
 * - OPR_NOT：逻辑非（not）
 * - OPR_LEN：长度运算符（#）
 *
 * 取负运算（OPR_MINUS）：
 * - 优先处理数值常量
 * - 非数值常量需要转换为寄存器
 * - 使用OP_UNM指令实现
 *
 * 逻辑非运算（OPR_NOT）：
 * - 调用专门的codenot函数
 * - 支持常量折叠和跳转优化
 * - 处理真假跳转链表
 *
 * 长度运算（OPR_LEN）：
 * - 不能对常量操作
 * - 必须转换为寄存器值
 * - 使用OP_LEN指令实现
 *
 * 虚拟操作数：
 * - 创建值为0的虚拟第二操作数
 * - 统一二元运算的接口
 * - 简化代码生成逻辑
 *
 * 优化策略：
 * - 数值常量的取负可以常量折叠
 * - 逻辑非支持跳转优化
 * - 长度运算需要运行时计算
 *
 * 使用场景：
 * - 一元表达式的实现
 * - 算术和逻辑运算的组合
 * - 字符串和表的长度获取
 * - 条件表达式的否定
 */
void luaK_prefix (FuncState *fs, UnOpr op, expdesc *e) {
    expdesc e2;
    e2.t = e2.f = NO_JUMP; e2.k = VKNUM; e2.u.nval = 0;
    switch (op) {
        case OPR_MINUS: {
            if (!isnumeral(e))
                luaK_exp2anyreg(fs, e);  /* 不能对非数值常量操作 */
            codearith(fs, OP_UNM, e, &e2);
            break;
        }
        case OPR_NOT: codenot(fs, e); break;
        case OPR_LEN: {
            luaK_exp2anyreg(fs, e);  /* 不能对常量操作 */
            codearith(fs, OP_LEN, e, &e2);
            break;
        }
        default: lua_assert(0);
    }
}


/**
 * @brief 处理二元运算符的中缀操作
 *
 * 在处理二元运算符的左操作数后调用，为右操作数的处理做准备。
 * 不同的运算符需要不同的左操作数处理策略。
 *
 * @param fs 函数编译状态指针
 * @param op 二元运算符类型
 * @param v 左操作数表达式描述符
 *
 * @note 为短路求值和特殊运算做准备
 * @note 不同运算符有不同的操作数要求
 *
 * @see luaK_goiftrue, luaK_goiffalse, luaK_exp2nextreg, luaK_exp2RK
 *
 * 运算符分类处理：
 *
 * 逻辑运算符：
 * - OPR_AND：实现短路求值，左操作数为真时继续
 * - OPR_OR：实现短路求值，左操作数为假时继续
 *
 * 字符串连接：
 * - OPR_CONCAT：操作数必须在栈上连续存储
 * - 支持多个字符串的连续连接优化
 *
 * 算术运算符：
 * - OPR_ADD/SUB/MUL/DIV/MOD/POW：数值运算
 * - 数值常量保持原状，非常量转为RK格式
 * - 支持常量折叠优化
 *
 * 比较运算符：
 * - 默认情况：转换为RK格式
 * - 为后续的比较指令做准备
 *
 * 短路求值实现：
 * - AND：左操作数为真时跳转到右操作数
 * - OR：左操作数为假时跳转到右操作数
 * - 利用跳转链表管理控制流
 *
 * 字符串连接优化：
 * - 要求操作数在连续寄存器中
 * - 支持CONCAT指令的多操作数特性
 * - 减少临时字符串的创建
 *
 * 常量优化：
 * - 算术运算保持数值常量
 * - 其他运算转换为RK格式
 * - 为常量折叠做准备
 *
 * 使用场景：
 * - 二元表达式解析的中间步骤
 * - 运算符优先级处理
 * - 短路求值的实现
 * - 表达式求值的准备阶段
 */
void luaK_infix (FuncState *fs, BinOpr op, expdesc *v) {
    switch (op) {
        case OPR_AND: {
            luaK_goiftrue(fs, v);
            break;
        }
        case OPR_OR: {
            luaK_goiffalse(fs, v);
            break;
        }
        case OPR_CONCAT: {
            luaK_exp2nextreg(fs, v);  /* 操作数必须在`栈'上 */
            break;
        }
        case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
        case OPR_MOD: case OPR_POW: {
            if (!isnumeral(v)) luaK_exp2RK(fs, v);
            break;
        }
        default: {
            luaK_exp2RK(fs, v);
            break;
        }
    }
}


/**
 * @brief 处理二元运算符的后缀操作
 *
 * 在处理完左右两个操作数后，生成最终的运算指令。这是二元运算符
 * 处理的最后阶段，负责生成实际的运算代码。
 *
 * @param fs 函数编译状态指针
 * @param op 二元运算符类型
 * @param e1 左操作数表达式描述符
 * @param e2 右操作数表达式描述符
 *
 * @note 不同运算符有不同的代码生成策略
 * @note 逻辑运算符使用跳转链表实现短路求值
 *
 * @see codearith, codecomp, luaK_dischargevars, luaK_concat
 *
 * 运算符实现策略：
 *
 * 逻辑运算符（短路求值）：
 * - OPR_AND：合并假跳转链表，实现"与"逻辑
 * - OPR_OR：合并真跳转链表，实现"或"逻辑
 * - 利用跳转链表避免不必要的求值
 *
 * 字符串连接（特殊优化）：
 * - OPR_CONCAT：支持多字符串连接优化
 * - 检测连续的CONCAT指令并合并
 * - 减少临时字符串的创建开销
 *
 * 算术运算符：
 * - OPR_ADD/SUB/MUL/DIV/MOD/POW：标准算术运算
 * - 支持常量折叠和RK操作数优化
 * - 生成对应的算术指令
 *
 * 比较运算符：
 * - OPR_EQ/NE：等于/不等于比较
 * - OPR_LT/LE/GT/GE：大小比较
 * - 生成条件跳转指令
 * - 利用指令复用减少虚拟机复杂度
 *
 * 短路求值实现：
 * ```
 * AND运算：
 * - 左操作数为假时跳转到结果（假）
 * - 左操作数为真时继续求值右操作数
 * - 合并两个操作数的假跳转链表
 *
 * OR运算：
 * - 左操作数为真时跳转到结果（真）
 * - 左操作数为假时继续求值右操作数
 * - 合并两个操作数的真跳转链表
 * ```
 *
 * 字符串连接优化：
 * ```
 * 检测模式：a .. b .. c
 * 优化前：CONCAT t1 a b; CONCAT t2 t1 c
 * 优化后：CONCAT t2 a c（扩展操作数范围）
 * ```
 *
 * 比较运算映射：
 * - NE (!=) -> EQ (==) + 条件反转
 * - GT (>) -> LT (<) + 操作数交换
 * - GE (>=) -> LE (<=) + 操作数交换
 *
 * 使用场景：
 * - 所有二元表达式的最终代码生成
 * - 运算符优先级处理的结果
 * - 复杂表达式的组合运算
 * - 编译器表达式处理的核心
 */
void luaK_posfix (FuncState *fs, BinOpr op, expdesc *e1, expdesc *e2) {
    switch (op) {
        case OPR_AND: {
            lua_assert(e1->t == NO_JUMP);  /* 列表必须关闭 */
            luaK_dischargevars(fs, e2);
            luaK_concat(fs, &e2->f, e1->f);
            *e1 = *e2;
            break;
        }
        case OPR_OR: {
            lua_assert(e1->f == NO_JUMP);  /* 列表必须关闭 */
            luaK_dischargevars(fs, e2);
            luaK_concat(fs, &e2->t, e1->t);
            *e1 = *e2;
            break;
        }
        case OPR_CONCAT: {
            luaK_exp2val(fs, e2);
            if (e2->k == VRELOCABLE && GET_OPCODE(getcode(fs, e2)) == OP_CONCAT) {
                lua_assert(e1->u.s.info == GETARG_B(getcode(fs, e2))-1);
                freeexp(fs, e1);
                SETARG_B(getcode(fs, e2), e1->u.s.info);
                e1->k = VRELOCABLE; e1->u.s.info = e2->u.s.info;
            }
            else {
                luaK_exp2nextreg(fs, e2);  /* 操作数必须在'栈'上 */
                codearith(fs, OP_CONCAT, e1, e2);
            }
            break;
        }
        case OPR_ADD: codearith(fs, OP_ADD, e1, e2); break;
        case OPR_SUB: codearith(fs, OP_SUB, e1, e2); break;
        case OPR_MUL: codearith(fs, OP_MUL, e1, e2); break;
        case OPR_DIV: codearith(fs, OP_DIV, e1, e2); break;
        case OPR_MOD: codearith(fs, OP_MOD, e1, e2); break;
        case OPR_POW: codearith(fs, OP_POW, e1, e2); break;
        case OPR_EQ: codecomp(fs, OP_EQ, 1, e1, e2); break;
        case OPR_NE: codecomp(fs, OP_EQ, 0, e1, e2); break;
        case OPR_LT: codecomp(fs, OP_LT, 1, e1, e2); break;
        case OPR_LE: codecomp(fs, OP_LE, 1, e1, e2); break;
        case OPR_GT: codecomp(fs, OP_LT, 0, e1, e2); break;
        case OPR_GE: codecomp(fs, OP_LE, 0, e1, e2); break;
        default: lua_assert(0);
    }
}

/** @} */ /* 结束算术运算符处理文档组 */


/**
 * @defgroup CodeGeneration 代码生成基础设施
 * @brief 字节码生成和管理的底层基础设施
 *
 * 代码生成基础设施提供了字节码生成的核心功能，包括指令编码、
 * 代码数组管理、行号信息维护和特殊指令处理等基础服务。
 *
 * 核心功能：
 * - 指令编码：将操作码和操作数编码为字节码指令
 * - 代码管理：动态扩展代码数组和行号数组
 * - 调试支持：维护源码行号和字节码的对应关系
 * - 特殊处理：处理复杂指令的特殊需求
 *
 * 指令格式：
 * - ABC格式：操作码 + 3个操作数（A、B、C）
 * - ABx格式：操作码 + 1个操作数（A）+ 1个扩展操作数（Bx）
 * - AsBx格式：操作码 + 1个操作数（A）+ 1个有符号扩展操作数（sBx）
 *
 * 设计特点：
 * - 动态扩展：根据需要自动扩展代码数组
 * - 类型安全：严格的参数类型和范围检查
 * - 调试友好：完整的行号信息维护
 * - 高效编码：紧凑的指令格式和快速编码
 * @{
 */

/**
 * @brief 修正指令的行号信息
 *
 * 更新最后生成的指令的行号信息。这用于处理跨行的复杂表达式
 * 或语句，确保调试信息的准确性。
 *
 * @param fs 函数编译状态指针
 * @param line 新的行号
 *
 * @note 只修改最后一条指令的行号
 * @note 用于处理复杂语句的行号归属
 *
 * @see FuncState::f::lineinfo
 *
 * 使用场景：
 * - 复杂表达式跨越多行时的行号修正
 * - 语句结束时的行号归属调整
 * - 调试信息的精确化处理
 * - 错误报告的准确定位
 */
void luaK_fixline (FuncState *fs, int line) {
    fs->f->lineinfo[fs->pc - 1] = line;
}

/**
 * @brief 生成字节码指令的核心函数
 *
 * 将编码后的指令添加到函数的代码数组中，同时维护行号信息。
 * 这是所有指令生成的最终通道。
 *
 * @param fs 函数编译状态指针
 * @param i 编码后的指令
 * @param line 指令对应的源码行号
 * @return 指令在代码数组中的位置（程序计数器）
 *
 * @note 自动处理待处理的跳转链表
 * @note 动态扩展代码数组和行号数组
 *
 * @see dischargejpc, luaM_growvector
 *
 * 生成过程：
 * 1. 释放待处理的跳转链表
 * 2. 扩展代码数组（如需要）
 * 3. 存储指令到代码数组
 * 4. 扩展行号数组（如需要）
 * 5. 存储行号信息
 * 6. 递增程序计数器并返回
 *
 * 内存管理：
 * - 使用luaM_growvector动态扩展数组
 * - 检查数组大小限制（MAX_INT）
 * - 处理内存分配失败的情况
 *
 * 调试支持：
 * - 维护指令和源码行号的一一对应
 * - 支持调试器的断点设置
 * - 提供错误报告的精确位置信息
 *
 * 性能考虑：
 * - 批量扩展减少内存分配次数
 * - 紧凑的指令格式节省内存
 * - 高效的数组访问模式
 */
static int luaK_code (FuncState *fs, Instruction i, int line) {
    Proto *f = fs->f;
    dischargejpc(fs);  /* `pc'将改变 */
    /* 将新指令放入代码数组 */
    luaM_growvector(fs->L, f->code, fs->pc, f->sizecode, Instruction,
                    MAX_INT, "code size overflow");
    f->code[fs->pc] = i;
    /* 保存对应的行号信息 */
    luaM_growvector(fs->L, f->lineinfo, fs->pc, f->sizelineinfo, int,
                    MAX_INT, "code size overflow");
    f->lineinfo[fs->pc] = line;
    return fs->pc++;
}

/**
 * @brief 生成ABC格式的指令
 *
 * 编码并生成ABC格式的字节码指令。这是最常用的指令格式，
 * 包含一个操作码和三个操作数。
 *
 * @param fs 函数编译状态指针
 * @param o 操作码
 * @param a A操作数（通常是目标寄存器）
 * @param b B操作数（第一个源操作数）
 * @param c C操作数（第二个源操作数）
 * @return 指令在代码数组中的位置
 *
 * @note 严格检查指令格式和参数有效性
 * @note 自动使用当前行号
 *
 * @see luaK_code, CREATE_ABC, getOpMode
 *
 * 参数检查：
 * - 验证操作码确实是ABC格式
 * - 检查B操作数的有效性
 * - 检查C操作数的有效性
 * - 确保参数在有效范围内
 *
 * 指令编码：
 * - 使用CREATE_ABC宏进行指令编码
 * - 将操作码和三个操作数打包为32位指令
 * - 保持指令格式的一致性
 *
 * 使用场景：
 * - 算术运算指令（ADD、SUB、MUL等）
 * - 逻辑运算指令（AND、OR、NOT等）
 * - 数据移动指令（MOVE、LOADK等）
 * - 函数调用指令（CALL、RETURN等）
 */
int luaK_codeABC (FuncState *fs, OpCode o, int a, int b, int c) {
    lua_assert(getOpMode(o) == iABC);
    lua_assert(getBMode(o) != OpArgN || b == 0);
    lua_assert(getCMode(o) != OpArgN || c == 0);
    return luaK_code(fs, CREATE_ABC(o, a, b, c), fs->ls->lastline);
}

/**
 * @brief 生成ABx格式的指令
 *
 * 编码并生成ABx或AsBx格式的字节码指令。这种格式包含一个
 * 操作码、一个A操作数和一个扩展的Bx操作数。
 *
 * @param fs 函数编译状态指针
 * @param o 操作码
 * @param a A操作数（通常是目标寄存器）
 * @param bc Bx操作数（扩展操作数，可以是无符号或有符号）
 * @return 指令在代码数组中的位置
 *
 * @note 支持ABx和AsBx两种格式
 * @note C操作数必须为空（OpArgN）
 *
 * @see luaK_code, CREATE_ABx, getOpMode
 *
 * 格式支持：
 * - iABx：A + 无符号Bx（如LOADK指令）
 * - iAsBx：A + 有符号sBx（如JMP指令）
 *
 * 参数检查：
 * - 验证操作码格式正确
 * - 确保C操作数未使用
 * - 检查Bx操作数范围
 *
 * 使用场景：
 * - 常量加载指令（LOADK）
 * - 跳转指令（JMP）
 * - 全局变量访问（GETGLOBAL、SETGLOBAL）
 * - 上值访问（GETUPVAL、SETUPVAL）
 */
int luaK_codeABx (FuncState *fs, OpCode o, int a, unsigned int bc) {
    lua_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);
    lua_assert(getCMode(o) == OpArgN);
    return luaK_code(fs, CREATE_ABx(o, a, bc), fs->ls->lastline);
}

/**
 * @brief 生成表列表设置指令
 *
 * 为表构造器生成SETLIST指令，用于批量设置表的数组部分。
 * 这是表构造优化的重要组成部分。
 *
 * @param fs 函数编译状态指针
 * @param base 表所在的寄存器
 * @param nelems 要设置的元素数量
 * @param tostore 实际存储的元素数量
 *
 * @note 支持大量元素的批量设置
 * @note 自动处理超出C操作数范围的情况
 *
 * @see luaK_codeABC, luaK_code, LFIELDS_PER_FLUSH
 *
 * 批量设置优化：
 * - 一次指令设置多个连续元素
 * - 减少指令数量和执行开销
 * - 提高表构造的效率
 *
 * 大数组处理：
 * - C操作数范围内：使用单条SETLIST指令
 * - 超出范围：使用SETLIST + 额外指令存储大数值
 * - 支持任意大小的数组构造
 *
 * 寄存器管理：
 * - 释放用于存储列表值的寄存器
 * - 只保留表本身的寄存器
 * - 优化寄存器使用效率
 *
 * 使用场景：
 * - 表构造器的数组部分（{1, 2, 3, ...}）
 * - 大型数组的初始化
 * - 批量数据的表存储
 * - 表构造优化的核心实现
 */
void luaK_setlist (FuncState *fs, int base, int nelems, int tostore) {
    int c =  (nelems - 1)/LFIELDS_PER_FLUSH + 1;
    int b = (tostore == LUA_MULTRET) ? 0 : tostore;
    lua_assert(tostore != 0);
    if (c <= MAXARG_C)
        luaK_codeABC(fs, OP_SETLIST, base, b, c);
    else {
        luaK_codeABC(fs, OP_SETLIST, base, b, 0);
        luaK_code(fs, cast(Instruction, c), fs->ls->lastline);
    }
    fs->freereg = base + 1;  /* 释放包含列表值的寄存器 */
}

/** @} */ /* 结束代码生成基础设施文档组 */

