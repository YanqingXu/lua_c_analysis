/**
 * @file ltablib.c
 * @brief Lua表操作库：动态数组和表的高级操作功能
 *
 * 版权信息：
 * $Id: ltablib.c,v 1.38.1.3 2008/02/14 16:46:58 roberto Exp $
 * 表操作库
 * 版权声明见lua.h文件
 *
 * 程序概述：
 * 本文件实现了Lua的表操作库，提供了对表和数组的高级操作
 * 功能。它是Lua数据结构操作的核心组件，支持表的插入、
 * 删除、排序、连接、遍历等复杂操作。
 *
 * 主要功能：
 * 1. 表遍历操作（foreach, foreachi）
 * 2. 表大小管理（getn, setn, maxn）
 * 3. 表元素操作（insert, remove）
 * 4. 表连接操作（concat）
 * 5. 表排序算法（sort - 快速排序实现）
 * 6. 表结构分析和统计
 * 7. 高效的数组操作
 * 8. 自定义比较函数支持
 *
 * 设计特点：
 * 1. 高效算法：基于优化的快速排序和高效遍历
 * 2. 灵活接口：支持多种调用模式和参数组合
 * 3. 内存优化：最小化内存分配和数据移动
 * 4. 错误处理：完善的参数检查和边界条件处理
 * 5. 兼容性：支持不同版本的Lua表操作约定
 *
 * 数据结构技术：
 * - 动态数组的高效操作
 * - 表遍历的迭代器模式
 * - 快速排序的尾递归优化
 * - 字符串缓冲区的高效连接
 * - 回调函数的安全调用机制
 *
 * 应用场景：
 * - 数据处理和分析
 * - 算法实现和优化
 * - 配置文件处理
 * - 数据结构操作
 * - 字符串处理和格式化
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2008
 *
 * @note 这是Lua标准库的表操作实现
 * @see lua.h, lauxlib.h, lualib.h
 */

#include <stddef.h>

