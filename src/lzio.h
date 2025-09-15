/**
 * @file lzio.h
 * @brief Lua缓冲I/O系统接口：实现高效的流式字符读取和缓冲区管理
 * 
 * 详细说明：
 * 这个头文件定义了Lua的缓冲I/O系统，提供了统一的流式输入抽象。
 * 该系统将不同的输入源（文件、字符串、网络等）抽象为统一的字符
 * 流接口，并通过缓冲机制优化读取性能。ZIO系统是Lua词法分析器
 * 和字节码加载器的基础，为高层组件提供了高效、一致的字符输入
 * 服务。
 * 
 * 系统架构定位：
 * - 位于Lua输入处理的底层，为所有输入操作提供统一抽象
 * - 与词法分析器(llex)紧密配合，提供高效的字符流读取
 * - 与字节码加载器(lundump)协作，支持二进制数据的流式读取
 * - 与内存管理器(lmem)集成，实现动态缓冲区的高效管理
 * 
 * 技术特点：
 * - 统一抽象：将各种输入源抽象为一致的流接口
 * - 缓冲优化：通过预读和缓冲减少I/O操作次数
 * - Reader模式：使用函数指针实现可扩展的输入源支持
 * - 零拷贝设计：尽量避免不必要的数据拷贝
 * - 动态缓冲：根据需要自动调整缓冲区大小
 * - 错误处理：集成的错误检测和传播机制
 * 
 * 依赖关系：
 * - lua.h: Lua核心接口，提供基本类型和Reader函数类型
 * - lmem.h: 内存管理器，提供动态内存分配功能
 * 
 * 编译要求：
 * - C标准版本：C99或更高版本
 * - 函数指针：支持Reader回调函数机制
 * - 动态内存：支持malloc/realloc/free操作
 * - 字符处理：正确的字符类型转换
 * 
 * 使用示例：
 * @code
 * #include "lzio.h"
 * #include "lua.h"
 * 
 * // 字符串Reader函数示例
 * const char* string_reader(lua_State *L, void *data, size_t *size) {
 *     struct { const char *s; size_t len; } *ctx = data;
 *     if (ctx->len == 0) return NULL;  // EOF
 *     *size = ctx->len;
 *     ctx->len = 0;  // 标记为已读
 *     return ctx->s;
 * }
 * 
 * // 从字符串创建ZIO流
 * lua_State *L = luaL_newstate();
 * const char *script = "print('Hello, World!')";
 * struct { const char *s; size_t len; } ctx = { script, strlen(script) };
 * 
 * ZIO z;
 * luaZ_init(L, &z, string_reader, &ctx);
 * 
 * // 逐字符读取
 * int c;
 * while ((c = zgetc(&z)) != EOZ) {
 *     printf("%c", c);
 * }
 * 
 * // 使用动态缓冲区
 * Mbuffer buff;
 * luaZ_initbuffer(L, &buff);
 * 
 * // 分配缓冲区空间
 * char *space = luaZ_openspace(L, &buff, 100);
 * strcpy(space, "动态缓冲区内容");
 * buff.n = strlen(space);
 * 
 * printf("缓冲区内容: %s\n", luaZ_buffer(&buff));
 * printf("缓冲区长度: %zu\n", luaZ_bufflen(&buff));
 * 
 * // 清理资源
 * luaZ_freebuffer(L, &buff);
 * lua_close(L);
 * @endcode
 * 
 * I/O系统架构：
 * - ZIO流：统一的字符流抽象
 * - Reader接口：可扩展的输入源适配
 * - 缓冲机制：高效的预读和批量处理
 * - 动态内存：按需分配的缓冲区管理
 * 
 * 性能特征：
 * - 缓冲读取：减少系统调用次数
 * - 预读机制：提高字符访问效率
 * - 零拷贝：直接从缓冲区返回数据
 * - 内存复用：动态调整缓冲区大小
 * 
 * 输入源支持：
 * - 文件流：标准文件I/O
 * - 字符串：内存中的字符数据
 * - 网络流：套接字等网络输入
 * - 自定义源：用户定义的Reader函数
 * 
 * 错误处理：
 * - EOF检测：流结束的正确识别
 * - I/O错误：底层读取错误的传播
 * - 内存错误：缓冲区分配失败的处理
 * - 格式错误：无效输入的检测
 * 
 * 注意事项：
 * - ZIO流是单向的，只支持顺序读取
 * - 缓冲区在使用后需要正确释放
 * - Reader函数需要正确处理EOF和错误
 * - 字符编码需要与Lua的期望保持一致
 * 
 * @author Roberto Ierusalimschy
 * @version 1.21.1.1
 * @date 2007/12/27
 * @since Lua 5.1
 * @see lua.h, lmem.h, llex.h
 */

