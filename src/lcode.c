/*
** [核心] Lua代码生成器模块
**
** 功能概述：
** 负责将抽象语法树转换为Lua虚拟机字节码，是编译器的核心组件
**
** 主要功能：
** - 指令生成与优化（ABC、ABx、AsBx格式）
** - 跳转处理与回填机制
** - 表达式求值与寄存器分配
** - 常量折叠优化
** - 控制流结构的代码生成
**
** 模块依赖：
** - lparser.c：接收语法分析结果
** - lopcodes.h：使用虚拟机指令定义
** - lvm.c：为虚拟机执行提供字节码
**
** 算法复杂度：
** - 指令生成：O(n) 时间，n为AST节点数
** - 跳转回填：O(m) 时间，m为跳转指令数
**
** 相关文档：参见 docs/wiki_code.md
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


/*
** [入门] 检查表达式是否包含跳转指令
**
** 实现原理：
** 通过比较表达式的真跳转列表(t)和假跳转列表(f)是否相同来判断
** 如果两者不同，说明表达式包含条件跳转逻辑
**
** 用途：用于条件表达式的代码生成优化
*/
#define hasjumps(e) ((e)->t != (e)->f)


/*
** [入门] 判断表达式是否为数字字面量
**
** 检查条件：
** 1. 表达式类型为VKNUM（数字常量）
** 2. 没有真跳转（t == NO_JUMP）
** 3. 没有假跳转（f == NO_JUMP）
**
** @param e - expdesc*：表达式描述符
** @return int：是数字字面量返回1，否则返回0
**
** 用途：常量折叠优化的前置检查
*/
static int isnumeral(expdesc *e)
{
    return (e->k == VKNUM && e->t == NO_JUMP && e->f == NO_JUMP);
}


/*
** [核心] 生成LOADNIL指令，将连续寄存器设置为nil
**
** 功能详述：
** 这是一个重要的优化函数，会尝试将多个LOADNIL指令合并为一个
** 以减少字节码数量并提高执行效率
**
** 优化策略：
** 1. 检查前一条指令是否也是LOADNIL
** 2. 如果可以合并，扩展前一条指令的范围
** 3. 否则生成新的LOADNIL指令
**
** @param fs - FuncState*：函数编译状态
** @param from - int：起始寄存器编号
** @param n - int：连续寄存器数量
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只有在没有跳转目标时才能进行优化
** - 函数开始位置有特殊处理逻辑
*/
void luaK_nil(FuncState *fs, int from, int n)
{
    Instruction *previous;
    
    /*
    ** [优化] 检查是否没有跳转到当前位置
    ** 如果有跳转，就不能做优化，因为可能改变程序语义
    */
    if (fs->pc > fs->lasttarget) 
    {
        /*
        ** [特殊情况] 函数开始的处理
        ** 在函数开始时，某些寄存器可能已经是nil
        */
        if (fs->pc == 0) 
        {
            if (from >= fs->nactvar)
            {
                return;
            }
        }
        else 
        {
            previous = &fs->f->code[fs->pc - 1];
            
            /*
            ** [优化核心] 检查前一条指令是否为LOADNIL
            ** 如果是，尝试合并两个LOADNIL指令
            */
            if (GET_OPCODE(*previous) == OP_LOADNIL) 
            {
                int pfrom = GETARG_A(*previous);
                int pto = GETARG_B(*previous);
                
                /*
                ** [合并条件] 检查寄存器范围是否可以连接
                ** 条件：前一个范围的结束位置与当前范围的开始位置相邻或重叠
                */
                if (pfrom <= from && from <= pto + 1) 
                {
                    if (from + n - 1 > pto)
                    {
                        SETARG_B(*previous, from + n - 1);
                    }
                    return;
                }
            }
        }
    }
    
    /*
    ** [默认路径] 无法优化的情况，生成新的LOADNIL指令
    ** 指令格式：LOADNIL A B，将寄存器A到B设置为nil
    */
    luaK_codeABC(fs, OP_LOADNIL, from, from + n - 1, 0);
}


/*
** [核心] 生成无条件跳转指令
**
** 功能详述：
** 生成JMP指令实现无条件跳转，同时处理待处理的跳转列表连接
** 这是控制流编译的核心函数之一
**
** 算法步骤：
** 1. 保存当前待处理的跳转列表
** 2. 清空跳转列表，避免干扰新指令
** 3. 生成JMP指令，目标暂时未知
** 4. 将新跳转与之前的跳转列表连接
**
** @param fs - FuncState*：函数编译状态指针
**
** @return int：新生成的跳转指令在代码数组中的位置
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 跳转目标需要后续通过 luaK_patchlist 等函数回填
** - 会修改函数状态中的 jpc 字段
*/
int luaK_jump(FuncState *fs)
{
    // 保存当前待处理的跳转到此位置的指令列表
    int jpc = fs->jpc;
    int j;

    // 清空待处理跳转列表，避免影响新生成的指令
    fs->jpc = NO_JUMP;

    // 生成JMP指令，A参数为0，sBx参数暂时设为NO_JUMP
    // 实际跳转目标将在后续通过回填机制确定
    j = luaK_codeABx(fs, OP_JMP, 0, NO_JUMP);

    // 将新生成的跳转指令与之前保存的跳转列表连接
    // 形成跳转链，用于统一的目标地址回填
    luaK_concat(fs, &j, jpc);

    // 返回新生成的跳转指令的位置
    return j;
}


/*
** [入门] 生成函数返回指令
**
** 功能说明：
** 生成RETURN指令，用于从当前函数返回指定数量的值
** 这是函数调用机制的重要组成部分
**
** 指令格式：
** RETURN A B：返回寄存器A到A+B-2范围内的值
** 当B=0时，返回从A开始到栈顶的所有值
**
** @param fs - FuncState*：函数编译状态指针
** @param first - int：第一个返回值所在的寄存器编号
** @param nret - int：返回值的数量（LUA_MULTRET表示可变数量）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 相关指令：OP_RETURN
*/
void luaK_ret(FuncState *fs, int first, int nret)
{
    // 生成RETURN指令
    // A参数：第一个返回值的寄存器位置
    // B参数：返回值数量+1（+1是为了区分0个返回值和可变返回值）
    // C参数：对RETURN指令无意义，设为0
    luaK_codeABC(fs, OP_RETURN, first, nret + 1, 0);
}


/*
** [核心] 生成条件跳转指令
**
** 功能说明：
** 生成指定的条件测试指令，并在其后立即生成无条件跳转
** 这是条件控制流的基础构建块
**
** 工作机制：
** 1. 先生成指定的测试指令（如TEST、TESTSET等）
** 2. 立即生成JMP指令形成条件跳转结构
** 3. 返回跳转指令位置用于后续的目标回填
**
** @param fs - FuncState*：函数编译状态指针
** @param op - OpCode：条件测试指令的操作码
** @param A - int：指令的A参数（通常是测试的寄存器）
** @param B - int：指令的B参数（操作数或目标寄存器）
** @param C - int：指令的C参数（条件或操作数）
**
** @return int：生成的条件跳转指令在代码数组中的位置
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 测试指令与跳转指令必须相邻，形成原子的条件跳转
** - 返回的位置是JMP指令的位置，不是测试指令的位置
*/
static int condjump(FuncState *fs, OpCode op, int A, int B, int C)
{
    // 生成条件测试指令（如TEST、TESTSET等）
    // 该指令检查条件，如果条件不满足则跳过下一条指令
    luaK_codeABC(fs, op, A, B, C);

    // 立即生成无条件跳转指令
    // 形成完整的条件跳转结构：测试+跳转
    return luaK_jump(fs);
}


/*
** [核心] 修复跳转指令的目标地址
**
** 功能说明：
** 将之前生成的跳转指令的目标地址从占位符修改为实际目标
** 这是跳转回填机制的核心实现
**
** 工作原理：
** 1. 计算相对偏移量：目标位置减去跳转指令的下一条指令位置
** 2. 检查偏移量是否在有效范围内
** 3. 将计算出的偏移量写入跳转指令的sBx字段
**
** @param fs - FuncState*：函数编译状态指针
** @param pc - int：需要修复的跳转指令在代码数组中的位置
** @param dest - int：跳转的目标位置（绝对位置）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 错误处理：
** - 如果偏移量超出MAXARG_sBx范围，报告语法错误
** - 确保dest不是NO_JUMP（无效目标）
**
** 注意事项：
** - 偏移量是相对于跳转指令的下一条指令计算的
** - 负偏移量表示向后跳转，正偏移量表示向前跳转
*/
static void fixjump(FuncState *fs, int pc, int dest)
{
    // 获取需要修复的跳转指令的指针
    Instruction *jmp = &fs->f->code[pc];

    // 计算相对偏移量：目标位置 - (跳转指令位置 + 1)
    // +1是因为偏移量相对于跳转指令的下一条指令
    int offset = dest - (pc + 1);

    // 断言确保目标是有效的
    lua_assert(dest != NO_JUMP);

    // 检查偏移量是否在指令格式允许的范围内
    if (abs(offset) > MAXARG_sBx)
    {
        luaX_syntaxerror(fs->ls, "control structure too long");
    }

    // 将计算出的偏移量设置到跳转指令的sBx字段
    SETARG_sBx(*jmp, offset);
}


