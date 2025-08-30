/*
** [基础库] Lua 基础库实现
** 
** 功能说明：
** 本文件实现了 Lua 的基础库函数，包括：
** - 基本输入输出函数（print）
** - 类型转换函数（tonumber, tostring, type）  
** - 元表操作函数（getmetatable, setmetatable）
** - 错误处理函数（error, assert, pcall, xpcall）
** - 环境操作函数（getfenv, setfenv）
** - 表操作函数（rawget, rawset, rawequal, next, pairs, ipairs）
** - 代码加载函数（load, loadfile, loadstring, dofile）
** - 垃圾回收函数（collectgarbage, gcinfo）
** - 协程库函数（coroutine.create, coroutine.resume等）
** - 工具函数（unpack, select, newproxy）
**
** 设计模式：
** - 使用标准的 Lua C API 模式
** - 每个函数都是 lua_CFunction 类型
** - 通过函数注册表导出到 Lua 环境
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lbaselib_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/*
** [基础] print 函数实现
**
** 功能说明：
** 实现 Lua 的 print 函数，用于输出值到标准输出
** 
** 实现机制：
** 1. 获取全局 tostring 函数
** 2. 对每个参数调用 tostring 进行字符串转换
** 3. 使用制表符分隔多个参数
** 4. 最后输出换行符
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：返回值数量（总是0）
**
** 注意事项：
** - 如果系统不支持 stdout，可以移除此函数
** - 可以修改 fputs 的目标来改变输出位置（控制台窗口或日志文件）
*/
static int luaB_print (lua_State *L) 
{
    // 获取参数数量
    int n = lua_gettop(L);
    int i;
    
    // 获取全局 tostring 函数用于类型转换
    lua_getglobal(L, "tostring");
    
    // 遍历所有参数并转换为字符串输出
    for (i = 1; i <= n; i++) 
    {
        const char *s;
        
        // 复制 tostring 函数到栈顶
        lua_pushvalue(L, -1);
        
        // 推送要转换的参数
        lua_pushvalue(L, i);
        
        // 调用 tostring 函数
        lua_call(L, 1, 1);
        
        // 获取转换结果
        s = lua_tostring(L, -1);
        
        // 检查 tostring 是否返回了有效字符串
        if (s == NULL)
        {
            return luaL_error(L, LUA_QL("tostring") " must return a string to "
                                 LUA_QL("print"));
        }
        
        // 如果不是第一个参数，输出制表符作为分隔符
        if (i > 1) 
        {
            fputs("\t", stdout);
        }
        
        // 输出字符串内容
        fputs(s, stdout);
        
        // 弹出转换结果
        lua_pop(L, 1);
    }
    
    // 输出换行符
    fputs("\n", stdout);
    return 0;
}


/*
** [类型转换] tonumber 函数实现
**
** 功能说明：
** 将字符串或其他类型转换为数字
**
** 转换规则：
** - base = 10：标准数字转换，支持浮点数
** - base = 2-36：按指定进制解析整数字符串
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要转换的值
** 栈参数2：进制基数（可选，默认为10）
**
** 返回值：
** @return int：返回1个值（转换后的数字或nil）
*/
static int luaB_tonumber (lua_State *L) 
{
    // 获取进制基数，默认为10
    int base = luaL_optint(L, 2, 10);
    
    if (base == 10) 
    {
        // 标准转换（支持浮点数）
        luaL_checkany(L, 1);
        
        if (lua_isnumber(L, 1)) 
        {
            lua_pushnumber(L, lua_tonumber(L, 1));
            return 1;
        }
    }
    else 
    {
        // 按指定进制转换整数
        const char *s1 = luaL_checkstring(L, 1);
        char *s2;
        unsigned long n;
        
        // 检查进制范围
        luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
        
        // 使用标准库函数进行进制转换
        n = strtoul(s1, &s2, base);
        
        if (s1 != s2) 
        {
            // 至少有一个有效数字
            
            // 跳过尾部空白字符
            while (isspace((unsigned char)(*s2))) 
            {
                s2++;
            }
            
            // 检查是否没有无效的尾部字符
            if (*s2 == '\0') 
            {
                lua_pushnumber(L, (lua_Number)n);
                return 1;
            }
        }
    }
    
    // 转换失败，返回 nil
    lua_pushnil(L);
    return 1;
}


/*
** [错误处理] error 函数实现
**
** 功能说明：
** 抛出一个错误，终止当前函数的执行
**
** 错误处理机制：
** - 如果错误消息是字符串且level > 0，会添加调用位置信息
** - level 参数控制错误报告的调用层级
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：错误消息
** 栈参数2：错误层级（可选，默认为1）
**
** 返回值：
** @return int：此函数不会正常返回，总是抛出错误
*/
static int luaB_error (lua_State *L) 
{
    // 获取错误层级，默认为1
    int level = luaL_optint(L, 2, 1);
    
    // 只保留错误消息参数
    lua_settop(L, 1);
    
    // 如果错误消息是字符串且需要添加位置信息
    if (lua_isstring(L, 1) && level > 0) 
    {
        // 获取调用位置信息
        luaL_where(L, level);
        
        // 将位置信息与错误消息连接
        lua_pushvalue(L, 1);
        lua_concat(L, 2);
    }
    
    // 抛出错误
    return lua_error(L);
}


/*
** [元表操作] getmetatable 函数实现
**
** 功能说明：
** 获取指定对象的元表
**
** 返回规则：
** - 如果对象有元表，返回其元表
** - 如果元表中有 __metatable 字段，返回该字段的值（元表保护机制）
** - 如果对象没有元表，返回 nil
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要获取元表的对象
**
** 返回值：
** @return int：返回1个值（元表或nil）
*/
static int luaB_getmetatable (lua_State *L) 
{
    // 检查参数存在性
    luaL_checkany(L, 1);
    
    // 尝试获取对象的元表
    if (!lua_getmetatable(L, 1)) 
    {
        // 对象没有元表，返回 nil
        lua_pushnil(L);
        return 1;
    }
    
    // 检查元表是否有 __metatable 字段（元表保护）
    luaL_getmetafield(L, 1, "__metatable");
    return 1;
}


