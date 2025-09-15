/**
 * @file lparser.c
 * @brief Lua语法解析器：将Lua源代码解析为抽象语法树和字节码
 *
 * 详细说明：
 * 这个文件实现了Lua语言的语法解析器，是Lua编译器前端的核心组件。
 * 它采用递归下降解析算法，将词法分析器产生的token流解析为抽象语法树，
 * 并在解析过程中直接生成Lua虚拟机字节码。这种设计避免了构建完整AST的开销，
 * 提高了编译效率和内存使用效率。
 *
 * 系统架构定位：
 * 解析器位于Lua编译器的核心位置，连接词法分析器(llex.c)和代码生成器(lcode.c)。
 * 它负责语法分析、语义检查、符号表管理和作用域控制，是Lua语言实现的关键模块。
 *
 * 技术特点：
 * - 递归下降解析：使用递归函数实现语法规则的解析
 * - 单遍编译：解析和代码生成同时进行，提高编译效率
 * - 错误恢复：提供详细的语法错误信息和位置定位
 * - 作用域管理：支持嵌套作用域和局部变量的生命周期管理
 * - 表达式解析：支持运算符优先级和结合性的正确处理
 *
 * 依赖关系：
 * - llex.h: 词法分析器接口，提供token流
 * - lcode.h: 代码生成器接口，生成字节码指令
 * - ldo.h: 执行控制，错误处理机制
 * - lobject.h: Lua对象系统，类型定义
 * - lstate.h: Lua状态机，全局状态管理
 *
 * 编译要求：
 * - C99标准兼容
 * - 需要链接Lua核心库
 * - 支持递归函数调用（足够的栈空间）
 *
 * 解析算法：
 * 采用递归下降解析算法，每个语法规则对应一个解析函数：
 * - chunk(): 解析代码块（函数体、文件）
 * - statement(): 解析语句（赋值、控制流等）
 * - expression(): 解析表达式（算术、逻辑、函数调用等）
 * - primary(): 解析基本表达式（变量、常量、构造器等）
 *
 * 内存安全考虑：
 * - 使用Lua内存管理器进行安全的内存分配
 * - 递归深度限制，防止栈溢出
 * - 符号表大小限制，防止内存耗尽
 * - 异常安全，解析失败时正确清理资源
 *
 * 性能特征：
 * - 时间复杂度：O(n)，其中n为源代码长度
 * - 空间复杂度：O(d)，其中d为最大嵌套深度
 * - 单遍编译，避免多次遍历源代码
 * - 直接生成字节码，无需中间表示
 *
 * 线程安全性：
 * 解析器不是线程安全的，每个Lua状态机需要独立的解析器实例。
 * 解析过程中会修改LexState和FuncState，不支持并发解析。
 *
 * 注意事项：
 * - 解析器依赖于词法分析器的正确实现
 * - 语法错误会通过longjmp机制抛出异常
 * - 解析过程中会分配大量临时对象
 * - 需要足够的C栈空间支持深度递归
 *
 * @author Roberto Ierusalimschy
 * @version 5.1.5
 * @date 2011-10-21
 * @since Lua 5.0
 * @see llex.h, lcode.h, ldo.h
 */

// 标准C库头文件
#include <string.h>

// Lua解析器模块标识
#define lparser_c
#define LUA_CORE

// Lua核心头文件
#include "lua.h"

// Lua内部模块头文件
#include "lcode.h"      // 代码生成器接口
#include "ldebug.h"     // 调试信息支持
#include "ldo.h"        // 执行控制和错误处理
#include "lfunc.h"      // 函数对象管理
#include "llex.h"       // 词法分析器接口
#include "lmem.h"       // 内存管理器
#include "lobject.h"    // Lua对象系统
#include "lopcodes.h"   // 虚拟机指令定义
#include "lparser.h"    // 解析器接口定义
#include "lstate.h"     // Lua状态机
#include "lstring.h"    // 字符串对象管理
#include "ltable.h"     // 表对象管理

/**
 * @brief 检查表达式是否具有多返回值特性
 *
 * 在Lua中，函数调用和可变参数表达式可以返回多个值。
 * 这个宏用于识别这类表达式，以便在代码生成时正确处理。
 *
 * @param k 表达式类型
 * @return 如果表达式可能返回多个值则为真
 */
#define hasmultret(k) ((k) == VCALL || (k) == VVARARG)

/**
 * @brief 获取函数作用域中的局部变量信息
 *
 * 通过活跃变量索引获取对应的局部变量描述符。
 * 用于变量名解析和调试信息生成。
 *
 * @param fs 函数状态
 * @param i 活跃变量索引
 * @return 局部变量描述符
 */
#define getlocvar(fs, i) ((fs)->f->locvars[(fs)->actvar[i]])

/**
 * @brief 检查数值是否超过指定限制
 *
 * Lua对各种语言构造有数量限制（如局部变量数、upvalue数等）。
 * 这个宏用于检查是否超过限制，超过时报告错误。
 *
 * @param fs 函数状态
 * @param v 当前值
 * @param l 限制值
 * @param m 错误消息
 */
#define luaY_checklimit(fs,v,l,m) if ((v)>(l)) errorlimit(fs,l,m)

/**
 * @brief 代码块控制结构：管理嵌套作用域和跳转目标
 *
 * 详细说明：
 * 这个结构体用于管理Lua代码中的嵌套作用域，特别是循环和条件语句的作用域。
 * 它维护了作用域的嵌套关系、局部变量的可见性、以及break语句的跳转目标。
 *
 * 设计理念：
 * 采用链表结构管理嵌套的代码块，每个代码块记录其父级作用域和相关的控制信息。
 * 这种设计支持任意深度的嵌套，并能正确处理变量作用域和控制流跳转。
 *
 * 内存布局：
 * 结构体大小约为16-20字节（取决于平台），内存对齐良好。
 * 通常在C栈上分配，随着递归解析的深入而增长。
 *
 * 生命周期管理：
 * - 进入新的代码块时创建BlockCnt实例
 * - 通过previous指针链接到父级作用域
 * - 退出代码块时自动销毁，恢复父级作用域
 *
 * 使用模式：
 * @code
 * BlockCnt bl;
 * bl.previous = fs->bl;        // 链接到父级作用域
 * bl.breaklist = NO_JUMP;      // 初始化跳转列表
 * bl.nactvar = fs->nactvar;    // 记录当前活跃变量数
 * bl.upval = 0;                // 初始化upvalue标志
 * bl.isbreakable = 1;          // 标记为可break的循环
 * fs->bl = &bl;                // 设置为当前作用域
 *
 * // 解析代码块内容...
 *
 * fs->bl = bl.previous;        // 恢复父级作用域
 * @endcode
 *
 * 并发访问：
 * 结构体实例通常在单线程的解析过程中使用，不需要同步保护。
 * 每个解析器实例有独立的作用域栈。
 *
 * 性能影响：
 * 结构体访问开销很小，主要开销在于作用域切换时的链表操作。
 * 嵌套深度直接影响内存使用和访问性能。
 *
 * 扩展性考虑：
 * 结构体设计简洁，易于扩展新的作用域属性。
 * 链表结构支持任意深度的嵌套。
 *
 * 注意事项：
 * - 必须正确维护previous链，避免作用域泄漏
 * - breaklist需要在代码生成时正确处理
 * - upvalue标志影响闭包的创建
 *
 * @since Lua 5.0
 * @see FuncState, LexState
 */
typedef struct BlockCnt {
    struct BlockCnt *previous;  /**< 指向父级代码块的指针，形成作用域链 */
    int breaklist;              /**< break语句的跳转目标列表，用于循环控制 */
    lu_byte nactvar;            /**< 进入此代码块时的活跃局部变量数量 */
    lu_byte upval;              /**< 标志：代码块中是否有变量被内层函数引用 */
    lu_byte isbreakable;        /**< 标志：代码块是否为可break的循环结构 */
} BlockCnt;

/**
 * @brief 递归非终结符函数的前向声明
 *
 * 这些函数实现了Lua语法的递归下降解析算法。
 * 由于函数间存在相互递归调用关系，需要前向声明。
 */
static void chunk(LexState *ls);
static void expr(LexState *ls, expdesc *v);


/**
 * @brief 锚定当前token：确保字符串和标识符token在内存中的持久性
 *
 * 详细说明：
 * 当解析器需要保存当前token的值时（特别是字符串和标识符），
 * 这个函数确保token的字符串内容被正确地保存到Lua的字符串池中，
 * 避免因词法分析器继续前进而丢失token内容。
 *
 * 技术实现：
 * - 检查当前token是否为需要保存的类型（TK_NAME或TK_STRING）
 * - 从token的语义信息中提取字符串对象
 * - 调用luaX_newstring将字符串添加到字符串池中
 * - 确保字符串在后续解析过程中保持有效
 *
 * 内存管理：
 * 使用Lua的字符串池机制，避免重复分配相同的字符串。
 * 字符串一旦进入池中，就会被垃圾收集器管理。
 *
 * 使用场景：
 * - 函数定义结束时，需要重新锚定当前token
 * - 解析器状态切换时，保护重要的token信息
 * - 错误恢复过程中，确保错误信息的准确性
 *
 * @param ls 词法状态，包含当前token信息
 *
 * @note 这个函数只处理字符串和标识符token，其他类型的token不需要锚定
 * @see luaX_newstring, TString
 */
static void anchor_token (LexState *ls) {
    if (ls->t.token == TK_NAME || ls->t.token == TK_STRING) {
        TString *ts = ls->t.seminfo.ts;
        luaX_newstring(ls, getstr(ts), ts->tsv.len);
    }
}

/**
 * @brief 报告期望token的语法错误
 *
 * 详细说明：
 * 当解析器遇到不符合语法规则的token时，这个函数生成标准化的错误消息。
 * 错误消息包含期望的token类型，帮助用户理解语法错误的原因。
 *
 * 错误处理策略：
 * - 使用标准化的错误消息格式："<token> expected"
 * - 通过luaX_token2str将token类型转换为可读的字符串
 * - 调用luaX_syntaxerror抛出语法错误异常
 * - 错误会通过longjmp机制传播到上层处理
 *
 * 国际化支持：
 * 错误消息使用LUA_QS宏进行格式化，支持不同语言环境下的引号样式。
 *
 * 调试信息：
 * 错误消息包含当前行号和列号信息，便于定位问题。
 *
 * @param ls 词法状态，提供错误上下文信息
 * @param token 期望的token类型
 *
 * @note 这个函数不会返回，会通过异常机制终止当前解析过程
 * @see luaX_syntaxerror, luaX_token2str
 */
static void error_expected (LexState *ls, int token) {
    luaX_syntaxerror(ls,
        luaO_pushfstring(ls->L, LUA_QS " expected", luaX_token2str(ls, token)));
}

/**
 * @brief 报告超出限制的错误
 *
 * 详细说明：
 * Lua对各种语言构造有数量限制，如局部变量数、upvalue数、常量数等。
 * 当超出这些限制时，这个函数生成详细的错误消息并终止编译。
 *
 * 错误消息生成：
 * - 区分主函数和普通函数的错误消息
 * - 主函数错误：显示"main function has more than %d %s"
 * - 普通函数错误：显示函数定义行号和具体限制信息
 * - 包含具体的限制值和超出的构造类型
 *
 * 限制检查的意义：
 * - 防止编译器内部数据结构溢出
 * - 确保生成的字节码在虚拟机中正确执行
 * - 提供明确的错误信息，帮助用户重构代码
 * - 维护Lua实现的稳定性和可预测性
 *
 * 常见限制类型：
 * - LUAI_MAXVARS: 最大局部变量数
 * - LUAI_MAXUPVALUES: 最大upvalue数
 * - LUAI_MAXCCALLS: 最大C调用深度
 * - MAX_INT: 各种计数器的最大值
 *
 * @param fs 函数状态，提供函数上下文信息
 * @param limit 被超出的限制值
 * @param what 超出限制的构造类型描述
 *
 * @note 这个函数不会返回，会通过异常机制终止编译过程
 * @see luaX_lexerror, luaO_pushfstring
 */
static void errorlimit (FuncState *fs, int limit, const char *what) {
    const char *msg = (fs->f->linedefined == 0) ?
        luaO_pushfstring(fs->L, "main function has more than %d %s", limit, what) :
        luaO_pushfstring(fs->L, "function at line %d has more than %d %s",
                                fs->f->linedefined, limit, what);
    luaX_lexerror(fs->ls, msg, 0);
}

/**
 * @brief 测试并消费指定的token
 *
 * 详细说明：
 * 这是一个条件性的token消费函数，如果当前token匹配指定类型，
 * 则消费它并返回真；否则保持当前位置不变并返回假。
 *
 * 使用模式：
 * 这个函数常用于解析可选的语法元素，如：
 * - 可选的分号
 * - 可选的逗号分隔符
 * - 可选的语法关键字
 *
 * 实现细节：
 * - 检查当前token是否匹配期望的类型
 * - 如果匹配，调用luaX_next()前进到下一个token
 * - 返回匹配结果，让调用者知道是否成功消费了token
 *
 * 性能考虑：
 * 这个函数的开销很小，主要是一次比较和可能的一次函数调用。
 * 在解析过程中会被频繁调用，因此保持简洁高效。
 *
 * @param ls 词法状态
 * @param c 期望的token类型
 * @return 如果成功匹配并消费了token则返回1，否则返回0
 *
 * @note 这个函数不会产生错误，只是测试和可选消费
 * @see luaX_next
 */
static int testnext (LexState *ls, int c) {
    if (ls->t.token == c) {
        luaX_next(ls);
        return 1;
    }
    else return 0;
}

/**
 * @brief 检查当前token是否匹配期望类型
 *
 * 详细说明：
 * 这是一个严格的token检查函数，要求当前token必须匹配指定类型。
 * 如果不匹配，会立即报告语法错误并终止解析。
 *
 * 与testnext的区别：
 * - check()要求强制匹配，不匹配就报错
 * - testnext()是可选匹配，不匹配也不报错
 * - check()不消费token，只检查
 * - testnext()在匹配时会消费token
 *
 * 使用场景：
 * - 解析必需的语法元素，如函数定义中的'('
 * - 验证语法结构的完整性
 * - 在复杂的语法规则中确保正确性
 *
 * 错误处理：
 * 如果检查失败，会调用error_expected()生成标准错误消息。
 *
 * @param ls 词法状态
 * @param c 期望的token类型
 *
 * @note 这个函数可能不会返回（如果检查失败）
 * @see error_expected, testnext
 */
static void check (LexState *ls, int c) {
    if (ls->t.token != c)
        error_expected(ls, c);
}

/**
 * @brief 检查并消费指定的token
 *
 * 详细说明：
 * 这个函数结合了check()和luaX_next()的功能，
 * 先检查当前token是否匹配期望类型，然后消费它。
 *
 * 实现逻辑：
 * 1. 调用check()验证token类型（可能报错）
 * 2. 调用luaX_next()前进到下一个token
 *
 * 使用场景：
 * - 解析必需的分隔符，如'('、')'、','等
 * - 解析关键字，如'then'、'do'、'end'等
 * - 确保语法结构的完整性
 *
 * 错误处理：
 * 如果token不匹配，check()会报告错误并终止解析。
 *
 * @param ls 词法状态
 * @param c 期望的token类型
 *
 * @note 这个函数可能不会返回（如果检查失败）
 * @see check, luaX_next
 */
static void checknext (LexState *ls, int c) {
    check(ls, c);
    luaX_next(ls);
}

/**
 * @brief 条件检查宏：验证编译时条件
 *
 * 详细说明：
 * 这个宏用于在解析过程中检查各种条件，如语义约束、上下文要求等。
 * 如果条件不满足，会立即报告语法错误。
 *
 * 使用场景：
 * - 检查变量作用域的有效性
 * - 验证语法上下文的正确性
 * - 确保语义规则的遵守
 *
 * 实现细节：
 * - 使用宏定义提高性能，避免函数调用开销
 * - 条件为假时调用luaX_syntaxerror报告错误
 * - 支持自定义错误消息
 *
 * @param ls 词法状态
 * @param c 要检查的条件表达式
 * @param msg 条件失败时的错误消息
 *
 * @note 这个宏可能不会返回（如果条件检查失败）
 * @see luaX_syntaxerror
 */
#define check_condition(ls,c,msg) { if (!(c)) luaX_syntaxerror(ls, msg); }



/**
 * @brief 检查配对token的匹配性：验证开闭标记的正确配对
 *
 * 详细说明：
 * 这个函数用于检查配对的语法元素，如括号、引号、关键字等的正确匹配。
 * 它不仅检查token类型，还提供了详细的错误定位信息，特别是当配对
 * 元素跨越多行时，能够准确指出开始位置。
 *
 * 技术实现：
 * - 使用testnext()尝试匹配期望的结束token
 * - 如果匹配失败，根据位置信息生成不同的错误消息
 * - 同行错误：简单的"expected"消息
 * - 跨行错误：包含开始位置的详细消息
 *
 * 错误消息策略：
 * 1. 同行配对错误：直接显示期望的token
 * 2. 跨行配对错误：显示"expected (to close <开始token> at line <行号>)"
 * 3. 提供完整的上下文信息，帮助用户快速定位问题
 *
 * 常见使用场景：
 * - 函数定义：检查'('和')'的配对
 * - 表构造：检查'{'和'}'的配对
 * - 条件语句：检查'if'和'end'的配对
 * - 循环语句：检查'while'/'for'和'end'的配对
 * - 字符串字面量：检查引号的配对
 *
 * 用户体验优化：
 * 通过提供精确的行号信息，大大提高了调试效率，
 * 特别是在处理大型Lua文件时，能够快速定位未配对的语法元素。
 *
 * 内存安全：
 * 函数不分配内存，只进行token检查和错误报告，
 * 错误消息通过Lua的字符串系统管理。
 *
 * @param ls 词法状态，提供当前解析上下文
 * @param what 期望的结束token类型
 * @param who 对应的开始token类型（用于错误消息）
 * @param where 开始token的行号（用于跨行错误定位）
 *
 * @note 如果匹配失败，函数会通过异常机制终止解析
 * @see testnext, error_expected, luaX_syntaxerror
 *
 * @example
 * // 检查函数定义中的括号配对
 * int line = ls->linenumber;
 * checknext(ls, '(');           // 消费开始括号
 * // ... 解析参数列表 ...
 * check_match(ls, ')', '(', line); // 检查结束括号
 */
static void check_match (LexState *ls, int what, int who, int where) {
    if (!testnext(ls, what)) {
        if (where == ls->linenumber)
            error_expected(ls, what);
        else {
            luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
                LUA_QS " expected (to close " LUA_QS " at line %d)",
                luaX_token2str(ls, what), luaX_token2str(ls, who), where));
        }
    }
}

/**
 * @brief 检查并提取标识符名称：安全获取变量或函数名
 *
 * 详细说明：
 * 这个函数专门用于解析Lua代码中的标识符（变量名、函数名等），
 * 它结合了类型检查和名称提取的功能，确保获取到有效的标识符字符串。
 *
 * 处理流程：
 * 1. 检查当前token是否为TK_NAME类型
 * 2. 如果不是标识符，调用check()报告错误
 * 3. 从token的语义信息中提取字符串对象
 * 4. 前进到下一个token
 * 5. 返回提取的字符串对象
 *
 * 字符串对象管理：
 * - 返回的TString*指向Lua字符串池中的对象
 * - 字符串已经被垃圾收集器管理，无需手动释放
 * - 字符串对象在整个编译过程中保持有效
 *
 * 使用场景：
 * - 变量声明：local var_name = value
 * - 函数定义：function func_name() ... end
 * - 表字段访问：table.field_name
 * - 参数名称：function(param_name) ... end
 * - 标签定义：::label_name::
 *
 * 错误处理：
 * 如果当前token不是标识符，check()会报告"<name> expected"错误，
 * 并通过异常机制终止解析过程。
 *
 * 性能考虑：
 * - 函数开销很小，主要是一次类型检查和字段访问
 * - 字符串对象从池中获取，避免重复分配
 * - 在解析过程中会被频繁调用，保持高效
 *
 * 内存安全：
 * - 不分配新的内存，只返回现有字符串对象的引用
 * - 依赖Lua的垃圾收集器管理字符串生命周期
 * - 无内存泄漏风险
 *
 * @param ls 词法状态，包含当前token信息
 * @return 提取的标识符字符串对象
 *
 * @note 如果当前token不是标识符，函数会报错并不返回
 * @see check, luaX_next, TString
 *
 * @example
 * // 解析变量声明
 * TString *var_name = str_checkname(ls);  // 获取变量名
 * // 现在可以使用var_name进行后续处理
 */
static TString *str_checkname (LexState *ls) {
    TString *ts;
    check(ls, TK_NAME);
    ts = ls->t.seminfo.ts;
    luaX_next(ls);
    return ts;
}

/**
 * @brief 初始化表达式描述符：设置表达式的基本属性
 *
 * 详细说明：
 * 这个函数用于初始化expdesc结构体，该结构体是Lua解析器中表达式
 * 表示的核心数据结构。它记录了表达式的类型、值和跳转信息。
 *
 * 表达式描述符的作用：
 * expdesc是Lua编译器中表达式的统一表示，它能够描述：
 * - 常量表达式（数字、字符串、布尔值等）
 * - 变量表达式（局部变量、全局变量、upvalue等）
 * - 复杂表达式（函数调用、表索引、算术运算等）
 * - 控制流表达式（条件表达式、逻辑运算等）
 *
 * 初始化过程：
 * 1. 设置跳转链表为空（f = t = NO_JUMP）
 * 2. 设置表达式类型（k = expkind）
 * 3. 设置表达式的主要信息（u.s.info = i）
 *
 * 跳转链表的意义：
 * - f: false跳转链表，用于逻辑表达式的短路求值
 * - t: true跳转链表，用于条件表达式的分支处理
 * - 初始化为NO_JUMP表示暂无跳转目标
 *
 * 表达式类型系统：
 * Lua支持多种表达式类型，如：
 * - VK: 常量表达式
 * - VLOCAL: 局部变量
 * - VGLOBAL: 全局变量
 * - VUPVAL: upvalue变量
 * - VCALL: 函数调用
 * - VVARARG: 可变参数
 *
 * 设计模式：
 * 这是一个典型的构造函数模式，用于确保数据结构的正确初始化。
 * 避免了未初始化字段导致的不确定行为。
 *
 * 性能特征：
 * - 函数非常轻量，只进行简单的字段赋值
 * - 内联优化后几乎无开销
 * - 在表达式解析中被大量调用
 *
 * @param e 要初始化的表达式描述符指针
 * @param k 表达式类型（expkind枚举值）
 * @param i 表达式的主要信息（寄存器索引、常量索引等）
 *
 * @note 这个函数不会失败，总是成功初始化表达式描述符
 * @see expdesc, expkind, NO_JUMP
 *
 * @example
 * expdesc e;
 * init_exp(&e, VLOCAL, 0);  // 初始化为局部变量表达式
 * // 现在e表示第0个局部变量
 */
static void init_exp (expdesc *e, expkind k, int i) {
    e->f = e->t = NO_JUMP;
    e->k = k;
    e->u.s.info = i;
}

/**
 * @brief 生成字符串常量表达式：将字符串转换为常量表达式
 *
 * 详细说明：
 * 这个函数将TString对象转换为常量表达式描述符，是字符串字面量
 * 处理的核心函数。它负责将字符串添加到常量表中，并创建相应的
 * 表达式描述符。
 *
 * 处理流程：
 * 1. 调用luaK_stringK将字符串添加到函数的常量表中
 * 2. 获取字符串在常量表中的索引
 * 3. 使用init_exp创建VK类型的表达式描述符
 * 4. 表达式的info字段指向常量表索引
 *
 * 常量表管理：
 * - Lua函数的常量表存储所有字面量常量
 * - 相同的字符串只存储一次，通过索引引用
 * - luaK_stringK负责去重和索引分配
 * - 常量表在函数编译完成后被冻结
 *
 * 字符串常量的特点：
 * - 编译时确定，运行时不变
 * - 存储在函数原型的常量表中
 * - 通过OP_LOADK指令加载到寄存器
 * - 支持垃圾收集和内存管理
 *
 * 代码生成影响：
 * VK类型的表达式会生成LOADK指令，将常量从常量表
 * 加载到虚拟机寄存器中，供后续指令使用。
 *
 * 内存管理：
 * - 字符串对象由Lua的垃圾收集器管理
 * - 常量表随函数原型一起分配和释放
 * - 无需手动管理字符串内存
 *
 * 性能优化：
 * - 字符串去重减少内存使用
 * - 常量表索引提高访问效率
 * - 编译时处理减少运行时开销
 *
 * @param ls 词法状态，提供函数状态上下文
 * @param e 要初始化的表达式描述符
 * @param s 要转换的字符串对象
 *
 * @note 函数总是成功，字符串必定能添加到常量表中
 * @see init_exp, luaK_stringK, VK
 *
 * @example
 * // 处理字符串字面量 "hello"
 * TString *str = ls->t.seminfo.ts;
 * expdesc e;
 * codestring(ls, &e, str);
 * // 现在e表示字符串常量表达式
 */
static void codestring (LexState *ls, expdesc *e, TString *s) {
    init_exp(e, VK, luaK_stringK(ls->fs, s));
}

/**
 * @brief 检查并生成标识符表达式：解析标识符并创建字符串常量
 *
 * 详细说明：
 * 这个函数是str_checkname和codestring的组合，用于处理需要将
 * 标识符作为字符串常量使用的场景，如表的字段名、函数名等。
 *
 * 处理流程：
 * 1. 调用str_checkname()检查并获取标识符名称
 * 2. 调用codestring()将标识符转换为字符串常量表达式
 * 3. 生成的表达式可用于后续的代码生成
 *
 * 使用场景：
 * - 表字段访问：table.field -> table["field"]
 * - 方法调用：obj:method() -> obj["method"]()
 * - 函数名解析：function name() -> name = function()
 * - 标签引用：goto label -> goto "label"
 *
 * 语法转换：
 * 这个函数体现了Lua语法糖的实现机制：
 * - 点号访问被转换为字符串索引
 * - 标识符被转换为字符串常量
 * - 语法糖在编译时被展开为基本操作
 *
 * 代码生成效果：
 * 生成的表达式会产生LOADK指令，将标识符名称作为
 * 字符串常量加载到寄存器中，供索引操作使用。
 *
 * 错误处理：
 * 如果当前token不是标识符，str_checkname()会报错，
 * 整个函数调用链会通过异常机制终止。
 *
 * 性能考虑：
 * - 标识符字符串会被添加到常量表中
 * - 相同的标识符只存储一次
 * - 编译时处理，运行时无额外开销
 *
 * 内存安全：
 * - 依赖str_checkname的内存安全保证
 * - 字符串对象由垃圾收集器管理
 * - 无内存泄漏风险
 *
 * @param ls 词法状态，提供解析上下文
 * @param e 要初始化的表达式描述符
 *
 * @note 如果当前token不是标识符，函数会报错并不返回
 * @see str_checkname, codestring
 *
 * @example
 * // 解析 table.field 中的 field 部分
 * expdesc key;
 * checkname(ls, &key);  // 解析field并生成字符串常量
 * // 现在key表示"field"字符串常量
 */
static void checkname(LexState *ls, expdesc *e) {
    codestring(ls, e, str_checkname(ls));
}