/*
** [入门] 获取当前PC并标记为跳转目标
**
** 功能说明：
** 获取当前程序计数器（PC）值，并将其标记为跳转目标位置
** 防止编译器进行可能破坏跳转语义的优化
**
** 重要性：
** 当某个位置可能成为跳转目标时，编译器需要知道这一信息
** 以避免进行连续指令合并等优化，确保跳转的正确性
**
** @param fs - FuncState*：函数编译状态指针
**
** @return int：当前程序计数器的值，即下一条指令的位置
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 用途：
** - 在生成循环、条件分支的目标标签时使用
** - 确保编译器不会错误地优化跳转目标附近的代码
*/
int luaK_getlabel(FuncState *fs)
{
    // 将当前PC标记为最后一个跳转目标
    // 这告诉编译器此位置可能有跳转指向，不能随意优化
    fs->lasttarget = fs->pc;

    // 返回当前PC值，即下一条指令将要生成的位置
    return fs->pc;
}


/*
** [入门] 获取跳转指令的目标位置
**
** 功能说明：
** 解析跳转指令中的相对偏移量，转换为绝对目标位置
** 用于遍历跳转链表和分析控制流
**
** 工作原理：
** 1. 提取指令的sBx字段（带符号偏移量）
** 2. 如果偏移量是NO_JUMP，表示链表结束
** 3. 否则将相对偏移转换为绝对位置
**
** @param fs - FuncState*：函数编译状态指针
** @param pc - int：跳转指令在代码数组中的位置
**
** @return int：跳转的绝对目标位置，或NO_JUMP表示链表结束
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - NO_JUMP偏移量表示跳转链表的结束
** - 偏移量是相对于跳转指令的下一条指令计算的
*/
static int getjump(FuncState *fs, int pc)
{
    // 从跳转指令中提取有符号偏移量
    int offset = GETARG_sBx(fs->f->code[pc]);

    // 检查是否为链表结束标记
    if (offset == NO_JUMP)
    {
        // 偏移量为NO_JUMP表示这是跳转链表的最后一个元素
        return NO_JUMP;
    }
    else
    {
        // 将相对偏移转换为绝对位置
        // (pc + 1) 是跳转指令的下一条指令位置
        return (pc + 1) + offset;
    }
}


/*
** [进阶] 获取跳转控制指令
**
** 功能说明：
** 获取控制跳转行为的指令指针，考虑测试模式指令的特殊性
** 某些指令（如TEST、TESTSET）会影响下一条指令的执行
**
** 工作机制：
** 1. 首先获取指定位置的指令
** 2. 检查前一条指令是否为测试模式指令
** 3. 如果是，返回前一条指令（真正的控制指令）
** 4. 否则返回当前指令
**
** @param fs - FuncState*：函数编译状态指针
** @param pc - int：指令位置
**
** @return Instruction*：控制跳转的指令指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 设计考虑：
** - 测试模式指令与跳转指令往往配对出现
** - 需要找到真正控制跳转逻辑的指令
*/
static Instruction *getjumpcontrol(FuncState *fs, int pc)
{
    // 获取指定位置的指令指针
    Instruction *pi = &fs->f->code[pc];

    // 检查是否有前一条指令，且前一条指令是测试模式指令
    if (pc >= 1 && testTMode(GET_OPCODE(*(pi - 1))))
    {
        // 如果前一条指令是测试模式指令，它才是真正的控制指令
        return pi - 1;
    }
    else
    {
        // 否则当前指令就是控制指令
        return pi;
    }
}


/*
** [进阶] 检查跳转列表中是否需要值
**
** 功能说明：
** 遍历跳转列表，检查是否有跳转需要产生具体的值
** 而不仅仅是控制流的改变
**
** 检查原理：
** - TESTSET指令既测试条件又设置值，不需要额外的值
** - 其他类型的跳转指令只控制流程，需要明确的值
**
** @param fs - FuncState*：函数编译状态指针
** @param list - int：跳转列表的头节点位置
**
** @return int：如果需要值返回1，否则返回0
**
** 算法复杂度：O(n) 时间，n为跳转列表长度
**
** 应用场景：
** - 条件表达式的代码生成优化
** - 确定是否需要生成值加载指令
*/
static int need_value(FuncState *fs, int list)
{
    // 遍历整个跳转列表
    for (; list != NO_JUMP; list = getjump(fs, list))
    {
        // 获取控制该跳转的指令
        Instruction i = *getjumpcontrol(fs, list);

        // 检查指令类型
        if (GET_OPCODE(i) != OP_TESTSET)
        {
            // 如果不是TESTSET指令，说明需要明确的值
            return 1;
        }
    }

    // 所有跳转都是TESTSET类型，不需要额外的值
    return 0;
}


/*
** [进阶] 修补测试寄存器
**
** 功能说明：
** 修改TESTSET指令的目标寄存器，或将其转换为TEST指令
** 这是条件表达式优化的关键机制
**
** 工作原理：
** 1. 检查指令是否为TESTSET类型
** 2. 如果需要修改目标寄存器且寄存器不同，修改A字段
** 3. 如果不需要存储值，将TESTSET转换为TEST指令
**
** @param fs - FuncState*：函数编译状态指针
** @param node - int：要修补的跳转节点位置
** @param reg - int：目标寄存器，NO_REG表示不需要存储值
**
** @return int：成功修补返回1，无法修补返回0
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 指令转换：
** - TESTSET A B C → TEST B 0 C（当reg == NO_REG时）
** - TESTSET A B C → TESTSET reg B C（当需要改变目标寄存器时）
*/
static int patchtestreg(FuncState *fs, int node, int reg)
{
    // 获取控制该跳转的指令
    Instruction *i = getjumpcontrol(fs, node);

    // 只能修补TESTSET指令
    if (GET_OPCODE(*i) != OP_TESTSET)
    {
        return 0;
    }

    // 根据寄存器参数决定修补方式
    if (reg != NO_REG && reg != GETARG_B(*i))
    {
        // 需要将值存储到不同的寄存器
        SETARG_A(*i, reg);
    }
    else
    {
        // 不需要存储值或值已在正确寄存器中
        // 将TESTSET转换为TEST指令
        *i = CREATE_ABC(OP_TEST, GETARG_B(*i), 0, GETARG_C(*i));
    }

    return 1;
}


/*
** [入门] 移除跳转列表中的值
**
** 功能说明：
** 遍历跳转列表，将所有TESTSET指令转换为TEST指令
** 用于不需要条件值的场景，只保留控制流功能
**
** 应用场景：
** 当条件表达式只用于控制流而不需要具体值时，
** 可以通过此函数优化掉不必要的值存储操作
**
** @param fs - FuncState*：函数编译状态指针
** @param list - int：跳转列表的头节点位置
**
** 算法复杂度：O(n) 时间，n为跳转列表长度
**
** 优化效果：
** - 减少不必要的寄存器写入操作
** - 简化指令序列，提高执行效率
*/
static void removevalues(FuncState *fs, int list)
{
    // 遍历整个跳转列表
    for (; list != NO_JUMP; list = getjump(fs, list))
    {
        // 尝试将每个节点的TESTSET指令转换为TEST指令
        // 使用NO_REG表示不需要存储任何值
        patchtestreg(fs, list, NO_REG);
    }
}


/*
** [核心] 修补跳转列表的辅助函数
**
** 功能说明：
** 遍历跳转列表，根据指令类型将跳转目标设置为不同的地址
** 这是跳转回填机制的核心实现函数
**
** 处理策略：
** 1. 对于TESTSET指令：修补寄存器后跳转到vtarget
** 2. 对于其他指令：直接跳转到dtarget
**
** @param fs - FuncState*：函数编译状态指针
** @param list - int：跳转列表的头节点位置
** @param vtarget - int：值目标地址（TESTSET指令使用）
** @param reg - int：目标寄存器（传递给patchtestreg）
** @param dtarget - int：默认目标地址（其他指令使用）
**
** 算法复杂度：O(n) 时间，n为跳转列表长度
**
** 设计原理：
** - 不同类型的跳转指令需要不同的目标地址
** - TESTSET指令需要特殊处理，因为它既测试又赋值
*/
static void patchlistaux(FuncState *fs, int list, int vtarget, int reg, int dtarget)
{
    // 遍历整个跳转列表
    while (list != NO_JUMP)
    {
        // 保存下一个节点的位置
        int next = getjump(fs, list);

        // 尝试修补当前节点的测试寄存器
        if (patchtestreg(fs, list, reg))
        {
            // 如果是TESTSET指令，跳转到值目标
            fixjump(fs, list, vtarget);
        }
        else
        {
            // 其他类型的指令，跳转到默认目标
            fixjump(fs, list, dtarget);
        }

        // 移动到下一个节点
        list = next;
    }
}


/*
** [入门] 释放待处理的跳转列表
**
** 功能说明：
** 将函数状态中积累的待处理跳转列表（jpc）全部回填为当前位置
** 这通常在代码生成的关键点调用，确保之前的跳转能正确到达当前位置
**
** 工作流程：
** 1. 将所有待处理跳转的目标设置为当前PC
** 2. 清空待处理跳转列表
**
** @param fs - FuncState*：函数编译状态指针
**
** 算法复杂度：O(n) 时间，n为待处理跳转的数量
**
** 调用时机：
** - 在PC即将改变之前
** - 在生成新指令之前
** - 确保跳转语义的正确性
*/
static void dischargejpc(FuncState *fs)
{
    // 将所有待处理的跳转列表回填为当前PC位置
    // 参数说明：
    // - fs->jpc: 待处理跳转列表
    // - fs->pc: 当前程序计数器（目标位置）
    // - NO_REG: 不指定特定寄存器
    // - fs->pc: 默认目标也是当前位置
    patchlistaux(fs, fs->jpc, fs->pc, NO_REG, fs->pc);

    // 清空待处理跳转列表
    fs->jpc = NO_JUMP;
}


