/*
** [核心] Lua调试接口模块
**
** 功能概述：
** 提供完整的调试支持，包括调用栈跟踪、局部变量查询、Hook机制、
** 错误处理和字节码验证等功能
**
** 主要功能模块：
** 1. Hook系统：支持行、调用、返回、计数Hook
** 2. 调用栈管理：获取和遍历函数调用栈信息
** 3. 局部变量访问：运行时查询和修改局部变量
** 4. 错误处理：格式化错误消息，提供调用上下文
** 5. 字节码验证：静态分析字节码的正确性
**
** 调试信息结构：
** - lua_Debug：调试信息的标准容器
** - CallInfo：调用栈帧信息
** - Proto：函数原型的调试元数据
**
** 核心算法：
** - 符号执行：静态分析字节码流
** - 栈回溯：动态分析调用链
** - 变量作用域计算：确定变量的有效范围
**
** 性能考虑：
** - Hook调用的开销最小化
** - 调试信息的延迟计算
** - 字节码验证的增量执行
**
** 模块依赖：
** - lvm.c：虚拟机执行状态
** - ldo.c：函数调用和错误处理
** - lstring.c：调试字符串管理
**
** 相关文档：参见 docs/wiki_debug.md
*/

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#define ldebug_c
#define LUA_CORE

#include "lua.h"

#include "lapi.h"
#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


// [内部] 前向声明
static const char *getfuncname(lua_State *L, CallInfo *ci, const char **name);


/*
** [进阶] 获取当前程序计数器
**
** 功能详述：
** 计算调用信息对应的程序计数器相对位置，用于定位当前执行的字节码指令
**
** 计算逻辑：
** 1. 验证是否为Lua函数（C函数没有PC概念）
** 2. 如果是当前活跃调用，同步保存的PC
** 3. 计算相对于函数原型起始位置的偏移量
**
** @param L - lua_State*：Lua状态机
** @param ci - CallInfo*：目标调用信息
** @return int：程序计数器相对位置，C函数返回-1
**
** 算法复杂度：O(1) 时间
**
** 注意事项：
** - 只有Lua函数才有有效的程序计数器
** - 当前调用需要同步PC状态
*/
static int currentpc(lua_State *L, CallInfo *ci)
{
    //
    // [类型检查] 验证是否为Lua函数
    // C函数没有字节码，因此没有程序计数器概念
    //
    if (!isLua(ci)) 
    {
        return -1;
    }
        
    //
    // [状态同步] 如果是当前活跃调用，更新保存的程序计数器
    // 这确保获取到最新的执行位置
    //
    if (ci == L->ci)
    {
        ci->savedpc = L->savedpc;
    }
        
    //
    // [相对位置] 计算相对于函数原型的程序计数器位置
    // pcRel宏将绝对地址转换为相对偏移量
    //
    return pcRel(ci->savedpc, ci_func(ci)->l.p);
}


/*
** [基础] 获取当前行号
**
** 功能详述：
** 获取指定调用信息对应的当前执行行号，用于调试和错误报告
**
** 执行流程：
** 1. 获取当前程序计数器位置
** 2. 检查是否为有效的Lua函数调用
** 3. 从函数原型的行信息表中查找对应行号
**
** @param L - lua_State*：Lua状态机
** @param ci - CallInfo*：目标调用信息
** @return int：当前行号，无法获取返回-1
**
** 算法复杂度：O(1) 时间
**
** 注意事项：
** - C函数没有行号概念
** - 需要函数原型包含调试行信息
*/
static int currentline(lua_State *L, CallInfo *ci)
{
    // [位置计算] 获取当前程序计数器位置
    int pc = currentpc(L, ci);
    
    // [有效性检查] 只有活跃的Lua函数才有当前行信息
    if (pc < 0)
    {
        return -1;
    }
    else
    {
        // [行号查找] 从原型行信息表中获取对应行号
        return getline(ci_func(ci)->l.p, pc);
    }
}


/*
** [核心] 设置Hook函数
**
** 功能详述：
** 为Lua状态机设置调试Hook函数，支持行、调用、返回、计数等事件
** 这个函数可以异步调用（例如在信号处理中），保证线程安全
**
** Hook类型支持：
** 1. LUA_MASKCALL - 函数调用事件
** 2. LUA_MASKRET - 函数返回事件  
** 3. LUA_MASKLINE - 行执行事件
** 4. LUA_MASKCOUNT - 指令计数事件
**
** @param L - lua_State*：Lua状态机
** @param func - lua_Hook：Hook函数指针，NULL表示禁用
** @param mask - int：Hook事件掩码
** @param count - int：Hook触发计数，用于MASKCOUNT
** @return int：总是返回1表示成功
**
** 算法复杂度：O(1) 时间
**
** 线程安全性：
** - 可以异步调用
** - 使用原子操作更新Hook状态
*/
LUA_API int lua_sethook(lua_State *L, lua_Hook func, int mask, int count)
{
    // [参数验证] 如果函数为空或掩码为0，关闭Hook
    if (func == NULL || mask == 0) 
    {
        mask = 0;
        func = NULL;
    }
    
    // [状态更新] 设置Hook相关参数
    L->hook = func;
    L->basehookcount = count;
    resethookcount(L);
    L->hookmask = cast_byte(mask);
    
    return 1;
}


/*
** [接口] 获取Hook函数
**
** 功能详述：
** 返回当前Lua状态机设置的Hook函数指针
**
** @param L - lua_State*：Lua状态机
** @return lua_Hook：Hook函数指针，未设置返回NULL
**
** 算法复杂度：O(1) 时间
*/
LUA_API lua_Hook lua_gethook(lua_State *L)
{
    return L->hook;
}


