#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>

#include <wifi.h>

#include <config.h>
#include <time.h>
#include <rc3serial.h>

#include <PubSubClient.h>
#include <ArduinoJson.h>


WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

const char THING_NAME[] = DEVICE_NAME;
const char INITIAL_AP_PASSWORD[] = "mhimhi";
const char CONFIG_VERSION[] = "a1";


char mqttServer[32] = MQTT_SERVER;
char mqttPort[6] = MQTT_PORT;
char mqttUser[32] = MQTT_USER;
char mqttPass[32] = MQTT_PASSWORD;
char baseTopic[80] = DEVICE_NAME;
char mqttDevice[256] = MQTT_DEVICE;
char haBaseTopic[64];

struct ClimateState {
    bool power;
    uint8_t mode;   // 0=auto,1=dry,2=cool,3=fan_only,4=heat
    uint8_t speed;  // 0-4
    float targetTemp;
    float currentTemp;
} state = {false, 0, 0, 21.0f, 21.0f};

unsigned long lastStatusUpdate = 0;
unsigned long lastStatusRequest = 0;
unsigned long lastUpdate = 0;

void wifiConnected();



// void publishZoneState(uint8_t zone) {
//     char topic[64];
//     snprintf(topic, sizeof(topic), "%s/zone%u/state", haBaseTopic, zone + 1);
//     mqttClient.publish(topic, zoneState[zone] ? "ON" : "OFF", true);
// }

// void publishZoneName(uint8_t zone) {
//     char topic[64];
//     snprintf(topic, sizeof(topic), "%s/zone%u/name", haBaseTopic, zone + 1);
//     String n("ffffuuuu");
//     mqttClient.publish(topic, ZONE_NAMES[zone], true);
// }

// void publishAllStates() {
//     for (uint8_t i = 0; i < numZones; ++i) {
//         publishZoneState(i);
//     }
// }

// void publishAllZoneNames() {
//     for (uint8_t i = 0; i < numZones; ++i) {
//         publishZoneName(i);
//     }
// }

void mqttLog(String logMessage) {

  char mbuf[128];
  snprintf(mbuf,sizeof(mbuf), "%s/log", haBaseTopic);

  mqttClient.publish(mbuf, logMessage.substring(0,128).c_str());
  Serial.println(logMessage); // optional, also print to local serial
}



const char* modeToString(uint8_t m) {
  switch(m) {
    case 0: return "auto";
    case 1: return "dry";
    case 2: return "cool";
    case 3: return "fan_only";
    case 4: return "heat";
  }
  return "off";
}

uint8_t modeFromString(const String &s) {
  if (s.equalsIgnoreCase("auto")) return 0;
  if (s.equalsIgnoreCase("dry")) return 1;
  if (s.equalsIgnoreCase("cool")) return 2;
  if (s.equalsIgnoreCase("fan") || s.equalsIgnoreCase("fan_only")) return 3;
  if (s.equalsIgnoreCase("heat")) return 4;
  return 0xFF;
}

void publishState() {
  char topic[128];
  char buf[16];

  snprintf(topic, sizeof(topic), "%s/power/state", haBaseTopic);
  mqttClient.publish(topic, state.power ? "ON" : "OFF", true);

  snprintf(topic, sizeof(topic), "%s/mode/state", haBaseTopic);
  mqttClient.publish(topic, modeToString(state.mode), true);

  snprintf(topic, sizeof(topic), "%s/speed/state", haBaseTopic);
  snprintf(buf, sizeof(buf), "%u", state.speed);
  mqttClient.publish(topic, buf, true);

  snprintf(topic, sizeof(topic), "%s/temp/state", haBaseTopic);
  dtostrf(state.targetTemp, 0, 1, buf);
  mqttClient.publish(topic, buf, true);

  snprintf(topic, sizeof(topic), "%s/temperature/current", haBaseTopic);
  dtostrf(state.currentTemp, 0, 1, buf);
  mqttClient.publish(topic, buf, true);
}

