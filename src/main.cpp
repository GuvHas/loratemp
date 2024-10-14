#include <LoRa.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <stdio.h>

#include "SSD1306.h"
#include "rom/ets_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include <SimpleDHT.h>

#define SCK  5 // GPIO5  -- SX1278's SCK
#define MISO 19 // GPIO19 -- SX1278's MISnO
#define MOSI 27 // GPIO27 -- SX1278's MOSI
#define SS   18 // GPIO18 -- SX1278's CS
#define RST  23 // GPIO14 -- SX1278's RESET
#define DI0  26 // GPIO26 -- SX1278's IRQ(Interrupt Request)
#define BAND 868E6

unsigned int counter = 0;

//dht pin on ttgo board
byte pinDHT22 = 13;

float temperature = 0;
float humidity = 0;
SimpleDHT22 dht22;

SSD1306 display(0x3c, 21, 22);
String rssi = "RSSI --";
String packSize = "--";
String packet;

float dht_temp(){
  dht22.read2(pinDHT22, &temperature, &humidity, NULL);
  float temperature_c = temperature;
  return temperature_c;
}
float dht_hum(){
  dht22.read2(pinDHT22, &temperature, &humidity, NULL);
  float hum = humidity;
  return hum;
}


void setup() {
  //pinMode(16,OUTPUT);
  pinMode(25,OUTPUT);

  //digitalWrite(16, LOW); // set GPIO16 low to reset OLED
  //delay(50);
  //digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high

  Serial.begin(115200);
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

  delay(1500);
}



void loop() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);

  display.drawString(0, 0, "Sending packet: ");
  display.drawString(90, 0, String(counter));

  //String NodeId = WiFi.macAddress();
  //float temp = intTemperatureRead();
  String NodeId = "GarageTemp";
  float temp = dht_temp();
  float hum = dht_hum();
  
  // send packet
  LoRa.beginPacket();
  // Build json string to send
  String msg = "{\"name\":\"ESP32TEMP\",\"id\":\"" + NodeId + "\",\"tempc\":" + String(temp) + ", \"hum:\":" + String(hum) +"}";
  //String msg = "{\"topic\":\"home/DHTtoMQTT/\",\"ESP32TEMP\",\"id\":\"" + NodeId + "\",\"tempc\":" + String(temp) + ", \"hum:\":" + String(hum) +", \"packet:\","+ String(counter) +"}";
  // Send json string
  LoRa.print(msg);
  LoRa.endPacket();
  
  Serial.println(msg);
  
  display.drawString(0, 15, String(NodeId));
  display.drawString(0, 30, "tempc: " + String(temp) + " C");
  display.drawString(0, 45, "hum: " + String(hum) + " %");
  display.display();
  
  delay(5000);

  counter++;

  digitalWrite(25, HIGH); // turn the LED on (HIGH is the voltage level)
  //Serial.print("HIGH");
  delay(1000); // wait for a second
  digitalWrite(25, LOW); // turn the LED off by making the voltage LOW
  //Serial.print("LOW");
  //delay(60000); // wait for 60 seconds
}