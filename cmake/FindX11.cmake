set(X11_DIR ${deps_loc}/libx11)
set(X11_SRC ${X11_DIR}/src)
set(X11_GIT https://github.com/pytgcalls/libx11)

if (LINUX_ARM64)
    set(PLATFORM linux)
    set(ARCHIVE_FORMAT .tar.gz)
    set(ARCH arm64)
elseif (LINUX_x86_64)
    set(PLATFORM linux)
    set(ARCHIVE_FORMAT .tar.gz)
    set(ARCH x86_64)
else ()
    return()
endif ()

GetProperty("version.libX11" X11_VERSION)
message(STATUS "libX11 v${X11_VERSION}")

set(FILE_NAME libX11.${PLATFORM}-${ARCH}${ARCHIVE_FORMAT})
DownloadProject(
    URL ${X11_GIT}/releases/download/v${X11_VERSION}/${FILE_NAME}
    DOWNLOAD_DIR ${X11_DIR}/download
    SOURCE_DIR ${X11_SRC}
)

foreach (lib ${X11_LIBS})
    if(NOT TARGET x11::${lib})
        add_library(x11::${lib} UNKNOWN IMPORTED)
        set_target_properties(x11::${lib} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${X11_SRC}/include"
                IMPORTED_LOCATION "${X11_SRC}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endif ()
endforeach ()