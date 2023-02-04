// References:
// https://github.com/ocornut/imgui/blob/master/examples/example_sdl_vulkan/main.cpp

#include "vulkan.hpp"

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_vulkan.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

bool
init_sdl2()
{
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return false;
  }

  std::cout << "(init) sdl2 success\n";
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

VkInstance
init_vulkan(SDL_Window* window)
{
  uint32_t extensions_count = 0;
  SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, NULL);
  const char** extensions = new const char*[extensions_count];
  SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, extensions);

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
  create_info.enabledExtensionCount = extensions_count;
  create_info.ppEnabledExtensionNames = extensions;

  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS)
    throw std::runtime_error("failed to create instance!");
  std::cout << "(init) vulkan success\n";

  delete[] extensions;

  return instance;
}

void
loop()
{
  //
}

void
cleanup(VkInstance& instance)
{
  vkDestroyInstance(instance, nullptr);
}

int
main(int, char**)
{
  if (!init_sdl2())
    return EXIT_FAILURE;
  const std::string name = "Vulkan App";
  const int32_t w = 1200;
  const int32_t h = 800;
  auto window = init_sdl2_window(name, w, h);
  auto vk_instance = init_vulkan(window);

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event))
      sdl2_handle_quit_event(event, running);
  }

  cleanup(vk_instance);
  std::cout << "shutdown...\n";

  return EXIT_SUCCESS;
}