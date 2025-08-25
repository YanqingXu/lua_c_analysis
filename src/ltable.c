/*
** $Id: ltable.c,v 2.32.1.2 2007/12/28 15:32:23 roberto Exp $
** Lua 表（哈希表）实现
** 版权声明见 lua.h
*/


/*
** 表（也称为数组、对象或哈希表）的实现。
** 表将其元素保存在两个部分：数组部分和哈希部分。
** 非负整数键都是保存在数组部分的候选者。数组的实际大小是最大的 `n`，
** 使得在 0 和 n 之间至少有一半的槽位被使用。
** 哈希使用链式散列表与 Brent 变体的混合。
** 这些表的一个主要不变量是，如果一个元素不在其主位置
** （即其哈希值给出的"原始"位置），那么冲突的元素就在其自己的主位置。
** 因此，即使负载因子达到 100%，性能仍然保持良好。
*/

#include <math.h>
#include <string.h>

#define ltable_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"


/*
** 数组部分的最大大小是 2^MAXBITS
*/
#if LUAI_BITSINT > 26
#define MAXBITS		26
#else
#define MAXBITS		(LUAI_BITSINT-2)
#endif

#define MAXASIZE	(1 << MAXBITS)


/* 2的幂次哈希函数 */
#define hashpow2(t,n)      (gnode(t, lmod((n), sizenode(t))))
  
/* 字符串哈希函数 */
#define hashstr(t,str)  hashpow2(t, (str)->tsv.hash)
/* 布尔值哈希函数 */
#define hashboolean(t,p)        hashpow2(t, p)


/*
** 对于某些类型，最好避免使用 2 的幂次取模，
** 因为它们往往有很多 2 的因子。
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))

/* 指针哈希函数 */
#define hashpointer(t,p)	hashmod(t, IntPoint(p))


/*
** lua_Number 内部的整数个数
*/
#define numints		cast_int(sizeof(lua_Number)/sizeof(int))



/* 虚拟节点定义 */
#define dummynode		(&dummynode_)

/* 静态虚拟节点，用于空哈希表 */
static const Node dummynode_ = {
  {{NULL}, LUA_TNIL},  /* 值 */
  {{{NULL}, LUA_TNIL, NULL}}  /* 键 */
};


/*
** lua_Number 的哈希函数
*/
static Node *hashnum(const Table *t, lua_Number n)
{
    /* 用于存储数字的整数数组 */
    unsigned int a[numints];
    int i;
    
    /* 避免 -0 的问题 */
    if (luai_numeq(n, 0))
    {
        /* 直接返回索引0的节点 */
        return gnode(t, 0);
    }

    /* 将浮点数按位复制到整数数组 */
    memcpy(a, &n, sizeof(a));
    
    /* 将所有整数部分相加到a[0] */
    for (i = 1; i < numints; i++)
    {
        a[0] += a[i];
    }

    /* 使用模运算计算哈希位置 */
    return hashmod(t, a[0]);
}



/*
** 返回表中元素的"主"位置（即其哈希值的索引）
*/
static Node *mainposition(const Table *t, const TValue *key)
{
    /* 根据键的类型选择不同的哈希函数 */
    switch (ttype(key))
    {
        /* 数字类型 */
        case LUA_TNUMBER:
            /* 使用数字哈希函数 */
            return hashnum(t, nvalue(key));
            
        /* 字符串类型 */
        case LUA_TSTRING:
            /* 使用字符串哈希函数 */
            return hashstr(t, rawtsvalue(key));
            
        /* 布尔类型 */
        case LUA_TBOOLEAN:
            /* 使用布尔哈希函数 */
            return hashboolean(t, bvalue(key));
            
        /* 轻量用户数据 */
        case LUA_TLIGHTUSERDATA:
            /* 使用指针哈希函数 */
            return hashpointer(t, pvalue(key));
            
        /* 其他可回收对象 */
        default:
            /* 使用指针哈希函数 */
            return hashpointer(t, gcvalue(key));
    }
}


