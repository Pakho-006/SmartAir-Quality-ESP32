#include <FS.h>
#include <ESPAsyncWebServer.h>     //Local WebServer used to serve the configuration portal
#include <ESPAsyncWiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <pgmspace.h>
#include <AWS_IOT.h>
#include "SCD30.h"
#include <Wire.h>
//#include "secrets.h"
#include <sps30.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>

#define BUTTON_PIN 23
#define WDT_TIMEOUT 6
int led_state = LOW;    // the current state of LED
int button_state;       // the current state of button
int last_button_state;  // the previous state of button
AsyncWebServer server(80);
DNSServer dns;

char buffer[200];
AWS_IOT hornbill;
char HOST_ADDRESS[]="a2q3551lfc7yvg-ats.iot.eu-west-1.amazonaws.com";
char CLIENT_ID[]= "sdk-scd3001";
char TOPIC_NAME[]= "sdk/test/scd3001";

int status = WL_IDLE_STATUS;
int tick=0,msgCount=0,msgReceived = 0;
char payload[512];
char rcvdPayload[512];

int countnum=0;
int failed=0;

void reset(){
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  while(1){
    delay(500);
  }
}

void messageHandler(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);

}

void mySubCallBackHandler (char *topicName, int payloadLen, char *payLoad)
{
    strncpy(rcvdPayload,payLoad,payloadLen);
    rcvdPayload[payloadLen] = 0;
    msgReceived = 1;
}

