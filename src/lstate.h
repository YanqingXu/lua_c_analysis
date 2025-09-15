/**
 * @file lstate.h
 * @brief Lua状态管理器接口：实现虚拟机状态的完整生命周期管理
 * 
 * 详细说明：
 * 这个头文件定义了Lua虚拟机状态管理的核心数据结构和接口，包括全局状态
 * 和线程状态的完整定义。Lua的状态管理采用分层设计：全局状态在所有线程
 * 间共享，包含垃圾收集器、字符串表、元表等系统级资源；线程状态则是每个
 * 协程独有的，包含调用栈、局部变量、执行状态等。这种设计既支持了高效的
 * 协程实现，又确保了系统资源的合理共享。
 * 
 * 系统架构定位：
 * - 位于Lua虚拟机的核心层，是所有组件的基础依赖
 * - 与垃圾收集器(lgc)紧密集成，提供GC所需的对象管理
 * - 与调试系统(ldebug)协作，支持调试钩子和错误处理
 * - 与协程系统(ldo)配合，管理多个执行上下文
 * 
 * 技术特点：
 * - 分层状态管理：全局状态和线程状态的清晰分离
 * - 高效调用栈：基于数组的调用信息管理
 * - 灵活内存管理：自定义内存分配器支持
 * - 协程支持：完整的协程状态管理和切换
 * - 错误处理：结构化的异常处理和恢复机制
 * - 调试集成：内置的调试钩子和性能监控
 * 
 * 依赖关系：
 * - lua.h: Lua公共接口，定义基础类型和常量
 * - lobject.h: Lua对象系统，提供值类型和对象定义
 * - ltm.h: 标签方法系统，定义元方法相关常量
 * - lzio.h: 缓冲I/O系统，提供内存缓冲区支持
 * 
 * 编译要求：
 * - C标准版本：C99或更高版本
 * - 联合体支持：用于GCObject的多态设计
 * - 结构体嵌套：支持复杂的嵌套数据结构
 * - 指针算术：用于栈管理和内存优化
 * 
 * 使用示例：
 * @code
 * #include "lstate.h"
 * #include "lua.h"
 * 
 * // 创建新的Lua状态
 * lua_State *L = luaL_newstate();
 * if (L == NULL) {
 *     fprintf(stderr, "无法创建Lua状态\n");
 *     return -1;
 * }
 * 
 * // 访问全局状态
 * global_State *g = G(L);
 * printf("当前分配字节数: %lu\n", g->totalbytes);
 * 
 * // 创建新线程
 * lua_State *L1 = luaE_newthread(L);
 * if (L1 != NULL) {
 *     // 新线程与主线程共享全局状态
 *     assert(G(L1) == G(L));
 * }
 * 
 * // 访问调用信息
 * CallInfo *ci = L->ci;
 * printf("当前函数栈大小: %d\n", 
 *        (int)(ci->top - ci->base));
 * 
 * // 清理资源
 * lua_close(L);  // 会自动清理所有关联的线程
 * @endcode
 * 
 * 状态管理架构：
 * - 全局状态：系统级资源的集中管理
 * - 线程状态：执行上下文的独立管理
 * - 调用栈：函数调用的高效管理
 * - 错误处理：结构化的异常恢复机制
 * 
 * 性能特征：
 * - 栈操作：O(1)的栈元素访问和修改
 * - 状态切换：高效的协程上下文切换
 * - 内存布局：缓存友好的数据结构设计
 * - 调用开销：最小化的函数调用成本
 * 
 * 并发模型：
 * - 协作式多任务：基于协程的并发模型
 * - 状态隔离：每个协程有独立的执行状态
 * - 资源共享：全局资源在协程间安全共享
 * 
 * 注意事项：
 * - 状态对象的生命周期管理需要特别小心
 * - 跨线程状态访问需要适当的同步机制
 * - 调用栈的大小限制需要在设计时考虑
 * - 错误处理机制依赖于setjmp/longjmp
 * 
 * @author Roberto Ierusalimschy
 * @version 2.24.1.2
 * @date 2008/01/03
 * @since Lua 5.1
 * @see lua.h, lobject.h, lgc.h
 */

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"

