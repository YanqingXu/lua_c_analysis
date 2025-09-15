/**
 * @file ldump.c
 * @brief Lua字节码序列化模块：将Lua函数原型转换为可执行字节码文件
 * 
 * 详细说明：
 * 此模块实现了Lua虚拟机编译器的后端功能，负责将解析和编译后的Lua函数原型
 * (Proto结构)序列化为紧凑的字节码格式。这个过程是Lua脚本预编译的核心组件，
 * 能够显著提升脚本加载速度并减少运行时开销。
 * 
 * 系统架构定位：
 * 在Lua虚拟机架构中，本模块处于编译器后端位置，与词法分析器(llex)、
 * 语法分析器(lparser)、代码生成器(lcode)协作，完成从源代码到字节码的
 * 完整转换流程。序列化后的字节码可以被lundump模块重新加载。
 * 
 * 技术特点：
 * - 使用递归下降的序列化策略处理嵌套函数结构
 * - 采用回调机制(lua_Writer)实现灵活的输出目标支持
 * - 支持调试信息的可选剥离，平衡文件大小和调试能力
 * - 实现了完整的Lua值类型序列化(nil、boolean、number、string)
 * - 提供统一的错误状态管理和错误传播机制
 * 
 * 依赖关系：
 * - lua.h: Lua核心API和基础类型定义
 * - lobject.h: Lua对象系统和值类型定义
 * - lstate.h: Lua状态机和虚拟机状态管理
 * - lundump.h: 字节码格式定义和加载接口
 * - stddef.h: 标准C库的size_t等基础类型
 * 
 * 编译要求：
 * - C标准版本：C99或更高版本
 * - 必须定义LUA_CORE宏以访问内部API
 * - 需要链接Lua核心库和标准C运行时库
 * - 支持函数指针和结构体的编译器
 * 
 * 使用示例：
 * @code
 * #include "ldump.h"
 * 
 * // 自定义写入器实现
 * static int file_writer(lua_State *L, const void *data, 
 *                       size_t size, void *ud) {
 *     FILE *f = (FILE*)ud;
 *     return (fwrite(data, size, 1, f) != 1) && (size != 0);
 * }
 * 
 * // 将函数原型序列化到文件
 * int save_bytecode(lua_State *L, const Proto *f, const char *filename) {
 *     FILE *file = fopen(filename, "wb");
 *     if (file == NULL) {
 *         return -1;
 *     }
 *     
 *     // 执行字节码dump，不剥离调试信息
 *     int status = luaU_dump(L, f, file_writer, file, 0);
 *     
 *     fclose(file);
 *     return status;
 * }
 * @endcode
 * 
 * 内存安全考虑：
 * 模块使用回调机制避免直接的内存分配，所有内存管理责任由调用者承担。
 * 通过lua_lock/lua_unlock机制确保在多线程环境中的状态一致性。
 * 严格的错误检查防止在序列化过程中出现数据损坏。
 * 
 * 性能特征：
 * - 时间复杂度：O(n)，其中n为函数原型的总大小
 * - 空间复杂度：O(1)，除了递归栈空间外不分配额外内存
 * - 优化策略：使用宏定义减少函数调用开销
 * - 瓶颈分析：主要瓶颈在I/O操作和回调函数的性能
 * 
 * 线程安全性：
 * 模块本身是无状态的，线程安全性取决于lua_State的使用。
 * 在序列化过程中使用lua_lock保护Lua状态，确保原子性操作。
 * 回调函数的线程安全性由用户实现保证。
 * 
 * 注意事项：
 * - 序列化的字节码格式与Lua版本强相关，不同版本间不兼容
 * - 调试信息剥离是不可逆操作，需要根据部署需求谨慎选择
 * - 回调函数必须正确处理所有可能的错误情况
 * - 大型函数的序列化可能导致深度递归，需要足够的栈空间
 * 
 * @author Roberto Ierusalimschy等Lua开发团队
 * @version 2.8.1.1 (Lua 5.1.5)
 * @date 2007/12/27
 * @since C99
 * @see lundump.h, lcode.h, lparser.h
 */

#include <stddef.h>

#define ldump_c
#define LUA_CORE

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"

