/**
 * @file lauxlib.h
 * @brief Lua 5.1.5 辅助库API头文件：提供高级便利函数和实用工具
 *
 * 详细说明：
 * 本文件是Lua辅助库的核心头文件，提供了大量高级、便利的API函数，
 * 简化了常见的Lua编程任务。辅助库建立在基础Lua C API之上，提供
 * 更加用户友好和安全的接口，是开发Lua扩展和应用程序的重要工具。
 *
 * 系统架构定位：
 * - 位于基础Lua C API之上的便利层
 * - 提供参数检查、错误处理、类型转换等常用功能
 * - 简化库注册、元表管理、缓冲区操作等复杂任务
 * - 为标准库和第三方扩展提供统一的开发框架
 *
 * 主要功能模块：
 * 1. 参数检查和验证：luaL_check*系列函数
 * 2. 错误处理和报告：luaL_error、luaL_argerror等
 * 3. 库注册和管理：luaL_register、luaL_openlib等
 * 4. 元表操作：luaL_newmetatable、luaL_getmetafield等
 * 5. 代码加载：luaL_loadfile、luaL_loadstring等
 * 6. 字符串缓冲区：luaL_Buffer及相关操作
 * 7. 引用系统：luaL_ref、luaL_unref等
 * 8. 便利宏：简化常见操作的宏定义
 *
 * 设计理念：
 * - 安全性：提供参数检查和错误处理
 * - 便利性：简化复杂操作，减少样板代码
 * - 一致性：统一的命名规范和使用模式
 * - 扩展性：支持自定义类型和操作
 *
 * 使用优势：
 * - 减少错误：自动参数检查和类型验证
 * - 提高效率：预定义的常用操作模式
 * - 简化开发：高级抽象隐藏底层复杂性
 * - 增强可读性：语义化的函数名称
 *
 * 典型使用场景：
 * - 开发Lua C扩展模块
 * - 嵌入Lua到C/C++应用程序
 * - 实现自定义数据类型
 * - 构建领域特定语言(DSL)
 *
 * 性能特征：
 * - 函数调用开销：比直接API稍高，但提供更多安全检查
 * - 内存使用：合理的内存分配策略
 * - 错误处理：统一的错误报告机制
 * - 缓冲区管理：高效的字符串构建
 *
 * 兼容性考虑：
 * - 向后兼容：支持旧版本API的兼容性宏
 * - 平台无关：纯C实现，跨平台支持
 * - 编译器支持：兼容主流C编译器
 *
 * 最佳实践：
 * - 优先使用辅助库函数而非直接API
 * - 合理使用参数检查函数确保类型安全
 * - 利用错误处理机制提供友好的错误信息
 * - 使用缓冲区API进行高效的字符串操作
 *
 * 注意事项：
 * - 某些函数会抛出Lua错误，需要在保护模式下调用
 * - 参数检查函数在失败时不会返回
 * - 缓冲区操作需要正确的初始化和清理
 * - 引用系统需要配对使用ref/unref
 *
 * @author Roberto Ierusalimschy, Luiz Henrique de Figueiredo, Waldemar Celes
 * @version 5.1.5
 * @date 2012
 * @since C89
 * @see lua.h, lualib.h
 */

#ifndef lauxlib_h
#define lauxlib_h

#include <stddef.h>
#include <stdio.h>

#include "lua.h"

/**
 * @name 兼容性支持
 * @brief 提供与旧版本Lua的兼容性支持
 * @{
 */

#if defined(LUA_COMPAT_GETN)
/**
 * @brief 获取表的长度（兼容性函数）
 *
 * 详细说明：
 * 这是为了与Lua 5.0兼容而保留的函数。在新代码中应该使用
 * lua_objlen或#操作符来获取表的长度。
 *
 * @param[in] L Lua状态机指针
 * @param[in] t 表的栈索引
 * @return 表的长度
 *
 * @deprecated 建议使用lua_objlen替代
 * @since C89
 */
LUALIB_API int (luaL_getn) (lua_State *L, int t);

/**
 * @brief 设置表的长度（兼容性函数）
 *
 * 详细说明：
 * 这是为了与Lua 5.0兼容而保留的函数。在Lua 5.1中，
 * 表的长度是自动计算的，不需要手动设置。
 *
 * @param[in] L Lua状态机指针
 * @param[in] t 表的栈索引
 * @param[in] n 要设置的长度值
 *
 * @deprecated 在Lua 5.1中此函数无实际作用
 * @since C89
 */
LUALIB_API void (luaL_setn) (lua_State *L, int t, int n);
#else
/** @brief 获取表长度的宏定义（推荐方式） */
#define luaL_getn(L,i)          ((int)lua_objlen(L, i))
/** @brief 设置表长度的空操作宏（兼容性） */
#define luaL_setn(L,i,j)        ((void)0)
#endif

#if defined(LUA_COMPAT_OPENLIB)
/** @brief 库打开函数的兼容性别名 */
#define luaI_openlib            luaL_openlib
#endif

/** @} */

/**
 * @name 错误代码常量
 * @brief 扩展的错误代码定义
 * @{
 */

/**
 * @brief 文件错误代码
 *
 * 详细说明：
 * 这是luaL_loadfile函数专用的错误代码，表示文件操作失败。
 * 它扩展了基础Lua API的错误代码集合。
 *
 * 错误情况：
 * - 文件不存在
 * - 文件无法打开
 * - 文件读取失败
 * - 权限不足
 *
 * @since C89
 * @see luaL_loadfile(), LUA_ERRERR
 */
#define LUA_ERRFILE             (LUA_ERRERR+1)

/** @} */

/**
 * @brief 库注册结构体：定义C函数库的注册信息
 *
 * 详细说明：
 * luaL_Reg结构体用于批量注册C函数到Lua中。它包含函数名称
 * 和对应的C函数指针，是构建Lua扩展库的基础数据结构。
 *
 * 结构体成员：
 * - name: 函数在Lua中的名称
 * - func: 对应的C函数指针
 *
 * 使用模式：
 * @code
 * static const luaL_Reg mylib[] = {
 *     {"add", l_add},
 *     {"sub", l_sub},
 *     {"mul", l_mul},
 *     {NULL, NULL}  // 结束标记
 * };
 *
 * luaL_register(L, "mylib", mylib);
 * @endcode
 *
 * 注册规则：
 * - 数组必须以{NULL, NULL}结尾
 * - 函数名称不能重复
 * - C函数必须符合lua_CFunction签名
 *
 * @since C89
 * @see luaL_register(), luaL_openlib(), lua_CFunction
 */
typedef struct luaL_Reg {
    const char *name;           /**< 函数在Lua中的名称 */
    lua_CFunction func;         /**< 对应的C函数指针 */
} luaL_Reg;



/**
 * @name 库注册和管理函数
 * @brief 用于注册和管理Lua扩展库的函数组
 * @{
 */

/**
 * @brief 打开库并注册函数（内部函数）
 *
 * 详细说明：
 * 这是一个内部函数，用于打开库并注册函数列表。它支持上值传递，
 * 允许为注册的函数提供共享的上值。
 *
 * 注册过程：
 * 1. 创建或获取指定名称的表
 * 2. 为每个函数创建闭包（包含上值）
 * 3. 将函数注册到表中
 * 4. 设置表为全局变量（如果libname不为NULL）
 *
 * 上值处理：
 * - 栈顶的nup个值会作为上值传递给所有函数
 * - 上值在函数中通过lua_upvalueindex访问
 * - 所有注册的函数共享相同的上值
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] libname 库名称，NULL表示不创建全局表
 * @param[in] l 函数注册数组，必须以{NULL,NULL}结尾
 * @param[in] nup 上值数量，栈顶的nup个值作为上值
 *
 * @note 栈变化：移除nup个上值，可能推入库表
 * @warning 这是内部函数，一般不直接使用
 * @since C89
 * @see luaL_register(), luaL_openlib()
 */
LUALIB_API void (luaI_openlib) (lua_State *L, const char *libname,
                                const luaL_Reg *l, int nup);

/**
 * @brief 注册函数库到Lua
 *
 * 详细说明：
 * 将C函数数组注册为Lua库。这是Lua C API中最重要和最常用的库注册
 * 函数，为创建Lua扩展模块提供了标准化的接口。函数简化了扩展库的
 * 创建过程，自动处理表的创建、函数的注册和全局变量的设置。
 *
 * 注册行为详解：
 * - libname为NULL：函数注册到栈顶的现有表中
 * - libname不为NULL：创建新的全局表并注册函数
 * - 函数数组必须以{NULL, NULL}结尾作为终止标记
 * - 注册完成后库表保留在栈顶
 * - 支持函数名重复，后注册的函数会覆盖先注册的
 *
 * 表创建策略：
 * - 如果全局表已存在，直接使用现有表
 * - 如果全局表不存在，创建新的空表
 * - 表创建后立即设置为全局变量
 * - 支持嵌套模块名（如"math.complex"）
 *
 * 函数注册过程：
 * - 遍历luaL_Reg数组直到遇到终止标记
 * - 为每个函数创建C闭包
 * - 将函数名作为键，闭包作为值存入表中
 * - 自动处理函数名的字符串内部化
 *
 * 内存管理：
 * - 函数名字符串由调用者管理生命周期
 * - 库表由Lua垃圾回收器管理
 * - C函数指针无需内存管理
 * - 注册过程中的临时对象自动清理
 *
 * 性能考虑：
 * - 注册时间：O(n)，n为函数数量
 * - 空间开销：每个函数一个表项
 * - 查找性能：O(1)哈希表查找
 * - 建议一次性注册所有函数
 *
 * 错误处理：
 * - 栈空间不足：抛出栈溢出错误
 * - 内存分配失败：抛出内存不足错误
 * - 无效函数指针：导致运行时错误
 * - 表操作失败：抛出相应的Lua错误
 *
 * 使用场景：
 * - 创建标准的Lua扩展模块
 * - 注册C函数库到Lua环境
 * - 实现领域特定的API
 * - 扩展现有的Lua库
 *
 * 最佳实践：
 * - 使用静态const数组定义函数列表
 * - 采用一致的命名约定
 * - 提供完整的错误检查
 * - 考虑模块的版本兼容性
 * - 实现标准的luaopen_*函数
 *
 * 标准模块示例：
 * @code
 * // 数学扩展库
 * static const luaL_Reg mathlib[] = {
 *     {"sin", math_sin},
 *     {"cos", math_cos},
 *     {"tan", math_tan},
 *     {"sqrt", math_sqrt},
 *     {"pow", math_pow},
 *     {"log", math_log},
 *     {NULL, NULL}  // 终止标记
 * };
 *
 * int luaopen_mathext(lua_State *L) {
 *     luaL_register(L, "mathext", mathlib);
 *
 *     // 添加常量
 *     lua_pushnumber(L, M_PI);
 *     lua_setfield(L, -2, "PI");
 *
 *     lua_pushnumber(L, M_E);
 *     lua_setfield(L, -2, "E");
 *
 *     return 1;  // 返回库表
 * }
 * @endcode
 *
 * 子模块注册：
 * @code
 * static const luaL_Reg stringutils[] = {
 *     {"trim", string_trim},
 *     {"split", string_split},
 *     {"join", string_join},
 *     {NULL, NULL}
 * };
 *
 * int luaopen_stringutils(lua_State *L) {
 *     // 注册到现有的string表中
 *     lua_getglobal(L, "string");
 *     if (lua_isnil(L, -1)) {
 *         lua_pop(L, 1);
 *         lua_newtable(L);
 *         lua_setglobal(L, "string");
 *         lua_getglobal(L, "string");
 *     }
 *
 *     luaL_register(L, NULL, stringutils);  // 注册到栈顶的表
 *     return 1;
 * }
 * @endcode
 *
 * 动态模块加载：
 * @code
 * // 支持动态加载的模块
 * static const luaL_Reg mylib[] = {
 *     {"func1", my_func1},
 *     {"func2", my_func2},
 *     {NULL, NULL}
 * };
 *
 * // 标准的动态库入口点
 * LUALIB_API int luaopen_mylib(lua_State *L) {
 *     luaL_register(L, "mylib", mylib);
 *
 *     // 设置版本信息
 *     lua_pushstring(L, "1.0.0");
 *     lua_setfield(L, -2, "_VERSION");
 *
 *     // 设置模块信息
 *     lua_pushstring(L, "My Custom Library");
 *     lua_setfield(L, -2, "_DESCRIPTION");
 *
 *     return 1;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 忘记添加终止标记{NULL, NULL}
 * - 函数名字符串生命周期管理错误
 * - 假设全局表总是存在
 * - 不检查栈空间是否足够
 * - 混淆库名和模块名的概念
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] libname 库名称，NULL表示使用栈顶的表，非NULL时创建全局表
 * @param[in] l 函数注册数组，必须以{NULL,NULL}结尾，不能为NULL
 *
 * @warning 函数数组必须以{NULL,NULL}结尾，否则导致未定义行为
 * @warning libname为NULL时栈顶必须是有效的表
 * @warning 函数名字符串的生命周期必须超过注册过程
 *
 * @note 栈增长1个位置（库表留在栈顶）
 * @note 如果libname不为NULL，会创建或获取对应的全局表
 * @note 支持向现有表中添加函数（libname为NULL时）
 * @note 函数注册顺序与数组中的顺序一致
 *
 * @since C89
 * @see luaL_Reg, luaL_openlib(), lua_CFunction, luaI_openlib()
 */
