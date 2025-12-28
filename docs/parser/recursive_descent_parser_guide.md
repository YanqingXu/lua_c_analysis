# Lua 递归下降解析器详解：基于龙书的编译器标准流程

## 引言

本文档基于《编译原理》（龙书，Compilers: Principles, Techniques, and Tools）的标准编译器架构，详细解析 `ast_calculator.lua` 中实现的**递归下降解析器（Recursive Descent Parser）**。

### 什么是编译器的标准流程？

根据龙书的定义，编译器将源程序转换为目标程序的过程分为两个主要阶段：
- **前端（Front End）**：分析阶段，包括词法分析、语法分析、语义分析
- **后端（Back End）**：综合阶段，包括中间代码生成、代码优化、目标代码生成

在我们的计算器中，实现了前端的核心阶段和一个简化的后端（直接求值）：

```
源代码字符串 → [词法分析] → Token流 → [语法分析] → 解析树 → [AST构建] → AST → [求值器] → 计算结果
```

### 本文档的组织结构

按照龙书的标准流程，本文档分为以下四个主要部分：

1. **词法分析（Lexical Analysis）** - 第一阶段
2. **语法分析（Syntax Analysis）** - 第二阶段  
3. **AST 构建（Abstract Syntax Tree Construction）** - 第三阶段
4. **求值器（Evaluator）** - 第四阶段（代码生成的简化版本）

每个阶段都会详细介绍其理论基础、实现原理、代码示例以及输入输出的转换过程。

---

## 第一阶段：词法分析（Lexical Analysis）

### 1.1 理论基础

**词法分析器（Lexical Analyzer）**，也称为**扫描器（Scanner）**或**词法器（Tokenizer）**，是编译器的第一个阶段。

#### 主要任务

根据龙书的定义，词法分析器的核心任务是：
- 读取源程序的字符流
- 将字符组合成**词法单元（Token）**
- 过滤空白符、注释等无关字符
- 识别词法错误（如非法字符）

#### 词法单元（Token）的定义

Token 是源程序中具有独立意义的最小单位，包含两个属性：
- **类型（Token Type）**：标识符、关键字、运算符、常量等
- **值（Token Value）**：Token 的字面值

在我们的计算器中，Token 类型包括：
- `NUMBER`：数字常量（如 `42`、`3.14`）
- `PLUS`：加号 `+`
- `MINUS`：减号 `-`
- `MUL`：乘号 `*`
- `DIV`：除号 `/`
- `LPAREN`：左括号 `(`
- `RPAREN`：右括号 `)`
- `EOF`：文件结束标记

### 1.2 实现原理

#### 状态机模型

词法分析器本质上是一个**有限状态自动机（Finite Automaton）**，通过状态转换来识别不同类型的 Token。

```
[开始] → [识别空白] → 跳过
       → [识别数字] → 读取完整数字 → 返回 NUMBER
       → [识别运算符] → 返回对应 Token
       → [文件末尾] → 返回 EOF
```

#### 核心数据结构

```lua
Tokenizer = {
    input = "3 + 5 * 2",    -- 输入字符串
    pos = 1,                -- 当前位置
    current_char = "3"      -- 当前字符
}
```

### 1.3 代码实现详解

#### Tokenizer 类的创建

```lua
local Tokenizer = {}
Tokenizer.__index = Tokenizer

function Tokenizer.new(input)
    return setmetatable({
        input = input,
        pos = 1,
        current_char = input:sub(1, 1)
    }, Tokenizer)
end
```

#### 字符遍历：advance() 方法

```lua
function Tokenizer:advance()
    self.pos = self.pos + 1
    if self.pos <= #self.input then
        self.current_char = self.input:sub(self.pos, self.pos)
    else
        self.current_char = nil  -- 到达文件末尾
    end
end
```

**关键点**：`advance()` 向前移动一个字符，类似于文件读取的指针移动。

#### 跳过空白符

```lua
function Tokenizer:skip_whitespace()
    while self.current_char and self.current_char:match("%s") do
        self:advance()
    end
end
```

#### 读取数字：状态机实现

```lua
function Tokenizer:read_number()
    local num_str = ""
    -- 持续读取数字和小数点
    while self.current_char and 
          (self.current_char:match("%d") or self.current_char == ".") do
        num_str = num_str .. self.current_char
        self:advance()
    end
    return {type = "NUMBER", value = num_str}
end
```

**DFA（确定有限自动机）描述**：
```
[开始] → [数字] → [数字或小数点]* → [结束]
```

#### 主函数：get_next_token()

