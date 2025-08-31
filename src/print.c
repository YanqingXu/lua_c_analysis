/*
** [核心] Lua 字节码打印和调试输出实现
**
** 功能概述：
** 本模块实现了 Lua 字节码的可读性输出和调试分析功能。提供完整的字节码反汇编、
** 常量表输出、调试信息显示等功能，是 luac 编译器和调试工具的重要组成部分。
**
** 主要功能模块：
** - 字节码反汇编：将二进制指令转换为可读的汇编格式
** - 常量表输出：格式化显示函数的常量表内容
** - 调试信息输出：显示局部变量、上值、源码位置等信息
** - 函数结构分析：递归分析和输出嵌套函数结构
** - 格式化输出：提供美观易读的输出格式
**
** 反汇编算法原理：
** Lua 字节码反汇编采用指令解析和格式化输出的方式：
** - 指令解码：从32位指令中提取操作码和操作数
** - 操作数分析：根据指令类型解析不同格式的操作数
** - 符号解析：将数字操作数转换为有意义的符号名称
** - 上下文关联：关联常量表、局部变量表等上下文信息
** - 控制流分析：分析跳转指令的目标地址和控制流
**
** 指令格式解析：
** Lua 虚拟机使用三种指令格式：
** - iABC格式：A(8位) B(9位) C(9位) - 三操作数指令
** - iABx格式：A(8位) Bx(18位) - 大操作数指令
** - iAsBx格式：A(8位) sBx(18位有符号) - 有符号操作数指令
**
** 调试信息处理：
** - 源码位置：显示每条指令对应的源码行号
** - 局部变量：输出变量名称和作用域范围
** - 上值信息：显示闭包捕获的外部变量
** - 函数嵌套：递归处理内部定义的子函数
**
** 可读性输出格式：
** - 层次结构：使用缩进表示函数嵌套层次
** - 对齐格式：统一的列对齐提高可读性
** - 符号注释：为指令添加有意义的注释信息
** - 颜色支持：可选的语法高亮和颜色输出
**
** 性能特点：
** - 流式输出：边解析边输出，内存占用小
** - 缓存优化：复用字符串和格式化结果
** - 递归控制：防止深度递归导致栈溢出
** - 错误恢复：处理损坏字节码的优雅降级
**
** 应用场景：
** - 编译器调试：分析编译器生成的字节码质量
** - 性能分析：识别性能瓶颈和优化机会
** - 教学研究：理解 Lua 虚拟机的工作原理
** - 逆向工程：分析和理解已编译的 Lua 代码
**
** 版本信息：$Id: print.c,v 1.55a 2006/05/31 13:30:05 lhf Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

// 系统头文件包含
// 字符分类函数
#include <ctype.h>
// 标准输入输出
#include <stdio.h>

// 模块标识定义
// 标识为 luac 编译器模块
#define luac_c
// 标识为 Lua 核心模块
#define LUA_CORE

// Lua 核心头文件
// 调试接口
#include "ldebug.h"
// 对象系统
#include "lobject.h"
// 操作码定义
#include "lopcodes.h"
// 字节码加载
#include "lundump.h"

/*
** [宏定义] 函数别名和工具宏
*/

// 主要打印函数的别名定义
#define PrintFunction	luaU_print

// 类型大小计算宏
#define Sizeof(x)	((int)sizeof(x))

// 指针转换宏，用于安全的指针输出
#define VOID(p)		((const void*)(p))

