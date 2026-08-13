# Shenzhen compact daily deployment

The fixed deployment contains the libraries, the MD template, the launcher,
the preflight script, and the two fixed systemd units. Those files are copied
once to `/home/zane/run_main` and `/etc/systemd/system`.

Each trading day, replace only these two files:

```text
/home/zane/configs/config_sze_daily_YYYYMMDD.json
/home/zane/configs/main_sze_daily_YYYYMMDD.conf
```

Both files are dated business artifacts. The launcher automatically selects
the newest `config_sze_daily_*.json`, or accepts an explicit path as its first
argument. The fixed MD template, libraries, launcher and systemd units are
deployed once.

`config_sze_daily_YYYYMMDD.json` contains the trading day, all static inputs, journal/
SHM paths, worker count, worker CPU/state CPU lists, per-instrument CPU owner,
and model SHA256. `main_sze_daily_YYYYMMDD.conf` is the dated strategy entry point.

The launcher creates worker JSON/conf files under `/dev/shm` at runtime and
deletes them on shutdown. They are not daily deployment artifacts.

Research-only files (`static_audit_*.json`, `rejected_*.json`, and
`universe_*.csv`) belong under an archive directory and are not required by
production preflight.
