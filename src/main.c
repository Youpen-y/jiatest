#include "msg_queue.h"
#include "rdma_comm.h"
#include "tools.h"
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>

extern int snd_seq[Maxhosts];

// cm_id_array[i] is to used to communicate with host[i]
struct rdma_cm_id cm_id_array[Maxhosts];

// these need to reconfiguration on another host
#ifdef MASTER
    const char *server_ip = "192.168.103.1";
    const char *client_ip = "192.168.103.2";
    int jia_pid = 0;
    int to_pid = 1;
#else
    const char *server_ip = "192.168.103.2";
    const char *client_ip = "192.168.103.1";
    int jia_pid = 1;
    int to_pid = 0;
#endif

FILE *logfile;

void generate_random_string(char *dest, size_t length);
int move_msg_to_outqueue(jia_msg_t *msg, msg_queue_t *outqueue);

int main()
{
    int batching_num = 8;
    if(open_logfile("jiajia.log")) {
        log_err("Unable to open jiajia.log");
        exit(-1);
    }
    setbuf(logfile, NULL);

    init_msg_queue(&inqueue, SIZE);
    init_msg_queue(&outqueue, SIZE);

    init_rdma_context(&ctx, batching_num);

    pthread_create(&rdma_listen_tid, NULL, rdma_listen, NULL);
    pthread_create(&rdma_client_tid, NULL, rdma_client, NULL);
    pthread_create(&rdma_server_tid, NULL, rdma_server, NULL);


    jia_msg_t msg;
    while(1) {
        msg.frompid = jia_pid;
        msg.topid = to_pid;
        msg.temp = -1;
        msg.seqno = snd_seq[msg.topid];
        msg.index = 0;
        msg.scope = 0;
        msg.size = 16;
        generate_random_string((char *)msg.data, SIZE);
        
        move_msg_to_outqueue(&msg, &outqueue);
        sleep(10);
    }
}

void generate_random_string(char *dest, size_t length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"; // 字符集
    size_t charset_size = sizeof(charset) - 1; // 不包括末尾的 '\0'

    // 生成随机字符串并存储到 dest 中
    for (size_t i = 0; i < length - 1; ++i) { // 留出最后一个字符的位置给 '\0'
        dest[i] = charset[rand() % charset_size];
    }

    dest[length - 1] = '\0'; // 添加字符串结束符
    log_info(3, "generate string: %s", dest);
}

int move_msg_to_outqueue(jia_msg_t *msg, msg_queue_t *outqueue) {
    int ret = enqueue(outqueue, msg);
    if (ret == -1) {
        perror("enqueue");
        return ret;
    }
    return 0;
}