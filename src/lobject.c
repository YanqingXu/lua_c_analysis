/*
** Lua对象系统的通用函数实现
** 包含对象类型转换、比较、格式化等核心功能
** 版权信息请参见lua.h文件
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
** 将整数转换为"浮点字节"格式
** 格式为(eeeeexxx)，其中真实值为：
** - 如果eeeee != 0：(1xxx) * 2^(eeeee - 1)
** - 如果eeeee == 0：(xxx)
** 用于紧凑存储小整数
*/
int luaO_int2fb(unsigned int x)
{
    /* 指数部分 */
    int e = 0;

    while (x >= 16)
    {
        /* 右移并进位 */
        x = (x + 1) >> 1;
        /* 增加指数 */
        e++;
    }

    if (x < 8)
    {
        /* 直接返回小值 */
        return x;
    }
    else
    {
        /* 编码为浮点字节格式 */
        return ((e + 1) << 3) | (cast_int(x) - 8);
    }
}


/*
** 将"浮点字节"格式转换回整数
** 与luaO_int2fb函数互为逆操作
*/
int luaO_fb2int(int x)
{
    /* 提取指数部分 */
    int e = (x >> 3) & 31;

    if (e == 0)
    {
        /* 直接返回小值 */
        return x;
    }
    else
    {
        /* 解码浮点字节格式 */
        return ((x & 7) + 8) << (e - 1);
    }
}


/*
** 计算无符号整数的以2为底的对数
** 使用查找表优化小值的计算
*/
int luaO_log2(unsigned int x)
{
    /* 预计算的对数查找表，用于0-255范围内的值 */
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

    int l = -1;

    /* 处理大于255的值，每次右移8位 */
    while (x >= 256)
    {
        l += 8;
        x >>= 8;
    }

    /* 返回最终的对数值 */
    return l + log_2[x];
}


/*
** 比较两个Lua值是否原始相等（不调用元方法）
** 用于内部比较操作，避免元方法调用的开销
*/
int luaO_rawequalObj(const TValue *t1, const TValue *t2)
{
    /* 类型不同则不相等 */
    if (ttype(t1) != ttype(t2))
    {
        return 0;
    }
    else
    {
        switch (ttype(t1))
        {
            case LUA_TNIL:
                /* nil值总是相等 */
                return 1;

            case LUA_TNUMBER:
                /* 数值比较 */
                return luai_numeq(nvalue(t1), nvalue(t2));

            case LUA_TBOOLEAN:
                /* 布尔值比较 */
                return bvalue(t1) == bvalue(t2);

            case LUA_TLIGHTUSERDATA:
                /* 轻量用户数据指针比较 */
                return pvalue(t1) == pvalue(t2);

            default:
                /* 断言是可回收对象 */
                lua_assert(iscollectable(t1));
                /* GC对象指针比较 */
                return gcvalue(t1) == gcvalue(t2);
        }
    }
}


/*
** 将字符串转换为数值
** 支持十进制和十六进制格式
** 返回1表示成功，0表示失败
*/
int luaO_str2d(const char *s, lua_Number *result)
{
    char *endptr;

    /* 尝试标准数值转换 */
    *result = lua_str2number(s, &endptr);
    if (endptr == s)
    {
        /* 转换失败，没有有效数字 */
        return 0;
    }

    /* 检查是否为十六进制常量 */
    if (*endptr == 'x' || *endptr == 'X')
    {
        /* 十六进制转换 */
        *result = cast_num(strtoul(s, &endptr, 16));
    }

    if (*endptr == '\0')
    {
        /* 最常见情况：字符串完全转换 */
        return 1;
    }

    /* 跳过尾部空白字符 */
    while (isspace(cast(unsigned char, *endptr)))
    {
        endptr++;
    }

    if (*endptr != '\0')
    {
        /* 存在无效的尾部字符 */
        return 0;
    }

    /* 转换成功 */
    return 1;
}


/*
** 将字符串推入Lua栈
** 内部辅助函数，用于字符串格式化操作
*/
static void pushstr(lua_State *L, const char *str)
{
    /* 创建字符串对象并设置到栈顶 */
    setsvalue2s(L, L->top, luaS_new(L, str));
    /* 增加栈顶指针 */
    incr_top(L);
}


