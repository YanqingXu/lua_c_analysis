/*
** [核心] Lua词法分析器模块
**
** 功能概述：
** 负责将源代码字符流转换为标记(token)流，是编译器前端的关键组件
**
** 主要功能：
** - 词法标记识别（关键字、标识符、字面量、运算符）
** - 字符串解析（包括转义字符和长字符串）
** - 数字解析（整数、浮点数、科学记数法）
** - 注释处理（单行注释和多行注释）
** - 错误定位与报告
**
** 核心算法：
** - 有限状态自动机进行词法识别
** - 递归下降解析长字符串
** - 本地化数字格式支持
**
** 模块依赖：
** - lparser.c：为语法分析器提供token流
** - lstring.c：字符串对象管理
** - lzio.c：输入流缓冲管理
**
** 算法复杂度：O(n) 时间，n为源代码字符数
**
** 相关文档：参见 docs/wiki_lexer.md
*/

#include <ctype.h>
#include <locale.h>
#include <string.h>

#define llex_c
#define LUA_CORE

#include "lua.h"

#include "ldo.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"


/*
** [入门] 读取下一个字符的宏定义
**
** 实现原理：
** 从输入流中读取下一个字符并更新词法状态的当前字符
**
** 使用场景：
** - 字符串解析过程中的字符推进
** - 跳过空白字符和注释
** - 数字和标识符的连续读取
*/
#define next(ls) (ls->current = zgetc(ls->z))

/*
** [入门] 检查当前字符是否为换行符
**
** 支持的换行符：
** - \n（Unix/Linux标准）
** - \r（Mac经典格式）
** - \r\n（Windows格式，需要额外处理）
*/
#define currIsNewline(ls) (ls->current == '\n' || ls->current == '\r')


/*
** Lua保留字数组 - 按特定顺序排列
** 这个顺序必须与llex.h中的枚举保持一致
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


/*
** 保存当前字符并读取下一个字符的宏
** 先保存当前字符到缓冲区，然后读取下一个字符
*/
#define save_and_next(ls) (save(ls, ls->current), next(ls))


/*
** [基础] 将字符保存到词法分析器的缓冲区
**
** 功能详述：
** 将指定字符保存到词法分析器的动态缓冲区中，在缓冲区空间不足时
** 自动进行扩容操作，确保字符能够正确保存
**
** 缓冲区管理：
** - 检查当前缓冲区容量是否足够
** - 在需要时动态扩展缓冲区大小
** - 使用安全的内存分配策略
**
** @param ls - LexState*：词法状态指针，包含缓冲区信息
** @param c - int：要保存的字符值
**
** 算法复杂度：O(1) 平摊时间，扩容时为 O(n)
**
** 注意事项：
** - 扩容可能触发内存重新分配
** - 字符值应该在有效ASCII范围内
*/
static void save(LexState *ls, int c)
{
    Mbuffer *b = ls->buff;
    
    // 检查缓冲区是否需要扩容
    if (b->n + 1 > b->buffsize) {
        size_t newsize;
        
        // 防止缓冲区过大
        if (b->buffsize >= MAX_SIZET / 2)
        {
            luaX_lexerror(ls, "lexical element too long", 0);
        }
            
        newsize = b->buffsize * 2;
        luaZ_resizebuffer(ls->L, b, newsize);
    }
    
    b->buffer[b->n++] = cast(char, c);
}


/*
** [核心] 初始化词法分析器
**
** 功能详述：
** 初始化Lua词法分析器，将所有保留字注册到字符串表中并标记为保留字
** 确保保留字永远不会被垃圾回收，提高词法分析的效率
**
** 初始化过程：
** 1. 遍历所有预定义的保留字
** 2. 将每个保留字创建为TString对象
** 3. 标记为保留字并固定在内存中
** 4. 设置保留字的特殊标记值
**
** @param L - lua_State*：Lua状态机指针
**
** 算法复杂度：O(k) 时间，k为保留字数量
**
** 注意事项：
** - 必须在词法分析器使用前调用
** - 保留字的顺序必须与枚举定义一致
** - 保留字一旦注册将永久存在
*/
void luaX_init(lua_State *L)
{
    int i;
    
    // [遍历处理] 遍历所有保留字并注册到字符串表中
    for (i = 0; i < NUM_RESERVED; i++) {
        TString *ts = luaS_new(L, luaX_tokens[i]);
        
        // [内存固定] 保留字永远不会被垃圾回收
        luaS_fix(ts);
        
        lua_assert(strlen(luaX_tokens[i]) + 1 <= TOKEN_LEN);
        
        // [标记设置] 标记为保留字，索引从1开始
        ts->tsv.reserved = cast_byte(i + 1);
    }
}


