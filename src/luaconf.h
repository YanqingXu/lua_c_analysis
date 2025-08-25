/*
** $Id: luaconf.h,v 1.82.1.7 2008/02/11 16:25:08 roberto Exp $
** Lua配置文件 - 包含所有可配置的编译时选项
** 版权声明见lua.h
*/

#ifndef lconfig_h
#define lconfig_h

#include <limits.h>
#include <stddef.h>

/*
** ==================================================================
** 搜索"@@"可以找到所有可配置的定义
** ===================================================================
*/

/*
** 平台和标准兼容性配置
*/

/*
@@ LUA_ANSI 控制是否使用非ANSI特性
** 如果希望Lua避免使用任何非ANSI特性或库，请定义此宏
** 在严格ANSI模式下自动定义
*/
#if defined(__STRICT_ANSI__)
#define LUA_ANSI
#endif

/*
** 平台特定的配置
** 根据目标平台自动选择合适的特性集
*/

/* Windows平台配置 */
#if !defined(LUA_ANSI) && defined(_WIN32)
#define LUA_WIN
#endif

/* Linux平台配置 - 启用POSIX特性和动态库支持 */
#if defined(LUA_USE_LINUX)
#define LUA_USE_POSIX
#define LUA_USE_DLOPEN		/* 需要额外链接库: -ldl */
#define LUA_USE_READLINE	/* 需要额外的readline库 */
#endif

/* Mac OS X平台配置 - 使用POSIX和dyld动态库 */
#if defined(LUA_USE_MACOSX)
#define LUA_USE_POSIX
#define LUA_DL_DYLD		/* 不需要额外库 */
#endif

/*
@@ LUA_USE_POSIX 包含所有X/Open系统接口扩展(XSI)功能
** 如果您的系统兼容XSI，请定义此宏
*/
#if defined(LUA_USE_POSIX)
#define LUA_USE_MKSTEMP    /* 使用mkstemp创建临时文件(更安全) */
#define LUA_USE_ISATTY     /* 使用isatty检测终端 */
#define LUA_USE_POPEN      /* 使用popen进行进程通信 */
#define LUA_USE_ULONGJMP   /* 使用_longjmp/_setjmp(更高效) */
#endif

/*
** 环境变量配置
** 这些宏定义了Lua用于查找模块和初始化的环境变量名
*/

/*
@@ LUA_PATH 和 LUA_CPATH 是Lua检查以设置其路径的环境变量名
@@ LUA_INIT 是Lua检查初始化代码的环境变量名
** 如果需要不同的名称，请修改这些定义
*/
#define LUA_PATH        "LUA_PATH"   /* Lua模块搜索路径环境变量 */
#define LUA_CPATH       "LUA_CPATH"  /* C模块搜索路径环境变量 */
#define LUA_INIT	"LUA_INIT"   /* 初始化代码环境变量 */

/*
** 默认搜索路径配置
** 定义Lua查找模块的默认路径
*/

/*
@@ LUA_PATH_DEFAULT 是Lua查找Lua库的默认路径
@@ LUA_CPATH_DEFAULT 是Lua查找C库的默认路径
** 如果您的机器有非常规的目录层次结构或希望在非常规目录中安装库，请修改这些路径
*/
#if defined(_WIN32)
/*
** 在Windows中，路径中的任何感叹号('!')都会被当前进程可执行文件目录的路径替换
*/
#define LUA_LDIR	"!\\lua\\"     /* Lua库目录 */
#define LUA_CDIR	"!\\"          /* C库目录 */
#define LUA_PATH_DEFAULT  \
		".\\?.lua;"  LUA_LDIR"?.lua;"  LUA_LDIR"?\\init.lua;" \
		             LUA_CDIR"?.lua;"  LUA_CDIR"?\\init.lua"
#define LUA_CPATH_DEFAULT \
	".\\?.dll;"  LUA_CDIR"?.dll;" LUA_CDIR"loadall.dll"