/*
** 格式化字符串并推入栈（可变参数版本）
** 仅处理 %d、%c、%f、%p 和 %s 格式
** 这是Lua内部字符串格式化的核心函数
*/
const char *luaO_pushvfstring(lua_State *L, const char *fmt, va_list argp)
{
    /* 字符串片段计数 */
    int n = 1;
    /* 推入空字符串作为起始 */
    pushstr(L, "");

    for (;;)
    {
        /* 查找下一个格式符 */
        const char *e = strchr(fmt, '%');
        if (e == NULL)
        {
            /* 没有更多格式符 */
            break;
        }

        /* 推入格式符之前的字符串部分 */
        setsvalue2s(L, L->top, luaS_newlstr(L, fmt, e - fmt));
        incr_top(L);

        /* 处理格式符 */
        switch (*(e + 1))
        {
            /* 字符串格式 */
            case 's':
            {
                const char *s = va_arg(argp, char *);
                if (s == NULL)
                {
                    /* 处理空指针 */
                    s = "(null)";
                }
                pushstr(L, s);
                break;
            }

            /* 字符格式 */
            case 'c':
            {
                char buff[2];
                buff[0] = cast(char, va_arg(argp, int));
                buff[1] = '\0';
                pushstr(L, buff);
                break;
            }

            /* 整数格式 */
            case 'd':
            {
                setnvalue(L->top, cast_num(va_arg(argp, int)));
                incr_top(L);
                break;
            }

            /* 浮点数格式 */
            case 'f':
            {
                setnvalue(L->top, cast_num(va_arg(argp, l_uacNumber)));
                incr_top(L);
                break;
            }

            /* 指针格式 */
            case 'p':
            {
                /* 足够容纳指针格式的缓冲区 */
                char buff[4 * sizeof(void *) + 8];
                sprintf(buff, "%p", va_arg(argp, void *));
                pushstr(L, buff);
                break;
            }

            /* 转义的百分号 */
            case '%':
            {
                pushstr(L, "%");
                break;
            }

            /* 未知格式符，原样输出 */
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

        /* 增加字符串片段计数 */
        n += 2;
        /* 移动到下一个位置 */
        fmt = e + 2;
    }

    /* 推入剩余的字符串部分 */
    pushstr(L, fmt);
    /* 连接所有字符串片段 */
    luaV_concat(L, n + 1, cast_int(L->top - L->base) - 1);
    /* 调整栈顶，移除中间结果 */
    L->top -= n;
    /* 返回最终字符串 */
    return svalue(L->top - 1);
}


/*
** 格式化字符串并推入栈（标准参数版本）
** 这是luaO_pushvfstring的包装函数
*/
const char *luaO_pushfstring(lua_State *L, const char *fmt, ...)
{
    const char *msg;
    va_list argp;

    /* 初始化可变参数列表 */
    va_start(argp, fmt);
    /* 调用可变参数版本 */
    msg = luaO_pushvfstring(L, fmt, argp);
    /* 清理可变参数列表 */
    va_end(argp);

    return msg;
}


/*
** 生成代码块标识符
** 用于错误消息和调试信息中显示源代码位置
** 处理不同类型的源标识符格式
*/
void luaO_chunkid(char *out, const char *source, size_t bufflen)
{
    /* 字面量名称格式 */
    if (*source == '=')
    {
        /* 移除首个'='字符 */
        strncpy(out, source + 1, bufflen);
        /* 确保字符串以null结尾 */
        out[bufflen - 1] = '\0';
    }
    /* 文件名或字符串格式 */
    else
    {
        /* 文件名格式 */
        if (*source == '@')
        {
            size_t l;

            /* 跳过'@'字符 */
            source++;
            /* 为省略号预留空间 */
            bufflen -= sizeof(" '...' ");
            l = strlen(source);
            strcpy(out, "");

            /* 文件名太长需要截断 */
            if (l > bufflen)
            {
                /* 获取文件名的后半部分 */
                source += (l - bufflen);
                /* 添加省略号前缀 */
                strcat(out, "...");
            }

            /* 添加文件名 */
            strcat(out, source);
        }
        /* 字符串字面量格式 */
        else
        {
            /* 在第一个换行符处停止 */
            size_t len = strcspn(source, "\n\r");

            /* 为格式字符预留空间 */
            bufflen -= sizeof(" [string \"...\"] ");
            if (len > bufflen)
            {
                /* 限制长度 */
                len = bufflen;
            }

            strcpy(out, "[string \"");

            /* 需要截断？ */
            if (source[len] != '\0')
            {
                /* 添加截断的字符串 */
                strncat(out, source, len);
                /* 添加省略号 */
                strcat(out, "...");
            }
            else
            {
                /* 添加完整字符串 */
                strcat(out, source);
            }

            /* 添加结束标记 */
            strcat(out, "\"]");
        }
    }
}
