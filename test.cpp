#include <stdio.h>
#include <unistd.h>
#include "RoboCupGameControlData.h"

int main(int argc, char *argv[]){
  RoboCupGameControlData gameCtrlData; // should probably zero it the first time it is used
  AL::ALMemoryProxy *memory = new AL::ALMemoryProxy(pBroker);
  memory->insertData("GameCtrl/teamNumber", 1);
  memory->insertData("GameCtrl/teamColour", TEAM_CYAN);
  memory->insertData("GameCtrl/playerNumber", 2);

  while(1){
    AL::ALValue value = memory->getData("GameCtrl/RoboCupGameControlData");
    if (value.isBinary() && value.getSize() == sizeof(RoboCupGameControlData))
      memcpy(&gameCtrlData, value, sizeof(RoboCupGameControlData));
    printf("%d\n",gameCtrlData.state);
    sleep(1);
  }
  return 0;
}