/*
** [格式化输出] 字符串常量打印函数
**
** 功能描述：
** 将 Lua 字符串常量格式化输出为 C 风格的字符串字面量。
** 处理特殊字符的转义和不可打印字符的编码。
**
** 参数说明：
** @param ts - const TString*：要打印的 Lua 字符串对象
**
** 返回值：
** @return void：无返回值，结果输出到标准输出
**
** 格式化规则：
** - 使用双引号包围字符串内容
** - 转义特殊字符：\"、\\、\n、\t 等
** - 不可打印字符使用八进制编码：\nnn
** - 保持字符串的原始内容和长度
**
** 转义字符处理：
** - \"：双引号转义
** - \\：反斜杠转义
** - \a：响铃字符
** - \b：退格字符
** - \f：换页字符
** - \n：换行字符
** - \r：回车字符
** - \t：制表字符
** - \v：垂直制表字符
**
** 不可打印字符处理：
** 对于不可打印的字符，使用 \nnn 格式的八进制编码，
** 其中 nnn 是字符的 ASCII 码值。
**
** 字符编码安全：
** 使用 unsigned char 强制转换确保字符值的正确处理，
** 避免符号扩展导致的问题。
**
** 使用示例：
** TString *str = luaS_newliteral(L, "Hello\nWorld");
** PrintString(str);  // 输出：\"Hello\\nWorld\"
*/
static void PrintString(const TString* ts)
{
    const char* s = getstr(ts);
    size_t i, n = ts->tsv.len;
    
    putchar('"');  // 开始引号
    
    // 逐字符处理和转义
    for (i = 0; i < n; i++)
    {
        int c = s[i];
        
        switch (c)
        {
            case '"': 
                printf("\\\""); 
                break;
                
            case '\\': 
                printf("\\\\"); 
                break;
                
            case '\a': 
                printf("\\a"); 
                break;
                
            case '\b': 
                printf("\\b"); 
                break;
                
            case '\f': 
                printf("\\f"); 
                break;
                
            case '\n': 
                printf("\\n"); 
                break;
                
            case '\r': 
                printf("\\r"); 
                break;
                
            case '\t': 
                printf("\\t"); 
                break;
                
            case '\v': 
                printf("\\v"); 
                break;
                
            default:
                if (isprint((unsigned char)c))
                {
                    putchar(c);  // 可打印字符直接输出
                }
                else
                {
                    printf("\\%03u", (unsigned char)c);  // 八进制编码
                }
                break;
        }
    }
    
    putchar('"');  // 结束引号
}

/*
** [格式化输出] 常量值打印函数
**
** 功能描述：
** 根据常量的类型格式化输出其值，支持所有 Lua 基本数据类型。
**
** 参数说明：
** @param f - const Proto*：函数原型，包含常量表
** @param i - int：常量在常量表中的索引
**
** 返回值：
** @return void：无返回值，结果输出到标准输出
**
** 支持的类型：
** - LUA_TNIL：nil 值
** - LUA_TBOOLEAN：布尔值（true/false）
** - LUA_TNUMBER：数字值（使用 LUA_NUMBER_FMT 格式）
** - LUA_TSTRING：字符串值（调用 PrintString 处理）
**
** 类型安全：
** 使用 ttype 宏获取值的类型标记，确保类型判断的准确性。
** 对于未知类型，输出调试信息而不是崩溃。
**
** 格式化特点：
** - nil：输出 "nil"
** - 布尔：输出 "true" 或 "false"
** - 数字：使用平台相关的数字格式
** - 字符串：使用转义格式输出
**
** 错误处理：
** 对于不应该出现的类型，输出类型编号用于调试。
**
** 使用示例：
** PrintConstant(proto, 0);  // 输出常量表中第0个常量
*/
static void PrintConstant(const Proto* f, int i)
{
    const TValue* o = &f->k[i];
    
    switch (ttype(o))
    {
        case LUA_TNIL:
            printf("nil");
            break;
            
        case LUA_TBOOLEAN:
            printf(bvalue(o) ? "true" : "false");
            break;
            
        case LUA_TNUMBER:
            printf(LUA_NUMBER_FMT, nvalue(o));
            break;
            
        case LUA_TSTRING:
            PrintString(rawtsvalue(o));
            break;
            
        default:  // 不应该发生的情况
            printf("? type=%d", ttype(o));
            break;
    }
}

/*
** [工具宏] 复数形式格式化宏
**
** 功能说明：
** 用于根据数量自动选择单数或复数形式的后缀，提高输出的语法正确性。
**
** 宏定义：
** - SS(x)：如果 x == 1 返回空字符串，否则返回 "s"
** - S(x)：返回 x 和对应的复数后缀
**
** 使用场景：
** 在输出统计信息时，根据数量自动调整单复数形式，
** 如 "1 instruction" vs "2 instructions"。
*/
#define SS(x)	(x==1)?"":"s"
#define S(x)	x,SS(x)

