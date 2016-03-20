/**
 * @file GameCtrl.cpp
 * Implementation of a NAOqi library that communicates with the GameController.
 * It provides the data received in ALMemory.
 * It also implements the official button interface and sets the LEDs as
 * specified in the rules.
 *
 * @author Thomas Röfer
 */

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <cstring>
#include "RoboCupGameControlData.h"
#include "UdpComm.h"

#include <iostream>

static const int BUTTON_DELAY = 30; /**< Button state changes are ignored when happening in less than 30 ms. */
static const int GAMECONTROLLER_TIMEOUT = 2000; /**< Connected to GameController when packet was received within the last 2000 ms. */
static const int ALIVE_DELAY = 500; /**< Send an alive signal every 500 ms. */



class GameCtrl{

public:
//private:
  static GameCtrl* theInstance; /**< The only instance of this class. */

  UdpComm* udp; /**< The socket used to communicate. */
  const int* playerNumber; /** Points to where ALMemory stores the player number. */
  const int* teamNumberPtr; /** Points to where ALMemory stores the team number. The number be set to 0 after it was read. */
  const int* defaultTeamColour; /** Points to where ALMemory stores the default team color. */
  int teamNumber; /**< The team number. */
  RoboCupGameControlData gameCtrlData; /**< The local copy of the GameController packet. */
  uint8_t previousState; /**< The game state during the previous cycle. Used to detect when LEDs have to be updated. */
  uint8_t previousSecondaryState; /**< The secondary game state during the previous cycle. Used to detect when LEDs have to be updated. */
  uint8_t previousKickOffTeam; /**< The kick-off team during the previous cycle. Used to detect when LEDs have to be updated. */
  uint8_t previousTeamColour; /**< The team colour during the previous cycle. Used to detect when LEDs have to be updated. */
  uint8_t previousPenalty; /**< The penalty set during the previous cycle. Used to detect when LEDs have to be updated. */
  unsigned whenPacketWasReceived; /**< When the last GameController packet was received (DCM time). */
  unsigned whenPacketWasSent; /**< When the last return packet was sent to the GameController (DCM time). */

  /**
   * Resets the internal state when an application was just started.
   */
  void init()
  {
    previousState = (uint8_t) -1;
    previousSecondaryState = (uint8_t) -1;
    previousKickOffTeam = (uint8_t) -1;
    previousTeamColour = (uint8_t) -1;
    previousPenalty = (uint8_t) -1;
    whenPacketWasReceived = 0;
    whenPacketWasSent = 0;
    memset(&gameCtrlData, 0, sizeof(gameCtrlData));
  }


  /**
   * Sends the return packet to the GameController.
   * @param message The message contained in the packet (GAMECONTROLLER_RETURN_MSG_MAN_PENALISE,
   *                GAMECONTROLLER_RETURN_MSG_MAN_UNPENALISE or GAMECONTROLLER_RETURN_MSG_ALIVE).
   */
  bool send(uint8_t message)
  {
    RoboCupGameControlReturnData returnPacket;
    returnPacket.team = (uint8_t) teamNumber;
    returnPacket.player = (uint8_t) *playerNumber;
    returnPacket.message = message;
    return !udp || udp->write((const char*) &returnPacket, sizeof(returnPacket));
  }

  /**
   * Receives a packet from the GameController.
   * Packets are only accepted when the team number is know (nonzero) and
   * they are addressed to this team.
   */
  bool receive()
  {
    bool received = false;
    int size;
    RoboCupGameControlData buffer;
    while(udp && (size = udp->read((char*) &buffer, sizeof(buffer))) > 0)
    {
      if(size == sizeof(buffer) &&
         !std::memcmp(&buffer, GAMECONTROLLER_STRUCT_HEADER, 4) &&
         buffer.version == GAMECONTROLLER_STRUCT_VERSION &&
         teamNumber &&
         (buffer.teams[0].teamNumber == teamNumber ||
          buffer.teams[1].teamNumber == teamNumber))
      {
        gameCtrlData = buffer;
        received = true;
      }
    }
    return received;
  }


  /**
   * Close all resources acquired.
   * Called when initialization failed or during destruction.
   */
  void close()
  {
    if(udp)
      delete udp;
  }

  //

  /**
   * The constructor sets up the structures required to communicate with NAOqi.
   * @param pBroker A NAOqi broker that allows accessing other NAOqi modules.
   */
  GameCtrl()
  : udp(0),
    teamNumber(0)
  {
    init();
    theInstance = this;
    //todo
    //playerNumber = (int*) memory->getDataPtr("GameCtrl/playerNumber");
    //teamNumberPtr = (int*) memory->getDataPtr("GameCtrl/teamNumber");
    //defaultTeamColour = (int*) memory->getDataPtr("GameCtrl/teamColour");

    udp = new UdpComm();
    if(!udp->setBlocking(false) ||
       !udp->setBroadcast(true) ||
       !udp->bind("0.0.0.0", GAMECONTROLLER_PORT) ||
       !udp->setTarget(UdpComm::getWifiBroadcastAddress(), GAMECONTROLLER_PORT) ||
       !udp->setLoopback(false))
      {
        fprintf(stderr, "libgamectrl: Could not open UDP port\n");
        delete udp;
        udp = 0;
        close();
      }
  }

  /**
   * Close all resources acquired.
   */
  ~GameCtrl()
  {
    close();
  }
};

GameCtrl* GameCtrl::theInstance = 0;

int main(int argc, char *argv[])
{
  GameCtrl gamectl;
  gamectl.teamNumber=2;
  while(1){
    if(gamectl.receive()){
      printf("%d\n",gamectl.gameCtrlData.state);
    }
  }
  return 0;
}
