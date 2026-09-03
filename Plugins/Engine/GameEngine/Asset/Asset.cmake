# -- MAHOGEN Asset -- auto-generated build block, do not edit --
file(GLOB Asset_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Public/*.h")
file(GLOB Asset_PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.h")
file(GLOB Asset_PRIVATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Private/*.cpp")
add_library(Asset SHARED
${Asset_PUBLIC_HEADERS}
${Asset_PRIVATE_HEADERS}
${Asset_PRIVATE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/Asset.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Asset.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json"
)
set_source_files_properties(	"${CMAKE_CURRENT_LIST_DIR}/Asset.cplugin"
	"${CMAKE_CURRENT_LIST_DIR}/Asset.cmake"
	"${CMAKE_CURRENT_LIST_DIR}/settings.json" PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(Asset PUBLIC
	"${ENGINE_DIR}/Source/Public"
	"${CMAKE_CURRENT_LIST_DIR}/Public"
	"${CMAKE_CURRENT_SOURCE_DIR}/Plugins/ExampleEngine/Public"
	"${ENGINE_DIR}/Plugins/Engine/GameEngine/Resource/Public"
)
set_target_properties(Asset PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(Asset PRIVATE MAHO_ASSET_MODULE_EXPORTS)
target_link_libraries(Asset PUBLIC Maho)
set_property(TARGET Asset PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>")
target_link_libraries(Asset PUBLIC Resource)
set_target_properties(Asset PROPERTIES FOLDER "Maho/Plugins/Engine/GameEngine")
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${Asset_PUBLIC_HEADERS} ${Asset_PRIVATE_HEADERS} ${Asset_PRIVATE_SOURCES})
# -- /MAHOGEN Asset --

# Asset plugin: third-party dependencies.
# Concrete asset codecs. Texture decoding (WIC) uses Windows codecs + COM;
# Windows-only. (std::vector/std::string otherwise; links the Resource plugin,
# whose Public includes propagate transitively.)
if(WIN32)
	target_link_libraries(Asset PRIVATE windowscodecs ole32)
endif()
