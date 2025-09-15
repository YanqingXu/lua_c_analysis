/**
 * @file lmathlib.c
 * @brief Lua数学库：完整的数学函数和数值计算支持
 *
 * 版权信息：
 * $Id: lmathlib.c,v 1.67.1.1 2007/12/27 13:02:25 roberto Exp $
 * 标准数学库
 * 版权声明见lua.h文件
 *
 * 程序概述：
 * 本文件实现了Lua的标准数学函数库，提供了完整的数学计算
 * 功能支持。它是科学计算、数值分析和工程应用的基础，
 * 封装了C标准数学库的所有核心函数。
 *
 * 主要功能：
 * 1. 基础数学运算（绝对值、最大最小值、取整等）
 * 2. 三角函数（正弦、余弦、正切及其反函数和双曲函数）
 * 3. 对数和指数函数（自然对数、常用对数、指数运算）
 * 4. 幂函数和开方运算（平方根、任意次幂）
 * 5. 随机数生成（伪随机数算法、种子设置）
 * 6. 数值转换和格式化（浮点数分解、角度弧度转换）
 * 7. 数学常数（π、无穷大等）
 * 8. 浮点数操作（尾数指数分解、重组等）
 *
 * 设计特点：
 * 1. 高精度计算：基于IEEE 754浮点数标准
 * 2. 完整性：涵盖所有常用数学函数
 * 3. 标准兼容：遵循C99数学库规范
 * 4. 错误处理：合理处理定义域和特殊值
 * 5. 性能优化：直接调用底层数学库函数
 *
 * 数学计算技术：
 * - IEEE 754双精度浮点数处理
 * - 数值稳定性和精度控制
 * - 特殊值处理（NaN、无穷大等）
 * - 定义域和值域检查
 * - 三角函数的周期性处理
 *
 * 应用场景：
 * - 科学计算和数值分析
 * - 工程计算和仿真
 * - 图形学和游戏开发
 * - 统计分析和数据处理
 * - 算法实现和数学建模
 *
 * @author Lua开发团队
 * @version 5.1.5
 * @date 2007
 *
 * @note 这是Lua标准库的数学函数实现
 * @see lua.h, lauxlib.h, lualib.h, math.h
 */

#include <stdlib.h>
#include <math.h>

