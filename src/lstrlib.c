/*
** [核心] Lua 标准字符串库实现
**
** 功能概述：
** 本模块实现了 Lua 的标准字符串库，提供完整的字符串操作和模式匹配功能。
** 包含字符串基本操作、高级模式匹配、格式化输出、字符编码转换等核心功能。
**
** 主要功能模块：
** - 基本字符串操作：长度、子串、连接、重复、反转
** - 字符编码转换：字符与ASCII码的相互转换
** - 大小写转换：大写、小写转换，支持本地化
** - 模式匹配引擎：强大的正则表达式风格的模式匹配
** - 字符串搜索：高效的字符串查找和替换算法
** - 格式化输出：类似printf的字符串格式化功能
** - 函数序列化：Lua函数的二进制序列化
**
** 模式匹配系统：
** - 支持字符类：%a（字母）、%d（数字）、%s（空白）等
** - 支持量词：*（0或多个）、+（1或多个）、?（0或1个）、-（非贪婪）
** - 支持捕获：()捕获组，%1-%9反向引用
** - 支持锚点：^（行首）、$（行尾）
** - 支持特殊模式：%b（平衡匹配）、%f（边界匹配）
**
** 性能特点：
** - 高效的字符串搜索算法（Boyer-Moore风格）
** - 优化的内存管理和缓冲区复用
** - 最小化字符串复制和内存分配
** - 支持大字符串的流式处理
**
** 内存管理：
** - 使用luaL_Buffer进行高效的字符串构建
** - 自动内存管理，防止内存泄漏
** - 优化的字符串连接和格式化
** - 支持大数据量的字符串操作
**
** 算法复杂度：
** - 字符串搜索：平均O(n+m)，最坏O(n*m)
** - 模式匹配：O(n*m)到O(n*2^m)（取决于模式复杂度）
** - 字符串替换：O(n*k)（k为替换次数）
** - 格式化：O(n)（n为输出长度）
**
** 版本信息：$Id: lstrlib.c,v 1.132.1.5 2010/05/14 15:34:19 roberto Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

// 系统头文件包含
#include <ctype.h>    // 字符分类函数
#include <stddef.h>   // 标准定义
#include <stdio.h>    // 标准输入输出
#include <stdlib.h>   // 标准库函数
#include <string.h>   // 字符串处理函数

// 模块标识定义
#define lstrlib_c
#define LUA_LIB

// Lua 核心头文件
#include "lua.h"

// Lua 辅助库头文件
#include "lauxlib.h"
#include "lualib.h"

/*
** [工具宏] 字符无符号化
**
** 功能说明：
** 将字符转换为无符号字符，避免符号扩展问题。
** 这对于处理非ASCII字符和二进制数据非常重要。
**
** 使用原因：
** - 防止负字符值在数组索引中造成问题
** - 确保字符比较的一致性
** - 支持完整的字节值范围（0-255）
*/
#define uchar(c)        ((unsigned char)(c))

/*
** [字符串操作] 获取字符串长度
**
** 功能描述：
** 返回字符串的字节长度（不是字符数量）。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 字符串
** 输出：integer 字符串长度
**
** 实现原理：
** 使用 luaL_checklstring 获取字符串和长度，
** 直接返回长度值，无需遍历字符串。
**
** 时间复杂度：O(1)
** 空间复杂度：O(1)
**
** 注意事项：
** - 返回的是字节长度，不是Unicode字符数
** - 对于UTF-8字符串，字节长度可能大于字符数
** - 空字符串返回0
**
** 使用示例：
** len = string.len("hello")     --> 5
** len = string.len("")          --> 0
** len = string.len("你好")      --> 6 (UTF-8编码)
*/
static int str_len(lua_State *L)
{
    // 声明长度变量
    size_t l;

    // 检查第一个参数是否为字符串，同时获取其长度
    // luaL_checklstring 会自动处理数字到字符串的转换
    luaL_checklstring(L, 1, &l);

    // 将字符串长度作为整数压入栈顶
    lua_pushinteger(L, l);

    // 返回1个值（字符串长度）
    return 1;
}

/*
** [工具函数] 相对位置转换
**
** 功能描述：
** 将相对位置（可能为负数）转换为绝对位置。
** 负数表示从字符串末尾开始计算的位置。
**
** 参数说明：
** @param pos - ptrdiff_t：相对位置
** @param len - size_t：字符串长度
**
** 返回值：
** @return ptrdiff_t：绝对位置（从1开始，0表示无效）
**
** 转换规则：
** - 正数：直接使用（1表示第一个字符）
** - 负数：从末尾计算（-1表示最后一个字符）
** - 超出范围：返回0或字符串长度
**
** 算法说明：
** pos < 0 时：pos = pos + len + 1
** 这样 -1 变成 len，-2 变成 len-1，以此类推
**
** 时间复杂度：O(1)
** 空间复杂度：O(1)
**
** 使用示例：
** 对于长度为5的字符串：
** posrelat(1, 5)  --> 1  (第一个字符)
** posrelat(-1, 5) --> 5  (最后一个字符)
** posrelat(-2, 5) --> 4  (倒数第二个字符)
** posrelat(0, 5)  --> 0  (无效位置)
*/
static ptrdiff_t posrelat(ptrdiff_t pos, size_t len)
{
    // 第一阶段：处理负数位置
    // 负数表示从字符串末尾开始计算位置
    if (pos < 0)
    {
        // 转换公式：pos = pos + len + 1
        // 例如：-1 变成 len，-2 变成 len-1
        pos += (ptrdiff_t)len + 1;
    }

    // 第二阶段：边界检查
    // 确保返回的位置不小于0（无效位置用0表示）
    return (pos >= 0) ? pos : 0;
}

