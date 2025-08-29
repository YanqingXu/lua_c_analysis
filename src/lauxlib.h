/*
** [核心] Lua 辅助库头文件 - lauxlib.h
**
** 本文件定义了用于构建 Lua 库的辅助函数和宏定义。
** 这些函数简化了 C 代码与 Lua 虚拟机的交互，提供了类型检查、
** 错误处理、缓冲区管理等常用功能。
**
** 主要功能模块：
** - 参数检查和类型验证函数
** - 错误处理和报告函数  
** - 库注册和元表管理
** - 代码加载和执行函数
** - 字符串缓冲区操作
** - 引用系统支持
**
** 设计理念：
** - 提供安全的类型检查机制
** - 简化常见的 C/Lua 交互模式
** - 统一错误处理和报告方式
** - 支持库的模块化注册
*/

#ifndef lauxlib_h
#define lauxlib_h

// 标准C库头文件
#include <stddef.h>   // 定义 size_t 等基础类型
#include <stdio.h>    // 文件操作函数

// Lua核心头文件
#include "lua.h"      // Lua虚拟机核心API

/*
** [兼容] Lua 5.0 兼容性支持
**
** 为了向后兼容，保留了 Lua 5.0 中的 getn/setn 函数。
** 在新版本中，这些函数的功能已经被 lua_objlen 替代。
*/
#if defined(LUA_COMPAT_GETN)
// 获取表长度的兼容函数声明
LUALIB_API int (luaL_getn) (lua_State *L, int t);
// 设置表长度的兼容函数声明  
LUALIB_API void (luaL_setn) (lua_State *L, int t, int n);
#else
// 现代版本：使用 lua_objlen 获取对象长度
#define luaL_getn(L,i)          ((int)lua_objlen(L, i))
// 现代版本：设置长度操作已不需要（空操作）
#define luaL_setn(L,i,j)        ((void)0)  // 空操作，无实际功能
#endif

/*
** [兼容] 库打开函数别名
** 为旧版本代码提供函数名称兼容性
*/
#if defined(LUA_COMPAT_OPENLIB)
#define luaI_openlib	luaL_openlib
#endif

/*
** [错误] 文件加载错误码定义
** 
** LUA_ERRFILE: 专门用于 luaL_load 系列函数的文件错误
** 该错误码在标准 Lua 错误码基础上扩展
*/
#define LUA_ERRFILE     (LUA_ERRERR+1)

/*
** [核心] 库函数注册结构体
**
** 用于批量注册 C 函数到 Lua 环境中。
** 这是 Lua 库模块化的基础数据结构。
**
** 结构说明：
** @field name - const char*: 函数在 Lua 中的名称
** @field func - lua_CFunction: 对应的 C 函数指针
**
** 使用场景：
** - 创建标准库模块
** - 注册用户自定义函数集
** - 批量导出 C API 到 Lua
*/
typedef struct luaL_Reg
{
    const char *name;      // 函数名称（Lua中的标识符）
    lua_CFunction func;    // C函数指针
} luaL_Reg;


/*
** ====================================================================
** [核心] 库注册和管理函数
** ====================================================================
*/

/*
** [兼容] 内部库打开函数
** 
** 功能：打开并注册一个 C 库到 Lua 环境
** @param L - lua_State*: Lua状态机
** @param libname - const char*: 库名称，如果为NULL则注册到全局环境
** @param l - const luaL_Reg*: 函数注册表数组
** @param nup - int: 每个函数关联的上值数量
*/
LUALIB_API void (luaI_openlib) (lua_State *L, 
                                const char *libname,
                                const luaL_Reg *l, 
                                int nup);

/*
** [核心] 注册库函数到指定表中
**
** 功能：将函数数组批量注册到指定的库表中
** @param L - lua_State*: Lua状态机指针
** @param libname - const char*: 库名称，NULL表示注册到栈顶表
** @param l - const luaL_Reg*: 函数注册表，以{NULL,NULL}结尾
**
** 执行流程：
** 1. 如果libname不为空，创建或获取对应的库表
** 2. 遍历注册表，将每个函数添加到表中
** 3. 将库表留在栈顶
*/
LUALIB_API void (luaL_register) (lua_State *L, 
                                 const char *libname,
                                 const luaL_Reg *l);

/*
** ====================================================================
** [核心] 元表操作函数
** ====================================================================
*/

/*
** [核心] 获取对象的元表字段
**
** 功能：获取指定对象元表中的特定字段值
** @param L - lua_State*: Lua状态机
** @param obj - int: 对象在栈中的索引
** @param e - const char*: 要获取的元表字段名
** @return int: 如果字段存在返回非0，否则返回0
**
** 执行步骤：
** 1. 获取对象的元表
** 2. 在元表中查找指定字段
** 3. 如果找到，将字段值压入栈顶
*/
LUALIB_API int (luaL_getmetafield) (lua_State *L, 
                                    int obj, 
                                    const char *e);