#ifndef lzio_h
#define lzio_h

#include "lua.h"
#include "lmem.h"

/**
 * @brief 流结束标志：表示输入流已到达末尾
 * 
 * 这个常量用于标识输入流的结束状态。当zgetc()等读取函数
 * 遇到流末尾时，会返回这个特殊值来通知调用者没有更多
 * 数据可读。
 * 
 * 设计考虑：
 * - 使用-1是C语言的传统约定
 * - 与标准库函数（如getc）保持一致
 * - 确保与有效字符值不冲突
 * 
 * @note 值为-1，与C标准库的EOF约定一致
 */
#define EOZ    (-1)

/* 前向声明：ZIO结构体的类型别名 */
typedef struct Zio ZIO;

/**
 * @brief 字符转整数：安全的字符到整数转换宏
 * 
 * 详细说明：
 * 这个宏将字符安全地转换为整数，避免符号扩展问题。
 * 在某些系统上，char可能是有符号类型，直接转换为int
 * 可能导致负值，这个宏确保结果总是正值。
 * 
 * 转换过程：
 * 1. 先转换为unsigned char，消除符号位
 * 2. 再转换为int，保证结果为正值
 * 3. 使用cast宏保证类型安全
 * 
 * 应用场景：
 * - 字符读取函数的返回值处理
 * - 词法分析中的字符分类
 * - 确保字符值在0-255范围内
 * 
 * @param c 要转换的字符
 * @return 对应的非负整数值（0-255）
 * 
 * @note 这是避免符号扩展的标准技巧
 */
#define char2int(c)    cast(int, cast(unsigned char, (c)))

/**
 * @brief 读取字符：从ZIO流中读取下一个字符
 * 
 * 详细说明：
 * 这是ZIO系统最重要的宏，提供了高效的字符读取机制。
 * 它首先检查内部缓冲区是否有数据，如果有则直接返回；
 * 否则调用luaZ_fill()函数重新填充缓冲区。
 * 
 * 工作原理：
 * 1. 检查z->n（剩余字节数）是否大于0
 * 2. 如果是，递减计数器并返回当前字符
 * 3. 如果否，调用luaZ_fill()重新填充缓冲区
 * 4. 使用char2int确保返回值为正整数
 * 
 * 性能优化：
 * - 内联操作：大多数情况下是简单的指针操作
 * - 缓冲预读：减少函数调用开销
 * - 分支预测：正常情况优先处理
 * 
 * 返回值：
 * - 成功：返回字符的整数值（0-255）
 * - 失败：返回EOZ表示流结束或错误
 * 
 * 使用注意：
 * - 每次调用都会推进流位置
 * - 不能回退或重复读取
 * - 需要检查返回值是否为EOZ
 * 
 * @param z ZIO流指针
 * @return 下一个字符的整数值，或EOZ表示流结束
 * 
 * @note 这是性能关键的宏，经过高度优化
 * @see luaZ_fill(), char2int()
 */
#define zgetc(z)  (((z)->n--)>0 ?  char2int(*(z)->p++) : luaZ_fill(z))

/**
 * @brief 动态缓冲区：可自动增长的内存缓冲区结构
 * 
 * 详细说明：
 * Mbuffer是一个通用的动态缓冲区实现，主要用于存储可变长度
 * 的数据。它在词法分析、字符串构建、临时数据存储等场景中
 * 广泛使用。缓冲区会根据需要自动扩展，并提供高效的内存管理。
 * 
 * 设计特点：
 * - 动态扩展：根据需要自动增长
 * - 内存复用：支持重置和重复使用
 * - 高效分配：使用Lua的内存管理器
 * - 简单接口：提供易用的操作宏
 * 
 * 生命周期：
 * 1. 初始化：luaZ_initbuffer()设置初始状态
 * 2. 使用：通过各种宏进行操作
 * 3. 扩展：luaZ_openspace()按需分配空间
 * 4. 清理：luaZ_freebuffer()释放内存
 * 
 * 内存策略：
 * - 延迟分配：初始时不分配内存
 * - 指数增长：扩展时使用合理的增长策略
 * - 内存对齐：确保良好的缓存性能
 * 
 * 应用场景：
 * - 词法分析：累积token内容
 * - 字符串构建：动态拼接字符
 * - 临时缓存：存储中间结果
 * - 数据缓冲：I/O操作的缓冲区
 */
