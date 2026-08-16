## Why

The Shenzhen runtime has accumulated overlapping configuration paths, an
experimental direct-sharded-ring implementation compiled into the production MD
library, and repeated recovery cleanup logic. This makes deployment behavior
hard to reason about and increases the surface area for production regressions.

## What Changes

- Make the current two-layer configuration (`sze_system.json` plus the dated
  daily static JSON) the only production path. **BREAKING:** remove implicit
  legacy daily-config conversion from the normal launcher.
- Keep the direct sharded ring as an explicit experimental/test target, but do
  not compile or parse it in the default production `libsze_md.so` path.
- Centralize recovery resource teardown so initialization failure and normal
  shutdown use the same cleanup path.
- Preserve health pages, continuity gates, journal format, factor order,
  prediction values, and TD risk gates unchanged.

## Capabilities

### New Capabilities

- `sze-runtime-simplification`: defines the strict production configuration and
  build boundary for Shenzhen runtime components.

### Modified Capabilities

None.

## Impact

- Affects `deploy/sze_compact_daily/prepare_sze_runtime.py` and its launcher
  callers, `CMakeLists.txt`, `MDEngineSZE.*`, and recovery lifecycle helpers.
- Existing legacy all-in-one daily JSONs will require a one-time conversion
  before deployment; no market-data or strategy ABI changes are intended.
- Experimental direct-sharded-ring tests remain buildable through an explicit
  opt-in target.
