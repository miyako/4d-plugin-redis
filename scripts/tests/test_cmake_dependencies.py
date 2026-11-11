"""Verify clean configuration resolves project-built OpenSSL, not system discovery."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CMAKE = "cmake"
BOOST_SOURCE = ROOT / "redis/build/_deps/boost-src"


class CMakeDependenciesTests(unittest.TestCase):
    def test_incomplete_redis_interface_fails_during_configuration(self):
        with tempfile.TemporaryDirectory(prefix="dependency-config-", dir=ROOT / "redis/build") as directory:
            root = Path(directory)
            boost = root / "boost"
            boost.mkdir()
            (boost / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\n"
                "project(disabled_boost LANGUAGES NONE)\n"
                "add_library(boost_redis INTERFACE)\n"
                "add_library(Boost::redis ALIAS boost_redis)\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [CMAKE, "-S", str(ROOT / "redis"), "-B", str(root / "build"),
                 f"-DFETCHCONTENT_SOURCE_DIR_BOOST={boost.as_posix()}"],
                capture_output=True, text=True, timeout=150,
            )
            output = result.stdout + result.stderr
            self.assertNotEqual(result.returncode, 0, output)
            self.assertIn("Boost.Redis dependency configuration failed", output)
            self.assertNotIn("_redis_dependencies-NOTFOUND", output)

    def test_fresh_configuration_uses_private_package(self):
        self.assertTrue(BOOST_SOURCE.is_dir(), "Configure the native build first to obtain pinned Boost")
        with tempfile.TemporaryDirectory(prefix="dependency-config-", dir=ROOT / "redis/build") as directory:
            root = Path(directory)
            modules = root / "modules"
            modules.mkdir()
            (modules / "FindOpenSSL.cmake").write_text(
                'message(FATAL_ERROR "System OpenSSL discovery must not run for the private build")\n',
                encoding="utf-8",
            )
            build = root / "build"
            command = [
                CMAKE, "-S", str(ROOT / "redis"), "-B", str(build),
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DFETCHCONTENT_SOURCE_DIR_BOOST={BOOST_SOURCE.as_posix()}",
                f"-DCMAKE_MODULE_PATH={modules.as_posix()}",
            ]
            if os.name == "nt":
                command.extend(["-A", "x64"])
            result = subprocess.run(command, capture_output=True, text=True, timeout=150)
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            self.assertIn("Using project-built OpenSSL 3.5.9", output)
            self.assertNotIn("Boost.Redis has been disabled", output)
            self.assertNotIn("_redis_dependencies-NOTFOUND", output)
            self.assertFalse((build / "openssl/lib").exists(), "Discovery must work before OpenSSL is built")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--cmake", default=CMAKE)
    parser.add_argument("--boost-source", type=Path, default=BOOST_SOURCE)
    options, remaining = parser.parse_known_args()
    CMAKE = options.cmake
    BOOST_SOURCE = options.boost_source
    unittest.main(argv=[__file__, *remaining])
