# 📦 TValue 统一值表示详解

> **技术主题**：Lua 动态类型系统的核心 - Tagged Union 实现

## 📋 概述

TValue（Tagged Value）是 Lua 实现动态类型系统的关键。它通过联合体（Union）和类型标记（Type Tag）实现了统一的值表示，使得所有 Lua 值都使用相同大小的结构。

## 🔧 核心数据结构

### Value 联合体

```c
// 值联合体（lobject.h）
typedef union {
    GCObject *gc;      // 可垃圾回收对象的指针
    void *p;           // 轻量用户数据指针
    lua_Number n;      // 数字值（通常是 double）
    int b;             // 布尔值
} Value;
```

**设计要点**：
- 联合体确保所有成员共享同一块内存
- 大小等于最大成员的大小（通常是 8 字节）
- 通过类型标记区分实际存储的是哪种类型

### TValue 结构

```c
// Tagged Value 结构（lobject.h）
typedef struct lua_TValue {
    Value value;       // 值联合体
    int tt;            // 类型标记（Type Tag）
} TValue;
```

**内存布局**：
```
┌──────────────────────────────────┐
│  value (8 bytes)                  │
│  - gc pointer / p / n / b         │
├──────────────────────────────────┤
│  tt (4 bytes)                     │
│  - 类型标记                       │
└──────────────────────────────────┘
总大小：12 字节（32位）或 16 字节（64位，考虑对齐）
```

## 🏷️ 类型标记系统

### 类型常量定义

```c
// 基本类型（lua.h）
#define LUA_TNIL           0
#define LUA_TBOOLEAN       1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMBER        3
#define LUA_TSTRING        4
#define LUA_TTABLE         5
#define LUA_TFUNCTION      6
#define LUA_TUSERDATA      7
#define LUA_TTHREAD        8

// 内部类型（lobject.h）
#define LUA_TPROTO      (LAST_TAG+1)  // 函数原型
#define LUA_TUPVAL      (LAST_TAG+2)  // Upvalue
#define LUA_TDEADKEY    (LAST_TAG+3)  // 死键（表中已删除的键）
```

### 类型分类

**立即值类型**（不需要 GC）：
- nil
- boolean
- number
- lightuserdata

**GC 管理类型**（需要垃圾回收）：
- string
- table
- function
- userdata
- thread

## 🔍 类型检查宏

### 快速类型检查

```c
// 获取类型标记
#define ttype(o)        ((o)->tt)

// 类型判断宏（lobject.h）
#define ttisnil(o)      (ttype(o) == LUA_TNIL)
#define ttisboolean(o)  (ttype(o) == LUA_TBOOLEAN)
#define ttisnumber(o)   (ttype(o) == LUA_TNUMBER)
#define ttisstring(o)   (ttype(o) == LUA_TSTRING)
#define ttistable(o)    (ttype(o) == LUA_TTABLE)
#define ttisfunction(o) (ttype(o) == LUA_TFUNCTION)
#define ttisuserdata(o) (ttype(o) == LUA_TUSERDATA)
#define ttisthread(o)   (ttype(o) == LUA_TTHREAD)

// 复合判断
#define ttislightuserdata(o) (ttype(o) == LUA_TLIGHTUSERDATA)
#define iscollectable(o) (ttype(o) >= LUA_TSTRING)
```

### 值提取宏

```c
// 获取各类型的值（lobject.h）

// GC 对象指针
#define gcvalue(o)      check_exp(iscollectable(o), (o)->value.gc)

// 布尔值
#define bvalue(o)       check_exp(ttisboolean(o), (o)->value.b)

// 数字
#define nvalue(o)       check_exp(ttisnumber(o), (o)->value.n)

// 轻量用户数据
#define pvalue(o)       check_exp(ttislightuserdata(o), (o)->value.p)

// 字符串
#define rawtsvalue(o)   check_exp(ttisstring(o), &(o)->value.gc->ts)
#define tsvalue(o)      (&rawtsvalue(o)->tsv)
#define svalue(o)       getstr(tsvalue(o))

// 表
#define hvalue(o)       check_exp(ttistable(o), &(o)->value.gc->h)

// 函数
#define clvalue(o)      check_exp(ttisfunction(o), &(o)->value.gc->cl)

// 用户数据
#define rawuvalue(o)    check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define uvalue(o)       (&rawuvalue(o)->uv)

// 线程
#define thvalue(o)      check_exp(ttisthread(o), &(o)->value.gc->th)
```

## 📝 值设置宏

### 设置各类型的值

