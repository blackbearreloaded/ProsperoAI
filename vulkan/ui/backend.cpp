// ProsperoAI Vulkan backend for the pinned upstream UI draw API.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend.hpp"
#include "ui_shaders.hpp"
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/glcorearb.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#ifdef PROSPERO_HOST
#include <dlfcn.h>
#else
extern "C" PFN_vkVoidFunction radv_GetInstanceProcAddr(VkInstance, const char *);
extern "C" int sceKernelDebugOutText(int, const char *);
#endif
namespace prospero::vkui
{
namespace
{
#define INSTANCE_FUNCTIONS(X)                                                                      \
    X(CreateInstance)                                                                              \
    X(DestroyInstance)                                                                             \
    X(EnumeratePhysicalDevices) X(GetPhysicalDeviceQueueFamilyProperties)                          \
        X(GetPhysicalDeviceMemoryProperties) X(CreateDevice) X(GetDeviceProcAddr)                  \
            X(GetPhysicalDeviceDisplayPropertiesKHR) X(GetDisplayModePropertiesKHR)                \
                X(CreateDisplayPlaneSurfaceKHR) X(DestroySurfaceKHR)                               \
                    X(GetPhysicalDeviceSurfaceSupportKHR)                                          \
                        X(GetPhysicalDeviceSurfaceCapabilitiesKHR)                                 \
                            X(GetPhysicalDeviceSurfaceFormatsKHR)
#define DEVICE_FUNCTIONS(X)                                                                        \
    X(DestroyDevice)                                                                               \
    X(GetDeviceQueue)                                                                              \
    X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR)   \
        X(QueuePresentKHR) X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements)            \
            X(CreateImageView) X(DestroyImageView) X(AllocateMemory) X(FreeMemory) X(              \
                BindImageMemory) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements)   \
                X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateRenderPass)                \
                    X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(             \
                        CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers)         \
                        X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandBuffer) X(         \
                            CreateSemaphore) X(DestroySemaphore) X(QueueSubmit) X(QueueWaitIdle)   \
                            X(DeviceWaitIdle) X(CmdPipelineBarrier) X(CmdCopyBufferToImage)        \
                                X(CmdCopyImageToBuffer) X(CmdCopyImage) X(CmdBlitImage) X(         \
                                    CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdClearAttachments) \
                                    X(CmdBindPipeline) X(CmdBindVertexBuffers) X(CmdSetViewport)   \
                                        X(CmdSetScissor) X(CmdDraw) X(CmdPushConstants) X(         \
                                            CmdBindDescriptorSets) X(CreateShaderModule)           \
                                            X(DestroyShaderModule) X(CreatePipelineLayout) X(      \
                                                DestroyPipelineLayout) X(CreateGraphicsPipelines)  \
                                                X(DestroyPipeline) X(CreateDescriptorSetLayout)    \
                                                    X(DestroyDescriptorSetLayout)                  \
                                                        X(CreateDescriptorPool)                    \
                                                            X(DestroyDescriptorPool)               \
                                                                X(ResetDescriptorPool)             \
                                                                    X(AllocateDescriptorSets)      \
                                                                        X(UpdateDescriptorSets)    \
                                                                            X(CreateSampler)       \
                                                                                X(DestroySampler)
#define DECLARE(n) PFN_vk##n vk##n = nullptr;
INSTANCE_FUNCTIONS(DECLARE)
DEVICE_FUNCTIONS(DECLARE)
PFN_vkGetInstanceProcAddr get_instance;
VkInstance instance{};
VkPhysicalDevice physical{};
VkDevice device{};
VkQueue queue{};
uint32_t family{};
VkPhysicalDeviceMemoryProperties memory_properties{};
VkSurfaceKHR surface{};
VkSwapchainKHR swapchain{};
VkSemaphore acquired{};
VkCommandPool command_pool{};
VkCommandBuffer command{};
VkDescriptorSetLayout descriptor_layout{};
VkDescriptorPool descriptor_pool{};
VkPipelineLayout pipeline_layout{};
std::array<VkSampler, 4> samplers{};
VkExtent2D extent{};
struct Image
{
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    VkFramebuffer framebuffer{};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    int width{}, height{};
    bool display{};
    bool min_linear = true, mag_linear = true;
};
struct Buffer
{
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void *mapped{};
};
struct Program
{
    VkShaderModule vertex{}, fragment{};
    std::array<float, 28> push{};
};
std::map<GLuint, Image> textures;
std::map<GLuint, GLuint> framebuffers;
std::map<GLuint, std::vector<unsigned char>> buffers;
std::map<GLuint, Program> programs;
std::map<GLuint, GLuint> vertex_arrays;
std::map<GLuint, VkBuffer> uploaded_buffers;
GLuint bound_vao{};
std::map<VkFormat, VkRenderPass> passes;
std::map<unsigned, VkPipeline> pipelines;
std::vector<Image> display_images;
std::vector<VkSemaphore> rendered;
std::vector<Buffer> temporary;
GLuint serial = 1, bound_framebuffer{}, bound_buffer{}, bound_program{}, bound_texture[7]{};
GLuint dummy_texture{};
unsigned texture_unit{}, display_index{};
int view_x{}, view_y{}, view_width{}, view_height{}, scissor_x{}, scissor_y{}, scissor_width{},
    scissor_height{};
