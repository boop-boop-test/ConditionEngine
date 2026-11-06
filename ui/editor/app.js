// Dials editor page. Talks to Dials.dll through Magelight's page core:
//   page -> host: magelight.send('dials', JSON {op, req, ...})
//   host -> page: dials_state, dials_live, dials_reply, dials_catalog, dials_focus
// In a plain browser a small mock stands in, so the page can be worked on without the game.
(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);
  const el = (tag, cls, text) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  };
  const clone = (o) => JSON.parse(JSON.stringify(o));
  const parse = (raw) => {
    if (typeof raw !== 'string') return raw;
    try { return JSON.parse(raw); } catch (e) { return raw; }
  };
  const fmt = (v) => {
    if (typeof v !== 'number' || !isFinite(v)) return String(v);
    return Math.abs(v - Math.round(v)) < 1e-9 ? String(Math.round(v)) : v.toFixed(Math.abs(v) < 10 ? 2 : 1);
  };

  // ── Bridge ─────────────────────────────────────────────────────────────────
  const core = window.magelight || makeMock();
  let reqN = 1;
  const pending = {};
  function send(op, data) {
    const req = reqN++;
    core.send('dials', JSON.stringify(Object.assign({ op: op, req: req }, data || {})));
    return req;
  }
  function request(op, data) {
    return new Promise((resolve) => { pending[send(op, data)] = resolve; });
  }

  // ── State ──────────────────────────────────────────────────────────────────
  let state = { dials: [], events: [], ready: true };
  let live = { values: {}, actors: {}, log: [] };
  let selectedKey = null;   // lower-case id of the dial shown (null: a new one)
  let draft = null;         // the dial's JSON being edited
  let draftId = '';
  let previousId = '';
  let dirty = false;
  let jsonMode = false;
  let idTouched = true;      // false: the name of a new item follows its label
  let validateTimer = 0;
  let tab = 'dials';         // dials | conditions | values
  let catalog = null;        // ConditionEngine's catalog (named conditions and values)
  let named = null;          // the condition or value shown: {kind, name, previousName, isUser, isNew, item, json}
  let snStatus = null;       // SkyrimNet bridge status
  const SKYRIMNET_EVENTS = ['dialogue', 'dialogue_player_text', 'dialogue_player_stt', 'combat', 'hit', 'death',
    'activation', 'spell_cast', 'equip'];

  const GAME_EVENTS = () => state.events && state.events.length ? state.events : [{ name: 'Hit', help: '' }];
  // Each label completes "When …" and "Do …"
  const TRIGGERS = [
    ['on', 'a game event occurs'], ['event', 'a mod event occurs'], ['when', 'a condition changes'], ['while', 'a condition is true'],
    ['every', 'a time interval ends'], ['key', 'the player pushes a key'], ['skyrimnet', 'a SkyrimNet event occurs'], ['aiAction', 'the AI selects an action']
  ];
  const ACTIONS = [
    ['set', 'set the value to'], ['add', 'add'], ['multiply', 'multiply by'], ['toggle', 'switch on / off'], ['reset', 'reset'],
    ['cycle', 'go to the next option'], ['rate', 'change the value by'], ['approach', 'move the value toward'],
    ['send', 'send a mod event']
  ];
  const TRACK_PRESETS = ['IsPlayer || IsFollower', 'IsFollower && !IsPlayer', '!IsPlayer', 'true'];
  const CONTINUOUS = ['rate', 'approach', 'set'];
  const DISCRETE = ['set', 'add', 'multiply', 'toggle', 'reset', 'cycle', 'send'];

  function findDial(key) { return state.dials.find((d) => d.id.toLowerCase() === key) || null; }
  function selectedDial() { return selectedKey ? findDial(selectedKey) : null; }

  // ── Host messages ──────────────────────────────────────────────────────────
  core.on('dials_state', (raw) => {
    state = parse(raw) || state;
    renderList();
    renderStatus();
    fillLists();
    renderLog(state.log || []);
    if (tab !== 'dials') return;
    const d = selectedDial();
    if (d && !dirty) loadDial(d, d.id === draftId);
    else if (d) renderOrigin();
    if (!draft) renderEmpty();
  });
  core.on('dials_live', (raw) => {
    live = parse(raw) || live;
    renderListValues();
    renderLive();
    renderLog(live.log || []);
  });
  core.on('dials_reply', (raw) => {
    const r = parse(raw);
    if (r && pending[r.req]) { pending[r.req](r); delete pending[r.req]; }
  });
  core.on('dials_catalog', (raw) => {
    catalog = parse(raw);
    fillNames(catalog);
    if (tab !== 'dials') renderList();
    if (named && !named.isNew) openNamed(named.kind, named.name, true);
  });
  core.on('dials_focus', (raw) => {
    const id = parse(raw);
    if (typeof id === 'string' && id) select(id.toLowerCase());
  });

  // ── The list ───────────────────────────────────────────────────────────────
  function valueOf(d) {
    if (live.values && live.values[d.id] !== undefined) return live.values[d.id];
    return d.value;
  }
  function shown(d, v) {
    if (v === undefined || v === null) return '';
    const label = d.options && d.options[String(Math.round(v))];
    if (d.kind === 'toggle') return v >= 0.5 ? 'on' : 'off';
    return label ? label : fmt(v) + (d.unit ? (d.unit === '%' ? '%' : ' ' + d.unit) : '');
  }
  function fraction(d, v) {
    if (typeof v !== 'number' || d.max === undefined || d.max === d.min) return 0;
    return Math.max(0, Math.min(1, (v - d.min) / (d.max - d.min)));
  }

  function renderList() {
    if (tab !== 'dials') return renderNamedList();
    const q = $('search').value.trim().toLowerCase();
    const list = $('list');
    list.innerHTML = '';
    const groups = {};
    for (const d of state.dials) {
      const hay = (d.id + ' ' + (d.label || '') + ' ' + (d.category || '') + ' ' + (d.source || '')).toLowerCase();
      if (q && hay.indexOf(q) < 0) continue;
      (groups[d.category || 'dials'] = groups[d.category || 'dials'] || []).push(d);
    }
    const names = Object.keys(groups).sort();
    if (!names.length) {
      list.appendChild(el('div', 'hint', state.dials.length ? 'No dial agrees with the search.' : 'There are no dials. Click + New dial to make one.'));
    }
    for (const g of names) {
      const box = el('div', 'group');
      box.appendChild(el('div', 'group-title', g));
      groups[g].sort((a, b) => (a.label || a.id).localeCompare(b.label || b.id));
      for (const d of groups[g]) {
        const item = el('div', 'dial');
        item.dataset.key = d.id.toLowerCase();
        if (item.dataset.key === selectedKey) item.classList.add('selected');
        if (!d.live || d.enabled === false) item.classList.add('off');
        const top = el('div', 'top');
        top.appendChild(el('span', 'name', d.label || d.id));
        if (d.errors && d.errors.length) top.appendChild(el('span', 'dot'));
        top.appendChild(el('span', 'val', d.live ? shown(d, valueOf(d)) : 'off'));
        item.appendChild(top);
        item.appendChild(el('div', 'id', '$' + d.id + '  ·  ' + originText(d)));
        const mini = el('div', 'mini');
        const fill = el('div');
        fill.style.width = (fraction(d, valueOf(d)) * 100) + '%';
        mini.appendChild(fill);
        item.appendChild(mini);
        item.addEventListener('click', () => select(item.dataset.key));
        box.appendChild(item);
      }
      list.appendChild(box);
    }
  }

  function renderListValues() {
    for (const item of document.querySelectorAll('#list .dial')) {
      const d = findDial(item.dataset.key);
      if (!d || !d.live) continue;
      const v = valueOf(d);
      item.querySelector('.val').textContent = shown(d, v);
      item.querySelector('.mini div').style.width = (fraction(d, v) * 100) + '%';
    }
  }

  function originText(d) {
    switch (d.origin) {
      case 'user': return 'Made by you';
      case 'override': return 'Your version of ' + d.source;
      case 'file': return 'From ' + d.source + '.json';
      default: return 'From ' + d.source;
    }
  }

  function renderStatus() {
    const s = $('status');
    s.classList.toggle('bad', !state.ready);
    if (!state.ready) { s.textContent = 'Dials do not run: ' + (state.why || 'ConditionEngine is not installed.'); return; }
    const n = state.dials.filter((d) => d.live).length;
    const errs = (state.fileErrors || []).length;
    s.textContent = n + (n === 1 ? ' dial runs' : ' dials run') + (state.inGame === false ? ' · No game is loaded' : '') +
      (errs ? ' · ' + errs + (errs === 1 ? ' file has a problem' : ' files have problems') : '');
    renderKeyRow();
    s.title = (state.fileErrors || []).join('\n');
  }

  const EMPTY = {
    dials: ['Dials: values that change',
      'A dial is a number. Rules change the number. For example, an event sets the dial to on, a condition sets it to 10, ' +
      'or a rate decreases it each second. Each dial is also a value ($name) that other mods can read. ' +
      'SkyrimNet prompts read all dials with get_dials(npc.UUID).'],
    conditions: ['Named conditions',
      'A named condition is a condition with a name (@Name). Mods, dials and you can make named conditions. ' +
      'No Talk Just Hands, OAR and scripts can use them. SkyrimNet prompts read them with condition_engine(npc.UUID). ' +
      'You can also give a condition a text. NPCs know this text while the condition is true for them.'],
    values: ['Named values',
      'A named value is a number with a name ($name). Some values combine many mods. For example, $hunger gives the value ' +
      'from the survival mod that you use. Dials are values too. To make a value, write an expression, for example ' +
      '$hunger * 0.5 + $fatigue * 0.5.']
  };
  function showPanel(which) {
    $('empty').classList.toggle('hidden', which !== 'empty');
    $('editor').classList.toggle('hidden', which !== 'editor');
    $('named').classList.toggle('hidden', which !== 'named');
  }
  function renderEmpty() {
    const [title, text] = EMPTY[tab];
    $('empty').querySelector('h2').textContent = title;
    const ps = $('empty').querySelectorAll('p');
    ps[0].textContent = text;
    ps[1].classList.toggle('hidden', tab !== 'dials');
    $('ideas').classList.toggle('hidden', tab !== 'dials');
    showPanel('empty');
  }

  // ── Selecting, new, templates ──────────────────────────────────────────────
  function confirmLeave() {
    return !dirty || window.confirm('You did not save the changes to ' + (draft && draft.label || draftId) + '. Discard the changes?');
  }

  function select(key) {
    const d = findDial(key);
    if (!d || !confirmLeave()) return;
    selectedKey = key;
    loadDial(d);
    renderList();
  }

  // `refresh`: the same dial came back from the host; keep what the messages say
  function loadDial(d, refresh) {
    draft = clone(d.json || {});
    draftId = d.id;
    previousId = d.id;
    idTouched = true;
    dirty = false;
    showEditor();
    if (!refresh || (d.errors || []).length) showMessages(d.errors || [], d.warnings || []);
  }

  function uniqueId(base) {
    let id = (base || 'dial').replace(/[^A-Za-z0-9_]/g, '_').replace(/^(\d)/, '_$1');
    if (!findDial(id.toLowerCase())) return id;
    for (let i = 2; ; ++i) if (!findDial((id + '_' + i).toLowerCase())) return id + '_' + i;
  }

  // A name for expressions from a label: "Fear of the player" -> fear_of_the_player (dials, values), FearOfThePlayer (conditions)
  function slug(label, pascal) {
    const words = String(label || '').replace(/[^A-Za-z0-9]+/g, ' ').trim().split(/\s+/).filter(Boolean);
    if (!words.length) return '';
    const s = pascal ? words.map((w) => w[0].toUpperCase() + w.slice(1)).join('') : words.join('_').toLowerCase();
    return /^\d/.test(s) ? '_' + s : s;
  }

  function startNew(json, id) {
    if (!confirmLeave()) return;
    selectedKey = null;
    draft = clone(json);
    draftId = uniqueId(id);
    previousId = '';
    idTouched = false;
    dirty = true;
    showEditor();
    renderList();
    scheduleValidate();
    $('f-label').focus();
  }

  function renderTemplates() {
    const menu = $('templates');
    const ideas = $('ideas');
    menu.innerHTML = '';
    ideas.innerHTML = '';
    for (const t of window.DIAL_TEMPLATES || []) {
      const item = el('div', 'item');
      item.appendChild(el('b', '', t.title));
      item.appendChild(el('span', '', t.blurb));
      item.addEventListener('click', () => { menu.classList.add('hidden'); startNew(t.json, t.id); });
      menu.appendChild(item);
      const idea = el('div', 'idea');
      idea.appendChild(el('b', '', t.title));
      idea.appendChild(el('span', '', t.blurb));
      idea.addEventListener('click', () => startNew(t.json, t.id));
      ideas.appendChild(idea);
    }
  }

  // ── The editor ─────────────────────────────────────────────────────────────
  function showEditor() {
    showPanel('editor');
    renderEditor();
  }

  function kindOf(j) {
    if (j.kind) return j.kind === 'bool' || j.kind === 'switch' ? 'toggle' : (j.kind === 'options' ? 'choice' : j.kind);
    return j.options ? 'choice' : 'number';
  }

  function optionsOf(j) {
    const out = [];
    if (Array.isArray(j.options)) j.options.forEach((label, i) => out.push([i, label]));
    else if (j.options && typeof j.options === 'object') {
      for (const k of Object.keys(j.options)) out.push([Number(k), j.options[k]]);
      out.sort((a, b) => a[0] - b[0]);
    }
    return out;
  }

  function renderEditor() {
    const j = draft;
    $('f-label').value = j.label || '';
    $('f-id').value = draftId;
    const kind = kindOf(j);
    for (const b of document.querySelectorAll('#f-kind-seg button')) b.classList.toggle('active', b.dataset.kind === kind);
    const range = Array.isArray(j.range) ? j.range : [j.min !== undefined ? j.min : 0, j.max !== undefined ? j.max : 100];
    $('f-min').value = range[0];
    $('f-max').value = range[1];
    renderDefault(kind);
    $('f-unit').value = j.unit || '';
    $('f-integer').checked = !!j.integer;
    $('f-description').value = j.description || '';
    $('f-category').value = j.category || '';
    $('f-icon').value = j.icon || '';
    const scope = (j.scope || 'global').toLowerCase();
    // An actor dial with no track is the player's alone
    const track = (j.track || j.actors || '').trim();
    $('f-scope').value = scope === 'global' ? 'global' : scope === 'player' || !track || track === 'IsPlayer' ? 'player' : 'actor';
    renderTrack(track);
    $('f-persist').checked = j.persist !== false;
    $('f-thresholds').value = (j.thresholds || []).join(', ');
    $('f-precision').value = j.precision !== undefined ? j.precision : '';
    $('f-provides').value = [].concat(j.provides || []).join(', ');
    $('f-priority').value = j.priority !== undefined ? j.priority : '';
    $('f-enabled').checked = j.enabled !== false;
    for (const e of document.querySelectorAll('.when-number')) e.classList.toggle('hidden', kind !== 'number');
    for (const e of document.querySelectorAll('.when-choice')) e.classList.toggle('hidden', kind !== 'choice');
    for (const e of document.querySelectorAll('.when-actor')) e.classList.toggle('hidden', $('f-scope').value !== 'actor');
    renderOptions();
    renderRules();
    renderConditions();
    renderOrigin();
    renderLive();
    renderDialSnippets();
    $('json').value = JSON.stringify(draft, null, 2);
    $('form').classList.toggle('hidden', jsonMode);
    $('json-box').classList.toggle('hidden', !jsonMode);
  }

  // The start value: a number, on / off, or one of the options
  function renderDefault(kind) {
    const box = $('f-default-box');
    box.innerHTML = '';
    let c;
    if (kind === 'toggle' || kind === 'choice') {
      c = el('select');
      const opts = kind === 'toggle' ? [[0, 'Off'], [1, 'On']] : optionsOf(draft);
      for (const [v, l] of opts) { const o = el('option', '', l); o.value = String(v); c.appendChild(o); }
      const d = draft.default;
      const byLabel = typeof d === 'string' ? opts.find(([, l]) => l.toLowerCase() === d.toLowerCase()) : null;
      c.value = byLabel ? String(byLabel[0]) : d !== undefined ? String(d) : String(opts.length ? opts[0][0] : 0);
      c.addEventListener('change', () => { setKey('default', Number(c.value) || undefined); changed(); });
    } else {
      c = el('input');
      c.type = 'text';
      c.placeholder = 'The lowest value';
      c.value = draft.default !== undefined ? draft.default : '';
      c.addEventListener('input', () => { setKey('default', numberOrText(c.value)); changed(); });
    }
    c.id = 'f-default';
    box.appendChild(c);
  }

  // Which actors have a per-actor dial: a preset, or conditions
  // `forceCustom`: show the conditions also for a preset
  function renderTrack(track, forceCustom) {
    const t = String(track || '').trim();
    const preset = TRACK_PRESETS.find((p) => p.replace(/\s/g, '') === t.replace(/\s/g, ''));
    const custom = forceCustom || (!preset && t !== '' && t !== 'IsPlayer');
    $('f-track-preset').value = custom ? 'custom' : preset || TRACK_PRESETS[0];
    const box = $('f-track-box');
    box.innerHTML = '';
    box.classList.toggle('hidden', !custom || $('f-scope').value !== 'actor');
    if (!custom) return;
    box.appendChild(el('div', 'hint', 'An actor has the dial when these conditions are true for the actor:'));
    box.appendChild(window.CEPicker.conditionPicker({
      value: t, catalog: () => catalog, explain: (x) => explain(x, false),
      onChange: (x) => { delete draft.actors; setKey('track', x.trim()); changed(); }
    }).el);
  }

  function renderOrigin() {
    const d = selectedDial();
    const badge = $('origin');
    const revert = $('revert');
    const save = $('save');
    if (!d) {
      badge.textContent = 'New. Not saved.';
      revert.classList.add('hidden');
      save.textContent = 'Save';
    } else {
      badge.textContent = originText(d) + (d.live ? '' : ' · Off');
      revert.classList.toggle('hidden', d.origin !== 'user' && d.origin !== 'override');
      revert.textContent = d.origin === 'override' ? 'Use ' + d.source + '\'s version' : 'Delete';
      save.textContent = d.origin === 'file' || d.origin === 'mod' ? 'Save as my version' : 'Save';
    }
    save.disabled = !dirty && !!d;
  }

  function changed() {
    dirty = true;
    renderDialSnippets();
    $('json').value = JSON.stringify(draft, null, 2);
    renderOrigin();
    scheduleValidate();
  }

  function setKey(key, value) {
    if (value === undefined || value === '' || value === null) delete draft[key];
    else draft[key] = value;
  }

  function numberOrText(text) {
    const t = String(text).trim();
    if (t === '') return undefined;
    const n = Number(t);
    return isFinite(n) && t !== '' ? n : t;
  }

  function list(text) {
    return String(text).split(',').map((s) => s.trim()).filter(Boolean);
  }

  function bindBasics() {
    const on = (id, fn, ev) => $(id).addEventListener(ev || 'input', () => { fn($(id)); changed(); });
    on('f-label', (e) => {
      setKey('label', e.value);
      if (!idTouched && !selectedDial()) {
        draftId = uniqueId(slug(e.value) || 'dial');
        $('f-id').value = draftId;
        $('f-id').classList.remove('bad');
      }
    });
    $('f-id').addEventListener('input', () => {
      draftId = $('f-id').value.trim();
      idTouched = true;
      $('f-id').classList.toggle('bad', !/^[A-Za-z_][A-Za-z0-9_]*$/.test(draftId));
      changed();
    });
    for (const b of document.querySelectorAll('#f-kind-seg button')) {
      b.addEventListener('click', () => {
        const kind = b.dataset.kind;
        if (kind === kindOf(draft)) return;
        delete draft.kind; delete draft.options; delete draft.range; delete draft.min; delete draft.max; delete draft.integer;
        if (kind === 'toggle') draft.kind = 'toggle';
        if (kind === 'choice') { draft.kind = 'choice'; draft.options = ['Off', 'On']; }
        if (kind === 'number') draft.range = [0, 100];
        delete draft.default;
        renderEditor();
        changed();
      });
    }
    const range = () => {
      const min = Number($('f-min').value), max = Number($('f-max').value);
      draft.range = [isFinite(min) ? min : 0, isFinite(max) ? max : 100];
      delete draft.min; delete draft.max;
    };
    on('f-min', range);
    on('f-max', range);
    on('f-unit', (e) => setKey('unit', e.value.trim()));
    on('f-integer', (e) => setKey('integer', e.checked || undefined), 'change');
    on('f-description', (e) => setKey('description', e.value));
    on('f-category', (e) => setKey('category', e.value.trim()));
    on('f-icon', (e) => setKey('icon', e.value.trim()));
    on('f-scope', (e) => {
      setKey('scope', e.value === 'global' ? undefined : e.value);
      delete draft.actors;
      if (e.value !== 'actor') delete draft.track;
      else if (!draft.track) draft.track = TRACK_PRESETS[0];
      for (const x of document.querySelectorAll('.when-actor')) x.classList.toggle('hidden', e.value !== 'actor');
      renderTrack(draft.track);
    }, 'change');
    on('f-track-preset', (e) => {
      delete draft.actors;
      const custom = e.value === 'custom';
      if (!custom) draft.track = e.value;
      renderTrack(draft.track, custom);
    }, 'change');
    on('f-persist', (e) => setKey('persist', e.checked ? undefined : false), 'change');
    on('f-thresholds', (e) => {
      const t = list(e.value).map(Number).filter((n) => isFinite(n));
      setKey('thresholds', t.length ? t : undefined);
    });
    on('f-precision', (e) => setKey('precision', e.value === '' ? undefined : Number(e.value)));
    on('f-provides', (e) => { const p = list(e.value); setKey('provides', p.length ? p : undefined); });
    on('f-priority', (e) => setKey('priority', e.value === '' ? undefined : Number(e.value)));
    on('f-enabled', (e) => setKey('enabled', e.checked ? undefined : false), 'change');
  }

  // options
  function renderOptions() {
    const box = $('options');
    box.innerHTML = '';
    const opts = optionsOf(draft);
    opts.forEach(([value, label], i) => {
      const row = el('div', 'row');
      const v = el('input', 'narrow');
      v.type = 'number';
      v.value = value;
      const l = el('input', 'grow');
      l.value = label;
      const x = el('button', 'small', '✕');
      const write = () => {
        const o = {};
        document.querySelectorAll('#options .row').forEach((r) => {
          const ins = r.querySelectorAll('input');
          if (ins[1].value.trim() !== '') o[String(Math.round(Number(ins[0].value) || 0))] = ins[1].value;
        });
        draft.options = o;
        renderDefault('choice');
        changed();
      };
      v.title = 'The number of the option';
      l.placeholder = 'Name of the option';
      x.title = 'Remove this option';
      v.addEventListener('input', write);
      l.addEventListener('input', write);
      x.addEventListener('click', () => { row.remove(); write(); });
      row.append(v, l, x);
      box.appendChild(row);
    });
  }
  $('add-option').addEventListener('click', () => {
    const opts = optionsOf(draft);
    const next = opts.length ? opts[opts.length - 1][0] + 1 : 0;
    const o = {};
    for (const [v, l] of opts) o[String(v)] = l;
    o[String(next)] = 'Option ' + (next + 1);
    draft.options = o;
    renderOptions();
    renderDefault('choice');
    changed();
  });

  // conditions
  // A condition of the dial in short form: ">= 75" (a number), "== 1" (on / off), "Casual" (an option label).
  // Returns {op, value} or null for a full expression.
  function shortForm(text) {
    const t = String(text || '').trim();
    const m = /^(>=|<=|==|!=|>|<|=)\s*(-?\d+(?:\.\d+)?)$/.exec(t);
    if (m) return { op: m[1] === '=' ? '==' : m[1], value: Number(m[2]) };
    const opt = optionsOf(draft).find(([, label]) => label.toLowerCase() === t.toLowerCase());
    return opt ? { op: '==', value: opt[0] } : null;
  }

  function renderConditions() {
    const box = $('conditions');
    box.innerHTML = '';
    const conds = draft.conditions || {};
    const rows = Object.keys(conds).map((name) => {
      const entry = conds[name];
      const obj = typeof entry === 'object' && entry;
      return { name: name, expr: obj ? entry.expr || '' : String(entry), knowledge: obj ? entry.knowledge || '' : '' };
    });
    if (!rows.length) box.appendChild(el('div', 'hint', 'The dial has no named conditions.'));
    const write = () => {
      const o = {};
      for (const r of rows) {
        const key = r.name.trim().replace(/^@/, '');
        if (!key) continue;
        o[key] = r.knowledge.trim() ? { expr: r.expr, knowledge: r.knowledge.trim() } : r.expr;
      }
      draft.conditions = o;
      if (!Object.keys(o).length) delete draft.conditions;
      changed();
    };
    const kind = kindOf(draft);
    rows.forEach((r, index) => {
      const wrap = el('div', 'cond-row');
      const row = el('div', 'row');
      const n = el('input', 'cond-name');
      n.value = r.name;
      n.title = 'The name that other mods use: @' + r.name;
      n.addEventListener('input', () => { r.name = n.value; write(); });
      row.append(el('span', 'dollar', '@'), n);
      const short = shortForm(r.expr);
      if (short) {
        row.appendChild(el('span', 'word', 'is true when the dial is'));
        if (kind === 'toggle' && short.op === '==' && (short.value === 0 || short.value === 1)) {
          const s = select2([['1', 'on'], ['0', 'off']], String(short.value));
          s.addEventListener('change', () => { r.expr = '== ' + s.value; write(); });
          row.appendChild(s);
        } else if (kind === 'choice' && short.op === '==') {
          const opts = optionsOf(draft);
          const s = select2(opts.map(([v, l]) => [String(v), l]), String(short.value));
          s.addEventListener('change', () => { const o = opts.find(([v]) => String(v) === s.value); r.expr = o ? o[1] : '== ' + s.value; write(); });
          row.appendChild(s);
        } else {
          const op = select2([['>=', 'at least'], ['>', 'more than'], ['<=', 'at most'], ['<', 'less than'], ['==', 'equal to'], ['!=', 'not equal to']], short.op);
          const v = el('input', 'cond-num');
          v.type = 'number';
          v.step = 'any';
          v.value = short.value;
          const upd = () => { r.expr = op.value + ' ' + (v.value === '' ? 0 : v.value); write(); };
          op.addEventListener('change', upd);
          v.addEventListener('input', upd);
          row.append(op, v);
        }
      } else {
        row.appendChild(el('span', 'word', 'is true when'));
      }
      const more = el('button', 'small', short ? 'Other conditions' : 'Simple');
      more.title = short ? 'Use other conditions too, not only the value of this dial.' : 'Compare only the value of this dial.';
      more.addEventListener('click', () => {
        r.expr = short ? conditionExpr(r.expr) : '>= 50';
        write();
        renderConditions();
      });
      const x = el('button', 'small', '✕');
      x.title = 'Remove this condition';
      x.addEventListener('click', () => { rows.splice(index, 1); write(); renderConditions(); });
      row.append(more, x);
      wrap.appendChild(row);
      if (!short) {
        wrap.appendChild(window.CEPicker.conditionPicker({
          value: r.expr, catalog: () => catalog, explain: (t) => explain(t, false),
          onChange: (t) => { r.expr = t; write(); }
        }).el);
      }
      const k = el('input', 'knowledge');
      k.placeholder = 'What NPCs know while it is true (SkyrimNet). Optional.';
      k.value = r.knowledge;
      k.addEventListener('input', () => { r.knowledge = k.value; write(); });
      if (!r.knowledge) {
        k.classList.add('hidden');
        const add = el('button', 'link', '+ What NPCs know (SkyrimNet)');
        add.addEventListener('click', () => { add.remove(); k.classList.remove('hidden'); k.focus(); });
        wrap.appendChild(add);
      }
      wrap.appendChild(k);
      box.appendChild(wrap);
    });
  }
  function conditionExpr(text) {
    const t = text.trim();
    if (/^(>=|<=|==|!=|>|<|=)/.test(t)) return '$' + draftId + ' ' + t.replace(/^=(?!=)/, '==');
    for (const [v, label] of optionsOf(draft)) if (label.toLowerCase() === t.toLowerCase()) return '$' + draftId + ' == ' + v;
    return t;
  }
  $('add-condition').addEventListener('click', () => {
    draft.conditions = draft.conditions || {};
    const kind = kindOf(draft);
    const opts = optionsOf(draft);
    const base = slug(draft.label || draftId, true) || 'Dial';
    let name = kind === 'toggle' ? base + 'On' : kind === 'choice' && opts.length ? base + slug(opts[opts.length - 1][1], true) : base + 'High';
    while (draft.conditions[name] !== undefined) name += '2';
    draft.conditions[name] = kind === 'toggle' ? '== 1' : kind === 'choice' && opts.length ? opts[opts.length - 1][1] : '>= 50';
    renderConditions();
    changed();
  });

  // ── Rules ──────────────────────────────────────────────────────────────────
  function triggerOf(r) { for (const [k] of TRIGGERS) if (r[k] !== undefined) return k; return 'on'; }
  function actionOf(r) { for (const [k] of ACTIONS) if (r[k] !== undefined) return k; return 'set'; }
  function rules() { return (draft.rules = Array.isArray(draft.rules) ? draft.rules : []); }

  function select2(options, value) {
    const s = el('select');
    for (const [v, label] of options) {
      const o = el('option', '', label);
      o.value = v;
      s.appendChild(o);
    }
    s.value = value;
    return s;
  }

  function input(value, cls, placeholder) {
    const i = el('input', cls || '');
    i.value = value === undefined || value === null ? '' : value;
    if (placeholder) i.placeholder = placeholder;
    return i;
  }

  function exprInput(value, placeholder, isValue) {
    const wrap = el('span', 'expr-wrap');
    const i = input(value, 'expr', placeholder);
    i.setAttribute('list', 'names');
    const why = el('button', 'why', '?');
    why.title = isValue ? 'Show the value now, for the player, and how ConditionEngine calculates it.' : 'Show why this is true or false now, for the player.';
    why.addEventListener('click', () => explain(i.value, !!isValue));
    wrap.append(i, why);
    return { wrap: wrap, input: i };
  }

  function renderRules() {
    const box = $('rules');
    box.innerHTML = '';
    const rs = rules();
    if (!rs.length) box.appendChild(el('div', 'hint', 'The dial has no rules. Only a script, a mod or you can change it.'));
    rs.forEach((r, i) => box.appendChild(ruleCard(r, i)));
  }

  function ruleCard(r, index) {
    const card = el('div', 'rule' + (r.enabled === false ? ' disabled' : ''));
    const head = el('div', 'rule-head');
    head.appendChild(el('span', 'num', '#' + (index + 1)));
    const label = input(r.label, '', 'Name of the rule (optional)');
    label.addEventListener('input', () => { if (label.value) r.label = label.value; else delete r.label; changed(); });
    head.appendChild(label);
    const en = el('label', 'check');
    en.style.paddingTop = '0';
    const enBox = el('input');
    enBox.type = 'checkbox';
    enBox.checked = r.enabled !== false;
    enBox.addEventListener('change', () => { if (enBox.checked) delete r.enabled; else r.enabled = false; card.classList.toggle('disabled', !enBox.checked); changed(); });
    en.append(enBox, document.createTextNode('On'));
    en.title = 'Clear this box to stop the rule. The editor keeps the rule.';
    head.appendChild(en);
    const test = el('button', 'small', 'Test');
    test.title = 'Run the saved version of this rule one time now, on the player.';
    test.addEventListener('click', () => {
      if (dirty) { showMessages([], ['Save the dial first. Test runs the saved rules.']); return; }
      request('fire', { id: draftId, rule: index }).then((res) => showResult(res, 'Rule ' + (index + 1) + ' ran.'));
    });
    const up = el('button', 'small', '▲');
    const down = el('button', 'small', '▼');
    const del = el('button', 'small danger', '✕');
    up.title = 'Move the rule up';
    down.title = 'Move the rule down';
    del.title = 'Remove the rule';
    up.disabled = index === 0;
    down.disabled = index === rules().length - 1;
    up.addEventListener('click', () => { const rs = rules(); rs.splice(index - 1, 0, rs.splice(index, 1)[0]); renderRules(); changed(); });
    down.addEventListener('click', () => { const rs = rules(); rs.splice(index + 1, 0, rs.splice(index, 1)[0]); renderRules(); changed(); });
    del.addEventListener('click', () => { rules().splice(index, 1); renderRules(); changed(); });
    head.append(test, up, down, del);
    card.appendChild(head);

    // WHEN
    const trig = triggerOf(r);
    const whenLine = el('div', 'rule-line');
    whenLine.appendChild(el('span', 'word', 'When'));
    const trigSel = select2(TRIGGERS, trig);
    whenLine.appendChild(trigSel);
    const tf = el('div', 'fields');
    let help = '';
    if (trig === 'on') {
      const s = select2(GAME_EVENTS().map((e) => [e.name, e.name]), r.on);
      s.addEventListener('change', () => { r.on = s.value; changed(); renderRules(); });
      tf.appendChild(s);
      const ev = GAME_EVENTS().find((e) => e.name === r.on);
      help = ev ? ev.help : '';
    } else if (trig === 'event') {
      const i = input(r.event, '', 'Name of the mod event');
      i.setAttribute('list', 'event-names');
      i.addEventListener('input', () => { r.event = i.value.trim(); changed(); });
      tf.appendChild(i);
      help = 'An SKSE mod event (SendModEvent), or Dials.Signal from a script. DialsEvent gives the number of the event.';
    } else if (trig === 'when') {
      const edge = select2([['rising', 'to true'], ['falling', 'to false'], ['both', 'to true or false']], r.edge || 'rising');
      edge.addEventListener('change', () => { if (edge.value === 'rising') delete r.edge; else r.edge = edge.value; changed(); });
      tf.appendChild(edge);
      help = 'ConditionEngine examines the conditions at each tick. The rule runs one time when the result changes.';
    } else if (trig === 'while') {
      help = 'The rule runs at each tick while the conditions are true. With no conditions, the rule runs always.';
    } else if (trig === 'every') {
      const i = input(r.every, '', '5');
      i.type = 'number';
      i.style.width = '90px';
      i.addEventListener('input', () => { r.every = Number(i.value) || 1; changed(); });
      tf.append(el('span', 'unit', 'each'), i, el('span', 'unit', r.clock === 'game' ? 'game hours' : 'seconds'));
    } else if (trig === 'skyrimnet') {
      const s = select2(SKYRIMNET_EVENTS.concat(SKYRIMNET_EVENTS.indexOf(r.skyrimnet) < 0 && r.skyrimnet ? [r.skyrimnet] : []).map((e) => [e, e]), r.skyrimnet);
      s.addEventListener('change', () => { r.skyrimnet = s.value; changed(); });
      tf.appendChild(s);
      help = 'A SkyrimNet event about the actor. For example, dialogue: a person spoke. To change the actor who heard it, set "Acts on" to the other actor.';
    } else if (trig === 'aiAction') {
      const i = input(r.aiAction, '', 'CalmDown');
      i.style.width = '170px';
      i.title = 'The name of the action';
      i.addEventListener('input', () => { r.aiAction = i.value.trim(); changed(); });
      const d = input(r.description, '', 'The AI reads this text, for example: Calm down after a manic spell');
      d.addEventListener('input', () => { r.description = d.value; changed(); });
      tf.append(i, d);
      help = 'SkyrimNet shows this action to an NPC that has the dial, when "Only if" is true. When the AI selects the action, this rule runs.';
    } else if (trig === 'key') {
      const keys = KEY_CHOICES.indexOf(r.key) < 0 && r.key ? [r.key].concat(KEY_CHOICES) : KEY_CHOICES;
      const s = select2(keys.map((k) => [k, k]), r.key);
      s.addEventListener('change', () => { r.key = s.value; changed(); });
      tf.appendChild(s);
      help = 'A key on the keyboard. It does not work in menus. To change the actor in the crosshair, set "Acts on" to the crosshair target.';
    }
    whenLine.appendChild(tf);
    card.appendChild(whenLine);
    if (trig === 'when' || trig === 'while') {
      const box = el('div', 'rule-cond');
      box.appendChild(window.CEPicker.conditionPicker({
        value: r[trig], catalog: () => catalog, explain: (t) => explain(t, false),
        empty: trig === 'while' ? 'No conditions: the rule runs always.' : 'Add a condition.',
        onChange: (t) => { r[trig] = trig === 'while' && !t.trim() ? true : t; changed(); }
      }).el);
      card.appendChild(box);
    }
    trigSel.addEventListener('change', () => {
      for (const [k] of TRIGGERS) delete r[k];
      const t = trigSel.value;
      r[t] = t === 'on' ? 'Hit' : t === 'event' ? 'MyMod_Event' : t === 'while' ? true : t === 'every' ? 5 : t === 'key' ? 'F7' :
        t === 'skyrimnet' ? 'dialogue' : t === 'aiAction' ? 'MyAction' : 'Combat';
      if (t === 'aiAction' && !r.description) r.description = 'Describe when the NPC should do this';
      if (t !== 'aiAction') delete r.description;
      const a = actionOf(r);
      const allowed = t === 'while' ? CONTINUOUS : DISCRETE;
      if (allowed.indexOf(a) < 0) { delete r[a]; delete r.halfLife; delete r.sendNumber; delete r.sendText; r[allowed[0]] = a === 'rate' || a === 'approach' ? 1 : 1; }
      if (t === 'while') delete r.for;
      renderRules();
      changed();
    });

    // DO
    const act = actionOf(r);
    const doLine = el('div', 'rule-line');
    doLine.appendChild(el('span', 'word', 'Do'));
    const allowed = trig === 'while' ? CONTINUOUS : DISCRETE;
    const actSel = select2(ACTIONS.filter(([k]) => allowed.indexOf(k) >= 0).map(([k, l]) => [k, k === 'set' && trig === 'while' ? 'keep the value at' : l]), act);
    doLine.appendChild(actSel);
    const af = el('div', 'fields');
    if (act === 'send') {
      // The mod event: a name, and optionally a number (else the dial's value) and a text
      const name = input(r.send, '', 'Name of the mod event');
      name.setAttribute('list', 'event-names');
      name.addEventListener('input', () => { r.send = name.value.trim(); changed(); });
      const num = input(r.sendNumber, '', 'Dial value');
      num.title = 'The number of the event (numArg). Leave empty to send the value of the dial.';
      num.style.width = '120px';
      num.addEventListener('input', () => { const v = numberOrText(num.value); if (v === undefined) delete r.sendNumber; else r.sendNumber = v; changed(); });
      const txt = input(r.sendText, '', 'Text');
      txt.title = 'The text of the event (strArg)';
      txt.style.width = '140px';
      txt.addEventListener('input', () => { if (txt.value) r.sendText = txt.value; else delete r.sendText; changed(); });
      af.classList.add('wrap');
      name.style.width = '200px';
      const pair = (label, box) => { const s = el('span', 'pair'); s.append(el('span', 'unit', label), box); return s; };
      af.append(name, pair('number', num), pair('text', txt));
    } else if (act !== 'toggle' && act !== 'reset') {
      const x = exprInput(r[act] === true ? '' : r[act], act === 'cycle' ? '1, or -1 for back' : 'A number, or a value such as $hunger / 10', true);
      x.input.addEventListener('input', () => { r[act] = numberOrText(x.input.value); if (r[act] === undefined) r[act] = act === 'cycle' ? 1 : 0; changed(); });
      x.wrap.insertBefore(window.CEPicker.insertValueButton(x.input, () => catalog, () => {
        r[act] = numberOrText(x.input.value);
        changed();
      }), x.wrap.lastChild);
      af.appendChild(x.wrap);
      if (act === 'rate') af.appendChild(el('span', 'unit', r.clock === 'game' ? 'each game hour' : 'each second'));
      if (act === 'approach') {
        af.appendChild(el('span', 'unit', 'half the distance in'));
        const h = input(r.halfLife, '', '30');
        h.type = 'number';
        h.style.width = '80px';
        h.addEventListener('input', () => { r.halfLife = Number(h.value) || 1; changed(); });
        af.append(h, el('span', 'unit', r.clock === 'game' ? 'game hours' : 'seconds'));
      }
    }
    doLine.appendChild(af);
    card.appendChild(doLine);
    actSel.addEventListener('change', () => {
      const old = actionOf(r);
      const keep = old === 'send' ? undefined : r[old];  // an event name is not an amount
      delete r[old];
      const a = actSel.value;
      if (old === 'send') { delete r.sendNumber; delete r.sendText; }
      if (a === 'send') { r.send = 'MyMod_Event'; delete r.for; }
      else if (a === 'toggle' || a === 'reset') r[a] = true;
      else r[a] = typeof keep === 'number' || typeof keep === 'string' ? keep : (a === 'cycle' ? 1 : a === 'multiply' ? 1 : 0);
      if (a === 'approach' && !r.halfLife) r.halfLife = 30;
      if (a !== 'approach') delete r.halfLife;
      renderRules();
      changed();
    });
    if (act === 'send') help = (help ? help + ' ' : '') + 'Scripts get the event with RegisterForModEvent. The event goes out at the next tick. This dial does not get its own event.';
    if (help) card.appendChild(el('div', 'help', help));

    // MORE
    const more = el('details', 'more');
    const extras = ['if', 'for', 'cooldown', 'chance', 'actor', 'clock', 'form', 'keyword', 'text', 'other', 'otherIf', 'tell'];
    if (extras.some((k) => r[k] !== undefined)) more.open = true;
    more.appendChild(el('summary', '', 'More: only if, duration, wait, chance, filters'));
    // Only if: conditions on the actor, with the picker
    const onlyIf = el('div', 'rule-cond');
    onlyIf.appendChild(el('div', 'sub-label', 'Only if'));
    onlyIf.appendChild(window.CEPicker.conditionPicker({
      value: r.if, catalog: () => catalog, explain: (t) => explain(t, false),
      empty: 'No conditions: the rule runs for all actors.',
      onChange: (t) => { if (t.trim()) r.if = t; else delete r.if; changed(); }
    }).el);
    more.appendChild(onlyIf);
    const grid = el('div', 'grid');
    const field = (key, label, placeholder, kind) => {
      const l = el('label', '', label);
      let i;
      if (kind === 'select') {
        i = select2(placeholder, r[key] || placeholder[0][0]);
        i.addEventListener('change', () => { if (i.value === placeholder[0][0]) delete r[key]; else r[key] = i.value; changed(); renderRules(); });
      } else {
        i = input(r[key], kind === 'expr' ? 'expr' : '', placeholder);
        if (kind === 'number') i.type = 'number';
        if (kind === 'expr') i.setAttribute('list', 'names');
        i.addEventListener('input', () => {
          const v = kind === 'number' ? (i.value === '' ? undefined : Number(i.value)) : i.value.trim();
          if (v === undefined || v === '') delete r[key]; else r[key] = v;
          changed();
        });
      }
      l.appendChild(i);
      grid.appendChild(l);
    };
    if (trig !== 'while' && act !== 'send') field('for', 'Undo after (seconds)', 'Never', 'number');
    field('cooldown', 'Wait before it runs again (seconds)', '0', 'number');
    field('chance', 'Chance (0 to 1)', '1', 'number');
    field('actor', 'Acts on', [['auto', 'the actor of the event'], ['other', 'the other actor'], ['player', 'the player'], ['crosshair', 'the crosshair target']], 'select');
    field('clock', 'Clock', [['real', 'real seconds'], ['game', 'game hours']], 'select');
    if (trig !== 'while') {
      field('tell', 'Tell SkyrimNet', '{name} had a manic spell.', 'text');
      field('tellAs', 'As', [['memory', 'a memory'], ['narration', 'an event that NPCs react to']], 'select');
    }
    if (trig === 'on' || trig === 'event' || trig === 'skyrimnet') {
      field('form', 'The form is', '0x12EB7|Skyrim.esm', 'text');
      field('keyword', 'The form has the keyword', 'WeapTypeSword', 'text');
      field('text', trig === 'event' ? 'The text is' : 'The name is (menu, animation, skill)', '', 'text');
      field('other', 'The other actor is', [['any', 'any actor'], ['player', 'the player'], ['!player', 'not the player']], 'select');
    }
    more.appendChild(grid);
    if (trig === 'on' || trig === 'event' || trig === 'skyrimnet') {
      const other = el('div', 'rule-cond');
      other.appendChild(el('div', 'sub-label', 'Only if the other actor agrees with'));
      other.appendChild(window.CEPicker.conditionPicker({
        value: r.otherIf, catalog: () => catalog, explain: (t) => explain(t, false),
        empty: 'No conditions.',
        onChange: (t) => { if (t.trim()) r.otherIf = t; else delete r.otherIf; changed(); }
      }).el);
      more.appendChild(other);
    }
    card.appendChild(more);
    return card;
  }

  $('add-rule').addEventListener('click', () => {
    rules().push({ on: 'Hit', add: 1 });
    renderRules();
    changed();
  });

  // ── Live values ────────────────────────────────────────────────────────────
  function renderLive() {
    const d = selectedDial();
    const box = $('live');
    if (!d || !d.live || dirty && !d) { box.classList.add('hidden'); return; }
    box.classList.remove('hidden');
    const v = valueOf(d);
    const label = d.options && d.options[String(Math.round(v))];
    $('live-number').textContent = d.kind === 'toggle' ? (v >= 0.5 ? 'on' : 'off') : label || fmt(v);
    $('live-unit').textContent = d.unit || '';
    $('live-bar').style.width = (fraction(d, v) * 100) + '%';
    const slider = $('live-slider');
    slider.min = d.min;
    slider.max = d.max;
    slider.step = d.integer ? 1 : (d.max - d.min) / 200;
    if (document.activeElement !== slider) slider.value = v;
    $('live-toggle').classList.toggle('hidden', d.kind !== 'toggle');
    $('live-cycle').classList.toggle('hidden', d.kind !== 'choice');
    $('live-who').textContent = d.scope === 'actor' ? 'Shows the player. ' + (d.tracked || 0) + ' actors near you have the dial.' : d.scope === 'player' ? 'The player only' : 'One value for the game';
    const actors = $('live-actors');
    actors.innerHTML = '';
    const rows = (live.actors && live.actors[d.id]) || d.actors || [];
    for (const a of rows) {
      if (a.key === '0x00000014') continue;
      const row = el('div', 'actor-row');
      row.appendChild(el('span', 'who', a.name));
      row.appendChild(el('span', 'v', shown(d, a.value)));
      const reset = el('button', 'small', 'Reset');
      reset.addEventListener('click', () => send('reset', { id: d.id, actor: a.key }));
      row.appendChild(reset);
      actors.appendChild(row);
    }
  }
  $('live-slider').addEventListener('change', () => {
    const d = selectedDial();
    if (d) send('set', { id: d.id, value: Number($('live-slider').value) });
  });
  $('live-reset').addEventListener('click', () => { const d = selectedDial(); if (d) send('reset', { id: d.id }); });
  $('live-toggle').addEventListener('click', () => { const d = selectedDial(); if (d) send('toggle', { id: d.id }); });
  $('live-cycle').addEventListener('click', () => { const d = selectedDial(); if (d) send('cycle', { id: d.id, step: 1 }); });

  // ── Validation, saving ─────────────────────────────────────────────────────
  function showMessages(errors, warnings, ok) {
    const box = $('messages');
    box.innerHTML = '';
    for (const e of errors || []) box.appendChild(el('div', 'msg error', e));
    for (const w of warnings || []) box.appendChild(el('div', 'msg warn', w));
    if (ok) box.appendChild(el('div', 'msg ok', ok));
  }

  function showResult(res, okText) {
    showMessages(res.errors, res.warnings, res.ok ? okText : '');
  }

  function scheduleValidate() {
    clearTimeout(validateTimer);
    validateTimer = setTimeout(() => {
      if (!draft) return;
      const id = draftId;
      request('validate', { id: id, json: draft }).then((res) => {
        if (id !== draftId) return;
        showMessages(res.errors, res.warnings);
        $('save').disabled = !res.ok;
      });
    }, 350);
  }

  $('save').addEventListener('click', () => {
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(draftId)) { showMessages(['Use only letters, digits and _ in the name. Do not start the name with a digit.'], []); return; }
    request('save', { id: draftId, json: draft, previousId: previousId }).then((res) => {
      showResult(res, 'Saved. $' + draftId + ' runs now.');
      if (res.ok) {
        dirty = false;
        previousId = draftId;
        selectedKey = draftId.toLowerCase();
        renderOrigin();
      }
    });
  });

  $('revert').addEventListener('click', () => {
    const d = selectedDial();
    if (!d) return;
    const what = d.origin === 'override' ? 'Discard your version and use the version from ' + d.source + '?' : 'Delete ' + (d.label || d.id) + '?';
    if (!window.confirm(what)) return;
    dirty = false;
    if (d.origin === 'user') { selectedKey = null; draft = null; renderEmpty(); }
    send('revert', { id: d.id });
  });

  $('duplicate').addEventListener('click', () => {
    const copy = clone(draft);
    copy.label = (copy.label || draftId) + ' (copy)';
    delete copy.conditions;  // condition names must be unique
    startNew(copy, draftId + '_copy');
    idTouched = true;
  });

  $('mode').addEventListener('click', () => { jsonMode = true; renderEditor(); });
  $('mode-back').addEventListener('click', () => { jsonMode = false; renderEditor(); });
  $('json').addEventListener('input', () => {
    try {
      draft = JSON.parse($('json').value);
      $('json').classList.remove('bad');
      dirty = true;
      renderOrigin();
      scheduleValidate();
    } catch (e) {
      $('json').classList.add('bad');
      showMessages(['The JSON text has an error: ' + e.message], []);
    }
  });

  // ── Explain ────────────────────────────────────────────────────────────────
  function explain(expr, isValue) {
    if (!expr || !String(expr).trim()) return;
    request('explain', { expr: String(expr), value: !!isValue }).then((res) => {
      $('explain-text').textContent = expr + '\n\n' + (res.text || (res.errors || []).join('\n') || 'ConditionEngine gave no answer.');
      $('explain').classList.remove('hidden');
    });
  }
  $('explain-close').addEventListener('click', () => $('explain').classList.add('hidden'));
  document.addEventListener('click', (e) => {
    const b = e.target.closest && e.target.closest('button.why[data-for]');
    if (b) explain($(b.dataset.for).value, false);
  });

  // ── Activity, signals ──────────────────────────────────────────────────────
  function renderLog(entries) {
    const box = $('log');
    box.innerHTML = '';
    if (!entries.length) { box.appendChild(el('div', 'hint', 'When a rule changes a dial, the change shows here.')); return; }
    for (const e of entries.slice().reverse()) {
      const row = el('div', 'log-row');
      const what = el('div', 'what');
      what.appendChild(el('b', '', e.id ? '$' + e.id : 'Dials'));
      if (e.id && e.from !== e.to) what.appendChild(el('span', '', fmt(e.from) + ' → ' + fmt(e.to)));
      row.appendChild(what);
      row.appendChild(el('div', 'why', e.cause + (e.actor && e.actor !== 'global' && e.actor !== 'player' ? ' · ' + e.actor : '')));
      row.appendChild(el('div', 't', fmt(e.t) + ' s'));
      box.appendChild(row);
    }
  }
  $('sig-send').addEventListener('click', () => {
    const name = $('sig-name').value.trim();
    if (name) send('signal', { event: name, number: Number($('sig-number').value) || 0 });
  });

  // ── Suggestions ────────────────────────────────────────────────────────────
  function fillLists() {
    const cats = new Set(['dials']);
    const events = new Set();
    for (const d of state.dials) {
      if (d.category) cats.add(d.category);
      for (const r of (d.json && d.json.rules) || []) if (r.event) events.add(r.event);
    }
    $('categories').innerHTML = '';
    for (const c of cats) { const o = el('option'); o.value = c; $('categories').appendChild(o); }
    $('event-names').innerHTML = '';
    for (const e of events) { const o = el('option'); o.value = e; $('event-names').appendChild(o); }
  }

  function fillNames(catalog) {
    if (!catalog || typeof catalog !== 'object') return;
    const names = $('names');
    names.innerHTML = '';
    const add = (value, label) => { const o = el('option'); o.value = value; if (label) o.label = label; names.appendChild(o); };
    for (const t of catalog.types || []) add(t.name, t.label);
    for (const v of catalog.namedValues || catalog.values || []) add('$' + v.name, v.label);
    for (const c of catalog.namedConditions || catalog.conditions || []) add('@' + (c.id || c.name), c.label);
  }

  // ── Snippets: lines to copy into prompts, expressions, scripts ─────────────
  function copyText(input, button) {
    input.focus();
    input.select();
    let ok = false;
    try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
    if (!ok && navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(input.value).then(() => { button.textContent = 'Copied'; }, () => {});
    }
    button.textContent = ok ? 'Copied' : 'Push Ctrl+C';
    setTimeout(() => { button.textContent = 'Copy'; }, 1500);
  }

  function snippetRows(box, rows) {
    box.innerHTML = '';
    for (const [what, code] of rows) {
      const row = el('div', 'snippet');
      row.appendChild(el('span', 'what', what));
      const i = el('input');
      i.readOnly = true;
      i.value = code;
      i.addEventListener('focus', () => i.select());
      const b = el('button', 'small', 'Copy');
      b.addEventListener('click', () => copyText(i, b));
      row.append(i, b);
      box.appendChild(row);
    }
  }

  function dialSnippetRows(id, j) {
    const kind = kindOf(j);
    const opts = optionsOf(j);
    const rows = [['Prompt: the value', '{{ get_dials(npc.UUID).' + id + ' }}']];
    if (kind === 'toggle') rows.push(['Prompt: only when on', '{% if get_dials(npc.UUID).' + id + ' %} … {% endif %}']);
    else if (kind === 'choice' && opts.length) rows.push(['Prompt: one option', '{% if get_dials(npc.UUID).' + id + ' == "' + opts[opts.length - 1][1] + '" %} … {% endif %}']);
    else rows.push(['Prompt: at a level or more', '{% if get_dials(npc.UUID).' + id + ' >= 50 %} … {% endif %}']);
    for (const name of Object.keys(j.conditions || {})) {
      rows.push(['Prompt: @' + name, '{% if default(condition_engine(npc.UUID).conditions.' + name + ', false) %} … {% endif %}']);
    }
    rows.push(['Expression', kind === 'toggle' ? '$' + id + ' == 1' : '$' + id + ' >= 50']);
    for (const name of Object.keys(j.conditions || {})) rows.push(['Expression: @' + name, '@' + name]);
    rows.push(['Papyrus', 'Dials.GetValue("' + id + '", akActor)']);
    return rows;
  }

  function renderDialSnippets() {
    if (!draft || !$('dial-snippets')) return;
    snippetRows($('dial-snippets'), dialSnippetRows(draftId || 'name', draft));
  }

  // ── Conditions and values tabs ─────────────────────────────────────────────
  function namedItems(kind) {
    const list = kind === 'value' ? (catalog && catalog.namedValues) || [] : (catalog && catalog.namedConditions) || [];
    const user = (kind === 'value' ? state.userValues : state.userConditions) || {};
    const out = list.map((it) => {
      const name = kind === 'value' ? it.name : it.id;
      const own = Object.keys(user).find((k) => k.toLowerCase() === String(name).toLowerCase());
      return { name: name, item: it, isUser: !!own, json: own ? user[own] : null };
    });
    for (const k of Object.keys(user)) {
      if (!out.some((o) => String(o.name).toLowerCase() === k.toLowerCase())) out.push({ name: k, item: { source: 'User', category: 'mine' }, isUser: true, json: user[k] });
    }
    return out;
  }

  function renderNamedList() {
    const kind = tab === 'values' ? 'value' : 'condition';
    const q = $('search').value.trim().toLowerCase();
    const list = $('list');
    list.innerHTML = '';
    if (!catalog) { list.appendChild(el('div', 'hint', 'The editor reads ConditionEngine…')); return; }
    const groups = {};
    const cats = {};
    for (const c of catalog.categories || []) cats[c.id] = (c.icon ? c.icon + '  ' : '') + (c.label || c.id);
    for (const n of namedItems(kind)) {
      const it = n.item || {};
      const hay = (n.name + ' ' + (it.label || '') + ' ' + (it.category || '') + ' ' + (it.source || '') + ' ' + (it.expression || '')).toLowerCase();
      if (q && hay.indexOf(q) < 0) continue;
      const g = n.isUser ? 'Yours' : cats[it.category] || it.category || 'Other';
      (groups[g] = groups[g] || []).push(n);
    }
    const names = Object.keys(groups).sort((a, b) => (a === 'Yours' ? -1 : b === 'Yours' ? 1 : a.replace(/^\W+/, '').localeCompare(b.replace(/^\W+/, ''))));
    if (!names.length) list.appendChild(el('div', 'hint', 'No item agrees with the search.'));
    const isOpen = (x) => named && named.kind === kind && lc(named.name) === lc(x.name);
    for (const g of names) {
      const box = el('div', 'group');
      box.appendChild(el('div', 'group-title', g));
      groups[g].sort((a, b) => String((a.item || {}).label || a.name).localeCompare(String((b.item || {}).label || b.name)));
      // Items that mods keep out of lists (each survival mod on its own, stages) fold away, unless you search
      const hidden = groups[g].filter((n) => !n.isUser && (n.item || {}).offer === 'nowhere' && !q && !isOpen(n));
      for (const n of groups[g]) {
        if (hidden.indexOf(n) >= 0 && !openGroups.has(kind + ':' + g)) continue;
        box.appendChild(namedListItem(kind, n));
      }
      if (hidden.length) {
        const key = kind + ':' + g;
        const t = el('button', 'link fold-link', openGroups.has(key) ? 'Show fewer' : hidden.length + ' more (not in the lists of other mods)');
        t.addEventListener('click', () => { if (openGroups.has(key)) openGroups.delete(key); else openGroups.add(key); renderNamedList(); });
        box.appendChild(t);
      }
      list.appendChild(box);
    }
  }
  const openGroups = new Set();

  function namedListItem(kind, n) {
    const it = n.item || {};
    const item = el('div', 'dial');
    if (named && named.kind === kind && lc(named.name) === lc(n.name)) item.classList.add('selected');
    const top = el('div', 'top');
    top.appendChild(el('span', 'name', it.label || n.name));
    top.appendChild(el('span', 'val', n.isUser ? 'yours' : ''));
    item.appendChild(top);
    const feeds = it.provides ? '  ·  gives to ' + [].concat(it.provides).map((p) => '$' + p).join(', ') : '';
    item.appendChild(el('div', 'id', (kind === 'value' ? '$' : '@') + n.name + '  ·  ' + (n.isUser ? 'made by you' : 'from ' + (it.sourceLabel || it.source || '?')) + feeds));
    const expr = n.json ? (typeof n.json === 'string' ? n.json : n.json.expr) : it.expression;
    if (expr) item.appendChild(el('div', 'tagline', expr));
    item.addEventListener('click', () => openNamed(kind, n.name));
    return item;
  }

  function setTab(t) {
    if (t === tab) return;
    if (tab === 'dials' && !confirmLeave()) return;
    tab = t;
    dirty = false;
    for (const b of document.querySelectorAll('.tab')) b.classList.toggle('active', b.dataset.tab === t);
    $('new').textContent = t === 'dials' ? '+ New dial' : t === 'conditions' ? '+ New condition' : '+ New value';
    $('search').value = '';
    $('search').placeholder = t === 'dials' ? 'Find a dial' : t === 'conditions' ? 'Find a condition' : 'Find a value';
    named = null;
    renderList();
    if (t === 'dials') {
      const d = selectedDial();
      if (d) loadDial(d); else if (draft && !selectedKey) showEditor(); else renderEmpty();
    } else {
      renderEmpty();
    }
  }

  function namedJson(kind, n) {
    if (n.json) return typeof n.json === 'string' ? { expr: n.json } : clone(n.json);
    const it = n.item || {};
    const j = { expr: it.expression || '' };
    if (it.label) j.label = it.label;
    if (it.description) j.description = it.description;
    if (it.category) j.category = it.category;
    if (kind === 'value') {
      if (it.unit) j.unit = it.unit;
      if (Array.isArray(it.range)) j.range = it.range;
    }
    return j;
  }

  function openNamed(kind, name, keepMessages) {
    const n = namedItems(kind).find((x) => String(x.name).toLowerCase() === String(name).toLowerCase());
    if (!n) return;
    named = { kind: kind, name: n.name, previousName: n.name, isUser: n.isUser, isNew: false, item: n.item, json: namedJson(kind, n), nameTouched: true };
    renderNamed();
    if (!keepMessages) $('n-messages').innerHTML = '';
    renderNamedList();
  }

  function uniqueNamed(kind, base) {
    const taken = (x) => namedItems(kind).some((it) => lc(it.name) === lc(x)) && !(named && !named.isNew && lc(named.name) === lc(x));
    let name = base;
    for (let i = 2; taken(name); ++i) name = base + i;
    return name;
  }

  function newNamed(copyOf) {
    const kind = tab === 'values' ? 'value' : 'condition';
    const label = copyOf ? (copyOf.json.label || copyOf.name) + ' (mine)' : kind === 'value' ? 'My value' : 'My condition';
    named = { kind: kind, name: '', previousName: '', isUser: true, isNew: true, item: null, nameTouched: false,
      json: copyOf ? clone(copyOf.json) : { expr: kind === 'value' ? '$hunger * 0.5' : 'Combat', label: label } };
    named.json.label = label;
    named.name = uniqueNamed(kind, copyOf ? copyOf.name + '_mine' : slug(label, kind === 'condition'));
    renderNamed();
    $('n-messages').innerHTML = '';
    $('n-label').focus();
    $('n-label').select();
  }

  let namedPicker = null;
  function renderNamed() {
    if (!named) return;
    showPanel('named');
    const isValue = named.kind === 'value';
    const j = named.json;
    const editable = named.isUser;
    $('n-sigil').textContent = isValue ? '$' : '@';
    $('n-name').value = named.name;
    $('n-expr-label').textContent = isValue ? 'The value is' : 'True when';
    $('n-expr').value = j.expr || '';
    const pickerBox = $('n-picker');
    pickerBox.innerHTML = '';
    namedPicker = null;
    if (!isValue) {
      namedPicker = window.CEPicker.conditionPicker({
        value: j.expr || '', catalog: () => catalog, readOnly: !editable,
        empty: 'Add a condition.',
        onChange: (t) => { if (named && named.isUser) { named.json.expr = t.trim(); if (!named.json.expr) delete named.json.expr; renderNamedSnippets(); } }
      });
      pickerBox.appendChild(namedPicker.el);
    }
    const tools = $('n-value-tools');
    tools.innerHTML = '';
    if (isValue && editable) {
      tools.appendChild(window.CEPicker.insertValueButton($('n-expr'), () => catalog, () => readNamedForm()));
      const why = el('button', 'small', 'Why?');
      why.title = 'Show the value now, for the player, and how ConditionEngine calculates it.';
      why.addEventListener('click', () => $('n-explain').click());
      tools.appendChild(why);
      tools.appendChild(el('span', 'hint', 'Use + - * / and functions such as min, max, clamp and lerp.'));
    }
    $('n-knowledge-box').open = !!j.knowledge;
    $('n-label').value = j.label || '';
    $('n-category').value = j.category || '';
    $('n-description').value = j.description || '';
    $('n-knowledge').value = j.knowledge || '';
    $('n-unit').value = j.unit || '';
    $('n-min').value = Array.isArray(j.range) ? j.range[0] : '';
    $('n-max').value = Array.isArray(j.range) ? j.range[1] : '';
    $('n-provides').value = [].concat(j.provides || []).join(', ');
    $('n-priority').value = j.priority !== undefined ? j.priority : '';
    for (const e of document.querySelectorAll('.n-value-only')) e.classList.toggle('hidden', !isValue);
    for (const e of document.querySelectorAll('.n-condition-only')) e.classList.toggle('hidden', isValue);
    for (const id of ['n-name', 'n-expr', 'n-label', 'n-category', 'n-description', 'n-knowledge', 'n-unit', 'n-min', 'n-max', 'n-provides', 'n-priority']) {
      $(id).readOnly = !editable;
    }
    const it = named.item || {};
    $('n-origin').textContent = named.isNew ? 'New. Not saved.' : named.isUser ? 'Made by you' :
      'From ' + (it.sourceLabel || it.source || '?') + (it.file ? ' (' + it.file + ')' : '') + ' · You cannot change it';
    $('n-copy').classList.toggle('hidden', editable);
    $('n-delete').classList.toggle('hidden', !editable || named.isNew);
    $('n-save').classList.toggle('hidden', !editable);
    $('n-why').classList.add('hidden');
    renderNamedLinks();
    renderNamedSnippets();
  }

  // ── How a named item ties to the others ────────────────────────────────────
  // A combined value's sources (highest priority first), the value a source feeds, what an expression reads and
  // what reads this one (the levels: "@HungerMild  >= 25", "$hunger_stage"). Each opens on click.
  const lc = (s) => String(s || '').toLowerCase();
  function exprOfNamed(n) {
    const j = n.json;
    return j ? (typeof j === 'string' ? j : j.expr || '') : (n.item || {}).expression || '';
  }
  function refsIn(expr) {
    const out = { value: [], condition: [] };
    String(expr || '').replace(/([$@])([A-Za-z_][A-Za-z0-9_]*)/g, (m, sigil, name) => {
      const list = out[sigil === '$' ? 'value' : 'condition'];
      if (!list.some((x) => lc(x) === lc(name))) list.push(name);
      return m;
    });
    return out;
  }
  function mentions(expr, sigil, name) {
    return new RegExp('\\' + sigil + name.replace(/[^A-Za-z0-9_]/g, '') + '(?![A-Za-z0-9_])', 'i').test(String(expr || ''));
  }
  // "$hunger >= 25" -> ">= 25", the level a condition picks out of the value
  function thresholdOf(expr, name) {
    const m = new RegExp('\\$' + name + '\\s*(>=|<=|==|!=|>|<)\\s*(-?[0-9.]+)', 'i').exec(String(expr || ''));
    return m ? m[1] + ' ' + m[2] : '';
  }
  function renderNamedLinks() {
    const box = $('n-links');
    box.innerHTML = '';
    const it = named && named.item || {};
    if (!named || named.isNew || !catalog) { box.classList.add('hidden'); return; }
    const isValue = named.kind === 'value';
    const ref = (kind, name, extra, title) => {
      const b = el('button', 'ref', (kind === 'value' ? '$' : '@') + name);
      if (extra) b.appendChild(el('span', 'faint', extra));
      const found = namedItems(kind).find((x) => lc(x.name) === lc(name));
      if (found && found.item && found.item.offer === 'nowhere') b.title = (title ? title + '\n' : '') + 'Not in the lists of other mods';
      else if (title) b.title = title;
      b.addEventListener('click', () => {
        if (kind !== named.kind) setTab(kind === 'value' ? 'values' : 'conditions');
        openNamed(kind, name);
      });
      return b;
    };
    const row = (what, refs, note) => {
      if (!refs.length && !note) return;
      const r = el('div', 'link-row');
      r.appendChild(el('span', 'what', what));
      for (const x of refs) r.appendChild(x);
      if (note) r.appendChild(el('span', 'note', note));
      box.appendChild(r);
    };
    const values = (catalog.namedValues || []);
    if (isValue && Array.isArray(it.providers) && it.providers.length) {
      const ps = it.providers.slice().sort((a, b) => (b.priority || 0) - (a.priority || 0));
      row('Gets its value from', ps.map((p) => ref('value', p.name, 'priority ' + p.priority)),
        'For each actor, the first source that has a value gives the value. "Why?" shows which source.');
    }
    if (isValue && it.provides) {
      for (const target of [].concat(it.provides)) {
        const master = values.find((v) => lc(v.name) === lc(target));
        const ps = (master && master.providers || []).slice().sort((a, b) => (b.priority || 0) - (a.priority || 0));
        const rank = ps.findIndex((p) => lc(p.name) === lc(named.name));
        row('Gives its value to', [ref('value', target, rank >= 0 ? (rank + 1) + ' of ' + ps.length : '')],
          'Only when no source with a higher priority has a value for the actor.');
      }
    }
    const own = refsIn(exprOfNamed(named));
    row('Uses', own.value.filter((n) => lc(n) !== lc(named.name)).map((n) => ref('value', n))
      .concat(own.condition.filter((n) => lc(n) !== lc(named.name)).map((n) => ref('condition', n))));
    const sigil = isValue ? '$' : '@';
    const users = [];
    for (const kind of ['condition', 'value']) {
      for (const n of namedItems(kind)) {
        if (kind === named.kind && lc(n.name) === lc(named.name)) continue;
        const e = exprOfNamed(n);
        if (mentions(e, sigil, named.name)) users.push(ref(kind, n.name, isValue ? thresholdOf(e, named.name) : '', (n.item || {}).label));
      }
    }
    row('Used by', users);
    if (it.offer === 'nowhere') row('', [], 'This item is not in the lists of other mods, for example No Talk Just Hands. Expressions, prompts and Papyrus can use it.');
    else if (it.offer === 'owner') row('', [], 'Only the lists of ' + (it.sourceLabel || it.owner || it.source) + ' show this item.');
    box.classList.toggle('hidden', !box.childNodes.length);
  }

  function renderNamedSnippets() {
    const n = named.name;
    const rows = named.kind === 'value'
      ? [['Prompt: the value', '{{ default(condition_engine(npc.UUID).values.' + n + ', 0) }}'],
         ['Prompt: at a level or more', '{% if default(condition_engine(npc.UUID).values.' + n + ', 0) >= 50 %} … {% endif %}'],
         ['Expression', '$' + n + ' >= 50']]
      : [['Prompt: only when true', '{% if default(condition_engine(npc.UUID).conditions.' + n + ', false) %} … {% endif %}'],
         ['Prompt: only when false', '{% if not default(condition_engine(npc.UUID).conditions.' + n + ', false) %} … {% endif %}'],
         ['Expression', '@' + n],
         ['Papyrus', 'ConditionEngine.EvaluateNamed(akActor, "' + n + '")']];
    snippetRows($('n-snippets'), rows);
  }

  function readNamedForm() {
    const j = named.json;
    const put = (k, v) => { if (v === '' || v === undefined || (Array.isArray(v) && !v.length)) delete j[k]; else j[k] = v; };
    named.name = $('n-name').value.trim().replace(/^[@$]/, '');
    if (named.kind === 'value') put('expr', $('n-expr').value.trim());
    put('label', $('n-label').value.trim());
    put('category', $('n-category').value.trim());
    put('description', $('n-description').value.trim());
    if (named.kind === 'value') {
      put('unit', $('n-unit').value.trim());
      const min = $('n-min').value, max = $('n-max').value;
      put('range', min !== '' && max !== '' ? [Number(min), Number(max)] : undefined);
      put('provides', list($('n-provides').value));
      put('priority', $('n-priority').value === '' ? undefined : Number($('n-priority').value));
    } else {
      put('knowledge', $('n-knowledge').value.trim());
    }
    renderNamedSnippets();
  }

  for (const id of ['n-name', 'n-expr', 'n-label', 'n-category', 'n-description', 'n-knowledge', 'n-unit', 'n-min', 'n-max', 'n-provides', 'n-priority']) {
    $(id).addEventListener('input', () => {
      if (!named || !named.isUser) return;
      if (id === 'n-name') named.nameTouched = true;
      // The name of a new item follows its label until you change the name
      if (id === 'n-label' && named.isNew && !named.nameTouched) {
        $('n-name').value = uniqueNamed(named.kind, slug($('n-label').value, named.kind === 'condition') || (named.kind === 'value' ? 'my_value' : 'MyCondition'));
      }
      readNamedForm();
    });
  }
  $('n-save').addEventListener('click', () => {
    if (!named) return;
    readNamedForm();
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(named.name)) {
      $('n-messages').innerHTML = '';
      $('n-messages').appendChild(el('div', 'msg error', 'Use only letters, digits and _ in the name. Do not start the name with a digit.'));
      return;
    }
    request('saveNamed', { kind: named.kind, name: named.name, json: named.json, previousName: named.previousName }).then((res) => {
      const box = $('n-messages');
      box.innerHTML = '';
      for (const e of res.errors || []) box.appendChild(el('div', 'msg error', e));
      for (const w of res.warnings || []) box.appendChild(el('div', 'msg warn', w));
      if (res.ok) {
        box.appendChild(el('div', 'msg ok', 'Saved. ' + (named.kind === 'value' ? '$' : '@') + named.name + ' works now.'));
        named.isNew = false;
        named.previousName = named.name;
        send('catalog');
        refreshSkyrimNet();
      }
    });
  });
  $('n-delete').addEventListener('click', () => {
    if (!named || !window.confirm('Delete ' + (named.kind === 'value' ? '$' : '@') + named.name + '?')) return;
    send('deleteNamed', { kind: named.kind, name: named.name });
    named = null;
    renderEmpty();
    send('catalog');
  });
  $('n-copy').addEventListener('click', () => { if (named) newNamed({ name: named.name, json: named.json }); });
  $('n-explain').addEventListener('click', () => {
    if (!named) return;
    const isValue = named.kind === 'value';
    const expr = named.isNew || named.isUser ? (isValue ? $('n-expr').value : named.json.expr || 'false') : (isValue ? '$' : '@') + named.name;
    request('explain', { expr: expr, value: isValue }).then((res) => {
      $('n-why').textContent = res.text || (res.errors || []).join('\n') || 'ConditionEngine gave no answer.';
      $('n-why').classList.remove('hidden');
    });
  });
  for (const b of document.querySelectorAll('.tab')) b.addEventListener('click', () => setTab(b.dataset.tab));

  // ── SkyrimNet status ───────────────────────────────────────────────────────
  function refreshSkyrimNet() {
    request('skyrimnet', {}).then((res) => {
      snStatus = parse(res.text || '{}');
      const s = $('sn-status');
      if (!snStatus || !snStatus.available) {
        s.textContent = 'SkyrimNet is not installed. Prompts cannot read the dials.';
        s.title = ((snStatus && snStatus.problems) || []).join('\n');
        return;
      }
      const parts = ['SkyrimNet API ' + snStatus.version, (snStatus.decorators || []).join(' + ')];
      if ((snStatus.actions || []).length) parts.push(snStatus.actions.length + ' AI action' + (snStatus.actions.length === 1 ? '' : 's'));
      if (snStatus.knowledge) parts.push(snStatus.knowledge + ' NPC knowledge text' + (snStatus.knowledge === 1 ? '' : 's'));
      if ((snStatus.problems || []).length) parts.push(snStatus.problems.length + ' problem' + (snStatus.problems.length === 1 ? '' : 's'));
      s.textContent = parts.join(' · ');
      s.title = (snStatus.problems || []).join('\n');
      s.classList.toggle('bad', (snStatus.problems || []).length > 0);
    });
  }

  // ── The key that opens the editor ──────────────────────────────────────────
  const KEY_CHOICES = ['F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12', 'Insert', 'Delete', 'Home', 'End',
    'PageUp', 'PageDown', 'Num0', 'Num1', 'Num2', 'Num3', 'Num4', 'Num5', 'Num6', 'Num7', 'Num8', 'Num9', 'NumDivide',
    'NumMultiply', 'NumMinus', 'NumPlus', 'NumPeriod', 'Tilde', 'Backslash']
    .concat('ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('')).concat('0123456789'.split(''));
  // KeyboardEvent.code -> the names ConditionEngine's ParseKey knows
  const CODE_NAMES = {
    Escape: 'Esc', Backquote: 'Tilde', Minus: 'Minus', Equal: 'Equals', Backspace: 'Backspace', Tab: 'Tab',
    BracketLeft: 'LeftBracket', BracketRight: 'RightBracket', Enter: 'Enter', ControlLeft: 'LeftCtrl', Semicolon: 'Semicolon',
    Quote: 'Apostrophe', ShiftLeft: 'LeftShift', Backslash: 'Backslash', Comma: 'Comma', Period: 'Period', Slash: 'Slash',
    ShiftRight: 'RightShift', NumpadMultiply: 'NumMultiply', AltLeft: 'LeftAlt', Space: 'Space', CapsLock: 'CapsLock',
    NumLock: 'NumLock', ScrollLock: 'ScrollLock', NumpadSubtract: 'NumMinus', NumpadAdd: 'NumPlus', NumpadDecimal: 'NumPeriod',
    NumpadEnter: 'NumEnter', ControlRight: 'RightCtrl', NumpadDivide: 'NumDivide', AltRight: 'RightAlt', Home: 'Home',
    ArrowUp: 'Up', PageUp: 'PageUp', ArrowLeft: 'Left', ArrowRight: 'Right', End: 'End', ArrowDown: 'Down',
    PageDown: 'PageDown', Insert: 'Insert', Delete: 'Delete'
  };
  // keyCode fallback, for a host without KeyboardEvent.code
  const KEYCODE_NAMES = { 27: 'Esc', 192: 'Tilde', 9: 'Tab', 13: 'Enter', 32: 'Space', 45: 'Insert', 46: 'Delete', 36: 'Home',
    35: 'End', 33: 'PageUp', 34: 'PageDown', 37: 'Left', 38: 'Up', 39: 'Right', 40: 'Down', 106: 'NumMultiply', 107: 'NumPlus',
    109: 'NumMinus', 110: 'NumPeriod', 111: 'NumDivide', 220: 'Backslash' };

  function keyNameOf(e) {
    const code = e.code || '';
    if (CODE_NAMES[code]) return CODE_NAMES[code];
    let m = /^Key([A-Z])$/.exec(code);
    if (m) return m[1];
    m = /^Digit([0-9])$/.exec(code);
    if (m) return m[1];
    m = /^Numpad([0-9])$/.exec(code);
    if (m) return 'Num' + m[1];
    if (/^F([1-9]|1[0-2])$/.test(code)) return code;
    const k = e.keyCode || e.which || 0;
    if (KEYCODE_NAMES[k]) return KEYCODE_NAMES[k];
    if (k >= 112 && k <= 123) return 'F' + (k - 111);
    if (k >= 65 && k <= 90) return String.fromCharCode(k);
    if (k >= 48 && k <= 57) return String.fromCharCode(k);
    if (k >= 96 && k <= 105) return 'Num' + (k - 96);
    return '';
  }

  let keyPick = '';         // the key chosen in the panel, without modifiers ('' = none)
  const MOD_CODES = ['ShiftLeft', 'ShiftRight', 'ControlLeft', 'ControlRight', 'AltLeft', 'AltRight', 'MetaLeft', 'MetaRight'];

  // "Ctrl+Shift+F10" -> {key: 'F10', shift, ctrl, alt}
  function splitCombo(text) {
    const parts = String(text || '').split('+');
    const out = { key: '', shift: false, ctrl: false, alt: false };
    while (parts.length > 1) {
      const p = parts.shift().trim().toLowerCase();
      if (p === 'shift') out.shift = true;
      else if (p === 'ctrl' || p === 'control') out.ctrl = true;
      else if (p === 'alt') out.alt = true;
    }
    out.key = parts[0] ? parts[0].trim() : '';
    return out;
  }

  function comboText() {
    if (!keyPick) return '';
    return ($('key-ctrl').checked ? 'Ctrl+' : '') + ($('key-alt').checked ? 'Alt+' : '') +
      ($('key-shift').checked ? 'Shift+' : '') + keyPick;
  }

  function showCombo() {
    const text = comboText();
    $('key-capture').textContent = text ? text + '  (push a different key to change it)' : 'Push a key';
  }
  let capturing = false;

  function renderKeyRow() {
    const name = state.editorKey || '';
    $('key-name').textContent = name || 'no key';
    $('key-row').title = state.editorKeyVia === 'InputManager' ? 'Bound in InputManager' : '';
    $('key-row').classList.toggle('bad', !!state.editorKeyError);
  }

  function setKeyPick(text, keepMods) {
    const c = splitCombo(text);
    const name = c.key;
    if (!keepMods) {
      $('key-shift').checked = c.shift;
      $('key-ctrl').checked = c.ctrl;
      $('key-alt').checked = c.alt;
    }
    keyPick = name;
    const sel = $('key-select');
    if (name && !Array.prototype.some.call(sel.options, (o) => o.value === name)) {
      const o = el('option', '', name);
      o.value = name;
      sel.appendChild(o);
    }
    sel.value = name;
    showCombo();
  }

  function openKeyPanel() {
    const sel = $('key-select');
    if (!sel.options.length) {
      const none = el('option', '', 'No key (use the console command: ce ui)');
      none.value = '';
      sel.appendChild(none);
      for (const k of KEY_CHOICES) { const o = el('option', '', k); o.value = k; sel.appendChild(o); }
    }
    setKeyPick(state.editorKey || '');
    $('key-pause').checked = state.pauseGame !== false;
    const notes = [];
    if (state.editorKeyVia === 'InputManager') {
      notes.push('InputManager controls this key. You can also change the key, or set a gamepad button, in the InputManager menu.');
      if (state.editorKeyGamepad) notes.push('InputManager also has a gamepad button for the editor.');
    } else {
      notes.push('The key works outside menus. Esc closes the editor.');
    }
    if (state.editorKeyError) notes.push(state.editorKeyError);
    $('key-note').textContent = notes.join(' ');
    $('key-messages').innerHTML = '';
    capturing = false;
    $('key-panel').classList.remove('hidden');
  }

  function closeKeyPanel() {
    capturing = false;
    $('key-panel').classList.add('hidden');
  }

  $('key-change').addEventListener('click', openKeyPanel);
  $('key-cancel').addEventListener('click', closeKeyPanel);
  $('key-select').addEventListener('change', () => setKeyPick($('key-select').value, true));
  for (const id of ['key-shift', 'key-ctrl', 'key-alt']) $(id).addEventListener('change', showCombo);
  $('key-capture').addEventListener('click', () => {
    capturing = true;
    $('key-capture').textContent = 'Push a key. You can hold Shift, Ctrl or Alt. Push Esc to stop.';
  });
  document.addEventListener('keydown', (e) => {
    if (!capturing) return;
    e.preventDefault();
    e.stopPropagation();
    if (MOD_CODES.indexOf(e.code) >= 0 || ['Shift', 'Control', 'Alt', 'Meta'].indexOf(e.key) >= 0) return;  // wait for the key
    capturing = false;
    if (e.key === 'Escape' || e.code === 'Escape') { showCombo(); return; }
    const name = keyNameOf(e);
    if (!name) { $('key-capture').textContent = 'You cannot use that key. Select a key from the list.'; return; }
    setKeyPick((e.ctrlKey ? 'Ctrl+' : '') + (e.altKey ? 'Alt+' : '') + (e.shiftKey ? 'Shift+' : '') + name);
  }, true);
  $('key-save').addEventListener('click', () => {
    request('setEditorKey', { key: comboText(), pauseGame: $('key-pause').checked }).then((res) => {
      const box = $('key-messages');
      box.innerHTML = '';
      for (const e of res.errors || []) box.appendChild(el('div', 'msg error', e));
      for (const w of res.warnings || []) box.appendChild(el('div', 'msg warn', w));
      if (res.ok) closeKeyPanel();
    });
  });

  // ── Wiring ─────────────────────────────────────────────────────────────────
  bindBasics();
  renderTemplates();
  $('search').addEventListener('input', renderList);
  $('new').addEventListener('click', (e) => {
    e.stopPropagation();
    if (tab === 'dials') $('templates').classList.toggle('hidden');
    else newNamed();
  });
  document.addEventListener('click', (e) => { if (!e.target.closest('.new-wrap')) $('templates').classList.add('hidden'); });
  $('close').addEventListener('click', () => { if (confirmLeave()) send('close'); });
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && !$('key-panel').classList.contains('hidden')) { closeKeyPanel(); e.preventDefault(); return; }
    if (e.key === 'Escape' && !$('explain').classList.contains('hidden')) { $('explain').classList.add('hidden'); e.preventDefault(); }
    if (e.key === 's' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); $('save').click(); }
  });
  renderEmpty();
  send('ready');
  send('catalog');
  refreshSkyrimNet();

  // ── Browser mock ───────────────────────────────────────────────────────────
  function makeMock() {
    const subs = {};
    const emit = (ch, payload) => setTimeout(() => (subs[ch] || []).forEach((f) => f(JSON.stringify(payload))), 30);
    const sample = ['alarm', 'wetness', 'outfit_mode', 'kills_today'];
    const dials = {};
    const values = {};
    const mockDial = (id, json, origin, source) => {
      const kind = json.kind || (json.options ? 'choice' : 'number');
      const opts = {};
      if (Array.isArray(json.options)) json.options.forEach((l, i) => (opts[i] = l));
      else Object.assign(opts, json.options || {});
      const keys = Object.keys(opts).map(Number);
      const range = kind === 'toggle' ? [0, 1] : kind === 'choice' ? [Math.min.apply(null, keys), Math.max.apply(null, keys)] : (json.range || [0, 100]);
      if (values[id] === undefined) values[id] = typeof json.default === 'number' ? json.default : range[0];
      dials[id.toLowerCase()] = {
        id: id, origin: origin, source: source, json: json, errors: [], warnings: [], live: true, label: json.label || id,
        category: json.category || 'dials', kind: kind, scope: json.scope === 'actor' ? 'actor' : 'global',
        min: range[0], max: range[1], integer: !!json.integer || kind !== 'number', unit: json.unit || '', options: opts,
        value: values[id], actors: [], tracked: 2, enabled: json.enabled !== false
      };
    };
    for (const t of window.DIAL_TEMPLATES || []) {
      if (sample.indexOf(t.id) >= 0) mockDial(t.id, t.json, t.id === 'outfit_mode' ? 'user' : 'file', t.id === 'outfit_mode' ? 'User' : 'Examples');
    }
    const log = [];
    let editorKey = 'Shift+F10';
    let pauseGame = true;
    const userConditions = { Restless: { expr: '$alarm >= 3 && !Sitting', label: 'Restless', knowledge: 'You cannot sit still; you pace and fidget.' } };
    const userValues = { my_mood: { expr: '100 - $alarm * 10', label: 'Mood', unit: '%' } };
    const mockCatalog = () => ({
      categories: [
        { id: 'actor', label: 'Actor', icon: '🧍' }, { id: 'world', label: 'World', icon: '🌍' }, { id: 'weather', label: 'Weather', icon: '🌧' },
        { id: 'survival', label: 'Survival', icon: '🍖' }, { id: 'dials', label: 'Dials', icon: '◉' }, { id: 'mine', label: 'Yours' }
      ],
      types: [
        { name: 'Combat', label: 'In combat', category: 'actor', description: 'The actor is in combat.', fields: [] },
        { name: 'Sneaking', label: 'Sneaking', category: 'actor', fields: [] },
        { name: 'Sitting', label: 'Sitting', category: 'actor', fields: [] },
        { name: 'InWater', label: 'In water', category: 'actor', fields: [] },
        { name: 'IsPlayer', label: 'Is the player', category: 'actor', fields: [] },
        { name: 'IsFollower', label: 'Is a follower', category: 'actor', fields: [] },
        { name: 'WeaponDrawn', label: 'Weapon drawn', category: 'actor', fields: [] },
        { name: 'Time', label: 'Time of day', category: 'world', description: 'The game hour is between start and end.', positional: ['start', 'end'],
          fields: [{ key: 'start', label: 'From hour', type: 'float', required: true, default: 20 }, { key: 'end', label: 'To hour', type: 'float', required: true, default: 6 }] },
        { name: 'InteriorExterior', label: 'Inside or outside', category: 'world', positional: ['interior'],
          fields: [{ key: 'interior', label: 'Inside', type: 'bool', required: true, default: true }] },
        { name: 'WeatherKind', label: 'Weather strength', category: 'weather', value: { unit: '%', range: [0, 100] }, positional: ['kind'],
          fields: [{ key: 'kind', label: 'Kind', type: 'enum', required: true, options: ['Rain', 'Snow', 'Storm', 'Fog', 'Wind'].map((v) => ({ value: v, label: v })) }] },
        { name: 'ActorValue', label: 'Actor value', category: 'actor', value: {}, positional: ['actorValue'],
          fields: [{ key: 'actorValue', label: 'Actor value', type: 'enum', required: true, allowCustom: true, options: ['Health', 'Magicka', 'Stamina'].map((v) => ({ value: v, label: v })) },
            { key: 'usePercentage', label: 'As a percentage', type: 'bool', default: false }] },
        { name: 'Logical', label: 'And / or', hidden: true, offer: 'nowhere', fields: [] }
      ],
      namedConditions: [
        { id: 'OutsideInRain', label: 'Outside in the rain', expression: 'WeatherKind(Rain) >= 25 && !InteriorExterior(true)', source: 'ConditionEngine', category: 'weather' },
        { id: 'Alarmed', label: 'Alarmed', expression: '$alarm >= 5', source: 'Dials', category: 'dials' },
        { id: 'WarmthPoor', label: 'Poorly dressed for the cold', expression: '$warmth < 25', source: 'ConditionEngine', category: 'survival' },
        { id: 'HungerMild', label: 'Hunger: mild or worse', expression: '$hunger >= 25', source: 'ConditionEngine', category: 'survival', offer: 'nowhere' },
        { id: 'HungerModerate', label: 'Hunger: moderate or worse', expression: '$hunger >= 50', source: 'ConditionEngine', category: 'survival' },
        { id: 'HungerSevere', label: 'Hunger: severe', expression: '$hunger >= 75', source: 'ConditionEngine', category: 'survival', offer: 'nowhere' }
      ].concat(Object.keys(userConditions).map((k) => ({ id: k, label: userConditions[k].label || k, expression: userConditions[k].expr, source: 'User', category: 'mine' }))),
      namedValues: [
        { name: 'hunger', label: 'Hunger', expression: '', combined: true, source: 'ConditionEngine', category: 'survival', unit: '%',
          providers: [{ name: 'severactions_hunger', priority: 40 }, { name: 'sunhelm_hunger', priority: 80 }, { name: 'lastseed_hunger', priority: 70 }] },
        { name: 'sunhelm_hunger', label: 'Hunger (SunHelm)', expression: 'piecewise(GlobalVariable("0xEAAE|SunHelmSurvival.esp"), 0, 0, 80, 25, 140, 50, 240, 75, 360, 100)',
          source: 'SunHelm', sourceLabel: 'SunHelm', category: 'survival', unit: '%', offer: 'nowhere', provides: ['hunger'], priority: 80 },
        { name: 'lastseed_hunger', label: 'Hunger (Last Seed)', expression: 'piecewise(GlobalVariable("0x29CD|LastSeed.esp"), 0, 0, 40, 25, 60, 50, 80, 75, 120, 100)',
          source: 'LastSeed', sourceLabel: 'Last Seed', category: 'survival', unit: '%', offer: 'nowhere', provides: ['hunger'], priority: 70 },
        { name: 'severactions_hunger', label: 'Hunger (SeverActions)', expression: 'clamp(PapyrusCall(SeverActionsNative, Native_Survival_GetHunger), 0, 100)',
          source: 'SeverActions', sourceLabel: 'SeverActions', category: 'survival', unit: '%', offer: 'nowhere', provides: ['hunger'], priority: 40 },
        { name: 'hunger_stage', label: 'Hunger stage', expression: 'steps($hunger, 25, 50, 75)', source: 'ConditionEngine', category: 'survival', offer: 'nowhere' },
        { name: 'alarm', label: 'Alarm', pushed: true, source: 'Dials', category: 'dials' }
      ].concat(Object.keys(userValues).map((k) => ({ name: k, label: userValues[k].label || k, expression: userValues[k].expr, source: 'User', category: 'mine', unit: userValues[k].unit })))
    });
    const snapshot = () => ({
      ready: true, inGame: true, dials: Object.keys(dials).map((k) => Object.assign({}, dials[k], { value: values[dials[k].id] })),
      events: ['Load', 'Hit', 'CombatStart', 'CombatEnd', 'Death', 'Kill', 'Equip', 'Unequip', 'SpellCast', 'MagicEffect',
        'Activate', 'ItemAdded', 'ItemRemoved', 'LocationChange', 'SleepStart', 'SleepStop', 'MenuOpen', 'MenuClose',
        'Animation', 'SkillUp'].map((n) => ({ name: n, help: 'browser mock: ' + n })),
      fileErrors: [], editorKey: editorKey, editorKeyVia: 'ConditionEngine', editorKeyGamepad: false, editorKeyError: '',
      pauseGame: pauseGame, log: log, userConditions: userConditions, userValues: userValues
    });
    const liveNow = () => ({ values: Object.assign({}, values), actors: {}, log: log, inGame: true });
    setInterval(() => {
      values.alarm = Math.max(0, (values.alarm || 0) - 0.25);
      values.wetness = Math.min(100, (values.wetness || 0) + 0.4);
      emit('dials_live', liveNow());
    }, 500);
    const change = (id, to, cause) => {
      log.push({ t: performance.now() / 1000, id: id, actor: 'player', from: values[id], to: to, cause: cause });
      values[id] = to;
    };
    console.info('Dials editor: no Magelight host, using the browser mock');
    return {
      on(ch, fn) { (subs[ch] = subs[ch] || []).push(fn); return () => {}; },
      send(ch, payload) {
        const m = JSON.parse(payload);
        const reply = (extra) => emit('dials_reply', Object.assign({ req: m.req, op: m.op, ok: true, errors: [], warnings: [] }, extra || {}));
        switch (m.op) {
          case 'ready': case 'state': emit('dials_state', snapshot()); break;
          case 'catalog': emit('dials_catalog', mockCatalog()); break;
          case 'saveNamed': {
            const store = m.kind === 'value' ? userValues : userConditions;
            if (m.previousName && m.previousName !== m.name) delete store[m.previousName];
            store[m.name] = m.json;
            reply();
            emit('dials_state', snapshot());
            emit('dials_catalog', mockCatalog());
            break;
          }
          case 'deleteNamed': delete (m.kind === 'value' ? userValues : userConditions)[m.name]; reply(); emit('dials_state', snapshot()); emit('dials_catalog', mockCatalog()); break;
          case 'setEditorKey':
            if (m.key === 'G' || m.key === 'X') { reply({ ok: false, errors: ['G is not free: NoTalkJustHands holds it (browser mock)'] }); break; }
            editorKey = m.key;
            pauseGame = m.pauseGame;
            reply();
            emit('dials_state', snapshot());
            break;
          case 'skyrimnet': reply({ text: JSON.stringify({ available: true, version: 12, decorators: ['get_dials', 'condition_engine'], actions: ['calmdown'], events: ['dialogue'], knowledge: 2, problems: [], dials: { alarm: values.alarm, wetness: values.wetness } }) }); break;
          case 'validate': reply(m.json.rules && m.json.rules.some((r) => r.rate !== undefined && r.while === undefined) ? { ok: false, errors: ['rate runs over time, so it needs "while"'] } : {}); break;
          case 'save': {
            const old = dials[m.id.toLowerCase()];
            mockDial(m.id, m.json, old && old.origin !== 'user' ? 'override' : 'user', old ? old.source : 'User');
            if (m.previousId && m.previousId.toLowerCase() !== m.id.toLowerCase()) delete dials[m.previousId.toLowerCase()];
            reply();
            emit('dials_state', snapshot());
            break;
          }
          case 'revert': delete dials[m.id.toLowerCase()]; reply(); emit('dials_state', snapshot()); break;
          case 'set': change(m.id, m.value, 'editor'); reply(); break;
          case 'reset': change(m.id, 0, 'editor'); reply(); break;
          case 'toggle': change(m.id, values[m.id] >= 0.5 ? 0 : 1, 'editor'); reply(); break;
          case 'cycle': change(m.id, ((values[m.id] || 0) + 1) % 3, 'editor'); reply(); break;
          case 'fire': change(m.id, 10, 'rule ' + (m.rule + 1) + ' (test)'); reply(); break;
          case 'signal': reply(); break;
          case 'explain': reply({ text: '[T] ' + m.expr + '   (browser mock)' }); break;
          case 'close': console.info('close'); break;
          default: break;
        }
      }
    };
  }
})();