LUALIB_API void (luaL_register) (lua_State *L, const char *libname,
                                 const luaL_Reg *l);

/** @} */

/**
 * @name 元表操作函数
 * @brief 用于元表查询和调用的便利函数
 * @{
 */

/**
 * @brief 获取对象的元表字段
 *
 * 详细说明：
 * 检查指定对象是否有元表，如果有则获取元表中指定字段的值。
 * 这是一个安全的元表字段访问函数。
 *
 * 查找过程：
 * 1. 获取对象的元表
 * 2. 在元表中查找指定字段
 * 3. 如果找到，将字段值推入栈顶
 * 4. 返回字段是否存在
 *
 * 使用场景：
 * - 检查对象是否有特定的元方法
 * - 安全地访问元表字段
 * - 实现条件性的元方法调用
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] obj 对象的栈索引
 * @param[in] e 要查找的字段名称，不能为NULL
 *
 * @return 是否找到指定字段
 * @retval 1 找到字段，字段值已推入栈顶
 * @retval 0 未找到字段或对象无元表
 *
 * @note 只有在返回1时栈才会增长
 * @since C89
 * @see luaL_callmeta(), lua_getmetatable()
 */
LUALIB_API int (luaL_getmetafield) (lua_State *L, int obj, const char *e);

/**
 * @brief 调用对象的元方法
 *
 * 详细说明：
 * 检查对象是否有指定的元方法，如果有则调用该元方法。
 * 这是一个便利函数，简化了元方法的条件调用。
 *
 * 调用过程：
 * 1. 检查对象是否有指定的元方法
 * 2. 如果有，将对象作为参数调用元方法
 * 3. 元方法的返回值留在栈中
 * 4. 返回是否成功调用了元方法
 *
 * 常用元方法：
 * - "__tostring": 字符串转换
 * - "__len": 长度操作
 * - "__call": 函数调用
 * - "__gc": 垃圾回收
 *
 * 使用示例：
 * @code
 * // 尝试调用对象的__tostring元方法
 * if (luaL_callmeta(L, 1, "__tostring")) {
 *     // 元方法被调用，结果在栈顶
 *     const char *str = lua_tostring(L, -1);
 *     printf("Object string: %s\n", str);
 * } else {
 *     // 对象没有__tostring元方法
 *     printf("Object has no __tostring method\n");
 * }
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] obj 对象的栈索引
 * @param[in] e 元方法名称，不能为NULL
 *
 * @return 是否成功调用了元方法
 * @retval 1 元方法被调用，返回值在栈中
 * @retval 0 对象没有该元方法
 *
 * @warning 可能调用Lua代码，因此可能抛出错误
 * @since C89
 * @see luaL_getmetafield(), lua_call()
 */
LUALIB_API int (luaL_callmeta) (lua_State *L, int obj, const char *e);

/** @} */

/**
 * @name 错误处理函数
 * @brief 用于参数检查和错误报告的函数组
 * @{
 */

/**
 * @brief 报告类型错误
 *
 * 详细说明：
 * 抛出一个类型错误，指示指定参数的类型不正确。
 * 这个函数永远不会返回，而是抛出Lua错误。
 *
 * 错误信息格式：
 * "bad argument #<narg> (<tname> expected, got <actual_type>)"
 *
 * 使用场景：
 * - 参数类型检查失败时
 * - 自定义类型验证
 * - 提供标准化的错误信息
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] narg 参数位置（从1开始）
 * @param[in] tname 期望的类型名称，不能为NULL
 *
 * @return 永远不会返回
 *
 * @warning 此函数永远不会返回，会抛出Lua错误
 * @since C89
 * @see luaL_argerror(), luaL_checktype()
 */
LUALIB_API int (luaL_typerror) (lua_State *L, int narg, const char *tname);

/**
 * @brief 报告参数错误
 *
 * 详细说明：
 * 抛出一个参数错误，提供自定义的错误信息。
 * 这是最通用的参数错误报告函数。
 *
 * 错误信息格式：
 * "bad argument #<numarg> (<extramsg>)"
 *
 * 使用场景：
 * - 参数值超出有效范围
 * - 参数组合无效
 * - 自定义验证失败
 * - 提供详细的错误说明
 *
 * 使用示例：
 * @code
 * static int l_sqrt(lua_State *L) {
 *     double x = luaL_checknumber(L, 1);
 *     if (x < 0) {
 *         return luaL_argerror(L, 1, "negative number");
 *     }
 *     lua_pushnumber(L, sqrt(x));
 *     return 1;
 * }
 * @endcode
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] numarg 参数位置（从1开始）
 * @param[in] extramsg 额外的错误信息，不能为NULL
 *
 * @return 永远不会返回
 *
 * @warning 此函数永远不会返回，会抛出Lua错误
 * @since C89
 * @see luaL_typerror(), luaL_error()
 */
LUALIB_API int (luaL_argerror) (lua_State *L, int numarg, const char *extramsg);

/** @} */
/**
 * @name 参数检查和转换函数
 * @brief 用于检查和转换函数参数的安全函数组
 * @{
 */

/**
 * @brief 检查并获取字符串参数
 *
 * 详细说明：
 * 检查指定位置的参数是否为字符串，如果是则返回字符串内容和长度。
 * 如果参数不是字符串或无法转换为字符串，抛出类型错误。这是最常用的
 * 字符串参数验证函数，提供了类型安全和自动转换功能。
 *
 * 转换规则：
 * - 字符串类型：直接返回，无需转换
 * - 数字类型：自动转换为字符串表示（注意：会修改栈中的值）
 * - 布尔类型：不支持转换，抛出类型错误
 * - nil类型：不支持转换，抛出类型错误
 * - 表、函数、用户数据：不支持转换，抛出类型错误
 *
 * 内存安全：
 * - 返回的指针指向Lua内部管理的字符串
 * - 字符串生命周期由Lua垃圾回收器管理
 * - 指针在下次Lua API调用后可能失效
 * - 如需长期保存，应立即复制字符串内容
 *
 * 性能考虑：
 * - 字符串类型检查：O(1)时间复杂度
 * - 数字到字符串转换：O(log n)时间复杂度
 * - 字符串长度计算：O(1)时间复杂度（已缓存）
 * - 避免在循环中重复调用，缓存结果
 *
 * 错误处理：
 * - 参数位置无效：抛出"bad argument #N"错误
 * - 类型不匹配：抛出"string expected, got <type>"错误
 * - 栈溢出：抛出"stack overflow"错误
 * - 内存不足：抛出"not enough memory"错误
 *
 * 使用场景：
 * - 验证必需的字符串参数
 * - 获取文件名、路径、模式字符串
 * - 处理用户输入和配置参数
 * - 实现字符串处理函数
 *
 * 最佳实践：
 * - 总是检查返回值的有效性
 * - 如需保存字符串，立即复制内容
 * - 使用长度参数处理二进制数据
 * - 结合luaL_argcheck进行额外验证
 *
 * 使用示例：
 * @code
 * static int l_file_read(lua_State *L) {
 *     size_t len;
 *     const char *filename = luaL_checklstring(L, 1, &len);
 *
 *     // 验证文件名长度
 *     luaL_argcheck(L, len > 0, 1, "filename cannot be empty");
 *     luaL_argcheck(L, len < 260, 1, "filename too long");
 *
 *     // 立即复制文件名（如果需要长期保存）
 *     char *filename_copy = malloc(len + 1);
 *     if (!filename_copy) {
 *         return luaL_error(L, "out of memory");
 *     }
 *     memcpy(filename_copy, filename, len + 1);
 *
 *     // 使用文件名...
 *     FILE *f = fopen(filename_copy, "r");
 *     free(filename_copy);
 *
 *     if (!f) {
 *         return luaL_error(L, "cannot open file: %s", filename);
 *     }
 *
 *     // 读取文件内容...
 *     fclose(f);
 *     return 1;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 不要假设返回的指针永远有效
 * - 数字转换会修改栈中的原始值
 * - 长度参数可能包含嵌入的null字符
 * - 不要在错误处理代码中使用返回的指针
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] numArg 参数位置（从1开始，必须 > 0）
 * @param[out] l 输出字符串长度指针，可以为NULL
 *
 * @return 字符串内容指针
 * @retval 非NULL 指向有效字符串的指针，以null结尾
 *
 * @warning 参数类型错误时会抛出Lua错误，函数不会返回
 * @warning 返回的指针在下次Lua API调用后可能失效
 * @warning 数字参数会被转换为字符串，修改栈中的值
 *
 * @note 返回的指针指向Lua内部字符串，不需要手动释放
 * @note 字符串保证以null字符结尾，可安全用于C字符串函数
 * @note 如果l不为NULL，会设置为字符串的实际字节长度
 *
 * @since C89
 * @see luaL_optlstring(), luaL_checkstring(), luaL_argcheck(), lua_tolstring()
 */
