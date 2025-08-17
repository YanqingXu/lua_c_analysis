# Lua 基础库详解

## 概述

Lua 基础库（lbaselib.c）提供了 Lua 语言的核心函数，包括类型转换、元表操作、环境操作、垃圾回收控制等基本功能。这些函数构成了 Lua 标准环境的基础。

## 基础库函数

### 1. 输出函数

#### print(...)
打印任意数量的值到标准输出。

```c
static int luaB_print (lua_State *L) {
  int n = lua_gettop(L);  // 获取参数数量
  int i;
  lua_getglobal(L, "tostring");  // 获取 tostring 函数
  
  for (i=1; i<=n; i++) {
    const char *s;
    lua_pushvalue(L, -1);  // 复制 tostring 函数
    lua_pushvalue(L, i);   // 获取要打印的值
    lua_call(L, 1, 1);     // 调用 tostring
    s = lua_tostring(L, -1);  // 获取字符串结果
    
    if (s == NULL)
      return luaL_error(L, LUA_QL("tostring") " must return a string to "
                           LUA_QL("print"));
    
    if (i>1) fputs("\t", stdout);  // 添加制表符分隔
    fputs(s, stdout);
    lua_pop(L, 1);  // 弹出结果
  }
  fputs("\n", stdout);  // 添加换行符
  return 0;
}
```

**使用示例：**
```lua
print("Hello", "World", 123)  -- 输出: Hello	World	123
```

### 2. 类型转换函数

#### tonumber(e [, base])
将值转换为数字。

```c
static int luaB_tonumber (lua_State *L) {
  int base = luaL_optint(L, 2, 10);  // 默认十进制
  
  if (base == 10) {  // 标准转换
    luaL_checkany(L, 1);
    if (lua_isnumber(L, 1)) {
      lua_pushnumber(L, lua_tonumber(L, 1));
      return 1;
    }
  }
  else {  // 指定进制转换
    const char *s1 = luaL_checkstring(L, 1);
    char *s2;
    unsigned long n;
    
    luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    n = strtoul(s1, &s2, base);
    
    if (s1 != s2) {  // 至少有一个有效数字？
      while (isspace((unsigned char)(*s2))) s2++;  // 跳过尾部空格
      if (*s2 == '\0') {  // 没有无效的尾部字符？
        lua_pushnumber(L, (lua_Number)n);
        return 1;
      }
    }
  }
  lua_pushnil(L);  // 转换失败返回 nil
  return 1;
}
```

**使用示例：**
```lua
print(tonumber("123"))     -- 123
print(tonumber("ff", 16))  -- 255
print(tonumber("xyz"))     -- nil
```

#### tostring(e)
将值转换为字符串。这个函数实际上调用值的 `__tostring` 元方法。

```c
static int luaB_tostring (lua_State *L) {
  luaL_checkany(L, 1);
  if (luaL_callmeta(L, 1, "__tostring"))  // 尝试元方法
    return 1;  // 元方法已经将结果放在栈上
  
  switch (lua_type(L, 1)) {
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
```

### 3. 错误处理

#### error(message [, level])
抛出错误并停止当前函数的执行。

```c
static int luaB_error (lua_State *L) {
  int level = luaL_optint(L, 2, 1);  // 默认错误级别为 1
  lua_settop(L, 1);
  
  if (lua_isstring(L, 1) && level > 0) {  // 添加额外信息？
    luaL_where(L, level);  // 获取错误位置信息
    lua_pushvalue(L, 1);   // 错误消息
    lua_concat(L, 2);      // 连接位置和消息
  }
  return lua_error(L);  // 抛出错误
}
```

**使用示例：**
```lua
function check_positive(n)
  if n <= 0 then
    error("number must be positive", 2)  -- 错误指向调用者
  end
end
```

#### assert(v [, message])
检查条件，如果为假则抛出错误。

```c
static int luaB_assert (lua_State *L) {
  luaL_checkany(L, 1);
  if (!lua_toboolean(L, 1))
    return luaL_error(L, "%s", luaL_optstring(L, 2, "assertion failed!"));
  return lua_gettop(L);  // 返回所有参数
}
```

### 4. 元表操作

#### getmetatable(object)
获取对象的元表。