bool parseStatus(const char *s) {
  if (!s || strlen(s) < 32) return false;
  if (s[4] != '1') return false;

  char tbuf[3];
  strncpy(tbuf, &s[30], 2);
  tbuf[2] = '\0';
  int number = strtol(tbuf, NULL, 16);
  float temp = number * 0.5f;

  state.power = (s[13] == '1');

  switch(s[17]) {
    case '1': state.mode = 1; break;
    case '2': state.mode = 2; break;
    case '3': state.mode = 3; break;
    case '4': state.mode = 4; break;
    default: state.mode = 0; break;
  }

  switch(s[21]) {
    case '0': state.speed = 1; break;
    case '1': state.speed = 2; break;
    case '2': state.speed = 3; break;
    case '6': state.speed = 4; break;
    default: state.speed = 0; break;
  }

  state.targetTemp = temp;
  state.currentTemp = temp;

  lastStatusUpdate = millis();
  publishState();
  return true;
}

void handleSerial() {
  if (!Serial.available()) return;
  size_t len = Serial.available();
  char rbuf[len+1];
  Serial.readBytes(rbuf, len);
  char sbuf[len+1];
  int sl = 0;
  for (uint8_t i = 1; i < len; ++i) {
    if (sl) {
      if ((uint8_t)rbuf[i] > 32 && (uint8_t)rbuf[i] < 127) sbuf[sl++] = rbuf[i];
    } else if (rbuf[i] == 'R') {
      sbuf[sl++] = rbuf[i];
    }
  }
  sbuf[sl] = '\0';
  parseStatus(sbuf);
}

void requestStatus() {
  getStatus();
  lastStatusRequest = millis();
}



void updateStatus()
{
  serialFlush();
  requestStatus();
  delay(200);
  handleSerial();
  serialFlush();
  lastUpdate = millis();
}

void sendDiscovery() {
  mqttLog("Sending discovery messages:");

  char topic[128];  
  char payload[1152];


  // //
  // // Power on/off
  // //

  // const char *item = "power";
  //snprintf(topic, sizeof(topic), "homeassistant/switch/%s/power/config", THING_NAME);
  // snprintf(payload, sizeof(payload), "{\"name\":\"%s\",\"command_topic\":\"%s/%s/set\",\"state_topic\":\"%s/%s/state\",\"unique_id\":\"%s_%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\"}", item, haBaseTopic, item, haBaseTopic, item, THING_NAME, item);
    
  
  // mqttLog(payload);
  // mqttClient.publish(topic, payload, true);

  mqttLog("climate: 1");
  snprintf(topic, sizeof(topic), "homeassistant/climate/%s/config", THING_NAME);
  snprintf(payload, sizeof(payload),
  "{"
    "%s,"
    "\"name\": \"%s\","
    "\"unique_id\": \"%s_climate\","
    "\"mode_command_topic\": \"%s/mode/set\","
    "\"mode_state_topic\": \"%s/mode/state\","
    "\"modes\": [\"off\", \"cool\", \"dry\", \"heat\", \"fan_only\", \"auto\"],"
    "\"temperature_command_topic\": \"%s/temp/set\","
    "\"temperature_state_topic\": \"%s/temp/state\","
    "\"min_temp\": 16,"
    "\"max_temp\": 30,"
    "\"temp_step\": 0.5,"
    "\"current_temperature_topic\": \"%s/temperature/current\","
    "\"availability_topic\": \"%s/status\","
    "\"payload_available\": \"online\","
    "\"payload_not_available\": \"offline\""
  "}",
  mqttDevice,
  THING_NAME,
  THING_NAME,
  haBaseTopic, haBaseTopic,
  haBaseTopic, haBaseTopic,
  haBaseTopic,
  haBaseTopic
  );
  
  mqttLog(String("climate: 2 to topic: ") + topic);

  mqttClient.publish(topic, payload, true);

  mqttLog("climate: 3");
  mqttLog(payload);

  mqttLog("climate: 4");

//
// Fan speed: 0 - 4
//
snprintf(topic, sizeof(topic), "homeassistant/number/%s/fan_speed/config", THING_NAME);
snprintf(payload, sizeof(payload),
  "{"
    "%s,"
    "\"name\": \"Fan Speed\","
    "\"command_topic\": \"%s/speed/set\","
    "\"state_topic\": \"%s/speed/state\","
    "\"unique_id\": \"%s_fan_speed\","
    "\"min\": 0,"
    "\"max\": 4,"
    "\"step\": 1,"
    "\"mode\": \"box\""    
  "}",
  mqttDevice,
  haBaseTopic, haBaseTopic, 
  THING_NAME
);




mqttClient.publish(topic, payload, true);
mqttLog(payload);

snprintf(topic, sizeof(topic), "homeassistant/number/%s/set_temp/config", THING_NAME);
snprintf(payload, sizeof(payload),
  "{"
    "%s,"
    "\"name\": \"Target Temperature\","
    "\"command_topic\": \"%s/temp/set\","
    "\"state_topic\": \"%s/temp/state\","
    "\"unique_id\": \"%s_set_temp\","
    "\"min\": 16,"
    "\"max\": 30,"
    "\"step\": 1,"
    "\"mode\": \"box\""
  "}",
  mqttDevice,
  haBaseTopic, haBaseTopic,
  THING_NAME
);




mqttClient.publish(topic, payload, true);
mqttLog(payload);

snprintf(topic, sizeof(topic), "homeassistant/number/%s/delay_off/config", THING_NAME);
snprintf(payload, sizeof(payload),
  "{"
    "%s,"
    "\"name\": \"Delay Off Hours\","
    "\"command_topic\": \"%s/delayOffHours/set\","
    "\"state_topic\": \"%s/delayOffHours/state\","
    "\"unique_id\": \"%s_delay_off\","
    "\"min\": 1,"
    "\"max\": 12,"
    "\"step\": 1,"
    "\"mode\": \"box\""    
  "}",
  mqttDevice,
  haBaseTopic, haBaseTopic,
  THING_NAME
);




mqttClient.publish(topic, payload, true);
mqttLog(payload);

}



