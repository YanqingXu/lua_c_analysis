/**
 * [核心] Lua 对象类型定义头文件
 *
 * 功能概述：
 * 定义Lua值系统的核心数据结构和类型系统。
 * 包含所有Lua值类型、垃圾收集对象、字符串和表的定义。
 *
 * 主要组件：
 * - 值类型系统：TValue结构和类型标记
 * - 垃圾收集：GCObject和公共头部定义
 * - 字符串系统：TString结构和字符串池
 * - 表结构：Table、Node和哈希表实现
 * - 函数对象：Closure、Proto和上值定义
 * 
 * 设计原理：
 * 使用标记联合(tagged union)实现动态类型系统，
 * 所有可收集对象共享统一的垃圾收集头部，
 * 提供高效的内存管理和类型检查机制。
 */

// [头文件保护] 防止重复包含
// #ifndef lobject_h
// #define lobject_h

#include <stdarg.h>

#include "llimits.h"
#include "lua.h"

/**
 * [设计原理] Lua值类型系统
 * 
 * Lua使用标记联合(tagged union)来表示所有值类型。
 * 每个值都包含一个类型标记和对应的数据。
 * 这种设计实现了Lua的动态类型系统。
 * 
 * 核心概念：
 * - 类型标记：标识值的具体类型
 * - 联合体：根据类型存储不同的数据
 * - 垃圾收集：可收集对象的统一管理
 */

/**
 * [类型系统] 从Lua可见的值类型标记
 * 
 * 这些是用户可以通过type()函数看到的基本类型。
 * 对应Lua的8种基本类型：nil、boolean、number、string、
 * table、function、userdata、thread。
 */
#define LAST_TAG    LUA_TTHREAD  // 最后一个用户可见类型
#define NUM_TAGS    (LAST_TAG+1) // 用户可见类型的总数

/**
 * [内部实现] 内部使用的额外类型标记
 * 
 * 这些类型对Lua用户不可见，仅用于虚拟机内部实现。
 * 它们扩展了基本类型系统，支持更精细的内部对象管理。
 */
#define LUA_TPROTO    (LAST_TAG+1)  // 函数原型(编译后的函数)
#define LUA_TUPVAL    (LAST_TAG+2)  // 上值(upvalue)
#define LUA_TDEADKEY  (LAST_TAG+3)  // 表中的死键(已删除的键)

/**
 * [垃圾收集] 所有可收集对象的联合类型
 * 
 * 这是垃圾收集器操作的基础类型，所有需要被GC管理的
 * 对象都必须能够转换为这个类型。
 */
typedef union GCObject GCObject;

/**
 * [垃圾收集] 所有可收集对象的公共头部
 * 
 * 这个宏定义了所有GC对象必须包含的公共字段。
 * 包含在其他对象结构中，确保垃圾收集的一致性。
 * 
 * 字段说明：
 * @field next - GCObject*：指向下一个GC对象的指针，用于GC链表
 * @field tt - lu_byte：类型标记(type tag)，标识对象的具体类型
 * @field marked - lu_byte：GC标记字节，用于标记-清除垃圾收集算法
 * 
 * GC算法支持：
 * - 标记阶段：设置marked字段标记可达对象
 * - 清除阶段：回收未标记的对象
 * - 链表遍历：通过next字段遍历所有GC对象
 */
#define CommonHeader    GCObject *next; lu_byte tt; lu_byte marked

/**
 * [垃圾收集] 公共头部的结构体形式
 * 
 * 将CommonHeader宏转换为具体的结构体类型。
 * 用于需要显式访问头部字段或进行类型转换的场合。
 * 
 * 使用场景：
 * - 遍历GC对象链表时的类型转换
 * - 访问任意GC对象的公共字段
 * - 调试和内存分析工具
 */
typedef struct GCheader
{
    CommonHeader;
} GCheader;

/**
 * [值系统] Lua值的联合类型
 * 
 * 这是TValue结构中实际存储数据的部分。
 * 所有Lua值的数据都存储在这个联合体中。
 * 
 * 设计理念：
 * - 所有Lua值都可以用这个联合表示
 * - 大小固定，便于栈操作和数组存储
 * - 通过类型标记区分具体类型
 * 
 * 字段说明：
 * @field gc - GCObject*：指向可收集对象(string, table, function等)
 * @field p - void*：轻量用户数据(light userdata)的指针
 * @field n - lua_Number：数值类型(通常是double)
 * @field b - int：布尔类型(0为false，非0为true)
 */
typedef union
{
    GCObject *gc;    // 指向可收集对象(string, table, function等)
    void *p;         // 轻量用户数据(light userdata)的指针
    lua_Number n;    // 数值类型
    int b;           // 布尔类型
} Value;

/**
 * [核心] 标记值(Tagged Values) - Lua值系统的核心
 * 
 * TValue是Lua中所有值的统一表示，实现了动态类型系统。
 * 每个TValue包含值数据和类型标记。
 * 
 * 组成部分：
 * @field value - Value：实际的值数据
 * @field tt - int：类型标记，指示value中哪个字段有效
 * 
 * 这种设计的优势：
 * 1. 类型安全：每个值都携带类型信息
 * 2. 统一接口：所有值都可以用相同方式处理
 * 3. 内存效率：固定大小，便于管理
 * 4. 性能优化：类型检查只需比较整数
 * 
 * 使用方式：
 * - 栈上的所有值都是TValue类型
 * - 表中的键值对都存储为TValue
 * - 全局变量和局部变量都用TValue表示
 */
#define TValuefields    Value value; int tt

/**
 * [核心] TValue结构体定义
 * 
 * 完整的Lua值表示，包含数据和类型信息。
 * 这是Lua虚拟机中最基础和最重要的数据结构。
 */