/* 前向声明：lua_longjmp结构在ldo.c中定义 */
struct lua_longjmp;

/**
 * @brief 获取全局表：访问线程的全局变量表
 * 
 * 这个宏提供了访问线程全局表的便利接口。全局表存储了该线程
 * 可见的所有全局变量，是变量解析的重要组成部分。
 * 
 * @param L lua_State指针
 * @return 指向全局表的TValue指针
 */
#define gt(L)    (&L->l_gt)

/**
 * @brief 获取注册表：访问Lua的注册表
 * 
 * 注册表是一个全局的、只能从C代码访问的表，用于存储需要在
 * C代码间共享但不应暴露给Lua代码的数据。
 * 
 * @param L lua_State指针
 * @return 指向注册表的TValue指针
 */
#define registry(L)    (&G(L)->l_registry)

/**
 * @brief 额外栈空间：处理元方法调用和其他操作的保留空间
 * 
 * 这个常量定义了栈顶之上需要保留的额外空间，用于：
 * - 元方法调用时的参数传递
 * - 错误处理时的临时存储
 * - 内部操作的缓冲区需求
 */
#define EXTRA_STACK   5

/**
 * @brief 基础调用信息大小：CallInfo数组的初始容量
 * 
 * 定义了CallInfo数组的初始大小，用于管理函数调用栈。
 * 当调用深度超过此值时，数组会自动扩展。
 */
#define BASIC_CI_SIZE           8

/**
 * @brief 基础栈大小：Lua栈的初始容量
 * 
 * 定义了Lua值栈的初始大小，必须至少是LUA_MINSTACK的两倍，
 * 以确保有足够空间进行基本操作。
 */
#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)

/**
 * @brief 字符串表：全局字符串管理的哈希表结构
 * 
 * 详细说明：
 * 这个结构体实现了Lua的全局字符串表，用于字符串的内部化处理。
 * 所有相同内容的字符串在系统中只保留一份拷贝，这不仅节省内存，
 * 还使得字符串比较可以通过指针比较来完成。
 * 
 * 设计特点：
 * - 开放寻址法解决哈希冲突
 * - 动态调整表大小保持性能
 * - 与垃圾收集器集成管理生命周期
 * 
 * 性能优化：
 * - 使用高质量哈希函数分布字符串
 * - 负载因子控制保持查找效率
 * - 惰性删除避免频繁内存操作
 */
typedef struct stringtable {
    /**
     * @brief 哈希数组：存储字符串对象指针的哈希表
     * 
     * 每个槽位可能包含：
     * - NULL：空槽位
     * - 指向TString的GCObject指针：有效字符串
     * - 已删除标记：用于惰性删除
     */
    GCObject **hash;

    /**
     * @brief 使用计数：当前表中字符串的数量
     * 
     * 用于计算负载因子和决定何时调整表大小。
     * 这个值不包括已删除但未清理的槽位。
     */
    lu_int32 nuse;

    /**
     * @brief 表大小：哈希数组的总容量
     * 
     * 总是2的幂次，以优化哈希计算。当负载因子
     * 过高或过低时，会调整到合适的大小。
     */
    int size;
} stringtable;

/**
 * @brief 调用信息：描述函数调用上下文的完整信息
 * 
 * 详细说明：
 * 这个结构体包含了一次函数调用的所有上下文信息，包括参数位置、
 * 返回地址、局部变量空间等。Lua使用CallInfo数组来维护调用栈，
 * 这种设计比传统的链式调用栈更高效。
 * 
 * 栈布局：
 * - [base, top): 当前函数的栈空间
 * - func: 函数对象的栈位置
 * - savedpc: 函数内当前执行位置
 * 
 * 生命周期：
 * - 函数调用时创建
 * - 函数执行期间维护
 * - 函数返回时清理
 */
