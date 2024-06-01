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
#include "Web_Fetch.h"
#include "index.h"
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
      sec5over = false,
      sec10over = false,
      min1over = false,
      min5over = true,
      internetAvailable = false;

unsigned long lastInternetRefresh = 0,
      checkInternetInterval = 60000;

//update timeframes in multiples of 500ms
const uint16_t displayUpdatet = 500/500,
         sensorsUpdatet = 2000/500,
         timeUpdatet = 500/500,
         sec5timer = 5000/500,
         sec10timer = 10000/500,
         min1timer = 60000/500,
         min5timer = 300000/500;

uint8_t currDisplayFace = 0,
        prevDisplayFace = -1,
        tftBrightness = 39,
        rotation = 0,
        prevMinute = 100;

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
  tomorrowMaxTempAPI = 0.0f, tomorrowMinTempAPI = 0.0f, tomorrowFeelsLikeAPI = 0.0f, tomorrowHumidityAPI = 0.0f, tomorrowRainAPI = 0.0f, tomorrowPopAPI = 0.0f;

char currentDescAPI[33], currentSummaryAPI[100], currentIconAPI[4],
  forecastHourDescAPI[FORECAST_RANGE][33], forecastHourIconAPI[FORECAST_RANGE][4],
  tomorrowSummaryAPI[100];

// The parameters are  RST pin, BUS number, CS pin, DC pin, FREQ (0 means default), CLK pin, MOSI pin
//DisplayST7735_128x160x16_SPI displayTFT(tftRST,{-1, tftCS, tftDC, 0, tftCLK, tftMOSI}); //max freq tested 6000000
TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

ESP8266WebServer server(80);
//SpotConn spotifyConnection;

bool serverOn = true;

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
  
  if(!refreshSensors && ticks % sensorsUpdatet == 0)
    refreshSensors = true;

  if(ticks % timeUpdatet == 0)
    refreshTime = true;

  if(!sec5over && ticks % sec5timer == 0)
    sec5over = true;

  if(!sec10over && ticks % sec10timer == 0)
    sec10over = true;
  
  if(!min1over && ticks % min1timer == 0)
    min1over = true;

  if(!min5over && ticks % min5timer == 0)
    min5over = true;

  if(++ticks >=600)
    ticks = 0;

  //button.tick();
}

IRAM_ATTR void clickUp(){
  startInput = true;
  button.tick(LOW);
}

IRAM_ATTR void clickDown(){
  startInput = true;
  button.tick(HIGH);
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
  currDisplayFace = (currDisplayFace+1)%2;
  Serial.print("Current Face : ");
  Serial.println(currDisplayFace);
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

//////////////////////////////////////////////////////
//
//                Spotify Functions
//
/////////////////////////////////////////////////////

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap){
  // Stop further decoding as image is running off bottom of screen
  if ( y >= tft.height() ) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  tft.pushImage(x, y, w, h, bitmap);

  // Return 1 to decode next block
  return 1;
}

String getValue(HTTPClient &http, String key) {
  bool found = false, look = false, seek = true;
  int ind = 0;
  String ret_str = "";

  int len = http.getSize();
  char char_buff[1];
  WiFiClient * stream = http.getStreamPtr();
  while (http.connected() && (len > 0 || len == -1)) {
    size_t size = stream->available();
    if (size) {
      int c = stream->readBytes(char_buff, ((size > sizeof(char_buff)) ? sizeof(char_buff) : size));
      if (found) {
        if (seek && char_buff[0] != ':') {
          continue;
        } else if(char_buff[0] != '\n'){
            if(seek && char_buff[0] == ':'){
                seek = false;
                int c = stream->readBytes(char_buff, 1);
            }else{
                ret_str += char_buff[0];
            }
        }else{
            break;
        }
      }
      else if ((!look) && (char_buff[0] == key[0])) {
        look = true;
        ind = 1;
      } else if (look && (char_buff[0] == key[ind])) {
        ind ++;
        if (ind == key.length()) found = true;
      } else if (look && (char_buff[0] != key[ind])) {
        ind = 0;
        look = false;
      }
    }
  }
  if(*(ret_str.end()-1) == ','){
    ret_str = ret_str.substring(0,ret_str.length()-1);
  }
  return ret_str;
}

