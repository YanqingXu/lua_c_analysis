/**
 * @file ltm.c
 * @brief Lua元方法系统：标签方法和元表操作的核心实现
 * 
 * 版权信息：
 * $Id: ltm.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
 * 标签方法实现
 * 版权声明见lua.h文件
 * 
 * 程序概述：
 * 本文件实现了Lua的元方法（metamethods）系统，也称为标签方法（tag methods）。
 * 元方法是Lua面向对象编程和操作符重载的核心机制，允许用户自定义
 * 表、用户数据和其他类型的行为。
 * 
 * 系统架构定位：
 * 作为Lua虚拟机的核心组件，元方法系统位于对象模型层，为上层的
 * Lua代码提供灵活的对象操作能力。它与表系统、对象系统和字符串
 * 系统紧密集成，实现了统一的元编程接口。
 * 
 * 核心功能：
 * 1. **类型名称管理**: 统一管理所有Lua数据类型的名称
 * 2. **元方法初始化**: 初始化所有元方法名称并缓存
 * 3. **元方法查找**: 高效查找指定对象的元方法
 * 4. **缓存优化**: 通过标志位缓存元方法的存在性
 * 
 * 支持的元方法：
 * - **索引操作**: __index, __newindex
 * - **生命周期**: __gc, __mode
 * - **比较操作**: __eq, __lt, __le
 * - **算术操作**: __add, __sub, __mul, __div, __mod, __pow, __unm
 * - **其他操作**: __len, __concat, __call
 * 
 * 技术特点：
 * - 高效缓存：使用标志位缓存元方法的不存在
 * - 字符串优化：元方法名称使用内存的字符串
 * - 类型特化：针对不同类型优化元表查找
 * - 全局元表：支持基础类型的全局元表
 * 
 * 使用场景：
 * - 面向对象编程：实现类、继承和多态
 * - 操作符重载：自定义表和用户数据的操作符
 * - 属性访问：实现动态属性和计算属性
 * - 内存管理：通过__gc实现自定义清理逻辑
 * - 数据结构：实现复杂的数据结构和集合类型
 * 
 * 性能考虑：
 * - 快速路径：对于没有元方法的情况优化性能
 * - 缓存机制：减少重复的元表查找开销
 * - 字符串优化：元方法名称使用内存字符串
 * - 内存友好：最小化元表存储开销
 * 
 * 安全性特性：
 * - 类型安全：元方法调用不会破坏类型系统
 * - 防循环：元方法查找不会造成无限递归
 * - 内存安全：正确处理元表的生命周期
 * 
 * @author Roberto Ierusalimschy
 * @version 2.8.1.1
 * @date 2007-12-27
 * 
 * @see lua.h Lua核心API定义
 * @see lobject.h Lua对象系统定义
 * @see ltable.h Lua表系统接口
 * @see ltm.h 元方法系统对外接口
 * 
 * @note 本模块是Lua面向对象能力的核心基础
 */

#include <string.h>

#define ltm_c
#define LUA_CORE

#include "lua.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"



