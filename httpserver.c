#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdbool.h>
#include "queue.h"
#include <sys/file.h>

//defines
#define BUFSIZE 4096

//structures
struct Request {
    char method[500];
    char URI[500];
    char version[500];
    int error;
    int content_length;
    int startingIndex;
    int requestID;
    int connfd;
};

//fuction declarations:
void tokenizeRequest(int connfd);
void putRequest(char *message_body, struct Request req, int connfd);
void getRequest(int connfd, struct Request req);
void handleResponses(char *method, int status_code);
void appendRequest(char *message_body, struct Request req, int connfd);
void goToRequest(char *message_body, int connfd, struct Request req);
struct Request parseTokenizeBuffer(char *tokenizebuffer, struct Request req2, int byteswritten);
void generateLog(int status_code, struct Request req);
void *thread_function(void *arg);

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condvar = PTHREAD_COND_INITIALIZER;

//lock for writing to logfile
pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;

#define OPTIONS              "t:l:"
#define BUF_SIZE             4096
#define DEFAULT_THREAD_COUNT 4

//thread pool global
pthread_t *thread_pool;
int numthreads;
volatile sig_atomic_t exitRequested = 0;

static FILE *logfile;
#define LOG(...) fprintf(logfile, __VA_ARGS__);

// Converts a string to an 16 bits unsigned integer.
// Returns 0 if the string is malformed or out of the range.
static size_t strtouint16(char number[]) {
    char *last;
    long num = strtol(number, &last, 10);
    if (num <= 0 || num > UINT16_MAX || *last != '\0') {
        return 0;
    }
    return num;
}

// Creates a socket for listening for connections.
// Closes the program and prints an error message on error.
static int create_listen_socket(uint16_t port) {
    struct sockaddr_in addr;
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        err(EXIT_FAILURE, "socket error");
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htons(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *) &addr, sizeof addr) < 0) {
        err(EXIT_FAILURE, "bind error");
    }
    if (listen(listenfd, 128) < 0) {
        err(EXIT_FAILURE, "listen error");
    }
    return listenfd;
}

void *handle_connection(void *pconnfd) {
    int connfd = *((int *) pconnfd);
    free(pconnfd);

    // printf("************NEW REQUEST************\n");
    // printf("connfd: %d\n", connfd);
    tokenizeRequest(connfd);
    // printf("************END REQUEST************\n");

    return NULL;
}

// static void sigterm_handler(int sig) {
//     if (sig == SIGTERM) {
//         warnx("received SIGTERM");
//         fclose(logfile);
//         free(thread_pool);
//         exit(EXIT_SUCCESS);
//     }
// }

// static void sigint_handler(int sig) {
//     if (sig == SIGINT) {
//         warnx("received SIGINT");
//         pthread_cond_broadcast(&condvar);
//         // join threads
//         int counter1;
//         for (counter1=0; counter1<numthreads; counter1++){
//             if(pthread_cancel(thread_pool[counter1]) != 0){
//                 err(1, "pthread_cancel failed");
//             }
//         }
//         int counter;
//         for (counter=0; counter<numthreads; counter++){
//             if(pthread_join(thread_pool[counter], NULL) != 0){
//                 err(1, "pthread_join failed");
//             }
//         }
//         free(thread_pool);
//         fclose(logfile);
//         exit(EXIT_SUCCESS);
//     }
// }

void handler(int num) {
    (void) num; //stop compiler from complaining
    exitRequested = 1; //trigger global to stop threads
}

static void usage(char *exec) {
    fprintf(stderr, "usage: %s [-t threads] [-l logfile] <port>\n", exec);
}

