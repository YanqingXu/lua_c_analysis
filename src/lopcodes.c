/*
** [核心] Lua 虚拟机操作码定义与元数据
**
** 功能概述：
** 本模块定义了Lua虚拟机的所有操作码及其元数据信息。包含操作码的名称
** 映射表和操作模式定义，为虚拟机指令的执行、调试和反汇编提供基础数据。
** 这是Lua编译器和虚拟机的核心组件，定义了虚拟机指令集架构。
**
** 主要组件：
** - luaP_opnames：操作码名称字符串数组
** - luaP_opmodes：操作码模式定义数组
** - 指令格式元数据：参数类型和编码方式信息
** - 调试支持：为反汇编器提供可读的指令名称
**
** 指令集特点：
** - 3操作数指令：大部分指令采用A、B、C三个操作数
** - 混合编码：支持iABC、iABx、iAsBx三种编码格式
** - 参数类型：寄存器、常量、upvalue等多种参数类型
** - 条件执行：支持条件跳转和测试指令
**
** 操作码分类：
** - 数据移动：MOVE、LOADK、LOADBOOL等
** - 变量访问：GETGLOBAL、SETGLOBAL、GETTABLE等
** - 算术运算：ADD、SUB、MUL、DIV等
** - 逻辑运算：NOT、TEST、比较操作等
** - 控制流：JMP、CALL、RETURN等
** - 对象操作：NEWTABLE、SETLIST、CLOSURE等
**
** 设计原则：
** - 简洁高效：精简的指令集，高效的执行
** - 类型安全：明确的参数类型检查
** - 调试友好：完善的元数据支持
** - 扩展性：预留扩展空间和版本兼容性
**
** 依赖模块：
** - lopcodes.h：操作码枚举和常量定义
** - lua.h：基础类型和常量定义
*/

#define lopcodes_c
#define LUA_CORE

#include "lopcodes.h"


/*
** [公共] 操作码名称字符串数组
**
** 功能说明：
** 将操作码枚举值映射为可读的字符串名称，主要用于调试、反汇编和错误
** 报告。数组按照操作码的枚举顺序排列，通过索引直接访问对应的名称。
**
** 数据结构：
** - 索引：对应OpCode枚举值
** - 内容：操作码的ASCII名称字符串
** - 终止：以NULL结尾标记数组边界
**
** 算法复杂度：O(1) 访问时间
**
** 使用场景：
** - 调试器显示指令名称
** - 反汇编器生成可读代码
** - 错误信息中显示指令类型
** - 性能分析工具的指令统计
**
** 维护注意：
** - 必须与OpCode枚举保持严格同步
** - 顺序必须与lopcodes.h中的定义一致
** - 添加新操作码时必须同步更新
*/
const char *const luaP_opnames[NUM_OPCODES+1] = {
    /*
    ** [数据移动指令] 基础的数据传输操作
    */
    "MOVE",          /* 寄存器间数据移动 */
    "LOADK",         /* 加载常量到寄存器 */
    "LOADBOOL",      /* 加载布尔值到寄存器 */
    "LOADNIL",       /* 加载nil值到寄存器范围 */
    
    /*
    ** [变量访问指令] upvalue和全局变量操作
    */
    "GETUPVAL",      /* 获取upvalue值 */
    "GETGLOBAL",     /* 获取全局变量值 */
    "GETTABLE",      /* 获取表元素值 */
    "SETGLOBAL",     /* 设置全局变量值 */
    "SETUPVAL",      /* 设置upvalue值 */
    "SETTABLE",      /* 设置表元素值 */
    
    /*
    ** [对象构造指令] 表和函数对象创建
    */
    "NEWTABLE",      /* 创建新表对象 */
    "SELF",          /* 方法调用的self参数准备 */
    
    /*
    ** [算术运算指令] 数值计算操作
    */
    "ADD",           /* 加法运算 */
    "SUB",           /* 减法运算 */
    "MUL",           /* 乘法运算 */
    "DIV",           /* 除法运算 */
    "MOD",           /* 取模运算 */
    "POW",           /* 幂运算 */
    "UNM",           /* 一元负号运算 */
    
    /*
    ** [逻辑运算指令] 布尔和比较操作
    */
    "NOT",           /* 逻辑非运算 */
    "LEN",           /* 长度运算符 */
    "CONCAT",        /* 字符串连接运算 */
    
    /*
    ** [控制流指令] 跳转和条件执行
    */
    "JMP",           /* 无条件跳转 */
    "EQ",            /* 相等比较 */
    "LT",            /* 小于比较 */
    "LE",            /* 小于等于比较 */
    "TEST",          /* 条件测试 */
    "TESTSET",       /* 条件测试并设置 */
    
    /*
    ** [函数调用指令] 函数调用和返回
    */
    "CALL",          /* 普通函数调用 */
    "TAILCALL",      /* 尾调用优化 */
    "RETURN",        /* 函数返回 */
    
    /*
    ** [循环控制指令] for循环的特殊支持
    */
    "FORLOOP",       /* 数值for循环控制 */
    "FORPREP",       /* 数值for循环准备 */
    "TFORLOOP",      /* 通用for循环控制 */
    
    /*
    ** [表操作指令] 表的批量操作
    */
    "SETLIST",       /* 批量设置表元素 */
    
    /*
    ** [其他指令] 特殊功能操作
    */
    "CLOSE",         /* 关闭upvalue */
    "CLOSURE",       /* 创建闭包 */
    "VARARG",        /* 可变参数处理 */
    
    /*
    ** [数组终止] NULL标记数组结束
    */
    NULL
};


