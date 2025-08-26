/*
** [核心] Lua代码生成器模块
**
** 功能概述：
** 负责将抽象语法树转换为Lua虚拟机字节码，是编译器的核心组件
**
** 主要功能：
** - 指令生成与优化（ABC、ABx、AsBx格式）
** - 跳转处理与回填机制
** - 表达式求值与寄存器分配
** - 常量折叠优化
** - 控制流结构的代码生成
**
** 模块依赖：
** - lparser.c：接收语法分析结果
** - lopcodes.h：使用虚拟机指令定义
** - lvm.c：为虚拟机执行提供字节码
**
** 算法复杂度：
** - 指令生成：O(n) 时间，n为AST节点数
** - 跳转回填：O(m) 时间，m为跳转指令数
**
** 相关文档：参见 docs/wiki_code.md
*/

#include <stdlib.h>

#define lcode_c
#define LUA_CORE

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "ltable.h"


/*
** [入门] 检查表达式是否包含跳转指令
**
** 实现原理：
** 通过比较表达式的真跳转列表(t)和假跳转列表(f)是否相同来判断
** 如果两者不同，说明表达式包含条件跳转逻辑
**
** 用途：用于条件表达式的代码生成优化
*/
#define hasjumps(e) ((e)->t != (e)->f)


/*
** [入门] 判断表达式是否为数字字面量
**
** 检查条件：
** 1. 表达式类型为VKNUM（数字常量）
** 2. 没有真跳转（t == NO_JUMP）
** 3. 没有假跳转（f == NO_JUMP）
**
** @param e - expdesc*：表达式描述符
** @return int：是数字字面量返回1，否则返回0
**
** 用途：常量折叠优化的前置检查
*/
static int isnumeral(expdesc *e)
{
    return (e->k == VKNUM && e->t == NO_JUMP && e->f == NO_JUMP);
}


/*
** [核心] 生成LOADNIL指令，将连续寄存器设置为nil
**
** 功能详述：
** 这是一个重要的优化函数，会尝试将多个LOADNIL指令合并为一个
** 以减少字节码数量并提高执行效率
**
** 优化策略：
** 1. 检查前一条指令是否也是LOADNIL
** 2. 如果可以合并，扩展前一条指令的范围
** 3. 否则生成新的LOADNIL指令
**
** @param fs - FuncState*：函数编译状态
** @param from - int：起始寄存器编号
** @param n - int：连续寄存器数量
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只有在没有跳转目标时才能进行优化
** - 函数开始位置有特殊处理逻辑
*/
void luaK_nil(FuncState *fs, int from, int n)
{
    Instruction *previous;
    
    /*
    ** [优化] 检查是否没有跳转到当前位置
    ** 如果有跳转，就不能做优化，因为可能改变程序语义
    */
    if (fs->pc > fs->lasttarget) 
    {
        /*
        ** [特殊情况] 函数开始的处理
        ** 在函数开始时，某些寄存器可能已经是nil
        */
        if (fs->pc == 0) 
        {
            if (from >= fs->nactvar)
            {
                return;
            }
        }
        else 
        {
            previous = &fs->f->code[fs->pc - 1];
            
            /*
            ** [优化核心] 检查前一条指令是否为LOADNIL
            ** 如果是，尝试合并两个LOADNIL指令
            */
            if (GET_OPCODE(*previous) == OP_LOADNIL) 
            {
                int pfrom = GETARG_A(*previous);
                int pto = GETARG_B(*previous);
                
                /*
                ** [合并条件] 检查寄存器范围是否可以连接
                ** 条件：前一个范围的结束位置与当前范围的开始位置相邻或重叠
                */
                if (pfrom <= from && from <= pto + 1) 
                {
                    if (from + n - 1 > pto)
                    {
                        SETARG_B(*previous, from + n - 1);
                    }
                    return;
                }
            }
        }
    }
    
    /*
    ** [默认路径] 无法优化的情况，生成新的LOADNIL指令
    ** 指令格式：LOADNIL A B，将寄存器A到B设置为nil
    */
    luaK_codeABC(fs, OP_LOADNIL, from, from + n - 1, 0);
}


/*
** 生成无条件跳转指令
** 保存当前待处理的跳转列表，生成JMP指令，然后将跳转列表连接起来
** fs: 函数状态
** 返回: 跳转指令的位置
*/
int luaK_jump(FuncState *fs)
{
    /*
    ** 保存跳转到这里的列表
    */
    int jpc = fs->jpc;
    int j;
    
    /*
    ** 清空待处理跳转列表
    */
    fs->jpc = NO_JUMP;
    
    /*
    ** 生成跳转指令
    */
    j = luaK_codeAsBx(fs, OP_JMP, 0, NO_JUMP);
    
    /*
    ** 将新跳转与之前的跳转列表连接
    */
    luaK_concat(fs, &j, jpc);
    
    return j;
}


