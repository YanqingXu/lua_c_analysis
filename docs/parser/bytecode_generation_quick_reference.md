# Lua 字节码生成过程 - 快速参考卡片

## 📋 核心函数速查

### 指令生成函数

| 函数 | 用途 | 返回值 |
|------|------|--------|
| `luaK_code(fs, i, line)` | 写入指令到 Proto->code | 指令位置 (pc) |
| `luaK_codeABC(fs, op, a, b, c)` | 生成 iABC 格式指令 | 指令位置 |
| `luaK_codeABx(fs, op, a, bx)` | 生成 iABx 格式指令 | 指令位置 |
| `luaK_codeAsBx(fs, op, a, sbx)` | 生成 iAsBx 格式指令 | 指令位置 |

### 寄存器管理函数

| 函数 | 用途 | 说明 |
|------|------|------|
| `luaK_reserveregs(fs, n)` | 预留 n 个寄存器 | freereg += n |
| `luaK_exp2nextreg(fs, e)` | 将表达式结果放入下一个寄存器 | 可能生成 MOVE/LOADK 等指令 |
| `luaK_exp2anyreg(fs, e)` | 将表达式结果放入任意寄存器 | 优先使用已有寄存器 |
| `luaK_exp2val(fs, e)` | 确保表达式有值 | 可能生成指令 |
| `luaK_exp2RK(fs, e)` | 转换为 RK 格式 | 寄存器或常量 |

### 跳转管理函数

| 函数 | 用途 | 说明 |
|------|------|------|
| `luaK_jump(fs)` | 生成无条件跳转 | 返回跳转指令位置 |
| `luaK_patchlist(fs, list, target)` | 回填跳转链表 | 设置跳转目标 |
| `luaK_patchtohere(fs, list)` | 回填到当前位置 | target = 当前 pc |
| `luaK_concat(fs, l1, l2)` | 连接两个跳转链表 | 合并链表 |

### 常量管理函数

| 函数 | 用途 | 返回值 |
|------|------|--------|
| `luaK_stringK(fs, s)` | 添加字符串常量 | 常量表索引 |
| `luaK_numberK(fs, n)` | 添加数字常量 | 常量表索引 |
| `luaK_boolK(fs, b)` | 处理布尔常量 | 特殊处理（不在常量表） |
| `luaK_nilK(fs)` | 处理 nil 常量 | 特殊处理（不在常量表） |

## 🔄 expdesc 类型转换流程

### 常见转换路径

```
源代码          →  初始类型      →  中间类型      →  最终类型
─────────────────────────────────────────────────────────────
常量 10         →  VKNUM        →  VK           →  VNONRELOC
局部变量 x      →  VLOCAL       →  [保持]       →  VNONRELOC
全局变量 print  →  VGLOBAL      →  VNONRELOC    →  VNONRELOC
表索引 t[k]     →  VINDEXED     →  VNONRELOC    →  VNONRELOC
函数调用 f()    →  VCALL        →  VNONRELOC    →  VNONRELOC
a + b           →  [两个操作数]  →  VRELOCABLE   →  VNONRELOC
```

### 类型说明

| 类型 | 含义 | 何时使用 |
|------|------|----------|
| VVOID | 无值 | 空表达式 |
| VNIL | nil 常量 | nil 字面量 |
| VTRUE/VFALSE | 布尔常量 | true/false |
| VKNUM | 数字常量 | 数字字面量 |
| VK | 常量表常量 | 字符串等 |
| VLOCAL | 局部变量 | 已注册的局部变量 |
| VUPVAL | Upvalue | 闭包变量 |
| VGLOBAL | 全局变量 | 全局变量访问 |
| VINDEXED | 表索引 | t[k] 形式 |
| VJMP | 跳转表达式 | 逻辑运算 |
| VRELOCABLE | 可重定位 | 刚生成的指令结果 |
| VNONRELOC | 非重定位 | 固定寄存器中的值 |
| VCALL | 函数调用 | 函数调用表达式 |
| VVARARG | 可变参数 | ... |

## 📊 指令格式速查

### iABC 格式（30 条指令）

```
 31    23    15    7     0
 +-----+-----+-----+-----+
 |  B  |  C  |  A  | OP  |
 +-----+-----+-----+-----+
  9位   9位   8位   6位
```

**用途：** 算术运算、表操作、函数调用

**示例：**
- `ADD R(A) R(B) R(C)` - 加法
- `SETTABLE R(A) RK(B) RK(C)` - 表赋值
- `CALL R(A) B C` - 函数调用

### iABx 格式（5 条指令）

```
 31          15    7     0
 +-----------+-----+-----+
 |    Bx     |  A  | OP  |
 +-----------+-----+-----+
     18位      8位   6位
```

**用途：** 常量加载、全局变量、闭包创建

**示例：**
- `LOADK R(A) K(Bx)` - 加载常量
- `GETGLOBAL R(A) K(Bx)` - 获取全局变量
- `CLOSURE R(A) Proto[Bx]` - 创建闭包

### iAsBx 格式（3 条指令）

```
 31          15    7     0
 +-----------+-----+-----+
 |   sBx     |  A  | OP  |
 +-----------+-----+-----+
  18位(有符号) 8位   6位
```

