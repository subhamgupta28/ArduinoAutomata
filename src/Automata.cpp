#include "Automata.h"

Automata *Automata::instance = nullptr;

Automata::Automata(String deviceName, const char *HOST, int PORT)
    : deviceName(deviceName), HOST(HOST), PORT(PORT),
      stomper(webSocket, HOST, PORT, "/ws/", true),
      _handleAction(nullptr), _handleDelay(nullptr), server(80), events("/events")
{
    instance = this;
}

String Automata::convertToLowerAndUnderscore(String input)
{
    String output = "";

    for (int i = 0; i < input.length(); i++)
    {
        char ch = input.charAt(i);
        if (ch == ' ')
        {
            output += '_';
        }
        else
        {
            output += toLowerCase(ch);
        }
    }

    return output;
}

// Helper function to convert a character to lowercase
char Automata::toLowerCase(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c + 32;
    }
    return c;
}

void Automata::configureWiFi()
{
    JsonDocument req;
    req["wifi"] = "get";
    String jsonString;
    serializeJson(req, jsonString);
    // Try to fetch the latest Wi-Fi list
    String res;

    if (sendHttp(jsonString, "wifiList", res))
    {
        Serial.print("wifi list ");
        Serial.println(res);
        preferences.putString("wifiList", res);
    }

    // Retrieve Wi-Fi config from preferences
    String config = preferences.getString("wifiList", "");
    if (config == "")
    {
        Serial.println("No Wi-Fi configuration found.");
        return;
    }

    // Parse the JSON config
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, config);
    if (error)
    {
        Serial.print("Failed to parse Wi-Fi list JSON: ");
        Serial.println(error.c_str());
        return;
    }

    // Dynamically add available Wi-Fi networks
    for (int i = 1; i <= 5; ++i) // Increase this if more networks are supported
    {
        String keySsid = "wn" + String(i);
        String keyPass = "wp" + String(i);

        if (doc.containsKey(keySsid) && doc[keySsid].as<String>() != "")
        {
            String ssid = doc[keySsid].as<String>();
            String password = doc.containsKey(keyPass) ? doc[keyPass].as<String>() : "";
            wifiMulti.addAP(ssid.c_str(), password.c_str());
            Serial.printf("Added Wi-Fi network: %s\n", ssid.c_str());
        }
    }
}

void Automata::begin()
{
    // Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(convertToLowerAndUnderscore(deviceName).c_str());
    Serial.println(preferences.begin("my-app", false));

    // preferences.clear();

    wifiMulti.addAP("LAN-D", "Jio@12345");
    wifiMulti.addAP("Net2.4", "12345678");
    wifiMulti.addAP("JioFiber-x5hnq", "12341234");
    wifiMulti.addAP("wifi_NET", "444555666");
    // wifiMulti.addAP("Automata", "12345678");
    macAddr = getMacAddress();

    getConfig();
    // xTaskCreatePinnedToCore([](void *params)
    //                         { static_cast<Automata *>(params)->keepWiFiAlive(); },
    //                         "keepWiFiAlive", 3096, this, 1, NULL, xPortGetCoreID());

    xTaskCreate([](void *params)
                { static_cast<Automata *>(params)->keepWiFiAlive(); }, "keepWiFiAlive", 4096, this, 2, NULL);
}

void Automata::handleWebServer()
{
#if ENABLE_SD_FILE_SERVER
    beginSDFileServer(&server);
#endif
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html", index_html); });
    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
              { 
            
                ESP.restart();
                request->send(200, "text/html", "ok"); });
    events.onConnect([](AsyncEventSourceClient *client)
                     {
                         if (client->lastId())
                         {
                             Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
                         } });
    server.addHandler(&events);
    server.begin();
}
#if ENABLE_SD_FILE_SERVER
void Automata::beginSDFileServer(AsyncWebServer *existingServer)
{
    if (existingServer)
    {
        sdweb = new SDWebServer(*existingServer); // share
    }
    else
    {
        sdweb = new SDWebServer(); // create internal
    }
    sdweb->begin();
}
#endif
Preferences Automata::getPreferences()
{
    return preferences;
}

