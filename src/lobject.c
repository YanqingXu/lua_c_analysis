/**
 * @file lobject.c
 * @brief Lua对象系统：通用对象操作和数值处理
 *
 * 详细说明：
 * 本文件实现了Lua对象系统的通用操作，包括数值编码、字符串转换、
 * 对象比较和格式化输出等核心功能。这些函数为Lua的动态类型系统
 * 提供了基础支持。
 *
 * 核心功能：
 * 1. 浮点字节编码：将整数压缩为浮点字节表示
 * 2. 对数计算：高效的整数对数计算
 * 3. 对象比较：统一的对象相等性判断
 * 4. 数值转换：字符串到数值的解析
 * 5. 格式化输出：类似printf的字符串格式化
 * 6. 源码信息：调试用的源码位置格式化
 *
 * 设计特色：
 * - 统一抽象：为所有Lua值类型提供统一操作
 * - 高效编码：使用位操作实现紧凑的数值表示
 * - 精确转换：支持十进制和十六进制数值解析
 * - 调试友好：提供丰富的调试信息格式化
 *
 * 浮点字节编码：
 * 使用(eeeeexxx)格式表示数值，其中：
 * - eeeee：5位指数部分
 * - xxx：3位尾数部分
 * - 实际值：(1xxx) * 2^(eeeee-1) 当eeeee!=0
 * - 实际值：(xxx) 当eeeee==0
 *
 * 技术亮点：
 * - 位操作优化：高效的位运算实现
 * - 查表算法：使用预计算表加速对数计算
 * - 状态机解析：复杂的字符串解析逻辑
 * - 内存安全：严格的边界检查和错误处理
 *
 * 应用场景：
 * - 表大小编码：将表大小压缩为字节表示
 * - 数值解析：解析Lua源码中的数值常量
 * - 错误报告：格式化错误消息和调试信息
 * - 对象操作：实现对象的基本操作
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2007-12-27
 * @since Lua 5.0
 * @see lobject.h, lvm.h, lstring.h
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

// ============================================================================
// 全局常量定义
// ============================================================================

/**
 * @brief 全局nil对象
 *
 * 详细说明：
 * 这是一个全局的nil值对象，用于表示Lua中的nil值。
 * 所有需要返回nil的地方都可以返回这个对象的指针。
 *
 * 设计优势：
 * - 内存效率：避免重复创建nil对象
 * - 比较效率：可以使用指针比较
 * - 类型安全：确保nil值的一致性
 */
const TValue luaO_nilobject_ = {{NULL}, LUA_TNIL};

/**
 * @brief 将整数转换为浮点字节表示
 * @param x 要转换的无符号整数
 * @return 浮点字节表示（8位）
 *
 * 详细说明：
 * 这个函数将一个整数转换为紧凑的浮点字节表示，格式为(eeeeexxx)，
 * 其中eeeee是5位指数，xxx是3位尾数。这种编码用于表示表的大小等。
 *
 * 编码格式：
 * - 当x < 8时：直接返回x（指数为0）
 * - 当x >= 8时：使用指数形式 (1xxx) * 2^(eeeee-1)
 *
 * 算法步骤：
 * 1. 通过右移将x规约到[8,15]范围内
 * 2. 记录右移次数作为指数
 * 3. 将指数和尾数组合成8位表示
 *
 * 精度处理：
 * 在右移过程中使用(x+1)>>1而不是x>>1，这相当于四舍五入，
 * 减少了精度损失。
 *
 * 表示范围：
 * - 最小值：0
 * - 最大值：约2^30（当指数为31时）
 * - 精度：随着数值增大而降低
 *
 * 使用场景：
 * - 表大小的紧凑表示
 * - 内存使用的优化
 * - 配置参数的编码
 *
 * @pre x必须是有效的无符号整数
 * @post 返回8位的浮点字节表示
 *
 * @note 这是一种有损压缩，大数值会失去精度
 * @see luaO_fb2int()
 */
int luaO_int2fb(unsigned int x) {
    int e = 0;  // 指数计数器

    // 将x规约到[8,15]范围，记录指数
    while (x >= 16) {
        x = (x + 1) >> 1;  // 四舍五入的右移
        e++;
    }

    if (x < 8) {
        return x;  // 小数值直接表示
    } else {
        // 组合指数和尾数：((e+1) << 3) | (x-8)
        return ((e + 1) << 3) | (cast_int(x) - 8);
    }
}

