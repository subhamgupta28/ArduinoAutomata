#include "Automata.h"

Automata *Automata::instance = nullptr;

Automata::Automata(String deviceName)
    :deviceName(deviceName), stomper(webSocket, HOST, PORT, "/ws/", true)
{
    instance = this;
}

void Automata::begin()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);

    Serial.println(preferences.begin("my-app", false));

    // preferences.clear();

    wifiMulti.addAP("JioFiber-x5hnq", "12341234");
    wifiMulti.addAP("Net2.4", "12345678");
    wifiMulti.addAP("wifi_NET", "444555666");

    macAddr = getMacAddress();

    getConfig();
    xTaskCreatePinnedToCore([](void *params)
                            { static_cast<Automata *>(params)->keepWiFiAlive(); },
                            "keepWiFiAlive", 8000, this, 1, NULL, CONFIG_ARDUINO_RUNNING_CORE);
    Serial.println("waiting");
    while (!WiFi.isConnected())
    {
    }
    ws();
    // registerDevice();
}

String Automata::getMacAddress()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA); // for Wi-Fi station

    // Store MAC address in a string
    String macAddress = "";
    for (int i = 0; i < 6; i++)
    {
        if (i > 0)
        {
            macAddress += ":";
        }
        macAddress += String(mac[i], HEX);
    }
    Serial.println(macAddress);
    return macAddress;
}

void Automata::keepWiFiAlive()
{
    for (;;)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        Serial.print("Connecting to WiFi...");
        unsigned long attemptTime = millis();

        while (wifiMulti.run() != WL_CONNECTED && millis() - attemptTime < 20000)
        {
            // Serial.print(".");
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("WiFi FAILED");
            vTaskDelay(20000 / portTICK_PERIOD_MS);
            continue;
        }

        if (!MDNS.begin("automata_mmw"))
        {
            Serial.println("Error setting up MDNS responder!");
        }

        configTime(5.5 * 3600, 0, ntpServer);
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
    }
}

void Automata::loop()
{
    unsigned long currentMillis = millis();
    if (wifiMulti.run() == WL_CONNECTED)
    {
        webSocket.loop();
        if (currentMillis - previousMillis > getDelay())
        {
            _handleDelay();
            previousMillis = currentMillis;
        }
    }
}

void Automata::sendLive(JsonDocument doc)
{
    stomper.sendMessage("/app/sendLiveData", send(doc));
}

void Automata::sendData(JsonDocument doc)
{

    stomper.sendMessage("/app/sendData", send(doc));
}

void Automata::sendAction(JsonDocument doc)
{
    stomper.sendMessage("/app/action", send(doc));
}

String Automata::send(JsonDocument doc)
{
    doc["device_id"] = deviceId;

    String output;
    serializeJson(doc, output);
    String escapedString = "";
    for (int i = 0; i < output.length(); i++)
    {
        if (output.charAt(i) == '"')
        {
            escapedString += '\\';
        }
        escapedString += output.charAt(i);
    }
    // Serial.println("Data sent: ");
    // Serial.print(output);
    return escapedString;
}
void Automata::addAttribute(String key, String displayName, String unit, String type, JsonDocument extras)
{
    Attribute atb;
    atb.displayName = displayName;
    atb.key = key;
    atb.unit = unit;
    atb.type = type;
    atb.extras = extras;
    attributeList.push_back(atb);
}

void Automata::registerDevice()
{
    Serial.println("Registering Device");

    JsonDocument doc;
    doc["name"] = deviceName;
    doc["deviceId"] = deviceId;
    doc["type"] = "sensor";
    doc["updateInterval"] = d;
    doc["status"] = "ONLINE";
    doc["macAddr"] = macAddr;
    doc["reboot"] = false;
    doc["sleep"] = false;
    doc["accessUrl"] = "http://" + WiFi.localIP().toString();

    JsonArray attributes = doc["attributes"].to<JsonArray>();

    for (auto attribute : attributeList)
    {
        JsonObject attr1 = attributes.add<JsonObject>();
        attr1["value"] = "";
        attr1["displayName"] = attribute.displayName;
        attr1["key"] = attribute.key;
        attr1["units"] = attribute.unit;
        attr1["type"] = attribute.type;
        attr1["extras"] = attribute.extras;
        attr1["valueDataType"] = "String";
    }

    String jsonString;
    serializeJson(doc, jsonString);
    String res = sendHttp(jsonString, "register");

    while (res == "")
    {
        Serial.println("Device Registeration failed.");
        Serial.println("Trying to register the device again in 5 sec");
        res = sendHttp(jsonString, "register");
        delay(5000);
    }
    isDeviceRegistered = true;
    Serial.print("Response: ");
    Serial.println(res);
    JsonDocument resp;

    deserializeJson(resp, res);
    deviceId = resp["id"].as<String>();
    preferences.putString("deviceId", deviceId);
    Serial.println("Device Registered");
    Serial.println(res);
}

