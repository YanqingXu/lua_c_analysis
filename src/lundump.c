/**
 * @file lundump.c
 * @brief Lua字节码反序列化模块：将字节码文件重建为可执行的函数原型
 * 
 * 详细说明：
 * 此模块实现了Lua虚拟机的字节码加载功能，负责将由ldump模块生成的
 * 字节码文件重新解析并构建为Lua函数原型对象。这是Lua脚本加载过程
 * 的核心组件，支持从文件、内存或网络等多种来源加载预编译的字节码。
 * 
 * 系统架构定位：
 * 在Lua虚拟机架构中，本模块处于加载器层，与ldump模块形成完整的
 * 序列化/反序列化对。它桥接了字节码存储格式和运行时内存表示，
 * 是实现脚本预编译和快速加载的关键技术。
 * 
 * 技术特点：
 * - 使用流式读取器(ZIO)支持多种数据源的统一访问
 * - 实现完整的格式验证和兼容性检查机制
 * - 提供可配置的安全检查级别(LUAC_TRUST_BINARIES)
 * - 采用异常安全的内存管理和错误恢复策略
 * - 支持递归函数结构的深度限制保护
 * - 实现了高效的内存分配和对象初始化
 * 
 * 依赖关系：
 * - lua.h: Lua核心API和基础类型定义
 * - lzio.h: 缓冲IO和流式读取接口
 * - lmem.h: 内存管理和安全分配函数
 * - lfunc.h: 函数原型创建和管理接口
 * - lstring.h: 字符串对象创建和管理
 * - ldebug.h: 调试支持和代码验证
 * - ldo.h: 执行控制和异常处理
 * - lobject.h: Lua对象系统和值操作
 * - lundump.h: 模块接口和格式定义
 * - string.h: 标准C字符串操作函数
 * 
 * 编译要求：
 * - C标准版本：C99或更高版本
 * - 必须定义LUA_CORE宏以访问内部API
 * - 可选定义LUAC_TRUST_BINARIES以关闭安全检查
 * - 需要链接Lua核心库和标准C运行时库
 * 
 * 安全模式配置：
 * @code
 * // 安全模式(默认)：启用所有格式检查和验证
 * // #define LUAC_TRUST_BINARIES 未定义
 * 
 * // 信任模式：跳过检查以提高加载性能
 * #define LUAC_TRUST_BINARIES
 * @endcode
 * 
 * 使用示例：
 * @code
 * #include \"lundump.h\"
 * 
 * // 从文件加载字节码
 * int load_bytecode_file(lua_State *L, const char *filename) {
 *     FILE *f = fopen(filename, \"rb\");
 *     if (f == NULL) {
 *         return -1;
 *     }
 *     
 *     // 创建ZIO读取器
 *     ZIO z;
 *     luaZ_init(L, &z, file_reader, f);
 *     
 *     // 创建缓冲区
 *     Mbuffer buff;
 *     luaZ_initbuffer(L, &buff);
 *     
 *     // 加载字节码
 *     Proto *f_proto = luaU_undump(L, &z, &buff, filename);
 *     
 *     // 清理资源
 *     luaZ_freebuffer(L, &buff);
 *     fclose(f);
 *     
 *     if (f_proto == NULL) {
 *         return -1;  // 加载失败
 *     }
 *     
 *     // 创建闭包并执行
 *     lua_pushcclosure(L, f_proto, 0);
 *     return 0;
 * }
 * @endcode
 * 
 * 内存安全考虑：
 * 模块使用Lua的垃圾收集器管理所有分配的内存，确保即使在错误情况下
 * 也不会发生内存泄漏。所有字符串和对象都通过安全的API创建，
 * 自动处理引用计数和生命周期管理。
 * 
 * 性能特征：
 * - 时间复杂度：O(n)，其中n为字节码文件的大小
 * - 空间复杂度：O(m)，m为重建的函数原型总大小
 * - I/O优化：使用缓冲读取器减少系统调用次数
 * - 内存优化：延迟分配和增量构建策略
 * 
 * 安全特性：
 * - 完整的格式验证防止恶意字节码
 * - 递归深度限制防止栈溢出攻击
 * - 数据范围检查防止缓冲区溢出
 * - 版本兼容性检查防止格式不匹配
 * - 可配置的信任级别适应不同安全需求
 * 
 * 错误处理：
 * 使用Lua的异常机制进行错误传播，所有错误都会触发
 * luaD_throw异常，调用者可以通过lua_pcall等保护调用
 * 来捕获和处理加载错误。
 * 
 * 注意事项：
 * - 字节码格式与Lua版本强相关，不同版本间不兼容
 * - 在不信任的环境中应启用完整的安全检查
 * - 大型字节码文件可能需要足够的内存和栈空间
 * - 递归函数结构受到深度限制保护
 * 
 * @author Roberto Ierusalimschy等Lua开发团队
 * @version 2.7.1.4 (Lua 5.1.5)
 * @date 2008/04/04
 * @since C99
 * @see ldump.h, lzio.h, lfunc.h, lundump.h
 */

#include <string.h>

#define lundump_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "lundump.h"
#include "lzio.h"

