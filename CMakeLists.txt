cmake_minimum_required(VERSION 3.19)

project(tinyshot VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# No .ui files: every dialog is built in code.
set(CMAKE_AUTOUIC OFF)

include(GNUInstallDirs)
include(CTest) # provides BUILD_TESTING

# Qt 6.3: the QWidget::addAction(text, ...) convenience factories and
# QCryptographicHash::addData(QByteArrayView) are the two 6.3 APIs the code uses.
find_package(Qt6 6.3 REQUIRED COMPONENTS Widgets Network DBus PrintSupport LinguistTools)
if(BUILD_TESTING)
  find_package(Qt6 6.3 REQUIRED COMPONENTS Test)
endif()

file(GLOB_RECURSE LS_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

qt_add_executable(tinyshot ${LS_SOURCES} resources/art.qrc)

target_include_directories(tinyshot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(tinyshot PRIVATE LS_VERSION="${PROJECT_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tinyshot PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(tinyshot PRIVATE
                      Qt6::Widgets
                      Qt6::Network
                      Qt6::DBus
                      Qt6::PrintSupport)

# Translations: English is the source language, so translations/tinyshot_en.ts collects the
# strings of the user interface; a further <lang>.ts is picked up by the same lupdate run.
# The .qm files are embedded under :/i18n/ and installed by src/app/translation.cpp.
qt_add_translations(tinyshot
                    TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/tinyshot_en.ts"
                    RESOURCE_PREFIX "/i18n")

# Optional: the XGrabKey fallback for the global hotkeys when the session has no
# org.freedesktop.portal.GlobalShortcuts (see src/app/hotkeyportal.cpp).
find_package(X11)
if(X11_FOUND)
  message(STATUS "tinyshot: X11 found - building the XGrabKey hotkey fallback")
  target_compile_definitions(tinyshot PRIVATE LS_HAVE_X11)
  target_include_directories(tinyshot PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(tinyshot PRIVATE X11::X11)
else()
  message(STATUS "tinyshot: X11 not found - global hotkeys use xdg-desktop-portal only")
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS tinyshot RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Exec=tinyshot, which is what the .deb needs: CPack stages the package under
# /usr, so the plain name is right there.  scripts/install.sh rewrites the
# installed entry to the absolute path of its prefix, where bin/ may not be on
# PATH.
install(FILES resources/io.github.pawslaves.Tinyshot.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES packaging/io.github.pawslaves.Tinyshot.metainfo.xml
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
install(FILES LICENSE
        DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME copyright)

# The icons and the toolbar artwork are SVG, drawn through the Qt SVG image plugin;
# the plugins live in qt6-svg-plugins, which dpkg-shlibdeps cannot see (the binary
# does not link Qt6::Svg), so the package names it explicitly.
install(FILES resources/icons/io.github.pawslaves.Tinyshot.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")

# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------
set(TINYSHOT_MAINTAINER "pawslaves <164549221+pawslaves@users.noreply.github.com>" CACHE STRING
    "Maintainer field of the generated Debian package")

set(CPACK_PACKAGE_NAME "tinyshot")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Tinyshot")
set(CPACK_PACKAGE_CONTACT "${TINYSHOT_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tiny screenshot tool with a Lightshot-style selection overlay")
set(CPACK_PACKAGE_DESCRIPTION
    "Tinyshot grabs the whole screen or a region you drag out, lets you annotate the\n\
shot with pen, line, arrow, rectangle, marker and text tools, and then copies,\n\
saves or prints it.  Global hotkeys work on Wayland and X11; the optional\n\
prnt.sc upload is off by default.\n\
.\n\
https://github.com/pawslaves/tinyshot")

# Stripped payload and 0755 directories: the stage tree would otherwise inherit the
# builder's umask and the symbol table.
set(CPACK_STRIP_FILES ON)
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)  # tinyshot_<version>_<arch>.deb
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${TINYSHOT_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/pawslaves/tinyshot")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-svg-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "xdg-desktop-portal")
set(CPACK_GENERATOR "DEB")
include(CPack)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