typedef struct CallInfo {
    /**
     * @brief 栈基址：当前函数的栈空间起始位置
     * 
     * 指向当前函数第一个局部变量或参数的位置。
     * 所有局部变量都通过相对于base的偏移来访问。
     */
    StkId base;

    /**
     * @brief 函数位置：函数对象在栈中的位置
     * 
     * 指向当前正在执行的函数对象。这个位置在
     * 整个函数调用期间保持不变。
     */
    StkId func;

    /**
     * @brief 栈顶：当前函数可用栈空间的结束位置
     * 
     * 指向当前函数栈空间的上界。在函数执行过程中，
     * 栈指针不能超过这个位置。
     */
    StkId top;

    /**
     * @brief 保存的程序计数器：当前执行指令的位置
     * 
     * 对于Lua函数，指向当前正在执行的字节码指令。
     * 对于C函数，这个字段通常为NULL。
     */
    const Instruction *savedpc;

    /**
     * @brief 期望返回值数量：调用者期望的返回值个数
     * 
     * 特殊值：
     * - LUA_MULTRET (-1)：接受所有返回值
     * - 0：不需要返回值
     * - n (n>0)：需要n个返回值
     */
    int nresults;

    /**
     * @brief 尾调用计数：当前调用下丢失的尾调用次数
     * 
     * 记录了在当前调用下进行的尾调用优化次数。
     * 这个信息用于调试和错误跟踪。
     */
    int tailcalls;
} CallInfo;

/**
 * @brief 当前函数：获取当前正在执行的函数对象
 * 
 * @param L lua_State指针
 * @return 当前函数的Closure指针
 */
#define curr_func(L)    (clvalue(L->ci->func))

/**
 * @brief 调用信息函数：从CallInfo获取函数对象
 * 
 * @param ci CallInfo指针
 * @return 该调用信息对应的Closure指针
 */
#define ci_func(ci)     (clvalue((ci)->func))

/**
 * @brief 检查是否为Lua函数：判断CallInfo对应的是否为Lua函数
 * 
 * @param ci CallInfo指针
 * @return 非零值表示是Lua函数，0表示是C函数
 */
#define f_isLua(ci)     (!ci_func(ci)->c.isC)

/**
 * @brief 检查是否为Lua调用：综合检查函数类型和调用有效性
 * 
 * @param ci CallInfo指针
 * @return 非零值表示是有效的Lua函数调用
 */
#define isLua(ci)       (ttisfunction((ci)->func) && f_isLua(ci))

/**
 * @brief 全局状态：所有线程共享的系统级状态信息
 * 
 * 详细说明：
 * 全局状态包含了一个Lua状态机中所有线程共享的资源和配置信息。
 * 这包括垃圾收集器状态、字符串表、元表、内存分配器等系统级组件。
 * 所有协程共享同一个全局状态，这确保了资源的一致性和高效利用。
 * 
 * 设计原则：
 * - 资源共享：避免重复存储相同数据
 * - 一致性保证：所有线程看到相同的全局状态
 * - 性能优化：集中管理提高访问效率
 * - 内存效率：共享资源减少内存使用
 */