/**
 * @brief 字节码加载状态：管理整个反序列化过程的上下文和资源
 * 
 * 详细说明：
 * LoadState结构体封装了字节码加载过程中需要的所有状态信息，
 * 包括Lua虚拟机状态、输入流、缓冲区和文件名信息。这种设计
 * 实现了加载过程的状态管理和资源的统一访问。
 * 
 * 设计理念：
 * 采用状态机模式，将复杂的反序列化过程分解为一系列状态驱动的操作。
 * 通过ZIO抽象输入源，支持文件、内存、网络等多种数据来源。
 * 集中管理缓冲区和错误信息，简化错误处理和资源清理。
 * 
 * 内存布局：
 * 结构体设计为栈分配友好，所有成员都是指针类型，总大小约为32字节。
 * 不持有任何需要显式释放的资源，生命周期与加载操作同步。
 * 
 * 成员详细说明：
 * - L: Lua虚拟机状态指针，用于内存分配、异常处理和对象创建
 * - Z: ZIO流读取器指针，抽象不同类型的输入数据源
 * - b: 内存缓冲区指针，用于高效的字符串和数据读取
 * - name: 数据源名称，用于错误报告和调试信息显示
 * 
 * 生命周期管理：
 * 结构体在luaU_undump函数中栈上分配，生命周期与加载操作相同。
 * 初始化时设置所有必要字段，加载完成后自动销毁。
 * 不负责底层资源的管理，仅维护对外部资源的引用。
 * 
 * 使用模式：
 * @code
 * LoadState S;
 * S.L = L;                    // 设置Lua状态
 * S.Z = &zio;                 // 设置输入流
 * S.b = &buffer;              // 设置缓冲区
 * S.name = filename;          // 设置源名称
 * 
 * // 执行加载操作
 * LoadHeader(&S);
 * Proto *f = LoadFunction(&S, source_name);
 * 
 * return f;                   // 返回加载结果
 * @endcode
 * 
 * 错误处理：
 * 结构体通过name字段提供错误上下文信息，所有错误都会
 * 包含数据源名称，便于调试和问题定位。
 * 
 * 线程安全性：
 * 结构体实例与特定的加载操作绑定，不在多个线程间共享。
 * 通过Lua虚拟机状态的锁机制保证线程安全性。
 * 
 * 性能影响：
 * 结构体设计轻量级，适合频繁的栈分配和访问。
 * 通过引用而非复制减少数据传递开销。
 * ZIO和缓冲区的使用优化了I/O性能。
 * 
 * 扩展性考虑：
 * 结构体设计支持未来添加新的加载选项和状态信息。
 * ZIO抽象层提供了良好的输入源扩展能力。
 * 统一的状态管理便于添加新的错误处理和验证逻辑。
 * 
 * 注意事项：
 * - 结构体必须完全初始化后才能使用
 * - 不持有资源的所有权，仅维护引用关系
 * - name字段用于错误报告，应指向有效的字符串
 * - 在加载过程中不应修改结构体内容
 * 
 * @since C99
 * @see luaU_undump(), ZIO, Mbuffer, lua_State
 */
typedef struct {
    lua_State *L;              /**< Lua虚拟机状态指针 */
    ZIO *Z;                    /**< 输入流读取器指针 */
    Mbuffer *b;                /**< 内存缓冲区指针 */
    const char *name;          /**< 数据源名称字符串 */
} LoadState;

/**
 * @brief 条件编译安全检查机制：根据信任级别启用或禁用验证
 * 
 * 安全模式设计：
 * Lua提供了两种字节码加载模式以平衡安全性和性能：
 * 
 * 1. 安全模式(默认)：LUAC_TRUST_BINARIES未定义
 *    - 启用所有格式检查和数据验证
 *    - 适用于加载不可信的字节码文件
 *    - 提供最高级别的安全保护
 *    - 略微影响加载性能
 * 
 * 2. 信任模式：LUAC_TRUST_BINARIES已定义
 *    - 跳过大部分检查以提高性能
 *    - 适用于加载可信的字节码文件
 *    - 假设输入数据格式正确
 *    - 最大化加载性能
 * 
 * 宏定义说明：
 * - IF(c,s): 条件检查宏，在安全模式下检查条件c，失败时报告错误s
 * - error(S,s): 错误处理宏，在安全模式下触发异常，信任模式下为空操作
 */
#ifdef LUAC_TRUST_BINARIES
    /**
     * @brief 信任模式：禁用所有安全检查以获得最大性能
     * 
     * 在信任模式下，假设所有输入数据都是有效和安全的，
     * 跳过所有验证步骤以实现最快的加载速度。
     * 
     * @warning 仅在完全信任字节码来源时使用此模式
     * @warning 恶意或损坏的字节码可能导致未定义行为
     */
    #define IF(c, s)                  // 空操作：不执行任何检查
    #define error(S, s)               // 空操作：不报告任何错误
#else
    /**
     * @brief 安全模式：启用完整的格式验证和错误检查
     * 
     * 在安全模式下，对所有输入数据进行严格验证，
     * 确保字节码格式的正确性和数据的完整性。
     */
    #define IF(c, s)              if (c) error(S, s)

    /**
     * @brief 字节码加载错误处理函数：报告详细错误信息并触发异常
     * 
     * 详细说明：
     * 当检测到字节码格式错误、数据损坏或兼容性问题时，此函数会
     * 生成包含上下文信息的错误消息，并通过Lua的异常机制中断
     * 加载过程。这确保了错误的及时发现和处理。
     * 
     * 错误消息格式：
     * "{数据源名称}: {错误描述} in precompiled chunk"
     * 例如："script.luac: bad header in precompiled chunk"
     * 
     * 异常处理：
     * 函数通过luaD_throw触发LUA_ERRSYNTAX异常，调用者可以
     * 通过lua_pcall等保护调用来捕获和处理这些错误。
     * 
     * 错误类型示例：
     * - "bad header": 文件头格式不正确
     * - "bad integer": 整数值为负数或超出范围
     * - "unexpected end": 文件意外结束
     * - "bad constant": 常量类型不支持
     * - "bad code": 字节码验证失败
     * - "code too deep": 函数嵌套层次过深
     * 
     * @param[in] S 加载状态指针，包含错误上下文信息
     * @param[in] why 错误描述字符串，说明具体的错误原因
     * 
     * @pre S != NULL && S->L != NULL && S->name != NULL
     * @pre why != NULL
     * @post 函数不会正常返回，总是抛出异常
     * 
     * @note 函数使用luaO_pushfstring进行安全的字符串格式化
     * @warning 函数会中断当前执行流程，调用者应使用保护调用
     * 
     * @since C99
     * @see luaO_pushfstring(), luaD_throw(), lua_pcall()
     */
    static void error(LoadState *S, const char *why)
    {
        // 格式化错误消息：包含数据源名称和错误描述
        luaO_pushfstring(S->L, "%s: %s in precompiled chunk", S->name, why);
        
        // 触发语法错误异常：中断加载过程
        luaD_throw(S->L, LUA_ERRSYNTAX);
    }
#endif

/**
 * @brief 内存块加载宏：从输入流读取指定大小的数据块
 * 
 * 这个宏简化了内存块的读取操作，通过计算总字节数来调用底层的
 * LoadBlock函数。常用于数组和结构体的批量读取。
 * 
 * @param S LoadState加载状态指针
 * @param b 目标内存块指针
 * @param n 元素个数
 * @param size 每个元素的字节大小
 */
#define LoadMem(S, b, n, size)       LoadBlock(S, b, (n) * (size))

/**
 * @brief 字节加载宏：读取单个字节并转换为lu_byte类型
 * 
 * 通过LoadChar读取一个字符并转换为无符号字节类型，
 * 主要用于读取小范围的无符号整数值。
 * 
 * @param S LoadState加载状态指针
 * @return lu_byte类型的字节值
 */
