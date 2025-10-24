# 📚 标准库模块总览

> **模块定位**：Lua 内置函数和常用功能的实现

## 📋 模块概述

标准库提供了 Lua 的内置函数和常用功能，包括基础库、字符串库、表库、数学库、I/O 库等。所有标准库都是通过 C API 实现的。

### 核心文件

- `lbaselib.c` - 基础库
- `lstrlib.c` - 字符串库
- `ltablib.c` - 表库
- `lmathlib.c` - 数学库
- `liolib.c` - I/O 库
- `loslib.c` - 操作系统库
- `ldblib.c` - 调试库
- `loadlib.c` - 模块加载库

## 🎯 核心技术

### 1. C API 调用规范

**C 函数原型**：

```c
typedef int (*lua_CFunction) (lua_State *L);
```

**调用约定**：
- 参数通过栈传递
- 返回值压入栈
- 返回值数量通过返回值表示

**示例**：
```c
static int math_abs (lua_State *L) {
    lua_Number x = luaL_checknumber(L, 1);  // 获取参数
    lua_pushnumber(L, fabs(x));             // 压入结果
    return 1;                               // 返回值数量
}
```

### 2. 基础库（base）

**核心功能**：
- `print` - 打印输出
- `type` - 获取类型
- `tonumber` - 类型转换
- `pairs/ipairs` - 迭代器
- `pcall/xpcall` - 保护调用
- `load/loadfile` - 动态加载
- `setmetatable/getmetatable` - 元表操作

### 3. 字符串库（string）

**模式匹配**：

Lua 实现了自己的模式匹配系统（不是正则表达式）：

```lua
string.find("hello world", "w%w+")   -- 查找单词
string.match("123abc", "%d+")        -- 匹配数字
string.gsub("aaa", "a", "b")         -- 全局替换
```

**模式字符**：
- `%a` - 字母
- `%d` - 数字
- `%w` - 字母数字
- `%s` - 空白字符
- `.` - 任意字符
- `*` - 0或多次重复
- `+` - 1或多次重复
- `-` - 最少匹配

**实现技术**：
- 递归下降匹配算法
- 回溯机制
- 捕获组支持

### 4. 表库（table）

**核心功能**：
- `table.insert` - 插入元素
- `table.remove` - 删除元素
- `table.sort` - 排序
- `table.concat` - 连接字符串

**排序算法**：
快速排序（Quicksort）实现，支持自定义比较函数。

### 5. 数学库（math）

**数学函数**：
- 三角函数：`sin`, `cos`, `tan`
- 指数对数：`exp`, `log`, `log10`
- 取整函数：`floor`, `ceil`
- 随机数：`random`, `randomseed`
- 其他：`abs`, `max`, `min`, `sqrt`, `pow`

### 6. I/O 库（io）

**文件操作**：

```lua
local f = io.open("file.txt", "r")  -- 打开文件
local content = f:read("*a")         -- 读取全部
f:close()                            -- 关闭文件
```

**I/O 模式**：
- `"*n"` - 读取数字
- `"*a"` - 读取全部
- `"*l"` - 读取一行
- `n` - 读取 n 个字符

### 7. 操作系统库（os）

**功能**：
- `os.time` - 获取时间
- `os.date` - 格式化日期
- `os.clock` - CPU 时间
- `os.execute` - 执行系统命令
- `os.getenv` - 环境变量
- `os.exit` - 退出程序

### 8. 调试库（debug）

**调试功能**：
- `debug.getinfo` - 函数信息
- `debug.getlocal` - 局部变量
- `debug.setlocal` - 设置局部变量
- `debug.traceback` - 调用栈跟踪
- `debug.sethook` - 设置调试钩子

## 📚 详细技术文档

- [基础库实现](base_library.md) - 基础函数的实现细节
- [字符串模式匹配](pattern_matching.md) - 模式匹配算法
- [表操作优化](table_operations.md) - 表库的性能优化
- [文件 I/O 实现](file_io.md) - 文件操作的实现
- [调试接口](debug_interface.md) - 调试库的使用

## 🔗 相关模块

- [虚拟机模块](../vm/wiki_vm.md) - 标准库通过虚拟机接口实现
- [对象系统模块](../object/wiki_object.md) - 标准库操作的数据类型

---

*继续阅读：[基础库实现](base_library.md)*
