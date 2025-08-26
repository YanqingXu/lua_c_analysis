/*
** $Id: lobject.h,v 2.20.1.2 2008/08/06 13:29:48 roberto Exp $
** Lua对象类型定义 - Lua值系统的核心
** 版权声明见lua.h
*/

// #ifndef lobject_h
// #define lobject_h

#include <stdarg.h>

#include "llimits.h"
#include "lua.h"

/*
** Lua值类型系统
** Lua使用标记联合(tagged union)来表示所有值类型
*/

/*
** 从Lua可见的值类型标记
** 这些是用户可以通过type()函数看到的类型
*/
#define LAST_TAG	LUA_TTHREAD  /* 最后一个用户可见类型 */
#define NUM_TAGS	(LAST_TAG+1) /* 用户可见类型的总数 */

/*
** 内部使用的额外类型标记
** 这些类型对Lua用户不可见，仅用于内部实现
*/
#define LUA_TPROTO	(LAST_TAG+1)  /* 函数原型(编译后的函数) */
#define LUA_TUPVAL	(LAST_TAG+2)  /* 上值(upvalue) */
#define LUA_TDEADKEY	(LAST_TAG+3)  /* 表中的死键(已删除的键) */

/*
** 所有可收集对象的联合类型
** 这是垃圾收集器操作的基础类型
*/
typedef union GCObject GCObject;

/*
** 所有可收集对象的公共头部(宏形式)
** 包含在其他对象结构中，确保垃圾收集的一致性
** 
** 字段说明：
** - next: 指向下一个GC对象的指针，用于GC链表
** - tt: 类型标记(type tag)，标识对象的具体类型
** - marked: GC标记字节，用于标记-清除垃圾收集算法
*/
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked

/*
** 公共头部的结构体形式
** 用于需要显式访问头部字段的场合
*/
typedef struct GCheader {
  CommonHeader;
} GCheader;

/*
** Lua值的联合类型
** 这是TValue结构中实际存储数据的部分
** 
** 设计理念：
** - 所有Lua值都可以用这个联合表示
** - 大小固定，便于栈操作和数组存储
** - 通过类型标记区分具体类型
*/
typedef union {
  GCObject *gc;    /* 指向可收集对象(string, table, function等) */
  void *p;         /* 轻量用户数据(light userdata)的指针 */
  lua_Number n;    /* 数值类型 */
  int b;           /* 布尔类型 */
} Value;

/*
** 标记值(Tagged Values) - Lua值系统的核心
** 
** TValue是Lua中所有值的统一表示：
** - value: 实际的值数据
** - tt: 类型标记，指示value中哪个字段有效
** 
** 这种设计的优势：
** 1. 类型安全：每个值都携带类型信息
** 2. 统一接口：所有值都可以用相同方式处理
** 3. 内存效率：固定大小，便于管理
** 4. 性能优化：类型检查只需比较整数
*/
#define TValuefields	Value value; int tt

typedef struct lua_TValue {
  TValuefields;
} TValue;

/*
** 类型检测宏
** 这些宏提供了高效的类型检查，只需要比较类型标记
*/
#define ttisnil(o)	(ttype(o) == LUA_TNIL)           /* 是否为nil */
#define ttisnumber(o)	(ttype(o) == LUA_TNUMBER)       /* 是否为数值 */
#define ttisstring(o)	(ttype(o) == LUA_TSTRING)       /* 是否为字符串 */
#define ttistable(o)	(ttype(o) == LUA_TTABLE)        /* 是否为表 */
#define ttisfunction(o)	(ttype(o) == LUA_TFUNCTION)     /* 是否为函数 */
#define ttisboolean(o)	(ttype(o) == LUA_TBOOLEAN)      /* 是否为布尔值 */
#define ttisuserdata(o)	(ttype(o) == LUA_TUSERDATA)     /* 是否为用户数据 */
#define ttisthread(o)	(ttype(o) == LUA_TTHREAD)       /* 是否为线程 */
#define ttislightuserdata(o)	(ttype(o) == LUA_TLIGHTUSERDATA) /* 是否为轻量用户数据 */

