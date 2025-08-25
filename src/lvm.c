/*
** $Id: lvm.c,v 2.63.1.5 2011/08/17 20:43:11 roberto Exp $
** Lua 虚拟机实现
** 版权声明见 lua.h
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lvm_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"



/* 表标签方法链的限制（避免循环） */
#define MAXTAGLOOP	100


/*
** 将对象转换为数字
** 参数：
**   obj - 要转换的对象
**   n - 存储转换结果的TValue指针
** 返回值：
**   成功时返回指向数字的指针，失败时返回NULL
*/
const TValue *luaV_tonumber(const TValue *obj, TValue *n)
{
    lua_Number num;
    /* 如果已经是数字，直接返回 */
    if (ttisnumber(obj))
    {
        return obj;
    }
    /* 尝试将字符串转换为数字 */
    if (ttisstring(obj) && luaO_str2d(svalue(obj), &num))
    {
        /* 设置转换后的数字值 */
        setnvalue(n, num);
        /* 返回转换结果 */
        return n;
    }
    else
    {
        /* 转换失败，返回NULL */
        return NULL;
    }
}


/*
** 将数字转换为字符串
** 参数：
**   L - Lua状态机
**   obj - 要转换的栈位置
** 返回值：
**   成功时返回1，失败时返回0
*/
int luaV_tostring(lua_State *L, StkId obj)
{
    /* 如果不是数字类型 */
    if (!ttisnumber(obj))
    {
        /* 转换失败 */
        return 0;
    }
    else
    {
        /* 分配字符串缓冲区 */
        char s[LUAI_MAXNUMBER2STR];
        /* 获取数字值 */
        lua_Number n = nvalue(obj);
        /* 将数字转换为字符串 */
        lua_number2str(s, n);
        /* 创建新字符串并设置到对象 */
        setsvalue2s(L, obj, luaS_new(L, s));
        /* 转换成功 */
        return 1;
    }
}


/*
** 执行跟踪（调试钩子）
** 参数：
**   L - Lua状态机
**   pc - 当前程序计数器
*/
static void traceexec(lua_State *L, const Instruction *pc)
{
    /* 获取钩子掩码 */
    lu_byte mask = L->hookmask;
    /* 保存旧的程序计数器 */
    const Instruction *oldpc = L->savedpc;
    /* 更新当前程序计数器 */
    L->savedpc = pc;

    /* 检查计数钩子 */
    if ((mask & LUA_MASKCOUNT) && L->hookcount == 0)
    {
        /* 重置钩子计数 */
        resethookcount(L);
        /* 调用计数钩子 */
        luaD_callhook(L, LUA_HOOKCOUNT, -1);
    }

    /* 检查行钩子 */
    if (mask & LUA_MASKLINE)
    {
        /* 获取当前函数原型 */
        Proto *p = ci_func(L->ci)->l.p;
        /* 计算相对程序计数器 */
        int npc = pcRel(pc, p);
        /* 获取当前行号 */
        int newline = getline(p, npc);
        /* 当进入新函数、向后跳转（循环）或进入新行时调用行钩子 */
        if (npc == 0 || pc <= oldpc || newline != getline(p, pcRel(oldpc, p)))
        {
            /* 调用行钩子 */
            luaD_callhook(L, LUA_HOOKLINE, newline);
        }
    }
}


/*
** 调用标签方法并返回结果
** 参数：
**   L - Lua状态机
**   res - 存储结果的栈位置
**   f - 要调用的函数
**   p1 - 第一个参数
**   p2 - 第二个参数
*/
static void callTMres(lua_State *L, StkId res, const TValue *f,
                       const TValue *p1, const TValue *p2)
{
    ptrdiff_t result = savestack(L, res);  /* 保存结果位置（防止栈重分配） */
    setobj2s(L, L->top, f);  /* 压入函数 */
    setobj2s(L, L->top + 1, p1);  /* 第一个参数 */
    setobj2s(L, L->top + 2, p2);  /* 第二个参数 */
    luaD_checkstack(L, 3);  /* 确保栈空间足够 */
    L->top += 3;  /* 调整栈顶指针 */
    luaD_call(L, L->top - 3, 1);  /* 调用函数，期望1个返回值 */
    res = restorestack(L, result);  /* 恢复结果位置 */
    L->top--;  /* 调整栈顶指针 */
    setobjs2s(L, res, L->top);  /* 将结果复制到指定位置 */
}



/*
** 调用标签方法（无返回值）
** 参数：
**   L - Lua状态机
**   f - 要调用的函数
**   p1 - 第一个参数
**   p2 - 第二个参数
**   p3 - 第三个参数
*/
static void callTM(lua_State *L, const TValue *f, const TValue *p1,
                    const TValue *p2, const TValue *p3)
{
    setobj2s(L, L->top, f);  /* 压入函数 */
    setobj2s(L, L->top + 1, p1);  /* 第一个参数 */
    setobj2s(L, L->top + 2, p2);  /* 第二个参数 */
    setobj2s(L, L->top + 3, p3);  /* 第三个参数 */
    luaD_checkstack(L, 4);
    L->top += 4;
    luaD_call(L, L->top - 4, 0);
}


