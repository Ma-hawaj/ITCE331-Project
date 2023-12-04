#include <WiFi.h>
#include <WebServer.h>
#include <Arduino.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "DHT.h"
#include "Head.h"
#include <SPIFFS.h>

DHT dht(15,DHT11);

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for SSD1306 display connected using I2C
#define OLED_RESET     -1 // Reset pin
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

WebServer server(80);

TaskHandle_t WifiConfigHandle, serverConfigHandle, Queueread, serverHandle,displayHandle,sensorHandle,outputHandle;

QueueHandle_t temp_queue, trig_queue;

typedef struct temp_data{
  float temp;
}temp_data;



void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial.println("begin");

  pinMode(15, INPUT);
  dht.begin();

  
  pinMode(23, OUTPUT);

  temp_queue = xQueueCreate(1, sizeof(temp_data));
  trig_queue = xQueueCreate(1, sizeof(uint8_t));

  
  if(temp_queue == NULL || trig_queue == NULL){
    Serial.println("Queue could not be created.");
    while(1) delay(1000); // Halt at this point as is not possible to continue
  }

  uint8_t trigger = 30;
  temp_data t;
  t.temp = 30.05;

  xQueueSend(temp_queue, &t, portMAX_DELAY);
  xQueueSend(trig_queue, &trigger, portMAX_DELAY);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
	Serial.println(F("SSD1306 allocation failed"));
	for(;;); // Don't proceed, loop forever
  }

  xTaskCreate(wifi_setup, "WiFi configuration", 10240 , NULL , 3 , &WifiConfigHandle);
  xTaskCreate(server_setup, "Server configuration", 20480 , NULL , 3 , &serverConfigHandle);
  xTaskCreate(server_handle, "Server Handler", 10240 , NULL , 2 , &serverHandle);
  xTaskCreate(Display_print, "Printeing to the OLED", 2048 , NULL , 2 , &displayHandle);
  xTaskCreate(sensor_read, "Reading the sensors", 2048 , NULL , 2 , &sensorHandle);
  xTaskCreate(output_trig, "Triggering output", 1024 , NULL , 2 , &outputHandle);


  Serial.println("setup finished");

  xTaskNotifyGive(WifiConfigHandle);
  xTaskNotifyGive(sensorHandle);
  xTaskNotifyGive(outputHandle);

}



void loop() {
  //Serial.println("I'm Idle");
}


void output_trig(void* pvParameters){

  temp_data temp;
  uint8_t trig;

  if(ulTaskNotifyTake(pdTRUE, portMAX_DELAY)){
    while(1){

      xQueuePeek(temp_queue, &temp, portMAX_DELAY);
      xQueuePeek(trig_queue, &trig, portMAX_DELAY);
      if(temp.temp > trig){
        digitalWrite(23, HIGH);
      }
      else{
        digitalWrite(23, LOW);
      }
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    
  }
}


void wifi_setup(void* pvParameters){

  while(1){

    if(ulTaskNotifyTake(pdTRUE, portMAX_DELAY)){
      Serial.print("Connecting to Wifi ...");
      WiFi.begin(SECRET_SSID, SECRET_PASS);
    
      while(WiFi.status() != WL_CONNECTED){
        vTaskDelay(500 / portTICK_PERIOD_MS);
        Serial.print(".");
      }

      Serial.print("\n My IP address: ");
      Serial.println(WiFi.localIP());

      xTaskNotifyGive(serverConfigHandle);
      xTaskNotifyGive(displayHandle);


      vTaskSuspend(NULL); // suspened the task after connecting to wifi
    }
  }

}

void Display_print(void* pvParameters){
  temp_data temp;
  uint8_t trig;
  if(ulTaskNotifyTake(pdTRUE, portMAX_DELAY)){
  
    while(1){
      xQueuePeek(temp_queue, &temp, portMAX_DELAY);
      xQueuePeek(trig_queue, &trig, portMAX_DELAY);
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(0,8);
      display.print("IP: ");
      display.println(WiFi.localIP());
      display.setCursor(0,24);
      display.println("Current temp: "+String(temp.temp));
      display.setCursor(0,40);
      display.println("Trigger temp: "+String(trig));
      display.display();
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
  
  }
}


void server_setup(void* pvParameters){
  
  while(1){
    if(ulTaskNotifyTake(pdTRUE, portMAX_DELAY)){
      
      server.on("/", handle_root);
      server.on("/update", handle_update);
      server.onNotFound(handle_NotFound);

      server.begin();

      xTaskNotifyGive(serverHandle);
      Serial.println("Server Online");
      vTaskSuspend(NULL); // suspened the task after connecting to wifi        
    }
  } 
}


void server_handle(void* pvParameters){
  if(ulTaskNotifyTake(pdTRUE, portMAX_DELAY)){
    while(1){
      while(WiFi.status() != WL_CONNECTED){
        vTaskResume(WifiConfigHandle);
        xTaskNotifyGive(WifiConfigHandle);
        taskYIELD();
      }
      server.handleClient();
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }
  }
}


void sensor_read(void* pvParameters){

  temp_data temp;
  if(ulTaskNotifyTake(pdTRUE, portMAX_DELAY)){
  while(1){
    vTaskDelay(250/portTICK_PERIOD_MS);
  
    temp.temp = dht.readTemperature();   //Reading tempraeture from the sensor
    // Serial.println("seneor: "+ String(temp.temp));

    xQueueOverwrite(temp_queue , &temp);  // write data to the queue

    
  }}
  
}

void handle_root(){
  server.send(200, "text/html", my_html());
}

void handle_NotFound(){
  server.send(404, "text/plain", "Not found");
}

void handle_update(){

  uint8_t trig = server.arg("trig").toInt();
  Serial.println("Trigger Tempreature = "+String(trig));
  ///   Send to queue the trig data
  xQueueOverwrite(trig_queue, &trig);

  server.send(200, "text/html", my_html());

}


String my_html(){
  temp_data temp;
  uint8_t trig;

  xQueuePeek(temp_queue, &temp, portMAX_DELAY);
  xQueuePeek(trig_queue, &trig, portMAX_DELAY);
  Serial.println("From queue trig= "+String(trig));
  Serial.println("From queue temp= "+String(temp.temp));
  
  String html = "";
  if (SPIFFS.begin()) {
    File file = SPIFFS.open("/index.html", "r");
    if (file) {
      html = file.readString();
      Serial.println("file readed");
      file.close();
    }
    SPIFFS.end();
  }

  html.replace("[temp]", String(temp.temp));
  html.replace("[trig]", String(trig));
  
  return html;
}