LUALIB_API const char *(luaL_checklstring) (lua_State *L, int numArg,
                                            size_t *l);

/**
 * @brief 获取可选的字符串参数
 *
 * 详细说明：
 * 检查指定位置的参数，如果是字符串则返回其内容，如果是nil或不存在
 * 则返回默认值。这是处理可选参数的标准模式，广泛用于提供函数参数
 * 的默认值功能。
 *
 * 处理规则：
 * - 字符串类型：返回字符串内容和长度
 * - 数字类型：自动转换为字符串（会修改栈中的值）
 * - nil类型：返回默认值和默认长度
 * - 参数不存在：返回默认值和默认长度
 * - 其他类型：抛出类型错误
 *
 * 默认值处理：
 * - 如果def为NULL，返回NULL并设置长度为0
 * - 如果def不为NULL，返回def并设置长度为strlen(def)
 * - 默认值的生命周期由调用者管理
 * - 默认值不会被Lua垃圾回收器管理
 *
 * 内存安全：
 * - 字符串返回值指向Lua内部字符串或调用者提供的默认值
 * - Lua字符串的生命周期由垃圾回收器管理
 * - 默认值的生命周期由调用者保证
 * - 混合使用时需要特别注意指针的有效性
 *
 * 性能考虑：
 * - nil检查：O(1)时间复杂度
 * - 字符串处理：与luaL_checklstring相同
 * - 默认值长度计算：O(n)时间复杂度（如果需要）
 * - 建议缓存默认值长度以提高性能
 *
 * 错误处理：
 * - 参数类型错误：抛出"string expected, got <type>"错误
 * - 栈溢出：抛出"stack overflow"错误
 * - 内存不足：抛出"not enough memory"错误
 * - 默认值相关错误不会发生（由调用者保证）
 *
 * 使用场景：
 * - 处理可选的文件名、路径参数
 * - 提供默认的配置字符串
 * - 实现带默认值的字符串选项
 * - 简化API的参数处理逻辑
 *
 * 最佳实践：
 * - 使用静态字符串作为默认值
 * - 预先计算默认值的长度
 * - 明确文档化默认值的含义
 * - 考虑使用空字符串而非NULL作为默认值
 *
 * 使用示例：
 * @code
 * static int l_log_message(lua_State *L) {
 *     size_t msg_len, level_len;
 *     const char *message = luaL_checklstring(L, 1, &msg_len);
 *     const char *level = luaL_optlstring(L, 2, "INFO", &level_len);
 *
 *     // 验证日志级别
 *     if (strcmp(level, "DEBUG") != 0 && strcmp(level, "INFO") != 0 &&
 *         strcmp(level, "WARN") != 0 && strcmp(level, "ERROR") != 0) {
 *         return luaL_argerror(L, 2, "invalid log level");
 *     }
 *
 *     // 输出日志
 *     printf("[%s] %.*s\n", level, (int)msg_len, message);
 *     return 0;
 * }
 *
 * // Lua调用示例：
 * // log_message("Hello World")           -- 使用默认级别 "INFO"
 * // log_message("Debug info", "DEBUG")   -- 使用指定级别
 * // log_message("Error!", "ERROR")       -- 使用错误级别
 * @endcode
 *
 * 高级用法：
 * @code
 * static int l_format_string(lua_State *L) {
 *     size_t fmt_len, sep_len;
 *     const char *format = luaL_checklstring(L, 1, &fmt_len);
 *     const char *separator = luaL_optlstring(L, 2, ", ", &sep_len);
 *
 *     // 使用格式字符串和分隔符处理数据
 *     luaL_Buffer b;
 *     luaL_buffinit(L, &b);
 *
 *     int argc = lua_gettop(L);
 *     for (int i = 3; i <= argc; i++) {
 *         if (i > 3) {
 *             luaL_addlstring(&b, separator, sep_len);
 *         }
 *         // 格式化并添加参数...
 *     }
 *
 *     luaL_pushresult(&b);
 *     return 1;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 混淆Lua字符串和默认值的生命周期
 * - 假设默认值永远不为NULL
 * - 忘记处理默认值的长度计算
 * - 在多线程环境中使用非线程安全的默认值
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] numArg 参数位置（从1开始，必须 > 0）
 * @param[in] def 默认值字符串，可以为NULL
 * @param[out] l 输出字符串长度指针，可以为NULL
 *
 * @return 字符串内容指针或默认值
 * @retval 非NULL 指向有效字符串的指针（Lua字符串或默认值）
 * @retval NULL 仅当def为NULL且参数为nil时返回
 *
 * @warning 参数类型错误时会抛出Lua错误，函数不会返回
 * @warning 返回的Lua字符串指针在下次API调用后可能失效
 * @warning 数字参数会被转换为字符串，修改栈中的值
 *
 * @note 返回的指针可能指向Lua内部字符串或调用者提供的默认值
 * @note 如果l不为NULL，会设置为实际字符串的字节长度
 * @note 默认值的生命周期由调用者负责管理
 *
 * @since C89
 * @see luaL_checklstring(), luaL_optstring(), luaL_opt(), lua_isnoneornil()
 */
LUALIB_API const char *(luaL_optlstring) (lua_State *L, int numArg,
                                          const char *def, size_t *l);

/**
 * @brief 检查并获取数字参数
 *
 * 详细说明：
 * 检查指定位置的参数是否为数字，如果是则返回数值。如果参数不是数字
 * 或无法转换为数字，抛出类型错误。这是数值参数验证的标准函数，
 * 支持整数和浮点数的统一处理。
 *
 * 转换规则：
 * - 数字类型（整数/浮点）：直接返回，无精度损失
 * - 数字字符串：解析为数字（支持科学计数法）
 * - 十六进制字符串：支持0x前缀的十六进制数
 * - 布尔类型：不支持转换，抛出类型错误
 * - nil类型：不支持转换，抛出类型错误
 * - 其他类型：不支持转换，抛出类型错误
 *
 * 数值精度：
 * - 返回类型为lua_Number（通常是double）
 * - 支持IEEE 754双精度浮点数范围
 * - 整数在±2^53范围内保证精确表示
 * - 超出范围的整数可能有精度损失
 *
 * 字符串解析：
 * - 支持标准数字格式：123, -456, 3.14, -2.5e10
 * - 支持十六进制格式：0x1A, 0X2B, 0xff
 * - 忽略前导和尾随空白字符
 * - 部分匹配：字符串开头是数字即可（如"123abc"解析为123）
 *
 * 性能考虑：
 * - 数字类型检查：O(1)时间复杂度
 * - 字符串解析：O(n)时间复杂度，n为字符串长度
 * - 无内存分配，性能优秀
 * - 避免在紧密循环中重复调用
 *
 * 错误处理：
 * - 参数位置无效：抛出"bad argument #N"错误
 * - 类型不匹配：抛出"number expected, got <type>"错误
 * - 字符串解析失败：抛出"malformed number"错误
 * - 栈溢出：抛出"stack overflow"错误
 *
 * 使用场景：
 * - 数学函数的参数验证
 * - 几何计算的坐标参数
 * - 物理模拟的数值参数
 * - 配置文件的数值选项
 *
 * 最佳实践：
 * - 结合luaL_argcheck进行范围验证
 * - 考虑使用luaL_checkinteger处理整数
 * - 注意浮点数的精度限制
 * - 处理特殊值（NaN、无穷大）
 *
 * 使用示例：
 * @code
 * static int l_circle_area(lua_State *L) {
 *     lua_Number radius = luaL_checknumber(L, 1);
 *
 *     // 验证半径有效性
 *     luaL_argcheck(L, radius >= 0, 1, "radius must be non-negative");
 *     luaL_argcheck(L, isfinite(radius), 1, "radius must be finite");
 *
 *     lua_Number area = M_PI * radius * radius;
 *     lua_pushnumber(L, area);
 *     return 1;
 * }
 *
 * static int l_power(lua_State *L) {
 *     lua_Number base = luaL_checknumber(L, 1);
 *     lua_Number exponent = luaL_checknumber(L, 2);
 *
 *     // 处理特殊情况
 *     if (base == 0.0 && exponent < 0.0) {
 *         return luaL_error(L, "division by zero");
 *     }
 *
 *     lua_Number result = pow(base, exponent);
 *
 *     // 检查结果有效性
 *     if (!isfinite(result)) {
 *         return luaL_error(L, "result overflow or underflow");
 *     }
 *
 *     lua_pushnumber(L, result);
 *     return 1;
 * }
 * @endcode
 *
 * 高级用法（范围验证）：
 * @code
 * static int l_set_volume(lua_State *L) {
 *     lua_Number volume = luaL_checknumber(L, 1);
 *
 *     // 音量范围验证
 *     luaL_argcheck(L, volume >= 0.0 && volume <= 1.0, 1,
 *                   "volume must be between 0.0 and 1.0");
 *
 *     // 设置音量...
 *     set_audio_volume((float)volume);
 *     return 0;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 忽略浮点数精度限制
 * - 不检查特殊值（NaN、无穷大）
 * - 假设所有数字都在有效范围内
 * - 混淆lua_Number和C的数值类型
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] numArg 参数位置（从1开始，必须 > 0）
 *
 * @return 数字值
 * @retval lua_Number 有效的数值，可能是整数或浮点数
 *
 * @warning 参数类型错误时会抛出Lua错误，函数不会返回
 * @warning 字符串解析失败时会抛出Lua错误
 * @warning 返回值可能是NaN或无穷大，需要调用者检查
 *
 * @note 支持整数和浮点数的统一处理
 * @note 字符串参数会被自动解析为数字
 * @note 返回类型为lua_Number，通常是double
 *
 * @since C89
 * @see luaL_optnumber(), luaL_checkinteger(), luaL_argcheck(), lua_tonumber()
 */
LUALIB_API lua_Number (luaL_checknumber) (lua_State *L, int numArg);

/**
 * @brief 获取可选的数字参数
 *
 * 详细说明：
 * 检查指定位置的参数，如果是数字则返回其值，
 * 如果是nil或不存在则返回默认值。
 *
 * 处理规则：
 * - 数字类型：返回数值
 * - 数字字符串：解析为数字
 * - nil或不存在：返回默认值
 * - 其他类型：抛出类型错误
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] nArg 参数位置（从1开始）
 * @param[in] def 默认值
 *
 * @return 数字值或默认值
 *
 * @since C89
 * @see luaL_checknumber(), luaL_optinteger()
 */