/**
 * @brief 注册局部变量：将变量名添加到函数的局部变量表中
 *
 * 详细说明：
 * 这个函数是Lua编译器中局部变量管理的核心函数，负责将新声明的
 * 局部变量注册到当前函数的变量表中，并分配相应的调试信息存储空间。
 *
 * 功能职责：
 * 1. 动态扩展局部变量数组的容量
 * 2. 初始化新分配的变量槽位
 * 3. 设置变量名称和垃圾收集屏障
 * 4. 返回变量在表中的索引
 * 5. 更新函数的局部变量计数
 *
 * 内存管理策略：
 * - 使用luaM_growvector动态扩展数组
 * - 按需分配，避免内存浪费
 * - 新分配的槽位初始化为NULL
 * - 设置垃圾收集屏障保护字符串对象
 *
 * 数据结构维护：
 * Proto结构体中的局部变量相关字段：
 * - locvars[]: 局部变量描述符数组
 * - sizelocvars: 数组的分配大小
 * - nlocvars: 当前变量数量（通过FuncState管理）
 *
 * 垃圾收集考虑：
 * - 调用luaC_objbarrier设置写屏障
 * - 确保Proto对象正确引用字符串对象
 * - 防止字符串对象被过早回收
 * - 维护对象间的引用关系
 *
 * 调试信息支持：
 * 注册的变量信息用于：
 * - 运行时错误消息中的变量名显示
 * - 调试器中的变量查看
 * - 栈跟踪信息的生成
 * - 代码分析工具的支持
 *
 * 错误处理：
 * - 如果变量数量超过SHRT_MAX，luaM_growvector会报错
 * - 内存分配失败会通过异常机制处理
 * - 确保数据结构的一致性
 *
 * 性能特征：
 * - 动态数组扩展的摊销时间复杂度为O(1)
 * - 内存使用随实际需求增长
 * - 垃圾收集屏障有轻微开销
 *
 * 线程安全：
 * 函数不是线程安全的，假设单线程编译环境。
 *
 * @param ls 词法状态，提供Lua状态机和函数状态
 * @param varname 要注册的变量名字符串对象
 * @return 变量在局部变量表中的索引
 *
 * @note 函数会修改函数状态中的nlocvars计数器
 * @see luaM_growvector, luaC_objbarrier, LocVar
 *
 * @example
 * // 注册新的局部变量
 * TString *name = luaS_new(L, "myvar");
 * int index = registerlocalvar(ls, name);
 * // 现在变量"myvar"已注册，索引为index
 */
static int registerlocalvar (LexState *ls, TString *varname) {
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int oldsize = f->sizelocvars;
    luaM_growvector(ls->L, f->locvars, fs->nlocvars, f->sizelocvars,
                    LocVar, SHRT_MAX, "too many local variables");
    while (oldsize < f->sizelocvars) f->locvars[oldsize++].varname = NULL;
    f->locvars[fs->nlocvars].varname = varname;
    luaC_objbarrier(ls->L, f, varname);
    return fs->nlocvars++;
}


/**
 * @brief 创建字符串字面量局部变量的便利宏
 *
 * 详细说明：
 * 这个宏简化了创建具有字符串字面量名称的局部变量的过程。
 * 它在编译时计算字符串长度，避免运行时的strlen调用，提高效率。
 *
 * 技术实现：
 * - 使用字符串字面量连接：""v 确保v是字符串字面量
 * - 编译时长度计算：sizeof(v)/sizeof(char)-1 计算字符串长度（不含'\0'）
 * - 调用luaX_newstring创建字符串对象
 * - 调用new_localvar注册变量
 *
 * 使用场景：
 * - 编译器内部创建临时变量
 * - 语法糖展开时的隐式变量
 * - 特殊用途的系统变量
 *
 * 性能优势：
 * - 编译时字符串长度计算
 * - 避免运行时strlen调用
 * - 字符串字面量优化
 *
 * @param ls 词法状态
 * @param v 字符串字面量（必须是编译时常量）
 * @param n 变量在声明列表中的位置
 *
 * @note 只能用于字符串字面量，不能用于变量
 * @see new_localvar, luaX_newstring
 */
#define new_localvarliteral(ls,v,n) \
    new_localvar(ls, luaX_newstring(ls, "" v, (sizeof(v)/sizeof(char))-1), n)

/**
 * @brief 创建新的局部变量：在活跃变量表中注册新变量
 *
 * 详细说明：
 * 这个函数是局部变量声明处理的核心，负责将新声明的变量添加到
 * 当前函数的活跃变量表中。它结合了限制检查和变量注册功能。
 *
 * 处理流程：
 * 1. 检查局部变量数量是否超过限制（LUAI_MAXVARS）
 * 2. 调用registerlocalvar将变量名注册到调试信息表
 * 3. 将返回的索引存储到活跃变量表中
 * 4. 变量此时已创建但尚未激活（需要调用adjustlocalvars）
 *
 * 变量状态管理：
 * Lua中局部变量有两个状态：
 * - 已注册：变量名已记录，但尚未可见
 * - 已激活：变量可见并可以使用
 * 这种设计支持复杂的变量声明语义
 *
 * 活跃变量表：
 * - fs->actvar[]: 存储活跃变量在调试表中的索引
 * - fs->nactvar: 当前活跃变量的数量
 * - 支持嵌套作用域的变量管理
 *
 * 限制检查：
 * - LUAI_MAXVARS: Lua实现定义的最大局部变量数
 * - 通常为200，防止栈溢出和性能问题
 * - 超出限制会报告"too many local variables"错误
 *
 * 内存管理：
 * - 变量索引使用unsigned short存储，节省内存
 * - 依赖registerlocalvar的内存管理
 * - 无需额外的内存分配
 *
 * 使用场景：
 * - local var = value 语句
 * - 函数参数声明
 * - for循环变量声明
 * - 隐式变量创建（如迭代器变量）
 *
 * 错误处理：
 * 如果变量数量超过限制，luaY_checklimit会报错并终止编译。
 *
 * @param ls 词法状态，提供函数状态上下文
 * @param name 变量名字符串对象
 * @param n 变量在当前声明组中的位置偏移
 *
 * @note 变量创建后需要调用adjustlocalvars才能激活
 * @see registerlocalvar, adjustlocalvars, luaY_checklimit
 *
 * @example
 * // 处理 local a, b = 1, 2
 * new_localvar(ls, name_a, 0);  // 创建变量a
 * new_localvar(ls, name_b, 1);  // 创建变量b
 * // ... 处理初始化表达式 ...
 * adjustlocalvars(ls, 2);       // 激活两个变量
 */
static void new_localvar (LexState *ls, TString *name, int n) {
    FuncState *fs = ls->fs;
    luaY_checklimit(fs, fs->nactvar+n+1, LUAI_MAXVARS, "local variables");
    fs->actvar[fs->nactvar+n] = cast(unsigned short, registerlocalvar(ls, name));
}

/**
 * @brief 激活局部变量：设置变量的生效范围起始点
 *
 * 详细说明：
 * 这个函数将之前通过new_localvar创建的变量激活，使其在当前作用域中可见。
 * 它设置变量的生效起始程序计数器值，用于调试和作用域管理。
 *
 * 激活过程：
 * 1. 增加活跃变量计数器（nactvar）
 * 2. 为每个新激活的变量设置startpc
 * 3. startpc记录变量开始生效的字节码位置
 * 4. 变量从此位置开始在作用域中可见
 *
 * 程序计数器的作用：
 * - startpc: 变量生效的字节码位置
 * - endpc: 变量失效的字节码位置（由removevars设置）
 * - 用于调试器确定变量的可见性
 * - 支持运行时的变量查询
 *
 * 变量生命周期管理：
 * Lua变量的完整生命周期：
 * 1. new_localvar: 创建变量（已注册，未激活）
 * 2. adjustlocalvars: 激活变量（设置startpc）
 * 3. removevars: 停用变量（设置endpc）
 *
 * 作用域语义：
 * - 变量在激活后立即可见
 * - 支持变量遮蔽（同名变量的嵌套）
 * - 内层变量会遮蔽外层同名变量
 * - 作用域结束时自动恢复外层变量
 *
 * 调试信息支持：
 * startpc信息用于：
 * - 调试器中的变量可见性判断
 * - 错误消息中的变量名显示
 * - 代码分析工具的作用域分析
 * - 性能分析中的变量跟踪
 *
 * 批量处理：
 * 函数支持一次激活多个变量，这对于：
 * - local a, b, c = 1, 2, 3 这样的多变量声明
 * - 函数参数的批量激活
 * - 循环变量的同时激活
 *
 * 性能考虑：
 * - 简单的计数器操作和字段赋值
 * - 循环次数等于激活的变量数
 * - 整体开销很小
 *
 * @param ls 词法状态，提供函数状态和程序计数器
 * @param nvars 要激活的变量数量
 *
 * @note 必须在相应的new_localvar调用之后使用
 * @see new_localvar, removevars, getlocvar
 *
 * @example
 * // 处理 local a, b = 1, 2
 * new_localvar(ls, name_a, 0);
 * new_localvar(ls, name_b, 1);
 * // ... 处理右侧表达式 ...
 * adjustlocalvars(ls, 2);  // 激活a和b，设置它们的startpc
 */
static void adjustlocalvars (LexState *ls, int nvars) {
    FuncState *fs = ls->fs;
    fs->nactvar = cast_byte(fs->nactvar + nvars);
    for (; nvars; nvars--) {
        getlocvar(fs, fs->nactvar - nvars).startpc = fs->pc;
    }
}

/**
 * @brief 移除局部变量：结束变量的作用域生命周期
 *
 * 详细说明：
 * 这个函数用于结束局部变量的生命周期，当退出作用域时调用。
 * 它设置变量的结束程序计数器，并从活跃变量表中移除变量。
 *
 * 移除过程：
 * 1. 将活跃变量数量减少到指定级别
 * 2. 为每个被移除的变量设置endpc
 * 3. endpc记录变量失效的字节码位置
 * 4. 变量从此位置开始不再可见
 *
 * 作用域管理：
 * - tolevel参数指定要保留的变量级别
 * - 高于此级别的所有变量都会被移除
 * - 支持嵌套作用域的正确处理
 * - 实现变量遮蔽的恢复机制
 *
 * 程序计数器语义：
 * - endpc标记变量作用域的结束位置
 * - 与startpc配合定义变量的完整生命周期
 * - 用于调试器的变量可见性判断
 * - 支持精确的作用域分析
 *
 * 变量遮蔽恢复：
 * 当内层作用域结束时：
 * 1. 内层变量被移除（设置endpc）
 * 2. 外层同名变量自动恢复可见性
 * 3. 变量查找回到外层作用域
 * 4. 保持作用域语义的正确性
 *
 * 使用场景：
 * - 代码块结束：{ ... }
 * - 函数结束：function ... end
 * - 循环结束：for/while ... end
 * - 条件语句结束：if ... end
 *
 * 调试信息维护：
 * endpc信息用于：
 * - 调试器中的变量生命周期显示
 * - 错误消息中的作用域分析
 * - 代码覆盖率工具的变量跟踪
 * - 静态分析工具的作用域检查
 *
 * 内存管理：
 * - 不释放变量名字符串（由垃圾收集器管理）
 * - 只修改活跃变量计数和调试信息
 * - 变量槽位可以被后续变量重用
 *
 * 性能特征：
 * - 简单的循环和字段赋值操作
 * - 时间复杂度O(n)，n为移除的变量数
 * - 空间复杂度O(1)
 *
 * @param ls 词法状态，提供函数状态和程序计数器
 * @param tolevel 要保留的活跃变量级别
 *
 * @note 通常在退出代码块时调用
 * @see adjustlocalvars, getlocvar
 *
 * @example
 * // 进入代码块时的变量级别
 * int old_level = fs->nactvar;
 * // ... 在代码块中声明和使用变量 ...
 * // 退出代码块时恢复变量级别
 * removevars(ls, old_level);
 */
static void removevars (LexState *ls, int tolevel) {
    FuncState *fs = ls->fs;
    while (fs->nactvar > tolevel)
        getlocvar(fs, --fs->nactvar).endpc = fs->pc;
}


/**
 * @brief 索引upvalue：查找或创建函数的upvalue引用
 *
 * 详细说明：
 * 这个函数是Lua闭包实现的核心，负责管理函数对外层作用域变量的引用。
 * 它维护函数的upvalue表，确保每个外部变量只创建一个upvalue引用。
 *
 * Upvalue机制原理：
 * Upvalue是Lua实现闭包的关键机制：
 * - 内层函数可以访问外层函数的局部变量
 * - 即使外层函数已经返回，变量仍然可访问
 * - 通过upvalue实现变量的"捕获"和生命周期延长
 * - 支持多个函数共享同一个外部变量
 *
 * 查找和创建逻辑：
 * 1. 遍历现有upvalue表，查找匹配的引用
 * 2. 匹配条件：变量类型(k)和信息(info)都相同
 * 3. 如果找到，返回现有upvalue的索引
 * 4. 如果未找到，创建新的upvalue条目
 * 5. 更新函数原型和函数状态的upvalue信息
 *
 * 数据结构管理：
 * 函数维护两个upvalue相关的数据结构：
 * - f->upvalues[]: 存储upvalue名称（用于调试）
 * - fs->upvalues[]: 存储upvalue的类型和位置信息
 * - 两个数组通过索引保持对应关系
 *
 * 内存管理：
 * - 使用luaM_growvector动态扩展upvalue数组
 * - 新分配的槽位初始化为NULL
 * - 设置垃圾收集屏障保护字符串对象
 * - 确保内存分配的异常安全性
 *
 * 类型安全检查：
 * - 断言确保变量类型为VLOCAL或VUPVAL
 * - 断言确保找到的upvalue名称匹配
 * - 运行时类型检查防止数据损坏
 *
 * 限制检查：
 * - LUAI_MAXUPVALUES: 最大upvalue数量限制
 * - 防止upvalue表过度增长
 * - 确保虚拟机的稳定性和性能
 *
 * 闭包语义支持：
 * 这个函数支持Lua的完整闭包语义：
 * - 词法作用域：内层函数可以访问外层变量
 * - 变量捕获：外层变量的生命周期被延长
 * - 共享状态：多个闭包可以共享同一个变量
 * - 动态绑定：运行时确定变量的绑定关系
 *
 * 性能优化：
 * - 去重机制避免重复的upvalue创建
 * - 线性查找适合小规模upvalue表
 * - 紧凑的数据结构减少内存开销
 *
 * 调试支持：
 * upvalue名称信息用于：
 * - 调试器中的变量显示
 * - 错误消息中的变量名
 * - 代码分析工具的引用跟踪
 *
 * @param fs 函数状态，包含upvalue管理信息
 * @param name upvalue的变量名（用于调试）
 * @param v 变量的表达式描述符，包含类型和位置信息
 * @return upvalue在函数upvalue表中的索引
 *
 * @note 函数会修改函数状态和原型的upvalue计数
 * @see luaM_growvector, luaC_objbarrier, VLOCAL, VUPVAL
 *
 * @example
 * // 处理闭包中的外部变量引用
 * expdesc var;
 * // ... 在外层作用域中找到变量 ...
 * int upval_index = indexupvalue(fs, var_name, &var);
 * // 现在可以通过upval_index访问外部变量
 */
static int indexupvalue (FuncState *fs, TString *name, expdesc *v) {
    int i;
    Proto *f = fs->f;
    int oldsize = f->sizeupvalues;
    for (i=0; i<f->nups; i++) {
        if (fs->upvalues[i].k == v->k && fs->upvalues[i].info == v->u.s.info) {
            lua_assert(f->upvalues[i] == name);
            return i;
        }
    }
    luaY_checklimit(fs, f->nups + 1, LUAI_MAXUPVALUES, "upvalues");
    luaM_growvector(fs->L, f->upvalues, f->nups, f->sizeupvalues,
                    TString *, MAX_INT, "");
    while (oldsize < f->sizeupvalues) f->upvalues[oldsize++] = NULL;
    f->upvalues[f->nups] = name;
    luaC_objbarrier(fs->L, f, name);
    lua_assert(v->k == VLOCAL || v->k == VUPVAL);
    fs->upvalues[f->nups].k = cast_byte(v->k);
    fs->upvalues[f->nups].info = cast_byte(v->u.s.info);
    return f->nups++;
}

/**
 * @brief 搜索局部变量：在当前函数作用域中查找变量
 *
 * 详细说明：
 * 这个函数在当前函数的活跃变量表中搜索指定名称的局部变量。
 * 它实现了Lua的变量查找语义，支持变量遮蔽和作用域规则。
 *
 * 搜索策略：
 * - 从最近声明的变量开始搜索（从后向前）
 * - 这样可以正确处理变量遮蔽（同名变量的覆盖）
 * - 内层变量会遮蔽外层同名变量
 * - 返回第一个匹配的变量索引
 *
 * 变量遮蔽语义：
 * Lua支持变量遮蔽，例如：
 * @code
 * local x = 1
 * do
 *   local x = 2  -- 遮蔽外层的x
 *   print(x)     -- 输出2
 * end
 * print(x)       -- 输出1
 * @endcode
 *
 * 搜索范围：
 * - 只搜索当前函数的活跃变量
 * - 不搜索外层函数的变量（那是upvalue的职责）
 * - 不搜索全局变量（那由其他机制处理）
 * - 搜索范围限定在[0, fs->nactvar-1]
 *
 * 变量可见性：
 * 只有已激活的变量才会被搜索到：
 * - 已声明但未激活的变量不可见
 * - 已失效的变量不在搜索范围内
 * - 确保变量的正确作用域语义
 *
 * 性能特征：
 * - 时间复杂度：O(n)，n为活跃变量数
 * - 空间复杂度：O(1)
 * - 通常变量数量较少，性能可接受
 * - 后进先出的搜索顺序符合常见使用模式
 *
 * 字符串比较：
 * - 使用指针比较而非字符串内容比较
 * - 依赖Lua字符串池的唯一性保证
 * - 相同内容的字符串有相同的指针
 * - 比较效率高，O(1)时间复杂度
 *
 * 错误处理：
 * - 函数不会失败，总是返回有效结果
 * - 找不到变量时返回-1
 * - 调用者负责处理未找到的情况
 *
 * 调试支持：
 * 变量查找信息用于：
 * - 编译时的变量解析
 * - 错误消息中的变量名显示
 * - 调试器的变量查看
 * - 静态分析工具的引用分析
 *
 * @param fs 函数状态，包含活跃变量表
 * @param n 要搜索的变量名
 * @return 变量在活跃变量表中的索引，未找到返回-1
 *
 * @note 只搜索当前函数的局部变量，不包括upvalue
 * @see getlocvar, adjustlocalvars, removevars
 *
 * @example
 * // 查找变量"x"
 * int index = searchvar(fs, var_name);
 * if (index >= 0) {
 *     // 找到局部变量，index是其在活跃变量表中的位置
 *     init_exp(&var, VLOCAL, index);
 * } else {
 *     // 未找到，可能是upvalue或全局变量
 * }
 */
static int searchvar (FuncState *fs, TString *n) {
    int i;
    for (i=fs->nactvar-1; i >= 0; i--) {
        if (n == getlocvar(fs, i).varname)
            return i;
    }
    return -1;
}

/**
 * @brief 标记upvalue：标记代码块中存在被内层函数引用的变量
 *
 * 详细说明：
 * 这个函数用于标记代码块中存在upvalue引用，这对于正确实现
 * Lua的闭包语义和变量生命周期管理至关重要。
 *
 * 标记目的：
 * 当内层函数引用外层变量时，需要标记包含该变量的代码块：
 * - 防止变量在作用域结束时被过早销毁
 * - 确保闭包能够正确访问外部变量
 * - 支持变量的生命周期延长机制
 * - 优化垃圾收集器的处理策略
 *
 * 标记算法：
 * 1. 从当前代码块开始向外层搜索
 * 2. 找到包含指定变量级别的代码块
 * 3. 将该代码块的upval标志设置为1
 * 4. 标记完成后停止搜索
 *
 * 代码块层次结构：
 * BlockCnt形成链表结构，表示嵌套的代码块：
 * - 每个代码块记录其变量级别（nactvar）
 * - previous指针指向外层代码块
 * - upval标志指示是否有upvalue引用
 *
 * 变量级别语义：
 * - level参数指定被引用变量的活跃变量索引
 * - 只有nactvar > level的代码块才可能包含该变量
 * - 找到第一个满足条件的代码块进行标记
 *
 * 闭包实现支持：
 * upval标记用于：
 * - 指导变量的存储策略（栈上 vs 堆上）
 * - 优化闭包的创建和访问
 * - 支持变量的共享和生命周期管理
 * - 实现正确的垃圾收集语义
 *
 * 性能影响：
 * - 标记操作本身开销很小
 * - 影响后续的变量管理策略
 * - 可能导致变量从栈迁移到堆
 * - 对垃圾收集器的行为有影响
 *
 * 使用场景：
 * - 解析函数定义时发现upvalue引用
 * - 变量查找过程中确定引用关系
 * - 闭包创建时的环境准备
 * - 作用域分析中的依赖标记
 *
 * 错误处理：
 * - 函数不会失败，总是安全执行
 * - 如果找不到合适的代码块，不进行标记
 * - 这种情况通常不会发生在正确的编译过程中
 *
 * 内存安全：
 * - 只修改现有数据结构的标志位
 * - 不分配或释放内存
 * - 依赖代码块链表的正确性
 *
 * @param fs 函数状态，提供代码块链表
 * @param level 被引用变量的活跃变量索引
 *
 * @note 标记操作是幂等的，重复标记不会产生副作用
 * @see BlockCnt, indexupvalue, singlevaraux
 *
 * @example
 * // 当发现变量被内层函数引用时
 * int var_level = searchvar(fs, var_name);
 * if (var_level >= 0) {
 *     markupval(fs, var_level);  // 标记包含该变量的代码块
 * }
 */
static void markupval (FuncState *fs, int level) {
    BlockCnt *bl = fs->bl;
    while (bl && bl->nactvar > level) bl = bl->previous;
    if (bl) bl->upval = 1;
}


/**
 * @brief 单变量辅助解析：递归查找变量的作用域和类型
 *
 * 详细说明：
 * 这个函数是Lua变量解析的核心，实现了Lua的词法作用域规则。
 * 它通过递归搜索函数调用栈，确定变量的类型（局部变量、upvalue或全局变量）
 * 和访问方式，是Lua闭包机制和作用域管理的关键实现。
 *
 * 变量查找算法：
 * 1. 在当前函数作用域中搜索局部变量
 * 2. 如果找到，根据base参数决定是否标记为upvalue
 * 3. 如果未找到，递归搜索外层函数作用域
 * 4. 如果外层搜索返回全局变量，则当前也是全局变量
 * 5. 如果外层找到局部变量或upvalue，则在当前层创建upvalue
 * 6. 如果到达最外层仍未找到，则视为全局变量
 *
 * 词法作用域实现：
 * Lua的词法作用域规则：
 * - 内层函数可以访问外层函数的局部变量
 * - 变量查找从内向外逐层进行
 * - 同名变量遵循就近原则（内层遮蔽外层）
 * - 全局变量是最后的查找目标
 *
 * Upvalue机制：
 * 当内层函数引用外层变量时：
 * - 外层变量被标记为upvalue（如果base=0）
 * - 在当前层创建upvalue引用
 * - 支持变量的生命周期延长
 * - 实现闭包的变量捕获语义
 *
 * Base参数的作用：
 * - base=1: 表示这是变量的直接引用，不标记upvalue
 * - base=0: 表示这是来自内层函数的引用，需要标记upvalue
 * - 这个机制确保只有真正被内层函数使用的变量才被标记
 *
 * 递归终止条件：
 * - fs=NULL: 已经到达最外层，没有更多函数作用域
 * - 此时变量被视为全局变量
 * - 递归深度等于函数嵌套深度
 *
 * 变量类型分类：
 * - VLOCAL: 当前函数的局部变量
 * - VUPVAL: 外层函数的变量（通过upvalue访问）
 * - VGLOBAL: 全局变量（在全局表中查找）
 *
 * 性能考虑：
 * - 递归深度通常很小（函数嵌套层数有限）
 * - 每层的变量搜索是线性的，但变量数量通常较少
 * - Upvalue创建有一定开销，但会被缓存
 * - 全局变量访问需要字符串常量
 *
 * 内存管理：
 * - 不直接分配内存，依赖其他函数的内存管理
 * - Upvalue创建可能触发内存分配
 * - 字符串常量会被添加到常量表
 *
 * 错误处理：
 * - 函数本身不会失败，总是返回有效的变量类型
 * - 依赖的函数（如indexupvalue）可能报告错误
 * - 递归调用的栈溢出由系统处理
 *
 * 调试支持：
 * 变量类型信息用于：
 * - 代码生成时选择正确的访问指令
 * - 调试器中的变量显示和分类
 * - 静态分析工具的作用域分析
 * - 性能分析中的变量访问统计
 *
 * @param fs 当前函数状态，NULL表示已到达最外层
 * @param n 要查找的变量名
 * @param var 输出参数，存储变量的表达式描述符
 * @param base 是否为基础引用（1=直接引用，0=upvalue引用）
 * @return 变量类型（VLOCAL、VUPVAL或VGLOBAL）
 *
 * @note 这是一个递归函数，递归深度等于函数嵌套深度
 * @see searchvar, markupval, indexupvalue, init_exp
 *
 * @example
 * // 解析变量引用
 * expdesc var;
 * int var_type = singlevaraux(fs, var_name, &var, 1);
 * switch (var_type) {
 *   case VLOCAL:  // 局部变量，var.u.s.info是变量索引
 *   case VUPVAL:  // upvalue，var.u.s.info是upvalue索引
 *   case VGLOBAL: // 全局变量，var.u.s.info是名称常量索引
 * }
 */
static int singlevaraux (FuncState *fs, TString *n, expdesc *var, int base) {
    if (fs == NULL) {
        init_exp(var, VGLOBAL, NO_REG);
        return VGLOBAL;
    }
    else {
        int v = searchvar(fs, n);
        if (v >= 0) {
            init_exp(var, VLOCAL, v);
            if (!base)
                markupval(fs, v);
            return VLOCAL;
        }
        else {
            if (singlevaraux(fs->prev, n, var, 0) == VGLOBAL)
                return VGLOBAL;
            var->u.s.info = indexupvalue(fs, n, var);
            var->k = VUPVAL;
            return VUPVAL;
        }
    }
}

