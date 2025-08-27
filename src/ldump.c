/*
** [核心] Lua 字节码序列化(Dump)实现
**
** 功能概述：
** 本模块实现了Lua函数原型到预编译字节码的序列化功能。能够将内存中的
** Lua函数对象(Proto)转换为二进制字节码格式，用于保存预编译的Lua脚本。
** 这是luac编译器和load/loadfile函数的核心组件，与lundump.c形成完整的
** 序列化/反序列化对。
**
** 主要组件：
** - DumpState：序列化状态管理结构
** - 数据类型输出：支持各种Lua数据类型的二进制写入
** - 函数原型序列化：递归输出嵌套的函数结构
** - 调试信息控制：可选的调试信息剥离功能
** - 错误处理：检测和报告序列化过程中的错误
**
** 设计特点：
** - 平台无关性：生成的字节码在不同平台间兼容
** - 压缩选项：支持剥离调试信息以减小文件大小
** - 流式输出：通过回调函数支持各种输出目标
** - 错误恢复：序列化失败时保持状态一致性
** - 递归安全：正确处理嵌套函数和循环引用
**
** 输出格式：
** - 文件头：版本信息和平台特征
** - 函数原型：函数的完整定义
** - 指令序列：虚拟机字节码指令
** - 常量表：数值、字符串等常量数据
** - 调试信息：行号、变量名等调试数据（可选）
**
** 依赖模块：
** - lobject.c：基础对象系统，访问Lua值结构
** - lstate.c：全局状态管理，线程安全控制
** - lundump.c：共享头部格式定义
*/

#include <stddef.h>

#define ldump_c
#define LUA_CORE

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"


/*
** [内部] 序列化状态结构
**
** 功能说明：
** 维护字节码输出过程中的所有状态信息，包括输出回调、错误状态、
** 调试信息控制等。所有序列化函数都通过此结构共享状态。
**
** 字段说明：
** - L：Lua状态机指针，用于线程安全和错误处理
** - writer：用户提供的输出回调函数
** - data：传递给writer函数的用户数据
** - strip：调试信息剥离标志（1=剥离，0=保留）
** - status：当前序列化状态（0=成功，非0=错误码）
**
** 生命周期：
** 1. 在luaU_dump中初始化
** 2. 在整个序列化过程中保持有效
** 3. 序列化完成或出错时返回状态
**
** 错误处理：
** - status字段记录第一个错误
** - 后续操作在错误状态下被跳过
** - 保证序列化的原子性
*/
typedef struct {
    lua_State* L;        /* Lua状态机 */
    lua_Writer writer;   /* 输出回调函数 */
    void* data;          /* 用户数据 */
    int strip;           /* 调试信息剥离标志 */
    int status;          /* 序列化状态 */
} DumpState;


/*
** [宏定义] 数据输出便利宏
**
** 功能说明：
** 提供类型安全的数据输出宏，简化常见的序列化操作。
** 这些宏在内部函数中广泛使用，提高代码的可读性和一致性。
**
** 宏定义说明：
** - DumpMem：输出指定大小的内存块
** - DumpVar：输出变量，自动计算大小
*/
#define DumpMem(b,n,size,D)    DumpBlock(b,(n)*(size),D)
#define DumpVar(x,D)           DumpMem(&x,1,sizeof(x),D)


