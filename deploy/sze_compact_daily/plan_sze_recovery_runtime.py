#!/usr/bin/env python3
"""Create ephemeral recovery worker configs from one daily SZE config."""
import copy
import json
import os
import sys


def write_json(path, value):
    with open(path, "w") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: plan_sze_recovery_runtime.py DAILY_JSON RUNTIME_DIR")
    daily_path, runtime_dir = sys.argv[1:]
    base = json.load(open(daily_path))
    count = int(base.get("worker_count", 8))
    cpus = list(base.get("worker_cpus", []))
    state_cpus = list(base.get("worker_state_cpus", []))
    if count < 1 or len(cpus) != count or len(state_cpus) != count:
        raise SystemExit("worker_count/worker_cpus/worker_state_cpus mismatch")
    params = base.get("ins_params") or {}
    run_main = os.environ.get("SZE_RUN_MAIN", "/home/zane/run_main")
    cpu_to_worker = {int(cpu): index for index, cpu in enumerate(cpus)}
    groups = [[] for _ in range(count)]
    for code in sorted(params):
        item = params[code]
        cpu = int(item.get("cpu", cpus[0]))
        if cpu not in cpu_to_worker:
            raise SystemExit("instrument cpu is not in worker_cpus: {} {}".format(code, cpu))
        groups[cpu_to_worker[cpu]].append(code)
    os.makedirs(runtime_dir, exist_ok=True)
    config_dir = os.path.join(runtime_dir, "configs")
    os.makedirs(config_dir)
    capture = base.get("sze_prediction_capture", {})
    for worker, codes in enumerate(groups):
        config = copy.deepcopy(base)
        config["strategy_name"] = "{}_worker{:02d}".format(
            base.get("strategy_name", "sze_recovery"), worker)
        config["ins_params"] = {code: params[code] for code in codes}
        consumer = config["sze_recovery_consumer"]
        consumer["state_cpu"] = int(state_cpus[worker])
        consumer["strategy_cpu"] = int(cpus[worker])
        config["sze_prediction_capture"] = copy.deepcopy(capture)
        config["sze_prediction_capture"]["directory"] = os.path.join(
            capture.get("directory", "/home/zane/run_main/log/sze_all"),
            "worker_{:02d}".format(worker))
        config["sze_prediction_capture"]["prefix"] = "worker_{:02d}".format(worker)
        config["sze_prediction_capture"]["instruments"] = codes
        write_json(os.path.join(config_dir, "config_worker_{:02d}.json".format(worker)), config)
        write_json(os.path.join(config_dir, "main_worker_{:02d}.conf".format(worker)), {
            "base_rid": 1200000 + worker,
            "vmd": [],
            "vtd": [],
            "vstr": [{"lib": os.path.join(run_main, "libt0_strategy_sze.so"),
                      "config": os.path.join(config_dir,
                                              "config_worker_{:02d}.json".format(worker))}],
            "zmq": "tcp://127.0.0.1:{}".format(6570 + worker),
        })
    write_json(os.path.join(runtime_dir, "worker_manifest.json"), {
        "trading_day": base.get("trading_day"),
        "worker_count": count,
        "workers": [{"worker": i, "cpu": int(cpus[i]),
                      "state_cpu": int(state_cpus[i]),
                      "instrument_count": len(groups[i])}
                     for i in range(count)],
    })


if __name__ == "__main__":
    main()
