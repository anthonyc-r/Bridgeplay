#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

static int failed(const char *step, HRESULT hr)
{
    fprintf(stderr, "%s failed: 0x%08lx\n", step, (unsigned long)hr);
    return 1;
}

static int bgra_volume(ID3D11Device *device, ID3D11DeviceContext *context)
{
    ID3D11Texture3D *target = NULL, *staging = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    int bad = 1;
    D3D11_TEXTURE3D_DESC desc = {
        .Width = 1, .Height = 1, .Depth = 1, .MipLevels = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };
    HRESULT hr = ID3D11Device_CreateTexture3D(device, &desc, NULL, &target);
    if (FAILED(hr)) { failed("BGRA3D CreateTexture3D target", hr); goto done; }
    D3D11_RENDER_TARGET_VIEW_DESC rt_desc = {
        .Format = desc.Format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D,
        .Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = 1 },
    };
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, &rt_desc, &rtv);
    if (FAILED(hr)) { failed("BGRA3D CreateRenderTargetView", hr); goto done; }
    D3D11_SHADER_RESOURCE_VIEW_DESC sr_desc = {
        .Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
        .Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 },
    };
    hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)target, &sr_desc, &srv);
    if (FAILED(hr)) { failed("BGRA3D CreateShaderResourceView", hr); goto done; }
    const float color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    ID3D11DeviceContext_ClearRenderTargetView(context, rtv, color);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture3D(device, &desc, NULL, &staging);
    if (FAILED(hr)) { failed("BGRA3D CreateTexture3D staging", hr); goto done; }
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    D3D11_MAPPED_SUBRESOURCE map;
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) { failed("BGRA3D Map readback", hr); goto done; }
    const unsigned char *pixel = map.pData;
    bad = pixel[0] != 191 || pixel[1] != 128 || pixel[2] != 64 || pixel[3] != 255;
    if (bad) fprintf(stderr, "BGRA3D pixel: %u,%u,%u,%u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
done:
    if (srv) ID3D11ShaderResourceView_Release(srv);
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (staging) ID3D11Texture3D_Release(staging);
    if (target) ID3D11Texture3D_Release(target);
    return bad;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *target = NULL, *staging = NULL;
    ID3D11RenderTargetView *view = NULL;
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        NULL, 0, D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) return failed("D3D11CreateDevice", hr);
    D3D11_TEXTURE2D_DESC desc = {
        .Width = 8, .Height = 8, .MipLevels = 1, .ArraySize = 1,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = { 1, 0 },
        .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET,
    };
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &target);
    if (FAILED(hr)) return failed("CreateTexture2D target", hr);
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &view);
    if (FAILED(hr)) return failed("CreateRenderTargetView", hr);
    const float color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    ID3D11DeviceContext_ClearRenderTargetView(context, view, color);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(hr)) return failed("CreateTexture2D staging", hr);
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    D3D11_MAPPED_SUBRESOURCE map;
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) return failed("Map readback", hr);
    int bad = 0;
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x) {
            unsigned char *pixel = (unsigned char *)map.pData + y * map.RowPitch + x * 4;
            if (pixel[0] != 64 || pixel[1] != 128 || pixel[2] != 191 || pixel[3] != 255) {
                fprintf(stderr, "pixel %u,%u: %u,%u,%u,%u\n", x, y,
                    pixel[0], pixel[1], pixel[2], pixel[3]);
                bad = 1;
            }
        }
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    ID3D11RenderTargetView_Release(view);
    ID3D11Texture2D_Release(target);
    ID3D11Texture2D_Release(staging);
    if (!bad) printf("BRIDGEPLAY_INTEL_D3D11_2D_OK bits=%u feature_level=0x%x pixels=64\n",
        (unsigned)(sizeof(void *) * 8), (unsigned)level);
    bad |= bgra_volume(device, context);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    if (!bad) printf("BRIDGEPLAY_INTEL_D3D11_OK bits=%u feature_level=0x%x pixels=64 bgra3d_pixels=1\n",
        (unsigned)(sizeof(void *) * 8), (unsigned)level);
    return bad;
}
