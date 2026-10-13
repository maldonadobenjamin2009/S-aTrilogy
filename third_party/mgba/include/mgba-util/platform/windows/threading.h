/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef WINDOWS_THREADING_H
#define WINDOWS_THREADING_H

#include <mgba-util/common.h>

#define THREAD_ENTRY DWORD WINAPI
typedef THREAD_ENTRY ThreadEntry(LPVOID);
#define THREAD_EXIT(RES) return RES

typedef HANDLE Thread;
typedef CRITICAL_SECTION Mutex;
typedef CONDITION_VARIABLE Condition;

static inline int MutexInit(Mutex* mutex) {
	InitializeCriticalSection(mutex);
	return 0;
}

static inline int MutexDeinit(Mutex* mutex) {
	DeleteCriticalSection(mutex);
	return 0;
}

static inline int MutexLock(Mutex* mutex) {
	EnterCriticalSection(mutex);
	return 0;
}

static inline int MutexTryLock(Mutex* mutex) {
	if (TryEnterCriticalSection(mutex)) {
		return 0;
	}
	return 1;
}

static inline int MutexUnlock(Mutex* mutex) {
	LeaveCriticalSection(mutex);
	return 0;
}

static inline int ConditionInit(Condition* cond) {
	InitializeConditionVariable(cond);
	return 0;
}

static inline int ConditionDeinit(Condition* cond) {
	// This is a no-op on Windows
	UNUSED(cond);
	return 0;
}

static inline int ConditionWait(Condition* cond, Mutex* mutex) {
	if (SleepConditionVariableCS(cond, mutex, INFINITE)) {
		return 0;
	}
	return (int) GetLastError();
}

static inline int ConditionWaitTimed(Condition* cond, Mutex* mutex, int32_t timeoutMs) {
	if (SleepConditionVariableCS(cond, mutex, (DWORD) timeoutMs)) {
		return 0;
	}
	return (int) GetLastError();
}

static inline int ConditionWake(Condition* cond) {
	WakeAllConditionVariable(cond);
	return 0;
}

static inline int ThreadCreate(Thread* thread, ThreadEntry entry, void* context) {
	HANDLE created = CreateThread(NULL, 0, entry, context, 0, 0);
	if (!created) {
		return (int) GetLastError();
	}
	*thread = created;
	return 0;
}

static inline int ThreadJoin(Thread* thread) {
	DWORD error = WaitForSingleObject(*thread, INFINITE);
	if (error == WAIT_FAILED) {
		return GetLastError();
	}
	return 0;
}

static inline int ThreadSetName(const char* name) {
	UNUSED(name);
	return -1;
}

#if (__STDC_VERSION__ < 201112L) || (__STDC_NO_THREADS__ == 1)
typedef DWORD ThreadLocal;

static inline void ThreadLocalInitKey(ThreadLocal* key) {
	*key = TlsAlloc();
}

static inline void ThreadLocalSetKey(ThreadLocal key, void* value) {
	TlsSetValue(key, value);
}

static inline void* ThreadLocalGetValue(ThreadLocal key) {
	return TlsGetValue(key);
}
#endif

#endif