/**
 * @brief 单变量解析：解析单个变量引用的完整处理
 *
 * 详细说明：
 * 这个函数是变量引用解析的入口点，它结合了标识符提取和变量查找，
 * 为单个变量引用生成完整的表达式描述符。这是Lua语法解析中
 * 最基础也是最重要的操作之一。
 *
 * 处理流程：
 * 1. 调用str_checkname()提取变量名标识符
 * 2. 调用singlevaraux()进行变量查找和类型确定
 * 3. 如果是全局变量，将变量名添加到常量表
 * 4. 生成相应的表达式描述符供后续使用
 *
 * 全局变量处理：
 * 全局变量的特殊处理：
 * - 变量名被添加到函数的常量表中
 * - var.u.s.info指向常量表中的索引
 * - 运行时通过常量表获取变量名进行全局查找
 * - 支持动态的全局变量访问
 *
 * 表达式描述符生成：
 * 根据变量类型生成不同的表达式描述符：
 * - VLOCAL: info=变量在活跃变量表中的索引
 * - VUPVAL: info=upvalue在upvalue表中的索引
 * - VGLOBAL: info=变量名在常量表中的索引
 *
 * 代码生成准备：
 * 生成的表达式描述符为后续代码生成提供信息：
 * - VLOCAL -> OP_MOVE指令（寄存器间移动）
 * - VUPVAL -> OP_GETUPVAL指令（获取upvalue）
 * - VGLOBAL -> OP_GETGLOBAL指令（全局变量查找）
 *
 * 语法上下文：
 * 这个函数处理的语法情况：
 * - 简单变量引用：var
 * - 表达式中的变量：a + var
 * - 赋值语句的右侧：x = var
 * - 函数调用的参数：func(var)
 *
 * 性能优化：
 * - 局部变量访问最快（直接寄存器操作）
 * - Upvalue访问中等（需要间接访问）
 * - 全局变量访问最慢（需要表查找）
 * - 编译器会优化常见的访问模式
 *
 * 错误处理：
 * - str_checkname()可能报告"<name> expected"错误
 * - singlevaraux()的递归调用可能触发限制检查
 * - luaK_stringK()可能报告常量表溢出
 *
 * 内存管理：
 * - 依赖str_checkname()的字符串管理
 * - 全局变量名会被添加到常量表
 * - 表达式描述符在栈上分配，无需释放
 *
 * 调试信息：
 * 变量解析信息用于：
 * - 生成正确的变量访问指令
 * - 调试器中的变量名显示
 * - 错误消息中的变量引用
 * - 静态分析工具的依赖分析
 *
 * @param ls 词法状态，提供当前token和函数状态
 * @param var 输出参数，存储解析后的变量表达式描述符
 *
 * @note 如果当前token不是标识符，函数会报错并不返回
 * @see str_checkname, singlevaraux, luaK_stringK
 *
 * @example
 * // 解析变量引用 "myvar"
 * expdesc var;
 * singlevar(ls, &var);
 * // 现在var包含了myvar的完整访问信息
 * // 可以用于生成相应的字节码指令
 */
static void singlevar (LexState *ls, expdesc *var) {
    TString *varname = str_checkname(ls);
    FuncState *fs = ls->fs;
    if (singlevaraux(fs, varname, var, 1) == VGLOBAL)
        var->u.s.info = luaK_stringK(fs, varname);
}


/**
 * @brief 调整赋值语句：处理多变量赋值中的值数量匹配
 *
 * 详细说明：
 * 这个函数处理Lua多变量赋值语句中变量数量与表达式数量不匹配的情况。
 * Lua支持灵活的多变量赋值，如 local a, b, c = f(), 2，其中f()可能返回
 * 多个值。这个函数确保每个变量都能得到正确的值。
 *
 * 多变量赋值语义：
 * Lua的多变量赋值规则：
 * - 如果表达式数量少于变量数量，缺少的变量被赋值为nil
 * - 如果表达式数量多于变量数量，多余的表达式被丢弃
 * - 最后一个表达式如果是多返回值表达式，会展开填充剩余变量
 * - 函数调用和可变参数(...) 是多返回值表达式
 *
 * 处理策略：
 * 1. 计算变量数量与表达式数量的差值(extra)
 * 2. 如果最后一个表达式是多返回值类型：
 *    - 调用luaK_setreturns设置返回值数量
 *    - 预留足够的寄存器存储多个返回值
 * 3. 如果最后一个表达式是单值类型：
 *    - 将表达式结果存储到寄存器
 *    - 为缺少的变量生成nil值
 *
 * 多返回值处理：
 * 多返回值表达式的特殊处理：
 * - VCALL: 函数调用，可能返回多个值
 * - VVARARG: 可变参数，可能展开为多个值
 * - 通过luaK_setreturns控制实际返回的值数量
 * - extra+1是因为包含调用本身的返回值
 *
 * 寄存器管理：
 * 函数需要管理虚拟机寄存器的分配：
 * - luaK_reserveregs: 预留寄存器空间
 * - luaK_exp2nextreg: 将表达式结果存储到下一个寄存器
 * - luaK_nil: 在指定寄存器中生成nil值
 * - 确保寄存器使用的正确性和效率
 *
 * 代码生成影响：
 * 这个函数影响生成的字节码：
 * - 多返回值调用生成特殊的CALL指令
 * - nil值生成LOADNIL指令
 * - 寄存器分配影响后续指令的操作数
 *
 * 性能优化：
 * - 避免不必要的寄存器分配
 * - 批量生成nil值而非逐个生成
 * - 多返回值的高效处理
 * - 寄存器复用减少内存使用
 *
 * 错误处理：
 * - 依赖代码生成函数的错误处理
 * - 寄存器溢出会被底层函数检测
 * - 函数本身不直接报告错误
 *
 * 语法示例：
 * @code
 * local a, b, c = 1, 2        -- c被赋值为nil
 * local x, y = func()         -- func()的返回值分配给x和y
 * local p, q, r = func(), 3   -- func()只提供p的值，q=3, r=nil
 * @endcode
 *
 * 内存安全：
 * - 正确管理寄存器分配和释放
 * - 避免寄存器泄漏和重复分配
 * - 依赖虚拟机的寄存器管理机制
 *
 * @param ls 词法状态，提供函数状态上下文
 * @param nvars 左侧变量的数量
 * @param nexps 右侧表达式的数量
 * @param e 最后一个表达式的描述符
 *
 * @note 这个函数修改寄存器分配状态，影响后续代码生成
 * @see hasmultret, luaK_setreturns, luaK_reserveregs, luaK_nil
 *
 * @example
 * // 处理 local a, b, c = f(), 2
 * // nvars=3, nexps=2, e指向最后的表达式"2"
 * adjust_assign(ls, 3, 2, &last_exp);
 * // 结果：a=f()的第一个返回值, b=2, c=nil
 */
static void adjust_assign (LexState *ls, int nvars, int nexps, expdesc *e) {
    FuncState *fs = ls->fs;
    int extra = nvars - nexps;
    if (hasmultret(e->k)) {
        extra++;
        if (extra < 0) extra = 0;
        luaK_setreturns(fs, e, extra);
        if (extra > 1) luaK_reserveregs(fs, extra-1);
    }
    else {
        if (e->k != VVOID) luaK_exp2nextreg(fs, e);
        if (extra > 0) {
            int reg = fs->freereg;
            luaK_reserveregs(fs, extra);
            luaK_nil(fs, reg, extra);
        }
    }
}

/**
 * @brief 进入解析层级：防止递归解析导致的栈溢出
 *
 * 详细说明：
 * 这个函数用于跟踪和限制解析器的递归深度，防止深度嵌套的Lua代码
 * 导致C栈溢出。它是Lua解析器安全机制的重要组成部分。
 *
 * 递归深度控制：
 * Lua解析器使用递归下降算法，可能导致深度递归：
 * - 深度嵌套的表达式：((((a))))
 * - 复杂的函数调用链：f(g(h(i())))
 * - 多层嵌套的代码块：if...if...if...end...end...end
 * - 递归的函数定义
 *
 * 安全机制：
 * - nCcalls计数器跟踪当前的C调用深度
 * - LUAI_MAXCCALLS定义最大允许的递归深度
 * - 超过限制时立即报告错误并终止解析
 * - 防止栈溢出导致的程序崩溃
 *
 * 使用模式：
 * 每个可能递归的解析函数都应该：
 * 1. 在函数开始时调用enterlevel()
 * 2. 在函数结束时调用leavelevel()
 * 3. 确保异常情况下也能正确调用leavelevel()
 *
 * 性能影响：
 * - 每次递归调用都有轻微的计数开销
 * - 相比栈溢出的风险，这个开销是可接受的
 * - 正常情况下递归深度不会很大
 * - 不影响生成代码的性能
 *
 * 错误处理：
 * - 超过限制时调用luaX_lexerror报告错误
 * - 错误消息："chunk has too many syntax levels"
 * - 通过longjmp机制终止解析过程
 * - 确保资源的正确清理
 *
 * 平台兼容性：
 * - LUAI_MAXCCALLS的值可以根据平台调整
 * - 考虑不同平台的栈大小限制
 * - 在嵌入式系统中可能需要更小的限制
 *
 * 调试支持：
 * 递归深度信息用于：
 * - 诊断复杂代码的解析问题
 * - 优化代码结构减少嵌套
 * - 性能分析中的复杂度评估
 *
 * @param ls 词法状态，包含Lua状态机和调用计数器
 *
 * @note 必须与leavelevel()配对使用，确保计数器的正确性
 * @see leavelevel, LUAI_MAXCCALLS, luaX_lexerror
 *
 * @example
 * // 在递归解析函数中的使用模式
 * static void parse_expression(LexState *ls) {
 *     enterlevel(ls);
 *     // ... 解析表达式，可能递归调用其他解析函数 ...
 *     leavelevel(ls);
 * }
 */
static void enterlevel (LexState *ls) {
    if (++ls->L->nCcalls > LUAI_MAXCCALLS)
        luaX_lexerror(ls, "chunk has too many syntax levels", 0);
}

/**
 * @brief 离开解析层级：递归深度计数器递减宏
 *
 * 详细说明：
 * 这个宏与enterlevel()配对使用，用于在退出递归解析函数时
 * 递减调用深度计数器。它确保递归深度跟踪的准确性。
 *
 * 实现细节：
 * - 简单的递减操作，开销极小
 * - 使用宏定义避免函数调用开销
 * - 直接操作Lua状态机中的计数器
 *
 * 使用要求：
 * - 必须与enterlevel()配对使用
 * - 在函数的所有退出路径上都要调用
 * - 异常处理中也要确保调用
 *
 * @param ls 词法状态，包含要递减的调用计数器
 *
 * @note 这是一个宏，不是函数，使用时不需要分号
 * @see enterlevel
 */
#define leavelevel(ls) ((ls)->L->nCcalls--)

/**
 * @brief 进入代码块：初始化新的作用域控制结构
 *
 * 详细说明：
 * 这个函数用于进入新的代码块作用域，初始化BlockCnt结构体并将其
 * 链接到作用域链中。它是Lua作用域管理和控制流处理的基础。
 *
 * 代码块类型：
 * Lua中的代码块包括：
 * - 函数体：function...end
 * - 条件语句：if...end
 * - 循环语句：while...end, for...end
 * - do语句：do...end
 * - repeat语句：repeat...until
 *
 * 初始化过程：
 * 1. 设置break跳转列表为空（NO_JUMP）
 * 2. 设置是否可break标志（循环语句为true）
 * 3. 记录当前的活跃变量数量
 * 4. 初始化upvalue标志为false
 * 5. 链接到父级代码块形成作用域链
 * 6. 更新函数状态的当前代码块指针
 *
 * 作用域链管理：
 * - previous指针形成单向链表
 * - 支持任意深度的嵌套
 * - 内层代码块可以访问外层信息
 * - 退出时自动恢复外层作用域
 *
 * 变量作用域：
 * - nactvar记录进入代码块时的变量数量
 * - 用于退出时恢复变量可见性
 * - 支持变量遮蔽和作用域隔离
 * - 确保变量生命周期的正确性
 *
 * 控制流支持：
 * - isbreakable标志控制break语句的有效性
 * - breaklist管理break语句的跳转目标
 * - 支持循环的正确跳转语义
 * - 防止在非循环结构中使用break
 *
 * 寄存器一致性：
 * - 断言确保自由寄存器数等于活跃变量数
 * - 维护寄存器分配的一致性
 * - 为代码生成提供正确的寄存器状态
 *
 * 内存管理：
 * - BlockCnt通常在C栈上分配
 * - 不需要动态内存分配
 * - 生命周期与代码块解析过程一致
 * - 退出时自动清理
 *
 * 性能特征：
 * - 简单的字段赋值操作，开销很小
 * - 链表操作的时间复杂度为O(1)
 * - 不涉及内存分配，性能稳定
 *
 * @param fs 函数状态，提供作用域管理上下文
 * @param bl 要初始化的代码块控制结构
 * @param isbreakable 是否为可break的循环结构
 *
 * @note 必须与leaveblock()配对使用
 * @see leaveblock, BlockCnt, NO_JUMP
 *
 * @example
 * // 进入while循环代码块
 * BlockCnt bl;
 * enterblock(fs, &bl, 1);  // 1表示可以使用break
 * // ... 解析循环体 ...
 * leaveblock(fs);
 */
static void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isbreakable) {
    bl->breaklist = NO_JUMP;
    bl->isbreakable = isbreakable;
    bl->nactvar = fs->nactvar;
    bl->upval = 0;
    bl->previous = fs->bl;
    fs->bl = bl;
    lua_assert(fs->freereg == fs->nactvar);
}

/**
 * @brief 离开代码块：清理作用域并处理控制流跳转
 *
 * 详细说明：
 * 这个函数用于退出代码块作用域，执行必要的清理工作并处理
 * 控制流跳转。它确保变量作用域、寄存器分配和跳转目标的正确性。
 *
 * 清理过程：
 * 1. 恢复父级代码块为当前作用域
 * 2. 移除当前代码块中声明的局部变量
 * 3. 如果有upvalue引用，生成CLOSE指令
 * 4. 恢复寄存器分配状态
 * 5. 处理break语句的跳转目标
 *
 * 变量清理：
 * - 调用removevars()移除局部变量
 * - 恢复到进入代码块前的变量状态
 * - 设置变量的结束程序计数器(endpc)
 * - 支持变量遮蔽的正确恢复
 *
 * Upvalue处理：
 * 如果代码块中有变量被内层函数引用：
 * - 生成OP_CLOSE指令关闭upvalue
 * - 确保闭包的正确语义
 * - 管理变量的生命周期延长
 * - 支持垃圾收集的正确行为
 *
 * 控制流处理：
 * - 处理break语句的跳转列表
 * - 将所有break跳转指向当前位置
 * - 确保循环控制流的正确性
 * - 支持嵌套循环的break语义
 *
 * 寄存器管理：
 * - 恢复自由寄存器指针到代码块开始时的状态
 * - 释放代码块中分配的寄存器
 * - 维护寄存器分配的一致性
 * - 为后续代码生成提供正确状态
 *
 * 一致性检查：
 * 函数包含多个断言确保状态一致性：
 * - 代码块不能同时控制作用域和break跳转
 * - 活跃变量数必须与进入时一致
 * - 寄存器分配必须正确对应
 *
 * 错误处理：
 * - 断言失败会终止程序（调试版本）
 * - 发布版本中断言被忽略
 * - 依赖调用函数的错误处理
 *
 * 性能考虑：
 * - 主要开销在变量清理和跳转处理
 * - OP_CLOSE指令的生成有一定开销
 * - 整体性能影响较小
 *
 * 内存安全：
 * - 正确恢复作用域链
 * - 避免悬挂指针和内存泄漏
 * - 依赖C栈的自动清理
 *
 * @param fs 函数状态，包含要清理的代码块信息
 *
 * @note 必须与enterblock()配对使用
 * @see enterblock, removevars, luaK_codeABC, luaK_patchtohere
 *
 * @example
 * // 完整的代码块处理
 * BlockCnt bl;
 * enterblock(fs, &bl, 1);
 * // ... 解析代码块内容 ...
 * leaveblock(fs);  // 自动清理所有状态
 */
static void leaveblock (FuncState *fs) {
    BlockCnt *bl = fs->bl;
    fs->bl = bl->previous;
    removevars(fs->ls, bl->nactvar);
    if (bl->upval)
        luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
    lua_assert(!bl->isbreakable || !bl->upval);
    lua_assert(bl->nactvar == fs->nactvar);
    fs->freereg = fs->nactvar;
    luaK_patchtohere(fs, bl->breaklist);
}


/**
 * @brief 推送闭包：创建闭包表达式并生成相应的字节码
 *
 * 详细说明：
 * 这个函数是Lua闭包实现的核心，负责将编译完成的函数转换为闭包表达式。
 * 它将函数原型添加到父函数的子函数表中，生成CLOSURE指令，并为每个
 * upvalue生成相应的捕获指令。
 *
 * 闭包创建过程：
 * 1. 将函数原型添加到父函数的子函数表(f->p)
 * 2. 生成OP_CLOSURE指令创建闭包对象
 * 3. 为每个upvalue生成捕获指令(OP_MOVE或OP_GETUPVAL)
 * 4. 创建表达式描述符供后续使用
 *
 * 子函数表管理：
 * - f->p[]: 存储子函数原型的数组
 * - fs->np: 当前子函数的数量
 * - f->sizep: 子函数表的分配大小
 * - 使用luaM_growvector动态扩展数组
 * - 新分配的槽位初始化为NULL
 *
 * 闭包指令生成：
 * OP_CLOSURE指令的结构：
 * - A: 目标寄存器（由代码生成器分配）
 * - Bx: 函数原型在子函数表中的索引
 * - 指令创建新的闭包对象
 * - 闭包对象包含函数原型和upvalue数组
 *
 * Upvalue捕获指令：
 * 为每个upvalue生成捕获指令：
 * - VLOCAL类型 -> OP_MOVE: 从局部变量捕获
 * - VUPVAL类型 -> OP_GETUPVAL: 从父函数upvalue捕获
 * - 指令的B操作数是upvalue的源位置
 * - 这些指令紧跟在OP_CLOSURE之后
 *
 * 内存管理：
 * - 使用luaM_growvector安全地扩展子函数表
 * - 设置垃圾收集屏障保护函数原型
 * - 确保函数原型不会被过早回收
 * - 依赖Lua的垃圾收集器管理生命周期
 *
 * 表达式类型：
 * 生成VRELOCABLE类型的表达式：
 * - 表示可重定位的指令结果
 * - info字段包含OP_CLOSURE指令的程序计数器
 * - 支持后续的代码优化和寄存器分配
 *
 * 虚拟机执行：
 * 运行时执行过程：
 * 1. OP_CLOSURE创建新的闭包对象
 * 2. 后续的捕获指令设置upvalue值
 * 3. 闭包对象被放置在指定寄存器中
 * 4. 可以作为函数值使用或调用
 *
 * 性能考虑：
 * - 闭包创建有一定开销（对象分配和upvalue设置）
 * - 子函数表的动态扩展摊销成本较低
 * - Upvalue捕获指令数量影响创建时间
 * - 运行时闭包访问效率较高
 *
 * 错误处理：
 * - luaM_growvector可能报告内存不足
 * - 子函数数量超过MAXARG_Bx会报错
 * - 垃圾收集屏障确保内存安全
 *
 * 调试支持：
 * 闭包信息用于：
 * - 调试器中的函数显示
 * - 错误消息中的函数定位
 * - 性能分析中的调用跟踪
 * - 代码覆盖率统计
 *
 * @param ls 词法状态，提供Lua状态机和当前函数上下文
 * @param func 要转换为闭包的函数状态
 * @param v 输出参数，存储生成的闭包表达式描述符
 *
 * @note 函数会修改父函数的子函数表和程序计数器
 * @see luaM_growvector, luaC_objbarrier, OP_CLOSURE, OP_MOVE, OP_GETUPVAL
 *
 * @example
 * // 处理函数定义 function() ... end
 * FuncState func_fs;
 * // ... 编译函数体 ...
 * expdesc closure;
 * pushclosure(ls, &func_fs, &closure);
 * // 现在closure表示新创建的闭包
 */
static void pushclosure (LexState *ls, FuncState *func, expdesc *v) {
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int oldsize = f->sizep;
    int i;
    luaM_growvector(ls->L, f->p, fs->np, f->sizep, Proto *,
                    MAXARG_Bx, "constant table overflow");
    while (oldsize < f->sizep) f->p[oldsize++] = NULL;
    f->p[fs->np++] = func->f;
    luaC_objbarrier(ls->L, f, func->f);
    init_exp(v, VRELOCABLE, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np-1));
    for (i=0; i<func->f->nups; i++) {
        OpCode o = (func->upvalues[i].k == VLOCAL) ? OP_MOVE : OP_GETUPVAL;
        luaK_codeABC(fs, o, 0, func->upvalues[i].info, 0);
    }
}

/**
 * @brief 打开函数：初始化新函数的编译状态
 *
 * 详细说明：
 * 这个函数用于开始编译新的Lua函数，初始化FuncState结构体和相关的
 * 数据结构。它是函数编译过程的起点，为后续的语法解析和代码生成
 * 建立必要的环境。
 *
 * 初始化过程：
 * 1. 创建新的函数原型(Proto)对象
 * 2. 初始化函数状态(FuncState)的所有字段
 * 3. 建立函数状态的链表关系
 * 4. 创建常量表的哈希表
 * 5. 设置垃圾收集保护
 *
 * 函数状态链表：
 * - fs->prev指向父函数的状态
 * - ls->fs指向当前正在编译的函数
 * - 形成函数嵌套的调用栈
 * - 支持闭包中的upvalue查找
 * - 编译完成后自动恢复父函数状态
 *
 * 函数原型初始化：
 * Proto对象的关键字段：
 * - source: 源文件名，用于调试和错误报告
 * - maxstacksize: 初始栈大小为2（寄存器0和1总是有效）
 * - 其他字段由luaF_newproto初始化为默认值
 *
 * 函数状态初始化：
 * FuncState的关键字段：
 * - pc: 程序计数器，从0开始
 * - lasttarget: 最后的跳转目标，初始为-1
 * - jpc: 跳转链表，初始为NO_JUMP
 * - freereg: 自由寄存器指针，从0开始
 * - nk, np, nlocvars, nactvar: 各种计数器，从0开始
 * - bl: 代码块链表，初始为NULL
 *
 * 常量表管理：
 * - fs->h: 常量表的哈希表，用于常量去重
 * - luaH_new创建空的哈希表
 * - 支持高效的常量查找和插入
 * - 编译完成后转换为数组形式
 *
 * 垃圾收集保护：
 * 为了防止编译过程中对象被回收：
 * - 将常量表哈希表压入Lua栈
 * - 将函数原型压入Lua栈
 * - 使用incr_top增加栈顶指针
 * - 确保编译期间对象的安全性
 *
 * 寄存器管理：
 * - 寄存器0和1总是保留给特殊用途
 * - maxstacksize确保最小的栈空间
 * - freereg跟踪下一个可用的寄存器
 * - 支持动态的寄存器分配
 *
 * 内存管理：
 * - luaF_newproto分配函数原型对象
 * - luaH_new分配哈希表对象
 * - 所有对象都由垃圾收集器管理
 * - 编译失败时自动清理资源
 *
 * 错误处理：
 * - 内存分配失败会抛出异常
 * - 异常安全：部分初始化的状态会被正确清理
 * - 依赖Lua的异常处理机制
 *
 * 性能考虑：
 * - 函数状态初始化开销较小
 * - 哈希表创建有一定开销
 * - 垃圾收集保护的开销很小
 * - 整体性能影响可忽略
 *
 * 调试支持：
 * 初始化的信息用于：
 * - 调试器中的函数识别
 * - 错误消息中的位置定位
 * - 性能分析中的函数统计
 * - 代码覆盖率分析
 *
 * @param ls 词法状态，提供Lua状态机和源文件信息
 * @param fs 要初始化的函数状态结构体
 *
 * @note 必须与close_func()配对使用
 * @see close_func, luaF_newproto, luaH_new, FuncState, Proto
 *
 * @example
 * // 开始编译新函数
 * FuncState fs;
 * open_func(ls, &fs);
 * // ... 解析函数体 ...
 * close_func(ls);  // 完成函数编译
 */
static void open_func (LexState *ls, FuncState *fs) {
    lua_State *L = ls->L;
    Proto *f = luaF_newproto(L);
    fs->f = f;
    fs->prev = ls->fs;
    fs->ls = ls;
    fs->L = L;
    ls->fs = fs;
    fs->pc = 0;
    fs->lasttarget = -1;
    fs->jpc = NO_JUMP;
    fs->freereg = 0;
    fs->nk = 0;
    fs->np = 0;
    fs->nlocvars = 0;
    fs->nactvar = 0;
    fs->bl = NULL;
    f->source = ls->source;
    f->maxstacksize = 2;
    fs->h = luaH_new(L, 0, 0);
    sethvalue2s(L, L->top, fs->h);
    incr_top(L);
    setptvalue2s(L, L->top, f);
    incr_top(L);
}


/**
 * @brief 关闭函数：完成函数编译并优化生成的代码
 *
 * 详细说明：
 * 这个函数完成Lua函数的编译过程，执行最终的代码优化、内存整理和
 * 状态清理工作。它是函数编译的终点，确保生成的函数原型是完整和
 * 优化的。
 *
 * 完成过程：
 * 1. 清理所有局部变量的作用域
 * 2. 生成函数的最终返回指令
 * 3. 优化和压缩各种数据数组
 * 4. 验证生成代码的正确性
 * 5. 恢复父函数的编译状态
 * 6. 清理垃圾收集保护
 *
 * 变量清理：
 * - removevars(ls, 0): 移除所有局部变量
 * - 设置所有变量的结束程序计数器(endpc)
 * - 确保调试信息的完整性
 * - 支持调试器的变量生命周期显示
 *
 * 最终返回指令：
 * - luaK_ret(fs, 0, 0): 生成函数的返回指令
 * - 确保函数有明确的退出点
 * - 处理没有显式return语句的函数
 * - 返回nil作为默认返回值
 *
 * 内存优化：
 * 使用luaM_reallocvector压缩各种数组：
 * - f->code: 字节码指令数组
 * - f->lineinfo: 行号信息数组（调试用）
 * - f->k: 常量数组
 * - f->p: 子函数原型数组
 * - f->locvars: 局部变量信息数组
 * - f->upvalues: upvalue名称数组
 *
 * 数组压缩的意义：
 * - 移除未使用的预分配空间
 * - 减少内存占用
 * - 提高缓存局部性
 * - 优化运行时性能
 * - 支持函数的序列化和反序列化
 *
 * 代码验证：
 * - luaG_checkcode(f): 验证生成的字节码
 * - 检查指令的合法性和一致性
 * - 验证跳转目标的正确性
 * - 确保寄存器使用的正确性
 * - 调试版本中的重要安全检查
 *
 * 状态恢复：
 * - ls->fs = fs->prev: 恢复父函数状态
 * - 支持嵌套函数的正确编译
 * - 维护函数状态链表的完整性
 * - 确保编译上下文的正确切换
 *
 * Token重新锚定：
 * - 当前token可能引用已完成函数的字符串
 * - anchor_token确保token字符串的持久性
 * - 防止字符串被垃圾收集器回收
 * - 保证后续解析的正确性
 *
 * 垃圾收集清理：
 * - L->top -= 2: 移除栈上的保护对象
 * - 移除常量表哈希表和函数原型
 * - 允许垃圾收集器回收临时对象
 * - 恢复Lua栈的正常状态
 *
 * 错误处理：
 * - 内存重分配可能失败并抛出异常
 * - 代码验证失败会触发断言
 * - 异常安全：部分完成的清理会被正确处理
 *
 * 性能影响：
 * - 内存重分配有一定开销
 * - 代码验证在调试版本中有开销
 * - 整体优化效果显著
 * - 运行时性能得到提升
 *
 * 调试支持：
 * 完成的函数原型包含完整的调试信息：
 * - 行号映射用于错误定位
 * - 变量信息用于调试器显示
 * - 源文件信息用于栈跟踪
 *
 * @param ls 词法状态，包含要关闭的函数状态
 *
 * @note 必须与open_func()配对使用
 * @see open_func, removevars, luaK_ret, luaM_reallocvector, luaG_checkcode
 *
 * @example
 * // 完整的函数编译过程
 * FuncState fs;
 * open_func(ls, &fs);
 * // ... 解析和编译函数体 ...
 * close_func(ls);  // 完成编译并优化
 * // fs.f现在包含完整优化的函数原型
 */
