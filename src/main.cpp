#include <cstdio>
#include <iostream>
#include <string>
#include <sys/inotify.h>
#include <pthread.h>
#include <queue>
#include <unistd.h> 
#include <filesystem> 
#include <gpgme.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_THREADS 8


const char* recipient;
std::string LOG_FILE = "bread.log";
std::queue<std::string> job;
pthread_mutex_t job_lock;
pthread_cond_t job_cond;


int encrypt(const char* file) {

    gpgme_error_t err = GPG_ERR_NO_ERROR;
    gpgme_ctx_t ctx;
    gpgme_key_t key;
    gpgme_key_t recipients[2];
    gpgme_data_t plain, cypher;
    const char* data;
    size_t size;
    FILE* fd; 


    const char* version = gpgme_check_version("1.24.0");
    if (version == NULL) {
        fprintf(stderr, "GPGME version check failed\n");
        goto cleanup;
    }

    err = gpgme_new(&ctx);
    if (err != GPG_ERR_NO_ERROR) {
        fprintf(stderr, "Failed to create GPGME context: %u\n", err);
        goto cleanup;
    }

    err = gpgme_set_protocol(ctx, GPGME_PROTOCOL_OPENPGP);
    if (err != GPG_ERR_NO_ERROR) {
        fprintf(stderr, "Failed to set GPGME protocol: %u\n", err);
        goto cleanup;
    }


    err = gpgme_data_new(&plain);
    if (err != GPG_ERR_NO_ERROR) {
        fprintf(stderr, "Failed to create data from file: %u\n", err);
        goto cleanup;
    }
    err = gpgme_data_set_file_name(plain, file);  
    if (err == GPG_ERR_ENOMEM) {
        fprintf(stderr, "Failed to set file name: not enough memory is available.\n");
        goto cleanup;
    }

    err = gpgme_data_new(&cypher);
    if (err != GPG_ERR_NO_ERROR) {
        fprintf(stderr, "Failed to create cipher data: %u\n", err);
        goto cleanup;
    }


    err = gpgme_get_key(ctx, recipient, &key, 0);
    if (key == NULL) {
        fprintf(stderr, "Failed to get key: %u\n", err);
        goto cleanup;
    }

    recipients[0] = key;
    recipients[1] = NULL;
   
    err = gpgme_op_encrypt(ctx, recipients, GPGME_ENCRYPT_FILE, plain, cypher);
    if (err != GPG_ERR_NO_ERROR) {
        fprintf(stderr, "Encryption failed: %u\n", err);
        goto cleanup;
    }
   

    data = gpgme_data_release_and_get_mem(cypher, &size);

    if (data != NULL) { 
       fd = fopen(file, "wb");
        if (fd == NULL) {
            fprintf(stderr, "Failed to open output file for writing\n");
            goto cleanup;
        }

        if(fwrite(data, 1, size, fd) != size ) {
            fprintf(stderr, "Failed to write encrypted data to file\n");
            err = 1; 
        }

        gpgme_free((void*) data);
        fclose(fd);
    }
    else {
        fprintf(stderr, "Failed to retrive encrypted data\n");
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
        pthread_cond_wait(&job_cond, &job_lock);

        path = job.front();
        job.pop();
        
        pthread_mutex_unlock(&job_lock);

        
        if (std::filesystem::is_directory(path)) {
            pthread_mutex_lock(&job_lock); 

            for (const auto& dir_entry : std::filesystem::directory_iterator(path)){
                job.push(dir_entry.path().string());
                pthread_cond_signal(&job_cond);

            }
            pthread_mutex_unlock(&job_lock); 

            continue;
        }
        printf("thread: %lu processing: %s\n", (pthread_t)tid, path.c_str());
        

        if(encrypt(path.c_str()))
            printf("Encrypted: %s\n", path.c_str());
        else 
            printf("Failed to encrypt: %s\n", path.c_str());

    }

}


int main (int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./bread <directory> <recipient>\n");
        return EXIT_FAILURE;
    }
    
    std::string dir_path = argv[1];
    if (dir_path.back() != '/')
        dir_path.append("/");

    recipient = argv[2];
    
    LOG_FILE = dir_path + LOG_FILE;
    FILE* file = fopen(LOG_FILE.c_str(), "w");
    if (!file)
        exit(1);
    fclose(file);

    int fd = inotify_init();
    if (fd == -1)
        exit(1);

    int iaw = inotify_add_watch(fd, dir_path.c_str(), IN_MOVED_TO);
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

            std::string path;
            path.append(dir_path);
            path.append(ev->name);

            pthread_mutex_lock(&job_lock);

            job.push(path);
        
            pthread_cond_signal(&job_cond);
            pthread_mutex_unlock(&job_lock);
            std::cout << "added " << path << " to jobs" << std::endl;
            prev_len += sizeof(struct inotify_event) + ev->len; 
            if (prev_len >= read_count)
                break;

        }
    
    }


    return EXIT_SUCCESS;
}