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
#include <sstream>
#include <vector>

using namespace std;

#define MAX_BUFFER_SIZE 2048
#define MAX_FILE_SIZE 4096
#define MAGIC_NUMBER_LENGTH 6
#define MAX_ARGC 3

int str2type(string str){
    if(str == "open") return 1;
    else if(str == "ls") return 2;
    else if(str == "get") return 3;
    else if(str == "put") return 4;
    else if(str == "sha256") return 5;
    else if(str == "quit") return 6;
    else return 0;
}   

char send_buffer[MAX_FILE_SIZE];

struct sockaddr_in servaddr;
int client_socket;
int close_client=0; //判断是否关闭客户端
string state = "no connection"; //储存当前状态，是否连接到端口
//state也是很多操作第一个判断的条件，比如ls，get，put，close等等

struct myFTPHeader{
    char m_protocol[MAGIC_NUMBER_LENGTH]; /* protocol magic number (6 bytes) */
    byte m_type;                          /* type (1 byte) */
    byte m_status;                        /* status (1 byte) */
    uint32_t m_length;                    /* length (4 bytes) in Big endian*/
} __attribute__ ((packed));

char prot[6] = {'\xc1','\xa1','\x10','f','t','p'};

int protocol_check(char* arr)
{
    return (arr[0]=='\xc1' && arr[1]=='\xa1' && arr[2]=='\x10' && arr[3]=='f' && arr[4]=='t' && arr[5]=='p');
}

