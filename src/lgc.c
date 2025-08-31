/*
** [核心] Lua 垃圾回收器实现
**
** 功能概述：
** 本模块实现了Lua虚拟机的自动内存管理系统，采用增量式三色标记清除
** 垃圾回收算法。负责自动识别和回收不再使用的内存对象，确保程序
** 运行时的内存使用效率和稳定性。支持弱引用、终结化器等高级特性。
**
** 主要组件：
** - 标记阶段：三色标记算法，识别可达对象
** - 清扫阶段：回收白色（不可达）对象的内存
** - 终结化：执行用户数据的析构函数
** - 弱引用：支持弱键和弱值的表结构
** - 增量执行：分步执行以减少程序暂停时间
** - 写屏障：维护增量回收的正确性
**
** 算法特性：
** - 三色标记：白色（未访问）、灰色（待处理）、黑色（已处理）
** - 增量式：避免长时间暂停，提高响应性
** - 分代假设：新对象更容易成为垃圾
** - 自适应：根据内存使用情况调整回收频率
** - 并发安全：支持多线程环境下的安全回收
**
** 回收流程：
** 1. 暂停阶段：准备开始新的回收周期
** 2. 标记阶段：从根对象开始标记所有可达对象
** 3. 原子阶段：完成标记，处理弱引用
** 4. 清扫阶段：回收未标记的对象内存
** 5. 终结化阶段：执行需要终结化的对象
**
** 文件标识：lgc.c - Lua垃圾回收核心模块
** 版权声明：参见 lua.h 中的版权说明
*/

#include <string.h>

#define lgc_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"

// 垃圾回收相关常量定义
// 垃圾回收步骤大小
#define GCSTEPSIZE      1024u
// 每次清扫的最大对象数
#define GCSWEEPMAX      40
// 清扫操作的成本
#define GCSWEEPCOST     10
// 终结化操作的成本
#define GCFINALIZECOST  100

// 标记位操作宏定义
#define maskmarks       cast_byte(~(bitmask(BLACKBIT)|WHITEBITS))

// 将对象标记为白色
#define makewhite(g,x)  \
   ((x)->gch.marked = cast_byte(((x)->gch.marked & maskmarks) | luaC_white(g)))

// 颜色转换宏
#define white2gray(x)   reset2bits((x)->gch.marked, WHITE0BIT, WHITE1BIT)
#define black2gray(x)   resetbit((x)->gch.marked, BLACKBIT)

// 字符串标记宏
#define stringmark(s)   reset2bits((s)->tsv.marked, WHITE0BIT, WHITE1BIT)

// 终结化状态检查和设置
#define isfinalized(u)      testbit((u)->marked, FINALIZEDBIT)
#define markfinalized(u)    l_setbit((u)->marked, FINALIZEDBIT)

// 弱引用类型定义
#define KEYWEAK         bitmask(KEYWEAKBIT)
#define VALUEWEAK       bitmask(VALUEWEAKBIT)

// 标记值和对象的宏
#define markvalue(g,o) \
    { \
        checkconsistency(o); \
        if (iscollectable(o) && iswhite(gcvalue(o))) \
            reallymarkobject(g,gcvalue(o)); \
    }

#define markobject(g,t) \
    { \
        if (iswhite(obj2gco(t))) \
            reallymarkobject(g, obj2gco(t)); \
    }

// 设置垃圾回收阈值
#define setthreshold(g)  (g->GCthreshold = (g->estimate/100) * g->gcpause)

/*
** [工具] 从表节点中移除条目
**
** 详细功能说明：
** 当节点的值为 nil 时，检查键是否为可回收对象。如果是，则将键
** 标记为死键（LUA_TDEADKEY），这样在后续的表遍历中可以识别并
** 跳过这些无效的条目。
**
** 参数说明：
** @param n - Node*：要处理的表节点指针
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 调用前必须确保节点值为 nil
** - 只处理可回收的键对象
**
** 逐行详细注释（短函数详细说明）：
*/
static void removeentry(Node *n)
{
    // 断言检查：确保节点的值确实为 nil，这是调用此函数的前提条件
    lua_assert(ttisnil(gval(n)));
    
    // 条件检查：判断节点的键是否为可回收的对象类型
    // 只有可回收对象才需要特殊处理，基本类型（数字、布尔等）不需要
    if (iscollectable(gkey(n)))
    {
        // 标记操作：将键的类型设置为 LUA_TDEADKEY（死键标记）
        // 这样在后续的表遍历中可以识别并跳过这些无效条目
        setttype(gkey(n), LUA_TDEADKEY);
    }
}

/*
** [核心] 真正标记对象的核心函数
**
** 详细功能说明：
** 这是垃圾回收器的核心标记函数，负责将白色对象标记为灰色或黑色。
** 根据不同的对象类型采用不同的标记策略：字符串直接标记为黑色，
** 复合对象（表、函数、线程等）先标记为灰色并加入灰色列表等待遍历。
**
** 参数说明：
** @param g - global_State*：全局状态指针，包含垃圾回收器状态
** @param o - GCObject*：要标记的对象指针
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 调用前对象必须是白色且未死亡
** - 复合对象会被加入相应的灰色列表
** - 字符串和用户数据会直接变为黑色
*/
static void reallymarkobject(global_State *g, GCObject *o)
{
    // 断言确保对象是白色且未死亡
    lua_assert(iswhite(o) && !isdead(g, o));
    
    // 将白色对象转换为灰色
    white2gray(o);
    
    switch (o->gch.tt)
    {
        case LUA_TSTRING:
        {
            // 字符串对象没有引用其他对象，直接标记为黑色
            gray2black(o);
            return;
        }
        
        case LUA_TUSERDATA:
        {
            Table *mt = gco2u(o)->metatable;
            
            // 用户数据对象标记为黑色
            gray2black(o);
            
            // 如果有元表，则标记元表
            if (mt)
            {
                markobject(g, mt);
            }
            
            // 标记用户数据的环境表
            markobject(g, gco2u(o)->env);
            return;
        }
        
        case LUA_TUPVAL:
        {
            UpVal *uv = gco2uv(o);
            
            // 标记上值指向的值
            markvalue(g, uv->v);
            
            // 检查是否为闭合的上值（值存储在上值对象内部）
            if (uv->v == &uv->u.value)
            {
                // 闭合的上值直接标记为黑色
                gray2black(o);
            }
            return;
        }
        
        case LUA_TFUNCTION:
        {
            // 将函数对象加入灰色列表等待遍历
            gco2cl(o)->c.gclist = g->gray;
            g->gray = o;
            break;
        }
        
        case LUA_TTABLE:
        {
            // 将表对象加入灰色列表等待遍历
            gco2h(o)->gclist = g->gray;
            g->gray = o;
            break;
        }
        
        case LUA_TTHREAD:
        {
            // 将线程对象加入灰色列表等待遍历
            gco2th(o)->gclist = g->gray;
            g->gray = o;
            break;
        }
        
        case LUA_TPROTO:
        {
            // 将函数原型加入灰色列表等待遍历
            gco2p(o)->gclist = g->gray;
            g->gray = o;
            break;
        }
        
        default:
        {
            lua_assert(0);
        }
    }
}