/*
** 生成返回指令
** fs: 函数状态
** first: 第一个返回值的寄存器
** nret: 返回值数量
*/
void luaK_ret(FuncState *fs, int first, int nret)
{
    luaK_codeABC(fs, OP_RETURN, first, nret + 1, 0);
}


/*
** 生成条件跳转指令
** fs: 函数状态
** op: 操作码
** A, B, C: 指令参数
** 返回: 跳转指令的位置
*/
static int condjump(FuncState *fs, OpCode op, int A, int B, int C)
{
    luaK_codeABC(fs, op, A, B, C);
    return luaK_jump(fs);
}


/*
** 修复跳转指令的目标地址
** fs: 函数状态
** pc: 跳转指令的位置
** dest: 目标地址
*/
static void fixjump(FuncState *fs, int pc, int dest)
{
    Instruction *jmp = &fs->f->code[pc];
    int offset = dest - (pc + 1);
    
    lua_assert(dest != NO_JUMP);
    if (abs(offset) > MAXARG_sBx)
        luaX_syntaxerror(fs->ls, "control structure too long");
        
    SETARG_sBx(*jmp, offset);
}


/*
** 获取当前PC并标记为跳转目标
** 用于避免连续指令不在同一基本块的错误优化
** fs: 函数状态
** 返回: 当前PC值
*/
int luaK_getlabel(FuncState *fs)
{
    fs->lasttarget = fs->pc;
    return fs->pc;
}


/*
** 获取指定位置跳转指令的目标
** fs: 函数状态
** pc: 指令位置
** 返回: 目标位置或NO_JUMP
*/
static int getjump(FuncState *fs, int pc)
{
    int offset = GETARG_sBx(fs->f->code[pc]);
    
    if (offset == NO_JUMP)  /* 指向自己表示列表结束 */
        return NO_JUMP;
    else
        return (pc + 1) + offset;  /* 将偏移转换为绝对位置 */
}


/*
** 获取跳转控制指令
** fs: 函数状态
** pc: 指令位置
** 返回: 控制指令指针
*/
static Instruction *getjumpcontrol(FuncState *fs, int pc)
{
    Instruction *pi = &fs->f->code[pc];
    
    if (pc >= 1 && testTMode(GET_OPCODE(*(pi - 1))))
        return pi - 1;
    else
        return pi;
}


/*
** 检查列表中是否有不产生值的跳转(或产生反转值)
** fs: 函数状态
** list: 跳转列表
** 返回: 如果需要值返回1，否则返回0
*/
static int need_value(FuncState *fs, int list)
{
    for (; list != NO_JUMP; list = getjump(fs, list)) {
        Instruction i = *getjumpcontrol(fs, list);
        if (GET_OPCODE(i) != OP_TESTSET) 
            return 1;
    }
    return 0;  /* 未找到 */
}


/*
** 修补测试寄存器
** fs: 函数状态
** node: 节点位置
** reg: 寄存器编号
** 返回: 成功返回1，失败返回0
*/
static int patchtestreg(FuncState *fs, int node, int reg)
{
    Instruction *i = getjumpcontrol(fs, node);
    
    if (GET_OPCODE(*i) != OP_TESTSET)
        return 0;  /* 无法修补其他指令 */
        
    if (reg != NO_REG && reg != GETARG_B(*i))
        SETARG_A(*i, reg);
    else  /* 没有寄存器存放值或寄存器已有值 */
        *i = CREATE_ABC(OP_TEST, GETARG_B(*i), 0, GETARG_C(*i));

    return 1;
}


/*
** 移除跳转列表中的值
** fs: 函数状态
** list: 跳转列表
*/
static void removevalues(FuncState *fs, int list)
{
    for (; list != NO_JUMP; list = getjump(fs, list))
        patchtestreg(fs, list, NO_REG);
}


/*
** 修补跳转列表辅助函数
** fs: 函数状态
** list: 跳转列表
** vtarget: 值目标
** reg: 寄存器
** dtarget: 默认目标
*/
static void patchlistaux(FuncState *fs, int list, int vtarget, int reg, int dtarget)
{
    while (list != NO_JUMP) {
        int next = getjump(fs, list);
        
        if (patchtestreg(fs, list, reg))
            fixjump(fs, list, vtarget);
        else
            fixjump(fs, list, dtarget);  /* 跳转到默认目标 */
            
        list = next;
    }
}


/*
** 释放待处理的跳转列表
** fs: 函数状态
*/
static void dischargejpc(FuncState *fs)
{
    patchlistaux(fs, fs->jpc, fs->pc, NO_REG, fs->pc);
    fs->jpc = NO_JUMP;
}


