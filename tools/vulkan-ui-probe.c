// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 1: real PS5 Vulkan presentation, independent of the upstream UI.
#include <stdio.h>
#include <stdlib.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
extern PFN_vkVoidFunction radv_GetInstanceProcAddr(VkInstance, const char *);
extern int sceKernelDebugOutText(int, const char *);
extern int sceKernelUsleep(unsigned int);
#define INSTANCE_COMMANDS(X)                                                                       \
    X(CreateInstance)                                                                              \
    X(EnumeratePhysicalDevices) X(GetPhysicalDeviceQueueFamilyProperties) X(CreateDevice)          \
        X(GetDeviceProcAddr) X(GetPhysicalDeviceDisplayPropertiesKHR)                              \
            X(GetDisplayModePropertiesKHR) X(CreateDisplayPlaneSurfaceKHR)                         \
                X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR)   \
                    X(GetPhysicalDeviceSurfaceFormatsKHR)
#define DEVICE_COMMANDS(X)                                                                         \
    X(GetDeviceQueue)                                                                              \
    X(CreateSwapchainKHR) X(GetSwapchainImagesKHR) X(CreateImageView) X(CreateRenderPass)          \
        X(CreateFramebuffer) X(CreateCommandPool) X(AllocateCommandBuffers) X(CreateSemaphore)     \
            X(AcquireNextImageKHR) X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandBuffer) \
                X(CmdBeginRenderPass) X(CmdClearAttachments) X(CmdEndRenderPass) X(QueueSubmit)    \
                    X(QueuePresentKHR) X(QueueWaitIdle)
#define DECLARE(name) static PFN_vk##name vk##name;
INSTANCE_COMMANDS(DECLARE)
DEVICE_COMMANDS(DECLARE)
static FILE *report;
static int quiet;
static void checked(VkResult result, const char *operation)
{
    if (quiet && result == VK_SUCCESS)
        return;
    char line[160];
    snprintf(line, sizeof(line), "[ProsperoVKUI] %s=%d\n", operation, result);
    sceKernelDebugOutText(0, line);
    if (report)
    {
        fputs(line, report);
        fflush(report);
    }
    if (result != VK_SUCCESS)
        for (;;)
            sceKernelUsleep(100000);
}
#define CHECK(call) checked((call), #call)
static void require(int condition, const char *message)
{
    checked(condition ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED, message);
}
int main(void)
{
    report = fopen("/download0/prospero-vulkan-ui.txt", "w");
    VkInstance instance = VK_NULL_HANDLE;
    vkCreateInstance = (PFN_vkCreateInstance)radv_GetInstanceProcAddr(NULL, "vkCreateInstance");
    require(vkCreateInstance != NULL, "entrypoint");
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "ProsperoAI Vulkan UI probe",
                             .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app,
                               .enabledExtensionCount = 2,
                               .ppEnabledExtensionNames = extensions};
    CHECK(vkCreateInstance(&ic, NULL, &instance));
