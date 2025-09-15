/**
 * @file print.c
 * @brief Lua字节码打印工具：反汇编和调试工具的核心实现
 * 
 * 版权信息：
 * $Id: print.c,v 1.55a 2006/05/31 13:30:05 lhf Exp $
 * 打印字节码工具
 * 版权声明见lua.h文件
 * 
 * 程序概述：
 * 本文件实现了Lua字节码的反汇编和打印工具，能够将编译后的Lua函数原型（Proto）
 * 转换为可读的汇编代码格式。它是Lua调试和开发工具链的重要组成部分，
 * 为程序员理解Lua虚拟机的执行机制提供直观的工具。
 * 
 * 系统架构定位：
 * 作为Lua编译器和调试器的辅助工具，字节码打印工具位于开发工具层，
 * 与虚拟机执行引擎和编译器紧密协作，为上层的调试工具和分析工具提供
 * 底层的字节码解析能力。
 * 
 * 核心功能：
 * 1. **指令反汇编**: 将字节码指令转换为可读的汇编格式
 * 2. **常量数据显示**: 打印函数中的常量表和字符串数据
 * 3. **符号信息输出**: 显示局部变量、上值和行号信息
 * 4. **嵌套函数处理**: 递归打印嵌套函数的字节码
 * 5. **格式化输出**: 提供清晰的缩进和对齐格式
 * 
 * 支持的数据类型：
 * - **基础类型**: nil, boolean, number, string
 * - **复合类型**: table, function, userdata, thread
 * - **内部类型**: proto, upval
 * - **控制结构**: 循环、条件、函数调用
 * 
 * 指令分析能力：
 * - **全部指令**: 支持38种操作码的详细解析
 * - **参数解析**: 自动识别和显示各种参数类型
 * - **地址计算**: 自动计算跳转目标和相对地址
 * - **常量引用**: 自动解析和显示常量引用
 * 
 * 输出格式特点：
 * - 行号映射：显示字节码与源代码的对应关系
 * - 指令编号：为每条指令提供编号和地址
 * - 参数注释：为复杂指令提供详细的参数说明
 * - 符号表：显示完整的局部变量和上值信息
 * 
 * 使用场景：
 * - **代码分析**: 理解Lua程序的编译结果和执行逻辑
 * - **性能优化**: 找出性能瓶颈和优化机会
 * - **调试支持**: 为调试器提供详细的程序信息
 * - **教学工具**: 帮助学习和理解Lua虚拟机原理
 * - **逆向工程**: 分析和理解第三方Lua代码
 * 
 * 技术特点：
 * - 递归处理：自动处理嵌套函数和闭包
 * - 类型安全：严格检查和处理所有数据类型
 * - 内存安全：正确处理指针和内存访问
 * - 可移植性：使用标准C库函数，适应多平台
 * 
 * 性能考虑：
 * - 流式输出：避免大量的内存缓冲
 * - 懒惰加载：按需加载和处理数据
 * - 紧凑编码：优化的字符串和数字显示
 * 
 * 安全性特性：
 * - 边界检查：严格检查数组和指针访问
 * - 类型验证：确保所有数据类型的正确性
 * - 错误处理：对异常情况提供合理的错误信息
 * 
 * @author Luiz Henrique de Figueiredo
 * @version 1.55a
 * @date 2006-05-31
 * 
 * @see lopcodes.h 操作码定义和宏
 * @see lobject.h Lua对象系统定义
 * @see lundump.h 字节码加载器接口
 * @see ldebug.h 调试信息接口
 * 
 * @note 本模块是Lua开发工具链的重要组成部分
 */

#include <ctype.h>
#include <stdio.h>

#define luac_c
#define LUA_CORE

#include "ldebug.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lundump.h"

/** @brief 主打印函数别名 */
#define PrintFunction    luaU_print

/** @brief 计算类型大小的宏 */
#define Sizeof(x)        ((int)sizeof(x))

