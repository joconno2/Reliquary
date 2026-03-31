set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Prefer posix threading variant (win32 variant breaks C++20 <memory>)
find_program(MINGW_GCC x86_64-w64-mingw32-gcc-posix)
find_program(MINGW_GXX x86_64-w64-mingw32-g++-posix)
if(NOT MINGW_GCC)
    set(MINGW_GCC x86_64-w64-mingw32-gcc)
endif()
if(NOT MINGW_GXX)
    set(MINGW_GXX x86_64-w64-mingw32-g++)
endif()
set(CMAKE_C_COMPILER ${MINGW_GCC})
set(CMAKE_CXX_COMPILER ${MINGW_GXX})
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Pkg-config for cross-compiled libraries
set(ENV{PKG_CONFIG_PATH} "/usr/x86_64-w64-mingw32/lib/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-w64-mingw32/lib/pkgconfig")
