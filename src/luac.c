/**
 * @file luac.c
 * @brief Lua编译器：将Lua源代码编译为字节码的完整实现
 *
 * 版权信息：
 * $Id: luac.c,v 1.54 2006/06/02 17:37:11 lhf Exp $
 * Lua编译器（将字节码保存到文件；也可列出字节码）
 * 版权声明见lua.h文件
 *
 * 程序概述：
 * 本文件实现了Lua的独立编译器luac，它是Lua工具链的重要组成部分。
 * luac将Lua源代码编译为字节码，支持多种编译选项和输出格式，
 * 为Lua程序的分发和执行提供了高效的解决方案。
 *
 * 主要功能：
 * 1. Lua源代码的语法分析和编译
 * 2. 字节码生成和优化处理
 * 3. 二进制字节码文件输出
 * 4. 字节码列表和反汇编显示
 * 5. 调试信息的处理和剥离
 * 6. 多文件编译和合并
 * 7. 命令行选项和参数处理
 *
 * 设计特点：
 * 1. 高效编译：快速的语法分析和字节码生成
 * 2. 灵活输出：支持多种输出格式和选项
 * 3. 调试支持：可选的调试信息保留和剥离
 * 4. 批处理：支持多文件批量编译
 * 5. 跨平台：使用标准C库，具有良好的可移植性
 *
 * 编译器架构：
 * - 前端：词法分析、语法分析、AST构建
 * - 中端：语义分析、优化处理
 * - 后端：字节码生成、文件输出
 * - 工具：反汇编、调试信息处理
 *
 * 字节码技术：
 * - 基于栈的虚拟机指令集
 * - 紧凑的二进制格式
 * - 高效的执行性能
 * - 跨平台的可移植性
 *
 * 使用场景：
 * - 源代码保护：编译为字节码隐藏源码
 * - 性能优化：预编译减少运行时开销
 * - 分发部署：字节码文件更小更快
 * - 开发调试：字节码分析和优化
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2006-2008
 *
 * @note 这是Lua语言的官方编译器实现
 * @see lua.h, lauxlib.h, lundump.h, lopcodes.h
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define luac_c
#define LUA_CORE

#include "lua.h"
#include "lauxlib.h"

#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstring.h"
#include "lundump.h"

/**
 * @defgroup CompilerConstants 编译器常量定义
 * @brief 编译器的基本常量和默认配置
 *
 * 这些常量定义了编译器的基本行为和默认设置，
 * 包括程序名称、输出文件名等核心配置。
 * @{
 */

/**
 * @brief 默认程序名称
 *
 * 用于错误消息显示和帮助信息中的程序标识。
 */
#define PROGNAME	"luac"		/* 默认程序名称 */

/**
 * @brief 默认输出文件名
 *
 * 当没有指定输出文件时使用的默认文件名。
 * 通常为"luac.out"。
 */
#define	OUTPUT		PROGNAME ".out"	/* 默认输出文件 */

/** @} */ /* 结束编译器常量定义文档组 */

/**
 * @defgroup GlobalState 全局状态和配置
 * @brief 编译器的全局状态变量和运行时配置
 *
 * 这些全局变量控制编译器的行为，包括输出模式、
 * 调试信息处理、文件名配置等。
 * @{
 */

/**
 * @brief 字节码列表标志
 *
 * 控制是否输出字节码的反汇编列表。
 * - 0：不列出字节码
 * - 1：列出基本字节码
 * - >1：列出详细字节码信息
 */
static int listing=0;			/* 列出字节码？ */

/**
 * @brief 字节码转储标志
 *
 * 控制是否将字节码写入输出文件。
 * - 1：转储字节码到文件（默认）
 * - 0：仅解析，不输出字节码
 */
static int dumping=1;			/* 转储字节码？ */

/**
 * @brief 调试信息剥离标志
 *
 * 控制是否在输出的字节码中保留调试信息。
 * - 0：保留调试信息（默认）
 * - 1：剥离调试信息，减小文件大小
 */
static int stripping=0;			/* 剥离调试信息？ */

