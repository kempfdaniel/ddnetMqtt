// mqtt.cpp

#include "mqtt.h"

#include "game/server/gamecontroller.h"
#include <atomic>
#include <base/system.h>
#include <game/server/entities/character.h>
#include <game/server/player.h>
#include <thread>
#define CONF_MQTTSERVICES
#ifdef CONF_MQTTSERVICES

IMqtt *CreateMqtt() { return new CMqtt(); }

CMqtt::CMqtt() :
	m_pServer(0), m_pConsole(0), m_pGameServer(0), m_lastHeartBeat(0), m_rMapUpdate(true), m_rHeartbeat(true), m_connected(false)
{
	connOpts_.set_keep_alive_interval(20);
	connOpts_.set_clean_session(true);
	dbg_msg("mqtt", "MQTT service initialized");
}

CMqtt::~CMqtt()
{
	if(client_ && m_connected)
	{
		client_->disconnect()->wait();
	}
}

void CMqtt::Init()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGameServer = Kernel()->RequestInterface<IGameServer>();
	m_pGameContext = static_cast<CGameContext *>(Kernel()->RequestInterface<IGameServer>());

	if(str_comp_nocase(g_Config.m_SvMQTTAddresse, "") == 0 || str_comp_nocase(g_Config.m_SvMQTTUsername, "") == 0 || str_comp_nocase(g_Config.m_SvMQTTPassword, "") == 0)
	{
		dbg_msg("mqtt", "MQTT service not initialized, missing configuration");
		return;
	}

	dbg_msg("mqtt", "Connecting to MQTT server over interface");
	try
	{
		client_ = std::make_unique<mqtt::async_client>(g_Config.m_SvMQTTAddresse, "DDNetServer");
		connOpts_.set_user_name(g_Config.m_SvMQTTUsername);
		connOpts_.set_password(g_Config.m_SvMQTTPassword);
		prefix = std::string(g_Config.m_SvMQTTTopic) + "/" + std::string(g_Config.m_SvSID);

		client_->connect(connOpts_)->wait();
		m_connected = true;
		client_->set_message_callback([this](mqtt::const_message_ptr msg) {
			const std::string topic = msg->get_topic();
			const std::string payload = msg->to_string();

			if(m_responseCallbacks.find(topic) != m_responseCallbacks.end())
			{
				// Rufe den Callback für dieses Antwortthema auf
				m_responseCallbacks[topic](payload);

				// Optional vom Antwortthema abmelden
				client_->unsubscribe(topic);

				// Entferne den Callback aus der Map
				m_responseCallbacks.erase(topic);
			}
			else
			{
				HandleMessage(topic, payload);
			}
		});

		Subscribe(CHANNEL_EXECUTE);

		dbg_msg("mqtt", "Connected to the MQTT broker with topic %s", prefix.c_str());
		Publish(CHANNEL_SERVER, std::string("Connected to the MQTT broker"));
	}
	catch(const mqtt::exception &exc)
	{
		dbg_msg("mqtt", "MQTT connect error: %s", exc.what());
		return;
	}
}

