/*
 * Elite Dangerous UDP Receiver for ESP32
 * 
 * Simple example showing how to receive Elite Dangerous events over UDP
 * Connect to your WiFi network and receive broadcast packets
 * 
 * Protocol: TYPE|JSON_DATA
 * Examples:
 *   JOURNAL|{"event":"Docked","StationName":"Jameson Memorial"}
 *   STATUS|{"timestamp":"2025-11-15T13:44:53Z","event":"Status","Flags":151060493}
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// WiFi Configuration - UPDATE THESE
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// UDP Configuration
const unsigned int udpPort = 12345;
WiFiUDP udp;

// Buffer for incoming packets
char packetBuffer[1024];

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\nElite Dangerous UDP Receiver for ESP32");
  Serial.println("========================================");
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Start UDP listener
  if (udp.begin(udpPort)) {
    Serial.print("Listening on UDP port: ");
    Serial.println(udpPort);
  } else {
    Serial.println("ERROR: Failed to start UDP listener!");
  }
  
  Serial.println("Ready to receive Elite Dangerous events...\n");
}

void loop() {
  // Check for incoming UDP packets
  int packetSize = udp.parsePacket();
  
  if (packetSize) {
    // Read the packet
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = 0; // Null terminate
    }
    
    // Parse the simple protocol: TYPE|JSON_DATA
    String message = String(packetBuffer);
    int separatorPos = message.indexOf('|');
    
    if (separatorPos > 0) {
      String eventType = message.substring(0, separatorPos);
      String jsonData = message.substring(separatorPos + 1);
      
      // Parse JSON
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, jsonData);
      
      if (!error) {
        handleEvent(eventType, doc);
      } else {
        Serial.print("JSON Parse Error: ");
        Serial.println(error.c_str());
      }
    }
  }
  
  // Small delay to prevent watchdog issues
  delay(10);
}

void handleEvent(String eventType, JsonDocument& data) {
  /*
   * Process received event
   * Here you can add your custom logic based on event types
   */
  
  Serial.println("----------------------------------------");
  Serial.print("Type: ");
  Serial.println(eventType);
  
  if (eventType == "JOURNAL") {
    String event = data["event"].as<String>();
    Serial.print("Event: ");
    Serial.println(event);
    
    // Handle specific journal events
    if (event == "Docked") {
      String station = data["StationName"].as<String>();
      String system = data["StarSystem"].as<String>();
      Serial.print("Docked at: ");
      Serial.print(station);
      Serial.print(" in ");
      Serial.println(system);
      
      // Example: Light up an LED, show on display, etc.
      
    } else if (event == "Undocked") {
      String station = data["StationName"].as<String>();
      Serial.print("Undocked from: ");
      Serial.println(station);
      
    } else if (event == "FSDJump") {
      String system = data["StarSystem"].as<String>();
      float distance = data["JumpDist"];
      Serial.print("Jumped to: ");
      Serial.print(system);
      Serial.print(" (");
      Serial.print(distance);
      Serial.println(" ly)");
      
    } else if (event == "Location") {
      String system = data["StarSystem"].as<String>();
      Serial.print("Current System: ");
      Serial.println(system);
      
      if (data["Docked"]) {
        String station = data["StationName"].as<String>();
        Serial.print("Docked at: ");
        Serial.println(station);
      }
    }
    
    String timestamp = data["timestamp"].as<String>();
    Serial.print("Timestamp: ");
    Serial.println(timestamp);
    
  } else if (eventType == "STATUS") {
    // Ship status updates (frequent)
    int flags = data["Flags"];
    float cargo = data["Cargo"];
    
    Serial.print("Flags: ");
    Serial.println(flags);
    Serial.print("Cargo: ");
    Serial.print(cargo);
    Serial.println("t");
    
    // Check if we have fuel data
    if (data.containsKey("Fuel")) {
      float fuelMain = data["Fuel"]["FuelMain"];
      Serial.print("Fuel: ");
      Serial.print(fuelMain);
      Serial.println("t");
    }
    
    // Decode flags (examples)
    bool docked = flags & 0x01;
    bool landed = flags & 0x02;
    bool landingGearDown = flags & 0x04;
    bool shieldsUp = flags & 0x08;
    bool supercruise = flags & 0x10;
    
    Serial.print("Docked: ");
    Serial.println(docked ? "Yes" : "No");
    Serial.print("Landed: ");
    Serial.println(landed ? "Yes" : "No");
    Serial.print("Supercruise: ");
    Serial.println(supercruise ? "Yes" : "No");
  }
  
  Serial.println();
}