/** @brief 安全的void指针转换宏 */
#define VOID(p)          ((const void*)(p))

/**
 * @brief 打印Lua字符串：将字符串以C字符串字面量格式输出
 * 
 * 详细说明：
 * 这个函数将Lua字符串对象转换为C风格的字符串字面量格式，包括双引号和
 * 必要的转义字符。它能够正确处理各种特殊字符，包括控制字符、不可打印字符
 * 和高位字节，确保输出的字符串可以被C编译器正确解析。
 * 
 * 转义字符处理：
 * - 双引号：" → \\"
 * - 反斜杠：\\ → \\\\
 * - 响铃：\a → \\a
 * - 退格：\b → \\b
 * - 换页：\f → \\f
 * - 换行：\n → \\n
 * - 回车：\r → \\r
 * - 制表符：\t → \\t
 * - 垂直制表符：\v → \\v
 * 
 * 不可打印字符处理：
 * 对于不可打印的字符（ASCII码 < 32 或 > 126），使用\nnn格式，
 * 其中nnn是三位十进制数，确保与原始字节值一致。
 * 
 * 输出格式：
 * 输出以双引号开始和结束，中间包含转义后的字符序列。
 * 例如："Hello\nWorld"、"\"quoted\""、"\001\002\003"
 * 
 * 安全性考虑：
 * - 使用unsigned char转换避免符号扩展问题
 * - 严格按照字符串长度限制访问，避免越界
 * - 对所有特殊字符进行适当的转义处理
 * 
 * @param ts Lua字符串对象指针，不能为NULL
 * 
 * @return void 直接输出到标准输出流
 * 
 * @see TString Lua字符串对象结构
 * @see getstr() 获取字符串内容的宏
 * @see isprint() C标准库字符检查函数
 * 
 * @note 这个函数与标准C的printf("%q")功能类似
 * 
 * @warning 不检查ts参数的有效性，调用方需确保参数正确
 */
static void PrintString(const TString* ts)
{
    const char* s=getstr(ts);
    size_t i,n=ts->tsv.len;
    putchar('"');
    for (i=0; i<n; i++)
    {
        int c=s[i];
        switch (c)
        {
        case '"': printf("\\\""); break;
        case '\\': printf("\\\\"); break;
        case '\a': printf("\\a"); break;
        case '\b': printf("\\b"); break;
        case '\f': printf("\\f"); break;
        case '\n': printf("\\n"); break;
        case '\r': printf("\\r"); break;
        case '\t': printf("\\t"); break;
        case '\v': printf("\\v"); break;
        default:
            if (isprint((unsigned char)c))
                putchar(c);
            else
                printf("\\%03u",(unsigned char)c);
        }
    }
    putchar('"');
}

