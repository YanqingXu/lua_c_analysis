# Lua 对象系统详解

## 概述

Lua 的对象系统是整个语言的基础，提供了统一的值表示和类型系统。所有 Lua 值都通过 Tagged Values (TValue) 表示，支持 8 种基本类型，并通过垃圾回收管理内存。

## 核心数据结构

### 1. Tagged Value (TValue)

```c
/*
** Union of all Lua values
*/
typedef union {
  GCObject *gc;
  void *p;
  lua_Number n;
  int b;
} Value;

#define TValuefields	Value value; int tt

typedef struct lua_TValue {
  TValuefields;
} TValue;
```

### 2. 类型系统

```c
// 基本类型常量
#define LUA_TNIL           0
#define LUA_TBOOLEAN       1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMBER        3
#define LUA_TSTRING        4
#define LUA_TTABLE         5
#define LUA_TFUNCTION      6
#define LUA_TUSERDATA      7
#define LUA_TTHREAD        8

// 内部类型（不在 Lua 中可见）
#define LUA_TPROTO      (LAST_TAG+1)  // 函数原型
#define LUA_TUPVAL      (LAST_TAG+2)  // upvalue
#define LUA_TDEADKEY    (LAST_TAG+3)  // 死亡键
```

### 3. 类型检查宏

```c
// 类型判断宏
#define ttisnil(o)          (ttype(o) == LUA_TNIL)
#define ttisnumber(o)       (ttype(o) == LUA_TNUMBER)
#define ttisstring(o)       (ttype(o) == LUA_TSTRING)
#define ttistable(o)        (ttype(o) == LUA_TTABLE)
#define ttisfunction(o)     (ttype(o) == LUA_TFUNCTION)
#define ttisboolean(o)      (ttype(o) == LUA_TBOOLEAN)
#define ttisuserdata(o)     (ttype(o) == LUA_TUSERDATA)
#define ttisthread(o)       (ttype(o) == LUA_TTHREAD)
#define ttislightuserdata(o) (ttype(o) == LUA_TLIGHTUSERDATA)

// 获取类型
#define ttype(o)            ((o)->tt)
```

### 4. 值访问宏

```c
// 值获取宏
#define gcvalue(o)     check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o)      check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o)      check_exp(ttisnumber(o), (o)->value.n)
#define rawtsvalue(o)  check_exp(ttisstring(o), &(o)->value.gc->ts)
#define tsvalue(o)     (&rawtsvalue(o)->tsv)
#define rawuvalue(o)   check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define uvalue(o)      (&rawuvalue(o)->uv)
#define clvalue(o)     check_exp(ttisfunction(o), &(o)->value.gc->cl)
#define hvalue(o)      check_exp(ttistable(o), &(o)->value.gc->h)
#define bvalue(o)      check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o)     check_exp(ttisthread(o), &(o)->value.gc->th)
```

## 各类型详细实现

### 1. Nil 类型

Nil 是 Lua 中表示"无值"的特殊类型：

```c
#define setnilvalue(obj) ((obj)->tt=LUA_TNIL)

// 全局 nil 对象
LUAI_DATA const TValue luaO_nilobject_;
#define luaO_nilobject (&luaO_nilobject_)

// 假值判断（nil 和 false）
#define l_isfalse(o) (ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))
```

### 2. Boolean 类型

布尔类型存储 true/false 值：

```c
#define setbvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN; }
```

### 3. Number 类型

数字类型使用 lua_Number（通常是 double）：

```c
#define setnvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER; }

// 数字转换函数
const TValue *luaV_tonumber (const TValue *obj, TValue *n) {
  lua_Number num;
  if (ttisnumber(obj)) return obj;
  if (ttisstring(obj) && luaO_str2d(svalue(obj), &num)) {
    setnvalue(n, num);
    return n;
  }
  else return NULL;
}
```

### 4. String 类型

字符串是不可变的，使用内化机制提高效率：

```c
typedef union TString {
  L_Umaxalign dummy;  // 确保最大对齐
  struct {
    CommonHeader;       // GC 头部
    lu_byte reserved;   // 保留字标志
    unsigned int hash;  // 哈希值
    size_t len;        // 字符串长度
  } tsv;
} TString;

#define getstr(ts)  cast(const char *, (ts) + 1)
#define svalue(o)   getstr(tsvalue(o))
```

#### 字符串内化

```c
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  GCObject *o;
  unsigned int h = cast(unsigned int, l);  // 种子
  size_t step = (l>>5)+1;  // 如果字符串太长，不哈希所有字符
  size_t l1;
  
  // 计算哈希值
  for (l1=l; l1>=step; l1-=step)
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
  
  // 在字符串表中查找
  for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
       o != NULL;
       o = o->gch.next) {
    TString *ts = rawgco2ts(o);
    if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0)) {
      // 找到现有字符串
      if (isdead(G(L), o))  // 死亡的？
        changewhite(o);     // 复活它
      return ts;
    }
  }
  
  // 创建新字符串
  return newlstr(L, str, l, h);  // 未找到，创建新的
}
```

