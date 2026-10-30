// desktop-canvas settings UI. Talks to the daemon's JSON API (see
// src/http/WebApi.h). Plain DOM, no framework, no build step.
//
// All text that comes from wallpapers (titles, tags, property labels) is set
// with textContent, never innerHTML, since workshop files are untrusted.
'use strict';

const state = {
  status: null,
  settings: null,
  library: [],
  revealed: new Set(),   // ids whose blurred preview was clicked open
  current: null,         // wallpaper open in the drawer
};

// ---------------------------------------------------------------- helpers

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === undefined || value === null || value === false) continue;
    if (key === 'class') node.className = value;
    else if (key === 'text') node.textContent = value;
    else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
    else if (key in node && key !== 'list') node[key] = value;
    else node.setAttribute(key, value === true ? '' : value);
  }
  for (const child of children.flat()) {
    if (child === null || child === undefined || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

async function api(method, path, body) {
  const options = { method, headers: {} };
  if (body !== undefined) {
    options.headers['Content-Type'] = 'application/json';
    options.body = JSON.stringify(body);
  }
  const response = await fetch(path, options);
  let data = null;
  try { data = await response.json(); } catch (_) { /* empty body */ }
  if (!response.ok) throw new Error((data && data.error) || response.statusText);
  return data;
}

let toastTimer = 0;
function toast(message, isError = false) {
  const t = document.getElementById('toast');
  t.textContent = message;
  t.classList.toggle('error', isError);
  t.classList.remove('hidden');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.add('hidden'), isError ? 6000 : 2500);
}

async function attempt(fn, success) {
  try {
    const result = await fn();
    if (success) toast(success);
    return result;
  } catch (e) {
    toast(e.message, true);
    return undefined;
  }
}

// Wallpaper Engine labels may contain markup or be ui_* localisation keys.
function labelText(text, fallback) {
  const plain = text
    ? new DOMParser().parseFromString(text, 'text/html').body.textContent.trim()
    : '';
  if (!plain || /^ui_[a-z0-9_]+$/i.test(plain)) {
    return fallback.replace(/[_-]+/g, ' ').replace(/^\w/, c => c.toUpperCase());
  }
  return plain;
}

function formatDuration(seconds) {
  if (!seconds) return '0s';
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return [h && `${h}h`, m && `${m}m`, (s || (!h && !m)) && `${s}s`].filter(Boolean).join(' ');
}

// "r g b" in 0..1 (Wallpaper Engine) <-> "#rrggbb"
function colorToHex(value) {
  const parts = String(value || '').trim().split(/\s+/).map(Number);
  if (parts.length < 3 || parts.some(Number.isNaN)) return '#ffffff';
  return '#' + parts.slice(0, 3)
    .map(v => Math.round(Math.min(1, Math.max(0, v)) * 255).toString(16).padStart(2, '0'))
    .join('');
}
function hexToColor(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255].map(v => (v / 255).toFixed(3)).join(' ');
}

function shouldBlur(item) {
  return state.settings && state.settings.ui.blur_mature &&
    (item.rating === 'Mature' || item.rating === 'Questionable') &&
    !state.revealed.has(item.id);
}

function thumb(item, onOpen) {
  const box = el('div', { class: 'thumb' });
  if (item.preview) {
    box.append(el('img', { src: `/preview/${encodeURIComponent(item.id)}`, loading: 'lazy', alt: '' }));
  } else {
    box.append(el('div', { class: 'noimg', text: 'no preview' }));
  }
  const blurred = item.preview && shouldBlur(item);
  if (blurred) {
    box.classList.add('blurred');
    box.append(el('div', { class: 'veil', text: `${item.rating}: click to show` }));
  }
  box.addEventListener('click', () => {
    if (box.classList.contains('blurred')) {
      state.revealed.add(item.id);
      box.classList.remove('blurred');
      box.querySelector('.veil')?.remove();
    } else if (onOpen) {
      onOpen();
    }
  });
  return box;
}