LUALIB_API lua_Number (luaL_optnumber) (lua_State *L, int nArg, lua_Number def);

/**
 * @brief 检查并获取整数参数
 *
 * 详细说明：
 * 检查指定位置的参数是否为整数，如果是则返回整数值。
 * 浮点数会被截断为整数。
 *
 * 转换规则：
 * - 整数：直接返回
 * - 浮点数：截断为整数
 * - 数字字符串：解析后截断
 * - 其他类型：抛出类型错误
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] numArg 参数位置（从1开始）
 *
 * @return 整数值
 *
 * @warning 参数类型错误时会抛出Lua错误，不会返回
 * @since C89
 * @see luaL_optinteger(), luaL_checknumber()
 */
LUALIB_API lua_Integer (luaL_checkinteger) (lua_State *L, int numArg);

/**
 * @brief 获取可选的整数参数
 *
 * 详细说明：
 * 检查指定位置的参数，如果是整数则返回其值，如果是nil或不存在
 * 则返回默认值。这是处理可选整数参数的标准函数，广泛用于
 * 提供函数参数的默认值功能。
 *
 * 处理规则：
 * - 整数类型：直接返回整数值
 * - 浮点数类型：截断为整数（丢弃小数部分）
 * - 数字字符串：解析后截断为整数
 * - nil类型：返回默认值
 * - 参数不存在：返回默认值
 * - 其他类型：抛出类型错误
 *
 * 数值转换：
 * - 浮点数截断：使用C语言的强制类型转换
 * - 范围检查：确保值在lua_Integer范围内
 * - 精度处理：大数值可能有精度损失
 * - 特殊值：NaN和无穷大会导致未定义行为
 *
 * 默认值处理：
 * - 默认值直接返回，无需验证
 * - 默认值的类型为lua_Integer
 * - 调用者负责确保默认值的合理性
 * - 支持负数、零和正数作为默认值
 *
 * 性能考虑：
 * - nil检查：O(1)时间复杂度
 * - 数值转换：与luaL_checkinteger相同
 * - 默认值返回：O(1)时间复杂度
 * - 无额外内存分配
 *
 * 错误处理：
 * - 参数类型错误：抛出"number expected, got <type>"错误
 * - 字符串解析失败：抛出"malformed number"错误
 * - 栈溢出：抛出"stack overflow"错误
 * - 默认值相关错误不会发生
 *
 * 使用场景：
 * - 可选的数组索引参数
 * - 可选的计数器和大小参数
 * - 可选的配置数值
 * - 可选的标志位和选项
 *
 * 最佳实践：
 * - 选择合理的默认值
 * - 结合luaL_argcheck进行范围验证
 * - 考虑整数溢出问题
 * - 文档化默认值的含义
 *
 * 使用示例：
 * @code
 * static int l_substring(lua_State *L) {
 *     size_t len;
 *     const char *str = luaL_checklstring(L, 1, &len);
 *     lua_Integer start = luaL_optinteger(L, 2, 1);        // 默认从第1个字符开始
 *     lua_Integer end = luaL_optinteger(L, 3, (lua_Integer)len);  // 默认到字符串末尾
 *
 *     // 参数范围验证
 *     luaL_argcheck(L, start >= 1, 2, "start position must be >= 1");
 *     luaL_argcheck(L, end >= start, 3, "end position must be >= start");
 *     luaL_argcheck(L, start <= (lua_Integer)len, 2, "start position out of range");
 *     luaL_argcheck(L, end <= (lua_Integer)len, 3, "end position out of range");
 *
 *     // 提取子字符串
 *     lua_pushlstring(L, str + start - 1, (size_t)(end - start + 1));
 *     return 1;
 * }
 * @endcode
 *
 * 高级用法（配置参数）：
 * @code
 * static int l_create_buffer(lua_State *L) {
 *     lua_Integer size = luaL_optinteger(L, 1, 1024);      // 默认1KB
 *     lua_Integer align = luaL_optinteger(L, 2, 8);        // 默认8字节对齐
 *
 *     // 参数验证
 *     luaL_argcheck(L, size > 0, 1, "buffer size must be positive");
 *     luaL_argcheck(L, align > 0 && (align & (align - 1)) == 0, 2,
 *                   "alignment must be power of 2");
 *
 *     // 创建缓冲区
 *     void *buffer = aligned_alloc((size_t)align, (size_t)size);
 *     if (!buffer) {
 *         return luaL_error(L, "cannot allocate buffer");
 *     }
 *
 *     lua_pushlightuserdata(L, buffer);
 *     return 1;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 忽略浮点数截断可能导致的精度损失
 * - 不验证默认值的合理性
 * - 假设lua_Integer总是32位或64位
 * - 混淆参数位置和数组索引
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] nArg 参数位置（从1开始，必须 > 0）
 * @param[in] def 默认值，当参数为nil或不存在时返回
 *
 * @return 整数值或默认值
 * @retval lua_Integer 有效的整数值或提供的默认值
 *
 * @warning 参数类型错误时会抛出Lua错误，函数不会返回
 * @warning 浮点数参数会被截断，可能导致精度损失
 * @warning 大数值可能超出lua_Integer范围
 *
 * @note 浮点数参数会被截断为整数
 * @note 默认值直接返回，无需类型检查
 * @note 支持负数、零和正数作为默认值
 *
 * @since C89
 * @see luaL_checkinteger(), luaL_optnumber(), luaL_optint(), lua_tointeger()
 */
LUALIB_API lua_Integer (luaL_optinteger) (lua_State *L, int nArg,
                                          lua_Integer def);

/** @} */

/**
 * @name 栈和类型检查函数
 * @brief 用于栈空间和参数类型验证的函数组
 * @{
 */

/**
 * @brief 检查栈空间是否充足
 *
 * 详细说明：
 * 检查栈是否有足够的空间容纳指定数量的元素。
 * 如果空间不足，抛出带有自定义消息的错误。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] sz 需要的栈空间大小
 * @param[in] msg 错误消息，不能为NULL
 *
 * @warning 栈空间不足时会抛出Lua错误，不会返回
 * @since C89
 * @see lua_checkstack()
 */
LUALIB_API void (luaL_checkstack) (lua_State *L, int sz, const char *msg);

/**
 * @brief 检查参数类型
 *
 * 详细说明：
 * 检查指定位置的参数是否为指定类型。
 * 如果类型不匹配，抛出类型错误。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] narg 参数位置（从1开始）
 * @param[in] t 期望的类型（LUA_T*常量）
 *
 * @warning 类型不匹配时会抛出Lua错误，不会返回
 * @since C89
 * @see luaL_typerror(), lua_type()
 */
LUALIB_API void (luaL_checktype) (lua_State *L, int narg, int t);

/**
 * @brief 检查参数是否存在
 *
 * 详细说明：
 * 检查指定位置是否有参数（不是nil或不存在）。
 * 如果参数不存在，抛出参数错误。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] narg 参数位置（从1开始）
 *
 * @warning 参数不存在时会抛出Lua错误，不会返回
 * @since C89
 * @see luaL_argerror()
 */
LUALIB_API void (luaL_checkany) (lua_State *L, int narg);

/** @} */

/**
 * @name 元表和用户数据管理
 * @brief 用于管理元表和用户数据的函数组
 * @{
 */

/**
 * @brief 创建新的元表
 *
 * 详细说明：
 * 创建一个新的元表并注册到注册表中。如果同名元表已存在，
 * 则将现有元表推入栈顶。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] tname 元表名称，不能为NULL
 *
 * @return 是否创建了新元表
 * @retval 1 创建了新元表
 * @retval 0 元表已存在
 *
 * @note 元表会被推入栈顶
 * @since C89
 * @see luaL_checkudata(), luaL_getmetatable()
 */
LUALIB_API int   (luaL_newmetatable) (lua_State *L, const char *tname);

/**
 * @brief 检查用户数据类型
 *
 * 详细说明：
 * 检查指定位置的参数是否为指定类型的用户数据。
 * 通过元表名称进行类型验证。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] ud 用户数据的栈索引
 * @param[in] tname 期望的元表名称，不能为NULL
 *
 * @return 用户数据指针
 * @retval 非NULL 有效的用户数据指针
 *
 * @warning 类型不匹配时会抛出Lua错误，不会返回
 * @since C89
 * @see luaL_newmetatable(), lua_touserdata()
 */
LUALIB_API void *(luaL_checkudata) (lua_State *L, int ud, const char *tname);

/** @} */

/**
 * @name 错误处理和报告
 * @brief 用于错误处理和调试信息的函数组
 * @{
 */

/**
 * @brief 获取调用位置信息
 *
 * 详细说明：
 * 将调用栈中指定层级的位置信息推入栈顶。
 * 信息格式为"filename:line:"。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] lvl 调用栈层级（1表示调用者）
 *
 * @note 栈增长1个位置（位置信息字符串）
 * @since C89
 * @see luaL_error()
 */
LUALIB_API void (luaL_where) (lua_State *L, int lvl);

