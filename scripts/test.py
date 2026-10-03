"""Run hardware-independent C++ checks. Needs a host C++ compiler."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
if not compiler:
    candidates = list((Path.home() / ".platformio" / "packages").glob("toolchain-gccmingw32*/bin/g++.exe"))
    if candidates:
        compiler = str(candidates[0])
if not compiler:
    raise SystemExit("Install g++ or set CXX to a host C++ compiler; tests were not run.")
with tempfile.TemporaryDirectory(prefix="relay-tests-") as tmp:
    executable = Path(tmp) / ("tests.exe" if os.name == "nt" else "tests")
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + environment.get("PATH", "")
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "tests/stubs"), "-I" + str(root / "include"),
                    str(root / "tests/test_control.cpp"), "-o", str(executable)], env=environment, check=True)
    subprocess.run([str(executable)], env=environment, check=True)