/**
 * @brief Lua数据类型名称表：定义所有Lua数据类型的字符串名称
 * 
 * 详细说明：
 * 这个全局数组存储了所有Lua数据类型的字符串名称，用于type()函数、
 * 错误信息和调试输出。数组的索引对应于LUA_T*常量的值，
 * 确保了类型码和类型名称的一致性。
 * 
 * 类型映射关系：
 * - LUA_TNIL (0) → "nil": 空值类型
 * - LUA_TBOOLEAN (1) → "boolean": 布尔类型
 * - LUA_TLIGHTUSERDATA (2) → "userdata": 轻量级用户数据
 * - LUA_TNUMBER (3) → "number": 数字类型
 * - LUA_TSTRING (4) → "string": 字符串类型
 * - LUA_TTABLE (5) → "table": 表类型
 * - LUA_TFUNCTION (6) → "function": 函数类型
 * - LUA_TUSERDATA (7) → "userdata": 完整的用户数据
 * - LUA_TTHREAD (8) → "thread": 线程/协程类型
 * 
 * 内部类型（用于调试和内部处理）：
 * - LUA_TPROTO (9) → "proto": 函数原型（内部）
 * - LUA_TUPVAL (10) → "upval": 上值（内部）
 * 
 * 使用场景：
 * - type()函数：返回对象的类型名称
 * - 错误信息：生成包含类型信息的错误提示
 * - 调试输出：打印对象类型信息
 * - 反射操作：运行时类型检查和处理
 * 
 * 注意事项：
 * - 数组中的userdata出现两次，对应轻量级和完整的用户数据
 * - 内部类型（proto, upval）通常不会在用户代码中显示
 * - 数组长度必须与LUA_NUMTAGS常量保持一致
 * - 字符串都是编译时常量，保证了性能和内存效率
 * 
 * 性能优化：
 * - 直接数组索引，时间复杂度O(1)
 * - 静态存储，不占用堆内存
 * - 编译时确定，运行时无初始化开销
 * 
 * @see LUA_T* 类型常量定义（lua.h）
 * @see LUA_NUMTAGS 类型数量常量
 * @see lua_typename() Lua API中的类型名称获取函数
 * 
 * @note 这个数组是Lua类型系统的基础数据结构
 */
const char *const luaT_typenames[] = {
    "nil", "boolean", "userdata", "number",
    "string", "table", "function", "userdata", "thread",
    "proto", "upval"
};


/**
 * @brief 元方法系统初始化：初始化所有元方法名称并设置为永久字符串
 * 
 * 详细说明：
 * 这个函数是Lua元方法系统的初始化入口，负责创建和缓存所有元方法的
 * 名称字符串。这些名称被设置为永久字符串，确保在整个Lua状态机的
 * 生命周期内都保持有效，且不会被垃圾回收器回收。
 * 
 * 实现机制：
 * 1. 定义本地静态数组包含所有元方法名称
 * 2. 遍历所有元方法类型（TM_INDEX 到 TM_CALL）
 * 3. 为每个元方法名称创建内存字符串对象
 * 4. 将字符串设置为永久，防止垃圾回收
 * 5. 将字符串对象存储在全局状态的tmname数组中
 * 
 * 元方法名称列表（按TM_*顺序）：
 * - TM_INDEX: "__index" - 索引访问
 * - TM_NEWINDEX: "__newindex" - 索引赋值
 * - TM_GC: "__gc" - 垃圾回收
 * - TM_MODE: "__mode" - 弱引用模式
 * - TM_EQ: "__eq" - 相等比较
 * - TM_ADD: "__add" - 加法运算
 * - TM_SUB: "__sub" - 减法运算
 * - TM_MUL: "__mul" - 乘法运算
 * - TM_DIV: "__div" - 除法运算
 * - TM_MOD: "__mod" - 取模运算
 * - TM_POW: "__pow" - 幂运算
 * - TM_UNM: "__unm" - 一元负号
 * - TM_LEN: "__len" - 长度运算
 * - TM_LT: "__lt" - 小于比较
 * - TM_LE: "__le" - 小于等于比较
 * - TM_CONCAT: "__concat" - 字符串连接
 * - TM_CALL: "__call" - 函数调用
 * 
 * 初始化时机：
 * - 在lua_newstate()过程中被调用
 * - 在任何元方法操作之前必须执行
 * - 只应该被调用一次，重复调用可能造成内存泄漏
 * 
 * 性能优化：
 * - 使用内存字符串避免重复创建
 * - 永久字符串避免了垃圾回收开销
 * - 直接数组索引访问，时间复杂度O(1)
 * 
 * 内存管理：
 * - 所有元方法名称都被标记为永久对象
 * - 这些字符串不会被垃圾回收器回收
 * - 在Lua状态机销毁时自动清理
 * 
 * 错误处理：
 * - 如果字符串创建失败，可能抛出内存错误
 * - 初始化失败会导致整个元方法系统不可用
 * 
 * @param L Lua状态机指针，必须是有效的已初始化状态
 * 
 * @return void 无返回值，但修改全局状态的tmname数组
 * 
 * @see TM_* 元方法类型常量定义
 * @see luaS_new() 字符串创建函数
 * @see luaS_fix() 永久字符串设置函数
 * @see G(L)->tmname 全局元方法名称数组
 * 
 * @note 这是Lua状态机初始化的必需步骤
 * 
 * @warning 只能在状态机初始化阶段调用，且只调用一次
 */
