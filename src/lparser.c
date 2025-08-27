/*
** [专家] Lua语法分析器 - lparser.c
**
** 功能概述：
** 本文件实现了Lua语言的语法分析器，负责将词法分析器产生的token序列
** 转换为抽象语法树(AST)，并生成相应的字节码指令。这是Lua编译器的核心组件。
**
** 主要组件：
** 1. 递归下降语法分析器 - 处理Lua语法结构
** 2. 表达式解析器 - 处理操作符优先级和结合性
** 3. 语句解析器 - 处理控制结构和声明
** 4. 作用域管理器 - 处理变量作用域和上值
** 5. 代码生成接口 - 与代码生成器协作
**
** 技术特点：
** - 递归下降解析算法
** - 操作符优先级处理
** - 错误恢复机制
** - 语义动作集成
**
** 算法复杂度：O(n) 时间，n为源代码长度
** 内存使用：O(d) 空间，d为语法树深度
*/

#include <string.h>

#define lparser_c
#define LUA_CORE

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"


/* 宏定义：检查表达式是否有多返回值 */
#define hasmultret(k)		((k) == VCALL || (k) == VVARARG)

/* 宏定义：获取函数状态中的局部变量 */
#define getlocvar(fs, i)	((fs)->f->locvars[(fs)->actvar[i]])

/* 宏定义：检查限制并在超出时报错 */
#define luaY_checklimit(fs,v,l,m)	if ((v)>(l)) errorlimit(fs,l,m)


/*
** [数据结构] 块计数器结构体
**
** 功能说明：
** 用于管理嵌套代码块的结构，维护跳出循环的跳转列表、
** 活跃局部变量数量和上值信息。
**
** 字段说明：
** @field previous - struct BlockCnt*：指向外层块的指针，形成链式结构
** @field breaklist - int：跳出当前循环的跳转指令列表
** @field nactvar - lu_byte：当前块外部的活跃局部变量数量
** @field upval - lu_byte：当前块中是否有变量被内层函数引用
** @field isbreakable - lu_byte：当前块是否为可跳出的循环结构
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  /* 链表指针：指向外层块 */
  int breaklist;              /* 跳转列表：break语句的跳转目标 */
  lu_byte nactvar;            /* 变量计数：外层活跃局部变量数 */
  lu_byte upval;              /* 上值标志：是否包含被引用的变量 */
  lu_byte isbreakable;        /* 循环标志：是否为循环结构 */
} BlockCnt;


/*
** [函数原型声明] 递归非终结符函数
**
** 说明：由于语法分析中存在相互递归调用，需要提前声明函数原型
*/
static void chunk (LexState *ls);      /* 解析代码块 */
static void expr (LexState *ls, expdesc *v);  /* 解析表达式 */


/*
** [进阶] 锚定当前token
**
** 详细功能说明：
** 保存当前token的信息，防止在错误恢复过程中丢失重要的词法信息。
** 主要用于错误处理和语法恢复。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void anchor_token (LexState *ls) 
{
    if (ls->t.token == TK_NAME || ls->t.token == TK_STRING)
    {
        TString *ts = ls->t.seminfo.ts;
        luaX_newstring(ls, getstr(ts), ts->tsv.len);  /* 创建字符串副本 */
    }
}


/*
** [进阶] 报告期望token错误
**
** 详细功能说明：
** 当解析器期望某个特定token但遇到其他token时，生成详细的错误信息。
** 提供准确的错误定位和有用的错误提示。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param token - int：期望的token类型
**
** 返回值：该函数不返回（抛出错误）
**
** 算法复杂度：O(1) 时间
*/
static void error_expected (LexState *ls, int token) 
{
  luaX_syntaxerror(ls,
      luaO_pushfstring(ls->L, LUA_QS " expected", luaX_token2str(ls, token)));
}


/*
** [进阶] 报告数量限制错误
**
** 详细功能说明：
** 当某个语法结构超出Lua的内部限制时报告错误。例如局部变量数量、
** 上值数量、参数数量等超出限制。
**
** 参数说明：
** @param fs - FuncState*：函数编译状态
** @param limit - int：限制值
** @param what - const char*：描述超限的内容
**
** 返回值：该函数不返回（抛出错误）
**
** 算法复杂度：O(1) 时间
*/
static void errorlimit (FuncState *fs, int limit, const char *what) 
{
  const char *msg = (fs->f->linedefined == 0) ?
    luaO_pushfstring(fs->L, "main function has more than %d %s", limit, what) :
    luaO_pushfstring(fs->L, "function at line %d has more than %d %s",
                            fs->f->linedefined, limit, what);
  luaX_lexerror(fs->ls, msg, 0);
}


/*
** [入门] 测试并消费下一个token
**
** 详细功能说明：
** 检查当前token是否为指定类型，如果是则消费它并返回true，
** 否则返回false且不消费token。常用于可选语法元素的解析。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param c - int：要测试的token类型
**
** 返回值：
** @return int：匹配返回1，不匹配返回0
**
** 算法复杂度：O(1) 时间
*/
static int testnext (LexState *ls, int c) 
{
    if (ls->t.token == c)
    {
        luaX_next(ls);  /* 消费匹配的token */
        return 1;
    }
    else
    {
        return 0;  /* token不匹配 */
    }
}


/*
** [入门] 检查期望的token
**
** 详细功能说明：
** 验证当前token是否为期望的类型，如果不是则报告语法错误。
** 这是语法分析中最基本的验证操作。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param c - int：期望的token类型
**
** 返回值：无（错误时抛出异常）
**
** 算法复杂度：O(1) 时间
*/
static void check (LexState *ls, int c) 
{
    if (ls->t.token != c)
    {
        error_expected(ls, c);  /* 报告期望token错误 */
    }
}


/*
** [入门] 检查并消费期望的token
**
** 详细功能说明：
** 结合check和next操作，先验证当前token是否为期望类型，
** 然后消费它并移动到下一个token。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param c - int：期望的token类型
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void checknext (LexState *ls, int c) 
{
    check(ls, c);     /* 检查token类型 */
    luaX_next(ls);    /* 移动到下一个token */
}


#define check_condition(ls,c,msg)	{ if (!(c)) luaX_syntaxerror(ls, msg); }


/*
** [进阶] 检查匹配的配对token
**
** 详细功能说明：
** 检查配对的语法结构（如括号、方括号等）是否正确匹配。
** 如果不匹配，提供包含原始位置信息的详细错误消息。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param what - int：当前应该匹配的token类型
** @param who - int：与之配对的开始token类型
** @param where - int：配对开始token的行号
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void check_match (LexState *ls, int what, int who, int where) 
{
    if (!testnext(ls, what))
    {
        if (where == ls->linenumber)
        {
            error_expected(ls, what);  /* 在同一行，简单报错 */
        }
        else
        {
            luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
                   LUA_QS " expected (to close " LUA_QS " at line %d)",
                    luaX_token2str(ls, what), luaX_token2str(ls, who), where));
        }
    }
}


/*
** [入门] 检查并返回标识符字符串
**
** 详细功能说明：
** 验证当前token是否为标识符，如果是则返回对应的字符串对象
** 并移动到下一个token。这是处理变量名、函数名等的基础操作。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
**
** 返回值：
** @return TString*：标识符的字符串对象
**
** 算法复杂度：O(1) 时间
*/
static TString *str_checkname (LexState *ls) 
{
    TString *ts;
    check(ls, TK_NAME);           /* 检查是否为标识符token */
    ts = ls->t.seminfo.ts;        /* 获取标识符字符串 */
    luaX_next(ls);                /* 移动到下一个token */
    return ts;
}


