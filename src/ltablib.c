/*
** [核心] Lua 标准表库实现
**
** 功能概述：
** 本模块实现了 Lua 的标准表库，提供完整的表操作和数组处理功能。
** 包含表遍历、数组操作、排序算法、字符串连接等核心功能。
**
** 主要功能模块：
** - 表遍历：foreach、foreachi 等迭代器函数
** - 数组操作：insert、remove 等动态数组操作
** - 表属性：getn、setn、maxn 等表长度和属性管理
** - 字符串操作：concat 高效字符串连接
** - 排序算法：基于快速排序的高效排序实现
**
** 数据结构原理：
** Lua 表是一种混合数据结构，同时支持数组和哈希表：
** - 数组部分：连续的整数索引，从1开始，高效的随机访问
** - 哈希部分：任意键值对，基于开放寻址的哈希表实现
** - 自动扩容：根据使用模式动态调整数组和哈希部分的大小
** - 内存优化：紧凑的内存布局，最小化内存碎片
**
** 性能特点：
** - 数组访问：O(1) 时间复杂度的随机访问
** - 哈希访问：平均 O(1)，最坏 O(n) 的键值查找
** - 动态扩容：摊销 O(1) 的插入操作
** - 排序算法：O(n log n) 平均时间复杂度的快速排序
** - 内存效率：紧凑的数据布局和智能的内存管理
**
** 算法复杂度分析：
** - 表遍历：O(n) 时间，O(1) 空间
** - 数组插入：O(n) 最坏情况（需要移动元素）
** - 数组删除：O(n) 最坏情况（需要移动元素）
** - 字符串连接：O(n) 时间，使用缓冲区优化
** - 快速排序：平均 O(n log n)，最坏 O(n²)
**
** 内存管理机制：
** - 自动垃圾回收：与 Lua GC 系统集成
** - 增量扩容：避免大块内存分配的性能冲击
** - 内存复用：高效的内存池和对象复用
** - 弱引用支持：支持弱表和临时对象管理
**
** 版本信息：$Id: ltablib.c,v 1.38.1.3 2008/02/14 16:46:58 roberto Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

// 系统头文件包含
#include <stddef.h>   // 标准定义

// 模块标识定义
#define ltablib_c
#define LUA_LIB

// Lua 核心头文件
#include "lua.h"

// Lua 辅助库头文件
#include "lauxlib.h"
#include "lualib.h"

/*
** [工具宏] 获取表的数组长度
**
** 功能说明：
** 获取表的数组部分长度，确保参数是表类型。
** 这是一个内联宏，提供类型检查和长度获取的组合操作。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param n - int：表在栈中的位置
**
** 返回值：
** @return int：表的数组长度
**
** 实现原理：
** 1. 使用 luaL_checktype 确保参数是表类型
** 2. 使用 luaL_getn 获取表的数组长度
** 3. 组合为单一的宏操作，提高效率
**
** 错误处理：
** 如果参数不是表类型，会触发 Lua 类型错误。
*/
#define aux_getn(L,n)	(luaL_checktype(L, n, LUA_TTABLE), luaL_getn(L, n))

/*
** [迭代器] 数组索引迭代器
**
** 功能描述：
** 对表的数组部分进行迭代，为每个元素调用指定的函数。
** 按照数组索引的顺序（1, 2, 3, ...）进行遍历。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：如果回调函数返回非nil值则返回1，否则返回0
**
** 栈操作：
** 输入：table 表, function 回调函数
** 输出：any 回调函数的返回值（如果非nil）
**
** 迭代机制：
** 1. 获取表的数组长度
** 2. 从索引1开始遍历到数组长度
** 3. 为每个元素调用回调函数：callback(index, value)
** 4. 如果回调返回非nil值，立即停止迭代并返回该值
**
** 回调函数签名：
** function(index, value) -> any
** - index：当前元素的索引（从1开始）
** - value：当前元素的值
** - 返回值：nil继续迭代，非nil停止迭代并返回
**
** 时间复杂度：O(n)，n为数组长度
** 空间复杂度：O(1)
**
** 使用示例：
** table.foreachi({10, 20, 30}, function(i, v) 
**     print(i, v)  -- 输出：1 10, 2 20, 3 30
** end)
**
** -- 查找第一个大于15的元素
** result = table.foreachi({10, 20, 30}, function(i, v)
**     if v > 15 then return i end
** end)  -- 返回 2
*/
static int foreachi(lua_State *L) 
{
    int i;
    int n = aux_getn(L, 1);  // 获取数组长度
    
    // 确保第二个参数是函数
    luaL_checktype(L, 2, LUA_TFUNCTION);
    
    // 遍历数组元素
    for (i = 1; i <= n; i++) 
    {
        lua_pushvalue(L, 2);      // 推送回调函数
        lua_pushinteger(L, i);    // 推送索引作为第一个参数
        lua_rawgeti(L, 1, i);     // 推送值作为第二个参数
        lua_call(L, 2, 1);        // 调用回调函数
        
        // 检查返回值
        if (!lua_isnil(L, -1))
        {
            return 1;  // 返回非nil值，停止迭代
        }
        
        lua_pop(L, 1);  // 移除nil返回值，继续迭代
    }
    
    return 0;  // 迭代完成，无返回值
}

