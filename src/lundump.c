/*
** [核心] Lua 字节码反序列化(Undump)实现
**
** 功能概述：
** 本模块实现了Lua预编译字节码的加载和反序列化功能。能够从二进制流中
** 重建完整的Lua函数原型(Proto)对象，包括指令序列、常量表、调试信息等。
** 这是Lua支持预编译脚本的核心组件，与luac编译器生成的字节码格式兼容。
**
** 主要组件：
** - LoadState：反序列化状态管理结构
** - 数据类型加载：支持各种Lua数据类型的二进制读取
** - 函数原型重建：递归构建嵌套的函数结构
** - 头部验证：确保字节码格式和平台兼容性
** - 错误处理：检测损坏或不兼容的字节码
**
** 设计特点：
** - 平台兼容性：处理不同平台的字节序和数据大小差异
** - 版本控制：验证字节码版本与当前Lua版本的兼容性
** - 安全检查：防止恶意或损坏的字节码导致崩溃
** - 内存管理：正确处理加载过程中的内存分配和错误清理
** - 递归支持：处理嵌套函数和复杂的程序结构
**
** 支持的数据类型：
** - 基础类型：nil、boolean、number、string
** - 复合类型：function、table（作为常量）
** - 调试信息：行号映射、局部变量名、upvalue名
** - 指令序列：Lua虚拟机字节码指令
**
** 依赖模块：
** - lzio.c：缓冲I/O系统，提供流式数据读取
** - lfunc.c：函数对象管理，创建Proto结构
** - lstring.c：字符串管理，处理字符串常量
** - lmem.c：内存管理，动态数组分配
** - ldebug.c：调试支持，字节码验证
*/

#include <string.h>

#define lundump_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "lundump.h"
#include "lzio.h"


/*
** [内部] 反序列化状态结构
**
** 功能说明：
** 维护字节码加载过程中的所有状态信息，包括输入流、缓冲区、
** 错误上下文等。所有加载函数都通过此结构共享状态。
**
** 字段说明：
** - L：Lua状态机指针，用于内存分配和错误处理
** - Z：ZIO输入流，提供字节码数据源
** - b：内存缓冲区，用于临时存储变长数据
** - name：源文件名称，用于错误消息和调试信息
**
** 生命周期：
** 1. 在luaU_undump中初始化
** 2. 在整个加载过程中保持有效
** 3. 加载完成或出错时自动销毁
*/
typedef struct {
    lua_State* L;        /* Lua状态机 */
    ZIO* Z;              /* 输入数据流 */
    Mbuffer* b;          /* 临时缓冲区 */
    const char* name;    /* 源文件名称 */
} LoadState;


/*
** [安全] 条件编译：信任二进制代码模式
**
** 说明：
** 当定义LUAC_TRUST_BINARIES时，禁用所有安全检查，假设输入的
** 字节码是完全可信的。这可以提高加载性能，但会降低安全性。
** 生产环境中通常不建议启用此选项。
*/
#ifdef LUAC_TRUST_BINARIES
#define IF(c,s)         /* 禁用条件检查 */
#define error(S,s)      /* 禁用错误处理 */
#else
#define IF(c,s)         if (c) error(S,s)

/*
** [内部] 反序列化错误处理
**
** 详细功能说明：
** 当检测到字节码格式错误、数据损坏或不兼容时，生成详细的错误消息
** 并抛出语法错误异常。错误消息包含源文件名和具体的错误原因。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
** @param why - const char*：错误原因描述
**
** 错误处理流程：
** 1. 构造包含文件名和错误原因的详细消息
** 2. 将错误消息推入Lua栈
** 3. 抛出LUA_ERRSYNTAX类型的异常
** 4. 函数不会返回（通过longjmp跳转）
**
** 常见错误类型：
** - "unexpected end"：文件过早结束
** - "bad header"：文件头格式错误
** - "bad integer"：整数格式异常
** - "bad constant"：常量类型不支持
** - "bad code"：字节码验证失败
*/
static void error(LoadState* S, const char* why)
{
    /*
    ** [错误消息] 构造详细的错误描述
    ** 格式：源文件名: 错误原因 in precompiled chunk
    */
    luaO_pushfstring(S->L, "%s: %s in precompiled chunk", S->name, why);
    
    /*
    ** [异常抛出] 抛出语法错误异常
    */
    luaD_throw(S->L, LUA_ERRSYNTAX);
}
#endif


