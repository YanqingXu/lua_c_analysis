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


/*
** 前向声明
*/
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
    /*
    ** [类型检查] 验证是否为Lua函数
    ** C函数没有字节码，因此没有程序计数器概念
    */
    if (!isLua(ci)) 
        return -1;
        
    /*
    ** [状态同步] 如果是当前活跃调用，更新保存的程序计数器
    ** 这确保获取到最新的执行位置
    */
    if (ci == L->ci)
    {
        ci->savedpc = L->savedpc;
    }
        
    /*
    ** [相对位置] 计算相对于函数原型的程序计数器位置
    ** pcRel宏将绝对地址转换为相对偏移量
    */
    return pcRel(ci->savedpc, ci_func(ci)->l.p);
}


/*
** 获取当前行号
** L: Lua状态机
** ci: 调用信息
** 返回: 当前行号，如果无法获取返回-1
*/
static int currentline(lua_State *L, CallInfo *ci)
{
    int pc = currentpc(L, ci);
    
    /*
    ** 只有活跃的Lua函数才有当前行信息
    */
    if (pc < 0)
    {
        return -1;
    }
    else
    {
        return getline(ci_func(ci)->l.p, pc);
    }
}


/*
** 设置Hook函数
** 这个函数可以异步调用（例如在信号处理中）
** L: Lua状态机
** func: Hook函数指针
** mask: Hook掩码
** count: Hook计数
** 返回: 总是返回1
*/
LUA_API int lua_sethook(lua_State *L, lua_Hook func, int mask, int count)
{
    /*
    ** 如果函数为空或掩码为0，关闭Hook
    */
    if (func == NULL || mask == 0) {
        mask = 0;
        func = NULL;
    }
    
    /*
    ** 设置Hook相关参数
    */
    L->hook = func;
    L->basehookcount = count;
    resethookcount(L);
    L->hookmask = cast_byte(mask);
    
    return 1;
}


/*
** 获取Hook函数
** L: Lua状态机
** 返回: Hook函数指针
*/
LUA_API lua_Hook lua_gethook(lua_State *L)
{
    return L->hook;
}


/*
** 获取Hook掩码
** L: Lua状态机
** 返回: Hook掩码
*/
LUA_API int lua_gethookmask(lua_State *L)
{
    return L->hookmask;
}


/*
** 获取Hook计数
** L: Lua状态机
** 返回: Hook计数
*/
LUA_API int lua_gethookcount(lua_State *L)
{
    return L->basehookcount;
}


/*
** 获取调用栈信息
** L: Lua状态机
** level: 栈层级
** ar: 调试信息结构
** 返回: 成功返回1，失败返回0
*/
LUA_API int lua_getstack(lua_State *L, int level, lua_Debug *ar)
{
    int status;
    CallInfo *ci;
    
    lua_lock(L);
    
    /*
    ** 向下遍历调用栈
    ** 跳过指定层数，同时处理尾调用
    */
    for (ci = L->ci; level > 0 && ci > L->base_ci; ci--) {
        level--;
        
        /*
        ** 如果是Lua函数，需要跳过丢失的尾调用
        */
        if (f_isLua(ci))
        {
            level -= ci->tailcalls;
        }
    }
    
    /*
    ** 检查是否找到了对应层级
    */
    if (level == 0 && ci > L->base_ci) {
        status = 1;
        ar->i_ci = cast_int(ci - L->base_ci);
    }
    else if (level < 0) {
        /*
        ** 层级是丢失的尾调用
        */
        status = 1;
        ar->i_ci = 0;
    }
    else {
        /*
        ** 没有这样的层级
        */
        status = 0;
    }
    
    lua_unlock(L);
    return status;
}


/*
** 获取Lua函数原型
** ci: 调用信息
** 返回: 函数原型指针，如果不是Lua函数返回NULL
*/
static Proto *getluaproto(CallInfo *ci)
{
    return (isLua(ci) ? ci_func(ci)->l.p : NULL);
}


