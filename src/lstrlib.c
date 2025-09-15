/**
 * @file lstrlib.c
 * @brief Lua字符串库：字符串操作和模式匹配的标准库实现
 * 
 * 详细说明：
 * 这个文件实现了Lua编程语言的完整字符串处理功能，包括基本的字符串操作函数
 * （如长度获取、子串提取、大小写转换等）和强大的模式匹配系统。模式匹配系统
 * 是Lua的核心特色之一，提供了类似正则表达式但更轻量的文本处理能力。
 * 
 * 系统架构定位：
 * 作为Lua虚拟机的核心库之一，本模块位于Lua解释器的标准库层，为上层Lua脚本
 * 提供高效的字符串处理接口。所有函数都遵循Lua的C API规范，与虚拟机栈系统
 * 紧密集成，确保内存管理的安全性和性能的最优化。
 * 
 * 技术特点：
 * - 使用Lua缓冲区系统（luaL_Buffer）优化字符串构建性能
 * - 实现了自定义的模式匹配引擎，支持捕获组和高级匹配特性
 * - 采用尾递归优化技术（goto替代递归）提升模式匹配性能
 * - 严格的边界检查和错误处理，防止缓冲区溢出和内存泄漏
 * 
 * 依赖关系：
 * - 标准C库：ctype.h（字符分类）、string.h（字符串操作）、stdio.h（格式化）
 * - Lua核心API：lua.h（虚拟机接口）、lauxlib.h（辅助函数）、lualib.h（库定义）
 * - 无平台特定依赖，保证跨平台兼容性
 * 
 * 编译要求：
 * - C标准版本：C89/C90兼容，支持更高版本
 * - 编译器要求：支持标准C库和函数指针
 * - 链接要求：需要链接Lua核心库和标准C运行时库
 * 
 * 使用示例：
 * @code
 * // 在Lua脚本中使用字符串库
 * local str = "Hello, World!"
 * local len = string.len(str)              -- 获取字符串长度
 * local sub = string.sub(str, 1, 5)        -- 提取子串："Hello"
 * local upper = string.upper(str)          -- 转换为大写
 * 
 * // 模式匹配示例
 * local text = "Email: user@domain.com"
 * local email = string.match(text, "(%w+@%w+%.%w+)")  -- 提取邮箱地址
 * local result = string.gsub(text, "user", "admin")   -- 替换文本
 * @endcode
 * 
 * 内存安全考虑：
 * 所有字符串操作都通过Lua的垃圾回收器管理内存，自动处理内存分配和释放。
 * 使用luaL_Buffer系统进行字符串构建，避免频繁的内存重分配，提升性能同时
 * 保证内存安全。模式匹配过程中的捕获组管理采用固定大小数组，防止栈溢出。
 * 
 * 性能特征：
 * - 字符串基本操作：O(1)到O(n)复杂度，取决于操作类型
 * - 模式匹配：平均O(nm)复杂度，其中n为文本长度，m为模式长度
 * - 内存效率：使用缓冲区复用减少内存分配开销
 * 
 * 线程安全性：
 * 函数本身是可重入的，但依赖于传入的lua_State的线程安全性。在多线程
 * 环境中，每个线程应使用独立的lua_State实例。
 * 
 * 注意事项：
 * - 模式匹配系统使用'%'作为转义字符，与标准正则表达式不同
 * - 所有字符串索引从1开始，符合Lua的约定而非C的约定
 * - 大字符串操作时需要注意栈空间限制
 * 
 * @author Roberto Ierusalimschy and Lua Team
 * @version 5.1.5
 * @date 2010/05/14
 * @since Lua 5.0
 * @see lua.h, lauxlib.h
 */

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lstrlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/**
 * @brief 字符无符号转换宏：将有符号字符安全转换为无符号字符
 * 
 * 详细说明：
 * 这个宏用于解决C语言中char类型的符号问题。在某些平台上，char默认是有符号的，
 * 当处理非ASCII字符（值大于127）时，会产生负数，导致字符分类函数（如isalpha等）
 * 行为未定义。通过强制转换为unsigned char，确保字符值始终为非负数。
 * 
 * 使用场景：
 * - 调用标准库字符分类函数（isalpha、isdigit等）之前
 * - 进行字符比较和数组索引操作时
 * - 处理非ASCII字符和国际化文本时
 * 
 * 安全性考虑：
 * 避免了有符号字符溢出和未定义行为，确保字符处理的可移植性和正确性。
 * 
 * @param c 需要转换的字符，可以是char或int类型
 * @return 转换后的无符号字符值，范围0-255
 */
#define uchar(c)        ((unsigned char)(c))



/**
 * @brief 获取字符串长度：返回字符串的字节长度
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.len函数，用于获取字符串的字节长度。它使用Lua的
 * 辅助函数luaL_checklstring来获取字符串并同时获得其长度，然后将长度作为
 * 整数压入Lua栈。这是最基本也是最常用的字符串操作函数之一。
 * 
 * 算法描述：
 * 1. 使用luaL_checklstring检查第一个参数是否为字符串并获取长度
 * 2. 将长度值作为Lua整数压入栈顶
 * 3. 返回1表示压入了一个返回值
 * 
 * 算法复杂度：
 * - 时间复杂度：O(1)，长度已在字符串对象中预先计算并存储
 * - 空间复杂度：O(1)，只使用常量额外空间
 * 
 * 线程安全性：
 * 函数是可重入的，不修改全局状态，依赖传入的lua_State的线程安全性。
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local str = "Hello, 世界!"
 * local len = string.len(str)  -- 返回字节长度，注意UTF-8字符占用多个字节
 * print(len)  -- 输出实际字节数
 * @endcode
 * 
 * 注意事项：
 * - 返回的是字节长度，不是字符数，对于UTF-8等多字节编码需要特别注意
 * - 函数会自动检查参数类型，如果不是字符串会抛出Lua错误
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1：要获取长度的字符串
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 成功执行，栈顶为字符串长度（整数类型）
 * 
 * @pre L != NULL && lua_isstring(L, 1)
 * @post 栈顶包含一个整数，表示字符串的字节长度
 * 
 * @note 函数内部会进行类型检查，非字符串参数会导致Lua错误
 * @since Lua 5.0
 * @see luaL_checklstring(), lua_pushinteger()
 */
static int str_len(lua_State *L) {
    size_t l;
    luaL_checklstring(L, 1, &l);    // 检查参数并获取字符串长度
    lua_pushinteger(L, l);          // 将长度压入栈作为返回值
    return 1;
}


/**
 * @brief 位置相对化转换：将相对位置转换为绝对位置
 * 
 * 详细说明：
 * 这是一个内部辅助函数，用于处理Lua字符串操作中的位置参数。Lua允许使用负数
 * 表示从字符串末尾开始的相对位置，这个函数负责将这种相对位置转换为标准的
 * 从字符串开头开始的绝对位置。这是Lua字符串库中位置处理的核心算法。
 * 
 * 算法描述：
 * - 如果位置为负数：pos = pos + len + 1（从末尾倒数）
 * - 如果位置为非负数：保持原值
 * - 如果转换后位置小于0：返回0（防止越界）
 * 
 * 算法复杂度：
 * - 时间复杂度：O(1)，只进行简单的数学运算
 * - 空间复杂度：O(1)，不使用额外存储空间
 * 
 * 位置计算规则：
 * - 正数位置：1表示第一个字符，2表示第二个字符...
 * - 负数位置：-1表示最后一个字符，-2表示倒数第二个字符...
 * - 边界处理：超出范围的位置会被调整到有效范围内
 * 
 * 使用示例：
 * @code
 * // 对于长度为5的字符串"hello"
 * posrelat(1, 5)   // 返回1（第一个字符'h'）
 * posrelat(-1, 5)  // 返回5（最后一个字符'o'）
 * posrelat(-3, 5)  // 返回3（倒数第三个字符'l'）
 * posrelat(0, 5)   // 返回0（无效位置，被规范化）
 * posrelat(-10, 5) // 返回0（超出范围，被规范化）
 * @endcode
 * 
 * 设计理念：
 * 这种设计让Lua的字符串操作更加直观和灵活，用户可以方便地从字符串末尾
 * 开始计算位置，而不需要事先知道字符串的确切长度。
 * 
 * @param[in] pos 相对位置值，可以是正数（从开头计算）或负数（从末尾计算）
 * @param[in] len 字符串的长度，用于负数位置的计算基准
 * 
 * @return 转换后的绝对位置，保证为非负数
 * @retval >=0 有效的绝对位置，可直接用于字符串索引
 * @retval 0   无效位置或超出范围的位置
 * 
 * @pre len >= 0（字符串长度必须为非负数）
 * @post 返回值 >= 0（绝对位置始终为非负数）
 * 
 * @note 返回的位置遵循Lua约定（从1开始），而非C约定（从0开始）
 * @note 函数保证不会返回负数，超出范围的位置会被规范化为0
 * 
 * @since Lua 5.0
 * @see str_sub(), str_byte()
 */
static ptrdiff_t posrelat(ptrdiff_t pos, size_t len) {
    // 负数位置表示从字符串末尾开始倒数
    if (pos < 0) {
        pos += (ptrdiff_t)len + 1;
    }
    
    // 确保位置不小于0，防止数组越界
    return (pos >= 0) ? pos : 0;
}