/*
** 如果 `key` 是适合存放在表的数组部分的键，则返回其索引，
** 否则返回 -1。
*/
static int arrayindex(const TValue *key)
{
    /* 检查键是否为数字类型 */
    if (ttisnumber(key))
    {
        /* 获取数字值 */
        lua_Number n = nvalue(key);
        /* 整数转换结果 */
        int k;
        
        /* 将浮点数转换为整数 */
        lua_number2int(k, n);
        
        /* 检查转换是否无损（即原数是整数） */
        if (luai_numeq(cast_num(k), n))
        {
            /* 返回整数索引 */
            return k;
        }
    }

    /* `key` 不满足某些条件（非数字或非整数） */
    return -1;
}


/*
** 返回表遍历中 `key` 的索引。首先遍历数组部分的所有元素，
** 然后遍历哈希部分的元素。遍历的开始由 -1 表示。
*/
static int findindex(lua_State *L, Table *t, StkId key)
{
    int i;
    
    /* 第一次迭代，返回起始标记 */
    if (ttisnil(key))
    {
        return -1;
    }

    /* 尝试将键转换为数组索引 */
    i = arrayindex(key);
    
    /* `key` 在数组部分内吗？ */
    if (0 < i && i <= t->sizearray)
    {
        /* 是的；那就是索引（修正为 C 风格，从0开始） */
        return i - 1;
    }
    else
    {
        /* 键不在数组部分，搜索哈希部分 */
        /* 获取键的主位置 */
        Node *n = mainposition(t, key);
        
        do
        {
            /* 沿着冲突链检查 `key` 是否在链中的某处 */
            /* 键可能已经死亡，但在 `next` 中使用它是可以的 */
            /* 直接比较键值 */
            /* 或者是死键但GC值相同 */
            if (luaO_rawequalObj(key2tval(n), key) ||
                (ttype(gkey(n)) == LUA_TDEADKEY && iscollectable(key) &&
                 gcvalue(gkey(n)) == gcvalue(key)))
            {
                /* 计算节点在哈希表中的索引 */
                i = cast_int(n - gnode(t, 0));
                /* 哈希元素在数组元素之后编号 */
                /* 返回全局索引（数组大小+哈希索引） */
                return i + t->sizearray;
            }
            else
            {
                /* 移动到链中的下一个节点 */
                n = gnext(n);
            }
            
        /* 继续直到链结束 */
        } while (n);
        
        /* 未找到键，抛出错误 */
        luaG_runerror(L, "invalid key to " LUA_QL("next"));
        /* 避免编译器警告 */
        return 0;
    }
}


/*
** 表的 next 函数实现
*/
int luaH_next(lua_State *L, Table *t, StkId key)
{
    /* 找到当前键的索引位置 */
    int i = findindex(L, t, key);
    
    /* 从下一个位置开始搜索数组部分 */
    for (i++; i < t->sizearray; i++)
    {
        /* 找到非 nil 值？ */
        if (!ttisnil(&t->array[i]))
        {
            /* 设置键为数组索引（Lua风格，从1开始） */
            setnvalue(key, cast_num(i + 1));
            /* 设置值 */
            setobj2s(L, key + 1, &t->array[i]);
            /* 成功找到下一个元素 */
            return 1;
        }
    }
    
    /* 遍历哈希部分 */
    for (i -= t->sizearray; i < sizenode(t); i++)
    {
        /* 找到非 nil 值？ */
        if (!ttisnil(gval(gnode(t, i))))
        {
            /* 设置键 */
            setobj2s(L, key, key2tval(gnode(t, i)));
            /* 设置值 */
            setobj2s(L, key + 1, gval(gnode(t, i)));
            /* 成功找到下一个元素 */
            return 1;
        }
    }
    
    /* 没有更多元素，遍历结束 */
    return 0;
}


/*
** {=============================================================
** 重新哈希
** ==============================================================
*/


