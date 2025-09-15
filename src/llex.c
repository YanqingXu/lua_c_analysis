/**
 * @file llex.c
 * @brief Lua词法分析器：高性能字符流到Token流转换系统
 *
 * 详细说明：
 * 本文件实现了Lua编程语言的词法分析器（Lexical Analyzer），负责将
 * 源代码字符流转换为结构化的Token流，为语法分析器提供输入。这是
 * Lua编译器前端的第一个处理阶段，直接影响整个编译过程的性能。
 *
 * 系统架构定位：
 * 词法分析器位于Lua编译器架构的最前端，接收原始源代码文本，输出
 * 标准化的Token序列。它与输入抽象层（ZIO）紧密集成，与语法分析器
 * （Parser）协同工作，是编译器管道中的关键组件。
 *
 * 核心设计理念：
 * 1. 状态机驱动：基于有限状态自动机的字符识别和Token构建
 * 2. 流式处理：逐字符处理，支持大文件的内存高效处理
 * 3. 错误恢复：精确的错误定位和友好的错误报告机制
 * 4. 性能优化：针对常见Token类型的快速路径优化
 * 5. 扩展性：支持长字符串、数值格式、转义序列等复杂语法元素
 *
 * 技术特色：
 * - 统一Token接口：所有Token类型通过统一的接口处理
 * - 动态缓冲区：自适应的Token内容缓冲区管理
 * - 本地化支持：支持不同locale的数值格式（小数点分隔符）
 * - 兼容性处理：支持Lua 5.1的长字符串兼容性选项
 * - 预读机制：支持一个Token的预读，简化语法分析
 *
 * 支持的Token类型：
 * - 保留字：and, break, do, else, elseif, end, false, for, function等
 * - 操作符：+, -, *, /, %, ^, ==, ~=, <=, >=, <, >, =, ..等
 * - 字面量：数值常量、字符串常量（支持转义序列）
 * - 标识符：变量名、函数名等用户定义标识符
 * - 分隔符：括号、逗号、分号等语法分隔符
 * - 特殊Token：文件结束符、换行符等控制Token
 *
 * 性能优化策略：
 * - 字符分类缓存：利用ctype.h函数的高效字符分类
 * - 分支预测优化：将常见情况放在switch语句的前面
 * - 内存局部性：紧凑的数据结构布局，提高缓存命中率
 * - 最小化分配：重用缓冲区，减少动态内存分配
 * - 快速路径：为常见Token类型提供优化的处理路径
 *
 * 内存管理策略：
 * - 统一分配：通过Lua的内存管理系统分配所有内存
 * - 动态扩展：Token缓冲区根据需要自动扩展
 * - 垃圾回收集成：新创建的字符串自动纳入GC管理
 * - 内存安全：严格的边界检查，防止缓冲区溢出
 *
 * 线程安全性：
 * 词法分析器设计为单线程使用，每个LexState实例绑定到特定的
 * lua_State，不支持多线程并发访问。在多线程环境中，每个线程
 * 应该使用独立的LexState实例。
 *
 * @author Roberto Ierusalimschy, Waldemar Celes, Luiz Henrique de Figueiredo
 * @version 5.1.5
 * @date 2009-11-23
 * @since Lua 5.0
 * @see lparser.h, lzio.h, lstring.h
 */

// 标准C库头文件 - 字符处理和本地化支持
#include <ctype.h>      // 字符分类函数：isdigit, isalpha, isalnum等
#include <locale.h>     // 本地化支持：localeconv获取小数点分隔符
#include <string.h>     // 字符串处理函数：strlen, strchr等

// Lua模块标识符定义
#define llex_c          // 标识当前编译单元为词法分析器模块
#define LUA_CORE        // 标识这是Lua核心模块，可以访问内部API

// Lua核心头文件
#include "lua.h"        // Lua主头文件：基本类型和常量定义

// Lua内部模块头文件 - 按依赖关系排序
#include "ldo.h"        // 执行控制：错误处理和异常传播
#include "llex.h"       // 词法分析器接口：LexState和Token定义
#include "lobject.h"    // 对象系统：TValue, TString等核心类型
#include "lparser.h"    // 语法分析器接口：与Parser的协作接口
#include "lstate.h"     // Lua状态机：lua_State和全局状态管理
#include "lstring.h"    // 字符串管理：字符串创建和内部化
#include "ltable.h"     // 表操作：用于字符串常量池管理
#include "lzio.h"       // 输入抽象层：统一的输入流接口

/**
 * @brief 读取下一个字符的宏定义
 *
 * 这个宏通过ZIO输入抽象层读取下一个字符，并更新词法分析器的
 * 当前字符状态。这是词法分析器中最频繁使用的操作之一。
 *
 * 实现细节：
 * - 调用zgetc()从输入流读取字符
 * - 将读取的字符存储在ls->current中
 * - 支持文件结束符(EOZ)的检测
 * - 通过函数指针实现输入源的抽象
 *
 * 性能考虑：
 * - 使用宏避免函数调用开销
 * - 直接操作LexState结构体成员
 * - 与ZIO缓冲机制协同工作
 *
 * @param ls 词法分析器状态指针
 * @return 读取的字符（通过ls->current访问）
 */
#define next(ls) (ls->current = zgetc(ls->z))

/**
 * @brief 检查当前字符是否为换行符的宏定义
 *
 * 这个宏检查当前字符是否为换行符，支持不同平台的换行符格式：
 * - Unix/Linux: \n (LF)
 * - Classic Mac: \r (CR)
 * - Windows: \r\n (CRLF，通过组合处理)
 *
 * 设计理念：
 * - 跨平台兼容性：支持所有主流平台的换行符
 * - 性能优化：使用宏避免函数调用
 * - 简化逻辑：将复杂的换行符检查抽象为简单的宏
 *
 * 使用场景：
 * - 行号计数：在遇到换行符时增加行号
 * - 字符串处理：在字符串中正确处理换行符
 * - 注释跳过：跳过单行注释时检测行结束
 *
 * @param ls 词法分析器状态指针
 * @return 非零值表示当前字符是换行符，零表示不是
 */
#define currIsNewline(ls) (ls->current == '\n' || ls->current == '\r')

/**
 * @brief Lua保留字和操作符的字符串表示数组
 *
 * 这个数组按照特定的顺序存储了所有Lua保留字和多字符操作符的
 * 字符串表示。数组的顺序必须与llex.h中定义的Token枚举值严格对应。
 *
 * 数组结构说明：
 * - 前21个元素：Lua语言的保留字（关键字）
 * - 接下来6个元素：多字符操作符（.., ..., ==, >=, <=, ~=）
 * - 最后4个元素：特殊Token的显示名称
 * - NULL终止符：标记数组结束
 *
 * 保留字列表（按字母顺序排列以便查找）：
 * and, break, do, else, elseif, end, false, for, function, if,
 * in, local, nil, not, or, repeat, return, then, true, until, while
 *
 * 多字符操作符：
 * - ".."：字符串连接操作符
 * - "..."：可变参数操作符
 * - "=="：等于比较操作符
 * - ">="：大于等于比较操作符
 * - "<="：小于等于比较操作符
 * - "~="：不等于比较操作符
 *
 * 特殊Token显示名称：
 * - "<number>"：数值字面量的显示名称
 * - "<name>"：标识符的显示名称
 * - "<string>"：字符串字面量的显示名称
 * - "<eof>"：文件结束符的显示名称
 *
 * 使用场景：
 * - Token到字符串转换：luaX_token2str()函数使用此数组
 * - 错误消息生成：在错误报告中显示Token的可读名称
 * - 调试输出：在调试模式下显示Token信息
 * - 保留字识别：在词法分析过程中识别保留字
 *
 * 注意事项：
 * - 数组顺序不能随意更改，必须与Token枚举对应
 * - 保留字字符串用于初始化字符串表中的保留字标记
 * - 数组是const类型，在程序运行期间不会被修改
 */
const char *const luaX_tokens[] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "if",
    "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
    "..", "...", "==", ">=", "<=", "~=",
    "<number>", "<name>", "<string>", "<eof>",
    NULL
};

/**
 * @brief 保存当前字符并读取下一个字符的复合宏
 *
 * 这个宏将两个常用操作组合在一起：
 * 1. 将当前字符保存到Token缓冲区
 * 2. 读取下一个字符
 *
 * 这是词法分析器中非常频繁的操作模式，特别是在构建Token内容时。
 * 通过宏的方式实现可以避免重复代码，提高代码的可读性和维护性。
 *
 * 实现原理：
 * - 使用C语言的逗号操作符将两个操作组合
 * - 先执行save(ls, ls->current)保存当前字符
 * - 再执行next(ls)读取下一个字符
 * - 整个表达式的值是next(ls)的返回值
 *
 * 使用场景：
 * - 标识符识别：逐字符构建标识符内容
 * - 字符串解析：逐字符构建字符串内容
 * - 数值解析：逐字符构建数值字面量
 * - 操作符识别：构建多字符操作符
 *
 * 性能优势：
 * - 避免函数调用开销
 * - 减少代码重复
 * - 提高编译器优化效果
 *
 * @param ls 词法分析器状态指针
 */