struct httpResponse{
    int responseCode;
    String responseMessage;
};
struct songDetails{
    int durationMs;
    String album;
    String artist;
    String song;
    String Id;
    bool isLiked;
};

char *parts[10];

void printSplitString(String text,int maxLineSize, int yPos)
{
    int currentWordStart = 0;
    int spacedCounter = 0;
    int spaceIndex = text.indexOf(" ");
    
    while(spaceIndex != -1){
        // Serial.println(ESP.getFreeHeap());
        char *part = parts[spacedCounter]; 
        sprintf(part,text.substring(currentWordStart,spaceIndex).c_str());
        // Serial.println(ESP.getFreeHeap());
        // parts[spacedCounter] = part;
        currentWordStart = spaceIndex;
        spacedCounter++;
        spaceIndex = text.indexOf(" ",spaceIndex+1);
    }
    // Serial.println(ESP.getFreeHeap());
    char *part = parts[spacedCounter]; 
    sprintf(part,text.substring(currentWordStart,text.length()).c_str());
    // Serial.println(ESP.getFreeHeap());
    currentWordStart = spaceIndex;
    size_t counter = 0;
    currentWordStart = 0;
    while(counter <= spacedCounter){
        char printable[maxLineSize];
        char* printablePointer = printable;
        // sprintf in word at counter always
        sprintf(printablePointer,parts[counter]);
        //get length of first word
        int currentLen = 0;
        while(parts[counter][currentLen] != '\0'){
            currentLen++;
            printablePointer++;
        }
        counter++;
        while(counter <= spacedCounter){
            int nextLen = 0;
            while(parts[counter][nextLen] != '\0'){
                nextLen++;
            }
            if(currentLen + nextLen > maxLineSize)
                break;
            sprintf(printablePointer, parts[counter]);
            currentLen += nextLen;
            printablePointer += nextLen;
            counter++;
        }
        String output = String(printable);
        if(output[0] == ' ')
            output = output.substring(1);
        // Serial.println(output);
        //tft.setCursor((int)(tft.width()/2 - tft.textWidth(output) / 2),tft.getCursorY());
        tft.setCursor(6, tft.getCursorY());
        tft.println(output);
        // free(printable);
    }
    // Serial.println(ESP.getFreeHeap());
}

//Create spotify connection class
class SpotConn {
public:
  SpotConn(){
      // client = std::make_unique<BearSSL::WiFiClientSecure>();
      // client->setInsecure();
  }
  // httpResponse makeSpotifyRequest(const char* URI, const char** headers, int numHeaders, const char* RequestBody){
  //     https.begin(*client,URI);
  //     for(;numHeaders>0;numHeaders--,headers += 2){
  //         https.addHeader(*headers,*(headers+1));
  //     }
  //     struct httpResponse res;
  //     res.responseCode = https.POST(RequestBody);
  //     res.responseMessage = https.getString()
  //     https.end();
  //     return res;
  // }