void CMqtt::Run()
{
	try
	{
		while(m_connected)
		{
			if(m_rMapUpdate)
			{
				Publish(CHANNEL_MAP, SerializeMapInfo());
				m_rMapUpdate = false;
			}

			if(m_rHeartbeat)
			{
				// Publish(CHANNEL_SERVERINFO, SerializeServer());
				m_rHeartbeat = false;
			}

			if(!m_missedMessages.empty())
			{
				for(auto &message : m_missedMessages)
				{
					Publish(message.first, message.second);
				}
				m_missedMessages.clear();
			}
			// std::this_thread::sleep_for(std::chrono::milliseconds(20));
		
			// Handle the inmovable players
			for(const auto &player : m_inmovablePlayers)
			{
				if(m_pGameContext->m_apPlayers[player] && m_pGameContext->m_apPlayers[player]->GetCharacter())
				{
					m_pGameContext->m_apPlayers[player]->GetCharacter()->m_moveable = false;
					m_pGameContext->m_apPlayers[player]->GetCharacter()->Freeze();
					m_inmovablePlayers.erase(std::remove(m_inmovablePlayers.begin(), m_inmovablePlayers.end(), player), m_inmovablePlayers.end());
					break;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}
	catch(const std::exception &e)
	{
		dbg_msg("mqtt", "Error during Run: %s", e.what());
	}
}

bool CMqtt::Subscribe(const int &topic)
{
	if(!m_connected)
		return false;

	try
	{
		client_->subscribe(GetChannelName(topic), 1)->wait();
		return true;
	}
	catch(const mqtt::exception &exc)
	{
		dbg_msg("mqtt", "MQTT subscribe error: %s", exc.what());
		return false;
	}

	return true;
}

bool CMqtt::Publish(const int &topic, const std::string &payload)
{
	if(!m_connected)
	{
		dbg_msg("mqtt", "Cannot publish: Not connected");
		return false;
	}

	try
	{
		client_->publish(GetChannelName(topic), payload.c_str(), payload.size(), 1, false);
		return true;
	}
	catch(const mqtt::exception &exc)
	{
		dbg_msg("mqtt", "Publish failed: %s", exc.what());
		return false;
	}
}

bool CMqtt::Publish(const int &topic, const json &payload)
{
	if(!m_connected)
	{
		m_missedMessages.emplace(topic, payload);
		return false;
	}

	try
	{
		json payloadEx = payload;
		payloadEx["tick"] = Server()->Tick();
		client_->publish(GetChannelName(topic), payloadEx.dump().c_str(), payloadEx.dump().size(), 1, false);
		return true;
	}
	catch(const mqtt::exception &exc)
	{
		dbg_msg("mqtt", "Publish failed: %s", exc.what());
		return false;
	}
}

bool CMqtt::PublishWithResponse(const int &topic, const std::string &payload, const std::string &responseTopic)
{
	try
	{
		if(!m_connected)
		{
			dbg_msg("mqtt", "Not connected to MQTT broker, cannot publish with response");
			return false;
		}
		// Subscribe to the response topic
		client_->subscribe(responseTopic, 1)->wait();

		// Add a callback to handle the response
		m_responseCallbacks[responseTopic] = [](const std::string &response) {
			// Handle the response (e.g., log it or process it)
			dbg_msg("mqtt", "Received response: %s", response.c_str());
		};

		// Publish the message
		client_->publish(GetChannelName(topic), payload.c_str(), payload.size(), 1, false)->wait();
		return true;
	}
	catch(const mqtt::exception &exc)
	{
		dbg_msg("mqtt", "Publish with response failed: %s", exc.what());
		return false;
	}
}

bool CMqtt::WaitForResponse(const std::string &responseTopic, std::function<void(const std::string &)> callback)
{
	if(!m_connected)
	{
		dbg_msg("mqtt", "Not connected to MQTT broker, cannot wait for response");
		return false;
	}

	// Store the callback for the response topic
	m_responseCallbacks[responseTopic] = callback;
	return true;
}

std::string CMqtt::GetChannelName(int channel)
{
	switch(channel)
	{
	case CHANNEL_SERVER:
		return std::string(prefix) + "/server";
	case CHANNEL_CONSOLE:
		return std::string(prefix) + "/console";
	case CHANNEL_CHAT:
		return std::string(prefix) + "/chat";
	case CHANNEL_RCON:
		return std::string(prefix) + "/rcon";
	case CHANNEL_LOGIN:
		return std::string(prefix) + "/login";
	case CHANNEL_MAP:
		return std::string(prefix) + "/map";
	case CHANNEL_CONNECTION:
		return std::string(prefix) + "/connection";
	case CHANNEL_RESPONSE:
		return std::string(prefix) + "/response";
	case CHANNEL_VOTE:
		return std::string(prefix) + "/vote";
	case CHANNEL_PLAYERINFO:
		return std::string(prefix) + "/playerinfo";
	case CHANNEL_SERVERINFO:
		return std::string(prefix) + "/serverinfo";
	case CHANNEL_INGAME:
		return std::string(prefix) + "/ingame";
	case CHANNEL_EXECUTE:
		return std::string(prefix) + "/execute";
	default:
		return std::string(prefix) + "/default";
	}
}

std::string CMqtt::RandomUuid()
{
	std::string uuid = "4y4yxxxxxxxxxxx";
	for(int i = 0; i < (int)uuid.size(); ++i)
	{
		if(uuid[i] == 'x')
		{
			uuid[i] = "0123456789abcdef"[rand() % 16];
		}
		else if(uuid[i] == 'y')
		{
			uuid[i] = "89ab"[rand() % 4];
		}
	}
	return uuid;
}

bool CMqtt::RequestLogin(const int &clientId, const std::string &logintoken)
{
	if(!m_connected)
	{
		dbg_msg("mqtt", "Not connected to MQTT broker, cannot request login");
		return false;
	}

	if(clientId < 0 || clientId >= MAX_CLIENTS)
	{
		return false;
	}
	if(m_pGameContext->m_apPlayers[clientId] == nullptr)
	{
		return false;
	}
	if(logintoken.empty())
	{
		return false;
	}

	json payload;
	std::string responseTopic = GetChannelName(CHANNEL_RESPONSE) + "/" + RandomUuid();
	char pLoginToken[128];
	char pUsername[MAX_NAME_LENGTH];
	char pIp[NETADDR_MAXSTRSIZE];
	int pClientID = clientId;
	str_copy(pLoginToken, logintoken.c_str(), sizeof(pLoginToken));
	str_copy(pUsername, m_pServer->ClientName(clientId), sizeof(pUsername));
	m_pGameContext->Server()->GetClientAddr(pClientID, pIp, sizeof(pIp));

	payload["clientid"] = pClientID;
	payload["logintoken"] = pLoginToken;
	payload["username"] = pUsername;
	payload["responseTopic"] = responseTopic;
	payload["ip"] = pIp;
	PublishWithResponse(CHANNEL_LOGIN, payload.dump(), responseTopic);

	WaitForResponse(responseTopic, [this](const std::string &response) {
		json jsonResponse = json::parse(response.c_str());
		// TODO: Handle the response
		//! Server should crash if status is not set.
		//! This is just a placeholder. Make sure to handle the response properly with try-catch
		if(jsonResponse["status"] == "success")
		{
			dbg_msg("mqtt", "Login successful");
		}
		else
		{
			dbg_msg("mqtt", "Login failed");
		}
	});

	return true;
}

/* --- TOURNEMENT LOGIC --- */

bool CMqtt::RequestTJoin(const int &clientId, const std::string &teamname)
{
	if(!m_connected)
	{
		dbg_msg("mqtt", "Not connected to MQTT broker, cannot request TJoin");
		return false;
	}

	if(clientId < 0 || clientId >= MAX_CLIENTS)
	{
		return false;
	}
	if(m_pGameContext->m_apPlayers[clientId] == nullptr)
	{
		return false;
	}
	if(teamname.empty())
	{
		return false;
	}

	json payload;
	std::string responseTopic = GetChannelName(CHANNEL_RESPONSE) + "/" + RandomUuid();
	char pTeamname[128];
	char pUsername[MAX_NAME_LENGTH];
	char pIp[NETADDR_MAXSTRSIZE];
	int pClientID = clientId;
	str_copy(pTeamname, teamname.c_str(), sizeof(pTeamname));
	str_copy(pUsername, m_pServer->ClientName(clientId), sizeof(pUsername));
	m_pGameContext->Server()->GetClientAddr(pClientID, pIp, sizeof(pIp));

	payload["action"] = "TJoin";
	payload["clientid"] = pClientID;
	payload["teamname"] = pTeamname;
	payload["username"] = pUsername;
	payload["responseTopic"] = responseTopic;
	payload["ip"] = pIp;
	PublishWithResponse(CHANNEL_RESPONSE, payload.dump(), responseTopic);

	WaitForResponse(responseTopic, [this](const std::string &mqttResponse) {
		json json_data = nlohmann::json::parse(mqttResponse.c_str());
		CMqtt::ResponseType response = json_data.get<CMqtt::ResponseType>();
		std::string requester = response.data.requester;
		std::string reason = response.data.reason;

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
			{
				GameContext()->SendChatTarget(i, response.data.reason.c_str());
			}
		}
	});

	return true;
}

bool CMqtt::RequestTInvite(const int &clientId, const std::string &playername)
{
	if(!m_connected)
	{
		dbg_msg("mqtt", "Not connected to MQTT broker, cannot request TInvite");
		return false;
	}

	if(clientId < 0 || clientId >= MAX_CLIENTS)
	{
		return false;
	}
	if(m_pGameContext->m_apPlayers[clientId] == nullptr)
	{
		return false;
	}
	if(playername.empty())
	{
		return false;
	}

	json payload;
	std::string responseTopic = GetChannelName(CHANNEL_RESPONSE) + "/" + RandomUuid();
	char pPlayername[128];
	char pUsername[MAX_NAME_LENGTH];
	char pIp[NETADDR_MAXSTRSIZE];
	int pClientID = clientId;
	str_copy(pPlayername, playername.c_str(), sizeof(pPlayername));
	str_copy(pUsername, m_pServer->ClientName(clientId), sizeof(pUsername));
	m_pGameContext->Server()->GetClientAddr(pClientID, pIp, sizeof(pIp));

	payload["action"] = "TInvite";
	payload["responseTopic"] = responseTopic;
	payload["clientid"] = pClientID;
	payload["username"] = pUsername;
	payload["playername"] = pPlayername;
	payload["ip"] = pIp;
	PublishWithResponse(CHANNEL_RESPONSE, payload.dump(), responseTopic);

	WaitForResponse(responseTopic, [this](const std::string &mqttResponse) {
		json json_data = nlohmann::json::parse(mqttResponse.c_str());
		CMqtt::ResponseType response = json_data.get<CMqtt::ResponseType>();
		std::string requester = response.data.requester;
		std::string reason = response.data.reason;

		if(response.data.tTeam)
		{
			CMqtt::Team tTeam = *(response.data.tTeam);
			if(response.success)
			{
				std::vector<std::string> teamMembers = tTeam.members;
				std::string invitedPlayer;
				if(response.data.invitedPlayer)
				{
					invitedPlayer = *(response.data.invitedPlayer);
				}
				std::string teamname = tTeam.name;

				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					// inform the requester
					if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
					{
						GameContext()->SendChatTarget(i, reason.c_str());
					}

					// inform the invited player
					if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), invitedPlayer.c_str()) == 0)
					{
						GameContext()->SendChatTarget(i, ("You have been invited to a team by '" + requester + "'. Type '/taccept " + teamname + "' to accept.").c_str());
					}

					// inform the team members except the requester
					for(const auto &member : teamMembers)
					{
						if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), member.c_str()) == 0 && str_comp(Server()->ClientName(i), requester.c_str()) != 0)
						{
							GameContext()->SendChatTarget(i, ("Player '" + invitedPlayer + "' has been invited to the team.").c_str());
						}
					}
				}
			}
			else
			{
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
					{
						GameContext()->SendChatTarget(i, reason.c_str());
					}
				}
			}
		}
		else
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
				{
					GameContext()->SendChatTarget(i, reason.c_str());
				}
			}
		}
	});

	return true;
}

