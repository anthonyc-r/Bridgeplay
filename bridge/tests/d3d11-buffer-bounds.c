#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_WORDS 16
#define RESULT_WORDS 88
#define CANARY 0xcafe1234u
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define RELEASE(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = NULL; } } while (0)

static const char *entries[] = { "read_buffers", "write_buffers", "atomic_buffers", "counter_buffers",
    "coherent_buffers", "tgsm_control" };
#define RUN_CASES 4
typedef HRESULT (WINAPI *create_device_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static int failed(const char *step, HRESULT hr)
{
    fprintf(stderr, "%s failed: 0x%08lx\n", step, (unsigned long)hr);
    return 1;
}

static int write_blob(const char *directory, const char *entry, const char *extension, ID3DBlob *blob)
{
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s.%s", directory, entry, extension) >= (int)sizeof(path)) return 1;
    FILE *file = fopen(path, "wb");
    if (!file) { perror(path); return 1; }
    size_t bytes = ID3D10Blob_GetBufferSize(blob);
    if (strcmp(extension, "asm") == 0 && bytes &&
        ((const char *)ID3D10Blob_GetBufferPointer(blob))[bytes - 1] == '\0') --bytes;
    int bad = fwrite(ID3D10Blob_GetBufferPointer(blob), 1, bytes, file) != bytes;
    if (fclose(file)) bad = 1;
    return bad;
}

static int compile_shaders(const char *path, const char *output, ID3DBlob **shaders)
{
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return 1; }
    if (fseek(file, 0, SEEK_END)) { fclose(file); return 1; }
    long size = ftell(file);
    if (size <= 0 || size > 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return 1; }
    char *source = malloc((size_t)size);
    if (!source) { fclose(file); return 1; }
    int bad = fread(source, 1, (size_t)size, file) != (size_t)size;
    fclose(file);
    if (output && !CreateDirectoryA(output, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) bad = 1;
    for (unsigned i = 0; !bad && i < COUNT(entries); ++i) {
        ID3DBlob *errors = NULL, *assembly = NULL;
        // Preserve the individual boundary operations for translator regression checks.
        HRESULT hr = D3DCompile(source, (size_t)size, path, NULL, NULL, entries[i], "cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &shaders[i], &errors);
        if (errors) fwrite(ID3D10Blob_GetBufferPointer(errors), 1, ID3D10Blob_GetBufferSize(errors), stderr);
        RELEASE(errors);
        if (FAILED(hr)) { bad = failed(entries[i], hr); break; }
        if (output) {
            bad = write_blob(output, entries[i], "cso", shaders[i]);
            hr = D3DDisassemble(ID3D10Blob_GetBufferPointer(shaders[i]), ID3D10Blob_GetBufferSize(shaders[i]),
                0, NULL, &assembly);
            if (FAILED(hr)) bad = failed("D3DDisassemble", hr);
            else bad |= write_blob(output, entries[i], "asm", assembly);
            RELEASE(assembly);
        }
        if (!bad) printf("COMPILED %s cs_5_0 bytes=%lu\n", entries[i], (unsigned long)ID3D10Blob_GetBufferSize(shaders[i]));
    }
    free(source);
    return bad;
}

struct buffer {
    ID3D11Buffer *gpu, *staging;
    ID3D11ShaderResourceView *srv;
    ID3D11UnorderedAccessView *uav;
    unsigned words;
};

static void release_buffer(struct buffer *buffer)
{
    RELEASE(buffer->srv);
    RELEASE(buffer->uav);
    RELEASE(buffer->staging);
    RELEASE(buffer->gpu);
}

static int make_buffer(ID3D11Device *device, struct buffer *buffer, const uint32_t *data,
    unsigned words, unsigned stride, UINT bind, UINT first, UINT count, UINT uav_flags)
{
    buffer->words = words;
    D3D11_BUFFER_DESC desc = {
        .ByteWidth = words * 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind,
        .MiscFlags = stride ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
        .StructureByteStride = stride,
    };
    D3D11_SUBRESOURCE_DATA initial = { .pSysMem = data };
    HRESULT hr = ID3D11Device_CreateBuffer(device, &desc, &initial, &buffer->gpu);
    if (FAILED(hr)) return failed("CreateBuffer", hr);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateBuffer(device, &desc, NULL, &buffer->staging);
    if (FAILED(hr)) return failed("CreateBuffer staging", hr);
    if (bind & D3D11_BIND_SHADER_RESOURCE) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view = {
            .Format = stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS,
            .ViewDimension = stride ? D3D11_SRV_DIMENSION_BUFFER : D3D11_SRV_DIMENSION_BUFFEREX,
            .BufferEx = { .FirstElement = first, .NumElements = count,
                .Flags = stride ? 0 : D3D11_BUFFEREX_SRV_FLAG_RAW },
        };
        hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)buffer->gpu, &view, &buffer->srv);
        if (FAILED(hr)) return failed("CreateShaderResourceView", hr);
    }
    if (bind & D3D11_BIND_UNORDERED_ACCESS) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC view = {
            .Format = stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS,
            .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
            .Buffer = { .FirstElement = first, .NumElements = count,
                .Flags = stride ? uav_flags : D3D11_BUFFER_UAV_FLAG_RAW },
        };
        hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)buffer->gpu, &view, &buffer->uav);
        if (FAILED(hr)) return failed("CreateUnorderedAccessView", hr);
    }
    return 0;
}