/*
** [内部] 操作码模式构造宏
**
** 功能说明：
** 将操作码的各种属性编码为一个字节，用于快速查询指令的执行模式。
** 这个宏将多个布尔和枚举值打包为一个紧凑的位域结构。
**
** 参数说明：
** @param t - 测试模式标志(0=普通指令, 1=测试指令)
** @param a - A操作数测试标志(0=不测试, 1=测试)
** @param b - B操作数类型(OpArgN/OpArgU/OpArgR/OpArgK)
** @param c - C操作数类型(OpArgN/OpArgU/OpArgR/OpArgK)
** @param m - 指令编码模式(iABC/iABx/iAsBx)
**
** 位域布局：
** - 位7：测试模式标志
** - 位6：A操作数测试标志
** - 位5-4：B操作数类型
** - 位3-2：C操作数类型
** - 位1-0：指令编码模式
**
** 算法复杂度：O(1) 编译时计算
*/
#define opmode(t,a,b,c,m) (((t)<<7) | ((a)<<6) | ((b)<<4) | ((c)<<2) | (m))


/*
** [公共] 操作码模式定义数组
**
** 功能说明：
** 为每个操作码定义其执行模式和参数类型，虚拟机根据这些信息正确
** 解码和执行指令。包含参数类型、编码格式、测试模式等元数据。
**
** 数据结构：
** - 每个元素对应一个操作码
** - 使用位域编码多种属性
** - 按照OpCode枚举顺序排列
**
** 参数类型说明：
** - OpArgN：无参数
** - OpArgU：无类型参数（原始值）
** - OpArgR：寄存器参数
** - OpArgK：寄存器或常量参数
**
** 编码模式说明：
** - iABC：A(8bit) B(9bit) C(9bit)三操作数格式
** - iABx：A(8bit) Bx(18bit)两操作数格式
** - iAsBx：A(8bit) sBx(18bit)带符号两操作数格式
**
** 算法复杂度：O(1) 查询时间
**
** 使用场景：
** - 虚拟机指令解码
** - 编译器指令生成验证
** - 调试器指令分析
** - 反汇编器参数解析
*/
const lu_byte luaP_opmodes[NUM_OPCODES] = {
    /*
    ** [数据移动指令模式定义]
    ** 基础的数据传输操作的参数和编码模式
    */
    /*       T  A    B       C     mode           opcode      */
    opmode(0, 1, OpArgR, OpArgN, iABC),         /* OP_MOVE     */
    opmode(0, 1, OpArgK, OpArgN, iABx),         /* OP_LOADK    */
    opmode(0, 1, OpArgU, OpArgU, iABC),         /* OP_LOADBOOL */
    opmode(0, 1, OpArgR, OpArgN, iABC),         /* OP_LOADNIL  */
    
    /*
    ** [变量访问指令模式定义]
    ** upvalue和全局变量操作的参数模式
    */
    opmode(0, 1, OpArgU, OpArgN, iABC),         /* OP_GETUPVAL */
    opmode(0, 1, OpArgK, OpArgN, iABx),         /* OP_GETGLOBAL */
    opmode(0, 1, OpArgR, OpArgK, iABC),         /* OP_GETTABLE */
    opmode(0, 0, OpArgK, OpArgN, iABx),         /* OP_SETGLOBAL */
    opmode(0, 0, OpArgU, OpArgN, iABC),         /* OP_SETUPVAL */
    opmode(0, 0, OpArgK, OpArgK, iABC),         /* OP_SETTABLE */
    
    /*
    ** [对象构造指令模式定义]
    ** 表和函数对象创建操作的参数模式
    */
    opmode(0, 1, OpArgU, OpArgU, iABC),         /* OP_NEWTABLE */
    opmode(0, 1, OpArgR, OpArgK, iABC),         /* OP_SELF     */
    
    /*
    ** [算术运算指令模式定义]
    ** 数值计算操作的参数模式，支持寄存器和常量操作数
    */
    opmode(0, 1, OpArgK, OpArgK, iABC),         /* OP_ADD      */
    opmode(0, 1, OpArgK, OpArgK, iABC),         /* OP_SUB      */
    opmode(0, 1, OpArgK, OpArgK, iABC),         /* OP_MUL      */
    opmode(0, 1, OpArgK, OpArgK, iABC),         /* OP_DIV      */
    opmode(0, 1, OpArgK, OpArgK, iABC),         /* OP_MOD      */
    opmode(0, 1, OpArgK, OpArgK, iABC),         /* OP_POW      */
    opmode(0, 1, OpArgR, OpArgN, iABC),         /* OP_UNM      */
    
    /*
    ** [逻辑运算指令模式定义]
    ** 布尔和比较操作的参数模式
    */
    opmode(0, 1, OpArgR, OpArgN, iABC),         /* OP_NOT      */
    opmode(0, 1, OpArgR, OpArgN, iABC),         /* OP_LEN      */
    opmode(0, 1, OpArgR, OpArgR, iABC),         /* OP_CONCAT   */
    
    /*
    ** [控制流指令模式定义]
    ** 跳转和条件执行操作的参数模式
    */
    opmode(0, 0, OpArgR, OpArgN, iAsBx),        /* OP_JMP      */
    opmode(1, 0, OpArgK, OpArgK, iABC),         /* OP_EQ       */
    opmode(1, 0, OpArgK, OpArgK, iABC),         /* OP_LT       */
    opmode(1, 0, OpArgK, OpArgK, iABC),         /* OP_LE       */
    opmode(1, 1, OpArgR, OpArgU, iABC),         /* OP_TEST     */
    opmode(1, 1, OpArgR, OpArgU, iABC),         /* OP_TESTSET  */
    
    /*
    ** [函数调用指令模式定义]
    ** 函数调用和返回操作的参数模式
    */
    opmode(0, 1, OpArgU, OpArgU, iABC),         /* OP_CALL     */
    opmode(0, 1, OpArgU, OpArgU, iABC),         /* OP_TAILCALL */
    opmode(0, 0, OpArgU, OpArgN, iABC),         /* OP_RETURN   */
    
    /*
    ** [循环控制指令模式定义]
    ** for循环特殊支持操作的参数模式
    */
    opmode(0, 1, OpArgR, OpArgN, iAsBx),        /* OP_FORLOOP  */
    opmode(0, 1, OpArgR, OpArgN, iAsBx),        /* OP_FORPREP  */
    opmode(1, 0, OpArgN, OpArgU, iABC),         /* OP_TFORLOOP */
    
    /*
    ** [表操作指令模式定义]
    ** 表的批量操作和特殊处理的参数模式
    */
    opmode(0, 0, OpArgU, OpArgU, iABC),         /* OP_SETLIST  */
    
    /*
    ** [其他指令模式定义]
    ** 特殊功能操作的参数模式
    */
    opmode(0, 0, OpArgN, OpArgN, iABC),         /* OP_CLOSE    */
    opmode(0, 1, OpArgU, OpArgN, iABx),         /* OP_CLOSURE  */
    opmode(0, 1, OpArgU, OpArgN, iABC)          /* OP_VARARG   */
};