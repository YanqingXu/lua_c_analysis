# 🎨 三色标记算法详解

> **技术主题**：Lua GC 的核心 - 三色标记增量回收算法

## 📋 概述

三色标记算法是 Lua 垃圾回收器的核心技术。它通过将对象标记为三种颜色（白、灰、黑），实现增量式的垃圾回收，避免长时间停顿。

## 🎨 三色抽象模型

### 颜色的含义

```
┌─────────────────────────────────────────┐
│  白色（White）                           │
│  - 未被标记的对象                        │
│  - 可能是垃圾对象                        │
│  - 在清扫阶段会被回收                    │
│  - 有两种白色（当前白色和另一种白色）    │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│  灰色（Gray）                            │
│  - 已被标记但未完成扫描的对象            │
│  - 在灰色队列中等待处理                  │
│  - 其引用的对象可能还是白色              │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│  黑色（Black）                           │
│  - 已完成扫描的活跃对象                  │
│  - 所有引用的对象都已标记                │
│  - 保证不会被回收                        │
└─────────────────────────────────────────┘
```

### 颜色状态转换

```
开始新GC周期
    │
    ↓
[White] ──扫描根对象──> [Gray] ──扫描子对象──> [Black]
    │                       │                     │
    │                       ↓                     │
    │                  处理灰色队列                │
    │                       │                     │
    │                  扫描完成后                 │
    │                       ↓                     │
    └───────────────────  清扫阶段  ←─────────────┘
                            │
                        回收白色对象
```

## 🔧 实现机制

### 1. 颜色的位表示

```c
// GC 颜色位定义（lgc.h）
#define WHITE0BIT       0  // 白色0位
#define WHITE1BIT       1  // 白色1位
#define BLACKBIT        2  // 黑色位
#define FINALIZEDBIT    3  // 已终结位

// 颜色掩码
#define WHITEBITS       bit2mask(WHITE0BIT, WHITE1BIT)
#define bitmask(b)      (1<<(b))
#define bit2mask(b1,b2) (bitmask(b1) | bitmask(b2))

// 颜色判断宏
#define iswhite(x)      test2bits((x)->gch.marked, WHITE0BIT, WHITE1BIT)
#define isblack(x)      testbit((x)->gch.marked, BLACKBIT)
#define isgray(x)       (!isblack(x) && !iswhite(x))

// 颜色设置宏
#define white2gray(x)   reset2bits((x)->gch.marked, WHITE0BIT, WHITE1BIT)
#define gray2black(x)   l_setbit((x)->gch.marked, BLACKBIT)
```

### 2. 双色白（Two-White）技术

Lua 使用两种白色来优化 GC：

```c
// 全局状态中的当前白色
global_State *g;
lu_byte currentwhite = g->currentwhite & WHITEBITS;
lu_byte otherwhite = otherwhite(g);

// 新对象总是使用当前白色
#define luaC_white(g)   cast(lu_byte, (g)->currentwhite & WHITEBITS)
```

**为什么需要两种白色？**

- 在标记阶段，新创建的对象标记为当前白色
- 旧的未标记对象使用另一种白色
- GC 结束后，交换当前白色和另一种白色
- 这样避免了重新标记所有对象为白色的开销

### 3. 灰色队列管理

Lua 使用多个灰色队列来组织待扫描的对象：

```c
typedef struct global_State {
    GCObject *gray;       // 灰色对象列表
    GCObject *grayagain;  // 需要重新扫描的灰色对象
    GCObject *weak;       // 弱引用表列表
    // ...
} global_State;
```

**三个队列的用途**：

1. **gray**：普通的灰色对象队列
2. **grayagain**：需要在原子阶段重新扫描的对象（如打开的 Upvalue）
3. **weak**：弱引用表，在清扫阶段特殊处理

## 🔄 标记过程详解

### 阶段 1：标记根对象

```c
static void markroot (lua_State *L) {
    global_State *g = G(L);
    
    // 1. 标记主线程
    gray2black(obj2gco(g->mainthread));
    markobject(g, g->mainthread);
    
    // 2. 标记全局表
    markvalue(g, &g->l_registry);
    
    // 3. 标记元表
    for (i = 0; i < NUM_TAGS; i++)
        markobject(g, g->mt[i]);
    
    // 4. 标记其他根对象
    markobject(g, g->mainthread->env);
}
```

### 阶段 2：传播标记