/*
** [工具] 标记需要终结化的用户数据
**
** 详细功能说明：
** 遍历待终结化列表（tmudata）中的所有用户数据对象，将它们重新
** 标记为可达。这是为了确保这些对象在当前垃圾回收周期中不会被
** 释放，而是等待终结化处理完成。
**
** 参数说明：
** @param g - global_State*：全局状态指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中 n 是待终结化对象的数量
**
** 注意事项：
** - 使用循环链表遍历所有待终结化对象
** - 对象可能在上次垃圾回收中已被标记
** - 需要重新标记以确保在当前周期中可达
**
** 逐行详细注释（短函数详细说明）：
*/
static void marktmu(global_State *g)
{
    // 获取起始对象：从全局状态中获取待终结化对象链表的头指针
    GCObject *u = g->tmudata;
    
    // 空值检查：如果待终结化列表不为空，则进行遍历处理
    if (u)
    {
        // 循环遍历：使用 do-while 循环遍历整个循环链表
        do
        {
            // 移动指针：移动到链表中的下一个对象
            u = u->gch.next;
            // 重置颜色：将对象标记为白色，清除之前的标记状态
            // 可能已被标记，如果是上次 GC 遗留的
            makewhite(g, u);
            // 重新标记：将对象重新标记为可达，确保不会被回收
            // 重新标记
            reallymarkobject(g, u);
        }
        // 循环条件：当回到起始对象时结束循环（循环链表特性）
        while (u != g->tmudata);
    }
}

/*
** [接口] 分离需要终结化的用户数据
**
** 详细功能说明：
** 遍历主对象列表，找出需要终结化的用户数据对象并将它们移动到
** 专门的待终结化列表（tmudata）中。这些对象具有 __gc 元方法
** 且当前为白色（即将被回收）。分离后的对象会在后续阶段调用
** 其终结化方法。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param all - int：是否处理所有对象（非零值表示处理所有对象）
**
** 返回值：
** @return size_t：被分离的用户数据对象的总内存大小
**
** 算法复杂度：O(n) 时间，其中 n 是主对象列表中的对象数量
**
** 注意事项：
** - 只处理具有 __gc 元方法的用户数据
** - 已终结化的对象不会被重复处理
** - 使用循环链表管理待终结化对象
*/
size_t luaC_separateudata(lua_State *L, int all)
{
    global_State *g = G(L);
    size_t deadmem = 0;
    GCObject **p = &g->mainthread->next;
    GCObject *curr;
    
    while ((curr = *p) != NULL)
    {
        if (!(iswhite(curr) || all) || isfinalized(gco2u(curr)))
        {
            // 不处理这些对象
            p = &curr->gch.next;
        }
        else if (fasttm(L, gco2u(curr)->metatable, TM_GC) == NULL)
        {
            // 不需要终结化
            markfinalized(gco2u(curr));
            p = &curr->gch.next;
        }
        else
        {
            // 必须调用其 gc 方法
            deadmem += sizeudata(gco2u(curr));
            markfinalized(gco2u(curr));
            *p = curr->gch.next;
            
            // 将 curr 链接到 tmudata 列表的末尾
            // 检查 tmudata 列表是否为空
            if (g->tmudata == NULL)
            {
                // 创建循环列表
                g->tmudata = curr->gch.next = curr;
            }
            else
            {
                curr->gch.next = g->tmudata->gch.next;
                g->tmudata->gch.next = curr;
                g->tmudata = curr;
            }
        }
    }
    
    return deadmem;
}

/*
** [核心] 遍历表对象并处理弱引用
**
** 详细功能说明：
** 遍历表对象的所有键值对，根据表的弱引用模式决定如何标记。
** 对于普通表，标记所有键值对；对于弱表，根据弱引用类型（键弱、
** 值弱或键值都弱）选择性标记。弱表会被加入特殊列表以便后续
** 清理无效引用。
**
** 参数说明：
** @param g - global_State*：全局状态指针
** @param h - Table*：要遍历的表对象指针
**
** 返回值：
** @return int：如果表是弱引用表返回非零值，否则返回 0
**
** 算法复杂度：O(n) 时间，其中 n 是表中的元素数量
**
** 注意事项：
** - 会检查表的元表和弱引用模式
** - 弱表会被加入全局弱引用列表
** - 空条目会被移除以优化表结构
*/
static int traversetable(global_State *g, Table *h)
{
    int i;
    int weakkey = 0;
    int weakvalue = 0;
    const TValue *mode;
    
    if (h->metatable)
    {
        // 标记元表
        markobject(g, h->metatable);
    }
    
    mode = gfasttm(g, h->metatable, TM_MODE);
    
    // 检查是否有弱引用模式
    if (mode && ttisstring(mode))
    {
        weakkey = (strchr(svalue(mode), 'k') != NULL);
        weakvalue = (strchr(svalue(mode), 'v') != NULL);
        
        // 确认是否为弱引用表
        if (weakkey || weakvalue)
        {
            // 清除位
            h->marked &= ~(KEYWEAK | VALUEWEAK);
            h->marked |= cast_byte((weakkey << KEYWEAKBIT) |
                                 (weakvalue << VALUEWEAKBIT));
            // 必须在 GC 后清除
            h->gclist = g->weak;
            // 放入适当的列表
            g->weak = obj2gco(h);
        }
    }
    
    if (weakkey && weakvalue)
    {
        return 1;
    }
    
    if (!weakvalue)
    {
        i = h->sizearray;
        while (i--)
        {
            // 标记数组部分
            markvalue(g, &h->array[i]);
        }
    }
    
    i = sizenode(h);
    while (i--)
    {
        Node *n = gnode(h, i);
        lua_assert(ttype(gkey(n)) != LUA_TDEADKEY || ttisnil(gval(n)));
        
        if (ttisnil(gval(n)))
        {
            // 移除空条目
            removeentry(n);
        }
        else
        {
            lua_assert(!ttisnil(gkey(n)));
            
            if (!weakkey)
            {
                // 标记键
                markvalue(g, gkey(n));
            }
            
            if (!weakvalue)
            {
                // 标记值
                markvalue(g, gval(n));
            }
        }
    }
    
    return weakkey || weakvalue;
}