#define LoadByte(S)                  (lu_byte)LoadChar(S)

/**
 * @brief 变量加载宏：读取单个变量的完整数据
 * 
 * 这个宏通过获取变量地址并指定其大小来加载任意类型的变量。
 * 是最常用的数据读取操作，适用于所有基础数据类型。
 * 
 * @param S LoadState加载状态指针
 * @param x 要加载数据的目标变量
 */
#define LoadVar(S, x)                LoadMem(S, &x, 1, sizeof(x))

/**
 * @brief 向量加载宏：读取数组类型的数据
 * 
 * 直接调用LoadMem进行数组数据的批量读取，
 * 用于加载字节码、行号数组等连续数据。
 * 
 * @param S LoadState加载状态指针
 * @param b 目标数组指针
 * @param n 数组元素个数
 * @param size 每个元素的字节大小
 */
#define LoadVector(S, b, n, size)    LoadMem(S, b, n, size)

/**
 * @brief 核心数据块加载函数：从输入流读取指定大小的数据
 * 
 * 详细说明：
 * 这是整个加载系统的基础函数，所有其他加载操作最终都会调用此函数。
 * 它负责从输入流中读取指定大小的数据到目标内存位置，
 * 并进行错误检查确保数据的完整性。
 * 
 * 算法描述：
 * 1. 调用ZIO读取器从输入流读取数据
 * 2. 检查实际读取的字节数是否与期望一致
 * 3. 如果读取不完整，触发“意外结束”错误
 * 
 * 错误检测：
 * 函数会检查读取操作是否成功，如果输入流提前结束
 * 或者发生读取错误，会立即触发异常停止加载过程。
 * 
 * 性能特征：
 * - 时间复杂度：O(n)，其中n为要读取的字节数
 * - 空间复杂度：O(1)，不分配额外内存
 * - I/O依赖：性能主要受底层ZIO读取器限制
 * 
 * @param[in] S 加载状态指针，包含输入流和错误上下文
 * @param[out] b 目标内存块指针，用于存储读取的数据
 * @param[in] size 要读取的字节数，可以为0
 * 
 * @pre S != NULL && S->Z != NULL
 * @pre b != NULL || size == 0
 * @post 成功时b中包含从输入流读取的数据
 * @post 失败时触发异常，不会正常返回
 * 
 * @note 函数会修改目标内存区域的内容
 * @note 在读取错误时会立即中断加载过程
 * @warning 调用者必须确保目标内存区域足够大
 * 
 * @since C99
 * @see ZIO, luaZ_read(), LoadMem(), LoadVar()
 */
static void LoadBlock(LoadState *S, void *b, size_t size)
{
    // 执行读取操作：从输入流读取数据到目标地址
    size_t r = luaZ_read(S->Z, b, size);
    
    // 错误检查：验证读取操作是否完整成功
    IF(r != 0, "unexpected end");
}

/**
 * @brief 字符加载函数：从输入流读取单个字符
 * 
 * 详细说明：
 * 此函数读取一个char类型的字符并返回为int类型。
 * 主要用于读取小范围的整数值，如标志位、枚举值等。
 * 
 * 数据转换：
 * 读取char类型数据并转换为int类型返回，
 * 符号扩展取决于平台的char类型定义。
 * 
 * 使用场景：
 * - Lua值类型标识(LUA_TNIL、LUA_TBOOLEAN等)
 * - 函数参数个数、upvalue个数等小整数
 * - 布尔值的加载(0或1)
 * 
 * @param[in] S 加载状态指针
 * @return int类型的字符值
 * 
 * @note 返回值的范围取决于平台的char类型定义
 * @note 符号扩展行为依赖于编译器和平台
 * 
 * @since C99
 * @see LoadVar(), LoadByte()
 */
static int LoadChar(LoadState *S)
{
    // 读取字符：使用标准变量加载宏
    char x;
    LoadVar(S, x);
    
    // 类型转换：将char转换为int返回
    return x;
}

/**
 * @brief 整数加载函数：从输入流读取非负整数
 * 
 * 详细说明：
 * 此函数读取一个int类型的整数并进行安全性验证。
 * 由于在字节码中整数通常用于表示大小、计数等非负值，
 * 函数会检查整数的有效性以防止恶意数据。
 * 
 * 安全验证：
 * 检查读取的整数是否为非负数，负数通常表示
 * 数据损坏或恶意构造的字节码文件。
 * 
 * 数据格式：
 * 读取int类型的直接二进制表示，字节序依赖于平台。
 * 在字节码文件中，整数通常用于表示数组大小、元素计数等。
 * 
 * 使用场景：
 * - 数组大小和元素计数
 * - 函数的行号信息(linedefined、lastlinedefined)
 * - 局部变量的作用域范围(startpc、endpc)
 * 
 * @param[in] S 加载状态指针
 * @return 非负整数值
 * 
 * @pre S != NULL
 * @post 成功时返回非负整数
 * @post 失败时触发“bad integer”错误
 * 
 * @note 负数被视为无效数据，会触发错误
 * @note 字节序依赖于平台，跨平台使用需要注意兼容性
 * @warning 恶意构造的负数可能导致安全问题
 * 
 * @since C99
 * @see LoadVar(), LoadChar(), LoadNumber()
 */
static int LoadInt(LoadState *S)
{
    // 读取整数：使用标准变量加载宏
    int x;
    LoadVar(S, x);
    
    // 安全验证：检查整数是否为非负数
    IF(x < 0, "bad integer");
    
    return x;
}

/**
 * @brief Lua数值加载函数：从输入流读取lua_Number类型数值
 * 
 * 详细说明：
 * 加载Lua的数值类型，在Lua 5.1中通常为double类型。
 * 这是Lua常量表中数值常量的加载函数，保持高精度的浮点数表示。
 * 
 * 数据精度：
 * lua_Number通常定义为double，提供64位IEEE 754浮点精度。
 * 加载过程保持原始的二进制表示，确保数值的完全精确恢复。
 * 
 * 跨平台考虑：
 * 浮点数的二进制表示在不同平台上可能存在差异，包括字节序
 * 和浮点格式。字节码文件只能在兼容的平台上正确加载。
 * 
 * 特殊值处理：
 * 支持IEEE 754的特殊值如NaN、正无穷、负无穷等。
 * 这些特殊值的加载和使用需要特别的注意。
 * 
 * @param[in] S 加载状态指针
 * @return 加载的Lua数值
 * 
 * @note 依赖于lua_Number的具体定义，通常为double类型
 * @note 特殊值(NaN、Inf)的处理依赖于平台的IEEE 754支持
 * @warning 跨平台使用时需要确保浮点格式的兼容性
 * 
 * @since C99
 * @see lua_Number, LoadVar(), LoadConstants()
 */