/*
** [核心] 调用对象的元方法
**
** 功能：调用指定对象的特定元方法
** @param L - lua_State*: Lua状态机
** @param obj - int: 对象在栈中的索引  
** @param e - const char*: 元方法名称（如"__add", "__index"等）
** @return int: 如果元方法存在并被调用返回非0，否则返回0
**
** 应用场景：
** - 实现运算符重载
** - 自定义对象行为
** - 元表继承机制
*/
LUALIB_API int (luaL_callmeta) (lua_State *L, 
                                int obj, 
                                const char *e);
/*
** ====================================================================
** [核心] 错误处理和参数检查函数
** ====================================================================
*/

/*
** [错误] 类型错误报告
**
** 功能：报告函数参数类型错误并抛出异常
** @param L - lua_State*: Lua状态机
** @param narg - int: 出错参数的位置（从1开始）
** @param tname - const char*: 期望的类型名称
** @return int: 此函数不会返回，总是抛出异常
**
** 错误消息格式：
** "bad argument #<narg> (<tname> expected, got <actual_type>)"
*/
LUALIB_API int (luaL_typerror) (lua_State *L, 
                                int narg, 
                                const char *tname);

/*
** [错误] 参数错误报告
**
** 功能：报告函数参数错误并抛出异常
** @param L - lua_State*: Lua状态机  
** @param numarg - int: 出错参数位置
** @param extramsg - const char*: 附加错误信息
** @return int: 此函数不会返回，总是抛出异常
**
** 使用场景：
** - 参数值超出有效范围
** - 参数组合不合法
** - 自定义参数验证失败
*/
LUALIB_API int (luaL_argerror) (lua_State *L, 
                                int numarg, 
                                const char *extramsg);

/*
** ====================================================================
** [核心] 字符串参数检查函数
** ====================================================================
*/

/*
** [核心] 检查并获取字符串参数
**
** 功能：检查指定位置参数是否为字符串，并返回其内容
** @param L - lua_State*: Lua状态机
** @param numArg - int: 参数在栈中的位置
** @param l - size_t*: 输出参数，存储字符串长度（可为NULL）
** @return const char*: 字符串内容指针
**
** 行为说明：
** - 如果参数不是字符串，抛出类型错误
** - 支持数字到字符串的自动转换
** - 返回的指针在下次GC前有效
*/
LUALIB_API const char *(luaL_checklstring) (lua_State *L, 
                                            int numArg,
                                            size_t *l);

/*
** [核心] 可选字符串参数获取
**
** 功能：获取可选的字符串参数，如果不存在则使用默认值
** @param L - lua_State*: Lua状态机
** @param numArg - int: 参数位置
** @param def - const char*: 默认值（参数为nil或不存在时使用）
** @param l - size_t*: 输出字符串长度（可为NULL）
** @return const char*: 字符串内容或默认值
**
** 适用场景：
** - 函数的可选参数处理
** - 提供合理的默认行为
** - 简化参数验证逻辑
*/
LUALIB_API const char *(luaL_optlstring) (lua_State *L, 
                                          int numArg,
                                          const char *def, 
                                          size_t *l);

/*
** ====================================================================
** [核心] 数值参数检查函数  
** ====================================================================
*/

/*
** [核心] 检查并获取数值参数
**
** 功能：检查指定位置参数是否为数值，并返回其值
** @param L - lua_State*: Lua状态机
** @param numArg - int: 参数位置
** @return lua_Number: 数值参数的值
**
** 类型转换：
** - 支持整数到浮点数的自动转换
** - 支持数值字符串的解析
** - 不支持其他类型，会抛出类型错误
*/
LUALIB_API lua_Number (luaL_checknumber) (lua_State *L, 
                                          int numArg);

/*
** [核心] 可选数值参数获取
**
** 功能：获取可选的数值参数，不存在时使用默认值
** @param L - lua_State*: Lua状态机
** @param nArg - int: 参数位置
** @param def - lua_Number: 默认数值
** @return lua_Number: 参数值或默认值
*/
LUALIB_API lua_Number (luaL_optnumber) (lua_State *L, 
                                        int nArg, 
                                        lua_Number def);

/*
** [核心] 检查并获取整数参数
**
** 功能：检查指定位置参数是否为整数，并返回其值
** @param L - lua_State*: Lua状态机
** @param numArg - int: 参数位置
** @return lua_Integer: 整数参数的值
**
** 精度处理：
** - 浮点数会被截断为整数
** - 超出整数范围的值可能导致精度丢失
** - 建议在关键场景下进行范围检查
*/
LUALIB_API lua_Integer (luaL_checkinteger) (lua_State *L, 
                                            int numArg);

