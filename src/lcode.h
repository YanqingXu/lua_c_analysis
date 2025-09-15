/**
 * @file lcode.h
 * @brief Lua代码生成器头文件：定义Lua编译器的代码生成接口和数据结构
 * 
 * 详细说明：
 * 本文件是Lua编译器代码生成器的核心头文件，定义了将Lua源代码编译为
 * 字节码的关键接口和数据结构。代码生成器是Lua编译器的最后阶段，负责
 * 将语法分析器产生的抽象语法树转换为可执行的Lua字节码指令序列。
 * 
 * 系统架构定位：
 * 在Lua编译器架构中，本文件位于编译器后端，连接语法分析和虚拟机执行。
 * 编译流程：源代码 → 词法分析(llex) → 语法分析(lparser) → 代码生成(lcode) → 字节码
 * 
 * 主要职责：
 * - 定义字节码指令生成接口
 * - 管理编译时的寄存器分配
 * - 处理表达式求值和代码优化
 * - 实现跳转指令的补丁机制
 * - 支持常量表和局部变量管理
 * 
 * 技术特点：
 * - 基于寄存器的虚拟机指令集设计
 * - 支持三地址码格式的字节码指令
 * - 实现寄存器分配和生命周期管理
 * - 提供表达式优化和常量折叠
 * - 支持控制流分析和跳转优化
 * 
 * Lua字节码架构：
 * Lua使用基于寄存器的虚拟机架构，相比基于栈的架构具有以下优势：
 * - 减少了值的移动操作，提高执行效率
 * - 指令数量更少，减少指令分发开销
 * - 更接近真实硬件，便于后续优化
 * 
 * 指令格式：
 * - iABC: 三操作数指令 [6位操作码][8位A][9位B][9位C]
 * - iABx: 两操作数指令 [6位操作码][8位A][18位Bx]
 * - iAsBx: 带符号偏移指令 [6位操作码][8位A][18位sBx]
 * 
 * @author Roberto Ierusalimschy (Lua团队)
 * @version 5.1.5
 * @date 2007年12月27日
 * @since Lua 5.0
 * @see lparser.h, lopcodes.h, lvm.h
 */

#ifndef lcode_h
#define lcode_h

#include "llex.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"

/**
 * @brief 跳转补丁列表结束标记
 * 
 * 详细说明：
 * 这个常量用于标记跳转补丁列表的结束。在Lua编译器中，前向跳转指令
 * （如if语句、循环控制等）在生成时目标地址未知，需要使用补丁机制
 * 在后续确定目标地址后回填跳转目标。
 * 
 * 技术原理：
 * 跳转补丁使用链表结构管理，每个需要补丁的跳转指令在其操作数字段
 * 存储指向下一个需要补丁的指令的索引。NO_JUMP作为链表的终止标记。
 * 
 * 选择-1的原因：
 * - 作为绝对地址无效：指令地址从0开始，-1永远不是有效地址
 * - 作为链表链接无效：如果用作链接会形成自环，便于检测错误
 * - 易于调试：在调试器中-1很容易识别为特殊值
 */
#define NO_JUMP (-1)

/**
 * @brief 二元运算符枚举类型
 * 
 * 详细说明：
 * 定义了Lua语言支持的所有二元运算符的内部表示。这些运算符在语法分析
 * 阶段被识别，在代码生成阶段被转换为相应的字节码指令。运算符的顺序
 * 对于优先级处理和指令选择具有重要意义。
 * 
 * 运算符分类：
 * 
 * 算术运算符（优先级从高到低）：
 * - OPR_POW: 乘方运算（^），右结合
 * - OPR_MUL, OPR_DIV, OPR_MOD: 乘法、除法、取模，左结合
 * - OPR_ADD, OPR_SUB: 加法、减法，左结合
 * 
 * 字符串运算符：
 * - OPR_CONCAT: 字符串连接（..），右结合
 * 
 * 关系运算符（无结合性）：
 * - OPR_LT, OPR_LE, OPR_GT, OPR_GE: 比较运算符
 * - OPR_EQ, OPR_NE: 相等性运算符
 * 
 * 逻辑运算符（短路求值）：
 * - OPR_AND: 逻辑与（and），左结合
 * - OPR_OR: 逻辑或（or），左结合
 * 
 * 字节码映射：
 * 运算符到字节码指令的典型映射：
 * OPR_ADD → OP_ADD, OPR_SUB → OP_SUB, OPR_MUL → OP_MUL 等
 * 
 * 元方法支持：
 * 大多数运算符都支持元方法机制，允许用户自定义行为：
 * OPR_ADD → __add, OPR_SUB → __sub, OPR_MUL → __mul 等
 * 
 * @note 枚举顺序重要：搜索"ORDER OPR"了解依赖此顺序的代码
 * @warning 修改此枚举需要同步更新优先级表和指令映射表
 */