/*
** [接口] 获取Hook掩码
**
** 功能详述：
** 返回当前Lua状态机设置的Hook事件掩码
**
** @param L - lua_State*：Lua状态机
** @return int：Hook掩码值
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_gethookmask(lua_State *L)
{
    return L->hookmask;
}


/*
** [接口] 获取Hook计数
**
** 功能详述：
** 返回当前Lua状态机设置的Hook触发计数基数
**
** @param L - lua_State*：Lua状态机
** @return int：Hook计数基数
**
** 算法复杂度：O(1) 时间
*/
LUA_API int lua_gethookcount(lua_State *L)
{
    return L->basehookcount;
}


/*
** [核心] 获取调用栈信息
**
** 功能详述：
** 获取指定层级的调用栈信息，用于调试和错误追踪
** 支持尾调用优化的栈帧处理
**
** 栈层级计算：
** 1. level=0 表示当前调用
** 2. level=1 表示调用者
** 3. 负数level表示丢失的尾调用
**
** @param L - lua_State*：Lua状态机
** @param level - int：栈层级（0=当前，1=调用者...）
** @param ar - lua_Debug*：调试信息结构输出
** @return int：成功返回1，失败返回0
**
** 算法复杂度：O(level) 时间
**
** 尾调用处理：
** - 正确计算尾调用丢失的栈帧数
** - 区分真实栈帧和虚拟栈帧
*/
LUA_API int lua_getstack(lua_State *L, int level, lua_Debug *ar)
{
    int status;
    CallInfo *ci;
    
    lua_lock(L);
    
    //
    // [栈遍历] 向下遍历调用栈
    // 跳过指定层数，同时处理尾调用
    //
    for (ci = L->ci; level > 0 && ci > L->base_ci; ci--) 
    {
        level--;
        
        // [尾调用处理] 如果是Lua函数，需要跳过丢失的尾调用
        if (f_isLua(ci))
        {
            level -= ci->tailcalls;
        }
    }
    
    // [结果检查] 检查是否找到了对应层级
    if (level == 0 && ci > L->base_ci) 
    {
        status = 1;
        ar->i_ci = cast_int(ci - L->base_ci);
    }
    else if (level < 0) 
    {
        // [尾调用层级] 层级是丢失的尾调用
        status = 1;
        ar->i_ci = 0;
    }
    else 
    {
        // [无效层级] 没有这样的层级
        status = 0;
    }
    
    lua_unlock(L);
    return status;
}


/*
** [基础] 获取Lua函数原型
**
** 功能详述：
** 从调用信息中提取Lua函数的原型指针，用于访问调试元数据
**
** @param ci - CallInfo*：调用信息
** @return Proto*：函数原型指针，如果不是Lua函数返回NULL
**
** 算法复杂度：O(1) 时间
*/
static Proto *getluaproto(CallInfo *ci)
{
    return (isLua(ci) ? ci_func(ci)->l.p : NULL);
}


/*
** [进阶] 查找局部变量名称
**
** 功能详述：
** 根据调用信息和变量编号查找局部变量的名称
** 支持Lua函数的调试符号和临时变量处理
**
** 查找策略：
** 1. 优先从函数原型的调试信息中查找
** 2. 对于C函数或无调试信息，检查栈范围
** 3. 在栈范围内但无名称的变量标记为临时变量
**
** @param L - lua_State*：Lua状态机
** @param ci - CallInfo*：调用信息
** @param n - int：变量编号（1开始）
** @return const char*：变量名称，未找到返回NULL
**
** 算法复杂度：O(log k) 时间，k为局部变量数
**
** 变量类型：
** - 命名局部变量：来自调试符号
** - 临时变量：栈上但无名称的变量
*/
static const char *findlocal(lua_State *L, CallInfo *ci, int n)
{
    const char *name;
    Proto *fp = getluaproto(ci);
    
    // [调试信息查找] 如果是Lua函数，尝试从调试信息中获取变量名
    if (fp && (name = luaF_getlocalname(fp, n, currentpc(L, ci))) != NULL)
    {
        return name;
    }
    else 
    {
        // [栈范围检查] 不是Lua函数或没有调试信息，检查是否在栈范围内
        StkId limit = (ci == L->ci) ? L->top : (ci + 1)->func;
        
        if (limit - ci->base >= n && n > 0)
        {
            // [临时变量] 在栈范围内但无具体名称
            return "(*temporary)";
        }
        else
        {
            // [无效索引] 超出栈范围
            return NULL;
        }
    }
}


/*
** [接口] 获取局部变量
**
** 功能详述：
** 获取指定调试层级的局部变量值，并将其压入栈顶
** 这是Lua调试API的核心函数之一
**
** 执行流程：
** 1. 从调试信息获取调用信息
** 2. 查找对应的局部变量名称
** 3. 如果找到变量，将其值压入栈
**
** @param L - lua_State*：Lua状态机
** @param ar - const lua_Debug*：调试信息
** @param n - int：变量编号（1开始）
** @return const char*：变量名称，未找到返回NULL
**
** 算法复杂度：O(log k) 时间，k为局部变量数
**
** 栈操作：
** - 成功时：压入变量值到栈顶
** - 失败时：栈不变
*/
LUA_API const char *lua_getlocal(lua_State *L, const lua_Debug *ar, int n)
{
    // [调用信息] 从调试信息获取调用信息指针
    CallInfo *ci = L->base_ci + ar->i_ci;
    const char *name = findlocal(L, ci, n);
    
    lua_lock(L);
    
    // [值获取] 如果找到变量，将其值压入栈
    if (name)
    {
        luaA_pushobject(L, ci->base + (n - 1));
    }
        
    lua_unlock(L);
    return name;
}