/*
** 查找局部变量名称
** L: Lua状态机
** ci: 调用信息
** n: 变量编号
** 返回: 变量名称，如果未找到返回NULL
*/
static const char *findlocal(lua_State *L, CallInfo *ci, int n)
{
    const char *name;
    Proto *fp = getluaproto(ci);
    
    /*
    ** 如果是Lua函数，尝试从调试信息中获取变量名
    */
    if (fp && (name = luaF_getlocalname(fp, n, currentpc(L, ci))) != NULL)
    {
        return name;
    }
    else {
        /*
        ** 不是Lua函数或没有调试信息，检查是否在栈范围内
        */
        StkId limit = (ci == L->ci) ? L->top : (ci + 1)->func;
        
        if (limit - ci->base >= n && n > 0)
        {
            return "(*temporary)";
        }
        else
        {
            return NULL;
        }
    }
}


/*
** 获取局部变量
** L: Lua状态机
** ar: 调试信息
** n: 变量编号
** 返回: 变量名称，如果未找到返回NULL
*/
LUA_API const char *lua_getlocal(lua_State *L, const lua_Debug *ar, int n)
{
    CallInfo *ci = L->base_ci + ar->i_ci;
    const char *name = findlocal(L, ci, n);
    
    lua_lock(L);
    
    /*
    ** 如果找到变量，将其值压入栈
    */
    if (name)
        luaA_pushobject(L, ci->base + (n - 1));
        
    lua_unlock(L);
    return name;
}


/*
** 设置局部变量
** L: Lua状态机
** ar: 调试信息
** n: 变量编号
** 返回: 变量名称，如果未找到返回NULL
*/
LUA_API const char *lua_setlocal(lua_State *L, const lua_Debug *ar, int n)
{
    CallInfo *ci = L->base_ci + ar->i_ci;
    const char *name = findlocal(L, ci, n);
    
    lua_lock(L);
    
    /*
    ** 如果找到变量，设置其值
    */
    if (name)
        setobjs2s(L, ci->base + (n - 1), L->top - 1);
        
    /*
    ** 弹出栈顶值
    */
    L->top--;
    
    lua_unlock(L);
    return name;
}


/*
** 填充函数信息
** ar: 调试信息结构
** cl: 闭包对象
*/
static void funcinfo(lua_Debug *ar, Closure *cl)
{
    if (cl->c.isC) {
        /*
        ** C函数的信息
        */
        ar->source = "=[C]";
        ar->linedefined = -1;
        ar->lastlinedefined = -1;
        ar->what = "C";
    }
    else {
        /*
        ** Lua函数的信息
        */
        ar->source = getstr(cl->l.p->source);
        ar->linedefined = cl->l.p->linedefined;
        ar->lastlinedefined = cl->l.p->lastlinedefined;
        ar->what = (ar->linedefined == 0) ? "main" : "Lua";
    }
    
    /*
    ** 生成简短的源文件标识
    */
    luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
}


/*
** 填充尾调用信息
** ar: 调试信息结构
*/
static void info_tailcall(lua_Debug *ar)
{
    ar->name = ar->namewhat = "";
    ar->what = "tail";
    ar->lastlinedefined = ar->linedefined = ar->currentline = -1;
    ar->source = "=(tail call)";
    luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
    ar->nups = 0;
}


/*
** 收集有效行号
** L: Lua状态机
** f: 闭包对象
*/
static void collectvalidlines(lua_State *L, Closure *f)
{
    /*
    ** 如果不是Lua函数，返回nil
    */
    if (f == NULL || f->c.isC) {
        setnilvalue(L->top);
    }
    else {
        /*
        ** 为Lua函数创建行号表
        */
        Table *t = luaH_new(L, 0, 0);
        int *lineinfo = f->l.p->lineinfo;
        int i;
        
        /*
        ** 将所有有效行号加入表中
        */
        for (i = 0; i < f->l.p->sizelineinfo; i++)
            setbvalue(luaH_setnum(L, t, lineinfo[i]), 1);
            
        sethvalue(L, L->top, t);
    }
    
    incr_top(L);
}