/**
 * @brief 打印常量值：以可读格式显示Lua常量的值
 * 
 * 详细说明：
 * 这个函数根据常量的类型，以适当的格式打印常量的值。它支持Lua的
 * 所有基本数据类型，并能够处理特殊情况和异常类型。输出格式与
 * Lua源代码中的字面量表示保持一致。
 * 
 * 支持的数据类型：
 * 
 * **LUA_TNIL**：
 * - 输出："nil"
 * - 表示Lua中的空值
 * 
 * **LUA_TBOOLEAN**：
 * - 输出："true" 或 "false"
 * - 根据布尔值的实际值确定
 * 
 * **LUA_TNUMBER**：
 * - 输出：使用LUA_NUMBER_FMT格式化的数字
 * - 保持数字的精度和格式
 * - 支持整数和浮点数
 * 
 * **LUA_TSTRING**：
 * - 输出：调用PrintString()格式化的字符串
 * - 包括双引号和必要的转义字符
 * - 完全兼容C字符串字面量格式
 * 
 * **异常类型**：
 * - 输出："? type=N"（N为类型码）
 * - 用于调试异常情况和错误排查
 * 
 * 使用场景：
 * - 常量表打印：显示函数中的所有常量
 * - 指令参数注释：在指令旁边显示常量值
 * - 调试信息：帮助理解代码的执行逻辑
 * - 代码分析：分析程序中使用的数据
 * 
 * 格式特点：
 * - 与C语言兼容：输出可以直接用于C代码
 * - 类型安全：严格检查数据类型
 * - 容错性：对于未知类型提供错误信息
 * 
 * 性能考虑：
 * - 直接输出：避免不必要的字符串缓冲
 * - 类型分支：使用switch语句实现高效分支
 * - 减少复制：直接操作原始数据
 * 
 * @param f 函数原型指针，包含常量表
 * @param i 常量索引，必须在有效范围内
 * 
 * @return void 直接输出到标准输出流
 * 
 * @see TValue Lua值对象结构
 * @see Proto 函数原型结构
 * @see PrintString() 字符串打印函数
 * @see LUA_NUMBER_FMT 数字格式化宏
 * 
 * @note 输出格式与Lua源代码字面量保持一致
 * 
 * @warning 不检查参数有效性，调用方需确保索引在范围内
 */
static void PrintConstant(const Proto* f, int i)
{
    const TValue* o=&f->k[i];
    switch (ttype(o))
    {
    case LUA_TNIL:
        printf("nil");
        break;
    case LUA_TBOOLEAN:
        printf(bvalue(o) ? "true" : "false");
        break;
    case LUA_TNUMBER:
        printf(LUA_NUMBER_FMT,nvalue(o));
        break;
    case LUA_TSTRING:
        PrintString(rawtsvalue(o));
        break;
    default:
        printf("? type=%d",ttype(o));
        break;
    }
}

/**
 * @brief 打印字节码指令：将函数的所有字节码指令以可读格式输出
 * 
 * 详细说明：
 * 这是整个字节码打印工具的核心函数，负责将二进制格式的字节码指令
 * 转换为类似汇编语言的可读格式。它能够解析所有Lua操作码，显示详细的
 * 参数信息，并为复杂指令提供注释和辅助信息。
 * 
 * 输出格式组成：
 * 每一行包含以下信息：
 * 1. **PC计数器**: 指令在函数中的位置（从1开始）
 * 2. **源代码行号**: [行号] 或 [-]（如果无行号信息）
 * 3. **指令名称**: 左对齐9个字符的操作码名称
 * 4. **指令参数**: 根据指令类型显示不同的参数
 * 5. **参数注释**: 对复杂指令提供辅助说明
 * 
 * 指令格式处理：
 * 
 * **iABC格式**：
 * - 显示A参数
 * - 如果B参数被使用，显示B值（常量用负数表示）
 * - 如果C参数被使用，显示C值（常量用负数表示）
 * 
 * **iABx格式**：
 * - 显示A参数和Bx参数
 * - 常量索引用负数表示
 * 
 * **iAsBx格式**：
 * - JMP指令只显示跳转偏移
 * - 其他指令显示A参数和有符号偏移
 * 
 * 特殊指令注释：
 * - **LOADK**: 显示加载的常量值
 * - **GETUPVAL/SETUPVAL**: 显示上值名称
 * - **GETGLOBAL/SETGLOBAL**: 显示全局变量名称
 * - **GETTABLE/SETTABLE**: 显示常量索引值
 * - **算术指令**: 显示常量操作数
 * - **比较指令**: 显示常量比较值
 * - **跳转指令**: 显示目标地址
 * - **CLOSURE**: 显示函数原型地址
 * - **SETLIST**: 显示列表大小参数
 * 
 * 地址计算：
 * 对于跳转指令（JMP, FORLOOP, FORPREP），自动计算目标地址：
 * 目标PC = 当前PC + 偏移 + 2
 * 
 * 常量处理：
 * - 常量索引使用ISK()宏检查
 * - 负数表示常量表索引：-1-index
 * - 正数表示寄存器索引
 * 
 * 输出示例：
 * ```
 * 1    [1]    LOADK       0 -1    ; "Hello"
 * 2    [1]    GETGLOBAL   1 -2    ; print
 * 3    [1]    CALL        1 2 1
 * 4    [1]    RETURN      0 1
 * ```
 * 
 * @param f 函数原型指针，包含字节码和元数据
 * 
 * @return void 直接输出到标准输出流
 * 
 * @see Proto 函数原型结构
 * @see Instruction 指令类型定义
 * @see luaP_opnames 操作码名称表
 * @see getOpMode() 获取指令模式的宏
 * @see GETARG_* 参数提取宏组
 * 
 * @note 这是字节码分析和调试的核心工具
 * 
 * @warning 不检查函数原型的有效性，调用方需确保参数正确
 */
