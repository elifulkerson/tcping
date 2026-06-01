#!/usr/bin/env python3
"""Smoke tests for tcping.

Validates the documented exit-code contract on whatever platform the build
produced a binary for: 0 when all probes succeed, 1 when none succeed.  The
test stands up its own TCP listener so it needs nothing but Python 3.

Usage:
    python smoke_test.py <path-to-tcping-binary>
"""

import socket
import subprocess
import sys
import threading


failures = []


def run(binary, args):
    """Run the binary, returning (exit_code, combined_output)."""
    proc = subprocess.run(
        [binary] + args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=30,
    )
    return proc.returncode, proc.stdout.decode(errors="replace")


def free_port():
    """Grab a port the OS hands out, then release it so it's (almost certainly)
    closed when we probe it a moment later."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start_listener():
    """Start a background TCP listener that accepts and immediately closes
    connections.  Returns (server_socket, port, stop_event)."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(16)
    srv.settimeout(0.5)
    port = srv.getsockname()[1]
    stop = threading.Event()

    def serve():
        while not stop.is_set():
            try:
                conn, _ = srv.accept()
                conn.close()
            except socket.timeout:
                continue
            except OSError:
                break

    threading.Thread(target=serve, daemon=True).start()
    return srv, port, stop


def check(name, condition, detail=""):
    print("[{}] {}{}".format(
        "PASS" if condition else "FAIL",
        name,
        " -- {}".format(detail) if detail else "",
    ))
    if not condition:
        failures.append(name)


def main():
    if len(sys.argv) < 2:
        print("usage: smoke_test.py <tcping-binary>")
        return 2
    binary = sys.argv[1]
    print("Testing binary: {}\n".format(binary))

    # 1. Open port -> all probes succeed -> exit 0.
    srv, open_port, stop = start_listener()
    try:
        rc, out = run(binary, ["-n", "3", "-i", "0", "-w", "2",
                               "127.0.0.1", str(open_port)])
        check("open port: exit code is 0", rc == 0, "rc={}".format(rc))
        check("open port: reports 'Port is open'", "Port is open" in out)
    finally:
        stop.set()
        srv.close()

    # 2. Closed port -> no probe succeeds -> exit 1.
    closed_port = free_port()
    rc, out = run(binary, ["-n", "2", "-i", "0", "-w", "2", "-g", "1",
                           "127.0.0.1", str(closed_port)])
    check("closed port: exit code is 1", rc == 1, "rc={}".format(rc))
    check("closed port: reports '0 successful'", "0 successful" in out)

    print()
    if failures:
        print("{} check(s) failed: {}".format(len(failures), ", ".join(failures)))
        return 1
    print("All smoke tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
