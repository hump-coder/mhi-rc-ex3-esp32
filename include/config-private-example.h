#pragma once

#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your pass"

#define MQTT_SERVER "192.168.1.11"
#define MQTT_PORT "1883"
#define MQTT_USER "mqtt-user"
#define MQTT_PASSWORD "mqtt pass"

#define DEVICE_NAME "mhi-ac-rc-ex3-1"
#define BASE_TOPIC "mhi-ac-rc-ex3-1"

#define MQTT_DEVICE \
    "\"device\": {" \
      "\"identifiers\": [\"esp8266_hvac_controller\"]," \
      "\"name\": \"HVAC Controller\"," \
      "\"manufacturer\": \"HumpTech\"," \
      "\"model\": \"ESP8266-MHI-RC-1\"," \
      "\"sw_version\": \"1.0\"" \
    "}"