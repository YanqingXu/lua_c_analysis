/**
 * @file lvm.c
 * @brief Lua虚拟机核心执行引擎
 *
 * 详细说明：
 * 这是Lua虚拟机的核心执行引擎，实现了字节码解释器和运算符重载机制。
 * 该文件是Lua语言实现的心脏，负责执行编译后的字节码指令。
 *
 * 主要功能模块：
 *
 * 1. **类型转换系统**：
 *    - 数字与字符串的相互转换
 *    - 类型强制转换和兼容性检查
 *    - 支持Lua的动态类型特性
 *    - 高效的类型判断和转换算法
 *
 * 2. **运算符实现**：
 *    - 算术运算符（+、-、*、/、%、^）
 *    - 比较运算符（<、<=、==、~=、>、>=）
 *    - 逻辑运算符和位运算符
 *    - 字符串连接运算符（..）
 *
 * 3. **元方法调用机制**：
 *    - 运算符重载的实现
 *    - 元表查找和元方法调用
 *    - 支持用户自定义运算符行为
 *    - 面向对象编程的基础设施
 *
 * 4. **字节码解释器**：
 *    - 核心执行循环
 *    - 指令分发和处理
 *    - 栈管理和寄存器操作
 *    - 函数调用和返回处理
 *
 * 5. **调试支持**：
 *    - 执行跟踪和钩子调用
 *    - 行号跟踪和断点支持
 *    - 性能分析和诊断
 *
 * 设计原则：
 * - 高性能：优化的指令分发和执行
 * - 可扩展：支持元方法和运算符重载
 * - 安全性：完整的错误检查和异常处理
 * - 兼容性：严格遵循Lua语言规范
 *
 * 性能特征：
 * - 基于寄存器的虚拟机架构
 * - 高效的指令编码和解码
 * - 优化的栈操作和内存管理
 * - 支持尾调用优化
 *
 * 与其他模块的关系：
 * - lapi.c：提供C API接口
 * - ldo.c：函数调用和错误处理
 * - lgc.c：垃圾回收集成
 * - lparser.c：字节码生成
 * - ltable.c：表操作实现
 *
 * 使用场景：
 * - Lua脚本执行
 * - 嵌入式脚本引擎
 * - 配置文件处理
 * - 游戏脚本系统
 * - 扩展应用程序
 *
 * @author Roberto Ierusalimschy
 * @version 5.1.5
 * @date 2011
 * @copyright Copyright (C) 1994-2012 Lua.org, PUC-Rio
 *
 * @see lua.h 基础API定义
 * @see ldo.c 函数调用和错误处理
 * @see lgc.c 垃圾回收器
 * @see lopcodes.h 字节码指令定义
 * @see ltm.c 元方法管理
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lvm_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"

/**
 * @brief 表元方法链的最大长度限制
 *
 * 详细说明：
 * 限制表元方法调用链的最大深度，防止无限递归循环。
 * 当元方法调用链超过此限制时，会抛出错误。
 *
 * 设计考虑：
 * - 防止元方法无限递归
 * - 保护栈空间不被耗尽
 * - 提供合理的递归深度
 * - 便于调试和错误诊断
 *
 * @since Lua 5.1
 */
#define MAXTAGLOOP 100

/**
 * @brief 字符串转换宏
 *
 * 详细说明：
 * 将栈中的值转换为字符串的便利宏。如果值已经是字符串则直接返回true，
 * 如果是数字则调用luaV_tostring进行转换。
 *
 * @param L Lua状态机指针
 * @param o 要转换的栈位置
 * @return 转换成功返回非零值，失败返回0
 *
 * @since Lua 5.1
 * @see luaV_tostring
 */
#define tostring(L,o) ((ttype(o) == LUA_TSTRING) || (luaV_tostring(L, o)))


/**
 * @name 类型转换系统
 * @brief Lua动态类型系统的核心转换函数
 * @{
 */

/**
 * @brief 将值转换为数字
 *
 * 详细说明：
 * 尝试将Lua值转换为数字类型。这是Lua动态类型系统的核心函数之一，
 * 支持数字和字符串到数字的转换。
 *
 * 转换规则：
 * 1. 如果已经是数字类型，直接返回原对象
 * 2. 如果是字符串类型，尝试解析为数字
 * 3. 其他类型无法转换，返回NULL
 *
 * 字符串解析：
 * - 支持整数格式：123, -456
 * - 支持浮点格式：3.14, -2.5
 * - 支持科学记数法：1e10, 2.5e-3
 * - 支持十六进制：0xff, 0X1A
 * - 忽略前后空白字符
 *
 * 性能优化：
 * - 数字类型直接返回，无额外开销
 * - 字符串解析使用优化的luaO_str2d函数
 * - 避免不必要的内存分配
 *
 * 错误处理：
 * - 转换失败时返回NULL
 * - 不修改原始对象
 * - 不抛出异常，由调用者处理
 *
 * 使用场景：
 * - 算术运算的操作数转换
 * - 字符串到数字的强制转换
 * - 类型兼容性检查
 * - 实现tonumber()函数
 *
 * @param obj 要转换的Lua值
 * @param n 用于存储转换结果的TValue（仅在字符串转换时使用）
 * @return 成功时返回指向数字值的指针，失败时返回NULL
 *
 * @note 如果obj已经是数字，返回obj本身
 * @note 如果obj是字符串，转换结果存储在n中并返回n
 * @note 调用者需要检查返回值是否为NULL
 *
 * @since Lua 5.1
 * @see luaO_str2d, lua_tonumber, 算术运算
 */
const TValue *luaV_tonumber(const TValue *obj, TValue *n)
{
    lua_Number num;
    if (ttisnumber(obj)) return obj;
    if (ttisstring(obj) && luaO_str2d(svalue(obj), &num)) {
        setnvalue(n, num);
        return n;
    }
    else
        return NULL;
}

/**
 * @brief 将数字转换为字符串
 *
 * 详细说明：
 * 将栈中的数字值就地转换为字符串。这个函数直接修改栈中的对象，
 * 将数字类型替换为对应的字符串表示。
 *
 * 转换过程：
 * 1. 检查对象是否为数字类型
 * 2. 如果不是数字，返回失败
 * 3. 如果是数字，格式化为字符串
 * 4. 创建Lua字符串对象
 * 5. 替换栈中的原对象
 *
 * 字符串格式：
 * - 整数：不带小数点（如：123）
 * - 浮点数：带小数点（如：3.14）
 * - 科学记数法：大数或小数（如：1e+10）
 * - 特殊值：inf, -inf, nan
 *
 * 内存管理：
 * - 创建新的字符串对象
 * - 字符串会被内化（去重）
 * - 原数字对象被替换
 * - 可能触发垃圾回收
 *
 * 性能考虑：
 * - 就地转换，避免额外栈操作
 * - 使用优化的数字格式化函数
 * - 字符串内化减少内存使用
 *
 * 使用场景：
 * - 字符串连接操作
 * - 实现tostring()函数
 * - 输出和显示数字
 * - 类型强制转换
 *
 * 安全性：
 * - 只转换数字类型
 * - 格式化结果总是有效的
 * - 不会产生缓冲区溢出
 *
 * @param L Lua状态机指针
 * @param obj 指向栈中要转换的对象
 * @return 成功返回1，失败返回0
 *
 * @note 成功时obj指向的对象被修改为字符串
 * @note 只有数字类型才能转换
 * @note 可能触发垃圾回收
 *
 * @since Lua 5.1
 * @see lua_number2str, luaS_new, 字符串连接
 */
int luaV_tostring(lua_State *L, StkId obj)
{
    if (!ttisnumber(obj))
        return 0;
    else {
        char s[LUAI_MAXNUMBER2STR];
        lua_Number n = nvalue(obj);
        lua_number2str(s, n);
        setsvalue2s(L, obj, luaS_new(L, s));
        return 1;
    }
}