/**
 * @brief 默认输出文件名缓冲区
 *
 * 存储默认输出文件名的字符数组。
 */
static char Output[]={ OUTPUT };	/* 默认输出文件名 */

/**
 * @brief 实际输出文件名指针
 *
 * 指向当前使用的输出文件名，可能是默认值或用户指定值。
 */
static const char* output=Output;	/* 实际输出文件名 */

/**
 * @brief 实际程序名指针
 *
 * 指向当前使用的程序名，用于错误消息显示。
 */
static const char* progname=PROGNAME;	/* 实际程序名 */

/** @} */ /* 结束全局状态和配置文档组 */

/**
 * @defgroup ErrorHandling 错误处理系统
 * @brief 编译器的错误处理和用户反馈机制
 *
 * 错误处理系统提供了统一的错误报告、程序终止和
 * 用户帮助功能，确保用户能够获得清晰的反馈信息。
 * @{
 */

/**
 * @brief 致命错误处理函数
 *
 * 显示致命错误消息并终止程序执行。用于处理无法
 * 恢复的严重错误情况。
 *
 * @param message 错误消息字符串
 *
 * @note 此函数不会返回，总是调用exit()终止程序
 * @note 错误消息格式：程序名: 错误消息
 *
 * @see fprintf, exit, EXIT_FAILURE
 *
 * 使用场景：
 * - 内存分配失败
 * - 无法创建Lua状态机
 * - 编译过程中的严重错误
 * - 文件操作的致命错误
 *
 * 错误消息格式：
 * ```
 * luac: error message
 * ```
 */
static void fatal(const char* message)
{
    fprintf(stderr,"%s: %s\n",progname,message);
    exit(EXIT_FAILURE);
}

/**
 * @brief 文件操作错误处理函数
 *
 * 处理文件操作相关的错误，显示详细的错误信息
 * 包括操作类型、文件名和系统错误描述。
 *
 * @param what 操作类型描述（如"open", "write", "close"）
 *
 * @note 此函数不会返回，总是调用exit()终止程序
 * @note 自动获取系统错误信息
 *
 * @see fprintf, strerror, errno, exit
 *
 * 使用场景：
 * - 无法打开输出文件
 * - 文件写入失败
 * - 文件关闭错误
 * - 其他文件系统错误
 *
 * 错误消息格式：
 * ```
 * luac: cannot open filename.out: Permission denied
 * luac: cannot write filename.out: Disk full
 * ```
 *
 * 系统集成：
 * - 使用errno获取系统错误码
 * - 使用strerror转换为可读消息
 * - 提供完整的错误上下文
 */
static void cannot(const char* what)
{
    fprintf(stderr,"%s: cannot %s %s: %s\n",progname,what,output,strerror(errno));
    exit(EXIT_FAILURE);
}

/**
 * @brief 使用帮助和错误提示函数
 *
 * 显示程序的使用方法和可用选项。当用户提供无效
 * 参数或请求帮助时调用。
 *
 * @param message 错误消息或无效选项
 *
 * @note 此函数不会返回，总是调用exit()终止程序
 * @note 区分选项错误和一般错误消息
 *
 * @see fprintf, exit, EXIT_FAILURE
 *
 * 消息处理：
 * - 以"-"开头：识别为无效选项
 * - 其他：识别为一般错误消息
 *
 * 显示的选项说明：
 * - "-"：处理标准输入
 * - "-l"：列出字节码
 * - "-o name"：指定输出文件
 * - "-p"：仅解析，不输出
 * - "-s"：剥离调试信息
 * - "-v"：显示版本信息
 * - "--"：停止选项处理
 *
 * 使用格式：
 * ```
 * usage: luac [options] [filenames].
 * ```
 *
 * 设计考虑：
 * - 清晰的选项说明
 * - 标准的Unix命令行格式
 * - 输出到stderr符合惯例
 * - 包含默认输出文件名信息
 */