// 错误信息中源代码最大显示长度
#define MAXSRC 80


/*
** 将标记转换为字符串表示
** ls: 词法状态
** token: 标记类型
** 返回: 标记的字符串表示
*/
const char *luaX_token2str(LexState *ls, int token)
{
    if (token < FIRST_RESERVED) {
        lua_assert(token == cast(unsigned char, token));
        return (iscntrl(token)) ? luaO_pushfstring(ls->L, "char(%d)", token) :
                                  luaO_pushfstring(ls->L, "%c", token);
    }
    else {
        return luaX_tokens[token - FIRST_RESERVED];
    }
}


/*
** 获取标记的文本表示
** ls: 词法状态
** token: 标记类型
** 返回: 标记的文本内容
*/
static const char *txtToken(LexState *ls, int token)
{
    switch (token) {
        case TK_NAME:
        case TK_STRING:
        case TK_NUMBER:
            save(ls, '\0');
            return luaZ_buffer(ls->buff);
        default:
            return luaX_token2str(ls, token);
    }
}


/*
** [核心] 抛出词法错误
**
** 功能详述：
** 格式化词法分析错误消息并抛出语法错误异常，提供详细的错误位置
** 和上下文信息，帮助用户快速定位语法错误
**
** 错误信息构成：
** 1. 源文件名和行号信息
** 2. 具体的错误描述消息
** 3. 出错标记的详细信息（如果提供）
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param msg - const char*：错误描述消息
** @param token - int：出错的标记类型，0表示无特定标记
**
** 算法复杂度：O(1) 时间
**
** 注意事项：
** - 此函数不会返回，总是抛出异常
** - 错误消息会被推入Lua栈中
** - 自动包含源文件和行号信息
*/
void luaX_lexerror(LexState *ls, const char *msg, int token)
{
    char buff[MAXSRC];
    
    // 获取源文件信息
    luaO_chunkid(buff, getstr(ls->source), MAXSRC);
    msg = luaO_pushfstring(ls->L, "%s:%d: %s", buff, ls->linenumber, msg);
    
    // 如果有具体的标记，添加标记信息
    if (token)
    {
        luaO_pushfstring(ls->L, "%s near " LUA_QS, msg, txtToken(ls, token));
    }
        
    luaD_throw(ls->L, LUA_ERRSYNTAX);
}


/*
** [核心] 抛出语法错误
**
** 功能详述：
** 抛出不带额外标记信息的语法错误，是luaX_lexerror的简化版本
** 用于处理不需要具体标记信息的一般语法错误情况
**
** 错误处理特点：
** 1. 不包含具体的标记信息
** 2. 直接传递错误消息
** 3. 保持与lexerror相同的异常抛出行为
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param msg - const char*：错误描述消息
**
** 算法复杂度：O(1) 时间
**
** 注意事项：
** - 内部调用luaX_lexerror(ls, msg, 0)
** - 同样不会返回，总是抛出异常
*/
void luaX_syntaxerror(LexState *ls, const char *msg)
{
    luaX_lexerror(ls, msg, ls->t.token);
}


/*
** [基础] 创建新字符串对象并加入常量表
**
** 功能详述：
** 在词法分析过程中创建新的字符串对象，并将其添加到函数的常量表中
** 以便在后续的字节码生成和执行过程中进行引用
**
** 字符串处理流程：
** 1. 使用luaS_newlstr创建字符串对象
** 2. 将字符串添加到当前函数的常量表
** 3. 标记字符串为已使用状态
** 4. 触发垃圾回收检查
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param str - const char*：待创建的字符串内容
** @param l - size_t：字符串长度
** @return TString*：创建的字符串对象指针
**
** 算法复杂度：O(l) 时间，l为字符串长度
**
** 注意事项：
** - 新字符串会被自动加入常量表
** - 设置标记防止被垃圾回收
** - 可能触发垃圾回收检查
*/
TString *luaX_newstring(LexState *ls, const char *str, size_t l)
{
    lua_State *L = ls->L;
    TString *ts = luaS_newlstr(L, str, l);
    TValue *o = luaH_setstr(L, ls->fs->h, ts);  // 加入常量表
    
    // 如果是新字符串，确保不会被垃圾回收
    if (ttisnil(o)) {
        setbvalue(o, 1);  // 标记为已使用
        luaC_checkGC(L);
    }
    
    return ts;
}


