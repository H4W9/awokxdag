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
C5_FQBN = "esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled"

CLASSIC_FQBN = "esp32:esp32:esp32:FlashSize=4M,PartitionScheme=huge_app,PSRAM=disabled"
PROFILES = {
    "dual-c5-touch": (C5_FQBN, "AWOK_DUAL_C5_TOUCH"),
    "dual-c5-mini": (C5_FQBN, "AWOK_DUAL_C5_MINI"),
    "dual-esp32-touch-v1": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_V1"),
    "dual-esp32-touch-v2": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_V2"),
    "dual-esp32-touch-v3": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_TOUCH_V3"),
    "dual-esp32-mini-v1": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_V1"),
    "dual-esp32-mini-v2": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_V2"),
    "dual-esp32-mini-v3": (CLASSIC_FQBN, "AWOK_DUAL_ESP32_MINI_V3"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", choices=tuple(PROFILES))
    parser.add_argument("--build-path", type=Path)
    args = parser.parse_args()
    fqbn, define = PROFILES[args.board]
    source = (ROOT / "AWOKxDAG/awok_common.h").read_text()
    version = re.search(r'kVersion\[\]\s*=\s*"([^"]+)"', source)
    if not version:
        raise SystemExit("Cannot locate kVersion in awok_common.h")
    version = version.group(1)
    work = (args.build_path or ROOT / "build" / (args.board + "-objects")).resolve()
    output = ROOT / "build" / f"{args.board}-{version}"
    command = ["arduino-cli", "compile", "--fqbn", fqbn, "--warnings", "all",
               "--build-path", str(work)]
    command += ["--build-property", f"compiler.cpp.extra_flags=-D{define}"]
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
        "version": version, "board": args.board, "fqbn": fqbn,
        "experimental": args.board != "dual-c5-touch", "hardware_tested": False,
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