#define save_and_next(ls) (save(ls, ls->current), next(ls))

/**
 * @brief 将字符保存到Token缓冲区的核心函数
 *
 * 这个函数负责将字符添加到词法分析器的Token构建缓冲区中。
 * 它实现了动态缓冲区管理，当缓冲区空间不足时自动扩展。
 *
 * 缓冲区管理策略：
 * - 初始缓冲区大小由LUA_MINBUFFER定义
 * - 当空间不足时，缓冲区大小翻倍（倍增策略）
 * - 最大缓冲区大小受MAX_SIZET/2限制，防止整数溢出
 * - 使用luaZ_resizebuffer()进行内存重分配
 *
 * 错误处理：
 * - 当Token长度超过最大限制时，抛出词法错误
 * - 错误消息："lexical element too long"
 * - 通过luaX_lexerror()报告错误并终止分析
 *
 * 性能优化：
 * - 倍增策略减少重分配次数，平摊时间复杂度为O(1)
 * - 直接操作缓冲区指针，避免额外的函数调用
 * - 使用cast宏确保类型安全的字符转换
 *
 * 内存安全：
 * - 严格的边界检查，防止缓冲区溢出
 * - 整数溢出保护，防止恶意输入导致的安全问题
 * - 与Lua内存管理系统集成，支持内存限制和统计
 *
 * 算法复杂度：
 * - 时间复杂度：平摊O(1)，最坏情况O(n)（重分配时）
 * - 空间复杂度：O(n)，其中n为Token的最大长度
 *
 * @param[in,out] ls 词法分析器状态指针，包含缓冲区信息
 * @param[in] c 要保存的字符，将被转换为char类型
 *
 * @pre ls != NULL && ls->buff != NULL
 * @post 字符c被添加到缓冲区末尾，缓冲区长度增加1
 *
 * @throws 当Token长度超过MAX_SIZET/2时抛出词法错误
 *
 * @see luaZ_resizebuffer(), luaX_lexerror()
 */
static void save(LexState *ls, int c) {
    Mbuffer *b = ls->buff;

    if (b->n + 1 > b->buffsize) {
        size_t newsize;

        if (b->buffsize >= MAX_SIZET/2) {
            luaX_lexerror(ls, "lexical element too long", 0);
        }

        newsize = b->buffsize * 2;
        luaZ_resizebuffer(ls->L, b, newsize);
    }

    b->buffer[b->n++] = cast(char, c);
}

/**
 * @brief 初始化Lua词法分析器的保留字系统
 *
 * 这个函数在Lua状态机初始化时被调用，负责将所有保留字注册到
 * 字符串表中，并标记它们为保留字，使其永远不会被垃圾回收。
 *
 * 初始化过程：
 * 1. 遍历luaX_tokens数组中的所有保留字
 * 2. 为每个保留字创建TString对象
 * 3. 调用luaS_fix()防止保留字被垃圾回收
 * 4. 设置字符串的reserved字段，标识其为保留字
 * 5. 验证保留字长度不超过TOKEN_LEN限制
 *
 * 保留字标记机制：
 * - reserved字段值 = Token枚举值 + 1
 * - 值为0表示普通字符串，非0表示保留字
 * - 通过reserved值可以直接获取对应的Token类型
 *
 * 内存管理：
 * - 保留字字符串被标记为"固定"，永不回收
 * - 这确保了保留字在整个Lua生命周期内都可用
 * - 避免了重复创建保留字字符串的开销
 *
 * 性能优化：
 * - 预先创建所有保留字，避免运行时查找开销
 * - 保留字识别通过简单的字段检查完成，O(1)时间复杂度
 * - 字符串内部化机制确保相同字符串只存储一份
 *
 * 错误检查：
 * - 使用lua_assert验证保留字长度限制
 * - 确保所有保留字都能正确存储在Token缓冲区中
 *
 * @param[in] L Lua状态机指针，用于字符串创建和内存管理
 *
 * @pre L != NULL，Lua状态机已正确初始化
 * @post 所有保留字已注册到字符串表并标记为固定
 *
 * @note 此函数只应在Lua状态机初始化时调用一次
 * @see luaS_new(), luaS_fix(), NUM_RESERVED, TOKEN_LEN
 */
void luaX_init(lua_State *L) {
    int i;

    // 遍历所有保留字：NUM_RESERVED定义了保留字的数量
    for (i = 0; i < NUM_RESERVED; i++) {
        // 为保留字创建TString对象：使用字符串内部化机制
        TString *ts = luaS_new(L, luaX_tokens[i]);

        // 标记为固定字符串：防止垃圾回收器回收保留字
        luaS_fix(ts);

        // 验证保留字长度：确保不超过TOKEN_LEN限制
        // +1是为了包含字符串终止符'\0'
        lua_assert(strlen(luaX_tokens[i]) + 1 <= TOKEN_LEN);

        // 设置保留字标记：reserved值 = Token枚举值 + 1
        // +1是因为0表示非保留字，从1开始表示保留字类型
        ts->tsv.reserved = cast_byte(i + 1);
    }
}

/**
 * @brief 错误消息中源代码位置信息的最大长度
 *
 * 这个常量定义了在错误消息中显示的源代码文件名或代码块标识的
 * 最大字符数。当源代码标识超过此长度时，会被截断并添加省略号。
 *
 * 设计考虑：
 * - 80字符是传统终端的标准宽度
 * - 为错误消息的其他部分（行号、错误描述等）预留空间
 * - 平衡信息完整性和显示美观性
 *
 * 使用场景：
 * - luaO_chunkid()函数使用此值截断源代码标识
 * - 错误消息格式化时的缓冲区大小计算
 * - 调试信息显示时的长度限制
 */
#define MAXSRC 80

/**
 * @brief 将Token类型转换为可读字符串表示
 *
 * 这个函数将Token的数值类型转换为人类可读的字符串表示，
 * 主要用于错误消息生成和调试输出。它能够处理所有类型的Token，
 * 包括单字符Token、保留字和特殊Token。
 *
 * Token类型处理：
 * 1. 单字符Token（< FIRST_RESERVED）：
 *    - 控制字符：显示为"char(数值)"格式
 *    - 可打印字符：直接显示字符本身
 * 2. 保留字和多字符操作符（>= FIRST_RESERVED）：
 *    - 从luaX_tokens数组中获取对应的字符串
 *
 * 字符处理逻辑：
 * - 使用iscntrl()检测控制字符（ASCII 0-31和127）
 * - 控制字符以"char(n)"格式显示，便于调试
 * - 可打印字符直接以字符形式显示
 *
 * 内存管理：
 * - 使用luaO_pushfstring()在Lua栈上创建格式化字符串
 * - 返回的字符串由Lua的垃圾回收器管理
 * - 调用者不需要手动释放返回的字符串
 *
 * 性能考虑：
 * - 对于保留字，直接返回预存储的字符串指针
 * - 对于单字符Token，只在需要时才创建格式化字符串
 * - 使用lua_assert进行类型检查，在发布版本中被优化掉
 *
 * @param[in] ls 词法分析器状态指针，用于访问Lua状态机
 * @param[in] token Token类型值，可以是字符值或Token枚举值
 *
 * @return 指向Token字符串表示的指针，由Lua GC管理
 * @retval "char(n)" 控制字符的格式化表示
 * @retval "c" 可打印单字符的直接表示
 * @retval "keyword" 保留字或操作符的字符串表示
 *
 * @pre ls != NULL && ls->L != NULL
 * @post 返回值指向有效的以null结尾的字符串
 *
 * @see luaO_pushfstring(), iscntrl(), luaX_tokens[]
 */
const char *luaX_token2str(LexState *ls, int token) {
    // 处理单字符Token：ASCII字符和操作符
    if (token < FIRST_RESERVED) {
        // 类型安全检查：确保token值可以安全转换为unsigned char
        lua_assert(token == cast(unsigned char, token));

        // 根据字符类型选择显示格式
        return (iscntrl(token)) ?
            // 控制字符：显示为"char(数值)"格式，便于调试识别
            luaO_pushfstring(ls->L, "char(%d)", token) :
            // 可打印字符：直接显示字符本身
            luaO_pushfstring(ls->L, "%c", token);
    } else {
        // 处理保留字和多字符操作符：从预定义数组中获取字符串
        // token - FIRST_RESERVED 计算在luaX_tokens数组中的索引
        return luaX_tokens[token - FIRST_RESERVED];
    }
}

