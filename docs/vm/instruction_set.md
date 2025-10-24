# 🎯 Lua 指令集详解

> **技术主题**：Lua 5.1 虚拟机的 38 条核心指令

## 📋 概述

Lua 5.1 虚拟机使用 38 条指令实现完整的语言语义。每条指令都是 32 位固定长度，采用不同的编码格式来适应不同的操作需求。

## 🔧 指令编码格式

### 三种指令格式

```
iABC 格式（最常用）:
┌───────┬───────┬───────┬───────┐
│  OP   │   A   │   B   │   C   │
│ 6bit  │ 8bit  │ 9bit  │ 9bit  │
└───────┴───────┴───────┴───────┘
用途：三个操作数的指令（如 ADD、SUB）

iABx 格式:
┌───────┬───────┬───────────────┐
│  OP   │   A   │      Bx       │
│ 6bit  │ 8bit  │    18bit      │
└───────┴───────┴───────────────┘
用途：需要大常量索引的指令（如 LOADK）

iAsBx 格式:
┌───────┬───────┬───────────────┐
│  OP   │   A   │     sBx       │
│ 6bit  │ 8bit  │18bit(有符号)  │
└───────┴───────┴───────────────┘
用途：需要跳转偏移的指令（如 JMP）
```

### 编码宏定义

```c
#define GET_OPCODE(i)   (cast(OpCode, (i) & 0x3F))
#define GET_A(i)        (cast(int, ((i) >> 6) & 0xFF))
#define GET_B(i)        (cast(int, ((i) >> 23) & 0x1FF))
#define GET_C(i)        (cast(int, ((i) >> 14) & 0x1FF))
#define GET_Bx(i)       (cast(int, ((i) >> 14) & 0x3FFFF))
#define GET_sBx(i)      (GET_Bx(i) - MAXARG_sBx)
```

## 📊 指令分类详解

### 1. 数据移动指令

#### OP_MOVE（寄存器间移动）
```
格式：MOVE A B
功能：R(A) := R(B)
说明：将寄存器 B 的值复制到寄存器 A
示例：MOVE 0 1  ; R(0) = R(1)
```

#### OP_LOADK（加载常量）
```
格式：LOADK A Bx
功能：R(A) := Kst(Bx)
说明：将常量表中索引 Bx 的常量加载到寄存器 A
示例：LOADK 0 1  ; R(0) = constants[1]
```

#### OP_LOADBOOL（加载布尔值）
```
格式：LOADBOOL A B C
功能：R(A) := (Bool)B; if (C) pc++
说明：将布尔值 B 加载到 R(A)，如果 C 非零则跳过下一条指令
示例：LOADBOOL 0 1 0  ; R(0) = true
```

#### OP_LOADNIL（加载 nil）
```
格式：LOADNIL A B
功能：R(A) := ... := R(B) := nil
说明：将 R(A) 到 R(B) 的所有寄存器设置为 nil
示例：LOADNIL 0 2  ; R(0) = R(1) = R(2) = nil
```

### 2. 算术运算指令

#### OP_ADD（加法）
```
格式：ADD A B C
功能：R(A) := RK(B) + RK(C)
说明：将 RK(B) 和 RK(C) 相加，结果存入 R(A)
注：RK(x) 表示 x < 256 时为 R(x)，否则为 Kst(x-256)
示例：ADD 0 1 2  ; R(0) = R(1) + R(2)
```

#### OP_SUB（减法）
```
格式：SUB A B C
功能：R(A) := RK(B) - RK(C)
```

#### OP_MUL（乘法）
```
格式：MUL A B C
功能：R(A) := RK(B) * RK(C)
```

#### OP_DIV（除法）
```
格式：DIV A B C
功能：R(A) := RK(B) / RK(C)
```

#### OP_MOD（取模）
```
格式：MOD A B C
功能：R(A) := RK(B) % RK(C)
```

#### OP_POW（幂运算）
```
格式：POW A B C
功能：R(A) := RK(B) ^ RK(C)
```

#### OP_UNM（取负）
```
格式：UNM A B
功能：R(A) := -R(B)
```

### 3. 表操作指令

#### OP_NEWTABLE（创建新表）
```
格式：NEWTABLE A B C
功能：R(A) := {} (size = BC)
说明：创建新表，B 为数组部分大小提示，C 为哈希部分大小提示
示例：NEWTABLE 0 0 0  ; R(0) = {}
```

#### OP_GETTABLE（表索引读取）
```
格式：GETTABLE A B C
功能：R(A) := R(B)[RK(C)]
说明：从表 R(B) 中读取键 RK(C) 对应的值到 R(A)
示例：GETTABLE 0 1 2  ; R(0) = R(1)[R(2)]
```

#### OP_SETTABLE（表索引赋值）
```
格式：SETTABLE A B C
功能：R(A)[RK(B)] := RK(C)
说明：将值 RK(C) 存入表 R(A) 的键 RK(B)
示例：SETTABLE 0 1 2  ; R(0)[R(1)] = R(2)
```

#### OP_SETLIST（批量设置数组元素）
```
格式：SETLIST A B C
功能：R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B
说明：批量设置表的数组部分，用于表构造器
```

### 4. 控制流指令

