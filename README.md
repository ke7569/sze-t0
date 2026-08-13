# t0-sze

Independent Shenzhen market-data, order-book, recovery, factor, and prediction
project.

## Baseline

- Source identity: `88`
- Strategy build id: `sze-strategy-20260806-v04-source88-v1`
- Journal segment target: `1024 MiB`
- Minimum free space after journal allocation: `80 GiB`

## Layout

- `src/t0-main`: strategy, order book, factors, prediction, config tools
- `modules/deepwin_guoxin/md`: Shenzhen decoder, journal, SHM ring, tools
- `toolchain`: Deepwin, Boost, Python headers and link inputs
- `reference`: protocol and research references
- `models`: model inputs
- `testdata`: decoder and order/trade fixtures
- `deploy`: generated deployment artifacts, kept outside this source tree
- `sse-t0`: disposable Shanghai development child project. `strategy/` contains the HStrategy boundary, `market_observer/` contains the raw UDP feed observer, and `config/` contains credential-free templates.

## Rules

- Do not store live passwords, account credentials, journals, CSV output, or
  private keys in this repository.
- Trading gateway credentials must be injected through a root-only secrets file
  or environment at deployment time.
- Validate source-id, journal segment size, ABI, model hash, and config hash
  before replacing a live binary.