/**
 * @brief 获取Token的文本表示用于错误消息显示
 *
 * 这个静态函数为错误消息生成提供Token的文本表示。它处理不同类型
 * 的Token，为用户提供有意义的错误上下文信息。
 *
 * Token类型处理策略：
 * 1. TK_NAME（标识符）：返回实际的标识符文本
 * 2. TK_STRING（字符串）：返回实际的字符串内容
 * 3. TK_NUMBER（数值）：返回实际的数值文本表示
 * 4. 其他Token：使用luaX_token2str()获取标准表示
 *
 * 缓冲区处理：
 * - 对于需要显示内容的Token，在缓冲区末尾添加'\0'终止符
 * - 使用luaZ_buffer()获取缓冲区内容指针
 * - 缓冲区内容在下次Token处理时可能被覆盖
 *
 * 内存安全：
 * - 返回的字符串指针指向内部缓冲区或Lua管理的字符串
 * - 调用者不应修改返回的字符串内容
 * - 字符串的生命周期与LexState或Lua状态机相关
 *
 * 错误消息质量：
 * - 为用户提供具体的Token内容，而不是抽象的类型名
 * - 帮助用户快速定位和理解语法错误
 * - 在复杂表达式中提供精确的错误位置信息
 *
 * @param[in] ls 词法分析器状态指针
 * @param[in] token Token类型值
 *
 * @return 指向Token文本表示的字符串指针
 * @retval 标识符/字符串/数值的实际内容 对于TK_NAME/TK_STRING/TK_NUMBER
 * @retval Token的标准字符串表示 对于其他Token类型
 *
 * @pre ls != NULL && ls->buff != NULL
 * @post 返回值指向有效的以null结尾的字符串
 *
 * @see luaX_token2str(), luaZ_buffer()
 */
static const char *txtToken(LexState *ls, int token) {
    switch (token) {
        case TK_NAME:       // 标识符Token
        case TK_STRING:     // 字符串字面量Token
        case TK_NUMBER:     // 数值字面量Token
            // 在缓冲区末尾添加字符串终止符
            save(ls, '\0');
            // 返回缓冲区内容：包含Token的实际文本
            return luaZ_buffer(ls->buff);

        default:
            // 其他Token类型：返回标准的字符串表示
            return luaX_token2str(ls, token);
    }
}

/**
 * @brief 报告词法分析错误并终止分析过程
 *
 * 这个函数是词法分析器的核心错误处理机制，负责生成详细的错误消息
 * 并通过Lua的异常系统终止当前的分析过程。它提供了丰富的上下文信息
 * 帮助用户定位和理解错误。
 *
 * 错误消息格式：
 * "源文件:行号: 错误消息 [near 'Token内容']"
 *
 * 错误消息组成：
 * 1. 源代码标识：通过luaO_chunkid()生成，限制在MAXSRC字符内
 * 2. 行号信息：当前词法分析器所在的行号
 * 3. 错误描述：调用者提供的具体错误消息
 * 4. Token上下文：可选的Token内容，帮助定位错误位置
 *
 * 源代码标识处理：
 * - 文件名：显示为文件路径（可能被截断）
 * - 字符串代码：显示为[string "..."]格式
 * - 其他源：显示为相应的标识格式
 *
 * Token上下文处理：
 * - 当token参数非零时，在错误消息中包含Token内容
 * - 使用LUA_QS宏格式化Token内容，通常添加引号
 * - 提供"near 'Token'"格式的上下文信息
 *
 * 异常处理：
 * - 使用luaD_throw()抛出LUA_ERRSYNTAX类型的异常
 * - 异常会被上层的错误处理机制捕获
 * - 函数不会正常返回，总是通过异常退出
 *
 * 内存管理：
 * - 错误消息字符串在Lua栈上创建
 * - 由Lua的垃圾回收器管理内存
 * - 异常抛出前确保栈状态正确
 *
 * 调试支持：
 * - 提供精确的行号信息，便于调试
 * - 包含源代码上下文，帮助理解错误环境
 * - 支持各种类型的源代码输入（文件、字符串等）
 *
 * @param[in] ls 词法分析器状态指针，包含错误位置信息
 * @param[in] msg 错误消息字符串，描述具体的错误类型
 * @param[in] token 可选的Token值，为0时不显示Token上下文
 *
 * @pre ls != NULL && ls->L != NULL && msg != NULL
 * @post 函数不会正常返回，总是通过异常退出
 *
 * @throws LUA_ERRSYNTAX 语法错误异常，包含格式化的错误消息
 *
 * @see luaO_chunkid(), luaO_pushfstring(), luaD_throw(), txtToken()
 */
void luaX_lexerror(LexState *ls, const char *msg, int token) {
    char buff[MAXSRC];      // 源代码标识缓冲区

    // 生成源代码标识：文件名或代码块描述，限制长度为MAXSRC
    luaO_chunkid(buff, getstr(ls->source), MAXSRC);

    // 构建基本错误消息：包含源标识、行号和错误描述
    msg = luaO_pushfstring(ls->L, "%s:%d: %s", buff, ls->linenumber, msg);

    // 如果提供了Token上下文，添加"near 'Token'"信息
    if (token) {
        luaO_pushfstring(ls->L, "%s near " LUA_QS, msg, txtToken(ls, token));
    }

    // 抛出语法错误异常：终止词法分析过程
    luaD_throw(ls->L, LUA_ERRSYNTAX);
}

/**
 * @brief 报告语法错误的便捷函数
 *
 * 这个函数是luaX_lexerror()的便捷包装，专门用于报告与当前Token
 * 相关的语法错误。它自动使用当前Token作为错误上下文。
 *
 * 使用场景：
 * - 当前Token不符合语法期望时
 * - 需要快速报告语法错误而不需要指定特定Token时
 * - 简化错误处理代码，避免重复传递当前Token
 *
 * 功能特点：
 * - 自动使用ls->t.token作为错误上下文
 * - 保持与luaX_lexerror()相同的错误消息格式
 * - 提供更简洁的错误报告接口
 *
 * @param[in] ls 词法分析器状态指针
 * @param[in] msg 错误消息字符串
 *
 * @pre ls != NULL && ls->L != NULL && msg != NULL
 * @post 函数不会正常返回，总是通过异常退出
 *
 * @throws LUA_ERRSYNTAX 语法错误异常
 *
 * @see luaX_lexerror()
 */
void luaX_syntaxerror(LexState *ls, const char *msg) {
    luaX_lexerror(ls, msg, ls->t.token);
}

/**
 * @brief 创建新字符串并将其添加到常量池中
 *
 * 这个函数为词法分析器创建新的字符串对象，并将其添加到当前函数
 * 的常量池中。这确保了相同的字符串在编译过程中只存储一份，
 * 同时防止字符串在编译期间被垃圾回收。
 *
 * 字符串处理流程：
 * 1. 使用luaS_newlstr()创建TString对象
 * 2. 将字符串添加到函数的常量表中
 * 3. 如果是新字符串，标记为不可回收
 * 4. 触发垃圾回收检查，保持内存使用平衡
 *
 * 常量池管理：
 * - 使用哈希表存储所有字符串常量
 * - 相同内容的字符串只存储一份（字符串内部化）
 * - 通过luaH_setstr()将字符串添加到常量表
 * - 新字符串被标记为布尔值true，防止回收
 *
 * 内存管理策略：
 * - 字符串对象由Lua的字符串管理系统创建
 * - 常量池中的字符串在编译期间不会被回收
 * - 编译完成后，未使用的字符串可能被垃圾回收
 * - 调用luaC_checkGC()保持内存使用的平衡
 *
 * 性能优化：
 * - 字符串内部化避免重复存储相同内容
 * - 哈希表提供O(1)平均时间复杂度的查找
 * - 减少编译期间的内存分配和释放
 * - 提高运行时字符串比较的效率
 *
 * 垃圾回收集成：
 * - 新创建的字符串自动纳入GC管理
 * - 常量池中的字符串被标记为"活跃"
 * - 定期触发GC检查，防止内存过度使用
 * - 与Lua的增量垃圾回收器协同工作
 *
 * 使用场景：
 * - 标识符名称的存储和管理
 * - 字符串字面量的常量池管理
 * - 保留字和操作符的字符串表示
 * - 错误消息中的字符串内容
 *
 * @param[in] ls 词法分析器状态指针，包含函数状态信息
 * @param[in] str 要创建的字符串内容指针
 * @param[in] l 字符串长度，以字节为单位
 *
 * @return 指向新创建或已存在的TString对象的指针
 * @retval 非NULL 成功创建或找到字符串对象
 * @retval NULL 内存分配失败（通过异常处理）
 *
 * @pre ls != NULL && ls->L != NULL && ls->fs != NULL
 * @pre str != NULL || l == 0
 * @post 返回的字符串已添加到常量池中
 * @post 字符串在编译期间不会被垃圾回收
 *
 * @note 函数可能触发垃圾回收，调用者需要保护重要对象
 * @see luaS_newlstr(), luaH_setstr(), luaC_checkGC()
 */
TString *luaX_newstring(LexState *ls, const char *str, size_t l) {
    lua_State *L = ls->L;
    TString *ts = luaS_newlstr(L, str, l);
    TValue *o = luaH_setstr(L, ls->fs->h, ts);

    if (ttisnil(o)) {
        setbvalue(o, 1);
        luaC_checkGC(L);
    }

    return ts;
}