/*
** [核心] 遍历函数原型对象
**
** 详细功能说明：
** 遍历函数原型对象的所有组成部分，包括源代码字符串、常量表、
** 上值名称、嵌套函数原型、局部变量名等。所有标记都是条件性的，
** 因为在原型创建过程中可能发生垃圾回收，某些字段可能为空。
**
** 参数说明：
** @param g - global_State*：全局状态指针
** @param f - Proto*：要遍历的函数原型指针
**
** 返回值：无
**
** 算法复杂度：O(k + u + p + v) 时间，其中 k 是常量数、u 是上值数、
**             p 是嵌套原型数、v 是局部变量数
**
** 注意事项：
** - 所有指针字段都需要空值检查
** - 字符串对象使用特殊的 stringmark 函数标记
** - 嵌套原型会递归标记
*/
static void traverseproto(global_State *g, Proto *f)
{
    int i;
    
    if (f->source)
    {
        // 标记源代码字符串
        stringmark(f->source);
    }
    
    // 标记字面量
    for (i = 0; i < f->sizek; i++)
    {
        markvalue(g, &f->k[i]);
    }
    
    // 标记上值名称
    for (i = 0; i < f->sizeupvalues; i++)
    {
        if (f->upvalues[i])
        {
            stringmark(f->upvalues[i]);
        }
    }
    
    // 标记嵌套原型
    for (i = 0; i < f->sizep; i++)
    {
        if (f->p[i])
        {
            markobject(g, f->p[i]);
        }
    }
    
    // 标记局部变量名
    for (i = 0; i < f->sizelocvars; i++)
    {
        if (f->locvars[i].varname)
        {
            stringmark(f->locvars[i].varname);
        }
    }
}

/*
** [核心] 遍历闭包对象
**
** 详细功能说明：
** 遍历闭包对象的所有组成部分，包括环境表和上值。对于 C 闭包，
** 标记其 C 上值数组；对于 Lua 闭包，标记其函数原型和 Lua 上值
** 对象。这确保闭包引用的所有对象都被正确标记。
**
** 参数说明：
** @param g - global_State*：全局状态指针
** @param cl - Closure*：要遍历的闭包对象指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中 n 是闭包的上值数量
**
** 注意事项：
** - C 闭包和 Lua 闭包的处理方式不同
** - C 闭包的上值是 TValue 数组
** - Lua 闭包的上值是 UpVal 对象指针数组
*/
static void traverseclosure(global_State *g, Closure *cl)
{
    // 标记环境
    markobject(g, cl->c.env);
    
    if (cl->c.isC)
    {
        int i;
        
        for (i = 0; i < cl->c.nupvalues; i++)  // 标记 C 闭包的上值
        {
            markvalue(g, &cl->c.upvalue[i]);
        }
    }
    else
    {
        int i;
        
        lua_assert(cl->l.nupvalues == cl->l.p->nups);
        // 标记原型
        markobject(g, cl->l.p);
        
        for (i = 0; i < cl->l.nupvalues; i++)  // 标记 Lua 闭包的上值
        {
            markobject(g, cl->l.upvals[i]);
        }
    }
}

/*
** [优化] 检查并调整栈大小
**
** 详细功能说明：
** 检查 Lua 状态机的栈和调用信息栈的使用情况，如果使用率较低
** （小于 25%），则缩小栈大小以节省内存。这是一个内存优化措施，
** 避免长期占用过多的栈空间。
**
** 参数说明：
** @param L - lua_State*：要检查的 Lua 状态机指针
** @param max - StkId：栈的最大使用位置
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只在使用率低于 25% 时才缩小栈
** - 保持最小栈大小以避免频繁重分配
** - 处理栈溢出情况时不进行调整
*/
static void checkstacksizes(lua_State *L, StkId max)
{
    // 使用中的 ci 数量
  int ci_used = cast_int(L->ci - L->base_ci);
  // 使用中的栈部分
  int s_used = cast_int(max - L->stack);
    
    // 检查调用栈是否溢出
    if (L->size_ci > LUAI_MAXCALLS)
    {
        // 不触碰栈
    return;
    }
    
    if (4 * ci_used < L->size_ci && 2 * BASIC_CI_SIZE < L->size_ci)
    {
        // 仍然足够大
    luaD_reallocCI(L, L->size_ci / 2);
    }
    
    condhardstacktests(luaD_reallocCI(L, ci_used + 1));
    
    if (4 * s_used < L->stacksize &&
        2 * (BASIC_STACK_SIZE + EXTRA_STACK) < L->stacksize)
    {
        // 仍然足够大
    luaD_reallocstack(L, L->stacksize / 2);
    }
    
    condhardstacktests(luaD_reallocstack(L, s_used));
}

/*
** [核心] 遍历线程栈并标记所有可达对象
**
** 详细功能说明：
** 遍历 Lua 线程的整个栈空间，标记栈中的所有值以及全局表。
** 同时检查所有调用信息以确定栈的实际使用范围，清空未使用的
** 栈位置，并调用栈大小检查函数进行内存优化。
**
** 参数说明：
** @param g - global_State*：全局状态指针
** @param l - lua_State*：要遍历的线程状态指针
**
** 返回值：无
**
** 算法复杂度：O(n + m) 时间，其中 n 是栈大小，m 是调用信息数量
**
** 注意事项：
** - 会标记线程的全局表
** - 清空未使用的栈位置以避免误标记
** - 调用栈大小检查进行内存优化
*/
static void traversestack(global_State *g, lua_State *l)
{
    StkId o, lim;
    CallInfo *ci;
    
    // 标记全局表
  markvalue(g, gt(l));
    lim = l->top;
    
    for (ci = l->base_ci; ci <= l->ci; ci++)
    {
        lua_assert(ci->top <= l->stack_last);
        
        if (lim < ci->top)
        {
            lim = ci->top;
        }
    }
    
    for (o = l->stack; o < l->top; o++)
    {
        // 标记栈中的值
        markvalue(g, o);
    }
    
    for (; o <= lim; o++)
    {
        // 清空未使用的栈位置
        setnilvalue(o);
    }
    
    // 检查并调整栈大小
    checkstacksizes(l, lim);
}

