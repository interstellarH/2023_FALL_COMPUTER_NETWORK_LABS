#include "receiver.h"
#include <errno.h>

using namespace std;

//注意const int和static int的区别
static uint32_t WINDOW_SIZE;//窗口大小
static bool MODE; //0 for GBN, 1 for SR
static uint32_t init_x;

static int sockfd;
static struct sockaddr_in saddr;

void receiveMsgGBN(const char* filename){
    // The receiver only keeps track of the expected sequence number to receive next: nextseqnum.
    uint32_t nextseqnum = 0;

    //打开文件
    FILE *fp = fopen(filename, "wb");
    if(fp == nullptr){
        LOG_FATAL("open file failed\n");
        return;
    }

    while(1){
        rtp_packet_t* DATA = rtp_receive(sockfd, (struct sockaddr*)&saddr);
        if(DATA == NULL)
            continue;
        else{
            LOG_DEBUG("\n");
            LOG_DEBUG("DATA->rtp.seq_num: %u\n",DATA->rtp.seq_num);
            LOG_DEBUG("DATA->rtp.flags: %u\n",DATA->rtp.flags);
            LOG_DEBUG("DATA->rtp.length: %u\n",DATA->rtp.length);
            LOG_DEBUG("DATA->rtp.checksum: %u\n",DATA->rtp.checksum);
            LOG_DEBUG("\n");
            uint32_t real_seq_num = DATA->rtp.seq_num - init_x;
            LOG_DEBUG("nextseqnum: %u\n",nextseqnum);
            LOG_DEBUG("real_seq_num: %u\n",real_seq_num);
            LOG_DEBUG("\n");
            if(DATA->rtp.flags == 0 && real_seq_num == nextseqnum ){
                fwrite(DATA->payload, sizeof(char), DATA->rtp.length, fp);
                nextseqnum++;
                //发送ACK，注意ACK的seq_num为下一个期望收到的数据包的seq_num
                rtp_send(sockfd, nextseqnum + init_x, 0, RTP_ACK, NULL, (struct sockaddr*)&saddr);
                LOG_DEBUG("receiver send ACK: %u\n", nextseqnum); 
                free(DATA);  
            }
            else if(DATA->rtp.flags == 0 && real_seq_num != nextseqnum){
                LOG_DEBUG("Unexpected seq_num, drop it\n");
                rtp_send(sockfd, nextseqnum + init_x, 0, RTP_ACK, NULL, (struct sockaddr*)&saddr);
                free(DATA);
            }
            else if(DATA->rtp.flags == RTP_FIN && real_seq_num == nextseqnum){
                LOG_DEBUG("receiver收到了FIN\n");
                init_x = DATA->rtp.seq_num; //更改init_x，便于之后挥手使用
                free(DATA);
                break;//退出此函数，交由最后一部分处理
            }
            else{
                LOG_DEBUG("Unexpected packet!!!!!!!!!!\n");
                rtp_send(sockfd, nextseqnum + init_x, 0, RTP_ACK, NULL, (struct sockaddr*)&saddr);
                free(DATA);
            }
        }
    }
    fclose(fp);
}