bool CMqtt::RequestTLeave(const int &clientId)
{
	if(!m_connected)
	{
		dbg_msg("mqtt", "Not connected to MQTT broker, cannot request TLeave");
		return false;
	}

	if(clientId < 0 || clientId >= MAX_CLIENTS)
	{
		return false;
	}
	if(m_pGameContext->m_apPlayers[clientId] == nullptr)
	{
		return false;
	}

	json payload;
	std::string responseTopic = GetChannelName(CHANNEL_RESPONSE) + "/" + RandomUuid();
	char pUsername[MAX_NAME_LENGTH];
	char pIp[NETADDR_MAXSTRSIZE];
	int pClientID = clientId;
	str_copy(pUsername, m_pServer->ClientName(clientId), sizeof(pUsername));
	m_pGameContext->Server()->GetClientAddr(pClientID, pIp, sizeof(pIp));

	payload["action"] = "TLeave";
	payload["clientid"] = pClientID;
	payload["username"] = pUsername;
	payload["responseTopic"] = responseTopic;
	payload["ip"] = pIp;
	PublishWithResponse(CHANNEL_RESPONSE, payload.dump(), responseTopic);

	WaitForResponse(responseTopic, [this](const std::string &mqttResponse) {
		json json_data = nlohmann::json::parse(mqttResponse.c_str());
		CMqtt::ResponseType response = json_data.get<CMqtt::ResponseType>();
		std::string requester = response.data.requester;
		std::string reason = response.data.reason;

		if(response.data.tTeam)
		{
			CMqtt::Team tTeam = *(response.data.tTeam);
			if(response.success)
			{
				std::vector<std::string> teamMembers = tTeam.members;
				std::string teamname = tTeam.name;

				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					// inform the requester
					if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
					{
						GameContext()->SendChatTarget(i, reason.c_str());
					}

					// inform the team members except the requester
					for(const auto &member : teamMembers)
					{
						if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), member.c_str()) == 0 && str_comp(Server()->ClientName(i), requester.c_str()) != 0)
						{
							GameContext()->SendChatTarget(i, ("Player '" + requester + "' has left the team.").c_str());
						}
					}
				}

				// TODO: Logic to announce the new leader
			}
			else
			{
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
					{
						GameContext()->SendChatTarget(i, reason.c_str());
					}
				}
			}
		}
		else
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
				{
					GameContext()->SendChatTarget(i, reason.c_str());
				}
			}
		}
	});

	return true;
}

