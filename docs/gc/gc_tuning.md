# Lua 5.1.5 GC 参数调优实战指南

> **文档类型**: 实战指南 (Practical Guide)  
> **难度级别**: ⭐⭐⭐⭐ (高级)  
> **预计阅读时间**: 60-75 分钟  
> **前置知识**: 
> - [GC模块概览](./wiki_gc.md)
> - [增量垃圾回收详解](./incremental_gc.md)
> - [写屏障实现](./write_barrier.md)
> - 应用性能分析基础

---

## 📋 目录

- [1. 引言](#1-引言)
- [2. GC参数详解](#2-gc参数详解)
- [3. 性能诊断方法](#3-性能诊断方法)
- [4. 典型场景调优](#4-典型场景调优)
- [5. 调优流程](#5-调优流程)
- [6. 监控与分析工具](#6-监控与分析工具)
- [7. 最佳实践](#7-最佳实践)
- [8. 案例研究](#8-案例研究)
- [9. 故障排查](#9-故障排查)
- [10. 常见问题与解答](#10-常见问题与解答)

---

## 1. 引言

### 1.1 为什么需要GC调优？

#### 问题场景

```lua
-- 场景1：游戏掉帧
-- 正常帧率：60 FPS（每帧16.67ms）
-- 问题：GC导致某些帧超过16.67ms

function game_loop()
    while running do
        local frame_start = os.clock()
        
        update_game()      -- 10ms
        render_scene()     -- 5ms
        
        -- 问题：GC突然触发
        -- collectgarbage执行：8ms
        -- 总帧时间：23ms > 16.67ms
        -- 结果：掉帧！❌
        
        local frame_time = (os.clock() - frame_start) * 1000
        if frame_time > 16.67 then
            print("掉帧！", frame_time, "ms")
        end
    end
end
```

```lua
-- 场景2：服务器响应延迟
-- 要求：平均响应 < 10ms

function handle_request(request)
    local start = os.clock()
    
    -- 处理请求
    local response = process(request)  -- 5ms
    
    -- 问题：GC触发
    -- 导致总响应时间激增
    local elapsed = (os.clock() - start) * 1000
    
    if elapsed > 10 then
        print("响应超时:", elapsed, "ms")  -- 经常发生 ❌
    end
    
    return response
end
```

```lua
-- 场景3：内存占用过高
-- 限制：嵌入式设备只有128MB RAM

function load_data()
    local data_cache = {}
    
    for i = 1, 10000 do
        -- 每个对象约10KB
        data_cache[i] = load_object(i)
    end
    
    -- 问题：内存占用100MB
    -- 接近限制！⚠️
    print("内存使用:", collectgarbage("count"), "KB")
    
    -- 需要更积极的GC策略
end
```

### 1.2 GC调优的目标

#### 三大核心目标

```
GC调优的平衡三角：

        ⏱️ 低延迟
         /\
        /  \
       /    \
      /  ⚖️  \
     /  平衡  \
    /__________\
 💾 低内存    🚀 高吞吐量

不可能三角：
• 低延迟 + 低内存 → 吞吐量下降
• 低延迟 + 高吞吐量 → 内存占用高
• 低内存 + 高吞吐量 → 延迟增加

调优目标：根据应用特点找到最佳平衡点 ✅
```

#### 目标量化

```lua
-- 量化GC性能目标

-- 目标1：延迟（Latency）
local latency_requirements = {
    game_frame = 16.67,      -- 60 FPS
    web_request = 100,       -- HTTP响应
    realtime_audio = 5,      -- 音频处理
    batch_task = 1000        -- 批处理任务
}

-- 目标2：内存（Memory）
local memory_limits = {
    mobile = 50 * 1024,      -- 50MB
    embedded = 10 * 1024,    -- 10MB
    desktop = 500 * 1024,    -- 500MB
    server = 2 * 1024 * 1024 -- 2GB
}

-- 目标3：吞吐量（Throughput）
local throughput_targets = {
    requests_per_sec = 10000,    -- QPS
    objects_per_sec = 100000,    -- 对象创建速率
    gc_overhead = 0.05           -- GC开销 < 5%
}

-- 根据应用选择优先级
function set_gc_priority(app_type)
    if app_type == "game" then
        return "低延迟优先"
    elseif app_type == "embedded" then
        return "低内存优先"
    elseif app_type == "server" then
        return "高吞吐量优先"
    end
end
```

### 1.3 调优的基本原则

#### 原则1：先测量，后优化

```lua
-- ❌ 错误：盲目调整参数
collectgarbage("setpause", 50)   -- 随便设置
collectgarbage("setstepmul", 500) -- 没有依据

-- ✅ 正确：基于测量的调优
local Profiler = {}

function Profiler.measure_baseline()
    collectgarbage("collect")  -- 清空基线
    
    local start_mem = collectgarbage("count")
    local start_time = os.clock()
    
    -- 运行典型负载
    run_typical_workload()
    
    collectgarbage("collect")
    local end_time = os.clock()
    local end_mem = collectgarbage("count")
    
    return {
        duration = end_time - start_time,
        memory_peak = end_mem,
        memory_growth = end_mem - start_mem,
        gc_count = get_gc_count()
    }
end

-- 基于测量结果调整
local baseline = Profiler.measure_baseline()
print("基线性能:", baseline.duration, "秒")
print("内存峰值:", baseline.memory_peak, "KB")

-- 根据数据决定调优方向 ✅
```

#### 原则2：渐进式调整

```lua
-- ❌ 错误：激进调整
collectgarbage("setpause", 1000)  -- 从200跳到1000

-- ✅ 正确：小步调整
local function tune_gcpause_gradually()
    local current = 200  -- 默认值
    local step = 50      -- 每次调整50
    local target = 400   -- 目标值
    
    while current < target do
        current = current + step
        collectgarbage("setpause", current)
        
        -- 测试性能
        local perf = measure_performance()
        print(string.format("gcpause=%d, 性能=%s", 
            current, perf))
        
        -- 如果性能下降，回退
        if perf.worse_than_before then
            current = current - step
            break
        end
    end
    
    print("最优gcpause:", current)
end
```

#### 原则3：考虑应用特点

```lua
-- 不同应用的调优策略

-- 应用1：实时游戏
local function tune_for_game()
    -- 优先级：低延迟 > 内存 > 吞吐量
    collectgarbage("setpause", 200)    -- 适中的GC频率
    collectgarbage("setstepmul", 100)  -- 小步执行，减少停顿
    
    -- 在帧间隙手动触发GC
    function on_frame_end()
        collectgarbage("step", 1000)  -- 控制执行量
    end
end

-- 应用2：嵌入式设备
local function tune_for_embedded()
    -- 优先级：低内存 > 延迟 > 吞吐量
    collectgarbage("setpause", 100)    -- 频繁GC，保持低内存
    collectgarbage("setstepmul", 200)  -- 标准步进
end

-- 应用3：批处理服务器
local function tune_for_batch()
    -- 优先级：高吞吐量 > 内存 > 延迟
    collectgarbage("setpause", 400)    -- 减少GC频率
    collectgarbage("setstepmul", 400)  -- 大步执行，提高效率
end
```

### 1.4 调优的范围

#### GC参数总览

```lua
-- Lua 5.1.5 提供的GC控制接口

-- 1. gcpause - GC暂停倍数
collectgarbage("setpause", value)
-- 范围：0 - 无穷大
-- 默认：200
-- 作用：控制GC触发频率

-- 2. gcstepmul - GC步进倍数
collectgarbage("setstepmul", value)
-- 范围：0 - 无穷大
-- 默认：200
-- 作用：控制每步GC工作量

-- 3. 手动控制
collectgarbage("collect")    -- 完整GC
collectgarbage("stop")        -- 停止自动GC
collectgarbage("restart")     -- 重启自动GC
collectgarbage("step", size)  -- 执行size KB的GC工作
collectgarbage("count")       -- 获取内存使用量

-- 4. 代码层面优化
-- • 对象复用
-- • 避免临时对象
-- • 弱引用表
-- • 显式清理
```

---

## 2. GC参数详解

### 2.1 gcpause - 暂停倍数

#### 工作原理

```c
// lgc.c - GC阈值计算
#define setthreshold(g) \
    (g->GCthreshold = (g->estimate / 100) * g->gcpause)

/**
 * GC触发条件：
 * 
 * 当前内存 >= GC阈值
 * GC阈值 = (上次GC后内存估算 / 100) × gcpause
 * 
 * 示例：
 * 上次GC后：1000 KB
 * gcpause = 200
 * GC阈值 = (1000 / 100) × 200 = 2000 KB
 * 
 * 即：内存翻倍时触发下次GC
 */
```

#### 值的影响

```lua
-- gcpause值对GC行为的影响

-- gcpause = 100（激进）
-- GC阈值 = 当前内存 × 1.0
-- 内存增长即触发GC
collectgarbage("setpause", 100)

local test_data = {}
for i = 1, 1000 do
    test_data[i] = {size = 1024}  -- 每次循环都可能触发GC
end
-- 特点：
-- ✅ 内存占用低
-- ❌ GC非常频繁
-- ❌ 吞吐量下降

-- gcpause = 200（默认）
-- GC阈值 = 当前内存 × 2.0
-- 内存翻倍时触发GC
collectgarbage("setpause", 200)

-- 特点：
-- ⚖️ 平衡的选择
-- ✅ 适中的内存使用
-- ✅ 适中的GC频率

-- gcpause = 400（保守）
-- GC阈值 = 当前内存 × 4.0
-- 内存增长4倍才触发GC
collectgarbage("setpause", 400)

-- 特点：
-- ✅ GC频率低
-- ✅ 吞吐量高
-- ❌ 内存峰值高

-- gcpause = 1000（极端）
-- GC阈值 = 当前内存 × 10.0
-- 几乎不自动触发GC
collectgarbage("setpause", 1000)

-- 特点：
-- ✅ 最高吞吐量
-- ❌ 内存占用极高
-- ⚠️ 可能内存溢出
```

#### 选择指南

```lua
-- gcpause选择决策树

function choose_gcpause(requirements)
    if requirements.memory_critical then
        -- 内存受限设备
        return 100  -- 频繁GC，保持低内存
        
    elseif requirements.latency_critical then
        -- 低延迟要求
        if requirements.predictable then
            return 150  -- 较频繁的小GC
        else
            return 200  -- 默认值
        end
        
    elseif requirements.throughput_critical then
        -- 高吞吐量要求
        if requirements.has_idle_time then
            return 300  -- 在空闲时GC
        else
            return 400  -- 减少GC频率
        end
        
    else
        -- 通用场景
        return 200  -- 默认值
    end
end

-- 使用示例
local game_pause = choose_gcpause({
    memory_critical = false,
    latency_critical = true,
    throughput_critical = false,
    predictable = true
})
collectgarbage("setpause", game_pause)  -- 150
```

### 2.2 gcstepmul - 步进倍数

#### 工作原理

```c
// lgc.c - 每步GC工作量计算
#define GCSTEPSIZE 1024u  // 基础步长：1KB

void luaC_step(lua_State *L) {
    global_State *g = G(L);
    
    // 计算本次步进的工作量
    lu_mem lim = (GCSTEPSIZE / 100) * g->gcstepmul;
    
    // 工作量 = (1024 / 100) × gcstepmul
    //        = 10.24 × gcstepmul (字节)
    
    // 示例：
    // gcstepmul = 200 (默认)
    // 工作量 = 10.24 × 200 = 2048 字节 ≈ 2KB
    
    // 执行GC工作
    // ...
}
```

#### 值的影响

```lua
-- gcstepmul值对GC行为的影响

-- gcstepmul = 50（细粒度）
-- 每步工作量 = 10.24 × 50 = 512 字节
collectgarbage("setstepmul", 50)

function test_fine_grained()
    local start = os.clock()
    
    for i = 1, 10000 do
        local obj = {data = string.rep("x", 100)}
        -- 每次分配都可能触发小步GC
        -- 单次停顿时间短
    end
    
    local elapsed = os.clock() - start
    print("细粒度GC:", elapsed, "秒")
end

-- 特点：
-- ✅ 每次停顿时间短
-- ✅ 延迟可预测
-- ❌ GC总开销大（频繁调用）

-- gcstepmul = 200（默认）
-- 每步工作量 = 10.24 × 200 = 2048 字节
collectgarbage("setstepmul", 200)

-- 特点：
-- ⚖️ 平衡的选择
-- ✅ 适中的停顿时间
-- ✅ 适中的总开销

-- gcstepmul = 400（粗粒度）
-- 每步工作量 = 10.24 × 400 = 4096 字节
collectgarbage("setstepmul", 400)

-- 特点：
-- ✅ GC总开销小
-- ✅ 吞吐量高
-- ❌ 单次停顿时间长

-- gcstepmul = 1000（极端）
-- 每步工作量 = 10.24 × 1000 = 10240 字节
collectgarbage("setstepmul", 1000)

-- 特点：
-- ✅ 最低GC开销
-- ❌ 停顿时间不可预测
-- ⚠️ 可能导致掉帧/超时
```

#### 与gcpause的协同

```lua
-- gcpause和gcstepmul的组合策略

-- 策略1：频繁小步（低延迟）
collectgarbage("setpause", 150)    -- 较频繁触发
collectgarbage("setstepmul", 100)  -- 小步执行

-- 效果：
-- • GC频繁触发，内存保持低位
-- • 每次GC工作量小，停顿短
-- • 适合：实时游戏、音视频处理

-- 策略2：稀疏大步（高吞吐）
collectgarbage("setpause", 400)    -- 稀疏触发
collectgarbage("setstepmul", 400)  -- 大步执行

-- 效果：
-- • GC不常触发，减少中断
-- • 每次GC工作量大，效率高
-- • 适合：批处理、服务器

-- 策略3：平衡模式（通用）
collectgarbage("setpause", 200)    -- 默认触发
collectgarbage("setstepmul", 200)  -- 默认步长

-- 效果：
-- • 各方面平衡
-- • 适合：大多数应用

-- 策略4：自适应调整
local function adaptive_gc_tuning()
    local frame_times = {}
    local window_size = 60  -- 监控60帧
    
    return function()
        local frame_start = os.clock()
        
        -- 执行帧逻辑
        game_update()
        
        local frame_time = os.clock() - frame_start
        table.insert(frame_times, frame_time)
        
        if #frame_times > window_size then
            table.remove(frame_times, 1)
        end
        
        -- 计算平均帧时间
        local avg_time = 0
        for _, t in ipairs(frame_times) do
            avg_time = avg_time + t
        end
        avg_time = avg_time / #frame_times
        
        -- 自适应调整
        if avg_time > 0.0167 then  -- > 16.67ms
            -- 帧时间过长，减小步长
            local current = collectgarbage("setstepmul", -1)
            collectgarbage("setstepmul", math.max(50, current - 10))
            print("降低gcstepmul")
        elseif avg_time < 0.0100 then  -- < 10ms
            -- 帧时间充裕，增加步长
            local current = collectgarbage("setstepmul", -1)
            collectgarbage("setstepmul", math.min(400, current + 10))
            print("提高gcstepmul")
        end
    end
end
```

### 2.3 手动GC控制

#### 完整GC

```lua
-- collectgarbage("collect") - 完整GC周期

function perform_full_gc()
    local before = collectgarbage("count")
    local start = os.clock()
    
    collectgarbage("collect")
    
    local elapsed = os.clock() - start
    local after = collectgarbage("count")
    local freed = before - after
    
    print(string.format(
        "完整GC: 释放%d KB, 耗时%.3f ms",
        freed, elapsed * 1000
    ))
end

-- 使用场景

-- 场景1：关卡切换
function load_new_level(level_id)
    -- 卸载旧关卡
    unload_current_level()
    
    -- 完整GC，清理旧资源
    collectgarbage("collect")
    
    -- 加载新关卡
    load_level(level_id)
end

-- 场景2：定期维护
local last_full_gc = os.time()

function periodic_maintenance()
    local now = os.time()
    if now - last_full_gc > 300 then  -- 5分钟
        collectgarbage("collect")
        last_full_gc = now
        print("定期完整GC完成")
    end
end

-- 场景3：低负载时段
function on_idle()
    -- 检测是否空闲
    if is_system_idle() then
        collectgarbage("collect")
    end
end
```

#### 增量GC步进

```lua
-- collectgarbage("step", size) - 执行指定量的GC工作

-- 策略1：固定步长
function fixed_step_gc()
    -- 每帧执行1KB的GC工作
    collectgarbage("step", 1)
end

-- 策略2：基于帧时间
function adaptive_step_gc(target_frame_time)
    local frame_start = os.clock()
    
    -- 执行游戏逻辑
    game_update()
    game_render()
    
    -- 计算剩余时间
    local elapsed = os.clock() - frame_start
    local remaining = target_frame_time - elapsed
    
    if remaining > 0.001 then  -- 剩余 > 1ms
        -- 利用剩余时间执行GC
        local gc_budget = remaining * 1000  -- 转换为KB
        collectgarbage("step", math.floor(gc_budget))
    end
end

-- 策略3：基于内存压力
function pressure_based_gc()
    local current_mem = collectgarbage("count")
    local threshold = 50 * 1024  -- 50MB
    
    if current_mem > threshold * 0.9 then
        -- 接近阈值，激进GC
        collectgarbage("step", 10)
    elseif current_mem > threshold * 0.7 then
        -- 中等压力
        collectgarbage("step", 5)
    else
        -- 低压力
        collectgarbage("step", 1)
    end
end
```

#### 停止和重启

```lua
-- 手动控制GC开关

-- 场景1：关键路径禁用GC
function critical_section()
    collectgarbage("stop")  -- 停止自动GC
    
    -- 执行关键代码
    local result = perform_critical_task()
    
    collectgarbage("restart")  -- 恢复自动GC
    collectgarbage("step", 10)  -- 补偿性GC
    
    return result
end

-- 场景2：批量操作
function batch_insert(items)
    collectgarbage("stop")
    
    local db = open_database()
    for _, item in ipairs(items) do
        db:insert(item)
    end
    db:close()
    
    collectgarbage("restart")
    collectgarbage("collect")  -- 批量完成后清理
end

-- 场景3：自定义GC调度
local GCScheduler = {}

function GCScheduler.init()
    collectgarbage("stop")
    
    -- 在特定时机执行GC
    timer.create(1/60, function()  -- 60 FPS
        -- 帧开始前执行GC
        collectgarbage("step", 2)
    end)
end
```

### 2.4 内存查询

#### 基本查询

```lua
-- collectgarbage("count") - 获取当前内存使用

function get_memory_stats()
    local mem_kb = collectgarbage("count")
    local mem_mb = mem_kb / 1024
    
    return {
        kilobytes = mem_kb,
        megabytes = mem_mb,
        formatted = string.format("%.2f MB", mem_mb)
    }
end

-- 使用
local stats = get_memory_stats()
print("内存使用:", stats.formatted)
```

#### 增长率监控

```lua
-- 内存增长率分析
local MemoryMonitor = {}

function MemoryMonitor.new()
    local self = {
        samples = {},
        max_samples = 100
    }
    
    function self.record()
        local sample = {
            time = os.clock(),
            memory = collectgarbage("count")
        }
        
        table.insert(self.samples, sample)
        
        if #self.samples > self.max_samples then
            table.remove(self.samples, 1)
        end
    end
    
    function self.get_growth_rate()
        if #self.samples < 2 then
            return 0
        end
        
        local first = self.samples[1]
        local last = self.samples[#self.samples]
        
        local time_delta = last.time - first.time
        local mem_delta = last.memory - first.memory
        
        -- KB/秒
        return mem_delta / time_delta
    end
    
    function self.predict_oom(limit_kb)
        local rate = self.get_growth_rate()
        if rate <= 0 then
            return math.huge  -- 不会OOM
        end
        
        local current = collectgarbage("count")
        local remaining = limit_kb - current
        
        -- 预计多少秒后OOM
        return remaining / rate
    end
    
    return self
end

-- 使用
local monitor = MemoryMonitor.new()

-- 定期记录
timer.every(1.0, function()
    monitor.record()
    
    local rate = monitor.get_growth_rate()
    print(string.format("内存增长率: %.2f KB/s", rate))
    
    local oom_time = monitor.predict_oom(100 * 1024)  -- 100MB限制
    if oom_time < 60 then
        print(string.format("警告：预计%d秒后OOM", oom_time))
    end
end)
```

---

## 3. 性能诊断方法

### 3.1 识别性能问题

#### GC相关的性能症状

```lua
-- 症状检测工具
local PerformanceDetector = {}

function PerformanceDetector.detect_issues()
    local issues = {}
    
    -- 症状1：频繁掉帧
    local frame_times = measure_frame_times(60)
    local dropped_frames = 0
    
    for _, time in ipairs(frame_times) do
        if time > 16.67 then
            dropped_frames = dropped_frames + 1
        end
    end
    
    if dropped_frames > 6 then  -- 超过10%
        table.insert(issues, {
            type = "frequent_frame_drops",
            severity = "high",
            description = string.format(
                "%d/60 帧超时 (%.1f%%)",
                dropped_frames,
                dropped_frames / 60 * 100
            )
        })
    end
    
    -- 症状2：内存持续增长
    local growth_rate = get_memory_growth_rate()
    if growth_rate > 100 then  -- > 100 KB/s
        table.insert(issues, {
            type = "memory_leak",
            severity = "critical",
            description = string.format(
                "内存泄漏：增长率 %.2f KB/s",
                growth_rate
            )
        })
    end
    
    -- 症状3：GC时间占比过高
    local gc_overhead = measure_gc_overhead()
    if gc_overhead > 0.10 then  -- > 10%
        table.insert(issues, {
            type = "excessive_gc",
            severity = "medium",
            description = string.format(
                "GC开销过高：%.1f%%",
                gc_overhead * 100
            )
        })
    end
    
    -- 症状4：内存峰值过高
    local peak_memory = get_peak_memory()
    local limit = get_memory_limit()
    
    if peak_memory > limit * 0.9 then
        table.insert(issues, {
            type = "memory_pressure",
            severity = "high",
            description = string.format(
                "内存使用接近限制：%.1f%%",
                peak_memory / limit * 100
            )
        })
    end
    
    return issues
end

-- 使用
local issues = PerformanceDetector.detect_issues()
for _, issue in ipairs(issues) do
    print(string.format(
        "[%s] %s: %s",
        issue.severity,
        issue.type,
        issue.description
    ))
end
```

### 3.2 性能分析工具

#### 简易性能分析器

```lua
-- 内置性能分析器
local SimpleProfiler = {}

function SimpleProfiler.new()
    local self = {
        samples = {},
        active = true
    }
    
    function self.sample()
        if not self.active then return end
        
        local sample = {
            timestamp = os.clock(),
            memory = collectgarbage("count"),
            
            -- 尝试获取GC状态（Lua 5.1不直接支持，需估算）
            gc_phase = estimate_gc_phase()
        }
        
        table.insert(self.samples, sample)
    end
    
    function self.analyze()
        if #self.samples < 2 then
            return nil
        end
        
        local total_time = self.samples[#self.samples].timestamp - 
                          self.samples[1].timestamp
        
        local memory_max = 0
        local memory_min = math.huge
        local memory_total = 0
        
        for _, sample in ipairs(self.samples) do
            memory_max = math.max(memory_max, sample.memory)
            memory_min = math.min(memory_min, sample.memory)
            memory_total = memory_total + sample.memory
        end
        
        local memory_avg = memory_total / #self.samples
        
        return {
            duration = total_time,
            samples = #self.samples,
            memory = {
                min = memory_min,
                max = memory_max,
                avg = memory_avg,
                range = memory_max - memory_min
            }
        }
    end
    
    function self.report()
        local result = self.analyze()
        if not result then
            print("样本不足")
            return
        end
        
        print(string.format([[
性能分析报告：
  采样时长: %.2f 秒
  采样次数: %d 次
  内存统计:
    最小值: %.2f MB
    最大值: %.2f MB
    平均值: %.2f MB
    波动范围: %.2f MB
]], 
            result.duration,
            result.samples,
            result.memory.min / 1024,
            result.memory.max / 1024,
            result.memory.avg / 1024,
            result.memory.range / 1024
        ))
    end
    
    return self
end

-- 使用示例
local profiler = SimpleProfiler.new()

-- 持续采样
timer.every(0.1, function()
    profiler.sample()
end)

-- 5秒后生成报告
timer.after(5.0, function()
    profiler.report()
end)
```

### 3.3 基准测试

#### 标准基准测试套件

```lua
-- GC基准测试
local GCBenchmark = {}

function GCBenchmark.test_allocation_rate()
    collectgarbage("collect")
    collectgarbage("stop")
    
    local start = os.clock()
    local start_mem = collectgarbage("count")
    
    -- 分配大量对象
    local objects = {}
    for i = 1, 10000 do
        objects[i] = {
            id = i,
            data = string.rep("x", 100)
        }
    end
    
    local end_mem = collectgarbage("count")
    local elapsed = os.clock() - start
    
    collectgarbage("restart")
    
    local allocated = end_mem - start_mem
    local rate = allocated / elapsed
    
    return {
        allocated_kb = allocated,
        duration_sec = elapsed,
        rate_kb_per_sec = rate
    }
end

function GCBenchmark.test_gc_throughput(pause, stepmul)
    collectgarbage("setpause", pause)
    collectgarbage("setstepmul", stepmul)
    collectgarbage("collect")
    
    local start = os.clock()
    
    -- 工作负载
    for i = 1, 1000 do
        local temp = {}
        for j = 1, 100 do
            temp[j] = {value = math.random()}
        end
    end
    
    local elapsed = os.clock() - start
    
    return {
        duration = elapsed,
        throughput = 1000 / elapsed  -- ops/sec
    }
end

function GCBenchmark.test_gc_latency(pause, stepmul)
    collectgarbage("setpause", pause)
    collectgarbage("setstepmul", stepmul)
    
    local latencies = {}
    
    for i = 1, 100 do
        collectgarbage("collect")
        
        local start = os.clock()
        
        -- 模拟工作
        local obj = {}
        for j = 1, 1000 do
            obj[j] = {data = j}
        end
        
        local elapsed = (os.clock() - start) * 1000  -- ms
        table.insert(latencies, elapsed)
    end
    
    -- 统计
    table.sort(latencies)
    local p50 = latencies[math.floor(#latencies * 0.50)]
    local p95 = latencies[math.floor(#latencies * 0.95)]
    local p99 = latencies[math.floor(#latencies * 0.99)]
    local max = latencies[#latencies]
    
    return {
        p50 = p50,
        p95 = p95,
        p99 = p99,
        max = max
    }
end

-- 运行完整基准测试
function GCBenchmark.run_full_suite()
    print("=== GC 基准测试套件 ===\n")
    
    -- 测试1：分配速率
    print("测试1：对象分配速率")
    local alloc = GCBenchmark.test_allocation_rate()
    print(string.format(
        "  分配: %.2f MB\n  速率: %.2f MB/s\n",
        alloc.allocated_kb / 1024,
        alloc.rate_kb_per_sec / 1024
    ))
    
    -- 测试2：吞吐量（不同参数）
    print("测试2：GC吞吐量")
    local configs = {
        {pause = 100, stepmul = 100},
        {pause = 200, stepmul = 200},
        {pause = 400, stepmul = 400}
    }
    
    for _, cfg in ipairs(configs) do
        local result = GCBenchmark.test_gc_throughput(
            cfg.pause, cfg.stepmul
        )
        print(string.format(
            "  pause=%d, stepmul=%d: %.2f ops/s",
            cfg.pause, cfg.stepmul, result.throughput
        ))
    end
    print()
    
    -- 测试3：延迟
    print("测试3：GC延迟分布")
    local latency = GCBenchmark.test_gc_latency(200, 200)
    print(string.format(
        "  P50: %.2f ms\n  P95: %.2f ms\n  P99: %.2f ms\n  Max: %.2f ms\n",
        latency.p50, latency.p95, latency.p99, latency.max
    ))
end

-- 运行
GCBenchmark.run_full_suite()
```

---

## 4. 典型场景调优

### 4.1 实时游戏优化

#### 问题分析

```lua
-- 游戏性能需求
local GAME_REQUIREMENTS = {
    target_fps = 60,
    frame_budget = 16.67,  -- ms
    max_gc_pause = 2.0,    -- ms
    memory_limit = 100 * 1024  -- 100 MB
}

-- 问题检测
function detect_game_issues()
    local frame_times = measure_frames(120)  -- 2秒样本
    
    local issues = {}
    local total_over = 0
    local max_over = 0
    
    for _, time in ipairs(frame_times) do
        local over = time - GAME_REQUIREMENTS.frame_budget
        if over > 0 then
            total_over = total_over + over
            max_over = math.max(max_over, over)
            table.insert(issues, {
                frame = _,
                time = time,
                overage = over
            })
        end
    end
    
    print(string.format(
        "性能分析：\n" ..
        "  超时帧数: %d/%d (%.1f%%)\n" ..
        "  总超时: %.2f ms\n" ..
        "  最大超时: %.2f ms",
        #issues, #frame_times,
        #issues / #frame_times * 100,
        total_over,
        max_over
    ))
    
    return issues
end
```

#### 优化策略

```lua
-- 游戏GC优化方案
local GameGCTuner = {}

function GameGCTuner.apply_optimization()
    -- 策略1：小步快走
    collectgarbage("setpause", 150)
    collectgarbage("setstepmul", 100)
    
    print("应用游戏GC优化：")
    print("  gcpause = 150（较频繁触发）")
    print("  gcstepmul = 100（小步执行）")
end

function GameGCTuner.manual_scheduling()
    -- 策略2：手动调度GC
    collectgarbage("stop")
    
    local gc_budget = 2.0  -- 每帧2ms预算
    
    function on_frame_update()
        local frame_start = os.clock()
        
        -- 游戏逻辑
        update_game_logic()      -- ~8ms
        render_scene()           -- ~5ms
        
        -- 使用剩余时间执行GC
        local used = (os.clock() - frame_start) * 1000
        local remaining = GAME_REQUIREMENTS.frame_budget - used
        
        if remaining > gc_budget then
            local gc_start = os.clock()
            collectgarbage("step", 2)  -- 执行2KB GC
            local gc_time = (os.clock() - gc_start) * 1000
            
            -- 监控GC时间
            if gc_time > gc_budget then
                print(string.format(
                    "警告：GC超预算 %.2f ms",
                    gc_time
                ))
            end
        end
    end
end

function GameGCTuner.load_time_gc()
    -- 策略3：加载时完整GC
    function on_level_load()
        print("关卡加载：执行完整GC")
        collectgarbage("collect")
        
        load_level_assets()
        
        -- 加载后再次GC
        collectgarbage("collect")
        print("关卡加载完成，内存:", 
            collectgarbage("count") / 1024, "MB")
    end
end

-- 应用优化
GameGCTuner.apply_optimization()
GameGCTuner.manual_scheduling()
```

### 4.2 服务器应用优化

#### 高并发场景

```lua
-- 服务器GC优化
local ServerGCTuner = {}

function ServerGCTuner.high_throughput_config()
    -- 目标：最大化吞吐量
    collectgarbage("setpause", 400)    -- 减少GC频率
    collectgarbage("setstepmul", 400)  -- 大步执行
    
    print("高吞吐量配置：")
    print("  gcpause = 400")
    print("  gcstepmul = 400")
    print("  预期效果：GC开销 < 3%")
end

function ServerGCTuner.request_based_gc()
    -- 基于请求计数触发GC
    local request_count = 0
    local gc_interval = 10000  -- 每10000请求
    
    function handle_request(req)
        request_count = request_count + 1
        
        -- 处理请求
        local response = process(req)
        
        -- 定期GC
        if request_count % gc_interval == 0 then
            collectgarbage("step", 10)
        end
        
        return response
    end
end

function ServerGCTuner.scheduled_gc()
    -- 定时GC（低峰期）
    local function is_low_traffic()
        local hour = tonumber(os.date("%H"))
        return hour >= 2 and hour <= 6  -- 凌晨2-6点
    end
    
    -- 每小时检查
    timer.every(3600, function()
        if is_low_traffic() then
            print("低峰期：执行完整GC")
            collectgarbage("collect")
        end
    end)
end

-- 应用
ServerGCTuner.high_throughput_config()
ServerGCTuner.request_based_gc()
ServerGCTuner.scheduled_gc()
```

#### 长连接服务

```lua
-- WebSocket/长连接服务器优化
local LongConnectionTuner = {}

function LongConnectionTuner.optimize()
    -- 问题：长连接导致对象存活时间长
    -- 解决：使用弱引用表管理连接
    
    local connections = {}
    setmetatable(connections, {__mode = "v"})  -- 弱值表
    
    function on_connect(conn_id, socket)
        connections[conn_id] = socket
        print("连接建立:", conn_id)
    end
    
    function on_disconnect(conn_id)
        connections[conn_id] = nil
        -- socket会被自动GC
    end
    
    -- 定期清理
    timer.every(60, function()
        local count = 0
        for _ in pairs(connections) do
            count = count + 1
        end
        print("活跃连接:", count)
        
        -- 强制GC，清理断开的连接
        collectgarbage("collect")
    end)
end
```

### 4.3 嵌入式系统优化

#### 内存受限设备

```lua
-- 嵌入式设备GC优化
local EmbeddedGCTuner = {}

function EmbeddedGCTuner.low_memory_config()
    -- 目标：最小化内存占用
    collectgarbage("setpause", 100)    -- 激进GC
    collectgarbage("setstepmul", 200)  -- 标准步长
    
    print("低内存配置：")
    print("  gcpause = 100（频繁GC）")
    print("  gcstepmul = 200")
    print("  预期：内存峰值降低50%")
end

function EmbeddedGCTuner.memory_pressure_monitor()
    local MEMORY_LIMIT = 10 * 1024  -- 10MB限制
    
    function check_memory_pressure()
        local current = collectgarbage("count")
        local usage = current / MEMORY_LIMIT
        
        if usage > 0.9 then
            -- 严重压力：立即完整GC
            print("内存告急！执行紧急GC")
            collectgarbage("collect")
            
        elseif usage > 0.7 then
            -- 中度压力：增加GC频率
            collectgarbage("setpause", 80)
            collectgarbage("step", 5)
            
        elseif usage < 0.5 then
            -- 低压力：恢复正常
            collectgarbage("setpause", 100)
        end
        
        print(string.format(
            "内存使用: %.1f%% (%.2f MB / %.2f MB)",
            usage * 100,
            current / 1024,
            MEMORY_LIMIT / 1024
        ))
    end
    
    -- 定期检查
    timer.every(1.0, check_memory_pressure)
end

function EmbeddedGCTuner.object_pooling()
    -- 对象池，减少GC压力
    local pools = {}
    
    function create_pool(name, factory, max_size)
        local pool = {
            free = {},
            factory = factory,
            max_size = max_size or 100
        }
        
        function pool.acquire()
            local obj = table.remove(pool.free)
            if not obj then
                obj = pool.factory()
            end
            return obj
        end
        
        function pool.release(obj)
            if #pool.free < pool.max_size then
                -- 清理对象状态
                for k in pairs(obj) do
                    obj[k] = nil
                end
                table.insert(pool.free, obj)
            end
        end
        
        pools[name] = pool
        return pool
    end
    
    -- 示例：消息池
    local msg_pool = create_pool("message", function()
        return {}
    end, 200)
    
    function send_message(type, data)
        local msg = msg_pool.acquire()
        msg.type = type
        msg.data = data
        
        transmit(msg)
        
        msg_pool.release(msg)  -- 复用
    end
end

-- 应用
EmbeddedGCTuner.low_memory_config()
EmbeddedGCTuner.memory_pressure_monitor()
EmbeddedGCTuner.object_pooling()
```

### 4.4 批处理任务优化

#### 数据处理

```lua
-- 批处理GC优化
local BatchGCTuner = {}

function BatchGCTuner.disable_during_processing()
    -- 处理期间禁用GC，完成后统一清理
    
    function process_large_dataset(data)
        print("开始批处理，禁用GC")
        collectgarbage("stop")
        
        local start = os.clock()
        local processed = 0
        
        for i, item in ipairs(data) do
            process_item(item)
            processed = processed + 1
            
            -- 每1000项检查一次内存
            if processed % 1000 == 0 then
                local mem = collectgarbage("count")
                if mem > 500 * 1024 then  -- 超过500MB
                    print("内存压力过大，执行GC")
                    collectgarbage("collect")
                end
            end
        end
        
        local elapsed = os.clock() - start
        
        -- 处理完成，执行完整GC
        print("批处理完成，执行GC")
        collectgarbage("restart")
        collectgarbage("collect")
        
        print(string.format(
            "处理%d项，耗时%.2f秒",
            processed, elapsed
        ))
    end
end

function BatchGCTuner.chunked_processing()
    -- 分块处理，每块后GC
    
    function process_in_chunks(data, chunk_size)
        chunk_size = chunk_size or 1000
        
        local total = #data
        local chunks = math.ceil(total / chunk_size)
        
        for i = 1, chunks do
            local start_idx = (i - 1) * chunk_size + 1
            local end_idx = math.min(i * chunk_size, total)
            
            -- 处理本块
            for j = start_idx, end_idx do
                process_item(data[j])
            end
            
            -- 块间GC
            collectgarbage("collect")
            
            print(string.format(
                "完成块 %d/%d (%.1f%%)",
                i, chunks, i / chunks * 100
            ))
        end
    end
end

-- 应用
function main()
    local data = load_large_dataset()
    
    -- 方式1：完全禁用
    -- BatchGCTuner.disable_during_processing(data)
    
    -- 方式2：分块处理（推荐）
    BatchGCTuner.chunked_processing(data, 5000)
end
```

---

## 5. 调优流程

### 5.1 建立基线

#### 基线测量

```lua
-- 完整的基线测量工具
local BaselineMeasurement = {}

function BaselineMeasurement.capture()
    print("=== 开始基线测量 ===\n")
    
    -- 1. 环境信息
    print("环境信息：")
    print("  Lua版本:", _VERSION)
    print("  平台:", get_platform())
    print()
    
    -- 2. 当前GC配置
    local pause = collectgarbage("setpause", -1)
    local stepmul = collectgarbage("setstepmul", -1)
    
    print("GC配置：")
    print("  gcpause:", pause)
    print("  gcstepmul:", stepmul)
    print()
    
    -- 3. 内存基线
    collectgarbage("collect")
    local base_memory = collectgarbage("count")
    
    print("内存基线：")
    print(string.format("  基础内存: %.2f MB", base_memory / 1024))
    print()
    
    -- 4. 运行典型负载
    print("执行典型负载...")
    local start_time = os.clock()
    
    run_typical_workload()
    
    local elapsed = os.clock() - start_time
    local peak_memory = collectgarbage("count")
    
    print()
    print("负载测试结果：")
    print(string.format("  执行时间: %.2f 秒", elapsed))
    print(string.format("  内存峰值: %.2f MB", peak_memory / 1024))
    print(string.format("  内存增长: %.2f MB", 
        (peak_memory - base_memory) / 1024))
    print()
    
    -- 5. GC性能测试
    print("GC性能测试...")
    local gc_times = {}
    
    for i = 1, 10 do
        local gc_start = os.clock()
        collectgarbage("collect")
        local gc_time = (os.clock() - gc_start) * 1000
        table.insert(gc_times, gc_time)
    end
    
    table.sort(gc_times)
    local avg_gc = 0
    for _, t in ipairs(gc_times) do
        avg_gc = avg_gc + t
    end
    avg_gc = avg_gc / #gc_times
    
    print("GC时间统计：")
    print(string.format("  平均: %.2f ms", avg_gc))
    print(string.format("  最小: %.2f ms", gc_times[1]))
    print(string.format("  最大: %.2f ms", gc_times[#gc_times]))
    print(string.format("  中位数: %.2f ms", 
        gc_times[math.floor(#gc_times / 2)]))
    print()
    
    -- 6. 保存基线
    local baseline = {
        timestamp = os.time(),
        config = {
            pause = pause,
            stepmul = stepmul
        },
        memory = {
            base = base_memory,
            peak = peak_memory,
            growth = peak_memory - base_memory
        },
        performance = {
            workload_time = elapsed,
            gc_avg = avg_gc,
            gc_min = gc_times[1],
            gc_max = gc_times[#gc_times]
        }
    }
    
    save_baseline(baseline)
    
    print("=== 基线测量完成 ===")
    return baseline
end

function BaselineMeasurement.compare(baseline, current)
    print("=== 性能对比 ===\n")
    
    -- 内存对比
    local mem_diff = current.memory.peak - baseline.memory.peak
    local mem_percent = mem_diff / baseline.memory.peak * 100
    
    print("内存变化：")
    print(string.format("  基线峰值: %.2f MB", 
        baseline.memory.peak / 1024))
    print(string.format("  当前峰值: %.2f MB", 
        current.memory.peak / 1024))
    print(string.format("  变化: %+.2f MB (%+.1f%%)",
        mem_diff / 1024, mem_percent))
    
    if mem_percent < -10 then
        print("  评价: ✅ 显著改善")
    elseif mem_percent > 10 then
        print("  评价: ❌ 显著恶化")
    else
        print("  评价: ➖ 基本持平")
    end
    print()
    
    -- 性能对比
    local time_diff = current.performance.workload_time - 
                     baseline.performance.workload_time
    local time_percent = time_diff / baseline.performance.workload_time * 100
    
    print("执行时间变化：")
    print(string.format("  基线: %.2f 秒", 
        baseline.performance.workload_time))
    print(string.format("  当前: %.2f 秒", 
        current.performance.workload_time))
    print(string.format("  变化: %+.2f 秒 (%+.1f%%)",
        time_diff, time_percent))
    
    if time_percent < -5 then
        print("  评价: ✅ 性能提升")
    elseif time_percent > 5 then
        print("  评价: ❌ 性能下降")
    else
        print("  评价: ➖ 基本持平")
    end
    print()
    
    -- GC时间对比
    local gc_diff = current.performance.gc_avg - 
                   baseline.performance.gc_avg
    local gc_percent = gc_diff / baseline.performance.gc_avg * 100
    
    print("GC时间变化：")
    print(string.format("  基线平均: %.2f ms", 
        baseline.performance.gc_avg))
    print(string.format("  当前平均: %.2f ms", 
        current.performance.gc_avg))
    print(string.format("  变化: %+.2f ms (%+.1f%%)",
        gc_diff, gc_percent))
    print()
end

-- 使用流程
function tuning_workflow()
    -- 步骤1：捕获基线
    local baseline = BaselineMeasurement.capture()
    
    -- 步骤2：应用调优
    print("\n应用调优参数...")
    collectgarbage("setpause", 300)
    collectgarbage("setstepmul", 300)
    
    -- 步骤3：测量新配置
    local current = BaselineMeasurement.capture()
    
    -- 步骤4：对比结果
    BaselineMeasurement.compare(baseline, current)
end
```

### 5.2 迭代优化

#### A/B测试框架

```lua
-- GC参数A/B测试
local ABTester = {}

function ABTester.test_configurations(configs)
    local results = {}
    
    for i, config in ipairs(configs) do
        print(string.format(
            "\n=== 测试配置 %d/%d ===",
            i, #configs
        ))
        print(string.format(
            "gcpause=%d, gcstepmul=%d",
            config.pause, config.stepmul
        ))
        
        -- 应用配置
        collectgarbage("setpause", config.pause)
        collectgarbage("setstepmul", config.stepmul)
        collectgarbage("collect")
        
        -- 测试
        local result = run_benchmark()
        result.config = config
        
        table.insert(results, result)
        
        print(string.format(
            "结果: 时间=%.2fs, 内存峰值=%.2fMB, GC次数=%d",
            result.time,
            result.peak_memory / 1024,
            result.gc_count
        ))
    end
    
    -- 分析结果
    return ABTester.analyze_results(results)
end

function ABTester.analyze_results(results)
    print("\n=== 结果分析 ===\n")
    
    -- 找出最佳配置
    local best_time = results[1]
    local best_memory = results[1]
    local best_balanced = results[1]
    
    for _, result in ipairs(results) do
        if result.time < best_time.time then
            best_time = result
        end
        
        if result.peak_memory < best_memory.peak_memory then
            best_memory = result
        end
        
        -- 综合评分
        local score = function(r)
            return r.time * 0.5 + r.peak_memory / 1024 * 0.5
        end
        
        if score(result) < score(best_balanced) then
            best_balanced = result
        end
    end
    
    print("最快配置：")
    print(string.format(
        "  gcpause=%d, gcstepmul=%d, 时间=%.2fs",
        best_time.config.pause,
        best_time.config.stepmul,
        best_time.time
    ))
    print()
    
    print("最省内存配置：")
    print(string.format(
        "  gcpause=%d, gcstepmul=%d, 内存=%.2fMB",
        best_memory.config.pause,
        best_memory.config.stepmul,
        best_memory.peak_memory / 1024
    ))
    print()
    
    print("最佳平衡配置：")
    print(string.format(
        "  gcpause=%d, gcstepmul=%d",
        best_balanced.config.pause,
        best_balanced.config.stepmul
    ))
    print(string.format(
        "  时间=%.2fs, 内存=%.2fMB",
        best_balanced.time,
        best_balanced.peak_memory / 1024
    ))
    
    return {
        fastest = best_time,
        lowest_memory = best_memory,
        best_balanced = best_balanced
    }
end

-- 使用示例
function find_optimal_config()
    local configs = {
        {pause = 100, stepmul = 100},
        {pause = 150, stepmul = 150},
        {pause = 200, stepmul = 200},
        {pause = 300, stepmul = 300},
        {pause = 400, stepmul = 400}
    }
    
    local best = ABTester.test_configurations(configs)
    
    -- 应用最佳配置
    local optimal = best.best_balanced
    collectgarbage("setpause", optimal.config.pause)
    collectgarbage("setstepmul", optimal.config.stepmul)
    
    print("\n已应用最佳配置")
end
```

### 5.3 验证改进

#### 回归测试

```lua
-- 性能回归测试
local RegressionTest = {}

function RegressionTest.run(baseline, threshold)
    threshold = threshold or 0.05  -- 5%容差
    
    print("=== 性能回归测试 ===\n")
    
    local current = capture_metrics()
    local passed = true
    
    -- 测试1：执行时间
    local time_ratio = current.time / baseline.time
    local time_ok = time_ratio <= (1 + threshold)
    
    print("执行时间：")
    print(string.format("  基线: %.2fs", baseline.time))
    print(string.format("  当前: %.2fs", current.time))
    print(string.format("  变化: %+.1f%%", 
        (time_ratio - 1) * 100))
    print(time_ok and "  ✅ 通过" or "  ❌ 失败")
    print()
    
    passed = passed and time_ok
    
    -- 测试2：内存峰值
    local mem_ratio = current.peak_memory / baseline.peak_memory
    local mem_ok = mem_ratio <= (1 + threshold)
    
    print("内存峰值：")
    print(string.format("  基线: %.2fMB", 
        baseline.peak_memory / 1024))
    print(string.format("  当前: %.2fMB", 
        current.peak_memory / 1024))
    print(string.format("  变化: %+.1f%%", 
        (mem_ratio - 1) * 100))
    print(mem_ok and "  ✅ 通过" or "  ❌ 失败")
    print()
    
    passed = passed and mem_ok
    
    -- 测试3：GC开销
    local gc_ratio = current.gc_overhead / baseline.gc_overhead
    local gc_ok = gc_ratio <= (1 + threshold)
    
    print("GC开销：")
    print(string.format("  基线: %.1f%%", 
        baseline.gc_overhead * 100))
    print(string.format("  当前: %.1f%%", 
        current.gc_overhead * 100))
    print(string.format("  变化: %+.1f%%", 
        (gc_ratio - 1) * 100))
    print(gc_ok and "  ✅ 通过" or "  ❌ 失败")
    print()
    
    passed = passed and gc_ok
    
    print(string.format(
        "=== 测试%s ===",
        passed and "全部通过 ✅" or "存在失败 ❌"
    ))
    
    return passed
end
```

---

## 6. 监控与分析工具

### 6.1 实时监控

#### 内存监控器

```lua
-- 实时内存监控
local MemoryMonitor = {}

function MemoryMonitor.new(config)
    config = config or {}
    
    local self = {
        interval = config.interval or 1.0,  -- 采样间隔
        history_size = config.history_size or 100,
        history = {},
        alerts = config.alerts or {},
        running = false
    }
    
    function self.start()
        self.running = true
        
        local function monitor_loop()
            if not self.running then return end
            
            -- 采样
            local sample = {
                timestamp = os.time(),
                memory = collectgarbage("count"),
                gc_state = estimate_gc_state()
            }
            
            table.insert(self.history, sample)
            
            -- 保持历史大小
            if #self.history > self.history_size then
                table.remove(self.history, 1)
            end
            
            -- 检查告警
            self.check_alerts(sample)
            
            -- 继续监控
            timer.after(self.interval, monitor_loop)
        end
        
        monitor_loop()
        print("内存监控已启动")
    end
    
    function self.stop()
        self.running = false
        print("内存监控已停止")
    end
    
    function self.check_alerts(sample)
        for _, alert in ipairs(self.alerts) do
            if alert.condition(sample) then
                alert.action(sample)
            end
        end
    end
    
    function self.get_stats()
        if #self.history == 0 then
            return nil
        end
        
        local min_mem = math.huge
        local max_mem = 0
        local total_mem = 0
        
        for _, sample in ipairs(self.history) do
            min_mem = math.min(min_mem, sample.memory)
            max_mem = math.max(max_mem, sample.memory)
            total_mem = total_mem + sample.memory
        end
        
        local avg_mem = total_mem / #self.history
        
        return {
            samples = #self.history,
            min = min_mem,
            max = max_mem,
            avg = avg_mem,
            current = self.history[#self.history].memory
        }
    end
    
    function self.print_stats()
        local stats = self.get_stats()
        if not stats then
            print("暂无数据")
            return
        end
        
        print(string.format([[
内存统计：
  采样数: %d
  当前值: %.2f MB
  最小值: %.2f MB
  最大值: %.2f MB
  平均值: %.2f MB
  波动: %.2f MB
]], 
            stats.samples,
            stats.current / 1024,
            stats.min / 1024,
            stats.max / 1024,
            stats.avg / 1024,
            (stats.max - stats.min) / 1024
        ))
    end
    
    return self
end

-- 使用示例
local monitor = MemoryMonitor.new({
    interval = 1.0,
    history_size = 60,
    alerts = {
        {
            name = "内存告警",
            condition = function(sample)
                return sample.memory > 50 * 1024  -- 50MB
            end,
            action = function(sample)
                print(string.format(
                    "⚠️ 内存告警: %.2f MB",
                    sample.memory / 1024
                ))
            end
        },
        {
            name = "内存泄漏检测",
            condition = function(sample)
                -- 持续增长检测
                local history = monitor.history
                if #history < 10 then return false end
                
                local recent = {}
                for i = #history - 9, #history do
                    table.insert(recent, history[i].memory)
                end
                
                -- 检查是否持续增长
                local increasing = true
                for i = 2, #recent do
                    if recent[i] < recent[i-1] then
                        increasing = false
                        break
                    end
                end
                
                return increasing
            end,
            action = function(sample)
                print("🚨 检测到可能的内存泄漏！")
                print("最近10次采样持续增长")
            end
        }
    }
})

monitor.start()

-- 定期打印统计
timer.every(10, function()
    monitor.print_stats()
end)
```

### 6.2 性能分析

#### GC性能分析器

```lua
-- GC性能分析器
local GCProfiler = {}

function GCProfiler.new()
    local self = {
        gc_events = {},
        allocation_samples = {}
    }
    
    -- Hook内存分配（近似）
    local last_memory = collectgarbage("count")
    
    function self.sample_allocation()
        local current = collectgarbage("count")
        local allocated = current - last_memory
        
        if allocated > 0 then
            table.insert(self.allocation_samples, {
                timestamp = os.clock(),
                amount = allocated
            })
        end
        
        last_memory = current
    end
    
    function self.record_gc(duration, freed)
        table.insert(self.gc_events, {
            timestamp = os.clock(),
            duration = duration,
            freed = freed
        })
    end
    
    function self.get_allocation_rate()
        if #self.allocation_samples < 2 then
            return 0
        end
        
        local first = self.allocation_samples[1]
        local last = self.allocation_samples[#self.allocation_samples]
        
        local time_span = last.timestamp - first.timestamp
        local total_allocated = 0
        
        for _, sample in ipairs(self.allocation_samples) do
            total_allocated = total_allocated + sample.amount
        end
        
        return total_allocated / time_span  -- KB/s
    end
    
    function self.get_gc_stats()
        if #self.gc_events == 0 then
            return nil
        end
        
        local total_duration = 0
        local total_freed = 0
        
        for _, event in ipairs(self.gc_events) do
            total_duration = total_duration + event.duration
            total_freed = total_freed + event.freed
        end
        
        return {
            count = #self.gc_events,
            total_duration = total_duration,
            avg_duration = total_duration / #self.gc_events,
            total_freed = total_freed,
            avg_freed = total_freed / #self.gc_events
        }
    end
    
    function self.print_report()
        print("=== GC 性能报告 ===\n")
        
        -- 分配速率
        local alloc_rate = self.get_allocation_rate()
        print(string.format(
            "对象分配速率: %.2f KB/s",
            alloc_rate
        ))
        print()
        
        -- GC统计
        local gc_stats = self.get_gc_stats()
        if gc_stats then
            print(string.format(
                "GC统计:\n" ..
                "  执行次数: %d\n" ..
                "  总耗时: %.2f ms\n" ..
                "  平均耗时: %.2f ms\n" ..
                "  总回收: %.2f MB\n" ..
                "  平均回收: %.2f MB",
                gc_stats.count,
                gc_stats.total_duration * 1000,
                gc_stats.avg_duration * 1000,
                gc_stats.total_freed / 1024,
                gc_stats.avg_freed / 1024
            ))
        end
        print()
    end
    
    return self
end

-- 使用
local profiler = GCProfiler.new()

-- 定期采样分配
timer.every(0.1, function()
    profiler.sample_allocation()
end)

-- Hook GC事件
local original_collect = collectgarbage
collectgarbage = function(opt, arg)
    if opt == "collect" then
        local before = original_collect("count")
        local start = os.clock()
        
        original_collect("collect")
        
        local duration = os.clock() - start
        local after = original_collect("count")
        local freed = before - after
        
        profiler.record_gc(duration, freed)
        
        return 0
    else
        return original_collect(opt, arg)
    end
end

-- 生成报告
timer.after(60, function()
    profiler.print_report()
end)
```

### 6.3 可视化工具

#### 内存曲线图

```lua
-- ASCII内存曲线图
local MemoryChart = {}

function MemoryChart.draw(history, width, height)
    width = width or 60
    height = height or 20
    
    if #history < 2 then
        print("数据不足")
        return
    end
    
    -- 提取内存值
    local values = {}
    for _, sample in ipairs(history) do
        table.insert(values, sample.memory)
    end
    
    -- 找出范围
    local min_val = math.huge
    local max_val = 0
    
    for _, v in ipairs(values) do
        min_val = math.min(min_val, v)
        max_val = math.max(max_val, v)
    end
    
    local range = max_val - min_val
    if range == 0 then range = 1 end
    
    -- 采样到width个点
    local sampled = {}
    local step = #values / width
    
    for i = 1, width do
        local idx = math.floor((i - 1) * step) + 1
        table.insert(sampled, values[idx])
    end
    
    -- 绘制
    print(string.format(
        "内存曲线 (%.2f MB - %.2f MB)",
        min_val / 1024,
        max_val / 1024
    ))
    print()
    
    -- 从上到下绘制每一行
    for row = height, 1, -1 do
        local threshold = min_val + (row / height) * range
        local line = ""
        
        for col = 1, width do
            if sampled[col] >= threshold then
                line = line .. "█"
            else
                line = line .. " "
            end
        end
        
        -- 添加刻度
        if row == height then
            line = line .. string.format(
                " %.1f MB",
                max_val / 1024
            )
        elseif row == 1 then
            line = line .. string.format(
                " %.1f MB",
                min_val / 1024
            )
        end
        
        print(line)
    end
    
    -- 时间轴
    print(string.repeat("-", width))
    print(string.format(
        "时间跨度: %d 秒",
        history[#history].timestamp - history[1].timestamp
    ))
    print()
end

-- 使用示例
function visualize_memory()
    local monitor = MemoryMonitor.new({interval = 0.5})
    monitor.start()
    
    -- 60秒后绘制图表
    timer.after(60, function()
        monitor.stop()
        MemoryChart.draw(monitor.history, 80, 25)
    end)
end
```

---

## 7. 最佳实践

### 7.1 代码优化

#### 减少临时对象

```lua
-- ❌ 糟糕：大量临时对象
function process_items_bad(items)
    local results = {}
    
    for i, item in ipairs(items) do
        -- 每次迭代创建临时表
        local temp = {
            id = item.id,
            value = item.value * 2,
            formatted = string.format("%d: %d", item.id, item.value)
        }
        
        table.insert(results, temp.formatted)
    end
    
    return results
end

-- ✅ 优化：复用对象
function process_items_good(items)
    local results = {}
    local temp = {}  -- 复用
    
    for i, item in ipairs(items) do
        temp.id = item.id
        temp.value = item.value * 2
        
        local formatted = string.format(
            "%d: %d",
            temp.id,
            temp.value
        )
        
        table.insert(results, formatted)
    end
    
    return results
end
```

#### 字符串拼接优化

```lua
-- ❌ 糟糕：连续拼接
function build_string_bad(parts)
    local result = ""
    
    for _, part in ipairs(parts) do
        result = result .. part  -- 每次创建新字符串
    end
    
    return result
end

-- ✅ 优化：使用table.concat
function build_string_good(parts)
    return table.concat(parts)  -- 一次性拼接
end

-- 性能对比
local parts = {}
for i = 1, 1000 do
    parts[i] = tostring(i)
end

-- 测试
local start = os.clock()
build_string_bad(parts)
local bad_time = os.clock() - start

start = os.clock()
build_string_good(parts)
local good_time = os.clock() - start

print(string.format(
    "拼接1000个字符串:\n" ..
    "  差方法: %.3f ms\n" ..
    "  好方法: %.3f ms\n" ..
    "  提升: %.1fx",
    bad_time * 1000,
    good_time * 1000,
    bad_time / good_time
))
-- 输出示例：
-- 拼接1000个字符串:
--   差方法: 125.420 ms
--   好方法: 0.850 ms
--   提升: 147.6x
```

#### 弱引用表应用

```lua
-- 使用弱引用避免内存泄漏

-- 场景1：对象属性缓存
local object_properties = {}
setmetatable(object_properties, {__mode = "k"})  -- 弱键

function set_property(obj, key, value)
    if not object_properties[obj] then
        object_properties[obj] = {}
    end
    object_properties[obj][key] = value
end

function get_property(obj, key)
    local props = object_properties[obj]
    return props and props[key]
end

-- 对象被回收时，属性自动清理 ✅

-- 场景2：缓存系统
local cache = {}
setmetatable(cache, {__mode = "v"})  -- 弱值

function get_resource(id)
    if cache[id] then
        return cache[id]  -- 缓存命中
    end
    
    local resource = load_resource(id)
    cache[id] = resource
    return resource
end

-- 资源不再使用时会被GC，不会内存泄漏 ✅
```

### 7.2 配置推荐

#### 场景配置表

```lua
-- 不同场景的推荐GC配置
local GC_PRESETS = {
    -- 实时游戏（60 FPS）
    realtime_game = {
        gcpause = 150,
        gcstepmul = 100,
        description = "小步快走，减少单帧停顿",
        suitable_for = "游戏主循环、音视频处理"
    },
    
    -- 移动端游戏（30 FPS）
    mobile_game = {
        gcpause = 120,
        gcstepmul = 120,
        description = "更激进的GC，保持低内存",
        suitable_for = "移动设备、内存受限环境"
    },
    
    -- Web服务器
    web_server = {
        gcpause = 300,
        gcstepmul = 300,
        description = "高吞吐量，减少GC中断",
        suitable_for = "HTTP服务、API后端"
    },
    
    -- 批处理
    batch_processing = {
        gcpause = 400,
        gcstepmul = 400,
        description = "最大化吞吐量",
        suitable_for = "数据处理、离线计算"
    },
    
    -- 嵌入式设备
    embedded = {
        gcpause = 100,
        gcstepmul = 200,
        description = "极低内存占用",
        suitable_for = "IoT设备、单片机"
    },
    
    -- 交互式工具
    interactive = {
        gcpause = 200,
        gcstepmul = 150,
        description = "平衡延迟和吞吐",
        suitable_for = "IDE、调试工具、REPL"
    }
}

function apply_preset(preset_name)
    local preset = GC_PRESETS[preset_name]
    if not preset then
        print("未知预设:", preset_name)
        return
    end
    
    collectgarbage("setpause", preset.gcpause)
    collectgarbage("setstepmul", preset.gcstepmul)
    
    print(string.format(
        "已应用预设: %s\n" ..
        "  gcpause = %d\n" ..
        "  gcstepmul = %d\n" ..
        "  说明: %s\n" ..
        "  适用: %s",
        preset_name,
        preset.gcpause,
        preset.gcstepmul,
        preset.description,
        preset.suitable_for
    ))
end

-- 使用
apply_preset("realtime_game")
```

### 7.3 避免常见陷阱

#### 陷阱列表

```lua
-- 陷阱1：过度调优
-- ❌ 错误
collectgarbage("setpause", 50)   -- 过于激进
collectgarbage("setstepmul", 50) -- GC开销巨大

-- ✅ 正确：渐进调整，基于测量
local function tune_gradually()
    local baseline = measure_performance()
    local current_pause = 200
    
    while current_pause > 100 do
        current_pause = current_pause - 20
        collectgarbage("setpause", current_pause)
        
        local new_perf = measure_performance()
        if new_perf.worse_than(baseline) then
            current_pause = current_pause + 20
            break
        end
    end
    
    collectgarbage("setpause", current_pause)
end

-- 陷阱2：忽略内存泄漏
-- ❌ 错误：调大gcpause掩盖泄漏
collectgarbage("setpause", 1000)  -- 泄漏仍在！

-- ✅ 正确：先修复泄漏
local function fix_memory_leak()
    -- 使用弱引用表
    local cache = {}
    setmetatable(cache, {__mode = "kv"})
    
    -- 显式清理
    function cleanup()
        for k in pairs(large_table) do
            large_table[k] = nil
        end
    end
end

-- 陷阱3：在关键路径执行完整GC
-- ❌ 错误
function on_user_click()
    collectgarbage("collect")  -- 导致卡顿！
    handle_click()
end

-- ✅ 正确：延迟或分步
function on_user_click()
    handle_click()  -- 先响应用户
    
    -- 在空闲时GC
    schedule_idle_task(function()
        collectgarbage("step", 10)
    end)
end

-- 陷阱4：不监控实际效果
-- ❌ 错误：调整参数后不验证
collectgarbage("setpause", 300)
-- ...然后就不管了

-- ✅ 正确：持续监控
local monitor = MemoryMonitor.new()
monitor.start()

collectgarbage("setpause", 300)

timer.after(60, function()
    local stats = monitor.get_stats()
    if stats.max > threshold then
        print("警告：内存仍然过高")
        collectgarbage("setpause", 200)  -- 回退
    end
end)
```

---

## 8. 案例研究

### 8.1 案例1：MMORPG游戏优化

#### 问题背景

```lua
-- 某大型多人在线游戏的性能问题
local GAME_PROFILE = {
    genre = "MMORPG",
    target_fps = 60,
    avg_players_per_scene = 50,
    typical_session = "2-4 hours",
    
    problems = {
        "战斗场景频繁掉帧",
        "长时间游戏后内存占用高",
        "场景切换时卡顿明显"
    }
}

-- 基线数据
local BASELINE = {
    frame_drops_per_minute = 8,    -- 每分钟8次掉帧
    avg_frame_time = 17.5,          -- 平均17.5ms (目标16.67ms)
    memory_growth_rate = 50,        -- 50 KB/s
    scene_load_time = 3.2           -- 场景加载3.2秒
}
```

#### 诊断过程

```lua
-- 第1步：性能剖析
local Diagnosis = {}

function Diagnosis.profile_game_loop()
    local profiler = {}
    
    function profiler.run(frames)
        local results = {
            frame_times = {},
            gc_pauses = {},
            memory_samples = {}
        }
        
        for i = 1, frames do
            local frame_start = os.clock()
            
            -- 游戏逻辑
            local logic_start = os.clock()
            update_game_logic()
            local logic_time = os.clock() - logic_start
            
            -- 渲染
            local render_start = os.clock()
            render_scene()
            local render_time = os.clock() - render_start
            
            -- 记录
            local frame_time = (os.clock() - frame_start) * 1000
            table.insert(results.frame_times, frame_time)
            
            if frame_time > 16.67 then
                -- 分析超时原因
                if logic_time > 0.010 then
                    print(string.format(
                        "帧%d: 逻辑超时 %.2fms",
                        i, logic_time * 1000
                    ))
                elseif render_time > 0.008 then
                    print(string.format(
                        "帧%d: 渲染超时 %.2fms",
                        i, render_time * 1000
                    ))
                else
                    print(string.format(
                        "帧%d: GC暂停 %.2fms",
                        i, frame_time - (logic_time + render_time) * 1000
                    ))
                    table.insert(results.gc_pauses, {
                        frame = i,
                        duration = frame_time - (logic_time + render_time) * 1000
                    })
                end
            end
            
            table.insert(results.memory_samples, collectgarbage("count"))
        end
        
        return results
    end
    
    return profiler
end

-- 运行诊断
local profiler = Diagnosis.profile_game_loop()
local results = profiler.run(3600)  -- 60秒 @ 60fps

print(string.format(
    "诊断结果：\n" ..
    "  GC导致的掉帧: %d/%d (%.1f%%)",
    #results.gc_pauses,
    #results.frame_times,
    #results.gc_pauses / #results.frame_times * 100
))
-- 输出：GC导致的掉帧: 472/3600 (13.1%)
```

#### 优化方案

```lua
-- 第2步：应用多层优化

-- 优化1：调整GC参数
function Optimization1_tune_gc()
    print("=== 优化1：GC参数调整 ===")
    
    -- 从默认 (200, 200) 调整为
    collectgarbage("setpause", 150)
    collectgarbage("setstepmul", 80)
    
    print("新配置:")
    print("  gcpause = 150（更频繁GC，保持低内存）")
    print("  gcstepmul = 80（更小的步长，减少单次停顿）")
    
    -- 测试
    local results = profiler.run(3600)
    print(string.format(
        "  GC掉帧: %d (%.1f%%)",
        #results.gc_pauses,
        #results.gc_pauses / 3600 * 100
    ))
    -- 结果：GC掉帧: 245/3600 (6.8%)
    -- 改善：-48%
end

-- 优化2：手动GC调度
function Optimization2_manual_scheduling()
    print("=== 优化2：手动GC调度 ===")
    
    collectgarbage("stop")  -- 停止自动GC
    
    local gc_budget = 1.5  -- 每帧1.5ms预算
    
    function on_frame()
        local frame_start = os.clock()
        
        update_game_logic()
        render_scene()
        
        -- 计算剩余时间
        local used = (os.clock() - frame_start) * 1000
        local remaining = 16.67 - used
        
        if remaining > gc_budget then
            -- 执行适量GC
            collectgarbage("step", 1)
        end
    end
    
    -- 测试
    local results = test_with_manual_gc()
    print(string.format(
        "  GC掉帧: %d (%.1f%%)",
        #results.gc_pauses,
        #results.gc_pauses / 3600 * 100
    ))
    -- 结果：GC掉帧: 52/3600 (1.4%)
    -- 改善：-89%
end

-- 优化3：场景切换时完整GC
function Optimization3_scene_transition_gc()
    print("=== 优化3：场景切换优化 ===")
    
    function load_new_scene(scene_id)
        -- 显示加载画面
        show_loading_screen()
        
        -- 卸载旧场景
        unload_current_scene()
        
        -- 完整GC，清理旧资源
        collectgarbage("collect")
        print("场景切换GC完成")
        
        -- 加载新场景
        load_scene(scene_id)
        
        -- 再次GC，清理加载临时对象
        collectgarbage("collect")
        
        hide_loading_screen()
    end
    
    -- 测试
    local start = os.clock()
    load_new_scene(123)
    local duration = os.clock() - start
    
    print(string.format("场景加载时间: %.2fs", duration))
    -- 结果：2.1秒（原3.2秒）
    -- 改善：-34%
end

-- 优化4：对象池
function Optimization4_object_pooling()
    print("=== 优化4：对象池化 ===")
    
    -- 战斗伤害数字对象池
    local damage_number_pool = {}
    for i = 1, 100 do
        table.insert(damage_number_pool, {})
    end
    
    function show_damage(amount, position)
        local obj = table.remove(damage_number_pool)
        if not obj then
            obj = {}  -- 池耗尽时创建新对象
        end
        
        obj.amount = amount
        obj.position = position
        obj.lifetime = 2.0
        
        -- 显示
        display_damage_number(obj)
        
        -- 动画结束后回收
        timer.after(2.0, function()
            for k in pairs(obj) do
                obj[k] = nil
            end
            table.insert(damage_number_pool, obj)
        end)
    end
    
    -- 测试：战斗场景
    local start_mem = collectgarbage("count")
    
    for i = 1, 10000 do
        show_damage(math.random(100, 999), {x = 0, y = 0})
    end
    
    collectgarbage("collect")
    local end_mem = collectgarbage("count")
    
    print(string.format(
        "内存占用: %.2f MB (池化前: ~%.2f MB)",
        (end_mem - start_mem) / 1024,
        10000 * 0.5 / 1024  -- 估算
    ))
    -- 结果：0.82 MB (池化前: ~4.88 MB)
    -- 改善：-83%
end

-- 应用所有优化
function apply_all_optimizations()
    Optimization1_tune_gc()
    Optimization2_manual_scheduling()
    Optimization3_scene_transition_gc()
    Optimization4_object_pooling()
end
```

#### 优化效果

```lua
-- 最终对比
local FINAL_RESULTS = {
    frame_drops_per_minute = 1,     -- 从8降至1 (-87.5%)
    avg_frame_time = 15.2,          -- 从17.5降至15.2 (-13.1%)
    memory_growth_rate = 12,        -- 从50降至12 (-76%)
    scene_load_time = 2.1,          -- 从3.2降至2.1 (-34%)
    
    player_feedback = "流畅度显著提升",
    metrics_improvement = {
        frame_drops = "-87.5%",
        memory_usage = "-76%",
        load_time = "-34%"
    }
}

print("=== 最终优化效果 ===")
for metric, value in pairs(FINAL_RESULTS.metrics_improvement) do
    print(string.format("  %s: %s", metric, value))
end
```

### 8.2 案例2：Web服务器内存泄漏

#### 问题描述

```lua
-- 某Lua Web服务器的内存泄漏问题
local SERVER_PROFILE = {
    framework = "OpenResty",
    avg_qps = 5000,
    typical_request = "API调用",
    
    symptoms = {
        "内存持续增长",
        "24小时后达到8GB",
        "需要每天重启"
    }
}

-- 泄漏检测
function detect_memory_leak()
    local samples = {}
    
    for i = 1, 60 do
        table.insert(samples, {
            time = os.time(),
            memory = collectgarbage("count")
        })
        
        os.execute("sleep 60")  -- 等待1分钟
    end
    
    -- 分析趋势
    local increasing = 0
    for i = 2, #samples do
        if samples[i].memory > samples[i-1].memory then
            increasing = increasing + 1
        end
    end
    
    if increasing > #samples * 0.8 then
        print("🚨 检测到内存泄漏！")
        print(string.format(
            "  持续增长率: %.1f%%",
            increasing / #samples * 100
        ))
        
        -- 计算增长速率
        local first = samples[1]
        local last = samples[#samples]
        local rate = (last.memory - first.memory) / 
                    (last.time - first.time)
        
        print(string.format(
            "  增长速率: %.2f KB/s",
            rate
        ))
        
        -- 预测OOM时间
        local limit = 8 * 1024 * 1024  -- 8GB
        local remaining = limit - last.memory
        local oom_seconds = remaining / rate
        
        print(string.format(
            "  预计OOM: %.1f 小时后",
            oom_seconds / 3600
        ))
    end
end
```

#### 排查过程

```lua
-- 使用弱引用表排查
local LeakDetector = {}

function LeakDetector.track_objects()
    local registry = {}
    setmetatable(registry, {__mode = "k"})
    
    local counts = {}
    
    -- Hook对象创建
    local original_setmetatable = setmetatable
    setmetatable = function(t, mt)
        registry[t] = true
        
        local type_name = mt.__name or "table"
        counts[type_name] = (counts[type_name] or 0) + 1
        
        return original_setmetatable(t, mt)
    end
    
    return {
        get_stats = function()
            -- 统计活跃对象
            local live_objects = 0
            for _ in pairs(registry) do
                live_objects = live_objects + 1
            end
            
            return {
                total_created = sum_table(counts),
                live_objects = live_objects,
                by_type = counts
            }
        end,
        
        reset = function()
            counts = {}
            collectgarbage("collect")
        end
    }
end

-- 使用
local detector = LeakDetector.track_objects()

-- 运行一段时间后检查
timer.after(300, function()
    local stats = detector.get_stats()
    
    print("对象统计：")
    print(string.format(
        "  总创建: %d",
        stats.total_created
    ))
    print(string.format(
        "  仍存活: %d",
        stats.live_objects
    ))
    
    print("\n按类型分类：")
    for type_name, count in pairs(stats.by_type) do
        print(string.format("  %s: %d", type_name, count))
    end
end)

-- 输出示例：
-- 对象统计：
--   总创建: 1500000
--   仍存活: 850000  ← 异常高！
--
-- 按类型分类：
--   Session: 75000  ← 疑似泄漏
--   Request: 45000
--   Response: 30000
```

#### 根因分析

```lua
-- 发现问题：Session缓存使用强引用
local sessions = {}  -- ❌ 强引用表

function create_session(user_id)
    local session = {
        user_id = user_id,
        created_at = os.time(),
        data = {}
    }
    
    sessions[session_id] = session
    return session
end

-- 问题：Session永不过期，持续累积！

-- 解决方案1：使用弱引用表
local sessions_fixed = {}
setmetatable(sessions_fixed, {__mode = "v"})  -- ✅ 弱值表

-- 解决方案2：定期清理
function cleanup_old_sessions()
    local now = os.time()
    local timeout = 3600  -- 1小时超时
    
    for id, session in pairs(sessions) do
        if now - session.created_at > timeout then
            sessions[id] = nil
        end
    end
    
    collectgarbage("collect")
end

timer.every(300, cleanup_old_sessions)  -- 每5分钟清理

-- 解决方案3：LRU缓存
local LRUCache = {}

function LRUCache.new(max_size)
    local self = {
        max_size = max_size,
        items = {},
        order = {}
    }
    
    function self.set(key, value)
        if not self.items[key] then
            table.insert(self.order, key)
        end
        
        self.items[key] = value
        
        -- 超过容量时删除最旧的
        if #self.order > self.max_size then
            local oldest = table.remove(self.order, 1)
            self.items[oldest] = nil
        end
    end
    
    function self.get(key)
        return self.items[key]
    end
    
    return self
end

-- 使用LRU缓存
local session_cache = LRUCache.new(10000)

function get_session(session_id)
    local session = session_cache.get(session_id)
    if not session then
        session = load_session_from_db(session_id)
        session_cache.set(session_id, session)
    end
    return session
end
```

#### 修复效果

```lua
-- 应用修复后的效果
local AFTER_FIX = {
    memory_stable_at = 1.2 * 1024 * 1024,  -- 稳定在1.2GB
    uptime = "30+ days",
    no_restart_needed = true,
    
    improvements = {
        memory_usage = "-85%",    -- 从8GB降至1.2GB
        stability = "100%",       -- 不再需要重启
        performance = "+15%"      -- CPU利用率降低
    }
}
```

### 8.3 案例3：嵌入式设备优化

#### 设备规格

```lua
local DEVICE_SPEC = {
    cpu = "ARM Cortex-M4 @ 120MHz",
    ram = "512 KB",
    flash = "2 MB",
    os = "FreeRTOS + eLua",
    
    constraints = {
        max_heap = 256 * 1024,  -- 256KB堆
        gc_pause_limit = 50,    -- GC不能超过50ms
        critical_task_rt = 10   -- 关键任务10ms响应
    }
}
```

#### 极限优化

```lua
-- 策略1：激进GC + 对象池
function embedded_optimization()
    -- 极低内存配置
    collectgarbage("setpause", 80)
    collectgarbage("setstepmul", 150)
    
    -- 全局对象池
    local pools = {
        small = {},   -- < 64 bytes
        medium = {},  -- 64-256 bytes
        large = {}    -- > 256 bytes
    }
    
    function allocate(size)
        local pool
        if size < 64 then
            pool = pools.small
        elseif size < 256 then
            pool = pools.medium
        else
            pool = pools.large
        end
        
        local obj = table.remove(pool)
        if not obj then
            obj = {}  -- 创建新对象
        end
        
        return obj
    end
    
    function deallocate(obj, size)
        -- 清空对象
        for k in pairs(obj) do
            obj[k] = nil
        end
        
        -- 归还池
        local pool
        if size < 64 then
            pool = pools.small
        elseif size < 256 then
            pool = pools.medium
        else
            pool = pools.large
        end
        
        if #pool < 50 then  -- 限制池大小
            table.insert(pool, obj)
        end
    end
end

-- 策略2：禁用自动GC，手动调度
function manual_gc_control()
    collectgarbage("stop")
    
    -- 在空闲任务中GC
    function idle_task()
        while true do
            if system_is_idle() then
                collectgarbage("step", 1)
            end
            os.sleep(0.010)  -- 10ms
        end
    end
    
    -- 在关键任务前确保足够内存
    function critical_task()
        local mem = collectgarbage("count")
        if mem > 200 * 1024 then  -- 超过200KB
            collectgarbage("collect")
        end
        
        perform_critical_work()
    end
end

-- 策略3：预分配缓冲区
function preallocate_buffers()
    local buffers = {
        uart = string.rep("\0", 256),
        i2c = string.rep("\0", 128),
        spi = string.rep("\0", 512)
    }
    
    -- 复用缓冲区，避免分配
    function read_uart()
        return uart_read_into(buffers.uart)
    end
end
```

---

## 9. 故障排查

### 9.1 常见问题诊断

#### 问题1：突然的长时间停顿

```lua
-- 症状：偶尔出现>100ms的停顿
function diagnose_long_pause()
    print("=== 诊断长时间停顿 ===\n")
    
    -- 可能原因1：完整GC
    local gc_time = measure_full_gc()
    if gc_time > 100 then
        print("原因：完整GC耗时过长")
        print(string.format("  测量: %.2f ms", gc_time))
        print("解决：")
        print("  1. 降低内存使用，减少GC工作量")
        print("  2. 使用增量GC，避免完整GC")
        print("  3. 在合适时机手动GC")
        return
    end
    
    -- 可能原因2：大对象分配
    print("检查大对象分配...")
    -- 需要在分配时Hook
    
    -- 可能原因3：弱引用表清理
    print("检查弱引用表...")
    -- 大量弱引用表可能导致原子阶段过长
end

-- 解决方案
function fix_long_pause()
    -- 方案1：分散GC工作
    collectgarbage("stop")
    
    function on_frame()
        -- 每帧少量GC
        collectgarbage("step", 2)
    end
    
    -- 方案2：在可预测时机完整GC
    function on_level_load()
        show_loading_screen()
        collectgarbage("collect")
        hide_loading_screen()
    end
end
```

#### 问题2：内存占用持续增长

```lua
-- 症状：内存不断上涨，GC无效
function diagnose_memory_growth()
    print("=== 诊断内存增长 ===\n")
    
    -- 收集证据
    local samples = {}
    for i = 1, 10 do
        collectgarbage("collect")
        table.insert(samples, collectgarbage("count"))
        os.execute("sleep 30")
    end
    
    -- 检查是否持续增长
    local increasing = true
    for i = 2, #samples do
        if samples[i] < samples[i-1] then
            increasing = false
            break
        end
    end
    
    if increasing then
        print("确认：内存持续增长（泄漏）")
        print("\n可能原因：")
        print("  1. 全局表持有引用")
        print("  2. 闭包捕获大对象")
        print("  3. 循环引用且无__gc")
        print("  4. C扩展持有Lua对象")
        
        print("\n排查步骤：")
        print("  1. 检查全局变量")
        check_globals()
        print("  2. 检查长生命周期对象")
        check_long_lived_objects()
        print("  3. 使用弱引用表")
        suggest_weak_tables()
    end
end

function check_globals()
    print("\n全局变量检查：")
    local count = 0
    for k, v in pairs(_G) do
        if type(v) == "table" then
            count = count + 1
            local size = estimate_table_size(v)
            if size > 1024 then
                print(string.format(
                    "  %s: ~%.2f KB",
                    k, size / 1024
                ))
            end
        end
    end
    print(string.format("  总计: %d 个表", count))
end
```

#### 问题3：GC频率过高

```lua
-- 症状：CPU大量时间花在GC上
function diagnose_excessive_gc()
    print("=== 诊断GC频率 ===\n")
    
    -- 测量GC开销
    local total_time = 0
    local gc_time = 0
    local gc_count = 0
    
    local start = os.clock()
    
    -- Hook GC
    local old_step = collectgarbage
    collectgarbage = function(opt, ...)
        if opt == "step" then
            local gc_start = os.clock()
            local result = old_step(opt, ...)
            gc_time = gc_time + (os.clock() - gc_start)
            gc_count = gc_count + 1
            return result
        end
        return old_step(opt, ...)
    end
    
    -- 运行工作负载
    run_workload()
    
    total_time = os.clock() - start
    
    local gc_overhead = gc_time / total_time
    
    print(string.format(
        "GC统计：\n" ..
        "  总时间: %.2f s\n" ..
        "  GC时间: %.2f s\n" ..
        "  GC次数: %d\n" ..
        "  GC开销: %.1f%%",
        total_time,
        gc_time,
        gc_count,
        gc_overhead * 100
    ))
    
    if gc_overhead > 0.10 then
        print("\n⚠️ GC开销过高！")
        print("建议：")
        print("  1. 提高gcpause，减少GC频率")
        print("  2. 提高gcstepmul，减少GC次数")
        print("  3. 减少对象分配")
    end
end
```

### 9.2 调试技巧

#### 技巧1：内存快照对比

```lua
-- 内存快照工具
local MemorySnapshot = {}

function MemorySnapshot.capture()
    collectgarbage("collect")
    
    local snapshot = {
        timestamp = os.time(),
        memory = collectgarbage("count"),
        objects = {}
    }
    
    -- 遍历所有对象（简化版）
    for k, v in pairs(_G) do
        if type(v) == "table" then
            snapshot.objects[k] = {
                type = "table",
                size = estimate_table_size(v)
            }
        end
    end
    
    return snapshot
end

function MemorySnapshot.compare(snap1, snap2)
    print("=== 内存快照对比 ===\n")
    
    print(string.format(
        "总内存: %.2f MB → %.2f MB (%+.2f MB)",
        snap1.memory / 1024,
        snap2.memory / 1024,
        (snap2.memory - snap1.memory) / 1024
    ))
    print()
    
    print("对象变化：")
    
    -- 新增对象
    for k, v in pairs(snap2.objects) do
        if not snap1.objects[k] then
            print(string.format(
                "  [新增] %s: %.2f KB",
                k, v.size / 1024
            ))
        end
    end
    
    -- 增长的对象
    for k, v2 in pairs(snap2.objects) do
        local v1 = snap1.objects[k]
        if v1 and v2.size > v1.size * 1.5 then
            print(string.format(
                "  [增长] %s: %.2f KB → %.2f KB (%+.1f%%)",
                k,
                v1.size / 1024,
                v2.size / 1024,
                (v2.size / v1.size - 1) * 100
            ))
        end
    end
end

-- 使用
local snap1 = MemorySnapshot.capture()

-- 运行一段时间
run_for_a_while()

local snap2 = MemorySnapshot.capture()
MemorySnapshot.compare(snap1, snap2)
```

#### 技巧2：GC事件跟踪

```lua
-- GC事件跟踪器
local GCTracer = {}

function GCTracer.install()
    local events = {}
    
    -- Hook collectgarbage
    local original = collectgarbage
    _G.collectgarbage = function(opt, arg)
        local event = {
            timestamp = os.clock(),
            operation = opt,
            argument = arg
        }
        
        if opt == "collect" then
            local before = original("count")
            local start = os.clock()
            
            original("collect")
            
            event.duration = os.clock() - start
            event.freed = before - original("count")
        elseif opt == "step" then
            local start = os.clock()
            local result = original("step", arg)
            event.duration = os.clock() - start
            event.result = result
        else
            return original(opt, arg)
        end
        
        table.insert(events, event)
        
        return 0
    end
    
    return {
        get_events = function() return events end,
        clear = function() events = {} end,
        
        print_summary = function()
            print("=== GC事件统计 ===\n")
            
            local collect_count = 0
            local step_count = 0
            local total_duration = 0
            local total_freed = 0
            
            for _, e in ipairs(events) do
                if e.operation == "collect" then
                    collect_count = collect_count + 1
                    total_freed = total_freed + (e.freed or 0)
                elseif e.operation == "step" then
                    step_count = step_count + 1
                end
                total_duration = total_duration + (e.duration or 0)
            end
            
            print(string.format(
                "collect调用: %d 次\n" ..
                "step调用: %d 次\n" ..
                "总耗时: %.2f ms\n" ..
                "总回收: %.2f MB",
                collect_count,
                step_count,
                total_duration * 1000,
                total_freed / 1024
            ))
        end
    }
end

-- 使用
local tracer = GCTracer.install()

-- 运行测试
run_test()

-- 查看统计
tracer.print_summary()
```

---

## 10. 常见问题与解答

### 10.1 基础概念

**Q1: gcpause和gcstepmul的区别是什么？**

A: 两者控制GC的不同方面：

```lua
-- gcpause：控制"何时"触发GC
-- GC阈值 = (当前内存 / 100) × gcpause
collectgarbage("setpause", 200)
-- 意思：内存翻倍时触发下次GC

-- gcstepmul：控制"每次做多少"GC工作
-- 工作量 = (1024 / 100) × gcstepmul
collectgarbage("setstepmul", 200)
-- 意思：每次执行约2KB的GC工作

-- 类比：
-- gcpause = 洗碗频率（多脏才洗）
-- gcstepmul = 每次洗多少碗
```

**Q2: 什么时候应该手动调用collectgarbage("collect")？**

A: 在以下场景手动GC是合适的：

```lua
-- ✅ 适合手动GC的场景
function suitable_for_manual_gc() 
    -- 1. 关卡/场景切换
    function load_level()
        unload_old_level()
        collectgarbage("collect")  -- 清理旧资源
        load_new_level()
    end
    
    -- 2. 长时间空闲后
    function on_resume_from_background()
        collectgarbage("collect")
    end
    
    -- 3. 批处理完成后
    function finish_batch()
        process_all_items()
        collectgarbage("collect")
    end
    
    -- 4. 低峰期维护
    function nightly_maintenance()
        collectgarbage("collect")
    end
end

-- ❌ 不适合手动GC的场景
function unsuitable_for_manual_gc()
    -- 1. 游戏主循环
    function game_loop()
        while true do
            collectgarbage("collect")  -- ❌ 导致卡顿
            update_and_render()
        end
    end
    
    -- 2. 请求处理中
    function handle_request(req)
        collectgarbage("collect")  -- ❌ 增加延迟
        return process(req)
    end
    
    -- 3. 实时音视频
    function audio_callback()
        collectgarbage("collect")  -- ❌ 破坏实时性
        generate_audio()
    end
end
```

**Q3: 为什么调大gcpause后内存使用反而降低了？**

A: 这是一个常见误解：

```lua
-- 现象解释
function explain_paradox()
    print([[
gcpause调大 → 内存降低？

看似矛盾，实则合理：

gcpause = 100（激进）：
  • GC非常频繁
  • 每次GC开销大
  • 频繁中断程序
  • 对象来不及释放
  • 内存碎片多
  → 总内存使用可能更高

gcpause = 300（保守）：
  • GC不太频繁
  • 程序连续运行时间长
  • 对象生命周期完整
  • 一次性回收更多
  → 内存峰值可能更低

结论：不是越激进越好，需要找到平衡点
    ]])
end
```

### 10.2 性能调优

**Q4: 如何确定最优的GC参数？**

A: 使用科学的方法：

```lua
-- 参数寻优流程
function find_optimal_params()
    -- 1. 定义目标
    local objectives = {
        min_latency = true,     -- 最小化延迟
        max_throughput = false, -- 或最大化吞吐
        min_memory = false      -- 或最小化内存
    }
    
    -- 2. 定义搜索空间
    local search_space = {
        gcpause = {100, 150, 200, 250, 300, 400},
        gcstepmul = {100, 150, 200, 250, 300, 400}
    }
    
    -- 3. 网格搜索
    local best_config = nil
    local best_score = math.huge
    
    for _, pause in ipairs(search_space.gcpause) do
        for _, stepmul in ipairs(search_space.gcstepmul) do
            collectgarbage("setpause", pause)
            collectgarbage("setstepmul", stepmul)
            
            local metrics = benchmark()
            local score = calculate_score(metrics, objectives)
            
            if score < best_score then
                best_score = score
                best_config = {
                    pause = pause,
                    stepmul = stepmul,
                    metrics = metrics
                }
            end
        end
    end
    
    -- 4. 应用最优配置
    collectgarbage("setpause", best_config.pause)
    collectgarbage("setstepmul", best_config.stepmul)
    
    return best_config
end
```

**Q5: GC调优能带来多大的性能提升？**

A: 根据场景不同，提升幅度差异很大：

```lua
-- 不同场景的典型提升
local TYPICAL_IMPROVEMENTS = {
    real_time_game = {
        frame_drops = "-50% ~ -80%",
        avg_latency = "-10% ~ -30%",
        description = "显著提升流畅度"
    },
    
    web_server = {
        throughput = "+10% ~ +25%",
        latency_p99 = "-20% ~ -40%",
        description = "提升吞吐和尾延迟"
    },
    
    embedded_system = {
        memory_usage = "-30% ~ -60%",
        battery_life = "+15% ~ -25%",
        description = "大幅降低资源占用"
    },
    
    batch_processing = {
        total_time = "-5% ~ -15%",
        memory_peak = "-20% ~ -40%",
        description = "适度提升，主要降低内存"
    }
}

print("GC调优效果预期：")
for scene, improvements in pairs(TYPICAL_IMPROVEMENTS) do
    print(string.format("\n%s:", scene))
    for metric, value in pairs(improvements) do
        if metric ~= "description" then
            print(string.format("  %s: %s", metric, value))
        end
    end
    print("  " .. improvements.description)
end
```

### 10.3 故障排查

**Q6: 如何判断是否存在内存泄漏？**

A: 使用以下诊断流程：

```lua
-- 内存泄漏诊断清单
local LeakDiagnostics = {}

function LeakDiagnostics.run_checklist()
    print("=== 内存泄漏诊断清单 ===\n")
    
    -- 检查1：内存持续增长
    print("[1/5] 检查内存趋势...")
    local growing = check_memory_trend()
    print(growing and "  ⚠️ 内存持续增长" or "  ✅ 内存稳定")
    
    -- 检查2：完整GC后仍高
    print("[2/5] 执行完整GC...")
    local before = collectgarbage("count")
    collectgarbage("collect")
    collectgarbage("collect")  -- 两次确保彻底
    local after = collectgarbage("count")
    local reduced = (before - after) / before
    
    print(string.format(
        "  释放: %.1f%% %s",
        reduced * 100,
        reduced < 0.1 and "⚠️ 释放很少" or "✅ 正常"
    ))
    
    -- 检查3：全局表检查
    print("[3/5] 检查全局变量...")
    local globals = count_global_objects()
    print(string.format(
        "  全局对象: %d %s",
        globals,
        globals > 1000 and "⚠️ 过多" or "✅ 正常"
    ))
    
    -- 检查4：长生命周期对象
    print("[4/5] 检查长生命周期对象...")
    -- 需要额外工具支持
    
    -- 检查5：循环引用
    print("[5/5] 检查循环引用...")
    -- 需要额外工具支持
    
    print("\n诊断结果：")
    if growing or reduced < 0.1 or globals > 1000 then
        print("  🚨 可能存在内存泄漏！")
        print("  建议：")
        print("    1. 使用弱引用表")
        print("    2. 检查全局变量")
        print("    3. 添加显式清理代码")
    else
        print("  ✅ 未发现明显泄漏")
    end
end
```

**Q7: 为什么禁用GC后内存还在增长？**

A: 禁用GC并不阻止内存分配：

```lua
-- 误区演示
function gc_disable_misconception()
    collectgarbage("stop")  -- 停止GC
    
    local data = {}
    for i = 1, 10000 do
        data[i] = {value = i}
        -- 内存仍在分配！
        -- GC只是不回收而已
    end
    
    print("内存使用:", collectgarbage("count") / 1024, "MB")
    -- 内存会不断增长，直到OOM！
    
    collectgarbage("restart")
    collectgarbage("collect")
    print("GC后内存:", collectgarbage("count") / 1024, "MB")
end

-- 正确用法
function correct_gc_disable_usage()
    -- 场景：关键路径，已知内存充足
    collectgarbage("stop")
    
    perform_time_critical_task()
    
    collectgarbage("restart")
    -- 完成后立即GC
    collectgarbage("collect")
end
```

**Q8: 如何在生产环境监控GC性能？**

A: 建立完整的监控体系：

```lua
-- 生产环境GC监控
local ProductionMonitor = {}

function ProductionMonitor.setup()
    local metrics = {
        gc_count = 0,
        gc_total_time = 0,
        memory_samples = {},
        alerts = {}
    }
    
    -- Hook GC
    local original = collectgarbage
    _G.collectgarbage = function(opt, arg)
        if opt == "collect" then
            local start = os.clock()
            original("collect")
            local duration = os.clock() - start
            
            metrics.gc_count = metrics.gc_count + 1
            metrics.gc_total_time = metrics.gc_total_time + duration
            
            -- 慢GC告警
            if duration > 0.100 then  -- > 100ms
                table.insert(metrics.alerts, {
                    type = "slow_gc",
                    duration = duration,
                    timestamp = os.time()
                })
            end
        end
        
        return original(opt, arg)
    end
    
    -- 定期采样内存
    timer.every(60, function()
        local mem = collectgarbage("count")
        table.insert(metrics.memory_samples, {
            timestamp = os.time(),
            memory = mem
        })
        
        -- 保持最近1小时的数据
        if #metrics.memory_samples > 60 then
            table.remove(metrics.memory_samples, 1)
        end
        
        -- 内存告警
        if mem > 500 * 1024 then  -- > 500MB
            table.insert(metrics.alerts, {
                type = "high_memory",
                memory = mem,
                timestamp = os.time()
            })
        end
    end)
    
    -- 定期上报
    timer.every(300, function()  -- 每5分钟
        report_to_monitoring_system(metrics)
    end)
    
    return metrics
end

-- 启用监控
local monitor_metrics = ProductionMonitor.setup()
```

---

## 🎯 总结

### 调优黄金法则

1. **测量先行**：没有测量就没有优化依据
2. **渐进调整**：小步快走，及时验证
3. **因地制宜**：不同场景需要不同策略
4. **持续监控**：调优不是一次性工作

### 快速参考

```lua
-- 常见场景的快速配置

-- 游戏（60 FPS）
collectgarbage("setpause", 150)
collectgarbage("setstepmul", 100)

-- 服务器（高吞吐）
collectgarbage("setpause", 300)
collectgarbage("setstepmul", 300)

-- 嵌入式（低内存）
collectgarbage("setpause", 100)
collectgarbage("setstepmul", 200)

-- 批处理（高效率）
collectgarbage("setpause", 400)
collectgarbage("setstepmul", 400)
```

### 进一步学习

- [增量GC详解](./incremental_gc.md)
- [写屏障机制](./write_barrier.md)
- [终结器实现](./finalizer.md)
- [弱引用表](./weak_table.md)

---

<div align="center">

**[⬆️ 返回顶部](#lua-515-gc-参数调优实战指南)** · **[📖 返回GC模块](./wiki_gc.md)** · **[🏠 返回总览](../wiki.md)**

---

**📅 最后更新**：2025-10-25  
**📌 文档版本**：v1.0  
**🔖 基于 Lua 版本**：5.1.5

</div>




