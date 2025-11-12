if (VCPKG_TARGET_IS_LINUX)
    message(NOTICE [[
openssl requires Linux kernel headers from the system package manager.
   They can be installed on Alpine systems via `apk add linux-headers`.
   They can be installed on Ubuntu systems via `apt install linux-libc-dev`.
]])
endif()

if(VCPKG_HOST_IS_WINDOWS)
    vcpkg_acquire_msys(MSYS_ROOT PACKAGES make perl)
    set(MAKE "${MSYS_ROOT}/usr/bin/make.exe")
    set(PERL "${MSYS_ROOT}/usr/bin/perl.exe")
else()
    find_program(MAKE make)
    if(NOT MAKE)
        message(FATAL_ERROR "Could not find make. Please install it through your package manager.")
    endif()
    vcpkg_find_acquire_program(PERL)
endif()
set(INTERPRETER "${PERL}")

execute_process(
    COMMAND "${PERL}" -e "use IPC::Cmd;"
    RESULT_VARIABLE perl_ipc_cmd_result
)
if(NOT perl_ipc_cmd_result STREQUAL "0")
    message(FATAL_ERROR "\nPerl cannot find IPC::Cmd. Please install it through your system package manager.\n")
endif()

# Ideally, OpenSSL should use `CC` from vcpkg as is (absolute path).
# But in reality, OpenSSL expects to locate the compiler via `PATH`,
# and it makes its own choices e.g. for Android.
vcpkg_cmake_get_vars(cmake_vars_file)
include("${cmake_vars_file}")
cmake_path(GET VCPKG_DETECTED_CMAKE_C_COMPILER PARENT_PATH compiler_path)
cmake_path(GET VCPKG_DETECTED_CMAKE_C_COMPILER FILENAME compiler_name)
find_program(compiler_in_path NAMES "${compiler_name}" PATHS ENV PATH NO_DEFAULT_PATH)
if(NOT compiler_in_path)
    vcpkg_host_path_list(APPEND ENV{PATH} "${compiler_path}")
elseif(NOT compiler_in_path STREQUAL VCPKG_DETECTED_CMAKE_C_COMPILER)
    vcpkg_host_path_list(PREPEND ENV{PATH} "${compiler_path}")
endif()

vcpkg_list(SET MAKEFILE_OPTIONS)
if(VCPKG_TARGET_IS_ANDROID)
    set(ENV{ANDROID_NDK_ROOT} "${VCPKG_DETECTED_CMAKE_ANDROID_NDK}")
    set(OPENSSL_ARCH "android-${VCPKG_DETECTED_CMAKE_ANDROID_ARCH}")
    if(VCPKG_DETECTED_CMAKE_ANDROID_ARCH STREQUAL "arm" AND NOT VCPKG_DETECTED_CMAKE_ANDROID_ARM_NEON)
        vcpkg_list(APPEND CONFIGURE_OPTIONS no-asm)
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    if(VCPKG_TARGET_ARCHITECTURE MATCHES "arm64")
        set(OPENSSL_ARCH linux-aarch64)
    elseif(VCPKG_TARGET_ARCHITECTURE MATCHES "arm")
        set(OPENSSL_ARCH linux-armv4)
    elseif(VCPKG_TARGET_ARCHITECTURE MATCHES "x64")
        set(OPENSSL_ARCH linux-x86_64)
    elseif(VCPKG_TARGET_ARCHITECTURE MATCHES "x86")
        set(OPENSSL_ARCH linux-x86)
    else()
        set(OPENSSL_ARCH linux-generic32)
    endif()
elseif(VCPKG_TARGET_IS_IOS)
    if(VCPKG_TARGET_ARCHITECTURE MATCHES "arm64")
        set(OPENSSL_ARCH ios64-xcrun)
    elseif(VCPKG_TARGET_ARCHITECTURE MATCHES "arm")
        set(OPENSSL_ARCH ios-xcrun)
    elseif(VCPKG_TARGET_ARCHITECTURE MATCHES "x86" OR VCPKG_TARGET_ARCHITECTURE MATCHES "x64")
        set(OPENSSL_ARCH iossimulator-xcrun)
    else()
        message(FATAL_ERROR "Unknown iOS target architecture: ${VCPKG_TARGET_ARCHITECTURE}")
    endif()
    # disable that makes linkage error (e.g. require stderr usage)
    list(APPEND CONFIGURE_OPTIONS no-ui no-asm)
elseif(VCPKG_TARGET_IS_TVOS OR VCPKG_TARGET_IS_WATCHOS)
    set(OPENSSL_ARCH iphoneos-cross)
    # disable that makes linkage error (e.g. require stderr usage)
    list(APPEND CONFIGURE_OPTIONS no-ui no-asm)
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_TARGET_ARCHITECTURE MATCHES "arm64")
        set(OPENSSL_ARCH darwin64-arm64)
    else()
        set(OPENSSL_ARCH darwin64-x86_64)
    endif()