static int await_gpu(ID3D11DeviceContext *context, ID3D11Query *query, DWORD timeout)
{
    ULONGLONG start = GetTickCount64();
    BOOL done = FALSE;
    for (;;) {
        HRESULT hr = ID3D11DeviceContext_GetData(context, (ID3D11Asynchronous *)query, &done, sizeof(done), 0);
        if (FAILED(hr)) return failed("GetData", hr);
        if (hr == S_OK && done) return 0;
        if (GetTickCount64() - start >= timeout) {
            fprintf(stderr, "GPU completion timed out after %lu ms\n", (unsigned long)timeout);
            return 1;
        }
        Sleep(1);
    }
}

static int check_buffer(ID3D11DeviceContext *context, struct buffer *buffer, const char *name,
    const uint32_t *expected, const unsigned char *skip)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)buffer->staging, 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr)) return failed("Map completed readback", hr);
    int bad = 0;
    const uint32_t *actual = mapped.pData;
    for (unsigned i = 0; i < buffer->words; ++i) {
        if (skip && skip[i]) continue;
        if (actual[i] != expected[i]) {
            fprintf(stderr, "%s[%u]: want 0x%08lx, got 0x%08lx\n", name, i,
                (unsigned long)expected[i], (unsigned long)actual[i]);
            bad = 1;
        }
    }
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)buffer->staging, 0);
    return bad;
}

