include(ExternalProject)
find_package(Perl REQUIRED)

set(OPENSSL_ROOT_DIR "${CMAKE_BINARY_DIR}/openssl")
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
file(MAKE_DIRECTORY "${OPENSSL_INCLUDE_DIR}")
set(_openssl_source "${CMAKE_CURRENT_SOURCE_DIR}/../openssl-src")

if(APPLE)
    find_program(REDIS_MAKE make REQUIRED)
    set(_architectures ${CMAKE_OSX_ARCHITECTURES})
    if(NOT _architectures)
        set(_architectures "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(_ssl_archives)
    set(_crypto_archives)
    set(_openssl_targets)
    foreach(_arch IN LISTS _architectures)
        if(_arch STREQUAL "arm64")
            set(_target darwin64-arm64-cc)
        elseif(_arch STREQUAL "x86_64")
            set(_target darwin64-x86_64-cc)
        else()
            message(FATAL_ERROR "Unsupported OpenSSL architecture: ${_arch}")
        endif()
        set(_prefix "${CMAKE_BINARY_DIR}/openssl-${_arch}")
        ExternalProject_Add(openssl_${_arch}
            SOURCE_DIR "${_openssl_source}"
            BINARY_DIR "${CMAKE_BINARY_DIR}/openssl-build-${_arch}"
            CONFIGURE_COMMAND "${PERL_EXECUTABLE}" "${_openssl_source}/Configure" ${_target}
                no-shared no-tests no-apps no-module no-legacy no-asm
                "--prefix=${_prefix}" --libdir=lib
                "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            BUILD_COMMAND "${REDIS_MAKE}" -j4
            INSTALL_COMMAND "${REDIS_MAKE}" install_sw
            BUILD_BYPRODUCTS "${_prefix}/lib/libssl.a" "${_prefix}/lib/libcrypto.a"
            UPDATE_COMMAND ""
            TEST_COMMAND "")
        list(APPEND _ssl_archives "${_prefix}/lib/libssl.a")
        list(APPEND _crypto_archives "${_prefix}/lib/libcrypto.a")
        list(APPEND _openssl_targets openssl_${_arch})
        set(_header_prefix "${_prefix}")
    endforeach()
    find_program(REDIS_LIPO lipo REQUIRED)
    add_custom_command(OUTPUT "${OPENSSL_ROOT_DIR}/lib/libssl.a" "${OPENSSL_ROOT_DIR}/lib/libcrypto.a"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPENSSL_ROOT_DIR}/lib"
        COMMAND "${REDIS_LIPO}" -create ${_ssl_archives} -output "${OPENSSL_ROOT_DIR}/lib/libssl.a"
        COMMAND "${REDIS_LIPO}" -create ${_crypto_archives} -output "${OPENSSL_ROOT_DIR}/lib/libcrypto.a"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_header_prefix}/include" "${OPENSSL_INCLUDE_DIR}"
        DEPENDS ${_openssl_targets})
    add_custom_target(redis_openssl DEPENDS "${OPENSSL_ROOT_DIR}/lib/libssl.a" "${OPENSSL_ROOT_DIR}/lib/libcrypto.a")
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libssl.a")
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libcrypto.a")
elseif(WIN32)
    find_program(REDIS_VSWHERE vswhere
        PATHS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer" REQUIRED)
    execute_process(COMMAND "${REDIS_VSWHERE}" -latest -products *
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
        -property installationPath
        OUTPUT_VARIABLE _vs_root OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(REDIS_VCVARS "${_vs_root}/VC/Auxiliary/Build/vcvars64.bat")
    if(NOT EXISTS "${REDIS_VCVARS}")
        message(FATAL_ERROR "Visual Studio x64 developer environment not found")
    endif()
    configure_file(cmake/openssl-windows.cmd.in
        "${CMAKE_BINARY_DIR}/openssl-windows.cmd" @ONLY NEWLINE_STYLE CRLF)
    ExternalProject_Add(redis_openssl
        SOURCE_DIR "${_openssl_source}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/openssl-build"
        CONFIGURE_COMMAND cmd /c "${CMAKE_BINARY_DIR}/openssl-windows.cmd" configure
        BUILD_COMMAND cmd /c "${CMAKE_BINARY_DIR}/openssl-windows.cmd" build
        INSTALL_COMMAND cmd /c "${CMAKE_BINARY_DIR}/openssl-windows.cmd" install
        BUILD_BYPRODUCTS "${OPENSSL_ROOT_DIR}/lib/libssl.lib" "${OPENSSL_ROOT_DIR}/lib/libcrypto.lib"
        UPDATE_COMMAND "" TEST_COMMAND "")
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libssl.lib")
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libcrypto.lib")
else()
    message(FATAL_ERROR "Only macOS and Windows builds are supported")
endif()

add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
add_dependencies(OpenSSL::Crypto redis_openssl)
add_dependencies(OpenSSL::SSL redis_openssl)
if(WIN32)
    set_property(TARGET OpenSSL::Crypto APPEND PROPERTY INTERFACE_LINK_LIBRARIES crypt32 ws2_32 advapi32 user32 bcrypt)
endif()
set(OPENSSL_FOUND TRUE)
set(OpenSSL_FOUND TRUE)
set(OPENSSL_VERSION "3.5.9")