/*
** 修补跳转列表到指定目标
** fs: 函数状态
** list: 跳转列表
** target: 目标位置
*/
void luaK_patchlist(FuncState *fs, int list, int target)
{
    if (target == fs->pc)
        luaK_patchtohere(fs, list);
    else {
        lua_assert(target < fs->pc);
        patchlistaux(fs, list, target, NO_REG, target);
    }
}


/*
** 修补跳转列表到当前位置
** fs: 函数状态
** list: 跳转列表
*/
void luaK_patchtohere(FuncState *fs, int list)
{
    luaK_getlabel(fs);
    luaK_concat(fs, &fs->jpc, list);
}


/*
** 连接两个跳转列表
** fs: 函数状态
** l1: 第一个列表的指针
** l2: 第二个列表
*/
void luaK_concat(FuncState *fs, int *l1, int l2)
{
    if (l2 == NO_JUMP) 
        return;
    else if (*l1 == NO_JUMP)
        *l1 = l2;
    else {
        int list = *l1;
        int next;
        
        /* 找到第一个列表的最后元素 */
        while ((next = getjump(fs, list)) != NO_JUMP)
            list = next;
            
        fixjump(fs, list, l2);
    }
}


/*
** 检查栈空间是否足够
** fs: 函数状态
** n: 需要的额外空间
*/
void luaK_checkstack(FuncState *fs, int n)
{
    int newstack = fs->freereg + n;
    
    if (newstack > fs->f->maxstacksize) {
        if (newstack >= MAXSTACK)
            luaX_syntaxerror(fs->ls, "function or expression too complex");
        fs->f->maxstacksize = cast_byte(newstack);
    }
}


/*
** 预留寄存器
** fs: 函数状态
** n: 预留的寄存器数量
*/
void luaK_reserveregs(FuncState *fs, int n)
{
    luaK_checkstack(fs, n);
    fs->freereg += n;
}


/*
** 释放寄存器
** fs: 函数状态
** reg: 要释放的寄存器
*/
static void freereg(FuncState *fs, int reg)
{
    if (!ISK(reg) && reg >= fs->nactvar) {
        fs->freereg--;
        lua_assert(reg == fs->freereg);
    }
}


/*
** 释放表达式占用的寄存器
** fs: 函数状态
** e: 表达式描述符
*/
static void freeexp(FuncState *fs, expdesc *e)
{
    if (e->k == VNONRELOC)
        freereg(fs, e->u.s.info);
}


/*
** 添加常量到常量表
** fs: 函数状态
** k: 键值
** v: 常量值
** 返回: 常量在表中的索引
*/
static int addk(FuncState *fs, TValue *k, TValue *v)
{
    lua_State *L = fs->L;
    TValue *idx = luaH_set(L, fs->h, k);
    Proto *f = fs->f;
    int oldsize = f->sizek;
    
    if (ttisnumber(idx)) {
        lua_assert(luaO_rawequalObj(&fs->f->k[cast_int(nvalue(idx))], v));
        return cast_int(nvalue(idx));
    }
    else {  /* 常量未找到；创建新条目 */
        setnvalue(idx, cast_num(fs->nk));
        luaM_growvector(L, f->k, fs->nk, f->sizek, TValue,
                        MAXARG_Bx, "constant table overflow");
                        
        while (oldsize < f->sizek) 
            setnilvalue(&f->k[oldsize++]);
            
        setobj(L, &f->k[fs->nk], v);
        luaC_barrier(L, f, v);
        return fs->nk++;
    }
}


/*
** 添加字符串常量
** fs: 函数状态
** s: 字符串对象
** 返回: 常量索引
*/
int luaK_stringK(FuncState *fs, TString *s)
{
    TValue o;
    setsvalue(fs->L, &o, s);
    return addk(fs, &o, &o);
}


/*
** 添加数字常量
** fs: 函数状态
** r: 数字值
** 返回: 常量索引
*/
int luaK_numberK(FuncState *fs, lua_Number r)
{
    TValue o;
    setnvalue(&o, r);
    return addk(fs, &o, &o);
}


/*
** 添加布尔常量
** fs: 函数状态
** b: 布尔值
** 返回: 常量索引
*/
static int boolK(FuncState *fs, int b)
{
    TValue o;
    setbvalue(&o, b);
    return addk(fs, &o, &o);
}


/*
** 添加nil常量
** fs: 函数状态
** 返回: 常量索引
*/
static int nilK(FuncState *fs)
{
    TValue k, v;
    setnilvalue(&v);
    
    /* 不能使用nil作为键；改用表本身表示nil */
    sethvalue(fs->L, &k, fs->h);
    return addk(fs, &k, &v);
}