/*
** [入门] 初始化表达式描述符
**
** 详细功能说明：
** 初始化表达式描述符结构，设置表达式的类型和相关信息。
** 这是表达式处理的基础操作。
**
** 参数说明：
** @param e - expdesc*：要初始化的表达式描述符
** @param k - expkind：表达式类型
** @param i - int：相关信息（如寄存器号、常量索引等）
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void init_exp (expdesc *e, expkind k, int i) 
{
    e->f = e->t = NO_JUMP;        /* 初始化跳转链表 */
    e->k = k;                     /* 设置表达式类型 */
    e->u.s.info = i;              /* 设置相关信息 */
}


/*
** [入门] 编码字符串常量
**
** 详细功能说明：
** 将字符串常量添加到常量表中，并初始化相应的表达式描述符。
** 处理字符串字面量的编译时表示。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param e - expdesc*：要初始化的表达式描述符
** @param s - TString*：字符串常量
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void codestring (LexState *ls, expdesc *e, TString *s) 
{
    init_exp(e, VK, luaK_stringK(ls->fs, s));  /* 创建字符串常量表达式 */
}


/*
** [入门] 检查标识符并创建表达式
**
** 详细功能说明：
** 处理标识符token，创建对应的变量表达式描述符。
** 这是变量引用解析的基础操作。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param e - expdesc*：要初始化的表达式描述符
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void checkname(LexState *ls, expdesc *e) 
{
    codestring(ls, e, str_checkname(ls));  /* 创建标识符表达式 */
}


/*
** [进阶] 注册局部变量
**
** 详细功能说明：
** 在当前函数的局部变量表中注册一个新的局部变量。
** 检查变量数量限制并返回变量在表中的索引。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param varname - TString*：变量名字符串
**
** 返回值：
** @return int：变量在局部变量表中的索引
**
** 算法复杂度：O(1) 时间
*/
static int registerlocalvar (LexState *ls, TString *varname) 
{
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int oldsize = f->sizelocvars;
    
    /* 扩展局部变量数组 */
    luaM_growvector(ls->L, f->locvars, fs->nlocvars, f->sizelocvars,
                    LocVar, SHRT_MAX, "too many local variables");
    
    /* 填充新扩展的数组元素 */
    while (oldsize < f->sizelocvars)
    {
        f->locvars[oldsize++].varname = NULL;
    }
    
    f->locvars[fs->nlocvars].varname = varname;  /* 设置变量名 */
    luaC_objbarrier(ls->L, f, varname);          /* GC屏障 */
    return fs->nlocvars++;                       /* 返回索引并递增计数 */
}


/* 宏定义：创建字面量局部变量 */
#define new_localvarliteral(ls,v,n) \
  new_localvar(ls, luaX_newstring(ls, "" v, (sizeof(v)/sizeof(char))-1), n)


/*
** [进阶] 创建新的局部变量
**
** 详细功能说明：
** 在当前作用域中创建新的局部变量，检查变量数量限制。
** 支持一次创建多个变量（如多重赋值）。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param name - TString*：变量名
** @param n - int：变量在当前语句中的序号
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void new_localvar (LexState *ls, TString *name, int n) 
{
    FuncState *fs = ls->fs;
    luaY_checklimit(fs, fs->nactvar+n+1, LUAI_MAXVARS, "local variables");
    fs->actvar[fs->nactvar+n] = cast(unsigned short, registerlocalvar(ls, name));
}


/*
** [进阶] 调整局部变量作用域
**
** 详细功能说明：
** 激活新创建的局部变量，设置它们的起始PC位置。
** 这标志着变量从此刻开始在当前作用域中可见。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param nvars - int：要激活的变量数量
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，n为变量数量
*/
static void adjustlocalvars (LexState *ls, int nvars) 
{
    FuncState *fs = ls->fs;
    fs->nactvar = cast_byte(fs->nactvar + nvars);
    for (; nvars; nvars--)
    {
        getlocvar(fs, fs->nactvar - nvars).startpc = fs->pc;  /* 设置变量起始PC */
    }
}


/*
** [进阶] 移除局部变量
**
** 详细功能说明：
** 将局部变量从指定层级移除，设置它们的结束PC位置。
** 用于离开作用域时清理局部变量。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param tolevel - int：要保留的变量层级
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，n为移除的变量数量
*/
static void removevars (LexState *ls, int tolevel) 
{
    FuncState *fs = ls->fs;
    while (fs->nactvar > tolevel)
    {
        getlocvar(fs, --fs->nactvar).endpc = fs->pc;  /* 设置变量结束PC */
    }
}


/*
** [核心] 索引上值变量
**
** 详细功能说明：
** 在当前函数的上值表中查找或创建指定的上值。上值是被内层函数
** 引用的外层函数的局部变量，这是闭包机制的核心。
**
** 参数说明：
** @param fs - FuncState*：函数编译状态
** @param name - TString*：上值变量名
** @param v - expdesc*：变量表达式描述符
**
** 返回值：
** @return int：上值在上值表中的索引
**
** 算法复杂度：O(n) 时间，n为现有上值数量
*/
static int indexupvalue (FuncState *fs, TString *name, expdesc *v) 
{
    int i;
    Proto *f = fs->f;
    int oldsize = f->sizeupvalues;
    
    /* 查找现有的上值 */
    for (i=0; i<f->nups; i++)
    {
        if (fs->upvalues[i].k == v->k && fs->upvalues[i].info == v->u.s.info)
        {
            lua_assert(f->upvalues[i] == name);
            return i;  /* 找到现有上值 */
        }
    }
    
    /* 创建新上值 */
    luaY_checklimit(fs, f->nups + 1, LUAI_MAXUPVALUES, "upvalues");
    luaM_growvector(fs->L, f->upvalues, f->nups, f->sizeupvalues,
                    TString *, MAX_INT, "");
    while (oldsize < f->sizeupvalues)
    {
        f->upvalues[oldsize++] = NULL;
    }
    
    f->upvalues[f->nups] = name;           /* 设置上值名 */
    luaC_objbarrier(fs->L, f, name);       /* GC屏障 */
    lua_assert(v->k == VLOCAL || v->k == VUPVAL);
    fs->upvalues[f->nups].k = cast_byte(v->k);     /* 设置上值类型 */
    fs->upvalues[f->nups].info = cast_byte(v->u.s.info);  /* 设置上值信息 */
    return f->nups++;                      /* 返回新上值索引 */
}


/*
** [进阶] 搜索变量
**
** 详细功能说明：
** 在当前函数作用域中搜索指定名称的局部变量。
** 从最近声明的变量开始向前搜索，实现变量遮蔽机制。
**
** 参数说明：
** @param fs - FuncState*：函数编译状态
** @param n - TString*：要搜索的变量名
**
** 返回值：
** @return int：变量索引，未找到返回-1
**
** 算法复杂度：O(n) 时间，n为活跃变量数量
*/
static int searchvar (FuncState *fs, TString *n) 
{
    int i;
    for (i=fs->nactvar-1; i >= 0; i--)
    {
        if (n == getlocvar(fs, i).varname)
        {
            return i;  /* 找到变量 */
        }
    }
    return -1;  /* 未找到 */
}


/*
** [进阶] 标记上值变量
**
** 详细功能说明：
** 标记指定层级的变量为上值，用于闭包中的变量引用。
** 向上遍历块链表，标记相应的块包含上值。
**
** 参数说明：
** @param fs - FuncState*：函数编译状态
** @param level - int：变量层级
**
** 返回值：无
**
** 算法复杂度：O(d) 时间，d为嵌套深度
*/
static void markupval (FuncState *fs, int level) 
{
    BlockCnt *bl = fs->bl;
    while (bl && bl->nactvar > level)
    {
        bl = bl->previous;  /* 向上遍历块链表 */
    }
    if (bl)
    {
        bl->upval = 1;  /* 标记块包含上值 */
    }
}


