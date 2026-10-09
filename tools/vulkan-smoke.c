// SPDX-License-Identifier: GPL-3.0-or-later
// Console startup and device discovery, independent of the model runtime.
#include <stdio.h>
#include <vulkan/vulkan.h>

extern PFN_vkVoidFunction radv_GetInstanceProcAddr(VkInstance, const char *);
extern int sceKernelDebugOutText(int, const char *);
extern int sceKernelUsleep(unsigned int);
extern int sceSystemServiceLoadExec(const char *, const char *const *);

int main(void)
{
    sceKernelDebugOutText(0, "[ProsperoVK] main entered\n");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)radv_GetInstanceProcAddr(NULL, "vkCreateInstance");
    VkInstance instance = VK_NULL_HANDLE;
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ProsperoAI Vulkan smoke", .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkResult result = create ? create(&ci, NULL, &instance) : VK_ERROR_INITIALIZATION_FAILED;
    char line[512];
    snprintf(line, sizeof(line), "[ProsperoVK] vkCreateInstance=%d instance=%p\n", result, (void *)instance);
    sceKernelDebugOutText(0, line);
    if (result == VK_SUCCESS) {
        PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)radv_GetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
        PFN_vkGetPhysicalDeviceProperties properties = (PFN_vkGetPhysicalDeviceProperties)radv_GetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
        PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)radv_GetInstanceProcAddr(instance, "vkDestroyInstance");
        uint32_t count = 0;
        result = enumerate ? enumerate(instance, &count, NULL) : VK_ERROR_INITIALIZATION_FAILED;
        snprintf(line, sizeof(line), "[ProsperoVK] enumerate=%d count=%u\n", result, count);
        sceKernelDebugOutText(0, line);
        if (result == VK_SUCCESS && count && properties) {
            VkPhysicalDevice devices[8];
            if (count > 8) count = 8;
            result = enumerate(instance, &count, devices);
            if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
                for (uint32_t i = 0; i < count; ++i) {
                    VkPhysicalDeviceProperties p;
                    properties(devices[i], &p);
                    snprintf(line, sizeof(line), "[ProsperoVK] GPU=%s vendor=%04x device=%04x Vulkan=%u.%u.%u\n",
                        p.deviceName, p.vendorID, p.deviceID, VK_VERSION_MAJOR(p.apiVersion),
                        VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion));
                    sceKernelDebugOutText(0, line);
                }
            }
        }
        if (destroy) destroy(instance, NULL);
    }
    sceKernelDebugOutText(0, "[ProsperoVK] discovery complete; waiting 30 seconds\n");
    for (int i = 0; i < 300; ++i) sceKernelUsleep(100000);
    sceKernelDebugOutText(0, "[ProsperoVK] requesting shell exit\n");
    sceSystemServiceLoadExec("exit", NULL);
    for (;;) sceKernelUsleep(100000);
}