typedef struct global_State {
    /**
     * @brief 字符串表：全局字符串内部化管理
     * 
     * 管理所有字符串的全局唯一性，确保相同内容的字符串
     * 只在内存中存储一份拷贝。
     */
    stringtable strt;

    /**
     * @brief 内存分配器：自定义内存管理函数
     * 
     * 指向用户提供的内存分配函数，支持自定义内存管理策略。
     * 所有内存分配都通过这个函数进行。
     */
    lua_Alloc frealloc;

    /**
     * @brief 用户数据：传递给内存分配器的辅助数据
     * 
     * 用户可以通过这个指针向内存分配器传递额外的上下文信息。
     */
    void *ud;

    /**
     * @brief 当前白色：垃圾收集器的当前白色标记
     * 
     * 用于三色标记算法，标识当前收集周期使用的白色类型。
     */
    lu_byte currentwhite;

    /**
     * @brief GC状态：垃圾收集器的当前执行状态
     * 
     * 指示垃圾收集器当前处于哪个执行阶段：
     * - GCSpause：暂停状态
     * - GCSpropagate：标记传播阶段
     * - GCSsweepstring：字符串清扫阶段
     * - GCSsweep：对象清扫阶段
     * - GCSfinalize：终结器执行阶段
     */
    lu_byte gcstate;

    /**
     * @brief 字符串GC位置：字符串表清扫的当前位置
     * 
     * 记录增量垃圾收集在字符串表中的清扫进度。
     */
    int sweepstrgc;

    /**
     * @brief 根GC对象：所有可收集对象的根链表
     * 
     * 连接了所有需要垃圾收集管理的对象，形成单向链表。
     */
    GCObject *rootgc;

    /**
     * @brief GC清扫位置：对象清扫的当前位置指针
     * 
     * 指向rootgc链表中当前清扫位置的指针，用于增量清扫。
     */
    GCObject **sweepgc;

    /**
     * @brief 灰色对象链表：待处理的灰色对象
     * 
     * 在三色标记算法中，灰色对象是已访问但未完全扫描的对象。
     */
    GCObject *gray;

    /**
     * @brief 再次灰色对象：需要原子遍历的对象链表
     * 
     * 某些对象需要在标记阶段进行原子性处理，避免并发修改问题。
     */
    GCObject *grayagain;

    /**
     * @brief 弱引用表链表：需要清理的弱引用表
     * 
     * 包含弱键或弱值的表需要在垃圾收集时进行特殊处理。
     */
    GCObject *weak;

    /**
     * @brief 待终结用户数据：有终结器的用户数据链表
     * 
     * 链表尾部指针，用于管理需要调用__gc元方法的用户数据。
     */
    GCObject *tmudata;

    /**
     * @brief 临时缓冲区：字符串连接等操作的临时存储
     * 
     * 用于字符串连接、格式化等需要临时内存的操作。
     */
    Mbuffer buff;

    /**
     * @brief GC阈值：触发垃圾收集的内存使用阈值
     * 
     * 当totalbytes达到这个值时，会触发下一次垃圾收集。
     */
    lu_mem GCthreshold;

    /**
     * @brief 总分配字节数：当前分配的内存总量
     * 
     * 跟踪所有通过Lua内存分配器分配的内存总量。
     */
    lu_mem totalbytes;

    /**
     * @brief 估计使用量：实际使用内存的估计值
     * 
     * 由于内存对齐和分配器开销，实际使用量可能小于分配量。
     */
    lu_mem estimate;

    /**
     * @brief GC债务：垃圾收集的"滞后"程度
     * 
     * 表示垃圾收集相对于理想进度的滞后量，用于调整收集频率。
     */
    lu_mem gcdept;

    /**
     * @brief GC暂停：连续垃圾收集之间的暂停大小
     * 
     * 控制垃圾收集的频率，值越大收集越不频繁。
     */
    int gcpause;

    /**
     * @brief GC步长倍数：垃圾收集的"粒度"控制
     * 
     * 控制每次增量收集的工作量，影响收集的彻底程度。
     */
    int gcstepmul;

    /**
     * @brief 恐慌函数：无保护错误时调用的函数
     * 
     * 当发生无法处理的错误时，会调用这个函数进行最后的清理。
     */
    lua_CFunction panic;

    /**
     * @brief 注册表：全局的C代码专用表
     * 
     * 只能从C代码访问的全局表，用于存储C扩展的私有数据。
     */
    TValue l_registry;

    /**
     * @brief 主线程：主要的lua_State线程
     * 
     * 指向创建这个全局状态的主线程，用于生命周期管理。
     */
    struct lua_State *mainthread;

    /**
     * @brief upvalue链表头：所有开放upvalue的双向链表头
     * 
     * 管理所有当前开放的upvalue，用于闭包和变量生命周期管理。
     */
    UpVal uvhead;

    /**
     * @brief 基础类型元表：各基础类型的元表数组
     * 
     * 为数字、字符串、布尔值等基础类型提供元表支持。
     */
    struct Table *mt[NUM_TAGS];

    /**
     * @brief 标签方法名称：元方法名称的字符串数组
     * 
     * 存储了所有元方法名称的字符串对象，如"__index"、"__newindex"等。
     */
    TString *tmname[TM_N];
} global_State;

