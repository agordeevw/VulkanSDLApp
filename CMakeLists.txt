cmake_minimum_required(VERSION 3.7)

project(VulkanSDLApp)

set(ASSIMP_DIR "external/assimp")
set(SDL2_DIR "external/SDL2")
set(GLM_DIR "external/GLM")

find_package(ASSIMP REQUIRED)
find_package(SDL2 REQUIRED)
find_package(GLM REQUIRED)
find_package(Vulkan REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(VulkanSDLApp
	"src/stb_image.h"
	"src/stb_image.c"
	"src/Main.cpp")
target_include_directories(VulkanSDLApp PUBLIC
	"src"
	${SDL2_INCLUDE_DIRS}
	${ASSIMP_INCLUDE_DIRS}
	${GLM_INCLUDE_DIRS}
	${Vulkan_INCLUDE_DIRS})
target_link_libraries(VulkanSDLApp
	${SDL2_LIBRARIES}
	${ASSIMP_LIBRARIES}
	${Vulkan_LIBRARIES})