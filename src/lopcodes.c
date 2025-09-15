/**
 * @file lopcodes.c
 * @brief Lua虚拟机操作码系统：指令定义和操作码元数据的核心实现
 * 
 * 版权信息：
 * $Id: lopcodes.c,v 1.37.1.1 2007/12/27 13:02:25 roberto Exp $
 * 版权声明见lua.h文件
 * 
 * 程序概述：
 * 本文件实现了Lua虚拟机的操作码（opcodes）系统，定义了虚拟机执行的所有指令类型
 * 及其属性。它是Lua解释器和编译器之间的重要桥梁，为字节码的生成、执行和调试
 * 提供统一的指令集架构。
 * 
 * 系统架构定位：
 * 作为Lua虚拟机的核心组件，操作码系统位于指令层，为上层的编译器提供目标指令集，
 * 为下层的虚拟机执行引擎提供指令解析依据。它与词法分析器、语法分析器、代码生成器
 * 和虚拟机执行器紧密配合，形成完整的代码执行链。
 * 
 * 核心功能：
 * 1. **指令名称定义**: 提供所有操作码的可读字符串名称
 * 2. **指令模式描述**: 定义每个指令的参数格式和行为特征
 * 3. **指令分类管理**: 按功能对指令进行逻辑分组
 * 4. **调试支持**: 为反汇编和调试工具提供指令信息
 * 
 * 支持的指令分类：
 * - **数据移动**: MOVE, LOADK, LOADBOOL, LOADNIL
 * - **变量访问**: GETUPVAL, GETGLOBAL, GETTABLE, SETGLOBAL, SETUPVAL, SETTABLE
 * - **表操作**: NEWTABLE, SELF, SETLIST
 * - **算术运算**: ADD, SUB, MUL, DIV, MOD, POW, UNM
 * - **逻辑运算**: NOT, LEN
 * - **字符串操作**: CONCAT
 * - **控制流**: JMP, EQ, LT, LE, TEST, TESTSET
 * - **函数调用**: CALL, TAILCALL, RETURN, CLOSURE
 * - **循环控制**: FORLOOP, FORPREP, TFORLOOP
 * - **其他**: CLOSE, VARARG
 * 
 * 指令格式类型：
 * - **iABC格式**: 一个操作码 + 三个参数（A, B, C）
 * - **iABx格式**: 一个操作码 + 一个A参数 + 一个扩展Bx参数
 * - **iAsBx格式**: 一个操作码 + 一个A参数 + 一个有符号sBx参数
 * 
 * 参数类型定义：
 * - **OpArgN**: 不使用参数
 * - **OpArgU**: 使用参数，具体含义由指令决定
 * - **OpArgR**: 寄存器索引（栈位置）
 * - **OpArgK**: 常量索引或寄存器索引
 * 
 * 技术特点：
 * - 紧凑编码：使用位域压缩指令信息
 * - 类型安全：每个参数都有明确的类型约束
 * - 易于扩展：模块化的指令定义便于添加新指令
 * - 调试友好：提供可读的指令名称和详细的元信息
 * 
 * 使用场景：
 * - 编译器：生成目标字节码时查询指令格式
 * - 虚拟机：执行字节码时解析指令类型和参数
 * - 调试器：反汇编字节码，显示可读的指令信息
 * - 分析工具：静态分析Lua程序的执行流程
 * - 性能优化：基于指令特征进行代码优化
 * 
 * 性能考虑：
 * - 查表效率：使用数组索引实现O(1)时间复杂度
 * - 内存友好：紧凑的数据结构减少内存占用
 * - 缓存友好：相关数据集中存储，提高访问局部性
 * - 编译时优化：静态数据结构支持编译器优化
 * 
 * 安全性特性：
 * - 类型检查：参数类型在编译时和运行时验证
 * - 边界检查：操作码索引有明确的范围限制
 * - 一致性保证：名称和模式数组保持同步
 * 
 * @author Roberto Ierusalimschy
 * @version 1.37.1.1
 * @date 2007-12-27
 * 
 * @see lopcodes.h 操作码常量和宏定义
 * @see lvm.h 虚拟机执行引擎接口
 * @see lcode.h 代码生成器接口
 * @see ldebug.h 调试信息接口
 * 
 * @note 本模块是Lua虚拟机指令集架构的核心定义
 */

#define lopcodes_c
#define LUA_CORE

#include "lopcodes.h"