/*
** [核心] 可选整数参数获取
**
** 功能：获取可选的整数参数，不存在时使用默认值
** @param L - lua_State*: Lua状态机
** @param nArg - int: 参数位置
** @param def - lua_Integer: 默认整数值
** @return lua_Integer: 参数值或默认值
*/
LUALIB_API lua_Integer (luaL_optinteger) (lua_State *L, 
                                          int nArg,
                                          lua_Integer def);

/*
** ====================================================================
** [核心] 栈和类型验证函数
** ====================================================================
*/

/*
** [核心] 检查栈空间是否充足
**
** 功能：确保栈中有足够空间容纳指定数量的元素
** @param L - lua_State*: Lua状态机
** @param sz - int: 需要的额外栈空间大小
** @param msg - const char*: 空间不足时的错误消息
**
** 安全机制：
** - 如果空间不足，自动抛出错误并显示自定义消息
** - 避免栈溢出导致的程序崩溃
** - 是防御性编程的重要工具
**
** 使用时机：
** - 在大量压栈操作前
** - 递归函数中
** - 处理大型数据结构时
*/
LUALIB_API void (luaL_checkstack) (lua_State *L, 
                                   int sz, 
                                   const char *msg);

/*
** [核心] 检查参数类型
**
** 功能：验证指定位置的参数是否为期望的类型
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置（从1开始）
** @param t - int: 期望的类型（LUA_TSTRING, LUA_TNUMBER等）
**
** 类型常量：
** - LUA_TNIL: nil类型
** - LUA_TBOOLEAN: 布尔类型
** - LUA_TNUMBER: 数值类型  
** - LUA_TSTRING: 字符串类型
** - LUA_TTABLE: 表类型
** - LUA_TFUNCTION: 函数类型
** - LUA_TUSERDATA: 用户数据类型
** - LUA_TTHREAD: 线程类型
*/
LUALIB_API void (luaL_checktype) (lua_State *L, 
                                  int narg, 
                                  int t);

/*
** [核心] 检查参数是否存在
**
** 功能：验证指定位置存在参数（不是nil且不超出参数范围）
** @param L - lua_State*: Lua状态机  
** @param narg - int: 参数位置
**
** 使用场景：
** - 验证必需参数的存在性
** - 防止访问不存在的参数
** - 确保函数调用的完整性
*/
LUALIB_API void (luaL_checkany) (lua_State *L, 
                                 int narg);

/*
** ====================================================================
** [核心] 元表和用户数据管理
** ====================================================================
*/

/*
** [核心] 创建新元表
**
** 功能：在注册表中创建一个新的元表
** @param L - lua_State*: Lua状态机
** @param tname - const char*: 元表的唯一标识名称
** @return int: 如果是新创建返回1，如果已存在返回0
**
** 工作机制：
** 1. 在注册表中查找指定名称的元表
** 2. 如果不存在，创建新元表并注册
** 3. 将元表压入栈顶
** 4. 返回是否为新创建
**
** 命名约定：
** - 使用模块名作为前缀，如"mylib.Point"
** - 确保全局唯一性
** - 避免与标准库冲突
*/
LUALIB_API int (luaL_newmetatable) (lua_State *L, 
                                    const char *tname);

/*
** [核心] 检查用户数据类型
**
** 功能：检查指定位置是否为特定类型的用户数据
** @param L - lua_State*: Lua状态机
** @param ud - int: 用户数据在栈中的位置
** @param tname - const char*: 期望的用户数据类型名称
** @return void*: 用户数据的指针，类型不匹配时抛出错误
**
** 验证流程：
** 1. 检查指定位置是否为用户数据
** 2. 获取其元表并与注册的类型元表比较
** 3. 如果匹配，返回数据指针
** 4. 如果不匹配，抛出类型错误
**
** 类型安全：
** - 防止错误的类型转换
** - 确保C结构体的访问安全
** - 实现类型化的用户数据系统
*/
LUALIB_API void *(luaL_checkudata) (lua_State *L, 
                                    int ud, 
                                    const char *tname);

/*
** ====================================================================
** [核心] 错误位置和错误处理
** ====================================================================
*/

/*
** [调试] 获取错误发生位置
**
** 功能：将错误发生的位置信息压入栈顶
** @param L - lua_State*: Lua状态机
** @param lvl - int: 调用栈层级（1表示调用者，2表示调用者的调用者）
**
** 位置信息格式：
** - "filename:line:" 对于Lua代码
** - "[C]:" 对于C函数
** - "main:" 对于主代码块
**
** 用途：
** - 生成详细的错误报告
** - 调试和故障诊断
** - 提供用户友好的错误信息
*/
LUALIB_API void (luaL_where) (lua_State *L, 
                              int lvl);

