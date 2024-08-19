// mqtt.h
#ifndef ENGINE_SERVER_MQTT_H
#define ENGINE_SERVER_MQTT_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/kernel.h>
#include <engine/mqtt.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <mqtt/async_client.h>
#include <nlohmann/json.hpp>

class CConfig;
using json = nlohmann::json;

class CMqtt : public IMqtt
{
	class IServer *m_pServer;
	class IConsole *m_pConsole;
	class IGameServer *m_pGameServer;
	class CGameContext *m_pGameContext;

	class IServer *Server() const { return m_pServer; }
	class IConsole *Console() const { return m_pConsole; }
	class IGameServer *GameServer() const { return m_pGameServer; }
	class CGameContext *GameContext() const { return m_pGameContext; }

	std::unique_ptr<mqtt::async_client> client_; // Use unique_ptr for the client
	mqtt::connect_options connOpts_;

	// Define the structure for Player (optional, if needed)
	struct Player
	{
		std::string username;
		std::shared_ptr<std::string> team;
	};

	// Define the structure for Team
	struct Team
	{
		std::string name;
		std::string leader;
		std::vector<std::string> members;
		std::shared_ptr<std::vector<std::string>> invites;
	};

	// Define the structure for the data in ResponseType
	struct ResponseData
	{
		std::string reason;
		std::string requester;
		std::shared_ptr<Team> tTeam;
		std::shared_ptr<std::string> invitedPlayer;
	};

	// Define the structure for ResponseType
	struct ResponseType
	{
		bool success;
		ResponseData data;
	};

	// Implement to_json and from_json for Player
	friend void to_json(nlohmann::json &j, const Player &p)
	{
		j = nlohmann::json{{"username", p.username}};
		if(p.team)
		{
			j["team"] = *(p.team);
		}
	}

	friend void from_json(const nlohmann::json &j, Player &p)
	{
		j.at("username").get_to(p.username);
		if(j.contains("team"))
		{
			p.team = std::make_shared<std::string>(j.at("team").get<std::string>());
		}
	}

	// Implement to_json and from_json for Team
	friend void to_json(nlohmann::json &j, const Team &t)
	{
		j = nlohmann::json{{"name", t.name}, {"leader", t.leader}, {"members", t.members}};
		if(t.invites)
		{
			j["invites"] = *(t.invites);
		}
	}

	friend void from_json(const nlohmann::json &j, Team &t)
	{
		j.at("name").get_to(t.name);
		j.at("leader").get_to(t.leader);
		j.at("members").get_to(t.members);
		if(j.contains("invites"))
		{
			t.invites = std::make_shared<std::vector<std::string>>(j.at("invites").get<std::vector<std::string>>());
		}
	}

	// Implement to_json and from_json for ResponseData
	friend void to_json(nlohmann::json &j, const ResponseData &rd)
	{
		j = nlohmann::json{{"reason", rd.reason}, {"requester", rd.requester}};
		if(rd.tTeam)
		{
			j["tTeam"] = *(rd.tTeam);
		}
		if(rd.invitedPlayer)
		{
			j["invitedPlayer"] = *(rd.invitedPlayer);
		}
	}

	friend void from_json(const nlohmann::json &j, ResponseData &rd)
	{
		j.at("reason").get_to(rd.reason);
		j.at("requester").get_to(rd.requester);
		if(j.contains("tTeam"))
		{
			rd.tTeam = std::make_shared<Team>(j.at("tTeam").get<Team>());
		}
		if(j.contains("invitedPlayer"))
		{
			rd.invitedPlayer = std::make_shared<std::string>(j.at("invitedPlayer").get<std::string>());
		}
	}

	// Implement to_json and from_json for ResponseType
	friend void to_json(nlohmann::json &j, const ResponseType &rt)
	{
		j = nlohmann::json{{"success", rt.success}, {"data", rt.data}};
	}

	friend void from_json(const nlohmann::json &j, ResponseType &rt)
	{
		j.at("success").get_to(rt.success);
		j.at("data").get_to(rt.data);
	}

public:
	CMqtt();
	virtual ~CMqtt();
	IMqtt *CreateMqtt();

	/* VARIABLES */
	std::string prefix;

	int m_lastHeartBeat;
	bool m_rMapUpdate;
	bool m_rHeartbeat;
	bool m_rServerUpdate;

	enum class TmState
	{
		Idle,
		RegisterStart,
		RegisterEnd,
		Racing,
		Finished
	};

	TmState tmState = TmState::Idle;

	/* SETTER AND GETTER */
	void SetMapUpdate(bool update) override { m_rMapUpdate = update; }
	void SetHeartbeat(bool update) override { m_rHeartbeat = update; }
	void SetServerUpdate(bool update) override { m_rServerUpdate = update; }
	void SetLastHeartbeat(int lastHeartBeat) override { m_lastHeartBeat = lastHeartBeat; }

	int GetLastHeartbeat() override { return m_lastHeartBeat; }

	/* MQTT FUNCTIONS */
	void Run() override;
	void Init() override;
	bool Subscribe(const int &topic) override;
	bool Publish(const int &topic, const std::string &payload) override;
	bool Publish(const int &topic, const json &payload) override;
	bool PublishWithResponse(const int &topic, const std::string &payload, const std::string &responseTopic) override;
	bool WaitForResponse(const std::string &responseTopic, std::function<void(const std::string &)> callback) override;

	/* EXTRA FUNCTIONS */
	std::string GetChannelName(int channel);
	std::string RandomUuid();

	bool RequestLogin(const int &clientId, const std::string &logintoken) override;
	bool RequestTJoin(const int &clientId, const std::string &token) override;
	bool RequestTInvite(const int &clientId, const std::string &token) override;
	bool RequestTLeave(const int &clientId) override;
	bool RequestTAccept(const int &clientId, const std::string &token) override;
	bool RequestTournementMode(std::string mode, int teamSize) override;
	void Simulate(CPlayer *pPlayer) override;
private:
	std::unordered_map<std::string, std::function<void(const std::string &)>> m_responseCallbacks;
	bool m_connected;
	std::unordered_map<int, json> m_missedMessages;
	// list of players who need to get inmovable
	std::vector<int> m_inmovablePlayers;

	json SerializeMapInfo();
	json SerializeServer();
	json SerializePlayer(CPlayer *pPlayer, int JID);
	void HandleMessage(const std::string &topic, const std::string &payload);
	void SendRconLine(int ClientId, const char *pLine);
};

#endif // ENGINE_SERVER_MQTT_H
