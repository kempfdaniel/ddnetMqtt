package main

import (
	"fmt"
	"log"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

func main() {
	// Define the MQTT broker URL and settings
	broker := "tcp://broker.hivemq.com:1883"
	clientID := "DDNETSender"
	topic := "ddnet/FMS/response"

	// Set up a new MQTT client options
	opts := mqtt.NewClientOptions()
	opts.AddBroker(broker)
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

	// Create message from user input
	//reader := bufio.NewReader(os.Stdin)
	//fmt.Println("What message do you want to send?")
	//message, _ := reader.ReadString('\n')

	// Construct the JSON response as required
	jsonResponse := fmt.Sprintf(`{"type": 1, "data": {"cid": -1, "team": -2, "message": "Hello, World!"}}`)
	token := client.Publish(topic, 1, false, jsonResponse)
	token.Wait()
	fmt.Printf("Published message to %s: %s\n", topic, jsonResponse)
}
