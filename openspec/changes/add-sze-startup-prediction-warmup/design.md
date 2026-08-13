## Context

`ZStrategy::on_signal` 当前用编译期常量控制启动空窗，并在空窗内直接返回。深圳实盘需要在模型和订单簿刚完成初始化时继续产出预测值，但前 50 个采样点不能触发任何交易动作。该逻辑必须按股票实例计数，不能使用全局进程计数。

## Goals / Non-Goals

**Goals:**

- 默认 warmup 为 50 个已分发到深圳策略实例的采样点。
- warmup 期间完成 `calcTheo` 并保留预测输出路径。
- warmup 期间屏蔽普通 T0 处理和测试报单。
- 允许每日 config 覆盖 warmup 点数，并由配置 guard 校验。

**Non-Goals:**

- 不改变模型因子计算、订单簿恢复或北交所逻辑。
- 不把 warmup 计数扩展为原始逐笔事件计数；计数单位是最终交给 `ZStrategy::on_signal` 的采样点。

## Decisions

- 在 `ZStrategy` 中保存每个股票自己的 `startup_signal_count_` 和 `startup_warmup_signal_count_`。
- `on_signal` 先更新上下文并执行 `calcTheo(signal)`，然后当计数不超过 warmup 时记录预测并返回；只有超过 warmup 才调用 `handleT0()`。
- `sze_startup_warmup_signals` 放在策略根配置，缺省为 50，必须是非负整数；0 表示不预热。
- `sze_test_order` 仍受 warmup 门控，即使其 `trigger_after_signals` 已满足，也不能在 warmup 内发送。

## Risks / Trade-offs

- [Risk] warmup 点数变化会改变首笔可交易时刻 → 启动日志打印最终值，配置审计记录该字段。
- [Risk] 预测输出接口若只在 `handleT0` 前生成，warmup 预测可能缺少原有订单相关字段 → 使用已有 `calcTheo` 和预测日志路径，不新增交易副作用。