/**
 * @brief 字符串子串提取：从字符串中提取指定范围的子串
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.sub函数，用于从字符串中提取子串。它支持正数和负数
 * 索引，允许灵活地指定提取范围。函数会自动处理边界情况，确保提取操作的安全性。
 * 这是字符串操作中最常用的函数之一，广泛用于文本分析和处理。
 * 
 * 算法描述：
 * 1. 获取源字符串及其长度
 * 2. 将相对位置转换为绝对位置（支持负数索引）
 * 3. 调整起始和结束位置到有效范围内
 * 4. 如果范围有效则提取子串，否则返回空字符串
 * 
 * 算法复杂度：
 * - 时间复杂度：O(m)，其中m为提取的子串长度
 * - 空间复杂度：O(m)，需要为子串分配新的内存空间
 * 
 * 位置处理逻辑：
 * - 起始位置小于1：自动调整为1（字符串开头）
 * - 结束位置大于字符串长度：自动调整为字符串长度
 * - 起始位置大于结束位置：返回空字符串
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local str = "Hello, World!"
 * 
 * -- 基本用法
 * local sub1 = string.sub(str, 1, 5)    -- "Hello"
 * local sub2 = string.sub(str, 8)       -- "World!"（从第8位到末尾）
 * 
 * -- 负数索引
 * local sub3 = string.sub(str, -6, -2)  -- "World"（倒数第6到第2位）
 * local sub4 = string.sub(str, -1)      -- "!"（最后一个字符）
 * 
 * -- 边界情况
 * local sub5 = string.sub(str, 10, 5)   -- ""（起始位置大于结束位置）
 * local sub6 = string.sub(str, 0, 3)    -- "Hel"（起始位置自动调整为1）
 * @endcode
 * 
 * 内存管理：
 * 函数使用lua_pushlstring创建新的字符串对象，内存由Lua垃圾回收器自动管理。
 * 不会修改原始字符串，保证了操作的安全性。
 * 
 * 性能考虑：
 * 对于大字符串的小范围提取，性能优越。但频繁的子串操作可能导致内存碎片，
 * 建议在性能敏感场景中谨慎使用。
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1：源字符串
 *              栈位置2：起始位置（支持负数，表示从末尾开始）
 *              栈位置3：结束位置（可选，默认为-1即字符串末尾）
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 成功执行，栈顶为提取的子串
 * 
 * @pre L != NULL && lua_isstring(L, 1) && lua_isnumber(L, 2)
 * @post 栈顶包含提取的子串，可能为空字符串
 * 
 * @note 位置索引从1开始，符合Lua约定
 * @note 负数索引表示从字符串末尾开始计算
 * @note 无效范围会返回空字符串，不会抛出错误
 * 
 * @since Lua 5.0
 * @see posrelat(), lua_pushlstring()
 */
static int str_sub(lua_State *L) {
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);                    // 获取源字符串和长度
    ptrdiff_t start = posrelat(luaL_checkinteger(L, 2), l);         // 转换起始位置
    ptrdiff_t end = posrelat(luaL_optinteger(L, 3, -1), l);         // 转换结束位置（默认-1）
    
    // 边界检查和调整：确保位置在有效范围内
    if (start < 1) {
        start = 1;                          // 起始位置不能小于1
    }
    if (end > (ptrdiff_t)l) {
        end = (ptrdiff_t)l;                 // 结束位置不能超过字符串长度
    }
    
    // 提取子串或返回空字符串
    if (start <= end) {
        // 有效范围：提取从start-1到end-1的子串（转换为C语言的0索引）
        lua_pushlstring(L, s + start - 1, end - start + 1);
    } else {
        // 无效范围：返回空字符串
        lua_pushliteral(L, "");
    }
    
    return 1;
}


/**
 * @brief 字符串反转：将字符串中的字符顺序完全颠倒
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.reverse函数，通过逐个字符读取并反向添加到缓冲区
 * 的方式实现字符串反转。使用Lua的缓冲区系统（luaL_Buffer）来高效地构建结果
 * 字符串，避免频繁的内存分配操作。
 * 
 * 算法描述：
 * 1. 初始化Lua缓冲区系统
 * 2. 从字符串末尾开始，逐个字符向前遍历
 * 3. 将每个字符添加到缓冲区中
 * 4. 将构建完成的缓冲区内容作为结果返回
 * 
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为字符串长度，需要遍历每个字符
 * - 空间复杂度：O(n)，需要为反转后的字符串分配新的内存空间
 * 
 * 缓冲区优化：
 * 使用luaL_Buffer系统可以避免字符串拼接时的重复内存分配，显著提升性能。
 * 缓冲区会根据需要自动扩展，确保操作的高效性。
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local str = "Hello"
 * local reversed = string.reverse(str)  -- "olleH"
 * 
 * local text = "12345"
 * local backwards = string.reverse(text)  -- "54321"
 * 
 * -- 处理空字符串
 * local empty = ""
 * local still_empty = string.reverse(empty)  -- ""
 * @endcode
 * 
 * 字符编码注意事项：
 * 函数按字节进行反转，对于单字节编码（如ASCII、Latin-1）工作正常。
 * 对于多字节编码（如UTF-8），反转后可能产生无效的字符序列，需要谨慎使用。
 * 
 * 性能特征：
 * - 对于短字符串：性能优越，开销主要在函数调用
 * - 对于长字符串：线性时间复杂度，内存使用效率高
 * - 缓冲区机制减少了内存分配次数，提升整体性能
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1：要反转的字符串
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 成功执行，栈顶为反转后的字符串
 * 
 * @pre L != NULL && lua_isstring(L, 1)
 * @post 栈顶包含反转后的字符串
 * 
 * @note 按字节进行反转，多字节字符编码需要特别注意
 * @note 空字符串反转后仍为空字符串
 * @note 使用缓冲区系统优化内存分配性能
 * 
 * @since Lua 5.0
 * @see luaL_Buffer, luaL_addchar(), luaL_pushresult()
 */
static int str_reverse(lua_State *L) {
    size_t l;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);    // 获取字符串及其长度
    
    luaL_buffinit(L, &b);                           // 初始化字符串构建缓冲区
    
    // 从字符串末尾开始，逐个字符向前遍历并添加到缓冲区
    while (l--) {
        luaL_addchar(&b, s[l]);                     // 添加当前字符到缓冲区
    }
    
    luaL_pushresult(&b);                            // 将缓冲区内容作为字符串推入栈
    return 1;
}


/**
 * @brief 字符串转小写：将字符串中的所有大写字母转换为小写字母
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.lower函数，逐个检查字符串中的每个字符，
 * 使用标准C库的tolower函数将大写字母转换为小写字母，其他字符保持不变。
 * 使用缓冲区系统来高效地构建转换后的字符串。
 * 
 * 算法描述：
 * 1. 获取输入字符串并初始化缓冲区
 * 2. 遍历字符串中的每个字符
 * 3. 对每个字符应用tolower转换（通过uchar宏确保安全）
 * 4. 将转换后的字符添加到缓冲区
 * 5. 返回构建完成的结果字符串
 * 
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为字符串长度
 * - 空间复杂度：O(n)，需要为结果字符串分配内存
 * 
 * 字符处理安全性：
 * 使用uchar宏将字符转换为unsigned char，避免有符号字符在某些平台上
 * 产生负值，导致tolower函数行为未定义的问题。这确保了跨平台兼容性。
 * 
 * 本地化考虑：
 * tolower函数的行为依赖于当前的locale设置，对于非ASCII字符的处理
 * 可能因系统而异。在国际化应用中需要考虑Unicode和UTF-8的正确处理。
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local str = "Hello, WORLD!"
 * local lower = string.lower(str)     -- "hello, world!"
 * 
 * local mixed = "ABC123xyz"
 * local result = string.lower(mixed)  -- "abc123xyz"
 * 
 * -- 处理特殊字符
 * local special = "Hello@World#2024"
 * local converted = string.lower(special)  -- "hello@world#2024"
 * @endcode
 * 
 * 性能特征：
 * - 单次遍历，性能高效
 * - 缓冲区机制减少内存分配开销
 * - 对于ASCII字符处理速度最快
 * 
 * 注意事项：
 * - 只转换ASCII大写字母，对于扩展字符集的支持依赖于系统locale
 * - 多字节字符（如UTF-8）可能无法正确处理，需要专门的Unicode库
 * - 转换是基于字节的，不是基于字符的
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1：要转换为小写的字符串
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 成功执行，栈顶为转换后的小写字符串
 * 
 * @pre L != NULL && lua_isstring(L, 1)
 * @post 栈顶包含转换为小写的字符串
 * 
 * @note 转换基于当前系统的locale设置
 * @note 非字母字符保持不变
 * @note 使用uchar宏确保字符处理的安全性
 * 
 * @since Lua 5.0
 * @see str_upper(), tolower(), uchar()
 */
static int str_lower(lua_State *L) {
    size_t l;
    size_t i;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);    // 获取字符串及其长度
    
    luaL_buffinit(L, &b);                           // 初始化字符串构建缓冲区
    
    // 遍历字符串中的每个字符并转换为小写
    for (i = 0; i < l; i++) {
        // 使用uchar宏确保字符为无符号，然后应用tolower转换
        luaL_addchar(&b, tolower(uchar(s[i])));
    }
    
    luaL_pushresult(&b);                            // 将缓冲区内容作为字符串推入栈
    return 1;
}

/**
 * @brief 字符串转大写：将字符串中的所有小写字母转换为大写字母
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.upper函数，与str_lower函数对应，用于将字符串
 * 中的小写字母转换为大写字母。实现机制完全相同，只是使用toupper函数替代
 * tolower函数。同样使用缓冲区系统来优化性能。
 * 
 * 算法描述：
 * 1. 获取输入字符串并初始化缓冲区
 * 2. 遍历字符串中的每个字符
 * 3. 对每个字符应用toupper转换（通过uchar宏确保安全）
 * 4. 将转换后的字符添加到缓冲区
 * 5. 返回构建完成的结果字符串
 * 
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为字符串长度
 * - 空间复杂度：O(n)，需要为结果字符串分配内存
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local str = "hello, world!"
 * local upper = string.upper(str)     -- "HELLO, WORLD!"
 * 
 * local mixed = "abc123XYZ"
 * local result = string.upper(mixed)  -- "ABC123XYZ"
 * 
 * -- 处理特殊字符
 * local special = "hello@world#2024"
 * local converted = string.upper(special)  -- "HELLO@WORLD#2024"
 * @endcode
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1：要转换为大写的字符串
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 成功执行，栈顶为转换后的大写字符串
 * 
 * @pre L != NULL && lua_isstring(L, 1)
 * @post 栈顶包含转换为大写的字符串
 * 
 * @note 转换基于当前系统的locale设置
 * @note 非字母字符保持不变
 * @note 使用uchar宏确保字符处理的安全性
 * 
 * @since Lua 5.0
 * @see str_lower(), toupper(), uchar()
 */
