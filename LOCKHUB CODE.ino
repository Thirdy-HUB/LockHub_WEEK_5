#include <ESP32Servo.h>
#include <WiFi.h>
#include <DHT.h>

const char* ssid = "RW5800-273C";
const char* password = "Almazan2024";

WiFiServer server(8888);
WiFiClient client;

// Relay pins for each room (OCCUPIED, LIGHT)
int roomRelays[4][2] = {
  {13, 14},     // ROOM 101
  {26, 25},     // ROOM 201
  {0, 0},       // ROOM 301
  {0, 0}        // ROOM 401
};

// Reset buttons for each room
int resetButtons[4] = {32, 33, 0, 0};

unsigned long lastDebounceTime[4] = {0};
bool lastButtonState[4] = {HIGH};

// DHT22 setup
#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Servo for ROOM 201
Servo room201Servo;
#define SERVO_PIN 27  // PWM-capable pin

bool room101Active = false;
unsigned long lastDHTRead = 0;
unsigned long lastTempSend = 0;
const long tempSendInterval = 5000; // Send every 5 sec

// Temperature threshold
const float tempThreshold = 29.0;

// Numerical Method variables
#define MAX_READINGS 10
float tempReadings[MAX_READINGS];
int tempIndex = 0;
bool readingsFilled = false;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("Server started");

  for (int i = 0; i < 4; i++) {
    if (roomRelays[i][0] != 0) pinMode(roomRelays[i][0], OUTPUT);
    if (roomRelays[i][1] != 0) pinMode(roomRelays[i][1], OUTPUT);
    digitalWrite(roomRelays[i][0], HIGH);
    digitalWrite(roomRelays[i][1], HIGH);
    if (resetButtons[i] != 0) pinMode(resetButtons[i], INPUT_PULLUP);
  }

  dht.begin();

  // Initialize Servo
  room201Servo.attach(SERVO_PIN);
  room201Servo.write(180); // Start at 180° (initial reset position)
}

void loop() {
  if (!client || !client.connected()) {
    client = server.available();
    return;
  }

  if (client.available()) {
    String command = client.readStringUntil('\n');
    command.trim();
    Serial.println("Received: " + command);
    handleCommand(command);
  }

  // Handle reset buttons
  for (int i = 0; i < 4; i++) {
    if (resetButtons[i] == 0) continue;

    bool buttonPressed = digitalRead(resetButtons[i]) == HIGH;
    if (buttonPressed && !lastButtonState[i]) {
      if (millis() - lastDebounceTime[i] > 300) {
        String msg = "RESET:" + String(i + 1) + ":01";
        client.println(msg);
        Serial.println("Sent: " + msg);
        digitalWrite(roomRelays[i][0], HIGH);
        digitalWrite(roomRelays[i][1], HIGH);
        lastDebounceTime[i] = millis();

        if (i == 0) {
          room101Active = false;
          tempIndex = 0;
          readingsFilled = false;
        }

        if (i == 1) {
          room201Servo.write(180); // Reset servo to 180°
        }
      }
    }
    lastButtonState[i] = buttonPressed;
  }

  // Temperature updates
  if (room101Active && millis() - lastTempSend > tempSendInterval) {
    float temp = dht.readTemperature();

    if (!isnan(temp)) {
      // Store reading
      tempReadings[tempIndex] = temp;
      tempIndex = (tempIndex + 1) % MAX_READINGS;
      if (tempIndex == 0) readingsFilled = true;

      float predictedTemp = temp;

      if (readingsFilled) {
        // Linear regression - smoothed and trend-adjusted temp value
        float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        for (int i = 0; i < MAX_READINGS; i++) {
          sumX += i;
          sumY += tempReadings[i];
          sumXY += i * tempReadings[i];
          sumX2 += i * i;
        }

        float slope = (MAX_READINGS * sumXY - sumX * sumY) / (MAX_READINGS * sumX2 - sumX * sumX);
        float intercept = (sumY - slope * sumX) / MAX_READINGS;
        predictedTemp = slope * MAX_READINGS + intercept;

        client.println("PREDICT:" + String(predictedTemp, 1));
        Serial.println("Sent: PREDICT:" + String(predictedTemp, 1));
      }

      // Send actual temp
      client.println("TEMP:" + String(temp, 1));
      Serial.println("Sent: TEMP:" + String(temp, 1));

      if (temp >= tempThreshold) {
        client.println("ALERT:HIGH_TEMP");
        Serial.println("Sent: ALERT:HIGH_TEMP");
      }

      lastTempSend = millis();
    } else {
      Serial.println("Failed to read temperature from DHT sensor");
    }
  }
}

void handleCommand(String cmd) {
  if (cmd.startsWith("ROOM:")) {
    int roomNum = cmd.substring(5, 8).toInt();
    String state = cmd.substring(9);
    int index = (roomNum / 100) - 1;

    if (index >= 0 && index < 4 && roomRelays[index][0] != 0) {
      if (state == "OCCUPIED") {
        digitalWrite(roomRelays[index][0], LOW);
        if (index == 0) room101Active = true;
        if (index == 1) {
          room201Servo.write(0);  // Move servo to 0°
          Serial.println("ROOM 201 OCCUPIED: Servo moved to 0°");
        }
      } else {
        digitalWrite(roomRelays[index][0], HIGH);
        if (index == 0) {
          room101Active = false;
          tempIndex = 0;
          readingsFilled = false;
        }
        if (index == 1) {
          room201Servo.write(180);   // Move servo back to 180°
          Serial.println("ROOM 201 VACANT: Servo moved to 180°");
        }
      }
    }
  } else if (cmd.startsWith("LIGHT:")) {
    int roomNum = cmd.substring(6, 9).toInt();
    String state = cmd.substring(10);
    int index = (roomNum / 100) - 1;

    if (index >= 0 && index < 4 && roomRelays[index][1] != 0) {
      if (state == "ON") {
        digitalWrite(roomRelays[index][1], LOW);
      } else {
        digitalWrite(roomRelays[index][1], HIGH);
      }
    }
  } else if (cmd.startsWith("RESET:")) {
    int roomNum = cmd.substring(6).toInt();
    int index = (roomNum / 100) - 1;

    if (index >= 0 && index < 4) {
      if (roomRelays[index][0] != 0) digitalWrite(roomRelays[index][0], HIGH);
      if (roomRelays[index][1] != 0) digitalWrite(roomRelays[index][1], HIGH);

      if (index == 0) {
        room101Active = false;
        tempIndex = 0;
        readingsFilled = false;
      }

      if (index == 1) {
        room201Servo.write(180); // Reset servo
      }
    }
  }
}
