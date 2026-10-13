#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke-test every installed text preset on a running PS5 test title."""
import argparse
import json
import os
from pathlib import Path
import time
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
host = os.environ.get('PS5_HOST')
title = os.environ.get('PROSPERO_TEST_TITLE')
if not host or not title:
    parser.error('PS5_HOST and PROSPERO_TEST_TITLE are required')
base = f'http://{host}:11434'


def request(path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data,
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=900) as response:
        return json.load(response)


original = request('/api/settings/runtime')
result = {'title_id': title, 'method': 'greedy arithmetic smoke, one request per installed text preset',
          'original_settings': original, 'models': []}
args.output.parent.mkdir(parents=True, exist_ok=True)


def save():
    args.output.write_text(json.dumps(result, indent=2) + '\n')


try:
    request('/api/settings/runtime', {'context': '4k', 'thinking': False, 'kv': 'auto'})
    presets = request('/api/models/presets')['data']
    installed = [p for p in presets if p['kind'] == 0 and p['installed']]
    if not installed:
        raise RuntimeError('No installed text presets were reported by the test title')
    for preset in installed:
        model = preset['id']
        body = {'model': model, 'prompt': 'What is 2 plus 2? Answer with the number only.',
                'stream': False, 'options': {'num_predict': 32, 'temperature': 0}}
        start = time.monotonic()
        response = request('/api/generate', body)
        elapsed = time.monotonic() - start
        answer = response.get('response', '')
        passed = not response.get('error') and ('4' in answer or 'four' in answer.lower())
        record = {'id': model, 'name': preset['name'], 'http_seconds': elapsed,
                  'request': body, 'response': response, 'passed': passed}
        result['models'].append(record)
        save()
        print(json.dumps({'model': model, 'seconds': round(elapsed, 3),
                          'passed': passed, 'answer': answer[:180]}), flush=True)
        if not passed:
            raise RuntimeError(f'Text smoke failed for {model}: {answer!r}')
    result['passed'] = True
except Exception as error:
    result['passed'] = False
    result['failure'] = {'type': type(error).__name__, 'message': str(error)}
    raise
finally:
    try:
        request('/api/settings/runtime', original)
        result['restored_settings'] = request('/api/settings/runtime')
    except Exception as error:
        result['restore_error'] = {'type': type(error).__name__, 'message': str(error)}
    save()