/**
 * @brief 字节码序列化状态：管理整个dump过程的上下文和状态信息
 * 
 * 详细说明：
 * DumpState结构体封装了字节码序列化过程中需要的所有状态信息，
 * 包括Lua虚拟机状态、输出回调函数、用户数据、处理选项和错误状态。
 * 这种设计实现了状态的集中管理和错误的统一传播。
 * 
 * 设计理念：
 * 采用状态机模式，将复杂的序列化过程分解为一系列状态驱动的操作。
 * 通过回调机制实现输出目标的抽象，支持文件、内存、网络等多种输出方式。
 * 错误状态的及时检查和传播确保了序列化过程的可靠性。
 * 
 * 内存布局：
 * 结构体按照访问频率排列成员，最常用的status放在最后便于缓存。
 * 指针成员在64位系统上占用8字节，int成员占用4字节。
 * 结构体总大小在64位系统约为32字节，具有良好的缓存友好性。
 * 
 * 成员详细说明：
 * - L: 当前的Lua虚拟机状态指针，用于访问Lua内部API和状态管理
 * - writer: 用户提供的写入回调函数，实现具体的数据输出操作
 * - data: 传递给写入器的用户数据指针，通常为文件句柄或缓冲区
 * - strip: 调试信息剥离标志，1表示剥离调试信息以减小文件大小
 * - status: 当前序列化状态，0表示成功，非0表示发生错误
 * 
 * 生命周期管理：
 * 结构体在luaU_dump函数中栈上分配，生命周期与dump操作相同。
 * 初始化时设置所有必要字段，错误状态在首次错误时设置并保持。
 * 序列化完成后结构体自动销毁，不需要显式清理。
 * 
 * 使用模式：
 * @code
 * DumpState D;
 * D.L = L;                    // 设置Lua状态
 * D.writer = user_writer;     // 设置输出回调
 * D.data = user_data;         // 设置用户数据
 * D.strip = strip_debug;      // 设置剥离选项
 * D.status = 0;               // 初始化状态为成功
 * 
 * // 执行序列化操作
 * DumpHeader(&D);
 * if (D.status == 0) {
 *     DumpFunction(f, NULL, &D);
 * }
 * 
 * return D.status;            // 返回最终状态
 * @endcode
 * 
 * 并发访问：
 * 结构体实例与特定的dump操作绑定，不在多个线程间共享。
 * 通过lua_lock/lua_unlock保护对Lua状态的访问。
 * 用户提供的writer回调函数需要自行保证线程安全。
 * 
 * 性能影响：
 * 结构体大小适中，适合频繁的栈分配和访问。
 * 状态检查开销最小，错误传播机制高效。
 * 回调函数的性能直接影响整体序列化性能。
 * 
 * 扩展性考虑：
 * 结构体设计支持未来添加新的序列化选项。
 * 回调机制提供了良好的扩展性和定制能力。
 * 状态管理模式便于添加新的错误类型和处理逻辑。
 * 
 * 注意事项：
 * - 结构体必须完全初始化后才能使用
 * - 错误状态一旦设置就不会重置，确保错误的可靠传播
 * - writer回调函数不能为NULL，且必须正确处理所有输入
 * - 在dump过程中不应修改结构体内容
 * 
 * @since C99
 * @see luaU_dump(), lua_Writer, DumpBlock()
 */
typedef struct {
    lua_State *L;              /**< Lua虚拟机状态指针 */
    lua_Writer writer;         /**< 用户提供的写入回调函数 */
    void *data;                /**< 传递给写入器的用户数据 */
    int strip;                 /**< 调试信息剥离标志 */
    int status;                /**< 序列化状态码 */
} DumpState;

/**
 * @brief 内存块序列化宏：将指定大小的内存块写入输出流
 * 
 * 这个宏简化了内存块的序列化操作，通过计算总字节数来调用底层的
 * DumpBlock函数。常用于数组和结构体的批量序列化。
 * 
 * @param b 要序列化的内存块指针
 * @param n 元素个数
 * @param size 每个元素的字节大小
 * @param D DumpState状态指针
 */
#define DumpMem(b, n, size, D)    DumpBlock(b, (n) * (size), D)

/**
 * @brief 变量序列化宏：将单个变量的值写入输出流
 * 
 * 这个宏通过获取变量地址并指定其大小来序列化任意类型的变量。
 * 是最常用的序列化操作，适用于所有基础数据类型。
 * 
 * @param x 要序列化的变量
 * @param D DumpState状态指针
 */
#define DumpVar(x, D)             DumpMem(&x, 1, sizeof(x), D)