typedef struct Mbuffer {
    /**
     * @brief 缓冲区指针：指向实际的内存空间
     * 
     * 指向动态分配的内存区域，存储缓冲区的数据内容。
     * 初始时为NULL，在第一次使用时分配。
     */
    char *buffer;

    /**
     * @brief 当前长度：缓冲区中有效数据的字节数
     * 
     * 表示当前存储在缓冲区中的数据量，不包括未使用的空间。
     * 这个值总是小于等于buffsize。
     */
    size_t n;

    /**
     * @brief 缓冲区大小：已分配的总内存大小
     * 
     * 表示当前分配的内存空间大小，当n达到这个值时，
     * 需要重新分配更大的空间。
     */
    size_t buffsize;
} Mbuffer;

/**
 * @brief 初始化缓冲区：设置缓冲区的初始状态
 * 
 * 这个宏将缓冲区设置为初始状态，不分配实际内存。
 * 采用延迟分配策略，只在实际需要时才分配内存。
 * 
 * @param L Lua状态机指针（此处未使用，为接口一致性保留）
 * @param buff 要初始化的缓冲区指针
 * 
 * @note 使用逗号操作符执行多个赋值
 */
#define luaZ_initbuffer(L, buff) ((buff)->buffer = NULL, (buff)->buffsize = 0)

/**
 * @brief 获取缓冲区：返回缓冲区的内存指针
 * 
 * @param buff 缓冲区指针
 * @return 指向缓冲区内存的char指针
 */
#define luaZ_buffer(buff)    ((buff)->buffer)

/**
 * @brief 获取缓冲区大小：返回分配的总大小
 * 
 * @param buff 缓冲区指针
 * @return 缓冲区的总容量（字节数）
 */
#define luaZ_sizebuffer(buff)    ((buff)->buffsize)

/**
 * @brief 获取缓冲区长度：返回有效数据的长度
 * 
 * @param buff 缓冲区指针
 * @return 当前存储的数据长度（字节数）
 */
#define luaZ_bufflen(buff)    ((buff)->n)

/**
 * @brief 重置缓冲区：清空缓冲区内容但保留内存
 * 
 * 这个宏将缓冲区的长度重置为0，但不释放已分配的内存，
 * 允许重复使用相同的内存空间。
 * 
 * @param buff 缓冲区指针
 * 
 * @note 只重置长度，不清零内存内容
 */
#define luaZ_resetbuffer(buff) ((buff)->n = 0)

/**
 * @brief 调整缓冲区大小：重新分配缓冲区内存
 * 
 * 详细说明：
 * 这个宏使用内存管理器重新分配缓冲区的内存空间。
 * 它可以扩大或缩小缓冲区，并更新大小字段。
 * 
 * 重分配过程：
 * 1. 调用luaM_reallocvector进行内存重分配
 * 2. 更新buffsize字段为新的大小
 * 3. 保持现有数据的完整性（在新大小允许范围内）
 * 
 * 使用场景：
 * - 扩大缓冲区：当前空间不足时
 * - 缩小缓冲区：释放多余的内存
 * - 释放缓冲区：大小为0时完全释放
 * 
 * @param L Lua状态机指针，用于内存分配
 * @param buff 要调整的缓冲区指针
 * @param size 新的大小（字节数）
 * 
 * @note 内存重分配可能失败，调用者需要检查错误
 * @warning 重分配后buffer指针可能改变
 */
#define luaZ_resizebuffer(L, buff, size) \
    (luaM_reallocvector(L, (buff)->buffer, (buff)->buffsize, size, char), \
    (buff)->buffsize = size)

/**
 * @brief 释放缓冲区：完全释放缓冲区内存
 * 
 * 这个宏通过将大小调整为0来释放缓冲区的所有内存。
 * 释放后缓冲区回到初始状态，可以重新使用。
 * 
 * @param L Lua状态机指针，用于内存释放
 * @param buff 要释放的缓冲区指针
 * 
 * @note 这是luaZ_resizebuffer(L, buff, 0)的便利宏
 */
#define luaZ_freebuffer(L, buff)    luaZ_resizebuffer(L, buff, 0)

