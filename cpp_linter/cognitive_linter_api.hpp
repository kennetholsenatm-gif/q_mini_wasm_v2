#pragma once

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
    #ifdef COGNITIVE_LINTER_EXPORTS
        #define COGNITIVE_LINTER_API __declspec(dllexport)
    #else
        #define COGNITIVE_LINTER_API __declspec(dllimport)
    #endif
#else
    #define COGNITIVE_LINTER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum CognitiveLinterError {
    COG_LINTER_OK = 0,
    COG_LINTER_ERROR_INVALID_PATH = -1,
    COG_LINTER_ERROR_READ_FAILED = -2
};

enum ViolationType {
    VIOLATION_LINE_LENGTH = 0,
    VIOLATION_PARAGRAPH_CHUNKING = 1,
    VIOLATION_HEADER_FREQUENCY = 2,
    VIOLATION_CODE_BLOCK_LENGTH = 3,
    VIOLATION_NAV_DEPTH = 4
};

typedef struct {
    ViolationType type;
    int line_number;
    const char* rule;
    const char* message;
} Violation;

typedef struct {
    int max_line_length;
    int max_paragraph_lines;
    int max_code_block_lines;
    int header_interval_words;
    int max_nav_depth;
} LinterConfig;

COGNITIVE_LINTER_API LinterConfig cognitive_linter_default_config();
COGNITIVE_LINTER_API int cognitive_linter_lint_file(const char* filepath, LinterConfig config, Violation** violations, size_t* violation_count);
COGNITIVE_LINTER_API void cognitive_linter_free_violations(Violation* violations, size_t count);

#ifdef __cplusplus
}
#endif