typedef enum BinOpr {
    OPR_ADD,        /**< 加法运算符 (+) */
    OPR_SUB,        /**< 减法运算符 (-) */
    OPR_MUL,        /**< 乘法运算符 (*) */
    OPR_DIV,        /**< 除法运算符 (/) */
    OPR_MOD,        /**< 取模运算符 (%) */
    OPR_POW,        /**< 乘方运算符 (^) */
    OPR_CONCAT,     /**< 字符串连接运算符 (..) */
    OPR_NE,         /**< 不等运算符 (~=) */
    OPR_EQ,         /**< 相等运算符 (==) */
    OPR_LT,         /**< 小于运算符 (<) */
    OPR_LE,         /**< 小于等于运算符 (<=) */
    OPR_GT,         /**< 大于运算符 (>) */
    OPR_GE,         /**< 大于等于运算符 (>=) */
    OPR_AND,        /**< 逻辑与运算符 (and) */
    OPR_OR,         /**< 逻辑或运算符 (or) */
    OPR_NOBINOPR    /**< 无二元运算符标记（用于错误检测） */
} BinOpr;

/**
 * @brief 一元运算符枚举类型
 * 
 * 详细说明：
 * 定义了Lua语言支持的所有一元运算符的内部表示。一元运算符的处理
 * 相对简单，但在表达式求值和代码生成中仍然需要特殊处理。
 * 
 * 运算符详细说明：
 * 
 * OPR_MINUS（一元负号）：
 * - 语法：-expression
 * - 语义：数值取负，字符串/表等类型触发__unm元方法
 * - 字节码：OP_UNM
 * 
 * OPR_NOT（逻辑非）：
 * - 语法：not expression  
 * - 语义：逻辑取反，只有nil和false为假，其他都为真
 * - 字节码：OP_NOT
 * 
 * OPR_LEN（长度运算符）：
 * - 语法：#expression
 * - 语义：获取字符串或表的长度
 * - 字节码：OP_LEN
 * - 元方法：__len (Lua 5.2+引入，5.1中仅对表有效)
 * 
 * 元方法处理：
 * - OPR_MINUS → __unm（一元负号元方法）
 * - OPR_LEN → __len（长度元方法，Lua 5.2+）
 * - OPR_NOT 没有对应的元方法，总是执行逻辑取反
 */
typedef enum UnOpr {
    OPR_MINUS,      /**< 一元负号运算符 (-) */
    OPR_NOT,        /**< 逻辑非运算符 (not) */
    OPR_LEN,        /**< 长度运算符 (#) */
    OPR_NOUNOPR     /**< 无一元运算符标记（用于错误检测） */
} UnOpr;

/**
 * @brief 获取指令代码的便捷宏
 * 
 * 从表达式描述符中提取对应的字节码指令。在Lua编译器中，表达式在
 * 编译过程中用expdesc结构体描述，其中包含了表达式的类型、值和在
 * 指令序列中的位置信息。
 * 
 * @param fs FuncState指针，包含当前函数的编译状态
 * @param e expdesc指针，包含表达式的描述信息
 * @note 这是一个访问内部数据结构的底层宏，直接使用需要谨慎
 */
#define getcode(fs,e) ((fs)->f->code[(e)->u.s.info])

/**
 * @brief 生成sBx格式指令的便捷宏
 * 
 * 简化了带符号偏移指令的生成。Lua字节码中的sBx格式指令使用
 * 有符号的18位偏移量，但在指令编码时需要加上偏移量MAXARG_sBx来
 * 将有符号数转换为无符号数存储。
 * 
 * @param fs FuncState指针，当前函数的编译状态
 * @param o OpCode，要生成的操作码
 * @param A int，A操作数（通常是目标寄存器）
 * @param sBx int，有符号偏移量
 * @note 这个宏隐藏了有符号到无符号的转换细节
 */
#define luaK_codeAsBx(fs,o,A,sBx) luaK_codeABx(fs,o,A,(sBx)+MAXARG_sBx)

