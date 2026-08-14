# Shared CMake helpers for Maho workspace.

function(maho_collect_sources OUT_VAR ROOT_DIR)
	if(NOT IS_DIRECTORY "${ROOT_DIR}")
		set(${OUT_VAR} "" PARENT_SCOPE)
		return()
	endif()

	file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
		"${ROOT_DIR}/*.c"
		"${ROOT_DIR}/*.cc"
		"${ROOT_DIR}/*.cpp"
		"${ROOT_DIR}/*.cxx"
		"${ROOT_DIR}/*.h"
		"${ROOT_DIR}/*.hh"
		"${ROOT_DIR}/*.hpp"
		"${ROOT_DIR}/*.hxx"
		"${ROOT_DIR}/*.inl"
		"${ROOT_DIR}/*.ipp"
	)
	set(${OUT_VAR} "${_sources}" PARENT_SCOPE)
endfunction()

function(maho_source_group_mirrors ROOT_DIR)
	foreach(_file IN LISTS ARGN)
		file(RELATIVE_PATH _rel "${ROOT_DIR}" "${_file}")
		get_filename_component(_dir "${_rel}" DIRECTORY)
		if(_dir STREQUAL "")
			set(_group "Source")
		else()
			string(REPLACE "/" "\\" _group "Source/${_dir}")
		endif()
		source_group("${_group}" FILES "${_file}")
	endforeach()
endfunction()

# Attach .lua files to a target for IDE browsing (not compiled).
function(maho_add_lua_scripts TARGET_NAME SCRIPTS_DIR)
	if(NOT IS_DIRECTORY "${SCRIPTS_DIR}")
		return()
	endif()

	file(GLOB_RECURSE _scripts CONFIGURE_DEPENDS "${SCRIPTS_DIR}/*.lua")
	if(_scripts STREQUAL "")
		return()
	endif()

	target_sources(${TARGET_NAME} PRIVATE ${_scripts})
	set_source_files_properties(${_scripts} PROPERTIES
		HEADER_FILE_ONLY TRUE
		VS_TOOL_OVERRIDE "None"
	)

	foreach(_file IN LISTS _scripts)
		file(RELATIVE_PATH _rel "${SCRIPTS_DIR}" "${_file}")
		get_filename_component(_dir "${_rel}" DIRECTORY)
		if(_dir STREQUAL "")
			set(_group "Scripts")
		else()
			string(REPLACE "/" "\\" _group "Scripts/${_dir}")
		endif()
		source_group("${_group}" FILES "${_file}")
	endforeach()
endfunction()

function(maho_set_output_dirs TARGET_NAME)
	set_target_properties(${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${MAHO_BIN_DIR}"
		LIBRARY_OUTPUT_DIRECTORY "${MAHO_BIN_DIR}"
		ARCHIVE_OUTPUT_DIRECTORY "${MAHO_LIB_DIR}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MAHO_BIN_DIR}/Debug"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MAHO_BIN_DIR}/Release"
		RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MAHO_BIN_DIR}/RelWithDebInfo"
		RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${MAHO_BIN_DIR}/MinSizeRel"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MAHO_BIN_DIR}/Debug"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MAHO_BIN_DIR}/Release"
		LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MAHO_BIN_DIR}/RelWithDebInfo"
		LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${MAHO_BIN_DIR}/MinSizeRel"
		ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${MAHO_LIB_DIR}/Debug"
		ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${MAHO_LIB_DIR}/Release"
		ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MAHO_LIB_DIR}/RelWithDebInfo"
		ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${MAHO_LIB_DIR}/MinSizeRel"
	)
endfunction()

function(maho_copy_shaders TARGET_NAME SHADER_ROOT DEST_SUBDIR)
	if(NOT IS_DIRECTORY "${SHADER_ROOT}")
		return()
	endif()

	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/${DEST_SUBDIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADER_ROOT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/${DEST_SUBDIR}"
		COMMENT "Copy shaders: ${DEST_SUBDIR}"
		VERBATIM
	)
endfunction()

function(maho_install_runtime_target TARGET_NAME)
	install(FILES "$<TARGET_FILE:${TARGET_NAME}>"
		DESTINATION .
		COMPONENT Runtime
	)
endfunction()

function(maho_install_directory SRC_DIR DEST_SUBDIR)
	if(NOT IS_DIRECTORY "${SRC_DIR}")
		return()
	endif()
	install(DIRECTORY "${SRC_DIR}/"
		DESTINATION "${DEST_SUBDIR}"
		COMPONENT Runtime
		PATTERN ".gitkeep" EXCLUDE
		PATTERN "README.md" EXCLUDE
	)
endfunction()