/*
** [内部] 输出原始内存块
**
** 详细功能说明：
** 将指定大小的原始数据通过writer回调函数输出到目标。这是所有其他
** 输出函数的基础，提供底层的数据写入和错误处理功能。实现了线程
** 安全的输出操作和错误状态管理。
**
** 参数说明：
** @param b - const void*：要输出的数据缓冲区
** @param size - size_t：要输出的字节数
** @param D - DumpState*：序列化状态对象
**
** 返回值：无（错误通过D->status记录）
**
** 算法复杂度：O(n) 时间，其中n是数据大小
**
** 线程安全：
** - 在调用writer前解锁Lua状态机
** - 允许writer函数执行可能阻塞的I/O操作
** - 调用完成后重新锁定状态机
**
** 错误处理：
** - 只在当前状态正常时执行输出
** - 记录writer函数返回的错误状态
** - 错误状态具有粘性（一旦出错就保持错误）
**
** 注意事项：
** - writer函数的返回值：0=成功，非0=错误
** - 函数不会抛出异常，错误通过status字段传播
** - 用于所有类型数据的底层输出操作
*/
static void DumpBlock(const void* b, size_t size, DumpState* D)
{
    /*
    ** [状态检查] 只在无错误时继续操作
    */
    if (D->status == 0)
    {
        /*
        ** [线程安全] 解锁状态机允许I/O操作
        ** writer函数可能执行阻塞的文件或网络I/O
        */
        lua_unlock(D->L);
        
        /*
        ** [数据输出] 调用用户提供的writer函数
        */
        D->status = (*D->writer)(D->L, b, size, D->data);
        
        /*
        ** [状态恢复] 重新锁定状态机
        */
        lua_lock(D->L);
    }
}


/*
** [内部] 输出单个字符
**
** 详细功能说明：
** 将一个整数值作为字符（8位有符号整数）输出到字节码流。主要用于
** 输出类型标识符、标志位和其他单字节数据。
**
** 参数说明：
** @param y - int：要输出的字符值
** @param D - DumpState*：序列化状态对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 类型转换：
** - 将int安全转换为char
** - 处理值的截断和符号扩展
**
** 使用场景：
** - 输出数据类型标识符
** - 写入布尔值和标志位
** - 保存枚举值和小整数
*/
static void DumpChar(int y, DumpState* D)
{
    char x = (char)y;
    DumpVar(x, D);
}


/*
** [内部] 输出整数
**
** 详细功能说明：
** 将一个完整的整数值输出到字节码流。在Lua字节码格式中，
** 整数用于表示数组大小、索引值、行号等结构化信息。
**
** 参数说明：
** @param x - int：要输出的整数值
** @param D - DumpState*：序列化状态对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 数据格式：
** - 使用平台本地的int格式
** - 字节序由文件头信息标识
** - 保持与lundump.c的加载格式一致
**
** 使用场景：
** - 数组和向量的大小信息
** - 函数参数数量和upvalue数量
** - 行号和程序计数器值
*/
static void DumpInt(int x, DumpState* D)
{
    DumpVar(x, D);
}


/*
** [内部] 输出Lua数值
**
** 详细功能说明：
** 将一个Lua数值（lua_Number类型）输出到字节码流。数值是Lua中
** 数值常量的二进制表示，通常为双精度浮点数。
**
** 参数说明：
** @param x - lua_Number：要输出的数值
** @param D - DumpState*：序列化状态对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 平台兼容性：
** - 使用平台本地的浮点数格式
** - 保持IEEE 754标准的精度和特殊值
** - 字节序信息在文件头中标识
**
** 使用场景：
** - 输出数值常量到常量表
** - 保存编译时计算的数值结果
*/
static void DumpNumber(lua_Number x, DumpState* D)
{
    DumpVar(x, D);
}


/*
** [内部] 输出向量数据
**
** 详细功能说明：
** 输出一个数组或向量的完整内容，包括元素数量和所有元素数据。
** 这是序列化数组类型数据的标准模式。
**
** 参数说明：
** @param b - const void*：要输出的数组数据
** @param n - int：数组元素数量
** @param size - size_t：单个元素的大小
** @param D - DumpState*：序列化状态对象
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是数组大小
**
** 输出格式：
** 1. 首先输出元素数量（int类型）
** 2. 然后输出所有元素的原始数据
**
** 使用场景：
** - 输出指令数组
** - 输出行号信息数组
** - 输出常量数组
*/
static void DumpVector(const void* b, int n, size_t size, DumpState* D)
{
    /*
    ** [元素数量] 首先输出数组大小
    */
    DumpInt(n, D);
    
    /*
    ** [数组数据] 批量输出所有元素
    */
    DumpMem(b, n, size, D);
}