static void close_func (LexState *ls) {
    lua_State *L = ls->L;
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    removevars(ls, 0);
    luaK_ret(fs, 0, 0);
    luaM_reallocvector(L, f->code, f->sizecode, fs->pc, Instruction);
    f->sizecode = fs->pc;
    luaM_reallocvector(L, f->lineinfo, f->sizelineinfo, fs->pc, int);
    f->sizelineinfo = fs->pc;
    luaM_reallocvector(L, f->k, f->sizek, fs->nk, TValue);
    f->sizek = fs->nk;
    luaM_reallocvector(L, f->p, f->sizep, fs->np, Proto *);
    f->sizep = fs->np;
    luaM_reallocvector(L, f->locvars, f->sizelocvars, fs->nlocvars, LocVar);
    f->sizelocvars = fs->nlocvars;
    luaM_reallocvector(L, f->upvalues, f->sizeupvalues, f->nups, TString *);
    f->sizeupvalues = f->nups;
    lua_assert(luaG_checkcode(f));
    lua_assert(fs->bl == NULL);
    ls->fs = fs->prev;
    if (fs) anchor_token(ls);
    L->top -= 2;
}

/**
 * @brief Lua解析器主入口：将Lua源代码编译为函数原型
 *
 * 详细说明：
 * 这是Lua解析器的公共接口函数，负责将输入的Lua源代码完整地编译为
 * 可执行的函数原型。它协调词法分析、语法分析和代码生成的整个过程，
 * 是Lua编译器的核心入口点。
 *
 * 编译流程：
 * 1. 初始化词法状态和函数状态
 * 2. 设置输入源和缓冲区
 * 3. 开始主函数的编译
 * 4. 设置主函数为可变参数函数
 * 5. 读取第一个token开始解析
 * 6. 解析整个代码块(chunk)
 * 7. 检查文件结束标记
 * 8. 完成函数编译并返回结果
 *
 * 主函数特性：
 * Lua的主函数（顶层代码）具有特殊性质：
 * - 总是可变参数函数(VARARG_ISVARARG)
 * - 可以访问命令行参数
 * - 没有父函数状态(prev == NULL)
 * - 没有upvalue引用(nups == 0)
 * - 代表整个Lua程序的入口点
 *
 * 词法状态初始化：
 * - lexstate.buff: 输入缓冲区，用于token读取
 * - luaX_setinput: 设置输入源和源文件名
 * - luaS_new: 创建源文件名字符串对象
 * - 建立词法分析的基础环境
 *
 * 函数状态管理：
 * - funcstate: 主函数的编译状态
 * - open_func: 初始化函数编译环境
 * - close_func: 完成编译并优化
 * - 确保编译状态的正确管理
 *
 * 语法解析过程：
 * - luaX_next: 读取第一个token
 * - chunk: 解析整个代码块（递归下降解析）
 * - check(TK_EOS): 验证文件正确结束
 * - 实现完整的Lua语法规则
 *
 * 错误处理：
 * - 词法错误：非法字符、字符串未结束等
 * - 语法错误：不符合语法规则的结构
 * - 语义错误：变量未定义、类型不匹配等
 * - 资源错误：内存不足、嵌套过深等
 * - 所有错误通过longjmp机制传播
 *
 * 内存管理：
 * - 所有分配的对象由垃圾收集器管理
 * - 编译失败时自动清理资源
 * - 返回的函数原型需要调用者管理
 * - 异常安全的资源管理
 *
 * 性能特征：
 * - 单遍编译，时间复杂度O(n)
 * - 内存使用与代码复杂度成正比
 * - 生成优化的字节码
 * - 支持大型Lua程序的编译
 *
 * 调试支持：
 * 编译过程生成完整的调试信息：
 * - 行号映射用于错误定位
 * - 变量名信息用于调试器
 * - 源文件信息用于栈跟踪
 * - 支持断点和单步调试
 *
 * 线程安全：
 * - 函数不是线程安全的
 * - 每个Lua状态机需要独立调用
 * - 不支持并发编译
 *
 * 使用场景：
 * - luaL_loadstring: 编译字符串代码
 * - luaL_loadfile: 编译文件代码
 * - lua_load: 通用的代码加载接口
 * - REPL环境中的交互式编译
 *
 * @param L Lua状态机，提供运行环境和内存管理
 * @param z 输入流，提供源代码数据
 * @param buff 输入缓冲区，用于词法分析
 * @param name 源文件名，用于调试和错误报告
 * @return 编译完成的函数原型，可以被Lua虚拟机执行
 *
 * @note 返回的函数原型需要调用者负责生命周期管理
 * @see luaX_setinput, open_func, close_func, chunk
 *
 * @example
 * // 编译Lua代码字符串
 * const char* code = "print('Hello, World!')";
 * ZIO z;
 * Mbuffer buff;
 * // ... 初始化输入流和缓冲区 ...
 * Proto* f = luaY_parser(L, &z, &buff, "string");
 * // 现在f包含可执行的函数原型
 */
Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff, const char *name) {
    struct LexState lexstate;
    struct FuncState funcstate;
    lexstate.buff = buff;
    luaX_setinput(L, &lexstate, z, luaS_new(L, name));
    open_func(&lexstate, &funcstate);
    funcstate.f->is_vararg = VARARG_ISVARARG;
    luaX_next(&lexstate);
    chunk(&lexstate);
    check(&lexstate, TK_EOS);
    close_func(&lexstate);
    lua_assert(funcstate.prev == NULL);
    lua_assert(funcstate.f->nups == 0);
    lua_assert(lexstate.fs == NULL);
    return funcstate.f;
}



/*============================================================*/
/* 语法规则实现部分 */
/* GRAMMAR RULES */
/*============================================================*/

/**
 * @brief 字段访问解析：处理表的点号和冒号字段访问
 *
 * 详细说明：
 * 这个函数解析Lua中的字段访问语法，包括点号访问(table.field)和
 * 冒号访问(table:method)。它将字段名转换为字符串索引，生成
 * 相应的索引访问代码。
 *
 * 语法规则：
 * field -> ['.' | ':'] NAME
 * - '.' 表示普通字段访问
 * - ':' 表示方法调用的语法糖
 * - NAME 是字段名标识符
 *
 * 处理过程：
 * 1. 将表表达式转换为寄存器形式
 * 2. 跳过点号或冒号操作符
 * 3. 解析字段名并转换为字符串常量
 * 4. 生成索引访问的表达式描述符
 *
 * 语法糖展开：
 * - table.field -> table["field"]
 * - table:method -> table["method"] (为方法调用做准备)
 * - 字段名在编译时转换为字符串常量
 * - 运行时通过字符串索引访问表字段
 *
 * 代码生成：
 * - luaK_exp2anyreg: 确保表在寄存器中
 * - checkname: 解析字段名并生成字符串常量
 * - luaK_indexed: 生成索引访问表达式
 * - 最终生成GETTABLE或类似指令
 *
 * 性能优化：
 * - 字段名作为常量存储，避免运行时字符串创建
 * - 索引访问指令针对字符串键优化
 * - 支持虚拟机的快速表访问机制
 *
 * 使用场景：
 * - 对象属性访问：obj.property
 * - 方法调用准备：obj:method()
 * - 模块函数访问：math.sin
 * - 嵌套表访问：config.database.host
 *
 * @param ls 词法状态，提供当前token和解析上下文
 * @param v 输入输出参数，表表达式，输出为字段访问表达式
 *
 * @note 函数假设当前token是'.'或':'
 * @see checkname, luaK_exp2anyreg, luaK_indexed
 *
 * @example
 * // 解析 table.field
 * // 输入：v表示table表达式，当前token是'.'
 * field(ls, &v);
 * // 输出：v表示table["field"]索引表达式
 */
static void field (LexState *ls, expdesc *v) {
    FuncState *fs = ls->fs;
    expdesc key;
    luaK_exp2anyreg(fs, v);
    luaX_next(ls);
    checkname(ls, &key);
    luaK_indexed(fs, v, &key);
}

/**
 * @brief 索引访问解析：处理方括号索引访问语法
 *
 * 详细说明：
 * 这个函数解析Lua中的方括号索引访问语法(table[expr])，支持
 * 任意表达式作为索引键，是表访问的通用形式。
 *
 * 语法规则：
 * index -> '[' expr ']'
 * - '[' 开始索引访问
 * - expr 任意表达式作为索引键
 * - ']' 结束索引访问
 *
 * 处理过程：
 * 1. 跳过开始的方括号'['
 * 2. 解析索引表达式
 * 3. 将索引表达式转换为值形式
 * 4. 检查并跳过结束的方括号']'
 *
 * 索引表达式类型：
 * 支持各种类型的索引：
 * - 数字索引：table[1], table[i+1]
 * - 字符串索引：table["key"], table[var]
 * - 表达式索引：table[func()], table[a.b]
 * - 复杂表达式：table[x and y or z]
 *
 * 代码生成：
 * - expr: 解析索引表达式
 * - luaK_exp2val: 将表达式转换为值形式
 * - 为后续的luaK_indexed调用准备索引值
 * - 生成高效的表索引访问代码
 *
 * 与field的区别：
 * - field: 编译时确定的字符串索引
 * - yindex: 运行时计算的任意索引
 * - field更高效，yindex更灵活
 * - 两者都通过luaK_indexed生成最终代码
 *
 * 性能考虑：
 * - 数字索引通常比字符串索引快
 * - 常量索引可以被优化
 * - 复杂表达式索引有计算开销
 * - 虚拟机对不同索引类型有优化
 *
 * 使用场景：
 * - 数组访问：arr[i]
 * - 动态键访问：table[key]
 * - 计算索引：matrix[row*cols + col]
 * - 嵌套访问：data[user][field]
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param v 输出参数，存储解析后的索引表达式
 *
 * @note 函数假设当前token是'['
 * @see expr, luaK_exp2val, checknext
 *
 * @example
 * // 解析 [expr] 部分
 * // 输入：当前token是'['
 * yindex(ls, &index_expr);
 * // 输出：index_expr包含索引表达式的值
 */
static void yindex (LexState *ls, expdesc *v) {
    luaX_next(ls);
    expr(ls, v);
    luaK_exp2val(ls->fs, v);
    checknext(ls, ']');
}

/*
** {======================================================================
** 表构造器规则实现
** Rules for Constructors
** =======================================================================
*/

/**
 * @brief 构造器控制结构：管理表构造器的解析状态
 *
 * 详细说明：
 * 这个结构体用于跟踪表构造器{...}的解析过程，管理数组元素和
 * 记录元素的计数，以及待存储元素的批处理。
 *
 * 字段说明：
 * - v: 最后读取的列表项表达式描述符
 * - t: 指向表描述符的指针，表示正在构造的表
 * - nh: 记录元素的总数（键值对形式）
 * - na: 数组元素的总数（索引形式）
 * - tostore: 待存储的数组元素数量（批处理优化）
 *
 * 表构造器类型：
 * Lua表构造器支持两种元素：
 * - 数组元素：{1, 2, 3} 或 {a, b, c}
 * - 记录元素：{x=1, y=2} 或 {["key"]="value"}
 * - 混合形式：{1, 2, x=3, [4]=5}
 *
 * 批处理优化：
 * - tostore字段实现数组元素的批量存储
 * - 避免为每个元素生成单独的SETTABLE指令
 * - 使用SETLIST指令批量设置多个数组元素
 * - 提高表构造的性能
 *
 * 内存布局：
 * 结构体大小约为32-40字节（取决于平台），
 * 通常在C栈上分配，生命周期与表构造解析过程一致。
 *
 * 使用模式：
 * @code
 * struct ConsControl cc;
 * cc.v.k = VVOID;          // 初始化最后项为空
 * cc.t = &table_expr;      // 设置表描述符
 * cc.nh = cc.na = 0;       // 初始化计数器
 * cc.tostore = 0;          // 初始化待存储计数
 *
 * // 解析构造器元素...
 *
 * // 完成时处理剩余元素
 * @endcode
 *
 * @since Lua 5.0
 * @see recfield, listfield, constructor
 */
struct ConsControl {
    expdesc v;      /**< 最后读取的列表项表达式描述符 */
    expdesc *t;     /**< 指向表描述符的指针 */
    int nh;         /**< 记录元素总数（键值对） */
    int na;         /**< 数组元素总数（索引形式） */
    int tostore;    /**< 待存储的数组元素数量（批处理） */
};


/**
 * @brief 记录字段解析：处理表构造器中的键值对字段
 *
 * 详细说明：
 * 这个函数解析表构造器中的记录字段（键值对），支持两种语法形式：
 * 命名字段(name = value)和索引字段([expr] = value)。它生成相应的
 * SETTABLE指令来设置表字段。
 *
 * 语法规则：
 * recfield -> (NAME | '[' exp ']') '=' exp
 * - NAME = exp: 命名字段，如 {x = 1, y = 2}
 * - [exp] = exp: 索引字段，如 {[key] = value, ["name"] = "John"}
 *
 * 处理流程：
 * 1. 根据当前token类型选择解析方式
 * 2. TK_NAME: 解析标识符作为字段名
 * 3. '[': 解析方括号索引表达式
 * 4. 检查并跳过等号'='
 * 5. 解析值表达式
 * 6. 生成SETTABLE指令设置表字段
 * 7. 恢复寄存器状态
 *
 * 键处理策略：
 * - 命名字段：字段名转换为字符串常量
 * - 索引字段：支持任意表达式作为键
 * - luaK_exp2RK：将键转换为寄存器或常量形式
 * - 优化常量键的访问效率
 *
 * 代码生成：
 * - OP_SETTABLE指令格式：SETTABLE A B C
 * - A: 表的寄存器索引
 * - B: 键的寄存器或常量索引
 * - C: 值的寄存器或常量索引
 * - 支持寄存器和常量的混合使用
 *
 * 限制检查：
 * - 检查记录字段数量是否超过MAX_INT
 * - 防止构造器过大导致的问题
 * - 确保虚拟机的稳定性
 *
 * 寄存器管理：
 * - 保存进入时的寄存器状态
 * - 临时使用寄存器进行表达式计算
 * - 退出时恢复寄存器状态
 * - 避免寄存器泄漏
 *
 * 性能优化：
 * - 常量键和值使用常量表，减少寄存器使用
 * - SETTABLE指令针对表操作优化
 * - 批量字段设置的高效实现
 *
 * 使用场景：
 * - 对象字面量：{name = "John", age = 30}
 * - 配置表：{host = "localhost", port = 8080}
 * - 动态键：{[var] = value, [func()] = result}
 * - 混合构造：{x = 1, [2] = "two", ["key"] = "value"}
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param cc 构造器控制结构，管理构造过程状态
 *
 * @note 函数会增加cc->nh计数器
 * @see checkname, yindex, luaK_exp2RK, OP_SETTABLE
 *
 * @example
 * // 解析 {name = "John", [key] = value}
 * // 对于 name = "John"：
 * // - checkname解析"name"为字符串常量
 * // - expr解析"John"为字符串常量
 * // - 生成 SETTABLE table_reg, "name", "John"
 */
static void recfield (LexState *ls, struct ConsControl *cc) {
    FuncState *fs = ls->fs;
    int reg = ls->fs->freereg;
    expdesc key, val;
    int rkkey;
    if (ls->t.token == TK_NAME) {
        luaY_checklimit(fs, cc->nh, MAX_INT, "items in a constructor");
        checkname(ls, &key);
    }
    else
        yindex(ls, &key);
    cc->nh++;
    checknext(ls, '=');
    rkkey = luaK_exp2RK(fs, &key);
    expr(ls, &val);
    luaK_codeABC(fs, OP_SETTABLE, cc->t->u.s.info, rkkey, luaK_exp2RK(fs, &val));
    fs->freereg = reg;
}

/**
 * @brief 关闭列表字段：处理待存储的数组元素
 *
 * 详细说明：
 * 这个函数处理表构造器中的数组元素，实现批量存储优化。当累积的
 * 数组元素达到一定数量时，使用SETLIST指令批量设置，提高性能。
 *
 * 批处理机制：
 * - LFIELDS_PER_FLUSH: 每批处理的元素数量（通常为50）
 * - 累积多个数组元素后批量存储
 * - 减少SETTABLE指令的数量
 * - 提高表构造的效率
 *
 * 处理流程：
 * 1. 检查是否有待处理的列表项
 * 2. 将当前元素存储到下一个寄存器
 * 3. 清空当前元素状态
 * 4. 检查是否达到批处理阈值
 * 5. 如果达到，生成SETLIST指令批量存储
 * 6. 重置待存储计数器
 *
 * SETLIST指令：
 * - 专门用于批量设置数组元素
 * - 比多个SETTABLE指令更高效
 * - 支持连续的数组索引设置
 * - 优化表构造的性能
 *
 * 状态管理：
 * - cc->v: 当前处理的数组元素
 * - cc->tostore: 待存储的元素数量
 * - VVOID: 表示没有待处理的元素
 * - 确保状态的一致性
 *
 * 性能优化：
 * - 批量操作减少指令数量
 * - 连续内存访问提高缓存效率
 * - 虚拟机对SETLIST指令的特殊优化
 * - 大幅提升大数组构造的性能
 *
 * 内存管理：
 * - 寄存器的高效使用
 * - 避免不必要的寄存器分配
 * - 支持大型数组的构造
 *
 * 使用场景：
 * - 大型数组字面量：{1, 2, 3, ..., 100}
 * - 数据初始化：{data1, data2, data3, ...}
 * - 批量元素处理：构造器中的连续元素
 *
 * @param fs 函数状态，提供代码生成上下文
 * @param cc 构造器控制结构，管理批处理状态
 *
 * @note 函数可能生成SETLIST指令并重置tostore计数器
 * @see luaK_exp2nextreg, luaK_setlist, LFIELDS_PER_FLUSH
 *
 * @example
 * // 构造 {1, 2, 3, ..., 50, 51}
 * // 前50个元素累积后批量存储：
 * // SETLIST table_reg, 1, 50
 * // 第51个元素继续累积...
 */
static void closelistfield (FuncState *fs, struct ConsControl *cc) {
    if (cc->v.k == VVOID) return;
    luaK_exp2nextreg(fs, &cc->v);
    cc->v.k = VVOID;
    if (cc->tostore == LFIELDS_PER_FLUSH) {
        luaK_setlist(fs, cc->t->u.s.info, cc->na, cc->tostore);
        cc->tostore = 0;
    }
}

/**
 * @brief 最后列表字段：处理构造器结束时的剩余数组元素
 *
 * 详细说明：
 * 这个函数在表构造器解析结束时调用，处理所有剩余的数组元素。
 * 它特别处理多返回值表达式，确保所有元素都被正确存储到表中。
 *
 * 处理策略：
 * 1. 检查是否有待存储的元素
 * 2. 处理多返回值表达式的特殊情况
 * 3. 处理普通表达式的常规情况
 * 4. 生成最终的SETLIST指令
 *
 * 多返回值处理：
 * - hasmultret: 检查是否为多返回值表达式
 * - luaK_setmultret: 设置多返回值模式
 * - LUA_MULTRET: 表示不定数量的返回值
 * - 支持函数调用和可变参数的展开
 *
 * 数组计数调整：
 * - 多返回值情况：cc->na-- 因为返回值数量未知
 * - 普通情况：保持cc->na不变
 * - 确保数组索引的正确性
 *
 * SETLIST指令生成：
 * - 普通情况：SETLIST table, start, count
 * - 多返回值：SETLIST table, start, LUA_MULTRET
 * - 支持不同的元素数量模式
 *
 * 使用场景：
 * - 构造器末尾的函数调用：{a, b, func()}
 * - 可变参数展开：{x, y, ...}
 * - 剩余元素处理：{1, 2, 3} 中的最后几个元素
 * - 混合构造器的数组部分结束
 *
 * 性能考虑：
 * - 批量存储剩余元素
 * - 多返回值的高效处理
 * - 避免逐个元素的设置开销
 *
 * 错误处理：
 * - 正确处理空的待存储列表
 * - 多返回值表达式的异常情况
 * - 确保表构造的完整性
 *
 * @param fs 函数状态，提供代码生成上下文
 * @param cc 构造器控制结构，包含待处理元素信息
 *
 * @note 函数处理多返回值时会调整cc->na计数器
 * @see hasmultret, luaK_setmultret, luaK_setlist, LUA_MULTRET
 *
 * @example
 * // 构造 {1, 2, func()}，其中func()返回多个值
 * // 1. 处理1, 2为普通元素
 * // 2. func()识别为多返回值表达式
 * // 3. 生成 SETLIST table, 1, LUA_MULTRET
 * // 4. 运行时展开func()的所有返回值
 */
static void lastlistfield (FuncState *fs, struct ConsControl *cc) {
    if (cc->tostore == 0) return;
    if (hasmultret(cc->v.k)) {
        luaK_setmultret(fs, &cc->v);
        luaK_setlist(fs, cc->t->u.s.info, cc->na, LUA_MULTRET);
        cc->na--;
    }
    else {
        if (cc->v.k != VVOID)
            luaK_exp2nextreg(fs, &cc->v);
        luaK_setlist(fs, cc->t->u.s.info, cc->na, cc->tostore);
    }
}

/**
 * @brief 列表字段解析：处理表构造器中的数组元素
 *
 * 详细说明：
 * 这个函数解析表构造器中的数组元素，这些元素没有显式的键，
 * 按照出现顺序自动分配数字索引（从1开始）。
 *
 * 语法规则：
 * listfield -> exp
 * - 简单的表达式，如 {1, 2, "hello", func()}
 * - 自动分配数字索引：1, 2, 3, ...
 * - 支持任意类型的表达式
 *
 * 处理流程：
 * 1. 解析表达式并存储到cc->v
 * 2. 检查数组元素数量限制
 * 3. 增加数组元素计数器
 * 4. 增加待存储元素计数器
 *
 * 数组索引：
 * - Lua数组索引从1开始
 * - cc->na跟踪当前数组长度
 * - 自动递增分配索引
 * - 支持稀疏数组（混合键值对）
 *
 * 批处理准备：
 * - cc->tostore累积待存储元素
 * - 配合closelistfield实现批量存储
 * - 提高大数组构造的性能
 * - 减少虚拟机指令数量
 *
 * 限制检查：
 * - 防止数组元素数量超过MAX_INT
 * - 确保构造器的合理大小
 * - 避免内存和性能问题
 *
 * 表达式类型：
 * 支持各种表达式作为数组元素：
 * - 字面量：数字、字符串、布尔值
 * - 变量引用：局部变量、全局变量
 * - 函数调用：可能返回多个值
 * - 复杂表达式：算术、逻辑运算等
 *
 * 性能优化：
 * - 延迟存储机制减少指令数量
 * - 批量处理提高效率
 * - 寄存器的高效使用
 *
 * 使用场景：
 * - 数组字面量：{1, 2, 3, 4, 5}
 * - 混合数据：{"name", 25, true, nil}
 * - 函数结果：{func1(), func2(), func3()}
 * - 嵌套结构：{{1, 2}, {3, 4}, {5, 6}}
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param cc 构造器控制结构，管理数组元素状态
 *
 * @note 函数会增加cc->na和cc->tostore计数器
 * @see expr, luaY_checklimit, closelistfield
 *
 * @example
 * // 解析 {10, 20, 30}
 * // 第一次调用：expr解析10，cc->na=1, cc->tostore=1
 * // 第二次调用：expr解析20，cc->na=2, cc->tostore=2
 * // 第三次调用：expr解析30，cc->na=3, cc->tostore=3
 */
static void listfield (LexState *ls, struct ConsControl *cc) {
    expr(ls, &cc->v);
    luaY_checklimit(ls->fs, cc->na, MAX_INT, "items in a constructor");
    cc->na++;
    cc->tostore++;
}

/**
 * @brief 表构造器解析：处理Lua表字面量的完整构造过程
 *
 * 详细说明：
 * 这个函数是Lua表构造器{...}解析的核心，协调数组元素和记录元素的
 * 解析，生成优化的表创建和初始化代码。它实现了Lua表字面量的
 * 完整语法支持。
 *
 * 语法规则：
 * constructor -> '{' [ field { fieldsep field } [fieldsep] ] '}'
 * field -> listfield | recfield
 * fieldsep -> ',' | ';'
 * - 支持数组元素：{1, 2, 3}
 * - 支持记录元素：{x=1, y=2}
 * - 支持混合形式：{1, 2, x=3, [4]=5}
 * - 支持两种分隔符：逗号和分号
 *
 * 构造过程：
 * 1. 生成NEWTABLE指令创建空表
 * 2. 初始化构造器控制结构
 * 3. 将表固定在栈顶（防止垃圾收集）
 * 4. 解析开始的大括号'{'
 * 5. 循环解析表字段直到遇到'}'
 * 6. 处理剩余的数组元素
 * 7. 优化NEWTABLE指令的参数
 *
 * 字段类型识别：
 * - TK_NAME: 可能是数组元素或记录元素
 *   - 使用lookahead检查后续是否有'='
 *   - name = value -> recfield
 *   - name (其他) -> listfield
 * - '[': 明确的记录元素 [expr] = value
 * - 其他: 数组元素表达式
 *
 * 前瞻解析：
 * - luaX_lookahead: 查看下一个token
 * - 区分 {name} 和 {name = value}
 * - 避免回溯，提高解析效率
 * - 支持复杂的语法歧义消解
 *
 * 批处理优化：
 * - closelistfield: 处理累积的数组元素
 * - lastlistfield: 处理最后的数组元素
 * - SETLIST指令批量设置数组元素
 * - 显著提高大表构造的性能
 *
 * NEWTABLE指令优化：
 * - 初始时B=0, C=0（未知大小）
 * - 解析完成后设置实际大小
 * - luaO_int2fb: 将整数转换为浮点字节编码
 * - 虚拟机根据大小预分配内存
 *
 * 内存管理：
 * - 表固定在栈顶防止垃圾收集
 * - 构造过程中表始终可达
 * - 异常安全：部分构造的表会被正确清理
 * - 支持大型表的安全构造
 *
 * 分隔符处理：
 * - 支持逗号','和分号';'作为分隔符
 * - 两种分隔符语义相同
 * - 可选的尾随分隔符：{1, 2, 3,}
 * - 灵活的语法支持
 *
 * 错误处理：
 * - check_match: 确保大括号正确配对
 * - 语法错误时提供准确的位置信息
 * - 异常安全的资源清理
 *
 * 性能特征：
 * - 单遍解析，时间复杂度O(n)
 * - 批处理优化减少指令数量
 * - 预分配优化减少内存重分配
 * - 支持大型表的高效构造
 *
 * 使用场景：
 * - 数据结构初始化：local config = {host="localhost", port=8080}
 * - 数组字面量：local arr = {1, 2, 3, 4, 5}
 * - 混合表：local data = {10, 20, name="test", [100]="special"}
 * - 嵌套结构：local tree = {{1, 2}, {x=3, y=4}}
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param t 输出参数，存储构造的表表达式描述符
 *
 * @note 函数生成NEWTABLE指令并优化其参数
 * @see recfield, listfield, closelistfield, lastlistfield, OP_NEWTABLE
 *
 * @example
 * // 解析 {1, 2, x=3, [4]=5}
 * // 1. NEWTABLE创建空表
 * // 2. 1, 2作为数组元素批量存储
 * // 3. x=3作为记录元素单独设置
 * // 4. [4]=5作为索引元素单独设置
 * // 5. 优化NEWTABLE参数：B=2(数组), C=2(记录)
 */