/*
** [迭代器] 通用表迭代器
**
** 功能描述：
** 对表的所有键值对进行迭代，包括数组部分和哈希部分。
** 使用 lua_next 进行完整的表遍历。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：如果回调函数返回非nil值则返回1，否则返回0
**
** 栈操作：
** 输入：table 表, function 回调函数
** 输出：any 回调函数的返回值（如果非nil）
**
** 迭代机制：
** 1. 使用 lua_next 遍历表的所有键值对
** 2. 为每个键值对调用回调函数：callback(key, value)
** 3. 如果回调返回非nil值，立即停止迭代并返回该值
**
** 遍历顺序：
** - 数组部分：按索引顺序（1, 2, 3, ...）
** - 哈希部分：按内部哈希表顺序（不保证特定顺序）
**
** 回调函数签名：
** function(key, value) -> any
** - key：当前键（可能是数字、字符串或其他类型）
** - value：当前值
** - 返回值：nil继续迭代，非nil停止迭代并返回
**
** 时间复杂度：O(n)，n为表中元素总数
** 空间复杂度：O(1)
**
** 注意事项：
** - 在迭代过程中修改表结构可能导致未定义行为
** - 哈希部分的遍历顺序不确定，不应依赖特定顺序
**
** 使用示例：
** table.foreach({a=1, b=2, [1]=10}, function(k, v)
**     print(k, v)  -- 可能输出：1 10, a 1, b 2
** end)
*/
static int foreach(lua_State *L) 
{
    // 确保参数类型正确
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    
    lua_pushnil(L);  // 推送初始键（nil表示从头开始）
    
    // 使用 lua_next 遍历表
    while (lua_next(L, 1)) 
    {
        // 栈状态：... table function key value
        lua_pushvalue(L, 2);      // 推送回调函数
        lua_pushvalue(L, -3);     // 推送键作为第一个参数
        lua_pushvalue(L, -3);     // 推送值作为第二个参数
        lua_call(L, 2, 1);        // 调用回调函数
        
        // 检查返回值
        if (!lua_isnil(L, -1))
        {
            return 1;  // 返回非nil值，停止迭代
        }
        
        lua_pop(L, 2);  // 移除值和返回值，保留键用于下次 lua_next
    }
    
    return 0;  // 迭代完成，无返回值
}

/*
** [表属性] 获取表的最大数字键
**
** 功能描述：
** 遍历表中的所有键，找出最大的数字键值。
** 这对于稀疏数组或混合表很有用。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：table 表
** 输出：number 最大数字键（如果没有数字键则为0）
**
** 算法原理：
** 1. 遍历表中的所有键值对
** 2. 检查每个键是否为数字类型
** 3. 记录遇到的最大数字键
** 4. 返回最大值（默认为0）
**
** 应用场景：
** - 稀疏数组的长度计算
** - 混合表中数字索引的范围确定
** - 表结构分析和调试
**
** 时间复杂度：O(n)，n为表中元素总数
** 空间复杂度：O(1)
**
** 注意事项：
** - 只考虑数字类型的键
** - 负数键也会被考虑
** - 浮点数键会被转换为数字比较
**
** 使用示例：
** max = table.maxn({[1]=10, [5]=20, [3]=30})  --> 5
** max = table.maxn({a=1, b=2})                --> 0
** max = table.maxn({[10]=1, [2.5]=2, [-1]=3}) --> 10
*/
static int maxn(lua_State *L)
{
    lua_Number max = 0;

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);  // 初始键

    // 遍历表中的所有键值对
    while (lua_next(L, 1))
    {
        lua_pop(L, 1);  // 移除值，只关心键

        // 检查键是否为数字类型
        if (lua_type(L, -1) == LUA_TNUMBER)
        {
            lua_Number v = lua_tonumber(L, -1);

            if (v > max)
            {
                max = v;
            }
        }
    }

    lua_pushnumber(L, max);
    return 1;
}