// ---------------------------------------------------------------- header

function renderStatus() {
  const s = state.status;
  if (!s) return;
  const showing = s.outputs.map(o => `${o.name}: ${o.title || o.id || 'nothing'}`).join(' | ');
  const r = s.rotation;
  const rotation = r.active
    ? `rotating ${r.enabled} enabled, next in ${formatDuration(r.next_in)}`
    : `${r.enabled} enabled, rotation off`;
  document.getElementById('status').textContent = `${showing}   ·   ${rotation}`;
  document.getElementById('btn-pause').textContent = s.paused ? 'Resume' : 'Pause';
  document.getElementById('btn-mute').textContent = s.muted ? 'Unmute' : 'Mute';
}

async function refreshStatus() {
  try {
    const previous = state.status;
    state.status = await api('GET', '/api/status');
    renderStatus();
    // The daemon rescans when wallpaper folders are added to or removed
    // from the Steam library; reload the grid when that happened.
    if (previous && previous.library_revision !== state.status.library_revision) {
      const before = state.library.length;
      await loadLibrary();
      const delta = state.library.length - before;
      toast(delta > 0 ? `Library updated: ${delta} new wallpaper(s)`
        : delta < 0 ? `Library updated: ${-delta} wallpaper(s) removed`
        : 'Library updated');
      if (state.current && !state.library.some(i => i.id === state.current.id)) closeDrawer();
    }
  } catch (_) { /* daemon restarting; try again next tick */ }
}

async function runAction(name, message) {
  await attempt(() => api('POST', `/api/actions/${name}`), message);
  await refreshStatus();
}

// ---------------------------------------------------------------- library

function filtered() {
  const q = document.getElementById('search').value.trim().toLowerCase();
  const type = document.getElementById('filter-type').value;
  const st = document.getElementById('filter-state').value;
  const rating = document.getElementById('filter-rating').value;
  return state.library.filter(item => {
    if (type && item.type !== type) return false;
    if (rating && item.rating !== rating) return false;
    if (st === 'enabled' && !item.enabled) return false;
    if (st === 'disabled' && item.enabled) return false;
    if (st === 'customized' && !item.customized) return false;
    if (st === 'broken' && !item.broken) return false;
    if (q) {
      const hay = [item.title, item.id, ...(item.tags || [])].join(' ').toLowerCase();
      if (!hay.includes(q)) return false;
    }
    return true;
  });
}

function renderLibrary() {
  const grid = document.getElementById('grid');
  const items = filtered();
  document.getElementById('count').textContent =
    `${items.length} shown, ${state.library.filter(i => i.enabled).length} enabled`;
  grid.replaceChildren(...items.map(card));
}

function card(item) {
  const usable = item.type !== 'unsupported';
  const toggle = el('input', {
    type: 'checkbox', checked: item.enabled, disabled: !usable,
    onchange: async e => {
      const enabled = e.target.checked;
      const ok = await attempt(() => api('PUT', `/api/wallpapers/${item.id}`, { enabled }));
      if (ok) {
        item.enabled = enabled;
        node.classList.toggle('enabled', enabled);
        renderCount();
      } else {
        e.target.checked = !enabled;
      }
    },
  });
  const node = el('div', { class: `card${item.enabled ? ' enabled' : ''}${item.broken ? ' broken' : ''}` },
    thumb(item, () => openWallpaper(item.id)),
    el('div', { class: 'card-body' },
      el('div', { class: 'card-title', text: item.title || item.id, title: item.title, onclick: () => openWallpaper(item.id) }),
      el('div', { class: 'card-meta' },
        el('span', { class: 'badge', text: item.type }),
        item.broken && el('span', { class: 'badge broken', text: 'broken' }),
        item.customized && el('span', { class: 'badge custom', text: 'custom' }),
        el('label', { title: usable ? 'Include in rotation' : item.reason }, toggle, 'Enabled'))));
  return node;
}