/*
** 获取表元素
** 参数：
**   L - Lua状态机
**   t - 表对象
**   key - 键
**   val - 存储结果的栈位置
*/
void luaV_gettable(lua_State *L, const TValue *t, TValue *key, StkId val)
{
    int loop;
    for (loop = 0; loop < MAXTAGLOOP; loop++)  /* 防止无限循环 */
    {
        const TValue *tm;
        if (ttistable(t))  /* t是表吗？ */
        {
            Table *h = hvalue(t);  /* 获取表结构 */
            const TValue *res = luaH_get(h, key); /* 执行原始获取 */
            if (!ttisnil(res) ||  /* 结果不是nil？ */
                (tm = fasttm(L, h->metatable, TM_INDEX)) == NULL) /* 或者没有TM？ */
            {
                setobj2s(L, val, res);  /* 直接设置结果 */
                return;  /* 获取成功，返回 */
            }
            /* 否则尝试标签方法 */
        }
        else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_INDEX)))  /* 获取__index元方法 */
        {
            luaG_typeerror(L, t, "index");  /* 类型错误 */
        }
        if (ttisfunction(tm))  /* 元方法是函数？ */
        {
            callTMres(L, val, tm, t, key);  /* 调用元方法函数 */
            return;  /* 调用完成，返回 */
        }
        t = tm;  /* 否则用tm重复（元方法是表） */
    }
    luaG_runerror(L, "loop in gettable");  /* 循环次数过多，报错 */
}


/*
** 设置表元素
** 参数：
**   L - Lua状态机
**   t - 表对象
**   key - 键
**   val - 要设置的值
*/
void luaV_settable(lua_State *L, const TValue *t, TValue *key, StkId val)
{
    int loop;
    TValue temp;
    for (loop = 0; loop < MAXTAGLOOP; loop++)  /* 防止无限循环 */
    {
        const TValue *tm;
        if (ttistable(t))  /* t是表吗？ */
        {
            Table *h = hvalue(t);  /* 获取表结构 */
            TValue *oldval = luaH_set(L, h, key); /* 执行原始设置 */
            if (!ttisnil(oldval) ||  /* 结果不是nil？ */
                (tm = fasttm(L, h->metatable, TM_NEWINDEX)) == NULL) /* 或者没有TM？ */
            {
                setobj2t(L, oldval, val);  /* 直接设置值 */
                h->flags = 0;  /* 清除表标志 */
                luaC_barriert(L, h, val);  /* 设置垃圾回收屏障 */
                return;  /* 设置成功，返回 */
            }
            /* 否则尝试标签方法 */
        }
        else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_NEWINDEX)))  /* 获取__newindex元方法 */
        {
            luaG_typeerror(L, t, "index");  /* 类型错误 */
        }
        if (ttisfunction(tm))  /* 元方法是函数？ */
        {
            callTM(L, tm, t, key, val);  /* 调用元方法函数 */
            return;  /* 调用完成，返回 */
        }
        /* 否则用tm重复 */
        setobj(L, &temp, tm);  /* 避免指向表内部（可能重新哈希） */
        t = &temp;  /* 使用临时变量继续循环 */
    }
    luaG_runerror(L, "loop in settable");  /* 循环次数过多，报错 */
}


/*
** 调用二元标签方法
** 参数：
**   L - Lua状态机
**   p1 - 第一个操作数
**   p2 - 第二个操作数
**   res - 存储结果的栈位置
**   event - 事件类型
** 返回值：
**   成功时返回1，失败时返回0
*/
static int call_binTM (lua_State *L, const TValue *p1, const TValue *p2,
                       StkId res, TMS event) {
  const TValue *tm = luaT_gettmbyobj(L, p1, event);  /* 尝试第一个操作数 */
  if (ttisnil(tm))  /* 第一个操作数没有元方法？ */
    tm = luaT_gettmbyobj(L, p2, event);  /* 尝试第二个操作数 */
  if (ttisnil(tm)) return 0;  /* 两个操作数都没有元方法，返回失败 */
  callTMres(L, res, tm, p1, p2);  /* 调用找到的元方法 */
  return 1;  /* 调用成功 */
}


/*
** 获取比较标签方法
** 参数：
**   L - Lua状态机
**   mt1 - 第一个元表
**   mt2 - 第二个元表
**   event - 事件类型
** 返回值：
**   标签方法或NULL
*/
static const TValue *get_compTM (lua_State *L, Table *mt1, Table *mt2,
                                  TMS event) {
  const TValue *tm1 = fasttm(L, mt1, event);
  const TValue *tm2;
  if (tm1 == NULL) return NULL;  /* 没有元方法 */
  if (mt1 == mt2) return tm1;  /* 相同的元表 => 相同的元方法 */
  tm2 = fasttm(L, mt2, event);
  if (tm2 == NULL) return NULL;  /* 没有元方法 */
  if (luaO_rawequalObj(tm1, tm2))  /* 相同的元方法？ */
    return tm1;
  return NULL;
}


