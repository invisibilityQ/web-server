#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "./locker/locker.h"
#include "threadpool.h"
#include "./http_conn/http_conn.h"
#include "./log/log.h"


int main(int argc,char* argv[]){
    log_write();
    http_conn task;
    task.start();
    return 0;
}