/** @} */

/**
 * @name 调试和跟踪系统
 * @brief 虚拟机执行跟踪和调试支持
 * @{
 */

/**
 * @brief 执行跟踪和钩子调用
 *
 * 详细说明：
 * 在虚拟机执行过程中进行跟踪，根据设置的钩子掩码调用相应的钩子函数。
 * 这是Lua调试器和性能分析器的基础设施。
 *
 * 钩子类型：
 * 1. **计数钩子（LUA_MASKCOUNT）**：
 *    - 每执行指定数量的指令后触发
 *    - 用于性能分析和超时控制
 *    - 可以中断长时间运行的脚本
 *
 * 2. **行钩子（LUA_MASKLINE）**：
 *    - 每执行新的源代码行时触发
 *    - 用于单步调试和断点
 *    - 支持源码级调试
 *
 * 触发条件：
 * - 进入新函数（npc == 0）
 * - 向后跳转，如循环（pc <= oldpc）
 * - 执行新的源代码行
 *
 * 性能考虑：
 * - 只在设置了钩子时才进行检查
 * - 使用位掩码进行快速判断
 * - 最小化对正常执行的影响
 *
 * 调试信息：
 * - 维护当前和之前的程序计数器
 * - 计算相对程序计数器位置
 * - 获取源代码行号信息
 *
 * 使用场景：
 * - 调试器实现
 * - 性能分析工具
 * - 脚本执行监控
 * - 超时和中断控制
 *
 * @param L Lua状态机指针
 * @param pc 当前指令指针
 *
 * @note 这是一个内部函数，由虚拟机执行循环调用
 * @note 钩子函数可能会修改虚拟机状态
 * @warning 钩子函数中的错误会传播到主执行流程
 *
 * @since Lua 5.1
 * @see luaD_callhook, 调试API, 钩子机制
 */
static void traceexec(lua_State *L, const Instruction *pc)
{
    lu_byte mask = L->hookmask;
    const Instruction *oldpc = L->savedpc;
    L->savedpc = pc;
    if ((mask & LUA_MASKCOUNT) && L->hookcount == 0) {
        resethookcount(L);
        luaD_callhook(L, LUA_HOOKCOUNT, -1);
    }
    if (mask & LUA_MASKLINE) {
        Proto *p = ci_func(L->ci)->l.p;
        int npc = pcRel(pc, p);
        int newline = getline(p, npc);
        if (npc == 0 || pc <= oldpc || newline != getline(p, pcRel(oldpc, p)))
            luaD_callhook(L, LUA_HOOKLINE, newline);
    }
}


/** @} */

/**
 * @name 元方法调用系统
 * @brief 运算符重载和元方法调用的实现
 * @{
 */

/**
 * @brief 调用元方法并获取结果
 *
 * 详细说明：
 * 调用二元运算符的元方法，并将结果存储到指定位置。这是实现
 * 运算符重载的核心函数，支持用户自定义运算符行为。
 *
 * 调用过程：
 * 1. 保存结果位置（防止栈重分配）
 * 2. 将元方法函数推入栈
 * 3. 将两个操作数作为参数推入栈
 * 4. 调用元方法函数
 * 5. 将返回值存储到结果位置
 *
 * 栈管理：
 * - 自动检查栈空间
 * - 正确处理栈重分配
 * - 恢复结果位置指针
 * - 清理临时栈内容
 *
 * 元方法类型：
 * - 算术运算：__add, __sub, __mul, __div, __mod, __pow
 * - 比较运算：__eq, __lt, __le
 * - 其他运算：__concat, __unm
 *
 * 错误处理：
 * - 元方法调用可能抛出错误
 * - 错误会传播到调用者
 * - 栈状态会被正确恢复
 *
 * 性能考虑：
 * - 使用savestack/restorestack处理栈重分配
 * - 最小化栈操作开销
 * - 支持尾调用优化
 *
 * @param L Lua状态机指针
 * @param res 结果存储位置
 * @param f 元方法函数
 * @param p1 第一个操作数
 * @param p2 第二个操作数
 *
 * @note 结果会覆盖res位置的原值
 * @note 元方法必须返回一个值
 * @warning 元方法调用可能导致栈重分配
 *
 * @since Lua 5.1
 * @see callTM, 运算符重载, 元表机制
 */
static void callTMres(lua_State *L, StkId res, const TValue *f,
                      const TValue *p1, const TValue *p2)
{
    ptrdiff_t result = savestack(L, res);
    setobj2s(L, L->top, f);
    setobj2s(L, L->top+1, p1);
    setobj2s(L, L->top+2, p2);
    luaD_checkstack(L, 3);
    L->top += 3;
    luaD_call(L, L->top - 3, 1);
    res = restorestack(L, result);
    L->top--;
    setobjs2s(L, res, L->top);
}

/**
 * @brief 调用元方法（无返回值）
 *
 * 详细说明：
 * 调用三元运算符的元方法，不期望返回值。主要用于赋值类操作
 * 和其他不需要返回值的元方法调用。
 *
 * 调用过程：
 * 1. 将元方法函数推入栈
 * 2. 将三个操作数作为参数推入栈
 * 3. 调用元方法函数
 * 4. 丢弃所有返回值
 *
 * 使用场景：
 * - __newindex元方法调用
 * - __settable操作
 * - 其他副作用操作
 *
 * 与callTMres的区别：
 * - 不保存和恢复结果位置
 * - 支持三个参数
 * - 不处理返回值
 * - 更简单的栈管理
 *
 * 栈操作：
 * - 检查栈空间充足
 * - 推入函数和参数
 * - 调用后自动清理栈
 *
 * @param L Lua状态机指针
 * @param f 元方法函数
 * @param p1 第一个操作数
 * @param p2 第二个操作数
 * @param p3 第三个操作数
 *
 * @note 不处理返回值
 * @note 主要用于副作用操作
 * @note 元方法调用可能抛出错误
 *
 * @since Lua 5.1
 * @see callTMres, __newindex, 表赋值操作
 */
static void callTM(lua_State *L, const TValue *f, const TValue *p1,
                   const TValue *p2, const TValue *p3)
{
    setobj2s(L, L->top, f);
    setobj2s(L, L->top+1, p1);
    setobj2s(L, L->top+2, p2);
    setobj2s(L, L->top+3, p3);
    luaD_checkstack(L, 4);
    L->top += 4;
    luaD_call(L, L->top - 4, 0);
}


/** @} */

/**
 * @name 表操作和元方法系统
 * @brief 表访问、赋值和元方法调用的核心实现
 * @{
 */

/**
 * @brief 从表中获取值（支持元方法）
 *
 * 详细说明：
 * 实现Lua的表索引操作，支持__index元方法。这是table[key]操作的底层实现，
 * 提供了完整的元方法调用链和循环检测机制。
 *
 * 查找过程：
 * 1. 如果t是表，直接在表中查找key
 * 2. 如果找到非nil值，直接返回
 * 3. 如果没找到且有__index元方法，调用元方法
 * 4. 如果__index是表，递归查找
 * 5. 如果__index是函数，调用函数获取结果
 *
 * 元方法链处理：
 * - 支持多级__index元方法链
 * - 使用MAXTAGLOOP防止无限循环
 * - 正确处理元方法返回的表对象
 * - 保持查找语义的一致性
 *
 * 类型处理：
 * - 表类型：直接查找，支持元表
 * - 非表类型：必须有__index元方法
 * - 元方法可以是函数或表
 * - 错误处理：类型错误和循环检测
 *
 * 性能优化：
 * - 使用fasttm快速元方法查找
 * - 原始表查找使用luaH_get
 * - 最小化元方法调用开销
 * - 缓存友好的访问模式
 *
 * 错误处理：
 * - 无效类型抛出类型错误
 * - 循环检测防止无限递归
 * - 元方法调用可能抛出错误
 * - 保持错误信息的准确性
 *
 * 使用场景：
 * - 实现table[key]语法
 * - 支持面向对象编程
 * - 实现代理和包装对象
 * - 提供动态属性访问
 *
 * @param L Lua状态机指针
 * @param t 要索引的对象
 * @param key 索引键
 * @param val 结果存储位置
 *
 * @note 结果存储在val指向的位置
 * @note 支持完整的__index元方法链
 * @warning 元方法调用可能修改虚拟机状态
 *
 * @since Lua 5.1
 * @see luaV_settable, luaH_get, __index元方法
 */