/**
 * @brief Lua操作码名称表：定义所有虚拟机指令的字符串名称
 * 
 * 详细说明：
 * 这个全局数组包含了Lua虚拟机所有38个操作码的可读字符串名称，用于调试输出、
 * 反汇编工具和错误信息生成。数组按照操作码的数值顺序排列，确保了操作码常量
 * 和名称字符串的一一对应关系。
 * 
 * 指令分类说明：
 * 
 * **数据加载指令（0-3）**：
 * - MOVE: 寄存器间数据移动，R(A) := R(B)
 * - LOADK: 加载常量到寄存器，R(A) := Kst(Bx)
 * - LOADBOOL: 加载布尔值，R(A) := (Bool)B; if (C) pc++
 * - LOADNIL: 加载nil值，R(A) := ... := R(B) := nil
 * 
 * **变量访问指令（4-9）**：
 * - GETUPVAL: 获取上值，R(A) := UpValue[B]
 * - GETGLOBAL: 获取全局变量，R(A) := Gbl[Kst(Bx)]
 * - GETTABLE: 表索引读取，R(A) := R(B)[RK(C)]
 * - SETGLOBAL: 设置全局变量，Gbl[Kst(Bx)] := R(A)
 * - SETUPVAL: 设置上值，UpValue[B] := R(A)
 * - SETTABLE: 表索引写入，R(A)[RK(B)] := RK(C)
 * 
 * **表和对象操作（10-11）**：
 * - NEWTABLE: 创建新表，R(A) := {} (size = B,C)
 * - SELF: 方法调用准备，R(A+1) := R(B); R(A) := R(B)[RK(C)]
 * 
 * **算术运算指令（12-18）**：
 * - ADD: 加法运算，R(A) := RK(B) + RK(C)
 * - SUB: 减法运算，R(A) := RK(B) - RK(C)
 * - MUL: 乘法运算，R(A) := RK(B) * RK(C)
 * - DIV: 除法运算，R(A) := RK(B) / RK(C)
 * - MOD: 取模运算，R(A) := RK(B) % RK(C)
 * - POW: 幂运算，R(A) := RK(B) ^ RK(C)
 * - UNM: 一元负号，R(A) := -R(B)
 * 
 * **逻辑和字符串操作（19-21）**：
 * - NOT: 逻辑非，R(A) := not R(B)
 * - LEN: 取长度，R(A) := length of R(B)
 * - CONCAT: 字符串连接，R(A) := R(B).. ... ..R(C)
 * 
 * **控制流指令（22-27）**：
 * - JMP: 无条件跳转，pc += sBx
 * - EQ: 相等比较，if ((RK(B) == RK(C)) ~= A) then pc++
 * - LT: 小于比较，if ((RK(B) < RK(C)) ~= A) then pc++
 * - LE: 小于等于比较，if ((RK(B) <= RK(C)) ~= A) then pc++
 * - TEST: 条件测试，if not (R(A) <=> C) then pc++
 * - TESTSET: 条件测试并设置，if (R(B) <=> C) then R(A) := R(B) else pc++
 * 
 * **函数调用指令（28-30）**：
 * - CALL: 函数调用，R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1))
 * - TAILCALL: 尾调用，return R(A)(R(A+1), ... ,R(A+B-1))
 * - RETURN: 函数返回，return R(A), ... ,R(A+B-2)
 * 
 * **循环控制指令（31-33）**：
 * - FORLOOP: 数值for循环体，R(A)+=R(A+2); if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }
 * - FORPREP: 数值for循环准备，R(A)-=R(A+2); pc+=sBx
 * - TFORLOOP: 通用for循环，R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2))
 * 
 * **其他指令（34-37）**：
 * - SETLIST: 列表设置，R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B
 * - CLOSE: 关闭上值，close all variables in the stack up to (>=) R(A)
 * - CLOSURE: 闭包创建，R(A) := closure(KPROTO[Bx], R(A), ... ,R(A+n))
 * - VARARG: 可变参数，R(A), R(A+1), ..., R(A+B-1) = vararg
 * 
 * 使用场景：
 * - 调试器：显示可读的指令名称
 * - 反汇编器：生成可读的字节码清单
 * - 错误报告：在运行时错误中显示当前指令
 * - 性能分析：统计各类指令的执行频率
 * - 教学工具：帮助理解Lua虚拟机的工作原理
 * 
 * 注意事项：
 * - 数组长度为NUM_OPCODES+1，最后一个元素为NULL作为结束标记
 * - 数组索引必须与lopcodes.h中定义的OP_*常量保持一致
 * - 字符串都是编译时常量，保证了运行时的高效访问
 * - 指令名称使用全大写字母，与汇编语言传统保持一致
 * 
 * 性能特性：
 * - 直接数组索引，时间复杂度O(1)
 * - 静态存储，不占用动态内存
 * - 编译器优化友好，支持常量折叠
 * 
 * @see OpCode 操作码枚举定义（lopcodes.h）
 * @see NUM_OPCODES 操作码总数常量
 * @see luaP_opmodes 对应的操作码模式数组
 * 
 * @note 这个数组是Lua调试和开发工具的基础数据结构
 */