void mqttCallback(char *topic, byte *payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; ++i) {
        msg += (char)payload[i];
    }
    String t(topic);
    String base = String(haBaseTopic) + "/";
    if (!t.startsWith(base)) return;
    String cmd = t.substring(base.length());

    if (cmd == "power/set") {
        bool on = msg.equalsIgnoreCase("ON") || msg == "1";
        setPowerOn(on ? 1 : 0);
        state.power = on;
    } else if (cmd == "mode/set") {
        if (msg.equalsIgnoreCase("off")) {
            setPowerOn(0);
            serialFlush();
            state.power = false;
        } else {
            uint8_t m = modeFromString(msg);
            if (m != 0xFF) {              
                setPowerOn(1);
                serialFlush();
                setMode(m);
                serialFlush();
                state.mode = m;
                state.power = true;
            }
        }
    } else if (cmd == "temp/set") {
        float tval = msg.toFloat();
        state.targetTemp = tval;
        setTemp((uint16_t)(tval * 10));
        serialFlush();
    } else if (cmd == "speed/set") {
        uint8_t sp = msg.toInt();
        if (sp <= 4) {
            state.speed = sp;
            setFanSpeed(sp);
            serialFlush();
        }
    } else if (cmd == "delayOffHours/set") {
        uint8_t h = msg.toInt();
        if (h >= 1 && h <= 12) {
            setOffTimer(h);
            serialFlush();
        }
    }

    publishState();
    updateStatus();
}

///
///
///

