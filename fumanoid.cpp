/** @file
 **
 ** Converts status messages from/to old robots.
 **
 */

#include "referee.h"
#include "communication/comm.h"

#include "robot.h"
#include "services.h"
#include "management/config/configRegistry.h"
#include "management/config/config.h"

#include "debug.h"

#include "platform/system/timer.h"
#include "platform/system/transport/transport_udp.h"

#include <msg_gamecontroller.pb.h>

#include <sys/types.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <string>


/*------------------------------------------------------------------------------------------------*/

REGISTER_PARAMETER("game.team.id",    DEFAULTTEAMID,         "Team ID for game controller");
REGISTER_PARAMETER("game.team.color", "magenta",             "our team color (magenta or cyan)");

REGISTER_PARAMETER("referee.port",    REFEREEPORT,           "UDP port for referee / game controller messages");
REGISTER_PARAMETER("referee.enabled", DEFAULTREFEREEENABLED, "Whether the referee is enabled");


/*------------------------------------------------------------------------------------------------*/

/**
 ** Constructor
**/

RefereeGameController::RefereeGameController() {
	cs.setName("RefereeGameController::cs");

	services.getMessageRegistry().registerMessageCallback(this, "gcInfo");
}


/*------------------------------------------------------------------------------------------------*/

/**
 **
 */

void RefereeGameController::init() {
	CriticalSectionLock lock(cs);
	// get team ID
	state.teamID = services.getConfig().getIntValue("game.team.id");

	// get team color
	std::string teamColorStr = services.getConfig().getStrValue("game.team.color", "magenta");
	if (teamColorStr != "") {
		state.teamColor = teamColorStr == "magenta" ? Magenta : Cyan;
	}

	// enable referee unless turned off
	refereeEnabled = (0 != services.getConfig().getIntValue("referee.enabled"));

	if (!refereeEnabled) {
		WARNING("Referee is disabled");
		return;
	}

	gcPort = services.getConfig().getIntValue("referee.port");
	transport = new TransportUDP(gcPort, gcPort, true);
	if (transport == 0) {
		WARNING("Could not open UDP socket for referee, referee is disabled");
		return;
	}

	if (false == transport->open()) {
		WARNING("Could not open UDP connection for referee, referee is disabled");
		return;
	}

	// start thread
	run();
}


/*------------------------------------------------------------------------------------------------*/

/**
 ** Destructor
**/

RefereeGameController::~RefereeGameController() {
	kill();

	// close transport
	if (transport) {
		transport->close();
		delete transport;
	}

	transport = 0;
}


/*------------------------------------------------------------------------------------------------*/

/** Message callback
 **
 ** @param messageName    Name/type of message
 ** @param msg            Received message
 ** @param senderID       ID of sender
 **
 ** @return true if message was processed
 */

bool RefereeGameController::messageCallback(
	const std::string               &messageName,
	const google::protobuf::Message &msg,
	int32_t                          id,
	RemoteConnectionPtr              remote)
{
	const de::fumanoids::message::GCInfo &message = (const de::fumanoids::message::GCInfo&)msg;

	if (message.has_sendrequest() and message.sendrequest()) {
		de::fumanoids::message::Message msg;
		msg.set_robotid(robot.getID());

		de::fumanoids::message::GCInfo &gcInfo = *msg.MutableExtension(de::fumanoids::message::gcInfo);
		gcInfo.set_robotgamecontrollerport(gcPort);

		services.getComm().broadcastMessage(msg);
	}

	return true;
}


/*------------------------------------------------------------------------------------------------*/

/**
 ** Handles incoming data.
 **
 ** The thread code is a simple loop receiving messages and dispatching them to handleOperation().
 ** Note that messages are processed in the order they are received, and one after the other.
**/

#define BUFFSIZE 1500

void RefereeGameController::threadMain() {
	INFO("GameController listener started");
	while (isRunning()) {
		if (false == transport->waitForData(1, Microsecond(500*milliseconds)))
			continue;

		struct sockaddr_in remoteAddress;
		uint8_t buffer[BUFFSIZE];

		int received = transport->read(buffer, BUFFSIZE, &remoteAddress);
		if (received <= 0) {
			// TODO: handle error in receiving
			printf("error recv: %d - %s\n", errno, strerror(errno));
			continue;
		}

		if (strncmp((const char*)buffer, STRUCT_HEADER, 4) == 0) {
			if (received != sizeof(RoboCupGameControlData))
				ERROR("Received referee data with size mismatch, got %d but expected %d bytes",
				       received, (int)sizeof(RoboCupGameControlData))
			else {
				handleRefereeMessage((RoboCupGameControlData*)(void*)buffer);

				// send status to game controller (we only send this each time
				// we successfully received a message from the game controller
				// as this is the only way to make sure that the game controller
				// display will reflect whether we receive its messages)
				struct RoboCupGameControlReturnData status = {
					"",
					GAMECONTROLLER_RETURN_STRUCT_VERSION,
					state.teamID,
					robot.getID(),
					GAMECONTROLLER_RETURN_MSG_ALIVE
				};
				strncpy(status.header, GAMECONTROLLER_RETURN_STRUCT_HEADER, 4);

				// adjust port, as it is sent from a random one but we need to
				// return our answer to the correct port!
				remoteAddress.sin_port = htons(gcPort);
				transport->write((uint8_t*)&status, sizeof(status), &remoteAddress);
			}
		}
		/* else {
			printf("received unknown packet, header is %x%x%x%x\n", buffer[0], buffer[1], buffer[2], buffer[3]);
			printf("received unknown packet, header is %c%c%c%c\n", buffer[0], buffer[1], buffer[2], buffer[3]);
		}*/
	}
}


