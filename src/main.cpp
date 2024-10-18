#include <LoRa.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <stdio.h>
#include "SSD1306.h"
#include "rom/ets_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include <DHT.h>
#include <WiFiManager.h>

#define SCK  5 // GPIO5  -- SX1278's SCK
#define MISO 19 // GPIO19 -- SX1278's MISnO
#define MOSI 27 // GPIO27 -- SX1278's MOSI
#define SS   18 // GPIO18 -- SX1278's CS
#define RST  23 // GPIO14 -- SX1278's RESET
#define DI0  26 // GPIO26 -- SX1278's IRQ(Interrupt Request)
#define BAND 868E6 // LoRa band used
#define DHTTYPE DHT22 //Sensor used
#define pinDHT22 13 //DHTPin used on board

unsigned int counter = 0;

float temp;
float hum;

DHT dhtsensor(pinDHT22, DHTTYPE);

SSD1306 display(0x3c, 21, 22);
WiFiManager wifiManager;

//subroutines
void get_temperature_and_humidity() {  
   temp = dhtsensor.readTemperature();
   hum = dhtsensor.readHumidity();         
   if (isnan(temp) || isnan(hum)) {              // Check if any reads failed and exit early (to try again).
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
   }  
}

void blink_led() {
  digitalWrite(25, HIGH); // turn the green LED on (HIGH is the voltage level)
  delay(1000); // wait for a second  
  digitalWrite(25, LOW); // turn the green LED off by making the voltage LOW 
  return;
}


void setup() {  
  pinMode(25,OUTPUT); //Output for green LED
  pinMode(13,INPUT); //Input for DHT-sensor
  dhtsensor.begin();
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  while (!Serial)
    ;
  Serial.println();
  Serial.println("LoRa Sender Test");
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DI0);
  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1)
      ;
  }

  Serial.println("init ok");
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);  
  //wifiManager.autoConnect("ESPTemp_AP");
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConfigPortalTimeout(60);
  if(wifiManager.autoConnect("ESPTemp_AP")) {
    Serial.println("Connected");
  }
  else {
        Serial.println("Configportal running.");
  }  
  delay(1500);
}


void loop() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);

  display.drawString(0, 0, "Sending packet: ");
  display.drawString(90, 0, String(counter));
  
  

  get_temperature_and_humidity();

  String NodeId = "GarageTemp"; //Friendly name for the device
  //String IP = WiFi.localIP();
  LoRa.beginPacket(); // send packet
  String msg = "{\"name\":\"DHT_LoRa_Sensor\",\"id\":\"" + NodeId + "\",\"temperature\":" + String(temp) + ", \"humidity\":" + String(hum) +"}"; // Build json string to send
  LoRa.print(msg); // Send json string
  LoRa.endPacket();
  blink_led(); //blink led after transmission
  Serial.println(msg); //print the message to console
  
  display.drawString(0, 12, String(NodeId));
  display.drawString(0, 24, "temp: " + String(temp) + " C");
  display.drawString(0, 36, "hum: " + String(hum) + " %");
  display.drawString(0, 48, "IP: " + String(WiFi.localIP().toString()));
  display.display();
  
  delay(5000); //wait for 5 second

  counter++;
  wifiManager.process(); 
}