#if 0 // excluded original code, keep here for reference.

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    
    for (unsigned int i = 0; i < length; ++i) {
        msg += (char)payload[i];
    }

    // mqttLog((String("Got MQTT message payload ['") +msg + String("'")).c_str());


    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(msg);

    Settings s = {
        .power = 0xff, 
        .mode = 0xff,
        .degrees = 0xffff,
        .speed = 0xff
    };


    if(1)
    {

    if (!root.success()) {
        // JSON parsing failed
        char mbuf[50];
        sprintf(mbuf, "%s/status", BASE_TOPIC);
        mqtt.publish(mbuf, "parse_fail", false);
        jsonBuffer.clear();

        mqttLog("parse fail");

        return;
    } 

    yield();


    if (root.containsKey("speed")) {
      //  mqttLog("Root contains speed!");

        s.speed = root["speed"];
    }

    if (root.containsKey("mode")) {
        const char *mode = root["mode"];
        uint8_t imode;
        switch (mode[0]) {
        case 'c':
        case 'C': imode=2; break;;
        case 'd':
        case 'D': imode=1; break;;
        case 'h':
        case 'H': imode=4; break;;
        case 'f':
        case 'F': imode=3; break;;
        case 'a':
        case 'A': imode=0; break;;
        default: imode=0; break;;
        }
        s.mode = imode;
    }

    if (root.containsKey("temp")) {
        float temp = atof(root["temp"]);
        temp = temp * 10.0;
        s.degrees = (int)temp;
    }

    if (root.containsKey("power")) {
        s.power = root["power"];
    }

    if (!root.containsKey("status")) {
        serialFlush();
    }

    if ((s.speed & s.power & s.mode) != 0xFF && s.degrees != 0xFFFF) {
        mqttLog("SETTING CLIMATE VALUES ON UNIT!");
        setClimate(s);
        delay(100);
        serialFlush();
    }
    else
    {
        if(s.power != 0xff)
        {
          setPowerOn(s.power);
          delay(100);
          serialFlush();
        }

        if(s.mode != 0xff)
        {
          setFanSpeed(s.speed);
          delay(100);
          serialFlush();            
        }

        if(s.speed != 0xff)
        {
          setFanSpeed(s.speed);
          delay(100);
          serialFlush();
        }

        if(s.degrees != 0xFFFF)
        {
            setTemp(s.degrees);
            delay(100);
            serialFlush();
        }

       // mqttLog("NOT SETTING VALUES!");
    }

    if (root.containsKey("delayOffHours")) {
        uint8_t hours = root["delayOffHours"];
        setOffTimer(hours);
        delay(100);
        serialFlush();
    }
    }
    mqttLog("calling getStatus then checking if serial is available");

    getStatus();
    delay(200);

    if(Serial.available()){
        mqttLog("Serial is available.");

        size_t len = Serial.available();
        char rbuf[len+1];
        Serial.readBytes(rbuf, len);
        char sbuf[len+1];
        int sbuflen=0;
        for (uint8_t i=1; i<len; i++) {
          if (sbuflen) {
            if ((uint8_t)rbuf[i] > 32 && (uint8_t)rbuf[i] < 127) { // ascii printable
              sbuf[sbuflen++]=rbuf[i];
            }
          }
          else {
            if (rbuf[i]=='R') {
              sbuf[sbuflen++]=rbuf[i];
            }
          }
        }
        sbuf[sbuflen]='\0';
        char mbuf[50];
        sprintf(mbuf, "%s/status", BASE_TOPIC);

        if (sbuf[4]=='1') {
            char pwr = sbuf[13];
            char mode = sbuf[17];
            char fan = sbuf[21];
            char tbuf[2];
            strncpy(tbuf, &sbuf[30], 2);
            unsigned int number = (int)strtol(tbuf, NULL, 16);
            unsigned int temp = number * 5;
            char rem[]=".0\0";
            if (temp % 10) rem[1]='5';
            temp = temp / 10;

            char sfan[2];
            switch(fan) {
                case '0': sfan[0]='1';
                break;
                case '1': sfan[0]='2';
                break;
                case '2': sfan[0]='3';
                break;
                case '6': sfan[0]='4';
                break;
                default: sfan[0]='0';
            }
            sfan[1]='\0';
            char smode[6];
            switch (mode) {
                case '2': strncpy(smode,"cool\0",5); break;;
                case '1': strncpy(smode,"dry\0",4); break;;
                case '4': strncpy(smode,"heat\0",5); break;;
                case '3': strncpy(smode,"fan\0",4); break;;
                case '0': strncpy(smode,"auto\0",5); break;;
            }

            String buffer = "{\"power\":" + String(pwr) + ",\"mode\":\"" + String(smode) + "\",\"speed\":" + String(sfan) + ",\"temp\":" + String(temp) + String(rem) + ",\"response\":\"" + String(sbuf) + "\"";
            if (root.containsKey("delayOffHours")) {
              buffer += ",\"delayOffHours\":" + String(root["delayOffHours"]);
            }
            buffer += "}\n";
            mqtt.publish(mbuf, buffer.c_str());
        }
        else {
            String buffer = "{\"response\":\"" + String(sbuf) + "\"}";
            mqtt.publish(mbuf, buffer.c_str());
        }
    }
    

    mqttLog("Finished MQTT message.");

    jsonBuffer.clear();

}

