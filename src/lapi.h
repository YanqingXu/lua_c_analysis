/**
 * @file lapi.h
 * @brief Lua API辅助函数头文件：提供Lua C API的内部辅助函数声明
 * 
 * 详细说明：
 * 本文件定义了Lua C API的内部辅助函数，这些函数是Lua解释器内部使用的
 * 核心API组件，主要负责Lua值在C和Lua之间的转换和传递。这些函数不直接
 * 暴露给最终用户，而是作为Lua解释器内部实现的基础设施。
 * 
 * 系统架构定位：
 * 在Lua解释器架构中，本文件位于C API层，负责：
 * - Lua虚拟机栈操作的底层实现
 * - Lua值类型的安全转换和传递
 * - C代码与Lua虚拟机的接口桥梁
 * - 内存安全和类型安全的保障
 * 
 * 技术特点：
 * - 使用C89/C99标准，确保最大兼容性
 * - 实现Lua虚拟机的栈操作机制
 * - 提供类型安全的值传递接口
 * - 优化的内存管理和性能考虑
 * - 支持Lua的动态类型系统
 * 
 * 依赖关系：
 * - lobject.h: Lua对象系统和类型定义
 * - lua.h: Lua核心API和状态机定义
 * - 标准C库: 基础类型和函数支持
 * 
 * Lua虚拟机栈模型：
 * Lua使用一个虚拟栈来在C和Lua之间传递值。栈的工作原理：
 * 1. 栈底(index 1)存储函数的第一个参数
 * 2. 栈顶存储最新推入的值
 * 3. 负索引从栈顶开始计算(-1是栈顶)
 * 4. 所有C API操作都通过栈进行
 * 
 * 使用模式：
 * @code
 * // 典型的栈操作流程
 * lua_State *L = luaL_newstate();
 * 
 * // 推入一个数字到栈顶
 * lua_pushnumber(L, 42.0);
 * 
 * // 推入一个字符串到栈顶
 * lua_pushstring(L, "Hello, Lua!");
 * 
 * // 获取栈顶元素的值
 * if (lua_isnumber(L, -1)) {
 *     double value = lua_tonumber(L, -1);
 *     printf("栈顶的数字: %f\n", value);
 * }
 * 
 * // 弹出栈顶元素
 * lua_pop(L, 1);
 * 
 * lua_close(L);
 * @endcode
 * 
 * 内存安全考虑：
 * - 所有栈操作都进行边界检查
 * - 自动垃圾回收管理Lua对象生命周期
 * - 防止栈溢出和下溢
 * - C指针与Lua引用的安全转换
 * 
 * 性能特征：
 * - 栈操作的时间复杂度为O(1)
 * - 最小化内存分配和复制
 * - 优化的类型检查和转换
 * - 支持增量垃圾回收
 * 
 * 线程安全性：
 * - 每个lua_State是独立的，不共享状态
 * - 多个lua_State可以在不同线程中安全使用
 * - 单个lua_State不是线程安全的
 * - 需要外部同步机制保护并发访问
 * 
 * 注意事项：
 * - 这些是内部API，不保证版本间的兼容性
 * - 直接使用这些函数需要深入理解Lua内部机制
 * - 不当使用可能导致虚拟机状态不一致
 * - 建议使用公开的Lua C API而非这些内部函数
 * 
 * @author Roberto Ierusalimschy (Lua团队)
 * @version 5.1.5
 * @date 2007年12月27日
 * @since Lua 5.0
 * @see lua.h, lobject.h, lstate.h
 */

#ifndef lapi_h
#define lapi_h

#include "lobject.h"

