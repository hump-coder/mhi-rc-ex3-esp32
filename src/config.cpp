#if 0
#include <FS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <config.h>
#include <wifi.h>



#define CONFIG_FILENAME "/config_x.json"

bool EspConfig::resetConfig() {
    return SPIFFS.remove(CONFIG_FILENAME);
}

bool EspConfig::saveConfig() {

    Serial.println("saving config...");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["hostname"] = host_name;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_topic"] = mqtt_topic;
    #ifdef STATICIP
    json["ip"] = static_ip;
    json["gw"] = static_gw;
    json["sn"] = static_sn;
    #endif


    File configFile = SPIFFS.open(CONFIG_FILENAME, "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
      return false;
    }

    json.printTo(Serial);
    Serial.println("");
    json.printTo(configFile);
    configFile.close();
    jsonBuffer.clear();
    return true;
}

EspConfig::EspConfig() {
    // if (init)
        // initEspConfig();
}

String genTopic() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String topic = "RCEX3-";
  for (uint8_t i=4; i<6; ++i) {
    topic += String(mac[i], 16);
  }
  return topic;
}

bool EspConfig::initEspConfig() {

    // Config cfg;

    if (SPIFFS.begin()) {
        Serial.println("mounted file system");
        if (SPIFFS.exists(CONFIG_FILENAME)) {
        //file exists, reading and loading
        Serial.println("reading config file");
        File configFile = SPIFFS.open(CONFIG_FILENAME, "r");
        if (configFile) {
            Serial.println("opened config file");
            size_t size = configFile.size();
            // Allocate a buffer to store contents of the file.
            std::unique_ptr<char[]> buf(new char[size]);

            configFile.readBytes(buf.get(), size);
            DynamicJsonBuffer jsonBuffer;
            JsonObject& json = jsonBuffer.parseObject(buf.get());
            json.printTo(Serial);
            if (json.success()) {
                Serial.println("\nparsed json");

                if (json.containsKey("hostname")) strncpy(host_name, json["hostname"], HOSTNAME_LEN);
                else strncpy(host_name, HOSTNAME, HOSTNAME_LEN);
                if (json.containsKey("mqtt_server")) strncpy(mqtt_server, json["mqtt_server"], 40);
                else strncpy(mqtt_server, MQTT_SERVER, 40);
                if (json.containsKey("mqtt_topic")) strncpy(mqtt_topic, json["mqtt_topic"], 40);
                else strncpy(mqtt_topic, genTopic().c_str(), 40);

                #ifdef STATICIP
                if (json.containsKey("ip")) strncpy(static_ip, json["ip"], 16);
                else strncpy(static_ip, IPADDR, 16);
                if (json.containsKey("gw")) strncpy(static_gw, json["gw"], 16);
                else strncpy(static_gw, GATEWAY, 16);
                if (json.containsKey("sn")) strncpy(static_sn, json["sn"], 16);
                else strncpy(static_sn, NETMASK, 16);
                #endif
            } else {
                Serial.println("failed to load json config");
                return false;
            }
        }
        }
    } else {
        Serial.println("failed to mount FS");
        return false;
    }
    return true;
}

#endif