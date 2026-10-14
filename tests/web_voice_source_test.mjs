// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { test } from 'node:test';

const source = readFileSync(new URL('../assets/web/app.js', import.meta.url), 'utf8');
const code = source.slice(source.indexOf('function voiceStatus('),
                          source.indexOf('const voiceRecorder=window'));

function fixture({ secureContext = false, mediaDevices = {}, mediaRecorder = true, sample = false } = {}) {
    const elements = {
        'voice-record-status': { textContent: '', classList: { names: new Set(),
            toggle(name, on) { if (on) this.names.add(name); else this.names.delete(name); } } },
        'voice-record-hint': { textContent: '' },
        'voice-record-start': { textContent: 'Start recording' },
        'voice-record-source': { value: 'browser' },
    };
    const scope = {
        window: { isSecureContext: secureContext },
        navigator: { mediaDevices },
        $: id => elements[id],
        recordedVoicePcm: sample ? new ArrayBuffer(8) : null,
        nativeVoiceReady: false,
    };
    if (mediaRecorder) scope.window.MediaRecorder = function () {};
    runInNewContext(code + '\nthis.api={voiceStatus,updateVoiceSource,browserLiveRecording};', scope);
    return { ...scope.api, elements };
}

test('plain HTTP offers the device recorder instead of live microphone capture', () => {
    const { updateVoiceSource, browserLiveRecording, elements } = fixture({
        secureContext: false, mediaDevices: { getUserMedia() {} } });
    assert.equal(browserLiveRecording(), false);
    updateVoiceSource();
    assert.match(elements['voice-record-hint'].textContent, /plain HTTP/);
    assert.match(elements['voice-record-hint'].textContent, /localhost or HTTPS/);
    assert.equal(elements['voice-record-start'].textContent, 'Record or choose audio');
});

test('a secure context keeps live recording and its permission hint', () => {
    const { updateVoiceSource, browserLiveRecording, elements } = fixture({
        secureContext: true, mediaDevices: { getUserMedia() {} } });
    assert.equal(browserLiveRecording(), true);
    updateVoiceSource();
    assert.match(elements['voice-record-hint'].textContent, /microphone access/);
    assert.equal(elements['voice-record-start'].textContent, 'Start recording');
});

test('the PS5 microphone needs no secure context and keeps its own hint', () => {
    const { updateVoiceSource, elements } = fixture({ secureContext: false });
    elements['voice-record-source'].value = 'ps5';
    updateVoiceSource();
    assert.match(elements['voice-record-hint'].textContent, /DualSense/);
    assert.equal(elements['voice-record-start'].textContent, 'Start recording');
});

test('a prepared sample keeps the Record again label, and errors are marked', () => {
    const { updateVoiceSource, voiceStatus, elements } = fixture({ secureContext: false, sample: true });
    elements['voice-record-start'].textContent = 'Record again';
    updateVoiceSource();
    assert.equal(elements['voice-record-start'].textContent, 'Record again');
    voiceStatus('Something went wrong.', 'error');
    assert.ok(elements['voice-record-status'].classList.names.has('error'));
    voiceStatus('Ready to record.');
    assert.ok(!elements['voice-record-status'].classList.names.has('error'));
});
