/*
** [核心] Lua 元方法(Tag Methods/Metamethods)管理实现
**
** 功能概述：
** 本模块实现了Lua的元方法机制，这是Lua面向对象编程和运算符重载的核心基础。
** 元方法允许用户自定义表和用户数据的行为，包括算术运算、比较操作、索引访问、
** 垃圾回收等。通过元表(metatable)和元方法的组合，Lua提供了强大的元编程能力。
**
** 主要组件：
** - 元方法名称管理：维护所有标准元方法名称
** - 元方法查找机制：从元表中快速定位元方法
** - 类型名称映射：提供类型到字符串的转换
** - 缓存优化：通过标志位避免重复查找
**
** 设计特点：
** - 高性能查找：使用缓存机制避免重复的元方法查找
** - 统一接口：所有类型使用相同的元方法查找API
** - 内存优化：元方法名称字符串固定在内存中不被回收
** - 扩展性：支持用户自定义元方法
**
** 元方法类型：
** - 算术元方法：__add, __sub, __mul, __div, __mod, __pow, __unm
** - 比较元方法：__eq, __lt, __le
** - 索引元方法：__index, __newindex
** - 其他元方法：__gc, __mode, __len, __concat, __call
**
** 依赖模块：
** - lobject.c：基础对象系统
** - lstring.c：字符串管理和intern机制
** - ltable.c：表操作和元表访问
** - lstate.c：全局状态管理
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


/*
** [入门] Lua 类型名称数组
**
** 功能说明：
** 提供从Lua类型枚举值到对应字符串名称的映射。数组索引对应于
** lua.h中定义的类型常量（LUA_TNIL, LUA_TBOOLEAN等）。
**
** 类型映射：
** - 索引0(LUA_TNIL): "nil" - 空值类型
** - 索引1(LUA_TBOOLEAN): "boolean" - 布尔类型
** - 索引2(LUA_TLIGHTUSERDATA): "userdata" - 轻量用户数据
** - 索引3(LUA_TNUMBER): "number" - 数值类型
** - 索引4(LUA_TSTRING): "string" - 字符串类型
** - 索引5(LUA_TTABLE): "table" - 表类型
** - 索引6(LUA_TFUNCTION): "function" - 函数类型
** - 索引7(LUA_TUSERDATA): "userdata" - 完整用户数据
** - 索引8(LUA_TTHREAD): "thread" - 协程类型
** - 索引9: "proto" - 内部原型对象
** - 索引10: "upval" - 内部upvalue对象
**
** 使用场景：
** - 错误消息生成：提供用户友好的类型名称
** - 调试信息：在调试器中显示类型信息
** - 反射操作：运行时获取对象类型名称
**
** 注意事项：
** - 数组元素顺序必须与lua.h中的类型枚举严格一致
** - 最后两个元素是Lua内部类型，用户代码通常不会遇到
** - 这是只读的全局常量数组，在程序生命周期内保持不变
*/
const char *const luaT_typenames[] = {
    "nil", "boolean", "userdata", "number",
    "string", "table", "function", "userdata", "thread",
    "proto", "upval"
};