/* ORDER OP */
const char *const luaP_opnames[NUM_OPCODES+1] = {
    "MOVE",
    "LOADK",
    "LOADBOOL",
    "LOADNIL",
    "GETUPVAL",
    "GETGLOBAL",
    "GETTABLE",
    "SETGLOBAL",
    "SETUPVAL",
    "SETTABLE",
    "NEWTABLE",
    "SELF",
    "ADD",
    "SUB",
    "MUL",
    "DIV",
    "MOD",
    "POW",
    "UNM",
    "NOT",
    "LEN",
    "CONCAT",
    "JMP",
    "EQ",
    "LT",
    "LE",
    "TEST",
    "TESTSET",
    "CALL",
    "TAILCALL",
    "RETURN",
    "FORLOOP",
    "FORPREP",
    "TFORLOOP",
    "SETLIST",
    "CLOSE",
    "CLOSURE",
    "VARARG",
    NULL
};


/**
 * @brief 操作码模式编码宏：将指令属性压缩为单字节表示
 * 
 * 详细说明：
 * 这个宏将操作码的各种属性（测试标志、A参数使用、B参数类型、C参数类型、指令模式）
 * 编码为一个8位整数，实现紧凑的指令元数据存储。
 * 
 * 参数说明：
 * @param t 测试标志位（1位）：指令是否用于条件测试
 * @param a A参数标志位（1位）：指令是否修改A寄存器
 * @param b B参数类型（2位）：OpArgN/OpArgU/OpArgR/OpArgK之一
 * @param c C参数类型（2位）：OpArgN/OpArgU/OpArgR/OpArgK之一  
 * @param m 指令模式（2位）：iABC/iABx/iAsBx之一
 * 
 * 位域布局（从高位到低位）：
 * - 位7：测试标志（T）
 * - 位6：A参数标志（A） 
 * - 位5-4：B参数类型（BB）
 * - 位3-2：C参数类型（CC）
 * - 位1-0：指令模式（MM）
 * 
 * 编码公式：TABBCCMM
 * 
 * @see OpArgMode B和C参数的类型枚举
 * @see OpMode 指令格式模式枚举
 */
#define opmode(t,a,b,c,m) (((t)<<7) | ((a)<<6) | ((b)<<4) | ((c)<<2) | (m))

/**
 * @brief Lua操作码模式表：定义所有指令的格式和参数类型
 * 
 * 详细说明：
 * 这个数组为每个操作码定义了详细的格式信息，包括指令是否用于测试、
 * 是否修改A寄存器、B和C参数的类型以及整体的指令格式。虚拟机和编译器
 * 使用这些信息来正确解析和生成字节码指令。
 * 
 * 字段含义说明：
 * 
 * **测试标志（T）**：
 * - 0：普通指令，执行后正常推进PC
 * - 1：测试指令，可能跳过下一条指令
 * 
 * **A参数标志（A）**：
 * - 0：指令不修改A寄存器的值
 * - 1：指令会将结果存储到A寄存器
 * 
 * **参数类型（B/C）**：
 * - OpArgN：参数未使用
 * - OpArgU：参数使用，具体含义由指令定义
 * - OpArgR：寄存器索引（直接栈位置）
 * - OpArgK：常量表索引或寄存器索引（RK格式）
 * 
 * **指令模式（M）**：
 * - iABC：标准三参数格式（A:8位, B:9位, C:9位）
 * - iABx：A参数+扩展Bx参数（A:8位, Bx:18位）
 * - iAsBx：A参数+有符号sBx参数（A:8位, sBx:18位）
 * 
 * 典型指令模式分析：
 * 
 * **数据移动类**：
 * - MOVE: (0,1,R,N,ABC) - 不测试，修改A，B为寄存器，C未用，ABC格式
 * - LOADK: (0,1,K,N,ABx) - 不测试，修改A，Bx为常量索引，ABx格式
 * 
 * **算术运算类**：
 * - ADD: (0,1,K,K,ABC) - 不测试，修改A，B和C都可以是常量或寄存器
 * 
 * **比较测试类**：
 * - EQ: (1,0,K,K,ABC) - 测试指令，不修改A，B和C可以是常量或寄存器
 * 
 * **控制流类**：
 * - JMP: (0,0,R,N,AsBx) - 不测试，不修改A，使用有符号偏移
 * 
 * **函数调用类**：
 * - CALL: (0,1,U,U,ABC) - 不测试，修改A，B和C的含义由指令特定
 * 
 * 使用场景：
 * - 编译器：生成指令时查询参数格式
 * - 虚拟机：执行时解析指令和参数
 * - 优化器：基于指令属性进行优化
 * - 调试器：正确解析和显示指令信息
 * 
 * 性能优化：
 * - 位操作提取：使用位掩码快速提取各字段
 * - 查表访问：O(1)时间复杂度获取指令信息
 * - 紧凑存储：每个指令仅占用1字节元数据
 * 
 * @see OpCode 操作码枚举（对应数组索引）
 * @see opmode() 模式编码宏
 * @see GETARG_* 参数提取宏（lopcodes.h）
 * 
 * @note 数组顺序必须与OpCode枚举严格对应
 * @warning 修改此数组会影响所有相关的编译器和虚拟机代码
 */
