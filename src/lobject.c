/*
** [核心] Lua对象系统通用函数实现
**
** 功能概述：
** 本模块实现了Lua对象系统的核心通用函数，包括对象类型转换、比较、
** 格式化输出等基础操作。这些函数为Lua虚拟机的对象操作提供了
** 底层支持，是整个对象系统的基础组件。
**
** 主要功能：
** - 数值格式转换：整数与浮点字节格式的相互转换
** - 对象比较：原始相等性比较，不触发元方法
** - 字符串转换：字符串到数值的转换和验证
** - 格式化输出：类似printf的字符串格式化功能
** - 源码标识：生成调试和错误信息中的源码标识符
**
** 设计特点：
** - 高性能：使用查找表等优化技术提高执行效率
** - 类型安全：严格的类型检查和转换验证
** - 内存高效：紧凑的数据表示和最小化内存分配
** - 错误处理：完善的边界条件检查和错误恢复
**
** 核心算法：
** - 浮点字节编码：用于紧凑存储小整数的特殊格式
** - 对数计算：使用查找表优化的快速对数计算
** - 字符串解析：支持十进制和十六进制的数值解析
** - 格式化引擎：高效的字符串拼接和格式化处理
**
** 模块依赖：
** - ldo.c：执行控制和错误处理
** - lmem.c：内存管理接口
** - lstate.c：全局状态管理
** - lstring.c：字符串对象管理
** - lvm.c：虚拟机核心操作
**
** 相关文档：参见 docs/wiki_object.md
*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lobject_c
#define LUA_CORE

#include "lua.h"

#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "lvm.h"


/* 全局nil对象常量，用于表示空值 */
const TValue luaO_nilobject_ = {{NULL}, LUA_TNIL};


/*
** [工具] 将整数转换为"浮点字节"格式
**
** 详细功能说明：
** 将无符号整数转换为紧凑的"浮点字节"格式，用于节省存储空间。
** 格式为(eeeeexxx)，其中eeeee是5位指数，xxx是3位尾数。
** 真实值计算规则：
** - 如果eeeee != 0：(1xxx) * 2^(eeeee - 1)
** - 如果eeeee == 0：(xxx)
**
** 参数说明：
** @param x - unsigned int：要转换的无符号整数值
**
** 返回值：
** @return int：转换后的浮点字节格式值（0-255范围）
**
** 算法复杂度：O(log x) 时间，O(1) 空间
**
** 注意事项：
** - 主要用于Lua表的大小编码
** - 转换过程可能有精度损失
** - 与luaO_fb2int函数互为逆操作
*/
int luaO_int2fb(unsigned int x)
{
    // 初始化指数部分
    int e = 0;

    // 当值大于等于16时，进行指数编码
    while (x >= 16)
    {
        // 右移一位并进位，减少精度但保持近似值
        x = (x + 1) >> 1;
        // 增加指数计数
        e++;
    }

    // 小于8的值直接返回（指数为0的情况）
    if (x < 8)
    {
        return x;
    }
    else
    {
        // 编码为浮点字节格式：指数部分左移3位，加上尾数部分
        return ((e + 1) << 3) | (cast_int(x) - 8);
    }
}


/*
** [工具] 将"浮点字节"格式转换回整数
**
** 详细功能说明：
** 将luaO_int2fb函数生成的浮点字节格式转换回原始整数值。
** 这是浮点字节编码的逆操作，用于从紧凑格式恢复原始数值。
**
** 参数说明：
** @param x - int：浮点字节格式的值（0-255范围）
**
** 返回值：
** @return int：转换后的整数值
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 与luaO_int2fb函数互为逆操作
** - 转换结果可能与原始值有微小差异（由于精度损失）
** - 主要用于Lua表大小的解码
*/
int luaO_fb2int(int x)
{
    // 提取5位指数部分（右移3位，掩码31=0x1F）
    int e = (x >> 3) & 31;

    if (e == 0)
    {
        // 指数为0时，直接返回原值（小于8的情况）
        return x;
    }
    else
    {
        // 解码浮点字节格式：(尾数+8) * 2^(指数-1)
        return ((x & 7) + 8) << (e - 1);
    }
}