/*
** [核心] 传播标记到一个灰色对象
**
** 详细功能说明：
** 从灰色对象列表中取出一个对象，根据其类型进行相应的遍历处理，
** 并将其标记为黑色。对于不同类型的对象采用不同的遍历策略：
** 表和线程可能保持灰色状态，函数和原型会被完全遍历。
**
** 参数说明：
** @param g - global_State*：全局状态指针
**
** 返回值：
** @return l_mem：遍历此对象所涉及的内存大小
**
** 算法复杂度：取决于对象类型，从 O(1) 到 O(n)
**
** 注意事项：
** - 弱表会保持灰色状态等待后续处理
** - 线程对象会被重新加入灰色列表
** - 返回值用于垃圾回收的进度控制
*/
static l_mem propagatemark(global_State *g)
{
    GCObject *o = g->gray;
    lua_assert(isgray(o));
    // 将灰色转为黑色
    gray2black(o);
    
    switch (o->gch.tt)
    {
        case LUA_TTABLE:
        {
            Table *h = gco2h(o);
            g->gray = h->gclist;
            
            // 检查表是否为弱引用
            if (traversetable(g, h))
            {
                // 保持灰色
    black2gray(o);
            }
            
            return sizeof(Table) + sizeof(TValue) * h->sizearray +
                                 sizeof(Node) * sizenode(h);
        }
        
        case LUA_TFUNCTION:
        {
            Closure *cl = gco2cl(o);
            g->gray = cl->c.gclist;
            traverseclosure(g, cl);
            
            return (cl->c.isC) ? sizeCclosure(cl->c.nupvalues) :
                               sizeLclosure(cl->l.nupvalues);
        }
        
        case LUA_TTHREAD:
        {
            lua_State *th = gco2th(o);
            g->gray = th->gclist;
            th->gclist = g->grayagain;
            g->grayagain = o;
            black2gray(o);
            traversestack(g, th);
            
            return sizeof(lua_State) + sizeof(TValue) * th->stacksize +
                                     sizeof(CallInfo) * th->size_ci;
        }
        
        case LUA_TPROTO:
        {
            Proto *p = gco2p(o);
            g->gray = p->gclist;
            traverseproto(g, p);
            
            return sizeof(Proto) + sizeof(Instruction) * p->sizecode +
                                 sizeof(Proto *) * p->sizep +
                                 sizeof(TValue) * p->sizek +
                                 sizeof(int) * p->sizelineinfo +
                                 sizeof(LocVar) * p->sizelocvars +
                                 sizeof(TString *) * p->sizeupvalues;
        }
        
        default:
        {
            lua_assert(0);
            return 0;
        }
    }
}

/*
** [核心] 传播所有待处理的标记
**
** 详细功能说明：
** 循环调用 propagatemark 函数，处理灰色对象列表中的所有对象，
** 直到没有更多的灰色对象需要处理。这是标记阶段的核心循环，
** 确保所有可达对象都被正确标记。
**
** 参数说明：
** @param g - global_State*：全局状态指针
**
** 返回值：
** @return size_t：总共遍历的内存大小
**
** 算法复杂度：O(n) 时间，其中 n 是所有可达对象的总数
**
** 注意事项：
** - 会处理所有灰色对象直到列表为空
** - 返回值用于垃圾回收的统计和控制
** - 某些对象可能在处理过程中重新变为灰色
**
** 逐行详细注释（短函数详细说明）：
*/
static size_t propagateall(global_State *g)
{
    // 计数器初始化：用于累计所有传播操作处理的内存大小
    size_t m = 0;
    
    // 循环处理：当灰色对象列表不为空时继续处理
    // g->gray 指向灰色对象链表的头部
    while (g->gray)
    {
        // 传播标记：处理一个灰色对象，并累加处理的内存大小
        // propagatemark 会从灰色列表中取出一个对象进行遍历
        m += propagatemark(g);
    }
    
    // 返回总计：返回本次传播操作处理的总内存大小
    return m;
}

/*
** [工具] 检查弱表中的对象是否应被清除
**
** 详细功能说明：
** 判断弱表中的键或值是否应该被清除。清除规则如下：
** 1. 不可回收对象（如数字、布尔值）永远不清除
** 2. 字符串对象永远不清除（被视为"值"）
** 3. 白色对象（垃圾）应该被清除
** 4. 正在终结化的用户数据：作为键时保留，作为值时清除
**
** 参数说明：
** @param o - const TValue*：要检查的值指针
** @param iskey - int：是否为键（非零值表示是键，零表示是值）
**
** 返回值：
** @return int：如果应该清除返回非零值，否则返回 0
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 字符串会被重新标记以确保不被回收
** - 终结化用户数据的处理取决于是键还是值
** - 用于弱表的清理过程
*/
static int iscleared(const TValue *o, int iskey)
{
    if (!iscollectable(o))
    {
        return 0;
    }
    
    if (ttisstring(o))
    {
        // 字符串是"值"，所以永远不弱
      stringmark(rawtsvalue(o));
        return 0;
    }
    
    return iswhite(gcvalue(o)) ||
           (ttisuserdata(o) && (!iskey && isfinalized(uvalue(o))));
}

