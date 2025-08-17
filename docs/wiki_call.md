# Lua 调用栈管理详解

## 概述

Lua 的调用栈管理是虚拟机的核心组件之一，负责函数调用、参数传递、返回值处理、异常处理和协程切换。本文档详细介绍了 Lua 5.1 中调用栈的实现机制。

## 核心数据结构

### lua_State (线程状态)

```c
struct lua_State {
  CommonHeader;
  lu_byte status;              // 线程状态
  StkId top;                   // 栈顶指针
  StkId base;                  // 当前函数的栈基址
  global_State *l_G;           // 全局状态指针
  CallInfo *ci;                // 当前调用信息
  const Instruction *savedpc;  // 保存的程序计数器
  StkId stack_last;            // 栈的最后可用位置
  StkId stack;                 // 栈底指针
  CallInfo *end_ci;            // CallInfo 数组结束位置
  CallInfo *base_ci;           // CallInfo 数组基址
  int stacksize;               // 栈大小
  int size_ci;                 // CallInfo 数组大小
  unsigned short nCcalls;      // C 调用嵌套深度
  // ... 其他字段
};
```

### CallInfo (调用信息)

```c
typedef struct CallInfo {
  StkId base;                  // 函数的栈基址
  StkId func;                  // 函数在栈中的位置
  StkId top;                   // 栈顶位置
  const Instruction *savedpc;  // 保存的程序计数器
  int nresults;                // 期望的返回值数量
  int tailcalls;               // 尾调用计数
} CallInfo;
```

## 栈布局

Lua 的栈是一个连续的 TValue 数组，栈的布局如下：

```
栈顶 (L->top)
    ↓
┌─────────────┐
│   临时值    │
├─────────────┤ ← L->ci->top
│  局部变量   │
├─────────────┤ ← L->base = L->ci->base  
│    参数     │
├─────────────┤ ← L->ci->func
│   函数对象  │
├─────────────┤
│     ...     │
└─────────────┘ ← L->stack (栈底)
```

## 栈管理

### 1. 栈扩展

```c
void luaD_growstack (lua_State *L, int n) {
  if (n <= L->stacksize)  // 已经足够大
    luaD_reallocstack(L, 2*L->stacksize);
  else
    luaD_reallocstack(L, L->stacksize + n + EXTRA_STACK);
}
```

### 2. 栈重分配

```c
void luaD_reallocstack (lua_State *L, int newsize) {
  TValue *oldstack = L->stack;
  int realsize = newsize + 1 + EXTRA_STACK;
  
  lua_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK - 1);
  
  // 重新分配栈内存
  luaM_reallocvector(L, L->stack, L->stacksize, realsize, TValue);
  L->stacksize = realsize;
  L->stack_last = L->stack + newsize;
  
  // 调整所有指针
  correctstack(L, oldstack);
}
```

### 3. 栈检查宏

```c
#define luaD_checkstack(L,n) \
  if ((char *)L->stack_last - (char *)L->top <= (n)*(int)sizeof(TValue)) \
    luaD_growstack(L, n);
```

## 函数调用机制

### 1. 预调用处理 (luaD_precall)

```c
int luaD_precall (lua_State *L, StkId func, int nresults) {
  LClosure *cl;
  ptrdiff_t funcr;
  
  if (!ttisfunction(func)) {  // 不是函数
    func = tryfuncTM(L, func);  // 尝试调用元方法
  }
  
  funcr = savestack(L, func);
  cl = &clvalue(func)->l;
  L->ci->savedpc = L->savedpc;
  
  if (!cl->isC) {  // Lua 函数
    CallInfo *ci;
    StkId st, base;
    Proto *p = cl->p;
    
    // 检查参数数量
    if (p->is_vararg & VARARG_NEEDSARG)
      luaD_checkstack(L, p->maxstacksize + p->numparams);
    else
      luaD_checkstack(L, p->maxstacksize);
    
    func = restorestack(L, funcr);
    
    // 分配新的 CallInfo
    if (L->ci + 1 == L->end_ci) 
      luaD_reallocCI(L, L->size_ci);
    
    ci = ++L->ci;
    L->base = L->ci->base = func + 1;
    ci->func = func;
    ci->top = L->base + p->maxstacksize;
    L->savedpc = p->code;  // 指向函数的字节码
    ci->tailcalls = 0;
    ci->nresults = nresults;
    
    // 初始化局部变量为 nil
    for (st = L->top; st < ci->top; st++)
      setnilvalue(st);
    L->top = ci->top;
    
    return PCRLUA;
  }
  else {  // C 函数
    CallInfo *ci;
    int n;
    
    // 检查 C 调用深度
    if (L->nCcalls >= LUAI_MAXCCALLS) {
      if (L->nCcalls == LUAI_MAXCCALLS)
        luaG_runerror(L, "C stack overflow");
      else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS>>3)))
        luaD_throw(L, LUA_ERRERR);  // 错误处理中的错误
    }
    
    // 分配新的 CallInfo
    if (L->ci + 1 == L->end_ci) 
      luaD_reallocCI(L, L->size_ci);
    
    ci = ++L->ci;
    ci->func = restorestack(L, funcr);
    L->base = L->ci->base = ci->func + 1;
    ci->top = L->top + LUA_MINSTACK;
    ci->nresults = nresults;
    
    if (L->hookmask & LUA_MASKCALL)
      luaD_callhook(L, LUA_HOOKCALL, -1);
    
    lua_unlock(L);
    L->nCcalls++;
    n = (*curr_func(L)->c.f)(L);  // 调用 C 函数
    L->nCcalls--;
    lua_lock(L);
    
    return PCRC;
  }
}
```

