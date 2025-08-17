# Lua 表实现详解

## 概述

Lua 的表（Table）是 Lua 中唯一的数据结构化类型，它既可以作为数组使用，也可以作为哈希表使用。Lua 表的实现采用了一种混合策略，将数组部分和哈希部分分开存储，以优化不同使用模式的性能。

## 表的数据结构

### Table 结构定义

```c
typedef struct Table {
  CommonHeader;           // GC 对象通用头部
  lu_byte flags;          // 元方法存在标志 (1<<p 表示元方法p不存在)
  lu_byte lsizenode;      // 哈希部分大小的对数值 (log2(size))
  struct Table *metatable;// 元表指针
  TValue *array;          // 数组部分
  Node *node;             // 哈希部分
  Node *lastfree;         // 指向最后一个空闲位置
  GCObject *gclist;       // GC 链表指针
  int sizearray;          // 数组部分的大小
} Table;
```

### Node 结构 (哈希表节点)

```c
typedef struct Node {
  TValue i_val;           // 存储的值
  TKey i_key;             // 键信息
} Node;

typedef union TKey {
  struct {
    TValuefields;         // 键的值和类型
    struct Node *next;    // 冲突链表指针
  } nk;
  TValue tvk;            // 作为 TValue 访问
} TKey;
```

## 设计原理

### 混合存储策略

Lua 表采用混合存储策略，将数据分为两部分：

1. **数组部分** (`array`):
   - 存储非负整数键的元素
   - 连续存储，访问效率高 O(1)
   - 大小为 2 的幂次

2. **哈希部分** (`node`):
   - 存储其他类型的键
   - 使用开放寻址法处理冲突
   - 采用 Brent 变种算法优化

### 数组大小的确定

数组大小的确定遵循一个重要原则：**数组大小 n 应该是最大的 2 的幂次，使得数组中至少一半的位置被使用**。

```c
// 数组大小计算逻辑示例
static int computesizes (int nums[], int *narray) {
  int i;
  int twotoi;  // 2^i
  int a = 0;   // 数组中元素的数量
  int na = 0;  // 数组的最优大小
  int n = 0;   // 总元素数量
  
  for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {  // 超过一半被使用
        n = twotoi;        // 这个大小是候选
        na = a;
      }
    }
  }
  *narray = n;
  lua_assert(na <= *narray && *narray <= 2*na);
  return na;
}
```

## 哈希算法

### 主位置计算

不同类型的键使用不同的哈希函数：

```c
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNUMBER:
      return hashnum(t, nvalue(key));
    case LUA_TSTRING:
      return hashstr(t, rawtsvalue(key));
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    default:
      return hashpointer(t, gcvalue(key));
  }
}
```

### 数字哈希算法

```c
static Node *hashnum (const Table *t, lua_Number n) {
  unsigned int a[numints];
  int i;
  n += 1;  // 规范化数字（避免 -0）
  memcpy(a, &n, sizeof(a));
  for (i = 1; i < numints; i++) a[0] += a[i];
  return hashmod(t, a[0]);
}
```

### 冲突解决

Lua 使用开放寻址法的 Brent 变种来解决哈希冲突：

```c
static void setnodevector (lua_State *L, Table *t, int size) {
  int lsize;
  if (size == 0) {  // 没有哈希部分
    t->node = cast(Node *, dummynode);
    lsize = 0;
  }
  else {
    int i;
    lsize = ceillog2(size);
    if (lsize > MAXBITS)
      luaG_runerror(L, "table overflow");
    size = twoto(lsize);
    t->node = luaM_newvector(L, size, Node);
    for (i=0; i<size; i++) {
      Node *n = gnode(t, i);
      gnext(n) = NULL;
      setnilvalue(gkey(n));
      setnilvalue(gval(n));
    }
  }
  t->lsizenode = cast_byte(lsize);
  t->lastfree = gnode(t, size);  // 所有位置最初都是空闲的
}
```

## 关键操作实现

### 1. 获取操作 (luaH_get)

```c
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TSTRING: return luaH_getstr(t, rawtsvalue(key));
    case LUA_TNUMBER: {
      int k;
      lua_Number n = nvalue(key);
      lua_number2int(k, n);
      if (luai_numeq(cast_num(k), nvalue(key)))
        return luaH_getnum(t, k);  // 整数键，尝试数组部分
      // 否则进入哈希部分
    }
    default: {
      Node *n = mainposition(t, key);
      do {  // 检查主位置是否匹配
        if (luaO_rawequalObj(key2tval(n), key))
          return gval(n);
        else n = gnext(n);
      } while (n);
      return luaO_nilobject;
    }
  }
}
```

### 2. 设置操作 (luaH_set)