/**
 * @brief 分配缓冲区空间：确保缓冲区有足够的可用空间
 * 
 * 详细说明：
 * 这个函数确保缓冲区至少有n个字节的可用空间。如果当前
 * 空间不足，函数会自动扩展缓冲区。返回的指针指向可写入
 * 数据的位置。
 * 
 * 分配策略：
 * - 检查当前可用空间是否足够
 * - 如果不够，计算新的合适大小
 * - 重新分配内存并保持现有数据
 * - 返回可写入位置的指针
 * 
 * 增长策略：
 * - 通常使用指数增长（如2倍扩展）
 * - 考虑请求的大小和当前使用情况
 * - 平衡内存使用和重分配频率
 * 
 * 返回值：
 * - 成功：指向可写入空间的char指针
 * - 失败：通过Lua错误机制报告内存不足
 * 
 * 使用模式：
 * @code
 * char *space = luaZ_openspace(L, &buff, 100);
 * strcpy(space, "新内容");
 * buff.n += strlen("新内容");
 * @endcode
 * 
 * @param L Lua状态机指针，用于内存分配和错误处理
 * @param buff 缓冲区指针
 * @param n 需要的空间大小（字节数）
 * @return 指向可用空间的char指针
 * 
 * @note 调用者负责更新buff->n字段
 * @warning 内存分配失败会抛出Lua错误
 * @see luaZ_resizebuffer()
 */
LUAI_FUNC char *luaZ_openspace(lua_State *L, Mbuffer *buff, size_t n);

/**
 * @brief 初始化ZIO流：设置流的Reader函数和数据源
 * 
 * 详细说明：
 * 这个函数初始化一个ZIO流对象，设置其Reader函数和相关数据。
 * Reader函数负责从实际的数据源读取数据到内部缓冲区，而ZIO
 * 则提供统一的字符流接口。
 * 
 * 初始化过程：
 * 1. 设置Reader函数指针
 * 2. 设置Reader的用户数据
 * 3. 设置Lua状态机引用
 * 4. 清空内部缓冲区状态
 * 5. 准备第一次读取操作
 * 
 * Reader函数规范：
 * - 类型：const char* (*lua_Reader)(lua_State*, void*, size_t*)
 * - 参数：Lua状态机、用户数据、返回的数据大小
 * - 返回：数据指针（NULL表示EOF）
 * - 职责：从数据源读取数据并返回指针和大小
 * 
 * 数据流向：
 * 数据源 -> Reader函数 -> ZIO内部缓冲 -> zgetc()等接口
 * 
 * 支持的数据源：
 * - 文件：通过文件I/O函数读取
 * - 字符串：从内存中的字符串读取
 * - 网络：从套接字等网络源读取
 * - 自定义：任何实现Reader接口的数据源
 * 
 * @param L Lua状态机指针
 * @param z 要初始化的ZIO流指针
 * @param reader Reader函数指针
 * @param data 传递给Reader函数的用户数据
 * 
 * @note 初始化后ZIO流准备好进行读取操作
 * @see lua_Reader, zgetc()
 */
LUAI_FUNC void luaZ_init(lua_State *L, ZIO *z, lua_Reader reader, void *data);

/**
 * @brief 批量读取：从ZIO流中读取指定数量的字节
 * 
 * 详细说明：
 * 这个函数从ZIO流中读取最多n个字节的数据到指定的缓冲区。
 * 与zgetc()的逐字符读取不同，这个函数支持批量读取，提高
 * 大量数据读取的效率。
 * 
 * 读取策略：
 * 1. 首先从内部缓冲区复制已有数据
 * 2. 如果内部缓冲区数据不足，调用Reader函数
 * 3. 直接复制Reader返回的数据，避免额外拷贝
 * 4. 重复直到读取足够数据或遇到EOF
 * 
 * 返回值含义：
 * - 等于n：成功读取了请求的字节数
 * - 小于n：遇到了流结束或错误
 * - 0：立即遇到EOF，没有读取任何数据
 * 
 * 性能优化：
 * - 尽量直接从Reader复制数据，减少中间缓冲
 * - 批量操作减少函数调用开销
 * - 智能缓冲管理避免不必要的内存操作
 * 
 * 使用场景：
 * - 字节码加载：读取二进制数据块
 * - 大文件处理：批量读取文本内容
 * - 网络传输：接收数据包
 * - 压缩数据：读取压缩流
 * 
 * @param z ZIO流指针
 * @param b 目标缓冲区指针
 * @param n 要读取的字节数
 * @return 实际读取的字节数（可能小于n）
 * 
 * @note 返回值小于n时通常表示遇到了EOF
 * @see zgetc(), lua_Reader
 */
LUAI_FUNC size_t luaZ_read(ZIO* z, void* b, size_t n);