static void usage(const char* message)
{
    if (*message=='-')
        fprintf(stderr,"%s: unrecognized option " LUA_QS "\n",progname,message);
    else
        fprintf(stderr,"%s: %s\n",progname,message);
    fprintf(stderr,
    "usage: %s [options] [filenames].\n"
    "Available options are:\n"
    "  -        process stdin\n"
    "  -l       list\n"
    "  -o name  output to file " LUA_QL("name") " (default is \"%s\")\n"
    "  -p       parse only\n"
    "  -s       strip debug information\n"
    "  -v       show version information\n"
    "  --       stop handling options\n",
    progname,Output);
    exit(EXIT_FAILURE);
}

/** @} */ /* 结束错误处理系统文档组 */

/**
 * @defgroup ArgumentProcessing 命令行参数处理系统
 * @brief 命令行参数解析和编译选项配置
 *
 * 参数处理系统负责解析命令行参数，设置编译选项，
 * 并为后续的编译流程提供配置信息。
 * @{
 */

/**
 * @brief 字符串比较宏
 *
 * 用于简化命令行参数的字符串比较操作。
 * 比较当前参数argv[i]与指定字符串s是否相等。
 *
 * @param s 要比较的字符串
 * @return 相等返回非0，不等返回0
 */
#define	IS(s)	(strcmp(argv[i],s)==0)

/**
 * @brief 处理命令行参数
 *
 * 解析命令行参数，设置编译选项和配置。这是编译器
 * 参数处理的核心函数。
 *
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 第一个非选项参数的索引（通常是输入文件）
 *
 * @note 修改全局配置变量
 * @note 处理版本信息显示
 *
 * @see usage, printf, exit
 *
 * 支持的选项：
 * - "--"：停止选项处理，后续参数作为文件名
 * - "-"：从标准输入读取，停止选项处理
 * - "-l"：启用字节码列表输出
 * - "-o file"：指定输出文件名
 * - "-p"：仅解析，不生成字节码文件
 * - "-s"：剥离调试信息
 * - "-v"：显示版本信息
 *
 * 参数处理流程：
 * 1. **程序名设置**：
 *    - 从argv[0]获取程序名
 *    - 更新全局progname变量
 *
 * 2. **选项解析循环**：
 *    - 遍历所有命令行参数
 *    - 识别以"-"开头的选项
 *    - 根据选项类型设置相应标志
 *    - 处理需要参数的选项
 *
 * 3. **特殊处理**：
 *    - 检测仅列出或仅解析模式
 *    - 自动设置默认输入文件
 *    - 处理版本信息显示
 *
 * 4. **返回值**：
 *    - 返回第一个输入文件的索引
 *    - 用于后续的文件处理
 *
 * 选项组合规则：
 * - "-l"可以多次使用，增加详细程度
 * - "-v"可以多次使用，控制退出行为
 * - "-p"和"-s"可以组合使用
 * - "-o"必须跟随文件名参数
 *
 * 默认行为：
 * - 如果没有输入文件且启用列出或禁用转储：
 *   * 使用默认输出文件作为输入
 *   * 禁用字节码转储
 * - 版本信息显示后可能退出程序
 *
 * 错误处理：
 * - 无效选项：调用usage()显示帮助
 * - 缺少参数：调用usage()显示错误
 * - 参数格式错误：调用usage()终止程序
 *
 * 使用示例：
 * ```bash
 * luac -o output.out input.lua
 * luac -l -s input1.lua input2.lua
 * luac -p -v input.lua
 * ```
 */
static int doargs(int argc, char* argv[])
{
    int i;
    int version=0;
    if (argv[0]!=NULL && *argv[0]!=0) progname=argv[0];
    for (i=1; i<argc; i++)
    {
        if (*argv[i]!='-')			/* 选项结束；保留它 */
            break;
        else if (IS("--"))			/* 选项结束；跳过它 */
        {
            ++i;
            if (version) ++version;
            break;
        }
        else if (IS("-"))			/* 选项结束；使用标准输入 */
            break;
        else if (IS("-l"))			/* 列出 */
            ++listing;
        else if (IS("-o"))			/* 输出文件 */
        {
            output=argv[++i];
            if (output==NULL || *output==0) usage(LUA_QL("-o") " needs argument");
            if (IS("-")) output=NULL;
        }
        else if (IS("-p"))			/* 仅解析 */
            dumping=0;
        else if (IS("-s"))			/* 剥离调试信息 */
            stripping=1;
        else if (IS("-v"))			/* 显示版本 */
            ++version;
        else					/* 未知选项 */
            usage(argv[i]);
    }
    if (i==argc && (listing || !dumping))
    {
        dumping=0;
        argv[--i]=Output;
    }
    if (version)
    {
        printf("%s  %s\n",LUA_RELEASE,LUA_COPYRIGHT);
        if (version==argc-1) exit(EXIT_SUCCESS);
    }
    return i;
}

