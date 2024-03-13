#include "util.h"
#include "rtp.h"
#include <sys/socket.h> // Include the header file for struct sockaddr
#include <netinet/in.h> // Include the header file for struct sockaddr_in
#include <arpa/inet.h>  // Include the header file for inet_pton
#include <string.h>
#include <time.h>

static uint32_t crc32_for_byte(uint32_t r) {
    for (int j = 0; j < 8; ++j) 
        r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
    return r ^ (uint32_t)0xFF000000L;
}

static void crc32(const void* data, size_t n_bytes, uint32_t* crc) {
    static uint32_t table[0x100];
    if (!*table)
        for (size_t i = 0; i < 0x100; ++i) 
            table[i] = crc32_for_byte(i);
    for (size_t i = 0; i < n_bytes; ++i)
        *crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
}

// Computes checksum for `n_bytes` of data
//
// Hint 1: Before computing the checksum, you should set everything up
// and set the "checksum" field to 0. And when checking if a packet
// has the correct check sum, don't forget to set the "checksum" field
// back to 0 before invoking this function.
//
// Hint 2: `len + sizeof(rtp_header_t)` is the real length of a rtp
// data packet.
uint32_t compute_checksum(const void* pkt, size_t n_bytes) {
    uint32_t crc = 0;
    crc32(pkt, n_bytes, &crc);
    return crc;
}

/*
* @brief: check whether the checksum and flag of package are correct
* @return: 0 if not, 1 if yes
*/
int check_checksum(rtp_packet_t pkg){ 
    uint32_t checksum = pkg.rtp.checksum;
    pkg.rtp.checksum = 0;
    uint32_t temp = compute_checksum(&pkg, 11+pkg.rtp.length);
    if(checksum != temp){
        LOG_DEBUG("checksum is not correct, wrong checksum=%u, right checksum=%u\n", checksum, temp);
        return 0;
    }
    LOG_DEBUG("check_pkg success\n");
    return 1;
}   

/*
* @brief: send a rtp package
*/
void rtp_send(int sockfd, uint32_t seq_num, uint16_t length, uint8_t flag, const char* payload,struct sockaddr* addr)
{
    rtp_packet_t pkg;
    pkg.rtp.seq_num = seq_num;
    pkg.rtp.length = length;
    pkg.rtp.flags = flag;
    pkg.rtp.checksum = 0;
    if(length)
        memcpy(pkg.payload, payload, length);
    pkg.rtp.checksum = compute_checksum(&pkg, 11 + length);
    ssize_t size = sendto(sockfd, &pkg, 11 + length, 0, addr, sizeof(*addr));
    if (size == -1)
        LOG_FATAL("rtp_send failed, sent = %d\n, rtp_send time out\n", (int)size);
    else if (size == 0)
        LOG_FATAL("rtp_send failed, sent = %d\n, connection closed by peer\n", (int)size);
    else if (size != 11 + length) // 当我们通过UDP发出的包总是符合MTU的限制时，我们的包一定可以一次性发出，而不会被分片
        LOG_FATAL("rtp_send failed, sent = %d\n, rtp_send not complete\n", (int)size);
    else{
        LOG_DEBUG("rtp_send success, seqnum = %u, payload length = %d, flags = %d, checksum = %u\n", pkg.rtp.seq_num, pkg.rtp.length, pkg.rtp.flags, pkg.rtp.checksum);
        LOG_DEBUG("rtp_send success\n");
    }
    return;
}