/*
** 计算数组和哈希部分的最优大小
*/
static int computesizes(int nums[], int *narray)
{
    int i;
    /* 2^i，当前考虑的数组大小 */
    int twotoi;
    /* 累计的小于等于 2^i 的元素数量 */
    int a = 0;
    /* 最终要放入数组部分的元素数量 */
    int na = 0;
    /* 数组部分的最优大小 */
    int n = 0;
    
    /* 遍历所有可能的2的幂次大小 */
    for (i = 0, twotoi = 1; twotoi / 2 < *narray; i++, twotoi *= 2)
    {
        /* 这个范围内有元素 */
        if (nums[i] > 0)
        {
            /* 累加元素数量 */
            a += nums[i];
            
            /* 超过一半的槽位被使用？ */
            if (a > twotoi / 2)
            {
                /* 更新最优大小（到目前为止） */
                n = twotoi;
                /* 记录将要放入数组部分的元素数量 */
                na = a;
            }
        }

        /* 所有元素已经计算完毕，提前退出 */
        if (a == *narray)
        {
            break;
        }
    }
    
    /* 返回计算出的最优数组大小 */
    *narray = n;
    /* 断言：使用率在50%-100%之间 */
    lua_assert(*narray / 2 <= na && na <= *narray);
    /* 返回将要放入数组部分的元素数量 */
    return na;
}


/*
** 计算整数键的数量
*/
static int countint(const TValue *key, int *nums)
{
    /* 尝试将键转换为数组索引 */
    int k = arrayindex(key);
    
    /* `key` 是合适的数组索引吗？ */
    if (0 < k && k <= MAXASIZE)
    {
        /* 在对应的2的幂次范围内计数 */
        nums[ceillog2(k)]++;
        /* 返回1表示这是一个有效的整数键 */
        return 1;
    }
    else
    {
        /* 返回0表示这不是有效的整数键 */
        return 0;
    }
}


/*
** 计算数组部分使用的元素数量
*/
static int numusearray(const Table *t, int *nums)
{
    int lg;
    /* 2^lg */
    int ttlg;
    /* `nums` 的总和 */
    int ause = 0;
    /* 遍历所有数组键的计数器 */
    int i = 1;
    
    /* 对于每个切片 */
    for (lg = 0, ttlg = 1; lg <= MAXBITS; lg++, ttlg *= 2)
    {
        /* 计数器 */
        int lc = 0;
        int lim = ttlg;

        if (lim > t->sizearray)
        {
            /* 调整上限 */
            lim = t->sizearray;
            /* 没有更多元素要计算 */
            if (i > lim)
            {
                break;
            }
        }

        /* 计算范围 (2^(lg-1), 2^lg] 中的元素 */
        for (; i <= lim; i++)
        {
            if (!ttisnil(&t->array[i - 1]))
            {
                lc++;
            }
        }

        nums[lg] += lc;
        ause += lc;
    }
    
    return ause;
}


/*
** 计算哈希部分使用的元素数量
*/
static int numusehash(const Table *t, int *nums, int *pnasize)
{
    /* 元素总数 */
    int totaluse = 0;
    /* `nums` 的总和 */
    int ause = 0;
    int i = sizenode(t);

    while (i--)
    {
        Node *n = &t->node[i];
        
        if (!ttisnil(gval(n)))
        {
            ause += countint(key2tval(n), nums);
            totaluse++;
        }
    }

    *pnasize += ause;
    return totaluse;
}


/*
** 设置数组向量的大小
*/
static void setarrayvector(lua_State *L, Table *t, int size)
{
    int i;
    
    /* 重新分配数组内存 */
    luaM_reallocvector(L, t->array, t->sizearray, size, TValue);
    
    /* 初始化新分配的槽位 */
    for (i = t->sizearray; i < size; i++)
    {
        /* 将新槽位设置为nil */
        setnilvalue(&t->array[i]);
    }
    
    /* 更新数组大小 */
    t->sizearray = size;
}