/*
** [优化] 计算无符号整数的以2为底的对数
**
** 详细功能说明：
** 计算无符号整数的以2为底的对数值（向下取整）。使用预计算的查找表
** 优化0-255范围内值的计算，对于更大的值采用分段处理策略。
**
** 参数说明：
** @param x - unsigned int：要计算对数的无符号整数
**
** 返回值：
** @return int：以2为底的对数值（向下取整）
**
** 算法复杂度：O(log x) 时间，O(1) 空间
**
** 注意事项：
** - 使用查找表优化小值计算，提高性能
** - 对于x=0的情况，结果为-1
** - 主要用于Lua表的容量计算和内存分配
*/
int luaO_log2(unsigned int x)
{
    // 预计算的对数查找表，用于0-255范围内的值
    // log_2[i] = floor(log2(i))，其中i为数组索引
    static const lu_byte log_2[256] =
    {
        0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
    };

    // 初始化对数值为-1（处理x=0的情况）
    int l = -1;

    // 处理大于255的值，每次右移8位相当于对数增加8
    while (x >= 256)
    {
        l += 8;
        x >>= 8;
    }

    // 使用查找表获取最终的对数值
    return l + log_2[x];
}


/*
** [核心] 比较两个Lua值是否原始相等（不调用元方法）
**
** 详细功能说明：
** 执行两个Lua值的原始相等性比较，不会触发任何元方法。这是虚拟机
** 内部使用的基础比较函数，用于避免元方法调用的开销和副作用。
**
** 参数说明：
** @param t1 - const TValue*：第一个要比较的Lua值
** @param t2 - const TValue*：第二个要比较的Lua值
**
** 返回值：
** @return int：相等返回1，不相等返回0
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 不会调用__eq元方法，是纯粹的值比较
** - 对于GC对象，比较的是对象指针而非内容
** - 用于虚拟机内部操作，避免递归调用
*/
int luaO_rawequalObj(const TValue *t1, const TValue *t2)
{
    // 首先比较类型，类型不同则必然不相等
    if (ttype(t1) != ttype(t2))
    {
        return 0;
    }
    else
    {
        // 根据类型进行相应的比较
        switch (ttype(t1))
        {
            case LUA_TNIL:
                // nil值总是相等
                return 1;

            case LUA_TNUMBER:
                // 数值比较，使用平台相关的数值相等判断
                return luai_numeq(nvalue(t1), nvalue(t2));

            case LUA_TBOOLEAN:
                // 布尔值直接比较
                return bvalue(t1) == bvalue(t2);

            case LUA_TLIGHTUSERDATA:
                // 轻量用户数据比较指针值
                return pvalue(t1) == pvalue(t2);

            default:
                // 其他类型都是GC对象，比较对象指针
                lua_assert(iscollectable(t1));
                return gcvalue(t1) == gcvalue(t2);
        }
    }
}


/*
** [工具] 将字符串转换为数值
**
** 详细功能说明：
** 将字符串转换为Lua数值类型，支持十进制和十六进制格式。
** 函数会跳过前导和尾随的空白字符，确保字符串被完全解析。
** 支持标准的十进制数值和0x前缀的十六进制数值。
**
** 参数说明：
** @param s - const char*：要转换的字符串
** @param result - lua_Number*：存储转换结果的指针
**
** 返回值：
** @return int：转换成功返回1，失败返回0
**
** 算法复杂度：O(n) 时间，其中n是字符串长度
**
** 注意事项：
** - 支持十进制和十六进制（0x前缀）格式
** - 会跳过尾随空白字符
** - 字符串必须完全被解析才算成功
** - 使用平台相关的lua_str2number和strtoul函数
*/
int luaO_str2d(const char *s, lua_Number *result)
{
    char *endptr;

    // 尝试使用标准函数进行数值转换
    *result = lua_str2number(s, &endptr);
    if (endptr == s)
    {
        // 转换失败，没有找到有效的数字
        return 0;
    }

    // 检查是否为十六进制常量（以x或X结尾）
    if (*endptr == 'x' || *endptr == 'X')
    {
        // 使用strtoul进行十六进制转换
        *result = cast_num(strtoul(s, &endptr, 16));
    }

    // 检查是否已经完全转换
    if (*endptr == '\0')
    {
        // 最常见情况：字符串完全转换成功
        return 1;
    }

    // 跳过尾部的空白字符
    while (isspace(cast(unsigned char, *endptr)))
    {
        endptr++;
    }

    // 检查是否还有未处理的字符
    if (*endptr != '\0')
    {
        // 存在无效的尾部字符，转换失败
        return 0;
    }

    // 转换成功
    return 1;
}