/*
** [元表操作] setmetatable 函数实现
**
** 功能说明：
** 为指定的表设置元表
**
** 设置规则：
** - 第一个参数必须是表
** - 第二个参数必须是表或nil
** - 如果当前元表有 __metatable 字段，则禁止修改（元表保护）
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要设置元表的表
** 栈参数2：新的元表（表或nil）
**
** 返回值：
** @return int：返回1个值（设置后的表本身）
*/
static int luaB_setmetatable (lua_State *L) 
{
    // 获取第二个参数的类型
    int t = lua_type(L, 2);
    
    // 检查第一个参数必须是表
    luaL_checktype(L, 1, LUA_TTABLE);
    
    // 检查第二个参数必须是nil或表
    luaL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2,
                      "nil or table expected");
    
    // 检查是否有元表保护
    if (luaL_getmetafield(L, 1, "__metatable"))
    {
        luaL_error(L, "cannot change a protected metatable");
    }
    
    // 只保留前两个参数
    lua_settop(L, 2);
    
    // 设置元表
    lua_setmetatable(L, 1);
    
    return 1;
}


/*
** [辅助函数] 获取函数对象
**
** 功能说明：
** 根据参数获取函数对象，支持两种方式：
** 1. 直接传入函数对象
** 2. 传入调用栈层级数字，获取对应层级的函数
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param opt - int：是否可选参数（1表示可选，0表示必需）
**
** 副作用：
** 将获取到的函数推入栈顶
*/
static void getfunc (lua_State *L, int opt) 
{
    if (lua_isfunction(L, 1)) 
    {
        // 参数1是函数，直接使用
        lua_pushvalue(L, 1);
    }
    else 
    {
        lua_Debug ar;
        int level;
        
        // 参数1是数字，表示调用栈层级
        if (opt)
        {
            level = luaL_optint(L, 1, 1);
        }
        else
        {
            level = luaL_checkint(L, 1);
        }
        
        // 层级必须非负
        luaL_argcheck(L, level >= 0, 1, "level must be non-negative");
        
        // 获取指定层级的调用栈信息
        if (lua_getstack(L, level, &ar) == 0)
        {
            luaL_argerror(L, 1, "invalid level");
        }
        
        // 获取该层级的函数对象
        lua_getinfo(L, "f", &ar);
        
        // 检查是否成功获取函数
        if (lua_isnil(L, -1))
        {
            luaL_error(L, "no function environment for tail call at level %d",
                          level);
        }
    }
}


/*
** [环境操作] getfenv 函数实现
**
** 功能说明：
** 获取函数或线程的环境表
**
** 获取规则：
** - 对于 C 函数，返回线程的全局环境
** - 对于 Lua 函数，返回其环境表
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：函数对象或栈层级数字
**
** 返回值：
** @return int：返回1个值（环境表）
*/
static int luaB_getfenv (lua_State *L) 
{
    // 获取函数对象
    getfunc(L, 1);
    
    if (lua_iscfunction(L, -1))
    {
        // C函数：返回线程的全局环境
        lua_pushvalue(L, LUA_GLOBALSINDEX);
    }
    else
    {
        // Lua函数：获取其环境表
        lua_getfenv(L, -1);
    }
    
    return 1;
}


/*
** [环境操作] setfenv 函数实现
**
** 功能说明：
** 设置函数或线程的环境表
**
** 设置规则：
** - 第二个参数必须是表
** - 如果第一个参数是0，修改当前线程的环境
** - 不能修改C函数的环境
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：函数对象、栈层级数字或0（表示当前线程）
** 栈参数2：新的环境表
**
** 返回值：
** @return int：返回0或1个值
*/
static int luaB_setfenv (lua_State *L) 
{
    // 检查第二个参数必须是表
    luaL_checktype(L, 2, LUA_TTABLE);
    
    // 获取函数对象
    getfunc(L, 0);
    
    // 复制环境表到栈顶
    lua_pushvalue(L, 2);
    
    if (lua_isnumber(L, 1) && lua_tonumber(L, 1) == 0) 
    {
        // 修改当前线程的环境
        lua_pushthread(L);
        lua_insert(L, -2);
        lua_setfenv(L, -2);
        return 0;
    }
    else if (lua_iscfunction(L, -2) || lua_setfenv(L, -2) == 0)
    {
        // 无法修改C函数环境或设置失败
        luaL_error(L,
              LUA_QL("setfenv") " cannot change environment of given object");
    }
    
    return 1;
}


/*
** [原始操作] rawequal 函数实现
**
** 功能说明：
** 比较两个值是否相等，不触发元方法
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：第一个比较值
** 栈参数2：第二个比较值
**
** 返回值：
** @return int：返回1个值（布尔值）
*/
static int luaB_rawequal (lua_State *L) 
{
    // 检查两个参数都存在
    luaL_checkany(L, 1);
    luaL_checkany(L, 2);
    
    // 进行原始比较并返回结果
    lua_pushboolean(L, lua_rawequal(L, 1, 2));
    return 1;
}


/*
** [原始操作] rawget 函数实现
**
** 功能说明：
** 从表中获取值，不触发 __index 元方法
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：表对象
** 栈参数2：键值
**
** 返回值：
** @return int：返回1个值（获取到的值）
*/
static int luaB_rawget (lua_State *L) 
{
    // 检查第一个参数必须是表
    luaL_checktype(L, 1, LUA_TTABLE);
    
    // 检查第二个参数存在
    luaL_checkany(L, 2);
    
    // 只保留前两个参数
    lua_settop(L, 2);
    
    // 进行原始获取操作
    lua_rawget(L, 1);
    return 1;
}