/**
 * @brief 处理换行符并更新行号计数
 *
 * 这个函数负责处理源代码中的换行符，支持不同平台的换行符格式，
 * 并维护准确的行号计数。这对于错误报告和调试信息至关重要。
 *
 * 支持的换行符格式：
 * - Unix/Linux: \n (LF, Line Feed)
 * - Classic Mac: \r (CR, Carriage Return)
 * - Windows: \r\n (CRLF, Carriage Return + Line Feed)
 * - 混合格式：\n\r（不常见但也支持）
 *
 * 处理算法：
 * 1. 保存当前换行符字符
 * 2. 跳过第一个换行符字符
 * 3. 检查是否为双字符换行符（如\r\n）
 * 4. 如果是不同的换行符，跳过第二个字符
 * 5. 增加行号计数并检查溢出
 *
 * 平台兼容性：
 * - 自动识别不同平台的换行符约定
 * - 正确处理混合换行符格式的文件
 * - 确保跨平台的一致行为
 * - 避免重复计数双字符换行符
 *
 * 错误检测：
 * - 行号溢出检查：防止行号超过MAX_INT
 * - 源文件过大保护：避免处理异常大的文件
 * - 整数溢出安全：确保行号计数的安全性
 *
 * 性能考虑：
 * - 最小化字符读取次数
 * - 避免不必要的函数调用
 * - 直接操作字符，无字符串处理开销
 * - 快速的条件检查逻辑
 *
 * 调试支持：
 * - 提供准确的行号信息用于错误报告
 * - 支持IDE和调试器的行号映射
 * - 便于源代码定位和问题诊断
 *
 * 使用场景：
 * - 词法分析过程中的换行符处理
 * - 字符串字面量中的换行符处理
 * - 注释中的换行符处理
 * - 错误报告中的行号定位
 *
 * 算法复杂度：
 * - 时间复杂度：O(1)，常量时间操作
 * - 空间复杂度：O(1)，只使用常量额外空间
 *
 * @param[in,out] ls 词法分析器状态指针
 *
 * @pre ls != NULL
 * @pre currIsNewline(ls) 当前字符必须是换行符
 *
 * @post ls->linenumber增加1
 * @post 词法分析器位置移动到换行符序列之后
 * @post 如果是双字符换行符，两个字符都被跳过
 *
 * @throws "chunk has too many lines" 当行号超过MAX_INT时
 *
 * @note 函数会修改词法分析器的当前位置和行号状态
 * @see currIsNewline(), next(), MAX_INT定义
 */
static void inclinenumber(LexState *ls) {
    int old = ls->current;

    lua_assert(currIsNewline(ls));

    next(ls);

    if (currIsNewline(ls) && ls->current != old) {
        next(ls);
    }

    if (++ls->linenumber >= MAX_INT) {
        luaX_syntaxerror(ls, "chunk has too many lines");
    }
}

/**
 * @brief 初始化词法分析器的输入源和状态
 *
 * 这个函数负责设置词法分析器的输入源和初始状态，为开始词法分析
 * 做好准备。它是词法分析器的入口点，必须在开始分析前调用。
 *
 * 初始化内容：
 * 1. 输入流设置：关联ZIO输入抽象层
 * 2. 状态重置：清除所有分析状态
 * 3. 缓冲区准备：初始化Token构建缓冲区
 * 4. 行号初始化：设置起始行号
 * 5. 预读状态：清除预读Token状态
 * 6. 小数点设置：初始化本地化设置
 *
 * 参数说明：
 * - L：Lua状态机，用于内存管理和错误处理
 * - ls：词法分析器状态结构，存储所有分析状态
 * - z：ZIO输入流，提供字符输入抽象
 * - source：源代码标识字符串，用于错误报告
 *
 * 状态初始化：
 * - decpoint：小数点分隔符，默认为'.'
 * - L：关联的Lua状态机指针
 * - lookahead：预读Token状态，初始为TK_EOS
 * - z：输入流指针
 * - fs：函数状态指针，初始为NULL
 * - linenumber：当前行号，从1开始
 * - lastline：上一个Token的行号，初始为1
 * - source：源代码标识字符串
 *
 * 缓冲区管理：
 * - 使用luaZ_resizebuffer()初始化Token缓冲区
 * - 设置初始缓冲区大小为LUA_MINBUFFER
 * - 缓冲区会根据需要自动扩展
 * - 与Lua的内存管理系统集成
 *
 * 输入流准备：
 * - 调用next()读取第一个字符
 * - 为后续的词法分析做好准备
 * - 初始化输入流的内部状态
 *
 * 错误处理准备：
 * - 设置源代码标识，用于错误消息
 * - 初始化行号跟踪，用于错误定位
 * - 关联Lua状态机，用于异常处理
 *
 * 使用场景：
 * - 编译器前端初始化
 * - 脚本解析开始前的准备
 * - 交互式解释器的输入设置
 * - 代码分析工具的初始化
 *
 * 性能考虑：
 * - 一次性初始化所有必要状态
 * - 避免重复的状态设置操作
 * - 最小化内存分配次数
 * - 为高效的词法分析做准备
 *
 * 内存安全：
 * - 所有指针都正确初始化
 * - 缓冲区大小设置合理
 * - 与Lua GC系统正确集成
 *
 * @param[in] L Lua状态机指针，用于内存管理和错误处理
 * @param[out] ls 词法分析器状态结构指针，将被初始化
 * @param[in] z ZIO输入流指针，提供字符输入
 * @param[in] source 源代码标识字符串，用于错误报告
 *
 * @pre L != NULL && ls != NULL && z != NULL && source != NULL
 * @pre z已正确初始化并准备好提供输入
 *
 * @post ls完全初始化并准备好进行词法分析
 * @post ls->current包含输入流的第一个字符
 * @post Token缓冲区已分配并准备使用
 *
 * @note 函数可能触发内存分配
 * @see luaZ_resizebuffer(), next(), ZIO输入抽象层
 */
void luaX_setinput(lua_State *L, LexState *ls, ZIO *z, TString *source) {
    ls->decpoint = '.';
    ls->L = L;
    ls->lookahead.token = TK_EOS;
    ls->z = z;
    ls->fs = NULL;
    ls->linenumber = 1;
    ls->lastline = 1;
    ls->source = source;
    luaZ_resizebuffer(ls->L, ls->buff, LUA_MINBUFFER);
    next(ls);
}

/**
 * @brief 检查当前字符是否在指定字符集中，如果是则消费该字符
 *
 * 这个函数是词法分析器中的一个重要工具函数，用于检查当前字符
 * 是否属于指定的字符集合。如果匹配，则保存该字符并移动到下一个字符。
 * 这种模式在解析多字符操作符和数值格式时非常有用。
 *
 * 功能特点：
 * - 条件性消费：只有匹配时才消费字符
 * - 原子操作：检查和消费作为一个原子操作
 * - 灵活匹配：支持任意字符集合的匹配
 * - 高效实现：使用标准库函数strchr()进行快速查找
 *
 * 使用场景：
 * 1. 科学计数法解析：检查'E'或'e'指数标记
 * 2. 符号处理：检查'+'或'-'符号
 * 3. 多字符操作符：检查操作符的后续字符
 * 4. 格式验证：验证特定位置的字符是否符合预期
 *
 * 算法逻辑：
 * 1. 使用strchr()在字符集中查找当前字符
 * 2. 如果找到，保存字符并移动到下一个位置
 * 3. 返回匹配结果（1表示匹配，0表示不匹配）
 *
 * 性能特点：
 * - 时间复杂度：O(n)，其中n为字符集长度（通常很小）
 * - 空间复杂度：O(1)，只使用常量额外空间
 * - 高效查找：strchr()通常有优化实现
 * - 最小开销：只在匹配时才执行保存操作
 *
 * 错误处理：
 * - 无异常抛出：函数总是安全返回
 * - 状态保持：不匹配时词法分析器状态不变
 * - 边界安全：strchr()处理NULL终止符
 *
 * 内存安全：
 * - 字符集参数：假设为有效的C字符串
 * - 缓冲区操作：通过save_and_next()确保安全
 * - 无内存分配：不涉及动态内存操作
 *
 * 使用示例：
 * @code
 * // 检查科学计数法的指数部分
 * if (check_next(ls, "Ee")) {
 *     // 找到指数标记，检查可选的符号
 *     check_next(ls, "+-");
 * }
 *
 * // 检查多字符操作符
 * if (check_next(ls, "=")) {
 *     return TK_EQ;  // ==
 * }
 * @endcode
 *
 * 设计模式：
 * - 预测性解析：在不确定的情况下尝试匹配
 * - 回退安全：不匹配时状态不变，支持其他尝试
 * - 组合使用：可以连续调用处理复杂模式
 *
 * @param[in,out] ls 词法分析器状态指针
 * @param[in] set 要检查的字符集合，以NULL结尾的C字符串
 *
 * @return 匹配结果
 * @retval 1 当前字符在字符集中，已保存并移动到下一个字符
 * @retval 0 当前字符不在字符集中，词法分析器状态未改变
 *
 * @pre ls != NULL && set != NULL
 * @pre set是有效的以NULL结尾的C字符串
 *
 * @post 如果返回1，当前字符已保存到缓冲区，位置已移动
 * @post 如果返回0，词法分析器状态完全不变
 *
 * @note 函数不会抛出异常，总是安全返回
 * @see strchr(), save_and_next(), 数值解析函数
 */