static int str_upper(lua_State *L) {
    size_t l;
    size_t i;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);    // 获取字符串及其长度
    
    luaL_buffinit(L, &b);                           // 初始化字符串构建缓冲区
    
    // 遍历字符串中的每个字符并转换为大写
    for (i = 0; i < l; i++) {
        // 使用uchar宏确保字符为无符号，然后应用toupper转换
        luaL_addchar(&b, toupper(uchar(s[i])));
    }
    
    luaL_pushresult(&b);                            // 将缓冲区内容作为字符串推入栈
    return 1;
}

/**
 * @brief 字符串重复：将指定字符串重复指定次数并连接
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.rep函数，用于创建由原字符串重复多次组成的新字符串。
 * 它通过循环将原字符串多次添加到缓冲区中来实现，是一个非常实用的字符串生成工具。
 * 常用于创建分隔符、填充字符或生成重复模式的文本。
 * 
 * 算法描述：
 * 1. 获取源字符串和重复次数
 * 2. 初始化字符串构建缓冲区
 * 3. 循环指定次数，每次将完整的源字符串添加到缓冲区
 * 4. 返回构建完成的结果字符串
 * 
 * 算法复杂度：
 * - 时间复杂度：O(n*m)，其中n为重复次数，m为源字符串长度
 * - 空间复杂度：O(n*m)，结果字符串的大小
 * 
 * 边界处理：
 * - 重复次数为0：返回空字符串
 * - 重复次数为负数：由于循环条件，实际上也会返回空字符串
 * - 空字符串重复：返回空字符串
 * 
 * 内存考虑：
 * 对于大字符串或大重复次数，可能消耗大量内存。缓冲区系统会自动管理内存分配，
 * 但仍需注意潜在的内存不足问题。
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local dash = string.rep("-", 20)         -- "--------------------"
 * local hello = string.rep("Hello", 3)     -- "HelloHelloHello"
 * local empty = string.rep("test", 0)      -- ""
 * 
 * -- 创建分隔线
 * local separator = string.rep("=", 50)
 * 
 * -- 创建缩进
 * local indent = string.rep("  ", 4)       -- 8个空格的缩进
 * @endcode
 * 
 * 性能特征：
 * - 对于小字符串和少量重复：性能优越
 * - 对于大量重复：内存使用呈线性增长
 * - 缓冲区机制优化了字符串拼接性能
 * 
 * 实际应用：
 * - 生成表格分隔线
 * - 创建固定长度的填充字符
 * - 生成重复模式的测试数据
 * - 创建缩进和对齐空格
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1：要重复的源字符串
 *              栈位置2：重复次数（整数）
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 成功执行，栈顶为重复后的字符串
 * 
 * @pre L != NULL && lua_isstring(L, 1) && lua_isnumber(L, 2)
 * @post 栈顶包含重复后的字符串
 * 
 * @note 重复次数为0或负数时返回空字符串
 * @note 大量重复时需要注意内存使用量
 * @note 使用缓冲区系统优化性能
 * 
 * @since Lua 5.0
 * @see luaL_addlstring(), luaL_Buffer
 */
static int str_rep(lua_State *L) {
    size_t l;
    luaL_Buffer b;
    const char *s = luaL_checklstring(L, 1, &l);    // 获取源字符串和长度
    int n = luaL_checkint(L, 2);                    // 获取重复次数
    
    luaL_buffinit(L, &b);                           // 初始化字符串构建缓冲区
    
    // 循环指定次数，每次添加完整的源字符串
    while (n-- > 0) {
        luaL_addlstring(&b, s, l);                  // 将源字符串添加到缓冲区
    }
    
    luaL_pushresult(&b);                            // 将缓冲区内容作为字符串推入栈
    return 1;
}


/**
 * @brief 获取字符字节值：返回字符串中指定位置字符的字节值
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.byte函数，用于获取字符串中一个或多个字符的字节值。
 * 它支持指定起始位置和结束位置，可以一次性获取多个字符的字节值。这是底层字符
 * 处理和二进制数据操作的重要工具。
 * 
 * 算法描述：
 * 1. 获取字符串和位置参数（支持负数索引）
 * 2. 转换相对位置为绝对位置并进行边界检查
 * 3. 验证位置范围的有效性和栈空间充足性
 * 4. 循环获取指定范围内每个字符的字节值并推入栈
 * 5. 返回获取的字节值数量
 * 
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为要获取的字符数量
 * - 空间复杂度：O(n)，需要为每个字节值在栈上分配空间
 * 
 * 参数处理：
 * - 第一个参数：源字符串
 * - 第二个参数：起始位置（可选，默认为1）
 * - 第三个参数：结束位置（可选，默认等于起始位置）
 * 
 * 安全检查：
 * - 位置边界检查：防止越界访问
 * - 栈溢出检查：防止请求过多字符导致栈溢出
 * - 整数溢出检查：防止计算字符数量时发生溢出
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local str = "Hello"
 * 
 * -- 获取单个字符的字节值
 * local byte1 = string.byte(str, 1)      -- 72 (ASCII 'H')
 * local byte5 = string.byte(str, 5)      -- 111 (ASCII 'o')
 * 
 * -- 获取多个字符的字节值
 * local b1, b2, b3 = string.byte(str, 1, 3)  -- 72, 101, 108 ('H', 'e', 'l')
 * 
 * -- 使用负数索引
 * local last = string.byte(str, -1)      -- 111 (最后一个字符 'o')
 * 
 * -- 获取所有字符的字节值
 * local bytes = {string.byte(str, 1, -1)}
 * @endcode
 * 
 * 返回值特征：
 * - 有效范围：返回对应数量的字节值
 * - 无效范围：返回0个值（空间隔）
 * - 越界访问：自动调整到有效范围
 * 
 * 性能考虑：
 * - 单字符获取：O(1)时间复杂度
 * - 多字符获取：线性时间复杂度
 * - 栈操作开销随字符数量增加
 * 
 * 实际应用：
 * - 字符编码转换和检查
 * - 二进制数据处理
 * - 协议解析和数据验证
 * - 加密和哈希算法实现
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1：源字符串
 *              栈位置2：起始位置（可选，默认为1）
 *              栈位置3：结束位置（可选，默认等于起始位置）
 * 
 * @return 返回的字节值数量，范围从0到请求的字符数
 * @retval 0   无效范围或空间隔
 * @retval >0  成功获取的字节值数量，每个值作为独立的返回值
 * 
 * @pre L != NULL && lua_isstring(L, 1)
 * @post 栈上包含请求范围内每个字符的字节值（0-255）
 * 
 * @note 字节值范围为0-255（unsigned char范围）
 * @note 支持负数索引，表示从字符串末尾开始计算
 * @note 会进行栈空间检查，防止栈溢出
 * @note 无效范围返回0个值，不会抛出错误
 * 
 * @warning 请求过多字符可能导致栈溢出错误
 * @warning 整数溢出检查确保计算安全性
 * 
 * @since Lua 5.0
 * @see str_char(), posrelat(), uchar()
 */
static int str_byte(lua_State *L) {
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);                // 获取字符串和长度
    ptrdiff_t posi = posrelat(luaL_optinteger(L, 2, 1), l);     // 起始位置（默认1）
    ptrdiff_t pose = posrelat(luaL_optinteger(L, 3, posi), l);  // 结束位置（默认等于起始位置）
    int n, i;
    
    // 边界检查和调整
    if (posi <= 0) {
        posi = 1;                               // 起始位置不能小于1
    }
    if ((size_t)pose > l) {
        pose = l;                               // 结束位置不能超过字符串长度
    }
    if (posi > pose) {
        return 0;                               // 空间隔：返回0个值
    }
    
    // 计算要返回的字符数量并检查是否会发生溢出
    n = (int)(pose - posi + 1);
    if (posi + n <= pose) {                     // 检查整数溢出
        luaL_error(L, "string slice too long");
    }
    
    // 检查栈空间是否充足
    luaL_checkstack(L, n, "string slice too long");
    
    // 循环获取指定范围内每个字符的字节值
    for (i = 0; i < n; i++) {
        // 将字节值作为整数推入栈（使用uchar确保非负值）
        lua_pushinteger(L, uchar(s[posi + i - 1]));
    }
    
    return n;                                   // 返回推入栈的值的数量
}


