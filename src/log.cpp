#include "log.h"
#include <time.h>
#include <pthread.h>
#include <string>

std::string logstr;
const std::string lvmsg[] = {"[INFO] ", "[ERROR] "};
pthread_mutex_t log_lock;

void log(LogLevel lv, const std::string logmsg) {

    time_t curr_time = time(NULL); 
    tm* ltime = localtime(&curr_time);

    std::string time = "[" + std::to_string(ltime->tm_hour) + ":" + std::to_string(ltime->tm_min) + ":" + std::to_string(ltime->tm_sec) + "] ";

    pthread_mutex_lock(&log_lock);
    logstr += time + lvmsg[lv] + logmsg;
    pthread_mutex_unlock(&log_lock);

}

bool log_write(const char* file) {
    if (logstr.empty())
        return 1;

    FILE* fd = fopen(file, "a");   
    if (fd == NULL)    
        return 0;

    pthread_mutex_lock(&log_lock);

    if (fwrite(logstr.c_str(), 1,logstr.length(), fd) != logstr.length())
        return 0;
    
    logstr.clear();
    pthread_mutex_unlock(&log_lock);

    fclose(fd);
    

    return 1;
}