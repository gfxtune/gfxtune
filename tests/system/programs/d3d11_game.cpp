// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// A DirectX 11 test game for the system tests, run under Proton, where DXVK
// turns it into Vulkan. It shows a known picture as fast as it can and
// reports how many frames it showed; see pattern.h for its options. Built
// with MinGW.

#include <d3d11.h>

#include <vector>

#include "windows_window.h"

int main(int argc, char **argv)
{
    GameOptions options;
    if (!parse_options(argc, argv, &options)) {
        return 2;
    }
    HWND window = create_game_window(&options);

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = options.width;
    desc.BufferDesc.Height = options.height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain *swapchain = nullptr;
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc,
                                             &swapchain, &device, nullptr, &context))) {
        return 1;
    }

    // The picture, once, in a texture copied to the back buffer every frame.
    std::vector<uint8_t> pixels(size_t(options.width) * options.height * 4);
    fill_pattern(&options, pixels.data(), 0);
    D3D11_TEXTURE2D_DESC texture = {};
    texture.Width = options.width;
    texture.Height = options.height;
    texture.MipLevels = 1;
    texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_IMMUTABLE;
    texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = {pixels.data(), options.width * 4, 0};
    ID3D11Texture2D *picture = nullptr;
    if (FAILED(device->CreateTexture2D(&texture, &data, &picture))) {
        return 1;
    }
    ID3D11Texture2D *back = nullptr;
    swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back));

    const double start = seconds_now();
    long frames = 0;
    while (seconds_now() - start < options.seconds) {
        pump_messages();
        context->CopyResource(back, picture);
        if (FAILED(swapchain->Present(0, 0))) {
            return 1;
        }
        if (frames++ == 0) {
            write_report(&options, 0, 0);
        }
    }
    write_report(&options, frames, seconds_now() - start);
    back->Release();
    picture->Release();
    context->Release();
    device->Release();
    swapchain->Release();
    DestroyWindow(window);
    return 0;
}
