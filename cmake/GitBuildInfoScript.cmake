# Run via `cmake -P` each build (see voltmod_stamp_build_info). All values derive
# from committed state so the header is only rewritten when a commit changes.
# Inputs: TEMPLATE_FILE, OUTPUT_FILE, VERSION_FILE, REPO_DIR.

find_program(GIT_EXECUTABLE git)

function(_voltmod_git out_var default)
    if(NOT GIT_EXECUTABLE)
        set("${out_var}" "${default}" PARENT_SCOPE)
        return()
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" ${ARGN}
        OUTPUT_VARIABLE output
        RESULT_VARIABLE result
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(result EQUAL 0)
        set("${out_var}" "${output}" PARENT_SCOPE)
    else()
        set("${out_var}" "${default}" PARENT_SCOPE)
    endif()
endfunction()

set(version_base "0.0.0")
if(EXISTS "${VERSION_FILE}")
    file(STRINGS "${VERSION_FILE}" version_lines LIMIT_COUNT 1)
    if(version_lines)
        string(STRIP "${version_lines}" version_base)
    endif()
endif()

_voltmod_git(repo_commit "" -C "${REPO_DIR}" rev-parse --short HEAD)
if(repo_commit STREQUAL "" AND DEFINED ENV{GITHUB_SHA})
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 repo_commit)
endif()
if(repo_commit STREQUAL "")
    set(repo_commit "unknown")
endif()

_voltmod_git(commit_date "unknown" -C "${REPO_DIR}" log -1 --format=%cI)

# A modified submodule shows up here as ` M vendor/...`, so this covers the kit too.
_voltmod_git(status_output "" -C "${REPO_DIR}" status --porcelain --untracked-files=no)

set(VOLTMOD_BI_VERSION "${version_base}")
if(NOT repo_commit STREQUAL "unknown")
    string(APPEND VOLTMOD_BI_VERSION "+${repo_commit}")
    if(NOT status_output STREQUAL "")
        string(APPEND VOLTMOD_BI_VERSION "-dirty")
    endif()
endif()
set(VOLTMOD_BI_REPO_COMMIT "${repo_commit}")
set(VOLTMOD_BI_DATE "${commit_date}")

configure_file("${TEMPLATE_FILE}" "${OUTPUT_FILE}" @ONLY)