### 5. Table 类型

表是 Lua 的唯一数据结构化类型，详细实现见 [表实现详解](wiki_table.md)。

### 6. Function 类型

函数分为 Lua 函数和 C 函数，详细实现见 [函数系统详解](wiki_function.md)。

### 7. Userdata 类型

用户数据允许在 Lua 中表示任意的 C 数据：

```c
typedef union Udata {
  L_Umaxalign dummy;  // 确保最大对齐
  struct {
    CommonHeader;
    struct Table *metatable;  // 元表
    struct Table *env;        // 环境表
    size_t len;              // 数据长度
  } uv;
} Udata;

#define sizeudata(u) (sizeof(union Udata)+(u)->len)
```

#### 创建用户数据

```c
void *lua_newuserdata (lua_State *L, size_t size) {
  Udata *u;
  lua_lock(L);
  luaC_checkGC(L);
  u = cast(Udata *, luaM_malloc(L, sizeludata(size)));
  u->uv.marked = luaC_white(G(L));
  u->uv.tt = LUA_TUSERDATA;
  u->uv.len = size;
  u->uv.metatable = NULL;
  u->uv.env = G(L)->l_gt;
  // 链接到 GC 链表
  u->uv.next = G(L)->rootgc;
  G(L)->rootgc = obj2gco(u);
  setuvalue(L, L->top, u);
  api_incr_top(L);
  lua_unlock(L);
  return u + 1;  // 返回用户数据部分
}
```

### 8. Thread 类型

线程（协程）是独立的执行状态，详细实现见 [调用栈管理详解](wiki_call.md)。

### 9. Light Userdata 类型

轻量用户数据直接存储 C 指针，不被 GC 管理：

```c
#define setpvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA; }

void lua_pushlightuserdata (lua_State *L, void *p) {
  lua_lock(L);
  setpvalue(L->top, p);
  api_incr_top(L);
  lua_unlock(L);
}
```

## 值的设置和复制

### 1. 设置值宏

```c
// 设置各种类型的值
#define setnilvalue(obj)     ((obj)->tt=LUA_TNIL)
#define setnvalue(obj,x)     {TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER;}
#define setpvalue(obj,x)     {TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA;}
#define setbvalue(obj,x)     {TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN;}

// 设置 GC 对象（需要检查生命周期）
#define setsvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TSTRING; \
    checkliveness(G(L),i_o); }

#define setuvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TUSERDATA; \
    checkliveness(G(L),i_o); }

#define sethvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTABLE; \
    checkliveness(G(L),i_o); }

#define setclvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TFUNCTION; \
    checkliveness(G(L),i_o); }

#define setthvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTHREAD; \
    checkliveness(G(L),i_o); }
```

### 2. 对象复制

```c
#define setobj(L,obj1,obj2) \
  { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; o1->tt=o2->tt; \
    checkliveness(G(L),o1); }

// 根据目标位置的不同优化
#define setobjs2s   setobj    // 栈到栈
#define setobj2s    setobj    // 到栈
#define setobj2t    setobj    // 到表
#define setobj2n    setobj    // 到新对象
```

## 类型转换

### 1. 数字转换

```c
int luaV_tostring (lua_State *L, StkId obj) {
  if (!ttisnumber(obj))
    return 0;
  else {
    char s[LUAI_MAXNUMBER2STR];
    lua_Number n = nvalue(obj);
    lua_number2str(s, n);
    setsvalue2s(L, obj, luaS_new(L, s));
    return 1;
  }
}
```

### 2. 字符串转换

```c
const char *luaO_pushvfstring (lua_State *L, const char *fmt, va_list argp) {
  int n = 1;
  const char *p = fmt;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  
  while ((p = strchr(p, '%')) != NULL) {
    luaL_addlstring(&b, fmt, p-fmt);
    switch (*(++p)) {
      case 's': {
        const char *s = va_arg(argp, char *);
        if (s == NULL) s = "(null)";
        luaL_addstring(&b, s);
        break;
      }
      case 'c': {
        char buff;
        buff = cast(char, va_arg(argp, int));
        luaL_addlstring(&b, &buff, 1);
        break;
      }
      case 'd': {
        luaL_addstring(&b, luaO_pushfstring(L, "%d", va_arg(argp, int)));
        break;
      }
      case 'f': {
        luaL_addstring(&b, luaO_pushfstring(L, "%f", va_arg(argp, l_uacNumber)));
        break;
      }
      case 'p': {
        char buff[4*sizeof(void *) + 8]; // 应该足够容纳一个 '%p'
        sprintf(buff, "%p", va_arg(argp, void *));
        luaL_addstring(&b, buff);
        break;
      }
      case '%': {
        luaL_addchar(&b, '%');
        break;
      }
      default: {
        char buff[3];
        buff[0] = '%';
        buff[1] = *p;
        buff[2] = '\0';
        luaL_addstring(&b, buff);
        break;
      }
    }
    p++;
    fmt = p;
  }
  luaL_addstring(&b, fmt);
  luaL_pushresult(&b);
  return lua_tostring(L, -1);
}
```