int main(int argc, char *argv[]) {
    int opt = 0;
    bool enq;
    int threads = DEFAULT_THREAD_COUNT;
    logfile = stderr;

    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
        switch (opt) {
        case 't':
            threads = strtol(optarg, NULL, 10);
            if (threads <= 0) {
                errx(EXIT_FAILURE, "bad number of threads");
            }
            break;
        case 'l':
            logfile = fopen(optarg, "w");
            if (!logfile) {
                errx(EXIT_FAILURE, "bad logfile");
            }
            break;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        warnx("wrong number of arguments");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    uint16_t port = strtouint16(argv[optind]);
    if (port == 0) {
        errx(EXIT_FAILURE, "bad port number: %s", argv[1]);
    }

    signal(SIGPIPE, SIG_IGN);
    // signal(SIGTERM, sigterm_handler);
    // signal(SIGINT, sigint_handler);

    int listenfd = create_listen_socket(port);
    // LOG("port=%" PRIu16 ", threads=%d\n", port, threads);

    printf("num threads to create: %d\n", threads);

    thread_pool = malloc(threads * sizeof(pthread_t));
    numthreads = threads;

    /*create threads*/
    int counter;
    for (counter = 0; counter < threads; counter++) {
        if (pthread_create(&thread_pool[counter], NULL, thread_function, NULL) != 0) {
            errx(EXIT_FAILURE, "pthread create failed");
        }
    }

    struct sigaction sa = { 0 };
    sa.sa_handler = handler;

    sigaction(SIGINT, &sa, NULL); //send sigint to handler
    sigaction(SIGTERM, &sa, NULL); //send sigterm to handler

    printf("Gets to inf loop\n");
    while (!exitRequested) { //infinite loop
        int connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) {
            warn("accept error");
            continue;
        }
        //***********Handle Connection*************
        int *pconnfd = malloc(sizeof(int));
        *pconnfd = connfd;
        pthread_mutex_lock(&lock); //lock critical section
        enq = enqueue(pconnfd);
        if (enq != false) { //enqueue failed, don't signal
            pthread_cond_signal(
                &condvar); //signal to thread_func that there is a request to process
        }
        pthread_mutex_unlock(&lock); //unlock critical section
        //*****************************************
        // close(connfd);
        // handle_connection(connfd);
    }
    //**************Cleanup*******************

    //close logfile
    fclose(logfile);

    //broadcast to all threads to stop waiting on condvar
    if (exitRequested == 1) {
        pthread_cond_broadcast(&condvar);
    }
    //join finished threads
    int counter1;
    for (counter1 = 0; counter1 < threads; counter1++) {
        if (pthread_join(thread_pool[counter1], NULL) != 0) {
            printf("Join error\n");
        }
    }

    //free thread pool memory
    free(thread_pool);

    //*******************************************

    return EXIT_SUCCESS;
}

//////////////////////////// My Functions /////////////////////////////////////

void *thread_function(void *arg) {
    (void) arg;
    printf("in thread func\n");
    while (1) {
        int *pconnfd;
        pthread_mutex_lock(&lock); //lock critical section to avoid enq and deq at same time
        if ((pconnfd = dequeue())
            == NULL) { //only wait if theres nothing in the queue, otherwise dequeue as needed
            pthread_cond_wait(&condvar, &lock); //wait on an enqueue from dispatcher thread
        }
        pthread_mutex_unlock(&lock); //unlock critical section
        if (exitRequested == 1) { //if sigint or sigterm, unlock everything and return
            printf("exiting threadfunc\n");
            return NULL;
        }
        if (pconnfd != NULL) {
            handle_connection(pconnfd);
        }
    }

    return NULL;
}

