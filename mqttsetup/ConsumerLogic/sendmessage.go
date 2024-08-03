package main

import (
	"fmt"
	"log"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

func main() {
	/*
	 * Same as in the consumer.go file, the broker, clientID, and topic are set up here
	 * Look at the consumer.go file for more information on the broker, clientID, and topic
	 */
	broker := "tcp://localhost:1883"
	clientID := "DDNETSender"
	topic := "ddnet/FMS/response"

	// Set up a new MQTT client options
	opts := mqtt.NewClientOptions()
	opts.AddBroker(broker)
	opts.SetUsername("admin")  // Set the username for the MQTT broker
	opts.SetPassword("instar") // Set the password for the MQTT broker
	opts.SetClientID(clientID)
	opts.SetDefaultPublishHandler(func(client mqtt.Client, msg mqtt.Message) {
		log.Printf("Received confirmation[%s]: %s\n", msg.Topic(), msg.Payload())
	})

	// Connect to the MQTT broker
	client := mqtt.NewClient(opts)
	if token := client.Connect(); token.Wait() && token.Error() != nil {
		log.Fatalf("Error connecting to broker: %v", token.Error())
	}
	defer client.Disconnect(250)
	fmt.Println("Connected to the MQTT broker")

	/**
	"type": choose from CHANNEL_RESPONSETYPE_* constants in src/engine/mqtt.h (currently 0, 1, 2) and find the needed data fields in src/engine/server/mqtt.cpp
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
		CHANNEL_INGAME,
		CHANNEL_RESPONSETYPE_RCON = 0,
		CHANNEL_RESPONSETYPE_CHAT,
		CHANNEL_RESPONSETYPE_RESENDMAP
	};
	**/

	/** USE THE CODE BELOW TO SEND A MESSAGE TO THE MQTT BROKER **/
	// Create message from user input
	//reader := bufio.NewReader(os.Stdin)
	//fmt.Println("What message do you want to send?")
	//message, _ := reader.ReadString('\n')
	//jsonResponse := fmt.Sprintf(`{"type": 1, "data": {"cid": -1, "team": -2, "message": "%s"}}`, message) // Write the message into the ingame chat

	// Current test messages to send
	jsonResponse := fmt.Sprintf(`{"type": 0, "data": "broadcast Hello, World!"}`) // rcon command
	//jsonResponse := fmt.Sprintf(`{"type": 1, "data": {"cid": -1, "team": -2, "message": "Hello, World!"}}`) // Write the message into the ingame chat as server
	//jsonResponse := fmt.Sprintf(`{"type": 3, "data": {"cid": 0, "team": -2, "message": "Hello, World!"}}`) // Write the message into the ingame chat as player with client ID 0
	//jsonResponse := fmt.Sprintf(`{"type": 2`) // Resend the map to the client
	//
	token := client.Publish(topic, 1, false, jsonResponse)
	token.Wait()
	fmt.Printf("Published message to %s: %s\n", topic, jsonResponse)
}
