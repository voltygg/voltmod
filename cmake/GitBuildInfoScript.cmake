# `cmake -P` on each build; the header only changes when the repository does.
# Inputs: TEMPLATE_FILE, OUTPUT_FILE, VERSION, REPO_DIR.

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

_voltmod_git(repo_commit "unknown" -C "${REPO_DIR}" rev-parse --short HEAD)
_voltmod_git(commit_date "unknown" -C "${REPO_DIR}" log -1 --format=%cI)

# An editable checkout under vendor/ is a build input, `ignore = all` or not.
_voltmod_git(status_output "" -C "${REPO_DIR}" status --porcelain --untracked-files=no
    --ignore-submodules=none)

set(VOLTMOD_BI_VERSION "${VERSION}")
if(NOT repo_commit STREQUAL "unknown")
    string(APPEND VOLTMOD_BI_VERSION "+${repo_commit}")
    if(NOT status_output STREQUAL "")
        string(APPEND VOLTMOD_BI_VERSION "-dirty")
    endif()
endif()
set(VOLTMOD_BI_REPO_COMMIT "${repo_commit}")
set(VOLTMOD_BI_DATE "${commit_date}")

configure_file("${TEMPLATE_FILE}" "${OUTPUT_FILE}" @ONLY)
