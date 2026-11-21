#include "CorePch.h"
#include "CoreTLS.h"

thread_local uint32_t tls_threadId = 0;
thread_local char tls_threadName[ThreadNameSize] = {};

thread_local JobQueue* tls_currentJobQueue = nullptr;
thread_local uint64_t tls_endTickMs = 0;
thread_local uint32_t tls_jobTimeBudgetMs = 0;