/*
** 设置节点向量的大小
*/
static void setnodevector(lua_State *L, Table *t, int size)
{
    /* 大小的对数值 */
    int lsize;
    
    /* 哈希部分没有元素？ */
    if (size == 0)
    {
        /* 使用公共的 `dummynode` 节省内存 */
        t->node = cast(Node *, dummynode);
        /* 对数大小为0 */
        lsize = 0;
    }
    else
    {
        int i;
        
        /* 计算大小的对数（向上取整） */
        lsize = ceillog2(size);
        
        /* 检查是否超过最大限制 */
        if (lsize > MAXBITS)
        {
            /* 抛出表溢出错误 */
            luaG_runerror(L, "table overflow");
        }
        
        /* 将大小调整为2的幂次 */
        size = twoto(lsize);
        /* 分配新的节点数组 */
        t->node = luaM_newvector(L, size, Node);
        
        /* 初始化所有节点 */
        for (i = 0; i < size; i++)
        {
            /* 获取第i个节点 */
            Node *n = gnode(t, i);
            /* 清空链表指针 */
            gnext(n) = NULL;
            /* 将键设置为nil */
            setnilvalue(gkey(n));
            /* 将值设置为nil */
            setnilvalue(gval(n));
        }
    }
    
    /* 保存对数大小 */
    t->lsizenode = cast_byte(lsize);
    /* 设置最后空闲位置指针（所有位置都是空闲的） */
    t->lastfree = gnode(t, size);
}


/*
** 调整表的大小
*/
static void resize(lua_State *L, Table *t, int nasize, int nhsize)
{
    int i;
    int oldasize = t->sizearray;
    int oldhsize = t->lsizenode;
    /* 保存旧哈希... */
    Node *nold = t->node;
    
    /* 数组部分必须增长？ */
    if (nasize > oldasize)
    {
        setarrayvector(L, t, nasize);
    }

    /* 创建适当大小的新哈希部分 */
    setnodevector(L, t, nhsize);
    
    /* 数组部分必须收缩？ */
    if (nasize < oldasize)
    {
        t->sizearray = nasize;
        
        /* 重新插入来自消失切片的元素 */
        for (i = nasize; i < oldasize; i++)
        {
            if (!ttisnil(&t->array[i]))
            {
                setobjt2t(L, luaH_setnum(L, t, i + 1), &t->array[i]);
            }
        }

        /* 收缩数组 */
        luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
    }
    
    /* 重新插入来自哈希部分的元素 */
    for (i = twoto(oldhsize) - 1; i >= 0; i--)
    {
        Node *old = nold + i;

        if (!ttisnil(gval(old)))
        {
            setobjt2t(L, luaH_set(L, t, key2tval(old)), gval(old));
        }
    }

    if (nold != dummynode)
    {
        /* 释放旧数组 */
        luaM_freearray(L, nold, twoto(oldhsize), Node);
    }
}


/*
** 调整数组大小
*/
void luaH_resizearray(lua_State *L, Table *t, int nasize)
{
    int nsize = (t->node == dummynode) ? 0 : sizenode(t);
    resize(L, t, nasize, nsize);
}


/*
** 重新哈希表
*/
static void rehash(lua_State *L, Table *t, const TValue *ek)
{
    int nasize, na;
    /* nums[i] = 2^(i-1) 和 2^i 之间的键数量 */
    int nums[MAXBITS + 1];
    int i;
    int totaluse;
    
    /* 重置计数 */
    for (i = 0; i <= MAXBITS; i++)
    {
        nums[i] = 0;
    }

    /* 计算数组部分的键 */
    nasize = numusearray(t, nums);
    /* 所有这些键都是整数键 */
    totaluse = nasize;
    /* 计算哈希部分的键 */
    totaluse += numusehash(t, nums, &nasize);
    /* 计算额外的键 */
    nasize += countint(ek, nums);
    totaluse++;
    /* 计算数组部分的新大小 */
    na = computesizes(nums, &nasize);
    /* 将表调整为新计算的大小 */
    resize(L, t, nasize, totaluse - na);
}