/*
** [内部] 输出字符串
**
** 详细功能说明：
** 将Lua字符串常量输出到字节码流。字符串在字节码中以长度前缀的
** 形式存储，支持包含空字符的二进制字符串。处理NULL字符串的
** 特殊情况。
**
** 参数说明：
** @param s - const TString*：要输出的字符串对象
** @param D - DumpState*：序列化状态对象
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是字符串长度
**
** 字符串格式：
** 1. NULL字符串：输出长度0
** 2. 非NULL字符串：输出长度+1，然后输出字符串内容+'\0'
**
** 特殊处理：
** - NULL指针被序列化为长度0
** - 字符串内容包含尾部空字符
** - 长度包含尾部空字符（+1）
**
** 注意事项：
** - 输出的长度比实际字符串长度多1（包含'\0'）
** - 与lundump.c的加载格式严格对应
** - 支持空字符串和包含空字符的字符串
*/
static void DumpString(const TString* s, DumpState* D)
{
    /*
    ** [NULL检查] 处理NULL字符串的特殊情况
    */
    if (s == NULL || getstr(s) == NULL)
    {
        /*
        ** [空字符串] 输出长度0表示NULL字符串
        */
        size_t size = 0;
        DumpVar(size, D);
    }
    else
    {
        /*
        ** [字符串长度] 计算包含尾部'\0'的完整长度
        */
        size_t size = s->tsv.len + 1;
        
        /*
        ** [长度输出] 首先输出字符串长度
        */
        DumpVar(size, D);
        
        /*
        ** [内容输出] 输出字符串内容（包含尾部'\0'）
        */
        DumpBlock(getstr(s), size, D);
    }
}


/*
** [宏定义] 输出函数代码
**
** 功能说明：
** 使用DumpVector宏输出函数的指令序列。这是一个便利宏，
** 简化了指令数组的序列化操作。
*/
#define DumpCode(f,D)    DumpVector(f->code,f->sizecode,sizeof(Instruction),D)


/*
** [前向声明] 递归函数序列化声明
**
** 说明：
** 由于DumpConstants需要调用DumpFunction来处理嵌套函数，
** 而DumpFunction又需要调用DumpConstants，因此需要前向声明。
*/
static void DumpFunction(const Proto* f, const TString* p, DumpState* D);


/*
** [内部] 输出常量表
**
** 详细功能说明：
** 将函数的常量表输出到字节码流，包括各种类型的Lua值和嵌套函数。
** 常量表包含函数执行时需要的只读数据，如数值、字符串、布尔值等。
** 同时递归输出嵌套的函数原型。
**
** 参数说明：
** @param f - const Proto*：要序列化的函数原型
** @param D - DumpState*：序列化状态对象
**
** 返回值：无
**
** 算法复杂度：O(n + m) 时间，其中n是常量数量，m是嵌套函数数量
**
** 输出流程：
** 1. 输出基础常量表（nil、boolean、number、string）
** 2. 逐个输出每个常量的类型和值
** 3. 输出嵌套函数数量
** 4. 递归输出每个嵌套函数
**
** 支持的常量类型：
** - LUA_TNIL：空值常量
** - LUA_TBOOLEAN：布尔值常量
** - LUA_TNUMBER：数值常量
** - LUA_TSTRING：字符串常量
**
** 错误处理：
** - 遇到不支持的常量类型时断言失败
** - 在调试模式下检测数据一致性
** - 递归输出中的错误会向上传播
*/
static void DumpConstants(const Proto* f, DumpState* D)
{
    int i, n = f->sizek;
    
    /*
    ** [常量数量] 输出基础常量表大小
    */
    DumpInt(n, D);
    
    /*
    ** [常量输出] 逐个输出每个常量
    */
    for (i = 0; i < n; i++)
    {
        const TValue* o = &f->k[i];
        
        /*
        ** [类型标识] 首先输出常量的类型
        */
        DumpChar(ttype(o), D);
        
        /*
        ** [值输出] 根据类型输出对应的值
        */
        switch (ttype(o))
        {
            case LUA_TNIL:
            {
                /* nil值不需要额外数据 */
                break;
            }
            
            case LUA_TBOOLEAN:
            {
                DumpChar(bvalue(o), D);
                break;
            }
            
            case LUA_TNUMBER:
            {
                DumpNumber(nvalue(o), D);
                break;
            }
            
            case LUA_TSTRING:
            {
                DumpString(rawtsvalue(o), D);
                break;
            }
            
            default:
            {
                /*
                ** [异常情况] 不应该出现的常量类型
                ** 在调试模式下断言失败
                */
                lua_assert(0);
                break;
            }
        }
    }
    
    /*
    ** [嵌套函数] 输出函数原型表
    */
    n = f->sizep;
    DumpInt(n, D);
    
    /*
    ** [递归输出] 输出每个嵌套函数
    */
    for (i = 0; i < n; i++) 
    {
        DumpFunction(f->p[i], f->source, D);
    }
}


