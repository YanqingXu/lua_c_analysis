# Lua 垃圾回收器详解

## 概述

Lua 5.1 使用增量三色标记-清除垃圾回收算法。这种设计允许垃圾回收器与程序执行交错进行，避免长时间的停顿，同时保持内存使用的高效性。

## 垃圾回收基础

### 1. GC 对象结构

所有可回收对象都包含通用的 GC 头部：

```c
#define CommonHeader  GCObject *next; lu_byte tt; lu_byte marked

typedef struct GCheader {
  CommonHeader;
} GCheader;

// 所有 GC 对象的联合
union GCObject {
  GCheader gch;           // 通用头部
  union TString ts;       // 字符串
  union Udata u;          // 用户数据
  union Closure cl;       // 闭包
  struct Table h;         // 表
  struct Proto p;         // 函数原型
  struct UpVal uv;        // upvalue
  struct lua_State th;    // 线程
};
```

### 2. 颜色标记系统

垃圾回收器使用三色标记算法：

```c
// marked 字段的位布局
#define WHITE0BIT    0  // 白色类型 0
#define WHITE1BIT    1  // 白色类型 1  
#define BLACKBIT     2  // 黑色
#define FINALIZEDBIT 3  // 已终结化（用户数据）
#define KEYWEAKBIT   3  // 键弱引用（表）
#define VALUEWEAKBIT 4  // 值弱引用（表）
#define FIXEDBIT     5  // 固定对象（不回收）
#define SFIXEDBIT    6  // 超级固定（主线程）

#define WHITEBITS    bit2mask(WHITE0BIT, WHITE1BIT)

// 颜色检查宏
#define iswhite(x)   test2bits((x)->gch.marked, WHITE0BIT, WHITE1BIT)
#define isblack(x)   testbit((x)->gch.marked, BLACKBIT)
#define isgray(x)    (!isblack(x) && !iswhite(x))
```

### 3. GC 状态

```c
/*
** Possible states of the Garbage Collector
*/
#define GCSpause	0
#define GCSpropagate	1
#define GCSsweepstring	2
#define GCSsweep	3
#define GCSfinalize	4
```

## 全局 GC 状态

```c
typedef struct global_State {
  stringtable strt;        // 字符串表
  lua_Alloc frealloc;      // 内存分配函数
  void *ud;               // 分配器用户数据
  lu_byte currentwhite;   // 当前白色
  lu_byte gcstate;        // GC 状态
  int sweepstrgc;         // 字符串清除位置
  GCObject *rootgc;       // 所有可回收对象根
  GCObject **sweepgc;     // 清除位置指针
  GCObject *gray;         // 灰色对象链表
  GCObject *grayagain;    // 需要重新标记的对象
  GCObject *weak;         // 弱表链表
  GCObject *tmudata;      // 有终结器的用户数据
  Mbuffer buff;           // 字符串连接缓冲区
  lu_mem GCthreshold;     // GC 阈值
  lu_mem totalbytes;      // 当前分配字节数
  lu_mem estimate;        // 实际使用字节数估计
  lu_mem gcdept;          // GC "债务"
  int gcpause;            // GC 暂停参数
  int gcstepmul;          // GC 步长倍数
  // ...
} global_State;
```

## 内存分配和释放

### 1. 基础内存管理

```c
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize) {
  global_State *g = G(L);
  lua_assert((osize == 0) == (block == NULL));
  block = (*g->frealloc)(g->ud, block, osize, nsize);
  
  if (block == NULL && nsize > 0)
    luaD_throw(L, LUA_ERRMEM);
  
  lua_assert((nsize == 0) == (block == NULL));
  g->totalbytes = (g->totalbytes - osize) + nsize;
  return block;
}

// 分配新对象
#define luaM_new(L,t)       cast(t *, luaM_malloc(L, sizeof(t)))
#define luaM_newvector(L,n,t) cast(t *, luaM_malloc(L, (n)*sizeof(t)))
#define luaM_free(L, b)     luaM_realloc_(L, (b), sizeof(*(b)), 0)
```

### 2. GC 对象链接

```c
void luaC_link (lua_State *L, GCObject *o, lu_byte tt) {
  global_State *g = G(L);
  o->gch.next = g->rootgc;  // 链接到根链表
  g->rootgc = o;
  o->gch.marked = luaC_white(g);  // 标记为当前白色
  o->gch.tt = tt;
}
```