static lua_Number LoadNumber(LoadState *S)
{
    // 直接加载：保持浮点数的完整精度
    lua_Number x;
    LoadVar(S, x);
    
    return x;
}

/**
 * @brief Lua字符串加载函数：从输入流重建字符串对象
 * 
 * 详细说明：
 * 此函数处理Lua字符串的加载和重建，支持NULL字符串
 * 和空字符串的特殊情况。加载后的字符串会成为Lua的
 * 内部字符串对象(TString)，由垃圾收集器管理生命周期。
 * 
 * 加载策略：
 * 1. 空字符串（大小为0）：返回NULL指针
 * 2. 非空字符串：读取字符串内容并创建TString对象
 * 
 * 内存管理：
 * 使用luaZ_openspace在缓冲区中分配临时存储空间，
 * 然后通过luaS_newlstr创建正式的字符串对象。
 * 临时缓冲区会被Lua的缓冲区管理器自动重用。
 * 
 * 字符串处理：
 * 加载的字符串長度不包括终止符'\0'，
 * 因为字节码中存储的长度包含了终止符，
 * 需要在创建TString时减去1。
 * 
 * 安全特性：
 * 使用Lua的安全内存分配函数，自动处理内存不足等异常情况。
 * 所有创建的对象都会被Lua的垃圾收集器跟踪和管理。
 * 
 * 性能特征：
 * - 时间复杂度：O(n)，其中n为字符串长度
 * - 空间复杂度：O(n)，需要分配字符串存储空间
 * - 内存优化：使用缓冲区池减少分配开销
 * 
 * @param[in] S 加载状态指针
 * @return TString指针，如果是空字符串则返回NULL
 * 
 * @pre S != NULL && S->L != NULL && S->b != NULL
 * @post 成功时返回有效的TString指针或NULL
 * @post 失败时触发异常或内存错误
 * 
 * @note 空字符串(大小为0)返回NULL而不是空的TString对象
 * @note 创建的TString对象会被Lua的垃圾收集器管理
 * @warning 调用者不应手动释放返回的TString对象
 * 
 * @since C99
 * @see TString, luaZ_openspace(), luaS_newlstr(), LoadVar(), LoadBlock()
 */
static TString *LoadString(LoadState *S)
{
    // 读取字符串大小：包含终止符的总字节数
    size_t size;
    LoadVar(S, size);
    
    // 空字符串处理：直接返回NULL
    if (size == 0) {
        return NULL;
    } else {
        // 分配临时缓冲区：用于存储字符串数据
        char *s = luaZ_openspace(S->L, S->b, size);
        
        // 加载字符串内容：读取完整的字符串数据
        LoadBlock(S, s, size);
        
        // 创建字符串对象：从缓冲区创建TString，長度减1去除终止符
        return luaS_newlstr(S->L, s, size - 1);
    }
}

/**
 * @brief 函数字节码加载函数：重建函数的指令序列
 * 
 * 详细说明：
 * 此函数负责加载和重建Lua函数的字节码指令数组。字节码是Lua虚拟机
 * 的核心执行单元，包含了函数的所有执行逻辑。函数会分配适当的内存
 * 空间并从输入流中加载完整的指令序列。
 * 
 * 加载过程：
 * 1. 读取指令数量
 * 2. 分配指令数组内存空间
 * 3. 设置函数原型的代码大小字段
 * 4. 批量加载所有指令数据
 * 
 * 内存管理：
 * 使用luaM_newvector进行安全的内存分配，分配的内存会被Lua的
 * 垃圾收集器跟踪和管理。如果内存分配失败，会自动触发异常。
 * 
 * 指令格式：
 * 每条指令为Instruction类型，通常为32位整数，包含操作码和操作数。
 * 指令的具体格式定义在lopcodes.h中，支持多种寻址模式。
 * 
 * 性能考虑：
 * 使用批量加载(LoadVector)而非逐条加载，显著提高大型函数的
 * 加载性能。内存分配一次完成，避免频繁的内存操作。
 * 
 * @param[in] S 加载状态指针
 * @param[out] f 目标函数原型指针，用于存储加载的字节码
 * 
 * @pre S != NULL && f != NULL
 * @pre S->L != NULL(用于内存分配)
 * @post f->code指向分配的指令数组
 * @post f->sizecode包含指令数量
 * 
 * @note 分配的内存由Lua垃圾收集器管理，不需要手动释放
 * @note 指令数组的大小必须与实际指令数量匹配
 * @warning 损坏的指令数据可能导致虚拟机执行错误
 * 
 * @since C99
 * @see Instruction, Proto, luaM_newvector(), LoadVector()
 */
static void LoadCode(LoadState *S, Proto *f)
{
    // 读取指令数量：获取函数包含的字节码指令总数
    int n = LoadInt(S);
    
    // 分配指令数组：为函数字节码分配内存空间
    f->code = luaM_newvector(S->L, n, Instruction);
    f->sizecode = n;
    
    // 批量加载指令：从输入流读取所有字节码指令
    LoadVector(S, f->code, n, sizeof(Instruction));
}

/**
 * @brief 函数原型加载函数：递归加载嵌套的函数定义
 * 
 * 前向声明，用于处理函数间的相互递归调用关系。
 * 主要用于加载函数内部定义的嵌套函数和闭包。
 */
static Proto *LoadFunction(LoadState *S, TString *p);