/*
** 获取调试信息的辅助函数
** L: Lua状态机
** what: 请求的信息类型
** ar: 调试信息结构
** f: 闭包对象
** ci: 调用信息
** 返回: 成功返回1，失败返回0
*/
static int auxgetinfo(lua_State *L, const char *what, lua_Debug *ar,
                      Closure *f, CallInfo *ci)
{
    int status = 1;
    
    /*
    ** 如果没有函数，填充尾调用信息
    */
    if (f == NULL) {
        info_tailcall(ar);
        return status;
    }
    
    /*
    ** 根据请求类型填充信息
    */
    for (; *what; what++) {
        switch (*what) {
            case 'S': {
                /*
                ** 源文件信息
                */
                funcinfo(ar, f);
                break;
            }
            
            case 'l': {
                /*
                ** 当前行号
                */
                ar->currentline = (ci) ? currentline(L, ci) : -1;
                break;
            }
            
            case 'u': {
                /*
                ** upvalue数量
                */
                ar->nups = f->c.nupvalues;
                break;
            }
            
            case 'n': {
                /*
                ** 函数名称信息
                */
                ar->namewhat = (ci) ? getfuncname(L, ci, &ar->name) : NULL;
                if (ar->namewhat == NULL) {
                    ar->namewhat = "";
                    ar->name = NULL;
                }
                break;
            }
            
            case 'L':
            case 'f':
                /*
                ** 这些选项由lua_getinfo处理
                */
                break;
                
            default:
                /*
                ** 无效选项
                */
                status = 0;
        }
    }
    
    return status;
}


/*
** 获取调试信息
** L: Lua状态机
** what: 请求的信息类型
** ar: 调试信息结构
** 返回: 成功返回1，失败返回0
*/
LUA_API int lua_getinfo(lua_State *L, const char *what, lua_Debug *ar)
{
    int status;
    Closure *f = NULL;
    CallInfo *ci = NULL;
    
    lua_lock(L);
    
    /*
    ** 检查是否从栈顶获取函数
    */
    if (*what == '>') {
        StkId func = L->top - 1;
        luai_apicheck(L, ttisfunction(func));
        
        /*
        ** 跳过'>'字符
        */
        what++;
        f = clvalue(func);
        
        /*
        ** 弹出函数
        */
        L->top--;
    }
    else if (ar->i_ci != 0) {
        /*
        ** 不是尾调用，从调用信息获取函数
        */
        ci = L->base_ci + ar->i_ci;
        lua_assert(ttisfunction(ci->func));
        f = clvalue(ci->func);
    }
    
    /*
    ** 获取调试信息
    */
    status = auxgetinfo(L, what, ar, f, ci);
    
    /*
    ** 如果请求函数对象
    */
    if (strchr(what, 'f')) {
        if (f == NULL) 
            setnilvalue(L->top);
        else 
            setclvalue(L, L->top, f);
            
        incr_top(L);
    }
    
    /*
    ** 如果请求有效行号
    */
    if (strchr(what, 'L'))
        collectvalidlines(L, f);
        
    lua_unlock(L);
    return status;
}


/*
** =====================================================================
** 符号执行和代码检查器
** =====================================================================
*/

/*
** 检查宏定义
*/
#define check(x) if (!(x)) return 0;

/*
** 检查跳转目标
*/
#define checkjump(pt,pc) check(0 <= pc && pc < pt->sizecode)

/*
** 检查寄存器
*/
#define checkreg(pt,reg) check((reg) < (pt)->maxstacksize)


/*
** 预检查函数原型
** pt: 函数原型
** 返回: 检查通过返回1，失败返回0
*/
static int precheck(const Proto *pt)
{
    /*
    ** 检查最大栈大小
    */
    check(pt->maxstacksize <= MAXSTACK);
    
    /*
    ** 检查参数数量
    */
    check(pt->numparams + (pt->is_vararg & VARARG_HASARG) <= pt->maxstacksize);
    
    /*
    ** 检查变参标志
    */
    check(!(pt->is_vararg & VARARG_NEEDSARG) ||
          (pt->is_vararg & VARARG_HASARG));
          
    /*
    ** 检查upvalue数量
    */
    check(pt->sizeupvalues <= pt->nups);
    
    /*
    ** 检查行信息大小
    */
    check(pt->sizelineinfo == pt->sizecode || pt->sizelineinfo == 0);
    
    /*
    ** 检查代码以RETURN结尾
    */
    check(pt->sizecode > 0 && GET_OPCODE(pt->code[pt->sizecode - 1]) == OP_RETURN);
    
    return 1;
}


