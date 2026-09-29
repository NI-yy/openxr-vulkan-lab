#include <vulkan/vulkan.h>

#include <cstdio>
#include <vector>

static void print_version(uint32_t value) {
    std::printf("%u.%u.%u", VK_VERSION_MAJOR(value), VK_VERSION_MINOR(value), VK_VERSION_PATCH(value));
}

int main() {
    uint32_t instance_version = VK_API_VERSION_1_0;
    const VkResult version_result = vkEnumerateInstanceVersion(&instance_version);
    if (version_result != VK_SUCCESS) {
        std::fprintf(stderr, "vkEnumerateInstanceVersion failed: %d\n", version_result);
        return 1;
    }
    std::printf("instance_api_version=");
    print_version(instance_version);
    std::printf("\n");

    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (result != VK_SUCCESS) return 2;
    std::vector<VkExtensionProperties> instance_extensions(count);
    result = vkEnumerateInstanceExtensionProperties(nullptr, &count, instance_extensions.data());
    if (result != VK_SUCCESS) return 3;
    std::printf("instance_extension_count=%u\n", count);
    for (const auto& extension : instance_extensions) {
        std::printf("instance_extension=%s:%u\n", extension.extensionName, extension.specVersion);
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Quest Vulkan inventory";
    app_info.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    VkInstance instance = VK_NULL_HANDLE;
    result = vkCreateInstance(&create_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "vkCreateInstance failed: %d\n", result);
        return 4;
    }

    result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS) return 5;
    std::vector<VkPhysicalDevice> devices(count);
    result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (result != VK_SUCCESS) return 6;
    std::printf("physical_device_count=%u\n", count);
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        std::printf("device_%u_name=%s\n", i, properties.deviceName);
        std::printf("device_%u_api_version=", i);
        print_version(properties.apiVersion);
        std::printf("\n");
        std::printf("device_%u_driver_version=%u\n", i, properties.driverVersion);
        std::printf("device_%u_vendor_id=0x%04x\n", i, properties.vendorID);
        std::printf("device_%u_device_id=0x%04x\n", i, properties.deviceID);

        VkPhysicalDeviceDriverProperties driver_properties{};
        driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &driver_properties;
        vkGetPhysicalDeviceProperties2(devices[i], &properties2);
        std::printf("device_%u_driver_name=%s\n", i, driver_properties.driverName);
        std::printf("device_%u_driver_info=%s\n", i, driver_properties.driverInfo);

        uint32_t extension_count = 0;
        result = vkEnumerateDeviceExtensionProperties(devices[i], nullptr, &extension_count, nullptr);
        if (result != VK_SUCCESS) return 7;
        std::vector<VkExtensionProperties> extensions(extension_count);
        result = vkEnumerateDeviceExtensionProperties(devices[i], nullptr, &extension_count, extensions.data());
        if (result != VK_SUCCESS) return 8;
        std::printf("device_%u_extension_count=%u\n", i, extension_count);
        for (const auto& extension : extensions) {
            std::printf("device_%u_extension=%s:%u\n", i, extension.extensionName, extension.specVersion);
        }
    }
    vkDestroyInstance(instance, nullptr);
    return 0;
}