/*
** 值访问宏
** 这些宏提供类型安全的值提取，使用check_exp确保类型正确
*/
#define ttype(o)	((o)->tt)  /* 获取类型标记 */

/* 获取可收集对象指针，带类型检查 */
#define gcvalue(o)	check_exp(iscollectable(o), (o)->value.gc)

/* 获取轻量用户数据指针 */
#define pvalue(o)	check_exp(ttislightuserdata(o), (o)->value.p)

/* 获取数值 */
#define nvalue(o)	check_exp(ttisnumber(o), (o)->value.n)

/* 获取字符串对象(原始形式) */
#define rawtsvalue(o)	check_exp(ttisstring(o), &(o)->value.gc->ts)

/* 获取字符串对象(标准形式) */
#define tsvalue(o)	(&rawtsvalue(o)->tsv)

/* 获取用户数据对象(原始形式) */
#define rawuvalue(o)	check_exp(ttisuserdata(o), &(o)->value.gc->u)

/* 获取用户数据对象(标准形式) */
#define uvalue(o)	(&rawuvalue(o)->uv)

/* 获取闭包对象 */
#define clvalue(o)	check_exp(ttisfunction(o), &(o)->value.gc->cl)

/* 获取表对象 */
#define hvalue(o)	check_exp(ttistable(o), &(o)->value.gc->h)

/* 获取布尔值 */
#define bvalue(o)	check_exp(ttisboolean(o), (o)->value.b)

/* 获取线程对象 */
#define thvalue(o)	check_exp(ttisthread(o), &(o)->value.gc->th)

/*
** Lua的假值判断
** 在Lua中，只有nil和false被认为是假值
** 这与C语言不同，0和空字符串在Lua中都是真值
*/
#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

/*
** 内部调试检查宏
** 这些宏在调试版本中验证对象的一致性
*/

/*
** 检查对象一致性：
** 对于可收集对象，其类型标记必须与GC对象头部的类型标记一致
*/
#define checkconsistency(obj) \
  lua_assert(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

/*
** 检查对象存活性：
** 确保对象类型一致且未被垃圾收集器标记为死亡
*/
#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
  ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))

/*
** 值设置宏
** 这些宏提供类型安全的值设置操作
*/

/* 设置为nil值 */
#define setnilvalue(obj) ((obj)->tt=LUA_TNIL)

/* 设置数值，使用临时变量避免副作用 */
#define setnvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER; }

/* 设置轻量用户数据 */
#define setpvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA; }

/* 设置布尔值 */
#define setbvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN; }

/* 设置字符串值，包含存活性检查 */
#define setsvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TSTRING; \
    checkliveness(G(L),i_o); }

/* 设置用户数据值 */
#define setuvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TUSERDATA; \
    checkliveness(G(L),i_o); }

/* 设置线程值 */
#define setthvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTHREAD; \
    checkliveness(G(L),i_o); }

/* 设置函数值 */
#define setclvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TFUNCTION; \
    checkliveness(G(L),i_o); }

/* 设置表值 */
#define sethvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTABLE; \
    checkliveness(G(L),i_o); }

/* 设置函数原型值(内部使用) */
#define setptvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TPROTO; \
    checkliveness(G(L),i_o); }

/*
** 对象复制宏
** 将一个TValue的内容完整复制到另一个TValue
** 包括值和类型标记，以及存活性检查
*/
#define setobj(L,obj1,obj2) \
  { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; o1->tt=o2->tt; \
    checkliveness(G(L),o1); }

/*
** 不同目标的赋值操作
** 根据赋值的源和目标位置，可能需要不同的处理
** 这些宏为将来的优化预留了空间(如写屏障)
*/