/*
** [核心] 清除弱表中的无效条目
**
** 详细功能说明：
** 遍历弱表的所有条目，检查每个键值对是否应该被清除。
** 如果键或值中任何一个被标记为应清除（通过 iscleared 函数判断），
** 则将该条目从表中移除。这是弱表垃圾回收的关键步骤。
**
** 参数说明：
** @param l - GCObject*：要清理的弱表对象指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中 n 是表中的条目数量
**
** 注意事项：
** - 只处理弱表（具有弱引用模式的表）
** - 会修改表的结构，移除无效条目
** - 在垃圾回收的原子阶段调用
*/
static void cleartable(GCObject *l)
{
    while (l)
    {
        Table *h = gco2h(l);
        int i = h->sizearray;
        
        lua_assert(testbit(h->marked, VALUEWEAKBIT) ||
                   testbit(h->marked, KEYWEAKBIT));
        
        if (testbit(h->marked, VALUEWEAKBIT))
        {
            while (i--)
            {
                TValue *o = &h->array[i];
                // 检查值是否已被回收
                if (iscleared(o, 0))
                {
                    // 移除值
      setnilvalue(o);
                }
            }
        }
        
        i = sizenode(h);
        while (i--)
        {
            Node *n = gnode(h, i);
            // 检查是否为非空条目
            if (!ttisnil(gval(n)) &&
                (iscleared(key2tval(n), 1) || iscleared(gval(n), 0)))
            {
                // 移除值
    setnilvalue(gval(n));
    // 从表中移除条目
    removeentry(n);
            }
        }
        
        l = h->gclist;
    }
}

/*
** 释放对象
** 根据对象类型调用相应的释放函数
*/
static void freeobj(lua_State *L, GCObject *o)
{
    switch (o->gch.tt)
    {
        case LUA_TPROTO:
            luaF_freeproto(L, gco2p(o));
            break;
            
        case LUA_TFUNCTION:
            luaF_freeclosure(L, gco2cl(o));
            break;
            
        case LUA_TUPVAL:
            luaF_freeupval(L, gco2uv(o));
            break;
            
        case LUA_TTABLE:
            luaH_free(L, gco2h(o));
            break;
            
        case LUA_TTHREAD:
        {
            lua_assert(gco2th(o) != L && gco2th(o) != G(L)->mainthread);
            luaE_freethread(L, gco2th(o));
            break;
        }
        
        case LUA_TSTRING:
        {
            G(L)->strt.nuse--;
            luaM_freemem(L, o, sizestring(gco2ts(o)));
            break;
        }
        
        case LUA_TUSERDATA:
        {
            luaM_freemem(L, o, sizeudata(gco2u(o)));
            break;
        }
        
        default:
            lua_assert(0);
    }
}

// 清扫整个列表的宏定义
#define sweepwholelist(L,p) sweeplist(L,p,MAX_LUMEM)

/*
** [核心] 清扫对象链表并释放垃圾
**
** 详细功能说明：
** 遍历对象链表，释放其中的白色对象（垃圾），保留黑色和灰色对象。
** 同时将所有保留对象的颜色重置为白色，为下一轮垃圾回收做准备。
** 这是垃圾回收清扫阶段的核心函数。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param p - GCObject**：指向链表头指针的指针
** @param count - lu_mem：要处理的最大对象数量
**
** 返回值：
** @return GCObject**：指向下一个要处理位置的指针
**
** 算法复杂度：O(min(n, count)) 时间，其中 n 是链表长度
**
** 注意事项：
** - 会修改链表结构，移除垃圾对象
** - 重置保留对象的颜色标记
** - 支持增量清扫以避免长时间暂停
*/
static GCObject **sweeplist(lua_State *L, GCObject **p, lu_mem count)
{
    GCObject *curr;
    global_State *g = G(L);
    int deadmask = otherwhite(g);
    
    while ((curr = *p) != NULL && count-- > 0)
    {
        if (curr->gch.tt == LUA_TTHREAD)  // 清扫每个线程的开放上值
        {
            sweepwholelist(L, &gco2th(curr)->openupval);
        }
        
        // 检查是否为活跃对象
        if ((curr->gch.marked ^ WHITEBITS) & deadmask)
        {
            lua_assert(!isdead(g, curr) || testbit(curr->gch.marked, FIXEDBIT));
            // 标记为白色（为下次循环准备）
    makewhite(g, curr);
            p = &curr->gch.next;
        }
        else  // 必须删除 curr
        {
            lua_assert(isdead(g, curr) || deadmask == bitmask(SFIXEDBIT));
            *p = curr->gch.next;
            
            // 检查是否为根列表的第一个元素
            if (curr == g->rootgc)
            {
                // 调整第一个
  g->rootgc = curr->gch.next;
            }
            
            freeobj(L, curr);
        }
    }
    
    return p;
}

/*
** [优化] 检查并调整内存结构大小
**
** 详细功能说明：
** 检查字符串哈希表和全局缓冲区的使用情况，如果使用率过低
** 则缩小其大小以节省内存。这是垃圾回收后的内存优化步骤，
** 避免长期占用过多的内存空间。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只在使用率低于 25% 时才缩小哈希表
** - 保持最小大小以避免频繁重分配
** - 在垃圾回收的清扫阶段结束时调用
*/
static void checkSizes(lua_State *L)
{
    global_State *g = G(L);
    
    // 检查字符串哈希表大小
    if (g->strt.nuse < cast(lu_int32, g->strt.size / 4) &&
        g->strt.size > MINSTRTABSIZE * 2)
    {
        // 表太大了
    luaS_resize(L, g->strt.size / 2);
    }
    
    // 检查缓冲区大小
    // 检查缓冲区是否过大
    if (luaZ_sizebuffer(&g->buff) > LUA_MINBUFFER * 2)
    {
        size_t newsize = luaZ_sizebuffer(&g->buff) / 2;
        luaZ_resizebuffer(L, &g->buff, newsize);
    }
}

