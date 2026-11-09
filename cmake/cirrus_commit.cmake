# Writes OUT (a header defining CIRRUS_COMMIT) from the work tree's commit, "-dirty" when
# it has changes; leaves the file alone when nothing changed, so nothing rebuilds.
# Run at every build (ui/app/CMakeLists.txt) for the log's session header.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${SRC}"
    OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
if(failed OR NOT commit)
    set(commit "unknown")
else()
    execute_process(COMMAND git diff --quiet HEAD -- WORKING_DIRECTORY "${SRC}"
        RESULT_VARIABLE dirty ERROR_QUIET)
    if(dirty)
        string(APPEND commit "-dirty")
    endif()
endif()
set(content "#pragma once\n#define CIRRUS_COMMIT \"${commit}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
