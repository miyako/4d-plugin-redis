//%attributes = {"invisible":true,"preemptive":"incapable"}
var $result; $options : Object
var $invalidOptions : Collection
var $invalid : Integer
var $blob; $original : Blob

// These invalid options must be rejected before attempting any network connection.
$invalidOptions:=New collection(\
	New object("unexpected"; True); \
	New object("host"; 1); \
	New object("port"; 0); \
	New object("port"; 65536); \
	New object("port"; "6379"); \
	New object("database"; -1); \
	New object("database"; 0.5); \
	New object("connectTimeout"; -1); \
	New object("commandTimeout"; -1); \
	New object("tls"; "false"); \
	New object("username"; 1); \
	New object("password"; 1); \
	New object("clientName"; 1); \
	New object("caFile"; 1); \
	New object("certFile"; 1); \
	New object("keyFile"; 1))
For each ($options; $invalidOptions)
	$result:=REDIS Open($options)
	test_error($result; "not_submitted"; False)
	ASSERT(Not(OB Is defined($result; "connection")))
End for each

$invalid:=0
SET BLOB SIZE($original; 4)
$original{0}:=0
$original{1}:=255
$original{2}:=0
$original{3}:=65
COPY BLOB($original; $blob; 0; 0; BLOB size($original))

$result:=REDIS Close($invalid)
test_error($result; "not_submitted"; False)
$result:=REDIS Connection info($invalid)
test_error($result; "not_submitted"; False)
$result:=REDIS Get text($invalid; "offline")
test_error($result; "not_submitted"; False)
$result:=REDIS Set text($invalid; "offline"; ""; New object)
test_error($result; "not_submitted"; False)
$result:=REDIS Get blob($invalid; "offline"; $blob)
test_error($result; "not_submitted"; False)
test_same_blob($blob; $original)
$result:=REDIS Set blob($invalid; "offline"; $blob; New object)
test_error($result; "not_submitted"; False)
test_same_blob($blob; $original)
$result:=REDIS Delete($invalid; New collection("offline"))
test_error($result; "not_submitted"; False)
$result:=REDIS Exists($invalid; New collection("offline"))
test_error($result; "not_submitted"; False)
$result:=REDIS Increment($invalid; "offline"; "1")
test_error($result; "not_submitted"; False)
$result:=REDIS Expire($invalid; "offline"; "1000")
test_error($result; "not_submitted"; False)
$result:=REDIS TTL($invalid; "offline")
test_error($result; "not_submitted"; False)
$result:=REDIS Persist($invalid; "offline")
test_error($result; "not_submitted"; False)
$result:=REDIS Ping($invalid)
test_error($result; "not_submitted"; False)

If (Get environment variable("REDIS_TEST_PORT")#"")
	test_live
End if