## 标记阶段

### 1. 标记传播

```c
static void propagatemark (global_State *g) {
  GCObject *o = g->gray;
  lua_assert(isgray(o));
  gray2black(o);  // 从灰色变为黑色
  
  switch (o->gch.tt) {
    case LUA_TTABLE: {
      Table *h = gco2h(o);
      g->gray = h->gclist;
      if (luaH_next(L, h, &n))  // 遍历表中的元素
        reallymarkobject(g, gkey(&n));  // 标记键
      // 标记数组部分
      for (i = 0; i < h->sizearray; i++)
        markvalue(g, &h->array[i]);
      // 标记哈希部分  
      for (i = 0; i < sizenode(h); i++) {
        Node *n = gnode(h, i);
        if (!ttisnil(gval(n))) {
          lua_assert(!ttisnil(gkey(n)));
          markvalue(g, gkey(n));
          markvalue(g, gval(n));
        }
      }
      break;
    }
    case LUA_TFUNCTION: {
      Closure *cl = gco2cl(o);
      g->gray = cl->c.gclist;
      markobject(g, cl->c.env);  // 标记环境
      if (cl->c.isC) {
        for (i=0; i<cl->c.nupvalues; i++)  // 标记 upvalue
          markvalue(g, &cl->c.upvalue[i]);
      }
      else {
        markobject(g, cl->l.p);  // 标记原型
        for (i=0; i<cl->l.nupvalues; i++)  // 标记 upvalue
          markobject(g, cl->l.upvals[i]);
      }
      break;
    }
    case LUA_TTHREAD: {
      lua_State *th = gco2th(o);
      g->gray = th->gclist;
      markvalue(g, gt(th));  // 全局表
      markvalue(g, &th->env);  // 环境
      traversestack(g, th);   // 遍历栈
      break;
    }
    case LUA_TPROTO: {
      Proto *f = gco2p(o);
      g->gray = f->gclist;
      for (i=0; i<f->sizek; i++)  // 标记常量
        markvalue(g, &f->k[i]);
      for (i=0; i<f->sizeupvalues; i++)  // 标记 upvalue 名
        markobject(g, f->upvalues[i]);
      for (i=0; i<f->sizep; i++)  // 标记子原型
        markobject(g, f->p[i]);
      for (i=0; i<f->sizelocvars; i++)  // 标记局部变量名
        markobject(g, f->locvars[i].varname);
      markobject(g, f->source);  // 标记源文件名
      break;
    }
  }
}
```

### 2. 原子性标记

```c
static void atomic (lua_State *L) {
  global_State *g = G(L);
  size_t udsize;  // 用户数据总大小
  
  // 重新标记可能在并发标记中变化的对象
  remarkupvals(g);
  propagateall(g);
  
  // 标记弱表
  g->gray = g->weak;
  g->weak = NULL;
  lua_assert(!iswhite(obj2gco(g->mainthread)));
  markobject(g, L);  // 标记状态本身
  markmt(g);  // 标记所有元表
  
  // 再次传播
  propagateall(g);
  
  // 分离用户数据以便终结化
  udsize = luaC_separateudata(L, 0);  // 分离所有用户数据
  marktmu(g);  // 标记有终结器的用户数据
  udsize += propagateall(g);  // 再次传播
  
  cleartable(g->weak);  // 清理弱表
  
  // 切换白色
  g->currentwhite = cast_byte(otherwhite(g));
  g->sweepstrgc = 0;
  g->sweepgc = &g->rootgc;
  g->gcstate = GCSsweepstring;
  g->estimate = g->totalbytes - udsize;
}
```

## 清除阶段

### 1. 字符串清除

```c
static lu_mem sweepwholelist (lua_State *L, GCObject **p) {
  GCObject *curr;
  global_State *g = G(L);
  lu_mem size = 0;
  
  while ((curr = *p) != NULL) {
    if (curr->gch.tt == LUA_TTHREAD)
      sweepthread(L, gco2th(curr));
    
    if ((curr->gch.marked ^ WHITEBITS) & g->currentwhite) {
      *p = curr->gch.next;  // 移除死亡对象
      if (curr->gch.tt == LUA_TUSERDATA)
        size += sizeudata(gco2u(curr));
      freeobj(L, curr);  // 释放对象
    }
    else {  // 对象存活
      lua_assert(curr->gch.marked & WHITEBITS);
      curr->gch.marked &= cast_byte(~WHITEBITS);  // 清除白色标记
      p = &curr->gch.next;
    }
  }
  return size;
}
```

