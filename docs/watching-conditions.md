# Watching conditions

A watch re-checks a condition for one actor when something it depends on changes, and tells you when the result
flips. It's cheaper than evaluating every frame and works from Papyrus too.

## How checking works

1. **Every second:** cheap snapshots of the cell, location, game minute, weather and actor flags. Only watches that
   depend on what changed are re-checked. Game events do the same: equip, inventory, magic effects, combat, cell
   load, sleep and wait, menus, game load.
2. **A full poll every 10 seconds** for state with no change signal (`Game(...)`, script properties). Change it in
   `ConditionEngine.ini` (`[Watcher] fPollIntervalSeconds`), globally with `ConditionEngine_SetPollInterval`, or per
   watch.
3. **Your own signals:** `ConditionEngine_NotifyEvent(bits, actor)` re-checks watches that depend on those bits.

## From C or C++

```cpp
char err[256];
CE_Handle guarded = ConditionEngine_Compile("Combat || WeaponDrawn", err, sizeof err);

CE_WatchOptions options{sizeof options};
options.fireImmediately = 1;                  // also deliver the current result
options.pollIntervalMs = CE_POLL_NEVER;       // events only; CE_POLL_DEFAULT or milliseconds otherwise
options.extraDependencies = CE_DEP_LOCATION;  // also re-check on every cell transition

CE_WatchHandle watch = ConditionEngine_Watch(guarded, actor,
    [](CE_WatchHandle, void* actor, int32_t result, void* userData) {
        // Main thread, only when the result changes.
    },
    nullptr, &options);

ConditionEngine_Release(guarded);   // the watch keeps what it needs
// ...
ConditionEngine_Unwatch(watch);
```

- Null options use the defaults: no immediate result, the global poll interval, no extra triggers.
- `ConditionEngine_Refresh(watch)` re-checks now (main thread); the callback still fires only on a change.
- Watches survive game loads, except watches on created actors (`0xFF...` FormIDs): that FormID can name a different
  actor in another save, so those stop at the next load. Watch them again after loading.

## List watches: the first rule that matches

When you have an ordered list of conditions and want the first true one (outfit rules, animation sets, dialogue
moods), use one list watch instead of many watches:

```cpp
CE_Handle rules[] = {
    ConditionEngine_Compile("Combat", err, sizeof err),
    ConditionEngine_Compile("@MyMod.BadWeather && !InteriorExterior(true)", err, sizeof err),
    ConditionEngine_Compile("true", err, sizeof err),
};

CE_WatchHandle list = ConditionEngine_WatchList(rules, 3, actor, nullptr, nullptr, nullptr);
for (CE_Handle h : rules) ConditionEngine_Release(h);   // the watch copies them

// Any thread, never blocks: the winner's index, -1 when none matches, -2 for an unknown watch.
const int32_t winner = ConditionEngine_ReadListWatch(list);
```

Pass a `CE_ListWatchFn` to be called on the main thread when the winner changes. After a change only the rules that
can move the result are evaluated. A pending rule above the winner holds it, so the result doesn't flip while a mod is
still loading. `ConditionEngine_ReadListWatchState` returns every rule's state as JSON for a debug panel.

## From Papyrus

```papyrus
Int watchId

Event OnPlayerLoadGame()
    RegisterForModEvent("MyMod_GuardedChanged", "OnGuardedChanged")
    watchId = ConditionEngine.Watch(Game.GetPlayer(), "Combat || WeaponDrawn", "MyMod_GuardedChanged", True)
EndEvent

Event OnGuardedChanged(String eventName, String watchIdText, Float result, Form akActor)
    Bool guarded = result == 1.0
EndEvent
```

- `Watch(akActor, asExpression, asEventName, abFireImmediately, afPollIntervalSeconds)` returns the watch id, or 0
  on failure. A poll interval of 0 uses the global default; below 0 never polls.
- `Unwatch(id)` stops it and `RefreshWatch(id)` re-checks now.
- Script watches are cleared on every load: create them again from `OnPlayerLoadGame`.

## Signalling your own changes

If a condition reads state your plugin owns (a [callback named condition](adding-named-conditions.md#c-api-callbacks)
or a [custom type](registering-condition-types.md) with `CE_DEP_HOST`), tell the engine when it changes:

```cpp
ConditionEngine_NotifyEvent(CE_DEP_HOST, actor);   // any thread; null actor = every watched actor
```

Any `CE_DEP_*` bits work, and notifications before the main thread handles them merge into one re-check.
