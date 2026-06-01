# tcping Modernization Plan

Goal: bring `tcping` to a modern, maintainable, **cross-platform** (Linux / macOS / Windows)
codebase **without a big-bang rewrite**. Each phase is small, independently shippable, and
keeps the Windows build working at every step. Bug/security fixes come first; the
cross-platform port is introduced as an *additive* compatibility layer, not a rewrite.

Guiding principles:
- Targeted, low-risk changes. No restructuring beyond what portability requires.
- Windows keeps building (and behaving identically) after every phase.
- Each phase ends in a commit/PR that compiles and runs.

---

## Phase 0 — Repo hygiene (no code changes)   ✅ done

Low risk, immediate. Stop tracking generated/IDE files and document the build.

- [ ] Add a `.gitignore` covering: `*.suo`, `*.user`, `*.obj`, `*.o`, `*.exe`,
      `*/Release/`, `*/Debug/`, `*/x64/`, `*.log`, `*.swp`, `*.bak`, CMake `build/`.
- [ ] `git rm --cached` the committed cruft: `master.v11.suo`, `master.v12.suo`,
      `*.vcxproj.user`, `tcping-32/Release/*.log`, `tcping-64/x64/Release/*.log`.
- [ ] Add a real `README.md` (currently there is none) summarizing what the tool does,
      how to build, and usage — reuse content from `usage.txt`.

**Done when:** `git status` is clean of build artifacts and a fresh clone has no IDE/binary junk.

---

## Phase 1 — Targeted correctness & security fixes (still Windows/MSVC)   ✅ done

These are bug fixes, not refactors. They make the later port safer and fix real defects.

1. **Format-string vulnerabilities (highest severity).**
   - `tee::p()` calls `printf(text)` — change to `printf("%s", text)`. The logged string
     (hostnames, URLs, HTTP responses) must never be a format string.
   - `sprintf_s(proxy_server, sizeof(...), argv[x+1])` and the matching
     `proxy_credentials` line in `main.cpp` use user input as the format string — change to
     `"%s", argv[x+1]`.
   - Audit every `sprintf*`/`pf` call where the format arg is data, not a literal.

2. **Winsock version.** `WSAStartup(MAKEWORD(1,1), ...)` → `MAKEWORD(2,2)`. The code already
   relies on Winsock 2.x APIs (`getaddrinfo`, IPv6).

3. **`ReadReply()` hardening.** Null-terminate the receive buffer; NULL-check the results of
   `strchr` before dereferencing so a non-HTTP / short reply can't crash the process.

4. **Argument-parser bounds checks.** Every flag that reads `argv[x+1]` must verify
   `x+1 < argc` first. Today a trailing flag causes an out-of-bounds read.

5. **`const`-correct string literals.** `char* pcHost = ""` and `char* src_address = ""`
   assign literals to non-const pointers (ill-formed in modern C++); the proxy path could
   write into a read-only literal. Use `const char*` and a separate writable buffer where a
   write is actually needed.

6. **Deprecated `<strstream>`.** Replace `ostrstream`/`acErrorBuffer` in `ws-util.cpp` with
   `std::ostringstream`/`std::string`. (Removed from strict modern toolchains; required for
   the cross-platform compilers anyway.)

7. **DNS relookup leak.** Call `freeaddrinfo()` before each re-`getaddrinfo()` in the relookup
   path (the leak is already flagged in a code comment).

8. **Line-buffer truncation.** `tee::pf` uses a fixed `char buffer[256]`; long lines silently
   truncate. Size from `vsnprintf`'s return value (or use `std::string`).

**Done when:** no format-string sinks remain, the tool can't crash on a hostile/empty reply,
and it still builds and behaves identically on Windows.

> **Follow-up found during Phase 1 — RESOLVED during Phase 4:** in `DoWinsock()` proxy
> mode, `sprintf(pcHost, "%s", proxy_server)` wrote the proxy hostname into `pcHost`, which
> points into `argv`; a proxy name longer than the original host argument would overflow.
> Fixed by copying the hostname into a local `host_storage[2048]` buffer at the top of
> `DoWinsock()` and pointing `pcHost` at it, so the function never writes into the caller's
> `argv` and the proxy overwrite is bounded.

---

## Phase 2 — Modern build system (CMake), still Windows-only   ◑ in progress

Replace the VS2012/2013 solution + nmake Makefile with **CMake** so the same build description
works for every compiler/platform later.

- [x] Add `CMakeLists.txt` building one `tcping` target from the five real sources
      (`base64`, `main`, `tcping`, `tee`, `ws-util` — the old `Makefile` was stale).
- [x] On Windows, link `Ws2_32` and reproduce the current settings (static CRT via
      `CMAKE_MSVC_RUNTIME_LIBRARY`, Console subsystem, Unicode defines). 32- vs 64-bit is a
      configure-time choice (`-A Win32` / `-A x64`), so one file replaces both vcxproj.
- [x] Validated that the project **configures** cleanly (on Linux, via CMake 4.x) — confirms
      the build description is well-formed.
- [x] **Verified on Windows via CI** (Phase 4): a clean CMake configure + `--build` produces
      a working `tcping.exe` that passes the smoke test on `windows-latest` (MSVC).
- [ ] Now-unblocked cleanup: the legacy `master.sln`/vcxproj and nmake `Makefile` can be
      removed, since CMake is confirmed equivalent on Windows. (Left for a follow-up so the
      removal is its own reviewable change.)

