# 🚀 虚拟机模块总览

> **模块定位**：Lua 字节码解释执行的核心引擎

## 📋 模块概述

虚拟机（Virtual Machine）模块是 Lua 解释器的心脏，负责解释执行编译器生成的字节码指令。Lua 采用**基于寄存器的虚拟机架构**，这是它高性能的关键所在。

### 核心文件

- `lvm.c/h` - 虚拟机执行引擎
- `ldo.c/h` - 执行控制和函数调用
- `lstate.c/h` - 状态管理

## 🎯 核心技术

### 1. 基于寄存器的架构

**与栈式虚拟机的对比**：

```
栈式VM（如Python/Java）:
  PUSH 10      # 压入常量10
  PUSH 20      # 压入常量20
  ADD          # 弹出两个值，相加，压入结果
  STORE a      # 弹出结果，存入变量a
  需要4条指令

寄存器式VM（Lua）:
  LOADK R0 10  # 加载常量10到寄存器R0
  LOADK R1 20  # 加载常量20到寄存器R1
  ADD R2 R0 R1 # R2 = R0 + R1
  需要3条指令
```

**优势**：指令数量减少 30-40%，内存访问减少，更接近现代 CPU 架构，易于 JIT 编译优化。

### 2. 指令执行循环

虚拟机采用经典的 **Fetch-Decode-Execute** 循环：

```c
for (;;) {
    Instruction i = *pc++;     // 取指令 (Fetch)
    OpCode op = GET_OPCODE(i); // 解码 (Decode)
    
    switch (op) {              // 执行 (Execute)
        case OP_MOVE: ...
        case OP_ADD: ...
        // ... 38个指令
    }
}
```

### 3. 寄存器和栈管理

寄存器实际上是栈上的连续内存，通过 `base` 指针定位函数的寄存器起始位置。

### 4. 函数调用机制

每次函数调用创建 CallInfo 结构，保存函数对象、基址指针、栈顶指针、返回地址等信息。支持尾调用优化。

### 5. 错误处理

采用 **longjmp/setjmp** 机制实现异常处理，支持保护模式调用（pcall）。

## 📚 详细技术文档

- [指令集详解](instruction_set.md) - 38 条指令的详细说明
- [执行循环实现](execution_loop.md) - 主循环的实现细节
- [寄存器管理](register_management.md) - 寄存器分配和栈管理
- [函数调用机制](function_call.md) - 函数调用的完整流程
- [错误处理机制](error_handling.md) - 异常处理的实现
- [尾调用优化](tail_call_optimization.md) - 尾调用的优化策略

## 🔗 相关模块

- [编译器模块](../compiler/wiki_compiler.md) - 生成虚拟机执行的字节码
- [对象系统模块](../object/wiki_object.md) - 虚拟机操作的数据类型
- [运行时模块](../runtime/wiki_runtime.md) - 运行时控制和错误处理

---

*继续阅读：[指令集详解](instruction_set.md)*