/*
** [核心] 修补跳转列表到指定目标
**
** 功能说明：
** 将跳转列表中的所有跳转指令回填为指定的目标位置
** 根据目标位置的不同选择最优的处理策略
**
** 优化策略：
** 1. 如果目标是当前位置，使用更高效的 luaK_patchtohere
** 2. 否则使用通用的回填函数处理
**
** @param fs - FuncState*：函数编译状态指针
** @param list - int：跳转列表的头节点位置
** @param target - int：跳转的目标位置
**
** 算法复杂度：O(n) 时间，n为跳转列表长度
**
** 前置条件：target必须小于或等于当前PC
**
** 应用场景：
** - 控制流结构的跳转回填
** - 循环和条件语句的代码生成
*/
void luaK_patchlist(FuncState *fs, int list, int target)
{
    if (target == fs->pc)
    {
        // 目标是当前位置，使用专门的优化函数
        luaK_patchtohere(fs, list);
    }
    else
    {
        // 确保目标位置在当前PC之前（向前跳转或已生成的位置）
        lua_assert(target < fs->pc);

        // 使用通用的回填函数处理
        patchlistaux(fs, list, target, NO_REG, target);
    }
}


/*
** [入门] 修补跳转列表到当前位置
**
** 功能说明：
** 将跳转列表中的所有跳转回填为当前位置，并处理待处理跳转列表
** 这是最常用的跳转回填操作，用于"落地"到当前代码位置
**
** 工作流程：
** 1. 获取当前位置的标签（标记为跳转目标）
** 2. 将传入的跳转列表合并到待处理列表中
**
** @param fs - FuncState*：函数编译状态指针
** @param list - int：要回填的跳转列表
**
** 算法复杂度：O(n) 时间，n为跳转列表长度
**
** 设计优势：
** - 延迟处理：将跳转加入待处理列表，统一在合适时机处理
** - 效率优化：避免重复的目标地址计算
*/
void luaK_patchtohere(FuncState *fs, int list)
{
    // 获取当前位置的标签，标记为跳转目标
    luaK_getlabel(fs);

    // 将传入的跳转列表合并到函数状态的待处理跳转列表中
    // 这些跳转将在下次调用 dischargejpc 时统一回填
    luaK_concat(fs, &fs->jpc, list);
}


/*
** [核心] 连接两个跳转列表
**
** 功能说明：
** 将两个跳转列表连接成一个单一的链表结构
** 这是跳转列表管理的基础操作
**
** 连接策略：
** 1. 如果第二个列表为空，无需操作
** 2. 如果第一个列表为空，直接赋值
** 3. 否则找到第一个列表的末尾，连接第二个列表
**
** @param fs - FuncState*：函数编译状态指针
** @param l1 - int*：第一个列表的指针（会被修改）
** @param l2 - int：第二个列表的头节点
**
** 算法复杂度：O(n) 时间，n为第一个列表的长度
**
** 链表结构：
** 跳转列表通过指令的sBx字段形成单向链表
** 链表末尾的sBx字段为NO_JUMP
*/
void luaK_concat(FuncState *fs, int *l1, int l2)
{
    if (l2 == NO_JUMP)
    {
        // 第二个列表为空，无需连接
        return;
    }
    else if (*l1 == NO_JUMP)
    {
        // 第一个列表为空，直接将第二个列表赋值给第一个
        *l1 = l2;
    }
    else
    {
        // 两个列表都不为空，需要找到第一个列表的末尾
        int list = *l1;
        int next;

        // 遍历第一个列表，找到最后一个元素
        while ((next = getjump(fs, list)) != NO_JUMP)
        {
            list = next;
        }

        // 将第一个列表的最后一个元素连接到第二个列表
        fixjump(fs, list, l2);
    }
}


/*
** [入门] 检查栈空间是否足够
**
** 功能说明：
** 检查当前函数的栈空间是否足够容纳额外的n个寄存器
** 如果不够则更新函数的最大栈大小，如果超出限制则报错
**
** 安全检查：
** 1. 计算需要的新栈大小
** 2. 检查是否超出虚拟机限制
** 3. 更新函数的最大栈大小记录
**
** @param fs - FuncState*：函数编译状态指针
** @param n - int：需要的额外栈空间大小
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 错误处理：
** 如果栈需求超过MAXSTACK，报告语法错误
**
** 重要性：
** 确保运行时栈溢出不会发生，保证程序安全性
*/
void luaK_checkstack(FuncState *fs, int n)
{
    // 计算需要的新栈大小：当前空闲寄存器位置 + 额外需求
    int newstack = fs->freereg + n;

    // 检查是否需要更新函数的最大栈大小
    if (newstack > fs->f->maxstacksize)
    {
        // 检查是否超出虚拟机的栈大小限制
        if (newstack >= MAXSTACK)
        {
            luaX_syntaxerror(fs->ls, "function or expression too complex");
        }

        // 更新函数的最大栈大小记录
        fs->f->maxstacksize = cast_byte(newstack);
    }
}


/*
** [入门] 预留寄存器空间
**
** 功能说明：
** 预留指定数量的寄存器空间，确保后续操作有足够的栈空间
** 这是寄存器分配管理的基础操作
**
** 工作流程：
** 1. 检查栈空间是否足够
** 2. 向前移动空闲寄存器指针
**
** @param fs - FuncState*：函数编译状态指针
** @param n - int：要预留的寄存器数量
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 副作用：
** 修改fs->freereg，指向下一个可用的寄存器位置
**
** 使用场景：
** - 函数调用前预留参数空间
** - 表达式计算前预留临时变量空间
*/
void luaK_reserveregs(FuncState *fs, int n)
{
    // 首先检查栈空间是否足够容纳新的寄存器
    luaK_checkstack(fs, n);

    // 向前移动空闲寄存器指针，标记这些寄存器为已使用
    fs->freereg += n;
}


/*
** [入门] 释放单个寄存器
**
** 功能说明：
** 释放指定的寄存器，将其标记为可重新使用
** 只有非常量且非局部变量的寄存器才能被释放
**
** 释放条件：
** 1. 不是常量寄存器（!ISK(reg)）
** 2. 寄存器编号大于等于活跃变量数（reg >= fs->nactvar）
** 3. 必须是最后分配的寄存器
**
** @param fs - FuncState*：函数编译状态指针
** @param reg - int：要释放的寄存器编号
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 安全检查：
** 确保释放的寄存器确实是最后分配的，维护栈的LIFO特性
*/
static void freereg(FuncState *fs, int reg)
{
    // 检查释放条件：
    // 1. 不是常量（ISK检查高位标志）
    // 2. 不是局部变量寄存器
    if (!ISK(reg) && reg >= fs->nactvar)
    {
        // 向后移动空闲寄存器指针
        fs->freereg--;

        // 断言确保释放的确实是最后分配的寄存器
        // 这维护了寄存器栈的LIFO（后进先出）特性
        lua_assert(reg == fs->freereg);
    }
}


/*
** [入门] 释放表达式占用的寄存器
**
** 功能说明：
** 如果表达式占用了临时寄存器，则释放该寄存器
** 只有VNONRELOC类型的表达式可能占用需要释放的寄存器
**
** 表达式类型检查：
** VNONRELOC表示值存储在不可重定位的寄存器中
** 这种寄存器可能是临时分配的，需要在使用后释放
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 设计原理：
** 避免寄存器泄漏，确保临时寄存器能够被重复使用
*/
static void freeexp(FuncState *fs, expdesc *e)
{
    // 只有VNONRELOC类型的表达式占用需要释放的寄存器
    if (e->k == VNONRELOC)
    {
        // 释放表达式占用的寄存器
        freereg(fs, e->u.s.info);
    }
}


/*
** [核心] 添加常量到常量表
**
** 功能说明：
** 将常量值添加到函数的常量表中，如果常量已存在则返回现有索引
** 这是常量管理和去重的核心机制
**
** 去重机制：
** 1. 使用哈希表检查常量是否已存在
** 2. 如果存在，返回已有的索引
** 3. 如果不存在，创建新的常量表项
**
** @param fs - FuncState*：函数编译状态指针
** @param k - TValue*：用作哈希表键的值
** @param v - TValue*：要添加的常量值
**
** @return int：常量在常量表中的索引
**
** 算法复杂度：O(1) 平均时间（哈希表查找），O(n) 最坏情况
**
** 内存管理：
** - 可能触发常量表的重新分配
** - 设置垃圾回收屏障确保引用安全
*/
static int addk(FuncState *fs, TValue *k, TValue *v)
{
    lua_State *L = fs->L;
    TValue *idx = luaH_set(L, fs->h, k);
    Proto *f = fs->f;
    int oldsize = f->sizek;

    // 检查常量是否已经存在于哈希表中
    if (ttisnumber(idx))
    {
        // 常量已存在，验证值的一致性并返回索引
        lua_assert(luaO_rawequalObj(&fs->f->k[cast_int(nvalue(idx))], v));
        return cast_int(nvalue(idx));
    }
    else
    {
        // 常量不存在，需要创建新的条目

        // 在哈希表中设置新常量的索引
        setnvalue(idx, cast_num(fs->nk));

        // 扩展常量表以容纳新常量
        luaM_growvector(L, f->k, fs->nk, f->sizek, TValue,
                        MAXARG_Bx, "constant table overflow");

        // 初始化新增的常量表槽位为nil
        while (oldsize < f->sizek)
        {
            setnilvalue(&f->k[oldsize++]);
        }

        // 设置新常量的值
        setobj(L, &f->k[fs->nk], v);

        // 设置垃圾回收屏障，确保常量不会被意外回收
        luaC_barrier(L, f, v);

        // 返回新常量的索引，并递增计数器
        return fs->nk++;
    }
}


