/*
** Lua 垃圾回收器实现
** 负责管理 Lua 虚拟机的内存分配和回收
** 实现了增量式三色标记清除垃圾回收算法
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
#define GCSTEPSIZE      1024u           // 每次垃圾回收步骤的大小
#define GCSWEEPMAX      40              // 每次清扫的最大对象数
#define GCSWEEPCOST     10              // 清扫操作的成本
#define GCFINALIZECOST  100             // 终结化操作的成本

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
** 从节点中移除条目
** 当节点的值为 nil 时，如果键是可回收的，则将其标记为死键
*/
static void removeentry(Node *n)
{
    lua_assert(ttisnil(gval(n)));
    
    if (iscollectable(gkey(n)))
    {
        setttype(gkey(n), LUA_TDEADKEY);  // 标记为死键并移除
    }
}

/*
** 真正标记对象的函数
** 根据对象类型进行不同的标记处理
*/
static void reallymarkobject(global_State *g, GCObject *o)
{
    lua_assert(iswhite(o) && !isdead(g, o));
    white2gray(o);  // 将白色对象转为灰色
    
    switch (o->gch.tt)
    {
        case LUA_TSTRING:
        {
            // 字符串不需要进一步遍历
            return;
        }
        
        case LUA_TUSERDATA:
        {
            Table *mt = gco2u(o)->metatable;
            gray2black(o);  // 用户数据永远不是灰色
            
            if (mt)
            {
                markobject(g, mt);  // 标记元表
            }
            
            markobject(g, gco2u(o)->env);  // 标记环境
            return;
        }
        
        case LUA_TUPVAL:
        {
            UpVal *uv = gco2uv(o);
            markvalue(g, uv->v);  // 标记上值的值
            
            if (uv->v == &uv->u.value)  // 是否已关闭？
            {
                gray2black(o);  // 开放的上值永远不是黑色
            }
            return;
        }
        
        case LUA_TFUNCTION:
        {
            // 将函数加入灰色列表
            gco2cl(o)->c.gclist = g->gray;
            g->gray = o;
            break;
        }
        
        case LUA_TTABLE:
        {
            // 将表加入灰色列表
            gco2h(o)->gclist = g->gray;
            g->gray = o;
            break;
        }
        
        case LUA_TTHREAD:
        {
            // 将线程加入灰色列表
            gco2th(o)->gclist = g->gray;
            g->gray = o;
            break;
        }
        
        case LUA_TPROTO:
        {
            // 将原型加入灰色列表
            gco2p(o)->gclist = g->gray;
            g->gray = o;
            break;
        }
        
        default:
            lua_assert(0);
    }
}

/*
** 标记需要终结化的用户数据
** 遍历 tmudata 列表中的所有用户数据
*/
static void marktmu(global_State *g)
{
    GCObject *u = g->tmudata;
    
    if (u)
    {
        do
        {
            u = u->gch.next;
            makewhite(g, u);  // 可能已被标记，如果是上次 GC 遗留的
            reallymarkobject(g, u);  // 重新标记
        } while (u != g->tmudata);
    }
}

