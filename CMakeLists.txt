cmake_minimum_required(VERSION 3.20)
project(CSPEmbind LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
set(CSP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/csp" CACHE PATH
    "Path to connected-spaces-platform source root")
set(CSP_BUILD_CONFIG "Debug" CACHE STRING "CSP build config: Debug or Release")

string(TOLOWER "${CSP_BUILD_CONFIG}" CSP_CONFIG_LOWER)
if(CSP_CONFIG_LOWER STREQUAL "debug")
    set(CSP_LIB_DIR "Debug")
else()
    set(CSP_LIB_DIR "Release")
endif()

# ---------------------------------------------------------------------------
# Imported: CSP static library (archived from .o files by build.sh)
# ---------------------------------------------------------------------------
add_library(csp_static STATIC IMPORTED)
set_target_properties(csp_static PROPERTIES
    IMPORTED_LOCATION "${CSP_ROOT}/libCSP_wasm.a"
)

# ---------------------------------------------------------------------------
# Imported: Third-party static libraries built by CSP's premake/make
# ---------------------------------------------------------------------------
set(CSP_THIRDPARTY_LIBS "")

macro(import_csp_lib name location)
    add_library(${name} STATIC IMPORTED)
    set_target_properties(${name} PROPERTIES IMPORTED_LOCATION "${location}")
    list(APPEND CSP_THIRDPARTY_LIBS ${name})
endmacro()

import_csp_lib(signalrclient
    "${CSP_ROOT}/ThirdParty/signalrclient/Binaries/wasm/${CSP_LIB_DIR}/libsignalrclient.a")
import_csp_lib(quickjs
    "${CSP_ROOT}/ThirdParty/quickjs/Binaries/wasm/${CSP_LIB_DIR}/libquickjs.a")
import_csp_lib(asyncplusplus
    "${CSP_ROOT}/ThirdParty/asyncplusplus/Binaries/wasm/${CSP_LIB_DIR}/libasyncplusplus.a")
import_csp_lib(tinyspline
    "${CSP_ROOT}/modules/_build/tinyspline/Binaries/wasm/${CSP_LIB_DIR}/libtinyspline.a")

# ---------------------------------------------------------------------------
# Include directories (must match CSP's premake includedirs for WASM)
# ---------------------------------------------------------------------------
set(CSP_INCLUDE_DIRS
    "${CSP_ROOT}/Library/include"
    "${CSP_ROOT}/Library/src"
    "${CSP_ROOT}/ThirdParty/signalrclient/include"
    "${CSP_ROOT}/ThirdParty/rapidjson/include"
    "${CSP_ROOT}/ThirdParty/msgpack/include"
    "${CSP_ROOT}/ThirdParty/quickjs/include"
    "${CSP_ROOT}/ThirdParty/glm"
    "${CSP_ROOT}/ThirdParty/asyncplusplus/include"
    "${CSP_ROOT}/ThirdParty/atomic_queue/include"
    "${CSP_ROOT}/ThirdParty/fmt/include"
    "${CSP_ROOT}/modules/csp-services/generated"
    "${CSP_ROOT}/modules/tinyspline/src"
)

# ---------------------------------------------------------------------------
# Preprocessor defines (must match CSP's premake defines for WASM)
# ---------------------------------------------------------------------------
set(CSP_DEFINES
    CSP_WASM
    USE_STD_MALLOC=1
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    NO_SIGNALRCLIENT_EXPORTS
    USE_MSGPACK
    POCO_NO_INOTIFY
    POCO_NO_FILECHANNEL
    POCO_NO_SPLITTERCHANNEL
    POCO_NO_SYSLOGCHANNEL
    POCO_UTIL_NO_INIFILECONFIGURATION
    POCO_UTIL_NO_JSONCONFIGURATION
    POCO_UTIL_NO_XMLCONFIGURATION
    POCO_NET_NO_IPv6
    LIBASYNC_STATIC
    LIBASYNC_CUSTOM_DEFAULT_SCHEDULER
    FMT_HEADER_ONLY
)

# ---------------------------------------------------------------------------
# Embind binding sources
# ---------------------------------------------------------------------------
set(EMBIND_SOURCES
    src/bindings/csp_bindings.cpp
)

# ---------------------------------------------------------------------------
# Target: csp_embind (final WASM executable)
# ---------------------------------------------------------------------------
add_executable(csp_embind ${EMBIND_SOURCES})

set_target_properties(csp_embind PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    SUFFIX ".js"
    OUTPUT_NAME "csp_embind"
)

target_include_directories(csp_embind PRIVATE ${CSP_INCLUDE_DIRS})
target_compile_definitions(csp_embind PRIVATE ${CSP_DEFINES})

# Compile flags — embind needs RTTI, so we compile with it enabled even though
# the CSP .a was built with -fno-rtti. We supply missing typeinfo below.
target_compile_options(csp_embind PRIVATE
    -pthread
    -fwasm-exceptions
)

# Link flags — --bind enables embind; rest matches CSP's premake linkoptions
target_link_options(csp_embind PRIVATE
    --bind
    -pthread
    -fwasm-exceptions
    -sPTHREAD_POOL_SIZE_STRICT=0
    "SHELL:-s EXPORTED_FUNCTIONS=['_malloc','_free']"
    -sEXPORT_ES6=1
    -sMODULARIZE=1
    "SHELL:-s EXPORT_NAME='createCSPModule'"
    -sFETCH
    -sALLOW_TABLE_GROWTH=1
    -sWASM_BIGINT
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=33554432
    -sMAXIMUM_MEMORY=1073741824
    "SHELL:-s ENVIRONMENT='web,worker'"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8']"
)

# Link against CSP and third-party libs
# Use whole-archive for CSP to ensure all symbols are available
target_link_libraries(csp_embind PRIVATE
    -Wl,--whole-archive
    csp_static
    -Wl,--no-whole-archive
    ${CSP_THIRDPARTY_LIBS}
    websocket.js
)