  bool getUserCode(String serverCode) {
      std::unique_ptr<BearSSL::WiFiClientSecure>client(std::make_unique<BearSSL::WiFiClientSecure>());
      client->setInsecure();
      HTTPClient https;

      https.begin(*client,"https://accounts.spotify.com/api/token");
      String auth = "Basic " + base64::encode(String(CLIENT_ID) + ":" + String(CLIENT_SECRET));
      https.addHeader("Authorization",auth);
      https.addHeader("Content-Type","application/x-www-form-urlencoded");
      String requestBody = "grant_type=authorization_code&code="+serverCode+"&redirect_uri="+String(REDIRECT_URI);
      // Send the POST request to the Spotify API
      int httpResponseCode = https.POST(requestBody);
      // Check if the request was successful
      if (httpResponseCode == HTTP_CODE_OK) {
          String response = https.getString();
          //DynamicJsonDocument doc(1024);
          JsonDocument doc;
          deserializeJson(doc, response);
          accessToken = String((const char*)doc["access_token"]);
          refreshToken = String((const char*)doc["refresh_token"]);
          tokenExpireTime = doc["expires_in"];
          tokenStartTime = millis();
          accessTokenSet = true;
          Serial.println(accessToken);
          Serial.println(refreshToken);
          prefs.putString("refreshToken", refreshToken);
      }else{
          Serial.println(https.getString());
      }
      // Disconnect from the Spotify API
      https.end();
      return accessTokenSet;
  }
  bool refreshAuth(){
      std::unique_ptr<BearSSL::WiFiClientSecure>client(std::make_unique<BearSSL::WiFiClientSecure>());
      client->setInsecure();
      HTTPClient https;

      https.begin(*client,"https://accounts.spotify.com/api/token");
      String auth = "Basic " + base64::encode(String(CLIENT_ID) + ":" + String(CLIENT_SECRET));

      https.addHeader("Authorization",auth);
      https.addHeader("Content-Type","application/x-www-form-urlencoded");
      String requestBody = "grant_type=refresh_token&refresh_token="+String(refreshToken);
      // Send the POST request to the Spotify API
      int httpResponseCode = https.POST(requestBody);
      accessTokenSet = false;
      // Check if the request was successful
      if (httpResponseCode == HTTP_CODE_OK) {
          //String response = https.getString();
          //DynamicJsonDocument doc(1024);
          JsonDocument doc;

          deserializeJson(doc, https.getStream());
          //accessToken = String((const char*)doc["access_token"]);
          accessToken = doc["access_token"].as<String>();
          // refreshToken = doc["refresh_token"];
          tokenExpireTime = doc["expires_in"];
          tokenStartTime = millis();
          accessTokenSet = true;
          // Serial.println(accessToken);
          // Serial.println(refreshToken);
          prefs.putString("refreshToken", refreshToken);
      }else{
          Serial.println("Refresh Failed");
          Serial.println(https.getString());
      }
      // Disconnect from the Spotify API
      https.end();
      return accessTokenSet;
  }
  bool getTrackInfo(){
      std::unique_ptr<BearSSL::WiFiClientSecure>client(std::make_unique<BearSSL::WiFiClientSecure>());
      client->setInsecure();
      HTTPClient https;

      String url = "https://api.spotify.com/v1/me/player/currently-playing";
      https.useHTTP10(true);
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      int httpResponseCode = https.GET();
      bool success = false;
      String songId;
      //bool refresh = false;
      // Check if the request was successful
      if (httpResponseCode == 200) {
          
          String currentSongProgress = getValue(https,"progress_ms");
          currentSongPositionMs = currentSongProgress.toFloat();
          String imageLink = "";
          while(imageLink.indexOf("image") == -1){
              String height = getValue(https,"height");
              // Serial.println(height);
              if(height.toInt() > 300){
                  imageLink = "";
                  continue;
              }
              imageLink = getValue(https, "url");
              
              // Serial.println(imageLink);
          }
          
          String albumName = getValue(https,"name");
          String artistName = getValue(https,"name");
          String songDuration = getValue(https,"duration_ms");
          currentSong.durationMs = songDuration.toInt();
          String songName = getValue(https,"name");
          songId = getValue(https,"uri");
          String isPlay = getValue(https, "is_playing");
          isPlaying = isPlay == "true";
          Serial.println(isPlay);
          // Serial.println(songId);
          songId = songId.substring(15,songId.length()-1);
          // Serial.println(songId);
          //Serial.println(ESP.getFreeHeap());
          https.end();
          Serial.println(ESP.getFreeHeap());
          // listLittleFS();
          if (songId != currentSong.Id){
              
              if(LittleFS.exists("/albumArt.jpg") == true) {
                  LittleFS.remove("/albumArt.jpg");
              }
              // Serial.println("trying to get album art");
              bool loaded_ok = getFile(imageLink.substring(1,imageLink.length()-1).c_str(), "/albumArt.jpg"); // Note name preceded with "/"
              Serial.println("Image load was: ");
              Serial.println(loaded_ok);
              //refresh = true;
              songChanged = true;
              stateChanged = true;
              //tft.fillScreen(TFT_BLACK);
          }
          currentSong.album = albumName.substring(1,albumName.length()-1);
          currentSong.artist = artistName.substring(1,artistName.length()-1);
          currentSong.song = songName.substring(1,songName.length()-1);
          currentSong.Id = songId;
          currentSong.isLiked = findLikedStatus(songId);
          success = true;
      } else {
          Serial.print("Error getting track info: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
          https.end();
      }
      
      
      // Disconnect from the Spotify API
      // if(success){
      //     drawScreen(refresh);
      //     lastSongPositionMs = currentSongPositionMs;
      // }
      if(isAvailable != success){
        stateChanged = true;
        isAvailable = success;
      }
      return success;
  }
  bool findLikedStatus(String songId){
      std::unique_ptr<BearSSL::WiFiClientSecure>client(std::make_unique<BearSSL::WiFiClientSecure>());
      client->setInsecure();
      HTTPClient https;

      String url = "https://api.spotify.com/v1/me/tracks/contains?ids="+songId;
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      https.addHeader("Content-Type","application/json");
      int httpResponseCode = https.GET();
      bool success = false;
      // Check if the request was successful
      if (httpResponseCode == 200) {
          String response = https.getString();
          https.end();
          return(response == "[ true ]");
      } else {
          Serial.print("Error toggling liked songs: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
          https.end();
      }

      
      // Disconnect from the Spotify API
      
      return success;
  }
  /*
  bool toggleLiked(String songId){
      String url = "https://api.spotify.com/v1/me/tracks/contains?ids="+songId;
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      https.addHeader("Content-Type","application/json");
      int httpResponseCode = https.GET();
      bool success = false;
      // Check if the request was successful
      if (httpResponseCode == 200) {
          String response = https.getString();
          https.end();
          if(response == "[ true ]"){
              currentSong.isLiked = false;
              dislikeSong(songId);
          }else{
              currentSong.isLiked = true;
              likeSong(songId);
          }
          drawScreen(false,true);
          Serial.println(response);
          success = true;
      } else {
          Serial.print("Error toggling liked songs: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
          https.end();
      }

      
      // Disconnect from the Spotify API
      
      return success;
  }
  */
  bool drawScreen(){
      // int rectWidth = 120;
      // int rectHeight = 10;
      //uint16_t color = 0xFD80, bg = 0x1AC6; //fg = 0xfd80(yellow red)
      //uint16_t color = 0xFD80, bg = 0x0284; //fg = 0xfd80(yellow red)
      uint16_t color = 0xFD80, bg = 0x09C3; //fg = 0xfd80(yellow red)
      tft.fillScreen(bg);
      if (LittleFS.exists("/albumArt.jpg") == true) { 
          TJpgDec.setSwapBytes(true);
          uint16_t xpos = 53, ypos = 0;
          
          TJpgDec.setJpgScale(4);
          TJpgDec.drawFsJpg(xpos, ypos, "/albumArt.jpg", LittleFS); // scale 4 upper right
          //TJpgDec.setJpgScale(2);
          //TJpgDec.drawFsJpg(89, 2, "/albumArt.jpg", LittleFS); // scale 2 upper right
          //TJpgDec.drawFsJpg(-11 , 0, "/albumArt.jpg", LittleFS); // scale 2 full screen

          tft.drawRect(xpos, ypos, 75, 75, bg);
          tft.drawRect(xpos+1, ypos+1, 73, 73, bg);
          tft.drawRect(xpos+2, ypos+2, 71, 71, bg);
          tft.drawRect(xpos+3, ypos+3, 69, 69, 0x8C8B); //6691
          // tft.drawSmoothRoundRect(xpos, ypos, 0, 4, 74, 74, TFT_BLACK, TFT_BLACK); //inner 
          // tft.drawSmoothRoundRect(xpos + 2, ypos, 9, 4, 69, 71, TFT_BLACK, TFT_BLACK); //outer

          tft.loadFont("manrope-regular36", LittleFS);
          tft.setTextColor(color, bg);
          tft.setCursor(4, 4);
          tft.println("00");
          tft.setCursor(4, tft.getCursorY());
          tft.println("00");
          tft.unloadFont();
      }else{
          TJpgDec.setSwapBytes(false);
          TJpgDec.setJpgScale(1);
          TJpgDec.drawFsJpg(0, 0, "/Angry.jpg", LittleFS);
      }
      tft.setTextDatum(BL_DATUM);
      tft.setTextWrap(true);
      tft.setCursor(0,85);
      tft.setTextColor(color, bg);

      tft.loadFont("leelawad12", LittleFS);
      //tft.setTextColor(TFT_WHITE, TFT_BLACK);
      printSplitString(currentSong.artist,19,95); //15 was 20
      // tft.drawString(currentSong.artist, tft.width() / 2, 10);
      tft.setCursor(0, tft.getCursorY() + 5);
      printSplitString(currentSong.song,19,130);  //15 was 20
      tft.unloadFont();
      // tft.print(currentSong.song);
      // tft.drawString(currentSong.song, tft.width() / 2, 115);
      // tft.drawString(currentSong.song, tft.width() / 2, 125);
      return true;
  }
  /*
  bool togglePlay(){
      String url = "https://api.spotify.com/v1/me/player/" + String(isPlaying ? "pause" : "play");
      isPlaying = !isPlaying;
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      int httpResponseCode = https.PUT("");
      bool success = false;
      // Check if the request was successful
      if (httpResponseCode == 204) {
          // String response = https.getString();
          Serial.println((isPlaying ? "Playing" : "Pausing"));
          success = true;
      } else {
          Serial.print("Error pausing or playing: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
      }

      
      // Disconnect from the Spotify API
      https.end();
      getTrackInfo();
      return success;
  }
  bool adjustVolume(int vol){
      String url = "https://api.spotify.com/v1/me/player/volume?volume_percent=" + String(vol);
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      int httpResponseCode = https.PUT("");
      bool success = false;
      // Check if the request was successful
      if (httpResponseCode == 204) {
          // String response = https.getString();
          currVol = vol;
          success = true;
      }else if(httpResponseCode == 403){
            currVol = vol;
          success = false;
          Serial.print("Error setting volume: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
      } else {
          Serial.print("Error setting volume: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
      }

      
      // Disconnect from the Spotify API
      https.end();
      return success;
  }
  bool skipForward(){
      String url = "https://api.spotify.com/v1/me/player/next";
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      int httpResponseCode = https.POST("");
      bool success = false;
      // Check if the request was successful
      if (httpResponseCode == 204) {
          // String response = https.getString();
          Serial.println("skipping forward");
          success = true;
      } else {
          Serial.print("Error skipping forward: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
      }

      
      // Disconnect from the Spotify API
      https.end();
      getTrackInfo();
      return success;
  }
  bool skipBack(){
      String url = "https://api.spotify.com/v1/me/player/previous";
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      int httpResponseCode = https.POST("");
      bool success = false;
      // Check if the request was successful
      if (httpResponseCode == 204) {
          // String response = https.getString();
          Serial.println("skipping backward");
          success = true;
      } else {
          Serial.print("Error skipping backward: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
      }

      
      // Disconnect from the Spotify API
      https.end();
      getTrackInfo();
      return success;
  }
  bool likeSong(String songId){
      String url = "https://api.spotify.com/v1/me/tracks?ids="+songId;
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      https.addHeader("Content-Type","application/json");
      char requestBody[] = "{\"ids\":[\"string\"]}";
      int httpResponseCode = https.PUT(requestBody);
      bool success = false;
      // Check if the request was successful
      if (httpResponseCode == 200) {
          // String response = https.getString();
          Serial.println("added track to liked songs");
          success = true;
      } else {
          Serial.print("Error adding to liked songs: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
      }
      
      // Disconnect from the Spotify API
      https.end();
      return success;
  }
  bool dislikeSong(String songId){
      String url = "https://api.spotify.com/v1/me/tracks?ids="+songId;
      https.begin(*client,url);
      String auth = "Bearer " + String(accessToken);
      https.addHeader("Authorization",auth);
      // https.addHeader("Content-Type","application/json");
      // char requestBody[] = "{\"ids\":[\"string\"]}";
      int httpResponseCode = https.DELETE();
      bool success = false;
      // Check if the request was successful
      if (httpResponseCode == 200) {
          // String response = https.getString();
          Serial.println("removed liked songs");
          success = true;
      } else {
          Serial.print("Error removing from liked songs: ");
          Serial.println(httpResponseCode);
          String response = https.getString();
          Serial.println(response);
      }

      
      // Disconnect from the Spotify API
      https.end();
      return success;
  }
  */
  bool setRefreshToken(String token){
    refreshToken = token;
    return refreshAuth();
  }
  
  bool accessTokenSet = false;
  bool isPlaying = false;
  bool isAvailable = false;
  bool songChanged = false;
  bool stateChanged = false;
  long tokenStartTime;
  int tokenExpireTime;
  songDetails currentSong;
  float currentSongPositionMs;
  float lastSongPositionMs;
  //int currVol;
private:
  // std::unique_ptr<BearSSL::WiFiClientSecure> client;
  // HTTPClient https;
  String accessToken;
  String refreshToken;
}spotifyConnection;
//Vars for keys, play state, last song, etc.
//Func to establish connection
//Func to refresh connection 
//Funcs for all api calls

//Web server callbacks
void handleRoot() {
    Serial.println("handling root");
    char page[500];
    sprintf(page,mainPage,CLIENT_ID,REDIRECT_URI);
    server.send(200, "text/html", String(page)+"\r\n"); //Send web page
}

void handleCallbackPage() {
    if(!spotifyConnection.accessTokenSet){
        if (server.arg("code") == ""){     //Parameter not found
            char page[500];
            sprintf(page,errorPage,CLIENT_ID,REDIRECT_URI);
            server.send(200, "text/html", String(page)); //Send web page
        }else{     //Parameter found
            if(spotifyConnection.getUserCode(server.arg("code"))){
                server.send(200,"text/html","Spotify setup complete Auth refresh in :"+String(spotifyConnection.tokenExpireTime));
            }else{
                char page[500];
                sprintf(page,errorPage,CLIENT_ID,REDIRECT_URI);
                server.send(200, "text/html", String(page)); //Send web page
            }
        }
    }else{
        server.send(200,"text/html","Spotify setup complete");
    }
}

////////////////////////////////////////////////////
//
//        Weather API functions and Faces
//
////////////////////////////////////////////////////

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

bool getApiWeatherCurrent(){
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;
  int httpCode = -1;

  char completeRequest[140];
  sprintf(completeRequest, openWeatherCurrentUrl, openWeatherApiKey, lat, lon);

  //Initializing an HTTPS communication using the secure client
  //Serial.print("[HTTPS] begin...\n");
  if (https.begin(*client, completeRequest)) {  // HTTPS
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
        strcpy(currentIconAPI, doc["weather"][0]["icon"]);
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

bool getApiWeather3HrForecast(){
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;
  int httpCode = -1;

  char completeRequest[135];
  sprintf(completeRequest, openWeather3HrForecastUrl, openWeatherApiKey, lat, lon, FORECAST_RANGE);
  if (https.begin(*client, completeRequest)) {  // HTTPS
    httpCode = https.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        JsonDocument doc;
        deserializeJson(doc, https.getStream());
        
        for(int i=0 ; i<FORECAST_RANGE;i++){
          strcpy(forecastHourDescAPI[i], doc["list"][i]["weather"][0]["description"]);
          strcpy(forecastHourIconAPI[i], doc["list"][i]["weather"][0]["icon"]);
          forecastHourHumidityAPI[i] = doc["list"][i]["main"]["humidity"];
          forecastHourPopAPI[i] = doc["list"][i]["pop"];
          forecastHourPressAPI[i] = doc["list"][i]["main"]["pressure"];
          forecastHourRainAPI[i] = doc["list"][i]["rain"]["3h"] | 0.0f;
          forecastHourTempAPI[i] = doc["list"][i]["main"]["temp"];
        }
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

bool getApiWeatherDailyForecast(){
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;
  int httpCode = -1;

  char completeRequest[160];
  sprintf(completeRequest, openWeatherDailyForecastUrl, openWeatherApiKey, lat, lon);
  if (https.begin(*client, completeRequest)) {  // HTTPS
    httpCode = https.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        JsonDocument doc;
        deserializeJson(doc, https.getStream());
        
        strcpy(currentSummaryAPI, doc["daily"][0]["summary"]);
        currentMaxTempAPI = doc["daily"][0]["temp"]["max"];
        currentMinTempAPI = doc["daily"][0]["temp"]["min"];
        
        strcpy(tomorrowSummaryAPI, doc["daily"][0]["summary"]);
        tomorrowMinTempAPI = doc["daily"][1]["temp"]["min"];
        tomorrowMaxTempAPI = doc["daily"][1]["temp"]["max"];
        tomorrowFeelsLikeAPI = doc["daily"][1]["feels_like"]["day"];
        tomorrowHumidityAPI = doc["daily"][1]["humidity"];
        tomorrowRainAPI = doc["daily"][1]["rain"] | 0.0f;
        tomorrowPopAPI = doc["daily"][1]["pop"];

        // for(int i=0 ; i<FORECAST_RANGE;i++){
        //   strcpy(forecastHourDescAPI[i], doc["list"][i]["weather"][0]["description"]);
        //   strcpy(forecastHourIconAPI[i], doc["list"][i]["weather"][0]["icon"]);
        //   forecastHourHumidityAPI[i] = doc["list"][i]["main"]["humidity"];
        //   forecastHourPopAPI[i] = doc["list"][i]["pop"];
        //   forecastHourPressAPI[i] = doc["list"][i]["main"]["pressure"];
        //   forecastHourRainAPI[i] = doc["list"][i]["rain"]["3h"] | 0.0f;
        //   forecastHourTempAPI[i] = doc["list"][i]["main"]["temp"];
        // }
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

/*
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
        
        //Serial.println(ESP.getFreeHeap(),DEC);
        DeserializationError error = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
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
*/
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

//default face, includes a bit of everything
void displayFace0(){
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

//spotify face, but only the time is updated regularly
void displayFace1(){
  uint16_t color = 0xFD80, bg = 0x09C3;
  
  if(spotifyConnection.stateChanged){
    tft.fillScreen(bg);
    tft.setTextColor(color, bg, true);
    tft.loadFont("leelawad12", LittleFS);
    tft.setTextDatum(BL_DATUM);
    tft.setTextWrap(true);
    tft.setCursor(0,85);

    if(!spotifyConnection.accessTokenSet){
      printSplitString("Spotify not logged in",19,95);
    }
    else if(!spotifyConnection.isAvailable){
      printSplitString("Not connected or Nothing Playing",19,95);
    }
    //else if(spotifyConnection.songChanged){
    else{
      if (LittleFS.exists("/albumArt.jpg") == true) { 
        TJpgDec.setSwapBytes(true);
        uint16_t xpos = 53, ypos = 0;
        
        TJpgDec.setJpgScale(4);
        TJpgDec.drawFsJpg(xpos, ypos, "/albumArt.jpg", LittleFS); // scale 4 upper right

        tft.drawRect(xpos, ypos, 75, 75, bg);
        tft.drawRect(xpos+1, ypos+1, 73, 73, bg);
        tft.drawRect(xpos+2, ypos+2, 71, 71, bg);
        tft.drawRect(xpos+3, ypos+3, 69, 69, 0x8C8B); //6691
      }
      
      printSplitString(spotifyConnection.currentSong.artist,19,95); //15 was 20
      // tft.drawString(currentSong.artist, tft.width() / 2, 10);
      tft.setCursor(0, tft.getCursorY() + 5);
      printSplitString(spotifyConnection.currentSong.song,19,130);  //15 was 20
      spotifyConnection.songChanged = false;
    }
    tft.unloadFont();
  }

  if(prevMinute != now.minute() || spotifyConnection.stateChanged){
    char hour[3] = "00", min[3] = "00";
    hour[0] = '0' + now.hour() / 10;
    hour[1] = '0' + now.hour() % 10;
    min[0] = '0' + now.minute() / 10;
    min[1] = '0' + now.minute() % 10;

    tft.loadFont("manrope-regular36", LittleFS);
    tft.setTextColor(color, bg, true);

    //tft.fillRect(3, 3, 49, 72, bg);

    tft.setCursor(4, 4);
    tft.println(hour);
    tft.setCursor(4, tft.getCursorY());
    tft.println(min);
    tft.unloadFont();
    prevMinute = now.minute();
  }
  spotifyConnection.stateChanged = false;
}

//////////////////////////////////////////////////////
//
//                SETUP
//
//////////////////////////////////////////////////////

void setup(){
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  internetAvailable = connectToWifi(true, 10000);//initiate wifi connection
  //connectToWifi(false);

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
  analogWriteFreq(72);
  analogWrite(tftPow,tftBrightness);

  //initiate TFT display
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setJpgScale(4);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  //setting up button fuctions
  button = OneButton(btnInput, true);
  // attachInterrupt(digitalPinToInterrupt(btnInput), clickDown, FALLING);
  // attachInterrupt(digitalPinToInterrupt(btnInput), clickUp, RISING);

  button.attachClick(singleClick);
  // button.attachDoubleClick(doubleClick);
  // button.attachMultiClick(multiClick);
  // button.setPressMs(400); // that is the time when LongPressStart is called
  // button.attachLongPressStart(pressStart);
  // button.attachLongPressStop(pressStop);

  // //perhaps use long press instead of double click
  // button.attachDuringLongPress(duringLongPress);
  // button.setLongPressIntervalMs(1200);

  //setup external interrupt from ds1307
  pinMode(extIntrpt, INPUT);
  attachInterrupt(digitalPinToInterrupt(extIntrpt), checkTicks, CHANGE);

  if(rtc.readSqwPinMode() != DS1307_SquareWave1HZ)
    rtc.writeSqwPinMode(DS1307_SquareWave1HZ);

  //check if refresh token is already present
  prefs.begin("spotify");
  if(prefs.isKey("refreshToken")){
    // Serial.println("Refresh token already present---");
    // Serial.println(prefs.getString("refreshToken"));
    // Serial.println("---");
    delay(4000);
    spotifyConnection.setRefreshToken(prefs.getString("refreshToken"));
  }

//start server if token could not be refreshed
  if(!spotifyConnection.accessTokenSet){
    server.on("/", handleRoot);      //Which routine to handle at root location
    server.on("/callback", handleCallbackPage);      //Which routine to handle at root location
    server.begin();                  //Start server
    Serial.println("HTTP server started");
    //tft.println(WiFi.localIP());
  }
  else
    serverOn = false;

  for(int i = 0 ; i < 10; i++){
    parts[i] = (char*)malloc(sizeof(char) * 20);
  }

  //setup mdns
  if (!MDNS.begin("esp8266")) {
    Serial.println("Error setting up MDNS responder!");
    while (1) { delay(1000); }
  }
  Serial.println("mDNS responder started");
  //MDNS.addService("http", "tcp", 80);
  //Begin OTA service
  // ArduinoOTA.setHostname("esp8266");
  // ArduinoOTA.setPassword("123");
  //ArduinoOTA.begin();
}//setup

//////////////////////////////////////////////////////
//
//                LOOP
//
//////////////////////////////////////////////////////

void loop(){

  //TODO : How to refresh spotify token?? And Setup timers for daily and 3 hourly forecasts

  MDNS.update();
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
    case 0:
      if(currDisplayFace != prevDisplayFace){
        prevDisplayFace = currDisplayFace;
        tft.fillScreen(TFT_BLACK);
      }
      displayFace0();
      break;
    
    case 1:
      if(currDisplayFace != prevDisplayFace){
        prevDisplayFace = currDisplayFace;
        tft.fillScreen(0x09C3);
        spotifyConnection.stateChanged = true;
        //tft.fillScreen(TFT_BLACK);
      }
      displayFace1();
      break;
    
    case 3:
      break;
    }
    refreshDisplay = false;
    //Serial.println(ESP.getFreeHeap(), DEC);
  }
  button.tick();
  if( !isTimeSetFromNTP && WiFi.status() == WL_CONNECTED) { // TODO: find way to reduce checking rate if rtc is already set
    if(checkInternet()){
      Serial.println(F("Connected to WiFi, updating time from NTP"));
      setRTCfromNTP();
      isTimeSetFromNTP = true;
    }
    sec10over = false;//TODO : find a better/efficient solution
  }

  if(sec5over){
    if(currDisplayFace == 1 && spotifyConnection.accessTokenSet){
      spotifyConnection.getTrackInfo();
    }
    sec5over = false;
  }

  if(sec10over){
    if(currDisplayFace != 1 && spotifyConnection.accessTokenSet){
      spotifyConnection.getTrackInfo();
    }
    sec10over = false;
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
      internetAvailable = getApiWeatherCurrent();
      if(internetAvailable)
        min5over=false;
    }
    //Serial.print("\nFinished in ");
    //Serial.println(millis()-time);
  }

  button.tick();

  if(spotifyConnection.accessTokenSet){
    if(serverOn)
      serverOn = false;
  }
  else
    server.handleClient();
  delay(10);
  button.tick();
  //Serial.println(ticks);
}