```c
// nil
#define setnilvalue(obj) ((obj)->tt=LUA_TNIL)

// 数字
#define setnvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER; }

// 布尔
#define setbvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN; }

// 轻量用户数据
#define setpvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA; }

// 字符串（需要 GC 检查）
#define setsvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TSTRING; \
    checkliveness(G(L),i_o); }

// 表（需要 GC 检查）
#define sethvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTABLE; \
    checkliveness(G(L),i_o); }

// 函数（需要 GC 检查）
#define setclvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TFUNCTION; \
    checkliveness(G(L),i_o); }
```

### GC 活性检查

```c
// 检查 GC 对象是否活跃（lobject.h）
#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
             (ttype(obj) == (obj)->value.gc->gch.tt))
```

## 🔄 值的复制

### 对象间复制

```c
// 通用复制宏（lobject.h）
#define setobj(L,obj1,obj2) \
  { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; o1->tt=o2->tt; \
    checkliveness(G(L),o1); }

// 栈到栈复制（不需要 GC 检查）
#define setobjs2s    setobj

// 栈到其他位置复制（需要屏障）
#define setobj2s    setobj

// 其他位置到栈复制
#define setsvalue2s    setsvalue
#define sethvalue2s    sethvalue
```

### 复制优化

```c
// 栈到栈的快速复制（不触发 GC 屏障）
#define setobjs2s(L,o1,o2)  setobj(L,o1,o2)

// 表到栈的复制（可能需要屏障）
#define setobj2t    setobj

// 栈到表的复制（需要屏障）
#define setobj2s    setobj
```

## 💡 设计优势

### 1. 统一的值大小

**优点**：
- 栈管理简化：所有值占用相同空间
- 数组实现简单：TValue 数组直接索引
- 内存预测：容易计算内存需求

**示例**：
```c
// 栈是 TValue 数组
typedef TValue *StkId;

// 简单的栈操作
void push(lua_State *L, TValue *v) {
    setobj(L, L->top, v);
    L->top++;
}

TValue *pop(lua_State *L) {
    return --L->top;
}
```

### 2. 快速类型检查

**实现**：
```c
// O(1) 的整数比较
if (ttype(o) == LUA_TNUMBER) {
    // 处理数字
}
```

**优于其他方案**：
- 不需要虚函数调用
- 不需要类型转换
- CPU 分支预测友好

### 3. 灵活的类型扩展

**添加新类型很简单**：
```c
// 1. 定义新类型常量
#define LUA_TNEWTYPE  9

// 2. 添加类型检查宏
#define ttisnewtype(o)  (ttype(o) == LUA_TNEWTYPE)

// 3. 添加值提取宏
#define newtypevalue(o) check_exp(ttisnewtype(o), (o)->value.gc->nt)

// 4. 在 GCObject 联合体中添加新成员
union GCObject {
    // ...
    struct NewType nt;
};
```

### 4. 内存效率

**对比其他方案**：

方案1：每种类型独立结构（低效）
```c
typedef struct {
    int type;
    union {
        Number num;
        String *str;
        Table *tab;
        // ...
    } data;
} Value; // 浪费空间，每个值都需要类型字段
```

方案2：Tagged Pointer（复杂）
```c
// 使用指针的低位存储类型
// 限制：只适用于指针对齐的架构
// 实现复杂，不够通用
```

**TValue 方案的优势**：
- 简单直观
- 通用性好（不依赖指针对齐）
- 便于调试
- 性能优秀

## 🎓 应用示例

### 示例 1：创建 Lua 值

```c
// 创建数字值
TValue v;
setnvalue(&v, 3.14);

// 创建字符串值
TString *s = luaS_newlstr(L, "hello", 5);
setsvalue(L, &v, s);

// 创建表值
Table *t = luaH_new(L, 0, 0);
sethvalue(L, &v, t);
```

### 示例 2：类型检查和转换

```c
void print_value(lua_State *L, TValue *v) {
    if (ttisnil(v)) {
        printf("nil\n");
    }
    else if (ttisboolean(v)) {
        printf("%s\n", bvalue(v) ? "true" : "false");
    }
    else if (ttisnumber(v)) {
        printf("%g\n", nvalue(v));
    }
    else if (ttisstring(v)) {
        printf("%s\n", svalue(v));
    }
    // ...
}
```

### 示例 3：值的操作

```c
// 加法操作
void lua_arith_add(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    if (ttisnumber(rb) && ttisnumber(rc)) {
        setnvalue(ra, nvalue(rb) + nvalue(rc));
    }
    else {
        // 元方法调用
        Arith(L, ra, rb, rc, TM_ADD);
    }
}
```

## 🔗 相关文档

- [Table 数据结构](table_structure.md) - Table 如何使用 TValue
- [类型转换](type_conversion.md) - TValue 的类型转换规则
- [GC 对象管理](../gc/tri_color_marking.md) - GC 如何管理 TValue 中的对象

---

*返回：[对象系统模块总览](wiki_object.md)*
