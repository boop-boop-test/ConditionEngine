// Condition pickers for the editor: an expression as rows you pick from, not text you type.
//   "Combat && $hunger >= 50 || @OutsideInRain"  <->  [is Combat] AND [Hunger >= 50] OR [is Outside in the rain]
// Each row is one condition: a named condition (@Name), a value compared to a number ($hunger >= 50), or a condition
// type with its fields (Time(20, 6), WeatherKind(Rain) >= 25). AND binds tighter than OR, the same as the expression.
// An expression the rows cannot show (brackets, arithmetic) stays as text.
(function () {
  'use strict';

  const el = (tag, cls, text) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  };
  const lc = (s) => String(s || '').toLowerCase();
  const OPS = [['>=', 'at least'], ['>', 'more than'], ['<=', 'at most'], ['<', 'less than'], ['==', 'equal to'], ['!=', 'not equal to']];

  // ── Expression text <-> rows ─────────────────────────────────────────────
  // Splits at top-level && and ||. Returns [{join, text}] or null when brackets or quotes do not close.
  function splitTop(text) {
    const parts = [];
    let depth = 0, quote = false, start = 0, join = 'and';
    for (let i = 0; i < text.length; ++i) {
      const c = text[i];
      if (quote) { if (c === '\\') ++i; else if (c === '"') quote = false; continue; }
      if (c === '"') quote = true;
      else if (c === '(') ++depth;
      else if (c === ')') { if (--depth < 0) return null; }
      else if (depth === 0 && (text.startsWith('&&', i) || text.startsWith('||', i))) {
        parts.push({ join: join, text: text.slice(start, i).trim() });
        join = c === '&' ? 'and' : 'or';
        start = i + 2;
        ++i;
      }
    }
    if (depth !== 0 || quote) return null;
    parts.push({ join: join, text: text.slice(start).trim() });
    return parts;
  }

  // Splits "a, b, key=c" at top-level commas
  function splitArgs(text) {
    const out = [];
    let depth = 0, quote = false, start = 0;
    for (let i = 0; i < text.length; ++i) {
      const c = text[i];
      if (quote) { if (c === '\\') ++i; else if (c === '"') quote = false; continue; }
      if (c === '"') quote = true;
      else if (c === '(') ++depth;
      else if (c === ')') --depth;
      else if (c === ',' && depth === 0) { out.push(text.slice(start, i).trim()); start = i + 1; }
    }
    const last = text.slice(start).trim();
    if (last || out.length) out.push(last);
    return out;
  }
  const unquote = (s) => (/^".*"$/.test(s) ? s.slice(1, -1).replace(/\\"/g, '"') : s);
  const quoteArg = (v) => {
    const s = String(v);
    if (/^-?\d+(\.\d+)?$/.test(s) || /^[A-Za-z_][A-Za-z0-9_]*$/.test(s) || /^[$@][A-Za-z_]\w*$/.test(s)) return s;
    return '"' + s.replace(/"/g, '\\"') + '"';
  };

  // One condition: !?(@Name | $name OP rhs | Type(args)? (OP rhs)? | true | false). null: not a simple condition.
  function parseAtom(text) {
    let t = text.trim();
    let not = false;
    while (t.startsWith('!')) { not = !not; t = t.slice(1).trim(); }
    if (!t || t.startsWith('(')) return null;
    if (t === 'true' || t === 'false') return { not: not, kind: 'literal', name: t };
    const head = /^([@$]?)([A-Za-z_][A-Za-z0-9_]*)/.exec(t);
    if (!head) return null;
    let rest = t.slice(head[0].length).trim();
    let args = null;
    if (head[1] === '' && rest.startsWith('(')) {
      let depth = 0, quote = false, end = -1;
      for (let i = 0; i < rest.length; ++i) {
        const c = rest[i];
        if (quote) { if (c === '\\') ++i; else if (c === '"') quote = false; continue; }
        if (c === '"') quote = true;
        else if (c === '(') ++depth;
        else if (c === ')' && --depth === 0) { end = i; break; }
      }
      if (end < 0) return null;
      args = rest.slice(1, end);
      rest = rest.slice(end + 1).trim();
    }
    let op = null, rhs = null;
    if (rest) {
      const m = /^(>=|<=|==|!=|>|<)\s*(.+)$/.exec(rest);
      if (!m || /&&|\|\|/.test(m[2])) return null;
      op = m[1];
      rhs = m[2].trim();
    }
    const kind = head[1] === '@' ? 'named' : head[1] === '$' ? 'value' : 'type';
    if (kind === 'named' && op) return null;
    return { not: not, kind: kind, name: head[2], args: args, op: op, rhs: rhs };
  }

  function parse(text) {
    const t = String(text === true ? '' : text || '').trim();
    if (!t) return [];
    const parts = splitTop(t);
    if (!parts) return null;
    const rows = [];
    for (const p of parts) {
      const atom = parseAtom(p.text);
      if (!atom) return null;
      atom.join = p.join;
      rows.push(atom);
    }
    return rows;
  }

  function atomText(r) {
    let s;
    if (r.kind === 'named') s = '@' + r.name;
    else if (r.kind === 'value') s = '$' + r.name;
    else if (r.kind === 'literal') s = r.name;
    else s = r.name + (r.args !== null && r.args !== undefined && r.args !== '' ? '(' + r.args + ')' : '');
    if (r.op) s += ' ' + r.op + ' ' + (r.rhs === null || r.rhs === '' ? '0' : r.rhs);
    return (r.not ? '!' : '') + s;
  }

  function print(rows) {
    return rows.map((r, i) => (i === 0 ? '' : r.join === 'or' ? ' || ' : ' && ') + atomText(r)).join('');
  }

  // ── The catalog as things to pick ────────────────────────────────────────
  function categoryLabels(catalog) {
    const out = {};
    for (const c of (catalog && catalog.categories) || []) out[c.id] = (c.icon ? c.icon + '  ' : '') + (c.label || c.id);
    return out;
  }
  function typeOf(catalog, name) {
    return ((catalog && catalog.types) || []).find((t) => lc(t.name) === lc(name) || (t.aliases || []).some((a) => lc(a) === lc(name))) || null;
  }
  function valueOf(catalog, name) {
    return ((catalog && catalog.namedValues) || []).find((v) => lc(v.name) === lc(name)) || null;
  }
  function namedOf(catalog, name) {
    return ((catalog && catalog.namedConditions) || []).find((c) => lc(c.id) === lc(name)) || null;
  }
  const isValueType = (t) => !!(t && (t.value || (t.tags || []).indexOf('value') >= 0));
  const offered = (it) => it.offer !== 'nowhere' && !it.hidden;

  // Entries for the list. kind: 'condition' (all) or 'value' (numbers only).
  function entries(catalog, kind) {
    const cats = categoryLabels(catalog);
    const group = (id, fallback) => cats[id] || id || fallback;
    const out = [];
    if (kind !== 'value') {
      for (const c of (catalog && catalog.namedConditions) || []) {
        out.push({ kind: 'named', name: c.id, label: c.label || c.id, code: '@' + c.id, group: group(c.category, 'Named conditions'),
          note: c.description || c.expression, offered: offered(c) });
      }
    }
    for (const v of (catalog && catalog.namedValues) || []) {
      out.push({ kind: 'value', name: v.name, label: v.label || v.name, code: '$' + v.name, group: group(v.category, 'Values'),
        note: v.description || '', offered: offered(v), number: true });
    }
    for (const t of (catalog && catalog.types) || []) {
      if (t.name === 'Logical' || t.name === 'Rule') continue;
      const number = isValueType(t);
      if (kind === 'value' && !number) continue;
      out.push({ kind: 'type', name: t.name, label: t.label || t.name, code: t.name, group: group(t.category, 'Other'),
        note: t.description || '', offered: offered(t), number: number });
    }
    return out;
  }

  // ── The list you pick from ───────────────────────────────────────────────
  let openPop = null;
  function closeList() {
    if (openPop) { openPop.remove(); openPop = null; }
  }
  document.addEventListener('mousedown', (e) => { if (openPop && !openPop.contains(e.target) && !e.target.closest('.pk-thing')) closeList(); });
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && openPop) { closeList(); e.stopPropagation(); } }, true);

  // Opens a searchable list below `anchor`. onPick(entry).
  function openList(anchor, list, onPick) {
    closeList();
    const pop = el('div', 'pk-pop');
    const search = el('input', 'pk-search');
    search.placeholder = 'Find…';
    const all = el('label', 'pk-all');
    const allBox = el('input');
    allBox.type = 'checkbox';
    all.append(allBox, document.createTextNode('Show all'));
    all.title = 'Also show the items that mods keep out of lists, for example each survival mod on its own.';
    const head = el('div', 'pk-head');
    head.append(search, all);
    const body = el('div', 'pk-body');
    pop.append(head, body);
    const render = () => {
      const q = lc(search.value.trim());
      body.innerHTML = '';
      const groups = {};
      for (const e of list) {
        if (!allBox.checked && !e.offered) continue;
        if (q && lc(e.label + ' ' + e.code + ' ' + e.group + ' ' + e.note).indexOf(q) < 0) continue;
        (groups[e.group] = groups[e.group] || []).push(e);
      }
      const names = Object.keys(groups).sort((a, b) => a.replace(/^\W+/, '').localeCompare(b.replace(/^\W+/, '')));
      if (!names.length) body.appendChild(el('div', 'pk-none', 'No item agrees with the search.'));
      for (const g of names) {
        body.appendChild(el('div', 'pk-group', g));
        groups[g].sort((a, b) => a.label.localeCompare(b.label));
        for (const e of groups[g]) {
          const row = el('div', 'pk-item');
          const top = el('div', 'pk-top');
          top.appendChild(el('span', 'pk-label', e.label));
          top.appendChild(el('span', 'pk-code', e.code));
          row.appendChild(top);
          if (e.note) row.appendChild(el('div', 'pk-note', e.note));
          row.addEventListener('click', () => { closeList(); onPick(e); });
          body.appendChild(row);
        }
      }
    };
    search.addEventListener('input', render);
    allBox.addEventListener('change', render);
    render();
    document.body.appendChild(pop);
    const r = anchor.getBoundingClientRect();
    const width = Math.max(360, r.width);
    pop.style.width = width + 'px';
    pop.style.left = Math.max(8, Math.min(r.left, window.innerWidth - width - 8)) + 'px';
    const below = window.innerHeight - r.bottom;
    if (below < 320 && r.top > below) { pop.style.bottom = (window.innerHeight - r.top + 4) + 'px'; pop.style.maxHeight = (r.top - 12) + 'px'; }
    else { pop.style.top = (r.bottom + 4) + 'px'; pop.style.maxHeight = (below - 12) + 'px'; }
    openPop = pop;
    search.focus();
  }

  // ── Fields of a condition type ───────────────────────────────────────────
  // args text -> {key: text}. Positional args fill `positional` (or the field order) first.
  function readArgs(type, args) {
    const fields = (type && type.fields) || [];
    const order = (type && type.positional && type.positional.length ? type.positional : fields.map((f) => f.key));
    const out = {};
    let next = 0;
    for (const part of splitArgs(args || '')) {
      const m = /^([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)\s*([\s\S]*)$/.exec(part);
      if (m && fields.some((f) => lc(f.key) === lc(m[1]))) out[fields.find((f) => lc(f.key) === lc(m[1])).key] = unquote(m[2].trim());
      else if (next < order.length) out[order[next++]] = unquote(part);
      else return null;
    }
    return out;
  }
  function writeArgs(type, values) {
    const fields = (type && type.fields) || [];
    const order = (type && type.positional && type.positional.length ? type.positional : []);
    const parts = [];
    let positional = true;
    for (const key of order) {
      const v = values[key];
      if (v === undefined || v === '') { positional = false; continue; }
      if (positional) parts.push(quoteArg(v));
      else parts.push(key + '=' + quoteArg(v));
    }
    for (const f of fields) {
      if (order.indexOf(f.key) >= 0) continue;
      const v = values[f.key];
      if (v === undefined || v === '') continue;
      parts.push(f.key + '=' + quoteArg(v));
    }
    return parts.join(', ');
  }
  // The fields a row shows: required, positional, or with a value set. Not the compare field.
  function shownFields(type, values) {
    const fields = (type && type.fields) || [];
    return fields.filter((f) => f.key !== type.compareField && f.type !== 'comparison' && f.type !== 'conditions' &&
      (f.required || (type.positional || []).indexOf(f.key) >= 0 || (values[f.key] !== undefined && values[f.key] !== '')));
  }
  function defaultArgs(type) {
    const values = {};
    for (const f of shownFields(type, {})) {
      if (f.default !== undefined && f.default !== null && f.default !== '') values[f.key] = String(f.default);
      else if (f.required && f.options && f.options.length) values[f.key] = f.options[0].value;
    }
    return writeArgs(type, values);
  }
  function fieldControl(f, value, set) {
    let c;
    let dl = null;
    const opts = f.options || [];
    if (f.type === 'bool') {
      c = el('select');
      for (const [v, l] of [['true', 'yes'], ['false', 'no']]) { const o = el('option', '', l); o.value = v; c.appendChild(o); }
      c.value = value === undefined || value === '' ? String(f.default === undefined ? true : f.default) : value;
      c.addEventListener('change', () => set(c.value));
    } else if (opts.length && !f.allowCustom) {
      c = el('select');
      for (const o of opts) { const x = el('option', '', o.label || o.value); x.value = o.value; if (o.description) x.title = o.description; c.appendChild(x); }
      if (value !== undefined && value !== '' && !opts.some((o) => o.value === value)) { const x = el('option', '', value); x.value = value; c.appendChild(x); }
      c.value = value === undefined ? '' : value;
      c.addEventListener('change', () => set(c.value));
    } else {
      c = el('input');
      if (f.type === 'int' || f.type === 'float') { c.type = 'number'; c.step = f.type === 'int' ? '1' : 'any'; c.className = 'pk-num'; }
      c.value = value === undefined ? '' : value;
      c.placeholder = f.label || f.key;
      if (opts.length) {
        const id = 'pk-dl-' + Math.random().toString(36).slice(2);
        dl = el('datalist');
        dl.id = id;
        for (const o of opts) { const x = el('option'); x.value = o.value; dl.appendChild(x); }
        c.setAttribute('list', id);
      }
      c.addEventListener('input', () => set(c.value.trim()));
    }
    const wrap = el('label', 'pk-field');
    wrap.appendChild(el('span', '', f.label || f.key));
    wrap.appendChild(c);
    if (dl) wrap.appendChild(dl);
    if (f.description) wrap.title = f.description;
    return wrap;
  }

  // ── The picker ───────────────────────────────────────────────────────────
  // opts: {value, catalog(), onChange(text), empty: 'text when there is no condition', explain(text), compact}
  function conditionPicker(opts) {
    const root = el('div', 'pk');
    let text = String(opts.value === true ? '' : opts.value || '');
    let rows = parse(text);
    let textMode = rows === null;
    const catalog = () => (typeof opts.catalog === 'function' ? opts.catalog() : opts.catalog) || {};

    const emit = () => { text = print(rows); opts.onChange(text); };

    function thingLabel(r) {
      const cat = catalog();
      if (r.kind === 'named') { const c = namedOf(cat, r.name); return c ? c.label || c.id : '@' + r.name; }
      if (r.kind === 'value') { const v = valueOf(cat, r.name); return v ? v.label || v.name : '$' + r.name; }
      if (r.kind === 'literal') return r.name === 'true' ? 'Always' : 'Never';
      const t = typeOf(cat, r.name);
      return t ? t.label || t.name : r.name;
    }

    function pick(r, e) {
      const cat = catalog();
      r.kind = e.kind;
      r.name = e.name;
      r.args = null;
      r.op = null;
      r.rhs = null;
      if (e.kind === 'value' || (e.kind === 'type' && e.number)) {
        const v = e.kind === 'value' ? valueOf(cat, e.name) : null;
        const range = v && Array.isArray(v.range) ? v.range : null;
        r.op = '>=';
        r.rhs = range && range[1] !== null && range[1] !== undefined ? String(Math.round(((range[0] || 0) + range[1]) / 2)) : '50';
        r.not = false;
      }
      if (e.kind === 'type') {
        const t = typeOf(cat, e.name);
        const a = defaultArgs(t);
        r.args = a || null;
      }
    }

    function rowEl(r, i) {
      const row = el('div', 'pk-row');
      const cat = catalog();
      if (i > 0) {
        const j = el('button', 'pk-join' + (r.join === 'or' ? ' or' : ''), r.join === 'or' ? 'OR' : 'AND');
        j.title = 'Click to change AND / OR. AND applies before OR.';
        j.addEventListener('click', () => { r.join = r.join === 'or' ? 'and' : 'or'; emit(); render(); });
        row.appendChild(j);
      } else {
        row.appendChild(el('span', 'pk-join first', 'IF'));
      }
      const compares = !!r.op;
      if (!compares && r.kind !== 'literal') {
        const isSel = el('select', 'pk-is');
        for (const [v, l] of [['is', 'is'], ['not', 'is not']]) { const o = el('option', '', l); o.value = v; isSel.appendChild(o); }
        isSel.value = r.not ? 'not' : 'is';
        isSel.addEventListener('change', () => { r.not = isSel.value === 'not'; emit(); });
        row.appendChild(isSel);
      }
      const thing = el('button', 'pk-thing', thingLabel(r));
      thing.title = atomText(r);
      thing.addEventListener('click', () => openList(thing, entries(cat, 'condition'), (e) => { pick(r, e); emit(); render(); }));
      row.appendChild(thing);
      if (r.kind === 'type') {
        const t = typeOf(cat, r.name);
        const values = t ? readArgs(t, r.args) : null;
        if (t && values) {
          for (const f of shownFields(t, values)) {
            row.appendChild(fieldControl(f, values[f.key], (v) => {
              if (v === '') delete values[f.key]; else values[f.key] = v;
              r.args = writeArgs(t, values) || null;
              emit();
            }));
          }
        } else if (r.args !== null) {
          const a = el('input', 'pk-args');
          a.value = r.args;
          a.placeholder = 'fields';
          a.addEventListener('input', () => { r.args = a.value; emit(); });
          row.appendChild(a);
        }
      }
      if (compares) {
        const op = el('select', 'pk-op');
        for (const [v, l] of OPS) { const o = el('option', '', l); o.value = v; op.appendChild(o); }
        op.value = r.op;
        op.addEventListener('change', () => { r.op = op.value; emit(); });
        const rhs = el('input', 'pk-rhs');
        rhs.value = r.rhs;
        rhs.addEventListener('input', () => { r.rhs = rhs.value.trim(); emit(); });
        row.append(op, rhs);
        const v = r.kind === 'value' ? valueOf(cat, r.name) : null;
        if (v && v.unit) row.appendChild(el('span', 'pk-unit', v.unit));
      }
      const x = el('button', 'pk-x', '✕');
      x.title = 'Remove this condition';
      x.addEventListener('click', () => {
        rows.splice(i, 1);
        if (rows.length && i === 0) rows[0].join = 'and';
        emit();
        render();
      });
      row.appendChild(x);
      return row;
    }

    function render() {
      root.innerHTML = '';
      if (textMode) {
        const wrap = el('div', 'pk-text');
        const input = el('input', 'expr');
        input.value = text;
        input.placeholder = opts.empty || 'Combat && $hunger >= 50';
        input.setAttribute('list', 'names');
        input.addEventListener('input', () => { text = input.value; opts.onChange(text); });
        wrap.appendChild(input);
        if (opts.explain) {
          const why = el('button', 'why', '?');
          why.title = 'Show why this is true or false now, for the player.';
          why.addEventListener('click', () => opts.explain(input.value));
          wrap.appendChild(why);
        }
        const back = el('button', 'small', 'Use lists');
        back.title = 'Edit this with lists again.';
        back.addEventListener('click', () => {
          const parsed = parse(input.value);
          if (parsed === null) { note.textContent = 'The lists cannot show this expression. Remove the brackets or the arithmetic first.'; return; }
          rows = parsed;
          textMode = false;
          render();
        });
        wrap.appendChild(back);
        root.appendChild(wrap);
        const note = el('div', 'pk-note-line');
        root.appendChild(note);
        return;
      }
      if (!rows.length && opts.empty) root.appendChild(el('div', 'pk-empty', opts.empty));
      rows.forEach((r, i) => root.appendChild(rowEl(r, i)));
      const foot = el('div', 'pk-foot');
      const add = el('button', 'small', '+ Condition');
      add.addEventListener('click', () => {
        openList(add, entries(catalog(), 'condition'), (e) => {
          const r = { join: 'and', not: false };
          pick(r, e);
          rows.push(r);
          emit();
          render();
        });
      });
      foot.appendChild(add);
      if (opts.explain && rows.length) {
        const why = el('button', 'small', 'Why?');
        why.title = 'Show why this is true or false now, for the player.';
        why.addEventListener('click', () => opts.explain(text));
        foot.appendChild(why);
      }
      const asText = el('button', 'small pk-astext', 'Edit as text');
      asText.addEventListener('click', () => { textMode = true; render(); });
      foot.appendChild(asText);
      root.appendChild(foot);
    }

    // Read only: show the rows, change nothing
    function lock() {
      if (!opts.readOnly) return;
      root.classList.add('pk-locked');
      for (const c of root.querySelectorAll('input, select, button')) {
        if (c.classList.contains('pk-x') || c.closest('.pk-foot')) c.remove();
        else if (c.classList.contains('pk-astext')) c.remove();
        else c.disabled = true;
      }
    }
    const baseRender = render;
    render = function () { baseRender(); lock(); };

    render();
    return {
      el: root,
      set(value) { text = String(value === true ? '' : value || ''); rows = parse(text); textMode = rows === null; render(); },
      refresh() { if (!openPop) render(); }
    };
  }

  // A button that opens the list of values and puts the picked one into `input` at the cursor.
  function insertValueButton(input, catalog, onInsert) {
    const b = el('button', 'small', '+ Value');
    b.title = 'Put a value into the expression.';
    b.addEventListener('click', () => {
      const cat = (typeof catalog === 'function' ? catalog() : catalog) || {};
      openList(b, entries(cat, 'value'), (e) => {
        let code = e.code;
        if (e.kind === 'type') { const a = defaultArgs(typeOf(cat, e.name)); if (a) code += '(' + a + ')'; }
        const s = input.selectionStart === undefined || input.selectionStart === null ? input.value.length : input.selectionStart;
        const end = input.selectionEnd === undefined || input.selectionEnd === null ? s : input.selectionEnd;
        if (s > 0 && /[^\s(+\-*/,]/.test(input.value[s - 1])) code = ' ' + code;
        input.value = input.value.slice(0, s) + code + input.value.slice(end);
        input.focus();
        input.selectionStart = input.selectionEnd = s + code.length;
        if (onInsert) onInsert(input.value);
      });
    });
    return b;
  }

  window.CEPicker = { parse: parse, print: print, conditionPicker: conditionPicker, insertValueButton: insertValueButton, openList: openList, entries: entries };
})();