### 2. 增量清除

```c
static GCObject **sweeplist (lua_State *L, GCObject **p, lu_mem count) {
  GCObject *curr;
  global_State *g = G(L);
  int deadmask = otherwhite(g);
  
  while ((curr = *p) != NULL && count-- > 0) {
    if (curr->gch.tt == LUA_TTHREAD)
      sweepthread(L, gco2th(curr));
    
    if ((curr->gch.marked ^ WHITEBITS) & deadmask) {
      *p = curr->gch.next;
      freeobj(L, curr);
    }
    else {
      curr->gch.marked &= cast_byte(~deadmask);
      p = &curr->gch.next;
    }
  }
  return p;
}
```

## 终结化

### 1. 用户数据分离

```c
size_t luaC_separateudata (lua_State *L, int all) {
  global_State *g = G(L);
  size_t deadmem = 0;
  GCObject **p = &g->rootgc;
  GCObject *curr;
  
  while ((curr = *p) != NULL) {
    if (!(iswhite(curr) || all) || isfinalized(gco2u(curr)))
      p = &curr->gch.next;  // 不需要终结化
    else if (fasttm(L, gco2u(curr)->metatable, TM_GC) == NULL) {
      markfinalized(gco2u(curr));  // 没有终结器
      p = &curr->gch.next;
    }
    else {  // 必须调用终结器
      deadmem += sizeudata(gco2u(curr));
      markfinalized(gco2u(curr));
      *p = curr->gch.next;
      // 链接到 'tmudata' 列表
      curr->gch.next = g->tmudata;
      g->tmudata = curr;
    }
  }
  return deadmem;
}
```

### 2. 调用终结器

```c
void luaC_callGCTM (lua_State *L) {
  global_State *g = G(L);
  
  while (g->tmudata) {
    GCObject *o = g->tmudata;
    Udata *udata = rawgco2u(o);
    const TValue *tm;
    
    g->tmudata = udata->uv.next;  // 从列表中移除
    udata->uv.next = g->rootgc;   // 返回到主列表
    g->rootgc = o;
    makewhite(g, o);  // 对象又变成白色
    tm = fasttm(L, udata->metatable, TM_GC);
    
    if (tm != NULL) {
      lu_byte oldah = L->allowhook;
      lu_mem oldt = g->GCthreshold;
      L->allowhook = 0;  // 停止调试钩子
      g->GCthreshold = 2*g->totalbytes;  // 避免在终结器中 GC
      L->top++;
      setobj2s(L, L->top - 1, tm);
      setuvalue(L, L->top, udata);
      L->top++;
      luaD_call(L, L->top - 2, 0);  // 调用终结器
      L->allowhook = oldah;  // 恢复钩子
      g->GCthreshold = oldt;  // 恢复阈值
    }
  }
}
```

## 弱引用处理

### 1. 弱表标记

```c
void luaC_linkupval (lua_State *L, UpVal *uv) {
  global_State *g = G(L);
  GCObject *o = obj2gco(uv);
  o->gch.next = g->rootgc;  // 链接到根列表
  g->rootgc = o;
  if (isgray(o)) {
    if (g->gcstate == GCSpropagate) {
      gray2black(o);  // 闭合状态：变黑
      luaC_linkupval(L, uv);  // 确保它在某个列表中
    }
    else {  // 开放状态：保持灰色
      o->gch.gclist = g->gray;
      g->gray = o;
    }
  }
}
```

### 2. 清理弱表

```c
static void cleartable (GCObject *l) {
  while (l) {
    Table *h = gco2h(l);
    int i;
    
    for (i = 0; i < h->sizearray; i++) {
      TValue *o = &h->array[i];
      if (iscleared(o, 0))  // 值被清除？
        setnilvalue(o);  // 移除条目
    }
    
    for (i = 0; i < sizenode(h); i++) {
      Node *n = gnode(h, i);
      if (!ttisnil(gval(n)) &&  // 非空条目且
          (iscleared(key2tval(n), 1) || iscleared(gval(n), 0))) {
        setnilvalue(gval(n));  // 移除值
        removeentry(n);  // 移除条目
      }
    }
    l = h->gclist;
  }
}
```