void tokenizeRequest(int connfd) {

    char buf[BUFSIZE + 1] = { 0 };
    buf[BUFSIZE] = '\0';
    char tokenizebuffer[BUFSIZE + 1] = { 0 };
    tokenizebuffer[BUFSIZE] = '\0';

    int bytesread = 0;
    int iterations = 0;
    int len;
    int step = 1;
    int count = 0;
    int byteswritten = 0;
    int testcase = 0;
    int byteswrittentorequest = 0;
    int byteswrittentorequest2 = 0;
    int skip = 0;
    int wereadflag = 0;

    //declare struct for request line
    struct Request req;
    memset(req.method, 0, 500);
    memset(req.URI, 0, 500);
    memset(req.version, 0, 500);
    req.content_length = 0;
    req.startingIndex = 0;
    req.requestID = 0;
    req.connfd = connfd;

    char *message_body = NULL;
    message_body = malloc(5 * sizeof(char)); //allocate empty array size of incoming packet

    while ((len = read(connfd, buf, BUFSIZE)) > 0) {
        printf("Request ID: %d, iteration: %d, step: %d, len: %d\n", req.requestID, iterations,
            step, len);
        //set flag so we know that we read bytes
        if (iterations == 0) {
            wereadflag = 1;
        }
        if (len != 0) {
            // printf("----------NEW READ----------\n");
            //  printf("len: %d\n", len);

            bytesread = bytesread + len;

            //step 1: first find \r\n\r\n, set startingindex, and send everything up to message body to parsetokenizebuffer
            if (step == 1) {
                for (int i = 0; i < len; i++) {
                    if (buf[i] == '\r' || buf[i] == '\n') {
                        count++;
                    } else {
                        count = 0;
                    }
                    strncat(tokenizebuffer, &buf[i], 1);
                    byteswritten++;
                    if (count == 4) {
                        req.startingIndex = i + 1;
                        step = 2; //flag up
                        break;
                    }
                }
            }

            //step 2: parse all information and store in struct req
            if (step == 2) {
                req = parseTokenizeBuffer(tokenizebuffer, req, byteswritten);
                // printf("method: %s\n", req.method);
                // printf("URI: %s\n", req.URI);
                // printf("version: %s\n", req.version);
                // printf("requestID: %d\n", req.requestID);
                // printf("len: %d\n", len);
                // printf("startingindex: %d\n", req.startingIndex);

                //go to GET request if method is get and return
                if (strcmp(req.method, "GET") == 0) {
                    free(message_body);
                    getRequest(connfd, req);
                    return;
                }
                //reallocate memory to fit message body
                printf("content length: %d\n", req.content_length);
                message_body
                    = (char *) realloc(message_body, req.content_length + 1 * sizeof(char));
                step = 3;

                // printf("len: %d\n", len);
                // printf("startingindex: %d\n", req.startingIndex);
                // printf("len-startingindex-1: %d\n", len-req.startingIndex-1);
            }

            //setp 3: find out if we are case 1 (some bytes of message body still in buffer) or case 2 (startingindex = len). Then move to step 4
            if (step == 3) {
                //probably need to wait here if message body comes later, or just keep calling
                // printf("len: %d\n", len);
                // while(len == 0){ //wait until theres something to do with message body
                //     break;
                // }
                // printf("starting index: %d\n", req.startingIndex);
                if (req.startingIndex == len) { //we're in case 2
                    testcase = 2;
                    step = 4;
                } else { //we're in case 1
                    testcase = 1;
                    // if(iterations == 0){
                    //     req.startingIndex = req.startingIndex;
                    // }
                    step = 4;
                }
            }

            if (step == 4) {
                if (testcase == 1) {
                    // printf("len: %d\n", len);
                    // printf("startingindex: %d\n", req.startingIndex);
                    // printf("len-startingindex-1: %d\n", len-req.startingIndex-1);
                    if (iterations == 0) {
                        for (int i = 0; i < len - req.startingIndex; i++) {
                            // printf("%c", buf[req.startingIndex+i]);
                            message_body[byteswrittentorequest] = buf[req.startingIndex + i];
                            byteswrittentorequest++;
                        }
                    } else {
                        for (int i = 0; i < len; i++) {
                            // printf("%c", buf[req.startingIndex+i]);
                            message_body[byteswrittentorequest] = buf[i];
                            byteswrittentorequest++;
                        }
                    }
                    // printf("byteswrittentorequest: %d, req.content_length: %d\n", byteswrittentorequest, req.content_length);
                    req.startingIndex = 0;
                    if (byteswrittentorequest == req.content_length) { //we done
                        // printf("we done!\n");
                        goToRequest(message_body, connfd, req);
                        free(message_body);
                    } else if ((byteswrittentorequest - req.content_length) == 1) {
                        // printf("we done!\n");
                        goToRequest(message_body, connfd, req);
                        free(message_body);
                    }
                }

                if (testcase == 2) {
                    skip++;
                    if (skip == 1) {
                        // printf("need to read again\n");
                    } else {
                        step = 5;
                    }
                }
            }

            if (step == 5) {
                for (int i = 0; i < len; i++) {
                    message_body[byteswrittentorequest2] = buf[i];
                    byteswrittentorequest2++;
                }
                if (byteswrittentorequest2 == req.content_length) {
                    // printf("we done!\n");
                    goToRequest(message_body, connfd, req);
                    free(message_body);
                }
            }

            // printf("----------END READ----------\n");
        } else {
            printf("empty read!\n");
        }
        memset(buf, 0, BUFSIZE);
        iterations++;
    }

    //we didnt read any bytes, free allocated memory
    if (wereadflag == 0) {
        free(message_body);
    }
    return;
}

