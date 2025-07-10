#include <time.h>

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <wifi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "config.h"
#include "rc3serial.h"

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

struct ClimateState
{
  bool power;
  uint8_t mode;  // 0=auto,1=dry,2=cool,3=fan_only,4=heat
  uint8_t speed; // 0-4
  float targetTemp;
  float currentTemp;
} state = {false, 0, 0, 21.0f, 21.0f};

unsigned long lastStatusUpdate = 0;
unsigned long lastStatusRequest = 0;
unsigned long lastUpdate = 0;

void wifiConnected();

void mqttLog(String logMessage)
{

  char mbuf[128];
  snprintf(mbuf, sizeof(mbuf), "%s/log", haBaseTopic);

  mqttClient.publish(mbuf, logMessage.substring(0, 128).c_str());
  Serial.println(logMessage); // optional, also print to local serial
}

const char *modeToString(uint8_t m)
{
  switch (m)
  {
  case 0:
    return "auto";
  case 1:
    return "dry";
  case 2:
    return "cool";
  case 3:
    return "fan_only";
  case 4:
    return "heat";
  }
  return "off";
}

uint8_t modeFromString(const String &s)
{
  if (s.equalsIgnoreCase("auto"))
    return 0;
  if (s.equalsIgnoreCase("dry"))
    return 1;
  if (s.equalsIgnoreCase("cool"))
    return 2;
  if (s.equalsIgnoreCase("fan") || s.equalsIgnoreCase("fan_only"))
    return 3;
  if (s.equalsIgnoreCase("heat"))
    return 4;
  return 0xFF;
}

const char *fanModeToString(uint8_t s)
{
  switch (s)
  {
  case 0:
    return "auto";
  case 1:
    return "1";
  case 2:
    return "2";
  case 3:
    return "3";
  case 4:
    return "4";
  }
  return "auto";
}

uint8_t fanModeFromString(const String &s)
{
  if (s.equalsIgnoreCase("auto") || s == "0")
    return 0;
  if (s == "1")
    return 1;
  if (s == "2")
    return 2;
  if (s == "3")
    return 3;
  if (s == "4")
    return 4;
  return 0;
}

void publishState()
{
  char topic[128];
  char buf[16];

  snprintf(topic, sizeof(topic), "%s/power/state", haBaseTopic);
  mqttClient.publish(topic, state.power ? "ON" : "OFF", true);

  snprintf(topic, sizeof(topic), "%s/mode/state", haBaseTopic);
  if (state.power)
  {
    mqttClient.publish(topic, modeToString(state.mode), true);
  }
  else
  {
    mqttClient.publish(topic, modeToString(0xff), true);
  }

  snprintf(topic, sizeof(topic), "%s/fan_mode/state", haBaseTopic);
  mqttClient.publish(topic, fanModeToString(state.speed), true);

  snprintf(topic, sizeof(topic), "%s/temp/state", haBaseTopic);
  dtostrf(state.targetTemp, 0, 1, buf);
  mqttClient.publish(topic, buf, true);

  snprintf(topic, sizeof(topic), "%s/temperature/current", haBaseTopic);
  dtostrf(state.currentTemp, 0, 1, buf);
  mqttClient.publish(topic, buf, true);
}

bool parseStatus(const char *s)
{
  if (!s || strlen(s) < 32)
    return false;
  if (s[4] != '1')
    return false;

  char tbuf[3];
  strncpy(tbuf, &s[30], 2);
  tbuf[2] = '\0';
  int number = strtol(tbuf, NULL, 16);
  float temp = number * 0.5f;

  state.power = (s[13] == '1');

  switch (s[17])
  {
  case '1':
    state.mode = 1;
    break;
  case '2':
    state.mode = 2;
    break;
  case '3':
    state.mode = 3;
    break;
  case '4':
    state.mode = 4;
    break;
  default:
    state.mode = 0;
    break;
  }

  switch (s[21])
  {
  case '0':
    state.speed = 1;
    break;
  case '1':
    state.speed = 2;
    break;
  case '2':
    state.speed = 3;
    break;
  case '6':
    state.speed = 4;
    break;
  default:
    state.speed = 0;
    break;
  }

  // mqttLog(String("parseState: '") + tbuf + "'");
  // mqttLog(String("speed: '") + s[21] + "'");

  state.targetTemp = temp;
  state.currentTemp = temp;

  lastStatusUpdate = millis();
  publishState();
  return true;
}

void handleSerial()
{
  if (!Serial.available())
    return;
  size_t len = Serial.available();
  char rbuf[len + 1];
  Serial.readBytes(rbuf, len);
  char sbuf[len + 1];
  int sl = 0;
  for (uint8_t i = 1; i < len; ++i)
  {
    if (sl)
    {
      if ((uint8_t)rbuf[i] > 32 && (uint8_t)rbuf[i] < 127)
        sbuf[sl++] = rbuf[i];
    }
    else if (rbuf[i] == 'R')
    {
      sbuf[sl++] = rbuf[i];
    }
  }
  sbuf[sl] = '\0';
  parseStatus(sbuf);
}