/**
 * @brief 核心数据块序列化函数：将任意内存块写入输出流
 * 
 * 详细说明：
 * 这是整个序列化系统的基础函数，所有其他序列化操作最终都会调用此函数。
 * 它负责将内存中的数据通过用户提供的writer回调函数写入目标输出流。
 * 函数实现了错误状态的检查和传播，确保序列化过程的可靠性。
 * 
 * 算法描述：
 * 1. 检查当前状态是否为成功(status == 0)
 * 2. 如果状态正常，释放Lua锁以允许回调函数执行
 * 3. 调用用户提供的writer函数执行实际的数据写入
 * 4. 重新获取Lua锁以保护虚拟机状态
 * 5. 更新状态码以反映写入操作的结果
 * 
 * 线程安全性：
 * 使用lua_unlock/lua_lock机制确保在回调执行期间释放Lua虚拟机锁，
 * 允许其他线程操作Lua状态，同时保护虚拟机状态的一致性。
 * 
 * 错误处理策略：
 * 采用"一次错误，永久失败"的策略，一旦发生错误就不再执行后续操作，
 * 确保错误状态的可靠传播和数据完整性。
 * 
 * 性能特征：
 * - 时间复杂度：O(1)，不考虑writer回调的复杂度
 * - 空间复杂度：O(1)，不分配额外内存
 * - 关键路径：writer回调函数的性能决定整体序列化性能
 * 
 * @param[in] b 要写入的内存块指针，不能为NULL
 * @param[in] size 内存块大小，以字节为单位，可以为0
 * @param[in,out] D 序列化状态指针，包含writer和错误状态
 * 
 * @pre b != NULL || size == 0
 * @pre D != NULL && D->writer != NULL
 * @post 如果D->status为0且写入成功，则D->status保持为0
 * @post 如果写入失败，则D->status被设置为writer返回的错误码
 * 
 * @note 函数不会修改输入的内存块内容
 * @note 在错误状态下函数会直接返回，不执行任何操作
 * @warning 回调函数必须正确处理所有可能的输入情况
 * 
 * @since C99
 * @see lua_Writer, DumpMem(), DumpVar()
 */
static void DumpBlock(const void *b, size_t size, DumpState *D)
{
    // 错误状态检查：如果已经发生错误，直接返回
    if (D->status == 0) {
        // 释放Lua锁：允许回调函数安全执行
        lua_unlock(D->L);
        
        // 执行写入操作：通过回调函数将数据写入目标
        D->status = (*D->writer)(D->L, b, size, D->data);
        
        // 重新获取Lua锁：保护虚拟机状态
        lua_lock(D->L);
    }
}

/**
 * @brief 字符序列化函数：将整数值作为单字节字符写入输出流
 * 
 * 详细说明：
 * 此函数将整数参数转换为char类型并序列化。主要用于序列化
 * 小范围的整数值，如标志位、枚举值等，以节省存储空间。
 * 
 * 数据转换：
 * 执行从int到char的显式类型转换，可能发生数据截断。
 * 调用者必须确保输入值在char类型的有效范围内(-128到127或0到255)。
 * 
 * 使用场景：
 * - Lua值类型标识(LUA_TNIL、LUA_TBOOLEAN等)
 * - 函数参数个数、upvalue个数等小整数
 * - 布尔值的序列化(0或1)
 * 
 * @param[in] y 要序列化的整数值，应在char范围内
 * @param[in,out] D 序列化状态指针
 * 
 * @warning 如果y超出char范围，会发生数据截断
 * @note 函数不检查输入值的范围，调用者负责确保数据有效性
 * 
 * @since C99
 * @see DumpInt(), DumpVar()
 */
static void DumpChar(int y, DumpState *D)
{
    // 类型转换：将int转换为char，可能发生截断
    char x = (char)y;
    
    // 序列化字符：使用标准变量序列化宏
    DumpVar(x, D);
}

/**
 * @brief 整数序列化函数：将int类型值写入输出流
 * 
 * 详细说明：
 * 这是基础整数类型的序列化函数，直接使用DumpVar宏将整数的
 * 二进制表示写入输出流。序列化的整数保持原始的字节序。
 * 
 * 数据格式：
 * 序列化后的数据为int类型的直接二进制表示，通常为4字节。
 * 字节序依赖于目标平台，在加载时必须使用相同的字节序。
 * 
 * 使用场景：
 * - 函数的行号信息(linedefined、lastlinedefined)
 * - 数组大小和元素计数
 * - 局部变量的作用域范围(startpc、endpc)
 * 
 * @param[in] x 要序列化的整数值
 * @param[in,out] D 序列化状态指针
 * 
 * @note 字节序依赖于平台，跨平台使用需要注意兼容性
 * @note 函数假设int类型为4字节，在特殊平台上可能不适用
 * 
 * @since C99
 * @see DumpChar(), DumpNumber(), DumpVar()
 */
static void DumpInt(int x, DumpState *D)
{
    // 直接序列化：使用标准变量序列化宏
    DumpVar(x, D);
}