```lua
function Tokenizer:get_next_token()
    while self.current_char do
        -- 跳过空白符
        if self.current_char:match("%s") then
            self:skip_whitespace()
        
        -- 识别数字
        elseif self.current_char:match("%d") then
            return self:read_number()
        
        -- 识别单字符运算符
        elseif self.current_char == "+" then
            self:advance()
            return {type = "PLUS", value = "+"}
        elseif self.current_char == "-" then
            self:advance()
            return {type = "MINUS", value = "-"}
        elseif self.current_char == "*" then
            self:advance()
            return {type = "MUL", value = "*"}
        elseif self.current_char == "/" then
            self:advance()
            return {type = "DIV", value = "/"}
        elseif self.current_char == "(" then
            self:advance()
            return {type = "LPAREN", value = "("}
        elseif self.current_char == ")" then
            self:advance()
            return {type = "RPAREN", value = ")"}
        
        -- 词法错误
        else
            error("Invalid character: " .. self.current_char)
        end
    end

    -- 文件结束
    return {type = "EOF", value = nil}
end
```

### 1.4 词法分析的输入输出示例

#### 示例 1：简单表达式

**输入（源代码字符串）**：
```lua
"3 + 5"
```

**输出（Token 流）**：
```lua
[
    {type = "NUMBER", value = "3"},
    {type = "PLUS", value = "+"},
    {type = "NUMBER", value = "5"},
    {type = "EOF", value = nil}
]
```

#### 示例 2：复杂表达式

**输入**：
```lua
"(10.5 + 2) * 3 / 4"
```

**输出**：
```lua
[
    {type = "LPAREN", value = "("},
    {type = "NUMBER", value = "10.5"},
    {type = "PLUS", value = "+"},
    {type = "NUMBER", value = "2"},
    {type = "RPAREN", value = ")"},
    {type = "MUL", value = "*"},
    {type = "NUMBER", value = "3"},
    {type = "DIV", value = "/"},
    {type = "NUMBER", value = "4"},
    {type = "EOF", value = nil}
]
```

### 1.5 词法分析的关键特性

1. **流式处理**：逐字符读取，不需要回溯
2. **局部性**：只需要向前看一个或几个字符
3. **错误检测**：在最早阶段发现非法字符
4. **效率高**：通常是编译器中最快的阶段

### 1.6 与下一阶段的接口

词法分析器为语法分析器提供 Token 流接口：

```lua
local tokenizer = Tokenizer.new("3 + 5")
local token1 = tokenizer:get_next_token()  -- {type="NUMBER", value="3"}
local token2 = tokenizer:get_next_token()  -- {type="PLUS", value="+"}
local token3 = tokenizer:get_next_token()  -- {type="NUMBER", value="5"}
```

**关键点**：语法分析器通过重复调用 `get_next_token()` 来获取 Token，词法分析器维护内部状态（位置指针）。

---

## 第二阶段：语法分析（Syntax Analysis / Parsing）

### 2.1 理论基础

**语法分析器（Syntax Analyzer）**，也称为**解析器（Parser）**，是编译器的第二个阶段。

#### 主要任务

根据龙书的定义，语法分析器的核心任务是：
- 接收词法分析器产生的 Token 流
- 根据**语法规则（Grammar）**检查 Token 序列是否合法
- 构建**解析树（Parse Tree）**或直接构建 AST
- 报告语法错误

#### 上下文无关文法（Context-Free Grammar, CFG）

我们的计算器使用 CFG 来定义语法规则：

```
expr    ::= term (('+' | '-') term)*
term    ::= factor (('*' | '/') factor)*
factor  ::= ('+' | '-') factor | primary
primary ::= NUMBER | '(' expr ')'
```

**巴科斯-诺尔范式（BNF）说明**：
- `::=` 表示"定义为"
- `|` 表示"或"
- `*` 表示"零次或多次"
- `()` 表示分组

#### 递归下降解析器

**递归下降解析器**是一种**自顶向下（Top-Down）**的解析技术，其特点是：
- 每个非终结符对应一个解析函数
- 通过递归调用实现语法规则的嵌套
- 适用于 **LL(1) 文法**（从左到右扫描，最左推导，向前看 1 个 Token）

### 2.2 语法规则与优先级

#### 优先级层次设计

在我们的文法中，**优先级通过调用层次实现**：

```
第4层: expr     ← 加减运算（优先级最低）
        ↓ 调用
第3层: term     ← 乘除运算（优先级中等）
        ↓ 调用
第2层: factor   ← 一元运算（优先级较高）
        ↓ 调用
第1层: primary  ← 数字、括号（优先级最高）
```

