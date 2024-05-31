/*
  TODO : 1) Setup state diagram/machine
         2) Determine how the display will be updated
            a) decide if display behavoiur will change on power status
         3) Setup the following required modules - 
            a) Time updater (RTC ds1307) - DONE
                i) Setup time on - (first boot, connection to network, repeated interval when connected to network)
                ii) worry if leap years, minutes and second will be managed locally or by NTP - (done by NTP)
            b) Temp and pressure updater (bmp280) - DONE-ish
                i) Altitude meter? - Done
            c) Temp and humidity updater (aht20) - DONE
            d) Setup button for input - DONE
                i) Set state via interrupt method into a variable
            e) Setup method to retrieve weather from OpenWeatherMap
                i) Decide if both current and forecast is required?
            f) Find some way to log local temp, humidity and pressure
                i) store it in flash or external eprom?
            g) Determine whether to offload data to some external site
                i) if doing this.. decide where
                ii) decide frequency of update
            h) manage wifi
                i) add multiple AP if possible
                ii) change state according to wifi connection
                iii) use wifi library in future...
            i) determine whether running on battery(3.3v) or main(5v)
                i) update states as necessary
                ii) Show battery status if possible
            j) Spotify Player info (overkill)
                i) Determine polling rate
*/
/////////////////////////////////////////////////////
//
//                DECLARATIONS
//
/////////////////////////////////////////////////////
#define FORECAST_RANGE                6
/////////////////////////////////////////////////////
//
//                INCLUDED LIBRARIES
//
/////////////////////////////////////////////////////

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <WiFiClientSecureBearSSL.h>
#include <FS.h>
#include <base64.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <ESP8266Ping.h>
#include <ArduinoJson.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h> 
#include <NTPClient.h>
#include "RTClib.h"
#include "OneButton.h"
#include <TJpg_Decoder.h> 
#include "TFT_eSPI.h"
#include <Preferences.h>

#include "user_constants.hpp"
//////////////////////////////////////////////////////
//
//              GLOBAL VARIABLES
//
//////////////////////////////////////////////////////

//pins
const uint8_t tftPow = 16,//3
              tftRST = 2, 
              tftCS = 0,
              tftDC = 15,
              tftCLK = 14,
              tftMOSI = 13,
              btnInput = 12,
              extIntrpt = 3;

bool  isTimeSetFromNTP = false,
      refreshDisplay = true,
      refreshSensors = true,
      refreshTime = true,
      startInput = false,
      isInputOngoing = false,
      sec10over = false,
      min1over = false,
      min5over = false,
      internetAvailable = false;

unsigned long lastInternetRefresh = 0,
      checkInternetInterval = 60000;

//update timeframes in multiples of 500ms
const uint16_t displayUpdatet = 500/500,
         sensorsUpdatet = 2000/500,
         timeUpdatet = 500/500,
         sec10timer = 10000/500,
         min1timer = 60000/500,
         min5timer = 300000/500;

uint8_t currDisplayFace = 1,
        prevDisplayFace = 1,
        tftBrightness = 30,
        rotation = 0;

//count no. of external interrupts
volatile uint16_t ticks = 0;

unsigned long pressStartTime,
              inputStartTime,
              inputWaitTime = 2000;

OneButton button;

RTC_DS1307 rtc;
DateTime now;//Time object, represents latest updated time from rtc

Adafruit_BMP280 bmp; // I2C
float tempBMP, pressureBMP, altitudeBMP;

Adafruit_AHTX0 aht;
float humidityAHT, tempAHT;

