#include <string>
#include <sys/inotify.h>
#include <pthread.h>
#include <queue>
#include <unistd.h> 
#include <signal.h>
#include <filesystem> 
#include <gpgme.h>
#include <fcntl.h>
#include "log.h"

#define MAX_BUFFER_SIZE 1024
#define MAX_THREADS 8
#define READ_AGAIN_TIMEOUT 5


bool running = true;
const char* recipient;
std::string LOG_FILE = "bread.log";
std::queue<std::string> job;
pthread_mutex_t job_lock;
pthread_cond_t job_cond;


void quit() {
    running = false;
    pthread_cond_broadcast(&job_cond);

}

void signal_handler(int signum) {
    log(LV_INFO, "Signal: " + std::to_string(signum) + " recieved, exiting\n");
    quit();
}


bool set_fd_nonblock(int fd) {

    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)    
        return 0;

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        return 0;

    return 1;
}

int encrypt(const char* file) {

    gpgme_error_t err = GPG_ERR_NO_ERROR;
    gpgme_ctx_t ctx;
    gpgme_key_t key;
    gpgme_key_t recipients[2];
    gpgme_data_t plain, cypher;
    const char* data;
    size_t size;
    FILE* fd; 


    err = gpgme_new(&ctx);
    if (err != GPG_ERR_NO_ERROR) {
        log(LV_ERROR, "Failed to create GPGME context: " + std::to_string(err) + "\n");
        goto cleanup;
    }

    err = gpgme_set_protocol(ctx, GPGME_PROTOCOL_OPENPGP);
    if (err != GPG_ERR_NO_ERROR) {
        log(LV_ERROR, "Failed to set GPGME protocol: " + std::to_string(err) + "\n");
        goto cleanup;
    }


    err = gpgme_data_new(&plain);
    if (err != GPG_ERR_NO_ERROR) {
        log(LV_ERROR, "Failed to create data from file: " + std::to_string(err) + "\n");
        goto cleanup;
    }
    err = gpgme_data_set_file_name(plain, file);  
    if (err == GPG_ERR_ENOMEM) {
        log(LV_ERROR, "Failed to set file name: not enough memory is available.\n");
        goto cleanup;
    }

    err = gpgme_data_new(&cypher);
    if (err != GPG_ERR_NO_ERROR) {
        log(LV_ERROR, "Failed to create cipher data: " + std::to_string(err) + "\n");
        goto cleanup;
    }


    err = gpgme_get_key(ctx, recipient, &key, 0);
    if (key == NULL) {
        log(LV_ERROR, "Failed to get key: " + std::to_string(err) + "\n");
        goto cleanup;
    }

    recipients[0] = key;
    recipients[1] = NULL;
   
    err = gpgme_op_encrypt(ctx, recipients, GPGME_ENCRYPT_FILE, plain, cypher);
    if (err != GPG_ERR_NO_ERROR) {
        log(LV_ERROR, "Encryption failed: " + std::to_string(err) + "\n");
        goto cleanup;
    }
   

    data = gpgme_data_release_and_get_mem(cypher, &size);

    if (data != NULL) { 
       fd = fopen(file, "wb");
        if (fd == NULL) {
            log(LV_ERROR, "Failed to open output file for writing\n");
            goto cleanup;
        }

        if(fwrite(data, 1, size, fd) != size ) {
            log(LV_ERROR, "Failed to write encrypted data to file\n");
            err = 1; 
        }

        gpgme_free((void*) data);
        fclose(fd);
    }
    else {
        log(LV_ERROR, "Failed to retrive encrypted data\n");
        err = 1;
    }
    

    cleanup:
        if(ctx) gpgme_release(ctx);
        if(key) gpgme_key_release(key);
        if(plain) gpgme_data_release(plain);
    

    return (err == GPG_ERR_NO_ERROR ? 1 : 0);
}

