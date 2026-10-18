# Викликається перед збіркою gamex, щоб номер і час білда були актуальні.
if(NOT DEFINED GAMEX_VERSION_STRING OR NOT DEFINED GAMEX_TEMPLATE OR NOT DEFINED GAMEX_OUTPUT)
  message(FATAL_ERROR "write_build_info.cmake: потрібні GAMEX_VERSION_STRING, GAMEX_TEMPLATE, GAMEX_OUTPUT")
endif()

string(TIMESTAMP GAMEX_BUILD_TIME "%Y-%m-%dT%H:%M:%S" UTC)
string(TIMESTAMP GAMEX_BUILD_TIME_READABLE "%d.%m.%Y %H:%M:%S UTC" UTC)
string(TIMESTAMP _stamp "%Y%m%d%H%M" UTC)
set(GAMEX_BUILD_NUMBER "${_stamp}")

if(DEFINED GAMEX_SOURCE_DIR AND EXISTS "${GAMEX_SOURCE_DIR}/.git")
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
      WORKING_DIRECTORY "${GAMEX_SOURCE_DIR}"
      OUTPUT_VARIABLE _count
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_count MATCHES "^[0-9]+$")
      set(GAMEX_BUILD_NUMBER "${_count}")
    endif()
  endif()
endif()

get_filename_component(_outdir "${GAMEX_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_outdir}")
configure_file("${GAMEX_TEMPLATE}" "${GAMEX_OUTPUT}" @ONLY)