/**
 * @brief 将浮点字节表示转换回整数
 * @param x 浮点字节表示（8位）
 * @return 对应的整数值
 *
 * 详细说明：
 * 这个函数是luaO_int2fb的逆操作，将浮点字节表示转换回整数。
 *
 * 解码过程：
 * 1. 提取指数部分：(x >> 3) & 31
 * 2. 提取尾数部分：x & 7
 * 3. 根据指数计算实际值
 *
 * 计算公式：
 * - 当指数为0时：返回尾数
 * - 当指数非0时：返回 (尾数+8) * 2^(指数-1)
 *
 * 精度说明：
 * 由于编码过程中的精度损失，解码结果可能与原始值略有差异，
 * 但误差在可接受范围内。
 *
 * @pre x必须是有效的浮点字节表示
 * @post 返回对应的整数值
 *
 * @note 这是luaO_int2fb的精确逆操作
 * @see luaO_int2fb()
 */
int luaO_fb2int(int x) {
    int e = (x >> 3) & 31;  // 提取5位指数

    if (e == 0) {
        return x;  // 指数为0，直接返回尾数
    } else {
        // 计算 (尾数+8) * 2^(指数-1)
        return ((x & 7) + 8) << (e - 1);
    }
}


/**
 * @brief 计算整数的以2为底的对数（向下取整）
 * @param x 要计算对数的无符号整数
 * @return floor(log2(x))，如果x=0则结果未定义
 *
 * 详细说明：
 * 这个函数使用查表法高效计算整数的二进制对数。算法结合了
 * 位移操作和预计算表，在时间和空间之间取得了良好平衡。
 *
 * 算法策略：
 * 1. 对于大于255的数，每次右移8位，对数增加8
 * 2. 对于小于256的数，使用预计算的查找表
 * 3. 最终结果是两部分的和
 *
 * 查找表设计：
 * log_2[i] = floor(log2(i))，其中i从0到255
 * 表中的值范围是0到8，每个值重复出现2^k次
 *
 * 性能特征：
 * - 时间复杂度：O(log(x)/8) = O(log(x))
 * - 空间复杂度：O(1)（256字节的静态表）
 * - 缓存友好：查找表较小，容易缓存
 *
 * 应用场景：
 * - 哈希表大小计算
 * - 位操作优化
 * - 数值范围估算
 * - 算法复杂度分析
 *
 * 边界情况：
 * - x=0：结果未定义（通常不应传入0）
 * - x=1：返回0
 * - x=2^k：返回k
 *
 * 优化技巧：
 * 使用静态查找表避免重复计算，8位分块处理减少循环次数。
 *
 * @pre x > 0（x=0的行为未定义）
 * @post 返回floor(log2(x))
 *
 * @note 这是高效的整数对数实现
 * @see 位操作，查表算法
 */
int luaO_log2(unsigned int x) {
    // 预计算的对数查找表：log_2[i] = floor(log2(i))
    static const lu_byte log_2[256] = {
        0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
    };

    int l = -1;

    // 处理大于255的部分：每右移8位，对数增加8
    while (x >= 256) {
        l += 8;
        x >>= 8;
    }

    // 使用查找表处理剩余部分
    return l + log_2[x];
}


/**
 * @brief 原始对象相等性比较（不调用元方法）
 * @param t1 第一个对象指针
 * @param t2 第二个对象指针
 * @return 如果对象相等返回1，否则返回0
 *
 * 详细说明：
 * 这个函数实现了Lua对象的原始相等性比较，不会调用任何元方法。
 * 它是Lua相等性语义的基础实现。
 *
 * 比较规则：
 * 1. 类型不同：直接返回false
 * 2. nil：所有nil都相等
 * 3. 数值：使用数值相等性比较（处理NaN等特殊情况）
 * 4. 布尔值：直接比较布尔值
 * 5. 轻量用户数据：比较指针值
 * 6. 可回收对象：比较对象指针（引用相等性）
 *
 * 类型处理：
 * - 基本类型：按值比较
 * - 引用类型：按引用比较
 * - 特殊类型：按特定规则比较
 *
 * 数值比较：
 * 使用luai_numeq宏进行数值比较，正确处理浮点数的特殊情况，
 * 如NaN、正负零等。
 *
 * 布尔值比较：
 * 注释强调布尔true必须是1，这是Lua实现的重要约定，
 * 确保布尔值的内部表示一致性。
 *
 * 引用相等性：
 * 对于字符串、表、函数等可回收对象，比较的是对象的身份
 * （指针相等），而不是内容相等。
 *
 * 性能考虑：
 * - 类型检查：快速的类型标签比较
 * - 分支预测：常见类型放在前面
 * - 指针比较：高效的引用比较
 *
 * 与元方法的区别：
 * 这个函数不会触发__eq元方法，是最底层的相等性判断。
 *
 * @pre t1和t2必须是有效的TValue指针
 * @post 返回原始相等性比较结果
 *
 * @note 这是Lua相等性语义的基础
 * @see luaV_equalval(), 元方法系统
 */