/*
** [基础] 递增行号计数器
**
** 功能详述：
** 处理换行符并更新词法分析器的行号计数，支持不同平台的换行符格式
** 包括Unix(\n)、Windows(\r\n)和Mac(\r)风格的换行序列
**
** 换行符处理逻辑：
** 1. 跳过当前换行符(\n或\r)
** 2. 检查并跳过配对的换行符(如\r\n中的\n)
** 3. 递增行号计数器
** 4. 检查行号是否溢出
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
**
** 算法复杂度：O(1) 时间
**
** 注意事项：
** - 支持跨平台换行符格式
** - 检查行号溢出并在必要时抛出错误
** - 自动处理\r\n和\n\r序列
*/
static void inclinenumber(LexState *ls)
{
    int old = ls->current;
    lua_assert(currIsNewline(ls));
    
    next(ls);  // 跳过 '\n' 或 '\r'
    
    // 处理 '\n\r' 或 '\r\n' 的情况
    if (currIsNewline(ls) && ls->current != old)
    {
        next(ls);
    }
        
    // 检查行号是否溢出
    if (++ls->linenumber >= MAX_INT)
    {
        luaX_syntaxerror(ls, "chunk has too many lines");
    }
}


/*
** [核心] 设置词法分析器的输入源
**
** 功能详述：
** 初始化词法状态结构，设置输入流、源文件信息和相关参数
** 为后续的词法分析过程准备完整的解析环境
**
** 初始化内容：
** 1. 基本参数设置（小数点字符、行号等）
** 2. 输入流和源文件绑定
** 3. 缓冲区初始化
** 4. 读取首个字符启动解析
**
** @param L - lua_State*：Lua虚拟机状态
** @param ls - LexState*：词法状态结构指针
** @param z - ZIO*：输入流对象
** @param source - TString*：源文件名字符串
**
** 算法复杂度：O(1) 时间
**
** 注意事项：
** - 必须在词法分析开始前调用
** - 自动初始化缓冲区大小
** - 预读取第一个字符
*/
void luaX_setinput(lua_State *L, LexState *ls, ZIO *z, TString *source)
{
    ls->decpoint = '.';                      // 默认小数点字符
    ls->L = L;                              // Lua状态机
    ls->lookahead.token = TK_EOS;           // 无前瞻标记
    ls->z = z;                              // 输入流
    ls->fs = NULL;                          // 函数状态
    ls->linenumber = 1;                     // 初始行号
    ls->lastline = 1;                       // 上一行号
    ls->source = source;                    // 源文件名
    
    // 初始化缓冲区
    luaZ_resizebuffer(ls->L, ls->buff, LUA_MINBUFFER);
    next(ls);  // 读取第一个字符
}


/* =================================================================== */
/*                           词法分析器核心                               */
/* =================================================================== */


/*
** [基础] 检查当前字符是否在指定字符集中
**
** 功能详述：
** 检查词法分析器当前字符是否属于指定的字符集合，如果匹配则
** 保存字符到缓冲区并前进到下一个字符
**
** 匹配处理流程：
** 1. 使用strchr检查当前字符是否在字符集中
** 2. 如果匹配，调用save_and_next保存并前进
** 3. 返回匹配结果（1匹配，0不匹配）
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param set - const char*：目标字符集合字符串
** @return int：匹配则返回1，不匹配返回0
**
** 算法复杂度：O(|set|) 时间，|set|为字符集大小
**
** 注意事项：
** - 仅在匹配时才会修改词法状态
** - 字符集使用C字符串格式
** - 匹配时自动保存字符到缓冲区
*/
static int check_next(LexState *ls, const char *set)
{
    if (!strchr(set, ls->current))
    {
        return 0;
    }
        
    save_and_next(ls);
    return 1;
}


