#ifndef AUTOMATA_H
#define AUTOMATA_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include "StompClient.h"
#include <Preferences.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <vector>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "esp_mac.h"

class Automata;
void freeSubscribe(Stomp::StompCommand cmd);
void freeError(Stomp::StompCommand cmd);
Stomp::Stomp_Ack_t freeHandleUpdate(Stomp::StompCommand cmd);
Stomp::Stomp_Ack_t freeHandleAction(Stomp::StompCommand cmd);

struct Attribute {
    String key;
    String displayName;
    String unit;
    String type;
    JsonDocument extras;
};

struct WifiConfig {
    String name;
    String password;
    String key;
};

typedef struct {
    JsonDocument data;
} Action;

typedef std::vector<Attribute> AttributeList;
typedef std::vector<WifiConfig> WifiList;
typedef void (*HandleAction)(const Action action);
typedef void (*HandleDelay)();

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { background-color: #1a1a1a; color: #ffffff; font-family: Arial, sans-serif; }
        h2 { text-align: center; color: #ffffff; }
        table { border-collapse: collapse; width: 80%; margin: auto; background-color: #333333; color: #ffffff; }
        th, td { border: 2px solid #212121; padding: 10px; text-align: left; }
        th { background-color: #222222; }
        tr:nth-child(even) { background-color: #444444; }
        footer { text-align: center; margin-top: 20px; color: #ffffff; }
    </style>
</head>
<body>
    <h2>Automata</h2>
    <div id="data">Loading...</div>
    <script>
        if (!!window.EventSource) {
            var source = new EventSource('/events');
            source.addEventListener('open', function () { console.log("Events Connected"); });
            source.addEventListener('error', function (e) {
                if (e.target.readyState !== EventSource.OPEN) { console.log("Events Disconnected"); }
            });
            source.addEventListener('live', function (e) {
                console.log("Received Data: ", e.data);
                try {
                    let jsonData = JSON.parse(e.data);
                    let table = `<table><tr><th>Key</th><th>Value</th></tr>`;
                    Object.entries(jsonData).forEach(([key, value]) => {
                        table += `<tr><td>${key}</td><td>${value}</td></tr>`;
                    });
                    table += '</table>';
                    document.getElementById('data').innerHTML = table;
                } catch (error) {
                    console.error("Error parsing JSON: ", error);
                    document.getElementById('data').innerHTML = "<p>Error loading data</p>";
                }
            });
        }
    </script>
</body>
<footer><p>Made by Subham</p></footer>
</html>
)rawliteral";

class Automata {
public:
    static Automata *instance;
    Automata(String deviceName, const char *HOST, int PORT);
    void begin();
    void loop();
    void registerDevice();
    void registerDeviceOld();
    void addAttribute(String key, String displayName, String unit, String type = "INFO", JsonDocument extras = JsonDocument());
    void sendData(JsonDocument doc);
    void sendLive(JsonDocument doc);
    void sendAction(JsonDocument doc);
    void subscribe(const Stomp::StompCommand cmd);
    void onActionReceived(HandleAction cb);
    void delayedUpdate(HandleDelay hd);
    Preferences getPreferences();
    int getDelay();
    void error(const Stomp::StompCommand cmd);
    Stomp::Stomp_Ack_t handleUpdate(const Stomp::StompCommand cmd);
    Stomp::Stomp_Ack_t handleAction(const Stomp::StompCommand cmd);

private:
    void keepWiFiAlive();
    void keepWiFiAlive2();
    void configureWiFi();
    void getConfig();
    void ws();
    String getMacAddress();
    void setOTA();
    char toLowerCase(char c);
    String convertToLowerAndUnderscore(String input);
    void parseConditionToArray(const String &automationId, const JsonDocument &resp, JsonArray &automations);
    String sendHttp(String output, String endpoint);
    String send(JsonDocument doc);
    JsonDocument parseString(String str);
    void handleWebServer();

    const char *ntpServer = "0.in.pool.ntp.org";

    // ORDER FIXED — matches Automata.cpp initializer list
    AttributeList attributeList;
    WifiList wifiList;
    HandleAction _handleAction;
    HandleDelay _handleDelay;
    AsyncWebServer server;
    AsyncEventSource events;
    WebSocketsClient webSocket;
    Stomp::StompClient stomper;
    WiFiMulti wifiMulti;
    HTTPClient http;
    Preferences preferences;
    String deviceName;
    const char *HOST;
    int PORT;
    String config;
    String deviceId;
    String macAddr;
    bool isDeviceRegistered;
    unsigned long previousMillis;
    int d;
};

#endif