/*
** 检查开放操作宏
*/
#define checkopenop(pt,pc) luaG_checkopenop((pt)->code[(pc)+1])


/*
** 检查开放操作
** i: 指令
** 返回: 有效返回1，无效返回0
*/
int luaG_checkopenop(Instruction i)
{
    switch (GET_OPCODE(i)) {
        case OP_CALL:
        case OP_TAILCALL:
        case OP_RETURN:
        case OP_SETLIST: {
            check(GETARG_B(i) == 0);
            return 1;
        }
        
        default: 
            /*
            ** 开放调用后的无效指令
            */
            return 0;
    }
}


/*
** 检查参数模式
** pt: 函数原型
** r: 寄存器/常量索引
** mode: 参数模式
** 返回: 检查通过返回1，失败返回0
*/
static int checkArgMode(const Proto *pt, int r, enum OpArgMask mode)
{
    switch (mode) {
        case OpArgN: 
            /*
            ** 不使用参数
            */
            check(r == 0); 
            break;
            
        case OpArgU: 
            /*
            ** 任意值
            */
            break;
            
        case OpArgR: 
            /*
            ** 寄存器
            */
            checkreg(pt, r); 
            break;
            
        case OpArgK:
            /*
            ** 常量或寄存器
            */
            check(ISK(r) ? INDEXK(r) < pt->sizek : r < pt->maxstacksize);
            break;
    }
    
    return 1;
}