/*
** 调用顺序标签方法
** 参数：
**   L - Lua状态机
**   p1 - 第一个操作数
**   p2 - 第二个操作数
**   event - 事件类型
** 返回值：
**   比较结果或-1（失败）
*/
static int call_orderTM (lua_State *L, const TValue *p1, const TValue *p2,
                         TMS event) {
  const TValue *tm1 = luaT_gettmbyobj(L, p1, event);
  const TValue *tm2;
  if (ttisnil(tm1)) return -1;  /* 没有元方法？ */
  tm2 = luaT_gettmbyobj(L, p2, event);
  if (!luaO_rawequalObj(tm1, tm2))  /* 不同的元方法？ */
    return -1;
  callTMres(L, L->top, tm1, p1, p2);
  return !l_isfalse(L->top);
}


/*
** 字符串比较函数
** 参数：
**   ls - 左字符串
**   rs - 右字符串
** 返回值：
**   比较结果（<0, 0, >0）
*/
static int l_strcmp (const TString *ls, const TString *rs) {
  const char *l = getstr(ls);
  size_t ll = ls->tsv.len;
  const char *r = getstr(rs);
  size_t lr = rs->tsv.len;
  for (;;) {
    int temp = strcoll(l, r);
    if (temp != 0) return temp;
    else {  /* 字符串在\0处相等 */
      size_t len = strlen(l);  /* 两个字符串中第一个\0的索引 */
      if (len == lr)  /* r结束了？ */
        return (len == ll) ? 0 : 1;
      else if (len == ll)  /* l结束了？ */
        return -1;  /* l小于r（因为r没有结束） */
      /* 两个字符串都比len长；继续比较（在\0之后） */
      len++;
      l += len; ll -= len; r += len; lr -= len;
    }
  }
}


/*
** 小于比较
** 参数：
**   L - Lua状态机
**   l - 左操作数
**   r - 右操作数
** 返回值：
**   比较结果
*/
int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttype(l) != ttype(r))  /* 类型不同？ */
    return luaG_ordererror(L, l, r);  /* 报告顺序错误 */
  else if (ttisnumber(l))  /* 都是数字？ */
    return luai_numlt(nvalue(l), nvalue(r));  /* 数字比较 */
  else if (ttisstring(l))  /* 都是字符串？ */
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) < 0;  /* 字符串比较 */
  else if ((res = call_orderTM(L, l, r, TM_LT)) != -1)  /* 尝试__lt元方法 */
    return res;  /* 返回元方法结果 */
  return luaG_ordererror(L, l, r);  /* 无法比较，报告错误 */
}


/*
** 小于等于比较
** 参数：
**   L - Lua状态机
**   l - 左操作数
**   r - 右操作数
** 返回值：
**   比较结果
*/
static int lessequal (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttype(l) != ttype(r))
    return luaG_ordererror(L, l, r);
  else if (ttisnumber(l))
    return luai_numle(nvalue(l), nvalue(r));
  else if (ttisstring(l))
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) <= 0;
  else if ((res = call_orderTM(L, l, r, TM_LE)) != -1)  /* 首先尝试le */
    return res;
  else if ((res = call_orderTM(L, r, l, TM_LT)) != -1)  /* 否则尝试lt */
    return !res;
  return luaG_ordererror(L, l, r);
}


/*
** 相等比较
** 参数：
**   L - Lua状态机
**   t1 - 第一个值
**   t2 - 第二个值
** 返回值：
**   比较结果
*/
int luaV_equalval (lua_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  lua_assert(ttype(t1) == ttype(t2));
  switch (ttype(t1)) {
    case LUA_TNIL: return 1;
    case LUA_TNUMBER: return luai_numeq(nvalue(t1), nvalue(t2));
    case LUA_TBOOLEAN: return bvalue(t1) == bvalue(t2);  /* true必须是1！！ */
    case LUA_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case LUA_TUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;
      tm = get_compTM(L, uvalue(t1)->metatable, uvalue(t2)->metatable,
                         TM_EQ);
      break;  /* 将尝试TM */
    }
    case LUA_TTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      tm = get_compTM(L, hvalue(t1)->metatable, hvalue(t2)->metatable, TM_EQ);
      break;  /* 将尝试TM */
    }
    default: return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == NULL) return 0;  /* 没有TM？ */
  callTMres(L, L->top, tm, t1, t2);  /* 调用TM */
  return !l_isfalse(L->top);
}


/*
** 字符串连接
** 参数：
**   L - Lua状态机
**   total - 总元素数
**   last - 最后一个元素的索引
*/
void luaV_concat (lua_State *L, int total, int last) {
  do {
    StkId top = L->base + last + 1;
    int n = 2;  /* 此次处理的元素数（至少2个） */
    if (!(ttisstring(top-2) || ttisnumber(top-2)) || !tostring(L, top-1)) {
      if (!call_binTM(L, top-2, top-1, top-2, TM_CONCAT))
        luaG_concaterror(L, top-2, top-1);
    } else if (tsvalue(top-1)->len == 0)  /* 第二个操作数为空？ */
      (void)tostring(L, top - 2);  /* 结果是第一个操作数（作为字符串） */
    else {
      /* 至少两个字符串值；尽可能获取更多 */
      size_t tl = tsvalue(top-1)->len;
      char *buffer;
      int i;
      /* 收集总长度 */
      for (n = 1; n < total && tostring(L, top-n-1); n++) {
        size_t l = tsvalue(top-n-1)->len;
        if (l >= MAX_SIZET - tl) luaG_runerror(L, "string length overflow");
        tl += l;
      }
      buffer = luaZ_openspace(L, &G(L)->buff, tl);
      tl = 0;
      for (i=n; i>0; i--) {  /* 连接所有字符串 */
        size_t l = tsvalue(top-i)->len;
        memcpy(buffer+tl, svalue(top-i), l);
        tl += l;
      }
      setsvalue2s(L, top-n, luaS_newlstr(L, buffer, tl));
    }
    total -= n-1;  /* 得到n个字符串创建1个新字符串 */
    last -= n-1;
  } while (total > 1);  /* 重复直到只剩1个结果 */
}


