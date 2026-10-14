cmake_minimum_required(VERSION 3.16)
project(SonicAdvanceTrilogy C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(BUILD_TESTS "Compilar la prueba sin ventana (headless_test)" OFF)

if(MSVC)
  message(FATAL_ERROR "En Windows compila con MinGW-w64 (MSYS2: pacman -S mingw-w64-x86_64-gcc cmake ninja SDL2) o con clang.")
endif()

# ---------------------------------------------------------------------------------------
# mGBA (sin modificar) compilado como biblioteca estática DENTRO de este proyecto.
# Lista de fuentes = núcleo de GBA; mismas opciones que usa el CMake oficial en el port libretro.
# ---------------------------------------------------------------------------------------
set(MGBA ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mgba)
set(ARM_SRC arm.c decoder-arm.c decoder.c decoder-thumb.c isa-arm.c isa-thumb.c)
set(CORE_SRC bitmap-cache.c cache-set.c cheats.c config.c core.c directories.c input.c interface.c lockstep.c
  log.c map-cache.c mem-search.c rewind.c serialize.c sync.c thread.c tile-cache.c timing.c)
set(GBA_SRC audio.c bios.c cart/ereader.c cart/gpio.c cart/matrix.c cart/unlicensed.c cart/vfame.c cheats.c
  cheats/codebreaker.c cheats/gameshark.c cheats/parv3.c core.c dma.c gba.c hle-bios.c input.c io.c memory.c
  overrides.c renderers/cache-set.c renderers/common.c renderers/gl.c renderers/software-bg.c
  renderers/software-mode0.c renderers/software-obj.c renderers/video-software.c savedata.c serialize.c
  sharkport.c sio.c sio/gbp.c sio/dolphin.c sio/lockstep.c timer.c video.c)
set(UTIL_SRC circle-buffer.c configuration.c crc32.c formatting.c gbk-table.c hash.c md5.c sha1.c string.c table.c
  vector.c vfs.c audio-buffer.c audio-resampler.c convolve.c elf-read.c geometry.c image.c image/export.c
  image/font.c image/png-io.c interpolator.c patch.c patch-fast.c patch-ips.c patch-ups.c ring-fifo.c sfo.c
  text-codec.c vfs/vfs-mem.c vfs/vfs-fifo.c vfs/vfs-fd.c)

set(MGBA_SRCS)
foreach(f ${ARM_SRC})
  list(APPEND MGBA_SRCS ${MGBA}/src/arm/${f})
endforeach()
foreach(f ${CORE_SRC})
  list(APPEND MGBA_SRCS ${MGBA}/src/core/${f})
endforeach()
foreach(f ${GBA_SRC})
  list(APPEND MGBA_SRCS ${MGBA}/src/gba/${f})
endforeach()
foreach(f ${UTIL_SRC})
  list(APPEND MGBA_SRCS ${MGBA}/src/util/${f})
endforeach()
list(APPEND MGBA_SRCS ${MGBA}/src/gb/audio.c ${MGBA}/src/third-party/inih/ini.c
  ${MGBA}/src/feature/thread-proxy.c ${MGBA}/src/feature/video-logger.c ${CMAKE_CURRENT_SOURCE_DIR}/third_party/version.c)
if(WIN32)
  file(GLOB OS_SRC ${MGBA}/src/platform/windows/*.c)
else()
  file(GLOB OS_SRC ${MGBA}/src/platform/posix/*.c)
  list(APPEND OS_SRC ${MGBA}/src/util/vfs/vfs-dirent.c)
endif()
list(APPEND MGBA_SRCS ${OS_SRC})

add_library(emu_core STATIC ${MGBA_SRCS} src/emu.c src/debug_log.c)
target_include_directories(emu_core PUBLIC ${MGBA}/include src PRIVATE ${MGBA}/src)
target_compile_definitions(emu_core PRIVATE
  M_CORE_GBA ENABLE_VFS ENABLE_DIRECTORIES ENABLE_VFS_FD MINIMAL_CORE=2 MGBA_STANDALONE
  HAVE_STRDUP)
if(WIN32)
  target_compile_definitions(emu_core PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
  target_link_libraries(emu_core PUBLIC ws2_32 shlwapi)
elseif(APPLE)
  target_compile_definitions(emu_core PRIVATE USE_PTHREADS _DARWIN_C_SOURCE HAVE_LOCALE HAVE_XLOCALE HAVE_SNPRINTF_L HAVE_STRTOF_L)
  find_package(Threads REQUIRED)
  target_link_libraries(emu_core PUBLIC Threads::Threads)
else()
  target_compile_definitions(emu_core PRIVATE USE_PTHREADS _GNU_SOURCE HAVE_LOCALE)
  find_package(Threads REQUIRED)
  target_link_libraries(emu_core PUBLIC Threads::Threads)
endif()
if(NOT WIN32)
  target_compile_definitions(emu_core PRIVATE HAVE_LOCALTIME_R)   # en Windows mGBA usa su propia localtime_r (localtime_s)
endif()
target_compile_options(emu_core PRIVATE -w)
find_library(MATH_LIB m)
if(MATH_LIB)
  target_link_libraries(emu_core PUBLIC ${MATH_LIB})
endif()

# ---------------------------------------------------------------------------------------
# Núcleo del launcher (portable): gráficos por software, PNG, interfaz, lógica
# ---------------------------------------------------------------------------------------
add_library(launcher_core STATIC src/gfx.c src/png.c src/font_data.c src/sys.c src/config.c src/app_logic.c src/app_ui.c)
target_include_directories(launcher_core PUBLIC src)
target_link_libraries(launcher_core PUBLIC emu_core)
set_target_properties(emu_core launcher_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(ANDROID)
  # Un APK debug también debe tener el núcleo de emulación optimizado; -O0 es demasiado lento en móviles.
  target_compile_options(emu_core PRIVATE -O2)
  target_compile_options(launcher_core PRIVATE -O2)
endif()

# ---------------------------------------------------------------------------------------
# Android: SDL2 oficial + una biblioteca nativa "main" cargada por SDLActivity.
# La carpeta android/SDL2 se descarga automáticamente desde Android Gradle al sincronizar.
# ---------------------------------------------------------------------------------------
if(ANDROID)
  set(SDL2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android/SDL2" CACHE PATH "Ruta al código fuente SDL2")
  if(NOT EXISTS "${SDL2_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "No se encuentra SDL2 en ${SDL2_SOURCE_DIR}. Abre android/ en Android Studio con conexión a Internet para descargar SDL2 automáticamente, o coloca SDL2 2.30.9 ahí.")
  endif()
  set(SDL_SHARED ON CACHE BOOL "Build SDL2 shared library" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "Build SDL2 static library" FORCE)
  set(SDL_TEST OFF CACHE BOOL "Disable SDL2 tests" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "Disable SDL2 tests" FORCE)
  add_subdirectory("${SDL2_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/SDL2")
  if(TARGET SDL2::SDL2)
    set(SDL_TARGET SDL2::SDL2)
  elseif(TARGET SDL2)
    set(SDL_TARGET SDL2)
  elseif(TARGET SDL2-static)
    set(SDL_TARGET SDL2-static)
  else()
    message(FATAL_ERROR "SDL2 no creó un destino conocido (SDL2::SDL2 / SDL2 / SDL2-static)")
  endif()
  add_library(main SHARED src/main_sdl.c)
  target_link_libraries(main PRIVATE launcher_core ${SDL_TARGET} android log)
  target_compile_options(main PRIVATE -Wall -Wextra -O2)
  target_compile_definitions(main PRIVATE NDEBUG)
  set_target_properties(main PROPERTIES OUTPUT_NAME main)
  return()
endif()

# ---------------------------------------------------------------------------------------
# Plataforma de escritorio (SDL2). Si SDL2 no está instalado se descarga y compila (hace falta internet).
# ---------------------------------------------------------------------------------------
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
  message(STATUS "SDL2 no encontrado: se descargará y compilará con FetchContent")
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2 URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
  FetchContent_MakeAvailable(SDL2)
endif()
if(TARGET SDL2::SDL2)
  set(SDL_TARGET SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
  set(SDL_TARGET SDL2::SDL2-static)
elseif(TARGET SDL2-static)
  set(SDL_TARGET SDL2-static)
else()
  message(FATAL_ERROR "No se encontró ningún destino de SDL2")
endif()
message(STATUS "Usando SDL2: ${SDL_TARGET}")

add_executable(SonicAdvanceTrilogy src/main_sdl.c)
target_link_libraries(SonicAdvanceTrilogy PRIVATE launcher_core ${SDL_TARGET})
if(WIN32)
  target_link_options(SonicAdvanceTrilogy PRIVATE -mwindows -static-libgcc)
endif()

install(TARGETS SonicAdvanceTrilogy DESTINATION .)
if(WIN32 AND SDL_TARGET STREQUAL "SDL2::SDL2")
  install(FILES $<TARGET_FILE:SDL2::SDL2> DESTINATION .)
endif()
install(DIRECTORY dist/ DESTINATION .)
install(FILES third_party/mgba/LICENSE DESTINATION licencias RENAME mGBA-LICENSE-MPL-2.0.txt)
install(FILES third_party/DejaVu-LICENSE.txt DESTINATION licencias)

if(BUILD_TESTS)
  enable_testing()
  add_executable(headless_test tests/headless.c)
  target_link_libraries(headless_test PRIVATE launcher_core)
  add_test(NAME headless COMMAND headless_test ${CMAKE_BINARY_DIR}/test_work)
endif()
