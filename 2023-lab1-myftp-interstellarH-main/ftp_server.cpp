#include <iostream>
#include <cstring>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <cstddef>
using namespace std;

#define MAX_BUFFER_SIZE 2048
#define MAX_FILE_SIZE 4096
#define MAGIC_NUMBER_LENGTH 6

struct myFTPHeader{
    char m_protocol[MAGIC_NUMBER_LENGTH]; /* protocol magic number (6 bytes) */
    byte m_type;                          /* type (1 byte) */
    byte m_status;                        /* status (1 byte) */
    uint32_t m_length;                    /* length (4 bytes) in Big endian*/
} __attribute__ ((packed));


char send_buffer[MAX_BUFFER_SIZE];
int state;//0表示无连接，1表示有连接

char prot[6] = {'\xc1','\xa1','\x10','f','t','p'};

int protocol_check(char* arr)
{
    return (arr[0]=='\xc1' && arr[1]=='\xa1' && arr[2]=='\x10' && arr[3]=='f' && arr[4]=='t' && arr[5]=='p');
}

void svOpen(int sock_fd){
    myFTPHeader send_header = {
        .m_protocol = {'\xc1','\xa1','\x10','f','t','p'},
        .m_type = (byte)0xA2,
        .m_status = (byte)1,
        .m_length = htonl(12),
    };
    int bytes_sent = 0;
    while(bytes_sent < 12){
        int sent = send(sock_fd, &send_header + bytes_sent, 12 - bytes_sent, 0);
        if(sent < 0){
            cout << "Send open_reply failed" << endl;
            return;
        }
        bytes_sent += sent;
    }
    state = 1; //表示连接建立
    cout << "Send open_reply successfully!" << endl;
    return;
}

void svLs(int sock_fd){
    //利用popen获取file list
    FILE* fp = popen("ls", "r");//注意popen没有rb模式
    cout<<"popen"<<endl;
    if(fp == nullptr){
        cout << "popen failed" << endl;
        return;
    }
    char file_ls[2050] = {0};
    int ls_len = fread(file_ls, 1, sizeof(file_ls), fp);
    if(ls_len < 0){
        cout << "Read file list failed" << endl;
        return;
    }
    file_ls[ls_len] = '\0';//结尾添加终止符
    pclose(fp);

    //发送file list
    myFTPHeader send_header = {
        .m_protocol = {'\xc1','\xa1','\x10','f','t','p'},
        .m_type = (byte)0xA4,
        .m_length = htonl(12 + ls_len + 1),
    };

    memset(send_buffer, 0, sizeof(send_buffer));
    memcpy(send_buffer, &send_header, sizeof(myFTPHeader));
    memcpy(send_buffer + 12, file_ls, ls_len + 1);
    int bytes_sent = 0;
    int total_len = 12 + ls_len + 1;
    while(bytes_sent < total_len){
        int sent = send(sock_fd, send_buffer + bytes_sent, total_len - bytes_sent, 0);
        if(sent < 0){
            cout << "Send file list failed" << endl;
            return;
        }
        bytes_sent += sent;
    }
    cout << "Send file list successfully!" << endl;
    return;
}

void svGet(int sock_fd, char* filename){
    myFTPHeader send_header = {
        .m_protocol = {'\xc1','\xa1','\x10','f','t','p'},
        .m_type = (byte)0xA6,
        .m_length = htonl(12),
    };
    // server查看文件是否可用
    if(access(filename, F_OK) == -1){
        cout<<"File not found!"<<endl;
        send_header.m_status = (byte)0;
        int bytes_sent = 0;
        while(bytes_sent < 12){
            int sent = send(sock_fd, &send_header + bytes_sent, 12 - bytes_sent, 0);
            if(sent < 0){
                cout << "Send get_reply failed" << endl;
                return;
            }
            bytes_sent += sent;
        }
        cout << "Send get_reply successfully!" << endl;
        return;
    }
    else{
        cout<<"File found!"<<endl;
        send_header.m_status = (byte)1;
        int bytes_sent = 0;
        while(bytes_sent < 12){
            int sent = send(sock_fd, &send_header + bytes_sent, 12 - bytes_sent, 0);
            if(sent < 0){
                cout << "Send get_reply failed" << endl;
                return;
            }
            bytes_sent += sent;
        }
        cout << "Send get_reply successfully!" << endl;
        
        //读取本地文件
        FILE* fp = fopen(filename, "rb");
        if(fp == nullptr){
            cout << "Failed to open file!" << endl;
            return;
        }
        //获取文件大小
        fseek(fp, 0, SEEK_END);
        int file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        //发送file data header
        myFTPHeader file_header = {
            .m_protocol = {'\xc1','\xa1','\x10','f','t','p'},
            .m_type = (byte)0xFF,
            .m_length = htonl(12 + file_size),
        };
        bytes_sent = 0;
        while(bytes_sent < 12){
            int sent = send(sock_fd, &file_header + bytes_sent, 12 - bytes_sent, 0);
            if(sent < 0){
                cout << "Send file data header failed" << endl;
                return;
            }
            bytes_sent += sent;
        }
        cout << "Send file data header successfully!" << endl;

        //发送file data
        //先将本地文件内容读至file_buffer
        char file_buffer[file_size] = {0};
        if(fread(file_buffer, 1, file_size, fp) != file_size){
            cout << "Read file to buffer failed" << endl;
            return;
        }
        fclose(fp);

        //发送file_buffer
        bytes_sent = 0;
        while(bytes_sent < file_size){
            int sent = send(sock_fd, file_buffer + bytes_sent, file_size - bytes_sent, 0);
            if(sent < 0){
                cout << "Send file data failed" << endl;
                return;
            }
            bytes_sent += sent;
            cout << "Send progress: " << bytes_sent/file_size*100 << "%"<< endl;
        }
        cout << "Send file to client successfully!" << endl;
        
    }
    return;

}