/**
 * @brief Lua数值序列化函数：将lua_Number类型值写入输出流
 * 
 * 详细说明：
 * 序列化Lua的数值类型，在Lua 5.1中通常为double类型。
 * 这是Lua常量表中数值常量的序列化函数，保持高精度的浮点数表示。
 * 
 * 数据精度：
 * lua_Number通常定义为double，提供64位IEEE 754浮点精度。
 * 序列化保持原始的二进制表示，确保数值的完全精确恢复。
 * 
 * 跨平台考虑：
 * 浮点数的二进制表示在不同平台上可能存在差异，包括字节序
 * 和浮点格式。在跨平台部署时需要确保兼容性。
 * 
 * 特殊值处理：
 * 支持IEEE 754的特殊值如NaN、正无穷、负无穷等。
 * 这些特殊值的序列化和反序列化需要特别的注意。
 * 
 * @param[in] x 要序列化的Lua数值
 * @param[in,out] D 序列化状态指针
 * 
 * @note 依赖于lua_Number的具体定义，通常为double类型
 * @note 特殊值(NaN、Inf)的处理依赖于平台的IEEE 754支持
 * @warning 跨平台使用时需要确保浮点格式的兼容性
 * 
 * @since C99
 * @see lua_Number, DumpVar(), DumpConstants()
 */
static void DumpNumber(lua_Number x, DumpState *D)
{
    // 直接序列化：保持浮点数的完整精度
    DumpVar(x, D);
}

/**
 * @brief 向量数据序列化函数：将数组类型的数据写入输出流
 * 
 * 详细说明：
 * 此函数实现了数组数据的序列化，首先写入数组长度，然后写入数组内容。
 * 这种格式设计使得在反序列化时能够正确恢复数组的大小和内容。
 * 
 * 序列化格式：
 * 1. 写入数组元素个数(int类型)
 * 2. 写入数组的完整内容(连续的内存块)
 * 
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为数组的总字节数
 * - 空间复杂度：O(1)，不分配额外内存
 * 
 * 使用场景：
 * - 函数字节码数组的序列化
 * - 行号信息数组的序列化
 * - 局部变量信息数组的序列化
 * 
 * @param[in] b 数组数据指针，指向要序列化的数组首地址
 * @param[in] n 数组元素个数，可以为0表示空数组
 * @param[in] size 每个数组元素的字节大小
 * @param[in,out] D 序列化状态指针
 * 
 * @pre b != NULL || n == 0
 * @pre size > 0 || n == 0
 * @post 输出流中包含元素个数和数组内容
 * 
 * @note 空数组(n=0)会写入长度0，但不写入任何内容
 * @note 调用者必须确保b指向足够大小的有效内存区域
 * 
 * @since C99
 * @see DumpInt(), DumpMem(), DumpCode()
 */
static void DumpVector(const void *b, int n, size_t size, DumpState *D)
{
    // 序列化数组长度：写入元素个数用于反序列化时分配内存
    DumpInt(n, D);
    
    // 序列化数组内容：写入整个数组的二进制数据
    DumpMem(b, n, size, D);
}

/**
 * @brief Lua字符串序列化函数：将TString对象写入输出流
 * 
 * 详细说明：
 * 此函数处理Lua内部字符串对象(TString)的序列化，支持NULL字符串
 * 和空字符串的特殊情况。序列化格式包含字符串长度和字符串内容，
 * 确保在反序列化时能够正确重建字符串对象。
 * 
 * 序列化策略：
 * 1. NULL字符串或空字符串：写入长度0
 * 2. 非空字符串：写入长度(包括终止符)和完整字符串内容
 * 
 * 数据格式：
 * - 长度字段：size_t类型，表示字符串字节数(包括'\0')
 * - 内容字段：原始字符串数据(包括终止的'\0'字符)
 * 
 * 特殊情况处理：
 * - NULL指针：被视为空字符串，写入长度0
 * - 空字符串：通过getstr(s)返回NULL来检测
 * - 长度计算：s->tsv.len + 1 确保包含终止符
 * 
 * 内存安全：
 * 函数在访问字符串内容前进行NULL检查，防止解引用空指针。
 * 使用Lua内部的getstr宏安全获取字符串指针。
 * 
 * 性能考虑：
 * - 时间复杂度：O(n)，其中n为字符串长度
 * - 空间复杂度：O(1)，不分配额外内存
 * - 优化：直接写入内存块，避免字符级别的复制
 * 
 * @param[in] s TString指针，可以为NULL
 * @param[in,out] D 序列化状态指针
 * 
 * @post 输出流包含字符串长度和内容(如果非空)
 * @note NULL字符串和空字符串都序列化为长度0
 * @note 字符串长度包括终止的'\0'字符
 * @warning 依赖于TString结构的内部布局和getstr宏
 * 
 * @since C99
 * @see TString, getstr(), DumpVar(), DumpBlock()
 */