bool CMqtt::RequestTAccept(const int &clientId, const std::string &teamname)
{
	try
	{
		if(!m_connected)
		{
			dbg_msg("mqtt", "Not connected to MQTT broker, cannot request TAccept");
			return false;
		}

		if(clientId < 0 || clientId >= MAX_CLIENTS)
		{
			return false;
		}
		if(m_pGameContext->m_apPlayers[clientId] == nullptr)
		{
			return false;
		}
		if(teamname.empty())
		{
			return false;
		}

		json payload;
		std::string responseTopic = GetChannelName(CHANNEL_RESPONSE) + "/" + RandomUuid();
		char pTeamname[128];
		char pUsername[MAX_NAME_LENGTH];
		char pIp[NETADDR_MAXSTRSIZE];
		int pClientID = clientId;
		str_copy(pTeamname, teamname.c_str(), sizeof(pTeamname));
		str_copy(pUsername, m_pServer->ClientName(clientId), sizeof(pUsername));
		m_pGameContext->Server()->GetClientAddr(pClientID, pIp, sizeof(pIp));

		payload["action"] = "TAccept";
		payload["clientid"] = pClientID;
		payload["teamname"] = pTeamname;
		payload["username"] = pUsername;
		payload["responseTopic"] = responseTopic;
		payload["ip"] = pIp;
		PublishWithResponse(CHANNEL_RESPONSE, payload.dump(), responseTopic);

		WaitForResponse(responseTopic, [this](const std::string &mqttResponse) {
			json json_data = nlohmann::json::parse(mqttResponse.c_str());
			CMqtt::ResponseType response = json_data.get<CMqtt::ResponseType>();
			std::string requester = response.data.requester;
			std::string reason = response.data.reason;

			if(response.data.tTeam)
			{
				CMqtt::Team tTeam = *(response.data.tTeam);
				if(response.success)
				{
					std::vector<std::string> teamMembers = tTeam.members;
					std::string teamname = tTeam.name;

					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						// inform the requester
						if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
						{
							GameContext()->SendChatTarget(i, reason.c_str());
						}

						// inform the team members except the requester
						for(const auto &member : teamMembers)
						{
							if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), member.c_str()) == 0 && str_comp(Server()->ClientName(i), requester.c_str()) != 0)
							{
								GameContext()->SendChatTarget(i, ("Player '" + requester + "' has joined the team.").c_str());
							}
						}
					}
				}
				else
				{
					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
						{
							GameContext()->SendChatTarget(i, reason.c_str());
						}
					}
				}
			}
			else
			{
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), requester.c_str()) == 0)
					{
						GameContext()->SendChatTarget(i, reason.c_str());
					}
				}
			}
		});

		return true;
	}
	catch(const std::exception &e)
	{
		dbg_msg("mqtt", "Exception occurred in RequestTAccept: %s", e.what());
		return false;
	}

	return true;
}