/*
** 算术运算
** 参数：
**   L - Lua状态机
**   ra - 结果位置
**   rb - 第一个操作数
**   rc - 第二个操作数
**   op - 操作类型
*/
static void Arith (lua_State *L, StkId ra, const TValue *rb,
                   const TValue *rc, TMS op) {
  TValue tempb, tempc;
  const TValue *b, *c;
  if ((b = luaV_tonumber(rb, &tempb)) != NULL &&
      (c = luaV_tonumber(rc, &tempc)) != NULL) {
    lua_Number nb = nvalue(b), nc = nvalue(c);
    switch (op) {
      case TM_ADD: setnvalue(ra, luai_numadd(nb, nc)); break;
      case TM_SUB: setnvalue(ra, luai_numsub(nb, nc)); break;
      case TM_MUL: setnvalue(ra, luai_nummul(nb, nc)); break;
      case TM_DIV: setnvalue(ra, luai_numdiv(nb, nc)); break;
      case TM_MOD: setnvalue(ra, luai_nummod(nb, nc)); break;
      case TM_POW: setnvalue(ra, luai_numpow(nb, nc)); break;
      case TM_UNM: setnvalue(ra, luai_numunm(nb)); break;
      default: lua_assert(0); break;
    }
  }
  else if (!call_binTM(L, rb, rc, ra, op))
    luaG_aritherror(L, rb, rc);
}



/*
** luaV_execute中常用任务的一些宏
*/

#define runtime_check(L, c)	{ if (!(c)) break; }

#define RA(i)	(base+GETARG_A(i))
/* 在可能的栈重新分配后使用 */
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))
#define KBx(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, k+GETARG_Bx(i))


#define dojump(L,pc,i)	{(pc) += (i); luai_threadyield(L);}


#define Protect(x)	{ L->savedpc = pc; {x;}; base = L->base; }


#define arith_op(op,tm) { \
        TValue *rb = RKB(i); \
        TValue *rc = RKC(i); \
        if (ttisnumber(rb) && ttisnumber(rc)) { \
          lua_Number nb = nvalue(rb), nc = nvalue(rc); \
          setnvalue(ra, op(nb, nc)); \
        } \
        else \
          Protect(Arith(L, ra, rb, rc, tm)); \
      }



