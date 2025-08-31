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
    // 分配C闭包所需的内存空间（固定部分+upvalue数组）
    Closure *c = cast(Closure *, luaM_malloc(L, sizeCclosure(nelems)));

    // 将新创建的闭包链接到GC系统，类型标记为LUA_TFUNCTION
    luaC_link(L, obj2gco(c), LUA_TFUNCTION);

    // 设置C闭包的基本属性
    c->c.isC = 1;                              // 标记为C闭包类型
    c->c.env = e;                              // 设置环境表
    c->c.nupvalues = cast_byte(nelems);        // 设置upvalue数量

    // 返回新创建的C闭包对象
    return c;
}


/*
** [进阶] 创建Lua函数闭包
**
** 详细功能说明：
** 为Lua函数创建闭包对象，Lua闭包包含函数原型指针和upvalue数组。
** 与C闭包不同，Lua闭包的upvalue需要单独分配和管理。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于内存分配和GC注册
** @param nelems - int：upvalue数量，决定内存分配大小
** @param e - Table*：环境表，Lua函数的全局环境
**
** 返回值：
** @return Closure*：新创建的Lua闭包对象
**
** 算法复杂度：O(nelems) 时间，O(nelems) 空间
**
** 注意事项：
** - 新创建的闭包会立即注册到GC系统
** - 所有upvalue初始化为NULL，需要后续设置
** - 内存分配失败时会抛出异常
*/
Closure *luaF_newLclosure(lua_State *L, int nelems, Table *e)
{
    // 分配Lua闭包所需的内存空间（固定部分+upvalue指针数组）
    Closure *c = cast(Closure *, luaM_malloc(L, sizeLclosure(nelems)));

    // 将新创建的闭包链接到GC系统，类型标记为LUA_TFUNCTION
    luaC_link(L, obj2gco(c), LUA_TFUNCTION);

    // 设置Lua闭包的基本属性
    c->l.isC = 0;                              // 标记为Lua闭包类型
    c->l.env = e;                              // 设置环境表
    c->l.nupvalues = cast_byte(nelems);        // 设置upvalue数量

    // 初始化所有upvalue指针为NULL
    while (nelems--)
    {
        c->l.upvals[nelems] = NULL;
    }

    // 返回新创建的Lua闭包对象
    return c;
}


/*
** [内部] 创建新的upvalue对象
**
** 详细功能说明：
** 创建一个新的upvalue对象，用于实现Lua的词法作用域变量捕获。
** 新创建的upvalue处于"关闭"状态，值存储在对象内部。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于内存分配和GC注册
**
** 返回值：
** @return UpVal*：新创建的upvalue对象，初始值为nil
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 新创建的upvalue会立即注册到GC系统
** - upvalue初始状态为关闭，值为nil
** - 内存分配失败时会抛出异常
*/
UpVal *luaF_newupval(lua_State *L)
{
    // 分配新的upvalue对象内存
    UpVal *uv = luaM_new(L, UpVal);

    // 将upvalue链接到GC系统，类型标记为LUA_TUPVAL
    luaC_link(L, obj2gco(uv), LUA_TUPVAL);

    // 初始化upvalue为关闭状态，值指向内部存储
    uv->v = &uv->u.value;                      // 指向自己的值存储区域
    setnilvalue(uv->v);                        // 设置初始值为nil

    // 返回新创建的upvalue对象
    return uv;
}


/*
** [核心] 查找或创建指向指定栈位置的upvalue
**
** 详细功能说明：
** 在开放upvalue链表中查找指向指定栈位置的upvalue，如果不存在则创建新的。
** 这是实现Lua词法作用域的关键函数，确保同一栈位置只有一个upvalue对象。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于访问开放upvalue链表
** @param level - StkId：栈中的位置，指向要捕获的变量
**
** 返回值：
** @return UpVal*：指向指定栈位置的upvalue对象
**
** 算法复杂度：O(n) 时间，其中n是开放upvalue的数量
**
** 注意事项：
** - 开放upvalue链表按栈位置降序排列
** - 如果upvalue被标记为死亡，会自动复活
** - 新创建的upvalue会插入到正确的位置保持排序
*/
UpVal *luaF_findupval(lua_State *L, StkId level)
{
    global_State *g = G(L);
    GCObject **pp = &L->openupval;
    UpVal *p;
    UpVal *uv;

    // 在开放upvalue链表中查找（链表按栈位置降序排列）
    while (*pp != NULL && (p = ngcotouv(*pp))->v >= level) {
        lua_assert(p->v != &p->u.value);

        // 找到指向相同栈位置的upvalue
        if (p->v == level) {
            // 如果upvalue已被标记为死亡，复活它
            if (isdead(g, obj2gco(p)))
            {
                changewhite(obj2gco(p));
            }
            return p;
        }
        pp = &p->next;
    }

    // 未找到：创建新的开放upvalue
    uv = luaM_new(L, UpVal);
    uv->tt = LUA_TUPVAL;                       // 设置对象类型
    uv->marked = luaC_white(g);                // 设置GC标记为白色
    uv->v = level;                             // 指向栈中的变量位置
    uv->next = *pp;                            // 插入到链表的正确位置
    *pp = obj2gco(uv);

    // 将upvalue双向链接到全局uvhead链表中
    uv->u.l.prev = &g->uvhead;
    uv->u.l.next = g->uvhead.u.l.next;
    uv->u.l.next->u.l.prev = uv;
    g->uvhead.u.l.next = uv;

    // 验证双向链表的完整性
    lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
    return uv;
}