void luaV_gettable(lua_State *L, const TValue *t, TValue *key, StkId val)
{
    int loop;
    for (loop = 0; loop < MAXTAGLOOP; loop++) {
        const TValue *tm;
        if (ttistable(t)) {
            Table *h = hvalue(t);
            const TValue *res = luaH_get(h, key);
            if (!ttisnil(res) ||
                (tm = fasttm(L, h->metatable, TM_INDEX)) == NULL) {
                setobj2s(L, val, res);
                return;
            }
        }
        else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_INDEX)))
            luaG_typeerror(L, t, "index");
        if (ttisfunction(tm)) {
            callTMres(L, val, tm, t, key);
            return;
        }
        t = tm;
    }
    luaG_runerror(L, "loop in gettable");
}


/**
 * @brief 向表中设置值（支持元方法）
 *
 * 详细说明：
 * 实现Lua的表赋值操作，支持__newindex元方法。这是table[key]=value操作的底层实现，
 * 提供了完整的元方法调用链和垃圾回收集成。
 *
 * 赋值过程：
 * 1. 如果t是表，尝试直接在表中设置key=val
 * 2. 如果key已存在或没有__newindex元方法，直接赋值
 * 3. 如果key不存在且有__newindex元方法，调用元方法
 * 4. 如果__newindex是表，递归设置
 * 5. 如果__newindex是函数，调用函数处理赋值
 *
 * 元方法链处理：
 * - 支持多级__newindex元方法链
 * - 使用MAXTAGLOOP防止无限循环
 * - 正确处理元方法返回的表对象
 * - 保持赋值语义的一致性
 *
 * 内存管理：
 * - 使用luaC_barriert设置垃圾回收屏障
 * - 正确处理表的rehash操作
 * - 避免指向表内部的悬挂指针
 * - 清除表的缓存标志
 *
 * 类型处理：
 * - 表类型：直接设置，支持元表
 * - 非表类型：必须有__newindex元方法
 * - 元方法可以是函数或表
 * - 错误处理：类型错误和循环检测
 *
 * 性能优化：
 * - 使用fasttm快速元方法查找
 * - 原始表设置使用luaH_set
 * - 最小化元方法调用开销
 * - 优化的垃圾回收集成
 *
 * 安全考虑：
 * - 防止表rehash导致的指针失效
 * - 使用临时变量保存元方法引用
 * - 正确的垃圾回收屏障设置
 * - 循环检测和错误处理
 *
 * 使用场景：
 * - 实现table[key]=value语法
 * - 支持面向对象编程
 * - 实现代理和包装对象
 * - 提供动态属性设置
 *
 * @param L Lua状态机指针
 * @param t 要设置的对象
 * @param key 索引键
 * @param val 要设置的值
 *
 * @note 可能触发垃圾回收
 * @note 支持完整的__newindex元方法链
 * @warning 元方法调用可能修改虚拟机状态
 *
 * @since Lua 5.1
 * @see luaV_gettable, luaH_set, __newindex元方法
 */
void luaV_settable(lua_State *L, const TValue *t, TValue *key, StkId val)
{
    int loop;
    TValue temp;
    for (loop = 0; loop < MAXTAGLOOP; loop++) {
        const TValue *tm;
        if (ttistable(t)) {
            Table *h = hvalue(t);
            TValue *oldval = luaH_set(L, h, key);
            if (!ttisnil(oldval) ||
                (tm = fasttm(L, h->metatable, TM_NEWINDEX)) == NULL) {
                setobj2t(L, oldval, val);
                h->flags = 0;
                luaC_barriert(L, h, val);
                return;
            }
        }
        else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_NEWINDEX)))
            luaG_typeerror(L, t, "index");
        if (ttisfunction(tm)) {
            callTM(L, tm, t, key, val);
            return;
        }
        setobj(L, &temp, tm);
        t = &temp;
    }
    luaG_runerror(L, "loop in settable");
}


/**
 * @brief 调用二元运算符元方法
 *
 * 详细说明：
 * 为二元运算符查找并调用相应的元方法。这是实现运算符重载的核心函数，
 * 支持算术运算、比较运算和其他二元运算的元方法调用。
 *
 * 查找顺序：
 * 1. 首先尝试第一个操作数的元方法
 * 2. 如果第一个操作数没有元方法，尝试第二个操作数
 * 3. 如果都没有元方法，返回失败
 * 4. 找到元方法后调用并返回结果
 *
 * 支持的运算符：
 * - 算术运算：__add, __sub, __mul, __div, __mod, __pow
 * - 比较运算：__eq, __lt, __le
 * - 其他运算：__concat, __unm
 *
 * 元方法优先级：
 * - 左操作数的元方法优先
 * - 右操作数的元方法次之
 * - 保持运算符的左结合性
 *
 * 错误处理：
 * - 没有元方法时返回0
 * - 元方法调用可能抛出错误
 * - 保持错误信息的准确性
 *
 * @param L Lua状态机指针
 * @param p1 第一个操作数
 * @param p2 第二个操作数
 * @param res 结果存储位置
 * @param event 元方法事件类型
 * @return 成功返回1，失败返回0
 *
 * @note 左操作数的元方法优先
 * @note 结果存储在res位置
 *
 * @since Lua 5.1
 * @see callTMres, 运算符重载
 */
static int call_binTM(lua_State *L, const TValue *p1, const TValue *p2,
                      StkId res, TMS event)
{
    const TValue *tm = luaT_gettmbyobj(L, p1, event);
    if (ttisnil(tm))
        tm = luaT_gettmbyobj(L, p2, event);
    if (ttisnil(tm)) return 0;
    callTMres(L, res, tm, p1, p2);
    return 1;
}

/**
 * @brief 获取比较运算的元方法
 *
 * 详细说明：
 * 为比较运算获取合适的元方法。比较运算要求两个操作数使用相同的元方法，
 * 这确保了比较操作的对称性和一致性。
 *
 * 检查规则：
 * 1. 获取第一个元表的元方法
 * 2. 如果没有元方法，返回NULL
 * 3. 如果两个元表相同，返回元方法
 * 4. 获取第二个元表的元方法
 * 5. 比较两个元方法是否相同
 * 6. 相同则返回，不同则返回NULL
 *
 * 对称性保证：
 * - 两个操作数必须有相同的元方法
 * - 防止不对称的比较行为
 * - 确保比较结果的一致性
 * - 支持自定义比较语义
 *
 * 性能优化：
 * - 使用fasttm快速查找
 * - 相同元表时直接返回
 * - 最小化元方法比较开销
 *
 * @param L Lua状态机指针
 * @param mt1 第一个操作数的元表
 * @param mt2 第二个操作数的元表
 * @param event 元方法事件类型
 * @return 找到相同元方法时返回元方法，否则返回NULL
 *
 * @note 要求两个操作数有相同的元方法
 * @note 用于比较运算的对称性检查
 *
 * @since Lua 5.1
 * @see call_orderTM, 比较运算符
 */
static const TValue *get_compTM(lua_State *L, Table *mt1, Table *mt2,
                                TMS event)
{
    const TValue *tm1 = fasttm(L, mt1, event);
    const TValue *tm2;
    if (tm1 == NULL) return NULL;
    if (mt1 == mt2) return tm1;
    tm2 = fasttm(L, mt2, event);
    if (tm2 == NULL) return NULL;
    if (luaO_rawequalObj(tm1, tm2))
        return tm1;
    return NULL;
}