//api results
float currentTempAPI=0.0f, currentPressAPI=0.0f, currentFeelsLikeAPI=0.0f, currentHumidityAPI=0.0f, currentMaxTempAPI = 0.0f, currentMinTempAPI = 0.0f,
  forecastHourTempAPI[FORECAST_RANGE], forecastHourPressAPI[FORECAST_RANGE], forecastHourHumidityAPI[FORECAST_RANGE], forecastHourRainAPI[FORECAST_RANGE], forecastHourPopAPI[FORECAST_RANGE],
  tomorrowMaxTempAPI = 0.0f, tomorrowMinTempAPI = 0.0f, tomorrowFeelsLikeAPI = 0.0f, tomorrowHumidityAPI = 0.0f, tomorrowRainAPI = 0.0f;

char currentDescAPI[33], currentSummaryAPI[100],
  forecastHourDescAPI[FORECAST_RANGE][33],
  tomorrowSummaryAPI[100];

// The parameters are  RST pin, BUS number, CS pin, DC pin, FREQ (0 means default), CLK pin, MOSI pin
//DisplayST7735_128x160x16_SPI displayTFT(tftRST,{-1, tftCS, tftDC, 0, tftCLK, tftMOSI}); //max freq tested 6000000
TFT_eSPI tft = TFT_eSPI();

const char daysOfTheWeekFull[7][10] = {"Sunday   ", "Monday   ", "Tuesday  ", "Wednesday", "Thursday ", "Friday   ", "Saturday "};
const char daysOfTheWeekShort[7][4] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

//////////////////////////////////////////////////////
//
//                UTILITY FUNCTIONS
//
//////////////////////////////////////////////////////

IRAM_ATTR void checkTicks(){
  if(ticks % displayUpdatet == 0)
    refreshDisplay = true;
  
  if(ticks % sensorsUpdatet == 0)
    refreshSensors = true;

  if(ticks % timeUpdatet == 0)
    refreshTime = true;

  if(!sec10over && ticks % sec10timer == 0)
    sec10over = true;
  
  if(!min1over && ticks % min1timer == 0)
    min1over = true;

  if(!min5over && ticks % min5timer == 0)
    min5over = true;

  if(++ticks >=600)
    ticks = 0;
}

IRAM_ATTR void checkClicks(){
  startInput = true;
  button.tick();
}

void refreshTimeFromRTC(){
  now = rtc.now();
}

void setRTCfromNTP(){ //set time to rtc from ntp, using unix timestamp, begins the timeclient to update, stops the timeclient after update is done
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, "time.google.com", 19800);//offset = +5:30 hrs = 5.5 * 3600 sec
  
  timeClient.begin();
  timeClient.update();
  DateTime now1 = DateTime(timeClient.getEpochTime());
  rtc.adjust(DateTime(timeClient.getEpochTime()));
  timeClient.end();
  refreshTimeFromRTC();
  
  Serial.print(now1.year(), DEC);
  Serial.print('/');
  Serial.print(now1.month(), DEC);
  Serial.print('/');
  Serial.print(now1.day(), DEC);
  Serial.print(" (");
  Serial.print(daysOfTheWeekFull[now1.dayOfTheWeek()]);
  Serial.print(") ");
  Serial.print(now1.hour(), DEC);
  Serial.print(':');
  Serial.print(now1.minute(), DEC);
  Serial.print(':');
  Serial.print(now1.second(), DEC);
  Serial.println();
}

bool connectToWifi(bool waitForTimeout = true, unsigned long timeout = 500L){
  //timeout of 0(zero) means wait forever
  //waitForTimeout being false means donot wait, just begin connection and proceed

  WiFi.begin(String(ssid), String(password));
  unsigned long lastTry = millis();
  while((timeout == 0 || millis() - lastTry > timeout) && waitForTimeout && WiFi.status() != WL_CONNECTED){
    delay(100);
  }

  return (WiFi.status() != WL_CONNECTED);
}

void refreshAHT(){
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity,&temp);
  humidityAHT = humidity.relative_humidity;
  tempAHT = temp.temperature;
}

void refreshBMP(){
  tempBMP = bmp.readTemperature();
  pressureBMP = bmp.readPressure()/100;
  altitudeBMP = bmp.readAltitude(1013.25);//TODO : make it constant
}

