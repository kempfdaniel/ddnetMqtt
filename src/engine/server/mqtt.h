// mqtt.h
#ifndef ENGINE_SERVER_MQTT_H
#define ENGINE_SERVER_MQTT_H

#include <string>
#include <mqtt/async_client.h>
#include <engine/mqtt.h>
#include <game/server/gamecontext.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/server.h>
#include <engine/config.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
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

    std::unique_ptr<mqtt::async_client> client_; // Verwenden Sie unique_ptr für den Client
    mqtt::connect_options connOpts_;

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

    /* SETTER AND GETTER */
    void SetMapUpdate(bool update) override { m_rMapUpdate = update; }
    void SetHeartbeat(bool update) override { m_rHeartbeat = update; }
    void SetServerUpdate(bool update) override { m_rServerUpdate = update; }
    void SetLastHeartbeat(int lastHeartBeat) override { m_lastHeartBeat = lastHeartBeat; }

    int GetLastHeartbeat() override { return m_lastHeartBeat; }

    /* MQTT FUNCTIONS */
    void Run() override;
    void Init() override;
    bool Subscribe(const int& topic) override;
    bool Publish(const int& topic, const std::string& payload) override;
    bool Publish(const int& topic, const json& payload) override;
    bool PublishWithResponse(const int& topic, const std::string& payload, const std::string& responseTopic) override;
    bool WaitForResponse(const std::string& responseTopic, std::function<void(const std::string&)> callback) override;

    /* EXTRA FUNCTIONS */
    std::string GetChannelName(int channel);
    std::string RandomUuid();

    bool RequestLogin(const int& clientId, const std::string& logintoken) override;

private:
    std::unordered_map<std::string, std::function<void(const std::string&)>> m_responseCallbacks;
    bool m_connected;
    std::unordered_map<int, json> m_missedMessages;

    json SerializeMapInfo();
    json SerializeServer();
    json SerializePlayer(CPlayer *pPlayer, int JID);
    void HandleMessage(const std::string& topic, const std::string& payload);
};

#endif // ENGINE_SERVER_MQTT_H
