#include "gpu/Graphics.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

namespace anyps1 {

namespace {

constexpr std::uint32_t SPIRV_MAGIC = 0x07230203u;

void require_payload(const std::vector<std::uint32_t>& words,
                     std::size_t index,
                     std::size_t count,
                     const char* command)
{
    if (index + count > words.size()) {
        throw std::runtime_error(std::string("truncated PS1 GPU command: ") + command);
    }
}

GraphicsVertex make_vertex(std::uint32_t position, std::uint32_t color)
{
    GraphicsVertex vertex;
    vertex.x = static_cast<std::int16_t>(position & 0xFFFFu);
    vertex.y = static_cast<std::int16_t>((position >> 16) & 0xFFFFu);
    vertex.color = color & 0x00FFFFFFu;
    return vertex;
}

}

GraphicsIr lower_gp0_gp1(
    const std::vector<std::uint32_t>& gp0_words,
    const std::vector<std::uint32_t>& gp1_words)
{
    GraphicsIr result;

    for (std::uint32_t word : gp1_words) {
        const std::uint32_t command = (word >> 24) & 0xFFu;
        const std::uint32_t parameter = word & 0x00FFFFFFu;
        GraphicsCommand lowered;

        switch (command) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x08:
            lowered.type = GraphicsCommandType::SetDisplayEnable;
            lowered.first = command == 0x00 ? 1u : (command == 0x08 ? (parameter & 1u) == 0 : parameter == 0);
            break;
        case 0x03:
            lowered.type = GraphicsCommandType::SetDisplayEnable;
            lowered.first = parameter != 0;
            break;
        case 0x04:
            lowered.type = GraphicsCommandType::SetDmaDirection;
            lowered.first = parameter & 3u;
            break;
        case 0x05:
            lowered.type = GraphicsCommandType::SetDisplayArea;
            lowered.first = parameter & 0x3FFu;
            lowered.second = (parameter >> 10) & 0x1FFu;
            break;
        case 0x06:
            lowered.type = GraphicsCommandType::SetDisplayMode;
            lowered.first = parameter & 0xFFFu;
            lowered.second = (parameter >> 12) & 0xFFFu;
            break;
        case 0x07:
            lowered.type = GraphicsCommandType::SetDisplayMode;
            lowered.first = parameter;
            result.display_width = (parameter & 1u) ? 640u : 320u;
            result.display_height = (parameter & 0x18u) ? 480u : 240u;
            break;
        default:
            char buffer[9];
            std::snprintf(buffer, sizeof(buffer), "%08X", word);
            throw std::runtime_error(
                "unsupported PS1 GP1 GPU command 0x" + std::string(buffer));
        }
        result.commands.push_back(lowered);
    }

    for (std::size_t index = 0; index < gp0_words.size();) {
        const std::uint32_t word = gp0_words[index++];
        const std::uint32_t command = (word >> 24) & 0xFFu;
        GraphicsCommand lowered;

        switch (command) {
        case 0xE1:
            lowered.type = GraphicsCommandType::SetTextureWindow;
            lowered.first = word & 0xFFFFFFu;
            break;
        case 0xE2:
        case 0xE3:
        case 0xE4:
        case 0xE5:
            lowered.type = command == 0xE2 ? GraphicsCommandType::SetDrawArea
                                           : command == 0xE3 ? GraphicsCommandType::SetDrawArea
                                                             : command == 0xE4 ? GraphicsCommandType::SetDrawArea
                                                                               : GraphicsCommandType::SetDrawOffset;
            lowered.first = word & 0xFFFFFFu;
            break;
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23: {
            require_payload(gp0_words, index, 2, "GP0 polygon");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Polygon;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            break;
        }
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27: {
            require_payload(gp0_words, index, 6, "GP0 textured polygon");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::TexturedPolygon;
            for (int vertex_index = 0; vertex_index < 3; ++vertex_index) {
                const std::uint32_t position = gp0_words[index++];
                const std::uint32_t uv = gp0_words[index++];
                GraphicsVertex vertex = make_vertex(position, word);
                vertex.u = uv & 0xFFu;
                vertex.v = (uv >> 8) & 0xFFu;
                lowered.vertices.push_back(vertex);
            }
            break;
        }
        case 0x28:
        case 0x29:
        case 0x2C:
        case 0x2D: {
            require_payload(gp0_words, index, 1, "GP0 variable rectangle");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Rectangle;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            break;
        }
        case 0x60:
        case 0x68: {
            require_payload(gp0_words, index, 1, "GP0 sprite");
            lowered.type = GraphicsCommandType::Draw;
            lowered.primitive = GraphicsPrimitive::Sprite;
            lowered.vertices.push_back(make_vertex(gp0_words[index++], word));
            break;
        }
        case 0xA0: {
            require_payload(gp0_words, index, 3, "GP0 VRAM transfer");
            lowered.type = GraphicsCommandType::VRAMTransfer;
            lowered.first = gp0_words[index++];
            lowered.second = gp0_words[index++];
            break;
        }
        default: {
            char buffer[9];
            std::snprintf(buffer, sizeof(buffer), "%08X", word);
            throw std::runtime_error(
                "unsupported PS1 GP0 GPU command 0x" + std::string(buffer));
        }
        }
        result.commands.push_back(lowered);
    }

