/*!
 *Arduino ESP32 WiFi Web Server for the DF Robot Gravity Geiger Counter SEN0463
 *@file geiger.ino
 *@brief detects the CPM radiation intensity, the readings may have a large deviation in the first few times, and the data tends to be stable after 3 times
 *@copyright   Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 *@licence     The MIT License (MIT)
 *@authors [fengli](li.feng@dfrobot.com), [sighmon](sighmon@sighmon.com)
 *@version  V1.0
 *@date  2021-9-17
 *@get from https://www.dfrobot.com
 *@https://github.com/DFRobot/DFRobot_Geiger
*/

#include "secrets.h"

// Task scheduler
#include <TaskScheduler.h>
void readSensorCallback();
void sendDataToSafecastCallback();
Task readSensorTask(3000, -1, &readSensorCallback);  // Read sensor every 3 seconds
Task sendDataToSafecastTask(300000, -1, &sendDataToSafecastCallback);  // Send to SafeCast every 300 seconds (5 minutes)
Scheduler runner;

// DF Robot sensor init
#include <DFRobot_Geiger.h>
#if defined ESP32
#define detect_pin 22  // Pin 22 for a TTGO LoRa ESP32 V2
#else
#define detect_pin 22
#endif
/*!
 * @brief Constructor
 * @param pin external interrupt pin
 */
DFRobot_Geiger geiger(detect_pin);
float cpm;
float nsvh;
float usvh;

// WiFi init
#include <WiFi.h>
#include <HTTPClient.h>
const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASSWORD;

WiFiServer server(80);
HTTPClient http;

// NTP time
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Task callback
void readSensorCallback() {
  //Get the current CPM, if it has been paused, the CPM is the last value before the pause
  //Predict CPM by falling edge pulse within 3 seconds, the error is ±3CPM
  cpm = geiger.getCPM();
  //Get the current nSv/h, if it has been paused, nSv/h is the last value before the pause
  nsvh = geiger.getnSvh();
  //Get the current μSv/h, if it has been suspended, μSv/h is the last value before the suspension
  usvh = geiger.getuSvh();
}

void sendDataToSafecastCallback() {
  if (SAFECAST_API_KEY != "") {
    http.begin(SAFECAST_API_URL + (String)"?api_key=" + SAFECAST_API_KEY);
    http.addHeader("Content-Type", "application/json");
    // JSON data to send with HTTP POST
    String timestamp = timeClient.getFormattedTime();
    String httpRequestData = (String)"{\"longitude\":\"" + SAFECAST_DEVICE_LONGITUDE + (String)"\",\"latitude\":\"" + SAFECAST_DEVICE_LATITUDE + (String)"\",\"device_id\":\"" + SAFECAST_DEVICE_ID + (String)"\",\"value\":\"" + usvh + (String)"\",\"unit\":\"usv\",\"captured_at\":\"" + timestamp + (String)"\"}";
    // Send HTTP POST request
    int httpResponseCode = http.POST(httpRequestData);
    Serial.print("HTTP Response code from SafeCast: ");
    Serial.println(httpResponseCode);
    http.end();
  } else {
    Serial.println("Sign up for a SafeCast API Key at https://api.safecast.org");
  }
}

void setup() {

  Serial.begin(115200);
  while (!Serial) {
    delay(100);
  }
  // DF Robot sensor setup
  // Start counting, enable external interrupt
  geiger.start();

  // WiFi setup
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();

  // Setup tasks
  readSensorTask.enable();
  runner.addTask(readSensorTask);
  sendDataToSafecastTask.enable();
  runner.addTask(sendDataToSafecastTask);
  runner.enableAll();

  // Setup NTP time
  timeClient.begin();
  timeClient.setTimeOffset(0);  // GMT
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
}

void loop() {

  runner.execute();

  // WiFi server
  WiFiClient client = server.available();   // listen for incoming clients
  if (client) {                             // if you get a client,
    Serial.println("New Client.");           // print a message out the serial port    
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html; charset=UTF-8");
            client.println();

            // Print sensor readings
            Serial.print("CPM: ");
            Serial.println(cpm);
            Serial.print("nSv/h: ");
            Serial.println(nsvh);
            Serial.print("μSv/h: ");
            Serial.println(usvh);
            Serial.println();

            // Send Prometheus data
            client.print("# HELP cpm CPM\n");
            client.print("# TYPE cpm gauge\n");
            client.print((String)"cpm " + cpm + "\n");
            client.print("# HELP radiation_nsvh Radiation nSv/h\n");
            client.print("# TYPE radiation_nsvh gauge\n");
            client.print((String)"radiation_nsvh " + nsvh + "\n");
            client.print("# HELP radiation_usvh Radiation μSv/h\n");
            client.print("# TYPE radiation_usvh gauge\n");
            client.print((String)"radiation_usvh " + usvh + "\n");

            // The HTTP response ends with another blank line:
            client.print("\n");
            // break out of the while loop:
            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");
  }
}
