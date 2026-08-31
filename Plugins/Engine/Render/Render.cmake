# Render plugin: third-party dependencies.
# The DLL target is built by codegen; this file only pulls FetchContent
# deps and links them into the Render target.

# ---------------------------------------------------------------------------
# glslang — Khronos GLSL -> SPIR-V compiler (runtime shader compilation).
# ---------------------------------------------------------------------------
option(MAHO_FETCH_GLSLANG "FetchContent glslang from GitHub" ON)
set(MAHO_GLSLANG_SOURCE_DIR "" CACHE PATH "Pre-cloned glslang root (optional)")
set(MAHO_GLSLANG_BINARY_DIR "" CACHE PATH "glslang build dir (optional, paired with SOURCE_DIR)")
set(_RENDER_HAS_GLSLANG 0)

set(_RENDER_GLSLANG_SRC "")
if(MAHO_GLSLANG_SOURCE_DIR AND EXISTS "${MAHO_GLSLANG_SOURCE_DIR}/CMakeLists.txt")
	set(_RENDER_GLSLANG_SRC "${MAHO_GLSLANG_SOURCE_DIR}")
	message(STATUS "Maho: using MAHO_GLSLANG_SOURCE_DIR=${_RENDER_GLSLANG_SRC}")
else()
	set(_maho_glslang_reuse "${FETCHCONTENT_BASE_DIR}/glslang-src")
	if(EXISTS "${_maho_glslang_reuse}/CMakeLists.txt")
		set(_RENDER_GLSLANG_SRC "${_maho_glslang_reuse}")
		message(STATUS "Maho: reusing glslang at ${_RENDER_GLSLANG_SRC}")
	elseif(MAHO_FETCH_GLSLANG)
		FetchContent_Declare(
			glslang
			URL "${MAHO_GITHUB_MIRROR}https://github.com/KhronosGroup/glslang/archive/refs/tags/14.3.0.tar.gz"
				"${MAHO_GITHUB_MIRROR}https://codeload.github.com/KhronosGroup/glslang/tar.gz/refs/tags/14.3.0"
			URL_HASH SHA256=BE6339048E20280938D9CB399FCDD06E04F8654D43E170E8CCE5A56C9A754284
			TIMEOUT 600
		)
		FetchContent_GetProperties(glslang)
		if(NOT glslang_POPULATED)
			message(STATUS "Maho: Downloading glslang from GitHub (tarball, may take a minute)...")
			FetchContent_Populate(glslang)
		endif()
		if(DEFINED glslang_SOURCE_DIR AND EXISTS "${glslang_SOURCE_DIR}/CMakeLists.txt")
			set(_RENDER_GLSLANG_SRC "${glslang_SOURCE_DIR}")
		endif()
	endif()
	unset(_maho_glslang_reuse)
endif()

if(_RENDER_GLSLANG_SRC AND EXISTS "${_RENDER_GLSLANG_SRC}/CMakeLists.txt")
	set(BUILD_TESTING OFF)
	set(ENABLE_GLSLANG_INSTALL OFF CACHE BOOL "" FORCE)
	set(ENABLE_SPIRV_TOOLS_INSTALL OFF CACHE BOOL "" FORCE)
	set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
	set(ENABLE_OPT OFF CACHE BOOL "" FORCE)

	set(_maho_build_shared_saved "${BUILD_SHARED_LIBS}")
	set(BUILD_SHARED_LIBS OFF)

	set(glslang_SOURCE_DIR "${_RENDER_GLSLANG_SRC}")
	set(glslang_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/glslang-build")
	if(NOT TARGET glslang)
		enable_language(C)
		add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR} EXCLUDE_FROM_ALL)
	endif()

	set(BUILD_SHARED_LIBS "${_maho_build_shared_saved}")
	unset(_maho_build_shared_saved)

	if(TARGET glslang AND TARGET OSDependent AND TARGET SPIRV)
		set(_RENDER_HAS_GLSLANG 1)
		set_target_properties(glslang PROPERTIES FOLDER "Plugins/Render")
		set_target_properties(OSDependent PROPERTIES FOLDER "Plugins/Render")
		set_target_properties(SPIRV PROPERTIES FOLDER "Plugins/Render")
		# glslang sources may contain non-UTF-8 bytes; force UTF-8 so MSVC's
		# structured CL diagnostics (JSON) do not choke on invalid text.
		if(MSVC)
			foreach(_glslang_tgt IN ITEMS glslang MachineIndependent OSDependent SPIRV glslang-default-resource-limits)
				if(TARGET ${_glslang_tgt})
					target_compile_options(${_glslang_tgt} PRIVATE /utf-8)
					# glslang's PCH pulls in a header with non-UTF-8 bytes; the
					# PCH C4828 warning breaks MSBuild's JSON diagnostics.
					set_target_properties(${_glslang_tgt} PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
				endif()
			endforeach()
			# Disable MSBuild's parallel CL (MultiToolTask): its JSON diagnostics
			# break on non-ASCII source paths (e.g. a Chinese project folder).
			set_property(GLOBAL PROPERTY VS_GLOBAL_UseMultiToolTask false)
		endif()
		message(STATUS "Maho: glslang enabled")
	else()
		message(WARNING "Maho: glslang sources present but required targets missing")
	endif()
else()
	message(WARNING
		"Maho: glslang not found. Shader compilation disabled. "
		"Set MAHO_GLSLANG_SOURCE_DIR or MAHO_FETCH_GLSLANG=ON when network allows.")
endif()

if(_RENDER_HAS_GLSLANG)
	target_link_libraries(Render PRIVATE
		glslang
		glslang-default-resource-limits
		OSDependent
		MachineIndependent
		SPIRV
	)
	target_compile_definitions(Render PRIVATE MAHO_WITH_GLSLANG=1)
	target_include_directories(Render PRIVATE
		"${glslang_SOURCE_DIR}"
		"${glslang_BINARY_DIR}"
	)
endif()
