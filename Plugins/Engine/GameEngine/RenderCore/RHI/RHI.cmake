# -- MAHOGEN RHI -- auto-generated build block, do not edit --
file(GLOB RHI_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB RHI_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB RHI_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(RHI SHARED
${RHI_PUBLIC_HEADERS}
${RHI_PRIVATE_HEADERS}
${RHI_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/RHI.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/RHI.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/RHI.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/RHI.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(RHI PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Common/ConsoleVariable/Public"
	"${ENGINE_DIR}/Plugins/Engine/Core/Log/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/Platform/Public"
)
set_target_properties(RHI PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(RHI PRIVATE MAHO_RHI_MODULE_EXPORTS)
target_link_libraries(RHI PUBLIC Maho)
set_property(TARGET RHI PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(RHI PUBLIC ConsoleVariable Log Platform)
set_target_properties(RHI PROPERTIES FOLDER "Maho/Plugins/Engine/GameEngine/RenderCore")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${RHI_PUBLIC_HEADERS} ${RHI_PRIVATE_HEADERS} ${RHI_PRIVATE_SOURCES})
# -- /MAHOGEN RHI --

# RHI plugin: Vulkan backend + VMA (header-only). Public headers stay
# backend-agnostic (no vulkan.h / VMA types).

# -- Vulkan SDK - headers + vulkan-1 lib. --
# VULKAN_SDK env var (LunarG installer) or find_package(Vulkan).
find_package(Vulkan REQUIRED)
target_link_libraries(RHI PUBLIC Vulkan::Vulkan)

# -- Vulkan Memory Allocator (header-only) - FetchContent. --
if(NOT TARGET vma)
	include(FetchContent)
	FetchContent_Declare(vma
		URL "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.2.1.tar.gz"
			"https://codeload.github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/tar.gz/refs/tags/v3.2.1"
		URL_HASH SHA256=5E7749504CB802427FFB7BEC38A0B6A15DB46AE253F00560ACB3E624D9FE695C
		TIMEOUT 600
	)
	FetchContent_MakeAvailable(vma)
	add_library(vma INTERFACE)
	target_include_directories(vma INTERFACE "${vma_SOURCE_DIR}/include")
endif()
target_link_libraries(RHI PRIVATE vma)

if(WIN32)
	target_compile_definitions(RHI PRIVATE VK_USE_PLATFORM_WIN32_KHR=1)
endif()