/*
** [基础] 替换缓冲区中的字符
**
** 功能详述：
** 在词法分析器的字符缓冲区中查找指定字符并替换为目标字符
** 用于处理数字字面量中的本地化小数点字符转换等场景
**
** 替换处理流程：
** 1. 获取缓冲区内容和长度
** 2. 从后向前遍历缓冲区
** 3. 将所有匹配的源字符替换为目标字符
**
** @param ls - LexState*：词法状态指针，包含字符缓冲区
** @param from - char：需要被替换的源字符
** @param to - char：替换后的目标字符
**
** 算法复杂度：O(n) 时间，n为缓冲区长度
**
** 注意事项：
** - 操作直接修改缓冲区内容
** - 采用逆序遍历提高效率
** - 常用于本地化数字格式处理
*/
static void buffreplace(LexState *ls, char from, char to)
{
    size_t n = luaZ_bufflen(ls->buff);
    char *p = luaZ_buffer(ls->buff);
    
    while (n--)
    {
        if (p[n] == from) 
        {
            p[n] = to;
        }
    }
}


/*
** [基础] 尝试使用本地化的小数点
**
** 功能详述：
** 当数字解析失败时，尝试使用系统本地化设置的小数点字符重新解析
** 这样可以支持不同地区的数字格式（如欧洲的逗号小数点）
**
** 本地化处理流程：
** 1. 获取系统本地化的小数点字符
** 2. 替换缓冲区中的小数点字符
** 3. 重新尝试数字解析
** 4. 如果仍然失败，恢复原格式并报错
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param seminfo - SemInfo*：用于存储解析后的数字值
**
** 算法复杂度：O(n) 时间，n为缓冲区长度
**
** 注意事项：
** - 仅在标准解析失败时调用
** - 自动获取系统本地化设置
** - 失败时恢复原始格式以便错误报告
*/
static void trydecpoint(LexState *ls, SemInfo *seminfo)
{
    // 格式错误：尝试更新小数点分隔符
    struct lconv *cv = localeconv();
    char old = ls->decpoint;
    
    ls->decpoint = (cv ? cv->decimal_point[0] : '.');
    buffreplace(ls, old, ls->decpoint);  // 尝试更新的小数分隔符
    
    if (!luaO_str2d(luaZ_buffer(ls->buff), &seminfo->r)) {
        // 使用正确小数点仍然格式错误：没有更多选项
        buffreplace(ls, ls->decpoint, '.');  // 撤销更改(用于错误消息)
        luaX_lexerror(ls, "malformed number", TK_NUMBER);
    }
}


/*
** [核心] 读取数字字面量
**
** 功能详述：
** 解析各种格式的数字字面量，包括整数、浮点数和科学计数法表示
** 支持本地化小数点格式，确保跨平台兼容性
**
** 数字解析流程：
** 1. 读取主要数字部分（整数+小数）
** 2. 检查并处理科学计数法（E/e指数）
** 3. 尝试标准格式转换
** 4. 失败时尝试本地化小数点格式
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param seminfo - SemInfo*：输出解析后的数字值
**
** 算法复杂度：O(n) 时间，n为数字字符串长度
**
** 注意事项：
** - 支持标准和本地化小数点格式
** - 自动处理科学计数法
** - 解析失败时提供详细错误信息
*/
static void read_numeral(LexState *ls, SemInfo *seminfo)
{
    lua_assert(isdigit(ls->current));
    
    // 读取数字和小数点
    do {
        save_and_next(ls);
    } while (isdigit(ls->current) || ls->current == '.');
    
    // 检查科学计数法
    if (check_next(ls, "Ee"))          // 'E'?
    {
        check_next(ls, "+-");          // 可选的指数符号
    }
        
    // 继续读取字母数字字符
    while (isalnum(ls->current) || ls->current == '_')
    {
        save_and_next(ls);
    }
        
    save(ls, '\0');
    buffreplace(ls, '.', ls->decpoint);  // 遵循本地化小数点
    
    // 尝试转换为数字
    if (!luaO_str2d(luaZ_buffer(ls->buff), &seminfo->r))  // 格式错误?
        trydecpoint(ls, seminfo); // 尝试更新小数点分隔符
}


