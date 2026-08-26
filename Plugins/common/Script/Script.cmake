# Script plugin: Lua 5.4 (compiled static lib) + sol2 (header-only bindings).
# sol2 + lua are owned here; game code that consumes sol2 links Script's public
# lua target (transitive via this plugin).

# ── Lua 5.4 — FetchContent (populates under the build Intermediate). ──
if(NOT TARGET lua)
	maho_git_repository_url(_SCRIPT_LUA_URL https://github.com/lua/lua.git)
	maho_fetchcontent_populate_or_reuse(lua_src ${_SCRIPT_LUA_URL} v5.4.7 lua.h)
	set(_SCRIPT_LUA_SOURCE_DIR "${lua_src_SOURCE_DIR}")
	set(_SCRIPT_LUA_SOURCES
		"${_SCRIPT_LUA_SOURCE_DIR}/lapi.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lauxlib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lbaselib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lcode.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lcorolib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lctype.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/ldblib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/ldebug.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/ldo.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/ldump.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lfunc.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lgc.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/linit.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/liolib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/llex.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lmathlib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lmem.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/loadlib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lobject.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lopcodes.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/loslib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lparser.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lstate.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lstring.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lstrlib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/ltable.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/ltablib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/ltm.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lundump.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lutf8lib.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lvm.c"
		"${_SCRIPT_LUA_SOURCE_DIR}/lzio.c"
	)
	add_library(lua STATIC ${_SCRIPT_LUA_SOURCES})
	target_include_directories(lua PUBLIC "${_SCRIPT_LUA_SOURCE_DIR}")
	set_target_properties(lua PROPERTIES
		FOLDER "ThirdParty"
		POSITION_INDEPENDENT_CODE ON
		C_STANDARD 99
	)
	if(MSVC)
		target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
		# Match the plugin DLL's dynamic CRT: /MDd in Debug, /MD in Release.
		target_compile_options(lua PRIVATE $<$<CONFIG:Debug>:/MDd> $<$<NOT:$<CONFIG:Debug>>:/MD>)
	endif()
endif()

# ── sol2 — header-only bindings; FetchContent. ──
maho_git_repository_url(_SCRIPT_SOL2_URL https://github.com/ThePhD/sol2.git)
maho_fetchcontent_populate_or_reuse(sol2 ${_SCRIPT_SOL2_URL} v3.3.1 include/sol/sol.hpp)
set(_SCRIPT_SOL2_INCLUDE_DIR "${sol2_SOURCE_DIR}/include")
set(MAHO_SOL2_INCLUDE_DIR "${_SCRIPT_SOL2_INCLUDE_DIR}" CACHE INTERNAL "sol2 include directory" FORCE)

# Script DLL links lua; sol2 include is public (consumers reflect Lua types).
target_link_libraries(Script PRIVATE lua)
target_include_directories(Script PUBLIC "${_SCRIPT_SOL2_INCLUDE_DIR}")