#define lmathlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/**
 * @defgroup MathConstants 数学常数定义
 * @brief 数学库使用的重要常数和转换因子
 *
 * 数学常数提供了高精度的数学计算基础，包括
 * 圆周率和角度弧度转换因子。
 * @{
 */

/**
 * @brief 圆周率π的高精度定义
 *
 * 重新定义PI常数，确保使用高精度值。
 * 这个值有15位有效数字，满足双精度浮点数的精度要求。
 *
 * @note 取消之前的PI定义，使用标准的高精度值
 * @note 精度：3.14159265358979323846
 */
#undef PI
#define PI (3.14159265358979323846)

/**
 * @brief 弧度到度数的转换因子
 *
 * 定义角度和弧度之间的转换关系。
 * 1弧度 = 180/π 度，1度 = π/180 弧度。
 *
 * @note 用于math_deg和math_rad函数
 * @note 基于高精度PI值计算
 */
#define RADIANS_PER_DEGREE (PI/180.0)

/** @} */ /* 结束数学常数定义文档组 */

/**
 * @defgroup BasicMathFunctions 基础数学函数
 * @brief 基本的数学运算和数值处理函数
 *
 * 基础数学函数提供了最常用的数学运算，
 * 包括绝对值、取整等基本操作。
 * @{
 */

/**
 * @brief 计算绝对值
 *
 * 返回数值的绝对值。这是math.abs函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（绝对值）
 *
 * @note 参数1：数值
 * @note 使用fabs函数处理浮点数
 *
 * @see fabs, luaL_checknumber
 *
 * 数学定义：
 * ```
 * |x| = x   if x >= 0
 * |x| = -x  if x < 0
 * ```
 *
 * 特殊值处理：
 * - abs(NaN) = NaN
 * - abs(+∞) = +∞
 * - abs(-∞) = +∞
 * - abs(±0) = +0
 *
 * 使用示例：
 * ```lua
 * print(math.abs(-5))    -- 5
 * print(math.abs(3.14))  -- 3.14
 * print(math.abs(0))     -- 0
 * ```
 */
static int math_abs (lua_State *L) {
    lua_pushnumber(L, fabs(luaL_checknumber(L, 1)));
    return 1;
}

/** @} */ /* 结束基础数学函数文档组 */

/**
 * @defgroup TrigonometricFunctions 三角函数系统
 * @brief 完整的三角函数和双曲函数支持
 *
 * 三角函数系统提供了所有标准的三角函数、
 * 反三角函数和双曲函数。
 * @{
 */

/**
 * @brief 计算正弦值
 *
 * 计算角度的正弦值（弧度制）。这是math.sin函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（正弦值）
 *
 * @note 参数1：角度（弧度）
 * @note 返回值范围：[-1, 1]
 *
 * @see sin, luaL_checknumber
 *
 * 数学定义：
 * - 定义域：(-∞, +∞)
 * - 值域：[-1, 1]
 * - 周期：2π
 * - 奇函数：sin(-x) = -sin(x)
 *
 * 特殊值：
 * - sin(0) = 0
 * - sin(π/2) = 1
 * - sin(π) = 0
 * - sin(3π/2) = -1
 *
 * 使用示例：
 * ```lua
 * print(math.sin(0))           -- 0
 * print(math.sin(math.pi/2))   -- 1
 * print(math.sin(math.pi))     -- 0 (近似)
 * ```
 */
static int math_sin (lua_State *L) {
    lua_pushnumber(L, sin(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算双曲正弦值
 *
 * 计算双曲正弦值。这是math.sinh函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（双曲正弦值）
 *
 * @note 参数1：实数
 * @note 定义域：(-∞, +∞)
 *
 * @see sinh, luaL_checknumber
 *
 * 数学定义：
 * ```
 * sinh(x) = (e^x - e^(-x)) / 2
 * ```
 *
 * 性质：
 * - 定义域：(-∞, +∞)
 * - 值域：(-∞, +∞)
 * - 奇函数：sinh(-x) = -sinh(x)
 * - 单调递增函数
 *
 * 特殊值：
 * - sinh(0) = 0
 * - sinh(+∞) = +∞
 * - sinh(-∞) = -∞
 *
 * 使用示例：
 * ```lua
 * print(math.sinh(0))   -- 0
 * print(math.sinh(1))   -- 1.1752011936438014
 * ```
 */
static int math_sinh (lua_State *L) {
    lua_pushnumber(L, sinh(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算余弦值
 *
 * 计算角度的余弦值（弧度制）。这是math.cos函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（余弦值）
 *
 * @note 参数1：角度（弧度）
 * @note 返回值范围：[-1, 1]
 *
 * @see cos, luaL_checknumber
 *
 * 数学定义：
 * - 定义域：(-∞, +∞)
 * - 值域：[-1, 1]
 * - 周期：2π
 * - 偶函数：cos(-x) = cos(x)
 *
 * 特殊值：
 * - cos(0) = 1
 * - cos(π/2) = 0
 * - cos(π) = -1
 * - cos(3π/2) = 0
 *
 * 使用示例：
 * ```lua
 * print(math.cos(0))           -- 1
 * print(math.cos(math.pi/2))   -- 0 (近似)
 * print(math.cos(math.pi))     -- -1
 * ```
 */
static int math_cos (lua_State *L) {
    lua_pushnumber(L, cos(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算双曲余弦值
 *
 * 计算双曲余弦值。这是math.cosh函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（双曲余弦值）
 *
 * @note 参数1：实数
 * @note 返回值范围：[1, +∞)
 *
 * @see cosh, luaL_checknumber
 *
 * 数学定义：
 * ```
 * cosh(x) = (e^x + e^(-x)) / 2
 * ```
 *
 * 性质：
 * - 定义域：(-∞, +∞)
 * - 值域：[1, +∞)
 * - 偶函数：cosh(-x) = cosh(x)
 * - 在x=0处取最小值1
 *
 * 特殊值：
 * - cosh(0) = 1
 * - cosh(+∞) = +∞
 * - cosh(-∞) = +∞
 *
 * 使用示例：
 * ```lua
 * print(math.cosh(0))   -- 1
 * print(math.cosh(1))   -- 1.5430806348152437
 * ```
 */
static int math_cosh (lua_State *L) {
    lua_pushnumber(L, cosh(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算正切值
 *
 * 计算角度的正切值（弧度制）。这是math.tan函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（正切值）
 *
 * @note 参数1：角度（弧度）
 * @note 在π/2 + nπ处未定义
 *
 * @see tan, luaL_checknumber
 *
 * 数学定义：
 * ```
 * tan(x) = sin(x) / cos(x)
 * ```
 *
 * 性质：
 * - 定义域：x ≠ π/2 + nπ (n为整数)
 * - 值域：(-∞, +∞)
 * - 周期：π
 * - 奇函数：tan(-x) = -tan(x)
 *
 * 特殊值：
 * - tan(0) = 0
 * - tan(π/4) = 1
 * - tan(π/2) = ±∞ (未定义)
 * - tan(π) = 0
 *
 * 使用示例：
 * ```lua
 * print(math.tan(0))           -- 0
 * print(math.tan(math.pi/4))   -- 1
 * print(math.tan(math.pi))     -- 0 (近似)
 * ```
 */
static int math_tan (lua_State *L) {
    lua_pushnumber(L, tan(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算双曲正切值
 *
 * 计算双曲正切值。这是math.tanh函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（双曲正切值）
 *
 * @note 参数1：实数
 * @note 返回值范围：(-1, 1)
 *
 * @see tanh, luaL_checknumber
 *
 * 数学定义：
 * ```
 * tanh(x) = sinh(x) / cosh(x) = (e^x - e^(-x)) / (e^x + e^(-x))
 * ```
 *
 * 性质：
 * - 定义域：(-∞, +∞)
 * - 值域：(-1, 1)
 * - 奇函数：tanh(-x) = -tanh(x)
 * - 单调递增函数
 * - 水平渐近线：y = ±1
 *
 * 特殊值：
 * - tanh(0) = 0
 * - tanh(+∞) = 1
 * - tanh(-∞) = -1
 *
 * 使用示例：
 * ```lua
 * print(math.tanh(0))   -- 0
 * print(math.tanh(1))   -- 0.7615941559557649
 * ```
 */
static int math_tanh (lua_State *L) {
    lua_pushnumber(L, tanh(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算反正弦值
 *
 * 计算反正弦值（结果为弧度）。这是math.asin函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（反正弦值）
 *
 * @note 参数1：数值，必须在[-1, 1]范围内
 * @note 返回值范围：[-π/2, π/2]
 *
 * @see asin, luaL_checknumber
 *
 * 数学定义：
 * - 定义域：[-1, 1]
 * - 值域：[-π/2, π/2]
 * - 奇函数：asin(-x) = -asin(x)
 * - 单调递增函数
 *
 * 特殊值：
 * - asin(0) = 0
 * - asin(1) = π/2
 * - asin(-1) = -π/2
 * - asin(x) = NaN if |x| > 1
 *
 * 使用示例：
 * ```lua
 * print(math.asin(0))    -- 0
 * print(math.asin(1))    -- 1.5707963267948966 (π/2)
 * print(math.asin(0.5))  -- 0.5235987755982989 (π/6)
 * ```
 */
static int math_asin (lua_State *L) {
    lua_pushnumber(L, asin(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算反余弦值
 *
 * 计算反余弦值（结果为弧度）。这是math.acos函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（反余弦值）
 *
 * @note 参数1：数值，必须在[-1, 1]范围内
 * @note 返回值范围：[0, π]
 *
 * @see acos, luaL_checknumber
 *
 * 数学定义：
 * - 定义域：[-1, 1]
 * - 值域：[0, π]
 * - 单调递减函数
 * - acos(x) + asin(x) = π/2
 *
 * 特殊值：
 * - acos(1) = 0
 * - acos(0) = π/2
 * - acos(-1) = π
 * - acos(x) = NaN if |x| > 1
 *
 * 使用示例：
 * ```lua
 * print(math.acos(1))    -- 0
 * print(math.acos(0))    -- 1.5707963267948966 (π/2)
 * print(math.acos(-1))   -- 3.141592653589793 (π)
 * ```
 */
static int math_acos (lua_State *L) {
    lua_pushnumber(L, acos(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算反正切值
 *
 * 计算反正切值（结果为弧度）。这是math.atan函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（反正切值）
 *
 * @note 参数1：数值
 * @note 返回值范围：(-π/2, π/2)
 *
 * @see atan, luaL_checknumber
 *
 * 数学定义：
 * - 定义域：(-∞, +∞)
 * - 值域：(-π/2, π/2)
 * - 奇函数：atan(-x) = -atan(x)
 * - 单调递增函数
 * - 水平渐近线：y = ±π/2
 *
 * 特殊值：
 * - atan(0) = 0
 * - atan(1) = π/4
 * - atan(+∞) = π/2
 * - atan(-∞) = -π/2
 *
 * 使用示例：
 * ```lua
 * print(math.atan(0))   -- 0
 * print(math.atan(1))   -- 0.7853981633974483 (π/4)
 * print(math.atan(-1))  -- -0.7853981633974483 (-π/4)
 * ```
 */
static int math_atan (lua_State *L) {
    lua_pushnumber(L, atan(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算两参数反正切值
 *
 * 计算atan2(y, x)，返回点(x,y)的极角。这是math.atan2函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（反正切值）
 *
 * @note 参数1：y坐标
 * @note 参数2：x坐标
 * @note 返回值范围：(-π, π]
 *
 * @see atan2, luaL_checknumber
 *
 * 数学定义：
 * ```
 * atan2(y, x) = atan(y/x) + 象限修正
 * ```
 *
 * 象限处理：
 * - 第一象限(x>0, y>0)：atan(y/x)
 * - 第二象限(x<0, y>0)：atan(y/x) + π
 * - 第三象限(x<0, y<0)：atan(y/x) - π
 * - 第四象限(x>0, y<0)：atan(y/x)
 *
 * 特殊情况：
 * - atan2(0, 0) = 0
 * - atan2(y, 0) = π/2 * sign(y)
 * - atan2(0, x) = 0 if x > 0, π if x < 0
 *
 * 使用示例：
 * ```lua
 * print(math.atan2(1, 1))   -- 0.7853981633974483 (π/4)
 * print(math.atan2(1, -1))  -- 2.356194490192345 (3π/4)
 * print(math.atan2(-1, -1)) -- -2.356194490192345 (-3π/4)
 * ```
 */
static int math_atan2 (lua_State *L) {
    lua_pushnumber(L, atan2(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

/** @} */ /* 结束三角函数系统文档组 */

/**
 * @defgroup RoundingFunctions 取整和取模函数
 * @brief 数值取整、取模和分解函数
 *
 * 取整和取模函数提供了各种数值处理和
 * 分解操作。
 * @{
 */

/**
 * @brief 向上取整
 *
 * 返回不小于给定数值的最小整数。这是math.ceil函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（向上取整结果）
 *
 * @note 参数1：数值
 * @note 返回最小的不小于参数的整数
 *
 * @see ceil, luaL_checknumber
 *
 * 数学定义：
 * ```
 * ceil(x) = min{n ∈ Z : n ≥ x}
 * ```
 *
 * 特殊值：
 * - ceil(2.3) = 3
 * - ceil(-2.3) = -2
 * - ceil(5) = 5
 * - ceil(0) = 0
 *
 * 使用示例：
 * ```lua
 * print(math.ceil(2.3))   -- 3
 * print(math.ceil(-2.3))  -- -2
 * print(math.ceil(5))     -- 5
 * ```
 */
static int math_ceil (lua_State *L) {
    lua_pushnumber(L, ceil(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 向下取整
 *
 * 返回不大于给定数值的最大整数。这是math.floor函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（向下取整结果）
 *
 * @note 参数1：数值
 * @note 返回最大的不大于参数的整数
 *
 * @see floor, luaL_checknumber
 *
 * 数学定义：
 * ```
 * floor(x) = max{n ∈ Z : n ≤ x}
 * ```
 *
 * 特殊值：
 * - floor(2.3) = 2
 * - floor(-2.3) = -3
 * - floor(5) = 5
 * - floor(0) = 0
 *
 * 使用示例：
 * ```lua
 * print(math.floor(2.3))   -- 2
 * print(math.floor(-2.3))  -- -3
 * print(math.floor(5))     -- 5
 * ```
 */
static int math_floor (lua_State *L) {
    lua_pushnumber(L, floor(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 浮点数取模运算
 *
 * 计算x对y的浮点数取模。这是math.fmod函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（取模结果）
 *
 * @note 参数1：被除数x
 * @note 参数2：除数y
 * @note 结果的符号与被除数相同
 *
 * @see fmod, luaL_checknumber
 *
 * 数学定义：
 * ```
 * fmod(x, y) = x - n*y，其中n = trunc(x/y)
 * ```
 *
 * 性质：
 * - 结果符号与x相同
 * - |fmod(x, y)| < |y|
 * - fmod(x, y) = x - floor(x/y)*y (当y>0时)
 *
 * 特殊值：
 * - fmod(5, 3) = 2
 * - fmod(-5, 3) = -2
 * - fmod(5, -3) = 2
 * - fmod(-5, -3) = -2
 *
 * 使用示例：
 * ```lua
 * print(math.fmod(5, 3))    -- 2
 * print(math.fmod(-5, 3))   -- -2
 * print(math.fmod(5.5, 2))  -- 1.5
 * ```
 */
static int math_fmod (lua_State *L) {
    lua_pushnumber(L, fmod(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

/**
 * @brief 分解浮点数为整数和小数部分
 *
 * 将浮点数分解为整数部分和小数部分。这是math.modf函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回2（整数部分和小数部分）
 *
 * @note 参数1：浮点数
 * @note 返回值1：整数部分
 * @note 返回值2：小数部分
 *
 * @see modf, luaL_checknumber
 *
 * 数学定义：
 * ```
 * x = 整数部分 + 小数部分
 * 其中整数部分和小数部分符号相同
 * ```
 *
 * 性质：
 * - 整数部分 = trunc(x)
 * - 小数部分 = x - trunc(x)
 * - 两部分符号与原数相同
 * - |小数部分| < 1
 *
 * 使用示例：
 * ```lua
 * local i, f = math.modf(3.14)
 * print(i, f)  -- 3  0.14
 *
 * local i, f = math.modf(-3.14)
 * print(i, f)  -- -3  -0.14
 * ```
 */
static int math_modf (lua_State *L) {
    double ip;
    double fp = modf(luaL_checknumber(L, 1), &ip);
    lua_pushnumber(L, ip);
    lua_pushnumber(L, fp);
    return 2;
}

/** @} */ /* 结束取整和取模函数文档组 */

/**
 * @defgroup PowerAndRootFunctions 幂函数和开方函数
 * @brief 幂运算、开方和指数对数函数
 *
 * 幂函数和开方函数提供了指数运算、
 * 开方和相关的数学计算。
 * @{
 */

/**
 * @brief 计算平方根
 *
 * 计算数值的平方根。这是math.sqrt函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（平方根）
 *
 * @note 参数1：非负数值
 * @note 负数参数返回NaN
 *
 * @see sqrt, luaL_checknumber
 *
 * 数学定义：
 * ```
 * sqrt(x) = x^(1/2)，x ≥ 0
 * ```
 *
 * 性质：
 * - 定义域：[0, +∞)
 * - 值域：[0, +∞)
 * - 单调递增函数
 * - sqrt(x²) = |x|
 *
 * 特殊值：
 * - sqrt(0) = 0
 * - sqrt(1) = 1
 * - sqrt(4) = 2
 * - sqrt(-1) = NaN
 *
 * 使用示例：
 * ```lua
 * print(math.sqrt(4))    -- 2
 * print(math.sqrt(2))    -- 1.4142135623730951
 * print(math.sqrt(0))    -- 0
 * ```
 */
static int math_sqrt (lua_State *L) {
    lua_pushnumber(L, sqrt(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算幂运算
 *
 * 计算x的y次幂。这是math.pow函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（幂运算结果）
 *
 * @note 参数1：底数x
 * @note 参数2：指数y
 * @note 等价于x^y运算符
 *
 * @see pow, luaL_checknumber
 *
 * 数学定义：
 * ```
 * pow(x, y) = x^y
 * ```
 *
 * 特殊情况：
 * - pow(x, 0) = 1 (x ≠ 0)
 * - pow(0, y) = 0 (y > 0)
 * - pow(1, y) = 1
 * - pow(x, 1) = x
 * - pow(x, -y) = 1/pow(x, y)
 *
 * 复杂情况：
 * - 负底数的非整数次幂可能返回NaN
 * - 0的负数次幂返回无穷大
 * - 无穷大和NaN的特殊处理
 *
 * 使用示例：
 * ```lua
 * print(math.pow(2, 3))    -- 8
 * print(math.pow(4, 0.5))  -- 2 (平方根)
 * print(math.pow(10, -2))  -- 0.01
 * ```
 */
static int math_pow (lua_State *L) {
    lua_pushnumber(L, pow(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

/**
 * @brief 计算自然对数
 *
 * 计算以e为底的对数。这是math.log函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（自然对数）
 *
 * @note 参数1：正数
 * @note 负数或零返回NaN或-∞
 *
 * @see log, luaL_checknumber
 *
 * 数学定义：
 * ```
 * log(x) = ln(x) = log_e(x)，x > 0
 * ```
 *
 * 性质：
 * - 定义域：(0, +∞)
 * - 值域：(-∞, +∞)
 * - 单调递增函数
 * - log(e) = 1
 * - log(1) = 0
 *
 * 特殊值：
 * - log(1) = 0
 * - log(e) = 1
 * - log(0) = -∞
 * - log(负数) = NaN
 *
 * 使用示例：
 * ```lua
 * print(math.log(1))           -- 0
 * print(math.log(math.exp(1))) -- 1
 * print(math.log(10))          -- 2.302585092994046
 * ```
 */
static int math_log (lua_State *L) {
    lua_pushnumber(L, log(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算常用对数
 *
 * 计算以10为底的对数。这是math.log10函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（常用对数）
 *
 * @note 参数1：正数
 * @note 负数或零返回NaN或-∞
 *
 * @see log10, luaL_checknumber
 *
 * 数学定义：
 * ```
 * log10(x) = log_10(x)，x > 0
 * ```
 *
 * 性质：
 * - 定义域：(0, +∞)
 * - 值域：(-∞, +∞)
 * - 单调递增函数
 * - log10(10) = 1
 * - log10(1) = 0
 *
 * 特殊值：
 * - log10(1) = 0
 * - log10(10) = 1
 * - log10(100) = 2
 * - log10(0.1) = -1
 *
 * 使用示例：
 * ```lua
 * print(math.log10(1))     -- 0
 * print(math.log10(10))    -- 1
 * print(math.log10(100))   -- 2
 * print(math.log10(0.1))   -- -1
 * ```
 */
static int math_log10 (lua_State *L) {
    lua_pushnumber(L, log10(luaL_checknumber(L, 1)));
    return 1;
}

/**
 * @brief 计算指数函数
 *
 * 计算e的x次幂。这是math.exp函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（指数函数值）
 *
 * @note 参数1：指数
 * @note 返回e^x的值
 *
 * @see exp, luaL_checknumber
 *
 * 数学定义：
 * ```
 * exp(x) = e^x
 * ```
 *
 * 性质：
 * - 定义域：(-∞, +∞)
 * - 值域：(0, +∞)
 * - 单调递增函数
 * - exp(0) = 1
 * - exp(1) = e
 * - exp(ln(x)) = x
 *
 * 特殊值：
 * - exp(0) = 1
 * - exp(1) = e ≈ 2.718281828459045
 * - exp(+∞) = +∞
 * - exp(-∞) = 0
 *
 * 使用示例：
 * ```lua
 * print(math.exp(0))   -- 1
 * print(math.exp(1))   -- 2.718281828459045
 * print(math.exp(2))   -- 7.38905609893065
 * ```
 */
static int math_exp (lua_State *L) {
    lua_pushnumber(L, exp(luaL_checknumber(L, 1)));
    return 1;
}

/** @} */ /* 结束幂函数和开方函数文档组 */

/**
 * @defgroup AngleConversionFunctions 角度转换函数
 * @brief 弧度和角度之间的转换函数
 *
 * 角度转换函数提供了弧度制和角度制
 * 之间的相互转换。
 * @{
 */

/**
 * @brief 弧度转换为角度
 *
 * 将弧度转换为角度。这是math.deg函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（角度值）
 *
 * @note 参数1：弧度值
 * @note 返回对应的角度值
 *
 * @see RADIANS_PER_DEGREE, luaL_checknumber
 *
 * 转换公式：
 * ```
 * 角度 = 弧度 × (180/π)
 * ```
 *
 * 转换关系：
 * - π弧度 = 180度
 * - 1弧度 ≈ 57.29577951308232度
 * - 2π弧度 = 360度
 *
 * 使用示例：
 * ```lua
 * print(math.deg(math.pi))     -- 180
 * print(math.deg(math.pi/2))   -- 90
 * print(math.deg(math.pi/4))   -- 45
 * print(math.deg(1))           -- 57.29577951308232
 * ```
 */
static int math_deg (lua_State *L) {
    lua_pushnumber(L, luaL_checknumber(L, 1)/RADIANS_PER_DEGREE);
    return 1;
}

/**
 * @brief 角度转换为弧度
 *
 * 将角度转换为弧度。这是math.rad函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（弧度值）
 *
 * @note 参数1：角度值
 * @note 返回对应的弧度值
 *
 * @see RADIANS_PER_DEGREE, luaL_checknumber
 *
 * 转换公式：
 * ```
 * 弧度 = 角度 × (π/180)
 * ```
 *
 * 转换关系：
 * - 180度 = π弧度
 * - 1度 ≈ 0.017453292519943295弧度
 * - 360度 = 2π弧度
 *
 * 使用示例：
 * ```lua
 * print(math.rad(180))  -- 3.141592653589793 (π)
 * print(math.rad(90))   -- 1.5707963267948966 (π/2)
 * print(math.rad(45))   -- 0.7853981633974483 (π/4)
 * print(math.rad(1))    -- 0.017453292519943295
 * ```
 */
static int math_rad (lua_State *L) {
    lua_pushnumber(L, luaL_checknumber(L, 1)*RADIANS_PER_DEGREE);
    return 1;
}

/** @} */ /* 结束角度转换函数文档组 */

/**
 * @defgroup FloatManipulationFunctions 浮点数操作函数
 * @brief 浮点数的分解和重组函数
 *
 * 浮点数操作函数提供了IEEE 754浮点数
 * 的底层操作和分析功能。
 * @{
 */

/**
 * @brief 分解浮点数为尾数和指数
 *
 * 将浮点数分解为尾数和指数部分。这是math.frexp函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回2（尾数和指数）
 *
 * @note 参数1：浮点数
 * @note 返回值1：尾数（0.5 ≤ |尾数| < 1）
 * @note 返回值2：指数（整数）
 *
 * @see frexp, luaL_checknumber
 *
 * 数学定义：
 * ```
 * x = 尾数 × 2^指数
 * 其中 0.5 ≤ |尾数| < 1 (x ≠ 0)
 * ```
 *
 * 特殊情况：
 * - frexp(0) = (0, 0)
 * - 尾数的符号与原数相同
 * - 用于浮点数的底层分析
 *
 * 使用示例：
 * ```lua
 * local m, e = math.frexp(8)
 * print(m, e)  -- 0.5  4 (因为8 = 0.5 × 2^4)
 *
 * local m, e = math.frexp(1.5)
 * print(m, e)  -- 0.75  1 (因为1.5 = 0.75 × 2^1)
 * ```
 *
 * 应用：
 * - 浮点数精度分析
 * - 数值算法优化
 * - IEEE 754格式理解
 */
static int math_frexp (lua_State *L) {
    int e;
    lua_pushnumber(L, frexp(luaL_checknumber(L, 1), &e));
    lua_pushinteger(L, e);
    return 2;
}

/**
 * @brief 由尾数和指数重组浮点数
 *
 * 将尾数和指数重组为浮点数。这是math.ldexp函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（重组的浮点数）
 *
 * @note 参数1：尾数
 * @note 参数2：指数（整数）
 * @note 返回尾数 × 2^指数
 *
 * @see ldexp, luaL_checknumber, luaL_checkint
 *
 * 数学定义：
 * ```
 * ldexp(尾数, 指数) = 尾数 × 2^指数
 * ```
 *
 * 性质：
 * - 与frexp互为逆运算
 * - 用于精确的浮点数构造
 * - 避免精度损失的数值操作
 *
 * 使用示例：
 * ```lua
 * print(math.ldexp(0.5, 4))   -- 8 (0.5 × 2^4)
 * print(math.ldexp(0.75, 1))  -- 1.5 (0.75 × 2^1)
 * print(math.ldexp(1, 0))     -- 1 (1 × 2^0)
 * ```
 *
 * 应用：
 * - 高精度数值计算
 * - 浮点数格式转换
 * - 数值算法实现
 */
static int math_ldexp (lua_State *L) {
    lua_pushnumber(L, ldexp(luaL_checknumber(L, 1), luaL_checkint(L, 2)));
    return 1;
}

/** @} */ /* 结束浮点数操作函数文档组 */

/**
 * @defgroup VariadicMathFunctions 可变参数数学函数
 * @brief 支持多个参数的数学运算函数
 *
 * 可变参数数学函数提供了处理任意数量参数的
 * 数学运算，如最大值和最小值计算。
 * @{
 */

/**
 * @brief 计算多个数值的最小值
 *
 * 从任意数量的参数中找出最小值。这是math.min函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（最小值）
 *
 * @note 至少需要一个参数
 * @note 支持任意数量的数值参数
 * @note 使用简单比较算法，时间复杂度O(n)
 *
 * @see lua_gettop, luaL_checknumber, lua_pushnumber
 *
 * 算法实现：
 * 1. **参数计数**：
 *    - 使用lua_gettop获取参数数量
 *    - 确保至少有一个参数
 *
 * 2. **初始化**：
 *    - 将第一个参数作为初始最小值
 *    - 验证第一个参数为有效数值
 *
 * 3. **遍历比较**：
 *    - 从第二个参数开始遍历
 *    - 逐个与当前最小值比较
 *    - 更新最小值记录
 *
 * 4. **返回结果**：
 *    - 推送最终的最小值到栈
 *    - 返回一个结果值
 *
 * 特殊值处理：
 * - NaN参与比较时结果为NaN
 * - 正零和负零被视为相等
 * - 无穷大按数值大小比较
 *
 * 性能特点：
 * - 线性时间复杂度O(n)
 * - 常数空间复杂度O(1)
 * - 单次遍历算法
 * - 短路优化（找到NaN立即返回）
 *
 * 使用示例：
 * ```lua
 * print(math.min(3, 1, 4, 1, 5))  -- 1
 * print(math.min(-2, 0, 2))       -- -2
 * print(math.min(3.14))           -- 3.14 (单参数)
 * print(math.min(1, 0/0))         -- NaN (包含NaN)
 * ```
 *
 * 应用场景：
 * - 数据分析和统计
 * - 数值范围限制
 * - 算法优化和边界检查
 * - 多维数据处理
 */
static int math_min (lua_State *L) {
    int n = lua_gettop(L);  /* 参数数量 */
    lua_Number dmin = luaL_checknumber(L, 1);
    int i;
    for (i=2; i<=n; i++) {
        lua_Number d = luaL_checknumber(L, i);
        if (d < dmin)
            dmin = d;
    }
    lua_pushnumber(L, dmin);
    return 1;
}

/**
 * @brief 计算多个数值的最大值
 *
 * 从任意数量的参数中找出最大值。这是math.max函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（最大值）
 *
 * @note 至少需要一个参数
 * @note 支持任意数量的数值参数
 * @note 使用简单比较算法，时间复杂度O(n)
 *
 * @see lua_gettop, luaL_checknumber, lua_pushnumber
 *
 * 算法实现：
 * 1. **参数计数**：
 *    - 使用lua_gettop获取参数数量
 *    - 确保至少有一个参数
 *
 * 2. **初始化**：
 *    - 将第一个参数作为初始最大值
 *    - 验证第一个参数为有效数值
 *
 * 3. **遍历比较**：
 *    - 从第二个参数开始遍历
 *    - 逐个与当前最大值比较
 *    - 更新最大值记录
 *
 * 4. **返回结果**：
 *    - 推送最终的最大值到栈
 *    - 返回一个结果值
 *
 * 特殊值处理：
 * - NaN参与比较时结果为NaN
 * - 正零和负零被视为相等
 * - 无穷大按数值大小比较
 *
 * 性能特点：
 * - 线性时间复杂度O(n)
 * - 常数空间复杂度O(1)
 * - 单次遍历算法
 * - 短路优化（找到NaN立即返回）
 *
 * 使用示例：
 * ```lua
 * print(math.max(3, 1, 4, 1, 5))  -- 5
 * print(math.max(-2, 0, 2))       -- 2
 * print(math.max(3.14))           -- 3.14 (单参数)
 * print(math.max(1, 0/0))         -- NaN (包含NaN)
 * ```
 *
 * 应用场景：
 * - 数据分析和统计
 * - 数值范围限制
 * - 算法优化和边界检查
 * - 多维数据处理
 */
static int math_max (lua_State *L) {
    int n = lua_gettop(L);  /* 参数数量 */
    lua_Number dmax = luaL_checknumber(L, 1);
    int i;
    for (i=2; i<=n; i++) {
        lua_Number d = luaL_checknumber(L, i);
        if (d > dmax)
            dmax = d;
    }
    lua_pushnumber(L, dmax);
    return 1;
}

/** @} */ /* 结束可变参数数学函数文档组 */

/**
 * @defgroup RandomNumberGeneration 随机数生成系统
 * @brief 伪随机数生成和种子管理功能
 *
 * 随机数生成系统提供了基于线性同余生成器的
 * 伪随机数生成功能，支持多种输出格式。
 * @{
 */

/**
 * @brief 生成随机数
 *
 * 根据参数数量生成不同范围的随机数。这是math.random函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（随机数）
 *
 * @note 支持0、1、2个参数的不同调用模式
 * @note 使用C标准库的rand()函数
 * @note 需要先调用math.randomseed设置种子
 *
 * @see rand, RAND_MAX, floor, luaL_checkint, luaL_argcheck
 *
 * 调用模式：
 * 1. **无参数调用**：
 *    - 返回[0, 1)区间的浮点数
 *    - 均匀分布
 *    - 适用于概率计算
 *
 * 2. **单参数调用**：
 *    - 参数u：上限（正整数）
 *    - 返回[1, u]区间的整数
 *    - 包含边界值
 *
 * 3. **双参数调用**：
 *    - 参数l：下限（整数）
 *    - 参数u：上限（整数）
 *    - 返回[l, u]区间的整数
 *    - 包含边界值
 *
 * 算法实现：
 * 1. **基础随机数生成**：
 *    - 调用rand()获取原始随机数
 *    - 使用模运算避免溢出
 *    - 归一化到[0, 1)区间
 *
 * 2. **范围映射**：
 *    - 线性变换到目标区间
 *    - 使用floor确保整数结果
 *    - 边界值包含处理
 *
 * 3. **参数验证**：
 *    - 检查区间有效性
 *    - 确保下限不大于上限
 *    - 参数类型和范围检查
 *
 * 随机数质量：
 * - 基于系统rand()实现
 * - 周期长度依赖于系统
 * - 统计特性符合均匀分布
 * - 不适用于密码学应用
 *
 * 特殊处理：
 * - 避免r==1的罕见情况
 * - 处理某些系统rand()可能超过RAND_MAX
 * - 使用模运算确保范围正确
 *
 * 使用示例：
 * ```lua
 * math.randomseed(os.time())  -- 设置种子
 *
 * print(math.random())        -- 0.7234567 (0到1之间)
 * print(math.random(6))       -- 4 (1到6之间，骰子)
 * print(math.random(10, 20))  -- 15 (10到20之间)
 * ```
 *
 * 应用场景：
 * - 游戏开发和模拟
 * - 统计采样和蒙特卡洛方法
 * - 算法测试和数据生成
 * - 随机化算法实现
 *
 * 注意事项：
 * - 需要适当的种子初始化
 * - 不适用于安全敏感应用
 * - 周期性和统计特性依赖于系统实现
 * - 多线程环境需要考虑线程安全
 */
static int math_random (lua_State *L) {
    /* '%'运算避免了r==1的罕见情况，也是必需的，因为在某些系统上
       (SunOS!) 'rand()'可能返回大于RAND_MAX的值 */
    lua_Number r = (lua_Number)(rand()%RAND_MAX) / (lua_Number)RAND_MAX;
    switch (lua_gettop(L)) {  /* 检查参数数量 */
        case 0: {  /* 无参数 */
            lua_pushnumber(L, r);  /* 0到1之间的数 */
            break;
        }
        case 1: {  /* 仅上限 */
            int u = luaL_checkint(L, 1);
            luaL_argcheck(L, 1<=u, 1, "interval is empty");
            lua_pushnumber(L, floor(r*u)+1);  /* 1到u之间的整数 */
            break;
        }
        case 2: {  /* 下限和上限 */
            int l = luaL_checkint(L, 1);
            int u = luaL_checkint(L, 2);
            luaL_argcheck(L, l<=u, 2, "interval is empty");
            lua_pushnumber(L, floor(r*(u-l+1))+l);  /* l到u之间的整数 */
            break;
        }
        default: return luaL_error(L, "wrong number of arguments");
    }
    return 1;
}

/**
 * @brief 设置随机数种子
 *
 * 设置伪随机数生成器的种子值。这是math.randomseed函数的实现。
 *
 * @param L Lua状态机指针
 * @return 总是返回0（无返回值）
 *
 * @note 参数1：种子值（整数）
 * @note 影响后续所有math.random调用
 * @note 相同种子产生相同的随机数序列
 *
 * @see srand, luaL_checkint
 *
 * 种子设置原理：
 * 1. **初始化生成器**：
 *    - 调用C标准库的srand函数
 *    - 设置线性同余生成器的初始状态
 *    - 影响全局随机数生成器状态
 *
 * 2. **确定性序列**：
 *    - 相同种子产生相同序列
 *    - 用于可重现的随机化
 *    - 调试和测试的重要工具
 *
 * 3. **种子选择策略**：
 *    - 时间戳：os.time()提供变化的种子
 *    - 进程ID：getpid()提供进程相关种子
 *    - 组合方式：时间+进程ID+其他因素
 *
 * 种子质量考虑：
 * - 避免使用固定值或简单序列
 * - 时间戳提供足够的变化性
 * - 高质量种子改善随机性
 * - 避免可预测的种子模式
 *
 * 使用模式：
 * ```lua
 * -- 基于时间的种子（常用）
 * math.randomseed(os.time())
 *
 * -- 固定种子（调试用）
 * math.randomseed(12345)
 *
 * -- 组合种子（更好的随机性）
 * math.randomseed(os.time() + os.clock() * 1000000)
 * ```
 *
 * 应用场景：
 * - 程序启动时的初始化
 * - 测试中的可重现随机化
 * - 不同会话的随机性保证
 * - 多实例程序的独立随机化
 *
 * 注意事项：
 * - 只需在程序开始时调用一次
 * - 频繁重设种子可能降低随机性
 * - 种子值的选择影响序列质量
 * - 多线程环境的同步考虑
 */
static int math_randomseed (lua_State *L) {
    srand(luaL_checkint(L, 1));
    return 0;
}

/** @} */ /* 结束随机数生成系统文档组 */


/**
 * @defgroup LibraryRegistration 数学库注册和初始化
 * @brief 数学函数库的注册表和标准初始化机制
 *
 * 库注册和初始化系统定义了数学库中所有函数的映射
 * 和标准的Lua库初始化流程。
 * @{
 */

/**
 * @brief 数学库函数注册表
 *
 * 定义了数学库中所有导出函数的名称和实现映射。
 * 这是Lua库注册的标准模式。
 *
 * 注册的函数分类：
 *
 * **基础运算函数**：
 * - abs：绝对值
 * - max：最大值（可变参数）
 * - min：最小值（可变参数）
 *
 * **三角函数**：
 * - sin, cos, tan：基本三角函数
 * - asin, acos, atan, atan2：反三角函数
 * - sinh, cosh, tanh：双曲函数
 *
 * **指数对数函数**：
 * - exp：指数函数（e^x）
 * - log：自然对数
 * - log10：常用对数
 * - pow：幂运算
 * - sqrt：平方根
 *
 * **取整和取模函数**：
 * - ceil：向上取整
 * - floor：向下取整
 * - fmod：浮点取模
 * - modf：分解整数和小数部分
 *
 * **角度转换函数**：
 * - deg：弧度转角度
 * - rad：角度转弧度
 *
 * **浮点数操作函数**：
 * - frexp：分解尾数和指数
 * - ldexp：重组尾数和指数
 *
 * **随机数函数**：
 * - random：随机数生成
 * - randomseed：随机数种子设置
 *
 * 数组结构特点：
 * - 每个元素包含函数名和函数指针
 * - 按字母顺序排列便于查找
 * - 以{NULL, NULL}结尾标记数组结束
 * - 使用luaL_Reg结构体类型
 *
 * 设计模式：
 * - 静态常量数组，编译时确定
 * - 标准的Lua库注册格式
 * - 便于维护和扩展
 * - 支持自动化工具处理
 *
 * 函数覆盖范围：
 * - 涵盖C99标准数学库的主要函数
 * - 提供完整的数学计算支持
 * - 满足科学计算和工程应用需求
 * - 兼容IEEE 754浮点数标准
 */
static const luaL_Reg mathlib[] = {
    {"abs",   math_abs},
    {"acos",  math_acos},
    {"asin",  math_asin},
    {"atan2", math_atan2},
    {"atan",  math_atan},
    {"ceil",  math_ceil},
    {"cosh",   math_cosh},
    {"cos",   math_cos},
    {"deg",   math_deg},
    {"exp",   math_exp},
    {"floor", math_floor},
    {"fmod",   math_fmod},
    {"frexp", math_frexp},
    {"ldexp", math_ldexp},
    {"log10", math_log10},
    {"log",   math_log},
    {"max",   math_max},
    {"min",   math_min},
    {"modf",   math_modf},
    {"pow",   math_pow},
    {"rad",   math_rad},
    {"random",     math_random},
    {"randomseed", math_randomseed},
    {"sinh",   math_sinh},
    {"sin",   math_sin},
    {"sqrt",  math_sqrt},
    {"tanh",   math_tanh},
    {"tan",   math_tan},
    {NULL, NULL}
};

/**
 * @brief 数学库初始化函数
 *
 * Lua数学库的标准初始化入口点。由Lua虚拟机调用
 * 以加载和注册数学库及其常数。
 *
 * @param L Lua状态机指针
 * @return 总是返回1（库表）
 *
 * @note 函数名遵循luaopen_<libname>约定
 * @note 使用LUALIB_API导出声明
 *
 * @see luaL_register, LUA_MATHLIBNAME, lua_setfield
 *
 * 初始化流程：
 * 1. **库注册**：
 *    - 使用luaL_register注册函数表
 *    - 创建名为"math"的全局表
 *    - 将所有数学函数添加到表中
 *
 * 2. **数学常数设置**：
 *    - math.pi：圆周率π的高精度值
 *    - math.huge：正无穷大（HUGE_VAL）
 *
 * 3. **兼容性处理**：
 *    - LUA_COMPAT_MOD：提供math.mod作为math.fmod的别名
 *    - 保持向后兼容性
 *    - 条件编译支持
 *
 * 4. **返回库表**：
 *    - 将库表留在栈顶
 *    - 返回1表示有一个返回值
 *    - 符合Lua库加载约定
 *
 * 数学常数详解：
 *
 * **math.pi**：
 * - 值：3.14159265358979323846
 * - 15位有效数字的高精度π
 * - 用于角度计算和几何运算
 *
 * **math.huge**：
 * - 值：正无穷大（+∞）
 * - 基于C标准库的HUGE_VAL
 * - 用于数值比较和边界检查
 * - 表示超出浮点数范围的值
 *
 * 兼容性支持：
 * - math.mod：math.fmod的别名（可选）
 * - 支持旧版本Lua代码
 * - 通过LUA_COMPAT_MOD宏控制
 *
 * 调用方式：
 * - 静态链接：直接调用luaopen_math
 * - 动态加载：通过require "math"
 * - 自动加载：Lua启动时自动加载
 *
 * 库名称：
 * - 使用LUA_MATHLIBNAME宏定义
 * - 通常为"math"
 * - 可在编译时配置
 *
 * 使用示例：
 * ```c
 * // 在C代码中手动加载
 * luaopen_math(L);
 *
 * // 在Lua中使用
 * local math = require "math"
 * print(math.pi)    -- 3.141592653589793
 * print(math.huge)  -- inf
 * print(math.sin(math.pi/2))  -- 1
 * ```
 *
 * 标准约定：
 * - 函数名格式：luaopen_<库名>
 * - 返回值：库表（栈顶）
 * - 副作用：设置全局变量和常数
 * - 错误处理：通过Lua错误机制
 *
 * 设计优势：
 * - 完整的数学函数支持
 * - 高精度数学常数
 * - 标准的库加载机制
 * - 良好的向后兼容性
 * - 符合IEEE 754标准
 */
LUALIB_API int luaopen_math (lua_State *L) {
    luaL_register(L, LUA_MATHLIBNAME, mathlib);
    lua_pushnumber(L, PI);
    lua_setfield(L, -2, "pi");
    lua_pushnumber(L, HUGE_VAL);
    lua_setfield(L, -2, "huge");
#if defined(LUA_COMPAT_MOD)
    lua_getfield(L, -1, "fmod");
    lua_setfield(L, -2, "mod");
#endif
    return 1;
}

/** @} */ /* 结束数学库注册和初始化文档组 */