void receiveMsgSR(char * filename){
    uint32_t nextseqnum = 0;

    //需要存储收到的报文以及窗口中每个位置的状态
    deque<rtp_packet_t*> cache(WINDOW_SIZE, nullptr);
    //打开文件
    FILE *fp = fopen(filename, "wb");
    if(fp == nullptr){
        LOG_FATAL("open file failed\n");
        return;
    }

    rtp_packet_t* DATA;
    while(1){
        DATA = rtp_receive(sockfd, (struct sockaddr*)&saddr);
        if(DATA == NULL){
            continue;
        }
        else {
            LOG_DEBUG("Receiver: receive data: seq_num = %d, length = %d\n", DATA->rtp.seq_num, DATA->rtp.length);
            uint32_t real_seq_num = DATA->rtp.seq_num - init_x;
            if((DATA->rtp.flags==0) && (real_seq_num==nextseqnum)){
                LOG_DEBUG("Moving window\n");
                cache[0]=DATA;
                while(cache.front()!=nullptr){
                    fwrite(cache[0]->payload, 1, cache[0]->rtp.length, fp);
                    free(cache[0]);//这里统一free
                    cache.pop_front();
                    cache.push_back(nullptr);
                    //注意先发送 ACK 报文，然后期望的报文序号加一；和GBN规则不同
                    rtp_send(sockfd, DATA->rtp.seq_num, 0, RTP_ACK, NULL, (struct sockaddr*)&saddr);
                    nextseqnum++;
                }
                continue;
            }
            // 在窗口内，但是不是期望的包，缓存，并发送ack
            else if((DATA->rtp.flags==0) && (real_seq_num > nextseqnum) && (real_seq_num < nextseqnum+WINDOW_SIZE)){
                cache[real_seq_num-nextseqnum] = DATA;
                rtp_send(sockfd, DATA->rtp.seq_num, 0, RTP_ACK, NULL, (struct sockaddr*)&saddr);
                //注意需要缓存的包可不能free
            }
            // 在前一个窗口内的，需要发送ack
            else if((DATA->rtp.flags==0) && (real_seq_num < nextseqnum) && (real_seq_num >= nextseqnum-WINDOW_SIZE)){
                rtp_send(sockfd, DATA->rtp.seq_num, 0, RTP_ACK, NULL, (struct sockaddr*)&saddr);
                free(DATA);
            }
            //收到FIN，且seq_num正确
            else if((DATA->rtp.flags==RTP_FIN) && (real_seq_num==nextseqnum)){
                LOG_DEBUG("receiver收到了FIN\n");
                init_x = DATA->rtp.seq_num; //更改init_x，便于之后挥手使用
                free(DATA);
                break;//退出此函数，交由最后一部分处理
            }
            //其余情况均可直接丢弃
        }
    }
    LOG_DEBUG("receiver: receive all files finished\n");
    fclose(fp);
}