/*
** 分离需要终结化的用户数据
** 将需要终结化的"死"用户数据移动到 tmudata 列表
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
            p = &curr->gch.next;  // 不处理这些对象
        }
        else if (fasttm(L, gco2u(curr)->metatable, TM_GC) == NULL)
        {
            markfinalized(gco2u(curr));  // 不需要终结化
            p = &curr->gch.next;
        }
        else
        {
            // 必须调用其 gc 方法
            deadmem += sizeudata(gco2u(curr));
            markfinalized(gco2u(curr));
            *p = curr->gch.next;
            
            // 将 curr 链接到 tmudata 列表的末尾
            if (g->tmudata == NULL)  // 列表为空？
            {
                g->tmudata = curr->gch.next = curr;  // 创建循环列表
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
** 遍历表对象
** 检查表的弱引用模式并标记相应的键值对
*/
static int traversetable(global_State *g, Table *h)
{
    int i;
    int weakkey = 0;
    int weakvalue = 0;
    const TValue *mode;
    
    if (h->metatable)
    {
        markobject(g, h->metatable);  // 标记元表
    }
    
    mode = gfasttm(g, h->metatable, TM_MODE);
    
    if (mode && ttisstring(mode))  // 是否有弱引用模式？
    {
        weakkey = (strchr(svalue(mode), 'k') != NULL);
        weakvalue = (strchr(svalue(mode), 'v') != NULL);
        
        if (weakkey || weakvalue)  // 真的是弱引用？
        {
            h->marked &= ~(KEYWEAK | VALUEWEAK);  // 清除位
            h->marked |= cast_byte((weakkey << KEYWEAKBIT) |
                                 (weakvalue << VALUEWEAKBIT));
            h->gclist = g->weak;  // 必须在 GC 后清除
            g->weak = obj2gco(h);  // 放入适当的列表
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
            markvalue(g, &h->array[i]);  // 标记数组部分
        }
    }
    
    i = sizenode(h);
    while (i--)
    {
        Node *n = gnode(h, i);
        lua_assert(ttype(gkey(n)) != LUA_TDEADKEY || ttisnil(gval(n)));
        
        if (ttisnil(gval(n)))
        {
            removeentry(n);  // 移除空条目
        }
        else
        {
            lua_assert(!ttisnil(gkey(n)));
            
            if (!weakkey)
            {
                markvalue(g, gkey(n));  // 标记键
            }
            
            if (!weakvalue)
            {
                markvalue(g, gval(n));  // 标记值
            }
        }
    }
    
    return weakkey || weakvalue;
}

/*
** 遍历函数原型
** 所有标记都是条件性的，因为在原型创建过程中可能发生 GC
*/
static void traverseproto(global_State *g, Proto *f)
{
    int i;
    
    if (f->source)
    {
        stringmark(f->source);  // 标记源代码字符串
    }
    
    for (i = 0; i < f->sizek; i++)  // 标记字面量
    {
        markvalue(g, &f->k[i]);
    }
    
    for (i = 0; i < f->sizeupvalues; i++)  // 标记上值名称
    {
        if (f->upvalues[i])
        {
            stringmark(f->upvalues[i]);
        }
    }
    
    for (i = 0; i < f->sizep; i++)  // 标记嵌套原型
    {
        if (f->p[i])
        {
            markobject(g, f->p[i]);
        }
    }
    
    for (i = 0; i < f->sizelocvars; i++)  // 标记局部变量名
    {
        if (f->locvars[i].varname)
        {
            stringmark(f->locvars[i].varname);
        }
    }
}

/*
** 遍历闭包对象
** 标记闭包的环境和上值
*/
static void traverseclosure(global_State *g, Closure *cl)
{
    markobject(g, cl->c.env);  // 标记环境
    
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
        markobject(g, cl->l.p);  // 标记原型
        
        for (i = 0; i < cl->l.nupvalues; i++)  // 标记 Lua 闭包的上值
        {
            markobject(g, cl->l.upvals[i]);
        }
    }
}

/*
** 检查栈大小
** 如果栈使用率较低，则缩小栈大小
*/
static void checkstacksizes(lua_State *L, StkId max)
{
    int ci_used = cast_int(L->ci - L->base_ci);  // 使用中的 ci 数量
    int s_used = cast_int(max - L->stack);  // 使用中的栈部分
    
    if (L->size_ci > LUAI_MAXCALLS)  // 处理溢出？
    {
        return;  // 不触碰栈
    }
    
    if (4 * ci_used < L->size_ci && 2 * BASIC_CI_SIZE < L->size_ci)
    {
        luaD_reallocCI(L, L->size_ci / 2);  // 仍然足够大
    }
    
    condhardstacktests(luaD_reallocCI(L, ci_used + 1));
    
    if (4 * s_used < L->stacksize &&
        2 * (BASIC_STACK_SIZE + EXTRA_STACK) < L->stacksize)
    {
        luaD_reallocstack(L, L->stacksize / 2);  // 仍然足够大
    }
    
    condhardstacktests(luaD_reallocstack(L, s_used));
}

