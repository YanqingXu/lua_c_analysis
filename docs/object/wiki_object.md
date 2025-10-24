# 📊 对象系统模块总览

> **模块定位**：Lua 数据类型实现的核心

## 📋 模块概述

对象系统模块定义了 Lua 的 8 种基本数据类型，提供统一的值表示机制（TValue），实现了 Lua 的动态类型系统。这是整个 Lua 实现的基础。

### 核心文件

- `lobject.c/h` - 对象类型定义和操作
- `ltable.c/h` - 表数据结构实现
- `lstring.c/h` - 字符串管理
- `lfunc.c/h` - 函数对象管理

## 🎯 核心技术

### 1. TValue 统一值表示

**Tagged Union 设计**：

```c
typedef struct lua_TValue {
    Value value;    // 值联合体
    int tt;         // 类型标记
} TValue;

typedef union Value {
    GCObject *gc;   // GC 对象指针
    void *p;        // 轻量用户数据
    lua_Number n;   // 数字
    int b;          // 布尔值
} Value;
```

**优势**：
- 所有值大小相同（便于栈管理）
- 类型检查通过整数比较（高效）
- 支持动态类型转换

### 2. 八种基本类型

| 类型 | 说明 | GC管理 |
|------|------|--------|
| nil | 空值 | 否 |
| boolean | 布尔值 | 否 |
| number | 数字（double） | 否 |
| string | 字符串 | 是 |
| table | 表 | 是 |
| function | 函数 | 是 |
| userdata | 用户数据 | 是 |
| thread | 协程 | 是 |

### 3. Table 混合数据结构

**数组部分 + 哈希部分**：

```
Table:
┌──────────────────┐
│  Array Part      │
│  [1] = value1    │
│  [2] = value2    │
│  [3] = value3    │
├──────────────────┤
│  Hash Part       │
│  ["a"] = value4  │
│  ["b"] = value5  │
└──────────────────┘
```

**自动调整策略**：
- 根据使用模式动态调整数组和哈希部分的大小
- 整数键尽量放入数组部分（O(1)访问）
- 其他键放入哈希部分
- rehash 时重新分配空间

### 4. 字符串驻留机制

**Hash Table 管理**：
- 所有字符串存储在全局字符串表中
- 相同内容的字符串共享同一个对象
- 字符串比较简化为指针比较

**优势**：
- 节省内存
- 字符串比较O(1)
- 自动去重

### 5. 函数对象和闭包

**Lua 闭包**：
```c
typedef struct LClosure {
    ClosureHeader;
    struct Proto *p;     // 函数原型
    UpVal *upvals[1];    // Upvalue 数组
} LClosure;
```

**C 闭包**：
```c
typedef struct CClosure {
    ClosureHeader;
    lua_CFunction f;     // C 函数指针
    TValue upvalue[1];   // Upvalue 数组
} CClosure;
```

**Upvalue 管理**：
- Open 状态：指向栈上的变量
- Closed 状态：复制变量值到 Upvalue 中

### 6. 元表和元方法

**元表机制**：
- 每种类型可以有自己的元表
- 通过元方法定制类型行为
- 支持运算符重载

**常用元方法**：
- `__index` - 索引访问
- `__newindex` - 索引赋值
- `__add`, `__sub` 等 - 算术运算
- `__call` - 函数调用
- `__gc` - 垃圾回收

## 📚 详细技术文档

- [TValue 实现详解](tvalue_implementation.md) - 统一值表示的实现
- [Table 数据结构](table_structure.md) - 表的混合结构详解
- [字符串驻留机制](string_interning.md) - 字符串管理的实现
- [闭包实现原理](closure_implementation.md) - 闭包和 Upvalue
- [元表机制](metatable_mechanism.md) - 元表和元方法
- [类型转换](type_conversion.md) - 自动类型转换规则

## 🔗 相关模块

- [内存管理模块](../memory/wiki_memory.md) - 对象的内存分配
- [垃圾回收模块](../gc/wiki_gc.md) - 对象的生命周期管理
- [虚拟机模块](../vm/wiki_vm.md) - 对象的操作和运算

---

*继续阅读：[TValue 实现详解](tvalue_implementation.md)*
