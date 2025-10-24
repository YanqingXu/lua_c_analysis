# 💾 内存管理模块总览

> **模块定位**：Lua 统一内存分配的基础设施

## 📋 模块概述

内存管理模块提供统一的内存分配接口，支持自定义内存分配器，进行内存使用统计。它是垃圾回收器和所有对象分配的基础。

### 核心文件

- `lmem.c/h` - 内存管理实现

## 🎯 核心技术

### 1. 统一的内存分配接口

**luaM_realloc_ 函数**：

```c
void *luaM_realloc_(lua_State *L, void *block, 
                    size_t osize, size_t nsize);
```

**功能**：
- `block == NULL, nsize > 0`：分配新内存
- `block != NULL, nsize == 0`：释放内存
- `block != NULL, nsize > 0`：重新分配内存

**优势**：
- 统一接口简化内存管理
- 便于跟踪内存使用
- 支持内存限制

### 2. 自定义内存分配器

**lua_Alloc 类型**：

```c
typedef void * (*lua_Alloc) (void *ud, void *ptr, 
                              size_t osize, size_t nsize);
```

**应用场景**：
- 内存池实现
- 内存限制控制
- 内存使用统计
- 调试和分析

### 3. 内存使用统计

**totalbytes 字段**：
- 跟踪总分配字节数
- 用于触发 GC
- 内存使用监控

**GC 触发机制**：
```c
if (g->totalbytes > g->GCthreshold)
    luaC_step(L);  // 触发GC
```

### 4. 内存错误处理

**错误处理策略**：
1. 分配失败时触发 GC
2. 再次尝试分配
3. 仍然失败则抛出内存错误

```c
if (newblock == NULL && nsize > 0) {
    luaC_fullgc(L);          // 完整GC
    newblock = reallocfunc(); // 再次尝试
    if (newblock == NULL)
        luaD_throw(L, LUA_ERRMEM);  // 抛出错误
}
```

## 📚 详细技术文档

- [内存分配接口](memory_allocation.md) - luaM_realloc_ 的实现细节
- [自定义分配器](custom_allocator.md) - 如何实现自定义分配器
- [内存统计机制](memory_statistics.md) - 内存使用的跟踪
- [内存错误处理](memory_error_handling.md) - 内存不足的处理策略

## 🔗 相关模块

- [垃圾回收模块](../gc/wiki_gc.md) - 基于内存管理实现自动回收
- [对象系统模块](../object/wiki_object.md) - 所有对象通过内存管理分配

---

*继续阅读：[内存分配接口](memory_allocation.md)*
