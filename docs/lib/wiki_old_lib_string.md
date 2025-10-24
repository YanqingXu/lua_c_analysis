# Lua 字符串库详解

## 概述

Lua 字符串库（lstrlib.c）提供了丰富的字符串操作函数，包括基本的字符串操作、强大的模式匹配功能以及字符串格式化。Lua 的模式匹配虽然不是完整的正则表达式，但功能强大且高效。

## 基础字符串函数

### 1. 字符串信息

#### string.len(s)
返回字符串的长度。

```c
static int str_len (lua_State *L) {
  size_t l;
  luaL_checklstring(L, 1, &l);  // 检查并获取字符串及其长度
  lua_pushinteger(L, l);
  return 1;
}
```

**使用示例：**
```lua
print(string.len("hello"))  -- 5
print(#"hello")            -- 等价写法
```

### 2. 字符串截取

#### string.sub(s, i [, j])
返回字符串的子串。

```c
static int str_sub (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  ptrdiff_t start = posrelat(luaL_checkinteger(L, 2), l);
  ptrdiff_t end = posrelat(luaL_optinteger(L, 3, -1), l);
  
  if (start < 1) start = 1;
  if (end > (ptrdiff_t)l) end = (ptrdiff_t)l;
  
  if (start <= end)
    lua_pushlstring(L, s+start-1, end-start+1);
  else 
    lua_pushliteral(L, "");
  return 1;
}
```

**位置计算函数：**
```c
static ptrdiff_t posrelat (ptrdiff_t pos, size_t len) {
  // 相对字符串位置：负数表示从末尾开始
  return (pos>=0) ? pos : (ptrdiff_t)len+pos+1;
}
```

**使用示例：**
```lua
local s = "hello world"
print(string.sub(s, 1, 5))   -- "hello"
print(string.sub(s, 7))      -- "world"
print(string.sub(s, -5))     -- "world"
print(string.sub(s, -5, -1)) -- "world"
```

### 3. 大小写转换

#### string.lower(s) 和 string.upper(s)
```c
static int str_lower (lua_State *L) {
  size_t l;
  size_t i;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  luaL_buffinit(L, &b);        // 初始化缓冲区
  for (i=0; i<l; i++)
    luaL_addchar(&b, tolower(uchar(s[i])));  // 逐字符转换
  luaL_pushresult(&b);         // 推送结果
  return 1;
}

static int str_upper (lua_State *L) {
  size_t l;
  size_t i;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  luaL_buffinit(L, &b);
  for (i=0; i<l; i++)
    luaL_addchar(&b, toupper(uchar(s[i])));
  luaL_pushresult(&b);
  return 1;
}
```

### 4. 字符串重复

#### string.rep(s, n)
```c
static int str_rep (lua_State *L) {
  size_t l;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  int n = luaL_checkint(L, 2);
  luaL_buffinit(L, &b);
  while (n-- > 0)
    luaL_addlstring(&b, s, l);   // 重复添加字符串
  luaL_pushresult(&b);
  return 1;
}
```

**使用示例：**
```lua
print(string.rep("ha", 3))  -- "hahaha"
print(string.rep("*", 10)) -- "**********"
```

### 5. 字符串反转

#### string.reverse(s)
```c
static int str_reverse (lua_State *L) {
  size_t l;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  luaL_buffinit(L, &b);
  while (l--) luaL_addchar(&b, s[l]);  // 从后向前添加字符
  luaL_pushresult(&b);
  return 1;
}
```

## 字符和字节码转换

### 1. 字符串转字节码

#### string.byte(s [, i [, j]])
```c
static int str_byte (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  ptrdiff_t posi = posrelat(luaL_optinteger(L, 2, 1), l);
  ptrdiff_t pose = posrelat(luaL_optinteger(L, 3, posi), l);
  int n, i;
  
  if (posi <= 0) posi = 1;
  if ((size_t)pose > l) pose = l;
  if (posi > pose) return 0;  // 空区间
  
  n = (int)(pose - posi + 1);
  if (posi + n <= pose)  // 溢出检查
    luaL_error(L, "string slice too long");
  
  luaL_checkstack(L, n, "string slice too long");
  for (i=0; i<n; i++)
    lua_pushinteger(L, uchar(s[posi+i-1]));  // 推送每个字节的值
  return n;
}
```

### 2. 字节码转字符串

#### string.char(...)
```c
static int str_char (lua_State *L) {
  int n = lua_gettop(L);  // 参数数量
  int i;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (i=1; i<=n; i++) {
    int c = luaL_checkint(L, i);
    luaL_argcheck(L, uchar(c) == c, i, "invalid value");  // 值范围检查
    luaL_addchar(&b, uchar(c));
  }
  luaL_pushresult(&b);
  return 1;
}
```