/*
** [核心] 单变量辅助解析
**
** 详细功能说明：
** 递归地在函数作用域链中查找变量，确定变量的类型（局部、上值、全局）。
** 这是变量名解析的核心算法，实现了Lua的作用域查找机制。
**
** 参数说明：
** @param fs - FuncState*：当前函数状态
** @param n - TString*：变量名
** @param var - expdesc*：变量表达式描述符
** @param base - int：是否为基础调用
**
** 返回值：
** @return int：变量类型（VLOCAL、VUPVAL、VGLOBAL）
**
** 算法复杂度：O(d*n) 时间，d为作用域深度，n为变量数量
*/
static int singlevaraux (FuncState *fs, TString *n, expdesc *var, int base) 
{
    if (fs == NULL)
    {  /* 没有更多层级？ */
        init_exp(var, VGLOBAL, NO_REG);  /* 默认为全局变量 */
        return VGLOBAL;
    }
    else
    {
        int v = searchvar(fs, n);  /* 在当前层级查找 */
        if (v >= 0)
        {
            init_exp(var, VLOCAL, v);
            if (!base)
            {
                markupval(fs, v);  /* 局部变量将被用作上值 */
            }
            return VLOCAL;
        }
        else
        {  /* 当前层级未找到；尝试上层 */
            if (singlevaraux(fs->prev, n, var, 0) == VGLOBAL)
            {
                return VGLOBAL;
            }
            var->u.s.info = indexupvalue(fs, n, var);  /* 创建上值 */
            var->k = VUPVAL;  /* 上值表达式 */
            return VUPVAL;
        }
    }
}


/*
** [进阶] 解析单个变量
**
** 详细功能说明：
** 解析单个变量引用，确定变量类型并设置相应的表达式描述符。
** 处理局部变量、上值和全局变量的识别。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param var - expdesc*：变量表达式描述符
**
** 返回值：无
**
** 算法复杂度：O(d*n) 时间，d为作用域深度
*/
static void singlevar (LexState *ls, expdesc *var) 
{
    TString *varname = str_checkname(ls);
    FuncState *fs = ls->fs;
    if (singlevaraux(fs, varname, var, 1) == VGLOBAL)
    {
        var->u.s.info = luaK_stringK(fs, varname);  /* 信息指向全局名称 */
    }
}


/*
** [核心] 调整赋值表达式
**
** 详细功能说明：
** 调整赋值语句中变量数量和表达式数量的匹配。处理多重赋值时的
** 值分配，包括函数多返回值和nil值填充。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param nvars - int：变量数量
** @param nexps - int：表达式数量
** @param e - expdesc*：最后一个表达式
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void adjust_assign (LexState *ls, int nvars, int nexps, expdesc *e) 
{
    FuncState *fs = ls->fs;
    int extra = nvars - nexps;
    if (hasmultret(e->k))
    {
        extra++;  /* 包括调用本身 */
        if (extra < 0)
        {
            extra = 0;
        }
        luaK_setreturns(fs, e, extra);  /* 最后的表达式提供差额 */
        if (extra > 1)
        {
            luaK_reserveregs(fs, extra-1);
        }
    }
    else
    {
        if (e->k != VVOID)
        {
            luaK_exp2nextreg(fs, e);  /* 关闭最后的表达式 */
        }
    }
    if (extra > 0)
    {
        int reg = fs->freereg;
        luaK_reserveregs(fs, extra);
        luaK_nil(fs, reg, extra);  /* 用nil填充剩余变量 */
    }
}
}


/*
** [进阶] 进入解析层级
**
** 详细功能说明：
** 检查并递增解析层级计数，防止过深的递归导致栈溢出。
** 这是防御性编程的重要实践。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void enterlevel (LexState *ls) 
{
    if (++ls->L->nCcalls > LUAI_MAXCCALLS)
    {
        luaX_lexerror(ls, "chunk has too many syntax levels", 0);
    }
}


#define leavelevel(ls)	((ls)->L->nCcalls--)


/*
** [进阶] 进入代码块
**
** 详细功能说明：
** 初始化新的代码块控制结构，设置块的属性并链接到块链表中。
** 用于管理嵌套的作用域和跳转控制。
**
** 参数说明：
** @param fs - FuncState*：函数编译状态
** @param bl - BlockCnt*：块计数器结构
** @param isbreakable - lu_byte：是否为可跳出的块
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isbreakable) 
{
    bl->breaklist = NO_JUMP;      /* 初始化跳转列表 */
    bl->isbreakable = isbreakable; /* 设置是否可跳出 */
    bl->nactvar = fs->nactvar;     /* 记录当前活跃变量数 */
    bl->upval = 0;                 /* 初始化上值标志 */
    bl->previous = fs->bl;         /* 链接到前一个块 */
    fs->bl = bl;                   /* 设置为当前块 */
    lua_assert(fs->freereg == fs->nactvar);
}


/*
** [进阶] 离开代码块
**
** 详细功能说明：
** 结束当前代码块，清理局部变量，处理上值关闭，
** 恢复到前一个块的状态。
**
** 参数说明：
** @param fs - FuncState*：函数编译状态
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，n为清理的变量数量
*/
static void leaveblock (FuncState *fs) 
{
    BlockCnt *bl = fs->bl;
    fs->bl = bl->previous;                    /* 恢复前一个块 */
    removevars(fs->ls, bl->nactvar);          /* 移除局部变量 */
    if (bl->upval)
    {
        luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);  /* 关闭上值 */
    }
    /* 块要么控制作用域，要么控制跳转（不会同时） */
    lua_assert(!bl->isbreakable || !bl->upval);
    lua_assert(bl->nactvar == fs->nactvar);
    fs->freereg = fs->nactvar;  /* 释放寄存器 */
    luaK_patchtohere(fs, bl->breaklist);  /* 修补跳转到这里 */
}


/*
** [核心] 推入闭包
**
** 详细功能说明：
** 将函数作为闭包推入表达式栈，处理上值的绑定。
** 生成CLOSURE指令和相应的上值获取指令。
**
** 参数说明：
** @param ls - LexState*：词法分析器状态
** @param func - FuncState*：函数编译状态
** @param v - expdesc*：表达式描述符
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，n为上值数量
*/
static void pushclosure (LexState *ls, FuncState *func, expdesc *v) 
{
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int oldsize = f->sizep;
    int i;
    
    luaM_growvector(ls->L, f->p, fs->np, f->sizep, Proto *,
                    MAXARG_Bx, "constant table overflow");
    while (oldsize < f->sizep)
    {
        f->p[oldsize++] = NULL;
    }
    
    f->p[fs->np++] = func->f;
    luaC_objbarrier(ls->L, f, func->f);
    init_exp(v, VRELOCABLE, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np-1));
    
    for (i=0; i<func->f->nups; i++)
    {
        OpCode o = (func->upvalues[i].k == VLOCAL) ? OP_MOVE : OP_GETUPVAL;
        luaK_codeABC(fs, o, 0, func->upvalues[i].info, 0);
    }
}


/*
** [核心] 初始化函数编译状态
**
** 详细功能说明：
** - 创建新的函数原型（Proto）对象
** - 初始化函数状态（FuncState）的所有字段
** - 建立函数状态链表连接（支持嵌套函数）
** - 初始化常量表和寄存器分配器
** - 在Lua栈上锚定函数原型以防止被垃圾回收
**
** 参数说明：
** @param ls - LexState*：词法分析状态，包含源代码信息
** @param fs - FuncState*：待初始化的函数编译状态
**
** 返回值：
** @return void：无返回值，通过修改fs参数输出结果
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 必须与close_func成对调用
** - 会在Lua栈上创建锚点，影响栈平衡
** - 支持递归调用以处理嵌套函数定义
*/
static void open_func (LexState *ls, FuncState *fs)
{
    lua_State *L = ls->L;
    Proto *f = luaF_newproto(L);
    fs->f = f;
    fs->prev = ls->fs;  /* linked list of funcstates */
    fs->ls = ls;
    fs->L = L;
    ls->fs = fs;
    fs->pc = 0;
    fs->lasttarget = -1;
    fs->jpc = NO_JUMP;
    fs->freereg = 0;
    fs->nk = 0;
    fs->np = 0;
    fs->nlocvars = 0;
    fs->nactvar = 0;
    fs->bl = NULL;
    f->source = ls->source;
    f->maxstacksize = 2;  /* registers 0/1 are always valid */
    fs->h = luaH_new(L, 0, 0);
    /* anchor table of constants and prototype (to avoid being collected) */
    sethvalue2s(L, L->top, fs->h);
    incr_top(L);
    setptvalue2s(L, L->top, f);
    incr_top(L);
}