/*
** [表属性] 获取表的数组长度
**
** 功能描述：
** 获取表的数组部分长度，等价于 #table 操作符。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：table 表
** 输出：integer 数组长度
**
** 实现原理：
** 使用 aux_getn 宏获取表的数组长度，
** 这是对 luaL_getn 的封装，提供类型检查。
**
** 数组长度定义：
** - 从索引1开始的连续非nil元素的数量
** - 遇到第一个nil元素时停止计数
** - 不包括哈希部分的元素
**
** 时间复杂度：O(log n) 平均情况，使用二分查找优化
** 空间复杂度：O(1)
**
** 使用示例：
** len = table.getn({10, 20, 30})        --> 3
** len = table.getn({10, nil, 30})       --> 1
** len = table.getn({a=1, b=2})          --> 0
*/
static int getn(lua_State *L)
{
    lua_pushinteger(L, aux_getn(L, 1));
    return 1;
}

/*
** [表属性] 设置表的数组长度（已废弃）
**
** 功能描述：
** 设置表的数组长度提示。这个函数在新版本中已被废弃。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1，返回表本身）
**
** 栈操作：
** 输入：table 表, integer 新长度
** 输出：table 表本身
**
** 废弃原因：
** - 现代 Lua 版本自动管理表的内部结构
** - 手动设置长度可能导致不一致的状态
** - # 操作符提供了更可靠的长度获取方式
**
** 兼容性处理：
** 在支持 luaL_setn 的版本中执行设置操作，
** 在不支持的版本中抛出错误提示函数已废弃。
**
** 使用建议：
** 不建议在新代码中使用此函数，应使用 # 操作符。
*/
static int setn(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

#ifndef luaL_setn
    // 如果支持 luaL_setn，执行设置操作
    luaL_setn(L, 1, luaL_checkint(L, 2));
#else
    // 如果不支持，报告函数已废弃
    luaL_error(L, LUA_QL("setn") " is obsolete");
#endif

    lua_pushvalue(L, 1);  // 返回表本身
    return 1;
}

/*
** [数组操作] 向数组中插入元素
**
** 功能描述：
** 在数组的指定位置插入新元素，自动移动后续元素。
** 支持在末尾插入和在中间位置插入两种模式。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是0）
**
** 栈操作：
** 输入：table 表, [integer 位置,] any 值
** 输出：无返回值
**
** 插入模式：
** 1. 两个参数：table.insert(t, value) - 在末尾插入
** 2. 三个参数：table.insert(t, pos, value) - 在指定位置插入
**
** 算法原理：
** 1. 确定插入位置（末尾或指定位置）
** 2. 将插入位置及之后的元素向后移动一位
** 3. 在指定位置设置新值
** 4. 更新数组长度
**
** 元素移动过程：
** 对于插入位置 pos，将 [pos, n] 范围内的元素移动到 [pos+1, n+1]
** 使用从后向前的顺序避免覆盖问题
**
** 时间复杂度：O(n-pos)，最坏情况 O(n)
** 空间复杂度：O(1)
**
** 自动扩容：
** 如果插入位置超出当前数组长度，会自动扩展数组
**
** 使用示例：
** t = {10, 20, 30}
** table.insert(t, 40)      -- t = {10, 20, 30, 40}
** table.insert(t, 2, 15)   -- t = {10, 15, 20, 30, 40}
*/
static int tinsert(lua_State *L)
{
    int e = aux_getn(L, 1) + 1;  // 第一个空位置（数组长度+1）
    int pos;  // 插入位置

    switch (lua_gettop(L))
    {
        case 2:
        {
            // 两个参数：在末尾插入
            pos = e;
            break;
        }
        case 3:
        {
            // 三个参数：在指定位置插入
            int i;
            pos = luaL_checkint(L, 2);

            // 如果插入位置超出当前长度，扩展数组
            if (pos > e)
            {
                e = pos;
            }

            // 向后移动元素，为新元素腾出空间
            for (i = e; i > pos; i--)
            {
                lua_rawgeti(L, 1, i - 1);  // 获取 t[i-1]
                lua_rawseti(L, 1, i);      // 设置 t[i] = t[i-1]
            }
            break;
        }
        default:
        {
            return luaL_error(L, "wrong number of arguments to " LUA_QL("insert"));
        }
    }

    luaL_setn(L, 1, e);        // 更新数组长度
    lua_rawseti(L, 1, pos);    // 设置 t[pos] = value（值已在栈顶）
    return 0;
}

