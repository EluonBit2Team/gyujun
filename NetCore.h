#ifndef NET_CORE_H

#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define TRUE 1
#define FALSE 0
#define PORT 3334
#define MAX_CLIENT_NUM 100
#define EPOLL_SIZE MAX_CLIENT_NUM
#define BUFF_SIZE 1024

#define SERVICE_FUNC_NUM 10
#define ECHO_SERVICE_FUNC 0

#define WOKER_THREAD_NUM 4
#define MAX_TASK_SIZE 100

struct st_task {
    int service_id;
    int req_client_fd;
    char buf[BUFF_SIZE];
    int task_data_len;
} typedef task;

struct st_thread_pool {

    pthread_mutex_t task_mutex;
    pthread_cond_t task_cond;
    int task_cnt;
    task tasks[MAX_TASK_SIZE];
    
    pthread_t worker_threads[WOKER_THREAD_NUM];
} typedef thread_pool_t;

// struct st_req_pack {
//     int service_id;
//     int client_fd;
//     char message[BUFF_SIZE + 1];
// } typedef req_pack;

struct st_client_session {
    int fd;
    char recv_buf[BUFF_SIZE];
    char send_buf[BUFF_SIZE];
    int send_data_size;
} typedef client_session;

struct st_epoll_net_core;
typedef void (*func_ptr)(struct st_epoll_net_core*, task*);
typedef struct st_epoll_net_core {
    int is_run;
    int listen_fd;

    func_ptr function_array[SERVICE_FUNC_NUM];
    
    client_session client_sessions[MAX_CLIENT_NUM];
    struct sockaddr_in listen_addr;
    
    int epoll_fd;
    struct epoll_event* epoll_events;

    thread_pool_t thread_pool;
} epoll_net_core;


void enqueue_task(thread_pool_t* thread_pool, int req_client_fd, int req_service_id, char* org_buf, int org_data_size)
{
    pthread_mutex_lock(&thread_pool->task_mutex);

    if (thread_pool->task_cnt == MAX_TASK_SIZE)
    {
        pthread_mutex_unlock(&thread_pool->task_mutex);
        return ;
    }

    task* queuing_task = &thread_pool->tasks[thread_pool->task_cnt++];
    //printf("%d task enqueue\n", thread_pool->task_cnt);
    queuing_task->service_id = req_service_id;
    queuing_task->req_client_fd = req_client_fd;
    memcpy(queuing_task->buf, org_buf, org_data_size);
    queuing_task->task_data_len = org_data_size;

    pthread_cond_signal(&thread_pool->task_cond);
    pthread_mutex_unlock(&thread_pool->task_mutex);
}

int deqeueu_and_get_task(thread_pool_t* thread_pool, task* des)
{
    pthread_mutex_lock(&thread_pool->task_mutex);

    if (thread_pool->task_cnt == 0)
    {
        pthread_mutex_unlock(&thread_pool->task_mutex);
        return FALSE;
    }

    //printf("%d task dequeue\n", thread_pool->task_cnt);
    task* dequeuing_task = &thread_pool->tasks[--thread_pool->task_cnt];
    des->req_client_fd = dequeuing_task->req_client_fd;
    des->service_id = dequeuing_task->service_id;
    memcpy(des->buf, dequeuing_task->buf, dequeuing_task->task_data_len);
    des->task_data_len = dequeuing_task->task_data_len;
    //thread_pool->task_cnt--;

    pthread_mutex_unlock(&thread_pool->task_mutex);
    return TRUE;
}

void* work_routine(void *ptr)
{
    epoll_net_core* server_ptr = (epoll_net_core *)ptr;
    thread_pool_t* thread_pool = &server_ptr->thread_pool;
    while (1) {
        //printf("thread wating...\n");
        pthread_mutex_lock(&thread_pool->task_mutex);
        while (thread_pool->task_cnt == 0) {
            pthread_cond_wait(&thread_pool->task_cond, &thread_pool->task_mutex);
        }
        //printf("thread wakeup\n");
        pthread_mutex_unlock(&thread_pool->task_mutex);

        task temp_task;
        //printf("thread deqeueu_and_get_task\n");
        if (deqeueu_and_get_task(thread_pool, &temp_task) == TRUE)
        {
            server_ptr->function_array[temp_task.service_id](server_ptr, &temp_task);
        }
    }
    return NULL;
}

