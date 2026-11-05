# Weather

ConditionEngine can tell what the current weather is like with any weather mod installed. It covers rain, snow,
storms, blizzards, fog, wind, ash, sand and the aurora, and it works for the vanilla weathers and for the hundreds a
weather mod adds. A preset, a DressCode rule or an OAR config can ask "is it raining", "is this a blizzard" or "how
foggy is it" without knowing which weather mod is installed.

The definitions are compiled into `ConditionEngine.dll` (`presets/ConditionEngine_Weather.json` in the repository),
like the [survival values](survival.md). A preset file that defines a value or condition of the same name replaces
the built-in one.

## What you can read

Values (the same for every actor; 0 when there is no weather, e.g. in the main menu):

| Name | What it is |
|---|---|
| `$weather_rain` | 25 light rain or a sun shower, 50 rain, 75 heavy rain, 100 extreme |
| `$weather_snow` | 25 flurries, 50 snow, 75 heavy snow |
| `$weather_precipitation` | rain or snow, whichever is heavier |
| `$weather_thunder` | lightning: 25 rare, 50 occasional, 75 frequent |
| `$weather_storm` | 75 a storm of any kind (thunder, gale-driven rain or snow, blizzard, ash or dust storm), 100 severe |
| `$weather_blizzard` | 75 when the snow is wind-driven |
| `$weather_fog` | 25 mist or haze, 50 fog, 75 thick fog |
| `$weather_wind` | the weather's wind speed, 0-100 (vanilla: about 10 calm, 20 storm, 30 snow, 70 blizzard) |
| `$weather_ash` | 25 ash in the air, 50 ashfall, 75 ash storm |
| `$weather_sand` | 25 dusty, 50 blowing sand or dust, 75 dust storm |
| `$weather_clouds` | 50 cloudy, 75 overcast, 100 storm clouds |
| `$weather_clear` | 100 clear, 50 partly cloudy |
| `$weather_aurora` | 100 when the weather shows an aurora (at night) |
| `$season` | 0 winter, 1 spring, 2 summer, 3 autumn, from the game month |

Conditions:

| Name | True when |
|---|---|
| `@WeatherClear`, `@WeatherCloudy`, `@WeatherOvercast` | clear >= 50, clouds >= 50, clouds >= 75 |
| `@WeatherRain`, `@WeatherHeavyRain` | rain >= 25, rain >= 75 |
| `@WeatherSnow`, `@WeatherHeavySnow` | snow >= 25, snow >= 75 |
| `@WeatherPrecipitation` | rain or snow >= 25 |
| `@WeatherStorm`, `@WeatherThunder`, `@WeatherBlizzard` | storm >= 75, any lightning, blizzard |
| `@WeatherMist`, `@WeatherFog`, `@WeatherThickFog` | fog >= 25, 50, 75 |
| `@WeatherWindy`, `@WeatherGale` | wind >= 40, wind >= 65 |
| `@WeatherAsh`, `@WeatherAshStorm` | ash >= 50, ash >= 75 |
| `@WeatherDust`, `@WeatherSandstorm` | sand >= 25, sand >= 75 |
| `@WeatherAurora` | the weather has an aurora |
| `@WeatherBad` | rain or snow, a storm, fog, ashfall or blowing sand |
| `@OutsideInRain`, `@OutsideInSnow`, `@OutsideInStorm`, `@OutsideInBadWeather` | the same, and the actor is in an exterior |
| `@SeasonWinter`, `@SeasonSpring`, `@SeasonSummer`, `@SeasonAutumn` | `$season` is 0, 1, 2, 3 |

The values read the sky's weather wherever the actor is. Indoors the sky keeps the last outdoor weather, so
`@WeatherRain` stays true inside a tavern. Use `@OutsideInRain` for "getting rained on".

The condition types behind them:

- `WeatherKind(Rain)` reads one kind as a value. As a condition it is true from 25: `WeatherKind(Rain)`, or
  `WeatherKind(Rain) >= 75`. Kinds: `Clear`, `Clouds`, `Rain`, `Snow`, `Precipitation`, `Thunder`, `Storm`,
  `Blizzard`, `Fog`, `Wind`, `Ash`, `Sand`, `Aurora` (aliases such as `Rainy`, `Overcast`, `Thunderstorm`, `Foggy`,
  `Windy`, `Ashfall`, `Dust`, `Sandstorm` work too).
- `Weather(...)` keeps its classification values `Clear`, `Cloudy`, `Rainy` and `Snowy`. These read the record's
  flags, as the game itself does. It also takes `Stormy`, `Thunderstorm`, `Blizzard`, `Foggy`, `Misty`, `Windy`,
  `Ashfall`, `Sandstorm`, `Aurora` and `Overcast`, which read the profile, and a weather's editor ID with `*` and `?`
  allowed (`Weather(TS_*_Heavy)`).

```papyrus
Float rain = ConditionEngine.GetValue(akActor, "weather_rain")          ; 0-100
Bool cloak = ConditionEngine.Evaluate(akActor, "@OutsideInRain || @OutsideInSnow")
```

In OAR, `ConditionEngine` with the preset `WeatherBlizzard`, or `ConditionEngineExpression` with
`$weather_wind >= 60 && !InteriorExterior(true)`.

`ce weather` in the console prints the current weather and what each kind reads, with the reason. During a weather
change it also prints the outgoing weather:

```
ce: weather SkyrimStormSnow (0x000C8221, Skyrim.esm)
  Clouds         100  storm
  Snow            75  classed snow, editor ID says heavy or storm
  Precipitation   75  snow
  Storm           75  editor ID
  Blizzard        75  snow, editor ID says storm or blizzard
  Wind            70  wind speed
```

## How a weather is read

Weather mods disagree on numbers. Vanilla rain has a particle density of 1, Obsidian's has 9. Azurite gives every
weather the same wind speed. Vivid's fog weathers have fog distances from 250000 down to 20000. So a weather's
profile combines three things that every weather mod keeps meaningful:

- **The classification flags** (Pleasant, Cloudy, Rainy, Snow). The engine itself uses these, for wet surfaces, NPC
  behavior and lightning.
- **The record's data**: the precipitation particles (their type, rain or snow, and texture: Dragonborn's ash
  texture, sand or dust, or debris such as pine needles and Sovngarde's ice shards), wind speed, lightning frequency,
  fog distance and the aurora model.
- **The editor ID**: weather mods name their weathers for what they are. Examples: `SkyrimStormSnow`,
  `TS_SolstheimDustStorm`, `SkyrimOvercast_LowRain_Thunder_43`, `xStormRain`, `CoTSnow_HS`. It is split into words
  ("Overcast", "Low", "Rain", "Thunder"). Words that follow "No" (`NoFog`) are dropped.

| Kind | Read from |
|---|---|
| Rain | classed rainy (50), or rain particles in a weather that isn't (25, a sun shower). Light words → 25. Heavy or storm words, or lightning frequency below 200 → 75. Heavy plus storm, or "extreme" → 100 |
| Snow | classed snow (50), or snow particles in a fair or cloudy weather (25, flurries). Heavy or storm words, or a gale → 75 |
| Blizzard | snow of at least 50, plus storm or blizzard words or wind of at least 50% |
| Thunder | lightning frequency in a weather classed rainy (255 means never): 240+ rare, 150+ occasional, below that frequent. "Thunder" or "Lightning" in the editor ID gives at least 50 |
| Storm | storm words, the heavy storm sky model, thunder of 50+, a blizzard, an ash or dust storm, or heavy rain or snow in strong wind |
| Fog | fog or mist words, with the fog distance setting how thick (beyond 100000 → 25, 12000 or nearer → 75), or "Heavy" / "Thick". Without words: a dry weather that isn't fair, with fog nearer than 10000 |
| Ash, Sand | ash or sand / dust words, or particles with that texture. They come ahead of snow because they fall as snow-type particles |
| Aurora | "Aurora" in the editor ID, or an aurora model (`Sky\SkyrimAurora*.nif`) on a dry, fair sky. Other sky models in that slot (storm sky, Soul Cairn, eclipse) are not auroras |
| Wind | the wind speed, 0-255, scaled to 0-100 |