/**
 * @brief Lua状态：每个线程的独立执行状态
 * 
 * 详细说明：
 * lua_State结构体包含了一个Lua线程的完整执行状态，包括调用栈、
 * 局部变量、程序计数器等。每个协程都有自己独立的lua_State，
 * 但它们共享同一个global_State。这种设计使得协程切换高效，
 * 同时保证了数据的一致性。
 * 
 * 状态组成：
 * - 值栈：存储局部变量和临时值
 * - 调用栈：管理函数调用层次
 * - 执行状态：程序计数器和当前位置
 * - 调试信息：钩子函数和调试状态
 */
struct lua_State {
    /**
     * @brief 公共头部：GC对象的标准头部信息
     * 
     * 包含对象类型、标记位等垃圾收集需要的信息。
     */
    CommonHeader;

    /**
     * @brief 线程状态：当前线程的执行状态
     * 
     * 可能的值：
     * - LUA_OK (0)：正常执行状态
     * - LUA_YIELD：协程挂起状态
     * - LUA_ERRRUN：运行时错误
     * - LUA_ERRSYNTAX：语法错误
     * - LUA_ERRMEM：内存错误
     * - LUA_ERRERR：错误处理函数错误
     */
    lu_byte status;

    /**
     * @brief 栈顶：栈中第一个空闲槽位
     * 
     * 指向值栈中下一个可用的位置，是栈操作的重要参考点。
     */
    StkId top;

    /**
     * @brief 栈基址：当前函数的栈基地址
     * 
     * 指向当前函数的第一个参数或局部变量位置。
     */
    StkId base;

    /**
     * @brief 全局状态指针：指向共享的全局状态
     * 
     * 通过这个指针访问所有全局共享的资源和配置。
     */
    global_State *l_G;

    /**
     * @brief 当前调用信息：当前函数的调用上下文
     * 
     * 指向CallInfo数组中当前活跃的调用信息。
     */
    CallInfo *ci;

    /**
     * @brief 保存的程序计数器：当前函数的执行位置
     * 
     * 对于Lua函数，指向当前执行的字节码指令。
     */
    const Instruction *savedpc;

    /**
     * @brief 栈尾：栈的最后一个可用位置
     * 
     * 指向值栈的上界，用于栈溢出检查。
     */
    StkId stack_last;

    /**
     * @brief 栈基址：值栈的起始位置
     * 
     * 指向整个值栈的第一个位置，是所有栈操作的基准。
     */
    StkId stack;

    /**
     * @brief 调用信息结束：CallInfo数组的结束位置
     * 
     * 指向CallInfo数组之后的位置，用于边界检查。
     */
    CallInfo *end_ci;

    /**
     * @brief 调用信息基址：CallInfo数组的起始位置
     * 
     * 指向CallInfo数组的第一个元素。
     */
    CallInfo *base_ci;

    /**
     * @brief 栈大小：值栈的总容量
     * 
     * 表示当前分配的栈空间大小，以TValue为单位。
     */
    int stacksize;

    /**
     * @brief 调用信息大小：CallInfo数组的容量
     * 
     * 表示当前分配的CallInfo数组大小。
     */
    int size_ci;

    /**
     * @brief C调用计数：当前嵌套的C调用深度
     * 
     * 跟踪从Lua调用C函数的嵌套深度，用于栈溢出保护。
     */
    unsigned short nCcalls;

    /**
     * @brief 基础C调用：协程恢复时的C调用深度
     * 
     * 记录协程挂起时的C调用深度，用于正确恢复执行。
     */
    unsigned short baseCcalls;

    /**
     * @brief 钩子掩码：调试钩子的启用标志
     * 
     * 位掩码，指示哪些类型的调试钩子被启用：
     * - LUA_MASKCALL：函数调用钩子
     * - LUA_MASKRET：函数返回钩子
     * - LUA_MASKLINE：行执行钩子
     * - LUA_MASKCOUNT：指令计数钩子
     */
    lu_byte hookmask;

    /**
     * @brief 允许钩子：是否允许调用钩子函数
     * 
     * 用于避免在钩子执行过程中递归调用钩子。
     */
    lu_byte allowhook;

