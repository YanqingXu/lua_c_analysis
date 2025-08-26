/*
** [核心] Lua函数管理模块
**
** 功能概述：
** 负责函数原型(Proto)和闭包(Closure)的创建、管理和释放，
** 是Lua函数系统的核心实现
**
** 主要数据结构：
** - Proto：函数原型，包含字节码、常量表、调试信息
** - Closure：闭包对象，分为C闭包和Lua闭包
** - UpVal：upvalue对象，实现词法作用域的变量捕获
**
** 核心机制：
** - upvalue的开放/关闭状态管理
** - 闭包的创建和内存分配
** - 函数原型的初始化和清理
** - 局部变量的调试信息查询
**
** 内存管理：
** - 与垃圾回收器紧密集成
** - 采用引用计数和标记清除混合策略
** - 支持增量垃圾回收
**
** 模块依赖：
** - lgc.c：垃圾回收管理
** - lmem.c：内存分配接口
** - lstring.c：字符串对象管理
**
** 相关文档：参见 docs/wiki_function.md
*/

#include <stddef.h>

#define lfunc_c
#define LUA_CORE

#include "lua.h"

#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"


/*
** [进阶] 创建C函数闭包
**
** 功能详述：
** 为C函数创建闭包对象，C闭包包含函数指针和upvalue数组
**
** 内存布局：
** - 固定部分：CClosure结构
** - 可变部分：upvalue数组[nelems]
**
** @param L - lua_State*：Lua状态机，用于内存分配和GC注册
** @param nelems - int：upvalue数量，决定内存分配大小
** @param e - Table*：环境表，C函数的全局环境
** @return Closure*：新创建的C闭包对象
**
** 算法复杂度：O(1) 时间，O(nelems) 空间
**
** 注意事项：
** - 新创建的闭包会立即注册到GC系统
** - upvalue初始值为未定义状态，需要调用者设置
*/
Closure *luaF_newCclosure(lua_State *L, int nelems, Table *e)
{
    Closure *c = cast(Closure *, luaM_malloc(L, sizeCclosure(nelems)));
    
    /*
    ** [内存] 将闭包链接到GC系统
    ** 类型标记为LUA_TFUNCTION，确保GC能正确处理
    */
    luaC_link(L, obj2gco(c), LUA_TFUNCTION);
    
    /*
    ** [初始化] 设置C闭包的基本属性
    */
    c->c.isC = 1;                              /* 标记为C闭包 */
    c->c.env = e;                              /* 设置环境表 */
    c->c.nupvalues = cast_byte(nelems);        /* upvalue数量 */
    
    return c;
}


/*
** 创建Lua闭包
** L: Lua状态机
** nelems: upvalue的数量
** e: 环境表
** 返回: 新创建的Lua闭包
*/
Closure *luaF_newLclosure(lua_State *L, int nelems, Table *e)
{
    Closure *c = cast(Closure *, luaM_malloc(L, sizeLclosure(nelems)));
    
    /* 将闭包链接到GC系统 */
    luaC_link(L, obj2gco(c), LUA_TFUNCTION);
    
    /* 初始化Lua闭包属性 */
    c->l.isC = 0;                              /* 标记为Lua闭包 */
    c->l.env = e;                              /* 设置环境表 */
    c->l.nupvalues = cast_byte(nelems);        /* 设置upvalue数量 */
    
    /* 初始化所有upvalue为NULL */
    while (nelems--) 
    {
        c->l.upvals[nelems] = NULL;
    }
        
    return c;
}


/*
** 创建新的upvalue对象
** L: Lua状态机
** 返回: 新创建的upvalue
*/
UpVal *luaF_newupval(lua_State *L)
{
    UpVal *uv = luaM_new(L, UpVal);
    
    /* 将upvalue链接到GC系统 */
    luaC_link(L, obj2gco(uv), LUA_TUPVAL);
    
    /* 初始化upvalue */
    uv->v = &uv->u.value;                      /* 指向自己的值 */
    setnilvalue(uv->v);                        /* 设置为nil值 */
    
    return uv;
}


/*
** 查找或创建指向指定栈位置的upvalue
** L: Lua状态机
** level: 栈中的位置
** 返回: 对应的upvalue对象
*/
UpVal *luaF_findupval(lua_State *L, StkId level)
{
    global_State *g = G(L);
    GCObject **pp = &L->openupval;
    UpVal *p;
    UpVal *uv;
    
    /* 在开放upvalue链表中查找 */
    while (*pp != NULL && (p = ngcotouv(*pp))->v >= level) {
        lua_assert(p->v != &p->u.value);
        
        /* 找到对应的upvalue */
        if (p->v == level) {
            /* 如果upvalue已被标记为死亡，复活它 */
            if (isdead(g, obj2gco(p)))
            {
                changewhite(obj2gco(p));
            }
            return p;
        }
        pp = &p->next;
    }
    
    /* 未找到：创建新的upvalue */
    uv = luaM_new(L, UpVal);
    uv->tt = LUA_TUPVAL;                       /* 设置类型 */
    uv->marked = luaC_white(g);                /* 设置GC标记 */
    uv->v = level;                             /* 当前值在栈中 */
    uv->next = *pp;                            /* 插入到适当位置 */
    *pp = obj2gco(uv);
    
    /* 双向链接到uvhead链表中 */
    uv->u.l.prev = &g->uvhead;
    uv->u.l.next = g->uvhead.u.l.next;
    uv->u.l.next->u.l.prev = uv;
    g->uvhead.u.l.next = uv;
    
    lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
    return uv;
}


