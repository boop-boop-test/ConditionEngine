# SkyrimNet

With SkyrimNet installed (public API 5 or newer), ConditionEngine connects to it on its own. Prompts can then read
every dial, named condition and named value, and dials can react to SkyrimNet, tell it things, give NPCs knowledge
and offer them actions. Without SkyrimNet none of this runs and nothing else changes. `[SkyrimNet] bEnable = false`
in `ConditionEngine.ini` turns it off.

## In prompts: two decorators

SkyrimNet decorators receive only the actor, so both answer with an object that the template reads into.

**`get_dials(actorUUID)`**: every dial's value for that actor. Toggles read `true` / `false`, choice dials read
their option label, and numbers read as numbers.

```
{{ get_dials(npc.UUID).wetness }}
{% if get_dials(npc.UUID).manic %}{{ decnpc(npc.UUID).name }} is restless and talks too fast.{% endif %}
{% if get_dials(npc.UUID).outfit_mode == "Battle" %} … {% endif %}
{% for name, value in get_dials(npc.UUID) %}{{ name }}: {{ value }}
{% endfor %}
```

Every dial is always in the object. Global dials read the same for everyone. Player-only dials read their default
for anyone but the player. Actor dials read the actor's own value, or the default if the actor has none.

**`condition_engine(actorUUID)`**: `{"conditions": {Name: true/false}, "values": {name: number}, "ready": bool}`
for every named condition and value: your own, the ones dials name, the built-ins (`OutsideInRain`, `WarmthPoor`,
`$hunger`, `$cold` …) and any mod's.

```
{% if default(condition_engine(npc.UUID).conditions.Soaked, false) %}{{ decnpc(npc.UUID).name }} is soaked through.{% endif %}
Hunger: {{ default(condition_engine(npc.UUID).values.hunger, 0) }}
```

Wrap names in `default(...)`. A condition whose mod is missing is left out, and the first read for an actor may
come before their answer is ready.

The editor (`ce ui`) shows these lines for every dial, condition and value under **Use it**, ready to copy.

### How fresh

SkyrimNet calls decorators on its own threads, but game state can only be read on the main thread.
- `get_dials` reads stored dial values, so it is always current.
- `condition_engine` answers from a cache, refreshed every `[SkyrimNet] fRefreshSeconds` (1.5 s) for the actors
  SkyrimNet asked about in the last 30 seconds (up to 16). The first question about an actor waits up to 300 ms for
  its answer.

SkyrimNet's docs say a mod decorator only resolves for the speaker and the target. That note is written for
Papyrus decorators; whether it also holds for these C++ ones (inside a loop over nearby NPCs, say) needs an
in-game check.

## From dials to SkyrimNet

These keys go in dial rules and conditions (format: [dials.md](dials.md)).

**React to SkyrimNet events.** `{"skyrimnet": "dialogue", ...}` fires on a SkyrimNet event of that type. The
event's actor is whoever it comes from; `"actor": "other"` acts on whoever it was aimed at. The types include
`dialogue`, `dialogue_player_text`, `dialogue_player_stt`, `combat`, `hit`, `death`, `activation`, `spell_cast` and
`equip`. The rule's `text` filter compares the event's text.

```json
{"label": "Someone spoke to them", "skyrimnet": "dialogue", "actor": "other", "add": 4, "cooldown": 5}
```

**Tell SkyrimNet.** `"tell"` on a rule registers an event when the rule fires. By default it is a memory event
(`persistent_generic`) that the actor and the people nearby have in their history. With `"tellAs": "narration"` it
is narration they react to (`direct_narration`). `{name}`, `{value}` and `{label}` are filled in. SkyrimNet only
takes actors who are loaded.

```json
{"every": 1, "clock": "game", "chance": 0.05, "cooldown": 20, "set": 1, "for": 0.5,
 "tell": "{name} is suddenly gripped by a manic restlessness."}
```

**Knowledge while a condition is true.** A dial condition (or a condition you make in the editor) can carry
`"knowledge"`. It becomes SkyrimNet world knowledge, injected for every NPC while that condition is true for them,
with no prompt editing. Under the hood the entry's condition is
`default(condition_engine(actorUUID).conditions.Name, false)`.

```json
"conditions": {
  "Manic": {"expr": "== 1", "knowledge": "You are in a manic phase: restless, talking fast, jumping between ideas."}
}
```

The entries are named `ConditionEngine: <Name>` in SkyrimNet. They are replaced every session and after every
change, once SkyrimNet's memory is ready. It is not yet confirmed in game that SkyrimNet evaluates a mod decorator
inside world-knowledge conditions.

**AI actions.** `{"aiAction": "CalmDown", "description": "...", ...}` registers a SkyrimNet action. The AI can pick
it for an NPC who has the dial, wherever the rule's `if` holds and its cooldown has passed. Choosing it runs the
rule. Actions defined after start-up (in the editor, or by a late `Define`) are registered too, but SkyrimNet may
only offer them after a restart.

```json
{"aiAction": "CalmDown", "description": "Take a breath and calm down after a manic spell, when someone helps you settle",
 "if": "@Manic", "set": 0, "tell": "{name} calms down."}
```

## The whole manic example

```json
{
  "dials": {
    "manic": {
      "kind": "toggle", "label": "Manic", "scope": "actor", "track": "IsFollower && !IsPlayer",
      "rules": [
        {"label": "A spell starts (about once a day)", "every": 1, "clock": "game", "chance": 0.05, "cooldown": 20,
         "set": 1, "for": 0.5, "tell": "{name} is suddenly gripped by a manic restlessness."},
        {"label": "Being talked down", "aiAction": "CalmDown",
         "description": "Take a breath and calm down after a manic spell, when someone helps you settle",
         "if": "@Manic", "set": 0, "tell": "{name} calms down."}
      ],
      "conditions": {
        "Manic": {"expr": "== 1", "knowledge": "You are in a manic phase: restless, talking fast, jumping between ideas, hard to calm."}
      }
    }
  }
}
```

Every game hour each follower has a 5% chance (with a 20-hour cooldown) of a 30-minute manic spell. SkyrimNet gets
a memory of it, the NPC knows they are manic while it lasts, and anyone can try to talk them down. Prompts can
also check `{% if get_dials(npc.UUID).manic %}` themselves.

## Checking it

- `ce dials skyrimnet [dial]`: what `get_dials` answers for the selected actor.
- The editor's sidebar shows the SkyrimNet API version, what is registered, and any problems (hover for details).
- `ConditionEngine.log`: lines starting with `SkyrimNet bridge:`.