**关键洞察**：
- 优先级越高的运算符，在递归调用链中越靠近底层（叶子节点）
- 这确保高优先级运算符先被"捕获"并形成子树

#### 左递归消除

原始的左递归文法：
```
expr ::= expr '+' term  // 左递归，会导致无限递归
```

消除左递归后：
```
expr ::= term (('+' | '-') term)*  // 使用循环代替递归
```

### 2.3 Parser 类的实现

#### 初始化

```lua
local Parser = {}
Parser.__index = Parser

function Parser.new(tokenizer)
    local parser = setmetatable({
        tokenizer = tokenizer,
        current_token = nil
    }, Parser)
    -- 预读第一个 Token（Lookahead）
    parser.current_token = parser.tokenizer:get_next_token()
    return parser
end
```

**Lookahead 机制**：`current_token` 保存当前正在处理的 Token，用于判断应该选择哪条语法规则。

#### eat() 方法：Token 匹配与消费

```lua
function Parser:eat(token_type)
    if self.current_token.type == token_type then
        -- Token 匹配成功，读取下一个 Token
        self.current_token = self.tokenizer:get_next_token()
    else
        -- 语法错误
        error("Expected token type " .. token_type .. 
              " but got " .. self.current_token.type)
    end
end
```

**龙书术语**：这是**匹配（Match）**操作，确保当前 Token 符合预期，然后前进到下一个 Token。

### 2.4 解析函数详解

#### 2.4.1 primary() - 基本表达式

**语法规则**：
```
primary ::= NUMBER | '(' expr ')'
```

**实现**：
```lua
function Parser:primary()
    local token = self.current_token

    if token.type == "NUMBER" then
        -- 终结符：直接创建叶子节点
        self:eat("NUMBER")
        return ASTNode.Number(token.value)

    elseif token.type == "LPAREN" then
        -- 括号表达式：递归调用 expr
        self:eat("LPAREN")
        local node = self:expr()  -- 关键：递归回到顶层
        self:eat("RPAREN")
        return node
    end

    error("Invalid primary expression")
end
```

**解析树示例**：解析 `(3 + 5)`

```
primary()
  ↓
  consume '('
  ↓
  expr() 解析 "3 + 5"
  ↓
  consume ')'
  ↓
  返回 expr() 的结果
```

#### 2.4.2 factor() - 一元运算

**语法规则**：
```
factor ::= ('+' | '-') factor | primary
```

**实现**：
```lua
function Parser:factor()
    local token = self.current_token

    if token.type == "PLUS" then
        self:eat("PLUS")
        -- 递归调用 factor（右结合）
        return self:factor()

    elseif token.type == "MINUS" then
        self:eat("MINUS")
        -- 创建一元操作节点
        return ASTNode.UnaryOp("-", self:factor())
    end

    -- 没有一元运算符，降级到 primary
    return self:primary()
end
```

**右结合性**：`-+-5` 被解析为 `-(+(-(5)))`

**解析过程**：
```
factor() 遇到 '-'
  ↓
  eat('-'), 创建 UnaryOp('-', ...)
  ↓
  递归 factor() 遇到 '+'
  ↓
  递归 factor() 遇到 '-'
  ↓
  递归 factor() → primary() → 5
```

#### 2.4.3 term() - 乘除运算

**语法规则**：
```
term ::= factor (('*' | '/') factor)*
```

**实现**：
```lua
function Parser:term()
    -- 解析第一个 factor
    local node = self:factor()

    -- 循环处理后续的 * 和 / 运算符（左结合）
    while self.current_token.type == "MUL"
       or self.current_token.type == "DIV" do

        local token = self.current_token
        self:eat(token.type)

        -- 构建二元操作节点
        node = ASTNode.BinaryOp(
            token.value,
            node,              -- 左操作数（累积结果）
            self:factor()      -- 右操作数（新的 factor）
        )
    end

    return node
end
```

**左结合性实现**：通过 `while` 循环不断将新运算符和右操作数附加到左侧累积结果上。

**解析过程示例**：`2 * 3 / 4`

```
步骤1：node = factor() → 2

步骤2：遇到 '*'
  node = BinaryOp('*', 2, factor())
       = BinaryOp('*', 2, 3)

步骤3：遇到 '/'（while 循环继续）
  node = BinaryOp('/', BinaryOp('*', 2, 3), factor())
       = BinaryOp('/', BinaryOp('*', 2, 3), 4)

最终解析树：
       /
      / \
     *   4
    / \
   2   3
```

#### 2.4.4 expr() - 加减运算（顶层）

