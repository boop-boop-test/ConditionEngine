// Ideas to start a dial from. Each is the same JSON a dial file holds.
window.DIAL_TEMPLATES = [
  {
    id: 'alarm', title: 'Combat alarm', blurb: 'Jumps to 10 when a fight starts, then drops 1 per second once it is over.',
    json: {
      label: 'Alarm', range: [0, 10], description: 'How alarmed you still are after a fight',
      rules: [
        { label: 'A fight starts', when: 'Combat', set: 10 },
        { label: 'Calms down', while: '!Combat', rate: -1 }
      ],
      conditions: { Alarmed: '>= 5' }
    }
  },
  {
    id: 'hungry_resolve', title: 'Fades with hunger', blurb: 'Set to 10 in combat, then lowered by your current $hunger each second.',
    json: {
      label: 'Resolve', range: [0, 10],
      rules: [
        { when: 'Combat', set: 10 },
        { label: 'Hunger wears it down', while: '!Combat', rate: '-$hunger / 100' }
      ]
    }
  },
  {
    id: 'performing', title: 'On / off by events', blurb: 'One mod event switches it on, another switches it off.',
    json: {
      kind: 'toggle', label: 'Performing',
      rules: [
        { event: 'Bard_Start', set: 1 },
        { event: 'Bard_Stop', set: 0 }
      ],
      conditions: { IsPerforming: '== 1' }
    }
  },
  {
    id: 'wetness', title: 'Wetness', blurb: 'Rain soaks you, water drenches you, you dry off indoors. Followers too.',
    json: {
      label: 'Wetness', range: [0, 100], unit: '%', scope: 'actor', track: 'IsPlayer || IsFollower',
      rules: [
        { label: 'Rain', while: '@OutsideInRain', rate: 1.5 },
        { label: 'In the water', while: 'InWater', set: 100 },
        { label: 'Drying', while: '!@OutsideInRain && !InWater', approach: 0, halfLife: 120 }
      ],
      thresholds: [20, 70],
      conditions: { Damp: '>= 20', Soaked: '>= 70' }
    }
  },
  {
    id: 'adrenaline', title: 'Adrenaline', blurb: 'Hits and fights pump it up; it ebbs away out of combat.',
    json: {
      label: 'Adrenaline', range: [0, 100],
      rules: [
        { label: 'Got hit', on: 'Hit', add: 12, cooldown: 1 },
        { label: 'A fight starts', on: 'CombatStart', add: 25 },
        { label: 'Ebbs', while: '!Combat', approach: 0, halfLife: 20 }
      ],
      thresholds: [60],
      conditions: { AdrenalineRush: '>= 60' }
    }
  },
  {
    id: 'kills_today', title: 'Kills today', blurb: 'Counts your kills and starts again at dawn.',
    json: {
      label: 'Kills today', range: [0, 9999], integer: true,
      rules: [
        { on: 'Kill', add: 1, if: 'IsPlayer' },
        { label: 'Dawn', when: 'Time(6, 7)', reset: true }
      ],
      conditions: { Bloodied: '>= 10' }
    }
  },
  {
    id: 'hours_awake', title: 'Hours awake', blurb: 'Rises 1 per game hour, back to 0 when you sleep. Waiting counts.',
    json: {
      label: 'Hours awake', range: [0, 96], unit: 'h',
      rules: [
        { while: true, rate: 1, clock: 'game' },
        { on: 'SleepStop', reset: true }
      ],
      conditions: { Drowsy: '>= 16', Exhausted: '>= 30' }
    }
  },
  {
    id: 'fear', title: 'Fear of you (per NPC)', blurb: 'Every NPC you hit fears you a little more; it fades over a minute.',
    json: {
      label: 'Fear of the player', range: [0, 100], scope: 'actor', track: '!IsPlayer',
      rules: [
        { label: 'The player hit them', on: 'Hit', other: 'player', add: 20 },
        { while: true, approach: 0, halfLife: 60 }
      ],
      conditions: { Terrified: '>= 80' }
    }
  },
  {
    id: 'outfit_mode', title: 'Mode switch', blurb: 'Named modes you cycle with a key; drawing a weapon picks Battle.',
    json: {
      label: 'Outfit mode', options: ['Casual', 'Formal', 'Battle'],
      rules: [
        { label: 'F7 cycles', key: 'F7', cycle: 1 },
        { label: 'Weapon out', when: 'WeaponDrawn', set: 2 }
      ],
      conditions: { CasualMode: 'Casual', FormalMode: 'Formal', BattleMode: 'Battle' }
    }
  },
  {
    id: 'burning', title: 'On fire (for a while)', blurb: 'A fire effect lands: on for 5 seconds, longer if it keeps landing.',
    json: {
      kind: 'toggle', label: 'Burning', scope: 'actor', track: 'IsPlayer || IsFollower',
      rules: [
        { on: 'MagicEffect', keyword: 'MagicDamageFire', set: 1, for: 5 }
      ]
    }
  },
  {
    id: 'tension', title: 'Sneak tension', blurb: 'Builds while sneaking, faster in combat; relaxes when you stand up.',
    json: {
      label: 'Tension', range: [0, 100],
      rules: [
        { while: 'Sneaking', rate: 3 },
        { while: 'Sneaking && Combat', rate: 6 },
        { while: '!Sneaking', approach: 0, halfLife: 8 }
      ]
    }
  },
  {
    id: 'felt_cold', title: 'Felt cold (smoothed)', blurb: 'Follows $cold slowly, so a gust does not make you shiver.',
    json: {
      label: 'Felt cold', range: [0, 100],
      rules: [
        { while: true, approach: '$cold', halfLife: 0.5, clock: 'game' }
      ],
      conditions: { Shivering: '>= 60' }
    }
  },
  {
    id: 'gift_mood', title: 'Gifts (per follower)', blurb: 'A follower you give items to warms up; it cools over game days.',
    json: {
      label: 'Gift mood', range: [0, 100], scope: 'actor', track: 'IsFollower && !IsPlayer',
      rules: [
        { label: 'A gift from the player', on: 'ItemAdded', other: 'player', add: 'DialsEvent * 2', cooldown: 5 },
        { while: true, rate: -2, clock: 'game' }
      ],
      conditions: { Pleased: '>= 50' }
    }
  },
  {
    id: 'manic', title: 'Manic spells (SkyrimNet)', blurb: 'About once a day each follower goes manic for half an hour; SkyrimNet knows, remembers, and can calm them down.',
    json: {
      kind: 'toggle', label: 'Manic', scope: 'actor', track: 'IsFollower && !IsPlayer',
      description: 'A manic spell: restless, talkative, impulsive',
      rules: [
        { label: 'A spell starts (about once a day)', every: 1, clock: 'game', chance: 0.05, cooldown: 20, set: 1, for: 0.5,
          tell: '{name} is suddenly gripped by a manic restlessness.' },
        { label: 'Being talked down', aiAction: 'CalmDown', description: 'Take a breath and calm down after a manic spell, when someone helps you settle',
          if: '@Manic', set: 0, tell: '{name} calms down.' }
      ],
      conditions: {
        Manic: { expr: '== 1', knowledge: 'You are in a manic phase: restless, talking fast, jumping between ideas, hard to calm. You do not notice it yourself.' }
      }
    }
  },
  {
    id: 'annoyance', title: 'Annoyed by chatter (SkyrimNet)', blurb: 'Every line someone says to an NPC annoys them a little; it fades over an hour.',
    json: {
      label: 'Annoyance', range: [0, 100], scope: 'actor', track: '!IsPlayer',
      rules: [
        { label: 'Someone spoke to them', skyrimnet: 'dialogue', actor: 'other', add: 4, cooldown: 5 },
        { while: true, rate: -20, clock: 'game' }
      ],
      thresholds: [60],
      conditions: { Irritated: { expr: '>= 60', knowledge: 'You have heard enough talking for now and are getting short-tempered.' } }
    }
  },
  {
    id: 'blank', title: 'Blank', blurb: 'An empty number from 0 to 100.',
    json: { label: 'New dial', range: [0, 100], rules: [] }
  }
];