### 2. 后调用处理 (luaD_poscall)

```c
int luaD_poscall (lua_State *L, StkId firstResult) {
  StkId res;
  int wanted, i;
  CallInfo *ci;
  
  if (L->hookmask & LUA_MASKRET)
    firstResult = callrethooks(L, firstResult);
  
  ci = L->ci--;
  res = ci->func;  // 返回值的目标位置
  wanted = ci->nresults;
  L->base = (ci - 1)->base;  // 恢复前一个函数的栈基址
  L->savedpc = (ci - 1)->savedpc;  // 恢复程序计数器
  
  // 移动返回值到正确位置
  for (i = wanted; i != 0 && firstResult < L->top; i--)
    setobjs2s(L, res++, firstResult++);
  
  while (i-- > 0)
    setnilvalue(res++);  // 不足的返回值用 nil 填充
  
  L->top = res;
  return (wanted - LUA_MULTRET);  // 如果是 LUA_MULTRET，返回实际返回值数量
}
```

### 3. 完整调用 (luaD_call)

```c
void luaD_call (lua_State *L, StkId func, int nResults) {
  if (++L->nCcalls >= LUAI_MAXCCALLS) {
    if (L->nCcalls == LUAI_MAXCCALLS)
      luaG_runerror(L, "C stack overflow");
    else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS>>3)))
      luaD_throw(L, LUA_ERRERR);
  }
  
  if (luaD_precall(L, func, nResults) == PCRLUA)  // Lua 函数？
    luaV_execute(L, 1);  // 执行 Lua 函数
  
  L->nCcalls--;
}
```

## 异常处理

### 1. 长跳转结构

```c
struct lua_longjmp {
  struct lua_longjmp *previous;  // 前一个跳转点
  luai_jmpbuf b;                 // 跳转缓冲区
  volatile int status;           // 错误码
};
```

### 2. 抛出异常

```c
void luaD_throw (lua_State *L, int errcode) {
  if (L->errorJmp) {
    L->errorJmp->status = errcode;
    LUAI_THROW(L, L->errorJmp);  // 长跳转
  }
  else {
    L->status = cast_byte(errcode);
    if (G(L)->panic) {
      lua_unlock(L);
      G(L)->panic(L);  // 调用 panic 函数
    }
    exit(EXIT_FAILURE);
  }
}
```

### 3. 保护调用 (luaD_pcall)

```c
int luaD_pcall (lua_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  int status;
  unsigned short oldnCcalls = L->nCcalls;
  ptrdiff_t old_ci = saveci(L, L->ci);
  lu_byte old_allowhooks = L->allowhook;
  ptrdiff_t old_errfunc = L->errfunc;
  
  L->errfunc = ef;
  status = luaD_rawrunprotected(L, func, u);
  
  if (status != 0) {  // 发生错误
    StkId oldtop = restorestack(L, old_top);
    luaF_close(L, oldtop);  // 关闭 upvalue
    luaD_seterrorobj(L, status, oldtop);
    L->nCcalls = oldnCcalls;
    L->ci = restoreci(L, old_ci);
    L->base = L->ci->base;
    L->savedpc = L->ci->savedpc;
    L->allowhook = old_allowhooks;
    restore_stack_limit(L);
  }
  L->errfunc = old_errfunc;
  return status;
}
```

## 尾调用优化

### 1. 尾调用检测

尾调用在字节码级别进行优化：

```c
case OP_TAILCALL: {
  int b = GETARG_B(i);
  if (b != 0) L->top = ra+b;  // else previous instruction set top
  lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);
  if (luaD_precall(L, ra, LUA_MULTRET) == PCRLUA) {
    // 尾调用：不增加调用栈深度
    CallInfo *ci = L->ci - 1;  // 当前调用信息
    CallInfo *lim = L->base_ci;
    StkId tb = ci->top;
    
    // 移动参数
    while (ci > lim && ci->tailcalls < LUAI_MAXTAILCALLS) {
      ci->tailcalls++;
      // ... 尾调用优化逻辑
    }
  }
  continue;
}
```