int luaO_rawequalObj(const TValue *t1, const TValue *t2) {
    // 首先比较类型
    if (ttype(t1) != ttype(t2)) {
        return 0;
    }

    // 根据类型进行相应的比较
    switch (ttype(t1)) {
        case LUA_TNIL:
            return 1;  // 所有nil都相等

        case LUA_TNUMBER:
            return luai_numeq(nvalue(t1), nvalue(t2));  // 数值相等性

        case LUA_TBOOLEAN:
            return bvalue(t1) == bvalue(t2);  // 布尔值比较（true必须是1）

        case LUA_TLIGHTUSERDATA:
            return pvalue(t1) == pvalue(t2);  // 指针值比较

        default:
            // 可回收对象：比较对象指针（引用相等性）
            lua_assert(iscollectable(t1));
            return gcvalue(t1) == gcvalue(t2);
    }
}


/**
 * @brief 将字符串转换为数值
 * @param s 要转换的字符串
 * @param result 指向存储结果的数值指针
 * @return 转换成功返回1，失败返回0
 *
 * 详细说明：
 * 这个函数将字符串转换为Lua数值，支持十进制和十六进制格式。
 * 它是Lua数值解析的核心函数，用于处理源码中的数值常量。
 *
 * 支持的格式：
 * - 十进制数：123, 3.14, 1e10, -5.2e-3
 * - 十六进制数：0x1A, 0XFF, 0x1.8p3
 * - 带空白字符的数值：前后可以有空白字符
 *
 * 转换过程：
 * 1. 使用标准库函数进行初始转换
 * 2. 检查是否有十六进制标记（x或X）
 * 3. 如果是十六进制，重新解析
 * 4. 跳过尾部空白字符
 * 5. 验证字符串完全被消费
 *
 * 十六进制处理：
 * 当检测到'x'或'X'字符时，使用strtoul以16进制解析整数部分。
 * 这支持0x前缀的十六进制数值。
 *
 * 空白字符处理：
 * 函数会跳过数值后面的空白字符，但如果还有其他字符，
 * 则认为转换失败。
 *
 * 错误检测：
 * - 如果没有任何字符被转换，返回失败
 * - 如果有无效的尾部字符，返回失败
 * - 只有完全匹配的字符串才算成功
 *
 * 类型转换：
 * 使用cast_num宏确保结果类型与Lua的数值类型匹配。
 *
 * 性能考虑：
 * - 利用标准库的优化实现
 * - 最小化字符串扫描次数
 * - 高效的错误检测
 *
 * @pre s必须是有效的字符串指针，result必须是有效的数值指针
 * @post 如果成功，*result包含转换后的数值
 *
 * @note 这是Lua数值解析的标准实现
 * @see lua_str2number, strtoul
 */
int luaO_str2d(const char *s, lua_Number *result) {
    char *endptr;

    // 使用标准转换函数进行初始解析
    *result = lua_str2number(s, &endptr);

    // 检查是否有字符被转换
    if (endptr == s) {
        return 0;  // 没有字符被转换
    }

    // 检查十六进制标记
    if (*endptr == 'x' || *endptr == 'X') {
        // 重新以十六进制解析
        *result = cast_num(strtoul(s, &endptr, 16));
    }

    // 如果已经到达字符串末尾，转换成功
    if (*endptr == '\0') {
        return 1;
    }

    // 跳过尾部空白字符
    while (isspace(cast(unsigned char, *endptr))) {
        endptr++;
    }

    // 检查是否还有其他字符
    if (*endptr != '\0') {
        return 0;  // 有无效的尾部字符
    }

    return 1;  // 转换成功
}

/**
 * @brief 将C字符串推入Lua栈（内部辅助函数）
 * @param L Lua状态机指针
 * @param str 要推入的C字符串
 *
 * 详细说明：
 * 这是一个内部辅助函数，用于将C字符串转换为Lua字符串并
 * 推入栈顶。它简化了字符串格式化过程中的操作。
 *
 * 操作步骤：
 * 1. 使用luaS_new创建Lua字符串对象
 * 2. 将字符串对象设置到栈顶
 * 3. 增加栈顶指针
 *
 * 内存管理：
 * 创建的字符串对象会被垃圾回收器管理，无需手动释放。
 *
 * @pre L必须是有效的Lua状态机，str必须是有效的C字符串
 * @post 字符串被推入栈顶，栈大小增加1
 *
 * @note 这是格式化函数的内部辅助工具
 * @see luaS_new(), setsvalue2s()
 */