void requestStatus()
{
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

void sendDiscovery()
{

  char topic[128];
  char payload[2048];

  snprintf(topic, sizeof(topic), "homeassistant/climate/%s/config", THING_NAME);
  snprintf(payload, sizeof(payload),
           "{"
           "%s,"
           "\"name\": \"%s\","
           "\"unique_id\": \"%s_climate\","
           "\"mode_command_topic\": \"%s/mode/set\","
           "\"mode_state_topic\": \"%s/mode/state\","
           "\"modes\": [\"off\", \"cool\", \"dry\", \"heat\", \"fan_only\", \"auto\"],"
           "\"fan_mode_command_topic\": \"%s/fan_mode/set\","
           "\"fan_mode_state_topic\": \"%s/fan_mode/state\","
           "\"fan_modes\": [\"auto\", \"1\", \"2\", \"3\", \"4\"],"
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
           haBaseTopic, haBaseTopic,
           haBaseTopic,
           haBaseTopic);

  mqttClient.publish(topic, payload, true);

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
           THING_NAME);

  mqttClient.publish(topic, payload, true);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String msg;
  for (unsigned int i = 0; i < length; ++i)
  {
    msg += (char)payload[i];
  }
  String t(topic);
  String base = String(haBaseTopic) + "/";
  if (!t.startsWith(base))
    return;
  String cmd = t.substring(base.length());

  if (cmd == "power/set")
  {
    bool on = msg.equalsIgnoreCase("ON") || msg == "1";
    setPowerOn(on ? 1 : 0);
    state.power = on;
  }
  else if (cmd == "mode/set")
  {
    if (msg.equalsIgnoreCase("off"))
    {
      setPowerOn(0);
      serialFlush();
      state.power = false;
    }
    else
    {
      uint8_t m = modeFromString(msg);
      if (m != 0xFF)
      {
        setPowerOn(1);
        serialFlush();
        setMode(m);
        serialFlush();
        state.mode = m;
        state.power = true;
      }
    }
  }
  else if (cmd == "temp/set")
  {
    float tval = msg.toFloat();
    state.targetTemp = tval;
    setTemp((uint16_t)(tval * 10));
    serialFlush();
  }
  else if (cmd == "fan_mode/set")
  {
    // mqttLog(String("setting fan got message: ") + msg);
    uint8_t sp = fanModeFromString(msg);
    // mqttLog(String("setting fan value from string: ") + sp);
    //
    // Special case - it seems we can't set fan speed to auto using the setFanSpeed()
    // method (it fails for some reason) instead we need to set it via the setClimate()
    // method.
    //
    if(sp == 0)
    {
      state.speed = sp;
      Settings settings;
      settings.degrees = state.targetTemp * 10;
      settings.mode = state.mode;
      settings.speed = state.speed;
      settings.power = state.power;
      
      // mqttLog(String("setting climate to: ") + sp);
      setClimate(settings);
    }
    else if (sp != 0xFF && sp <= 4)
    {
      state.speed = sp;
      // mqttLog(String("setting fan to: ") + sp);
      setFanSpeed(sp);
      serialFlush();
    }
  }
  else if (cmd == "delayOffHours/set")
  {
    uint8_t h = msg.toInt();
    if (h >= 1 && h <= 12)
    {
      setOffTimer(h);
      serialFlush();
    }
  }
  
  updateStatus();
  publishState();
}

///
///
///

void connectToWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  ArduinoOTA.setHostname(BASE_TOPIC);
  ArduinoOTA.begin();

  Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("IOT Thing name: %s\n", BASE_TOPIC);
}

bool connectToMqtt()
{

  static unsigned long lastMqttAttempt = 0;

  if (mqttClient.connected())
    return true;

  unsigned long now = millis();
  if (now - lastMqttAttempt < 1000)
  {
    return false; // limit reconnection attempts
  }
  lastMqttAttempt = now;

  String willTopic = String(haBaseTopic) + "/status";
  if (mqttClient.connect(THING_NAME, mqttUser, mqttPass, willTopic.c_str(), 0, true, "offline"))
  {

    String sub = String(haBaseTopic) + "/+/set";
    mqttClient.subscribe(sub.c_str());
    sendDiscovery();
    mqttClient.publish(willTopic.c_str(), "online", true);

    updateStatus();

    return true;
  }
  else
  {
    //  DEBUG_PRINT("failed, rc=");
    //  DEBUG_PRINTLN(mqttClient.state());
    return false;
  }
}

void setup()
{

  // Serial.begin(74880);
  Serial.begin(38400, SERIAL_8E1);

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

  // if(mqttClient.connected() && millis() - lastUpdate > 30000)
  // {
  //   updateStatus();
  // }

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