#else
/* Unix/Linux默认路径配置 */
#define LUA_ROOT	"/usr/local/"                    /* 安装根目录 */
#define LUA_LDIR	LUA_ROOT "share/lua/5.1/"       /* Lua库目录 */
#define LUA_CDIR	LUA_ROOT "lib/lua/5.1/"         /* C库目录 */
#define LUA_PATH_DEFAULT  \
		"./?.lua;"  LUA_LDIR"?.lua;"  LUA_LDIR"?/init.lua;" \
		            LUA_CDIR"?.lua;"  LUA_CDIR"?/init.lua"
#define LUA_CPATH_DEFAULT \
	"./?.so;"  LUA_CDIR"?.so;" LUA_CDIR"loadall.so"
#endif

/*
** 路径分隔符和特殊标记配置
*/

/*
@@ LUA_DIRSEP 是目录分隔符(用于子模块)
** 如果您的机器不使用"/"作为目录分隔符且不是Windows，请修改此定义
** (在Windows上Lua自动使用"\")
*/
#if defined(_WIN32)
#define LUA_DIRSEP	"\\"
#else
#define LUA_DIRSEP	"/"
#endif

/*
@@ LUA_PATHSEP 是分隔路径模板的字符
@@ LUA_PATH_MARK 是标记模板中替换点的字符串
@@ LUA_EXECDIR 在Windows路径中被可执行文件目录替换
@@ LUA_IGMARK 是构建luaopen_函数名时忽略其前面所有内容的标记
** 如果由于某种原因您的系统无法使用这些字符，请修改它们
** (例如，如果其中一个字符是文件/目录名中的常见字符)
** 通常您不需要更改它们
*/
#define LUA_PATHSEP	";"    /* 路径分隔符 */
#define LUA_PATH_MARK	"?"    /* 路径模板替换标记 */
#define LUA_EXECDIR	"!"    /* 可执行文件目录标记 */
#define LUA_IGMARK	"-"    /* 函数名忽略标记 */

/*
** 基础数据类型配置
*/

/*
@@ LUA_INTEGER 是lua_pushinteger/lua_tointeger使用的整数类型
** 如果ptrdiff_t在您的机器上不合适，请修改此定义
** (在大多数机器上，ptrdiff_t在int或long之间提供了很好的选择)
*/
#define LUA_INTEGER	ptrdiff_t

/*
** API导出配置
** 控制函数如何导出到外部模块
*/

/*
@@ LUA_API 是所有核心API函数的标记
@@ LUALIB_API 是所有标准库函数的标记
** 如果需要以特殊方式定义这些函数，请修改它们
** 例如，如果要创建一个包含核心和库的Windows DLL，
** 可能需要使用以下定义(定义LUA_BUILD_AS_DLL来获得它)
*/
#if defined(LUA_BUILD_AS_DLL)

#if defined(LUA_CORE) || defined(LUA_LIB)
#define LUA_API __declspec(dllexport)  /* 导出符号 */
#else
#define LUA_API __declspec(dllimport)  /* 导入符号 */
#endif

#else

#define LUA_API		extern  /* 标准外部链接 */

#endif

/* 库API通常与核心API一起使用 */
#define LUALIB_API	LUA_API

/*
@@ LUAI_FUNC 是所有不应导出到外部模块的extern函数的标记
@@ LUAI_DATA 是所有不应导出到外部模块的extern(const)变量的标记
** 如果需要以特殊方式标记它们，请修改它们
** Elf/gcc(3.2及更高版本)将它们标记为"hidden"以在Lua编译为共享库时优化访问
*/
#if defined(luaall_c)
#define LUAI_FUNC	static     /* 单文件编译时使用static */
#define LUAI_DATA	/* empty */

#elif defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 302) && \
      defined(__ELF__)
#define LUAI_FUNC	__attribute__((visibility("hidden"))) extern
#define LUAI_DATA	LUAI_FUNC

#else
#define LUAI_FUNC	extern     /* 标准外部链接 */
#define LUAI_DATA	extern
#endif

/*
** 错误消息和调试配置
*/

/*
@@ LUA_QL 描述错误消息如何引用程序元素
** 如果希望不同的外观，请修改此定义
*/
#define LUA_QL(x)	"'" x "'"      /* 用单引号包围 */
#define LUA_QS		LUA_QL("%s")   /* 格式化字符串的引用 */