/*
** [内部] 输出调试信息
**
** 详细功能说明：
** 将函数的调试信息输出到字节码流，包括行号映射、局部变量名称
** 和upvalue名称。支持调试信息剥离选项，可以生成不包含调试信息
** 的紧凑字节码。
**
** 参数说明：
** @param f - const Proto*：要序列化的函数原型
** @param D - DumpState*：序列化状态对象
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
** 剥离控制：
** - strip=1：输出空的调试信息（数量为0）
** - strip=0：输出完整的调试信息
** - 可大幅减小字节码文件大小
**
** 输出格式：
** - 每种调试信息都先输出数量，再输出具体内容
** - 局部变量包含名称和作用域范围
** - 所有字符串通过DumpString统一处理
*/
static void DumpDebug(const Proto* f, DumpState* D)
{
    int i, n;
    
    /*
    ** [行号信息] 输出指令行号映射
    ** 剥离模式下输出空数组
    */
    n = (D->strip) ? 0 : f->sizelineinfo;
    DumpVector(f->lineinfo, n, sizeof(int), D);
    
    /*
    ** [局部变量] 输出局部变量调试信息
    */
    n = (D->strip) ? 0 : f->sizelocvars;
    DumpInt(n, D);
    
    /*
    ** [变量详情] 输出每个局部变量的详细信息
    */
    for (i = 0; i < n; i++)
    {
        DumpString(f->locvars[i].varname, D);   /* 变量名 */
        DumpInt(f->locvars[i].startpc, D);      /* 作用域开始 */
        DumpInt(f->locvars[i].endpc, D);        /* 作用域结束 */
    }
    
    /*
    ** [Upvalue名称] 输出upvalue调试信息
    */
    n = (D->strip) ? 0 : f->sizeupvalues;
    DumpInt(n, D);
    
    /*
    ** [名称输出] 输出每个upvalue的名称
    */
    for (i = 0; i < n; i++) 
    {
        DumpString(f->upvalues[i], D);
    }
}


/*
** [内部] 输出函数原型
**
** 详细功能说明：
** 这是字节码序列化的核心函数，负责输出完整的Lua函数原型对象。
** 包括函数的所有组成部分：基本属性、指令代码、常量表、调试信息。
** 支持递归输出嵌套函数，并处理源文件名的优化。
**
** 参数说明：
** @param f - const Proto*：要序列化的函数原型
** @param p - const TString*：父函数的源文件名
** @param D - DumpState*：序列化状态对象
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是函数总大小（指令+常量+调试信息）
**
** 输出流程：
** 1. 输出源文件名（优化处理）
** 2. 输出函数基本属性
** 3. 输出指令、常量、调试信息
**
** 源文件名优化：
** - 如果与父函数相同，输出NULL节省空间
** - 剥离模式下总是输出NULL
** - 否则输出完整的源文件名
**
** 函数属性：
** - source：源文件名（可能为NULL）
** - linedefined：函数定义开始行
** - lastlinedefined：函数定义结束行
** - nups：upvalue数量
** - numparams：参数数量
** - is_vararg：是否为可变参数函数
** - maxstacksize：最大栈大小
**
** 递归处理：
** - 通过DumpConstants递归处理嵌套函数
** - 保持函数树的完整结构
** - 错误状态在递归中传播
*/
static void DumpFunction(const Proto* f, const TString* p, DumpState* D)
{
    /*
    ** [源文件名] 输出源文件名（带优化）
    ** 如果与父函数相同或剥离模式，输出NULL
    */
    DumpString((f->source == p || D->strip) ? NULL : f->source, D);
    
    /*
    ** [基本属性] 输出函数的基本属性
    */
    DumpInt(f->linedefined, D);           /* 定义开始行 */
    DumpInt(f->lastlinedefined, D);       /* 定义结束行 */
    DumpChar(f->nups, D);                 /* upvalue数量 */
    DumpChar(f->numparams, D);            /* 参数数量 */
    DumpChar(f->is_vararg, D);            /* 可变参数标志 */
    DumpChar(f->maxstacksize, D);         /* 最大栈大小 */
    
    /*
    ** [组件输出] 输出函数的各个组成部分
    */
    DumpCode(f, D);        /* 指令序列 */
    DumpConstants(f, D);   /* 常量表 */
    DumpDebug(f, D);       /* 调试信息 */
}