/*
** 设置函数调用的返回值数量
** fs: 函数状态
** e: 表达式描述符
** nresults: 期望的返回值数量
*/
void luaK_setreturns(FuncState *fs, expdesc *e, int nresults)
{
    if (e->k == VCALL) {  /* 表达式是开放的函数调用? */
        SETARG_C(getcode(fs, e), nresults + 1);
    }
    else if (e->k == VVARARG) {
        SETARG_B(getcode(fs, e), nresults + 1);
        SETARG_A(getcode(fs, e), fs->freereg);
        luaK_reserveregs(fs, 1);
    }
}


/*
** 设置表达式为单一返回值
** fs: 函数状态
** e: 表达式描述符
*/
void luaK_setoneret(FuncState *fs, expdesc *e)
{
    if (e->k == VCALL) {  /* 表达式是开放的函数调用? */
        e->k = VNONRELOC;
        e->u.s.info = GETARG_A(getcode(fs, e));
    }
    else if (e->k == VVARARG) {
        SETARG_B(getcode(fs, e), 2);
        e->k = VRELOCABLE;  /* 可以重定位其简单结果 */
    }
}


/*
** 释放变量表达式
** fs: 函数状态
** e: 表达式描述符
*/
void luaK_dischargevars(FuncState *fs, expdesc *e)
{
    switch (e->k) {
        case VLOCAL: {
            e->k = VNONRELOC;
            break;
        }
        
        case VUPVAL: {
            e->u.s.info = luaK_codeABC(fs, OP_GETUPVAL, 0, e->u.s.info, 0);
            e->k = VRELOCABLE;
            break;
        }
        
        case VGLOBAL: {
            e->u.s.info = luaK_codeABx(fs, OP_GETGLOBAL, 0, e->u.s.info);
            e->k = VRELOCABLE;
            break;
        }
        
        case VINDEXED: {
            freereg(fs, e->u.s.aux);
            freereg(fs, e->u.s.info);
            e->u.s.info = luaK_codeABC(fs, OP_GETTABLE, 0, e->u.s.info, e->u.s.aux);
            e->k = VRELOCABLE;
            break;
        }
        
        case VVARARG:
        case VCALL: {
            luaK_setoneret(fs, e);
            break;
        }
        
        default: 
            break;  /* 有一个值可用(在某处) */
    }
}


/*
** 生成布尔标签代码
** fs: 函数状态
** A: 目标寄存器
** b: 布尔值
** jump: 是否跳过下一条指令
** 返回: 指令位置
*/
static int code_label(FuncState *fs, int A, int b, int jump)
{
    luaK_getlabel(fs);  /* 这些指令可能是跳转目标 */
    return luaK_codeABC(fs, OP_LOADBOOL, A, b, jump);
}


/*
** 将表达式值放入指定寄存器
** fs: 函数状态
** e: 表达式描述符
** reg: 目标寄存器
*/
static void discharge2reg(FuncState *fs, expdesc *e, int reg)
{
    luaK_dischargevars(fs, e);
    
    switch (e->k) {
        case VNIL: {
            luaK_nil(fs, reg, 1);
            break;
        }
        
        case VFALSE:  
        case VTRUE: {
            luaK_codeABC(fs, OP_LOADBOOL, reg, e->k == VTRUE, 0);
            break;
        }
        
        case VK: {
            luaK_codeABx(fs, OP_LOADK, reg, e->u.s.info);
            break;
        }
        
        case VKNUM: {
            luaK_codeABx(fs, OP_LOADK, reg, luaK_numberK(fs, e->u.nval));
            break;
        }
        
        case VRELOCABLE: {
            Instruction *pc = &getcode(fs, e);
            SETARG_A(*pc, reg);
            break;
        }
        
        case VNONRELOC: {
            if (reg != e->u.s.info)
                luaK_codeABC(fs, OP_MOVE, reg, e->u.s.info, 0);
            break;
        }
        
        default: {
            lua_assert(e->k == VVOID || e->k == VJMP);
            return;  /* 无需做任何事... */
        }
    }
    
    e->u.s.info = reg;
    e->k = VNONRELOC;
}


/*
** 将表达式值放入任意寄存器
** fs: 函数状态
** e: 表达式描述符
*/
static void discharge2anyreg(FuncState *fs, expdesc *e)
{
    if (e->k != VNONRELOC) {
        luaK_reserveregs(fs, 1);
        discharge2reg(fs, e, fs->freereg - 1);
    }
}


