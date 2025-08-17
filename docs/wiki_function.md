# Lua 函数系统详解

## 概述

Lua 的函数系统包含了函数定义、闭包实现、upvalue 管理等核心组件。Lua 支持两种类型的函数：Lua 函数（由 Lua 代码定义）和 C 函数（由 C 代码定义）。本文档详细介绍了这些组件的内部实现。

## 核心数据结构

### Proto (函数原型)

```c
typedef struct Proto {
  CommonHeader;           // GC 对象通用头部
  TValue *k;              // 常量数组
  Instruction *code;      // 字节码指令数组  
  struct Proto **p;       // 内嵌函数原型数组
  int *lineinfo;          // 行号信息（调试用）
  struct LocVar *locvars; // 局部变量信息
  TString **upvalues;     // upvalue 名称数组
  TString *source;        // 源代码文件名
  int sizeupvalues;       // upvalue 数量
  int sizek;              // 常量数组大小
  int sizecode;           // 字节码数组大小
  int sizelineinfo;       // 行号信息数组大小  
  int sizep;              // 内嵌函数数量
  int sizelocvars;        // 局部变量数量
  int linedefined;        // 函数定义起始行号
  int lastlinedefined;    // 函数定义结束行号
  GCObject *gclist;       // GC 链表指针
  lu_byte nups;           // upvalue 数量
  lu_byte numparams;      // 参数数量
  lu_byte is_vararg;      // 可变参数标志
  lu_byte maxstacksize;   // 最大栈大小
} Proto;
```

### Closure (闭包)

Lua 中有两种类型的闭包：

#### C 闭包 (CClosure)
```c
typedef struct CClosure {
  ClosureHeader;          // 通用闭包头部
  lua_CFunction f;        // C 函数指针
  TValue upvalue[1];      // upvalue 数组
} CClosure;
```

#### Lua 闭包 (LClosure)  
```c
typedef struct LClosure {
  ClosureHeader;          // 通用闭包头部
  struct Proto *p;        // 函数原型指针
  UpVal *upvals[1];       // upvalue 指针数组
} LClosure;
```

#### 通用闭包头部
```c
#define ClosureHeader \
    CommonHeader;           \
    lu_byte isC;           /* 是否为 C 函数 */ \
    lu_byte nupvalues;     /* upvalue 数量 */ \
    GCObject *gclist;      /* GC 链表 */ \
    struct Table *env      /* 环境表 */
```

### UpVal (上值)

```c
typedef struct UpVal {
  CommonHeader;
  TValue *v;              // 指向值的指针
  union {
    TValue value;         // 关闭时的值存储
    struct {              // 开放时的双向链表
      struct UpVal *prev;
      struct UpVal *next;
    } l;
  } u;
} UpVal;
```

## 函数创建和管理

### 1. 创建函数原型

```c
Proto *luaF_newproto (lua_State *L) {
  Proto *f = luaM_new(L, Proto);
  luaC_link(L, obj2gco(f), LUA_TPROTO);
  
  // 初始化所有字段为默认值
  f->k = NULL;
  f->sizek = 0;
  f->p = NULL;
  f->sizep = 0;
  f->code = NULL;
  f->sizecode = 0;
  f->nups = 0;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  // ... 其他字段初始化
  
  return f;
}
```

### 2. 创建 C 闭包

```c
Closure *luaF_newCclosure (lua_State *L, int nelems, Table *e) {
  Closure *c = cast(Closure *, luaM_malloc(L, sizeCclosure(nelems)));
  luaC_link(L, obj2gco(c), LUA_TFUNCTION);
  
  c->c.isC = 1;                    // 标记为 C 函数
  c->c.env = e;                    // 设置环境表
  c->c.nupvalues = cast_byte(nelems); // 设置 upvalue 数量
  
  return c;
}
```

### 3. 创建 Lua 闭包

```c
Closure *luaF_newLclosure (lua_State *L, int nelems, Table *e) {
  Closure *c = cast(Closure *, luaM_malloc(L, sizeLclosure(nelems)));
  luaC_link(L, obj2gco(c), LUA_TFUNCTION);
  
  c->l.isC = 0;                    // 标记为 Lua 函数
  c->l.env = e;                    // 设置环境表
  c->l.nupvalues = cast_byte(nelems); // 设置 upvalue 数量
  
  // 初始化所有 upvalue 指针为 NULL
  while (nelems--) 
    c->l.upvals[nelems] = NULL;
    
  return c;
}
```

## UpValue 管理

### UpValue 的生命周期

UpValue 有两种状态：
1. **开放状态**：指向栈上的变量
2. **关闭状态**：拥有自己的值副本

### 1. 查找或创建 UpValue

```c
UpVal *luaF_findupval (lua_State *L, StkId level) {
  global_State *g = G(L);
  GCObject **pp = &L->openupval;
  UpVal *p;
  UpVal *uv;
  
  // 在开放 upvalue 链表中查找
  while ((p = ngcotouv(*pp)) != NULL && p->v >= level) {
    if (p->v == level) {           // 找到对应的 upvalue
      if (isdead(g, obj2gco(p)))   // 如果已死亡
        changewhite(obj2gco(p));   // 复活它
      return p;
    }
    pp = &p->next;
  }
  
  // 未找到，创建新的 upvalue
  uv = luaM_new(L, UpVal);
  uv->tt = LUA_TUPVAL;
  uv->marked = luaC_white(g);
  uv->v = level;                   // 指向栈上的值
  uv->next = *pp;                  // 插入链表
  *pp = obj2gco(uv);
  
  // 双向链表操作
  uv->u.l.prev = &g->uvhead;
  uv->u.l.next = g->uvhead.u.l.next;
  uv->u.l.next->u.l.prev = uv;
  g->uvhead.u.l.next = uv;
  
  return uv;
}
```