/** @} */ /* 结束命令行参数处理系统文档组 */

/**
 * @defgroup CompilationCore 编译核心系统
 * @brief 字节码生成、合并和输出的核心实现
 *
 * 编译核心系统负责将编译后的Lua函数原型进行合并、
 * 优化和输出，是编译器的核心技术实现。
 * @{
 */

/**
 * @brief 获取函数原型的宏
 *
 * 从Lua栈中的闭包对象提取函数原型。这是一个内部宏，
 * 用于简化从栈中获取Proto结构的操作。
 *
 * @param L Lua状态机指针
 * @param i 栈索引（相对于栈顶）
 * @return 指向Proto结构的指针
 *
 * @note 假设栈中的对象是闭包类型
 * @note 使用相对于栈顶的负索引
 */
#define toproto(L,i) (clvalue(L->top+(i))->l.p)

/**
 * @brief 合并多个函数原型
 *
 * 将多个编译后的Lua函数原型合并为一个主函数。
 * 这是多文件编译的核心技术。
 *
 * @param L Lua状态机指针
 * @param n 要合并的函数原型数量
 * @return 合并后的主函数原型
 *
 * @note 单个文件时直接返回原型
 * @note 多个文件时创建包装函数
 *
 * @see luaF_newproto, luaS_newliteral, luaM_newvector
 *
 * 合并策略：
 * 1. **单文件情况**：
 *    - 直接返回栈顶的函数原型
 *    - 不需要额外的包装处理
 *
 * 2. **多文件情况**：
 *    - 创建新的主函数原型
 *    - 设置源文件名为"=(luac)"
 *    - 为每个输入文件生成调用代码
 *    - 构建完整的字节码序列
 *
 * 字节码生成：
 * - 为每个子函数生成CLOSURE指令
 * - 生成CALL指令执行子函数
 * - 最后生成RETURN指令结束执行
 *
 * 内存管理：
 * - 分配指令数组：2*n+1条指令
 * - 分配子函数数组：n个Proto指针
 * - 自动管理栈和内存分配
 *
 * 指令序列：
 * ```
 * CLOSURE 0 0    ; 创建第一个函数的闭包
 * CALL 0 1 1     ; 调用第一个函数
 * CLOSURE 0 1    ; 创建第二个函数的闭包
 * CALL 0 1 1     ; 调用第二个函数
 * ...
 * RETURN 0 1 0   ; 返回
 * ```
 *
 * 栈布局：
 * - 输入：栈中有n个编译后的函数
 * - 处理：创建主函数并设置子函数引用
 * - 输出：返回合并后的主函数原型
 *
 * 应用场景：
 * - 多文件批量编译
 * - 模块化程序的合并
 * - 库文件的打包处理
 */
static const Proto* combine(lua_State* L, int n)
{
    if (n==1)
        return toproto(L,-1);
    else
    {
        int i,pc;
        Proto* f=luaF_newproto(L);
        setptvalue2s(L,L->top,f); incr_top(L);
        f->source=luaS_newliteral(L,"=(" PROGNAME ")");
        f->maxstacksize=1;
        pc=2*n+1;
        f->code=luaM_newvector(L,pc,Instruction);
        f->sizecode=pc;
        f->p=luaM_newvector(L,n,Proto*);
        f->sizep=n;
        pc=0;
        for (i=0; i<n; i++)
        {
            f->p[i]=toproto(L,i-n-1);
            f->code[pc++]=CREATE_ABx(OP_CLOSURE,0,i);
            f->code[pc++]=CREATE_ABC(OP_CALL,0,1,1);
        }
        f->code[pc++]=CREATE_ABC(OP_RETURN,0,1,0);
        return f;
    }
}

