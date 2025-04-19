#ifndef LOG

#include <iostream>
#include <pthread.h>

typedef enum {
    LV_INFO = 0,
    LV_ERROR,
}LogLevel;

extern pthread_mutex_t log_lock;

void log(LogLevel, const std::string);
bool log_write(const char*);

#endif 