//open a connection to the server
void myOpen(const char* ip, const char* port){

    if(state == "connected"){
        cout<<"You have already established a connection!"<<endl;
        return;
    }

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(client_socket < 0){
        cout<<"Socket creation failed!"<<endl;
        return;
    }
    uint16_t PORT = atoi(port);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT); //htons仅适用于16位整数，注意搭配
    // inet_pton()将点分十进制ip地址转换为网络字节序的二进制整数
    if(inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0){
        cout<<"Invalid address!"<<endl;
        return;
    }

    if(connect(client_socket, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0){
        cout<<"Connection failed!"<<endl;
        return;
    }

    myFTPHeader open_request, open_reply;
    memcpy(open_request.m_protocol, prot, 6);
    open_request.m_type = (byte)0xA1;
    open_request.m_length = htonl(12);

    memset(send_buffer, 0, sizeof(send_buffer));
    memcpy(send_buffer, &open_request, sizeof(myFTPHeader));

    int bytes_sent = 0;
    while(bytes_sent < 12){
        int sent = send(client_socket, send_buffer + bytes_sent, 12 - bytes_sent, 0);
        if(sent < 0){
            cout<<"Send open_request failed!"<<endl;
            return;
        }
        if(sent == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_sent += sent;
    }

    int bytes_received = 0;
    while(bytes_received < 12){
        int received = recv(client_socket, &open_reply + bytes_received, 12 - bytes_received, 0);
        if(received < 0){
            cout<<"Receive open_reply failed!"<<endl;
            return;
        }
        if(received == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_received += received;
    }

    //open_reply合法性检查
    if(!protocol_check(open_reply.m_protocol)){
        cout<<"Protocol check failed!"<<endl;
        return;
    }

    if(open_reply.m_type != (byte)0xA2){
        cout<<"Wrong type of reply, connection failed!"<<endl;
        return;
    }

    if(open_reply.m_status == (byte)0x1){
        cout<<"You have established a connection to "<<ip<<":"<<port<<endl;
        string s_ip(ip);
        string s_port(port);
        state = s_ip + ":" + s_port; //state显示当前ip与port，在quit()里面改回no connection
        return;
    }
    else{
        cout<<"Wrong status of reply, connection failed!"<<endl;
        return;
    }

}

// get the file list from the server
void myLs(){

    if(state == "no connection"){
        cout<<"You have not established a connection!"<<endl;
        return;
    }

    myFTPHeader ls_request, ls_reply;
    memcpy(ls_request.m_protocol, prot, 6);
    ls_request.m_type = (byte)0xA3;
    ls_request.m_length = htonl(12);

    memset(send_buffer, 0, sizeof(send_buffer));
    memcpy(send_buffer, &ls_request, sizeof(myFTPHeader));

    int bytes_sent = 0;
    while(bytes_sent < 12){
        int sent = send(client_socket, send_buffer + bytes_sent, 12 - bytes_sent, 0);
        if(sent < 0){
            cout<<"Send ls_request failed!"<<endl;
            return;
        }
        if(sent == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_sent += sent;
    }

    //接收ls_reply
    int bytes_received = 0;
    while(bytes_received < 12){
        int received = recv(client_socket, &ls_reply + bytes_received, 12 - bytes_received, 0);
        if(received < 0){
            cout<<"Receive ls_reply failed!"<<endl;
            return;
        }
        if(received == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_received += received;
    }

    if(!protocol_check(ls_reply.m_protocol)){
        cout<<"Protocol check failed!"<<endl;
        return;
    }
    //获取ls大小，用于动态分配数组
    int ls_size = ntohl(ls_reply.m_length) - 12;

    if(ls_reply.m_type == (byte)0xA4){
        //已知返回的结果的总长度不会超过 2047 个字符
        char file_list[ls_size]={0};
        int already_received = 0;
        while(already_received < ls_size){
            int received = recv(client_socket, file_list + already_received, ls_size - already_received, 0);
            if(received < 0){
                cout<<"Receive file list failed!"<<endl;
                return;
            }
            if(received == 0){
                cout << "Server closed connection!" << endl;
                return;
            }
            already_received += received;
        }
        cout<<"------file list starts------"<<endl;
        //打印文件列表
        cout<<file_list<<endl;
        cout<<"------file list ends------"<<endl;
        return;
    }
    else{
        cout<<"Wrong type of reply, ls failed!"<<endl;
        return;
    }
    return;
}

// get certain file from the server
// 确保 ASCII 和二进制文件都被支持 都使用二进制模式读写
void myGet(const char* filename){

    if(state == "no connection"){
        cout<<"You have not established a connection!"<<endl;
        return;
    }
    myFTPHeader get_request, get_reply;
    memcpy(get_request.m_protocol, prot, 6);
    get_request.m_type = (byte)0xA5;
    get_request.m_length = htonl(12+strlen(filename)+1);

    //request包含header和filename
    memset(send_buffer, 0, sizeof(send_buffer));
    memcpy(send_buffer, &get_request, sizeof(myFTPHeader));
    memcpy(send_buffer + 12, filename, strlen(filename)+1);
    int bytes_sent = 0;
    int expect_sent = 12+strlen(filename)+1;
    while(bytes_sent < expect_sent){
        int sent = send(client_socket, send_buffer + bytes_sent, expect_sent - bytes_sent, 0);
        if(sent < 0){
            cout<<"Send get_request failed!"<<endl;
            return;
        }
        if(sent == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_sent += sent;
    }

    //接收get_reply
    int bytes_received = 0;
    while(bytes_received < 12){
        int received = recv(client_socket, &get_reply + bytes_received, 12 - bytes_received, 0);
        if(received < 0){
            cout<<"Receive get_reply failed!"<<endl;
            return;
        }
        if(received == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_received += received;
    }

    //检查get_reply合法性
    if(!protocol_check(get_reply.m_protocol)){
        cout<<"Protocol check failed!"<<endl;
        return;
    }
    if(get_reply.m_type != (byte)0xA6){
        cout<<"Wrong type of reply, get failed!"<<endl;
        return;
    }

    if(get_reply.m_status == (byte)0x0){
        cout<<"File not found!"<<endl;
        return;
    }
    else if(get_reply.m_status == (byte)0x1){
        cout<<"File found!"<<endl;
        //接收文件头
        myFTPHeader file_header;
        int already_received = 0;
        while(already_received < 12){
            int received = recv(client_socket, &file_header + already_received, 12 - already_received, 0);
            if(received < 0){
                cout<<"Receive file header failed!"<<endl;
                return;
            }
            if(received == 0){
                cout << "Server closed connection!" << endl;
                return;
            }
            already_received += received;
        }
        //protocol check
        if(!protocol_check(file_header.m_protocol)){
            cout<<"Protocol check failed!"<<endl;
            return;
        }
        if(file_header.m_type != (byte)0xFF){
            cout<<"Wrong type of file header!"<<endl;
            return;
        }
        
        //获取文件大小
        int file_size = ntohl(file_header.m_length) - 12;
        // cout<<file_size<<endl;
        
        //接受文件至file_data
        char file_data[file_size]={0};//这里由于file_size可能超过MAX_FILE_SIZE所以必须动态分配
        int file_received = 0;//记录已经接收的字节数
        while(file_received < file_size){
            int received = recv(client_socket, file_data + file_received, file_size - file_received, 0);
            if(received < 0){
                cout<<"Receive file failed!"<<endl;
                return;
            }
            if(received == 0){
                cout << "Server closed connection!" << endl;
                return;
            }
            file_received += received;
            cout << "Download file progress: " << file_received / file_size *100 << "%" << endl;
        }

        //本地创建文件（以二进制方式）
        FILE* fp = fopen(filename, "wb");
        if(fp == nullptr){
            cout<<"Create local file failed!"<<endl;
            return;
        } 

        // 写入本地文件
        int file_written = 0;
        while(file_written < file_size){
            int written = fwrite(file_data + file_written, 1, file_size - file_written, fp);
            if(written < 0){
                cout<<"Write to local file failed!"<<endl;
                return;
            }
            file_written += written;
        }
        cout<<"Get file successfully!"<<endl;
        fclose(fp); //和fopen配套使用
        }
    return;
}

// upload certain file to the server
void myPut(const char* filename){

    if(state == "no connection"){
        cout<<"You have not established a connection!"<<endl;
        return;
    }

    //先判断文件是否存在，使用access函数
    if(access(filename, F_OK) == -1){
        cout<<"File not found!"<<endl;
        return;
    }

    //若存在，发送put_request
    myFTPHeader put_request, put_reply;
    memcpy(put_request.m_protocol, prot, 6);
    put_request.m_type = (byte)0xA7;
    put_request.m_length = htonl(12+strlen(filename)+1);

    memset(send_buffer, 0, sizeof(send_buffer));
    memcpy(send_buffer, &put_request, sizeof(put_request));
    memcpy(send_buffer + 12, filename, strlen(filename)+1);
    int bytes_sent = 0;
    int expect_sent = 12+strlen(filename)+1;
    while(bytes_sent < expect_sent){
        int sent = send(client_socket, send_buffer + bytes_sent, expect_sent - bytes_sent, 0);
        if(sent < 0){
            cout<<"Send put_request failed!"<<endl;
            return;
        }
        if(sent == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_sent += sent;
    }

    //接收put_reply
    int bytes_received = 0;
    while(bytes_received < 12){
        int received = recv(client_socket, &put_reply + bytes_received, 12 - bytes_received, 0);
        if(received < 0){
            cout<<"Receive put_reply failed!"<<endl;
            return;
        }
        if(received == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_received += received;
    }

    //检查put_reply合法性
    if(!protocol_check(put_reply.m_protocol)){
        cout<<"Protocol check failed!"<<endl;
        return;
    }

    if(put_reply.m_type != (byte)0xA8){
        cout<<"Wrong type of reply, put failed!"<<endl;
        return;
    }

    // 打开文件并获取文件大小
    FILE* fp = fopen(filename, "rb");
    if(fp == nullptr){
        cout<<"File open failed!"<<endl;
        return;
    } 
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    //Client向server发送文件
    //先发送文件头
    myFTPHeader file_header;
    memcpy(file_header.m_protocol, prot, 6);
    file_header.m_type = (byte)0xFF;
    file_header.m_length = htonl(12+file_size);

    int already_sent = 0;
    while(already_sent < 12){
        int sent = send(client_socket, &file_header + already_sent, 12 - already_sent, 0);
        if(sent < 0){
            cout<<"Send file header failed!"<<endl;
            return;
        }
        if(sent == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        already_sent += sent;
    }

    //根据file_size动态分配file_buffer接收文件体
    // file_data = (myFTPHeader*)realloc(file_data, file_size);
    char file_buffer[file_size] = {0};
    if(fread(file_buffer, 1, file_size, fp) != file_size){
        cout<<"Read file data failed!"<<endl;
        // free(file_data);
        return;
    }

    //将file_data发送到server
    int file_sent = 0;
    while(file_sent < file_size){
        int sent = send(client_socket, file_buffer + file_sent, file_size - file_sent, 0);
        if(sent < 0){
            cout<<"Send file failed!"<<endl;
            // free(file_data);
            return;
        }
        if(sent == 0){
            cout << "Server closed connection!" << endl;
            // free(file_data);
            return;
        }
        file_sent += sent;
        cout << "Upload progress: " << file_sent / file_size *100 << "%" << endl;
    }
    cout << "Client put file to server successfully!" << endl;
    fclose(fp);
    // free(file_data);
    return;
}

// Calculate the checksum
void mySha(const char* filename){

    if(state == "no connection"){
        cout<<"You have not established a connection!"<<endl;
        return;
    }

    myFTPHeader sha_request, sha_reply, sha_data;
    memcpy(sha_request.m_protocol, prot, 6);
    sha_request.m_type = (byte)0xA9;
    sha_request.m_length = htonl(12+strlen(filename)+1);

    memset(send_buffer, 0, sizeof(send_buffer));
    memcpy(send_buffer, &sha_request, sizeof(sha_request));
    memcpy(send_buffer + 12, filename, strlen(filename)+1);
    int bytes_sent = 0;
    int expect_sent = 12+strlen(filename)+1;
    while(bytes_sent < expect_sent){
        int sent = send(client_socket, send_buffer + bytes_sent, expect_sent - bytes_sent, 0);
        if(sent < 0){
            cout<<"Send sha_request failed!"<<endl;
            return;
        }
        if(sent == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_sent += sent;
    }

    //这里出现了阻塞情况，发现是由于send的时候没有发送完全，字节数写错了，在get和put里面也有同样的情况
    //接收sha_reply
    int bytes_received = 0;
    int expect_received = sizeof(myFTPHeader);
    while(bytes_received < expect_received){
        int received = recv(client_socket, &sha_reply + bytes_received, expect_received - bytes_received, 0);
        if(received < 0){
            cout<<"Receive sha_reply failed!"<<endl;
            return;
        }
        if(received == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_received += received;
    }
    
    //检查sha_reply合法性
    if(!protocol_check(sha_reply.m_protocol)){
        cout<<"Protocol check failed!"<<endl;
        return;
    }
    if(sha_reply.m_type != (byte)0xAA){
        cout<<"Wrong type of reply, sha failed!"<<endl;
        return;
    }

    if(sha_reply.m_status == (byte)0){
        cout<<"File not found!"<<endl;
        return;
    }
    else if(sha_reply.m_status != (byte)1){
        cout<<"Wrong status of reply, sha failed!"<<endl;
        return;
    }

    //接收sha_data header
    int already_received = 0;
    int expect_length = sizeof(myFTPHeader);
    while(already_received < expect_length){
        int received = recv(client_socket, &sha_data + already_received, expect_length - already_received, 0);
        if(received < 0){
            cout<<"Receive file header failed!"<<endl;
            return;
        }
        if(received == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        already_received += received;
    }
    //sha_data header合法性检查
    if(!protocol_check(sha_data.m_protocol)){
        cout<<"Protocol check failed!"<<endl;
        return;
    }

    if(sha_data.m_type != (byte)0xFF){
        cout<<"Wrong type of file header!"<<endl;
        return;
    }

    //获取sha256大小
    int sha_size = ntohl(sha_data.m_length) - 12;
    // cout<<sha_size<<endl;

    //接收sha256
    char sha_buffer[sha_size]={0}; //也是根据sha_size动态分配
    int sha_received = 0;
    while(sha_received < sha_size){
        int received = recv(client_socket, sha_buffer + sha_received, sha_size - sha_received, 0);
        if(received < 0){
            cout<<"Receive sha256 failed!"<<endl;
            return;
        }
        if(received == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        sha_received += received;
    }

    cout<<"------SHA256 result start------"<<endl;
    cout<<"SHA256: "<<sha_buffer<<endl;
    cout<<"------SHA256 result end------"<<endl;
    return;
}

//close the connection
void myQuit(){

    if(state == "no connection"){
        //如果没有连接，调用quit会关闭client
        close_client = 1;
        return;
    }

    //发送quit_request
    myFTPHeader quit_request, quit_reply;
    memcpy(quit_request.m_protocol, prot, 6);
    quit_request.m_type = (byte)0xAB;
    quit_request.m_length = htonl(12);

    memset(send_buffer, 0, sizeof(send_buffer));
    memcpy(send_buffer, &quit_request, sizeof(quit_request));
    int bytes_sent = 0;
    int expect_sent = sizeof(myFTPHeader);
    while(bytes_sent < expect_sent){
        int sent = send(client_socket, send_buffer + bytes_sent, expect_sent - bytes_sent, 0);
        if(sent < 0){
            cout<<"Send quit_request failed!"<<endl;
            return;
        }
        if(sent == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_sent += sent;
    }

    //接收quit_reply
    int bytes_received = 0;
    while(bytes_received < 12){
        int received = recv(client_socket, &quit_reply + bytes_received, 12 - bytes_received, 0);
        if(received < 0){
            cout<<"Receive quit_reply failed!"<<endl;
            return;
        }
        if(received == 0){
            cout << "Server closed connection!" << endl;
            return;
        }
        bytes_received += received;
    }
    //检查quit_reply合法性
    if(!protocol_check(quit_reply.m_protocol)){
        cout<<"Protocol check failed!"<<endl;
        return;
    }

    if(quit_reply.m_type == (byte)0xAC){
        if(close(client_socket) < 0){ //注意这里要用close关闭socket
            cout<<"Close failed!"<<endl;
            return;
        }
        else{
            cout<<"You have closed the connection!"<<endl;
            state = "no connection";
            return;
        }
    }
    else{
        cout<<"Wrong status of reply, quit failed!"<<endl;
        return;
    }

}

//parse command and return the number of arguments
int parse_cmd(char* cmd, string argv[]){
    if(cmd == nullptr)
    {
        cout << "Command is nullptr!" << endl;
        return -1;
    }
    istringstream iss(cmd);
    string tmp;
    int cnt = 0;
    while(iss >> tmp){
        argv[cnt++] = tmp;
    }
    argv[cnt] = "\0";
    return cnt;
}

void client_handler(char * cmd){
    // parse command and analyse it
    string argv[5];
    int argc = parse_cmd(cmd, argv);
    if(argc <= 0) return; //输入为空，直接返回继续输入
    if(argc > MAX_ARGC){
        cout<<"Invalid commands! Too many arguments!"<<endl;
        return;
    }

    int cmd_type = str2type(argv[0]);
    switch (cmd_type)
    {
    case 1:
        if(argc!=3){
            cout<<"Argument number error!"<<endl;
            return;
        }
        myOpen(argv[1].c_str(), argv[2].c_str());
        return;
    case 2:
        if(argc!=1){
            cout<<"Argument number error!"<<endl;
            return;
        }
        myLs();
        return;
    case 3:
        if(argc!=2){
            cout<<"Argument number error!"<<endl;
            return;
        }
        myGet(argv[1].c_str());
        return;
    case 4: 
        if(argc!=2){
            cout<<"Argument number error!"<<endl;
            return;
        }
        myPut(argv[1].c_str());
        return; 
    case 5:
        if(argc!=2){
            cout<<"Argument number error!"<<endl;
            return;
        }
        mySha(argv[1].c_str());
        return;
    case 6:    
        if(argc!=1){
            cout<<"Argument number error!"<<endl;
            return;
        }
        myQuit();
        return; 
    default:
        cout << "Invalid command!" << endl;
        return;
    }
}

int main() {
    while(1){
        cout<<"Client("<< state <<"): ";
        char command[MAX_BUFFER_SIZE];
        if(fgets(command, MAX_BUFFER_SIZE, stdin) != nullptr){
            //fgets会把换行符也读进来，需要替换为终止符
            command[strlen(command)-1] = '\0';
        }
        else{
            cout<<"Input error!"<<endl;
            continue; //输入错误，重新输入
        }
        client_handler(command);

        if(close_client == 1){
            cout<<"Client closed!"<<endl;
            break;
        }
    }
    return 0;
}