## 对象比较

### 1. 相等比较

```c
int luaV_equalval (lua_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  lua_assert(ttype(t1) == ttype(t2));
  
  switch (ttype(t1)) {
    case LUA_TNIL: return 1;
    case LUA_TNUMBER: return luai_numeq(nvalue(t1), nvalue(t2));
    case LUA_TBOOLEAN: return bvalue(t1) == bvalue(t2);  // true 必须等于 true
    case LUA_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case LUA_TUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;
      tm = get_compTM(L, uvalue(t1)->metatable, uvalue(t2)->metatable, TM_EQ);
      break;  // 尝试元方法
    }
    case LUA_TTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      tm = get_compTM(L, hvalue(t1)->metatable, hvalue(t2)->metatable, TM_EQ);
      break;  // 尝试元方法
    }
    default: return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == NULL) return 0;  // 没有元方法
  callTMres(L, tm, t1, t2, L->top);  // 调用元方法
  return !l_isfalse(L->top);
}
```

### 2. 小于比较

```c
int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttype(l) != ttype(r))
    return luaG_ordererror(L, l, r);
  else if (ttisnumber(l))
    return luai_numlt(nvalue(l), nvalue(r));
  else if (ttisstring(l))
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) < 0;
  else if ((res = call_orderTM(L, l, r, TM_LT)) != -1)
    return res;
  return luaG_ordererror(L, l, r);
}
```

## 栈索引和操作

### 1. 栈索引类型

```c
typedef TValue *StkId;  // 栈元素索引

// 特殊索引
#define LUA_REGISTRYINDEX   (-10000)
#define LUA_ENVIRONINDEX    (-10001)
#define LUA_GLOBALSINDEX    (-10002)
#define lua_upvalueindex(i) (LUA_GLOBALSINDEX-(i))
```

### 2. 索引有效性检查

```c
static TValue *index2adr (lua_State *L, int idx) {
  if (idx > 0) {
    TValue *o = L->base + (idx - 1);
    api_check(L, idx <= L->ci->top - L->base);
    if (o >= L->top) return cast(TValue *, luaO_nilobject);
    else return o;
  }
  else if (idx > LUA_REGISTRYINDEX) {
    api_check(L, idx != 0 && -idx <= L->top - L->base);
    return L->top + idx;
  }
  else switch (idx) {  // 伪索引
    case LUA_REGISTRYINDEX: return registry(L);
    case LUA_ENVIRONINDEX: {
      Closure *func = curr_func(L);
      sethvalue(L, &L->env, func->c.env);
      return &L->env;
    }
    case LUA_GLOBALSINDEX: return gt(L);
    default: {
      Closure *func = curr_func(L);
      idx = LUA_GLOBALSINDEX - idx;
      return (idx <= func->c.nupvalues)
                ? &func->c.upvalue[idx-1]
                : cast(TValue *, luaO_nilobject);
    }
  }
}
```

## 调试支持

### 1. 对象信息

```c
const char *luaO_chunkid (char *out, const char *source, size_t bufflen) {
  if (*source == '=') {
    strncpy(out, source+1, bufflen);  // 移除第一个字符
    out[bufflen-1] = '\0';  // 确保以 '\0' 结尾
  }
  else {  // 一般情况
    if (*source == '@') {
      size_t l;
      source++;  // 跳过 '@'
      bufflen -= sizeof(" '...' ");
      l = strlen(source);
      strcpy(out, "");
      if (l > bufflen) {
        source += (l-bufflen);  // 获取最后部分的文件名
        strcat(out, "...");
      }
      strcat(out, source);
    }
    else {  // 字符串；格式为 [string "source"]
      const char *nl = strchr(source, '\n');  // 找第一个新行
      strcpy(out, "[string \"");
      bufflen -= sizeof("[string \"]");
      if (l < bufflen && nl == NULL) {  // 小字符串
        strcat(out, source);
      }
      else {
        if (nl != NULL) l = nl-source;  // 停在第一个新行
        if (l > bufflen) l = bufflen;
        strncat(out, source, l);
        strcat(out, "...");
      }
      strcat(out, "\"]");
    }
  }
}
```

## 总结

Lua 的对象系统通过以下设计实现了简洁高效的类型管理：

1. **统一值表示**：所有值都通过 TValue 结构表示
2. **类型标记**：简单高效的类型检查机制
3. **内存管理**：自动垃圾回收管理对象生命周期
4. **字符串内化**：提高字符串比较和存储效率
5. **灵活索引**：支持正数、负数和伪索引
6. **类型转换**：自动和显式的类型转换机制
7. **元表支持**：强大的元编程能力

这种设计使得 Lua 在保持语言简洁性的同时，提供了强大的类型系统和对象操作能力。

---

*相关文档：[表实现](wiki_table.md) | [函数系统](wiki_function.md) | [垃圾回收器](wiki_gc.md) | [虚拟机执行](wiki_vm.md)*