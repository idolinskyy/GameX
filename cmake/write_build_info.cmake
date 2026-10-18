# Викликається перед кожною збіркою gamex: ++номер збірки, версія = major.minor.N
if(NOT DEFINED GAMEX_VERSION_MAJOR OR NOT DEFINED GAMEX_VERSION_MINOR)
  message(FATAL_ERROR "write_build_info.cmake: потрібні GAMEX_VERSION_MAJOR/MINOR")
endif()
if(NOT DEFINED GAMEX_TEMPLATE OR NOT DEFINED GAMEX_OUTPUT OR NOT DEFINED GAMEX_COUNTER_FILE)
  message(FATAL_ERROR
    "write_build_info.cmake: потрібні GAMEX_TEMPLATE, GAMEX_OUTPUT, GAMEX_COUNTER_FILE")
endif()

set(_count 0)
if(EXISTS "${GAMEX_COUNTER_FILE}")
  file(READ "${GAMEX_COUNTER_FILE}" _raw)
  string(STRIP "${_raw}" _raw)
  if(_raw MATCHES "^[0-9]+$")
    set(_count "${_raw}")
  endif()
elseif(DEFINED GAMEX_SOURCE_DIR AND EXISTS "${GAMEX_SOURCE_DIR}/.git")
  # Перший запуск: стартуємо від кількості комітів, щоб не стрибнути назад.
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
      WORKING_DIRECTORY "${GAMEX_SOURCE_DIR}"
      OUTPUT_VARIABLE _git_count
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_git_count MATCHES "^[0-9]+$")
      set(_count "${_git_count}")
    endif()
  endif()
endif()

math(EXPR _count "${_count} + 1")

get_filename_component(_counter_dir "${GAMEX_COUNTER_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_counter_dir}")
file(WRITE "${GAMEX_COUNTER_FILE}" "${_count}\n")

set(GAMEX_BUILD_NUMBER "${_count}")
set(GAMEX_VERSION_STRING
    "${GAMEX_VERSION_MAJOR}.${GAMEX_VERSION_MINOR}.${GAMEX_BUILD_NUMBER}")

string(TIMESTAMP GAMEX_BUILD_TIME "%Y-%m-%dT%H:%M:%S" UTC)
string(TIMESTAMP GAMEX_BUILD_TIME_READABLE "%d.%m.%Y %H:%M:%S UTC" UTC)

get_filename_component(_outdir "${GAMEX_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_outdir}")
configure_file("${GAMEX_TEMPLATE}" "${GAMEX_OUTPUT}" @ONLY)

message(STATUS "GameX version ${GAMEX_VERSION_STRING} (build ${_count})")
