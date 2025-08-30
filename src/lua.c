/*
** [Lua解释器] Lua独立解释器实现
** 
** 功能说明：
** 本文件实现了Lua的独立命令行解释器，提供完整的Lua运行环境
** 
** 主要功能：
** - 命令行参数解析和处理
** - 交互式REPL模式（读取-求值-打印循环）
** - 脚本文件执行
** - 库文件加载和管理
** - 错误处理和调试支持
** - 信号处理（中断信号）
** - 环境变量处理（LUA_INIT）
**
** 支持的命令行选项：
** -e stat    执行字符串语句
** -l name    加载指定库
** -i         交互模式
** -v         显示版本信息
** --         停止处理选项
** -          从标准输入执行
**
** 设计模式：
** - 使用保护调用模式确保解释器稳定性
** - 支持调试模式和错误追踪
** - 完整的信号处理机制
** - 模块化的参数处理设计
*/


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lua_c

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


// 全局Lua状态机指针，用于信号处理
static lua_State *globalL = NULL;

// 程序名称，用于错误消息显示
static const char *progname = LUA_PROGNAME;


/*
** [信号处理] 停止执行回调函数
**
** 功能说明：
** 当接收到中断信号时调用此函数停止Lua执行
** 通过设置调试钩子在下一个安全点中断执行
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param ar - lua_Debug*：调试信息（未使用）
*/
static void lstop (lua_State *L, lua_Debug *ar) 
{
    // 避免未使用参数警告
    (void)ar;
    
    // 清除调试钩子
    lua_sethook(L, NULL, 0, 0);
    
    // 抛出中断错误
    luaL_error(L, "interrupted!");
}


/*
** [信号处理] 中断信号处理函数
**
** 功能说明：
** 处理SIGINT信号（Ctrl+C），实现优雅的中断机制
** 设置调试钩子以在安全点停止执行，避免数据损坏
**
** 处理流程：
** 1. 恢复默认信号处理（防止重复中断导致强制终止）
** 2. 设置调试钩子，在下次函数调用/返回时中断
**
** 参数说明：
** @param i - int：信号编号
*/
static void laction (int i) 
{
    // 恢复默认信号处理，如果再次收到SIGINT则终止进程
    signal(i, SIG_DFL);
    
    // 设置调试钩子，在函数调用、返回或指令计数时触发
    lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}


/*
** [帮助信息] 打印程序使用说明
**
** 功能说明：
** 显示命令行参数的使用方法和可用选项
** 输出到stderr以便与程序输出分离
*/
static void print_usage (void) 
{
    fprintf(stderr,
    "usage: %s [options] [script [args]].\n"
    "Available options are:\n"
    "  -e stat  execute string " LUA_QL("stat") "\n"
    "  -l name  require library " LUA_QL("name") "\n"
    "  -i       enter interactive mode after executing " LUA_QL("script") "\n"
    "  -v       show version information\n"
    "  --       stop handling options\n"
    "  -        execute stdin and stop handling options\n"
    ,
    progname);
    fflush(stderr);
}


/*
** [消息输出] 格式化错误消息输出
**
** 功能说明：
** 统一的错误消息输出格式，支持带程序名前缀的消息显示
**
** 参数说明：
** @param pname - const char*：程序名称前缀（可为NULL）
** @param msg - const char*：要显示的消息内容
*/
static void l_message (const char *pname, const char *msg) 
{
    // 如果提供程序名，作为前缀输出
    if (pname) 
    {
        fprintf(stderr, "%s: ", pname);
    }
    
    // 输出消息内容
    fprintf(stderr, "%s\n", msg);
    fflush(stderr);
}


