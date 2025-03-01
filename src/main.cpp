#include <iostream>
#include <sys/inotify.h>
#include <pthread.h>
#include <queue>
#include <unistd.h> 

#define MAX_BUFFER_SIZE 1024
#define MAX_THREADS 8


std::queue<std::string> job;
pthread_mutex_t job_lock;
pthread_cond_t job_cond;


void* handle_thread (void* tid){
    std::string file;

    for (;;) {
        pthread_mutex_lock(&job_lock); 
        pthread_cond_wait(&job_cond, &job_lock);

        file = job.front();
        job.pop();
        
        pthread_mutex_unlock(&job_lock);
    
        printf("thread: %lu processing: %s\n", (pthread_t)tid , file.c_str());
   }

}


int main (int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./bread <directory> <public-key>\n");
        return EXIT_FAILURE;
    }
    
    const char* dir_path = argv[1];
    const char* file_path = argv[2];

    
    int fd = inotify_init();
    if (fd == -1)
        exit(1);

    int iaw = inotify_add_watch(fd, dir_path, IN_MOVED_TO);
    if (iaw == -1)
        exit(1);

    
    pthread_t tid[MAX_THREADS];
    pthread_mutex_init(&job_lock, NULL);
    pthread_cond_init(&job_cond, NULL);

    for (int i = 0 ; i < MAX_THREADS ; i++ ) {
        pthread_create(&tid[i], NULL, handle_thread, (void* ) &tid[i]);
    }


    char buf[MAX_BUFFER_SIZE];
    for (;;) {

        ssize_t read_count = read(fd, buf, sizeof(buf));
        if (read_count == -1)
            exit(1);

        size_t prev_len = 0;
        for (;;) {
            
            struct inotify_event* ev = (struct inotify_event* )&buf[prev_len];
            printf("added %s to jobs\n", ev->name);
            job.push(ev->name);
            
            pthread_cond_signal(&job_cond);

            prev_len += sizeof(struct inotify_event) + ev->len; 
            if (prev_len >= read_count)
                break;

        }
    
    }


    return EXIT_SUCCESS;
}
