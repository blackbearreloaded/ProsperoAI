#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare fixed media fixtures on an already deployed test build."""
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
import time
import urllib.request
import wave
import ps5ctl

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--label', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--artifacts', type=Path, required=True)
parser.add_argument('--warm-repeats', type=int, default=3)
parser.add_argument('--models', nargs='+', default=['sd-turbo-fp16', 'stable-audio-open-small', 'kokoro-82m-fp16'])
args = parser.parse_args()
if not os.environ.get('PS5_HOST'):
    parser.error('PS5_HOST is required')
title = os.environ.get('PROSPERO_TEST_TITLE')
if not title:
    parser.error('PROSPERO_TEST_TITLE is required')
if args.warm_repeats < 0:
    parser.error('warm repeats must be nonnegative')
base = 'http://' + os.environ['PS5_HOST'] + ':11434'
status = subprocess.check_output(['python3', str(Path(__file__).with_name('ps5ctl.py')), 'agent', 'status'], text=True)
assert f'title={title}' in status, status
fixtures = {
    'sd-turbo-fp16': 'A red apple on a wooden table, natural window light, photographic detail.',
    'stable-audio-open-small': 'Gentle acoustic guitar melody with soft warm notes, no speech.',
    'kokoro-82m-fp16': 'Hello from Prospero AI.'}
args.artifacts.mkdir(parents=True, exist_ok=True)
args.output.parent.mkdir(parents=True, exist_ok=True)


def request(path, body=None):
    req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(),
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=1800) as response:
        return json.load(response)


for attempt in range(60):
    try:
        original = request('/api/settings/runtime')
        break
    except urllib.error.URLError:
        if attempt == 59:
            raise
        time.sleep(0.5)
result = {'label': args.label, 'test_title': title, 'original_settings': original,
          'method': f'one initial request per model then {args.warm_repeats} repeated requests; elapsed HTTP includes actual runtime loading; no console transfers during generation',
          'fixtures': {'sd': {'width': 512, 'height': 512, 'steps': 1, 'seed': 42, 'cfg': 1},
                       'audio': {'latent_length': 256, 'expected_rate': 44100}, 'tts': {'expected_rate': 24000}},
          'runs': []}
result['switch_checks'] = []


def save():
    args.output.write_text(json.dumps(result, indent=2) + '\n')


def text_switch(name):
    body = {'model': 'Mistral-7B-Instruct-v0.3.Q4_0.gguf',
            'messages': [{'role': 'user', 'content': 'What is two plus two? Answer briefly.'}],
            'temperature': 0, 'max_tokens': 32, 'context_tokens': 4096}
    response = request('/v1/chat/completions', body)
    result['switch_checks'].append({'name': name, 'request': body, 'response': response})
    save()
    answer = response['choices'][0]['message']['content']
    assert 'four' in answer.lower() or '4' in answer, answer
    print('Text switch passed', name, flush=True)


def artifact(text, name):
    match = re.search(r'(/[^\s]+\.(?:tga|wav))', text)
    assert match, text
    sandbox = match.group(1)
    remote = sandbox.replace('/download0/ProsperoAI/', '/data/prosperoai/').replace('/download0/', '/data/prosperoai/work/tmp/')
    local = args.artifacts / (name + Path(remote).suffix)
    ftp = ps5ctl.ftp_connect()
    with local.open('wb') as target:
        ftp.retrbinary('RETR ' + remote, target.write, blocksize=1024 * 1024)
    ftp.quit()
    info = {'sandbox_path': sandbox, 'mapped_path': remote, 'artifact': str(local),
            'bytes': local.stat().st_size, 'sha256': hashlib.sha256(local.read_bytes()).hexdigest()}
    if local.suffix == '.tga':
        data = local.read_bytes()
        width, height = struct.unpack_from('<HH', data, 12)
        assert data[2] == 2 and (width, height) == (512, 512) and data[16] == 32
        assert len(data) == 18 + data[0] + width * height * 4
        colors = data[18 + data[0]:]
        # Alpha is constant. Only color channels determine non-uniformity.
        info['rgb_stddev'] = statistics.pstdev(x for i, x in enumerate(colors) if i % 4 != 3)
        assert info['rgb_stddev'] > 1, info
        info.update(width=width, height=height)
    else:
        with wave.open(str(local)) as audio:
            info.update(channels=audio.getnchannels(), rate=audio.getframerate(),
                        width=audio.getsampwidth(), frames=audio.getnframes())
            assert info['width'] == 2 and info['frames'] > 0
            samples = array.array('h', audio.readframes(info['frames']))
            info['peak'] = max(abs(x) for x in samples)
            info['rms'] = (sum(x * x for x in samples) / len(samples)) ** 0.5
            info['clipped_fraction'] = sum(abs(x) >= 32767 for x in samples) / len(samples)
            info['seconds'] = info['frames'] / info['rate']
            assert info['peak'] > 0 and info['rms'] > 0, info
    return info


try:
    request('/api/settings/runtime', {'context': '4k', 'thinking': False, 'kv': 'auto'})
    installed = {m['id'] for m in request('/v1/models')['data']}
    assert set(args.models).issubset(installed), installed
    text_switch('before-media')
    for model in args.models:
        for index in range(args.warm_repeats + 1):
            phase = 'initial' if index == 0 else f'repeat-{index}'
            print('Starting', args.label, model, phase, flush=True)
            body = {'model': model, 'prompt': fixtures[model], 'stream': False, 'options': {'num_predict': 64}}
            start = time.monotonic()
            response = request('/api/generate', body)
            elapsed = time.monotonic() - start
            record = {'model': model, 'phase': phase, 'request': body, 'response': response, 'http_seconds': elapsed}
            result['runs'].append(record)
            save()
            assert not response.get('error'), response
            record['output'] = artifact(response['response'], model + '-' + phase)
            if model == 'stable-audio-open-small':
                assert record['output']['rate'] == 44100 and record['output']['channels'] == 2 and record['output']['frames'] == 524288
            if model == 'kokoro-82m-fp16':
                assert record['output']['rate'] == 24000 and record['output']['channels'] == 1
            save()
            print('Completed', model, phase, round(elapsed, 3), record['output'], flush=True)
        text_switch('after-' + model)
    result['summary'] = {model: {
        'initial_http_seconds': next(r['http_seconds'] for r in result['runs'] if r['model'] == model and r['phase'] == 'initial'),
        'repeat_http_seconds': [r['http_seconds'] for r in result['runs'] if r['model'] == model and r['phase'] != 'initial'],
        'median_repeat_http_seconds': statistics.median(r['http_seconds'] for r in result['runs'] if r['model'] == model and r['phase'] != 'initial') if args.warm_repeats else None}
        for model in args.models}
    result['passed'] = True
finally:
    request('/api/settings/runtime', original)
    result['restored_settings'] = request('/api/settings/runtime')
    save()