/*
** [入门] 添加字符串常量
**
** 功能说明：
** 将字符串对象添加到函数的常量表中
** 使用字符串本身作为哈希表的键和值
**
** @param fs - FuncState*：函数编译状态指针
** @param s - TString*：要添加的字符串对象
**
** @return int：字符串常量在常量表中的索引
**
** 算法复杂度：O(1) 平均时间
**
** 设计特点：
** 字符串作为键和值都是同一个对象，实现了高效的去重
*/
int luaK_stringK(FuncState *fs, TString *s)
{
    TValue o;

    // 将字符串对象包装为TValue
    setsvalue(fs->L, &o, s);

    // 使用字符串本身作为键和值调用通用的添加函数
    return addk(fs, &o, &o);
}


/*
** [入门] 添加数字常量
**
** 功能说明：
** 将数字值添加到函数的常量表中
** 使用数字本身作为哈希表的键和值
**
** @param fs - FuncState*：函数编译状态指针
** @param r - lua_Number：要添加的数字值
**
** @return int：数字常量在常量表中的索引
**
** 算法复杂度：O(1) 平均时间
**
** 浮点数处理：
** 相同的数字值（包括浮点数）会被去重存储
*/
int luaK_numberK(FuncState *fs, lua_Number r)
{
    TValue o;

    // 将数字值包装为TValue
    setnvalue(&o, r);

    // 使用数字本身作为键和值调用通用的添加函数
    return addk(fs, &o, &o);
}


/*
** [入门] 添加布尔常量
**
** 功能说明：
** 将布尔值（true或false）添加到函数的常量表中
** 使用布尔值本身作为哈希表的键和值
**
** @param fs - FuncState*：函数编译状态指针
** @param b - int：布尔值（0表示false，非0表示true）
**
** @return int：布尔常量在常量表中的索引
**
** 算法复杂度：O(1) 平均时间
**
** 优化效果：
** 所有true值共享同一个常量表项，所有false值也共享同一个常量表项
*/
static int boolK(FuncState *fs, int b)
{
    TValue o;

    // 将布尔值包装为TValue
    setbvalue(&o, b);

    // 使用布尔值本身作为键和值调用通用的添加函数
    return addk(fs, &o, &o);
}


/*
** [入门] 添加nil常量
**
** 功能说明：
** 将nil值添加到函数的常量表中
** 由于nil不能作为哈希表的键，使用哈希表本身作为键
**
** 特殊处理：
** nil值在Lua哈希表中不能作为键，因此使用哈希表对象本身
** 作为键来代表nil常量
**
** @param fs - FuncState*：函数编译状态指针
**
** @return int：nil常量在常量表中的索引
**
** 算法复杂度：O(1) 平均时间
**
** 设计巧思：
** 使用常量表的哈希表自身作为nil的代表键，确保唯一性
*/
static int nilK(FuncState *fs)
{
    TValue k, v;

    // 设置值为nil
    setnilvalue(&v);

    // 由于nil不能用作哈希表键，使用哈希表本身作为键
    // 这是一个巧妙的设计，确保nil常量的唯一性
    sethvalue(fs->L, &k, fs->h);

    // 调用通用的添加函数
    return addk(fs, &k, &v);
}


/*
** [核心] 设置函数调用的返回值数量
**
** 功能说明：
** 根据表达式类型设置函数调用或可变参数的返回值数量
** 这影响栈上值的分配和后续指令的参数处理
**
** 处理的表达式类型：
** - VCALL：开放的函数调用，可以返回可变数量的值
** - VVARARG：可变参数表达式
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
** @param nresults - int：期望的返回值数量
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 指令修改：
** - 对VCALL：修改指令的C参数为nresults+1
** - 对VVARARG：修改指令的B参数为nresults+1，设置A参数
*/
void luaK_setreturns(FuncState *fs, expdesc *e, int nresults)
{
    if (e->k == VCALL)
    {
        // 表达式是开放的函数调用
        // 设置返回值数量（+1是指令格式的要求）
        SETARG_C(getcode(fs, e), nresults + 1);
    }
    else if (e->k == VVARARG)
    {
        // 表达式是可变参数
        // 设置返回值数量和目标寄存器
        SETARG_B(getcode(fs, e), nresults + 1);
        SETARG_A(getcode(fs, e), fs->freereg);

        // 为可变参数预留一个寄存器
        luaK_reserveregs(fs, 1);
    }
}


/*
** [入门] 设置表达式为单一返回值
**
** 功能说明：
** 将多返回值表达式（函数调用或可变参数）转换为单一返回值
** 确保表达式只产生一个值用于后续计算
**
** 转换处理：
** - VCALL → VNONRELOC：固定返回值数量为1
** - VVARARG → VRELOCABLE：设置返回值数量为2（实际1个值）
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 副作用：
** 修改表达式的类型和属性，影响后续的代码生成
*/
void luaK_setoneret(FuncState *fs, expdesc *e)
{
    if (e->k == VCALL)
    {
        // 函数调用表达式：固定为返回一个值
        e->k = VNONRELOC;
        e->u.s.info = GETARG_A(getcode(fs, e));
    }
    else if (e->k == VVARARG)
    {
        // 可变参数表达式：设置返回值数量为2（表示1个实际值）
        SETARG_B(getcode(fs, e), 2);
        e->k = VRELOCABLE;  // 可以重定位其简单结果
    }
}


/*
** [核心] 释放变量表达式
**
** 功能说明：
** 将各种类型的变量表达式转换为可执行的值形式
** 生成相应的取值指令（如GETUPVAL、GETGLOBAL等）
**
** 处理的表达式类型：
** - VLOCAL：局部变量，直接转换为VNONRELOC
** - VUPVAL：上值变量，生成GETUPVAL指令
** - VGLOBAL：全局变量，生成GETGLOBAL指令
** - VINDEXED：索引表达式，生成GETTABLE指令
** - VVARARG/VCALL：多返回值表达式，转换为单返回值
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 副作用：
** 可能生成新的指令，修改表达式类型和寄存器分配
*/
void luaK_dischargevars(FuncState *fs, expdesc *e)
{
    switch (e->k)
    {
        case VLOCAL:
        {
            // 局部变量：直接标记为不可重定位的寄存器值
            e->k = VNONRELOC;
            break;
        }

        case VUPVAL:
        {
            // 上值变量：生成GETUPVAL指令获取上值
            e->u.s.info = luaK_codeABC(fs, OP_GETUPVAL, 0, e->u.s.info, 0);
            e->k = VRELOCABLE;
            break;
        }

        case VGLOBAL:
        {
            // 全局变量：生成GETGLOBAL指令获取全局值
            e->u.s.info = luaK_codeABx(fs, OP_GETGLOBAL, 0, e->u.s.info);
            e->k = VRELOCABLE;
            break;
        }

        case VINDEXED:
        {
            // 索引表达式：生成GETTABLE指令
            // 首先释放索引和表的寄存器
            freereg(fs, e->u.s.aux);
            freereg(fs, e->u.s.info);

            // 生成GETTABLE指令：表[索引]
            e->u.s.info = luaK_codeABC(fs, OP_GETTABLE, 0, e->u.s.info, e->u.s.aux);
            e->k = VRELOCABLE;
            break;
        }

        case VVARARG:
        case VCALL:
        {
            // 多返回值表达式：转换为单返回值
            luaK_setoneret(fs, e);
            break;
        }

        default:
        {
            // 其他类型已经有值可用，无需处理
            break;
        }
    }
}


/*
** [进阶] 生成布尔标签代码
**
** 功能说明：
** 生成LOADBOOL指令，可选择性地跳过下一条指令
** 这是条件表达式代码生成的重要组件
**
** 指令功能：
** LOADBOOL A B C：将布尔值B加载到寄存器A，如果C非0则跳过下一条指令
**
** @param fs - FuncState*：函数编译状态指针
** @param A - int：目标寄存器编号
** @param b - int：布尔值（0或1）
** @param jump - int：是否跳过下一条指令（0或1）
**
** @return int：生成的指令在代码数组中的位置
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用场景：
** 条件表达式的真假分支代码生成
*/
static int code_label(FuncState *fs, int A, int b, int jump)
{
    // 标记当前位置为跳转目标（这些指令可能是跳转目标）
    luaK_getlabel(fs);

    // 生成LOADBOOL指令
    return luaK_codeABC(fs, OP_LOADBOOL, A, b, jump);
}


