//%attributes = {"invisible":true,"preemptive":"incapable"}
#DECLARE($actual : Blob; $expected : Blob)
var $i : Integer
ASSERT(BLOB size($actual)=BLOB size($expected))
For ($i; 0; BLOB size($expected)-1)
	ASSERT($actual{$i}=$expected{$i})
End for
