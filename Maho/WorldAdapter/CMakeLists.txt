set(MAHO_WORLD_ADAPTER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(MAHO_WORLD_ADAPTER_SOURCE_DIR "${MAHO_WORLD_ADAPTER_ROOT}/Source")
set(MAHO_WORLD_ADAPTER_PUBLIC_DIR "${MAHO_WORLD_ADAPTER_SOURCE_DIR}/Public")
set(MAHO_WORLD_ADAPTER_PRIVATE_DIR "${MAHO_WORLD_ADAPTER_SOURCE_DIR}/Private")

find_package(Threads REQUIRED)

set(MAHO_WORLD_ADAPTER_SOURCES
	"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}/WorldAdapter/WorldAdapter.h"
	"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}/WorldAdapter/Protocol/Dto.h"
	"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}/WorldAdapter/Protocol/Json.h"
	"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}/WorldAdapter/Core/Backend.h"
	"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}/WorldAdapter/Core/CommandQueue.h"
	"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}/WorldAdapter/Service/WorldAdapterService.h"
	"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}/WorldAdapter/Stub/StubBackend.h"
	"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}/WorldAdapter/Transport/HttpTransport.h"
	"${MAHO_WORLD_ADAPTER_PRIVATE_DIR}/CommandQueue.cpp"
	"${MAHO_WORLD_ADAPTER_PRIVATE_DIR}/HttpTransport.cpp"
	"${MAHO_WORLD_ADAPTER_PRIVATE_DIR}/WorldAdapter.cpp"
	"${MAHO_WORLD_ADAPTER_PRIVATE_DIR}/ProtocolJson.cpp"
	"${MAHO_WORLD_ADAPTER_PRIVATE_DIR}/StubBackend.cpp"
	"${MAHO_WORLD_ADAPTER_PRIVATE_DIR}/WorldAdapterService.cpp"
)

add_library(MahoWorldAdapter STATIC ${MAHO_WORLD_ADAPTER_SOURCES})
add_library(Maho::WorldAdapter ALIAS MahoWorldAdapter)

target_include_directories(MahoWorldAdapter
	PUBLIC
		"${MAHO_WORLD_ADAPTER_PUBLIC_DIR}"
		"${MAHO_NLOHMANN_JSON_INCLUDE_DIR}"
	PRIVATE
		"${MAHO_WORLD_ADAPTER_PRIVATE_DIR}"
)
target_compile_features(MahoWorldAdapter PUBLIC cxx_std_20)
target_link_libraries(MahoWorldAdapter PRIVATE Threads::Threads)

if(WIN32)
	target_link_libraries(MahoWorldAdapter PRIVATE ws2_32)
endif()

function(maho_configure_world_adapter_target TARGET_NAME)
	if(MSVC)
		target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
	else()
		target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
	endif()
	maho_set_output_dirs(${TARGET_NAME})
	set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "WorldAdapter")
endfunction()

maho_configure_world_adapter_target(MahoWorldAdapter)
maho_source_group_mirrors("${MAHO_WORLD_ADAPTER_SOURCE_DIR}" ${MAHO_WORLD_ADAPTER_SOURCES})

add_executable(MahoWorldAdapterHarness
	"${MAHO_WORLD_ADAPTER_ROOT}/Harness/Main.cpp"
)
target_link_libraries(MahoWorldAdapterHarness PRIVATE Maho::WorldAdapter)
maho_configure_world_adapter_target(MahoWorldAdapterHarness)

if(MAHO_BUILD_WORLD_ADAPTER_TESTS)
	file(TO_CMAKE_PATH
		"${MAHO_WORKSPACE_ROOT}/Tools/AgentBridge/tests/fixtures/world-adapter-v1"
		MAHO_WORLD_ADAPTER_FIXTURE_ROOT
	)

	add_executable(MahoWorldAdapterTests
		"${MAHO_WORLD_ADAPTER_ROOT}/Tests/TestFramework.h"
		"${MAHO_WORLD_ADAPTER_ROOT}/Tests/CoreTests.cpp"
		"${MAHO_WORLD_ADAPTER_ROOT}/Tests/HttpTests.cpp"
		"${MAHO_WORLD_ADAPTER_ROOT}/Tests/ProtocolTests.cpp"
		"${MAHO_WORLD_ADAPTER_ROOT}/Tests/WorldAdapterTests.cpp"
	)
	target_link_libraries(MahoWorldAdapterTests PRIVATE Maho::WorldAdapter)
	target_compile_definitions(MahoWorldAdapterTests PRIVATE
		"MAHO_WORLD_ADAPTER_FIXTURE_ROOT=\"${MAHO_WORLD_ADAPTER_FIXTURE_ROOT}\""
	)
	maho_configure_world_adapter_target(MahoWorldAdapterTests)

	add_test(NAME MahoWorldAdapterTests COMMAND MahoWorldAdapterTests)
	set_tests_properties(MahoWorldAdapterTests PROPERTIES TIMEOUT 60)
endif()