/*
** 将表达式转换为寄存器值
** fs: 函数状态
** e: 表达式描述符
** reg: 目标寄存器
*/
static void exp2reg(FuncState *fs, expdesc *e, int reg)
{
    discharge2reg(fs, e, reg);
    
    if (e->k == VJMP)
        luaK_concat(fs, &e->t, e->u.s.info);  /* 将跳转放入't'列表 */
        
    if (hasjumps(e)) {
        int final;   /* 整个表达式后的位置 */
        int p_f = NO_JUMP;  /* 可能的LOAD false位置 */
        int p_t = NO_JUMP;  /* 可能的LOAD true位置 */
        
        if (need_value(fs, e->t) || need_value(fs, e->f)) {
            int fj = (e->k == VJMP) ? NO_JUMP : luaK_jump(fs);
            p_f = code_label(fs, reg, 0, 1);
            p_t = code_label(fs, reg, 1, 0);
            luaK_patchtohere(fs, fj);
        }
        
        final = luaK_getlabel(fs);
        patchlistaux(fs, e->f, final, reg, p_f);
        patchlistaux(fs, e->t, final, reg, p_t);
    }
    
    e->f = e->t = NO_JUMP;
    e->u.s.info = reg;
    e->k = VNONRELOC;
}


/*
** 将表达式转换为下一个可用寄存器
** fs: 函数状态
** e: 表达式描述符
*/
void luaK_exp2nextreg(FuncState *fs, expdesc *e)
{
    luaK_dischargevars(fs, e);
    freeexp(fs, e);
    luaK_reserveregs(fs, 1);
    exp2reg(fs, e, fs->freereg - 1);
}


/*
** 将表达式转换为任意寄存器
** fs: 函数状态
** e: 表达式描述符
** 返回: 寄存器编号
*/
int luaK_exp2anyreg(FuncState *fs, expdesc *e)
{
    luaK_dischargevars(fs, e);
    
    if (e->k == VNONRELOC) {
        if (!hasjumps(e)) 
            return e->u.s.info;  /* 表达式已在寄存器中 */
            
        if (e->u.s.info >= fs->nactvar) {  /* 寄存器不是局部变量? */
            exp2reg(fs, e, e->u.s.info);  /* 将值放入其中 */
            return e->u.s.info;
        }
    }
    
    luaK_exp2nextreg(fs, e);  /* 默认处理 */
    return e->u.s.info;
}


/*
** 将表达式转换为值
** fs: 函数状态
** e: 表达式描述符
*/
void luaK_exp2val(FuncState *fs, expdesc *e)
{
    if (hasjumps(e))
        luaK_exp2anyreg(fs, e);
    else
        luaK_dischargevars(fs, e);
}


/*
** 将表达式转换为RK格式(寄存器或常量)
** fs: 函数状态
** e: 表达式描述符
** 返回: RK值
*/
int luaK_exp2RK(FuncState *fs, expdesc *e)
{
    luaK_exp2val(fs, e);
    
    switch (e->k) {
        case VKNUM:
        case VTRUE:
        case VFALSE:
        case VNIL: {
            if (fs->nk <= MAXINDEXRK) {  /* 常量适合RK操作数? */
                e->u.s.info = (e->k == VNIL)  ? nilK(fs) :
                              (e->k == VKNUM) ? luaK_numberK(fs, e->u.nval) :
                                                boolK(fs, (e->k == VTRUE));
                e->k = VK;
                return RKASK(e->u.s.info);
            }
            else break;
        }
        
        case VK: {
            if (e->u.s.info <= MAXINDEXRK)  /* 常量适合argC? */
                return RKASK(e->u.s.info);
            else break;
        }
        
        default: 
            break;
    }
    
    /* 不是合适范围内的常量：放入寄存器 */
    return luaK_exp2anyreg(fs, e);
}


/*
** 存储变量值
** fs: 函数状态
** var: 变量表达式
** ex: 要存储的表达式
*/
void luaK_storevar(FuncState *fs, expdesc *var, expdesc *ex)
{
    switch (var->k) {
        case VLOCAL: {
            freeexp(fs, ex);
            exp2reg(fs, ex, var->u.s.info);
            return;
        }
        
        case VUPVAL: {
            int e = luaK_exp2anyreg(fs, ex);
            luaK_codeABC(fs, OP_SETUPVAL, e, var->u.s.info, 0);
            break;
        }
        
        case VGLOBAL: {
            int e = luaK_exp2anyreg(fs, ex);
            luaK_codeABx(fs, OP_SETGLOBAL, e, var->u.s.info);
            break;
        }
        
        case VINDEXED: {
            int e = luaK_exp2RK(fs, ex);
            luaK_codeABC(fs, OP_SETTABLE, var->u.s.info, var->u.s.aux, e);
            break;
        }
        
        default: {
            lua_assert(0);  /* 无效的变量类型 */
            break;
        }
    }
    
    freeexp(fs, ex);
}


/*
** 生成self调用的代码
** fs: 函数状态
** e: 对象表达式
** key: 方法名表达式
*/
void luaK_self(FuncState *fs, expdesc *e, expdesc *key)
{
    int func;
    
    luaK_exp2anyreg(fs, e);
    freeexp(fs, e);
    func = fs->freereg;
    luaK_reserveregs(fs, 2);
    luaK_codeABC(fs, OP_SELF, func, e->u.s.info, luaK_exp2RK(fs, key));
    freeexp(fs, key);
    e->u.s.info = func;
    e->k = VNONRELOC;
}


