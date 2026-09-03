# -- MAHOGEN Log -- auto-generated build block, do not edit --
file(GLOB Log_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Log_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Log_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Log SHARED
${Log_PUBLIC_HEADERS}
${Log_PRIVATE_HEADERS}
${Log_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Log.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Log.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Log.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Log.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Log PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
)
set_target_properties(Log PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Log PRIVATE MAHO_LOG_MODULE_EXPORTS)
target_link_libraries(Log PUBLIC Maho)
set_property(TARGET Log PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
set_target_properties(Log PROPERTIES FOLDER "Maho/Plugins/Engine/Core")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Log_PUBLIC_HEADERS} ${Log_PRIVATE_HEADERS} ${Log_PRIVATE_SOURCES})
# -- /MAHOGEN Log --

# Log plugin: third-party dependencies (spdlog - header-only).
# Fetch spdlog via the engine's clone helper; wire it as the SPDLOG_POPULATED
# interface target the Log DLL links. Header-only -> no add_subdirectory.
include(FetchContent)

set(SPDLOG_VER v1.14.1)
maho_git_repository_url(SPDLOG_URL https://github.com/gabime/spdlog.git)
maho_fetchcontent_populate_or_reuse(
	spdlog "${SPDLOG_URL}" ${SPDLOG_VER} include/spdlog/spdlog.h
)

# An interface target carrying spdlog's include dir, usable by the Log DLL and
# by any plugin that depends on Log (PUBLIC propagates it).
if(NOT TARGET spdlog::spdlog)
	add_library(spdlog::spdlog INTERFACE IMPORTED)
	set_target_properties(spdlog::spdlog PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${spdlog_SOURCE_DIR}/include"
	)
endif()

# Link spdlog into the Log DLL (its own target exists by the time this runs).
target_link_libraries(Log PUBLIC spdlog::spdlog)



