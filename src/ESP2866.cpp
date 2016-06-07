#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#include "Credentials.hpp"

#define USE_SERIAL Serial
#define S_FIND(S, A, B) S->findUntil(A, strlen(A), B, strlen(B))

typedef struct {
  char planned[6] = {'\0'};
  char actual[6] = {'\0'};
  char product[6] = {'\0'};
  char destination[41] = {'\0'};
  char platform[11] = {'\0'};
} departure;

ESP8266WiFiMulti WiFiMulti;

HTTPClient http;

void connectWiFi();
void initOTA();
bool checkTrains(departure trains[]);

void setup() {
  USE_SERIAL.begin(/*115200*/9600);
  // USE_SERIAL.setDebugOutput(true);
  for (uint8_t t = 4; t > 0; t--) {
    USE_SERIAL.printf("[SETUP] WAIT %d...\n", t);
    USE_SERIAL.flush();
    delay(1000);
  }

  connectWiFi();
  initOTA();

  http.setReuse(true);
}

void displayDeparture(departure train) {
  Serial.print("\x02L1");
  Serial.print(strlen(train.actual) > 0 ? train.actual : train.planned);
  Serial.print(" (");
  Serial.print(train.planned);
  Serial.print(") ");
  String platform = String(train.platform);
  platform.replace("Gleis ", "G");
  Serial.print(platform);
  Serial.print("\x03");

  Serial.print("\x02L2");
  Serial.print(train.destination);
  Serial.print("\x03");

  Serial.print("\x02L4");
  Serial.print(train.product);
  Serial.print(" | TALENT 2");
  Serial.print("\x03");
}

void loop() {
  const int CHECK_INTERVAL = 60000;
  static long lastMillis = -CHECK_INTERVAL;

  while (Serial.available()) {
    Serial.read();
    lastMillis = -CHECK_INTERVAL;
  }

  // wait for WiFi connection
  if (millis() - lastMillis >= CHECK_INTERVAL && WiFiMulti.run() == WL_CONNECTED) {
    departure trains[5];
    if (checkTrains(trains)) {
      Serial.println("Abfahrtszeiten:");
      for (uint8_t i = 0; i < 5; i++) {
        Serial.println("==========");
        Serial.print(trains[i].product);
        Serial.print(": ");
        Serial.print(trains[i].destination);
        Serial.print(", Abfahrt: ");
        Serial.print(trains[i].planned);
        Serial.print(", aktuell: ");
        Serial.print(trains[i].actual);
        Serial.print(" ");
        Serial.println(trains[i].platform);
      }

      displayDeparture(trains[0]);
      lastMillis = millis();
    } else {
      // No connection.
      lastMillis = millis() - CHECK_INTERVAL / 2;
    }

  }

  ArduinoOTA.handle();
}

bool checkTrains(departure trains[]) {
  bool success = false;
  // http://fahrinfo.vbb.de/bin/stboard.exe/dn?input=9037168&boardType=dep&time=16:03&maxJourneys=50&dateBegin=04.06.16&dateEnd=10.12.16&selectDate=today&productsFilter=111011101&start=yes&pageViewMode=PRINT&dirInput=&
  http.begin("http://fahrinfo.vbb.de/bin/stboard.exe/dn?input=9037168&boardType=dep&time=&maxJourneys=5&dateBegin=&dateEnd=&selectDate=today&productsFilter=111011101&start=yes&pageViewMode=PRINT&dirInput=&");

  int httpCode = http.GET();
  if (httpCode > 0) {
    USE_SERIAL.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      // get lenght of document (is -1 when Server sends no Content-Length header)
      int len = http.getSize();

      // get tcp stream
      WiFiClient * stream = http.getStreamPtr();

      const char tableStart[] = "<table class=\"resultTable";
      const char tableEnd[] =  "</table>";
      if (S_FIND(stream, tableStart, tableEnd)) {
        // We're in the results table now.
        const char tableBodyStart[] = "<tbody>";
        if (S_FIND(stream, tableBodyStart, tableEnd)) {
          // We're in the tbody now.
          for (uint8_t i = 0; i < 5; i++) {
            const char tableRowStart[] = "<tr class=\"depboard";
            const char tableRowEnd[] = "</tr>";
            if (S_FIND(stream, tableRowStart, tableEnd)) {
              // We're inside a <tr> now.
              if (S_FIND(stream, ">", tableRowEnd) /* <tr> ends */ && S_FIND(stream, ">\n", tableRowEnd) /* <td> ends */) {
                uint8_t bytes_read = stream->readBytes(trains[i].planned, 5);
                trains[i].planned[bytes_read] = '\0';
                if (S_FIND(stream, "prognosis\">", "</td>") && S_FIND(stream, "> ", "</span>")) {
                  bytes_read = stream->readBytes(trains[i].actual, 5);
                  trains[i].actual[bytes_read] = '\0';
                }
                if (S_FIND(stream, "<td class=\"product", tableRowEnd) && S_FIND(stream, ">", tableRowEnd) && S_FIND(stream, "<span", tableRowEnd) && S_FIND(stream, ">", tableRowEnd)) {
                  bytes_read = stream->readBytesUntil('<', trains[i].product, 5);
                  trains[i].product[bytes_read] = '\0';
                  if (S_FIND(stream, "<td class=\"timetable", tableRowEnd) && S_FIND(stream, ">", tableRowEnd) && S_FIND(stream, "<strong", tableRowEnd) && S_FIND(stream, ">\n", tableRowEnd)) {
                    bytes_read = stream->readBytesUntil('\n', trains[i].destination, 40);
                    trains[i].destination[bytes_read] = '\0';
                    String tmp = String(trains[i].destination);
                    tmp.replace(" (Berlin)", "");
                    tmp.toCharArray(trains[i].destination, 41);
                    if (S_FIND(stream, "<td class=\"platform", tableRowEnd) && S_FIND(stream, ">", tableRowEnd) && S_FIND(stream, "</strong>\n", tableRowEnd)) {
                      bytes_read = stream->readBytesUntil('<', trains[i].platform, 10);
                      trains[i].platform[bytes_read] = '\0';
                    }
                  }
                }
              }
            }
          }
        }
      }

      USE_SERIAL.println();
      USE_SERIAL.print("[HTTP] connection closed or file end.\n");

      success = true;
    } else {
      USE_SERIAL.printf("[HTTP] GET... unknown code: %d\n", httpCode);
    }
  } else {
    USE_SERIAL.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();

  return success;
}

void initOTA() {
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void connectWiFi() {
  // @todo Why use WiFiMulti?
  WiFiMulti.addAP(SSID, PASS);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
