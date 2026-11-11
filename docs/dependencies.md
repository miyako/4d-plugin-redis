# Dependency decision and reproducible inputs

The native client uses **Boost.Redis in Boost 1.92.0**, privately linked with
**OpenSSL 3.5.9**. The C++ standard is **C++20**; Boost.Redis's pinned CMake
requires C++17 or newer. This is native socket/TLS code, not a CLI-backed
component. No dependency thread invokes the 4D SDK.

| Input | Exact identity | License |
|---|---|---|
| Tutorial main | `5ee8f3b9f9f7a9139c4b426411cdd2a185539804` | MIT |
| SDK gitlink | `ad7944bc7a13d93db8097c4c8ff55ba2f6e36f86` | MIT |
| Boost release | `boost-1.92.0`, superproject `afdfa32505af73e3d208144b3f623f0096cb62b6` | BSL-1.0 |
| Boost.Redis within distribution | `5770217136a1fac160afc77a2de99e0384b12767` | BSL-1.0 |
| OpenSSL gitlink | `openssl-3.5.9`, `45e844fa2a14ec92d146bd8f5778ac130b6625fb` | Apache-2.0 |
| Local acceptance server, not shipped | Redis `7.2.9`, `080b99d9827eac9e8a6639922d64b8a8cc5c930c` | BSD-3-Clause |

CMake downloads the complete upstream
[Boost CMake source archive](https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.xz),
verified against SHA256
`9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d`.
It does not mix independently selected Boost libraries or follow floating
branches. SDK/OpenSSL are committed gitlinks, not vendored source snapshots.
Clone with `--recurse-submodules`; the source tree, archive hash and VERSION
are the reproducible build inputs. Compiler/OS versions still affect binary
reproducibility; bit-identical outputs across toolchains are not promised.

OpenSSL is compiled separately for each macOS architecture using the upstream
`darwin64-arm64-cc`/`darwin64-x86_64-cc` configurations, then its static archives
are combined with lipo. Windows uses upstream `VC-WIN64A`, vcvars64, Perl and
nmake. `no-shared no-tests no-apps no-module no-legacy no-asm` avoids runtime
DLLs/providers and an extra NASM requirement. No FIPS validation is claimed.
OpenSSL's pinned configuration selects `/MT` for static library compilation;
the plug-in uses the same CRT.

Boost resolves the private OpenSSL imported targets through a project-generated
CMake config package, scoped to Boost configuration. It does not search for an
already installed OpenSSL: the headers and libraries are intentionally produced
later by ExternalProject. CMake's stock Windows FindOpenSSL module overwrites
the library-path output variables while searching, which previously disabled
Boost.Redis and leaked `_redis_dependencies-NOTFOUND.lib` into the link. A fresh
configuration regression test blocks system discovery and checks configuration
before any private OpenSSL library exists. An incomplete Redis link interface
now fails during configuration, not at link time.

The upstream Boost.Redis CMake interface links all Asio features, including
stackful `spawn` and architecture-specific Boost.Context assembly. This plug-in
uses only callbacks and `Boost::asio_core`, so its CMake narrows the Redis
interface to that target and excludes unused Boost targets from the default
build. No upstream protocol/TLS source is patched. This avoids compiling arm64
assembly as x86_64 in a universal build.

The macOS link exports only the SDK's `FourDPackex` entry point; Windows uses
the SDK export definition. Private OpenSSL symbols are not exposed to the host.
The library is initialized without automatic process-exit registration or
external OpenSSL configuration loading. A lifetime guard is constructed before
the registry finishes initialization, so all connections are destroyed before
private OpenSSL cleanup on module unload. Native loader tests repeatedly
load/unload the module without invoking SDK calls.

## Why not hiredis?

[Hiredis v1.5.0 README](https://github.com/redis/hiredis/blob/5bb7f030f6631b243438cf0fdd3be84352b905bb/README.md)
explicitly calls Windows unsupported/best-effort and warns of known
incompatibilities, including security vulnerabilities. Its Windows CI does
provide native build/test evidence, but does not enable SSL. Binary-safe argv
and its new `verify_name` option are useful, yet do not establish the required
cross-platform support commitment.

[redis-plus-plus 1.3.15](https://github.com/sewenew/redis-plus-plus/tree/a63ac43bf192772910b52e27cd2b42a6098a0071)
inherits hiredis; its TLS wrapper does not set hiredis 1.5's `verify_name`.
Old Windows hiredis forks did not provide stronger maintained support.
These are reasons for choosing Boost, not claims that those libraries cannot
ever be used securely.

At the pinned Redis revision,
[upstream CI](https://github.com/boostorg/redis/actions/runs/31592530333)
has successful MSVC and macOS jobs. The native Windows/macOS CMake jobs do not
run all live Redis tests. This is dependency build evidence, not qualification
of this plug-in on those hosts.

## Explicit transport policies

The pinned implementation's defaults are **not** relied on:

| Area | Plug-in configuration |
|---|---|
| Protocol | Custom setup explicitly sends `HELLO 3`, then optional SELECT/CLIENT SETNAME |
| Reconnect | `reconnect_wait_interval = 0` |
| Health checking | `health_check_interval = 0`, no implicit PING after Open |
| Replay | `cancel_if_unresponded = true`; terminal run failure also cancels queued executions |
| Deadlines | One total setup timer; one total command timer; stage-specific resolve/connect/TLS bounds additionally set |
| Logging | Boost logger disabled; server diagnostics and credentials never echoed |
| Pushes | Pending receive detects unsolicited pushes and discards connection |
| TLS | Peer chain plus DNS/IP verification; TLS >=1.2, no insecure mode |
| SNI | OpenSSL context info callback sets SNI at handshake start; verification also checks it was set |

Boost recreates its SSL stream even before the initial connection, so setting
SNI once on deprecated `next_layer()` would be lost. The context-level callback
uses public OpenSSL APIs and applies to each new SSL object; tests inspect the
server-observed SNI. Reconnect remains disabled.

All response parsing and TLS are implemented by the established libraries.
Only the portable **test fixtures** implement a small RESP peer. Production
has no generic execute surface and accepts only known scalar reply shapes for
each typed operation. Ambiguous failures cancel, drain and discard the
connection, never reuse stale replies or replay requests.

OpenSSL trust roots are imported from platform anchor stores unless caFile is
specified. This is not a promise of native revocation evaluation, enterprise
policy equivalence, or hard-real-time DNS cancellation. These limitations are
documented in README rather than hidden behind success-shaped fallbacks.
