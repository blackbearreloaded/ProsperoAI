#!/usr/bin/env python3
"""Benchmark every installed model on the PS5 test title for several rounds.

Each round visits all installed catalog models in an interleaved text/media order,
so every request is also a model switch (the path the native and web UI use).
Text models get a correctness check and a 128-token decode measurement; media
models use fixed prompts and their WAV/PNG output is downloaded and validated.
After every request the title must still be running; a crash is recorded and the
title is relaunched so the remaining models are still measured.
"""
import argparse
import array
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
import wave

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ps5ctl  # noqa: E402  (loads .env.ps5.local)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--artifacts', type=Path, required=True)
parser.add_argument('--models', nargs='*', help='limit to these model IDs (default: all installed)')
parser.add_argument('--label', default='all-models')
args = parser.parse_args()

host = os.environ['PS5_HOST']
title = os.environ['PROSPERO_TEST_TITLE']
base = f'http://{host}:11434'
ps5ctl_script = str(Path(__file__).with_name('ps5ctl.py'))

CHECK_PROMPT = 'What is 17 multiplied by 3? Answer with the number only.'
DECODE_PROMPT = 'Explain in about one hundred words why the sky is blue.'
MEDIA_PROMPTS = {
    1: 'A red apple on a wooden table, natural window light, photographic detail.',
    2: 'A short, clear bell chime with a soft resonant decay.',
    3: 'Hello from Prospero AI. This is a short speech test.',
}
MEDIA_PROMPT_OVERRIDES = {
    'realistic-vision-v51-fp16': 'A portrait of a woman in a garden, natural light, realistic photography.',
    'ace-step-1.5-turbo-q8-0': 'A short instrumental track with warm piano, soft strings, and a gentle steady rhythm.',
}


def request(path, body=None, timeout=1800):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data, headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.load(response)


def title_running():
    status = subprocess.run(['python3', ps5ctl_script, 'agent', 'procs'], text=True, capture_output=True)
    return f'title={title}' in status.stdout


def ensure_title_ready():
    if not title_running():
        subprocess.run(['python3', ps5ctl_script, 'agent', 'launch', title], text=True, capture_output=True)
    for attempt in range(120):
        try:
            request('/api/settings/runtime', timeout=5)
            return
        except Exception:
            time.sleep(1)
    raise RuntimeError(f'{title} did not start its HTTP API')


