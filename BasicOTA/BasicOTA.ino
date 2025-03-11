#define VERSION "980F.04"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include <WiFiUdp.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#else
#error this code only works for ESP 8266 or 32
#endif

#include <ArduinoOTA.h>

#ifndef STASSID
#define STASSID "honeyspot"
#define STAPSK "brigadoonwillbebacksoon"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

void startWifi(bool andShowMac) {
  Serial.println("Starting Wifi");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  
  if (andShowMac) {
    Serial.printf("My MAC id");
    byte ownAddress[6];
    WiFi.macAddress(ownAddress);
    for (unsigned i = 0; i < 6; ++i) {
      Serial.printf(":%02x", ownAddress[i]);
    }
    Serial.println();
  }
  
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void enableOTA(){
  // Port defaults to 8266 or 3232
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("\nStart updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin(); 
}

void setup() {
  Serial.begin(115200);
  Serial.println(VERSION);
  startWifi(true);
  enableOTA();
  Serial.println("Setup Complete\n\n");
}


void loop() {
  ArduinoOTA.handle();
  
  auto numChars = Serial.available();
  while (numChars-- > 0) {
    auto sea = Serial.read();
    Serial.print(char(sea >= 'A' ? sea ^ 0x20 : sea));
  }
  
}