void svPut(int sock_fd, char* filename){
    myFTPHeader send_header = {
        .m_protocol = {'\xc1','\xa1','\x10','f','t','p'},
        .m_type = (byte)0xA8,
        .m_length = htonl(12),
    };
    int bytes_sent = 0;
    while(bytes_sent < 12){
        int sent = send(sock_fd, &send_header + bytes_sent, 12 - bytes_sent, 0);
        if(sent < 0){
            cout << "Send put_reply failed" << endl;
            return;
        }
        bytes_sent += sent;
    }
    cout << "Send put_reply successfully!" << endl;

    //接收file data header
    myFTPHeader recv_header;
    int bytes_received = 0;
    while(bytes_received < 12){
        int ret = recv(sock_fd, &recv_header + bytes_received, 12 - bytes_received, 0);
        if(ret < 0){
            cout << "Receive file data header failed" << endl;
            return;
        }
        bytes_received += ret;
    }
    //判断file data header的合法性
    if(!protocol_check(recv_header.m_protocol)){
        cout << "Protocol check failed!" << endl;
        return;
    }
    if(recv_header.m_type != (byte)0xFF){
        cout << "Wrong type of file header!" << endl;
        return;
    }

    //接收file data
    int file_size = ntohl(recv_header.m_length) - 12;
    char file_buffer[file_size] = {0};
    bytes_received = 0;
    while(bytes_received < file_size){
        int ret = recv(sock_fd, file_buffer + bytes_received, file_size - bytes_received, 0);
        if(ret < 0){
            cout << "Receive file data failed" << endl;
            return;
        }
        bytes_received += ret;
    }

    //将file_buffer写入本地文件
    FILE* fp = fopen(filename, "wb");
    if(fp == nullptr){
        cout<< "Failed to create file!" <<endl;
        return;
    }

    int bytes_written = 0;
    while(bytes_written < file_size){
        int ret = fwrite(file_buffer + bytes_written, 1, file_size - bytes_written, fp);
        if(ret < 0){
            cout << "Write file failed" << endl;
            return;
        }
        bytes_written += ret;
    }
    cout << "Client put file successfully!" << endl;

    fclose(fp);
    return;
}

void svSha(int sock_fd, char* filename){
    myFTPHeader send_header = {
        .m_protocol = {'\xc1','\xa1','\x10','f','t','p'},
        .m_type = (byte)0xAA,
        .m_length = htonl(12),
    };

    if(access(filename, F_OK) == -1){
        cout<<"File not found!"<<endl;
        send_header.m_status = (byte)0;
        int bytes_sent = 0;
        while(bytes_sent < 12){
            int sent = send(sock_fd, &send_header + bytes_sent, 12 - bytes_sent, 0);
            if(sent < 0){
                cout << "Send sha_reply failed" << endl;
                return;
            }
            bytes_sent += sent;
        }
        cout << "Send sha_reply successfully!" << endl;
        return;
    }
    else{
        cout<<"File found!"<<endl;
        send_header.m_status = (byte)1;
        int bytes_sent = 0;
        while(bytes_sent < 12){
            int sent = send(sock_fd, &send_header + bytes_sent, 12 - bytes_sent, 0);
            if(sent < 0){
                cout << "Send sha_reply failed" << endl;
                return;
            }
            bytes_sent += sent;
        }
        cout << "Send sha_reply successfully!" << endl;
        
        //获得本地文件的sha256
        string cmd = "sha256sum " + string(filename);
        FILE* fp = popen(cmd.c_str(), "r");
        if(fp == nullptr){
            cout << "popen failed" << endl;
            return;
        }
        //获得sha256的大小 以下方法不适用于popen，因为打开的是管道而非文件
        // fseek(fp, 0, SEEK_END);
        // int sha_len = ftell(fp);
        // fseek(fp, 0, SEEK_SET);

        char sha256[MAX_BUFFER_SIZE] = {0};
        int sha_len = fread(sha256, 1, MAX_BUFFER_SIZE, fp);
        // cout<<sha_len<<endl;
        if(sha_len<0){
            cout << "Read sha256 failed" << endl;
            return;
        }
        sha256[sha_len] = '\0';//结尾添加终止符
        pclose(fp);     
        // cout<< sha256 <<endl;
        //发送sha256
        myFTPHeader sha_header = {
            .m_protocol = {'\xc1','\xa1','\x10','f','t','p'},
            .m_type = (byte)0xFF,
            .m_length = htonl(12 + sha_len + 1),
        };
        
        memset(send_buffer, 0, sizeof(send_buffer));
        memcpy(send_buffer, &sha_header, sizeof(myFTPHeader));
        memcpy(send_buffer + 12, sha256, sha_len + 1);
        bytes_sent = 0;
        int total_len = 12 + sha_len + 1;
        while(bytes_sent < total_len){
            int sent = send(sock_fd, send_buffer + bytes_sent, total_len - bytes_sent, 0);
            if(sent < 0){
                cout << "Send sha256 failed" << endl;
                return;
            }
            bytes_sent += sent;
        }
        cout << "Send sha256 successfully!" << endl;
        
    }
    return;

}