typedef struct lua_TValue
{
    TValuefields;
} TValue;

/**
 * [性能优化] 类型检测宏
 * 
 * 这些宏提供了高效的类型检查，只需要比较类型标记。
 * 编译器通常会将这些优化为单个比较指令。
 * 
 * 性能特点：
 * - O(1)时间复杂度
 * - 无函数调用开销
 * - 支持编译器内联优化
 */
#define ttisnil(o)          (ttype(o) == LUA_TNIL)           // 是否为nil
#define ttisnumber(o)       (ttype(o) == LUA_TNUMBER)       // 是否为数值
#define ttisstring(o)       (ttype(o) == LUA_TSTRING)       // 是否为字符串
#define ttistable(o)        (ttype(o) == LUA_TTABLE)        // 是否为表
#define ttisfunction(o)     (ttype(o) == LUA_TFUNCTION)     // 是否为函数
#define ttisboolean(o)      (ttype(o) == LUA_TBOOLEAN)      // 是否为布尔值
#define ttisuserdata(o)     (ttype(o) == LUA_TUSERDATA)     // 是否为用户数据
#define ttisthread(o)       (ttype(o) == LUA_TTHREAD)       // 是否为线程
#define ttislightuserdata(o) (ttype(o) == LUA_TLIGHTUSERDATA) // 是否为轻量用户数据

/**
 * [安全访问] 值访问宏
 * 
 * 这些宏提供类型安全的值提取，使用check_exp确保类型正确。
 * 在调试版本中会进行类型检查，在发布版本中优化为直接访问。
 */
#define ttype(o)    ((o)->tt)  // 获取类型标记

// 获取可收集对象指针，带类型检查
#define gcvalue(o)  check_exp(iscollectable(o), (o)->value.gc)

// 获取轻量用户数据指针
#define pvalue(o)   check_exp(ttislightuserdata(o), (o)->value.p)

// 获取数值
#define nvalue(o)   check_exp(ttisnumber(o), (o)->value.n)

// 获取布尔值
#define bvalue(o)   check_exp(ttisboolean(o), (o)->value.b)

/**
 * [字符串访问] 字符串对象访问宏
 * 
 * Lua字符串系统的核心访问接口，提供从TValue到字符串对象的转换。
 * 支持原始访问和标准访问两种模式。
 */
// 获取字符串对象(原始形式) - 直接访问GCObject中的TString
#define rawtsvalue(o)   check_exp(ttisstring(o), &(o)->value.gc->ts)

// 获取字符串对象(标准形式) - 访问TString中的实际数据
#define tsvalue(o)      (&rawtsvalue(o)->tsv)

/**
 * [用户数据] 用户数据访问宏
 * 
 * 提供对Lua中用户数据对象的安全访问。
 * 用户数据是C程序向Lua传递复杂数据结构的主要方式。
 */
// 获取用户数据对象(原始形式)
#define rawuvalue(o)    check_exp(ttisuserdata(o), &(o)->value.gc->u)

// 获取用户数据对象(标准形式)  
#define uvalue(o)       (&rawuvalue(o)->uv)

/**
 * [对象访问] 其他对象类型访问宏
 * 
 * 为各种Lua对象类型提供类型安全的访问接口。
 */
// 获取闭包对象
#define clvalue(o)      check_exp(ttisfunction(o), &(o)->value.gc->cl)

// 获取表对象  
#define hvalue(o)       check_exp(ttistable(o), &(o)->value.gc->h)

// 获取线程对象
#define thvalue(o)      check_exp(ttisthread(o), &(o)->value.gc->th)

/**
 * [逻辑运算] Lua真假值判断系统
 * 
 * Lua的假值定义与大多数编程语言不同：
 * - 假值：只有nil和false
 * - 真值：除nil和false之外的所有值(包括0、空字符串、空表)
 * 
 * 这种设计体现了Lua"简单即是美"的哲学。
 * 
 * 与其他语言对比：
 * - C/C++：0为假，非0为真
 * - JavaScript：null, undefined, false, 0, "", NaN为假
 * - Python：None, False, 0, [], {}, ""为假
 * - Lua：只有nil和false为假
 * 
 * @param o TValue* 要检查的Lua值
 * @return int 0表示真值，非0表示假值
 */
#define l_isfalse(o)    (ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

/**
 * [调试工具] 对象一致性检查
 * 
 * 验证TValue对象的类型标记与其GC对象头部的类型标记是否一致。
 * 这是一个重要的内部一致性检查，帮助发现类型系统的bug。
 * 
 * 检查逻辑：
 * 1. 如果对象不可收集(如数值、布尔值)，直接通过检查
 * 2. 如果对象可收集，其tt字段必须与gc->gch.tt字段匹配
 * 
 * 这个检查在调试版本中非常重要，可以及早发现：
 * - 类型标记不匹配的bug
 * - 内存损坏导致的数据不一致
 * - GC过程中的类型错误
 * 
 * @param obj TValue* 要检查的对象
 */
#define checkconsistency(obj) \
    lua_assert(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

/**
 * [GC安全] 对象存活性检查
 * 
 * 在一致性检查基础上，增加垃圾收集器的存活性验证。
 * 确保对象不仅类型一致，而且未被GC标记为死亡状态。
 * 
 * 这个检查对于GC安全性至关重要：
 * - 防止访问已死亡的对象
 * - 确保对象在GC周期中的正确状态
 * - 帮助调试GC相关的bug
 * 
 * @param g global_State* 全局状态，包含GC信息
 * @param obj TValue* 要检查的对象
 */
#define checkliveness(g,obj) \
    lua_assert(!iscollectable(obj) || \
        ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))