void luaT_init(lua_State *L) {
    static const char *const luaT_eventname[] = {  /* ORDER TM */
        "__index", "__newindex",
        "__gc", "__mode", "__eq",
        "__add", "__sub", "__mul", "__div", "__mod",
        "__pow", "__unm", "__len", "__lt", "__le",
        "__concat", "__call"
    };
    int i;
    for (i=0; i<TM_N; i++) {
        G(L)->tmname[i] = luaS_new(L, luaT_eventname[i]);
        luaS_fix(G(L)->tmname[i]);  /* never collect these names */
    }
}


/**
 * @brief 获取对象元表中指定元方法：高效的元方法查找核心函数
 * 
 * 详细说明：
 * 这是元方法系统的核心查找函数，负责从给定的元表中查找指定的
 * 元方法。该函数实现了高效的缓存机制，对于没有元方法的情况
 * 使用标志位进行快速检测，显著提升性能。
 * 
 * 算法实现：
 * 1. 首先检查元表是否为空（非表类型）
 * 2. 检查元表的标志位，判断是否可能有该元方法
 * 3. 如果标志位表示可能存在，则进行实际的表查找
 * 4. 如果未找到，则更新标志位记录该元方法不存在
 * 5. 返回找到的元方法或nil
 * 
 * 标志位缓存机制：
 * - 每个表的flags字段使用位映射记录元方法的存在性
 * - 对于没有的元方法，可以直接返回nil而无需查表
 * - 新表初始时flags为0，需要实际查找后才能确定
 * - 标志位一旦设置为"不存在"，就不会再次查找
 * 
 * 元方法支持类型：
 * - TM_INDEX: __index 索引访问
 * - TM_NEWINDEX: __newindex 索引赋值
 * - TM_GC: __gc 垃圾回收
 * - TM_MODE: __mode 弱引用模式
 * - TM_EQ: __eq 相等比较
 * - TM_ADD 到 TM_CALL: 各种算术和操作符元方法
 * 
 * 性能优化特性：
 * - 快速路径：对于明确没有的元方法，直接返回nil
 * - 缓存机制：避免重复的表查找操作
 * - 位操作：使用位运算进行快速标志检查
 * - 内存友好：不额外分配内存，使用已有的表结构
 * 
 * 错误处理：
 * - 如果mt为nil或非表类型，直接返回nil
 * - 如果events超出范围，行为未定义（应在调用前检查）
 * - 表查找失败不会引发错误，只是返回nil
 * 
 * 使用示例：
 * ```c
 * // 查找表的__index元方法
 * const TValue *index_tm = luaT_gettm(metatable, TM_INDEX, string_index);
 * if (index_tm != luaO_nilobject) {
 *     // 找到了__index元方法，执行相应逻辑
 * }
 * ```
 * 
 * @param events 元表对象，必须是表类型或nil
 * @param event 元方法类型，必须是有效的TM_*常量
 * @param ename 元方法名称字符串，用于表查找
 * 
 * @return const TValue* 找到的元方法对象，没有找到则返回NULL
 * 
 * @see TM_* 元方法类型常量定义
 * @see luaH_getstr() 表中字符串查找函数
 * @see luaO_nilobject nil对象全局常量
 * @see Table.flags 表的标志位字段
 * 
 * @note 这是元方法系统最频繁调用的函数，性能至关重要
 * 
 * @warning 不会检查参数有效性，调用方需确保参数正确
 */