/**
 * @brief 调用顺序比较元方法
 *
 * 详细说明：
 * 为顺序比较运算（<、<=）调用元方法。这个函数确保两个操作数使用相同的
 * 元方法，并调用元方法进行比较。
 *
 * 调用过程：
 * 1. 获取第一个操作数的元方法
 * 2. 如果没有元方法，返回-1（失败）
 * 3. 获取第二个操作数的元方法
 * 4. 检查两个元方法是否相同
 * 5. 如果不同，返回-1（失败）
 * 6. 调用元方法并返回布尔结果
 *
 * 对称性要求：
 * - 两个操作数必须有相同的元方法
 * - 防止不一致的比较行为
 * - 确保比较关系的传递性
 *
 * 返回值处理：
 * - -1：没有元方法或元方法不同
 * - 0：元方法返回false或nil
 * - 1：元方法返回true
 *
 * @param L Lua状态机指针
 * @param p1 第一个操作数
 * @param p2 第二个操作数
 * @param event 元方法事件类型
 * @return -1表示失败，0/1表示比较结果
 *
 * @note 要求两个操作数有相同的元方法
 * @note 用于__lt和__le元方法调用
 *
 * @since Lua 5.1
 * @see luaV_lessthan, 比较运算符
 */
static int call_orderTM(lua_State *L, const TValue *p1, const TValue *p2,
                        TMS event)
{
    const TValue *tm1 = luaT_gettmbyobj(L, p1, event);
    const TValue *tm2;
    if (ttisnil(tm1)) return -1;
    tm2 = luaT_gettmbyobj(L, p2, event);
    if (!luaO_rawequalObj(tm1, tm2))
        return -1;
    callTMres(L, L->top, tm1, p1, p2);
    return !l_isfalse(L->top);
}


/** @} */

/**
 * @name 字符串比较和运算符实现
 * @brief 字符串比较算法和运算符重载的具体实现
 * @{
 */

/**
 * @brief 字符串比较函数
 *
 * 详细说明：
 * 实现Lua字符串的比较算法，支持包含null字符的字符串比较。
 * 使用locale感知的strcoll函数进行比较，支持国际化。
 *
 * 比较算法：
 * 1. 使用strcoll进行locale感知的比较
 * 2. 如果结果不为0，直接返回比较结果
 * 3. 如果相等到null字符，处理null字符后的部分
 * 4. 递归比较null字符后的字符串片段
 * 5. 根据字符串长度确定最终结果
 *
 * null字符处理：
 * - Lua字符串可以包含null字符
 * - 使用strlen找到第一个null字符位置
 * - 比较null字符后的剩余部分
 * - 正确处理不同长度的字符串
 *
 * 国际化支持：
 * - 使用strcoll而不是strcmp
 * - 支持locale特定的排序规则
 * - 处理多字节字符和Unicode
 * - 符合用户的语言环境设置
 *
 * 性能特征：
 * - 大多数情况下单次strcoll调用
 * - 只有包含null字符时才需要循环
 * - 避免不必要的字符串复制
 * - 高效的指针算术操作
 *
 * 边界情况：
 * - 空字符串的正确处理
 * - 只包含null字符的字符串
 * - 长度不同但前缀相同的字符串
 * - 完全相同的字符串
 *
 * 返回值语义：
 * - 负数：左字符串小于右字符串
 * - 零：两个字符串相等
 * - 正数：左字符串大于右字符串
 * - 与标准C库strcmp一致
 *
 * 使用场景：
 * - 实现<、<=、>、>=运算符
 * - 表的键排序
 * - 字符串数组排序
 * - 模式匹配和搜索
 *
 * @param ls 左字符串对象
 * @param rs 右字符串对象
 * @return 比较结果（负数、零、正数）
 *
 * @note 支持包含null字符的字符串
 * @note 使用locale感知的比较
 * @note 性能针对普通字符串优化
 *
 * @since Lua 5.1
 * @see strcoll, luaV_lessthan, 字符串比较
 */
static int l_strcmp(const TString *ls, const TString *rs)
{
    const char *l = getstr(ls);
    size_t ll = ls->tsv.len;
    const char *r = getstr(rs);
    size_t lr = rs->tsv.len;
    for (;;) {
        int temp = strcoll(l, r);
        if (temp != 0) return temp;
        else {
            size_t len = strlen(l);
            if (len == lr)
                return (len == ll) ? 0 : 1;
            else if (len == ll)
                return -1;
            len++;
            l += len; ll -= len; r += len; lr -= len;
        }
    }
}


/**
 * @brief 小于比较运算符实现
 *
 * 详细说明：
 * 实现Lua的小于（<）运算符，支持数字、字符串和用户定义类型的比较。
 * 这是Lua比较运算的核心函数之一。
 *
 * 比较规则：
 * 1. 两个操作数必须是相同类型
 * 2. 数字：使用数值比较
 * 3. 字符串：使用字典序比较
 * 4. 其他类型：尝试__lt元方法
 * 5. 没有元方法时报告类型错误
 *
 * 类型检查：
 * - 严格的类型匹配要求
 * - 不同类型间不能比较
 * - 防止意外的类型转换
 * - 保持比较语义的清晰性
 *
 * 数字比较：
 * - 使用luai_numlt宏进行比较
 * - 支持整数和浮点数
 * - 处理特殊值（NaN、无穷大）
 * - 高效的数值比较算法
 *
 * 字符串比较：
 * - 使用l_strcmp进行字典序比较
 * - 支持locale感知的排序
 * - 处理包含null字符的字符串
 * - 国际化字符串比较
 *
 * 元方法支持：
 * - 查找并调用__lt元方法
 * - 支持用户自定义比较逻辑
 * - 实现自定义类型的排序
 * - 面向对象编程支持
 *
 * 错误处理：
 * - 类型不匹配时抛出错误
 * - 没有比较方法时报告错误
 * - 提供清晰的错误信息
 * - 保持错误的一致性
 *
 * 性能优化：
 * - 内置类型的快速路径
 * - 最小化元方法查找开销
 * - 高效的类型检查
 * - 缓存友好的实现
 *
 * @param L Lua状态机指针
 * @param l 左操作数
 * @param r 右操作数
 * @return 如果l < r返回非零值，否则返回0
 *
 * @note 要求两个操作数类型相同
 * @note 支持__lt元方法
 * @warning 类型不匹配会抛出错误
 *
 * @since Lua 5.1
 * @see lessequal, luaV_equalval, __lt元方法
 */
int luaV_lessthan(lua_State *L, const TValue *l, const TValue *r)
{
    int res;
    if (ttype(l) != ttype(r))
        return luaG_ordererror(L, l, r);
    else if (ttisnumber(l))
        return luai_numlt(nvalue(l), nvalue(r));
    else if (ttisstring(l))
        return l_strcmp(rawtsvalue(l), rawtsvalue(r)) < 0;
    else if ((res = call_orderTM(L, l, r, TM_LT)) != -1)
        return res;
    return luaG_ordererror(L, l, r);
}