/**
 * [核心操作] TValue设置宏系统
 * 
 * 提供类型安全的TValue设置操作。这些宏确保：
 * 1. 值和类型标记的原子性设置
 * 2. 避免宏参数的副作用(使用临时变量)
 * 3. 可收集对象的存活性检查
 * 4. 编译器优化友好的实现
 * 
 * 设计模式：
 * - 简单类型(nil, number, boolean)：直接设置
 * - 可收集类型(string, table等)：设置值+类型+存活性检查
 */

// [基础类型] 设置为nil值 - 最简单的设置操作
#define setnilvalue(obj) ((obj)->tt=LUA_TNIL)

// [数值类型] 设置数值，使用临时变量避免副作用
#define setnvalue(obj,x) \
    { TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER; }

// [轻量数据] 设置轻量用户数据指针
#define setpvalue(obj,x) \
    { TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA; }

// [布尔类型] 设置布尔值
#define setbvalue(obj,x) \
    { TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN; }

// [字符串] 设置字符串值，包含GC存活性检查
#define setsvalue(L,obj,x) \
    { TValue *i_o=(obj); \
      i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TSTRING; \
      checkliveness(G(L),i_o); }

// [用户数据] 设置用户数据值，包含GC存活性检查  
#define setuvalue(L,obj,x) \
    { TValue *i_o=(obj); \
      i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TUSERDATA; \
      checkliveness(G(L),i_o); }

// [线程] 设置线程值，包含GC存活性检查
#define setthvalue(L,obj,x) \
    { TValue *i_o=(obj); \
      i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTHREAD; \
      checkliveness(G(L),i_o); }

// [函数] 设置函数值，包含GC存活性检查
#define setclvalue(L,obj,x) \
    { TValue *i_o=(obj); \
      i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TFUNCTION; \
      checkliveness(G(L),i_o); }

// [表] 设置表值，包含GC存活性检查
#define sethvalue(L,obj,x) \
    { TValue *i_o=(obj); \
      i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTABLE; \
      checkliveness(G(L),i_o); }

// [原型] 设置函数原型值(内部使用)，包含GC存活性检查
#define setptvalue(L,obj,x) \
    { TValue *i_o=(obj); \
      i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TPROTO; \
      checkliveness(G(L),i_o); }

/**
 * [对象复制] TValue完整复制操作
 * 
 * 将一个TValue的完整内容复制到另一个TValue中。
 * 这是Lua虚拟机中最基础的值传递操作。
 * 
 * 复制内容包括：
 * 1. value联合中的实际数据
 * 2. tt类型标记
 * 3. GC存活性验证(如果是可收集对象)
 * 
 * 使用const限定源对象，防止意外修改。
 * 使用临时变量避免宏参数的副作用。
 * 
 * @param L lua_State* Lua状态机，用于GC检查
 * @param obj1 TValue* 目标对象
 * @param obj2 const TValue* 源对象
 */
#define setobj(L,obj1,obj2) \
    { const TValue *o2=(obj2); TValue *o1=(obj1); \
      o1->value = o2->value; o1->tt=o2->tt; \
      checkliveness(G(L),o1); }

/**
 * [性能优化] 上下文相关的赋值操作宏
 * 
 * 根据赋值的源和目标位置，提供不同的赋值操作。
 * 目前这些宏都映射到相同的实现，但为将来的优化预留了空间。
 * 
 * 潜在优化方向：
 * 1. 写屏障(Write Barriers)：分代GC优化
 * 2. 栈缓存：栈到栈传输的特殊优化
 * 3. 表优化：表内操作的专门处理
 * 4. 内存局部性：针对不同内存区域的优化
 * 
 * 分类：
 * - s2s: stack to stack (栈到栈)
 * - 2s: to stack (到栈)
 * - t2t: table to table (表到表)
 * - 2t: to table (到表)
 * - 2n: to new object (到新对象)
 */

// [栈操作] 从栈到(同一)栈的赋值
#define setobjs2s   setobj

// [栈操作] 到栈的赋值(不是从同一栈)
#define setobj2s    setobj
#define setsvalue2s setsvalue
#define sethvalue2s sethvalue
#define setptvalue2s setptvalue

// [表操作] 从表到同一表的赋值
#define setobjt2t   setobj

// [表操作] 到表的赋值
#define setobj2t    setobj

// [内存管理] 到新对象的赋值
#define setobj2n    setobj
#define setsvalue2n setsvalue

/**
 * [危险操作] 直接类型标记设置
 * 
 * 警告：这是一个底层操作，仅供内部使用！
 * 直接修改类型标记而不更新值，可能导致类型不一致。
 * 
 * 使用场景：
 * - 性能关键路径的优化
 * - 特殊的类型转换操作
 * - 调试和测试代码
 * 
 * @param obj TValue* 目标对象
 * @param tt int 新的类型标记
 */
#define setttype(obj, tt) (ttype(obj) = (tt))

/**
 * [GC系统] 可收集性判断
 * 
 * 判断一个Lua值是否需要垃圾收集管理。
 * Lua的值类型分为两类：
 * 
 * 不可收集类型(类型标记 < LUA_TSTRING)：
 * - LUA_TNIL (nil)
 * - LUA_TBOOLEAN (boolean)  
 * - LUA_TLIGHTUSERDATA (light userdata)
 * - LUA_TNUMBER (number)
 * 
 * 可收集类型(类型标记 >= LUA_TSTRING)：
 * - LUA_TSTRING (string)
 * - LUA_TTABLE (table)
 * - LUA_TFUNCTION (function)
 * - LUA_TUSERDATA (userdata)
 * - LUA_TTHREAD (thread)
 * - LUA_TPROTO (function prototype, 内部类型)
 * 
 * 这种分类基于类型标记的数值范围，实现了O(1)的判断效率。
 * 
 * @param o TValue* 要检查的值
 * @return int 非0表示可收集，0表示不可收集
 */
