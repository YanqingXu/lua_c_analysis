# 🔧 递归下降解析详解

> **技术主题**：Lua 语法分析的核心算法

## 📋 概述

递归下降解析是 Lua 编译器使用的语法分析方法。它的核心思想是：为每个语法规则编写一个解析函数，通过递归调用这些函数来解析源代码。

## 🎯 基本原理

### 语法规则到函数的映射

**BNF 语法规则**：
```
statement ::= ifstat | whilestat | assignment | ...
ifstat ::= 'if' exp 'then' block ('elseif' exp 'then' block)* ('else' block)? 'end'
exp ::= term (('+' | '-') term)*
term ::= factor (('*' | '/') factor)*
factor ::= number | '(' exp ')'
```

**对应的解析函数**：
```c
static void statement(LexState *ls);
static void ifstat(LexState *ls, int line);
static void exp(LexState *ls, expdesc *v);
static void term(LexState *ls, expdesc *v);
static void factor(LexState *ls, expdesc *v);
```

## 🔍 Lua 的解析器实现

### 核心数据结构

```c
// 词法状态（lparser.c）
typedef struct LexState {
    int current;          // 当前字符
    int linenumber;       // 当前行号
    Token t;              // 当前 Token
    Token lookahead;      // 前瞻 Token
    FuncState *fs;        // 当前函数状态
    lua_State *L;         // Lua 状态
    ZIO *z;               // 输入流
    Mbuffer *buff;        // Token 缓冲区
    const char *source;   // 源文件名
} LexState;

// Token 结构
typedef struct Token {
    int token;           // Token 类型
    SemInfo seminfo;     // 语义信息（数字值、字符串等）
} Token;
```

### 基本解析函数模式

```c
// Token 匹配和前进
static void next(LexState *ls) {
    ls->lastline = ls->linenumber;
    if (ls->lookahead.token != TK_EOS) {  // 有前瞻 Token？
        ls->t = ls->lookahead;             // 使用前瞻
        ls->lookahead.token = TK_EOS;      // 清空前瞻
    }
    else {
        ls->t.token = llex(ls, &ls->t.seminfo);  // 扫描新 Token
    }
}

// 检查并消费指定 Token
static void check(LexState *ls, int c) {
    if (ls->t.token != c)
        error_expected(ls, c);
}

static void checknext(LexState *ls, int c) {
    check(ls, c);
    next(ls);
}

// 检查匹配（用于成对的符号）
static void check_match(LexState *ls, int what, int who, int where) {
    if (ls->t.token != what) {
        if (where == ls->linenumber)
            error_expected(ls, what);
        else {
            luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
                "%s expected (to close %s at line %d)",
                luaX_token2str(ls, what),
                luaX_token2str(ls, who), where));
        }
    }
    next(ls);
}
```

## 📖 实际解析示例

### 示例 1：解析 if 语句

```c
// if 语句的语法规则：
// ifstat ::= 'if' exp 'then' block ('elseif' exp 'then' block)* ('else' block)? 'end'

static void ifstat(LexState *ls, int line) {
    FuncState *fs = ls->fs;
    expdesc v;
    int escapelist = NO_JUMP;
    
    // 解析 'if' exp 'then' block
    test_then_block(ls);  // 解析条件和 then 块
    
    // 解析 'elseif' exp 'then' block（可以有多个）
    while (ls->t.token == TK_ELSEIF)
        test_then_block(ls);
    
    // 解析 'else' block（可选）
    if (ls->t.token == TK_ELSE) {
        next(ls);
        block(ls);
    }
    
    // 解析 'end'
    check_match(ls, TK_END, TK_IF, line);
}

// 辅助函数：解析条件和 then 块
static void test_then_block(LexState *ls) {
    next(ls);  // 跳过 'if' 或 'elseif'
    
    // 解析条件表达式
    expdesc v;
    expr(ls, &v);
    
    // 检查 'then'
    checknext(ls, TK_THEN);
    
    // 解析 then 块
    block(ls);
}
```

### 示例 2：解析表达式（带优先级）

```c
// 运算符优先级表
static const struct {
    lu_byte left;   // 左结合优先级
    lu_byte right;  // 右结合优先级
} priority[] = {
    {6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  // + - * / %
    {10, 9}, {5, 4},                          // ^ ..
    {3, 3}, {3, 3}, {3, 3}, {3, 3}, {3, 3}, {3, 3}  // == < > ~= <= >=
};

// 表达式解析（使用优先级爬升法）
static void expr(LexState *ls, expdesc *v) {
    subexpr(ls, v, 0);  // 从最低优先级开始
}

static BinOpr subexpr(LexState *ls, expdesc *v, unsigned int limit) {
    BinOpr op;
    UnOpr uop;
    
    // 处理一元运算符
    uop = getunopr(ls->t.token);
    if (uop != OPR_NOUNOPR) {
        next(ls);
        subexpr(ls, v, UNARY_PRIORITY);
        luaK_prefix(ls->fs, uop, v);
    }
    else {
        simpleexp(ls, v);  // 解析简单表达式
    }
    
    // 处理二元运算符（优先级爬升）
    op = getbinopr(ls->t.token);
    while (op != OPR_NOBINOPR && priority[op].left > limit) {
        expdesc v2;
        BinOpr nextop;
        
        next(ls);
        luaK_infix(ls->fs, op, v);
        
        // 递归解析右侧（使用右结合优先级）
        nextop = subexpr(ls, &v2, priority[op].right);
        
        luaK_posfix(ls->fs, op, v, &v2);
        op = nextop;
    }
    
    return op;
}
```

