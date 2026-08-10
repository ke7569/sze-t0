#!/usr/bin/env python3
"""Convert the frozen NPZ state_dict into a small, endian-stable FP32 artifact."""
import argparse
import json
import re
import struct
import zipfile
from pathlib import Path

MAGIC = b"SZE15GRU"
VERSION = 1
EXPECTED = {
    "proj.weight": (128, 36), "proj.bias": (128,),
    "feature_layers.0.weight": (512, 36), "feature_layers.0.bias": (512,),
    "feature_layers.2.weight": (256, 512), "feature_layers.2.bias": (256,),
    "feature_layers.4.weight": (128, 256), "feature_layers.4.bias": (128,),
    "gru.weight_ih_l0": (192, 128), "gru.weight_hh_l0": (192, 64),
    "gru.bias_ih_l0": (192,), "gru.bias_hh_l0": (192,),
    "res_gru.weight": (64, 128), "res_gru.bias": (64,),
    "ln.weight": (64,), "ln.bias": (64,),
    "prediction_head.0.weight": (8, 64), "prediction_head.0.bias": (8,),
    "prediction_head.2.weight": (1, 8), "prediction_head.2.bias": (1,),
}


def read_npy(payload: bytes):
    if payload[:6] != b"\x93NUMPY":
        raise ValueError("invalid npy magic")
    major, minor = payload[6], payload[7]
    header_len_size = 2 if major == 1 else 4
    header_start = 8 + header_len_size
    fmt = "<H" if header_len_size == 2 else "<I"
    header_len = struct.unpack_from(fmt, payload, 8)[0]
    header = payload[header_start:header_start + header_len].decode("latin1")
    descr = re.search(r"'descr'\s*:\s*'([^']+)'", header).group(1)
    fortran = re.search(r"'fortran_order'\s*:\s*(True|False)", header).group(1)
    shape_text = re.search(r"'shape'\s*:\s*\(([^)]*)\)", header).group(1)
    shape = tuple(int(x.strip()) for x in shape_text.split(",") if x.strip())
    if descr not in ("<f4", "|f4") or fortran != "False":
        raise ValueError("only C-order float32 arrays are supported")
    data = payload[header_start + header_len:]
    count = 1
    for dim in shape:
        count *= dim
    if len(data) != count * 4:
        raise ValueError("npy payload length mismatch")
    return shape, data


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    npz = args.bundle / "model" / "state_dict.npz"
    arrays = {}
    with zipfile.ZipFile(npz) as archive:
        for name, expected_shape in EXPECTED.items():
            shape, data = read_npy(archive.read(name + ".npy"))
            if shape != expected_shape:
                raise ValueError(f"{name}: shape {shape}, expected {expected_shape}")
            arrays[name] = data
    with args.output.open("wb") as out:
        out.write(MAGIC)
        out.write(struct.pack("<IIII", VERSION, 36, 64, len(EXPECTED)))
        for name, shape in EXPECTED.items():
            encoded = name.encode("ascii")
            out.write(struct.pack("<H", len(encoded)))
            out.write(encoded)
            out.write(struct.pack("<B", len(shape)))
            out.write(b"\0\0\0")
            for dim in shape:
                out.write(struct.pack("<I", dim))
            out.write(struct.pack("<Q", len(arrays[name])))
            out.write(arrays[name])
    scaler = json.loads((args.bundle / "model" / "scaler.json").read_text())
    if len(scaler["mean"]) != 36 or len(scaler["scale"]) != 36:
        raise ValueError("scaler must contain 36 mean/scale values")
    print(f"wrote {args.output} tensors={len(arrays)} bytes={args.output.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