struct Request parseTokenizeBuffer(char *tokenizebuffer, struct Request req2, int byteswritten) {
    struct Request req = req2;
    int sizeofrequestline = 0;
    char request_line[2048 + 1] = { 0 };
    request_line[2048] = '\0';
    char header_field[2048 + 1] = { 0 };
    header_field[2048] = '\0';

    for (int i = 0; i < byteswritten; i++) {
        if (tokenizebuffer[i] == '\n' || tokenizebuffer[i] == '\r') {
            break;
        }
        request_line[i] = tokenizebuffer[i];
        sizeofrequestline++;
    }

    sscanf(request_line, "%s %*c%s %s", req.method, req.URI, req.version);

    for (int i = 0; i < byteswritten - sizeofrequestline; i++) {
        header_field[i] = tokenizebuffer[sizeofrequestline + i];
    }

    char parse[50];
    const char s[3] = "\r\n";
    char *token;
    token = strtok(header_field, s);
    int rqidexists = 0;
    int clexists = 0;
    while (token != NULL) {
        sscanf(token, "%s", parse);
        //get content length
        if (strcmp(parse, "Content-Length:") == 0) {
            clexists++;
            sscanf(token, "%*s%*c %d", &req.content_length);
        }
        //get request ID
        if (strcmp(parse, "Request-Id:") == 0) {
            rqidexists++;
            sscanf(token, "%*s%*c %d", &req.requestID);
        }
        token = strtok(NULL, s);
    }
    if (rqidexists == 0) {
        // printf("no request ID found! Setting req.requestID = 0\n");
        req.requestID = 0;
    }
    if (clexists == 0) {
        // printf("no content length found! Setting req.content_length = 0\n");
        req.content_length = 0;
    }

    return req;
}

void goToRequest(char *message_body, int connfd, struct Request req) {

    if (strcmp(req.method, "PUT") == 0) {
        putRequest(message_body, req, connfd);
    } else {
        appendRequest(message_body, req, connfd);
    }

    return;
}

