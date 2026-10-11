#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Resident Bonsai benchmark and text regression on an already deployed test app."""
import argparse
import json
import os
from pathlib import Path
import statistics
import time
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--include-qwen', action='store_true')
args = parser.parse_args()
host = os.environ.get('PS5_HOST')
if not host:
    parser.error('PS5_HOST is required')
title = os.environ.get('PROSPERO_TEST_TITLE')
if not title:
    parser.error('PROSPERO_TEST_TITLE is required')
base = f'http://{host}:11434'
model = 'Ternary-Bonsai-2-27B-PTQ1_0.gguf'


def request(path, body=None):
    req = urllib.request.Request(base + path,
        data=None if body is None else json.dumps(body).encode(),
        headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=300) as response:
        return json.load(response)


original = request('/api/settings/runtime')
result = {'runtime': 'Prism e8fd209c975c80aa7fb36a8423571ff3b1b7e955',
    'title': title, 'method': 'resident Vulkan, greedy, 4K, automatic KV; warm repeats; no model transfer during timing',
    'original_settings': original, 'presets': request('/api/models/presets'), 'runs': []}
args.output.parent.mkdir(parents=True, exist_ok=True)


def save():
    args.output.write_text(json.dumps(result, indent=2) + '\n')


def run(name, prompt, selected=model, thinking=False, context='4k', limit=256):
    request('/api/settings/runtime', {'context': context, 'thinking': thinking, 'kv': 'auto'})
    body = {'model': selected, 'prompt': prompt, 'stream': False,
            'options': {'num_predict': limit, 'temperature': 0}}
    start = time.monotonic()
    response = request('/api/generate', body)
    elapsed = time.monotonic() - start
    assert not response.get('error'), response
    count, duration = response['eval_count'], response['eval_duration']
    record = {'name': name, 'context': context, 'thinking': thinking,
        'request': body, 'response': response, 'http_seconds': elapsed,
        'decode_tokens_per_second': count * 1e9 / duration if duration else None}
    result['runs'].append(record)
    save()
    print(json.dumps(record), flush=True)
    return response['response']


try:
    assert any(p['name'] == 'Ternary Bonsai 2 27B PTQ1_0' and p['installed']
               for p in result['presets']['data']), result['presets']
    text = run('cold-correctness', 'What is two plus two? Give a one-word answer.', limit=64)
    assert 'four' in text.lower() or '4' in text, text
    prompt = 'Explain why the sky appears blue during the day in three concise sentences.'
    for index in range(3):
        text = run(f'warm-benchmark-{index}', prompt)
        assert 'scatter' in text.lower() and '<think>' not in text, text
    repeats = result['runs'][1:4]
    assert all(r['response']['eval_count'] >= 64 for r in repeats)
    result['benchmark'] = {'runs': 3,
        'median_decode_tokens_per_second': statistics.median(r['decode_tokens_per_second'] for r in repeats),
        'decode_tokens_per_second': [r['decode_tokens_per_second'] for r in repeats],
        'prompt_tokens_per_second': [r['response']['prompt_eval_count'] * 1e9 / r['response']['prompt_eval_duration'] for r in repeats],
        'generated_tokens': [r['response']['eval_count'] for r in repeats]}
    text = run('thinking', 'A box has three red balls and two blue balls. What fraction of the balls is blue? Explain briefly.', thinking=True, limit=768)
    assert text.startswith('<think>\n') and '</think>' in text and '2/5' in text, text
    run('context-16k', 'Write a short greeting.', context='16k', limit=64)
    old = ['Mistral-7B-Instruct-v0.3.Q4_0.gguf', 'minicpm5-2b-q8-0.gguf']
    if args.include_qwen:
        old.append('qwen3.5-9b-q4-0.gguf')
    for selected in old:
        text = run('regression-' + selected, 'What is two plus two? Answer briefly.', selected=selected, limit=64)
        assert 'four' in text.lower() or '4' in text, text
    text = run('return-bonsai', 'What is two plus two? Answer briefly.', limit=64)
    assert 'four' in text.lower() or '4' in text, text
    result['passed'] = True
finally:
    request('/api/settings/runtime', original)
    result['restored_settings'] = request('/api/settings/runtime')
    save()