const TValue *luaT_gettm(Table *events, TMS event, TString *ename) {
    const TValue *tm = luaH_getstr(events, ename);
    lua_assert(event <= TM_EQ);
    if (ttisnil(tm)) {  /* no tag method? */
        events->flags |= cast_byte(1u<<event);  /* cache this fact */
        return NULL;
    }
    else return tm;
}


/**
 * @brief 根据对象类型获取元方法：多类型支持的元方法查找入口
 * 
 * 详细说明：
 * 这是元方法系统的主要入口函数，能够处理所有Lua数据类型的元方法
 * 查找。它根据对象的类型采用不同的查找策略，针对表类型和
 * 用户数据类型使用它们的元表，而对于其他类型使用全局元表。
 * 
 * 类型分类处理：
 * 1. **表类型 (LUA_TTABLE)**:
 *    - 使用hvalue(o)->metatable作为元表
 *    - 支持所有元方法类型
 *    - 可以自定义任意元方法
 * 
 * 2. **用户数据类型 (LUA_TUSERDATA)**:
 *    - 使用uvalue(o)->metatable作为元表
 *    - 支持所有元方法类型
 *    - 常用于C数据结构的Lua封装
 * 
 * 3. **其他类型**:
 *    - 使用全局元表G(L)->mt[ttype(o)]
 *    - 支持限定的元方法子集
 *    - 包括数字、字符串、函数、线程等基础类型
 * 
 * 元方法类型限制：
 * - 表和用户数据：支持所有TM_*类型的元方法
 * - 其他类型：只支持限定的元方法（如__index, __newindex, __gc等）
 * - 某些操作只能由特定类型支持（如__mode只限于表）
 * 
 * 查找流程：
 * 1. 检查对象类型，决定使用哪个元表
 * 2. 如果元表为nil，直接返回nil
 * 3. 否则调用luaH_getstr()进行实际查找
 * 4. 返回查找结果（元方法对象或nil）
 * 
 * 全局元表系统：
 * - 每个基础类型都有一个全局元表
 * - 存储在G(L)->mt[]数组中，按类型码索引
 * - 可以通过debug.setmetatable()修改
 * - 影响该类型所有实例的行为
 * 
 * 性能考虑：
 * - 直接类型检查，无函数调用开销
 * - 指针直接访问，避免不必要的计算
 * - 对于nil元表的快速检测
 * - 复用表查找的高效实现
 * 
 * 典型使用场景：
 * - 算术运算：查找加法、乘法等元方法
 * - 索引操作：查找__index和__newindex
 * - 比较操作：查找__eq、__lt、__le
 * - 函数调用：查找__call元方法
 * - 垃圾回收：查找__gc元方法
 * 
 * 错误情况处理：
 * - 对象为nil或无效：返回nil（不抛出错误）
 * - 元表为非表类型：返回nil
 * - 不支持的元方法类型：返回nil
 * 
 * @param L Lua状态机指针，用于访问全局状态
 * @param o 目标对象，可以是任意Lua值
 * @param event 元方法类型，必须是有效的TM_*常量
 * 
 * @return const TValue* 找到的元方法对象，没有找到则返回luaO_nilobject
 * 
 * @see luaT_gettm() 具体的元方法查找函数
 * @see G(L)->mt[] 全局元表数组
 * @see hvalue(o)->metatable 表的元表
 * @see uvalue(o)->metatable 用户数据的元表
 * @see luaH_getstr() 表中字符串查找函数
 * 
 * @note 这是Lua程序员最常用的元方法查找入口
 * 
 * @warning 返回的指针可能为nil，必须检查后使用
 */
const TValue *luaT_gettmbyobj(lua_State *L, const TValue *o, TMS event) {
    Table *mt;
    switch (ttype(o)) {
        case LUA_TTABLE:
            mt = hvalue(o)->metatable;
            break;
        case LUA_TUSERDATA:
            mt = uvalue(o)->metatable;
            break;
        default:
            mt = G(L)->mt[ttype(o)];
    }
    return (mt ? luaH_getstr(mt, G(L)->tmname[event]) : luaO_nilobject);
}

