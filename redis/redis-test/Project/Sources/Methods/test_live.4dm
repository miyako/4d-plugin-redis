//%attributes = {"invisible":true,"preemptive":"incapable"}
var $result; $options; $badOption : Object
var $host; $prefix; $key; $unicode; $delta; $password; $caFile : Text
var $connection; $closed; $port : Integer
var $keys; $badOptions; $badDeltas; $badExpiries : Collection
var $blob; $actual : Blob

$host:=Get environment variable("REDIS_TEST_HOST")
If ($host="")
	$host:="127.0.0.1"
End if
$port:=Num(Get environment variable("REDIS_TEST_PORT"))
ASSERT(($port>0) && ($port<=65535))
$options:=New object("host"; $host; "port"; $port; "database"; 1; "clientName"; "4d-plugin-redis-tests")
$password:=Get environment variable("REDIS_TEST_PASSWORD")
If ($password#"")
	$options.password:=$password
End if
$caFile:=Get environment variable("REDIS_TEST_TLS_CA")
If ($caFile#"")
	$options.tls:=True
	$options.caFile:=$caFile
End if
$result:=REDIS Open($options)
ASSERT($result.ok)
$connection:=$result.connection
ASSERT($connection>0)

$result:=REDIS Connection info($connection)
ASSERT($result.ok)
test_exact_text($result.host; $host)
ASSERT($result.port=$port)
ASSERT($result.database=1)
ASSERT($result.tls=($caFile#""))
ASSERT($result.connected)
ASSERT($result.connectTimeout=1000)
ASSERT($result.commandTimeout=1000)
ASSERT(Not(OB Is defined($result; "username")))
ASSERT(Not(OB Is defined($result; "password")))
ASSERT(Not(OB Is defined($result; "keyFile")))
ASSERT(Not(OB Is defined($result; "clientName")))
$result:=REDIS Ping($connection)
ASSERT($result.ok)
test_exact_text($result.value; "PONG")

// A UUID namespace prevents collisions without flushing any existing database.
$prefix:="4d-plugin-redis:test:"+Generate UUID+":"
$keys:=New collection($prefix+"text"; $prefix+"blob"; $prefix+"empty"; $prefix+"missing"; $prefix+"conditional"; $prefix+"counter"; $prefix+"utf8")
$result:=REDIS Delete($connection; $keys)
ASSERT($result.ok)
test_exact_text($result.value; "0")

$unicode:="éÉ日本語"+Char(0)+"tail"
$key:=$prefix+"text"
$result:=REDIS Set text($connection; $key; $unicode; New object)
ASSERT($result.ok && $result.applied)
$result:=REDIS Get text($connection; $key)
ASSERT($result.ok && $result.found)
test_exact_text($result.value; $unicode)
$result:=REDIS Exists($connection; New collection($key; $prefix+"missing"))
ASSERT($result.ok)
test_exact_text($result.value; "1")

$result:=REDIS Set text($connection; $prefix+"empty"; ""; New object)
ASSERT($result.ok && $result.applied)
$result:=REDIS Get text($connection; $prefix+"empty")
ASSERT($result.ok && $result.found)
test_exact_text($result.value; "")
$result:=REDIS Get text($connection; $prefix+"missing")
ASSERT($result.ok && Not($result.found))
ASSERT(Not(OB Is defined($result; "value")))

SET BLOB SIZE($blob; 5)
$blob{0}:=0
$blob{1}:=255
$blob{2}:=65
$blob{3}:=0
$blob{4}:=128
$result:=REDIS Set blob($connection; $prefix+"blob"; $blob; New object)
ASSERT($result.ok && $result.applied)
$result:=REDIS Get blob($connection; $prefix+"blob"; $actual)
ASSERT($result.ok && $result.found)
test_same_blob($actual; $blob)
$result:=REDIS Get blob($connection; $prefix+"missing"; $actual)
ASSERT($result.ok && Not($result.found))
ASSERT(BLOB size($actual)=0)
$result:=REDIS Set blob($connection; $prefix+"empty"; $actual; New object)
ASSERT($result.ok && $result.applied)
$result:=REDIS Get blob($connection; $prefix+"empty"; $actual)
ASSERT($result.ok && $result.found)
ASSERT(BLOB size($actual)=0)

$result:=REDIS Set blob($connection; $prefix+"utf8"; $blob; New object)
ASSERT($result.ok && $result.applied)
$result:=REDIS Get text($connection; $prefix+"utf8")
test_error($result; "received"; True)
test_exact_text($result.code; "invalid_utf8")
$result:=REDIS Ping($connection)
ASSERT($result.ok)

$key:=$prefix+"conditional"
$result:=REDIS Set text($connection; $key; "first"; New object("condition"; "XX"))
ASSERT($result.ok && Not($result.applied))
$result:=REDIS Set text($connection; $key; "first"; New object("condition"; "NX"; "ttlMilliseconds"; 60000))
ASSERT($result.ok && $result.applied)
$result:=REDIS TTL($connection; $key)
ASSERT($result.ok)
ASSERT((Num($result.value)>0) && (Num($result.value)<=60000))
$result:=REDIS Set text($connection; $key; "not-applied"; New object("condition"; "NX"; "ttlMilliseconds"; 120000))
ASSERT($result.ok && Not($result.applied))
$result:=REDIS Get text($connection; $key)
ASSERT($result.ok && $result.found)
test_exact_text($result.value; "first")
$result:=REDIS TTL($connection; $key)
ASSERT($result.ok)
ASSERT((Num($result.value)>0) && (Num($result.value)<=60000))
$result:=REDIS Set text($connection; $key; "second"; New object("condition"; "XX"; "ttlMilliseconds"; "120000"))
ASSERT($result.ok && $result.applied)
$result:=REDIS TTL($connection; $key)
ASSERT($result.ok)
ASSERT((Num($result.value)>60000) && (Num($result.value)<=120000))
$result:=REDIS Set text($connection; $key; "third"; New object("keepTTL"; True))
ASSERT($result.ok && $result.applied)
$result:=REDIS TTL($connection; $key)
ASSERT($result.ok)
ASSERT((Num($result.value)>60000) && (Num($result.value)<=120000))
$result:=REDIS Persist($connection; $key)
ASSERT($result.ok && $result.applied)
$result:=REDIS Persist($connection; $key)
ASSERT($result.ok && Not($result.applied))
$result:=REDIS TTL($connection; $key)
ASSERT($result.ok)
test_exact_text($result.value; "-1")
$result:=REDIS Expire($connection; $key; "60000")
ASSERT($result.ok && $result.applied)
$result:=REDIS TTL($connection; $key)
ASSERT($result.ok)
ASSERT((Num($result.value)>0) && (Num($result.value)<=60000))
$result:=REDIS TTL($connection; $prefix+"missing")
ASSERT($result.ok)
test_exact_text($result.value; "-2")
$result:=REDIS Expire($connection; $prefix+"missing"; "1000")
ASSERT($result.ok && Not($result.applied))
$result:=REDIS Persist($connection; $prefix+"missing")
ASSERT($result.ok && Not($result.applied))

$badOptions:=New collection(\
	New object("unexpected"; True); \
	New object("condition"; "nx"); \
	New object("condition"; "bad"); \
	New object("keepTTL"; "true"); \
	New object("ttlMilliseconds"; 0); \
	New object("ttlMilliseconds"; -1); \
	New object("ttlMilliseconds"; 1.5); \
	New object("ttlMilliseconds"; 2147483648); \
	New object("ttlMilliseconds"; "9223372036854775808"); \
	New object("ttlMilliseconds"; 1000; "keepTTL"; True))
For each ($badOption; $badOptions)
	$result:=REDIS Set text($connection; $prefix+"text"; "must not replace"; $badOption)
	test_error($result; "not_submitted"; True)
	$result:=REDIS Set blob($connection; $prefix+"text"; $blob; $badOption)
	test_error($result; "not_submitted"; True)
End for each
$result:=REDIS Set text($connection; $prefix+"text"; "must not replace"; New object("condition"; ""))
test_error($result; "not_submitted"; True)
test_exact_text($result.code; "invalid_arguments")
$result:=REDIS Set blob($connection; $prefix+"text"; $blob; New object("condition"; ""))
test_error($result; "not_submitted"; True)
test_exact_text($result.code; "invalid_arguments")
$result:=REDIS Get text($connection; $prefix+"text")
ASSERT($result.ok && $result.found)
test_exact_text($result.value; $unicode)
$result:=REDIS Delete($connection; New collection($prefix+"text"; 1))
test_error($result; "not_submitted"; True)
$result:=REDIS Exists($connection; New collection($prefix+"text"; 1))
test_error($result; "not_submitted"; True)

$key:=$prefix+"counter"
$badDeltas:=New collection(""; "+1"; "01"; "-0"; "1.5"; " 1"; "1 "; "9223372036854775808"; "-9223372036854775809")
For each ($delta; $badDeltas)
	$result:=REDIS Increment($connection; $key; $delta)
	test_error($result; "not_submitted"; True)
End for each
$result:=REDIS Increment($connection; $key)
test_error($result; "not_submitted"; True)
$badExpiries:=New collection(""; "0"; "-1"; "+1"; "01"; "1.5"; "9223372036854775808")
For each ($delta; $badExpiries)
	$result:=REDIS Expire($connection; $key; $delta)
	test_error($result; "not_submitted"; True)
End for each
$result:=REDIS Increment($connection; $key; "9223372036854775806")
ASSERT($result.ok)
test_exact_text($result.value; "9223372036854775806")
$result:=REDIS Increment($connection; $key; "1")
ASSERT($result.ok)
test_exact_text($result.value; "9223372036854775807")
$result:=REDIS Increment($connection; $key; "1")
test_error($result; "server_error"; True)
$result:=REDIS Get text($connection; $key)
ASSERT($result.ok && $result.found)
test_exact_text($result.value; "9223372036854775807")
$result:=REDIS Set text($connection; $key; "-9223372036854775807"; New object)
ASSERT($result.ok && $result.applied)
$result:=REDIS Increment($connection; $key; "-1")
ASSERT($result.ok)
test_exact_text($result.value; "-9223372036854775808")
$result:=REDIS Increment($connection; $key; "-1")
test_error($result; "server_error"; True)
$result:=REDIS Ping($connection)
ASSERT($result.ok)
test_exact_text($result.value; "PONG")
$result:=REDIS Get text($connection; $key)
ASSERT($result.ok && $result.found)
test_exact_text($result.value; "-9223372036854775808")

$result:=REDIS Delete($connection; $keys)
ASSERT($result.ok)
test_exact_text($result.value; "6")
$result:=REDIS Exists($connection; $keys)
ASSERT($result.ok)
test_exact_text($result.value; "0")
test_worker_lifecycle($options)
$closed:=$connection
$result:=REDIS Close($connection)
ASSERT($result.ok)
$result:=REDIS Ping($closed)
test_error($result; "not_submitted"; False)
$result:=REDIS Close($closed)
test_error($result; "not_submitted"; False)