**语法规则**：
```
expr ::= term (('+' | '-') term)*
```

**实现**：
```lua
function Parser:expr()
    local node = self:term()

    while self.current_token.type == "PLUS"
       or self.current_token.type == "MINUS" do

        local token = self.current_token
        self:eat(token.type)

        node = ASTNode.BinaryOp(
            token.value,
            node,
            self:term()
        )
    end

    return node
end
```

**结构对称性**：`expr()` 和 `term()` 的结构完全相同，只是调用的下层函数不同。

### 2.5 优先级的实现原理

#### 关键机制

**案例**：解析 `1 + 2 * 3`

```
步骤1: expr() 开始
  ↓
步骤2: 调用 term() 解析第一个项
  ↓
步骤3: term() 调用 factor() → primary() 得到 '1'
       term() 检查下一个 Token 是 '+' (不是 * 或 /)
       term() 立即返回 '1'
  ↓
步骤4: expr() 得到 node = 1, 遇到 '+'
       eat('+')
  ↓
步骤5: expr() 调用 term() 解析右侧
  ↓
步骤6: term() 得到 '2', 遇到 '*'
       term() 继续循环，构建 BinaryOp('*', 2, 3)
       term() 返回完整的乘法子树
  ↓
步骤7: expr() 构建 BinaryOp('+', 1, BinaryOp('*', 2, 3))
```

**最终解析树**：
```
     +
    / \
   1   *
      / \
     2   3
```

**核心原理**：
- 当 `expr()` 调用 `term()` 时，`term()` 会**贪婪地**消费所有连续的 `*` 和 `/` 运算符
- 这确保了乘除法形成完整的子树后才返回给加减法
- 自底向上构建，高优先级运算符在树的底层（先计算）

### 2.6 语法分析的输入输出示例

#### 示例：`3 + 5 * 2`

**输入（Token 流）**：
```lua
[NUMBER(3), PLUS, NUMBER(5), MUL, NUMBER(2), EOF]
```

**输出（解析树的逻辑结构）**：
```
expr()
├─ term() → 3
│   └─ factor() → primary() → 3
├─ 遇到 PLUS
└─ term() → 5 * 2
    ├─ factor() → primary() → 5
    ├─ 遇到 MUL
    └─ factor() → primary() → 2
    └─ 返回 BinaryOp('*', 5, 2)

最终返回: BinaryOp('+', 3, BinaryOp('*', 5, 2))
```

### 2.7 与下一阶段的接口

语法分析器为 AST 构建阶段提供结构信息：

```lua
local parser = Parser.new(tokenizer)
local result = parser:expr()  -- 返回 AST 根节点（或解析树）
```

---

## 第三阶段：AST 构建（Abstract Syntax Tree Construction）

### 3.1 理论基础

**抽象语法树（AST）**是源程序的树形表示，与解析树相比，AST 去除了语法细节，只保留语义信息。

#### AST vs 解析树（Parse Tree）

**解析树**（具体语法树）：
```
       expr
      / | \
   term '+' term
    |        |
  primary  term
    |      / | \
  NUMBER factor '*' factor
   '3'     |        |
        primary  primary
           |        |
        NUMBER   NUMBER
          '5'      '2'
```

**AST**（抽象语法树）：
```
     +
    / \
   3   *
      / \
     5   2
```

**关键区别**：
- 解析树包含所有语法规则节点（expr、term、factor 等）
- AST 只包含操作符和操作数，去除了语法冗余

#### 在我们的实现中

我们采用**语法制导翻译（Syntax-Directed Translation）**：在语法分析的同时直接构建 AST，不生成中间的解析树。

### 3.2 AST 节点类型设计

#### 节点类型定义

```lua
-- 数字节点（叶子节点）
{
    type = "Number",
    value = 42
}

-- 二元操作节点（内部节点）
{
    type = "BinaryOp",
    operator = "+",
    left = <AST节点>,
    right = <AST节点>
}

-- 一元操作节点（内部节点）
{
    type = "UnaryOp",
    operator = "-",
    expr = <AST节点>
}
```

### 3.3 AST 节点构造函数

#### Number 节点

```lua
function ASTNode.Number(value)
    return setmetatable({
        type = "Number",
        value = tonumber(value)  -- 转换为数值类型
    }, ASTNode)
end
```

**用途**：表示数字字面量，如 `42`、`3.14`

#### BinaryOp 节点

```lua
function ASTNode.BinaryOp(operator, left, right)
    return setmetatable({
        type = "BinaryOp",
        operator = operator,  -- '+', '-', '*', '/'
        left = left,          -- 左子树
        right = right         -- 右子树
    }, ASTNode)
end
```