/*
** 遍历栈
** 标记栈中的所有值和调用信息
*/
static void traversestack(global_State *g, lua_State *l)
{
    StkId o, lim;
    CallInfo *ci;
    
    markvalue(g, gt(l));  // 标记全局表
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
        markvalue(g, o);  // 标记栈中的值
    }
    
    for (; o <= lim; o++)
    {
        setnilvalue(o);  // 清空未使用的栈位置
    }
    
    checkstacksizes(l, lim);  // 检查并调整栈大小
}

/*
** 传播标记
** 遍历一个灰色对象，将其转为黑色
** 返回遍历的数量
*/
static l_mem propagatemark(global_State *g)
{
    GCObject *o = g->gray;
    lua_assert(isgray(o));
    gray2black(o);  // 将灰色转为黑色
    
    switch (o->gch.tt)
    {
        case LUA_TTABLE:
        {
            Table *h = gco2h(o);
            g->gray = h->gclist;
            
            if (traversetable(g, h))  // 表是弱引用？
            {
                black2gray(o);  // 保持灰色
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
            lua_assert(0);
            return 0;
    }
}

/*
** 传播所有标记
** 处理所有灰色对象直到没有更多灰色对象
*/
static size_t propagateall(global_State *g)
{
    size_t m = 0;
    
    while (g->gray)
    {
        m += propagatemark(g);
    }
    
    return m;
}

/*
** 检查键或值是否可以从弱表中清除
** 不可回收的对象永远不会从弱表中移除
** 字符串表现为"值"，所以也永远不会被移除
** 对于其他对象：如果真的被回收了，不能保留它们
** 对于正在终结化的用户数据，在键中保留，但不在值中保留
*/
static int iscleared(const TValue *o, int iskey)
{
    if (!iscollectable(o))
    {
        return 0;
    }
    
    if (ttisstring(o))
    {
        stringmark(rawtsvalue(o));  // 字符串是"值"，所以永远不弱
        return 0;
    }
    
    return iswhite(gcvalue(o)) ||
           (ttisuserdata(o) && (!iskey && isfinalized(uvalue(o))));
}

/*
** 清除弱表中的已回收条目
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
                if (iscleared(o, 0))  // 值被回收了？
                {
                    setnilvalue(o);  // 移除值
                }
            }
        }
        
        i = sizenode(h);
        while (i--)
        {
            Node *n = gnode(h, i);
            if (!ttisnil(gval(n)) &&  // 非空条目？
                (iscleared(key2tval(n), 1) || iscleared(gval(n), 0)))
            {
                setnilvalue(gval(n));  // 移除值
                removeentry(n);  // 从表中移除条目
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
** 清扫列表
** 遍历对象列表，释放死对象，保留活对象
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
        
        if ((curr->gch.marked ^ WHITEBITS) & deadmask)  // 不是死对象？
        {
            lua_assert(!isdead(g, curr) || testbit(curr->gch.marked, FIXEDBIT));
            makewhite(g, curr);  // 标记为白色（为下次循环准备）
            p = &curr->gch.next;
        }
        else  // 必须删除 curr
        {
            lua_assert(isdead(g, curr) || deadmask == bitmask(SFIXEDBIT));
            *p = curr->gch.next;
            
            if (curr == g->rootgc)  // 是列表的第一个元素？
            {
                g->rootgc = curr->gch.next;  // 调整第一个
            }
            
            freeobj(L, curr);
        }
    }
    
    return p;
}

/*
** 检查大小
** 检查字符串哈希表和缓冲区的大小，必要时调整
*/
static void checkSizes(lua_State *L)
{
    global_State *g = G(L);
    
    // 检查字符串哈希表大小
    if (g->strt.nuse < cast(lu_int32, g->strt.size / 4) &&
        g->strt.size > MINSTRTABSIZE * 2)
    {
        luaS_resize(L, g->strt.size / 2);  // 表太大了
    }
    
    // 检查缓冲区大小
    if (luaZ_sizebuffer(&g->buff) > LUA_MINBUFFER * 2)  // 缓冲区太大？
    {
        size_t newsize = luaZ_sizebuffer(&g->buff) / 2;
        luaZ_resizebuffer(L, &g->buff, newsize);
    }
}

/*
** 垃圾回收标记方法
** 执行单个用户数据的终结化
*/
static void GCTM(lua_State *L)
{
    global_State *g = G(L);
    GCObject *o = g->tmudata->gch.next;  // 获取第一个元素
    Udata *udata = rawgco2u(o);
    const TValue *tm;
    
    // 从 tmudata 中移除用户数据
    if (o == g->tmudata)  // 最后一个元素？
    {
        g->tmudata = NULL;
    }
    else
    {
        g->tmudata->gch.next = udata->uv.next;
    }
    
    udata->uv.next = g->mainthread->next;  // 返回到根列表
    g->mainthread->next = o;
    makewhite(g, o);
    
    tm = fasttm(L, udata->uv.metatable, TM_GC);
    
    if (tm != NULL)
    {
        lu_byte oldah = L->allowhook;
        lu_mem oldt = g->GCthreshold;
        
        L->allowhook = 0;  // 在 GC 标记方法期间停止调试钩子
        g->GCthreshold = 2 * g->totalbytes;  // 避免 GC 步骤
        
        setobj2s(L, L->top, tm);
        setuvalue(L, L->top + 1, udata);
        L->top += 2;
        luaD_call(L, L->top - 2, 0);
        
        L->allowhook = oldah;  // 恢复钩子
        g->GCthreshold = oldt;  // 恢复阈值
    }
}

/*
** 调用所有 GC 标记方法
*/
void luaC_callGCTM(lua_State *L)
{
    while (G(L)->tmudata)
    {
        GCTM(L);
    }
}

/*
** 释放所有对象
*/
void luaC_freeall(lua_State *L)
{
    global_State *g = G(L);
    int i;
    
    g->currentwhite = WHITEBITS | bitmask(SFIXEDBIT);  // 收集所有元素的掩码
    sweepwholelist(L, &g->rootgc);
    
    for (i = 0; i < g->strt.size; i++)  // 释放所有字符串列表
    {
        sweepwholelist(L, &g->strt.hash[i]);
    }
}

/*
** 标记元表
** 标记所有基本类型的元表
*/
static void markmt(global_State *g)
{
    int i;
    
    for (i = 0; i < NUM_TAGS; i++)
    {
        if (g->mt[i])
        {
            markobject(g, g->mt[i]);
        }
    }
}

/*
** 标记根集合
** 标记所有根对象，开始新的垃圾回收周期
*/
static void markroot(lua_State *L)
{
    global_State *g = G(L);
    
    g->gray = NULL;
    g->grayagain = NULL;
    g->weak = NULL;
    
    markobject(g, g->mainthread);
    
    // 确保全局表在主栈之前被遍历
    markvalue(g, gt(g->mainthread));
    markvalue(g, registry(L));
    markmt(g);
    
    g->gcstate = GCSpropagate;
}

/*
** 重新标记上值
** 标记可能死线程的偶然上值
*/
static void remarkupvals(global_State *g)
{
    UpVal *uv;
    
    for (uv = g->uvhead.u.l.next; uv != &g->uvhead; uv = uv->u.l.next)
    {
        lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
        
        if (isgray(obj2gco(uv)))
        {
            markvalue(g, uv->v);
        }
    }
}

/*
** 原子操作
** 完成标记阶段的原子操作
*/
static void atomic(lua_State *L)
{
    global_State *g = G(L);
    size_t udsize;  // 要终结化的用户数据总大小
    
    // 重新标记（可能）死线程的偶然上值
    remarkupvals(g);
    
    // 遍历被写屏障和 remarkupvals 捕获的对象
    propagateall(g);
    
    // 重新标记弱表
    g->gray = g->weak;
    g->weak = NULL;
    lua_assert(!iswhite(obj2gco(g->mainthread)));
    markobject(g, L);  // 标记运行中的线程
    markmt(g);  // 再次标记基本元表
    propagateall(g);
    
    // 再次标记灰色
    g->gray = g->grayagain;
    g->grayagain = NULL;
    propagateall(g);
    
    udsize = luaC_separateudata(L, 0);  // 分离要终结化的用户数据
    marktmu(g);  // 标记"保留的"用户数据
    udsize += propagateall(g);  // 重新标记，传播"保留性"
    
    cleartable(g->weak);  // 从弱表中移除已回收的对象
    
    // 翻转当前白色
    g->currentwhite = cast_byte(otherwhite(g));
    g->sweepstrgc = 0;
    g->sweepgc = &g->rootgc;
    g->gcstate = GCSsweepstring;
    g->estimate = g->totalbytes - udsize;  // 第一次估计
}

/*
** 单步执行
** 执行垃圾回收的一个步骤
*/
static l_mem singlestep(lua_State *L)
{
    global_State *g = G(L);
    
    switch (g->gcstate)
    {
        case GCSpause:
        {
            markroot(L);  // 开始新的回收
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
                atomic(L);  // 完成标记阶段
                return 0;
            }
        }
        
        case GCSsweepstring:
        {
            lu_mem old = g->totalbytes;
            sweepwholelist(L, &g->strt.hash[g->sweepstrgc++]);
            
            if (g->sweepstrgc >= g->strt.size)  // 没有更多要清扫的？
            {
                g->gcstate = GCSsweep;  // 结束清扫字符串阶段
            }
            
            lua_assert(old >= g->totalbytes);
            g->estimate -= old - g->totalbytes;
            return GCSWEEPCOST;
        }
        
        case GCSsweep:
        {
            lu_mem old = g->totalbytes;
            g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX);
            
            if (*g->sweepgc == NULL)  // 没有更多要清扫的？
            {
                checkSizes(L);
                g->gcstate = GCSfinalize;  // 结束清扫阶段
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
                g->gcstate = GCSpause;  // 结束回收
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
** 垃圾回收步骤
** 执行增量垃圾回收的一个步骤
*/
void luaC_step(lua_State *L)
{
    global_State *g = G(L);
    l_mem lim = (GCSTEPSIZE / 100) * g->gcstepmul;
    
    if (lim == 0)
    {
        lim = (MAX_LUMEM - 1) / 2;  // 无限制
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
** 完整垃圾回收
** 执行完整的垃圾回收周期
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
** 前向屏障
** 处理对象间的引用，维护垃圾回收的不变性
*/
void luaC_barrierf(lua_State *L, GCObject *o, GCObject *v)
{
    global_State *g = G(L);
    
    lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
    lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
    lua_assert(ttype(&o->gch) != LUA_TTABLE);
    
    // 必须保持不变性？
    if (g->gcstate == GCSpropagate)
    {
        reallymarkobject(g, v);  // 恢复不变性
    }
    else  // 不在意
    {
        makewhite(g, o);  // 标记为白色以避免其他屏障
    }
}

/*
** 后向屏障
** 处理表的后向引用
*/
void luaC_barrierback(lua_State *L, Table *t)
{
    global_State *g = G(L);
    GCObject *o = obj2gco(t);
    
    lua_assert(isblack(o) && !isdead(g, o));
    lua_assert(g->gcstate != GCSfinalize && g->gcstate != GCSpause);
    
    black2gray(o);  // 使表再次变为灰色
    t->gclist = g->grayagain;
    g->grayagain = o;
}

/*
** 链接对象
** 将新对象链接到垃圾回收器的根列表
*/
void luaC_link(lua_State *L, GCObject *o, lu_byte tt)
{
    global_State *g = G(L);
    
    o->gch.next = g->rootgc;
    g->rootgc = o;
    o->gch.marked = luaC_white(g);
    o->gch.tt = tt;
}

/*
** 链接上值
** 将上值链接到垃圾回收器的根列表
*/
void luaC_linkupval(lua_State *L, UpVal *uv)
{
    global_State *g = G(L);
    GCObject *o = obj2gco(uv);
    
    o->gch.next = g->rootgc;  // 将上值链接到 rootgc 列表
    g->rootgc = o;
    
    if (isgray(o))
    {
        if (g->gcstate == GCSpropagate)
        {
            gray2black(o);  // 关闭的上值需要屏障
            luaC_barrier(L, uv, uv->v);
        }
        else  // 清扫阶段：清扫它（转为白色）
        {
            makewhite(g, o);
        }
    }
}

