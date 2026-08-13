## Context

北交所的生产模式使用固定运行目录和固定 main 配置，每日仅替换策略业务 JSON 与一个行情/交易日参数文件。深圳当前将交易日嵌入多个 JSON、conf、systemd unit 和脚本文件名，造成不必要的发布面。深圳 recovery 已经能够从一个 SHM/journal consumer 顺序消费事件，因此每日分片文件不是业务必需品。

## Goals / Non-Goals

**Goals:**

- 每日实盘只替换两个业务文件。
- 固定 main conf 和 systemd unit 读取固定路径，不携带日期。
- 唯一策略 JSON 既包含全市场静态参数，也包含 worker 数量、CPU 分配和当日 journal/SHM/output 路径。
- 固定 launcher 按股票 CPU 分配启动 worker 进程，保留单股票 orderbook owner、故障隔离和缓存局部性。
- 审计输出与实盘发布解耦。

**Non-Goals:**

- 不改变模型二进制和策略库加载方式。
- 不改变行情协议、journal 格式或预测因子。
- 不把 capture 和 recovery 合并为一个进程，也不重写已验证的 orderbook 状态为多线程共享结构。

## Decisions

- 每日业务文件按交易日命名为 `config_sze_daily_YYYYMMDD.json` 和 `main_sze_daily_YYYYMMDD.conf`；行情 MD JSON 为首次部署的固定文件，不作为每日文件。
- 固定 `main_sze_capture.conf` 与 `main_sze_recovery.conf` 使用上述固定路径。
- `deepwin_sze_daily.json` 只承载行情接收和 journal/SHM 参数；策略 JSON 承载静态参数和 recovery worker 参数。若实际链路只允许一个 JSON，可在后续合并，但当前目标先保证两文件清晰分层。
- 策略 JSON 增加 `worker_count`、`worker_cpus` 和 `worker_state_cpus`，每个 `ins_params` 含 `cpu`。固定 launcher 只在 `/dev/shm` 创建运行期临时子配置，不形成每日部署文件。
- 固定启动脚本从策略 JSON 读取交易日和路径，一次启动并监督全部 worker，不再通过 `%i` 启动 8 个 systemd 实例。

## Risks / Trade-offs

- [Risk] worker 异常 → launcher 记录失败 worker、终止同批次并由 systemd 按策略重启；capture/journal 独立运行不受影响。
- [Risk] 单 consumer 分发队列拥塞 → 使用有界无锁/低锁队列、队列水位监控，reader 不做 CSV 和模型推理。
- [Risk] 老日期化文件残留导致误启动 → preflight 只接受固定文件中的 trading_day，并拒绝旧的日期化服务。

## Migration Plan

1. 保留现有日期化 8 shard 方案作为回滚路径。
2. 新增固定文件名配置和单 launcher service，先用 shadow 模式回放验证事件、样本和预测数量。
3. 对比 launcher 运行期分片与旧部署分片输出，确认每股票顺序一致后切换定时器。
4. 回滚时恢复旧 unit，业务 JSON 不需要改回日期化命名。
