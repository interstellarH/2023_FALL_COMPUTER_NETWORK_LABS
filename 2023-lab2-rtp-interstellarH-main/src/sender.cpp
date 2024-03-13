#include "sender.h"
#include <deque>
#include <errno.h>
using namespace std;

static int WINDOW_SIZE;
static int MODE; //0 for GBN, 1 for SR
static int sockfd;
static struct sockaddr_in raddr;
static uint32_t x_seqnum; 

void sendMsgGBN(const char* filename)
{
    int send_base = 0;
    int nextseqnum = 0;
    int init_x = x_seqnum;//保留原始的x_seqnum

    //读取文件
    FILE *fp = fopen(filename, "rb");
    if(fp == NULL){
        LOG_FATAL("File open failed\n");
    }
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    //注意要分片发送，每次的数据大小为PAYLOAD_MAX
    int num_packets = file_size / PAYLOAD_MAX + 1;
    int last_packet_size = file_size % PAYLOAD_MAX;

    //先发一个window，但是也要注意不能过大
    for (int i = 0; i < WINDOW_SIZE && i < num_packets && i < 32; i++)
    {
        fseek(fp, i * PAYLOAD_MAX, SEEK_SET); //将文件指针移动到相应位置!
        char buf[PAYLOAD_MAX];
        memset(buf, 0, PAYLOAD_MAX);
        fread(buf, sizeof(char), PAYLOAD_MAX, fp);
        if(i == num_packets - 1){
            rtp_send(sockfd, i+init_x, last_packet_size, 0, buf, (struct sockaddr*)&raddr);
            LOG_DEBUG("send file finished\n");
        }
        else{
            rtp_send(sockfd, i+init_x, PAYLOAD_MAX, 0, buf, (struct sockaddr*)&raddr);
        }
        nextseqnum++;
    }

    clock_t start = clock();
    clock_t end;
    while(1) //三个模块
    {
        //超时重传：对于回退N算法，Sender每次重传当前窗口的所有报文
        errno = 0;
        //接收ACK，判断相应包的发送情况
        rtp_packet_t* DATA_ACK = rtp_receive(sockfd, (struct sockaddr*)&raddr);
        end = clock();
        //超时重传窗口内的数据包，包括套接字超时和计时器超时
        if(((double)(end - start)/CLOCKS_PER_SEC > 0.1) || (DATA_ACK == NULL && (errno == EAGAIN || errno == EWOULDBLOCK))){
            LOG_DEBUG("Time out when waiting for ACK\n");
            for(int i = send_base; i < nextseqnum; i++){ //这里是传从base到nextseqnum-1的数据包
                LOG_DEBUG("\n");
                LOG_DEBUG("Resend package %d\n", i);
                fseek(fp, i * PAYLOAD_MAX, SEEK_SET); //将文件指针移动到相应位置!
                char buf[PAYLOAD_MAX];
                memset(buf, 0, PAYLOAD_MAX);
                fread(buf, sizeof(char), PAYLOAD_MAX, fp);
                if(i == num_packets - 1){
                    rtp_send(sockfd, i+init_x, last_packet_size, 0, buf, (struct sockaddr*)&raddr);
                    LOG_DEBUG("send file finished\n");
                }
                else{
                    rtp_send(sockfd, i+init_x, PAYLOAD_MAX, 0, buf, (struct sockaddr*)&raddr);
                }
            }
            start = clock();
            if(DATA_ACK!=NULL) free(DATA_ACK);
            continue;
        }
        else if(DATA_ACK == NULL){
            LOG_DEBUG("sender: receive NULL ACK\n");
            continue;
        }
        else if(DATA_ACK != NULL){
            LOG_DEBUG("sender: receive ACK\n");
            LOG_DEBUG("\n");
            LOG_DEBUG("DATA->rtp.seq_num: %u\n",DATA_ACK->rtp.seq_num);
            LOG_DEBUG("DATA->rtp.flags: %u\n",DATA_ACK->rtp.flags);
            LOG_DEBUG("DATA->rtp.length: %u\n",DATA_ACK->rtp.length);
            LOG_DEBUG("DATA->rtp.checksum: %u\n",DATA_ACK->rtp.checksum);
            int ack_seqnum = DATA_ACK->rtp.seq_num - init_x - 1; //这里必须用init_x，因为x_seqnum会变化
            //且应该是 DATA_ACK.rtp.seq_num - 1因为receiver返回的是期望收到的
            LOG_DEBUG("\n");
            LOG_DEBUG("nextseqnum: %u\n",nextseqnum);
            LOG_DEBUG("ack_seq_num: %u\n",ack_seqnum);
            LOG_DEBUG("\n");
            if(DATA_ACK->rtp.flags != RTP_ACK){
                LOG_DEBUG("sender: receive unexpected package, drop it\n");
                free(DATA_ACK);//收到不需要回复的包，丢弃需要free
                continue;
            }
            else if (ack_seqnum < send_base || ack_seqnum >= nextseqnum){
                LOG_DEBUG("sender: receive ACK out of window\n");
                free(DATA_ACK);
                continue;
            }
            else if(ack_seqnum >= send_base && ack_seqnum < nextseqnum ){
                LOG_DEBUG("sender: receive ACK in window\n");
                //移动窗口并重置计时器
                send_base = ack_seqnum + 1;
                //发送新包
                while((nextseqnum < send_base + WINDOW_SIZE) && (nextseqnum < num_packets)){
                    LOG_DEBUG("sender: move window, send new packages\n");
                    fseek(fp, nextseqnum * PAYLOAD_MAX, SEEK_SET); //将文件指针移动到相应位置!
                    char buf[PAYLOAD_MAX];
                    memset(buf, 0, PAYLOAD_MAX);
                    fread(buf, sizeof(char), PAYLOAD_MAX, fp);
                    if(nextseqnum == num_packets - 1){
                        rtp_send(sockfd, nextseqnum+init_x, last_packet_size, 0, buf, (struct sockaddr*)&raddr);
                        LOG_DEBUG("sender: send file finished\n");
                    }
                    else{
                        rtp_send(sockfd, nextseqnum+init_x, PAYLOAD_MAX, 0, buf, (struct sockaddr*)&raddr);
                    }
                    nextseqnum++;
                }
                if(send_base == num_packets){ //说明所有包传输且确认完毕
                    LOG_DEBUG("All sent packages have been acked.\n");
                    x_seqnum = nextseqnum + init_x;//这里必须更新，因为下面发FIN要用到
                    if(DATA_ACK!=NULL) free(DATA_ACK);
                    break;
                }
                start = clock();
                free(DATA_ACK);
            }
        }
        else {
            LOG_FATAL("sender: Unexpected error\n");
        }
    }
    fclose(fp);
    return;
}