/*
** [错误] 格式化错误并抛出异常
**
** 功能：使用printf风格的格式字符串生成错误消息并抛出
** @param L - lua_State*: Lua状态机
** @param fmt - const char*: 格式字符串
** @param ... - 可变参数: 格式化参数
** @return int: 此函数不会返回，总是抛出异常
**
** 特性：
** - 自动添加位置信息前缀
** - 支持标准printf格式化
** - 异常会被Lua错误处理机制捕获
**
** 使用示例：
** luaL_error(L, "invalid value: %d", value);
*/
LUALIB_API int (luaL_error) (lua_State *L, 
                             const char *fmt, 
                             ...);

/*
** ====================================================================
** [核心] 选项检查和引用管理
** ====================================================================
*/

/*
** [核心] 检查选项参数
**
** 功能：检查参数是否为预定义选项列表中的一个
** @param L - lua_State*: Lua状态机
** @param narg - int: 参数位置
** @param def - const char*: 默认选项（参数为nil时使用）
** @param lst - const char* const[]: 有效选项列表，以NULL结尾
** @return int: 选项在列表中的索引（从0开始）
**
** 应用场景：
** - 处理枚举类型参数
** - 验证配置选项
** - 实现多选一的参数接口
**
** 错误处理：
** - 如果参数不在选项列表中，抛出错误
** - 显示所有有效选项供用户参考
*/
LUALIB_API int (luaL_checkoption) (lua_State *L, 
                                   int narg, 
                                   const char *def,
                                   const char *const lst[]);

/*
** [核心] 创建引用
**
** 功能：在指定表中为栈顶值创建一个引用
** @param L - lua_State*: Lua状态机
** @param t - int: 表在栈中的位置（通常使用LUA_REGISTRYINDEX）
** @return int: 引用标识符，可用于后续访问
**
** 引用机制：
** - 防止值被垃圾回收
** - 提供稳定的访问标识符
** - 支持在C代码中长期持有Lua值
**
** 注意事项：
** - 必须调用luaL_unref释放引用
** - 避免引用泄漏导致内存问题
*/
LUALIB_API int (luaL_ref) (lua_State *L, 
                           int t);

/*
** [核心] 释放引用
**
** 功能：释放之前创建的引用，允许值被垃圾回收
** @param L - lua_State*: Lua状态机
** @param t - int: 引用所在的表位置
** @param ref - int: 要释放的引用标识符
**
** 资源管理：
** - 对应luaL_ref的清理操作
** - 防止内存泄漏
** - 维护引用表的整洁性
*/
LUALIB_API void (luaL_unref) (lua_State *L, 
                              int t, 
                              int ref);

/*
** ====================================================================
** [核心] 代码加载和执行函数
** ====================================================================
*/

/*
** [核心] 从文件加载Lua代码
**
** 功能：从指定文件加载Lua源代码或字节码
** @param L - lua_State*: Lua状态机
** @param filename - const char*: 文件路径名
** @return int: 加载结果状态码
**
** 返回值说明：
** - 0 (LUA_OK): 加载成功
** - LUA_ERRSYNTAX: 语法错误
** - LUA_ERRFILE: 文件不存在或无法读取
** - LUA_ERRMEM: 内存不足
**
** 加载流程：
** 1. 打开并读取文件内容
** 2. 检测文件类型（源码或字节码）
** 3. 编译或验证代码
** 4. 将编译后的函数压入栈顶
**
** 文件格式支持：
** - .lua源代码文件（文本格式）
** - .luac字节码文件（二进制格式）
** - 自动识别文件类型
*/
LUALIB_API int (luaL_loadfile) (lua_State *L, 
                                const char *filename);

/*
** [核心] 从内存缓冲区加载代码
**
** 功能：从内存中的字符串缓冲区加载Lua代码
** @param L - lua_State*: Lua状态机
** @param buff - const char*: 包含代码的内存缓冲区
** @param sz - size_t: 缓冲区大小（字节数）
** @param name - const char*: 代码块名称（用于错误报告和调试）
** @return int: 加载结果状态码
**
** 应用场景：
** - 动态生成的代码
** - 嵌入式代码字符串
** - 网络传输的代码
** - 加密或压缩的代码
**
** 名称约定：
** - 使用"@filename"表示来自文件
** - 使用"=source"表示特殊来源
** - 直接使用描述性字符串
*/
LUALIB_API int (luaL_loadbuffer) (lua_State *L, 
                                  const char *buff, 
                                  size_t sz,
                                  const char *name);

/*
** [核心] 从字符串加载代码
**
** 功能：从C字符串加载Lua代码（以null结尾）
** @param L - lua_State*: Lua状态机
** @param s - const char*: 包含Lua代码的C字符串
** @return int: 加载结果状态码
**
** 便利性：
** - 自动计算字符串长度
** - 适用于字面量代码
** - 简化单行代码加载
**
** 等价操作：
** luaL_loadstring(L, s) 等价于
** luaL_loadbuffer(L, s, strlen(s), s)
*/
LUALIB_API int (luaL_loadstring) (lua_State *L, 
                                  const char *s);

