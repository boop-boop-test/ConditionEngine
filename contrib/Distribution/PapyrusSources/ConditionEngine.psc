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
; Evaluation
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

;
; Watches - sends a mod event whenever the result changes:
;   RegisterForModEvent(asEventName, "OnConditionChanged")
;   Event OnConditionChanged(String eventName, String watchId, Float result, Form akActor)
; result is 1.0 (true) or 0.0 (false). Returns the watch id, 0 on failure.
; Watches are cleared on every load - create them again from OnPlayerLoadGame.
;
Int      Function Watch(Actor akActor, String asExpression, String asEventName, Bool abFireImmediately = False) Global Native
         Function Unwatch(Int aiWatchId) Global Native
