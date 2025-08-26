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
** 将字符保存到词法分析器的缓冲区中
** ls: 词法状态
** c: 要保存的字符
*/
static void save(LexState *ls, int c)
{
    Mbuffer *b = ls->buff;
    
    /* 检查缓冲区是否需要扩容 */
    if (b->n + 1 > b->buffsize) {
        size_t newsize;
        
        /* 防止缓冲区过大 */
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
** 初始化词法分析器 - 注册所有保留字
** 将所有保留字加入字符串表，并标记为保留字，确保它们不会被垃圾回收
** L: Lua状态机
*/
void luaX_init(lua_State *L)
{
    int i;
    
    /*
    ** 遍历所有保留字并注册到字符串表中
    */
    for (i = 0; i < NUM_RESERVED; i++) {
        TString *ts = luaS_new(L, luaX_tokens[i]);
        
        /*
        ** 保留字永远不会被垃圾回收
        */
        luaS_fix(ts);
        
        lua_assert(strlen(luaX_tokens[i]) + 1 <= TOKEN_LEN);
        
        /*
        ** 标记为保留字，索引从1开始
        */
        ts->tsv.reserved = cast_byte(i + 1);
    }
}


/* 错误信息中源代码最大显示长度 */
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
** 抛出词法错误
** ls: 词法状态
** msg: 错误消息
** token: 出错的标记
*/
void luaX_lexerror(LexState *ls, const char *msg, int token)
{
    char buff[MAXSRC];
    
    /* 获取源文件信息 */
    luaO_chunkid(buff, getstr(ls->source), MAXSRC);
    msg = luaO_pushfstring(ls->L, "%s:%d: %s", buff, ls->linenumber, msg);
    
    /* 如果有具体的标记，添加标记信息 */
    if (token)
    {
        luaO_pushfstring(ls->L, "%s near " LUA_QS, msg, txtToken(ls, token));
    }
        
    luaD_throw(ls->L, LUA_ERRSYNTAX);
}


/*
** 抛出语法错误
** ls: 词法状态
** msg: 错误消息
*/
void luaX_syntaxerror(LexState *ls, const char *msg)
{
    luaX_lexerror(ls, msg, ls->t.token);
}


/*
** 创建新的字符串对象并加入常量表
** ls: 词法状态
** str: 字符串内容
** l: 字符串长度
** 返回: 字符串对象
*/
TString *luaX_newstring(LexState *ls, const char *str, size_t l)
{
    lua_State *L = ls->L;
    TString *ts = luaS_newlstr(L, str, l);
    TValue *o = luaH_setstr(L, ls->fs->h, ts);  /* 加入常量表 */
    
    /* 如果是新字符串，确保不会被垃圾回收 */
    if (ttisnil(o)) {
        setbvalue(o, 1);  /* 标记为已使用 */
        luaC_checkGC(L);
    }
    
    return ts;
}


/*
** 递增行号计数器
** ls: 词法状态
*/
static void inclinenumber(LexState *ls)
{
    int old = ls->current;
    lua_assert(currIsNewline(ls));
    
    next(ls);  /* 跳过 '\n' 或 '\r' */
    
    /* 处理 '\n\r' 或 '\r\n' 的情况 */
    if (currIsNewline(ls) && ls->current != old)
    {
        next(ls);
    }
        
    /* 检查行号是否溢出 */
    if (++ls->linenumber >= MAX_INT)
    {
        luaX_syntaxerror(ls, "chunk has too many lines");
    }
}


/*
** 设置词法分析器的输入源
** L: Lua状态机
** ls: 词法状态
** z: 输入流
** source: 源文件名
*/
void luaX_setinput(lua_State *L, LexState *ls, ZIO *z, TString *source)
{
    ls->decpoint = '.';                      /* 默认小数点字符 */
    ls->L = L;                              /* Lua状态机 */
    ls->lookahead.token = TK_EOS;           /* 无前瞻标记 */
    ls->z = z;                              /* 输入流 */
    ls->fs = NULL;                          /* 函数状态 */
    ls->linenumber = 1;                     /* 初始行号 */
    ls->lastline = 1;                       /* 上一行号 */
    ls->source = source;                    /* 源文件名 */
    
    /* 初始化缓冲区 */
    luaZ_resizebuffer(ls->L, ls->buff, LUA_MINBUFFER);
    next(ls);  /* 读取第一个字符 */
}


/* =================================================================== */
/*                           词法分析器核心                               */
/* =================================================================== */


/*
** 检查当前字符是否在指定字符集中
** ls: 词法状态
** set: 字符集合
** 返回: 如果匹配则保存字符并返回1，否则返回0
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
** 替换缓冲区中的字符
** ls: 词法状态
** from: 源字符
** to: 目标字符
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
** 尝试使用本地化的小数点
** ls: 词法状态
** seminfo: 语义信息
*/
static void trydecpoint(LexState *ls, SemInfo *seminfo)
{
    /* 格式错误：尝试更新小数点分隔符 */
    struct lconv *cv = localeconv();
    char old = ls->decpoint;
    
    ls->decpoint = (cv ? cv->decimal_point[0] : '.');
    buffreplace(ls, old, ls->decpoint);  /* 尝试更新的小数分隔符 */
    
    if (!luaO_str2d(luaZ_buffer(ls->buff), &seminfo->r)) {
        /* 使用正确小数点仍然格式错误：没有更多选项 */
        buffreplace(ls, ls->decpoint, '.');  /* 撤销更改(用于错误消息) */
        luaX_lexerror(ls, "malformed number", TK_NUMBER);
    }
}


/*
** 读取数字字面量
** ls: 词法状态
** seminfo: 语义信息输出
*/
static void read_numeral(LexState *ls, SemInfo *seminfo)
{
    lua_assert(isdigit(ls->current));
    
    /* 读取数字和小数点 */
    do {
        save_and_next(ls);
    } while (isdigit(ls->current) || ls->current == '.');
    
    /* 检查科学计数法 */
    if (check_next(ls, "Ee"))          /* 'E'? */
    {
        check_next(ls, "+-");          /* 可选的指数符号 */
    }
        
    /* 继续读取字母数字字符 */
    while (isalnum(ls->current) || ls->current == '_')
    {
        save_and_next(ls);
    }
        
    save(ls, '\0');
    buffreplace(ls, '.', ls->decpoint);  /* 遵循本地化小数点 */
    
    /* 尝试转换为数字 */
    if (!luaO_str2d(luaZ_buffer(ls->buff), &seminfo->r))  /* 格式错误? */
        trydecpoint(ls, seminfo); /* 尝试更新小数点分隔符 */
}


/*
** 跳过长字符串/注释的分隔符
** ls: 词法状态
** 返回: 分隔符中'='的个数，如果格式错误返回负数
*/
static int skip_sep(LexState *ls)
{
    int count = 0;
    int s = ls->current;
    
    lua_assert(s == '[' || s == ']');
    save_and_next(ls);
    
    /* 计算等号个数 */
    while (ls->current == '=') {
        save_and_next(ls);
        count++;
    }
    
    return (ls->current == s) ? count : (-count) - 1;
}


/*
** 读取长字符串或长注释
** ls: 词法状态
** seminfo: 语义信息(如果为NULL则是注释)
** sep: 分隔符中等号的个数
*/
static void read_long_string(LexState *ls, SemInfo *seminfo, int sep)
{
    int cont = 0;
    (void)(cont);  /* 避免未使用变量警告 */
    
    save_and_next(ls);  /* 跳过第二个'[' */
    
    /* 如果字符串以换行符开始，跳过它 */
    if (currIsNewline(ls))
    {
        inclinenumber(ls);
    }
        
    for (;;) {
        switch (ls->current) {
            case EOZ:
                luaX_lexerror(ls, (seminfo) ? "unfinished long string" :
                                             "unfinished long comment", TK_EOS);
                break;  /* 避免警告 */
                
#if defined(LUA_COMPAT_LSTR)
            case '[': {
                if (skip_sep(ls) == sep) {
                    save_and_next(ls);  /* 跳过第二个'[' */
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
                    save_and_next(ls);  /* 跳过第二个']' */
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
                /* 如果是注释，避免浪费空间 */
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
** 读取字符串字面量
** ls: 词法状态
** del: 字符串分隔符(单引号或双引号)
** seminfo: 语义信息输出
*/
static void read_string(LexState *ls, int del, SemInfo *seminfo)
{
    save_and_next(ls);
    
    while (ls->current != del) {
        switch (ls->current) {
            case EOZ:
                luaX_lexerror(ls, "unfinished string", TK_EOS);
                continue;  /* 避免警告 */
                
            case '\n':
            case '\r':
                luaX_lexerror(ls, "unfinished string", TK_STRING);
                continue;  /* 避免警告 */
                
            case '\\': {
                int c;
                next(ls);  /* 不保存'\' */
                
                switch (ls->current) {
                    case 'a': c = '\a'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'v': c = '\v'; break;
                    case '\n':  /* 继续处理 */
                    case '\r': 
                        save(ls, '\n'); 
                        inclinenumber(ls); 
                        continue;
                    case EOZ: 
                        continue;  /* 下次循环会抛出错误 */
                        
                    default: {
                        if (!isdigit(ls->current))
                        {
                            save_and_next(ls);  /* 处理 \\, \", \', \? */
                        }
                        else {  /* \xxx 数字转义 */
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
    
    save_and_next(ls);  /* 跳过结束分隔符 */
    seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + 1,
                                     luaZ_bufflen(ls->buff) - 2);
}


/*
** 词法分析器主函数
** ls: 词法状态
** seminfo: 语义信息输出
** 返回: 标记类型
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
                
                /* 注释处理 */
                next(ls);
                if (ls->current == '[') {
                    int sep = skip_sep(ls);
                    luaZ_resetbuffer(ls->buff);  /* skip_sep可能弄脏缓冲区 */
                    if (sep >= 0) {
                        read_long_string(ls, NULL, sep);  /* 长注释 */
                        luaZ_resetbuffer(ls->buff);
                        continue;
                    }
                }
                
                /* 短注释 */
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
                        return TK_DOTS;      /* ... */
                    }
                    else 
                    {
                        return TK_CONCAT;   /* .. */
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
                    /* 标识符或保留字 */
                    TString *ts;
                    do {
                        save_and_next(ls);
                    } while (isalnum(ls->current) || ls->current == '_');
                    
                    ts = luaX_newstring(ls, luaZ_buffer(ls->buff),
                                            luaZ_bufflen(ls->buff));
                                            
                    /* 检查是否为保留字 */
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
                    /* 单字符标记 (+ - / ...) */
                    int c = ls->current;
                    next(ls);
                    return c;
                }
            }
        }
    }
}


/*
** 读取下一个标记
** ls: 词法状态
*/
void luaX_next(LexState *ls)
{
    ls->lastline = ls->linenumber;
    
    /* 检查是否有前瞻标记 */
    if (ls->lookahead.token != TK_EOS) {
        ls->t = ls->lookahead;               /* 使用前瞻标记 */
        ls->lookahead.token = TK_EOS;        /* 清除前瞻标记 */
    }
    else {
        ls->t.token = llex(ls, &ls->t.seminfo);  /* 读取下一个标记 */
    }
}


/*
** 前瞻一个标记
** ls: 词法状态
*/
void luaX_lookahead(LexState *ls)
{
    lua_assert(ls->lookahead.token == TK_EOS);
    ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
}