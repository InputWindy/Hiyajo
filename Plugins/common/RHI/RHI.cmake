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