bool recording{}, in_pass{}, have_display{}, wait_acquired{}, blend{}, scissor{}, presentation{};
std::array<float, 4> clear_color{};
void fail(const char *name, VkResult result)
{
    char line[256];
    std::snprintf(line, sizeof(line), "[ProsperoVKUI] %s failed: %d\n", name, result);
    std::fputs(line, stderr);
#ifndef PROSPERO_HOST
    sceKernelDebugOutText(0, line);
#endif
    std::abort();
}
void check(VkResult result, const char *name)
{
    if (result != VK_SUCCESS)
        fail(name, result);
}
#define CHECK(call) check((call), #call)
uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memory_properties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    fail("memory type", VK_ERROR_FEATURE_NOT_PRESENT);
    return 0;
}
Buffer make_buffer(size_t size, VkBufferUsageFlags usage)
{
    Buffer b;
    VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    c.size = std::max<size_t>(size, 4);
    c.usage = usage;
    CHECK(vkCreateBuffer(device, &c, nullptr, &b.buffer));
    VkMemoryRequirements r;
    vkGetBufferMemoryRequirements(device, b.buffer, &r);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = r.size;
    a.memoryTypeIndex = memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkAllocateMemory(device, &a, nullptr, &b.memory));
    CHECK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
    CHECK(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped));
    return b;
}
void destroy_buffer(Buffer &b)
{
    if (b.mapped)
        vkUnmapMemory(device, b.memory);
    if (b.buffer)
        vkDestroyBuffer(device, b.buffer, nullptr);
    if (b.memory)
        vkFreeMemory(device, b.memory, nullptr);
    b = {};
}
void begin_commands()
{
    if (recording)
        return;
    CHECK(vkResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK(vkBeginCommandBuffer(command, &info));
    recording = true;
}
void end_pass()
{
    if (in_pass)
    {
        vkCmdEndRenderPass(command);
        in_pass = false;
    }
}
void barrier(Image &image, VkImageLayout layout)
{
    // A transfer can reuse the current layout after flush(), but it still
    // needs a fresh command buffer before recording its copy operation.
    begin_commands();
    if (image.layout == layout)
        return;
    end_pass();
    begin_commands();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = image.layout == VK_IMAGE_LAYOUT_UNDEFINED
                          ? 0
                          : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = image.layout;
    b.newLayout = layout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    image.layout = layout;
}
VkRenderPass pass(VkFormat format)
{
    if (passes.count(format))
        return passes.at(format);
    VkAttachmentDescription a{};
    a.format = format;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo c{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    c.attachmentCount = 1;
    c.pAttachments = &a;
    c.subpassCount = 1;
    c.pSubpasses = &sub;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    c.dependencyCount = 2;
    c.pDependencies = dependencies;
    VkRenderPass p;
    CHECK(vkCreateRenderPass(device, &c, nullptr, &p));
    passes[format] = p;
    return p;
}
void make_view(Image &image)
{
    VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    c.image = image.image;
    c.viewType = VK_IMAGE_VIEW_TYPE_2D;
    c.format = image.format;
    c.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    CHECK(vkCreateImageView(device, &c, nullptr, &image.view));
}
void make_framebuffer(Image &image)
{
    if (image.framebuffer)
        return;
    VkFramebufferCreateInfo c{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    c.renderPass = pass(image.format);
    c.attachmentCount = 1;
    c.pAttachments = &image.view;
    c.width = image.width;
    c.height = image.height;
    c.layers = 1;
    CHECK(vkCreateFramebuffer(device, &c, nullptr, &image.framebuffer));
}
void acquire()
{
    if (have_display)
        return;
    CHECK(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE,
                                &display_index));
    have_display = true;
    wait_acquired = true;
}
Image &target()
{
    if (bound_framebuffer)
        return textures.at(framebuffers.at(bound_framebuffer));
    if (presentation)
    {
        acquire();
        return display_images.at(display_index);
    }
    return textures.at(framebuffers.at(0));
}
void begin_pass()
{
    Image &image = target();
    barrier(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    make_framebuffer(image);
    begin_commands();
    if (!in_pass)
    {
        VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        info.renderPass = pass(image.format);
        info.framebuffer = image.framebuffer;
        info.renderArea.extent = {uint32_t(image.width), uint32_t(image.height)};
        vkCmdBeginRenderPass(command, &info, VK_SUBPASS_CONTENTS_INLINE);
        in_pass = true;
    }
}
void flush(bool present = false)
{
    if (present && presentation)
    {
        acquire();
        barrier(display_images[display_index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    end_pass();
    if (recording)
    {
        CHECK(vkEndCommandBuffer(command));
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        if (wait_acquired)
        {
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &acquired;
            submit.pWaitDstStageMask = &stage;
        }
        if (present && presentation)
        {
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &rendered[display_index];
        }
        CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        wait_acquired = false;
        recording = false;
    }
    if (present && presentation)
    {
        VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        p.waitSemaphoreCount = 1;
        p.pWaitSemaphores = &rendered[display_index];
        p.swapchainCount = 1;
        p.pSwapchains = &swapchain;
        p.pImageIndices = &display_index;
        CHECK(vkQueuePresentKHR(queue, &p));
        have_display = false;
    }
    CHECK(vkQueueWaitIdle(queue));
    for (auto &b : temporary)
        destroy_buffer(b);
    temporary.clear();
    uploaded_buffers.clear();
    CHECK(vkResetDescriptorPool(device, descriptor_pool, 0));
}
void destroy_image(Image &image)
{
    if (image.framebuffer)
        vkDestroyFramebuffer(device, image.framebuffer, nullptr);
    if (image.view)
        vkDestroyImageView(device, image.view, nullptr);
    if (image.memory)
    {
        vkDestroyImage(device, image.image, nullptr);
        vkFreeMemory(device, image.memory, nullptr);
    }
    image = {};
}
Image make_image(int width, int height, VkFormat format)
{
    if (width <= 0 || height <= 0)
        fail("invalid texture dimensions", VK_ERROR_INITIALIZATION_FAILED);
    Image image;
    image.width = width;
    image.height = height;
    image.format = format;
    VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    c.imageType = VK_IMAGE_TYPE_2D;
    c.format = format;
    c.extent = {uint32_t(width), uint32_t(height), 1};
    c.mipLevels = c.arrayLayers = 1;
    c.samples = VK_SAMPLE_COUNT_1_BIT;
    c.tiling = VK_IMAGE_TILING_OPTIMAL;
    c.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (format != VK_FORMAT_R8_UNORM)
        c.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    CHECK(vkCreateImage(device, &c, nullptr, &image.image));
    VkMemoryRequirements r;
    vkGetImageMemoryRequirements(device, image.image, &r);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = r.size;
    a.memoryTypeIndex = memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(vkAllocateMemory(device, &a, nullptr, &image.memory));
    CHECK(vkBindImageMemory(device, image.image, image.memory, 0));
    make_view(image);
    return image;
}
void upload(Image &image, const void *data)
{
    size_t size = size_t(image.width) * image.height * (image.format == VK_FORMAT_R8_UNORM ? 1 : 4);
    Buffer b = make_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (data)
        std::memcpy(b.mapped, data, size);
    else
        std::memset(b.mapped, 0, size);
    barrier(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {uint32_t(image.width), uint32_t(image.height), 1};
    vkCmdCopyBufferToImage(command, b.buffer, image.image, image.layout, 1, &copy);
    barrier(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    temporary.push_back(b);
}
VkShaderModule shader(const uint32_t *words, size_t size)
{
    VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    c.codeSize = size;
    c.pCode = words;
    VkShaderModule m;
    CHECK(vkCreateShaderModule(device, &c, nullptr, &m));
    return m;
}
VkPipeline pipeline(GLuint id, VkFormat format, bool blending)
{
    unsigned key = id * 4 + (format == VK_FORMAT_B8G8R8A8_UNORM ? 2 : 0) + (blending ? 1 : 0);
    if (pipelines.count(key))
        return pipelines.at(key);
    auto &program = programs.at(id);
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i)
    {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? program.fragment : program.vertex;
        stages[i].pName = "main";
    }
    VkVertexInputBindingDescription binding{0, id == 1 ? 96u : 24u,
                                            id == 1 ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                    : VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[6]{};
    for (uint32_t i = 0; i < 6; ++i)
        attributes[i] = {i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16};
    if (id == 2)
    {
        attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
        attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8};
    }
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (id <= 2)
    {
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = id == 1 ? 6 : 2;
        vi.pVertexAttributeDescriptions = attributes;
    }
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{};
    ba.blendEnable = blending;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.alphaBlendOp = VK_BLEND_OP_ADD;
    ba.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo cb{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo c{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    c.stageCount = 2;
    c.pStages = stages;
    c.pVertexInputState = &vi;
    c.pInputAssemblyState = &ia;
    c.pViewportState = &vp;
    c.pRasterizationState = &rs;
    c.pMultisampleState = &ms;
    c.pColorBlendState = &cb;
    c.pDynamicState = &ds;
    c.layout = pipeline_layout;
    c.renderPass = pass(format);
    VkPipeline p;
    CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &c, nullptr, &p));
    pipelines[key] = p;
    return p;
}
void draw(int first, int count, int instances, unsigned base)
{
    if (count <= 0 || instances <= 0)
        return;
    // Descriptor images must transition before beginning the attachment pass.
    std::array<VkDescriptorImageInfo, 7> info{};
    for (unsigned i = 0; i < 7; ++i)
    {
        GLuint texture =
            (bound_program == 1 || (bound_program == 4 && i == 0)) ? bound_texture[i] : 0;
        if (texture && textures.at(texture).image == target().image)
            texture = 0;
        auto &image = textures.at(texture ? texture : dummy_texture);
        barrier(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        info[i] = {samplers[(image.min_linear ? 1 : 0) + (image.mag_linear ? 2 : 0)], image.view,
                   image.layout};
    }
    begin_pass();
    auto &image = target();
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline(bound_program, image.format, blend));
    VkViewport vp{float(view_x),
                  float(image.display ? view_y + view_height : view_y),
                  float(view_width),
                  float(image.display ? -view_height : view_height),
                  0,
                  1};
    vkCmdSetViewport(command, 0, 1, &vp);
    int sx = scissor ? scissor_x : 0, sy = scissor ? scissor_y : 0,
        sw = scissor ? scissor_width : image.width, sh = scissor ? scissor_height : image.height;
    if (image.display && scissor)
        sy = image.height - sy - sh;
    int x0 = std::clamp(sx, 0, image.width), y0 = std::clamp(sy, 0, image.height);
    int x1 = std::clamp(sx + sw, 0, image.width), y1 = std::clamp(sy + sh, 0, image.height);
    VkRect2D clip{{x0, y0}, {uint32_t(std::max(0, x1 - x0)), uint32_t(std::max(0, y1 - y0))}};
    vkCmdSetScissor(command, 0, 1, &clip);
    auto &p = programs.at(bound_program);
    if (bound_program == 3)
        p.push[25] = image.display ? 1 : 0;
    vkCmdPushConstants(command, pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 112,
                       p.push.data());
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = descriptor_pool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &descriptor_layout;
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(device, &da, &set));
    VkWriteDescriptorSet writes[2]{};
    for (unsigned i = 0; i < 2; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = i ? 6 : 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = info.data() + (i ? 1 : 0);
    }
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &set,
                            0, nullptr);
    if (bound_program <= 2)
    {
        if (!uploaded_buffers.count(bound_buffer))
        {
            const auto &bytes = buffers.at(bound_buffer);
            Buffer b = make_buffer(bytes.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            std::memcpy(b.mapped, bytes.data(), bytes.size());
            uploaded_buffers[bound_buffer] = b.buffer;
            temporary.push_back(b);
        }
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &uploaded_buffers.at(bound_buffer), &offset);
    }
    vkCmdDraw(command, count, instances, first, base);
}
} // namespace
bool open(bool display, int requested_width, int requested_height)
{
    presentation = display;
    extent = {uint32_t(requested_width), uint32_t(requested_height)};
#ifdef PROSPERO_HOST
    auto library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library)
        return false;
    get_instance =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
#else
    get_instance = radv_GetInstanceProcAddr;
#endif
    if (!get_instance)
        return false;
    vkCreateInstance =
        reinterpret_cast<PFN_vkCreateInstance>(get_instance(nullptr, "vkCreateInstance"));
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ProsperoAI native Vulkan UI";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ic.pApplicationInfo = &app;
    if (display)
    {
        ic.enabledExtensionCount = 2;
        ic.ppEnabledExtensionNames = extensions;
    }
    CHECK(vkCreateInstance(&ic, nullptr, &instance));
#define LOAD_I(n) vk##n = reinterpret_cast<PFN_vk##n>(get_instance(instance, "vk" #n));
    INSTANCE_FUNCTIONS(LOAD_I)
    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count)
        return false;
    std::vector<VkPhysicalDevice> devices(count);
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    physical = devices.front();
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        ++family;
    if (family == count)
        return false;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    float priority = 1;
    VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qc.queueFamilyIndex = family;
    qc.queueCount = 1;
    qc.pQueuePriorities = &priority;
    const char *extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dc.queueCreateInfoCount = 1;
    dc.pQueueCreateInfos = &qc;
    if (display)
    {
        dc.enabledExtensionCount = 1;
        dc.ppEnabledExtensionNames = &extension;
    }
    CHECK(vkCreateDevice(physical, &dc, nullptr, &device));
#define LOAD_D(n) vk##n = reinterpret_cast<PFN_vk##n>(vkGetDeviceProcAddr(device, "vk" #n));
    DEVICE_FUNCTIONS(LOAD_D)
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = family;
    CHECK(vkCreateCommandPool(device, &cp, nullptr, &command_pool));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = command_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    CHECK(vkAllocateCommandBuffers(device, &ca, &command));
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (unsigned i = 0; i < 2; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = i ? 6 : 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2;
    dl.pBindings = bindings;
    CHECK(vkCreateDescriptorSetLayout(device, &dl, nullptr, &descriptor_layout));
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 * 16384};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 16384;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &size;
    CHECK(vkCreateDescriptorPool(device, &dp, nullptr, &descriptor_pool));
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 112};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &descriptor_layout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    CHECK(vkCreatePipelineLayout(device, &pl, nullptr, &pipeline_layout));
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        si.minFilter = (i & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        si.magFilter = (i & 2) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        CHECK(vkCreateSampler(device, &si, nullptr, &samplers[i]));
    }
    dummy_texture = serial++;
    textures[dummy_texture] = make_image(1, 1, VK_FORMAT_R8G8B8A8_UNORM);
    uint32_t white = 0xffffffff;
    upload(textures.at(dummy_texture), &white);
    if (display)
    {
        VkDisplayPropertiesKHR properties{};
        count = 1;
        CHECK(vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &count, &properties));
        VkDisplayModePropertiesKHR mode{};
        count = 1;
        VkResult r = vkGetDisplayModePropertiesKHR(physical, properties.display, &count, &mode);
        if (r != VK_SUCCESS && r != VK_INCOMPLETE)
            fail("display mode", r);
        VkDisplaySurfaceCreateInfoKHR sc{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
        sc.displayMode = mode.displayMode;
        sc.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        sc.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        sc.imageExtent = mode.parameters.visibleRegion;
        CHECK(vkCreateDisplayPlaneSurfaceKHR(instance, &sc, nullptr, &surface));
        VkBool32 supported;
        CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical, family, surface, &supported));
        if (!supported)
            return false;
        VkSurfaceCapabilitiesKHR caps;
        CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
        extent = caps.currentExtent;
        VkSurfaceFormatKHR format;
        count = 1;
        r = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, &format);
        if (r != VK_SUCCESS && r != VK_INCOMPLETE)
            fail("display format", r);
        VkSwapchainCreateInfoKHR sw{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sw.surface = surface;
        sw.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount)
            sw.minImageCount = std::min(sw.minImageCount, caps.maxImageCount);
        sw.imageFormat = format.format;
        sw.imageColorSpace = format.colorSpace;
        sw.imageExtent = extent;
        sw.imageArrayLayers = 1;
        sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            sw.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        sw.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sw.preTransform = caps.currentTransform;
        sw.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sw.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sw.clipped = VK_TRUE;
        CHECK(vkCreateSwapchainKHR(device, &sw, nullptr, &swapchain));
        CHECK(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr));
        std::vector<VkImage> images(count);
        CHECK(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()));
        display_images.resize(count);
        rendered.resize(count);
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        CHECK(vkCreateSemaphore(device, &sem, nullptr, &acquired));
        for (uint32_t i = 0; i < count; ++i)
        {
            auto &image = display_images[i];
            image.image = images[i];
            image.width = extent.width;
            image.height = extent.height;
            image.format = format.format;
            image.display = true;
            make_view(image);
            CHECK(vkCreateSemaphore(device, &sem, nullptr, &rendered[i]));
        }
    }
    else
    {
        GLuint id = serial++;
        textures[id] = make_image(requested_width, requested_height, VK_FORMAT_R8G8B8A8_UNORM);
        upload(textures[id], nullptr);
        framebuffers[0] = id;
    }
    flush();
    return true;
}
bool swap()
{
    flush(true);
    return true;
}
int width()
{
    return int(extent.width);
}
int height()
{
    return int(extent.height);
}
uint32_t program(const char *name)
{
    uint32_t id = 0;
    Program p;
#define SHADERS(label, number)                                                                     \
    if (std::strcmp(name, #label) == 0)                                                            \
    {                                                                                              \
        id = number;                                                                               \
        p.vertex = shader(label##_vert, sizeof(label##_vert));                                     \
        p.fragment = shader(label##_frag, sizeof(label##_frag));                                   \
    }
    SHADERS(batch2d, 1)
    SHADERS(mesh2d, 2)
    SHADERS(backdrop, 3) SHADERS(blur, 4) if (!id)
        fail("unknown pinned UI shader", VK_ERROR_INITIALIZATION_FAILED);
    programs[id] = p;
    return id;
}
void close()
{
    if (!device)
        return;
    flush();
    CHECK(vkDeviceWaitIdle(device));
    for (auto &[key, p] : pipelines)
        vkDestroyPipeline(device, p, nullptr);
    pipelines.clear();
    for (auto &[key, p] : programs)
    {
        vkDestroyShaderModule(device, p.vertex, nullptr);
        vkDestroyShaderModule(device, p.fragment, nullptr);
    }
    programs.clear();
    for (auto &[key, image] : textures)
        destroy_image(image);
    textures.clear();
    for (auto &image : display_images)
        destroy_image(image);
    display_images.clear();
    for (auto &[key, p] : passes)
        vkDestroyRenderPass(device, p, nullptr);
    passes.clear();
    for (auto sem : rendered)
        vkDestroySemaphore(device, sem, nullptr);
    rendered.clear();
    if (acquired)
        vkDestroySemaphore(device, acquired, nullptr);
    if (swapchain)
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    for (auto sampler : samplers)
        vkDestroySampler(device, sampler, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    if (surface)
        vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
    buffers.clear();
    framebuffers.clear();
    bound_framebuffer = bound_buffer = bound_program = 0;
    have_display = wait_acquired = false;
}
} // namespace prospero::vkui

// Narrow compatibility entrypoints used only by the pinned 2D UI kit. These
// allocate/record Vulkan resources; there is no GL context or GL driver linkage.
using namespace prospero::vkui;
extern "C"
{
    void glGenTextures(GLsizei n, GLuint *ids)
    {
        for (int i = 0; i < n; ++i)
        {
            ids[i] = serial++;
            textures[ids[i]] = {};
        }
    }
    void glBindTexture(GLenum, GLuint id)
    {
        if (texture_unit >= 7)
            fail("UI texture unit", VK_ERROR_INITIALIZATION_FAILED);
        bound_texture[texture_unit] = id;
    }
    void glActiveTexture(GLenum unit)
    {
        texture_unit = unit - GL_TEXTURE0;
    }
    void glTexImage2D(GLenum, GLint level, GLint format, GLsizei w, GLsizei h, GLint, GLenum,
                      GLenum, const void *pixels)
    {
        if (level != 0)
            fail("UI mip level", VK_ERROR_FEATURE_NOT_PRESENT);
        auto &image = textures.at(bound_texture[texture_unit]);
        if (image.image)
        {
            flush();
            destroy_image(image);
        }
        image = make_image(w, h, format == GL_R8 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM);
        upload(image, pixels);
    }
    void glDeleteTextures(GLsizei n, const GLuint *ids)
    {
        if (!device)
            return;
        flush();
        for (int i = 0; i < n; ++i)
        {
            auto it = textures.find(ids[i]);
            if (it != textures.end())
            {
                destroy_image(it->second);
                textures.erase(it);
            }
            for (auto &bound : bound_texture)
                if (bound == ids[i])
                    bound = 0;
        }
    }
    void glTexParameteri(GLenum, GLenum parameter, GLint value)
    {
        auto &image = textures.at(bound_texture[texture_unit]);
        if (parameter == GL_TEXTURE_MIN_FILTER || parameter == GL_TEXTURE_MAG_FILTER)
        {
            if (value != GL_LINEAR && value != GL_NEAREST)
                fail("UI texture filter", VK_ERROR_FEATURE_NOT_PRESENT);
            if (parameter == GL_TEXTURE_MIN_FILTER)
                image.min_linear = value == GL_LINEAR;
            else
                image.mag_linear = value == GL_LINEAR;
        }
        else if (parameter == GL_TEXTURE_MAX_LEVEL)
        {
            if (value != 0)
                fail("UI mip count", VK_ERROR_FEATURE_NOT_PRESENT);
        }
        else if (parameter == GL_TEXTURE_WRAP_S || parameter == GL_TEXTURE_WRAP_T)
        {
            if (value != GL_CLAMP_TO_EDGE)
                fail("UI texture wrap", VK_ERROR_FEATURE_NOT_PRESENT);
        }
        else
            fail("UI texture parameter", VK_ERROR_FEATURE_NOT_PRESENT);
    }
    void glPixelStorei(GLenum, GLint)
    {
    } // All uploads are tightly packed.
    void glGenBuffers(GLsizei n, GLuint *ids)
    {
        for (int i = 0; i < n; ++i)
        {
            ids[i] = serial++;
            buffers[ids[i]] = {};
        }
    }
    void glBindBuffer(GLenum, GLuint id)
    {
        bound_buffer = id;
    }
    void glBufferData(GLenum, GLsizeiptr size, const void *data, GLenum)
    {
        uploaded_buffers.erase(bound_buffer);
        auto &bytes = buffers.at(bound_buffer);
        bytes.resize(size);
        if (data)
            std::memcpy(bytes.data(), data, size);
    }
    void glBufferSubData(GLenum, GLintptr offset, GLsizeiptr size, const void *data)
    {
        uploaded_buffers.erase(bound_buffer);
        auto &bytes = buffers.at(bound_buffer);
        if (offset < 0 || size < 0 || size_t(offset) + size_t(size) > bytes.size())
            fail("UI vertex bounds", VK_ERROR_INITIALIZATION_FAILED);
        std::memcpy(bytes.data() + offset, data, size);
    }
    void glDeleteBuffers(GLsizei n, const GLuint *ids)
    {
        for (int i = 0; i < n; ++i)
            buffers.erase(ids[i]);
    }
    void glGenVertexArrays(GLsizei n, GLuint *ids)
    {
        for (int i = 0; i < n; ++i)
            ids[i] = serial++;
    }
    void glBindVertexArray(GLuint id)
    {
        bound_vao = id;
        if (vertex_arrays.count(id))
            bound_buffer = vertex_arrays.at(id);
    }
    void glDeleteVertexArrays(GLsizei, const GLuint *)
    {
    }
    void glEnableVertexAttribArray(GLuint)
    {
    }
    void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)
    {
        vertex_arrays[bound_vao] = bound_buffer;
    }
    void glVertexAttribDivisor(GLuint, GLuint)
    {
    }
    void glUseProgram(GLuint id)
    {
        bound_program = id;
    }
    void glDeleteProgram(GLuint)
    {
    } // Program/pipeline lifetime belongs to backend.close().
    void glUniform4f(GLint location, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
    {
        auto &p = programs.at(bound_program).push;
        unsigned offset = location == 7 ? 20 : 0;
        p[offset] = a;
        p[offset + 1] = b;
        p[offset + 2] = c;
        p[offset + 3] = d;
    }
    void glUniform2f(GLint location, GLfloat a, GLfloat b)
    {
        auto &p = programs.at(bound_program).push;
        unsigned offset = bound_program <= 2 && location == 1 ? 4 : 0;
        p[offset] = a;
        p[offset + 1] = b;
    }
    void glUniform1f(GLint location, GLfloat value)
    {
        auto &p = programs.at(bound_program).push;
        p[location == 8 ? 24 : 2] = value;
    }
    void glUniform1i(GLint location, GLint value)
    {
        if (bound_program == 3 && location == 2)
            programs.at(bound_program).push[3] = float(value);
    }
    void glUniform1iv(GLint, GLsizei, const GLint *)
    {
    } // Fixed upstream sampler slots 1..6.
    void glUniform4fv(GLint location, GLsizei count, const GLfloat *values)
    {
        if (bound_program != 3 || location != 3 || count != 4)
            fail("UI color uniforms", VK_ERROR_INITIALIZATION_FAILED);
        std::memcpy(programs.at(bound_program).push.data() + 4, values, 64);
    }
    void glEnable(GLenum what)
    {
        if (what == GL_BLEND)
            blend = true;
        if (what == GL_SCISSOR_TEST)
            scissor = true;
    }
    void glDisable(GLenum what)
    {
        if (what == GL_BLEND)
            blend = false;
        if (what == GL_SCISSOR_TEST)
            scissor = false;
    }
    void glBlendFuncSeparate(GLenum sr, GLenum dr, GLenum sa, GLenum da)
    {
        if (sr != GL_SRC_ALPHA || dr != GL_ONE_MINUS_SRC_ALPHA || sa != GL_ONE ||
            da != GL_ONE_MINUS_SRC_ALPHA)
            fail("UI blend mode", VK_ERROR_FEATURE_NOT_PRESENT);
    }
    void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
    {
        view_x = x;
        view_y = y;
        view_width = w;
        view_height = h;
    }
    void glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
    {
        scissor_x = x;
        scissor_y = y;
        scissor_width = w;
        scissor_height = h;
    }
    void glDrawArrays(GLenum mode, GLint first, GLsizei count)
    {
        if (mode != GL_TRIANGLES)
            fail("UI topology", VK_ERROR_FEATURE_NOT_PRESENT);
        draw(first, count, 1, 0);
    }
    void glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count,
                                           GLsizei instances, GLuint base)
    {
        if (mode != GL_TRIANGLES)
            fail("UI topology", VK_ERROR_FEATURE_NOT_PRESENT);
        draw(first, count, instances, base);
    }
    void glGenFramebuffers(GLsizei n, GLuint *ids)
    {
        for (int i = 0; i < n; ++i)
        {
            ids[i] = serial++;
            framebuffers[ids[i]] = 0;
        }
    }
    void glBindFramebuffer(GLenum, GLuint id)
    {
        if (id != bound_framebuffer)
        {
            end_pass();
            bound_framebuffer = id;
        }
    }
    void glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint texture, GLint)
    {
        framebuffers.at(bound_framebuffer) = texture;
    }
    GLenum glCheckFramebufferStatus(GLenum)
    {
        return framebuffers.count(bound_framebuffer) &&
                       textures.count(framebuffers[bound_framebuffer])
                   ? GL_FRAMEBUFFER_COMPLETE
                   : GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }
    void glDeleteFramebuffers(GLsizei n, const GLuint *ids)
    {
        if (device)
            flush();
        for (int i = 0; i < n; ++i)
            framebuffers.erase(ids[i]);
    }
    void glGetIntegerv(GLenum name, GLint *value)
    {
        if (name == GL_MAX_SAMPLES)
            *value = 1;
        else
            fail("unsupported UI query", VK_ERROR_FEATURE_NOT_PRESENT);
    }
    void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
    {
        clear_color = {r, g, b, a};
    }
    void glClear(GLbitfield mask)
    {
        if (mask != GL_COLOR_BUFFER_BIT)
            fail("UI clear mask", VK_ERROR_FEATURE_NOT_PRESENT);
        begin_pass();
        auto &image = target();
        VkClearAttachment attachment{};
        attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        std::memcpy(attachment.clearValue.color.float32, clear_color.data(), 16);
        VkClearRect rect{{{0, 0}, {uint32_t(image.width), uint32_t(image.height)}}, 0, 1};
        vkCmdClearAttachments(command, 1, &attachment, 1, &rect);
    }
    void glGenRenderbuffers(GLsizei n, GLuint *ids)
    {
        glGenTextures(n, ids);
    }
    void glDeleteRenderbuffers(GLsizei n, const GLuint *ids)
    {
        glDeleteTextures(n, ids);
    }
    static GLuint bound_renderbuffer;
    void glBindRenderbuffer(GLenum, GLuint id)
    {
        bound_renderbuffer = id;
    }
    void glRenderbufferStorage(GLenum, GLenum, GLsizei w, GLsizei h)
    {
        auto &image = textures.at(bound_renderbuffer);
        image = make_image(w, h, VK_FORMAT_R8G8B8A8_UNORM);
        upload(image, nullptr);
    }
    void glRenderbufferStorageMultisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei)
    {
        fail("UI MSAA is not requested", VK_ERROR_FEATURE_NOT_PRESENT);
    }
    void glFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint id)
    {
        framebuffers.at(bound_framebuffer) = id;
    }
    void glBlitFramebuffer(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield,
                           GLenum)
    {
        fail("UI MSAA resolve is not requested", VK_ERROR_FEATURE_NOT_PRESENT);
    }
    void glCopyImageSubData(GLuint src, GLenum, GLint, GLint sx, GLint sy, GLint, GLuint dst,
                            GLenum, GLint, GLint dx, GLint dy, GLint, GLsizei w, GLsizei h, GLsizei)
    {
        auto &s = textures.at(src);
        auto &d = textures.at(dst);
        barrier(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        barrier(d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.srcOffset = {sx, sy, 0};
        copy.dstOffset = {dx, dy, 0};
        copy.extent = {uint32_t(w), uint32_t(h), 1};
        vkCmdCopyImage(command, s.image, s.layout, d.image, d.layout, 1, &copy);
        barrier(d, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *out)
    {
        if (type != GL_UNSIGNED_BYTE || (format != GL_RGBA && format != GL_RGB) || w <= 0 || h <= 0)
            fail("UI readback format", VK_ERROR_FEATURE_NOT_PRESENT);
        auto &image = target();
        Buffer b = make_buffer(size_t(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        barrier(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {x, image.display ? image.height - y - h : y, 0};
        copy.imageExtent = {uint32_t(w), uint32_t(h), 1};
        vkCmdCopyImageToBuffer(command, image.image, image.layout, b.buffer, 1, &copy);
        flush();
        auto *bytes = static_cast<unsigned char *>(out);
        auto *pixels = static_cast<unsigned char *>(b.mapped);
        unsigned channels = format == GL_RGB ? 3 : 4;
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
            {
                auto *s = pixels + (size_t(image.display ? h - 1 - row : row) * w + col) * 4;
                auto *d = bytes + (size_t(row) * w + col) * channels;
                d[0] = s[image.format == VK_FORMAT_B8G8R8A8_UNORM ? 2 : 0];
                d[1] = s[1];
                d[2] = s[image.format == VK_FORMAT_B8G8R8A8_UNORM ? 0 : 2];
                if (channels == 4)
                    d[3] = s[3];
            }
        destroy_buffer(b);
    }
    GLenum glGetError()
    {
        return GL_NO_ERROR;
    }
}