const lu_byte luaP_opmodes[NUM_OPCODES] = {
/*       T  A    B       C     mode              opcode    */
    opmode(0, 1, OpArgR, OpArgN, iABC),        /* OP_MOVE */
    opmode(0, 1, OpArgK, OpArgN, iABx),        /* OP_LOADK */
    opmode(0, 1, OpArgU, OpArgU, iABC),        /* OP_LOADBOOL */
    opmode(0, 1, OpArgR, OpArgN, iABC),        /* OP_LOADNIL */
    opmode(0, 1, OpArgU, OpArgN, iABC),        /* OP_GETUPVAL */
    opmode(0, 1, OpArgK, OpArgN, iABx),        /* OP_GETGLOBAL */
    opmode(0, 1, OpArgR, OpArgK, iABC),        /* OP_GETTABLE */
    opmode(0, 0, OpArgK, OpArgN, iABx),        /* OP_SETGLOBAL */
    opmode(0, 0, OpArgU, OpArgN, iABC),        /* OP_SETUPVAL */
    opmode(0, 0, OpArgK, OpArgK, iABC),        /* OP_SETTABLE */
    opmode(0, 1, OpArgU, OpArgU, iABC),        /* OP_NEWTABLE */
    opmode(0, 1, OpArgR, OpArgK, iABC),        /* OP_SELF */
    opmode(0, 1, OpArgK, OpArgK, iABC),        /* OP_ADD */
    opmode(0, 1, OpArgK, OpArgK, iABC),        /* OP_SUB */
    opmode(0, 1, OpArgK, OpArgK, iABC),        /* OP_MUL */
    opmode(0, 1, OpArgK, OpArgK, iABC),        /* OP_DIV */
    opmode(0, 1, OpArgK, OpArgK, iABC),        /* OP_MOD */
    opmode(0, 1, OpArgK, OpArgK, iABC),        /* OP_POW */
    opmode(0, 1, OpArgR, OpArgN, iABC),        /* OP_UNM */
    opmode(0, 1, OpArgR, OpArgN, iABC),        /* OP_NOT */
    opmode(0, 1, OpArgR, OpArgN, iABC),        /* OP_LEN */
    opmode(0, 1, OpArgR, OpArgR, iABC),        /* OP_CONCAT */
    opmode(0, 0, OpArgR, OpArgN, iAsBx),       /* OP_JMP */
    opmode(1, 0, OpArgK, OpArgK, iABC),        /* OP_EQ */
    opmode(1, 0, OpArgK, OpArgK, iABC),        /* OP_LT */
    opmode(1, 0, OpArgK, OpArgK, iABC),        /* OP_LE */
    opmode(1, 1, OpArgR, OpArgU, iABC),        /* OP_TEST */
    opmode(1, 1, OpArgR, OpArgU, iABC),        /* OP_TESTSET */
    opmode(0, 1, OpArgU, OpArgU, iABC),        /* OP_CALL */
    opmode(0, 1, OpArgU, OpArgU, iABC),        /* OP_TAILCALL */
    opmode(0, 0, OpArgU, OpArgN, iABC),        /* OP_RETURN */
    opmode(0, 1, OpArgR, OpArgN, iAsBx),       /* OP_FORLOOP */
    opmode(0, 1, OpArgR, OpArgN, iAsBx),       /* OP_FORPREP */
    opmode(1, 0, OpArgN, OpArgU, iABC),        /* OP_TFORLOOP */
    opmode(0, 0, OpArgU, OpArgU, iABC),        /* OP_SETLIST */
    opmode(0, 0, OpArgN, OpArgN, iABC),        /* OP_CLOSE */
    opmode(0, 1, OpArgU, OpArgN, iABx),        /* OP_CLOSURE */
    opmode(0, 1, OpArgU, OpArgN, iABC),        /* OP_VARARG */
};