**用途：** 跳转控制、循环

**示例：**
- `JMP A sBx` - 无条件跳转
- `FORLOOP R(A) sBx` - for 循环
- `FORPREP R(A) sBx` - for 准备

## 🎯 RK 编码规则

### 编码方式

```c
// RK(x) 的含义：
// - 如果 x < 256：表示寄存器 R(x)
// - 如果 x >= 256：表示常量 K(x - 256)

// 在字节码输出中：
// - 正数：寄存器编号
// - 负数：常量表索引（-1 表示 K(0)）

#define RKASK(x)  ((x) | (1 << 8))  // 标记为常量
#define ISK(x)    ((x) & (1 << 8))  // 检查是否为常量
#define INDEXK(x) ((x) & ~(1 << 8)) // 获取常量索引
```

### 示例

```lua
local a = 10
local b = a + 20
```

字节码：
```
1  LOADK  0 -1    ; R(0) = K(0) = 10
2  ADD    1 0 -2  ; R(1) = R(0) + K(1) = a + 20
```

解读：
- `0` → 寄存器 R(0)
- `-1` → 常量 K(0)
- `-2` → 常量 K(1)

## 🔗 跳转链表机制

### 链表结构

```
跳转链表是一个单链表，每个节点是一条 JMP 指令
节点的 sBx 字段临时存储下一个节点的位置

示例：
指令 5: JMP 0 ???  → sBx = -1 (链表结束)
指令 3: JMP 0 ???  → sBx = 5 - 3 - 1 = 1 (指向指令 5)
指令 1: JMP 0 ???  → sBx = 3 - 1 - 1 = 1 (指向指令 3)

链表头 = 1
```

### 回填过程

```c
// 回填跳转链表到目标位置
void luaK_patchlist(FuncState *fs, int list, int target) {
    while (list != NO_JUMP) {
        int next = getjump(fs, list);  // 获取下一个节点
        patchtestreg(fs, list, target); // 修正测试寄存器
        fixjump(fs, list, target);      // 设置跳转目标
        list = next;
    }
}
```

## 📦 常见语句的字节码模式

### 赋值语句

```lua
local x = 10
```
→ `LOADK 0 -1  ; R(0) = 10`

### 条件语句

```lua
if x > 5 then
    print("ok")
end
```
→
```
LT       1 -1 0   ; if not (5 < x) then skip
JMP      0 2      ; 跳过 then 分支
GETGLOBAL 1 -2    ; print
LOADK    2 -3     ; "ok"
CALL     1 2 1
```

### while 循环

```lua
while x < 10 do
    x = x + 1
end
```
→
```
[loop_start]
LT       1 0 -1   ; if not (x < 10) then exit
JMP      0 2      ; 跳出循环
ADD      0 0 -2   ; x = x + 1
JMP      0 -4     ; 回到 loop_start
```

### for 循环

```lua
for i = 1, 10 do
    print(i)
end
```
→
```
LOADK     0 -1    ; 初始值 1
LOADK     1 -2    ; 限制值 10
LOADK     2 -3    ; 步长 1
FORPREP   0 3     ; 准备循环
GETGLOBAL 4 -4    ; print
MOVE      5 3     ; i
CALL      4 2 1
FORLOOP   0 -4    ; 循环控制
```

## 🚀 性能提示

### 优化建议

1. **常量折叠**：编译器会优化常量表达式
   ```lua
   local x = 10 + 20  -- 优化为 LOADK 0 30
   ```

2. **寄存器复用**：临时值使用后立即释放
   ```lua
   local a = f() + g()  -- f() 和 g() 的结果会复用寄存器
   ```

3. **尾调用**：避免栈增长
   ```lua
   return f(x)  -- 使用 TAILCALL 而不是 CALL + RETURN
   ```

4. **表预分配**：NEWTABLE 会预估大小
   ```lua
   local t = {1, 2, 3}  -- NEWTABLE 0 3 0
   ```

5. **SETLIST 批量**：大数组使用批量设置
   ```lua
   local t = {1, 2, ..., 100}  -- 使用 SETLIST 分批处理
   ```

## 📝 调试命令

```bash
# 查看字节码
luac -l file.lua

# 查看详细信息（包括常量表、局部变量）
luac -l -l file.lua

# 只检查语法
luac -p file.lua

# 编译成字节码文件
luac -o output.out file.lua
```

## 🔍 常见问题

**Q: 为什么常量表索引是负数？**  
A: 这是 luac 的显示约定，-1 表示 K(0)，-2 表示 K(1)，以此类推。

**Q: CALL 指令的 B 和 C 参数为什么要 +1？**  
A: 0 有特殊含义（表示可变数量），所以实际数量需要 +1。

**Q: 为什么有些局部变量不生成 MOVE 指令？**  
A: 如果变量已经在正确的寄存器中，编译器会优化掉不必要的 MOVE。

**Q: 跳转偏移量如何计算？**  
A: `offset = target - (current + 1)`，因为 pc 在执行 JMP 后会自动 +1。

---

**提示：** 将此文档打印或保存为书签，在学习和调试时快速查阅！