#define iscollectable(o)    (ttype(o) >= LUA_TSTRING)

/**
 * [类型别名] 栈索引类型
 * 
 * 指向栈元素的指针，用于所有栈操作。
 * 这是Lua虚拟机栈管理的基础类型。
 */
typedef TValue *StkId;

/**
 * [字符串系统] 字符串对象定义
 * 
 * Lua中的字符串是不可变的，并且被内部化(interned)。
 * 这种设计实现了高效的字符串管理和比较。
 *
 * 设计特点：
 * 1. 不可变性：字符串一旦创建就不能修改
 * 2. 内部化：相同内容的字符串只存储一份
 * 3. 哈希缓存：预计算哈希值，加速表查找
 * 4. 长度缓存：存储长度，避免重复计算
 * 
 * 内存布局：
 * - TString结构体
 * - 字符串内容（紧跟在结构体后面）
 * - null终结符
 */
typedef union TString
{
    L_Umaxalign dummy;  // 确保字符串的最大对齐
    struct
    {
        CommonHeader;        // GC公共头部
        lu_byte reserved;    // 保留字段，用于标记保留字符串
        unsigned int hash;   // 字符串的哈希值
        size_t len;         // 字符串长度(不包括结尾的\0)
    } tsv;
} TString;

/**
 * [字符串访问] 字符串内容访问宏
 * 
 * 字符串内容紧跟在TString结构之后存储。
 * 这些宏提供了高效的字符串内容访问方式。
 */
#define getstr(ts)  cast(const char *, (ts) + 1)  // 获取字符串内容指针
#define svalue(o)   getstr(rawtsvalue(o))         // 从TValue中获取字符串内容

/**
 * [用户数据] 用户数据对象定义
 * 
 * 用户数据是Lua中存储C数据的核心机制，提供了C和Lua之间的桥梁。
 * 它允许C程序将复杂的数据结构安全地传递给Lua，并支持完整的面向对象特性。
 *
 * 核心特点：
 * 1. 元表支持：可以定义元方法，实现操作符重载和面向对象特性
 * 2. 环境表：每个用户数据可以有独立的全局环境
 * 3. 任意数据：可以存储任意大小的C数据结构
 * 4. GC管理：自动垃圾收集，支持finalizer(__gc元方法)
 * 5. 类型安全：通过元表实现运行时类型检查
 * 
 * 内存布局：
 * - Udata结构体头部
 * - 用户数据内容(紧跟在结构体后面)
 * 
 * 使用场景：
 * - 文件句柄、网络连接等系统资源
 * - 复杂的C数据结构(图形对象、数据库连接等)
 * - 需要特殊生命周期管理的对象
 * 
 * @field metatable Table* 元表，定义对象的操作行为和类型信息
 * @field env Table* 环境表，用户数据的全局环境
 * @field len size_t 用户数据的大小(字节数)
 */
typedef union Udata 
{
    L_Umaxalign dummy;  // 确保用户数据的最大对齐
    struct 
    {
        CommonHeader;           // GC公共头部
        struct Table *metatable; // 元表，定义操作行为
        struct Table *env;       // 环境表
        size_t len;             // 用户数据的大小
    } uv;
} Udata;

/**
 * [编译器] 函数原型定义
 * 
 * Proto是Lua函数的编译结果，包含了执行函数所需的全部信息。
 * 它是Lua编译器输出和虚拟机执行之间的桥梁。
 *
 * 核心概念：
 * 1. 字节码容器：存储编译后的虚拟机指令
 * 2. 常量池：函数中使用的所有字面量常量
 * 3. 调试信息：源码位置、变量名等调试数据
 * 4. 嵌套结构：支持闭包和嵌套函数的原型引用
 * 5. 上值管理：闭包变量的名称和绑定信息
 * 
 * 编译到执行的流程：
 * Lua源码 → 语法分析 → Proto对象 → 虚拟机执行
 * 
 * 内存优化：
 * - 多个闭包可以共享同一个Proto
 * - 常量表去重，减少内存占用
 * - 指令紧凑编码，提高缓存效率
 * 
 * @field k TValue* 常量数组，存储函数使用的字面量
 * @field code Instruction* 字节码指令数组，虚拟机执行的指令序列
 * @field p Proto** 嵌套函数原型数组，支持函数内部定义函数
 * @field lineinfo int* 指令到源码行号的映射，用于调试和错误报告
 * @field locvars LocVar* 局部变量信息数组，变量名、作用域等
 * @field upvalues TString** 上值名称数组，闭包变量的名称
 * @field source TString* 源码文件名，用于错误报告和调试
 * @field sizeupvalues int 上值数组大小
 * @field sizek int 常量数组大小  
 * @field sizecode int 指令数组大小
 * @field sizelineinfo int 行号信息数组大小
 */
typedef struct Proto 
{
    CommonHeader;                // GC公共头部
    TValue *k;                  // 常量数组，函数使用的常量值
    Instruction *code;          // 字节码指令数组
    struct Proto **p;           // 嵌套函数原型数组
    int *lineinfo;             // 指令到源码行号的映射
    struct LocVar *locvars;     // 局部变量信息数组
    TString **upvalues;         // 上值名称数组
    TString  *source;           // 源码文件名
    int sizeupvalues;          // 上值数组大小
    int sizek;                 // 常量数组大小
    int sizecode;              // 指令数组大小
    int sizelineinfo;          // 行号信息数组大小
    int sizep;                 // 嵌套函数数组大小
    int sizelocvars;           // 局部变量信息数组大小
    int linedefined;           // 函数定义开始行号
    int lastlinedefined;       // 函数定义结束行号
    GCObject *gclist;          // GC链表指针
    lu_byte nups;              // 上值数量
    lu_byte numparams;         // 参数数量
    lu_byte is_vararg;         // 可变参数标志
    lu_byte maxstacksize;      // 函数需要的最大栈大小
} Proto;

