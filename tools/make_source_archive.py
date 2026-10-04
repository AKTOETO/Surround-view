#!/usr/bin/env python3
"""Archive the committed source, excluding local builds, traces and private files."""

import argparse
import gzip
import io
from pathlib import Path
import re
import subprocess
import tarfile


def git(*arguments):
    return subprocess.check_output(["git", *arguments], cwd=ROOT)


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "sources")
    arguments = parser.parse_args()
    if git("status", "--porcelain", "--untracked-files=normal").strip():
        parser.error("commit the working tree before exporting a release archive")
    version = re.search(r"project\(surround_view VERSION ([0-9.]+)",
                        git("show", "HEAD:CMakeLists.txt").decode()).group(1)
    revision = git("rev-parse", "HEAD").decode().strip()
    prefix = f"surround-view-{version}/"
    destination = arguments.output / f"surround-view-{version}.tar.gz"
    arguments.output.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        parser.error(f"archive already exists: {destination}")
    committed = git("archive", "--format=tar", f"--prefix={prefix}", "HEAD")
    with tarfile.open(fileobj=io.BytesIO(committed)) as source:
        with destination.open("xb") as raw:
            with gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                    for entry in sorted(source.getmembers(), key=lambda value: value.name):
                        entry.uid = entry.gid = entry.mtime = 0
                        entry.uname = entry.gname = ""
                        entry.pax_headers = {}
                        archive.addfile(entry, source.extractfile(entry) if entry.isfile() else None)
                    data = (revision + "\n").encode()
                    entry = tarfile.TarInfo(prefix + ".source-revision")
                    entry.size = len(data)
                    entry.mode = 0o644
                    archive.addfile(entry, io.BytesIO(data))
    print(f"{destination}\nrevision: {revision}")


if __name__ == "__main__":
    main()