/*
** Lua虚拟机执行函数
** 参数：
**   L - Lua状态机
**   nexeccalls - 嵌套执行调用计数
*/
void luaV_execute (lua_State *L, int nexeccalls) {
  LClosure *cl;  /* 当前闭包 */
  StkId base;    /* 栈基址 */
  TValue *k;     /* 常量表 */
  const Instruction *pc;  /* 程序计数器 */
 reentry:  /* 入口点 */
  lua_assert(isLua(L->ci));  /* 确保当前调用信息是Lua函数 */
  pc = L->savedpc;  /* 恢复程序计数器 */
  cl = &clvalue(L->ci->func)->l;  /* 获取当前Lua闭包 */
  base = L->base;  /* 获取栈基址 */
  k = cl->p->k;  /* 获取常量表 */
  /* 解释器主循环 */
  for (;;) {
    const Instruction i = *pc++;  /* 获取当前指令并递增PC */
    StkId ra;  /* 指令的A操作数（结果寄存器） */
    if ((L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT)) &&  /* 检查是否需要调用钩子 */
        (--L->hookcount == 0 || L->hookmask & LUA_MASKLINE)) {
      traceexec(L, pc);  /* 执行跟踪钩子 */
      if (L->status == LUA_YIELD) {  /* 钩子产生了yield？ */
        L->savedpc = pc - 1;  /* 保存当前PC */
        return;  /* 暂停执行 */
      }
      base = L->base;  /* 重新获取栈基址（可能被钩子改变） */
    }
    /* 警告！！几个调用可能重新分配栈并使ra无效 */
    ra = RA(i);  /* 计算A操作数的栈位置 */
    lua_assert(base == L->base && L->base == L->ci->base);  /* 栈一致性检查 */
    lua_assert(base <= L->top && L->top <= L->stack + L->stacksize);  /* 栈边界检查 */
    lua_assert(L->top == L->ci->top || luaG_checkopenop(i));  /* 栈顶一致性检查 */
    switch (GET_OPCODE(i)) {
      case OP_MOVE: {  /* 移动指令：R(A) := R(B) */
        setobjs2s(L, ra, RB(i));  /* 将B寄存器的值复制到A寄存器 */
        continue;
      }
      case OP_LOADK: {  /* 加载常量指令：R(A) := Kst(Bx) */
        setobj2s(L, ra, KBx(i));  /* 将常量表中的值加载到A寄存器 */
        continue;
      }
      case OP_LOADBOOL: {  /* 加载布尔值指令：R(A) := (Bool)B; if (C) pc++ */
        setbvalue(ra, GETARG_B(i));  /* 设置布尔值 */
        if (GETARG_C(i)) pc++;  /* 跳过下一条指令（如果C为真） */
        continue;
      }
      case OP_LOADNIL: {  /* 加载nil指令：R(A) := ... := R(B) := nil */
        TValue *rb = RB(i);  /* 获取结束位置 */
        do {
          setnilvalue(rb--);  /* 从B到A设置为nil */
        } while (rb >= ra);
        continue;
      }
      case OP_GETUPVAL: {  /* 获取上值指令：R(A) := UpValue[B] */
        int b = GETARG_B(i);  /* 获取上值索引 */
        setobj2s(L, ra, cl->upvals[b]->v);  /* 将上值复制到A寄存器 */
        continue;
      }
      case OP_GETGLOBAL: {  /* 获取全局变量指令：R(A) := Gbl[Kst(Bx)] */
        TValue g;  /* 临时变量存储全局表 */
        TValue *rb = KBx(i);  /* 获取变量名（常量） */
        sethvalue(L, &g, cl->env);  /* 设置全局环境表 */
        lua_assert(ttisstring(rb));  /* 确保变量名是字符串 */
        Protect(luaV_gettable(L, &g, rb, ra));  /* 从全局表获取值 */
        continue;
      }
      case OP_GETTABLE: {  /* 获取表元素指令：R(A) := R(B)[RK(C)] */
        Protect(luaV_gettable(L, RB(i), RKC(i), ra));  /* 从表B获取键C对应的值 */
        continue;
      }
      case OP_SETGLOBAL: {  /* 设置全局变量指令：Gbl[Kst(Bx)] := R(A) */
        TValue g;  /* 临时变量存储全局表 */
        sethvalue(L, &g, cl->env);  /* 设置全局环境表 */
        lua_assert(ttisstring(KBx(i)));  /* 确保变量名是字符串 */
        Protect(luaV_settable(L, &g, KBx(i), ra));  /* 设置全局变量值 */
        continue;
      }
      case OP_SETUPVAL: {  /* 设置上值指令：UpValue[B] := R(A) */
        UpVal *uv = cl->upvals[GETARG_B(i)];  /* 获取上值对象 */
        setobj(L, uv->v, ra);  /* 设置上值的值 */
        luaC_barrier(L, uv, ra);  /* 设置写屏障（垃圾回收） */
        continue;
      }
      case OP_SETTABLE: {  /* 设置表元素指令：R(A)[RK(B)] := RK(C) */
        Protect(luaV_settable(L, ra, RKB(i), RKC(i)));  /* 设置表元素值 */
        continue;
      }
      case OP_NEWTABLE: {  /* 创建新表指令：R(A) := {} (size = B,C) */
        int b = GETARG_B(i);  /* 数组部分大小的编码值 */
        int c = GETARG_C(i);  /* 哈希部分大小的编码值 */
        sethvalue(L, ra, luaH_new(L, luaO_fb2int(b), luaO_fb2int(c)));  /* 创建新表 */
        Protect(luaC_checkGC(L));  /* 检查垃圾回收 */
        continue;
      }
      case OP_SELF: {  /* 自引用指令：R(A+1) := R(B); R(A) := R(B)[RK(C)] */
        StkId rb = RB(i);  /* 获取对象 */
        setobjs2s(L, ra+1, rb);  /* 将对象复制到A+1位置（作为self参数） */
        Protect(luaV_gettable(L, rb, RKC(i), ra));  /* 获取方法并存储到A位置 */
        continue;
      }
      case OP_ADD: {  /* 加法指令：R(A) := RK(B) + RK(C) */
        arith_op(luai_numadd, TM_ADD);  /* 执行加法运算 */
        continue;
      }
      case OP_SUB: {  /* 减法指令：R(A) := RK(B) - RK(C) */
        arith_op(luai_numsub, TM_SUB);  /* 执行减法运算 */
        continue;
      }
      case OP_MUL: {  /* 乘法指令：R(A) := RK(B) * RK(C) */
        arith_op(luai_nummul, TM_MUL);  /* 执行乘法运算 */
        continue;
      }
      case OP_DIV: {  /* 除法指令：R(A) := RK(B) / RK(C) */
        arith_op(luai_numdiv, TM_DIV);  /* 执行除法运算 */
        continue;
      }
      case OP_MOD: {  /* 取模指令：R(A) := RK(B) % RK(C) */
        arith_op(luai_nummod, TM_MOD);  /* 执行取模运算 */
        continue;
      }
      case OP_POW: {  /* 幂运算指令：R(A) := RK(B) ^ RK(C) */
        arith_op(luai_numpow, TM_POW);  /* 执行幂运算 */
        continue;
      }
      case OP_UNM: {  /* 取负指令：R(A) := -R(B) */
        TValue *rb = RB(i);  /* 获取操作数 */
        if (ttisnumber(rb)) {  /* 如果是数字类型 */
          lua_Number nb = nvalue(rb);  /* 获取数值 */
          setnvalue(ra, luai_numunm(nb));  /* 直接取负 */
        }
        else {  /* 否则调用元方法 */
          Protect(Arith(L, ra, rb, rb, TM_UNM));  /* 调用__unm元方法 */
        }
        continue;
      }
      case OP_NOT: {  /* 逻辑非指令：R(A) := not R(B) */
        int res = l_isfalse(RB(i));  /* 检查B是否为假值（nil或false） */
        setbvalue(ra, res);  /* 设置结果布尔值 */
        continue;
      }
      case OP_LEN: {  /* 长度指令：R(A) := length of R(B) */
        const TValue *rb = RB(i);  /* 获取操作数 */
        switch (ttype(rb)) {  /* 根据类型处理 */
          case LUA_TTABLE: {  /* 表类型 */
            setnvalue(ra, cast_num(luaH_getn(hvalue(rb))));  /* 获取表长度 */
            break;
          }
          case LUA_TSTRING: {  /* 字符串类型 */
            setnvalue(ra, cast_num(tsvalue(rb)->len));  /* 获取字符串长度 */
            break;
          }
          default: {  /* 其他类型，尝试元方法 */
            Protect(
              if (!call_binTM(L, rb, luaO_nilobject, ra, TM_LEN))  /* 调用__len元方法 */
                luaG_typeerror(L, rb, "get length of");  /* 类型错误 */
            )
          }
        }
        continue;
      }
      case OP_CONCAT: {  /* 连接指令：R(A) := R(B).. ... ..R(C) */
        int b = GETARG_B(i);  /* 起始寄存器 */
        int c = GETARG_C(i);  /* 结束寄存器 */
        Protect(luaV_concat(L, c-b+1, c); luaC_checkGC(L));  /* 连接B到C的值并检查GC */
        setobjs2s(L, RA(i), base+b);  /* 将结果存储到A寄存器 */
        continue;
      }
      case OP_JMP: {  /* 跳转指令：pc += sBx */
        dojump(L, pc, GETARG_sBx(i));  /* 执行跳转 */
        continue;
      }
      case OP_EQ: {  /* 相等比较指令：if ((RK(B) == RK(C)) ~= A) then pc++ */
        TValue *rb = RKB(i);  /* 获取第一个操作数 */
        TValue *rc = RKC(i);  /* 获取第二个操作数 */
        Protect(
          if (equalobj(L, rb, rc) == GETARG_A(i))  /* 比较相等性与A值 */
            dojump(L, pc, GETARG_sBx(*pc));  /* 条件成立则跳转 */
        )
        pc++;  /* 跳过跳转指令 */
        continue;
      }
      case OP_LT: {  /* 小于比较指令：if ((RK(B) < RK(C)) ~= A) then pc++ */
        Protect(
          if (luaV_lessthan(L, RKB(i), RKC(i)) == GETARG_A(i))  /* 比较小于关系与A值 */
            dojump(L, pc, GETARG_sBx(*pc));  /* 条件成立则跳转 */
        )
        pc++;  /* 跳过跳转指令 */
        continue;
      }
      case OP_LE: {  /* 小于等于比较指令：if ((RK(B) <= RK(C)) ~= A) then pc++ */
        Protect(
          if (lessequal(L, RKB(i), RKC(i)) == GETARG_A(i))  /* 比较小于等于关系与A值 */
            dojump(L, pc, GETARG_sBx(*pc));  /* 条件成立则跳转 */
        )
        pc++;  /* 跳过跳转指令 */
        continue;
      }
      case OP_TEST: {  /* 测试指令：if not (R(A) <=> C) then pc++ */
        if (l_isfalse(ra) != GETARG_C(i))  /* 检查A的真假性与C是否不同 */
          dojump(L, pc, GETARG_sBx(*pc));  /* 条件成立则跳转 */
        pc++;  /* 跳过跳转指令 */
        continue;
      }
      case OP_TESTSET: {  /* 测试设置指令：if (R(B) <=> C) then R(A) := R(B) else pc++ */
        TValue *rb = RB(i);  /* 获取测试值 */
        if (l_isfalse(rb) != GETARG_C(i)) {  /* 检查B的真假性与C是否不同 */
          setobjs2s(L, ra, rb);  /* 将B的值复制到A */
          dojump(L, pc, GETARG_sBx(*pc));  /* 跳转 */
        }
        pc++;  /* 跳过跳转指令 */
        continue;
      }
      case OP_CALL: {  /* 函数调用指令：R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */
        int b = GETARG_B(i);  /* 参数个数+1（0表示到栈顶） */
        int nresults = GETARG_C(i) - 1;  /* 期望返回值个数（-1表示多返回值） */
        if (b != 0) L->top = ra+b;  /* 设置栈顶（否则前一条指令设置了top） */
        L->savedpc = pc;  /* 保存程序计数器 */
        switch (luaD_precall(L, ra, nresults)) {  /* 准备函数调用 */
          case PCRLUA: {  /* Lua函数 */
            nexeccalls++;  /* 增加嵌套调用计数 */
            goto reentry;  /* 在新的Lua函数上重新启动luaV_execute */
          }
          case PCRC: {  /* C函数 */
            /* 这是一个C函数（precall调用了它）；调整结果 */
            if (nresults >= 0) L->top = L->ci->top;  /* 调整栈顶 */
            base = L->base;  /* 重新获取栈基址 */
            continue;
          }
          default: {  /* 错误情况 */
            return;  /* yield */
          }
        }
      }
      case OP_TAILCALL: {  /* 尾调用指令：return R(A)(R(A+1), ... ,R(A+B-1)) */
        int b = GETARG_B(i);  /* 参数个数+1 */
        if (b != 0) L->top = ra+b;  /* 设置栈顶（否则前一条指令设置了top） */
        L->savedpc = pc;  /* 保存程序计数器 */
        lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);  /* 尾调用总是多返回值 */
        switch (luaD_precall(L, ra, LUA_MULTRET)) {  /* 准备尾调用 */
          case PCRLUA: {  /* Lua函数的尾调用 */
            /* 尾调用：将新帧放在前一个帧的位置 */
            CallInfo *ci = L->ci - 1;  /* 前一个帧（调用者） */
            int aux;  /* 辅助变量 */
            StkId func = ci->func;  /* 调用者函数位置 */
            StkId pfunc = (ci+1)->func;  /* 被调用函数位置 */
            if (L->openupval) luaF_close(L, ci->base);  /* 关闭上值 */
            L->base = ci->base = ci->func + ((ci+1)->base - pfunc);  /* 调整栈基址 */
            for (aux = 0; pfunc+aux < L->top; aux++)  /* 向下移动帧 */
              setobjs2s(L, func+aux, pfunc+aux);  /* 复制参数和函数 */
            ci->top = L->top = func+aux;  /* 修正top */
            lua_assert(L->top == L->base + clvalue(func)->l.p->maxstacksize);  /* 栈大小检查 */
            ci->savedpc = L->savedpc;  /* 保存PC */
            ci->tailcalls++;  /* 增加尾调用计数 */
            L->ci--;  /* 移除新帧 */
            goto reentry;  /* 重新开始执行 */
          }
          case PCRC: {  /* C函数的尾调用 */
            /* 这是一个C函数（precall调用了它） */
            base = L->base;  /* 重新获取栈基址 */
            continue;
          }
          default: {  /* 错误情况 */
            return;  /* yield */
          }
        }
      }
      case OP_RETURN: {  /* 返回指令：return R(A), ... ,R(A+B-2) */
        int b = GETARG_B(i);  /* 返回值个数+1（0表示到栈顶） */
        if (b != 0) L->top = ra+b-1;  /* 设置返回值的栈顶 */
        if (L->openupval) luaF_close(L, base);  /* 关闭所有上值 */
        L->savedpc = pc;  /* 保存程序计数器 */
        b = luaD_poscall(L, ra);  /* 完成函数调用，返回是否有更多结果 */
        if (--nexeccalls == 0)  /* 这是主要的（外部）函数吗？ */
          return;  /* 是的，返回到'luaD_call' */
        else {  /* 不，继续在'luaV_execute'中执行 */
          if (b) L->top = L->ci->top;  /* 调整栈顶 */
          lua_assert(isLua(L->ci));  /* 确保当前是Lua函数 */
          lua_assert(GET_OPCODE(*((L->ci)->savedpc - 1)) == OP_CALL);  /* 确保前一条是CALL指令 */
          goto reentry;  /* 重新开始执行 */
        }
      }
      case OP_FORLOOP: {  /* 数值for循环指令：R(A)+=R(A+2); if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) } */
        lua_Number step = nvalue(ra+2);  /* 获取步长 */
        lua_Number idx = luai_numadd(nvalue(ra), step);  /* 计算新的索引值 */
        lua_Number limit = nvalue(ra+1);  /* 获取限制值 */
        if (luai_numlt(0, step) ? luai_numle(idx, limit)  /* 正步长：检查idx <= limit */
                                : luai_numle(limit, idx)) {  /* 负步长：检查limit <= idx */
          dojump(L, pc, GETARG_sBx(i));  /* 跳回循环开始 */
          setnvalue(ra, idx);  /* 更新内部索引变量 */
          setnvalue(ra+3, idx);  /* 更新外部循环变量 */
        }
        continue;
      }
      case OP_FORPREP: {  /* 数值for循环准备指令：R(A)-=R(A+2); pc+=sBx */
        const TValue *init = ra;  /* 初始值 */
        const TValue *plimit = ra+1;  /* 限制值 */
        const TValue *pstep = ra+2;  /* 步长值 */
        L->savedpc = pc;  /* 保存PC，下一条指令将设置savedpc */
        if (!tonumber(init, ra))  /* 转换初始值为数字 */
          luaG_runerror(L, LUA_QL("for") " initial value must be a number");
        else if (!tonumber(plimit, ra+1))  /* 转换限制值为数字 */
          luaG_runerror(L, LUA_QL("for") " limit must be a number");
        else if (!tonumber(pstep, ra+2))  /* 转换步长为数字 */
          luaG_runerror(L, LUA_QL("for") " step must be a number");
        setnvalue(ra, luai_numsub(nvalue(ra), nvalue(pstep)));  /* 初始值减去步长 */
        dojump(L, pc, GETARG_sBx(i));  /* 跳转到循环开始 */
        continue;
      }
      case OP_TFORLOOP: {  /* 泛型for循环指令：R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2)) */
        StkId cb = ra + 3;  /* 调用基址 */
        setobjs2s(L, cb+2, ra+2);  /* 复制控制变量 */
        setobjs2s(L, cb+1, ra+1);  /* 复制状态变量 */
        setobjs2s(L, cb, ra);  /* 复制迭代器函数 */
        L->top = cb+3;  /* 设置栈顶：函数 + 2个参数（状态和控制） */
        Protect(luaD_call(L, cb, GETARG_C(i)));  /* 调用迭代器函数 */
        L->top = L->ci->top;  /* 恢复栈顶 */
        cb = RA(i) + 3;  /* 重新计算cb（前一个调用可能改变栈） */
        if (!ttisnil(cb)) {  /* 继续循环？（第一个返回值不为nil） */
          setobjs2s(L, cb-1, cb);  /* 保存控制变量 */
          dojump(L, pc, GETARG_sBx(*pc));  /* 跳回循环开始 */
        }
        pc++;  /* 跳过跳转指令 */
        continue;
      }
      case OP_SETLIST: {  /* 设置列表指令：R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B */
        int n = GETARG_B(i);  /* 要设置的元素个数 */
        int c = GETARG_C(i);  /* 块编号 */
        int last;  /* 最后一个索引 */
        Table *h;  /* 表对象 */
        if (n == 0) {  /* 如果n为0，使用栈顶到ra的所有值 */
          n = cast_int(L->top - ra) - 1;  /* 计算实际元素个数 */
          L->top = L->ci->top;  /* 恢复栈顶 */
        }
        if (c == 0) c = cast_int(*pc++);  /* 如果c为0，从下一条指令获取 */
        runtime_check(L, ttistable(ra));  /* 确保ra是表 */
        h = hvalue(ra);  /* 获取表对象 */
        last = ((c-1)*LFIELDS_PER_FLUSH) + n;  /* 计算最后一个索引 */
        if (last > h->sizearray)  /* 需要更多空间？ */
          luaH_resizearray(L, h, last);  /* 预分配数组空间 */
        for (; n > 0; n--) {  /* 设置每个元素 */
          TValue *val = ra+n;  /* 获取值 */
          setobj2t(L, luaH_setnum(L, h, last--), val);  /* 设置表元素 */
          luaC_barriert(L, h, val);  /* 设置写屏障 */
        }
        continue;
      }
      case OP_CLOSE: {  /* 关闭上值指令：close all variables in the stack up to (>=) R(A) */
        luaF_close(L, ra);  /* 关闭从ra开始的所有上值 */
        continue;
      }
      case OP_CLOSURE: {  /* 创建闭包指令：R(A) := closure(KPROTO[Bx], R(A), ... ,R(A+n)) */
        Proto *p;  /* 函数原型 */
        Closure *ncl;  /* 新闭包 */
        int nup, j;  /* 上值个数和循环变量 */
        p = cl->p->p[GETARG_Bx(i)];  /* 获取函数原型 */
        nup = p->nups;  /* 获取上值个数 */
        ncl = luaF_newLclosure(L, nup, cl->env);  /* 创建新的Lua闭包 */
        ncl->l.p = p;  /* 设置函数原型 */
        for (j = 0; j < nup; j++, pc++) {  /* 设置每个上值 */
          if (GET_OPCODE(*pc) == OP_GETUPVAL)  /* 从当前闭包的上值获取 */
            ncl->l.upvals[j] = cl->upvals[GETARG_B(*pc)];
          else {  /* 从栈上的局部变量创建上值 */
            lua_assert(GET_OPCODE(*pc) == OP_MOVE);
            ncl->l.upvals[j] = luaF_findupval(L, base + GETARG_B(*pc));
          }
        }
        setclvalue(L, ra, ncl);  /* 将闭包设置到ra */
        Protect(luaC_checkGC(L));  /* 检查垃圾回收 */
        continue;
      }
      case OP_VARARG: {
        int b = GETARG_B(i) - 1;
        int j;
        CallInfo *ci = L->ci;
        int n = cast_int(ci->base - ci->func) - cl->p->numparams - 1;
        if (b == LUA_MULTRET) {
          Protect(luaD_checkstack(L, n));
          ra = RA(i);  /* 前一个调用可能改变栈 */
          b = n;
          L->top = ra + n;
        }
        for (j = 0; j < b; j++) {
          if (j < n) {
            setobjs2s(L, ra + j, ci->base - n + j);
          }
          else {
            setnilvalue(ra + j);
          }
        }
        continue;
      }
    }
  }
}