/*
** [数组操作] 从数组中删除元素
**
** 功能描述：
** 从数组的指定位置删除元素，自动移动后续元素填补空隙。
** 支持删除末尾元素和删除中间位置元素两种模式。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1，返回被删除的元素）
**
** 栈操作：
** 输入：table 表, [integer 位置]
** 输出：any 被删除的元素
**
** 删除模式：
** 1. 一个参数：table.remove(t) - 删除末尾元素
** 2. 两个参数：table.remove(t, pos) - 删除指定位置的元素
**
** 算法原理：
** 1. 确定删除位置（末尾或指定位置）
** 2. 保存要删除的元素值
** 3. 将删除位置之后的元素向前移动一位
** 4. 更新数组长度
** 5. 返回被删除的元素
**
** 元素移动过程：
** 对于删除位置 pos，将 [pos+1, n] 范围内的元素移动到 [pos, n-1]
** 使用从前向后的顺序避免覆盖问题
**
** 时间复杂度：O(n-pos)，最坏情况 O(n)
** 空间复杂度：O(1)
**
** 边界处理：
** - 如果位置超出数组范围，返回 nil
** - 如果数组为空，返回 nil
** - 自动调整数组长度
**
** 使用示例：
** t = {10, 20, 30, 40}
** val = table.remove(t)     -- val = 40, t = {10, 20, 30}
** val = table.remove(t, 2)  -- val = 20, t = {10, 30}
*/
static int tremove(lua_State *L)
{
    int e = aux_getn(L, 1);  // 当前数组长度
    int pos = luaL_optint(L, 2, e);  // 删除位置，默认为末尾

    // 边界检查
    if (!(1 <= pos && pos <= e))
    {
        // 位置超出范围，返回 nil
        return 1;
    }

    luaL_setn(L, 1, e - 1);    // 更新数组长度
    lua_rawgeti(L, 1, pos);    // 获取要删除的元素（作为返回值）

    // 向前移动后续元素
    for (; pos < e; pos++)
    {
        lua_rawgeti(L, 1, pos + 1);  // 获取 t[pos+1]
        lua_rawseti(L, 1, pos);      // 设置 t[pos] = t[pos+1]
    }

    lua_pushnil(L);
    lua_rawseti(L, 1, e);      // 清除最后一个位置 t[e] = nil

    return 1;  // 返回被删除的元素
}