/*
** [核心] 初始化元方法名称系统
**
** 详细功能说明：
** 在Lua状态机初始化时调用，负责创建并缓存所有标准元方法的名称字符串。
** 这些字符串被标记为永久字符串，不会被垃圾回收器回收，确保元方法
** 查找的高性能和内存安全。
**
** 参数说明：
** @param L - lua_State*：要初始化的Lua状态机指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是元方法数量，O(n) 空间
**
** 初始化流程：
** 1. 定义标准元方法名称数组
** 2. 为每个元方法名称创建字符串对象
** 3. 将字符串对象存储到全局状态的tmname数组中
** 4. 标记字符串为固定字符串，防止被垃圾回收
**
** 元方法列表（按TMS枚举顺序）：
** - __index：索引访问元方法
** - __newindex：索引赋值元方法  
** - __gc：垃圾回收元方法
** - __mode：弱引用模式元方法
** - __eq：相等比较元方法
** - __add：加法运算元方法
** - __sub：减法运算元方法
** - __mul：乘法运算元方法
** - __div：除法运算元方法
** - __mod：取模运算元方法
** - __pow：幂运算元方法
** - __unm：一元减法元方法
** - __len：长度运算元方法
** - __lt：小于比较元方法
** - __le：小于等于比较元方法
** - __concat：连接运算元方法
** - __call：调用元方法
**
** 性能优化：
** - 使用luaS_fix固定字符串，避免垃圾回收开销
** - 预分配所有元方法名称，避免运行时字符串创建
** - 全局缓存确保整个程序生命周期内的快速访问
**
** 注意事项：
** - 必须在Lua状态机的其他初始化之前调用
** - 元方法名称顺序必须与TMS枚举严格一致
** - 失败时可能导致后续元方法操作异常
*/
void luaT_init(lua_State *L)
{
    /*
    ** [元方法映射] 标准元方法名称数组
    ** 顺序必须与ltm.h中的TMS枚举完全一致
    */
    static const char *const luaT_eventname[] = {
        "__index",    /* TM_INDEX：索引访问 */
        "__newindex", /* TM_NEWINDEX：索引赋值 */
        "__gc",       /* TM_GC：垃圾回收 */
        "__mode",     /* TM_MODE：弱引用模式 */
        "__eq",       /* TM_EQ：相等比较 */
        "__add",      /* TM_ADD：加法运算 */
        "__sub",      /* TM_SUB：减法运算 */
        "__mul",      /* TM_MUL：乘法运算 */
        "__div",      /* TM_DIV：除法运算 */
        "__mod",      /* TM_MOD：取模运算 */
        "__pow",      /* TM_POW：幂运算 */
        "__unm",      /* TM_UNM：一元减法 */
        "__len",      /* TM_LEN：长度运算 */
        "__lt",       /* TM_LT：小于比较 */
        "__le",       /* TM_LE：小于等于比较 */
        "__concat",   /* TM_CONCAT：连接运算 */
        "__call"      /* TM_CALL：调用元方法 */
    };
    
    int i;
    
    /*
    ** [字符串创建] 为每个元方法名称创建字符串对象
    ** 并将其存储在全局状态的tmname数组中
    */
    for (i = 0; i < TM_N; i++) 
    {
        /*
        ** [字符串intern] 创建元方法名称的字符串对象
        ** luaS_new会自动处理字符串intern，确保相同字符串只有一个实例
        */
        G(L)->tmname[i] = luaS_new(L, luaT_eventname[i]);
        
        /*
        ** [永久化] 标记为固定字符串，防止垃圾回收
        ** 这确保了元方法名称在整个程序运行期间都有效
        */
        luaS_fix(G(L)->tmname[i]);
    }
}


/*
** [高级] 快速元方法查找（优化版本）
**
** 详细功能说明：
** 这是一个为fasttm宏优化的元方法查找函数，专门处理没有元方法的情况。
** 通过缓存机制避免重复查找，当确定某个元方法不存在时，会在事件表的
** 标志位中记录这个信息，后续查找可以直接返回，避免昂贵的表查找操作。
**
** 参数说明：
** @param events - Table*：元表对象，包含元方法定义
** @param event - TMS：要查找的元方法类型枚举
** @param ename - TString*：元方法名称字符串对象
**
** 返回值：
** @return const TValue*：找到的元方法函数，或NULL表示不存在
**
** 算法复杂度：O(1) 平均时间（有缓存），O(log n) 最坏时间（表查找）
**
** 优化机制：
** 1. 首先在元表中查找指定名称的元方法
** 2. 如果找到非nil值，直接返回
** 3. 如果是nil，在元表标志位中缓存这个结果
** 4. 后续对同一元方法的查找会被fasttm宏拦截
**
** 缓存策略：
** - 使用元表的flags字段存储缓存信息
** - 每个位对应一个元方法类型
** - 仅缓存"不存在"的情况，存在的元方法总是需要返回具体值
**
** 使用场景：
** - 虚拟机执行器中的高频元方法检查
** - 算术运算和比较操作的快速路径判断
** - 与fasttm宏配合实现零开销的元方法检查
**
** 注意事项：
** - 仅处理event <= TM_EQ的元方法（可缓存的类型）
** - 不能直接调用，应该通过fasttm宏使用
** - 元表结构变化时需要清除缓存标志
*/
const TValue *luaT_gettm(Table *events, TMS event, TString *ename)
{
    /*
    ** [表查找] 在元表中查找指定名称的元方法
    */
    const TValue *tm = luaH_getstr(events, ename);
    
    /*
    ** [缓存限制] 断言检查：只有特定元方法可以被缓存
    ** TM_EQ及之前的元方法可以被缓存优化
    */
    lua_assert(event <= TM_EQ);
    
    /*
    ** [结果判断] 检查查找结果并决定缓存策略
    */
    if (ttisnil(tm)) 
    {
        /*
        ** [缓存设置] 元方法不存在，设置缓存标志
        ** 在对应的位上设置标志，避免后续重复查找
        */
        events->flags |= cast_byte(1u << event);
        return NULL;
    }
    else 
    {
        /*
        ** [成功返回] 找到有效的元方法，直接返回
        */
        return tm;
    }
}