static int check_next(LexState *ls, const char *set) {
    if (!strchr(set, ls->current)) {
        return 0;
    }
    save_and_next(ls);
    return 1;
}

/**
 * @brief 在Token缓冲区中替换所有指定字符
 *
 * 这个函数在词法分析器的Token构建缓冲区中查找并替换所有出现的
 * 指定字符。主要用于数值解析中的本地化处理，特别是小数点分隔符的转换。
 *
 * 功能特点：
 * - 全局替换：替换缓冲区中所有匹配的字符
 * - 就地修改：直接在原缓冲区中进行替换，无需额外内存
 * - 高效实现：单次遍历完成所有替换
 * - 类型安全：使用char类型确保字符操作的正确性
 *
 * 主要用途：
 * 1. 小数点本地化：将'.'转换为系统locale的小数点分隔符
 * 2. 格式标准化：将本地化分隔符转换回标准格式
 * 3. 字符规范化：统一字符表示格式
 * 4. 错误恢复：在数值解析失败时恢复原始格式
 *
 * 算法实现：
 * - 从缓冲区末尾开始向前遍历（避免索引计算）
 * - 逐个检查每个字符是否匹配源字符
 * - 匹配时直接替换为目标字符
 * - 使用递减循环优化性能
 *
 * 性能特点：
 * - 时间复杂度：O(n)，其中n为缓冲区长度
 * - 空间复杂度：O(1)，就地替换无额外空间需求
 * - 缓存友好：顺序访问内存，良好的缓存局部性
 * - 分支预测：简单的条件判断，易于CPU优化
 *
 * 内存安全：
 * - 边界检查：通过luaZ_bufflen()获取准确的缓冲区长度
 * - 指针安全：使用luaZ_buffer()获取有效的缓冲区指针
 * - 类型匹配：char类型确保字符操作的一致性
 * - 无溢出风险：在已知边界内进行操作
 *
 * 使用场景：
 * - 数值解析：处理不同locale的小数点格式
 * - 格式转换：在解析前后进行字符格式调整
 * - 错误处理：在解析失败时恢复原始字符
 * - 标准化：确保数值字符串符合特定格式要求
 *
 * 设计考虑：
 * - 向后遍历：避免正向遍历时的索引递增开销
 * - 简单接口：只需要源字符和目标字符两个参数
 * - 无返回值：就地修改，不需要返回结果
 * - 异常安全：不会抛出异常，总是安全完成
 *
 * 本地化支持：
 * - 支持各种locale的小数点分隔符（如','代替'.'）
 * - 处理不同地区的数值格式约定
 * - 确保数值解析的跨平台兼容性
 * - 提供格式转换的灵活性
 *
 * @param[in] ls 词法分析器状态指针，包含要处理的缓冲区
 * @param[in] from 要被替换的源字符
 * @param[in] to 替换后的目标字符
 *
 * @pre ls != NULL && ls->buff != NULL
 * @pre 缓冲区包含有效的字符数据
 *
 * @post 缓冲区中所有from字符都被替换为to字符
 * @post 缓冲区长度和其他属性保持不变
 *
 * @note 函数不会抛出异常，总是安全完成
 * @see luaZ_bufflen(), luaZ_buffer(), 数值本地化处理
 */
static void buffreplace(LexState *ls, char from, char to) {
    size_t n = luaZ_bufflen(ls->buff);
    char *p = luaZ_buffer(ls->buff);

    while (n--) {
        if (p[n] == from) {
            p[n] = to;
        }
    }
}

/**
 * @brief 尝试使用系统locale的小数点分隔符重新解析数值
 *
 * 这个函数是数值解析的错误恢复机制，当使用默认小数点分隔符('.')
 * 解析数值失败时，尝试使用系统locale的小数点分隔符重新解析。
 * 这确保了Lua在不同地区和语言环境下的数值兼容性。
 *
 * 工作原理：
 * 1. 获取系统locale的小数点分隔符
 * 2. 更新词法分析器的小数点设置
 * 3. 在缓冲区中替换小数点字符
 * 4. 尝试重新解析数值
 * 5. 如果仍然失败，恢复原始格式并报告错误
 *
 * 本地化支持：
 * - 欧洲格式：使用逗号','作为小数点分隔符
 * - 美式格式：使用点号'.'作为小数点分隔符
 * - 其他地区：支持locale定义的任何小数点字符
 * - 动态适应：运行时检测和适应系统设置
 *
 * 错误恢复策略：
 * - 两阶段解析：先用默认格式，失败后用本地化格式
 * - 状态恢复：解析失败时恢复原始字符格式
 * - 明确错误：提供清晰的错误消息
 * - 异常安全：确保词法分析器状态的一致性
 *
 * 性能考虑：
 * - 延迟调用：只在初次解析失败时才调用
 * - 缓存locale：避免重复的系统调用
 * - 最小修改：只替换必要的字符
 * - 快速失败：第二次失败时立即报错
 *
 * 兼容性处理：
 * - NULL检查：处理localeconv()返回NULL的情况
 * - 默认回退：locale不可用时使用标准'.'分隔符
 * - 跨平台：支持不同操作系统的locale实现
 * - 编码安全：处理不同字符编码的小数点字符
 *
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为数值字符串长度（主要是buffreplace）
 * - 空间复杂度：O(1)，只使用常量额外空间
 * - 系统调用：一次localeconv()调用
 *
 * 使用场景：
 * - 国际化应用：支持不同地区的数值格式
 * - 数据导入：处理来自不同locale的数值数据
 * - 用户输入：适应用户习惯的数值格式
 * - 错误恢复：在标准解析失败时提供备选方案
 *
 * 错误处理：
 * - 双重验证：两种格式都尝试后才报错
 * - 状态一致：确保错误时词法分析器状态正确
 * - 精确消息：提供具体的错误描述
 * - 位置信息：保持准确的错误位置
 *
 * @param[in,out] ls 词法分析器状态指针，包含小数点设置和缓冲区
 * @param[out] seminfo 语义信息结构，用于存储解析后的数值
 *
 * @pre ls != NULL && seminfo != NULL
 * @pre 缓冲区包含有效的数值字符串
 * @pre 使用默认小数点分隔符的解析已经失败
 *
 * @post 如果成功，seminfo->r包含解析后的数值
 * @post 如果成功，ls->decpoint更新为系统locale的分隔符
 * @post 如果失败，抛出"malformed number"错误
 *
 * @throws "malformed number" 当使用locale分隔符仍无法解析时
 *
 * @note 函数会修改词法分析器的小数点设置和缓冲区内容
 * @see localeconv(), buffreplace(), luaO_str2d()
 */
static void trydecpoint(LexState *ls, SemInfo *seminfo) {
    struct lconv *cv = localeconv();
    char old = ls->decpoint;
    ls->decpoint = (cv ? cv->decimal_point[0] : '.');
    buffreplace(ls, old, ls->decpoint);

    if (!luaO_str2d(luaZ_buffer(ls->buff), &seminfo->r)) {
        buffreplace(ls, ls->decpoint, '.');
        luaX_lexerror(ls, "malformed number", TK_NUMBER);
    }
}

/**
 * @brief 解析数值字面量：支持整数、浮点数和科学计数法
 *
 * 这个函数负责解析Lua源代码中的数值字面量，支持多种数值格式
 * 并处理本地化的小数点分隔符。它是词法分析器中处理数值的核心函数。
 *
 * 支持的数值格式：
 * - 整数：123, 0, 999999
 * - 浮点数：3.14, 0.5, 123.456
 * - 科学计数法：1e10, 3.14E-2, 2.5e+3
 * - 以小数点开头：.5, .123（由调用者处理前导点）
 *
 * 解析算法：
 * 1. 收集数字和小数点字符
 * 2. 检查并处理科学计数法指数部分（E/e）
 * 3. 处理指数符号（+/-）
 * 4. 收集剩余的字母数字字符（用于错误检测）
 * 5. 应用本地化小数点分隔符
 * 6. 转换为Lua数值类型
 *
 * 本地化支持：
 * - 使用系统locale的小数点分隔符
 * - 自动替换'.'为本地化分隔符
 * - 支持不同地区的数值格式约定
 * - 错误时尝试更新小数点分隔符
 *
 * 错误处理：
 * - 格式错误：无效的数值格式
 * - 溢出检测：数值超出Lua数值范围
 * - 本地化错误：小数点分隔符不匹配
 * - 语法错误：数值后跟无效字符
 *
 * 性能优化：
 * - 直接在缓冲区中构建数值字符串
 * - 最小化字符串操作和内存分配
 * - 使用高效的字符分类函数
 * - 避免不必要的数值转换
 *
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为数值字符串长度
 * - 空间复杂度：O(n)，用于存储数值字符串
 *
 * 内存管理：
 * - 使用词法分析器的动态缓冲区
 * - 自动处理缓冲区扩展
 * - 数值转换结果存储在seminfo中
 *
 * 平台兼容性：
 * - 支持不同平台的locale设置
 * - 兼容各种小数点分隔符约定
 * - 使用标准C库函数确保可移植性
 *
 * @param[in,out] ls 词法分析器状态指针，包含输入流和缓冲区
 * @param[out] seminfo 语义信息结构，用于存储解析后的数值
 *
 * @pre ls != NULL && ls->buff != NULL && seminfo != NULL
 * @pre isdigit(ls->current) 当前字符必须是数字
 *
 * @post seminfo->r包含解析后的数值（lua_Number类型）
 * @post 词法分析器位置移动到数值结束后的第一个字符
 *
 * @throws "malformed number" 当数值格式错误时
 *
 * @note 函数会修改词法分析器的缓冲区和小数点设置
 * @see luaO_str2d(), trydecpoint(), buffreplace(), check_next()
 */