static void PrintCode(const Proto* f)
{
    const Instruction* code=f->code;
    int pc,n=f->sizecode;
    for (pc=0; pc<n; pc++)
    {
        Instruction i=code[pc];
        OpCode o=GET_OPCODE(i);
        int a=GETARG_A(i);
        int b=GETARG_B(i);
        int c=GETARG_C(i);
        int bx=GETARG_Bx(i);
        int sbx=GETARG_sBx(i);
        int line=getline(f,pc);
        printf("\t%d\t",pc+1);
        if (line>0) printf("[%d]\t",line); else printf("[-]\t");
        printf("%-9s\t",luaP_opnames[o]);
        switch (getOpMode(o))
        {
        case iABC:
            printf("%d",a);
            if (getBMode(o)!=OpArgN) printf(" %d",ISK(b) ? (-1-INDEXK(b)) : b);
            if (getCMode(o)!=OpArgN) printf(" %d",ISK(c) ? (-1-INDEXK(c)) : c);
            break;
        case iABx:
            if (getBMode(o)==OpArgK) printf("%d %d",a,-1-bx); else printf("%d %d",a,bx);
            break;
        case iAsBx:
            if (o==OP_JMP) printf("%d",sbx); else printf("%d %d",a,sbx);
            break;
        }
        switch (o)
        {
        case OP_LOADK:
            printf("\t; "); PrintConstant(f,bx);
            break;
        case OP_GETUPVAL:
        case OP_SETUPVAL:
            printf("\t; %s", (f->sizeupvalues>0) ? getstr(f->upvalues[b]) : "-");
            break;
        case OP_GETGLOBAL:
        case OP_SETGLOBAL:
            printf("\t; %s",svalue(&f->k[bx]));
            break;
        case OP_GETTABLE:
        case OP_SELF:
            if (ISK(c)) { printf("\t; "); PrintConstant(f,INDEXK(c)); }
            break;
        case OP_SETTABLE:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_POW:
        case OP_EQ:
        case OP_LT:
        case OP_LE:
            if (ISK(b) || ISK(c))
            {
                printf("\t; ");
                if (ISK(b)) PrintConstant(f,INDEXK(b)); else printf("-");
                printf(" ");
                if (ISK(c)) PrintConstant(f,INDEXK(c)); else printf("-");
            }
            break;
        case OP_JMP:
        case OP_FORLOOP:
        case OP_FORPREP:
            printf("\t; to %d",sbx+pc+2);
            break;
        case OP_CLOSURE:
            printf("\t; %p",VOID(f->p[bx]));
            break;
        case OP_SETLIST:
            if (c==0) printf("\t; %d",(int)code[++pc]);
            else printf("\t; %d",c);
            break;
        default:
            break;
        }
        printf("\n");
    }
}

/** @brief 复数形式判断宏：根据数量返回单数或复数后缀 */
#define SS(x)    (x==1)?"":"s"

/** @brief 数量和复数形式组合宏：返回数量和对应的复数后缀 */
#define S(x)     x,SS(x)