/**
 * [可变参数] 可变参数控制掩码
 * 
 * 用于Proto.is_vararg字段，控制函数的可变参数行为。
 * Lua支持灵活的参数系统，这些标志提供了精确的控制。
 * 
 * 设计目的：
 * 1. 区分固定参数和可变参数函数
 * 2. 向后兼容旧版本的arg表机制
 * 3. 优化参数传递和栈管理
 * 
 * 使用方式：通过位运算组合多个标志
 */
#define VARARG_HASARG       1  // 函数有固定参数
#define VARARG_ISVARARG     2  // 函数接受可变参数(...语法)
#define VARARG_NEEDSARG     4  // 需要arg表(向后兼容旧版本)

/**
 * [调试信息] 局部变量信息结构
 * 
 * 记录局部变量的生命周期和作用域信息，主要用于：
 * 1. 调试器支持：显示变量名和值
 * 2. 错误报告：提供更精确的错误位置
 * 3. 反射功能：运行时获取变量信息
 * 4. 代码分析：静态分析工具的支持
 * 
 * 作用域管理：
 * - startpc到endpc之间，变量在作用域内
 * - 超出范围时，变量不可访问
 * - 支持变量遮蔽(同名变量在不同作用域)
 * 
 * @field varname TString* 变量名字符串
 * @field startpc int 变量生效的第一个指令位置(inclusive)
 * @field endpc int 变量失效的第一个指令位置(exclusive)
 */
typedef struct LocVar 
{
    TString *varname;  // 变量名
    int startpc;       // 变量生效的第一个指令位置
    int endpc;         // 变量失效的第一个指令位置
} LocVar;

/**
 * [闭包系统] 上值(Upvalues)结构定义
 * 
 * 上值是Lua闭包机制的核心，实现了词法作用域的变量捕获。
 * 它允许内层函数访问外层函数的局部变量，即使外层函数已经返回。
 *
 * 设计理念和状态转换：
 * 1. 开放状态(Open)：变量仍在栈上，上值指向栈位置
 *    - v指向栈上的TValue
 *    - u.l维护双向链表，管理生命周期
 *    - 多个上值可以指向同一个栈位置
 * 
 * 2. 关闭状态(Closed)：变量离开作用域，值被复制
 *    - v指向自身的u.value字段
 *    - u.value存储变量的副本
 *    - 脱离栈的生命周期管理
 * 
 * 生命周期管理：
 * - 函数调用时：创建开放上值，链接到栈位置
 * - 变量离开作用域：关闭上值，复制值到上值对象
 * - 垃圾收集：清理不再使用的上值对象
 * 
 * 性能优化：
 * - 延迟关闭：只在必要时才关闭上值
 * - 共享上值：相同栈位置的多个引用共享同一个上值
 * - 双向链表：高效的插入和删除操作
 * 
 * @field v TValue* 值指针：开放时指向栈，关闭时指向自身value
 * @field u 联合体：根据状态存储不同内容
 *   - value TValue：关闭状态时存储值的副本
 *   - l 结构体：开放状态时的链表节点
 *     - prev UpVal*：链表前驱节点
 *     - next UpVal*：链表后继节点
 */
typedef struct UpVal 
{
    CommonHeader;      // GC公共头部
    TValue *v;         // 指向栈或自身的value字段
    union 
    {
        TValue value;    // 值(当关闭时)
        struct 
        {         // 双向链表(当开放时)
            struct UpVal *prev;  // 前一个上值
            struct UpVal *next;  // 后一个上值
        } l;
    } u;
} UpVal;

/**
 * [函数系统] 闭包(Closures)定义
 * 
 * 在Lua中，函数实际上是闭包对象。闭包 = 函数 + 环境 + 上值。
 * 这种设计统一了函数的表示，支持强大的函数式编程特性。
 *
 * 核心概念：
 * 1. 一等公民：函数可以作为值传递、存储、返回
 * 2. 词法作用域：内层函数可以访问外层函数的变量
 * 3. 环境独立：每个闭包可以有独立的全局环境
 * 4. 上值捕获：自动管理外部变量的生命周期
 * 
 * 两种实现类型：
 * 1. C闭包(CClosure)：包装原生C函数，高性能调用
 * 2. Lua闭包(LClosure)：包装Lua字节码，支持完整语言特性
 * 
 * 性能特点：
 * - C闭包：直接函数指针调用，接近原生性能
 * - Lua闭包：虚拟机解释执行，但支持所有Lua特性
 * - 上值共享：多个闭包可以共享相同的上值对象
 */

/**
 * [闭包基础] 闭包公共头部宏
 * 
 * 所有类型的闭包都包含这些基础字段。
 * 使用宏定义确保结构一致性和维护便利性。
 * 
 * @field CommonHeader GC公共头部，垃圾收集管理
 * @field isC lu_byte 类型标记：0=Lua闭包，1=C闭包
 * @field nupvalues lu_byte 上值数量，决定上值数组大小
 * @field gclist GCObject* GC链表指针，垃圾收集管理
 * @field env Table* 环境表，闭包的全局变量环境
 */
#define ClosureHeader \
    CommonHeader; lu_byte isC; lu_byte nupvalues; GCObject *gclist; \
    struct Table *env

