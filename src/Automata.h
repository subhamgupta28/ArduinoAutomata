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
#include "config.h"
#include <vector>

class Automata;
void freeSubscribe(Stomp::StompCommand cmd);
void freeError(Stomp::StompCommand cmd);
Stomp::Stomp_Ack_t freeHandleUpdate(Stomp::StompCommand cmd);
Stomp::Stomp_Ack_t freeHandleAction(Stomp::StompCommand cmd);

struct Attribute
{
    String key;
    String displayName;
    String unit;
    String type;
    JsonDocument extras;
};

typedef struct
{
    JsonDocument data;
} Action;

typedef std::vector<Attribute> AttributeList;
typedef void (*HandleAction)(const Action action);
typedef void (*HandleDelay)();

class Automata
{
public:
    static Automata *instance;
    Automata();
    void begin();
    void loop();
    void registerDevice();
    void addAttribute(String key, String displayName, String unit, String type = "INFO", JsonDocument extras = JsonDocument());
    void sendData(JsonDocument doc);
    void sendLive(JsonDocument doc);
    void sendAction(JsonDocument doc);
    void subscribe(const Stomp::StompCommand cmd);
    void onActionReceived(HandleAction cb);
    void delayedUpdate(HandleDelay hd);
    int getDelay();
    void error(const Stomp::StompCommand cmd);
    Stomp::Stomp_Ack_t handleUpdate(const Stomp::StompCommand cmd);
    Stomp::Stomp_Ack_t handleAction(const Stomp::StompCommand cmd);

private:
    void keepWiFiAlive();

    void getConfig();
    void ws();
    String getMacAddress();

    String sendHttp(String output, String endpoint);
    String send(JsonDocument doc);
    JsonDocument parseString(String str);

    const char *ntpServer = "0.in.pool.ntp.org";

    AttributeList attributeList;

    HandleAction _handleAction;
    HandleDelay _handleDelay;

    WebSocketsClient webSocket;
    Stomp::StompClient stomper;
    WiFiMulti wifiMulti;
    HTTPClient http;
    Preferences preferences;

    String config = "";
    String deviceId = "1";
    String macAddr = "";
    bool isDeviceRegistered = false;
    unsigned long previousMillis = 0;
    int d = 9000;
};

#endif