/*
** [接口] 设置局部变量
**
** 功能详述：
** 设置指定调试层级的局部变量值，从栈顶取值
** 这是Lua调试API中修改变量的核心函数
**
** 执行流程：
** 1. 从调试信息获取调用信息
** 2. 查找对应的局部变量名称
** 3. 如果找到变量，用栈顶值设置它
** 4. 弹出栈顶值
**
** @param L - lua_State*：Lua状态机
** @param ar - const lua_Debug*：调试信息
** @param n - int：变量编号（1开始）
** @return const char*：变量名称，未找到返回NULL
**
** 算法复杂度：O(log k) 时间，k为局部变量数
**
** 栈操作：
** - 总是弹出栈顶值
** - 成功时：将值设置给对应变量
*/
LUA_API const char *lua_setlocal(lua_State *L, const lua_Debug *ar, int n)
{
    // [调用信息] 从调试信息获取调用信息指针
    CallInfo *ci = L->base_ci + ar->i_ci;
    const char *name = findlocal(L, ci, n);
    
    lua_lock(L);
    
    // [值设置] 如果找到变量，设置其值
    if (name)
    {
        setobjs2s(L, ci->base + (n - 1), L->top - 1);
    }
        
    // [栈清理] 弹出栈顶值
    L->top--;
    
    lua_unlock(L);
    return name;
}


// [基础] 填充函数信息
// 功能详述：
// 根据闭包类型填充调试信息结构中的函数相关字段
// 区分C函数和Lua函数的不同属性
//
// 信息类型：
// 1. C函数：source="[C]", 无行号信息
// 2. Lua函数：实际源文件、行号范围
// 3. 主块：特殊标记为"main"
//
// @param ar - lua_Debug*：调试信息结构
// @param cl - Closure*：闭包对象
//
// 算法复杂度：O(1) 时间
//
// 字段填充：
// - source: 源文件标识
// - linedefined: 函数定义起始行
// - lastlinedefined: 函数定义结束行
// - what: 函数类型描述
// - short_src: 简短源文件标识
//
static void funcinfo(lua_Debug *ar, Closure *cl)
{
    if (cl->c.isC) 
    {
        // [C函数信息] C函数的标准调试信息
        ar->source = "=[C]";
        ar->linedefined = -1;
        ar->lastlinedefined = -1;
        ar->what = "C";
    }
    else 
    {
        // [Lua函数信息] 从函数原型获取调试信息
        ar->source = getstr(cl->l.p->source);
        ar->linedefined = cl->l.p->linedefined;
        ar->lastlinedefined = cl->l.p->lastlinedefined;
        ar->what = (ar->linedefined == 0) ? "main" : "Lua";
    }
    
    // [源文件标识] 生成简短的源文件标识
    luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
}


// [基础] 填充尾调用信息
// 功能详述：
// 为尾调用优化的虚拟栈帧填充特殊的调试信息
// 尾调用没有真实的栈帧，需要特殊处理
//
// @param ar - lua_Debug*：调试信息结构
//
// 算法复杂度：O(1) 时间
//
// 特殊标记：
// - name: 空字符串（无函数名）
// - what: "tail"（尾调用标识）
// - source: "=(tail call)"（特殊源标识）
// - 所有行号信息均为-1
//
static void info_tailcall(lua_Debug *ar)
{
    // [名称信息] 尾调用没有具体的函数名称信息
    ar->name = ar->namewhat = "";
    ar->what = "tail";
    
    // [位置信息] 尾调用没有具体的位置信息
    ar->lastlinedefined = ar->linedefined = ar->currentline = -1;
    
    // [源标识] 使用特殊的尾调用源标识
    ar->source = "=(tail call)";
    luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
    
    // [upvalue] 尾调用没有upvalue信息
    ar->nups = 0;
}


// [基础] 收集有效行号
// 功能详述：
// 为指定闭包创建有效行号表，用于调试器的断点设置
// 支持C函数和Lua函数的不同处理策略
//
// 处理逻辑：
// 1. C函数：返回nil（无行号概念）
// 2. Lua函数：遍历行信息表，创建行号到true的映射
//
// @param L - lua_State*：Lua状态机
// @param f - Closure*：闭包对象
//
// 算法复杂度：O(n) 时间，n为行信息数量
//
// 结果形式：
// - table[行号] = true（有效行）
// - nil（C函数或无调试信息）
//
static void collectvalidlines(lua_State *L, Closure *f)
{
    // [类型检查] 如果不是Lua函数，返回nil
    if (f == NULL || f->c.isC) 
    {
        setnilvalue(L->top);
    }
    else 
    {
        // [表创建] 为Lua函数创建行号表
        Table *t = luaH_new(L, 0, 0);
        int *lineinfo = f->l.p->lineinfo;
        int i;
        
        // [行号遍历] 将所有有效行号加入表中
        for (i = 0; i < f->l.p->sizelineinfo; i++)
        {
            setbvalue(luaH_setnum(L, t, lineinfo[i]), 1);
        }
            
        sethvalue(L, L->top, t);
    }
    
    incr_top(L);
}


// [进阶] 获取调试信息的辅助函数
// 功能详述：
// 根据请求字符串填充调试信息结构的各个字段
// 这是lua_getinfo的核心实现函数
//
// 支持的请求类型：
// - 'S': 源文件信息（source, linedefined等）
// - 'l': 当前行号（currentline）
// - 'u': upvalue数量（nups）
// - 'n': 函数名称信息（name, namewhat）
// - 'L': 有效行号表（由调用者处理）
// - 'f': 函数对象（由调用者处理）
//
// @param L - lua_State*：Lua状态机
// @param what - const char*：请求的信息类型字符串
// @param ar - lua_Debug*：调试信息结构
// @param f - Closure*：闭包对象
// @param ci - CallInfo*：调用信息
// @return int：成功返回1，失败返回0
//
// 算法复杂度：O(|what|) 时间
//
// 错误处理：
// - 无效请求字符导致返回0
// - 尾调用情况的特殊处理
//
static int auxgetinfo(lua_State *L, const char *what, lua_Debug *ar,
                      Closure *f, CallInfo *ci)
{
    int status = 1;
    
    // [尾调用处理] 如果没有函数，填充尾调用信息
    if (f == NULL) 
    {
        info_tailcall(ar);
        return status;
    }
    
    // [请求处理] 根据请求类型填充信息
    for (; *what; what++) 
    {
        switch (*what) 
        {
            case 'S': 
            {
                // [源信息] 源文件信息
                funcinfo(ar, f);
                break;
            }
            
            case 'l': 
            {
                // [行号] 当前行号
                ar->currentline = (ci) ? currentline(L, ci) : -1;
                break;
            }
            
            case 'u': 
            {
                // [upvalue] upvalue数量
                ar->nups = f->c.nupvalues;
                break;
            }
            
            case 'n': 
            {
                // [名称] 函数名称信息
                ar->namewhat = (ci) ? getfuncname(L, ci, &ar->name) : NULL;
                if (ar->namewhat == NULL) 
                {
                    ar->namewhat = "";
                    ar->name = NULL;
                }
                break;
            }
            
            case 'L':
            case 'f':
                // [延迟处理] 这些选项由lua_getinfo处理
                break;
                
            default:
                // [错误] 无效选项
                status = 0;
        }
    }
    
    return status;
}