#### OP_JMP（无条件跳转）
```
格式：JMP sBx
功能：pc += sBx
说明：无条件跳转 sBx 条指令（可正可负）
示例：JMP 5  ; 向前跳转 5 条指令
```

#### OP_EQ（相等比较）
```
格式：EQ A B C
功能：if ((RK(B) == RK(C)) ~= A) then pc++
说明：比较 RK(B) 和 RK(C)，结果与 A 不同则跳过下一条指令
通常后面跟一个 JMP 指令
```

#### OP_LT（小于比较）
```
格式：LT A B C
功能：if ((RK(B) < RK(C)) ~= A) then pc++
```

#### OP_LE（小于等于比较）
```
格式：LE A B C
功能：if ((RK(B) <= RK(C)) ~= A) then pc++
```

#### OP_TEST（逻辑测试）
```
格式：TEST A C
功能：if not (R(A) <=> C) then pc++
说明：测试 R(A) 的布尔值，与 C 不同则跳过下一条指令
```

#### OP_TESTSET（测试并设置）
```
格式：TESTSET A B C
功能：if (R(B) <=> C) then R(A) := R(B) else pc++
说明：用于实现短路求值（and/or）
```

### 5. 函数调用指令

#### OP_CALL（函数调用）
```
格式：CALL A B C
功能：R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1))
说明：
  - R(A) 是函数对象
  - B-1 是参数个数（B=0 表示使用到栈顶的所有值）
  - C-1 是返回值个数（C=0 表示接受所有返回值）
示例：CALL 0 2 3  ; R(0), R(1) = R(0)(R(1))
```

#### OP_TAILCALL（尾调用）
```
格式：TAILCALL A B C
功能：return R(A)(R(A+1), ... ,R(A+B-1))
说明：尾调用优化，重用当前栈帧
```

#### OP_RETURN（返回）
```
格式：RETURN A B
功能：return R(A), ... ,R(A+B-2)
说明：从函数返回，B-1 是返回值个数（B=0 表示返回到栈顶的所有值）
示例：RETURN 0 2  ; return R(0)
```

### 6. 闭包和 Upvalue 指令

#### OP_CLOSURE（创建闭包）
```
格式：CLOSURE A Bx
功能：R(A) := closure(KPROTO[Bx], R(base), ... ,R(base+n))
说明：创建新闭包，Bx 是函数原型在常量表中的索引
```

#### OP_GETUPVAL（读取 Upvalue）
```
格式：GETUPVAL A B
功能：R(A) := UpValue[B]
说明：将 Upvalue[B] 的值读取到 R(A)
```

#### OP_SETUPVAL（设置 Upvalue）
```
格式：SETUPVAL A B
功能：UpValue[B] := R(A)
说明：将 R(A) 的值存入 Upvalue[B]
```

#### OP_CLOSE（关闭 Upvalue）
```
格式：CLOSE A
功能：close all variables in the stack up to (>=) R(A)
说明：关闭栈上 >= R(A) 的所有 Upvalue（从 open 变为 closed）
```

### 7. 其他指令

#### OP_CONCAT（字符串连接）
```
格式：CONCAT A B C
功能：R(A) := R(B).. ... ..R(C)
说明：将 R(B) 到 R(C) 的所有值连接成字符串
示例：CONCAT 0 1 3  ; R(0) = R(1) .. R(2) .. R(3)
```

#### OP_LEN（取长度）
```
格式：LEN A B
功能：R(A) := length of R(B)
说明：获取 R(B) 的长度（# 运算符）
```

#### OP_VARARG（可变参数）
```
格式：VARARG A B
功能：R(A), R(A+1), ..., R(A+B-1) = vararg
说明：展开可变参数（...）
```

## 💡 指令优化技术

### 1. RK 编码优化

RK(x) 可以是寄存器或常量：
- `x < 256`：表示寄存器 R(x)
- `x >= 256`：表示常量 Kst(x-256)

这样一个操作数可以同时支持寄存器和常量，减少指令种类。

### 2. 跳转链

Lua 使用跳转链来优化条件跳转：
```lua
if a and b then
    -- code
end
```

生成的字节码：
```
TEST    R(a) 0      ; 测试 a
JMP     L1          ; a 为 false 时跳转
TEST    R(b) 0      ; 测试 b
JMP     L1          ; b 为 false 时跳转
; then 分支代码
L1:
```

### 3. 短路求值

`and` 和 `or` 运算使用 TESTSET 指令实现短路求值：
```lua
local c = a or b
```

生成的字节码：
```
TESTSET 0 R(a) 1    ; if a then R(0) = a else continue
JMP     L1
MOVE    0 R(b)      ; R(0) = b
L1:
```

## 🎓 学习建议

1. **从简单指令开始**：先理解 MOVE、LOADK 等基本指令
2. **结合源码分析**：编译简单的 Lua 代码，查看生成的字节码
3. **使用 luac**：`luac -l -l script.lua` 查看详细的字节码列表
4. **调试跟踪**：在虚拟机中单步执行，观察指令效果

## 🔗 相关文档

- [执行循环实现](execution_loop.md) - 指令如何被执行
- [寄存器管理](register_management.md) - 寄存器的分配和使用
- [字节码生成](../compiler/codegen_algorithm.md) - 指令如何被生成

---

*返回：[虚拟机模块总览](wiki_vm.md)*