# Build plugin modules from .cplugin manifests (scan_plugins.py → CMake JSON).
# Requires: MAHO_PLUGINS_DIR, MAHO_ENGINE_TARGET, MAHO_SCAN_PLUGINS_PY, MAHO_PYTHON_EXE.
function(maho_add_plugin_modules)
	set(_options "")
	set(_oneValueArgs
		PLUGINS_DIR
		ENGINE_TARGET
		SCAN_SCRIPT
		PYTHON_EXE
		JSON_OUT
		CPROJECT
	)
	set(_multiValueArgs "")
	cmake_parse_arguments(_MAHO_PM "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

	if(NOT _MAHO_PM_ENGINE_TARGET)
		message(FATAL_ERROR "maho_add_plugin_modules: ENGINE_TARGET required")
	endif()
	if(NOT _MAHO_PM_SCAN_SCRIPT)
		message(FATAL_ERROR "maho_add_plugin_modules: SCAN_SCRIPT required")
	endif()
	if(NOT _MAHO_PM_PYTHON_EXE)
		message(FATAL_ERROR "maho_add_plugin_modules: PYTHON_EXE required")
	endif()
	if(NOT _MAHO_PM_CPROJECT AND NOT _MAHO_PM_PLUGINS_DIR)
		message(FATAL_ERROR "maho_add_plugin_modules: CPROJECT or PLUGINS_DIR required")
	endif()

	if(NOT _MAHO_PM_JSON_OUT)
		set(_MAHO_PM_JSON_OUT "${CMAKE_BINARY_DIR}/maho_plugin_modules.json")
	endif()

	set(_MAHO_PLUGIN_SCAN_ARGS
		"--out" "${_MAHO_PM_JSON_OUT}"
		"--check"
	)
	if(_MAHO_PM_CPROJECT)
		list(APPEND _MAHO_PLUGIN_SCAN_ARGS "--cproject" "${_MAHO_PM_CPROJECT}")
	else()
		list(APPEND _MAHO_PLUGIN_SCAN_ARGS "--plugins-dir" "${_MAHO_PM_PLUGINS_DIR}")
	endif()

	execute_process(
		COMMAND "${_MAHO_PM_PYTHON_EXE}" "${_MAHO_PM_SCAN_SCRIPT}" ${_MAHO_PLUGIN_SCAN_ARGS}
		RESULT_VARIABLE _MAHO_PLUGIN_SCAN_RC
		OUTPUT_VARIABLE _MAHO_PLUGIN_SCAN_OUT
		ERROR_VARIABLE _MAHO_PLUGIN_SCAN_ERR
	)
	if(NOT _MAHO_PLUGIN_SCAN_RC EQUAL 0)
		message(FATAL_ERROR
			"Maho plugin scan failed (exit ${_MAHO_PLUGIN_SCAN_RC}):\n"
			"${_MAHO_PLUGIN_SCAN_OUT}${_MAHO_PLUGIN_SCAN_ERR}")
	endif()
	message(STATUS "Maho: plugin scan OK (${_MAHO_PM_JSON_OUT})")

	file(READ "${_MAHO_PM_JSON_OUT}" _MAHO_PLUGIN_JSON)

	string(JSON _MAHO_MODULE_COUNT LENGTH "${_MAHO_PLUGIN_JSON}" Modules)
	if(_MAHO_MODULE_COUNT EQUAL 0)
		if(_MAHO_PM_CPROJECT)
			message(WARNING "Maho: no enabled plugin modules for ${_MAHO_PM_CPROJECT}")
		else()
			message(WARNING "Maho: no plugin modules found under ${_MAHO_PM_PLUGINS_DIR}")
		endif()
	endif()

	add_library(MahoModules INTERFACE)
	add_library(Maho::Modules ALIAS MahoModules)

	if(_MAHO_MODULE_COUNT EQUAL 0)
		return()
	endif()

	math(EXPR _MAHO_MODULE_LAST "${_MAHO_MODULE_COUNT} - 1")
	foreach(_idx RANGE 0 ${_MAHO_MODULE_LAST})
		string(JSON _MOD_NAME GET "${_MAHO_PLUGIN_JSON}" Modules ${_idx} Name)
		string(JSON _MOD_PLUGIN GET "${_MAHO_PLUGIN_JSON}" Modules ${_idx} Plugin)
		string(JSON _MOD_CPLUGIN GET "${_MAHO_PLUGIN_JSON}" Modules ${_idx} Cplugin)
		string(JSON _MOD_SOURCE_DIR GET "${_MAHO_PLUGIN_JSON}" Modules ${_idx} SourceDir)
		cmake_path(CONVERT "${_MOD_SOURCE_DIR}" TO_CMAKE_PATH_LIST _MOD_SOURCE_DIR NORMALIZE)
		cmake_path(CONVERT "${_MOD_CPLUGIN}" TO_CMAKE_PATH_LIST _MOD_CPLUGIN NORMALIZE)
		string(JSON _MOD_DEP_COUNT LENGTH "${_MAHO_PLUGIN_JSON}" Modules ${_idx} Dependencies)

		maho_collect_sources(_MOD_SOURCES "${_MOD_SOURCE_DIR}")
		if(_MOD_SOURCES STREQUAL "")
			message(FATAL_ERROR "Maho module '${_MOD_NAME}': no sources under ${_MOD_SOURCE_DIR}")
		endif()

		set(_MOD_SOURCES_NORM "")
		foreach(_mod_src IN LISTS _MOD_SOURCES)
			cmake_path(CONVERT "${_mod_src}" TO_CMAKE_PATH_LIST _mod_src_norm NORMALIZE)
			list(APPEND _MOD_SOURCES_NORM "${_mod_src_norm}")
		endforeach()
		set(_MOD_SOURCES "${_MOD_SOURCES_NORM}")

		# Target / DLL = module Name (e.g. CMyFeature).
		set(_MOD_TARGET "${_MOD_NAME}")
		string(TOUPPER "${_MOD_NAME}" _MOD_EXPORT_TOKEN)

		if(MAHO_BUILD_SHARED)
			add_library(${_MOD_TARGET} SHARED ${_MOD_SOURCES} "${_MOD_CPLUGIN}")
			target_compile_definitions(${_MOD_TARGET}
				PUBLIC  MAHO_BUILD_SHARED=1
				PRIVATE MAHO_${_MOD_EXPORT_TOKEN}_MODULE_EXPORTS=1
			)
		else()
			add_library(${_MOD_TARGET} STATIC ${_MOD_SOURCES} "${_MOD_CPLUGIN}")
			target_compile_definitions(${_MOD_TARGET}
				PRIVATE MAHO_${_MOD_EXPORT_TOKEN}_MODULE_EXPORTS=1
			)
		endif()

		set_source_files_properties("${_MOD_CPLUGIN}" PROPERTIES
			HEADER_FILE_ONLY TRUE
			VS_TOOL_OVERRIDE "None"
		)
		# Same filter root as CMakeLists.txt (no nested "Plugin" folder).
		source_group("" FILES "${_MOD_CPLUGIN}")

		target_include_directories(${_MOD_TARGET}
			PUBLIC "${_MOD_SOURCE_DIR}/Public"
		)
		# Also on MahoModules so game EXE always sees plugin Public (Generated App includes).
		target_include_directories(MahoModules INTERFACE "${_MOD_SOURCE_DIR}/Public")
		# ImGui's IMGUI_USER_CONFIG (UI/ImGuiConfig.h) lives under the Render plugin Public root.
		if(TARGET imgui)
			target_include_directories(imgui PUBLIC "${_MOD_SOURCE_DIR}/Public")
		endif()
		target_link_libraries(${_MOD_TARGET} PUBLIC ${_MAHO_PM_ENGINE_TARGET})
		if(TARGET glfw)
			target_link_libraries(${_MOD_TARGET} PRIVATE glfw)
		endif()
		target_compile_features(${_MOD_TARGET} PUBLIC cxx_std_20)

		if(_MOD_DEP_COUNT GREATER 0)
			math(EXPR _MOD_DEP_LAST "${_MOD_DEP_COUNT} - 1")
			foreach(_dep_idx RANGE 0 ${_MOD_DEP_LAST})
				string(JSON _MOD_DEP GET "${_MAHO_PLUGIN_JSON}" Modules ${_idx} Dependencies ${_dep_idx})
				target_link_libraries(${_MOD_TARGET} PUBLIC "${_MOD_DEP}")
			endforeach()
		endif()

		if(MSVC)
			target_compile_options(${_MOD_TARGET} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
			set_target_properties(${_MOD_TARGET} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
		else()
			target_compile_options(${_MOD_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
			if(MAHO_BUILD_SHARED)
				set_target_properties(${_MOD_TARGET} PROPERTIES
					CXX_VISIBILITY_PRESET hidden
					VISIBILITY_INLINES_HIDDEN ON
				)
			endif()
		endif()

		maho_set_output_dirs(${_MOD_TARGET})
		maho_source_group_mirrors("${_MOD_SOURCE_DIR}" ${_MOD_SOURCES})
		set_target_properties(${_MOD_TARGET} PROPERTIES
			FOLDER "Plugins/${_MOD_PLUGIN}"
			OUTPUT_NAME "${_MOD_NAME}"
		)

		maho_install_runtime_target(${_MOD_TARGET})
		target_link_libraries(MahoModules INTERFACE ${_MOD_TARGET})
	endforeach()
endfunction()