/**
 * @brief 小于等于比较运算符实现
 *
 * 详细说明：
 * 实现Lua的小于等于（<=）运算符，支持数字、字符串和用户定义类型的比较。
 * 使用__le元方法或__lt元方法的组合来实现。
 *
 * 比较策略：
 * 1. 两个操作数必须是相同类型
 * 2. 数字：使用数值比较
 * 3. 字符串：使用字典序比较
 * 4. 其他类型：首先尝试__le元方法
 * 5. 如果没有__le，尝试!(__lt(r,l))
 * 6. 都没有时报告类型错误
 *
 * 元方法回退：
 * - 优先使用__le元方法
 * - 回退到__lt元方法的逆向调用
 * - 实现a <= b等价于!(b < a)
 * - 保持比较关系的一致性
 *
 * 数学语义：
 * - a <= b 等价于 a < b 或 a == b
 * - 使用De Morgan定律：a <= b 等价于 !(b < a)
 * - 保持偏序关系的性质
 * - 支持自定义比较语义
 *
 * 性能考虑：
 * - 内置类型的直接比较
 * - 元方法查找的优化
 * - 避免不必要的元方法调用
 * - 高效的回退机制
 *
 * 类型安全：
 * - 严格的类型检查
 * - 防止隐式类型转换
 * - 清晰的错误报告
 * - 一致的比较语义
 *
 * @param L Lua状态机指针
 * @param l 左操作数
 * @param r 右操作数
 * @return 如果l <= r返回非零值，否则返回0
 *
 * @note 优先使用__le，回退到__lt
 * @note 要求两个操作数类型相同
 * @warning 类型不匹配会抛出错误
 *
 * @since Lua 5.1
 * @see luaV_lessthan, __le元方法, __lt元方法
 */
static int lessequal(lua_State *L, const TValue *l, const TValue *r)
{
    int res;
    if (ttype(l) != ttype(r))
        return luaG_ordererror(L, l, r);
    else if (ttisnumber(l))
        return luai_numle(nvalue(l), nvalue(r));
    else if (ttisstring(l))
        return l_strcmp(rawtsvalue(l), rawtsvalue(r)) <= 0;
    else if ((res = call_orderTM(L, l, r, TM_LE)) != -1)
        return res;
    else if ((res = call_orderTM(L, r, l, TM_LT)) != -1)
        return !res;
    return luaG_ordererror(L, l, r);
}


/**
 * @brief 相等性比较运算符实现
 *
 * 详细说明：
 * 实现Lua的相等（==）运算符，支持所有Lua类型的相等性比较。
 * 这是Lua比较运算的核心函数，处理值相等和引用相等的区别。
 *
 * 比较规则（按类型）：
 *
 * 1. **nil类型**：
 *    - 所有nil值都相等
 *    - 直接返回true
 *
 * 2. **数字类型**：
 *    - 使用数值相等比较
 *    - 处理浮点数精度问题
 *    - 支持NaN的特殊语义
 *
 * 3. **布尔类型**：
 *    - 直接比较布尔值
 *    - true必须是1，false必须是0
 *
 * 4. **轻量用户数据**：
 *    - 比较指针值
 *    - 相同指针认为相等
 *
 * 5. **完整用户数据**：
 *    - 首先比较对象指针
 *    - 相同对象直接返回true
 *    - 不同对象尝试__eq元方法
 *
 * 6. **表类型**：
 *    - 首先比较表指针
 *    - 相同表直接返回true
 *    - 不同表尝试__eq元方法
 *
 * 7. **其他GC对象**：
 *    - 字符串、函数、线程等
 *    - 比较GC对象指针
 *    - 相同对象认为相等
 *
 * 元方法处理：
 * - 只有用户数据和表支持__eq元方法
 * - 要求两个对象有相同的__eq元方法
 * - 元方法返回false或nil表示不相等
 * - 其他返回值表示相等
 *
 * 性能优化：
 * - 指针比较的快速路径
 * - 内置类型的直接比较
 * - 最小化元方法调用开销
 * - 高效的类型分发
 *
 * 语义保证：
 * - 自反性：a == a总是true（除了NaN）
 * - 对称性：a == b等价于b == a
 * - 传递性：如果a == b且b == c，则a == c
 * - 一致性：重复比较结果相同
 *
 * 错误处理：
 * - 假设两个参数类型相同
 * - 元方法调用可能抛出错误
 * - 保持比较语义的一致性
 *
 * @param L Lua状态机指针
 * @param t1 第一个操作数
 * @param t2 第二个操作数
 * @return 如果相等返回非零值，否则返回0
 *
 * @note 假设两个参数类型相同
 * @note 支持__eq元方法
 * @note 区分值相等和引用相等
 *
 * @since Lua 5.1
 * @see luaV_lessthan, get_compTM, __eq元方法
 */
int luaV_equalval(lua_State *L, const TValue *t1, const TValue *t2)
{
    const TValue *tm;
    lua_assert(ttype(t1) == ttype(t2));
    switch (ttype(t1)) {
        case LUA_TNIL: return 1;
        case LUA_TNUMBER: return luai_numeq(nvalue(t1), nvalue(t2));
        case LUA_TBOOLEAN: return bvalue(t1) == bvalue(t2);
        case LUA_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
        case LUA_TUSERDATA: {
            if (uvalue(t1) == uvalue(t2)) return 1;
            tm = get_compTM(L, uvalue(t1)->metatable, uvalue(t2)->metatable,
                           TM_EQ);
            break;
        }
        case LUA_TTABLE: {
            if (hvalue(t1) == hvalue(t2)) return 1;
            tm = get_compTM(L, hvalue(t1)->metatable, hvalue(t2)->metatable, TM_EQ);
            break;
        }
        default: return gcvalue(t1) == gcvalue(t2);
    }
    if (tm == NULL) return 0;
    callTMres(L, L->top, tm, t1, t2);
    return !l_isfalse(L->top);
}


/**
 * @brief 字符串连接运算符实现
 *
 * 详细说明：
 * 实现Lua的字符串连接（..）运算符，支持多个值的高效连接。
 * 这个函数优化了连续连接操作，减少了内存分配和复制开销。
 *
 * 连接算法：
 * 1. 从右到左处理栈中的值
 * 2. 尽可能多地收集可连接的值
 * 3. 一次性分配足够的缓冲区
 * 4. 按顺序复制所有字符串
 * 5. 创建最终的连接结果
 *
 * 类型处理：
 * - 字符串：直接连接
 * - 数字：自动转换为字符串
 * - 其他类型：尝试__concat元方法
 * - 转换失败：抛出类型错误
 *
 * 性能优化：
 * - 批量处理多个连接操作
 * - 预计算总长度，一次分配
 * - 使用全局缓冲区避免重复分配
 * - 最小化字符串对象创建
 *
 * 特殊情况处理：
 * - 空字符串优化：直接返回非空操作数
 * - 单个字符串：确保转换为字符串类型
 * - 长度溢出：检查并报告错误
 * - 元方法回退：支持自定义连接行为
 *
 * 内存管理：
 * - 使用luaZ_openspace管理缓冲区
 * - 自动扩展缓冲区大小
 * - 字符串内化和去重
 * - 正确的垃圾回收集成
 *
 * 错误处理：
 * - 长度溢出检查
 * - 类型转换失败处理
 * - 元方法调用错误传播
 * - 内存分配失败处理
 *
 * 算法复杂度：
 * - 时间复杂度：O(总字符数)
 * - 空间复杂度：O(结果长度)
 * - 最优的字符串连接算法
 * - 避免二次复制开销
 *
 * 使用场景：
 * - 实现..运算符
 * - 字符串构建和格式化
 * - 模板系统
 * - 动态代码生成
 *
 * 栈操作：
 * - 输入：栈顶total个值
 * - 输出：单个连接结果
 * - 就地操作，减少栈使用
 *
 * @param L Lua状态机指针
 * @param total 要连接的值的总数
 * @param last 最后一个值在栈中的索引
 *
 * @note 支持数字到字符串的自动转换
 * @note 优化了多个连续连接操作
 * @warning 可能触发垃圾回收
 *
 * @since Lua 5.1
 * @see luaV_tostring, __concat元方法, 字符串连接
 */