/*
** [原始操作] rawset 函数实现
**
** 功能说明：
** 向表中设置值，不触发 __newindex 元方法
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：表对象
** 栈参数2：键值
** 栈参数3：要设置的值
**
** 返回值：
** @return int：返回1个值（设置后的表）
*/
static int luaB_rawset (lua_State *L) 
{
    // 检查第一个参数必须是表
    luaL_checktype(L, 1, LUA_TTABLE);
    
    // 检查第二和第三个参数存在
    luaL_checkany(L, 2);
    luaL_checkany(L, 3);
    
    // 只保留前三个参数
    lua_settop(L, 3);
    
    // 进行原始设置操作
    lua_rawset(L, 1);
    return 1;
}


/*
** [垃圾回收] gcinfo 函数实现
**
** 功能说明：
** 获取当前垃圾回收器的内存使用量（以KB为单位）
** 这是一个兼容性函数，建议使用 collectgarbage("count")
**
** 返回值：
** @return int：返回1个值（内存使用量的整数部分）
*/
static int luaB_gcinfo (lua_State *L) 
{
    lua_pushinteger(L, lua_getgccount(L));
    return 1;
}


/*
** [垃圾回收] collectgarbage 函数实现
**
** 功能说明：
** 控制垃圾回收器的行为
**
** 支持的操作：
** - "stop": 停止垃圾回收
** - "restart": 重启垃圾回收
** - "collect": 执行完整的垃圾回收周期
** - "count": 返回内存使用量（KB）
** - "step": 执行一步增量垃圾回收
** - "setpause": 设置垃圾回收暂停值
** - "setstepmul": 设置垃圾回收步进倍数
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：操作类型字符串（可选，默认"collect"）
** 栈参数2：额外参数（可选，默认0）
**
** 返回值：
** @return int：返回1个值（根据操作类型而定）
*/
static int luaB_collectgarbage (lua_State *L) 
{
    // 支持的操作选项
    static const char *const opts[] = {"stop", "restart", "collect",
        "count", "step", "setpause", "setstepmul", NULL};
    
    // 对应的操作码
    static const int optsnum[] = {LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT,
        LUA_GCCOUNT, LUA_GCSTEP, LUA_GCSETPAUSE, LUA_GCSETSTEPMUL};
    
    // 获取操作类型，默认为"collect"
    int o = luaL_checkoption(L, 1, "collect", opts);
    
    // 获取额外参数，默认为0
    int ex = luaL_optint(L, 2, 0);
    
    // 执行垃圾回收操作
    int res = lua_gc(L, optsnum[o], ex);
    
    // 根据操作类型返回不同结果
    switch (optsnum[o]) 
    {
        case LUA_GCCOUNT: 
        {
            // 获取精确的内存使用量（包含小数部分）
            int b = lua_gc(L, LUA_GCCOUNTB, 0);
            lua_pushnumber(L, res + ((lua_Number)b/1024));
            return 1;
        }
        case LUA_GCSTEP: 
        {
            // 返回是否完成了完整的回收周期
            lua_pushboolean(L, res);
            return 1;
        }
        default: 
        {
            // 其他操作返回数值结果
            lua_pushnumber(L, res);
            return 1;
        }
    }
}


/*
** [类型检测] type 函数实现
**
** 功能说明：
** 返回给定值的类型名称字符串
**
** 可能的返回值：
** "nil", "boolean", "number", "string", "function", "userdata", "thread", "table"
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要检测类型的值
**
** 返回值：
** @return int：返回1个值（类型名称字符串）
*/
static int luaB_type (lua_State *L) 
{
    // 检查参数存在
    luaL_checkany(L, 1);
    
    // 获取类型名称并推入栈
    lua_pushstring(L, luaL_typename(L, 1));
    return 1;
}


/*
** [表遍历] next 函数实现
**
** 功能说明：
** 返回表中的下一个键值对，用于实现通用表遍历
**
** 遍历规则：
** - 如果没有提供键或键为nil，返回第一个键值对
** - 如果提供了键，返回该键之后的下一个键值对
** - 如果已经是最后一个键，返回nil
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要遍历的表
** 栈参数2：当前键（可选）
**
** 返回值：
** @return int：返回1或2个值（下一个键值对或nil）
*/
static int luaB_next (lua_State *L) 
{
    // 检查第一个参数必须是表
    luaL_checktype(L, 1, LUA_TTABLE);
    
    // 确保有第二个参数（如果没有则创建nil）
    lua_settop(L, 2);
    
    if (lua_next(L, 1))
    {
        // 找到下一个键值对，返回键和值
        return 2;
    }
    else 
    {
        // 没有更多键值对，返回nil
        lua_pushnil(L);
        return 1;
    }
}


/*
** [表遍历] pairs 函数实现
**
** 功能说明：
** 返回用于遍历表的迭代器函数、状态和初始值
** 用于 for k,v in pairs(t) do ... end 语法
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要遍历的表
**
** 返回值：
** @return int：返回3个值（迭代器函数、状态表、初始键nil）
*/
static int luaB_pairs (lua_State *L) 
{
    // 检查参数必须是表
    luaL_checktype(L, 1, LUA_TTABLE);
    
    // 返回迭代器函数（next函数）
    lua_pushvalue(L, lua_upvalueindex(1));
    
    // 返回状态（表本身）
    lua_pushvalue(L, 1);
    
    // 返回初始值（nil）
    lua_pushnil(L);
    
    return 3;
}


/*
** [表遍历] ipairs 辅助函数
**
** 功能说明：
** ipairs 的迭代器函数，用于按数字索引遍历数组部分
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：数组表
** 栈参数2：当前索引
**
** 返回值：
** @return int：返回0或2个值（下一个索引和值，或无返回值表示结束）
*/
static int ipairsaux (lua_State *L) 
{
    // 获取当前索引
    int i = luaL_checkint(L, 2);
    
    // 检查第一个参数必须是表
    luaL_checktype(L, 1, LUA_TTABLE);
    
    // 计算下一个索引
    i++;
    
    // 推入下一个索引
    lua_pushinteger(L, i);
    
    // 获取该索引对应的值
    lua_rawgeti(L, 1, i);
    
    // 如果值为nil，表示遍历结束
    return (lua_isnil(L, -1)) ? 0 : 2;
}