/**
 * [核心] 关闭函数编译，完成函数原型的构建
 * 
 * 功能：完成当前函数的编译过程，包括：
 * 1. 清理所有局部变量
 * 2. 生成最终返回指令
 * 3. 收缩并整理各种数组（代码、行信息、常量等）
 * 4. 验证生成的字节码正确性
 * 5. 恢复到父函数状态
 * 
 * @param ls 词法状态，包含当前函数编译状态
 * 
 * 时间复杂度：O(n)，n为函数中的指令/常量数量
 * 空间优化：收缩所有数组到实际使用大小
 */
static void close_func (LexState *ls)
{
    lua_State *L = ls->L;
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    
    // 清理所有局部变量，回到函数开始状态
    removevars(ls, 0);
    
    // 生成最终的返回指令（无返回值）
    luaK_ret(fs, 0, 0);
    
    // 收缩代码数组到实际大小，释放多余内存
    luaM_reallocvector(L, f->code, f->sizecode, fs->pc, Instruction);
    f->sizecode = fs->pc;
    
    // 收缩行号信息数组，用于调试
    luaM_reallocvector(L, f->lineinfo, f->sizelineinfo, fs->pc, int);
    f->sizelineinfo = fs->pc;
    
    // 收缩常量数组
    luaM_reallocvector(L, f->k, f->sizek, fs->nk, TValue);
    f->sizek = fs->nk;
    
    // 收缩子函数原型数组
    luaM_reallocvector(L, f->p, f->sizep, fs->np, Proto *);
    f->sizep = fs->np;
    
    // 收缩局部变量信息数组
    luaM_reallocvector(L, f->locvars, f->sizelocvars, fs->nlocvars, LocVar);
    f->sizelocvars = fs->nlocvars;
    
    // 收缩上值名称数组
    luaM_reallocvector(L, f->upvalues, f->sizeupvalues, f->nups, TString *);
    f->sizeupvalues = f->nups;
    
    // 验证生成的字节码正确性
    lua_assert(luaG_checkcode(f));
    lua_assert(fs->bl == NULL);
    
    // 恢复到父函数状态
    ls->fs = fs->prev;
    
    // 最后读取的token锚定在已失效的函数中；必须重新锚定
    if (fs)
    {
        anchor_token(ls);
    }
    
    // 从栈中移除表和原型
    L->top -= 2;
}


/**
 * [核心] Lua解析器主入口函数
 * 
 * 功能：解析Lua源代码并生成函数原型（Proto）
 * 这是整个语法分析的顶层控制函数，负责：
 * 1. 初始化词法分析器和函数状态
 * 2. 设置主函数为变参函数
 * 3. 驱动整个语法分析过程
 * 4. 验证解析完整性
 * 
 * @param L Lua虚拟机状态
 * @param z 输入流对象
 * @param buff 字符串缓冲区
 * @param name 源文件名称，用于错误报告
 * @return 编译完成的函数原型
 * 
 * 时间复杂度：O(n)，n为源代码长度
 * 核心算法：递归下降语法分析
 */
Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff, const char *name)
{
    struct LexState lexstate;
    struct FuncState funcstate;
    
    // 初始化词法状态，关联输入缓冲区
    lexstate.buff = buff;
    
    // 设置输入源，创建源文件名字符串
    luaX_setinput(L, &lexstate, z, luaS_new(L, name));
    
    // 打开主函数，初始化函数编译状态
    open_func(&lexstate, &funcstate);
    
    // 主函数总是变参函数（支持...参数）
    funcstate.f->is_vararg = VARARG_ISVARARG;
    
    // 读取第一个token，启动词法分析
    luaX_next(&lexstate);
    
    // 解析整个源文件（chunk规则）
    chunk(&lexstate);
    
    // 检查是否到达文件结尾
    check(&lexstate, TK_EOS);
    
    // 关闭主函数，完成编译
    close_func(&lexstate);
    
    // 验证编译状态正确性
    lua_assert(funcstate.prev == NULL);    // 主函数无父函数
    lua_assert(funcstate.f->nups == 0);    // 主函数无上值
    lua_assert(lexstate.fs == NULL);       // 词法状态已清理
    
    return funcstate.f;
}



/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/


/*
** [进阶] 解析字段访问操作符（. 或 :）
**
** 详细功能说明：
** - 处理table.field或table:method的语法结构
** - 将前缀表达式转换为任意寄存器形式
** - 解析字段名称并创建索引表达式
** - 生成GETTABLE或类似的字节码指令
**
** 参数说明：
** @param ls - LexState*：词法分析状态
** @param v - expdesc*：前缀表达式，输出时转换为索引表达式
**
** 返回值：
** @return void：无返回值，通过修改v参数输出结果
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 调用前当前token必须是'.'或':'
** - 会消耗一个token（跳过操作符）
** - 字段名必须是有效的标识符
*/
static void field (LexState *ls, expdesc *v)
{
    /* field -> ['.' | ':'] NAME */
    FuncState *fs = ls->fs;
    expdesc key;
    luaK_exp2anyreg(fs, v);
    luaX_next(ls);  /* skip the dot or colon */
    checkname(ls, &key);
    luaK_indexed(fs, v, &key);
}


/*
** [进阶] 解析数组索引操作符 [expr]
**
** 详细功能说明：
** - 处理table[expr]的语法结构
** - 解析方括号内的任意表达式作为索引键
** - 将索引表达式转换为值形式
** - 验证方括号的匹配性
**
** 参数说明：
** @param ls - LexState*：词法分析状态
** @param v - expdesc*：输出参数，存储解析的索引表达式
**
** 返回值：
** @return void：无返回值，通过修改v参数输出结果
**
** 算法复杂度：O(n) 时间，O(1) 空间，n为表达式复杂度
**
** 注意事项：
** - 调用前当前token必须是'['
** - 会消耗多个token（整个[expr]结构）
** - 表达式可以是任意复杂度的Lua表达式
*/
static void yindex (LexState *ls, expdesc *v)
{
    /* index -> '[' expr ']' */
    luaX_next(ls);  /* skip the '[' */
    expr(ls, v);
    luaK_exp2val(ls->fs, v);
    checknext(ls, ']');
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/


struct ConsControl {
  expdesc v;  /* last list item read */
  expdesc *t;  /* table descriptor */
  int nh;  /* total number of `record' elements */
  int na;  /* total number of array elements */
  int tostore;  /* number of array elements pending to be stored */
};


static void recfield (LexState *ls, struct ConsControl *cc)
{
    /* recfield -> (NAME | `['exp1`]') = exp1 */
    FuncState *fs = ls->fs;
    int reg = ls->fs->freereg;
    expdesc key, val;
    int rkkey;
    if (ls->t.token == TK_NAME)
    {
        luaY_checklimit(fs, cc->nh, MAX_INT, "items in a constructor");
        checkname(ls, &key);
    }
    else  /* ls->t.token == '[' */
    {
        yindex(ls, &key);
    }
    cc->nh++;
    checknext(ls, '=');
    rkkey = luaK_exp2RK(fs, &key);
    expr(ls, &val);
    luaK_codeABC(fs, OP_SETTABLE, cc->t->u.s.info, rkkey, luaK_exp2RK(fs, &val));
    fs->freereg = reg;  /* free registers */
}


/*
** [进阶] 关闭列表字段
**
** 详细功能说明：
** 完成表构造中列表部分字段的处理，将表达式转换为下一个寄存器，
** 并在必要时刷新字段到表中。
**
** 参数说明：
** @param fs - FuncState*：函数编译状态
** @param cc - struct ConsControl*：构造控制结构
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void closelistfield (FuncState *fs, struct ConsControl *cc) 
{
    if (cc->v.k == VVOID)
    {
        return;  /* 没有列表项 */
    }
    luaK_exp2nextreg(fs, &cc->v);
    cc->v.k = VVOID;
    if (cc->tostore == LFIELDS_PER_FLUSH)
    {
        luaK_setlist(fs, cc->t->u.s.info, cc->na, cc->tostore);  /* 刷新 */
        cc->tostore = 0;  /* 没有更多待处理项 */
  }
}