void luaV_concat(lua_State *L, int total, int last)
{
    do {
        StkId top = L->base + last + 1;
        int n = 2;
        if (!(ttisstring(top-2) || ttisnumber(top-2)) || !tostring(L, top-1)) {
            if (!call_binTM(L, top-2, top-1, top-2, TM_CONCAT))
                luaG_concaterror(L, top-2, top-1);
        } else if (tsvalue(top-1)->len == 0)
            (void)tostring(L, top - 2);
        else {
            size_t tl = tsvalue(top-1)->len;
            char *buffer;
            int i;
            for (n = 1; n < total && tostring(L, top-n-1); n++) {
                size_t l = tsvalue(top-n-1)->len;
                if (l >= MAX_SIZET - tl) luaG_runerror(L, "string length overflow");
                tl += l;
            }
            buffer = luaZ_openspace(L, &G(L)->buff, tl);
            tl = 0;
            for (i=n; i>0; i--) {
                size_t l = tsvalue(top-i)->len;
                memcpy(buffer+tl, svalue(top-i), l);
                tl += l;
            }
            setsvalue2s(L, top-n, luaS_newlstr(L, buffer, tl));
        }
        total -= n-1;
        last -= n-1;
    } while (total > 1);
}


/** @} */

/**
 * @name 虚拟机执行引擎
 * @brief Lua字节码解释器和执行引擎的核心实现
 * @{
 */

/**
 * @brief 算术运算处理函数
 *
 * 详细说明：
 * 处理所有算术运算的统一函数，支持数字运算和元方法调用。
 * 这是虚拟机算术指令的核心实现。
 *
 * 运算处理流程：
 * 1. 尝试将操作数转换为数字
 * 2. 如果转换成功，执行相应的数字运算
 * 3. 如果转换失败，尝试调用元方法
 * 4. 如果没有元方法，抛出算术错误
 *
 * 支持的运算：
 * - TM_ADD：加法运算（+）
 * - TM_SUB：减法运算（-）
 * - TM_MUL：乘法运算（*）
 * - TM_DIV：除法运算（/）
 * - TM_MOD：取模运算（%）
 * - TM_POW：幂运算（^）
 * - TM_UNM：一元负号（-）
 *
 * 类型转换：
 * - 使用luaV_tonumber进行安全转换
 * - 支持数字和数字字符串
 * - 使用临时变量避免修改原值
 * - 转换失败时回退到元方法
 *
 * 元方法支持：
 * - 自动查找相应的算术元方法
 * - 支持用户自定义运算行为
 * - 实现运算符重载
 * - 面向对象编程支持
 *
 * 性能优化：
 * - 数字运算的快速路径
 * - 最小化类型检查开销
 * - 高效的元方法调用
 * - 避免不必要的对象创建
 *
 * 错误处理：
 * - 类型转换失败处理
 * - 算术错误报告
 * - 元方法调用错误传播
 * - 清晰的错误信息
 *
 * @param L Lua状态机指针
 * @param ra 结果存储位置
 * @param rb 第一个操作数（或唯一操作数）
 * @param rc 第二个操作数（二元运算）
 * @param op 运算类型
 *
 * @note 支持一元和二元算术运算
 * @note 自动处理类型转换和元方法
 * @warning 元方法调用可能修改虚拟机状态
 *
 * @since Lua 5.1
 * @see luaV_tonumber, call_binTM, 算术元方法
 */
static void Arith(lua_State *L, StkId ra, const TValue *rb,
                  const TValue *rc, TMS op)
{
    TValue tempb, tempc;
    const TValue *b, *c;
    if ((b = luaV_tonumber(rb, &tempb)) != NULL &&
        (c = luaV_tonumber(rc, &tempc)) != NULL) {
        lua_Number nb = nvalue(b), nc = nvalue(c);
        switch (op) {
            case TM_ADD: setnvalue(ra, luai_numadd(nb, nc)); break;
            case TM_SUB: setnvalue(ra, luai_numsub(nb, nc)); break;
            case TM_MUL: setnvalue(ra, luai_nummul(nb, nc)); break;
            case TM_DIV: setnvalue(ra, luai_numdiv(nb, nc)); break;
            case TM_MOD: setnvalue(ra, luai_nummod(nb, nc)); break;
            case TM_POW: setnvalue(ra, luai_numpow(nb, nc)); break;
            case TM_UNM: setnvalue(ra, luai_numunm(nb)); break;
            default: lua_assert(0); break;
        }
    }
    else if (!call_binTM(L, rb, rc, ra, op))
        luaG_aritherror(L, rb, rc);
}



/**
 * @name 虚拟机执行宏定义
 * @brief luaV_execute函数中使用的通用宏定义
 * @{
 */

/**
 * @brief 运行时检查宏
 *
 * 详细说明：
 * 在虚拟机执行过程中进行运行时检查，如果条件不满足则跳出当前循环。
 * 用于处理运行时错误和异常情况。
 *
 * @param L Lua状态机指针
 * @param c 要检查的条件
 */
#define runtime_check(L, c) { if (!(c)) break; }

/**
 * @brief 获取指令的A寄存器地址
 *
 * 详细说明：
 * 计算指令A字段对应的栈位置。A字段通常用于存储结果。
 *
 * @param i 指令
 * @return 指向A寄存器的指针
 */
#define RA(i) (base+GETARG_A(i))

/**
 * @brief 获取指令的B寄存器地址
 *
 * 详细说明：
 * 计算指令B字段对应的栈位置。仅用于寄存器操作数。
 * 在可能的栈重分配后使用。
 *
 * @param i 指令
 * @return 指向B寄存器的指针
 */
#define RB(i) check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))

/**
 * @brief 获取指令的C寄存器地址
 *
 * 详细说明：
 * 计算指令C字段对应的栈位置。仅用于寄存器操作数。
 *
 * @param i 指令
 * @return 指向C寄存器的指针
 */
#define RC(i) check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))

/**
 * @brief 获取指令的B操作数（寄存器或常量）
 *
 * 详细说明：
 * 根据B字段的值决定是访问寄存器还是常量表。
 * 支持寄存器和常量的统一访问。
 *
 * @param i 指令
 * @return 指向B操作数的指针
 */
#define RKB(i) check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
    ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))

/**
 * @brief 获取指令的C操作数（寄存器或常量）
 *
 * 详细说明：
 * 根据C字段的值决定是访问寄存器还是常量表。
 * 支持寄存器和常量的统一访问。
 *
 * @param i 指令
 * @return 指向C操作数的指针
 */
#define RKC(i) check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
    ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))

/**
 * @brief 获取指令的Bx常量
 *
 * 详细说明：
 * 获取指令Bx字段对应的常量表项。用于访问大索引的常量。
 *
 * @param i 指令
 * @return 指向常量的指针
 */
#define KBx(i) check_exp(getBMode(GET_OPCODE(i)) == OpArgK, k+GETARG_Bx(i))

/**
 * @brief 执行跳转操作
 *
 * 详细说明：
 * 更新程序计数器并让出线程控制权。用于实现条件跳转和循环。
 *
 * @param L Lua状态机指针
 * @param pc 程序计数器
 * @param i 跳转偏移量
 */
#define dojump(L,pc,i) {(pc) += (i); luai_threadyield(L);}

/**
 * @brief 保护操作宏
 *
 * 详细说明：
 * 在执行可能导致栈重分配的操作前保存程序计数器，
 * 操作后恢复base指针。确保虚拟机状态的一致性。
 *
 * @param x 要保护的操作
 */
#define Protect(x) { L->savedpc = pc; {x;}; base = L->base; }

/**
 * @brief 算术运算宏
 *
 * 详细说明：
 * 实现算术运算的通用宏，优化了数字运算的快速路径。
 * 如果操作数都是数字，直接计算；否则调用Arith函数处理元方法。
 *
 * @param op 数字运算操作
 * @param tm 对应的元方法类型
 */
#define arith_op(op,tm) { \
        TValue *rb = RKB(i); \
        TValue *rc = RKC(i); \
        if (ttisnumber(rb) && ttisnumber(rc)) { \
          lua_Number nb = nvalue(rb), nc = nvalue(rc); \
          setnvalue(ra, op(nb, nc)); \
        } \
        else \
          Protect(Arith(L, ra, rb, rc, tm)); \
      }