int main(int argc, char **argv) {
    if (argc != 5) {
        LOG_FATAL("Usage: ./receiver [listen port] [file path] [window size] "
                  "[mode]\n");
    }

    /*Step0: parse the command*/
    int sender_port = atoi(argv[1]);
    char* filename = argv[2]; //本地需要写入的文件
    WINDOW_SIZE = atoi(argv[3]);
    MODE = atoi(argv[4]);
    LOG_DEBUG("filename: %s\n", filename); 
    LOG_DEBUG("window size: %d\n", WINDOW_SIZE);
    LOG_DEBUG("mode: %d\n", MODE);
    LOG_DEBUG("sender port: %d\n", sender_port);

    /*Step1: establish connection*/
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0){
        cout << "socket creation failed" << endl;
        return -1;
    }
    memset(&saddr, 0, sizeof(saddr));  // lstAddr is the address on which the receiver listens
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);  // receiver accepts incoming data from any address
    saddr.sin_port = htons(sender_port);  // but only accepts data coming from a certain port
    if(bind(sockfd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) { // assign the address to the listening socket
        cout << "Bind failed!" << endl;
        return -1;
    }

    // 设置接收超时，如果 5s 收不到报文就说明连接已断开，对全局传输过程均成立
    // 如果超时, recv 返回 -1
    struct timeval tv;
    tv.tv_sec = 5; // 设置超时时间为 5 秒
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        LOG_FATAL("setsockopt() in receiver_establish_connection() failed\n");

    clock_t start, end;//用于判断第二次握手是否超时
    //第一次握手，receiver接收SND_SYN
    rtp_packet_t* RCV_SYN;
    while(1){
        RCV_SYN = rtp_receive(sockfd, (sockaddr*)&saddr);
        if(RCV_SYN == NULL) continue;
        else if(RCV_SYN->rtp.flags == RTP_SYN){//若收到的包是SND_SYN则发送第二次握手报文
            LOG_DEBUG("1st handshake finished\n");
            init_x = RCV_SYN->rtp.seq_num; //记录下这个随机数
            //第二次握手，receiver发送SYN_ACK
            rtp_send(sockfd, init_x+1, 0, RTP_SYN | RTP_ACK, NULL, (sockaddr*)&saddr);
            LOG_DEBUG("2nd handshake SYN_ACK sent\n");
            start = clock();
            free(RCV_SYN);
            break;
        }
        else{
            LOG_DEBUG("Unexpected handshake packet, drop it\n");
            free(RCV_SYN);
            continue;
        }
    }
    
    //第三次握手，receiver接收ACK
    rtp_packet_t* RCV_ACK;
    while(1){
        end = clock();
        RCV_ACK = rtp_receive(sockfd, (sockaddr*)&saddr);
        if(RCV_ACK == NULL) continue;
        //第二次握手ACK超时或者收到的是第一次握手报文SYN，则重传SYN_ACK
        else if(((double)(end - start)/CLOCKS_PER_SEC > 0.1) || ((RCV_ACK->rtp.flags == RTP_SYN)&&(RCV_ACK->rtp.seq_num == init_x))){
            rtp_send(sockfd, init_x+1, 0, RTP_SYN | RTP_ACK, NULL, (sockaddr*)&saddr);
            LOG_DEBUG("RESEND 2nd handshake SYN_ACK\n");
            start = clock();
            free(RCV_ACK);
            continue;
        }
        else if(RCV_ACK->rtp.flags != RTP_ACK || RCV_ACK->rtp.seq_num != init_x+1){
            LOG_DEBUG("Unexpected handshake packet, drop it\n");
            free(RCV_ACK);
            continue;
        }
        else if(RCV_ACK->rtp.flags == RTP_ACK && RCV_ACK->rtp.seq_num == init_x+1){
            free(RCV_ACK);
            break;
        }
    }
    LOG_DEBUG("3rd handshake finished, connection established!\n");
    //树洞上有人说这里应该sleep>2s
    // sleep(2);
    /************************Step2: Transfer Data************************/

    init_x++;
    if(MODE == 0){
        receiveMsgGBN(filename);
    }
    else if (MODE == 1){
        receiveMsgSR(filename);
    }

    /************************Step3: Close socket************************/

    LOG_DEBUG("Receiver: exiting...\n");
    //第一次挥手，receiver接收FIN，已经在上面的receive中收到了
    //第二次挥手，receiver发送FIN_ACK,并等待一定时间确认sender是否收到FIN_ACK
    rtp_send(sockfd, init_x, 0, RTP_FIN | RTP_ACK, NULL, (sockaddr*)&saddr);
    rtp_packet_t* RCV_FIN;
    while(1){
        errno=0;
        RCV_FIN = rtp_receive(sockfd, (struct sockaddr*)&saddr);
        if(RCV_FIN == NULL && (errno == EAGAIN || errno == EWOULDBLOCK)) //若超时则说明连接已经断开，退出循环
            break;
        else if(RCV_FIN == NULL)
            continue;   
        else if(RCV_FIN->rtp.flags == RTP_FIN && RCV_FIN->rtp.seq_num == init_x){
            LOG_DEBUG("sender没有收到FIN_ACK,重传\n");
            rtp_send(sockfd, init_x, 0, RTP_FIN | RTP_ACK, NULL, (sockaddr*)&saddr);
            free(RCV_FIN);
            continue;   
        }
        else{
            LOG_DEBUG("Unexpected packet, drop it\n");
            free(RCV_FIN);
            continue;
        }
    }
    LOG_DEBUG("Connection closed\n");
    close(sockfd);
    return 0;
}
