//%attributes = {"invisible":true,"preemptive":"incapable"}
ON ERR CALL("test_runtime_error")
If (Application info.headless)
	test_api
	LOG EVENT(Into system standard outputs; "PASS"+Char(10); Information message)
End if