/**
 * @brief 字节值转字符串：将一系列字节值转换为字符串
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.char函数，是string.byte的逆向操作。它接受任意数量
 * 的数字参数，将每个数字当作字节值（0-255）转换为对应的字符，然后组合成字符串。
 * 这是构建二进制数据和进行低级字符串操作的重要工具。
 * 
 * 算法描述：
 * 1. 获取参数数量（即要转换的字节值数量）
 * 2. 初始化字符串构建缓冲区
 * 3. 循环处理每个参数：验证范围并转换为字符
 * 4. 将所有字符组合成最终的字符串
 * 5. 返回构建完成的字符串
 * 
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为参数数量
 * - 空间复杂度：O(n)，结果字符串的大小
 * 
 * 参数验证：
 * 每个参数必须是有效的字节值（0-255范围内的整数）。超出范围的值会导致
 * 参数错误，确保生成的字符串符合字节序列的要求。
 * 
 * 字符编码考虑：
 * 函数生成的是字节序列，对于ASCII字符工作正常。对于多字节编码（如UTF-8），
 * 调用者需要确保字节序列的正确性，否则可能产生无效的字符串。
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * local str1 = string.char(72, 101, 108, 108, 111)  -- "Hello"
 * local str2 = string.char(65, 66, 67)              -- "ABC"
 * 
 * -- 创建特殊字符
 * local newline = string.char(10)                    -- "\n"
 * local tab = string.char(9)                         -- "\t"
 * 
 * -- 结合使用
 * local bytes = {string.byte("Hello", 1, -1)}
 * local reconstructed = string.char(unpack(bytes))   -- "Hello"
 * 
 * -- 创建二进制数据
 * local binary = string.char(0x00, 0xFF, 0x80)      -- 包含null、0xFF和0x80字节
 * @endcode
 * 
 * 错误处理：
 * - 无参数：返回空字符串
 * - 无效字节值：抛出参数错误
 * - 内存不足：由缓冲区系统处理
 * 
 * 性能特征：
 * - 少量字符：性能优越
 * - 大量字符：线性时间复杂度
 * - 缓冲区机制优化内存分配
 * 
 * 实际应用：
 * - 二进制协议构建
 * - 字符编码转换
 * - 数据序列化
 * - 特殊字符和控制字符生成
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1到n：要转换的字节值（每个必须在0-255范围内）
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 成功执行，栈顶为转换后的字符串
 * 
 * @pre L != NULL
 * @pre 每个参数都是0-255范围内的整数
 * @post 栈顶包含由字节值组成的字符串
 * 
 * @note 字节值必须在0-255范围内，否则抛出参数错误
 * @note 无参数时返回空字符串
 * @note 生成的是字节序列，多字节编码需要调用者保证正确性
 * 
 * @warning 超出0-255范围的值会导致运行时错误
 * @warning 多字节字符编码需要特别注意字节序列的正确性
 * 
 * @since Lua 5.0
 * @see str_byte(), luaL_argcheck(), uchar()
 */
static int str_char(lua_State *L) {
    int n = lua_gettop(L);                          // 获取参数数量
    int i;
    luaL_Buffer b;
    
    luaL_buffinit(L, &b);                           // 初始化字符串构建缓冲区
    
    // 循环处理每个参数
    for (i = 1; i <= n; i++) {
        int c = luaL_checkint(L, i);                // 获取字节值参数
        
        // 验证字节值在有效范围内（0-255）
        luaL_argcheck(L, uchar(c) == c, i, "invalid value");
        
        // 将字节值作为字符添加到缓冲区
        luaL_addchar(&b, uchar(c));
    }
    
    luaL_pushresult(&b);                            // 将缓冲区内容作为字符串推入栈
    return 1;
}


/**
 * @brief 写入回调函数：用于lua_dump的字符串写入器
 * 
 * 详细说明：
 * 这是一个专门为string.dump函数设计的写入回调函数。当lua_dump需要输出
 * 编译后的字节码时，会调用这个函数将数据写入到luaL_Buffer中。这种设计
 * 允许将函数的字节码表示累积到一个字符串缓冲区中。
 * 
 * 回调机制：
 * lua_dump函数使用writer回调模式，每次有数据需要输出时就调用注册的
 * 写入函数。这个函数的职责是将接收到的数据添加到目标缓冲区中。
 * 
 * @param[in] L Lua虚拟机状态指针（在此函数中未使用）
 * @param[in] b 要写入的数据缓冲区指针
 * @param[in] size 要写入的数据大小（字节数）
 * @param[in] B 目标luaL_Buffer指针，用于接收数据
 * 
 * @return 写入状态码，0表示成功
 * @retval 0 写入成功，数据已添加到缓冲区
 * 
 * @note 第一个参数L被显式标记为未使用，避免编译器警告
 * @since Lua 5.0
 * @see str_dump(), lua_dump()
 */
static int writer(lua_State *L, const void* b, size_t size, void* B) {
    (void)L;                                        // 显式标记L参数未使用
    luaL_addlstring((luaL_Buffer*) B, (const char *)b, size);
    return 0;
}

/**
 * @brief 函数序列化：将Lua函数转换为字节码字符串
 * 
 * 详细说明：
 * 这个函数实现了Lua的string.dump函数，用于将Lua函数序列化为二进制字节码
 * 表示。生成的字节码可以保存到文件或通过网络传输，之后可以使用loadstring
 * 或load函数重新加载。这是Lua代码持久化和分发的重要机制。
 * 
 * 算法描述：
 * 1. 验证第一个参数是函数类型
 * 2. 清理栈，确保只有函数在栈顶
 * 3. 初始化字符串缓冲区用于接收字节码
 * 4. 调用lua_dump将函数编译为字节码并写入缓冲区
 * 5. 将缓冲区内容作为字符串返回
 * 
 * 字节码特征：
 * - 平台相关：字节码格式可能因Lua版本和平台而异
 * - 二进制格式：包含操作码、常量表、调试信息等
 * - 完整性：包含函数执行所需的全部信息
 * 
 * 安全考虑：
 * 字节码是二进制格式，包含可执行代码。在加载外部字节码时需要谨慎，
 * 确保来源可信，避免执行恶意代码。
 * 
 * 使用示例：
 * @code
 * // Lua代码示例
 * function greet(name)
 *     return "Hello, " .. name
 * end
 * 
 * -- 将函数序列化为字节码
 * local bytecode = string.dump(greet)
 * 
 * -- 保存到文件
 * local file = io.open("greet.luac", "wb")
 * file:write(bytecode)
 * file:close()
 * 
 * -- 重新加载函数
 * local loaded_func = loadstring(bytecode)
 * print(loaded_func("World"))  -- "Hello, World"
 * @endcode
 * 
 * 限制和注意事项：
 * - 只能序列化Lua函数，不能序列化C函数
 * - 字节码格式可能不兼容不同版本的Lua
 * - 生成的字节码包含调试信息，可能较大
 * 
 * 实际应用：
 * - 代码预编译和分发
 * - 动态代码生成和缓存
 * - 插件系统和模块化架构
 * - 代码保护和混淆
 * 
 * @param[in] L Lua虚拟机状态指针，包含函数调用的上下文信息
 *              栈位置1：要序列化的Lua函数
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 成功执行，栈顶为函数的字节码字符串
 * 
 * @pre L != NULL && lua_isfunction(L, 1) && !lua_iscfunction(L, 1)
 * @post 栈顶包含函数的二进制字节码表示
 * 
 * @note 只能处理Lua函数，不能处理C函数
 * @note 生成的字节码是二进制数据，包含null字节
 * @note 字节码格式依赖于Lua版本和编译配置
 * 
 * @warning 加载外部字节码时需要验证来源可信性
 * @warning 字节码可能包含敏感的源代码信息
 * 
 * @since Lua 5.0
 * @see lua_dump(), writer(), loadstring()
 */
static int str_dump(lua_State *L) {
    luaL_Buffer b;
    
    luaL_checktype(L, 1, LUA_TFUNCTION);            // 确保第一个参数是函数
    lua_settop(L, 1);                               // 清理栈，只保留函数
    luaL_buffinit(L, &b);                           // 初始化字符串缓冲区
    
    // 将函数编译为字节码并写入缓冲区
    if (lua_dump(L, writer, &b) != 0) {
        luaL_error(L, "unable to dump given function");
    }
    
    luaL_pushresult(&b);                            // 将缓冲区内容作为字符串推入栈
    return 1;
}



/**
 * =====================================================================
 * 模式匹配系统 - Lua字符串库的核心功能
 * =====================================================================
 * 
 * 这一部分实现了Lua独特的模式匹配系统，它提供了类似正则表达式但更轻量的
 * 文本处理能力。与标准正则表达式相比，Lua的模式匹配系统设计更简洁，
 * 性能更高，同时提供了足够的功能来处理大多数文本处理任务。
 * 
 * 核心特性：
 * - 字符类匹配：支持预定义和自定义字符类
 * - 量词支持：*（0或多个）、+（1或多个）、?（0或1个）、-（最少匹配）
 * - 捕获组：使用()进行分组捕获
 * - 位置锚定：^（字符串开始）、$（字符串结束）
 * - 平衡匹配：%b用于匹配平衡的括号对
 * - 边界匹配：%f用于单词边界匹配
 * 
 * 设计哲学：
 * Lua的模式匹配追求简洁性和可预测性，避免了正则表达式的复杂性和
 * 潜在的性能陷阱，同时保持足够的表达能力来处理常见的文本处理任务。
 */

/**
 * @brief 捕获状态常量：未完成的捕获组标记
 * 
 * 当捕获组开始但尚未结束时，使用此值标记捕获组的长度。这允许模式匹配
 * 引擎跟踪嵌套的捕获组和处理匹配失败时的回溯操作。
 */
#define CAP_UNFINISHED	(-1)

/**
 * @brief 位置捕获常量：位置捕获组标记
 * 
 * 用于标记位置捕获组，这种捕获组不捕获实际的文本内容，而是捕获匹配
 * 发生的位置。通常用于()空捕获组，返回匹配位置而非匹配文本。
 */
#define CAP_POSITION	(-2)

/**
 * @brief 模式匹配状态结构：维护模式匹配过程中的全部状态信息
 * 
 * 详细说明：
 * 这个结构体是整个模式匹配系统的核心，它维护了匹配过程中需要的所有
 * 状态信息。通过这个结构体，匹配引擎可以跟踪源字符串、当前位置、
 * 捕获组信息等，支持复杂的模式匹配操作。
 * 
 * 设计理念：
 * 采用状态机模式，将匹配过程的所有上下文信息集中管理，便于实现
 * 递归匹配、回溯操作和捕获组管理。
 * 
 * 内存管理：
 * 结构体通常在栈上分配，生命周期与单次匹配操作相同。捕获组数组
 * 使用固定大小，避免动态内存分配的开销和复杂性。
 * 
 * 线程安全性：
 * 结构体实例是线程局部的，不同线程的匹配操作使用独立的状态结构，
 * 确保线程安全性。
 * 
 * @since Lua 5.0
 * @see match(), start_capture(), end_capture()
 */
