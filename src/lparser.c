/*
** $Id: lparser.c,v 2.42.1.4 2011/10/21 19:31:42 roberto Exp $
** Lua 语法分析器
** 版权声明见 lua.h
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



/* 检查表达式是否有多个返回值 */
#define hasmultret(k)		((k) == VCALL || (k) == VVARARG)

/* 获取局部变量 */
#define getlocvar(fs, i)	((fs)->f->locvars[(fs)->actvar[i]])

/* 检查限制 */
#define luaY_checklimit(fs,v,l,m)	if ((v)>(l)) errorlimit(fs,l,m)


/*
** 块列表的节点（活动块的列表）
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  /* 链表 */
  int breaklist;  /* 跳出此循环的跳转列表 */
  lu_byte nactvar;  /* 可中断结构外的活动局部变量数量 */
  lu_byte upval;  /* 如果块中某个变量是上值则为真 */
  lu_byte isbreakable;  /* 如果块是循环则为真 */
} BlockCnt;



/*
** 递归非终结符函数的原型
*/
static void chunk (LexState *ls);
static void expr (LexState *ls, expdesc *v);


/* 锚定标记 - 确保字符串和名称标记不被垃圾回收 */
static void anchor_token(LexState *ls)
{
    /* 检查当前标记是否为名称或字符串类型 */
    if (ls->t.token == TK_NAME || ls->t.token == TK_STRING)
    {
        /* 获取标记的字符串信息 */
        TString *ts = ls->t.seminfo.ts;
        /* 创建新字符串以防止被垃圾回收器回收 */
        luaX_newstring(ls, getstr(ts), ts->tsv.len);
    }
}


/* 期望的错误 */
static void error_expected(LexState *ls, int token)
{
    luaX_syntaxerror(ls,
        luaO_pushfstring(ls->L, LUA_QS " expected", luaX_token2str(ls, token)));
}


/* 错误限制 */
static void errorlimit(FuncState *fs, int limit, const char *what)
{
    const char *msg = (fs->f->linedefined == 0) ?
        luaO_pushfstring(fs->L, "main function has more than %d %s", limit, what) :
        luaO_pushfstring(fs->L, "function at line %d has more than %d %s",
                                fs->f->linedefined, limit, what);
    luaX_lexerror(fs->ls, msg, 0);
}


/* 测试下一个标记 */
static int testnext(LexState *ls, int c)
{
    if (ls->t.token == c)
    {
        luaX_next(ls);
        return 1;
    }
    else
    {
        return 0;
    }
}


/* 检查标记 */
static void check(LexState *ls, int c)
{
    if (ls->t.token != c)
    {
        error_expected(ls, c);
    }
}

/* 检查下一个标记 */
static void checknext(LexState *ls, int c)
{
    check(ls, c);
    luaX_next(ls);
}


/* 检查条件 */
#define check_condition(ls, c, msg) { if (!(c)) luaX_syntaxerror(ls, msg); }



/* 检查匹配 */
static void check_match(LexState *ls, int what, int who, int where)
{
    if (!testnext(ls, what))
    {
        if (where == ls->linenumber)
        {
            error_expected(ls, what);
        }
        else
        {
            luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
                LUA_QS " expected (to close " LUA_QS " at line %d)",
                luaX_token2str(ls, what), luaX_token2str(ls, who), where));
        }
    }
}


/* 字符串检查名称 */
static TString *str_checkname(LexState *ls)
{
    TString *ts;
    check(ls, TK_NAME);
    ts = ls->t.seminfo.ts;
    luaX_next(ls);
    return ts;
}


/* 初始化表达式 */
static void init_exp(expdesc *e, expkind k, int i)
{
    e->f = e->t = NO_JUMP;
    e->k = k;
    e->u.s.info = i;
}


/* 编码字符串 */
static void codestring(LexState *ls, expdesc *e, TString *s)
{
    init_exp(e, VK, luaK_stringK(ls->fs, s));
}


/* 检查名称 */
static void checkname(LexState *ls, expdesc *e)
{
    codestring(ls, e, str_checkname(ls));
}


/* 注册局部变量 */
static int registerlocalvar(LexState *ls, TString *varname)
{
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int oldsize = f->sizelocvars;
    luaM_growvector(ls->L, f->locvars, fs->nlocvars, f->sizelocvars,
                    LocVar, SHRT_MAX, "too many local variables");
    while (oldsize < f->sizelocvars)
    {
        f->locvars[oldsize++].varname = NULL;
    }
    f->locvars[fs->nlocvars].varname = varname;
    luaC_objbarrier(ls->L, f, varname);
    return fs->nlocvars++;
}


/* 新建局部变量字面量 */
#define new_localvarliteral(ls, v, n) \
    new_localvar(ls, luaX_newstring(ls, "" v, (sizeof(v)/sizeof(char))-1), n)


/* 新建局部变量 */
static void new_localvar(LexState *ls, TString *name, int n)
{
    FuncState *fs = ls->fs;
    luaY_checklimit(fs, fs->nactvar + n + 1, LUAI_MAXVARS, "local variables");
    fs->actvar[fs->nactvar + n] = cast(unsigned short, registerlocalvar(ls, name));
}


/* 调整局部变量 */
static void adjustlocalvars(LexState *ls, int nvars)
{
    FuncState *fs = ls->fs;
  fs->nactvar = cast_byte(fs->nactvar + nvars);
  for (; nvars; nvars--) {
    getlocvar(fs, fs->nactvar - nvars).startpc = fs->pc;
  }
}


/* 移除变量 */
static void removevars (LexState *ls, int tolevel) {
  FuncState *fs = ls->fs;
  while (fs->nactvar > tolevel)
    getlocvar(fs, --fs->nactvar).endpc = fs->pc;
}


/* 索引上值 */
static int indexupvalue (FuncState *fs, TString *name, expdesc *v) {
  int i;
  Proto *f = fs->f;
  int oldsize = f->sizeupvalues;
  for (i=0; i<f->nups; i++) {
    if (fs->upvalues[i].k == v->k && fs->upvalues[i].info == v->u.s.info) {
      lua_assert(f->upvalues[i] == name);
      return i;
    }
  }
  /* 新的上值 */
  luaY_checklimit(fs, f->nups + 1, LUAI_MAXUPVALUES, "upvalues");
  luaM_growvector(fs->L, f->upvalues, f->nups, f->sizeupvalues,
                  TString *, MAX_INT, "");
  while (oldsize < f->sizeupvalues) f->upvalues[oldsize++] = NULL;
  f->upvalues[f->nups] = name;
  luaC_objbarrier(fs->L, f, name);
  lua_assert(v->k == VLOCAL || v->k == VUPVAL);
  fs->upvalues[f->nups].k = cast_byte(v->k);
  fs->upvalues[f->nups].info = cast_byte(v->u.s.info);
  return f->nups++;
}


/* 搜索变量 */
static int searchvar (FuncState *fs, TString *n) {
  int i;
  for (i=fs->nactvar-1; i >= 0; i--) {
    if (n == getlocvar(fs, i).varname)
      return i;
  }
  return -1;  /* 未找到 */
}


/* 标记上值 */
static void markupval (FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl && bl->nactvar > level) bl = bl->previous;
  if (bl) bl->upval = 1;
}


