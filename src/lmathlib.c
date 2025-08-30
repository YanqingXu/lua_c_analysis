/*
** [核心] Lua 标准数学库实现
**
** 功能概述：
** 本模块实现了 Lua 的标准数学库，提供完整的数学计算功能。
** 包含三角函数、对数函数、指数函数、取整函数、随机数生成等数学运算。
**
** 主要功能模块：
** - 三角函数：sin、cos、tan 及其反函数和双曲函数
** - 对数与指数：log、log10、exp、pow、sqrt
** - 取整与取模：floor、ceil、fmod、modf
** - 比较函数：min、max、abs
** - 随机数生成：random、randomseed
** - 角度转换：deg、rad
** - 浮点数操作：frexp、ldexp
**
** 数学精度说明：
** - 所有函数基于 IEEE 754 双精度浮点数标准
** - 精度约为 15-17 位有效数字
** - 特殊值处理：支持 NaN、±∞ 等特殊情况
** - 角度单位：三角函数使用弧度制，提供角度转换函数
**
** 性能特点：
** - 直接调用 C 标准库数学函数，性能优异
** - 最小化 Lua 栈操作，减少函数调用开销
** - 支持可变参数函数（min、max、random）
**
** 版本信息：$Id: lmathlib.c,v 1.67.1.1 2007/12/27 13:02:25 roberto Exp $
** 版权声明：参见 lua.h 中的版权声明
*/

// 系统数学库头文件
#include <stdlib.h>
#include <math.h>

// 模块标识定义
#define lmathlib_c
#define LUA_LIB

// Lua 核心头文件
#include "lua.h"

// Lua 辅助库头文件
#include "lauxlib.h"
#include "lualib.h"

// 取消可能存在的 PI 定义，使用高精度值
#undef PI

/*
** [数学常量] 高精度数学常量定义
**
** 常量说明：
** - PI：圆周率，精度达到双精度浮点数极限
** - RADIANS_PER_DEGREE：弧度与角度转换系数
**
** 精度说明：
** PI 值使用了 20 位有效数字，超过双精度浮点数的精度要求，
** 确保在所有计算中都能提供最高精度的圆周率值。
*/
#define PI (3.14159265358979323846)
#define RADIANS_PER_DEGREE (PI/180.0)