String Automata::sendHttp(String output, String endpoint)
{
    http.begin("http://" + String(HOST) + ":" + String(PORT) + "/api/v1/main/" + endpoint);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(output);

    if (httpCode > 0)
    {
        Serial.printf("[HTTP] GET... code: %d\n", httpCode);

        if (httpCode == HTTP_CODE_OK)
        {
            String response = http.getString();
            http.end();
            return response;
        }
        else
        {
            return "";
        }
    }
    else
    {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    return "";
}

void Automata::ws()
{
    Serial.println("ws connecting");
    stomper.onConnect(freeSubscribe);
    stomper.onError(freeError);
    Serial.println("ws connected");
    stomper.begin();
}

void Automata::getConfig()
{
    String sv = preferences.getString("config", "");
    if (sv != "")
    {
        isDeviceRegistered = true;
        Serial.println("Config");
        Serial.println(sv);
        JsonDocument resp;
        deserializeJson(resp, sv);
        d = resp["updateInterval"].as<int>();
    }
}

void Automata::subscribe(const Stomp::StompCommand cmd)
{
    Serial.println("Connected to STOMP broker");
    String queueStr = "/topic/update/" + deviceId;
    char queue[queueStr.length() + 1];
    strcpy(queue, queueStr.c_str());
    stomper.subscribe(queue, Stomp::CLIENT, freeHandleUpdate);
    String actionStr = "/topic/action/" + deviceId;
    char action[actionStr.length() + 1];
    strcpy(action, actionStr.c_str());

    stomper.subscribe(action, Stomp::CLIENT, freeHandleAction);
}
void Automata::error(const Stomp::StompCommand cmd)
{
    Serial.println("ERROR: " + cmd.body);
}
Stomp::Stomp_Ack_t Automata::handleUpdate(const Stomp::StompCommand cmd)
{
    String res = String(cmd.body);
    JsonDocument resp = parseString(res);
    String output;
    serializeJson(resp, output);
    Serial.println(output);

    deviceId = resp["id"].as<String>();
    preferences.putString("deviceId", deviceId);
    preferences.putString("config", output);
    getConfig();

    return Stomp::CONTINUE;
}

int Automata::getDelay()
{
    return d;
}

void freeSubscribe(Stomp::StompCommand cmd)
{
    Automata::instance->subscribe(cmd);
}
void freeError(Stomp::StompCommand cmd)
{
    Automata::instance->error(cmd);
}
Stomp::Stomp_Ack_t freeHandleUpdate(Stomp::StompCommand cmd)
{
    return Automata::instance->handleUpdate(cmd);
}

Stomp::Stomp_Ack_t freeHandleAction(Stomp::StompCommand cmd)
{
    return Automata::instance->handleAction(cmd);
}

JsonDocument Automata::parseString(String str)
{
    JsonDocument resp;
    Serial.println("Action Received");

    str.trim();
    str.replace("\\", "");

    DeserializationError err = deserializeJson(resp, str);
    if (err)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(err.c_str());
        Serial.println(str);
    }
    return resp;
}

Stomp::Stomp_Ack_t Automata::handleAction(const Stomp::StompCommand cmd)
{
    String res = String(cmd.body);
    JsonDocument resp = parseString(res);
    Action action;

    action.data = resp;
    _handleAction(action);

    return Stomp::CONTINUE;
}
void Automata::onActionReceived(HandleAction cb)
{
    _handleAction = cb;
}
void Automata::delayedUpdate(HandleDelay hd){
    _handleDelay = hd;
}