## GC 控制和接口

### 1. 主 GC 函数

```c
void luaC_step (lua_State *L) {
  global_State *g = G(L);
  lu_mem lim = (GCSTEPSIZE/100) * g->gcstepmul;
  
  if (lim == 0)
    lim = (MAX_LUMEM-1)/2;  // 没有限制
  
  g->gcdept += g->totalbytes - g->GCthreshold;
  
  do {
    lim -= singlestep(L);
    if (g->gcstate == GCSpause) {
      g->GCthreshold = (g->estimate/100) * g->gcpause;
      return;
    }
  } while (lim > 0);
  
  if (g->gcstate != GCSpause) {
    if (g->gcdept < GCSTEPSIZE)
      g->GCthreshold = g->totalbytes + GCSTEPSIZE;  // 延迟下一步
    else {
      g->gcdept -= GCSTEPSIZE;
      g->GCthreshold = g->totalbytes;  // 现在运行
    }
  }
  else {
    lua_assert(g->totalbytes >= g->estimate);
    setthreshold(g);
  }
}
```

### 2. GC 状态机

```c
static lu_mem singlestep (lua_State *L) {
  global_State *g = G(L);
  
  switch (g->gcstate) {
    case GCSpause: {
      markroot(L);  // 开始新的回收周期
      return 0;
    }
    case GCSpropagate: {
      if (g->gray)
        return propagatemark(g);
      else {  // 没有更多灰色对象
        atomic(L);  // 完成这个周期
        return 0;
      }
    }
    case GCSsweepstring: {
      lu_mem old = g->totalbytes;
      sweepwholelist(L, &g->strt.hash[g->sweepstrgc++]);
      if (g->sweepstrgc >= g->strt.size)  // 没有更多字符串
        g->gcstate = GCSsweep;  // 结束这个阶段
      lua_assert(old >= g->totalbytes);
      g->estimate -= old - g->totalbytes;
      return GCSWEEPCOST;
    }
    case GCSsweep: {
      lu_mem old = g->totalbytes;
      g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX);
      if (*g->sweepgc == NULL) {  // 没有更多对象
        checkSizes(L);
        g->gcstate = GCSfinalize;  // 结束这个阶段
      }
      lua_assert(old >= g->totalbytes);
      g->estimate -= old - g->totalbytes;
      return GCSWEEPMAX*GCSWEEPCOST;
    }
    case GCSfinalize: {
      if (g->tmudata) {
        GCTM(L);
        if (g->estimate > GCFINALIZECOST)
          g->estimate -= GCFINALIZECOST;
        return GCFINALIZECOST;
      }
      else {
        g->gcstate = GCSpause;  // 结束收集
        g->gcdept = 0;
        return 0;
      }
    }
    default: lua_assert(0); return 0;
  }
}
```

## 写屏障

写屏障确保增量回收的正确性：

```c
void luaC_barrierf (lua_State *L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
  lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
  lua_assert(ttype(&o->gch) != LUA_TTABLE);
  
  // 必须保持不变量？
  if (g->gcstate == GCSpropagate)
    reallymarkobject(g, v);  // 恢复不变量
  else  // 不必保持不变量
    makewhite(g, o);  // 将'o'标记为白色
}

void luaC_barrierback (lua_State *L, Table *t) {
  global_State *g = G(L);
  GCObject *o = obj2gco(t);
  lua_assert(isblack(o) && !isdead(g, o));
  lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
  
  black2gray(o);  // 使表再次变为灰色
  t->gclist = g->grayagain;
  g->grayagain = o;
}
```

## 总结

Lua 的垃圾回收器通过以下设计实现了高效的内存管理：

1. **增量回收**：避免长时间停顿，与程序执行交错进行
2. **三色标记**：准确标记所有可达对象
3. **弱引用支持**：正确处理弱表和弱引用
4. **终结化机制**：有序调用对象终结器
5. **写屏障**：维护增量回收的正确性
6. **可调参数**：允许应用调整 GC 行为

这种设计使得 Lua 能够在保持高性能的同时，提供自动的内存管理，非常适合嵌入式环境和实时应用。

---

*相关文档：[对象系统](wiki_object.md) | [表实现](wiki_table.md) | [函数系统](wiki_function.md)*