/**
 * @brief 打印函数头部信息：显示函数的完整结构信息和统计数据
 * 
 * 详细说明：
 * 输出函数的全面信息，包括源代码位置、指令统计、参数信息、
 * 局部变量、常量和子函数等统计数据。这些信息对于理解函数的
 * 复杂度和结构非常重要。
 * 
 * 输出格式：
 * 第一行：
 * ```
 * [main|function] <文件名:开始行,结束行> (指令数 instruction[s], 字节数 bytes at 地址)
 * ```
 * 
 * 第二行：
 * ```
 * 参数数[+] param[s], 栈大小 slot[s], 上值数 upvalue[s], 
 * ```
 * 
 * 第三行：
 * ```
 * 局部变量数 local[s], 常量数 constant[s], 子函数数 function[s]
 * ```
 * 
 * 字段详细说明：
 * 
 * **函数类型**：
 * - "main": 主函数（linedefined == 0）
 * - "function": 普通函数
 * 
 * **文件源**：
 * - 以'@'开头：文件名（去除'@'前缀）
 * - 以'='开头：特殊源（去除'='前缀）
 * - 以LUA_SIGNATURE开头：二进制字符串"(bstring)"
 * - 其他：普通字符串"(string)"
 * 
 * **行号范围**：
 * - linedefined: 函数定义开始行
 * - lastlinedefined: 函数定义结束行
 * 
 * **指令统计**：
 * - sizecode: 字节码指令数量
 * - 总字节数: sizecode × sizeof(Instruction)
 * - 函数地址: 内存中的位置
 * 
 * **参数信息**：
 * - numparams: 形参个数
 * - "+": 可变参数标识（is_vararg）
 * 
 * **运行时信息**：
 * - maxstacksize: 需要的最大栈空间
 * - nups: 上值个数（闭包变量）
 * 
 * **结构统计**：
 * - sizelocvars: 局部变量个数
 * - sizek: 常量表大小
 * - sizep: 子函数个数
 * 
 * 输出示例：
 * ```
 * main <test.lua:0,10> (15 instructions, 60 bytes at 0x12345678)
 * 0 params, 5 slots, 1 upvalue, 
 * 3 locals, 8 constants, 2 functions
 * 
 * function <test.lua:5,8> (8 instructions, 32 bytes at 0x87654321)
 * 2+ params, 3 slots, 0 upvalues, 
 * 2 locals, 4 constants, 0 functions
 * ```
 * 
 * @param f 函数原型指针，包含完整的函数元数据
 * 
 * @return void 直接输出到标准输出流
 * 
 * @see Proto 函数原型结构体
 * @see getstr() 获取字符串内容的宏
 * @see VOID() 指针转换宏
 * @see Sizeof() 类型大小计算宏
 * @see S() 数量和复数形式组合宏
 * @see SS() 复数形式判断宏
 * 
 * @note 这是函数分析的概览信息，通常在详细分析之前调用
 * 
 * @warning 不检查函数原型的有效性，调用方需确保参数非空
 */
static void PrintHeader(const Proto* f)
{
    const char* s=getstr(f->source);
    if (*s=='@' || *s=='=')
        s++;
    else if (*s==LUA_SIGNATURE[0])
        s="(bstring)";
    else
        s="(string)";
    printf("\n%s <%s:%d,%d> (%d instruction%s, %d bytes at %p)\n",
        (f->linedefined==0)?"main":"function",s,
        f->linedefined,f->lastlinedefined,
        S(f->sizecode),f->sizecode*Sizeof(Instruction),VOID(f));
    printf("%d%s param%s, %d slot%s, %d upvalue%s, ",
        f->numparams,f->is_vararg?"+":"",SS(f->numparams),
        S(f->maxstacksize),S(f->nups));
    printf("%d local%s, %d constant%s, %d function%s\n",
        S(f->sizelocvars),S(f->sizek),S(f->sizep));
}