/*
** [核心] 将表达式值放入指定寄存器
**
** 功能说明：
** 根据表达式类型生成相应的指令，将表达式的值加载到指定寄存器
** 这是表达式求值的核心函数之一
**
** 处理的表达式类型：
** - VNIL：生成LOADNIL指令
** - VFALSE/VTRUE：生成LOADBOOL指令
** - VK：生成LOADK指令（常量）
** - VKNUM：将数字转为常量后生成LOADK指令
** - VRELOCABLE：修改已生成指令的目标寄存器
** - VNONRELOC：如果需要则生成MOVE指令
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
** @param reg - int：目标寄存器编号
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 副作用：
** 修改表达式类型为VNONRELOC，设置info为目标寄存器
*/
static void discharge2reg(FuncState *fs, expdesc *e, int reg)
{
    // 首先处理变量表达式，转换为值表达式
    luaK_dischargevars(fs, e);

    switch (e->k)
    {
        case VNIL:
        {
            // nil值：生成LOADNIL指令
            luaK_nil(fs, reg, 1);
            break;
        }

        case VFALSE:
        case VTRUE:
        {
            // 布尔值：生成LOADBOOL指令
            luaK_codeABC(fs, OP_LOADBOOL, reg, e->k == VTRUE, 0);
            break;
        }

        case VK:
        {
            // 常量：生成LOADK指令
            luaK_codeABx(fs, OP_LOADK, reg, e->u.s.info);
            break;
        }

        case VKNUM:
        {
            // 数字常量：先添加到常量表，再生成LOADK指令
            luaK_codeABx(fs, OP_LOADK, reg, luaK_numberK(fs, e->u.nval));
            break;
        }

        case VRELOCABLE:
        {
            // 可重定位的指令：直接修改目标寄存器
            Instruction *pc = &getcode(fs, e);
            SETARG_A(*pc, reg);
            break;
        }

        case VNONRELOC:
        {
            // 已在寄存器中的值：如果不在目标寄存器则生成MOVE指令
            if (reg != e->u.s.info)
            {
                luaK_codeABC(fs, OP_MOVE, reg, e->u.s.info, 0);
            }
            break;
        }

        default:
        {
            // VVOID或VJMP类型：无需任何操作
            lua_assert(e->k == VVOID || e->k == VJMP);
            return;
        }
    }

    // 更新表达式描述符
    e->u.s.info = reg;
    e->k = VNONRELOC;
}


/*
** [入门] 将表达式值放入任意寄存器
**
** 功能说明：
** 如果表达式不在寄存器中，则分配一个新寄存器并将值加载到其中
** 确保表达式有一个确定的寄存器位置
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 副作用：
** 可能分配新的寄存器，修改表达式类型为VNONRELOC
*/
static void discharge2anyreg(FuncState *fs, expdesc *e)
{
    if (e->k != VNONRELOC)
    {
        // 表达式不在寄存器中，需要分配新寄存器
        luaK_reserveregs(fs, 1);
        discharge2reg(fs, e, fs->freereg - 1);
    }
}


/*
** [核心] 将表达式转换为寄存器值
**
** 功能说明：
** 将表达式转换为存储在指定寄存器中的值，处理跳转逻辑
** 这是处理含有条件跳转的复杂表达式的核心函数
**
** 复杂处理：
** 1. 基本值转换：调用discharge2reg处理基本情况
** 2. 跳转处理：如果表达式是VJMP，将跳转合并到真列表
** 3. 条件分支：如果有跳转，生成相应的真假分支代码
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
** @param reg - int：目标寄存器编号
**
** 算法复杂度：O(1) 时间，O(n) 空间（n为跳转列表长度）
**
** 跳转处理逻辑：
** - 如果表达式需要值，生成LOADBOOL指令
** - 分别处理真跳转列表和假跳转列表
*/
static void exp2reg(FuncState *fs, expdesc *e, int reg)
{
    // 基本的寄存器转换
    discharge2reg(fs, e, reg);

    // 如果表达式是跳转类型，将跳转合并到真列表
    if (e->k == VJMP)
    {
        luaK_concat(fs, &e->t, e->u.s.info);
    }

    // 处理包含跳转的表达式
    if (hasjumps(e))
    {
        int final;       // 整个表达式后的位置
        int p_f = NO_JUMP;  // 可能的LOAD false位置
        int p_t = NO_JUMP;  // 可能的LOAD true位置

        // 检查是否需要为跳转生成值
        if (need_value(fs, e->t) || need_value(fs, e->f))
        {
            // 生成跳过真假值设置的跳转
            int fj = (e->k == VJMP) ? NO_JUMP : luaK_jump(fs);

            // 生成假值分支：加载false，跳过下一条指令
            p_f = code_label(fs, reg, 0, 1);

            // 生成真值分支：加载true，不跳过
            p_t = code_label(fs, reg, 1, 0);

            // 回填跳过分支
            luaK_patchtohere(fs, fj);
        }

        // 获取最终位置标签
        final = luaK_getlabel(fs);

        // 回填假跳转列表和真跳转列表
        patchlistaux(fs, e->f, final, reg, p_f);
        patchlistaux(fs, e->t, final, reg, p_t);
    }

    // 清理跳转列表并设置最终状态
    e->f = e->t = NO_JUMP;
    e->u.s.info = reg;
    e->k = VNONRELOC;
}


/*
** [入门] 将表达式转换为下一个可用寄存器
**
** 功能说明：
** 将表达式转换为存储在下一个可用寄存器中的值
** 这是最常用的表达式求值函数之一
**
** 处理流程：
** 1. 释放变量表达式（转换为值）
** 2. 释放表达式当前占用的寄存器
** 3. 预留一个新寄存器
** 4. 将表达式转换到新寄存器
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 用途：
** 函数参数求值、赋值操作等需要明确寄存器位置的场景
*/
void luaK_exp2nextreg(FuncState *fs, expdesc *e)
{
    // 处理变量表达式，转换为值形式
    luaK_dischargevars(fs, e);

    // 释放表达式当前占用的寄存器
    freeexp(fs, e);

    // 预留下一个可用寄存器
    luaK_reserveregs(fs, 1);

    // 将表达式转换到预留的寄存器
    exp2reg(fs, e, fs->freereg - 1);
}


/*
** [核心] 将表达式转换为任意寄存器
**
** 功能说明：
** 智能地将表达式转换为寄存器值，优化寄存器的使用
** 尽可能重用已有的寄存器，避免不必要的MOVE指令
**
** 优化策略：
** 1. 如果表达式已在寄存器中且无跳转，直接返回
** 2. 如果在非局部变量寄存器中，尝试就地转换
** 3. 否则使用标准的下一个寄存器转换
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** @return int：表达式值所在的寄存器编号
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 优化效果：
** 减少不必要的寄存器分配和MOVE指令生成
*/
int luaK_exp2anyreg(FuncState *fs, expdesc *e)
{
    // 首先处理变量表达式
    luaK_dischargevars(fs, e);

    if (e->k == VNONRELOC)
    {
        // 表达式已在寄存器中
        if (!hasjumps(e))
        {
            // 没有跳转，可以直接使用当前寄存器
            return e->u.s.info;
        }

        if (e->u.s.info >= fs->nactvar)
        {
            // 寄存器不是局部变量，可以就地转换
            exp2reg(fs, e, e->u.s.info);
            return e->u.s.info;
        }
    }

    // 默认处理：使用下一个可用寄存器
    luaK_exp2nextreg(fs, e);
    return e->u.s.info;
}


/*
** [入门] 将表达式转换为值
**
** 功能说明：
** 确保表达式有确定的值，如果有跳转则转换为寄存器值
** 否则只进行变量释放处理
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 选择策略：
** 有跳转的表达式需要寄存器，无跳转的只需变量释放
*/
void luaK_exp2val(FuncState *fs, expdesc *e)
{
    if (hasjumps(e))
    {
        // 有跳转的表达式需要转换为寄存器值
        luaK_exp2anyreg(fs, e);
    }
    else
    {
        // 无跳转的表达式只需要变量释放
        luaK_dischargevars(fs, e);
    }
}


/*
** [核心] 将表达式转换为RK格式
**
** 功能说明：
** 将表达式转换为RK格式（寄存器或常量），用于指令的操作数
** RK格式允许操作数既可以是寄存器也可以是常量索引
**
** 优化策略：
** 1. 对于适合的常量，直接使用常量索引（RKASK编码）
** 2. 对于不适合的表达式，转换为寄存器值
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** @return int：RK值（寄存器编号或编码的常量索引）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** RK编码：
** - 常量：高位置1 + 常量索引
** - 寄存器：直接使用寄存器编号
*/
int luaK_exp2RK(FuncState *fs, expdesc *e)
{
    // 确保表达式有确定的值
    luaK_exp2val(fs, e);

    switch (e->k)
    {
        case VKNUM:
        case VTRUE:
        case VFALSE:
        case VNIL:
        {
            // 字面量常量，检查是否适合RK操作数
            if (fs->nk <= MAXINDEXRK)
            {
                // 添加到常量表并返回编码的常量索引
                e->u.s.info = (e->k == VNIL)  ? nilK(fs) :
                              (e->k == VKNUM) ? luaK_numberK(fs, e->u.nval) :
                                                boolK(fs, (e->k == VTRUE));
                e->k = VK;
                return RKASK(e->u.s.info);
            }
            else
            {
                // 常量表太大，转为寄存器
                break;
            }
        }

        case VK:
        {
            // 已经是常量，检查索引范围
            if (e->u.s.info <= MAXINDEXRK)
            {
                return RKASK(e->u.s.info);
            }
            else
            {
                // 常量索引超出范围，转为寄存器
                break;
            }
        }

        default:
        {
            // 其他类型无法优化为常量
            break;
        }
    }

    // 不是合适范围内的常量，转换为寄存器
    return luaK_exp2anyreg(fs, e);
}


