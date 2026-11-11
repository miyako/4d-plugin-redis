"""Portable adversarial RESP fixtures; these are not a substitute for real Redis."""

import os
from contextlib import ExitStack
import selectors
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def command(stream):
    line = stream.readline()
    if not line:
        return None
    assert line.startswith(b"*"), line
    parts = []
    for _ in range(int(line[1:])):
        header = stream.readline()
        assert header.startswith(b"$"), header
        length = int(header[1:])
        parts.append(stream.read(length))
        assert stream.read(2) == b"\r\n"
    return parts


def serve(selector, mode, failures, context=None, tls_reject=False):
    try:
        ready = selector.select(timeout=5)
        if not ready:
            raise TimeoutError("Fixture did not receive a connection")
        sock, _ = ready[0][0].fileobj.accept()
        with sock:
            sock.settimeout(4)
            if context is not None:
                try:
                    sock = context.wrap_socket(sock, server_side=True)
                except ssl.SSLError:
                    if tls_reject:
                        return
                    raise
            if tls_reject:
                raise AssertionError("Invalid TLS peer was accepted")
            stream = sock.makefile("rb")
            args = command(stream)
            expected_hello = [b"HELLO", b"3"]
            if mode == "setup":
                expected_hello.extend([b"AUTH", b"cache-user", b"test\0password"])
            assert args == expected_hello, args
            if mode == "open-timeout":
                time.sleep(1.0)
                return
            if mode == "open-fail":
                sock.sendall(b"-NOAUTH Authentication required\r\n")
                return
            sock.sendall(b"%1\r\n+proto\r\n:3\r\n")
            if mode == "setup":
                assert command(stream) == [b"SELECT", b"3"]
                sock.sendall(b"+OK\r\n")
                assert command(stream) == [b"CLIENT", b"SETNAME", b"4d:fixture"]
                sock.sendall(b"+OK\r\n")
            assert command(stream) == [b"PING"]
            sock.sendall(b"+PONG\r\n")
            if mode == "write-timeout":
                time.sleep(0.35)
                return
            args = command(stream)
            if mode in ("timeout", "trickle", "disconnect", "push", "malformed"):
                assert args == [b"SET", b"k", b"v"], args
                if mode == "timeout":
                    time.sleep(0.35)
                elif mode == "trickle":
                    try:
                        for byte in b"+OK\r\n":
                            sock.sendall(bytes([byte]))
                            time.sleep(0.06)
                    except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                        pass
                elif mode == "push":
                    sock.sendall(b">2\r\n+message\r\n+unsupported\r\n")
                elif mode == "malformed":
                    sock.sendall(b"!invalid\r\n")
                return
            assert args == [b"GET", b"missing"], args
            sock.sendall(b"_\r\n")
            assert command(stream) == [b"GET", b"empty"]
            sock.sendall(b"$0\r\n\r\n")
            assert command(stream) == [b"SET", b"binary", b"\x00\xff\r\n", b"PX", b"5000", b"NX"]
            sock.sendall(b"+OK\r\n")
            assert command(stream) == [b"GET", b"binary"]
            sock.sendall(b"$4\r\n\x00\xff\r\n\r\n")
            assert command(stream) == [b"INCRBY", b"overflow", b"1"]
            sock.sendall(b"-ERR increment or decrement would overflow\r\n")
            assert command(stream) == [b"PING"]
            sock.sendall(b"+PONG\r\n")
            assert command(stream) == [b"INCRBY", b"counter", b"1"]
            sock.sendall(b":9223372036854775807\r\n")
            assert command(stream) is None
    except Exception as exc:
        failures.append(exc)


def run_case(mode, context=None, ca=None, host=None, tls_reject=False, identity=None):
    failures = []
    with ExitStack() as stack:
        selector = stack.enter_context(selectors.DefaultSelector())
        addresses = [(socket.AF_INET, "127.0.0.1")]
        if host == "localhost" and any(
            item[0] == socket.AF_INET6
            for item in socket.getaddrinfo("localhost", None, type=socket.SOCK_STREAM)
        ):
            addresses.append((socket.AF_INET6, "::1"))
        port = 0
        for family, address in addresses:
            listener = stack.enter_context(socket.socket(family, socket.SOCK_STREAM))
            if family == socket.AF_INET6:
                listener.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
            listener.bind((address, port))
            listener.listen(1)
            listener.settimeout(5)
            port = listener.getsockname()[1]
            selector.register(listener, selectors.EVENT_READ)
        thread = threading.Thread(target=serve, args=(selector, mode, failures, context, tls_reject))
        thread.start()
        args = [sys.argv[1], mode, str(port)]
        if os.environ.get("REDIS_TEST_ARCH"):
            args = ["arch", "-" + os.environ["REDIS_TEST_ARCH"], *args]
        if ca:
            args.extend([str(ca), host])
        if identity:
            args.extend(map(str, identity))
        result = subprocess.run(args, capture_output=True, text=True, timeout=8)
        thread.join(timeout=5)
        if thread.is_alive() or failures or result.returncode or result.stdout.strip() != "PASS":
            raise RuntimeError(f"{mode}/{host}: {failures} {result.stdout} {result.stderr}")
        endpoint = host or ("system-roots" if context else "plaintext")
        print(f"PASS {mode}/{endpoint}")


def certificate(root, name, hostname="localhost"):
    cert, key = root / f"{name}.pem", root / f"{name}.key"
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
         "-subj", f"/CN={hostname}", "-addext", f"subjectAltName=DNS:{hostname}",
         "-keyout", str(key), "-out", str(cert)],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
    )
    return cert, key


def main():
    for mode in ("success", "setup", "open-fail", "open-timeout", "timeout", "trickle",
                 "write-timeout", "disconnect", "push", "malformed"):
        run_case(mode)
    with tempfile.TemporaryDirectory(prefix="4d-redis-tls-") as directory:
        root = Path(directory)
        cert, key = certificate(root, "server")
        untrusted, _ = certificate(root, "other")
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        names = []
        context.set_servername_callback(lambda sock, name, ctx: names.append(name))
        run_case("success", context, cert, "localhost")
        assert names == ["localhost"], names
        names.clear()
        run_case("open-fail", context, cert, "127.0.0.1", tls_reject=True)
        assert names == [None], names
        run_case("open-fail", context, untrusted, "localhost", tls_reject=True)
        run_case("open-fail-system", context, tls_reject=True)
        wrong_cert, wrong_key = certificate(root, "wrong-host", "wrong.invalid")
        wrong_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        wrong_context.load_cert_chain(wrong_cert, wrong_key)
        run_case("open-fail", wrong_context, wrong_cert, "localhost", tls_reject=True)
        context.verify_mode = ssl.CERT_REQUIRED
        context.load_verify_locations(cafile=cert)
        run_case("open-fail", context, cert, "localhost", tls_reject=True)
        run_case("success", context, cert, "localhost", identity=(cert, key))
        encrypted = root / "encrypted.key"
        subprocess.run(["openssl", "pkey", "-in", str(key), "-aes-256-cbc",
                        "-passout", "pass:disposable-test-only", "-out", str(encrypted)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        result = subprocess.run(
            [sys.argv[1], "tls-config-fail", "9", str(cert), "localhost", str(cert), str(encrypted)],
            capture_output=True, text=True, timeout=5)
        assert result.returncode == 0 and result.stdout.strip() == "PASS", (result.stdout, result.stderr)
        assert "pass phrase" not in result.stderr.lower(), result.stderr
        print("PASS TLS SNI, DNS/IP identity, untrusted chain, mutual TLS and no passphrase prompt")


if __name__ == "__main__":
    main()
