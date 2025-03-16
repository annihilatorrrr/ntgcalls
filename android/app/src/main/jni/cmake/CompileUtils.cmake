file(READ ${ANDROID_CMAKE_DIR} _ANDROID_CMAKE_DIR_CONTENTS)
string(REGEX REPLACE "\n" ";" _ANDROID_CMAKE_DIR_CONTENTS "${_ANDROID_CMAKE_DIR_CONTENTS}")
set(BASE_COMPILE_OPTIONS)
set(BASE_COMPILE_DEFINITIONS)
set(_READING_OPTIONS OFF)
set(_READING_DEFINITIONS OFF)
foreach (_LINE IN LISTS _ANDROID_CMAKE_DIR_CONTENTS)
    if (_LINE MATCHES "target_compile_options")
        set(_READING_OPTIONS ON)
        continue()
    endif()
    if (_LINE MATCHES "target_compile_definitions")
        set(_READING_DEFINITIONS ON)
        continue()
    endif()
    if (_READING_OPTIONS OR _READING_DEFINITIONS)
        if (_LINE MATCHES "PRIVATE" OR _LINE MATCHES "PUBLIC")
            continue()
        endif()
        if (_LINE MATCHES "\\)")
            set(_READING_OPTIONS OFF)
            set(_READING_DEFINITIONS OFF)
            continue()
        endif()
        string(STRIP "${_LINE}" _LINE)
        if (_READING_OPTIONS)
            list(APPEND BASE_COMPILE_OPTIONS ${_LINE})
        else ()
            list(APPEND BASE_COMPILE_DEFINITIONS ${_LINE})
        endif()
    endif()
endforeach()