/*
** 反转跳转条件
** fs: 函数状态
** e: 表达式描述符
*/
static void invertjump(FuncState *fs, expdesc *e)
{
    Instruction *pc = getjumpcontrol(fs, e->u.s.info);
    lua_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != OP_TESTSET &&
                                             GET_OPCODE(*pc) != OP_TEST);
    SETARG_A(*pc, !(GETARG_A(*pc)));
}


/*
** 根据条件生成跳转
** fs: 函数状态
** e: 表达式描述符
** cond: 条件
** 返回: 跳转指令位置
*/
static int jumponcond(FuncState *fs, expdesc *e, int cond)
{
    if (e->k == VRELOCABLE) {
        Instruction ie = getcode(fs, e);
        if (GET_OPCODE(ie) == OP_NOT) {
            fs->pc--;  /* 移除前一个OP_NOT */
            return condjump(fs, OP_TEST, GETARG_B(ie), 0, !cond);
        }
        /* 否则继续 */
    }
    
    discharge2anyreg(fs, e);
    freeexp(fs, e);
    return condjump(fs, OP_TESTSET, NO_REG, e->u.s.info, cond);
}


/*
** 为真时跳转
** fs: 函数状态
** e: 表达式描述符
*/
void luaK_goiftrue(FuncState *fs, expdesc *e)
{
    int pc;  /* 最后跳转的pc */
    
    luaK_dischargevars(fs, e);
    
    switch (e->k) {
        case VK: 
        case VKNUM: 
        case VTRUE: {
            pc = NO_JUMP;  /* 总是真；无需做任何事 */
            break;
        }
        
        case VJMP: {
            invertjump(fs, e);
            pc = e->u.s.info;
            break;
        }
        
        default: {
            pc = jumponcond(fs, e, 0);
            break;
        }
    }
    
    luaK_concat(fs, &e->f, pc);  /* 将最后跳转插入'f'列表 */
    luaK_patchtohere(fs, e->t);
    e->t = NO_JUMP;
}


/*
** 为假时跳转
** fs: 函数状态
** e: 表达式描述符
*/
static void luaK_goiffalse(FuncState *fs, expdesc *e)
{
    int pc;  /* 最后跳转的pc */
    
    luaK_dischargevars(fs, e);
    
    switch (e->k) {
        case VNIL: 
        case VFALSE: {
            pc = NO_JUMP;  /* 总是假；无需做任何事 */
            break;
        }
        
        case VJMP: {
            pc = e->u.s.info;
            break;
        }
        
        default: {
            pc = jumponcond(fs, e, 1);
            break;
        }
    }
    
    luaK_concat(fs, &e->t, pc);  /* 将最后跳转插入't'列表 */
    luaK_patchtohere(fs, e->f);
    e->f = NO_JUMP;
}


/*
** 生成逻辑非操作的代码
** fs: 函数状态
** e: 表达式描述符
*/
static void codenot(FuncState *fs, expdesc *e)
{
    luaK_dischargevars(fs, e);
    
    switch (e->k) {
        case VNIL: 
        case VFALSE: {
            e->k = VTRUE;
            break;
        }
        
        case VK: 
        case VKNUM: 
        case VTRUE: {
            e->k = VFALSE;
            break;
        }
        
        case VJMP: {
            invertjump(fs, e);
            break;
        }
        
        case VRELOCABLE:
        case VNONRELOC: {
            discharge2anyreg(fs, e);
            freeexp(fs, e);
            e->u.s.info = luaK_codeABC(fs, OP_NOT, 0, e->u.s.info, 0);
            e->k = VRELOCABLE;
            break;
        }
        
        default: {
            lua_assert(0);  /* 不可能发生 */
            break;
        }
    }
    
    /* 交换真假列表 */
    { int temp = e->f; e->f = e->t; e->t = temp; }
    removevalues(fs, e->f);
    removevalues(fs, e->t);
}


/*
** 生成索引操作的代码
** fs: 函数状态
** t: 表表达式
** k: 键表达式
*/
void luaK_indexed(FuncState *fs, expdesc *t, expdesc *k)
{
    t->u.s.aux = luaK_exp2RK(fs, k);
    t->k = VINDEXED;
}