/*
@@ LUA_IDSIZE 给出调试信息中函数源描述的最大大小
** 如果希望不同的大小，请修改此定义
*/
#define LUA_IDSIZE	60  /* 函数源描述的最大长度 */

/*
** ==================================================================
** 独立解释器配置
** ===================================================================
*/

#if defined(lua_c) || defined(luaall_c)

/*
@@ lua_stdin_is_tty 检测标准输入是否为'tty'(即是否交互式运行lua)
** 如果您的非POSIX/非Windows系统有更好的定义，请修改此定义
*/
#if defined(LUA_USE_ISATTY)
#include <unistd.h>
#define lua_stdin_is_tty()	isatty(0)  /* POSIX系统使用isatty */
#elif defined(LUA_WIN)
#include <io.h>
#include <stdio.h>
#define lua_stdin_is_tty()	_isatty(_fileno(stdin))  /* Windows系统 */
#else
#define lua_stdin_is_tty()	1  /* 假设stdin是tty */
#endif

/*
@@ LUA_PROMPT 是独立Lua使用的默认提示符
@@ LUA_PROMPT2 是独立Lua使用的默认续行提示符
** 如果希望不同的提示符，请修改它们
** (您也可以通过赋值给全局变量_PROMPT/_PROMPT2来动态更改提示符)
*/
#define LUA_PROMPT		"> "   /* 主提示符 */
#define LUA_PROMPT2		">> "  /* 续行提示符 */

/*
@@ LUA_PROGNAME 是独立Lua程序的默认名称
** 如果您的独立解释器有不同的名称且您的系统无法自动检测该名称，请修改此定义
*/
#define LUA_PROGNAME		"lua"  /* 程序名称 */

/*
@@ LUA_MAXINPUT 是独立解释器中输入行的最大长度
** 如果需要更长的行，请修改此定义
*/
#define LUA_MAXINPUT	512  /* 最大输入行长度 */

/*
@@ lua_readline 定义如何显示提示符然后从标准输入读取一行
@@ lua_saveline 定义如何在"历史记录"中"保存"读取的行
@@ lua_freeline 定义如何释放lua_readline读取的行
** 如果希望改进此功能(例如，使用GNU readline和历史记录功能)，请修改它们
*/
#if defined(LUA_USE_READLINE)
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#define lua_readline(L,b,p)	((void)L, ((b)=readline(p)) != NULL)
#define lua_saveline(L,idx) \
	if (lua_strlen(L,idx) > 0)  /* 非空行? */ \
	  add_history(lua_tostring(L, idx));  /* 添加到历史记录 */
#define lua_freeline(L,b)	((void)L, free(b))
#else
/* 简单的输入实现 */
#define lua_readline(L,b,p)	\
	((void)L, fputs(p, stdout), fflush(stdout),  /* 显示提示符 */ \
	fgets(b, LUA_MAXINPUT, stdin) != NULL)  /* 获取行 */
#define lua_saveline(L,idx)	{ (void)L; (void)idx; }  /* 不保存 */
#define lua_freeline(L,b)	{ (void)L; (void)b; }    /* 不释放 */
#endif

#endif

/* ================================================================== */

/*
** 垃圾收集器配置
** 这些参数控制垃圾收集器的行为和性能
*/

/*
@@ LUAI_GCPAUSE 定义垃圾收集器周期之间的默认暂停时间(百分比)
** 如果希望GC运行得更快或更慢，请修改此值
** (更高的值意味着更大的暂停，即更慢的收集)
** 您也可以动态更改此值
*/
#define LUAI_GCPAUSE	200  /* 200%(等待内存翻倍后进行下次GC) */

/*
@@ LUAI_GCMUL 定义垃圾收集相对于内存分配的默认速度(百分比)
** 如果希望更改垃圾收集的粒度，请修改此值
** (更高的值意味着更粗糙的收集。0表示无穷大，每步执行完整收集)
** 您也可以动态更改此值
*/
#define LUAI_GCMUL	200 /* GC以内存分配"两倍速度"运行 */

/*
** 兼容性选项
** 控制与旧版本Lua的兼容性
*/

/*
@@ LUA_COMPAT_GETN 控制与旧getn行为的兼容性
** 如果希望与Lua 5.0中setn/getn的行为完全兼容，请定义此宏
*/
#undef LUA_COMPAT_GETN