typedef struct MatchState {
    const char *src_init;                           /**< 源字符串的起始指针，用于计算相对位置 */
    const char *src_end;                            /**< 源字符串的结束指针（指向'\0'），用于边界检查 */
    lua_State *L;                                   /**< Lua虚拟机状态指针，用于错误处理和栈操作 */
    int level;                                      /**< 当前捕获组的总数量（包括已完成和未完成的） */
    struct {
        const char *init;                           /**< 捕获组的起始位置指针 */
        ptrdiff_t len;                              /**< 捕获组的长度，或特殊状态标记 */
    } capture[LUA_MAXCAPTURES];                     /**< 捕获组数组，固定大小避免动态分配 */
} MatchState;

/**
 * @brief 转义字符常量：Lua模式中的转义字符
 * 
 * 在Lua模式中，'%'字符用作转义字符，类似于其他正则表达式系统中的'\'。
 * 它用于转义特殊字符，定义字符类，以及实现特殊的匹配功能。
 */
#define L_ESC		'%'

/**
 * @brief 特殊字符集合：需要转义的模式特殊字符
 * 
 * 这个字符串包含了所有在Lua模式中具有特殊含义的字符。当需要匹配这些
 * 字符的字面值时，必须使用'%'进行转义。
 * 
 * 特殊字符含义：
 * - ^ : 字符串开始锚定
 * - $ : 字符串结束锚定  
 * - * : 零个或多个前面的字符
 * - + : 一个或多个前面的字符
 * - ? : 零个或一个前面的字符
 * - . : 匹配任意字符
 * - ( : 捕获组开始
 * - [ : 字符类开始
 * - % : 转义字符
 * - - : 字符类中的范围操作符
 */
#define SPECIALS	"^$*+?.([%-"


/**
 * @brief 检查捕获组有效性：验证并返回有效的捕获组索引
 * 
 * 详细说明：
 * 这个函数用于验证捕获组引用的有效性。在Lua模式中，可以使用%1、%2等
 * 来引用之前捕获的内容。这个函数确保引用的捕获组存在且已经完成捕获，
 * 防止访问无效或未完成的捕获组。
 * 
 * 验证逻辑：
 * 1. 将字符索引转换为数组索引（'1'->0, '2'->1等）
 * 2. 检查索引是否在有效范围内
 * 3. 确保对应的捕获组已经完成（不是CAP_UNFINISHED状态）
 * 
 * 错误处理：
 * 如果捕获组索引无效，函数会抛出Lua错误并终止当前操作，确保不会
 * 访问无效的内存位置或返回错误的匹配结果。
 * 
 * @param[in] ms 模式匹配状态结构指针
 * @param[in] l 捕获组字符索引（'1'表示第一个捕获组）
 * 
 * @return 有效的捕获组数组索引（0-based）
 * @retval >=0 有效的捕获组索引
 * 
 * @pre ms != NULL && ms->L != NULL
 * @pre l为有效的数字字符('1'-'9')
 * 
 * @note 如果验证失败会抛出Lua错误，函数不会正常返回
 * @since Lua 5.0
 * @see match_capture()
 */
static int check_capture(MatchState *ms, int l) {
    l -= '1';                                       // 转换为数组索引（'1'->0）
    
    // 验证捕获组索引的有效性
    if (l < 0 || l >= ms->level || ms->capture[l].len == CAP_UNFINISHED) {
        return luaL_error(ms->L, "invalid capture index");
    }
    
    return l;
}

/**
 * @brief 查找待关闭的捕获组：寻找最近的未完成捕获组
 * 
 * 详细说明：
 * 当遇到捕获组结束标记')'时，需要找到对应的开始捕获组'('。这个函数
 * 从当前最高级别的捕获组开始向前搜索，找到最近的未完成捕获组。
 * 这支持了捕获组的嵌套结构。
 * 
 * 搜索算法：
 * 从当前最高level开始向下搜索，寻找第一个状态为CAP_UNFINISHED的
 * 捕获组。这确保了正确的嵌套匹配行为。
 * 
 * 错误处理：
 * 如果没有找到未完成的捕获组，说明模式中存在不匹配的')'，函数会
 * 抛出错误提示模式格式错误。
 * 
 * @param[in] ms 模式匹配状态结构指针
 * 
 * @return 待关闭的捕获组索引
 * @retval >=0 找到的未完成捕获组索引
 * 
 * @pre ms != NULL && ms->level > 0
 * 
 * @note 如果未找到待关闭的捕获组会抛出Lua错误
 * @since Lua 5.0
 * @see end_capture()
 */
static int capture_to_close(MatchState *ms) {
    int level = ms->level;
    
    // 从最高级别开始向下搜索未完成的捕获组
    for (level--; level >= 0; level--) {
        if (ms->capture[level].len == CAP_UNFINISHED) {
            return level;
        }
    }
    
    // 没有找到未完成的捕获组，模式错误
    return luaL_error(ms->L, "invalid pattern capture");
}


/**
 * @brief 查找字符类结束位置：定位模式中字符类的结束位置
 * 
 * 详细说明：
 * 这个函数用于在解析Lua模式时确定字符类的边界。它需要正确处理各种
 * 字符类格式，包括转义字符、字符集合以及简单字符。这是模式解析器的
 * 重要组成部分，确保模式能够被正确分解为匹配单元。
 * 
 * 处理的字符类类型：
 * 1. 转义序列：%d、%a、%s等预定义字符类
 * 2. 字符集合：[abc]、[^abc]、[a-z]等自定义字符集
 * 3. 简单字符：普通字符直接匹配
 * 
 * 算法复杂度：
 * - 时间复杂度：O(n)，其中n为字符类的长度
 * - 空间复杂度：O(1)，只使用常量额外空间
 * 
 * 边界检查：
 * 函数会检测模式中的语法错误，如未闭合的字符集合或不完整的转义序列，
 * 并通过Lua错误机制报告问题。
 * 
 * @param[in] ms 模式匹配状态结构指针，用于错误报告
 * @param[in] p 字符类开始位置的指针
 * 
 * @return 字符类结束后的下一个位置指针
 * @retval const char* 指向字符类之后的第一个字符
 * 
 * @pre ms != NULL && p != NULL
 * @post 返回值指向有效的字符位置或字符串结尾
 * 
 * @note 如果遇到格式错误的模式会抛出Lua错误
 * @since Lua 5.0
 * @see match_class(), singlematch()
 */
static const char *classend(MatchState *ms, const char *p) {
    switch (*p++) {
        case L_ESC: {
            // 处理转义字符：%d、%a、%s等
            if (*p == '\0') {
                luaL_error(ms->L, "malformed pattern (ends with " LUA_QL("%%") ")");
            }
            return p + 1;
        }
        case '[': {
            // 处理字符集合：[abc]、[^abc]等
            if (*p == '^') {
                p++;                                // 跳过否定标记
            }
            
            do {
                // 查找字符集合的结束标记']'
                if (*p == '\0') {
                    luaL_error(ms->L, "malformed pattern (missing " LUA_QL("]") ")");
                }
                
                if (*(p++) == L_ESC && *p != '\0') {
                    p++;                            // 跳过转义字符（如%]）
                }
            } while (*p != ']');
            
            return p + 1;
        }
        default: {
            // 简单字符：直接返回下一个位置
            return p;
        }
    }
}

/**
 * @brief 字符类匹配：检查字符是否匹配指定的字符类
 * 
 * 详细说明：
 * 这个函数实现了Lua模式中预定义字符类的匹配逻辑。Lua提供了一套丰富的
 * 字符类，用于匹配不同类型的字符。函数支持大小写区分的字符类匹配，
 * 小写字符类表示正向匹配，大写字符类表示反向匹配。
 * 
 * 支持的字符类：
 * - %a/%A: 字母字符/非字母字符
 * - %c/%C: 控制字符/非控制字符
 * - %d/%D: 数字字符/非数字字符
 * - %l/%L: 小写字母/非小写字母
 * - %p/%P: 标点字符/非标点字符
 * - %s/%S: 空白字符/非空白字符
 * - %u/%U: 大写字母/非大写字母
 * - %w/%W: 字母数字字符/非字母数字字符
 * - %x/%X: 十六进制数字/非十六进制数字
 * - %z/%Z: null字符/非null字符
 * 
 * 实现机制：
 * 使用标准C库的字符分类函数（isalpha、isdigit等）进行字符类型判断，
 * 确保跨平台兼容性。通过大小写判断来决定是正向匹配还是反向匹配。
 * 
 * @param[in] c 要检查的字符
 * @param[in] cl 字符类标识符（如'd'表示数字，'D'表示非数字）
 * 
 * @return 匹配结果
 * @retval 1 字符匹配指定的字符类
 * @retval 0 字符不匹配指定的字符类
 * 
 * @pre c和cl都是有效的字符值
 * 
 * @note 使用标准C库函数，行为依赖于当前locale设置
 * @note 对于未知的字符类，执行直接字符比较
 * 
 * @since Lua 5.0
 * @see singlematch(), matchbracketclass()
 */
static int match_class(int c, int cl) {
    int res;
    
    // 根据字符类标识符进行匹配
    switch (tolower(cl)) {
        case 'a': res = isalpha(c); break;          // 字母字符
        case 'c': res = iscntrl(c); break;          // 控制字符
        case 'd': res = isdigit(c); break;          // 数字字符
        case 'l': res = islower(c); break;          // 小写字母
        case 'p': res = ispunct(c); break;          // 标点字符
        case 's': res = isspace(c); break;          // 空白字符
        case 'u': res = isupper(c); break;          // 大写字母
        case 'w': res = isalnum(c); break;          // 字母数字字符
        case 'x': res = isxdigit(c); break;         // 十六进制数字
        case 'z': res = (c == 0); break;           // null字符
        default: return (cl == c);                  // 直接字符比较
    }
    
    // 根据字符类的大小写决定是否反转结果
    return (islower(cl) ? res : !res);
}


