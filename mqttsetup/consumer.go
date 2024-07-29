package main

import (
	"fmt"
	"log"
	"os"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

func main() {
	// Define the MQTT broker URL
	broker := "tcp://localhost:1883"
	clientID := "DDNETConsumer"
	topic := "ddnet/#"

	// Create a new MQTT client options
	opts := mqtt.NewClientOptions()
	opts.AddBroker(broker)
	opts.SetClientID(clientID)
	opts.SetCleanSession(false) // Set to false to receive messages sent while offline
	opts.SetUsername("admin")
	opts.SetPassword("instar")
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