/*
** [内部] 将字符串推入Lua栈
**
** 详细功能说明：
** 创建一个新的Lua字符串对象并将其推入栈顶。这是格式化函数
** 使用的内部辅助函数，用于处理格式化过程中的字符串片段。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param str - const char*：要推入的C字符串
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是字符串长度
**
** 注意事项：
** - 会创建新的Lua字符串对象
** - 自动增加栈顶指针
** - 仅供内部格式化函数使用
*/
static void pushstr(lua_State *L, const char *str)
{
    // 创建新的Lua字符串对象并设置到栈顶
    setsvalue2s(L, L->top, luaS_new(L, str));

    // 增加栈顶指针
    incr_top(L);
}


/*
** [核心] 格式化字符串并推入栈（可变参数版本）
**
** 详细功能说明：
** 类似于printf的字符串格式化函数，但仅支持Lua内部需要的格式符。
** 支持的格式：%d（整数）、%c（字符）、%f（浮点数）、%p（指针）、%s（字符串）、%%（转义）。
** 格式化结果会作为Lua字符串推入栈顶。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param fmt - const char*：格式字符串
** @param argp - va_list：可变参数列表
**
** 返回值：
** @return const char*：格式化后的字符串指针
**
** 算法复杂度：O(n) 时间，其中n是格式字符串长度
**
** 注意事项：
** - 仅支持有限的格式符，不是完整的printf实现
** - 结果字符串会推入栈顶
** - 使用字符串连接操作合并所有片段
** - 对NULL字符串指针会输出"(null)"
*/
const char *luaO_pushvfstring(lua_State *L, const char *fmt, va_list argp)
{
    // 初始化字符串片段计数
    int n = 1;

    // 推入空字符串作为起始片段
    pushstr(L, "");

    for (;;)
    {
        // 查找下一个格式符的位置
        const char *e = strchr(fmt, '%');
        if (e == NULL)
        {
            // 没有更多格式符，退出循环
            break;
        }

        // 推入格式符之前的字符串部分
        setsvalue2s(L, L->top, luaS_newlstr(L, fmt, e - fmt));
        incr_top(L);

        // 根据格式符类型进行处理
        switch (*(e + 1))
        {
            // 字符串格式：%s
            case 's':
            {
                const char *s = va_arg(argp, char *);
                if (s == NULL)
                {
                    // 处理NULL指针，输出特殊标识
                    s = "(null)";
                }
                pushstr(L, s);
                break;
            }

            // 字符格式：%c
            case 'c':
            {
                char buff[2];
                buff[0] = cast(char, va_arg(argp, int));
                buff[1] = '\0';
                pushstr(L, buff);
                break;
            }

            // 整数格式：%d
            case 'd':
            {
                setnvalue(L->top, cast_num(va_arg(argp, int)));
                incr_top(L);
                break;
            }

            // 浮点数格式：%f
            case 'f':
            {
                setnvalue(L->top, cast_num(va_arg(argp, l_uacNumber)));
                incr_top(L);
                break;
            }

            // 指针格式：%p
            case 'p':
            {
                // 分配足够容纳指针格式的缓冲区
                char buff[4 * sizeof(void *) + 8];
                sprintf(buff, "%p", va_arg(argp, void *));
                pushstr(L, buff);
                break;
            }

            // 转义的百分号：%%
            case '%':
            {
                pushstr(L, "%");
                break;
            }

            // 未知格式符，原样输出
            default:
            {
                char buff[3];
                buff[0] = '%';
                buff[1] = *(e + 1);
                buff[2] = '\0';
                pushstr(L, buff);
                break;
            }
        }

        // 增加字符串片段计数
        n += 2;

        // 移动到下一个位置（跳过格式符）
        fmt = e + 2;
    }

    // 推入剩余的字符串部分
    pushstr(L, fmt);

    // 使用虚拟机的字符串连接操作合并所有片段
    luaV_concat(L, n + 1, cast_int(L->top - L->base) - 1);

    // 调整栈顶，移除中间结果
    L->top -= n;

    // 返回最终格式化的字符串
    return svalue(L->top - 1);
}