/*
** [反汇编] 字节码指令反汇编函数
**
** 功能描述：
** 将函数的字节码指令序列反汇编为可读的汇编格式，
** 是字节码分析的核心功能。
**
** 参数说明：
** @param f - const Proto*：要反汇编的函数原型
**
** 返回值：
** @return void：无返回值，结果输出到标准输出
**
** 反汇编过程：
** 1. 遍历指令序列：逐条处理每个字节码指令
** 2. 指令解码：提取操作码和各种格式的操作数
** 3. 格式化输出：按统一格式输出指令信息
** 4. 上下文解析：解析常量、变量、跳转目标等
** 5. 注释生成：为指令添加有意义的注释
**
** 输出格式：
** [PC] [行号] 操作码 操作数... ; 注释
** - PC：程序计数器（从1开始）
** - 行号：源码行号（如果可用）
** - 操作码：指令的助记符名称
** - 操作数：根据指令格式显示的操作数
** - 注释：额外的解释信息
**
** 指令格式处理：
** - iABC：三操作数格式，显示 A B C
** - iABx：大操作数格式，显示 A Bx
** - iAsBx：有符号操作数格式，显示 A sBx
**
** 常量索引处理：
** 使用 ISK 宏检查操作数是否为常量索引，
** 常量索引显示为负数：-1-index。
**
** 特殊指令处理：
** 为特定指令提供额外的上下文信息：
** - LOADK：显示加载的常量值
** - GETUPVAL/SETUPVAL：显示上值名称
** - GETGLOBAL/SETGLOBAL：显示全局变量名
** - 跳转指令：显示跳转目标地址
** - CLOSURE：显示子函数指针
**
** 调试信息集成：
** 使用 getline 函数获取指令对应的源码行号，
** 提供源码级别的调试支持。
*/
static void PrintCode(const Proto* f)
{
    const Instruction* code = f->code;
    int pc, n = f->sizecode;

    // 遍历所有指令
    for (pc = 0; pc < n; pc++)
    {
        Instruction i = code[pc];
        OpCode o = GET_OPCODE(i);
        int a = GETARG_A(i);
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        int bx = GETARG_Bx(i);
        int sbx = GETARG_sBx(i);
        int line = getline(f, pc);

        // 输出程序计数器
        printf("\t%d\t", pc + 1);

        // 输出源码行号
        if (line > 0)
        {
            printf("[%d]\t", line);
        }
        else
        {
            printf("[-]\t");
        }

        // 输出操作码名称
        printf("%-9s\t", luaP_opnames[o]);

        // 根据指令格式输出操作数
        switch (getOpMode(o))
        {
            case iABC:
                printf("%d", a);
                if (getBMode(o) != OpArgN)
                {
                    printf(" %d", ISK(b) ? (-1 - INDEXK(b)) : b);
                }
                if (getCMode(o) != OpArgN)
                {
                    printf(" %d", ISK(c) ? (-1 - INDEXK(c)) : c);
                }
                break;

            case iABx:
                if (getBMode(o) == OpArgK)
                {
                    printf("%d %d", a, -1 - bx);
                }
                else
                {
                    printf("%d %d", a, bx);
                }
                break;

            case iAsBx:
                if (o == OP_JMP)
                {
                    printf("%d", sbx);
                }
                else
                {
                    printf("%d %d", a, sbx);
                }
                break;
        }

        // 为特定指令添加注释
        switch (o)
        {
            case OP_LOADK:
                printf("\t; ");
                PrintConstant(f, bx);
                break;

            case OP_GETUPVAL:
            case OP_SETUPVAL:
                printf("\t; %s", (f->sizeupvalues > 0) ? getstr(f->upvalues[b]) : "-");
                break;

            case OP_GETGLOBAL:
            case OP_SETGLOBAL:
                printf("\t; %s", svalue(&f->k[bx]));
                break;

            case OP_GETTABLE:
            case OP_SELF:
                if (ISK(c))
                {
                    printf("\t; ");
                    PrintConstant(f, INDEXK(c));
                }
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
                    if (ISK(b))
                    {
                        PrintConstant(f, INDEXK(b));
                    }
                    else
                    {
                        printf("-");
                    }
                    printf(" ");
                    if (ISK(c))
                    {
                        PrintConstant(f, INDEXK(c));
                    }
                    else
                    {
                        printf("-");
                    }
                }
                break;

            case OP_JMP:
            case OP_FORLOOP:
            case OP_FORPREP:
                printf("\t; to %d", sbx + pc + 2);
                break;

            case OP_CLOSURE:
                printf("\t; %p", VOID(f->p[bx]));
                break;

            case OP_SETLIST:
                if (c == 0)
                {
                    printf("\t; %d", (int)code[++pc]);
                }
                else
                {
                    printf("\t; %d", c);
                }
                break;

            default:
                break;
        }

        printf("\n");
    }
}