/*
** [核心] 存储变量值
**
** 功能说明：
** 根据变量类型生成相应的赋值指令，将表达式的值存储到变量中
** 处理各种类型的左值：局部变量、上值、全局变量、表索引
**
** 支持的变量类型：
** - VLOCAL：局部变量，直接在寄存器中赋值
** - VUPVAL：上值变量，生成SETUPVAL指令
** - VGLOBAL：全局变量，生成SETGLOBAL指令
** - VINDEXED：表索引，生成SETTABLE指令
**
** @param fs - FuncState*：函数编译状态指针
** @param var - expdesc*：变量表达式（左值）
** @param ex - expdesc*：要存储的表达式（右值）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 优化策略：
** 局部变量使用就地赋值，避免额外的MOVE指令
*/
void luaK_storevar(FuncState *fs, expdesc *var, expdesc *ex)
{
    switch (var->k)
    {
        case VLOCAL:
        {
            // 局部变量：释放右值表达式，然后就地赋值
            freeexp(fs, ex);
            exp2reg(fs, ex, var->u.s.info);
            return;
        }

        case VUPVAL:
        {
            // 上值变量：将右值转为寄存器，生成SETUPVAL指令
            int e = luaK_exp2anyreg(fs, ex);
            luaK_codeABC(fs, OP_SETUPVAL, e, var->u.s.info, 0);
            break;
        }

        case VGLOBAL:
        {
            // 全局变量：将右值转为寄存器，生成SETGLOBAL指令
            int e = luaK_exp2anyreg(fs, ex);
            luaK_codeABx(fs, OP_SETGLOBAL, e, var->u.s.info);
            break;
        }

        case VINDEXED:
        {
            // 表索引：将右值转为RK格式，生成SETTABLE指令
            int e = luaK_exp2RK(fs, ex);
            luaK_codeABC(fs, OP_SETTABLE, var->u.s.info, var->u.s.aux, e);
            break;
        }

        default:
        {
            // 无效的变量类型
            lua_assert(0);
            break;
        }
    }

    // 释放右值表达式占用的资源
    freeexp(fs, ex);
}


/*
** [核心] 生成self调用的代码
**
** 功能说明：
** 为方法调用生成SELF指令，实现Lua的语法糖 obj:method()
** SELF指令同时完成方法查找和self参数设置
**
** 语法转换：
** obj:method(args) → obj.method(obj, args)
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：对象表达式
** @param key - expdesc*：方法名表达式
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 指令效果：
** SELF指令将方法放入func寄存器，对象放入func+1寄存器
**
** 寄存器布局：
** func: 方法函数
** func+1: self参数（对象本身）
** func+2+: 其他参数
*/
void luaK_self(FuncState *fs, expdesc *e, expdesc *key)
{
    int func;

    // 确保对象表达式在寄存器中
    luaK_exp2anyreg(fs, e);
    freeexp(fs, e);

    // 预留函数和self参数的寄存器空间
    func = fs->freereg;
    luaK_reserveregs(fs, 2);

    // 生成SELF指令：func[0] = e[key], func[1] = e
    luaK_codeABC(fs, OP_SELF, func, e->u.s.info, luaK_exp2RK(fs, key));

    // 释放方法名表达式
    freeexp(fs, key);

    // 更新表达式为函数寄存器
    e->u.s.info = func;
    e->k = VNONRELOC;
}


/*
** [进阶] 反转跳转条件
**
** 功能说明：
** 反转测试指令的跳转条件，用于逻辑优化
** 将"为真跳转"改为"为假跳转"，或相反
**
** 工作原理：
** 通过修改测试指令的A参数来反转条件
** A参数控制测试结果的逻辑（0或1）
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 限制条件：
** 只能反转测试模式的指令，不能是TESTSET或TEST
*/
static void invertjump(FuncState *fs, expdesc *e)
{
    // 获取控制跳转的指令
    Instruction *pc = getjumpcontrol(fs, e->u.s.info);

    // 确保是可以反转的测试指令
    lua_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != OP_TESTSET &&
                                             GET_OPCODE(*pc) != OP_TEST);

    // 反转A参数：0变1，1变0
    SETARG_A(*pc, !(GETARG_A(*pc)));
}


/*
** [核心] 根据条件生成跳转
**
** 功能说明：
** 根据表达式类型和期望的跳转条件生成相应的条件跳转指令
** 这是条件表达式代码生成的核心逻辑
**
** 优化处理：
** - 如果表达式是NOT指令，可以直接使用其操作数并反转条件
** - 否则生成TESTSET指令进行条件测试
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
** @param cond - int：期望的跳转条件（0或1）
**
** @return int：生成的跳转指令位置
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 指令选择：
** - TEST：只测试不赋值
** - TESTSET：测试并可能赋值
*/
static int jumponcond(FuncState *fs, expdesc *e, int cond)
{
    if (e->k == VRELOCABLE)
    {
        // 检查是否为NOT指令，可以进行优化
        Instruction ie = getcode(fs, e);
        if (GET_OPCODE(ie) == OP_NOT)
        {
            // 移除NOT指令，直接使用其操作数并反转条件
            fs->pc--;
            return condjump(fs, OP_TEST, GETARG_B(ie), 0, !cond);
        }
        // 否则继续使用标准处理
    }

    // 标准处理：确保表达式在寄存器中
    discharge2anyreg(fs, e);
    freeexp(fs, e);

    // 生成TESTSET指令进行条件测试
    return condjump(fs, OP_TESTSET, NO_REG, e->u.s.info, cond);
}


/*
** [核心] 为真时跳转
**
** 功能说明：
** 生成条件为真时的跳转代码，处理各种表达式类型的真值跳转
** 这是实现and、or逻辑运算符的基础
**
** 处理策略：
** - 常量真值：无需跳转
** - 跳转表达式：反转跳转条件
** - 其他表达式：生成条件跳转
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 跳转列表管理：
** - 将假跳转合并到f列表
** - 将真跳转回填到当前位置
*/
void luaK_goiftrue(FuncState *fs, expdesc *e)
{
    int pc;  // 最后跳转的pc

    // 处理变量表达式
    luaK_dischargevars(fs, e);

    switch (e->k)
    {
        case VK:
        case VKNUM:
        case VTRUE:
        {
            // 总是真值，无需生成跳转
            pc = NO_JUMP;
            break;
        }

        case VJMP:
        {
            // 已经是跳转表达式，反转跳转条件
            invertjump(fs, e);
            pc = e->u.s.info;
            break;
        }

        default:
        {
            // 其他情况：生成条件跳转（为假时跳转）
            pc = jumponcond(fs, e, 0);
            break;
        }
    }

    // 将最后的跳转插入假跳转列表
    luaK_concat(fs, &e->f, pc);

    // 将真跳转列表回填到当前位置
    luaK_patchtohere(fs, e->t);
    e->t = NO_JUMP;
}


/*
** [核心] 为假时跳转
**
** 功能说明：
** 生成条件为假时的跳转代码，处理各种表达式类型的假值跳转
** 与luaK_goiftrue相对应，用于处理逻辑运算的假分支
**
** 处理策略：
** - 常量假值：无需跳转
** - 跳转表达式：直接使用现有跳转
** - 其他表达式：生成条件跳转
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 跳转列表管理：
** - 将真跳转合并到t列表
** - 将假跳转回填到当前位置
*/
static void luaK_goiffalse(FuncState *fs, expdesc *e)
{
    int pc;  // 最后跳转的pc

    // 处理变量表达式
    luaK_dischargevars(fs, e);

    switch (e->k)
    {
        case VNIL:
        case VFALSE:
        {
            // 总是假值，无需生成跳转
            pc = NO_JUMP;
            break;
        }

        case VJMP:
        {
            // 已经是跳转表达式，直接使用
            pc = e->u.s.info;
            break;
        }

        default:
        {
            // 其他情况：生成条件跳转（为真时跳转）
            pc = jumponcond(fs, e, 1);
            break;
        }
    }

    // 将最后的跳转插入真跳转列表
    luaK_concat(fs, &e->t, pc);

    // 将假跳转列表回填到当前位置
    luaK_patchtohere(fs, e->f);
    e->f = NO_JUMP;
}


/*
** [核心] 生成逻辑非操作的代码
**
** 功能说明：
** 生成逻辑非运算符(not)的代码，处理各种类型表达式的逻辑取反
** 包括常量折叠优化和跳转列表的交换
**
** 常量折叠：
** - nil, false → true
** - 其他常量 → false
**
** 跳转处理：
** - 交换真假跳转列表
** - 移除跳转列表中的值依赖
**
** @param fs - FuncState*：函数编译状态指针
** @param e - expdesc*：表达式描述符指针
**
** 算法复杂度：O(1) 时间，O(n) 空间（n为跳转列表长度）
**
** 优化效果：
** 常量表达式在编译时求值，避免运行时计算
*/
static void codenot(FuncState *fs, expdesc *e)
{
    // 处理变量表达式
    luaK_dischargevars(fs, e);

    switch (e->k)
    {
        case VNIL:
        case VFALSE:
        {
            // nil和false的逻辑非为true
            e->k = VTRUE;
            break;
        }

        case VK:
        case VKNUM:
        case VTRUE:
        {
            // 其他常量值的逻辑非为false
            e->k = VFALSE;
            break;
        }

        case VJMP:
        {
            // 跳转表达式：反转跳转条件
            invertjump(fs, e);
            break;
        }

        case VRELOCABLE:
        case VNONRELOC:
        {
            // 寄存器值：确保在寄存器中，生成NOT指令
            discharge2anyreg(fs, e);
            freeexp(fs, e);
            e->u.s.info = luaK_codeABC(fs, OP_NOT, 0, e->u.s.info, 0);
            e->k = VRELOCABLE;
            break;
        }

        default:
        {
            // 不可能的情况
            lua_assert(0);
            break;
        }
    }

    // 交换真假跳转列表（逻辑非的效果）
    {
        int temp = e->f;
        e->f = e->t;
        e->t = temp;
    }

    // 移除跳转列表中的值依赖
    removevalues(fs, e->f);
    removevalues(fs, e->t);
}


