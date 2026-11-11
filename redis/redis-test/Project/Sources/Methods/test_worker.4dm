//%attributes = {"invisible":true,"preemptive":"incapable"}
#DECLARE($state : Object; $optionsJSON : Text)
var $result : Object
$result:=REDIS Open(JSON Parse($optionsJSON))
Use ($state)
	$state.ok:=$result.ok
	If ($result.ok)
		$state.connection:=$result.connection
	Else
		$state.code:=$result.code
	End if
	$state.ready:=True
End use
// The worker remains alive after this method returns; do not close its connection.