/*
** [进阶] 处理最后的列表字段
**
** 详细功能说明：
** 处理表构造中的最后一个列表字段，特别处理多返回值的情况。
** 设置可变数量的表元素。
**
** 参数说明：
** @param fs - FuncState*：函数编译状态
** @param cc - struct ConsControl*：构造控制结构
**
** 返回值：无
**
** 算法复杂度：O(1) 时间
*/
static void lastlistfield (FuncState *fs, struct ConsControl *cc) 
{
    if (cc->tostore == 0)
    {
        return;
    }
    if (hasmultret(cc->v.k))
    {
        luaK_setmultret(fs, &cc->v);
        luaK_setlist(fs, cc->t->u.s.info, cc->na, LUA_MULTRET);
        cc->na--;  /* 不计算最后的表达式（未知元素数量） */
    }
    else
    {
        if (cc->v.k != VVOID)
        {
      luaK_exp2nextreg(fs, &cc->v);
    }
    luaK_setlist(fs, cc->t->u.s.info, cc->na, cc->tostore);
  }
}


static void listfield (LexState *ls, struct ConsControl *cc)
{
    expr(ls, &cc->v);
    luaY_checklimit(ls->fs, cc->na, MAX_INT, "items in a constructor");
    cc->na++;
    cc->tostore++;
}


static void constructor (LexState *ls, expdesc *t)
{
    /* constructor -> ?? */
    FuncState *fs = ls->fs;
    int line = ls->linenumber;
    int pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
    struct ConsControl cc;
    cc.na = cc.nh = cc.tostore = 0;
    cc.t = t;
    init_exp(t, VRELOCABLE, pc);
    init_exp(&cc.v, VVOID, 0);  /* no value (yet) */
    luaK_exp2nextreg(ls->fs, t);  /* fix it at stack top (for gc) */
    checknext(ls, '{');
    do
    {
        lua_assert(cc.v.k == VVOID || cc.tostore > 0);
        if (ls->t.token == '}')
        {
            break;
        }
        closelistfield(fs, &cc);
        switch(ls->t.token)
        {
            case TK_NAME:
            {  /* may be listfields or recfields */
                luaX_lookahead(ls);
                if (ls->lookahead.token != '=')
                {  /* expression? */
                    listfield(ls, &cc);
                }
                else
                {
                    recfield(ls, &cc);
                }
                break;
            }
            case '[':
            {  /* constructor_item -> recfield */
                recfield(ls, &cc);
                break;
            }
            default:
            {  /* constructor_part -> listfield */
                listfield(ls, &cc);
                break;
            }
        }
    }
    while (testnext(ls, ',') || testnext(ls, ';'));
    check_match(ls, '}', '{', line);
    lastlistfield(fs, &cc);
    SETARG_B(fs->f->code[pc], luaO_int2fb(cc.na)); /* set initial array size */
    SETARG_C(fs->f->code[pc], luaO_int2fb(cc.nh));  /* set initial table size */
}

/* }====================================================================== */



static void parlist (LexState *ls)
{
    /* parlist -> [ param { `,' param } ] */
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int nparams = 0;
    f->is_vararg = 0;
    if (ls->t.token != ')')
    {  /* is `parlist' not empty? */
        do
        {
            switch (ls->t.token)
            {
                case TK_NAME:
                {  /* param -> NAME */
                    new_localvar(ls, str_checkname(ls), nparams++);
                    break;
                }
                case TK_DOTS:
                {  /* param -> `...' */
                    luaX_next(ls);
#if defined(LUA_COMPAT_VARARG)
                    /* use `arg' as default name */
                    new_localvarliteral(ls, "arg", nparams++);
                    f->is_vararg = VARARG_HASARG | VARARG_NEEDSARG;
#endif
                    f->is_vararg |= VARARG_ISVARARG;
                    break;
                }
                default: luaX_syntaxerror(ls, "<name> or " LUA_QL("...") " expected");
            }
        }
        while (!f->is_vararg && testnext(ls, ','));
    }
    adjustlocalvars(ls, nparams);
    f->numparams = cast_byte(fs->nactvar - (f->is_vararg & VARARG_HASARG));
    luaK_reserveregs(fs, fs->nactvar);  /* reserve register for parameters */
}


static void body (LexState *ls, expdesc *e, int needself, int line)
{
    /* body ->  `(' parlist `)' chunk END */
    FuncState new_fs;
    open_func(ls, &new_fs);
    new_fs.f->linedefined = line;
    checknext(ls, '(');
    if (needself)
    {
        new_localvarliteral(ls, "self", 0);
        adjustlocalvars(ls, 1);
    }
    parlist(ls);
    checknext(ls, ')');
    chunk(ls);
    new_fs.f->lastlinedefined = ls->linenumber;
    check_match(ls, TK_END, TK_FUNCTION, line);
    close_func(ls);
    pushclosure(ls, &new_fs, e);
}


static int explist1 (LexState *ls, expdesc *v)
{
    /* explist1 -> expr { `,' expr } */
    int n = 1;  /* at least one expression */
    expr(ls, v);
    while (testnext(ls, ','))
    {
        luaK_exp2nextreg(ls->fs, v);
        expr(ls, v);
        n++;
    }
    return n;
}


static void funcargs (LexState *ls, expdesc *f)
{
    FuncState *fs = ls->fs;
    expdesc args;
    int base, nparams;
    int line = ls->linenumber;
    switch (ls->t.token)
    {
        case '(':
        {  /* funcargs -> `(' [ explist1 ] `)' */
            if (line != ls->lastline)
            {
                luaX_syntaxerror(ls,"ambiguous syntax (function call x new statement)");
            }
            luaX_next(ls);
            if (ls->t.token == ')')
            {  /* arg list is empty? */
                args.k = VVOID;
            }
            else
            {
                explist1(ls, &args);
                luaK_setmultret(fs, &args);
            }
            check_match(ls, ')', '(', line);
            break;
        }
        case '{':
        {  /* funcargs -> constructor */
            constructor(ls, &args);
            break;
        }
        case TK_STRING:
        {  /* funcargs -> STRING */
            codestring(ls, &args, ls->t.seminfo.ts);
            luaX_next(ls);  /* must use `seminfo' before `next' */
            break;
        }
        default:
        {
            luaX_syntaxerror(ls, "function arguments expected");
            return;
        }
    }
    lua_assert(f->k == VNONRELOC);
    base = f->u.s.info;  /* base register for call */
    if (hasmultret(args.k))
    {
        nparams = LUA_MULTRET;  /* open call */
    }
    else
    {
        if (args.k != VVOID)
        {
            luaK_exp2nextreg(fs, &args);  /* close last argument */
        }
        nparams = fs->freereg - (base+1);
    }
    init_exp(f, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2));
    luaK_fixline(fs, line);
    fs->freereg = base+1;  /* call remove function and arguments and leaves
                              (unless changed) one result */
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