void svQuit(int sock_fd){
    myFTPHeader send_header = {
        .m_protocol = {'\xc1','\xa1','\x10','f','t','p'},
        .m_type = (byte)0xAC,
        .m_length = htonl(12),
    };
    int bytes_sent = 0;
    while(bytes_sent < 12){
        int sent = send(sock_fd, &send_header + bytes_sent, 12 - bytes_sent, 0);
        if(sent < 0){
            cout << "Send quit_reply failed" << endl;
            return;
        }
        bytes_sent += sent;
    }
    if(close(sock_fd) < 0){
        cout << "Close socket failed" << endl;
        return;
    }
    state = 0; //表示连接断开
    return;
}

int main(int argc, char ** argv)
{
    //server端创建socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket == -1){
        cout << "socket creation failed" << endl;
        return -1;
    }

    //server端绑定socket
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);
    
    if(bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        cout << "Bind failed!" << endl;
        return -1;
    }
    //server端监听socket
    if(listen(server_socket, 10) < 0){ //但是本lab中只要做到一个连接即可
        cout << "Listen failed!" << endl;
        return -1;
    }
    //server端接受socket
    int connfd;
    while(1){
        if(state == 0){
            cout << "Waiting for connection..." << endl;
            if((connfd = accept(server_socket, nullptr, nullptr)) < 0){
                cout << "Accept failed!" << endl;
                return -1;
            }
        }
        else{
            cout << "Connected! Waiting for request..." << endl;
        }

        //server端接收client端的request_header
        myFTPHeader* recv_header = (myFTPHeader*)malloc(12);
        int bytes_received = 0;
        while(bytes_received < 12){
            int ret = recv(connfd, recv_header + bytes_received, 12 - bytes_received, 0);
            if(ret < 0){
                cout << "Receive request_header failed" << endl;
                free(recv_header);
                return -1;
            }
            bytes_received += ret;
        }

        //判断request的合法性
        if(!protocol_check(recv_header->m_protocol)){
            cout << "Protocol check failed!" << endl;
            return -1;
        }

        //接收可能存在的filename
        char* file_name = nullptr;
        int filename_len = ntohl(recv_header->m_length) - 12;//这里是否需要再-1
        if(filename_len != 0){
            file_name = (char*)malloc(filename_len); //只需要在使用file_name的函数后free
            bytes_received = 0;
            while(bytes_received < filename_len){
                int ret = recv(connfd, file_name + bytes_received, filename_len - bytes_received, 0);
                if(ret < 0){
                    cout << "Receive file_name failed" << endl;
                    free(file_name);
                    return -1;
                }
                bytes_received += ret;
            }
            cout<<file_name<<endl;
        }
        
        //判断request的type
        switch(recv_header->m_type)
        {
        case (byte)0xA1: //open
            cout << "client open connection" << endl;
            svOpen(connfd);
            break;
        case (byte)0xA3: //ls
            cout<<"client get file list from server"<<endl;
            svLs(connfd);
            break;

        case (byte)0xA5: //get
            cout<<"client get file from server"<<endl;
            svGet(connfd, file_name);
            free(file_name);
            break;

        case (byte)0xA7: //put
            cout<<"client put file to server"<<endl;
            svPut(connfd, file_name);
            free(file_name);
            break;
        
        case (byte)0xA9: //sha
            cout<<"client check sha256 of file"<<endl;
            svSha(connfd, file_name);
            free(file_name);
            break;
        case (byte)0xAB: //quit
            cout<<"client quit"<<endl;
            svQuit(connfd);        
            break;
        default:
            cout << "Illegal command!" << endl;
            break;
        }

    }
    close(connfd);
    close(server_socket);
    return 0;
}