bool CMqtt::RequestTournementMode(std::string mode, int teamSize)
{
	if(!m_connected)
	{
		dbg_msg("mqtt", "Not connected to MQTT broker, cannot request TournementStart");
		return false;
	}

	json payload;
	std::string responseTopic = GetChannelName(CHANNEL_RESPONSE) + "/" + RandomUuid();
	char pTeamSize[128];
	str_format(pTeamSize, sizeof(pTeamSize), "%d", teamSize);

	payload["action"] = "TMode";
	payload["teamSize"] = pTeamSize;
	payload["responseTopic"] = responseTopic;
	payload["mode"] = mode;

	PublishWithResponse(CHANNEL_RESPONSE, payload.dump(), responseTopic);
	WaitForResponse(responseTopic, [this, mode](const std::string &mqttResponse) {
		try
		{
			json json_data = json::parse(mqttResponse);

			if(!json_data.is_object())
			{
				SendRconLine(-1, "Invalid JSON response format: root is not an object");
				return;
			}

			if(!json_data.contains("success") || !json_data["success"].is_boolean())
			{
				SendRconLine(-1, "Invalid JSON response format: missing or invalid 'success' key");
				return;
			}

			bool success = json_data["success"].get<bool>();
			std::string reason = "No reason provided";

			if(json_data.contains("data") && json_data["data"].is_object() &&
				json_data["data"].contains("reason") && json_data["data"]["reason"].is_string())
			{
				reason = json_data["data"]["reason"].get<std::string>();
			}

			std::string response;
			if(success)
			{
				response = "Successfully changed tournament mode to " + mode + "; Reason: " + reason;
			}
			else
			{
				response = "Failed to change tournament mode, reason: " + reason;
			}
			SendRconLine(-1, response.c_str());
		}
		catch(const nlohmann::json::exception &e)
		{
			std::string error_msg = "JSON parsing error: " + std::string(e.what());
			SendRconLine(-1, error_msg.c_str());
		}
	});

	return true;
}