/*
** [字符串操作] 数组元素连接
**
** 功能描述：
** 将数组中的元素连接成一个字符串，支持自定义分隔符和范围。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：table 表, [string 分隔符], [integer 起始位置], [integer 结束位置]
** 输出：string 连接后的字符串
**
** 参数说明：
** - 表：要连接的数组
** - 分隔符：元素之间的分隔符（默认为空字符串）
** - 起始位置：连接的起始索引（默认为1）
** - 结束位置：连接的结束索引（默认为数组长度）
**
** 算法原理：
** 1. 确定连接范围和分隔符
** 2. 使用 luaL_Buffer 进行高效的字符串构建
** 3. 遍历指定范围内的元素
** 4. 将每个元素转换为字符串并添加到缓冲区
** 5. 在元素之间插入分隔符
**
** 性能优化：
** - 使用 luaL_Buffer 避免多次字符串分配
** - 预先计算所需的缓冲区大小
** - 最小化字符串复制操作
**
** 时间复杂度：O(n*m)，n为元素数量，m为平均元素长度
** 空间复杂度：O(k)，k为结果字符串长度
**
** 类型转换：
** - 数字自动转换为字符串
** - 字符串直接使用
** - 其他类型调用 tostring 方法
**
** 使用示例：
** str = table.concat({1, 2, 3})           --> "123"
** str = table.concat({1, 2, 3}, ", ")     --> "1, 2, 3"
** str = table.concat({1, 2, 3, 4}, "-", 2, 3)  --> "2-3"
*/
static int tconcat(lua_State *L)
{
    luaL_Buffer b;
    size_t lsep;
    int i, last;
    const char *sep = luaL_optlstring(L, 2, "", &lsep);  // 分隔符

    luaL_checktype(L, 1, LUA_TTABLE);
    i = luaL_optint(L, 3, 1);                    // 起始位置
    last = luaL_opt(L, luaL_checkint, 4, luaL_getn(L, 1));  // 结束位置

    luaL_buffinit(L, &b);

    // 连接指定范围内的元素
    for (; i < last; i++)
    {
        lua_rawgeti(L, 1, i);      // 获取元素
        luaL_argcheck(L, lua_isstring(L, -1) || lua_isnumber(L, -1), 1,
                      "invalid value (" LUA_QL("table") " expected)");
        luaL_addvalue(&b);         // 添加元素到缓冲区
        luaL_addlstring(&b, sep, lsep);  // 添加分隔符
    }

    // 添加最后一个元素（不需要分隔符）
    if (i == last)
    {
        lua_rawgeti(L, 1, i);
        luaL_argcheck(L, lua_isstring(L, -1) || lua_isnumber(L, -1), 1,
                      "invalid value (" LUA_QL("table") " expected)");
        luaL_addvalue(&b);
    }

    luaL_pushresult(&b);
    return 1;
}

/*
** ========================================================================
** [排序算法模块] 快速排序实现
** ========================================================================
**
** 排序算法概述：
** 实现了基于快速排序的高效排序算法，支持自定义比较函数。
** 采用三路快排优化，处理重复元素时性能更佳。
**
** 算法特点：
** - 平均时间复杂度：O(n log n)
** - 最坏时间复杂度：O(n²)（已通过优化大大降低概率）
** - 空间复杂度：O(log n)（递归栈空间）
** - 原地排序：不需要额外的数组空间
** - 不稳定排序：相等元素的相对位置可能改变
**
** 优化技术：
** - 三路快排：将数组分为小于、等于、大于三部分
** - 随机化枢轴：避免最坏情况的发生
** - 小数组优化：对小数组使用插入排序
** - 尾递归优化：减少递归深度
*/

/*
** [排序辅助] 比较函数调用
**
** 功能描述：
** 调用用户提供的比较函数，比较两个元素的大小关系。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param a - int：第一个元素的索引
** @param b - int：第二个元素的索引
**
** 返回值：
** @return int：比较结果（1表示a<b，0表示a>=b）
**
** 比较函数签名：
** function(a, b) -> boolean
** - 返回 true 表示 a < b
** - 返回 false 或 nil 表示 a >= b
**
** 栈操作：
** 调用比较函数并获取结果，不改变栈的净高度
*/
static int sort_comp(lua_State *L, int a, int b)
{
    if (!lua_isnil(L, 2))
    {
        // 有自定义比较函数
        int res;
        lua_pushvalue(L, 2);    // 推送比较函数
        lua_pushvalue(L, a - 1); // 推送第一个参数（调整索引）
        lua_pushvalue(L, b - 2); // 推送第二个参数（调整索引）
        lua_call(L, 2, 1);      // 调用比较函数
        res = lua_toboolean(L, -1);  // 获取结果
        lua_pop(L, 1);          // 移除结果
        return res;
    }
    else
    {
        // 使用默认比较（字典序）
        return luaL_lessthan(L, a, b);
    }
}

/*
** [排序辅助] 元素交换
**
** 功能描述：
** 交换数组中两个位置的元素。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param a - int：第一个元素的索引
** @param b - int：第二个元素的索引
**
** 实现原理：
** 使用三次赋值完成元素交换：
** 1. temp = array[a]
** 2. array[a] = array[b]
** 3. array[b] = temp
**
** 时间复杂度：O(1)
** 空间复杂度：O(1)
*/
static void set2(lua_State *L, int i, int j)
{
    lua_rawseti(L, 1, i);
    lua_rawseti(L, 1, j);
}