```c
static int luaB_getmetatable (lua_State *L) {
  luaL_checkany(L, 1);
  if (!lua_getmetatable(L, 1)) {
    lua_pushnil(L);
    return 1;  // 没有元表
  }
  luaL_getmetafield(L, 1, "__metatable");
  return 1;  // 返回 __metatable 字段或元表本身
}
```

#### setmetatable(table, metatable)
设置表的元表。

```c
static int luaB_setmetatable (lua_State *L) {
  int t = lua_type(L, 2);
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2,
                    "nil or table expected");
  
  if (luaL_getmetafield(L, 1, "__metatable"))
    luaL_error(L, "cannot change a protected metatable");
  
  lua_settop(L, 2);
  lua_setmetatable(L, 1);
  return 1;
}
```

**使用示例：**
```lua
local t = {}
local mt = {
  __tostring = function() return "custom table" end
}
setmetatable(t, mt)
print(t)  -- 输出: custom table
```

### 5. 原始操作

这些函数绕过元方法直接操作表：

#### rawget(table, index)
```c
static int luaB_rawget (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkany(L, 2);
  lua_settop(L, 2);
  lua_rawget(L, 1);
  return 1;
}
```

#### rawset(table, index, value)
```c
static int luaB_rawset (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkany(L, 2);
  luaL_checkany(L, 3);
  lua_settop(L, 3);
  lua_rawset(L, 1);
  return 1;
}
```

#### rawequal(v1, v2)
```c
static int luaB_rawequal (lua_State *L) {
  luaL_checkany(L, 1);
  luaL_checkany(L, 2);
  lua_pushboolean(L, lua_rawequal(L, 1, 2));
  return 1;
}
```

### 6. 环境操作

#### getfenv([f])
获取函数的环境表。

```c
static int luaB_getfenv (lua_State *L) {
  getfunc(L);  // 获取函数对象
  if (lua_iscfunction(L, -1))  // C 函数？
    lua_pushvalue(L, LUA_GLOBALSINDEX);  // 返回全局环境
  else
    lua_getfenv(L, -1);  // 获取 Lua 函数的环境
  return 1;
}
```

#### setfenv(f, table)
设置函数的环境表。

```c
static int luaB_setfenv (lua_State *L) {
  luaL_checktype(L, 2, LUA_TTABLE);
  getfunc(L);
  lua_pushvalue(L, 2);
  
  if (lua_isnumber(L, 1) && lua_tonumber(L, 1) == 0) {
    // 改变当前线程的环境
    lua_pushthread(L);
    lua_insert(L, -2);
    lua_setfenv(L, -2);
    return 0;
  }
  else if (lua_iscfunction(L, -2) || lua_setfenv(L, -2) == 0)
    luaL_error(L,
          LUA_QL("setfenv") " cannot change environment of given object");
  return 1;
}
```

### 7. 垃圾回收控制

#### collectgarbage([opt [, arg]])
控制垃圾回收器。

```c
static int luaB_collectgarbage (lua_State *L) {
  static const char *const opts[] = {"stop", "restart", "collect",
    "count", "step", "setpause", "setstepmul", NULL};
  static const int optsnum[] = {LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT,
    LUA_GCCOUNT, LUA_GCSTEP, LUA_GCSETPAUSE, LUA_GCSETSTEPMUL};
  
  int o = luaL_checkoption(L, 1, "collect", opts);
  int ex = luaL_optint(L, 2, 0);
  int res = lua_gc(L, optsnum[o], ex);
  
  switch (optsnum[o]) {
    case LUA_GCCOUNT: {
      int b = lua_gc(L, LUA_GCCOUNTB, 0);
      lua_pushnumber(L, res + ((lua_Number)b/1024));
      return 1;
    }
    case LUA_GCSTEP: {
      lua_pushboolean(L, res);
      return 1;
    }
    default: {
      lua_pushinteger(L, res);
      return 1;
    }
  }
}
```

**使用示例：**
```lua
collectgarbage("collect")    -- 执行完整的垃圾回收
collectgarbage("count")      -- 返回当前内存使用量(KB)
collectgarbage("stop")       -- 停止垃圾回收
collectgarbage("restart")    -- 重启垃圾回收
```

### 8. 迭代器函数

#### next(table [, index])
返回表中的下一个键值对。

```c
static int luaB_next (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 2);  // 创建第二个参数（如果不存在）
  if (lua_next(L, 1))
    return 2;
  else {
    lua_pushnil(L);
    return 1;
  }
}
```