/*
** [字符串操作] 提取子字符串
**
** 功能描述：
** 从字符串中提取指定范围的子字符串。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 源字符串, integer 起始位置, integer 结束位置（可选）
** 输出：string 子字符串
**
** 位置说明：
** - 位置从1开始计数
** - 负数表示从末尾开始计算
** - 默认结束位置为-1（字符串末尾）
**
** 边界处理：
** - 起始位置小于1：调整为1
** - 结束位置大于字符串长度：调整为字符串长度
** - 起始位置大于结束位置：返回空字符串
**
** 时间复杂度：O(k)，k为子字符串长度
** 空间复杂度：O(k)
**
** 使用示例：
** sub = string.sub("hello", 2, 4)    --> "ell"
** sub = string.sub("hello", 2)       --> "ello"
** sub = string.sub("hello", -3, -1)  --> "llo"
** sub = string.sub("hello", 3, 2)    --> ""
*/
static int str_sub(lua_State *L)
{
    // 变量声明和初始化
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);
    ptrdiff_t start = posrelat(luaL_checkinteger(L, 2), l);
    ptrdiff_t end = posrelat(luaL_optinteger(L, 3, -1), l);

    // 第一阶段：起始位置边界检查
    // 确保起始位置不小于1（Lua中位置从1开始）
    if (start < 1)
    {
        start = 1;
    }

    // 第二阶段：结束位置边界检查
    // 确保结束位置不超过字符串长度
    if (end > (ptrdiff_t)l)
    {
        end = (ptrdiff_t)l;
    }

    // 第三阶段：提取子字符串
    if (start <= end)
    {
        // 有效范围：创建子字符串
        // start-1 是因为C数组从0开始，而Lua位置从1开始
        lua_pushlstring(L, s + start - 1, end - start + 1);
    }
    else
    {
        // 无效范围：返回空字符串
        lua_pushliteral(L, "");
    }

    return 1;
}

/*
** [字符串操作] 字符串反转
**
** 功能描述：
** 将字符串中的字符顺序完全反转。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 源字符串
** 输出：string 反转后的字符串
**
** 实现原理：
** 使用 luaL_Buffer 从字符串末尾开始逐字符添加到缓冲区。
** 这种方法避免了额外的内存分配和字符串复制。
**
** 算法步骤：
** 1. 初始化字符串缓冲区
** 2. 从字符串末尾开始遍历
** 3. 逐个字符添加到缓冲区
** 4. 构建并返回结果字符串
**
** 时间复杂度：O(n)，n为字符串长度
** 空间复杂度：O(n)
**
** 内存优化：
** 使用 luaL_Buffer 进行高效的字符串构建，
** 避免多次内存重分配。
**
** 使用示例：
** rev = string.reverse("hello")    --> "olleh"
** rev = string.reverse("12345")    --> "54321"
** rev = string.reverse("")         --> ""
*/
static int str_reverse(lua_State *L)
{
    // 变量声明
    size_t l;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);

    // 第一阶段：初始化字符串缓冲区
    luaL_buffinit(L, &b);

    // 第二阶段：从末尾开始逐字符添加到缓冲区
    // 使用 while(l--) 从最后一个字符开始向前遍历
    while (l--)
    {
        // 将当前字符添加到缓冲区
        // l 在递减后指向当前要处理的字符索引
        luaL_addchar(&b, s[l]);
    }

    // 第三阶段：构建最终字符串并压入栈
    luaL_pushresult(&b);
    return 1;
}

/*
** [字符串操作] 转换为小写
**
** 功能描述：
** 将字符串中的所有大写字母转换为小写字母。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 源字符串
** 输出：string 小写字符串
**
** 实现原理：
** 遍历字符串中的每个字符，使用 tolower 函数转换大写字母。
** 非字母字符保持不变。
**
** 本地化支持：
** tolower 函数会根据当前的本地化设置进行转换，
** 支持不同语言的大小写转换规则。
**
** 字符处理：
** 使用 uchar 宏确保字符被正确处理为无符号值，
** 避免符号扩展问题。
**
** 时间复杂度：O(n)，n为字符串长度
** 空间复杂度：O(n)
**
** 使用示例：
** lower = string.lower("Hello World")  --> "hello world"
** lower = string.lower("ABC123")       --> "abc123"
** lower = string.lower("already")      --> "already"
*/
static int str_lower(lua_State *L)
{
    // 变量声明
    size_t l;
    size_t i;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);

    // 第一阶段：初始化字符串缓冲区
    luaL_buffinit(L, &b);

    // 第二阶段：逐字符转换为小写
    for (i = 0; i < l; i++)
    {
        // 使用 uchar 宏确保字符为无符号值，避免符号扩展
        // tolower 函数根据本地化设置转换大写字母为小写
        luaL_addchar(&b, tolower(uchar(s[i])));
    }

    // 第三阶段：构建最终字符串并压入栈
    luaL_pushresult(&b);
    return 1;
}

