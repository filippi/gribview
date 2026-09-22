/* Detection suggests an OS, never a CPU architecture or Linux distribution. */
function detectPlatform(browser) {
    const ua = browser.userAgent || '';
    const platform = browser.userAgentData?.platform || browser.platform || ua;
    if (browser.userAgentData?.mobile || /Android|iPhone|iPad|iPod|CrOS/i.test(ua + ' ' + platform) ||
        (/Mac/i.test(platform) && browser.maxTouchPoints > 1)) return '';
    if (/Mac/i.test(platform)) return 'mac';
    if (/Win/i.test(platform)) return 'windows';
    if (/Linux/i.test(platform)) return 'linux';
    return '';
}

const installMethods = {
    mac: [['brew', 'Homebrew CLI (default)'], ['dmg', 'Download Mac app (DMG)'], ['cask', 'Homebrew app']],
    linux: [['appimage', 'AppImage'], ['deb', 'Ubuntu / Debian package'], ['portable', 'Portable archive'], ['brew', 'Homebrew CLI'], ['pip', 'Python / pip']],
    windows: [['zip', 'Download ZIP']]
};

function setupInstaller(doc, browser) {
    const platform = doc.getElementById('platform');
    const method = doc.getElementById('install-method');
    const requirements = {
        mac: 'Apple Silicon (M1 or newer), macOS 15+. No Intel Mac download.',
        linux: 'x86_64 desktop, glibc 2.35+ and OpenGL 3.2. Not Alpine Linux.',
        windows: 'Windows x64, with OpenGL 3.2 support.'
    };
    function showMethod() {
        for (const panel of doc.querySelectorAll('[data-method]')) {
            panel.hidden = panel.dataset.method !== method.value ||
                (platform.value === 'mac' && method.value === 'brew');
        }
        doc.getElementById('mac-approval').hidden = platform.value !== 'mac' || method.value === 'brew';
        doc.getElementById('requirements').textContent = method.value === 'deb'
            ? 'Ubuntu 22.04+ or Debian 12+, x86_64 desktop with OpenGL 3.2.'
            : requirements[platform.value] || '';
    }
    function showPlatform() {
        const choices = installMethods[platform.value] || [];
        method.replaceChildren();
        for (const [value, label] of choices) {
            const option = doc.createElement('option');
            option.value = value;
            option.textContent = label;
            method.append(option);
        }
        method.value = choices[0]?.[0] || '';
        doc.getElementById('method-label').hidden = choices.length < 2;
        doc.getElementById('platform-prompt').hidden = choices.length > 0;
        doc.getElementById('mac-quick-install').hidden = platform.value !== 'mac';
        showMethod();
    }
    platform.addEventListener('change', showPlatform);
    method.addEventListener('change', showMethod);
    platform.value = detectPlatform(browser);
    showPlatform();
    doc.getElementById('install-selectors').hidden = false;
}

if (typeof module !== 'undefined') module.exports = { detectPlatform, installMethods, setupInstaller };
if (typeof document !== 'undefined') setupInstaller(document, navigator);