/*
** [核心] 获取调试信息
**
** 功能详述：
** Lua调试API的主要接口，获取函数或调用栈的详细调试信息
** 支持从栈或调用信息获取函数，并根据请求填充调试结构
**
** 输入模式：
** 1. '>'开头：从栈顶获取函数
** 2. 普通调用：从调试信息的调用索引获取
**
** 输出选项：
** - 'f': 将函数对象压入栈
** - 'L': 将有效行号表压入栈
** - 其他：填充调试结构字段
**
** @param L - lua_State*：Lua状态机
** @param what - const char*：请求的信息类型
** @param ar - lua_Debug*：调试信息结构
** @return int：成功返回1，失败返回0
**
** 算法复杂度：O(|what| + n) 时间，n为行信息数量
**
** 栈操作：
** - '>'模式：弹出函数对象
** - 'f'选项：压入函数对象
** - 'L'选项：压入行号表
*/
LUA_API int lua_getinfo(lua_State *L, const char *what, lua_Debug *ar)
{
    int status;
    Closure *f = NULL;
    CallInfo *ci = NULL;
    
    lua_lock(L);
    
    // [输入模式] 检查是否从栈顶获取函数
    if (*what == '>') 
    {
        StkId func = L->top - 1;
        luai_apicheck(L, ttisfunction(func));
        
        // [跳过标记] 跳过'>'字符
        what++;
        f = clvalue(func);
        
        // [栈清理] 弹出函数
        L->top--;
    }
    else if (ar->i_ci != 0) 
    {
        // [调用信息] 不是尾调用，从调用信息获取函数
        ci = L->base_ci + ar->i_ci;
        lua_assert(ttisfunction(ci->func));
        f = clvalue(ci->func);
    }
    
    // [信息获取] 获取调试信息
    status = auxgetinfo(L, what, ar, f, ci);
    
    // [函数对象] 如果请求函数对象
    if (strchr(what, 'f')) 
    {
        if (f == NULL) 
        {
            setnilvalue(L->top);
        }
        else 
        {
            setclvalue(L, L->top, f);
        }
            
        incr_top(L);
    }
    
    // [有效行号] 如果请求有效行号
    if (strchr(what, 'L'))
    {
        collectvalidlines(L, f);
    }
        
    lua_unlock(L);
    return status;
}


//
// =====================================================================
// [专家] 符号执行和代码检查器
// =====================================================================
//

// [内部] 检查宏定义
#define check(x) if (!(x)) return 0;

// [内部] 检查跳转目标
#define checkjump(pt,pc) check(0 <= pc && pc < pt->sizecode)

// [内部] 检查寄存器
#define checkreg(pt,reg) check((reg) < (pt)->maxstacksize)


/*
** [专家] 预检查函数原型
**
** 功能详述：
** 对函数原型进行基本的一致性检查，验证各项元数据的合理性
** 这是字节码验证的第一步，确保基本约束条件
**
** 检查项目：
** 1. 最大栈大小不超过VM限制
** 2. 参数数量与栈大小的一致性
** 3. 变参标志的正确组合
** 4. upvalue数量的合理性
** 5. 行信息与代码大小的一致性
** 6. 代码必须以RETURN结尾
**
** @param pt - const Proto*：函数原型
** @return int：检查通过返回1，失败返回0
**
** 算法复杂度：O(1) 时间
**
** 安全性：
** - 防止栈溢出攻击
** - 验证变参使用的正确性
** - 确保函数有正确的返回路径
*/
static int precheck(const Proto *pt)
{
    // [栈大小] 检查最大栈大小
    check(pt->maxstacksize <= MAXSTACK);
    
    // [参数数量] 检查参数数量
    check(pt->numparams + (pt->is_vararg & VARARG_HASARG) <= pt->maxstacksize);
    
    // [变参标志] 检查变参标志
    check(!(pt->is_vararg & VARARG_NEEDSARG) ||
          (pt->is_vararg & VARARG_HASARG));
          
    // [upvalue] 检查upvalue数量
    check(pt->sizeupvalues <= pt->nups);
    
    // [行信息] 检查行信息大小
    check(pt->sizelineinfo == pt->sizecode || pt->sizelineinfo == 0);
    
    // [代码结构] 检查代码以RETURN结尾
    check(pt->sizecode > 0 && GET_OPCODE(pt->code[pt->sizecode - 1]) == OP_RETURN);
    
    return 1;
}


// [内部] 检查开放操作宏
#define checkopenop(pt,pc) luaG_checkopenop((pt)->code[(pc)+1])


// [专家] 检查开放操作
// 功能详述：
// 验证开放指令（参数数量不定的指令）的后续指令是否有效
// 开放指令如CALL、TAILCALL等需要特殊的后续指令处理
//
// 有效的开放指令后续：
// - OP_CALL: B=0的调用指令
// - OP_TAILCALL: B=0的尾调用指令  
// - OP_RETURN: B=0的返回指令
// - OP_SETLIST: B=0的列表设置指令
//
// @param i - Instruction：待检查的指令
// @return int：有效返回1，无效返回0
//
// 算法复杂度：O(1) 时间
//
// 安全性：
// - 防止非法的开放指令组合
// - 确保虚拟机执行的正确性
//
int luaG_checkopenop(Instruction i)
{
    switch (GET_OPCODE(i)) 
    {
        case OP_CALL:
        case OP_TAILCALL:
        case OP_RETURN:
        case OP_SETLIST: 
        {
            check(GETARG_B(i) == 0);
            return 1;
        }
        
        default: 
            // [错误] 开放调用后的无效指令
            return 0;
    }
}