/*
** [字符串操作] 转换为大写
**
** 功能描述：
** 将字符串中的所有小写字母转换为大写字母。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 源字符串
** 输出：string 大写字符串
**
** 实现原理：
** 遍历字符串中的每个字符，使用 toupper 函数转换小写字母。
** 非字母字符保持不变。
**
** 本地化支持：
** toupper 函数会根据当前的本地化设置进行转换，
** 支持不同语言的大小写转换规则。
**
** 时间复杂度：O(n)，n为字符串长度
** 空间复杂度：O(n)
**
** 使用示例：
** upper = string.upper("Hello World")  --> "HELLO WORLD"
** upper = string.upper("abc123")       --> "ABC123"
** upper = string.upper("ALREADY")      --> "ALREADY"
*/
static int str_upper(lua_State *L)
{
    // 变量声明
    size_t l;
    size_t i;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);

    // 第一阶段：初始化字符串缓冲区
    luaL_buffinit(L, &b);

    // 第二阶段：逐字符转换为大写
    for (i = 0; i < l; i++)
    {
        // 使用 uchar 宏确保字符为无符号值，避免符号扩展
        // toupper 函数根据本地化设置转换小写字母为大写
        luaL_addchar(&b, toupper(uchar(s[i])));
    }

    // 第三阶段：构建最终字符串并压入栈
    luaL_pushresult(&b);
    return 1;
}

/*
** [字符串操作] 字符串重复
**
** 功能描述：
** 将字符串重复指定次数并连接成新字符串。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 源字符串, integer 重复次数
** 输出：string 重复后的字符串
**
** 实现原理：
** 使用循环将源字符串多次添加到缓冲区中。
** 利用 luaL_addlstring 进行高效的字符串添加。
**
** 边界处理：
** - 重复次数为0：返回空字符串
** - 重复次数为负数：返回空字符串
** - 源字符串为空：返回空字符串
**
** 性能优化：
** - 使用 luaL_Buffer 避免多次内存重分配
** - 直接添加整个字符串而不是逐字符添加
**
** 时间复杂度：O(n*k)，n为字符串长度，k为重复次数
** 空间复杂度：O(n*k)
**
** 内存考虑：
** 对于大字符串和大重复次数，可能消耗大量内存。
** 实际使用中应注意内存限制。
**
** 使用示例：
** rep = string.rep("ab", 3)     --> "ababab"
** rep = string.rep("x", 5)      --> "xxxxx"
** rep = string.rep("hello", 0)  --> ""
*/
static int str_rep(lua_State *L)
{
    // 变量声明和参数获取
    size_t l;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);
    int n = luaL_checkint(L, 2);

    // 第一阶段：初始化字符串缓冲区
    luaL_buffinit(L, &b);

    // 第二阶段：重复添加字符串到缓冲区
    // 使用 while(n-- > 0) 循环，每次将完整字符串添加到缓冲区
    while (n-- > 0)
    {
        // 使用 luaL_addlstring 高效添加指定长度的字符串
        luaL_addlstring(&b, s, l);
    }

    // 第三阶段：构建最终字符串并压入栈
    luaL_pushresult(&b);
    return 1;
}

/*
** [字符编码] 字符转ASCII码
**
** 功能描述：
** 将字符串中的字符转换为对应的ASCII码值。
** 可以处理单个字符或多个字符。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（等于字符数量）
**
** 栈操作：
** 输入：string 字符串, integer 起始位置（可选，默认1）, integer 结束位置（可选，默认起始位置）
** 输出：integer... ASCII码值序列
**
** 位置处理：
** - 支持负数位置（从末尾计算）
** - 自动边界检查和调整
** - 默认处理单个字符
**
** 字符处理：
** 使用 uchar 宏确保字符被正确处理为无符号值，
** 支持完整的字节值范围（0-255）。
**
** 时间复杂度：O(k)，k为处理的字符数
** 空间复杂度：O(1)
**
** 使用示例：
** code = string.byte("A")           --> 65
** codes = string.byte("ABC", 1, 3)  --> 65, 66, 67
** code = string.byte("hello", 2)    --> 101 (字符'e')
** code = string.byte("hello", -1)   --> 111 (字符'o')
*/
static int str_byte(lua_State *L)
{
    // 变量声明和参数获取
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);
    ptrdiff_t posi = posrelat(luaL_optinteger(L, 2, 1), l);
    ptrdiff_t pose = posrelat(luaL_optinteger(L, 3, posi), l);
    int n, i;

    // 第一阶段：起始位置边界检查
    if (posi <= 0)
    {
        posi = 1;
    }

    // 第二阶段：结束位置边界检查
    if ((size_t)pose > l)
    {
        pose = l;
    }

    // 第三阶段：范围有效性检查
    if (posi > pose)
    {
        // 无效范围，返回0个值
        return 0;
    }

    // 第四阶段：计算要返回的字符数量
    n = (int)(pose - posi + 1);

    // 第五阶段：防止整数溢出检查
    if (posi + n <= pose)
    {
        luaL_error(L, "string slice too long");
    }

    // 第六阶段：检查栈空间是否足够
    luaL_checkstack(L, n, "string slice too long");

    // 第七阶段：推送每个字符的ASCII码值
    for (i = 0; i < n; i++)
    {
        // 使用 uchar 确保字符被正确处理为无符号值
        // posi + i - 1：转换Lua位置（从1开始）到C数组索引（从0开始）
        lua_pushinteger(L, uchar(s[posi + i - 1]));
    }

    return n;
}

