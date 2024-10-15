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

struct Attribute
{
    String key;
    String displayName;
    String unit;
    String type;
};

typedef std::vector<Attribute> AttributeList;

class Automata
{
public:
    static Automata *instance;
    Automata();
    void begin();
    void loop();
    void registerDevice();
    void addAttribute(String key, String displayName, String unit, String type = "INFO");
    void sendData(JsonDocument doc);
    void sendLive(JsonDocument doc);
    void sendAction(JsonDocument doc);
    void subscribe(const Stomp::StompCommand cmd);
    int getDelay();
    void error(const Stomp::StompCommand cmd);
    Stomp::Stomp_Ack_t handleUpdate(const Stomp::StompCommand cmd);

private:
    void keepWiFiAlive();

    void getConfig();
    void ws();
    String getMacAddress();


    String sendHttp(String output, String endpoint);
    String send(JsonDocument doc);

    const char *ntpServer = "0.in.pool.ntp.org";

    AttributeList attributeList;

    WebSocketsClient webSocket;
    Stomp::StompClient stomper;
    WiFiMulti wifiMulti;
    HTTPClient http;
    Preferences preferences;

    String config = "";
    String deviceId = "1";
    String macAddr = "";
    bool isDeviceRegistered = false;
    int d = 9000;
};

#endif
