//%attributes = {"invisible":true,"preemptive":"incapable"}
#DECLARE($actual : Text; $expected : Text)
var $i : Integer
ASSERT(Length($actual)=Length($expected))
For ($i; 1; Length($expected))
	ASSERT(Character code(Substring($actual; $i; 1))=Character code(Substring($expected; $i; 1)))
End for
