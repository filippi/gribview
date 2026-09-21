const { test } = require('node:test');
const assert = require('node:assert/strict');
const { detectPlatform, installMethods, setupInstaller } = require('../docs/install.js');

test('OS detection is conservative on mobile, ChromeOS and unknown browsers', () => {
    for (const [browser, expected] of [
        [{ platform: 'MacIntel', maxTouchPoints: 0 }, 'mac'],
        [{ userAgentData: { platform: 'macOS' } }, 'mac'],
        [{ platform: 'Win32' }, 'windows'],
        [{ userAgentData: { platform: 'Linux' } }, 'linux'],
        [{ userAgent: 'Mozilla/5.0 (X11; Linux x86_64)' }, 'linux'],
        [{ platform: 'Linux aarch64', userAgent: 'Android' }, ''],
        [{ platform: 'MacIntel', maxTouchPoints: 5 }, ''],
        [{ platform: 'iPhone' }, ''],
        [{ platform: 'Linux x86_64', userAgent: 'CrOS' }, ''],
        [{ userAgentData: { platform: 'Windows', mobile: true } }, ''],
        [{}, '']
    ]) assert.equal(detectPlatform(browser), expected);
});

function fixture() {
    const nodes = new Map();
    function node() {
        return {
            value: '', hidden: true, children: [], listeners: {}, textContent: '',
            addEventListener(event, callback) { this.listeners[event] = callback; },
            replaceChildren() { this.children = []; },
            append(child) { this.children.push(child); }
        };
    }
    const panels = [...new Set(Object.values(installMethods).flat().map(([id]) => id))]
        .map(method => ({ ...node(), dataset: { method } }));
    const doc = {
        getElementById(id) { if (!nodes.has(id)) nodes.set(id, node()); return nodes.get(id); },
        createElement: node,
        querySelectorAll: () => panels
    };
    return { doc, panels };
}

test('manual platform and method selections show only the relevant instructions', () => {
    const { doc, panels } = fixture();
    setupInstaller(doc, { platform: 'MacIntel' });
    const platform = doc.getElementById('platform');
    const method = doc.getElementById('install-method');
    assert.equal(method.value, 'dmg');
    assert.equal(doc.getElementById('install-selectors').hidden, false);
    for (const [os, choices] of Object.entries(installMethods)) {
        platform.value = os;
        platform.listeners.change();
        assert.equal(method.value, choices[0][0]);
        assert.deepEqual(method.children.map(option => option.value), choices.map(([id]) => id));
        for (const [id] of choices) {
            method.value = id;
            method.listeners.change();
            assert.deepEqual(panels.filter(panel => !panel.hidden).map(panel => panel.dataset.method), [id]);
            assert.equal(doc.getElementById('mac-approval').hidden, os !== 'mac' || id === 'brew');
            if (id === 'deb') assert.match(doc.getElementById('requirements').textContent, /Ubuntu 22.04/);
        }
    }
    platform.value = '';
    platform.listeners.change();
    assert.equal(panels.every(panel => panel.hidden), true);
    assert.equal(doc.getElementById('platform-prompt').hidden, false);
    assert.equal(doc.getElementById('method-label').hidden, true);
});

test('mobile visitors are not offered a mismatched desktop download', () => {
    const { doc, panels } = fixture();
    setupInstaller(doc, { platform: 'iPhone' });
    assert.equal(doc.getElementById('platform').value, '');
    assert.equal(panels.every(panel => panel.hidden), true);
});