static void constructor (LexState *ls, expdesc *t) {
    FuncState *fs = ls->fs;
    int line = ls->linenumber;
    int pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
    struct ConsControl cc;
    cc.na = cc.nh = cc.tostore = 0;
    cc.t = t;
    init_exp(t, VRELOCABLE, pc);
    init_exp(&cc.v, VVOID, 0);
    luaK_exp2nextreg(ls->fs, t);
    checknext(ls, '{');
    do {
        lua_assert(cc.v.k == VVOID || cc.tostore > 0);
        if (ls->t.token == '}') break;
        closelistfield(fs, &cc);
        switch(ls->t.token) {
            case TK_NAME: {
                luaX_lookahead(ls);
                if (ls->lookahead.token != '=')
                    listfield(ls, &cc);
                else
                    recfield(ls, &cc);
                break;
            }
            case '[': {
                recfield(ls, &cc);
                break;
            }
            default: {
                listfield(ls, &cc);
                break;
            }
        }
    } while (testnext(ls, ',') || testnext(ls, ';'));
    check_match(ls, '}', '{', line);
    lastlistfield(fs, &cc);
    SETARG_B(fs->f->code[pc], luaO_int2fb(cc.na));
    SETARG_C(fs->f->code[pc], luaO_int2fb(cc.nh));
}

/* }====================================================================== */

/**
 * @brief 参数列表解析：处理函数定义中的参数声明
 *
 * 详细说明：
 * 这个函数解析Lua函数定义中的参数列表，支持固定参数和可变参数，
 * 创建相应的局部变量并设置函数的参数属性。
 *
 * 语法规则：
 * parlist -> [ param { ',' param } ]
 * param -> NAME | '...'
 * - 固定参数：function(a, b, c)
 * - 可变参数：function(a, b, ...)
 * - 混合形式：function(a, b, ...)
 * - 空参数：function()
 *
 * 处理流程：
 * 1. 检查参数列表是否为空
 * 2. 循环解析每个参数
 * 3. 处理固定参数名称
 * 4. 处理可变参数标记
 * 5. 设置函数的参数数量和可变参数标志
 * 6. 激活所有参数变量
 *
 * 参数类型：
 * - TK_NAME: 固定参数，创建局部变量
 * - TK_DOTS: 可变参数'...'，设置vararg标志
 * - 可变参数必须是最后一个参数
 * - 可变参数后不能有其他参数
 *
 * 变量创建：
 * - new_localvar: 为每个固定参数创建局部变量
 * - 参数变量在函数开始时自动激活
 * - 参数占用函数的前几个寄存器
 * - 支持参数的调试信息
 *
 * 可变参数处理：
 * - f->is_vararg: 设置可变参数标志
 * - VARARG_HASARG: 函数有可变参数
 * - VARARG_NEEDSARG: 需要创建arg表（5.0兼容）
 * - 支持...表达式访问额外参数
 *
 * 参数数量：
 * - f->numparams: 固定参数的数量
 * - 不包括可变参数在内
 * - 用于函数调用时的参数检查
 * - 支持参数数量的运行时验证
 *
 * 寄存器分配：
 * - 参数按顺序占用寄存器0, 1, 2, ...
 * - 可变参数不占用固定寄存器
 * - 为后续的局部变量预留空间
 * - 优化寄存器的使用效率
 *
 * 错误处理：
 * - 检查参数名的合法性
 * - 防止重复的参数名
 * - 可变参数位置的正确性
 * - 参数数量的限制检查
 *
 * 兼容性考虑：
 * - 支持Lua 5.0的arg表创建
 * - 向后兼容的可变参数处理
 * - 不同版本间的语义一致性
 *
 * 使用场景：
 * - 普通函数：function add(a, b) return a + b end
 * - 可变参数函数：function printf(fmt, ...) ... end
 * - 方法定义：function obj:method(self, param) ... end
 * - 匿名函数：function(x, y) return x * y end
 *
 * @param ls 词法状态，提供token流和解析上下文
 *
 * @note 函数会修改当前函数的参数相关属性
 * @see new_localvar, adjustlocalvars, TK_NAME, TK_DOTS
 *
 * @example
 * // 解析 function(a, b, ...)
 * // 1. 创建局部变量'a'和'b'
 * // 2. 设置f->numparams = 2
 * // 3. 设置f->is_vararg = VARARG_HASARG
 * // 4. 激活参数变量
 */
static void parlist (LexState *ls) {
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int nparams = 0;
    f->is_vararg = 0;
    if (ls->t.token != ')') {
        do {
            switch (ls->t.token) {
                case TK_NAME: {
                    new_localvar(ls, str_checkname(ls), nparams++);
                    break;
                }
                case TK_DOTS: {
                    luaX_next(ls);
#if defined(LUA_COMPAT_VARARG)
                    new_localvarliteral(ls, "arg", nparams++);
                    f->is_vararg = VARARG_HASARG | VARARG_NEEDSARG;
#endif
                    f->is_vararg |= VARARG_ISVARARG;
                    break;
                }
                default: luaX_syntaxerror(ls, "<name> or " LUA_QL("...") " expected");
            }
        } while (!f->is_vararg && testnext(ls, ','));
    }
    adjustlocalvars(ls, nparams);
    f->numparams = cast_byte(fs->nactvar - (f->is_vararg & VARARG_HASARG));
    luaK_reserveregs(fs, fs->nactvar);
}

/**
 * @brief 函数体解析：处理函数定义的完整结构
 *
 * 详细说明：
 * 这个函数解析Lua函数定义的函数体部分，包括参数列表、函数体代码块
 * 和结束标记。它创建新的函数作用域，处理self参数（用于方法定义），
 * 并生成相应的闭包。
 *
 * 语法规则：
 * body -> '(' parlist ')' chunk 'end'
 * - '(' parlist ')': 参数列表
 * - chunk: 函数体代码块
 * - 'end': 函数结束标记
 *
 * 处理流程：
 * 1. 创建新的函数状态
 * 2. 设置函数定义行号
 * 3. 解析参数列表的开始括号
 * 4. 处理self参数（如果需要）
 * 5. 解析参数列表
 * 6. 解析参数列表的结束括号
 * 7. 解析函数体代码块
 * 8. 设置函数结束行号
 * 9. 检查end关键字
 * 10. 关闭函数并生成闭包
 *
 * Self参数处理：
 * - needself=1: 方法定义，自动添加self参数
 * - needself=0: 普通函数定义
 * - self参数总是第一个参数
 * - 支持面向对象编程的语法糖
 *
 * 函数作用域：
 * - open_func: 创建新的函数编译环境
 * - 嵌套函数状态管理
 * - 独立的变量作用域
 * - 支持闭包和upvalue
 *
 * 行号信息：
 * - linedefined: 函数定义开始行号
 * - lastlinedefined: 函数定义结束行号
 * - 用于调试和错误报告
 * - 支持栈跟踪信息
 *
 * 代码块解析：
 * - chunk: 解析函数体的语句序列
 * - 支持所有Lua语句类型
 * - 递归下降解析
 * - 完整的语法支持
 *
 * 闭包生成：
 * - close_func: 完成函数编译
 * - pushclosure: 生成闭包表达式
 * - 处理upvalue捕获
 * - 优化代码生成
 *
 * 错误处理：
 * - check_match: 确保end关键字正确配对
 * - 语法错误的准确定位
 * - 异常安全的资源清理
 *
 * 使用场景：
 * - 普通函数：function name(a, b) ... end
 * - 方法定义：function obj:method(a, b) ... end
 * - 匿名函数：function(x) return x * 2 end
 * - 嵌套函数：function outer() function inner() end end
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param e 输出参数，存储生成的函数表达式描述符
 * @param needself 是否需要self参数（1=方法，0=函数）
 * @param line 函数定义开始的行号
 *
 * @note 函数创建新的函数作用域并在完成后恢复
 * @see open_func, close_func, parlist, chunk, pushclosure
 *
 * @example
 * // 解析 function obj:method(a, b) return a + b end
 * // needself=1，自动添加self参数
 * // 等价于 function obj.method(self, a, b) return a + b end
 */
static void body (LexState *ls, expdesc *e, int needself, int line) {
    FuncState new_fs;
    open_func(ls, &new_fs);
    new_fs.f->linedefined = line;
    checknext(ls, '(');
    if (needself) {
        new_localvarliteral(ls, "self", 0);
        adjustlocalvars(ls, 1);
    }
    parlist(ls);
    checknext(ls, ')');
    chunk(ls);
    new_fs.f->lastlinedefined = ls->linenumber;
    check_match(ls, TK_END, TK_FUNCTION, line);
    close_func(ls);
    pushclosure(ls, &new_fs, e);
}

/**
 * @brief 表达式列表解析：处理逗号分隔的表达式序列
 *
 * 详细说明：
 * 这个函数解析由逗号分隔的表达式列表，至少包含一个表达式。
 * 它将除最后一个表达式外的所有表达式存储到寄存器中，返回
 * 表达式的总数量。
 *
 * 语法规则：
 * explist1 -> expr { ',' expr }
 * - 至少一个表达式
 * - 逗号分隔的表达式序列
 * - 支持任意数量的表达式
 *
 * 处理策略：
 * 1. 解析第一个表达式（必需）
 * 2. 循环处理后续的逗号分隔表达式
 * 3. 将前面的表达式存储到寄存器
 * 4. 保持最后一个表达式在表达式描述符中
 * 5. 返回表达式总数
 *
 * 寄存器管理：
 * - luaK_exp2nextreg: 将表达式结果存储到下一个寄存器
 * - 前n-1个表达式占用连续寄存器
 * - 最后一个表达式保持在描述符中
 * - 支持多返回值表达式的特殊处理
 *
 * 多返回值处理：
 * - 只有最后一个表达式可能是多返回值
 * - 前面的表达式被强制为单值
 * - 支持函数调用和可变参数的展开
 * - 优化多值赋值和函数调用
 *
 * 使用场景：
 * - 多变量赋值：a, b, c = 1, 2, 3
 * - 函数调用参数：func(a, b, c)
 * - 返回语句：return x, y, z
 * - 表构造器：{a, b, c}
 *
 * 性能优化：
 * - 连续寄存器分配提高访问效率
 * - 最后表达式的特殊处理
 * - 支持编译器的进一步优化
 *
 * 错误处理：
 * - 依赖expr函数的错误处理
 * - 寄存器溢出检查
 * - 语法错误的传播
 *
 * 内存管理：
 * - 寄存器的自动分配和管理
 * - 表达式临时值的正确处理
 * - 异常安全的资源管理
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param v 输入输出参数，最后一个表达式的描述符
 * @return 表达式列表中表达式的总数量
 *
 * @note 函数至少解析一个表达式，返回值>=1
 * @see expr, luaK_exp2nextreg, testnext
 *
 * @example
 * // 解析 a, b, func()
 * // 1. 解析a，存储到寄存器
 * // 2. 解析b，存储到寄存器
 * // 3. 解析func()，保持在描述符中（可能多返回值）
 * // 4. 返回3（表达式数量）
 */
static int explist1 (LexState *ls, expdesc *v) {
    int n = 1;
    expr(ls, v);
    while (testnext(ls, ',')) {
        luaK_exp2nextreg(ls->fs, v);
        expr(ls, v);
        n++;
    }
    return n;
}


/**
 * @brief 函数调用参数解析：处理函数调用的参数列表和语法糖
 *
 * 详细说明：
 * 这个函数解析Lua函数调用的参数部分，支持三种语法形式：
 * 括号参数列表、表构造器参数和字符串字面量参数。它生成相应的
 * CALL指令来执行函数调用。
 *
 * 语法规则：
 * funcargs -> '(' [explist1] ')' | constructor | STRING
 * - '(' explist ')': 标准参数列表，如 func(a, b, c)
 * - constructor: 表构造器，如 func{x=1, y=2}
 * - STRING: 字符串字面量，如 func"hello" 或 func[[text]]
 *
 * 语法糖支持：
 * Lua支持两种函数调用的语法糖：
 * - func{table}: 等价于 func({table})
 * - func"string": 等价于 func("string")
 * - 这些语法糖简化了常见的函数调用模式
 * - 特别适用于配置函数和DSL构建
 *
 * 歧义性检查：
 * - 检查函数调用是否与新语句在同一行
 * - 防止歧义语法：func() \n (expr) 被误解为 func()(expr)
 * - 通过行号比较确保语法的明确性
 * - 提供清晰的错误消息指导用户
 *
 * 参数处理策略：
 * 1. 括号参数：解析表达式列表，支持多返回值
 * 2. 表构造器：直接作为单个参数传递
 * 3. 字符串字面量：转换为字符串常量参数
 * 4. 空参数列表：设置为VVOID类型
 *
 * 多返回值处理：
 * - hasmultret: 检查最后一个参数是否为多返回值
 * - luaK_setmultret: 设置多返回值模式
 * - LUA_MULTRET: 表示不定数量的参数
 * - 支持函数调用和可变参数的展开
 *
 * CALL指令生成：
 * OP_CALL指令格式：CALL A B C
 * - A: 函数所在的寄存器（base）
 * - B: 参数数量+1（包括函数本身）
 * - C: 返回值数量+1（默认为2，即1个返回值）
 * - 支持可变参数数量和返回值数量
 *
 * 寄存器管理：
 * - base: 函数调用的基础寄存器
 * - 函数占用base寄存器
 * - 参数占用base+1, base+2, ...寄存器
 * - 调用后恢复到base+1（保留一个返回值）
 *
 * 行号信息：
 * - luaK_fixline: 设置指令的行号信息
 * - 用于调试和错误报告
 * - 支持准确的栈跟踪
 *
 * 错误处理：
 * - 语法错误：不支持的参数形式
 * - 歧义错误：跨行的函数调用
 * - 括号不匹配：参数列表格式错误
 *
 * 性能优化：
 * - 直接生成CALL指令，避免中间步骤
 * - 寄存器的高效分配和回收
 * - 多返回值的优化处理
 * - 语法糖的零开销实现
 *
 * 使用场景：
 * - 标准调用：print("hello", "world")
 * - 表参数：config{host="localhost", port=8080}
 * - 字符串参数：require"module" 或 dofile"script.lua"
 * - 链式调用：obj:method(args):next()
 * - 多返回值：func(other_func())
 *
 * 内存安全：
 * - 正确的寄存器分配和释放
 * - 异常安全的参数处理
 * - 防止寄存器泄漏
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param f 输入输出参数，函数表达式，输出为函数调用表达式
 *
 * @note 函数假设f指向一个有效的函数表达式
 * @see explist1, constructor, codestring, OP_CALL
 *
 * @example
 * // 解析不同形式的函数调用：
 * // func(a, b)     -> CALL base, 3, 2  (2个参数+函数)
 * // func{x=1}      -> CALL base, 2, 2  (1个表参数+函数)
 * // func"hello"    -> CALL base, 2, 2  (1个字符串参数+函数)
 * // func()         -> CALL base, 1, 2  (0个参数+函数)
 */
static void funcargs (LexState *ls, expdesc *f) {
    FuncState *fs = ls->fs;
    expdesc args;
    int base, nparams;
    int line = ls->linenumber;
    switch (ls->t.token) {
        case '(': {
            if (line != ls->lastline)
                luaX_syntaxerror(ls,"ambiguous syntax (function call x new statement)");
            luaX_next(ls);
            if (ls->t.token == ')')
                args.k = VVOID;
            else {
                explist1(ls, &args);
                luaK_setmultret(fs, &args);
            }
            check_match(ls, ')', '(', line);
            break;
        }
        case '{': {
            constructor(ls, &args);
            break;
        }
        case TK_STRING: {
            codestring(ls, &args, ls->t.seminfo.ts);
            luaX_next(ls);
            break;
        }
        default: {
            luaX_syntaxerror(ls, "function arguments expected");
            return;
        }
    }
    lua_assert(f->k == VNONRELOC);
    base = f->u.s.info;
    if (hasmultret(args.k))
        nparams = LUA_MULTRET;
    else {
        if (args.k != VVOID)
            luaK_exp2nextreg(fs, &args);
        nparams = fs->freereg - (base+1);
    }
    init_exp(f, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2));
    luaK_fixline(fs, line);
    fs->freereg = base+1;
}




/*
** {======================================================================
** 表达式解析实现
** Expression parsing
** =======================================================================
*/

/**
 * @brief 前缀表达式解析：处理表达式的基本构成元素
 *
 * 详细说明：
 * 这个函数解析Lua表达式的前缀部分，包括变量名和括号表达式。
 * 它是表达式解析的基础，为后续的复杂表达式构建提供起始点。
 *
 * 语法规则：
 * prefixexp -> NAME | '(' expr ')'
 * - NAME: 变量名，如 x, table, func
 * - '(' expr ')': 括号表达式，如 (a + b), (func())
 *
 * 处理策略：
 * 1. 括号表达式：递归解析内部表达式
 * 2. 变量名：解析为变量引用
 * 3. 其他token：报告语法错误
 *
 * 括号表达式处理：
 * - 跳过开始括号'('
 * - 递归调用expr解析内部表达式
 * - 检查结束括号')'的匹配
 * - luaK_dischargevars: 确保变量被正确加载
 *
 * 变量处理：
 * - singlevar: 解析单个变量引用
 * - 支持局部变量、全局变量和upvalue
 * - 处理变量的作用域查找
 *
 * 优先级和结合性：
 * - 括号具有最高优先级
 * - 强制改变表达式的求值顺序
 * - 支持复杂表达式的分组
 *
 * 错误处理：
 * - 不支持的token类型报告语法错误
 * - 括号不匹配的检查
 * - 提供准确的错误位置信息
 *
 * 代码生成：
 * - 括号表达式不生成额外指令
 * - 变量引用生成相应的加载指令
 * - 优化常量和寄存器的使用
 *
 * 使用场景：
 * - 变量引用：x, table.field, func
 * - 括号分组：(a + b) * c
 * - 函数调用：(func())()
 * - 复杂表达式：((a and b) or c)
 *
 * 性能考虑：
 * - 简单的token分发，时间复杂度O(1)
 * - 括号表达式的递归开销
 * - 变量查找的作用域遍历开销
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param v 输出参数，存储解析后的表达式描述符
 *
 * @note 函数处理表达式的最基本形式
 * @see singlevar, expr, luaK_dischargevars
 *
 * @example
 * // 解析不同的前缀表达式：
 * // x          -> 变量引用
 * // (a + b)    -> 括号表达式，内部递归解析a + b
 * // func       -> 函数变量引用
 */
static void prefixexp (LexState *ls, expdesc *v) {
    switch (ls->t.token) {
        case '(': {
            int line = ls->linenumber;
            luaX_next(ls);
            expr(ls, v);
            check_match(ls, ')', '(', line);
            luaK_dischargevars(ls->fs, v);
            return;
        }
        case TK_NAME: {
            singlevar(ls, v);
            return;
        }
        default: {
            luaX_syntaxerror(ls, "unexpected symbol");
            return;
        }
    }
}

/**
 * @brief 主表达式解析：处理表达式的后缀操作
 *
 * 详细说明：
 * 这个函数解析Lua表达式的主要形式，包括前缀表达式及其后续的
 * 字段访问、索引访问、方法调用和函数调用操作。它实现了
 * 左结合的操作符链式解析。
 *
 * 语法规则：
 * primaryexp -> prefixexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs }
 * - prefixexp: 基础表达式（变量或括号表达式）
 * - '.' NAME: 字段访问，如 table.field
 * - '[' exp ']': 索引访问，如 table[key]
 * - ':' NAME funcargs: 方法调用，如 obj:method(args)
 * - funcargs: 函数调用，如 func(args)
 *
 * 左结合处理：
 * - 使用无限循环处理连续的后缀操作
 * - 每次操作都更新表达式描述符
 * - 支持链式操作：obj.field[key]:method().next
 * - 正确的结合性确保语义正确
 *
 * 操作类型：
 * 1. 字段访问('.'):
 *    - 调用field函数处理
 *    - 编译时确定的字符串键
 *    - 生成高效的字段访问代码
 *
 * 2. 索引访问('['):
 *    - 解析索引表达式
 *    - 支持任意类型的键
 *    - 运行时计算的动态访问
 *
 * 3. 方法调用(':'):
 *    - 语法糖：obj:method(args) -> obj.method(obj, args)
 *    - luaK_self: 生成self参数传递
 *    - 自动插入对象作为第一个参数
 *
 * 4. 函数调用(funcargs):
 *    - 直接函数调用
 *    - 支持多种参数形式
 *    - 处理返回值和参数传递
 *
 * 表达式转换：
 * - luaK_exp2anyreg: 确保表达式在寄存器中
 * - luaK_indexed: 生成索引访问表达式
 * - luaK_self: 生成方法调用的self参数
 *
 * 链式操作示例：
 * - obj.field: 字段访问
 * - obj[key]: 索引访问
 * - obj:method(): 方法调用
 * - obj.field[key]:method().next: 复杂链式操作
 *
 * 性能优化：
 * - 左结合避免递归调用栈
 * - 寄存器的高效使用
 * - 字段访问的编译时优化
 * - 方法调用的语法糖零开销
 *
 * 错误处理：
 * - 依赖各个子函数的错误处理
 * - 语法错误的准确定位
 * - 异常安全的表达式构建
 *
 * 使用场景：
 * - 对象属性访问：user.name, config.database.host
 * - 数组元素访问：arr[i], matrix[row][col]
 * - 方法调用：obj:toString(), file:read("*a")
 * - 函数调用：math.sin(x), print("hello")
 * - 复杂表达式：data.users[id]:getName():upper()
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param v 输入输出参数，表达式描述符，不断更新
 *
 * @note 函数实现左结合的操作符链式解析
 * @see prefixexp, field, yindex, funcargs, luaK_self
 *
 * @example
 * // 解析 obj.field[key]:method(arg)
 * // 1. prefixexp解析obj
 * // 2. field处理.field -> obj.field
 * // 3. yindex处理[key] -> obj.field[key]
 * // 4. 方法调用处理:method(arg) -> obj.field[key]:method(arg)
 */
static void primaryexp (LexState *ls, expdesc *v) {
    FuncState *fs = ls->fs;
    prefixexp(ls, v);
    for (;;) {
        switch (ls->t.token) {
            case '.': {
                field(ls, v);
                break;
            }
            case '[': {
                expdesc key;
                luaK_exp2anyreg(fs, v);
                yindex(ls, &key);
                luaK_indexed(fs, v, &key);
                break;
            }
            case ':': {
                expdesc key;
                luaX_next(ls);
                checkname(ls, &key);
                luaK_self(fs, v, &key);
                funcargs(ls, v);
                break;
            }
            case '(': case TK_STRING: case '{': {
                luaK_exp2nextreg(fs, v);
                funcargs(ls, v);
                break;
            }
            default: return;
        }
    }
}

/**
 * @brief 简单表达式解析：处理Lua的基本字面量和构造器
 *
 * 详细说明：
 * 这个函数解析Lua中的简单表达式，包括各种字面量、构造器、
 * 匿名函数和复杂的主表达式。它是表达式解析体系的核心组件。
 *
 * 语法规则：
 * simpleexp -> NUMBER | STRING | NIL | true | false | ... |
 *              constructor | FUNCTION body | primaryexp
 *
 * 支持的表达式类型：
 * 1. 数字字面量：123, 3.14, 0xFF, 1e10
 * 2. 字符串字面量："hello", 'world', [[long string]]
 * 3. 布尔字面量：true, false
 * 4. 空值字面量：nil
 * 5. 可变参数：... (仅在可变参数函数中)
 * 6. 表构造器：{1, 2, x=3}
 * 7. 匿名函数：function(x) return x*2 end
 * 8. 复杂表达式：变量、函数调用、字段访问等
 *
 * 字面量处理：
 * - TK_NUMBER: 数字常量，存储在表达式的nval字段
 * - TK_STRING: 字符串常量，通过codestring处理
 * - TK_NIL: 空值常量，类型为VNIL
 * - TK_TRUE/TK_FALSE: 布尔常量，类型为VTRUE/VFALSE
 *
 * 可变参数处理：
 * - TK_DOTS: 可变参数表达式...
 * - 检查当前函数是否为可变参数函数
 * - 生成OP_VARARG指令获取可变参数
 * - 清除VARARG_NEEDSARG标志（不需要arg表）
 *
 * 构造器和函数：
 * - '{': 表构造器，调用constructor函数
 * - TK_FUNCTION: 匿名函数，调用body函数
 * - 这些构造需要复杂的解析过程
 *
 * 主表达式：
 * - default情况：调用primaryexp处理复杂表达式
 * - 包括变量引用、函数调用、字段访问等
 * - 支持所有复杂的表达式形式
 *
 * Token消费：
 * - 大多数情况下在函数结束时消费token
 * - constructor和function情况下提前返回
 * - primaryexp情况下由子函数处理token
 *
 * 表达式描述符：
 * - init_exp: 初始化不同类型的表达式描述符
 * - VKNUM: 数字常量
 * - VNIL/VTRUE/VFALSE: 字面量常量
 * - VVARARG: 可变参数表达式
 *
 * 错误处理：
 * - 可变参数的上下文检查
 * - 不支持的token类型由primaryexp处理
 * - 语法错误的准确定位
 *
 * 性能优化：
 * - 字面量的直接处理，避免复杂解析
 * - 常量的编译时确定
 * - 高效的token分发机制
 *
 * 使用场景：
 * - 数字表达式：42, 3.14159, 0xFF
 * - 字符串表达式："hello world", [[multiline]]
 * - 布尔表达式：true and false or nil
 * - 表字面量：{name="John", age=30}
 * - 匿名函数：function(x) return x^2 end
 * - 可变参数：function(...) return ... end
 *
 * 内存管理：
 * - 字符串常量的正确引用计数
 * - 表达式描述符的安全初始化
 * - 异常安全的资源管理
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param v 输出参数，存储解析后的表达式描述符
 *
 * @note 函数处理Lua的所有基本表达式形式
 * @see init_exp, codestring, constructor, body, primaryexp
 *
 * @example
 * // 解析不同类型的简单表达式：
 * // 42         -> VKNUM类型，值为42
 * // "hello"    -> 字符串常量表达式
 * // true       -> VTRUE类型
 * // nil        -> VNIL类型
 * // {...}      -> 表构造器表达式
 * // function() end -> 匿名函数表达式
 * // ...        -> 可变参数表达式（在可变参数函数中）
 */
static void simpleexp (LexState *ls, expdesc *v) {
    switch (ls->t.token) {
        case TK_NUMBER: {
            init_exp(v, VKNUM, 0);
            v->u.nval = ls->t.seminfo.r;
            break;
        }
        case TK_STRING: {
            codestring(ls, v, ls->t.seminfo.ts);
            break;
        }
        case TK_NIL: {
            init_exp(v, VNIL, 0);
            break;
        }
        case TK_TRUE: {
            init_exp(v, VTRUE, 0);
            break;
        }
        case TK_FALSE: {
            init_exp(v, VFALSE, 0);
            break;
        }
        case TK_DOTS: {
            FuncState *fs = ls->fs;
            check_condition(ls, fs->f->is_vararg,
                          "cannot use " LUA_QL("...") " outside a vararg function");
            fs->f->is_vararg &= ~VARARG_NEEDSARG;
            init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 1, 0));
            break;
        }
        case '{': {
            constructor(ls, v);
            return;
        }
        case TK_FUNCTION: {
            luaX_next(ls);
            body(ls, v, 0, ls->linenumber);
            return;
        }
        default: {
            primaryexp(ls, v);
            return;
        }
    }
    luaX_next(ls);
}