void sendMsgSR(const char* filename)
{
    int send_base = 0;
    int nextseqnum = 0;
    int init_x = x_seqnum;//保留原始的x_seqnum
    deque<int> acks(WINDOW_SIZE, 0);

    //读取文件
    FILE *fp = fopen(filename, "rb");
    if(fp == NULL){
        LOG_FATAL("File open failed\n");
    }
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    //注意要分片发送，每次的数据大小为PAYLOAD_MAX
    int num_packets = file_size / PAYLOAD_MAX + 1;
    int last_packet_size = file_size % PAYLOAD_MAX;
    
    // [send_base, next_seq_num - 1]: 已发送但未收到 ACK 的数据包
    // [next_seq_num, send_base + window_size - 1]: 未发送且未收到 ACK 的数据包

    //发送第一个window
    for(int i = 0; i < WINDOW_SIZE && i < num_packets; i++){
        fseek(fp, i * PAYLOAD_MAX, SEEK_SET); //将文件指针移动到相应位置!
        char buf[PAYLOAD_MAX];
        memset(buf, 0, PAYLOAD_MAX);
        fread(buf, sizeof(char), PAYLOAD_MAX, fp);
        if(i == num_packets - 1){
            rtp_send(sockfd, i+init_x, last_packet_size, 0, buf, (struct sockaddr*)&raddr);
            LOG_DEBUG("send file finished\n");
        }
        else{
            rtp_send(sockfd, i+init_x, PAYLOAD_MAX, 0, buf, (struct sockaddr*)&raddr);
        }
        nextseqnum++;
    }

    clock_t start=clock();
    clock_t end;
    while(1){
        end = clock();
        rtp_packet_t* DATA_ACK = rtp_receive(sockfd, (struct sockaddr*)&raddr);
        //超时重传窗口内的ack[i]==0的数据包，包括套接字超时和计时器超时
        if((DATA_ACK == NULL && (errno == EAGAIN || errno == EWOULDBLOCK)) || ((double)(end - start)/CLOCKS_PER_SEC > 0.1)){
            LOG_DEBUG("Time out when waiting for ACK, resend all unacked packages\n");
            for(int i=send_base; i<nextseqnum; i++){
                if(acks[i-send_base] == 0){
                    LOG_DEBUG("\n");
                    LOG_DEBUG("Resend package %d\n", i);
                    fseek(fp, i * PAYLOAD_MAX, SEEK_SET); //将文件指针移动到相应位置!
                    char buf[PAYLOAD_MAX];
                    memset(buf, 0, PAYLOAD_MAX);
                    fread(buf, sizeof(char), PAYLOAD_MAX, fp);
                    if(i == num_packets - 1){
                        rtp_send(sockfd, i+init_x, last_packet_size, 0, buf, (struct sockaddr*)&raddr);
                        LOG_DEBUG("send file finished\n");
                    }
                    else{
                        rtp_send(sockfd, i+init_x, PAYLOAD_MAX, 0, buf, (struct sockaddr*)&raddr);
                    }
                }
            }
            start = clock();
            if(DATA_ACK!=NULL) free(DATA_ACK);
            continue;
        }
        else if(DATA_ACK == NULL){
            LOG_DEBUG("sender: receive NULL ACK\n");
            continue;
        }
        else if(DATA_ACK != NULL){
            LOG_DEBUG("sender: receive ACK\n");
            LOG_DEBUG("\n");
            LOG_DEBUG("DATA->rtp.seq_num: %u\n",DATA_ACK->rtp.seq_num);
            LOG_DEBUG("DATA->rtp.flags: %u\n",DATA_ACK->rtp.flags);
            LOG_DEBUG("DATA->rtp.length: %u\n",DATA_ACK->rtp.length);
            LOG_DEBUG("DATA->rtp.checksum: %u\n",DATA_ACK->rtp.checksum);
            int ack_seqnum = DATA_ACK->rtp.seq_num - init_x;
            LOG_DEBUG("\n");
            LOG_DEBUG("nextseqnum: %u\n",nextseqnum);
            LOG_DEBUG("ack_seq_num: %u\n",ack_seqnum);
            LOG_DEBUG("\n");
            if(DATA_ACK->rtp.flags != RTP_ACK){
                LOG_DEBUG("sender: receive unexpected package, drop it\n");
                free(DATA_ACK);
                continue;
            }
            else if(ack_seqnum < send_base || ack_seqnum >= nextseqnum){ //这里之前写的是ack_seqnum < send_base + WINDOW_SIZE，导致一直出错
                LOG_DEBUG("sender: receive ACK out of window\n");
                free(DATA_ACK);
                continue;
            }
            else if(ack_seqnum >= send_base && ack_seqnum < nextseqnum && DATA_ACK->rtp.flags == RTP_ACK){
                LOG_DEBUG("sender: receive ACK in window\n");
                if(acks[ack_seqnum - send_base] == 1){
                    LOG_DEBUG("sender: receive duplicate ACK\n");
                    free(DATA_ACK);
                    continue;
                }
                else{
                    acks[ack_seqnum - send_base] = 1;
                    //移动窗口并重置计时器
                    while(acks[0]==1){
                        LOG_DEBUG("sender: move window\n");
                        send_base++;
                        acks.pop_front();
                        acks.push_back(0);
                        //发送新的数据包
                        if(nextseqnum < num_packets){
                            fseek(fp, nextseqnum * PAYLOAD_MAX, SEEK_SET); //将文件指针移动到相应位置!
                            char buf[PAYLOAD_MAX];
                            memset(buf, 0, PAYLOAD_MAX);
                            fread(buf, sizeof(char), PAYLOAD_MAX, fp);
                            if(nextseqnum == num_packets - 1){
                                rtp_send(sockfd, nextseqnum+init_x, last_packet_size, 0, buf, (struct sockaddr*)&raddr);
                                LOG_DEBUG("sender: send file finished\n");
                            }
                            else{
                                rtp_send(sockfd, nextseqnum+init_x, PAYLOAD_MAX, 0, buf, (struct sockaddr*)&raddr);
                            }
                            nextseqnum++;
                        }
                    }
                    if(send_base == num_packets){
                        LOG_DEBUG("sender: All files acked!\n");
                        x_seqnum = nextseqnum + init_x;//这里必须更新，因为下面发FIN要用到， 啊啊啊忘了加init_x了
                        if(DATA_ACK!=NULL) free(DATA_ACK);
                        break;
                    }
                    start = clock();
                    free(DATA_ACK);
                }  
            }
        }
        else {
            LOG_FATAL("sender: Unexpected error\n");
        }
    }
    fclose(fp);
    return;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        LOG_FATAL("Usage: ./sender [receiver ip] [receiver port] [file path] "
                  "[window size] [mode]\n");
    }

    /*Step0: parse arguments*/
    string sender_ip = argv[1];
    int sender_port = atoi(argv[2]);
    const char* filename = argv[3];
    WINDOW_SIZE = atoi(argv[4]);
    MODE = atoi(argv[5]);
    LOG_DEBUG("filename: %s\n", filename);
    LOG_DEBUG("window size: %d\n", WINDOW_SIZE);
    LOG_DEBUG("mode: %d\n", MODE);
    LOG_DEBUG("sender port: %d\n", sender_port);
    LOG_DEBUG("receiver ip: %s\n", sender_ip.c_str());

    /************************Step1: init socket************************/
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        LOG_FATAL("socket error\n");
    }
    bzero(&raddr, sizeof(raddr));
    raddr.sin_family = AF_INET;
    raddr.sin_port = htons(sender_port);
    inet_pton(AF_INET, sender_ip.c_str(), &raddr.sin_addr);

    srand((unsigned int)time(NULL));

    struct timeval tv;
    tv.tv_sec = 5; 
    tv.tv_usec = 100000;// 设置超时时间为 5 秒
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        LOG_FATAL("setsockopt() in receiver_establish_connection() failed\n");

    //第一次握手，发送SND_SYN
    x_seqnum = rand(); //这个随机数最好记录下来，方便后面重传
    rtp_send(sockfd, x_seqnum, 0, RTP_SYN, NULL, (struct sockaddr*)&raddr);
    LOG_DEBUG("sender: send 1st handshake SYN\n");

    clock_t start1 = clock();
    clock_t end1;
    //第二次握手确认，sender接收SYN_ACK
    rtp_packet_t* SYN_ACK;
    while(1){
        end1 = clock();
        errno = 0;
        SYN_ACK = rtp_receive(sockfd, (struct sockaddr*)&raddr);
        if(SYN_ACK == NULL && (errno == EAGAIN || errno == EWOULDBLOCK)){
            LOG_DEBUG("sender: socket time out, resend 1st handshake package\n");
            rtp_send(sockfd, x_seqnum, 0, RTP_SYN, NULL, (struct sockaddr*)&raddr);
            start1 = clock();
            continue;
        }
        else if((double)(end1 - start1) / CLOCKS_PER_SEC > 0.1){ //超时重传
            LOG_DEBUG("sender: normal timeout, RESEND 1st handshake SYN\n");
            rtp_send(sockfd, x_seqnum, 0, RTP_SYN, NULL, (struct sockaddr*)&raddr);
            start1 = clock();
            if(SYN_ACK!=NULL) free(SYN_ACK);
            continue;
        }
        else if(SYN_ACK == NULL){
            LOG_DEBUG("sender: receive NULL SYN_ACK\n");
            continue;
        }
        else if((SYN_ACK->rtp.flags != (RTP_SYN | RTP_ACK)) || (SYN_ACK->rtp.seq_num != x_seqnum + 1))
        {
            LOG_DEBUG("sender: Unexpected package, drop it\n");
            free(SYN_ACK);
            continue;
        }
        //正确的第二次握手报文，可以发送第三次握手报文
        else if((SYN_ACK->rtp.flags == (RTP_SYN | RTP_ACK)) && (SYN_ACK->rtp.seq_num == x_seqnum + 1))
        {
            LOG_DEBUG("sender: receive 2nd handshake SYN_ACK\n");
            //第三次握手：向receiver发送一个ack
            rtp_send(sockfd, x_seqnum+1, 0, RTP_ACK, NULL, (struct sockaddr*)&raddr);
            start1 = clock();
            LOG_DEBUG("sender: 3rd handshake ACK sent\n");
            free(SYN_ACK);
            break;
        }
        else{
            LOG_FATAL("sender: Unexpected error\n");
        }
    }

    //等待一定时间确认receiver是否收到ACK，2s内收不到报文就说明建立连接
    tv.tv_sec = 2; 
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        LOG_FATAL("setsockopt() in receiver_establish_connection() failed\n");

    //但是这里还是需要设置计时器
    start1 = clock();
    while(1){
        end1 = clock();
        errno = 0;
        rtp_packet_t* tmp_pkg = rtp_receive(sockfd, (struct sockaddr*)&raddr);
        if(tmp_pkg == NULL && (errno == EAGAIN || errno == EWOULDBLOCK)){
            LOG_DEBUG("sender: connection established!\n");
            break;
        }
        else if((double)(end1 - start1) / CLOCKS_PER_SEC > 2){ //超时重传
            LOG_DEBUG("sender: normal timeout, resend 3rd handshake package\n");
            rtp_send(sockfd, x_seqnum+1, 0, RTP_ACK, NULL, (struct sockaddr*)&raddr);
            start1 = clock();
            if(tmp_pkg!=NULL) free(tmp_pkg);
            continue;
        }
        else if(tmp_pkg == NULL){
            LOG_DEBUG("sender: receive NULL SYN_ACK\n");
            continue;
        }
        else if((tmp_pkg->rtp.flags != (RTP_SYN | RTP_ACK)) || (tmp_pkg->rtp.seq_num != x_seqnum + 1))
        {
            LOG_DEBUG("sender: Unexpected package, drop it\n");
            free(tmp_pkg);
            continue;
        }
        else if((tmp_pkg->rtp.flags == (RTP_SYN | RTP_ACK)) || (tmp_pkg->rtp.seq_num == x_seqnum + 1))
        {
            LOG_DEBUG("sender: received 2rd handshake package, resend 3rd handshake package\n");
            rtp_send(sockfd, x_seqnum+1, 0, RTP_ACK, NULL, (struct sockaddr*)&raddr);
            start1 = clock();
            free(tmp_pkg);
            continue;
        }
        else{
            LOG_FATAL("sender: Unexpected error\n");
        }
    }
    //将超时重新设置为0.1，之后都以此为准
    tv.tv_sec = 0;  
    tv.tv_usec = 100000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)) < 0)
        LOG_FATAL("setsockopt() in sender_establish_connection() failed, failed to cancel recv timeout\n");
    
    LOG_DEBUG("sender: Connection established!\n");

    /************************Step2: Transfer Data************************/

    //成功连接之后，Sender发送的第一个数据报文（flag为0）的seq_num应该为x+1（假设SYN报文的编号为x）
    x_seqnum++;
    if(MODE == 0){
        sendMsgGBN(filename);
    }
    else if (MODE == 1){
        sendMsgSR(filename);
    }

    /************************Step3: Close socket************************/
    LOG_DEBUG("Sender: exiting...\n");


    //第一次挥手
    LOG_DEBUG("x_seqnum: %u\n", x_seqnum);
    rtp_send(sockfd, x_seqnum, 0, RTP_FIN, NULL, (struct sockaddr*)&raddr);
    LOG_DEBUG("sender: send FIN\n");

    //第二次挥手
    rtp_packet_t* FIN_ACK;
    start1 = clock();
    while(1){
        end1 = clock();
        errno=0;
        FIN_ACK = rtp_receive(sockfd, (struct sockaddr*)&raddr);
        if(FIN_ACK == NULL && (errno == EAGAIN || errno == EWOULDBLOCK)){
            LOG_DEBUG("sender: socket time out, resend FIN\n");
            rtp_send(sockfd, x_seqnum, 0, RTP_FIN, NULL, (struct sockaddr*)&raddr);
            start1 = clock();
            continue;
        }
        else if((double)(end1 - start1) / CLOCKS_PER_SEC > 0.1){ //超时重传
            LOG_DEBUG("sender: RESEND FIN\n");
            rtp_send(sockfd, x_seqnum, 0, RTP_FIN, NULL, (struct sockaddr*)&raddr);
            start1 = clock();
            if(FIN_ACK!=NULL) free(FIN_ACK);
            continue;
        }
        else if(FIN_ACK == NULL){
            LOG_DEBUG("sender: receive NULL FIN_ACK\n");
        }
        else if((FIN_ACK->rtp.flags != (RTP_FIN | RTP_ACK)) || (FIN_ACK->rtp.seq_num != x_seqnum))
        {
            LOG_DEBUG("sender: Unexpected package, drop it\n");
            free(FIN_ACK);
            continue;
        }
        else if(FIN_ACK->rtp.flags == (RTP_FIN | RTP_ACK) && FIN_ACK->rtp.seq_num == x_seqnum)
        {
            LOG_DEBUG("sender: receive FIN_ACK\n");
            free(FIN_ACK);
            break;
        }
        else {
            LOG_FATAL("sender: Unexpected error\n");
        }
    }
    LOG_DEBUG("Connection closed\n");
    close(sockfd);
    return 0;
}