void CMqtt::SendRconLine(int ClientId, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE, true);
	Msg.AddString(pLine, 512);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CMqtt::HandleMessage(const std::string &topic, const std::string &payload)
{
	try
	{
		if(!(str_comp_nocase(topic.c_str(), GetChannelName(CHANNEL_EXECUTE).c_str()) == 0))
		{
			return;
		}

		json response = json::parse(payload.c_str());
		const int type = response["type"].get<int>();

		dbg_msg("mqtt", "Received message of type %d", type);
		dbg_msg("mqtt", "Payload: %s", payload.c_str());
		switch(type)
		{
		case CHANNEL_RESPONSETYPE_RCON:
		{
			// Execute a console command
			const std::string data = response["data"].get<std::string>();
			Console()->ExecuteLine(data.c_str());
			break;
		}
		case CHANNEL_RESPONSETYPE_CHAT:
		{
			// Send a chat message
			const int cid = response["data"]["cid"].get<int>();
			const int team = response["data"]["team"].get<int>();
			const std::string message = response["data"]["message"].get<std::string>();
			GameContext()->SendChat(cid, team, message.c_str());
			break;
		}
		case CHANNEL_RESPONSETYPE_RESENDMAP:
		{
			// Resend the map
			m_rMapUpdate = true;
			break;
		}
		case CHANNEL_RESPONSETYPE_CREATETEAM:
		{
			// Create a team
			std::string teamname = response["data"]["name"].get<std::string>();
			std::string leader = response["data"]["leader"].get<std::string>();
			unsigned int teamSize = std::stoi(response["data"]["teamsize"].get<std::string>());
			std::vector<std::string> members = response["data"]["members"].get<std::vector<std::string>>();
			unsigned int teamnumber = response["data"]["teamnumber"].get<int>();

			if(teamSize != members.size())
			{
				GameContext()->SendChat(-1, TEAM_ALL, ("Team '" + teamname + "' is not valid. Wrong teamsize.").c_str());
				return;
			}

			// Check if every member is a valid player in the server
			for(const auto &member : members)
			{
				bool found = false;
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), member.c_str()) == 0)
					{
						found = true;
						break;
					}
				}
				if(!found)
				{
					GameContext()->SendChat(-1, TEAM_ALL, ("Team '" + teamname + "' is not valid. Player '" + member + "' not found.").c_str());
					return;
				}
			}

			auto *pController = GameContext()->m_pController;
			// Find empty team and add members and lock it
			if(teamnumber == -1)
			{
				GameContext()->SendChat(-1, TEAM_ALL, ("No empty team found for team '" + teamname + "'").c_str());
				return;
			}

			// Add members to team and lock team
			pController->Teams().SetTeamLock(teamnumber, true);
			for(const auto &member : members)
			{
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(m_pGameContext->m_apPlayers[i] && str_comp(Server()->ClientName(i), member.c_str()) == 0)
					{
						CCharacter *pChr = GameContext()->GetPlayerChar(i);
						CPlayer *pPlayer = GameContext()->m_apPlayers[i];
						CGameContext *pGameContext = (CGameContext *)m_pGameContext;
						pPlayer->KillCharacter(WEAPON_GAME);
						pController->Teams().SetForceCharacterTeam(i, teamnumber);
						m_inmovablePlayers.push_back(i);
						GameContext()->SendBroadcast(("You fight for team '" + teamname + "'\nYou have 10 Minutes time to\n1.Finish first\n2.Best time\n3.Most Finishes").c_str(), i);
					}
				}
			}

			break;
		}
		}
	}
	catch(const std::exception &e)
	{
		dbg_msg("mqtt", "Error handling message: %s", e.what());
	}
}

