cmake_minimum_required(VERSION 3.28)
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()
project(redis LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" PLUGIN_VERSION)
string(STRIP "${PLUGIN_VERSION}" PLUGIN_VERSION)
if(NOT PLUGIN_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must be a numeric semantic version")
endif()

include(cmake/OpenSSL.cmake)
include(FetchContent)
set(BOOST_INCLUDE_LIBRARIES redis)
set(BOOST_ENABLE_CMAKE ON)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.xz
    URL_HASH SHA256=9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL)
block(SCOPE_FOR VARIABLES)
    # FindOpenSSL's Windows module re-searches for libraries that are not built yet.
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
    set(OpenSSL_DIR "${REDIS_OPENSSL_PACKAGE_DIR}")
    FetchContent_MakeAvailable(Boost)
endblock()
# Redis uses callbacks, not Asio spawn/Boost.Context's single-architecture assembly.
get_target_property(_redis_dependencies boost_redis INTERFACE_LINK_LIBRARIES)
if(NOT _redis_dependencies)
    message(FATAL_ERROR "Boost.Redis dependency configuration failed; refusing an incomplete link interface")
endif()
list(REMOVE_ITEM _redis_dependencies Boost::asio)
list(APPEND _redis_dependencies Boost::asio_core)
set_property(TARGET boost_redis PROPERTY INTERFACE_LINK_LIBRARIES "${_redis_dependencies}")

add_library(redis_core STATIC value.cpp commands.cpp transport.cpp)
target_include_directories(redis_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(redis_core PRIVATE Boost::redis OpenSSL::SSL OpenSSL::Crypto)
add_dependencies(redis_core redis_openssl)

set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../4D-Plugin-SDK/4D Plugin API")
set(PLUGIN_SOURCES redis-4dplugin.cpp "${SDK_DIR}/4DPluginAPI.c")
set(PLUGIN_RESOURCES manifest.json constants.xlf)
if(APPLE)
    add_library(redis MODULE ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})
    set_target_properties(redis PROPERTIES
        BUNDLE TRUE BUNDLE_EXTENSION bundle
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.miyako.redis"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PLUGIN_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PLUGIN_VERSION}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/redis-test/Plugins"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/redis-test/Plugins"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/redis-test/Plugins")
    set_source_files_properties(${PLUGIN_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(redis_core PRIVATE "${SECURITY_FRAMEWORK}" "${COREFOUNDATION_FRAMEWORK}")
    target_link_libraries(redis PRIVATE "${COREGRAPHICS_FRAMEWORK}")
    target_link_options(redis PRIVATE "LINKER:-exported_symbol,_FourDPackex")
elseif(WIN32)
    add_library(redis SHARED ${PLUGIN_SOURCES})
    set(PLUGIN_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/redis-test/Plugins/redis/Contents/Windows64")
    set_target_properties(redis PROPERTIES SUFFIX ".4DX"
        LINK_FLAGS "/DEF:\"${SDK_DIR}/4DPluginAPI.def\""
        RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_OUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PLUGIN_OUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PLUGIN_OUT_DIR}")
    add_custom_command(TARGET redis POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/redis-test/Plugins/redis/Contents/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${CMAKE_CURRENT_SOURCE_DIR}/constants.xlf"
            "${CMAKE_CURRENT_SOURCE_DIR}/redis-test/Plugins/redis/Contents/Resources")
endif()
target_include_directories(redis PRIVATE "${SDK_DIR}")
set_source_files_properties("${SDK_DIR}/4DPluginAPI.c" PROPERTIES LANGUAGE CXX)
target_compile_definitions(redis PRIVATE UNICODE _UNICODE)
target_link_libraries(redis PRIVATE redis_core)

enable_testing()
add_executable(redis_native_tests tests/native.cpp)
target_link_libraries(redis_native_tests PRIVATE redis_core)
add_test(NAME redis_units COMMAND redis_native_tests units)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME redis_transport COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/transport.py" "$<TARGET_FILE:redis_native_tests>")
set_tests_properties(redis_transport PROPERTIES TIMEOUT 60)
add_test(NAME redis_load_unload COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/load_unload.py" "$<TARGET_FILE:redis>")
set_tests_properties(redis_load_unload PROPERTIES TIMEOUT 30)
FetchContent_GetProperties(Boost SOURCE_DIR _boost_source_dir)
add_test(NAME redis_dependency_configuration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/tests/test_cmake_dependencies.py"
    --cmake "${CMAKE_COMMAND}" --boost-source "${_boost_source_dir}")
set_tests_properties(redis_dependency_configuration PROPERTIES TIMEOUT 180)
