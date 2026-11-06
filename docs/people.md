# Important people

One question many mods ask: *is this somebody the player cares about?* A follower mod that should leave the Jarl
alone, an outfit rule for when your spouse is the one wearing it, an animation that should only play on the cast
that matters, a SkyrimNet prompt that treats court members differently. ConditionEngine keeps one shared list of
important people, in groups, that every mod reads the same way and that mods and players add to.

```
ImportantPerson()                       the actor is in any group
ImportantPerson([court, spouse])        in one of these groups
ImportantPerson(court)                  one group
!ImportantPerson() && Combat            an unimportant actor in combat
ImportantPerson(includePlayer=true)     the player counts too (by default the player never does)
```

In the editor and other mods' pickers it is **Important Person** in the **Important People** category; the groups
field lists every group, those presets and mods added too. `IsImportant` and `Important` are aliases.

## The built-in groups

| Group | Label | Who |
|---|---|---|
| `court` | Jarls & their courts | Jarls, stewards, housecarls and court wizards of every hold (the vanilla job factions) |
| `leaders` | Leaders & royalty | Titus Mede II, Ulfric, Tullius, Elisif, Galmar, Rikke, Elenwen, Maven Black-Briar, Isran, Harkon |
| `guilds` | Guild members | Companions, College of Winterhold, Thieves Guild, Dark Brotherhood, Bards College, Dawnguard |
| `quest` | Main & DLC quest people | Greybeards, Blades, Arngeir, Delphine, Esbern, Paarthurnax, Serana, Valerica, Frea, Storn, Neloth, Miraak |
| `followers` | Your followers | Everyone following you now: vanilla followers (`CurrentFollowerFaction`) and any mod's (player teammates) |
| `spouse` | Your spouse | `PlayerMarriedFaction` |
| `household` | Your housecarls & family | `PlayerHousecarlFaction`, Hearthfire housecarls, stewards and adopted children |
| `essential` | Essential characters | Anyone the game keeps from dying right now: quest characters of vanilla and of mods |
| `custom` | Added by you | Empty until you add someone |

They are built into the DLL (`presets/ConditionEngine_People.json` in the repository). Members from a DLC that is not
installed are skipped. Groups are checked in this order, so `GetImportantGroup` and `ce people` name the first group
an actor is in.

## Adding people yourself

In game, open the console, click the person and type:

```
ce people                  the groups, and which ones the selected actor (or the player) is in
ce people add              adds the selected actor to "custom"
ce people add court        ...or to any group, a new name makes a new group
ce people remove           takes them out again
ce people remove court     takes a preset's member out of a group, for you only
```

What you add is kept in `Data/SKSE/Plugins/ConditionEngine/User/People.json` (under Mod Organizer, in Overwrite),
for every save. You can edit it by hand; it is the preset format below plus a `"remove"` list per group.

## Adding people from a mod

**A preset file** (`Data/SKSE/Plugins/ConditionEngine/*.json`) with a `"people"` section. Naming a group that exists
adds members to it; a new name makes a group. The first file that labels a group keeps its label and order.

```json
{
    "people": {
        "court":  { "members": ["0x801|MyCastle.esp"] },
        "mycast": {
            "label": "The Ashen Order", "order": 35, "icon": "🔥",
            "description": "The people of my quest mod.",
            "members": ["0x802|MyMod.esp", "0x803|MyMod.esp", "0x810|MyMod.esp"]
        }
    }
}
```

A member is a form string (`"0x1A6B|Skyrim.esm"`), a console form ID (`"0x1A6B"`) or an editor ID, of:

- an **NPC** (actor base): every actor with that base, leveled and templated ones too;
- a **placed actor** (reference): that one;
- a **faction**: its members (any rank);
- a **keyword**: actors whose race or base has it;
- a **form list** of any of these.

A group can also say `"teammates": true` (every current follower) or `"essential": true` (every essential actor).
A bare list is the members: `"mycast": ["0x802|MyMod.esp"]`.

**Papyrus** (`ConditionEngine.psc`):

```papyrus
ConditionEngine.AddImportantPerson(MyBoss, "mycast")         ; until the game closes; add again OnPlayerLoadGame
ConditionEngine.RememberImportantPerson(akTarget)            ; the player's own list, kept (e.g. an MCM option)
ConditionEngine.ForgetImportantPerson(akTarget)
Bool isVip = ConditionEngine.IsImportantPerson(akActor)      ; latent, one frame
String which = ConditionEngine.GetImportantGroup(akActor, "court, leaders")
String json = ConditionEngine.GetPeopleGroups()
```

Actors are stored as their base NPC, so an entry survives the actor respawning. Actors created in game (no plugin)
cannot be stored; add a faction you put them in instead.

**C API** (`ConditionEngineAPI.h`):

```c
ConditionEngine_DefinePeopleGroup("mycast", "{\"label\": \"The Ashen Order\", \"members\": [\"0x802|MyMod.esp\"]}",
                                  NULL, err, sizeof err);
ConditionEngine_AddImportantPerson("mycast", "0x803|MyMod.esp", NULL);
char group[64];
ConditionEngine_GetImportantGroup(actor, NULL, group, sizeof group);   /* "" when none; main thread */
```

## Notes

- Watches see a follower joining or leaving within a second (change detection). Faction changes (a marriage,
  joining a guild) and members added later show on the next full re-check (the poll interval, 10 s by default).
- `essential` changes with quests: an NPC is essential while a quest needs them. Leave it out
  (`ImportantPerson([court, leaders, guilds, quest, followers, spouse, household, custom])`) for a fixed list.
- The player is never an important person unless the condition says `includePlayer=true`: the player is in the
  Companions, married, and so on, and most rules mean other people.
