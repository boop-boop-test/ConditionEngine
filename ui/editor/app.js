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
  let validateTimer = 0;
  let tab = 'dials';         // dials | conditions | values
  let catalog = null;        // ConditionEngine's catalog (named conditions and values)
  let named = null;          // the condition or value shown: {kind, name, previousName, isUser, isNew, item, json}
  let snStatus = null;       // SkyrimNet bridge status
  const SKYRIMNET_EVENTS = ['dialogue', 'dialogue_player_text', 'dialogue_player_stt', 'combat', 'hit', 'death',
    'activation', 'spell_cast', 'equip'];

  const GAME_EVENTS = () => state.events && state.events.length ? state.events : [{ name: 'Hit', help: '' }];
  const TRIGGERS = [
    ['on', 'Game event'], ['event', 'Mod event'], ['when', 'Becomes true'], ['while', 'As long as'],
    ['every', 'Every'], ['key', 'Key pressed'], ['skyrimnet', 'SkyrimNet event'], ['aiAction', 'AI action']
  ];
  const ACTIONS = [
    ['set', 'Set to'], ['add', 'Add'], ['multiply', 'Multiply by'], ['toggle', 'Toggle'], ['reset', 'Reset'],
    ['cycle', 'Next option'], ['rate', 'Change per'], ['approach', 'Move toward']
  ];
  const CONTINUOUS = ['rate', 'approach', 'set'];
  const DISCRETE = ['set', 'add', 'multiply', 'toggle', 'reset', 'cycle'];

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
      list.appendChild(el('div', 'hint', state.dials.length ? 'Nothing matches.' : 'No dials yet. Make one with + New dial.'));
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
      case 'user': return 'made by you';
      case 'override': return 'your edit of ' + d.source;
      case 'file': return 'from ' + d.source + '.json';
      default: return 'from ' + d.source;
    }
  }

  function renderStatus() {
    const s = $('status');
    s.classList.toggle('bad', !state.ready);
    if (!state.ready) { s.textContent = 'Not running: ' + (state.why || 'ConditionEngine is missing'); return; }
    const n = state.dials.filter((d) => d.live).length;
    const errs = (state.fileErrors || []).length;
    s.textContent = n + ' live dial' + (n === 1 ? '' : 's') + (state.inGame === false ? ' · no game loaded' : '') +
      (errs ? ' · ' + errs + ' file problem' + (errs === 1 ? '' : 's') : '') +
      (state.editorKey ? ' · ' + state.editorKey + ' opens this' : '');
    s.title = (state.fileErrors || []).join('\n');
  }

  const EMPTY = {
    dials: ['Make a value that changes on its own',
      'A dial is a number. Rules move it: an event switches it on, a condition sets it, a rate wears it down every second. ' +
      'Every dial is also a ConditionEngine value ($name), and SkyrimNet prompts read them all with get_dials(npc.UUID).'],
    conditions: ['Named conditions',
      'A named condition is an expression with a name: @Name. Mods, dials and you can make them; anything that reads ' +
      'ConditionEngine (NTJH, OAR, scripts) can use them, and SkyrimNet prompts read them with condition_engine(npc.UUID). ' +
      'Give one some knowledge text and NPCs know it while it is true for them.'],
    values: ['Named values',
      'A named value is a number with a name: $name. Some combine several mods (one $hunger, whichever survival mod you use); ' +
      'dials are values too. Make your own from an expression, e.g. $hunger * 0.5 + $fatigue * 0.5.']
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
    return !dirty || window.confirm('Discard the changes to ' + (draft && draft.label || draftId) + '?');
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
    dirty = false;
    showEditor();
    if (!refresh || (d.errors || []).length) showMessages(d.errors || [], d.warnings || []);
  }

  function uniqueId(base) {
    let id = (base || 'dial').replace(/[^A-Za-z0-9_]/g, '_').replace(/^(\d)/, '_$1');
    if (!findDial(id.toLowerCase())) return id;
    for (let i = 2; ; ++i) if (!findDial((id + '_' + i).toLowerCase())) return id + '_' + i;
  }

  function startNew(json, id) {
    if (!confirmLeave()) return;
    selectedKey = null;
    draft = clone(json);
    draftId = uniqueId(id);
    previousId = '';
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
    $('f-kind').value = kind;
    const range = Array.isArray(j.range) ? j.range : [j.min !== undefined ? j.min : 0, j.max !== undefined ? j.max : 100];
    $('f-min').value = range[0];
    $('f-max').value = range[1];
    $('f-default').value = j.default !== undefined ? j.default : '';
    $('f-unit').value = j.unit || '';
    $('f-integer').checked = !!j.integer;
    $('f-description').value = j.description || '';
    $('f-category').value = j.category || '';
    $('f-icon').value = j.icon || '';
    const scope = (j.scope || 'global').toLowerCase();
    // An actor dial with no track is the player's alone
    const track = (j.track || j.actors || '').trim();
    $('f-scope').value = scope === 'global' ? 'global' : scope === 'player' || !track || track === 'IsPlayer' ? 'player' : 'actor';
    $('f-track').value = j.track || j.actors || '';
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
    $('mode').textContent = jsonMode ? 'Form' : '{ } JSON';
  }

  function renderOrigin() {
    const d = selectedDial();
    const badge = $('origin');
    const revert = $('revert');
    const save = $('save');
    if (!d) {
      badge.textContent = 'new, not saved';
      revert.classList.add('hidden');
      save.textContent = 'Create';
    } else {
      badge.textContent = originText(d) + (d.live ? '' : ' · off');
      revert.classList.toggle('hidden', d.origin !== 'user' && d.origin !== 'override');
      revert.textContent = d.origin === 'override' ? 'Revert to ' + d.source : 'Delete';
      save.textContent = d.origin === 'file' || d.origin === 'mod' ? 'Save my version' : 'Save';
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
    on('f-label', (e) => setKey('label', e.value));
    $('f-id').addEventListener('input', () => {
      draftId = $('f-id').value.trim();
      $('f-id').classList.toggle('bad', !/^[A-Za-z_][A-Za-z0-9_]*$/.test(draftId));
      changed();
    });
    on('f-kind', (e) => {
      const kind = e.value;
      delete draft.kind; delete draft.options; delete draft.range; delete draft.min; delete draft.max; delete draft.integer;
      if (kind === 'toggle') draft.kind = 'toggle';
      if (kind === 'choice') { draft.kind = 'choice'; draft.options = ['Off', 'On']; }
      if (kind === 'number') draft.range = [0, 100];
      delete draft.default;
      renderEditor();
    }, 'change');
    const range = () => {
      const min = Number($('f-min').value), max = Number($('f-max').value);
      draft.range = [isFinite(min) ? min : 0, isFinite(max) ? max : 100];
      delete draft.min; delete draft.max;
    };
    on('f-min', range);
    on('f-max', range);
    on('f-default', (e) => setKey('default', numberOrText(e.value)));
    on('f-unit', (e) => setKey('unit', e.value.trim()));
    on('f-integer', (e) => setKey('integer', e.checked || undefined), 'change');
    on('f-description', (e) => setKey('description', e.value));
    on('f-category', (e) => setKey('category', e.value.trim()));
    on('f-icon', (e) => setKey('icon', e.value.trim()));
    on('f-scope', (e) => {
      setKey('scope', e.value === 'global' ? undefined : e.value);
      delete draft.actors;
      if (e.value !== 'actor') delete draft.track;
      else if (!draft.track) { draft.track = 'IsPlayer || IsFollower'; $('f-track').value = draft.track; }
      for (const x of document.querySelectorAll('.when-actor')) x.classList.toggle('hidden', e.value !== 'actor');
    }, 'change');
    on('f-track', (e) => { delete draft.actors; setKey('track', e.value.trim()); });
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
        changed();
      };
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
    changed();
  });

  // conditions
  function renderConditions() {
    const box = $('conditions');
    box.innerHTML = '';
    const conds = draft.conditions || {};
    const names = Object.keys(conds);
    if (!names.length) box.appendChild(el('div', 'hint', 'None.'));
    for (const name of names) {
      const entry = conds[name];
      const wrap = el('div', 'cond-row');
      const row = el('div', 'row');
      const at = el('span', 'dollar', '@');
      const n = el('input', 'narrow');
      n.value = name;
      n.style.width = '200px';
      const e = el('input', 'grow expr');
      e.value = typeof entry === 'object' && entry ? entry.expr || '' : entry;
      e.setAttribute('list', 'names');
      const why = el('button', 'why', '?');
      const x = el('button', 'small', '✕');
      const k = el('input', 'knowledge');
      k.placeholder = 'What NPCs know while it is true (SkyrimNet), optional';
      k.value = typeof entry === 'object' && entry ? entry.knowledge || '' : '';
      const write = () => {
        const o = {};
        document.querySelectorAll('#conditions .cond-row').forEach((r) => {
          const ins = r.querySelectorAll('input');
          const key = ins[0].value.trim().replace(/^@/, '');
          if (!key) return;
          o[key] = ins[2].value.trim() ? { expr: ins[1].value, knowledge: ins[2].value.trim() } : ins[1].value;
        });
        draft.conditions = o;
        if (!Object.keys(o).length) delete draft.conditions;
        changed();
      };
      n.addEventListener('input', write);
      e.addEventListener('input', write);
      k.addEventListener('input', write);
      why.addEventListener('click', () => explain(conditionExpr(e.value), false));
      x.addEventListener('click', () => { wrap.remove(); write(); renderConditions(); });
      row.append(at, n, e, why, x);
      wrap.append(row, k);
      box.appendChild(wrap);
    }
  }
  function conditionExpr(text) {
    const t = text.trim();
    if (/^(>=|<=|==|!=|>|<|=)/.test(t)) return '$' + draftId + ' ' + t.replace(/^=(?!=)/, '==');
    for (const [v, label] of optionsOf(draft)) if (label.toLowerCase() === t.toLowerCase()) return '$' + draftId + ' == ' + v;
    return t;
  }
  $('add-condition').addEventListener('click', () => {
    draft.conditions = draft.conditions || {};
    let name = (draft.label || draftId).replace(/[^A-Za-z0-9_]/g, '') + 'High';
    while (draft.conditions[name] !== undefined) name += '2';
    draft.conditions[name] = '>= 50';
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
    why.title = 'Why is this true or false right now (for the player)?';
    why.addEventListener('click', () => explain(i.value, !!isValue));
    wrap.append(i, why);
    return { wrap: wrap, input: i };
  }

  function renderRules() {
    const box = $('rules');
    box.innerHTML = '';
    const rs = rules();
    if (!rs.length) box.appendChild(el('div', 'hint', 'No rules: the dial only changes when a script, a mod or you set it.'));
    rs.forEach((r, i) => box.appendChild(ruleCard(r, i)));
  }

  function ruleCard(r, index) {
    const card = el('div', 'rule' + (r.enabled === false ? ' disabled' : ''));
    const head = el('div', 'rule-head');
    head.appendChild(el('span', 'num', '#' + (index + 1)));
    const label = input(r.label, '', 'Rule name (optional)');
    label.addEventListener('input', () => { if (label.value) r.label = label.value; else delete r.label; changed(); });
    head.appendChild(label);
    const en = el('label', 'check');
    en.style.paddingTop = '0';
    const enBox = el('input');
    enBox.type = 'checkbox';
    enBox.checked = r.enabled !== false;
    enBox.addEventListener('change', () => { if (enBox.checked) delete r.enabled; else r.enabled = false; card.classList.toggle('disabled', !enBox.checked); changed(); });
    en.append(enBox, document.createTextNode('on'));
    head.appendChild(en);
    const test = el('button', 'small', 'Test');
    test.title = 'Run this rule once now, on the player (saved version)';
    test.addEventListener('click', () => {
      if (dirty) { showMessages([], ['Save first: Test runs the saved rules.']); return; }
      request('fire', { id: draftId, rule: index }).then((res) => showResult(res, 'Rule ' + (index + 1) + ' ran.'));
    });
    const up = el('button', 'small', '▲');
    const down = el('button', 'small', '▼');
    const del = el('button', 'small danger', '✕');
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
      const i = input(r.event, '', 'ModEventName');
      i.setAttribute('list', 'event-names');
      i.addEventListener('input', () => { r.event = i.value.trim(); changed(); });
      tf.appendChild(i);
      help = 'An SKSE mod event (SendModEvent), or Dials.Signal from a script. DialsEvent reads its number.';
    } else if (trig === 'when' || trig === 'while') {
      const val = r[trig] === true ? '' : r[trig];
      const x = exprInput(val, trig === 'while' ? 'empty = always' : 'Combat, @OutsideInRain, $hunger > 50 …');
      x.input.addEventListener('input', () => { r[trig] = trig === 'while' && !x.input.value.trim() ? true : x.input.value; changed(); });
      tf.appendChild(x.wrap);
      if (trig === 'when') {
        const edge = select2([['rising', 'becomes true'], ['falling', 'becomes false'], ['both', 'changes']], r.edge || 'rising');
        edge.style.width = '140px';
        edge.addEventListener('change', () => { if (edge.value === 'rising') delete r.edge; else r.edge = edge.value; changed(); });
        tf.appendChild(edge);
        help = 'Checked every tick; fires once each time it flips.';
      } else {
        help = 'Runs every tick while true.';
      }
    } else if (trig === 'every') {
      const i = input(r.every, '', '5');
      i.type = 'number';
      i.style.width = '100px';
      i.addEventListener('input', () => { r.every = Number(i.value) || 1; changed(); });
      tf.append(i, el('span', 'unit', r.clock === 'game' ? 'game hours' : 'seconds'));
    } else if (trig === 'skyrimnet') {
      const i = input(r.skyrimnet, '', 'dialogue');
      i.setAttribute('list', 'skyrimnet-events');
      i.addEventListener('input', () => { r.skyrimnet = i.value.trim(); changed(); });
      tf.appendChild(i);
      help = 'A SkyrimNet event about the actor (dialogue: someone spoke). actor: other acts on who it was aimed at.';
    } else if (trig === 'aiAction') {
      const i = input(r.aiAction, '', 'CalmDown');
      i.style.width = '170px';
      i.addEventListener('input', () => { r.aiAction = i.value.trim(); changed(); });
      const d = input(r.description, '', 'what the AI reads to decide: "Calm down after a manic spell"');
      d.addEventListener('input', () => { r.description = d.value; changed(); });
      tf.append(i, d);
      help = 'SkyrimNet offers this to the NPC as an action wherever "Only if" holds (and they have the dial); choosing it runs this rule.';
    } else if (trig === 'key') {
      const i = input(r.key, '', 'F7, K, Num5 …');
      i.style.width = '140px';
      i.addEventListener('input', () => { r.key = i.value.trim(); changed(); });
      tf.appendChild(i);
      help = 'A keyboard key, outside menus. actor: crosshair acts on whoever you look at.';
    }
    whenLine.appendChild(tf);
    card.appendChild(whenLine);
    trigSel.addEventListener('change', () => {
      for (const [k] of TRIGGERS) delete r[k];
      const t = trigSel.value;
      r[t] = t === 'on' ? 'Hit' : t === 'event' ? 'MyMod_Event' : t === 'while' ? true : t === 'every' ? 5 : t === 'key' ? 'F7' :
        t === 'skyrimnet' ? 'dialogue' : t === 'aiAction' ? 'MyAction' : 'Combat';
      if (t === 'aiAction' && !r.description) r.description = 'Describe when the NPC should do this';
      if (t !== 'aiAction') delete r.description;
      const a = actionOf(r);
      const allowed = t === 'while' ? CONTINUOUS : DISCRETE;
      if (allowed.indexOf(a) < 0) { delete r[a]; delete r.halfLife; r[allowed[0]] = a === 'rate' || a === 'approach' ? 1 : 1; }
      if (t === 'while') delete r.for;
      renderRules();
      changed();
    });

    // DO
    const act = actionOf(r);
    const doLine = el('div', 'rule-line');
    doLine.appendChild(el('span', 'word', 'Do'));
    const allowed = trig === 'while' ? CONTINUOUS : DISCRETE;
    const actSel = select2(ACTIONS.filter(([k]) => allowed.indexOf(k) >= 0).map(([k, l]) => [k, k === 'set' && trig === 'while' ? 'Hold at' : l]), act);
    doLine.appendChild(actSel);
    const af = el('div', 'fields');
    if (act !== 'toggle' && act !== 'reset') {
      const x = exprInput(r[act] === true ? '' : r[act], act === 'cycle' ? '1 (or -1)' : 'a number or $hunger / 10 …', true);
      x.input.addEventListener('input', () => { r[act] = numberOrText(x.input.value); if (r[act] === undefined) r[act] = act === 'cycle' ? 1 : 0; changed(); });
      af.appendChild(x.wrap);
      if (act === 'rate') af.appendChild(el('span', 'unit', r.clock === 'game' ? 'per game hour' : 'per second'));
      if (act === 'approach') {
        af.appendChild(el('span', 'unit', 'halfway in'));
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
      const keep = r[old];
      delete r[old];
      const a = actSel.value;
      if (a === 'toggle' || a === 'reset') r[a] = true;
      else r[a] = typeof keep === 'number' || typeof keep === 'string' ? keep : (a === 'cycle' ? 1 : a === 'multiply' ? 1 : 0);
      if (a === 'approach' && !r.halfLife) r.halfLife = 30;
      if (a !== 'approach') delete r.halfLife;
      renderRules();
      changed();
    });
    if (help) card.appendChild(el('div', 'help', help));

    // MORE
    const more = el('details', 'more');
    const extras = ['if', 'for', 'cooldown', 'chance', 'actor', 'clock', 'form', 'keyword', 'text', 'other', 'otherIf'];
    if (extras.some((k) => r[k] !== undefined)) more.open = true;
    more.appendChild(el('summary', '', 'More: only if, for how long, cooldown, chance, filters'));
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
    field('if', 'Only if', 'a condition on the actor', 'expr');
    if (trig !== 'while') field('for', 'Undo after', 'seconds', 'number');
    field('cooldown', 'Cooldown', 'seconds', 'number');
    field('chance', 'Chance (0–1)', '1', 'number');
    field('actor', 'Acts on', [['auto', 'the event\'s actor'], ['other', 'its other actor'], ['player', 'the player'], ['crosshair', 'the crosshair target']], 'select');
    field('clock', 'Clock', [['real', 'real seconds'], ['game', 'game hours']], 'select');
    if (trig !== 'while') {
      field('tell', 'Tell SkyrimNet', '{name} had a manic spell.', 'text');
      field('tellAs', 'As', [['memory', 'a memory'], ['narration', 'narration they react to']], 'select');
    }
    if (trig === 'on' || trig === 'event' || trig === 'skyrimnet') {
      field('form', 'Form is', '0x12EB7|Skyrim.esm', 'text');
      field('keyword', 'Has keyword', 'WeapTypeSword', 'text');
      field('text', trig === 'event' ? 'Text is' : 'Name is (menu, anim, skill)', '', 'text');
      field('other', 'Other actor', [['any', 'anyone'], ['player', 'the player'], ['!player', 'not the player']], 'select');
      field('otherIf', 'Other actor matches', 'a condition', 'expr');
    }
    more.appendChild(grid);
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
    $('live-who').textContent = d.scope === 'actor' ? 'the player · ' + (d.tracked || 0) + ' tracked nearby' : d.scope === 'player' ? 'the player only' : 'global';
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
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(draftId)) { showMessages(['The name must be letters, digits and _.'], []); return; }
    request('save', { id: draftId, json: draft, previousId: previousId }).then((res) => {
      showResult(res, 'Saved. $' + draftId + ' is live.');
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
    const what = d.origin === 'override' ? 'Throw away your edit and use ' + d.source + '\'s version?' : 'Delete ' + (d.label || d.id) + '?';
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
  });

  $('mode').addEventListener('click', () => {
    jsonMode = !jsonMode;
    renderEditor();
  });
  $('json').addEventListener('input', () => {
    try {
      draft = JSON.parse($('json').value);
      $('json').classList.remove('bad');
      dirty = true;
      renderOrigin();
      scheduleValidate();
    } catch (e) {
      $('json').classList.add('bad');
      showMessages(['Not valid JSON: ' + e.message], []);
    }
  });

  // ── Explain ────────────────────────────────────────────────────────────────
  function explain(expr, isValue) {
    if (!expr || !String(expr).trim()) return;
    request('explain', { expr: String(expr), value: !!isValue }).then((res) => {
      $('explain-text').textContent = expr + '\n\n' + (res.text || (res.errors || []).join('\n') || 'no answer');
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
    if (!entries.length) { box.appendChild(el('div', 'hint', 'Changes show up here as rules fire.')); return; }
    for (const e of entries.slice().reverse()) {
      const row = el('div', 'log-row');
      const what = el('div', 'what');
      what.appendChild(el('b', '', '$' + e.id));
      what.appendChild(el('span', '', fmt(e.from) + ' → ' + fmt(e.to)));
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
    button.textContent = ok ? 'Copied' : 'Ctrl+C';
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
    else rows.push(['Prompt: above a level', '{% if get_dials(npc.UUID).' + id + ' >= 50 %} … {% endif %}']);
    for (const name of Object.keys(j.conditions || {})) {
      rows.push(['Prompt: @' + name, '{% if default(condition_engine(npc.UUID).conditions.' + name + ', false) %} … {% endif %}']);
    }
    rows.push(['ConditionEngine', kind === 'toggle' ? '$' + id + ' == 1' : '$' + id + ' >= 50']);
    for (const name of Object.keys(j.conditions || {})) rows.push(['ConditionEngine: @' + name, '@' + name]);
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
    if (!catalog) { list.appendChild(el('div', 'hint', 'Reading ConditionEngine…')); return; }
    const groups = {};
    for (const n of namedItems(kind)) {
      const it = n.item || {};
      const hay = (n.name + ' ' + (it.label || '') + ' ' + (it.category || '') + ' ' + (it.source || '') + ' ' + (it.expression || '')).toLowerCase();
      if (q && hay.indexOf(q) < 0) continue;
      const g = n.isUser ? 'yours' : (it.category || 'other');
      (groups[g] = groups[g] || []).push(n);
    }
    const names = Object.keys(groups).sort((a, b) => (a === 'yours' ? -1 : b === 'yours' ? 1 : a.localeCompare(b)));
    if (!names.length) list.appendChild(el('div', 'hint', 'Nothing matches.'));
    for (const g of names) {
      const box = el('div', 'group');
      box.appendChild(el('div', 'group-title', g));
      groups[g].sort((a, b) => String(a.name).localeCompare(String(b.name)));
      for (const n of groups[g]) {
        const it = n.item || {};
        const item = el('div', 'dial');
        if (named && named.kind === kind && String(named.name).toLowerCase() === String(n.name).toLowerCase()) item.classList.add('selected');
        const top = el('div', 'top');
        top.appendChild(el('span', 'name', it.label || n.name));
        top.appendChild(el('span', 'val', n.isUser ? 'yours' : ''));
        item.appendChild(top);
        item.appendChild(el('div', 'id', (kind === 'value' ? '$' : '@') + n.name + '  ·  ' + (n.isUser ? 'made by you' : 'from ' + (it.sourceLabel || it.source || '?'))));
        const expr = n.json ? (typeof n.json === 'string' ? n.json : n.json.expr) : it.expression;
        if (expr) item.appendChild(el('div', 'tagline', expr));
        item.addEventListener('click', () => openNamed(kind, n.name));
        box.appendChild(item);
      }
      list.appendChild(box);
    }
  }

  function setTab(t) {
    if (t === tab) return;
    if (tab === 'dials' && !confirmLeave()) return;
    tab = t;
    dirty = false;
    for (const b of document.querySelectorAll('.tab')) b.classList.toggle('active', b.dataset.tab === t);
    $('new').textContent = t === 'dials' ? '+ New dial' : t === 'conditions' ? '+ New condition' : '+ New value';
    $('search').value = '';
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
    named = { kind: kind, name: n.name, previousName: n.name, isUser: n.isUser, isNew: false, item: n.item, json: namedJson(kind, n) };
    renderNamed();
    if (!keepMessages) $('n-messages').innerHTML = '';
    renderNamedList();
  }

  function newNamed(copyOf) {
    const kind = tab === 'values' ? 'value' : 'condition';
    let base = copyOf ? copyOf.name + '_mine' : kind === 'value' ? 'my_value' : 'MyCondition';
    const taken = (x) => namedItems(kind).some((it) => String(it.name).toLowerCase() === x.toLowerCase());
    let name = base;
    for (let i = 2; taken(name); ++i) name = base + i;
    named = { kind: kind, name: name, previousName: '', isUser: true, isNew: true, item: null,
      json: copyOf ? clone(copyOf.json) : { expr: kind === 'value' ? '$hunger * 0.5' : 'Combat && $hunger > 50', label: name } };
    renderNamed();
    $('n-messages').innerHTML = '';
    $('n-name').focus();
  }

  function renderNamed() {
    if (!named) return;
    showPanel('named');
    const isValue = named.kind === 'value';
    const j = named.json;
    $('n-sigil').textContent = isValue ? '$' : '@';
    $('n-name').value = named.name;
    $('n-expr-label').textContent = isValue ? 'Expression (a number)' : 'Expression (true or false)';
    $('n-expr').value = j.expr || '';
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
    const editable = named.isUser;
    for (const id of ['n-name', 'n-expr', 'n-label', 'n-category', 'n-description', 'n-knowledge', 'n-unit', 'n-min', 'n-max', 'n-provides', 'n-priority']) {
      $(id).readOnly = !editable;
    }
    const it = named.item || {};
    $('n-origin').textContent = named.isNew ? 'new, not saved' : named.isUser ? 'made by you' :
      'from ' + (it.sourceLabel || it.source || '?') + (it.file ? ' (' + it.file + ')' : '') + ' · read only';
    $('n-copy').classList.toggle('hidden', editable);
    $('n-delete').classList.toggle('hidden', !editable || named.isNew);
    $('n-save').classList.toggle('hidden', !editable);
    $('n-why').classList.add('hidden');
    renderNamedSnippets();
  }

  function renderNamedSnippets() {
    const n = named.name;
    const rows = named.kind === 'value'
      ? [['Prompt: the value', '{{ default(condition_engine(npc.UUID).values.' + n + ', 0) }}'],
         ['Prompt: above a level', '{% if default(condition_engine(npc.UUID).values.' + n + ', 0) >= 50 %} … {% endif %}'],
         ['ConditionEngine', '$' + n + ' >= 50']]
      : [['Prompt: only when true', '{% if default(condition_engine(npc.UUID).conditions.' + n + ', false) %} … {% endif %}'],
         ['Prompt: only when false', '{% if not default(condition_engine(npc.UUID).conditions.' + n + ', false) %} … {% endif %}'],
         ['ConditionEngine', '@' + n],
         ['Papyrus', 'ConditionEngine.EvaluateNamed(akActor, "' + n + '")']];
    snippetRows($('n-snippets'), rows);
  }

  function readNamedForm() {
    const j = named.json;
    const put = (k, v) => { if (v === '' || v === undefined || (Array.isArray(v) && !v.length)) delete j[k]; else j[k] = v; };
    named.name = $('n-name').value.trim().replace(/^[@$]/, '');
    put('expr', $('n-expr').value.trim());
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
    $(id).addEventListener('input', () => { if (named && named.isUser) readNamedForm(); });
  }
  $('n-save').addEventListener('click', () => {
    if (!named) return;
    readNamedForm();
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(named.name)) {
      $('n-messages').innerHTML = '';
      $('n-messages').appendChild(el('div', 'msg error', 'The name must be letters, digits and _.'));
      return;
    }
    request('saveNamed', { kind: named.kind, name: named.name, json: named.json, previousName: named.previousName }).then((res) => {
      const box = $('n-messages');
      box.innerHTML = '';
      for (const e of res.errors || []) box.appendChild(el('div', 'msg error', e));
      for (const w of res.warnings || []) box.appendChild(el('div', 'msg warn', w));
      if (res.ok) {
        box.appendChild(el('div', 'msg ok', 'Saved. ' + (named.kind === 'value' ? '$' : '@') + named.name + ' is live.'));
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
    const expr = named.isNew || named.isUser ? $('n-expr').value : (isValue ? '$' : '@') + named.name;
    request('explain', { expr: expr, value: isValue }).then((res) => {
      $('n-why').textContent = res.text || (res.errors || []).join('\n') || 'no answer';
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
        s.textContent = 'SkyrimNet: not installed (prompts cannot read dials)';
        s.title = ((snStatus && snStatus.problems) || []).join('\n');
        return;
      }
      const parts = ['SkyrimNet API ' + snStatus.version, (snStatus.decorators || []).join(' + ')];
      if ((snStatus.actions || []).length) parts.push(snStatus.actions.length + ' AI action' + (snStatus.actions.length === 1 ? '' : 's'));
      if (snStatus.knowledge) parts.push(snStatus.knowledge + ' knowledge');
      if ((snStatus.problems || []).length) parts.push(snStatus.problems.length + ' problem' + (snStatus.problems.length === 1 ? '' : 's'));
      s.textContent = parts.join(' · ');
      s.title = (snStatus.problems || []).join('\n');
      s.classList.toggle('bad', (snStatus.problems || []).length > 0);
    });
  }

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
    const userConditions = { Restless: { expr: '$alarm >= 3 && !Sitting', label: 'Restless', knowledge: 'You cannot sit still; you pace and fidget.' } };
    const userValues = { my_mood: { expr: '100 - $alarm * 10', label: 'Mood', unit: '%' } };
    const mockCatalog = () => ({
      types: [{ name: 'Combat' }, { name: 'Time' }, { name: 'WeatherKind' }, { name: 'Sitting' }],
      namedConditions: [
        { id: 'OutsideInRain', label: 'Outside in the rain', expression: 'WeatherKind(Rain) >= 25 && !InteriorExterior(true)', source: 'ConditionEngine', category: 'weather' },
        { id: 'Alarmed', label: 'Alarmed', expression: '$alarm >= 5', source: 'Dials', category: 'dials' },
        { id: 'WarmthPoor', label: 'Poorly dressed for the cold', expression: '$warmth < 25', source: 'ConditionEngine', category: 'survival' }
      ].concat(Object.keys(userConditions).map((k) => ({ id: k, label: userConditions[k].label || k, expression: userConditions[k].expr, source: 'User', category: 'mine' }))),
      namedValues: [
        { name: 'hunger', label: 'Hunger', expression: '', combined: true, source: 'ConditionEngine', category: 'survival', unit: '%' },
        { name: 'alarm', label: 'Alarm', pushed: true, source: 'Dials', category: 'dials' }
      ].concat(Object.keys(userValues).map((k) => ({ name: k, label: userValues[k].label || k, expression: userValues[k].expr, source: 'User', category: 'mine', unit: userValues[k].unit })))
    });
    const snapshot = () => ({
      ready: true, inGame: true, dials: Object.keys(dials).map((k) => Object.assign({}, dials[k], { value: values[dials[k].id] })),
      events: ['Load', 'Hit', 'CombatStart', 'CombatEnd', 'Death', 'Kill', 'Equip', 'Unequip', 'SpellCast', 'MagicEffect',
        'Activate', 'ItemAdded', 'ItemRemoved', 'LocationChange', 'SleepStart', 'SleepStop', 'MenuOpen', 'MenuClose',
        'Animation', 'SkillUp'].map((n) => ({ name: n, help: 'browser mock: ' + n })),
      fileErrors: [], editorKey: 'F10', log: log, userConditions: userConditions, userValues: userValues
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