#### pairs(t)
返回遍历表的迭代器。

```c
static int luaB_pairs (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushvalue(L, lua_upvalueindex(1));  // next 函数
  lua_pushvalue(L, 1);  // 表
  lua_pushnil(L);       // 起始键
  return 3;
}
```

#### ipairs(t)
返回遍历数组部分的迭代器。

```c
static int luaB_ipairs (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushvalue(L, lua_upvalueindex(1));  // ipairs 迭代函数
  lua_pushvalue(L, 1);  // 表
  lua_pushinteger(L, 0);  // 起始索引
  return 3;
}
```

### 9. 类型检查

#### type(v)
返回值的类型名称。

```c
static int luaB_type (lua_State *L) {
  luaL_checkany(L, 1);
  lua_pushstring(L, luaL_typename(L, 1));
  return 1;
}
```

**使用示例：**
```lua
print(type(nil))        -- "nil"
print(type(true))       -- "boolean"
print(type(123))        -- "number"
print(type("hello"))    -- "string"
print(type({}))         -- "table"
print(type(print))      -- "function"
```

### 10. 协程相关

虽然协程函数在单独的库中，但一些基础的协程函数也在基础库中：

#### pcall(f, arg1, ...)
保护模式调用函数。

```c
static int luaB_pcall (lua_State *L) {
  int status;
  luaL_checkany(L, 1);
  status = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);
  lua_pushboolean(L, (status == 0));
  lua_insert(L, 1);
  return lua_gettop(L);  // 返回状态 + 所有结果
}
```

#### xpcall(f, err)
带错误处理函数的保护调用。

```c
static int luaB_xpcall (lua_State *L) {
  int status;
  luaL_checkany(L, 2);
  lua_settop(L, 2);
  lua_insert(L, 1);  // 将错误函数放在函数下面
  status = lua_pcall(L, 0, LUA_MULTRET, 1);
  lua_pushboolean(L, (status == 0));
  lua_replace(L, 1);
  return lua_gettop(L);
}
```

## 库注册

基础库通过以下方式注册：

```c
static const luaL_Reg base_funcs[] = {
  {"assert", luaB_assert},
  {"collectgarbage", luaB_collectgarbage},
  {"dofile", luaB_dofile},
  {"error", luaB_error},
  {"gcinfo", luaB_gcinfo},
  {"getfenv", luaB_getfenv},
  {"getmetatable", luaB_getmetatable},
  {"loadfile", luaB_loadfile},
  {"loadstring", luaB_loadstring},
  {"next", luaB_next},
  {"pcall", luaB_pcall},
  {"print", luaB_print},
  {"rawequal", luaB_rawequal},
  {"rawget", luaB_rawget},
  {"rawset", luaB_rawset},
  {"select", luaB_select},
  {"setfenv", luaB_setfenv},
  {"setmetatable", luaB_setmetatable},
  {"tonumber", luaB_tonumber},
  {"tostring", luaB_tostring},
  {"type", luaB_type},
  {"unpack", luaB_unpack},
  {"xpcall", luaB_xpcall},
  {NULL, NULL}
};

LUAMOD_API int luaopen_base (lua_State *L) {
  lua_pushvalue(L, LUA_GLOBALSINDEX);
  luaL_register(L, "_G", base_funcs);
  lua_pushliteral(L, LUA_VERSION);
  lua_setglobal(L, "_VERSION");
  return 1;
}
```

## 总结

Lua 基础库提供了语言运行所需的核心功能：

1. **类型转换**：`tonumber`, `tostring`, `type` 等
2. **错误处理**：`error`, `assert`, `pcall`, `xpcall`
3. **元编程**：`getmetatable`, `setmetatable`, `rawget`, `rawset`
4. **环境控制**：`getfenv`, `setfenv`
5. **迭代器**：`next`, `pairs`, `ipairs`
6. **垃圾回收**：`collectgarbage`
7. **输出调试**：`print`

这些函数构成了 Lua 编程的基础，为更高级的功能提供了支撑。基础库的设计体现了 Lua 的简洁性原则，每个函数都有明确的职责，实现高效且易于理解。

---

*相关文档：[字符串库](wiki_lib_string.md) | [表实现](wiki_table.md) | [对象系统](wiki_object.md)*