/*
** [入门] 生成索引操作的代码
**
** 功能说明：
** 为表索引操作准备代码生成，将键转换为RK格式并设置表达式类型
** 这是实现table[key]访问的前置处理
**
** @param fs - FuncState*：函数编译状态指针
** @param t - expdesc*：表表达式
** @param k - expdesc*：键表达式
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 副作用：
** - 修改表表达式的类型为VINDEXED
** - 将键转换为RK格式并保存
*/
void luaK_indexed(FuncState *fs, expdesc *t, expdesc *k)
{
    // 将键转换为RK格式（寄存器或常量）
    t->u.s.aux = luaK_exp2RK(fs, k);

    // 设置表达式类型为索引类型
    t->k = VINDEXED;
}


/*
** [优化] 常量折叠优化
**
** 功能说明：
** 在编译时计算常量表达式的值，避免运行时计算
** 这是编译器优化的重要组成部分
**
** 支持的运算：
** 算术运算：+, -, *, /, %, ^, -（一元）
** 特殊处理：除零检查、NaN检查
**
** @param op - OpCode：运算操作码
** @param e1 - expdesc*：第一个操作数表达式
** @param e2 - expdesc*：第二个操作数表达式
**
** @return int：成功折叠返回1，失败返回0
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 安全检查：
** - 除零操作不进行折叠
** - NaN结果不进行折叠
*/
static int constfolding(OpCode op, expdesc *e1, expdesc *e2)
{
    lua_Number v1, v2, r;

    // 检查两个操作数是否都是数字常量
    if (!isnumeral(e1) || !isnumeral(e2))
    {
        return 0;
    }

    // 提取数字值
    v1 = e1->u.nval;
    v2 = e2->u.nval;

    // 根据操作类型计算结果
    switch (op)
    {
        case OP_ADD: r = luai_numadd(v1, v2); break;
        case OP_SUB: r = luai_numsub(v1, v2); break;
        case OP_MUL: r = luai_nummul(v1, v2); break;
        case OP_DIV:
            // 避免除零
            if (v2 == 0) return 0;
            r = luai_numdiv(v1, v2);
            break;
        case OP_MOD:
            // 避免除零
            if (v2 == 0) return 0;
            r = luai_nummod(v1, v2);
            break;
        case OP_POW: r = luai_numpow(v1, v2); break;
        case OP_UNM: r = luai_numunm(v1); break;
        case OP_LEN:
            // 长度运算不做常量折叠
            return 0;
        default:
            lua_assert(0);
            r = 0;
            break;
    }

    // 检查结果是否为NaN
    if (luai_numisnan(r))
    {
        return 0;  // 不折叠NaN结果
    }

    // 将计算结果保存到第一个操作数
    e1->u.nval = r;
    return 1;
}


/*
** [核心] 生成算术运算代码
**
** 功能说明：
** 为算术运算生成字节码，首先尝试常量折叠优化
** 如果无法折叠则生成运行时计算指令
**
** 优化策略：
** 1. 优先进行常量折叠
** 2. 将操作数转换为RK格式
** 3. 优化寄存器释放顺序
**
** @param fs - FuncState*：函数编译状态指针
** @param op - OpCode：算术操作码
** @param e1 - expdesc*：第一个操作数表达式
** @param e2 - expdesc*：第二个操作数表达式
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 寄存器优化：
** 先释放编号较大的操作数，提高寄存器复用效率
*/
static void codearith(FuncState *fs, OpCode op, expdesc *e1, expdesc *e2)
{
    // 尝试常量折叠优化
    if (constfolding(op, e1, e2))
    {
        return;
    }
    else
    {
        // 将操作数转换为RK格式
        int o2 = (op != OP_UNM && op != OP_LEN) ? luaK_exp2RK(fs, e2) : 0;
        int o1 = luaK_exp2RK(fs, e1);

        // 优化寄存器释放顺序：先释放编号大的
        if (o1 > o2)
        {
            freeexp(fs, e1);
            freeexp(fs, e2);
        }
        else
        {
            freeexp(fs, e2);
            freeexp(fs, e1);
        }

        // 生成算术运算指令
        e1->u.s.info = luaK_codeABC(fs, op, 0, o1, o2);
        e1->k = VRELOCABLE;
    }
}


/*
** [核心] 生成比较运算代码
**
** 功能说明：
** 为比较运算生成条件跳转代码，处理操作数顺序优化
** 比较运算的结果是条件跳转而不是具体值
**
** 操作数优化：
** 对于不等式运算，可以通过交换操作数来减少指令类型
** 例如：a > b 可以转换为 b < a
**
** @param fs - FuncState*：函数编译状态指针
** @param op - OpCode：比较操作码
** @param cond - int：跳转条件
** @param e1 - expdesc*：第一个操作数表达式
** @param e2 - expdesc*：第二个操作数表达式
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 指令优化：
** 通过操作数交换将所有不等式归一化为<和<=
*/
static void codecomp(FuncState *fs, OpCode op, int cond, expdesc *e1, expdesc *e2)
{
    // 将操作数转换为RK格式
    int o1 = luaK_exp2RK(fs, e1);
    int o2 = luaK_exp2RK(fs, e2);

    // 释放操作数表达式
    freeexp(fs, e2);
    freeexp(fs, e1);

    // 优化不等式：通过交换操作数减少指令类型
    if (cond == 0 && op != OP_EQ)
    {
        // 交换操作数：a > b → b < a, a >= b → b <= a
        int temp;
        temp = o1; o1 = o2; o2 = temp;  // 交换 o1 <==> o2
        cond = 1;  // 反转条件
    }

    // 生成条件跳转指令
    e1->u.s.info = condjump(fs, op, cond, o1, o2);
    e1->k = VJMP;
}


/*
** [核心] 处理前缀操作符
**
** 功能说明：
** 处理一元运算符（前缀操作符）的代码生成
** 包括算术取负、逻辑非、长度运算符
**
** 支持的操作符：
** - OPR_MINUS：算术取负（-）
** - OPR_NOT：逻辑非（not）
** - OPR_LEN：长度运算符（#）
**
** @param fs - FuncState*：函数编译状态指针
** @param op - UnOpr：一元操作符类型
** @param e - expdesc*：操作数表达式
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 优化处理：
** 数字取负可以进行常量折叠优化
*/
void luaK_prefix(FuncState *fs, UnOpr op, expdesc *e)
{
    expdesc e2;

    // 初始化辅助表达式为0（用于一元运算）
    e2.t = e2.f = NO_JUMP;
    e2.k = VKNUM;
    e2.u.nval = 0;

    switch (op)
    {
        case OPR_MINUS:
        {
            // 算术取负
            if (!isnumeral(e))
            {
                // 非数字常量需要确保在寄存器中
                luaK_exp2anyreg(fs, e);
            }
            // 生成一元减法指令（可能进行常量折叠）
            codearith(fs, OP_UNM, e, &e2);
            break;
        }

        case OPR_NOT:
        {
            // 逻辑非
            codenot(fs, e);
            break;
        }

        case OPR_LEN:
        {
            // 长度运算符
            luaK_exp2anyreg(fs, e);  // 不能操作常量
            codearith(fs, OP_LEN, e, &e2);
            break;
        }

        default:
        {
            lua_assert(0);
        }
    }
}


/*
** [核心] 处理中缀操作符的第一个操作数
**
** 功能说明：
** 为二元运算符的第一个操作数进行预处理
** 根据运算符类型决定不同的处理策略
**
** 特殊处理：
** - AND：如果第一个操作数为真才计算第二个
** - OR：如果第一个操作数为假才计算第二个  
** - CONCAT：需要确保操作数在栈上（连续寄存器）
** - 算术运算：尽可能保持常量形式用于折叠优化
**
** @param fs - FuncState*：函数编译状态指针
** @param op - BinOpr：二元操作符类型
** @param v - expdesc*：第一个操作数表达式
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 优化策略：
** 逻辑运算符使用短路求值，算术运算保持常量形式
*/
void luaK_infix(FuncState *fs, BinOpr op, expdesc *v)
{
    switch (op)
    {
        case OPR_AND:
        {
            // 逻辑与：第一个操作数为真时才继续
            luaK_goiftrue(fs, v);
            break;
        }

        case OPR_OR:
        {
            // 逻辑或：第一个操作数为假时才继续
            luaK_goiffalse(fs, v);
            break;
        }

        case OPR_CONCAT:
        {
            // 字符串连接：操作数必须在栈上（连续寄存器）
            luaK_exp2nextreg(fs, v);
            break;
        }

        case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
        case OPR_MOD: case OPR_POW:
        {
            // 算术运算：保持数字常量形式以便折叠优化
            if (!isnumeral(v))
            {
                luaK_exp2RK(fs, v);
            }
            break;
        }

        default:
        {
            // 比较运算等：转换为RK格式
            luaK_exp2RK(fs, v);
            break;
        }
    }
}