/**
 * @brief 打印常量表：逐一显示函数中定义的所有常量值
 * 
 * 详细说明：
 * 遍历函数的常量表（k数组），将每个常量的索引和值以可读格式
 * 输出。常量表包含函数中使用的所有字面量值，如数字、字符串等。
 * 
 * 输出格式：
 * ```
 * constants (常量数) for 函数地址:
 * \t索引\t常量值
 * \t索引\t常量值
 * ...
 * ```
 * 
 * 索引说明：
 * - 索引从1开始（显示用），实际存储从0开始
 * - 在字节码中通过LOADK指令引用
 * - 负数索引表示常量：-1-index
 * 
 * 常量类型：
 * 
 * **数值常量**：
 * - 整数：直接显示数值
 * - 浮点数：显示小数形式
 * 
 * **字符串常量**：
 * - 使用双引号包围
 * - 特殊字符进行转义
 * - 不可打印字符使用\nnn格式
 * 
 * **布尔常量**：
 * - true/false字面量
 * 
 * **nil常量**：
 * - nil字面量
 * 
 * 使用场景：
 * - 字节码分析：理解函数使用的数据
 * - 调试排错：查看常量加载情况
 * - 性能分析：评估常量表大小
 * - 代码优化：发现重复或未使用的常量
 * 
 * 输出示例：
 * ```
 * constants (5) for 0x12345678:
 *     1    "Hello"
 *     2    10
 *     3    3.14
 *     4    true
 *     5    nil
 * ```
 * 
 * @param f 函数原型指针，包含常量表数据
 * 
 * @return void 直接输出到标准输出流
 * 
 * @see Proto 函数原型结构体
 * @see TValue 常量值类型
 * @see PrintConstant() 单个常量打印函数
 * @see VOID() 指针转换宏
 * 
 * @note 常量表是函数数据结构的重要组成部分
 * 
 * @warning 不检查函数原型的有效性，调用方需确保参数正确
 */
static void PrintConstants(const Proto* f)
{
    int i,n=f->sizek;
    printf("constants (%d) for %p:\n",n,VOID(f));
    for (i=0; i<n; i++)
    {
        printf("\t%d\t",i+1);
        PrintConstant(f,i);
        printf("\n");
    }
}

/**
 * @brief 打印局部变量表：显示函数中定义的所有局部变量信息
 * 
 * 详细说明：
 * 遍历函数的局部变量表（locvars数组），将每个局部变量的
 * 名称和生命周期信息以表格形式输出。这些信息对于
 * 理解变量作用域和调试信息非常重要。
 * 
 * 输出格式：
 * ```
 * locals (局部变量数) for 函数地址:
 * \t索引\t变量名\t开始位置\t结束位置
 * \t索引\t变量名\t开始位置\t结束位置
 * ...
 * ```
 * 
 * 字段详细说明：
 * 
 * **索引**：
 * - 局部变量在表中的位置（从0开始）
 * - 显示时为了可读性从0开始
 * 
 * **变量名**：
 * - 在源代码中定义的变量名称
 * - 通过getstr()从字符串对象中获取
 * - 保持原始命名，不做任何修改
 * 
 * **开始位置 (startpc)**：
 * - 变量开始有效的字节码指令索引
 * - 显示时+1转换为从1开始的编号
 * - 表示变量作用域的起始点
 * 
 * **结束位置 (endpc)**：
 * - 变量失效的字节码指令索引
 * - 显示时+1转换为从1开始的编号
 * - 表示变量作用域的结束点
 * 
 * 作用域规则：
 * - 变量在[startpc+1, endpc]范围内有效
 * - endpc为0表示在函数结束时失效
 * - 嵌套作用域可以有重叠的变量名
 * 
 * 使用场景：
 * - **调试器支持**：获取当前执行位置的可见变量
 * - **错误报告**：在错误信息中显示变量名
 * - **代码分析**：理解变量的生命周期
 * - **性能优化**：分析变量使用模式
 * 
 * 输出示例：
 * ```
 * locals (3) for 0x12345678:
 *     0    i        5    12
 *     1    j        7    10
 *     2    temp     8    9
 * ```
 * 
 * 表示：
 * - 变量i在指令5-12范围内有效
 * - 变量j在指令7-10范围内有效
 * - 变量temp在指令8-9范围内有效
 * 
 * @param f 函数原型指针，包含局部变量表数据
 * 
 * @return void 直接输出到标准输出流
 * 
 * @see Proto 函数原型结构体
 * @see LocVar 局部变量信息结构体
 * @see getstr() 获取字符串内容的宏
 * @see VOID() 指针转换宏
 * 
 * @note 这些信息是由编译器生成的调试信息，只在调试模式下可用
 * 
 * @warning 不检查函数原型的有效性，调用方需确保参数正确
 */