/**
 * @brief 常量表加载函数：重建函数的常量和嵌套函数
 * 
 * 详细说明：
 * 此函数负责加载和重建Lua函数的常量表和嵌套函数原型。常量表包含函数中
 * 使用的所有字面量值(nil、boolean、number、string)，嵌套函数包含
 * 在当前函数内部定义的所有子函数。这是函数重建过程的核心组件。
 * 
 * 加载结构：
 * 1. 常量数组：数量 + 每个常量的类型和值
 * 2. 嵌套函数数组：数量 + 每个函数的完整原型
 * 
 * 常量类型处理：
 * - LUA_TNIL: 只需要类型信息，设置为nil值
 * - LUA_TBOOLEAN: 类型 + 布尔值(0或1)
 * - LUA_TNUMBER: 类型 + 数值(lua_Number)
 * - LUA_TSTRING: 类型 + 字符串对象(TString)
 * 
 * 内存安全策略：
 * 采用"先分配后填充"的策略：
 * 1. 分配完整的数组空间
 * 2. 初始化所有元素为安全的默认值
 * 3. 逐个加载和设置实际值
 * 这确保了即使在加载过程中发生错误，也不会留下未初始化的内存。
 * 
 * 递归处理：
 * 对于嵌套函数，递归调用LoadFunction进行完整的函数加载，
 * 形成深度优先的加载顺序，与dump时的顺序一致。
 * 
 * 错误恢复：
 * 如果在加载过程中发生错误，已分配的内存和已创建的对象
 * 会被Lua的垃圾收集器自动清理，不会造成内存泄漏。
 * 
 * 算法复杂度：
 * - 时间复杂度：O(k + Σf_i)，k为常量数，f_i为各嵌套函数大小
 * - 空间复杂度：O(k + Σf_i)，需要为所有常量和函数分配内存
 * 
 * @param[in] S 加载状态指针
 * @param[out] f 目标函数原型指针，用于存储加载的常量表
 * 
 * @pre S != NULL && f != NULL
 * @pre S->L != NULL(用于内存分配和对象创建)
 * @post f->k指向分配的常量数组，f->sizek包含常量数量
 * @post f->p指向分配的函数指针数组，f->sizep包含嵌套函数数量
 * 
 * @note 所有分配的内存和创建的对象由Lua垃圾收集器管理
 * @note 函数会递归加载所有层级的嵌套函数
 * @warning 深度嵌套可能导致栈溢出，需要足够的栈空间
 * @warning 损坏的常量数据可能导致类型系统错误
 * 
 * @since C99
 * @see Proto, TValue, TString, LoadFunction(), setnilvalue(), setbvalue(), setnvalue(), setsvalue2n()
 */
static void LoadConstants(LoadState *S, Proto *f)
{
    int i, n;
    
    // === 加载常量数组 ===
    
    // 读取常量数量
    n = LoadInt(S);
    
    // 分配常量数组：为所有常量分配TValue数组空间
    f->k = luaM_newvector(S->L, n, TValue);
    f->sizek = n;
    
    // 安全初始化：将所有常量初始化为nil值，确保内存安全
    for (i = 0; i < n; i++) {
        setnilvalue(&f->k[i]);
    }
    
    // 逐个加载常量：根据类型加载每个常量的实际值
    for (i = 0; i < n; i++) {
        TValue *o = &f->k[i];
        
        // 读取常量类型标识
        int t = LoadChar(S);
        
        // 根据类型加载相应的数据
        switch (t) {
        case LUA_TNIL:
            // nil值：已在初始化时设置，无需额外操作
            setnilvalue(o);
            break;
            
        case LUA_TBOOLEAN:
            // 布尔值：读取布尔标志并设置TValue
            setbvalue(o, LoadChar(S) != 0);
            break;
            
        case LUA_TNUMBER:
            // 数值：读取lua_Number并设置TValue
            setnvalue(o, LoadNumber(S));
            break;
            
        case LUA_TSTRING:
            // 字符串：加载TString对象并设置TValue
            setsvalue2n(S->L, o, LoadString(S));
            break;
            
        default:
            // 未知类型：字节码损坏或版本不兼容
            error(S, "bad constant");
            break;
        }
    }
    
    // === 加载嵌套函数数组 ===
    
    // 读取嵌套函数数量
    n = LoadInt(S);
    
    // 分配函数指针数组：为所有嵌套函数分配指针数组空间
    f->p = luaM_newvector(S->L, n, Proto *);
    f->sizep = n;
    
    // 安全初始化：将所有函数指针初始化为NULL，确保内存安全
    for (i = 0; i < n; i++) {
        f->p[i] = NULL;
    }
    
    // 递归加载嵌套函数：逐个加载每个嵌套函数的完整原型
    for (i = 0; i < n; i++) {
        f->p[i] = LoadFunction(S, f->source);
    }
}

/**
 * @brief 调试信息加载函数：重建函数的调试数据
 * 
 * 详细说明：
 * 此函数处理Lua函数调试信息的加载和重建，包括行号信息、局部变量信息
 * 和upvalue信息。这些信息对于调试器、错误报告和代码分析工具非常重要。
 * 如果字节码在生成时被剥离了调试信息，这些数组将为空。
 * 
 * 调试信息组成：
 * 1. 行号信息数组：每条指令对应的源代码行号
 * 2. 局部变量信息：变量名、作用域范围(startpc、endpc)
 * 3. Upvalue信息：闭包中使用的外部变量名称
 * 
 * 数据格式处理：
 * - 行号数组：整数数组，直接批量加载
 * - 局部变量：结构体数组，逐个加载每个字段
 * - Upvalue：字符串指针数组，逐个加载字符串对象
 * 
 * 内存安全策略：
 * 采用"先分配后填充"的安全策略：
 * 1. 分配完整的数组空间
 * 2. 初始化所有指针为NULL或安全值
 * 3. 逐个加载和设置实际值
 * 确保即使加载失败也不会留下悬垂指针。
 * 
 * 剥离处理：
 * 如果字节码生成时启用了调试信息剥离，所有调试相关的
 * 数组大小都为0，函数会分配空数组或NULL指针。
 * 
 * 性能影响：
 * 调试信息通常占字节码的很大比例，跳过调试信息加载
 * 可以显著提高加载速度和减少内存使用。
 * 
 * @param[in] S 加载状态指针
 * @param[out] f 目标函数原型指针，用于存储调试信息
 * 
 * @pre S != NULL && f != NULL
 * @pre S->L != NULL(用于内存分配和字符串创建)
 * @post f->lineinfo指向行号数组，f->sizelineinfo包含数组大小
 * @post f->locvars指向局部变量数组，f->sizelocvars包含数组大小
 * @post f->upvalues指向upvalue数组，f->sizeupvalues包含数组大小
 * 
 * @note 如果调试信息被剥离，相应的数组大小为0
 * @note 所有分配的内存由Lua垃圾收集器管理
 * @warning 损坏的调试信息不会影响函数执行，但会影响调试体验
 * 
 * @since C99
 * @see Proto, LocVar, TString, LoadVector(), LoadInt(), LoadString()
 */
