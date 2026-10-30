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
#define AWS_IOT_PUBLISH_TOPIC "/6"  // Topic for publishing sensor data
#define AWS_IOT_SUBSCRIBE_TOPIC \
  "/downlink"  // Topic for receiving commands from AWS

long sendInterval = 60000;  // Default interval (in milliseconds) at which to
                            // send telemetry to AWS

char
  THINGNAME[13];  // 12 hex digits from the MAC address, plus a null terminator

void setThingName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(THINGNAME, sizeof(THINGNAME), "%02X%02X%02X%02X%02X%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Initialize secure WiFi client and MQTT client with 1024 byte buffer for
// messages
WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(1024);

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
  Serial.println();

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
    delay(100);
  }
  Serial.println();

  // Verify connection was successful
  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }
  Serial.println();

  // Subscribe to the downlink topic to receive commands from AWS
  char topic[64];
  snprintf(topic, sizeof(topic), "%s%s", THINGNAME, AWS_IOT_SUBSCRIBE_TOPIC);
  client.subscribe(topic);

  Serial.println("AWS IoT Connected!");
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

// Callback function that handles incoming MQTT messages
void messageHandler(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);

  // Parse the JSON payload
  JsonDocument doc;
  deserializeJson(doc, payload);
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

// Arduino setup function - runs once at startup
void setup() {
  delay(5000);
  Serial.begin(115200);  // Initialize serial communication for debugging
  delay(5000);           // Wait for serial monitor to connect
  connectAWS();          // Establish connection to AWS IoT Core
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
  }

  // Process incoming MQTT messages and maintain connection
  client.loop();
}