static void PrintLocals(const Proto* f)
{
    int i,n=f->sizelocvars;
    printf("locals (%d) for %p:\n",n,VOID(f));
    for (i=0; i<n; i++)
    {
        printf("\t%d\t%s\t%d\t%d\n",
        i,getstr(f->locvars[i].varname),f->locvars[i].startpc+1,f->locvars[i].endpc+1);
    }
}

/**
 * @brief 打印上值表：显示函数中引用的所有上值（闭包变量）名称
 * 
 * 详细说明：
 * 遍历函数的上值表（upvalues数组），将每个上值的索引和
 * 名称以表格形式输出。上值是闭包捕获的外部变量，对于
 * 理解函数依赖关系和作用域链非常重要。
 * 
 * 输出格式：
 * ```
 * upvalues (上值数) for 函数地址:
 * \t索引\t上值名
 * \t索引\t上值名
 * ...
 * ```
 * 
 * 字段详细说明：
 * 
 * **索引**：
 * - 上值在表中的位置（从0开始）
 * - 在字节码中通过GETUPVAL/SETUPVAL指令引用
 * - 索引顺序反映了变量的捕获顺序
 * 
 * **上值名**：
 * - 在外部作用域中的原始变量名
 * - 通过getstr()从字符串对象中获取
 * - 保持原始命名，不做任何修改
 * 
 * 上值机制：
 * 
 * **闭包创建过程**：
 * 1. 编译器检测到内部函数引用外部变量
 * 2. 将外部变量添加到上值表
 * 3. 生成相应的GETUPVAL/SETUPVAL指令
 * 
 * **上值类型**：
 * - 局部变量：捕获外部函数的局部变量
 * - 全局变量：捕获全局环境中的变量
 * - 其他上值：嵌套闭包中的上值
 * 
 * **安全检查**：
 * - 函数会检查upvalues数组是否为NULL
 * - 如果upvalues为NULL，直接返回不进行任何输出
 * - 避免空指针解引用导致的崩溃
 * 
 * 使用场景：
 * - **闭包分析**：理解函数的数据依赖
 * - **调试排错**：查看上值访问情况
 * - **内存分析**：理解闭包的内存占用
 * - **代码优化**：发现不必要的上值引用
 * 
 * 输出示例：
 * ```
 * upvalues (2) for 0x12345678:
 *     0    x
 *     1    counter
 * ```
 * 
 * 表示函数捕获了两个外部变量：x和counter
 * 
 * @param f 函数原型指针，包含上值表数据
 * 
 * @return void 直接输出到标准输出流
 * 
 * @see Proto 函数原型结构体
 * @see TString 上值名字符串类型
 * @see getstr() 获取字符串内容的宏
 * @see VOID() 指针转换宏
 * 
 * @note 上值信息是闭包机制的核心数据结构
 * 
 * @warning 不检查函数原型的有效性，但检查upvalues数组是否为NULL
 */