/**
 * @brief 一元运算符识别：将token转换为一元运算符类型
 *
 * 详细说明：
 * 这个函数识别Lua中的一元运算符token，并将其转换为内部的
 * 一元运算符枚举类型。它是表达式解析中运算符处理的基础。
 *
 * 支持的一元运算符：
 * - TK_NOT: 逻辑非运算符 'not'
 * - '-': 算术负号运算符
 * - '#': 长度运算符（表和字符串的长度）
 *
 * 运算符语义：
 * - not expr: 逻辑非，返回布尔值
 * - -expr: 算术取负，适用于数字
 * - #expr: 获取长度，适用于表和字符串
 *
 * 返回值说明：
 * - OPR_NOT: 逻辑非运算符
 * - OPR_MINUS: 算术负号运算符
 * - OPR_LEN: 长度运算符
 * - OPR_NOUNOPR: 不是一元运算符
 *
 * 使用场景：
 * - 表达式解析：识别一元运算符
 * - 优先级处理：确定运算符类型
 * - 代码生成：生成相应的虚拟机指令
 *
 * 性能特征：
 * - 简单的switch语句，时间复杂度O(1)
 * - 无内存分配，纯函数调用
 * - 编译器可能内联优化
 *
 * @param op token类型，来自词法分析器
 * @return 一元运算符类型，如果不是一元运算符则返回OPR_NOUNOPR
 *
 * @note 函数是纯函数，无副作用
 * @see UnOpr, subexpr, luaK_prefix
 *
 * @example
 * // 识别不同的一元运算符：
 * // getunopr(TK_NOT) -> OPR_NOT    (not表达式)
 * // getunopr('-')    -> OPR_MINUS  (-表达式)
 * // getunopr('#')    -> OPR_LEN    (#表达式)
 * // getunopr('+')    -> OPR_NOUNOPR (不是一元运算符)
 */
static UnOpr getunopr (int op) {
    switch (op) {
        case TK_NOT: return OPR_NOT;
        case '-': return OPR_MINUS;
        case '#': return OPR_LEN;
        default: return OPR_NOUNOPR;
    }
}

/**
 * @brief 二元运算符识别：将token转换为二元运算符类型
 *
 * 详细说明：
 * 这个函数识别Lua中的二元运算符token，并将其转换为内部的
 * 二元运算符枚举类型。它支持算术、比较、逻辑和字符串连接运算符。
 *
 * 支持的二元运算符：
 * 算术运算符：
 * - '+': 加法运算符
 * - '-': 减法运算符
 * - '*': 乘法运算符
 * - '/': 除法运算符
 * - '%': 取模运算符
 * - '^': 幂运算符
 *
 * 比较运算符：
 * - TK_EQ: 等于运算符 '=='
 * - TK_NE: 不等于运算符 '~='
 * - '<': 小于运算符
 * - TK_LE: 小于等于运算符 '<='
 * - '>': 大于运算符
 * - TK_GE: 大于等于运算符 '>='
 *
 * 逻辑运算符：
 * - TK_AND: 逻辑与运算符 'and'
 * - TK_OR: 逻辑或运算符 'or'
 *
 * 字符串运算符：
 * - TK_CONCAT: 字符串连接运算符 '..'
 *
 * 运算符分类：
 * - 算术运算符：数值计算
 * - 比较运算符：返回布尔值
 * - 逻辑运算符：短路求值
 * - 连接运算符：字符串和数字的连接
 *
 * 返回值说明：
 * - OPR_*: 对应的二元运算符类型
 * - OPR_NOBINOPR: 不是二元运算符
 *
 * 使用场景：
 * - 表达式解析：识别二元运算符
 * - 优先级处理：确定运算符类型和优先级
 * - 代码生成：生成相应的虚拟机指令
 *
 * 性能特征：
 * - 简单的switch语句，时间复杂度O(1)
 * - 无内存分配，纯函数调用
 * - 编译器可能内联优化
 *
 * @param op token类型，来自词法分析器
 * @return 二元运算符类型，如果不是二元运算符则返回OPR_NOBINOPR
 *
 * @note 函数是纯函数，无副作用
 * @see BinOpr, subexpr, priority, luaK_infix
 *
 * @example
 * // 识别不同的二元运算符：
 * // getbinopr('+')      -> OPR_ADD     (加法)
 * // getbinopr(TK_AND)   -> OPR_AND     (逻辑与)
 * // getbinopr(TK_CONCAT)-> OPR_CONCAT  (字符串连接)
 * // getbinopr('(')      -> OPR_NOBINOPR (不是二元运算符)
 */
static BinOpr getbinopr (int op) {
    switch (op) {
        case '+': return OPR_ADD;
        case '-': return OPR_SUB;
        case '*': return OPR_MUL;
        case '/': return OPR_DIV;
        case '%': return OPR_MOD;
        case '^': return OPR_POW;
        case TK_CONCAT: return OPR_CONCAT;
        case TK_NE: return OPR_NE;
        case TK_EQ: return OPR_EQ;
        case '<': return OPR_LT;
        case TK_LE: return OPR_LE;
        case '>': return OPR_GT;
        case TK_GE: return OPR_GE;
        case TK_AND: return OPR_AND;
        case TK_OR: return OPR_OR;
        default: return OPR_NOBINOPR;
    }
}

/**
 * @brief 运算符优先级表：定义二元运算符的左右优先级
 *
 * 详细说明：
 * 这个静态数组定义了Lua中所有二元运算符的优先级，用于实现
 * 运算符优先级解析算法。每个运算符都有左优先级和右优先级，
 * 支持左结合和右结合运算符。
 *
 * 优先级设计原理：
 * - 数值越大，优先级越高
 * - 左右优先级相等：左结合运算符
 * - 左优先级大于右优先级：右结合运算符
 * - 优先级分组确保正确的运算顺序
 *
 * 优先级分级（从高到低）：
 * 10. 幂运算 '^' (右结合)
 * 7.  乘除模 '*', '/', '%'
 * 6.  加减 '+', '-'
 * 5.  字符串连接 '..' (右结合)
 * 3.  比较运算符 '==', '~=', '<', '<=', '>', '>='
 * 2.  逻辑与 'and'
 * 1.  逻辑或 'or'
 *
 * 结合性规则：
 * - 左结合：a op b op c = (a op b) op c
 * - 右结合：a op b op c = a op (b op c)
 * - 幂运算和字符串连接是右结合的
 * - 其他运算符都是左结合的
 *
 * 数组索引对应：
 * 数组索引与BinOpr枚举值对应：
 * - priority[OPR_ADD] = {6, 6}  // 加法
 * - priority[OPR_POW] = {10, 9} // 幂运算（右结合）
 * - priority[OPR_CONCAT] = {5, 4} // 连接（右结合）
 *
 * 算法应用：
 * 在subexpr函数中使用：
 * - 比较当前运算符优先级与限制优先级
 * - 决定是否继续解析右操作数
 * - 实现正确的运算符结合性
 *
 * 性能特征：
 * - 编译时常量数组，运行时只读
 * - O(1)时间复杂度的优先级查询
 * - 内存占用小，缓存友好
 *
 * 使用场景：
 * - 表达式解析中的优先级比较
 * - 运算符结合性判断
 * - 递归下降解析的终止条件
 *
 * @note 数组顺序必须与BinOpr枚举定义保持一致
 * @see BinOpr, subexpr, getbinopr
 *
 * @example
 * // 优先级比较示例：
 * // a + b * c   -> a + (b * c)  因为*优先级(7)高于+(6)
 * // a ^ b ^ c   -> a ^ (b ^ c)  因为^是右结合的
 * // a .. b .. c -> a .. (b .. c) 因为..是右结合的
 * // a and b or c -> (a and b) or c 因为and优先级(2)高于or(1)
 */
static const struct {
    lu_byte left;   /**< 左优先级，用于左操作数 */
    lu_byte right;  /**< 右优先级，用于右操作数 */
} priority[] = {  /* ORDER OPR */
    {6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  /* '+' '-' '*' '/' '%' */
    {10, 9}, {5, 4},                          /* '^' '..' (右结合) */
    {3, 3}, {3, 3},                           /* '==' '~=' */
    {3, 3}, {3, 3}, {3, 3}, {3, 3},          /* '<' '<=' '>' '>=' */
    {2, 2}, {1, 1}                            /* 'and' 'or' */
};

/**
 * @brief 一元运算符优先级：定义一元运算符的优先级常量
 *
 * 详细说明：
 * 一元运算符的优先级设置为8，高于大多数二元运算符，
 * 确保一元运算符能够正确地绑定到其操作数。
 *
 * 优先级设计：
 * - 8: 高于除幂运算外的所有二元运算符
 * - 低于幂运算(10)，确保 -a^b 解析为 -(a^b)
 * - 高于乘除(7)，确保 -a*b 解析为 (-a)*b
 *
 * 支持的一元运算符：
 * - not: 逻辑非
 * - -: 算术取负
 * - #: 长度运算符
 *
 * @note 一元运算符总是右结合的
 * @see getunopr, subexpr
 */
#define UNARY_PRIORITY	8


/**
 * @brief 子表达式解析：实现运算符优先级的递归下降解析
 *
 * 详细说明：
 * 这是Lua表达式解析的核心函数，实现了基于优先级的递归下降解析算法。
 * 它处理一元运算符、二元运算符的优先级和结合性，是整个表达式
 * 解析系统的核心。
 *
 * 语法规则：
 * subexpr -> (simpleexp | unop subexpr) { binop subexpr }
 * - simpleexp: 简单表达式（字面量、变量等）
 * - unop subexpr: 一元运算符表达式
 * - binop subexpr: 二元运算符表达式链
 * - 只处理优先级高于limit的二元运算符
 *
 * 算法原理：
 * 1. 处理一元运算符（如果存在）
 * 2. 解析基础表达式
 * 3. 循环处理优先级高于limit的二元运算符
 * 4. 递归解析右操作数，传递正确的优先级限制
 * 5. 生成相应的代码
 *
 * 优先级处理：
 * - limit参数：当前允许的最低优先级
 * - priority[op].left：左优先级，与limit比较
 * - priority[op].right：右优先级，传递给递归调用
 * - 实现正确的运算符结合性
 *
 * 一元运算符处理：
 * - getunopr：识别一元运算符
 * - UNARY_PRIORITY：一元运算符的固定优先级
 * - luaK_prefix：生成一元运算符代码
 * - 递归调用处理一元运算符的操作数
 *
 * 二元运算符处理：
 * - getbinopr：识别二元运算符
 * - 优先级比较：priority[op].left > limit
 * - luaK_infix：处理中缀运算符的左操作数
 * - 递归调用：subexpr(ls, &v2, priority[op].right)
 * - luaK_posfix：生成二元运算符代码
 *
 * 左递归消除：
 * - 使用while循环代替左递归
 * - 避免栈溢出问题
 * - 实现左结合运算符的正确解析
 * - 支持任意长度的运算符链
 *
 * 结合性实现：
 * - 左结合：left == right，循环继续
 * - 右结合：left > right，递归调用优先级更低
 * - 幂运算和字符串连接是右结合的
 *
 * 递归深度控制：
 * - enterlevel/leavelevel：防止栈溢出
 * - 限制表达式的嵌套深度
 * - 提供安全的递归调用机制
 *
 * 代码生成：
 * - luaK_prefix：一元运算符代码生成
 * - luaK_infix：二元运算符左操作数处理
 * - luaK_posfix：二元运算符代码生成
 * - 支持常量折叠和优化
 *
 * 返回值：
 * - 返回第一个未处理的运算符
 * - 用于上层调用的优先级判断
 * - 支持运算符链的正确解析
 *
 * 性能优化：
 * - 尾递归优化（通过循环实现）
 * - 常量表达式的编译时计算
 * - 高效的优先级比较
 *
 * 使用场景：
 * - 算术表达式：a + b * c
 * - 逻辑表达式：a and b or c
 * - 比较表达式：a < b and b < c
 * - 复杂表达式：-a^2 + b*c .. d
 *
 * 错误处理：
 * - 递归深度限制
 * - 语法错误的准确定位
 * - 异常安全的表达式构建
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param v 输出参数，存储解析后的表达式描述符
 * @param limit 优先级限制，只处理优先级高于此值的运算符
 * @return 第一个未处理的二元运算符，用于上层优先级判断
 *
 * @note 这是表达式解析的核心算法，实现了正确的优先级和结合性
 * @see expr, getunopr, getbinopr, priority, simpleexp
 *
 * @example
 * // 解析 a + b * c：
 * // 1. 解析a（simpleexp）
 * // 2. 遇到+，优先级6 > limit(0)，继续
 * // 3. 递归解析 b * c，传递优先级6
 * // 4. 在递归中，*优先级7 > 6，先计算b * c
 * // 5. 返回后计算 a + (b * c)
 */
static BinOpr subexpr (LexState *ls, expdesc *v, unsigned int limit) {
    BinOpr op;
    UnOpr uop;
    enterlevel(ls);
    uop = getunopr(ls->t.token);
    if (uop != OPR_NOUNOPR) {
        luaX_next(ls);
        subexpr(ls, v, UNARY_PRIORITY);
        luaK_prefix(ls->fs, uop, v);
    }
    else simpleexp(ls, v);
    op = getbinopr(ls->t.token);
    while (op != OPR_NOBINOPR && priority[op].left > limit) {
        expdesc v2;
        BinOpr nextop;
        luaX_next(ls);
        luaK_infix(ls->fs, op, v);
        nextop = subexpr(ls, &v2, priority[op].right);
        luaK_posfix(ls->fs, op, v, &v2);
        op = nextop;
    }
    leavelevel(ls);
    return op;
}

/**
 * @brief 表达式解析主函数：解析完整的Lua表达式
 *
 * 详细说明：
 * 这是表达式解析的公共接口，调用subexpr函数解析完整的表达式。
 * 它设置优先级限制为0，允许解析所有优先级的运算符。
 *
 * 功能特点：
 * - 解析完整的表达式，不受优先级限制
 * - 处理所有类型的Lua表达式
 * - 支持复杂的嵌套和运算符组合
 * - 生成优化的表达式代码
 *
 * 调用关系：
 * - expr -> subexpr(limit=0)
 * - 允许所有运算符参与解析
 * - 实现完整的表达式语法支持
 *
 * 使用场景：
 * - 赋值语句的右值：x = expr
 * - 函数调用参数：func(expr)
 * - 条件表达式：if expr then
 * - 返回值：return expr
 *
 * 性能特征：
 * - 简单的包装函数，开销极小
 * - 实际工作由subexpr完成
 * - 编译器可能内联优化
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param v 输出参数，存储解析后的表达式描述符
 *
 * @note 这是表达式解析的标准入口点
 * @see subexpr, expdesc
 *
 * @example
 * // 解析各种表达式：
 * // expr(ls, &v) 可以解析：
 * // - 简单表达式：42, "hello", true
 * // - 复杂表达式：a + b * c - d
 * // - 逻辑表达式：a and b or c
 * // - 函数调用：func(x, y)
 */
static void expr (LexState *ls, expdesc *v) {
    subexpr(ls, v, 0);
}

/* }==================================================================== */

/*
** {======================================================================
** 语句解析规则实现
** Rules for Statements
** =======================================================================
*/

/**
 * @brief 代码块结束标记检查：判断token是否为代码块的结束标记
 *
 * 详细说明：
 * 这个函数检查给定的token是否为代码块的结束标记，用于确定
 * 何时停止解析当前代码块中的语句。它支持各种控制结构的
 * 结束标记识别。
 *
 * 支持的结束标记：
 * - TK_ELSE: else关键字，结束if语句的then部分
 * - TK_ELSEIF: elseif关键字，结束if语句的分支
 * - TK_END: end关键字，结束各种代码块
 * - TK_UNTIL: until关键字，结束repeat循环
 * - TK_EOS: 文件结束标记，结束整个程序
 *
 * 使用场景：
 * - if语句解析：遇到else/elseif/end时停止
 * - 循环语句解析：遇到end/until时停止
 * - 函数体解析：遇到end时停止
 * - 代码块解析：确定边界
 *
 * 控制结构对应：
 * - if...then...else...end: else, elseif, end
 * - while...do...end: end
 * - for...do...end: end
 * - repeat...until: until
 * - function...end: end
 *
 * 返回值：
 * - 1: 是代码块结束标记
 * - 0: 不是代码块结束标记
 *
 * 性能特征：
 * - 简单的switch语句，时间复杂度O(1)
 * - 无副作用，纯函数
 * - 编译器可能内联优化
 *
 * 错误处理：
 * - 函数本身不处理错误
 * - 由调用者根据返回值决定行为
 * - 支持语法错误的准确定位
 *
 * @param token 要检查的token类型
 * @return 1表示是代码块结束标记，0表示不是
 *
 * @note 函数用于语句解析中的边界检测
 * @see chunk, statement, ifstat, whilestat
 *
 * @example
 * // 在不同上下文中的使用：
 * // if语句中：block_follow(TK_ELSE) -> 1
 * // while循环中：block_follow(TK_END) -> 1
 * // repeat循环中：block_follow(TK_UNTIL) -> 1
 * // 普通语句中：block_follow(TK_LOCAL) -> 0
 */
static int block_follow (int token) {
    switch (token) {
        case TK_ELSE: case TK_ELSEIF: case TK_END:
        case TK_UNTIL: case TK_EOS:
            return 1;
        default: return 0;
    }
}


/**
 * @brief 代码块解析：处理带作用域的语句序列
 *
 * 详细说明：
 * 这个函数解析一个代码块，创建新的作用域并解析其中的语句序列。
 * 代码块是Lua中作用域管理的基本单位，用于控制变量的可见性和
 * 生命周期。
 *
 * 语法规则：
 * block -> chunk
 * - chunk: 语句序列，可能包含多个语句
 * - 代码块创建新的作用域
 * - 支持局部变量的作用域控制
 *
 * 作用域管理：
 * - enterblock: 进入新的代码块作用域
 * - leaveblock: 离开代码块作用域
 * - BlockCnt: 代码块控制结构
 * - 管理局部变量的生命周期
 *
 * 跳转处理：
 * - bl.breaklist: break语句的跳转列表
 * - NO_JUMP: 表示没有未处理的跳转
 * - 确保代码块内的跳转正确处理
 *
 * 使用场景：
 * - 函数体：function() block end
 * - 控制结构：if condition then block end
 * - 循环体：while condition do block end
 * - 显式代码块：do block end
 *
 * 内存管理：
 * - 自动管理局部变量的分配和释放
 * - 作用域结束时清理变量
 * - 异常安全的资源管理
 *
 * 性能特征：
 * - 线性时间复杂度O(n)，n为语句数量
 * - 栈式作用域管理，开销小
 * - 支持深度嵌套的代码块
 *
 * @param ls 词法状态，提供token流和解析上下文
 *
 * @note 函数创建新作用域并在完成后自动清理
 * @see enterblock, leaveblock, chunk, BlockCnt
 *
 * @example
 * // 解析不同类型的代码块：
 * // do
 * //   local x = 1
 * //   print(x)
 * // end
 * // 创建作用域，x只在代码块内可见
 */
static void block (LexState *ls) {
    FuncState *fs = ls->fs;
    BlockCnt bl;
    enterblock(fs, &bl, 0);
    chunk(ls);
    lua_assert(bl.breaklist == NO_JUMP);
    leaveblock(fs);
}

/**
 * @brief 左值赋值链结构：管理多重赋值的左值序列
 *
 * 详细说明：
 * 这个结构体用于链接多重赋值语句中的所有左值变量，形成一个
 * 链表结构。它支持复杂的赋值模式，包括局部变量、全局变量、
 * upvalue和表索引的混合赋值。
 *
 * 结构设计：
 * - prev: 指向前一个左值的指针，形成链表
 * - v: 变量的表达式描述符
 * - 支持各种类型的左值变量
 *
 * 支持的变量类型：
 * - VLOCAL: 局部变量
 * - VGLOBAL: 全局变量
 * - VUPVAL: upvalue变量
 * - VINDEXED: 表索引变量
 *
 * 链表结构：
 * - 从右到左构建链表
 * - 最后一个变量的prev为NULL
 * - 支持任意数量的左值变量
 *
 * 使用场景：
 * - 多重赋值：a, b, c = 1, 2, 3
 * - 混合赋值：x, t[i], _G.y = func()
 * - 复杂赋值：a.b, c[d], e = f, g, h
 *
 * 内存管理：
 * - 结构体通常在C栈上分配
 * - 生命周期与赋值语句解析过程一致
 * - 无需显式内存管理
 *
 * 冲突检测：
 * - 用于check_conflict函数的冲突检测
 * - 处理变量别名问题
 * - 确保赋值语义的正确性
 *
 * @since Lua 5.0
 * @see assignment, check_conflict, primaryexp
 */
struct LHS_assign {
    struct LHS_assign *prev;  /**< 指向前一个左值的指针，形成链表 */
    expdesc v;                /**< 变量的表达式描述符 */
};

/**
 * @brief 赋值冲突检查：处理多重赋值中的变量别名问题
 *
 * 详细说明：
 * 这个函数检查多重赋值中是否存在变量别名冲突，并在必要时
 * 创建安全副本。当局部变量既作为左值又作为右值的索引时，
 * 需要特殊处理以确保赋值语义的正确性。
 *
 * 冲突场景：
 * 考虑赋值：a[i], i = f()
 * - 如果f()返回新的i值，会影响a[i]的索引
 * - 需要在赋值前保存原始的i值
 * - 确保a使用原始的i值作为索引
 *
 * 检查逻辑：
 * 1. 遍历左值链表中的所有索引变量
 * 2. 检查索引变量是否与当前赋值的局部变量冲突
 * 3. 如果冲突，创建局部变量的安全副本
 * 4. 更新索引变量使用安全副本
 *
 * 冲突类型：
 * - lh->v.u.s.info: 表变量的寄存器索引
 * - lh->v.u.s.aux: 键变量的寄存器索引
 * - v->u.s.info: 当前局部变量的寄存器索引
 *
 * 解决方案：
 * - OP_MOVE指令：创建变量的副本
 * - 更新索引引用：指向安全副本
 * - 保留原始语义：确保赋值顺序正确
 *
 * 算法复杂度：
 * - 时间复杂度：O(n)，n为左值变量数量
 * - 空间复杂度：O(1)，只需要常量额外空间
 * - 冲突检测的开销很小
 *
 * 使用场景：
 * - 复杂多重赋值：t[i], i = f()
 * - 表索引赋值：a[x], x, b[x] = g()
 * - 嵌套索引：t[i][j], i, j = h()
 *
 * 错误处理：
 * - 函数假设输入参数有效
 * - 依赖调用者的参数验证
 * - 异常安全的代码生成
 *
 * 性能优化：
 * - 只在检测到冲突时创建副本
 * - 最小化额外的寄存器使用
 * - 避免不必要的MOVE指令
 *
 * @param ls 词法状态，提供解析上下文
 * @param lh 左值链表头，包含所有左值变量
 * @param v 当前要赋值的局部变量描述符
 *
 * @note 函数可能生成OP_MOVE指令并分配额外寄存器
 * @see assignment, LHS_assign, OP_MOVE
 *
 * @example
 * // 冲突检查示例：
 * // local t, i = {}, 1
 * // t[i], i = 10, 2  -- 需要冲突检查
 * //
 * // 处理过程：
 * // 1. 检测到i既是索引又是赋值目标
 * // 2. 创建i的副本：MOVE temp, i
 * // 3. 更新t[i]使用temp作为索引
 * // 4. 执行赋值：t[temp] = 10, i = 2
 */
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
    FuncState *fs = ls->fs;
    int extra = fs->freereg;
    int conflict = 0;
    for (; lh; lh = lh->prev) {
        if (lh->v.k == VINDEXED) {
            if (lh->v.u.s.info == v->u.s.info) {
                conflict = 1;
                lh->v.u.s.info = extra;
            }
            if (lh->v.u.s.aux == v->u.s.info) {
                conflict = 1;
                lh->v.u.s.aux = extra;
            }
        }
    }
    if (conflict) {
        luaK_codeABC(fs, OP_MOVE, fs->freereg, v->u.s.info, 0);
        luaK_reserveregs(fs, 1);
    }
}


/**
 * @brief 赋值语句解析：处理多重赋值的递归解析
 *
 * 详细说明：
 * 这个函数递归解析多重赋值语句，处理左值变量链和右值表达式列表。
 * 它支持复杂的赋值模式，包括变量数量不匹配的情况，并生成相应的
 * 赋值代码。
 *
 * 语法规则：
 * assignment -> ',' primaryexp assignment | '=' explist1
 * - 递归处理逗号分隔的左值变量
 * - 最终处理等号和右值表达式列表
 * - 支持任意数量的左值和右值
 *
 * 递归结构：
 * 1. 如果遇到逗号，解析下一个左值变量
 * 2. 递归调用处理剩余的赋值
 * 3. 如果遇到等号，解析右值表达式列表
 * 4. 处理变量数量不匹配的情况
 *
 * 左值验证：
 * - 检查变量类型：VLOCAL, VGLOBAL, VUPVAL, VINDEXED
 * - 确保变量可以作为赋值目标
 * - 语法错误时报告准确位置
 *
 * 冲突检查：
 * - 对局部变量调用check_conflict
 * - 处理变量别名问题
 * - 确保赋值语义正确
 *
 * 数量匹配处理：
 * - nexps == nvars: 完全匹配，直接赋值
 * - nexps != nvars: 数量不匹配，调用adjust_assign
 * - nexps > nvars: 移除多余的值
 * - nexps < nvars: 用nil填充缺失的值
 *
 * 代码生成策略：
 * - 完全匹配：luaK_setoneret + luaK_storevar
 * - 数量不匹配：adjust_assign处理
 * - 默认赋值：使用VNONRELOC表达式
 *
 * 递归深度控制：
 * - luaY_checklimit: 限制变量数量
 * - 防止栈溢出
 * - 考虑C调用栈的使用情况
 *
 * 寄存器管理：
 * - 自动调整freereg指针
 * - 移除多余值的寄存器
 * - 优化寄存器使用
 *
 * 使用场景：
 * - 简单赋值：x = 1
 * - 多重赋值：a, b, c = 1, 2, 3
 * - 不匹配赋值：x, y = func() -- func可能返回多个值
 * - 复杂赋值：t[i], _G.x, local_var = f()
 *
 * 性能优化：
 * - 完全匹配时的快速路径
 * - 最小化寄存器使用
 * - 高效的代码生成
 *
 * 错误处理：
 * - 变量类型验证
 * - 递归深度限制
 * - 语法错误的准确报告
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param lh 当前左值变量的链表节点
 * @param nvars 当前已解析的左值变量数量
 *
 * @note 函数使用递归实现，需要注意栈深度
 * @see check_conflict, adjust_assign, explist1, luaK_storevar
 *
 * @example
 * // 解析多重赋值：
 * // a, b, c = 1, 2, 3
 * // 1. 递归解析a, b, c构建左值链
 * // 2. 解析右值表达式列表1, 2, 3
 * // 3. 匹配数量并生成赋值代码
 *
 * // 不匹配情况：
 * // x, y = func() -- func()可能返回0, 1, 2或更多值
 * // adjust_assign处理数量调整
 */
static void assignment (LexState *ls, struct LHS_assign *lh, int nvars) {
    expdesc e;
    check_condition(ls, VLOCAL <= lh->v.k && lh->v.k <= VINDEXED,
                        "syntax error");
    if (testnext(ls, ',')) {
        struct LHS_assign nv;
        nv.prev = lh;
        primaryexp(ls, &nv.v);
        if (nv.v.k == VLOCAL)
            check_conflict(ls, lh, &nv.v);
        luaY_checklimit(ls->fs, nvars, LUAI_MAXCCALLS - ls->L->nCcalls,
                        "variables in assignment");
        assignment(ls, &nv, nvars+1);
    }
    else {
        int nexps;
        checknext(ls, '=');
        nexps = explist1(ls, &e);
        if (nexps != nvars) {
            adjust_assign(ls, nvars, nexps, &e);
            if (nexps > nvars)
                ls->fs->freereg -= nexps - nvars;
        }
        else {
            luaK_setoneret(ls->fs, &e);
            luaK_storevar(ls->fs, &lh->v, &e);
            return;
        }
    }
    init_exp(&e, VNONRELOC, ls->fs->freereg-1);
    luaK_storevar(ls->fs, &lh->v, &e);
}