function renderCount() {
  document.getElementById('count').textContent =
    `${filtered().length} shown, ${state.library.filter(i => i.enabled).length} enabled`;
}

async function bulkEnable(enabled) {
  const ids = filtered().filter(i => i.type !== 'unsupported').map(i => i.id);
  if (!ids.length) return;
  if (!confirm(`${enabled ? 'Enable' : 'Disable'} ${ids.length} wallpaper(s)?`)) return;
  const ok = await attempt(() => api('PUT', '/api/wallpapers', { ids, enabled }),
                           `${enabled ? 'Enabled' : 'Disabled'} ${ids.length}`);
  if (ok) {
    for (const item of state.library) if (ids.includes(item.id)) item.enabled = enabled;
    renderLibrary();
    refreshStatus();
  }
}

async function loadLibrary() {
  state.library = await api('GET', '/api/library');
  renderLibrary();
}

// ---------------------------------------------------------------- drawer

function closeDrawer() {
  document.getElementById('drawer').classList.add('hidden');
  state.current = null;
}

async function openWallpaper(id) {
  const w = await attempt(() => api('GET', `/api/wallpapers/${encodeURIComponent(id)}`));
  if (!w) return;
  state.current = w;
  document.getElementById('d-title').textContent = w.title || w.id;
  document.getElementById('d-body').replaceChildren(...drawerContent(w));
  document.getElementById('drawer').classList.remove('hidden');
}

function drawerContent(w) {
  const usable = w.type !== 'unsupported' && !w.broken;
  const outputs = (state.status?.outputs || []).map(o => o.name);
  const showOn = el('select', {}, el('option', { value: '*', text: 'All outputs' }),
    ...outputs.map(n => el('option', { value: n, text: n })));
  const meta = [w.type, w.rating, `id ${w.id}`].filter(Boolean).join(' · ');
  const nodes = [
    thumb(w),
    el('div', { class: 'muted', text: meta }),
    w.tags?.length ? el('div', { class: 'muted', text: `Tags: ${w.tags.join(', ')}` }) : null,
    w.reason ? el('div', { class: 'muted', text: `Unsupported: ${w.reason}` }) : null,
    el('div', { class: 'row' },
      el('label', { class: 'row' },
        el('input', {
          type: 'checkbox', checked: w.enabled, disabled: w.type === 'unsupported',
          onchange: async e => {
            const enabled = e.target.checked;
            if (await attempt(() => api('PUT', `/api/wallpapers/${w.id}`, { enabled }),
                              enabled ? 'Added to rotation' : 'Removed from rotation')) {
              const item = state.library.find(i => i.id === w.id);
              if (item) item.enabled = enabled;
              renderLibrary();
              refreshStatus();
            } else {
              e.target.checked = !enabled;
            }
          },
        }), 'Enabled for rotation'),
      el('span', { class: 'spacer' }),
      showOn,
      el('button', {
        text: 'Show now', disabled: !usable,
        onclick: () => attempt(() => api('POST', `/api/wallpapers/${w.id}/show`, { output: showOn.value }),
                               'Showing').then(refreshStatus),
      })),
    w.broken ? el('div', { class: 'row' },
      el('span', { class: 'muted', text: 'This wallpaper crashed repeatedly and was blacklisted.' }),
      el('button', { text: 'Clear broken flag', onclick: () => unbreak(w.id) })) : null,
  ];

  if (w.type !== 'scene') {
    nodes.push(el('div', { class: 'muted', text: 'Only scene wallpapers have adjustable properties.' }));
    return nodes;
  }
  if (!w.properties.length) {
    nodes.push(el('div', { class: 'muted', text: 'This scene declares no user properties.' }));
    return nodes;
  }

  const randomize = state.settings?.scene.randomize_options;
  const includeNsfw = state.settings?.scene.include_nsfw_options;
  nodes.push(el('div', {
    class: 'muted',
    text: randomize
      ? 'Pin a value to always use it. Unpinned dropdowns pick randomly from the checked options at each launch.'
      : 'Pin a value to always use it. Random dropdown picks are off (Settings > Scenes); checked options only matter when it is on.',
  }));

  const editors = w.properties.map(p => propertyEditor(p, w, includeNsfw)).filter(Boolean);
  nodes.push(...editors.map(e => e.node));

  const relaunch = el('input', { type: 'checkbox', checked: w.showing_on.length > 0 });
  nodes.push(el('div', { class: 'row' },
    el('button', {
      class: 'primary', text: 'Save',
      onclick: async () => {
        const properties = {};
        const allowed = {};
        for (const e of editors) e.collect(properties, allowed);
        const ok = await attempt(() => api('PUT', `/api/wallpapers/${w.id}`,
          { properties, allowed, apply: relaunch.checked }), 'Saved');
        if (ok) { await loadLibrary(); openWallpaper(w.id); }
      },
    }),
    el('label', { class: 'row' }, relaunch, 'Relaunch now if showing'),
    el('span', { class: 'spacer' }),
    el('button', {
      text: 'Reset to defaults',
      onclick: async () => {
        if (!confirm('Clear every pinned value and option restriction for this wallpaper?')) return;
        const ok = await attempt(() => api('PUT', `/api/wallpapers/${w.id}`,
          { properties: {}, allowed: {}, apply: relaunch.checked }), 'Reset');
        if (ok) { await loadLibrary(); openWallpaper(w.id); }
      },
    })));
  return nodes;
}

