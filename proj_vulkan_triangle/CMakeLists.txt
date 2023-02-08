# this cmake lists compiles the proj_vulkan_triangle with the engine

cmake_minimum_required(VERSION 3.0.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
  add_definitions("-std=c++20")
  add_definitions("-Wall")
  add_definitions("-Wformat")
  add_definitions("-O2")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES MSVC)
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Bt+")
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /d1reportTime") # debug compiler frontend???
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /d2cgsummary") # debug compiler backend???
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}/WX /W3 /wd4275 /wd4251")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /fp:fast")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++20 /permissive-")
endif()

project(proj_vulkan_triangle VERSION 0.1.0)
message("proj_vulkan_triangle: CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}")
message("proj_vulkan_triangle: CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE}")
message("proj_vulkan_triangle: C: ${CMAKE_C_COMPILER_ID}")
message("proj_vulkan_triangle: CXX: ${CMAKE_CXX_COMPILER_ID}")
message("proj_vulkan_triangle: current_list_dir: ${CMAKE_CURRENT_LIST_DIR}")
message("proj_vulkan_triangle: source_dir: ${CMAKE_SOURCE_DIR}")
message("index CMAKE_TOOLCHAIN_FILE: ${CMAKE_TOOLCHAIN_FILE}")

# Add VCPKG packages
find_package(glm CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
include(${CMAKE_SOURCE_DIR}/cmake/imgui.cmake)

# Add source files
file(GLOB_RECURSE SRC_FILES
  ${IMGUI_SOURCE}
  ${CMAKE_SOURCE_DIR}/proj_vulkan_triangle/src/*.cpp
)

add_executable(proj_vulkan_triangle
  ${SRC_FILES}
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  set(VCPKG_INCLUDES
    ${CMAKE_SOURCE_DIR}/build/vcpkg_installed/x64-windows/include
  )
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  set(VCPKG_INCLUDES
    ${CMAKE_SOURCE_DIR}/build/vcpkg_installed/x64-linux/include
  )
endif()

# includes
target_include_directories(proj_vulkan_triangle PRIVATE
  ${CMAKE_SOURCE_DIR}/thirdparty/VulkanSDK/1.3.236.0/Include
  ${IMGUI_INCLUDES}
  ${VCPKG_INCLUDES}
  ${CMAKE_SOURCE_DIR}/proj_vulkan_triangle/src
)

target_link_libraries(proj_vulkan_triangle PRIVATE glm::glm)
target_link_libraries(proj_vulkan_triangle PRIVATE SDL2::SDL2 SDL2::SDL2main)
target_link_libraries(proj_vulkan_triangle PRIVATE Vulkan::Vulkan)

# target_link_libraries(proj_vulkan_triangle PRIVATE )
IF(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  IF(CMAKE_BUILD_TYPE MATCHES DEBUG)
    # create symlink assets for build
    set(src ${CMAKE_SOURCE_DIR}/proj_vulkan_triangle/assets)
    set(dst ${CMAKE_CURRENT_BINARY_DIR}/assets)
    add_custom_command(
      TARGET ${PROJECT_NAME} PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${src} ${dst}
      DEPENDS ${dst}
      COMMENT "symbolic link resources folder from ${src} => ${dst}"
    )
  endif()
endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)