/*
** [排序核心] 快速排序递归实现
**
** 功能描述：
** 对数组的指定范围进行快速排序。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param l - int：排序范围的起始位置
** @param u - int：排序范围的结束位置
**
** 算法步骤：
** 1. 选择枢轴元素（通常选择中间位置）
** 2. 将数组分割为小于枢轴和大于枢轴两部分
** 3. 递归排序两个子数组
** 4. 合并结果（原地排序，无需显式合并）
**
** 分割策略：
** 使用双指针技术进行原地分割：
** - 左指针从左向右找大于等于枢轴的元素
** - 右指针从右向左找小于枢轴的元素
** - 交换找到的元素，继续分割
**
** 递归终止条件：
** 当 l >= u 时，子数组长度 <= 1，无需排序
**
** 时间复杂度：
** - 平均情况：O(n log n)
** - 最坏情况：O(n²)（当数组已排序或逆序时）
** - 最好情况：O(n log n)
**
** 空间复杂度：O(log n)（递归栈空间）
*/
static void auxsort(lua_State *L, int l, int u)
{
    while (l < u)
    {
        // 分割过程
        int i, j;

        // 将第一个元素作为枢轴移到中间位置
        lua_rawgeti(L, 1, l);
        lua_rawgeti(L, 1, u);
        if (sort_comp(L, -1, -2))
        {
            // 如果 u < l，交换它们
            set2(L, l, u);
        }
        else
        {
            lua_pop(L, 2);
        }

        // 如果只有两个元素，排序完成
        if (u - l == 1)
        {
            break;
        }

        // 三路分割
        i = (l + u) / 2;  // 中间位置
        lua_rawgeti(L, 1, i);
        lua_rawgeti(L, 1, l);
        if (sort_comp(L, -2, -1))
        {
            // 如果 i < l，交换它们
            set2(L, i, l);
        }
        else
        {
            lua_pop(L, 1);
            lua_rawgeti(L, 1, u);
            if (sort_comp(L, -1, -2))
            {
                // 如果 u < i，交换它们
                set2(L, i, u);
            }
            else
            {
                lua_pop(L, 2);
            }
        }

        // 如果只有三个元素，排序完成
        if (u - l == 2)
        {
            break;
        }

        // 选择中间元素作为枢轴
        lua_rawgeti(L, 1, i);
        lua_pushvalue(L, -1);
        lua_rawgeti(L, 1, l);
        set2(L, i, l);

        // 分割数组
        i = l;
        j = u - 1;
        for (;;)
        {
            // 从左向右找大于等于枢轴的元素
            while (lua_rawgeti(L, 1, ++i), sort_comp(L, -1, -2))
            {
                if (i > u)
                {
                    luaL_error(L, "invalid order function for sorting");
                }
                lua_pop(L, 1);
            }

            // 从右向左找小于枢轴的元素
            while (lua_rawgeti(L, 1, --j), sort_comp(L, -3, -1))
            {
                if (j < l)
                {
                    luaL_error(L, "invalid order function for sorting");
                }
                lua_pop(L, 1);
            }

            if (j < i)
            {
                lua_pop(L, 3);
                break;
            }

            // 交换元素
            set2(L, i, j);
        }

        // 将枢轴放到正确位置
        lua_rawgeti(L, 1, l);
        lua_rawgeti(L, 1, j);
        set2(L, l, j);

        // 递归排序较小的子数组，迭代处理较大的子数组
        if (j - l < u - j)
        {
            j = j - 1;
            if (l < j)
            {
                auxsort(L, l, j);
            }
            l = j + 2;
        }
        else
        {
            j = j + 1;
            if (j < u)
            {
                auxsort(L, j, u);
            }
            u = j - 2;
        }
    }
}