bool Automata::getMasterDeviceByName(const char *searchName, String &outId, String &outKey)
{
    for (auto item : masterDataList)
    {
        if (item.name.equals(searchName))
        {
            outId = item.id;
            outKey = item.key0;
            return true; // found
        }
    }
    return false; // not found
}
void Automata::parseJsonList(String jsonData)
{
    StaticJsonDocument<1024> doc;
    masterDataList.clear();

    // Parse JSON
    DeserializationError error = deserializeJson(doc, jsonData);
    if (error)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    JsonArray arr = doc.as<JsonArray>();

    for (JsonObject obj : arr)
    {
        MasterData md;
        md.key0 = (const char *)obj["key"];
        md.name = (const char *)obj["name"];
        md.id = (const char *)obj["id"];
        masterDataList.push_back(md);
    }
}
MasterDataList Automata::getMasterDataList()
{
    getMasterList();
    return masterDataList;
}
void Automata::getMasterList()
{
    JsonDocument req;
    req["list"] = "get";
    String jsonString;
    serializeJson(req, jsonString);
    String res;

    if (sendHttp(jsonString, "masterList", res))
    {
        Serial.println(res);
        parseJsonList(res);
    }
}
void Automata::getAutomationsList()
{
    JsonDocument req;
    req["list"] = "get";
    String jsonString;
    serializeJson(req, jsonString);
    String res;

    if (sendHttp(jsonString, "automations", res))
    {
        Serial.println(res);
        String names, ids;
        automationKeyIds = res;
        splitAutomations(res, names, ids);
        automations = names;
        automationIds = ids;
    }
}
String Automata::getIdByName(const String &input, const String &searchName)
{
    int start = 0;

    while (start < input.length())
    {
        // find the next comma
        int commaIndex = input.indexOf(',', start);
        if (commaIndex == -1)
            commaIndex = input.length();

        // extract "name:id"
        String pair = input.substring(start, commaIndex);

        int colonIndex = pair.indexOf(':');
        if (colonIndex > 0)
        {
            String name = pair.substring(0, colonIndex);
            String id = pair.substring(colonIndex + 1);

            // match name (case-sensitive)
            if (name == searchName)
            {
                return id;
            }
        }

        start = commaIndex + 1;
    }

    // not found
    return "";
}

void Automata::splitAutomations(const String &input, String &names, String &ids)
{
    names = "";
    ids = "";
    int start = 0;

    while (start < input.length())
    {
        // find the next comma
        int commaIndex = input.indexOf(',', start);
        if (commaIndex == -1)
            commaIndex = input.length();

        // extract "name:id"
        String pair = input.substring(start, commaIndex);

        int colonIndex = pair.indexOf(':');
        if (colonIndex > 0)
        {
            String name = pair.substring(0, colonIndex);
            String id = pair.substring(colonIndex + 1);

            if (names.length() > 0)
            {
                names += ",";
                ids += ",";
            }
            names += name;
            ids += id;
        }

        start = commaIndex + 1;
    }
}

String Automata::getAutomations()
{
    return automations;
}
String Automata::getAutomationId(const String &name)
{
    return getIdByName(automationKeyIds, name);
}
// String Automata::getMacAddress()
// {
//     uint8_t mac[6];
//     esp_read_mac(mac, ESP_IF_WIFI_STA); // for Wi-Fi station