/*
** [内部] 从uvhead链表中断开upvalue的链接
**
** 详细功能说明：
** 将upvalue从全局的uvhead双向链表中移除，这是upvalue关闭过程的一部分。
** 函数确保双向链表的完整性，正确更新前后节点的指针。
**
** 参数说明：
** @param uv - UpVal*：要从链表中移除的upvalue对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 调用前必须确保upvalue在uvhead链表中
** - 函数会验证双向链表的完整性
** - 仅断开链接，不释放upvalue内存
*/
static void unlinkupval(UpVal *uv)
{
    // 验证双向链表的完整性
    lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);

    // 从uvhead双向链表中移除当前upvalue
    uv->u.l.next->u.l.prev = uv->u.l.prev;
    uv->u.l.prev->u.l.next = uv->u.l.next;
}


/*
** [内部] 释放upvalue对象
**
** 详细功能说明：
** 释放upvalue对象占用的内存，如果是开放状态的upvalue，
** 还需要从uvhead链表中移除。这是垃圾回收过程的一部分。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于内存释放
** @param uv - UpVal*：要释放的upvalue对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 开放状态的upvalue需要先从链表中移除
** - 关闭状态的upvalue可以直接释放
** - 调用者需要确保upvalue不再被引用
*/
void luaF_freeupval(lua_State *L, UpVal *uv)
{
    // 如果是开放状态的upvalue，从uvhead链表中移除
    if (uv->v != &uv->u.value)
    {
        unlinkupval(uv);
    }

    // 释放upvalue对象的内存
    luaM_free(L, uv);
}


/*
** [核心] 关闭指定栈层级及以上的所有upvalue
**
** 详细功能说明：
** 当函数返回或栈收缩时，需要关闭指向即将失效栈位置的upvalue。
** 关闭过程将upvalue的值从栈复制到upvalue内部，使其独立于栈。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于访问开放upvalue链表
** @param level - StkId：栈层级，该层级及以上的upvalue将被关闭
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中n是需要关闭的upvalue数量
**
** 注意事项：
** - 开放upvalue链表按栈位置降序排列
** - 死亡的upvalue直接释放，活跃的upvalue转为关闭状态
** - 关闭后的upvalue会链接到gcroot链表
*/
void luaF_close(lua_State *L, StkId level)
{
    UpVal *uv;
    global_State *g = G(L);

    // 处理所有需要关闭的upvalue（按栈位置从高到低）
    while (L->openupval != NULL && (uv = ngcotouv(L->openupval))->v >= level) {
        GCObject *o = obj2gco(uv);
        lua_assert(!isblack(o) && uv->v != &uv->u.value);

        // 从开放upvalue链表中移除
        L->openupval = uv->next;

        if (isdead(g, o)) {
            // 如果upvalue已被标记为死亡，直接释放内存
            luaF_freeupval(L, uv);
        }
        else {
            // 关闭upvalue：将栈中的值复制到upvalue内部存储
            unlinkupval(uv);
            setobj(L, &uv->u.value, uv->v);    // 复制当前值到内部存储
            uv->v = &uv->u.value;              // 更新值指针指向内部存储
            luaC_linkupval(L, uv);             // 链接到gcroot链表进行GC管理
        }
    }
}


