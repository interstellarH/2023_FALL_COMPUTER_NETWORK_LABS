#ifndef COMPNET_LAB4_SRC_SWITCH_H
#define COMPNET_LAB4_SRC_SWITCH_H
using namespace std;
#include "types.h"
#include <iostream>
#include <vector>
#include <set>
#include <map>

class SwitchBase {
 public:
  SwitchBase() = default;
  ~SwitchBase() = default;

  virtual void InitSwitch(int numPorts) = 0;
  virtual int ProcessFrame(int inPort, char* framePtr) = 0;
};

extern SwitchBase* CreateSwitchObject();
extern int PackFrame(char* unpacked_frame, char* packed_frame, int frame_length);
extern int UnpackFrame(char* unpacked_frame, char* packed_frame, int frame_length);

// TODO : Implement your switch class.

struct node{
  int port;
  mac_addr_t mac;
  uint16_t timestamp;
  node(int p, mac_addr_t m, uint16_t t): port(p), timestamp(t){
    memcpy(this->mac, m, sizeof(mac_addr_t));
  }
};

class Switch: public SwitchBase{
  public:
    int numPorts;
    vector<node> switch_table;

    Switch() = default;
    ~Switch() = default;
    void InitSwitch(int numPorts);
    int ProcessFrame(int inPort, char* framePtr);
};

#endif  // ! COMPNET_LAB4_SRC_SWITCH_H
