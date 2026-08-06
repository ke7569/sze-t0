# Trading Gateway Integration Note

## Current finding

The live Shenzhen deployment is currently capture/recovery/prediction shadow
only. Its main configurations have empty `vmd` and `vtd` arrays and use
`capture_only=true`. No live Shenzhen TD plugin, gateway implementation, or
account configuration was found under `/home/zane`, `/home/zane/run_main`, or
the independent `t0-sze` source tree.

The source tree contains the Deepwin `ITDEngine` interface and a virtual-live
example referring to `libsze_td.so`, but that library is not deployed and the
example is not a production gateway implementation.

## Target account

The requested target is the Yuheng neutral fund account under `branch_id=1700`.
Credentials and account numbers are intentionally not stored in this project.
They must be supplied through a root-only deployment secret.

## Endpoint roles

- Shenzhen ordinary spot: two failover endpoints, port 32001
- Shenzhen spot IX: two failover endpoints, port 32002
- Shanghai spot IX: two failover endpoints, port 32002
- The Yuheng account uses the spot IX path, with Shenzhen/Shanghai allocation
  0.5/0.5 and Shanghai as the default deposit/withdrawal node.

## Required implementation before activation

1. Identify or obtain the vendor TD plugin and its ABI-compatible build.
2. Confirm whether the plugin expects one `vtd` source or separate Shenzhen and
   Shanghai sources.
3. Implement endpoint failover, login, heartbeat, reconnect, query, order,
   cancel, and response correlation.
4. Add a root-only secret file containing account credentials; do not put them
   into JSON committed to Git or into process command lines.
5. Add a dry-run/login-only configuration and an order-disabled integration
   test. Do not enable order submission from the shadow recovery strategy.
6. Record the exact TD source id, plugin SHA256, endpoint configuration hash,
   and account alias in startup logs without printing secrets.