static void DumpString(const TString *s, DumpState *D)
{
    // 空字符串检查：处理NULL指针或空字符串的情况
    if (s == NULL || getstr(s) == NULL) {
        // 序列化空字符串：写入长度0表示无内容
        size_t size = 0;
        DumpVar(size, D);
    } else {
        // 计算字符串长度：包括终止的'\0'字符
        size_t size = s->tsv.len + 1;
        
        // 序列化字符串长度：用于反序列化时分配内存
        DumpVar(size, D);
        
        // 序列化字符串内容：写入完整的字符串数据
        DumpBlock(getstr(s), size, D);
    }
}

/**
 * @brief 函数字节码序列化宏：将函数的字节码数组写入输出流
 * 
 * 这个宏专门用于序列化Lua函数的字节码指令数组。它使用DumpVector
 * 来处理指令数组，每个指令的大小为sizeof(Instruction)。
 * 
 * 字节码特点：
 * - 指令数组是函数的核心执行逻辑
 * - 每条指令为固定大小的Instruction类型
 * - 指令数量存储在f->sizecode字段中
 * 
 * @param f Proto函数原型指针
 * @param D DumpState序列化状态指针
 */
#define DumpCode(f, D)           DumpVector(f->code, f->sizecode, sizeof(Instruction), D)

/**
 * @brief 函数原型序列化函数：递归序列化嵌套的函数定义
 * 
 * 前向声明，用于处理函数间的相互递归调用关系。
 * 主要用于序列化函数内部定义的嵌套函数和闭包。
 */
static void DumpFunction(const Proto *f, const TString *p, DumpState *D);

/**
 * @brief 常量表序列化函数：将函数的常量和嵌套函数写入输出流
 * 
 * 详细说明：
 * 此函数负责序列化Lua函数的常量表和嵌套函数原型。常量表包含函数中
 * 使用的所有字面量值(nil、boolean、number、string)，嵌套函数包含
 * 在当前函数内部定义的所有子函数。
 * 
 * 序列化结构：
 * 1. 常量数组长度(int)
 * 2. 每个常量的类型标识(char)和值
 * 3. 嵌套函数数量(int)
 * 4. 每个嵌套函数的完整序列化数据
 * 
 * 常量类型处理：
 * - LUA_TNIL: 只写入类型，无附加数据
 * - LUA_TBOOLEAN: 写入类型和布尔值(char)
 * - LUA_TNUMBER: 写入类型和数值(lua_Number)
 * - LUA_TSTRING: 写入类型和字符串对象
 * 
 * 递归处理：
 * 对于嵌套函数，递归调用DumpFunction进行完整的函数序列化，
 * 形成深度优先的序列化顺序。
 * 
 * 算法复杂度：
 * - 时间复杂度：O(k + Σf_i)，k为常量数，f_i为各嵌套函数大小
 * - 空间复杂度：O(d)，d为函数嵌套深度(递归栈空间)
 * 
 * @param[in] f 函数原型指针，包含常量表和嵌套函数信息
 * @param[in,out] D 序列化状态指针
 * 
 * @pre f != NULL && f->k != NULL(如果f->sizek > 0)
 * @pre f->p != NULL(如果f->sizep > 0)
 * @post 输出流包含完整的常量表和嵌套函数数据
 * 
 * @note 函数会递归处理所有层级的嵌套函数
 * @warning 深度嵌套可能导致栈溢出，需要足够的栈空间
 * 
 * @since C99
 * @see Proto, TValue, DumpFunction(), ttype(), bvalue(), nvalue(), rawtsvalue()
 */
static void DumpConstants(const Proto *f, DumpState *D)
{
    // 获取常量数组长度
    int i, n = f->sizek;
    
    // 序列化常量数组长度：用于反序列化时分配常量表
    DumpInt(n, D);
    
    // 遍历所有常量：逐个序列化每个常量值
    for (i = 0; i < n; i++) {
        // 获取当前常量的TValue指针
        const TValue *o = &f->k[i];
        
        // 序列化常量类型：写入Lua值类型标识
        DumpChar(ttype(o), D);
        
        // 根据常量类型进行不同的序列化处理
        switch (ttype(o)) {
        case LUA_TNIL:
            // nil值：只需要类型信息，无附加数据
            break;
            
        case LUA_TBOOLEAN:
            // 布尔值：序列化布尔值(0或1)
            DumpChar(bvalue(o), D);
            break;
            
        case LUA_TNUMBER:
            // 数值：序列化lua_Number类型的数值
            DumpNumber(nvalue(o), D);
            break;
            
        case LUA_TSTRING:
            // 字符串：序列化TString对象
            DumpString(rawtsvalue(o), D);
            break;
            
        default:
            // 不应该到达这里：常量表中不应包含其他类型
            lua_assert(0);
            break;
        }
    }
    
    // 获取嵌套函数数量
    n = f->sizep;
    
    // 序列化嵌套函数数量：用于反序列化时分配函数数组
    DumpInt(n, D);
    
    // 递归序列化所有嵌套函数：深度优先遍历函数树
    for (i = 0; i < n; i++) {
        DumpFunction(f->p[i], f->source, D);
    }
}