/*
** ====================================================================
** [核心] Lua状态机管理
** ====================================================================
*/

/*
** [核心] 创建新的Lua状态机
**
** 功能：创建一个全新的独立Lua状态机实例
** @return lua_State*: 新创建的状态机指针，失败时返回NULL
**
** 特性：
** - 包含标准内存分配器
** - 初始化基本数据结构
** - 创建主线程
** - 设置默认错误处理函数
**
** 资源管理：
** - 使用lua_close()关闭状态机
** - 每个状态机独立管理内存
** - 支持多个并发状态机实例
**
** 内存分配：
** - 使用系统默认的realloc
** - 可通过lua_newstate指定自定义分配器
** - 自动垃圾回收管理
*/
LUALIB_API lua_State *(luaL_newstate) (void);

/*
** ====================================================================
** [核心] 字符串处理函数
** ====================================================================
*/

/*
** [核心] 字符串替换函数
**
** 功能：在字符串中进行模式替换
** @param L - lua_State*: Lua状态机
** @param s - const char*: 源字符串
** @param p - const char*: 要查找的模式字符串  
** @param r - const char*: 替换字符串
** @return const char*: 替换后的新字符串
**
** 替换规则：
** - 将s中所有出现的p替换为r
** - 进行字面量匹配（不是模式匹配）
** - 结果字符串存储在Lua管理的内存中
**
** 内存管理：
** - 返回的字符串由Lua垃圾回收器管理
** - 在下次GC前保持有效
** - 自动处理内存分配和释放
*/
LUALIB_API const char *(luaL_gsub) (lua_State *L, 
                                    const char *s, 
                                    const char *p,
                                    const char *r);

/*
** [核心] 查找或创建嵌套表
**
** 功能：在表中查找或创建指定路径的嵌套表结构
** @param L - lua_State*: Lua状态机
** @param idx - int: 基表在栈中的位置
** @param fname - const char*: 字段路径（如"a.b.c"）
** @param szhint - int: 新表的建议初始大小
** @return const char*: 如果路径中存在非表值，返回冲突的路径；否则返回NULL
**
** 路径处理：
** - 使用点号分隔嵌套字段
** - 自动创建不存在的中间表
** - 支持任意深度的嵌套
**
** 应用场景：
** - 创建模块的命名空间
** - 初始化配置表结构
** - 实现包系统的路径管理
**
** 错误处理：
** - 如果路径中存在非表类型的值，返回错误路径
** - 调用者可据此进行适当的错误处理
*/
LUALIB_API const char *(luaL_findtable) (lua_State *L, 
                                         int idx,
                                         const char *fname, 
                                         int szhint);




/*
** ====================================================================
** [实用工具] 常用操作宏定义
** ====================================================================
**
** 这些宏提供了常见操作的简化接口，提高代码的可读性和开发效率。
** 所有宏都基于已有的辅助函数，提供更便捷的调用方式。
*/

/*
** [核心] 参数条件检查宏
**
** 功能：检查条件是否成立，不成立时抛出参数错误
** @param L - lua_State*: Lua状态机
** @param cond - 表达式: 要检查的条件
** @param numarg - int: 参数位置
** @param extramsg - const char*: 错误消息
**
** 使用模式：
** luaL_argcheck(L, value > 0, 1, "must be positive");
**
** 展开为：
** if (!(cond)) luaL_argerror(L, numarg, extramsg);
*/
#define luaL_argcheck(L, cond, numarg, extramsg) \
    ((void)((cond) || luaL_argerror(L, (numarg), (extramsg))))

/*
** [核心] 字符串参数检查宏（忽略长度）
**
** 功能：检查并获取字符串参数，不返回长度信息
** @param L - lua_State*: Lua状态机  
** @param n - int: 参数位置
** @return const char*: 字符串内容
**
** 适用场景：
** - 只需要字符串内容，不关心长度
** - 简化字符串参数处理
*/
#define luaL_checkstring(L,n) (luaL_checklstring(L, (n), NULL))

/*
** [核心] 可选字符串参数宏（忽略长度）
**
** 功能：获取可选字符串参数，不返回长度信息
** @param L - lua_State*: Lua状态机
** @param n - int: 参数位置
** @param d - const char*: 默认值
** @return const char*: 字符串内容或默认值
*/
#define luaL_optstring(L,n,d) (luaL_optlstring(L, (n), (d), NULL))

/*
** [兼容] 整数参数检查宏
**
** 功能：检查并获取int类型参数
** 注意：这是为了兼容性保留的宏，可能丢失精度
*/
#define luaL_checkint(L,n)    ((int)luaL_checkinteger(L, (n)))

/*
** [兼容] 可选整数参数宏
**
** 功能：获取可选的int类型参数
*/
#define luaL_optint(L,n,d)    ((int)luaL_optinteger(L, (n), (d)))