/**
 * @brief 格式化错误并抛出
 *
 * 详细说明：
 * 使用printf风格的格式化创建错误消息并抛出Lua错误。这是Lua C API中
 * 最重要的错误处理函数，提供了灵活的错误报告机制。函数会自动添加
 * 调用位置信息，帮助用户定位错误源头。
 *
 * 错误处理机制：
 * - 使用printf风格的格式化字符串
 * - 自动添加调用位置信息（文件名:行号:）
 * - 创建完整的错误消息字符串
 * - 抛出Lua错误，触发错误处理流程
 * - 支持错误传播和捕获机制
 *
 * 格式化支持：
 * - 支持所有标准printf格式说明符
 * - %s: 字符串格式化
 * - %d, %i: 整数格式化
 * - %f, %g: 浮点数格式化
 * - %c: 字符格式化
 * - %p: 指针格式化
 * - %x, %X: 十六进制格式化
 *
 * 内存管理：
 * - 错误消息字符串由Lua内部管理
 * - 无需手动释放错误消息内存
 * - 格式化过程中的临时内存自动清理
 * - 支持任意长度的错误消息
 *
 * 错误传播：
 * - 错误会沿调用栈向上传播
 * - 可被lua_pcall等保护调用捕获
 * - 未捕获的错误会终止Lua程序
 * - 支持错误处理函数的自定义处理
 *
 * 性能考虑：
 * - 错误处理是异常路径，性能不是主要考虑
 * - 格式化过程有一定开销
 * - 栈展开过程可能较慢
 * - 避免在性能关键路径中频繁使用
 *
 * 调试支持：
 * - 自动包含调用位置信息
 * - 支持调试器断点设置
 * - 提供完整的错误上下文
 * - 便于错误追踪和诊断
 *
 * 使用场景：
 * - 参数验证失败
 * - 资源分配失败
 * - 业务逻辑错误
 * - 系统调用失败
 * - 配置错误
 *
 * 最佳实践：
 * - 提供清晰、具体的错误消息
 * - 包含足够的上下文信息
 * - 使用一致的错误消息格式
 * - 避免暴露内部实现细节
 * - 考虑国际化和本地化需求
 *
 * 使用示例：
 * @code
 * static int l_divide(lua_State *L) {
 *     lua_Number a = luaL_checknumber(L, 1);
 *     lua_Number b = luaL_checknumber(L, 2);
 *
 *     if (b == 0.0) {
 *         return luaL_error(L, "division by zero");
 *     }
 *
 *     if (!isfinite(a) || !isfinite(b)) {
 *         return luaL_error(L, "invalid operand: %g, %g", a, b);
 *     }
 *
 *     lua_pushnumber(L, a / b);
 *     return 1;
 * }
 *
 * static int l_open_file(lua_State *L) {
 *     const char *filename = luaL_checkstring(L, 1);
 *     const char *mode = luaL_optstring(L, 2, "r");
 *
 *     FILE *f = fopen(filename, mode);
 *     if (!f) {
 *         return luaL_error(L, "cannot open file '%s': %s",
 *                          filename, strerror(errno));
 *     }
 *
 *     // 创建文件用户数据...
 *     return 1;
 * }
 * @endcode
 *
 * 高级错误处理：
 * @code
 * static int l_safe_operation(lua_State *L) {
 *     const char *operation = luaL_checkstring(L, 1);
 *
 *     // 复杂的错误检查
 *     if (strlen(operation) == 0) {
 *         return luaL_error(L, "operation name cannot be empty");
 *     }
 *
 *     if (strlen(operation) > MAX_OPERATION_NAME) {
 *         return luaL_error(L, "operation name too long: %zu characters "
 *                          "(maximum %d)", strlen(operation), MAX_OPERATION_NAME);
 *     }
 *
 *     // 执行操作...
 *     int result = perform_operation(operation);
 *     if (result != 0) {
 *         return luaL_error(L, "operation '%s' failed with code %d",
 *                          operation, result);
 *     }
 *
 *     return 0;
 * }
 * @endcode
 *
 * 错误消息设计原则：
 * - 明确指出问题所在
 * - 提供足够的上下文信息
 * - 建议可能的解决方案
 * - 使用用户友好的语言
 * - 避免技术术语的滥用
 *
 * 常见陷阱：
 * - 错误消息过于简单或模糊
 * - 暴露敏感的内部信息
 * - 格式化字符串与参数不匹配
 * - 在错误处理中再次产生错误
 * - 忘记错误函数不会返回
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] fmt printf风格的格式化字符串，不能为NULL
 * @param[in] ... 与格式化字符串匹配的参数列表
 *
 * @return 永远不会返回（函数签名返回int仅为兼容性）
 *
 * @warning 此函数永远不会返回，会抛出Lua错误并开始栈展开
 * @warning 格式化字符串必须与参数类型匹配，否则行为未定义
 * @warning 不要在错误处理函数中调用可能失败的操作
 *
 * @note 错误消息会自动包含调用位置信息
 * @note 支持任意长度的错误消息
 * @note 错误消息的内存由Lua自动管理
 * @note 函数返回类型为int仅为与其他错误函数保持一致
 *
 * @since C89
 * @see luaL_argerror(), luaL_typerror(), luaL_where(), lua_error(), lua_pcall()
 */
LUALIB_API int (luaL_error) (lua_State *L, const char *fmt, ...);

/**
 * @brief 检查选项参数
 *
 * 详细说明：
 * 检查字符串参数是否在给定的选项列表中，返回匹配选项在列表中的索引。
 * 这是一个非常有用的函数，用于验证枚举类型的参数，确保参数值在
 * 预定义的有效选项范围内。
 *
 * 匹配机制：
 * - 使用strcmp进行精确字符串匹配
 * - 区分大小写的比较
 * - 支持默认值机制
 * - 返回第一个匹配项的索引
 *
 * 默认值处理：
 * - 如果参数为nil且def不为NULL，使用默认值
 * - 如果参数为nil且def为NULL，抛出参数错误
 * - 默认值也必须在选项列表中
 *
 * 错误处理：
 * - 参数不是字符串：抛出类型错误
 * - 选项不在列表中：抛出参数错误，显示有效选项
 * - 默认值不在列表中：抛出参数错误
 *
 * 性能考虑：
 * - 线性搜索：O(n)时间复杂度
 * - 字符串比较开销
 * - 建议将常用选项放在列表前面
 * - 选项数量较多时考虑使用哈希表
 *
 * 使用场景：
 * - 文件打开模式验证
 * - 算法类型选择
 * - 配置选项验证
 * - 枚举值参数检查
 *
 * 最佳实践：
 * - 使用静态const数组定义选项列表
 * - 提供有意义的默认值
 * - 选项名称应该简洁明了
 * - 考虑大小写敏感性
 *
 * 使用示例：
 * @code
 * static const char *const file_modes[] = {
 *     "read", "write", "append", "binary", NULL
 * };
 *
 * static int l_open_file(lua_State *L) {
 *     const char *filename = luaL_checkstring(L, 1);
 *     int mode = luaL_checkoption(L, 2, "read", file_modes);
 *
 *     switch (mode) {
 *         case 0: // "read"
 *             return open_file_read(L, filename);
 *         case 1: // "write"
 *             return open_file_write(L, filename);
 *         case 2: // "append"
 *             return open_file_append(L, filename);
 *         case 3: // "binary"
 *             return open_file_binary(L, filename);
 *         default:
 *             return luaL_error(L, "internal error");
 *     }
 * }
 * @endcode
 *
 * 高级用法（配置选项）：
 * @code
 * static const char *const log_levels[] = {
 *     "debug", "info", "warning", "error", "fatal", NULL
 * };
 *
 * static int l_set_log_level(lua_State *L) {
 *     int level = luaL_checkoption(L, 1, "info", log_levels);
 *
 *     // 设置全局日志级别
 *     global_log_level = level;
 *
 *     // 返回设置的级别名称
 *     lua_pushstring(L, log_levels[level]);
 *     return 1;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 忘记在选项列表末尾添加NULL
 * - 默认值不在选项列表中
 * - 假设选项匹配是大小写不敏感的
 * - 选项列表的生命周期管理错误
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] narg 参数位置（从1开始，必须 > 0）
 * @param[in] def 默认选项字符串，可以为NULL
 * @param[in] lst 选项列表数组，必须以NULL结尾，不能为NULL
 *
 * @return 选项在列表中的索引（从0开始）
 * @retval >=0 匹配选项的索引位置
 *
 * @warning 选项不在列表中时会抛出Lua错误，函数不会返回
 * @warning 选项列表必须以NULL结尾，否则导致未定义行为
 * @warning 默认值（如果提供）也必须在选项列表中
 *
 * @note 使用精确的字符串匹配（区分大小写）
 * @note 返回第一个匹配项的索引
 * @note 选项列表的生命周期必须超过函数调用
 *
 * @since C89
 * @see luaL_checkstring(), luaL_optstring(), luaL_argerror()
 */
LUALIB_API int (luaL_checkoption) (lua_State *L, int narg, const char *def,
                                   const char *const lst[]);

/** @} */

/**
 * @name 引用系统
 * @brief 用于管理Lua对象引用的函数组
 * @{
 */

/**
 * @brief 创建对象引用
 *
 * 详细说明：
 * 为栈顶的对象在指定表中创建一个引用，返回唯一的引用ID。引用系统
 * 是Lua C API中用于在C代码中持久化保存Lua对象的核心机制，解决了
 * 垃圾回收环境下的对象生命周期管理问题。
 *
 * 引用机制原理：
 * - 将栈顶对象存储到指定的引用表中
 * - 使用整数作为键，对象作为值
 * - 返回唯一的整数引用ID作为访问凭证
 * - 对象被引用期间不会被垃圾回收
 * - 引用表通常使用注册表（LUA_REGISTRYINDEX）
 *
 * 引用ID分配：
 * - 正整数：有效的引用ID，从1开始递增
 * - LUA_REFNIL：对象为nil时的特殊返回值
 * - LUA_NOREF：引用创建失败时的错误返回值
 * - 引用ID在表的生命周期内保持唯一
 *
 * 内存管理：
 * - 被引用的对象不会被垃圾回收
 * - 引用表本身受垃圾回收管理
 * - 必须配对使用luaL_ref和luaL_unref
 * - 忘记释放引用会导致内存泄漏
 *
 * 线程安全：
 * - 在单线程Lua环境中是安全的
 * - 多线程环境需要额外的同步机制
 * - 引用ID在不同线程间不可共享
 *
 * 性能考虑：
 * - 引用创建：O(1)时间复杂度
 * - 空间开销：每个引用占用一个表项
 * - 垃圾回收影响：减少GC压力但增加内存使用
 * - 大量引用时考虑使用弱引用表
 *
 * 错误处理：
 * - 栈为空：返回LUA_NOREF
 * - 表索引无效：可能导致运行时错误
 * - 内存不足：抛出内存分配错误
 * - 引用表不是表类型：导致未定义行为
 *
 * 使用场景：
 * - 回调函数的持久化存储
 * - 用户数据的关联对象
 * - 事件处理器的注册
 * - 配置对象的缓存
 * - 跨C函数调用的对象传递
 *
 * 最佳实践：
 * - 总是检查返回值的有效性
 * - 及时释放不再需要的引用
 * - 使用注册表作为引用表
 * - 避免循环引用导致的内存泄漏
 * - 在模块卸载时清理所有引用
 *
 * 使用示例：
 * @code
 * // 存储回调函数
 * static int callback_ref = LUA_NOREF;
 *
 * static int l_set_callback(lua_State *L) {
 *     luaL_checktype(L, 1, LUA_TFUNCTION);
 *
 *     // 释放旧的回调引用
 *     if (callback_ref != LUA_NOREF) {
 *         luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
 *     }
 *
 *     // 创建新的回调引用
 *     lua_pushvalue(L, 1);  // 复制函数到栈顶
 *     callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
 *
 *     return 0;
 * }
 *
 * static int l_call_callback(lua_State *L) {
 *     if (callback_ref == LUA_NOREF) {
 *         return luaL_error(L, "no callback function set");
 *     }
 *
 *     // 获取并调用回调函数
 *     lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);
 *     lua_pushvalue(L, 1);  // 传递参数
 *
 *     if (lua_pcall(L, 1, 1, 0) != 0) {
 *         return lua_error(L);  // 传播错误
 *     }
 *
 *     return 1;  // 返回回调结果
 * }
 * @endcode
 *
 * 高级用法（对象关联）：
 * @code
 * typedef struct {
 *     int data_ref;
 *     int meta_ref;
 * } UserObject;
 *
 * static int l_create_object(lua_State *L) {
 *     UserObject *obj = lua_newuserdata(L, sizeof(UserObject));
 *
 *     // 关联数据对象
 *     lua_newtable(L);
 *     obj->data_ref = luaL_ref(L, LUA_REGISTRYINDEX);
 *
 *     // 关联元数据
 *     if (lua_gettop(L) > 1) {
 *         lua_pushvalue(L, 2);
 *         obj->meta_ref = luaL_ref(L, LUA_REGISTRYINDEX);
 *     } else {
 *         obj->meta_ref = LUA_NOREF;
 *     }
 *
 *     return 1;
 * }
 *
 * static int object_gc(lua_State *L) {
 *     UserObject *obj = lua_touserdata(L, 1);
 *
 *     // 清理引用
 *     if (obj->data_ref != LUA_NOREF) {
 *         luaL_unref(L, LUA_REGISTRYINDEX, obj->data_ref);
 *         obj->data_ref = LUA_NOREF;
 *     }
 *
 *     if (obj->meta_ref != LUA_NOREF) {
 *         luaL_unref(L, LUA_REGISTRYINDEX, obj->meta_ref);
 *         obj->meta_ref = LUA_NOREF;
 *     }
 *
 *     return 0;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 忘记释放引用导致内存泄漏
 * - 多次释放同一个引用
 * - 使用无效的引用ID
 * - 在错误的表中创建引用
 * - 假设引用ID永远有效
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] t 存储引用的表的栈索引，必须指向有效的表
 *
 * @return 引用ID
 * @retval >0 有效的引用ID，可用于后续访问对象
 * @retval LUA_REFNIL 栈顶对象为nil，未创建引用
 * @retval LUA_NOREF 引用创建失败或栈为空
 *
 * @warning 必须配对使用luaL_unref释放引用，否则导致内存泄漏
 * @warning 引用表必须是有效的表类型，否则行为未定义
 * @warning 栈顶必须有有效对象，否则可能返回LUA_NOREF
 *
 * @note 栈减少1个位置（弹出被引用的对象）
 * @note 引用ID在引用表的生命周期内保持唯一
 * @note 通常使用LUA_REGISTRYINDEX作为引用表
 * @note nil对象不会创建实际引用，返回LUA_REFNIL
 *
 * @since C89
 * @see luaL_unref(), lua_rawgeti(), LUA_REGISTRYINDEX, LUA_REFNIL, LUA_NOREF
 */