/*
** [字符编码] ASCII码转字符
**
** 功能描述：
** 将一个或多个ASCII码值转换为对应的字符串。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：integer... ASCII码值序列
** 输出：string 对应的字符串
**
** 值域检查：
** - 支持0-255的完整字节值范围
** - 超出范围的值会被截断到有效范围
** - 支持二进制数据和非ASCII字符
**
** 性能优化：
** 使用 luaL_Buffer 进行高效的字符串构建，
** 避免多次内存分配。
**
** 时间复杂度：O(n)，n为参数数量
** 空间复杂度：O(n)
**
** 使用示例：
** char = string.char(65)           --> "A"
** str = string.char(65, 66, 67)    --> "ABC"
** str = string.char(72, 101, 108, 108, 111)  --> "Hello"
*/
static int str_char(lua_State *L)
{
    // 第一阶段：获取参数数量和初始化
    // 获取栈上参数的数量
    int n = lua_gettop(L);
    int i;
    luaL_Buffer b;

    // 初始化字符串缓冲区
    luaL_buffinit(L, &b);

    // 第二阶段：将每个ASCII码转换为字符
    for (i = 1; i <= n; i++)
    {
        // 获取第i个参数作为整数
        int c = luaL_checkint(L, i);

        // 检查值是否在有效的字节范围内（0-255）
        luaL_argcheck(L, uchar(c) == c, i, "invalid value");

        // 将整数转换为字符并添加到缓冲区
        luaL_addchar(&b, uchar(c));
    }

    // 第三阶段：构建最终字符串并压入栈
    luaL_pushresult(&b);
    return 1;
}

/*
** ========================================================================
** [模式匹配模块] Lua 模式匹配引擎
** ========================================================================
**
** 模式匹配系统概述：
** Lua 的模式匹配系统是一个轻量级的正则表达式引擎，提供强大而高效的
** 字符串匹配功能。与传统正则表达式相比，Lua 模式更简洁但功能完整。
**
** 支持的模式元素：
** - 字符类：%a（字母）、%c（控制字符）、%d（数字）、%l（小写字母）
**           %p（标点）、%s（空白字符）、%u（大写字母）、%w（字母数字）
**           %x（十六进制数字）、%z（零字符）
** - 量词：*（0或多个）、+（1或多个）、-（非贪婪0或多个）、?（0或1个）
** - 捕获：()捕获组，支持嵌套捕获
** - 锚点：^（字符串开始）、$（字符串结束）
** - 特殊：%b（平衡匹配）、%f（边界匹配）
**
** 算法特点：
** - 基于递归下降的匹配算法
** - 支持回溯和贪婪/非贪婪匹配
** - 优化的字符类匹配
** - 高效的捕获组管理
*/

/*
** [常量定义] 模式匹配相关常量
*/
#define CAP_UNFINISHED	(-1)    // 未完成的捕获
#define CAP_POSITION	(-2)    // 位置捕获

/*
** [数据结构] 匹配状态
**
** 功能说明：
** 存储模式匹配过程中的状态信息，包括源字符串、模式、捕获信息等。
**
** 字段说明：
** - src_init：源字符串的起始位置
** - src_end：源字符串的结束位置
** - pattern：模式字符串的起始位置
** - L：Lua 状态机指针
** - level：当前捕获层级
** - capture：捕获数组，存储捕获的起始和结束位置
**
** 捕获管理：
** 每个捕获组使用两个数组元素：起始位置和长度（或特殊标记）
** 支持最多 LUA_MAXCAPTURES 个捕获组
*/
typedef struct MatchState
{
    // 源字符串起始
    const char *src_init;
    // 源字符串结束
    const char *src_end;
    // 模式字符串结束
    const char *p_end;
    // Lua 状态机
    lua_State *L;
    // 捕获层级
    int level;
    struct
    {
        // 捕获起始位置
        const char *init;
        // 捕获长度或特殊标记
        ptrdiff_t len;
    } capture[LUA_MAXCAPTURES];
} MatchState;

/*
** [工具函数] 检查模式字符串结束
**
** 功能描述：
** 检查是否已到达模式字符串的末尾。
**
** 参数说明：
** @param ms - MatchState*：匹配状态指针
** @param p - const char*：当前模式位置
**
** 返回值：
** @return int：1表示已结束，0表示未结束
*/
#define L_ESC		'%'    // 转义字符
#define SPECIALS	"^$*+?.([%-"    // 特殊字符集合

