#include "msg_queue.h"
#include "rdma_comm.h"
#include "tools.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <infiniband/verbs.h>
#include <stdint.h>

#define RETRYNUM 50 // when hosts increases, this number should increases too.
pthread_t rdma_client_tid;
static struct ibv_wc wc;
static struct ibv_send_wr *bad_wr;
static bool success = false;
static jia_msg_t *msg_ptr;
int snd_seq[Maxhosts] = {0};
int seq = 0;

void printmsg(jia_msg_t *msg);

int post_send(jia_context_t *ctx, jia_msg_t *msg_ptr) {
    /* step 1: init wr, sge, for rdma to send */
    struct ibv_sge sge = {.addr = (uint64_t)msg_ptr,
                          .length = sizeof(jia_msg_t),
                          .lkey = ctx->send_mr[ctx->outqueue->head]->lkey};

    struct ibv_send_wr wr = {
        .wr_id = seq,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED,
        .wr = {.ud = {.ah = ctx->ah[msg_ptr->topid],
                      .remote_qpn = dest_info[msg_ptr->topid].qpn,
                      .remote_qkey = 0x11111111}}};

    /* step 2: loop until ibv_post_send wr successfully */
    while (ibv_post_send(ctx->qp, &wr, &bad_wr)) {
        log_err("Failed to post send");
    }

    /* step 3: check if we send the packet to fabric */
    int ne = ibv_poll_cq(ctx->send_cq, 1, &wc);
    if (ne < 0) {
        log_err("ibv_poll_cq failed");
        return -1;
    }
    if (wc.status != IBV_WC_SUCCESS) {
        log_err("Failed status %s (%d) for wr_id %d",
                ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
        return -1;
    }

    return 0;
}

void *rdma_client(void *arg) {
    while (1) {
        /* step 0: get sem value to print */
        int semvalue;
        sem_getvalue(&ctx.outqueue->busy_count, &semvalue);
        log_info(4, "pre client outqueue dequeue busy_count value: %d",
                 semvalue);
        // wait for busy slot
        sem_wait(&ctx.outqueue->busy_count);
        sem_getvalue(&ctx.outqueue->busy_count, &semvalue);
        log_info(4, "enter client outqueue dequeue! busy_count value: %d",
                 semvalue);

        /* step 1: give seqno */
        msg_ptr = &(ctx.outqueue->queue[ctx.outqueue->head].msg);
        msg_ptr->seqno = snd_seq[msg_ptr->topid];

        /* step 2: send msg && ack */
        for (int retries_num = 0; retries_num < RETRYNUM; retries_num++) {
            if (!post_send(&ctx, msg_ptr)) {
                success = true;
                break;
            }
#ifdef DOSTAT
            STATOP(jiastat.resendcnt++;)
#endif
            log_info(3, "Failed to send msg, will resend");
        }
        log_info(3, "Send outqueue[%d] msg <%s> successfully", ctx.outqueue->head, ctx.outqueue->queue[ctx.outqueue->head].msg.data);

        /* step 3: manage error */
        if (success) {
            log_info(4, "send msg success!");
            success = false;
        } else {
            log_err("send msg failed[msg: %lx]", (unsigned long)msg_ptr);
            printmsg(msg_ptr);
        }

        /* step 4: update snd_seq and head ptr */
        snd_seq[msg_ptr->topid]++;
        ctx.outqueue->head =
            (ctx.outqueue->head + 1) % SIZE;

        /* step 5: sem post and print value */
        sem_post(&ctx.outqueue->free_count);
        sem_getvalue(&ctx.outqueue->free_count, &semvalue);
        log_info(4, "after client outqueue dequeue free_count value: %d",
                 semvalue);
    }
}

void printmsg(jia_msg_t *msg) {
    printf("msg <from:%d, to:%d, seq:%d, data:%s> \n", msg->frompid, msg->topid, msg_ptr->seqno, msg_ptr->data);
}