**用途**：表示二元运算，如 `3 + 5`、`2 * 4`

#### UnaryOp 节点

```lua
function ASTNode.UnaryOp(operator, expr)
    return setmetatable({
        type = "UnaryOp",
        operator = operator,  -- '+', '-'
        expr = expr           -- 子表达式
    }, ASTNode)
end
```

**用途**：表示一元运算，如 `-5`、`+(3)`

### 3.4 AST 构建过程示例

#### 示例 1：简单表达式 `3 + 5`

**语法分析过程**：
```
1. expr() 调用 term()
   term() 返回 Number(3)

2. expr() 遇到 PLUS，eat(PLUS)

3. expr() 调用 term()
   term() 返回 Number(5)

4. expr() 构建 BinaryOp('+', Number(3), Number(5))
```

**生成的 AST**：
```lua
{
    type = "BinaryOp",
    operator = "+",
    left = {type = "Number", value = 3},
    right = {type = "Number", value = 5}
}
```

**树形表示**：
```
     +
    / \
   3   5
```

#### 示例 2：复杂表达式 `3 + 5 * 2`

**生成的 AST**：
```lua
{
    type = "BinaryOp",
    operator = "+",
    left = {type = "Number", value = 3},
    right = {
        type = "BinaryOp",
        operator = "*",
        left = {type = "Number", value = 5},
        right = {type = "Number", value = 2}
    }
}
```

**树形表示**：
```
       +
      / \
     3   *
        / \
       5   2
```

#### 示例 3：一元运算 `-(3 + 5)`

**生成的 AST**：
```lua
{
    type = "UnaryOp",
    operator = "-",
    expr = {
        type = "BinaryOp",
        operator = "+",
        left = {type = "Number", value = 3},
        right = {type = "Number", value = 5}
    }
}
```

**树形表示**：
```
     -
     |
     +
    / \
   3   5
```

### 3.5 AST 的可视化打印

#### 实现递归打印方法

```lua
function ASTNode:print(indent)
    indent = indent or 0
    local prefix = string.rep("  ", indent)

    if self.type == "Number" then
        print(prefix .. "Number: " .. self.value)

    elseif self.type == "UnaryOp" then
        print(prefix .. "UnaryOp: " .. self.operator)
        self.expr:print(indent + 1)

    elseif self.type == "BinaryOp" then
        print(prefix .. "BinaryOp: " .. self.operator)
        print(prefix .. "  Left:")
        self.left:print(indent + 2)
        print(prefix .. "  Right:")
        self.right:print(indent + 2)
    end
end
```

#### 使用示例

```lua
local ast = parser:parse()
ast:print()
```

**输出（表达式 `3 + 5 * 2`）**：
```
BinaryOp: +
  Left:
    Number: 3
  Right:
    BinaryOp: *
      Left:
        Number: 5
      Right:
        Number: 2
```

### 3.6 AST 的优势

1. **语义清晰**：只保留程序的语义结构，去除语法噪音
2. **便于遍历**：树形结构便于递归遍历和转换
3. **优化基础**：许多编译器优化基于 AST 进行（常量折叠、死代码消除等）
4. **中间表示**：AST 是前端和后端的标准接口

### 3.7 与下一阶段的接口

AST 作为数据结构传递给求值器：

```lua
local tokenizer = Tokenizer.new("3 + 5 * 2")
local parser = Parser.new(tokenizer)
local ast = parser:parse()  -- 返回 AST 根节点

-- AST 可以被打印、遍历或传递给求值器
ast:print()
local result = evaluator:visit(ast)
```

---

## 第四阶段：求值器（Evaluator / Code Generation）

### 4.1 理论基础

在龙书中，编译器的后端通常包括：
- **中间代码生成**
- **代码优化**
- **目标代码生成**

我们的计算器实现了一个简化的后端：**解释执行（Interpretation）**，即直接遍历 AST 并计算结果，而不生成目标代码。

#### 解释器 vs 编译器

**解释器（Interpreter）**：
- 直接执行 AST
- 输出计算结果
- 适合交互式环境和脚本语言

**编译器（Compiler）**：
- 将 AST 转换为目标代码（如汇编、机器码）
- 输出可执行文件
- 适合静态编译语言

### 4.2 访问者模式（Visitor Pattern）

我们使用**访问者模式**遍历 AST：