    /**
     * @brief 基础钩子计数：钩子计数的基准值
     * 
     * 用于重置指令计数钩子的计数器。
     */
    int basehookcount;

    /**
     * @brief 钩子计数：当前的指令计数
     * 
     * 跟踪执行的指令数量，用于指令计数钩子。
     */
    int hookcount;

    /**
     * @brief 钩子函数：用户设置的调试钩子函数
     * 
     * 当相应事件发生时，会调用这个函数进行调试处理。
     */
    lua_Hook hook;

    /**
     * @brief 全局表：当前线程的全局变量表
     * 
     * 存储该线程可见的所有全局变量。
     */
    TValue l_gt;

    /**
     * @brief 环境：临时环境存储
     * 
     * 用于存储临时的环境表，支持环境切换操作。
     */
    TValue env;

    /**
     * @brief 开放upvalue：当前栈中开放的upvalue链表
     * 
     * 管理当前栈中所有开放的upvalue，用于闭包实现。
     */
    GCObject *openupval;

    /**
     * @brief GC链表：垃圾收集的对象链表
     * 
     * 将此线程链接到垃圾收集器的管理链表中。
     */
    GCObject *gclist;

    /**
     * @brief 错误跳转：当前的错误恢复点
     * 
     * 指向当前活跃的setjmp缓冲区，用于错误处理。
     */
    struct lua_longjmp *errorJmp;

    /**
     * @brief 错误函数：当前错误处理函数的栈索引
     * 
     * 指向用户设置的错误处理函数在栈中的位置。
     */
    ptrdiff_t errfunc;
};

/**
 * @brief 获取全局状态：从线程状态获取全局状态指针
 * 
 * @param L lua_State指针
 * @return 对应的global_State指针
 */
#define G(L)    (L->l_G)

/**
 * @brief GC对象联合体：所有可收集对象的统一表示
 * 
 * 详细说明：
 * 这个联合体将所有可能被垃圾收集的对象类型统一在一个类型中，
 * 使得垃圾收集器可以用统一的方式处理不同类型的对象。每个
 * 对象都有一个公共的GC头部，包含类型信息和标记位。
 * 
 * 设计优势：
 * - 类型统一：所有GC对象都可以用GCObject*处理
 * - 内存效率：联合体只占用最大成员的空间
 * - 类型安全：通过宏进行安全的类型转换
 * - 扩展性：新的对象类型可以轻松加入
 */
union GCObject {
    GCheader gch;          /**< 公共GC头部信息 */
    union TString ts;      /**< 字符串对象 */
    union Udata u;         /**< 用户数据对象 */
    union Closure cl;      /**< 闭包对象 */
    struct Table h;        /**< 表对象 */
    struct Proto p;        /**< 函数原型对象 */
    struct UpVal uv;       /**< upvalue对象 */
    struct lua_State th;   /**< 线程对象 */
};

/* GCObject到具体类型的安全转换宏 */

/**
 * @brief 转换为字符串（原始）：将GCObject转换为TString联合体
 * 
 * @param o GCObject指针
 * @return TString联合体指针
 */
#define rawgco2ts(o)    check_exp((o)->gch.tt == LUA_TSTRING, &((o)->ts))

/**
 * @brief 转换为字符串：将GCObject转换为TString结构体
 * 
 * @param o GCObject指针
 * @return TString结构体指针
 */
#define gco2ts(o)       (&rawgco2ts(o)->tsv)

/**
 * @brief 转换为用户数据（原始）：将GCObject转换为Udata联合体
 * 
 * @param o GCObject指针
 * @return Udata联合体指针
 */
#define rawgco2u(o)     check_exp((o)->gch.tt == LUA_TUSERDATA, &((o)->u))

/**
 * @brief 转换为用户数据：将GCObject转换为Udata结构体
 * 
 * @param o GCObject指针
 * @return Udata结构体指针
 */
#define gco2u(o)        (&rawgco2u(o)->uv)

/**
 * @brief 转换为闭包：将GCObject转换为Closure联合体
 * 
 * @param o GCObject指针
 * @return Closure联合体指针
 */
