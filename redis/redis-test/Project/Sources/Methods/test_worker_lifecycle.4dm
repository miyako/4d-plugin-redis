//%attributes = {"invisible":true,"preemptive":"incapable"}
#DECLARE($options : Object)
var $state; $result : Object
var $worker : Text
var $attempt; $handle : Integer
$state:=New shared object("ready"; False; "ok"; False; "connection"; 0; "code"; "")
$worker:="redis-test-"+Generate UUID
CALL WORKER($worker; "test_worker"; $state; JSON Stringify($options))
$attempt:=0
While (Not($state.ready) && ($attempt<300))
	DELAY PROCESS(Current process; 1)
	$attempt:=$attempt+1
End while
If (Not($state.ready))
	KILL WORKER($worker)
End if
ASSERT($state.ready)
If (Not($state.ok))
	KILL WORKER($worker)
End if
ASSERT($state.ok)
$handle:=$state.connection
$result:=REDIS Ping($handle)
// Kill the named worker even if the ownership assertion subsequently fails.
KILL WORKER($worker)
test_error($result; "not_submitted"; False)
test_exact_text($result.code; "wrong_owner")

$attempt:=0
$result:=REDIS Connection info($handle)
While (($result.code="wrong_owner") && ($attempt<60))
	DELAY PROCESS(Current process; 1)
	$attempt:=$attempt+1
	$result:=REDIS Connection info($handle)
End while
test_error($result; "not_submitted"; False)
test_exact_text($result.code; "invalid_handle")
LOG EVENT(Into system standard outputs; "WORKER ownership and cleanup verified"+Char(10); Information message)
