if(NTGCALLS_TOOLCHAIN_INCLUDED)
    return()
endif(NTGCALLS_TOOLCHAIN_INCLUDED)
set(NTGCALLS_TOOLCHAIN_INCLUDED ON)

get_filename_component(deps_loc "${CMAKE_CURRENT_LIST_DIR}/../deps" REALPATH)
get_filename_component(props_loc "${CMAKE_CURRENT_LIST_DIR}/../version.properties" REALPATH)

find_package(Python3 REQUIRED COMPONENTS Interpreter QUIET)

include(${CMAKE_CURRENT_LIST_DIR}/PlatformUtils.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/FindCURL.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/PythonUtils.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/DownloadProject.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/GitUtils.cmake)

if (ANDROID_ABI)
    include(${CMAKE_CURRENT_LIST_DIR}/FindNDK.cmake)
    set(JUST_INSTALL_CLANG ON)
    include(${CMAKE_CURRENT_LIST_DIR}/FindClang.cmake)
else ()
    if (LINUX)
        include(${CMAKE_CURRENT_LIST_DIR}/FindClang.cmake)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE")
        if (USE_LIBCXX)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -nostdinc++")
        endif ()
    elseif (WINDOWS)
        include(${CMAKE_CURRENT_LIST_DIR}/FindVisualStudio.cmake)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /D_ITERATOR_DEBUG_LEVEL=0")
        string(LENGTH "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LENGTH)
        if (BUILD_TYPE_LENGTH GREATER 0)
            if (CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MTd")
            else ()
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
            endif ()
        endif ()
    elseif (MACOS)
        set(CMAKE_OSX_ARCHITECTURES "arm64")
        set(CMAKE_GENERATOR "Xcode")
    endif ()
endif ()