/*
** [表遍历] ipairs 函数实现
**
** 功能说明：
** 返回用于按数字索引遍历数组的迭代器函数、状态和初始值
** 用于 for i,v in ipairs(t) do ... end 语法
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要遍历的表
**
** 返回值：
** @return int：返回3个值（迭代器函数、状态表、初始索引0）
*/
static int luaB_ipairs (lua_State *L) 
{
    // 检查参数必须是表
    luaL_checktype(L, 1, LUA_TTABLE);
    
    // 返回迭代器函数（ipairsaux）
    lua_pushvalue(L, lua_upvalueindex(1));
    
    // 返回状态（表本身）
    lua_pushvalue(L, 1);
    
    // 返回初始值（索引0）
    lua_pushinteger(L, 0);
    
    return 3;
}


/*
** [代码加载] 加载辅助函数
**
** 功能说明：
** 处理代码加载的结果，统一错误处理格式
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param status - int：加载状态（0表示成功）
**
** 返回值：
** @return int：成功返回1个值（编译后的函数），失败返回2个值（nil和错误消息）
*/
static int load_aux (lua_State *L, int status) 
{
    if (status == 0)
    {
        // 加载成功，返回编译后的函数
        return 1;
    }
    else 
    {
        // 加载失败，返回nil和错误消息
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
}


/*
** [代码加载] loadstring 函数实现
**
** 功能说明：
** 将字符串编译为Lua函数
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要编译的Lua代码字符串
** 栈参数2：chunk名称（可选，用于错误报告）
**
** 返回值：
** @return int：成功返回函数，失败返回nil和错误消息
*/
static int luaB_loadstring (lua_State *L) 
{
    size_t l;
    
    // 获取代码字符串和长度
    const char *s = luaL_checklstring(L, 1, &l);
    
    // 获取chunk名称，默认使用代码字符串本身
    const char *chunkname = luaL_optstring(L, 2, s);
    
    // 编译字符串并返回结果
    return load_aux(L, luaL_loadbuffer(L, s, l, chunkname));
}


/*
** [代码加载] loadfile 函数实现
**
** 功能说明：
** 从文件加载并编译Lua代码
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：文件名（可选，NULL表示从标准输入读取）
**
** 返回值：
** @return int：成功返回函数，失败返回nil和错误消息
*/
static int luaB_loadfile (lua_State *L) 
{
    // 获取文件名，NULL表示标准输入
    const char *fname = luaL_optstring(L, 1, NULL);
    
    // 从文件加载并返回结果
    return load_aux(L, luaL_loadfile(L, fname));
}


/*
** [代码加载] 通用读取器函数
**
** 功能说明：
** 用于通用 load 函数的读取器，lua_load 使用栈进行内部操作，
** 所以读取器不能改变栈顶。它将结果字符串保存在栈的保留位置中。
**
** 工作机制：
** 1. 调用用户提供的读取器函数
** 2. 如果返回nil，表示读取结束
** 3. 如果返回字符串，将其保存到保留栈位置
** 4. 如果返回其他类型，报错
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param ud - void*：用户数据（未使用）
** @param size - size_t*：返回的字符串长度
**
** 返回值：
** @return const char*：读取到的字符串数据，NULL表示结束
*/
static const char *generic_reader (lua_State *L, void *ud, size_t *size) 
{
    // 避免未使用参数的警告
    (void)ud;
    
    // 检查栈空间，防止嵌套层次过深
    luaL_checkstack(L, 2, "too many nested functions");
    
    // 获取读取器函数
    lua_pushvalue(L, 1);
    
    // 调用读取器函数
    lua_call(L, 0, 1);
    
    if (lua_isnil(L, -1)) 
    {
        // 返回nil表示读取结束
        *size = 0;
        return NULL;
    }
    else if (lua_isstring(L, -1)) 
    {
        // 将字符串保存到保留的栈位置
        lua_replace(L, 3);
        
        // 返回字符串数据和长度
        return lua_tolstring(L, 3, size);
    }
    else 
    {
        // 读取器函数必须返回字符串
        luaL_error(L, "reader function must return a string");
    }
    
    // 避免警告的返回语句
    return NULL;
}


/*
** [代码加载] load 函数实现
**
** 功能说明：
** 使用读取器函数动态加载Lua代码
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：读取器函数
** 栈参数2：chunk名称（可选，默认"=(load)"）
**
** 返回值：
** @return int：成功返回函数，失败返回nil和错误消息
*/
static int luaB_load (lua_State *L) 
{
    int status;
    
    // 获取chunk名称，默认为"=(load)"
    const char *cname = luaL_optstring(L, 2, "=(load)");
    
    // 检查第一个参数必须是函数
    luaL_checktype(L, 1, LUA_TFUNCTION);
    
    // 设置栈：函数、可能的名称，加上一个保留位置
    lua_settop(L, 3);
    
    // 使用通用读取器加载代码
    status = lua_load(L, generic_reader, NULL, cname);
    
    // 返回加载结果
    return load_aux(L, status);
}


/*
** [代码加载] dofile 函数实现
**
** 功能说明：
** 加载并立即执行Lua文件
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：文件名（可选，NULL表示标准输入）
**
** 返回值：
** @return int：返回执行结果的所有返回值
*/
static int luaB_dofile (lua_State *L) 
{
    // 获取文件名
    const char *fname = luaL_optstring(L, 1, NULL);
    
    // 记录当前栈大小
    int n = lua_gettop(L);
    
    // 加载文件，如果失败则抛出错误
    if (luaL_loadfile(L, fname) != 0) 
    {
        lua_error(L);
    }
    
    // 执行加载的函数
    lua_call(L, 0, LUA_MULTRET);
    
    // 返回执行结果的数量
    return lua_gettop(L) - n;
}


/*
** [错误处理] assert 函数实现
**
** 功能说明：
** 断言函数，如果条件为假则抛出错误
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要检查的条件值
** 栈参数2：错误消息（可选，默认"assertion failed!"）
**
** 返回值：
** @return int：如果断言成功，返回所有参数；如果失败，抛出错误
*/
static int luaB_assert (lua_State *L) 
{
    // 检查至少有一个参数
    luaL_checkany(L, 1);
    
    // 如果第一个参数为假值，抛出错误
    if (!lua_toboolean(L, 1))
    {
        return luaL_error(L, "%s", luaL_optstring(L, 2, "assertion failed!"));
    }
    
    // 断言成功，返回所有参数
    return lua_gettop(L);
}


/*
** [工具函数] unpack 函数实现
**
** 功能说明：
** 将数组表的元素展开为多个返回值
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要展开的数组表
** 栈参数2：起始索引（可选，默认1）
** 栈参数3：结束索引（可选，默认表长度）
**
** 返回值：
** @return int：返回指定范围内的所有数组元素
*/
static int luaB_unpack (lua_State *L) 
{
    int i, e, n;
    
    // 检查第一个参数必须是表
    luaL_checktype(L, 1, LUA_TTABLE);
    
    // 获取起始索引，默认为1
    i = luaL_optint(L, 2, 1);
    
    // 获取结束索引，默认为表长度
    e = luaL_opt(L, luaL_checkint, 3, luaL_getn(L, 1));
    
    // 如果起始索引大于结束索引，返回空范围
    if (i > e) 
    {
        return 0;
    }
    
    // 计算元素数量
    n = e - i + 1;
    
    // 检查是否会算术溢出或栈空间不足
    if (n <= 0 || !lua_checkstack(L, n))
    {
        return luaL_error(L, "too many results to unpack");
    }
    
    // 推入第一个元素（避免溢出问题）
    lua_rawgeti(L, 1, i);
    
    // 推入剩余元素
    while (i++ < e)
    {
        lua_rawgeti(L, 1, i);
    }
    
    return n;
}


/*
** [工具函数] select 函数实现
**
** 功能说明：
** 从参数列表中选择特定范围的参数
**
** 使用模式：
** - select("#", ...) 返回参数总数
** - select(n, ...) 返回从第n个参数开始的所有参数
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：选择器（"#"或数字索引）
** 栈参数2+：要选择的参数列表
**
** 返回值：
** @return int：返回选中的参数或参数数量
*/
static int luaB_select (lua_State *L) 
{
    // 获取总参数数量
    int n = lua_gettop(L);
    
    if (lua_type(L, 1) == LUA_TSTRING && *lua_tostring(L, 1) == '#') 
    {
        // 返回参数数量（不包括选择器本身）
        lua_pushinteger(L, n-1);
        return 1;
    }
    else 
    {
        // 获取起始索引
        int i = luaL_checkint(L, 1);
        
        // 处理负数索引
        if (i < 0) 
        {
            i = n + i;
        }
        else if (i > n) 
        {
            i = n;
        }
        
        // 检查索引范围
        luaL_argcheck(L, 1 <= i, 1, "index out of range");
        
        // 返回从索引i开始的所有参数
        return n - i;
    }
}


/*
** [错误处理] pcall 函数实现
**
** 功能说明：
** 在保护模式下调用函数，捕获可能的错误
**
** 返回格式：
** - 成功：true, 函数返回值...
** - 失败：false, 错误消息
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要调用的函数
** 栈参数2+：传递给函数的参数
**
** 返回值：
** @return int：返回状态标志加上所有结果
*/
static int luaB_pcall (lua_State *L) 
{
    int status;
    
    // 检查至少有一个参数（要调用的函数）
    luaL_checkany(L, 1);
    
    // 在保护模式下调用函数
    status = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);
    
    // 推入状态标志（true表示成功，false表示失败）
    lua_pushboolean(L, (status == 0));
    
    // 将状态标志移到栈底
    lua_insert(L, 1);
    
    // 返回状态加上所有结果
    return lua_gettop(L);
}


