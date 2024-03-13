#include "switch.h"

SwitchBase* CreateSwitchObject() {
  return new Switch();
}

int PackFrame(char* unpacked_frame, char* packed_frame, int frame_length)
{
  int packed_length = 0;
  int count = 0;
  //在开头加上值为0xDE的字节
  packed_frame[packed_length++] = (char)FRAME_DELI;
  for (int i = 0; i < frame_length; i++) {
    if(memcmp(&unpacked_frame[i], &FRAME_DELI, sizeof(char)) == 0){
      packed_frame[packed_length++] = (char)FRAME_DELI;
    }
    packed_frame[packed_length++] = unpacked_frame[i];
  }

  //在成帧后进行偶校验
  for(int i = 0; i < packed_length; i++){
    for (int j = 0; j < 8; j++) {
      count += (packed_frame[i]>>j) & 0x01;
    }
  }  

  // Add parity bit
  packed_frame[packed_length++] = (count % 2 == 0) ? 0x00 : 0x01;

  return packed_length;
}

int UnpackFrame(char* unpacked_frame, char* packed_frame, int frame_length)
{
  // 先进行偶校验
  int count = 0;
  for(int i = 0; i < frame_length-1; i++){ // Exclude the parity bit
    for (int j = 0; j < 8; j++) {
      count += (packed_frame[i]>>j) & 0x01; //这里bug是一开始写成unpacked了！！！
    }
  }
  // Check parity bit
  if ((count % 2 == 0) != (packed_frame[frame_length - 1] == (char)0x00)) {
    return -1;  // Parity check failed
  }

  // 删除多余的定界符以得到原来的帧
  // start from 1, exclude the first delimiter
  int unpacked_length = 0;
  for (int i = 1; i < frame_length - 1; i++) {  
    if (memcmp(&packed_frame[i], &FRAME_DELI, sizeof(char)) == 0) {
      if (memcmp(&packed_frame[i + 1], &FRAME_DELI, sizeof(char)) != 0) {
        return -1;  // Invalid frame
      }
      i++;  // Skip this delimiter
    }
    // cout<<(char)packed_frame[i];
    unpacked_frame[unpacked_length++] = packed_frame[i]; 
  }
  cout<<endl;
  return unpacked_length;
}

void Switch::InitSwitch(int numPorts)
{
  this->numPorts = numPorts;
}

/*
inPort表示收到的帧的入端口号
framePrt是一个指针，表示收到的帧，其中帧的格式满足3.1中规定的格式且未经封装，可以直接访问
ProcessFrame的返回值表示的是这个帧应当被转发到的端口号，特别地：
  返回的端口号为-1，表示此帧应当被丢弃。
  返回的端口号为0，表示此帧应当被广播到除了入端口外的其他端口上。
*/
int Switch::ProcessFrame(int inPort, char* framePtr)
{
  ether_header_t frameHeader;
  memcpy(&frameHeader, framePtr, sizeof(ether_header_t));
  if(frameHeader.ether_type == ETHER_DATA_TYPE){
    //先判断是否可以找到源mac地址端口号
    bool flag_src = false;
    for(auto i=switch_table.begin();i!=switch_table.end();++i){
      if(memcmp(i->mac, frameHeader.ether_src, sizeof(mac_addr_t)) == 0){
        i->timestamp = ETHER_MAC_AGING_THRESHOLD;//插入表项已存在，更新时间戳
        flag_src = true;
        break;
      }
    }
    //表项不存在，插入新表项
    if(!flag_src){
      //记录入端口与帧的源MAC地址的映射关系
      node temp(inPort, frameHeader.ether_src, ETHER_MAC_AGING_THRESHOLD);
      switch_table.push_back(temp);
    }
    //再判断是否可以找到目的mac地址端口号
    bool flag_dest = false;
    for(auto i=switch_table.begin();i!=switch_table.end();++i){
      if(memcmp(i->mac, frameHeader.ether_dest, sizeof(mac_addr_t)) == 0){
        int outPort = i->port;
        if(outPort == inPort)
          return -1;
        else
          return outPort;
      }
    }
    return 0;
  }
  // 交换机在收到控制指令后，应当立即对其所存储的转发表项进行老化
  else if(frameHeader.ether_type == ETHER_CONTROL_TYPE){
    for(auto i=switch_table.begin();i!=switch_table.end();++i){
      i->timestamp--;
      if(i->timestamp == 0){
        switch_table.erase(i);
        i--;
      }
    }
    return -1;
  }
  else{
    cout<<"error: Unknown ether_type"<<endl;
    return -1;
  }
}

