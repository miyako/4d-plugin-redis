# Validation and qualification gates

Preview release [0.1.2](https://github.com/miyako/4d-plugin-redis/releases/tag/v0.1.2)
was published on 2026-11-11 with Developer ID signed/notarized macOS packages
and an unsigned Windows x64 executable. Build targets and successful release
packaging are not equivalent to full runtime qualification.

Local toolchain: macOS 26.7 on Apple Silicon, Apple clang 21.0.0
(`clang-2100.1.1.101`), CMake 4.4.3, C++20 Release.

## Evidence from this implementation

| Surface | Evidence |
|---|---|
| macOS universal build | CMake Release builds both arm64 and x86_64 in the plug-in, native tests and private OpenSSL archives |
| Dynamic dependencies | `otool -L` on both plug-in slices reports only Apple frameworks, libSystem and libc++ |
| Native arm64 | Units, real socket/TLS fixtures, authenticated real Redis 7.2.9 acceptance pass |
| Native x86_64 | Same units, socket/TLS fixtures and real TLS Redis acceptance pass under Rosetta on Apple Silicon; not an Intel-host or 4D x86_64 runtime test |
| Real server | Redis 7.2.9 (`080b99d9827eac9e8a6639922d64b8a8cc5c930c`), disposable loopback instance, password authentication, db 1, nondefault client name; plaintext and TLS paths |
| Portable failure fixtures | No reply, slow/trickling reply, blocked write, disconnect after complete request receipt, malformed response, unsupported push; unusable connections reject subsequent calls and are not replayed |
| TLS fixtures | Server-observed DNS SNI, trusted wrong DNS certificate, missing IP SAN, untrusted chain (explicit CA and platform roots), required client certificate rejection and mutual-TLS success; encrypted key fails without prompting |
| Session setup fixture | Exact HELLO 3 AUTH argv including NUL in password, SELECT 3 and CLIENT SETNAME; no hidden PING from connection info |
| Generated pipeline | Ownership check and 118 canonical helper/packaging tests pass |
| Native module lifetime | Ten load/unload cycles pass; only FourDPackex is exported, private TLS symbols remain hidden |
| 4D arm64 host | tool4d 21.0 build 21.100301 passes offline checks and real authenticated Redis typed text/BLOB commands over plaintext and verified TLS, including worker ownership/termination assertions |
| 4D compiler | `Compile project` with empty targets passes syntax/type checking; one warning is the intentional omitted-delta rejection test |
| Negative runner proof | A temporary ASSERT(False) logs its failure and suppresses PASS despite tool4d exit 0; restored tests pass |
| Windows x64 CI | Windows Server 2025, MSVC 19.51.36260.0: Release plug-in and native executable build; units, adversarial socket/TLS/mutual-TLS fixtures, repeated load/unload, fresh dependency configuration, offline tool4d 21.0 host checks and native package staging pass in [run 43033046195](https://github.com/miyako/4d-plugin-redis/actions/runs/43033046195), source `4ea5d1764fd72c301a309b6416188a2ddeb7f06d` |
| Combined package CI | The same run assembles the Windows/macOS inputs, verifies dependency/provenance closure and ZIP/DMG round trips, and passes the offline tool4d smoke test from the extracted combined plug-in; macOS sealing here is ad-hoc/test-only |
| Signed release 0.1.2 | [Release run 43036770788](https://github.com/miyako/4d-plugin-redis/actions/runs/43036770788), source `3e08ea357da57c85155066366b159cf692e8b2a3`, passes both platform builds/tests, combined assembly, Developer ID/hardened-runtime signature checks, ZIP/DMG notarization, DMG stapling/Gatekeeper assessment, signed-package offline tool4d smoke testing and verified draft creation; both published notarization receipts report `Accepted` |

Native behavior includes nil versus empty replies, NUL/0xff BLOB payloads,
strict UTF-8/UTF-16 including supplementary characters and embedded NUL,
exact signed int64 boundaries, invalid decimals, server overflow/WRONGTYPE
with subsequent connection reuse, NX+PX in one serialized SET, XX+KEEPTTL,
database isolation, and owner/stale-handle rejection.

The portable fixtures deliberately inspect actual bytes and execution
outcomes; they are not described as real Redis integration tests.

## 4D qualification

The available local tool4d is **21.0 build 21.100301, macOS arm64**. The project
compatibilityVersion is 2100 and every command is cooperative (`threadSafe:false`).
The 4D test project covers offline option validation plus live typed commands,
BLOB preservation/clearing, exact Unicode/counters, conditional SET, and worker
ownership/lifecycle. The actual host path, not only the native library, passed
against the dedicated Redis instance.

The supported version policy starts at the qualified 4D 21.0 runtime.
The SDK pin fixes PA_JsonParse's uninitialized-parameter issue. OB Keys (1719),
introduced in 18 R3, is the newest explicitly executed language command;
older SDK availability alone is not evidence that older 4D versions work.

The first live run exposed an SDK BLOB output issue: the legacy
PA_SetBlobVariable/FromUserData path reported `eER_CallNotImplemented` (-1).
The implementation instead stages bytes in an SDK-allocated PA_Handle using
PA_NewHandle/PA_LockHandle, and transfers it through PA_SetBlobHandleParameter
only after the result is constructed. The caller's original BLOB is untouched
until that final commit. A real host round trip of `[0,255,65,0,128]` now passes;
this is not an object BLOB property or Pointer workaround.

The worker test publishes a live worker-owned handle through a shared object,
asserts wrong_owner from the parent, kills the worker, then observes
invalid_handle within a bounded wait. Thus kCloseProcess owner cleanup is
observed for this cooperative worker termination on this runtime. It does not
qualify every 4D process termination mode or any Windows runtime.

## Remaining gates

Windows x64 compilation, native transport/TLS fixtures, offline 4D host checks
and PE dependency validation now have CI evidence. The Windows TLS cases include
explicit-CA success, DNS SNI and identity checks, mutual TLS, and loading platform
roots to reject an untrusted chain. They do not establish trust-policy
equivalence or acceptance against a hosted Redis provider.

Live Redis acceptance through the actual Windows 4D typed text/BLOB API,
including worker termination cleanup, remains untested. Windows CI's tool4d
suite runs offline; it is not the authenticated real-Redis host acceptance run
performed locally on macOS. No Windows execution was performed locally, and
Windows beta deployment still needs this live-host acceptance.

An Intel Mac and an x86_64 4D runtime have not been tested. Rosetta execution
of native tests does not establish those host/runtime combinations. macOS
deployment target 11.0 is a binary build setting, not qualification of 4D 21
on every OS back to 11.0.

Redis 6.0 is the protocol feature floor, but only Redis 7.2.9 was actually
validated. Valkey has not been tested. No Redis Cluster, Sentinel, proxies,
hosted-service SNI routing, server plugins, RESP2 mode or unsolicited-push
support is claimed. Deterministic fixtures prove SNI on the wire, not every
hosted provider's routing behavior.

Automatic cleanup of every forced termination mode, preemptive scheduling and
hard-real-time DNS teardown are not promised. Explicit Close and plug-in unload
cleanup remain required lifecycle paths. Cooperative synchronous network waits
can block the scheduler, even in a worker.

Release 0.1.2 passed production Developer ID signing, notarization, DMG
stapling/Gatekeeper assessment, distribution verification and a signed-package
offline tool4d smoke test in CI. This supersedes the earlier test-only ad-hoc
packaging evidence; it does not qualify every installation/OS combination or
the remaining live Windows host cases. The Windows executable is unsigned.