/**
 * @brief 设置表达式为多返回值的便捷宏
 * 
 * 将表达式设置为返回多个值的模式。在Lua中，某些表达式可以
 * 返回多个值（如函数调用、vararg表达式等）。
 * 
 * @param fs FuncState指针，当前函数的编译状态
 * @param e expdesc指针，要设置的表达式描述符
 * @note 并非所有表达式都可以设置为多返回值
 */
#define luaK_setmultret(fs,e) luaK_setreturns(fs, e, LUA_MULTRET)

/* ============================================================================
 * 字节码指令生成接口
 * ============================================================================ */

/**
 * @brief 生成ABx格式的字节码指令
 * 
 * 生成具有A和Bx操作数的字节码指令。ABx格式是Lua字节码的三种主要
 * 格式之一，其中A是8位操作数，Bx是18位无符号操作数。
 * 
 * 典型用途：
 * - LOADK: 加载常量，A=目标寄存器，Bx=常量表索引
 * - GETGLOBAL: 获取全局变量，A=目标寄存器，Bx=变量名常量索引
 * - SETGLOBAL: 设置全局变量，A=源寄存器，Bx=变量名常量索引
 * - CLOSURE: 创建闭包，A=目标寄存器，Bx=函数原型索引
 * 
 * @param fs 函数状态指针，包含编译上下文
 * @param o 操作码，指定要生成的指令类型
 * @param A A操作数，通常是寄存器索引（0-255）
 * @param Bx Bx操作数，无符号整数（0-262143）
 * @return 指令在代码数组中的索引（PC值）
 */
LUAI_FUNC int luaK_codeABx(FuncState *fs, OpCode o, int A, unsigned int Bx);

/**
 * @brief 生成ABC格式的字节码指令
 * 
 * 生成具有A、B、C三个操作数的字节码指令。ABC格式是最常用的指令
 * 格式，支持三地址码操作，每个操作数都有特定的位宽。
 * 
 * 典型用途：
 * - ADD/SUB/MUL等：算术运算，A=目标，B=左操作数，C=右操作数
 * - GETTABLE：表索引，A=目标，B=表，C=索引
 * - SETTABLE：表赋值，A=表，B=索引，C=值
 * - CALL：函数调用，A=函数，B=参数数量，C=返回值数量
 * 
 * @param fs 函数状态指针
 * @param o 操作码
 * @param A A操作数（0-255）
 * @param B B操作数（0-511）
 * @param C C操作数（0-511）
 * @return 指令索引
 */
LUAI_FUNC int luaK_codeABC(FuncState *fs, OpCode o, int A, int B, int C);

/**
 * @brief 修正指令的行号信息
 * 
 * 为指定的指令设置正确的源代码行号。这个函数用于调试信息的生成，
 * 使得运行时错误能够准确报告源代码位置。
 * 
 * @param fs 函数状态指针
 * @param line 源代码行号
 */
LUAI_FUNC void luaK_fixline(FuncState *fs, int line);

/**
 * @brief 生成nil值初始化指令
 * 
 * 生成指令将指定范围的寄存器初始化为nil值。这通常用于局部变量的
 * 初始化或寄存器的清理。
 * 
 * @param fs 函数状态指针
 * @param from 起始寄存器索引
 * @param n 要初始化的寄存器数量
 */
LUAI_FUNC void luaK_nil(FuncState *fs, int from, int n);

/**
 * @brief 预留指定数量的寄存器
 * 
 * 确保有足够的寄存器可用于后续的代码生成。这个函数会调整栈顶指针，
 * 为即将生成的指令预留寄存器空间。
 * 
 * @param fs 函数状态指针
 * @param n 要预留的寄存器数量
 */
LUAI_FUNC void luaK_reserveregs(FuncState *fs, int n);

/**
 * @brief 检查栈空间是否足够
 * 
 * 验证当前函数栈是否有足够空间容纳指定数量的额外寄存器。如果空间
 * 不足，会自动扩展栈空间或报告错误。
 * 
 * @param fs 函数状态指针
 * @param n 需要的额外寄存器数量
 */
LUAI_FUNC void luaK_checkstack(FuncState *fs, int n);

/**
 * @brief 添加字符串常量到常量表
 * 
 * 将字符串常量添加到当前函数的常量表中，如果字符串已存在则返回
 * 现有索引。常量表用于存储编译时确定的不变值。
 * 
 * @param fs 函数状态指针
 * @param s 字符串对象指针
 * @return 常量在常量表中的索引
 */
