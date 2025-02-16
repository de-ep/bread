#include <iostream>
#include <sys/inotify.h>
#include <queue>
#include <unistd.h> 
#include <fcntl.h>

#define MAX_BUFFER_SIZE 1024


std::queue<std::string> job;

int main (int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: ./bread <directory> <public-key>\n");
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


    char buf[MAX_BUFFER_SIZE];
    for (;;) {

        ssize_t read_count = read(fd, buf, sizeof(buf));
        if (read_count == -1)
            exit(1);

        size_t prev_len = 0;
        for (;;) {
 
            struct inotify_event* ev = (struct inotify_event* )&buf[prev_len];

            job.push(ev->name);

            prev_len += sizeof(struct inotify_event) + ev->len; 
            if (prev_len >= read_count)
                break;

        }
    
    }


    return EXIT_SUCCESS;
}
