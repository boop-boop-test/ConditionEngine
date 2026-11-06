// Examples to start a dial from. Each example is the same JSON that a dial file holds.
window.DIAL_TEMPLATES = [
  {
    id: 'alarm', title: 'Combat alarm', blurb: 'Goes to 10 when a fight starts. After the fight, it decreases by 1 each second.',
    json: {
      label: 'Alarm', range: [0, 10], description: 'How alarmed you are after a fight',
      rules: [
        { label: 'A fight starts', when: 'Combat', set: 10 },
        { label: 'Calms down', while: '!Combat', rate: -1 }
      ],
      conditions: { Alarmed: '>= 5' }
    }
  },
  {
    id: 'hungry_resolve', title: 'Decreases with hunger', blurb: 'Goes to 10 in combat. Then your $hunger decreases it each second.',
    json: {
      label: 'Resolve', range: [0, 10],
      rules: [
        { when: 'Combat', set: 10 },
        { label: 'Hunger decreases it', while: '!Combat', rate: '-$hunger / 100' }
      ]
    }
  },
  {
    id: 'performing', title: 'On and off with events', blurb: 'One mod event sets it to on. A different mod event sets it to off.',
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
    id: 'wetness', title: 'Wetness', blurb: 'Rain makes you wet. Water makes you fully wet. You become dry indoors. This also applies to followers.',
    json: {
      label: 'Wetness', range: [0, 100], unit: '%', scope: 'actor', track: 'IsPlayer || IsFollower',
      rules: [
        { label: 'Rain', while: '@OutsideInRain', rate: 1.5 },
        { label: 'In the water', while: 'InWater', set: 100 },
        { label: 'Becomes dry', while: '!@OutsideInRain && !InWater', approach: 0, halfLife: 120 }
      ],
      thresholds: [20, 70],
      conditions: { Damp: '>= 20', Soaked: '>= 70' }
    }
  },
  {
    id: 'adrenaline', title: 'Adrenaline', blurb: 'Hits and fights increase it. It decreases slowly out of combat.',
    json: {
      label: 'Adrenaline', range: [0, 100],
      rules: [
        { label: 'A hit', on: 'Hit', add: 12, cooldown: 1 },
        { label: 'A fight starts', on: 'CombatStart', add: 25 },
        { label: 'Decreases', while: '!Combat', approach: 0, halfLife: 20 }
      ],
      thresholds: [60],
      conditions: { AdrenalineRush: '>= 60' }
    }
  },
  {
    id: 'kills_today', title: 'Kills today', blurb: 'Counts your kills. It goes back to 0 at dawn.',
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
    id: 'hours_awake', title: 'Hours awake', blurb: 'Increases by 1 each game hour. It goes back to 0 when you sleep. Time that you wait also counts.',
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
    id: 'fear', title: 'Fear of you (each NPC)', blurb: 'Each NPC that you hit has more fear of you. The fear decreases in one minute.',
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
    id: 'outfit_mode', title: 'Mode switch', blurb: 'Modes with names. A key goes to the next mode. When you draw a weapon, the mode is Battle.',
    json: {
      label: 'Outfit mode', options: ['Casual', 'Formal', 'Battle'],
      rules: [
        { label: 'F7 goes to the next mode', key: 'F7', cycle: 1 },
        { label: 'Weapon drawn', when: 'WeaponDrawn', set: 2 }
      ],
      conditions: { CasualMode: 'Casual', FormalMode: 'Formal', BattleMode: 'Battle' }
    }
  },
  {
    id: 'burning', title: 'On fire (for a while)', blurb: 'A fire effect hits the actor: on for 5 seconds. More fire keeps it on.',
    json: {
      kind: 'toggle', label: 'Burning', scope: 'actor', track: 'IsPlayer || IsFollower',
      rules: [
        { on: 'MagicEffect', keyword: 'MagicDamageFire', set: 1, for: 5 }
      ]
    }
  },
  {
    id: 'tension', title: 'Sneak tension', blurb: 'Increases while you sneak, and faster in combat. It decreases when you stand up.',
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
    id: 'felt_cold', title: 'Felt cold (smoothed)', blurb: 'Follows $cold slowly. A short gust of wind does not make you shiver.',
    json: {
      label: 'Felt cold', range: [0, 100],
      rules: [
        { while: true, approach: '$cold', halfLife: 0.5, clock: 'game' }
      ],
      conditions: { Shivering: '>= 60' }
    }
  },
  {
    id: 'gift_mood', title: 'Gifts (each follower)', blurb: 'A follower likes you more when you give items. This decreases over game days.',
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
    id: 'manic', title: 'Manic spells (SkyrimNet)', blurb: 'Approximately one time each day, a follower becomes manic for half an hour. SkyrimNet knows this, keeps a memory of it, and can calm the follower.',
    json: {
      kind: 'toggle', label: 'Manic', scope: 'actor', track: 'IsFollower && !IsPlayer',
      description: 'A manic period: restless, talks much, acts fast',
      rules: [
        { label: 'A manic period starts (approximately one time each day)', every: 1, clock: 'game', chance: 0.05, cooldown: 20, set: 1, for: 0.5,
          tell: '{name} suddenly becomes manic and restless.' },
        { label: 'A person calms the NPC', aiAction: 'CalmDown', description: 'Breathe slowly and become calm after a manic period, when a person helps you',
          if: '@Manic', set: 0, tell: '{name} becomes calm.' }
      ],
      conditions: {
        Manic: { expr: '== 1', knowledge: 'You are in a manic period. You are restless, you talk fast and you go from idea to idea. It is difficult to calm you. You do not know this about yourself.' }
      }
    }
  },
  {
    id: 'annoyance', title: 'Annoyed by talk (SkyrimNet)', blurb: 'Each line that a person says to an NPC annoys the NPC a little. This decreases in one hour.',
    json: {
      label: 'Annoyance', range: [0, 100], scope: 'actor', track: '!IsPlayer',
      rules: [
        { label: 'A person spoke to the NPC', skyrimnet: 'dialogue', actor: 'other', add: 4, cooldown: 5 },
        { while: true, rate: -20, clock: 'game' }
      ],
      thresholds: [60],
      conditions: { Irritated: { expr: '>= 60', knowledge: 'You do not want to hear more talk now. You become angry quickly.' } }
    }
  },
  {
    id: 'blank', title: 'Empty', blurb: 'A number from 0 to 100, with no rules.',
    json: { label: 'New dial', range: [0, 100], rules: [] }
  }
];