/**
 * @brief 字符集合匹配：检查字符是否匹配字符集合模式
 * 
 * 详细说明：
 * 这个函数处理Lua模式中的字符集合匹配，如[abc]、[^def]、[a-z]等。
 * 它支持字符范围、字符枚举、否定匹配以及集合内的转义字符。这是Lua
 * 模式系统中最复杂的匹配组件之一，提供了灵活的字符匹配能力。
 * 
 * 支持的字符集合格式：
 * - [abc]     : 匹配a、b或c中的任一字符
 * - [^abc]    : 匹配除a、b、c之外的任何字符
 * - [a-z]     : 匹配从a到z的任何字符（范围匹配）
 * - [%d%a]    : 匹配数字或字母（字符类组合）
 * - [%]]      : 匹配']'字符（转义）
 * 
 * 算法描述：
 * 1. 检查是否为否定模式（以^开头）
 * 2. 遍历字符集合内的每个字符或范围
 * 3. 处理转义字符和字符类
 * 4. 处理字符范围（a-z形式）
 * 5. 根据否定标志返回匹配结果
 * 
 * 性能特征：
 * - 时间复杂度：O(n)，其中n为字符集合的长度
 * - 空间复杂度：O(1)，不使用额外存储空间
 * - 对于简单集合匹配速度很快
 * 
 * @param[in] c 要检查的字符
 * @param[in] p 字符集合开始位置（'['之后）
 * @param[in] ec 字符集合结束位置（']'位置）
 * 
 * @return 匹配结果
 * @retval 1 字符匹配字符集合（或不匹配否定集合）
 * @retval 0 字符不匹配字符集合（或匹配否定集合）
 * 
 * @pre p != NULL && ec != NULL && p < ec
 * @pre p指向'['之后的第一个字符，ec指向']'字符
 * 
 * @note 支持字符范围、转义字符和字符类
 * @note '^'在集合开头表示否定，在其他位置表示普通字符
 * 
 * @since Lua 5.0
 * @see match_class(), classend()
 */
static int matchbracketclass(int c, const char *p, const char *ec) {
    int sig = 1;                                    // 匹配标志，1表示正向匹配，0表示反向匹配
    
    // 检查是否为否定字符集合（以^开头）
    if (*(p + 1) == '^') {
        sig = 0;                                    // 设置为反向匹配
        p++;                                        // 跳过'^'字符
    }
    
    // 遍历字符集合中的每个元素
    while (++p < ec) {
        if (*p == L_ESC) {
            // 处理转义字符或字符类（如%d、%a等）
            p++;
            if (match_class(c, uchar(*p))) {
                return sig;
            }
        }
        else if ((*(p + 1) == '-') && (p + 2 < ec)) {
            // 处理字符范围（如a-z）
            p += 2;                                 // 跳过'-'和范围结束字符
            if (uchar(*(p - 2)) <= c && c <= uchar(*p)) {
                return sig;
            }
        }
        else if (uchar(*p) == c) {
            // 直接字符匹配
            return sig;
        }
    }
    
    // 未找到匹配，返回相反的结果
    return !sig;
}

/**
 * @brief 单字符模式匹配：检查单个字符是否匹配模式元素
 * 
 * 详细说明：
 * 这个函数是模式匹配的基础组件，负责检查单个字符是否匹配模式中的一个
 * 元素。它处理各种类型的模式元素，包括通配符、转义序列、字符集合和
 * 普通字符。这是整个模式匹配系统的核心构建块。
 * 
 * 支持的模式元素：
 * - .      : 匹配任何字符（通配符）
 * - %x     : 匹配字符类x或转义字符
 * - [...]  : 匹配字符集合
 * - 普通字符: 直接字符匹配
 * 
 * 设计理念：
 * 函数采用简洁的switch-case结构，每种模式类型都有专门的处理逻辑。
 * 这种设计既保证了性能，又使代码易于理解和维护。
 * 
 * 性能优化：
 * - 通配符匹配：O(1)时间复杂度
 * - 字符类匹配：O(1)时间复杂度
 * - 字符集合匹配：O(n)时间复杂度，n为集合大小
 * - 普通字符匹配：O(1)时间复杂度
 * 
 * @param[in] c 要检查的字符
 * @param[in] p 模式元素开始位置
 * @param[in] ep 模式元素结束位置
 * 
 * @return 匹配结果
 * @retval 1 字符匹配模式元素
 * @retval 0 字符不匹配模式元素
 * 
 * @pre p != NULL && ep != NULL && p <= ep
 * 
 * @note 函数不验证模式的语法正确性，调用者需要确保模式有效
 * @note 字符集合的结束位置ep指向']'的下一个位置
 * 
 * @since Lua 5.0
 * @see match_class(), matchbracketclass()
 */
static int singlematch(int c, const char *p, const char *ep) {
    switch (*p) {
        case '.':
            // 通配符：匹配任何字符
            return 1;
            
        case L_ESC:
            // 转义字符或字符类：使用字符类匹配函数
            return match_class(c, uchar(*(p + 1)));
            
        case '[':
            // 字符集合：使用字符集合匹配函数
            return matchbracketclass(c, p, ep - 1);
            
        default:
            // 普通字符：直接比较
            return (uchar(*p) == c);
    }
}


static const char *match (MatchState *ms, const char *s, const char *p);


static const char *matchbalance (MatchState *ms, const char *s,
                                   const char *p) {
  if (*p == 0 || *(p+1) == 0)
    luaL_error(ms->L, "unbalanced pattern");
  if (*s != *p) return NULL;
  else {
    int b = *p;
    int e = *(p+1);
    int cont = 1;
    while (++s < ms->src_end) {
      if (*s == e) {
        if (--cont == 0) return s+1;
      }
      else if (*s == b) cont++;
    }
  }
  return NULL;  /* string ends out of balance */
}


static const char *max_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  ptrdiff_t i = 0;  /* counts maximum expand for item */
  while ((s+i)<ms->src_end && singlematch(uchar(*(s+i)), p, ep))
    i++;
  /* keeps trying to match with the maximum repetitions */
  while (i>=0) {
    const char *res = match(ms, (s+i), ep+1);
    if (res) return res;
    i--;  /* else didn't match; reduce 1 repetition to try again */
  }
  return NULL;
}


static const char *min_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  for (;;) {
    const char *res = match(ms, s, ep+1);
    if (res != NULL)
      return res;
    else if (s<ms->src_end && singlematch(uchar(*s), p, ep))
      s++;  /* try with one more repetition */
    else return NULL;
  }
}


static const char *start_capture (MatchState *ms, const char *s,
                                    const char *p, int what) {
  const char *res;
  int level = ms->level;
  if (level >= LUA_MAXCAPTURES) luaL_error(ms->L, "too many captures");
  ms->capture[level].init = s;
  ms->capture[level].len = what;
  ms->level = level+1;
  if ((res=match(ms, s, p)) == NULL)  /* match failed? */
    ms->level--;  /* undo capture */
  return res;
}


static const char *end_capture (MatchState *ms, const char *s,
                                  const char *p) {
  int l = capture_to_close(ms);
  const char *res;
  ms->capture[l].len = s - ms->capture[l].init;  /* close capture */
  if ((res = match(ms, s, p)) == NULL)  /* match failed? */
    ms->capture[l].len = CAP_UNFINISHED;  /* undo capture */
  return res;
}


static const char *match_capture (MatchState *ms, const char *s, int l) {
  size_t len;
  l = check_capture(ms, l);
  len = ms->capture[l].len;
  if ((size_t)(ms->src_end-s) >= len &&
      memcmp(ms->capture[l].init, s, len) == 0)
    return s+len;
  else return NULL;
}


static const char *match (MatchState *ms, const char *s, const char *p) {
  init: /* using goto's to optimize tail recursion */
  switch (*p) {
    case '(': {  /* start capture */
      if (*(p+1) == ')')  /* position capture? */
        return start_capture(ms, s, p+2, CAP_POSITION);
      else
        return start_capture(ms, s, p+1, CAP_UNFINISHED);
    }
    case ')': {  /* end capture */
      return end_capture(ms, s, p+1);
    }
    case L_ESC: {
      switch (*(p+1)) {
        case 'b': {  /* balanced string? */
          s = matchbalance(ms, s, p+2);
          if (s == NULL) return NULL;
          p+=4; goto init;  /* else return match(ms, s, p+4); */
        }
        case 'f': {  /* frontier? */
          const char *ep; char previous;
          p += 2;
          if (*p != '[')
            luaL_error(ms->L, "missing " LUA_QL("[") " after "
                               LUA_QL("%%f") " in pattern");
          ep = classend(ms, p);  /* points to what is next */
          previous = (s == ms->src_init) ? '\0' : *(s-1);
          if (matchbracketclass(uchar(previous), p, ep-1) ||
             !matchbracketclass(uchar(*s), p, ep-1)) return NULL;
          p=ep; goto init;  /* else return match(ms, s, ep); */
        }
        default: {
          if (isdigit(uchar(*(p+1)))) {  /* capture results (%0-%9)? */
            s = match_capture(ms, s, uchar(*(p+1)));
            if (s == NULL) return NULL;
            p+=2; goto init;  /* else return match(ms, s, p+2) */
          }
          goto dflt;  /* case default */
        }
      }
    }
    case '\0': {  /* end of pattern */
      return s;  /* match succeeded */
    }
    case '$': {
      if (*(p+1) == '\0')  /* is the `$' the last char in pattern? */
        return (s == ms->src_end) ? s : NULL;  /* check end of string */
      else goto dflt;
    }
    default: dflt: {  /* it is a pattern item */
      const char *ep = classend(ms, p);  /* points to what is next */
      int m = s<ms->src_end && singlematch(uchar(*s), p, ep);
      switch (*ep) {
        case '?': {  /* optional */
          const char *res;
          if (m && ((res=match(ms, s+1, ep+1)) != NULL))
            return res;
          p=ep+1; goto init;  /* else return match(ms, s, ep+1); */
        }
        case '*': {  /* 0 or more repetitions */
          return max_expand(ms, s, p, ep);
        }
        case '+': {  /* 1 or more repetitions */
          return (m ? max_expand(ms, s+1, p, ep) : NULL);
        }
        case '-': {  /* 0 or more repetitions (minimum) */
          return min_expand(ms, s, p, ep);
        }
        default: {
          if (!m) return NULL;
          s++; p=ep; goto init;  /* else return match(ms, s+1, ep); */
        }
      }
    }
  }
}