/*
** [核心] 执行单个用户数据对象的终结化处理
**
** 详细功能说明：
** 这是垃圾回收器的终结化函数，负责调用用户数据对象的 __gc 元方法。
** 函数会从待终结列表中取出对象，调用其 __gc 元方法，并在调用期间
** 暂停垃圾回收和调试钩子以避免干扰。调用完成后，对象会被重新
** 标记为白色并返回到主对象列表中。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 调用期间会暂停垃圾回收和调试钩子
** - 对象调用后会重新变为白色，可能在下次回收中被释放
** - 如果没有 __gc 元方法，对象会直接返回主列表
*/
static void GCTM(lua_State *L)
{
    global_State *g = G(L);
    
    // 获取待终结列表中的第一个对象
    GCObject *o = g->tmudata->gch.next;
    Udata *udata = rawgco2u(o);
    const TValue *tm;
    
    // 从待终结列表中移除用户数据对象
    // 检查是否为列表中的最后一个元素
    if (o == g->tmudata)
    {
        // 清空列表
  g->tmudata = NULL;
    }
    else
    {
        // 跳过当前对象
    g->tmudata->gch.next = udata->uv.next;
    }
    
    // 将对象返回到主对象列表
    udata->uv.next = g->mainthread->next;
    g->mainthread->next = o;
    
    // 将对象重新标记为白色
    makewhite(g, o);
    
    // 获取对象的 __gc 元方法
    tm = fasttm(L, udata->uv.metatable, TM_GC);
    
    // 如果存在 __gc 元方法，则调用它
    if (tm != NULL)
    {
        lu_byte oldah = L->allowhook;
        lu_mem oldt = g->GCthreshold;
        
        // 暂停调试钩子，避免在终结化过程中触发
        L->allowhook = 0;
        
        // 提高垃圾回收阈值，避免在终结化过程中进行回收
        g->GCthreshold = 2 * g->totalbytes;
        
        // 将终结器函数和用户数据压入栈
        setobj2s(L, L->top, tm);
        setuvalue(L, L->top + 1, udata);
        L->top += 2;
        
        // 调用终结器函数
        luaD_call(L, L->top - 2, 0);
        
        // 恢复原始的调试钩子状态
        L->allowhook = oldah;
        
        // 恢复原始的垃圾回收阈值
        g->GCthreshold = oldt;
    }
}

/*
** [接口] 调用所有待终结对象的 __gc 元方法
**
** 详细功能说明：
** 这是一个公共接口函数，用于处理所有待终结的用户数据对象。
** 函数会循环调用 GCTM 函数，直到所有待终结列表中的对象都被
** 处理完毕。通常在垃圾回收的终结化阶段或程序结束时调用。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中 n 是待终结对象的数量
**
** 注意事项：
** - 会处理所有待终结列表中的对象
** - 每个对象的 __gc 元方法都会被调用
** - 调用顺序与对象加入列表的顺序相关
*/
void luaC_callGCTM(lua_State *L)
{
    // 循环处理所有待终结的对象
    while (G(L)->tmudata)
    {
        GCTM(L);
    }
}

/*
** [接口] 释放虚拟机中的所有对象
**
** 详细功能说明：
** 这是垃圾回收器的清理函数，用于释放 Lua 虚拟机中的所有对象。
** 通常在虚拟机关闭时调用，确保所有分配的内存都被正确释放。
** 函数会设置特殊的白色标记，使所有对象都被视为垃圾并释放。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中 n 是所有对象的总数
**
** 注意事项：
** - 会释放根对象列表中的所有对象
** - 会释放字符串哈希表中的所有字符串
** - 调用后虚拟机状态不再可用
*/
void luaC_freeall(lua_State *L)
{
    global_State *g = G(L);
    int i;
    
    // 设置特殊的白色标记，使所有对象都被视为垃圾
    g->currentwhite = WHITEBITS | bitmask(SFIXEDBIT);
    
    // 清扫并释放根对象列表中的所有对象
    sweepwholelist(L, &g->rootgc);
    
    // 清扫并释放字符串哈希表中的所有字符串
    for (i = 0; i < g->strt.size; i++)
    {
        sweepwholelist(L, &g->strt.hash[i]);
    }
}

/*
** [工具] 标记所有基本类型的元表
**
** 详细功能说明：
** 遍历并标记所有基本 Lua 类型（如 number、string、table 等）的
** 默认元表。这些元表存储在全局状态的 mt 数组中，需要在垃圾回收
** 的标记阶段被标记以防止被误回收。
**
** 参数说明：
** @param g - global_State*：全局状态指针
**
** 返回值：无
**
** 算法复杂度：O(k) 时间，其中 k 是基本类型的数量（常数）
**
** 注意事项：
** - 只标记非空的元表
** - NUM_TAGS 定义了基本类型的数量
** - 这些元表是全局共享的
**
** 逐行详细注释（短函数详细说明）：
*/
static void markmt(global_State *g)
{
    // 循环变量声明：用于遍历所有基本类型索引
    int i;
    
    // 循环遍历：从 0 到 NUM_TAGS-1，覆盖所有 Lua 基本类型
    // NUM_TAGS 包括 nil、boolean、number、string、table、function、userdata、thread 等
    for (i = 0; i < NUM_TAGS; i++)
    {
        // 条件检查：判断当前类型的元表是否存在（非 NULL）
        // g->mt[i] 存储第 i 种类型的全局元表指针
        if (g->mt[i])
        {
            // 标记操作：将存在的元表标记为可达对象，防止被回收
            // markobject 宏会检查对象颜色并调用相应的标记函数
            markobject(g, g->mt[i]);
        }
    }
}

/*
** [核心] 标记所有根对象
**
** 详细功能说明：
** 这是垃圾回收标记阶段的起始函数，负责标记所有根对象。根对象
** 包括主线程、全局注册表、基本类型的元表等。这些对象是垃圾回收
** 的起点，从这些对象开始遍历可以找到所有可达的对象。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 会重置所有灰色对象列表
** - 标记完成后进入传播阶段
** - 这是三色标记算法的起始点
*/
static void markroot(lua_State *L)
{
    global_State *g = G(L);
    
    // 重置所有灰色对象列表
    g->gray = NULL;
    g->grayagain = NULL;
    g->weak = NULL;
    
    // 标记主线程对象
    markobject(g, g->mainthread);
    
    // 确保全局表在主栈之前被遍历
    markvalue(g, gt(g->mainthread));
    markvalue(g, registry(L));
    
    // 标记所有基本类型的元表
    markmt(g);
    
    // 进入标记传播阶段
    g->gcstate = GCSpropagate;
}

/*
** [核心] 重新标记所有上值对象
**
** 详细功能说明：
** 在垃圾回收的原子操作阶段，重新遍历并标记所有上值对象。这是
** 为了确保在并发环境下或增量回收过程中，上值对象及其引用的值
** 都能被正确标记，防止被误回收。
**
** 参数说明：
** @param g - global_State*：全局状态指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中 n 是上值对象的数量
**
** 注意事项：
** - 只处理灰色的上值对象
** - 使用双向链表遍历所有上值
** - 确保链表结构的完整性
*/
static void remarkupvals(global_State *g)
{
    UpVal *uv;
    
    // 遍历上值双向链表
    for (uv = g->uvhead.u.l.next; uv != &g->uvhead; uv = uv->u.l.next)
    {
        // 断言检查链表结构的完整性
        lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
        
        // 如果上值对象是灰色的，则标记其引用的值
        if (isgray(obj2gco(uv)))
        {
            markvalue(g, uv->v);
        }
    }
}