/*
** [兼容] 长整数参数检查宏
**
** 功能：检查并获取long类型参数
*/
#define luaL_checklong(L,n)   ((long)luaL_checkinteger(L, (n)))

/*
** [兼容] 可选长整数参数宏
**
** 功能：获取可选的long类型参数
*/
#define luaL_optlong(L,n,d)   ((long)luaL_optinteger(L, (n), (d)))

/*
** [实用] 获取类型名称宏
**
** 功能：获取栈中指定位置值的类型名称
** @param L - lua_State*: Lua状态机
** @param i - int: 栈位置
** @return const char*: 类型名称字符串
**
** 类型名称：
** - "nil", "boolean", "number", "string"
** - "table", "function", "userdata", "thread"
*/
#define luaL_typename(L,i) lua_typename(L, lua_type(L,(i)))

/*
** [实用] 执行文件宏
**
** 功能：加载并执行Lua文件
** @param L - lua_State*: Lua状态机
** @param fn - const char*: 文件路径
** @return int: 执行结果（0表示成功）
**
** 执行流程：
** 1. 加载文件到栈顶（luaL_loadfile）
** 2. 如果加载成功，立即执行（lua_pcall）
** 3. 返回合并的错误状态
*/
#define luaL_dofile(L, fn) \
    (luaL_loadfile(L, fn) || lua_pcall(L, 0, LUA_MULTRET, 0))

/*
** [实用] 执行字符串宏
**
** 功能：加载并执行Lua代码字符串
** @param L - lua_State*: Lua状态机
** @param s - const char*: 代码字符串
** @return int: 执行结果（0表示成功）
**
** 应用场景：
** - 执行动态生成的代码
** - 配置脚本的即时执行
** - 交互式代码执行
*/
#define luaL_dostring(L, s) \
    (luaL_loadstring(L, s) || lua_pcall(L, 0, LUA_MULTRET, 0))

/*
** [实用] 获取注册表中的元表
**
** 功能：从注册表中获取指定名称的元表
** @param L - lua_State*: Lua状态机
** @param n - const char*: 元表名称
** @return int: 是否成功获取（非0表示成功）
**
** 等价操作：
** lua_getfield(L, LUA_REGISTRYINDEX, n)
*/
#define luaL_getmetatable(L,n) (lua_getfield(L, LUA_REGISTRYINDEX, (n)))

/*
** [实用] 可选参数处理宏
**
** 功能：如果参数不存在或为nil，使用默认值；否则调用指定函数处理
** @param L - lua_State*: Lua状态机
** @param f - 函数: 参数处理函数
** @param n - int: 参数位置
** @param d - 默认值: 参数不存在时的默认值
** @return 处理后的值或默认值
**
** 使用示例：
** int size = luaL_opt(L, luaL_checkint, 2, 1024);
*/
#define luaL_opt(L,f,n,d) (lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

/*
** ====================================================================
** [高级] 通用缓冲区操作系统
** ====================================================================
**
** luaL_Buffer 提供了一个高效的字符串构建机制，避免了频繁的
** 内存分配和字符串连接操作。它特别适用于需要逐步构建长字符串
** 的场景，如文本处理、序列化、代码生成等。
*/

/*
** [核心] 字符串缓冲区结构体
**
** 这个结构体实现了一个可动态增长的字符串缓冲区，
** 内部使用栈来管理字符串片段，最终合并为单一字符串。
**
** 设计优势：
** - 减少内存碎片：使用预分配的缓冲区
** - 高效的增长策略：栈管理多个字符串片段  
** - 自动内存管理：与Lua GC集成
** - 类型安全：所有操作都有边界检查
*/
typedef struct luaL_Buffer
{
    char *p;                           // 当前写入位置指针
    int lvl;                          // 栈中字符串片段的数量
    lua_State *L;                     // 关联的Lua状态机
    char buffer[LUAL_BUFFERSIZE];     // 内部固定大小缓冲区
} luaL_Buffer;

/*
** [高频] 添加单个字符到缓冲区
**
** 功能：向缓冲区添加一个字符，自动处理空间不足的情况
** @param B - luaL_Buffer*: 缓冲区结构体指针
** @param c - char: 要添加的字符
**
** 性能优化：
** - 使用内联宏避免函数调用开销
** - 先检查空间，仅在必要时调用prepbuffer
** - 直接指针操作，最小化内存访问
**
** 安全机制：
** - 自动检查缓冲区边界
** - 空间不足时自动扩展
** - 字符类型强制转换确保正确性
*/
#define luaL_addchar(B,c) \
    ((void)((B)->p < ((B)->buffer+LUAL_BUFFERSIZE) || luaL_prepbuffer(B)), \
     (*(B)->p++ = (char)(c)))

/*
** [兼容] 添加字符的旧版本名称
** 保留此宏以兼容旧代码
*/
#define luaL_putchar(B,c) luaL_addchar(B,c)