void init_worker_thread(epoll_net_core* server_ptr, thread_pool_t* thread_pool_t_ptr)
{
    pthread_mutex_init(&thread_pool_t_ptr->task_mutex, NULL);
    pthread_cond_init(&thread_pool_t_ptr->task_cond, NULL);
    for (int i = 0; i < WOKER_THREAD_NUM; i++)
    {
        pthread_create(&thread_pool_t_ptr->worker_threads[i], NULL, work_routine, server_ptr);
    }
}


void echo_service(epoll_net_core* server_ptr, task* task) {
    // 보낸사람 이외에 전부 출력.
    for (int i = 0; i < MAX_CLIENT_NUM; i++)
    {
        if (server_ptr->client_sessions[i].fd == -1
            || task->req_client_fd == server_ptr->client_sessions[i].fd)
        {
            continue ;
        }

        struct epoll_event temp_event;
        temp_event.events = EPOLLOUT | EPOLLET;
        temp_event.data.fd = server_ptr->client_sessions[i].fd;
        if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, server_ptr->client_sessions[i].fd, &temp_event) == -1) {
            perror("epoll_ctl: add");
            close(task->req_client_fd);
        }
        // 버퍼에 데이터를 저장
        memcpy(server_ptr->client_sessions[i].send_buf, task->buf, task->task_data_len);
        server_ptr->client_sessions[i].send_data_size = task->task_data_len;
    }
}

void set_sock_nonblocking_mode(int sockFd) {
    int flag = fcntl(sockFd, F_GETFL, 0);
    fcntl(sockFd, F_SETFL, flag | O_NONBLOCK);
}


int init_server(epoll_net_core* server_ptr) {
    for (int i = 0; i < MAX_CLIENT_NUM; i++)
    {
        server_ptr->client_sessions[i].fd = -1;
        server_ptr->client_sessions[i].send_data_size = -1;
    }

    server_ptr->is_run = FALSE;
    server_ptr->listen_addr.sin_family = AF_INET;
    server_ptr->listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_ptr->listen_addr.sin_port = htons(PORT);

    for (int i = 0; i < SERVICE_FUNC_NUM; i++)
    {
        server_ptr->function_array[i] = NULL;
    }
    server_ptr->function_array[ECHO_SERVICE_FUNC] = echo_service;
    server_ptr->listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (server_ptr->listen_fd < 0)
    {
        printf("listen sock assignment error: \n", errno);
    }
    set_sock_nonblocking_mode(server_ptr->listen_fd);
}

int accept_client(epoll_net_core* server_ptr) {
    struct epoll_event temp_event;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = client_addr_size = sizeof(client_addr);
    int client_sock = accept(server_ptr->listen_fd, (struct sockaddr*)&(client_addr), &client_addr_size);
    if (client_sock < 0) {
        printf("accept error: \n", errno);
    }

    set_sock_nonblocking_mode(client_sock);

    server_ptr->client_sessions[client_sock].fd = client_sock;
    memset(server_ptr->client_sessions[client_sock].recv_buf, 0, BUFF_SIZE);
    memset(server_ptr->client_sessions[client_sock].send_buf, 0, BUFF_SIZE);

    temp_event.data.fd = client_sock;
    // ✨ 엣지트리거방식의(EPOLLIN) 입력 이벤트 대기 설정(EPOLLET)
    temp_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_ADD, client_sock, &temp_event);
    printf("accept \n client", client_sock);
}

void disconnect_client(epoll_net_core* server_ptr, int client_fd)
{
    epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
    printf("disconnect:%d\n", client_fd);
}