/* 单变量辅助函数 - 递归查找变量的作用域 */
static int singlevaraux (FuncState *fs, TString *n, expdesc *var, int base) {
  if (fs == NULL) {  /* 没有更多层级？ */
    /* 到达最顶层仍未找到，默认为全局变量 */
    init_exp(var, VGLOBAL, NO_REG);  /* 默认是全局变量 */
    return VGLOBAL;
  }
  else {
    /* 在当前函数作用域中搜索变量 */
    int v = searchvar(fs, n);  /* 在当前层级查找 */
    if (v >= 0) {
      /* 在当前作用域找到变量，初始化为局部变量 */
      init_exp(var, VLOCAL, v);
      if (!base)
        /* 如果不是基础调用，标记为上值（被内层函数引用） */
        markupval(fs, v);  /* 局部变量将被用作上值 */
      return VLOCAL;
    }
    else {  /* 在当前层级未找到；尝试上一层 */
      /* 递归查找外层作用域 */
      if (singlevaraux(fs->prev, n, var, 0) == VGLOBAL)
        return VGLOBAL;
      /* 在外层找到，创建上值索引 */
      var->u.s.info = indexupvalue(fs, n, var);  /* 否则是局部变量或上值 */
      var->k = VUPVAL;  /* 此层级的上值 */
      return VUPVAL;
    }
  }
}


/* 单变量 */
static void singlevar (LexState *ls, expdesc *var) {
  TString *varname = str_checkname(ls);
  FuncState *fs = ls->fs;
  if (singlevaraux(fs, varname, var, 1) == VGLOBAL)
    var->u.s.info = luaK_stringK(fs, varname);  /* info 指向全局名称 */
}


/* 调整赋值 */
static void adjust_assign (LexState *ls, int nvars, int nexps, expdesc *e) {
  FuncState *fs = ls->fs;
  int extra = nvars - nexps;
  if (hasmultret(e->k)) {
    extra++;  /* 包括调用本身 */
    if (extra < 0) extra = 0;
    luaK_setreturns(fs, e, extra);  /* 最后的表达式提供差值 */
    if (extra > 1) luaK_reserveregs(fs, extra-1);
  }
  else {
    if (e->k != VVOID) luaK_exp2nextreg(fs, e);  /* 关闭最后的表达式 */
    if (extra > 0) {
      int reg = fs->freereg;
      luaK_reserveregs(fs, extra);
      luaK_nil(fs, reg, extra);
    }
  }
}


/* 进入层级 */
static void enterlevel (LexState *ls) {
  if (++ls->L->nCcalls > LUAI_MAXCCALLS)
	luaX_lexerror(ls, "chunk has too many syntax levels", 0);
}


/* 离开层级 */
#define leavelevel(ls)	((ls)->L->nCcalls--)


/* 进入块 */
static void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isbreakable) {
  bl->breaklist = NO_JUMP;
  bl->isbreakable = isbreakable;
  bl->nactvar = fs->nactvar;
  bl->upval = 0;
  bl->previous = fs->bl;
  fs->bl = bl;
  lua_assert(fs->freereg == fs->nactvar);
}


/* 离开块 */
static void leaveblock (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  fs->bl = bl->previous;
  removevars(fs->ls, bl->nactvar);
  if (bl->upval)
    luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
  /* 一个块要么控制作用域要么中断（永远不会两者都有） */
  lua_assert(!bl->isbreakable || !bl->upval);
  lua_assert(bl->nactvar == fs->nactvar);
  fs->freereg = fs->nactvar;  /* 释放寄存器 */
  luaK_patchtohere(fs, bl->breaklist);
}


/* 推送闭包 */
static void pushclosure (LexState *ls, FuncState *func, expdesc *v) {
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  int oldsize = f->sizep;
  int i;
  luaM_growvector(ls->L, f->p, fs->np, f->sizep, Proto *,
                  MAXARG_Bx, "constant table overflow");
  while (oldsize < f->sizep) f->p[oldsize++] = NULL;
  f->p[fs->np++] = func->f;
  luaC_objbarrier(ls->L, f, func->f);
  init_exp(v, VRELOCABLE, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np-1));
  for (i=0; i<func->f->nups; i++) {
    OpCode o = (func->upvalues[i].k == VLOCAL) ? OP_MOVE : OP_GETUPVAL;
    luaK_codeABC(fs, o, 0, func->upvalues[i].info, 0);
  }
}


/* 打开函数 */
static void open_func (LexState *ls, FuncState *fs) {
  lua_State *L = ls->L;
  Proto *f = luaF_newproto(L);
  fs->f = f;
  fs->prev = ls->fs;  /* 函数状态的链表 */
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
  f->maxstacksize = 2;  /* 寄存器 0/1 总是有效的 */
  fs->h = luaH_new(L, 0, 0);
  /* 锚定常量表和原型（避免被回收） */
  sethvalue2s(L, L->top, fs->h);
  incr_top(L);
  setptvalue2s(L, L->top, f);
  incr_top(L);
}


/* 关闭函数 */
static void close_func (LexState *ls) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  removevars(ls, 0);
  luaK_ret(fs, 0, 0);  /* 最终返回 */
  luaM_reallocvector(L, f->code, f->sizecode, fs->pc, Instruction);
  f->sizecode = fs->pc;
  luaM_reallocvector(L, f->lineinfo, f->sizelineinfo, fs->pc, int);
  f->sizelineinfo = fs->pc;
  luaM_reallocvector(L, f->k, f->sizek, fs->nk, TValue);
  f->sizek = fs->nk;
  luaM_reallocvector(L, f->p, f->sizep, fs->np, Proto *);
  f->sizep = fs->np;
  luaM_reallocvector(L, f->locvars, f->sizelocvars, fs->nlocvars, LocVar);
  f->sizelocvars = fs->nlocvars;
  luaM_reallocvector(L, f->upvalues, f->sizeupvalues, f->nups, TString *);
  f->sizeupvalues = f->nups;
  lua_assert(luaG_checkcode(f));
  lua_assert(fs->bl == NULL);
  ls->fs = fs->prev;
  /* 最后读取的标记锚定在已失效的函数中；必须重新锚定 */
  if (fs) anchor_token(ls);
  L->top -= 2;  /* 从栈中移除表和原型 */
}


/* Lua 语法分析器 */
Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff, const char *name) {
  struct LexState lexstate;
  struct FuncState funcstate;
  lexstate.buff = buff;
  luaX_setinput(L, &lexstate, z, luaS_new(L, name));
  open_func(&lexstate, &funcstate);
  funcstate.f->is_vararg = VARARG_ISVARARG;  /* 主函数总是可变参数 */
  luaX_next(&lexstate);  /* 读取第一个标记 */
  chunk(&lexstate);
  check(&lexstate, TK_EOS);
  close_func(&lexstate);
  lua_assert(funcstate.prev == NULL);
  lua_assert(funcstate.f->nups == 0);
  lua_assert(lexstate.fs == NULL);
  return funcstate.f;
}



/*============================================================*/
/* 语法规则 */
/*============================================================*/


/* 字段 */
static void field (LexState *ls, expdesc *v) {
  /* field -> ['.' | ':'] NAME */
  FuncState *fs = ls->fs;
  expdesc key;
  luaK_exp2anyreg(fs, v);
  luaX_next(ls);  /* 跳过点或冒号 */
  checkname(ls, &key);
  luaK_indexed(fs, v, &key);
}


/* y索引 */
static void yindex (LexState *ls, expdesc *v) {
  /* index -> '[' expr ']' */
  luaX_next(ls);  /* 跳过 '[' */
  expr(ls, v);
  luaK_exp2val(ls->fs, v);
  checknext(ls, ']');
}


/*
** {======================================================================
** 构造器规则
** =======================================================================
*/


/* 构造器控制结构 */
struct ConsControl {
  expdesc v;  /* 最后读取的列表项 */
  expdesc *t;  /* 表描述符 */
  int nh;  /* 记录元素的总数 */
  int na;  /* 数组元素的总数 */
  int tostore;  /* 待存储的数组元素数量 */
};