static const char *lmemfind (const char *s1, size_t l1,
                               const char *s2, size_t l2) {
  if (l2 == 0) return s1;  /* empty strings are everywhere */
  else if (l2 > l1) return NULL;  /* avoids a negative `l1' */
  else {
    const char *init;  /* to search for a `*s2' inside `s1' */
    l2--;  /* 1st char will be checked by `memchr' */
    l1 = l1-l2;  /* `s2' cannot be found after that */
    while (l1 > 0 && (init = (const char *)memchr(s1, *s2, l1)) != NULL) {
      init++;   /* 1st char is already checked */
      if (memcmp(init, s2+1, l2) == 0)
        return init-1;
      else {  /* correct `l1' and `s1' to try again */
        l1 -= init-s1;
        s1 = init;
      }
    }
    return NULL;  /* not found */
  }
}


static void push_onecapture (MatchState *ms, int i, const char *s,
                                                    const char *e) {
  if (i >= ms->level) {
    if (i == 0)  /* ms->level == 0, too */
      lua_pushlstring(ms->L, s, e - s);  /* add whole match */
    else
      luaL_error(ms->L, "invalid capture index");
  }
  else {
    ptrdiff_t l = ms->capture[i].len;
    if (l == CAP_UNFINISHED) luaL_error(ms->L, "unfinished capture");
    if (l == CAP_POSITION)
      lua_pushinteger(ms->L, ms->capture[i].init - ms->src_init + 1);
    else
      lua_pushlstring(ms->L, ms->capture[i].init, l);
  }
}


static int push_captures (MatchState *ms, const char *s, const char *e) {
  int i;
  int nlevels = (ms->level == 0 && s) ? 1 : ms->level;
  luaL_checkstack(ms->L, nlevels, "too many captures");
  for (i = 0; i < nlevels; i++)
    push_onecapture(ms, i, s, e);
  return nlevels;  /* number of strings pushed */
}


static int str_find_aux (lua_State *L, int find) {
  size_t l1, l2;
  const char *s = luaL_checklstring(L, 1, &l1);
  const char *p = luaL_checklstring(L, 2, &l2);
  ptrdiff_t init = posrelat(luaL_optinteger(L, 3, 1), l1) - 1;
  if (init < 0) init = 0;
  else if ((size_t)(init) > l1) init = (ptrdiff_t)l1;
  if (find && (lua_toboolean(L, 4) ||  /* explicit request? */
      strpbrk(p, SPECIALS) == NULL)) {  /* or no special characters? */
    /* do a plain search */
    const char *s2 = lmemfind(s+init, l1-init, p, l2);
    if (s2) {
      lua_pushinteger(L, s2-s+1);
      lua_pushinteger(L, s2-s+l2);
      return 2;
    }
  }
  else {
    MatchState ms;
    int anchor = (*p == '^') ? (p++, 1) : 0;
    const char *s1=s+init;
    ms.L = L;
    ms.src_init = s;
    ms.src_end = s+l1;
    do {
      const char *res;
      ms.level = 0;
      if ((res=match(&ms, s1, p)) != NULL) {
        if (find) {
          lua_pushinteger(L, s1-s+1);  /* start */
          lua_pushinteger(L, res-s);   /* end */
          return push_captures(&ms, NULL, 0) + 2;
        }
        else
          return push_captures(&ms, s1, res);
      }
    } while (s1++ < ms.src_end && !anchor);
  }
  lua_pushnil(L);  /* not found */
  return 1;
}


static int str_find (lua_State *L) {
  return str_find_aux(L, 1);
}


static int str_match (lua_State *L) {
  return str_find_aux(L, 0);
}


static int gmatch_aux (lua_State *L) {
  MatchState ms;
  size_t ls;
  const char *s = lua_tolstring(L, lua_upvalueindex(1), &ls);
  const char *p = lua_tostring(L, lua_upvalueindex(2));
  const char *src;
  ms.L = L;
  ms.src_init = s;
  ms.src_end = s+ls;
  for (src = s + (size_t)lua_tointeger(L, lua_upvalueindex(3));
       src <= ms.src_end;
       src++) {
    const char *e;
    ms.level = 0;
    if ((e = match(&ms, src, p)) != NULL) {
      lua_Integer newstart = e-s;
      if (e == src) newstart++;  /* empty match? go at least one position */
      lua_pushinteger(L, newstart);
      lua_replace(L, lua_upvalueindex(3));
      return push_captures(&ms, src, e);
    }
  }
  return 0;  /* not found */
}


static int gmatch (lua_State *L) {
  luaL_checkstring(L, 1);
  luaL_checkstring(L, 2);
  lua_settop(L, 2);
  lua_pushinteger(L, 0);
  lua_pushcclosure(L, gmatch_aux, 3);
  return 1;
}


static int gfind_nodef (lua_State *L) {
  return luaL_error(L, LUA_QL("string.gfind") " was renamed to "
                       LUA_QL("string.gmatch"));
}


static void add_s (MatchState *ms, luaL_Buffer *b, const char *s,
                                                   const char *e) {
  size_t l, i;
  const char *news = lua_tolstring(ms->L, 3, &l);
  for (i = 0; i < l; i++) {
    if (news[i] != L_ESC)
      luaL_addchar(b, news[i]);
    else {
      i++;  /* skip ESC */
      if (!isdigit(uchar(news[i])))
        luaL_addchar(b, news[i]);
      else if (news[i] == '0')
          luaL_addlstring(b, s, e - s);
      else {
        push_onecapture(ms, news[i] - '1', s, e);
        luaL_addvalue(b);  /* add capture to accumulated result */
      }
    }
  }
}


static void add_value (MatchState *ms, luaL_Buffer *b, const char *s,
                                                       const char *e) {
  lua_State *L = ms->L;
  switch (lua_type(L, 3)) {
    case LUA_TNUMBER:
    case LUA_TSTRING: {
      add_s(ms, b, s, e);
      return;
    }
    case LUA_TFUNCTION: {
      int n;
      lua_pushvalue(L, 3);
      n = push_captures(ms, s, e);
      lua_call(L, n, 1);
      break;
    }
    case LUA_TTABLE: {
      push_onecapture(ms, 0, s, e);
      lua_gettable(L, 3);
      break;
    }
  }
  if (!lua_toboolean(L, -1)) {  /* nil or false? */
    lua_pop(L, 1);
    lua_pushlstring(L, s, e - s);  /* keep original text */
  }
  else if (!lua_isstring(L, -1))
    luaL_error(L, "invalid replacement value (a %s)", luaL_typename(L, -1)); 
  luaL_addvalue(b);  /* add result to accumulator */
}


static int str_gsub (lua_State *L) {
  size_t srcl;
  const char *src = luaL_checklstring(L, 1, &srcl);
  const char *p = luaL_checkstring(L, 2);
  int  tr = lua_type(L, 3);
  int max_s = luaL_optint(L, 4, srcl+1);
  int anchor = (*p == '^') ? (p++, 1) : 0;
  int n = 0;
  MatchState ms;
  luaL_Buffer b;
  luaL_argcheck(L, tr == LUA_TNUMBER || tr == LUA_TSTRING ||
                   tr == LUA_TFUNCTION || tr == LUA_TTABLE, 3,
                      "string/function/table expected");
  luaL_buffinit(L, &b);
  ms.L = L;
  ms.src_init = src;
  ms.src_end = src+srcl;
  while (n < max_s) {
    const char *e;
    ms.level = 0;
    e = match(&ms, src, p);
    if (e) {
      n++;
      add_value(&ms, &b, src, e);
    }
    if (e && e>src) /* non empty match? */
      src = e;  /* skip it */
    else if (src < ms.src_end)
      luaL_addchar(&b, *src++);
    else break;
    if (anchor) break;
  }
  luaL_addlstring(&b, src, ms.src_end-src);
  luaL_pushresult(&b);
  lua_pushinteger(L, n);  /* number of substitutions */
  return 2;
}

/* }====================================================== */


/* maximum size of each formatted item (> len(format('%99.99f', -1e308))) */
#define MAX_ITEM	512
/* valid flags in a format specification */
#define FLAGS	"-+ #0"
/*
** maximum size of each format specification (such as '%-099.99d')
** (+10 accounts for %99.99x plus margin of error)
*/
#define MAX_FORMAT	(sizeof(FLAGS) + sizeof(LUA_INTFRMLEN) + 10)


static void addquoted (lua_State *L, luaL_Buffer *b, int arg) {
  size_t l;
  const char *s = luaL_checklstring(L, arg, &l);
  luaL_addchar(b, '"');
  while (l--) {
    switch (*s) {
      case '"': case '\\': case '\n': {
        luaL_addchar(b, '\\');
        luaL_addchar(b, *s);
        break;
      }
      case '\r': {
        luaL_addlstring(b, "\\r", 2);
        break;
      }
      case '\0': {
        luaL_addlstring(b, "\\000", 4);
        break;
      }
      default: {
        luaL_addchar(b, *s);
        break;
      }
    }
    s++;
  }
  luaL_addchar(b, '"');
}

static const char *scanformat (lua_State *L, const char *strfrmt, char *form) {
  const char *p = strfrmt;
  while (*p != '\0' && strchr(FLAGS, *p) != NULL) p++;  /* skip flags */
  if ((size_t)(p - strfrmt) >= sizeof(FLAGS))
    luaL_error(L, "invalid format (repeated flags)");
  if (isdigit(uchar(*p))) p++;  /* skip width */
  if (isdigit(uchar(*p))) p++;  /* (2 digits at most) */
  if (*p == '.') {
    p++;
    if (isdigit(uchar(*p))) p++;  /* skip precision */
    if (isdigit(uchar(*p))) p++;  /* (2 digits at most) */
  }
  if (isdigit(uchar(*p)))
    luaL_error(L, "invalid format (width or precision too long)");
  *(form++) = '%';
  strncpy(form, strfrmt, p - strfrmt + 1);
  form += p - strfrmt + 1;
  *form = '\0';
  return p;
}