String getValue(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void setup()
{
  int16_t ret;
  uint8_t auto_clean_days = 4;
  uint32_t auto_clean;
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  Serial.begin(115200);  
  sensirion_i2c_init();
  while (sps30_probe() != 0) {
    Serial.print("SPS sensor probing failed\n");
    delay(500);
    sensirion_i2c_init();
  }
  #ifndef PLOTTER_FORMAT
  Serial.print("SPS sensor probing successful\n");
#endif /* PLOTTER_FORMAT */

  ret = sps30_set_fan_auto_cleaning_interval_days(auto_clean_days);
  if (ret) {
    Serial.print("error setting the auto-clean interval: ");
    Serial.println(ret);
  }

  ret = sps30_start_measurement();
  if (ret < 0) {
    Serial.print("error starting measurement\n");
  }

#ifndef PLOTTER_FORMAT
  Serial.print("measurements started\n");
#endif /* PLOTTER_FORMAT */

  scd30.initialize();
  scd30.setAutoSelfCalibration(1);

#ifdef SPS30_LIMITED_I2C_BUFFER_SIZE
  Serial.print("Your Arduino hardware has a limitation that only\n");
  Serial.print("  allows reading the mass concentrations. For more\n");
  Serial.print("  information, please check\n");
  Serial.print("  https://github.com/Sensirion/arduino-sps#esp8266-partial-legacy-support\n");
  Serial.print("\n");
  delay(1000);

#endif

  button_state = digitalRead(BUTTON_PIN);


  AsyncWiFiManager wifiManager(&server,&dns);
  wifiManager.setBreakAfterConfig(true);

  int renum = 0;
  
  
//  wifiManager.resetSettings();

  if (!wifiManager.autoConnect("IOT-AP01", "aaaaaaaa")) {
      Serial.println("failed to connect, we should reset as see if it connects");
      delay(500);
//      hard_restart();
      delay(500);
    }

  if (WiFi.status() == WL_CONNECTED)
   {
      Serial.println("Connected to wifi");
   }
   while(1){
    last_button_state = button_state;      // save the last state
    button_state = digitalRead(BUTTON_PIN); // read new state
    if (last_button_state == HIGH && button_state == LOW) {
      Serial.println("The button is pressed");
      wifiManager.resetSettings();
      delay(1000);
      reset();
    }
    renum++;
    Serial.println("Waiting for reset...");
    if(renum>=30){
      break;
    }
    delay(200);
  }
    
  delay(1000);
  if(hornbill.connect(HOST_ADDRESS,CLIENT_ID)== 0)
    {
        Serial.println("Connected to AWS");
        delay(1000);

        if(0==hornbill.subscribe(TOPIC_NAME,mySubCallBackHandler))
        {
            Serial.println("Subscribe Successfull");
        }
        else
        {
            Serial.println("Subscribe Failed, Check the Thing Name and Certificates");
            while(1);
        }
    }
    else
    {
        Serial.println("AWS connection failed, Check the HOST Address");
        delay(1000);
        reset();
    }

    delay(2000);
  while (!Serial);             // Leonardo: wait for serial monitor
  Serial.println("\nI2C Scanner");

  
}
 
 
void loop()
{   
    struct sps30_measurement m;
    char serial[SPS30_MAX_SERIAL_LEN];
    uint16_t data_ready;
    int16_t ret;
    float result[3] = {0};
    if (WiFi.status() == WL_CONNECTED)
    {
          Serial.println("Connected to wifi");
    }else{
      reset();
    }
    
    delay(1000);
    
    HTTPClient http1;
    http1.begin("https://zmca4jf7ck.execute-api.eu-west-1.amazonaws.com/devlop/iaqi");
    int httpCode = http1.GET();  
    if (scd30.isAvailable()) {
      if(httpCode>0){
          scd30.getCarbonDioxideConcentration(result);
          do {
            ret = sps30_read_data_ready(&data_ready);
            if (ret < 0) {
              Serial.print("error reading data-ready flag: ");
              Serial.println(ret);
            } else if (!data_ready)
              Serial.print("data not ready, no new measurement available\n");
            else
              break;
            delay(100); /* retry in 100ms */
          } while (1);
          
          ret = sps30_read_measurement(&m);
          
          String payload1 = http1.getString();
          delay(1000);
          char json[700];
          
          payload1.toCharArray(json, 800);
          StaticJsonDocument<700> doc;
          deserializeJson(doc, json);
          int panduan;
          String httpclock = doc["clock"];
          String xval = getValue(httpclock, ':', 0);
          int xvalue = xval.toInt();
          String yval = getValue(httpclock, ':', 1);
          int yvalue = yval.toInt();
          if( xvalue>=8 &&xvalue<=18){
            panduan = 1;
          }else{
            panduan = 0;
          }

          if(xvalue ==8 && yvalue == 10){
            reset();
          }else if(xvalue ==18 && yvalue == 10){
            reset();
          }else if(xvalue ==17 && yvalue == 40){
            reset();
          }
          String httptime = doc["date"];
          float outsidetemp = doc["temperature"];
          String updateTime = doc["min"];

          Serial.print(httpclock);
          Serial.println();
          
          Serial.print("Carbon Dioxide Concentration is: ");
          int pm25 =m.mc_2p5+9;
          float temperature = result[1] -9;
          float humidity = result[2]+17;
          int co2 = result[0];
          outsidetemp = round(outsidetemp * 10) / 10;
          temperature = round(temperature * 10) / 10;
          humidity = round(humidity * 10) / 10;
          Serial.print(result[0]);
          Serial.println(" ppm");
          Serial.println(" ");
          Serial.print("Temperature = ");
          Serial.print(result[1]);
          Serial.println(" ℃");
          Serial.println(" ");
          Serial.print("Humidity = ");
          Serial.print(result[2]);
          Serial.println(" %");
          Serial.println(" ");
          Serial.println(" ");
          Serial.println(" ");
          delay(500);
          StaticJsonDocument<200> testDocument;
          testDocument["Temperature"] = temperature;
          testDocument["Outsidetemp"] = outsidetemp;
          testDocument["Humidity"] = humidity;
          testDocument["PM25"] = pm25;
          testDocument["CO2"] = co2;
          testDocument["date"] =httptime;
          testDocument["datetime"] =httpclock;
          testDocument["updateTime"] =updateTime;
          delay(500);
          serializeJson(testDocument, buffer);
          Serial.println(buffer);
          delay(500);
          if(temperature<0){
            tick=tick-2;
          }
          if(msgReceived == 1)
          {
              msgReceived = 0;
              Serial.print("Received Message:");
              Serial.println(rcvdPayload);
          }
          if(tick >= 40 && panduan == 1 )   // publish to topic every 5seconds
          {
              tick=0;
              sprintf(payload,buffer);
              if(hornbill.publish(TOPIC_NAME,payload) == 0)
              {        
                  Serial.print("Publish Message:");
                  Serial.println(payload);
              }
              else
              {
                  Serial.println("Publish failed and try publish again...");
                  while(1){
                    if(hornbill.publish(TOPIC_NAME,payload) == 0)
                    {        
                        Serial.print("Publish Message:");
                        Serial.println(payload);
                        failed=0;
                        break;
                    }
                    delay(500);
                    failed++;
                    if(failed>4){
                      reset();
                    }
                    
                  }
              }
          }  
          vTaskDelay(1000 / portTICK_RATE_MS);
          tick++;
          delay(30000);
          Serial.println(tick);
       
        }
        
      }
      reset();
}
