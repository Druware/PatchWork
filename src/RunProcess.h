#ifndef RUN_PROCESS_H
#define RUN_PROCESS_H

#include <string>

/**
 * Runs argv[0] (searched in PATH) via fork/execvp, no shell, so arguments are
 * passed verbatim. stdin is /dev/null so an interactive prompt cannot hang us.
 * @param argv NULL-terminated argument vector.
 * @return the child's exit status, 127 if it could not be executed, or -1 if
 *         pipe/fork/wait failed.
 */
int RunProcess(const char* const argv[], std::string& out, std::string& err);

#endif