static void addintlen (char *form) {
  size_t l = strlen(form);
  char spec = form[l - 1];
  strcpy(form + l - 1, LUA_INTFRMLEN);
  form[l + sizeof(LUA_INTFRMLEN) - 2] = spec;
  form[l + sizeof(LUA_INTFRMLEN) - 1] = '\0';
}


static int str_format (lua_State *L) {
  int top = lua_gettop(L);
  int arg = 1;
  size_t sfl;
  const char *strfrmt = luaL_checklstring(L, arg, &sfl);
  const char *strfrmt_end = strfrmt+sfl;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  while (strfrmt < strfrmt_end) {
    if (*strfrmt != L_ESC)
      luaL_addchar(&b, *strfrmt++);
    else if (*++strfrmt == L_ESC)
      luaL_addchar(&b, *strfrmt++);  /* %% */
    else { /* format item */
      char form[MAX_FORMAT];  /* to store the format (`%...') */
      char buff[MAX_ITEM];  /* to store the formatted item */
      if (++arg > top)
        luaL_argerror(L, arg, "no value");
      strfrmt = scanformat(L, strfrmt, form);
      switch (*strfrmt++) {
        case 'c': {
          sprintf(buff, form, (int)luaL_checknumber(L, arg));
          break;
        }
        case 'd':  case 'i': {
          addintlen(form);
          sprintf(buff, form, (LUA_INTFRM_T)luaL_checknumber(L, arg));
          break;
        }
        case 'o':  case 'u':  case 'x':  case 'X': {
          addintlen(form);
          sprintf(buff, form, (unsigned LUA_INTFRM_T)luaL_checknumber(L, arg));
          break;
        }
        case 'e':  case 'E': case 'f':
        case 'g': case 'G': {
          sprintf(buff, form, (double)luaL_checknumber(L, arg));
          break;
        }
        case 'q': {
          addquoted(L, &b, arg);
          continue;  /* skip the 'addsize' at the end */
        }
        case 's': {
          size_t l;
          const char *s = luaL_checklstring(L, arg, &l);
          if (!strchr(form, '.') && l >= 100) {
            /* no precision and string is too long to be formatted;
               keep original string */
            lua_pushvalue(L, arg);
            luaL_addvalue(&b);
            continue;  /* skip the `addsize' at the end */
          }
          else {
            sprintf(buff, form, s);
            break;
          }
        }
        default: {  /* also treat cases `pnLlh' */
          return luaL_error(L, "invalid option " LUA_QL("%%%c") " to "
                               LUA_QL("format"), *(strfrmt - 1));
        }
      }
      luaL_addlstring(&b, buff, strlen(buff));
    }
  }
  luaL_pushresult(&b);
  return 1;
}


/**
 * @brief 字符串库函数注册表：定义所有导出的字符串操作函数
 * 
 * 详细说明：
 * 这个数组定义了Lua字符串库中所有可供Lua脚本调用的函数。每个条目
 * 包含函数在Lua中的名称和对应的C函数指针。这是Lua C扩展的标准模式，
 * 用于将C函数注册到Lua虚拟机中。
 * 
 * 函数分类：
 * 1. 基本操作：len、sub、reverse、upper、lower、rep
 * 2. 字符转换：byte、char
 * 3. 序列化：dump
 * 4. 查找匹配：find、match、gmatch
 * 5. 替换操作：gsub
 * 6. 格式化：format
 * 7. 兼容性：gfind（已废弃，指向错误函数）
 * 
 * 设计理念：
 * 函数命名遵循简洁明了的原则，每个函数都有明确的单一职责。
 * 这种设计使得字符串库既功能丰富又易于学习和使用。
 * 
 * 兼容性考虑：
 * gfind函数已在Lua 5.1中重命名为gmatch，但为了向后兼容，保留了
 * gfind条目，它会提示用户使用新的函数名。
 * 
 * @since Lua 5.0
 * @see luaL_register(), luaopen_string()
 */
static const luaL_Reg strlib[] = {
    {"byte", str_byte},         /**< 获取字符的字节值 */
    {"char", str_char},         /**< 将字节值转换为字符 */
    {"dump", str_dump},         /**< 序列化函数为字节码 */
    {"find", str_find},         /**< 查找模式匹配的位置 */
    {"format", str_format},     /**< 格式化字符串 */
    {"gfind", gfind_nodef},     /**< 已废弃，提示使用gmatch */
    {"gmatch", gmatch},         /**< 全局模式匹配迭代器 */
    {"gsub", str_gsub},         /**< 全局替换 */
    {"len", str_len},           /**< 获取字符串长度 */
    {"lower", str_lower},       /**< 转换为小写 */
    {"match", str_match},       /**< 模式匹配 */
    {"rep", str_rep},           /**< 重复字符串 */
    {"reverse", str_reverse},   /**< 反转字符串 */
    {"sub", str_sub},           /**< 提取子字符串 */
    {"upper", str_upper},       /**< 转换为大写 */
    {NULL, NULL}                /**< 数组结束标记 */
};

/**
 * @brief 创建字符串元表：为字符串类型设置元表和元方法
 * 
 * 详细说明：
 * 这个函数创建并设置字符串类型的元表，使得字符串值可以直接调用字符串库
 * 函数。通过设置__index元方法，实现了str:len()这样的面向对象语法，
 * 而不仅仅是string.len(str)的函数调用语法。
 * 
 * 实现机制：
 * 1. 创建一个新的表作为字符串元表
 * 2. 创建一个虚拟字符串对象并设置其元表
 * 3. 将字符串库设置为元表的__index方法
 * 4. 清理栈上的临时对象
 * 
 * 元表效果：
 * 设置元表后，字符串值可以像对象一样调用方法：
 * - "hello":len() 等价于 string.len("hello")
 * - "HELLO":lower() 等价于 string.lower("HELLO")
 * - "test":find("e") 等价于 string.find("test", "e")
 * 
 * 设计优势：
 * 这种设计提供了更加直观和面向对象的API，同时保持了函数式API的兼容性。
 * 用户可以根据偏好选择使用哪种语法风格。
 * 
 * 性能考虑：
 * 元表机制会带来轻微的性能开销，但提供了更好的可读性和易用性。
 * 在性能敏感的场景中，可以直接使用函数式API。
 * 
 * @param[in] L Lua虚拟机状态指针
 * 
 * @pre L != NULL
 * @post 字符串类型具有完整的元表和__index方法
 * 
 * @note 函数会修改Lua注册表，设置字符串类型的全局元表
 * @since Lua 5.0
 * @see luaopen_string()
 */
static void createmetatable(lua_State *L) {
    lua_createtable(L, 0, 1);              // 创建字符串元表
    lua_pushliteral(L, "");                // 压入一个空字符串作为示例
    lua_pushvalue(L, -2);                  // 复制元表到栈顶
    lua_setmetatable(L, -2);               // 为空字符串设置元表
    lua_pop(L, 1);                         // 弹出空字符串
    lua_pushvalue(L, -2);                  // 压入字符串库表
    lua_setfield(L, -2, "__index");        // 设置__index为字符串库
    lua_pop(L, 1);                         // 弹出元表
}

/**
 * @brief 打开字符串库：初始化并注册Lua字符串库
 * 
 * 详细说明：
 * 这是字符串库的主要入口函数，负责完整的库初始化过程。它注册所有的
 * 字符串函数到全局命名空间，设置字符串类型的元表，并处理向后兼容性
 * 问题。这个函数遵循Lua C库的标准初始化模式。
 * 
 * 初始化步骤：
 * 1. 注册所有字符串函数到string表中
 * 2. 处理向后兼容性（gfind到gmatch的映射）
 * 3. 创建并设置字符串元表
 * 4. 返回字符串库表
 * 
 * 兼容性处理：
 * 在编译时定义了LUA_COMPAT_GFIND时，会将gmatch函数同时注册为gfind，
 * 以保持与旧版本Lua代码的兼容性。
 * 
 * API规范：
 * 函数遵循Lua C库的标准API：
 * - 接受lua_State参数
 * - 返回推入栈的值的数量
 * - 使用LUALIB_API导出声明
 * 
 * 全局可见性：
 * 注册后的字符串库可以通过全局string表访问，也可以通过字符串值的
 * 元方法直接调用。
 * 
 * 使用示例：
 * @code
 * // C代码中加载字符串库
 * luaopen_string(L);
 * 
 * // Lua代码中使用
 * print(string.len("hello"))    -- 函数式调用
 * print(("hello"):len())        -- 面向对象调用
 * @endcode
 * 
 * @param[in] L Lua虚拟机状态指针
 * 
 * @return 返回值数量，总是返回1
 * @retval 1 字符串库表已压入栈顶
 * 
 * @pre L != NULL
 * @post 字符串库已完全初始化并可供使用
 * @post 全局string表包含所有字符串函数
 * @post 字符串类型具有正确的元表设置
 * 
 * @note 这是库的标准入口函数，通常由Lua核心在启动时调用
 * @note 函数是幂等的，重复调用不会产生副作用
 * 
 * @since Lua 5.0
 * @see luaL_register(), createmetatable()
 */
LUALIB_API int luaopen_string(lua_State *L) {
    // 注册所有字符串函数到string库中
    luaL_register(L, LUA_STRLIBNAME, strlib);
    
#if defined(LUA_COMPAT_GFIND)
    // 向后兼容：将gmatch也注册为gfind
    lua_getfield(L, -1, "gmatch");
    lua_setfield(L, -2, "gfind");
#endif
    
    // 创建字符串元表，支持面向对象语法
    createmetatable(L);
    return 1;
}

