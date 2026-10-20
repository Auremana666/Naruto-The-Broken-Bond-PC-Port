# Verify supplied dependency sources without fetching or modifying any checkout.
include_guard(GLOBAL)
find_package(Git REQUIRED)
file(READ "${CMAKE_CURRENT_LIST_DIR}/dependencies.json" NATIVE_RENDERER_DEPENDENCIES)

function(native_renderer_require_source name directory revision)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${directory}" rev-parse HEAD
        OUTPUT_VARIABLE source_head OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${directory}" status --porcelain --untracked-files=all
        OUTPUT_VARIABLE source_changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT source_head STREQUAL revision OR NOT source_changes STREQUAL "")
        message(FATAL_ERROR "${name} source must be clean at ${revision}: ${directory}")
    endif()
    message(STATUS "Native backend dependency ${name}: ${source_head} (${directory})")
endfunction()