/*
@@ LUA_COMPAT_LOADLIB 控制关于全局loadlib的兼容性
** 一旦不再需要全局'loadlib'函数，请将其取消定义
** (该函数仍可作为'package.loadlib'使用)
*/
#undef LUA_COMPAT_LOADLIB

/*
@@ LUA_COMPAT_VARARG 控制与旧vararg特性的兼容性
** 一旦您的程序仅使用'...'访问vararg参数(而不是旧的'arg'表)，请将其取消定义
*/
#define LUA_COMPAT_VARARG

/*
@@ LUA_COMPAT_MOD 控制与旧math.mod函数的兼容性
** 一旦您的程序使用'math.fmod'或新的'%'运算符而不是'math.mod'，请将其取消定义
*/
#define LUA_COMPAT_MOD

/*
@@ LUA_COMPAT_LSTR 控制与旧长字符串嵌套功能的兼容性
** 如果希望旧行为，请将其更改为2，或取消定义以关闭嵌套[[...]]时的警告错误
*/
#define LUA_COMPAT_LSTR		1

/*
@@ LUA_COMPAT_GFIND 控制与旧'string.gfind'名称的兼容性
** 一旦将'string.gfind'重命名为'string.gmatch'，请将其取消定义
*/
#define LUA_COMPAT_GFIND

/*
@@ LUA_COMPAT_OPENLIB 控制与旧'luaL_openlib'行为的兼容性
** 一旦将'luaL_openlib'的使用替换为'luaL_register'，请将其取消定义
*/
#define LUA_COMPAT_OPENLIB

/*
** API检查配置
*/

/*
@@ luai_apicheck 是Lua-C API使用的断言宏
** 如果希望Lua对从API调用获得的参数执行一些检查，请修改luai_apicheck
** 这可能会稍微减慢解释器的速度，但在调试与Lua接口的C代码时可能非常有用
** 一个有用的重定义是使用assert.h
*/
#if defined(LUA_USE_APICHECK)
#include <assert.h>
#define luai_apicheck(L,o)	{ (void)L; assert(o); }  /* 使用标准断言 */
#else
#define luai_apicheck(L,o)	{ (void)L; }  /* 不进行检查 */
#endif

/*
** 整数位数检测和类型定义
*/

/*
@@ LUAI_BITSINT 定义int中的位数
** 如果Lua无法自动检测您机器的位数，请在此修改
** 通常您不需要更改此定义
*/
/* 避免比较中的溢出 */
#if INT_MAX-20 < 32760
#define LUAI_BITSINT	16  /* 16位int */
#elif INT_MAX > 2147483640L
/* int至少有32位 */
#define LUAI_BITSINT	32  /* 32位int */
#else
#error "you must define LUA_BITSINT with number of bits in an integer"
#endif

/*
@@ LUAI_UINT32 是至少32位的无符号整数
@@ LUAI_INT32 是至少32位的有符号整数
@@ LUAI_UMEM 是足够大的无符号整数，可以计算Lua使用的总内存
@@ LUAI_MEM 是足够大的有符号整数，可以计算Lua使用的总内存
** 如果由于某种奇怪的原因默认定义对您的机器不够好，请在此修改
** ('else'部分中的定义总是有效的，但在64位long的机器上可能浪费空间)
** 通常您不需要更改此定义
*/
#if LUAI_BITSINT >= 32
#define LUAI_UINT32	unsigned int      /* 32位无符号整数 */
#define LUAI_INT32	int               /* 32位有符号整数 */
#define LUAI_MAXINT32	INT_MAX           /* int的最大值 */
#define LUAI_UMEM	size_t            /* 内存大小类型 */
#define LUAI_MEM	ptrdiff_t         /* 内存差值类型 */
#else
/* 16位int的情况 */
#define LUAI_UINT32	unsigned long     /* 使用long作为32位 */
#define LUAI_INT32	long
#define LUAI_MAXINT32	LONG_MAX
#define LUAI_UMEM	unsigned long
#define LUAI_MEM	long
#endif

/*
** 调用栈限制配置
** 这些限制防止无限递归耗尽内存
*/

