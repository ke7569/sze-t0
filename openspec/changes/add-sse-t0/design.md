# Design

Build `libt0_strategy_sse.so` from the existing common strategy sources without `T0_SZE_STRATEGY_ONLY`, so the ABI boundary remains available for the future HStrategy implementation. The current generic `SsePredictor` is a legacy path and is not allowed to consume the supplied v0.4 SSE model. The SSE strategy contract requires `sse_factor_contract=v0.4-sse-cob-batch-end-100us` and `sse_inference_backend=native-cpp`. Keep market-data and TD vendor settings in templates only; passwords and endpoint overrides are injected at deployment. The FPGA template records the Shanghai multicast addresses from the supplied server document.

The reusable `modules/deepwin_guoxin/md/common/UdpChannelRuntime.*` owns exchange-neutral UDP channel lifecycle and emits raw datagrams. The disposable `sse-t0/market_observer/sse_udp_observer` only formats those datagrams as JSONL. SSE and SZE decoders/factor engines remain separate above this boundary.

The supplied `ref/v04-legacy-midmix-sse.tar.zst` is treated as a deployment artifact. Its format is not assumed to be a standard tar archive; the deployment owner must use the provided vendor/research extraction workflow and set `model_path` to the extracted file after validation.

Production inference uses the native `sse-t0/model/sse_model_runtime.cpp` and
the `SSEMODL1` artifact generated offline from `state_dict.npz`; PyTorch is
reference-only and is not linked into the live process.
