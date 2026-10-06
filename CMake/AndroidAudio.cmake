# The NDK does not provide the desktop audio codec libraries.
include(FetchContent)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libogg GIT_REPOSITORY https://github.com/xiph/ogg.git GIT_TAG v1.3.5)
FetchContent_MakeAvailable(libogg)
FetchContent_Declare(opus GIT_REPOSITORY https://github.com/xiph/opus.git GIT_TAG v1.5.2)
FetchContent_MakeAvailable(opus)
set(OGG_INCLUDE_DIR ${libogg_SOURCE_DIR}/include ${libogg_BINARY_DIR}/include)
set(OGG_LIBRARY ogg)
FetchContent_Declare(libvorbis GIT_REPOSITORY https://github.com/xiph/vorbis.git GIT_TAG v1.3.7)
FetchContent_MakeAvailable(libvorbis)
FetchContent_Declare(opusfile GIT_REPOSITORY https://github.com/xiph/opusfile.git GIT_TAG v0.12)
FetchContent_MakeAvailable(opusfile)
add_library(opusfile STATIC
    ${opusfile_SOURCE_DIR}/src/info.c
    ${opusfile_SOURCE_DIR}/src/internal.c
    ${opusfile_SOURCE_DIR}/src/opusfile.c
    ${opusfile_SOURCE_DIR}/src/stream.c)
target_include_directories(opusfile PUBLIC ${opusfile_SOURCE_DIR}/include)
target_link_libraries(opusfile PUBLIC opus ogg)

# Existing audio sources include the packaged <opus/opus.h> layout.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/codec_compat/opus")
file(GLOB opus_headers "${opus_SOURCE_DIR}/include/*.h")
file(COPY ${opus_headers} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/codec_compat/opus")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/codec_compat")

set(SDL2NET_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2_net GIT_REPOSITORY https://github.com/libsdl-org/SDL_net.git GIT_TAG release-2.2.0)
FetchContent_MakeAvailable(SDL2_net)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sdl_net_compat/SDL2")
file(COPY "${sdl2_net_SOURCE_DIR}/SDL_net.h" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/sdl_net_compat/SDL2")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/sdl_net_compat")