/**
 * @brief 字节码写入器函数
 *
 * 用于luaU_dump函数的写入回调，将字节码数据写入文件。
 * 这是字节码输出的底层实现。
 *
 * @param L Lua状态机指针（未使用）
 * @param p 要写入的数据指针
 * @param size 数据大小（字节数）
 * @param u 用户数据（FILE*指针）
 * @return 0表示成功，非0表示失败
 *
 * @note L参数未使用，标记为UNUSED
 * @note 返回值遵循Lua写入器约定
 *
 * @see fwrite, luaU_dump
 *
 * 写入逻辑：
 * 1. 调用fwrite写入数据到文件
 * 2. 检查写入是否成功
 * 3. 特殊处理size为0的情况
 * 4. 返回适当的状态码
 *
 * 错误检测：
 * - fwrite返回值不等于1表示失败
 * - size为0时总是成功
 * - 组合条件确保正确的错误报告
 *
 * 使用场景：
 * - 字节码文件输出
 * - 二进制数据写入
 * - 流式数据处理
 *
 * 性能考虑：
 * - 直接使用fwrite提高效率
 * - 最小化错误检查开销
 * - 支持大块数据写入
 */
static int writer(lua_State* L, const void* p, size_t size, void* u)
{
    UNUSED(L);
    return (fwrite(p,size,1,(FILE*)u)!=1) && (size!=0);
}

/** @} */ /* 结束编译核心系统文档组 */

/**
 * @defgroup MainProgram 主程序和编译流程
 * @brief 编译器的主要执行流程和程序入口
 *
 * 主程序系统负责协调整个编译器的执行流程，包括文件加载、
 * 编译处理、字节码生成和输出等核心功能。
 * @{
 */

/**
 * @brief 主程序参数结构
 *
 * 用于在保护模式下传递主程序参数的结构体。
 * 通过lua_cpcall传递给pmain函数。
 */
struct Smain {
    int argc;       /**< 输入文件数量 */
    char** argv;    /**< 输入文件名数组 */
};

/**
 * @brief 保护模式下的主编译函数
 *
 * 在Lua保护模式下执行的主编译逻辑。处理所有可能抛出
 * Lua错误的编译操作，确保编译器的稳定性。
 *
 * @param L Lua状态机指针
 * @return 总是返回0（错误通过异常机制处理）
 *
 * @note 在保护模式下运行，捕获所有Lua错误
 * @note 通过Smain结构传递参数
 *
 * @see lua_cpcall, luaL_loadfile, combine, luaU_dump
 *
 * 编译流程：
 * 1. **参数获取**：
 *    - 从用户数据获取Smain结构
 *    - 提取文件数量和文件名数组
 *    - 检查栈空间是否足够
 *
 * 2. **文件加载**：
 *    - 遍历所有输入文件
 *    - 处理标准输入（文件名为"-"）
 *    - 调用luaL_loadfile编译每个文件
 *    - 编译后的函数推入栈中
 *
 * 3. **函数合并**：
 *    - 调用combine函数合并所有函数
 *    - 单文件直接使用，多文件创建包装函数
 *    - 生成最终的主函数原型
 *
 * 4. **字节码列出**：
 *    - 如果启用listing选项
 *    - 调用luaU_print输出字节码反汇编
 *    - 支持详细和简单两种模式
 *
 * 5. **字节码转储**：
 *    - 如果启用dumping选项
 *    - 打开输出文件或使用标准输出
 *    - 调用luaU_dump生成二进制字节码
 *    - 处理文件写入和关闭
 *
 * 错误处理：
 * - 栈空间不足：致命错误
 * - 文件加载失败：致命错误
 * - 文件操作失败：cannot错误
 * - 所有错误都会终止编译过程
 *
 * 文件处理：
 * - 支持多个输入文件
 * - 支持标准输入（"-"）
 * - 自动处理文件名和路径
 * - 统一的错误报告机制
 *
 * 输出控制：
 * - 字节码列出：可选的反汇编输出
 * - 字节码转储：二进制文件生成
 * - 调试信息：可选的剥离处理
 * - 灵活的输出目标选择
 *
 * 线程安全：
 * - 使用lua_lock/lua_unlock保护转储操作
 * - 确保多线程环境下的安全性
 * - 防止并发访问冲突
 */