/*
** [性能] 手动调整缓冲区指针
**
** 功能：在直接写入缓冲区后，手动推进写入位置
** @param B - luaL_Buffer*: 缓冲区指针
** @param n - size_t: 已写入的字节数
**
** 使用场景：
** - 使用sprintf等函数直接写入buffer时
** - 批量拷贝数据后
** - 与外部库交互时
**
** 注意事项：
** - 调用者必须确保不超出缓冲区边界
** - 通常与luaL_prepbuffer配合使用
*/
#define luaL_addsize(B,n) ((B)->p += (n))

/*
** [核心] 初始化缓冲区
**
** 功能：初始化一个新的字符串缓冲区
** @param L - lua_State*: Lua状态机
** @param B - luaL_Buffer*: 要初始化的缓冲区
**
** 初始化过程：
** 1. 设置关联的Lua状态机
** 2. 重置写入指针到缓冲区开始
** 3. 清空栈片段计数
** 4. 准备接收第一批数据
**
** 生命周期：
** - 必须在使用任何其他缓冲区操作前调用
** - 缓冲区可以重复使用（重新初始化）
** - 与luaL_pushresult配对使用
*/
LUALIB_API void (luaL_buffinit) (lua_State *L, 
                                 luaL_Buffer *B);

/*
** [核心] 准备缓冲区空间
**
** 功能：确保缓冲区有足够空间，必要时进行整理
** @param B - luaL_Buffer*: 缓冲区指针
** @return char*: 可用于写入的内存区域指针
**
** 空间管理策略：
** 1. 如果当前缓冲区空间充足，直接返回
** 2. 如果空间不足，将当前内容推入栈
** 3. 重置缓冲区指针，准备新的写入空间
** 4. 返回新的可写入区域
**
** 自动化处理：
** - 透明处理缓冲区溢出
** - 维护字符串片段栈
** - 确保总是有LUAL_BUFFERSIZE字节可用
*/
LUALIB_API char *(luaL_prepbuffer) (luaL_Buffer *B);

/*
** [核心] 添加指定长度的字符串
**
** 功能：向缓冲区添加指定长度的字符串数据
** @param B - luaL_Buffer*: 缓冲区指针
** @param s - const char*: 源字符串
** @param l - size_t: 要添加的字节数
**
** 特性：
** - 支持包含null字符的二进制数据
** - 高效处理大块数据
** - 自动管理缓冲区空间
** - 不依赖字符串的null终止符
**
** 适用场景：
** - 添加二进制数据
** - 处理已知长度的字符串片段
** - 从其他缓冲区拷贝数据
*/
LUALIB_API void (luaL_addlstring) (luaL_Buffer *B, 
                                   const char *s, 
                                   size_t l);

/*
** [实用] 添加C字符串
**
** 功能：向缓冲区添加以null结尾的C字符串
** @param B - luaL_Buffer*: 缓冲区指针
** @param s - const char*: 要添加的C字符串
**
** 便利性：
** - 自动计算字符串长度（使用strlen）
** - 适用于字面量字符串和标准C字符串
** - 简化常见的字符串追加操作
**
** 等价操作：
** luaL_addstring(B, s) 等价于
** luaL_addlstring(B, s, strlen(s))
*/
LUALIB_API void (luaL_addstring) (luaL_Buffer *B, 
                                  const char *s);

/*
** [核心] 添加栈顶值到缓冲区
**
** 功能：将栈顶的值转换为字符串并添加到缓冲区
** @param B - luaL_Buffer*: 缓冲区指针
**
** 转换规则：
** - 字符串：直接添加
** - 数字：转换为字符串表示
** - 其他类型：使用tostring元方法或默认转换
**
** 栈操作：
** - 消费栈顶元素（弹出）
** - 不改变栈的其他部分
** - 支持连续的addvalue操作
**
** 应用场景：
** - 序列化Lua值
** - 构建包含动态内容的字符串
** - 实现自定义的格式化函数
*/
LUALIB_API void (luaL_addvalue) (luaL_Buffer *B);

/*
** [核心] 完成缓冲区并生成最终字符串
**
** 功能：将缓冲区内容合并为单一字符串并推入栈顶
** @param B - luaL_Buffer*: 缓冲区指针
**
** 完成过程：
** 1. 将当前缓冲区内容推入栈（如果非空）
** 2. 将栈中的所有字符串片段连接
** 3. 生成最终的完整字符串
** 4. 将结果推入栈顶
** 5. 清理临时数据
**
** 结果：
** - 栈顶包含完整的构建结果
** - 缓冲区内部状态被清理
** - 所有临时字符串片段被回收
**
** 生命周期：
** - 每个缓冲区只能调用一次pushresult
** - 调用后缓冲区不再可用
** - 结果字符串由Lua内存管理器管理
*/
LUALIB_API void (luaL_pushresult) (luaL_Buffer *B);


