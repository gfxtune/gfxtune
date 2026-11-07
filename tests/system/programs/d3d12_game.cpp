// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// A DirectX 12 test game for the system tests, run under Proton, where
// VKD3D-Proton turns it into Vulkan. It shows a known picture as fast as it
// can and reports how many frames it showed; see pattern.h for its options.
// Built with MinGW.

#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstring>

#include "windows_window.h"

namespace
{

D3D12_RESOURCE_BARRIER transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    return barrier;
}

} // namespace

int main(int argc, char **argv)
{
    GameOptions options;
    if (!parse_options(argc, argv, &options)) {
        return 2;
    }
    HWND window = create_game_window(&options);

    IDXGIFactory4 *factory = nullptr;
    ID3D12Device *device = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory)))
        || FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&device)))) {
        return 1;
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = nullptr;
    device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&queue));

    constexpr UINT BufferCount = 2;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = options.width;
    desc.Height = options.height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = BufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *swapchain1 = nullptr;
    IDXGISwapChain3 *swapchain = nullptr;
    if (FAILED(factory->CreateSwapChainForHwnd(queue, window, &desc, nullptr, nullptr, &swapchain1))
        || FAILED(swapchain1->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&swapchain)))) {
        return 1;
    }
    ID3D12Resource *buffers[BufferCount] = {};
    for (UINT i = 0; i < BufferCount; ++i) {
        swapchain->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&buffers[i]));
    }

    // The picture, once, in an upload buffer laid out as a texture copy wants.
    D3D12_RESOURCE_DESC backDesc = buffers[0]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&backDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *upload = nullptr;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               __uuidof(ID3D12Resource), reinterpret_cast<void **>(&upload)))) {
        return 1;
    }
    {
        uint8_t *pixels = new uint8_t[size_t(options.width) * options.height * 4];
        fill_pattern(&options, pixels, 0);
        uint8_t *mapped = nullptr;
        upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped));
        for (UINT y = 0; y < options.height; ++y) {
            std::memcpy(mapped + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch, pixels + size_t(y) * options.width * 4,
                        size_t(options.width) * 4);
        }
        upload->Unmap(0, nullptr);
        delete[] pixels;
    }

    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void **>(&allocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, __uuidof(ID3D12GraphicsCommandList),
                              reinterpret_cast<void **>(&list));
    list->Close();
    ID3D12Fence *fence = nullptr;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence));
    HANDLE fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    UINT64 fenceValue = 0;

    const double start = seconds_now();
    long frames = 0;
    while (seconds_now() - start < options.seconds) {
        pump_messages();
        ID3D12Resource *back = buffers[swapchain->GetCurrentBackBufferIndex()];
        allocator->Reset();
        list->Reset(allocator, nullptr);
        D3D12_RESOURCE_BARRIER toCopy = transition(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &toCopy);
        D3D12_TEXTURE_COPY_LOCATION target = {};
        target.pResource = back;
        target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = upload;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
        D3D12_RESOURCE_BARRIER toPresent = transition(back, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        list->ResourceBarrier(1, &toPresent);
        list->Close();
        ID3D12CommandList *lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        if (FAILED(swapchain->Present(0, 0))) {
            return 1;
        }
        // One frame in flight: simple, and enough for a test.
        queue->Signal(fence, ++fenceValue);
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
        if (frames++ == 0) {
            write_report(&options, 0, 0);
        }
    }
    write_report(&options, frames, seconds_now() - start);

    CloseHandle(fenceEvent);
    fence->Release();
    list->Release();
    allocator->Release();
    upload->Release();
    for (ID3D12Resource *buffer : buffers) {
        buffer->Release();
    }
    swapchain->Release();
    swapchain1->Release();
    queue->Release();
    device->Release();
    factory->Release();
    DestroyWindow(window);
    return 0;
}