/**
 * @brief 调试信息序列化函数：将函数的调试数据写入输出流
 * 
 * 详细说明：
 * 此函数处理Lua函数调试信息的序列化，包括行号信息、局部变量信息
 * 和upvalue信息。这些信息对于调试器、错误报告和代码分析工具非常重要。
 * 支持通过strip标志控制是否包含调试信息以减小字节码文件大小。
 * 
 * 调试信息组成：
 * 1. 行号信息数组：每条指令对应的源代码行号
 * 2. 局部变量信息：变量名、作用域范围(startpc、endpc)
 * 3. Upvalue信息：闭包中使用的外部变量名称
 * 
 * 剥离机制：
 * 当D->strip为真时，所有调试信息都被剥离：
 * - 行号数组长度写为0
 * - 局部变量数量写为0
 * - Upvalue数量写为0
 * 这可以显著减小字节码文件大小，但会失去调试能力。
 * 
 * 序列化格式：
 * 1. 行号数组(可选剥离)
 * 2. 局部变量数量 + 每个变量的详细信息(可选剥离)
 * 3. Upvalue数量 + 每个upvalue的名称(可选剥离)
 * 
 * 性能影响：
 * 调试信息通常占字节码文件的很大比例，剥离后可以：
 * - 减小文件大小50-70%
 * - 加快加载速度
 * - 减少内存占用
 * 但会失去调试和错误定位能力。
 * 
 * @param[in] f 函数原型指针，包含所有调试信息
 * @param[in,out] D 序列化状态指针，包含剥离选项
 * 
 * @pre f != NULL
 * @post 输出流包含调试信息(除非被剥离)
 * 
 * @note 剥离操作是不可逆的，需要根据部署需求谨慎选择
 * @note 局部变量的作用域信息对于调试器至关重要
 * @warning 剥离调试信息后无法进行源码级调试
 * 
 * @since C99
 * @see Proto, LocVar, DumpVector(), DumpString(), DumpInt()
 */
static void DumpDebug(const Proto *f, DumpState *D)
{
    int i, n;
    
    // 序列化行号信息数组
    // 根据剥离标志决定是否包含行号信息
    n = (D->strip) ? 0 : f->sizelineinfo;
    DumpVector(f->lineinfo, n, sizeof(int), D);
    
    // 序列化局部变量信息
    // 根据剥离标志决定是否包含局部变量调试信息
    n = (D->strip) ? 0 : f->sizelocvars;
    DumpInt(n, D);
    
    // 逐个序列化局部变量的详细信息
    for (i = 0; i < n; i++) {
        // 变量名：用于调试器显示变量名称
        DumpString(f->locvars[i].varname, D);
        
        // 作用域起始：变量开始有效的指令位置
        DumpInt(f->locvars[i].startpc, D);
        
        // 作用域结束：变量失效的指令位置
        DumpInt(f->locvars[i].endpc, D);
    }
    
    // 序列化Upvalue信息
    // 根据剥离标志决定是否包含upvalue调试信息
    n = (D->strip) ? 0 : f->sizeupvalues;
    DumpInt(n, D);
    
    // 逐个序列化upvalue的名称信息
    for (i = 0; i < n; i++) {
        // Upvalue名称：用于调试器显示闭包变量名
        DumpString(f->upvalues[i], D);
    }
}