/*
** [工具] 格式化字符串并推入栈（标准参数版本）
**
** 详细功能说明：
** luaO_pushvfstring函数的包装版本，提供标准的可变参数接口。
** 内部调用luaO_pushvfstring完成实际的格式化工作。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param fmt - const char*：格式字符串
** @param ... - 可变参数：与格式字符串对应的参数
**
** 返回值：
** @return const char*：格式化后的字符串指针
**
** 算法复杂度：O(n) 时间，其中n是格式字符串长度
**
** 注意事项：
** - 这是luaO_pushvfstring的便利包装函数
** - 支持相同的格式符集合
** - 结果字符串会推入栈顶
*/
const char *luaO_pushfstring(lua_State *L, const char *fmt, ...)
{
    const char *msg;
    va_list argp;

    // 初始化可变参数列表
    va_start(argp, fmt);

    // 调用可变参数版本进行实际的格式化
    msg = luaO_pushvfstring(L, fmt, argp);

    // 清理可变参数列表
    va_end(argp);

    return msg;
}


/*
** [工具] 生成代码块标识符
**
** 详细功能说明：
** 根据源码名称生成适合在错误信息和调试信息中显示的标识符。
** 处理三种类型的源码标识符：
** - '='开头：直接使用指定的名称
** - '@'开头：文件名，可能需要截取
** - 其他：字符串源码，需要特殊格式化
**
** 参数说明：
** @param out - char*：输出缓冲区，存储生成的标识符
** @param source - const char*：源码名称字符串
** @param bufflen - size_t：输出缓冲区的大小
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是源码名称长度
**
** 注意事项：
** - 输出缓冲区必须足够大以容纳结果
** - 长文件名和字符串会被适当截取
** - 结果字符串总是以null结尾
*/
void luaO_chunkid(char *out, const char *source, size_t bufflen)
{
    // 检查源码名称的类型标识符
    if (*source == '=')
    {
        // '='开头：字面量名称格式，直接使用指定的名称
        strncpy(out, source + 1, bufflen);
        // 确保字符串以null结尾
        out[bufflen - 1] = '\0';
    }
    // 处理文件名或字符串格式
    else
    {
        // '@'开头：文件名格式
        if (*source == '@')
        {
            size_t l;

            // 跳过'@'前缀字符
            source++;

            // 为省略号预留空间
            bufflen -= sizeof(" '...' ");
            l = strlen(source);
            strcpy(out, "");

            // 检查文件名是否太长需要截断
            if (l > bufflen)
            {
                // 获取文件名的后半部分
                source += (l - bufflen);

                // 添加省略号前缀
                strcat(out, "...");
            }

            // 添加文件名到输出缓冲区
            strcat(out, source);
        }
        // 其他情况：字符串字面量格式
        else
        {
            // 在第一个换行符处停止，只显示第一行
            size_t len = strcspn(source, "\n\r");

            // 为格式字符预留空间
            bufflen -= sizeof(" [string \"...\"] ");
            if (len > bufflen)
            {
                // 限制显示长度
                len = bufflen;
            }

            // 开始构建字符串格式
            strcpy(out, "[string \"");

            // 检查是否需要截断
            if (source[len] != '\0')
            {
                // 添加截断的字符串部分
                strncat(out, source, len);

                // 添加省略号表示截断
                strcat(out, "...");
            }
            else
            {
                // 添加完整的字符串
                strcat(out, source);
            }

            // 添加结束标记
            strcat(out, "\"]");
        }
    }
}