void CMqtt::Simulate(CPlayer *pPlayer)
{
	// Funktion in einem separaten Thread ausführen
	std::thread simulationThread([this, pPlayer]() {
		// reset
		this->RequestTournementMode("reset", 1);
		std::this_thread::sleep_for(std::chrono::seconds(1));

		// Erstelle Turnier
		this->RequestTournementMode("create", 1);
		std::this_thread::sleep_for(std::chrono::seconds(1));

		this->RequestTournementMode("pre", 1);
		std::this_thread::sleep_for(std::chrono::seconds(1));

		// Teams zuweisen
		this->RequestTJoin(0, "Team1");
		std::this_thread::sleep_for(std::chrono::seconds(1));

		this->RequestTJoin(1, "Team2");
		std::this_thread::sleep_for(std::chrono::seconds(1));

		// Turnier starten
		this->RequestTournementMode("assign", 1);
		std::this_thread::sleep_for(std::chrono::seconds(1));

		this->RequestTournementMode("start", 1);
	});

	// Optional: Warten, bis der Thread fertig ist
	simulationThread.join();
}

json CMqtt::SerializeMapInfo()
{
	json mapInfo;
	mapInfo["sv_map"] = g_Config.m_SvMap;

	mapInfo["map"]["game"] = json::array();
	mapInfo["map"]["front"] = json::array();
	for(int y = 0; y < m_pGameContext->Collision()->GetHeight(); ++y)
	{
		mapInfo["map"]["game"][y] = json::array();
		mapInfo["map"]["front"][y] = json::array();
		for(int x = 0; x < m_pGameContext->Collision()->GetWidth(); ++x)
		{
			mapInfo["map"]["game"][y][x] = m_pGameContext->Collision()->GetIndex(x, y);
			mapInfo["map"]["front"][y][x] = m_pGameContext->Collision()->GetFIndex(x, y);
		}
	}
	return mapInfo;
}

json CMqtt::SerializePlayer(CPlayer *pPlayer, int JID)
{
	CCharacter *pChar = pPlayer->GetCharacter();
	json data;
	char aBuf[64];
	str_format(aBuf, 64, "%d", JID);
	data[aBuf]["cid"] = pPlayer->GetCid();
	data[aBuf]["team"] = pPlayer->GetTeam();
	data[aBuf]["name"] = m_pServer->ClientName(pPlayer->GetCid());
	data[aBuf]["clan"] = m_pServer->ClientClan(pPlayer->GetCid());
	data[aBuf]["clientversion"] = pPlayer->GetClientVersion();
	data[aBuf]["afk"] = pPlayer->IsAfk();
	data[aBuf]["moderating"] = pPlayer->m_Moderating;

	data[aBuf]["teeinfo"]["skinname"] = pPlayer->m_TeeInfos.m_aSkinName;
	data[aBuf]["teeinfo"]["usecustomcolor"] = pPlayer->m_TeeInfos.m_UseCustomColor;
	data[aBuf]["teeinfo"]["colorbody"] = pPlayer->m_TeeInfos.m_ColorBody;
	data[aBuf]["teeinfo"]["colorfeet"] = pPlayer->m_TeeInfos.m_ColorFeet;

	data[aBuf]["latency"]["accum"] = pPlayer->m_Latency.m_Accum;
	data[aBuf]["latency"]["accummax"] = pPlayer->m_Latency.m_AccumMax;
	data[aBuf]["latency"]["accummin"] = pPlayer->m_Latency.m_AccumMin;
	data[aBuf]["latency"]["avg"] = pPlayer->m_Latency.m_Avg;
	data[aBuf]["latency"]["max"] = pPlayer->m_Latency.m_Max;
	data[aBuf]["latency"]["min"] = pPlayer->m_Latency.m_Min;
	data[aBuf]["ischaracter"] = (pChar != nullptr);

	if(pChar)
	{
		data[aBuf]["character"]["input"]["direction"] = (int)pChar->Core()->m_Input.m_Direction;
		data[aBuf]["character"]["input"]["fire"] = (int)pChar->Core()->m_Input.m_Fire;
		data[aBuf]["character"]["input"]["hook"] = (int)pChar->Core()->m_Input.m_Hook;
		data[aBuf]["character"]["input"]["jump"] = (int)pChar->Core()->m_Input.m_Jump;
		data[aBuf]["character"]["input"]["nextweapon"] = (int)pChar->Core()->m_Input.m_NextWeapon;
		data[aBuf]["character"]["input"]["playerflags"] = (int)pChar->Core()->m_Input.m_PlayerFlags;
		data[aBuf]["character"]["input"]["prevweapon"] = (int)pChar->Core()->m_Input.m_PrevWeapon;
		data[aBuf]["character"]["input"]["targetx"] = (int)pChar->Core()->m_Input.m_TargetX;
		data[aBuf]["character"]["input"]["targety"] = (int)pChar->Core()->m_Input.m_TargetY;
		data[aBuf]["character"]["input"]["wantedweapon"] = (int)pChar->Core()->m_Input.m_WantedWeapon;

		data[aBuf]["character"]["activeweapon"] = (int)pChar->Core()->m_ActiveWeapon;
		data[aBuf]["character"]["freeze_start"] = (int)pChar->Core()->m_FreezeStart;
		data[aBuf]["character"]["freeze_end"] = (int)pChar->Core()->m_FreezeEnd;
		data[aBuf]["character"]["jumped"] = (int)pChar->Core()->m_Jumped;
		data[aBuf]["character"]["solo"] = (int)pChar->Core()->m_Solo;

		data[aBuf]["character"]["pos"]["x"] = (int)pChar->Core()->m_Pos.x;
		data[aBuf]["character"]["pos"]["y"] = (int)pChar->Core()->m_Pos.y;
		data[aBuf]["character"]["vel"]["x"] = (int)pChar->Core()->m_Vel.x;
		data[aBuf]["character"]["vel"]["y"] = (int)pChar->Core()->m_Vel.y;
		data[aBuf]["character"]["hookpos"]["x"] = (int)pChar->Core()->m_HookPos.x;
		data[aBuf]["character"]["hookpos"]["y"] = (int)pChar->Core()->m_HookPos.y;
		data[aBuf]["character"]["hookstate"] = (int)pChar->Core()->m_HookState;
	}
	return data;
}

