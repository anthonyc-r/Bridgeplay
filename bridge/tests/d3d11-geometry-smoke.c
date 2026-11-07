#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = NULL; } } while (0)
#define SIZE 16

static const char source[] =
    "struct Vertex { float4 color : TEXCOORD0; float4 position : SV_Position; };\n"
    "struct Geometry { float4 color : TEXCOORD0; float4 position : SV_Position; uint layer : SV_RenderTargetArrayIndex; };\n"
    "Vertex vertex_main(uint id : SV_VertexID) {\n"
    "  float2 points[3] = { float2(-0.75,-0.75), float2(0,0.75), float2(0.75,-0.75) };\n"
    "  Vertex v; v.position=float4(points[id],0,1); v.color=float4(1,0,0,1); return v;\n"
    "}\n"
    "[maxvertexcount(3)] void geometry_main(triangle Vertex input[3], inout TriangleStream<Geometry> stream) {\n"
    "  [unroll] for(uint i=0;i<3;++i) { Geometry v; v.position=input[i].position;\n"
    "    v.color=input[i].color.yxzw; v.layer=0; stream.Append(v); }\n"
    "  stream.RestartStrip();\n"
    "}\n"
    "float4 pixel_main(Vertex input) : SV_Target { return input.color; }\n"
    "float4 pixel_constant() : SV_Target { return float4(0,0,1,1); }\n";

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
    size_t size = ID3D10Blob_GetBufferSize(blob);
    const char *data = ID3D10Blob_GetBufferPointer(blob);
    if (strcmp(extension, "asm") == 0 && size && data[size - 1] == '\0') --size;
    int bad = fwrite(data, 1, size, file) != size;
    if (fclose(file)) bad = 1;
    return bad;
}