/*
** [基础] 跳过长字符串/注释的分隔符
**
** 功能详述：
** 解析长字符串和长注释的起始分隔符，计算其中等号的数量
** 用于确定匹配的结束分隔符格式，确保正确的字符串边界识别
**
** 分隔符格式：
** 1. 必须以'['开头
** 2. 可包含0个或多个'='字符  
** 3. 必须以'['结尾（如：[[, [=[, [===[等）
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @return int：分隔符中'='的个数，格式错误时返回负数
**
** 算法复杂度：O(n) 时间，n为'='字符的数量
**
** 注意事项：
** - 返回值用于匹配对应的结束分隔符
** - 格式错误时返回负数
** - 支持嵌套层级的长字符串
*/
static int skip_sep(LexState *ls)
{
    int count = 0;
    int s = ls->current;
    
    lua_assert(s == '[' || s == ']');
    save_and_next(ls);
    
    // 计算等号个数
    while (ls->current == '=') {
        save_and_next(ls);
        count++;
    }
    
    return (ls->current == s) ? count : (-count) - 1;
}


/*
** [核心] 读取长字符串或长注释
**
** 功能详述：
** 解析长字符串字面量或长注释块，支持多行内容和嵌套结构
** 通过匹配起始和结束分隔符来确定字符串边界
**
** 长字符串特性：
** 1. 支持换行符和特殊字符的原样保存
** 2. 不进行转义序列处理
** 3. 通过等号数量匹配起止分隔符
** 4. 可以作为字符串或注释使用
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param seminfo - SemInfo*：语义信息输出，NULL表示注释
** @param sep - int：起始分隔符中等号的个数
**
** 算法复杂度：O(n) 时间，n为字符串长度
**
** 注意事项：
** - seminfo为NULL时表示处理注释
** - 自动跳过首行换行符
** - 严格匹配分隔符等号数量
*/
static void read_long_string(LexState *ls, SemInfo *seminfo, int sep)
{
    int cont = 0;
    (void)(cont);  // 避免未使用变量警告
    
    save_and_next(ls);  // 跳过第二个'['
    
    // 如果字符串以换行符开始，跳过它
    if (currIsNewline(ls))
    {
        inclinenumber(ls);
    }
        
    for (;;) {
        switch (ls->current) {
            case EOZ:
                luaX_lexerror(ls, (seminfo) ? "unfinished long string" :
                                             "unfinished long comment", TK_EOS);
                break;  // 避免警告
                
#if defined(LUA_COMPAT_LSTR)
            case '[': {
                if (skip_sep(ls) == sep) {
                    save_and_next(ls);  // 跳过第二个'['
                    cont++;
#if LUA_COMPAT_LSTR == 1
                    if (sep == 0)
                    {
                        luaX_lexerror(ls, "nesting of [[...]] is deprecated", '[');
                    }
#endif
                }
                break;
            }
#endif
            case ']': {
                if (skip_sep(ls) == sep) {
                    save_and_next(ls);  // 跳过第二个']'
#if defined(LUA_COMPAT_LSTR) && LUA_COMPAT_LSTR == 2
                    cont--;
                    if (sep == 0 && cont >= 0) break;
#endif
                    goto endloop;
                }
                break;
            }
            
            case '\n':
            case '\r': {
                save(ls, '\n');
                inclinenumber(ls);
                // 如果是注释，避免浪费空间
                if (!seminfo) 
                {
                    luaZ_resetbuffer(ls->buff);
                }
                break;
            }
            
            default: {
                if (seminfo) 
                {
                    save_and_next(ls);
                }
                else 
                {
                    next(ls);
                }
            }
        }
    } 
    
endloop:
    if (seminfo)
    {
        seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + (2 + sep),
                                         luaZ_bufflen(ls->buff) - 2 * (2 + sep));
    }
}