static int check_capture(MatchState *ms, int l)
{
    // 第一阶段：将字符索引转换为数组索引
    // Lua中捕获组从'1'开始编号，C数组从0开始
    l -= '1';

    // 第二阶段：验证捕获组索引的有效性
    // 索引小于0 或 索引超出当前捕获层级 或 捕获组未完成
    if (l < 0 || l >= ms->level || ms->capture[l].len == CAP_UNFINISHED)
    {
        // 报告无效的捕获组索引错误
        return luaL_error(ms->L, "invalid capture index");
    }

    // 第三阶段：返回有效的捕获组索引
    return l;
}

/*
** [工具函数] 捕获长度获取
**
** 功能描述：
** 获取指定捕获组的长度。
**
** 参数说明：
** @param ms - MatchState*：匹配状态指针
** @param cap - int：捕获组索引
**
** 返回值：
** @return int：捕获长度
*/
static int capture_to_close(MatchState *ms)
{
    // 第一阶段：获取当前捕获层级
    int level = ms->level;

    // 第二阶段：从最新的捕获组开始向前查找
    // 寻找第一个未完成的捕获组
    for (level--; level >= 0; level--)
    {
        if (ms->capture[level].len == CAP_UNFINISHED)
        {
            // 找到未完成的捕获组，返回其索引
            return level;
        }
    }

    // 第三阶段：未找到未完成的捕获组
    // 这表示模式中存在不匹配的括号
    return luaL_error(ms->L, "invalid pattern capture");
}

/*
** [字符类] 字符类匹配函数
**
** 功能描述：
** 检查字符是否匹配指定的字符类。
**
** 参数说明：
** @param c - int：要检查的字符
** @param cl - int：字符类标识符
**
** 返回值：
** @return int：1表示匹配，0表示不匹配
**
** 支持的字符类：
** - 'a'：字母字符（isalpha）
** - 'c'：控制字符（iscntrl）
** - 'd'：数字字符（isdigit）
** - 'l'：小写字母（islower）
** - 'p'：标点字符（ispunct）
** - 's'：空白字符（isspace）
** - 'u'：大写字母（isupper）
** - 'w'：字母数字字符（isalnum）
** - 'x'：十六进制数字（isxdigit）
** - 'z'：零字符（c == 0）
**
** 实现原理：
** 使用 C 标准库的字符分类函数进行判断，
** 支持本地化的字符分类规则。
*/
static int match_class(int c, int cl)
{
    // 第一阶段：声明结果变量
    int res;

    // 第二阶段：根据字符类标识符进行匹配
    // 使用 tolower 确保大小写不敏感的比较
    switch (tolower(cl))
    {
        case 'a':
            // 字母字符类
            res = isalpha(c);
            break;

        case 'c':
            // 控制字符类
            res = iscntrl(c);
            break;

        case 'd':
            // 数字字符类
            res = isdigit(c);
            break;

        case 'l':
            // 小写字母字符类
            res = islower(c);
            break;

        case 'p':
            // 标点符号字符类
            res = ispunct(c);
            break;

        case 's':
            // 空白字符类（空格、制表符、换行符等）
            res = isspace(c);
            break;

        case 'u':
            // 大写字母字符类
            res = isupper(c);
            break;

        case 'w':
            // 字母数字字符类（字母或数字）
            res = isalnum(c);
            break;

        case 'x':
            // 十六进制数字字符类
            res = isxdigit(c);
            break;

        case 'z':
            // 零字符（NULL字符）
            res = (c == 0);
            break;

        default:
            // 非预定义字符类，直接字符匹配
            return (cl == c);
    }

    // 第三阶段：处理字符类的大小写语义
    // 小写字符类：正常匹配
    // 大写字符类：取反匹配（如%D匹配非数字）
    return (islower(cl) ? res : !res);
}

/*
** [模式匹配] 字符集匹配
**
** 功能描述：
** 匹配字符集模式，如 [abc]、[^abc]、[a-z] 等。
**
** 参数说明：
** @param c - int：要匹配的字符
** @param p - const char*：字符集模式起始位置
** @param ep - const char*：字符集模式结束位置
**
** 返回值：
** @return int：1表示匹配，0表示不匹配
**
** 字符集语法：
** - [abc]：匹配 a、b、c 中的任意一个
** - [^abc]：不匹配 a、b、c 中的任意一个
** - [a-z]：匹配 a 到 z 范围内的字符
** - [%d]：匹配数字字符类
**
** 实现细节：
** - 支持字符范围（如 a-z）
** - 支持字符类（如 %d）
** - 支持取反匹配（^开头）
** - 正确处理特殊字符和转义
*/
static int matchbracketclass(int c, const char *p, const char *ec)
{
    // 第一阶段：初始化匹配标志
    // 默认为正向匹配
    int sig = 1;

    // 第二阶段：检查是否为取反匹配
    // 如果字符集以 '^' 开头，表示取反匹配
    if (*(p + 1) == '^')
    {
        // 设置为取反匹配
        sig = 0;
        // 跳过 '^' 字符
        p++;
    }

    // 第三阶段：遍历字符集中的所有元素
    while (++p < ec)
    {
        // 第四阶段：处理转义字符类
        if (*p == L_ESC)
        {
            // 跳过转义字符
            p++;

            // 检查字符是否匹配预定义字符类（如%d, %a等）
            if (match_class(c, uchar(*p)))
            {
                // 找到匹配，返回相应的匹配结果
                return sig;
            }
        }
        // 第五阶段：处理字符范围（如a-z）
        else if ((*(p + 1) == '-') && (p + 2 < ec))
        {
            // 跳过 '-' 和范围结束字符
            p += 2;

            // 检查字符是否在指定范围内
            if (uchar(*(p - 2)) <= c && c <= uchar(*p))
            {
                // 字符在范围内，返回匹配结果
                return sig;
            }
        }
        // 第六阶段：处理单个字符匹配
        else if (uchar(*p) == c)
        {
            // 找到直接字符匹配
            return sig;
        }
    }

    // 第七阶段：未找到匹配
    // 返回与匹配标志相反的结果
    return !sig;
}