### 2. 尾调用计数

每个 CallInfo 记录尾调用次数：

```c
typedef struct CallInfo {
  // ...
  int tailcalls;  // 此调用下的尾调用数量
} CallInfo;
```

## 协程支持

### 1. 线程状态

```c
// 线程状态值
#define LUA_YIELD    1  // 协程挂起
#define LUA_ERRRUN   2  // 运行时错误
#define LUA_ERRSYNTAX 3 // 语法错误
#define LUA_ERRMEM   4  // 内存错误
#define LUA_ERRERR   5  // 错误处理中的错误
```

### 2. 协程挂起 (lua_yield)

```c
LUA_API int lua_yield (lua_State *L, int nresults) {
  luai_userstateyield(L, nresults);
  lua_lock(L);
  if (L->nCcalls > 0)
    luaG_runerror(L, "attempt to yield across metamethod/C-call boundary");
  L->base = L->top - nresults;  // 保护结果
  L->status = LUA_YIELD;
  lua_unlock(L);
  return -1;
}
```

### 3. 协程恢复 (lua_resume)

```c
LUA_API int lua_resume (lua_State *L, int narg) {
  int status;
  lua_lock(L);
  if (L->status != LUA_YIELD && (L->status != 0 || L->ci != L->base_ci))
    return resume_error(L, "cannot resume non-suspended coroutine");
  
  if (L->nCcalls >= LUAI_MAXCCALLS)
    return resume_error(L, "C stack overflow");
  
  luai_userstateresume(L, narg);
  lua_assert(L->errfunc == 0);
  L->baseCcalls = ++L->nCcalls;
  status = luaD_rawrunprotected(L, resume, L->top - narg);
  
  if (status != 0) {  // 错误？
    L->status = cast_byte(status);  // 标记为死亡状态
    luaD_seterrorobj(L, status, L->top);
    L->ci->top = L->top;
  }
  else {
    lua_assert(L->nCcalls == L->baseCcalls);
    status = L->status;
  }
  
  --L->nCcalls;
  lua_unlock(L);
  return status;
}
```

## 调试钩子

### 1. 钩子类型

```c
#define LUA_HOOKCALL     0  // 函数调用
#define LUA_HOOKRET      1  // 函数返回
#define LUA_HOOKLINE     2  // 行号改变
#define LUA_HOOKCOUNT    3  // 指令计数
#define LUA_HOOKTAILRET  4  // 尾调用返回
```

### 2. 调用钩子

```c
void luaD_callhook (lua_State *L, int event, int line) {
  lua_Hook hook = L->hook;
  if (hook && L->allowhook) {
    ptrdiff_t top = savestack(L, L->top);
    ptrdiff_t ci_top = savestack(L, L->ci->top);
    lua_Debug ar;
    ar.event = event;
    ar.currentline = line;
    if (event == LUA_HOOKTAILRET)
      ar.i_ci = 0;  // 尾调用没有调用信息
    else
      ar.i_ci = cast_int(L->ci - L->base_ci);
    
    luaD_checkstack(L, LUA_MINSTACK);  // 确保足够栈空间
    L->ci->top = L->top + LUA_MINSTACK;
    L->allowhook = 0;  // 不能在钩子中递归
    lua_unlock(L);
    (*hook)(L, &ar);
    lua_lock(L);
    
    lua_assert(!L->allowhook);
    L->allowhook = 1;
    L->ci->top = restorestack(L, ci_top);
    L->top = restorestack(L, top);
  }
}
```

## 性能优化

### 1. 栈指针缓存

关键的栈指针被缓存在寄存器中：

```c
#define savestack(L,p)     ((char *)(p) - (char *)L->stack)
#define restorestack(L,n)  ((TValue *)((char *)L->stack + (n)))
```

### 2. CallInfo 预分配

CallInfo 数组预分配，避免频繁内存分配：

```c
#define BASIC_CI_SIZE  8  // 基本 CallInfo 数组大小
```

### 3. 内联函数

关键的栈操作使用内联函数或宏：

```c
#define incr_top(L) {luaD_checkstack(L,1); L->top++;}
```

## 总结

Lua 的调用栈管理系统通过以下设计实现了高效性和可靠性：

1. **简洁的栈布局**：连续的 TValue 数组，访问效率高
2. **动态栈扩展**：根据需要自动扩展栈空间
3. **结构化异常处理**：使用长跳转实现异常处理
4. **尾调用优化**：避免深度递归的栈溢出
5. **协程支持**：轻量级的协程实现
6. **调试支持**：丰富的调试钩子机制

这种设计使得 Lua 能够高效地处理函数调用、异常处理和协程切换，同时保持代码的简洁性和可维护性。

---

*相关文档：[函数系统](wiki_function.md) | [虚拟机执行](wiki_vm.md) | [对象系统](wiki_object.md)*