static void read_numeral(LexState *ls, SemInfo *seminfo) {
    lua_assert(isdigit(ls->current));

    do {
        save_and_next(ls);
    } while (isdigit(ls->current) || ls->current == '.');

    if (check_next(ls, "Ee")) {
        check_next(ls, "+-");
    }

    while (isalnum(ls->current) || ls->current == '_') {
        save_and_next(ls);
    }

    save(ls, '\0');
    buffreplace(ls, '.', ls->decpoint);

    if (!luaO_str2d(luaZ_buffer(ls->buff), &seminfo->r)) {
        trydecpoint(ls, seminfo);
    }
}

/**
 * @brief 解析长字符串/长注释的分隔符并计算嵌套层级
 *
 * 这个函数负责解析Lua长字符串和长注释的分隔符格式，如[[、[=[、[==[等，
 * 并返回分隔符的嵌套层级。这是处理长字符串和长注释的关键函数。
 *
 * 分隔符格式：
 * - 基本格式：[[ ... ]]
 * - 一级嵌套：[=[ ... ]=]
 * - 二级嵌套：[==[ ... ]==]
 * - n级嵌套：[=...=[ ... ]=...=]（n个等号）
 *
 * 算法逻辑：
 * 1. 保存并跳过第一个方括号（'['或']'）
 * 2. 计算连续等号的数量
 * 3. 检查是否以相同的方括号结束
 * 4. 返回嵌套层级或错误标识
 *
 * 返回值含义：
 * - 非负数：有效的分隔符，值为嵌套层级（等号数量）
 * - -1：单独的方括号（如单独的'['字符）
 * - 负数（< -1）：无效的分隔符格式
 *
 * 使用场景：
 * 1. 长字符串解析：[[string]]、[=[string]=]等
 * 2. 长注释解析：--[[comment]]、--[=[comment]=]等
 * 3. 分隔符验证：确保开始和结束分隔符匹配
 *
 * 错误检测：
 * - 不匹配的方括号：[=[ 但结尾是 ]=]
 * - 不完整的分隔符：[= 但没有后续的 [
 * - 格式错误：等号数量不匹配
 *
 * 性能特点：
 * - 线性时间复杂度：O(n)，其中n为等号数量
 * - 最小内存使用：只需要几个局部变量
 * - 直接字符比较：避免字符串操作开销
 *
 * 兼容性考虑：
 * - 支持Lua 5.1的长字符串语法
 * - 处理嵌套层级的兼容性选项
 * - 与旧版本Lua的行为保持一致
 *
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为分隔符中等号的数量
 * - 空间复杂度：O(1)，只使用常量额外空间
 *
 * @param[in,out] ls 词法分析器状态指针
 *
 * @return 分隔符的嵌套层级或错误标识
 * @retval >=0 有效分隔符，值为嵌套层级（等号数量）
 * @retval -1 单独的方括号字符
 * @retval <-1 无效的分隔符格式
 *
 * @pre ls != NULL
 * @pre ls->current == '[' || ls->current == ']'
 *
 * @post 词法分析器位置移动到分隔符结束后
 * @post 分隔符内容已保存到缓冲区中
 *
 * @note 函数会修改词法分析器的位置和缓冲区状态
 * @see read_long_string(), 长字符串语法规范
 */
static int skip_sep(LexState *ls) {
    int count = 0;
    int s = ls->current;

    lua_assert(s == '[' || s == ']');
    save_and_next(ls);

    while (ls->current == '=') {
        save_and_next(ls);
        count++;
    }

    return (ls->current == s) ? count : (-count) - 1;
}

/**
 * @brief 解析长字符串和长注释：支持嵌套和多行内容
 *
 * 这个函数负责解析Lua的长字符串（[[...]]）和长注释（--[[...]]）。
 * 长字符串是Lua的一个重要特性，支持多行内容、嵌套结构和原始文本。
 *
 * 长字符串格式：
 * - 基本格式：[[string content]]
 * - 一级嵌套：[=[string content]=]
 * - 多级嵌套：[===[string content]===]（任意数量的等号）
 * - 支持换行：内容可以跨越多行
 * - 原始文本：不处理转义序列
 *
 * 长注释格式：
 * - 基本格式：--[[comment content]]
 * - 嵌套格式：--[=[comment content]=]
 * - 多行支持：注释可以跨越多行
 * - 嵌套兼容：支持旧版本的嵌套规则
 *
 * 算法流程：
 * 1. 跳过开始分隔符的第二个字符
 * 2. 处理开始后的换行符（如果存在）
 * 3. 逐字符处理内容直到找到匹配的结束分隔符
 * 4. 处理嵌套结构（如果启用兼容性）
 * 5. 创建字符串对象（去除分隔符）
 *
 * 兼容性处理：
 * - LUA_COMPAT_LSTR：支持长字符串嵌套的兼容性选项
 * - 嵌套计数：跟踪嵌套层级（仅在兼容模式下）
 * - 废弃警告：对旧式嵌套语法发出警告
 *
 * 换行符处理：
 * - 自动处理不同平台的换行符格式
 * - 长字符串开始后的第一个换行符被忽略
 * - 内容中的换行符被保留
 * - 正确更新行号计数
 *
 * 内存管理：
 * - 字符串内容：使用动态缓冲区存储
 * - 注释内容：不保存到缓冲区，定期清理
 * - 字符串对象：通过luaX_newstring()创建
 * - 缓冲区优化：注释处理时避免内存浪费
 *
 * 错误处理：
 * - 未结束检测：文件结束但长字符串/注释未结束
 * - 分隔符匹配：确保开始和结束分隔符层级一致
 * - 嵌套错误：处理不正确的嵌套结构
 *
 * 性能优化：
 * - 注释模式：不保存内容，只跳过字符
 * - 缓冲区管理：注释时定期重置缓冲区
 * - 字符处理：最小化不必要的字符保存
 * - 内存局部性：紧凑的循环结构
 *
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为长字符串/注释的长度
 * - 空间复杂度：O(m)，其中m为字符串内容长度（注释为O(1)）
 *
 * @param[in,out] ls 词法分析器状态指针，包含输入流和缓冲区
 * @param[in,out] seminfo 语义信息结构，非NULL时存储字符串对象
 * @param[in] sep 分隔符嵌套层级，由skip_sep()函数返回
 *
 * @pre ls != NULL
 * @pre sep >= 0（有效的分隔符层级）
 * @pre 当前位置在长字符串/注释的开始分隔符处
 *
 * @post 如果seminfo非NULL，包含解析后的字符串对象
 * @post 词法分析器位置移动到长字符串/注释结束后
 * @post 行号正确更新以反映跨行内容
 *
 * @throws "unfinished long string" 当长字符串未正确结束时
 * @throws "unfinished long comment" 当长注释未正确结束时
 * @throws "nesting of [[...]] is deprecated" 当使用废弃的嵌套语法时
 *
 * @note seminfo为NULL时表示处理长注释，非NULL时处理长字符串
 * @see skip_sep(), luaX_newstring(), inclinenumber()
 */
static void read_long_string(LexState *ls, SemInfo *seminfo, int sep) {
    int cont = 0;
    (void)(cont);

    save_and_next(ls);

    if (currIsNewline(ls)) {
        inclinenumber(ls);
    }

    for (;;) {
        switch (ls->current) {
            case EOZ:
                luaX_lexerror(ls, (seminfo) ? "unfinished long string" :
                                               "unfinished long comment", TK_EOS);
                break;

#if defined(LUA_COMPAT_LSTR)
            case '[': {
                if (skip_sep(ls) == sep) {
                    save_and_next(ls);
                    cont++;

#if LUA_COMPAT_LSTR == 1
                    if (sep == 0) {
                        luaX_lexerror(ls, "nesting of [[...]] is deprecated", '[');
                    }
#endif
                }
                break;
            }
#endif

            case ']': {
                if (skip_sep(ls) == sep) {
                    save_and_next(ls);

#if defined(LUA_COMPAT_LSTR) && LUA_COMPAT_LSTR == 2
                    cont--;
                    if (sep == 0 && cont >= 0) {
                        break;
                    }
#endif
                    goto endloop;
                }
                break;
            }

            case '\n':
            case '\r': {
                save(ls, '\n');
                inclinenumber(ls);

                if (!seminfo) {
                    luaZ_resetbuffer(ls->buff);
                }
                break;
            }

            default: {
                if (seminfo) {
                    save_and_next(ls);
                } else {
                    next(ls);
                }
            }
        }
    }

    endloop:
    if (seminfo) {
        seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + (2 + sep),
                                         luaZ_bufflen(ls->buff) - 2*(2 + sep));
    }
}

