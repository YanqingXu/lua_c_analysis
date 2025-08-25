/*
** $Id: lstring.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua字符串表实现 - 管理Lua中所有字符串的哈希表
** 版权声明见lua.h
*/


#include <string.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"



/*
** 重新调整字符串表的大小
** 当字符串表过于拥挤时，扩大哈希表以减少冲突
** 
** 参数说明：
** - L: Lua状态机
** - newsize: 新的哈希表大小
**
** 注意：在垃圾收集的字符串清理阶段不能调整大小
*/
void luaS_resize(lua_State *L, int newsize)
{
    GCObject **newhash;  /* 新哈希表的指针数组 */
    stringtable *tb;     /* 指向全局字符串表的指针 */
    int i;               /* 循环计数器 */

    /* 检查垃圾收集状态，如果正在清理字符串则不能调整大小 */
    if (G(L)->gcstate == GCSsweepstring)
    {
        return;  /* 在垃圾收集遍历期间不能调整大小 */
    }

    /* 分配新的哈希表数组，大小为newsize */
    newhash = luaM_newvector(L, newsize, GCObject *);

    /* 获取全局字符串表的引用 */
    tb = &G(L)->strt;

    /* 初始化新哈希表的所有槽位为NULL */
    for (i = 0; i < newsize; i++)
    {
        newhash[i] = NULL;
    }

    /* 重新哈希所有现有字符串到新表中 */
    for (i = 0; i < tb->size; i++)
    {
        GCObject *p = tb->hash[i];  /* 获取当前槽位的链表头 */
        while (p)  /* 遍历链表中的每个节点 */
        {
            GCObject *next = p->gch.next;  /* 保存下一个节点指针，避免链表断裂 */
            unsigned int h = gco2ts(p)->hash;  /* 获取字符串对象的哈希值 */
            int h1 = lmod(h, newsize);  /* 计算在新表中的位置 */
            lua_assert(cast_int(h % newsize) == lmod(h, newsize));  /* 断言：确保模运算正确 */
            p->gch.next = newhash[h1];  /* 将当前节点链接到新位置的链表头 */
            newhash[h1] = p;            /* 更新新位置的链表头为当前节点 */
            p = next;                   /* 移动到下一个节点 */
        }
    }

    /* 释放旧哈希表的内存 */
    luaM_freearray(L, tb->hash, tb->size, TString *);

    /* 更新字符串表的大小和哈希表指针 */
    tb->size = newsize;
    tb->hash = newhash;
}


/*
** 创建新的长字符串对象
** 这是一个内部函数，用于实际分配和初始化字符串对象
**
** 参数说明：
** - L: Lua状态机
** - str: 源字符串数据
** - l: 字符串长度
** - h: 预计算的哈希值
**
** 返回值：新创建的TString对象指针
*/
static TString *newlstr(lua_State *L, const char *str, size_t l,
                        unsigned int h)
{
    TString *ts;      /* 新创建的字符串对象指针 */
    stringtable *tb;  /* 全局字符串表指针 */

    /* 检查字符串长度是否会导致内存溢出 */
    if (l + 1 > (MAX_SIZET - sizeof(TString)) / sizeof(char))
    {
        luaM_toobig(L);  /* 字符串太长，抛出内存错误 */
    }

    /* 分配内存：TString结构体 + 字符串内容 + 结束符 */
    ts = cast(TString *, luaM_malloc(L, (l + 1) * sizeof(char) + sizeof(TString)));

    /* 初始化TString对象的各个字段 */
    ts->tsv.len = l;                    /* 设置字符串长度 */
    ts->tsv.hash = h;                   /* 设置预计算的哈希值 */
    ts->tsv.marked = luaC_white(G(L));  /* 设置GC标记为白色（新对象） */
    ts->tsv.tt = LUA_TSTRING;           /* 设置类型标记为字符串 */
    ts->tsv.reserved = 0;               /* 保留字段初始化为0 */

    /* 复制字符串内容到TString对象后面的内存区域 */
    memcpy(ts + 1, str, l * sizeof(char));
    ((char *)(ts + 1))[l] = '\0';  /* 添加C字符串结束符 */

    /* 将新字符串插入到字符串表中 */
    tb = &G(L)->strt;              /* 获取全局字符串表 */
    h = lmod(h, tb->size);         /* 计算在哈希表中的索引位置 */
    ts->tsv.next = tb->hash[h];    /* 将新字符串链接到对应槽位的链表头 */
    tb->hash[h] = obj2gco(ts);     /* 更新槽位的链表头为新字符串 */
    tb->nuse++;                    /* 增加字符串表中的字符串计数 */

    /* 检查是否需要扩容：如果使用率过高且表大小未达到上限 */
    if (tb->nuse > cast(lu_int32, tb->size) && tb->size <= MAX_INT / 2)
    {
        luaS_resize(L, tb->size * 2);  /* 表过于拥挤，扩容为原来的2倍 */
    }

    return ts;  /* 返回新创建的字符串对象 */
}


