# CPU verification of DXMT buffer guards

These checks translate the synthetic shaders from `bridge/tests/d3d11-buffer-bounds.hlsl`.
They never create a Metal device or submit GPU work. Export the six `.cso` fixtures
with the probe's `--compile-only` mode as described in `bridge/tests/README.md`.

Build the patched native `airconv` CLI and LLVM 15 `opt`, then run:

```sh
python3 dxmt-patch/tests/verify_buffer_bounds.py \
  --airconv /path/to/airconv --opt /path/to/opt \
  --dxbc-dir /path/to/exported/shaders --out-dir /path/to/results
```

The command checks optimized and unoptimized AIR for Metal 3.1 and 3.2, verifies
LLVM structure, and requires each device-buffer memory operation to remain in a
block reached by a conditional branch. This deliberately strict structural check
may need review if a future optimizer safely changes block naming or layout.

On macOS it also retargets the four synthetic read, write, atomic, and counter
kernels to x86 CPU code. Each runs in five isolated processes: bound resources,
unbound resources, zero-length views, null base pointers with nonzero lengths, and
an unbound output. Buffers end at a protected page to detect out-of-range accesses;
values and untouched regions are checked. Add/subtract AIR atomics use CPU atomic
stand-ins, and compare-exchange uses native LLVM operations. This checks generated
control flow and address handling, not Metal-driver behavior or GPU memory ordering.
The coherent/TGSM fixtures receive IR checks only.

The generated `.cpu.ll` and `.cpu.dylib` files are test artifacts, not Metal shaders.
`--skip-cpu` runs only the IR checks and records the CPU tests as skipped.
No proprietary game shader data is used by these tests.
