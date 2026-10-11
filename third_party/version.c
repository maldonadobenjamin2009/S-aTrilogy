/* version.c escrito a mano (en mGBA lo genera CMake desde src/core/version.c.in). */
#include <mgba/core/version.h>

MGBA_EXPORT const char* const gitCommit = "(embedded)";
MGBA_EXPORT const char* const gitCommitShort = "(embedded)";
MGBA_EXPORT const char* const gitBranch = "(embedded)";
MGBA_EXPORT const int gitRevision = -1;
MGBA_EXPORT const char* const binaryName = "mgba";
MGBA_EXPORT const char* const projectName = "mGBA";
MGBA_EXPORT const char* const projectVersion = "0.11.0-embedded";
