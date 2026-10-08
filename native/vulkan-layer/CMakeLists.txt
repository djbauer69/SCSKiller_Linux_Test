cmake_minimum_required(VERSION 3.20)
project(scskiller_vulkan_layer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_library(VK_LAYER_SCSKILLER SHARED
    scskiller_layer.cpp
)

target_include_directories(VK_LAYER_SCSKILLER PRIVATE
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(VK_LAYER_SCSKILLER PRIVATE
    Vulkan::Vulkan
)

set_target_properties(VK_LAYER_SCSKILLER PROPERTIES
    PREFIX ""
    OUTPUT_NAME "VK_LAYER_SCSKILLER"
)

install(TARGETS VK_LAYER_SCSKILLER LIBRARY DESTINATION lib)
install(FILES layer.json DESTINATION share/vulkan/implicit_layer.d)