/*
** [核心] 解析前缀表达式
**
** 详细功能说明：
** - 处理表达式的前缀部分：变量名或括号表达式
** - 支持简单变量名（包括局部变量、上值、全局变量）
** - 支持括号包围的复杂表达式
** - 为后续的后缀操作（函数调用、索引等）准备基础表达式
**
** 参数说明：
** @param ls - LexState*：词法分析状态
** @param v - expdesc*：输出参数，存储解析的前缀表达式
**
** 返回值：
** @return void：无返回值，通过修改v参数输出结果
**
** 算法复杂度：O(n) 时间，O(1) 空间，n为括号内表达式复杂度
**
** 注意事项：
** - 语法规则：prefixexp -> NAME | '(' expr ')'
** - 会自动处理变量类型识别（局部/上值/全局）
** - 括号表达式会递归调用expr函数
*/
static void prefixexp (LexState *ls, expdesc *v)
{
    /* prefixexp -> NAME | '(' expr ')' */
    switch (ls->t.token)
    {
        case '(':
        {
            int line = ls->linenumber;
            luaX_next(ls);
            expr(ls, v);
            check_match(ls, ')', '(', line);
            luaK_dischargevars(ls->fs, v);
            return;
        }
        case TK_NAME:
        {
            singlevar(ls, v);
            return;
        }
        default:
        {
            luaX_syntaxerror(ls, "unexpected symbol");
            return;
        }
    }
}


/*
** [核心] 解析主表达式（包含后缀操作）
**
** 详细功能说明：
** - 解析完整的主表达式，包括前缀和所有后缀操作
** - 支持字段访问：obj.field
** - 支持数组索引：obj[expr]
** - 支持方法调用：obj:method(args)
** - 支持函数调用：func(args)
** - 处理操作符的左结合性和优先级
**
** 参数说明：
** @param ls - LexState*：词法分析状态
** @param v - expdesc*：输出参数，存储解析的完整表达式
**
** 返回值：
** @return void：无返回值，通过修改v参数输出结果
**
** 算法复杂度：O(n) 时间，O(1) 空间，n为后缀操作数量
**
** 注意事项：
** - 语法规则：primaryexp -> prefixexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs }
** - 使用循环处理连续的后缀操作
** - 每个后缀操作都会修改表达式的类型和值
*/
static void primaryexp (LexState *ls, expdesc *v)
{
    /* primaryexp ->
          prefixexp { `.' NAME | `[' exp `]' | `:' NAME funcargs | funcargs } */
    FuncState *fs = ls->fs;
    prefixexp(ls, v);
    for (;;)
    {
        switch (ls->t.token)
        {
            case '.':
            {  /* field */
                field(ls, v);
                break;
            }
            case '[':
            {  /* `[' exp1 `]' */
                expdesc key;
                luaK_exp2anyreg(fs, v);
                yindex(ls, &key);
                luaK_indexed(fs, v, &key);
                break;
            }
            case ':':
            {  /* `:' NAME funcargs */
                expdesc key;
                luaX_next(ls);
                checkname(ls, &key);
                luaK_self(fs, v, &key);
                funcargs(ls, v);
                break;
            }
            case '(': case TK_STRING: case '{':
            {  /* funcargs */
                luaK_exp2nextreg(fs, v);
                funcargs(ls, v);
                break;
            }
            default: return;
        }
    }
}


static void simpleexp (LexState *ls, expdesc *v) 
{
    /* simpleexp -> NUMBER | STRING | NIL | true | false | ... |
                    constructor | FUNCTION body | primaryexp */
    switch (ls->t.token) 
    {
        case TK_NUMBER: 
        {
            init_exp(v, VKNUM, 0);
            v->u.nval = ls->t.seminfo.r;
            break;
        }
        case TK_STRING: 
        {
            codestring(ls, v, ls->t.seminfo.ts);
            break;
        }
        case TK_NIL: 
        {
            init_exp(v, VNIL, 0);
            break;
        }
        case TK_TRUE: 
        {
            init_exp(v, VTRUE, 0);
            break;
        }
        case TK_FALSE: 
        {
            init_exp(v, VFALSE, 0);
            break;
        }
        case TK_DOTS:   /* vararg */
        {
            FuncState *fs = ls->fs;
            check_condition(ls, fs->f->is_vararg,
                            "cannot use " LUA_QL("...") " outside a vararg function");
            fs->f->is_vararg &= ~VARARG_NEEDSARG;  /* don't need 'arg' */
            init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 1, 0));
            break;
        }
        case '{':   /* constructor */
        {
      constructor(ls, v);
      return;
    }
    case TK_FUNCTION: {
      luaX_next(ls);
      body(ls, v, 0, ls->linenumber);
      return;
    }
    default: {
      primaryexp(ls, v);
      return;
    }
  }
  luaX_next(ls);
}


static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '/': return OPR_DIV;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
    default: return OPR_NOBINOPR;
  }
}


static const struct {
  lu_byte left;  /* left priority for each binary operator */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  /* `+' `-' `/' `%' */
   {10, 9}, {5, 4},                 /* power and concat (right associative) */
   {3, 3}, {3, 3},                  /* equality and inequality */
   {3, 3}, {3, 3}, {3, 3}, {3, 3},  /* order */
   {2, 2}, {1, 1}                   /* logical (and/or) */
};

#define UNARY_PRIORITY	8  /* priority for unary operators */