/*
** [错误报告] 统一的错误状态报告函数
**
** 功能说明：
** 检查执行状态，如果有错误则输出错误消息
** 用于统一处理各种Lua操作的错误情况
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param status - int：执行状态（0表示成功）
**
** 返回值：
** @return int：原始状态值
*/
static int report (lua_State *L, int status) 
{
    // 如果有错误且栈顶不是nil
    if (status && !lua_isnil(L, -1)) 
    {
        // 获取错误消息
        const char *msg = lua_tostring(L, -1);
        
        // 如果错误对象不是字符串，使用默认消息
        if (msg == NULL) 
        {
            msg = "(error object is not a string)";
        }
        
        // 输出错误消息
        l_message(progname, msg);
        
        // 清理栈顶的错误对象
        lua_pop(L, 1);
    }
    
    return status;
}


/*
** [调试追踪] 错误追踪回调函数
**
** 功能说明：
** 为错误消息添加调用栈追踪信息
** 使用debug.traceback函数生成详细的错误上下文
**
** 处理流程：
** 1. 检查错误消息是否为字符串
** 2. 获取debug.traceback函数
** 3. 调用traceback生成栈追踪
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：返回1个值（原消息或带追踪的消息）
*/
static int traceback (lua_State *L) 
{
    // 如果错误消息不是字符串，保持原样
    if (!lua_isstring(L, 1))
    {
        return 1;
    }
    
    // 获取debug表
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (!lua_istable(L, -1)) 
    {
        lua_pop(L, 1);
        return 1;
    }
    
    // 获取debug.traceback函数
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) 
    {
        lua_pop(L, 2);
        return 1;
    }
    
    // 调用debug.traceback生成追踪信息
    lua_pushvalue(L, 1);        // 传递错误消息
    lua_pushinteger(L, 2);      // 跳过traceback函数本身
    lua_call(L, 2, 1);          // 调用traceback
    
    return 1;
}


/*
** [保护调用] 带错误处理的保护调用函数
**
** 功能说明：
** 在保护模式下调用Lua函数，提供错误追踪和信号处理
** 这是解释器执行代码的核心函数
**
** 执行流程：
** 1. 设置错误追踪函数
** 2. 设置中断信号处理
** 3. 执行保护调用
** 4. 清理和错误处理
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param narg - int：参数数量
** @param clear - int：是否清理返回值（0保留，非0清理）
**
** 返回值：
** @return int：执行状态（0表示成功）
*/
static int docall (lua_State *L, int narg, int clear) 
{
    int status;
    
    // 获取函数在栈中的位置
    int base = lua_gettop(L) - narg;
    
    // 推入追踪函数
    lua_pushcfunction(L, traceback);
    
    // 将追踪函数插入到函数和参数之前
    lua_insert(L, base);
    
    // 设置中断信号处理
    signal(SIGINT, laction);
    
    // 执行保护调用
    status = lua_pcall(L, narg, (clear ? 0 : LUA_MULTRET), base);
    
    // 恢复默认信号处理
    signal(SIGINT, SIG_DFL);
    
    // 移除追踪函数
    lua_remove(L, base);
    
    // 如果出错，强制执行完整的垃圾回收
    if (status != 0) 
    {
        lua_gc(L, LUA_GCCOLLECT, 0);
    }
    
    return status;
}


/*
** [版本信息] 打印Lua版本和版权信息
**
** 功能说明：
** 显示Lua的版本号和版权信息
** 通常在-v选项或交互模式启动时调用
*/
static void print_version (void) 
{
    l_message(NULL, LUA_RELEASE "  " LUA_COPYRIGHT);
}


/*
** [参数处理] 构建命令行参数表
**
** 功能说明：
** 将命令行参数转换为Lua的arg表
** arg表包含脚本参数，索引从0开始（脚本名为arg[0]）
**
** 参数分布：
** - arg[-n] 到 arg[-1]：解释器选项
** - arg[0]：脚本名称
** - arg[1] 到 arg[m]：脚本参数
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param argv - char**：命令行参数数组
** @param n - int：脚本在argv中的索引
**
** 返回值：
** @return int：脚本参数的数量
*/
static int getargs (lua_State *L, char **argv, int n) 
{
    int narg;
    int i;
    int argc = 0;
    
    // 计算总参数数量
    while (argv[argc]) 
    {
        argc++;
    }
    
    // 计算脚本参数数量
    narg = argc - (n + 1);
    
    // 检查栈空间
    luaL_checkstack(L, narg + 3, "too many arguments to script");
    
    // 推入脚本参数到栈
    for (i = n + 1; i < argc; i++)
    {
        lua_pushstring(L, argv[i]);
    }
    
    // 创建arg表
    lua_createtable(L, narg, n + 1);
    
    // 填充arg表（包括负索引的选项和正索引的参数）
    for (i = 0; i < argc; i++) 
    {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - n);     // arg[i-n] = argv[i]
    }
    
    return narg;
}


