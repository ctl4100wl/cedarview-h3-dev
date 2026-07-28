# CedarView target: Debian armhf on an ARMv7 Allwinner H3.
# Host: Debian amd64 under WSL2.
#
# Target development packages are installed with Debian multiarch under
# /usr/lib/arm-linux-gnueabihf. Qt code generators remain native amd64 tools.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_AR arm-linux-gnueabihf-ar CACHE FILEPATH "")
set(CMAKE_RANLIB arm-linux-gnueabihf-ranlib CACHE FILEPATH "")
set(CMAKE_STRIP arm-linux-gnueabihf-strip CACHE FILEPATH "")

# H3 is a Cortex-A7 with NEON/VFPv4 and the Debian armhf hard-float ABI.
set(_CEDARVIEW_H3_FLAGS "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${_CEDARVIEW_H3_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${_CEDARVIEW_H3_FLAGS}")

# Debian splits compiler sysroot files and multiarch development packages
# across these three prefixes. Put the target tuple paths first; an amd64
# library cannot be linked by the ARM linker and the finished ELF is checked.
set(CMAKE_LIBRARY_ARCHITECTURE arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
list(PREPEND CMAKE_PREFIX_PATH
    /usr/lib/arm-linux-gnueabihf/cmake
)
list(PREPEND CMAKE_LIBRARY_PATH
    /usr/lib/arm-linux-gnueabihf
)
list(PREPEND CMAKE_INCLUDE_PATH
    /usr/include/arm-linux-gnueabihf
)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Debian multiarch target Qt plus native amd64 moc/uic/rcc tools.
set(Qt6_DIR
    "/usr/lib/arm-linux-gnueabihf/cmake/Qt6"
    CACHE PATH "Target armhf Qt 6 package")
set(QT_HOST_PATH "/usr" CACHE PATH "Native amd64 Qt host tools")
set(QT_HOST_PATH_CMAKE_DIR
    "/usr/lib/x86_64-linux-gnu/cmake"
    CACHE PATH "Native amd64 Qt CMake packages")

# pkg-config must expose target libraries only.
set(PKG_CONFIG_EXECUTABLE "/usr/bin/pkg-config" CACHE FILEPATH "")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR}
    "/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/")
