#define VK_NO_PROTOTYPES
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Physical-device queries only: no VkDevice, images, queues, or submissions. */
static const char *result_name(VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default: return "other";
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/libMoltenVK.dylib\n", argv[0]);
        return 2;
    }
    const char *heap_setting = getenv("MVK_CONFIG_USE_MTLHEAP");
    printf("MVK_CONFIG_USE_MTLHEAP=%s\n", heap_setting ? heap_setting : "<unset: library default>");
    puts("Scope: instance creation and physical-device queries only");
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    if (!get_proc) { fputs("vkGetInstanceProcAddr unavailable\n", stderr); return 1; }
    PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)get_proc(VK_NULL_HANDLE, "vkCreateInstance");
    if (!create_instance) { fputs("vkCreateInstance unavailable\n", stderr); return 1; }
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Bridgeplay capability query", .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create_instance(&create_info, NULL, &instance);
    printf("vkCreateInstance: %s (%d)\n", result_name(result), result);
    if (result != VK_SUCCESS) { dlclose(library); return 1; }
#define LOAD(name) PFN_##name name = (PFN_##name)get_proc(instance, #name); \
    if (!name) { fprintf(stderr, "missing %s\n", #name); return 1; }
    LOAD(vkDestroyInstance);
    LOAD(vkEnumeratePhysicalDevices);
    LOAD(vkGetPhysicalDeviceProperties);
    LOAD(vkGetPhysicalDeviceFormatProperties);
    LOAD(vkGetPhysicalDeviceImageFormatProperties);
    LOAD(vkGetPhysicalDeviceFeatures2);
    LOAD(vkEnumerateDeviceExtensionProperties);
#undef LOAD
    uint32_t count = 0;
    result = vkEnumeratePhysicalDevices(instance, &count, NULL);
    if (result != VK_SUCCESS || !count) {
        fprintf(stderr, "physical device enumeration: %d; count=%u\n", result, count);
        vkDestroyInstance(instance, NULL); dlclose(library); return 1;
    }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) { vkDestroyInstance(instance, NULL); dlclose(library); return 1; }
    result = vkEnumeratePhysicalDevices(instance, &count, devices);
    if (result != VK_SUCCESS) { free(devices); vkDestroyInstance(instance, NULL); dlclose(library); return 1; }
    const VkFormat formats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
    const char *format_names[] = { "BGRA8_UNORM", "RGBA8_UNORM" };
    const VkImageCreateFlags flags[] = { 0, VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT,
        VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT };
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (uint32_t d = 0; d < count; ++d) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[d], &properties);
        printf("\nDevice: %s; vendor=0x%x device=0x%x Vulkan=%u.%u.%u driver=0x%x\n",
            properties.deviceName, properties.vendorID, properties.deviceID,
            VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion),
            VK_API_VERSION_PATCH(properties.apiVersion), properties.driverVersion);
        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &extension_count, NULL);
        VkExtensionProperties *extensions = calloc(extension_count, sizeof(*extensions));
        VkBool32 has_portability = VK_FALSE;
        if (extensions && vkEnumerateDeviceExtensionProperties(devices[d], NULL, &extension_count, extensions) == VK_SUCCESS)
            for (uint32_t e = 0; e < extension_count; ++e)
                if (!strcmp(extensions[e].extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) has_portability = VK_TRUE;
        free(extensions);
        VkPhysicalDevicePortabilitySubsetFeaturesKHR portability = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR };
        VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = has_portability ? &portability : NULL };
        vkGetPhysicalDeviceFeatures2(devices[d], &features);
        printf("portabilitySubset=%u; imageView2DOn3DImage=%u; imageViewFormatReinterpretation=%u\n",
            has_portability, portability.imageView2DOn3DImage, portability.imageViewFormatReinterpretation);
        for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
            VkFormatProperties format_properties;
            vkGetPhysicalDeviceFormatProperties(devices[d], formats[f], &format_properties);
            printf("%s format=%d optimalFeatures=0x%x\n", format_names[f], formats[f], format_properties.optimalTilingFeatures);
            for (unsigned c = 0; c < sizeof(flags) / sizeof(flags[0]); ++c) {
                VkImageFormatProperties image_properties = {0};
                result = vkGetPhysicalDeviceImageFormatProperties(devices[d], formats[f], VK_IMAGE_TYPE_3D,
                    VK_IMAGE_TILING_OPTIMAL, usage, flags[c], &image_properties);
                printf("  type=3D tiling=OPTIMAL usage=0x%x flags=0x%x: %s (%d)",
                    usage, flags[c], result_name(result), result);
                if (result == VK_SUCCESS)
                    printf("; maxExtent=%ux%ux%u maxMipLevels=%u maxArrayLayers=%u sampleCounts=0x%x maxResourceSize=%llu",
                        image_properties.maxExtent.width, image_properties.maxExtent.height, image_properties.maxExtent.depth,
                        image_properties.maxMipLevels, image_properties.maxArrayLayers, image_properties.sampleCounts,
                        (unsigned long long)image_properties.maxResourceSize);
                putchar('\n');
            }
        }
    }
    free(devices);
    vkDestroyInstance(instance, NULL);
    dlclose(library);
    puts("\nComplete: no logical device, image, queue, or command buffer was created.");
    return 0;
}