/*
** }=============================================================
*/


/*
** 创建新表
*/
Table *luaH_new(lua_State *L, int narray, int nhash)
{
    Table *t = luaM_new(L, Table);
    luaC_link(L, obj2gco(t), LUA_TTABLE);
    t->metatable = NULL;
    t->flags = cast_byte(~0);
    
    /* 临时值（仅在某些 malloc 失败时保留） */
    t->array = NULL;
    t->sizearray = 0;
    t->lsizenode = 0;
    t->node = cast(Node *, dummynode);
    
    setarrayvector(L, t, narray);
    setnodevector(L, t, nhash);
    return t;
}


/*
** 释放表
*/
void luaH_free(lua_State *L, Table *t)
{
    if (t->node != dummynode)
    {
        luaM_freearray(L, t->node, sizenode(t), Node);
    }
    luaM_freearray(L, t->array, t->sizearray, TValue);
    luaM_free(L, t);
}


/*
** 获取空闲位置
*/
static Node *getfreepos(Table *t)
{
    /* 从后向前搜索空闲位置 */
    while (t->lastfree-- > t->node)
    {
        /* 检查键是否为nil（表示空闲） */
        if (ttisnil(gkey(t->lastfree)))
        {
            /* 找到空闲位置，返回它 */
            return t->lastfree;
        }
    }

    /* 找不到空闲位置，需要重新哈希 */
    return NULL;
}



/*
** 将新键插入哈希表；首先，检查键的主位置是否空闲。
** 如果不是，检查冲突节点是否在其主位置：如果不是，
** 将冲突节点移动到空位置并将新键放在其主位置；
** 否则（冲突节点在其主位置），新键进入空位置。
*/
static TValue *newkey(lua_State *L, Table *t, const TValue *key)
{
    /* 计算键的主位置 */
    Node *mp = mainposition(t, key);
    
    /* 主位置被占用或表为空？ */
    if (!ttisnil(gval(mp)) || mp == dummynode)
    {
        /* 用于遍历冲突链的临时变量 */
        Node *othern;
        /* 获取一个空闲位置 */
        Node *n = getfreepos(t);
        
        /* 找不到空闲位置？ */
        if (n == NULL)
        {
            /* 重新哈希以增长表 */
            rehash(L, t, key);
            /* 重新插入键到增长的表中 */
            return luaH_set(L, t, key);
        }
        
        /* 断言：空闲位置不应该是虚拟节点 */
        lua_assert(n != dummynode);
        /* 获取占用主位置的键的主位置 */
        othern = mainposition(t, key2tval(mp));
        
        /* 冲突节点不在其主位置？ */
        if (othern != mp)
        {
            /* 是的；将冲突节点移动到空闲位置 */
            /* 在链中找到指向mp的前一个节点 */
            while (gnext(othern) != mp)
            {
                othern = gnext(othern);
            }

            /* 用空闲节点n代替mp重做链接 */
            gnext(othern) = n;
            /* 将冲突节点的内容复制到空闲位置（包括next指针） */
            *n = *mp;
            /* 清空主位置的next指针 */
            gnext(mp) = NULL;
            /* 清空主位置的值 */
            setnilvalue(gval(mp));
        }
        else
        {
            /* 冲突节点在其自己的主位置 */
            /* 新节点将进入空闲位置 */
            /* 新节点继承主位置的链接 */
            gnext(n) = gnext(mp);
            /* 主位置指向新节点 */
            gnext(mp) = n;
            /* 使用新节点作为插入位置 */
            mp = n;
        }
    }
    
    /* 复制键的内容 */
    gkey(mp)->value = key->value;
    gkey(mp)->tt = key->tt;
    /* 设置垃圾回收屏障 */
    luaC_barriert(L, t, key);
    /* 断言：值应该是nil（等待设置） */
    lua_assert(ttisnil(gval(mp)));
    /* 返回值的地址供调用者设置 */
    return gval(mp);
}