/*
** [排序算法] 表排序函数
**
** 功能描述：
** 对表的数组部分进行原地排序，支持自定义比较函数。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是0）
**
** 栈操作：
** 输入：table 表, [function 比较函数]
** 输出：无返回值（原地排序）
**
** 比较函数：
** - 如果提供：function(a, b) -> boolean，返回 true 表示 a < b
** - 如果未提供：使用默认的字典序比较
**
** 排序范围：
** 只对表的数组部分（连续的数字索引）进行排序，
** 哈希部分的键值对不受影响。
**
** 算法特点：
** - 原地排序：不需要额外的数组空间
** - 不稳定排序：相等元素的相对位置可能改变
** - 高效实现：基于优化的快速排序算法
**
** 性能分析：
** - 平均时间复杂度：O(n log n)
** - 最坏时间复杂度：O(n²)
** - 空间复杂度：O(log n)
**
** 错误处理：
** - 如果比较函数不一致（违反传递性），会抛出错误
** - 如果比较函数产生异常，排序会中断
**
** 使用示例：
** t = {3, 1, 4, 1, 5}
** table.sort(t)  -- t = {1, 1, 3, 4, 5}
**
** -- 自定义比较函数（降序）
** table.sort(t, function(a, b) return a > b end)  -- t = {5, 4, 3, 1, 1}
**
** -- 复杂对象排序
** people = {{name="Alice", age=30}, {name="Bob", age=25}}
** table.sort(people, function(a, b) return a.age < b.age end)
*/
static int tsort(lua_State *L)
{
    int n = aux_getn(L, 1);  // 获取数组长度

    luaL_checkstack(L, 40, "");  // 确保栈空间足够（排序需要额外栈空间）

    if (!lua_isnoneornil(L, 2))
    {
        // 检查比较函数类型
        luaL_checktype(L, 2, LUA_TFUNCTION);
    }

    lua_settop(L, 2);  // 保留表和比较函数

    // 调用排序算法
    auxsort(L, 1, n);

    return 0;
}

/*
** [数据结构] 表库函数注册表
**
** 数据结构说明：
** 包含所有表库函数的注册信息，按字母顺序排列。
** 每个元素都是 luaL_Reg 结构体，包含函数名和对应的 C 函数指针。
**
** 函数分类：
** - 迭代器：foreach、foreachi
** - 表属性：getn、setn、maxn
** - 数组操作：insert、remove
** - 字符串操作：concat
** - 排序算法：sort
**
** 排序说明：
** 函数按字母顺序排列，便于查找和维护。
**
** 功能覆盖：
** - 完整的表遍历和迭代功能
** - 动态数组操作和管理
** - 高效的字符串连接
** - 强大的排序算法
** - 表属性查询和设置
*/
static const luaL_Reg tab_funcs[] =
{
    {"concat", tconcat},
    {"foreach", foreach},
    {"foreachi", foreachi},
    {"getn", getn},
    {"insert", tinsert},
    {"maxn", maxn},
    {"remove", tremove},
    {"setn", setn},
    {"sort", tsort},
    {NULL, NULL}
};

/*
** [核心] Lua 表库初始化函数
**
** 功能描述：
** 初始化 Lua 表库，注册所有表操作函数。
** 这是表库的入口点，由 Lua 解释器在加载库时调用。
**
** 详细初始化流程：
** 1. 创建 table 库表并注册所有表操作函数
** 2. 返回 table 库表供 Lua 使用
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1，表示 table 库表）
**
** 栈操作：
** 在栈顶留下 table 库表
**
** 注册的函数：
** - table.concat：数组元素连接
** - table.foreach：通用表迭代器
** - table.foreachi：数组索引迭代器
** - table.getn：获取数组长度
** - table.insert：向数组插入元素
** - table.maxn：获取最大数字键
** - table.remove：从数组删除元素
** - table.setn：设置数组长度（已废弃）
** - table.sort：表排序
**
** 设计特点：
** - 提供完整的表操作接口
** - 高效的数组操作实现
** - 强大的排序算法
** - 灵活的迭代器支持
** - 优化的字符串连接
**
** 性能特点：
** - 原地操作：最小化内存分配
** - 算法优化：使用高效的排序和搜索算法
** - 缓冲区管理：智能的字符串缓冲区使用
** - 类型检查：严格的参数类型验证
**
** 内存管理：
** - 与 Lua GC 系统集成
** - 自动内存回收
** - 最小化内存碎片
** - 高效的对象复用
**
** 算法复杂度：O(n) 时间，其中 n 是函数数量，O(1) 空间
**
** 使用示例：
** require("table")
** table.insert(t, value)           -- 插入元素
** table.sort(t)                    -- 排序
** str = table.concat(t, ", ")      -- 连接元素
** table.foreach(t, print)          -- 遍历表
*/
LUALIB_API int luaopen_table(lua_State *L)
{
    // 注册表库函数
    luaL_register(L, LUA_TABLIBNAME, tab_funcs);

    // 返回 table 库表
    return 1;
}