static void LoadDebug(LoadState *S, Proto *f)
{
    int i, n;
    
    // === 加载行号信息数组 ===
    
    // 读取行号数组大小
    n = LoadInt(S);
    
    // 分配行号数组：为每条指令的行号信息分配空间
    f->lineinfo = luaM_newvector(S->L, n, int);
    f->sizelineinfo = n;
    
    // 批量加载行号：直接读取整个行号数组
    LoadVector(S, f->lineinfo, n, sizeof(int));
    
    // === 加载局部变量信息 ===
    
    // 读取局部变量数量
    n = LoadInt(S);
    
    // 分配局部变量数组：为所有局部变量信息分配LocVar数组空间
    f->locvars = luaM_newvector(S->L, n, LocVar);
    f->sizelocvars = n;
    
    // 安全初始化：将所有变量名指针初始化为NULL
    for (i = 0; i < n; i++) {
        f->locvars[i].varname = NULL;
    }
    
    // 逐个加载局部变量信息：变量名和作用域范围
    for (i = 0; i < n; i++) {
        // 变量名：加载TString对象表示变量名
        f->locvars[i].varname = LoadString(S);
        
        // 作用域起始：变量开始有效的指令位置
        f->locvars[i].startpc = LoadInt(S);
        
        // 作用域结束：变量失效的指令位置
        f->locvars[i].endpc = LoadInt(S);
    }
    
    // === 加载Upvalue信息 ===
    
    // 读取upvalue数量
    n = LoadInt(S);
    
    // 分配upvalue数组：为所有upvalue名称分配TString指针数组空间
    f->upvalues = luaM_newvector(S->L, n, TString *);
    f->sizeupvalues = n;
    
    // 安全初始化：将所有upvalue名称指针初始化为NULL
    for (i = 0; i < n; i++) {
        f->upvalues[i] = NULL;
    }
    
    // 逐个加载upvalue名称：闭包变量的名称信息
    for (i = 0; i < n; i++) {
        f->upvalues[i] = LoadString(S);
    }
}

/**
 * @brief 函数原型加载函数：重建完整的Lua函数定义
 * 
 * 详细说明：
 * 这是函数加载的核心函数，负责从字节码中重建一个完整的Lua函数原型(Proto)。
 * 函数按照严格的顺序加载所有组成部分，确保与dump时的顺序完全一致，
 * 最终重建出可执行的函数对象。
 * 
 * 加载顺序和内容：
 * 1. 源文件名：函数定义所在的源文件
 * 2. 函数位置：在源文件中的起始和结束行号
 * 3. 函数签名：upvalue数量、参数数量、可变参数标志
 * 4. 栈信息：函数执行时需要的最大栈大小
 * 5. 字节码：函数的指令序列
 * 6. 常量表：函数使用的常量和嵌套函数
 * 7. 调试信息：行号、局部变量、upvalue名称
 * 8. 代码验证：确保生成的字节码有效
 * 
 * 安全保护机制：
 * - 递归深度检查：防止恶意字节码导致栈溢出
 * - 代码验证：通过luaG_checkcode验证字节码的有效性
 * - GC保护：将新创建的函数压入栈中保护免被垃圾回收
 * - 异常安全：使用try-finally模式确保资源正确清理
 * 
 * 内存管理：
 * 函数创建后立即压入Lua栈进行GC保护，防止在构造过程中
 * 被垃圾收集器误回收。构造完成后从栈中移除。
 * 
 * 源文件优化：
 * 如果函数的源文件与父函数相同(NULL表示)，会自动继承父函数的源文件名，
 * 避免重复存储相同的路径字符串。
 * 
 * 递归处理：
 * 通过递归调用处理嵌套函数，支持任意层级的函数嵌套结构。
 * 使用C调用计数器限制递归深度，防止栈溢出攻击。
 * 
 * 错误恢复：
 * 如果在加载过程中发生任何错误，已分配的内存和创建的对象
 * 会被Lua的异常处理机制自动清理，确保不会造成内存泄漏。
 * 
 * 性能优化：
 * 使用批量操作加载大型数据结构，减少函数调用开销。
 * 延迟验证策略，在所有数据加载完成后进行最终验证。
 * 
 * @param[in] S 加载状态指针，包含输入流和错误上下文
 * @param[in] p 父函数的源文件名，用于源文件名继承优化
 * @return 加载完成的函数原型指针
 * 
 * @pre S != NULL && S->L != NULL
 * @post 返回完全初始化的Proto对象
 * @post 如果加载失败，触发异常不会正常返回
 * 
 * @note 函数会递归处理所有嵌套函数
 * @note 创建的Proto对象受Lua垃圾收集器管理
 * @warning 深度嵌套可能导致栈溢出，受LUAI_MAXCCALLS限制
 * @warning 损坏的字节码会导致验证失败和异常
 * 
 * @since C99
 * @see Proto, luaF_newproto(), LoadCode(), LoadConstants(), LoadDebug(), luaG_checkcode()
 */
static Proto *LoadFunction(LoadState *S, TString *p)
{
    Proto *f;
    
    // 递归深度检查：防止恶意字节码导致C栈溢出
    if (++S->L->nCcalls > LUAI_MAXCCALLS) {
        error(S, "code too deep");
    }
    
    // 创建新函数原型：分配并初始化Proto结构
    f = luaF_newproto(S->L);
    
    // GC保护：将新函数压入栈中，防止在构造过程中被回收
    setptvalue2s(S->L, S->L->top, f);
    incr_top(S->L);
    
    // === 加载函数基本信息 ===
    
    // 加载源文件名：如果为NULL则继承父函数的源文件
    f->source = LoadString(S);
    if (f->source == NULL) {
        f->source = p;
    }
    
    // 加载函数位置信息：在源文件中的行号范围
    f->linedefined = LoadInt(S);        // 函数定义开始行号
    f->lastlinedefined = LoadInt(S);    // 函数定义结束行号
    
    // 加载函数签名信息：函数的基本特征参数
    f->nups = LoadByte(S);              // upvalue数量(闭包变量个数)
    f->numparams = LoadByte(S);         // 固定参数数量
    f->is_vararg = LoadByte(S);         // 可变参数标志
    f->maxstacksize = LoadByte(S);      // 最大栈大小需求
    
    // === 加载函数内容 ===
    
    // 加载字节码：函数的核心执行逻辑
    LoadCode(S, f);
    
    // 加载常量表和嵌套函数：函数依赖的数据和子函数
    LoadConstants(S, f);
    
    // 加载调试信息：用于调试和错误报告
    LoadDebug(S, f);
    
    // === 验证和清理 ===
    
    // 字节码验证：确保生成的字节码在语义上正确
    IF(!luaG_checkcode(f), "bad code");
    
    // 清理GC保护：从栈中移除函数，恢复栈状态
    S->L->top--;
    
    // 恢复递归计数：允许后续的递归调用
    S->L->nCcalls--;
    
    return f;
}