/*
** [错误处理] xpcall 函数实现
**
** 功能说明：
** 在保护模式下调用函数，使用自定义错误处理函数
**
** 返回格式：
** - 成功：true, 函数返回值...
** - 失败：false, 错误处理函数的返回值
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要调用的函数
** 栈参数2：错误处理函数
**
** 返回值：
** @return int：返回状态标志加上所有结果
*/
static int luaB_xpcall (lua_State *L) 
{
    int status;
    
    // 检查第二个参数（错误处理函数）存在
    luaL_checkany(L, 2);
    
    // 只保留前两个参数
    lua_settop(L, 2);
    
    // 将错误处理函数移到栈底
    lua_insert(L, 1);
    
    // 在保护模式下调用函数，使用位置1的错误处理函数
    status = lua_pcall(L, 0, LUA_MULTRET, 1);
    
    // 推入状态标志
    lua_pushboolean(L, (status == 0));
    
    // 将状态标志移到栈底，替换错误处理函数
    lua_replace(L, 1);
    
    // 返回状态加上所有结果
    return lua_gettop(L);
}


static int luaB_tostring (lua_State *L) 
{
    luaL_checkany(L, 1);
    if (luaL_callmeta(L, 1, "__tostring"))
    {
        // 使用元方法返回值
        return 1;
    }
    
    switch (lua_type(L, 1)) 
    {
        case LUA_TNUMBER:
            lua_pushstring(L, lua_tostring(L, 1));
            break;
        case LUA_TSTRING:
            lua_pushvalue(L, 1);
            break;
        case LUA_TBOOLEAN:
            lua_pushstring(L, (lua_toboolean(L, 1) ? "true" : "false"));
            break;
        case LUA_TNIL:
            lua_pushliteral(L, "nil");
            break;
        default:
            lua_pushfstring(L, "%s: %p", luaL_typename(L, 1), lua_topointer(L, 1));
            break;
    }
    return 1;
}