static void pushstr(lua_State *L, const char *str) {
    setsvalue2s(L, L->top, luaS_new(L, str));
    incr_top(L);
}



/**
 * @brief 格式化字符串并推入栈（类似vsprintf）
 * @param L Lua状态机指针
 * @param fmt 格式化字符串
 * @param argp 可变参数列表
 * @return 格式化后的字符串指针
 *
 * 详细说明：
 * 这个函数实现了类似printf的字符串格式化功能，但结果会被推入
 * Lua栈。它支持有限的格式说明符，专门为Lua的需求设计。
 *
 * 支持的格式说明符：
 * - %s：字符串（char *）
 * - %c：字符（int，转换为char）
 * - %d：整数（int）
 * - %f：浮点数（l_uacNumber）
 * - %p：指针（void *）
 * - %%：字面量%字符
 * - 其他：原样输出%和后续字符
 *
 * 算法流程：
 * 1. 初始化：推入空字符串作为起始
 * 2. 查找格式说明符：使用strchr查找%字符
 * 3. 处理普通文本：将%前的文本推入栈
 * 4. 处理格式说明符：根据类型进行相应转换
 * 5. 连接所有部分：使用luaV_concat连接所有字符串
 *
 * 特殊处理：
 * - NULL字符串：显示为"(null)"
 * - 字符转换：确保正确的类型转换
 * - 指针格式化：使用系统的%p格式
 * - 未知格式：原样输出，避免崩溃
 *
 * 栈管理：
 * 函数会在栈上创建多个字符串片段，最后连接成一个字符串，
 * 并清理中间结果，只保留最终字符串。
 *
 * 内存安全：
 * - 缓冲区大小：为指针格式化分配足够空间
 * - 字符串终止：确保所有字符串正确终止
 * - 栈平衡：保证栈的正确状态
 *
 * 性能考虑：
 * - 最小化内存分配：重用栈空间
 * - 高效连接：使用Lua的字符串连接机制
 * - 避免重复扫描：一次性处理格式字符串
 *
 * 错误处理：
 * 对于未知的格式说明符，函数不会崩溃，而是原样输出，
 * 这提供了良好的容错性。
 *
 * @pre L必须是有效的Lua状态机，fmt必须是有效的格式字符串
 * @post 格式化的字符串被推入栈顶
 *
 * @note 这是Lua错误报告和调试的重要工具
 * @see luaO_pushfstring(), luaV_concat()
 */
const char *luaO_pushvfstring(lua_State *L, const char *fmt, va_list argp) {
    int n = 1;
    pushstr(L, "");  // 推入空字符串作为起始

    for (;;) {
        const char *e = strchr(fmt, '%');
        if (e == NULL) break;  // 没有更多格式说明符

        // 推入%前的普通文本
        setsvalue2s(L, L->top, luaS_newlstr(L, fmt, e - fmt));
        incr_top(L);

        // 处理格式说明符
        switch (*(e + 1)) {
            case 's': {  // 字符串
                const char *s = va_arg(argp, char *);
                if (s == NULL) s = "(null)";  // 处理NULL指针
                pushstr(L, s);
                break;
            }
            case 'c': {  // 字符
                char buff[2];
                buff[0] = cast(char, va_arg(argp, int));
                buff[1] = '\0';
                pushstr(L, buff);
                break;
            }
            case 'd': {  // 整数
                setnvalue(L->top, cast_num(va_arg(argp, int)));
                incr_top(L);
                break;
            }
            case 'f': {  // 浮点数
                setnvalue(L->top, cast_num(va_arg(argp, l_uacNumber)));
                incr_top(L);
                break;
            }
            case 'p': {  // 指针
                char buff[4 * sizeof(void *) + 8];  // 足够的空间
                sprintf(buff, "%p", va_arg(argp, void *));
                pushstr(L, buff);
                break;
            }
            case '%': {  // 字面量%
                pushstr(L, "%");
                break;
            }
            default: {  // 未知格式说明符
                char buff[3];
                buff[0] = '%';
                buff[1] = *(e + 1);
                buff[2] = '\0';
                pushstr(L, buff);
                break;
            }
        }
        n += 2;
        fmt = e + 2;  // 跳过格式说明符
    }

    // 推入剩余的普通文本
    pushstr(L, fmt);

    // 连接所有字符串片段
    luaV_concat(L, n + 1, cast_int(L->top - L->base) - 1);
    L->top -= n;  // 清理中间结果

    return svalue(L->top - 1);  // 返回最终字符串
}