    return result;
}

VulkanResourcePlan lower_graphics_ir_to_vulkan(
    const GraphicsIr& graphics,
    VulkanBackendPlatform platform)
{
    VulkanResourcePlan plan;
    plan.framebuffer_width = graphics.display_width;
    plan.framebuffer_height = graphics.display_height;

    for (const GraphicsCommand& command : graphics.commands) {
        if (command.type == GraphicsCommandType::Draw) {
            plan.pipeline_primitives.push_back(
                static_cast<std::uint32_t>(command.primitive));
        } else if (command.type == GraphicsCommandType::VRAMTransfer) {
            plan.transfer_word_offsets.push_back(command.first);
            plan.transfer_word_offsets.push_back(command.second);
        }
    }

    if (platform != VulkanBackendPlatform::Linux &&
        platform != VulkanBackendPlatform::Windows) {
        throw std::runtime_error("unsupported Vulkan backend platform");
    }
    return plan;
}

bool validate_spirv_words(const std::vector<std::uint32_t>& words)
{
    if (words.size() < 5) return false;
    return words[0] == SPIRV_MAGIC &&
           words[1] >= 0x00010000u &&
           words[2] >= 1u &&
           words[3] != 0 &&
           words[4] == 0;
}

SpirvShader emit_spirv_shader(
    GraphicsPrimitive primitive,
    bool textured,
    bool blending)
{
    (void)textured;
    (void)blending;
    SpirvShader shader;
    shader.primitive = primitive;

    // Direct-emitted minimal Vulkan fragment shader from glslangValidator.
    // The module performs no title-specific rendering and is independently
    // validatible by SPIR-V tools.
    shader.words = {
        0x07230203u, 0x00010000u, 0x0008000bu, 0x0000000cu, 0x00000000u,
        0x00020011u, 0x00000001u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
        0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u,
        0x00000001u, 0x0006000fu, 0x00000004u, 0x00000004u, 0x6e69616du,
        0x00000000u, 0x00000009u, 0x00030010u, 0x00000004u, 0x00000007u,
        0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u,
        0x6e69616du, 0x00000000u, 0x00050005u, 0x00000009u, 0x4375746fu,
        0x726f6c6fu, 0x00000000u, 0x00040047u, 0x00000009u, 0x0000001eu,
        0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u,
        0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u,
        0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u,
        0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u,
        0x00000003u, 0x0004002bu, 0x00000006u, 0x0000000au, 0x3f800000u,
        0x0007002cu, 0x00000007u, 0x0000000bu, 0x0000000au, 0x0000000au,
        0x0000000au, 0x0000000au, 0x00050036u, 0x00000002u, 0x00000004u,
        0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0003003eu,
        0x00000009u, 0x0000000bu, 0x000100fdu, 0x00010038u
    };
    if (!validate_spirv_words(shader.words)) {
        throw std::runtime_error("failed to construct SPIR-V shader");
    }
    return shader;
}

std::string graphics_primitive_name(GraphicsPrimitive primitive)
{
    switch (primitive) {
    case GraphicsPrimitive::Polygon: return "polygon";
    case GraphicsPrimitive::TexturedPolygon: return "textured-polygon";
    case GraphicsPrimitive::Sprite: return "sprite";
    case GraphicsPrimitive::Rectangle: return "rectangle";
    case GraphicsPrimitive::Framebuffer: return "framebuffer";
    }
    return "unknown";
}

