project(audiodecoder.vorbis)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(Kodi REQUIRED)
find_package(Ogg REQUIRED)
find_package(Vorbis REQUIRED)

include_directories(${KODI_INCLUDE_DIR}
                    ${OGG_INCLUDE_DIRS}
                    ${VORBIS_INCLUDE_DIRS})

add_definitions(-D__STDC_FORMAT_MACROS)
set(VORBIS_SOURCES src/OGGCodec.cpp)

set(DEPLIBS ${OGG_LIBRARIES}
            ${VORBIS_LIBRARIES})

build_addon(audiodecoder.vorbis VORBIS DEPLIBS)

include(CPack)
