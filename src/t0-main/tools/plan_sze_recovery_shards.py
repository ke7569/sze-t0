#!/usr/bin/env python3
"""Split an all-market Shenzhen recovery config into stable shadow shards."""

import argparse
import copy
import json
import os


def write_json(path, value):
    parent = os.path.dirname(os.path.abspath(path))
    if not os.path.isdir(parent):
        os.makedirs(parent)
    with open(path, "w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def fnv1a(text):
    value = 1469598103934665603
    for byte in text.encode("ascii"):
        value ^= byte
        value = (value * 1099511628211) & 0xffffffffffffffff
    return value


def normalized_symbol(code):
    text = str(code)
    return text if "." in text else text + ".SZ"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-json", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--shards", type=int, default=8)
    parser.add_argument("--state-cpu-base", type=int, default=16)
    parser.add_argument("--capture-directory", required=True)
    args = parser.parse_args()
    if args.shards < 1 or args.shards > 16:
        raise ValueError("shards must be in [1,16]")
    with open(args.input_json, encoding="utf-8") as stream:
        base = json.load(stream)
    codes = list(base.get("instrument_id") or [])
    amounts = list(base.get("his_amt") or [])
    params = base.get("ins_params") or {}
    if not codes or len(codes) != len(amounts):
        raise ValueError("instrument_id and his_amt must be non-empty and equal length")
    groups = [[] for _ in range(args.shards)]
    for index, code in enumerate(codes):
        symbol = normalized_symbol(code)
        if symbol not in params:
            raise ValueError("missing ins_params for " + symbol)
        groups[fnv1a(symbol) % args.shards].append((str(code), amounts[index], symbol))

    manifest = {"algorithm": "fnv1a64(normalized_symbol)%shards",
                "shards": args.shards, "source_config": os.path.basename(args.input_json),
                "assignments": []}
    for shard_id, entries in enumerate(groups):
        config = copy.deepcopy(base)
        config["strategy_name"] = "{}_shard{:02d}".format(
            base.get("strategy_name", "sze_recovery"), shard_id)
        config["instrument_id"] = [entry[0] for entry in entries]
        config["his_amt"] = [entry[1] for entry in entries]
        config["static_position"] = [0] * len(entries)
        config["last_position"] = [0] * len(entries)
        config["ins_params"] = {entry[2]: params[entry[2]] for entry in entries}
        consumer = config["sze_recovery_consumer"]
        consumer["state_cpu"] = args.state_cpu_base + shard_id
        consumer["strategy_cpu"] = args.state_cpu_base + args.shards + shard_id
        capture = config["mix153060_capture"]
        capture["directory"] = os.path.join(args.capture_directory,
                                             "shard_{:02d}".format(shard_id))
        capture["prefix"] = "shard_{:02d}".format(shard_id)
        capture["instruments"] = [entry[2] for entry in entries]
        name = "config_sze_recovery_shard_{:02d}.json".format(shard_id)
        write_json(os.path.join(args.output_dir, name), config)
        trading_day = config["sze_recovery_consumer"]["trading_day"]
        write_json(os.path.join(
            args.output_dir, "main_sze_recovery_shard_{:02d}.conf".format(shard_id)),
            {"base_rid": 1200100 + shard_id, "vmd": [], "vtd": [],
             "vstr": [{"lib": "./libt0_strategy_sze.so",
                       "config": "./configs/sze_all_{}/shards/".format(trading_day) + name}],
             "zmq": "tcp://127.0.0.1:{}".format(6570 + shard_id)})
        manifest["assignments"].append({"shard": shard_id,
                                        "config": name,
                                        "state_cpu": consumer["state_cpu"],
                                        "strategy_cpu": consumer["strategy_cpu"],
                                        "symbol_count": len(entries),
                                        "symbols": [entry[2] for entry in entries]})
    write_json(os.path.join(args.output_dir, "sze_shard_manifest.json"), manifest)
    print("planned {} symbols across {} shards".format(len(codes), args.shards))


if __name__ == "__main__":
    main()
