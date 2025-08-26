/*
** Lua 状态管理头文件
** 定义了 Lua 虚拟机的全局状态和线程状态结构
** 包含垃圾回收、栈管理、函数调用等核心数据结构
*/

// #ifndef lstate_h
// #define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"



// 前向声明，在 ldo.c 中定义
struct lua_longjmp;


// 获取全局表的宏
#define gt(L)    (&L->l_gt)

// 获取注册表的宏
#define registry(L)    (&G(L)->l_registry)


// 处理元方法调用和其他额外操作所需的额外栈空间
#define EXTRA_STACK   5


// 基本调用信息数组大小
#define BASIC_CI_SIZE           8

// 基本栈大小
#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)



// 字符串表结构体
// 用于存储所有字符串的哈希表
typedef struct stringtable
{
    GCObject **hash;    // 哈希表数组
    lu_int32 nuse;      // 已使用的元素数量
    int size;           // 哈希表大小
} stringtable;


// 函数调用信息结构体
// 存储每个函数调用的相关信息
typedef struct CallInfo
{
    StkId base;                     // 当前函数的栈基址
    StkId func;                     // 函数在栈中的索引
    StkId top;                      // 当前函数的栈顶
    const Instruction *savedpc;     // 保存的程序计数器
    int nresults;                   // 期望从此函数返回的结果数量
    int tailcalls;                  // 在此条目下丢失的尾调用数量
} CallInfo;



// 获取当前函数的宏
#define curr_func(L)    (clvalue(L->ci->func))
// 从调用信息获取函数的宏
#define ci_func(ci)     (clvalue((ci)->func))
// 检查函数是否为 Lua 函数的宏
#define f_isLua(ci)     (!ci_func(ci)->c.isC)
// 检查调用信息是否为 Lua 函数的宏
#define isLua(ci)       (ttisfunction((ci)->func) && f_isLua(ci))


// 全局状态结构体
// 由此状态的所有线程共享
typedef struct global_State
{
    stringtable strt;               // 字符串哈希表
    lua_Alloc frealloc;             // 内存重新分配函数
    void *ud;                       // frealloc 的辅助数据
    lu_byte currentwhite;           // 当前白色标记
    lu_byte gcstate;                // 垃圾回收器状态
    int sweepstrgc;                 // 在 strt 中的清扫位置
    GCObject *rootgc;               // 所有可回收对象的链表
    GCObject **sweepgc;             // 在 rootgc 中的清扫位置
    GCObject *gray;                 // 灰色对象链表
    GCObject *grayagain;            // 需要原子遍历的对象链表
    GCObject *weak;                 // 弱表链表（待清理）
    GCObject *tmudata;              // 待 GC 的用户数据链表的最后元素
    Mbuffer buff;                   // 字符串连接的临时缓冲区
    lu_mem GCthreshold;             // GC 阈值
    lu_mem totalbytes;              // 当前分配的字节数
    lu_mem estimate;                // 实际使用字节数的估计值
    lu_mem gcdept;                  // GC "落后进度" 的程度
    int gcpause;                    // 连续 GC 之间的暂停大小
    int gcstepmul;                  // GC "粒度"
    lua_CFunction panic;            // 在无保护错误中调用的函数
    TValue l_registry;              // 注册表
    struct lua_State *mainthread;   // 主线程
    UpVal uvhead;                   // 所有开放上值的双向链表头
    struct Table *mt[NUM_TAGS];     // 基本类型的元表
    TString *tmname[TM_N];          // 标签方法名称数组
} global_State;


// 每线程状态结构体
// Lua 状态机的核心数据结构
struct lua_State
{
    CommonHeader;                           // 公共 GC 头部
    lu_byte status;                         // 线程状态
    StkId top;                              // 栈中第一个空闲槽位
    StkId base;                             // 当前函数的基址
    global_State *l_G;                      // 全局状态指针
    CallInfo *ci;                           // 当前函数的调用信息
    const Instruction *savedpc;             // 当前函数的保存程序计数器
    StkId stack_last;                       // 栈中最后一个空闲槽位
    StkId stack;                            // 栈基址
    CallInfo *end_ci;                       // 指向 ci 数组末尾之后
    CallInfo *base_ci;                      // CallInfo 数组
    int stacksize;                          // 栈大小
    int size_ci;                            // base_ci 数组的大小
    unsigned short nCcalls;                 // 嵌套 C 调用数量
    unsigned short baseCcalls;              // 恢复协程时的嵌套 C 调用数
    lu_byte hookmask;                       // 钩子掩码
    lu_byte allowhook;                      // 是否允许钩子
    int basehookcount;                      // 基础钩子计数
    int hookcount;                          // 钩子计数
    lua_Hook hook;                          // 钩子函数
    TValue l_gt;                            // 全局表
    TValue env;                             // 环境的临时位置
    GCObject *openupval;                    // 此栈中开放上值的链表
    GCObject *gclist;                       // GC 链表
    struct lua_longjmp *errorJmp;           // 当前错误恢复点
    ptrdiff_t errfunc;                      // 当前错误处理函数（栈索引）
};


// 从 Lua 状态获取全局状态的宏
#define G(L)    (L->l_G)


// 所有可回收对象的联合体
// 用于统一管理不同类型的 GC 对象
union GCObject
{
    GCheader gch;           // GC 头部
    union TString ts;       // 字符串
    union Udata u;          // 用户数据
    union Closure cl;       // 闭包
    struct Table h;         // 表
    struct Proto p;         // 函数原型
    struct UpVal uv;        // 上值
    struct lua_State th;    // 线程
};


// 将 GCObject 转换为特定值的宏
#define rawgco2ts(o)    check_exp((o)->gch.tt == LUA_TSTRING, &((o)->ts))
#define gco2ts(o)       (&rawgco2ts(o)->tsv)
#define rawgco2u(o)     check_exp((o)->gch.tt == LUA_TUSERDATA, &((o)->u))
#define gco2u(o)        (&rawgco2u(o)->uv)
#define gco2cl(o)       check_exp((o)->gch.tt == LUA_TFUNCTION, &((o)->cl))
#define gco2h(o)        check_exp((o)->gch.tt == LUA_TTABLE, &((o)->h))
#define gco2p(o)        check_exp((o)->gch.tt == LUA_TPROTO, &((o)->p))
#define gco2uv(o)       check_exp((o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define ngcotouv(o) \
    check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define gco2th(o)       check_exp((o)->gch.tt == LUA_TTHREAD, &((o)->th))

// 将任何 Lua 对象转换为 GCObject 的宏
#define obj2gco(v)      (cast(GCObject *, (v)))


// 函数声明
LUAI_FUNC lua_State *luaE_newthread (lua_State *L);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);

//#endif