#!/usr/bin/env python3
import argparse
import struct
import zipfile
from pathlib import Path


def array(archive, name):
    payload = archive.read(name)
    major = payload[6]
    size = 2 if major == 1 else 4
    header_len = struct.unpack_from('<H' if size == 2 else '<I', payload, 8)[0]
    start = 8 + size + header_len
    data = payload[start:]
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('bundle', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    source = args.bundle / 'golden' / 'zero_raw_input.npz'
    with zipfile.ZipFile(source) as archive:
        raw = array(archive, 'input_raw.npy')
        prediction = array(archive, 'expected_prediction_cpu_fp32.npy')
        hidden = array(archive, 'expected_final_hidden_cpu_fp32.npy')
    rows = len(prediction) // 4
    if len(raw) != rows * 36 * 4 or len(hidden) != 64 * 4:
        raise ValueError('unexpected golden shape')
    with args.output.open('wb') as out:
        out.write(b'S15GOLD1')
        out.write(struct.pack('<I', rows))
        out.write(raw)
        out.write(prediction)
        out.write(hidden)
    print('wrote', args.output, 'rows', rows)


if __name__ == '__main__':
    main()