/* 记录字段 */
static void recfield (LexState *ls, struct ConsControl *cc) {
  /* recfield -> (NAME | `['exp1`]') = exp1 */
  FuncState *fs = ls->fs;
  int reg = ls->fs->freereg;
  expdesc key, val;
  int rkkey;
  if (ls->t.token == TK_NAME) {
    luaY_checklimit(fs, cc->nh, MAX_INT, "items in a constructor");
    checkname(ls, &key);
  }
  else  /* ls->t.token == '[' */
    yindex(ls, &key);
  cc->nh++;
  checknext(ls, '=');
  rkkey = luaK_exp2RK(fs, &key);
  expr(ls, &val);
  luaK_codeABC(fs, OP_SETTABLE, cc->t->u.s.info, rkkey, luaK_exp2RK(fs, &val));
  fs->freereg = reg;  /* 释放寄存器 */
}


/* 关闭列表字段 */
static void closelistfield (FuncState *fs, struct ConsControl *cc) {
  if (cc->v.k == VVOID) return;  /* 没有列表项 */
  luaK_exp2nextreg(fs, &cc->v);
  cc->v.k = VVOID;
  if (cc->tostore == LFIELDS_PER_FLUSH) {
    luaK_setlist(fs, cc->t->u.s.info, cc->na, cc->tostore);  /* 刷新 */
    cc->tostore = 0;  /* 没有更多待处理的项 */
  }
}


/* 最后列表字段 */
static void lastlistfield (FuncState *fs, struct ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->v.k)) {
    luaK_setmultret(fs, &cc->v);
    luaK_setlist(fs, cc->t->u.s.info, cc->na, LUA_MULTRET);
    cc->na--;  /* 不计算最后的表达式（未知元素数量） */
  }
  else {
    if (cc->v.k != VVOID)
      luaK_exp2nextreg(fs, &cc->v);
    luaK_setlist(fs, cc->t->u.s.info, cc->na, cc->tostore);
  }
}


/* 列表字段 */
static void listfield (LexState *ls, struct ConsControl *cc) {
  expr(ls, &cc->v);
  luaY_checklimit(ls->fs, cc->na, MAX_INT, "items in a constructor");
  cc->na++;
  cc->tostore++;
}


/* 构造器 - 解析表构造语法 {key=value, ...} */
static void constructor (LexState *ls, expdesc *t) {
  /* constructor -> ?? */
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  
  /* 生成 NEWTABLE 指令创建新表 */
  int pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
  struct ConsControl cc;
  
  /* 初始化构造器控制结构 */
  /* 数组元素数、哈希元素数、待存储数 */
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = t;
  init_exp(t, VRELOCABLE, pc);
  /* 暂无值 */
  init_exp(&cc.v, VVOID, 0);
  
  /* 将表固定在栈顶以防垃圾回收 */
  luaK_exp2nextreg(ls->fs, t);
  
  checknext(ls, '{');
  do {
    lua_assert(cc.v.k == VVOID || cc.tostore > 0);
    /* 空表或结束 */
    if (ls->t.token == '}') break;
    
    /* 处理之前的列表字段 */
    closelistfield(fs, &cc);
    
    /* 根据标记类型决定字段类型 */
    switch(ls->t.token) {
      /* 可能是列表字段或记录字段 */
      case TK_NAME: {
        /* 向前查看判断是 key=value 还是 value */
        luaX_lookahead(ls);
        /* 表达式？ */
        if (ls->lookahead.token != '=')
          /* 数组元素 */
          listfield(ls, &cc);
        else
          /* 记录字段 key=value */
          recfield(ls, &cc);
        break;
      }
      /* constructor_item -> recfield */
      case '[': {
        /* [key] = value 形式 */
        recfield(ls, &cc);
        break;
      }
      /* constructor_part -> listfield */
      default: {
        /* 默认为数组元素 */
        listfield(ls, &cc);
        break;
      }
    }
  } while (testnext(ls, ',') || testnext(ls, ';'));
  
  check_match(ls, '}', '{', line);
  
  /* 处理最后的列表字段 */
  lastlistfield(fs, &cc);
  
  /* 回填 NEWTABLE 指令的参数：数组大小和哈希大小 */
  /* 设置初始数组大小 */
  SETARG_B(fs->f->code[pc], luaO_int2fb(cc.na));
  /* 设置初始表大小 */
  SETARG_C(fs->f->code[pc], luaO_int2fb(cc.nh));
}

/* }====================================================================== */



/* 参数列表 - 解析函数参数声明 */
static void parlist (LexState *ls) {
  /* parlist -> [ param { `,' param } ] */
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  
  /* 参数计数器 */
  int nparams = 0;
  
  /* 初始化为非可变参数函数 */
  f->is_vararg = 0;
  
  /* 检查是否有参数（不是空的参数列表） */
  if (ls->t.token != ')') {
    do {
      switch (ls->t.token) {
        case TK_NAME: {
          /* 普通参数：param -> NAME */
          /* 创建新的局部变量作为参数 */
          new_localvar(ls, str_checkname(ls), nparams++);
          break;
        }
        case TK_DOTS: {
          /* 可变参数：param -> `...' */
          /* 跳过 '...' 标记 */
          luaX_next(ls);
#if defined(LUA_COMPAT_VARARG)
          /* 兼容模式：使用 `arg' 作为默认名称 */
          new_localvarliteral(ls, "arg", nparams++);
          /* 设置兼容标志 */
          f->is_vararg = VARARG_HASARG | VARARG_NEEDSARG;
#endif
          /* 标记为可变参数函数 */
          f->is_vararg |= VARARG_ISVARARG;
          break;
        }
        default: 
          /* 语法错误：期望参数名或 '...' */
          luaX_syntaxerror(ls, "<name> or " LUA_QL("...") " expected");
      }
      /* 继续解析下一个参数，直到遇到可变参数或没有更多参数 */
    } while (!f->is_vararg && testnext(ls, ','));
  }
  adjustlocalvars(ls, nparams);
  f->numparams = cast_byte(fs->nactvar - (f->is_vararg & VARARG_HASARG));
  luaK_reserveregs(fs, fs->nactvar);  /* 为参数保留寄存器 */
}


/* 函数体 - 解析函数定义的完整语法 */
static void body (LexState *ls, expdesc *e, int needself, int line) {
  /* body ->  `(' parlist `)' chunk END */
  FuncState new_fs;
  
  /* 打开新的函数作用域 */
  open_func(ls, &new_fs);
  
  /* 记录函数定义的起始行号 */
  new_fs.f->linedefined = line;
  
  /* 检查并跳过左括号 '(' */
  checknext(ls, '(');
  
  /* 如果需要self参数（方法调用），添加隐式self参数 */
  if (needself) {
    /* 创建名为"self"的局部变量 */
    new_localvarliteral(ls, "self", 0);
    /* 激活这个局部变量 */
    adjustlocalvars(ls, 1);
  }
  
  /* 解析参数列表 */
  parlist(ls);
  
  /* 检查并跳过右括号 ')' */
  checknext(ls, ')');
  
  /* 解析函数体（语句块） */
  chunk(ls);
  
  /* 记录函数定义的结束行号 */
  new_fs.f->lastlinedefined = ls->linenumber;
  
  /* 检查匹配的END关键字 */
  check_match(ls, TK_END, TK_FUNCTION, line);
  
  /* 关闭函数作用域并生成原型 */
  close_func(ls);
  
  /* 创建闭包并推送到表达式栈 */
  pushclosure(ls, &new_fs, e);
}


/* 表达式列表1 - 解析逗号分隔的表达式列表 */
static int explist1 (LexState *ls, expdesc *v) {
  /* explist1 -> expr { `,' expr } */
  
  /* 表达式计数器，至少有一个表达式 */
  int n = 1;
  
  /* 解析第一个表达式 */
  expr(ls, v);
  
  /* 继续解析后续的表达式（如果有逗号分隔） */
  while (testnext(ls, ',')) {
    /* 将当前表达式的值移动到下一个寄存器 */
    luaK_exp2nextreg(ls->fs, v);
    
    /* 解析下一个表达式 */
    expr(ls, v);
    
    /* 增加表达式计数 */
    n++;
  }
  
  /* 返回表达式的总数 */
  return n;
}