**使用示例：**
```lua
print(string.byte("ABC"))        -- 65
print(string.byte("ABC", 1, 3))  -- 65	66	67
print(string.char(65, 66, 67))   -- "ABC"
```

## 模式匹配系统

Lua 的模式匹配是字符串库的核心功能，提供了类似正则表达式的功能。

### 1. 匹配状态结构

```c
typedef struct MatchState {
  const char *src_init;  // 源字符串起始位置
  const char *src_end;   // 源字符串结束位置
  lua_State *L;
  int level;             // 捕获总数
  struct {
    const char *init;
    ptrdiff_t len;
  } capture[LUA_MAXCAPTURES];  // 捕获数组
} MatchState;
```

### 2. 字符类匹配

```c
static int match_class (int c, int cl) {
  int res;
  switch (tolower(cl)) {
    case 'a' : res = isalpha(c); break;   // 字母
    case 'c' : res = iscntrl(c); break;   // 控制字符
    case 'd' : res = isdigit(c); break;   // 数字
    case 'l' : res = islower(c); break;   // 小写字母
    case 'p' : res = ispunct(c); break;   // 标点符号
    case 's' : res = isspace(c); break;   // 空白字符
    case 'u' : res = isupper(c); break;   // 大写字母
    case 'w' : res = isalnum(c); break;   // 字母数字
    case 'x' : res = isxdigit(c); break;  // 十六进制数字
    case 'z' : res = (c == 0); break;     // 零字符
    default: return (cl == c);
  }
  return (islower(cl) ? res : !res);  // 大写表示取反
}
```

### 3. 基本模式匹配函数

#### string.find(s, pattern [, init [, plain]])
在字符串中查找模式。

```c
static int str_find_aux (lua_State *L, int find) {
  size_t l1, l2;
  const char *s = luaL_checklstring(L, 1, &l1);
  const char *p = luaL_checklstring(L, 2, &l2);
  ptrdiff_t init = posrelat(luaL_optinteger(L, 3, 1), l1) - 1;
  
  if (init < 0) init = 0;
  else if ((size_t)(init) > l1) init = (ptrdiff_t)l1;
  
  if (find && (lua_toboolean(L, 4) ||  // 明确请求或
      strpbrk(p, SPECIALS) == NULL)) { // 没有特殊字符
    // 进行朴素字符串搜索
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
          lua_pushinteger(L, s1-s+1);  // 起始位置
          lua_pushinteger(L, res-s);   // 结束位置
          return push_captures(&ms, NULL, 0) + 2;
        }
        else
          return push_captures(&ms, s1, res);
      }
    } while (s1++ < ms.src_end && !anchor);
  }
  lua_pushnil(L);  // 未找到
  return 1;
}
```

#### string.match(s, pattern [, init])
```c
static int str_match (lua_State *L) {
  return str_find_aux(L, 0);  // find=0，只返回捕获
}
```

#### string.gmatch(s, pattern)
返回迭代器函数，用于遍历所有匹配。

```c
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
      if (e == src) newstart++;  // 空匹配，移动一位
      lua_pushinteger(L, newstart);
      lua_replace(L, lua_upvalueindex(3));
      return push_captures(&ms, src, e);
    }
  }
  return 0;  // 没有更多匹配
}

static int str_gmatch (lua_State *L) {
  luaL_checkstring(L, 1);
  luaL_checkstring(L, 2);
  lua_settop(L, 2);
  lua_pushinteger(L, 0);
  lua_pushcclosure(L, gmatch_aux, 3);
  return 1;
}
```

### 4. 字符串替换

#### string.gsub(s, pattern, repl [, n])
```c
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
      add_value(&ms, &b, src, e);  // 添加替换值
    }
    if (e && e>src) // 非空匹配
      src = e;
    else if (src < ms.src_end)
      luaL_addchar(&b, *src++);
    else break;
    if (anchor) break;
  }
  luaL_addlstring(&b, src, ms.src_end-src);
  luaL_pushresult(&b);
  lua_pushinteger(L, n);  // 替换次数
  return 2;
}
```

### 5. 模式语法

| 字符类 | 含义 |
|--------|------|
| `.` | 任意字符 |
| `%a` | 字母 |
| `%c` | 控制字符 |
| `%d` | 数字 |
| `%l` | 小写字母 |
| `%p` | 标点符号 |
| `%s` | 空白字符 |
| `%u` | 大写字母 |
| `%w` | 字母数字 |
| `%x` | 十六进制数字 |
| `%z` | 空字符 (字节值为0) |
| `%X` | X的补集（X为上述字符类） |

