# SZ FPGA UDP Probe

This is a minimal raw UDP multicast probe for Guosen FPGA Shenzhen market data.
It does not use the vendor API and does not parse the business payload. It only
joins a multicast group and prints packet metadata plus a hex/ascii dump so the
link and packet shape can be checked quickly.

Build on the Linux host connected to the feed:

```bash
cd /path/to/bj_market_data
bash ./build_sz_fpga_udp_probe.sh
```

Default run: Dongguan Shenzhen snapshot primary, hard-core local data IP:

```bash
RUN_SECONDS=30 bash ./run_sz_fpga_udp_probe.sh
```

If the market-data NIC has a known name, bind to it explicitly:

```bash
IFNAME=hqh-p1-k2 RUN_SECONDS=30 bash ./run_sz_fpga_udp_probe.sh
```

Common Shenzhen channels from the FPGA manual:

```bash
# Snapshot primary
GROUP=239.35.80.1 PORT=37100 RUN_SECONDS=30 bash ./run_sz_fpga_udp_probe.sh

# Tick-by-tick primary
GROUP=239.35.81.1 PORT=37101 RUN_SECONDS=30 bash ./run_sz_fpga_udp_probe.sh

# Index primary
GROUP=239.35.82.1 PORT=37102 RUN_SECONDS=30 bash ./run_sz_fpga_udp_probe.sh

# Fund snapshot primary
GROUP=239.35.85.1 PORT=37105 RUN_SECONDS=30 bash ./run_sz_fpga_udp_probe.sh

# HK connect primary
GROUP=239.35.90.1 PORT=37110 RUN_SECONDS=30 bash ./run_sz_fpga_udp_probe.sh
```

Manual notes:

- Dongguan local SZ hard-core feed uses `IFACE_IP=11.11.11.11`.
- Dongguan cross-market soft-core feed uses `IFACE_IP=12.12.12.12`.
- Backup soft-core feed uses `IFACE_IP=13.13.13.13`.
- If no packets arrive, first verify the NIC with:

```bash
tcpdump -i <ifname> dst host 239.35.80.1
```