**Done when:** Windows builds from CMake with no Visual Studio project files required.

---

## Phase 3 — Cross-platform port via a thin compat layer   ✅ done

> Implemented and verified building + running on Linux (g++). `net_compat.h` is the
> compatibility shim. Discovered along the way: the `tee` class collided with POSIX
> `tee()` from `<unistd.h>` (renamed to `Tee`), and removing the Phase-1 double-`printf`
> revealed a stale `%%%%` escape in the stats line (fixed to `%%`). The Windows/MSVC build is
> now confirmed green via CI (Phase 4); no Windows-only code paths were removed — they are
> `#ifdef _WIN32`-guarded.

This is the only structural change, kept deliberately minimal and *additive*. Introduce a small
`net_compat` header that maps the handful of Winsock-isms to their POSIX equivalents, rather
than rewriting the logic.

- [ ] `net_compat.h`: typedef `SOCKET`/`INVALID_SOCKET`/`SOCKET_ERROR`, `closesocket`→`close`,
      `WSAGetLastError`→`errno`, `ioctlsocket`→`fcntl(O_NONBLOCK)`, no-op `WSAStartup/WSACleanup`
      on POSIX, and pull in `<sys/socket.h>`, `<netdb.h>`, `<arpa/inet.h>`, etc.
- [ ] Replace Windows-only pieces in the probe loop with portable equivalents:
  - `QueryPerformanceCounter` timing → `std::chrono::steady_clock`.
  - The **busy-wait connect** (`send()` spin + `Sleep`, `HIGH_PRIORITY_CLASS`,
    `SetThreadAffinityMask`) → a single non-blocking `connect()` + `select()`/`poll()` with the
    `-w` timeout. This is both portable *and* a real improvement (no CPU spin, accurate timeout,
    can distinguish refused vs. timed-out). Keep `--block` behavior via a blocking connect.
  - `SetConsoleCtrlHandler`/control-C → `signal(SIGINT, ...)`.
  - Console color (`SetConsoleTextAttribute`) → ANSI on POSIX; keep Windows codepath.
  - `inet_ntoa`/`getnameinfo` formatting → `inet_ntop` (now available everywhere, including
    modern Windows — the XP caveat in the comments no longer applies).
  - `strcpy_s`/`sprintf_s`/`_strtime_s`/`localtime_s` → portable `snprintf` + `localtime_r`,
    wrapped in the compat header.
- [ ] `#ifdef _WIN32` guards only around the genuinely platform-specific bits.

**Done when:** `tcping` builds and passes a smoke test on Linux (this Kali box), macOS, and
Windows from the same CMake build.

---

## Phase 4 — CI, tests, and release automation   ✅ done (core)

> **CI is green on all three platforms** (Linux, macOS, Windows/MSVC) — the first
> confirmed Windows build of the modernized tree. Remaining bullets (unit tests, release
> automation) stay deferred as noted below.

- [x] GitHub Actions matrix (`.github/workflows/ci.yml`): Linux, macOS, Windows (MSVC) —
      configure + build via CMake on every push / PR.
- [x] Smoke tests (`tests/smoke_test.py`, Python 3, no extra deps): stand up a local TCP
      listener and assert the exit-code contract — open port -> 0, closed port -> 1.
      Validated locally on Linux; the workflow runs the same steps on all three runners.
- [ ] Unit tests for pure logic (jitter math, arg parsing). Deferred — easier after the
      Phase 5 Config-struct refactor makes that logic separable/testable.
- [ ] Tag a `v0.40` release with prebuilt binaries per platform. Deferred (needs the
      "Read and write" workflow permission on the repo).

**Note:** the workflow file does nothing until the branch is pushed to GitHub; the first
real run (and the first automated Windows/MSVC build confirmation) happens there.

**Done when:** every push is built and smoke-tested on all three platforms automatically.

> **First CI run caught two real issues the local Linux build missed:**
> 1. macOS/libc++ resolved unqualified `bind(...)` to `std::bind` (from `<functional>`,
>    visible via `using namespace std;`) instead of the socket call — a hard compile error,
>    and the bare calls silently no-op'd source binding. Fixed by qualifying all three as
>    `::bind`. (libstdc++ happened to pick the socket function, so Linux built fine.)
> 2. The deprecated `sprintf` calls (proxy host + base64 credentials) — converted to
>    bounded `sprintf_s`, which also resolved the proxy-overflow follow-up above.

---

## Phase 5 — Optional future improvements (deferred, your call)

Not part of the targeted-fixes scope; listed so they aren't lost:

- **Config struct refactor.** Collapse the 33 positional parameters on `DoWinsock` /
  `DoWinsock_Single` / `DoWinsock_Multi` into a single `struct Config`. Biggest maintainability
  win, but a larger diff — revisit after the port stabilizes.
- **Machine-readable output** (`--json` / `--csv`) for monitoring/scripting.
- **HTTPS/TLS** support in HTTP mode (e.g. via OpenSSL/schannel), currently plaintext only.
- Split the ~600-line `DoWinsock()` into probe / stats / output helpers.

---

## Suggested order of execution

Phase 0 → 1 → 2 → 3 → 4, each as its own PR. Phase 5 items are independent and optional.
The risky-but-valuable work (Phase 3) lands only after the bugs are fixed (Phase 1) and the
build is reproducible (Phase 2), so regressions are easy to bisect.