// [进阶] 检查参数模式
// 功能详述：
// 根据指令的参数模式验证参数值的有效性
// 不同的参数模式对应不同的取值范围和约束
//
// 参数模式类型：
// - OpArgN: 不使用参数（必须为0）
// - OpArgU: 任意值（无约束）
// - OpArgR: 寄存器索引（必须小于maxstacksize）
// - OpArgK: 常量或寄存器（K格式或寄存器范围）
//
// @param pt - const Proto*：函数原型
// @param r - int：寄存器/常量索引
// @param mode - enum OpArgMask：参数模式
// @return int：检查通过返回1，失败返回0
//
// 算法复杂度：O(1) 时间
//
// 范围检查：
// - 寄存器索引的上界检查
// - 常量索引的有效性验证
// - 参数使用的一致性检查
//
static int checkArgMode(const Proto *pt, int r, enum OpArgMask mode)
{
    switch (mode) 
    {
        case OpArgN: 
            // [不使用] 不使用参数
            check(r == 0); 
            break;
            
        case OpArgU: 
            // [任意值] 任意值
            break;
            
        case OpArgR: 
            // [寄存器] 寄存器
            checkreg(pt, r); 
            break;
            
        case OpArgK:
            // [常量或寄存器] 常量或寄存器
            check(ISK(r) ? INDEXK(r) < pt->sizek : r < pt->maxstacksize);
            break;
    }
    
    return 1;
}