/*
** ====================================================================
** [兼容] 引用系统兼容接口
** ====================================================================
**
** 这一节提供了与旧版本Lua引用系统的兼容接口。
** 现代代码应该直接使用luaL_ref/luaL_unref函数。
*/

/*
** [常量] 预定义引用值
**
** 这些特殊值用于标识引用系统的特殊状态：
** - LUA_NOREF: 表示无效或未分配的引用
** - LUA_REFNIL: 表示对nil值的引用
*/
#define LUA_NOREF       (-2)    // 无效引用标识
#define LUA_REFNIL      (-1)    // nil值引用标识

/*
** [兼容] 创建引用的兼容宏
**
** 功能：为栈顶值创建引用，兼容旧版本的锁定机制
** @param L - lua_State*: Lua状态机
** @param lock - int: 锁定标志（非0表示锁定）
** @return int: 引用标识符
**
** 兼容性说明：
** - lock非0时：使用现代的luaL_ref创建引用
** - lock为0时：显示过时警告并抛出错误
**
** 迁移建议：
** 旧代码: lua_ref(L, 1)
** 新代码: luaL_ref(L, LUA_REGISTRYINDEX)
*/
#define lua_ref(L,lock) ((lock) ? luaL_ref(L, LUA_REGISTRYINDEX) : \
      (lua_pushstring(L, "unlocked references are obsolete"), lua_error(L), 0))

/*
** [兼容] 释放引用的兼容宏
**
** 功能：释放之前创建的引用
** @param L - lua_State*: Lua状态机
** @param ref - int: 要释放的引用标识符
**
** 等价操作：
** lua_unref(L, ref) 等价于
** luaL_unref(L, LUA_REGISTRYINDEX, ref)
*/
#define lua_unref(L,ref) luaL_unref(L, LUA_REGISTRYINDEX, (ref))

/*
** [兼容] 获取引用值的兼容宏
**
** 功能：通过引用标识符获取对应的值
** @param L - lua_State*: Lua状态机
** @param ref - int: 引用标识符
**
** 操作结果：
** - 将引用的值推入栈顶
** - 如果引用无效，推入nil
**
** 等价操作：
** lua_getref(L, ref) 等价于
** lua_rawgeti(L, LUA_REGISTRYINDEX, ref)
*/
#define lua_getref(L,ref) lua_rawgeti(L, LUA_REGISTRYINDEX, (ref))

/*
** [兼容] 注册结构体类型别名
**
** 为了保持与旧代码的兼容性，提供luaL_reg作为luaL_Reg的别名。
** 新代码应该使用标准的luaL_Reg类型名称。
*/
#define luaL_reg	luaL_Reg

/*
** [文件结束] 头文件保护结束标记
** 与开头的 #ifndef lauxlib_h 配对
*/
#endif

/*
** ====================================================================
** 文件总结
** ====================================================================
**
** 本头文件定义了Lua辅助库的完整接口，包括：
**
** 核心功能模块：
** 1. 参数检查和验证 - 确保C函数参数的类型和值正确性
** 2. 错误处理机制 - 统一的错误报告和异常抛出
** 3. 库注册系统 - 批量注册C函数到Lua环境
** 4. 元表管理 - 用户数据类型系统的基础
** 5. 代码加载执行 - 从各种来源加载和运行Lua代码
** 6. 字符串缓冲区 - 高效的字符串构建机制
** 7. 引用系统 - 在C代码中长期持有Lua值
**
** 设计原则：
** - 类型安全：严格的参数类型检查
** - 错误友好：清晰的错误消息和位置信息
** - 内存安全：与Lua GC系统完全集成
** - 性能优化：最小化函数调用开销
** - 向后兼容：保留旧版本接口
**
** 使用模式：
** 1. 包含此头文件到C扩展模块中
** 2. 使用类型检查函数验证Lua参数
** 3. 使用缓冲区系统构建返回字符串
** 4. 使用注册系统导出C函数
** 5. 使用错误处理函数报告异常情况
**
** 性能考虑：
** - 大多数宏操作都是内联的，无函数调用开销
** - 缓冲区系统避免了频繁的内存分配
** - 引用系统提供了高效的值持有机制
** - 类型检查在调试版本中可以禁用以提高性能
**
** 扩展建议：
** - 对于特定应用，可以基于这些基础函数构建更高级的辅助函数
** - 考虑为常用操作模式创建专门的宏
** - 在性能关键路径上，可以直接使用lua.h中的基础API
**
** 相关文档：
** - lua.h: Lua核心C API
** - lualib.h: 标准库函数声明
** - luaconf.h: 编译时配置选项
**
** 注意事项：
** - 所有函数都假设传入的lua_State*是有效的
** - 某些函数可能触发垃圾回收，注意对象生命周期
** - 错误处理函数不会返回，会进行长跳转
** - 缓冲区操作是非线程安全的，需要外部同步
*/