/*
** [宏定义] 数据加载便利宏
**
** 功能说明：
** 提供类型安全的数据加载宏，简化常见的数据读取操作。
** 这些宏在内部函数中广泛使用，提高代码的可读性和一致性。
**
** 宏定义说明：
** - LoadMem：加载指定大小的内存块
** - LoadByte：加载单个字节并转换为无符号类型
** - LoadVar：加载变量，自动计算大小
** - LoadVector：加载数组，指定元素数量和单元大小
*/
#define LoadMem(S,b,n,size)    LoadBlock(S,b,(n)*(size))
#define LoadByte(S)            (lu_byte)LoadChar(S)
#define LoadVar(S,x)           LoadMem(S,&x,1,sizeof(x))
#define LoadVector(S,b,n,size) LoadMem(S,b,n,size)


/*
** [内部] 加载原始内存块
**
** 详细功能说明：
** 从输入流中读取指定大小的原始数据到内存缓冲区。这是所有其他
** 加载函数的基础，提供底层的数据读取和错误检查功能。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
** @param b - void*：目标缓冲区指针
** @param size - size_t：要读取的字节数
**
** 返回值：无（出错时抛出异常）
**
** 算法复杂度：O(n) 时间，其中n是数据大小
**
** 错误检查：
** - 验证是否读取了预期的字节数
** - 检测文件过早结束的情况
** - 处理I/O错误和数据流异常
**
** 注意事项：
** - 函数可能不会返回（在错误情况下抛出异常）
** - 调用者必须确保缓冲区有足够的空间
** - 用于所有类型数据的底层读取操作
*/
static void LoadBlock(LoadState* S, void* b, size_t size)
{
    /*
    ** [数据读取] 从ZIO流中读取指定大小的数据
    */
    size_t r = luaZ_read(S->Z, b, size);
    
    /*
    ** [完整性检查] 验证是否读取了完整的数据
    ** r != 0 表示有部分数据未能读取
    */
    IF(r != 0, "unexpected end");
}


/*
** [内部] 加载单个字符
**
** 详细功能说明：
** 从字节码流中读取一个字符（8位有符号整数）。主要用于加载
** 类型标识符、标志位和其他单字节数据。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
**
** 返回值：
** @return int：读取的字符值（-128到127）
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用场景：
** - 加载数据类型标识符
** - 读取布尔值和标志位
** - 获取枚举值和小整数
*/
static int LoadChar(LoadState* S)
{
    char x;
    LoadVar(S, x);
    return x;
}


/*
** [内部] 加载整数
**
** 详细功能说明：
** 从字节码流中读取一个完整的整数值。在Lua字节码格式中，
** 整数用于表示数组大小、索引值、行号等。包含负数检查以
** 防止恶意数据导致的安全问题。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
**
** 返回值：
** @return int：读取的非负整数值
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 安全检查：
** - 验证整数值非负（防止数组大小为负）
** - 检测数据格式错误和恶意输入
**
** 使用场景：
** - 数组和向量的大小信息
** - 函数参数数量和upvalue数量
** - 行号和程序计数器值
*/
static int LoadInt(LoadState* S)
{
    int x;
    LoadVar(S, x);
    
    /*
    ** [安全检查] 确保整数值为非负数
    ** 负数通常表示数据损坏或格式错误
    */
    IF(x < 0, "bad integer");
    
    return x;
}


/*
** [内部] 加载Lua数值
**
** 详细功能说明：
** 从字节码流中读取一个Lua数值（lua_Number类型）。数值是Lua中
** 数值常量的表示形式，通常为双精度浮点数。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
**
** 返回值：
** @return lua_Number：读取的数值
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 平台兼容性：
** - 处理不同平台的浮点数格式差异
** - 支持IEEE 754标准的双精度浮点数
** - 保持数值精度和特殊值（NaN、Infinity）
**
** 使用场景：
** - 加载数值常量到常量表
** - 重建编译时计算的数值结果
*/
static lua_Number LoadNumber(LoadState* S)
{
    lua_Number x;
    LoadVar(S, x);
    return x;
}