//     // Store MAC address in a string
//     String macAddress = "";
//     for (int i = 0; i < 6; i++)
//     {
//         if (i > 0)
//         {
//             macAddress += ":";
//         }
//         macAddress += String(mac[i], HEX);
//     }
//     Serial.println(macAddress);
//     return macAddress;
// }
String Automata::getMacAddress()
{
    return WiFi.macAddress(); // Returns in format "AA:BB:CC:DD:EE:FF"
}
void Automata::keepWiFiAlive()
{

    const TickType_t delayConnected = 30000 / portTICK_PERIOD_MS;   // 30s
    const TickType_t delayDisconnected = 5000 / portTICK_PERIOD_MS; // 5s

    uint8_t retryCount = 0;
    bool wasConnected = false;

    for (;;)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            if (wasConnected)
            {
                Serial.println("WiFi disconnected. Retrying...");
                wasConnected = false;
            }

            if (wifiMulti.run() == WL_CONNECTED)
            {
                Serial.printf("WiFi connected!");
                Serial.printf("IP address: ");
                Serial.println(WiFi.localIP());
                vTaskDelay(pdMS_TO_TICKS(1000));
                // Re-init services only when first connected
                configTime(5.5 * 3600, 0, ntpServer);
                registerDevice();
                ws();
                handleWebServer();
                setOTA();
                retryCount = 0;
                wasConnected = true;
            }
            else
            {
                retryCount++;
                Serial.printf("WiFi reconnect failed (attempt %d)\n", retryCount);

                vTaskDelay(delayDisconnected);
            }
        }
        else
        {
            if (!webSocket.isConnected())
            {
                ws();
            }
            vTaskDelay(delayConnected);
        }
    }
}

void Automata::keepWiFiAliveOld()
{
    const TickType_t delayConnected = 10000 / portTICK_PERIOD_MS;    // 10s when connected
    const TickType_t delayDisconnected = 20000 / portTICK_PERIOD_MS; // 20s on failure

    bool wasConnected = false; // Track last connection state

    for (;;)
    {
        wl_status_t status = WiFi.status();

        if (status != WL_CONNECTED)
        {
            if (wasConnected)
            {
                Serial.println("WiFi disconnected. Retrying...");
                wasConnected = false;
            }

            unsigned long attemptStart = millis();
            while (wifiMulti.run() != WL_CONNECTED &&
                   millis() - attemptStart < 20000)
            {
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println("WiFi connected");
                Serial.println("IP address:");
                Serial.println(WiFi.localIP());

                // Setup only on *first connect* or after disconnection
                // if (!MDNS.begin(convertToLowerAndUnderscore(deviceName))) {
                //     Serial.println("Error setting up MDNS responder!");
                // }

                configTime(5.5 * 3600, 0, ntpServer);
                setOTA();
                ws();
                registerDevice();
                handleWebServer();

                wasConnected = true;
            }
            else
            {
                Serial.println("WiFi connect FAILED");
                vTaskDelay(delayDisconnected);
            }
        }
        else
        {
            // Still connected → just wait
            vTaskDelay(delayConnected);
        }
    }
}

long regTime = millis();
long regWait = 30000;

void Automata::loop()
{
    unsigned long currentMillis = millis();

    if (wifiMulti.run() == WL_CONNECTED)
    {
        // Maintain connections
        webSocket.loop();

        ArduinoOTA.handle();
        // Serial.println(isDeviceRegistered);

        // Handle periodic tasks
        if (currentMillis - previousMillis >= getDelay())
        {
            _handleDelay();
            previousMillis = millis(); // prevents drift
        }

        // Handle device registration retry
        if (!isDeviceRegistered && (currentMillis - regTime) >= regWait)
        {
            registerDevice();
            regTime = currentMillis;
        }
    }
    else
    {
        // Optional: Handle disconnected state (e.g., LED blink or minimal work)
    }
}