/**
 * @brief 函数原型序列化函数：将完整的Lua函数定义写入输出流
 * 
 * 详细说明：
 * 这是函数序列化的核心函数，负责将一个完整的Lua函数原型(Proto)
 * 序列化为字节码格式。函数按照严格的顺序序列化所有组成部分，
 * 确保反序列化时能够完整重建函数对象。
 * 
 * 序列化顺序和内容：
 * 1. 源文件名：函数定义所在的源文件(可选剥离)
 * 2. 函数位置：在源文件中的起始和结束行号
 * 3. 函数签名：upvalue数量、参数数量、可变参数标志
 * 4. 栈信息：函数执行时需要的最大栈大小
 * 5. 字节码：函数的指令序列
 * 6. 常量表：函数使用的常量和嵌套函数
 * 7. 调试信息：行号、局部变量、upvalue名称(可选剥离)
 * 
 * 源文件优化：
 * 当函数与父函数来自同一源文件或启用剥离时，源文件名被优化为NULL，
 * 避免重复存储相同的源文件路径，减小字节码大小。
 * 
 * 函数特征信息：
 * - nups: upvalue数量，表示闭包引用的外部变量个数
 * - numparams: 固定参数数量，不包括可变参数
 * - is_vararg: 可变参数标志，指示函数是否接受可变数量参数
 * - maxstacksize: 最大栈大小，虚拟机执行时需要的栈空间
 * 
 * 递归结构：
 * 通过调用DumpConstants处理嵌套函数，形成深度优先的序列化，
 * 支持任意层级的函数嵌套结构。
 * 
 * @param[in] f 要序列化的函数原型指针
 * @param[in] p 父函数的源文件名，用于源文件优化
 * @param[in,out] D 序列化状态指针
 * 
 * @pre f != NULL
 * @post 输出流包含完整的函数定义数据
 * 
 * @note 源文件名的处理对字节码大小优化很重要
 * @note 函数签名信息对虚拟机执行至关重要
 * @warning 序列化顺序不能改变，必须与lundump中的顺序一致
 * 
 * @since C99
 * @see Proto, DumpString(), DumpInt(), DumpChar(), DumpCode(), DumpConstants(), DumpDebug()
 */
static void DumpFunction(const Proto *f, const TString *p, DumpState *D)
{
    // 序列化源文件名：优化重复的源文件路径
    // 如果与父函数同源或启用剥离，则写入NULL节省空间
    DumpString((f->source == p || D->strip) ? NULL : f->source, D);
    
    // 序列化函数位置信息：在源文件中的行号范围
    DumpInt(f->linedefined, D);        // 函数定义开始行号
    DumpInt(f->lastlinedefined, D);    // 函数定义结束行号
    
    // 序列化函数签名信息：函数的基本特征参数
    DumpChar(f->nups, D);              // upvalue数量(闭包变量个数)
    DumpChar(f->numparams, D);         // 固定参数数量
    DumpChar(f->is_vararg, D);         // 可变参数标志
    DumpChar(f->maxstacksize, D);      // 最大栈大小需求
    
    // 序列化函数字节码：函数的核心执行逻辑
    DumpCode(f, D);
    
    // 序列化常量表和嵌套函数：函数依赖的数据和子函数
    DumpConstants(f, D);
    
    // 序列化调试信息：用于调试和错误报告(可选剥离)
    DumpDebug(f, D);
}

/**
 * @brief 字节码文件头序列化函数：写入Lua字节码文件的标准头部
 * 
 * 详细说明：
 * 此函数生成并写入Lua字节码文件的标准头部信息。头部包含了
 * 版本标识、平台信息、数据类型大小等关键元数据，用于确保
 * 字节码文件的正确性和兼容性检查。
 * 
 * 头部信息组成：
 * 1. Lua签名：标识这是一个Lua字节码文件
 * 2. 版本号：Lua虚拟机版本，用于兼容性检查
 * 3. 格式版本：字节码格式版本号
 * 4. 字节序标识：大端序或小端序标记
 * 5. 数据类型大小：int、size_t、Instruction等类型的字节数
 * 6. lua_Number类型：数值类型的具体实现信息
 * 
 * 兼容性保证：
 * 头部信息确保字节码只能在兼容的Lua虚拟机上运行：
 * - 不同版本的Lua会拒绝加载不兼容的字节码
 * - 不同平台或编译配置会导致数据类型大小不匹配
 * - 字节序不同会导致数据解释错误
 * 
 * 安全考虑：
 * 头部验证是防止恶意字节码的第一道防线，通过严格的
 * 格式检查可以及早发现损坏或篡改的字节码文件。
 * 
 * 性能影响：
 * 头部信息很小(通常几十字节)，对文件大小影响微小，
 * 但对加载时的兼容性检查非常重要。
 * 
 * @param[in,out] D 序列化状态指针
 * 
 * @pre D != NULL && D->writer != NULL
 * @post 输出流包含标准的Lua字节码文件头
 * 
 * @note 头部格式由luaU_header函数定义，不应手动修改
 * @note 头部长度为LUAC_HEADERSIZE字节
 * @warning 头部格式变化会导致版本间不兼容
 * 
 * @since C99
 * @see luaU_header(), LUAC_HEADERSIZE, DumpBlock()
 */
