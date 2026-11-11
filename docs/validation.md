# Validation and qualification gates

This preview is not a published or signed release. Build targets are not
equivalent to runtime qualification.

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
| TLS fixtures | Server-observed DNS SNI, trusted wrong DNS certificate, missing IP SAN, untrusted chain, required client certificate rejection and mutual-TLS success; encrypted key fails without prompting |
| Session setup fixture | Exact HELLO 3 AUTH argv including NUL in password, SELECT 3 and CLIENT SETNAME; no hidden PING from connection info |
| Generated pipeline | Ownership check and 118 canonical helper/packaging tests pass |

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
ownership/lifecycle. Runtime acceptance must complete before calling this path
qualified.

The supported version policy starts at 4D 21.0, subject to that qualification.
The SDK pin fixes PA_JsonParse's uninitialized-parameter issue. OB Keys (1719),
introduced in 18 R3, is the newest explicitly executed language command;
older SDK availability alone is not evidence that older 4D versions work.

## Remaining gates

Windows x64 compilation, runtime, native trust-store/mutual TLS behavior and PE
import closure must be verified by CI/on Windows. No Windows execution was
performed locally. The source includes the Windows implementation and native
toolchain setup, not a claim of qualified Windows support.

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

Signing credentials have not been accessed or configured. Developer ID signing,
notarization, Gatekeeper, signed ZIP/DMG smoke tests, combined Windows/macOS
package assembly and release publication remain separate acceptance gates.