// One editor per property. collect(properties, allowed) writes its result.
function propertyEditor(p, w, includeNsfw) {
  const label = labelText(p.text, p.name);
  const pinned = Object.prototype.hasOwnProperty.call(w.pinned, p.name) ? w.pinned[p.name] : undefined;
  const head = (...controls) => el('div', { class: 'prop-head' },
    el('span', { class: 'name', text: label }), el('span', { class: 'key', text: p.name }), ...controls);

  if (p.type === 'text') {
    return label ? { node: el('div', { class: 'prop-heading', text: label }), collect() {} } : null;
  }

  if (p.type === 'combo') {
    const pin = el('select', {},
      el('option', { value: '', text: 'Not pinned' }),
      ...p.options.map(o => el('option', { value: o.value, text: labelText(o.label, o.value), selected: pinned === o.value })));
    const restricted = Object.prototype.hasOwnProperty.call(w.allowed, p.name);
    const boxes = p.options.map(o => {
      const checked = restricted ? w.allowed[p.name].includes(o.value) : (includeNsfw || !o.nsfw);
      const box = el('input', { type: 'checkbox', checked, value: o.value });
      return { option: o, box, node: el('label', {}, box, labelText(o.label, o.value), o.nsfw ? el('span', { class: 'nsfw', text: 'NSFW' }) : null) };
    });
    const options = el('div', { class: 'options' }, ...boxes.map(b => b.node));
    const sync = () => options.classList.toggle('hidden', pin.value !== '');
    pin.addEventListener('change', sync);
    sync();
    return {
      node: el('div', { class: 'prop' }, head(pin), el('div', { class: 'muted', text: `Default: ${p.value}` }), options),
      collect(properties, allowed) {
        if (pin.value !== '') { properties[p.name] = pin.value; return; }
        const checked = boxes.filter(b => b.box.checked).map(b => b.option.value);
        const defaults = p.options.filter(o => includeNsfw || !o.nsfw).map(o => o.value);
        const same = checked.length === defaults.length && checked.every(v => defaults.includes(v));
        if (!same && checked.length) allowed[p.name] = checked;
      },
    };
  }

  // Scalar properties: an "Override" checkbox enables the value control.
  const override = el('input', { type: 'checkbox', checked: pinned !== undefined, title: 'Override the default' });
  let control, read;
  const current = pinned !== undefined ? pinned : p.value;
  if (p.type === 'slider') {
    const range = el('input', { type: 'range', min: p.min ?? 0, max: p.max ?? 100, step: p.step || 'any', value: current });
    const number = el('input', { type: 'number', min: p.min, max: p.max, step: p.step || 'any', value: current, style: 'width: 90px' });
    range.addEventListener('input', () => { number.value = range.value; });
    number.addEventListener('input', () => { range.value = number.value; });
    control = el('div', { class: 'row' }, range, number);
    read = () => number.value;
  } else if (p.type === 'bool') {
    const box = el('input', { type: 'checkbox', checked: current === 'true' || current === '1' });
    control = el('label', { class: 'row' }, box, 'On');
    read = () => (box.checked ? 'true' : 'false');
  } else if (p.type === 'color') {
    const picker = el('input', { type: 'color', value: colorToHex(current) });
    control = picker;
    read = () => hexToColor(picker.value);
  } else if (p.type === 'textinput') {
    const input = el('input', { type: 'text', value: current, style: 'width: 100%' });
    control = input;
    read = () => input.value;
  } else {
    return { node: el('div', { class: 'prop' }, head(), el('div', { class: 'muted', text: `${p.type} properties cannot be edited here.` })), collect() {} };
  }
  const sync = () => control.classList.toggle('hidden', !override.checked);
  override.addEventListener('change', sync);
  sync();
  return {
    node: el('div', { class: 'prop' },
      head(el('label', { class: 'row' }, override, 'Override')),
      el('div', { class: 'muted', text: `Default: ${p.type === 'color' ? colorToHex(p.value) : p.value}` }),
      control),
    collect(properties) { if (override.checked) properties[p.name] = read(); },
  };
}