/*
** 符号执行函数
** pt: 函数原型
** lastpc: 最后程序计数器
** reg: 寄存器编号
** 返回: 最后改变指定寄存器的指令
*/
static Instruction symbexec(const Proto *pt, int lastpc, int reg)
{
    int pc;
    /*
    ** 存储最后改变reg的指令位置
    */
    int last;
    
    /*
    ** 指向最终返回（一个'中性'指令）
    */
    last = pt->sizecode - 1;
    
    /*
    ** 预检查
    */
    check(precheck(pt));
    
    /*
    ** 遍历所有指令进行符号执行
    */
    for (pc = 0; pc < lastpc; pc++) {
        Instruction i = pt->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);
        int b = 0;
        int c = 0;
        
        /*
        ** 检查操作码有效性
        */
        check(op < NUM_OPCODES);
        
        /*
        ** 检查A参数
        */
        checkreg(pt, a);
        
        /*
        ** 根据指令格式检查参数
        */
        switch (getOpMode(op)) {
            case iABC: {
                b = GETARG_B(i);
                c = GETARG_C(i);
                check(checkArgMode(pt, b, getBMode(op)));
                check(checkArgMode(pt, c, getCMode(op)));
                break;
            }
            
            case iABx: {
                b = GETARG_Bx(i);
                if (getBMode(op) == OpArgK) 
                    check(b < pt->sizek);
                break;
            }
            
            case iAsBx: {
                b = GETARG_sBx(i);
                if (getBMode(op) == OpArgR) {
                    int dest = pc + 1 + b;
                    check(0 <= dest && dest < pt->sizecode);
                    
                    if (dest > 0) {
                        int j;
                        /*
                        ** 检查不会跳转到setlist计数
                        ** 这很复杂，因为之前setlist的计数可能与无效setlist有相同值
                        ** 所以必须回到第一个（如果有的话）
                        */
                        for (j = 0; j < dest; j++) {
                            Instruction d = pt->code[dest - 1 - j];
                            if (!(GET_OPCODE(d) == OP_SETLIST && GETARG_C(d) == 0)) 
                                break;
                        }
                        
                        /*
                        ** 如果j是偶数，前值不是setlist（即使看起来像）
                        */
                        check((j & 1) == 0);
                    }
                }
                break;
            }
        }
        
        /*
        ** 如果指令修改A寄存器
        */
        if (testAMode(op)) {
            if (a == reg) 
                last = pc;
        }
        
        /*
        ** 如果是测试指令
        */
        if (testTMode(op)) {
            /*
            ** 检查跳过
            */
            check(pc + 2 < pt->sizecode);
            check(GET_OPCODE(pt->code[pc + 1]) == OP_JMP);
        }
        
        /*
        ** 特殊指令检查
        */
        switch (op) {
            case OP_LOADBOOL: {
                /*
                ** 如果会跳转
                */
                if (c == 1) {
                    check(pc + 2 < pt->sizecode);
                    check(GET_OPCODE(pt->code[pc + 1]) != OP_SETLIST ||
                          GETARG_C(pt->code[pc + 1]) != 0);
                }
                break;
            }
            
            case OP_LOADNIL: {
                /*
                ** 设置从a到b的寄存器
                */
                if (a <= reg && reg <= b)
                    last = pc;
                break;
            }
            
            case OP_GETUPVAL:
            case OP_SETUPVAL: {
                check(b < pt->nups);
                break;
            }
            
            case OP_GETGLOBAL:
            case OP_SETGLOBAL: {
                check(ttisstring(&pt->k[b]));
                break;
            }
            
            case OP_SELF: {
                checkreg(pt, a + 1);
                if (reg == a + 1) 
                    last = pc;
                break;
            }
            
            case OP_CONCAT: {
                /*
                ** 至少两个操作数
                */
                check(b < c);
                break;
            }
            
            case OP_TFORLOOP: {
                /*
                ** 至少一个结果（控制变量）
                */
                check(c >= 1);
                
                /*
                ** 结果的空间
                */
                checkreg(pt, a + 2 + c);
                
                /*
                ** 影响其基址以上的所有寄存器
                */
                if (reg >= a + 2) 
                    last = pc;
                break;
            }
            
            case OP_FORLOOP:
            case OP_FORPREP:
                checkreg(pt, a + 3);
                /*
                ** 继续到JMP处理
                */
                
            case OP_JMP: {
                int dest = pc + 1 + b;
                
                /*
                ** 不是完全检查且跳转向前且不跳过lastpc
                */
                if (reg != NO_REG && pc < dest && dest <= lastpc)
                    pc += b;
                break;
            }
            
            case OP_CALL:
            case OP_TAILCALL: {
                if (b != 0) {
                    checkreg(pt, a + b - 1);
                }
                
                /*
                ** c = 返回值数量
                */
                c--;
                if (c == LUA_MULTRET) {
                    check(checkopenop(pt, pc));
                }
                else if (c != 0) {
                    checkreg(pt, a + c - 1);
                }
                
                /*
                ** 影响基址以上的所有寄存器
                */
                if (reg >= a) 
                    last = pc;
                break;
            }
            
            case OP_RETURN: {
                /*
                ** b = 返回值数量
                */
                b--;
                if (b > 0) 
                    checkreg(pt, a + b - 1);
                break;
            }
            
            case OP_SETLIST: {
                if (b > 0) 
                    checkreg(pt, a + b);
                    
                if (c == 0) {
                    pc++;
                    check(pc < pt->sizecode - 1);
                }
                break;
            }
            
            case OP_CLOSURE: {
                int nup, j;
                check(b < pt->sizep);
                nup = pt->p[b]->nups;
                check(pc + nup < pt->sizecode);
                
                for (j = 1; j <= nup; j++) {
                    OpCode op1 = GET_OPCODE(pt->code[pc + j]);
                    check(op1 == OP_GETUPVAL || op1 == OP_MOVE);
                }
                
                /*
                ** 如果在跟踪
                */
                if (reg != NO_REG)
                    pc += nup;
                break;
            }
            
            case OP_VARARG: {
                check((pt->is_vararg & VARARG_ISVARARG) &&
                      !(pt->is_vararg & VARARG_NEEDSARG));
                b--;
                if (b == LUA_MULTRET) 
                    check(checkopenop(pt, pc));
                checkreg(pt, a + b - 1);
                break;
            }
            
            default: 
                break;
        }
    }
    
    return pt->code[last];
}