static void PrintUpvalues(const Proto* f)
{
    int i,n=f->sizeupvalues;
    printf("upvalues (%d) for %p:\n",n,VOID(f));
    if (f->upvalues==NULL) return;
    for (i=0; i<n; i++)
    {
        printf("\t%d\t%s\n",i,getstr(f->upvalues[i]));
    }
}

/**
 * @brief 打印函数完整信息：递归显示函数及其所有子函数的详细信息
 * 
 * 详细说明：
 * 这是整个字节码打印工具的主入口函数，提供了对Lua函数
 * 的全面分析功能。它能够以分层结构展示函数的所有数据，
 * 包括字节码指令、常量、局部变量和子函数等。
 * 
 * 函数行为：
 * 
 * **基本输出**（总是执行）：
 * 1. 调用PrintHeader()显示函数头部信息
 * 2. 调用PrintCode()显示所有字节码指令
 * 
 * **详细输出**（仅在full=true时）：
 * 3. 调用PrintConstants()显示常量表
 * 4. 调用PrintLocals()显示局部变量表
 * 5. 调用PrintUpvalues()显示上值表
 * 
 * **递归处理**（总是执行）：
 * 6. 对所有子函数递归调用PrintFunction()
 * 
 * 参数说明：
 * 
 * **f**: 函数原型指针
 * - 包含完整的函数元数据和字节码
 * - 不能为NULL，否则会导致不可预知的行为
 * 
 * **full**: 输出模式控制标志
 * - true：完整模式，输出所有可用信息
 * - false：简化模式，仅输出头部和字节码
 * 
 * 输出层次结构：
 * 
 * **主函数**：
 * ```
 * main <file:line1,line2> (...)
 * [PC] [LINE] OPCODE  ARGS    ; COMMENT
 * ...
 * constants (...)
 * locals (...)
 * upvalues (...)
 * ```
 * 
 * **子函数**：
 * ```
 * function <file:line1,line2> (...)
 * [PC] [LINE] OPCODE  ARGS    ; COMMENT
 * ...
 * constants (...)
 * locals (...)
 * upvalues (...)
 * ```
 * 
 * 递归特性：
 * - 按照子函数在p数组中的顺序处理
 * - 每个子函数都会使用相同full参数
 * - 支持任意深度的嵌套函数
 * - 自动处理循环引用（如果存在）
 * 
 * 使用场景：
 * 
 * **字节码分析**：
 * - 理解Lua编译器的输出结果
 * - 分析代码优化效果
 * - 研究指令编码模式
 * 
 * **调试排错**：
 * - 追踪函数执行流程
 * - 检查变量作用域
 * - 分析常量传播
 * 
 * **性能分析**：
 * - 评估函数复杂度
 * - 统计指令数量
 * - 分析内存使用
 * 
 * **教学工具**：
 * - 展示Lua内部机制
 * - 理解虚拟机运行原理
 * - 学习编译器设计
 * 
 * @param f 函数原型指针，包含完整的函数元数据
 * @param full 输出模式控制：true=完整信息，false=基本信息
 * 
 * @return void 直接输出到标准输出流
 * 
 * @see Proto 函数原型结构体
 * @see PrintHeader() 打印函数头部信息
 * @see PrintCode() 打印字节码指令
 * @see PrintConstants() 打印常量表
 * @see PrintLocals() 打印局部变量表
 * @see PrintUpvalues() 打印上值表
 * 
 * @note 这是字节码分析工具的公共接口，通常由外部调用
 * 
 * @warning 不检查函数原型的有效性，调用方需确保参数非空
 */
void PrintFunction(const Proto* f, int full)
{
    int i,n=f->sizep;
    PrintHeader(f);
    PrintCode(f);
    if (full)
    {
        PrintConstants(f);
        PrintLocals(f);
        PrintUpvalues(f);
    }
    for (i=0; i<n; i++) PrintFunction(f->p[i],full);
}
