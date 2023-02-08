// References:
// https://github.com/ocornut/imgui/blob/master/examples/example_sdl_vulkan/main.cpp

#include "vulkan.hpp"
using namespace project;

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_vulkan.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <stdio.h> // printf, fprintf
#include <string>
#include <vector>

const int g_MinImageCount = 2;

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_report(VkDebugReportFlagsEXT flags,
             VkDebugReportObjectTypeEXT objectType,
             uint64_t object,
             size_t location,
             int32_t messageCode,
             const char* pLayerPrefix,
             const char* pMessage,
             void* pUserData)
{
  (void)flags;
  (void)object;
  (void)location;
  (void)messageCode;
  (void)pUserData;
  (void)pLayerPrefix; // Unused arguments
  fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
  return VK_FALSE;
}
#endif // _DEBUG

bool
init_sdl2()
{
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return false;
  }
  printf("(init) sdl2 success\n");
  return true;
}

SDL_Window*
init_sdl2_window(const std::string& window, int32_t x, int32_t y)
{
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  return SDL_CreateWindow(window.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, x, y, window_flags);
}

void
sdl2_handle_quit_event(const SDL_Event& event, bool& running)
{
  if (event.type == SDL_QUIT)
    running = false;

  // note: if multiple windows used, check for
  // event.window.windowID == SDL_GetWindowID(window))
  if (event.window.event == SDL_WINDOWEVENT_CLOSE)
    running = false;

  if (event.type == SDL_KEYDOWN) {
    switch (event.key.keysym.sym) {
      case SDLK_ESCAPE:
        running = false;
        break;
    }
  }
}

