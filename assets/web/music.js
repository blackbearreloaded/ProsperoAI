// Prospero Music Studio — logic for /music page. No inline JS (CSP script-src 'self').
(function () {
  'use strict';
  var KEY = 'prospero-key';
  var AUDIO_DIR = '/download0/ProsperoAI/work/audio/';
  var MUSIC_RE = /yue2|yue|ace[-_]?step|kokoro|chatterbox/i;
  var $ = function (id) { return document.getElementById(id); };

  function apiKey() { return localStorage.getItem(KEY) || ''; }

  function api(path, body, timeoutMs, method) {
    var opts = { headers: {} };
    var k = apiKey();
    if (k) opts.headers['Authorization'] = 'Bearer ' + k;
    if (body !== undefined) {
      opts.method = method || 'POST';
      opts.headers['Content-Type'] = 'application/json';
      opts.body = JSON.stringify(body);
    }
    var ctl = typeof AbortController !== 'undefined' ? new AbortController() : null;
    if (ctl && timeoutMs) setTimeout(function () { ctl.abort(); }, timeoutMs);
    if (ctl) opts.signal = ctl.signal;
    return fetch(path, opts).then(function (r) {
      if (r.status === 401) { $('key-bar').hidden = false; throw new Error('API key required (401)'); }
      if (!r.ok) return r.text().then(function (t) { throw new Error('HTTP ' + r.status + ' ' + t.slice(0, 200)); });
      return r.json();
    });
  }

  function setStatus(msg, cls) {
    var el = $('gen-status');
    el.textContent = msg || '';
    el.className = cls || '';
  }

  // ---- models ----
  function loadModels() {
    return api('/api/models/presets').then(function (j) {
      var sel = $('model');
      sel.innerHTML = '';
      (j.data || []).forEach(function (m) {
        if (!MUSIC_RE.test(m.id || '') && !MUSIC_RE.test(m.name || '')) return;
        var o = document.createElement('option');
        o.value = m.id;
        o.textContent = (m.installed ? '' : '(not installed) ') + (m.name || m.id);
        if (MUSIC_RE.test(m.id) && /yue2/i.test(m.id) && m.installed) o.selected = true;
        sel.appendChild(o);
      });
      if (!sel.options.length) {
        var o2 = document.createElement('option');
        o2.value = 'yue2-3b-q8-0'; o2.textContent = 'yue2-3b-q8-0';
        sel.appendChild(o2);
      }
      $('status-dot').className = 'dot on';
    }).catch(function (e) { $('status-dot').className = 'dot'; setStatus('Cannot load presets: ' + e.message, 'err'); });
  }

  // ---- runtime settings ----
  function loadSettings() {
    return api('/api/settings/runtime').then(function (s) {
      if (typeof s.audio_lyrics === 'string') $('lyrics').value = s.audio_lyrics;
      if (s.audio_duration_sec) { $('duration').value = s.audio_duration_sec; }
      if (s.audio_seed) $('seed').value = s.audio_seed;
      $('dur-val').textContent = $('duration').value;
      var opts = String(s.audio_options || '');
      opts.split('\n').forEach(function (line) {
        var kv = line.split('=');
        if (kv.length < 2) return;
        var key = kv[0].trim(), val = kv.slice(1).join('=').trim();
        if (key === 'cot') $('cot').value = val;
        else if (key === 'abc_max_tokens') $('abc-max').value = val;
        else if (key === 'abc_file') {
          var name = val.replace(AUDIO_DIR, '');
          $('abc-name').value = name;
          fetch(AUDIO_DIR + name).then(function (r) { return r.ok ? r.text() : ''; })
            .then(function (t) { if (t) $('abc').value = t; }).catch(function () {});
        }
      });
    }).catch(function () {});
  }

  function buildOptions(abcName) {
    var lines = [];
    if (!abcName) return '';
    var cot = $('cot').value;
    if (cot) lines.push('cot=' + cot);
    lines.push('abc_file=' + AUDIO_DIR + abcName);
    if ($('abc-max').value) lines.push('abc_max_tokens=' + $('abc-max').value);
    return lines.join('\n');
  }

  // ---- library ----
  function fmtSize(b) { return b > 1048576 ? (b / 1048576).toFixed(1) + ' MB' : Math.round(b / 1024) + ' KB'; }
  function fmtDate(t) { try { return new Date(t * 1000).toLocaleString('en-GB'); } catch (e) { return ''; } }

  function loadLibrary() {
    return api('/api/music/files').then(function (j) {
      trackFiles(j);
      var box = $('tracks');
      box.innerHTML = '';
      var files = j.files || [];
      if (!files.length) { box.innerHTML = '<div class="empty">No files yet.</div>'; }
      files.forEach(function (f) {
        var d = document.createElement('div'); d.className = 'track';
        var m = document.createElement('div'); m.className = 'meta';
        var n = document.createElement('span'); n.className = 'name'; n.textContent = f.name;
        var s = document.createElement('span');
        s.textContent = fmtSize(f.size) + ' · ' + fmtDate(f.mtime);
        m.appendChild(n); m.appendChild(s);
        d.appendChild(m);
        if (/\.(wav|mp3)$/i.test(f.name)) {
          var a = document.createElement('audio');
          a.controls = true; a.preload = 'none'; a.src = AUDIO_DIR + encodeURIComponent(f.name);
          d.appendChild(a);
        } else {
          var ext = document.createElement('div'); ext.className = 'path';
          ext.textContent = 'ABC — use \u201cLoad into editor\u201d to edit it';
          d.appendChild(ext);
        }
        var del = document.createElement('button');
        del.type = 'button'; del.className = 'del'; del.textContent = '🗑';
        del.title = 'Delete ' + f.name;
        del.addEventListener('click', function () {
          if (!window.confirm('Delete ' + f.name + '?')) return;
          api('/api/music/file', { name: f.name }, 15000, 'DELETE')
            .then(function () { delete knownFiles[f.name]; loadLibrary(); loadAbcSelect(); })
            .catch(function (e) { setStatus('Delete failed: ' + e.message, 'err'); });
        });
        s.appendChild(document.createTextNode(' '));
        s.appendChild(del);
        box.appendChild(d);
      });
    }).catch(function (e) { $('tracks').innerHTML = '<div class="empty">' + e.message + '</div>'; });
  }

  // ---- ABC save/load ----
  function loadAbcSelect() {
    return api('/api/music/files').then(function (j) {
      var sel = $('abc-files');
      sel.innerHTML = '';
      (j.files || []).filter(function (f) { return /\.abc$/i.test(f.name); }).forEach(function (f) {
        var o = document.createElement('option'); o.value = f.name; o.textContent = f.name;
        sel.appendChild(o);
      });
    }).catch(function () {});
  }

  $('abc-save').addEventListener('click', function () {
    var name = $('abc-name').value.trim() || 'studio.abc';
    if (!/\.abc$/i.test(name)) name += '.abc';
    setStatus('Saving ABC...', 'busy');
    api('/api/music/abc', { name: name, content: $('abc').value })
      .then(function () { $('abc-name').value = name; setStatus('ABC saved: ' + name); loadAbcSelect(); loadLibrary(); })
      .catch(function (e) { setStatus('Saving ABC failed: ' + e.message, 'err'); });
  });

  $('abc-open').addEventListener('click', function () { $('abc-disk').click(); });
  $('abc-disk').addEventListener('change', function (ev) {
    var f = ev.target.files && ev.target.files[0];
    if (!f) return;
    var reader = new FileReader();
    reader.onload = function () {
      $('abc').value = reader.result;
      var base = f.name.split(/[\\/]/).pop().replace(/[^A-Za-z0-9._-]/g, '_');
      if (!/\.abc$/i.test(base)) base += '.abc';
      $('abc-name').value = base;
      setStatus('File ' + f.name + ' loaded into editor — press GENERATE (auto-saves)', 'ok');
    };
    reader.readAsText(f);
    ev.target.value = '';
  });
  $('abc-clear').addEventListener('click', function () {
    $('abc').value = '';
    setStatus('Editor cleared — no score at GENERATE', 'ok');
  });

  $('abc-load').addEventListener('click', function () {
    var name = ($('abc-files').value || $('abc-name').value || '').trim();
    if (!name) { setStatus('Missing ABC file name', 'err'); return; }
    fetch(AUDIO_DIR + encodeURIComponent(name)).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.text();
    }).then(function (t) { $('abc').value = t; $('abc-name').value = name; setStatus('Loaded: ' + name); })
      .catch(function (e) { setStatus('Load failed: ' + e.message, 'err'); });
  });

  // ---- generate ----
  var knownFiles = {};
  function trackFiles(j) { (j.files || []).forEach(function (f) { knownFiles[f.name] = true; }); }

  function runGeneration() {
    var btn = $('generate');
    if (btn.disabled) return;
    btn.disabled = true;
    var t0 = Date.now();
    var timer = setInterval(function () {
      setStatus('Running... ' + Math.round((Date.now() - t0) / 1000) +
        's (model load + generation can take 4-6 min)', 'busy');
    }, 1000);
    var abcName = $('abc-name').value.trim();
    if (abcName && !/\.abc$/i.test(abcName)) abcName += '.abc';
    var abcContent = $('abc').value.trim();
    var doGenerate = function (effectiveName) {
      setStatus('Sending settings...', 'busy');
      var body = {
        audio_lyrics: $('lyrics').value,
        audio_duration_sec: parseInt($('duration').value, 10) || 240,
        audio_seed: parseInt($('seed').value, 10) || 42,
        // always sent: empty = CLEAR inherited options (otherwise an abc_file
        // from an earlier session stays in runtime settings and the engine sees it)
        audio_options: buildOptions(effectiveName)
      };
      return api('/api/settings/runtime', body).then(function () {
        setStatus('Generating...', 'busy');
        return api('/api/generate', { model: $('model').value, prompt: $('style').value }, 1800000);
      });
    };
    // ABC: non-empty editor => auto-save and use the file; empty editor =>
    // only names that already exist on the PS5; otherwise no abc_file at all.
    var pre = Promise.resolve('');
    if (abcName && abcContent) {
      setStatus('Auto-saving ABC...', 'busy');
      pre = api('/api/music/abc', { name: abcName, content: $('abc').value })
        .then(function () { knownFiles[abcName] = true; return abcName; });
    } else if (abcName && knownFiles[abcName]) {
      pre = Promise.resolve(abcName);
    } else if (abcName) {
      setStatus('ABC "' + abcName + '" missing on PS5 and the editor is empty — generating without a score', 'busy');
    }
    pre.then(doGenerate).then(function (j) {
      clearInterval(timer);
      var resp = String(j.response || j.error || JSON.stringify(j).slice(0, 300));
      var m = resp.match(/\/download0\/\S+?\.(?:wav|mp3)/);
      var res = $('result');
      res.innerHTML = '';
      if (m) {
        var url = m[0].replace(/[",).]+$/, '');
        var finish = function (finalUrl) {
          var res = $('result');
          res.innerHTML = '';
          var a = document.createElement('audio');
          a.controls = true; a.src = finalUrl;
          res.appendChild(a);
          var p = document.createElement('div'); p.className = 'path'; p.textContent = finalUrl;
          res.appendChild(p);
          setStatus('Done in ' + Math.round((Date.now() - t0) / 1000) + 's 🎵');
          loadLibrary();
        };
        var nice = $('track-name').value.trim();
        if (nice) {
          var src = url.substring(url.lastIndexOf('/') + 1);
          var ext = src.substring(src.lastIndexOf('.'));
          var safe = nice.toLowerCase().replace(/[ăâîșțşţ]/g, function (c) {
            return { 'ă': 'a', 'â': 'a', 'î': 'i', 'ș': 's', 'ş': 's', 'ț': 't', 'ţ': 't' }[c];
          }).replace(/[^a-z0-9._-]+/g, '-').replace(/^[-.]+|[-.]+$/g, '');
          if (!safe) safe = 'piesa';
          var dst = safe + '_seed' + ($('seed').value || '42') + ext;
          api('/api/music/rename', { src: src, dst: dst }).then(function () {
            finish(url.replace(src, dst));
          }).catch(function () {
            finish(url);
            setStatus('Track is ready, but "' + dst + '" already exists — kept the original name', 'err');
          });
        } else {
          finish(url);
        }
      } else {
        var res2 = $('result');
        res2.innerHTML = '';
        var p2 = document.createElement('div'); p2.className = 'path'; p2.textContent = resp;
        res2.appendChild(p2);
        setStatus('Response without audio file', 'err');
      }
    }).catch(function (e) {
      clearInterval(timer);
      setStatus('Failed: ' + e.message, 'err');
    }).then(function () { btn.disabled = false; });
  }
  $('generate').addEventListener('click', runGeneration);
  $('regen').addEventListener('click', function () {
    var s = $('seed');
    s.value = String(Math.min(4294967295, (parseInt(s.value, 10) || 42) + 1));
    runGeneration();
  });

  // ---- misc ----
  $('duration').addEventListener('input', function () { $('dur-val').textContent = $('duration').value; });
  $('dice').addEventListener('click', function () {
    $('seed').value = 1 + Math.floor(Math.random() * 4294967294);
  });
  $('refresh').addEventListener('click', function () { loadLibrary(); loadAbcSelect(); });
  $('key-save').addEventListener('click', function () {
    localStorage.setItem(KEY, $('key-input').value.trim());
    $('key-bar').hidden = true;
    loadModels(); loadSettings(); loadLibrary(); loadAbcSelect();
  });

  loadModels(); loadSettings(); loadLibrary(); loadAbcSelect();
})();