/*
@@ LUAI_MAXCALLS 限制嵌套调用的数量
** 如果需要真正深度的递归调用，请修改此值
** 此限制是任意的；其唯一目的是在耗尽内存之前停止无限递归
*/
#define LUAI_MAXCALLS	20000  /* 最大嵌套调用数 */

/*
@@ LUAI_MAXCSTACK 限制C函数可以使用的Lua栈槽数量
** 如果您的C函数需要大量(Lua)栈空间，请修改此值
** 此限制是任意的；其唯一目的是阻止C函数消耗无限的栈空间
** (必须小于-LUA_REGISTRYINDEX)
*/
#define LUAI_MAXCSTACK	8000  /* C函数最大栈使用量 */

/*
** ==================================================================
** 如果您的系统有小的C栈，请将以下定义更改为较小的值
** (或者如果您的系统有大的C栈且这些限制对您来说太严格，
** 您可能希望将它们更改为较大的值)
** 其中一些常量控制编译器或解释器使用的栈分配数组的大小，
** 而另一些限制编译器或解释器可以执行的最大递归调用数
** 过大的值可能会导致某些形式的深层构造出现C栈溢出
** ===================================================================
*/

/*
@@ LUAI_MAXCCALLS 是嵌套C调用(短)和程序中语法嵌套非终结符的最大深度
*/
#define LUAI_MAXCCALLS		200  /* 最大C调用深度 */

/*
@@ LUAI_MAXVARS 是每个函数的最大局部变量数(必须小于250)
*/
#define LUAI_MAXVARS		200  /* 最大局部变量数 */

/*
@@ LUAI_MAXUPVALUES 是每个函数的最大上值数(必须小于250)
*/
#define LUAI_MAXUPVALUES	60   /* 最大上值数 */

/*
@@ LUAL_BUFFERSIZE 是lauxlib缓冲区系统使用的缓冲区大小
*/
#define LUAL_BUFFERSIZE		BUFSIZ  /* 使用系统默认缓冲区大小 */

/* ================================================================== */

/*
** ==================================================================
** 数值类型配置
@@ LUA_NUMBER 是Lua中数值的类型
** 只有当您希望使用不同于double的数值类型构建Lua时，才更改以下定义
** 您可能还需要更改lua_number2int和lua_number2integer
** ===================================================================
*/

#define LUA_NUMBER_DOUBLE  /* 使用double作为数值类型 */
#define LUA_NUMBER	double /* Lua数值类型 */

/*
@@ LUAI_UACNUMBER 是对数值进行'常规参数转换'的结果
*/
#define LUAI_UACNUMBER	double  /* 参数转换后的数值类型 */

/*
@@ LUA_NUMBER_SCAN 是读取数值的格式
@@ LUA_NUMBER_FMT 是写入数值的格式
@@ lua_number2str 将数值转换为字符串
@@ LUAI_MAXNUMBER2STR 是前面转换的最大大小
@@ lua_str2number 将字符串转换为数值
*/
#define LUA_NUMBER_SCAN		"%lf"           /* scanf格式 */
#define LUA_NUMBER_FMT		"%.14g"         /* printf格式 */
#define lua_number2str(s,n)	sprintf((s), LUA_NUMBER_FMT, (n))  /* 数值到字符串 */
#define LUAI_MAXNUMBER2STR	32 /* 16位数字、符号、小数点和\0 */
#define lua_str2number(s,p)	strtod((s), (p))  /* 字符串到数值 */

/*
@@ luai_num* 宏定义对数值的基本操作
*/
#if defined(LUA_CORE)
#include <math.h>
#define luai_numadd(a,b)	((a)+(b))           /* 加法 */
#define luai_numsub(a,b)	((a)-(b))           /* 减法 */
#define luai_nummul(a,b)	((a)*(b))           /* 乘法 */
#define luai_numdiv(a,b)	((a)/(b))           /* 除法 */
#define luai_nummod(a,b)	((a) - floor((a)/(b))*(b))  /* 模运算 */
#define luai_numpow(a,b)	(pow(a,b))          /* 幂运算 */
#define luai_numunm(a)		(-(a))              /* 取负 */
#define luai_numeq(a,b)		((a)==(b))          /* 相等比较 */
#define luai_numlt(a,b)		((a)<(b))           /* 小于比较 */
#define luai_numle(a,b)		((a)<=(b))          /* 小于等于比较 */
#define luai_numisnan(a)	(!luai_numeq((a), (a)))  /* NaN检测 */
#endif