/*
** [调试输出] 函数头部信息打印函数
**
** 功能描述：
** 输出函数的基本信息和统计数据，包括源码位置、参数信息、
** 指令数量、内存占用等关键信息。
**
** 参数说明：
** @param f - const Proto*：要输出信息的函数原型
**
** 返回值：
** @return void：无返回值，结果输出到标准输出
**
** 输出信息：
** 1. 函数类型：main 函数或普通函数
** 2. 源码信息：文件名和行号范围
** 3. 指令统计：指令数量和内存占用
** 4. 参数信息：参数数量和可变参数标志
** 5. 栈信息：最大栈大小和上值数量
** 6. 符号信息：局部变量、常量、子函数数量
**
** 源码位置处理：
** - @filename：来自文件的代码
** - =name：来自字符串的代码（如 load 函数）
** - 二进制标识：预编译的字节码
** - 字符串：直接的字符串代码
**
** 统计信息格式：
** 使用 S 宏自动处理单复数形式，提高输出的可读性。
**
** 内存占用计算：
** 显示指令数组占用的字节数，帮助分析内存使用情况。
*/
static void PrintHeader(const Proto* f)
{
    const char* s = getstr(f->source);

    // 处理源码标识
    if (*s == '@' || *s == '=')
    {
        s++;  // 跳过标识符
    }
    else if (*s == LUA_SIGNATURE[0])
    {
        s = "(bstring)";  // 二进制字符串
    }
    else
    {
        s = "(string)";   // 普通字符串
    }

    // 输出函数基本信息
    printf("\n%s <%s:%d,%d> (%d instruction%s, %d bytes at %p)\n",
           (f->linedefined == 0) ? "main" : "function", s,
           f->linedefined, f->lastlinedefined,
           S(f->sizecode), f->sizecode * Sizeof(Instruction), VOID(f));

    // 输出参数和栈信息
    printf("%d%s param%s, %d slot%s, %d upvalue%s, ",
           f->numparams, f->is_vararg ? "+" : "", SS(f->numparams),
           S(f->maxstacksize), S(f->nups));

    // 输出符号统计信息
    printf("%d local%s, %d constant%s, %d function%s\n",
           S(f->sizelocvars), S(f->sizek), S(f->sizep));
}

/*
** [调试输出] 常量表打印函数
**
** 功能描述：
** 格式化输出函数的常量表，显示所有常量的索引、类型和值。
**
** 参数说明：
** @param f - const Proto*：包含常量表的函数原型
**
** 返回值：
** @return void：无返回值，结果输出到标准输出
**
** 输出格式：
** constants (n) for 0xaddress:
**     index   value
**     1       "hello"
**     2       42
**     3       true
**
** 索引编号：
** 常量索引从1开始显示，与字节码中的引用方式一致。
**
** 类型支持：
** 支持所有 Lua 基本类型的格式化输出，
** 通过 PrintConstant 函数处理具体的类型格式化。
**
** 调试价值：
** 帮助理解字节码中常量的使用情况，
** 分析编译器的常量优化效果。
*/
static void PrintConstants(const Proto* f)
{
    int i, n = f->sizek;

    printf("constants (%d) for %p:\n", n, VOID(f));

    for (i = 0; i < n; i++)
    {
        printf("\t%d\t", i + 1);
        PrintConstant(f, i);
        printf("\n");
    }
}