/**
 * @brief 格式化字符串并推入栈（可变参数版本）
 * @param L Lua状态机指针
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return 格式化后的字符串指针
 *
 * 详细说明：
 * 这是luaO_pushvfstring的便利包装函数，提供了更方便的
 * 可变参数接口，类似于sprintf。
 *
 * 实现方式：
 * 使用标准的va_list机制将可变参数转换为参数列表，
 * 然后调用luaO_pushvfstring完成实际工作。
 *
 * 使用场景：
 * - 错误消息格式化
 * - 调试信息生成
 * - 动态字符串构建
 *
 * @pre L必须是有效的Lua状态机，fmt必须是有效的格式字符串
 * @post 格式化的字符串被推入栈顶
 *
 * @note 这是最常用的字符串格式化接口
 * @see luaO_pushvfstring()
 */
const char *luaO_pushfstring(lua_State *L, const char *fmt, ...) {
    const char *msg;
    va_list argp;
    va_start(argp, fmt);
    msg = luaO_pushvfstring(L, fmt, argp);
    va_end(argp);
    return msg;
}

/**
 * @brief 生成代码块标识符（用于错误报告和调试）
 * @param out 输出缓冲区
 * @param source 源码标识字符串
 * @param bufflen 输出缓冲区长度
 *
 * 详细说明：
 * 这个函数根据源码标识生成用户友好的代码块标识符，
 * 用于错误报告和调试信息中显示源码位置。
 *
 * 源码标识格式：
 * 1. '='开头：直接显示内容（去掉=前缀）
 * 2. '@'开头：文件名（去掉@前缀）
 * 3. 其他：字符串内容（显示为[string "..."]）
 *
 * 处理规则：
 * - '='格式：用于自定义标识，直接显示内容
 * - '@'格式：用于文件，显示文件名，过长时截取尾部
 * - 字符串格式：用于动态代码，显示内容，遇到换行截断
 *
 * 长度处理：
 * - 文件名过长：显示"...filename"
 * - 字符串过长：显示"[string \"content...\"]"
 * - 确保输出不超过缓冲区大小
 *
 * 特殊字符处理：
 * - 换行符：作为字符串内容的结束标记
 * - 回车符：同样作为结束标记
 * - 确保输出的可读性
 *
 * 缓冲区安全：
 * - 预留空间：为格式化字符留出空间
 * - 强制终止：确保输出字符串正确终止
 * - 长度检查：防止缓冲区溢出
 *
 * 输出格式示例：
 * - "=main"：显示为"main"
 * - "@/path/to/file.lua"：显示为"/path/to/file.lua"或"...file.lua"
 * - "print('hello')"：显示为"[string \"print('hello')\"]"
 *
 * @pre out必须是有效的缓冲区，source必须是有效的字符串，bufflen > 0
 * @post out包含格式化的代码块标识符
 *
 * @note 这是调试信息生成的重要工具
 * @see 错误报告系统，调试接口
 */
void luaO_chunkid(char *out, const char *source, size_t bufflen) {
    if (*source == '=') {
        // 自定义标识：直接显示内容（去掉=前缀）
        strncpy(out, source + 1, bufflen);
        out[bufflen - 1] = '\0';  // 确保字符串终止
    } else {
        if (*source == '@') {
            // 文件名：去掉@前缀，处理长文件名
            size_t l;
            source++;  // 跳过@字符
            bufflen -= sizeof(" '...' ");  // 为省略号预留空间
            l = strlen(source);
            strcpy(out, "");

            if (l > bufflen) {
                // 文件名过长，显示尾部
                source += (l - bufflen);
                strcat(out, "...");
            }
            strcat(out, source);
        } else {
            // 字符串内容：显示为[string "..."]格式
            size_t len = strcspn(source, "\n\r");  // 找到第一个换行符
            bufflen -= sizeof(" [string \"...\"] ");  // 为格式字符预留空间

            if (len > bufflen) len = bufflen;  // 限制长度

            strcpy(out, "[string \"");
            if (source[len] != '\0') {
                // 内容被截断
                strncat(out, source, len);
                strcat(out, "...");
            } else {
                // 完整内容
                strcat(out, source);
            }
            strcat(out, "\"]");
        }
    }
}
