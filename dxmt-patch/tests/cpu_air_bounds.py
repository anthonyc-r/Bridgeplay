#!/usr/bin/env python3
"""Run synthetic raw/structured AIR on x86 CPU memory, never through Metal."""
import argparse
import ctypes
import mmap
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
PAGE = mmap.PAGESIZE
CANARY = 0xCAFE1234
LIBC = ctypes.CDLL(None)
LIBC.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_longlong]
LIBC.mmap.restype = ctypes.c_void_p
LIBC.mprotect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
LIBC.mprotect.restype = ctypes.c_int
LIBC.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]


class GuardedBuffer:
    def __init__(self, words):
        self.base = LIBC.mmap(None, PAGE * 3, 0, 0x1002, -1, 0)
        if self.base == ctypes.c_void_p(-1).value:
            raise OSError('mmap failed')
        if LIBC.mprotect(self.base + PAGE, PAGE, 3):
            raise OSError('mprotect failed')
        self.address = self.base + PAGE * 2 - len(words) * 4
        self.length = len(words) * 4
        self.array = (ctypes.c_uint32 * len(words)).from_address(self.address)
        self.array[:] = words

    def values(self):
        return list(self.array)

    def close(self):
        LIBC.munmap(self.base, PAGE * 3)


def layout(ir):
    declaration = re.search(r'^define\s+void\s+@shader_main\((.*?)\)', ir, re.M)
    if not declaration:
        raise ValueError('expected shader_main entry point')
    # All test entry points are thread-independent and need only a binding table.
    # Count arguments from the actual declaration rather than assuming an ABI.
    line = next(line for line in ir.splitlines() if line.startswith('define void @shader_main('))
    arguments = line.split('(', 1)[1].rsplit(')', 1)[0]
    if ',' in arguments or 'addrspace(2)' not in arguments:
        raise ValueError('unsupported CPU probe entry-point ABI: ' + line)
    metadata = next(line for line in ir.splitlines()
                    if '!"air.struct_type_info"' in line and '!"binding_table"' in line)
    node = re.search(r'!"air.struct_type_info", !(\d+)', metadata)[1]
    fields = next(line for line in ir.splitlines() if line.startswith('!' + node + ' = '))
    entries = re.findall(r'i32 (\d+), i32 8, i32 0, !"[^"]+", !"([^"]+)", !"air.indirect_argument"', fields)
    if not entries:
        raise ValueError('no argument-buffer fields found')
    return {name: int(offset) // 8 for offset, name in entries}


def run_case(ir_path, case):
    source = ir_path.read_text()
    fields = layout(source)
    library = ctypes.CDLL(str(ir_path.with_suffix('.cpu.dylib')))
    function = library.shader_main
    function.argtypes = [ctypes.c_void_p]
    function.restype = None
    initial = {
        't0': [0x100 + i for i in range(4)],
        't1': [0x300 + i for i in range(8)],
        'u0': [CANARY] * 80,
        'u1': [0x204 + i for i in range(4)],
        'u2': [0x404 + i for i in range(8)],
        'u3': [CANARY] * 8,
        'u4': [0x500 + i for i in range(8)],
        'counter3': [0],
        'counter4': [2],
    }
    buffers = {name: GuardedBuffer(values) for name, values in initial.items()}
    arguments = (ctypes.c_uint64 * (max(fields.values()) + 1))()
    for name, slot in fields.items():
        if name.startswith('meta_'):
            resource = name[5:]
            arguments[slot] = buffers[resource].length
            if case in ('unbound', 'zero_length') and resource != 'u0':
                arguments[slot] = 0
            if case == 'null_output' and resource == 'u0':
                arguments[slot] = 0
        else:
            arguments[slot] = buffers[name].address
            if case in ('unbound', 'null_nonzero_length') and name != 'u0':
                arguments[slot] = 0
            if case == 'null_output' and name == 'u0':
                arguments[slot] = 0
    function(ctypes.cast(arguments, ctypes.c_void_p))
    inactive = case in ('unbound', 'null_nonzero_length', 'zero_length')
    if ir_path.stem == 'read_buffers':
        expected = []
        for resource in ('t0', 'u1'):
            for offset in (0, 8, 12, 16, 0xFFFFFFFC):
                for component in range(4):
                    index = offset // 4 + component
                    expected.append(initial[resource][index] if not inactive and index < 4 else 0)
        for resource in ('t1', 'u2'):
            for index in (0, 1, 2, 0x10000000):
                expected.extend(initial[resource][index * 4:index * 4 + 4] if not inactive and index < 2 else [0] * 4)
        expected.extend([0] * 8)  # The two 0x40000000 index checks are last.
        assert len(expected) == 80
        if case == 'null_output':
            expected = [CANARY] * 80
        assert buffers['u0'].values() == expected, (case, 'read result mismatch')
        for resource in ('t0', 't1', 'u1', 'u2'):
            assert buffers[resource].values() == initial[resource], (case, resource, 'unexpected mutation')
    elif ir_path.stem == 'write_buffers':
        raw = initial['u1'][:]
        structured = initial['u2'][:]
        if not inactive:
            raw[0], raw[3] = 0xABC00001, 0xABC00002
            structured[:4] = [0xDEF00001 + i for i in range(4)]
        assert buffers['u1'].values() == raw, (case, 'raw store mismatch', buffers['u1'].values(), raw)
        assert buffers['u2'].values() == structured, (case, 'structured store mismatch')
        expected = initial['u0'][:]
        if case != 'null_output':
            expected[0] = 0x600D0001
        assert buffers['u0'].values() == expected, (case, 'completion result mismatch')
    elif ir_path.stem == 'atomic_buffers':
        raw, structured = initial['u1'][:], initial['u2'][:]
        expected = initial['u0'][:]
        if not inactive:
            raw[:2] = [0x207, 0xABC00011]
            structured[:2] = [0x407, 0xDEF00011]
        if case != 'null_output':
            expected[:12] = ([0x204, 0x205, 0x404, 0x405] if not inactive else [0] * 4) + [0] * 8
            expected[12] = 0x600D0002
        assert buffers['u1'].values() == raw, (case, 'raw atomic mutation mismatch')
        assert buffers['u2'].values() == structured, (case, 'structured atomic mutation mismatch')
        assert buffers['u0'].values() == expected, (case, 'atomic result mismatch')
    elif ir_path.stem == 'counter_buffers':
        counters_bound = case not in ('unbound', 'null_nonzero_length')
        appended = initial['u3'][:]
        expected = initial['u0'][:]
        if not inactive:
            appended[:4] = [0xABC00021 + i for i in range(4)]
        if case != 'null_output':
            expected[:4] = initial['u4'][4:] if not inactive else [0] * 4
            expected[4] = 0x600D0003
        assert buffers['counter3'].values() == [1 if counters_bound else 0], (case, 'append counter mismatch')
        assert buffers['counter4'].values() == [1 if counters_bound else 2], (case, 'consume counter mismatch')
        assert buffers['u3'].values() == appended, (case, 'append data mismatch')
        assert buffers['u4'].values() == initial['u4'], (case, 'consume unexpectedly modified data')
        assert buffers['u0'].values() == expected, (case, 'consume result mismatch')
    else:
        raise ValueError('unsupported synthetic entry point')
    for buffer in buffers.values():
        buffer.close()
    print('PASS CPU guard pages:', ir_path.stem, case)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('ir_directory', type=pathlib.Path)
    parser.add_argument('--case')
    parser.add_argument('--entry')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise SystemExit('CPU guard-page tests currently require macOS')
    if args.case:
        run_case(args.ir_directory / (args.entry + '.ll'), args.case)
        return
    for entry in ('read_buffers', 'write_buffers', 'atomic_buffers', 'counter_buffers'):
        ir_path = args.ir_directory / (entry + '.ll')
        layout(ir_path.read_text())
        # This invokes the CPU backend only. The resulting dylib links no Metal APIs.
        cpu_ir = ir_path.with_suffix('.cpu.ll')
        cpu_ir.write_text(ir_path.read_text().replace('@air.atomic.global.add.u.i32', '@cpu_atomic_add')
                          .replace('@air.atomic.global.sub.u.i32', '@cpu_atomic_sub'))
        subprocess.run(['clang', '-dynamiclib', '-Wno-override-module', '-x', 'ir', str(cpu_ir),
                        '-x', 'c', str(ROOT / 'cpu_air_atomics.c'),
                        '-o', str(ir_path.with_suffix('.cpu.dylib'))], check=True)
        for case in ('bound', 'unbound', 'zero_length', 'null_nonzero_length', 'null_output'):
            subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve()), str(args.ir_directory),
                            '--entry', entry, '--case', case], check=True, timeout=15)
    print('PASS: 20 isolated CPU cases; no Metal device or GPU submission')


if __name__ == '__main__':
    main()
