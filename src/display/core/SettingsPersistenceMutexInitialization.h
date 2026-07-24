#pragma once

template <typename MutexHandle, typename CreateMutex>
MutexHandle initializePersistenceMutex(MutexHandle mutex, CreateMutex createMutex) {
    return mutex == nullptr ? createMutex() : mutex;
}
