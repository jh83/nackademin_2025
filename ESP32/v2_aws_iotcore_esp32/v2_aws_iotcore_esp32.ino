// Requires "ESP32 by Espressif Systems" under boards manager
// or "Arduino ESP32 Boards"

// Include required libraries for WiFi, secure connections, MQTT, and JSON
// parsing
#include "secrets.h"
#include "WiFi.h"
//#include "secrets_real.h"
#include <ArduinoJson.h>  // Library > ArduinoJson Benoit Blanchon
#include <MQTTClient.h>   // Library > MQTT by Joel Gaehwiler
#include <WiFiClientSecure.h>

// The MQTT topics that this device should publish/subscribe
#define AWS_IOT_PUBLISH_TOPIC "/telemetry"   // Topic for publishing sensor data
#define AWS_IOT_SUBSCRIBE_TOPIC "/downlink"  // Topic for receiving commands from AWS

#define SEND_PIN 0  // Pin to trigger sending telemetry to AWS

long sendInterval = 60000;  // Default interval (in milliseconds) at which to
                            // send telemetry to AWS

volatile bool sendOnInterrupt = false;

char
  THINGNAME[13];  // 12 hex digits from the MAC address, plus a null terminator

// Initialize secure WiFi client and MQTT client with 1024 byte buffer for
// messages
WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(1024);

void setThingName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(THINGNAME, sizeof(THINGNAME), "%02X%02X%02X%02X%02X%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool subscribeToTopic(const char *topic) {
  Serial.print("Subscribing to: ");
  Serial.println(topic);
  if (!client.subscribe(topic)) {
    Serial.print("Subscribe failed: ");
    Serial.println(topic);
    Serial.println("Restarting in 30s");
    delay(30000);
    ESP.restart();
  }
  Serial.println("Subscribed");
  return true;
}

// Updates device settings based on shadow document and reports back to AWS
void updateSettings(JsonDocument settingsObj) {

  Serial.println("Received DeviceShadow document: ");
  serializeJson(settingsObj, Serial);
  Serial.println();

  // Update send interval if present in shadow document (convert seconds to
  // milliseconds)
  if (settingsObj.containsKey("sendInterval")) {
    sendInterval = settingsObj["sendInterval"].as<int>();
    Serial.println("Send interval updated to: " + String(sendInterval / 1000) + " seconds");
  }

  // Create response document to report current state back to shadow
  JsonDocument docResponse;
  docResponse["state"]["reported"] =
    settingsObj;  // Report the received settings as current state
  char jsonBuffer[512];
  serializeJson(docResponse, jsonBuffer);

  // Report back to device shadow to acknowledge the settings were applied
  Serial.print("Sending reported state to AWS: ");
  serializeJson(docResponse, Serial);
  Serial.println();

  char topic[64];
  snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/update", THINGNAME);
  client.publish(topic, jsonBuffer);
}

// Callback function that handles incoming MQTT messages
void messageHandler(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);

  // Parse the JSON payload
  JsonDocument doc;
  deserializeJson(doc, payload);

  // Handle device shadow messages
  if (topic.endsWith("/shadow/get/accepted")) {
    // Received accepted shadow get response - extract desired state
    updateSettings(doc["state"]["desired"]);
  } else if (topic.endsWith("/shadow/update/delta")) {
    // Received shadow delta update - extract the changed state
    updateSettings(doc["state"]);
  } else if (topic.endsWith("/shadow/get/rejected")) {
    Serial.print("Shadow get rejected: ");
    Serial.print(doc["code"].as<int>());
    Serial.print(" ");
    Serial.println(doc["message"].as<const char *>());
  }
}