/*
** 常量折叠优化
** op: 操作码
** e1, e2: 操作数表达式
** 返回: 成功返回1，失败返回0
*/
static int constfolding(OpCode op, expdesc *e1, expdesc *e2)
{
    lua_Number v1, v2, r;
    
    if (!isnumeral(e1) || !isnumeral(e2)) 
        return 0;
        
    v1 = e1->u.nval;
    v2 = e2->u.nval;
    
    switch (op) {
        case OP_ADD: r = luai_numadd(v1, v2); break;
        case OP_SUB: r = luai_numsub(v1, v2); break;
        case OP_MUL: r = luai_nummul(v1, v2); break;
        case OP_DIV:
            if (v2 == 0) return 0;  /* 不尝试除以0 */
            r = luai_numdiv(v1, v2); break;
        case OP_MOD:
            if (v2 == 0) return 0;  /* 不尝试除以0 */
            r = luai_nummod(v1, v2); break;
        case OP_POW: r = luai_numpow(v1, v2); break;
        case OP_UNM: r = luai_numunm(v1); break;
        case OP_LEN: return 0;  /* 'len'不做常量折叠 */
        default: lua_assert(0); r = 0; break;
    }
    
    if (luai_numisnan(r)) 
        return 0;  /* 不尝试产生NaN */
        
    e1->u.nval = r;
    return 1;
}


/*
** 生成算术运算代码
** fs: 函数状态
** op: 操作码
** e1, e2: 操作数表达式
*/
static void codearith(FuncState *fs, OpCode op, expdesc *e1, expdesc *e2)
{
    if (constfolding(op, e1, e2))
        return;
    else {
        int o2 = (op != OP_UNM && op != OP_LEN) ? luaK_exp2RK(fs, e2) : 0;
        int o1 = luaK_exp2RK(fs, e1);
        
        if (o1 > o2) {
            freeexp(fs, e1);
            freeexp(fs, e2);
        }
        else {
            freeexp(fs, e2);
            freeexp(fs, e1);
        }
        
        e1->u.s.info = luaK_codeABC(fs, op, 0, o1, o2);
        e1->k = VRELOCABLE;
    }
}


/*
** 生成比较运算代码
** fs: 函数状态
** op: 操作码
** cond: 条件
** e1, e2: 操作数表达式
*/
static void codecomp(FuncState *fs, OpCode op, int cond, expdesc *e1, expdesc *e2)
{
    int o1 = luaK_exp2RK(fs, e1);
    int o2 = luaK_exp2RK(fs, e2);
    
    freeexp(fs, e2);
    freeexp(fs, e1);
    
    if (cond == 0 && op != OP_EQ) {
        int temp;  /* 交换参数以替换为'<'或'<=' */
        temp = o1; o1 = o2; o2 = temp;  /* o1 <==> o2 */
        cond = 1;
    }
    
    e1->u.s.info = condjump(fs, op, cond, o1, o2);
    e1->k = VJMP;
}


/*
** 处理前缀操作符
** fs: 函数状态
** op: 操作符类型
** e: 表达式描述符
*/
void luaK_prefix(FuncState *fs, UnOpr op, expdesc *e)
{
    expdesc e2;
    e2.t = e2.f = NO_JUMP; 
    e2.k = VKNUM; 
    e2.u.nval = 0;
    
    switch (op) {
        case OPR_MINUS: {
            if (!isnumeral(e))
                luaK_exp2anyreg(fs, e);  /* 不能操作非数字常量 */
            codearith(fs, OP_UNM, e, &e2);
            break;
        }
        
        case OPR_NOT: 
            codenot(fs, e); 
            break;
            
        case OPR_LEN: {
            luaK_exp2anyreg(fs, e);  /* 不能操作常量 */
            codearith(fs, OP_LEN, e, &e2);
            break;
        }
        
        default: 
            lua_assert(0);
    }
}


/*
** 处理中缀操作符(第一个操作数)
** fs: 函数状态
** op: 操作符类型
** v: 表达式描述符
*/
void luaK_infix(FuncState *fs, BinOpr op, expdesc *v)
{
    switch (op) {
        case OPR_AND: {
            luaK_goiftrue(fs, v);
            break;
        }
        
        case OPR_OR: {
            luaK_goiffalse(fs, v);
            break;
        }
        
        case OPR_CONCAT: {
            luaK_exp2nextreg(fs, v);  /* 操作数必须在'栈'上 */
            break;
        }
        
        case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
        case OPR_MOD: case OPR_POW: {
            if (!isnumeral(v)) 
                luaK_exp2RK(fs, v);
            break;
        }
        
        default: {
            luaK_exp2RK(fs, v);
            break;
        }
    }
}