/*
** [文件执行] 执行Lua文件
**
** 功能说明：
** 加载并执行指定的Lua源文件
** 支持完整的错误处理和追踪
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param name - const char*：要执行的文件名
**
** 返回值：
** @return int：执行状态（0表示成功）
*/
static int dofile (lua_State *L, const char *name) 
{
    int status = luaL_loadfile(L, name) || docall(L, 0, 1);
    return report(L, status);
}


/*
** [字符串执行] 执行Lua代码字符串
**
** 功能说明：
** 将字符串作为Lua代码加载并执行
** 常用于-e选项指定的代码片段
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param s - const char*：要执行的Lua代码字符串
** @param name - const char*：代码块名称（用于错误信息）
**
** 返回值：
** @return int：执行状态（0表示成功）
*/
static int dostring (lua_State *L, const char *s, const char *name) 
{
    int status = luaL_loadbuffer(L, s, strlen(s), name) || docall(L, 0, 1);
    return report(L, status);
}


/*
** [库加载] 动态加载并执行库文件
**
** 功能说明：
** 加载指定的库文件到Lua环境
** 主要用于-l选项加载预加载库
** 使用require函数进行库的加载
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param name - const char*：库名称
**
** 返回值：
** @return int：加载状态（0表示成功）
*/
static int dolibrary (lua_State *L, const char *name) 
{
    lua_getglobal(L, "require");
    lua_pushstring(L, name);
    return report(L, docall(L, 1, 1));
}


/*
** [提示符获取] 获取交互式提示符
**
** 功能说明：
** 从Lua全局变量中获取提示符字符串
** 支持主提示符(_PROMPT)和续行提示符(_PROMPT2)
** 如果全局变量不存在则使用默认提示符
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param firstline - int：是否是首行（1为首行，0为续行）
**
** 返回值：
** @return const char*：提示符字符串
*/
static const char *get_prompt (lua_State *L, int firstline) 
{
    const char *p;
    lua_getfield(L, LUA_GLOBALSINDEX, firstline ? "_PROMPT" : "_PROMPT2");
    p = lua_tostring(L, -1);
    if (p == NULL) 
    {
        p = (firstline ? LUA_PROMPT : LUA_PROMPT2);
    }
    lua_pop(L, 1);  // 移除全局变量
    return p;
}


/*
** [语法完整性检查] 检查代码是否语法不完整
**
** 功能说明：
** 判断Lua代码是否因为语法不完整而导致编译错误
** 主要检查是否是因为遇到文件结尾(<eof>)而失败
** 用于交互式模式中判断是否需要继续输入
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param status - int：编译状态码
**
** 返回值：
** @return int：1表示语法不完整，0表示其他错误
*/
static int incomplete (lua_State *L, int status) 
{
    if (status == LUA_ERRSYNTAX) 
    {
        size_t lmsg;
        const char *msg = lua_tolstring(L, -1, &lmsg);
        const char *tp = msg + lmsg - (sizeof(LUA_QL("<eof>")) - 1);
        if (strstr(msg, LUA_QL("<eof>")) == tp) 
        {
            lua_pop(L, 1);
            return 1;
        }
    }
    return 0;  // 其他情况
}


