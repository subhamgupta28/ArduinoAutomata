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
    String res = sendHttp(jsonString, "wifiList");
    Serial.print("wifi list ");
    Serial.println(res);
    if (res != "")
    {
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
    wifiMulti.addAP("JioFiber-x5hnq", "12341234");
    wifiMulti.addAP("Airtel_Itsvergin", "touchmenot");
    wifiMulti.addAP("wifi_NET", "444555666");
    configureWiFi();

    macAddr = getMacAddress();

    getConfig();
    xTaskCreatePinnedToCore([](void *params)
                            { static_cast<Automata *>(params)->keepWiFiAlive(); },
                            "keepWiFiAlive", 6096, this, 1, NULL, xPortGetCoreID());
    Serial.println("waiting");
    while (!WiFi.isConnected())
    {
    }
    // ws();
    // registerDevice();
}

void Automata::handleWebServer()
{
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

Preferences Automata::getPreferences()
{
    return preferences;
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
String Automata::getMacAddress() {
    return WiFi.macAddress();  // Returns in format "AA:BB:CC:DD:EE:FF"
}

void Automata::keepWiFiAlive()
{
    for (;;)
    {
        if (!webSocket.isConnected())
        {
            ws();
        }
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
        // if (!MDNS.begin(convertToLowerAndUnderscore(deviceName)))
        // {
        //     Serial.println("Error setting up MDNS responder!");
        // }
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("WiFi FAILED");
            vTaskDelay(20000 / portTICK_PERIOD_MS);
            continue;
        }

        if (!webSocket.isConnected())
        {
            ws();
        }

        ws();
        configTime(5.5 * 3600, 0, ntpServer);
        setOTA();
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
        handleWebServer();
    }
}

long regTime = millis();
long regWait = 30000;

void Automata::loop()
{
    unsigned long currentMillis = millis();
    if (wifiMulti.run() == WL_CONNECTED)
    {
        webSocket.loop();
        // if (!webSocket.isConnected())
        // {
        //     ws();
        // }
        ArduinoOTA.handle();
        if (currentMillis - previousMillis > getDelay())
        {
            _handleDelay();
            previousMillis = currentMillis;
        }

        if (!isDeviceRegistered && (currentMillis - regTime) > regWait)
        {
            registerDevice();
            regTime = currentMillis;
        }
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

    // String res = preferences.getString("automations", "{}");
    // JsonDocument resp = parseString(res);
    // JsonArray automationsArray = resp["automations"];

    // Preferences statePrefs;
    // statePrefs.begin("matchStates", false);  // new namespace for match tracking

    // for (JsonObject automation : automationsArray)
    // {
    //     String sensorKey = automation["sensorKey"];
    //     if (!data.containsKey(sensorKey)) continue;

    //     float currentValue = data[sensorKey].as<float>();
    //     String cmd = automation["command"];
    //     String automationId = automation["automationId"];
    //     bool matched = false;

    //     if (cmd == "rangeCondition") {
    //         float above = automation["above"] | -9999.0;
    //         float below = automation["below"] | 9999.0;
    //         if (currentValue > above && currentValue < below)
    //             matched = true;
    //     } else if (cmd == "exactMatch") {
    //         float expected = automation["value"];
    //         if (currentValue == expected)
    //             matched = true;
    //     }

    //     // Read last known match state
    //     bool lastMatched = statePrefs.getBool(automationId.c_str(), false);

    //     // Only send if the match state changed
    //     if (matched != lastMatched) {
    //         JsonDocument actionDoc;
    //         actionDoc["key"] = sensorKey;
    //         actionDoc["automationId"] = automationId;
    //         actionDoc["value"] = currentValue;
    //         actionDoc["match"] = matched;

    //         stomper.sendMessage("/app/action", send(actionDoc));

    //         // Save updated state
    //         statePrefs.putBool(automationId.c_str(), matched);
    //     }
    // }

    // statePrefs.end();

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
    Serial.println("Registering Device");

    JsonDocument doc;
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

    JsonArray attributes = doc["attributes"].to<JsonArray>();

    for (auto &attribute : attributeList)
    {
        JsonObject attr1 = attributes.createNestedObject();
        attr1["value"] = "";
        attr1["displayName"] = attribute.displayName;
        attr1["key"] = attribute.key;
        attr1["units"] = attribute.unit;
        attr1["type"] = attribute.type;
        attr1["extras"] = attribute.extras;
        attr1["visible"] = true;
        attr1["valueDataType"] = "String";
    }

    String jsonString;
    serializeJson(doc, jsonString);

    int retryCount = 0;
    while (retryCount < 5)
    {
        String res = sendHttp(jsonString, "register");
        configureWiFi();
        if (res != "")
        {
            isDeviceRegistered = true;
            JsonDocument resp;
            deserializeJson(resp, res);
            deviceId = resp["id"].as<String>();
            preferences.putString("deviceId", deviceId);
            Serial.println("Device Registered");
            Serial.println(res);
            return;
        }
        Serial.println("Device Registration failed. Retrying in 5 sec...");
        delay(5000);
        retryCount++;
    }

    Serial.println("Device registration failed after multiple attempts. Rebooting...");
}

void Automata::registerDeviceOld()
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
        attr1["visible"] = true;
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
void Automata::parseConditionToArray(const String &automationId, const JsonDocument &resp, JsonArray &automations)
{
    String conditionStr = resp[automationId];
    if (conditionStr.isEmpty())
        return;

    JsonObject entry = automations.createNestedObject();
    entry["automationId"] = automationId;

    int gtIndex = conditionStr.indexOf('>');
    int ltIndex = conditionStr.indexOf('<');
    int eqIndex = conditionStr.indexOf('=');

    if (eqIndex != -1)
    {
        // Exact match
        String key = conditionStr.substring(0, eqIndex);
        String value = conditionStr.substring(eqIndex + 1);
        key.trim();
        value.trim();

        entry["command"] = "exactMatch";
        entry["sensorKey"] = key;
        entry["value"] = value.toFloat();
    }
    else
    {
        // Range match
        String key = conditionStr;
        if (gtIndex != -1)
            key = conditionStr.substring(0, gtIndex);
        else if (ltIndex != -1)
            key = conditionStr.substring(0, ltIndex);
        key.trim();

        entry["command"] = "rangeCondition";
        entry["sensorKey"] = key;

        if (gtIndex != -1)
        {
            int end = (ltIndex != -1) ? ltIndex - 1 : conditionStr.length();
            String above = conditionStr.substring(gtIndex + 1, end);
            entry["above"] = above.toFloat();
        }

        if (ltIndex != -1)
        {
            String below = conditionStr.substring(ltIndex + 1);
            entry["below"] = below.toFloat();
        }
    }
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
    else
    {
        const char *keysStr = resp["keys"];
        if (keysStr != nullptr)
        {
            String keys(keysStr);
            keys.trim();

            int start = 0;
            int commaIndex = 0;

            while ((commaIndex = keys.indexOf(',', start)) != -1)
            {
                String id = keys.substring(start, commaIndex);
                id.trim();
                parseConditionToArray(id, resp, automationsArray);
                start = commaIndex + 1;
            }

            // Handle the last key
            String lastId = keys.substring(start);
            lastId.trim();
            if (!lastId.isEmpty())
            {
                parseConditionToArray(lastId, resp, automationsArray);
            }

            // Save automation
            String jsonStr;
            serializeJson(doc2, jsonStr);
            preferences.putString("automations", jsonStr);
        }
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