```c
TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);
  t->flags = 0;  // 清除缓存的元方法标志
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    if (ttisnil(key)) luaG_runerror(L, "table index is nil");
    return newkey(L, t, key);
  }
}
```

### 3. 插入新键 (newkey)

```c
static TValue *newkey (lua_State *L, Table *t, const TValue *key) {
  Node *mp = mainposition(t, key);
  if (!ttisnil(gval(mp)) || mp == dummynode) {
    Node *othern;
    Node *n = getfreepos(t);  // 获取空闲位置
    if (n == NULL) {  // 没有空闲位置
      rehash(L, t, key);  // 扩展表
      return luaH_set(L, t, key);
    }
    // 处理冲突...
  }
  setobj2t(L, gkey(mp), key);
  luaC_barriert(L, t, key);
  return gval(mp);
}
```

## 表的重哈希 (rehash)

当表空间不足时，Lua 会进行重哈希操作：

```c
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  int nasize, na;
  int nums[MAXBITS+1];
  int i;
  int totaluse;
  
  // 统计各种大小的元素数量
  for (i=0; i<=MAXBITS; i++) nums[i] = 0;
  nasize = numusearray(t, nums);  // 统计数组部分
  totaluse = nasize;
  totaluse += numusehash(t, nums, &nasize);  // 统计哈希部分
  
  // 为新键留出空间
  nasize++;
  totaluse++;
  
  // 计算新的数组大小
  na = computesizes(nums, &nasize);
  
  // 重建表
  resize(L, t, nasize, totaluse - na);
}
```

## 性能特性

### 时间复杂度

| 操作 | 数组部分 | 哈希部分 |
|------|----------|----------|
| 访问 | O(1) | O(1) 平均 |
| 插入 | O(1) | O(1) 平均 |
| 删除 | O(1) | O(1) 平均 |
| 重哈希 | O(n) | O(n) |

### 空间优化

1. **数组部分优化**：
   - 连续的非负整数键使用数组存储
   - 减少了指针开销
   - 提高了缓存局部性

2. **哈希部分优化**：
   - 只在需要时分配哈希部分
   - 动态调整大小
   - Brent 算法减少冲突

## 元表支持

### 元方法缓存

```c
// flags 字段缓存元方法信息
#define gfasttm(g,et,e) ((et) == NULL ? NULL : \
  ((et)->flags & (1u<<(e))) ? NULL : luaT_gettm(et, e, (g)->tmname[e]))
```

每个位表示对应的元方法是否存在：
- 如果位为 1，表示该元方法不存在，直接返回 NULL
- 如果位为 0，需要实际查找元方法

## 调试和工具函数

### 1. 获取表长度 (luaH_getn)

```c
int luaH_getn (Table *t) {
  int j = t->sizearray;
  if (j > 0 && ttisnil(&t->array[j - 1])) {
    // 存在空洞，需要二分查找
    int i = 0;
    while (j - i > 1) {
      int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;
      else i = m;
    }
    return i;
  }
  else if (t->node != dummynode) {
    // 检查哈希部分是否有更大的整数键
    int i = 0;
    while (!ttisnil(luaH_getnum(t, j + 1 + i))) i++;
    return j + i;
  }
  else return j;
}
```

### 2. 下一个键值对 (luaH_next)

```c
int luaH_next (lua_State *L, Table *t, StkId key) {
  int i = findindex(L, t, key);  // 找到当前键的位置
  for (i++; i < t->sizearray; i++) {  // 尝试数组部分
    if (!ttisnil(&t->array[i])) {
      setnvalue(key, cast_num(i+1));
      setobj2s(L, key+1, &t->array[i]);
      return 1;
    }
  }
  // 继续在哈希部分查找
  for (i -= t->sizearray; i < sizenode(t); i++) {
    if (!ttisnil(gval(gnode(t, i)))) {
      setobj2s(L, key, key2tval(gnode(t, i)));
      setobj2s(L, key+1, gval(gnode(t, i)));
      return 1;
    }
  }
  return 0;  // 没有更多元素
}
```

## 总结

Lua 表的实现是一个精心设计的数据结构，通过以下特性实现了优异的性能：

1. **混合存储**：数组和哈希表的结合，针对不同访问模式优化
2. **动态调整**：根据使用情况自动调整数组和哈希部分的大小
3. **高效哈希**：Brent 变种算法确保即使在高负载因子下也有良好性能
4. **元方法缓存**：避免重复的元表查找
5. **内存优化**：只在需要时分配哈希部分

这种设计使得 Lua 表在各种使用场景下都能保持高效，无论是作为数组、字典还是对象使用。

---

*相关文档：[函数系统](wiki_function.md) | [虚拟机执行](wiki_vm.md) | [垃圾回收器](wiki_gc.md)*