/**
 * @brief 解析字符串字面量：处理转义序列和字符串内容
 *
 * 这个函数负责解析Lua源代码中的字符串字面量，支持单引号和双引号
 * 字符串，并正确处理各种转义序列。它是词法分析器中最复杂的函数之一。
 *
 * 字符串格式支持：
 * - 单引号字符串：'string content'
 * - 双引号字符串："string content"
 * - 转义序列：\n, \t, \r, \\, \", \', \a, \b, \f, \v
 * - 数值转义：\ddd（三位十进制数，范围0-255）
 * - 换行转义：字符串中的\n或\r转换为实际换行符
 *
 * 转义序列处理：
 * 1. 标准转义：\n→换行, \t→制表符, \\→反斜杠等
 * 2. 数值转义：\123→ASCII码123的字符
 * 3. 换行转义：字符串中的实际换行转为\n字符
 * 4. 引号转义：\"和\'用于在字符串中包含引号
 *
 * 错误检测：
 * - 未结束字符串：遇到文件结束或换行符而没有结束引号
 * - 转义序列过大：数值转义超过255（UCHAR_MAX）
 * - 格式错误：不正确的转义序列格式
 *
 * 算法流程：
 * 1. 保存并跳过开始引号（del参数）
 * 2. 逐字符处理直到遇到结束引号
 * 3. 处理转义序列和普通字符
 * 4. 保存并跳过结束引号
 * 5. 创建字符串对象（去除首尾引号）
 *
 * 内存管理：
 * - 使用动态缓冲区存储字符串内容
 * - 通过luaX_newstring()创建TString对象
 * - 字符串对象自动加入常量池和GC管理
 *
 * 性能优化：
 * - 直接在缓冲区中构建字符串，避免多次复制
 * - 转义序列的快速查找表处理
 * - 最小化函数调用和内存分配
 *
 * 平台兼容性：
 * - 支持不同平台的换行符格式
 * - 使用标准C转义序列定义
 * - 字符编码兼容ASCII和UTF-8
 *
 * @param[in,out] ls 词法分析器状态指针，包含输入流和缓冲区
 * @param[in] del 字符串分隔符（'"'或'\''），用于确定字符串结束
 * @param[out] seminfo 语义信息结构，用于存储解析后的字符串对象
 *
 * @pre ls != NULL && ls->buff != NULL && seminfo != NULL
 * @pre del == '"' || del == '\''
 * @pre ls->current == del（当前字符是字符串开始分隔符）
 *
 * @post seminfo->ts包含解析后的字符串对象
 * @post 词法分析器位置移动到字符串结束分隔符之后
 *
 * @throws "unfinished string" 当字符串未正确结束时
 * @throws "escape sequence too large" 当数值转义超过255时
 *
 * @note 函数会修改词法分析器的缓冲区和位置状态
 * @see luaX_newstring(), save(), next(), inclinenumber()
 */
static void read_string(LexState *ls, int del, SemInfo *seminfo) {
    save_and_next(ls);

    while (ls->current != del) {
        switch (ls->current) {
            case EOZ:
                luaX_lexerror(ls, "unfinished string", TK_EOS);
                continue;

            case '\n':
            case '\r':
                luaX_lexerror(ls, "unfinished string", TK_STRING);
                continue;

            case '\\': {
                int c;
                next(ls);

                switch (ls->current) {
                    case 'a': c = '\a'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'v': c = '\v'; break;

                    case '\n':
                    case '\r':
                        save(ls, '\n');
                        inclinenumber(ls);
                        continue;

                    case EOZ:
                        continue;

                    default: {
                        if (!isdigit(ls->current)) {
                            save_and_next(ls);
                        } else {
                            int i = 0;
                            c = 0;

                            do {
                                c = 10 * c + (ls->current - '0');
                                next(ls);
                            } while (++i < 3 && isdigit(ls->current));

                            if (c > UCHAR_MAX) {
                                luaX_lexerror(ls, "escape sequence too large",
                                             TK_STRING);
                            }

                            save(ls, c);
                        }
                        continue;
                    }
                }

                save(ls, c);
                next(ls);
                continue;
            }

            default:
                save_and_next(ls);
        }
    }

    save_and_next(ls);
    seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + 1,
                                     luaZ_bufflen(ls->buff) - 2);
}

/**
 * @brief 核心词法分析函数：将字符流转换为Token
 *
 * 这是Lua词法分析器的核心函数，实现了基于有限状态自动机的
 * 字符识别和Token构建。它从输入流中读取字符，识别各种语法元素，
 * 并返回相应的Token类型。
 *
 * 函数设计理念：
 * - 状态机驱动：通过switch-case实现状态转换
 * - 流式处理：逐字符处理，支持大文件高效解析
 * - 错误恢复：遇到错误时提供精确的位置信息
 * - 性能优化：常见情况优先处理，减少分支预测失误
 *
 * 支持的Token类型：
 * 1. 换行符：自动处理行号计数，跳过继续处理
 * 2. 注释：支持单行注释(--)和长注释(--[[...]])
 * 3. 字符串：支持短字符串("")和长字符串([[...]])
 * 4. 数值：支持整数、浮点数和科学计数法
 * 5. 操作符：支持单字符和多字符操作符
 * 6. 标识符：支持字母、数字、下划线组合
 * 7. 保留字：自动识别Lua关键字
 * 8. 特殊符号：括号、逗号、分号等分隔符
 *
 * 状态机处理流程：
 * 1. 重置Token缓冲区，准备构建新Token
 * 2. 进入无限循环，逐字符处理输入流
 * 3. 根据当前字符类型选择处理分支
 * 4. 构建Token内容并确定Token类型
 * 5. 返回Token类型，结束当前Token处理
 *
 * 错误处理策略：
 * - 词法错误：立即报告并终止分析
 * - 格式错误：提供具体的错误描述
 * - 位置信息：包含准确的行号和上下文
 *
 * 性能优化技术：
 * - 分支预测：常见字符类型放在前面
 * - 缓冲区重用：避免频繁的内存分配
 * - 字符分类缓存：利用ctype.h的高效实现
 * - 快速路径：为简单Token提供优化处理
 *
 * 内存管理：
 * - 动态缓冲区：根据Token长度自动扩展
 * - 字符串内部化：相同字符串只存储一份
 * - 垃圾回收集成：新字符串自动纳入GC管理
 *
 * @param[in,out] ls 词法分析器状态指针，包含输入流和缓冲区
 * @param[out] seminfo Token的语义信息，用于存储Token的具体内容
 *
 * @return Token类型的整数值
 * @retval TK_* Token枚举值，表示识别出的Token类型
 * @retval 字符值 单字符Token直接返回字符的ASCII值
 * @retval TK_EOS 文件结束Token
 *
 * @pre ls != NULL && ls->buff != NULL && seminfo != NULL
 * @post 返回值表示有效的Token类型
 * @post seminfo包含Token的具体内容（如适用）
 *
 * @note 函数可能触发内存分配和垃圾回收
 * @see LexState, SemInfo, Token枚举定义
 */