#define gco2cl(o)       check_exp((o)->gch.tt == LUA_TFUNCTION, &((o)->cl))

/**
 * @brief 转换为表：将GCObject转换为Table结构体
 * 
 * @param o GCObject指针
 * @return Table结构体指针
 */
#define gco2h(o)        check_exp((o)->gch.tt == LUA_TTABLE, &((o)->h))

/**
 * @brief 转换为原型：将GCObject转换为Proto结构体
 * 
 * @param o GCObject指针
 * @return Proto结构体指针
 */
#define gco2p(o)        check_exp((o)->gch.tt == LUA_TPROTO, &((o)->p))

/**
 * @brief 转换为upvalue：将GCObject转换为UpVal结构体
 * 
 * @param o GCObject指针
 * @return UpVal结构体指针
 */
#define gco2uv(o)       check_exp((o)->gch.tt == LUA_TUPVAL, &((o)->uv))

/**
 * @brief 转换为upvalue（可空）：将可能为空的GCObject转换为UpVal
 * 
 * @param o GCObject指针，可以为NULL
 * @return UpVal结构体指针
 */
#define ngcotouv(o) \
    check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, &((o)->uv))

/**
 * @brief 转换为线程：将GCObject转换为lua_State结构体
 * 
 * @param o GCObject指针
 * @return lua_State结构体指针
 */
#define gco2th(o)       check_exp((o)->gch.tt == LUA_TTHREAD, &((o)->th))

/**
 * @brief 对象转GC对象：将任意Lua对象转换为GCObject指针
 * 
 * @param v 任意Lua对象指针
 * @return GCObject指针
 */
#define obj2gco(v)      (cast(GCObject *, (v)))

/**
 * @brief 创建新线程：创建一个新的Lua协程
 * 
 * 详细说明：
 * 这个函数创建一个新的Lua线程（协程），新线程与原线程共享相同的
 * 全局状态，但拥有独立的调用栈和局部状态。这是Lua协程实现的基础。
 * 
 * 创建过程：
 * 1. 分配新的lua_State结构
 * 2. 初始化独立的调用栈
 * 3. 共享全局状态
 * 4. 设置初始执行状态
 * 5. 链接到垃圾收集器
 * 
 * 资源共享：
 * - 全局状态：完全共享
 * - 字符串表：共享
 * - 元表：共享
 * - 注册表：共享
 * 
 * 独立资源：
 * - 调用栈：每个线程独立
 * - 局部变量：每个线程独立
 * - 执行状态：每个线程独立
 * - 错误处理：每个线程独立
 * 
 * @param L 父线程的lua_State指针
 * @return 新创建的lua_State指针，失败时为NULL
 * 
 * @note 新线程的生命周期由垃圾收集器管理
 * @warning 线程创建可能失败，调用者需要检查返回值
 * @see luaE_freethread(), lua_newthread()
 */
LUAI_FUNC lua_State *luaE_newthread(lua_State *L);

/**
 * @brief 释放线程：释放指定的Lua线程资源
 * 
 * 详细说明：
 * 这个函数释放一个Lua线程的所有资源，包括调用栈、局部变量等。
 * 通常由垃圾收集器调用，也可以在明确不再需要线程时手动调用。
 * 
 * 清理流程：
 * 1. 清理调用栈和局部变量
 * 2. 释放CallInfo数组
 * 3. 关闭所有开放的upvalue
 * 4. 清理调试状态
 * 5. 释放线程对象本身
 * 
 * 安全考虑：
 * - 不能释放当前正在执行的线程
 * - 释放前确保线程不在使用中
 * - 自动处理相关资源的清理
 * 
 * 性能影响：
 * - 释放操作相对较重
 * - 可能触发upvalue的闭合
 * - 影响垃圾收集的性能
 * 
 * @param L 主线程的lua_State指针
 * @param L1 要释放的线程的lua_State指针
 * 
 * @note L1不能是当前正在执行的线程
 * @warning 释放后L1指针将无效，不能再使用
 * @see luaE_newthread(), lua_close()
 */
LUAI_FUNC void luaE_freethread(lua_State *L, lua_State *L1);

#endif