Editor IDs: the game drops weather editor IDs after loading. With powerofthree's Tweaks installed they are kept and
used. Otherwise ConditionEngine reads the weather records' editor IDs from the loaded plugin files once, on a
worker thread at load (a few hundred records, read straight from each plugin's weather group). A weather then
takes the editor ID from the last plugin that edits it. This matters because Climates of Tamriel renames the
vanilla records it edits.

## Tested weather mods

The profile was checked against every weather record in these, 1,262 records in all:

| Mod | Weathers | Notes |
|---|---|---|
| Vanilla, Update, Dawnguard, Dragonborn | 177 | Storms, Storm Call's storm, blizzard, fog, the aurora (`_A`) variants, Solstheim's ash and ash storm, Soul Cairn |
| Obsidian Weathers and Seasons 1.07a | 86 edits | Edits vanilla records only. Adds light snow to some fair mountain weathers (flurries, 25) |
| Cathedral Weathers and Seasons 2.23c | 94 edits, 9 new | `xStormRain`, `xStormSnow`, `xFog`, ... Some fog weathers made fair (mist 25). Aurora models on rainy skies are ignored |
| Azurite Weathers and Seasons III 3.35 | 92 edits | The same wind speed everywhere. Its overcast rain has rare lightning (thunder 25) |
| Vivid Weathers Definitive Edition X25 2.5.3 | 71 edits, 461 new | Region names in editor IDs (`SkyrimClear_Snow_54` is a clear sky over snow). Graded fog, low / high rain, thunder, blizzards, Solstheim ash |
| True Storms SE 1.0.3 | 18 edits, 12 new | Heavy fogs, Solstheim rainstorm, ashfall, ashen fog and dust storm |
| Climates of Tamriel 5.7 | 95 edits, 2 new | Renames the records it edits (`CoTSnow_HS`, `CoTAsh_LS`) |
| Rustic Weathers and Lighting v33b | 66 edits, 8 new | `RusticSunnyRain` (a sun shower), `RusticMistyRain`, snow bits in cloudy weathers |
| Aequinoctium 1.70 | 67 edits, 4 new | Ash-textured motes in Riften's fog are not ash |

Other weather mods and their patches work the same way, as long as their weathers are classified and named the way
these are. If one reads wrong, check it with `ce weather`, then correct it with a `"weathers"` section (below).

## Correcting a weather

A preset file in `Data/SKSE/Plugins/ConditionEngine/` can set any kind of any weather. Keys are editor IDs, with `*`
and `?` allowed, or form strings. A listed kind replaces the computed level, and unlisted kinds stay:

```json
{
  "weathers": {
    "MyMod_Monsoon*": { "rain": 100, "storm": 100 },
    "TS_*_Heavy": { "fog": 75 },
    "0x10A241|Skyrim.esm": { "storm": 0 }
  }
}
```

Later files win where they list the same kind.

## Seasons

`$season` comes from the game month, split the way seasonal weather mods split it (Azurite, Obsidian, Seasonal
Weathers Framework): winter from Evening Star to Sun's Dawn, spring from First Seed to Second Seed, summer from
Midyear to Last Seed, and autumn from Hearthfire to Sun's Dusk. A mod with a different calendar can define its own
`season` in a preset.

## Limits

- **The current weather only.** During a weather change, the values read the incoming weather from the start. The
  rain is reported while it is still fading in. `ce weather` shows both weathers and how far the change has gone.
- **The record, not the renderer.** ENB and Community Shaders fog, wetness and volumetric effects are not seen, and
  neither are scripted effects. True Storms' and Wonders of Weather's scripted lightning and sounds are examples.
- **Wetness is not tracked.** "How wet is the actor" needs a mod that tracks it, as Frostfall does. The weather
  values say what is falling, not how long the actor has been out in it.
- **A weather with no classification, no data and an unhelpful editor ID reads as nothing**, for example the quest
  and lighting weathers named `FXWthr...`. That is usually right.