```lua
function Evaluator:visit(node)
    if node.type == "Number" then
        return self:visit_Number(node)
    elseif node.type == "BinaryOp" then
        return self:visit_BinaryOp(node)
    elseif node.type == "UnaryOp" then
        return self:visit_UnaryOp(node)
    end
end
```

**优势**：
- 将数据结构（AST）与操作（求值）分离
- 易于扩展新的操作（如类型检查、代码生成）

### 4.3 Evaluator 实现详解

#### 初始化

```lua
local Evaluator = {}
Evaluator.__index = Evaluator

function Evaluator.new()
    return setmetatable({}, Evaluator)
end
```

#### 主访问函数

```lua
function Evaluator:visit(node)
    if node.type == "Number" then
        return node.value

    elseif node.type == "UnaryOp" then
        local v = self:visit(node.expr)  -- 递归求值子表达式
        if node.operator == "-" then
            return -v
        end
        return v  -- '+' 号直接返回

    elseif node.type == "BinaryOp" then
        -- 递归求值左右子树
        local left = self:visit(node.left)
        local right = self:visit(node.right)

        -- 根据运算符执行对应操作
        if node.operator == "+" then
            return left + right
        elseif node.operator == "-" then
            return left - right
        elseif node.operator == "*" then
            return left * right
        elseif node.operator == "/" then
            if right == 0 then
                error("Division by zero")
            end
            return left / right
        end
    end

    error("Unknown node type: " .. node.type)
end
```

### 4.4 求值过程示例

#### 示例：`3 + 5 * 2`

**AST**：
```
     +
    / \
   3   *
      / \
     5   2
```

**求值过程（后序遍历）**：
```
visit(根节点: BinaryOp('+'))
  ↓
  计算左子树: visit(Number(3))
    → 返回 3
  ↓
  计算右子树: visit(BinaryOp('*'))
    ↓
    计算左子树: visit(Number(5))
      → 返回 5
    ↓
    计算右子树: visit(Number(2))
      → 返回 2
    ↓
    执行运算: 5 * 2 = 10
    → 返回 10
  ↓
  执行运算: 3 + 10 = 13
  → 返回 13
```

**最终结果**：`13`

#### 示例：`-(3 + 5)`

**AST**：
```
     -
     |
     +
    / \
   3   5
```

**求值过程**：
```
visit(UnaryOp('-'))
  ↓
  计算子表达式: visit(BinaryOp('+'))
    ↓
    visit(Number(3)) → 3
    visit(Number(5)) → 5
    执行运算: 3 + 5 = 8
    → 返回 8
  ↓
  执行一元运算: -8
  → 返回 -8
```

**最终结果**：`-8`

### 4.5 错误处理

#### 除零检测

```lua
if node.operator == "/" then
    if right == 0 then
        error("Division by zero")
    end
    return left / right
end
```

**示例**：`10 / 0`
```
运行时错误: Division by zero
```

#### 未知节点类型

```lua
error("Unknown node type: " .. node.type)
```

### 4.6 完整的计算流程

#### 封装函数

```lua
local function calculate(expression)
    -- 第一阶段：词法分析
    local tokenizer = Tokenizer.new(expression)
    
    -- 第二阶段：语法分析 + AST 构建
    local parser = Parser.new(tokenizer)
    local ast = parser:parse()
    
    -- 第四阶段：求值
    local evaluator = Evaluator.new()
    return evaluator:visit(ast)
end
```

#### 使用示例

```lua
print(calculate("3 + 5"))           -- 输出: 8
print(calculate("3 + 5 * 2"))       -- 输出: 13
print(calculate("(3 + 5) * 2"))     -- 输出: 16
print(calculate("-5 + 3"))          -- 输出: -2
print(calculate("10 / 2 - 3"))      -- 输出: 2
print(calculate("-(3 + 2)"))        -- 输出: -5
```

---

## 完整的编译流程总结

### 数据流转示意图