/*
@@ lua_number2int 是将lua_Number转换为int的宏
@@ lua_number2integer 是将lua_Number转换为lua_Integer的宏
** 如果您知道在您的系统中将lua_Number转换为int的更快方法
** (使用任何舍入方法且不抛出错误)，请修改它们
** 在Pentium机器上，C中从double到int的简单类型转换极其缓慢，
** 因此任何替代方案都值得尝试
*/

/* 在Pentium上，使用技巧 */
#if defined(LUA_NUMBER_DOUBLE) && !defined(LUA_ANSI) && !defined(__SSE2__) && \
    (defined(__i386) || defined (_M_IX86) || defined(__i386__))

/* 在Microsoft编译器上，使用汇编 */
#if defined(_MSC_VER)

#define lua_number2int(i,d)   __asm fld d   __asm fistp i
#define lua_number2integer(i,n)		lua_number2int(i, n)

/* 下一个技巧应该在任何Pentium上工作，但有时与DirectX特性冲突 */
#else

union luai_Cast { double l_d; long l_l; };
#define lua_number2int(i,d) \
  { volatile union luai_Cast u; u.l_d = (d) + 6755399441055744.0; (i) = u.l_l; }
#define lua_number2integer(i,n)		lua_number2int(i, n)

#endif

/* 这个选项总是有效，但可能较慢 */
#else
#define lua_number2int(i,d)	((i)=(int)(d))           /* 简单转换 */
#define lua_number2integer(i,d)	((i)=(lua_Integer)(d))   /* 简单转换 */

#endif

/* ================================================================== */

/*
@@ LUAI_USER_ALIGNMENT_T 是需要最大对齐的类型
** 如果您的系统需要大于double的对齐，请修改此定义
** (例如，如果您的系统支持long double且它们必须在16字节边界上对齐，
** 那么您应该在联合中添加long double)
** 通常您不需要更改此定义
*/
#define LUAI_USER_ALIGNMENT_T	union { double u; void *s; long l; }

/*
@@ LUAI_THROW/LUAI_TRY 定义Lua如何进行异常处理
** 如果您更喜欢使用longjmp/setjmp即使在C++中，
** 或者如果您希望/不希望使用_longjmp/_setjmp而不是常规longjmp/setjmp，请修改它们
** 默认情况下，当编译为C++代码时Lua使用异常处理错误，
** 当要求使用时使用_longjmp/_setjmp，否则使用longjmp/setjmp
*/
#if defined(__cplusplus)
/* C++异常 */
#define LUAI_THROW(L,c)	throw(c)
#define LUAI_TRY(L,c,a)	try { a } catch(...) \
	{ if ((c)->status == 0) (c)->status = -1; }
#define luai_jmpbuf	int  /* 虚拟变量 */

#elif defined(LUA_USE_ULONGJMP)
/* 在Unix中，尝试_longjmp/_setjmp(更高效) */
#define LUAI_THROW(L,c)	_longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)	if (_setjmp((c)->b) == 0) { a }
#define luai_jmpbuf	jmp_buf

#else
/* 使用长跳转的默认处理 */
#define LUAI_THROW(L,c)	longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)	if (setjmp((c)->b) == 0) { a }
#define luai_jmpbuf	jmp_buf

#endif

/*
** 其他配置选项
*/

/*
@@ LUA_MAXCAPTURES 是模式匹配期间模式可以进行的最大捕获数
** 如果需要更多捕获，请修改此值。此限制是任意的
*/
#define LUA_MAXCAPTURES		32  /* 最大模式捕获数 */

/*
@@ lua_tmpnam 是OS库用于创建临时名称的函数
@@ LUA_TMPNAMBUFSIZE 是lua_tmpnam创建的名称的最大大小
** 如果您有tmpnam的替代方案(被认为不安全)或者您无论如何都想要原始tmpnam，请修改它们
** 默认情况下，除了在POSIX可用时使用mkstemp外，Lua使用tmpnam
*/
#if defined(loslib_c) || defined(luaall_c)

