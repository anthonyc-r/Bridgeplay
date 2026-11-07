#!/usr/bin/env python3
"""Check an Intel profile against Steam files without loading or modifying them."""

import argparse
import bisect
import hashlib
import json
from pathlib import Path
import struct
import uuid


class MachO:
    def __init__(self, path):
        whole = path.read_bytes()
        self.file_hash = hashlib.sha256(whole).hexdigest()
        self.data = whole
        if whole[:4] == b"\xca\xfe\xba\xbe":
            count = struct.unpack_from(">I", whole, 4)[0]
            for index in range(count):
                cpu, _, offset, size, _ = struct.unpack_from(
                    ">IIIII", whole, 8 + 20 * index
                )
                if cpu == 0x01000007:
                    self.data = whole[offset:offset + size]
                    break
            else:
                raise ValueError(f"{path}: no x86_64 slice")
        magic, cpu, _, _, commands, _, _, _ = struct.unpack_from("<8I", self.data)
        if magic != 0xFEEDFACF or cpu != 0x01000007:
            raise ValueError(f"{path}: not x86_64 Mach-O")
        self.slice_hash = hashlib.sha256(self.data).hexdigest()
        self.segments = {}
        self.sections = {}
        self.uuid = None
        function_info = None
        position = 32
        for _ in range(commands):
            command, size = struct.unpack_from("<II", self.data, position)
            if command == 0x19:
                fields = struct.unpack_from("<II16sQQQQiiII", self.data, position)
                _, _, name, va, vm_size, offset, file_size, _, _, count, _ = fields
                name = name.rstrip(b"\0").decode()
                self.segments[name] = (va, vm_size, offset, file_size)
                for index in range(count):
                    name, _, va, size, offset = struct.unpack_from(
                        "<16s16sQQI", self.data, position + 72 + 80 * index
                    )
                    self.sections[name.rstrip(b"\0").decode()] = (va, size, offset)
                # Section size is unrelated to the load-command size.
                size = fields[1]
            elif command == 0x26:
                function_info = struct.unpack_from("<II", self.data, position + 8)
            elif command == 0x1B:
                self.uuid = str(uuid.UUID(bytes=self.data[position + 8:position + 24]))
            position += size
        if function_info is None or self.uuid is None:
            raise ValueError(f"{path}: missing function starts or UUID")
        self.functions = []
        position, length = function_info
        end = position + length
        address = self.segments["__TEXT"][0]
        while position < end:
            delta = shift = 0
            while True:
                byte = self.data[position]
                position += 1
                delta |= (byte & 0x7F) << shift
                shift += 7
                if byte < 0x80:
                    break
            if delta == 0:
                break
            address += delta
            self.functions.append(address)


def check(profile_path, steam_path):
    profile = json.loads(profile_path.read_text())
    if profile["arch"] != "x86_64":
        raise ValueError("profile must target x86_64")
    images = {}
    for module, expected_uuid in profile["module_uuids"].items():
        image = MachO(steam_path / module)
        if image.uuid != expected_uuid:
            raise ValueError(f"{module}: UUID changed; profile needs revalidation")
        if image.slice_hash != profile["module_slice_sha256"][module]:
            raise ValueError(f"{module}: Intel slice SHA256 mismatch")
        images[module] = image
        print(f"{module}: UUID {image.uuid}, Intel slice SHA256 verified")

    for signature in profile["signatures"]:
        image = images[signature.get("module", "steamclient.dylib")]
        name = signature["name"]
        if signature["anchor"] != {"kind": "aob"}:
            raise ValueError(f"{name}: this checker requires an exact AOB profile")
        pattern = bytes.fromhex(signature["aob_hex"])
        va, _, offset, size = image.segments["__TEXT"]
        text = image.data[offset:offset + size]
        if not pattern or text.count(pattern) != 1:
            raise ValueError(f"{name}: pattern is not unique in __TEXT")
        site = va + text.index(pattern) - signature.get("match_offset", 0)
        if site != int(signature["func_addr_this_build"], 16):
            raise ValueError(f"{name}: pattern resolves to an unexpected address")
        index = bisect.bisect_right(image.functions, site) - 1
        start, end = image.functions[index:index + 2]
        if (start != int(signature["function_start"], 16)
                or end != int(signature["function_end"], 16)):
            raise ValueError(f"{name}: LC_FUNCTION_STARTS bounds differ")
        if signature["site_kind"] == "function" and site != start:
            raise ValueError(f"{name}: hook is not at a function entry")
        code_va, code_size, _ = image.sections["__text"]
        if not code_va <= site < code_va + code_size:
            raise ValueError(f"{name}: hook is outside executable instructions")
        print(f"  {site:#x}: {name}")
    print(f"PASS: {len(profile['signatures'])} unique signatures; Steam files unchanged")


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path,
                        default=root / "signatures/macos.x86_64/1792618285.json")
    parser.add_argument("--steam", type=Path, default=Path.home() /
                        "Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS")
    args = parser.parse_args()
    try:
        check(args.profile, args.steam)
    except (OSError, ValueError, KeyError, IndexError, struct.error) as error:
        parser.exit(1, f"intel-signatures: {error}\n")


if __name__ == "__main__":
    main()