/* 从栈到(同一)栈 */
#define setobjs2s	setobj
/* 到栈(不是从同一栈) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* 从表到同一表 */
#define setobjt2t	setobj
/* 到表 */
#define setobj2t	setobj
/* 到新对象 */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

/* 直接设置类型标记(危险操作，仅内部使用) */
#define setttype(obj, tt) (ttype(obj) = (tt))

/*
** 可收集性检查
** 类型标记 >= LUA_TSTRING 的对象都是可收集的
** 这包括：string, table, function, userdata, thread
*/
#define iscollectable(o)	(ttype(o) >= LUA_TSTRING)

/*
** 栈索引类型
** 指向栈元素的指针，用于栈操作
*/
typedef TValue *StkId;

/*
** 字符串对象定义
** Lua中的字符串是不可变的，并且被内部化(interned)
**
** 设计特点：
** 1. 不可变性：字符串一旦创建就不能修改
** 2. 内部化：相同内容的字符串只存储一份
** 3. 哈希缓存：预计算哈希值，加速表查找
** 4. 长度缓存：存储长度，避免重复计算
*/
typedef union TString {
  L_Umaxalign dummy;  /* 确保字符串的最大对齐 */
  struct {
    CommonHeader;        /* GC公共头部 */
    lu_byte reserved;    /* 保留字段，用于标记保留字符串 */
    unsigned int hash;   /* 字符串的哈希值 */
    size_t len;         /* 字符串长度(不包括结尾的\0) */
  } tsv;
} TString;

/*
** 字符串内容访问宏
** 字符串内容紧跟在TString结构之后
*/
#define getstr(ts)	cast(const char *, (ts) + 1)
#define svalue(o)       getstr(rawtsvalue(o))

/*
** 用户数据对象定义
** 用户数据是Lua中存储C数据的机制
**
** 特点：
** 1. 可以有元表，支持元方法
** 2. 有独立的环境表
** 3. 存储任意C数据
** 4. 支持垃圾收集
*/
typedef union Udata {
  L_Umaxalign dummy;  /* 确保用户数据的最大对齐 */
  struct {
    CommonHeader;           /* GC公共头部 */
    struct Table *metatable; /* 元表，定义操作行为 */
    struct Table *env;       /* 环境表 */
    size_t len;             /* 用户数据的大小 */
  } uv;
} Udata;

/*
** 函数原型定义
** 存储编译后的Lua函数的所有信息
**
** 这是Lua函数的"模板"，包含：
** 1. 字节码指令
** 2. 常量表
** 3. 调试信息
** 4. 嵌套函数
** 5. 局部变量信息
*/
typedef struct Proto {
  CommonHeader;                /* GC公共头部 */
  TValue *k;                  /* 常量数组，函数使用的常量值 */
  Instruction *code;          /* 字节码指令数组 */
  struct Proto **p;           /* 嵌套函数原型数组 */
  int *lineinfo;             /* 指令到源码行号的映射 */
  struct LocVar *locvars;     /* 局部变量信息数组 */
  TString **upvalues;         /* 上值名称数组 */
  TString  *source;           /* 源码文件名 */
  int sizeupvalues;          /* 上值数组大小 */
  int sizek;                 /* 常量数组大小 */
  int sizecode;              /* 指令数组大小 */
  int sizelineinfo;          /* 行号信息数组大小 */
  int sizep;                 /* 嵌套函数数组大小 */
  int sizelocvars;           /* 局部变量信息数组大小 */
  int linedefined;           /* 函数定义开始行号 */
  int lastlinedefined;       /* 函数定义结束行号 */
  GCObject *gclist;          /* GC链表指针 */
  lu_byte nups;              /* 上值数量 */
  lu_byte numparams;         /* 参数数量 */
  lu_byte is_vararg;         /* 可变参数标志 */
  lu_byte maxstacksize;      /* 函数需要的最大栈大小 */
} Proto;

