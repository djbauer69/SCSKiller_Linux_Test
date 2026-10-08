cmake_minimum_required(VERSION 3.20)
project(scskiller_vulkan_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_executable(scskiller-vulkan-probe probe.cpp)
target_link_libraries(scskiller-vulkan-probe PRIVATE Vulkan::Vulkan)
install(TARGETS scskiller-vulkan-probe RUNTIME DESTINATION bin)