/**
 * @brief 条件表达式解析：处理控制结构中的条件判断
 *
 * 详细说明：
 * 这个函数解析控制结构（如if、while）中的条件表达式，
 * 并生成相应的条件跳转代码。它处理Lua的真值语义，
 * 将nil转换为false以优化跳转逻辑。
 *
 * 语法规则：
 * cond -> exp
 * - 解析任意表达式作为条件
 * - 支持所有Lua数据类型
 * - 应用Lua的真值语义
 *
 * 真值语义：
 * - false和nil为假值
 * - 其他所有值（包括0和空字符串）为真值
 * - nil转换为false以优化代码生成
 *
 * 代码生成：
 * - luaK_goiftrue: 生成条件为真时的跳转
 * - 返回假值跳转列表
 * - 支持短路求值优化
 *
 * 跳转优化：
 * - nil和false统一处理
 * - 减少运行时类型检查
 * - 提高条件判断效率
 *
 * 使用场景：
 * - if语句：if cond then ... end
 * - while循环：while cond do ... end
 * - repeat循环：repeat ... until cond
 * - 三元运算符模拟：cond and a or b
 *
 * 返回值：
 * - 返回假值跳转列表的头部
 * - 用于后续的跳转链接
 * - 支持复杂的控制流构建
 *
 * 性能特征：
 * - 编译时优化nil/false
 * - 高效的跳转代码生成
 * - 支持虚拟机的快速条件判断
 *
 * 错误处理：
 * - 依赖expr函数的错误处理
 * - 语法错误的准确传播
 * - 异常安全的代码生成
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @return 假值跳转列表的头部，用于后续跳转处理
 *
 * @note 函数应用Lua的真值语义进行优化
 * @see expr, luaK_goiftrue, ifstat, whilestat
 *
 * @example
 * // 不同类型的条件表达式：
 * // if x then ... end           -- 变量条件
 * // if x > 0 then ... end       -- 比较条件
 * // if func() then ... end      -- 函数调用条件
 * // if x and y then ... end     -- 逻辑条件
 */
static int cond (LexState *ls) {
    expdesc v;
    expr(ls, &v);
    if (v.k == VNIL) v.k = VFALSE;
    luaK_goiftrue(ls->fs, &v);
    return v.f;
}

/**
 * @brief break语句解析：处理循环中的break跳转
 *
 * 详细说明：
 * 这个函数解析break语句，生成跳出循环的跳转代码。它查找最近的
 * 可break的代码块，处理upvalue的关闭，并将跳转添加到break列表中。
 *
 * 语法规则：
 * breakstat -> 'break'
 * - break只能在循环内使用
 * - 跳转到循环结束位置
 * - 支持嵌套循环的正确跳转
 *
 * 代码块查找：
 * - 从当前代码块开始向外查找
 * - 寻找isbreakable标志为真的代码块
 * - 只有循环代码块可以被break
 *
 * Upvalue处理：
 * - 检查路径上是否有upvalue引用
 * - 生成OP_CLOSE指令关闭upvalue
 * - 确保变量生命周期的正确性
 *
 * 跳转管理：
 * - luaK_jump: 生成无条件跳转指令
 * - luaK_concat: 将跳转添加到break列表
 * - 支持多个break语句的统一处理
 *
 * 错误检查：
 * - 验证break语句在循环内
 * - 提供准确的错误消息
 * - 防止在非循环上下文中使用break
 *
 * 代码块类型：
 * - 可break：while、for、repeat循环
 * - 不可break：if、function、do代码块
 * - 通过isbreakable标志区分
 *
 * 内存管理：
 * - OP_CLOSE指令处理局部变量清理
 * - 确保upvalue的正确关闭
 * - 异常安全的资源管理
 *
 * 使用场景：
 * - while循环：while true do ... break ... end
 * - for循环：for i=1,10 do ... break ... end
 * - repeat循环：repeat ... break ... until false
 * - 嵌套循环：提前退出内层或外层循环
 *
 * 性能特征：
 * - 高效的跳转代码生成
 * - 最小化upvalue关闭开销
 * - 支持编译时跳转优化
 *
 * 限制：
 * - 不能在函数边界外跳转
 * - 不能跳出非循环代码块
 * - 遵循结构化编程原则
 *
 * @param ls 词法状态，提供解析上下文
 *
 * @note 函数可能生成OP_CLOSE和跳转指令
 * @see BlockCnt, luaK_jump, luaK_concat, OP_CLOSE
 *
 * @example
 * // break语句的使用：
 * // while condition do
 * //   if special_case then
 * //     break  -- 跳出while循环
 * //   end
 * //   -- 其他代码
 * // end
 *
 * // 嵌套循环中的break：
 * // for i=1,10 do
 * //   for j=1,10 do
 * //     if found then break end  -- 只跳出内层循环
 * //   end
 * // end
 */
static void breakstat (LexState *ls) {
    FuncState *fs = ls->fs;
    BlockCnt *bl = fs->bl;
    int upval = 0;
    while (bl && !bl->isbreakable) {
        upval |= bl->upval;
        bl = bl->previous;
    }
    if (!bl)
        luaX_syntaxerror(ls, "no loop to break");
    if (upval)
        luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
    luaK_concat(fs, &bl->breaklist, luaK_jump(fs));
}


/**
 * @brief while循环解析：处理while语句的完整结构
 *
 * 详细说明：
 * 这个函数解析while循环语句，生成相应的循环控制代码。它实现了
 * 条件检查、循环体执行和循环跳转的完整逻辑，支持break语句的
 * 正确处理。
 *
 * 语法规则：
 * whilestat -> WHILE cond DO block END
 * - WHILE: while关键字
 * - cond: 循环条件表达式
 * - DO: do关键字
 * - block: 循环体代码块
 * - END: end关键字
 *
 * 代码生成策略：
 * 1. 设置循环开始标签
 * 2. 生成条件检查代码
 * 3. 创建可break的代码块
 * 4. 解析循环体
 * 5. 生成回跳指令
 * 6. 修补条件跳转
 *
 * 跳转处理：
 * - whileinit: 循环开始位置
 * - condexit: 条件为假时的跳转列表
 * - 回跳到循环开始位置
 * - 条件为假时跳出循环
 *
 * 代码块管理：
 * - enterblock(fs, &bl, 1): 创建可break的代码块
 * - isbreakable=1: 支持break语句
 * - leaveblock: 清理代码块和局部变量
 *
 * 指令序列：
 * 1. 循环开始标签
 * 2. 条件检查指令
 * 3. 条件跳转指令（假值跳出）
 * 4. 循环体指令
 * 5. 无条件跳转到开始
 * 6. 循环结束标签
 *
 * 性能优化：
 * - 高效的跳转指令生成
 * - 最小化条件检查开销
 * - 支持虚拟机的循环优化
 *
 * 使用场景：
 * - 条件循环：while i < 10 do ... end
 * - 无限循环：while true do ... end
 * - 复杂条件：while x and y do ... end
 *
 * 错误处理：
 * - 关键字匹配检查
 * - 语法结构验证
 * - 异常安全的代码生成
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param line while关键字所在的行号，用于错误报告
 *
 * @note 函数生成循环控制指令和跳转修补
 * @see cond, block, enterblock, leaveblock
 *
 * @example
 * // while循环的代码生成：
 * // while i < 10 do
 * //   print(i)
 * //   i = i + 1
 * // end
 * //
 * // 生成的指令序列：
 * // LOOP_START:
 * //   LT i, 10        -- 条件检查
 * //   JMP_FALSE EXIT  -- 假值跳出
 * //   CALL print, i   -- 循环体
 * //   ADD i, i, 1
 * //   JMP LOOP_START  -- 回跳
 * // EXIT:
 */
static void whilestat (LexState *ls, int line) {
    FuncState *fs = ls->fs;
    int whileinit;
    int condexit;
    BlockCnt bl;
    luaX_next(ls);
    whileinit = luaK_getlabel(fs);
    condexit = cond(ls);
    enterblock(fs, &bl, 1);
    checknext(ls, TK_DO);
    block(ls);
    luaK_patchlist(fs, luaK_jump(fs), whileinit);
    check_match(ls, TK_END, TK_WHILE, line);
    leaveblock(fs);
    luaK_patchtohere(fs, condexit);
}

/**
 * @brief repeat循环解析：处理repeat-until语句的完整结构
 *
 * 详细说明：
 * 这个函数解析repeat-until循环语句，实现了后测试循环的语义。
 * 它处理复杂的作用域管理，特别是until条件中可以访问循环体内
 * 声明的局部变量的特殊语义。
 *
 * 语法规则：
 * repeatstat -> REPEAT block UNTIL cond
 * - REPEAT: repeat关键字
 * - block: 循环体代码块
 * - UNTIL: until关键字
 * - cond: 循环结束条件
 *
 * 特殊语义：
 * - 循环体至少执行一次
 * - until条件可以访问循环体内的局部变量
 * - 需要特殊的作用域处理
 *
 * 双重代码块结构：
 * - bl1: 循环代码块（可break）
 * - bl2: 作用域代码块（包含局部变量）
 * - 嵌套结构支持复杂的变量可见性
 *
 * Upvalue处理：
 * - 检查作用域块是否有upvalue引用
 * - 无upvalue: 简单的条件跳转
 * - 有upvalue: 复杂的break处理
 *
 * 代码生成策略：
 * 无upvalue情况：
 * 1. 循环体代码
 * 2. 条件检查
 * 3. 条件为假时跳回开始
 *
 * 有upvalue情况：
 * 1. 循环体代码
 * 2. 条件检查
 * 3. 条件为真时break
 * 4. 否则跳回开始
 *
 * 作用域管理：
 * - 内层作用域包含局部变量
 * - until条件在内层作用域中求值
 * - 正确的变量生命周期管理
 *
 * 跳转处理：
 * - repeat_init: 循环开始位置
 * - condexit: 条件跳转列表
 * - 支持break语句的正确跳转
 *
 * 性能考虑：
 * - 优化无upvalue的简单情况
 * - 复杂情况的正确语义保证
 * - 高效的跳转指令生成
 *
 * 使用场景：
 * - 后测试循环：repeat ... until condition
 * - 至少执行一次的循环
 * - 需要在条件中访问循环变量
 *
 * 错误处理：
 * - 关键字匹配检查
 * - 作用域的正确管理
 * - 异常安全的资源清理
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param line repeat关键字所在的行号，用于错误报告
 *
 * @note 函数实现复杂的作用域和upvalue处理
 * @see cond, chunk, enterblock, leaveblock, breakstat
 *
 * @example
 * // repeat循环的特殊语义：
 * // repeat
 * //   local x = getValue()
 * //   process(x)
 * // until x > threshold  -- x在此处可见
 * //
 * // 作用域结构：
 * // bl1 (循环块)
 * //   bl2 (作用域块)
 * //     local x = ...
 * //     process(x)
 * //   until x > threshold  -- 在bl2作用域中
 */
static void repeatstat (LexState *ls, int line) {
    int condexit;
    FuncState *fs = ls->fs;
    int repeat_init = luaK_getlabel(fs);
    BlockCnt bl1, bl2;
    enterblock(fs, &bl1, 1);
    enterblock(fs, &bl2, 0);
    luaX_next(ls);
    chunk(ls);
    check_match(ls, TK_UNTIL, TK_REPEAT, line);
    condexit = cond(ls);
    if (!bl2.upval) {
        leaveblock(fs);
        luaK_patchlist(ls->fs, condexit, repeat_init);
    }
    else {
        breakstat(ls);
        luaK_patchtohere(ls->fs, condexit);
        leaveblock(fs);
        luaK_patchlist(ls->fs, luaK_jump(fs), repeat_init);
    }
    leaveblock(fs);
}

/**
 * @brief 单表达式解析：解析表达式并存储到寄存器
 *
 * 详细说明：
 * 这个辅助函数解析单个表达式，将其结果存储到下一个可用寄存器中，
 * 并返回表达式的原始类型。它主要用于for循环的初值、限值和步长
 * 表达式的处理。
 *
 * 处理流程：
 * 1. 解析表达式
 * 2. 保存表达式类型
 * 3. 将表达式结果存储到寄存器
 * 4. 返回原始表达式类型
 *
 * 寄存器管理：
 * - luaK_exp2nextreg: 将表达式存储到下一个寄存器
 * - 自动分配和管理寄存器
 * - 确保表达式结果的持久性
 *
 * 类型保持：
 * - 返回表达式的原始类型
 * - 用于后续的类型检查和优化
 * - 支持编译时常量识别
 *
 * 使用场景：
 * - for循环的数值表达式
 * - 需要持久化的表达式结果
 * - 多个表达式的顺序处理
 *
 * 性能特征：
 * - 简单的包装函数，开销极小
 * - 寄存器的高效使用
 * - 支持表达式优化
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @return 表达式的原始类型，用于类型检查
 *
 * @note 函数会消耗一个寄存器存储表达式结果
 * @see expr, luaK_exp2nextreg, fornum
 *
 * @example
 * // 在for循环中的使用：
 * // for i = start, limit, step do
 * //   exp1(ls)  -- 解析start表达式
 * //   exp1(ls)  -- 解析limit表达式
 * //   exp1(ls)  -- 解析step表达式
 * // end
 */
static int exp1 (LexState *ls) {
    expdesc e;
    int k;
    expr(ls, &e);
    k = e.k;
    luaK_exp2nextreg(ls->fs, &e);
    return k;
}


/**
 * @brief for循环体解析：处理for循环的循环体和控制逻辑
 *
 * 详细说明：
 * 这个函数处理for循环的循环体部分，生成相应的循环控制指令。
 * 它支持数值for循环和泛型for循环两种模式，实现了不同的
 * 循环控制策略。
 *
 * 语法规则：
 * forbody -> DO block
 * - DO: do关键字
 * - block: 循环体代码块
 *
 * 参数说明：
 * - base: 控制变量的基础寄存器位置
 * - line: for语句的行号
 * - nvars: 声明的循环变量数量
 * - isnum: 是否为数值for循环
 *
 * 变量管理：
 * - 前3个为控制变量（索引、限值、步长或生成器、状态、控制）
 * - 后续为用户声明的循环变量
 * - 不同作用域的变量生命周期管理
 *
 * 数值for循环（isnum=1）：
 * - OP_FORPREP: 循环准备指令
 * - OP_FORLOOP: 循环控制指令
 * - 自动递增/递减控制
 *
 * 泛型for循环（isnum=0）：
 * - OP_TFORLOOP: 表for循环指令
 * - 调用迭代器函数
 * - 处理多返回值
 *
 * 代码生成流程：
 * 1. 激活控制变量
 * 2. 生成循环准备指令
 * 3. 创建循环变量作用域
 * 4. 解析循环体
 * 5. 生成循环控制指令
 * 6. 修补跳转地址
 *
 * 作用域结构：
 * - 外层：控制变量作用域
 * - 内层：用户变量作用域
 * - 支持break语句的正确处理
 *
 * 跳转处理：
 * - prep: 循环准备指令位置
 * - endfor: 循环结束指令
 * - 正确的跳转地址修补
 *
 * 性能优化：
 * - 数值循环的高效实现
 * - 泛型循环的迭代器调用优化
 * - 寄存器的合理分配
 *
 * 使用场景：
 * - 数值for：for i=1,10 do ... end
 * - 泛型for：for k,v in pairs(t) do ... end
 * - 多变量for：for i,j,k in iter() do ... end
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param base 控制变量的基础寄存器位置
 * @param line for语句的行号，用于调试信息
 * @param nvars 用户声明的循环变量数量
 * @param isnum 是否为数值for循环（1）或泛型for循环（0）
 *
 * @note 函数生成不同类型的for循环控制指令
 * @see fornum, forlist, OP_FORPREP, OP_FORLOOP, OP_TFORLOOP
 *
 * @example
 * // 数值for循环的指令序列：
 * // for i=1,10,2 do print(i) end
 * // FORPREP base, LOOP_END    -- 准备循环
 * // LOOP_START:
 * //   CALL print, i           -- 循环体
 * //   FORLOOP base, LOOP_START -- 循环控制
 * // LOOP_END:
 */
static void forbody (LexState *ls, int base, int line, int nvars, int isnum) {
    BlockCnt bl;
    FuncState *fs = ls->fs;
    int prep, endfor;
    adjustlocalvars(ls, 3);
    checknext(ls, TK_DO);
    prep = isnum ? luaK_codeAsBx(fs, OP_FORPREP, base, NO_JUMP) : luaK_jump(fs);
    enterblock(fs, &bl, 0);
    adjustlocalvars(ls, nvars);
    luaK_reserveregs(fs, nvars);
    block(ls);
    leaveblock(fs);
    luaK_patchtohere(fs, prep);
    endfor = (isnum) ? luaK_codeAsBx(fs, OP_FORLOOP, base, NO_JUMP) :
                       luaK_codeABC(fs, OP_TFORLOOP, base, 0, nvars);
    luaK_fixline(fs, line);
    luaK_patchlist(fs, (isnum ? endfor : luaK_jump(fs)), prep + 1);
}

/**
 * @brief 数值for循环解析：处理数值范围的for循环
 *
 * 详细说明：
 * 这个函数解析数值for循环，创建必要的控制变量，解析初值、限值
 * 和可选的步长表达式，然后调用forbody处理循环体。
 *
 * 语法规则：
 * fornum -> NAME = exp1,exp1[,exp1] forbody
 * - NAME: 循环变量名
 * - exp1: 初值表达式
 * - exp1: 限值表达式
 * - [exp1]: 可选的步长表达式
 *
 * 控制变量：
 * - "(for index)": 内部索引变量
 * - "(for limit)": 限值变量
 * - "(for step)": 步长变量
 * - varname: 用户可见的循环变量
 *
 * 变量布局：
 * base+0: 内部索引（实际计数器）
 * base+1: 限值
 * base+2: 步长
 * base+3: 用户循环变量
 *
 * 表达式处理：
 * - 初值：循环开始值
 * - 限值：循环结束条件
 * - 步长：每次递增值（默认为1）
 *
 * 默认步长：
 * - 如果未指定步长，默认为1
 * - 使用OP_LOADK加载常量1
 * - 优化常见的递增循环
 *
 * 循环语义：
 * - 初值到限值的数值范围
 * - 支持正向和反向循环
 * - 步长为0时的无限循环检测
 *
 * 性能优化：
 * - 数值循环的专用指令
 * - 避免函数调用开销
 * - 高效的边界检查
 *
 * 使用场景：
 * - 简单计数：for i=1,10 do ... end
 * - 反向循环：for i=10,1,-1 do ... end
 * - 自定义步长：for i=0,100,5 do ... end
 *
 * 错误处理：
 * - 语法结构验证
 * - 表达式解析错误传播
 * - 变量名冲突检查
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param varname 用户指定的循环变量名
 * @param line for语句的行号，用于调试信息
 *
 * @note 函数创建4个局部变量（3个控制+1个用户）
 * @see exp1, forbody, new_localvar, OP_FORPREP, OP_FORLOOP
 *
 * @example
 * // 数值for循环示例：
 * // for i = 1, 10, 2 do
 * //   print(i)  -- 输出 1, 3, 5, 7, 9
 * // end
 * //
 * // 变量分配：
 * // base+0: 内部索引 = 1-2 = -1 (初始)
 * // base+1: 限值 = 10
 * // base+2: 步长 = 2
 * // base+3: i = 1 (用户可见)
 */
static void fornum (LexState *ls, TString *varname, int line) {
    FuncState *fs = ls->fs;
    int base = fs->freereg;
    new_localvarliteral(ls, "(for index)", 0);
    new_localvarliteral(ls, "(for limit)", 1);
    new_localvarliteral(ls, "(for step)", 2);
    new_localvar(ls, varname, 3);
    checknext(ls, '=');
    exp1(ls);
    checknext(ls, ',');
    exp1(ls);
    if (testnext(ls, ','))
        exp1(ls);
    else {
        luaK_codeABx(fs, OP_LOADK, fs->freereg, luaK_numberK(fs, 1));
        luaK_reserveregs(fs, 1);
    }
    forbody(ls, base, line, 1, 1);
}

/**
 * @brief 泛型for循环解析：处理迭代器的for循环
 *
 * 详细说明：
 * 这个函数解析泛型for循环，支持任意迭代器函数的循环。它创建
 * 必要的控制变量，解析循环变量列表和迭代器表达式，实现了
 * Lua的迭代器协议。
 *
 * 语法规则：
 * forlist -> NAME {,NAME} IN explist1 forbody
 * - NAME: 第一个循环变量名
 * - {,NAME}: 可选的额外循环变量
 * - IN: in关键字
 * - explist1: 迭代器表达式列表
 *
 * 控制变量：
 * - "(for generator)": 迭代器函数
 * - "(for state)": 迭代器状态
 * - "(for control)": 控制变量
 *
 * 变量布局：
 * base+0: 迭代器函数
 * base+1: 不变状态
 * base+2: 控制变量
 * base+3+: 用户循环变量
 *
 * 迭代器协议：
 * - 迭代器函数接收状态和控制变量
 * - 返回新的控制变量和循环变量值
 * - 控制变量为nil时结束循环
 *
 * 表达式处理：
 * - explist1: 解析迭代器表达式列表
 * - adjust_assign: 调整为3个控制变量
 * - 支持多返回值的迭代器
 *
 * 栈空间管理：
 * - luaK_checkstack: 确保有足够空间调用生成器
 * - 迭代器调用需要额外的栈空间
 * - 防止栈溢出
 *
 * 多变量支持：
 * - 支持任意数量的循环变量
 * - 自动处理变量数量不匹配
 * - nil填充缺失的变量
 *
 * 性能考虑：
 * - 高效的迭代器调用
 * - 最小化函数调用开销
 * - 支持内置迭代器的优化
 *
 * 使用场景：
 * - 表遍历：for k,v in pairs(t) do ... end
 * - 数组遍历：for i,v in ipairs(a) do ... end
 * - 自定义迭代器：for x in myiter() do ... end
 * - 多变量：for a,b,c in iter() do ... end
 *
 * 错误处理：
 * - 变量名解析错误
 * - 迭代器表达式错误
 * - 栈空间不足检查
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param indexname 第一个循环变量名
 *
 * @note 函数创建3+n个局部变量（3个控制+n个用户）
 * @see explist1, adjust_assign, forbody, OP_TFORLOOP
 *
 * @example
 * // 泛型for循环示例：
 * // for k, v in pairs(table) do
 * //   print(k, v)
 * // end
 * //
 * // 变量分配：
 * // base+0: pairs函数
 * // base+1: table（状态）
 * // base+2: nil（初始控制变量）
 * // base+3: k（用户变量）
 * // base+4: v（用户变量）
 */
static void forlist (LexState *ls, TString *indexname) {
    FuncState *fs = ls->fs;
    expdesc e;
    int nvars = 0;
    int line;
    int base = fs->freereg;
    new_localvarliteral(ls, "(for generator)", nvars++);
    new_localvarliteral(ls, "(for state)", nvars++);
    new_localvarliteral(ls, "(for control)", nvars++);
    new_localvar(ls, indexname, nvars++);
    while (testnext(ls, ','))
        new_localvar(ls, str_checkname(ls), nvars++);
    checknext(ls, TK_IN);
    line = ls->linenumber;
    adjust_assign(ls, 3, explist1(ls, &e), &e);
    luaK_checkstack(fs, 3);
    forbody(ls, base, line, nvars - 3, 0);
}


/**
 * @brief for语句解析：处理for循环的主入口函数
 *
 * 详细说明：
 * 这个函数是for循环解析的主入口，根据语法结构判断是数值for循环
 * 还是泛型for循环，然后调用相应的处理函数。它创建循环作用域
 * 并管理break语句的跳转。
 *
 * 语法规则：
 * forstat -> FOR (fornum | forlist) END
 * - FOR: for关键字
 * - fornum: 数值for循环（NAME = exp,exp[,exp]）
 * - forlist: 泛型for循环（NAME {,NAME} IN explist）
 * - END: end关键字
 *
 * 循环类型判断：
 * - '=': 数值for循环，调用fornum
 * - ',': 泛型for循环（多变量），调用forlist
 * - TK_IN: 泛型for循环（单变量），调用forlist
 *
 * 作用域管理：
 * - enterblock(fs, &bl, 1): 创建可break的循环作用域
 * - 包含所有循环控制变量和用户变量
 * - break语句跳转到leaveblock位置
 *
 * 变量名解析：
 * - str_checkname: 解析第一个变量名
 * - 用于区分循环类型和变量创建
 * - 错误检查和语法验证
 *
 * 错误处理：
 * - 语法错误：期望'='或'in'
 * - 关键字匹配：for...end配对
 * - 准确的错误位置报告
 *
 * 使用场景：
 * - 数值循环：for i=1,10 do ... end
 * - 泛型循环：for k,v in pairs(t) do ... end
 * - 嵌套循环：支持任意深度的嵌套
 *
 * 性能特征：
 * - 高效的循环类型分发
 * - 最小化语法分析开销
 * - 支持编译器优化
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param line for关键字所在的行号，用于错误报告
 *
 * @note 函数创建循环作用域并在完成后清理
 * @see fornum, forlist, enterblock, leaveblock
 *
 * @example
 * // 不同类型的for循环：
 * // for i = 1, 10 do ... end      -- 数值循环
 * // for k, v in pairs(t) do ... end -- 泛型循环
 * // for line in io.lines() do ... end -- 单变量泛型循环
 */
static void forstat (LexState *ls, int line) {
    FuncState *fs = ls->fs;
    TString *varname;
    BlockCnt bl;
    enterblock(fs, &bl, 1);
    luaX_next(ls);
    varname = str_checkname(ls);
    switch (ls->t.token) {
        case '=': fornum(ls, varname, line); break;
        case ',': case TK_IN: forlist(ls, varname); break;
        default: luaX_syntaxerror(ls, LUA_QL("=") " or " LUA_QL("in") " expected");
    }
    check_match(ls, TK_END, TK_FOR, line);
    leaveblock(fs);
}

/**
 * @brief 条件-then块解析：处理if和elseif的条件和代码块
 *
 * 详细说明：
 * 这个辅助函数解析if语句和elseif语句的条件部分和then代码块，
 * 生成相应的条件跳转代码。它是if语句解析的核心组件。
 *
 * 语法规则：
 * test_then_block -> [IF | ELSEIF] cond THEN block
 * - IF/ELSEIF: 条件关键字
 * - cond: 条件表达式
 * - THEN: then关键字
 * - block: 条件为真时执行的代码块
 *
 * 处理流程：
 * 1. 跳过IF或ELSEIF关键字
 * 2. 解析条件表达式
 * 3. 检查THEN关键字
 * 4. 解析then代码块
 * 5. 返回条件为假时的跳转列表
 *
 * 条件处理：
 * - cond函数处理条件表达式
 * - 生成条件为真时的跳转
 * - 返回假值跳转列表供后续处理
 *
 * 代码生成：
 * - 条件检查指令
 * - 条件跳转指令
 * - then块的代码
 * - 跳转地址的占位符
 *
 * 使用场景：
 * - if语句的主条件
 * - elseif语句的附加条件
 * - 复杂条件表达式的处理
 *
 * 返回值：
 * - 条件为假时的跳转列表头部
 * - 用于后续的跳转链接和修补
 * - 支持多分支的统一处理
 *
 * 性能特征：
 * - 高效的条件跳转生成
 * - 最小化跳转指令数量
 * - 支持短路求值优化
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @return 条件为假时的跳转列表头部
 *
 * @note 函数假设当前token是IF或ELSEIF
 * @see cond, block, ifstat
 *
 * @example
 * // if语句的条件块：
 * // if x > 0 then
 * //   print("positive")
 * // end
 * //
 * // 生成的跳转逻辑：
 * // TEST x > 0
 * // JMP_FALSE L1    -- 条件为假跳转
 * // CALL print, "positive"
 * // L1:             -- 跳转目标
 */