/*
** 新式可变参数的掩码
** 用于is_vararg字段，控制可变参数的行为
*/
#define VARARG_HASARG		1  /* 函数有固定参数 */
#define VARARG_ISVARARG		2  /* 函数接受可变参数 */
#define VARARG_NEEDSARG		4  /* 需要arg表(兼容性) */

/*
** 局部变量信息
** 用于调试，记录局部变量的作用域
*/
typedef struct LocVar {
  TString *varname;  /* 变量名 */
  int startpc;       /* 变量生效的第一个指令位置 */
  int endpc;         /* 变量失效的第一个指令位置 */
} LocVar;

/*
** 上值(Upvalues)
** 上值是闭包捕获外部变量的机制
**
** 设计理念：
** 1. 开放上值：指向栈上的活跃变量
** 2. 关闭上值：变量已离开作用域，值被复制到上值中
** 3. 双向链表：管理开放上值的生命周期
*/
typedef struct UpVal {
  CommonHeader;      /* GC公共头部 */
  TValue *v;         /* 指向栈或自身的value字段 */
  union {
    TValue value;    /* 值(当关闭时) */
    struct {         /* 双向链表(当开放时) */
      struct UpVal *prev;  /* 前一个上值 */
      struct UpVal *next;  /* 后一个上值 */
    } l;
  } u;
} UpVal;

/*
** 闭包(Closures)
** Lua中的函数实际上是闭包，可以捕获外部变量
**
** 两种类型：
** 1. C闭包：包装C函数
** 2. Lua闭包：包装Lua函数原型
*/

/*
** 闭包公共头部
** 所有闭包都包含这些字段
*/
#define ClosureHeader \
	CommonHeader; lu_byte isC; lu_byte nupvalues; GCObject *gclist; \
	struct Table *env

/*
** C闭包
** 包装C函数，可以有上值(实际是TValue)
*/
typedef struct CClosure {
  ClosureHeader;           /* 闭包公共头部 */
  lua_CFunction f;         /* C函数指针 */
  TValue upvalue[1];       /* 上值数组(可变长度) */
} CClosure;

/*
** Lua闭包
** 包装Lua函数原型，上值是UpVal指针
*/
typedef struct LClosure {
  ClosureHeader;           /* 闭包公共头部 */
  struct Proto *p;         /* 函数原型 */
  UpVal *upvals[1];        /* 上值指针数组(可变长度) */
} LClosure;

/*
** 闭包联合类型
** 统一表示C闭包和Lua闭包
*/
typedef union Closure {
  CClosure c;  /* C闭包 */
  LClosure l;  /* Lua闭包 */
} Closure;

/*
** 闭包类型检查宏
*/
#define iscfunction(o)	(ttype(o) == LUA_TFUNCTION && clvalue(o)->c.isC)
#define isLfunction(o)	(ttype(o) == LUA_TFUNCTION && !clvalue(o)->c.isC)

/*
** 表(Tables)
** Lua中的表是关联数组，是唯一的数据结构
**
** 混合实现：
** 1. 数组部分：存储连续的整数键
** 2. 哈希部分：存储其他类型的键
**
** 这种设计优化了常见的使用模式：
** - 数组访问：O(1)时间，紧凑存储
** - 哈希访问：平均O(1)时间，支持任意键类型
*/

/*
** 表键的联合类型
** 键可以是任何Lua值，还包含链表指针用于冲突解决
*/
typedef union TKey {
  struct {
    TValuefields;          /* 键值和类型 */
    struct Node *next;     /* 冲突链表的下一个节点 */
  } nk;
  TValue tvk;             /* 作为TValue访问 */
} TKey;

/*
** 哈希表节点
** 每个节点包含一个键值对
*/
typedef struct Node {
  TValue i_val;  /* 值 */
  TKey i_key;    /* 键 */
} Node;

