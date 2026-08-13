# Tasks

- [x] Update `master` to `origin/master` before testing.
- [x] Add `sse-t0/` child project with HStrategy boundary, factory, and configuration validation.
- [x] Add SSE CMake target reusing shared strategy sources.
- [x] Add raw per-channel UDP observer and JSONL capture format.
- [x] Add model, market-data, and runtime configuration templates without credentials.
- [x] Extract the supplied SSE model contract into a reproducible Python topology skeleton and 50-factor ABI.
- [x] Build and run config/unit tests in the approved CentOS 7.9 builder.
- [x] Add offline loopback UDP capture validation; defer live FPGA/TD connectivity until deployment.
- [x] Make the shared channel runtime concrete and keep exchange-specific decoders/factors above it.
- [x] Add a native C++ SSE v0.4 inference boundary and an offline NPZ conversion tool; keep PyTorch reference-only.
- [x] Record LV1/FPGA endpoints from the supplied server document, including the distinction between UDP FPGA and the separate Zhongchang L1 service endpoint.
- [x] Fail closed on the legacy SSE strategy factory until the native SSE factor pipeline is connected.
- [x] Add a self-contained live-capture package with start/stop scripts, dual-clock timestamps, relation analysis, and SHA256 manifest.
- [x] Validate the package contents and run the two-channel loopback capture before delivery.
- [x] Add a self-contained live-capture package with start/stop scripts, dual-clock timestamps, relation analysis, and SHA256 manifest.
- [x] Validate the package contents and run the two-channel loopback capture before delivery.
