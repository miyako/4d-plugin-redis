//%attributes = {"invisible":true,"preemptive":"incapable"}
var $details : Object
$details:=New object("error"; Error; "method"; Error method; "line"; Error line)
LOG EVENT(Into system standard outputs; "FAIL "+JSON Stringify($details)+Char(10); Error message)
QUIT 4D
ABORT
