include(${CMAKE_SOURCE_DIR}/cmake/GNUInstallDirs.cmake)
set(ARANGODB_SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(CMAKE_INSTALL_SYSCONFDIR_ARANGO "${CMAKE_INSTALL_SYSCONFDIR}/${CMAKE_PROJECT_NAME}")
set(CMAKE_INSTALL_FULL_SYSCONFDIR_ARANGO "${CMAKE_INSTALL_FULL_SYSCONFDIR}/${CMAKE_PROJECT_NAME}")

set(CMAKE_INSTALL_DATAROOTDIR_ARANGO "${CMAKE_INSTALL_DATAROOTDIR}/${CMAKE_PROJECT_NAME}")
set(CMAKE_INSTALL_FULL_DATAROOTDIR_ARANGO "${CMAKE_INSTALL_FULL_DATAROOTDIR}/${CMAKE_PROJECT_NAME}")

if (MSVC OR DARWIN)
  set(ENABLE_UID_CFG false)
else ()
  set(ENABLE_UID_CFG true)
endif ()
if (MSVC)
  # if we wouldn't do this, we would have to deploy the DLLs twice.
  set(CMAKE_INSTALL_SBINDIR ${CMAKE_INSTALL_BINDIR})
endif()

set(CMAKE_INSTALL_SYSCONFDIR_ARANGO "${CMAKE_INSTALL_SYSCONFDIR}/${CMAKE_PROJECT_NAME}")
set(CMAKE_INSTALL_FULL_SYSCONFDIR_ARANGO "${CMAKE_INSTALL_FULL_SYSCONFDIR}/${CMAKE_PROJECT_NAME}")

# database directory
set(ARANGODB_DB_DIRECTORY "${PROJECT_BINARY_DIR}/var/lib/${CMAKE_PROJECT_NAME}")
FILE(MAKE_DIRECTORY ${ARANGODB_DB_DIRECTORY})

# apps
set(ARANGODB_APPS_DIRECTORY "${PROJECT_BINARY_DIR}/var/lib/${CMAKE_PROJECT_NAME}-apps")
FILE(MAKE_DIRECTORY "${ARANGODB_APPS_DIRECTORY}")

# logs
FILE(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/var/log/${CMAKE_PROJECT_NAME}")

set(INSTALL_ICU_DT_DEST "${CMAKE_INSTALL_DATAROOTDIR}/arangodb3")

include(InstallMacros)

# install ----------------------------------------------------------------------
install(DIRECTORY ${PROJECT_SOURCE_DIR}/Documentation/man/
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/man)

install_readme(README README.txt)
install_readme(README.md README.md)
install_readme(LICENSES-OTHER-COMPONENTS.md LICENSES-OTHER-COMPONENTS.md)

if (USE_ENTERPRISE)
  install_readme(enterprise/LICENSE LICENSE.txt)
else ()
  install_readme(LICENSE LICENSE.txt)
endif ()

# Custom targets ----------------------------------------------------------------
# love
add_custom_target (love
  COMMENT "ArangoDB loves you."
  COMMAND ""
  )

include(InstallArangoDBJSClient)

################################################################################
### @brief install server-side JavaScript files
################################################################################

install(
  DIRECTORY
    ${PROJECT_SOURCE_DIR}/js/actions
    ${PROJECT_SOURCE_DIR}/js/apps
    ${PROJECT_SOURCE_DIR}/js/contrib
    ${PROJECT_SOURCE_DIR}/js/server
  DESTINATION
    ${CMAKE_INSTALL_DATAROOTDIR_ARANGO}/js
  REGEX
    "^.*/server/tests$" EXCLUDE
  REGEX
    "^.*/aardvark/APP/node_modules$" EXCLUDE
)

if (USE_ENTERPRISE)
  install(
    DIRECTORY
      ${PROJECT_SOURCE_DIR}/enterprise/js/server
    DESTINATION
      ${CMAKE_INSTALL_DATAROOTDIR_ARANGO}/js
    REGEX
      "^.*/server/tests$" EXCLUDE
    REGEX
      "^.*/aardvark/APP/node_modules$" EXCLUDE
  )
endif ()

################################################################################
### @brief install log directory
################################################################################

install(
  DIRECTORY
    ${PROJECT_BINARY_DIR}/var/log/arangodb3
  DESTINATION
    ${CMAKE_INSTALL_LOCALSTATEDIR}/log
)

################################################################################
### @brief install database directory
################################################################################

install(
  DIRECTORY
    ${ARANGODB_DB_DIRECTORY}
  DESTINATION
    ${CMAKE_INSTALL_LOCALSTATEDIR}/lib
)

################################################################################
### @brief install apps directory
################################################################################

install(
  DIRECTORY
    ${ARANGODB_APPS_DIRECTORY}
  DESTINATION
    ${CMAKE_INSTALL_LOCALSTATEDIR}/lib
)

################################################################################
### @brief detect if we're on a systemd enabled system; if install unit file.
################################################################################

set(IS_SYSTEMD_INSTALL 0)
set(SYSTEMD_UNIT_DIR "")
if (UNIX)
  find_package(PkgConfig QUIET)
  pkg_check_modules(SYSTEMD systemd)
  if (SYSTEMD_FOUND)
    # cmake to old: pkg_get_variable(SYSTEMD_UNIT_DIR systemd systemdsystemunitdir)
    execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} systemd --variable=systemdsystemunitdir
      OUTPUT_VARIABLE SYSTEMD_UNIT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(IS_SYSTEMD_INSTALL 1)
    configure_file (
        ${ARANGODB_SOURCE_DIR}/Installation/systemd/arangodb3.service.in
        ${PROJECT_BINARY_DIR}${SYSTEMD_UNIT_DIR}/arangodb3.service
        NEWLINE_STYLE UNIX)
      if (${USE_ENTERPRISE})
        install(FILES ${PROJECT_BINARY_DIR}${SYSTEMD_UNIT_DIR}/arangodb3.service
          DESTINATION ${SYSTEMD_UNIT_DIR}/arangodb3e.service)
      else()
        install(FILES ${PROJECT_BINARY_DIR}${SYSTEMD_UNIT_DIR}/arangodb3.service
          DESTINATION ${SYSTEMD_UNIT_DIR}/arangodb3.service)
      endif()
  endif()
endif()
################################################################################
### @brief propagate the locations into our programms:
################################################################################

set(PATH_SEP "/")

to_native_path("PATH_SEP")
to_native_path("CMAKE_INSTALL_FULL_LOCALSTATEDIR")
to_native_path("CMAKE_INSTALL_FULL_SYSCONFDIR_ARANGO")
to_native_path("PKGDATADIR")
to_native_path("CMAKE_INSTALL_DATAROOTDIR_ARANGO")
to_native_path("ICU_DT_DEST")
to_native_path("CMAKE_INSTALL_SBINDIR")
to_native_path("CMAKE_INSTALL_BINDIR")
to_native_path("INSTALL_ICU_DT_DEST")

configure_file (
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/Basics/directories.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lib/Basics/directories.h"
  NEWLINE_STYLE UNIX
)

# sub directories --------------------------------------------------------------

#if(BUILD_STATIC_EXECUTABLES)
#  set(CMAKE_EXE_LINKER_FLAGS -static)
#  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
#  set(CMAKE_EXE_LINK_DYNAMIC_C_FLAGS)       # remove -Wl,-Bdynamic
#  set(CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS)
#  set(CMAKE_SHARED_LIBRARY_C_FLAGS)         # remove -fPIC
#  set(CMAKE_SHARED_LIBRARY_CXX_FLAGS)
#  set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # remove -rdynamic
#  set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)
#  # Maybe this works as well, haven't tried yet.
#  # set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)
#else(BUILD_STATIC_EXECUTABLES)
#  # Set RPATH to use for installed targets; append linker search path
#  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${LOFAR_LIBDIR}")
#  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
#endif(BUILD_STATIC_EXECUTABLES) 


#--------------------------------------------------------------------------------
#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
#--------------------------------------------------------------------------------