void printTime(){
  //char timeStr[6] = "00:00";
  char hour[3] = "00", min[3] = "00",
    colon = (now.second() & 1)? ':' : ' ' ;
  hour[0] = '0' + now.hour() / 10;
  hour[1] = '0' + now.hour() % 10;
  min[0] = '0' + now.minute() / 10;
  min[1] = '0' + now.minute() % 10;
  tft.print(hour);
  tft.print(colon);
  tft.setCursor(70,0);
  tft.print(min);
}

void printDate(){
  char dateStr[9] = "00/00/00";
  dateStr[0] = '0' + now.day() / 10;
  dateStr[1] = '0' + now.day() % 10;
  dateStr[3] = '0' + now.month() / 10;
  dateStr[4] = '0' + now.month() % 10;
  dateStr[6] = '0' + (now.year() % 100) / 10;
  dateStr[7] = '0' + now.year() % 10;
  tft.print(dateStr);
}

void printDay(){
  tft.print(daysOfTheWeekFull[now.dayOfTheWeek()]);
}

void printTemp(bool sensor = false){
  //sensor = false means bmp, true means AHT
  char tempStr[6];
  if(!sensor)
    dtostrf(tempBMP, 4, 2, tempStr);
  else
    dtostrf(tempAHT, 4, 2, tempStr);

  tft.print(tempStr);
}

void printPressure(){
  char pressureStr[8];
  dtostrf(pressureBMP, 6, 2, pressureStr);
  tft.print(pressureStr);
}

void printHumidity(){
  char humidityStr[6];
  dtostrf(humidityAHT, 4, 2, humidityStr);
  tft.print(humidityStr);
}

// this function will be called when the button was pressed 1 time only.
void singleClick() {
  inputStartTime = millis();
  Serial.println("singleClick() detected.");
} // singleClick

// this function will be called when the button was pressed 2 times in a short timeframe.
void doubleClick() {
  inputStartTime = millis();
  Serial.println("doubleClick() detected.");
} // doubleClick

// this function will be called when the button was pressed multiple times in a short timeframe.
void multiClick() {
  inputStartTime = millis();
  int n = button.getNumberClicks();
  if (n == 3) {
    Serial.println("tripleClick detected.");
  } else if (n == 4) {
    Serial.println("quadrupleClick detected.");
  } else {
    Serial.print("multiClick(");
    Serial.print(n);
    Serial.println(") detected.");
  }

} // multiClick

// this function will be called when the button was held down for 1 second or more.
void pressStart() {
  inputStartTime = millis();
  Serial.println("pressStart()");
  pressStartTime = millis() - 1000; // as set in setPressMs()
} // pressStart()

// this function will be called when the button was released after a long hold.
void pressStop() {
  inputStartTime = millis();
  Serial.print("pressStop(");
  Serial.print(millis() - pressStartTime);
  Serial.println(") detected.");
} // pressStop()

void duringLongPress(){
  Serial.println("Long Press ongoing");
}