void
setup_vulkan(const std::vector<const char*>& extensions,
             // instance
             VkInstance& instance,
             VkAllocationCallbacks* allocator,
             VkDebugReportCallbackEXT& reporter,
             // device
             VkPhysicalDevice& physical_device,
             VkDevice& device,
             std::optional<uint32_t>& queue_family,
             VkQueue& queue,
             VkDescriptorPool& descriptor_pool)
{
  VkResult err;

  // Create vulkan instance
  {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Vulkan App";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
#ifdef _DEBUG
    // Enabling validation layers
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    create_info.enabledLayerCount = 1;
    create_info.ppEnabledLayerNames = layers;
    // Enable debug report extension
    // (we need additional storage, so we duplicate the user array to add our new extension to it)
    std::vector<const char*> extensions_ext(extensions.size() + 1);
    std::copy(extensions.begin(), extensions.end(), extensions_ext.begin());
    extensions_ext[extensions.size()] = "VK_EXT_debug_report";
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions_ext.size());
    create_info.ppEnabledExtensionNames = extensions_ext.data();

    // Create vulkan instance
    err = vkCreateInstance(&create_info, allocator, &instance);
    check_vk_result(err);

    // Get the function pointer (required for any extensions)
    auto vkCreateDebugReportCallbackEXT =
      (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
    IM_ASSERT(vkCreateDebugReportCallbackEXT != NULL);

    // Setup the debug report callback
    VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
    debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    debug_report_ci.pfnCallback = debug_report;
    debug_report_ci.pUserData = NULL;
    err = vkCreateDebugReportCallbackEXT(instance, &debug_report_ci, allocator, &reporter);
    check_vk_result(err);
#else
    // Create Vulkan Instance without any debug feature
    err = vkCreateInstance(&create_info, allocator, &instance);
    check_vk_result(err);
#endif
  }

  // Select GPU
  {
    uint32_t device_count = 0;
    err = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    check_vk_result(err);
    IM_ASSERT(device_count > 0);

    std::vector<VkPhysicalDevice> devices(device_count);
    err = vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    check_vk_result(err);

    // If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available.
    // This covers most common cases (multi-gpu/integrated+dedicated graphics)
    // Handling more complicated setups (multiple dedicated GPUs) is out of scope of this sample.
    for (const auto& d : devices) {
      VkPhysicalDeviceProperties device_properties;
      VkPhysicalDeviceFeatures device_features;
      vkGetPhysicalDeviceProperties(d, &device_properties);
      vkGetPhysicalDeviceFeatures(d, &device_features);

      if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && device_features.geometryShader) {
        physical_device = d;
        break;
      }
    }
  }

  // Select graphics queue family
  {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, NULL);
    std::vector<VkQueueFamilyProperties> queue_families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queue_families.data());

    for (uint32_t i = 0; i < count; i++) {
      if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        queue_family = i;
        break;
      }
    }
  }

  // Create logical device (with 1 queue)
  {
    int device_extension_count = 1;
    const char* device_extensions[] = { "VK_KHR_swapchain" };
    const float queue_priority[] = { 1.0f };
    VkDeviceQueueCreateInfo queue_info[1] = {};
    queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info[0].queueFamilyIndex = queue_family.value();
    queue_info[0].queueCount = 1;
    queue_info[0].pQueuePriorities = queue_priority;
    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
    create_info.pQueueCreateInfos = queue_info;
    create_info.enabledExtensionCount = device_extension_count;
    create_info.ppEnabledExtensionNames = device_extensions;
    err = vkCreateDevice(physical_device, &create_info, allocator, &device);
    check_vk_result(err);
    vkGetDeviceQueue(device, queue_family.value(), 0, &queue);
  }

  // Create descriptor pool
  {
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
                                          { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                                          { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
                                          { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
                                          { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
                                          { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
                                          { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
                                          { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
                                          { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
                                          { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
                                          { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    err = vkCreateDescriptorPool(device, &pool_info, allocator, &descriptor_pool);
    check_vk_result(err);
  }
}

void
setup_vulkan_window(ImGui_ImplVulkanH_Window* wd,
                    VkSurfaceKHR surface,
                    uint32_t width,
                    uint32_t height,
                    VkInstance& instance,
                    VkAllocationCallbacks* allocator,
                    VkPhysicalDevice& physical_device,
                    VkDevice& device,
                    std::optional<uint32_t>& queue_family)
{
  wd->Surface = surface;

  // Check for WSI support
  VkBool32 res;
  vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family.value(), wd->Surface, &res);
  if (res != VK_TRUE) {
    fprintf(stderr, "Error no WSI support on physical device 0\n");
    exit(-1);
  }

  // Select Surface Format
  const VkFormat image_format[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
  const VkColorSpaceKHR colour_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
  wd->SurfaceFormat =
    ImGui_ImplVulkanH_SelectSurfaceFormat(physical_device, wd->Surface, image_format, (size_t)IM_ARRAYSIZE(image_format), colour_space);

  // Select Present Mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
  VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
  VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
  wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physical_device, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
  // printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

  // Create SwapChain, RenderPass, Framebuffer, etc.
  IM_ASSERT(g_MinImageCount >= 2);
  ImGui_ImplVulkanH_CreateOrResizeWindow(
    instance, physical_device, device, wd, queue_family.value(), allocator, width, height, g_MinImageCount);
}

void
cleanup_vulkan(VkInstance& instance,
               VkAllocationCallbacks* allocator,
               VkDebugReportCallbackEXT& reporter,
               VkDevice& device,
               VkDescriptorPool& descriptor_pool)
{
  vkDestroyDescriptorPool(device, descriptor_pool, allocator);

#ifdef _DEBUG
  // Remove the debug report callback
  auto vkDestroyDebugReportCallbackEXT =
    (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
  vkDestroyDebugReportCallbackEXT(instance, reporter, allocator);
#endif // _DEBUG

  vkDestroyDevice(device, allocator);
  vkDestroyInstance(instance, nullptr);
}

void
cleanup_vulkan_window(VkInstance& instance, VkDevice& device, ImGui_ImplVulkanH_Window& main_window_data, VkAllocationCallbacks* allocator)
{
  ImGui_ImplVulkanH_DestroyWindow(instance, device, &main_window_data, allocator);
}

void
loop()
{
  //
}

int
main(int, char**)
{
  if (!init_sdl2())
    return EXIT_FAILURE;

  // Setup Window
  auto window = init_sdl2_window({ "Vulkan App" }, 1200, 800);

  // Graphics Context
  VkInstance instance = VK_NULL_HANDLE;
  VkAllocationCallbacks* allocator = NULL;
  VkDebugReportCallbackEXT reporter = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  std::optional<uint32_t> queue_family = std::nullopt;
  VkQueue queue = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  {
    uint32_t extensions_count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, NULL);
    std::vector<const char*> extensions_names(extensions_count);
    SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, extensions_names.data());
    setup_vulkan(extensions_names, instance, allocator, reporter, physical_device, device, queue_family, queue, descriptor_pool);
  }

  // Create Window Surface
  VkSurfaceKHR surface;
  if (SDL_Vulkan_CreateSurface(window, instance, &surface) == 0) {
    printf("Failed to create SDL_Vulkan surface.\n");
    return EXIT_FAILURE;
  }

  // Create framebuffers
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  ImGui_ImplVulkanH_Window main_window_data;
  ImGui_ImplVulkanH_Window* wd = &main_window_data;
  setup_vulkan_window(wd, surface, w, h, instance, allocator, physical_device, device, queue_family);

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event))
      sdl2_handle_quit_event(event, running);
    loop();
  }

  cleanup_vulkan_window(instance, device, main_window_data, allocator);
  cleanup_vulkan(instance, allocator, reporter, device, descriptor_pool);
  printf("shutdown...\n");
  return EXIT_SUCCESS;
}