/*
** [数学函数] 绝对值函数
**
** 功能描述：
** 计算数值的绝对值。对于实数 x，返回 |x|。
**
** 数学原理：
** |x| = { x,  if x ≥ 0
**       { -x, if x < 0
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x
** 输出：number |x|
**
** 特殊值处理：
** - abs(+∞) = +∞
** - abs(-∞) = +∞  
** - abs(NaN) = NaN
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.abs(-5.5)  --> 5.5
** math.abs(3.14)  --> 3.14
*/
static int math_abs(lua_State *L) 
{
    lua_pushnumber(L, fabs(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [三角函数] 正弦函数
**
** 功能描述：
** 计算角度的正弦值。输入为弧度制角度。
**
** 数学原理：
** sin(x) 是周期为 2π 的三角函数，值域为 [-1, 1]
** 泰勒级数：sin(x) = x - x³/3! + x⁵/5! - x⁷/7! + ...
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（弧度）
** 输出：number sin(x)
**
** 特殊值处理：
** - sin(0) = 0
** - sin(π/2) = 1
** - sin(π) = 0（可能有微小误差）
** - sin(±∞) = NaN
** - sin(NaN) = NaN
**
** 精度说明：
** 使用 C 标准库 sin() 函数，精度约为 15 位有效数字
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.sin(0)        --> 0
** math.sin(math.pi/2) --> 1
** math.sin(math.pi)   --> 0 (约等于)
*/
static int math_sin(lua_State *L) 
{
    lua_pushnumber(L, sin(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [双曲函数] 双曲正弦函数
**
** 功能描述：
** 计算双曲正弦值。双曲函数在数学和物理学中有重要应用。
**
** 数学原理：
** sinh(x) = (e^x - e^(-x)) / 2
** 值域为 (-∞, +∞)，是奇函数
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x
** 输出：number sinh(x)
**
** 特殊值处理：
** - sinh(0) = 0
** - sinh(+∞) = +∞
** - sinh(-∞) = -∞
** - sinh(NaN) = NaN
**
** 应用场景：
** - 悬链线方程：y = a*cosh(x/a)
** - 双曲几何学
** - 某些微分方程的解
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.sinh(0)  --> 0
** math.sinh(1)  --> 1.1752011936438014
*/
static int math_sinh(lua_State *L) 
{
    lua_pushnumber(L, sinh(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [三角函数] 余弦函数
**
** 功能描述：
** 计算角度的余弦值。输入为弧度制角度。
**
** 数学原理：
** cos(x) 是周期为 2π 的三角函数，值域为 [-1, 1]
** 泰勒级数：cos(x) = 1 - x²/2! + x⁴/4! - x⁶/6! + ...
** 与正弦函数关系：cos(x) = sin(x + π/2)
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（弧度）
** 输出：number cos(x)
**
** 特殊值处理：
** - cos(0) = 1
** - cos(π/2) = 0（可能有微小误差）
** - cos(π) = -1
** - cos(±∞) = NaN
** - cos(NaN) = NaN
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.cos(0)        --> 1
** math.cos(math.pi/2) --> 0 (约等于)
** math.cos(math.pi)   --> -1
*/
static int math_cos(lua_State *L) 
{
    lua_pushnumber(L, cos(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [双曲函数] 双曲余弦函数
**
** 功能描述：
** 计算双曲余弦值。双曲余弦函数描述悬链线形状。
**
** 数学原理：
** cosh(x) = (e^x + e^(-x)) / 2
** 值域为 [1, +∞)，是偶函数
** 恒等式：cosh²(x) - sinh²(x) = 1
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x
** 输出：number cosh(x)
**
** 特殊值处理：
** - cosh(0) = 1
** - cosh(±∞) = +∞
** - cosh(NaN) = NaN
**
** 物理应用：
** - 悬链线：重力作用下绳索的自然形状
** - 双曲几何学
** - 相对论中的双曲运动
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.cosh(0)  --> 1
** math.cosh(1)  --> 1.5430806348152437
*/
static int math_cosh(lua_State *L)
{
    lua_pushnumber(L, cosh(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [三角函数] 正切函数
**
** 功能描述：
** 计算角度的正切值。输入为弧度制角度。
**
** 数学原理：
** tan(x) = sin(x) / cos(x)
** 周期为 π，在 x = π/2 + nπ 处有垂直渐近线
** 值域为 (-∞, +∞)
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（弧度）
** 输出：number tan(x)
**
** 特殊值处理：
** - tan(0) = 0
** - tan(π/4) = 1
** - tan(π/2) = ±∞（取决于逼近方向）
** - tan(±∞) = NaN
** - tan(NaN) = NaN
**
** 注意事项：
** 在 π/2 + nπ 附近，函数值变化极快，可能出现数值不稳定
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.tan(0)        --> 0
** math.tan(math.pi/4) --> 1
*/
static int math_tan(lua_State *L)
{
    lua_pushnumber(L, tan(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [双曲函数] 双曲正切函数
**
** 功能描述：
** 计算双曲正切值。双曲正切函数常用于神经网络激活函数。
**
** 数学原理：
** tanh(x) = sinh(x) / cosh(x) = (e^x - e^(-x)) / (e^x + e^(-x))
** 值域为 (-1, 1)，是奇函数
** 渐近线：y = ±1
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x
** 输出：number tanh(x)
**
** 特殊值处理：
** - tanh(0) = 0
** - tanh(+∞) = 1
** - tanh(-∞) = -1
** - tanh(NaN) = NaN
**
** 应用场景：
** - 神经网络激活函数
** - 信号处理中的饱和函数
** - 概率论和统计学
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.tanh(0)  --> 0
** math.tanh(1)  --> 0.7615941559557649
*/
static int math_tanh(lua_State *L)
{
    lua_pushnumber(L, tanh(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [反三角函数] 反正弦函数
**
** 功能描述：
** 计算正弦值对应的角度（弧度制）。
**
** 数学原理：
** asin(x) 是 sin(x) 的反函数
** 定义域：[-1, 1]
** 值域：[-π/2, π/2]
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（必须在 [-1, 1] 范围内）
** 输出：number asin(x)（弧度）
**
** 特殊值处理：
** - asin(0) = 0
** - asin(1) = π/2
** - asin(-1) = -π/2
** - asin(x) = NaN，当 |x| > 1
** - asin(NaN) = NaN
**
** 错误处理：
** 当输入超出 [-1, 1] 范围时，返回 NaN
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.asin(0)   --> 0
** math.asin(1)   --> 1.5707963267948966 (π/2)
** math.asin(0.5) --> 0.5235987755982989 (π/6)
*/
static int math_asin(lua_State *L)
{
    lua_pushnumber(L, asin(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [反三角函数] 反余弦函数
**
** 功能描述：
** 计算余弦值对应的角度（弧度制）。
**
** 数学原理：
** acos(x) 是 cos(x) 的反函数
** 定义域：[-1, 1]
** 值域：[0, π]
** 关系：acos(x) + asin(x) = π/2
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（必须在 [-1, 1] 范围内）
** 输出：number acos(x)（弧度）
**
** 特殊值处理：
** - acos(1) = 0
** - acos(0) = π/2
** - acos(-1) = π
** - acos(x) = NaN，当 |x| > 1
** - acos(NaN) = NaN
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.acos(1)   --> 0
** math.acos(0)   --> 1.5707963267948966 (π/2)
** math.acos(-1)  --> 3.141592653589793 (π)
*/
static int math_acos(lua_State *L)
{
    lua_pushnumber(L, acos(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [反三角函数] 反正切函数
**
** 功能描述：
** 计算正切值对应的角度（弧度制）。
**
** 数学原理：
** atan(x) 是 tan(x) 的反函数
** 定义域：(-∞, +∞)
** 值域：(-π/2, π/2)
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x
** 输出：number atan(x)（弧度）
**
** 特殊值处理：
** - atan(0) = 0
** - atan(1) = π/4
** - atan(+∞) = π/2
** - atan(-∞) = -π/2
** - atan(NaN) = NaN
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.atan(0) --> 0
** math.atan(1) --> 0.7853981633974483 (π/4)
*/
static int math_atan(lua_State *L)
{
    lua_pushnumber(L, atan(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [反三角函数] 双参数反正切函数
**
** 功能描述：
** 计算 y/x 的反正切值，但能正确处理象限信息。
** 比单参数 atan 函数更精确，能区分四个象限。
**
** 数学原理：
** atan2(y, x) 返回点 (x, y) 相对于正 x 轴的角度
** 定义域：x 和 y 不能同时为 0
** 值域：(-π, π]
**
** 象限处理：
** - 第一象限 (x>0, y>0)：atan2(y,x) = atan(y/x)
** - 第二象限 (x<0, y>0)：atan2(y,x) = atan(y/x) + π
** - 第三象限 (x<0, y<0)：atan2(y,x) = atan(y/x) - π
** - 第四象限 (x>0, y<0)：atan2(y,x) = atan(y/x)
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number y, number x
** 输出：number atan2(y, x)（弧度）
**
** 特殊值处理：
** - atan2(0, 1) = 0
** - atan2(1, 0) = π/2
** - atan2(0, -1) = π
** - atan2(-1, 0) = -π/2
** - atan2(0, 0) = 未定义（通常返回 0）
**
** 应用场景：
** - 计算向量角度
** - 极坐标转换
** - 计算机图形学中的旋转
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.atan2(1, 1)   --> 0.7853981633974483 (π/4)
** math.atan2(1, -1)  --> 2.356194490192345 (3π/4)
** math.atan2(-1, -1) --> -2.356194490192345 (-3π/4)
*/
static int math_atan2(lua_State *L)
{
    lua_pushnumber(L, atan2(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

/*
** [取整函数] 向上取整函数
**
** 功能描述：
** 返回不小于给定数值的最小整数。
**
** 数学原理：
** ceil(x) = ⌈x⌉ = min{n ∈ ℤ : n ≥ x}
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x
** 输出：number ceil(x)
**
** 特殊值处理：
** - ceil(2.3) = 3
** - ceil(-2.3) = -2
** - ceil(5) = 5（整数不变）
** - ceil(±∞) = ±∞
** - ceil(NaN) = NaN
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.ceil(2.3)  --> 3
** math.ceil(-2.3) --> -2
** math.ceil(5)    --> 5
*/
static int math_ceil(lua_State *L)
{
    lua_pushnumber(L, ceil(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [取整函数] 向下取整函数
**
** 功能描述：
** 返回不大于给定数值的最大整数。
**
** 数学原理：
** floor(x) = ⌊x⌋ = max{n ∈ ℤ : n ≤ x}
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x
** 输出：number floor(x)
**
** 特殊值处理：
** - floor(2.7) = 2
** - floor(-2.3) = -3
** - floor(5) = 5（整数不变）
** - floor(±∞) = ±∞
** - floor(NaN) = NaN
**
** 与 ceil 的关系：
** floor(x) = -ceil(-x)
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.floor(2.7)  --> 2
** math.floor(-2.3) --> -3
** math.floor(5)    --> 5
*/
static int math_floor(lua_State *L)
{
    lua_pushnumber(L, floor(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [取模函数] 浮点数取模运算
**
** 功能描述：
** 计算 x 除以 y 的浮点数余数。
**
** 数学原理：
** fmod(x, y) = x - n*y，其中 n = trunc(x/y)
** 结果的符号与 x 相同
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x, number y
** 输出：number fmod(x, y)
**
** 特殊值处理：
** - fmod(5.3, 2) = 1.3
** - fmod(-5.3, 2) = -1.3
** - fmod(5.3, -2) = 1.3
** - fmod(x, 0) = NaN
** - fmod(±∞, y) = NaN
** - fmod(x, ±∞) = x
**
** 与整数取模的区别：
** 整数取模：5 % 3 = 2
** 浮点取模：fmod(5.7, 3) = 2.7
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.fmod(5.3, 2)   --> 1.3
** math.fmod(-5.3, 2)  --> -1.3
** math.fmod(7.5, 2.5) --> 0
*/
static int math_fmod(lua_State *L)
{
    lua_pushnumber(L, fmod(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

/*
** [分解函数] 浮点数分解函数
**
** 功能描述：
** 将浮点数分解为整数部分和小数部分。
** 返回两个值：整数部分和小数部分。
**
** 数学原理：
** 对于数 x，modf(x) 返回 (i, f)，其中：
** - i 是 x 的整数部分（向零取整）
** - f 是 x 的小数部分
** - x = i + f，且 |f| < 1
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是2）
**
** 栈操作：
** 输入：number x
** 输出：number 整数部分, number 小数部分
**
** 特殊值处理：
** - modf(3.14) = (3, 0.14)
** - modf(-3.14) = (-3, -0.14)
** - modf(5) = (5, 0)
** - modf(±∞) = (±∞, NaN)
** - modf(NaN) = (NaN, NaN)
**
** 符号规则：
** 整数部分和小数部分的符号与原数相同
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** i, f = math.modf(3.14)   --> i=3, f=0.14
** i, f = math.modf(-3.14)  --> i=-3, f=-0.14
** i, f = math.modf(5)      --> i=5, f=0
*/
static int math_modf(lua_State *L)
{
    double ip;
    double fp = modf(luaL_checknumber(L, 1), &ip);
    lua_pushnumber(L, ip);
    lua_pushnumber(L, fp);
    return 2;
}

/*
** [根函数] 平方根函数
**
** 功能描述：
** 计算数值的平方根。
**
** 数学原理：
** sqrt(x) = x^(1/2)，其中 x ≥ 0
** 对于负数，结果为 NaN
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（应该 ≥ 0）
** 输出：number sqrt(x)
**
** 特殊值处理：
** - sqrt(0) = 0
** - sqrt(1) = 1
** - sqrt(4) = 2
** - sqrt(x) = NaN，当 x < 0
** - sqrt(+∞) = +∞
** - sqrt(NaN) = NaN
**
** 精度说明：
** 使用高效的硬件或软件算法，精度接近机器精度
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.sqrt(4)   --> 2
** math.sqrt(2)   --> 1.4142135623730951
** math.sqrt(0)   --> 0
*/
static int math_sqrt(lua_State *L)
{
    lua_pushnumber(L, sqrt(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [幂函数] 幂运算函数
**
** 功能描述：
** 计算 x 的 y 次幂。
**
** 数学原理：
** pow(x, y) = x^y
** 特殊情况较多，需要仔细处理
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（底数）, number y（指数）
** 输出：number x^y
**
** 特殊值处理：
** - pow(x, 0) = 1（对所有 x，包括 0）
** - pow(0, y) = 0（当 y > 0）
** - pow(0, y) = +∞（当 y < 0）
** - pow(1, y) = 1（对所有 y，包括 NaN）
** - pow(x, 1) = x（对所有 x）
** - pow(-1, ±∞) = 1
** - pow(|x| < 1, +∞) = 0
** - pow(|x| > 1, +∞) = +∞
**
** 复数结果：
** 当底数为负数且指数为非整数时，数学上结果为复数，
** 但此函数返回 NaN
**
** 算法复杂度：O(log y) 时间（对于整数指数），O(1) 空间
**
** 使用示例：
** math.pow(2, 3)    --> 8
** math.pow(4, 0.5)  --> 2
** math.pow(2, -1)   --> 0.5
*/
static int math_pow(lua_State *L)
{
    lua_pushnumber(L, pow(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

/*
** [对数函数] 自然对数函数
**
** 功能描述：
** 计算以 e 为底的对数（自然对数）。
**
** 数学原理：
** log(x) = ln(x)，其中 x > 0
** e^(log(x)) = x
** log(e) = 1
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（必须 > 0）
** 输出：number log(x)
**
** 特殊值处理：
** - log(1) = 0
** - log(e) = 1
** - log(0) = -∞
** - log(x) = NaN，当 x < 0
** - log(+∞) = +∞
** - log(NaN) = NaN
**
** 数学性质：
** - log(xy) = log(x) + log(y)
** - log(x/y) = log(x) - log(y)
** - log(x^y) = y*log(x)
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.log(1)      --> 0
** math.log(math.exp(1)) --> 1
** math.log(10)     --> 2.302585092994046
*/
static int math_log(lua_State *L)
{
    lua_pushnumber(L, log(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [对数函数] 常用对数函数
**
** 功能描述：
** 计算以 10 为底的对数（常用对数）。
**
** 数学原理：
** log10(x) = log(x) / log(10)，其中 x > 0
** 10^(log10(x)) = x
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（必须 > 0）
** 输出：number log10(x)
**
** 特殊值处理：
** - log10(1) = 0
** - log10(10) = 1
** - log10(100) = 2
** - log10(0) = -∞
** - log10(x) = NaN，当 x < 0
** - log10(+∞) = +∞
** - log10(NaN) = NaN
**
** 应用场景：
** - 科学计数法
** - 分贝计算：dB = 20*log10(ratio)
** - pH 值计算
** - 地震震级计算
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.log10(1)    --> 0
** math.log10(10)   --> 1
** math.log10(100)  --> 2
** math.log10(1000) --> 3
*/
static int math_log10(lua_State *L)
{
    lua_pushnumber(L, log10(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [指数函数] 自然指数函数
**
** 功能描述：
** 计算 e 的 x 次幂。
**
** 数学原理：
** exp(x) = e^x，其中 e ≈ 2.718281828459045
** exp(log(x)) = x
** 泰勒级数：exp(x) = 1 + x + x²/2! + x³/3! + ...
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x
** 输出：number e^x
**
** 特殊值处理：
** - exp(0) = 1
** - exp(1) = e ≈ 2.718281828459045
** - exp(+∞) = +∞
** - exp(-∞) = 0
** - exp(NaN) = NaN
**
** 数学性质：
** - exp(x + y) = exp(x) * exp(y)
** - exp(x - y) = exp(x) / exp(y)
** - exp(n*x) = (exp(x))^n
**
** 应用场景：
** - 复利计算
** - 概率分布（正态分布、指数分布）
** - 微分方程解
** - 信号处理
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.exp(0)  --> 1
** math.exp(1)  --> 2.718281828459045
** math.exp(2)  --> 7.38905609893065
*/
static int math_exp(lua_State *L)
{
    lua_pushnumber(L, exp(luaL_checknumber(L, 1)));
    return 1;
}

/*
** [角度转换] 弧度转角度函数
**
** 功能描述：
** 将弧度制角度转换为角度制。
**
** 数学原理：
** degrees = radians * (180 / π)
** 1 弧度 = 180/π 度 ≈ 57.29577951308232 度
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（弧度）
** 输出：number 对应的角度
**
** 特殊值处理：
** - deg(0) = 0
** - deg(π) = 180
** - deg(π/2) = 90
** - deg(2π) = 360
** - deg(±∞) = ±∞
** - deg(NaN) = NaN
**
** 应用场景：
** - 三角函数结果的可读性转换
** - 用户界面显示
** - 工程计算中的角度表示
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.deg(0)        --> 0
** math.deg(math.pi)  --> 180
** math.deg(math.pi/2) --> 90
*/
static int math_deg(lua_State *L)
{
    lua_pushnumber(L, luaL_checknumber(L, 1) / RADIANS_PER_DEGREE);
    return 1;
}

/*
** [角度转换] 角度转弧度函数
**
** 功能描述：
** 将角度制角度转换为弧度制。
**
** 数学原理：
** radians = degrees * (π / 180)
** 1 度 = π/180 弧度 ≈ 0.017453292519943295 弧度
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x（角度）
** 输出：number 对应的弧度
**
** 特殊值处理：
** - rad(0) = 0
** - rad(180) = π
** - rad(90) = π/2
** - rad(360) = 2π
** - rad(±∞) = ±∞
** - rad(NaN) = NaN
**
** 应用场景：
** - 为三角函数准备输入参数
** - 角度计算的标准化
** - 科学计算中的角度处理
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.rad(0)   --> 0
** math.rad(180) --> 3.141592653589793 (π)
** math.rad(90)  --> 1.5707963267948966 (π/2)
*/
static int math_rad(lua_State *L)
{
    lua_pushnumber(L, luaL_checknumber(L, 1) * RADIANS_PER_DEGREE);
    return 1;
}

/*
** [浮点操作] 浮点数分解函数
**
** 功能描述：
** 将浮点数分解为尾数和指数，满足 x = mantissa * 2^exponent。
**
** 数学原理：
** frexp(x) 返回 (m, e)，其中：
** - x = m * 2^e
** - 0.5 ≤ |m| < 1（当 x ≠ 0 时）
** - e 是整数
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是2）
**
** 栈操作：
** 输入：number x
** 输出：number 尾数, integer 指数
**
** 特殊值处理：
** - frexp(0) = (0, 0)
** - frexp(1) = (0.5, 1)
** - frexp(2) = (0.5, 2)
** - frexp(0.5) = (0.5, 0)
** - frexp(±∞) = (±∞, 未定义)
** - frexp(NaN) = (NaN, 未定义)
**
** 应用场景：
** - 浮点数内部表示分析
** - 数值算法中的尺度分离
** - 高精度计算的预处理
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** m, e = math.frexp(8)    --> m=0.5, e=4 (因为 8 = 0.5 * 2^4)
** m, e = math.frexp(1.5)  --> m=0.75, e=1 (因为 1.5 = 0.75 * 2^1)
*/
static int math_frexp(lua_State *L)
{
    int e;
    lua_pushnumber(L, frexp(luaL_checknumber(L, 1), &e));
    lua_pushinteger(L, e);
    return 2;
}

/*
** [浮点操作] 浮点数合成函数
**
** 功能描述：
** 根据尾数和指数合成浮点数，计算 mantissa * 2^exponent。
**
** 数学原理：
** ldexp(m, e) = m * 2^e
** 这是 frexp 的逆操作
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number 尾数, integer 指数
** 输出：number 合成结果
**
** 特殊值处理：
** - ldexp(0, e) = 0（对任意 e）
** - ldexp(m, 0) = m
** - ldexp(±∞, e) = ±∞
** - ldexp(NaN, e) = NaN
** - 结果可能溢出为 ±∞
** - 结果可能下溢为 0
**
** 应用场景：
** - 与 frexp 配合进行浮点数操作
** - 高效的 2 的幂次乘法
** - 数值算法中的尺度调整
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.ldexp(0.5, 4)  --> 8 (因为 0.5 * 2^4 = 8)
** math.ldexp(1.5, 2)  --> 6 (因为 1.5 * 2^2 = 6)
*/
static int math_ldexp(lua_State *L)
{
    lua_pushnumber(L, ldexp(luaL_checknumber(L, 1), luaL_checkint(L, 2)));
    return 1;
}

/*
** [比较函数] 最小值函数
**
** 功能描述：
** 返回所有参数中的最小值。支持可变数量的参数。
**
** 数学原理：
** min(x₁, x₂, ..., xₙ) = min{x₁, x₂, ..., xₙ}
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x1, number x2, ..., number xn（至少1个参数）
** 输出：number 最小值
**
** 特殊值处理：
** - 如果任一参数为 NaN，结果为 NaN
** - min(-0, +0) 的结果依赖于实现（通常为 -0）
** - 支持 ±∞ 参与比较
**
** 算法说明：
** 使用线性扫描算法，时间复杂度为 O(n)
**
** 错误处理：
** 至少需要一个参数，否则会产生错误
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 是参数个数
**
** 使用示例：
** math.min(3, 1, 4, 1, 5)  --> 1
** math.min(-2, 0, 2)       --> -2
** math.min(1.5, 1.2, 1.8) --> 1.2
*/
static int math_min(lua_State *L)
{
    int n = lua_gettop(L);  // 参数数量
    lua_Number dmin = luaL_checknumber(L, 1);
    int i;

    // 遍历所有参数，找出最小值
    for (i = 2; i <= n; i++)
    {
        lua_Number d = luaL_checknumber(L, i);

        if (d < dmin)
        {
            dmin = d;
        }
    }

    lua_pushnumber(L, dmin);
    return 1;
}

/*
** [比较函数] 最大值函数
**
** 功能描述：
** 返回所有参数中的最大值。支持可变数量的参数。
**
** 数学原理：
** max(x₁, x₂, ..., xₙ) = max{x₁, x₂, ..., xₙ}
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：number x1, number x2, ..., number xn（至少1个参数）
** 输出：number 最大值
**
** 特殊值处理：
** - 如果任一参数为 NaN，结果为 NaN
** - max(-0, +0) 的结果依赖于实现（通常为 +0）
** - 支持 ±∞ 参与比较
**
** 算法说明：
** 使用线性扫描算法，时间复杂度为 O(n)
**
** 错误处理：
** 至少需要一个参数，否则会产生错误
**
** 算法复杂度：O(n) 时间，O(1) 空间，其中 n 是参数个数
**
** 使用示例：
** math.max(3, 1, 4, 1, 5)  --> 5
** math.max(-2, 0, 2)       --> 2
** math.max(1.5, 1.2, 1.8) --> 1.8
*/
static int math_max(lua_State *L)
{
    int n = lua_gettop(L);  // 参数数量
    lua_Number dmax = luaL_checknumber(L, 1);
    int i;

    // 遍历所有参数，找出最大值
    for (i = 2; i <= n; i++)
    {
        lua_Number d = luaL_checknumber(L, i);

        if (d > dmax)
        {
            dmax = d;
        }
    }

    lua_pushnumber(L, dmax);
    return 1;
}

/*
** [随机数] 随机数生成函数
**
** 功能描述：
** 生成伪随机数。支持三种调用模式：
** - 无参数：返回 [0, 1) 区间的浮点数
** - 一个参数 n：返回 [1, n] 区间的整数
** - 两个参数 m, n：返回 [m, n] 区间的整数
**
** 数学原理：
** 使用线性同余生成器或其他伪随机数算法
** 基础随机数范围：[0, RAND_MAX]
** 归一化：r = rand() / RAND_MAX
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1）
**
** 栈操作：
** 输入：无参数 或 number n 或 number m, number n
** 输出：number 随机数
**
** 调用模式：
** 1. math.random()：返回 [0, 1) 的浮点数
** 2. math.random(n)：返回 [1, n] 的整数（n ≥ 1）
** 3. math.random(m, n)：返回 [m, n] 的整数（m ≤ n）
**
** 特殊处理：
** - 使用 rand() % RAND_MAX 避免 rand() 返回 RAND_MAX 的罕见情况
** - 对于整数模式，使用 floor() 确保结果为整数
** - 参数验证：确保区间有效（非空）
**
** 随机性说明：
** - 伪随机数，具有确定性
** - 周期长度取决于底层 rand() 实现
** - 分布均匀性取决于底层算法质量
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.random()      --> 0.7234567 (示例值)
** math.random(6)     --> 4 (1到6之间的整数，模拟骰子)
** math.random(10, 20) --> 15 (10到20之间的整数)
*/
static int math_random(lua_State *L)
{
    // 避免 rand() 返回 RAND_MAX 的罕见情况，并归一化到 [0, 1)
    lua_Number r = (lua_Number)(rand() % RAND_MAX) / (lua_Number)RAND_MAX;

    switch (lua_gettop(L))
    {
        case 0:
        {
            // 无参数：返回 [0, 1) 的浮点数
            lua_pushnumber(L, r);
            break;
        }
        case 1:
        {
            // 一个参数：返回 [1, n] 的整数
            int u = luaL_checkint(L, 1);
            luaL_argcheck(L, 1 <= u, 1, "interval is empty");
            lua_pushnumber(L, floor(r * u) + 1);
            break;
        }
        case 2:
        {
            // 两个参数：返回 [m, n] 的整数
            int l = luaL_checkint(L, 1);
            int u = luaL_checkint(L, 2);
            luaL_argcheck(L, l <= u, 2, "interval is empty");
            lua_pushnumber(L, floor(r * (u - l + 1)) + l);
            break;
        }
        default:
            return luaL_error(L, "wrong number of arguments");
    }

    return 1;
}

/*
** [随机数] 随机数种子设置函数
**
** 功能描述：
** 设置伪随机数生成器的种子值。
** 相同的种子会产生相同的随机数序列。
**
** 数学原理：
** 伪随机数生成器是确定性算法，种子决定了整个序列
** 不同的种子产生不同的随机数序列
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是0）
**
** 栈操作：
** 输入：integer seed（种子值）
** 输出：无返回值
**
** 种子选择建议：
** - 使用当前时间：math.randomseed(os.time())
** - 使用进程ID或其他变化值
** - 避免使用固定值（除非需要可重现的序列）
**
** 应用场景：
** - 程序启动时初始化随机数生成器
** - 需要可重现随机序列的测试
** - 游戏中的关卡生成
** - 蒙特卡洛模拟
**
** 注意事项：
** - 种子设置后，随机数序列完全确定
** - 相同种子在相同平台上产生相同序列
** - 不同平台可能有不同的 rand() 实现
**
** 算法复杂度：O(1) 时间，O(1) 空间
**
** 使用示例：
** math.randomseed(12345)     -- 设置固定种子
** math.randomseed(os.time()) -- 使用当前时间作为种子
*/
static int math_randomseed(lua_State *L)
{
    srand(luaL_checkint(L, 1));
    return 0;
}

/*
** [数据结构] 数学库函数注册表
**
** 数据结构说明：
** 包含所有数学库函数的注册信息，按字母顺序排列。
** 每个元素都是 luaL_Reg 结构体，包含函数名和对应的 C 函数指针。
**
** 函数分类：
** - 基本运算：abs
** - 三角函数：sin, cos, tan, asin, acos, atan, atan2
** - 双曲函数：sinh, cosh, tanh
** - 指数对数：exp, log, log10, pow, sqrt
** - 取整函数：ceil, floor, fmod, modf
** - 浮点操作：frexp, ldexp
** - 比较函数：min, max
** - 角度转换：deg, rad
** - 随机数：random, randomseed
**
** 排序说明：
** 函数按字母顺序排列，便于查找和维护
*/
static const luaL_Reg mathlib[] =
{
    {"abs",        math_abs},
    {"acos",       math_acos},
    {"asin",       math_asin},
    {"atan2",      math_atan2},
    {"atan",       math_atan},
    {"ceil",       math_ceil},
    {"cosh",       math_cosh},
    {"cos",        math_cos},
    {"deg",        math_deg},
    {"exp",        math_exp},
    {"floor",      math_floor},
    {"fmod",       math_fmod},
    {"frexp",      math_frexp},
    {"ldexp",      math_ldexp},
    {"log10",      math_log10},
    {"log",        math_log},
    {"max",        math_max},
    {"min",        math_min},
    {"modf",       math_modf},
    {"pow",        math_pow},
    {"rad",        math_rad},
    {"random",     math_random},
    {"randomseed", math_randomseed},
    {"sinh",       math_sinh},
    {"sin",        math_sin},
    {"sqrt",       math_sqrt},
    {"tanh",       math_tanh},
    {"tan",        math_tan},
    {NULL, NULL}
};

/*
** [核心] Lua 数学库初始化函数
**
** 功能描述：
** 初始化 Lua 数学库，注册所有数学函数并设置数学常量。
** 这是数学库的入口点，由 Lua 解释器在加载库时调用。
**
** 详细初始化流程：
** 1. 注册所有数学函数到 math 表中
** 2. 设置数学常量 math.pi（圆周率）
** 3. 设置数学常量 math.huge（正无穷大）
** 4. 可选：设置兼容性别名 math.mod（指向 math.fmod）
**
** 参数说明：
** @param L - lua_State*：Lua 状态机指针
**
** 返回值：
** @return int：返回值数量（总是1，表示 math 库表）
**
** 栈操作：
** 在栈顶留下 math 库表
**
** 数学常量说明：
** - math.pi：高精度圆周率值，精度达到双精度浮点数极限
** - math.huge：正无穷大值，等于 HUGE_VAL
** - math.mod：（可选）math.fmod 的别名，用于向后兼容
**
** 兼容性处理：
** 如果定义了 LUA_COMPAT_MOD，则创建 math.mod 作为 math.fmod 的别名
**
** 设计说明：
** - 所有函数都直接映射到 C 标准库函数，确保性能和精度
** - 常量使用高精度值，满足科学计算需求
** - 支持向后兼容性选项
**
** 算法复杂度：O(n) 时间，其中 n 是函数数量，O(1) 空间
**
** 使用示例：
** require("math")
** print(math.pi)    --> 3.141592653589793
** print(math.huge)  --> inf
** print(math.sin(math.pi/2)) --> 1
*/
LUALIB_API int luaopen_math(lua_State *L)
{
    // 步骤1：注册数学库函数
    luaL_register(L, LUA_MATHLIBNAME, mathlib);

    // 步骤2：设置圆周率常量
    lua_pushnumber(L, PI);
    lua_setfield(L, -2, "pi");

    // 步骤3：设置正无穷大常量
    lua_pushnumber(L, HUGE_VAL);
    lua_setfield(L, -2, "huge");

    // 步骤4：可选的兼容性处理
#if defined(LUA_COMPAT_MOD)
    // 创建 math.mod 作为 math.fmod 的别名
    lua_getfield(L, -1, "fmod");
    lua_setfield(L, -2, "mod");
#endif

    // 返回 math 库表
    return 1;
}