/*
** 取消宏定义
*/
#undef check
#undef checkjump
#undef checkreg


/*
** 检查代码有效性
** pt: 函数原型
** 返回: 代码有效返回非0值
*/
int luaG_checkcode(const Proto *pt)
{
    return (symbexec(pt, pt->sizecode, NO_REG) != 0);
}


/*
** 获取常量名称
** p: 函数原型
** c: 常量索引
** 返回: 常量名称，如果不是字符串返回"?"
*/
static const char *kname(Proto *p, int c)
{
    if (ISK(c) && ttisstring(&p->k[INDEXK(c)]))
        return svalue(&p->k[INDEXK(c)]);
    else
        return "?";
}


/*
** 获取对象名称
** L: Lua状态机
** ci: 调用信息
** stackpos: 栈位置
** name: 输出名称
** 返回: 对象类型描述
*/
static const char *getobjname(lua_State *L, CallInfo *ci, int stackpos,
                              const char **name)
{
    /*
    ** 检查是否为Lua函数
    */
    if (isLua(ci)) {
        Proto *p = ci_func(ci)->l.p;
        int pc = currentpc(L, ci);
        Instruction i;
        
        /*
        ** 首先检查是否为局部变量
        */
        *name = luaF_getlocalname(p, stackpos + 1, pc);
        if (*name)
            return "local";
            
        /*
        ** 尝试符号执行
        */
        i = symbexec(p, pc, stackpos);
        lua_assert(pc != -1);
        
        /*
        ** 根据指令类型确定对象名称
        */
        switch (GET_OPCODE(i)) {
            case OP_GETGLOBAL: {
                /*
                ** 全局变量索引
                */
                int g = GETARG_Bx(i);
                lua_assert(ttisstring(&p->k[g]));
                *name = svalue(&p->k[g]);
                return "global";
            }
            
            case OP_MOVE: {
                int a = GETARG_A(i);
                /*
                ** 从b移动到a
                */
                int b = GETARG_B(i);
                if (b < a)
                    return getobjname(L, ci, b, name);
                break;
            }
            
            case OP_GETTABLE: {
                /*
                ** 键索引
                */
                int k = GETARG_C(i);
                *name = kname(p, k);
                return "field";
            }
            
            case OP_GETUPVAL: {
                /*
                ** upvalue索引
                */
                int u = GETARG_B(i);
                *name = p->upvalues ? getstr(p->upvalues[u]) : "?";
                return "upvalue";
            }
            
            case OP_SELF: {
                /*
                ** 键索引
                */
                int k = GETARG_C(i);
                *name = kname(p, k);
                return "method";
            }
            
            default: 
                break;
        }
    }
    
    /*
    ** 没有找到有用的名称
    */
    return NULL;
}


/*
** 获取函数名称
** L: Lua状态机
** ci: 调用信息
** name: 输出名称
** 返回: 函数类型描述
*/
static const char *getfuncname(lua_State *L, CallInfo *ci, const char **name)
{
    Instruction i;
    
    /*
    ** 调用函数不是Lua（或未知）
    */
    if ((isLua(ci) && ci->tailcalls > 0) || !isLua(ci - 1))
        return NULL;
        
    /*
    ** 调用函数
    */
    ci--;
    i = ci_func(ci)->l.p->code[currentpc(L, ci)];
    
    if (GET_OPCODE(i) == OP_CALL || GET_OPCODE(i) == OP_TAILCALL ||
        GET_OPCODE(i) == OP_TFORLOOP)
        return getobjname(L, ci, GETARG_A(i), name);
    else
        return NULL;
}


