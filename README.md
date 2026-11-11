# 4d-plugin-redis

A native, host-installed Redis client for classic 4D. Individual typed commands
cover a standalone cache endpoint; no PHP, Redis CLI process, automatic ORDA
integration, or replacement of 4D's internal cache.

[日本語](README.ja.md) · [Dependency decision and provenance](docs/dependencies.md)

**Development preview, version 0.1.0. No release has been published.** The build
targets macOS universal (arm64/x86_64, deployment target 11.0) and Windows x64.
See [validation](#validation) for the distinction between targets and tested
platforms. The supported 4D floor is deliberately **4D 21.0**, not a claim that
older versions cannot load the SDK. The protocol feature floor is Redis
**6.0** (`HELLO 3`, `SET KEEPTTL`); actual server validation is currently
**Redis 7.2.9 only**, with no Valkey qualification. Redis Cluster and Sentinel
are not supported.

**Synchronous and cooperative-only:** a native network wait can still pause the
UI even when called from a cooperative worker.

## Installation

Build from source, then copy `redis.bundle` (macOS), or the complete `redis`
folder containing `Contents/Windows64/redis.4DX` (Windows), into the host
application's `Plugins` folder. Restart 4D. Do not copy just the executable.
For client/server use, install on each host that actually executes the commands.
Handles belong to that runtime, not to the database or a remote client.

There are no release downloads yet. Do not treat a locally built or CI test-only
ad-hoc-signed bundle as a signed/notarized distribution.

## Quick start

```4d
var $r; $cache; $value; $closed : Object
var $connection : Integer

$cache:=REDIS Open(New object("host"; "127.0.0.1"; "port"; 6379; \
    "connectTimeout"; 1000; "commandTimeout"; 500))
If ($cache.ok)
    $connection:=$cache.connection
    // NX and expiry are part of the same atomic Redis SET.
    $r:=REDIS Set text($connection; "example:greeting"; "Hello"; \
        New object("condition"; "NX"; "ttlMilliseconds"; 60000))
    If ($r.ok)
        $value:=REDIS Get text($connection; "example:greeting")
        If ($value.ok)
            If ($value.found)
                // $value.value is Text; an empty value is not a cache miss.
            End if
        End if
    End if
    $closed:=REDIS Close($connection)
End if
```

Check `ok` **before** interpreting any other result. In application code, handle
errors from each call rather than treating an unsuccessful write as a miss.
Namespace application keys and use a least-privilege Redis ACL.

## Commands

All commands return an **Object**, never a global last-error value. A successful
result starts with `{ok:true}` and has the fields below. The handle is an opaque
positive 32-bit Integer/Longint ID, never a pointer.

| Command | Parameters after connection | Successful result |
|---|---|---|
| `REDIS Open` | Takes only `options : Object` (optional) | `connection : Integer` |
| `REDIS Close` | None | No additional fields |
| `REDIS Connection info` | None | `host`, `port`, `database`, `tls`, `connected`, `connectTimeout`, `commandTimeout` |
| `REDIS Get text` | `key : Text` | `found : Boolean`; `value : Text` only when found |
| `REDIS Set text` | `key : Text; value : Text; options : Object` (optional) | `applied : Boolean` |
| `REDIS Get blob` | `key : Text; value : Blob` (**writable output**) | `found : Boolean`; bytes in the BLOB parameter |
| `REDIS Set blob` | `key : Text; value : Blob; options : Object` (optional) | `applied : Boolean` |
| `REDIS Delete` | `keys : Collection` of Text | `value : Text` decimal count (`DEL`) |
| `REDIS Exists` | `keys : Collection` of Text | `value : Text` decimal count (`EXISTS`) |
| `REDIS Increment` | `key : Text; delta : Text` | `value : Text` exact signed int64 (`INCRBY`) |
| `REDIS Expire` | `key : Text; milliseconds : Text` | `applied : Boolean` (`PEXPIRE`) |
| `REDIS TTL` | `key : Text` | `value : Text` milliseconds; `"-1"` persistent, `"-2"` missing (`PTTL`) |
| `REDIS Persist` | `key : Text` | `applied : Boolean` (`PERSIST`) |
| `REDIS Ping` | None | `value : Text`, `"PONG"` |

Other than Open, the first parameter is always `connection : Integer`.
Delete/Exists require 1..10000 keys; duplicate keys retain Redis's own counting
semantics. Empty Text keys and values are permitted.
Supply every required argument, especially Get blob's writable output. 4D can
substitute typed defaults for omitted plug-in arguments and issue a compiler
warning rather than a hard error; only the trailing options Objects documented
above are intended to be omitted.

`REDIS Connection info` is strictly local: it does **not** ping, reconnect, or
return credentials, TLS paths, username, password, or client name. `connected`
means the connection has not been invalidated locally; it is not proof of
current server liveness. An unusable but not explicitly closed handle reports
`connected:false`.

### Text and BLOBs

Text is encoded as UTF-8 without normalization, including surrogate pairs and
embedded NUL. Ill-formed UTF-16 input is rejected. Get text rejects invalid
UTF-8 with `invalid_utf8` rather than replacing or dropping bytes; use Get blob
for arbitrary bytes. 4D's usual Text comparison is collation-aware, so compare
character codes when testing exact Unicode.

```4d
var $bytes : Blob
var $r : Object
$r:=REDIS Get blob($connection; "image:42"; $bytes)
If ($r.ok)
    If ($r.found)
        // $bytes contains the original bytes, including NUL.
    End if
End if
```

Get blob uses an actual **`&O` BLOB parameter** in the manifest, not a Pointer or
an object property. On a successful miss it clears the output and returns
`found:false`; an existing zero-length value returns `found:true` with an empty
BLOB. On failure it leaves the caller's BLOB unchanged. Set blob takes the
same typed BLOB representation. No base64 encoding is involved.

### Atomic SET options

| Property | Type and meaning |
|---|---|
| `ttlMilliseconds` | Positive integer Number up to 2147483647, or canonical decimal Text up to signed int64 max; emits `PX` |
| `condition` | Exactly `"NX"` (only absent) or `"XX"` (only existing) |
| `keepTTL` | Boolean; `true` emits `KEEPTTL` |

Unknown properties, wrong types, nonpositive TTLs, and `ttlMilliseconds` together
with `keepTTL:true` are rejected **before submission**. A condition not met is
`ok:true, applied:false`, not an error. Without TTL/KEEPTTL, SET removes an old
expiry, following Redis. Redis can reject extremely large TTLs if their
absolute expiry would overflow.

Increment is one `INCRBY`, not GET+SET; expiry on SET is not SET+EXPIRE. Delta is
required, including `"1"` for a simple increment. Exact counter/delta/Expire
values use canonical signed decimal Text (`"0"`, `"-1"`, etc.), with no leading
zeros, plus sign, whitespace, exponent or fraction. Expire requires a positive
value; use Delete for immediate deletion. Signed int64 limits are
`"-9223372036854775808"` and `"9223372036854775807"`. Out-of-range input raises
`integer_overflow`; Redis arithmetic overflow is a server error. There is no
Real/Number counter convenience that could silently round.

## Open settings

| Property | Default / constraints |
|---|---|
| `host` | `"127.0.0.1"`; nonempty DNS name or literal IP, no NUL |
| `port` | `6379`; integer Number 1..65535 |
| `database` | `0`; nonnegative Longint-range integer Number |
| `connectTimeout` | `1000`; integer Number 1..60000 milliseconds |
| `commandTimeout` | `1000`; integer Number 1..60000 milliseconds |
| `username`, `password` | Text; password alone uses ACL user `"default"`; username requires password |
| `clientName` | Optional Text; Redis's naming restrictions apply |
| `tls` | `false`; Boolean |
| `caFile` | Optional PEM trust file, native OS path; replaces imported system roots |
| `certFile`, `keyFile` | Optional paired PEM client identity paths; require TLS |

Unknown properties and null/wrong-type properties are errors. Omitted options
use defaults. TLS files with `tls:false` and an unpaired certificate/key are
errors. Do not put credentials in the host or key names. There is no URI parser.
Use ASCII/punycode DNS names; the plug-in does not perform IDNA conversion.

Open connects, negotiates **RESP3** (`HELLO 3`, optionally `AUTH`), selects the
database, optionally sets the client name, and checks PING before returning a
handle. RESP3 is a deliberate revision from the initial RESP2 draft, matching
the selected library. Push messages remain unsupported: receipt discards the
connection. There are no background PINGs, reconnects, write retries or replay.

TLS requires certificate-chain **and** DNS-name/IP-SAN verification, TLS 1.2 or
newer. DNS hosts send SNI; literal IPs are verified as IPs and do not send SNI.
Without caFile, system root certificates are imported from macOS Security or the
Windows ROOT store into OpenSSL. Verification is OpenSSL's, not native
platform-policy/revocation evaluation. Use caFile for a private CA. Client keys
must be readable without an interactive passphrase; the plug-in never prompts.
There is no insecure/skip-verification option.

## Ownership, timeouts and errors

Keep a handle in the **4D process/worker that opened it**. Every operation and
Close reject cross-owner usage. IDs are not reused during the runtime's life;
exhausting the positive Longint range requires a runtime restart. Handles are
not transferable across clients, servers, runtime restarts or plug-in reloads.
There is no shared pool.

Calls are synchronous. All manifest commands are **`threadSafe:false`**; no
preemptive safety is claimed. Prefer a dedicated cooperative worker for
ownership, short deadlines, and no UI-process cache calls. Cooperative workers
do not make blocking native calls preemptive: UI responsiveness can still be
affected. The library's executor runs only inside native invocations; no
library thread calls the 4D SDK.

`connectTimeout` bounds the complete network setup (DNS, TCP, TLS, Redis setup
and initial PING), not a separate allowance per stage. `commandTimeout` bounds
the complete request/response, including a stalled write/read. These are
practical synchronous deadlines, not hard real-time guarantees: local
certificate/root loading and OS resolver teardown can add overhead. Request
argument bytes and the reply read buffer are limited to 64 MiB. Close never
sends QUIT or waits for a Redis reply.

Always explicitly Close. Unload clears the registry. SDK
`kCreateProcess`/`kCloseProcess` hooks also perform owner cleanup; see validation
for runtime coverage rather than assuming every termination mode is identical.

Failures have this shape:

```json
{"ok":false,"code":"command_timeout","message":"...","outcome":"unknown","connectionUsable":false}
```

| `outcome` | Meaning |
|---|---|
| `not_submitted` | The requested cache operation was not submitted (invalid input/handle/owner, failed Open, or already unusable connection). Open's private session setup may have run. |
| `server_error` | Redis returned an error, not a miss. The stream remains usable. |
| `received` | A reply was consumed, but local conversion/type validation failed. |
| `unknown` | Submission/execution may already have happened; no usable response is available. |

**A timeout or lost response does not mean a write failed to execute. Do not
blindly retry Increment or other mutations.** The plug-in discards transport
state after timeout, disconnect, malformed/unsupported response, or push.
Open a new connection explicitly; automatic reconnect-and-replay is never used.
`connectionUsable` is local state, not a liveness probe; for argument errors it
does not validate the supplied handle. Failed Open never returns a usable
handle. Internal native exceptions discard the current owner's connections.

Codes include `invalid_arguments`, `integer_overflow`, `invalid_handle`,
`wrong_owner`, `tls_configuration`, `open_failed`, `open_timeout`,
`redis_error`, `command_timeout`, `connection_failure`, `connection_closed`,
`unsupported_push`, `unexpected_reply`, `invalid_utf8`, and `sdk_error`.
Server diagnostics are deliberately not echoed, so an endpoint cannot inject
credentials or invalid text into returned/logged diagnostics.
If memory exhaustion prevents even an error Object from being created, the
plug-in logs a fixed diagnostic to standard error and returns Null rather than
letting a C++ exception cross the SDK boundary.

## Scope

No Memcached, Cluster/Sentinel discovery, redirects following, Pub/Sub, MONITOR,
transaction helpers, subscriptions, generic executor, module/custom commands, or
pipeline API. Pipelines are not transactions. The fixed command surface cannot
issue AUTH/SELECT/HELLO/QUIT or change reply modes after Open.

## Build and test

Requires CMake 3.28+, C++20 compiler, Python 3.11+, Perl, and Git. macOS needs
Xcode command-line tools; Windows needs Visual Studio x64 C++ tools with
vswhere. Native TLS fixtures also require an `openssl` command on PATH. The
pinned OpenSSL library itself is built from source, not a Homebrew or PATH
library. See dependency provenance for immutable inputs and licenses.

```sh
git clone --recurse-submodules https://github.com/miyako/4d-plugin-redis.git
cd 4d-plugin-redis
cmake -S redis -B redis/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build redis/build --config Release --parallel
ctest --test-dir redis/build -C Release --output-on-failure
```

Windows configuration: `cmake -S redis -B redis/build -A x64`, then the same
build/ctest commands. The private OpenSSL build enters vcvars64 explicitly,
uses no assembler dependency and static `/MT` CRT, including Debug consumers.

Native tests include exact int64/Unicode/SET argv units and portable socket/TLS
fixtures. They are **not** a Redis-server replacement. For real Redis and
actual manifest/SDK tests:

```sh
python3 scripts/test_redis.py --server /path/to/redis-server \
  --native redis/build/redis_native_tests \
  --tool4d /path/to/tool4d --password disposable-test-only
```

This starts and terminates its own ephemeral Redis, uses database 1 and
namespaced keys, and checks both process status and an exact `PASS` line from
tool4d. Add `--tls` to generate a disposable localhost certificate and test a
TLS-enabled server. Never pass production credentials. Offline tool4d tests can also run:

```sh
/path/to/tool4d --dataless --startup-method=test_all \
  --project="$PWD/redis/redis-test/Project/redis.4DProject"
```

Live 4D tests require `REDIS_TEST_PORT`, with optional `REDIS_TEST_HOST`,
`REDIS_TEST_PASSWORD` and `REDIS_TEST_TLS_CA`. Without the port, only offline
acceptance runs; a PASS in that mode does not imply live Redis validation.

### Validation

Validation evidence and remaining platform/release gates are recorded in
[docs/validation.md](docs/validation.md). 4D 21.0 is a conservative supported
floor based on runtime qualification, not an SDK minimum inference. The newest
language command invoked through the SDK is OB Keys (1719), introduced in
18 R3; this alone does **not** qualify all earlier 4D versions.

## CI/CD and distribution

The canonical pipeline from
[4d-plugin-tutorial](https://github.com/miyako/4d-plugin-tutorial/blob/main/docs/plugin-pipeline.md)
generates exactly **Bump version**, **Build and Test**, and **Build and Release**.
`VERSION` is the only version source; bumping does not publish. Test builds have
no signing secrets and run CTest, packaging contract tests and tool4d offline
acceptance on both target platforms. Live Redis acceptance is a separate,
explicit command above, not silently implied by CI.

`python3 scripts/check_pipeline.py` checks generated-file ownership. To
regenerate, use the canonical generator at the recorded tutorial revision with
`.github/plugin-pipeline.json`; do not hand-edit managed workflows.
Normal macOS/Windows builds statically include Boost/OpenSSL and require only
OS libraries. Combined-package acceptance verifies both architectures,
system-only imports, immutable notices, provenance and same-run artifacts.

Release is manual on main and requires the `release-signing` environment:
`APPLE_DEVELOPER_ID_CERTIFICATE`, `APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD`,
`KEYCHAIN_PASSWORD`, `NOTARYTOOL_APPLE_ID`, `NOTARYTOOL_TEAM_ID`, and
`NOTARYTOOL_PASSWORD` (see the generated
release workflow for the authoritative names). Setup is user-run; agents do
not read or upload credentials. No signing setup or release is part of this
preview. macOS distributions require Developer ID/notarization acceptance;
the Windows distribution is explicitly unsigned.

## License and attribution

MIT, including the 4D SDK's MIT notice in LICENSE. The scaffold and generated
pipeline derive from miyako/4d-plugin-tutorial. Boost.Redis/Boost use BSL-1.0;
OpenSSL uses Apache-2.0. Committed `THIRD-PARTY-NOTICES` files are included by the
canonical package tooling. Redis itself is not bundled.