static int pmain(lua_State* L)
{
    struct Smain* s = (struct Smain*)lua_touserdata(L, 1);
    int argc=s->argc;
    char** argv=s->argv;
    const Proto* f;
    int i;
    if (!lua_checkstack(L,argc)) fatal("too many input files");
    for (i=0; i<argc; i++)
    {
        const char* filename=IS("-") ? NULL : argv[i];
        if (luaL_loadfile(L,filename)!=0) fatal(lua_tostring(L,-1));
    }
    f=combine(L,argc);
    if (listing) luaU_print(f,listing>1);
    if (dumping)
    {
        FILE* D= (output==NULL) ? stdout : fopen(output,"wb");
        if (D==NULL) cannot("open");
        lua_lock(L);
        luaU_dump(L,f,writer,D,stripping);
        lua_unlock(L);
        if (ferror(D)) cannot("write");
        if (fclose(D)) cannot("close");
    }
    return 0;
}

/**
 * @brief 编译器主入口点
 *
 * Lua编译器的主函数，负责创建Lua状态机、处理命令行参数
 * 和调用主编译逻辑。
 *
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return EXIT_SUCCESS表示成功，EXIT_FAILURE表示失败
 *
 * @note 使用保护模式调用主编译逻辑
 * @note 确保Lua状态机的正确创建和销毁
 *
 * @see lua_open, lua_cpcall, lua_close, doargs, pmain
 *
 * 程序生命周期：
 * 1. **参数处理**：
 *    - 调用doargs解析命令行参数
 *    - 设置编译选项和配置
 *    - 获取输入文件列表
 *
 * 2. **输入验证**：
 *    - 检查是否有输入文件
 *    - 如果没有输入文件，显示使用帮助
 *    - 确保编译有意义的输入
 *
 * 3. **状态机创建**：
 *    - 调用lua_open创建新的Lua状态机
 *    - 如果创建失败，显示内存错误
 *    - 准备编译环境
 *
 * 4. **编译执行**：
 *    - 准备Smain参数结构
 *    - 使用lua_cpcall在保护模式下调用pmain
 *    - 捕获所有可能的编译错误
 *
 * 5. **资源清理**：
 *    - 关闭Lua状态机
 *    - 释放所有相关资源
 *    - 返回适当的退出码
 *
 * 错误处理：
 * - 参数错误：显示使用帮助并退出
 * - 内存不足：显示错误并退出
 * - 编译错误：显示错误并退出
 * - 所有错误都有相应的错误消息
 *
 * 参数调整：
 * - 跳过已处理的选项参数
 * - 调整argc和argv指向输入文件
 * - 简化后续的文件处理逻辑
 *
 * 退出码：
 * - EXIT_SUCCESS (0)：编译成功
 * - EXIT_FAILURE (1)：编译失败
 *
 * 内存管理：
 * - 自动管理Lua状态机的生命周期
 * - 确保在任何情况下都正确清理
 * - 防止内存泄漏和资源泄漏
 *
 * 异常安全：
 * - 使用保护模式防止程序崩溃
 * - 即使编译错误也能优雅退出
 * - 提供清晰的错误信息和诊断
 */
int main(int argc, char* argv[])
{
    lua_State* L;
    struct Smain s;
    int i=doargs(argc,argv);
    argc-=i; argv+=i;
    if (argc<=0) usage("no input files given");
    L=lua_open();
    if (L==NULL) fatal("not enough memory for state");
    s.argc=argc;
    s.argv=argv;
    if (lua_cpcall(L,pmain,&s)!=0) fatal(lua_tostring(L,-1));
    lua_close(L);
    return EXIT_SUCCESS;
}

/** @} */ /* 结束主程序和编译流程文档组 */
