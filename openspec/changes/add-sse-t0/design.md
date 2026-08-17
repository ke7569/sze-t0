# Design

Build `libt0_strategy_sse.so` from the existing common strategy sources without `T0_SZE_STRATEGY_ONLY`, so the ABI boundary remains available for the future HStrategy implementation. The current generic `SsePredictor` is a legacy path and is not allowed to consume the supplied v0.4 SSE model. The SSE strategy contract requires `sse_factor_contract=v0.4-sse-cob-batch-end-100us` and `sse_inference_backend=native-cpp`. Keep market-data and TD vendor settings in templates only; passwords and endpoint overrides are injected at deployment. The FPGA template records the Shanghai multicast addresses from the supplied server document.

The reusable `modules/deepwin_guoxin/md/common/UdpChannelRuntime.*` owns exchange-neutral UDP channel lifecycle and emits raw datagrams. The disposable `sse-t0/market_observer/sse_udp_observer` only formats those datagrams as JSONL. SSE and SZE decoders/factor engines remain separate above this boundary.

The supplied `ref/v04-legacy-midmix-sse.tar.zst` is treated as a deployment artifact. Its format is not assumed to be a standard tar archive; the deployment owner must use the provided vendor/research extraction workflow and set `model_path` to the extracted file after validation.

Production inference uses the native `sse-t0/model/sse_model_runtime.cpp` and
the `SSEMODL1` artifact generated offline from `state_dict.npz`; PyTorch is
reference-only and is not linked into the live process.

The 2026-08-17 handoff under `/home/external` adds a separate SSE opening
snapshot model profile. It is not the v0.4 50-factor model and must not be
mixed with it. The profile has two independent `legacy-residual-gru` models:
the baseline consumes the ordered 36-dimensional `snapshot36` vector, while
the Auction59 arm consumes the same 36 values followed by the ordered 59
dimensional `auction59-v1.1.0` sidecar. Both arms use their own 64-element
hidden state and are advanced for every accepted row. A native, endian-stable
`SSESGRU1` artifact is produced offline from the supplied PyTorch state-dict
zip; PyTorch is never linked by the serving process.

The handoff's `best.pt` is the checkpoint authority; the included `model.pt`
exports are not assumed byte-equivalent and are only used when explicitly
selected for diagnosis. `sse-t0/model/snapshot_gru_runtime.*` validates the
artifact tensor names, feature count, scaler dimensions and finite values,
then performs the exact float32 CPU forward pass. `snapshot_ensemble.*` applies the candidate routing
policy using exchange event time (not receive time), float64 accumulation and
one final float32 cast. It rejects non-SSE, out-of-window, invalid or
non-finite inputs and never silently falls back to one arm. The model API
accepts already constructed factor vectors; snapshot and Auction factor
generation remain separate upstream modules until their live contracts are
connected.

The newly uploaded `/home/external/v04-legacy-midmix-sse.tar.zst` is the
separate SSE tick/L2 model. It uses the frozen 50-factor
`v0.4-sse-cob-batch-end-100us` contract, has no external scaler, and consumes
only strict CompleteOrderBookSH batch-end rows where the next local timestamp
gap is greater than 100 microseconds. The existing `sse_model_runtime.*` is
validated against this package's CPU Float32 zero-input and 4,102-row
continuous golden fixtures; its `SSEMODL1` artifact is generated offline from
`model/state_dict.npz`.

`sse-t0/model/sse_hybrid_model.*` is the exchange-level orchestrator. It keeps
the tick state hot from the first accepted tick row and keeps the two Snapshot
states hot on their own Snapshot rows. Both model families are therefore
generated concurrently, but selection is a hard half-open policy: Snapshot
ensemble output is selected for `[09:30:00,09:35:00)`, and the tick output is
selected at `09:35:00` and later. A failed selected model rejects that output;
the other model is never used as a silent fallback.