/*
** [进阶] 根据对象获取元方法
**
** 详细功能说明：
** 根据给定的Lua值对象，查找其对应的元方法。不同类型的对象有不同的
** 元表获取方式：表和用户数据有自己的元表，其他类型使用全局类型元表。
** 这是Lua元方法系统的统一入口点。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于访问全局状态
** @param o - const TValue*：要查找元方法的对象
** @param event - TMS：要查找的元方法类型
**
** 返回值：
** @return const TValue*：找到的元方法值，或nilobject表示不存在
**
** 算法复杂度：O(1) 时间（类型判断），O(log n) 时间（元方法查找）
**
** 查找策略：
** 1. 根据对象类型确定元表来源
** 2. 表类型：使用对象自身的metatable字段
** 3. 用户数据：使用用户数据的metatable字段
** 4. 其他类型：使用全局状态中的类型元表
** 5. 在确定的元表中查找指定的元方法
**
** 类型处理：
** - LUA_TTABLE：表对象有独立的元表
** - LUA_TUSERDATA：用户数据有独立的元表
** - 其他类型：共享全局类型元表（字符串、数值等）
**
** 返回值语义：
** - 返回luaO_nilobject表示没有找到元方法
** - 返回非nil的TValue表示找到了有效的元方法
** - 元方法可以是函数、表或任何可调用的值
**
** 使用场景：
** - 虚拟机执行器：在执行操作前检查元方法
** - C API：luaL_getmetafield等函数的底层实现
** - 调试系统：获取对象的元信息
**
** 性能考虑：
** - 高频调用的函数，需要尽可能快速
** - 类型判断使用快速的位操作
** - 全局元表访问经过优化的数组索引
*/
const TValue *luaT_gettmbyobj(lua_State *L, const TValue *o, TMS event)
{
    Table *mt;
    
    /*
    ** [元表定位] 根据对象类型确定元表来源
    */
    switch (ttype(o)) 
    {
        case LUA_TTABLE:
        {
            /*
            ** [表元表] 表对象的元表存储在表结构中
            */
            mt = hvalue(o)->metatable;
            break;
        }
        
        case LUA_TUSERDATA:
        {
            /*
            ** [用户数据元表] 用户数据的元表存储在用户数据结构中
            */
            mt = uvalue(o)->metatable;
            break;
        }
        
        default:
        {
            /*
            ** [全局元表] 其他类型使用全局状态中的类型元表
            ** 包括字符串、数值、函数等基础类型
            */
            mt = G(L)->mt[ttype(o)];
        }
    }
    
    /*
    ** [元方法查找] 在确定的元表中查找指定的元方法
    ** 如果没有元表或找不到元方法，返回nil对象
    */
    return (mt ? luaH_getstr(mt, G(L)->tmname[event]) : luaO_nilobject);
}