/*
** [内部] 加载字符串
**
** 详细功能说明：
** 从字节码流中读取Lua字符串常量。字符串在字节码中以长度前缀的
** 形式存储，支持包含空字符的二进制字符串。处理字符串的intern化
** 以确保内存效率和比较性能。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
**
** 返回值：
** @return TString*：创建的字符串对象，或NULL表示空字符串
**
** 算法复杂度：O(n) 时间，其中n是字符串长度
**
** 字符串格式：
** 1. 读取字符串长度（size_t类型）
** 2. 长度为0表示NULL字符串
** 3. 长度非0时读取字符串内容（包含尾部'\0'）
** 4. 创建字符串对象时去除尾部'\0'
**
** 内存管理：
** - 使用临时缓冲区避免重复分配
** - 通过luaS_newlstr创建intern化字符串
** - 自动处理垃圾回收和引用计数
**
** 注意事项：
** - 字节码中的字符串包含尾部空字符
** - 创建字符串对象时需要减去1个字符的长度
** - 返回NULL表示原始字符串为NULL（非空字符串）
*/
static TString* LoadString(LoadState* S)
{
    size_t size;
    LoadVar(S, size);
    
    /*
    ** [空字符串处理] 长度为0表示NULL字符串
    */
    if (size == 0)
    {
        return NULL;
    }
    else
    {
        /*
        ** [缓冲区准备] 分配临时空间存储字符串内容
        */
        char* s = luaZ_openspace(S->L, S->b, size);
        
        /*
        ** [数据读取] 加载完整的字符串内容
        */
        LoadBlock(S, s, size);
        
        /*
        ** [字符串创建] 创建intern化的字符串对象
        ** 长度减1是为了去除尾部的'\0'字符
        */
        return luaS_newlstr(S->L, s, size - 1);
    }
}


/*
** [内部] 加载函数代码
**
** 详细功能说明：
** 从字节码流中加载Lua函数的指令序列。指令是Lua虚拟机的基本
** 执行单元，包含操作码和操作数。正确加载指令序列对函数执行
** 至关重要。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
** @param f - Proto*：目标函数原型对象
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是指令数量
**
** 加载流程：
** 1. 读取指令数量
** 2. 分配指令数组内存
** 3. 批量读取所有指令
** 4. 更新函数原型的指令信息
**
** 内存管理：
** - 使用luaM_newvector分配指令数组
** - 自动处理内存分配失败的情况
** - 指令数组生命周期与函数原型绑定
**
** 数据格式：
** - 指令数量：32位整数
** - 指令序列：连续的Instruction数组
** - 每个指令包含操作码和操作数
*/
static void LoadCode(LoadState* S, Proto* f)
{
    /*
    ** [大小读取] 获取指令数量
    */
    int n = LoadInt(S);
    
    /*
    ** [内存分配] 为指令数组分配内存
    */
    f->code = luaM_newvector(S->L, n, Instruction);
    f->sizecode = n;
    
    /*
    ** [批量加载] 读取所有指令到内存
    */
    LoadVector(S, f->code, n, sizeof(Instruction));
}


/*
** [前向声明] 递归函数加载声明
**
** 说明：
** 由于LoadConstants需要调用LoadFunction来处理嵌套函数，
** 而LoadFunction又需要调用LoadConstants，因此需要前向声明。
*/
static Proto* LoadFunction(LoadState* S, TString* p);


