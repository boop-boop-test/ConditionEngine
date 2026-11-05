Scriptname Dials Hidden

; ==========================================================================
; Dials - values that rules drive: events turn them on and off, conditions set
; them, rates raise and lower them over time. Every dial is also a
; ConditionEngine value ($name) and can name conditions (@Name).
; C++ implementations in ConditionEngine.dll (src/Dials/Papyrus.cpp). Guide: docs/dials.md
;
; akActor = None means the player. Global dials ignore akActor.
; Values are saved with the game.
;
; Changes send the mod event "Dials_Changed": strArg = the dial, numArg = its
; value, sender = the actor (None for global dials):
;   RegisterForModEvent("Dials_Changed", "OnDialChanged")
;   Event OnDialChanged(string eventName, string dial, float value, Form sender)
; ==========================================================================

; ── Values ───────────────────────────────────────────────────────────────────
Float    Function GetValue(String asDial, Actor akActor = None) Global Native
Function SetValue(String asDial, Float afValue, Actor akActor = None) Global Native
Function AddValue(String asDial, Float afDelta, Actor akActor = None) Global Native
Function Reset(String asDial, Actor akActor = None) Global Native
Function Toggle(String asDial, Actor akActor = None) Global Native
; Next option of a choice dial (aiStep -1 = previous); numbers step and wrap
Function Cycle(String asDial, Int aiStep = 1, Actor akActor = None) Global Native
; A choice dial's current option label ("" for other dials)
String   Function GetLabel(String asDial, Actor akActor = None) Global Native

; ── Events ───────────────────────────────────────────────────────────────────
; Fires the rules that listen for asEvent ({"event": "asEvent"}) as if a mod event
; with this name, text and number arrived. Same as SendModEvent, without the event.
Function Signal(String asEvent, Actor akActor = None, Float afNumber = 0.0, String asText = "") Global Native

; ── Definitions ──────────────────────────────────────────────────────────────
; Defines (or replaces) a dial from JSON, the same object a dial file holds:
;   Dials.Define("MyMod_Alarm", "{\"range\": [0, 10], \"rules\": [{\"when\": \"IsInCombat\", \"set\": 10}, {\"while\": \"!IsInCombat\", \"rate\": -1}]}")
; Returns "" when it worked, otherwise the errors (one per line). Values survive
; redefining and saving. Define again from OnPlayerLoadGame or ship a dial file
; (a "dials" section in Data/SKSE/Plugins/ConditionEngine/*.json) instead: script
; definitions are not saved.
String   Function Define(String asDial, String asJson, String asSource = "Papyrus") Global Native
Bool     Function Remove(String asDial) Global Native
Bool     Function Exists(String asDial) Global Native
String[] Function List() Global Native
; The dial as JSON (its definition and values), "" when unknown
String   Function Describe(String asDial) Global Native

; ── Editor ───────────────────────────────────────────────────────────────────
; Opens the ConditionEngine editor (needs Magelight UI), at asDial when given
Function OpenEditor(String asDial = "") Global Native
Function ReloadFiles() Global Native