/*
** 表结构
** Lua表的完整定义，包含数组和哈希两部分
*/
typedef struct Table {
  CommonHeader;              /* GC公共头部 */
  lu_byte flags;             /* 1<<p表示元方法p不存在，用于优化 */
  lu_byte lsizenode;         /* node数组大小的log2值 */
  struct Table *metatable;   /* 元表 */
  TValue *array;             /* 数组部分 */
  Node *node;                /* 哈希部分 */
  Node *lastfree;            /* 最后一个空闲位置之前的任何位置都是空闲的 */
  GCObject *gclist;          /* GC链表指针 */
  int sizearray;             /* 数组部分的大小 */
} Table;

/*
** 哈希表相关的工具宏和函数
*/

/*
** 模运算宏(用于哈希)
** size总是2的幂，所以可以用位运算优化
**
** 原理：对于2的幂n，x % n == x & (n-1)
** 这比除法运算快得多
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))

/*
** 2的幂计算宏
*/
#define twoto(x)	(1<<(x))           /* 计算2^x */
#define sizenode(t)	(twoto((t)->lsizenode))  /* 计算哈希表大小 */

/*
** 全局nil对象
** 所有nil值都指向这个唯一的对象，节省内存
*/
#define luaO_nilobject		(&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

/*
** 向上取整的log2计算
** 用于计算容纳x个元素需要的2的幂大小
*/
#define ceillog2(x)	(luaO_log2((x)-1) + 1)

/*
** 对象操作函数声明
** 这些函数提供了对象的各种操作和转换
*/

/*
** luaO_log2 - 计算整数的log2值
** 参数：x - 无符号整数
** 返回：floor(log2(x))，即x的最高位的位置
*/
LUAI_FUNC int luaO_log2 (unsigned int x);

/*
** luaO_int2fb - 将整数转换为"浮点字节"格式
** 参数：x - 无符号整数
** 返回：紧凑的浮点表示，用于存储大整数
**
** 用途：在表的大小调整中存储大的数组大小
*/
LUAI_FUNC int luaO_int2fb (unsigned int x);

/*
** luaO_fb2int - 将"浮点字节"转换回整数
** 参数：x - 浮点字节格式的值
** 返回：对应的整数值
*/
LUAI_FUNC int luaO_fb2int (int x);

/*
** luaO_rawequalObj - 原始对象相等比较
** 参数：t1, t2 - 要比较的两个TValue
** 返回：如果两个对象原始相等则返回1，否则返回0
**
** 注意：这是原始比较，不调用元方法
** 用于内部比较，如表键的查找
*/
LUAI_FUNC int luaO_rawequalObj (const TValue *t1, const TValue *t2);

/*
** luaO_str2d - 字符串到数值的转换
** 参数：s - 字符串
**       result - 存储结果的指针
** 返回：转换成功返回1，失败返回0
**
** 功能：将字符串转换为lua_Number
** 支持各种数值格式，包括十六进制
*/
LUAI_FUNC int luaO_str2d (const char *s, lua_Number *result);

/*
** luaO_pushvfstring - 格式化字符串并推入栈(va_list版本)
** 参数：L - Lua状态机
**       fmt - 格式字符串
**       argp - 参数列表
** 返回：格式化后的字符串指针
**
** 功能：类似sprintf，但结果推入Lua栈
** 支持Lua特定的格式说明符
*/
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);

/*
** luaO_pushfstring - 格式化字符串并推入栈(可变参数版本)
** 参数：L - Lua状态机
**       fmt - 格式字符串
**       ... - 可变参数
** 返回：格式化后的字符串指针
**
** 功能：luaO_pushvfstring的便利包装
*/
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);

/*
** luaO_chunkid - 生成代码块标识符
** 参数：out - 输出缓冲区
**       source - 源码标识符
**       len - 输出缓冲区长度
**
** 功能：为错误消息生成简短的源码标识
** 处理长文件名的截断和格式化
*/
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);

//#endif