static int luaB_newproxy (lua_State *L) 
{
    lua_settop(L, 1);
    
    // 创建代理用户数据
    lua_newuserdata(L, 0);
    
    if (lua_toboolean(L, 1) == 0)
    {
        // 没有元表
        return 1;
    }
    else if (lua_isboolean(L, 1)) 
    {
        // 创建新元表并标记为有效
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_pushboolean(L, 1);
        lua_rawset(L, lua_upvalueindex(1));
    }
    else 
    {
        int validproxy = 0;
        
        // 检查是否为有效代理
        if (lua_getmetatable(L, 1)) 
        {
            lua_rawget(L, lua_upvalueindex(1));
            validproxy = lua_toboolean(L, -1);
            lua_pop(L, 1);
        }
        
        luaL_argcheck(L, validproxy, 1, "boolean or proxy expected");
        lua_getmetatable(L, 1);
    }
    
    lua_setmetatable(L, 2);
    return 1;
}


/*
** [函数注册表] 基础库函数注册表
**
** 功能说明：
** 定义所有基础库函数的注册信息，用于将C函数导出到Lua环境
**
** 注册的函数包括：
** - 断言和错误处理：assert, error, pcall, xpcall
** - 垃圾回收控制：collectgarbage, gcinfo
** - 文件操作：dofile, loadfile, load, loadstring
** - 环境操作：getfenv, setfenv
** - 元表操作：getmetatable, setmetatable
** - 表遍历：next, pairs, ipairs（pairs和ipairs通过auxopen单独处理）
** - 原始操作：rawequal, rawget, rawset
** - 基础输出：print
** - 类型相关：type, tonumber, tostring
** - 工具函数：select, unpack
*/
static const luaL_Reg base_funcs[] = 
{
    {"assert", luaB_assert},                    // 断言函数
    {"collectgarbage", luaB_collectgarbage},    // 垃圾回收控制
    {"dofile", luaB_dofile},                    // 执行Lua文件
    {"error", luaB_error},                      // 抛出错误
    {"gcinfo", luaB_gcinfo},                    // 获取GC信息（兼容函数）
    {"getfenv", luaB_getfenv},                  // 获取函数环境
    {"getmetatable", luaB_getmetatable},        // 获取元表
    {"loadfile", luaB_loadfile},                // 从文件加载代码
    {"load", luaB_load},                        // 从读取器加载代码
    {"loadstring", luaB_loadstring},            // 从字符串加载代码
    {"next", luaB_next},                        // 表遍历函数
    {"pcall", luaB_pcall},                      // 保护模式调用
    {"print", luaB_print},                      // 打印函数
    {"rawequal", luaB_rawequal},                // 原始相等比较
    {"rawget", luaB_rawget},                    // 原始表获取
    {"rawset", luaB_rawset},                    // 原始表设置
    {"select", luaB_select},                    // 参数选择函数
    {"setfenv", luaB_setfenv},                  // 设置函数环境
    {"setmetatable", luaB_setmetatable},        // 设置元表
    {"tonumber", luaB_tonumber},                // 转换为数字
    {"tostring", luaB_tostring},                // 转换为字符串
    {"type", luaB_type},                        // 获取类型
    {"unpack", luaB_unpack},                    // 解包数组
    {"xpcall", luaB_xpcall},                    // 带错误处理的保护调用
    {NULL, NULL}                                // 结束标记
};


/*
** {======================================================
** [协程库] 协程相关功能实现
** =======================================================
*/

// 协程状态常量定义
#define CO_RUN	0	// 运行中
#define CO_SUS	1	// 挂起
#define CO_NOR	2	// 正常（它恢复了另一个协程）
#define CO_DEAD	3	// 死亡

// 协程状态名称字符串数组
static const char *const statnames[] =
    {"running", "suspended", "normal", "dead"};

/*
** [协程] 获取协程状态
**
** 功能说明：
** 检测协程的当前状态
**
** 状态判断逻辑：
** - 如果是当前线程，返回运行中
** - 根据 lua_status 返回值判断具体状态
**
** 参数说明：
** @param L - lua_State*：当前Lua状态机
** @param co - lua_State*：要检查的协程
**
** 返回值：
** @return int：协程状态常量
*/
static int costatus (lua_State *L, lua_State *co) 
{
    // 如果是同一个状态机，表示正在运行
    if (L == co) 
    {
        return CO_RUN;
    }
    
    // 根据协程状态判断
    switch (lua_status(co)) 
    {
        case LUA_YIELD:
            // 协程被挂起
            return CO_SUS;
            
        case 0: 
        {
            lua_Debug ar;
            
            // 检查是否有调用栈帧
            if (lua_getstack(co, 0, &ar) > 0)
            {
                // 有栈帧表示正在运行
                return CO_NOR;
            }
            else if (lua_gettop(co) == 0)
            {
                // 栈为空表示已经执行完毕
                return CO_DEAD;
            }
            else
            {
                // 有值但没有栈帧，表示初始状态
                return CO_SUS;
            }
        }
        
        default:
            // 发生错误，协程死亡
            return CO_DEAD;
    }
}


/*
** [协程状态] coroutine.status 函数实现
**
** 功能说明：
** 获取指定协程的当前状态字符串
**
** 可能的状态：
** - "running": 协程正在运行（当前协程）
** - "suspended": 协程被挂起（可以被恢复）
** - "normal": 协程处于正常状态（恢复了另一个协程）
** - "dead": 协程已死亡（执行完毕或发生错误）
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要查询状态的协程
**
** 返回值：
** @return int：返回1个值（状态字符串）
*/
static int luaB_costatus (lua_State *L) 
{
    // 获取协程参数
    lua_State *co = lua_tothread(L, 1);
    
    // 检查参数必须是协程
    luaL_argcheck(L, co, 1, "coroutine expected");
    
    // 获取协程状态并转换为字符串
    lua_pushstring(L, statnames[costatus(L, co)]);
    return 1;
}