static int run_case(ID3D11Device *device, ID3D11DeviceContext *context, ID3DBlob *code,
    unsigned kind, int unbound, DWORD timeout)
{
    enum { RAW_IN, RAW_OUT, STRUCT_IN, STRUCT_OUT, APPEND, CONSUME, RESULTS, BUFFERS };
    struct buffer buffers[BUFFERS] = {0};
    uint32_t data[6][DATA_WORDS], expected[6][DATA_WORDS], output[RESULT_WORDS], want[RESULT_WORDS];
    unsigned char skip[RESULT_WORDS] = {0};
    for (unsigned b = 0; b < 6; ++b)
        for (unsigned i = 0; i < DATA_WORDS; ++i) data[b][i] = expected[b][i] = (b + 1) * 0x100u + i;
    for (unsigned i = 0; i < RESULT_WORDS; ++i) output[i] = want[i] = CANARY;
    ID3D11ComputeShader *shader = NULL;
    ID3D11Query *query = NULL;
    int bad = 1;
    printf("RUN %s %s\n", entries[kind], unbound ? "unbound" : "bound");
    if (make_buffer(device, &buffers[RAW_IN], data[RAW_IN], DATA_WORDS, 0, D3D11_BIND_SHADER_RESOURCE, 4, 4, 0) ||
        make_buffer(device, &buffers[RAW_OUT], data[RAW_OUT], DATA_WORDS, 0, D3D11_BIND_UNORDERED_ACCESS, 4, 4, 0) ||
        make_buffer(device, &buffers[STRUCT_IN], data[STRUCT_IN], DATA_WORDS, 16, D3D11_BIND_SHADER_RESOURCE, 1, 2, 0) ||
        make_buffer(device, &buffers[STRUCT_OUT], data[STRUCT_OUT], DATA_WORDS, 16, D3D11_BIND_UNORDERED_ACCESS, 1, 2, 0) ||
        make_buffer(device, &buffers[APPEND], data[APPEND], DATA_WORDS, 16, D3D11_BIND_UNORDERED_ACCESS, 1, 2, D3D11_BUFFER_UAV_FLAG_APPEND) ||
        make_buffer(device, &buffers[CONSUME], data[CONSUME], DATA_WORDS, 16, D3D11_BIND_UNORDERED_ACCESS, 1, 2, D3D11_BUFFER_UAV_FLAG_APPEND) ||
        make_buffer(device, &buffers[RESULTS], output, RESULT_WORDS, 0, D3D11_BIND_UNORDERED_ACCESS, 0, RESULT_WORDS, 0)) goto done;
    HRESULT hr = ID3D11Device_CreateComputeShader(device, ID3D10Blob_GetBufferPointer(code),
        ID3D10Blob_GetBufferSize(code), NULL, &shader);
    if (FAILED(hr)) { failed("CreateComputeShader", hr); goto done; }
    D3D11_QUERY_DESC query_desc = { .Query = D3D11_QUERY_EVENT };
    hr = ID3D11Device_CreateQuery(device, &query_desc, &query);
    if (FAILED(hr)) { failed("CreateQuery", hr); goto done; }
    ID3D11ShaderResourceView *srvs[] = { unbound ? NULL : buffers[RAW_IN].srv, unbound ? NULL : buffers[STRUCT_IN].srv };
    ID3D11UnorderedAccessView *uavs[] = {
        buffers[RESULTS].uav, unbound ? NULL : buffers[RAW_OUT].uav,
        unbound ? NULL : buffers[STRUCT_OUT].uav, unbound ? NULL : buffers[APPEND].uav,
        unbound ? NULL : buffers[CONSUME].uav,
    };
    UINT counts[] = { ~0u, ~0u, ~0u, 0, 1 };
    ID3D11DeviceContext_CSSetShader(context, shader, NULL, 0);
    ID3D11DeviceContext_CSSetShaderResources(context, 0, COUNT(srvs), srvs);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 0, COUNT(uavs), uavs, counts);
    ID3D11DeviceContext_Dispatch(context, 1, 1, 1);
    memset(srvs, 0, sizeof(srvs));
    memset(uavs, 0, sizeof(uavs));
    ID3D11DeviceContext_CSSetShaderResources(context, 0, COUNT(srvs), srvs);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 0, COUNT(uavs), uavs, NULL);
    if (kind == 3 && !unbound) {
        ID3D11DeviceContext_CopyStructureCount(context, buffers[RESULTS].gpu, 24, buffers[APPEND].uav);
        ID3D11DeviceContext_CopyStructureCount(context, buffers[RESULTS].gpu, 28, buffers[CONSUME].uav);
    }
    for (unsigned i = 0; i < BUFFERS; ++i)
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)buffers[i].staging, (ID3D11Resource *)buffers[i].gpu);
    ID3D11DeviceContext_End(context, (ID3D11Asynchronous *)query);
    ID3D11DeviceContext_Flush(context);
    if (await_gpu(context, query, timeout)) goto done;
    if (kind == 0) {
        for (unsigned i = 0; i < 80; ++i) want[i] = 0;
        if (!unbound) {
            for (unsigned b = 0; b < 2; ++b) {
                unsigned base = b * 20;
                for (unsigned i = 0; i < 4; ++i) want[base + i] = data[b][4 + i];
                want[base + 4] = data[b][6]; want[base + 5] = data[b][7]; want[base + 8] = data[b][7];
            }
            for (unsigned b = 0; b < 2; ++b)
                for (unsigned i = 0; i < 8; ++i) want[40 + b * 16 + i] = data[STRUCT_IN + b][4 + i];
        }
    } else if (kind == 1) {
        want[0] = 0x600d0001u;
        if (!unbound) {
            expected[RAW_OUT][4] = 0xabc00001u;
            expected[RAW_OUT][7] = 0xabc00002u;
            for (unsigned i = 0; i < 4; ++i) expected[STRUCT_OUT][4 + i] = 0xdef00001u + i;
        }
    } else if (kind == 2) {
        for (unsigned i = unbound ? 0 : 4; i < 12; ++i) skip[i] = 1;
        want[12] = 0x600d0002u;
        if (!unbound) {
            want[0] = data[RAW_OUT][4]; want[1] = data[RAW_OUT][5];
            want[2] = data[STRUCT_OUT][4]; want[3] = data[STRUCT_OUT][5];
            expected[RAW_OUT][4] += 3; expected[RAW_OUT][5] = 0xabc00011u;
            expected[STRUCT_OUT][4] += 3; expected[STRUCT_OUT][5] = 0xdef00011u;
        }
    } else {
        for (unsigned i = 0; i < 4; ++i) want[i] = unbound ? 0 : data[CONSUME][4 + i];
        want[4] = 0x600d0003u;
        if (!unbound) {
            for (unsigned i = 0; i < 4; ++i) expected[APPEND][4 + i] = 0xabc00021u + i;
            want[6] = 1; want[7] = 0;
        }
    }
    bad = check_buffer(context, &buffers[RESULTS], "results", want, skip);
    static const char *names[] = { "raw SRV", "raw UAV", "structured SRV", "structured UAV", "append", "consume" };
    for (unsigned i = 0; i < 6; ++i) bad |= check_buffer(context, &buffers[i], names[i], expected[i], NULL);
    if (!bad) printf("PASS %s %s\n", entries[kind], unbound ? "unbound" : "bound");
