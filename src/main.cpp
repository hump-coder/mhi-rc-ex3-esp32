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
char haBaseTopic[64];

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

  char mbuf[2048];
  sprintf(mbuf, "%s/log", BASE_TOPIC);

  mqttClient.publish(mbuf, logMessage.c_str());
  Serial.println(logMessage); // optional, also print to local serial
}



void sendDiscovery() {
  mqttLog("Sending discovery messages:");

  char topic[128];
  snprintf(topic, sizeof(topic), "homeassistant/switch/%s/power/config", THING_NAME);
  char payload[1024];


  //
  // Power on/off
  //

  const char *item = "power";

  snprintf(payload, sizeof(payload), "{\"name\":\"%s\",\"command_topic\":\"%s/%s/set\",\"state_topic\":\"%s/%s/state\",\"unique_id\":\"%s_%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\"}", item, haBaseTopic, item, haBaseTopic, item, THING_NAME, item);
    
  
  mqttLog(payload);
  mqttClient.publish(topic, payload, true);


snprintf(topic, sizeof(topic), "homeassistant/climate/%s/config", THING_NAME);
snprintf(payload, sizeof(payload),
  "{"
    "\"name\": \"%s\","
    "\"uniq_id\": \"%s_climate\","
    "\"mode_cmd_t\": \"%s/mode/set\","
    "\"mode_stat_t\": \"%s/mode/state\","
    "\"modes\": [\"off\", \"cool\", \"dry\", \"heat\", \"fan_only\", \"auto\"],"
    "\"temp_cmd_t\": \"%s/temp/set\","
    "\"temp_stat_t\": \"%s/temp/state\","
    "\"min_temp\": 16,"
    "\"max_temp\": 30,"
    "\"temp_step\": 0.5,"
    "\"curr_temp_t\": \"%s/temperature/current\","
    "\"avty_t\": \"%s/status\","
    "\"pl_avail\": \"online\","
    "\"pl_not_avail\": \"offline\","
    "%s"
  "}",
  THING_NAME,
  THING_NAME,
  haBaseTopic, haBaseTopic,
  haBaseTopic, haBaseTopic,
  haBaseTopic,
  haBaseTopic,
  MQTT_DEVICE
);

mqttLog(payload);

mqttClient.publish(topic, payload, true);

//
// Fan speed: 0 - 4
//
snprintf(topic, sizeof(topic), "homeassistant/number/%s/fan_speed/config", THING_NAME);
snprintf(payload, sizeof(payload),
  "{"
    "\"name\": \"Fan Speed\","
    "\"command_topic\": \"%s/speed/set\","
    "\"state_topic\": \"%s/speed/state\","
    "\"unique_id\": \"%s_fan_speed\","
    "\"min\": 0,"
    "\"max\": 4,"
    "\"step\": 1,"
    "\"mode\": \"box\","
    "%s"
  "}",
  haBaseTopic, haBaseTopic,
  THING_NAME,
  MQTT_DEVICE
);


mqttLog(payload);

mqttClient.publish(topic, payload, true);


snprintf(topic, sizeof(topic), "homeassistant/number/%s/set_temp/config", THING_NAME);
snprintf(payload, sizeof(payload),
  "{"
    "\"name\": \"Target Temperature\","
    "\"command_topic\": \"%s/temp/set\","
    "\"state_topic\": \"%s/temp/state\","
    "\"unique_id\": \"%s_set_temp\","
    "\"min\": 16,"
    "\"max\": 30,"
    "\"step\": 1,"
    "\"mode\": \"box\","
    "%s"
  "}",
  haBaseTopic, haBaseTopic,
  THING_NAME,
  MQTT_DEVICE
);


mqttLog(payload);

mqttClient.publish(topic, payload, true);

snprintf(topic, sizeof(topic), "homeassistant/number/%s/delay_off/config", THING_NAME);
snprintf(payload, sizeof(payload),
  "{"
    "\"name\": \"Delay Off Hours\","
    "\"command_topic\": \"%s/delayOffHours/set\","
    "\"state_topic\": \"%s/delayOffHours/state\","
    "\"unique_id\": \"%s_delay_off\","
    "\"min\": 1,"
    "\"max\": 12,"
    "\"step\": 1,"
    "\"mode\": \"box\","
    "%s"
  "}",
  haBaseTopic, haBaseTopic,
  THING_NAME,
  MQTT_DEVICE
);


mqttLog(payload);

mqttClient.publish(topic, payload, true);
  
}



void mqttCallback(char *topic, byte *payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; ++i) {
        msg += (char)payload[i];
    }
    mqttLog((String("Got MQTT message payload ['") +msg + String("'")).c_str());

    //mqttLog("Message received [");
    // DEBUG_PRINT(topic);
    // DEBUG_PRINT("] ");
    // DEBUG_PRINTLN(msg);
    // String t(topic);
    // String prefix = String(haBaseTopic) + "/zone";
    // if (t.startsWith(prefix) && t.endsWith("/set")) {
    //     int zone = t.substring(prefix.length(), t.length() - 4).toInt();
    //     if (zone >= 1 && zone <= numZones) {
    //         bool newState = msg.equalsIgnoreCase("ON") || msg.equalsIgnoreCase("OPEN");
    //         zoneState[zone - 1] = newState;
    //         stateChanged = true;
    //         lastChangeTime = millis();
    //         applyZones();
    //     }
    // }
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

    
    if (mqttClient.connect(THING_NAME, mqttUser, mqttPass)) {
    
        String sub = String(haBaseTopic) + "/+/set";
        mqttClient.subscribe(sub.c_str());
        sendDiscovery();
    //    publishAllStates();
    //    publishAllZoneNames();
        return true;
    } else {
      //  DEBUG_PRINT("failed, rc=");
      //  DEBUG_PRINTLN(mqttClient.state());
        return false;
    }
}

void setup() {

    Serial.begin(74880);
    //Serial.begin(38400,SERIAL_8E1);
    
    connectToWifi();

    delay(100);

    Serial.println("Starting MQTT server");

    mqttClient.setServer(mqttServer, atoi(mqttPort));
    mqttClient.setCallback(mqttCallback);

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

    // check UART for data
    // if (Serial.available())
    // {
    //     size_t len = Serial.available();
    //     uint8_t sbuf[len];
    //     Serial.readBytes(sbuf, len);
    //     if (localClient && localClient.connected())
    //     {
    //         localClient.write(sbuf, len);
    //     }
    // }
}