bool sendDataToRDS(float tbmp, float pbmp,float taht, float haht){
  //unsigned long times = millis();
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;

  //String completeRequest = rdsUrl+"tempBMP="+String(tbmp,2)+"&pressBMP="+String(pbmp,2)+"&tempAHT="+String(taht,2)+"&humAHT="+String(haht,2);
  //Serial.println(pbmp);
  char completeRequest[130];
  sprintf(completeRequest, rdsUrl, tbmp, pbmp, taht, haht);

  //https.begin(*client, completeRequest.c_str());
  https.begin(*client, completeRequest);
  https.addHeader("X-Api-Key", String(rdsApiKey));
  int responseCode = https.POST("");

  //Serial.printf("Response : %d\nMessage : %s\nTime taken in ms : %d",responseCode,https.getString().c_str(),millis()-times);
  return (responseCode == 200);
}
/*
bool getApiWeather(){
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;
  int httpCode = -1;

  String completeRequest = openWeatherUrl + "2.5/weather?appid=" + openWeatherApiKey + "&lat=22.5064&lon=88.2999&units=metric";

  //Initializing an HTTPS communication using the secure client
  //Serial.print("[HTTPS] begin...\n");
  if (https.begin(*client, completeRequest.c_str())) {  // HTTPS
    //Serial.print("[HTTPS] GET...\n");
    // start connection and send HTTP header
    httpCode = https.GET();
    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      //Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
      if (httpCode == HTTP_CODE_OK) {

        JsonDocument doc;
        deserializeJson(doc, https.getStream());
        
        strcpy(currentDescAPI, doc["weather"][0]["description"]);
        currentTempAPI = doc["main"]["temp"];
        currentFeelsLikeAPI = doc["main"]["feels_like"];
        currentPressAPI = doc["main"]["pressure"];
        currentHumidityAPI = doc["main"]["humidity"];
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  return (httpCode == 200);
}
*/
bool getApiv3() {
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;
  int httpCode = -1;

  //String completeRequest = openWeatherUrl + "3.0/onecall?appid=" + openWeatherApiKey + "&lat=22.5064&lon=88.2999&units=metric&exclude=minutely";
  //Serial.println(ESP.getFreeHeap(),DEC);
  char completeRequest [145];
  sprintf(completeRequest, openWeatherUrl, openWeatherApiKey, F("22.5064"), F("88.2999"));
  //if (https.begin(*client, completeRequest.c_str())) {  
  if (https.begin(*client, completeRequest)) {  
    Serial.print("[HTTPS] GET...\n");
    httpCode = https.GET();
    // httpCode will be negative on error
    if (httpCode > 0) {
      Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
      if (httpCode == HTTP_CODE_OK) {
        //filters
        JsonDocument doc, filter;
        JsonObject filter_current = filter["current"].to<JsonObject>();
        filter_current["temp"] = true;
        filter_current["feels_like"] = true;
        filter_current["pressure"] = true;
        filter_current["humidity"] = true;
        filter_current["weather"][0]["description"] = true;

        JsonObject filter_hourly_0 = filter["hourly"].add<JsonObject>();
        filter_hourly_0["temp"] = true;
        //filter_hourly_0["feels_like"] = true;
        filter_hourly_0["pressure"] = true;
        filter_hourly_0["humidity"] = true;
        filter_hourly_0["pop"] = true;
        filter_hourly_0["rain"]["1h"] = true;
        filter_hourly_0["weather"][0]["description"] = true;

        JsonObject filter_daily_0 = filter["daily"].add<JsonObject>();
        filter_daily_0["summary"] = true;

        JsonObject filter_daily_0_temp = filter_daily_0["temp"].to<JsonObject>();
        filter_daily_0_temp["min"] = true;
        filter_daily_0_temp["max"] = true;
        filter_daily_0["feels_like"]["day"] = true;
        filter_daily_0["humidity"] = true;
        //filter_daily_0["weather"][0]["description"] = true;
        filter_daily_0["rain"] = true;
        
        DeserializationError error = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
        //Serial.println(ESP.getFreeHeap(),DEC);
        if (error) {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
          return false;
        }
        //deserializeJson(doc, https.getString());
        
        // const char *current_desc = doc["current"]["weather"][0]["description"],
        //   *hourly_0_desc = doc["hourly"][0]["weather"][0]["description"],
        //   *hourly_1_desc = doc["hourly"][1]["weather"][0]["description"],
        //   *hourly_2_desc = doc["hourly"][2]["weather"][0]["description"],
        //   *daily_0_desc = doc["daily"][0]["weather"][0]["description"],
        //   *daily_1_desc = doc["daily"][1]["weather"][0]["description"];

        // Serial.printf("Current Weather Description : %s\n",current_desc);
        // Serial.printf("Hour 0 Weather Description : %s\n",hourly_0_desc);
        // Serial.printf("Hour 1 Weather Description : %s\n",hourly_1_desc);
        // Serial.printf("Daily 0 Weather Description : %s\n",daily_0_desc);
        // Serial.printf("Daily 1 Weather Description : %s\n",daily_1_desc);

        currentTempAPI = doc["current"]["temp"];
        currentFeelsLikeAPI = doc["current"]["feels_like"];
        currentPressAPI = doc["current"]["pressure"];
        currentHumidityAPI = doc["current"]["humidity"];
        strcpy(currentDescAPI,doc["current"]["weather"][0]["description"]);

        for(int i = 0 ; i < FORECAST_RANGE ; i++){
          forecastHourTempAPI[i] = doc["hourly"][i+1]["temp"];
          forecastHourPressAPI[i] = doc["hourly"][i+1]["pressure"];
          forecastHourHumidityAPI[i] = doc["hourly"][i+1]["humidity"];
          forecastHourPopAPI[i] = doc["hourly"][i+1]["pop"];
          forecastHourRainAPI[i] = doc["hourly"][i+1]["rain"]["1h"] | -1.0f ;
          strcpy(forecastHourDescAPI[i], doc["hourly"][i+1]["weather"][0]["description"]);
        }

        currentMaxTempAPI = doc["daily"][0]["temp"]["max"];
        currentMinTempAPI = doc["daily"][0]["temp"]["min"];
        strcpy(currentSummaryAPI, doc["daily"][0]["summary"]);

        tomorrowMaxTempAPI = doc["daily"][1]["temp"]["max"];
        tomorrowMinTempAPI = doc["daily"][1]["temp"]["min"];
        tomorrowFeelsLikeAPI = doc["daily"][1]["feels_like"]["day"];
        tomorrowHumidityAPI = doc["daily"][1]["humidity"];
        tomorrowRainAPI = doc["daily"][1]["rain"] | 0.0f;
        strcpy(tomorrowSummaryAPI, doc["daily"][1]["summary"]);
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  
  return (httpCode == 200);
}

unsigned long myAbs(long val){
  return (val>0)? val : -val;
}

bool checkInternet(){
  if(internetAvailable && WiFi.isConnected())
    return true;
  else if(WiFi.isConnected() && (myAbs(millis()-lastInternetRefresh) > checkInternetInterval || !rtc.isrunning())){
    lastInternetRefresh = millis();
    return internetAvailable = Ping.ping(String(remote_host).c_str(),1);
  }
  else
    return false;
}

void displayFace1(){
  // uint16_t ypos = 0;
  //tft.fillScreen(TFT_BLACK);
  tft.setCursor(2,0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(6);
  tft.setTextSize(1);

  printTime();
  tft.println();
  tft.setTextFont(1);
  //tft.setCursor(tft.getCursorX(),tft.getCursorY()-10);
  tft.setTextSize(2);
  printDate();
  tft.println();
  printDay();
  tft.println();

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setCursor(tft.getCursorX(),tft.getCursorY()+2);
  tft.print(F("BMP 'C : "));
  printTemp();
  tft.println();
  tft.setCursor(tft.getCursorX(),tft.getCursorY()+1);
  tft.print(F("Press mb : "));
  printPressure();
  tft.println();
  tft.setCursor(tft.getCursorX(),tft.getCursorY()+1);
  tft.print(F("Humid Rh : "));
  printHumidity();
  tft.println();
  tft.setCursor(tft.getCursorX(),tft.getCursorY()+1);
  tft.print(F("AHT 'C : "));
  printTemp(true);
  tft.println();

  char tempStr[17];
  sprintf(tempStr,"%s%.2f","Temp 'C : ",currentTempAPI);
  tft.setCursor(tft.getCursorX(),tft.getCursorY()+1);
  tft.println(tempStr);
  sprintf(tempStr,"%s%.2f","Feels 'C : ",currentFeelsLikeAPI);
  tft.setCursor(tft.getCursorX(),tft.getCursorY()+1);
  tft.println(tempStr);
  sprintf(tempStr,"%s%.2f","Press mb : ",currentPressAPI);
  tft.setCursor(tft.getCursorX(),tft.getCursorY()+1);
  tft.println(tempStr);
  sprintf(tempStr,"%s%.2f","Humid Rh : ", currentHumidityAPI);
  tft.setCursor(tft.getCursorX(),tft.getCursorY()+1);
  tft.println(tempStr);
}

//////////////////////////////////////////////////////
//
//                SETUP
//
//////////////////////////////////////////////////////

void setup(){
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  //connectToWifi(true, 0);//initiate wifi connection
  connectToWifi(false);

  //initialize RTC chip
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }
  
  //initialize AHT sensor
  if (! aht.begin()) {
    Serial.println("Could not find AHT? Check wiring");
    while (1) delay(10);
  }

  //initialize BMP sensor
  if (! bmp.begin(0x76)){
    Serial.println(F("Could not find a valid BMP280 sensor, check wiring or "
                      "try a different address!"));
    while (1) delay(10);
  }

  //for brightness control of tft screen, we will use pwm
  analogWriteRange(40);
  analogWriteFreq(48);
  analogWrite(tftPow,tftBrightness);

  //initiate TFT display

  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);

  //setting up button fuctions
  button = OneButton(btnInput, true);

  button.attachClick(singleClick);
  button.attachDoubleClick(doubleClick);
  button.attachMultiClick(multiClick);
  button.setPressMs(400); // that is the time when LongPressStart is called
  button.attachLongPressStart(pressStart);
  button.attachLongPressStop(pressStop);

  //perhaps use long press instead of double click
  button.attachDuringLongPress(duringLongPress);
  button.setLongPressIntervalMs(1200);

  //attachInterrupt(digitalPinToInterrupt(btnInput), checkClicks, CHANGE);

  //setup external interrupt from ds1307
  pinMode(extIntrpt, INPUT);
  attachInterrupt(digitalPinToInterrupt(extIntrpt), checkTicks, CHANGE);

  if(rtc.readSqwPinMode() != DS1307_SquareWave1HZ)
    rtc.writeSqwPinMode(DS1307_SquareWave1HZ);
  
  
}//setup

//////////////////////////////////////////////////////
//
//                LOOP
//
//////////////////////////////////////////////////////

void loop(){
  if(refreshTime){
    refreshTimeFromRTC();
    refreshTime = false;
  }
  if(refreshSensors){
    refreshBMP();
    refreshAHT();
    refreshSensors = false;
  }
  button.tick();
  if(refreshDisplay){
    switch (currDisplayFace)
    {
    case 1:
      if(currDisplayFace != prevDisplayFace){
        prevDisplayFace = currDisplayFace;
        tft.fillScreen(TFT_BLACK);
      }
      displayFace1();
      break;
    
    case 2:
      break;
    
    case 3:
      break;
    }
    refreshDisplay = false;
  }

  if( !isTimeSetFromNTP && WiFi.status() == WL_CONNECTED) { // TODO: find way to reduce checking rate if rtc is already set
    if(checkInternet()){
      Serial.println(F("Connected to WiFi, updating time from NTP"));
      setRTCfromNTP();
      isTimeSetFromNTP = true;
    }
    sec10over = false;//TODO : find a better/efficient solution
  }

  if(min1over){
    if(checkInternet())
      internetAvailable = sendDataToRDS(tempBMP,pressureBMP,tempAHT,humidityAHT);
    min1over=false;
  }
  button.tick();
  if(min5over){
    //getApiWeather();
    //unsigned long time = millis();
    if(checkInternet()){
      internetAvailable = getApiv3();
      if(internetAvailable)
        min5over=false;
    }
    //Serial.print("\nFinished in ");
    //Serial.println(millis()-time);
  }

  button.tick();
  //delay(20);
  //Serial.println(ticks);
}