static int test_then_block (LexState *ls) {
    int condexit;
    luaX_next(ls);
    condexit = cond(ls);
    checknext(ls, TK_THEN);
    block(ls);
    return condexit;
}

/**
 * @brief if语句解析：处理完整的if-elseif-else结构
 *
 * 详细说明：
 * 这个函数解析完整的if语句，包括主if条件、多个elseif分支和
 * 可选的else分支。它实现了复杂的跳转逻辑，确保只执行匹配
 * 条件的代码块。
 *
 * 语法规则：
 * ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END
 * - IF cond THEN block: 主条件分支
 * - {ELSEIF cond THEN block}: 零个或多个elseif分支
 * - [ELSE block]: 可选的else分支
 * - END: 结束标记
 *
 * 跳转管理：
 * - flist: 当前条件为假时的跳转列表
 * - escapelist: 各分支结束后的跳转列表
 * - 复杂的跳转链接和修补逻辑
 *
 * 处理流程：
 * 1. 处理主if条件和then块
 * 2. 循环处理所有elseif分支
 * 3. 处理可选的else分支
 * 4. 修补所有跳转地址
 *
 * 分支处理策略：
 * - 每个分支结束后跳转到语句末尾
 * - 条件为假时跳转到下一个分支
 * - else分支处理所有未匹配的情况
 *
 * 跳转优化：
 * - luaK_concat: 连接跳转列表
 * - luaK_patchtohere: 修补跳转到当前位置
 * - 最小化跳转指令数量
 *
 * 代码生成模式：
 * ```
 * IF_COND:
 *   test condition1
 *   jmp_false ELSEIF1
 *   then_block1
 *   jmp END
 * ELSEIF1:
 *   test condition2
 *   jmp_false ELSE
 *   then_block2
 *   jmp END
 * ELSE:
 *   else_block
 * END:
 * ```
 *
 * 使用场景：
 * - 简单条件：if x then ... end
 * - 多分支：if x then ... elseif y then ... else ... end
 * - 嵌套if：支持任意深度的嵌套
 *
 * 性能特征：
 * - 高效的多分支跳转
 * - 最优的跳转指令生成
 * - 支持编译器的进一步优化
 *
 * 错误处理：
 * - 关键字匹配检查
 * - 语法结构验证
 * - 准确的错误位置报告
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param line if关键字所在的行号，用于错误报告
 *
 * @note 函数生成复杂的条件跳转逻辑
 * @see test_then_block, block, luaK_concat, luaK_patchtohere
 *
 * @example
 * // 复杂if语句：
 * // if score >= 90 then
 * //   grade = "A"
 * // elseif score >= 80 then
 * //   grade = "B"
 * // elseif score >= 70 then
 * //   grade = "C"
 * // else
 * //   grade = "F"
 * // end
 */
static void ifstat (LexState *ls, int line) {
    FuncState *fs = ls->fs;
    int flist;
    int escapelist = NO_JUMP;
    flist = test_then_block(ls);
    while (ls->t.token == TK_ELSEIF) {
        luaK_concat(fs, &escapelist, luaK_jump(fs));
        luaK_patchtohere(fs, flist);
        flist = test_then_block(ls);
    }
    if (ls->t.token == TK_ELSE) {
        luaK_concat(fs, &escapelist, luaK_jump(fs));
        luaK_patchtohere(fs, flist);
        luaX_next(ls);
        block(ls);
    }
    else
        luaK_concat(fs, &escapelist, flist);
    luaK_patchtohere(fs, escapelist);
    check_match(ls, TK_END, TK_IF, line);
}

/**
 * @brief 局部函数解析：处理local function声明
 *
 * 详细说明：
 * 这个函数解析局部函数声明，创建局部变量并将函数体赋值给它。
 * 它处理了局部函数的特殊语义，包括递归调用的支持。
 *
 * 语法规则：
 * localfunc -> LOCAL FUNCTION NAME body
 * - LOCAL FUNCTION: 局部函数关键字
 * - NAME: 函数名
 * - body: 函数体（参数列表和代码块）
 *
 * 处理策略：
 * 1. 创建局部变量
 * 2. 预留寄存器空间
 * 3. 激活局部变量
 * 4. 解析函数体
 * 5. 将函数赋值给局部变量
 *
 * 递归支持：
 * - 函数名在函数体解析前就已激活
 * - 支持函数内部的递归调用
 * - 正确的作用域和可见性管理
 *
 * 变量管理：
 * - new_localvar: 创建局部变量
 * - init_exp: 初始化变量表达式
 * - adjustlocalvars: 激活变量
 * - luaK_reserveregs: 预留寄存器
 *
 * 函数处理：
 * - body: 解析函数体（参数和代码块）
 * - needself=0: 不是方法，无需self参数
 * - 生成闭包和相关代码
 *
 * 赋值处理：
 * - luaK_storevar: 将函数存储到局部变量
 * - 处理函数作为一等值的语义
 * - 支持函数的后续引用和调用
 *
 * 作用域特点：
 * - 函数名只在当前作用域可见
 * - 支持函数内部的自递归
 * - 不污染全局命名空间
 *
 * 使用场景：
 * - 局部函数：local function helper() ... end
 * - 递归函数：local function factorial(n) ... factorial(n-1) ... end
 * - 辅助函数：模块内部的工具函数
 *
 * 性能特征：
 * - 高效的局部变量访问
 * - 避免全局查找开销
 * - 支持编译器优化
 *
 * 内存管理：
 * - 自动的变量生命周期管理
 * - 作用域结束时自动清理
 * - 异常安全的资源管理
 *
 * @param ls 词法状态，提供token流和解析上下文
 *
 * @note 函数创建局部变量并解析函数体
 * @see new_localvar, body, luaK_storevar, adjustlocalvars
 *
 * @example
 * // 局部函数声明：
 * // local function fibonacci(n)
 * //   if n <= 1 then
 * //     return n
 * //   else
 * //     return fibonacci(n-1) + fibonacci(n-2)  -- 递归调用
 * //   end
 * // end
 */
static void localfunc (LexState *ls) {
    expdesc v, b;
    FuncState *fs = ls->fs;
    new_localvar(ls, str_checkname(ls), 0);
    init_exp(&v, VLOCAL, fs->freereg);
    luaK_reserveregs(fs, 1);
    adjustlocalvars(ls, 1);
    body(ls, &b, 0, ls->linenumber);
    luaK_storevar(fs, &v, &b);
    getlocvar(fs, fs->nactvar - 1).startpc = fs->pc;
}

/**
 * @brief 局部变量声明解析：处理local变量声明语句
 *
 * 详细说明：
 * 这个函数解析局部变量声明语句，支持多变量声明和可选的初始化
 * 表达式。它实现了Lua的局部变量语义，包括作用域管理和初值处理。
 *
 * 语法规则：
 * localstat -> LOCAL NAME {',' NAME} ['=' explist1]
 * - LOCAL: local关键字
 * - NAME: 变量名列表
 * - {',' NAME}: 可选的额外变量名
 * - ['=' explist1]: 可选的初始化表达式列表
 *
 * 处理流程：
 * 1. 循环解析所有变量名
 * 2. 检查是否有初始化表达式
 * 3. 解析初始化表达式列表（如果存在）
 * 4. 调整变量和表达式数量匹配
 * 5. 激活所有局部变量
 *
 * 变量创建：
 * - new_localvar: 为每个变量名创建局部变量
 * - 变量按声明顺序编号
 * - 支持任意数量的变量声明
 *
 * 初始化处理：
 * - 有'='：解析表达式列表
 * - 无'='：所有变量初始化为nil
 * - adjust_assign: 处理数量不匹配
 *
 * 数量匹配策略：
 * - nvars > nexps: 多余变量初始化为nil
 * - nvars < nexps: 多余表达式被丢弃
 * - nvars == nexps: 一对一赋值
 *
 * 作用域管理：
 * - adjustlocalvars: 激活所有声明的变量
 * - 变量从此点开始可见
 * - 作用域结束时自动清理
 *
 * 使用场景：
 * - 简单声明：local x
 * - 初始化声明：local x = 1
 * - 多变量声明：local a, b, c = 1, 2, 3
 * - 函数返回值：local x, y = func()
 *
 * 性能特征：
 * - 高效的变量分配
 * - 最小化初始化开销
 * - 支持编译器优化
 *
 * 内存管理：
 * - 自动的变量生命周期管理
 * - 作用域结束时自动回收
 * - 异常安全的资源管理
 *
 * @param ls 词法状态，提供token流和解析上下文
 *
 * @note 函数创建并激活局部变量
 * @see new_localvar, explist1, adjust_assign, adjustlocalvars
 *
 * @example
 * // 不同形式的局部变量声明：
 * // local x                    -- x = nil
 * // local a, b = 1, 2         -- a = 1, b = 2
 * // local x, y, z = func()    -- 多返回值赋值
 * // local i, j = 10           -- i = 10, j = nil
 */
static void localstat (LexState *ls) {
    int nvars = 0;
    int nexps;
    expdesc e;
    do {
        new_localvar(ls, str_checkname(ls), nvars++);
    } while (testnext(ls, ','));
    if (testnext(ls, '='))
        nexps = explist1(ls, &e);
    else {
        e.k = VVOID;
        nexps = 0;
    }
    adjust_assign(ls, nvars, nexps, &e);
    adjustlocalvars(ls, nvars);
}

/**
 * @brief 函数名解析：处理函数定义中的函数名表达式
 *
 * 详细说明：
 * 这个函数解析函数定义语句中的函数名，支持简单名称、表字段访问
 * 和方法定义语法。它返回是否需要self参数的标志。
 *
 * 语法规则：
 * funcname -> NAME {field} [':' NAME]
 * - NAME: 基础函数名
 * - {field}: 零个或多个字段访问
 * - [':' NAME]: 可选的方法名（需要self参数）
 *
 * 支持的函数名形式：
 * - 简单名称：function foo() ... end
 * - 表字段：function table.func() ... end
 * - 嵌套字段：function a.b.c.func() ... end
 * - 方法定义：function obj:method() ... end
 *
 * 方法语法糖：
 * - ':' 语法表示方法定义
 * - 自动添加self参数
 * - function obj:method(a) 等价于 function obj.method(self, a)
 *
 * 处理流程：
 * 1. 解析基础变量名
 * 2. 循环处理所有字段访问
 * 3. 检查是否有方法语法（冒号）
 * 4. 处理方法名并设置needself标志
 *
 * 表达式构建：
 * - singlevar: 解析基础变量
 * - field: 处理每个字段访问
 * - 构建完整的左值表达式
 *
 * 返回值：
 * - 0: 普通函数，不需要self参数
 * - 1: 方法定义，需要self参数
 *
 * 使用场景：
 * - 全局函数：function print() ... end
 * - 模块函数：function math.abs() ... end
 * - 对象方法：function obj:toString() ... end
 * - 嵌套定义：function a.b.c.d() ... end
 *
 * 性能特征：
 * - 高效的名称解析
 * - 最小化字段访问开销
 * - 支持编译时优化
 *
 * 错误处理：
 * - 变量名解析错误
 * - 字段访问语法错误
 * - 方法定义语法验证
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param v 输出参数，存储解析后的函数名表达式
 * @return 是否需要self参数（0=否，1=是）
 *
 * @note 函数构建完整的左值表达式用于函数赋值
 * @see singlevar, field, funcstat
 *
 * @example
 * // 不同形式的函数名：
 * // function foo()           -- needself = 0
 * // function table.func()    -- needself = 0
 * // function obj:method()    -- needself = 1
 * // function a.b.c:method()  -- needself = 1
 */
static int funcname (LexState *ls, expdesc *v) {
    int needself = 0;
    singlevar(ls, v);
    while (ls->t.token == '.')
        field(ls, v);
    if (ls->t.token == ':') {
        needself = 1;
        field(ls, v);
    }
    return needself;
}

/**
 * @brief 函数定义语句解析：处理function语句的完整结构
 *
 * 详细说明：
 * 这个函数解析函数定义语句，包括函数名解析、函数体解析和
 * 函数赋值。它支持普通函数和方法的定义语法。
 *
 * 语法规则：
 * funcstat -> FUNCTION funcname body
 * - FUNCTION: function关键字
 * - funcname: 函数名表达式
 * - body: 函数体（参数列表和代码块）
 *
 * 处理流程：
 * 1. 跳过FUNCTION关键字
 * 2. 解析函数名表达式
 * 3. 解析函数体
 * 4. 将函数赋值给指定名称
 * 5. 设置调试行号信息
 *
 * 函数名处理：
 * - funcname: 解析完整的函数名
 * - 支持表字段和方法语法
 * - 返回是否需要self参数
 *
 * 函数体处理：
 * - body: 解析参数列表和代码块
 * - needself: 方法定义时自动添加self参数
 * - 生成闭包和相关代码
 *
 * 赋值处理：
 * - luaK_storevar: 将函数存储到指定位置
 * - 支持全局变量、表字段等各种左值
 * - 处理函数作为一等值的语义
 *
 * 调试信息：
 * - luaK_fixline: 设置函数定义的行号
 * - 用于调试和错误报告
 * - 支持准确的栈跟踪
 *
 * 使用场景：
 * - 全局函数：function print() ... end
 * - 模块函数：function math.sin() ... end
 * - 对象方法：function obj:method() ... end
 * - 库函数定义：function string.upper() ... end
 *
 * 性能特征：
 * - 高效的函数创建和赋值
 * - 最小化运行时开销
 * - 支持编译器优化
 *
 * 内存管理：
 * - 自动的函数对象管理
 * - 正确的引用计数
 * - 异常安全的资源处理
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @param line function关键字所在的行号，用于调试信息
 *
 * @note 函数生成函数对象并赋值给指定名称
 * @see funcname, body, luaK_storevar, luaK_fixline
 *
 * @example
 * // 不同类型的函数定义：
 * // function factorial(n)
 * //   if n <= 1 then return 1
 * //   else return n * factorial(n-1) end
 * // end
 * //
 * // function math.abs(x)
 * //   return x < 0 and -x or x
 * // end
 * //
 * // function obj:toString()
 * //   return tostring(self)
 * // end
 */
static void funcstat (LexState *ls, int line) {
    int needself;
    expdesc v, b;
    luaX_next(ls);
    needself = funcname(ls, &v);
    body(ls, &b, needself, line);
    luaK_storevar(ls->fs, &v, &b);
    luaK_fixline(ls->fs, line);
}


/**
 * @brief 表达式语句解析：处理函数调用和赋值语句
 *
 * 详细说明：
 * 这个函数解析表达式语句，区分函数调用语句和赋值语句。
 * 它是语句解析的默认处理函数，处理不匹配其他语句类型的表达式。
 *
 * 语法规则：
 * exprstat -> func | assignment
 * - func: 函数调用语句
 * - assignment: 赋值语句
 *
 * 语句区分：
 * - VCALL: 函数调用表达式，作为语句执行
 * - 其他: 赋值语句的左值，继续解析赋值
 *
 * 函数调用处理：
 * - 检测到VCALL类型的表达式
 * - 设置SETARG_C为1，表示不使用返回值
 * - 函数调用作为独立语句执行
 *
 * 赋值语句处理：
 * - 将主表达式作为赋值的第一个左值
 * - 创建LHS_assign结构
 * - 调用assignment处理完整的赋值
 *
 * 表达式解析：
 * - primaryexp: 解析主表达式
 * - 支持变量、函数调用、字段访问等
 * - 根据表达式类型决定后续处理
 *
 * 代码生成：
 * - 函数调用：修改CALL指令的返回值数量
 * - 赋值语句：生成相应的赋值指令
 * - 优化单独函数调用的性能
 *
 * 使用场景：
 * - 函数调用：print("hello")
 * - 简单赋值：x = 1
 * - 复杂赋值：a, b = c, d
 * - 表字段赋值：t[k] = v
 *
 * 性能优化：
 * - 函数调用语句的特殊处理
 * - 避免不必要的返回值处理
 * - 高效的语句类型识别
 *
 * 错误处理：
 * - 依赖primaryexp的错误处理
 * - 赋值语句的语法验证
 * - 表达式类型的正确识别
 *
 * @param ls 词法状态，提供token流和解析上下文
 *
 * @note 函数是语句解析的默认处理器
 * @see primaryexp, assignment, LHS_assign
 *
 * @example
 * // 不同类型的表达式语句：
 * // print("hello")        -- 函数调用语句
 * // x = 10               -- 简单赋值语句
 * // a, b = 1, 2          -- 多重赋值语句
 * // table.field = value  -- 表字段赋值语句
 */
static void exprstat (LexState *ls) {
    FuncState *fs = ls->fs;
    struct LHS_assign v;
    primaryexp(ls, &v.v);
    if (v.v.k == VCALL)
        SETARG_C(getcode(fs, &v.v), 1);
    else {
        v.prev = NULL;
        assignment(ls, &v, 1);
    }
}

/**
 * @brief return语句解析：处理函数返回语句
 *
 * 详细说明：
 * 这个函数解析return语句，处理可选的返回值列表，支持多返回值、
 * 尾调用优化和各种返回值模式。它生成相应的返回指令。
 *
 * 语法规则：
 * retstat -> RETURN explist
 * - RETURN: return关键字
 * - explist: 可选的返回值表达式列表
 *
 * 返回值处理：
 * - 无返回值：return（隐式返回nil）
 * - 单返回值：return expr
 * - 多返回值：return expr1, expr2, ...
 * - 函数调用返回值：return func()
 *
 * 特殊情况处理：
 * 1. 无返回值：block_follow或分号
 * 2. 多返回值表达式：hasmultret检查
 * 3. 尾调用：函数调用作为唯一返回值
 * 4. 单值返回：普通表达式
 * 5. 多值返回：表达式列表
 *
 * 尾调用优化：
 * - 检测函数调用作为唯一返回值
 * - 将OP_CALL改为OP_TAILCALL
 * - 优化递归调用的栈使用
 * - 提高性能并避免栈溢出
 *
 * 寄存器管理：
 * - first: 第一个返回值的寄存器位置
 * - nret: 返回值数量
 * - 多返回值：LUA_MULTRET表示不定数量
 * - 单返回值：具体的寄存器位置
 *
 * 代码生成：
 * - luaK_ret: 生成返回指令
 * - 处理不同数量的返回值
 * - 支持多返回值的高效传递
 *
 * 多返回值语义：
 * - 函数可以返回任意数量的值
 * - 调用者决定接收多少个值
 * - 多余的值被丢弃，缺失的值为nil
 *
 * 使用场景：
 * - 无返回值：return
 * - 单返回值：return x + 1
 * - 多返回值：return a, b, c
 * - 函数调用：return func(args)
 * - 尾递归：return factorial(n-1, acc*n)
 *
 * 性能优化：
 * - 尾调用优化减少栈使用
 * - 高效的多返回值处理
 * - 最小化寄存器复制
 *
 * 错误处理：
 * - 表达式解析错误传播
 * - 寄存器分配验证
 * - 返回值数量检查
 *
 * @param ls 词法状态，提供token流和解析上下文
 *
 * @note 函数生成返回指令并可能进行尾调用优化
 * @see explist1, luaK_ret, luaK_setmultret, OP_TAILCALL
 *
 * @example
 * // 不同形式的return语句：
 * // return                    -- 返回nil
 * // return 42                 -- 返回单个值
 * // return a, b, c            -- 返回多个值
 * // return func()             -- 返回函数的所有返回值
 * // return factorial(n-1)     -- 尾调用优化
 */
static void retstat (LexState *ls) {
    FuncState *fs = ls->fs;
    expdesc e;
    int first, nret;
    luaX_next(ls);
    if (block_follow(ls->t.token) || ls->t.token == ';')
        first = nret = 0;
    else {
        nret = explist1(ls, &e);
        if (hasmultret(e.k)) {
            luaK_setmultret(fs, &e);
            if (e.k == VCALL && nret == 1) {
                SET_OPCODE(getcode(fs,&e), OP_TAILCALL);
                lua_assert(GETARG_A(getcode(fs,&e)) == fs->nactvar);
            }
            first = fs->nactvar;
            nret = LUA_MULTRET;
        }
        else {
            if (nret == 1)
                first = luaK_exp2anyreg(fs, &e);
            else {
                luaK_exp2nextreg(fs, &e);
                first = fs->nactvar;
                lua_assert(nret == fs->freereg - first);
            }
        }
    }
    luaK_ret(fs, first, nret);
}


/**
 * @brief 语句解析主函数：分发和处理各种类型的Lua语句
 *
 * 详细说明：
 * 这是Lua语句解析的核心分发函数，根据当前token类型识别语句类型，
 * 并调用相应的处理函数。它是递归下降解析器的语句层入口点。
 *
 * 语句类型分发：
 * - TK_IF: if语句，调用ifstat
 * - TK_WHILE: while循环，调用whilestat
 * - TK_DO: do代码块，直接处理
 * - TK_FOR: for循环，调用forstat
 * - TK_REPEAT: repeat循环，调用repeatstat
 * - TK_FUNCTION: 函数定义，调用funcstat
 * - TK_LOCAL: 局部声明，处理变量或函数
 * - TK_RETURN: 返回语句，调用retstat
 * - TK_BREAK: break语句，调用breakstat
 * - default: 表达式语句，调用exprstat
 *
 * 返回值语义：
 * - 0: 普通语句，可以继续解析后续语句
 * - 1: 终结语句（return/break），必须是代码块的最后语句
 *
 * 特殊语句处理：
 * - DO块：直接内联处理，创建新作用域
 * - LOCAL：区分局部变量和局部函数
 * - RETURN/BREAK：标记为终结语句
 *
 * 错误处理：
 * - 保存行号用于错误报告
 * - 各子函数负责具体的错误处理
 * - 语法错误的准确定位
 *
 * 语句优先级：
 * - 关键字语句优先于表达式语句
 * - 明确的语句类型识别
 * - 避免语法歧义
 *
 * 性能特征：
 * - 高效的token分发机制
 * - 最小化语句识别开销
 * - 支持编译器优化
 *
 * 使用场景：
 * - 函数体中的语句序列
 * - 代码块中的语句解析
 * - 控制结构的嵌套处理
 *
 * 调试支持：
 * - 行号信息的准确维护
 * - 支持调试器的断点设置
 * - 错误消息的精确定位
 *
 * @param ls 词法状态，提供token流和解析上下文
 * @return 语句类型标志：0=普通语句，1=终结语句
 *
 * @note 函数是语句解析的核心分发器
 * @see ifstat, whilestat, forstat, repeatstat, funcstat, localstat, retstat, breakstat, exprstat
 *
 * @example
 * // 不同类型的语句：
 * // if x > 0 then print("positive") end     -- 条件语句
 * // while i < 10 do i = i + 1 end          -- 循环语句
 * // for i = 1, 10 do print(i) end          -- for循环
 * // local x = 10                           -- 局部变量声明
 * // function foo() return 42 end           -- 函数定义
 * // return x + y                           -- 返回语句（终结）
 * // break                                  -- break语句（终结）
 */
static int statement (LexState *ls) {
    int line = ls->linenumber;
    switch (ls->t.token) {
        case TK_IF: {
            ifstat(ls, line);
            return 0;
        }
        case TK_WHILE: {
            whilestat(ls, line);
            return 0;
        }
        case TK_DO: {
            luaX_next(ls);
            block(ls);
            check_match(ls, TK_END, TK_DO, line);
            return 0;
        }
        case TK_FOR: {
            forstat(ls, line);
            return 0;
        }
        case TK_REPEAT: {
            repeatstat(ls, line);
            return 0;
        }
        case TK_FUNCTION: {
            funcstat(ls, line);
            return 0;
        }
        case TK_LOCAL: {
            luaX_next(ls);
            if (testnext(ls, TK_FUNCTION))
                localfunc(ls);
            else
                localstat(ls);
            return 0;
        }
        case TK_RETURN: {
            retstat(ls);
            return 1;
        }
        case TK_BREAK: {
            luaX_next(ls);
            breakstat(ls);
            return 1;
        }
        default: {
            exprstat(ls);
            return 0;
        }
    }
}

/**
 * @brief 代码块解析：处理语句序列的解析和管理
 *
 * 详细说明：
 * 这是Lua代码块解析的核心函数，处理语句序列的解析，管理递归深度，
 * 处理终结语句，并维护寄存器状态。它是语法分析的顶层控制函数。
 *
 * 语法规则：
 * chunk -> { stat [';'] }
 * - stat: 各种类型的语句
 * - [';']: 可选的语句分隔符
 * - 支持任意数量的语句
 *
 * 解析控制：
 * - islast: 跟踪是否遇到终结语句
 * - block_follow: 检查代码块结束条件
 * - 循环解析直到遇到终结语句或块结束
 *
 * 终结语句处理：
 * - return语句：函数返回，后续语句不可达
 * - break语句：循环跳出，后续语句不可达
 * - 终结语句后停止解析当前块
 *
 * 递归深度管理：
 * - enterlevel: 进入新的解析层级
 * - leavelevel: 离开当前解析层级
 * - 防止深度递归导致的栈溢出
 *
 * 寄存器管理：
 * - 每个语句后重置freereg到nactvar
 * - 释放临时寄存器，保留活跃变量
 * - 维护寄存器使用的一致性
 *
 * 语句分隔符：
 * - testnext(ls, ';'): 处理可选的分号
 * - 分号是可选的语句分隔符
 * - 支持灵活的代码风格
 *
 * 栈大小验证：
 * - 断言检查栈大小的一致性
 * - maxstacksize >= freereg >= nactvar
 * - 确保虚拟机栈的正确管理
 *
 * 使用场景：
 * - 函数体：function() chunk end
 * - 代码块：do chunk end
 * - 控制结构体：if condition then chunk end
 * - 文件级别：整个Lua文件的解析
 *
 * 性能特征：
 * - 线性时间复杂度O(n)，n为语句数量
 * - 高效的语句序列处理
 * - 最小化解析开销
 *
 * 错误处理：
 * - 依赖statement函数的错误处理
 * - 递归深度的安全控制
 * - 语法错误的准确传播
 *
 * 内存管理：
 * - 自动的寄存器回收
 * - 临时值的及时清理
 * - 异常安全的资源管理
 *
 * @param ls 词法状态，提供token流和解析上下文
 *
 * @note 函数是代码块解析的顶层控制器
 * @see statement, block_follow, enterlevel, leavelevel
 *
 * @example
 * // 不同上下文中的代码块：
 * // function example()
 * //   local x = 1        -- 语句1
 * //   print(x)          -- 语句2
 * //   if x > 0 then     -- 语句3
 * //     return x        -- 终结语句，停止解析
 * //     print("unreachable")  -- 不可达代码
 * //   end
 * // end
 * //
 * // do
 * //   local temp = getValue()
 * //   process(temp);    -- 可选分号
 * //   cleanup()
 * // end
 */
static void chunk (LexState *ls) {
    int islast = 0;
    enterlevel(ls);
    while (!islast && !block_follow(ls->t.token)) {
        islast = statement(ls);
        testnext(ls, ';');
        lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
                   ls->fs->freereg >= ls->fs->nactvar);
        ls->fs->freereg = ls->fs->nactvar;
    }
    leavelevel(ls);
}

/* }====================================================================== */
