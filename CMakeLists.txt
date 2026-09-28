# mullion-gtk, for projects built with CMake: as a subdirectory, through
# FetchContent, or installed with its pkg-config file. meson.build is the
# other way in and builds the same library from the same sources.

cmake_minimum_required(VERSION 3.21)
project(mullion-gtk VERSION 0.2.0 LANGUAGES C)

set(MLN_PACKAGE mullion-gtk-0)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MLN_GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.74)
pkg_check_modules(MLN_GTK REQUIRED IMPORTED_TARGET gtk4>=4.10)

# Static unless asked otherwise, so that an app bundle has no shared
# object to find.
option(MLN_SHARED "Build mullion-gtk as a shared library" OFF)
option(MLN_TESTS "Build and register the tests" ${PROJECT_IS_TOP_LEVEL})
# Built into an app (add_subdirectory, FetchContent), it installs nothing
# into the app's prefix.
option(MLN_INSTALL "Install the library, headers and pkg-config file" ${PROJECT_IS_TOP_LEVEL})
set(MLN_SHOTBOX "" CACHE PATH "A shotbox checkout for the widget tests, in place of the installed shotbox")

set(MLN_SOURCES src/mln-json.c src/mln-model.c src/mln-panes.c)

if(MLN_SHARED)
  add_library(mullion-gtk SHARED ${MLN_SOURCES})
  # Only the API is exported (MLN_EXPORT, mln-panes.h).
  set_target_properties(mullion-gtk PROPERTIES
    C_VISIBILITY_PRESET hidden
    SOVERSION 0)
  target_compile_definitions(mullion-gtk PRIVATE MLN_BUILDING_SHARED)
else()
  add_library(mullion-gtk STATIC ${MLN_SOURCES})
  set_target_properties(mullion-gtk PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# The warnings meson's warning_level=2 gives.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mullion-gtk PRIVATE -Wall -Wextra
    -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

add_library(mullion-gtk::mullion-gtk ALIAS mullion-gtk)

set_target_properties(mullion-gtk PROPERTIES
  OUTPUT_NAME ${MLN_PACKAGE}
  C_STANDARD 11
  C_EXTENSIONS ON
  VERSION ${PROJECT_VERSION})

target_include_directories(mullion-gtk PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include/${MLN_PACKAGE}>)

target_link_libraries(mullion-gtk PUBLIC PkgConfig::MLN_GTK PkgConfig::MLN_GLIB)

find_library(MLN_LIBM m)
if(MLN_LIBM)
  target_link_libraries(mullion-gtk PRIVATE ${MLN_LIBM})
endif()

include(GNUInstallDirs)

if(MLN_INSTALL)
  install(TARGETS mullion-gtk
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES src/mullion-gtk.h src/mln-panes.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${MLN_PACKAGE})

  # The prefix relative to where the file is, so that it is right wherever
  # the library ends up (`cmake --install --prefix' says where only after
  # this is written); absolute where the libdir was given as one, as a
  # packager's is.
  if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(MLN_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
    set(MLN_PC_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
    set(MLN_PC_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
  else()
    file(RELATIVE_PATH MLN_PC_UP "/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/")
    set(MLN_PC_PREFIX "\${pcfiledir}/${MLN_PC_UP}")
    set(MLN_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
    set(MLN_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
  endif()

  set(MLN_PC ${CMAKE_CURRENT_BINARY_DIR}/${MLN_PACKAGE}.pc)
  file(WRITE ${MLN_PC}
"prefix=${MLN_PC_PREFIX}
includedir=${MLN_PC_INCLUDEDIR}
libdir=${MLN_PC_LIBDIR}

Name: mullion-gtk
Description: Tiled panes for GTK 4 apps
Version: ${PROJECT_VERSION}
Requires: glib-2.0 >= 2.74, gtk4 >= 4.10
Libs: -L\${libdir} -l${MLN_PACKAGE}
Libs.private: -lm
Cflags: -I\${includedir}/${MLN_PACKAGE}
")
  install(FILES ${MLN_PC} DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

if(MLN_TESTS)
  enable_testing()

  add_executable(mullion-demo demo/demo.c)
  target_link_libraries(mullion-demo PRIVATE mullion-gtk)

  # The unit tests reach into the model, which a shared build hides: they
  # link a static copy of it.
  add_library(mullion-model-tests STATIC src/mln-json.c src/mln-model.c)
  target_include_directories(mullion-model-tests PUBLIC src)
  target_link_libraries(mullion-model-tests PUBLIC PkgConfig::MLN_GLIB)
  if(MLN_LIBM)
    target_link_libraries(mullion-model-tests PUBLIC ${MLN_LIBM})
  endif()

  foreach(t json layouts model)
    add_executable(test-${t} tests/test-${t}.c)
    target_link_libraries(test-${t} PRIVATE mullion-model-tests)
    add_test(NAME ${t} COMMAND test-${t})
    set_tests_properties(${t} PROPERTIES
      ENVIRONMENT "MLN_FIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests")
  endforeach()

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME widget
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/widget.py
              $<TARGET_FILE:mullion-demo>)
    # 77 is the widget suite saying it was skipped (no shotbox).
    set(widget_env "MESON_BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}")
    if(MLN_SHOTBOX)
      list(APPEND widget_env "PYTHONPATH=${MLN_SHOTBOX}")
    endif()
    set_tests_properties(widget PROPERTIES
      ENVIRONMENT "${widget_env}"
      SKIP_RETURN_CODE 77
      TIMEOUT 900
      RUN_SERIAL TRUE)
  endif()
endif()