```c
static void propagatemark (global_State *g) {
    GCObject *o = g->gray;  // 取出一个灰色对象
    
    // 从灰色队列中移除
    g->gray = o->gch.next;
    
    switch (o->gch.tt) {
        case LUA_TTABLE: {
            Table *h = gco2h(o);
            // 标记元表
            if (h->metatable)
                markobject(g, h->metatable);
            // 标记数组部分
            for (i = 0; i < h->sizearray; i++)
                markvalue(g, &h->array[i]);
            // 标记哈希部分
            for (i = 0; i < sizenode(h); i++) {
                Node *n = &h->node[i];
                markvalue(g, &n->i_key);
                markvalue(g, &n->i_val);
            }
            break;
        }
        case LUA_TFUNCTION: {
            Closure *cl = gco2cl(o);
            // 标记 Upvalue
            for (i = 0; i < cl->l.nupvalues; i++)
                markobject(g, cl->l.upvals[i]);
            // 标记函数原型
            markobject(g, cl->l.p);
            break;
        }
        // ... 其他类型
    }
    
    // 标记完成，变为黑色
    gray2black(o);
}
```

### 阶段 3：原子阶段

```c
static void atomic (lua_State *L) {
    global_State *g = G(L);
    
    // 1. 处理 grayagain 队列
    while (g->grayagain) {
        propagatemark(g);
    }
    
    // 2. 清理字符串表
    cleartable(g->strt.hash);
    
    // 3. 处理弱引用表
    while (g->weak) {
        Table *h = gco2h(g->weak);
        g->weak = h->gclist;
        clearweaktables(h);
    }
    
    // 4. 交换白色
    g->currentwhite = cast_byte(otherwhite(g));
    
    // 5. 准备清扫阶段
    g->sweepgc = &g->rootgc;
    g->gcstate = GCSsweepstring;
}
```

### 阶段 4：清扫阶段

```c
static GCObject **sweeplist (lua_State *L, GCObject **p, lu_mem count) {
    global_State *g = G(L);
    lu_byte currentwhite = luaC_white(g);
    
    while (*p != NULL && count-- > 0) {
        GCObject *curr = *p;
        
        if (curr->gch.marked & currentwhite) {
            // 当前白色对象（新对象），保留
            curr->gch.marked &= ~WHITEBITS;
            curr->gch.marked |= otherwhite(g);
            p = &curr->gch.next;
        }
        else {
            // 另一种白色对象（旧对象），回收
            *p = curr->gch.next;
            
            // 调用终结器（如果有）
            if (curr->gch.marked & FINALIZEDBIT)
                callfinalize(L, curr);
            
            // 释放内存
            freeobj(L, curr);
        }
    }
    
    return p;
}
```

## 💡 增量回收的关键技术

### 1. 三色不变性

为了保证增量回收的正确性，必须维护以下不变性：

**强三色不变性**：
- 黑色对象不能直接引用白色对象

**弱三色不变性**：
- 所有从黑色对象到白色对象的路径都要经过至少一个灰色对象

### 2. 写屏障（Write Barrier）

当程序修改对象引用时，写屏障确保三色不变性：

```c
// 屏障宏（lgc.h）
#define luaC_barrier(L,p,v) { \
    if (iscollectable(v) && isblack(obj2gco(p)) && iswhite(gcvalue(v))) \
        luaC_barrierf(L,obj2gco(p),gcvalue(v)); }

// 屏障实现（lgc.c）
void luaC_barrierf (lua_State *L, GCObject *o, GCObject *v) {
    global_State *g = G(L);
    
    // 策略1：将黑色对象变回灰色（向后屏障）
    black2gray(o);
    o->gch.gclist = g->grayagain;
    g->grayagain = o;
    
    // 策略2：将白色对象变为灰色（向前屏障）
    // reallymarkobject(g, v);
}
```

**两种屏障策略**：

1. **向后屏障（Backward Barrier）**：
   - 将黑色对象变回灰色
   - 重新扫描该对象
   - 适用于频繁修改的对象（如表）

2. **向前屏障（Forward Barrier）**：
   - 将白色对象变为灰色
   - 立即标记新引用的对象
   - 适用于不常修改的对象（如 Upvalue）

## 🎓 算法优势

1. **增量执行**：GC 工作分散执行，避免长时间停顿
2. **低延迟**：每次只处理少量对象，响应时间可预测
3. **内存效率**：及时回收垃圾，减少内存占用
4. **简单高效**：算法简单，实现高效

## 🔗 相关文档

- [增量回收机制](incremental_gc.md) - GC 的增量执行流程
- [写屏障实现](write_barrier.md) - 写屏障的详细实现
- [GC 性能调优](gc_tuning.md) - 如何调优 GC 参数

---

*返回：[垃圾回收模块总览](wiki_gc.md)*