LUAI_FUNC int luaK_stringK(FuncState *fs, TString *s);

/**
 * @brief 添加数字常量到常量表
 * 
 * 将数字常量添加到常量表中。Lua使用双精度浮点数作为数字类型。
 * 
 * @param fs 函数状态指针
 * @param r 数字值
 * @return 常量在常量表中的索引
 */
LUAI_FUNC int luaK_numberK(FuncState *fs, lua_Number r);

/**
 * @brief 确保表达式的值被加载到寄存器中
 * 
 * 处理各种类型的表达式，确保它们的值被正确加载到寄存器中。这是
 * 表达式求值的核心函数之一。
 * 
 * @param fs 函数状态指针
 * @param e 表达式描述符指针
 */
LUAI_FUNC void luaK_dischargevars(FuncState *fs, expdesc *e);

/**
 * @brief 将表达式结果存储到任意可用寄存器
 * 
 * 将表达式的值存储到一个可用的寄存器中，如果表达式已经在寄存器中
 * 则直接返回该寄存器索引。
 * 
 * @param fs 函数状态指针
 * @param e 表达式描述符指针
 * @return 包含表达式值的寄存器索引
 */
LUAI_FUNC int luaK_exp2anyreg(FuncState *fs, expdesc *e);

/**
 * @brief 将表达式结果存储到下一个可用寄存器
 * 
 * 将表达式的值存储到栈顶的下一个寄存器中，并更新栈顶指针。
 * 
 * @param fs 函数状态指针
 * @param e 表达式描述符指针
 */
LUAI_FUNC void luaK_exp2nextreg(FuncState *fs, expdesc *e);

/**
 * @brief 将表达式转换为值形式
 * 
 * 确保表达式被求值为一个具体的值，而不是地址或其他形式的引用。
 * 
 * @param fs 函数状态指针
 * @param e 表达式描述符指针
 */
LUAI_FUNC void luaK_exp2val(FuncState *fs, expdesc *e);

/**
 * @brief 将表达式转换为RK格式
 * 
 * 将表达式转换为RK（寄存器或常量）格式，这是Lua字节码指令操作数
 * 的标准格式。RK值可以是寄存器索引或常量表索引。
 * 
 * @param fs 函数状态指针
 * @param e 表达式描述符指针
 * @return RK编码的值
 */
LUAI_FUNC int luaK_exp2RK(FuncState *fs, expdesc *e);

/**
 * @brief 处理self调用语法（obj:method()）
 * 
 * 实现Lua的语法糖obj:method()，等价于obj.method(obj)。这个函数
 * 生成相应的字节码来支持这种调用语法。
 * 
 * @param fs 函数状态指针
 * @param e 对象表达式
 * @param key 方法名表达式
 */
LUAI_FUNC void luaK_self(FuncState *fs, expdesc *e, expdesc *key);

/**
 * @brief 处理表索引操作（table[key]）
 * 
 * 生成表索引访问的字节码指令。支持各种类型的索引（数字、字符串等）。
 * 
 * @param fs 函数状态指针
 * @param t 表表达式
 * @param k 索引表达式
 */
LUAI_FUNC void luaK_indexed(FuncState *fs, expdesc *t, expdesc *k);

/**
 * @brief 生成条件跳转（如果为真则跳转）
 * 
 * 为表达式生成条件跳转指令，当表达式为真时跳转到目标位置。这是
 * 实现if语句、while循环等控制结构的基础。
 * 
 * @param fs 函数状态指针
 * @param e 条件表达式
 */
LUAI_FUNC void luaK_goiftrue(FuncState *fs, expdesc *e);

/**
 * @brief 生成变量赋值代码
 * 
 * 生成将表达式的值赋给变量的字节码。支持各种类型的变量（局部变量、
 * 全局变量、表字段等）。
 * 
 * @param fs 函数状态指针
 * @param var 目标变量表达式
 * @param e 源表达式
 */
LUAI_FUNC void luaK_storevar(FuncState *fs, expdesc *var, expdesc *e);

/**
 * @brief 设置函数调用的返回值数量
 * 
 * 为函数调用表达式设置期望的返回值数量。这影响CALL指令的生成。
 * 
 * @param fs 函数状态指针
 * @param e 函数调用表达式
 * @param nresults 期望的返回值数量
 */