### 示例 3：解析函数定义

```c
// 函数定义的语法规则：
// funcstat ::= 'function' funcname body
// funcname ::= NAME ('.' NAME)* (':' NAME)?
// body ::= '(' parlist ')' block 'end'

static void funcstat(LexState *ls, int line) {
    expdesc v, b;
    
    next(ls);  // 跳过 'function'
    
    // 解析函数名（可能是 t.a.b.c 或 t.a.b:c）
    int needself = funcname(ls, &v);
    
    // 解析函数体
    body(ls, &b, needself, line);
    
    // 生成赋值代码
    luaK_storevar(ls->fs, &v, &b);
}

// 解析函数体
static void body(LexState *ls, expdesc *e, int needself, int line) {
    FuncState new_fs;
    
    // 创建新的函数状态
    open_func(ls, &new_fs);
    new_fs.f->linedefined = line;
    
    // 解析 '('
    checknext(ls, '(');
    
    // 如果是方法，添加 'self' 参数
    if (needself) {
        new_localvarliteral(ls, "self", 0);
        adjustlocalvars(ls, 1);
    }
    
    // 解析参数列表
    parlist(ls);
    
    // 解析 ')'
    checknext(ls, ')');
    
    // 解析函数体
    chunk(ls);
    
    // 检查 'end'
    new_fs.f->lastlinedefined = ls->linenumber;
    check_match(ls, TK_END, TK_FUNCTION, line);
    
    // 关闭函数
    close_func(ls);
    
    // 生成闭包指令
    pushclosure(ls, &new_fs, e);
}
```

## 💡 关键技术点

### 1. 优先级爬升法

用于解析带优先级的表达式：

```c
// 基本思想：
// 1. 从最低优先级开始
// 2. 遇到更高优先级的运算符，递归解析右侧
// 3. 使用右结合优先级处理右结合运算符（如 ^）

// 示例：解析 "a + b * c"
// 1. 解析 a（简单表达式）
// 2. 遇到 +（优先级 6）
// 3. 递归解析 "b * c"（优先级 7 > 6）
// 4. 生成代码：t1 = b * c; t2 = a + t1
```

### 2. 前瞻（Lookahead）

有些情况需要查看下一个 Token 才能决定如何解析：

```c
// 示例：区分函数调用和赋值
// a(...) 是函数调用
// a = ... 是赋值

static void exprstat(LexState *ls) {
    FuncState *fs = ls->fs;
    LHS_assign v;
    primaryexp(ls, &v.v);
    
    if (v.v.k == VCALL) {  // 函数调用
        SETARG_C(getcode(fs, &v.v), 1);
    }
    else {  // 赋值
        assignment(ls, &v, 1);
    }
}
```

### 3. 错误恢复

遇到语法错误时，跳到同步点继续解析：

```c
static void error_expected(LexState *ls, int token) {
    luaX_syntaxerror(ls,
        luaO_pushfstring(ls->L, "%s expected", luaX_token2str(ls, token)));
}

static void luaX_syntaxerror(LexState *ls, const char *msg) {
    // 添加位置信息
    msg = luaO_pushfstring(ls->L, "%s:%d: %s",
                           getstr(ls->source), ls->linenumber, msg);
    // 抛出错误
    luaD_throw(ls->L, LUA_ERRSYNTAX);
}
```

## 🎓 学习建议

1. **从简单语法规则开始**：
   - 先理解简单的语句解析（如 return、break）
   - 再学习复杂的控制结构（if、while、for）

2. **理解表达式解析**：
   - 掌握优先级爬升法
   - 理解一元和二元运算符的处理

3. **阅读实际代码**：
   - lparser.c 中的 statement() 函数
   - 跟踪简单 Lua 程序的解析过程

4. **动手实践**：
   - 添加自定义语法规则
   - 修改运算符优先级
   - 添加新的关键字

## 🔗 相关文档

- [词法分析详解](lexical_analysis.md) - Token 的生成
- [表达式解析](expression_parsing.md) - 表达式解析的详细实现
- [代码生成算法](../compiler/codegen_algorithm.md) - 如何在解析时生成代码

---

*返回：[解析器模块总览](wiki_parser.md)*