| 模式修饰符 | 含义 |
|------------|------|
| `+` | 匹配1个或多个 |
| `*` | 匹配0个或多个 |
| `-` | 匹配0个或多个（非贪婪） |
| `?` | 匹配0个或1个 |

### 6. 捕获机制

```c
static int push_captures (MatchState *ms, const char *s, const char *e) {
  int i;
  int nlevels = (ms->level == 0 && s) ? 1 : ms->level;
  luaL_checkstack(ms->L, nlevels, "too many captures");
  for (i = 0; i < nlevels; i++)
    push_onecapture(ms, i, s, e);
  return nlevels;  // 返回捕获数量
}

static void push_onecapture (MatchState *ms, int i, const char *s,
                                                    const char *e) {
  if (i >= ms->level) {
    if (i == 0)  // ms->level == 0, too
      lua_pushlstring(ms->L, s, e - s);  // 添加整个匹配
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
```

## 字符串格式化

#### string.format(formatstring, ...)
使用 C 风格的格式字符串。

```c
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
      luaL_addchar(&b, *strfrmt++);  // %%
    else { // 格式化序列
      char form[MAX_FORMAT];
      char buff[MAX_ITEM];
      arg++;
      strfrmt = scanformat(L, strfrmt, form);
      switch (*strfrmt++) {
        case 'c': {
          sprintf(buff, form, luaL_checkint(L, arg));
          break;
        }
        case 'd':  case 'i': {
          sprintf(buff, form, luaL_checklong(L, arg));
          break;
        }
        case 'o':  case 'u':  case 'x':  case 'X': {
          sprintf(buff, form, luaL_checkunsigned(L, arg));
          break;
        }
        case 'e':  case 'E': case 'f':
        case 'g': case 'G': {
          sprintf(buff, form, luaL_checknumber(L, arg));
          break;
        }
        case 'q': {
          addquoted(L, &b, arg);
          continue;  // 跳过下面的 addsize
        }
        case 's': {
          size_t l;
          const char *s = luaL_checklstring(L, arg, &l);
          if (!strchr(form, '.') && l >= 100) {
            // 没有精度，字符串太长
            luaL_addvalue(&b);  // 直接保持字符串
            continue;
          }
          else {
            sprintf(buff, form, s);
            break;
          }
        }
        default: {  // 错误
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
```

## 函数转储

#### string.dump(function)
返回函数的二进制表示。

```c
static int str_dump (lua_State *L) {
  luaL_Buffer b;
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_settop(L, 1);
  luaL_buffinit(L, &b);
  if (lua_dump(L, writer, &b) != 0)
    luaL_error(L, "unable to dump given function");
  luaL_pushresult(&b);
  return 1;
}
```

## 使用示例

### 1. 基础操作
```lua
local s = "Hello, World!"
print(string.len(s))           -- 13
print(string.upper(s))         -- "HELLO, WORLD!"
print(string.sub(s, 1, 5))     -- "Hello"
print(string.reverse(s))       -- "!dlroW ,olleH"
```

### 2. 模式匹配
```lua
local text = "The price is $123.45"
local price = string.match(text, "%$(%d+%.%d+)")
print(price)  -- "123.45"

-- 查找所有数字
for num in string.gmatch("12 cats, 34 dogs", "%d+") do
  print(num)  -- 12, 34
end

-- 替换
local result = string.gsub("hello world", "(%w+)", "%1!")
print(result)  -- "hello! world!"
```

### 3. 格式化
```lua
local name = "Alice"
local age = 30
local msg = string.format("Hello, %s! You are %d years old.", name, age)
print(msg)  -- "Hello, Alice! You are 30 years old."
```

## 总结

Lua 字符串库提供了完整的字符串处理功能：

1. **基础操作**：长度、截取、大小写转换、反转、重复
2. **字符转换**：字符串与字节码之间的转换
3. **模式匹配**：强大的模式匹配系统，支持捕获和替换
4. **格式化**：C 风格的字符串格式化
5. **二进制操作**：函数序列化

字符串库的设计体现了 Lua 的高效性和易用性，模式匹配功能虽然比正则表达式简单，但足以满足大多数字符串处理需求，同时保持了实现的简洁性。

---

*相关文档：[基础库](wiki_lib_base.md) | [表实现](wiki_table.md) | [虚拟机执行](wiki_vm.md)*