/**
 * @brief 预读字符：查看下一个字符但不消费它
 * 
 * 详细说明：
 * 这个函数返回流中的下一个字符，但不推进读取位置。
 * 这对于需要向前看的词法分析和语法解析非常有用。
 * 连续调用此函数会返回相同的字符。
 * 
 * 实现原理：
 * 1. 如果内部缓冲区有数据，直接返回第一个字符
 * 2. 如果缓冲区为空，调用luaZ_fill()填充
 * 3. 不推进读取位置，保持流状态不变
 * 
 * 使用场景：
 * - 词法分析：判断数字、标识符的结束
 * - 语法解析：实现LL(1)等预测性解析
 * - 格式检测：检查文件头或数据格式
 * - 条件读取：根据下一个字符决定读取策略
 * 
 * 性能考虑：
 * - 频繁调用开销较小（通常是简单的内存访问）
 * - 只在缓冲区为空时才调用填充函数
 * - 与zgetc()配合使用效率最高
 * 
 * 返回值：
 * - 成功：下一个字符的整数值（0-255）
 * - 失败：EOZ表示流结束或错误
 * 
 * @param z ZIO流指针
 * @return 下一个字符的整数值，或EOZ表示EOF
 * 
 * @note 此函数不改变流的读取位置
 * @see zgetc(), luaZ_fill()
 */
LUAI_FUNC int luaZ_lookahead(ZIO *z);

/* --------- 私有部分 ------------------ */

/**
 * @brief ZIO流结构：缓冲I/O流的内部实现
 * 
 * 详细说明：
 * 这个结构体定义了ZIO流的内部状态和数据。它包含了缓冲区
 * 管理、Reader函数、用户数据等所有必要的信息。这个结构
 * 的设计支持高效的字符流操作和灵活的数据源适配。
 * 
 * 设计原理：
 * - 内部缓冲：减少Reader函数调用次数
 * - 位置跟踪：维护当前读取位置和剩余数据
 * - 回调机制：通过Reader函数支持各种数据源
 * - 状态封装：将所有相关状态集中管理
 * 
 * 缓冲区管理：
 * - p指针指向当前读取位置
 * - n记录剩余未读字节数
 * - 当n为0时需要重新填充缓冲区
 * 
 * 生命周期：
 * 1. 通过luaZ_init()初始化
 * 2. 通过zgetc()等函数读取数据
 * 3. 内部自动调用Reader函数补充数据
 * 4. 流结束时自动清理（无需显式释放）
 */
struct Zio {
    /**
     * @brief 剩余字节数：内部缓冲区中尚未读取的字节数
     * 
     * 这个字段记录了从p指针开始，还有多少字节可以读取。
     * 当这个值为0时，表示需要调用Reader函数重新填充缓冲区。
     */
    size_t n;

    /**
     * @brief 当前位置：指向内部缓冲区中下一个要读取的字符
     * 
     * 这个指针指向当前读取位置，每次调用zgetc()都会推进
     * 这个指针并递减n字段。
     */
    const char *p;

    /**
     * @brief Reader函数：负责从数据源读取数据的回调函数
     * 
     * 当内部缓冲区耗尽时，ZIO系统会调用这个函数来获取
     * 更多数据。函数应该返回数据指针和大小。
     */
    lua_Reader reader;

    /**
     * @brief 用户数据：传递给Reader函数的附加数据
     * 
     * 这个指针由用户提供，会原样传递给Reader函数。
     * 通常用于传递文件句柄、字符串上下文等信息。
     */
    void* data;

    /**
     * @brief Lua状态机：用于错误处理和内存管理
     * 
     * Reader函数可能需要Lua状态机来报告错误或分配内存。
     * 这个字段确保Reader函数能够访问必要的Lua上下文。
     */
    lua_State *L;
};

/**
 * @brief 填充缓冲区：当内部缓冲区为空时重新填充数据
 * 
 * 详细说明：
 * 这是ZIO系统的核心函数，负责在内部缓冲区耗尽时调用
 * Reader函数获取更多数据。这个函数通常由zgetc()宏
 * 在缓冲区为空时自动调用。
 * 
 * 填充过程：
 * 1. 调用Reader函数获取新数据
 * 2. 更新内部缓冲区指针和计数
 * 3. 返回第一个可用字符
 * 4. 如果没有更多数据，返回EOZ
 * 
 * 错误处理：
 * - Reader函数返回NULL表示EOF
 * - Reader函数可能通过Lua错误机制报告错误
 * - 内存分配失败等异常情况的处理
 * 
 * 性能优化：
 * - 尽量减少Reader函数调用次数
 * - 智能缓冲区大小管理
 * - 避免不必要的数据拷贝
 * 
 * @param z ZIO流指针
 * @return 下一个可用字符，或EOZ表示流结束
 * 
 * @note 这是内部函数，通常不直接调用
 * @warning Reader函数可能抛出Lua错误
 * @see zgetc(), lua_Reader
 */
LUAI_FUNC int luaZ_fill(ZIO *z);

#endif