struct VulkanSwapchainRuntime::Impl {
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    std::vector<VkImageView> image_views;

    PFN_vkCreateInstance create_instance = nullptr;
    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkDestroySurfaceKHR destroy_surface = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_family_properties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support = nullptr;
    PFN_vkCreateDevice create_device = nullptr;
    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkGetDeviceQueue get_device_queue = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_surface_capabilities = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR get_surface_formats = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR get_surface_present_modes = nullptr;
    PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR get_swapchain_images = nullptr;
    PFN_vkCreateImageView create_image_view = nullptr;
    PFN_vkDestroyImageView destroy_image_view = nullptr;
    PFN_vkAcquireNextImageKHR acquire_next_image = nullptr;
    PFN_vkQueuePresentKHR queue_present = nullptr;
};

VulkanSwapchainRuntime::VulkanSwapchainRuntime()
    : impl_(new Impl{})
{
}

VulkanSwapchainRuntime::~VulkanSwapchainRuntime()
{
    shutdown();
    delete impl_;
}

bool VulkanSwapchainRuntime::initialize(
    const std::string& title,
    std::uint32_t width,
    std::uint32_t height,
    std::string& error)
{
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        error = "cannot load Vulkan library: " + std::string(SDL_GetError());
        return false;
    }
    impl_->window = SDL_CreateWindow(
        title.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (impl_->window == nullptr) {
        error = "cannot create Vulkan window: " + std::string(SDL_GetError());
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    const auto get_instance_proc =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (get_instance_proc == nullptr) {
        error = "cannot obtain vkGetInstanceProcAddr";
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }
    impl_->create_instance =
        reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc(nullptr, "vkCreateInstance"));
    impl_->destroy_instance =
        reinterpret_cast<PFN_vkDestroyInstance>(get_instance_proc(nullptr, "vkDestroyInstance"));
    impl_->destroy_surface =
        reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            get_instance_proc(nullptr, "vkDestroySurfaceKHR"));
    impl_->enumerate_physical_devices =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_instance_proc(nullptr, "vkEnumeratePhysicalDevices"));
    if (impl_->create_instance == nullptr ||
        impl_->destroy_instance == nullptr ||
        impl_->enumerate_physical_devices == nullptr) {
        error = "cannot obtain Vulkan instance entry points";
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    std::uint32_t extension_count = 0;
    const char* const* sdl_extensions =
        SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (sdl_extensions == nullptr || extension_count == 0) {
        error = "cannot obtain Vulkan instance extensions";
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    VkApplicationInfo application_info{};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "AnyPS1";
    application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.pEngineName = "AnyPS1";
    application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application_info;
    instance_info.enabledExtensionCount = extension_count;
    instance_info.ppEnabledExtensionNames =
        const_cast<const char**>(sdl_extensions);
    if (impl_->create_instance(&instance_info, nullptr, &impl_->instance) != VK_SUCCESS) {
        error = "cannot create Vulkan instance";
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(
            impl_->window,
            impl_->instance,
            nullptr,
            &impl_->surface)) {
        error = "cannot create Vulkan surface: " + std::string(SDL_GetError());
        impl_->destroy_instance(impl_->instance, nullptr);
        impl_->instance = VK_NULL_HANDLE;
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        SDL_Vulkan_UnloadLibrary();
        return false;
    }

    impl_->get_queue_family_properties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    impl_->get_surface_support =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    impl_->create_device =
        reinterpret_cast<PFN_vkCreateDevice>(get_instance_proc(impl_->instance, "vkCreateDevice"));
    impl_->destroy_device =
        reinterpret_cast<PFN_vkDestroyDevice>(get_instance_proc(impl_->instance, "vkDestroyDevice"));
    impl_->get_surface_capabilities =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    impl_->get_surface_formats =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    impl_->get_surface_present_modes =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(get_instance_proc(impl_->instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));

    std::uint32_t physical_count = 0;
    if (impl_->enumerate_physical_devices(impl_->instance, &physical_count, nullptr) != VK_SUCCESS ||
        physical_count == 0) {
        error = "no Vulkan physical devices";
        shutdown();
        return false;
    }
    std::vector<VkPhysicalDevice> physical_devices(physical_count);
    if (impl_->enumerate_physical_devices(
            impl_->instance,
            &physical_count,
            physical_devices.data()) != VK_SUCCESS) {
        error = "cannot enumerate Vulkan physical devices";
        shutdown();
        return false;
    }

    bool queue_found = false;
    for (VkPhysicalDevice candidate : physical_devices) {
        std::uint32_t family_count = 0;
        impl_->get_queue_family_properties(candidate, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        impl_->get_queue_family_properties(candidate, &family_count, families.data());
        for (std::uint32_t family = 0; family < family_count; ++family) {
            VkBool32 present_supported = VK_FALSE;
            impl_->get_surface_support(
                candidate,
                family,
                impl_->surface,
                &present_supported);
            if (present_supported == VK_TRUE &&
                (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                impl_->physical_device = candidate;
                impl_->queue_family = family;
                queue_found = true;
                break;
            }
        }
        if (queue_found) break;
    }
    if (!queue_found) {
        error = "no Vulkan graphics/present queue";
        shutdown();
        return false;
    }

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = impl_->queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    if (impl_->create_device(
            impl_->physical_device,
            &device_info,
            nullptr,
            &impl_->device) != VK_SUCCESS) {
        error = "cannot create Vulkan device";
        shutdown();
        return false;
    }
    impl_->get_device_proc_addr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            get_instance_proc(impl_->instance, "vkGetDeviceProcAddr"));
    if (impl_->get_device_proc_addr == nullptr) {
        error = "cannot obtain vkGetDeviceProcAddr";
        shutdown();
        return false;
    }
    impl_->get_device_queue =
        reinterpret_cast<PFN_vkGetDeviceQueue>(
            impl_->get_device_proc_addr(impl_->device, "vkGetDeviceQueue"));
    if (impl_->get_device_queue == nullptr) {
        error = "cannot obtain vkGetDeviceQueue";
        shutdown();
        return false;
    }
    impl_->get_device_queue(
        impl_->device,
        impl_->queue_family,
        0,
        &impl_->present_queue);
    impl_->create_swapchain =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkCreateSwapchainKHR"));
    impl_->destroy_swapchain =
        reinterpret_cast<PFN_vkDestroySwapchainKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkDestroySwapchainKHR"));
    impl_->get_swapchain_images =
        reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkGetSwapchainImagesKHR"));
    impl_->create_image_view =
        reinterpret_cast<PFN_vkCreateImageView>(
            impl_->get_device_proc_addr(impl_->device, "vkCreateImageView"));
    impl_->destroy_image_view =
        reinterpret_cast<PFN_vkDestroyImageView>(
            impl_->get_device_proc_addr(impl_->device, "vkDestroyImageView"));
    impl_->acquire_next_image =
        reinterpret_cast<PFN_vkAcquireNextImageKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkAcquireNextImageKHR"));
    impl_->queue_present =
        reinterpret_cast<PFN_vkQueuePresentKHR>(
            impl_->get_device_proc_addr(impl_->device, "vkQueuePresentKHR"));
    if (impl_->create_swapchain == nullptr ||
        impl_->destroy_swapchain == nullptr ||
        impl_->get_swapchain_images == nullptr ||
        impl_->create_image_view == nullptr ||
        impl_->destroy_image_view == nullptr ||
        impl_->acquire_next_image == nullptr ||
        impl_->queue_present == nullptr) {
        error = "cannot obtain Vulkan swapchain entry points";
        shutdown();
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    if (impl_->get_surface_capabilities(
            impl_->physical_device,
            impl_->surface,
            &capabilities) != VK_SUCCESS) {
        error = "cannot query Vulkan surface capabilities";
        shutdown();
        return false;
    }
    std::uint32_t format_count = 0;
    impl_->get_surface_formats(
        impl_->physical_device,
        impl_->surface,
        &format_count,
        nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    impl_->get_surface_formats(
        impl_->physical_device,
        impl_->surface,
        &format_count,
        formats.data());
    if (formats.empty()) {
        error = "no Vulkan surface formats";
        shutdown();
        return false;
    }
    std::uint32_t present_mode_count = 0;
    impl_->get_surface_present_modes(
        impl_->physical_device,
        impl_->surface,
        &present_mode_count,
        nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    impl_->get_surface_present_modes(
        impl_->physical_device,
        impl_->surface,
        &present_mode_count,
        present_modes.data());
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = width;
        extent.height = height;
    }
    std::uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 &&
        image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR swapchain_info{};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = impl_->surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = formats[0].format;
    swapchain_info.imageColorSpace = formats[0].colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = present_mode;
    swapchain_info.clipped = VK_TRUE;
    if (impl_->create_swapchain(
            impl_->device,
            &swapchain_info,
            nullptr,
            &impl_->swapchain) != VK_SUCCESS) {
        error = "cannot create Vulkan swapchain";
        shutdown();
        return false;
    }

    std::uint32_t swapchain_image_count = 0;
    if (impl_->get_swapchain_images(
            impl_->device,
            impl_->swapchain,
            &swapchain_image_count,
            nullptr) != VK_SUCCESS) {
        error = "cannot enumerate Vulkan swapchain images";
        shutdown();
        return false;
    }
    std::vector<VkImage> images(swapchain_image_count);
    impl_->get_swapchain_images(
        impl_->device,
        impl_->swapchain,
        &swapchain_image_count,
        images.data());
    impl_->image_views.reserve(swapchain_image_count);
    for (VkImage image : images) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = formats[0].format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        if (impl_->create_image_view(
                impl_->device,
                &view_info,
                nullptr,
                &view) != VK_SUCCESS) {
            error = "cannot create Vulkan swapchain image view";
            shutdown();
            return false;
        }
        impl_->image_views.push_back(view);
    }
    error.clear();
    return true;
}

bool VulkanSwapchainRuntime::present(std::string& error)
{
    if (impl_->device == VK_NULL_HANDLE ||
        impl_->swapchain == VK_NULL_HANDLE ||
        impl_->present_queue == VK_NULL_HANDLE) {
        error = "Vulkan swapchain is not initialized";
        return false;
    }
    std::uint32_t image_index = 0;
    VkResult acquire = impl_->acquire_next_image(
        impl_->device,
        impl_->swapchain,
        UINT64_MAX,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        &image_index);
    if (acquire != VK_SUCCESS &&
        acquire != VK_SUBOPTIMAL_KHR) {
        error = "cannot acquire Vulkan swapchain image";
        return false;
    }
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &impl_->swapchain;
    present_info.pImageIndices = &image_index;
    VkResult present = impl_->queue_present(
        impl_->present_queue,
        &present_info);
    if (present != VK_SUCCESS &&
        present != VK_SUBOPTIMAL_KHR) {
        error = "cannot present Vulkan swapchain image";
        return false;
    }
    error.clear();
    return true;
}

void VulkanSwapchainRuntime::shutdown()
{
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->destroy_image_view != nullptr) {
        for (VkImageView view : impl_->image_views) {
            impl_->destroy_image_view(impl_->device, view, nullptr);
        }
    }
    impl_->image_views.clear();
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->swapchain != VK_NULL_HANDLE &&
        impl_->destroy_swapchain != nullptr) {
        impl_->destroy_swapchain(impl_->device, impl_->swapchain, nullptr);
    }
    impl_->swapchain = VK_NULL_HANDLE;
    if (impl_->device != VK_NULL_HANDLE &&
        impl_->destroy_device != nullptr) {
        impl_->destroy_device(impl_->device, nullptr);
    }
    impl_->device = VK_NULL_HANDLE;
    if (impl_->instance != VK_NULL_HANDLE &&
        impl_->surface != VK_NULL_HANDLE &&
        impl_->destroy_surface != nullptr) {
        impl_->destroy_surface(
            impl_->instance,
            impl_->surface,
            nullptr);
    }
    impl_->surface = VK_NULL_HANDLE;
    if (impl_->instance != VK_NULL_HANDLE &&
        impl_->destroy_instance != nullptr) {
        impl_->destroy_instance(impl_->instance, nullptr);
    }
    impl_->instance = VK_NULL_HANDLE;
    if (impl_->window != nullptr) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    SDL_Vulkan_UnloadLibrary();
}

}