/*
** [协程恢复] 协程恢复辅助函数
**
** 功能说明：
** 恢复指定协程的执行，这是 coroutine.resume 和协程包装器的核心实现
**
** 恢复流程：
** 1. 检查协程状态是否可以恢复
** 2. 检查栈空间是否足够
** 3. 传递参数并恢复协程执行
** 4. 处理恢复结果
**
** 参数说明：
** @param L - lua_State*：当前Lua状态机
** @param co - lua_State*：要恢复的协程
** @param narg - int：传递给协程的参数数量
**
** 返回值：
** @return int：成功返回结果数量，失败返回-1
*/
static int auxresume (lua_State *L, lua_State *co, int narg) 
{
    // 获取协程当前状态
    int status = costatus(L, co);
    
    // 检查协程栈空间是否足够容纳参数
    if (!lua_checkstack(co, narg))
    {
        luaL_error(L, "too many arguments to resume");
    }
    
    // 只有挂起状态的协程可以被恢复
    if (status != CO_SUS) 
    {
        lua_pushfstring(L, "cannot resume %s coroutine", statnames[status]);
        return -1;
    }
    
    // 将参数从当前状态机移动到协程
    lua_xmove(L, co, narg);
    
    // 设置协程的调用层级
    lua_setlevel(L, co);
    
    // 恢复协程执行
    status = lua_resume(co, narg);
    
    // 处理恢复结果
    if (status == 0 || status == LUA_YIELD) 
    {
        // 协程正常执行或者主动让出
        int nres = lua_gettop(co);
        
        // 检查当前状态机是否有足够空间容纳结果
        if (!lua_checkstack(L, nres + 1))
        {
            luaL_error(L, "too many results to resume");
        }
        
        // 将结果从协程移动到当前状态机
        lua_xmove(co, L, nres);
        return nres;
    }
    else 
    {
        // 协程执行出错，移动错误消息
        lua_xmove(co, L, 1);
        return -1;
    }
}


/*
** [协程恢复] coroutine.resume 函数实现
**
** 功能说明：
** 恢复协程执行的标准接口
**
** 返回格式：
** - 成功：true, 协程返回值...
** - 失败：false, 错误消息
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：要恢复的协程
** 栈参数2+：传递给协程的参数
**
** 返回值：
** @return int：返回状态标志加上所有结果
*/
static int luaB_coresume (lua_State *L) 
{
    // 获取协程参数
    lua_State *co = lua_tothread(L, 1);
    int r;
    
    // 检查第一个参数必须是协程
    luaL_argcheck(L, co, 1, "coroutine expected");
    
    // 调用辅助函数恢复协程，传递除协程外的所有参数
    r = auxresume(L, co, lua_gettop(L) - 1);
    
    if (r < 0) 
    {
        // 恢复失败，返回 false 和错误消息
        lua_pushboolean(L, 0);
        lua_insert(L, -2);
        return 2;
    }
    else 
    {
        // 恢复成功，返回 true 和所有结果
        lua_pushboolean(L, 1);
        lua_insert(L, -(r + 1));
        return r + 1;
    }
}


/*
** [协程包装] 协程包装器辅助函数
**
** 功能说明：
** 协程包装器的核心实现，提供更简洁的协程调用方式
** 与 coroutine.resume 不同，包装器不返回状态标志，直接抛出错误
**
** 错误处理：
** - 成功：直接返回协程的所有返回值
** - 失败：抛出错误（带调用位置信息）
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数：传递给协程的所有参数
**
** 返回值：
** @return int：返回协程的所有返回值，或抛出错误
*/
static int luaB_auxwrap (lua_State *L) 
{
    // 从上值中获取协程对象
    lua_State *co = lua_tothread(L, lua_upvalueindex(1));
    
    // 恢复协程执行，传递所有参数
    int r = auxresume(L, co, lua_gettop(L));
    
    if (r < 0) 
    {
        // 恢复失败，处理错误
        if (lua_isstring(L, -1)) 
        {
            // 如果错误对象是字符串，添加调用位置信息
            luaL_where(L, 1);
            lua_insert(L, -2);
            lua_concat(L, 2);
        }
        
        // 抛出错误
        lua_error(L);
    }
    
    // 恢复成功，返回所有结果
    return r;
}


/*
** [协程创建] coroutine.create 函数实现
**
** 功能说明：
** 创建一个新的协程，以给定的Lua函数作为主体
**
** 创建规则：
** - 函数参数必须是Lua函数（不能是C函数）
** - 返回新创建的协程线程对象
** - 新协程处于挂起状态，需要通过resume启动
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：协程的主体函数（必须是Lua函数）
**
** 返回值：
** @return int：返回1个值（新创建的协程）
*/
static int luaB_cocreate (lua_State *L) 
{
    // 创建新的协程线程
    lua_State *NL = lua_newthread(L);
    
    // 检查参数必须是Lua函数（不能是C函数）
    luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1), 1,
        "Lua function expected");
    
    // 复制函数到栈顶
    lua_pushvalue(L, 1);
    
    // 将函数从当前状态机移动到新协程
    lua_xmove(L, NL, 1);
    
    return 1;
}


/*
** [协程包装] coroutine.wrap 函数实现
**
** 功能说明：
** 创建协程包装器函数，提供更简洁的协程使用方式
**
** 包装器特点：
** - 返回一个函数而不是协程对象
** - 调用返回的函数相当于resume协程
** - 不返回状态标志，错误时直接抛出异常
** - 使用闭包保存协程对象
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数1：协程的主体函数（必须是Lua函数）
**
** 返回值：
** @return int：返回1个值（包装器函数）
*/
static int luaB_cowrap (lua_State *L) 
{
    // 先创建协程（复用cocreate的逻辑）
    luaB_cocreate(L);
    
    // 创建闭包，将协程作为上值，luaB_auxwrap作为函数体
    lua_pushcclosure(L, luaB_auxwrap, 1);
    
    return 1;
}