static int llex(LexState *ls, SemInfo *seminfo) {
    // 重置Token构建缓冲区：准备构建新的Token
    luaZ_resetbuffer(ls->buff);

    // 主要的词法分析循环：持续处理字符直到识别出完整Token
    for (;;) {
        switch (ls->current) {
            // === 换行符处理：跨平台换行符支持 ===
            case '\n':
            case '\r': {
                // 增加行号计数：支持\n, \r, \r\n等格式
                inclinenumber(ls);
                continue;   // 跳过换行符，继续处理下一个字符
            }

            // === 注释和减号操作符处理 ===
            case '-': {
                next(ls);   // 读取'-'后的下一个字符

                // 检查是否为注释：需要连续两个'-'
                if (ls->current != '-') {
                    return '-';     // 单个'-'：返回减号操作符
                }

                // 确认为注释：跳过第二个'-'
                next(ls);

                // 检查是否为长注释：--[[...]]格式
                if (ls->current == '[') {
                    int sep = skip_sep(ls);     // 计算分隔符层级
                    luaZ_resetbuffer(ls->buff); // 清理可能的缓冲区污染

                    if (sep >= 0) {
                        // 长注释：使用read_long_string处理，不保存内容
                        read_long_string(ls, NULL, sep);
                        luaZ_resetbuffer(ls->buff);
                        continue;   // 跳过整个长注释
                    }
                }

                // 短注释：跳过到行尾的所有字符
                while (!currIsNewline(ls) && ls->current != EOZ) {
                    next(ls);
                }
                continue;   // 注释处理完毕，继续下一轮
            }
            // === 长字符串和左方括号处理 ===
            case '[': {
                // 尝试解析长字符串分隔符：[[...]]或[=[...]=]等格式
                int sep = skip_sep(ls);

                if (sep >= 0) {
                    // 有效的长字符串分隔符：读取完整的长字符串
                    read_long_string(ls, seminfo, sep);
                    return TK_STRING;
                } else if (sep == -1) {
                    // 单独的'['字符：返回左方括号Token
                    return '[';
                } else {
                    // 无效的长字符串分隔符：报告错误
                    luaX_lexerror(ls, "invalid long string delimiter", TK_STRING);
                }
            }

            // === 比较操作符处理：支持单字符和双字符操作符 ===
            case '=': {
                next(ls);   // 跳过第一个'='
                if (ls->current != '=') {
                    return '=';     // 单个'='：赋值操作符
                } else {
                    next(ls);       // 跳过第二个'='
                    return TK_EQ;   // 双个'=='：等于比较操作符
                }
            }

            case '<': {
                next(ls);   // 跳过'<'
                if (ls->current != '=') {
                    return '<';     // 单个'<'：小于操作符
                } else {
                    next(ls);       // 跳过'='
                    return TK_LE;   // '<='：小于等于操作符
                }
            }

            case '>': {
                next(ls);   // 跳过'>'
                if (ls->current != '=') {
                    return '>';     // 单个'>'：大于操作符
                } else {
                    next(ls);       // 跳过'='
                    return TK_GE;   // '>='：大于等于操作符
                }
            }

            case '~': {
                next(ls);   // 跳过'~'
                if (ls->current != '=') {
                    return '~';     // 单个'~'：按位取反操作符
                } else {
                    next(ls);       // 跳过'='
                    return TK_NE;   // '~='：不等于比较操作符
                }
            }

            // === 字符串字面量处理：支持单引号和双引号 ===
            case '"':
            case '\'': {
                // 调用字符串解析函数：处理转义序列和字符串内容
                read_string(ls, ls->current, seminfo);
                return TK_STRING;
            }
            // === 点号和数值处理：支持小数、连接符、可变参数 ===
            case '.': {
                save_and_next(ls);     // 保存'.'并读取下一个字符

                // 检查是否为多字符操作符
                if (check_next(ls, ".")) {
                    // 第二个点号存在，检查第三个点号
                    if (check_next(ls, ".")) {
                        return TK_DOTS;     // '...'：可变参数操作符
                    } else {
                        return TK_CONCAT;   // '..'：字符串连接操作符
                    }
                } else if (!isdigit(ls->current)) {
                    // 后面不是数字：单独的点号（表访问操作符）
                    return '.';
                } else {
                    // 后面是数字：以点号开头的小数（如.5）
                    read_numeral(ls, seminfo);
                    return TK_NUMBER;
                }
            }

            // === 文件结束符处理 ===
            case EOZ: {
                return TK_EOS;      // 返回文件结束Token
            }

            // === 默认字符处理：空白符、数字、标识符、单字符Token ===
            default: {
                // 处理空白字符（除换行符外）
                if (isspace(ls->current)) {
                    // 断言：换行符应该在前面的case中处理
                    lua_assert(!currIsNewline(ls));
                    next(ls);       // 跳过空白字符
                    continue;       // 继续处理下一个字符
                }
                // 处理数字字面量
                else if (isdigit(ls->current)) {
                    read_numeral(ls, seminfo);
                    return TK_NUMBER;
                }
                // 处理标识符和保留字
                else if (isalpha(ls->current) || ls->current == '_') {
                    TString *ts;

                    // 构建标识符：收集所有字母、数字、下划线
                    do {
                        save_and_next(ls);
                    } while (isalnum(ls->current) || ls->current == '_');

                    // 创建字符串对象并添加到常量池
                    ts = luaX_newstring(ls, luaZ_buffer(ls->buff),
                                            luaZ_bufflen(ls->buff));

                    // 检查是否为保留字
                    if (ts->tsv.reserved > 0) {
                        // 保留字：返回对应的Token类型
                        // reserved值从1开始，需要转换为Token枚举值
                        return ts->tsv.reserved - 1 + FIRST_RESERVED;
                    } else {
                        // 普通标识符：保存字符串对象并返回TK_NAME
                        seminfo->ts = ts;
                        return TK_NAME;
                    }
                }
                // 处理单字符Token（操作符、分隔符等）
                else {
                    int c = ls->current;    // 保存当前字符
                    next(ls);               // 读取下一个字符
                    return c;               // 返回字符的ASCII值作为Token类型
                }
            }
        }   // switch结束
    }       // for循环结束
}           // llex函数结束

/**
 * @brief 获取下一个Token并更新词法分析器状态
 *
 * 这个函数是词法分析器的主要接口，负责获取输入流中的下一个Token。
 * 它实现了Token预读机制，提高了语法分析的效率和灵活性。
 *
 * Token获取策略：
 * 1. 优先使用预读Token：如果存在预读Token，直接使用并清除预读状态
 * 2. 实时解析Token：如果没有预读Token，调用llex()解析新Token
 * 3. 行号跟踪：更新lastline字段，用于错误报告和调试
 *
 * 预读机制的优势：
 * - 语法分析器可以提前查看下一个Token而不消费它
 * - 支持LL(1)语法分析，简化语法分析器的实现
 * - 减少回溯需求，提高解析效率
 * - 便于实现复杂的语法结构识别
 *
 * 状态管理：
 * - ls->t：当前Token，包含Token类型和语义信息
 * - ls->lookahead：预读Token，用于前瞻分析
 * - ls->lastline：上一个Token的行号，用于错误报告
 * - ls->linenumber：当前行号，实时更新
 *
 * 内存管理：
 * - Token的语义信息（如字符串、数值）由Lua的GC管理
 * - 预读Token的内容在使用后自动释放
 * - 不需要手动释放Token相关的内存
 *
 * 性能考虑：
 * - 预读机制避免了重复解析的开销
 * - 直接的结构体赋值操作，效率很高
 * - 最小化函数调用，减少栈操作开销
 *
 * 错误处理：
 * - 词法错误会通过异常机制传播
 * - 行号信息用于生成准确的错误报告
 * - 保持词法分析器状态的一致性
 *
 * 使用场景：
 * - 语法分析器的主要Token获取接口
 * - 编译器前端的Token流处理
 * - 语法高亮和代码分析工具
 *
 * @param[in,out] ls 词法分析器状态指针
 *
 * @pre ls != NULL && ls->L != NULL
 * @post ls->t包含有效的Token信息
 * @post ls->lastline更新为前一个Token的行号
 * @post 如果使用了预读Token，预读状态被清除
 *
 * @note 函数可能触发内存分配和垃圾回收
 * @see luaX_lookahead(), llex(), Token结构定义
 */
void luaX_next(LexState *ls) {
    ls->lastline = ls->linenumber;

    if (ls->lookahead.token != TK_EOS) {
        ls->t = ls->lookahead;
        ls->lookahead.token = TK_EOS;
    } else {
        ls->t.token = llex(ls, &ls->t.seminfo);
    }
}

/**
 * @brief 预读下一个Token而不消费当前Token
 *
 * 这个函数实现了Token的前瞻功能，允许语法分析器查看下一个Token
 * 而不改变当前的Token状态。这对于实现LL(1)语法分析器至关重要。
 *
 * 预读机制原理：
 * - 调用llex()解析下一个Token并存储在lookahead字段中
 * - 不影响当前Token（ls->t）的状态
 * - 后续调用luaX_next()时会优先使用预读Token
 *
 * 使用场景：
 * 1. 语法歧义消解：根据下一个Token决定语法分析路径
 * 2. 表达式解析：确定操作符优先级和结合性
 * 3. 语句识别：区分不同类型的语句开始
 * 4. 错误恢复：在错误情况下查看后续Token
 *
 * 实现约束：
 * - 只支持一个Token的预读（LL(1)限制）
 * - 预读Token必须在下次调用luaX_next()时被消费
 * - 不能连续调用luaX_lookahead()而不调用luaX_next()
 *
 * 状态检查：
 * - 使用lua_assert确保预读状态的正确性
 * - 防止重复预读导致的Token丢失
 * - 保证预读机制的单一性约束
 *
 * 性能影响：
 * - 预读会触发额外的词法分析调用
 * - 但避免了语法分析中的回溯开销
 * - 总体上提高了编译器的效率
 *
 * 内存管理：
 * - 预读Token的内容由Lua GC管理
 * - 预读状态不需要手动清理
 * - 与主Token流使用相同的内存管理策略
 *
 * 错误处理：
 * - 预读过程中的词法错误会立即传播
 * - 不会影响当前Token的有效性
 * - 保持错误报告的准确性
 *
 * @param[in,out] ls 词法分析器状态指针
 *
 * @pre ls != NULL && ls->lookahead.token == TK_EOS
 * @post ls->lookahead包含有效的预读Token
 * @post ls->t保持不变
 *
 * @note 连续调用此函数而不调用luaX_next()会导致断言失败
 * @see luaX_next(), llex(), LL(1)语法分析
 */
void luaX_lookahead(LexState *ls) {
    lua_assert(ls->lookahead.token == TK_EOS);
    ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
}

