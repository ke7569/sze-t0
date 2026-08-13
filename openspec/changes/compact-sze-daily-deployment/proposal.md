## Why

深圳每日部署目前按交易日拆分 capture、recovery、shard 和审计文件，人工替换文件过多，容易发生日期、路径和分片配置不一致。北交所已经证明固定运行配置加一个每日业务 JSON 足够，深圳应采用同样的交付方式。

## What Changes

- 将深圳每日运行收敛为两个带日期的业务文件：`config_sze_daily_YYYYMMDD.json` 和 `main_sze_daily_YYYYMMDD.conf`；行情日参数从策略 JSON 生成到运行期临时文件。
- 固定 main conf、systemd unit、启动脚本和策略库，不再按日期复制。
- 将 CPU/worker 分配写入唯一策略 JSON，由一个固定 launcher 在运行时生成临时子配置并拉起 recovery worker 进程。
- 不再要求每日部署 shard JSON、shard main conf、universe、static_audit、rejected 和 artifact 清单。
- 审计文件仍可在研究机生成并归档，但不属于实盘替换包。
- 固定配置通过 JSON 内的 trading_day、journal_directory、shm_path 和 output 路径读取当日资源。

## Capabilities

### New Capabilities

- `sze-compact-daily-deployment`: 深圳固定运行配置与双文件每日发布协议。

### Modified Capabilities

无。

## Impact

- 修改深圳全市场配置生成器、recovery 启动编排和 systemd/脚本模板。
- 新增固定 recovery launcher，复用现有单 owner recovery 进程实现。
- 兼容现有模型和策略库，不改变模型文件格式。