```
                     编译器的四个阶段
┌─────────────────────────────────────────────────────────────┐
│                                                               │
│  源代码字符串: "3 + 5 * 2"                                    │
│        ↓                                                      │
│  ┌──────────────────────────────────────┐                   │
│  │  第一阶段：词法分析（Tokenizer）      │                   │
│  │  - 扫描字符流                          │                   │
│  │  - 识别 Token                          │                   │
│  │  - 过滤空白符                          │                   │
│  └──────────────────────────────────────┘                   │
│        ↓                                                      │
│  Token 流:                                                    │
│  [NUMBER(3), PLUS, NUMBER(5), MUL, NUMBER(2), EOF]          │
│        ↓                                                      │
│  ┌──────────────────────────────────────┐                   │
│  │  第二阶段：语法分析（Parser）          │                   │
│  │  - 检查语法正确性                      │                   │
│  │  - 根据文法规则解析                    │                   │
│  │  - 处理运算符优先级                    │                   │
│  └──────────────────────────────────────┘                   │
│        ↓                                                      │
│  ┌──────────────────────────────────────┐                   │
│  │  第三阶段：AST 构建                    │                   │
│  │  - 语法制导翻译                        │                   │
│  │  - 创建 AST 节点                       │                   │
│  │  - 形成树形结构                        │                   │
│  └──────────────────────────────────────┘                   │
│        ↓                                                      │
│  AST:                                                         │
│       +                                                       │
│      / \                                                      │
│     3   *                                                     │
│        / \                                                    │
│       5   2                                                   │
│        ↓                                                      │
│  ┌──────────────────────────────────────┐                   │
│  │  第四阶段：求值器（Evaluator）         │                   │
│  │  - 后序遍历 AST                        │                   │
│  │  - 执行算术运算                        │                   │
│  │  - 返回最终结果                        │                   │
│  └──────────────────────────────────────┘                   │
│        ↓                                                      │
│  结果: 13                                                     │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

### 各阶段的接口和数据结构

| 阶段 | 输入 | 输出 | 关键数据结构 |
|------|------|------|-------------|
| 词法分析 | 源代码字符串 | Token 流 | `{type, value}` |
| 语法分析 | Token 流 | 解析指令 | 递归调用栈 |
| AST 构建 | 解析过程 | AST | `ASTNode` 对象 |
| 求值器 | AST | 计算结果 | 数值栈（隐式） |

### 递归调用关系图

```
calculate()
    ↓
[词法分析] Tokenizer.new() → get_next_token()
    ↓
[语法分析] Parser.new() → parse()
    ↓
    expr() ────────────┐
      ↓                 │
    term()              │
      ↓                 │ (括号递归)
    factor()            │
      ↓                 │
    primary() ─────────┘
    ↓
[AST 构建] 在解析过程中通过 ASTNode 构造函数创建
    ↓
[求值] Evaluator.new() → visit()
    ↓
    结果
```

### 完整示例：`3 + 5 * 2` 的完整处理流程

#### 阶段 1：词法分析
```
输入: "3 + 5 * 2"
输出: [NUMBER(3), PLUS, NUMBER(5), MUL, NUMBER(2), EOF]
```

#### 阶段 2：语法分析
```
expr() 开始
├─ term() 获取 3
├─ 遇到 PLUS
└─ term() 获取 5 * 2
    ├─ factor() 获取 5
    ├─ 遇到 MUL
    └─ factor() 获取 2
```

#### 阶段 3：AST 构建
```
BinaryOp('+',
    Number(3),
    BinaryOp('*', Number(5), Number(2))
)

树形表示:
     +
    / \
   3   *
      / \
     5   2
```

#### 阶段 4：求值
```
visit(BinaryOp('+'))
├─ visit(Number(3)) → 3
└─ visit(BinaryOp('*'))
    ├─ visit(Number(5)) → 5
    └─ visit(Number(2)) → 2
    └─ 5 * 2 = 10
└─ 3 + 10 = 13

最终结果: 13
```

---

## 扩展与优化

### 扩展建议

#### 1. 添加新运算符

**指数运算 `^`**（右结合，高优先级）：

```lua
-- 在 Tokenizer 中添加
elseif self.current_char == "^" then
    self:advance()
    return {type = "POW", value = "^"}

-- 创建新的解析函数（在 factor 和 primary 之间）
function Parser:power()
    local node = self:primary()
    if self.current_token.type == "POW" then
        self:eat("POW")
        node = ASTNode.BinaryOp("^", node, self:power())  -- 右递归
    end
    return node
end

-- 修改 factor() 调用 power() 而非 primary()
function Parser:factor()
    -- ...
    return self:power()  -- 替代 return self:primary()
end
```

#### 2. 支持函数调用

```lua
-- 语法规则: primary ::= NUMBER | IDENT '(' args ')' | '(' expr ')'
function Parser:primary()
    local token = self.current_token
    
    if token.type == "IDENT" then
        local func_name = token.value
        self:eat("IDENT")
        self:eat("LPAREN")
        local args = self:parse_arguments()
        self:eat("RPAREN")
        return ASTNode.FunctionCall(func_name, args)
    end
    -- ... 其他情况
end
```

#### 3. 添加变量支持

```lua
-- AST 节点
function ASTNode.Variable(name)
    return setmetatable({
        type = "Variable",
        name = name
    }, ASTNode)
end