elseif(VCPKG_TARGET_IS_BSD)
    set(OPENSSL_ARCH BSD-nodef-generic64)
elseif(VCPKG_TARGET_IS_SOLARIS)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(OPENSSL_ARCH solaris64-x86_64-gcc)
    else()
        set(OPENSSL_ARCH solaris-x86-gcc)
    endif()
elseif(VCPKG_TARGET_IS_MINGW)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(OPENSSL_ARCH mingw64)
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(OPENSSL_ARCH mingwarm64)
    else()
        set(OPENSSL_ARCH mingw)
    endif()
elseif(VCPKG_TARGET_IS_EMSCRIPTEN)
    set(OPENSSL_ARCH linux-x32)
    vcpkg_list(APPEND CONFIGURE_OPTIONS
        no-engine
        no-asm
        no-sse2
        no-srtp
        --cross-compile-prefix=
    )
    # Cf. https://emscripten.org/docs/porting/pthreads.html:
    # For Pthreads support, not just openssl but everything
    # must be compiled and linked with `-pthread`.
    # This makes it a triplet/toolchain-wide setting.
    if(NOT " ${VCPKG_DETECTED_CMAKE_C_FLAGS} " MATCHES " -pthread ")
        vcpkg_list(APPEND CONFIGURE_OPTIONS no-threads)
    endif()
else()
    message(FATAL_ERROR "Unknown platform")
endif()

if(VCPKG_TARGET_IS_LINUX)
    set(_anofox_kernel_header "/usr/include/linux/version.h")
    if(NOT EXISTS "${_anofox_kernel_header}")
        if(EXISTS "/etc/alpine-release")
            set(_anofox_header_cmd "apk add --no-cache linux-headers")
        elseif(EXISTS "/etc/debian_version")
            set(_anofox_header_cmd "apt-get update && apt-get install -y linux-libc-dev")
        elseif(EXISTS "/etc/redhat-release")
            set(_anofox_header_cmd "yum install -y kernel-headers")
        else()
            set(_anofox_header_cmd "")
        endif()
        if(_anofox_header_cmd)
            vcpkg_execute_required_process(
                COMMAND "${CMAKE_COMMAND}" -E env bash -lc "${_anofox_header_cmd}"
                WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}"
                LOGNAME anofox-install-kernel-headers
            )
        endif()
        if(NOT EXISTS "${_anofox_kernel_header}")
            message(FATAL_ERROR "Linux kernel headers are required to build OpenSSL but could not be installed automatically. Please install them with your system package manager.")
        endif()
    endif()
endif()

file(MAKE_DIRECTORY "${SOURCE_PATH}/vcpkg")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/configure" DESTINATION "${SOURCE_PATH}/vcpkg")
if(NOT VCPKG_HOST_IS_WINDOWS)
    file(CHMOD "${SOURCE_PATH}/vcpkg/configure" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
vcpkg_configure_make(
    SOURCE_PATH "${SOURCE_PATH}"
    PROJECT_SUBPATH "vcpkg"
    NO_ADDITIONAL_PATHS
    OPTIONS
        "${INTERPRETER}"
        "${SOURCE_PATH}/Configure"
        ${OPENSSL_ARCH}
        ${CONFIGURE_OPTIONS}
        "--openssldir=/etc/ssl"
        "--libdir=lib"
    OPTIONS_DEBUG
        --debug
)
set(_anofox_disable_parallel "")
if(VCPKG_TARGET_IS_WINDOWS AND VCPKG_TARGET_IS_MINGW)
    set(_anofox_disable_parallel DISABLE_PARALLEL)
endif()
vcpkg_install_make(
    ${MAKEFILE_OPTIONS}
    ${_anofox_disable_parallel}
    BUILD_TARGET build_inst_sw
)
vcpkg_fixup_pkgconfig()

if("tools" IN_LIST FEATURES)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
    file(RENAME "${CURRENT_PACKAGES_DIR}/bin/c_rehash" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/c_rehash")
    file(REMOVE "${CURRENT_PACKAGES_DIR}/debug/bin/c_rehash")
    vcpkg_copy_tools(TOOL_NAMES openssl AUTO_CLEAN)
elseif(VCPKG_LIBRARY_LINKAGE STREQUAL "static" OR NOT VCPKG_TARGET_IS_WINDOWS)
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/etc/ssl/misc")
endif()

file(TOUCH "${CURRENT_PACKAGES_DIR}/etc/ssl/certs/.keep")
file(TOUCH "${CURRENT_PACKAGES_DIR}/etc/ssl/private/.keep")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/etc"
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

# For consistency of mingw build with nmake build
file(GLOB engines "${CURRENT_PACKAGES_DIR}/lib/ossl-modules/*.dll")
if(NOT engines STREQUAL "")
    file(COPY ${engines} DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib/ossl-modules")
endif()
file(GLOB engines "${CURRENT_PACKAGES_DIR}/debug/lib/ossl-modules/*.dll")
if(NOT engines STREQUAL "")
    file(COPY ${engines} DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/lib/ossl-modules")
endif()
