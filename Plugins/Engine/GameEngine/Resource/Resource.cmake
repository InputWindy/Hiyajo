# Resource plugin: third-party dependencies.
# None - pure std (atomic/fstream/iterator/mutex/unordered_map). Depends on the
# Name and Paths plugins (their Public/ include dirs propagate via .cplugin
# Dependencies) and on the engine's Engine/ThreadedServer.h header.
#
# Texture decoding (WIC) uses Windows codecs + COM; Windows-only.
if(WIN32)
	target_link_libraries(Resource PRIVATE windowscodecs ole32)
endif()