/*
** [专家] 符号执行函数
**
** 功能详述：
** 对字节码进行符号执行分析，跟踪寄存器的数据流和控制流
** 这是Lua字节码验证的核心算法，确保代码的正确性和安全性
**
** 主要功能：
** 1. 验证所有指令的操作码和参数有效性
** 2. 跟踪寄存器的最后修改位置
** 3. 检查跳转指令的目标合法性
** 4. 验证特殊指令的语义约束
**
** 符号执行过程：
** 1. 预检查函数原型的基本约束
** 2. 逐指令分析操作码和参数
** 3. 根据指令格式检查参数模式
** 4. 处理特殊指令的语义约束
** 5. 跟踪控制流和数据流
**
** @param pt - const Proto*：函数原型
** @param lastpc - int：最后程序计数器位置
** @param reg - int：跟踪的寄存器编号（NO_REG表示不跟踪）
** @return Instruction：最后改变指定寄存器的指令
**
** 算法复杂度：O(n) 时间，n为指令数量
**
** 安全性保障：
** - 防止无效操作码攻击
** - 验证参数范围和类型
** - 检查跳转目标的合法性
** - 确保栈操作的安全性
*/
static Instruction symbexec(const Proto *pt, int lastpc, int reg)
{
    int pc;
    // [追踪变量] 存储最后改变reg的指令位置
    int last;
    
    // [初始化] 指向最终返回（一个'中性'指令）
    last = pt->sizecode - 1;
    
    // [预检查] 预检查
    check(precheck(pt));
    
    // [主循环] 遍历所有指令进行符号执行
    for (pc = 0; pc < lastpc; pc++) 
    {
        Instruction i = pt->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);
        int b = 0;
        int c = 0;
        
        // [操作码检查] 检查操作码有效性
        check(op < NUM_OPCODES);
        
        // [A参数检查] 检查A参数
        checkreg(pt, a);
        
        // [参数解析] 根据指令格式检查参数
        switch (getOpMode(op)) 
        {
            case iABC: 
            {
                b = GETARG_B(i);
                c = GETARG_C(i);
                check(checkArgMode(pt, b, getBMode(op)));
                check(checkArgMode(pt, c, getCMode(op)));
                break;
            }
            
            case iABx: 
            {
                b = GETARG_Bx(i);
                if (getBMode(op) == OpArgK) 
                {
                    check(b < pt->sizek);
                }
                break;
            }
            
            case iAsBx: 
            {
                b = GETARG_sBx(i);
                if (getBMode(op) == OpArgR) 
                {
                    int dest = pc + 1 + b;
                    check(0 <= dest && dest < pt->sizecode);
                    
                    if (dest > 0) 
                    {
                        int j;
                        //
                        // [SETLIST检查] 检查不会跳转到setlist计数
                        // 这很复杂，因为之前setlist的计数可能与无效setlist有相同值
                        // 所以必须回到第一个（如果有的话）
                        //
                        for (j = 0; j < dest; j++) 
                        {
                            Instruction d = pt->code[dest - 1 - j];
                            if (!(GET_OPCODE(d) == OP_SETLIST && GETARG_C(d) == 0)) 
                            {
                                break;
                            }
                        }
                        
                        // [奇偶检查] 如果j是偶数，前值不是setlist（即使看起来像）
                        check((j & 1) == 0);
                    }
                }
                break;
            }
        }
        
        // [寄存器跟踪] 如果指令修改A寄存器
        if (testAMode(op)) 
        {
            if (a == reg) 
            {
                last = pc;
            }
        }
        
        // [测试指令] 如果是测试指令
        if (testTMode(op)) 
        {
            // [跳过检查] 检查跳过
            check(pc + 2 < pt->sizecode);
            check(GET_OPCODE(pt->code[pc + 1]) == OP_JMP);
        }
        
        // [特殊指令] 特殊指令检查
        switch (op) 
        {
            case OP_LOADBOOL: 
            {
                // [条件跳转] 如果会跳转
                if (c == 1) 
                {
                    check(pc + 2 < pt->sizecode);
                    check(GET_OPCODE(pt->code[pc + 1]) != OP_SETLIST ||
                          GETARG_C(pt->code[pc + 1]) != 0);
                }
                break;
            }
            
            case OP_LOADNIL: 
            {
                // [范围设置] 设置从a到b的寄存器
                if (a <= reg && reg <= b)
                {
                    last = pc;
                }
                break;
            }
            
            case OP_GETUPVAL:
            case OP_SETUPVAL: 
            {
                // [upvalue检查] 验证upvalue索引有效性
                check(b < pt->nups);
                break;
            }
            
            case OP_GETGLOBAL:
            case OP_SETGLOBAL: 
            {
                // [全局变量] 验证全局变量名为字符串
                check(ttisstring(&pt->k[b]));
                break;
            }
            
            case OP_SELF: 
            {
                // [self方法] 检查额外寄存器空间
                checkreg(pt, a + 1);
                if (reg == a + 1) 
                {
                    last = pc;
                }
                break;
            }
            
            case OP_CONCAT: 
            {
                // [连接操作] 至少两个操作数
                check(b < c);
                break;
            }
            
            case OP_TFORLOOP: 
            {
                // [迭代器] 至少一个结果（控制变量）
                check(c >= 1);
                
                // [空间检查] 结果的空间
                checkreg(pt, a + 2 + c);
                
                // [寄存器影响] 影响其基址以上的所有寄存器
                if (reg >= a + 2) 
                {
                    last = pc;
                }
                break;
            }
            
            case OP_FORLOOP:
            case OP_FORPREP:
                // [数值for] 检查for循环的寄存器空间
                checkreg(pt, a + 3);
                // [继续] 继续到JMP处理
                
            case OP_JMP: 
            {
                int dest = pc + 1 + b;
                
                // [条件跳转] 不是完全检查且跳转向前且不跳过lastpc
                if (reg != NO_REG && pc < dest && dest <= lastpc)
                {
                    pc += b;
                }
                break;
            }
            
            case OP_CALL:
            case OP_TAILCALL: 
            {
                // [参数检查] 验证参数数量
                if (b != 0) 
                {
                    checkreg(pt, a + b - 1);
                }
                
                // [返回值] c = 返回值数量
                c--;
                if (c == LUA_MULTRET) 
                {
                    check(checkopenop(pt, pc));
                }
                else if (c != 0) 
                {
                    checkreg(pt, a + c - 1);
                }
                
                // [寄存器影响] 影响基址以上的所有寄存器
                if (reg >= a) 
                {
                    last = pc;
                }
                break;
            }
            
            case OP_RETURN: 
            {
                // [返回检查] b = 返回值数量
                b--;
                if (b > 0) 
                {
                    checkreg(pt, a + b - 1);
                }
                break;
            }
            
            case OP_SETLIST: 
            {
                // [列表设置] 检查列表设置操作
                if (b > 0) 
                {
                    checkreg(pt, a + b);
                }
                    
                if (c == 0) 
                {
                    pc++;
                    check(pc < pt->sizecode - 1);
                }
                break;
            }
            
            case OP_CLOSURE: 
            {
                int nup, j;
                // [闭包检查] 验证闭包原型索引
                check(b < pt->sizep);
                nup = pt->p[b]->nups;
                check(pc + nup < pt->sizecode);
                
                // [upvalue指令] 检查后续upvalue指令
                for (j = 1; j <= nup; j++) 
                {
                    OpCode op1 = GET_OPCODE(pt->code[pc + j]);
                    check(op1 == OP_GETUPVAL || op1 == OP_MOVE);
                }
                
                // [跟踪跳过] 如果在跟踪
                if (reg != NO_REG)
                {
                    pc += nup;
                }
                break;
            }
            
            case OP_VARARG: 
            {
                // [变参检查] 验证变参标志
                check((pt->is_vararg & VARARG_ISVARARG) &&
                      !(pt->is_vararg & VARARG_NEEDSARG));
                b--;
                if (b == LUA_MULTRET) 
                {
                    check(checkopenop(pt, pc));
                }
                checkreg(pt, a + b - 1);
                break;
            }
            
            default: 
                break;
        }
    }
    
    // [返回结果] 返回最后修改目标寄存器的指令
    return pt->code[last];
}

// [清理] 取消宏定义
#undef check
#undef checkjump
#undef checkreg


// [接口] 检查代码有效性
// 功能详述：
// 对整个函数原型进行完整的字节码验证
// 调用符号执行器检查代码的正确性和安全性
//
// @param pt - const Proto*：函数原型
// @return int：代码有效返回非0值
//
// 算法复杂度：O(n) 时间，n为指令数量
//
int luaG_checkcode(const Proto *pt)
{
    return (symbexec(pt, pt->sizecode, NO_REG) != 0);
}


// [基础] 获取常量名称
// 功能详述：
// 从常量表中获取字符串常量的内容，用于调试信息显示
//
// @param p - Proto*：函数原型
// @param c - int：常量索引（可能是K格式）
// @return const char*：常量名称，如果不是字符串返回"?"
//
// 算法复杂度：O(1) 时间
//
static const char *kname(Proto *p, int c)
{
    if (ISK(c) && ttisstring(&p->k[INDEXK(c)]))
    {
        return svalue(&p->k[INDEXK(c)]);
    }
    else
    {
        return "?";
    }
}