json CMqtt::SerializeServer()
{
	json result;
	int playerCount = 0;

	result["sv_name"] = g_Config.m_SvName;
	result["sv_map"] = g_Config.m_SvMap;
	result["tick"] = m_pServer->Tick();

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_pGameContext->m_apPlayers[i])
		{
			result["players"].merge_patch(SerializePlayer(m_pGameContext->m_apPlayers[i], playerCount));
			++playerCount;
		}
	}
	result["playercount"] = playerCount;

	result["config"]["sv_warmup"] = g_Config.m_SvName;
	result["config"]["sv_motd"] = g_Config.m_SvMotd;
	result["config"]["sv_tournament_mode"] = g_Config.m_SvTournamentMode;
	result["config"]["sv_spamprotection"] = g_Config.m_SvSpamprotection;
	result["config"]["sv_spectator_slots"] = g_Config.m_SvSpectatorSlots;
	result["config"]["sv_inactivekick_time"] = g_Config.m_SvInactiveKickTime;
	result["config"]["sv_inactivekick"] = g_Config.m_SvInactiveKick;
	result["config"]["sv_strict_spectate_mode"] = g_Config.m_SvStrictSpectateMode;
	result["config"]["sv_vote_spectate"] = g_Config.m_SvVoteSpectate;
	result["config"]["sv_vote_spectate_rejoindelay"] = g_Config.m_SvVoteSpectateRejoindelay;
	result["config"]["sv_vote_kick"] = g_Config.m_SvVoteKick;
	result["config"]["sv_vote_kick_min"] = g_Config.m_SvVoteKickMin;
	result["config"]["sv_vote_kick_bantime"] = g_Config.m_SvVoteKickBantime;
	result["config"]["sv_join_vote_delay"] = g_Config.m_SvJoinVoteDelay;
	result["config"]["sv_old_teleport_weapons"] = g_Config.m_SvOldTeleportWeapons;
	result["config"]["sv_old_teleport_hook"] = g_Config.m_SvOldTeleportHook;
	result["config"]["sv_teleport_hold_hook"] = g_Config.m_SvTeleportHoldHook;
	result["config"]["sv_teleport_lose_weapons"] = g_Config.m_SvTeleportLoseWeapons;
	result["config"]["sv_deepfly"] = g_Config.m_SvDeepfly;
	result["config"]["sv_destroy_bullets_on_death"] = g_Config.m_SvDestroyBulletsOnDeath;
	result["config"]["sv_destroy_lasers_on_death"] = g_Config.m_SvDestroyLasersOnDeath;
	result["config"]["sv_mapupdaterate"] = g_Config.m_SvMapUpdateRate;
	result["config"]["sv_server_type"] = g_Config.m_SvServerType;
	result["config"]["sv_send_votes_per_tick"] = g_Config.m_SvSendVotesPerTick;
	result["config"]["sv_rescue"] = g_Config.m_SvRescue;
	result["config"]["sv_rescue_delay"] = g_Config.m_SvRescueDelay;
	result["config"]["sv_practice"] = g_Config.m_SvPractice;
	result["config"]["sv_sid"] = g_Config.m_SvSID;

	return result;
}

#endif