#define LOAD_I(name)                                                                               \
    vk##name = (PFN_vk##name)radv_GetInstanceProcAddr(instance, "vk" #name);                       \
    require(vk##name != NULL, "vk" #name);
    INSTANCE_COMMANDS(LOAD_I)
    uint32_t count = 1;
    VkPhysicalDevice physical;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    require(count == 1, "physical device");
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    VkQueueFamilyProperties *properties = calloc(families, sizeof(*properties));
    require(properties != NULL, "queue properties allocation");
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, properties);
    uint32_t family = 0;
    while (family < families && !(properties[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        ++family;
    free(properties);
    require(family < families, "graphics queue");
    float priority = 1;
    VkDeviceQueueCreateInfo qc = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                  .queueFamilyIndex = family,
                                  .queueCount = 1,
                                  .pQueuePriorities = &priority};
    const char *swap_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dc = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                             .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qc,
                             .enabledExtensionCount = 1,
                             .ppEnabledExtensionNames = &swap_extension};
    VkDevice device;
    CHECK(vkCreateDevice(physical, &dc, NULL, &device));
#define LOAD_D(name)                                                                               \
    vk##name = (PFN_vk##name)vkGetDeviceProcAddr(device, "vk" #name);                              \
    require(vk##name != NULL, "vk" #name);
    DEVICE_COMMANDS(LOAD_D)
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkDisplayPropertiesKHR display;
    count = 1;
    CHECK(vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &count, &display));
    require(count == 1, "display");
    VkDisplayModePropertiesKHR mode;
    count = 1;
    VkResult mode_result = vkGetDisplayModePropertiesKHR(physical, display.display, &count, &mode);
    require((mode_result == VK_SUCCESS || mode_result == VK_INCOMPLETE) && count > 0,
            "display mode");
    VkDisplaySurfaceCreateInfoKHR sc = {.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
                                        .displayMode = mode.displayMode,
                                        .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                        .alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
                                        .imageExtent = mode.parameters.visibleRegion};
    VkSurfaceKHR surface;
    CHECK(vkCreateDisplayPlaneSurfaceKHR(instance, &sc, NULL, &surface));
    VkBool32 supported = VK_FALSE;
    CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical, family, surface, &supported));
    require(supported, "present support");
    VkSurfaceCapabilitiesKHR caps;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
    require(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            "color attachment support");
    VkSurfaceFormatKHR format;
    count = 1;
    VkResult format_result =
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, &format);
    require((format_result == VK_SUCCESS || format_result == VK_INCOMPLETE) && count > 0,
            "surface format");
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;
    VkSwapchainCreateInfoKHR swc = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                    .surface = surface,
                                    .minImageCount = image_count,
                                    .imageFormat = format.format,
                                    .imageColorSpace = format.colorSpace,
                                    .imageExtent = caps.currentExtent,
                                    .imageArrayLayers = 1,
                                    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                    .preTransform = caps.currentTransform,
                                    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                    .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                                    .clipped = VK_TRUE};
    VkSwapchainKHR swapchain;
    CHECK(vkCreateSwapchainKHR(device, &swc, NULL, &swapchain));
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &count, NULL));
    require(count <= 8, "bounded swapchain");
    VkImage images[8];
    VkImageView views[8];
    VkFramebuffer frames[8];
    VkSemaphore rendered[8];
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &count, images));
    VkAttachmentDescription attachment = {.format = format.format,
                                          .samples = VK_SAMPLE_COUNT_1_BIT,
                                          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                          .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &reference};
    VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL,
                                      .dstSubpass = 0,
                                      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo rc = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                 .attachmentCount = 1,
                                 .pAttachments = &attachment,
                                 .subpassCount = 1,
                                 .pSubpasses = &subpass,
                                 .dependencyCount = 1,
                                 .pDependencies = &dependency};
    VkRenderPass pass;
    CHECK(vkCreateRenderPass(device, &rc, NULL, &pass));
    VkSemaphoreCreateInfo semc = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo vc = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                    .image = images[i],
                                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                    .format = format.format,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        CHECK(vkCreateImageView(device, &vc, NULL, &views[i]));
        VkFramebufferCreateInfo fc = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                      .renderPass = pass,
                                      .attachmentCount = 1,
                                      .pAttachments = &views[i],
                                      .width = caps.currentExtent.width,
                                      .height = caps.currentExtent.height,
                                      .layers = 1};
        CHECK(vkCreateFramebuffer(device, &fc, NULL, &frames[i]));
        CHECK(vkCreateSemaphore(device, &semc, NULL, &rendered[i]));
    }
    VkCommandPoolCreateInfo pc = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                  .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                  .queueFamilyIndex = family};
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pc, NULL, &pool));
    VkCommandBufferAllocateInfo ac = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                      .commandPool = pool,
                                      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                      .commandBufferCount = 1};
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &ac, &cmd));
    VkSemaphore acquired;
    CHECK(vkCreateSemaphore(device, &semc, NULL, &acquired));
    unsigned frame = 0;
    for (;;)
    {
        uint32_t index;
        CHECK(
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index));
        CHECK(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo bc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                       .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        CHECK(vkBeginCommandBuffer(cmd, &bc));
        VkClearValue background = {.color = {{0.025f, 0.03f, 0.055f, 1}}};
        VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = pass,
                                       .framebuffer = frames[index],
                                       .renderArea = {{0, 0}, caps.currentExtent},
                                       .clearValueCount = 1,
                                       .pClearValues = &background};
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        VkClearAttachment panel = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .colorAttachment = 0,
                                   .clearValue = {.color = {{0.15f, 0.25f, 0.55f, 1}}}};
        VkClearRect rect = {.rect = {{(int32_t)caps.currentExtent.width / 4,
                                      (int32_t)caps.currentExtent.height / 4},
                                     {caps.currentExtent.width / 2, caps.currentExtent.height / 2}},
                            .layerCount = 1};
        vkCmdClearAttachments(cmd, 1, &panel, 1, &rect);
        vkCmdEndRenderPass(cmd);
        CHECK(vkEndCommandBuffer(cmd));
        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                               .waitSemaphoreCount = 1,
                               .pWaitSemaphores = &acquired,
                               .pWaitDstStageMask = &wait,
                               .commandBufferCount = 1,
                               .pCommandBuffers = &cmd,
                               .signalSemaphoreCount = 1,
                               .pSignalSemaphores = &rendered[index]};
        CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                    .waitSemaphoreCount = 1,
                                    .pWaitSemaphores = &rendered[index],
                                    .swapchainCount = 1,
                                    .pSwapchains = &swapchain,
                                    .pImageIndices = &index};
        CHECK(vkQueuePresentKHR(queue, &present));
        CHECK(vkQueueWaitIdle(queue));
        if (++frame == 1)
            quiet = 1;
        if (frame == 120)
        {
            quiet = 0;
            checked(VK_SUCCESS, "120 frames presented");
            if (report)
            {
                fclose(report);
                report = NULL;
            }
            // Keep displaying without synchronous per-call diagnostic output.
            for (;;)
                sceKernelUsleep(100000);
        }
    }
}