int run_server(epoll_net_core* server_ptr) {
    server_ptr->is_run = TRUE;
    struct epoll_event temp_epoll_event;
    server_ptr->epoll_fd = epoll_create1(0);
    //server_ptr->epoll_fd = epoll_create(EPOLL_SIZE);
    if (server_ptr->epoll_fd < 0)
    {
        printf("epoll_fd Error : %d\n", errno);
    }
    server_ptr->epoll_events = malloc(sizeof(struct epoll_event) * EPOLL_SIZE);

    init_worker_thread(server_ptr, &server_ptr->thread_pool);
    int rt_val = bind(server_ptr->listen_fd, 
        (struct sockaddr*) &server_ptr->listen_addr, 
        sizeof(server_ptr->listen_addr));
    if (rt_val < 0) {
        printf("bind Error : %d\n", errno);
    }

    rt_val = listen(server_ptr->listen_fd, SOMAXCONN);
    if (rt_val < 0) {
        printf("listen Error : %d\n", errno);
    }
    set_sock_nonblocking_mode(server_ptr->listen_fd);

    temp_epoll_event.events = EPOLLIN;
    temp_epoll_event.data.fd = server_ptr->listen_fd;
    rt_val = epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_ADD, server_ptr->listen_fd, &temp_epoll_event);
    if (rt_val < 0) {
        printf("epoll_ctl Error : %d\n", errno);
    }

    while (server_ptr->is_run == TRUE) {
        int occured_event_cnt = epoll_wait(
            server_ptr->epoll_fd, server_ptr->epoll_events, 
            EPOLL_SIZE, -1);
        if (occured_event_cnt < 0) {
            printf("epoll_wait Error : %d\n", errno);
        }
        
        for (int i = 0; i < occured_event_cnt; i++) {
            if (server_ptr->epoll_events[i].data.fd == server_ptr->listen_fd) {
                accept_client(server_ptr);
                printf("accept\n");
            }
            else if (server_ptr->epoll_events[i].events & EPOLLIN) {
                int client_fd = server_ptr->epoll_events[i].data.fd;
                //printf("some thing in from :%d\n", client_fd);
                int input_size = read(client_fd, server_ptr->client_sessions[client_fd].recv_buf, BUFF_SIZE);
                if (input_size == 0)
                {
                    printf("input_size == 0\n");
                    disconnect_client(server_ptr, client_fd);
                }
                else if (input_size < 0)
                {
                    // errno EAGAIN?
                    printf("input_size < 0\n");
                }
                else
                {
                    printf("input_handler\n");
                    enqueue_task(
                        &server_ptr->thread_pool, client_fd, ECHO_SERVICE_FUNC, 
                        server_ptr->client_sessions[client_fd].recv_buf, input_size);
                    //input_handler(server_ptr, client_fd, input_size);
                }
            }
            else if (server_ptr->epoll_events[i].events & EPOLLOUT) {
                int client_fd = server_ptr->epoll_events[i].data.fd;
                //printf("some thing can send to:%d\n", client_fd);
                ssize_t sent = send(
                    client_fd, 
                    server_ptr->client_sessions[client_fd].send_buf, 
                    server_ptr->client_sessions[client_fd].send_data_size, 0);
                if (sent == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    } 
                    else {
                        perror("send");
                        close(server_ptr->epoll_events[i].data.fd);
                    }
                }
                struct epoll_event temp_event;
                temp_event.events = EPOLLIN | EPOLLET;
                temp_event.data.fd = server_ptr->epoll_events[i].data.fd;
                if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, server_ptr->epoll_events[i].data.fd, &temp_event) == -1) {
                    perror("epoll_ctl: del");
                    close(server_ptr->epoll_events[i].data.fd);
                }
            }
            else {
                printf("?\n");
            }
        }
    }
}

void down_server(epoll_net_core* server_ptr)
{
    server_ptr->is_run = FALSE;
    close(server_ptr->listen_fd);
    close(server_ptr->epoll_fd);
    free(server_ptr->epoll_events);
}

#endif