static void DumpHeader(DumpState *D)
{
    // 分配头部缓冲区：用于存储生成的头部信息
    char h[LUAC_HEADERSIZE];
    
    // 生成标准头部：调用lundump模块的头部生成函数
    luaU_header(h);
    
    // 写入头部数据：将完整的头部信息写入输出流
    DumpBlock(h, LUAC_HEADERSIZE, D);
}

/**
 * @brief Lua函数字节码序列化主接口：将函数原型转换为字节码文件
 * 
 * 详细说明：
 * 这是ldump模块的主要公共接口，提供了完整的Lua函数字节码序列化功能。
 * 函数接受一个编译后的函数原型，通过用户提供的写入器将其转换为
 * 标准的Lua字节码格式，可以保存到文件或其他存储介质中。
 * 
 * 序列化流程：
 * 1. 初始化序列化状态(DumpState)
 * 2. 写入字节码文件头部信息
 * 3. 序列化主函数原型(递归处理所有嵌套函数)
 * 4. 返回最终的错误状态
 * 
 * 参数详解：
 * - L: Lua虚拟机状态，提供内存管理和锁机制
 * - f: 要序列化的函数原型，通常是编译器的输出
 * - w: 用户提供的写入器回调函数，定义输出目标
 * - data: 传递给写入器的用户数据，通常为文件句柄或缓冲区
 * - strip: 调试信息剥离标志，1表示移除调试信息以减小大小
 * 
 * 写入器接口：
 * 写入器函数必须符合lua_Writer接口规范：
 * - 参数：lua_State*, const void*, size_t, void*
 * - 返回：0表示成功，非0表示错误
 * - 职责：将指定大小的数据写入目标位置
 * 
 * 错误处理：
 * 函数采用"快速失败"策略，一旦写入器返回错误就停止后续操作。
 * 错误状态会被保留并作为函数返回值，便于调用者处理。
 * 
 * 内存管理：
 * 函数本身不分配持久内存，所有内存操作通过回调函数完成。
 * 使用栈分配的DumpState管理序列化状态，函数结束时自动清理。
 * 
 * 线程安全：
 * 函数使用Lua的锁机制保护虚拟机状态，在回调执行期间会临时释放锁，
 * 允许其他线程操作Lua状态，但要求回调函数本身是线程安全的。
 * 
 * 性能特征：
 * - 时间复杂度：O(n)，n为函数原型的总大小
 * - 空间复杂度：O(d)，d为函数嵌套深度
 * - I/O依赖：性能主要受写入器的I/O性能限制
 * 
 * 使用场景：
 * - 脚本预编译：提高脚本加载速度
 * - 代码保护：隐藏源代码实现细节
 * - 分发优化：减小发布包大小(启用strip)
 * - 缓存机制：缓存编译结果避免重复编译
 * 
 * @param[in] L Lua虚拟机状态指针，不能为NULL
 * @param[in] f 要序列化的函数原型指针，不能为NULL
 * @param[in] w 写入器回调函数指针，不能为NULL
 * @param[in] data 传递给写入器的用户数据指针，可以为NULL
 * @param[in] strip 调试信息剥离标志，0保留调试信息，1剥离调试信息
 * 
 * @return 序列化状态码
 * @retval 0 序列化成功完成
 * @retval 非0 序列化过程中发生错误，值为写入器返回的错误码
 * 
 * @pre L != NULL && f != NULL && w != NULL
 * @post 成功时输出完整有效的Lua字节码
 * @post 失败时输出内容可能不完整，不应使用
 * 
 * @note 生成的字节码格式与Lua版本强相关
 * @note strip选项是不可逆的，需要根据需求谨慎选择
 * @warning 回调函数必须正确处理所有可能的输入
 * @warning 大型函数可能需要足够的栈空间处理深度递归
 * 
 * @since C99
 * @see lua_Writer, Proto, DumpState, DumpHeader(), DumpFunction()
 */
int luaU_dump(lua_State *L, const Proto *f, lua_Writer w, void *data, int strip)
{
    // 初始化序列化状态：设置所有必要的参数和状态
    DumpState D;
    D.L = L;                // Lua虚拟机状态
    D.writer = w;           // 用户提供的写入器
    D.data = data;          // 传递给写入器的用户数据
    D.strip = strip;        // 调试信息剥离选项
    D.status = 0;           // 初始状态为成功
    
    // 写入字节码文件头：包含版本和兼容性信息
    DumpHeader(&D);
    
    // 序列化主函数：递归处理整个函数树结构
    DumpFunction(f, NULL, &D);
    
    // 返回最终状态：0表示成功，非0表示错误
    return D.status;
}
