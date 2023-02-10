// References:
// https://github.com/ocornut/imgui/blob/master/examples/example_sdl_vulkan/main.cpp

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <stdio.h>  // printf, fprintf
#include <stdlib.h> // abort
#include <string>
#include <vector>

// #define IMGUI_UNLIMITED_FRAME_RATE

void
check_vk_result(VkResult err)
{
  if (err == VK_SUCCESS)
    return;
  fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
  if (err < 0)
    abort();
}

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
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
sdl2_handle_quit_event(SDL_Window* window, const SDL_Event& event, bool& running)
{
  if (event.type == SDL_QUIT)
    running = false;

  if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
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
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
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
    auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
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
    IM_UNUSED(debug_report);
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
                    std::optional<uint32_t>& queue_family,
                    const int& min_image_count)
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
  wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(physical_device, wd->Surface, image_format, (size_t)IM_ARRAYSIZE(image_format), colour_space);

  // Select Present Mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
  VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
  VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
  wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physical_device, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
  printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

  // Create SwapChain, RenderPass, Framebuffer, etc.
  ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physical_device, device, wd, queue_family.value(), allocator, width, height, min_image_count);
}

void
cleanup_vulkan(VkInstance& instance, VkAllocationCallbacks* allocator, VkDebugReportCallbackEXT& reporter, VkDevice& device, VkDescriptorPool& descriptor_pool)
{
  vkDestroyDescriptorPool(device, descriptor_pool, allocator);

#ifdef _DEBUG
  // Remove the debug report callback
  auto vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
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
frame_render(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data, VkQueue& queue, VkDevice& device, bool& rebuild_swapchain)
{
  VkResult err;

  VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
  VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
  err = vkAcquireNextImageKHR(device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
  if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
    rebuild_swapchain = true;
    return;
  }
  check_vk_result(err);

  ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
  {
    // wait for previous frame to finish
    // wait indefinitely instead of periodically checking
    err = vkWaitForFences(device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
    check_vk_result(err);

    err = vkResetFences(device, 1, &fd->Fence);
    check_vk_result(err);
  }
  {
    err = vkResetCommandPool(device, fd->CommandPool, 0);
    check_vk_result(err);
    VkCommandBufferBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
    check_vk_result(err);
  }
  {
    VkRenderPassBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = wd->RenderPass;
    info.framebuffer = fd->Framebuffer;
    info.renderArea.extent.width = wd->Width;
    info.renderArea.extent.height = wd->Height;
    info.clearValueCount = 1;
    info.pClearValues = &wd->ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
  }

  // Record dear imgui primitives into command buffer
  ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

  // Submit command buffer
  vkCmdEndRenderPass(fd->CommandBuffer);
  {
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &image_acquired_semaphore;
    info.pWaitDstStageMask = &wait_stage;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &fd->CommandBuffer;
    info.signalSemaphoreCount = 1;
    info.pSignalSemaphores = &render_complete_semaphore;

    err = vkEndCommandBuffer(fd->CommandBuffer);
    check_vk_result(err);
    err = vkQueueSubmit(queue, 1, &info, fd->Fence);
    check_vk_result(err);
  }
}

void
frame_present(ImGui_ImplVulkanH_Window* wd, VkQueue& queue, bool& rebuild_swapchain)
{
  if (rebuild_swapchain)
    return;
  VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
  VkPresentInfoKHR info = {};
  info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &render_complete_semaphore;
  info.swapchainCount = 1;
  info.pSwapchains = &wd->Swapchain;
  info.pImageIndices = &wd->FrameIndex;
  VkResult err = vkQueuePresentKHR(queue, &info);
  if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
    rebuild_swapchain = true;
    return;
  }
  check_vk_result(err);
  wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores
}

int
main(int, char**)
{
  if (!init_sdl2())
    return EXIT_FAILURE;

    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

  // Setup Window
  auto window = init_sdl2_window({ "Triangle App" }, 1200, 800);

  // Graphics Context
  const int min_image_count = 2;
  VkInstance instance = VK_NULL_HANDLE;
  VkAllocationCallbacks* allocator = NULL;
  VkDebugReportCallbackEXT reporter = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  std::optional<uint32_t> queue_family = std::nullopt;
  VkQueue queue = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkSurfaceKHR surface;
  //
  ImGui_ImplVulkanH_Window main_window_data;
  VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
  bool rebuild_swapchain = false;

  {
    uint32_t extensions_count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, NULL);
    std::vector<const char*> extensions_names(extensions_count);
    SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, extensions_names.data());
    setup_vulkan(extensions_names, instance, allocator, reporter, physical_device, device, queue_family, queue, descriptor_pool);
  }

  // Create Window Surface
  if (SDL_Vulkan_CreateSurface(window, instance, &surface) == 0) {
    printf("Failed to create SDL_Vulkan surface.\n");
    return EXIT_FAILURE;
  }

  // Create framebuffers
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  setup_vulkan_window(&main_window_data, surface, w, h, instance, allocator, physical_device, device, queue_family, min_image_count);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
  // io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
  // io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable Docking
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsLight();

  // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
  ImGuiStyle& style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForVulkan(window);
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = instance;
  init_info.PhysicalDevice = physical_device;
  init_info.Device = device;
  init_info.QueueFamily = queue_family.value();
  init_info.Queue = queue;
  init_info.PipelineCache = pipeline_cache;
  init_info.DescriptorPool = descriptor_pool;
  init_info.Subpass = 0;
  init_info.MinImageCount = min_image_count;
  init_info.ImageCount = main_window_data.ImageCount;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.Allocator = allocator;
  init_info.CheckVkResultFn = check_vk_result;
  ImGui_ImplVulkan_Init(&init_info, main_window_data.RenderPass);

  // Upload Fonts
  {
    // Use any command queue
    VkCommandPool command_pool = main_window_data.Frames[main_window_data.FrameIndex].CommandPool;
    VkCommandBuffer command_buffer = main_window_data.Frames[main_window_data.FrameIndex].CommandBuffer;

    auto err = vkResetCommandPool(device, command_pool, 0);
    check_vk_result(err);
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(command_buffer, &begin_info);
    check_vk_result(err);

    ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

    VkSubmitInfo end_info = {};
    end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    end_info.commandBufferCount = 1;
    end_info.pCommandBuffers = &command_buffer;
    err = vkEndCommandBuffer(command_buffer);
    check_vk_result(err);
    err = vkQueueSubmit(queue, 1, &end_info, VK_NULL_HANDLE);
    check_vk_result(err);

    err = vkDeviceWaitIdle(device);
    check_vk_result(err);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }

  // State
  bool show_demo_window = true;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      sdl2_handle_quit_event(window, event, running);
    }

    // resize swap chain?
    if (rebuild_swapchain) {
      int width = 0;
      int height = 0;
      SDL_GetWindowSize(window, &width, &height);
      if (width > 0 && height > 0) {
        ImGui_ImplVulkan_SetMinImageCount(min_image_count);
        ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physical_device, device, &main_window_data, queue_family.value(), allocator, width, height, min_image_count);
        main_window_data.FrameIndex = 0;
        rebuild_swapchain = false;
      }
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Show demo window
    ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
    ImGui::Begin("Sample window");
    ImGui::Text("Hello, World!");
    ImGui::End();

    // Rendering
    {
      ImGui::Render();
      ImDrawData* draw_data = ImGui::GetDrawData();
      const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
      main_window_data.ClearValue.color.float32[0] = clear_color.x * clear_color.w;
      main_window_data.ClearValue.color.float32[1] = clear_color.y * clear_color.w;
      main_window_data.ClearValue.color.float32[2] = clear_color.z * clear_color.w;
      main_window_data.ClearValue.color.float32[3] = clear_color.w;

      if (!is_minimized)
        frame_render(&main_window_data, draw_data, queue, device, rebuild_swapchain);

      // Update and Render additional Platform Windows
      if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
      }

      // Present Main Platform Window
      if (!is_minimized)
        frame_present(&main_window_data, queue, rebuild_swapchain);
    }
  }

  // Cleanup
  auto err = vkDeviceWaitIdle(device);
  check_vk_result(err);
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  cleanup_vulkan_window(instance, device, main_window_data, allocator);
  cleanup_vulkan(instance, allocator, reporter, device, descriptor_pool);
  SDL_DestroyWindow(window);
  SDL_Quit();

  printf("shutdown...\n");
  return EXIT_SUCCESS;
}