static int compile_shaders(const char *directory, ID3DBlob **shaders)
{
    static const char *entries[] = { "vertex_main", "geometry_main", "pixel_main", "pixel_constant" };
    static const char *profiles[] = { "vs_5_0", "gs_5_0", "ps_5_0", "ps_5_0" };
    if (directory && !CreateDirectoryA(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 1;
    for (unsigned i = 0; i < 4; ++i) {
        ID3DBlob *errors = NULL, *assembly = NULL;
        HRESULT hr = D3DCompile(source, sizeof(source) - 1, "d3d11-geometry-smoke", NULL, NULL,
            entries[i], profiles[i], D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &shaders[i], &errors);
        if (errors) fwrite(ID3D10Blob_GetBufferPointer(errors), 1, ID3D10Blob_GetBufferSize(errors), stderr);
        RELEASE(errors);
        if (FAILED(hr)) return failed(entries[i], hr);
        if (directory) {
            int bad = write_blob(directory, entries[i], "cso", shaders[i]);
            hr = D3DDisassemble(ID3D10Blob_GetBufferPointer(shaders[i]), ID3D10Blob_GetBufferSize(shaders[i]),
                0, NULL, &assembly);
            if (FAILED(hr)) bad = failed("D3DDisassemble", hr);
            else bad |= write_blob(directory, entries[i], "asm", assembly);
            RELEASE(assembly);
            if (bad) return 1;
        }
        printf("COMPILED %s %s bytes=%lu\n", entries[i], profiles[i],
            (unsigned long)ID3D10Blob_GetBufferSize(shaders[i]));
    }
    return 0;
}

static int await_gpu(ID3D11DeviceContext *context, ID3D11Query *query, ULONGLONG start, DWORD timeout)
{
    for (;;) {
        BOOL done = FALSE;
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

static int draw_triangle(ID3D11DeviceContext *context, ID3D11Texture2D *target, ID3D11Texture2D *staging,
    ID3D11RenderTargetView *rtv, ID3D11Query *query, ID3D11GeometryShader *geometry,
    ID3D11PixelShader *pixel, const char *name, const unsigned char expected[4], DWORD timeout)
{
    printf("RUN %s\n", name);
    ULONGLONG start = GetTickCount64();
    const float black[4] = { 0, 0, 0, 1 };
    ID3D11DeviceContext_ClearRenderTargetView(context, rtv, black);
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, NULL);
    ID3D11DeviceContext_GSSetShader(context, geometry, NULL, 0);
    ID3D11DeviceContext_PSSetShader(context, pixel, NULL, 0);
    ID3D11DeviceContext_Draw(context, 3, 0);
    ID3D11DeviceContext_OMSetRenderTargets(context, 0, NULL, NULL);
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    ID3D11DeviceContext_End(context, (ID3D11Asynchronous *)query);
    ID3D11DeviceContext_Flush(context);
    if (await_gpu(context, query, start, timeout)) return 1;
    D3D11_MAPPED_SUBRESOURCE map;
    for (;;) {
        HRESULT hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0,
            D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
        if (SUCCEEDED(hr)) break;
        if (hr != DXGI_ERROR_WAS_STILL_DRAWING) return failed("Map readback", hr);
        if (GetTickCount64() - start >= timeout) {
            fprintf(stderr, "Readback timed out after %lu ms\n", (unsigned long)timeout);
            return 1;
        }
        Sleep(1);
    }
    const unsigned char *center = (const unsigned char *)map.pData + (SIZE / 2) * map.RowPitch + (SIZE / 2) * 4;
    const unsigned char *corner = map.pData;
    int bad = memcmp(center, expected, 4) != 0 ||
        corner[0] != 0 || corner[1] != 0 || corner[2] != 0 || corner[3] != 255;
    printf("%s %s center=%u,%u,%u,%u corner=%u,%u,%u,%u\n", bad ? "FAIL" : "PASS", name,
        center[0], center[1], center[2], center[3], corner[0], corner[1], corner[2], corner[3]);
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    return bad;
}

static int run(ID3DBlob **shaders, DWORD timeout)
{
    int bad = 1;
    HMODULE library = LoadLibraryA("d3d11.dll");
    if (!library) { fprintf(stderr, "cannot load d3d11.dll: %lu\n", (unsigned long)GetLastError()); return 1; }
    FARPROC address = GetProcAddress(library, "D3D11CreateDevice");
    create_device_fn create_device;
    memcpy(&create_device, &address, sizeof(create_device));
    if (!create_device) { fprintf(stderr, "D3D11CreateDevice missing\n"); FreeLibrary(library); return 1; }
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *target = NULL, *staging = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11VertexShader *vertex = NULL;
    ID3D11GeometryShader *geometry = NULL;
    ID3D11PixelShader *pixel = NULL, *constant_pixel = NULL;
    ID3D11RasterizerState *rasterizer = NULL;
    ID3D11Query *query = NULL;
    D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0, level;
    HRESULT hr = create_device(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &requested, 1,
        D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) { failed("D3D11CreateDevice", hr); goto done; }
    printf("D3D11_GEOMETRY_SMOKE bits=%u feature_level=0x%x\n", (unsigned)(sizeof(void *) * 8), (unsigned)level);
    D3D11_TEXTURE2D_DESC desc = {
        .Width = SIZE, .Height = SIZE, .MipLevels = 1, .ArraySize = 1,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = { 1, 0 },
        .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET,
    };
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &target);
    if (FAILED(hr)) { failed("CreateTexture2D target", hr); goto done; }
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    if (FAILED(hr)) { failed("CreateRenderTargetView", hr); goto done; }
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(hr)) { failed("CreateTexture2D staging", hr); goto done; }
    hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(shaders[0]),
        ID3D10Blob_GetBufferSize(shaders[0]), NULL, &vertex);
    if (FAILED(hr)) { failed("CreateVertexShader", hr); goto done; }
    hr = ID3D11Device_CreateGeometryShader(device, ID3D10Blob_GetBufferPointer(shaders[1]),
        ID3D10Blob_GetBufferSize(shaders[1]), NULL, &geometry);
    if (FAILED(hr)) { failed("CreateGeometryShader", hr); goto done; }
    hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(shaders[2]),
        ID3D10Blob_GetBufferSize(shaders[2]), NULL, &pixel);
    if (FAILED(hr)) { failed("CreatePixelShader", hr); goto done; }
    hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(shaders[3]),
        ID3D10Blob_GetBufferSize(shaders[3]), NULL, &constant_pixel);
    if (FAILED(hr)) { failed("CreatePixelShader constant", hr); goto done; }
    D3D11_RASTERIZER_DESC raster_desc = { .FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE };
    hr = ID3D11Device_CreateRasterizerState(device, &raster_desc, &rasterizer);
    if (FAILED(hr)) { failed("CreateRasterizerState", hr); goto done; }
    D3D11_QUERY_DESC query_desc = { .Query = D3D11_QUERY_EVENT };
    hr = ID3D11Device_CreateQuery(device, &query_desc, &query);
    if (FAILED(hr)) { failed("CreateQuery", hr); goto done; }
    D3D11_VIEWPORT viewport = { .Width = SIZE, .Height = SIZE, .MinDepth = 0, .MaxDepth = 1 };
    ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
    ID3D11DeviceContext_RSSetState(context, rasterizer);
    ID3D11DeviceContext_IASetInputLayout(context, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(context, vertex, NULL, 0);
    const unsigned char red[4] = { 255, 0, 0, 255 }, blue[4] = { 0, 0, 255, 255 }, green[4] = { 0, 255, 0, 255 };
    bad = draw_triangle(context, target, staging, rtv, query, NULL, pixel, "VS_PS", red, timeout);
    if (!bad) bad = draw_triangle(context, target, staging, rtv, query, geometry, constant_pixel,
        "VS_GS_CONSTANT_PS", blue, timeout);
    if (!bad) bad = draw_triangle(context, target, staging, rtv, query, geometry, pixel,
        "VS_GS_PS", green, timeout);
done:
    if (context) ID3D11DeviceContext_ClearState(context);
    RELEASE(query); RELEASE(rasterizer); RELEASE(constant_pixel); RELEASE(pixel); RELEASE(geometry); RELEASE(vertex);
    RELEASE(rtv); RELEASE(staging); RELEASE(target); RELEASE(context); RELEASE(device);
    FreeLibrary(library);
    return bad;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int compile = argc == 3 && strcmp(argv[1], "--compile-only") == 0;
    int execute = (argc == 2 || argc == 3) && strcmp(argv[1], "--run") == 0;
    if (!compile && !execute) {
        fprintf(stderr, "usage: %s --compile-only output-directory\n       %s --run [timeout-ms]\n", argv[0], argv[0]);
        return 2;
    }
    DWORD timeout = 10000;
    if (execute && argc == 3) {
        char *end;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (!*argv[2] || *end || value < 1 || value > 60000) return 2;
        timeout = (DWORD)value;
    }
    ID3DBlob *shaders[4] = {0};
    int bad = compile_shaders(compile ? argv[2] : NULL, shaders);
    if (!bad && execute) {
        bad = run(shaders, timeout);
        if (!bad) puts("BRIDGEPLAY_D3D11_GEOMETRY_OK");
    }
    for (unsigned i = 0; i < 4; ++i) RELEASE(shaders[i]);
    return bad;
}