/*
** [核心] 垃圾回收的原子操作阶段
**
** 详细功能说明：
** 这是垃圾回收的关键阶段，执行不可中断的原子操作来完成标记过程。
** 主要包括重新标记可能在传播阶段被修改的对象、处理弱引用、分离
** 需要终结的对象、翻转白色标记等操作。这个阶段确保标记的完整性
** 和一致性。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中 n 是对象总数
**
** 注意事项：
** - 这个阶段不能被中断
** - 会翻转当前白色标记
** - 处理弱引用和终结化对象
*/
static void atomic(lua_State *L)
{
    global_State *g = G(L);
    // 要终结化的用户数据总大小
  size_t udsize;
    
    // 重新标记（可能）死线程的偶然上值
    remarkupvals(g);
    
    // 遍历被写屏障和 remarkupvals 捕获的对象
    propagateall(g);
    
    // 重新标记弱表
    g->gray = g->weak;
    g->weak = NULL;
    lua_assert(!iswhite(obj2gco(g->mainthread)));
    // 标记运行中的线程
  markobject(g, L);
  // 再次标记基本元表
  markmt(g);
    propagateall(g);
    
    // 再次标记灰色
    g->gray = g->grayagain;
    g->grayagain = NULL;
    propagateall(g);
    
    // 分离要终结化的用户数据
  udsize = luaC_separateudata(L, 0);
  // 标记"保留的"用户数据
  marktmu(g);
  // 重新标记，传播"保留性"
  udsize += propagateall(g);
    
    // 从弱表中移除已回收的对象
  cleartable(g->weak);
    
    // 翻转当前白色标记（白色对象变为垃圾）
    g->currentwhite = cast_byte(otherwhite(g));
    g->sweepstrgc = 0;
    g->sweepgc = &g->rootgc;
    g->gcstate = GCSsweepstring;
    // 第一次估计
  g->estimate = g->totalbytes - udsize;
}

/*
** [核心] 执行垃圾回收的单个步骤
**
** 详细功能说明：
** 根据当前垃圾回收状态执行相应的单个步骤操作。包括标记根对象、
** 传播标记、清扫字符串、清扫对象、终结化等阶段。这是增量垃圾
** 回收的核心函数，允许垃圾回收工作分散到多个时间片中执行。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return l_mem：本步骤处理的内存量或工作量
**
** 算法复杂度：取决于当前阶段，从 O(1) 到 O(n)
**
** 注意事项：
** - 状态机驱动，根据 gcstate 执行不同操作
** - 返回值用于控制垃圾回收的进度
** - 支持增量执行以减少暂停时间
*/
static l_mem singlestep(lua_State *L)
{
    global_State *g = G(L);
    
    switch (g->gcstate)
    {
        case GCSpause:
        {
            // 开始新的回收
    markroot(L);
            return 0;
        }
        
        case GCSpropagate:
        {
            if (g->gray)
            {
                return propagatemark(g);
            }
            else  // 没有更多灰色对象
            {
                // 完成标记阶段
    atomic(L);
                return 0;
            }
        }
        
        case GCSsweepstring:
        {
            lu_mem old = g->totalbytes;
            sweepwholelist(L, &g->strt.hash[g->sweepstrgc++]);
            
            // 检查是否还有字符串需要清扫
            if (g->sweepstrgc >= g->strt.size)
            {
                // 结束清扫字符串阶段
    g->gcstate = GCSsweep;
            }
            
            lua_assert(old >= g->totalbytes);
            g->estimate -= old - g->totalbytes;
            return GCSWEEPCOST;
        }
        
        case GCSsweep:
        {
            lu_mem old = g->totalbytes;
            g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX);
            
            // 检查是否还有对象需要清扫
            if (*g->sweepgc == NULL)
            {
                checkSizes(L);
                // 结束清扫阶段
    g->gcstate = GCSfinalize;
            }
            
            lua_assert(old >= g->totalbytes);
            g->estimate -= old - g->totalbytes;
            return GCSWEEPMAX * GCSWEEPCOST;
        }
        
        case GCSfinalize:
        {
            if (g->tmudata)
            {
                GCTM(L);
                
                if (g->estimate > GCFINALIZECOST)
                {
                    g->estimate -= GCFINALIZECOST;
                }
                
                return GCFINALIZECOST;
            }
            else
            {
                // 结束回收
    g->gcstate = GCSpause;
                g->gcdept = 0;
                return 0;
            }
        }
        
        default:
            lua_assert(0);
            return 0;
    }
}

/*
** [接口] 执行增量垃圾回收步骤
**
** 详细功能说明：
** 这是增量垃圾回收的主要接口函数，根据配置的步长和倍数执行
** 一定量的垃圾回收工作。函数会调用 singlestep 直到完成指定
** 的工作量或当前回收周期结束，然后调整垃圾回收阈值。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：无
**
** 算法复杂度：O(k) 时间，其中 k 是配置的步长大小
**
** 注意事项：
** - 根据 gcstepmul 参数调整工作量
** - 会更新垃圾回收阈值和债务
** - 在内存分配时自动调用
*/
void luaC_step(lua_State *L)
{
    global_State *g = G(L);
    l_mem lim = (GCSTEPSIZE / 100) * g->gcstepmul;
    
    if (lim == 0)
    {
        // 无限制
  lim = (MAX_LUMEM - 1) / 2;
    }
    
    g->gcdept += g->totalbytes - g->GCthreshold;
    
    do
    {
        lim -= singlestep(L);
        
        if (g->gcstate == GCSpause)
        {
            break;
        }
    } while (lim > 0);
    
    if (g->gcstate != GCSpause)
    {
        if (g->gcdept < GCSTEPSIZE)
        {
            g->GCthreshold = g->totalbytes + GCSTEPSIZE;
        }
        else
        {
            g->gcdept -= GCSTEPSIZE;
            g->GCthreshold = g->totalbytes;
        }
    }
    else
    {
        setthreshold(g);
    }
}