// [进阶] 获取对象名称
// 功能详述：
// 通过符号执行分析确定栈位置上对象的名称和类型
// 这是错误报告中显示有意义变量名的核心功能
//
// 分析策略：
// 1. 优先检查是否为局部变量
// 2. 使用符号执行追踪数据来源
// 3. 根据产生该值的指令确定名称类型
//
// 支持的对象类型：
// - local: 局部变量
// - global: 全局变量
// - field: 表字段
// - upvalue: upvalue变量
// - method: 方法调用
//
// @param L - lua_State*：Lua状态机
// @param ci - CallInfo*：调用信息
// @param stackpos - int：栈位置
// @param name - const char**：输出名称
// @return const char*：对象类型描述，未找到返回NULL
//
// 算法复杂度：O(n) 时间，n为指令数量
//
static const char *getobjname(lua_State *L, CallInfo *ci, int stackpos,
                              const char **name)
{
    // [类型检查] 检查是否为Lua函数
    if (isLua(ci)) 
    {
        Proto *p = ci_func(ci)->l.p;
        int pc = currentpc(L, ci);
        Instruction i;
        
        // [局部变量] 首先检查是否为局部变量
        *name = luaF_getlocalname(p, stackpos + 1, pc);
        if (*name)
        {
            return "local";
        }
            
        // [符号执行] 尝试符号执行
        i = symbexec(p, pc, stackpos);
        lua_assert(pc != -1);
        
        // [指令分析] 根据指令类型确定对象名称
        switch (GET_OPCODE(i)) 
        {
            case OP_GETGLOBAL: 
            {
                // [全局变量] 全局变量索引
                int g = GETARG_Bx(i);
                lua_assert(ttisstring(&p->k[g]));
                *name = svalue(&p->k[g]);
                return "global";
            }
            
            case OP_MOVE: 
            {
                int a = GETARG_A(i);
                // [移动指令] 从b移动到a
                int b = GETARG_B(i);
                if (b < a)
                {
                    return getobjname(L, ci, b, name);
                }
                break;
            }
            
            case OP_GETTABLE: 
            {
                // [表字段] 键索引
                int k = GETARG_C(i);
                *name = kname(p, k);
                return "field";
            }
            
            case OP_GETUPVAL: 
            {
                // [upvalue] upvalue索引
                int u = GETARG_B(i);
                *name = p->upvalues ? getstr(p->upvalues[u]) : "?";
                return "upvalue";
            }
            
            case OP_SELF: 
            {
                // [方法调用] 键索引
                int k = GETARG_C(i);
                *name = kname(p, k);
                return "method";
            }
            
            default: 
                break;
        }
    }
    
    // [未找到] 没有找到有用的名称
    return NULL;
}


// [进阶] 获取函数名称
// 功能详述：
// 分析调用指令获取被调用函数的名称和类型
// 通过回溯调用指令确定函数的来源
//
// 分析逻辑：
// 1. 检查调用者是否为Lua函数
// 2. 获取调用指令（CALL/TAILCALL/TFORLOOP）
// 3. 分析调用的目标对象获取名称
//
// @param L - lua_State*：Lua状态机
// @param ci - CallInfo*：调用信息
// @param name - const char**：输出名称
// @return const char*：函数类型描述，未找到返回NULL
//
// 算法复杂度：O(n) 时间，n为指令数量
//
// 限制条件：
// - 调用者必须是Lua函数
// - 不能有尾调用优化丢失的信息
//
static const char *getfuncname(lua_State *L, CallInfo *ci, const char **name)
{
    Instruction i;
    
    // [调用者检查] 调用函数不是Lua（或未知）
    if ((isLua(ci) && ci->tailcalls > 0) || !isLua(ci - 1))
    {
        return NULL;
    }
        
    // [调用指令] 调用函数
    ci--;
    i = ci_func(ci)->l.p->code[currentpc(L, ci)];
    
    if (GET_OPCODE(i) == OP_CALL || GET_OPCODE(i) == OP_TAILCALL ||
        GET_OPCODE(i) == OP_TFORLOOP)
    {
        return getobjname(L, ci, GETARG_A(i), name);
    }
    else
    {
        return NULL;
    }
}


// [基础] 检查指针是否指向数组的ANSI方法
// 功能详述：
// 检查给定的TValue指针是否指向调用信息的栈范围内
// 这是一个安全的指针范围检查函数
//
// @param ci - CallInfo*：调用信息
// @param o - const TValue*：值指针
// @return int：在栈中返回1，否则返回0
//
// 算法复杂度：O(n) 时间，n为栈大小
//
// 安全性：
// - 避免野指针访问
// - 确保错误处理的安全性
//
static int isinstack(CallInfo *ci, const TValue *o)
{
    StkId p;
    
    // [遍历检查] 遍历栈范围查找指针
    for (p = ci->base; p < ci->top; p++)
    {
        if (o == p) 
        {
            return 1;
        }
    }
            
    return 0;
}


/*
** [核心] 类型错误处理
**
** 功能详述：
** 生成详细的类型错误消息，包含变量名称和类型信息
** 这是Lua运行时错误报告的核心函数
**
** 错误信息构成：
** 1. 操作类型（op参数）
** 2. 变量来源（局部变量、全局变量等）
** 3. 变量名称（如果可获取）
** 4. 实际类型
**
** @param L - lua_State*：Lua状态机
** @param o - const TValue*：错误值
** @param op - const char*：操作名称
**
** 算法复杂度：O(n) 时间，n为指令数量（名称分析）
*/
void luaG_typeerror(lua_State *L, const TValue *o, const char *op)
{
    const char *name = NULL;
    const char *t = luaT_typenames[ttype(o)];
    const char *kind = (isinstack(L->ci, o)) ?
                       getobjname(L, L->ci, cast_int(o - L->base), &name) :
                       NULL;
                       
    if (kind)
    {
        luaG_runerror(L, "attempt to %s %s " LUA_QS " (a %s value)",
                      op, kind, name, t);
    }
    else
    {
        luaG_runerror(L, "attempt to %s a %s value", op, t);
    }
}


// [基础] 连接错误处理
// 功能详述：
// 处理字符串连接操作的类型错误，智能确定错误的操作数
// 字符串连接要求操作数为字符串或数字类型
//
// 错误定位策略：
// 1. 如果第一个操作数有效，错误在第二个
// 2. 否则错误在第一个操作数
//
// @param L - lua_State*：Lua状态机
// @param p1 - StkId：第一个操作数
// @param p2 - StkId：第二个操作数
//
// 算法复杂度：O(n) 时间（调用typeerror）
//
void luaG_concaterror(lua_State *L, StkId p1, StkId p2)
{
    // [错误定位] 确定哪个操作数导致错误
    if (ttisstring(p1) || ttisnumber(p1)) 
    {
        p1 = p2;
    }
        
    lua_assert(!ttisstring(p1) && !ttisnumber(p1));
    luaG_typeerror(L, p1, "concatenate");
}