LUALIB_API int (luaL_ref) (lua_State *L, int t);

/**
 * @brief 释放对象引用
 *
 * 详细说明：
 * 释放指定的对象引用，允许对象被垃圾回收。
 * 这是luaL_ref的配对操作。
 *
 * 释放过程：
 * - 从指定表中移除引用
 * - 对象变为可垃圾回收
 * - 引用ID变为无效
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] t 存储引用的表的栈索引
 * @param[in] ref 要释放的引用ID
 *
 * @note 释放无效引用是安全的（无操作）
 * @since C89
 * @see luaL_ref(), LUA_REGISTRYINDEX
 */
LUALIB_API void (luaL_unref) (lua_State *L, int t, int ref);

/** @} */

/**
 * @name 代码加载函数
 * @brief 用于加载和编译Lua代码的函数组
 * @{
 */

/**
 * @brief 从文件加载Lua代码
 *
 * 详细说明：
 * 从指定文件加载Lua代码并编译为函数。支持源代码和字节码文件。
 * 编译后的函数会被推入栈顶。
 *
 * 文件处理：
 * - 自动检测文件类型（源码或字节码）
 * - 处理文件读取错误
 * - 支持相对和绝对路径
 * - 自动关闭文件句柄
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] filename 文件路径，不能为NULL
 *
 * @return 加载结果状态码
 * @retval 0 加载成功，函数已推入栈
 * @retval LUA_ERRSYNTAX 语法错误
 * @retval LUA_ERRMEM 内存分配错误
 * @retval LUA_ERRFILE 文件操作错误
 *
 * @note 成功时栈增长1个位置，失败时错误消息在栈顶
 * @since C89
 * @see luaL_loadstring(), luaL_loadbuffer()
 */
LUALIB_API int (luaL_loadfile) (lua_State *L, const char *filename);

/**
 * @brief 从内存缓冲区加载Lua代码
 *
 * 详细说明：
 * 从内存缓冲区加载Lua代码并编译为函数。
 * 这是最灵活的代码加载方式。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] buff 代码缓冲区指针，不能为NULL
 * @param[in] sz 缓冲区大小
 * @param[in] name 代码块名称（用于错误报告），可以为NULL
 *
 * @return 加载结果状态码
 * @retval 0 加载成功
 * @retval LUA_ERRSYNTAX 语法错误
 * @retval LUA_ERRMEM 内存分配错误
 *
 * @since C89
 * @see luaL_loadfile(), luaL_loadstring()
 */
LUALIB_API int (luaL_loadbuffer) (lua_State *L, const char *buff, size_t sz,
                                  const char *name);

/**
 * @brief 从字符串加载Lua代码
 *
 * 详细说明：
 * 从null结尾的字符串加载Lua代码并编译为函数。
 * 这是最常用的代码加载方式。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] s Lua代码字符串，不能为NULL
 *
 * @return 加载结果状态码
 * @retval 0 加载成功
 * @retval LUA_ERRSYNTAX 语法错误
 * @retval LUA_ERRMEM 内存分配错误
 *
 * @since C89
 * @see luaL_loadfile(), luaL_loadbuffer()
 */
LUALIB_API int (luaL_loadstring) (lua_State *L, const char *s);

/** @} */

/**
 * @name 状态机管理
 * @brief 用于创建和管理Lua状态机的函数
 * @{
 */

/**
 * @brief 创建新的Lua状态机
 *
 * 详细说明：
 * 创建一个新的Lua状态机，使用标准的内存分配器。
 * 这是luaL_newstate的便利版本。
 *
 * @return 新的Lua状态机指针
 * @retval 非NULL 成功创建的状态机
 * @retval NULL 内存分配失败
 *
 * @note 新状态机不包含标准库，需要手动加载
 * @since C89
 * @see lua_newstate(), lua_close()
 */
LUALIB_API lua_State *(luaL_newstate) (void);

/** @} */

/**
 * @name 字符串处理函数
 * @brief 用于字符串操作的实用函数
 * @{
 */

/**
 * @brief 全局字符串替换
 *
 * 详细说明：
 * 在字符串中查找所有匹配的模式并替换为指定字符串。
 * 结果字符串会被推入栈顶。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] s 源字符串，不能为NULL
 * @param[in] p 要查找的模式，不能为NULL
 * @param[in] r 替换字符串，不能为NULL
 *
 * @return 替换后的字符串指针
 *
 * @note 栈增长1个位置（结果字符串）
 * @since C89
 */
LUALIB_API const char *(luaL_gsub) (lua_State *L, const char *s, const char *p,
                                    const char *r);

/**
 * @brief 查找或创建嵌套表
 *
 * 详细说明：
 * 在指定表中查找嵌套的子表，如果不存在则创建。
 * 支持点分隔的路径格式。
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[in] idx 起始表的栈索引
 * @param[in] fname 字段路径（如"a.b.c"）
 * @param[in] szhint 新表的大小提示
 *
 * @return 错误信息或NULL
 * @retval NULL 操作成功
 * @retval 非NULL 错误信息字符串
 *
 * @note 目标表会被推入栈顶
 * @since C89
 */
LUALIB_API const char *(luaL_findtable) (lua_State *L, int idx,
                                         const char *fname, int szhint);

/** @} */




/**
 * @name 便利宏定义
 * @brief 简化常见操作的宏定义集合
 * @{
 */

/**
 * @brief 参数条件检查宏
 *
 * 详细说明：
 * 检查指定条件是否为真，如果为假则抛出参数错误。这是Lua C API中
 * 最重要的参数验证宏之一，提供了简洁而强大的参数检查机制。
 * 宏的设计使得参数验证代码既简洁又具有良好的可读性。
 *
 * 宏展开机制：
 * - 使用短路求值：条件为真时不调用luaL_argerror
 * - 条件为假时调用luaL_argerror抛出错误
 * - 整个表达式的结果为void，避免副作用
 * - 编译器优化：条件为常量时可能完全优化掉
 *
 * 条件表达式：
 * - 支持任何可转换为布尔值的C表达式
 * - 常用于数值范围检查
 * - 支持复合条件（使用&&、||等）
 * - 可以包含函数调用（但要注意副作用）
 *
 * 错误消息设计：
 * - 应该清晰描述期望的条件
 * - 避免使用技术术语
 * - 提供具体的约束信息
 * - 考虑国际化需求
 *
 * 性能考虑：
 * - 宏展开：无函数调用开销
 * - 短路求值：条件为真时性能最优
 * - 错误路径：条件为假时有函数调用开销
 * - 编译优化：常量条件可能被完全优化
 *
 * 使用场景：
 * - 数值范围验证
 * - 字符串长度检查
 * - 指针有效性验证
 * - 业务逻辑约束检查
 * - 资源状态验证
 *
 * 最佳实践：
 * - 条件表达式应该简单明了
 * - 错误消息应该具体而有用
 * - 避免在条件中使用有副作用的操作
 * - 考虑使用更具体的检查函数
 * - 保持错误消息的一致性
 *
 * 基础使用示例：
 * @code
 * static int l_factorial(lua_State *L) {
 *     int n = luaL_checkint(L, 1);
 *     luaL_argcheck(L, n >= 0, 1, "non-negative number expected");
 *     luaL_argcheck(L, n <= 20, 1, "number too large (maximum 20)");
 *
 *     // 计算阶乘
 *     long result = 1;
 *     for (int i = 2; i <= n; i++) {
 *         result *= i;
 *     }
 *
 *     lua_pushinteger(L, result);
 *     return 1;
 * }
 * @endcode
 *
 * 高级使用示例：
 * @code
 * static int l_substring(lua_State *L) {
 *     size_t len;
 *     const char *str = luaL_checklstring(L, 1, &len);
 *     int start = luaL_checkint(L, 2);
 *     int end = luaL_optint(L, 3, len);
 *
 *     // 参数范围检查
 *     luaL_argcheck(L, start >= 1, 2, "start position must be >= 1");
 *     luaL_argcheck(L, start <= (int)len, 2, "start position out of range");
 *     luaL_argcheck(L, end >= start, 3, "end position must be >= start");
 *     luaL_argcheck(L, end <= (int)len, 3, "end position out of range");
 *
 *     // 提取子字符串
 *     lua_pushlstring(L, str + start - 1, end - start + 1);
 *     return 1;
 * }
 * @endcode
 *
 * 复合条件示例：
 * @code
 * static int l_set_color(lua_State *L) {
 *     int r = luaL_checkint(L, 1);
 *     int g = luaL_checkint(L, 2);
 *     int b = luaL_checkint(L, 3);
 *
 *     // RGB值范围检查
 *     luaL_argcheck(L, r >= 0 && r <= 255, 1, "red value must be 0-255");
 *     luaL_argcheck(L, g >= 0 && g <= 255, 2, "green value must be 0-255");
 *     luaL_argcheck(L, b >= 0 && b <= 255, 3, "blue value must be 0-255");
 *
 *     // 设置颜色
 *     set_rgb_color(r, g, b);
 *     return 0;
 * }
 * @endcode
 *
 * 字符串验证示例：
 * @code
 * static int l_open_file(lua_State *L) {
 *     size_t len;
 *     const char *filename = luaL_checklstring(L, 1, &len);
 *     const char *mode = luaL_optstring(L, 2, "r");
 *
 *     // 文件名验证
 *     luaL_argcheck(L, len > 0, 1, "filename cannot be empty");
 *     luaL_argcheck(L, len < 260, 1, "filename too long");
 *     luaL_argcheck(L, strchr(filename, '\0') == filename + len, 1,
 *                   "filename contains null character");
 *
 *     // 模式验证
 *     luaL_argcheck(L, strcmp(mode, "r") == 0 || strcmp(mode, "w") == 0 ||
 *                   strcmp(mode, "a") == 0, 2, "invalid file mode");
 *
 *     // 打开文件
 *     FILE *f = fopen(filename, mode);
 *     if (!f) {
 *         return luaL_error(L, "cannot open file: %s", strerror(errno));
 *     }
 *
 *     // 返回文件句柄
 *     lua_pushlightuserdata(L, f);
 *     return 1;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 在条件表达式中使用有副作用的操作
 * - 错误消息过于简单或模糊
 * - 忘记条件为假时函数不会返回
 * - 条件表达式过于复杂影响可读性
 * - 不一致的错误消息格式
 *
 * @param L Lua状态机指针，不能为NULL
 * @param cond 要检查的条件表达式，为假时抛出错误
 * @param numarg 参数位置（从1开始），用于错误报告
 * @param extramsg 错误消息字符串，不能为NULL
 *
 * @warning 条件为假时会抛出Lua错误，函数不会返回
 * @warning 条件表达式不应包含有副作用的操作
 * @warning 错误消息字符串的生命周期必须足够长
 *
 * @note 这是一个宏，会在编译时展开
 * @note 使用短路求值，条件为真时不调用错误函数
 * @note 整个表达式的结果为void类型
 *
 * @since C89
 * @see luaL_argerror(), luaL_typerror(), luaL_error()
 */
