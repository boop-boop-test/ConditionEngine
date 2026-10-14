Scriptname ConditionEngine Hidden

; ==========================================================================
; ConditionEngine - reusable condition expressions for Papyrus
; C++ implementations in ConditionEngine.dll (src/Papyrus.cpp)
;
; Expression examples:
;   "Time(20, 6) && InteriorExterior(false)"
;   "Weather(Rainy) || Weather(Snow)"
;   "ActorValue(Health, usePercentage=true) < 30"
;   "@IsNight and not Combat"
;
; akActor = None evaluates against the player.
; ==========================================================================

;
; Evaluation - latent: the script waits one frame while the main thread evaluates
; (game state is only safe to read there). Invalid expressions return False.
;
Bool     Function Evaluate(Actor akActor, String asExpression) Global Native
Bool     Function EvaluateNamed(Actor akActor, String asName) Global Native

;
; Named conditions (referenced as @Name or Rule("Name")).
; Script-registered names are cleared on every load - register them again
; from OnPlayerLoadGame.
;
Bool     Function RegisterNamed(String asName, String asExpression) Global Native
Bool     Function UnregisterNamed(String asName) Global Native

;
; Inspection - empty string when the expression is invalid
;
String   Function Validate(String asExpression) Global Native   ; "" when valid, else the problems
String   Function Describe(String asExpression) Global Native   ; human-readable summary
String   Function ToJson(String asExpression) Global Native     ; JSON condition tree
String   Function Normalize(String asExpression) Global Native  ; canonical expression text
String   Function GetCatalog() Global Native                    ; JSON: every condition type and its fields

;
; Watches - sends a mod event whenever the result changes:
;   RegisterForModEvent(asEventName, "OnConditionChanged")
;   Event OnConditionChanged(String eventName, String watchId, Float result, Form akActor)
; result is 1.0 (true) or 0.0 (false). Returns the watch id, 0 on failure.
; Watches are cleared on every load - create them again from OnPlayerLoadGame.
;
; afPollIntervalSeconds: 0 = global default (10 s, see ConditionEngine.ini),
; below 0 = never poll (re-check only on change detection and game events).
Int      Function Watch(Actor akActor, String asExpression, String asEventName, Bool abFireImmediately = False, Float afPollIntervalSeconds = 0.0) Global Native
         Function Unwatch(Int aiWatchId) Global Native
         Function RefreshWatch(Int aiWatchId) Global Native   ; re-check now; event fires only if the result changed

;
; Full re-check interval for watches (change detection still runs every second)
;
         Function SetPollInterval(Float afSeconds) Global Native
Float    Function GetPollInterval() Global Native

;
; Values - numbers read from the game, combined with arithmetic:
;   "ActorValue(Health, usePercentage=true)", "$vampire_hunger", "clamp($hunger * (1 - $light), 0, 100)"
; Conditions compare them: Evaluate(akActor, "$vampire_hunger > 80").
; asNameOrExpr is value text or a bare named-value name ("vampire_hunger").
;
; GetValue is latent (waits one frame). An unavailable value returns its fallback, or 0.
Float    Function GetValue(Actor akActor, String asNameOrExpr) Global Native

; Pushed values: set a named value from a script (e.g. a SkyrimNet action). Pushing a
; name nobody declared declares it. afTimeoutSeconds 0 = the value's default timeout
; (none when it has none); afterwards it reverts to its expression or default.
; Pushed values are not saved - every load starts clean, so push again after loading.
Bool     Function SetValue(Actor akActor, String asName, Float afValue, Float afTimeoutSeconds = 0.0) Global Native
Bool     Function SetGlobalValue(String asName, Float afValue, Float afTimeoutSeconds = 0.0) Global Native
Bool     Function ClearValue(Actor akActor, String asName) Global Native
Bool     Function ClearGlobalValue(String asName) Global Native

; Named values ($name). asDefinition is value text or a JSON definition
; ({"expr", "label", "unit", "range", "fallback", "enabledWhen", "pushed", ...}).
; Script-registered values are cleared on every load - register them again from OnPlayerLoadGame.
Bool     Function RegisterNamedValue(String asName, String asDefinition) Global Native

; Value watches - sends a mod event when the value moves by afMinStep (any change when 0):
;   RegisterForModEvent(asEventName, "OnValueChanged")
;   Event OnValueChanged(String eventName, String watchId, Float value, Form akActor)
; afPollIntervalSeconds: 0 = global default, below 0 = only on events. Stop with Unwatch.
Int      Function WatchValue(Actor akActor, String asNameOrExpr, String asEventName, Float afMinStep = 0.0, Float afPollIntervalSeconds = 0.0, Bool abFireImmediately = False) Global Native

; Why a condition is true / false (or what a value reads) for the actor right now, as
; indented text: each part's state ([T] [F] [P]ending [U]navailable [-] skipped), the numbers
; involved and the reasons. Latent. Print it with Debug.Trace or a message box.
String   Function Explain(Actor akActor, String asExpression) Global Native
