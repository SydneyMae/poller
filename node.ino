#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <WebServer.h>

const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";

const char* deviceName = "Friendly-Name";
IPAddress local_IP(192, 168, 1, 200);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

const char* webhookUrl = "https://discord.com/api/webhooks/XXX/YYY";

#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

WebServer server(80);

float lastTemp = 0.0;
float lastHum = 0.0;
unsigned long lastRead = 0;
unsigned long lastPeerCheck = 0;

bool alertSentLow = false;
bool alertSentHigh = false;
bool alertSentSensor = false;

struct Peer {
  const char* name;
  const char* ip;
  bool isDown;
};

Peer peers[] = {
  {"Kitchen", "192.168.1.201", false},
  {"Office",  "192.168.1.202", false},
  {"Garage",  "192.168.1.203", false}
};
const int peerCount = sizeof(peers) / sizeof(peers[0]);

void sendDiscordAlert(String message) {
  HTTPClient http;
  http.begin(webhookUrl);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"content\": \"" + message + "\"}";
  int code = http.POST(payload);

  if (code > 0)
    Serial.printf("Webhook sent: %d\n", code);
  else
    Serial.printf("Webhook failed: %s\n", http.errorToString(code).c_str());

  http.end();
}

void handleTemp() {
  String json = "{";
  json += "\"temperature\":" + String(lastTemp, 1) + ",";
  json += "\"humidity\":" + String(lastHum, 1);
  json += "}";
  server.send(200, "application/json", json);
}

void handlePeers() {
  String json = "{";
  json += "\"name\": \"" + String(deviceName) + "\",";
  json += "\"ip\": \"" + WiFi.localIP().toString() + "\",";
  json += "\"online\": true";
  json += "}";
  server.send(200, "application/json", json);
}

void checkPeers() {
  for (int i = 0; i < peerCount; i++) {
    HTTPClient http;
    String url = "http://" + String(peers[i].ip) + "/peers";
    http.begin(url);
    int code = http.GET();

    if (code == 200) {
      if (peers[i].isDown) {
        sendDiscordAlert("Peer *" + String(peers[i].name) + "* is back online at " + String(peers[i].ip));
        peers[i].isDown = false;
      }
    } else {
      if (!peers[i].isDown) {
        sendDiscordAlert("Peer *" + String(peers[i].name) + "* is offline at " + String(peers[i].ip));
        peers[i].isDown = true;
      }
    }

    http.end();
    delay(100);
  }
}

void setup() {
  Serial.begin(115200);

  if (!WiFi.config(local_IP, gateway, subnet))
    Serial.println("Could not configure static IP");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n Connected! IP: " + WiFi.localIP().toString());

  dht.begin();
  server.on("/temp", handleTemp);
  server.on("/peers", handlePeers);
  server.begin();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    WiFi.disconnect();
    WiFi.reconnect();
    delay(1000);
    return;
  }

  server.handleClient();

  if (millis() - lastRead > 15000) {
    float tempC = dht.readTemperature();
    float hum = dht.readHumidity();

    if (!isnan(tempC) && !isnan(hum)) {
      lastTemp = tempC;
      lastHum = hum;
      float tempF = (tempC * 9.0 / 5.0) + 32.0;
      Serial.printf("Temp: %.1f°F | Hum: %.1f%%\n", tempF, hum);

      if (tempF < 35.0 && !alertSentLow) {
        sendDiscordAlert("Temp below threshold (35°F) at " + String(deviceName) + " (" + WiFi.localIP().toString() + "): " + String(tempF, 1) + "°F");
        alertSentLow = true;
      } else if (tempF >= 35.0) {
        alertSentLow = false;
      }

      if (tempF > 110.0 && !alertSentHigh) {
        sendDiscordAlert("Temp above threshold (110°F) at " + String(deviceName) + " (" + WiFi.localIP().toString() + "): " + String(tempF, 1) + "°F");
        alertSentHigh = true;
      } else if (tempF <= 110.0) {
        alertSentHigh = false;
      }

      alertSentSensor = false;
    } else {
      Serial.println("Sensor read failed");
      if (!alertSentSensor) {
        sendDiscordAlert("Temp Sensor Error at " + String(deviceName) + " (" + WiFi.localIP().toString() + ")");
        alertSentSensor = true;
      }
    }

    lastRead = millis();
  }

  if (millis() - lastPeerCheck > 30000) {
    checkPeers();
    lastPeerCheck = millis();
  }
}
