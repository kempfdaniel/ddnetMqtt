#ifndef ENGINE_MQTT_H
#define ENGINE_MQTT_H

#include "kernel.h"
#include <memory>
#include <game/server/player.h>
#include <nlohmann/json.hpp>


enum
{
	CHANNEL_SERVER = 0,
	CHANNEL_CONSOLE,
	CHANNEL_CHAT,
	CHANNEL_VOTE,
	CHANNEL_RCON,
	CHANNEL_LOGIN,
	CHANNEL_MAP,
	CHANNEL_CONNECTION,
	CHANNEL_PLAYERINFO,
	CHANNEL_SERVERINFO,
	CHANNEL_RESPONSE,
	CHANNEL_EXECUTE,
	CHANNEL_INGAME,
	CHANNEL_RESPONSETYPE_RCON = 0,
	CHANNEL_RESPONSETYPE_CHAT,
	CHANNEL_RESPONSETYPE_RESENDMAP,
	CHANNEL_RESPONSETYPE_CREATETEAM,
};


class IMqtt : public IInterface
{
	MACRO_INTERFACE("mqtt")
public:	
    using json = nlohmann::json;
	virtual ~IMqtt() {};

	/* SETTER AND GETTER */
    virtual void SetMapUpdate(bool update) = 0;
	virtual void SetHeartbeat(bool update) = 0;
	virtual void SetServerUpdate(bool update) = 0;
	virtual void SetLastHeartbeat(int lastHeartBeat) = 0;
	virtual int GetLastHeartbeat() = 0;

	/* MQTT FUNCTIONS */
	virtual void Run() = 0;
	virtual void Init() = 0;
	virtual bool Subscribe(const int &topic) = 0;
	virtual bool Publish(const int &topic, const std::string &payload) = 0;
	virtual bool Publish(const int& topic, const json& payload) = 0;
    virtual bool PublishWithResponse(const int &topic, const std::string &payload, const std::string &responseTopic) = 0;
    virtual bool WaitForResponse(const std::string &responseTopic, std::function<void(const std::string&)> callback) = 0;

	/* EXTRA FUNCTIONS */
	virtual bool RequestLogin(const int &clientId, const std::string &logintoken) = 0;
	virtual bool RequestTJoin(const int &clientId, const std::string &teamname) = 0;
	virtual bool RequestTInvite(const int &clientId, const std::string &playername) = 0;
	virtual bool RequestTLeave(const int &clientId) = 0;
	virtual bool RequestTAccept(const int &clientId, const std::string &teamname) = 0;
	virtual bool RequestTournementMode(std::string mode, int teamSize) = 0;
	virtual void Simulate(CPlayer *pPlayer) = 0;

};
IMqtt *CreateMqtt();
#endif // ENGINE_MQTT_H