/*
** 整数搜索函数
*/
const TValue *luaH_getnum(Table *t, int key)
{
    /* 检查是否在数组范围内：(1 <= key && key <= t->sizearray) */
    /* 使用无符号比较优化边界检查 */
    if (cast(unsigned int, key - 1) < cast(unsigned int, t->sizearray))
    {
        /* 直接从数组返回（转换为0基索引） */
        return &t->array[key - 1];
    }
    else
    {
        /* 不在数组范围内，搜索哈希部分 */
        /* 将整数转换为浮点数进行哈希 */
        lua_Number nk = cast_num(key);
        /* 计算哈希位置 */
        Node *n = hashnum(t, nk);
        
        /* 沿着冲突链搜索 */
        do
        {
            /* 找到匹配的数字键？ */
            if (ttisnumber(gkey(n)) && luai_numeq(nvalue(gkey(n)), nk))
            {
                /* 返回对应的值 */
                return gval(n);
            }
            else
            {
                /* 移动到链中的下一个节点 */
                n = gnext(n);
            }

        /* 继续直到链结束 */
        } while (n);
        
        /* 未找到，返回nil对象 */
        return luaO_nilobject;
    }
}


/*
** 字符串搜索函数
*/
const TValue *luaH_getstr(Table *t, TString *key)
{
    /* 使用字符串的预计算哈希值定位 */
    Node *n = hashstr(t, key);
    
    /* 沿着冲突链搜索 */
    do
    {
        /* 找到匹配的字符串？（指针比较） */
        if (ttisstring(gkey(n)) && rawtsvalue(gkey(n)) == key)
        {
            /* 返回对应的值 */
            return gval(n);
        }
        else
        {
            /* 移动到链中的下一个节点 */
            n = gnext(n);
        }

    /* 继续直到链结束 */
    } while (n);
    
    /* 未找到，返回nil对象 */
    return luaO_nilobject;
}


/*
** 主搜索函数
*/
const TValue *luaH_get(Table *t, const TValue *key)
{
    /* 根据键的类型选择最优搜索策略 */
    switch (ttype(key))
    {
        /* nil键总是返回nil */
        case LUA_TNIL:
            return luaO_nilobject;
            
        /* 使用字符串专用搜索 */
        case LUA_TSTRING:
            return luaH_getstr(t, rawtsvalue(key));
            
        /* 数字键需要特殊处理 */
        case LUA_TNUMBER:
        {
            /* 整数转换结果 */
            int k;
            /* 获取数字值 */
            lua_Number n = nvalue(key);
            /* 尝试转换为整数 */
            lua_number2int(k, n);
            
            /* 索引是整数？ */
            if (luai_numeq(cast_num(k), nvalue(key)))
            {
                /* 使用整数专用搜索（可能使用数组部分） */
                return luaH_getnum(t, k);
            }

            /* 否则继续到默认情况（浮点数哈希搜索） */
        }
        
        /* 其他类型或浮点数 */
        default:
        {
            /* 计算主位置 */
            Node *n = mainposition(t, key);
            
            /* 沿着冲突链搜索 */
            do
            {
                /* 找到匹配的键？ */
                if (luaO_rawequalObj(key2tval(n), key))
                {
                    /* 返回对应的值 */
                    return gval(n);
                }
                /* 移动到链中的下一个节点 */
                else
                {
                    n = gnext(n);
                }
            /* 继续直到链结束 */
            } while (n);
            
            /* 未找到，返回nil对象 */
            return luaO_nilobject;
        }
    }
}


/*
** 表设置函数
*/
TValue *luaH_set(lua_State *L, Table *t, const TValue *key)
{
    /* 首先尝试查找现有键 */
    const TValue *p = luaH_get(t, key);
    /* 清除表的标志（可能影响元方法缓存） */
    t->flags = 0;
    
    /* 键已存在？ */
    if (p != luaO_nilobject)
    {
        /* 返回现有值的地址供修改 */
        return cast(TValue *, p);
    }
    else
    {
        /* 键不存在，需要创建新条目 */
        /* 不允许nil键 */
        if (ttisnil(key))
        {
            luaG_runerror(L, "table index is nil");
        }
        /* 检查NaN */
        else if (ttisnumber(key) && luai_numisnan(nvalue(key)))
        {
            /* 不允许NaN键 */
            luaG_runerror(L, "table index is NaN");
        }

        /* 创建新键并返回值的地址 */
        return newkey(L, t, key);
    }
}