/**
 * [C集成] C闭包结构定义
 * 
 * C闭包是Lua与C代码集成的核心机制，允许C函数参与Lua的闭包系统。
 * 
 * 特点：
 * 1. 高性能：直接调用C函数指针，无虚拟机开销
 * 2. 上值支持：C函数可以访问捕获的Lua值
 * 3. 环境隔离：每个C闭包有独立的环境表
 * 4. 类型安全：通过lua_CFunction标准接口调用
 * 
 * 使用场景：
 * - 库函数：数学、字符串、I/O等标准库
 * - 回调函数：事件处理、钩子函数
 * - 性能关键：需要高性能的计算密集型函数
 * - 系统接口：操作系统、网络、文件系统调用
 * 
 * 上值机制：
 * - 上值是完整的TValue对象，不是UpVal指针
 * - 支持所有Lua类型：number, string, table, function等
 * - 数组长度由nupvalues字段确定
 * 
 * @field f lua_CFunction C函数指针，实际执行的函数
 * @field upvalue TValue[] 上值数组，可变长度结构
 */
typedef struct CClosure 
{
    ClosureHeader;           // 闭包公共头部
    lua_CFunction f;         // C函数指针
    TValue upvalue[1];       // 上值数组(可变长度)
} CClosure;

/**
 * [Lua核心] Lua闭包结构定义
 * 
 * Lua闭包是纯Lua函数的运行时表示，支持完整的词法作用域和上值捕获。
 * 这是Lua函数式编程能力的核心实现。
 * 
 * 特点：
 * 1. 字节码执行：通过虚拟机解释执行函数原型中的指令
 * 2. 完整上值：支持复杂的变量捕获和生命周期管理
 * 3. 嵌套支持：支持任意深度的函数嵌套和闭包创建
 * 4. 动态特性：支持运行时函数创建和修改
 * 
 * 与C闭包的区别：
 * - 执行方式：虚拟机解释 vs 直接调用
 * - 上值类型：UpVal指针 vs TValue对象
 * - 功能完整性：支持所有Lua语言特性
 * - 性能特点：灵活但相对较慢
 * 
 * 上值管理：
 * - 存储UpVal指针，支持开放/关闭状态转换
 * - 多个闭包可以共享相同的UpVal对象
 * - 自动管理变量的生命周期和作用域
 * 
 * 应用场景：
 * - 用户定义函数：所有用户编写的Lua函数
 * - 回调和事件处理：需要捕获上下文的回调函数
 * - 迭代器：for...in循环中的迭代器函数
 * - 函数式编程：高阶函数、柯里化等
 * 
 * @field p Proto* 函数原型，包含字节码和调试信息
 * @field upvals UpVal*[] 上值指针数组，可变长度结构
 */
typedef struct LClosure 
{
    ClosureHeader;           // 闭包公共头部
    struct Proto *p;         // 函数原型
    UpVal *upvals[1];        // 上值指针数组(可变长度)
} LClosure;

/**
 * [类型统一] 闭包联合类型
 * 
 * 统一表示C闭包和Lua闭包，实现多态性。
 * 通过isC字段区分具体类型，提供统一的接口。
 * 
 * 设计优势：
 * 1. 类型统一：所有闭包都可以用Closure*表示
 * 2. 多态支持：运行时根据isC字段选择处理方式
 * 3. 内存效率：联合类型节省内存空间
 * 4. 接口简化：上层代码无需区分闭包类型
 * 
 * @field c CClosure C闭包成员
 * @field l LClosure Lua闭包成员
 */
typedef union Closure 
{
    CClosure c;  // C闭包
    LClosure l;  // Lua闭包
} Closure;

/**
 * [类型检查] 闭包类型判断宏
 * 
 * 提供快速的闭包类型检查，用于运行时分派。
 * 
 * 实现原理：
 * 1. 首先检查是否为函数类型
 * 2. 然后检查isC字段判断具体类型
 * 
 * @param o TValue* 要检查的值
 * @return int 非0表示匹配，0表示不匹配
 */
#define iscfunction(o)  (ttype(o) == LUA_TFUNCTION && clvalue(o)->c.isC)    // 是否为C函数
#define isLfunction(o)  (ttype(o) == LUA_TFUNCTION && !clvalue(o)->c.isC)   // 是否为Lua函数

/**
 * [表系统] Lua表 - 通用数据结构
 * 
 * 表是Lua中唯一的数据结构化类型，但功能极其强大。
 * 它统一了数组、哈希表、对象、模块等概念。
 *
 * 核心设计理念：
 * 1. 统一性：一种数据结构解决所有结构化数据需求
 * 2. 高效性：针对常见使用模式的特殊优化
 * 3. 灵活性：支持任意类型的键和值
 * 4. 扩展性：通过元表机制实现面向对象和操作符重载
 * 
 * 混合实现策略：
 * 1. 数组部分(array)：
 *    - 存储连续整数键(1, 2, 3, ...)
 *    - O(1)访问时间，内存紧凑
 *    - 专门优化数组和列表操作
 * 
 * 2. 哈希部分(node)：
 *    - 存储其他类型键(字符串、浮点数、对象等)
 *    - 开放地址法+链表法混合冲突解决
 *    - 支持复杂的键类型和关联数组
 * 
 * 性能优化：
 * - 自动调整：根据使用模式动态调整数组/哈希部分大小
 * - 元方法缓存：flags字段缓存元方法存在性，避免重复查找
 * - 内存局部性：相关数据紧密排列，提高缓存效率
 * 
 * 应用场景：
 * - 数组：{1, 2, 3, 4}
 * - 哈希表：{name="John", age=30}
 * - 对象：person.name, person:speak()
 * - 模块：math.sin, string.find
 */