-- Evaluator 添加符号表
function Evaluator.new()
    return setmetatable({
        variables = {}  -- 符号表
    }, Evaluator)
end

function Evaluator:visit(node)
    if node.type == "Variable" then
        return self.variables[node.name] or error("Undefined variable: " .. node.name)
    end
    -- ... 其他节点类型
end
```

### 优化建议

#### 1. 常量折叠（Constant Folding）

在 AST 构建时进行优化：

```lua
function ASTNode.BinaryOp(operator, left, right)
    -- 如果两个操作数都是常量，直接计算
    if left.type == "Number" and right.type == "Number" then
        if operator == "+" then
            return ASTNode.Number(left.value + right.value)
        elseif operator == "*" then
            return ASTNode.Number(left.value * right.value)
        end
        -- ... 其他运算符
    end
    
    -- 否则创建正常的二元操作节点
    return setmetatable({...}, ASTNode)
end
```

#### 2. Token 缓存

预先读取所有 Token，避免重复字符串处理：

```lua
function Tokenizer:tokenize_all()
    local tokens = {}
    while true do
        local token = self:get_next_token()
        table.insert(tokens, token)
        if token.type == "EOF" then break end
    end
    return tokens
end
```

#### 3. 错误恢复

实现 Panic Mode 错误恢复：

```lua
function Parser:synchronize()
    -- 跳过 Token 直到找到同步点（如分号、换行符）
    while self.current_token.type ~= "EOF" do
        if self.current_token.type == "SEMICOLON" then
            self:eat("SEMICOLON")
            return
        end
        self.current_token = self.tokenizer:get_next_token()
    end
end
```

---

## 术语对照表

| 中文 | 英文 | 龙书章节 |
|-----|------|---------|
| 词法分析 | Lexical Analysis | 第 3 章 |
| 语法分析 | Syntax Analysis / Parsing | 第 4 章 |
| 抽象语法树 | Abstract Syntax Tree (AST) | 第 5 章 |
| 中间代码生成 | Intermediate Code Generation | 第 6 章 |
| 递归下降解析器 | Recursive Descent Parser | 4.4 节 |
| 上下文无关文法 | Context-Free Grammar (CFG) | 4.1 节 |
| 终结符 | Terminal Symbol | 4.1 节 |
| 非终结符 | Non-terminal Symbol | 4.1 节 |
| 左递归 | Left Recursion | 4.3 节 |
| 左结合 | Left-associative | 4.2 节 |
| 右结合 | Right-associative | 4.2 节 |
| 前看符号 | Lookahead Symbol | 4.4 节 |
| 语法制导翻译 | Syntax-Directed Translation | 第 5 章 |
| 访问者模式 | Visitor Pattern | （设计模式） |

---

## 参考资料

1. **龙书（第2版）**：Aho, A. V., Lam, M. S., Sethi, R., & Ullman, J. D. (2006). *Compilers: Principles, Techniques, and Tools (2nd Edition)*. Addison-Wesley.

2. **相关章节**：
   - 第 2 章：一个简单的语法制导翻译器
   - 第 3 章：词法分析
   - 第 4 章：语法分析
   - 第 5 章：语法制导翻译

3. **在线资源**：
   - [Crafting Interpreters](https://craftinginterpreters.com/) - 现代解释器实现教程
   - [Let's Build a Simple Interpreter](https://ruslanspivak.com/lsbasi-part1/) - 递归下降解析器教程系列

---

## 总结

通过本文档，我们详细介绍了基于龙书标准流程的递归下降解析器实现：

1. **词法分析**：将字符流转换为 Token 流，使用有限状态自动机实现
2. **语法分析**：使用递归下降方法检查语法正确性，通过调用层次实现优先级
3. **AST 构建**：采用语法制导翻译，在解析过程中直接构建抽象语法树
4. **求值器**：使用访问者模式遍历 AST，实现解释执行

这个简单的计算器展示了编译器前端的核心技术，为理解更复杂的编程语言解析器奠定了基础。通过学习这个实现，你可以掌握：

- 如何设计和实现词法分析器
- 如何使用递归下降方法进行语法分析
- 如何通过语法规则的嵌套实现运算符优先级
- 如何构建和遍历抽象语法树
- 如何将理论知识应用于实际编程

希望这份文档能帮助你深入理解编译原理，并为你未来开发更复杂的语言处理工具提供参考。

---

**作者注**：本文档基于 `ast_calculator.lua` 的重构版本编写，严格遵循龙书的编译器标准流程。建议配合源代码阅读，并尝试扩展新功能以加深理解。