#define luaL_argcheck(L, cond,numarg,extramsg) \
    ((void)((cond) || luaL_argerror(L, (numarg), (extramsg))))

/**
 * @brief 检查字符串参数（不返回长度）
 *
 * 详细说明：
 * luaL_checklstring的简化版本，不需要获取字符串长度时使用。
 *
 * @param L Lua状态机指针
 * @param n 参数位置
 * @return 字符串指针
 *
 * @since C89
 * @see luaL_checklstring()
 */
#define luaL_checkstring(L,n)   (luaL_checklstring(L, (n), NULL))

/**
 * @brief 获取可选字符串参数（不返回长度）
 *
 * 详细说明：
 * luaL_optlstring的简化版本，不需要获取字符串长度时使用。
 *
 * @param L Lua状态机指针
 * @param n 参数位置
 * @param d 默认值
 * @return 字符串指针或默认值
 *
 * @since C89
 * @see luaL_optlstring()
 */
#define luaL_optstring(L,n,d)   (luaL_optlstring(L, (n), (d), NULL))

/**
 * @brief 检查int类型参数
 *
 * 详细说明：
 * 检查参数并转换为int类型。这是luaL_checkinteger的类型转换版本。
 *
 * @param L Lua状态机指针
 * @param n 参数位置
 * @return int类型的值
 *
 * @since C89
 * @see luaL_checkinteger()
 */
#define luaL_checkint(L,n)      ((int)luaL_checkinteger(L, (n)))

/**
 * @brief 获取可选int类型参数
 *
 * 详细说明：
 * 获取可选参数并转换为int类型。
 *
 * @param L Lua状态机指针
 * @param n 参数位置
 * @param d 默认值
 * @return int类型的值或默认值
 *
 * @since C89
 * @see luaL_optinteger()
 */
#define luaL_optint(L,n,d)      ((int)luaL_optinteger(L, (n), (d)))

/**
 * @brief 检查long类型参数
 *
 * 详细说明：
 * 检查参数并转换为long类型。
 *
 * @param L Lua状态机指针
 * @param n 参数位置
 * @return long类型的值
 *
 * @since C89
 * @see luaL_checkinteger()
 */
#define luaL_checklong(L,n)     ((long)luaL_checkinteger(L, (n)))

/**
 * @brief 获取可选long类型参数
 *
 * 详细说明：
 * 获取可选参数并转换为long类型。
 *
 * @param L Lua状态机指针
 * @param n 参数位置
 * @param d 默认值
 * @return long类型的值或默认值
 *
 * @since C89
 * @see luaL_optinteger()
 */
#define luaL_optlong(L,n,d)     ((long)luaL_optinteger(L, (n), (d)))

/**
 * @brief 获取值的类型名称
 *
 * 详细说明：
 * 获取栈中指定位置值的类型名称字符串。
 *
 * @param L Lua状态机指针
 * @param i 栈索引
 * @return 类型名称字符串
 *
 * @since C89
 * @see lua_typename(), lua_type()
 */
#define luaL_typename(L,i)      lua_typename(L, lua_type(L,(i)))

/**
 * @brief 执行Lua文件
 *
 * 详细说明：
 * 加载并执行指定的Lua文件。这是一个便利宏，
 * 组合了加载和执行操作。
 *
 * @param L Lua状态机指针
 * @param fn 文件名
 * @return 执行结果（0表示成功）
 *
 * @since C89
 * @see luaL_loadfile(), lua_pcall()
 */
#define luaL_dofile(L, fn) \
    (luaL_loadfile(L, fn) || lua_pcall(L, 0, LUA_MULTRET, 0))

/**
 * @brief 执行Lua字符串
 *
 * 详细说明：
 * 加载并执行指定的Lua代码字符串。这是一个便利宏，
 * 组合了加载和执行操作。
 *
 * @param L Lua状态机指针
 * @param s Lua代码字符串
 * @return 执行结果（0表示成功）
 *
 * @since C89
 * @see luaL_loadstring(), lua_pcall()
 */
#define luaL_dostring(L, s) \
    (luaL_loadstring(L, s) || lua_pcall(L, 0, LUA_MULTRET, 0))

/**
 * @brief 获取注册表中的元表
 *
 * 详细说明：
 * 从注册表中获取指定名称的元表。这是luaL_newmetatable的配对操作。
 *
 * @param L Lua状态机指针
 * @param n 元表名称
 * @return 是否找到元表（lua_getfield的返回值）
 *
 * @since C89
 * @see luaL_newmetatable(), lua_getfield()
 */
#define luaL_getmetatable(L,n)  (lua_getfield(L, LUA_REGISTRYINDEX, (n)))

/**
 * @brief 可选参数处理宏
 *
 * 详细说明：
 * 如果参数存在则调用指定函数处理，否则返回默认值。
 * 这是一个通用的可选参数处理模式。
 *
 * @param L Lua状态机指针
 * @param f 处理函数
 * @param n 参数位置
 * @param d 默认值
 * @return 处理结果或默认值
 *
 * @since C89
 * @see lua_isnoneornil()
 */
#define luaL_opt(L,f,n,d)       (lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

/** @} */



/**
 * @name 字符串缓冲区系统
 * @brief 高效的字符串构建和操作系统
 * @{
 */

/**
 * @brief 字符串缓冲区结构体：用于高效构建字符串
 *
 * 详细说明：
 * luaL_Buffer提供了一个高效的字符串构建机制，避免了频繁的
 * 内存分配和字符串连接操作。它使用内部缓冲区和Lua栈来
 * 管理字符串片段。
 *
 * 结构体成员：
 * - p: 当前写入位置指针
 * - lvl: 栈中字符串片段的数量
 * - L: 关联的Lua状态机
 * - buffer: 内部缓冲区
 *
 * 工作原理：
 * 1. 小的字符串片段写入内部缓冲区
 * 2. 缓冲区满时，内容推入Lua栈
 * 3. 最终合并所有片段为完整字符串
 *
 * 使用模式：
 * @code
 * luaL_Buffer b;
 * luaL_buffinit(L, &b);
 * luaL_addstring(&b, "Hello ");
 * luaL_addstring(&b, "World");
 * luaL_addchar(&b, '!');
 * luaL_pushresult(&b);  // 结果在栈顶
 * @endcode
 *
 * 性能优势：
 * - 减少内存分配次数
 * - 避免中间字符串的创建
 * - 利用Lua的字符串内部化机制
 * - 支持任意长度的字符串构建
 *
 * @since C89
 * @see luaL_buffinit(), luaL_pushresult()
 */
typedef struct luaL_Buffer {
    char *p;                        /**< 当前写入位置指针 */
    int lvl;                        /**< 栈中字符串片段数量 */
    lua_State *L;                   /**< 关联的Lua状态机 */
    char buffer[LUAL_BUFFERSIZE];   /**< 内部缓冲区 */
} luaL_Buffer;

