//%attributes = {"invisible":true,"preemptive":"incapable"}
#DECLARE($name : Text) : Text
var $worker : 4D.SystemWorker
var $value; $command : Text
ASSERT(New collection("REDIS_TEST_HOST"; "REDIS_TEST_PORT"; "REDIS_TEST_PASSWORD"; "REDIS_TEST_TLS_CA").includes($name))
If (Is Windows)
	// Delayed expansion keeps shell metacharacters in environment values as data.
	$command:="cmd.exe /d /v:on /c \"chcp 65001 >nul & if defined "+$name+" echo(!"+$name+"!\""
Else
	$command:="/usr/bin/printenv "+$name
End if
$worker:=4D.SystemWorker.new($command; New object("timeout"; 5))
$worker.wait(5)
$value:=$worker.response
If (Length($value)>0)
	If (Character code(Substring($value; Length($value); 1))=10)
		$value:=Substring($value; 1; Length($value)-1)
		If (Length($value)>0)
			If (Character code(Substring($value; Length($value); 1))=13)
				$value:=Substring($value; 1; Length($value)-1)
			End if
		End if
	End if
End if
return $value
