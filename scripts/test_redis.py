#!/usr/bin/env python3
"""Run real Redis acceptance against a private, disposable local server."""

import argparse
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, help="redis-server executable (Redis 6+)")
    parser.add_argument("--native", required=True, help="redis_native_tests executable")
    parser.add_argument("--tool4d", help="optional tool4d executable")
    parser.add_argument("--password", default="", help="disposable test password, never production credentials")
    parser.add_argument("--tls", action="store_true", help="generate a disposable localhost TLS identity (server must have TLS)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="4d-redis-real-") as directory:
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        command = [str(Path(args.server).resolve()), "--bind", "127.0.0.1",
                   "--port", str(port), "--save", "", "--appendonly", "no",
                   "--dir", directory, "--protected-mode", "yes"]
        ca = ""
        if args.tls:
            ca = str(Path(directory) / "server.pem")
            key = str(Path(directory) / "server.key")
            subprocess.run(
                ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                 "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                 "-keyout", key, "-out", ca],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            command = [str(Path(args.server).resolve()), "--bind", "127.0.0.1", "--port", "0",
                       "--tls-port", str(port), "--tls-cert-file", ca, "--tls-key-file", key,
                       "--tls-ca-cert-file", ca, "--tls-auth-clients", "no", "--save", "",
                       "--appendonly", "no", "--dir", directory, "--protected-mode", "yes"]
        if args.password:
            command.extend(["--requirepass", args.password])
        with open(Path(directory) / "server.log", "w+") as log:
            server = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            try:
                for _ in range(100):
                    if server.poll() is not None:
                        raise RuntimeError("Redis test server exited before becoming ready")
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=0.1) as sock:
                            if args.tls:
                                sock = ssl.create_default_context(cafile=ca).wrap_socket(sock, server_hostname="localhost")
                            sock.sendall(b"*1\r\n$4\r\nPING\r\n")
                            reply = sock.recv(256)
                            if reply.startswith((b"+PONG", b"-NOAUTH")):
                                break
                    except (ConnectionError, TimeoutError):
                        pass
                    time.sleep(0.05)
                else:
                    raise RuntimeError("Redis test server did not become ready")
                env = dict(os.environ, REDIS_TEST_HOST="localhost" if args.tls else "127.0.0.1",
                           REDIS_TEST_PORT=str(port), REDIS_TEST_PASSWORD=args.password,
                           REDIS_TEST_TLS_CA=ca)
                native = [str(Path(args.native).resolve()), "real", str(port)]
                if env.get("REDIS_TEST_ARCH"):
                    native = ["arch", "-" + env["REDIS_TEST_ARCH"], *native]
                subprocess.run(native,
                               check=True, env=env, timeout=30)
                if args.tool4d:
                    result = subprocess.run(
                        [str(Path(args.tool4d).resolve()), "--dataless", "--startup-method=test_all",
                         f"--project={root / 'redis/redis-test/Project/redis.4DProject'}"],
                        capture_output=True, text=True, env=env, timeout=60)
                    print(result.stdout, end="")
                    if result.returncode or "PASS" not in result.stdout.splitlines():
                        raise RuntimeError(f"tool4d acceptance failed ({result.returncode}):\n{result.stderr}")
                print("PASS real Redis acceptance")
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)


if __name__ == "__main__":
    main()