#endif


void connectToWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    ArduinoOTA.setHostname(BASE_TOPIC);
    ArduinoOTA.begin();

    Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("IOT Thing name: %s\n", BASE_TOPIC);

}

void connectToMqtt_old_unused()
{
    static unsigned long t_connect = 0;

    //   Serial.printf("mqtt NOT CONNECTED\n");
    delay(1000);
    unsigned long t_now = millis();
    Serial.printf("mqtt will attemp connection in: %ld]\n", t_now - t_connect);
    if (t_now - t_connect > 5000)
    {
        // Attempting MQTT connection...
        digitalWrite(13, HIGH);
        Serial.printf("mqtt attempting connection\n");
        if (mqttClient.connect(BASE_TOPIC, MQTT_USER, MQTT_PASSWORD))
        {
            Serial.printf("mqtt CONNECTED\n");
            // connected
            char mbuf[50];
            sprintf(mbuf, "%s/status", BASE_TOPIC);
            mqttClient.publish(mbuf, "connected", false);
            mqttClient.subscribe(BASE_TOPIC);
        }
        else
        {
            // failed
            Serial.printf("mqtt FAILEDn");
        }
        digitalWrite(13, LOW);
        t_connect = t_now;
    }
}

bool connectToMqtt() {

    static unsigned long lastMqttAttempt = 0;

    if (mqttClient.connected()) return true;

    unsigned long now = millis();
    if (now - lastMqttAttempt < 1000) {
        return false; // limit reconnection attempts
    }
    lastMqttAttempt = now;

    
    String willTopic = String(haBaseTopic) + "/status";
    if (mqttClient.connect(THING_NAME, mqttUser, mqttPass,
                           willTopic.c_str(), 0, true, "offline")) {

        String sub = String(haBaseTopic) + "/+/set";
        mqttClient.subscribe(sub.c_str());
        sendDiscovery();
        mqttClient.publish(willTopic.c_str(), "online", true);

        updateStatus();

        return true;
    } else {
      //  DEBUG_PRINT("failed, rc=");
      //  DEBUG_PRINTLN(mqttClient.state());
        return false;
    }
}


void setup() {

    //Serial.begin(74880);
    Serial.begin(38400,SERIAL_8E1);
    
    connectToWifi();

    delay(100);
    strncpy(haBaseTopic, BASE_TOPIC, sizeof(haBaseTopic));

    Serial.println("Starting MQTT server");

    mqttClient.setServer(mqttServer, atoi(mqttPort));
    mqttClient.setCallback(mqttCallback);   
    lastUpdate = millis(); 
    Serial.println("Setup complete");

}




void loop()
{
    ArduinoOTA.handle();

    if (!mqttClient.connected())
    {
        connectToMqtt();
    }
    else
    {
        mqttClient.loop();
    }

    if(mqttClient.connected() && millis() - lastUpdate > 30000)
    {
      updateStatus();      
    }


   // serialFlush();

    // if(millis() - lastSerialUpdate > 3000)
    // {
    //   handleSerial();
    //   lastSerialUpdate = millis();
    // }

    // if (millis() - lastStatusUpdate > 5000 && millis() - lastStatusRequest > 5000) {
    // //  requestStatus();
    //   delay(200);
    // //  handleSerial();

    //   //handleSerial();
    //  // publishState();
    // }
}
