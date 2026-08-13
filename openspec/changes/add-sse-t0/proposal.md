# Add SSE T0

Add Shanghai Stock Exchange T0 support as the disposable `sse-t0/` child project of `sze-t0`, while reusing compatible strategy lifecycle, market-data channel, order-book, and Deepwin ABI code. SSE keeps its own factor ABI and native model runtime; it does not reuse the SZE predictor/factor contract. The first runtime targets the supplied `v04-legacy-midmix-sse` model and deployment-injected server settings.

The existing SZE library remains Shenzhen-only; SSE is exposed as a separate strategy library and configuration contract. A temporary raw UDP observer records one JSONL row per datagram from each configured Shanghai multicast channel so feed batching, channel ordering, and packet metadata can be studied before SSE strategy optimization.
