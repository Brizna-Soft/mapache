cmake_minimum_required(VERSION 3.25)

# --- vcpkg: triplet propio (todo estático, solo Release) ---
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/triplets" CACHE STRING "")
set(VCPKG_TARGET_TRIPLET "x64-windows-static-rel" CACHE STRING "")
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _vcpkg)
    else()
        set(_vcpkg "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vcpkg")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${_vcpkg}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Solo la configuración Release: el triplet no compila las bibliotecas en Debug, y wxWidgets pide
# la carpeta lib/mswud (que no existe) cuando Visual Studio prepara también la configuración Debug
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)

# CRT estático, igual que las bibliotecas de vcpkg
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

file(STRINGS version.txt APP_VERSION LIMIT_COUNT 1)
project(Mapache VERSION ${APP_VERSION} LANGUAGES C CXX RC)
if(NOT APP_VERSION_TEXTO)
    set(APP_VERSION_TEXTO "${APP_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wxWidgets CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generado/version.h" @ONLY)

set(OPCIONES_CXX /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus /guard:cf)
# WIN32_LEAN_AND_MEAN: así <windows.h> no trae winsock.h, que choca con el winsock2.h de wxWidgets
set(DEFINICIONES UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)

# Opciones ocultas para probar (--limpieza-de-prueba=, --lento). Solo para desarrollo:
# nunca en lo que se publica (compilar.ps1 -Publicacion y el workflow las dejan apagadas).
option(MAPACHE_PRUEBAS "Compilar las opciones ocultas de prueba (solo desarrollo)" OFF)
if(MAPACHE_PRUEBAS)
    list(APPEND DEFINICIONES MAPACHE_PRUEBAS)
endif()

# --- El programa ---
file(GLOB FUENTES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
add_executable(Mapache WIN32 ${FUENTES} res/app.rc)
target_include_directories(Mapache PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generado")
target_compile_definitions(Mapache PRIVATE ${DEFINICIONES})
target_compile_options(Mapache PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${OPCIONES_CXX}>)
target_link_options(Mapache PRIVATE /MANIFEST:NO /guard:cf)
target_link_libraries(Mapache PRIVATE
    wx::core wx::base
    nlohmann_json::nlohmann_json
    shlwapi winhttp shell32 ole32 uuid)