/*
** 从uvhead链表中断开upvalue的链接
** uv: 要断开的upvalue
*/
static void unlinkupval(UpVal *uv)
{
    lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
    
    /* 从uvhead链表中移除 */
    uv->u.l.next->u.l.prev = uv->u.l.prev;
    uv->u.l.prev->u.l.next = uv->u.l.next;
}


/*
** 释放upvalue对象
** L: Lua状态机
** uv: 要释放的upvalue
*/
void luaF_freeupval(lua_State *L, UpVal *uv)
{
    /* 如果是开放的upvalue，从开放链表中移除 */
    if (uv->v != &uv->u.value)
    {
        unlinkupval(uv);
    }
        
    luaM_free(L, uv);  /* 释放upvalue内存 */
}


/*
** 关闭指定栈层级及以上的所有upvalue
** L: Lua状态机
** level: 栈层级
*/
void luaF_close(lua_State *L, StkId level)
{
    UpVal *uv;
    global_State *g = G(L);
    
    /* 处理所有需要关闭的upvalue */
    while (L->openupval != NULL && (uv = ngcotouv(L->openupval))->v >= level) {
        GCObject *o = obj2gco(uv);
        lua_assert(!isblack(o) && uv->v != &uv->u.value);
        
        /* 从开放链表中移除 */
        L->openupval = uv->next;
        
        if (isdead(g, o)) {
            /* 如果upvalue已死亡，直接释放 */
            luaF_freeupval(L, uv);
        }
        else {
            /* 关闭upvalue：将值复制到upvalue内部 */
            unlinkupval(uv);
            setobj(L, &uv->u.value, uv->v);    /* 复制当前值 */
            uv->v = &uv->u.value;              /* 现在值存储在这里 */
            luaC_linkupval(L, uv);             /* 链接到gcroot链表 */
        }
    }
}


/*
** 创建新的函数原型
** L: Lua状态机
** 返回: 新创建的函数原型
*/
Proto *luaF_newproto(lua_State *L)
{
    Proto *f = luaM_new(L, Proto);
    
    /* 将原型链接到GC系统 */
    luaC_link(L, obj2gco(f), LUA_TPROTO);
    
    /* 初始化所有字段为零/NULL */
    f->k = NULL;                               /* 常量数组 */
    f->sizek = 0;                              /* 常量数组大小 */
    f->p = NULL;                               /* 子函数数组 */
    f->sizep = 0;                              /* 子函数数组大小 */
    f->code = NULL;                            /* 字节码数组 */
    f->sizecode = 0;                           /* 字节码数组大小 */
    f->sizelineinfo = 0;                       /* 行信息数组大小 */
    f->sizeupvalues = 0;                       /* upvalue信息数组大小 */
    f->nups = 0;                               /* upvalue数量 */
    f->upvalues = NULL;                        /* upvalue信息数组 */
    f->numparams = 0;                          /* 参数数量 */
    f->is_vararg = 0;                          /* 是否为变参函数 */
    f->maxstacksize = 0;                       /* 最大栈大小 */
    f->lineinfo = NULL;                        /* 行信息数组 */
    f->sizelocvars = 0;                        /* 局部变量信息数组大小 */
    f->locvars = NULL;                         /* 局部变量信息数组 */
    f->linedefined = 0;                        /* 函数定义开始行 */
    f->lastlinedefined = 0;                    /* 函数定义结束行 */
    f->source = NULL;                          /* 源文件名 */
    
    return f;
}


/*
** 释放函数原型
** L: Lua状态机
** f: 要释放的函数原型
*/
void luaF_freeproto(lua_State *L, Proto *f)
{
    /* 释放所有数组 */
    luaM_freearray(L, f->code, f->sizecode, Instruction);
    luaM_freearray(L, f->p, f->sizep, Proto *);
    luaM_freearray(L, f->k, f->sizek, TValue);
    luaM_freearray(L, f->lineinfo, f->sizelineinfo, int);
    luaM_freearray(L, f->locvars, f->sizelocvars, struct LocVar);
    luaM_freearray(L, f->upvalues, f->sizeupvalues, TString *);
    
    /* 释放原型本身 */
    luaM_free(L, f);
}


/*
** 释放闭包对象
** L: Lua状态机
** c: 要释放的闭包
*/
void luaF_freeclosure(lua_State *L, Closure *c)
{
    /* 根据闭包类型计算大小 */
    int size = (c->c.isC) ? sizeCclosure(c->c.nupvalues) :
                            sizeLclosure(c->l.nupvalues);
                            
    luaM_freemem(L, c, size);
}


/*
** 在函数中查找第n个局部变量的名称
** f: 函数原型
** local_number: 局部变量编号
** pc: 程序计数器(指令位置)
** 返回: 变量名称，如果未找到返回NULL
*/
const char *luaF_getlocalname(const Proto *f, int local_number, int pc)
{
    int i;
    
    /* 遍历局部变量信息数组 */
    for (i = 0; i < f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
        /* 检查变量是否在指定位置活跃 */
        if (pc < f->locvars[i].endpc) {
            local_number--;
            if (local_number == 0)
            {
                return getstr(f->locvars[i].varname);
            }
        }
    }
    
    return NULL;  /* 未找到 */
}