/**
 * @brief 字节码文件头验证函数：检查字节码文件的有效性和兼容性
 * 
 * 详细说明：
 * 此函数读取并验证Lua字节码文件的标准头部信息。头部包含了
 * 版本标识、平台信息、数据类型大小等关键元数据，用于确保
 * 字节码文件的正确性和兼容性。
 * 
 * 验证过程：
 * 1. 生成当前平台的标准头部
 * 2. 从输入流读取字节码文件的头部
 * 3. 逐字节比较两个头部的内容
 * 4. 如果不匹配，触发“bad header”错误
 * 
 * 头部包含信息：
 * - Lua签名：标识这是一个Lua字节码文件
 * - 版本号：Lua虚拟机版本，用于兼容性检查
 * - 格式版本：字节码格式版本号
 * - 字节序标识：大端序或小端序标记
 * - 数据类型大小：int、size_t、Instruction等类型的字节数
 * - lua_Number类型：数值类型的具体实现信息
 * 
 * 安全重要性：
 * 头部验证是防止恶意字节码的第一道防线：
 * - 防止加载不兼容的字节码文件
 * - 检测损坏或篡改的字节码
 * - 防止版本不匹配导致的问题
 * - 早期发现平台兼容性问题
 * 
 * 错误情况：
 * - 文件签名不匹配：不是Lua字节码文件
 * - 版本不兼容：不同版本的Lua生成的字节码
 * - 平台不匹配：不同平台或编译配置生成的字节码
 * - 数据类型不兼容：不同架构或编译器生成的字节码
 * 
 * 性能影响：
 * 头部验证的性能开销微乎其微，但对安全性的保障至关重要。
 * 验证失败可以及早阻止后续的加载操作，避免更大的性能损失。
 * 
 * @param[in] S 加载状态指针，包含输入流和错误上下文
 * 
 * @pre S != NULL && S->Z != NULL
 * @post 成功时表示字节码文件有效且兼容
 * @post 失败时触发“bad header”错误
 * 
 * @note 头部格式由luaU_header函数定义，不应手动修改
 * @note 头部长度为LUAC_HEADERSIZE字节
 * @warning 跳过头部验证可能导致严重的安全问题
 * 
 * @since C99
 * @see luaU_header(), LUAC_HEADERSIZE, LoadBlock(), memcmp()
 */
static void LoadHeader(LoadState *S)
{
    // 分配头部缓冲区：用于存储标准头部和加载的头部
    char h[LUAC_HEADERSIZE];    // 标准头部缓冲区
    char s[LUAC_HEADERSIZE];    // 加载的头部缓冲区
    
    // 生成标准头部：创建当前平台的标准字节码头部
    luaU_header(h);
    
    // 加载字节码头部：从输入流读取字节码文件的头部
    LoadBlock(S, s, LUAC_HEADERSIZE);
    
    // 头部比较验证：检查标准头部和加载的头部是否完全一致
    IF(memcmp(h, s, LUAC_HEADERSIZE) != 0, "bad header");
}

/**
 * @brief Lua字节码加载主接口：将字节码文件转换为可执行的函数原型
 * 
 * 详细说明：
 * 这是lundump模块的主要公共接口，提供了完整的Lua字节码加载功能。
 * 函数接受一个字节码输入流，通过ZIO读取器将其转换为
 * 可执行的Lua函数原型，可以直接由Lua虚拟机执行。
 * 
 * 加载流程：
 * 1. 初始化加载状态(LoadState)
 * 2. 处理数据源名称和错误上下文
 * 3. 验证字节码文件头部信息
 * 4. 加载主函数原型(递归处理所有嵌套函数)
 * 5. 返回最终的函数原型对象
 * 
 * 参数详解：
 * - L: Lua虚拟机状态，提供内存管理和异常处理
 * - Z: ZIO输入流读取器，抽象不同类型的数据源
 * - buff: 内存缓冲区，用于高效的字符串和数据读取
 * - name: 数据源名称，用于错误报告和调试信息
 * 
 * 数据源名称处理：
 * 函数会智能处理不同格式的数据源名称：
 * - '@filename': 文件路径，去除'@'前缀
 * - '=string': 显式名称，去除'='前缀
 * - 以LUA_SIGNATURE开始: 二进制数据，设为"binary string"
 * - 其他: 直接使用原名称
 * 
 * 错误处理：
 * 函数采用“快速失败”策略，一旦检测到格式错误或数据损坏
 * 就立即停止加载过程。错误信息会包含数据源名称和具体错误描述。
 * 
 * 内存管理：
 * 函数本身不分配持久内存，所有内存操作通过Lua的垃圾收集器完成。
 * 使用栈分配的LoadState管理加载状态，函数结束时自动清理。
 * 
 * 线程安全：
 * 函数使用Lua的状态管理机制，线程安全性取决于Lua状态的使用。
 * ZIO读取器和缓冲区的线程安全性由调用者保证。
 * 
 * 性能特征：
 * - 时间复杂度：O(n)，其中n为字节码文件的大小
 * - 空间复杂度：O(m)，其中m为重建的函数原型总大小
 * - I/O优化：使用ZIO缓冲读取器减少系统调用次数
 * - 内存优化：延迟分配和增量构建策略
 * 
 * 使用场景：
 * - 脚本加载：加载预编译的Lua脚本文件
 * - 动态加载：运行时从各种数据源加载代码
 * - 模块系统：实现Lua模块的加载机制
 * - 代码缓存：加载缓存的编译结果
 * 
 * @param[in] L Lua虚拟机状态指针，不能为NULL
 * @param[in] Z ZIO输入流读取器指针，不能为NULL
 * @param[in] buff 内存缓冲区指针，不能为NULL
 * @param[in] name 数据源名称字符串，不能为NULL
 * 
 * @return 加载完成的函数原型指针
 * @retval Proto* 加载成功，返回有效的函数原型
 * @retval NULL 加载失败，不会正常返回(触发异常)
 * 
 * @pre L != NULL && Z != NULL && buff != NULL && name != NULL
 * @post 成功时返回完整的可执行函数原型
 * @post 失败时触发异常，不会正常返回
 * 
 * @note 生成的函数原型与Lua版本强相关
 * @note 数据源名称用于错误报告和调试信息
 * @warning ZIO读取器必须提供完整有效的字节码数据
 * @warning 损坏的字节码可能导致不可预测的错误
 * 
 * @since C99
 * @see ZIO, Mbuffer, Proto, LoadState, LoadHeader(), LoadFunction()
 */