/*
** [行读取] 读取一行用户输入
**
** 功能说明：
** 从标准输入读取一行文本
** 处理换行符移除和特殊语法转换
** 支持'='开头的表达式自动转换为return语句
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param firstline - int：是否是首行输入（影响提示符和语法处理）
**
** 返回值：
** @return int：1表示成功读取，0表示无输入
*/
static int pushline (lua_State *L, int firstline) 
{
    char buffer[LUA_MAXINPUT];
    char *b = buffer;
    size_t l;
    const char *prmt = get_prompt(L, firstline);
    
    if (lua_readline(L, b, prmt) == 0)
    {
        return 0;  // 无输入
    }
    
    l = strlen(b);
    if (l > 0 && b[l-1] == '\n')  // 行末有换行符？
    {
        b[l-1] = '\0';  // 移除换行符
    }
    
    if (firstline && b[0] == '=')  // 首行以'='开头？
    {
        lua_pushfstring(L, "return %s", b+1);  // 转换为return语句
    }
    else
    {
        lua_pushstring(L, b);
    }
    
    lua_freeline(L, b);
    return 1;
}


/*
** [完整行加载] 加载完整的Lua代码行
**
** 功能说明：
** 从交互式输入中读取完整的Lua代码
** 如果语法不完整会继续读取后续行
** 自动处理多行输入的连接和编译
**
** 处理流程：
** 1. 读取首行输入
** 2. 尝试编译代码
** 3. 如果语法不完整，继续读取下一行
** 4. 重复直到获得完整代码或遇到错误
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：加载状态（-1表示无输入，其他为编译状态）
*/
static int loadline (lua_State *L) 
{
    int status;
    lua_settop(L, 0);
    
    if (!pushline(L, 1))
    {
        return -1;  // 无输入
    }
    
    for (;;)  // 重复直到获得完整行
    {
        status = luaL_loadbuffer(L, lua_tostring(L, 1), lua_strlen(L, 1), "=stdin");
        if (!incomplete(L, status)) 
        {
            break;  // 无法尝试添加更多行？
        }
        if (!pushline(L, 0))  // 没有更多输入？
        {
            return -1;
        }
        lua_pushliteral(L, "\n");  // 添加新行符...
        lua_insert(L, -2);  // ...在两行之间
        lua_concat(L, 3);  // 连接它们
    }
    
    lua_saveline(L, 1);
    lua_remove(L, 1);  // 移除行
    return status;
}


/*
** [交互式模式] 交互式Lua解释器主循环
**
** 功能说明：
** 实现Lua的交互式模式（REPL）
** 循环读取用户输入、执行代码、显示结果
** 自动调用print函数显示表达式结果
**
** 处理流程：
** 1. 循环读取完整的代码行
** 2. 执行代码（如果加载成功）
** 3. 报告执行状态
** 4. 自动打印返回结果
** 5. 重复直到用户退出
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
*/
static void dotty (lua_State *L) 
{
    int status;
    const char *oldprogname = progname;
    progname = NULL;
    
    while ((status = loadline(L)) != -1) 
    {
        if (status == 0) 
        {
            status = docall(L, 0, 0);
        }
        report(L, status);
        
        if (status == 0 && lua_gettop(L) > 0)  // 有结果需要打印？
        {
            lua_getglobal(L, "print");
            lua_insert(L, 1);
            if (lua_pcall(L, lua_gettop(L)-1, 0, 0) != 0)
            {
                l_message(progname, lua_pushfstring(L,
                                       "error calling " LUA_QL("print") " (%s)",
                                       lua_tostring(L, -1)));
            }
        }
    }
    
    lua_settop(L, 0);  // 清空栈
    fputs("\n", stdout);
    fflush(stdout);
    progname = oldprogname;
}


static int handle_script (lua_State *L, char **argv, int n) {
  int status;
  const char *fname;
  int narg = getargs(L, argv, n);  /* collect arguments */
  lua_setglobal(L, "arg");
  fname = argv[n];
  if (strcmp(fname, "-") == 0 && strcmp(argv[n-1], "--") != 0) 
    fname = NULL;  /* stdin */
  status = luaL_loadfile(L, fname);
  lua_insert(L, -(narg+1));
  if (status == 0)
    status = docall(L, narg, 0);
  else
    lua_pop(L, narg);      
  return report(L, status);
}


