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
