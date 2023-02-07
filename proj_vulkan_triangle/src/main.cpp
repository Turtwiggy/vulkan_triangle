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
init_vulkan(const std::vector<const char*>& extensions,
            // instance
            VkInstance& instance,
            VkAllocationCallbacks* allocator,
            VkDebugReportCallbackEXT& reporter,
            // device
            VkPhysicalDevice& physical_device,
            VkDevice& device,
            std::optional<uint32_t>& queue_family)
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
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size() + 1);
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

  // Create local device...
  {
  }
}

void
loop()
{
  //
}

void
cleanup(VkInstance& instance, VkAllocationCallbacks* allocator, VkDebugReportCallbackEXT& reporter, VkDevice& device)
{
#ifdef _DEBUG
  // Remove the debug report callback
  auto vkDestroyDebugReportCallbackEXT =
    (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
  vkDestroyDebugReportCallbackEXT(instance, reporter, allocator);
#endif // _DEBUG

  vkDestroyDevice(device, allocator);
  vkDestroyInstance(instance, nullptr);
}

int
main(int, char**)
{
  if (!init_sdl2())
    return EXIT_FAILURE;
  auto window = init_sdl2_window({ "Vulkan App" }, 1200, 800);

  VkInstance instance = VK_NULL_HANDLE;
  VkAllocationCallbacks* allocator = NULL;
  VkDebugReportCallbackEXT reporter = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  std::optional<uint32_t> queue_family = std::nullopt;
  {
    uint32_t extensions_count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, NULL);
    std::vector<const char*> extensions_names(extensions_count);
    SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, extensions_names.data());
    init_vulkan(extensions_names, instance, allocator, reporter, physical_device, device, queue_family);
  }

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event))
      sdl2_handle_quit_event(event, running);
  }

  cleanup(instance, allocator, reporter, device);
  printf("shutdown...\n");
  return EXIT_SUCCESS;
}