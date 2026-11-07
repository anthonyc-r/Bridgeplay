#!/usr/bin/env python3
"""Copy DXMT and statically linked LLVM notices into a renderer archive."""

import argparse
from pathlib import Path
import re
import shutil


def leading_comments(path: Path) -> str:
    source = path.read_text()
    match = re.match(r"(?:\s|/\*.*?\*/|//[^\n]*(?:\n|$))+", source, re.DOTALL)
    if not match:
        raise ValueError(f"missing license header: {path}")
    return match.group().strip() + "\n"


def package(dxmt: Path, llvm: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for source, name in (
        (dxmt / "COPYING.LIB", "COPYING.LIB"),
        (dxmt / "LICENSE", "LICENSE"),
        (dxmt / "LICENSE.OLD", "LICENSE-0.80-and-earlier.txt"),
        (dxmt / "src/util/dxvk.LICENSE", "LICENSE-DXVK-utilities.txt"),
        (dxmt / "include/native/directx/COPYING.MinGW-w64.txt", "LICENSE-MinGW-headers.txt"),
        (llvm / "LICENSE.TXT", "LICENSE-LLVM.txt"),
        (llvm / "lib/Support/BLAKE3/LICENSE", "LICENSE-LLVM-BLAKE3.txt"),
        (llvm / "lib/Support/COPYRIGHT.regex", "LICENSE-LLVM-regex.txt"),
    ):
        shutil.copyfile(source, output / name)

    notices = ["Additional notices from LLVM 15.0.7's Support library.\n"]
    for relative in (
        "lib/Support/xxhash.cpp",
        "lib/Support/ConvertUTF.cpp",
        "lib/Support/UnicodeNameToCodepointGenerated.cpp",
        "lib/Support/MD5.cpp",
        "lib/Support/SHA1.cpp",
        "lib/Support/SHA256.cpp",
        "lib/Support/regcomp.c",
    ):
        notices.extend((f"\n--- {relative} ---\n\n", leading_comments(llvm / relative)))
    (output / "LICENSE-LLVM-third-party.txt").write_text("".join(notices))

    parser_notice = leading_comments(dxmt / "libs/DXBCParser/ShaderBinary.cpp")
    mit = (dxmt / "LICENSE.OLD").read_text()
    permission = mit[mit.index("Permission is hereby granted"):]
    (output / "LICENSE-DXBCParser.txt").write_text(parser_notice + "\n" + permission)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dxmt", type=Path)
    parser.add_argument("llvm", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    package(args.dxmt, args.llvm, args.output)