/*
** [内部] 加载常量表
**
** 详细功能说明：
** 从字节码流中加载函数的常量表，包括各种类型的Lua值和嵌套函数。
** 常量表是函数执行时引用的只读数据，包含数值、字符串、布尔值等。
** 还需要递归加载嵌套的函数原型。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
** @param f - Proto*：目标函数原型对象
**
** 返回值：无
**
** 算法复杂度：O(n + m) 时间，其中n是常量数量，m是嵌套函数数量
**
** 加载流程：
** 1. 加载基础常量表（nil、boolean、number、string）
** 2. 初始化所有常量为nil
** 3. 逐个加载并设置每个常量的值
** 4. 加载嵌套函数原型表
** 5. 递归加载每个嵌套函数
**
** 支持的常量类型：
** - LUA_TNIL：空值常量
** - LUA_TBOOLEAN：布尔值常量
** - LUA_TNUMBER：数值常量
** - LUA_TSTRING：字符串常量
**
** 内存管理：
** - 分配常量数组和函数原型数组
** - 初始化为安全状态以支持垃圾回收
** - 递归处理嵌套函数的内存分配
*/
static void LoadConstants(LoadState* S, Proto* f)
{
    int i, n;
    
    /*
    ** [常量表加载] 处理基础常量
    */
    n = LoadInt(S);
    f->k = luaM_newvector(S->L, n, TValue);
    f->sizek = n;
    
    /*
    ** [初始化] 将所有常量初始化为nil
    ** 这确保了垃圾回收器的安全性
    */
    for (i = 0; i < n; i++) 
    {
        setnilvalue(&f->k[i]);
    }
    
    /*
    ** [常量加载] 逐个加载每个常量值
    */
    for (i = 0; i < n; i++)
    {
        TValue* o = &f->k[i];
        int t = LoadChar(S);
        
        /*
        ** [类型分发] 根据类型标识符加载对应的值
        */
        switch (t)
        {
            case LUA_TNIL:
            {
                setnilvalue(o);
                break;
            }
            
            case LUA_TBOOLEAN:
            {
                setbvalue(o, LoadChar(S) != 0);
                break;
            }
            
            case LUA_TNUMBER:
            {
                setnvalue(o, LoadNumber(S));
                break;
            }
            
            case LUA_TSTRING:
            {
                setsvalue2n(S->L, o, LoadString(S));
                break;
            }
            
            default:
            {
                error(S, "bad constant");
                break;
            }
        }
    }
    
    /*
    ** [嵌套函数加载] 处理函数原型表
    */
    n = LoadInt(S);
    f->p = luaM_newvector(S->L, n, Proto*);
    f->sizep = n;
    
    /*
    ** [初始化] 将所有函数指针初始化为NULL
    */
    for (i = 0; i < n; i++) 
    {
        f->p[i] = NULL;
    }
    
    /*
    ** [递归加载] 加载每个嵌套函数
    */
    for (i = 0; i < n; i++) 
    {
        f->p[i] = LoadFunction(S, f->source);
    }
}


/*
** [内部] 加载调试信息
**
** 详细功能说明：
** 从字节码流中加载函数的调试信息，包括行号映射、局部变量名称
** 和upvalue名称。这些信息用于错误报告、调试器支持和反射操作。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
** @param f - Proto*：目标函数原型对象
**
** 返回值：无
**
** 算法复杂度：O(l + v + u) 时间，其中l是行号数量，v是局部变量数量，u是upvalue数量
**
** 调试信息组成：
** 1. 行号信息：指令到源代码行的映射
** 2. 局部变量：变量名、作用域起始和结束位置
** 3. Upvalue名称：闭包中引用的外部变量名
**
** 数据结构：
** - lineinfo：整数数组，存储每条指令对应的行号
** - locvars：LocVar数组，存储局部变量信息
** - upvalues：字符串数组，存储upvalue名称
**
** 内存管理：
** - 分配各种调试信息数组
** - 初始化指针为安全状态
** - 字符串通过LoadString自动管理
*/
static void LoadDebug(LoadState* S, Proto* f)
{
    int i, n;
    
    /*
    ** [行号信息] 加载指令行号映射
    */
    n = LoadInt(S);
    f->lineinfo = luaM_newvector(S->L, n, int);
    f->sizelineinfo = n;
    LoadVector(S, f->lineinfo, n, sizeof(int));
    
    /*
    ** [局部变量] 加载局部变量调试信息
    */
    n = LoadInt(S);
    f->locvars = luaM_newvector(S->L, n, LocVar);
    f->sizelocvars = n;
    
    /*
    ** [初始化] 初始化变量名指针为NULL
    */
    for (i = 0; i < n; i++) 
    {
        f->locvars[i].varname = NULL;
    }
    
    /*
    ** [变量信息] 加载每个局部变量的详细信息
    */
    for (i = 0; i < n; i++)
    {
        f->locvars[i].varname = LoadString(S);   /* 变量名 */
        f->locvars[i].startpc = LoadInt(S);      /* 作用域开始 */
        f->locvars[i].endpc = LoadInt(S);        /* 作用域结束 */
    }
    
    /*
    ** [Upvalue名称] 加载upvalue调试信息
    */
    n = LoadInt(S);
    f->upvalues = luaM_newvector(S->L, n, TString*);
    f->sizeupvalues = n;
    
    /*
    ** [初始化] 初始化upvalue名称指针为NULL
    */
    for (i = 0; i < n; i++) 
    {
        f->upvalues[i] = NULL;
    }
    
    /*
    ** [名称加载] 加载每个upvalue的名称
    */
    for (i = 0; i < n; i++) 
    {
        f->upvalues[i] = LoadString(S);
    }
}