#if defined(LUA_USE_MKSTEMP)
#include <unistd.h>
#define LUA_TMPNAMBUFSIZE	32
#define lua_tmpnam(b,e)	{ \
	strcpy(b, "/tmp/lua_XXXXXX"); \
	e = mkstemp(b); \
	if (e != -1) close(e); \
	e = (e == -1); }

#else
#define LUA_TMPNAMBUFSIZE	L_tmpnam
#define lua_tmpnam(b,e)		{ e = (tmpnam(b) == NULL); }
#endif

#endif

/*
@@ lua_popen 生成一个通过文件流连接到当前进程的新进程
** 如果您有在系统中实现它的方法，请修改此定义
*/
#if defined(LUA_USE_POPEN)

#define lua_popen(L,c,m)	((void)L, fflush(NULL), popen(c,m))
#define lua_pclose(L,file)	((void)L, (pclose(file) != -1))

#elif defined(LUA_WIN)

#define lua_popen(L,c,m)	((void)L, _popen(c,m))
#define lua_pclose(L,file)	((void)L, (_pclose(file) != -1))

#else

#define lua_popen(L,c,m)	((void)((void)c, m),  \
		luaL_error(L, LUA_QL("popen") " not supported"), (FILE*)0)
#define lua_pclose(L,file)		((void)((void)L, file), 0)

#endif

/*
@@ LUA_DL_* 定义Lua应该使用哪个动态库系统
** 如果Lua在为您的平台选择适当的动态库系统时遇到问题，请在此修改
** (Windows的DLL、Mac的dyld或Unix的dlopen)
** 如果您的系统是某种Unix，很可能它有dlopen，所以LUA_DL_DLOPEN对它有效
** 要使用dlopen，您还需要调整src/Makefile(可能向链接器选项添加-ldl)，
** 因此Lua不会自动选择它
** (当您更改makefile以添加-ldl时，您还必须添加-DLUA_USE_DLOPEN)
** 如果您不想要任何类型的动态库，请取消定义所有这些选项
** 默认情况下，_WIN32获得LUA_DL_DLL，MAC OS X获得LUA_DL_DYLD
*/
#if defined(LUA_USE_DLOPEN)
#define LUA_DL_DLOPEN  /* Unix dlopen */
#endif

#if defined(LUA_WIN)
#define LUA_DL_DLL     /* Windows DLL */
#endif

/*
@@ LUAI_EXTRASPACE 允许您在lua_State中添加用户特定数据
@* (数据位于lua_State指针之前)
** 如果您真的需要，请修改(定义)此值
** 此值必须是您机器所需最大对齐的倍数
*/
#define LUAI_EXTRASPACE		0  /* 无额外空间 */

/*
@@ luai_userstate* 允许对线程进行用户特定操作
** 如果您定义了LUAI_EXTRASPACE并需要在创建/删除/恢复/让出线程时执行额外操作，请修改它们
*/
#define luai_userstateopen(L)		((void)L)      /* 线程打开时 */
#define luai_userstateclose(L)		((void)L)      /* 线程关闭时 */
#define luai_userstatethread(L,L1)	((void)L)      /* 创建新线程时 */
#define luai_userstatefree(L)		((void)L)      /* 释放线程时 */
#define luai_userstateresume(L,n)	((void)L)      /* 恢复协程时 */
#define luai_userstateyield(L,n)	((void)L)      /* 让出协程时 */

/*
@@ LUA_INTFRMLEN 是'string.format'中整数转换的长度修饰符
@@ LUA_INTFRM_T 是对应于前面长度修饰符的整数类型
** 如果您的系统支持long long或不支持long，请修改它们
*/

#if defined(LUA_USELONGLONG)

#define LUA_INTFRMLEN		"ll"        /* long long修饰符 */
#define LUA_INTFRM_T		long long   /* long long类型 */

#else

#define LUA_INTFRMLEN		"l"         /* long修饰符 */
#define LUA_INTFRM_T		long        /* long类型 */

#endif

/* =================================================================== */

/*
** 本地配置。您可以使用此空间添加您的重定义，而无需修改文件的主要部分
*/



#endif