/* 函数参数 - 解析函数调用的参数 */
static void funcargs (LexState *ls, expdesc *f) {
  FuncState *fs = ls->fs;
  expdesc args;
  int base, nparams;
  
  /* 记录当前行号用于错误检查 */
  int line = ls->linenumber;
  
  /* 根据不同的参数形式进行解析 */
  switch (ls->t.token) {
    case '(': {
      /* 标准参数形式：funcargs -> `(' [ explist1 ] `)' */
      
      /* 检查语法歧义：函数调用与新语句 */
      if (line != ls->lastline)
        luaX_syntaxerror(ls,"ambiguous syntax (function call x new statement)");
      
      /* 跳过左括号 */
      luaX_next(ls);
      
      /* 检查是否为空参数列表 */
      if (ls->t.token == ')') {
        /* 空参数列表 */
        args.k = VVOID;
      }
      else {
        /* 解析参数表达式列表 */
        explist1(ls, &args);
        /* 设置多返回值标记 */
        luaK_setmultret(fs, &args);
      }
      
      /* 检查匹配的右括号 */
      check_match(ls, ')', '(', line);
      break;
    }
    case '{': {
      /* 表构造器作为参数：funcargs -> constructor */
      constructor(ls, &args);
      break;
    }
    case TK_STRING: {
      /* 字符串字面量作为参数：funcargs -> STRING */
      /* 创建字符串常量 */
      codestring(ls, &args, ls->t.seminfo.ts);
      /* 必须在next之前使用seminfo */
      luaX_next(ls);
      break;
    }
    default: {
      /* 语法错误：期望函数参数 */
      luaX_syntaxerror(ls, "function arguments expected");
      return;
    }
  }
  
  /* 断言：函数表达式应该在寄存器中 */
  lua_assert(f->k == VNONRELOC);
  
  /* 获取调用的基础寄存器（函数所在位置） */
  base = f->u.s.info;
  
  /* 确定参数数量 */
  if (hasmultret(args.k)) {
    /* 多返回值调用（开放调用） */
    nparams = LUA_MULTRET;
  }
  else {
    /* 固定参数数量 */
    if (args.k != VVOID) {
      /* 将最后的参数移到下一个寄存器 */
      luaK_exp2nextreg(fs, &args);
    }
    /* 计算参数数量 */
    nparams = fs->freereg - (base+1);
  }
  
  /* 生成函数调用指令 */
  init_exp(f, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2));
  
  /* 设置调试行号信息 */
  luaK_fixline(fs, line);
  
  /* 调用后释放寄存器：移除函数和参数，保留一个结果 */
  fs->freereg = base+1;
}




/*
** {======================================================================
** 表达式解析
** =======================================================================
*/


/* 前缀表达式 - 解析表达式的前缀部分 */
static void prefixexp (LexState *ls, expdesc *v) {
  /* prefixexp -> NAME | '(' expr ')' */
  
  switch (ls->t.token) {
    case '(': {
      /* 括号表达式：'(' expr ')' */
      
      /* 记录行号用于错误匹配检查 */
      int line = ls->linenumber;
      
      /* 跳过左括号 */
      luaX_next(ls);
      
      /* 解析括号内的表达式 */
      expr(ls, v);
      
      /* 检查匹配的右括号 */
      check_match(ls, ')', '(', line);
      
      /* 释放变量引用，确保表达式值在寄存器中 */
      luaK_dischargevars(ls->fs, v);
      return;
    }
    case TK_NAME: {
      /* 变量名：NAME */
      /* 解析单个变量（局部变量、上值或全局变量） */
      singlevar(ls, v);
      return;
    }
    default: {
      /* 语法错误：意外的符号 */
      luaX_syntaxerror(ls, "unexpected symbol");
      return;
    }
  }
}


/* 主表达式 - 解析复合表达式（字段访问、索引、方法调用等） */
static void primaryexp (LexState *ls, expdesc *v) {
  /* primaryexp ->
        prefixexp { `.' NAME | `[' exp `]' | `:' NAME funcargs | funcargs } */
  FuncState *fs = ls->fs;
  
  /* 首先解析前缀表达式（变量名或括号表达式） */
  prefixexp(ls, v);
  
  /* 循环处理后缀操作符 */
  for (;;) {
    switch (ls->t.token) {
      case '.': {
        /* 字段访问：obj.field */
        field(ls, v);
        break;
      }
      case '[': {
        /* 索引访问：obj[key] */
        expdesc key;
        
        /* 确保对象在寄存器中 */
        luaK_exp2anyreg(fs, v);
        
        /* 解析索引表达式 */
        yindex(ls, &key);
        
        /* 生成索引访问代码 */
        luaK_indexed(fs, v, &key);
        break;
      }
      case ':': {
        /* 方法调用：obj:method(args) */
        expdesc key;
        
        /* 跳过冒号 */
        luaX_next(ls);
        
        /* 获取方法名 */
        checkname(ls, &key);
        
        /* 生成self调用代码（将obj作为第一个参数） */
        luaK_self(fs, v, &key);
        
        /* 解析函数参数 */
        funcargs(ls, v);
        break;
      }
      case '(': case TK_STRING: case '{': {
        /* 函数调用：func(args) 或 func"string" 或 func{table} */
        
        /* 确保函数在下一个寄存器中 */
        luaK_exp2nextreg(fs, v);
        
        /* 解析函数参数 */
        funcargs(ls, v);
        break;
      }
      default: 
        /* 没有更多后缀操作符，退出循环 */
        return;
    }
  }
}


/* 简单表达式 - 解析基本的表达式类型 */
static void simpleexp (LexState *ls, expdesc *v) {
  /* simpleexp -> NUMBER | STRING | NIL | true | false | ... |
                  constructor | FUNCTION body | primaryexp */
  
  switch (ls->t.token) {
    case TK_NUMBER: {
      /* 数字字面量 */
      /* 初始化为数字常量表达式 */
      init_exp(v, VKNUM, 0);
      /* 设置数字值 */
      v->u.nval = ls->t.seminfo.r;
      break;
    }
    case TK_STRING: {
      /* 字符串字面量 */
      /* 生成字符串常量代码 */
      codestring(ls, v, ls->t.seminfo.ts);
      break;
    }
    case TK_NIL: {
      /* nil 字面量 */
      init_exp(v, VNIL, 0);
      break;
    }
    case TK_TRUE: {
      /* true 字面量 */
      init_exp(v, VTRUE, 0);
      break;
    }
    case TK_FALSE: {
      /* false 字面量 */
      init_exp(v, VFALSE, 0);
      break;
    }
    case TK_DOTS: {
      /* 可变参数：... */
      FuncState *fs = ls->fs;
      
      /* 检查是否在可变参数函数中 */
      check_condition(ls, fs->f->is_vararg,
                      "cannot use " LUA_QL("...") " outside a vararg function");
      
      /* 标记不需要'arg'参数 */
      fs->f->is_vararg &= ~VARARG_NEEDSARG;
      
      /* 生成VARARG指令 */
      init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 1, 0));
      break;
    }
    case '{': {
      /* 表构造器：{...} */
      constructor(ls, v);
      return;
    }
    case TK_FUNCTION: {
      /* 函数定义：function ... end */
      /* 跳过function关键字 */
      luaX_next(ls);
      /* 解析函数体（不需要self参数） */
      body(ls, v, 0, ls->linenumber);
      return;
    }
    default: {
      /* 其他情况：解析主表达式 */
      primaryexp(ls, v);
      return;
    }
  }
  
  /* 对于简单字面量，跳到下一个标记 */
  luaX_next(ls);
}


/* 获取一元运算符 */
static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


/* 获取二元运算符 */
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