void Automata::setOTA()
{
    ArduinoOTA.setHostname(convertToLowerAndUnderscore(deviceName).c_str());
    ArduinoOTA.setPassword("");
    ArduinoOTA.onStart([]()
                       {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else  // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); })
        .onEnd([]()
               { Serial.println("\nEnd"); })
        .onProgress([](unsigned int progress, unsigned int total)
                    { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
        .onError([](ota_error_t error)
                 {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed"); });

    ArduinoOTA.begin();

    Serial.println("Ready");
}

void Automata::sendLive(JsonDocument data)
{
    stomper.sendMessage("/app/sendLiveData", send(data));
    // Stream live data to clients
    String json;
    serializeJson(data, json);
    events.send(json.c_str(), "live", millis());
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
    static uint8_t retryCount = 0;
    static unsigned long lastAttempt = 0;

    // if (isDeviceRegistered)
    //     return;

    unsigned long now = millis();
    unsigned long backoff = min(60000UL, (1UL << retryCount) * 1000); // max 60s

    // if (retryCount > 0 && now - lastAttempt < backoff)
    //     return;

    Serial.printf("Registering Device (attempt %d)...\n", retryCount + 1);

    StaticJsonDocument<1024> doc;
    doc["name"] = deviceName;
    doc["deviceId"] = deviceId;
    doc["type"] = "sensor";
    doc["updateInterval"] = d;
    doc["status"] = "ONLINE";
    doc["host"] = String(WiFi.getHostname());
    doc["macAddr"] = macAddr;
    doc["reboot"] = false;
    doc["sleep"] = false;
    doc["accessUrl"] = "http://" + WiFi.localIP().toString();

    JsonArray attributes = doc.createNestedArray("attributes");
    for (auto &attribute : attributeList)
    {
        JsonObject attr = attributes.createNestedObject();
        attr["value"] = "";
        attr["displayName"] = attribute.displayName;
        attr["key"] = attribute.key;
        attr["units"] = attribute.unit;
        attr["type"] = attribute.type;
        attr["extras"] = attribute.extras;
        attr["visible"] = true;
        attr["valueDataType"] = "String";
    }

    String jsonString;
    serializeJson(doc, jsonString);
    String res;

    if (sendHttp(jsonString, "register", res))
    {
        DynamicJsonDocument resp(1024);
        if (deserializeJson(resp, res) == DeserializationError::Ok)
        {
            deviceId = resp["id"].as<String>();
            preferences.putString("deviceId", deviceId);
            isDeviceRegistered = true;
            retryCount = 0;
            Serial.println("Device Registered");
            vTaskDelay(200);
            getAutomationsList();
            getMasterList();
        }
    }
    else
    {
        retryCount++;
        Serial.printf("Device registration failed (attempt %d)\n", retryCount);
        if (retryCount > 8)
        {
            Serial.println("Max retries reached, rebooting...");
            // ESP.restart();
        }
    }

    lastAttempt = now;
}

int maxRetries = 2;
int retryDelayMs = 200;
bool Automata::sendHttp(const String &output, const String &endpoint, String &result)
{

    HTTPClient http;
    result = "";

    http.begin("http://" + String(HOST) + ":" + String(PORT) + "/api/v1/main/" + endpoint);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000); // 5 seconds per request

    int httpCode = http.POST(output);

    if (httpCode > 0)
    {
        Serial.printf("[HTTP] POST code: %d\n", httpCode);

        result = http.getString(); // always capture response

        if (httpCode >= 200 && httpCode < 300)
        {
            http.end();
            return true; // success, exit early
        }
        else
        {
            Serial.printf("[HTTP] POST unexpected code: %d, body: %s\n",
                          httpCode, result.c_str());
        }
    }
    else
    {
        Serial.printf("[HTTP] POST attempt failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();

    return false;
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
        // isDeviceRegistered = true;
        Serial.println("Config");
        Serial.println(sv);
        JsonDocument resp;
        deserializeJson(resp, sv);
        // d = resp["updateInterval"].as<int>();
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
    // ESP.restart();
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
    bool p1 = action.data["reboot"];
    JsonDocument doc;
    JsonDocument doc2;
    JsonArray automationsArray = doc2.createNestedArray("automations");
    if (p1)
    {
        doc["command"] = "reboot";
    }

    doc["key"] = "actionAck";
    doc["actionAck"] = "Success";
    stomper.sendMessage("/app/ackAction", send(doc));

    if (p1)
    {
        stomper.disconnect();
        delay(200);
        ESP.restart();
    }
    return Stomp::CONTINUE;
}
void Automata::onActionReceived(HandleAction cb)
{
    _handleAction = cb;
}
void Automata::delayedUpdate(HandleDelay hd)
{
    _handleDelay = hd;
}