// [基础] 算术错误处理
// 功能详述：
// 处理算术操作的类型错误，智能确定错误的操作数
// 算术操作要求操作数可转换为数字类型
//
// 错误定位策略：
// 1. 尝试将第一个操作数转换为数字
// 2. 如果转换失败，错误在第一个操作数
// 3. 否则错误在第二个操作数
//
// @param L - lua_State*：Lua状态机
// @param p1 - const TValue*：第一个操作数
// @param p2 - const TValue*：第二个操作数
//
// 算法复杂度：O(n) 时间（调用typeerror）
//
void luaG_aritherror(lua_State *L, const TValue *p1, const TValue *p2)
{
    TValue temp;
    
    // [错误定位] 确定哪个操作数无法转换为数字
    if (luaV_tonumber(p1, &temp) == NULL)
    {
        p2 = p1;
    }
        
    luaG_typeerror(L, p2, "perform arithmetic on");
}


// [基础] 比较错误处理
// 功能详述：
// 处理比较操作的类型错误，生成有意义的错误消息
// 比较操作要求操作数为相同类型或可比较的类型
//
// 错误消息策略：
// 1. 相同类型：报告尝试比较两个X值
// 2. 不同类型：报告尝试比较X与Y
//
// @param L - lua_State*：Lua状态机
// @param p1 - const TValue*：第一个操作数
// @param p2 - const TValue*：第二个操作数
// @return int：总是返回0（错误处理约定）
//
// 算法复杂度：O(1) 时间
//
int luaG_ordererror(lua_State *L, const TValue *p1, const TValue *p2)
{
    const char *t1 = luaT_typenames[ttype(p1)];
    const char *t2 = luaT_typenames[ttype(p2)];
    
    // [错误分类] 根据类型相同性生成不同消息
    if (t1[2] == t2[2])
    {
        luaG_runerror(L, "attempt to compare two %s values", t1);
    }
    else
    {
        luaG_runerror(L, "attempt to compare %s with %s", t1, t2);
    }
        
    return 0;
}


// [基础] 添加调试信息到错误消息
// 功能详述：
// 为错误消息添加源文件和行号信息，增强错误的可追踪性
// 只对Lua代码添加位置信息，C代码无需此信息
//
// 信息格式：
// "文件名:行号: 错误消息"
//
// @param L - lua_State*：Lua状态机
// @param msg - const char*：原始错误消息
//
// 算法复杂度：O(1) 时间
//
// 栈操作：
// - 将增强后的错误消息压入栈顶
//
static void addinfo(lua_State *L, const char *msg)
{
    CallInfo *ci = L->ci;
    
    // [代码类型] 检查是否为Lua代码
    if (isLua(ci)) 
    {
        // [位置信息] 添加文件:行号信息
        char buff[LUA_IDSIZE];
        int line = currentline(L, ci);
        luaO_chunkid(buff, getstr(getluaproto(ci)->source), LUA_IDSIZE);
        luaO_pushfstring(L, "%s:%d: %s", buff, line, msg);
    }
}


// [核心] 错误消息处理
// 功能详述：
// Lua的顶层错误处理机制，支持自定义错误处理函数
// 这是Lua错误传播的核心函数
//
// 处理流程：
// 1. 检查是否设置了错误处理函数
// 2. 如果有，调用错误处理函数处理错误
// 3. 最终抛出运行时错误
//
// 错误处理函数调用：
// - 将错误消息作为参数传递
// - 错误处理函数可以修改或包装错误消息
//
// @param L - lua_State*：Lua状态机
//
// 算法复杂度：O(1) 时间（不计错误处理函数执行）
//
// 异常安全：
// - 总是会抛出异常（不返回）
// - 正确处理错误处理函数本身的错误
//
void luaG_errormsg(lua_State *L)
{
    // [错误处理] 检查是否有错误处理函数
    if (L->errfunc != 0) 
    {
        StkId errfunc = restorestack(L, L->errfunc);
        
        if (!ttisfunction(errfunc)) 
        {
            luaD_throw(L, LUA_ERRERR);
        }
            
        // [参数准备] 移动参数
        setobjs2s(L, L->top, L->top - 1);
        
        // [函数准备] 压入函数
        setobjs2s(L, L->top - 1, errfunc);
        incr_top(L);
        
        // [错误处理] 调用错误处理函数
        luaD_call(L, L->top - 2, 1);
    }
    
    // [异常抛出] 抛出运行时错误
    luaD_throw(L, LUA_ERRRUN);
}


/*
** [核心] 运行时错误处理
**
** 功能详述：
** 格式化运行时错误消息并添加调试信息，然后触发错误处理流程
** 这是Lua运行时错误报告的主要入口点
**
** 处理步骤：
** 1. 使用可变参数格式化错误消息
** 2. 添加源文件和行号信息
** 3. 调用错误消息处理机制
**
** @param L - lua_State*：Lua状态机
** @param fmt - const char*：格式字符串
** @param ... - 可变参数：格式化参数
**
** 算法复杂度：O(1) 时间（不计字符串操作）
**
** 异常安全：
** - 总是会抛出异常（不返回）
** - 正确处理参数和堆栈状态
*/
void luaG_runerror(lua_State *L, const char *fmt, ...)
{
    va_list argp;
    
    // [消息格式化] 格式化错误消息
    va_start(argp, fmt);
    addinfo(L, luaO_pushvfstring(L, fmt, argp));
    va_end(argp);
    
    // [错误处理] 触发错误处理流程
    luaG_errormsg(L);
}