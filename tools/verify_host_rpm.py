#!/usr/bin/env python3
"""Build and inspect a native Linux RPM. This is not an Aurora SDK/ABI test."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new isolated RPM work directory")
    parser.add_argument("--profile", choices=["gpu", "cpu"], default="gpu")
    parser.add_argument("--toolchain-root", type=Path, help="optional unpacked RPM tools, containing usr/bin/rpmbuild")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("output already exists")
    environment = os.environ.copy()
    if args.toolchain_root:
        toolchain = args.toolchain_root.resolve()
        environment["RPM_CONFIGDIR"] = str(toolchain / "usr" / "lib" / "rpm")
        environment["LD_LIBRARY_PATH"] = str(toolchain / "usr" / "lib") + ":" + environment.get("LD_LIBRARY_PATH", "")
        rpm = toolchain / "usr" / "bin" / "rpm"
        rpmbuild = toolchain / "usr" / "bin" / "rpmbuild"
    else:
        if not shutil.which("rpmbuild") or not shutil.which("rpm"):
            parser.error("install RPM development tools or provide --toolchain-root")
        rpm, rpmbuild = Path(shutil.which("rpm")), Path(shutil.which("rpmbuild"))
    output.mkdir(parents=True)
    for name in ["SOURCES", "SPECS", "tmp"]:
        (output / name).mkdir()
    subprocess.run([sys.executable, str(ROOT / "tools" / "make_source_archive.py"), "--output", str(output / "SOURCES")], check=True)
    name = "surround-view" + ("-cpu" if args.profile == "cpu" else "")
    spec = output / "SPECS" / (name + ".spec")
    shutil.copyfile(ROOT / "packaging" / "rpm" / spec.name, spec)
    command = [str(rpmbuild), "-bb", "--nodeps", "--load", str(ROOT / "tests" / "rpm_host.macros"),
               "--define", f"_topdir {output}", "--define", f"_tmppath {output / 'tmp'}", str(spec)]
    with (output / "build.log").open("w") as log:
        result = subprocess.run(command, env=environment, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print((output / "build.log").read_text()[-12000:], file=sys.stderr)
        raise SystemExit(result.returncode)
    packages = [package for package in (output / "RPMS").rglob("*.rpm")
                if subprocess.check_output([str(rpm), "-qp", "--queryformat", "%{NAME}", str(package)],
                                           env=environment, text=True) == name]
    if len(packages) != 1:
        raise RuntimeError("expected one main native RPM; debug subpackages are allowed")
    package = packages[0]
    listing = subprocess.check_output([str(rpm), "-qpl", str(package)], env=environment, text=True)
    requirements = subprocess.check_output([str(rpm), "-qp", "--requires", str(package)], env=environment, text=True)
    (output / "files.txt").write_text(listing)
    (output / "requires.txt").write_text(requirements)
    expected = ["sv-project", "sv-core-tests", "sv-platform-test", "sv-calibrate", "sv-capture", "sv-scene"]
    if args.profile == "gpu":
        expected.extend(["sv-server", "sv-bench"])
    for executable in expected:
        if f"/usr/bin/{executable}\n" not in listing:
            raise RuntimeError(f"missing executable in RPM: {executable}")
    if any(token in listing for token in ["/.git/", "/build/", "/artifacts/", "__pycache__"]):
        raise RuntimeError("private/build files leaked into RPM")
    print(f"native {args.profile} RPM verified: {package}\nPayload listing and dependencies: {output}")


if __name__ == "__main__":
    main()