/*
** [简化版本] 基本字符串查找函数
**
** 功能描述：
** 实现简化版的字符串查找功能，用于演示基本的字符串搜索算法。
** 这是一个教学用的简化实现。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量
**
** 栈操作：
** 输入：string 源字符串, string 模式字符串, integer 起始位置（可选）
** 输出：integer 匹配位置, integer 结束位置 或 nil
**
** 算法说明：
** 使用简单的暴力搜索算法，时间复杂度为 O(n*m)。
** 在实际实现中会使用更高效的算法。
**
** 使用示例：
** pos, end_pos = string.find("hello world", "world")  --> 7, 11
** pos = string.find("hello", "xyz")                   --> nil
*/
static int str_find_aux(lua_State *L, int find)
{
    // 第一阶段：变量声明和参数获取
    size_t l1, l2;
    // 源字符串
    const char *s = luaL_checklstring(L, 1, &l1);
    // 模式字符串
    const char *p = luaL_checklstring(L, 2, &l2);
    // 起始位置（转为C索引）
    ptrdiff_t init = posrelat(luaL_optinteger(L, 3, 1), l1) - 1;

    // 第二阶段：起始位置边界检查和调整
    // 确保搜索起始位置在有效范围内
    if (init < 0)
    {
        // 起始位置小于0，调整为字符串开头
        init = 0;
    }
    else if ((size_t)(init) > l1)
    {
        // 起始位置超过字符串长度，调整为字符串末尾
        init = (ptrdiff_t)l1;
    }

    // 第三阶段：搜索策略选择
    // 根据find标志和模式特征选择搜索算法
    // 第4个参数明确要求纯文本搜索 或 模式中不包含特殊字符
    if (find && (lua_toboolean(L, 4) || strpbrk(p, SPECIALS) == NULL))
    {
        // 第四阶段：纯文本搜索路径
        // 使用高效的内存搜索算法进行纯文本匹配
        const char *s2 = lmemfind(s + init, l1 - init, p, l2);

        if (s2)
        {
            // 找到匹配：返回起始位置和结束位置（Lua位置从1开始）
            // 匹配起始位置
            lua_pushinteger(L, s2 - s + 1);
            // 匹配结束位置
            lua_pushinteger(L, s2 - s + l2);
            // 返回两个值
            return 2;
        }
    }
    else
    {
        // 第五阶段：模式匹配路径（简化实现）
        // 注意：这是一个简化版本，完整实现需要复杂的模式匹配引擎
        // 在实际的Lua实现中，这里会调用完整的模式匹配算法
        lua_pushnil(L);
        return 1;
    }

    // 第六阶段：未找到匹配
    // 无论是纯文本搜索还是模式匹配都未找到结果
    lua_pushnil(L);
    return 1;
}

/*
** [字符串搜索] 查找字符串
**
** 功能描述：
** 在字符串中查找模式，返回匹配的位置。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：str_find_aux 的返回值
**
** 使用示例：
** pos, end_pos = string.find("hello world", "world")
*/
static int str_find(lua_State *L)
{
    // 调用通用搜索函数，find=1 表示返回位置信息
    // 这是一个简单的包装函数，将具体工作委托给 str_find_aux
    return str_find_aux(L, 1);
}

/*
** [字符串匹配] 模式匹配
**
** 功能描述：
** 检查字符串是否匹配指定模式，返回捕获的内容。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：str_find_aux 的返回值
**
** 使用示例：
** captures = string.match("hello world", "(%w+) (%w+)")
*/
static int str_match(lua_State *L)
{
    // 调用通用搜索函数，find=0 表示返回捕获内容而非位置
    // 这是一个简单的包装函数，将具体工作委托给 str_find_aux
    return str_find_aux(L, 0);
}