### 2. 关闭 UpValue

当栈上的变量超出作用域时，需要关闭对应的 upvalue：

```c
void luaF_close (lua_State *L, StkId level) {
  UpVal *uv;
  global_State *g = G(L);
  
  // 关闭所有指向 level 及以上位置的 upvalue
  while ((uv = ngcotouv(L->openupval)) != NULL && uv->v >= level) {
    GCObject *o = obj2gco(uv);
    L->openupval = uv->next;       // 从开放列表中移除
    
    if (isdead(g, o))
      luaF_freeupval(L, uv);       // 释放死亡的 upvalue
    else {
      unlinkupval(uv);             // 从双向链表中移除
      setobj(L, &uv->u.value, uv->v); // 复制值
      uv->v = &uv->u.value;        // 指向自己的值
      luaC_linkupval(L, uv);       // 链接到 GC 根列表
    }
  }
}
```

## 函数调用机制

### 调用信息结构

```c
typedef struct CallInfo {
  StkId base;               // 函数的栈基址
  StkId func;               // 函数在栈中的位置
  StkId top;                // 栈顶位置
  const Instruction *savedpc; // 保存的程序计数器
  int nresults;             // 期望的返回值数量
  int tailcalls;            // 尾调用次数
} CallInfo;
```

### 函数调用过程

1. **准备阶段**：
   - 函数和参数压入栈
   - 分配新的 CallInfo
   - 设置栈基址和栈顶

2. **执行阶段**：
   - Lua 函数：执行字节码
   - C 函数：直接调用 C 函数指针

3. **返回阶段**：
   - 处理返回值
   - 恢复调用者的栈状态
   - 释放 CallInfo

### 尾调用优化

Lua 支持尾调用优化，避免栈溢出：

```lua
function factorial_tail(n, acc)
  if n == 0 then
    return acc
  else
    return factorial_tail(n-1, n*acc)  -- 尾调用
  end
end
```

尾调用不会增加调用栈的深度。

## 变参函数支持

### 变参标志

```c
// Proto 结构中的变参标志
#define VARARG_HASARG    1  // 函数有 ... 参数
#define VARARG_ISVARARG  2  // 函数是变参函数  
#define VARARG_NEEDSARG  4  // 需要创建 arg 表
```

### 变参处理

```c
// 在函数调用时处理变参
if (f->is_vararg & VARARG_HASARG) {
  // 创建变参表或设置变参访问
}
```

## 局部变量和调试信息

### 局部变量信息

```c
typedef struct LocVar {
  TString *varname;       // 变量名
  int startpc;            // 变量生效的起始指令
  int endpc;              // 变量失效的结束指令
} LocVar;
```

### 获取局部变量名

```c
const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i < f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  // 变量是否活跃
      local_number--;
      if (local_number == 0)
        return getstr(f->locvars[i].varname);
    }
  }
  return NULL;  // 未找到
}
```

## 内存管理

### 1. 释放函数原型

```c
void luaF_freeproto (lua_State *L, Proto *f) {
  luaM_freearray(L, f->code, f->sizecode, Instruction);
  luaM_freearray(L, f->p, f->sizep, Proto *);
  luaM_freearray(L, f->k, f->sizek, TValue);
  luaM_freearray(L, f->lineinfo, f->sizelineinfo, int);
  luaM_freearray(L, f->locvars, f->sizelocvars, struct LocVar);
  luaM_freearray(L, f->upvalues, f->sizeupvalues, TString *);
  luaM_free(L, f);
}
```

### 2. 释放闭包

```c
void luaF_freeclosure (lua_State *L, Closure *c) {
  int size = (c->c.isC) ? sizeCclosure(c->c.nupvalues) :
                          sizeLclosure(c->l.nupvalues);
  luaM_freemem(L, c, size);
}
```

## 使用示例

### 1. 创建简单的 C 函数

```c
static int my_c_function(lua_State *L) {
  int n = lua_gettop(L);  // 获取参数数量
  // 处理参数...
  lua_pushnumber(L, result); // 返回结果
  return 1;  // 返回值数量
}

// 注册函数
lua_pushcfunction(L, my_c_function);
lua_setglobal(L, "my_function");
```

### 2. Lua 闭包示例

```lua
function make_counter()
  local count = 0
  return function()
    count = count + 1
    return count
  end
end

local counter = make_counter()
print(counter())  -- 1
print(counter())  -- 2
```

在这个例子中：
- `count` 变量被内层函数的 upvalue 捕获
- 每次调用 `counter` 时，upvalue 保持状态

## 性能考虑

### 1. UpValue 访问优化

- 开放状态的 upvalue 直接访问栈
- 关闭状态的 upvalue 访问自己的值副本
- 避免不必要的 upvalue 创建

### 2. 函数调用优化

- 尾调用优化减少栈使用
- C 函数调用开销较小
- 内联缓存优化方法调用

### 3. 内存管理

- 及时关闭 upvalue 减少内存占用
- 共享函数原型减少重复
- GC 自动管理函数对象生命周期

## 总结

Lua 的函数系统通过以下设计实现了高效和灵活性：

1. **统一的闭包模型**：C 函数和 Lua 函数使用相同的闭包结构
2. **高效的 upvalue 管理**：开放/关闭状态优化内存使用
3. **尾调用优化**：避免深度递归的栈溢出
4. **丰富的调试信息**：支持局部变量名和行号信息
5. **自动内存管理**：GC 负责所有函数对象的生命周期

这种设计使得 Lua 在保持语言简洁性的同时，提供了强大的函数式编程能力。

---

*相关文档：[调用栈管理](wiki_call.md) | [虚拟机执行](wiki_vm.md) | [对象系统](wiki_object.md)*