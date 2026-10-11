// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { test } from 'node:test';

const source = readFileSync(new URL('../assets/web/app.js', import.meta.url), 'utf8');
const historyCode = source.slice(source.indexOf('function renderHistory(){'), source.indexOf('function openChat(){'));
const createCode = source.slice(source.indexOf('function newConversation(){'), source.indexOf('function startKind('));
function element() {
    return { value: '', textContent: '', children: [], events: {},
        classList: { toggle() {}, remove() {} }, setAttribute() {}, focus() {},
        replaceChildren(...children) { this.children = children; },
        append(...children) { this.children.push(...children); },
        addEventListener(name, callback) { this.events[name] = callback; },
        showModal() { this.open = true; } };
}
function fixture() {
    const elements = new Map();
    const lookup = id => { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); };
    lookup('history-kind').value = 'all';
    const context = { $: lookup, document: { createElement: element },
        conversations: [
            { id: 'first', title: 'Text title', model: 'text', messages: [{ content: 'a galaxy of stars' }] },
            { id: 'image', title: 'Mountain', model: 'image', messages: [] },
            { id: 'voice', title: 'Voice', model: 'removed-model', purpose: 'text-to-speech', modelName: 'Saved voice model', messages: [{ content: 'spoken words' }] },
            { id: 'old', title: 'Unclassified old model', model: 'uninstalled', messages: [] }
        ],
        models: [{ id: 'text', purpose: 'text-to-text', name: 'Text model' }, { id: 'image', purpose: 'text-to-image', name: 'Image model' }],
        current: null, controller: null, pendingDelete: '', selectedModel: 'image',
        save(key, value) { context.saved = { key, value }; },
        showWelcome() { context.current = null; }, openChat() {}, renderMessages() {} };
    runInNewContext(historyCode + createCode, context);
    return { context, lookup, rows: () => lookup('history').children };
}
test('category, content/model search and filtered open/delete keep conversation identity', () => {
    const { context: c, lookup: $, rows } = fixture();
    c.renderHistory(); assert.equal(rows().length, 4);
    $('history-search').value = 'STARS'; c.renderHistory();
    assert.equal(rows().length, 1); rows()[0].children[0].onclick(); assert.equal(c.current.id, 'first');
    $('history-kind').value = 'text-to-speech'; $('history-search').value = 'SAVED VOICE'; c.renderHistory();
    assert.equal(rows().length, 1); rows()[0].children[0].onclick(); assert.equal(c.current.id, 'voice');
    rows()[0].children[1].onclick(); assert.equal(c.pendingDelete, 'voice');
    $('delete-dialog').returnValue = 'delete'; $('delete-dialog').events.close();
    assert.equal(c.conversations.length, 3); assert.ok(!c.conversations.some(x => x.id === 'voice'));
    assert.equal(c.saved.value.length, 3);
    $('history-kind').value = 'other'; $('history-search').value = ''; c.renderHistory();
    assert.equal(rows().length, 1); assert.equal(rows()[0].children[0].textContent, 'Unclassified old model');
    $('history-search').value = 'does not exist'; c.renderHistory(); assert.equal(rows().length, 0);
    assert.equal($('history-count').textContent, '0 of 3 conversations');
});
test('new conversations retain model type/name after the installed catalog changes', () => {
    const { context: c, lookup: $, rows } = fixture();
    c.newConversation(); assert.equal(c.current.purpose, 'text-to-image'); assert.equal(c.current.modelName, 'Image model');
    const id = c.current.id; c.models = [];
    $('history-kind').value = 'text-to-image'; c.renderHistory();
    assert.equal(rows().length, 1); rows()[0].children[0].onclick(); assert.equal(c.current.id, id);
});