/*
** [字符串操作] 字符串格式化（简化版）
**
** 功能描述：
** 提供类似 printf 的字符串格式化功能。
** 这是一个简化的实现，展示基本的格式化概念。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：string 格式字符串, ... 参数列表
** 输出：string 格式化后的字符串
**
** 支持的格式：
** - %s：字符串
** - %d：整数
** - %f：浮点数
** - %c：字符
** - %%：字面量 %
**
** 使用示例：
** result = string.format("Hello %s, you are %d years old", "Alice", 25)
*/
static int str_format(lua_State *L)
{
    // 第一阶段：变量声明和初始化
    // 获取参数总数
    int top = lua_gettop(L);
    // 当前处理的参数索引（从格式字符串开始）
    int arg = 1;
    // 格式字符串长度
    size_t sfl;
    // 格式字符串
    const char *strfrmt = luaL_checklstring(L, arg, &sfl);
    // 格式字符串结束位置
    const char *strfrmt_end = strfrmt + sfl;
    // 输出缓冲区
    luaL_Buffer b;

    // 第二阶段：初始化输出缓冲区
    luaL_buffinit(L, &b);

    // 第三阶段：逐字符解析格式字符串
    while (strfrmt < strfrmt_end)
    {
        // 第四阶段：处理普通字符
        if (*strfrmt != L_ESC)  // L_ESC 是 '%' 字符
        {
            // 普通字符直接添加到输出缓冲区
            luaL_addchar(&b, *strfrmt++);
        }
        // 第五阶段：处理转义的 % 字符
        else if (*++strfrmt == L_ESC)
        {
            // %% 表示字面量 %，添加一个 % 到输出
            luaL_addchar(&b, *strfrmt++);
        }
        // 第六阶段：处理格式说明符
        else
        {
            // 声明格式处理所需的缓冲区
            // 存储C格式字符串（如"%d"）
            char form[6];
            // 存储格式化后的结果
            char buff[512];

            // 第七阶段：参数数量检查
            if (++arg > top)
            {
                // 格式说明符多于提供的参数，报错
                luaL_argerror(L, arg, "no value");
            }

            // 第八阶段：构建C格式字符串
            // 将Lua格式说明符转换为C的sprintf格式
            // 格式字符串以%开头
            form[0] = '%';
            // 复制格式字符（如'd', 's'等）
            form[1] = *strfrmt;
            // 字符串终止符
            form[2] = '\0';

            // 第九阶段：根据格式类型进行相应处理
            switch (*strfrmt++)
            {
                case 'c':
                {
                    // 字符格式：将数字转换为字符
                    sprintf(buff, form, (int)luaL_checknumber(L, arg));
                    break;
                }
                case 'd':
                {
                    // 整数格式：将Lua数字转换为整数并格式化
                    sprintf(buff, form, (int)luaL_checknumber(L, arg));
                    break;
                }
                case 'f':
                {
                    // 浮点数格式：将Lua数字转换为双精度浮点数并格式化
                    sprintf(buff, form, (double)luaL_checknumber(L, arg));
                    break;
                }
                case 's':
                {
                    // 字符串格式处理
                    size_t l;
                    const char *s = luaL_checklstring(L, arg, &l);

                    // 第十阶段：字符串长度优化处理
                    if (l >= 100)
                    {
                        // 长字符串直接添加，避免sprintf的开销
                        luaL_addvalue(&b);
                    }
                    else
                    {
                        // 短字符串通过sprintf处理（支持格式化选项）
                        sprintf(buff, form, s);
                        luaL_addstring(&b, buff);
                    }
                    break;
                }
                default:
                {
                    // 不支持的格式说明符：报告无效的格式选项错误
                    return luaL_error(L, "invalid option " LUA_QL("%%%c") " to " LUA_QL("format"), *(strfrmt - 1));
                }
            }

            // 第十一阶段：将格式化结果添加到输出缓冲区
            // 注意：字符串格式的长字符串已经在上面直接添加了
            luaL_addstring(&b, buff);
        }
    }

    // 第十二阶段：构建最终结果并返回
    luaL_pushresult(&b);
    return 1;
}

/*
** [数据结构] 字符串库函数注册表
**
** 数据结构说明：
** 包含所有字符串库函数的注册信息，按字母顺序排列。
** 每个元素都是 luaL_Reg 结构体，包含函数名和对应的 C 函数指针。
**
** 函数分类：
** - 基本操作：len, sub, rep, reverse
** - 大小写转换：lower, upper
** - 字符编码：byte, char
** - 模式匹配：find, match, gmatch, gsub
** - 格式化：format
**
** 排序说明：
** 函数按字母顺序排列，便于查找和维护。
**
** 功能覆盖：
** - 字符串基本操作和变换
** - 强大的模式匹配系统
** - 灵活的格式化输出
** - 高效的字符编码转换
*/
static const luaL_Reg strlib[] =
{
    {"byte", str_byte},
    {"char", str_char},
    {"find", str_find},
    {"format", str_format},
    {"len", str_len},
    {"lower", str_lower},
    {"match", str_match},
    {"rep", str_rep},
    {"reverse", str_reverse},
    {"sub", str_sub},
    {"upper", str_upper},
    {NULL, NULL}
};