/*
** [核心] 创建新的函数原型对象
**
** 详细功能说明：
** 创建并初始化一个新的函数原型对象，用于存储Lua函数的编译信息。
** 函数原型包含字节码、常量表、调试信息等编译时生成的数据。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于内存分配和GC注册
**
** 返回值：
** @return Proto*：新创建的函数原型对象，所有字段初始化为零/NULL
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 新创建的原型会立即注册到GC系统
** - 所有数组字段初始化为NULL，需要后续分配
** - 内存分配失败时会抛出异常
*/
Proto *luaF_newproto(lua_State *L)
{
    // 分配新的函数原型对象内存
    Proto *f = luaM_new(L, Proto);

    // 将原型链接到GC系统，类型标记为LUA_TPROTO
    luaC_link(L, obj2gco(f), LUA_TPROTO);

    // 初始化常量表相关字段
    f->k = NULL;                               // 常量数组
    f->sizek = 0;                              // 常量数组大小

    // 初始化子函数相关字段
    f->p = NULL;                               // 子函数数组
    f->sizep = 0;                              // 子函数数组大小

    // 初始化字节码相关字段
    f->code = NULL;                            // 字节码数组
    f->sizecode = 0;                           // 字节码数组大小

    // 初始化调试信息相关字段
    f->sizelineinfo = 0;                       // 行信息数组大小
    f->lineinfo = NULL;                        // 行信息数组
    f->sizelocvars = 0;                        // 局部变量信息数组大小
    f->locvars = NULL;                         // 局部变量信息数组

    // 初始化upvalue相关字段
    f->sizeupvalues = 0;                       // upvalue信息数组大小
    f->nups = 0;                               // upvalue数量
    f->upvalues = NULL;                        // upvalue信息数组

    // 初始化函数属性字段
    f->numparams = 0;                          // 参数数量
    f->is_vararg = 0;                          // 是否为变参函数
    f->maxstacksize = 0;                       // 最大栈大小
    f->linedefined = 0;                        // 函数定义开始行
    f->lastlinedefined = 0;                    // 函数定义结束行
    f->source = NULL;                          // 源文件名

    // 返回新创建的函数原型对象
    return f;
}


/*
** [内部] 释放函数原型对象
**
** 详细功能说明：
** 释放函数原型对象及其包含的所有动态分配的数组。这是垃圾回收
** 过程的一部分，确保所有相关内存都被正确释放。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于内存释放
** @param f - Proto*：要释放的函数原型对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 必须按正确顺序释放所有数组
** - 调用者需要确保原型不再被引用
** - 子函数原型由GC系统单独管理
*/
void luaF_freeproto(lua_State *L, Proto *f)
{
    // 释放字节码数组
    luaM_freearray(L, f->code, f->sizecode, Instruction);

    // 释放子函数指针数组
    luaM_freearray(L, f->p, f->sizep, Proto *);

    // 释放常量数组
    luaM_freearray(L, f->k, f->sizek, TValue);

    // 释放行号信息数组
    luaM_freearray(L, f->lineinfo, f->sizelineinfo, int);

    // 释放局部变量信息数组
    luaM_freearray(L, f->locvars, f->sizelocvars, struct LocVar);

    // 释放upvalue名称数组
    luaM_freearray(L, f->upvalues, f->sizeupvalues, TString *);

    // 释放函数原型对象本身
    luaM_free(L, f);
}


/*
** [内部] 释放闭包对象
**
** 详细功能说明：
** 释放闭包对象占用的内存，根据闭包类型（C闭包或Lua闭包）
** 计算正确的内存大小。这是垃圾回收过程的一部分。
**
** 参数说明：
** @param L - lua_State*：Lua状态机指针，用于内存释放
** @param c - Closure*：要释放的闭包对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - C闭包和Lua闭包的内存布局不同
** - 内存大小包括固定部分和upvalue数组
** - 调用者需要确保闭包不再被引用
*/
void luaF_freeclosure(lua_State *L, Closure *c)
{
    // 根据闭包类型计算需要释放的内存大小
    int size = (c->c.isC) ? sizeCclosure(c->c.nupvalues) :
                            sizeLclosure(c->l.nupvalues);

    // 释放闭包对象的内存
    luaM_freemem(L, c, size);
}


/*
** [工具] 查找函数中第n个局部变量的名称
**
** 详细功能说明：
** 根据程序计数器位置和局部变量编号，查找对应的局部变量名称。
** 这个函数主要用于调试信息的获取和错误报告。
**
** 参数说明：
** @param f - const Proto*：函数原型对象，包含局部变量信息
** @param local_number - int：局部变量编号（从1开始）
** @param pc - int：程序计数器位置，指定查找的指令位置
**
** 返回值：
** @return const char*：变量名称字符串，未找到时返回NULL
**
** 算法复杂度：O(n) 时间，其中n是局部变量数量
**
** 注意事项：
** - 局部变量编号从1开始计数
** - 只返回在指定PC位置活跃的变量
** - 变量的活跃范围由startpc和endpc定义
*/
const char *luaF_getlocalname(const Proto *f, int local_number, int pc)
{
    int i;

    // 遍历局部变量信息数组，查找在指定PC位置活跃的变量
    for (i = 0; i < f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
        // 检查变量是否在指定PC位置活跃
        if (pc < f->locvars[i].endpc) {
            local_number--;
            if (local_number == 0)
            {
                // 找到第n个活跃的局部变量，返回其名称
                return getstr(f->locvars[i].varname);
            }
        }
    }

    // 未找到对应的局部变量
    return NULL;
}