done:
    ID3D11DeviceContext_ClearState(context);
    RELEASE(query);
    RELEASE(shader);
    for (unsigned i = 0; i < BUFFERS; ++i) release_buffer(&buffers[i]);
    return bad;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int run = argc >= 3 && strcmp(argv[1], "--run") == 0;
    int compile = argc >= 3 && strcmp(argv[1], "--compile-only") == 0;
    if ((!run && !compile) || argc > 4) {
        fprintf(stderr, "usage: %s --compile-only source.hlsl [output-directory]\n"
            "       %s --run source.hlsl [timeout-ms]\n", argv[0], argv[0]);
        return 2;
    }
    DWORD timeout = 10000;
    if (run && argc == 4) {
        char *end;
        unsigned long value = strtoul(argv[3], &end, 10);
        if (!*argv[3] || *end || value < 1 || value > 60000) return 2;
        timeout = (DWORD)value;
    }
    ID3DBlob *shaders[COUNT(entries)] = {0};
    int bad = compile_shaders(argv[2], compile && argc == 4 ? argv[3] : NULL, shaders);
    if (bad || compile) goto done;
    HMODULE library = LoadLibraryA("d3d11.dll");
    if (!library) { fprintf(stderr, "cannot load d3d11.dll: %lu\n", (unsigned long)GetLastError()); bad = 1; goto done; }
    create_device_fn create_device;
    FARPROC address = GetProcAddress(library, "D3D11CreateDevice");
    memcpy(&create_device, &address, sizeof(create_device));
    if (!create_device) { fprintf(stderr, "D3D11CreateDevice missing\n"); bad = 1; FreeLibrary(library); goto done; }
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0, level;
    HRESULT hr = create_device(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &requested, 1,
        D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) bad = failed("D3D11CreateDevice", hr);
    else {
        printf("D3D11_BUFFER_BOUNDS bits=%u feature_level=0x%x\n", (unsigned)(sizeof(void *) * 8), (unsigned)level);
        for (unsigned i = 0; !bad && i < RUN_CASES; ++i)
            for (int unbound = 0; !bad && unbound < 2; ++unbound)
                bad = run_case(device, context, shaders[i], i, unbound, timeout);
    }
    RELEASE(context);
    RELEASE(device);
    FreeLibrary(library);
    if (!bad) puts("BRIDGEPLAY_D3D11_BUFFER_BOUNDS_OK");
done:
    for (unsigned i = 0; i < COUNT(entries); ++i) RELEASE(shaders[i]);
    return bad;
}
