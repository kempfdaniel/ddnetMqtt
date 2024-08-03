package main

import (
	"fmt"
	"log"
	"os"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

func main() {
	broker := "tcp://localhost:1883" // Mqtt broker address
	clientID := "DDNETConsumer"      // Client ID to be used when connecting to the broker
	topic := "ddnet/*"               // Topic to subscribe to (can be a pattern)
	/**
	 * The topic can be a pattern, for example:
	 * Explanation for the patterns: *, +, #
	 * - *: Matches a single level in the topic hierarchy
	 * - +: Matches one or more levels in the topic hierarchy
	 * - #: Matches zero or more levels in the topic hierarchy
	 * For example:
	 * - ddnet/*: Matches all topics under ddnet, such as ddnet/FMS/response
	 * - ddnet/+/response: Matches all topics under ddnet with a response subtopic, such as ddnet/FMS/response
	 * - ddnet/#: Matches all topics under ddnet, such as ddnet/FMS/response and ddnet/FMS/request
	 */

	// Create a new MQTT client options
	opts := mqtt.NewClientOptions()
	opts.AddBroker(broker)
	opts.SetClientID(clientID)
	opts.SetCleanSession(false) // Set to false to receive messages sent while offline -> Set to true to ignore messages sent while offline
	opts.SetUsername("admin")   // Set the username for the MQTT broker
	opts.SetPassword("instar")  // Set the password for the MQTT broker

	/**
	 * Set up a default handler for messages received on the subscribed topics
	 * This handler will be called for all messages received on the subscribed topics
	 */
	opts.SetDefaultPublishHandler(func(client mqtt.Client, msg mqtt.Message) {
		log.Printf("Received message[%s]: %s\n", msg.Topic(), string(msg.Payload()))
	})

	// Handle lost connection
	opts.SetConnectionLostHandler(func(client mqtt.Client, err error) {
		log.Printf("Connection lost: %v", err)
		os.Exit(1) // Exit the program, can be replaced with a reconnection logic
	})

	// Connect to the MQTT broker
	client := mqtt.NewClient(opts)
	if token := client.Connect(); token.Wait() && token.Error() != nil {
		log.Fatalf("Error connecting to broker: %v", token.Error())
	}
	defer client.Disconnect(250)
	fmt.Println("Connected to the MQTT broker")

	/*
	 * Subscribe to the topic to receive messages published on it by other clients
	 * The QoS level can be set to 0, 1, or 2
	 * - QoS 0: At most once delivery 	-> The message is sent once without any confirmation
	 * - QoS 1: At least once delivery 	-> The message is sent at least once and confirmed by the receiver
	 * - QoS 2: Exactly once delivery 	-> The message is sent exactly once by using a four-step handshake
	 * Ack and auto-ack can be used to handle the message delivery
	 * - Ack: Acknowledge the message after processing
	 * - Auto-ack: Automatically acknowledge the message after processing
	 */

	// Subscribe to the topic with QoS level 1 to ensure message delivery at least once
	if token := client.Subscribe(topic, 1, nil); token.Wait() && token.Error() != nil {
		log.Fatalf("Error subscribing to topic: %v", token.Error())
	}
	fmt.Println("Subscribed to topic:", topic)

	// Keep the main goroutine alive to continue receiving messages
	sigChan := make(chan os.Signal, 1)
	<-sigChan // Wait for termination signal
	// Note: No need to manually disconnect, defer will handle it unless the program exits due to a signal
}
