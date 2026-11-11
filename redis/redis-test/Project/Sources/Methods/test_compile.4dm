//%attributes = {"invisible":true,"preemptive":"incapable"}
var $result : Object
ON ERR CALL("test_runtime_error")
If (Application info.headless)
	$result:=Compile project(New object("targets"; New collection))
	LOG EVENT(Into system standard outputs; JSON Stringify($result.errors)+Char(10); Information message)
	ASSERT($result.success)
	LOG EVENT(Into system standard outputs; "COMPILE PASS"+Char(10); Information message)
End if