/**
 * @brief 向缓冲区添加单个字符
 *
 * 详细说明：
 * 高效地向缓冲区添加单个字符。这是缓冲区系统中最基础的操作，
 * 针对单字符添加进行了优化。如果缓冲区已满，会自动调用
 * luaL_prepbuffer进行扩展，确保操作总是成功。
 *
 * 实现细节：
 * - 首先检查缓冲区是否有足够空间（边界检查）
 * - 如果空间不足，自动调用luaL_prepbuffer扩展缓冲区
 * - 将字符写入当前位置并原子性地移动指针
 * - 整个操作是线程安全的（在单线程Lua环境中）
 *
 * 性能优化：
 * - 宏实现，无函数调用开销
 * - 内联边界检查，大多数情况下只需要一次比较
 * - 指针操作，避免数组索引计算
 * - 缓冲区满时才调用函数，摊销成本低
 *
 * 内存管理：
 * - 自动处理缓冲区扩展
 * - 无需手动内存分配
 * - 缓冲区满时内容推入Lua栈
 * - 利用Lua的垃圾回收机制
 *
 * 错误处理：
 * - 缓冲区扩展失败时抛出内存错误
 * - 栈溢出时抛出相应错误
 * - 无效缓冲区指针导致未定义行为
 *
 * 使用场景：
 * - 逐字符构建字符串
 * - 字符串转义处理
 * - 格式化输出的字符级控制
 * - 流式数据处理
 *
 * 最佳实践：
 * - 在循环中使用时考虑批量操作
 * - 避免添加null字符（除非有特殊需求）
 * - 结合其他缓冲区函数使用
 * - 注意字符编码问题
 *
 * 使用示例：
 * @code
 * static int l_escape_string(lua_State *L) {
 *     size_t len;
 *     const char *str = luaL_checklstring(L, 1, &len);
 *
 *     luaL_Buffer b;
 *     luaL_buffinit(L, &b);
 *
 *     luaL_addchar(&b, '"');  // 开始引号
 *
 *     for (size_t i = 0; i < len; i++) {
 *         char c = str[i];
 *         switch (c) {
 *             case '"':
 *                 luaL_addchar(&b, '\\');
 *                 luaL_addchar(&b, '"');
 *                 break;
 *             case '\\':
 *                 luaL_addchar(&b, '\\');
 *                 luaL_addchar(&b, '\\');
 *                 break;
 *             case '\n':
 *                 luaL_addchar(&b, '\\');
 *                 luaL_addchar(&b, 'n');
 *                 break;
 *             case '\t':
 *                 luaL_addchar(&b, '\\');
 *                 luaL_addchar(&b, 't');
 *                 break;
 *             default:
 *                 luaL_addchar(&b, c);
 *                 break;
 *         }
 *     }
 *
 *     luaL_addchar(&b, '"');  // 结束引号
 *     luaL_pushresult(&b);
 *     return 1;
 * }
 * @endcode
 *
 * 高性能用法：
 * @code
 * static int l_repeat_char(lua_State *L) {
 *     int c = luaL_checkint(L, 1);
 *     int count = luaL_checkint(L, 2);
 *
 *     luaL_argcheck(L, count >= 0, 2, "count must be non-negative");
 *     luaL_argcheck(L, c >= 0 && c <= 255, 1, "character out of range");
 *
 *     luaL_Buffer b;
 *     luaL_buffinit(L, &b);
 *
 *     // 高效的字符重复
 *     for (int i = 0; i < count; i++) {
 *         luaL_addchar(&b, (char)c);
 *     }
 *
 *     luaL_pushresult(&b);
 *     return 1;
 * }
 * @endcode
 *
 * 常见陷阱：
 * - 添加null字符可能导致字符串截断
 * - 在多字节字符编码中逐字节处理可能破坏字符
 * - 忘记调用luaL_pushresult完成构建
 * - 在错误处理中使用已损坏的缓冲区
 *
 * @param B 缓冲区指针，必须是有效的luaL_Buffer指针
 * @param c 要添加的字符，值范围0-255
 *
 * @warning 缓冲区指针必须有效，否则导致未定义行为
 * @warning 字符值应在有效范围内，避免符号扩展问题
 * @warning 缓冲区扩展失败时会抛出Lua错误
 *
 * @note 这是一个宏，执行效率极高
 * @note 自动处理缓冲区扩展，无需手动检查空间
 * @note 支持任意字符值，包括null字符
 *
 * @since C89
 * @see luaL_prepbuffer(), luaL_addstring(), luaL_addlstring(), luaL_buffinit()
 */
#define luaL_addchar(B,c) \
    ((void)((B)->p < ((B)->buffer+LUAL_BUFFERSIZE) || luaL_prepbuffer(B)), \
     (*(B)->p++ = (char)(c)))

/**
 * @brief 向缓冲区添加字符（兼容性宏）
 *
 * 详细说明：
 * 这是luaL_addchar的别名，为了向后兼容而保留。
 *
 * @param B 缓冲区指针
 * @param c 要添加的字符
 *
 * @deprecated 建议使用luaL_addchar
 * @since C89
 * @see luaL_addchar()
 */
#define luaL_putchar(B,c)       luaL_addchar(B,c)

/**
 * @brief 调整缓冲区写入位置
 *
 * 详细说明：
 * 手动调整缓冲区的写入位置指针。这通常在直接操作
 * 缓冲区内存后使用，以更新正确的位置。
 *
 * 使用场景：
 * - 使用sprintf等函数直接写入缓冲区后
 * - 批量复制数据到缓冲区后
 * - 需要精确控制写入位置时
 *
 * @param B 缓冲区指针
 * @param n 位置偏移量
 *
 * @warning 确保不会超出缓冲区边界
 * @since C89
 * @see luaL_prepbuffer()
 */
#define luaL_addsize(B,n)       ((B)->p += (n))

/**
 * @brief 初始化字符串缓冲区
 *
 * 详细说明：
 * 初始化luaL_Buffer结构体，准备进行字符串构建操作。
 * 这是使用缓冲区系统的第一步。
 *
 * 初始化过程：
 * - 设置关联的Lua状态机
 * - 重置写入位置指针
 * - 清空栈级别计数
 * - 准备内部缓冲区
 *
 * @param[in] L Lua状态机指针，不能为NULL
 * @param[out] B 要初始化的缓冲区，不能为NULL
 *
 * @note 必须在使用其他缓冲区函数前调用
 * @since C89
 * @see luaL_pushresult(), luaL_Buffer
 */
LUALIB_API void (luaL_buffinit) (lua_State *L, luaL_Buffer *B);

/**
 * @brief 准备缓冲区空间
 *
 * 详细说明：
 * 确保缓冲区有足够的空间进行写入。如果当前缓冲区已满，
 * 会将内容推入Lua栈并重置缓冲区。
 *
 * 空间管理：
 * - 检查当前缓冲区剩余空间
 * - 如果空间不足，推入栈并重置
 * - 返回可写入的缓冲区指针
 * - 保证至少有LUAL_BUFFERSIZE字节可用
 *
 * @param[in,out] B 缓冲区指针，不能为NULL
 *
 * @return 可写入的缓冲区指针
 * @retval 非NULL 指向可写入区域的指针
 *
 * @note 通常不需要直接调用，由其他函数自动调用
 * @since C89
 * @see luaL_addchar(), luaL_addsize()
 */
LUALIB_API char *(luaL_prepbuffer) (luaL_Buffer *B);

/**
 * @brief 向缓冲区添加指定长度的字符串
 *
 * 详细说明：
 * 向缓冲区添加指定长度的字符串数据。可以处理包含
 * 嵌入null字符的二进制数据。
 *
 * @param[in,out] B 缓冲区指针，不能为NULL
 * @param[in] s 字符串数据指针，不能为NULL
 * @param[in] l 字符串长度
 *
 * @since C89
 * @see luaL_addstring(), luaL_addchar()
 */
LUALIB_API void (luaL_addlstring) (luaL_Buffer *B, const char *s, size_t l);

/**
 * @brief 向缓冲区添加null结尾的字符串
 *
 * 详细说明：
 * 向缓冲区添加以null字符结尾的C字符串。
 * 这是最常用的字符串添加函数。
 *
 * @param[in,out] B 缓冲区指针，不能为NULL
 * @param[in] s 字符串指针，不能为NULL
 *
 * @since C89
 * @see luaL_addlstring(), luaL_addchar()
 */
LUALIB_API void (luaL_addstring) (luaL_Buffer *B, const char *s);

/**
 * @brief 向缓冲区添加栈顶的值
 *
 * 详细说明：
 * 将栈顶的值转换为字符串并添加到缓冲区。
 * 值会从栈中弹出。
 *
 * @param[in,out] B 缓冲区指针，不能为NULL
 *
 * @note 栈减少1个位置（弹出值）
 * @since C89
 * @see luaL_addstring(), lua_tostring()
 */
LUALIB_API void (luaL_addvalue) (luaL_Buffer *B);

/**
 * @brief 完成字符串构建并推入结果
 *
 * 详细说明：
 * 完成字符串构建过程，将所有片段合并为最终字符串
 * 并推入Lua栈顶。这是缓冲区操作的最后一步。
 *
 * 完成过程：
 * - 将当前缓冲区内容推入栈
 * - 合并栈中的所有字符串片段
 * - 将最终结果推入栈顶
 * - 清理中间数据
 *
 * @param[in,out] B 缓冲区指针，不能为NULL
 *
 * @note 栈增长1个位置（最终字符串）
 * @note 调用后缓冲区变为无效状态
 * @since C89
 * @see luaL_buffinit(), luaL_Buffer
 */
LUALIB_API void (luaL_pushresult) (luaL_Buffer *B);

/** @} */

/**
 * @name 引用系统常量
 * @brief 引用系统使用的特殊常量
 * @{
 */

/**
 * @brief 无效引用常量
 *
 * 详细说明：
 * 表示无效或不存在的引用。当引用操作失败时返回此值。
 *
 * @since C89
 * @see luaL_ref(), luaL_unref()
 */
#define LUA_NOREF               (-2)

/**
 * @brief nil引用常量
 *
 * 详细说明：
 * 表示对nil值的引用。当尝试引用nil值时返回此常量。
 *
 * @since C89
 * @see luaL_ref(), luaL_unref()
 */
#define LUA_REFNIL              (-1)

/** @} */

/**
 * @name 兼容性宏定义
 * @brief 提供与旧版本API的兼容性
 * @{
 */

/**
 * @brief 创建引用（兼容性宏）
 *
 * 详细说明：
 * 这是为了与旧版本Lua兼容而保留的宏。在新代码中应该
 * 直接使用luaL_ref函数。
 *
 * @param L Lua状态机指针
 * @param lock 锁定标志（已废弃）
 * @return 引用ID或错误
 *
 * @deprecated 建议直接使用luaL_ref
 * @warning lock参数为0时会抛出错误
 * @since C89
 * @see luaL_ref()
 */
#define lua_ref(L,lock) ((lock) ? luaL_ref(L, LUA_REGISTRYINDEX) : \
    (lua_pushstring(L, "unlocked references are obsolete"), lua_error(L), 0))

/**
 * @brief 释放引用（兼容性宏）
 *
 * 详细说明：
 * 这是luaL_unref的便利宏，自动使用注册表作为引用表。
 *
 * @param L Lua状态机指针
 * @param ref 要释放的引用ID
 *
 * @since C89
 * @see luaL_unref()
 */
#define lua_unref(L,ref)        luaL_unref(L, LUA_REGISTRYINDEX, (ref))

/**
 * @brief 获取引用的值（兼容性宏）
 *
 * 详细说明：
 * 通过引用ID获取对应的值，自动使用注册表作为引用表。
 *
 * @param L Lua状态机指针
 * @param ref 引用ID
 *
 * @since C89
 * @see lua_rawgeti(), LUA_REGISTRYINDEX
 */
#define lua_getref(L,ref)       lua_rawgeti(L, LUA_REGISTRYINDEX, (ref))

/**
 * @brief 库注册结构体别名（兼容性）
 *
 * 详细说明：
 * 这是luaL_Reg的别名，为了与旧代码兼容而保留。
 *
 * @deprecated 建议使用luaL_Reg
 * @since C89
 * @see luaL_Reg
 */
#define luaL_reg                luaL_Reg

/** @} */

#endif