/*------------------------------------------------------------------------------------------------*/

/** Convert a robot's ID to the one used by the game controller
 **
 ** @param robotID    The ID of the robot in our system
 ** @return ID of the robot in the game controller
 **
 */

int16_t RefereeGameController::getGCRobotID(int16_t robotID) {
	// robots 1..11 are mapped to 0..10 in the game controller
	if (robotID > 0 && robotID <= MAX_NUM_PLAYERS) {
		return robotID - 1;
	}
	// throw error on all other robots
	return -1;
}


/*------------------------------------------------------------------------------------------------*/

/**
 */

void RefereeGameController::handleRefereeMessage(RoboCupGameControlData *data) {
	if (data->version != STRUCT_VERSION) {
		refereeEnabled = false;
		ERROR("Referee game controller version mismatch (got %d, want %d)", data->version, STRUCT_VERSION);
		return;
	}

	if (refereeEnabled == false)
		return;

	// check whether we are an intended recipient
	int8_t teamIndex = -1;
	if (data->teams[0].teamNumber == state.teamID) {
		teamIndex = 0;
	} else if (data->teams[1].teamNumber == state.teamID) {
		teamIndex = 1;
	} else {
		// we are not one of the teams listed, ignore
		return;
	}

	// new state received
	state.counter++;

	// set the ID of our opponent
	state.opponentID = data->teams[1 - teamIndex].teamNumber;

	// adjust our score
	if (data->teams[teamIndex].score != state.teamScore) {
		state.teamScore = data->teams[teamIndex].score;
		INFO("We scored (%d:%d now)", state.teamScore, data->teams[1-teamIndex].score);
	}

	// adjust opponent score
	if (data->teams[1-teamIndex].score != state.opponentScore) {
		state.opponentScore = data->teams[1-teamIndex].score;
		INFO("Opponent scored (%d:%d now)", data->teams[teamIndex].score, state.opponentScore);
	}

	// addendum to rules in RoboCup 2010 - on drop in, the robots position outside the circle and
	// the ball is in play right away
	/*if (data->kickOffTeam == 2) {
		if (data->state == STATE_PLAYING)
			data->kickOffTeam = teamIndex; // when playing, assume we have kickoff
		else
			data->kickOffTeam = 1 - teamIndex; // when not playing (ready/positioning), assume we are not having the kick off
	}*/
	// handle drop ball, alternative implementation
	if (data->kickOffTeam == 2 && state.kickOffMode != KICKOFF_DROPBALL) {
		INFO("Switching to dropball mode");
		state.kickOffMode = KICKOFF_DROPBALL;
		state.kickOffSide = KICKOFFSIDE_ANY;
	}

	// adjust kick off if our settings mismatch those of the referee
	if (    (data->kickOffTeam ==   teamIndex && state.kickOffSide != KICKOFFSIDE_ME)
	     || (data->kickOffTeam == 1-teamIndex && state.kickOffSide != KICKOFFSIDE_OPPONENT)
	   )
	{
		INFO("Switching kick off team to %s", data->kickOffTeam == teamIndex ? "us" : "opponent");
		state.kickOffMode = KICKOFF_REGULAR;
		state.kickOffSide = data->kickOffTeam == teamIndex ? KICKOFFSIDE_ME : KICKOFFSIDE_OPPONENT;
	}

	// adjust team color
	Color teamColor = data->teams[teamIndex].teamColour == TEAM_MAGENTA ? Magenta : Cyan;
	if (teamColor != state.teamColor) {
		INFO("Switching team color to %s", teamColor == Magenta ? "magenta" : "cyan");
		state.teamColor = teamColor;
	}

	// adjust game state
	if (data->state == STATE_INITIAL && state.gameState != GAME_STOPPED) {
		INFO("Game state is now set to INITIAL");
		state.gameState = GAME_STOPPED;
	} else if (data->state == STATE_READY && state.gameState != GAME_READY) {
		INFO("Game state is now set to READY");
		state.gameState = GAME_READY;
	} else if (data->state == STATE_SET&& state.gameState != GAME_SET) {
		INFO("Game state is now set to SET");
		state.gameState = GAME_SET;
	} else if (data->state == STATE_PLAYING && state.gameState != GAME_STARTED) {
		INFO("Game state is now set to PLAY");
		state.gameState = GAME_STARTED;
	} else if (data->state == STATE_FINISHED&& state.gameState != GAME_STOPPED) {
		INFO("Game state is now set to FINISHED");
		state.gameState = GAME_STOPPED;
	}

	// penalty shootout?
	if (data->secGameState == STATE2_NORMAL && state.isPenaltyShoot) {
		INFO("Return to normal gameplay after penalty shoot mode");
		state.isPenaltyShoot = false;
	} else if (data->secGameState == STATE2_PENALTYSHOOT && false == state.isPenaltyShoot) {
		INFO("Penalty shoot mode activated");
		state.isPenaltyShoot = true;
	}
//	GameControllerStateProvider::getInstance().setPenaltyShoot(isPenaltyShoot);

	// handle penalties
	int gcRobotID = getGCRobotID(robot.getID());
	if (gcRobotID != -1) {
		state.remainingPenalizedTime = data->teams[teamIndex].players[ gcRobotID ].secsTillUnpenalised;
		if (state.remainingPenalizedTime > 0) {
			state.isPenalized = true;
		} else {
			state.isPenalized = false;
			state.remainingPenalizedTime = 0;
		}
	}

	GCinitialized = true;
}
