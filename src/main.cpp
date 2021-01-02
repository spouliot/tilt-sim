#include <Arduino.h>

#include "NimBLEDevice.h"
#include "NimBLEBeacon.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// https://kvurd.com/blog/tilt-hydrometer-ibeacon-data-format/

#define DEBUG 1

struct tilt {
    // static
    int index;
    BLEUUID uuid;
    String color;
    // dynamic
    bool enabled;
    uint16_t gravity;     // SG * 1000 or SG * 10000 for Tilt Pro/HD
    uint16_t temperature; // Deg F _or_ Deg F * 10 for Tilt Pro/HD
};

tilt tilt_array[] = {
  { 0, BLEUUID::fromString("A495BB10-C5B1-4B44-B512-1370F02D74DE"), "red", true, 1000, 32 },
  { 1, BLEUUID::fromString("A495BB20-C5B1-4B44-B512-1370F02D74DE"), "green", true, 1000, 32 },
  { 2, BLEUUID::fromString("A495BB30-C5B1-4B44-B512-1370F02D74DE"), "black", true, 1000, 32 },
  { 3, BLEUUID::fromString("A495BB40-C5B1-4B44-B512-1370F02D74DE"), "purple", true, 1000, 32 },
  { 4, BLEUUID::fromString("A495BB50-C5B1-4B44-B512-1370F02D74DE"), "orange", true, 1000, 32 },
  { 5, BLEUUID::fromString("A495BB60-C5B1-4B44-B512-1370F02D74DE"), "blue", true, 1000, 32 },
  { 6, BLEUUID::fromString("A495BB70-C5B1-4B44-B512-1370F02D74DE"), "yellow", true, 1000, 32 },
  { 7, BLEUUID::fromString("A495BB80-C5B1-4B44-B512-1370F02D74DE"), "pink", true, 1000, 32 },
  // Tilt Pro / https://tilthydrometer.com/products/tilt-pro-wireless-hydrometer-and-thermometer
  { 8, BLEUUID::fromString("A495BB10-C5B1-4B44-B512-1370F02D74DE"), "red*hd", true, 10000, 320 },
  { 9, BLEUUID::fromString("A495BB20-C5B1-4B44-B512-1370F02D74DE"), "green*hd", true, 10000, 320 },
  {10, BLEUUID::fromString("A495BB30-C5B1-4B44-B512-1370F02D74DE"), "black*hd", true, 10000, 320 },
  {11, BLEUUID::fromString("A495BB40-C5B1-4B44-B512-1370F02D74DE"), "purple*hd", true, 10000, 320 },
  {12, BLEUUID::fromString("A495BB50-C5B1-4B44-B512-1370F02D74DE"), "orange*hd", true, 10000, 320 },
  {13, BLEUUID::fromString("A495BB60-C5B1-4B44-B512-1370F02D74DE"), "blue*hd", true, 10000, 320 },
  {14, BLEUUID::fromString("A495BB70-C5B1-4B44-B512-1370F02D74DE"), "yellow*hd", true, 10000, 320 },
  {15, BLEUUID::fromString("A495BB80-C5B1-4B44-B512-1370F02D74DE"), "pink*hd", true, 10000, 320 },
};

BLEAdvertising *pAdvertising;
AsyncWebServer server(80);