/*
** 处理后缀操作符(第二个操作数)
** fs: 函数状态
** op: 操作符类型
** e1, e2: 操作数表达式
*/
void luaK_posfix(FuncState *fs, BinOpr op, expdesc *e1, expdesc *e2)
{
    switch (op) {
        case OPR_AND: {
            lua_assert(e1->t == NO_JUMP);  /* 列表必须关闭 */
            luaK_dischargevars(fs, e2);
            luaK_concat(fs, &e2->f, e1->f);
            *e1 = *e2;
            break;
        }
        
        case OPR_OR: {
            lua_assert(e1->f == NO_JUMP);  /* 列表必须关闭 */
            luaK_dischargevars(fs, e2);
            luaK_concat(fs, &e2->t, e1->t);
            *e1 = *e2;
            break;
        }
        
        case OPR_CONCAT: {
            luaK_exp2val(fs, e2);
            if (e2->k == VRELOCABLE && GET_OPCODE(getcode(fs, e2)) == OP_CONCAT) {
                lua_assert(e1->u.s.info == GETARG_B(getcode(fs, e2)) - 1);
                freeexp(fs, e1);
                SETARG_B(getcode(fs, e2), e1->u.s.info);
                e1->k = VRELOCABLE; 
                e1->u.s.info = e2->u.s.info;
            }
            else {
                luaK_exp2nextreg(fs, e2);  /* 操作数必须在'栈'上 */
                codearith(fs, OP_CONCAT, e1, e2);
            }
            break;
        }
        
        case OPR_ADD: codearith(fs, OP_ADD, e1, e2); break;
        case OPR_SUB: codearith(fs, OP_SUB, e1, e2); break;
        case OPR_MUL: codearith(fs, OP_MUL, e1, e2); break;
        case OPR_DIV: codearith(fs, OP_DIV, e1, e2); break;
        case OPR_MOD: codearith(fs, OP_MOD, e1, e2); break;
        case OPR_POW: codearith(fs, OP_POW, e1, e2); break;
        case OPR_EQ:  codecomp(fs, OP_EQ, 1, e1, e2); break;
        case OPR_NE:  codecomp(fs, OP_EQ, 0, e1, e2); break;
        case OPR_LT:  codecomp(fs, OP_LT, 1, e1, e2); break;
        case OPR_LE:  codecomp(fs, OP_LE, 1, e1, e2); break;
        case OPR_GT:  codecomp(fs, OP_LT, 0, e1, e2); break;
        case OPR_GE:  codecomp(fs, OP_LE, 0, e1, e2); break;
        
        default: 
            lua_assert(0);
    }
}


/*
** 修正行号信息
** fs: 函数状态
** line: 行号
*/
void luaK_fixline(FuncState *fs, int line)
{
    fs->f->lineinfo[fs->pc - 1] = line;
}


/*
** 生成指令的核心函数
** fs: 函数状态
** i: 指令
** line: 行号
** 返回: 指令位置
*/
static int luaK_code(FuncState *fs, Instruction i, int line)
{
    Proto *f = fs->f;
    
    dischargejpc(fs);  /* 'pc'将要改变 */
    
    /* 将新指令放入代码数组 */
    luaM_growvector(fs->L, f->code, fs->pc, f->sizecode, Instruction,
                    MAX_INT, "code size overflow");
    f->code[fs->pc] = i;
    
    /* 保存对应的行信息 */
    luaM_growvector(fs->L, f->lineinfo, fs->pc, f->sizelineinfo, int,
                    MAX_INT, "code size overflow");
    f->lineinfo[fs->pc] = line;
    
    return fs->pc++;
}


/*
** 生成ABC格式指令
** fs: 函数状态
** o: 操作码
** a, b, c: 指令参数
** 返回: 指令位置
*/
int luaK_codeABC(FuncState *fs, OpCode o, int a, int b, int c)
{
    lua_assert(getOpMode(o) == iABC);
    lua_assert(getBMode(o) != OpArgN || b == 0);
    lua_assert(getCMode(o) != OpArgN || c == 0);
    return luaK_code(fs, CREATE_ABC(o, a, b, c), fs->ls->lastline);
}


/*
** 生成ABx格式指令
** fs: 函数状态
** o: 操作码
** a: A参数
** bc: Bx参数
** 返回: 指令位置
*/
int luaK_codeABx(FuncState *fs, OpCode o, int a, unsigned int bc)
{
    lua_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);
    lua_assert(getCMode(o) == OpArgN);
    return luaK_code(fs, CREATE_ABx(o, a, bc), fs->ls->lastline);
}


/*
** 生成SETLIST指令(用于表初始化)
** fs: 函数状态
** base: 基础寄存器
** nelems: 元素数量
** tostore: 要存储的值数量
*/
void luaK_setlist(FuncState *fs, int base, int nelems, int tostore)
{
    int c = (nelems - 1) / LFIELDS_PER_FLUSH + 1;
    int b = (tostore == LUA_MULTRET) ? 0 : tostore;
    
    lua_assert(tostore != 0);
    
    if (c <= MAXARG_C)
        luaK_codeABC(fs, OP_SETLIST, base, b, c);
    else {
        luaK_codeABC(fs, OP_SETLIST, base, b, 0);
        luaK_code(fs, cast(Instruction, c), fs->ls->lastline);
    }
    
    fs->freereg = base + 1;  /* 释放带有列表值的寄存器 */
}