/* 运算符优先级表 */
static const struct {
  lu_byte left;  /* 左结合优先级 */
  lu_byte right; /* 右结合优先级 */
} priority[] = {  /* 运算符优先级顺序 */
  {6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  /* `+' `-' `*' `/' `%' */
  {10, 9}, {5, 4},                 /* ^, .. (右结合) */
  {3, 3}, {3, 3},                  /* ==, ~= */
  {3, 3}, {3, 3}, {3, 3}, {3, 3},  /* <, <=, >, >= */
  {2, 2}, {1, 1}                   /* and, or */
};

#define UNARY_PRIORITY	8  /* 一元运算符的优先级 */


/*
** 子表达式 -> (简单表达式 | 一元运算符 子表达式) { 二元运算符 子表达式 }
** 其中 `limit' 是要处理的运算符的最小优先级
** 使用运算符优先级解析算法（Pratt解析器）
*/
static BinOpr subexpr (LexState *ls, expdesc *v, unsigned int limit) {
  BinOpr op;
  UnOpr uop;
  
  /* 防止栈溢出 */
  enterlevel(ls);
  
  /* 首先检查一元运算符 */
  uop = getunopr(ls->t.token);
  if (uop != OPR_NOUNOPR) {
    /* 处理一元运算符：not, -, # */
    luaX_next(ls);
    /* 递归解析操作数，一元运算符优先级最高 */
    subexpr(ls, v, UNARY_PRIORITY);
    /* 生成一元运算符代码 */
    luaK_prefix(ls->fs, uop, v);
  }
  else {
    /* 解析简单表达式 */
    simpleexp(ls, v);
  }
  
  /* 展开二元运算符链 */
  op = getbinopr(ls->t.token);
  
  /* 当运算符优先级高于限制时继续处理 */
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    
    /* 跳过运算符 */
    luaX_next(ls);
    
    /* 处理中缀运算符的左操作数 */
    luaK_infix(ls->fs, op, v);
    
    /* 读取右操作数，使用右结合优先级 */
    /* 这实现了运算符的左/右结合性 */
    nextop = subexpr(ls, &v2, priority[op].right);
    
    /* 生成二元运算符代码 */
    luaK_posfix(ls->fs, op, v, &v2);
    
    /* 继续处理下一个运算符 */
    op = nextop;
  }
  
  leavelevel(ls);
  
  /* 返回第一个未处理的运算符 */
  return op;
}