/**
 * @brief 将Lua值对象推送到虚拟机栈顶
 * 
 * 详细说明：
 * 这是Lua C API的核心函数之一，负责将一个TValue类型的Lua值对象
 * 推送到指定Lua状态机的虚拟栈顶。这个函数是Lua解释器内部值传递
 * 机制的基础，确保了C代码和Lua代码之间的安全数据交换。
 * 
 * 实现原理：
 * 1. 检查虚拟栈的剩余空间，确保可以安全推入新值
 * 2. 将TValue对象的内容复制到栈顶位置
 * 3. 更新栈顶指针，使其指向新的栈顶位置
 * 4. 如果值包含GC对象，则更新垃圾回收器的引用计数
 * 
 * 栈操作机制：
 * Lua虚拟栈是一个动态数组，每个元素都是TValue结构体。
 * 栈的增长方向是向上的，即索引递增的方向。
 * 
 * TValue类型系统：
 * TValue是Lua的通用值类型，可以表示：
 * - LUA_TNUMBER: 数字类型(double)
 * - LUA_TSTRING: 字符串类型
 * - LUA_TTABLE: 表类型
 * - LUA_TFUNCTION: 函数类型
 * - LUA_TUSERDATA: 用户数据类型
 * - LUA_TTHREAD: 协程类型
 * - LUA_TNIL: 空值类型
 * - LUA_TBOOLEAN: 布尔类型
 * 
 * 垃圾回收考虑：
 * 当推入的值是GC对象时，需要：
 * 1. 在栈上创建对该对象的新引用
 * 2. 确保对象在栈上存在期间不会被回收
 * 3. 维护正确的引用计数和可达性信息
 * 
 * 使用示例：
 * @code
 * // 内部使用示例（仅供参考，不建议直接调用）
 * void push_lua_number(lua_State *L, double value) {
 *     TValue tv;
 *     setnvalue(&tv, value);  // 设置TValue为数字类型
 *     luaA_pushobject(L, &tv);  // 推入栈顶
 * }
 * 
 * void push_lua_string(lua_State *L, TString *str) {
 *     TValue tv;
 *     setsvalue(L, &tv, str);  // 设置TValue为字符串类型
 *     luaA_pushobject(L, &tv);  // 推入栈顶
 * }
 * @endcode
 * 
 * 错误处理：
 * 函数会检查以下错误情况：
 * - 栈空间不足：自动扩展栈或抛出内存错误
 * - 无效的TValue对象：可能导致运行时错误
 * - 虚拟机状态异常：可能导致不可预测的行为
 * 
 * 性能优化：
 * - 使用内联函数减少函数调用开销
 * - 直接内存复制避免不必要的转换
 * - 延迟垃圾回收标记提高吞吐量
 * - 栈缓存机制减少内存分配
 * 
 * 线程安全性：
 * - 函数不是线程安全的
 * - 必须在持有lua_State锁的情况下调用
 * - 不能在多个线程间共享同一个lua_State
 * 
 * 栈状态变化：
 * 调用前: [... | top]
 * 调用后: [... | value | top]
 * 栈大小: +1
 * 
 * 最佳实践：
 * - 推荐使用公开API如lua_pushvalue()而非直接调用此函数
 * - 在推入大量值时要注意栈溢出检查
 * - 及时弹出不需要的值以节省栈空间
 * - 使用lua_checkstack()预先检查栈空间
 * 
 * 相关函数：
 * - lua_pushvalue(): 公开API，推入栈上已有值的副本
 * - lua_pushnil(): 推入nil值
 * - lua_pushnumber(): 推入数字值
 * - lua_pushstring(): 推入字符串值
 * 
 * @param[in] L Lua状态机指针，不能为NULL
 *              必须是有效的、已初始化的Lua状态机
 *              状态机必须有足够的栈空间容纳新值
 * @param[in] o 要推入的TValue对象指针，不能为NULL
 *              必须是有效的、已正确初始化的TValue对象
 *              对象的类型标签必须与值内容匹配
 * 
 * @pre L != NULL && o != NULL
 * @pre L是有效的lua_State对象
 * @pre o指向有效的TValue对象
 * @pre 栈有足够空间容纳新值（或可以自动扩展）
 * 
 * @post 栈顶包含o的副本
 * @post 栈大小增加1
 * @post 如果o包含GC对象，则引用计数正确维护
 * 
 * @note 这是内部API，不保证版本间兼容性
 * @note 直接使用此函数需要深入理解Lua内部机制
 * @note 不当使用可能导致虚拟机状态不一致
 * 
 * @warning 函数不进行参数有效性检查，调用者必须确保参数正确
 * @warning 在多线程环境中使用需要适当的同步机制
 * @warning 栈溢出时的行为依赖于具体实现
 * 
 * @since Lua 5.0
 * @see lua_pushvalue(), setobj(), luaD_checkstack()
 */
LUAI_FUNC void luaA_pushobject(lua_State *L, const TValue *o);

#endif
