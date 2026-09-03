# -- MAHOGEN Resource -- auto-generated build block, do not edit --
file(GLOB Resource_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Resource_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Resource_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Resource SHARED
${Resource_PUBLIC_HEADERS}
${Resource_PRIVATE_HEADERS}
${Resource_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Resource.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Resource.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Resource.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Resource.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Resource PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Engine/Core/Name/Public"
	"${ENGINE_DIR}/Plugins/Engine/Core/Paths/Public"
	"${ENGINE_DIR}/Plugins/Engine/Core/Log/Public"
)
set_target_properties(Resource PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Resource PRIVATE MAHO_RESOURCE_MODULE_EXPORTS)
target_link_libraries(Resource PUBLIC Maho)
set_property(TARGET Resource PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(Resource PUBLIC Name Paths Log)
set_target_properties(Resource PROPERTIES FOLDER "Maho/Plugins/Engine/GameEngine")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Resource_PUBLIC_HEADERS} ${Resource_PRIVATE_HEADERS} ${Resource_PRIVATE_SOURCES})
# -- /MAHOGEN Resource --

# Resource plugin: third-party dependencies.
# None - pure std (atomic/fstream/iterator/mutex/unordered_map). Depends on the
# Name and Paths plugins (their Public/ include dirs propagate via .cplugin
# Dependencies) and on the engine's Engine/ThreadedServer.h header.
#
# Texture decoding (WIC) uses Windows codecs + COM; Windows-only.
if(WIN32)
	target_link_libraries(Resource PRIVATE windowscodecs ole32)
endif()