/*
** 设置数字键
*/
TValue *luaH_setnum(lua_State *L, Table *t, int key)
{
    /* 使用整数专用搜索 */
    const TValue *p = luaH_getnum(t, key);
    
    /* 键已存在？ */
    if (p != luaO_nilobject)
    {
        /* 返回现有值的地址供修改 */
        return cast(TValue *, p);
    }
    else
    {
        /* 键不存在，需要创建新条目 */
        /* 临时键值对象 */
        TValue k;
        /* 将整数转换为TValue */
        setnvalue(&k, cast_num(key));
        /* 创建新键并返回值的地址 */
        return newkey(L, t, &k);
    }
}


/*
** 设置字符串键
*/
TValue *luaH_setstr(lua_State *L, Table *t, TString *key)
{
    /* 使用字符串专用搜索 */
    const TValue *p = luaH_getstr(t, key);
    
    /* 键已存在？ */
    if (p != luaO_nilobject)
    {
        /* 返回现有值的地址供修改 */
        return cast(TValue *, p);
    }
    else
    {
        /* 键不存在，需要创建新条目 */
        /* 临时键值对象 */
        TValue k;
        /* 将字符串设置为TValue */
        setsvalue(L, &k, key);
        /* 创建新键并返回值的地址 */
        return newkey(L, t, &k);
    }
}


/*
** 无界搜索
*/
static int unbound_search(Table *t, unsigned int j)
{
    /* i 是零或存在的索引 */
    unsigned int i = j;
    j++;
    
    /* 找到 `i` 和 `j` 使得 i 存在而 j 不存在 */
    while (!ttisnil(luaH_getnum(t, j)))
    {
        i = j;
        j *= 2;
        
        /* 溢出？ */
        if (j > cast(unsigned int, MAX_INT))
        {
            /* 表是为了坏目的而构建的：求助于线性搜索 */
            i = 1;
            while (!ttisnil(luaH_getnum(t, i)))
            {
                i++;
            }

            return i - 1;
        }
    }
    
    /* 现在在它们之间进行二分搜索 */
    while (j - i > 1)
    {
        unsigned int m = (i + j) / 2;
        if (ttisnil(luaH_getnum(t, m)))
        {
            j = m;
        }
        else
        {
            i = m;
        }
    }

    return i;
}


/*
** 尝试在表 `t` 中找到边界。"边界"是一个整数索引，
** 使得 t[i] 非 nil 而 t[i+1] 是 nil（如果 t[1] 是 nil 则为 0）。
*/
int luaH_getn(Table *t)
{
    unsigned int j = t->sizearray;

    if (j > 0 && ttisnil(&t->array[j - 1]))
    {
        /* 数组部分有边界：（二分）搜索它 */
        unsigned int i = 0;
        while (j - i > 1)
        {
            unsigned int m = (i + j) / 2;
            if (ttisnil(&t->array[m - 1]))
            {
                j = m;
            }
            else
            {
                i = m;
            }
        }

        return i;
    }
    /* 否则必须在哈希部分找到边界 */
    /* 哈希部分是空的？ */
    else if (t->node == dummynode)
    {
        /* 那很容易... */
        return j;
    }
    else
    {
        return unbound_search(t, j);
    }
}



#if defined(LUA_DEBUG)

/*
** 调试函数：返回主位置
*/
Node *luaH_mainposition(const Table *t, const TValue *key)
{
  return mainposition(t, key);
}

/*
** 调试函数：检查是否为虚拟节点
*/
int luaH_isdummy (Node *n) { return n == dummynode; }

#endif