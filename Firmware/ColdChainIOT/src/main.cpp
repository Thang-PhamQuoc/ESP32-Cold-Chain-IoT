#include "UbidotsEsp32Mqtt.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h> 

const char *UBIDOTS_TOKEN = "BBUS-u2N7ayEIbyUq41lxo9xYtDhE1FXUtq"; 
const char *WIFI_SSID = "The New";             
const char *WIFI_PASS = "11111111";         
const char *DEVICE_LABEL = "esp32"; 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define RXD2 16
#define TXD2 17
HardwareSerial neogps(2);
TinyGPSPlus gps;

const int MQ135_PIN = 34; 
const int ONE_WIRE_BUS = 4; 
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

const int BUZZER_PIN = 5; 
bool isOverTemp = false;          
bool buzzerRinging = false;       
unsigned long buzzerStartTime = 0;

bool enableAir = true;
bool enableTemp = true;
bool enableGPS = true;
bool enableBuzzer = true; 

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;   

Ubidots client(UBIDOTS_TOKEN);

float currentLat = 0.0; 
float currentLon = 0.0;

void callback(char *topic, byte *payload, unsigned int length) {
  String topicStr = String(topic);
  String payloadStr = "";
  for (int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }
  int value = payloadStr.toInt(); 

  if (topicStr.indexOf("sw-air") > 0) enableAir = (value == 1);
  if (topicStr.indexOf("sw-temp") > 0) enableTemp = (value == 1);
  if (topicStr.indexOf("sw-gps") > 0) enableGPS = (value == 1);
  
  if (topicStr.indexOf("sw-buzzer") > 0) {
    enableBuzzer = (value == 1);
    if (!enableBuzzer) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerRinging = false;
      isOverTemp = false; 
    }
  }
  
  Serial.print(">>> Ubidots ra lenh: ");
  Serial.print(topicStr);
  Serial.print(" -> ");
  Serial.println(value == 1 ? "BAT" : "TAT");
}

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); 
  
  neogps.begin(9600, SERIAL_8N1, RXD2, TXD2);
  sensors.begin();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0, 20);
  display.println("Connecting WiFi...");
  display.display();

  Serial.println("Dang ket noi WiFi va Ubidots...");
  client.connectToWifi(WIFI_SSID, WIFI_PASS);
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Da cau hinh dong bo thoi gian NTP!");

  client.setCallback(callback); 
  client.setup();
  client.reconnect();

  client.subscribe("/v1.6/devices/esp32/sw-air/lv");
  client.subscribe("/v1.6/devices/esp32/sw-temp/lv");
  client.subscribe("/v1.6/devices/esp32/sw-gps/lv");
  client.subscribe("/v1.6/devices/esp32/sw-buzzer/lv"); 

  Serial.println("Setup hoan tat!");
}

void loop() {
  if (!client.connected()) {Serial.println("Mat ket noi MQTT, dang thu ket noi lai...");
    client.reconnect();
    client.subscribe("/v1.6/devices/esp32/sw-air/lv");
    client.subscribe("/v1.6/devices/esp32/sw-temp/lv");
    client.subscribe("/v1.6/devices/esp32/sw-gps/lv");
    client.subscribe("/v1.6/devices/esp32/sw-buzzer/lv"); 
  }

  // A. ĐỌC DỮ LIỆU CẢM BIẾN MQ-135 & DS18B20 & XỬ LÝ BUZZER
  int airValue = 0;
  if (enableAir) {
    airValue = analogRead(MQ135_PIN); 
  }
  
  float tempC = -127.00;
  if (enableTemp) {
    sensors.requestTemperatures();
    tempC = sensors.getTempCByIndex(0);

    if (tempC > 40.0) {
      if (!isOverTemp && enableBuzzer) { 
        isOverTemp = true;
        buzzerRinging = true;
        buzzerStartTime = millis();
        digitalWrite(BUZZER_PIN, HIGH);
        Serial.println("Canh bao: Nhiet do > 40C. Buzzer ON!");
      }
    } else {
      isOverTemp = false; 
    }
  }

  if (buzzerRinging && (millis() - buzzerStartTime >= 3000)) {
    buzzerRinging = false;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Buzzer OFF sau 3 giay.");
  }

  // B. ĐỌC DỮ LIỆU GPS NEO-6M
  while (neogps.available() > 0) {
    gps.encode(neogps.read());
  }
  if (gps.location.isUpdated() && gps.location.isValid()) {
    currentLat = gps.location.lat();
    currentLon = gps.location.lng();
  }

  // C. GỬI DỮ LIỆU LÊN UBIDOTS
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 5000) { 
    if (enableAir) client.add("air-quality", airValue);
    if (enableTemp && tempC != -127.00) client.add("temperature", tempC); 
    if (enableGPS && currentLat != 0.0 && currentLon != 0.0) {
      char context[60];
      sprintf(context, "\"lat\":%.6f, \"lng\":%.6f", currentLat, currentLon);
      client.add("location", 1, context); 
    }
    client.publish(DEVICE_LABEL);
    lastSend = millis();
    Serial.println("Da day du lieu len Ubidots!");
  }

  // D. HIỂN THỊ LÊN MÀN HÌNH OLED
  display.clearDisplay();
  
  display.setCursor(0, 0);
  display.print("Air: "); 
  if (enableAir) display.print(airValue); 
  else display.print("OFF"); 

  display.setCursor(0, 15);
  display.print("Temp: ");
  if (!enableTemp) display.print("OFF");
  else if (tempC != -127.00) { display.print(tempC, 1); display.print(" C"); }
  else display.print("ERR");

  display.setCursor(0, 30);
  if (enableGPS) {
    display.print("Lat:"); display.println(currentLat, 6);
    display.print("Lon:"); display.println(currentLon, 6);
  } else {
    display.print("GPS:"); display.println("OFF");
    display.println(); 
  }

  display.setCursor(0, 55);
  
  struct tm timeinfo;
  char timeStr[20];
  if (!getLocalTime(&timeinfo)) {
    sprintf(timeStr, "Time: Syncing..."); 
  } else {
    sprintf(timeStr, "Time: %02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }
  display.print(timeStr);

  display.display();
  
  client.loop(); 
}