/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where `binop' is any binary operator with a priority higher than `limit'
*/
static BinOpr subexpr (LexState *ls, expdesc *v, unsigned int limit) {
    BinOpr op;
    UnOpr uop;
    enterlevel(ls);
    uop = getunopr(ls->t.token);
    if (uop != OPR_NOUNOPR)
    {
        luaX_next(ls);
        subexpr(ls, v, UNARY_PRIORITY);
        luaK_prefix(ls->fs, uop, v);
    }
    else simpleexp(ls, v);
  /* expand while operators have priorities higher than `limit' */
  op = getbinopr(ls->t.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    luaX_next(ls);
    luaK_infix(ls->fs, op, v);
    /* read sub-expression with higher priority */
    nextop = subexpr(ls, &v2, priority[op].right);
    luaK_posfix(ls->fs, op, v, &v2);
    op = nextop;
  }
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


static void expr (LexState *ls, expdesc *v) {
  subexpr(ls, v, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


static int block_follow (int token) {
  switch (token) {
    case TK_ELSE: case TK_ELSEIF: case TK_END:
    case TK_UNTIL: case TK_EOS:
      return 1;
    default: return 0;
  }
}


static void block (LexState *ls) {
  /* block -> chunk */
  FuncState *fs = ls->fs;
  BlockCnt bl;
  enterblock(fs, &bl, 0);
  chunk(ls);
  lua_assert(bl.breaklist == NO_JUMP);
  leaveblock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  expdesc v;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to a local variable, the local variable
** is needed in a previous assignment (to a table). If so, save original
** local value in a safe place and use this safe copy in the previous
** assignment.
*/
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
    FuncState *fs = ls->fs;
    int extra = fs->freereg;  /* eventual position to save local variable */
    int conflict = 0;
    for (; lh; lh = lh->prev)
    {
        if (lh->v.k == VINDEXED)
        {
            if (lh->v.u.s.info == v->u.s.info)    /* conflict? */
            {
                conflict = 1;
                lh->v.u.s.info = extra;  /* previous assignment will use safe copy */
            }
            if (lh->v.u.s.aux == v->u.s.info)    /* conflict? */
            {
                conflict = 1;
                lh->v.u.s.aux = extra;  /* previous assignment will use safe copy */
            }
        }
    }
    if (conflict)
    {
        luaK_codeABC(fs, OP_MOVE, fs->freereg, v->u.s.info, 0);  /* make copy */
        luaK_reserveregs(fs, 1);
    }
}


static void assignment (LexState *ls, struct LHS_assign *lh, int nvars) 
{
    expdesc e;
    check_condition(ls, VLOCAL <= lh->v.k && lh->v.k <= VINDEXED,
                        "syntax error");
    if (testnext(ls, ','))    /* assignment -> `,' primaryexp assignment */
    {
        struct LHS_assign nv;
        nv.prev = lh;
        primaryexp(ls, &nv.v);
        if (nv.v.k == VLOCAL)
        {
            check_conflict(ls, lh, &nv.v);
        }
        luaY_checklimit(ls->fs, nvars, LUAI_MAXCCALLS - ls->L->nCcalls,
                        "variables in assignment");
        assignment(ls, &nv, nvars+1);
    }
    else    /* assignment -> `=' explist1 */
    {
        int nexps;
        checknext(ls, '=');
        nexps = explist1(ls, &e);
        if (nexps != nvars)
        {
            adjust_assign(ls, nvars, nexps, &e);
            if (nexps > nvars)
            {
                ls->fs->freereg -= nexps - nvars;  /* remove extra values */
            }
        }
        else
        {
            luaK_setoneret(ls->fs, &e);  /* close last expression */
            luaK_storevar(ls->fs, &lh->v, &e);
            return;  /* avoid default */
        }
    }
    init_exp(&e, VNONRELOC, ls->fs->freereg-1);  /* default assignment */
    luaK_storevar(ls->fs, &lh->v, &e);
}


static int cond (LexState *ls) 
{
    /* cond -> exp */
    expdesc v;
    expr(ls, &v);  /* read condition */
    if (v.k == VNIL)
    {
        v.k = VFALSE;  /* `falses' are all equal here */
    }
    luaK_goiftrue(ls->fs, &v);
    return v.f;
}


static void breakstat (LexState *ls) 
{
    FuncState *fs = ls->fs;
    BlockCnt *bl = fs->bl;
    int upval = 0;
    while (bl && !bl->isbreakable)
    {
        upval |= bl->upval;
        bl = bl->previous;
    }
    if (!bl)
    {
        luaX_syntaxerror(ls, "no loop to break");
    }
    if (upval)
    {
        luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
    }
    luaK_concat(fs, &bl->breaklist, luaK_jump(fs));
}


static void whilestat (LexState *ls, int line) {
  /* whilestat -> WHILE cond DO block END */
  FuncState *fs = ls->fs;
  int whileinit;
  int condexit;
  BlockCnt bl;
  luaX_next(ls);  /* skip WHILE */
  whileinit = luaK_getlabel(fs);
  condexit = cond(ls);
  enterblock(fs, &bl, 1);
  checknext(ls, TK_DO);
  block(ls);
  luaK_patchlist(fs, luaK_jump(fs), whileinit);
  check_match(ls, TK_END, TK_WHILE, line);
  leaveblock(fs);
  luaK_patchtohere(fs, condexit);  /* false conditions finish the loop */
}


static void repeatstat (LexState *ls, int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = ls->fs;
  int repeat_init = luaK_getlabel(fs);
  BlockCnt bl1, bl2;
  enterblock(fs, &bl1, 1);  /* loop block */
  enterblock(fs, &bl2, 0);  /* scope block */
  luaX_next(ls);  /* skip REPEAT */
  chunk(ls);
  check_match(ls, TK_UNTIL, TK_REPEAT, line);
  condexit = cond(ls);  /* read condition (inside scope block) */
  if (!bl2.upval) {  /* no upvalues? */
    leaveblock(fs);  /* finish scope */
    luaK_patchlist(ls->fs, condexit, repeat_init);  /* close the loop */
  }
  else {  /* complete semantics when there are upvalues */
    breakstat(ls);  /* if condition then break */
    luaK_patchtohere(ls->fs, condexit);  /* else... */
    leaveblock(fs);  /* finish scope... */
    luaK_patchlist(ls->fs, luaK_jump(fs), repeat_init);  /* and repeat */
  }
  leaveblock(fs);  /* finish loop */
}


static int exp1 (LexState *ls) {
  expdesc e;
  int k;
  expr(ls, &e);
  k = e.k;
  luaK_exp2nextreg(ls->fs, &e);
  return k;
}


static void forbody (LexState *ls, int base, int line, int nvars, int isnum) {
  /* forbody -> DO block */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  int prep, endfor;
  adjustlocalvars(ls, 3);  /* control variables */
  checknext(ls, TK_DO);
  prep = isnum ? luaK_codeAsBx(fs, OP_FORPREP, base, NO_JUMP) : luaK_jump(fs);
  enterblock(fs, &bl, 0);  /* scope for declared variables */
  adjustlocalvars(ls, nvars);
  luaK_reserveregs(fs, nvars);
  block(ls);
  leaveblock(fs);  /* end of scope for declared variables */
  luaK_patchtohere(fs, prep);
  endfor = (isnum) ? luaK_codeAsBx(fs, OP_FORLOOP, base, NO_JUMP) :
                     luaK_codeABC(fs, OP_TFORLOOP, base, 0, nvars);
  luaK_fixline(fs, line);  /* pretend that `OP_FOR' starts the loop */
  luaK_patchlist(fs, (isnum ? endfor : luaK_jump(fs)), prep + 1);
}


static void fornum (LexState *ls, TString *varname, int line) {
  /* fornum -> NAME = exp1,exp1[,exp1] forbody */
  FuncState *fs = ls->fs;
  int base = fs->freereg;
  new_localvarliteral(ls, "(for index)", 0);
  new_localvarliteral(ls, "(for limit)", 1);
  new_localvarliteral(ls, "(for step)", 2);
  new_localvar(ls, varname, 3);
  checknext(ls, '=');
  exp1(ls);  /* initial value */
  checknext(ls, ',');
  exp1(ls);  /* limit */
  if (testnext(ls, ',')) {
    exp1(ls);  /* optional step */
  } else {  /* default step = 1 */
    luaK_codeABx(fs, OP_LOADK, fs->freereg, luaK_numberK(fs, 1));
    luaK_reserveregs(fs, 1);
  }
  forbody(ls, base, line, 1, 1);
}


static void forlist (LexState *ls, TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist1 forbody */
  FuncState *fs = ls->fs;
  expdesc e;
  int nvars = 0;
  int line;
  int base = fs->freereg;
  /* create control variables */
  new_localvarliteral(ls, "(for generator)", nvars++);
  new_localvarliteral(ls, "(for state)", nvars++);
  new_localvarliteral(ls, "(for control)", nvars++);
  /* create declared variables */
  new_localvar(ls, indexname, nvars++);
  while (testnext(ls, ',')) {
    new_localvar(ls, str_checkname(ls), nvars++);
  }
  checknext(ls, TK_IN);
  line = ls->linenumber;
  adjust_assign(ls, 3, explist1(ls, &e), &e);
  luaK_checkstack(fs, 3);  /* extra space to call generator */
  forbody(ls, base, line, nvars - 3, 0);
}


static void forstat (LexState *ls, int line) {
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *fs = ls->fs;
  TString *varname;
  BlockCnt bl;
  enterblock(fs, &bl, 1);  /* scope for loop and control variables */
  luaX_next(ls);  /* skip `for' */
  varname = str_checkname(ls);  /* first variable name */
  switch (ls->t.token) {
    case '=': fornum(ls, varname, line); break;
    case ',': case TK_IN: forlist(ls, varname); break;
    default: luaX_syntaxerror(ls, LUA_QL("=") " or " LUA_QL("in") " expected");
  }
  check_match(ls, TK_END, TK_FOR, line);
  leaveblock(fs);  /* loop scope (`break' jumps to this point) */
}


static int test_then_block (LexState *ls) {
  /* test_then_block -> [IF | ELSEIF] cond THEN block */
  int condexit;
  luaX_next(ls);  /* skip IF or ELSEIF */
  condexit = cond(ls);
  checknext(ls, TK_THEN);
  block(ls);  /* `then' part */
  return condexit;
}


static void ifstat (LexState *ls, int line) 
{
    /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
    FuncState *fs = ls->fs;
    int flist;
    int escapelist = NO_JUMP;
    flist = test_then_block(ls);  /* IF cond THEN block */
    while (ls->t.token == TK_ELSEIF)
    {
        luaK_concat(fs, &escapelist, luaK_jump(fs));
        luaK_patchtohere(fs, flist);
        flist = test_then_block(ls);  /* ELSEIF cond THEN block */
    }
    if (ls->t.token == TK_ELSE)
    {
        luaK_concat(fs, &escapelist, luaK_jump(fs));
        luaK_patchtohere(fs, flist);
        luaX_next(ls);  /* skip ELSE (after patch, for correct line info) */
        block(ls);  /* `else' part */
    }
    else
    {
        luaK_concat(fs, &escapelist, flist);
    }
    luaK_patchtohere(fs, escapelist);
    check_match(ls, TK_END, TK_IF, line);
}


static void localfunc (LexState *ls) {
  expdesc v, b;
  FuncState *fs = ls->fs;
  new_localvar(ls, str_checkname(ls), 0);
  init_exp(&v, VLOCAL, fs->freereg);
  luaK_reserveregs(fs, 1);
  adjustlocalvars(ls, 1);
  body(ls, &b, 0, ls->linenumber);
  luaK_storevar(fs, &v, &b);
  /* debug information will only see the variable after this point! */
  getlocvar(fs, fs->nactvar - 1).startpc = fs->pc;
}


static void localstat (LexState *ls) 
{
    /* stat -> LOCAL NAME {`,' NAME} [`=' explist1] */
    int nvars = 0;
    int nexps;
    expdesc e;
    do
    {
        new_localvar(ls, str_checkname(ls), nvars++);
    } while (testnext(ls, ','));
    if (testnext(ls, '='))
    {
        nexps = explist1(ls, &e);
    }
    else
    {
        e.k = VVOID;
        nexps = 0;
    }
    adjust_assign(ls, nvars, nexps, &e);
    adjustlocalvars(ls, nvars);
}


static int funcname (LexState *ls, expdesc *v) 
{
    /* funcname -> NAME {field} [`:' NAME] */
    int needself = 0;
    singlevar(ls, v);
    while (ls->t.token == '.')
    {
        field(ls, v);
    }
    if (ls->t.token == ':')
    {
        needself = 1;
        field(ls, v);
    }
    return needself;
}


static void funcstat (LexState *ls, int line) {
  /* funcstat -> FUNCTION funcname body */
  int needself;
  expdesc v, b;
  luaX_next(ls);  /* skip FUNCTION */
  needself = funcname(ls, &v);
  body(ls, &b, needself, line);
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);  /* definition `happens' in the first line */
}


static void exprstat (LexState *ls) 
{
    /* stat -> func | assignment */
    FuncState *fs = ls->fs;
    struct LHS_assign v;
    primaryexp(ls, &v.v);
    if (v.v.k == VCALL)    /* stat -> func */
    {
        SETARG_C(getcode(fs, &v.v), 1);  /* call statement uses no results */
    }
    else    /* stat -> assignment */
    {
        v.prev = NULL;
        assignment(ls, &v, 1);
    }
}


static void retstat (LexState *ls) 
{
    /* stat -> RETURN explist */
    FuncState *fs = ls->fs;
    expdesc e;
    int first, nret;  /* registers with returned values */
    luaX_next(ls);  /* 跳过RETURN */
    if (block_follow(ls->t.token) || ls->t.token == ';')
    {
        first = nret = 0;  /* 不返回值 */
    }
    else
    {
        nret = explist1(ls, &e);  /* 可选的返回值 */
    if (hasmultret(e.k)) {
      luaK_setmultret(fs, &e);
      if (e.k == VCALL && nret == 1) {  /* 尾调用？ */
        SET_OPCODE(getcode(fs,&e), OP_TAILCALL);
        lua_assert(GETARG_A(getcode(fs,&e)) == fs->nactvar);
      }
      first = fs->nactvar;
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1) {  /* only one single value? */
        first = luaK_exp2anyreg(fs, &e);
      } else {
        luaK_exp2nextreg(fs, &e);  /* values must go to the `stack' */
        first = fs->nactvar;  /* return all `active' values */
        lua_assert(nret == fs->freereg - first);
      }
    }
  }
  luaK_ret(fs, first, nret);
}


static int statement (LexState *ls) {
  int line = ls->linenumber;  /* may be needed for error messages */
  switch (ls->t.token) {
    case TK_IF: {  /* stat -> ifstat */
      ifstat(ls, line);
      return 0;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      whilestat(ls, line);
      return 0;
    }
    case TK_DO: {  /* stat -> DO block END */
      luaX_next(ls);  /* skip DO */
      block(ls);
      check_match(ls, TK_END, TK_DO, line);
      return 0;
    }
    case TK_FOR: {  /* stat -> forstat */
      forstat(ls, line);
      return 0;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      repeatstat(ls, line);
      return 0;
    }
    case TK_FUNCTION: {
      funcstat(ls, line);  /* stat -> funcstat */
      return 0;
    }
    case TK_LOCAL: {  /* stat -> localstat */
      luaX_next(ls);  /* skip LOCAL */
      if (testnext(ls, TK_FUNCTION)) {  /* local function? */
        localfunc(ls);
      } else {
        localstat(ls);
      }
      return 0;
    }
    case TK_RETURN: {  /* stat -> retstat */
      retstat(ls);
      return 1;  /* must be last statement */
    }
    case TK_BREAK: {  /* stat -> breakstat */
      luaX_next(ls);  /* skip BREAK */
      breakstat(ls);
      return 1;  /* must be last statement */
    }
    default: {
      exprstat(ls);
      return 0;  /* to avoid warnings */
    }
  }
}


static void chunk (LexState *ls) {
  /* chunk -> { stat [`;'] } */
  int islast = 0;
  enterlevel(ls);
  while (!islast && !block_follow(ls->t.token)) {
    islast = statement(ls);
    testnext(ls, ';');
    lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
               ls->fs->freereg >= ls->fs->nactvar);
    ls->fs->freereg = ls->fs->nactvar;  /* 释放寄存器 */
  }
  leavelevel(ls);
}

/* }====================================================================== */

/*
** [文件总结] lparser.c - Lua语法分析器
**
** 本文件实现了完整的Lua语法分析器，包含以下核心组件：
**
** 1. **基础设施层**：
**    - 错误处理和报告机制
**    - Token检查和匹配功能
**    - 语法结构验证工具
**
** 2. **变量管理层**：
**    - 局部变量注册和管理
**    - 作用域控制和变量生命周期
**    - 上值(upvalue)处理机制
**
** 3. **表达式解析层**：
**    - 递归下降表达式解析
**    - 操作符优先级处理
**    - 函数调用和表构造
**
** 4. **语句解析层**：
**    - 控制流语句(if/while/for)
**    - 赋值和声明语句
**    - 函数定义和代码块
**
** 技术特点：
** - 递归下降算法，结构清晰
** - 一遍扫描生成字节码
** - 完善的错误恢复机制
** - 高效的内存管理
**
** 算法复杂度：O(n) 时间，n为源代码长度
** 设计模式：递归下降解析器 + 语义动作
*/