def download(sandbox_path, name):
    remote = sandbox_path.replace('/download0/ProsperoAI/', '/data/prosperoai/')
    local = args.artifacts / (name + Path(remote).suffix)
    ftp = ps5ctl.ftp_connect()
    with local.open('wb') as target:
        ftp.retrbinary('RETR ' + remote, target.write, blocksize=1024 * 1024)
    ftp.quit()
    data = local.read_bytes()
    return local, data, {'path': sandbox_path, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def validate_media(text, name):
    png = re.search(r'(/[^\s]+\.png)', text)
    wav = re.search(r'(/[^\s]+\.wav)', text)
    if png:
        _, data, info = download(png.group(1), name)
        assert data[:8] == b'\x89PNG\r\n\x1a\n' and data[12:16] == b'IHDR', 'not a PNG'
        info['width'], info['height'] = struct.unpack_from('>II', data, 16)
        return info
    assert wav, f'no media path in response: {text[:200]}'
    local, _, info = download(wav.group(1), name)
    with wave.open(str(local)) as audio:
        frames = audio.getnframes()
        samples = array.array('h', audio.readframes(frames))
        info.update(rate=audio.getframerate(), channels=audio.getnchannels(),
                    seconds=round(frames / audio.getframerate(), 3))
    info['peak'] = max(abs(x) for x in samples) / 32768
    info['clipped_fraction'] = sum(abs(x) >= 32767 for x in samples) / len(samples)
    assert info['peak'] > 0 and info['seconds'] >= 0.25, f'silent or too short audio: {info}'
    return info


def run_text(model):
    check = request('/v1/chat/completions', {
        'model': model, 'messages': [{'role': 'user', 'content': CHECK_PROMPT}],
        'temperature': 0, 'max_tokens': 256, 'context_tokens': 4096})
    answer = check['choices'][0]['message']['content']
    decode = request('/v1/chat/completions', {
        'model': model, 'messages': [{'role': 'user', 'content': DECODE_PROMPT}],
        'temperature': 0, 'max_tokens': 128, 'context_tokens': 4096})
    usage = decode.get('usage', {})
    record = {'answer': answer.strip()[:200], 'correct': '51' in answer,
              'decode_tokens': usage.get('completion_tokens'),
              'tokens_per_second': usage.get('tokens_per_second'),
              'generation_ms': usage.get('generation_time_ms'),
              'decode_text': decode['choices'][0]['message']['content'].strip()[:160]}
    assert record['correct'], f'wrong answer: {answer[:120]!r}'
    assert usage.get('completion_tokens', 0) > 0, 'no decode tokens'
    return record


def run_media(model, kind, name):
    prompt = MEDIA_PROMPT_OVERRIDES.get(model, MEDIA_PROMPTS[kind])
    response = request('/api/generate', {'model': model, 'prompt': prompt, 'stream': False,
                                         'options': {'num_predict': 64}})
    if response.get('error'):
        raise RuntimeError(response['error'])
    return {'prompt': prompt, 'output': validate_media(response['response'], name)}


def interleave(presets):
    text = [p for p in presets if p['kind'] == 0]
    media = [p for p in presets if p['kind'] != 0]
    order = []
    while text or media:
        if text:
            order.append(text.pop(0))
        if media:
            order.append(media.pop(0))
    return order


args.artifacts.mkdir(parents=True, exist_ok=True)
args.output.parent.mkdir(parents=True, exist_ok=True)
ensure_title_ready()
original = request('/api/settings/runtime')
request('/api/settings/runtime', {'context': '4k', 'thinking': False, 'kv': 'auto'})
presets = [p for p in request('/api/models/presets')['data'] if p.get('installed')]
if args.models:
    presets = [p for p in presets if p['id'] in args.models]
order = interleave(presets)
result = {'label': args.label, 'test_title': title, 'rounds': args.rounds,
          'order': [p['id'] for p in order],
          'method': 'each round visits every installed model in interleaved text/media order; '
                    'every request is a model switch; HTTP time includes model loading',
          'runs': []}


def save():
    args.output.write_text(json.dumps(result, indent=2) + '\n')


try:
    for round_index in range(1, args.rounds + 1):
        for preset in order:
            model, kind = preset['id'], preset['kind']
            ensure_title_ready()
            print(f'round {round_index} {model}', flush=True)
            start = time.monotonic()
            record = {'round': round_index, 'model': model, 'kind': kind}
            try:
                if kind == 0:
                    record.update(run_text(model))
                else:
                    record.update(run_media(model, kind, f'{model}-r{round_index}'))
                record['passed'] = True
            except Exception as error:
                record['passed'] = False
                record['failure'] = f'{type(error).__name__}: {error}'[:400]
            record['http_seconds'] = round(time.monotonic() - start, 3)
            record['title_alive'] = title_running()
            if not record['title_alive']:
                record['passed'] = False
                record.setdefault('failure', 'title exited during the request')
            result['runs'].append(record)
            save()
            print('  ', 'PASS' if record['passed'] else 'FAIL', record['http_seconds'], 's',
                  record.get('tokens_per_second') or record.get('output', {}).get('seconds') or '',
                  record.get('failure', ''), flush=True)
finally:
    summary = {}
    for preset in order:
        runs = [r for r in result['runs'] if r['model'] == preset['id']]
        if not runs:
            continue
        entry = {'passed': sum(r['passed'] for r in runs), 'runs': len(runs),
                 'median_http_seconds': statistics.median(r['http_seconds'] for r in runs)}
        speeds = [r['tokens_per_second'] for r in runs if r.get('tokens_per_second')]
        if speeds:
            entry['median_tokens_per_second'] = round(statistics.median(speeds), 2)
        summary[preset['id']] = entry
    result['summary'] = summary
    result['passed'] = bool(result['runs']) and all(r['passed'] for r in result['runs'])
    try:
        restore = {k: original[k] for k in ('context', 'thinking', 'kv') if k in original}
        request('/api/settings/runtime', restore)
        result['restored_settings'] = restore
    except Exception as error:
        result['restore_failure'] = str(error)
    save()
    for model, entry in summary.items():
        print(f"{model:40s} {entry['passed']}/{entry['runs']}  http={entry['median_http_seconds']:.1f}s"
              + (f"  {entry['median_tokens_per_second']} tok/s" if 'median_tokens_per_second' in entry else ''))