async function unbreak(id) {
  const broken = state.settings.broken.filter(b => b !== id);
  if (await attempt(() => api('PUT', '/api/settings', { broken }), 'Broken flag cleared')) {
    await loadSettings();
    await loadLibrary();
    if (state.current?.id === id) openWallpaper(id);
  }
}

// ---------------------------------------------------------------- settings

function field(label, control, help) {
  return [el('label', { text: label }), control, help ? el('div', { class: 'help', text: help }) : null];
}

function select(value, choices) {
  return el('select', {}, ...choices.map(([v, text]) => el('option', { value: v, text, selected: v === value })));
}

function renderSettings() {
  const s = state.settings;
  const outputs = (state.status?.outputs || []).map(o => o.name);
  const titleOf = id => state.library.find(i => i.id === id)?.title || '';
  const form = document.getElementById('settings-form');

  const fps = el('input', { type: 'number', min: 1, max: 240, value: s.fps });
  const scaling = select(s.scaling, [['fill', 'Fill (crop)'], ['fit', 'Fit (letterbox)'], ['stretch', 'Stretch'], ['default', 'Wallpaper default']]);
  const layer = select(s.layer, [['background', 'Background (below desktop icons)'], ['bottom', 'Bottom']]);
  const muted = el('input', { type: 'checkbox', checked: s.audio.muted });
  const volume = el('input', { type: 'range', min: 0, max: 100, value: s.audio.volume });
  const volumeText = el('span', { text: s.audio.volume });
  volume.addEventListener('input', () => { volumeText.textContent = volume.value; });

  const interval = el('input', { type: 'number', min: 0, step: 'any', value: +(s.playlist.interval / 60).toFixed(2), style: 'width: 120px' });
  const rotateOutputs = outputs.map(name => ({
    name, box: el('input', { type: 'checkbox', checked: s.playlist.outputs.includes(name) }),
  }));

  const engine = el('input', { type: 'text', value: s.scene.engine_path, placeholder: 'linux-wallpaperengine (from PATH)' });
  const assets = el('input', { type: 'text', value: s.scene.assets_dir, placeholder: 'autodetect' });
  const mouse = el('input', { type: 'checkbox', checked: s.scene.disable_mouse });
  const randomize = el('input', { type: 'checkbox', checked: s.scene.randomize_options });
  const nsfw = el('input', { type: 'checkbox', checked: s.scene.include_nsfw_options });

  const datalist = el('datalist', { id: 'wallpaper-ids' },
    ...state.library.filter(i => i.type !== 'unsupported')
      .map(i => el('option', { value: i.id, text: i.title })));
  const assignmentRows = ['*', ...outputs].map(name => {
    const input = el('input', { type: 'text', value: s.assignments[name] || '', placeholder: 'none', style: 'width: 100%' });
    input.setAttribute('list', 'wallpaper-ids');
    const hint = el('div', { class: 'help', text: titleOf(input.value) });
    input.addEventListener('input', () => { hint.textContent = titleOf(input.value.trim()); });
    return { name, input, nodes: [el('label', { text: name === '*' ? 'All outputs (*)' : name }), input, hint] };
  });

  const roots = el('textarea', { value: s.content_roots.join('\n'), placeholder: 'autodetect Steam libraries' });
  const web = el('input', { type: 'checkbox', checked: s.web.enabled });
  const uiEnabled = el('input', { type: 'checkbox', checked: s.ui.enabled });
  const port = el('input', { type: 'number', min: 1024, max: 65535, value: s.ui.port });
  const blur = el('input', { type: 'checkbox', checked: s.ui.blur_mature });

  const removedBroken = new Set();
  const brokenList = el('div', { class: 'broken-list full' },
    ...(s.broken.length ? s.broken.map(id => {
      const row = el('div', { class: 'row' }, el('span', { text: `${id}  ${titleOf(id)}` }), el('span', { class: 'spacer' }));
      row.append(el('button', {
        text: 'Remove', onclick: e => {
          e.preventDefault();
          removedBroken.add(id);
          row.remove();
        },
      }));
      return row;
    }) : [el('span', { class: 'muted', text: 'Nothing is blacklisted.' })]));

  form.replaceChildren(
    datalist,
    el('fieldset', {}, el('legend', { text: 'Display' }),
      ...field('Scene FPS cap', fps),
      ...field('Scaling', scaling),
      ...field('Layer', layer, 'Wayland layer for wallpapers. Background keeps desktop icons on top.')),
    el('fieldset', {}, el('legend', { text: 'Audio' }),
      ...field('Muted', muted),
      ...field('Volume', el('div', { class: 'row' }, volume, volumeText))),
    el('fieldset', {}, el('legend', { text: 'Rotation' }),
      ...field('Interval (minutes)', interval, '0 turns rotation off. Enable wallpapers in the Library tab.'),
      ...field('Rotating outputs', el('div', { class: 'row' },
        ...(rotateOutputs.length ? rotateOutputs.map(r => el('label', { class: 'row' }, r.box, r.name)) : [el('span', { class: 'muted', text: 'no outputs' })])),
        'None checked = every output, all showing the same wallpaper.')),
    el('fieldset', {}, el('legend', { text: 'Scenes' }),
      ...field('Random dropdown values', randomize, 'Each launch picks random poses, modes and styles (per wallpaper limits in the Library).'),
      ...field('Allow NSFW options', nsfw, 'Lets random picks use options labelled NSFW or nude unless a wallpaper restricts them.'),
      ...field('Disable mouse', mouse),
      ...field('Engine path', engine),
      ...field('Assets directory', assets)),
    el('fieldset', {}, el('legend', { text: 'Assignments' }),
      el('div', { class: 'help full', text: 'Wallpaper id shown on each output when rotation is off or not covering it. Leave blank for none.' }),
      ...assignmentRows.flatMap(r => r.nodes)),
    el('fieldset', {}, el('legend', { text: 'Library' }),
      ...field('Content roots', roots, 'One steamapps/workshop/content/431960 folder per line.'),
      ...field('', el('button', { text: 'Rescan library', onclick: async e => {
        e.preventDefault();
        await runAction('reload', 'Library rescanned');
        await loadLibrary();
      } }))),
    el('fieldset', {}, el('legend', { text: 'Web wallpapers' }),
      ...field('Enabled', web, 'Needs the optional desktop-canvas-web helper.')),
    el('fieldset', {}, el('legend', { text: 'Settings UI' }),
      ...field('Blur Mature / Questionable previews', blur),
      ...field('UI enabled', uiEnabled, 'Turning this off hides this page after the daemon restarts.'),
      ...field('Port', port, 'Takes effect after the daemon restarts.')),
    el('fieldset', {}, el('legend', { text: 'Broken wallpapers' }), brokenList),
    el('div', { class: 'form-actions' },
      el('button', { class: 'primary', type: 'submit', text: 'Save settings' }),
      el('button', { text: 'Revert', onclick: e => { e.preventDefault(); renderSettings(); } })));

  form.onsubmit = async e => {
    e.preventDefault();
    const assignments = {};
    for (const r of assignmentRows) if (r.input.value.trim()) assignments[r.name] = r.input.value.trim();
    const patch = {
      fps: Number(fps.value),
      scaling: scaling.value,
      layer: layer.value,
      audio: { muted: muted.checked, volume: Number(volume.value) },
      playlist: {
        interval: Math.round(Number(interval.value) * 60),
        outputs: rotateOutputs.filter(r => r.box.checked).map(r => r.name),
      },
      scene: {
        engine_path: engine.value.trim(),
        assets_dir: assets.value.trim(),
        disable_mouse: mouse.checked,
        randomize_options: randomize.checked,
        include_nsfw_options: nsfw.checked,
      },
      assignments,
      content_roots: roots.value.split('\n').map(v => v.trim()).filter(Boolean),
      web: { enabled: web.checked },
      ui: { enabled: uiEnabled.checked, port: Number(port.value), blur_mature: blur.checked },
      broken: s.broken.filter(id => !removedBroken.has(id)),
    };
    const result = await attempt(() => api('PUT', '/api/settings', patch));
    if (!result) return;
    toast(result.notes?.length ? `Saved. ${result.notes.join('; ')}` : 'Settings saved');
    await loadSettings();
    await refreshStatus();
    renderLibrary();
  };
}