/*
** [接口] 执行完整的垃圾回收周期
**
** 详细功能说明：
** 强制执行一个完整的垃圾回收周期，不管当前的垃圾回收状态如何。
** 函数会重置垃圾回收状态，执行完整的标记-清扫过程，并处理所有
** 待终结的对象。这通常在需要立即回收内存时调用。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：无
**
** 算法复杂度：O(n) 时间，其中 n 是所有对象的总数
**
** 注意事项：
** - 会暂停程序执行直到回收完成
** - 重置所有垃圾回收状态和列表
** - 在 lua_gc 函数中被调用
*/
void luaC_fullgc(lua_State *L)
{
    global_State *g = G(L);
    
    if (g->gcstate <= GCSpropagate)
    {
        // 重置清扫标记以清扫所有元素（将它们返回为白色）
        g->sweepstrgc = 0;
        g->sweepgc = &g->rootgc;
        
        // 重置其他回收器列表
        g->gray = NULL;
        g->grayagain = NULL;
        g->weak = NULL;
        g->gcstate = GCSsweepstring;
    }
    
    lua_assert(g->gcstate != GCSpause && g->gcstate != GCSpropagate);
    
    // 完成任何待处理的清扫阶段
    while (g->gcstate != GCSfinalize)
    {
        lua_assert(g->gcstate == GCSsweepstring || g->gcstate == GCSsweep);
        singlestep(L);
    }
    
    markroot(L);
    
    while (g->gcstate != GCSpause)
    {
        singlestep(L);
    }
    
    setthreshold(g);
}

/*
** [核心] 前向写屏障处理
**
** 详细功能说明：
** 当黑色对象引用白色对象时调用的写屏障函数。为了维护三色标记
** 算法的不变性（黑色对象不能直接引用白色对象），函数会将白色
** 对象标记为灰色，或将黑色对象退化为白色。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param o - GCObject*：引用者对象（黑色）
** @param v - GCObject*：被引用对象（白色）
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 只在标记阶段需要维护不变性
** - 不适用于表对象（表有专门的后向屏障）
** - 确保垃圾回收的正确性
*/
void luaC_barrierf(lua_State *L, GCObject *o, GCObject *v)
{
    global_State *g = G(L);
    
    lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
    lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
    lua_assert(ttype(&o->gch) != LUA_TTABLE);
    
    // 必须保持不变性
    if (g->gcstate == GCSpropagate)
    {
        // 恢复不变性
    reallymarkobject(g, v);
    }
    else  // 不在意
    {
        // 标记为白色以避免其他屏障
  makewhite(g, o);
    }
}

/*
** [核心] 后向写屏障处理
**
** 详细功能说明：
** 专门用于表对象的写屏障函数。当表（黑色）被修改时调用，
** 将表重新标记为灰色并加入 grayagain 列表，确保在原子阶段
** 重新扫描该表。这避免了逐个检查表中每个新增引用的开销。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param t - Table*：被修改的表对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 专门针对表对象的优化策略
** - 表会在原子阶段重新扫描
** - 比前向屏障更高效的处理方式
*/
void luaC_barrierback(lua_State *L, Table *t)
{
    global_State *g = G(L);
    GCObject *o = obj2gco(t);
    
    lua_assert(isblack(o) && !isdead(g, o));
    lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
    
    // 使表再次变为灰色
  black2gray(o);
    t->gclist = g->grayagain;
    g->grayagain = o;
}

/*
** [接口] 将新对象链接到垃圾回收器
**
** 详细功能说明：
** 将新创建的对象添加到垃圾回收器的根对象列表中，设置其类型
** 和初始颜色标记。所有可回收对象在创建时都必须调用此函数
** 进行注册，以便垃圾回收器能够管理它们。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param o - GCObject*：要链接的新对象
** @param tt - lu_byte：对象的类型标识
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 新对象被标记为当前白色
** - 对象被添加到根列表的头部
** - 在对象创建函数中调用
*/
void luaC_link(lua_State *L, GCObject *o, lu_byte tt)
{
    // 获取全局状态：从 Lua 状态机中提取全局状态指针
    global_State *g = G(L);
    
    // 链表插入：将新对象插入到根对象链表的头部
    // 采用头插法，新对象的 next 指向当前的根对象
    o->gch.next = g->rootgc;
    
    // 更新根指针：将根对象指针更新为新对象
    g->rootgc = o;
    
    // 颜色标记：将新对象标记为当前的白色（新对象默认为白色）
    // luaC_white(g) 返回当前的白色标记位
    o->gch.marked = luaC_white(g);
    
    // 类型设置：设置对象的类型标识符
    // tt 参数指定了对象的具体类型（表、函数、用户数据等）
    o->gch.tt = tt;
}

/*
** [接口] 将上值对象链接到垃圾回收器
**
** 详细功能说明：
** 专门用于上值对象的链接函数。除了基本的链接操作外，还会
** 根据当前垃圾回收状态和上值的颜色进行特殊处理，确保上值
** 对象在垃圾回收过程中的正确性。
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
** @param uv - UpVal*：要链接的上值对象
**
** 返回值：无
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 注意事项：
** - 处理灰色上值的特殊情况
** - 在传播阶段可能需要写屏障
** - 在清扫阶段会重置为白色
*/
void luaC_linkupval(lua_State *L, UpVal *uv)
{
    // 获取全局状态：从 Lua 状态机中提取全局状态指针
    global_State *g = G(L);
    
    // 类型转换：将上值对象转换为通用的垃圾回收对象指针
    GCObject *o = obj2gco(uv);
    
    // 链表插入：将上值对象插入到根对象链表的头部
    o->gch.next = g->rootgc;
    
    // 更新根指针：将根对象指针更新为当前上值对象
    g->rootgc = o;
    
    // 颜色检查：如果上值对象当前是灰色，需要特殊处理
    if (isgray(o))
    {
        // 状态判断：根据当前垃圾回收状态决定处理方式
        if (g->gcstate == GCSpropagate)
        {
            // 传播阶段：将灰色对象转为黑色，并设置写屏障
            gray2black(o);
            
            // 写屏障：确保上值引用的值也被正确标记
            luaC_barrier(L, uv, uv->v);
        }
        else
        {
            // 清扫阶段：将对象重置为白色
            // 颜色重置：在清扫阶段将灰色对象转为白色
            makewhite(g, o);
        }
    }
}

