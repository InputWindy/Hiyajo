# -- MAHOGEN ShaderLibrary -- auto-generated build block, do not edit --
file(GLOB ShaderLibrary_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB ShaderLibrary_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB ShaderLibrary_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(ShaderLibrary SHARED
${ShaderLibrary_PUBLIC_HEADERS}
${ShaderLibrary_PRIVATE_HEADERS}
${ShaderLibrary_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/ShaderLibrary.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ShaderLibrary.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/ShaderLibrary.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/ShaderLibrary.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(ShaderLibrary PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/GameEngine/RenderCore/RHI/Public"
	"${ENGINE_DIR}/Plugins/Common/Name/Public"
	"${ENGINE_DIR}/Plugins/Common/Paths/Public"
	"${ENGINE_DIR}/Plugins/Common/Log/Public"
)
set_target_properties(ShaderLibrary PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(ShaderLibrary PRIVATE MAHO_SHADERLIBRARY_MODULE_EXPORTS)
target_link_libraries(ShaderLibrary PUBLIC Maho)
set_property(TARGET ShaderLibrary PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(ShaderLibrary PROPERTIES OUTPUT_NAME "FShaderLibrary" PREFIX "")
target_link_libraries(ShaderLibrary PUBLIC RHI Name Paths Log)
set_target_properties(ShaderLibrary PROPERTIES FOLDER "Maho/Plugins/GameEngine/RenderCore")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${ShaderLibrary_PUBLIC_HEADERS} ${ShaderLibrary_PRIVATE_HEADERS} ${ShaderLibrary_PRIVATE_SOURCES})
# -- /MAHOGEN ShaderLibrary --

# ShaderLibrary plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the ShaderLibrary target.
#
# ---------------------------------------------------------------------------
# glslang — the GLSL -> SPIR-V compiler this layer compiles .shader text with.
#
# Render.cmake owns the FetchContent declaration (it was the first plugin that
# needed runtime shader compilation), and every plugin .cmake is include()d
# into the SAME top-level CMake scope in dependency order, so by the time this
# file runs `${glslang_SOURCE_DIR}` and the `glslang` target already exist.
# Linking the same targets (never a second fetch) is what keeps ONE glslang in
# the build: a second copy would double the compile and could disagree about
# the SPIR-V tool version.
#
# The fallback below only exists so that building this plugin on its own still
# works (`MAHO_GLSLANG_SOURCE_DIR`, or the shared FetchContent source tree).
# ---------------------------------------------------------------------------
set(_SHADERLIBRARY_GLSLANG_SRC "")
if(TARGET glslang)
	set(_SHADERLIBRARY_GLSLANG_SRC "${glslang_SOURCE_DIR}")
elseif(DEFINED MAHO_GLSLANG_SOURCE_DIR AND EXISTS "${MAHO_GLSLANG_SOURCE_DIR}/CMakeLists.txt")
	set(_SHADERLIBRARY_GLSLANG_SRC "${MAHO_GLSLANG_SOURCE_DIR}")
elseif(EXISTS "${FETCHCONTENT_BASE_DIR}/glslang-src/CMakeLists.txt")
	set(_SHADERLIBRARY_GLSLANG_SRC "${FETCHCONTENT_BASE_DIR}/glslang-src")
endif()

if(NOT TARGET glslang AND NOT _SHADERLIBRARY_GLSLANG_SRC STREQUAL "")
	# Same source + same binary tree Render uses, so `if(NOT TARGET glslang)` in
	# either file is the only guard needed: whichever runs first creates it.
	set(BUILD_TESTING OFF)
	set(ENABLE_GLSLANG_INSTALL OFF CACHE BOOL "" FORCE)
	set(ENABLE_SPIRV_TOOLS_INSTALL OFF CACHE BOOL "" FORCE)
	set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
	set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
	set(glslang_SOURCE_DIR "${_SHADERLIBRARY_GLSLANG_SRC}")
	set(glslang_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/glslang-build")
	enable_language(C)
	add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

if(TARGET glslang)
	target_link_libraries(ShaderLibrary PRIVATE
		glslang
		glslang-default-resource-limits
		OSDependent
		MachineIndependent
		SPIRV
	)
	# `<glslang/Public/...>` and `<SPIRV/...>` are rooted at the glslang source
	# tree; glslang's own targets do not export that root, so it is added here
	# (the binary dir carries glslang's generated headers).
	target_include_directories(ShaderLibrary PRIVATE
		"${glslang_SOURCE_DIR}"
		"${glslang_BINARY_DIR}"
	)
	# Same MSVC handling Render applies: glslang sources carry non-UTF-8 bytes.
	if(MSVC)
		foreach(_shaderlibrary_tgt IN ITEMS glslang MachineIndependent OSDependent SPIRV glslang-default-resource-limits)
			if(TARGET ${_shaderlibrary_tgt})
				target_compile_options(${_shaderlibrary_tgt} PRIVATE /utf-8)
			endif()
		endforeach()
		foreach(_shaderlibrary_tgt IN ITEMS glslang MachineIndependent OSDependent SPIRV glslang-default-resource-limits)
			if(TARGET ${_shaderlibrary_tgt})
				set_target_properties(${_shaderlibrary_tgt} PROPERTIES FOLDER "ThirdParty/glslang")
			endif()
		endforeach()
	endif()
	message(STATUS "Maho: ShaderLibrary links glslang at ${glslang_SOURCE_DIR}")
else()
	message(WARNING
		"Maho: glslang not available for ShaderLibrary -- .shader files cannot be compiled. "
		"Set MAHO_GLSLANG_SOURCE_DIR, or build the Render plugin in the same project.")
endif()

unset(_SHADERLIBRARY_GLSLANG_SRC)