/*
** [内部] 加载函数原型
**
** 详细功能说明：
** 这是字节码加载的核心函数，负责重建完整的Lua函数原型对象。
** 包括函数的所有组成部分：基本属性、指令代码、常量表、调试信息。
** 支持递归加载嵌套函数，并进行必要的安全检查。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
** @param p - TString*：父函数的源文件名
**
** 返回值：
** @return Proto*：完整构建的函数原型对象
**
** 算法复杂度：O(n) 时间，其中n是函数总大小（指令+常量+调试信息）
**
** 加载流程：
** 1. 递归深度检查（防止栈溢出）
** 2. 创建新的函数原型对象
** 3. 保护对象免受垃圾回收
** 4. 加载函数基本属性
** 5. 加载指令、常量、调试信息
** 6. 字节码验证
** 7. 清理保护并返回
**
** 安全机制：
** - 递归深度限制防止栈溢出
** - 垃圾回收保护确保对象安全
** - 字节码验证检测格式错误
** - 异常安全的资源管理
**
** 函数属性：
** - source：源文件名
** - linedefined：函数定义开始行
** - lastlinedefined：函数定义结束行
** - nups：upvalue数量
** - numparams：参数数量
** - is_vararg：是否为可变参数函数
** - maxstacksize：最大栈大小
*/
static Proto* LoadFunction(LoadState* S, TString* p)
{
    Proto* f;
    
    /*
    ** [递归检查] 防止函数嵌套过深导致栈溢出
    */
    if (++S->L->nCcalls > LUAI_MAXCCALLS) 
    {
        error(S, "code too deep");
    }
    
    /*
    ** [对象创建] 创建新的函数原型对象
    */
    f = luaF_newproto(S->L);
    
    /*
    ** [GC保护] 将对象放入栈中，防止被垃圾回收
    */
    setptvalue2s(S->L, S->L->top, f);
    incr_top(S->L);
    
    /*
    ** [基本属性] 加载函数的基本属性
    */
    f->source = LoadString(S);
    if (f->source == NULL) 
    {
        f->source = p;  /* 继承父函数的源文件名 */
    }
    
    f->linedefined = LoadInt(S);           /* 定义开始行 */
    f->lastlinedefined = LoadInt(S);       /* 定义结束行 */
    f->nups = LoadByte(S);                 /* upvalue数量 */
    f->numparams = LoadByte(S);            /* 参数数量 */
    f->is_vararg = LoadByte(S);            /* 可变参数标志 */
    f->maxstacksize = LoadByte(S);         /* 最大栈大小 */
    
    /*
    ** [组件加载] 加载函数的各个组成部分
    */
    LoadCode(S, f);        /* 指令序列 */
    LoadConstants(S, f);   /* 常量表 */
    LoadDebug(S, f);       /* 调试信息 */
    
    /*
    ** [字节码验证] 验证生成的字节码是否有效
    */
    IF(!luaG_checkcode(f), "bad code");
    
    /*
    ** [清理保护] 移除栈保护，减少递归计数
    */
    S->L->top--;
    S->L->nCcalls--;
    
    return f;
}


/*
** [内部] 加载并验证字节码文件头
**
** 详细功能说明：
** 验证字节码文件的头部信息，确保文件格式正确且与当前Lua版本
** 和平台兼容。头部包含版本信息、格式标识、平台特征等。
**
** 参数说明：
** @param S - LoadState*：加载状态对象
**
** 返回值：无（失败时抛出异常）
**
** 验证内容：
** - Lua签名：确认是有效的Lua字节码文件
** - 版本号：检查字节码版本与解释器版本的兼容性
** - 格式标识：验证字节码格式版本
** - 平台信息：字节序、数据类型大小等
**
** 兼容性检查：
** - 字节序（大端/小端）
** - 基础数据类型大小
** - 指令格式和数值类型
** - Lua版本兼容性
*/
static void LoadHeader(LoadState* S)
{
    char h[LUAC_HEADERSIZE];
    char s[LUAC_HEADERSIZE];
    
    /*
    ** [标准头生成] 生成当前平台的标准头部
    */
    luaU_header(h);
    
    /*
    ** [头部读取] 从字节码文件读取头部
    */
    LoadBlock(S, s, LUAC_HEADERSIZE);
    
    /*
    ** [兼容性验证] 比较文件头与标准头
    */
    IF(memcmp(h, s, LUAC_HEADERSIZE) != 0, "bad header");
}