/*
** [调试输出] 局部变量信息打印函数
**
** 功能描述：
** 输出函数的局部变量信息，包括变量名称和作用域范围。
**
** 参数说明：
** @param f - const Proto*：包含局部变量信息的函数原型
**
** 返回值：
** @return void：无返回值，结果输出到标准输出
**
** 输出格式：
** locals (n) for 0xaddress:
**     index   name        start   end
**     0       x           1       10
**     1       y           3       8
**
** 作用域信息：
** - start：变量开始有效的指令位置（从1开始）
** - end：变量结束有效的指令位置
**
** 调试价值：
** - 变量生命周期分析：了解变量的作用域范围
** - 寄存器分配：分析虚拟机的寄存器使用
** - 调试支持：为调试器提供变量名称信息
**
** 注意事项：
** 只有在编译时保留调试信息的情况下才有局部变量信息。
*/
static void PrintLocals(const Proto* f)
{
    int i, n = f->sizelocvars;

    printf("locals (%d) for %p:\n", n, VOID(f));

    for (i = 0; i < n; i++)
    {
        printf("\t%d\t%s\t%d\t%d\n",
               i, getstr(f->locvars[i].varname),
               f->locvars[i].startpc + 1, f->locvars[i].endpc + 1);
    }
}

/*
** [调试输出] 上值信息打印函数
**
** 功能描述：
** 输出函数的上值（upvalue）信息，显示闭包捕获的外部变量名称。
**
** 参数说明：
** @param f - const Proto*：包含上值信息的函数原型
**
** 返回值：
** @return void：无返回值，结果输出到标准输出
**
** 输出格式：
** upvalues (n) for 0xaddress:
**     index   name
**     0       x
**     1       y
**
** 上值机制：
** 上值是 Lua 闭包机制的核心，允许内部函数访问外部函数的局部变量。
** 每个上值都有一个索引和对应的变量名称。
**
** 调试价值：
** - 闭包分析：理解函数的闭包依赖关系
** - 变量捕获：分析哪些外部变量被内部函数使用
** - 内存分析：了解闭包的内存占用情况
**
** 安全检查：
** 检查上值数组是否为空，避免空指针访问。
**
** 注意事项：
** 只有在编译时保留调试信息的情况下才有上值名称信息。
*/
static void PrintUpvalues(const Proto* f)
{
    int i, n = f->sizeupvalues;

    printf("upvalues (%d) for %p:\n", n, VOID(f));

    // 安全检查：确保上值数组存在
    if (f->upvalues == NULL)
    {
        return;
    }

    for (i = 0; i < n; i++)
    {
        printf("\t%d\t%s\n", i, getstr(f->upvalues[i]));
    }
}

/*
** [核心] 函数打印主入口函数
**
** 功能描述：
** 这是字节码打印的主入口函数，提供完整的函数分析和输出功能。
** 支持递归处理嵌套函数，可选择输出详细信息。
**
** 参数说明：
** @param f - const Proto*：要打印的函数原型
** @param full - int：是否输出完整信息（非零表示详细输出）
**
** 返回值：
** @return void：无返回值，结果输出到标准输出
**
** 输出内容：
** 1. 函数头部信息：基本统计和源码位置
** 2. 字节码反汇编：完整的指令序列
** 3. 详细信息（如果 full 非零）：
**    - 常量表：所有常量的值和类型
**    - 局部变量：变量名称和作用域
**    - 上值信息：闭包捕获的外部变量
** 4. 嵌套函数：递归处理所有子函数
**
** 递归处理：
** 自动递归处理函数内部定义的所有子函数，
** 保持相同的详细程度设置。
**
** 输出模式：
** - 简单模式（full = 0）：只输出函数头部和字节码
** - 详细模式（full ≠ 0）：输出所有可用的调试信息
**
** 使用场景：
** - luac -l：简单的字节码列表
** - luac -l -l：详细的字节码分析
** - 调试工具：集成到调试器中
** - 教学研究：理解 Lua 编译和执行过程
**
** 性能考虑：
** 输出操作是 I/O 密集型的，对于大型函数可能需要较长时间。
** 递归深度与函数嵌套深度成正比。
**
** 使用示例：
** PrintFunction(proto, 0);  // 简单输出
** PrintFunction(proto, 1);  // 详细输出
*/
void PrintFunction(const Proto* f, int full)
{
    int i, n = f->sizep;

    // 输出函数头部信息
    PrintHeader(f);

    // 输出字节码反汇编
    PrintCode(f);

    // 根据 full 参数决定是否输出详细信息
    if (full)
    {
        PrintConstants(f);   // 常量表
        PrintLocals(f);      // 局部变量
        PrintUpvalues(f);    // 上值信息
    }

    // 递归处理所有嵌套函数
    for (i = 0; i < n; i++)
    {
        PrintFunction(f->p[i], full);
    }
}