// Establishes connection to AWS IoT Core via WiFi and MQTT
void connectAWS() {
  // Set WiFi to station mode (client mode, not access point)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  setThingName();

  Serial.println();
  Serial.print("MAC Address: ");
  Serial.println(THINGNAME);

  Serial.println("Connecting to Wi-Fi");

  // Wait until WiFi connection is established
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("-");
  }

  // Configure WiFiClientSecure to use the AWS IoT device credentials for mutual
  // TLS authentication
  net.setCACert(AWS_CERT_CA);           // Root CA certificate
  net.setCertificate(AWS_CERT_CRT);     // Device certificate
  net.setPrivateKey(AWS_CERT_PRIVATE);  // Device private key

  // Connect to the MQTT broker on the AWS endpoint (port 8883 is the standard
  // MQTT over TLS port)
  client.begin(AWS_IOT_ENDPOINT, 8883, net);

  char tlsError[100];
  if (net.connect(AWS_IOT_ENDPOINT, 8883)) {
    Serial.println("TLS verification succeeded");
  } else {
    int code = net.lastError(tlsError, sizeof(tlsError));
    Serial.printf("TLS verification failed (%d): %s\n", code, tlsError);
    time_t now = time(nullptr);
    Serial.print("Device time: ");
    Serial.println(ctime(&now));
    return;
  }

  // Register callback function to handle incoming MQTT messages
  client.onMessage(messageHandler);

  Serial.print("Connecting to AWS IOT");

  // Attempt to connect to AWS IoT using the thing name as client ID
  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  // Verify connection was successful
  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }
  Serial.println();
  Serial.println("AWS IoT Connected!");

  // Subscribe to the downlink topic to receive commands from AWS
  char topic[64];
  snprintf(topic, sizeof(topic), "%s%s", THINGNAME, AWS_IOT_SUBSCRIBE_TOPIC);
  subscribeToTopic(topic);

  // Connected
  Serial.println("AWS Subscribed!");
}

// Configures AWS IoT Device Shadow subscriptions and requests current shadow
// state
void setupShadow() {
  char topic[64];

  // Subscribe to shadow topics to receive shadow updates
  snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/get/accepted", THINGNAME);
  subscribeToTopic(topic);
  snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/get/rejected", THINGNAME);
  subscribeToTopic(topic);
  snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/update/delta", THINGNAME);
  subscribeToTopic(topic);

  // Request the current device shadow state from AWS
  snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/get", THINGNAME);
  client.publish(topic);
}

// Publishes telemetry data to AWS IoT Core
// Returns true if publish was successful, false otherwise
bool publishTelemetry(String payload) {
  Serial.print("Publishing: ");
  Serial.println(payload);
  char topic[64];
  snprintf(topic, sizeof(topic), "%s%s", THINGNAME, AWS_IOT_PUBLISH_TOPIC);
  return client.publish(topic, payload);
}

bool sendTelemetry() {
  // Publish simulated sensor data (temperature and humidity) as JSON
  // Note: random() with float arguments may not work as expected - consider
  // using random(15, 30) for integers
  Serial.println("Sending telemetry to AWS");
  bool sendResult =
    publishTelemetry("{\"temperature\":" + String(random(15.0, 30.0)) + ",\"humidity\":" + String(random(50, 90)) + "}");

  return sendResult;
}

void IRAM_ATTR interruptISR() {
  sendOnInterrupt = true;
}

// Arduino setup function - runs once at startup
void setup() {
  pinMode(SEND_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SEND_PIN), interruptISR, FALLING);
  Serial.begin(115200);  // Initialize serial communication for debugging
  delay(2000);           // Wait for serial monitor to connect
  connectAWS();          // Establish connection to AWS IoT Core
  setupShadow();         // Configure and request device shadow
}

// Arduino main loop - runs continuously
void loop() {
  // Use static variable to track last send time (initialized to allow immediate
  // first send)
  static unsigned long previousMillis = millis() - sendInterval;

  // Check if it's time to send telemetry data
  if (millis() - previousMillis >= sendInterval) {
    // Update the last send time
    previousMillis = millis();

    // Send telemetry to AWS
    bool sendResult = sendTelemetry();
    if (sendResult == false) {
      Serial.println("Loop failed to send telemetry to AWS");
      Serial.println("Restarting ESP32");
      ESP.restart();
    }
  } else if (sendOnInterrupt) {
    Serial.println("Triggered");
    sendTelemetry();
    sendOnInterrupt = false;
  }

  // Process incoming MQTT messages and maintain connection
  client.loop();
}