void appendRequest(char *message_body, struct Request req, int connfd) {

    //open file with append
    int fd = open(req.URI, O_WRONLY | O_APPEND);
    if (fd == -1) {
        if (errno == EACCES) { //bad access privelige
            // printf("bad access privelige\n");
            send(connfd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n",
                strlen("HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n"), 0);
            generateLog(403, req);
            return;
        } else if (errno == EISDIR) { //can't read from directory
            // printf("can't read from directory\n");
            send(connfd,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server "
                "Error\n",
                strlen("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                       "Server Error\n"),
                0);
            generateLog(500, req);
            return;
        } else if (errno == ENOENT) { //file doesn't exist
            // printf("file doesnt exist\n");
            send(connfd, "HTTP/1.1 404 Not Found\r\nContent-Length: 15\r\n\r\nFile Not Found\n",
                strlen("HTTP/1.1 404 Not Found\r\nContent-Length: 15\r\n\r\nFile Not Found\n"), 0);
            generateLog(404, req);
            return;
        }
    }

    //writer lock here
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK; //create read lock
    fcntl(fd, F_SETLKW, &lock);

    //write to file
    write(fd, message_body, req.content_length);

    //unlock
    lock.l_type = F_UNLCK; //create read lock
    fcntl(fd, F_SETLKW, &lock);

    //send OK 200
    write(connfd, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n",
        strlen("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n"));
    generateLog(200, req);

    //close file
    close(fd);
    return;
}

//needs work
void putRequest(char *message_body, struct Request req, int connfd) {

    int status_code = 0;

    //attempt to open file with dummy fd
    int fdummy = open(req.URI, O_RDWR);

    if (fdummy == -1) {
        //file does not exist, we will create and fill file
        status_code = 201;
        // printf("file does not exist\n");
    } else {
        //file exists already and we're just overriding it
        status_code = 200;
        // printf("file does exist\n");
        close(fdummy);
    }

    int fd = open(req.URI, O_WRONLY | O_CREAT | O_TRUNC, 0644); //need some error checking here
    if (fd == -1) {
        if (errno == EACCES) { //bad access privelige
            // printf("bad access privelige\n");
            send(connfd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n",
                strlen("HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n"), 0);
            generateLog(403, req);
            return;
        } else if (errno == EISDIR) { //can't read from directory
            // printf("can't read from directory\n");
            send(connfd,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server "
                "Error\n",
                strlen("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                       "Server Error\n"),
                0);
            generateLog(500, req);
            return;
        }
    }
    //writer lock here
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK; //create read lock
    fcntl(fd, F_SETLKW, &lock);

    write(fd, message_body, req.content_length);

    //unlock
    lock.l_type = F_UNLCK; //create read lock
    fcntl(fd, F_SETLKW, &lock);

    if (status_code == 201) {
        send(connfd, "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n",
            strlen("HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n"), 0);
        generateLog(201, req);
        // snprintf(response, max_len, "HTTP/1.1 201 Created\r\n", status_code);
        // printf("Sending...\n");
    }
    if (status_code == 200) {
        send(connfd, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n",
            strlen("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n"), 0);
        generateLog(200, req);
        // snprintf(response, max_len, "HTTP/1.1 200 OK\r\n", status_code);
        // printf("Sending...\n");
    }

    close(fd);
    return;
}

void getRequest(int connfd, struct Request req) {

    //first, get size of file from URI
    int size = 0;
    struct stat st;
    stat(req.URI, &st);
    int isdir = S_ISDIR(st.st_mode);
    if (isdir == 1) {
        send(connfd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n",
            strlen("HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n"), 0);
        generateLog(403, req);
        return;
    }
    // printf("isdir = %d\n", isdir);
    size = st.st_size;

    //open file
    int fd, len;
    // printf("%s\n", req.URI);
    fd = open(req.URI, O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) { //file doesn't exist
            // printf("file doesnt exist\n");
            send(connfd, "HTTP/1.1 404 Not Found\r\nContent-Length: 15\r\n\r\nFile Not Found\n",
                strlen("HTTP/1.1 404 Not Found\r\nContent-Length: 15\r\n\r\nFile Not Found\n"), 0);
            generateLog(404, req);
            return;
        } else if (errno == EACCES) { //bad access privelige
            // printf("bad access privelige\n");
            send(connfd,
                "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server "
                "Error\n",
                strlen("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                       "Server Error\n"),
                0);
            generateLog(500, req);
            return;
        } else if (errno == EISDIR) { //can't read from directory
            // printf("can't read from directory\n");
            send(connfd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n",
                strlen("HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n"), 0);
            generateLog(403, req);
            return;
        }
    }

    //test print
    // printf("URI: %s, size of file: %d, connfd: %d\n", URI, size, connfd);

    //allocate array for message body and buffer
    char *arr = NULL;
    arr = malloc(size + 1 * sizeof(char));

    //reader lock here
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK; //create read lock
    fcntl(fd, F_SETLKW, &lock);

    //read
    //read from file and store in arr
    while ((len = read(fd, arr, size)) > 0) {
        if (len != size) {
            printf("problem reading GET file\n");
        }
    }

    //reader unlock here
    lock.l_type = F_UNLCK; //unlock
    fcntl(fd, F_SETLKW, &lock);

    //put together response
    // int response_size = 17+16+content_length_size+4+
    char response[500 + size];
    int max_len = sizeof response;
    snprintf(response, max_len, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", size);

    send(connfd, response, strlen(response), 0);
    send(connfd, arr, size, 0);
    generateLog(200, req);

    //free arr and close fd
    close(fd);
    free(arr);
    return;
}

void generateLog(int status_code, struct Request req) {

    // printf("acquiring lock for logfile section\n");
    pthread_mutex_lock(&loglock); //lock critical section

    //write to logfile
    LOG("%s,/%s,%d,%d\n", req.method, req.URI, status_code, req.requestID);
    //need to flush or memory error happens
    fflush(logfile);
    // printf("done with reqID: %d\n", req.requestID);
    close(req.connfd);

    // printf("unlocking logfile section\n");
    pthread_mutex_unlock(&loglock); //lock critical section

    return;
}
