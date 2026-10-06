#!/usr/bin/env python3
"""Build and inspect a native Linux RPM. This is not an Aurora SDK/ABI test."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new isolated RPM work directory")
    parser.add_argument("--profile", choices=["gpu", "cpu"], default="gpu")
    parser.add_argument("--toolchain-root", type=Path, help="optional unpacked RPM tools, containing usr/bin/rpmbuild")
    parser.add_argument("--inspect-only", action="store_true", help="inspect and smoke-test previously built RPMs without rebuilding")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() and not args.inspect_only:
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
    name = "surround-view" + ("-cpu" if args.profile == "cpu" else "")
    if not args.inspect_only:
        output.mkdir(parents=True)
        for directory in ["SOURCES", "SPECS", "tmp", "rpmdb"]:
            (output / directory).mkdir()
        subprocess.run([sys.executable, str(ROOT / "tools" / "make_source_archive.py"), "--output", str(output / "SOURCES")], check=True)
        spec = output / "SPECS" / (name + ".spec")
        shutil.copyfile(ROOT / "packaging" / "rpm" / spec.name, spec)
        command = [str(rpmbuild), "-bb", "--nodeps", "--load", str(ROOT / "tests" / "rpm_host.macros"),
                   "--define", f"_topdir {output}", "--define", f"_tmppath {output / 'tmp'}",
                   "--define", f"_dbpath {output / 'rpmdb'}", str(spec)]
        with (output / "build.log").open("w") as log:
            result = subprocess.run(command, env=environment, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            print((output / "build.log").read_text()[-12000:], file=sys.stderr)
            raise SystemExit(result.returncode)
    rpm_query = [str(rpm), "--dbpath", str(output / "rpmdb")]
    packages = [package for package in (output / "RPMS").rglob("*.rpm")
                if subprocess.check_output([*rpm_query, "-qp", "--queryformat", "%{NAME}", str(package)],
                                           env=environment, text=True) == name]
    if len(packages) != 1:
        raise RuntimeError("expected one main native RPM; debug subpackages are allowed")
    package = packages[0]
    listing = subprocess.check_output([*rpm_query, "-qpl", str(package)], env=environment, text=True)
    requirements = subprocess.check_output([*rpm_query, "-qp", "--requires", str(package)], env=environment, text=True)
    (output / "files.txt").write_text(listing)
    (output / "requires.txt").write_text(requirements)
    expected = ["sv-client-probe", "sv-project", "sv-core-tests", "sv-platform-test", "sv-calibrate", "sv-capture", "sv-scene"]
    if args.profile == "gpu":
        expected.extend(["sv-server", "sv-bench"])
    for executable in expected:
        if f"/usr/bin/{executable}\n" not in listing:
            raise RuntimeError(f"missing executable in RPM: {executable}")
    for suffix in ["/libsv-client-lib.a", "/libsv-wire.a", "/sv/client.hpp", "/sv/protocol.hpp",
                   "/cmake/svClient/svClientConfig.cmake", "/cmake/svClient/svClientTargets.cmake"]:
        if not any(line.endswith(suffix) for line in listing.splitlines()):
            raise RuntimeError(f"missing client development file: {suffix}")
    if any(token in listing for token in ["/.git/", "/build/", "/artifacts/", "__pycache__"]):
        raise RuntimeError("private/build files leaked into RPM")
    with tempfile.TemporaryDirectory(prefix="installed-smoke-", dir=output) as temporary:
        temporary = Path(temporary)
        payload = temporary / "payload"
        payload.mkdir()
        # Use RPM's own decoder: libarchive may not recognize RPM 6 containers directly.
        archive = temporary / "payload.tar.gz"
        with package.open("rb") as source, archive.open("wb") as destination:
            subprocess.run([str(rpm.parent / "rpm2archive"), "-"], stdin=source, stdout=destination,
                           env=environment, check=True)
        subprocess.run(["bsdtar", "-xf", str(archive), "-C", str(payload)], check=True)
        consumer = temporary / "consumer"
        subprocess.run(["cmake", "-S", str(ROOT / "examples/client"), "-B", str(consumer),
                        f"-DCMAKE_PREFIX_PATH={payload / 'usr'}"], check=True, capture_output=True)
        subprocess.run(["cmake", "--build", str(consumer), "-j", "2"], check=True, capture_output=True)
        dependencies = subprocess.check_output(["ldd", str(consumer / "sv-client-probe")], text=True)
        if any(name in dependencies for name in ["opencv", "libQt", "libEGL", "libGLES"]):
            raise RuntimeError("installed client consumer links GUI/vision/GPU dependencies")
        (output / "CLIENT_CONSUMER.md").write_text(
            "# Installed RPM client consumer\n\nExternal CMake build passed using payload headers, static libraries and CMake exports.\n\n"
            "No Qt/OpenCV/EGL/GLES in consumer ldd.\n\n```text\n" + dependencies + "```\n")
        config = payload / "usr/share/surround-view/configs/synthetic.json"
        subprocess.run([str(payload / "usr/bin/sv-core-tests"), str(config)], cwd=temporary, check=True)
        command = [str(payload / "usr/bin/sv-platform-test"), "--config", str(config), "--output", str(temporary / "report"),
                   "--label", "installed-native-" + args.profile, "--iterations", "8", "--warmup", "2", "--repeats", "1"]
        command += ["--require-gpu", "--egl-platform", "surfaceless"] if args.profile == "gpu" else ["--cpu-only"]
        environment["LIBGL_ALWAYS_SOFTWARE"] = "1"
        subprocess.run(command, cwd=temporary, env=environment, check=True)
        shutil.copyfile(temporary / "report/REPORT.md", output / "INSTALLED_REPORT.md")
        street = payload / "usr/share/surround-view/configs/street-demo.json"
        panorama = payload / "usr/share/surround-view/assets/demo/urban_street_01_1k.hdr"
        subprocess.run([str(payload / "usr/bin/sv-scene"), "--config", str(street), "--panorama", str(panorama),
                        "--output", str(temporary / "street")], cwd=temporary, check=True)
        if args.profile == "gpu":
            subprocess.run([str(payload / "usr/bin/sv-bench"), "--config", str(street), "--manifest", str(temporary / "street/manifest.json"),
                            "--output", str(temporary / "view"), "--iterations", "2", "--warmup", "1", "--egl-platform", "surfaceless"],
                           cwd=temporary, env=environment, check=True, stdout=subprocess.DEVNULL)
    print(f"native {args.profile} RPM verified: {package}\nPayload listing and dependencies: {output}")


if __name__ == "__main__":
    main()