/*
** [核心] 处理后缀操作符（第二个操作数）
**
** 功能说明：
** 完成二元运算符的代码生成，结合第一个和第二个操作数
** 这是二元运算符处理的最终阶段
**
** 复杂处理：
** - 逻辑运算：合并跳转列表，实现短路求值
** - 字符串连接：优化连续连接操作
** - 算术运算：调用相应的代码生成函数
** - 比较运算：生成条件跳转代码
**
** @param fs - FuncState*：函数编译状态指针
** @param op - BinOpr：二元操作符类型
** @param e1 - expdesc*：第一个操作数表达式
** @param e2 - expdesc*：第二个操作数表达式
**
** 算法复杂度：O(1) 时间，O(n) 空间（n为跳转列表长度）
**
** 优化亮点：
** 字符串连接的链式优化，逻辑运算的短路优化
*/
void luaK_posfix(FuncState *fs, BinOpr op, expdesc *e1, expdesc *e2)
{
    switch (op)
    {
        case OPR_AND:
        {
            // 逻辑与：合并跳转列表
            lua_assert(e1->t == NO_JUMP);  // 真列表必须关闭
            luaK_dischargevars(fs, e2);
            luaK_concat(fs, &e2->f, e1->f);
            *e1 = *e2;
            break;
        }

        case OPR_OR:
        {
            // 逻辑或：合并跳转列表
            lua_assert(e1->f == NO_JUMP);  // 假列表必须关闭
            luaK_dischargevars(fs, e2);
            luaK_concat(fs, &e2->t, e1->t);
            *e1 = *e2;
            break;
        }

        case OPR_CONCAT:
        {
            // 字符串连接：优化连续连接
            luaK_exp2val(fs, e2);
            if (e2->k == VRELOCABLE && GET_OPCODE(getcode(fs, e2)) == OP_CONCAT)
            {
                // 连续的CONCAT指令优化：扩展现有的连接范围
                lua_assert(e1->u.s.info == GETARG_B(getcode(fs, e2)) - 1);
                freeexp(fs, e1);
                SETARG_B(getcode(fs, e2), e1->u.s.info);
                e1->k = VRELOCABLE;
                e1->u.s.info = e2->u.s.info;
            }
            else
            {
                // 标准字符串连接处理
                luaK_exp2nextreg(fs, e2);  // 确保操作数在栈上
                codearith(fs, OP_CONCAT, e1, e2);
            }
            break;
        }

        // 算术运算操作符
        case OPR_ADD: codearith(fs, OP_ADD, e1, e2); break;
        case OPR_SUB: codearith(fs, OP_SUB, e1, e2); break;
        case OPR_MUL: codearith(fs, OP_MUL, e1, e2); break;
        case OPR_DIV: codearith(fs, OP_DIV, e1, e2); break;
        case OPR_MOD: codearith(fs, OP_MOD, e1, e2); break;
        case OPR_POW: codearith(fs, OP_POW, e1, e2); break;

        // 比较运算操作符
        case OPR_EQ:  codecomp(fs, OP_EQ, 1, e1, e2); break;
        case OPR_NE:  codecomp(fs, OP_EQ, 0, e1, e2); break;
        case OPR_LT:  codecomp(fs, OP_LT, 1, e1, e2); break;
        case OPR_LE:  codecomp(fs, OP_LE, 1, e1, e2); break;
        case OPR_GT:  codecomp(fs, OP_LT, 0, e1, e2); break;
        case OPR_GE:  codecomp(fs, OP_LE, 0, e1, e2); break;

        default:
        {
            lua_assert(0);
        }
    }
}


/*
** [入门] 修正行号信息
**
** 功能说明：
** 修正最后生成的指令的行号信息，用于调试和错误报告
** 确保字节码与源代码行号的正确对应关系
**
** @param fs - FuncState*：函数编译状态指针
** @param line - int：正确的行号
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 用途：
** 在某些情况下指令的行号可能不准确，需要手动修正
*/
void luaK_fixline(FuncState *fs, int line)
{
    // 修正最后生成的指令的行号信息
    fs->f->lineinfo[fs->pc - 1] = line;
}


/*
** [核心] 生成指令的核心函数
**
** 功能说明：
** 生成字节码指令并添加到函数的代码数组中
** 这是所有指令生成的最终通道
**
** 核心流程：
** 1. 释放待处理的跳转列表
** 2. 扩展代码数组以容纳新指令
** 3. 存储指令和对应的行号信息
** 4. 递增程序计数器
**
** @param fs - FuncState*：函数编译状态指针
** @param i - Instruction：要生成的指令
** @param line - int：指令对应的源代码行号
**
** @return int：新指令在代码数组中的位置
**
** 算法复杂度：O(1) 摊销时间，O(n) 最坏情况（数组扩展）
**
** 内存管理：
** 自动扩展代码数组和行号信息数组
*/
static int luaK_code(FuncState *fs, Instruction i, int line)
{
    Proto *f = fs->f;

    // 释放待处理的跳转列表，因为PC即将改变
    dischargejpc(fs);

    // 扩展代码数组以容纳新指令
    luaM_growvector(fs->L, f->code, fs->pc, f->sizecode, Instruction,
                    MAX_INT, "code size overflow");
    f->code[fs->pc] = i;

    // 扩展行号信息数组并保存对应的行信息
    luaM_growvector(fs->L, f->lineinfo, fs->pc, f->sizelineinfo, int,
                    MAX_INT, "code size overflow");
    f->lineinfo[fs->pc] = line;

    // 返回指令位置并递增程序计数器
    return fs->pc++;
}


/*
** [入门] 生成ABC格式指令
**
** 功能说明：
** 生成ABC格式的字节码指令，这是最常用的指令格式
** 包含一个操作码和三个参数字段
**
** 指令格式：
** ABC: | OpCode | A (8位) | B (9位) | C (9位) |
**
** @param fs - FuncState*：函数编译状态指针
** @param o - OpCode：操作码
** @param a - int：A参数（通常是目标寄存器）
** @param b - int：B参数（源寄存器或常量索引）
** @param c - int：C参数（源寄存器或常量索引）
**
** @return int：生成的指令在代码数组中的位置
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 安全检查：
** 验证指令格式和参数模式的正确性
*/
int luaK_codeABC(FuncState *fs, OpCode o, int a, int b, int c)
{
    // 验证指令格式
    lua_assert(getOpMode(o) == iABC);

    // 验证B参数模式
    lua_assert(getBMode(o) != OpArgN || b == 0);

    // 验证C参数模式
    lua_assert(getCMode(o) != OpArgN || c == 0);

    // 生成ABC格式指令
    return luaK_code(fs, CREATE_ABC(o, a, b, c), fs->ls->lastline);
}


/*
** [入门] 生成ABx格式指令
**
** 功能说明：
** 生成ABx或AsBx格式的字节码指令，用于需要大参数的操作
** ABx用于无符号大参数，AsBx用于有符号大参数
**
** 指令格式：
** ABx:  | OpCode | A (8位) | Bx (18位，无符号) |
** AsBx: | OpCode | A (8位) | sBx (18位，有符号) |
**
** @param fs - FuncState*：函数编译状态指针
** @param o - OpCode：操作码
** @param a - int：A参数
** @param bc - unsigned int：Bx或sBx参数
**
** @return int：生成的指令在代码数组中的位置
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用场景：
** 常量加载、全局变量访问、大跳转偏移等
*/
int luaK_codeABx(FuncState *fs, OpCode o, int a, unsigned int bc)
{
    // 验证指令格式（ABx或AsBx）
    lua_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);

    // 验证C参数不使用（ABx格式没有C参数）
    lua_assert(getCMode(o) == OpArgN);

    // 生成ABx格式指令
    return luaK_code(fs, CREATE_ABx(o, a, bc), fs->ls->lastline);
}


/*
** [核心] 生成SETLIST指令（用于表初始化）
**
** 功能说明：
** 为表的列表部分初始化生成SETLIST指令
** 处理表构造器中数组元素的批量设置
**
** 工作机制：
** 1. 计算批次编号（每批LFIELDS_PER_FLUSH个元素）
** 2. 根据C参数范围选择指令格式
** 3. 如果C参数超出范围，使用额外的指令存储
**
** @param fs - FuncState*：函数编译状态指针
** @param base - int：基础寄存器（表所在位置）
** @param nelems - int：当前表中的元素总数
** @param tostore - int：本批次要存储的元素数量
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 指令格式：
** SETLIST A B C：将寄存器A+1到A+B的值设置到表A的索引(C-1)*LFIELDS_PER_FLUSH+1开始的位置
**
** 特殊处理：
** 当C参数超出范围时，生成额外的指令来存储大的C值
*/
void luaK_setlist(FuncState *fs, int base, int nelems, int tostore)
{
    // 计算批次编号：第几批LFIELDS_PER_FLUSH个元素
    int c = (nelems - 1) / LFIELDS_PER_FLUSH + 1;

    // 计算B参数：本次存储的元素数量
    int b = (tostore == LUA_MULTRET) ? 0 : tostore;

    // 确保有元素要存储
    lua_assert(tostore != 0);

    if (c <= MAXARG_C)
    {
        // C参数在正常范围内，生成标准SETLIST指令
        luaK_codeABC(fs, OP_SETLIST, base, b, c);
    }
    else
    {
        // C参数超出范围，需要额外指令存储
        luaK_codeABC(fs, OP_SETLIST, base, b, 0);
        luaK_code(fs, cast(Instruction, c), fs->ls->lastline);
    }

    // 释放存储列表值的寄存器，只保留基础寄存器（表）
    fs->freereg = base + 1;
}