/**
 * [键系统] 表键的联合类型定义
 * 
 * 表的键可以是任何Lua值(除了nil)，这个联合类型实现了
 * 高效的键存储和冲突解决机制。
 * 
 * 设计特点：
 * 1. 完整值存储：键包含完整的TValue信息
 * 2. 链表指针：支持哈希冲突的链式解决
 * 3. 双重访问：可以作为TValue或结构体访问
 * 4. 内存对齐：确保高效的内存访问
 * 
 * 冲突解决：
 * - 开放地址法为主：直接在哈希表中寻找空位
 * - 链表法为辅：next指针形成冲突链
 * - 混合策略：平衡空间效率和时间效率
 * 
 * @field nk 结构体：标准的键结构
 *   - TValuefields：键的值和类型信息
 *   - next Node*：冲突链表的下一个节点
 * @field tvk TValue：将键作为普通TValue访问
 */
typedef union TKey 
{
    struct 
    {
        TValuefields;          // 键值和类型
        struct Node *next;     // 冲突链表的下一个节点
    } nk;
    TValue tvk;             // 作为TValue访问
} TKey;

/**
 * [存储单元] 哈希表节点定义
 * 
 * 表的哈希部分由节点数组构成，每个节点存储一个键值对。
 * 这是表存储的基本单元。
 * 
 * 设计原则：
 * 1. 紧凑存储：键值紧密排列，提高缓存效率
 * 2. 直接访问：避免间接指针，减少内存访问
 * 3. 统一布局：所有节点具有相同的内存布局
 * 
 * 内存布局优化：
 * - 值在前：常见的值访问优先，提高缓存命中
 * - 键在后：键的比较相对较少
 * - 对齐考虑：确保最佳的内存访问性能
 * 
 * @field i_val TValue 存储的值
 * @field i_key TKey 存储的键
 */
typedef struct Node 
{
    TValue i_val;  // 值
    TKey i_key;    // 键
} Node;

/**
 * [核心结构] Lua表完整定义
 * 
 * 这是Lua语言最重要的数据结构，实现了高效的混合数组/哈希表。
 * 表的设计直接影响Lua程序的性能和内存使用。
 *
 * 架构设计：
 * 1. 双重存储：数组部分 + 哈希部分，针对不同访问模式优化
 * 2. 动态调整：根据使用模式自动调整两部分的大小比例
 * 3. 元表支持：通过元表实现继承、操作符重载等高级特性
 * 4. GC集成：与垃圾收集器紧密集成，支持弱引用等特性
 * 
 * 性能优化机制：
 * 1. 元方法缓存(flags)：
 *    - 每个bit对应一个元方法
 *    - 1表示该元方法不存在，避免重复查找
 *    - 大大提高元方法调用的性能
 * 
 * 2. 数组部分优化：
 *    - 连续整数键使用数组存储
 *    - 直接索引访问，O(1)时间复杂度
 *    - 内存紧凑，缓存友好
 * 
 * 3. 哈希部分优化：
 *    - lsizenode存储log2(size)，避免除法运算
 *    - lastfree指针优化空间分配
 *    - 开放地址+链表混合冲突解决
 * 
 * 内存管理：
 * - 数组和哈希部分可以独立调整大小
 * - 支持表的增长和收缩
 * - 与GC系统集成，支持增量收集
 * 
 * 元表机制：
 * - 每个表可以有一个元表
 * - 元表定义表的行为(运算、索引、调用等)
 * - 支持面向对象编程和操作符重载
 * 
 * @field flags lu_byte 元方法存在性标志，用于性能优化
 * @field lsizenode lu_byte 哈希部分大小的log2值(size = 2^lsizenode)
 * @field metatable Table* 元表指针，定义表的行为
 * @field array TValue* 数组部分，存储连续整数键
 * @field node Node* 哈希部分，存储其他类型键
 * @field lastfree Node* 最后一个空闲位置指针，优化分配
 * @field gclist GCObject* GC链表指针
 * @field sizearray int 数组部分的大小
 */
typedef struct Table 
{
    CommonHeader;              // GC公共头部
    lu_byte flags;             // 1<<p表示元方法p不存在，用于优化
    lu_byte lsizenode;         // node数组大小的log2值
    struct Table *metatable;   // 元表
    TValue *array;             // 数组部分
    Node *node;                // 哈希部分
    Node *lastfree;            // 最后一个空闲位置之前的任何位置都是空闲的
    GCObject *gclist;          // GC链表指针
    int sizearray;             // 数组部分的大小
} Table;

/**
 * [哈希优化] 表相关的工具宏定义
 * 
 * 这些宏提供了高效的哈希表操作，充分利用了表大小总是2的幂这一特性。
 */

/**
 * [位运算优化] 模运算宏
 * 
 * 利用位运算优化模运算，这是哈希表性能的关键优化。
 * 
 * 数学原理：
 * 当size是2的幂时：x % size == x & (size-1)
 * 
 * 性能提升：
 * - 位运算比除法快数倍
 * - 现代CPU的位运算是单周期指令
 * - 避免了除法器的使用
 * 
 * 安全检查：
 * check_exp确保size确实是2的幂，防止错误使用
 * 
 * @param s 被模数
 * @param size 模数(必须是2的幂)
 * @return 模运算结果
 */
#define lmod(s,size) \
    (check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))

/**
 * [数学工具] 2的幂计算宏
 * 
 * 提供快速的2的幂计算，用于表大小管理。
 * 
 * 应用场景：
 * - 计算哈希表的实际大小
 * - 表的扩容和缩容操作
 * - 内存分配大小计算
 */
#define twoto(x)        (1<<(x))           // 计算2^x
#define sizenode(t)     (twoto((t)->lsizenode))  // 计算哈希表大小

/**
 * [全局常量] 共享nil对象
 * 
 * 所有nil值都指向这个唯一的对象，实现了nil值的共享。
 * 这种设计节省内存并简化了nil值的比较。
 * 
 * 优势：
 * 1. 内存节省：所有nil引用共享一个对象
 * 2. 比较优化：可以用指针比较代替值比较
 * 3. 初始化简化：默认初始化为nil很容易
 */