void* handle_thread (void* tid){
    std::string path;

    for (;;) {
        pthread_mutex_lock(&job_lock); 
        pthread_cond_wait(&job_cond, &job_lock);log(LV_INFO, "wait over \n");

        //if we get here and there are no jobs or !running, the thread was woken up by signal handler and we need to exit 
        if (job.empty() || !running) {
            pthread_mutex_unlock(&job_lock);
            break;
        }

        path = job.front();
        job.pop();
        
        pthread_mutex_unlock(&job_lock);

        
        //if the job is a dir, push the contents of dir in job queue 
        if (std::filesystem::is_directory(path)) {
            pthread_mutex_lock(&job_lock); 

            for (const auto& dir_entry : std::filesystem::directory_iterator(path)){
                job.push(dir_entry.path().string());
                pthread_cond_signal(&job_cond);

            }
            pthread_mutex_unlock(&job_lock); 

            continue;
        }
        
        if(encrypt(path.c_str()))
            log(LV_INFO, "Encrypted: " + path + "\n");
        else {
            log(LV_ERROR, "Failed to encrypt: " + path + "\n");
            quit();
        }
    }
    return 0;
}


int main (int argc, char* argv[]) {
    int err = 0;

    if (argc < 3) {
        fprintf(stderr, "Usage: ./bread <directory> <recipient>\n");
        return EXIT_FAILURE;
    }

    const char* version = gpgme_check_version("1.24.0");
    if (version == NULL) {
        fprintf(stderr, "GPGME version check failed\nExpected version: 1.24.0\n");
        return EXIT_FAILURE;
    }

    std::string dir_path = argv[1];
    if (dir_path.back() != '/')
        dir_path.append("/");

    recipient = argv[2];
    LOG_FILE = dir_path + LOG_FILE;


    int fd = inotify_init();
    if (fd == -1) {
        perror("Failed to initialize inotify instance");
        return EXIT_FAILURE;
    }

    int iaw = inotify_add_watch(fd, dir_path.c_str(), IN_MOVED_TO);
    if (iaw == -1) {
        perror("Failed to add watch entry to inotify instance");
        return EXIT_FAILURE;
    }
    
    if (!set_fd_nonblock(fd)){
        perror("Failed to mark inotify fd as nonblocking");
        return EXIT_FAILURE;
    }
    
    pthread_t tid[MAX_THREADS];
    err = pthread_mutex_init(&job_lock, NULL);
    if (err) {
        perror("Failed to initialize mutex");
        return EXIT_FAILURE;
    }
    err = pthread_cond_init(&job_cond, NULL);
    if (err) {
        perror("Failed to initialize conditional variable");
        return EXIT_FAILURE;
    }
    err = pthread_mutex_init(&log_lock, NULL);
    if (err) {
        perror("Failed to initialize mutex");
        return EXIT_FAILURE;
    }


    if (daemon(1, 1) == -1) {
        perror("Unable to daemonize the process");
        return EXIT_FAILURE;
    }


    for (int i = 0 ; i < MAX_THREADS ; i++ ) {
        err = pthread_create(&tid[i], NULL, handle_thread, (void* ) &tid[i]);
        if (err) {
            log(LV_ERROR, "Failed to spawn threads");
            return EXIT_FAILURE;
        }

    }


    if (signal(SIGINT, signal_handler) == SIG_ERR){
        log(LV_ERROR, "Failed to set signal handler\n");
        quit();
    }
    if (signal(SIGTERM, signal_handler) == SIG_ERR){
        log(LV_ERROR, "Failed to set signal handler\n");
        quit();
    }


    char buf[MAX_BUFFER_SIZE];
    while (running) {
        if (!log_write(LOG_FILE.c_str()))
            quit();
        
        ssize_t read_count = read(fd, buf, sizeof(buf));
        if (read_count == -1) {
            if (errno == EAGAIN) {  
                sleep(READ_AGAIN_TIMEOUT);
                continue;
            }
            else
            quit();
        }
        
        size_t prev_len = 0;
        for (;;) {
            
            struct inotify_event* ev = (struct inotify_event* )&buf[prev_len];

            std::string path;
            path.append(dir_path);
            path.append(ev->name);

            pthread_mutex_lock(&job_lock);

            job.push(path);
        
            pthread_cond_signal(&job_cond);
            pthread_mutex_unlock(&job_lock);
            
            prev_len += sizeof(struct inotify_event) + ev->len; 
            if (prev_len >= read_count)
                break;

        }
    
    }

    cleanup: 
        if (inotify_rm_watch(fd, iaw) == -1)
            log(LV_ERROR, "Failed to remove an existing watch from an inotify instance");

        for (int i = 0 ; i < MAX_THREADS ; i++) {
            if (pthread_join(tid[i], NULL) != 0)
                log(LV_ERROR, "Failed to join thread");
        }

        if (!log_write(LOG_FILE.c_str()))
            quit();

    return EXIT_SUCCESS;
}