LUAI_FUNC void luaK_setreturns(FuncState *fs, expdesc *e, int nresults);

/**
 * @brief 设置表达式为单一返回值
 * 
 * 确保表达式只返回一个值，这是最常见的表达式求值模式。
 * 
 * @param fs 函数状态指针
 * @param e 表达式描述符
 */
LUAI_FUNC void luaK_setoneret(FuncState *fs, expdesc *e);

/**
 * @brief 生成无条件跳转指令
 * 
 * 生成JMP指令实现无条件跳转。返回跳转指令的位置，用于后续的
 * 目标地址回填。
 * 
 * @param fs 函数状态指针
 * @return 跳转指令的位置（用于补丁）
 */
LUAI_FUNC int luaK_jump(FuncState *fs);

/**
 * @brief 生成函数返回指令
 * 
 * 生成RETURN指令，实现函数的返回。支持返回多个值。
 * 
 * @param fs 函数状态指针
 * @param first 第一个返回值的寄存器索引
 * @param nret 返回值数量
 */
LUAI_FUNC void luaK_ret(FuncState *fs, int first, int nret);

/**
 * @brief 将跳转列表补丁到指定目标
 * 
 * 将一个跳转补丁列表中的所有跳转指令的目标地址设置为指定位置。
 * 这是实现前向跳转的关键机制。
 * 
 * @param fs 函数状态指针
 * @param list 跳转补丁列表的头节点
 * @param target 目标位置（指令索引）
 */
LUAI_FUNC void luaK_patchlist(FuncState *fs, int list, int target);

/**
 * @brief 将跳转列表补丁到当前位置
 * 
 * 将跳转列表中的所有跳转指令目标设置为当前的指令位置。这是
 * luaK_patchlist的便捷版本。
 * 
 * @param fs 函数状态指针
 * @param list 跳转补丁列表
 */
LUAI_FUNC void luaK_patchtohere(FuncState *fs, int list);

/**
 * @brief 连接两个跳转列表
 * 
 * 将两个跳转补丁列表合并为一个列表。这在处理复杂的控制流时很有用。
 * 
 * @param fs 函数状态指针
 * @param l1 第一个列表的头指针（会被修改）
 * @param l2 第二个列表的头
 */
LUAI_FUNC void luaK_concat(FuncState *fs, int *l1, int l2);

/**
 * @brief 获取当前指令位置的标签
 * 
 * 返回当前指令位置，用作跳转目标的标签。这是实现标签和跳转的基础。
 * 
 * @param fs 函数状态指针
 * @return 当前指令位置
 */
LUAI_FUNC int luaK_getlabel(FuncState *fs);

/**
 * @brief 处理前缀一元运算符
 * 
 * 为一元运算符生成相应的字节码指令。处理运算符优先级和结合性。
 * 
 * @param fs 函数状态指针
 * @param op 一元运算符类型
 * @param v 操作数表达式
 */
LUAI_FUNC void luaK_prefix(FuncState *fs, UnOpr op, expdesc *v);

/**
 * @brief 处理中缀二元运算符（第一阶段）
 * 
 * 处理二元运算符的左操作数，为后续的运算符处理做准备。这是二元
 * 表达式处理的第一阶段。
 * 
 * @param fs 函数状态指针
 * @param op 二元运算符类型
 * @param v 左操作数表达式
 */
LUAI_FUNC void luaK_infix(FuncState *fs, BinOpr op, expdesc *v);

/**
 * @brief 处理后缀二元运算符（第二阶段）
 * 
 * 完成二元运算符的处理，生成相应的字节码指令。这是二元表达式
 * 处理的第二阶段，此时左右操作数都已准备好。
 * 
 * @param fs 函数状态指针
 * @param op 二元运算符类型
 * @param v1 左操作数表达式
 * @param v2 右操作数表达式
 */
LUAI_FUNC void luaK_posfix(FuncState *fs, BinOpr op, expdesc *v1, expdesc *v2);

/**
 * @brief 生成表构造的列表部分设置指令
 * 
 * 为表构造器生成SETLIST指令，用于批量设置表的数组部分。这是优化
 * 表构造性能的重要机制。
 * 
 * @param fs 函数状态指针
 * @param base 表对象所在的寄存器
 * @param nelems 要设置的元素数量
 * @param tostore 实际存储的元素数量（可能不同于nelems）
 */
LUAI_FUNC void luaK_setlist(FuncState *fs, int base, int nelems, int tostore);

#endif