#define luaO_nilobject      (&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

/**
 * [算法工具] 向上取整的log2计算
 * 
 * 计算容纳x个元素需要的最小2的幂。
 * 这是表容量计算的核心算法。
 * 
 * 算法原理：
 * ceillog2(x) = floor(log2(x-1)) + 1
 * 
 * 应用：
 * - 确定哈希表的合适大小
 * - 内存分配的大小对齐
 * - 容量规划和性能优化
 * 
 * @param x 目标容量
 * @return 最小的满足2^result >= x的result值
 */
#define ceillog2(x) (luaO_log2((x)-1) + 1)

/**
 * [API声明] 对象操作函数接口
 * 
 * 这些函数提供了Lua对象的核心操作和转换功能。
 * 它们是Lua内部实现的重要组成部分。
 */

/**
 * [数学工具] 计算整数的log2值
 * 
 * 计算floor(log2(x))，即x的最高位的位置。
 * 这是表容量计算和位操作的基础函数。
 * 
 * 算法特点：
 * - 高效的位扫描算法
 * - 处理边界情况(x=0)
 * - 支持各种整数大小
 * 
 * @param x unsigned int 输入的无符号整数
 * @return int floor(log2(x))，x=0时返回-1
 */
LUAI_FUNC int luaO_log2 (unsigned int x);

/**
 * [压缩存储] 整数到浮点字节转换
 * 
 * 将整数转换为紧凑的"浮点字节"格式，用于节省存储空间。
 * 
 * 应用场景：
 * - 表的大小信息存储
 * - 大整数的紧凑表示
 * - 内存使用优化
 * 
 * 格式特点：
 * - 类似IEEE浮点数的指数+尾数结构
 * - 牺牲精度换取存储空间
 * - 适合存储大致的大小信息
 * 
 * @param x unsigned int 要转换的整数
 * @return int 浮点字节格式的紧凑表示
 */
LUAI_FUNC int luaO_int2fb (unsigned int x);

/**
 * [压缩存储] 浮点字节到整数转换
 * 
 * luaO_int2fb的逆操作，将浮点字节格式转换回整数。
 * 
 * @param x int 浮点字节格式的值
 * @return int 对应的整数值(可能有精度损失)
 */
LUAI_FUNC int luaO_fb2int (int x);

/**
 * [比较操作] 原始对象相等比较
 * 
 * 执行两个Lua值的原始相等比较，不触发元方法。
 * 这是表键查找和内部比较的基础操作。
 * 
 * 比较规则：
 * 1. 类型不同 → 不相等
 * 2. 基础类型 → 值比较
 * 3. 对象类型 → 指针比较(同一对象)
 * 4. 字符串 → 指针比较(内部化保证)
 * 
 * 注意事项：
 * - 不调用__eq元方法
 * - 字符串比较依赖内部化
 * - NaN与任何值都不相等(包括自身)
 * 
 * @param t1 const TValue* 第一个值
 * @param t2 const TValue* 第二个值
 * @return int 相等返回1，不相等返回0
 */
LUAI_FUNC int luaO_rawequalObj (const TValue *t1, const TValue *t2);

/**
 * [类型转换] 字符串到数值转换
 * 
 * 将字符串转换为Lua数值类型，支持多种数值格式。
 * 
 * 支持格式：
 * - 十进制：123, 123.45, 1.23e10
 * - 十六进制：0x1A, 0X1a
 * - 科学记数法：1e10, 1.5E-3
 * - 符号处理：+123, -456
 * 
 * 错误处理：
 * - 格式错误返回0
 * - 溢出处理依赖平台
 * - 空字符串和空白字符串处理
 * 
 * @param s const char* 要转换的字符串
 * @param result lua_Number* 存储结果的指针
 * @return int 转换成功返回1，失败返回0
 */
LUAI_FUNC int luaO_str2d (const char *s, lua_Number *result);

/**
 * [字符串格式化] 格式化字符串并推入栈(va_list版本)
 * 
 * 类似sprintf的功能，但将结果推入Lua栈作为字符串对象。
 * 支持Lua特定的格式说明符。
 * 
 * Lua特有格式：
 * - %s: Lua字符串
 * - %p: 指针
 * - %d, %f, %c: 标准格式
 * - %%: 字面量%
 * 
 * 内存管理：
 * - 自动管理临时缓冲区
 * - 结果作为GC对象管理
 * - 异常安全的内存分配
 * 
 * @param L lua_State* Lua状态机
 * @param fmt const char* 格式字符串
 * @param argp va_list 参数列表
 * @return const char* 格式化后的字符串指针
 */
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);

/**
 * [字符串格式化] 格式化字符串并推入栈(可变参数版本)
 * 
 * luaO_pushvfstring的便利包装，使用可变参数。
 * 
 * @param L lua_State* Lua状态机  
 * @param fmt const char* 格式字符串
 * @param ... 可变参数列表
 * @return const char* 格式化后的字符串指针
 */
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);

/**
 * [错误报告] 生成代码块标识符
 * 
 * 为错误消息生成简短且有意义的源码标识符。
 * 处理长文件名的截断和特殊源码的格式化。
 * 
 * 处理策略：
 * - 短名称：直接使用
 * - 长文件名：智能截断，保留重要部分
 * - 特殊源码：标准化格式(如[string "..."])
 * - 临时代码：使用描述性标识
 * 
 * 应用场景：
 * - 错误消息中的源码定位
 * - 调试信息的源码标识
 * - 栈回溯的可读性提升
 * 
 * @param out char* 输出缓冲区
 * @param source const char* 源码标识符
 * @param len size_t 输出缓冲区长度
 */
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);

//#endif