/*
** 创建新的长字符串或返回已存在的字符串
** 这是字符串内化(interning)的核心函数
**
** 参数说明：
** - L: Lua状态机
** - str: 源字符串数据
** - l: 字符串长度
**
** 返回值：TString对象指针（新创建或已存在的）
**
** 算法说明：
** 1. 计算字符串的哈希值（对于长字符串采用采样策略）
** 2. 在字符串表中查找是否已存在相同字符串
** 3. 如果存在则返回已有字符串，否则创建新字符串
*/
TString *luaS_newlstr(lua_State *L, const char *str, size_t l)
{
    GCObject *o;                         /* 用于遍历哈希链表的对象指针 */
    unsigned int h = cast(unsigned int, l);  /* 以字符串长度作为哈希种子 */
    size_t step = (l >> 5) + 1;              /* 采样步长：长字符串不哈希所有字符，提高性能 */
    size_t l1;                           /* 哈希计算的循环变量 */

    /* 计算字符串的哈希值（采用采样策略） */
    for (l1 = l; l1 >= step; l1 -= step)  /* 从字符串末尾开始，按步长采样字符 */
    {
        h = h ^ ((h << 5) + (h >> 2) + cast(unsigned char, str[l1 - 1]));  /* 混合哈希算法 */
    }

    /* 在字符串表中查找是否已存在相同的字符串 */
    for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];  /* 从对应哈希槽开始 */
         o != NULL;                                        /* 遍历整个冲突链表 */
         o = o->gch.next)                                 /* 移动到下一个节点 */
    {
        TString *ts = rawgco2ts(o);  /* 将GC对象转换为字符串对象 */

        /* 比较长度和内容是否完全相同 */
        if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0))
        {
            /* 找到相同字符串，检查是否被GC标记为死亡 */
            if (isdead(G(L), o))
            {
                changewhite(o);  /* 重新激活死亡对象 */
            }
            return ts;  /* 返回已存在的字符串对象 */
        }
    }

    /* 字符串表中不存在该字符串，创建新的字符串对象 */
    return newlstr(L, str, l, h);  /* 调用内部函数创建新字符串 */
}


/*
** 创建新的用户数据对象
** 用户数据是Lua中用于包装C数据的对象类型
**
** 参数说明：
** - L: Lua状态机
** - s: 用户数据的大小（字节数）
** - e: 环境表（用于访问控制）
**
** 返回值：新创建的Udata对象指针
**
** 注意：新创建的用户数据会被链接到主线程的GC链表中
*/
Udata *luaS_newudata(lua_State *L, size_t s, Table *e)
{
    Udata *u;  /* 新创建的用户数据对象指针 */

    /* 检查用户数据大小是否会导致内存溢出 */
    if (s > MAX_SIZET - sizeof(Udata))
    {
        luaM_toobig(L);  /* 用户数据太大，抛出内存错误 */
    }

    /* 分配内存：Udata结构体 + 用户数据内容 */
    u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));

    /* 初始化用户数据对象的各个字段 */
    u->uv.marked = luaC_white(G(L));  /* 设置GC标记为白色（新对象，未终结） */
    u->uv.tt = LUA_TUSERDATA;         /* 设置类型标记为用户数据 */
    u->uv.len = s;                    /* 设置用户数据的大小 */
    u->uv.metatable = NULL;           /* 初始化元表为空 */
    u->uv.env = e;                    /* 设置环境表（用于访问控制） */

    /* 将新用户数据对象链接到GC链表中 */
    /* 插入到主线程节点之后，便于垃圾收集器管理 */
    u->uv.next = G(L)->mainthread->next;  /* 新对象指向主线程的下一个对象 */
    G(L)->mainthread->next = obj2gco(u);  /* 主线程指向新创建的用户数据对象 */

    return u;  /* 返回新创建的用户数据对象 */
}