/*
** [公共] 加载预编译的Lua代码块
**
** 详细功能说明：
** 这是字节码加载的主要入口点，负责从输入流中加载完整的预编译
** Lua代码块。处理文件名解析、状态初始化、头部验证和函数加载。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param Z - ZIO*：输入数据流
** @param buff - Mbuffer*：临时缓冲区
** @param name - const char*：源文件名称
**
** 返回值：
** @return Proto*：加载的主函数原型对象
**
** 算法复杂度：O(n) 时间，其中n是字节码总大小
**
** 文件名处理：
** - "@filename"：文件路径名（去除@前缀）
** - "=name"：显式名称（去除=前缀）
** - 二进制标识：识别为"binary string"
** - 其他：直接使用原名称
**
** 加载流程：
** 1. 解析和处理源文件名
** 2. 初始化加载状态
** 3. 验证字节码文件头
** 4. 加载主函数原型
** 5. 返回完整的函数对象
**
** 错误处理：
** - 文件格式错误
** - 版本不兼容
** - 数据损坏
** - 内存分配失败
*/
Proto* luaU_undump(lua_State* L, ZIO* Z, Mbuffer* buff, const char* name)
{
    LoadState S;
    
    /*
    ** [文件名处理] 解析源文件名称格式
    */
    if (*name == '@' || *name == '=')
    {
        S.name = name + 1;  /* 去除前缀字符 */
    }
    else if (*name == LUA_SIGNATURE[0])
    {
        S.name = "binary string";  /* 二进制字符串 */
    }
    else
    {
        S.name = name;  /* 直接使用原名称 */
    }
    
    /*
    ** [状态初始化] 设置加载状态
    */
    S.L = L;
    S.Z = Z;
    S.b = buff;
    
    /*
    ** [头部验证] 验证字节码文件头
    */
    LoadHeader(&S);
    
    /*
    ** [主函数加载] 加载并返回主函数原型
    */
    return LoadFunction(&S, luaS_newliteral(L, "=?"));
}


/*
** [公共] 生成字节码文件头
**
** 详细功能说明：
** 生成Lua字节码文件的标准头部，包含所有必要的平台和版本信息。
** 这个头部用于验证字节码文件的兼容性和完整性。
**
** 参数说明：
** @param h - char*：输出缓冲区（至少LUAC_HEADERSIZE字节）
**
** 返回值：无
**
** 头部结构：
** 1. Lua签名：识别文件类型
** 2. 版本号：字节码版本
** 3. 格式号：字节码格式版本
** 4. 字节序：平台字节序标识
** 5. int大小：平台int类型大小
** 6. size_t大小：平台size_t类型大小
** 7. 指令大小：虚拟机指令大小
** 8. 数值大小：lua_Number类型大小
** 9. 数值类型：数值是否为整型
**
** 平台检测：
** - 使用小端序测试检测字节序
** - 通过sizeof获取数据类型大小
** - 检测数值类型的表示方式
*/
void luaU_header(char* h)
{
    int x = 1;
    
    /*
    ** [签名复制] 写入Lua文件签名
    */
    memcpy(h, LUA_SIGNATURE, sizeof(LUA_SIGNATURE) - 1);
    h += sizeof(LUA_SIGNATURE) - 1;
    
    /*
    ** [版本信息] 写入版本和格式号
    */
    *h++ = (char)LUAC_VERSION;    /* 字节码版本 */
    *h++ = (char)LUAC_FORMAT;     /* 字节码格式 */
    
    /*
    ** [平台信息] 写入平台特征
    */
    *h++ = (char)*(char*)&x;              /* 字节序检测 */
    *h++ = (char)sizeof(int);             /* int大小 */
    *h++ = (char)sizeof(size_t);          /* size_t大小 */
    *h++ = (char)sizeof(Instruction);     /* 指令大小 */
    *h++ = (char)sizeof(lua_Number);      /* 数值大小 */
    
    /*
    ** [数值类型] 检测lua_Number是否为整型
    ** 测试0.5是否等于0（整型会截断为0）
    */
    *h++ = (char)(((lua_Number)0.5) == 0);
}