/* check that argument has no extra characters at the end */
#define notail(x)	{if ((x)[2] != '\0') return -1;}


static int collectargs (char **argv, int *pi, int *pv, int *pe) {
  int i;
  for (i = 1; argv[i] != NULL; i++) {
    if (argv[i][0] != '-')  /* not an option? */
        return i;
    switch (argv[i][1]) {  /* option */
      case '-':
        notail(argv[i]);
        return (argv[i+1] != NULL ? i+1 : 0);
      case '\0':
        return i;
      case 'i':
        notail(argv[i]);
        *pi = 1;  /* go through */
      case 'v':
        notail(argv[i]);
        *pv = 1;
        break;
      case 'e':
        *pe = 1;  /* go through */
      case 'l':
        if (argv[i][2] == '\0') {
          i++;
          if (argv[i] == NULL) return -1;
        }
        break;
      default: return -1;  /* invalid option */
    }
  }
  return 0;
}


static int runargs (lua_State *L, char **argv, int n) {
  int i;
  for (i = 1; i < n; i++) {
    if (argv[i] == NULL) continue;
    lua_assert(argv[i][0] == '-');
    switch (argv[i][1]) {  /* option */
      case 'e': {
        const char *chunk = argv[i] + 2;
        if (*chunk == '\0') chunk = argv[++i];
        lua_assert(chunk != NULL);
        if (dostring(L, chunk, "=(command line)") != 0)
          return 1;
        break;
      }
      case 'l': {
        const char *filename = argv[i] + 2;
        if (*filename == '\0') filename = argv[++i];
        lua_assert(filename != NULL);
        if (dolibrary(L, filename))
          return 1;  /* stop if file fails */
        break;
      }
      default: break;
    }
  }
  return 0;
}


static int handle_luainit (lua_State *L) {
  const char *init = getenv(LUA_INIT);
  if (init == NULL) return 0;  /* status OK */
  else if (init[0] == '@')
    return dofile(L, init+1);
  else
    return dostring(L, init, "=" LUA_INIT);
}


struct Smain {
  int argc;
  char **argv;
  int status;
};


static int pmain (lua_State *L) {
  struct Smain *s = (struct Smain *)lua_touserdata(L, 1);
  char **argv = s->argv;
  int script;
  int has_i = 0, has_v = 0, has_e = 0;
  globalL = L;
  if (argv[0] && argv[0][0]) progname = argv[0];
  lua_gc(L, LUA_GCSTOP, 0);  /* stop collector during initialization */
  luaL_openlibs(L);  /* open libraries */
  lua_gc(L, LUA_GCRESTART, 0);
  s->status = handle_luainit(L);
  if (s->status != 0) return 0;
  script = collectargs(argv, &has_i, &has_v, &has_e);
  if (script < 0) {  /* invalid args? */
    print_usage();
    s->status = 1;
    return 0;
  }
  if (has_v) print_version();
  s->status = runargs(L, argv, (script > 0) ? script : s->argc);
  if (s->status != 0) return 0;
  if (script)
    s->status = handle_script(L, argv, script);
  if (s->status != 0) return 0;
  if (has_i)
    dotty(L);
  else if (script == 0 && !has_e && !has_v) {
    if (lua_stdin_is_tty()) {
      print_version();
      dotty(L);
    }
    else dofile(L, NULL);  /* executes stdin as a file */
  }
  return 0;
}


int main (int argc, char **argv) {
  int status;
  struct Smain s;
  lua_State *L = lua_open();  /* create state */
  if (L == NULL) {
    l_message(argv[0], "cannot create state: not enough memory");
    return EXIT_FAILURE;
  }
  s.argc = argc;
  s.argv = argv;
  status = lua_cpcall(L, &pmain, &s);
  report(L, status);
  lua_close(L);
  return (status || s.status) ? EXIT_FAILURE : EXIT_SUCCESS;
}

