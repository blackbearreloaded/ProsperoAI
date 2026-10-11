#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Summarize completed console media benchmarks and fixed-fixture artifacts."""
import argparse
import array
import json
import math
from pathlib import Path
import struct
import sys
import wave


def samples(path):
    path = Path(path)
    if path.suffix == '.wav':
        with wave.open(str(path)) as audio:
            shape = (audio.getnchannels(), audio.getframerate(), audio.getnframes())
            if audio.getsampwidth() != 2:
                raise ValueError('Expected PCM16')
            values = array.array('h', audio.readframes(audio.getnframes()))
            if sys.byteorder != 'little':
                values.byteswap()
        return shape, values, 32767
    data = path.read_bytes()
    width, height = struct.unpack_from('<HH', data, 12)
    if data[2] != 2 or data[16] != 32:
        raise ValueError('Expected uncompressed BGRA TGA')
    pixels = data[18 + data[0]:]
    if len(pixels) != width * height * 4:
        raise ValueError('Unexpected TGA size')
    # Normalize TGA horizontal/vertical origin before pixel comparison.
    rows = [pixels[y * width * 4:(y + 1) * width * 4] for y in range(height)]
    if not data[17] & 0x20:
        rows.reverse()
    values = bytearray()
    for row in rows:
        indices = range(width - 1, -1, -1) if data[17] & 0x10 else range(width)
        for x in indices:
            values.extend(row[x * 4:x * 4 + 3])
    return (width, height, 3), values, 255


def difference(left, right):
    shape, a, full_scale = samples(left['artifact'])
    other_shape, b, other_scale = samples(right['artifact'])
    if shape != other_shape or full_scale != other_scale or len(a) != len(b):
        raise ValueError(f'Artifact shapes differ: {shape} / {other_shape}')
    n = len(a)
    mse = sum((x - y) ** 2 for x, y in zip(a, b)) / n
    rmse = math.sqrt(mse)
    mean_a, mean_b = sum(a) / n, sum(b) / n
    variance_a = sum((x - mean_a) ** 2 for x in a)
    variance_b = sum((x - mean_b) ** 2 for x in b)
    denominator = math.sqrt(variance_a * variance_b)
    return {'shape': shape, 'file_sha256_equal': left['sha256'] == right['sha256'],
            'sample_equal': a == b,
            'mean_absolute_error': sum(abs(x - y) for x, y in zip(a, b)) / n,
            'rmse': rmse, 'normalized_rmse': rmse / full_scale,
            'psnr_db': 20 * math.log10(full_scale / rmse) if rmse else None,
            'correlation': sum((x - mean_a) * (y - mean_b) for x, y in zip(a, b)) / denominator if denominator else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('agc', type=Path)
    parser.add_argument('vulkan', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    agc, vulkan = (json.loads(path.read_text()) for path in (args.agc, args.vulkan))
    if not agc.get('passed') or not vulkan.get('passed'):
        parser.error('Both complete functional benchmark runs must pass')
    if agc['fixtures'] != vulkan['fixtures']:
        parser.error('Fixtures differ')
    report = {'inputs': [str(args.agc), str(args.vulkan)], 'fixtures': agc['fixtures'],
              'limitations': ['End-to-end builds, not isolated kernel benchmarks.',
                              'SD repeats retain context; audio/TTS entrypoints may reload weights. OS caches are uncontrolled.',
                              'Artifact metrics do not establish perceptual quality.',
                              'Peak memory and sustained thermals are not measured.'], 'models': {}}
    for model in agc['summary']:
        a, b = agc['summary'][model], vulkan['summary'][model]
        if len(a['repeat_http_seconds']) < 3 or len(b['repeat_http_seconds']) < 3:
            parser.error('At least three repeats per model are required')
        pairs = []
        for run in (r for r in agc['runs'] if r['model'] == model):
            other = next(r for r in vulkan['runs'] if r['model'] == model and r['phase'] == run['phase'])
            if run['request'] != other['request']:
                parser.error('Requests differ')
            pairs.append({'phase': run['phase'], **difference(run['output'], other['output'])})
        item = {'agc': a, 'vulkan': b,
                'speedup_agc_over_vulkan': a['median_repeat_http_seconds'] / b['median_repeat_http_seconds'],
                'artifact_pairs': pairs}
        for label, source in [('agc', agc), ('vulkan', vulkan)]:
            timings = [r['response']['eval_duration'] / 1e9 for r in source['runs']
                       if r['model'] == model and r['phase'] != 'initial']
            item[label + '_model_reported_seconds'] = timings
            item[label + '_median_model_reported_seconds'] = sorted(timings)[len(timings) // 2]
        first = next(r for r in agc['runs'] if r['model'] == model)
        if 'seconds' in first['output']:
            item['output_seconds'] = first['output']['seconds']
            item['agc_real_time_factor'] = a['median_repeat_http_seconds'] / item['output_seconds']
            item['vulkan_real_time_factor'] = b['median_repeat_http_seconds'] / item['output_seconds']
        report['models'][model] = item
        print(model, f"AGC {a['median_repeat_http_seconds']:.3f}s; Vulkan {b['median_repeat_http_seconds']:.3f}s; {item['speedup_agc_over_vulkan']:.3f}x")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