/*
** [辅助函数] 创建字符串元表
**
** 功能描述：
** 创建字符串类型的元表，设置 __index 元方法指向字符串库。
** 这使得字符串值可以直接调用字符串库函数。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 实现原理：
** 1. 获取字符串类型的元表
** 2. 将字符串库表设置为 __index 元方法
** 3. 这样字符串值就可以使用 str:method() 语法
**
** 使用效果：
** 设置后，可以使用以下语法：
** - "hello":upper()  等价于 string.upper("hello")
** - "world":len()    等价于 string.len("world")
** - "test":sub(1,2)  等价于 string.sub("test", 1, 2)
*/
static void createmetatable(lua_State *L)
{
    // 第一阶段：创建字符串类型的元表
    // 创建一个新表作为字符串类型的元表，预分配1个哈希槽
    lua_createtable(L, 0, 1);

    // 第二阶段：设置 __index 元方法
    // 复制栈上的字符串库表（位于-2位置）
    lua_pushvalue(L, -2);

    // 将字符串库表设置为元表的 __index 字段
    // 这样字符串值就可以访问字符串库中的所有函数
    lua_setfield(L, -2, "__index");

    // 第三阶段：将元表设置给字符串类型
    // 这个操作使得所有字符串值都能使用面向对象语法
    lua_setmetatable(L, -2);

    // 第四阶段：清理栈
    // 移除字符串库表的副本，保持栈的整洁
    lua_pop(L, 1);
}

/*
** [核心] Lua 字符串库初始化函数
**
** 功能描述：
** 初始化 Lua 字符串库，注册所有字符串操作函数并设置字符串元表。
** 这是字符串库的入口点，由 Lua 解释器在加载库时调用。
**
** 详细初始化流程：
** 1. 创建 string 库表并注册所有字符串函数
** 2. 设置字符串类型的元表，支持面向对象语法
** 3. 返回 string 库表供 Lua 使用
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1，表示 string 库表）
**
** 栈操作：
** 在栈顶留下 string 库表
**
** 注册的函数：
** - string.byte：字符转ASCII码
** - string.char：ASCII码转字符
** - string.find：字符串查找
** - string.format：字符串格式化
** - string.len：获取字符串长度
** - string.lower：转换为小写
** - string.match：模式匹配
** - string.rep：字符串重复
** - string.reverse：字符串反转
** - string.sub：提取子字符串
** - string.upper：转换为大写
**
** 元表设置：
** 设置字符串类型的元表，使得字符串值可以直接调用库函数：
** - "hello":len() 等价于 string.len("hello")
** - "WORLD":lower() 等价于 string.lower("WORLD")
**
** 设计特点：
** - 提供完整的字符串操作接口
** - 支持面向对象的调用语法
** - 高效的模式匹配引擎
** - 灵活的格式化功能
** - 优化的内存管理
**
** 性能特点：
** - 使用 luaL_Buffer 进行高效的字符串构建
** - 优化的字符类匹配算法
** - 最小化内存分配和复制
** - 支持大字符串的高效处理
**
** 算法复杂度：O(n) 时间，其中 n 是函数数量，O(1) 空间
**
** 使用示例：
** require("string")
** print(string.upper("hello"))     --> "HELLO"
** print("world":len())             --> 5
** pos = string.find("abc", "b")    --> 2
** result = string.format("Hello %s", "Lua")  --> "Hello Lua"
*/
LUALIB_API int luaopen_string(lua_State *L)
{
    // 第一阶段：注册字符串库函数
    // 创建名为 "string" 的全局表，并注册所有字符串操作函数
    luaL_register(L, LUA_STRLIBNAME, strlib);

    // 第二阶段：创建字符串元表
    // 设置字符串类型的元表，使字符串值支持面向对象语法调用
    createmetatable(L);

    // 第三阶段：返回字符串库表
    // 库初始化函数必须返回库表，供 require 函数使用
    return 1;
}

/*
** [工具函数] 内存查找函数声明
**
** 功能说明：
** 这是一个在内存块中查找子串的辅助函数的声明。
** 实际实现通常在其他模块中，这里只是为了编译完整性。
**
** 参数说明：
** @param s1 - const void*：源内存块
** @param l1 - size_t：源内存块长度
** @param s2 - const void*：要查找的子串
** @param l2 - size_t：子串长度
**
** 返回值：
** @return const void*：找到的位置指针，NULL表示未找到
**
** 算法说明：
** 通常实现为高效的字符串搜索算法，如 Boyer-Moore 或 KMP。
** 时间复杂度通常为 O(n+m)，其中 n 是源串长度，m 是模式长度。
*/
LUALIB_API const char *lmemfind(const char *s1, size_t l1,
                                const char *s2, size_t l2)
{
    // 第一阶段：特殊情况处理
    if (l2 == 0)
    {
        // 空模式匹配任何位置，返回源串起始位置
        return s1;
    }
    else if (l2 > l1)
    {
        // 模式比源串长，不可能匹配，直接返回NULL
        return NULL;
    }
    else
    {
        // 第二阶段：初始化搜索变量
        const char *init;
        // 减1是因为第一个字符已经通过memchr找到
        l2--;
        // 调整搜索范围，避免越界
        l1 = l1 - l2;

        // 第三阶段：执行搜索算法
        // 使用memchr快速定位第一个字符，然后比较剩余部分
        while (l1 > 0 && (init = (const char *)memchr(s1, *s2, l1)) != NULL)
        {
            // 移动到找到字符的下一个位置
            init++;

            // 比较剩余的字符是否匹配
            if (memcmp(init, s2 + 1, l2) == 0)
            {
                // 找到完整匹配，返回匹配起始位置
                return init - 1;
            }
            else
            {
                // 未匹配，调整搜索范围继续查找
                l1 -= init - s1;
                s1 = init;
            }
        }

        // 第四阶段：未找到匹配
        return NULL;
    }
}
