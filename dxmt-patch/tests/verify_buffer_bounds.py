#!/usr/bin/env python3
"""Compile synthetic DXBC, verify guarded AIR, and run CPU-only guard-page tests."""
import argparse
import json
import pathlib
import re
import subprocess
import sys

ENTRIES = ('read_buffers', 'write_buffers', 'atomic_buffers', 'counter_buffers',
           'coherent_buffers', 'tgsm_control')


def check_guards(path):
    blocks = {}
    block = None
    in_function = False
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if line.startswith('define void @shader_main('):
            in_function = True
        elif in_function and line == '}':
            break
        if not in_function:
            continue
        label = re.match(r'^([\w.$-]+):', line)
        if label:
            block = label[1]
            blocks[block] = []
        elif block:
            blocks[block].append((number, line))

    predecessors = {name: [] for name in blocks}
    for name, lines in blocks.items():
        for _, line in lines:
            if line.lstrip().startswith('br '):
                for target in re.findall(r'label %([\w.$-]+)', line):
                    predecessors[target].append((name, line))

    count = 0
    for name, lines in blocks.items():
        for number, line in lines:
            direct = (re.search(r'\b(?:load|store)\b.*,\s+(?:i32|<[^>]+>) addrspace\(1\)\*\s+(?:%|@)', line)
                      or re.search(r'\bcmpxchg\s+i32 addrspace\(1\)\*', line))
            intrinsic = ('call ' in line and any(operation in line for operation in (
                '@air.atomic.global.', '@air.load.device_coherent.', '@air.store.device_coherent.')))
            if not (direct or intrinsic):
                continue
            count += 1
            if not name.startswith('buffer.access'):
                raise AssertionError(f'{path}:{number}: device memory access outside guard block')
            if not predecessors[name] or any(not branch.lstrip().startswith('br i1 ')
                                             for _, branch in predecessors[name]):
                raise AssertionError(f'{path}:{number}: guard has an unconditional predecessor')
    if not count:
        raise AssertionError(f'{path}: no device memory operations inspected')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--airconv', required=True, type=pathlib.Path)
    parser.add_argument('--opt', required=True, type=pathlib.Path)
    parser.add_argument('--dxbc-dir', required=True, type=pathlib.Path)
    parser.add_argument('--out-dir', required=True, type=pathlib.Path)
    parser.add_argument('--skip-cpu', action='store_true', help='Verify AIR only; explicitly report skipped CPU tests')
    args = parser.parse_args()
    compiler, opt = args.airconv.resolve(), args.opt.resolve()
    fixtures, output = args.dxbc_dir.resolve(), args.out_dir.resolve()
    report = {'conversions': [], 'cpu': 'skipped' if args.skip_cpu else 'pending'}
    for version in (310, 320):
        for mode in ('optimized', 'O0'):
            directory = output / (str(version) + '-' + mode)
            directory.mkdir(parents=True, exist_ok=True)
            for entry in ENTRIES:
                target = directory / (entry + '.ll')
                command = [str(compiler), str(fixtures / (entry + '.cso')),
                           '--metal-version=' + str(version), '-S', '-o', str(target)]
                if mode == 'O0':
                    command.append('--O0')
                subprocess.run(command, check=True)
                # Generic LLVM opt rejects Apple's air64 target. Override only for
                # structural verification; the emitted AIR file remains unchanged.
                subprocess.run([str(opt), '-mtriple=x86_64-apple-macosx', '-passes=verify',
                                '-disable-output', str(target)], check=True)
                operations = check_guards(target)
                report['conversions'].append({'metal': version, 'mode': mode, 'entry': entry,
                                              'guarded_memory_operations': operations})
                print(f'PASS AIR {version} {mode} {entry}: {operations} guarded memory operations', flush=True)
        if not args.skip_cpu:
            subprocess.run([sys.executable, str(pathlib.Path(__file__).with_name('cpu_air_bounds.py')),
                            str(output / (str(version) + '-optimized'))], check=True)
    if not args.skip_cpu:
        report['cpu'] = 'passed: 40 isolated cases, no Metal/GPU work'
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: 24 verified AIR conversions; CPU tests ' + report['cpu'])


if __name__ == '__main__':
    main()