/*
** [内部] 输出字节码文件头
**
** 详细功能说明：
** 输出Lua字节码文件的标准头部，包含所有必要的平台和版本信息。
** 这个头部用于加载时验证字节码文件的兼容性和完整性。
**
** 参数说明：
** @param D - DumpState*：序列化状态对象
**
** 返回值：无
**
** 头部内容：
** - 使用luaU_header生成标准头部
** - 包含版本、格式、平台信息
** - 与lundump.c的加载格式一致
**
** 作用：
** - 标识文件为Lua字节码
** - 提供版本和兼容性信息
** - 检测平台特征差异
*/
static void DumpHeader(DumpState* D)
{
    char h[LUAC_HEADERSIZE];
    
    /*
    ** [头部生成] 生成标准字节码头部
    */
    luaU_header(h);
    
    /*
    ** [头部输出] 输出完整的头部数据
    */
    DumpBlock(h, LUAC_HEADERSIZE, D);
}


/*
** [公共] 序列化Lua函数为预编译字节码
**
** 详细功能说明：
** 这是字节码序列化的主要入口点，负责将Lua函数原型对象转换为
** 二进制字节码格式。支持调试信息剥离和错误处理。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param f - const Proto*：要序列化的函数原型
** @param w - lua_Writer：输出回调函数
** @param data - void*：传递给writer的用户数据
** @param strip - int：调试信息剥离标志（1=剥离，0=保留）
**
** 返回值：
** @return int：序列化状态（0=成功，非0=错误码）
**
** 算法复杂度：O(n) 时间，其中n是函数总大小
**
** 序列化流程：
** 1. 初始化序列化状态
** 2. 输出字节码文件头
** 3. 输出主函数原型
** 4. 返回最终状态
**
** 错误处理：
** - writer函数的错误会被记录并返回
** - 序列化过程具有原子性（要么全成功要么全失败）
** - 错误状态通过返回值传播给调用者
**
** 使用场景：
** - luac编译器生成字节码文件
** - string.dump函数的底层实现
** - 动态代码生成和缓存
**
** 调试信息控制：
** - strip=0：生成包含完整调试信息的字节码
** - strip=1：生成剥离调试信息的紧凑字节码
** - 影响文件大小和调试能力
*/
int luaU_dump(lua_State* L, const Proto* f, lua_Writer w, void* data, int strip)
{
    DumpState D;
    
    /*
    ** [状态初始化] 设置序列化状态
    */
    D.L = L;                 /* 关联Lua状态机 */
    D.writer = w;            /* 设置输出回调函数 */
    D.data = data;           /* 保存用户数据 */
    D.strip = strip;         /* 设置调试信息剥离标志 */
    D.status = 0;            /* 初始状态为成功 */
    
    /*
    ** [头部输出] 输出字节码文件头
    */
    DumpHeader(&D);
    
    /*
    ** [函数输出] 输出主函数原型
    ** 传入NULL作为父函数源文件名
    */
    DumpFunction(f, NULL, &D);
    
    /*
    ** [状态返回] 返回最终的序列化状态
    */
    return D.status;
}