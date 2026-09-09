#!/usr/bin/env python3
"""Compile and package a board-specific image; never opens or flashes a device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FQBN = "esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", choices=("dual-c5-touch", "dual-c5-mini"))
    parser.add_argument("--build-path", type=Path)
    args = parser.parse_args()
    source = (ROOT / "AWOKxDAG/awok_common.h").read_text()
    version = re.search(r'kVersion\[\]\s*=\s*"([^"]+)"', source)
    if not version:
        raise SystemExit("Cannot locate kVersion in awok_common.h")
    version = version.group(1)
    work = (args.build_path or ROOT / "build" / (args.board + "-objects")).resolve()
    output = ROOT / "build" / f"{args.board}-{version}"
    command = ["arduino-cli", "compile", "--fqbn", FQBN, "--warnings", "all",
               "--build-path", str(work)]
    if args.board == "dual-c5-mini":
        command += ["--build-property", "compiler.cpp.extra_flags=-DAWOK_DUAL_C5_MINI"]
    else:
        command += ["--build-property", "compiler.cpp.extra_flags=-DAWOK_DUAL_C5_TOUCH"]
    command += [str(ROOT / "AWOKxDAG/AWOKxDAG.ino")]
    subprocess.run(command, check=True)
    output.mkdir(parents=True, exist_ok=True)
    prefix = f"awokxdag-{version}-{args.board}"
    files = []
    for source_name, suffix in (
        ("AWOKxDAG.ino.merged.bin", "merged.bin"),
        ("AWOKxDAG.ino.bin", "app.bin"),
        ("AWOKxDAG.ino.bootloader.bin", "bootloader.bin"),
        ("AWOKxDAG.ino.partitions.bin", "partitions.bin"),
        ("boot_app0.bin", "boot_app0.bin"),
        ("AWOKxDAG.ino.elf", "elf"),
    ):
        target = output / f"{prefix}-{suffix}"
        shutil.copy2(work / source_name, target)
        files.append(target)
    metadata = output / "build-info.json"
    metadata.write_text(json.dumps({
        "version": version, "board": args.board, "fqbn": FQBN,
        "experimental": args.board == "dual-c5-mini", "hardware_tested": False,
        "compile_command": command,
        "core": subprocess.check_output(["arduino-cli", "core", "list"], text=True),
        "libraries": subprocess.check_output(["arduino-cli", "lib", "list"], text=True),
    }, indent=2) + "\n")
    files.append(metadata)
    (output / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files))
    print(f"Packaged firmware: {output}")


if __name__ == "__main__":
    main()
