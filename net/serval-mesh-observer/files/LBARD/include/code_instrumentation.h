#ifndef __INSTRUMENTATION_H__
#define __INSTRUMENTATION_H__

//
// 'code_instrumentation.h/.c' provide a number of light-weight debugging/logging 
// constructs that can be sprinkled over the source code. 
// 
// Most are implemented as macros, and can be made to completely disappear 
// from compiled production code by setting some precompiled constants
//

#define LOG_LEVEL_OFF   0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_NOTE  3
#define LOG_LEVEL_TRACE 4

// Any logging statements with a log level > COMPILE_LOG_LEVEL won't be compiled

#define COMPILE_LOG_LEVEL LOG_LEVEL_NOTE // Change this as desired
#define LOG_USAGE_COUNTS  1              // 1 or 0
#define LOG_ENTRY_EXIT    1              // 1 or 0

#define TEST_LEVEL_ALWAYS 0 // Always tested, even in production code
#define TEST_LEVEL_LIGHT  1 // Light-weight tests. Leave this on unless you're cramped in time or memory
#define TEST_LEVEL_HEAVY  2 // Heavy tests. Disable these in production releases

#define COMPILE_TEST_LEVEL TEST_LEVEL_LIGHT // Change this as desired

#define LOG_MESSAGE(...) \
  code_instrumentation_log(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_OFF, ##__VA_ARGS__)

#if COMPILE_LOG_LEVEL >= LOG_LEVEL_ERROR

#define LOG_ERROR(...) \
  code_instrumentation_log(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_ERROR, ##__VA_ARGS__)

#else

#define LOG_ERROR(...)

#endif

#if COMPILE_LOG_LEVEL >= LOG_LEVEL_WARN

#define LOG_WARN(...) \
  code_instrumentation_log(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_WARN, ##__VA_ARGS__)

#else

#define LOG_WARN(...)

#endif

#if COMPILE_LOG_LEVEL >= LOG_LEVEL_NOTE

#define LOG_NOTE(...) \
  code_instrumentation_log(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_NOTE, ##__VA_ARGS__)

#else

#define LOG_NOTE(...)

#endif

#if COMPILE_LOG_LEVEL >= LOG_LEVEL_TRACE

#define LOG_TRACE(...) \
  code_instrumentation_log(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_TRACE, ##__VA_ARGS__)

#else

#define LOG_TRACE(...)
  
#endif

// The entry/exit macros all come in pairs and will cause compiler errors if not balanced, 
// and that's on purpose

#if ! LOG_ENTRY_EXIT

#define LOG_ENTRY_PREFIX {
#define LOG_EXIT_SUFFIX }

#define LOG_ENTRY_PREFIX_RECURSIVE_FUNCTION {{
#define LOG_EXIT_SUFFIX_RECURSIVE_FUNCTION }}

#else // if LOG_ENTRY_EXIT

#define LOG_ENTRY_PREFIX \
  { \
    static int __entranceSentinel##__FUNCTION__ = 0; \
    if (__entranceSentinel##__FUNCTION__ > 0) \
      LOG_WARN("Recursive call level %d", __entranceSentinel##__FUNCTION__ + 1); \
    ++__entranceSentinel##__FUNCTION__; \
    LOG_TRACE("LOG_ENTRY")

#define LOG_EXIT_SUFFIX  \
    LOG_TRACE("LOG_EXIT"); \
    --__entranceSentinel##__FUNCTION__;\
  }

#define LOG_ENTRY_PREFIX_RECURSIVE_FUNCTION \
  {{ \
    LOG_TRACE("LOG_ENTRY")

#define LOG_EXIT_SUFFIX_RECURSIVE_FUNCTION  \
    LOG_TRACE("LOG_EXIT"); \
  }}

#endif // LOG_ENTRY_EXIT

#if ! LOG_USAGE_COUNTS

#define LOG_ENTRY \
  LOG_ENTRY_PREFIX

#define LOG_EXIT \
  LOG_EXIT_SUFFIX

#define LOG_ENTRY_RECURSIVE_FUNCTION \
  LOG_ENTRY_PREFIX_RECURSIVE_FUNCTION

#define LOG_EXIT_RECURSIVE_FUNCTION \
  LOG_EXIT_SUFFIX_RECURSIVE_FUNCTION

#else // if LOG_USAGE_COUNTS

#define LOG_ENTRY \
  LOG_ENTRY_PREFIX; \
  code_instrumentation_entry(__FUNCTION__)

#define LOG_EXIT \
  code_instrumentation_exit(__FUNCTION__); \
  LOG_EXIT_SUFFIX

#define LOG_ENTRY_RECURSIVE_FUNCTION \
  LOG_ENTRY_PREFIX_RECURSIVE_FUNCTION; \
  code_instrumentation_entry(__FUNCTION__)

#define LOG_EXIT_RECURSIVE_FUNCTION \
  code_instrumentation_exit(__FUNCTION__); \
  LOG_EXIT_SUFFIX_RECURSIVE_FUNCTION;

#endif

void code_instrumentation_log(const char* fileName, int line, const char* functionName, int logLevel, const char *msg, ...);
void code_instrumentation_entry(const char* functionName);
void code_instrumentation_exit(const char* functionName);

#endif