/*
** [协程让出] coroutine.yield 函数实现
**
** 功能说明：
** 挂起当前协程的执行，将控制权返回给调用者
**
** 让出机制：
** - 只能在协程内部调用
** - 可以传递任意数量的值给调用者
** - 协程状态变为挂起，等待下次resume
** - 传递的值将作为resume的返回值
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** 栈参数：要传递给调用者的所有值
**
** 返回值：
** @return int：此函数不会正常返回，而是挂起协程
*/
static int luaB_yield (lua_State *L) 
{
    // 挂起当前协程，传递栈上的所有参数
    return lua_yield(L, lua_gettop(L));
}


/*
** [当前协程] coroutine.running 函数实现
**
** 功能说明：
** 获取当前正在运行的协程
**
** 返回规则：
** - 如果在协程中调用，返回当前协程对象
** - 如果在主线程中调用，返回nil
** - 用于判断代码是否在协程环境中执行
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：返回1个值（当前协程或nil）
*/
static int luaB_corunning (lua_State *L) 
{
    // 将当前线程推入栈，如果返回true表示是主线程
    if (lua_pushthread(L))
    {
        // 主线程不是协程，返回nil
        lua_pushnil(L);
    }
    
    // 否则返回当前协程对象
    return 1;
}


/*
** [协程注册表] 协程库函数注册表
**
** 功能说明：
** 定义所有协程相关函数的注册信息，这些函数将被注册到coroutine表中
**
** 注册的协程函数包括：
** - create: 创建新协程
** - resume: 恢复协程执行
** - running: 获取当前运行的协程
** - status: 获取协程状态
** - wrap: 创建协程包装函数
** - yield: 挂起当前协程
*/
static const luaL_Reg co_funcs[] = 
{
    {"create", luaB_cocreate},      // 创建协程
    {"resume", luaB_coresume},      // 恢复协程
    {"running", luaB_corunning},    // 获取当前协程
    {"status", luaB_costatus},      // 获取协程状态
    {"wrap", luaB_cowrap},          // 创建协程包装器
    {"yield", luaB_yield},          // 挂起协程
    {NULL, NULL}                    // 结束标记
};


/*
** [辅助注册] 带上值的函数注册辅助函数
**
** 功能说明：
** 注册需要上值（upvalue）的函数到指定表中
** 主要用于注册pairs和ipairs这类需要辅助函数作为上值的函数
**
** 注册流程：
** 1. 将辅助函数u推入栈作为上值
** 2. 创建以f为主体、u为上值的闭包
** 3. 将闭包注册到表中，名称为name
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
** @param name - const char*：函数在Lua中的名称
** @param f - lua_CFunction：主函数（如luaB_pairs）
** @param u - lua_CFunction：辅助函数（如luaB_next）
*/
static void auxopen (lua_State *L, const char *name,
                     lua_CFunction f, lua_CFunction u) 
{
    // 推入辅助函数作为上值
    lua_pushcfunction(L, u);
    
    // 创建闭包：主函数f + 上值u
    lua_pushcclosure(L, f, 1);
    
    // 将闭包注册到表中
    lua_setfield(L, -2, name);
}


/*
** [基础库初始化] 基础库开放函数
**
** 功能说明：
** 初始化并注册所有基础库函数到Lua全局环境
**
** 初始化流程：
** 1. 设置全局变量_G指向全局表自身
** 2. 注册所有基础库函数到全局表
** 3. 设置Lua版本信息到_VERSION
** 4. 特殊处理需要上值的函数（pairs, ipairs）
** 5. 初始化newproxy函数及其弱表环境
**
** 特殊函数处理：
** - pairs/ipairs: 需要next/ipairsaux作为上值
** - newproxy: 需要弱表作为上值来跟踪代理对象
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
*/
static void base_open (lua_State *L) 
{
    // 设置全局 _G 指向全局表自身
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setglobal(L, "_G");
    
    // 将基础库函数注册到全局表
    luaL_register(L, "_G", base_funcs);
    
    // 设置Lua版本信息
    lua_pushliteral(L, LUA_VERSION);
    lua_setglobal(L, "_VERSION");
    
    // 注册需要辅助函数作为上值的迭代器
    // ipairs 使用 ipairsaux 作为上值
    auxopen(L, "ipairs", luaB_ipairs, ipairsaux);
    
    // pairs 使用 luaB_next 作为上值
    auxopen(L, "pairs", luaB_pairs, luaB_next);
    
    // 为 newproxy 创建弱表环境
    lua_createtable(L, 0, 1);           // 创建新表
    lua_pushvalue(L, -1);               // 复制表作为元表
    lua_setmetatable(L, -2);            // 设置表的元表为自身
    lua_pushliteral(L, "kv");           // 设置弱引用模式
    lua_setfield(L, -2, "__mode");      // metatable.__mode = "kv"
    
    // 创建newproxy闭包并注册
    lua_pushcclosure(L, luaB_newproxy, 1);
    lua_setglobal(L, "newproxy");
}


/*
** [库入口] 基础库主入口函数
**
** 功能说明：
** Lua基础库的主要入口点，按照Lua库加载约定实现
** 当通过require或直接调用时，此函数负责完整初始化基础库
**
** 初始化内容：
** 1. 调用base_open初始化所有基础函数
** 2. 注册协程库函数到coroutine表
** 3. 返回加载的库数量
**
** 返回值说明：
** 返回2表示加载了两个库：
** - 基础库（全局函数）
** - 协程库（coroutine表）
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针
**
** 返回值：
** @return int：返回2（表示加载了2个库）
*/
LUALIB_API int luaopen_base (lua_State *L) 
{
    // 初始化基础库（全局函数）
    base_open(L);
    
    // 注册协程库函数到coroutine表
    luaL_register(L, LUA_COLIBNAME, co_funcs);
    
    // 返回加载的库数量：基础库 + 协程库
    return 2;
}