/*
** 检查指针是否指向数组的ANSI方法
** ci: 调用信息
** o: 值指针
** 返回: 在栈中返回1，否则返回0
*/
static int isinstack(CallInfo *ci, const TValue *o)
{
    StkId p;
    
    for (p = ci->base; p < ci->top; p++)
        if (o == p) 
            return 1;
            
    return 0;
}


/*
** 类型错误处理
** L: Lua状态机
** o: 错误值
** op: 操作名称
*/
void luaG_typeerror(lua_State *L, const TValue *o, const char *op)
{
    const char *name = NULL;
    const char *t = luaT_typenames[ttype(o)];
    const char *kind = (isinstack(L->ci, o)) ?
                       getobjname(L, L->ci, cast_int(o - L->base), &name) :
                       NULL;
                       
    if (kind)
        luaG_runerror(L, "attempt to %s %s " LUA_QS " (a %s value)",
                      op, kind, name, t);
    else
        luaG_runerror(L, "attempt to %s a %s value", op, t);
}


/*
** 连接错误处理
** L: Lua状态机
** p1, p2: 操作数
*/
void luaG_concaterror(lua_State *L, StkId p1, StkId p2)
{
    if (ttisstring(p1) || ttisnumber(p1)) 
        p1 = p2;
        
    lua_assert(!ttisstring(p1) && !ttisnumber(p1));
    luaG_typeerror(L, p1, "concatenate");
}


/*
** 算术错误处理
** L: Lua状态机
** p1, p2: 操作数
*/
void luaG_aritherror(lua_State *L, const TValue *p1, const TValue *p2)
{
    TValue temp;
    
    if (luaV_tonumber(p1, &temp) == NULL)
        p2 = p1;
        
    luaG_typeerror(L, p2, "perform arithmetic on");
}


/*
** 比较错误处理
** L: Lua状态机
** p1, p2: 操作数
** 返回: 总是返回0
*/
int luaG_ordererror(lua_State *L, const TValue *p1, const TValue *p2)
{
    const char *t1 = luaT_typenames[ttype(p1)];
    const char *t2 = luaT_typenames[ttype(p2)];
    
    if (t1[2] == t2[2])
        luaG_runerror(L, "attempt to compare two %s values", t1);
    else
        luaG_runerror(L, "attempt to compare %s with %s", t1, t2);
        
    return 0;
}


/*
** 添加调试信息到错误消息
** L: Lua状态机
** msg: 错误消息
*/
static void addinfo(lua_State *L, const char *msg)
{
    CallInfo *ci = L->ci;
    
    /*
    ** 检查是否为Lua代码
    */
    if (isLua(ci)) {
        /*
        ** 添加文件:行号信息
        */
        char buff[LUA_IDSIZE];
        int line = currentline(L, ci);
        luaO_chunkid(buff, getstr(getluaproto(ci)->source), LUA_IDSIZE);
        luaO_pushfstring(L, "%s:%d: %s", buff, line, msg);
    }
}


/*
** 错误消息处理
** L: Lua状态机
*/
void luaG_errormsg(lua_State *L)
{
    /*
    ** 检查是否有错误处理函数
    */
    if (L->errfunc != 0) {
        StkId errfunc = restorestack(L, L->errfunc);
        
        if (!ttisfunction(errfunc)) 
            luaD_throw(L, LUA_ERRERR);
            
        /*
        ** 移动参数
        */
        setobjs2s(L, L->top, L->top - 1);
        
        /*
        ** 压入函数
        */
        setobjs2s(L, L->top - 1, errfunc);
        incr_top(L);
        
        /*
        ** 调用错误处理函数
        */
        luaD_call(L, L->top - 2, 1);
    }
    
    luaD_throw(L, LUA_ERRRUN);
}


/*
** 运行时错误处理
** L: Lua状态机
** fmt: 格式字符串
** ...: 参数
*/
void luaG_runerror(lua_State *L, const char *fmt, ...)
{
    va_list argp;
    
    va_start(argp, fmt);
    addinfo(L, luaO_pushvfstring(L, fmt, argp));
    va_end(argp);
    
    luaG_errormsg(L);
}