/** @} */



/**
 * @brief Lua虚拟机核心执行引擎
 *
 * 详细说明：
 * 这是Lua虚拟机的心脏，负责解释和执行Lua字节码。实现了基于寄存器的
 * 虚拟机架构，提供高效的指令分发和执行。
 *
 * 虚拟机架构：
 * - 基于寄存器的设计（相对于基于栈的设计）
 * - 使用程序计数器（PC）跟踪执行位置
 * - 维护函数调用栈和局部变量栈
 * - 支持常量表和上值访问
 *
 * 执行模型：
 * 1. 获取当前函数的闭包和常量表
 * 2. 设置栈基址和程序计数器
 * 3. 进入主执行循环
 * 4. 逐条获取和执行指令
 * 5. 处理调试钩子和线程让出
 * 6. 根据指令类型分发执行
 *
 * 调试支持：
 * - 行钩子：每执行新行时调用
 * - 计数钩子：每执行指定数量指令时调用
 * - 支持断点和单步调试
 * - 线程让出和恢复机制
 *
 * 性能优化：
 * - 高效的指令解码和分发
 * - 寄存器架构减少栈操作
 * - 内联的快速路径优化
 * - 最小化函数调用开销
 *
 * 内存管理：
 * - 自动栈管理和扩展
 * - 垃圾回收集成
 * - 安全的指针操作
 * - 栈溢出检测
 *
 * 错误处理：
 * - 运行时错误检测
 * - 异常传播机制
 * - 栈状态恢复
 * - 错误信息生成
 *
 * 指令集特性：
 * - 支持所有Lua语言特性
 * - 算术和逻辑运算
 * - 表操作和元方法调用
 * - 函数调用和返回
 * - 控制流和跳转
 *
 * 重入支持：
 * - 支持协程和线程切换
 * - 保存和恢复执行状态
 * - 嵌套调用处理
 * - 状态一致性保证
 *
 * 使用场景：
 * - 执行编译后的Lua字节码
 * - 实现Lua语言的所有特性
 * - 支持调试和性能分析
 * - 提供可扩展的执行环境
 *
 * @param L Lua状态机指针
 * @param nexeccalls 嵌套执行调用计数（用于栈溢出检测）
 *
 * @note 这是一个可重入函数，支持协程切换
 * @note 执行过程中可能触发垃圾回收
 * @warning 长时间执行可能需要让出控制权
 *
 * @since Lua 5.1
 * @see 字节码指令集, 虚拟机架构, 调试API
 */
