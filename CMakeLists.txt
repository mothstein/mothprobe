cmake_minimum_required(VERSION 3.20)
project(mothprobe)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force static linking of runtime library (MSVC /MT or /MTd)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Configure spdlog to use external fmt
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt library" FORCE)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "Disable libsodium vendor tests" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "Disable cpp-httplib tests" FORCE)
find_package(OpenSSL QUIET)
find_package(Threads REQUIRED)

# Add subdirectories
add_subdirectory(third_party/fmt)
add_subdirectory(third_party/spdlog)
add_subdirectory(third_party/json)
add_subdirectory(third_party/catch2)
add_subdirectory(third_party/tomlplusplus)
add_subdirectory(third_party/cpp-httplib)
add_subdirectory(third_party/libsodium)

add_library(cpp_mcp STATIC
    third_party/cpp-mcp/src/mcp_message.cpp
    third_party/cpp-mcp/src/mcp_resource.cpp
    third_party/cpp-mcp/src/mcp_server.cpp
    third_party/cpp-mcp/src/mcp_stdio_client.cpp
    third_party/cpp-mcp/src/mcp_sse_client.cpp
    third_party/cpp-mcp/src/mcp_tool.cpp
)
target_include_directories(cpp_mcp PUBLIC
    third_party/cpp-mcp/include
    third_party/cpp-mcp/common
)
target_compile_definitions(cpp_mcp PUBLIC
    MCP_MAX_SESSIONS=10
    MCP_SESSION_TIMEOUT=30
)
target_link_libraries(cpp_mcp PUBLIC Threads::Threads)
if(WIN32)
    target_compile_definitions(cpp_mcp PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

add_library(mothprobe_project INTERFACE)
target_include_directories(mothprobe_project INTERFACE src)
target_compile_definitions(mothprobe_project INTERFACE SPDLOG_FMT_EXTERNAL SODIUM_STATIC=1)
if(OpenSSL_FOUND)
    target_compile_definitions(mothprobe_project INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

set(MOTHPROBE_CORE_SOURCES
    src/core/audit.cpp
    src/core/config.cpp
    src/core/logger.cpp
    src/core/shell_runner.cpp
)

set(MOTHPROBE_MCP_SOURCES
    src/mcp/main_mcp.cpp
    src/mcp/chat_memory.cpp
    src/mcp/json_rpc.cpp
    src/mothon/protocol/upstream.cpp
    src/mothon/tools/tool_registry.cpp
    src/mcp/llm/claude_adapter.cpp
    src/mcp/llm/gemini_adapter.cpp
    src/mcp/llm/http_client.cpp
    src/mcp/llm/ollama_adapter.cpp
    src/mcp/llm/openai_compatible_adapter.cpp
    src/mcp/llm/openrouter_adapter.cpp
    src/mcp/llm/groq_adapter.cpp
    src/mcp/llm/provider_factory.cpp
    src/mcp/scope_validator.cpp
    src/mcp/server.cpp
)

add_executable(mothprobe_mcp ${MOTHPROBE_MCP_SOURCES} ${MOTHPROBE_CORE_SOURCES})
target_link_libraries(mothprobe_mcp PRIVATE
    mothprobe_project fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus httplib::httplib sodium cpp_mcp
)
if(OpenSSL_FOUND)
    target_link_libraries(mothprobe_mcp PRIVATE OpenSSL::SSL OpenSSL::Crypto)
elseif(WIN32)
    target_link_libraries(mothprobe_mcp PRIVATE winhttp)
endif()
if(WIN32)
    target_link_libraries(mothprobe_mcp PRIVATE ws2_32)
endif()
add_custom_command(TARGET mothprobe_mcp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/.mothprobe/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:mothprobe_mcp> ${CMAKE_SOURCE_DIR}/.mothprobe/bin/$<TARGET_FILE_NAME:mothprobe_mcp>
)

# Test Executable
enable_testing()
add_executable(smoke_test
    tests/test.cpp
)
target_link_libraries(smoke_test PRIVATE
    mothprobe_project
    Catch2::Catch2WithMain
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
    httplib::httplib
    sodium
)
add_dependencies(smoke_test mothprobe_mcp)
add_test(NAME SmokeTest COMMAND smoke_test)

add_executable(test_llm
    tests/test_llm.cpp
    src/mcp/llm/claude_adapter.cpp
    src/mcp/llm/gemini_adapter.cpp
    src/mcp/llm/http_client.cpp
    src/mcp/llm/ollama_adapter.cpp
    src/mcp/llm/openai_compatible_adapter.cpp
    src/mcp/llm/openrouter_adapter.cpp
    src/mcp/llm/groq_adapter.cpp
    src/mcp/llm/provider_factory.cpp
    ${MOTHPROBE_CORE_SOURCES}
)
target_link_libraries(test_llm PRIVATE
    mothprobe_project
    Catch2::Catch2WithMain
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
    httplib::httplib
    sodium
)
if(OpenSSL_FOUND)
    target_link_libraries(test_llm PRIVATE OpenSSL::SSL OpenSSL::Crypto)
elseif(WIN32)
    target_link_libraries(test_llm PRIVATE winhttp)
endif()