/*
** [核心] 读取字符串字面量
**
** 功能详述：
** 解析单引号或双引号包围的字符串字面量，处理转义序列
** 并将解析后的字符串存储到语义信息结构中
**
** 字符串解析特性：
** 1. 支持单引号和双引号字符串
** 2. 处理各种转义序列（\n, \t, \", \\等）
** 3. 支持数字转义序列（\nnn）
** 4. 检查字符串边界和格式错误
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param del - int：字符串分隔符（'或"）
** @param seminfo - SemInfo*：输出解析后的字符串对象
**
** 算法复杂度：O(n) 时间，n为字符串长度
**
** 注意事项：
** - 自动处理转义序列
** - 检查未结束的字符串
** - 支持跨行字符串（通过转义）
*/
static void read_string(LexState *ls, int del, SemInfo *seminfo)
{
    save_and_next(ls);
    
    while (ls->current != del) {
        switch (ls->current) {
            case EOZ:
                luaX_lexerror(ls, "unfinished string", TK_EOS);
                continue;  // 避免警告
                
            case '\n':
            case '\r':
                luaX_lexerror(ls, "unfinished string", TK_STRING);
                continue;  // 避免警告
                
            case '\\': {
                int c;
                next(ls);  // 不保存'\'
                
                switch (ls->current) {
                    case 'a': c = '\a'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'v': c = '\v'; break;
                    case '\n':  // 继续处理
                    case '\r': 
                        save(ls, '\n'); 
                        inclinenumber(ls); 
                        continue;
                    case EOZ: 
                        continue;  // 下次循环会抛出错误
                        
                    default: {
                        if (!isdigit(ls->current))
                        {
                            save_and_next(ls);  // 处理 \\, \", \', \?
                        }
                        else {  // \xxx 数字转义
                            int i = 0;
                            c = 0;
                            do {
                                c = 10 * c + (ls->current - '0');
                                next(ls);
                            } while (++i < 3 && isdigit(ls->current));
                            
                            if (c > UCHAR_MAX)
                            {
                                luaX_lexerror(ls, "escape sequence too large", TK_STRING);
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
    
    save_and_next(ls);  // 跳过结束分隔符
    seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + 1,
                                     luaZ_bufflen(ls->buff) - 2);
}


/*
** [核心] 词法分析器主函数
**
** 功能详述：
** 词法分析的核心函数，从输入流中识别并返回下一个词法标记
** 处理所有Lua语言的词法元素，包括关键字、操作符、字面量等
**
** 标记识别类型：
** 1. 空白字符和注释处理
** 2. 数字字面量解析
** 3. 字符串字面量解析
** 4. 标识符和关键字识别
** 5. 操作符和分隔符识别
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
** @param seminfo - SemInfo*：输出标记的语义信息
** @return int：识别的标记类型（TK_*常量）
**
** 算法复杂度：O(n) 时间，n为标记长度
**
** 注意事项：
** - 自动跳过空白字符和注释
** - 处理所有Lua词法规则
** - 返回的语义信息类型依标记而定
*/
static int llex(LexState *ls, SemInfo *seminfo)
{
    luaZ_resetbuffer(ls->buff);
    
    for (;;) {
        switch (ls->current) {
            case '\n':
            case '\r': {
                inclinenumber(ls);
                continue;
            }
            
            case '-': {
                next(ls);
                if (ls->current != '-') 
                {
                    return '-';
                }
                
                // 注释处理
                next(ls);
                if (ls->current == '[') {
                    int sep = skip_sep(ls);
                    luaZ_resetbuffer(ls->buff);  // skip_sep可能弄脏缓冲区
                    if (sep >= 0) {
                        read_long_string(ls, NULL, sep);  // 长注释
                        luaZ_resetbuffer(ls->buff);
                        continue;
                    }
                }
                
                // 短注释
                while (!currIsNewline(ls) && ls->current != EOZ)
                {
                    next(ls);
                }
                continue;
            }
            
            case '[': {
                int sep = skip_sep(ls);
                if (sep >= 0) {
                    read_long_string(ls, seminfo, sep);
                    return TK_STRING;
                }
                else if (sep == -1) return '[';
                else luaX_lexerror(ls, "invalid long string delimiter", TK_STRING);
            }
            
            case '=': {
                next(ls);
                if (ls->current != '=') 
                {
                    return '=';
                }
                else { 
                    next(ls); 
                    return TK_EQ; 
                }
            }
            
            case '<': {
                next(ls);
                if (ls->current != '=') 
                {
                    return '<';
                }
                else { 
                    next(ls); 
                    return TK_LE; 
                }
            }
            
            case '>': {
                next(ls);
                if (ls->current != '=') 
                {
                    return '>';
                }
                else { 
                    next(ls); 
                    return TK_GE; 
                }
            }
            
            case '~': {
                next(ls);
                if (ls->current != '=') 
                {
                    return '~';
                }
                else { 
                    next(ls); 
                    return TK_NE; 
                }
            }
            
            case '"':
            case '\'': {
                read_string(ls, ls->current, seminfo);
                return TK_STRING;
            }
            
            case '.': {
                save_and_next(ls);
                if (check_next(ls, ".")) {
                    if (check_next(ls, "."))
                    {
                        return TK_DOTS;      // ...
                    }
                    else {
                        return TK_CONCAT;   // ..
                    }
                }
                else if (!isdigit(ls->current)) 
                {
                    return '.';
                }
                else {
                    read_numeral(ls, seminfo);
                    return TK_NUMBER;
                }
            }
            
            case EOZ: {
                return TK_EOS;
            }
            
            default: {
                if (isspace(ls->current)) {
                    lua_assert(!currIsNewline(ls));
                    next(ls);
                    continue;
                }
                else if (isdigit(ls->current)) {
                    read_numeral(ls, seminfo);
                    return TK_NUMBER;
                }
                else if (isalpha(ls->current) || ls->current == '_') {
                    // 标识符或保留字
                    TString *ts;
                    do {
                        save_and_next(ls);
                    } while (isalnum(ls->current) || ls->current == '_');
                    
                    ts = luaX_newstring(ls, luaZ_buffer(ls->buff),
                                            luaZ_bufflen(ls->buff));
                                            
                    // 检查是否为保留字
                    if (ts->tsv.reserved > 0)
                    {
                        return ts->tsv.reserved - 1 + FIRST_RESERVED;
                    }
                    else {
                        seminfo->ts = ts;
                        return TK_NAME;
                    }
                }
                else {
                    // 单字符标记 (+ - / ...)
                    int c = ls->current;
                    next(ls);
                    return c;
                }
            }
        }
    }
}


/*
** [基础] 读取下一个标记
**
** 功能详述：
** 从输入流中读取下一个词法标记，更新词法状态的当前标记信息
** 支持前瞻标记机制，优化语法分析的性能
**
** 标记读取流程：
** 1. 检查是否存在前瞻标记
** 2. 如果有前瞻标记，直接使用并清除
** 3. 否则调用llex函数读取新标记
** 4. 更新行号和标记信息
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
**
** 算法复杂度：O(1) 平均时间（考虑前瞻机制）
**
** 注意事项：
** - 支持一个标记的前瞻机制
** - 自动更新行号信息
** - 被语法分析器频繁调用
*/
void luaX_next(LexState *ls)
{
    ls->lastline = ls->linenumber;
    
    // 检查是否有前瞻标记
    if (ls->lookahead.token != TK_EOS) {
        ls->t = ls->lookahead;               // 使用前瞻标记
        ls->lookahead.token = TK_EOS;        // 清除前瞻标记
    }
    else {
        ls->t.token = llex(ls, &ls->t.seminfo);  // 读取下一个标记
    }
}


/*
** [基础] 前瞻一个标记
**
** 功能详述：
** 读取但不消费下一个词法标记，将其保存到前瞻缓冲区中
** 用于语法分析中需要查看下一个标记才能做决策的场景
**
** 前瞻机制：
** 1. 确保当前没有待处理的前瞻标记
** 2. 调用llex读取下一个标记
** 3. 将标记保存到lookahead缓冲区
** 4. 不修改当前标记状态
**
** @param ls - LexState*：词法状态指针，包含当前解析上下文
**
** 算法复杂度：O(n) 时间，n为标记长度
**
** 注意事项：
** - 仅支持一个标记的前瞻
** - 必须在没有前瞻标记时调用
** - 前瞻标记会在下次luaX_next时被使用
*/
void luaX_lookahead(LexState *ls)
{
    lua_assert(ls->lookahead.token == TK_EOS);
    ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
}