void luaV_execute(lua_State *L, int nexeccalls) {
    // 局部变量声明 - 虚拟机执行状态
    LClosure *cl;              // 当前执行的Lua闭包
    StkId base;                // 栈基址指针
    TValue *k;                 // 常量表指针
    const Instruction *pc;     // 程序计数器

reentry:
    // 虚拟机状态初始化
    lua_assert(isLua(L->ci));
    pc = L->savedpc;
    cl = &clvalue(L->ci->func)->l;
    base = L->base;
    k = cl->p->k;

    // 主执行循环 - 字节码解释执行
    for (;;) {
        const Instruction i = *pc++;    // 获取当前指令并递增PC
        StkId ra;                       // 指令的A操作数

        // 调试钩子检查 - 处理行钩子和计数钩子
        if ((L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT)) &&
            (--L->hookcount == 0 || L->hookmask & LUA_MASKLINE)) {

            traceexec(L, pc);
            if (L->status == LUA_YIELD) {
                L->savedpc = pc - 1;
                return;
            }
            base = L->base;
        }

        // 指令解码和断言检查
        ra = RA(i);
        lua_assert(base == L->base && L->base == L->ci->base);
        lua_assert(base <= L->top && L->top <= L->stack + L->stacksize);
        lua_assert(L->top == L->ci->top || luaG_checkopenop(i));

        // 字节码指令分发
        switch (GET_OPCODE(i)) {
            case OP_MOVE: {
                setobjs2s(L, ra, RB(i));
                continue;
            }

            case OP_LOADK: {
                setobj2s(L, ra, KBx(i));
                continue;
            }

            case OP_LOADBOOL: {
                setbvalue(ra, GETARG_B(i));
                if (GETARG_C(i)) {
                    pc++;
                }
                continue;
            }

            case OP_LOADNIL: {
                TValue *rb = RB(i);
                do {
                    setnilvalue(rb--);
                } while (rb >= ra);
                continue;
            }
            case OP_GETUPVAL: {
                int b = GETARG_B(i);
                setobj2s(L, ra, cl->upvals[b]->v);
                continue;
            }

            case OP_GETGLOBAL: {
                TValue g;
                TValue *rb = KBx(i);
                sethvalue(L, &g, cl->env);
                lua_assert(ttisstring(rb));
                Protect(luaV_gettable(L, &g, rb, ra));
                continue;
            }

            case OP_GETTABLE: {
                Protect(luaV_gettable(L, RB(i), RKC(i), ra));
                continue;
            }

            case OP_SETGLOBAL: {
                TValue g;
                sethvalue(L, &g, cl->env);
                lua_assert(ttisstring(KBx(i)));
                Protect(luaV_settable(L, &g, KBx(i), ra));
                continue;
            }

            case OP_SETUPVAL: {
                UpVal *uv = cl->upvals[GETARG_B(i)];
                setobj(L, uv->v, ra);
                luaC_barrier(L, uv, ra);
                continue;
            }

            case OP_SETTABLE: {
                Protect(luaV_settable(L, ra, RKB(i), RKC(i)));
                continue;
            }

            case OP_NEWTABLE: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                sethvalue(L, ra, luaH_new(L, luaO_fb2int(b), luaO_fb2int(c)));
                Protect(luaC_checkGC(L));
                continue;
            }

            case OP_SELF: {
                StkId rb = RB(i);
                setobjs2s(L, ra + 1, rb);
                Protect(luaV_gettable(L, rb, RKC(i), ra));
                continue;
            }
            // 算术运算指令组
            case OP_ADD: {
                arith_op(luai_numadd, TM_ADD);
                continue;
            }

            case OP_SUB: {
                arith_op(luai_numsub, TM_SUB);
                continue;
            }

            case OP_MUL: {
                arith_op(luai_nummul, TM_MUL);
                continue;
            }

            case OP_DIV: {
                arith_op(luai_numdiv, TM_DIV);
                continue;
            }

            case OP_MOD: {
                arith_op(luai_nummod, TM_MOD);
                continue;
            }

            case OP_POW: {
                arith_op(luai_numpow, TM_POW);
                continue;
            }

            case OP_UNM: {
                TValue *rb = RB(i);
                if (ttisnumber(rb)) {
                    lua_Number nb = nvalue(rb);
                    setnvalue(ra, luai_numunm(nb));
                } else {
                    Protect(Arith(L, ra, rb, rb, TM_UNM));
                }
                continue;
            }

            case OP_NOT: {
                int res = l_isfalse(RB(i));
                setbvalue(ra, res);
                continue;
            }
            case OP_LEN: {
                const TValue *rb = RB(i);
                switch (ttype(rb)) {
                    case LUA_TTABLE: {
                        setnvalue(ra, cast_num(luaH_getn(hvalue(rb))));
                        break;
                    }

                    case LUA_TSTRING: {
                        setnvalue(ra, cast_num(tsvalue(rb)->len));
                        break;
                    }

                    default: {
                        Protect(
                            if (!call_binTM(L, rb, luaO_nilobject, ra, TM_LEN))
                                luaG_typeerror(L, rb, "get length of");
                        )
                    }
                }
                continue;
            }

            case OP_CONCAT: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                Protect(luaV_concat(L, c - b + 1, c); luaC_checkGC(L));
                setobjs2s(L, RA(i), base + b);
                continue;
            }

            // 跳转和比较指令组
            case OP_JMP: {
                dojump(L, pc, GETARG_sBx(i));
                continue;
            }
            case OP_EQ: {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                Protect(
                    if (equalobj(L, rb, rc) == GETARG_A(i))
                        dojump(L, pc, GETARG_sBx(*pc));
                )
                pc++;
                continue;
            }

            case OP_LT: {
                Protect(
                    if (luaV_lessthan(L, RKB(i), RKC(i)) == GETARG_A(i))
                        dojump(L, pc, GETARG_sBx(*pc));
                )
                pc++;
                continue;
            }

            case OP_LE: {
                Protect(
                    if (lessequal(L, RKB(i), RKC(i)) == GETARG_A(i))
                        dojump(L, pc, GETARG_sBx(*pc));
                )
                pc++;
                continue;
            }

            case OP_TEST: {
                if (l_isfalse(ra) != GETARG_C(i)) {
                    dojump(L, pc, GETARG_sBx(*pc));
                }
                pc++;
                continue;
            }

            case OP_TESTSET: {
                TValue *rb = RB(i);
                if (l_isfalse(rb) != GETARG_C(i)) {
                    setobjs2s(L, ra, rb);
                    dojump(L, pc, GETARG_sBx(*pc));
                }
                pc++;
                continue;
            }
            // 函数调用指令组
            case OP_CALL: {
                int b = GETARG_B(i);
                int nresults = GETARG_C(i) - 1;

                if (b != 0) {
                    L->top = ra + b;
                }
                L->savedpc = pc;

                switch (luaD_precall(L, ra, nresults)) {
                    case PCRLUA: {
                        nexeccalls++;
                        goto reentry;
                    }

                    case PCRC: {
                        if (nresults >= 0) {
                            L->top = L->ci->top;
                        }
                        base = L->base;
                        continue;
                    }

                    default: {
                        return;
                    }
                }
            }
            case OP_TAILCALL: {
                int b = GETARG_B(i);

                if (b != 0) {
                    L->top = ra + b;
                }
                L->savedpc = pc;
                lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);

                switch (luaD_precall(L, ra, LUA_MULTRET)) {
                    case PCRLUA: {
                        CallInfo *ci = L->ci - 1;
                        int aux;
                        StkId func = ci->func;
                        StkId pfunc = (ci + 1)->func;

                        if (L->openupval) {
                            luaF_close(L, ci->base);
                        }

                        L->base = ci->base = ci->func + ((ci + 1)->base - pfunc);

                        for (aux = 0; pfunc + aux < L->top; aux++) {
                            setobjs2s(L, func + aux, pfunc + aux);
                        }

                        ci->top = L->top = func + aux;
                        lua_assert(L->top == L->base +
                                   clvalue(func)->l.p->maxstacksize);
                        ci->savedpc = L->savedpc;
                        ci->tailcalls++;
                        L->ci--;
                        goto reentry;
                    }

                    case PCRC: {
                        base = L->base;
                        continue;
                    }

                    default: {
                        return;
                    }
                }
            }
            case OP_RETURN: {
                int b = GETARG_B(i);

                if (b != 0) {
                    L->top = ra + b - 1;
                }

                if (L->openupval) {
                    luaF_close(L, base);
                }

                L->savedpc = pc;
                b = luaD_poscall(L, ra);

                if (--nexeccalls == 0) {
                    return;
                } else {
                    if (b) {
                        L->top = L->ci->top;
                    }
                    lua_assert(isLua(L->ci));
                    lua_assert(GET_OPCODE(*((L->ci)->savedpc - 1)) == OP_CALL);
                    goto reentry;
                }
            }
            // 循环控制指令组
            case OP_FORLOOP: {
                lua_Number step = nvalue(ra + 2);
                lua_Number idx = luai_numadd(nvalue(ra), step);
                lua_Number limit = nvalue(ra + 1);

                if (luai_numlt(0, step) ? luai_numle(idx, limit)
                                        : luai_numle(limit, idx)) {
                    dojump(L, pc, GETARG_sBx(i));
                    setnvalue(ra, idx);
                    setnvalue(ra + 3, idx);
                }
                continue;
            }

            case OP_FORPREP: {
                const TValue *init = ra;
                const TValue *plimit = ra + 1;
                const TValue *pstep = ra + 2;

                L->savedpc = pc;

                if (!tonumber(init, ra)) {
                    luaG_runerror(L, LUA_QL("for")
                                  " initial value must be a number");
                } else if (!tonumber(plimit, ra + 1)) {
                    luaG_runerror(L, LUA_QL("for")
                                  " limit must be a number");
                } else if (!tonumber(pstep, ra + 2)) {
                    luaG_runerror(L, LUA_QL("for")
                                  " step must be a number");
                }

                setnvalue(ra, luai_numsub(nvalue(ra), nvalue(pstep)));
                dojump(L, pc, GETARG_sBx(i));
                continue;
            }
            case OP_TFORLOOP: {
                StkId cb = ra + 3;
                setobjs2s(L, cb + 2, ra + 2);
                setobjs2s(L, cb + 1, ra + 1);
                setobjs2s(L, cb, ra);
                L->top = cb + 3;

                Protect(luaD_call(L, cb, GETARG_C(i)));
                L->top = L->ci->top;
                cb = RA(i) + 3;

                if (!ttisnil(cb)) {
                    setobjs2s(L, cb - 1, cb);
                    dojump(L, pc, GETARG_sBx(*pc));
                }
                pc++;
                continue;
            }

            case OP_SETLIST: {
                int n = GETARG_B(i);
                int c = GETARG_C(i);
                int last;
                Table *h;

                if (n == 0) {
                    n = cast_int(L->top - ra) - 1;
                    L->top = L->ci->top;
                }

                if (c == 0) {
                    c = cast_int(*pc++);
                }

                runtime_check(L, ttistable(ra));
                h = hvalue(ra);
                last = ((c - 1) * LFIELDS_PER_FLUSH) + n;

                if (last > h->sizearray) {
                    luaH_resizearray(L, h, last);
                }

                for (; n > 0; n--) {
                    TValue *val = ra + n;
                    setobj2t(L, luaH_setnum(L, h, last--), val);
                    luaC_barriert(L, h, val);
                }
                continue;
            }
            case OP_CLOSE: {
                luaF_close(L, ra);
                continue;
            }

            case OP_CLOSURE: {
                Proto *p;
                Closure *ncl;
                int nup, j;

                p = cl->p->p[GETARG_Bx(i)];
                nup = p->nups;
                ncl = luaF_newLclosure(L, nup, cl->env);
                ncl->l.p = p;

                for (j = 0; j < nup; j++, pc++) {
                    if (GET_OPCODE(*pc) == OP_GETUPVAL) {
                        ncl->l.upvals[j] = cl->upvals[GETARG_B(*pc)];
                    } else {
                        lua_assert(GET_OPCODE(*pc) == OP_MOVE);
                        ncl->l.upvals[j] = luaF_findupval(L, base + GETARG_B(*pc));
                    }
                }

                setclvalue(L, ra, ncl);
                Protect(luaC_checkGC(L));
                continue;
            }

            case OP_VARARG: {
                int b = GETARG_B(i) - 1;
                int j;
                CallInfo *ci = L->ci;
                int n = cast_int(ci->base - ci->func) - cl->p->numparams - 1;

                if (b == LUA_MULTRET) {
                    Protect(luaD_checkstack(L, n));
                    ra = RA(i);
                    b = n;
                    L->top = ra + n;
                }

                for (j = 0; j < b; j++) {
                    if (j < n) {
                        setobjs2s(L, ra + j, ci->base - n + j);
                    } else {
                        setnilvalue(ra + j);
                    }
                }
                continue;
            }
        }
    }
}

/** @} */