/* 表达式 */
static void expr (LexState *ls, expdesc *v) {
  subexpr(ls, v, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** 语句规则
** =======================================================================
*/


/* 块跟随 */
static int block_follow (int token) {
  switch (token) {
    case TK_ELSE: case TK_ELSEIF: case TK_END:
    case TK_UNTIL: case TK_EOS:
      return 1;
    default: return 0;
  }
}


/* 块 */
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
** 左手边赋值结构
*/
struct LHS_assign {
  struct LHS_assign *prev;
  expdesc v;  /* 变量（全局、局部、上值或索引） */
};


/*
** 检查赋值冲突 - 避免在多重赋值中出现寄存器冲突
** 检查是否存在冲突：
** 1) 在赋值中是否有局部变量？
** 2) 如果有，是否有上值访问该局部变量？
*/
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
  FuncState *fs = ls->fs;
  
  /* 获取赋值完成后的寄存器位置 */
  int extra = fs->freereg;
  int conflict = 0;
  
  /* 遍历所有之前的左值变量 */
  for (; lh; lh = lh->prev) {
    /* 只检查索引类型的变量（如table[key]） */
    if (lh->v.k == VINDEXED) {
      /* 检查表对象是否与当前变量冲突 */
      if (lh->v.u.s.info == v->u.s.info) {
        conflict = 1;
        /* 将冲突的寄存器重定向到安全位置 */
        lh->v.u.s.info = extra;
      }
      
      /* 检查索引键是否与当前变量冲突 */
      if (lh->v.u.s.aux == v->u.s.info) {
        conflict = 1;
        /* 将冲突的寄存器重定向到安全位置 */
        lh->v.u.s.aux = extra;
      }
    }
  }
  
  /* 如果发现冲突，生成MOVE指令将值复制到安全位置 */
  if (conflict) {
    /* 生成移动指令：将当前变量的值复制到新的寄存器 */
    luaK_codeABC(fs, OP_MOVE, fs->freereg, v->u.s.info, 0);
    
    /* 预留一个寄存器用于存储复制的值 */
    luaK_reserveregs(fs, 1);
  }
}


/* 赋值 - 处理多重赋值语句 */
static void assignment (LexState *ls, struct LHS_assign *lh, int nvars) {
  expdesc e;
  
  /* 检查左值的有效性（必须是可赋值的表达式） */
  check_condition(ls, VLOCAL <= lh->v.k && lh->v.k <= VINDEXED,
                      "syntax error");
  
  /* 检查是否有更多的左值变量 */
  if (testnext(ls, ',')) {
    /* 多重赋值：var1, var2, ... = exp1, exp2, ... */
    struct LHS_assign nv;
    
    /* 链接到前一个左值 */
    nv.prev = lh;
    
    /* 解析下一个左值表达式 */
    primaryexp(ls, &nv.v);
    
    /* 如果是局部变量，检查是否有冲突 */
    if (nv.v.k == VLOCAL)
      check_conflict(ls, lh, &nv.v);
    
    /* 检查变量数量限制 */
    luaY_checklimit(ls->fs, nvars, LUAI_MAXCCALLS - ls->L->nCcalls,
                    "variables in assignment");
    
    /* 递归处理剩余的赋值 */
    assignment(ls, &nv, nvars+1);
  }
  else {
    /* 赋值操作：= explist1 */
    int nexps;
    
    /* 检查并跳过等号 */
    checknext(ls, '=');
    
    /* 解析右值表达式列表 */
    nexps = explist1(ls, &e);
    
    /* 处理左值和右值数量不匹配的情况 */
    if (nexps != nvars) {
      /* 调整赋值：补充nil或丢弃多余值 */
      adjust_assign(ls, nvars, nexps, &e);
      
      /* 如果右值多于左值，释放多余的寄存器 */
      if (nexps > nvars)
        ls->fs->freereg -= nexps - nvars;
    }
    else {
      /* 左值和右值数量相等的情况 */
      /* 确保最后的表达式只返回一个值 */
      luaK_setoneret(ls->fs, &e);
      
      /* 直接存储到左值 */
      luaK_storevar(ls->fs, &lh->v, &e);
      
      /* 避免执行默认赋值逻辑 */
      return;
    }
  }
  
  /* 默认赋值：使用栈顶的值 */
  init_exp(&e, VNONRELOC, ls->fs->freereg-1);
  
  /* 存储值到左值变量 */
  luaK_storevar(ls->fs, &lh->v, &e);
}


/* 条件 - 解析条件表达式并生成跳转代码 */
static int cond (LexState *ls) {
  /* cond -> exp */
  expdesc v;
  
  /* 解析条件表达式 */
  expr(ls, &v);
  
  /* 将nil转换为false（在Lua中nil和false都是假值） */
  if (v.k == VNIL) v.k = VFALSE;
  
  /* 生成条件为真时的跳转指令 */
  /* 如果条件为真，跳转到后续代码；如果为假，继续执行 */
  luaK_goiftrue(ls->fs, &v);
  
  /* 返回假值跳转链表，用于后续的跳转修正 */
  return v.f;
}


/* 中断语句 - 处理循环中的break跳转 */
static void breakstat (LexState *ls) {
  FuncState *fs = ls->fs;
  BlockCnt *bl = fs->bl;
  int upval = 0;
  
  /* 向上查找可以break的块（循环块） */
  while (bl && !bl->isbreakable) {
    /* 记录是否有upvalue需要关闭 */
    upval |= bl->upval;
    /* 继续向上查找 */
    bl = bl->previous;
  }
  
  /* 如果没有找到可break的循环块，报语法错误 */
  if (!bl)
    luaX_syntaxerror(ls, "no loop to break");
  
  /* 如果有upvalue需要关闭，生成CLOSE指令 */
  if (upval)
    luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
  
  /* 生成跳转指令并添加到break跳转链表 */
  /* 这个跳转将在循环结束时被修正到正确的位置 */
  luaK_concat(fs, &bl->breaklist, luaK_jump(fs));
}


/* while 语句 - 处理while循环结构 */
static void whilestat (LexState *ls, int line) {
  /* whilestat -> WHILE cond DO block END */
  FuncState *fs = ls->fs;
  int whileinit;
  int condexit;
  BlockCnt bl;
  
  /* 跳过WHILE关键字 */
  luaX_next(ls);
  
  /* 记录循环开始位置的标签 */
  whileinit = luaK_getlabel(fs);
  
  /* 解析条件表达式，获取条件为假时的跳转链表 */
  condexit = cond(ls);
  
  /* 进入可break的循环块 */
  enterblock(fs, &bl, 1);
  
  /* 检查并跳过DO关键字 */
  checknext(ls, TK_DO);
  
  /* 解析循环体 */
  block(ls);
  
  /* 生成跳转指令回到循环开始处 */
  luaK_patchlist(fs, luaK_jump(fs), whileinit);
  
  /* 检查END关键字与WHILE的匹配 */
  check_match(ls, TK_END, TK_WHILE, line);
  
  /* 离开循环块，处理break跳转 */
  leaveblock(fs);
  
  /* 将条件为假的跳转修正到循环结束位置 */
  luaK_patchtohere(fs, condexit);
}


/* repeat 语句 - 处理repeat-until循环结构 */
static void repeatstat (LexState *ls, int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = ls->fs;
  
  /* 记录循环开始位置 */
  int repeat_init = luaK_getlabel(fs);
  BlockCnt bl1, bl2;
  
  /* 进入循环块（可以break） */
  enterblock(fs, &bl1, 1);
  
  /* 进入作用域块（用于局部变量） */
  enterblock(fs, &bl2, 0);
  
  /* 跳过REPEAT关键字 */
  luaX_next(ls);
  
  /* 解析循环体 */
  chunk(ls);
  
  /* 检查UNTIL关键字与REPEAT的匹配 */
  check_match(ls, TK_UNTIL, TK_REPEAT, line);
  
  /* 解析条件表达式（在作用域块内，可以访问循环体中的局部变量） */
  condexit = cond(ls);
  
  /* 检查是否有upvalue需要处理 */
  if (!bl2.upval) {
    /* 没有upvalue的简单情况 */
    /* 完成作用域块 */
    leaveblock(fs);
    
    /* 条件为假时跳转回循环开始 */
    luaK_patchlist(fs, condexit, repeat_init);
  }
  else {
    /* 有upvalue的复杂情况，需要完整的语义处理 */
    /* 如果条件为真，执行break跳出循环 */
    breakstat(ls);
    
    /* 条件为假时继续执行（跳转到这里） */
    luaK_patchtohere(fs, condexit);
    
    /* 完成作用域块 */
    leaveblock(fs);
    
    /* 生成无条件跳转回循环开始 */
    luaK_patchlist(fs, luaK_jump(fs), repeat_init);
  }
  
  /* 完成循环块 */
  leaveblock(fs);
}


/* 表达式1 - 解析表达式并将结果放入下一个寄存器 */
static int exp1 (LexState *ls) {
  expdesc e;
  int k;
  
  /* 解析表达式 */
  expr(ls, &e);
  
  /* 保存表达式的类型 */
  k = e.k;
  
  /* 将表达式的值放入下一个可用寄存器 */
  /* 这确保表达式的值在栈上有固定位置 */
  luaK_exp2nextreg(ls->fs, &e);
  
  /* 返回原始表达式类型 */
  return k;
}


/* for 循环体 - 生成for循环的循环体代码 */
static void forbody (LexState *ls, int base, int line, int nvars, int isnum) {
  /* forbody -> DO block */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  int prep, endfor;
  
  /* 激活控制变量（数值for: index,limit,step; 通用for: generator,state,control） */
  adjustlocalvars(ls, 3);
  
  /* 检查并跳过DO关键字 */
  checknext(ls, TK_DO);
  
  /* 生成循环准备指令 */
  if (isnum) {
    /* 数值for循环：生成FORPREP指令 */
    prep = luaK_codeAsBx(fs, OP_FORPREP, base, NO_JUMP);
  } else {
    /* 通用for循环：生成跳转指令 */
    prep = luaK_jump(fs);
  }
  
  /* 进入循环体作用域（用于用户声明的循环变量） */
  enterblock(fs, &bl, 0);
  
  /* 激活用户声明的循环变量 */
  adjustlocalvars(ls, nvars);
  
  /* 为循环变量预留寄存器 */
  luaK_reserveregs(fs, nvars);
  
  /* 解析循环体代码块 */
  block(ls);
  
  /* 结束循环体作用域，清理用户变量 */
  leaveblock(fs);
  
  /* 修正准备指令的跳转目标到这里 */
  luaK_patchtohere(fs, prep);
  
  /* 生成循环结束指令 */
  if (isnum) {
    /* 数值for循环：生成FORLOOP指令 */
    endfor = luaK_codeAsBx(fs, OP_FORLOOP, base, NO_JUMP);
  } else {
    /* 通用for循环：生成TFORLOOP指令 */
    endfor = luaK_codeABC(fs, OP_TFORLOOP, base, 0, nvars);
  }
  
  /* 设置正确的行号信息 */
  luaK_fixline(fs, line);
  
  /* 修正循环跳转：循环继续时跳转到prep+1位置 */
  luaK_patchlist(fs, (isnum ? endfor : luaK_jump(fs)), prep + 1);
}


/* 数值 for 循环 */
static void fornum (LexState *ls, TString *varname, int line) {
  /* fornum -> NAME = exp1,exp1[,exp1] forbody */
  FuncState *fs = ls->fs;
  /* 数值 for 循环的基础寄存器 */
  int base = fs->freereg;
  
  /* 创建内部控制变量（用户不可见） */
  /* 当前索引值 */
  new_localvarliteral(ls, "(for index)", 0);
  /* 循环上限 */
  new_localvarliteral(ls, "(for limit)", 1);
  /* 步长 */
  new_localvarliteral(ls, "(for step)", 2);
  
  /* 创建用户可见的循环变量 */
  new_localvar(ls, varname, 3);
  
  checknext(ls, '=');
  
  /* 解析初始值表达式 */
  exp1(ls);
  checknext(ls, ',');
  
  /* 解析限制值表达式 */
  exp1(ls);
  
  /* 解析可选的步长表达式 */
  if (testnext(ls, ',')) {
    exp1(ls);
  }
  else {
    /* 默认步长 = 1 */
    /* 生成常数 1 作为默认步长 */
    luaK_codeABx(fs, OP_LOADK, fs->freereg, luaK_numberK(fs, 1));
    luaK_reserveregs(fs, 1);
  }
  
  /* 生成循环体代码 */
  forbody(ls, base, line, 1, 1);
}


/* 列表 for 循环 */
static void forlist (LexState *ls, TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist1 forbody */
  FuncState *fs = ls->fs;
  expdesc e;
  /* 变量计数器 */
  int nvars = 0;
  int line;
  /* 泛型 for 循环的基础寄存器 */
  int base = fs->freereg;
  
  /* 创建内部控制变量（用户不可见） */
  /* 生成器函数 */
  new_localvarliteral(ls, "(for generator)", nvars++);
  /* 状态变量 */
  new_localvarliteral(ls, "(for state)", nvars++);
  /* 控制变量 */
  new_localvarliteral(ls, "(for control)", nvars++);
  
  /* 创建用户声明的循环变量 */
  new_localvar(ls, indexname, nvars++);
  
  /* 处理额外的循环变量 */
  while (testnext(ls, ','))
    new_localvar(ls, str_checkname(ls), nvars++);
    
  checknext(ls, TK_IN);
  line = ls->linenumber;
  
  /* 解析迭代器表达式列表并调整为3个值 */
  /* 通常是 generator, state, control = explist */
  adjust_assign(ls, 3, explist1(ls, &e), &e);
  
  /* 为调用生成器函数预留栈空间 */
  luaK_checkstack(fs, 3);
  
  /* 生成循环体代码 */
  /* nvars-3: 用户变量数量，0: 不是数值循环 */
  forbody(ls, base, line, nvars - 3, 0);
}


/* for 语句 - 处理for循环语句的解析 */
static void forstat (LexState *ls, int line) {
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *fs = ls->fs;
  TString *varname;
  BlockCnt bl;
  
  /* 进入for循环作用域（可以break，用于循环和控制变量） */
  enterblock(fs, &bl, 1);
  
  /* 跳过FOR关键字 */
  luaX_next(ls);
  
  /* 获取第一个变量名 */
  varname = str_checkname(ls);
  
  /* 根据下一个标记判断for循环类型 */
  switch (ls->t.token) {
    case '=':
      /* 数值for循环：for var = start, limit [, step] do ... end */
      fornum(ls, varname, line);
      break;
    case ',':
    case TK_IN:
      /* 通用for循环：for var1, var2, ... in explist do ... end */
      forlist(ls, varname);
      break;
    default:
      /* 语法错误：期望'='或'in' */
      luaX_syntaxerror(ls, LUA_QL("=") " or " LUA_QL("in") " expected");
  }
  
  /* 检查END关键字与FOR的匹配 */
  check_match(ls, TK_END, TK_FOR, line);
  
  /* 离开for循环作用域（break跳转到这里） */
  leaveblock(fs);
}


/* 测试然后块 - 处理IF/ELSEIF条件和THEN块 */
static int test_then_block (LexState *ls) {
  /* test_then_block -> [IF | ELSEIF] cond THEN block */
  int condexit;
  
  /* 跳过IF或ELSEIF关键字 */
  luaX_next(ls);
  
  /* 解析条件表达式，获取条件为假时的跳转链表 */
  condexit = cond(ls);
  
  /* 检查并跳过THEN关键字 */
  checknext(ls, TK_THEN);
  
  /* 解析THEN部分的代码块 */
  block(ls);
  
  /* 返回条件为假时的跳转链表，用于后续修正 */
  return condexit;
}


/* if 语句 - 处理if-elseif-else条件语句 */
static void ifstat (LexState *ls, int line) {
  /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
  FuncState *fs = ls->fs;
  
  /* 条件失败时的跳转链表 */
  int flist;
  
  /* 分支执行完毕后跳出整个if语句的跳转链表 */
  int escapelist = NO_JUMP;
  
  /* 处理主IF分支：IF cond THEN block */
  flist = test_then_block(ls);
  
  /* 处理所有ELSEIF分支 */
  while (ls->t.token == TK_ELSEIF) {
    /* 当前分支执行完毕，生成跳转指令跳过后续所有分支 */
    luaK_concat(fs, &escapelist, luaK_jump(fs));
    
    /* 将上一个条件失败的跳转修正到这里（下一个条件测试） */
    luaK_patchtohere(fs, flist);
    
    /* 处理ELSEIF分支：ELSEIF cond THEN block */
    flist = test_then_block(ls);
  }
  
  /* 处理可选的ELSE分支 */
  if (ls->t.token == TK_ELSE) {
    /* 当前分支执行完毕，生成跳转指令跳过ELSE分支 */
    luaK_concat(fs, &escapelist, luaK_jump(fs));
    
    /* 将最后一个条件失败的跳转修正到ELSE分支 */
    luaK_patchtohere(fs, flist);
    
    /* 跳过ELSE关键字 */
    luaX_next(ls);
    
    /* 解析ELSE部分的代码块 */
    block(ls);
  }
  else {
    /* 没有ELSE分支，将条件失败的跳转合并到escapelist */
    luaK_concat(fs, &escapelist, flist);
  }
  
  /* 将所有分支执行完毕的跳转修正到if语句结束位置 */
  luaK_patchtohere(fs, escapelist);
  
  /* 检查END关键字与IF的匹配 */
  check_match(ls, TK_END, TK_IF, line);
}


/* 局部函数声明处理：local function name() ... end */
static void localfunc (LexState *ls) {
  expdesc v, b;  /* v: 函数变量表达式, b: 函数体表达式 */
  FuncState *fs = ls->fs;
  
  /* 步骤1: 创建局部函数变量，获取函数名并注册为局部变量 */
  new_localvar(ls, str_checkname(ls), 0);
  
  /* 步骤2: 初始化变量表达式为局部变量类型 */
  /* 设置表达式类型为VLOCAL，寄存器位置为当前空闲寄存器 */
  init_exp(&v, VLOCAL, fs->freereg);
  
  /* 步骤3: 为函数变量预留一个寄存器空间 */
  luaK_reserveregs(fs, 1);
  
  /* 步骤4: 激活局部变量，使其在当前作用域内可见 */
  /* 这样函数体内可以递归调用自身 */
  adjustlocalvars(ls, 1);
  
  /* 步骤5: 解析函数体，生成闭包对象 */
  /* needself=0表示不是方法调用，line为当前行号 */
  body(ls, &b, 0, ls->linenumber);
  
  /* 步骤6: 将生成的闭包存储到局部变量中 */
  /* 完成 local function name = function() ... end 的赋值 */
  luaK_storevar(fs, &v, &b);
  
  /* 步骤7: 设置调试信息，记录局部变量的起始PC位置 */
  /* 调试器将只在函数创建完成后才能看到这个局部变量 */
  getlocvar(fs, fs->nactvar - 1).startpc = fs->pc;
}


/* 局部变量声明语句处理：local var1, var2, ... = exp1, exp2, ... */
static void localstat (LexState *ls) {
  /* 语法规则: stat -> LOCAL NAME {`,' NAME} [`=' explist1] */
  int nvars = 0;    /* 局部变量计数器 */
  int nexps;        /* 右侧表达式数量 */
  expdesc e;        /* 表达式描述符 */
  
  /* 步骤1: 解析变量名列表 local var1, var2, var3, ... */
  do {
    /* 为每个变量名创建局部变量，但暂不激活 */
    /* nvars++确保每个变量有唯一的索引 */
    new_localvar(ls, str_checkname(ls), nvars++);
  } while (testnext(ls, ','));  /* 继续解析逗号分隔的变量名 */
  
  /* 步骤2: 检查是否有初始化表达式 local var = expr */
  if (testnext(ls, '=')) {
    /* 有赋值操作，解析右侧表达式列表 */
    nexps = explist1(ls, &e);
  }
  else {
    /* 没有初始化表达式，所有变量默认初始化为 nil */
    e.k = VVOID;  /* 设置为空表达式类型 */
    nexps = 0;    /* 表达式数量为0 */
  }
  
  /* 步骤3: 调整赋值，处理变量数量与表达式数量不匹配的情况 */
  /* 如果nvars > nexps，多余变量赋值为nil */
  /* 如果nvars < nexps，多余表达式被丢弃 */
  adjust_assign(ls, nvars, nexps, &e);
  
  /* 步骤4: 激活所有局部变量，使其在当前作用域内可见 */
  /* 只有在完成赋值后才激活，避免变量在自己的初始化表达式中被引用 */
  adjustlocalvars(ls, nvars);
}


/* 解析函数名称：支持 name、table.name、table:name 等形式 */
static int funcname (LexState *ls, expdesc *v) {
  /* 语法规则: funcname -> NAME {field} [`:' NAME] */
  int needself = 0;  /* 是否需要隐式self参数的标志 */
  
  /* 步骤1: 解析基础变量名 */
  singlevar(ls, v);
  
  /* 步骤2: 处理字段访问 table.field1.field2... */
  while (ls->t.token == '.') {
    /* 解析 .field 形式的字段访问 */
    field(ls, v);
  }
  
  /* 步骤3: 检查是否为方法定义 table:method */
  if (ls->t.token == ':') {
    needself = 1;  /* 标记需要隐式self参数 */
    field(ls, v);  /* 解析方法名 */
  }
  
  /* 返回是否需要self参数 */
  /* needself=1表示方法定义，需要在参数列表前插入self */
  /* needself=0表示普通函数定义 */
  return needself;
}


/* 函数定义语句处理：function name() ... end */
static void funcstat (LexState *ls, int line) {
  /* 语法规则: funcstat -> FUNCTION funcname body */
  int needself;     /* 是否需要self参数 */
  expdesc v, b;     /* v: 函数名表达式, b: 函数体表达式 */
  
  /* 步骤1: 跳过 FUNCTION 关键字 */
  luaX_next(ls);
  
  /* 步骤2: 解析函数名，确定是否为方法定义 */
  /* 支持 func、table.func、table:method 等形式 */
  needself = funcname(ls, &v);
  
  /* 步骤3: 解析函数体，生成闭包 */
  /* needself决定是否在参数列表前插入隐式self参数 */
  body(ls, &b, needself, line);
  
  /* 步骤4: 将生成的闭包存储到指定位置 */
  /* 完成函数定义的赋值操作 */
  luaK_storevar(ls->fs, &v, &b);
  
  /* 步骤5: 设置调试信息，函数定义发生在第一行 */
  luaK_fixline(ls->fs, line);
}


/* 表达式语句处理：函数调用或赋值语句 */
static void exprstat (LexState *ls) {
  /* 语法规则: stat -> func | assignment */
  FuncState *fs = ls->fs;
  struct LHS_assign v;  /* 左值赋值结构 */
  
  /* 步骤1: 解析主表达式（函数调用或左值表达式） */
  primaryexp(ls, &v.v);
  
  /* 步骤2: 根据表达式类型进行不同处理 */
  if (v.v.k == VCALL) {
    /* 情况1: 函数调用语句 func() */
    /* 设置调用指令的返回值数量为1（丢弃返回值） */
    /* SETARG_C设置OP_CALL指令的C参数为1，表示不保存返回值 */
    SETARG_C(getcode(fs, &v.v), 1);
  }
  else {
    /* 情况2: 赋值语句 var = expr 或 table[key] = expr */
    v.prev = NULL;  /* 初始化左值链表 */
    /* 调用赋值处理函数，nvars=1表示单个左值 */
    assignment(ls, &v, 1);
  }
}


/* 返回语句处理：return [explist] */
static void retstat (LexState *ls) {
  /* 语法规则: stat -> RETURN explist */
  FuncState *fs = ls->fs;
  expdesc e;        /* 表达式描述符 */
  int first, nret;  /* first: 起始寄存器, nret: 返回值数量 */
  
  /* 步骤1: 跳过 RETURN 关键字 */
  luaX_next(ls);
  
  /* 步骤2: 检查是否有返回值表达式 */
  if (block_follow(ls->t.token) || ls->t.token == ';') {
    /* 情况1: 无返回值 return 或 return; */
    first = nret = 0;
  }
  else {
    /* 情况2: 有返回值表达式 return exp1, exp2, ... */
    nret = explist1(ls, &e);  /* 解析表达式列表 */
    
    if (hasmultret(e.k)) {
      /* 情况2a: 最后一个表达式可能返回多个值（函数调用或vararg） */
      luaK_setmultret(fs, &e);  /* 设置为多返回值模式 */
      
      /* 尾调用优化检查 */
      if (e.k == VCALL && nret == 1) {
        /* 单个函数调用作为返回值，可以优化为尾调用 */
        SET_OPCODE(getcode(fs,&e), OP_TAILCALL);
        lua_assert(GETARG_A(getcode(fs,&e)) == fs->nactvar);
      }
      
      first = fs->nactvar;  /* 从活动变量开始的寄存器 */
      nret = LUA_MULTRET;   /* 返回所有可能的值 */
    }
    else {
      /* 情况2b: 固定数量的返回值 */
      if (nret == 1) {
        /* 单个返回值，可以放在任意寄存器 */
        first = luaK_exp2anyreg(fs, &e);
      }
      else {
        /* 多个返回值，必须放在连续寄存器中 */
        luaK_exp2nextreg(fs, &e);  /* 确保表达式在下一个寄存器 */
        first = fs->nactvar;       /* 从活动变量开始 */
        lua_assert(nret == fs->freereg - first);  /* 验证寄存器数量 */
      }
    }
  }
  
  /* 步骤3: 生成返回指令 */
  luaK_ret(fs, first, nret);
}


/* 语句解析分发器：根据关键字分发到相应的语句处理函数 */
static void statement (LexState *ls) {
  int line = ls->linenumber;  /* 记录当前行号，用于错误消息和调试信息 */
  
  /* 根据当前token类型分发到不同的语句处理函数 */
  switch (ls->t.token) {
    case TK_IF: {
      /* if-then-else条件语句 */
      ifstat(ls, line);
      return;
    }
    case TK_WHILE: {
      /* while循环语句 */
      whilestat(ls, line);
      return;
    }
    case TK_DO: {
      /* do-end代码块语句 */
      luaX_next(ls);  /* 跳过 DO 关键字 */
      block(ls);      /* 解析代码块内容 */
      check_match(ls, TK_END, TK_DO, line);  /* 检查匹配的END */
      return;
    }
    case TK_FOR: {
      /* for循环语句（数值for或泛型for） */
      forstat(ls, line);
      return;
    }
    case TK_REPEAT: {
      /* repeat-until循环语句 */
      repeatstat(ls, line);
      return;
    }
    case TK_FUNCTION: {
      /* 全局函数定义语句 */
      funcstat(ls, line);
      return;
    }
    case TK_LOCAL: {
      /* 局部变量或局部函数声明 */
      luaX_next(ls);  /* 跳过 LOCAL 关键字 */
      if (testnext(ls, TK_FUNCTION)) {
        /* local function name() ... end */
        localfunc(ls);
      }
      else {
        /* local var1, var2, ... = exp1, exp2, ... */
        localstat(ls);
      }
      return;
    }
    case TK_RETURN: {
      /* 返回语句 */
      retstat(ls);
      return;
    }
    case TK_BREAK: {
      /* 跳出循环语句 */
      luaX_next(ls);  /* 跳过 BREAK 关键字 */
      breakstat(ls);
      return;
    }
    default: {
      /* 默认情况：表达式语句（函数调用或赋值） */
      exprstat(ls);
      return;
    }
  }
}


/* 代码块解析：解析一系列语句直到遇到块结束标记 */
static void chunk (LexState *ls) {
  /* 语法规则: chunk -> { stat [`;'] } */
  int islast = 0;  /* 标记是否遇到最后一个语句（return语句） */
  
  /* 步骤1: 进入新的解析层级，防止栈溢出 */
  enterlevel(ls);
  
  /* 步骤2: 循环解析语句直到块结束或遇到return语句 */
  while (!islast && !block_follow(ls->t.token)) {
    /* 检查是否为return语句，return后不能再有其他语句 */
    islast = (ls->t.token == TK_RETURN);
    
    /* 解析当前语句 */
    statement(ls);
    
    /* 可选的分号分隔符 */
    testnext(ls, ';');
    
    /* 调试断言：验证寄存器分配的正确性 */
    lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
               ls->fs->freereg >= ls->fs->nactvar);
    
    /* 释放临时寄存器，保留活动变量的寄存器 */
    /* 每个语句结束后，只保留局部变量占用的寄存器 */
    ls->fs->freereg = ls->fs->nactvar;
  }
  
  /* 步骤3: 退出解析层级 */
  leavelevel(ls);
}

/* }====================================================================== */