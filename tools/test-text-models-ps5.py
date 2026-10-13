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
parser.add_argument('--passes', type=int, default=1,
                    help='number of full benchmark passes over installed text presets')
parser.add_argument('--exclude-model', action='append', default=[],
                    help='skip a model id (may be repeated)')
args = parser.parse_args()
if args.passes < 1:
    parser.error('--passes must be at least one')
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
result = {'title_id': title, 'method': 'greedy arithmetic smoke, repeated full passes over every installed text preset',
          'passes': args.passes, 'excluded_models': args.exclude_model,
          'original_settings': original, 'models': []}
args.output.parent.mkdir(parents=True, exist_ok=True)


def save():
    args.output.write_text(json.dumps(result, indent=2) + '\n')


try:
    request('/api/settings/runtime', {'context': '4k', 'thinking': False, 'kv': 'auto'})
    presets = request('/api/models/presets')['data']
    installed = {p['id']: p['name'] for p in presets
                 if p['kind'] == 0 and p['installed'] and p['id'] not in args.exclude_model}
    # Include supported standalone GGUFs already discovered by the runtime, such as
    # Mistral, while the preset is being added to an existing installation.
    for model in request('/v1/models')['data']:
        if model.get('purpose') == 'text-to-text':
            if model['id'] not in args.exclude_model:
                installed.setdefault(model['id'], model.get('name', model['id']))
    if not installed:
        raise RuntimeError('No installed text presets were reported by the test title')
    for pass_index in range(1, args.passes + 1):
        for model, name in installed.items():
            body = {'model': model, 'prompt': 'What is 2 plus 2? Answer with the number only.',
                    'stream': False, 'options': {'num_predict': 32, 'temperature': 0}}
            start = time.monotonic()
            try:
                response = request('/api/generate', body)
            except Exception as error:
                elapsed = time.monotonic() - start
                record = {'pass': pass_index, 'id': model, 'name': name,
                          'http_seconds': elapsed, 'request': body,
                          'error': {'type': type(error).__name__, 'message': str(error)},
                          'passed': False}
                result['models'].append(record)
                save()
                print(json.dumps({'pass': pass_index, 'model': model,
                                  'seconds': round(elapsed, 3), 'passed': False,
                                  'error': str(error)}), flush=True)
                continue
            elapsed = time.monotonic() - start
            answer = response.get('response', '')
            passed = not response.get('error') and ('4' in answer or 'four' in answer.lower())
            record = {'pass': pass_index, 'id': model, 'name': name,
                      'http_seconds': elapsed, 'request': body,
                      'response': response, 'passed': passed}
            result['models'].append(record)
            save()
            print(json.dumps({'pass': pass_index, 'model': model,
                              'seconds': round(elapsed, 3), 'passed': passed,
                              'answer': answer[:180]}), flush=True)
    result['passed'] = all(record['passed'] for record in result['models'])
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