async function loadSettings() {
  state.settings = await api('GET', '/api/settings');
  renderSettings();
}

// ---------------------------------------------------------------- startup

function showTab(name) {
  for (const t of document.querySelectorAll('.tab')) t.classList.toggle('active', t.dataset.tab === name);
  for (const page of document.querySelectorAll('.tab-page')) page.classList.toggle('hidden', page.id !== `tab-${name}`);
}

async function init() {
  for (const t of document.querySelectorAll('.tab')) t.addEventListener('click', () => showTab(t.dataset.tab));
  for (const id of ['search', 'filter-type', 'filter-state', 'filter-rating']) {
    document.getElementById(id).addEventListener('input', renderLibrary);
  }
  document.getElementById('enable-shown').addEventListener('click', () => bulkEnable(true));
  document.getElementById('disable-shown').addEventListener('click', () => bulkEnable(false));
  document.getElementById('btn-next').addEventListener('click', () => runAction('next', 'Next wallpaper'));
  document.getElementById('btn-pause').addEventListener('click', () =>
    runAction(state.status?.paused ? 'resume' : 'pause'));
  document.getElementById('btn-mute').addEventListener('click', () =>
    runAction(state.status?.muted ? 'unmute' : 'mute'));
  document.getElementById('btn-restart').addEventListener('click', () => runAction('restart', 'Restarted wallpapers'));
  document.getElementById('d-close').addEventListener('click', closeDrawer);
  document.addEventListener('keydown', e => { if (e.key === 'Escape') closeDrawer(); });

  await attempt(async () => {
    await refreshStatus();
    state.settings = await api('GET', '/api/settings');  // blur setting before the grid
    await loadLibrary();
    renderSettings();
  });
  setInterval(refreshStatus, 5000);
}

init();
