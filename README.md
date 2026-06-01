# tcping

[![CI](https://github.com/elifulkerson/tcping/actions/workflows/ci.yml/badge.svg)](https://github.com/elifulkerson/tcping/actions/workflows/ci.yml)

`tcping` simulates `ping` over TCP. Instead of ICMP, it measures the time to complete a
TCP three-way handshake (SYN → SYN/ACK → ACK) against a remote host and port. The travel
time of the final outgoing ACK is not counted — only the time to put it on the wire at the
near end — so the measured `SYN, SYN/ACK` round trip approximates the ICMP request/response
equivalent. This makes it useful for probing hosts that block ICMP, or for checking whether
a specific TCP service is reachable.

Copyright (C) 2005–2017 Eli Fulkerson. Licensed under the GNU GPL v2 (see `LICENSE`).
Project home: http://www.elifulkerson.com/projects/tcping.php

## Features

- TCP handshake timing ("ping" a host:port without ICMP)
- IPv4 / IPv6 (`-4` / `-6`)
- Continuous or fixed-count probing, configurable interval and timeout
- Jitter statistics, optional rolling average
- HTTP probe mode (GET/HEAD/POST) with response code, size, and throughput
- HTTP proxy support, including basic proxy authentication
- Read targets from a file (`--file`) for multi-host probing
- Mirror output to a log file (`--tee`), optional timestamps, colors, and beeps

## Building

The build uses CMake and works on Linux, macOS, and Windows. A small
compatibility shim (`tcping-src/net_compat.h`) maps the original Winsock calls
onto POSIX sockets off Windows.

### Linux / macOS

```
cmake -B build
cmake --build build
```

The binary is written to `build/tcping`.

### Windows

From a Visual Studio command prompt:

```
cmake -B build -A x64        # or -A Win32 for a 32-bit build
cmake --build build --config Release
```

The executable lands at `build\Release\tcping.exe`. The C runtime is linked
statically, so it runs without the Visual C++ redistributable.

### Legacy build files

The original Visual Studio solution (`master.sln`, configurations `tcping-32` /
`tcping-64`) and the nmake `Makefile` in `tcping-src/` are still included and
continue to work.

## Testing

A small smoke test (Python 3, no extra dependencies) checks the exit-code
contract against a local listener:

```
python tests/smoke_test.py build/tcping        # or build\Release\tcping.exe on Windows
```

The same test runs automatically on Linux, macOS, and Windows via GitHub
Actions (`.github/workflows/ci.yml`) on every push and pull request.

## Usage

```
tcping [-flags] server-address [server-port]
```

If `server-port` is omitted it defaults to `80`. Exit status is `0` when all probes
succeed, `1` when none succeed, and `2` for a mixed result.

### Common options

| Flag | Meaning |
|------|---------|
| `-t` | Probe continuously until stopped with Ctrl-C |
| `-n count` | Send `count` probes (default 4) |
| `-i seconds` | Interval between probes (default 1, decimals allowed) |
| `-w seconds` | Time to wait for a response (default 2, decimals allowed) |
| `-d` | Prefix each line with date and time |
| `-s` | Exit on the first successful probe |
| `-g count` | Give up after `count` consecutive failures |
| `-r count` | Re-resolve the hostname every `count` probes |
| `-j` / `-js depth` | Report jitter (optionally over a rolling window of `depth`) |
| `-b 1..4` | Audible beeps: on-down / on-up / on-change / always |
| `-c` | Only print a line when the up/down state changes |
| `-4` / `-6` | Prefer IPv4 / IPv6 |
| `-S addr` | Bind to source address `addr` |
| `-p port` | Alternate way to specify the port |
| `--file` | Treat `server-address` as a file of targets, one per line |
| `--tee file` | Mirror output to `file` (`--append` to append) |
| `-v` | Print version and exit |

### HTTP mode

| Flag | Meaning |
|------|---------|
| `-h` | HTTP mode (give the URL without `http://`) |
| `-u` | Include the target URL on each line |
| `--get` / `--head` / `--post` | Choose the HTTP method (default GET) |
| `--proxy-server host` | Send the request through a proxy |
| `--proxy-port port` | Proxy port (default 3128) |
| `--proxy-credentials user:pass` | Send a `Proxy-Authorization: Basic` header |

See [`usage.txt`](usage.txt) for the full manual-style reference.

### Examples

```
tcping www.example.com 443        # probe TCP port 443
tcping -t -d 10.0.0.1 22          # continuous, timestamped, port 22
tcping -h www.example.com/        # HTTP GET probe
tcping --file targets.txt         # probe every host listed in targets.txt
```