void setup() {
#if DEBUG
  Serial.begin(115200);
#endif

  if (WiFi.begin(SSID, PASSWORD) == WL_CONNECT_FAILED) {
#if DEBUG
     Serial.println("Error starting WiFi");
#endif
  } else {
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
#if DEBUG
      Serial.print(".");
#endif
    }

    if (!MDNS.begin("tilt-sim")) {
  #if DEBUG
      Serial.println("Error starting mDNS");
  #endif
    } else {
      MDNS.addService("http", "tcp", 80);
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {    
      String html = "<html><head><title>Tilt Simulator</title>";
      String refresh = request->arg("refresh");
      if (refresh.length() > 0) {
        html += "<meta http-equiv=\"refresh\" content=\"" + refresh + "\">";
      }
      html += "</head><body><h1>Tilt Simulator</h1>";
      html += "<table>";
      html += "<tr><td>Name</td><td>Active</td><td>Gravity</td><td>Temperature</td></tr>";
      for(auto tilt: tilt_array) {
        bool pro = tilt.color.endsWith("*hd");
        html += "<tr><td>" + tilt.color + "</td><td align='center'>";
        if (tilt.enabled)
          html += "on";
        else
          html += "off";
        float g = tilt.gravity / (pro ? 10000.0f : 1000.0f);
        html += "</td><td align='right'>" + String(g, pro ? 4 : 3) + " sg</td>";
        float t = tilt.temperature / (pro ? 10.0f : 1.0f);
        html += "<td align='right'>" + String(t, pro ? 1 : 0) + "&deg;F</td></tr>";
      }
      html += "</table>";
      html += "<p>A simple REST API can be use to control the simulated devices. Examples:</p>";
      html += "<p>Disable all devices:</p>";
      String ip = WiFi.localIP ().toString ();
      html += "<code>curl \"http://" + ip + "/setTilt?name=*&active=off\"</code>";
      html += "<p>Enable the <b>Red*HD</b> (Pro) device with specific gravity and temperature values:</p>";
      html += "<code>curl \"http://" + ip + "/setTilt?name=red*hd&active=on&sg=1.2001&temp=65.1\"</code>";
      html += "</body></html>";
      request->send(200, "text/html", html);
    });

    server.on("/setTilt", HTTP_GET, [](AsyncWebServerRequest *request) {
      // name/color is required, everything else is optional
     int status = 404;
      String name = request->arg("name");
      for(auto t: tilt_array) {
        if ((name == "*") || (t.color == name)) {
          bool pro = t.color.endsWith ("*hd");
          status = 200;
          String active = request->arg("active");
          if (active == "on")
            t.enabled = true;
          else if (active == "off")
            t.enabled = false;

          String sg = request->arg("sg");
          if (sg.length() > 0)
            t.gravity = (uint16_t) (sg.toDouble() * (pro ? 10000 : 1000));

          String temp = request->arg("temp");
          if (temp.length() > 0)
            t.temperature = (uint16_t) (temp.toDouble() * (pro ? 10 : 1));

#if DEBUG
          if (pro) {
            Serial.printf ("%s %s %.4f sg %.1f F\n", t.color.c_str (), t.enabled ? "on" : "off", t.gravity / 10000.0f, t.temperature / 10.0f);
          } else {
            Serial.printf ("%s %s %.3f sg %d F\n", t.color.c_str (), t.enabled ? "on" : "off", t.gravity / 1000.0f, t.temperature);
          }
#endif
          tilt_array[t.index] = t;
        }
      }
      request->send(status, "text/html");
    });

    server.begin();
  }

  BLEDevice::init("");
  // boost power to maximum - assume we're USB (not battery) powered
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN ,ESP_PWR_LVL_P9);

  pAdvertising = BLEDevice::getAdvertising();
}

void setBeacon(tilt t) {
  BLEBeacon oBeacon = BLEBeacon();
  // Apple iBeacon id (w/endian change)
  oBeacon.setManufacturerId(0x4C00);
  oBeacon.setProximityUUID(t.uuid);
  oBeacon.setMajor(t.temperature);
  oBeacon.setMinor(t.gravity);
  
  std::string strServiceData = "";
  strServiceData += (char)26;     // Len
  strServiceData += (char)0xFF;   // Type
  strServiceData += oBeacon.getData(); 

  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  oAdvertisementData.addData(strServiceData);
  oAdvertisementData.setFlags(0x04); // BR_EDR_NOT_SUPPORTED 0x04
  pAdvertising->setAdvertisementData(oAdvertisementData);

  BLEAdvertisementData oScanResponseData = BLEAdvertisementData();
  pAdvertising->setScanResponseData(oScanResponseData);

  pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
}

void loop() {
  for(auto tilt: tilt_array) {
    if (!tilt.enabled)
      continue;
    setBeacon(tilt);
    pAdvertising->start();
    delay(100);
    pAdvertising->stop();
    delay(100);
  }
}
