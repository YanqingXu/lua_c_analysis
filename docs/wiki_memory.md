# Lua 内存管理模块 (lmem.h/lmem.c)

## 概述

Lua 的内存管理模块提供了统一的内存分配、重新分配和释放接口。该模块是 Lua 虚拟机的核心组件之一，负责管理所有动态内存操作，包括内存统计和错误处理。

## 文件结构

- **lmem.h**: 内存管理接口定义
- **lmem.c**: 内存管理具体实现

## 核心设计理念

### 1. 统一的内存接口
所有内存操作都通过 `luaM_realloc_` 函数进行，这提供了：
- 统一的内存分配/释放入口
- 内存使用量统计
- 错误处理机制

### 2. 宏定义简化操作
通过宏定义提供便捷的内存操作接口，隐藏底层实现细节。

## 主要组件分析

### 头文件 (lmem.h)

#### 错误消息定义
```c
#define MEMERRMSG	"not enough memory"
```
定义了内存不足时的标准错误消息。

#### 核心宏定义

**1. 安全的向量重新分配**
```c
#define luaM_reallocv(L,b,on,n,e) \
	((cast(size_t, (n)+1) <= MAX_SIZET/(e)) ?  /* +1 to avoid warnings */ \
		luaM_realloc_(L, (b), (on)*(e), (n)*(e)) : \
		luaM_toobig(L))
```
- 检查溢出：防止 `n * e` 计算溢出
- 安全分配：确保请求的内存大小不超过系统限制

**2. 内存释放宏**
```c
#define luaM_freemem(L, b, s)	luaM_realloc_(L, (b), (s), 0)
#define luaM_free(L, b)		luaM_realloc_(L, (b), sizeof(*(b)), 0)
#define luaM_freearray(L, b, n, t)   luaM_reallocv(L, (b), n, 0, sizeof(t))
```
- 统一通过 `luaM_realloc_` 实现释放（新大小为 0）
- 自动计算对象大小

**3. 内存分配宏**
```c
#define luaM_malloc(L,t)	luaM_realloc_(L, NULL, 0, (t))
#define luaM_new(L,t)		cast(t *, luaM_malloc(L, sizeof(t)))
#define luaM_newvector(L,n,t) \
		cast(t *, luaM_reallocv(L, NULL, 0, n, sizeof(t)))
```
- 新分配：旧指针为 NULL，旧大小为 0
- 类型安全：自动进行类型转换

**4. 动态增长宏**
```c
#define luaM_growvector(L,v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=cast(t *, luaM_growaux_(L,v,&(size),sizeof(t),limit,e)))
```
- 按需增长：只在需要时才重新分配
- 容量管理：自动更新容量大小

**5. 向量重新分配宏**
```c
#define luaM_reallocvector(L, v,oldn,n,t) \
   ((v)=cast(t *, luaM_reallocv(L, v, oldn, n, sizeof(t))))
```

#### 函数声明
```c
LUAI_FUNC void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize, size_t size);
LUAI_FUNC void *luaM_toobig (lua_State *L);
LUAI_FUNC void *luaM_growaux_ (lua_State *L, void *block, int *size,
                               size_t size_elem, int limit, const char *errormsg);
```

### 实现文件 (lmem.c)

#### 内存分配器接口规范

文件开头的注释详细说明了 `frealloc` 函数的行为规范：

```c
/*
** About the realloc function:
** void * frealloc (void *ud, void *ptr, size_t osize, size_t nsize);
** (`osize' is the old size, `nsize' is the new size)
**
** Lua ensures that (ptr == NULL) iff (osize == 0).
**
** * frealloc(ud, NULL, 0, x) creates a new block of size `x'
** * frealloc(ud, p, x, 0) frees the block `p'
** * frealloc(ud, NULL, 0, 0) does nothing
*/
```

**关键约定：**
- `ptr == NULL` 当且仅当 `osize == 0`
- 新分配：`frealloc(ud, NULL, 0, x)`
- 释放内存：`frealloc(ud, p, x, 0)`
- 空操作：`frealloc(ud, NULL, 0, 0)`

#### 核心函数实现

**1. luaM_growaux_ - 动态增长辅助函数**

```c
void *luaM_growaux_ (lua_State *L, void *block, int *size, size_t size_elems,
                     int limit, const char *errormsg)
```

**功能：**
- 实现数组/向量的动态增长
- 采用倍增策略提高效率
- 处理增长限制和错误情况

**增长策略：**
1. 检查是否已达到限制的一半
2. 如果是，则设置为限制值
3. 否则，大小翻倍
4. 确保最小大小为 `MINSIZEARRAY` (4)

**2. luaM_toobig - 内存块过大错误处理**

```c
void *luaM_toobig (lua_State *L) {
  luaG_runerror(L, "memory allocation error: block too big");
  return NULL;
}
```

**功能：**
- 处理内存块大小超出限制的情况
- 抛出运行时错误
- 返回 NULL（避免编译器警告）

**3. luaM_realloc_ - 通用内存分配函数**

```c
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize)
```

**功能：**
- 所有内存操作的统一入口
- 调用用户定义的分配器
- 更新内存使用统计
- 处理分配失败

**实现细节：**
1. 获取全局状态：`global_State *g = G(L)`
2. 断言检查：`lua_assert((osize == 0) == (block == NULL))`
3. 调用分配器：`block = (*g->frealloc)(g->ud, block, osize, nsize)`
4. 错误处理：分配失败时抛出 `LUA_ERRMEM` 异常
5. 更新统计：`g->totalbytes = (g->totalbytes - osize) + nsize`
6. 最终检查：`lua_assert((nsize == 0) == (block == NULL))`

## 设计特点

### 1. 安全性
- **溢出检查**：防止大小计算溢出
- **断言验证**：确保函数调用的正确性
- **错误处理**：统一的错误处理机制

### 2. 效率
- **宏定义**：减少函数调用开销
- **倍增策略**：减少重新分配次数
- **统一接口**：简化内存管理逻辑

### 3. 可扩展性
- **用户定义分配器**：支持自定义内存分配策略
- **内存统计**：提供内存使用量跟踪
- **模块化设计**：清晰的接口分离

### 4. 一致性
- **统一的分配/释放**：所有操作通过同一函数
- **标准化接口**：遵循 C 标准库风格
- **类型安全**：通过宏提供类型检查

## 使用模式

### 典型的内存操作流程

1. **分配新对象**：
   ```c
   MyType *obj = luaM_new(L, MyType);
   ```

2. **分配数组**：
   ```c
   int *array = luaM_newvector(L, count, int);
   ```

3. **动态增长**：
   ```c
   luaM_growvector(L, array, used, size, int, MAX_INT, "array");
   ```

4. **释放内存**：
   ```c
   luaM_free(L, obj);
   luaM_freearray(L, array, size, int);
   ```

## 与其他模块的关系

- **lstate.h/lstate.c**：提供全局状态和分配器接口
- **ldo.h/ldo.c**：提供异常处理机制
- **ldebug.h/ldebug.c**：提供错误报告功能
- **lgc.h/lgc.c**：垃圾收集器使用内存统计信息

## 总结

Lua 的内存管理模块通过统一的接口、安全的操作和高效的策略，为整个虚拟机提供了可靠的内存管理服务。其设计充分考虑了安全性、效率和可扩展性，是 Lua 虚拟机稳定运行的重要基础。