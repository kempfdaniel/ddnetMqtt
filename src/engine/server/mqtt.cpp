// mqtt.cpp

#include "mqtt.h"

#include <atomic>
#include <base/system.h>
#include <game/server/entities/character.h>
#include <game/server/player.h>
#include <thread>

#define CONF_MQTTSERVICES

#ifdef CONF_MQTTSERVICES

IMqtt *CreateMqtt(const std::string &address, const std::string &clientID) { return new CMqtt(address, clientID); }

CMqtt::CMqtt(const std::string &address, const std::string &clientID) :
	m_pServer(0), m_pConsole(0), m_pGameServer(0), client_(address, clientID), connOpts_(), m_lastHeartBeat(0), m_rMapUpdate(true), m_rHeartbeat(true), m_connected(false)
{
	connOpts_.set_keep_alive_interval(20);
	connOpts_.set_clean_session(true);
	dbg_msg("mqtt", "MQTT service initialized");
}

CMqtt::~CMqtt()
{
	client_.disconnect()->wait();
}

void CMqtt::Init()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGameServer = Kernel()->RequestInterface<IGameServer>();
	m_pGameContext = (CGameContext *)Kernel()->RequestInterface<IGameServer>();
	prefix = "ddnet/" + std::string(g_Config.m_SvSID);
	dbg_msg("mqtt", "Connecting to MQTT server over interface");
	client_.connect(connOpts_)->wait();

	client_.set_message_callback([this](mqtt::const_message_ptr msg) {
		const std::string topic = msg->get_topic();
		const std::string payload = msg->to_string();

		if(m_responseCallbacks.find(topic) != m_responseCallbacks.end())
		{
			// Call the callback for this response topic
			m_responseCallbacks[topic](payload);

			// Optionally unsubscribe from the response topic
			client_.unsubscribe(topic)->wait();

			// Remove the callback from the map
			m_responseCallbacks.erase(topic);
		}

		HandleMessage(topic, payload);
	});

	Subscribe(CHANNEL_RESPONSE);

	m_connected = true;
	dbg_msg("mqtt", "Connected to the MQTT broker");
	Publish(CHANNEL_SERVER, std::string("Connected to the MQTT broker"));
}

void CMqtt::Run()
{
	while(1)
	{
		if(m_rMapUpdate)
		{
			Publish(CHANNEL_MAP, SerializeMapInfo());
			m_rMapUpdate = false;
		}

		if(m_rHeartbeat)
		{
			/// Publish(CHANNEL_SERVERINFO, SerializeServer());
			m_rHeartbeat = false;
		}

		if(m_connected && !m_missedMessages.empty())
		{
			for(auto &message : m_missedMessages)
			{
				Publish(message.first, message.second);
			}
			m_missedMessages.clear();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

void CMqtt::Subscribe(const int &topic)
{
	client_.subscribe(GetChannelName(topic), 1)->wait();
}

void CMqtt::Publish(const int &topic, const std::string &payload)
{
	if(!m_connected)
	{
		m_missedMessages.emplace(topic, payload);
		return;
	}

	// dbg_msg("mqtt", "Publishing to topic: %s, payload: %s", GetChannelName(topic).c_str(), payload.c_str());
	client_.publish(GetChannelName(topic), payload.c_str(), payload.size(), 1, false)->wait();
}

void CMqtt::Publish(const int &topic, const json &payload)
{
	if(!m_connected)
	{
		m_missedMessages.emplace(topic, payload);
		return;
	}

	json payloadEx = payload;
	payloadEx["tick"] = Server()->Tick();
	// dbg_msg("mqtt", "Publishing to topic: %s, payload: %s", GetChannelName(topic).c_str(), payloadEx.dump().c_str());
	client_.publish(GetChannelName(topic), payloadEx.dump().c_str(), payloadEx.dump().size(), 1, false);
}

void CMqtt::PublishWithResponse(const int &topic, const std::string &payload, const std::string &responseTopic)
{
	// Subscribe to the response topic
	client_.subscribe(responseTopic, 1)->wait();

	// Add a callback to handle the response
	m_responseCallbacks[responseTopic] = [](const std::string &response) {
		// Handle the response (e.g., log it or process it)
		dbg_msg("mqtt", "Received response: %s", response.c_str());
	};

	// Publish the message
	client_.publish(GetChannelName(topic), payload.c_str(), payload.size(), 1, false)->wait();
}

void CMqtt::WaitForResponse(const std::string &responseTopic, std::function<void(const std::string &)> callback)
{
	// Store the callback for the response topic
	m_responseCallbacks[responseTopic] = callback;
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

void CMqtt::RequestLogin(const int &clientId, const std::string &logintoken)
{
	if(clientId < 0 || clientId >= MAX_CLIENTS)
	{
		return;
	}
	if(m_pGameContext->m_apPlayers[clientId] == nullptr)
	{
		return;
	}
	if(logintoken.empty())
	{
		return;
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
		json jsonResponse = json::parse(response);
		//TODO: Handle the response
		if(jsonResponse["status"] == "success")
		{
			dbg_msg("mqtt", "Login successful");
		}
		else
		{
			dbg_msg("mqtt", "Login failed");
		}
	});
}

void CMqtt::HandleMessage(const std::string &topic, const std::string &payload)
{
	if(!(str_comp_nocase(topic.c_str(), GetChannelName(CHANNEL_RESPONSE).c_str()) == 0))
	{
		return;
	}

	json response = json::parse(payload);
	const int type = response["type"].get<int>();

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
	}
	default:
		dbg_msg("mqtt", "Unhandled type received in JSON response.");
		break;
	}
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
