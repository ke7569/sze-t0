# Independent SZE Level1 Snapshot Collector

This tool joins the Dongguan Shenzhen Level1 snapshot multicast and writes
decoded records to CSV. It reuses the repository's `SZEProtocol` decoder; it
does not modify the production capture process, journal, SHM, or strategy.

Build on the Linux market-data host:

```bash
cd /home/zane/<source-tree>
bash ./source/reference/sz_md_recv/build_sze_l1_snapshot_collector.sh
```

It is also registered as the CMake target `sze_l1_snapshot_collector` in the
main source tree.

Run a short validation:

```bash
./source/reference/sz_md_recv/sze_l1_snapshot_collector \
  --group 239.35.80.1 --port 37100 \
  --iface-ip 11.11.11.11 --ifname hqh-p1-k2 \
  --seconds 30 --output /tmp/sze_l1_snapshots.csv
```

The CSV contains the wire sequence/channel metadata, normalized instrument and
time fields, prices/volume/turnover, and ten bid/ask levels. Wire prices are
normalized by `10000`, quantities by `100`, and turnover by `1000000` in the
existing decoder.

The tool deliberately keeps its own UDP socket and output path. Integration
into the live SZE process should be a separate change after this collector has
been validated during a market session.