Proto *luaU_undump(lua_State *L, ZIO *Z, Mbuffer *buff, const char *name)
{
    // 初始化加载状态：设置所有必要的参数和状态
    LoadState S;
    
    // 处理数据源名称：根据名称格式进行适当处理
    if (*name == '@' || *name == '=') {
        // 文件路径或显式名称：去除前缀字符
        S.name = name + 1;
    } else if (*name == LUA_SIGNATURE[0]) {
        // 二进制数据：设为标准名称
        S.name = "binary string";
    } else {
        // 其他情况：直接使用原名称
        S.name = name;
    }
    
    // 设置加载状态的其他字段
    S.L = L;                // Lua虚拟机状态
    S.Z = Z;                // 输入流读取器
    S.b = buff;             // 内存缓冲区
    
    // 验证字节码文件头部：检查格式和兼容性
    LoadHeader(&S);
    
    // 加载主函数原型：递归处理所有嵌套结构
    return LoadFunction(&S, luaS_newliteral(L, "=?"));
}

/**
 * @brief 字节码文件头生成函数：创建标准的Lua字节码文件头部
 * 
 * 详细说明：
 * 此函数生成Lua字节码文件的标准头部信息，包含了版本标识、平台信息、
 * 数据类型大小等关键元数据。这些信息用于确保字节码文件的正确性和
 * 兼容性检查，是字节码格式规范的重要组成部分。
 * 
 * 头部信息组成（按顺序）：
 * 1. Lua签名：LUA_SIGNATURE字符串，标识这是一个Lua字节码文件
 * 2. 版本号：LUAC_VERSION，Lua虚拟机版本用于兼容性检查
 * 3. 格式版本：LUAC_FORMAT，字节码格式版本号
 * 4. 字节序标识：大端序或小端序的检测字节
 * 5. int大小：sizeof(int)，整数类型的字节数
 * 6. size_t大小：sizeof(size_t)，大小类型的字节数
 * 7. Instruction大小：sizeof(Instruction)，虚拟机指令的字节数
 * 8. lua_Number大小：sizeof(lua_Number)，数值类型的字节数
 * 9. 数值类型标识：lua_Number是否为整数类型的检测
 * 
 * 字节序检测：
 * 使用一个整数变量的字节表示来检测平台的字节序：
 * - 小端序：低位字节存储在低地址（x86、x64等）
 * - 大端序：高位字节存储在低地址（某些RISC架构）
 * 
 * 数据类型大小检测：
 * 记录关键数据类型的大小信息，确保字节码只能在兼容的平台上运行：
 * - int：通常为4字节，但某些平台可能不同
 * - size_t：32位平台为4字节，64位平台为8字节
 * - Instruction：虚拟机指令大小，通常为4字节
 * - lua_Number：数值类型大小，通常为8字节(double)
 * 
 * lua_Number类型检测：
 * 检测lua_Number是否被配置为整数类型：
 * - 浮点数：(lua_Number)0.5 != 0，返回0
 * - 整数：(lua_Number)0.5 == 0，返回1
 * 
 * 兼容性保证：
 * 通过记录详细的平台和编译信息，确保字节码文件只能在兼容的环境中运行：
 * - 不同版本的Lua会拒绝加载不兼容的字节码
 * - 不同平台或编译配置会导致头部信息不匹配
 * - 字节序不同会导致数据解释错误
 * 
 * 安全考虑：
 * 头部验证是防止恶意字节码的重要机制：
 * - 防止加载为其他平台生成的字节码
 * - 检测损坏或篡改的字节码文件
 * - 确保数据类型的正确解释
 * 
 * 使用场景：
 * - ldump模块调用此函数生成字节码文件头
 * - lundump模块调用此函数生成标准头用于比较验证
 * - 字节码工具使用此函数进行格式检查
 * 
 * @param[out] h 头部缓冲区指针，必须至少LUAC_HEADERSIZE字节大小
 * 
 * @pre h != NULL
 * @pre 缓冲区大小至少为LUAC_HEADERSIZE字节
 * @post h中包含完整的标准字节码文件头部
 * 
 * @note 头部格式是二进制格式，不是文本格式
 * @note 头部长度固定为LUAC_HEADERSIZE字节
 * @note 函数会修改整个头部缓冲区的内容
 * @warning 不要修改生成的头部内容，会导致兼容性问题
 * @warning 缓冲区大小不足会导致缓冲区溢出
 * 
 * @since C99
 * @see LUA_SIGNATURE, LUAC_VERSION, LUAC_FORMAT, LUAC_HEADERSIZE
 */
void luaU_header(char *h)
{
    // 字节序检测变量：用于确定平台的字节序
    int x = 1;
    
    // === 写入Lua签名 ===
    // 复制Lua字节码签名到头部，不包括字符串终止符
    memcpy(h, LUA_SIGNATURE, sizeof(LUA_SIGNATURE) - 1);
    h += sizeof(LUA_SIGNATURE) - 1;
    
    // === 写入版本信息 ===
    *h++ = (char)LUAC_VERSION;          // Lua编译器版本号
    *h++ = (char)LUAC_FORMAT;           // 字节码格式版本号
    
    // === 写入平台信息 ===
    *h++ = (char)*(char *)&x;           // 字节序标识：小端序为1，大端序为0
    
    // === 写入数据类型大小 ===
    *h++ = (char)sizeof(int);           // int类型大小（通常为4）
    *h++ = (char)sizeof(size_t);        // size_t类型大小（32位为4，64位为8）
    *h++ = (char)sizeof(Instruction);   // 虚拟机指令大小（通常为4）
    *h++ = (char)sizeof(lua_Number);    // 数值类型大小（通常为8）
    
    // === 写入数值类型特征 ===
    // 检测lua_Number是否为整数类型：0表示浮点数，1表示整数
    *h++ = (char)(((lua_Number)0.5) == 0);
}