#define ltablib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/**
 * @defgroup TableUtilities 表操作工具宏
 * @brief 表操作的基础工具和辅助宏
 *
 * 表操作工具提供了统一的表类型检查和
 * 大小获取功能。
 * @{
 */

/**
 * @brief 获取表的数组部分长度
 *
 * 检查参数是否为表类型，并返回其数组部分的长度。
 * 这是一个内部工具宏，用于统一表长度获取操作。
 *
 * @param L Lua状态机指针
 * @param n 参数索引
 * @return 表的数组部分长度
 *
 * @note 组合了类型检查和长度获取
 * @note 如果参数不是表类型会抛出错误
 *
 * 实现机制：
 * - 首先检查参数类型必须是表
 * - 然后调用luaL_getn获取数组长度
 * - 使用逗号运算符组合两个操作
 * - 提供统一的错误处理
 */
#define aux_getn(L,n)	(luaL_checktype(L, n, LUA_TTABLE), luaL_getn(L, n))

/** @} */ /* 结束表操作工具宏文档组 */

/**
 * @defgroup TableTraversal 表遍历系统
 * @brief 表的遍历和迭代操作功能
 *
 * 表遍历系统提供了对表进行完整遍历的
 * 功能，支持数组部分和哈希部分的遍历。
 * @{
 */

/**
 * @brief 遍历表的数组部分
 *
 * 对表的数组部分（索引从1开始的连续整数键）进行遍历，
 * 对每个元素调用指定的函数。这是table.foreachi函数的实现。
 *
 * @param L Lua状态机指针
 * @return 如果回调函数返回非nil值则返回1，否则返回0
 *
 * @note 参数1：要遍历的表
 * @note 参数2：回调函数，接收(index, value)参数
 * @note 只遍历数组部分（连续的正整数索引）
 *
 * @see aux_getn, lua_call, lua_rawgeti
 *
 * 遍历机制：
 * 1. **长度获取**：
 *    - 使用aux_getn获取表的数组长度
 *    - 确保表参数类型正确
 *
 * 2. **函数验证**：
 *    - 检查第二个参数必须是函数
 *    - 确保回调函数可调用
 *
 * 3. **遍历循环**：
 *    - 从索引1开始到数组长度n
 *    - 为每个索引调用回调函数
 *    - 传递索引和对应的值作为参数
 *
 * 4. **回调处理**：
 *    - 推送回调函数到栈
 *    - 推送当前索引作为第一个参数
 *    - 推送当前值作为第二个参数
 *    - 调用函数并检查返回值
 *
 * 5. **终止条件**：
 *    - 如果回调函数返回非nil值，立即停止遍历
 *    - 返回该非nil值给调用者
 *    - 否则继续遍历直到结束
 *
 * 使用示例：
 * ```lua
 * local t = {10, 20, 30, 40}
 * table.foreachi(t, function(i, v)
 *     print("Index:", i, "Value:", v)
 *     if v == 30 then return "found" end
 * end)
 * ```
 *
 * 性能特点：
 * - 线性时间复杂度O(n)
 * - 按索引顺序访问，缓存友好
 * - 支持早期终止优化
 * - 最小化栈操作开销
 *
 * 应用场景：
 * - 数组元素的顺序处理
 * - 数据验证和搜索
 * - 统计和聚合操作
 * - 条件过滤和转换
 */
static int foreachi (lua_State *L) {
    int i;
    int n = aux_getn(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    for (i=1; i <= n; i++) {
        lua_pushvalue(L, 2);  /* 函数 */
        lua_pushinteger(L, i);  /* 第1个参数 */
        lua_rawgeti(L, 1, i);  /* 第2个参数 */
        lua_call(L, 2, 1);
        if (!lua_isnil(L, -1))
            return 1;
        lua_pop(L, 1);  /* 移除nil结果 */
    }
    return 0;
}

/**
 * @brief 遍历表的所有键值对
 *
 * 对表的所有键值对进行遍历，包括数组部分和哈希部分，
 * 对每个键值对调用指定的函数。这是table.foreach函数的实现。
 *
 * @param L Lua状态机指针
 * @return 如果回调函数返回非nil值则返回1，否则返回0
 *
 * @note 参数1：要遍历的表
 * @note 参数2：回调函数，接收(key, value)参数
 * @note 遍历所有键值对，包括非整数键
 *
 * @see lua_next, lua_call, lua_pushvalue
 *
 * 遍历机制：
 * 1. **参数验证**：
 *    - 检查第一个参数必须是表
 *    - 检查第二个参数必须是函数
 *
 * 2. **遍历初始化**：
 *    - 推送nil作为第一个键
 *    - 开始lua_next遍历循环
 *
 * 3. **遍历循环**：
 *    - 使用lua_next获取下一个键值对
 *    - 如果没有更多键值对则结束
 *    - 否则处理当前键值对
 *
 * 4. **回调处理**：
 *    - 推送回调函数到栈
 *    - 推送当前键作为第一个参数
 *    - 推送当前值作为第二个参数
 *    - 调用函数并检查返回值
 *
 * 5. **栈管理**：
 *    - 保持键在栈上供lua_next使用
 *    - 移除值和函数返回值
 *    - 维护正确的栈状态
 *
 * 6. **终止条件**：
 *    - 如果回调函数返回非nil值，立即停止
 *    - 返回该非nil值给调用者
 *    - 否则继续遍历所有键值对
 *
 * 使用示例：
 * ```lua
 * local t = {a=1, b=2, [10]=3, [20]=4}
 * table.foreach(t, function(k, v)
 *     print("Key:", k, "Value:", v)
 *     if k == "b" then return "found b" end
 * end)
 * ```
 *
 * 遍历特点：
 * - 遍历顺序不确定（依赖内部实现）
 * - 包含所有类型的键
 * - 支持早期终止
 * - 安全的迭代器实现
 *
 * 性能考虑：
 * - 时间复杂度O(n)，n为键值对数量
 * - 内存使用最小化
 * - 避免创建临时表
 * - 高效的栈操作
 *
 * 应用场景：
 * - 表的完整遍历和处理
 * - 键值对的搜索和过滤
 * - 数据转换和映射
 * - 表结构分析
 */
static int foreach (lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushnil(L);  /* 第一个键 */
    while (lua_next(L, 1)) {
        lua_pushvalue(L, 2);  /* 函数 */
        lua_pushvalue(L, -3);  /* 键 */
        lua_pushvalue(L, -3);  /* 值 */
        lua_call(L, 2, 1);
        if (!lua_isnil(L, -1))
            return 1;
        lua_pop(L, 2);  /* 移除值和结果 */
    }
    return 0;
}

/** @} */ /* 结束表遍历系统文档组 */

/**
 * @defgroup TableSizeOperations 表大小操作
 * @brief 表大小的获取、设置和分析功能
 *
 * 表大小操作提供了对表大小的各种操作，
 * 包括数组长度和最大数值键的处理。
 * @{
 */

/**
 * @brief 获取表中最大的数值键
 *
 * 遍历表的所有键，找出其中最大的数值键。
 * 这是table.maxn函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（最大数值键）
 *
 * @note 参数1：要分析的表
 * @note 返回最大的数值键，如果没有数值键则返回0
 * @note 只考虑数值类型的键
 *
 * @see lua_next, lua_tonumber, lua_type
 *
 * 算法实现：
 * 1. **初始化**：
 *    - 设置最大值为0
 *    - 验证参数为表类型
 *
 * 2. **遍历所有键**：
 *    - 使用lua_next遍历表
 *    - 检查每个键的类型
 *    - 只处理数值类型的键
 *
 * 3. **比较更新**：
 *    - 将数值键转换为lua_Number
 *    - 与当前最大值比较
 *    - 更新最大值记录
 *
 * 4. **栈管理**：
 *    - 移除值，保留键供下次迭代
 *    - 维护正确的遍历状态
 *
 * 5. **结果返回**：
 *    - 推送最大数值键到栈
 *    - 如果没有数值键则返回0
 *
 * 使用示例：
 * ```lua
 * local t = {[1]=10, [5]=20, [3.5]=30, a="hello"}
 * print(table.maxn(t))  -- 5
 *
 * local t2 = {a=1, b=2}
 * print(table.maxn(t2)) -- 0 (没有数值键)
 * ```
 *
 * 特殊情况：
 * - 空表返回0
 * - 只有非数值键的表返回0
 * - 负数键也参与比较
 * - 浮点数键正常处理
 *
 * 性能特点：
 * - 时间复杂度O(n)，n为键的总数
 * - 空间复杂度O(1)
 * - 单次遍历算法
 * - 最小化类型转换开销
 *
 * 应用场景：
 * - 稀疏数组的边界确定
 * - 表结构分析
 * - 动态数组的大小估算
 * - 数据完整性检查
 */
static int maxn (lua_State *L) {
    lua_Number max = 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);  /* 第一个键 */
    while (lua_next(L, 1)) {
        lua_pop(L, 1);  /* 移除值 */
        if (lua_type(L, -1) == LUA_TNUMBER) {
            lua_Number v = lua_tonumber(L, -1);
            if (v > max) max = v;
        }
    }
    lua_pushnumber(L, max);
    return 1;
}

/**
 * @brief 获取表的数组长度
 *
 * 获取表的数组部分长度。这是table.getn函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（数组长度）
 *
 * @note 参数1：要获取长度的表
 * @note 返回表的数组部分长度
 *
 * @see aux_getn, luaL_getn
 *
 * 实现说明：
 * - 直接使用aux_getn宏获取长度
 * - 包含类型检查和长度获取
 * - 返回连续整数索引的最大值
 *
 * 使用示例：
 * ```lua
 * local t = {10, 20, 30}
 * print(table.getn(t))  -- 3
 * ```
 */
static int getn (lua_State *L) {
    lua_pushinteger(L, aux_getn(L, 1));
    return 1;
}

/**
 * @brief 设置表的数组长度
 *
 * 设置表的数组部分长度。这是table.setn函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（原表）
 *
 * @note 参数1：要设置长度的表
 * @note 参数2：新的长度值
 * @note 在新版本中此函数已过时
 *
 * @see luaL_setn, luaL_checkint
 *
 * 兼容性处理：
 * - 如果luaL_setn可用则设置长度
 * - 否则抛出"过时"错误
 * - 返回原表以支持链式调用
 *
 * 使用示例：
 * ```lua
 * local t = {10, 20, 30}
 * table.setn(t, 5)  -- 在支持的版本中设置长度为5
 * ```
 *
 * 注意事项：
 * - 此函数在新版本Lua中已废弃
 * - 建议使用#操作符获取长度
 * - 仅为向后兼容性保留
 */
static int setn (lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
#ifndef luaL_setn
    luaL_setn(L, 1, luaL_checkint(L, 2));
#else
    luaL_error(L, LUA_QL("setn") " is obsolete");
#endif
    lua_pushvalue(L, 1);
    return 1;
}

/** @} */ /* 结束表大小操作文档组 */

/**
 * @defgroup TableModification 表修改操作
 * @brief 表元素的插入、删除和修改功能
 *
 * 表修改操作提供了对表进行动态修改的
 * 功能，包括元素的插入和删除。
 * @{
 */

/**
 * @brief 向表中插入元素
 *
 * 在表的指定位置插入新元素，支持在末尾插入或在指定位置插入。
 * 这是table.insert函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回0（无返回值）
 *
 * @note 支持两种调用模式：
 * @note 模式1：table.insert(t, value) - 在末尾插入
 * @note 模式2：table.insert(t, pos, value) - 在指定位置插入
 *
 * @see aux_getn, lua_rawgeti, lua_rawseti, luaL_setn
 *
 * 插入算法：
 * 1. **参数分析**：
 *    - 获取表的当前长度
 *    - 根据参数数量确定插入模式
 *    - 计算插入位置和新长度
 *
 * 2. **末尾插入模式**（2个参数）：
 *    - 插入位置为当前长度+1
 *    - 直接在末尾添加新元素
 *    - 无需移动其他元素
 *
 * 3. **指定位置插入模式**（3个参数）：
 *    - 获取指定的插入位置
 *    - 检查位置的有效性
 *    - 移动后续元素为新元素腾出空间
 *
 * 4. **元素移动**：
 *    - 从末尾开始向后移动元素
 *    - 使用lua_rawgeti和lua_rawseti操作
 *    - 确保不覆盖未处理的元素
 *
 * 5. **插入完成**：
 *    - 在目标位置设置新元素
 *    - 更新表的长度信息
 *    - 维护数组的连续性
 *
 * 时间复杂度：
 * - 末尾插入：O(1)
 * - 中间插入：O(n)，n为需要移动的元素数量
 *
 * 使用示例：
 * ```lua
 * local t = {10, 20, 30}
 * table.insert(t, 40)      -- {10, 20, 30, 40}
 * table.insert(t, 2, 15)   -- {10, 15, 20, 30, 40}
 * ```
 *
 * 边界处理：
 * - 插入位置超出当前长度时自动扩展
 * - 支持在空表中插入第一个元素
 * - 正确处理表长度的更新
 *
 * 性能优化：
 * - 末尾插入避免元素移动
 * - 使用原始访问函数提高效率
 * - 最小化栈操作次数
 *
 * 应用场景：
 * - 动态数组的构建
 * - 列表数据的插入操作
 * - 有序数据的维护
 * - 数据结构的动态调整
 */
static int tinsert (lua_State *L) {
    int e = aux_getn(L, 1) + 1;  /* 第一个空元素 */
    int pos;  /* 插入新元素的位置 */
    switch (lua_gettop(L)) {
        case 2: {  /* 只有2个参数调用 */
            pos = e;  /* 在末尾插入新元素 */
            break;
        }
        case 3: {
            int i;
            pos = luaL_checkint(L, 2);  /* 第2个参数是位置 */
            if (pos > e) e = pos;  /* 必要时'增长'数组 */
            for (i = e; i > pos; i--) {  /* 向上移动元素 */
                lua_rawgeti(L, 1, i-1);
                lua_rawseti(L, 1, i);  /* t[i] = t[i-1] */
            }
            break;
        }
        default: {
            return luaL_error(L, "wrong number of arguments to " LUA_QL("insert"));
        }
    }
    luaL_setn(L, 1, e);  /* 新大小 */
    lua_rawseti(L, 1, pos);  /* t[pos] = v */
    return 0;
}

/** @} */ /* 结束表修改操作文档组 */

/**
 * @brief 从表中删除元素
 *
 * 从表的指定位置删除元素，支持从末尾删除或从指定位置删除。
 * 这是table.remove函数的实现。
 *
 * @param L Lua状态机指针
 * @return 如果成功删除则返回1（被删除的元素），否则返回0
 *
 * @note 支持两种调用模式：
 * @note 模式1：table.remove(t) - 删除末尾元素
 * @note 模式2：table.remove(t, pos) - 删除指定位置元素
 *
 * @see aux_getn, lua_rawgeti, lua_rawseti, luaL_setn
 *
 * 删除算法：
 * 1. **参数分析**：
 *    - 获取表的当前长度
 *    - 确定删除位置（默认为末尾）
 *    - 检查位置的有效性
 *
 * 2. **边界检查**：
 *    - 验证删除位置在有效范围内
 *    - 如果位置无效则直接返回
 *    - 确保不会访问越界元素
 *
 * 3. **元素保存**：
 *    - 获取要删除的元素作为返回值
 *    - 将其保留在栈上供返回
 *
 * 4. **元素移动**：
 *    - 将删除位置后的所有元素向前移动
 *    - 使用lua_rawgeti和lua_rawseti操作
 *    - 覆盖被删除元素的位置
 *
 * 5. **清理和更新**：
 *    - 将原末尾位置设置为nil
 *    - 更新表的长度信息
 *    - 维护数组的连续性
 *
 * 时间复杂度：
 * - 末尾删除：O(1)
 * - 中间删除：O(n)，n为需要移动的元素数量
 *
 * 使用示例：
 * ```lua
 * local t = {10, 20, 30, 40}
 * local last = table.remove(t)     -- 删除40，返回40
 * local second = table.remove(t, 2) -- 删除20，返回20
 * -- t现在是{10, 30}
 * ```
 *
 * 边界处理：
 * - 空表删除操作返回nil
 * - 无效位置不执行删除操作
 * - 正确处理表长度的更新
 *
 * 性能特点：
 * - 末尾删除效率最高
 * - 使用原始访问函数提高效率
 * - 最小化不必要的栈操作
 *
 * 应用场景：
 * - 动态数组的元素删除
 * - 栈和队列的实现
 * - 列表数据的维护
 * - 数据结构的动态调整
 */
static int tremove (lua_State *L) {
    int e = aux_getn(L, 1);
    int pos = luaL_optint(L, 2, e);
    if (!(1 <= pos && pos <= e))  /* 位置超出边界？ */
        return 0;  /* 没有要删除的 */
    luaL_setn(L, 1, e - 1);  /* t.n = n-1 */
    lua_rawgeti(L, 1, pos);  /* result = t[pos] */
    for ( ;pos<e; pos++) {
        lua_rawgeti(L, 1, pos+1);
        lua_rawseti(L, 1, pos);  /* t[pos] = t[pos+1] */
    }
    lua_pushnil(L);
    lua_rawseti(L, 1, e);  /* t[e] = nil */
    return 1;
}

/** @} */ /* 结束表修改操作文档组 */

/**
 * @defgroup TableConcatenation 表连接操作
 * @brief 表元素的字符串连接和缓冲区管理
 *
 * 表连接操作提供了将表中的元素连接成
 * 字符串的高效功能。
 * @{
 */

/**
 * @brief 添加表字段到字符串缓冲区
 *
 * 从表中获取指定索引的元素，验证其为字符串类型，
 * 然后添加到字符串缓冲区中。
 *
 * @param L Lua状态机指针
 * @param b 字符串缓冲区指针
 * @param i 表中的索引
 *
 * @note 内部辅助函数，用于tconcat
 * @note 如果元素不是字符串类型会抛出错误
 *
 * @see lua_rawgeti, lua_isstring, luaL_addvalue
 *
 * 处理流程：
 * 1. **元素获取**：
 *    - 使用lua_rawgeti获取表中指定索引的元素
 *    - 将元素推入栈顶
 *
 * 2. **类型检查**：
 *    - 验证元素是否为字符串类型
 *    - 如果不是字符串则生成详细错误信息
 *    - 包含类型名称和索引位置
 *
 * 3. **缓冲区添加**：
 *    - 调用luaL_addvalue将字符串添加到缓冲区
 *    - 自动处理字符串长度和内存管理
 *
 * 错误处理：
 * - 提供详细的错误信息
 * - 包含无效值的类型和位置
 * - 帮助用户快速定位问题
 *
 * 性能优化：
 * - 直接使用原始访问避免元方法调用
 * - 高效的缓冲区操作
 * - 最小化类型转换开销
 */
static void addfield (lua_State *L, luaL_Buffer *b, int i) {
    lua_rawgeti(L, 1, i);
    if (!lua_isstring(L, -1))
        luaL_error(L, "invalid value (%s) at index %d in table for "
                      LUA_QL("concat"), luaL_typename(L, -1), i);
    luaL_addvalue(b);
}

/**
 * @brief 连接表中的字符串元素
 *
 * 将表中指定范围的元素连接成一个字符串，支持自定义分隔符。
 * 这是table.concat函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（连接后的字符串）
 *
 * @note 参数1：要连接的表
 * @note 参数2：分隔符字符串（可选，默认为空字符串）
 * @note 参数3：起始索引（可选，默认为1）
 * @note 参数4：结束索引（可选，默认为表长度）
 *
 * @see addfield, luaL_Buffer, luaL_optlstring
 *
 * 连接算法：
 * 1. **参数处理**：
 *    - 获取分隔符字符串和长度
 *    - 确定连接的起始和结束索引
 *    - 验证表参数类型
 *
 * 2. **缓冲区初始化**：
 *    - 创建字符串缓冲区
 *    - 准备高效的字符串构建
 *
 * 3. **元素连接**：
 *    - 遍历指定范围的索引
 *    - 添加每个元素到缓冲区
 *    - 在元素间插入分隔符
 *
 * 4. **最后元素处理**：
 *    - 特殊处理最后一个元素
 *    - 避免在末尾添加分隔符
 *    - 确保正确的字符串格式
 *
 * 5. **结果生成**：
 *    - 完成缓冲区构建
 *    - 推送最终字符串到栈
 *
 * 使用示例：
 * ```lua
 * local t = {"hello", "world", "lua"}
 * print(table.concat(t))        -- "helloworldlua"
 * print(table.concat(t, " "))   -- "hello world lua"
 * print(table.concat(t, "-", 2, 3)) -- "world-lua"
 * ```
 *
 * 性能特点：
 * - 使用高效的字符串缓冲区
 * - 避免多次字符串分配
 * - 线性时间复杂度O(n)
 * - 内存使用优化
 *
 * 边界处理：
 * - 空范围返回空字符串
 * - 单元素不添加分隔符
 * - 无效索引范围的处理
 *
 * 应用场景：
 * - 字符串数组的连接
 * - CSV格式数据生成
 * - 路径字符串构建
 * - 模板字符串处理
 *
 * 缓冲区优势：
 * - 减少内存分配次数
 * - 提高大量字符串连接的性能
 * - 自动处理内存管理
 * - 支持任意长度的结果字符串
 */
static int tconcat (lua_State *L) {
    luaL_Buffer b;
    size_t lsep;
    int i, last;
    const char *sep = luaL_optlstring(L, 2, "", &lsep);
    luaL_checktype(L, 1, LUA_TTABLE);
    i = luaL_optint(L, 3, 1);
    last = luaL_opt(L, luaL_checkint, 4, luaL_getn(L, 1));
    luaL_buffinit(L, &b);
    for (; i < last; i++) {
        addfield(L, &b, i);
        luaL_addlstring(&b, sep, lsep);
    }
    if (i == last)  /* 添加最后一个值（如果区间不为空） */
        addfield(L, &b, i);
    luaL_pushresult(&b);
    return 1;
}

/** @} */ /* 结束表连接操作文档组 */


/**
 * @defgroup QuickSortAlgorithm 快速排序算法实现
 * @brief 高效的表排序算法和比较函数支持
 *
 * 快速排序算法实现基于Robert Sedgewick的《Algorithms in MODULA-3》
 * (Addison-Wesley, 1993)，提供了高效的表排序功能。
 *
 * 算法特点：
 * - 平均时间复杂度：O(n log n)
 * - 最坏时间复杂度：O(n²)
 * - 空间复杂度：O(log n)（尾递归优化）
 * - 原地排序算法
 * - 支持自定义比较函数
 * @{
 */

/**
 * @brief 交换表中两个位置的元素
 *
 * 交换表中索引i和j位置的两个元素。这是排序算法的基础操作。
 *
 * @param L Lua状态机指针
 * @param i 第一个元素的索引
 * @param j 第二个元素的索引
 *
 * @note 假设栈上已有要交换的两个元素
 * @note 栈顶是j位置的元素，栈顶-1是i位置的元素
 *
 * @see lua_rawseti
 *
 * 交换机制：
 * 1. **栈状态假设**：
 *    - 栈顶：j位置的元素值
 *    - 栈顶-1：i位置的元素值
 *
 * 2. **交换操作**：
 *    - 将栈顶元素设置到表的i位置
 *    - 将栈顶-1元素设置到表的j位置
 *    - 完成两个元素的位置交换
 *
 * 3. **栈清理**：
 *    - lua_rawseti会自动弹出栈顶元素
 *    - 两次调用后栈恢复到调用前状态
 *
 * 使用场景：
 * - 快速排序的分区操作
 * - 元素位置的调整
 * - 排序算法的基础操作
 *
 * 性能特点：
 * - 常数时间复杂度O(1)
 * - 使用原始访问避免元方法
 * - 最小化栈操作开销
 */
static void set2 (lua_State *L, int i, int j) {
    lua_rawseti(L, 1, i);
    lua_rawseti(L, 1, j);
}

/**
 * @brief 比较两个元素的大小
 *
 * 比较栈上两个元素的大小关系，支持自定义比较函数。
 * 这是排序算法的核心比较操作。
 *
 * @param L Lua状态机指针
 * @param a 第一个元素在栈中的位置
 * @param b 第二个元素在栈中的位置
 * @return 如果a < b则返回真，否则返回假
 *
 * @note 参数2位置可能有自定义比较函数
 * @note 栈位置参数需要考虑函数调用的栈变化
 *
 * @see lua_call, lua_lessthan, lua_toboolean
 *
 * 比较机制：
 * 1. **比较函数检查**：
 *    - 检查参数2位置是否有比较函数
 *    - 如果没有则使用默认比较
 *
 * 2. **自定义比较**：
 *    - 推送比较函数到栈
 *    - 推送两个要比较的元素
 *    - 调用比较函数获取结果
 *    - 转换结果为布尔值
 *
 * 3. **默认比较**：
 *    - 使用lua_lessthan进行默认比较
 *    - 支持数值、字符串等类型
 *
 * 4. **栈位置调整**：
 *    - a-1：补偿函数推入栈的影响
 *    - b-2：补偿函数和第一个参数的影响
 *
 * 比较函数约定：
 * - 接收两个参数：(a, b)
 * - 返回真值表示a < b
 * - 返回假值表示a >= b
 * - 必须满足传递性和反对称性
 *
 * 使用示例：
 * ```lua
 * -- 默认比较（升序）
 * table.sort(t)
 *
 * -- 自定义比较（降序）
 * table.sort(t, function(a, b) return a > b end)
 *
 * -- 复杂对象比较
 * table.sort(people, function(a, b) return a.age < b.age end)
 * ```
 *
 * 错误处理：
 * - 比较函数必须返回一致的结果
 * - 无效的比较函数会导致排序错误
 * - 排序过程中会检测比较函数的有效性
 *
 * 性能考虑：
 * - 自定义比较函数有额外开销
 * - 默认比较使用优化的内置函数
 * - 比较函数调用次数影响整体性能
 */
static int sort_comp (lua_State *L, int a, int b) {
    if (!lua_isnil(L, 2)) {  /* 有比较函数？ */
        int res;
        lua_pushvalue(L, 2);
        lua_pushvalue(L, a-1);  /* -1补偿函数 */
        lua_pushvalue(L, b-2);  /* -2补偿函数和'a' */
        lua_call(L, 2, 1);
        res = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return res;
    }
    else  /* a < b? */
        return lua_lessthan(L, a, b);
}

static void auxsort (lua_State *L, int l, int u) {
  while (l < u) {  /* for tail recursion */
    int i, j;
    /* sort elements a[l], a[(l+u)/2] and a[u] */
    lua_rawgeti(L, 1, l);
    lua_rawgeti(L, 1, u);
    if (sort_comp(L, -1, -2))  /* a[u] < a[l]? */
      set2(L, l, u);  /* swap a[l] - a[u] */
    else
      lua_pop(L, 2);
    if (u-l == 1) break;  /* only 2 elements */
    i = (l+u)/2;
    lua_rawgeti(L, 1, i);
    lua_rawgeti(L, 1, l);
    if (sort_comp(L, -2, -1))  /* a[i]<a[l]? */
      set2(L, i, l);
    else {
      lua_pop(L, 1);  /* remove a[l] */
      lua_rawgeti(L, 1, u);
      if (sort_comp(L, -1, -2))  /* a[u]<a[i]? */
        set2(L, i, u);
      else
        lua_pop(L, 2);
    }
    if (u-l == 2) break;  /* only 3 elements */
    lua_rawgeti(L, 1, i);  /* Pivot */
    lua_pushvalue(L, -1);
    lua_rawgeti(L, 1, u-1);
    set2(L, i, u-1);
    /* a[l] <= P == a[u-1] <= a[u], only need to sort from l+1 to u-2 */
    i = l; j = u-1;
    for (;;) {  /* invariant: a[l..i] <= P <= a[j..u] */
      /* repeat ++i until a[i] >= P */
      while (lua_rawgeti(L, 1, ++i), sort_comp(L, -1, -2)) {
        if (i>u) luaL_error(L, "invalid order function for sorting");
        lua_pop(L, 1);  /* remove a[i] */
      }
      /* repeat --j until a[j] <= P */
      while (lua_rawgeti(L, 1, --j), sort_comp(L, -3, -1)) {
        if (j<l) luaL_error(L, "invalid order function for sorting");
        lua_pop(L, 1);  /* remove a[j] */
      }
      if (j<i) {
        lua_pop(L, 3);  /* pop pivot, a[i], a[j] */
        break;
      }
      set2(L, i, j);
    }
    lua_rawgeti(L, 1, u-1);
    lua_rawgeti(L, 1, i);
    set2(L, u-1, i);  /* swap pivot (a[u-1]) with a[i] */
    /* a[l..i-1] <= a[i] == P <= a[i+1..u] */
    /* adjust so that smaller half is in [j..i] and larger one in [l..u] */
    if (i-l < u-i) {
      j=l; i=i-1; l=i+2;
    }
    else {
      j=i+1; i=u; u=j-2;
    }
    auxsort(L, j, i);  /* call recursively the smaller one */
  }  /* repeat the routine for the larger one */
}

static int sort (lua_State *L) {
  int n = aux_getn(L, 1);
  luaL_checkstack(L, 40, "");  /* assume array is smaller than 2^40 */
  if (!lua_isnoneornil(L, 2))  /* is there a 2nd argument? */
    luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2);  /* make sure there is two arguments */
  auxsort(L, 1, n);
  return 0;
}

/* }====================================================== */


static const luaL_Reg tab_funcs[] = {
  {"concat", tconcat},
  {"foreach", foreach},
  {"foreachi", foreachi},
  {"getn", getn},
  {"maxn", maxn},
  {"insert", tinsert},
  {"remove", tremove},
  {"setn", setn},
  {"sort", sort},
  {NULL, NULL}
};


LUALIB_API int luaopen_table (lua_State *L) {
  luaL_register(L, LUA_TABLIBNAME, tab_funcs);
  return 1;
}

