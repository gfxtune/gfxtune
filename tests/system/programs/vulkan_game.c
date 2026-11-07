// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// A Vulkan test game for the system tests: an XCB window that shows a known
// picture as fast as it can, and reports how many frames it showed. See
// pattern.h for its options. It copies the picture into each swapchain image
// instead of drawing, so it needs no shaders.

#define _POSIX_C_SOURCE 199309L // clock_gettime
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#include <time.h>

#include "pattern.h"

#define CHECK(call)                                                                                                                        \
    do {                                                                                                                                   \
        VkResult check_result = (call);                                                                                                    \
        if (check_result != VK_SUCCESS) {                                                                                                  \
            fprintf(stderr, "%s failed: %d\n", #call, (int)check_result);                                                                  \
            return 1;                                                                                                                      \
        }                                                                                                                                  \
    } while (0)

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static uint32_t memory_type(VkPhysicalDevice gpu, uint32_t allowed, VkMemoryPropertyFlags wanted)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(gpu, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((allowed & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted) {
            return i;
        }
    }
    return UINT32_MAX;
}

int main(int argc, char **argv)
{
    GameOptions options;
    if (!parse_options(argc, argv, &options)) {
        return 2;
    }

    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) {
        fprintf(stderr, "no X display\n");
        return 1;
    }
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_window_t window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, (uint16_t)options.width, (uint16_t)options.height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, (uint32_t)strlen(options.title),
                        options.title);
    xcb_map_window(connection, window);
    xcb_flush(connection);

    const char *instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "gfxtune test game", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                          .pApplicationInfo = &app,
                                          .enabledExtensionCount = 2,
                                          .ppEnabledExtensionNames = instance_extensions};
    VkInstance instance;
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));

    VkXcbSurfaceCreateInfoKHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR, .connection = connection, .window = window};
    VkSurfaceKHR surface;
    CHECK(vkCreateXcbSurfaceKHR(instance, &surface_info, NULL, &surface));

    uint32_t gpu_count = 1;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    vkEnumeratePhysicalDevices(instance, &gpu_count, &gpu);
    if (!gpu) {
        fprintf(stderr, "no Vulkan device\n");
        return 1;
    }
    uint32_t family = 0;
    VkBool32 can_present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu, family, surface, &can_present);
    if (!can_present) {
        fprintf(stderr, "queue family 0 cannot present\n");
        return 1;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                      .queueCreateInfoCount = 1,
                                      .pQueueCreateInfos = &queue_info,
                                      .enabledExtensionCount = 1,
                                      .ppEnabledExtensionNames = device_extensions};
    VkDevice device;
    CHECK(vkCreateDevice(gpu, &device_info, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);

    // Uncapped: the frame cap tests compare against this.
    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &mode_count, NULL);
    VkPresentModeKHR modes[16];
    mode_count = mode_count > 16 ? 16 : mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &mode_count, modes);
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < mode_count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            mode = modes[i];
        }
    }
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = caps.minImageCount + 1,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = {options.width, options.height},
        .imageArrayLayers = 1,
        // A game draws into its images; copying in is this one's way of drawing.
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = mode,
        .clipped = VK_TRUE,
    };
    if (caps.maxImageCount && swapchain_info.minImageCount > caps.maxImageCount) {
        swapchain_info.minImageCount = caps.maxImageCount;
    }
    VkSwapchainKHR swapchain;
    CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));
    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL);
    VkImage images[8];
    image_count = image_count > 8 ? 8 : image_count;
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, images);

    // The picture, once, in memory the copies read from.
    const VkDeviceSize size = (VkDeviceSize)options.width * options.height * 4;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements needs;
    vkGetBufferMemoryRequirements(device, buffer, &needs);
    VkMemoryAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = needs.size,
        .memoryTypeIndex =
            memory_type(gpu, needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(device, &allocate, NULL, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    void *mapped;
    CHECK(vkMapMemory(device, memory, 0, size, 0, &mapped));
    fill_pattern(&options, mapped, 1);
    vkUnmapMemory(device, memory);

    // One recorded copy per swapchain image.
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBuffer commands[8];
    VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                .commandPool = pool,
                                                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                .commandBufferCount = image_count};
    CHECK(vkAllocateCommandBuffers(device, &command_info, commands));
    for (uint32_t i = 0; i < image_count; ++i) {
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(vkBeginCommandBuffer(commands[i], &begin));
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .image = images[i],
                                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                             &barrier);
        VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                    .imageExtent = {options.width, options.height, 1}};
        vkCmdCopyBufferToImage(commands[i], buffer, images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
                             &barrier);
        CHECK(vkEndCommandBuffer(commands[i]));
    }

    VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    VkSemaphore acquired, drawn;
    VkFence done;
    CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &acquired));
    CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &drawn));
    CHECK(vkCreateFence(device, &fence_info, NULL, &done));

    const double start = now();
    long frames = 0;
    while (now() - start < options.seconds) {
        CHECK(vkWaitForFences(device, 1, &done, VK_TRUE, UINT64_MAX));
        CHECK(vkResetFences(device, 1, &done));
        uint32_t index = 0;
        CHECK(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index));
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                               .waitSemaphoreCount = 1,
                               .pWaitSemaphores = &acquired,
                               .pWaitDstStageMask = &wait_stage,
                               .commandBufferCount = 1,
                               .pCommandBuffers = &commands[index],
                               .signalSemaphoreCount = 1,
                               .pSignalSemaphores = &drawn};
        CHECK(vkQueueSubmit(queue, 1, &submit, done));
        VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                    .waitSemaphoreCount = 1,
                                    .pWaitSemaphores = &drawn,
                                    .swapchainCount = 1,
                                    .pSwapchains = &swapchain,
                                    .pImageIndices = &index};
        const VkResult presented = vkQueuePresentKHR(queue, &present);
        if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "vkQueuePresentKHR failed: %d\n", (int)presented);
            return 1;
        }
        if (frames++ == 0) {
            write_report(&options, 0, 0);
        }
    }
    const double seconds = now() - start;
    vkDeviceWaitIdle(device);
    write_report(&options, frames, seconds);

    vkDestroyFence(device, done, NULL);
    vkDestroySemaphore(device, drawn, NULL);
    vkDestroySemaphore(device, acquired, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    xcb_disconnect(connection);
    return 0;
}
