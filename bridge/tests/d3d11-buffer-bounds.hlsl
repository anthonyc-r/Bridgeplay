ByteAddressBuffer rawIn : register(t0);
StructuredBuffer<uint4> structuredIn : register(t1);
RWByteAddressBuffer results : register(u0);
RWByteAddressBuffer rawOut : register(u1);
RWStructuredBuffer<uint4> structuredOut : register(u2);
AppendStructuredBuffer<uint4> appendOut : register(u3);
ConsumeStructuredBuffer<uint4> consumeIn : register(u4);
globallycoherent RWByteAddressBuffer coherentRaw : register(u5);
globallycoherent RWStructuredBuffer<uint4> coherentStructured : register(u6);
groupshared uint localRaw[4];
groupshared uint4 localStructured[2];

[numthreads(1, 1, 1)]
void read_buffers()
{
    results.Store4(0, rawIn.Load4(0));
    results.Store4(16, rawIn.Load4(8));
    results.Store4(32, rawIn.Load4(12));
    results.Store4(48, rawIn.Load4(16));
    results.Store4(64, rawIn.Load4(0xfffffffcu));
    results.Store4(80, rawOut.Load4(0));
    results.Store4(96, rawOut.Load4(8));
    results.Store4(112, rawOut.Load4(12));
    results.Store4(128, rawOut.Load4(16));
    results.Store4(144, rawOut.Load4(0xfffffffcu));
    results.Store4(160, structuredIn[0]);
    results.Store4(176, structuredIn[1]);
    results.Store4(192, structuredIn[2]);
    results.Store4(208, structuredIn[0x10000000u]);
    results.Store4(224, structuredOut[0]);
    results.Store4(240, structuredOut[1]);
    results.Store4(256, structuredOut[2]);
    results.Store4(272, structuredOut[0x10000000u]);
    results.Store4(288, structuredIn[0x40000000u]);
    results.Store4(304, structuredOut[0x40000000u]);
}

[numthreads(1, 1, 1)]
void write_buffers()
{
    // The 16-byte SRV makes this zero without letting FXC fold overlapping stores.
    uint boundaryZero = rawIn.Load(16);
    rawOut.Store(0, 0xabc00001u);
    rawOut.Store4(12 + boundaryZero, uint4(0xabc00002u, 0xabc00003u, 0xabc00004u, 0xabc00005u));
    rawOut.Store4(16, uint4(0xbad00001u, 0xbad00002u, 0xbad00003u, 0xbad00004u));
    rawOut.Store4(0xfffffffcu + boundaryZero, uint4(0xbad10001u, 0xbad10002u, 0xbad10003u, 0xbad10004u));
    structuredOut[0] = uint4(0xdef00001u, 0xdef00002u, 0xdef00003u, 0xdef00004u);
    structuredOut[2] = uint4(0xbad20001u, 0xbad20002u, 0xbad20003u, 0xbad20004u);
    structuredOut[0x10000000u] = uint4(0xbad30001u, 0xbad30002u, 0xbad30003u, 0xbad30004u);
    structuredOut[0x40000000u] = uint4(0xbad40001u, 0xbad40002u, 0xbad40003u, 0xbad40004u);
    results.Store(0, 0x600d0001u);
}

[numthreads(1, 1, 1)]
void atomic_buffers()
{
    uint old;
    rawOut.InterlockedAdd(0, 3, old);
    results.Store(0, old);
    rawOut.InterlockedCompareExchange(4, 0x205u, 0xabc00011u, old);
    results.Store(4, old);
    rawOut.InterlockedAdd(16, 7, old);
    results.Store(16, old);
    rawOut.InterlockedCompareExchange(16, 0x208u, 0xbad00001u, old);
    results.Store(20, old);
    rawOut.InterlockedAdd(0xfffffffcu, 7, old);
    results.Store(24, old);
    rawOut.InterlockedCompareExchange(0xfffffffcu, 0x203u, 0xbad00002u, old);
    results.Store(28, old);
    InterlockedAdd(structuredOut[0].x, 3, old);
    results.Store(8, old);
    InterlockedCompareExchange(structuredOut[0].y, 0x405u, 0xdef00011u, old);
    results.Store(12, old);
    InterlockedAdd(structuredOut[2].x, 7, old);
    results.Store(32, old);
    InterlockedCompareExchange(structuredOut[2].y, 0x40du, 0xbad00003u, old);
    results.Store(36, old);
    InterlockedAdd(structuredOut[0x10000000u].x, 7, old);
    results.Store(40, old);
    InterlockedCompareExchange(structuredOut[0x10000000u].y, 0x405u, 0xbad00004u, old);
    results.Store(44, old);
    InterlockedAdd(structuredOut[2].z, 11);
    results.Store(48, 0x600d0002u);
}

[numthreads(1, 1, 1)]
void counter_buffers()
{
    appendOut.Append(uint4(0xabc00021u, 0xabc00022u, 0xabc00023u, 0xabc00024u));
    results.Store4(0, consumeIn.Consume());
    results.Store(16, 0x600d0003u);
}

// These two entrypoints are exported for compiler inspection, never dispatched here.
[numthreads(1, 1, 1)]
void coherent_buffers()
{
    results.Store4(0, coherentRaw.Load4(12));
    results.Store4(16, coherentStructured[0x40000000u]);
    coherentRaw.Store4(12, uint4(1, 2, 3, 4));
    coherentStructured[2] = uint4(5, 6, 7, 8);
}

[numthreads(1, 1, 1)]
void tgsm_control()
{
    localRaw[0] = 3;
    localStructured[0] = uint4(5, 6, 7, 8);
    GroupMemoryBarrierWithGroupSync();
    uint old;
    InterlockedAdd(localRaw[0], 2, old);
    results.Store(0, old);
    InterlockedCompareExchange(localStructured[0].x, 5, 9, old);
    results.Store(4, old);
    GroupMemoryBarrierWithGroupSync();
    results.Store(8, localRaw[0]);
    results.Store4(16, localStructured[0]);
}