rtp_packet_t* rtp_receive(int sockfd,struct sockaddr* addr)
{
    socklen_t addr_len = sizeof(*addr);
    void* tmp_pkg = malloc(sizeof(rtp_header_t) + PAYLOAD_MAX);
    int size = recvfrom(sockfd, tmp_pkg, 11+PAYLOAD_MAX, 0, (struct sockaddr*)addr, &addr_len);
    rtp_packet_t* pkg = (rtp_packet_t*)tmp_pkg;
    uint32_t checksum;  
    if(size == -1){
        free(pkg);
        LOG_DEBUG("rtp_receiver time out. Exit from rtp_receive\n");
        return NULL;
    }
    if(size == 0){
        free(pkg);
        LOG_DEBUG("rtp_receive failed, received = %d, connection closed by sender.\n", (int)size);
        return NULL;
    }
    else {
        if(pkg->rtp.length<0 || pkg->rtp.length>PAYLOAD_MAX){
            LOG_DEBUG("rtp_receive failed, payload length is not correct.\n");
            return NULL;
        }
        else if(check_checksum(*pkg)==0){
            LOG_DEBUG("rtp_receive failed, checksum is not correct.\n");
            return NULL;
        }
        else if(pkg->rtp.flags & 0b11111000) //对flag进行初查
        {
            return NULL;
        }
    }
    LOG_DEBUG("rtp_receive success: seq_num = %u, payload length = %d, flags = %d, checksum = %u\n", pkg->rtp.seq_num, pkg->rtp.length, pkg->rtp.flags, pkg->rtp.checksum);
    return pkg;
}

// rtp_packet_t *rtp_receive(int sockfd, struct sockaddr *from)
// {
//     LOG_DEBUG("into rtp_recvfrom()\n");
//     socklen_t fromlen = sizeof(*from);
//     void *tmp_packet = malloc(sizeof(rtp_header_t) + PAYLOAD_MAX);
//     int received = recvfrom(sockfd, tmp_packet, sizeof(rtp_header_t) + PAYLOAD_MAX, 0, from, &fromlen);
//     rtp_packet_t *received_packet = (rtp_packet_t *)tmp_packet;

//     uint32_t checksum;
//     if (received == -1)
//     {
//         free(received_packet);
//         // if (exit_when_time_out)
//         //     LOG_SYS_FATAL("    recvfrom() in rtp_recvfrom() failed, received = %d, recvfrom() time out\n", received);
//         // else
//         // {
//             LOG_DEBUG("    recvfrom() in rtp_recvfrom() failed, received = %d, recvfrom() time out\n", received);
//             LOG_DEBUG("exit rtp_recvfrom()\n");
//             return NULL;
//         // }
//     }
//     else if (received == 0)
//     {
//         free(received_packet);
//         LOG_FATAL("    recvfrom() in rtp_recvfrom() failed, received = %d, connection closed by peer\n", received);
//     }
//     else
//     {
//         checksum = received_packet->rtp.checksum;
//         received_packet->rtp.checksum = 0;
//         if (checksum != compute_checksum((void *)received_packet, received)) // 如果checksum不对，那么就丢弃这个包
//         {
//             free(received_packet);
//             LOG_DEBUG("    rtp_recvfrom() failed, wrong checksum = %d\n, real checksum = %d\n", checksum, compute_checksum((void *)received_packet, received));
//             LOG_DEBUG("exit rtp_recvfrom()\n");
//             return NULL;
//         }
//         else if (received_packet->rtp.flags & 0b11111000) // 如果flags不对，那么就丢弃这个包
//         {
//             free(received_packet);
//             LOG_DEBUG("    rtp_recvfrom() failed, wrong flags = %d\n", received_packet->rtp.flags);
//             LOG_DEBUG("exit rtp_recvfrom()\n");
//             return NULL;
//         }
//     }

//     LOG_DEBUG("    rtp_recvfrom() seq_num = %X, payload length = %d, flags = %d, checksum = %d\n", received_packet->rtp.seq_num, received_packet->rtp.length, received_packet->rtp.flags, checksum);
//     LOG_DEBUG("    rtp_recvfrom() already received %d bytes, in hex:\n", received);
//     received_packet->rtp.checksum = checksum;
//     LOG_DEBUG("exit rtp_recvfrom()\n");

//     return received_packet;
// }