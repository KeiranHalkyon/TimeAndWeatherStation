/*
  TODO : 1) Setup state diagram/machine
         2) Determine how the display will be updated - DONE
            a) decide if display behavoiur will change on power status - DONE
         3) Setup the following required modules - 
            a) Time updater (RTC ds1307) - DONE
                i) Setup time on - (first boot, connection to network, repeated interval when connected to network) - DONE
                ii) worry if leap years, minutes and second will be managed locally or by NTP - (done by NTP) - DONE
            b) Temp and pressure updater (bmp280) - DONE
                i) Altitude meter? - Done
            c) Temp and humidity updater (aht20) - DONE
            d) Setup button for input - DONE
                i) Set state via interrupt method into a variable - DONE
            e) Setup method to retrieve weather from OpenWeatherMap - DONE
                i) Decide if both current and forecast is required? - DONE
            f) Find some way to log local temp, humidity and pressure - NOT REQUIRED
                i) store it in flash or external eprom?
            g) Determine whether to offload data to some external site - DONE
                i) if doing this.. decide where - DONE
                ii) decide frequency of update - DONE
            h) manage wifi - DONE(Keeping singular AP for now)
                i) add multiple AP if possible
                ii) change state according to wifi connection
                iii) use wifi library in future...
            i) determine whether running on battery(3.3v) or main(5v) - DONE
                i) update states as necessary
                ii) Show battery status if possible
            j) Spotify Player info (overkill) - DONE
                i) Determine polling rate - YET TO DETERMINE
            h) Make the summary and default face more user friendly - DONE
            i) Make header file - ALMOST DONE
*/
/////////////////////////////////////////////////////
//
//                DECLARATIONS
//
/////////////////////////////////////////////////////
#define FORECAST_RANGE                6
#define DEFAULT_FACE 0
#define WEATHER_SUMMARY_FACE 1
#define SPOTIFY_FACE 2
#define OTA_FACE 3
#define NO_OF_FACES 4
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
#include <timer.h>
#include <timerManager.h>

#include "user_constants.hpp"
#include "Web_Fetch.h"
#include "index.h"
#include "iconsA.h"
#include "main.h"
//////////////////////////////////////////////////////
//
//              GLOBAL VARIABLES
//
//////////////////////////////////////////////////////

//display and input pins
const uint8_t tftPow = 16,//3
              tftRST = 2, 
              tftCS = 0,
              tftDC = 15,
              tftCLK = 14,
              tftMOSI = 13,
              btnInput = 12,
              extIntrpt = 16,
              batteryPin = 17; //3

//flag to see if time has been set from online source
bool  isTimeSetFromNTP = false;
//flag to see if internet is available, updated regularly
bool  internetAvailable = false;
//flag to see if weather is updated, updated regularly
bool  weatherUpdated = false;
//flag to see if weather and battery sensors is updated, updated regularly
bool  sensorUpdated = false;
//flag to see if esp has hanged or not, internel debug
bool  updateDot = false;
//flag indicating if device is on battery or not
bool  onBattery = false;
//flag indicating previous battery set
bool  prevOnBattery = false;

//display update interval in ms
const uint32_t displayUpdatet =       5*1000;
//sensor update interval in ms
const uint32_t sensorsUpdatet =       5*1000;
//rtc to esp time update interval in ms
const uint32_t timeUpdatet =             500;
//spotify update on live power interval in ms
const uint32_t spotifyLongt =        10*1000;
//spotify update on battery interval in ms
const uint32_t spotifyShortt =        5*1000;
//RDS data upload interval in ms
const uint32_t rdst =              5*60*1000;
//current weather update interval in ms
const uint32_t weathert =          5*60*1000;
//internet connection update interval in ms
const uint32_t internetUpdatet =   2*60*1000;

//various timers for regular function calls
Timer baseTimer, refreshTimeTimer, refreshSensorTimer, hourlyTimer,
      refreshDisplayTimer, spotifyTimer, rdsTimer, weatherTimer, refreshInternetTimer,
      pcInfoTimer;

//var to keep track of currently displayed face
uint8_t currDisplayFace = 1;
//var to force display updates on button press
uint8_t prevDisplayFace = -1;
//display rotation state
uint8_t rotation = 2;
//var to update time on minutes change
uint8_t prevMinute = 100;
//var to update time on hour change
uint8_t prevHour = 100;
//var to keep track of hour tasks execution
uint8_t hourTaskCount = 0;

//count no. of external interrupts
// volatile uint16_t ticks = 0;

OneButton button;

RTC_DS1307 rtc;
DateTime now;//Time object, represents latest updated time from rtc

Adafruit_BMP280 bmp; // I2C
float tempBMP, pressureBMP, altitudeBMP,
      sumTempBMP=0, sumPressureBMP=0;

Adafruit_AHTX0 aht;
float humidityAHT, tempAHT,
      sumHumidityAHT = 0, sumTempAHT = 0;

uint16_t sensorReadingCount = 0;

int8_t displayDefaultState = 0; 

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

//PC info variables
float pcCpuTemp = 0.0f, pcGpuTemp = 0.0f, pcBattery = 0.0f, pcMemory = 0.0f, pcCpuLoad = 0.0f, 
  pcGpuLoad = 0.0f, pcFPS = 0.0f, pcFrameTime = 0.0f, pcDownloadRate = 0.0f, pcUploadRate = 0.0f;
bool pcThrottle = false, pcInfoUpdated = false;
//unsigned long lastPcUpdate = 0l;

//////////////////////////////////////////////////////
//
//                UTILITY FUNCTIONS
//
//////////////////////////////////////////////////////

/*
IRAM_ATTR void checkTicks(){

  //TODO : why does button.tick() in this ISR crash album art download???????????

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

  //yield();
  //button.tick(); 
}
*/

IRAM_ATTR void checkClicks(){
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

//timeout of 0(zero) means wait forever, waitForTimeout being false means donot wait, just begin connection and proceed
bool connectToWifi(bool waitForTimeout, unsigned long timeout){

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

void printTemp(bool sensor){
  //sensor = false means bmp, true means AHT
  char tempStr[6];
  if(!sensor)
    dtostrf(tempBMP, 3, 1, tempStr);
  else
    dtostrf(tempAHT, 3, 1, tempStr);

  tft.print(tempStr);
}

// this function will be called when the button was pressed 1 time only.
void singleClick() {
  currDisplayFace = (currDisplayFace+1)%NO_OF_FACES;
  Serial.print("Current Face : ");
  Serial.println(currDisplayFace);
  refreshDisplay();
} // singleClick

// this function will be called when the button was pressed 2 times in a short timeframe.
void doubleClick() {
  if(currDisplayFace == DEFAULT_FACE){
    //if internet is available
    if(displayDefaultState >=0){
      displayDefaultState = -3;
      pcInfoTimer.setInterval(10*1000);
      pcInfoUpdated = true; //so that the screen is updated immediately
    }
    //if PC info is already shown, revert back to default
    else if(displayDefaultState == -3){
      pcInfoTimer.stop();
      displayDefaultState = 0;
    }
    refreshDisplay();
  }
} // doubleClick

// this function will be called when the button was pressed multiple times in a short timeframe.
void multiClick() {
  // inputStartTime = millis();
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
void longPressStart() {
  // inputStartTime = millis();
  Serial.println("pressStart()");
  // pressStartTime = millis() - 1000; // as set in setPressMs()
} // pressStart()

// long press button to force refresh RTC from NTP
void longPressStop() {
  Serial.println("Resetting time from NTP");
  setRTCfromNTP();
}

void duringLongPress(){
  Serial.println("Long Press ongoing");
}

String removeBackslash(String text){
  String result;
  result.reserve(text.length()+1);

  for(int i = 0; i < text.length(); i++){
    char ch = text.charAt(i);
    if(ch == '\\')
      continue;
    result += ch;
  }
  return result;
}

void checkPower(){
  onBattery = (analogRead(batteryPin)>25);
}

const unsigned short* getIcon(char icon[]){
  int iconNum = (icon[0] - '0')*10 + (icon[1] - '0');
  switch(iconNum){
    case 1 : return i01d;
    case 2 : return i02d;
    case 3 : return i03d;
    case 4 : return i04d;
    case 9 : return i09d;
    case 10 :return i10d;
    case 11 :return i11d;
    case 13 :return i13d;
    case 50 :return i50d;
    default : return exclamation;
  }
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
      [[maybe_unused]] int c = stream->readBytes(char_buff, ((size > sizeof(char_buff)) ? sizeof(char_buff) : size));
      if (found) {
        if (seek && char_buff[0] != ':') {
          continue;
        } else if(char_buff[0] != '\n'){
            if(seek && char_buff[0] == ':'){
                seek = false;
                [[maybe_unused]] int c = stream->readBytes(char_buff, 1);
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

void printSplitString(String text,int maxLineSize, int xPos, int yPos)
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
    tft.setCursor(xPos,yPos);
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
        tft.setCursor(xPos, tft.getCursorY());
        tft.println(output);
        // free(printable);
    }
    // Serial.println(ESP.getFreeHeap());
}

void printSplitString2(char text[], int maxLineSize, int xPos, int yPos){
  int lineStart = 0, currentPos = 0, lastPos = -1, currentCount = 0, flag = 0;
  tft.setCursor(xPos, yPos);
  while(true){
    while(text[currentPos] != '\0' && currentCount <= maxLineSize){
      if(text[currentPos] == ' ')
        lastPos = currentPos;
      currentPos++;
      currentCount++;
    }
    if(text[currentPos] == '\0')
      flag = 1;
    else
      text[lastPos] = '\0';
    tft.setCursor(xPos,tft.getCursorY());
    tft.println(&text[lineStart]);
    if(flag==1)
      break;
    else{
      text[lastPos] = ' ';
      currentPos = lastPos + 1;
      currentCount = 0;
      lastPos = -1;
      lineStart = currentPos;
    }
  }
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
          doc.clear();
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
          doc.clear();
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
          //Serial.println(isPlay);
          // Serial.println(songId);
          songId = songId.substring(15,songId.length()-1);
          // Serial.println(songId);
          //Serial.println(ESP.getFreeHeap());
          https.end();
          //Serial.println(ESP.getFreeHeap());
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
            currentSong.album = removeBackslash(albumName.substring(1,albumName.length()-1));
            currentSong.artist = removeBackslash(artistName.substring(1,artistName.length()-1));
            currentSong.song = removeBackslash(songName.substring(1,songName.length()-1));
            currentSong.Id = songId;
            currentSong.isLiked = findLikedStatus(songId);
          }
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
  uint8_t barLength;
  uint8_t barPosX, barPosY;
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
  https.end();
  return (responseCode == 200);
}

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
        doc.clear();
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  https.end();
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
        doc.clear();
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  https.end();
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

        doc.clear();
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  https.end();
  return (httpCode == 200);
}

void getPcInfo(){
  if(!internetAvailable)
    return;
  
  WiFiClient client;
  HTTPClient http;
  int httpCode = -1;

  if (http.begin(client, pcServer)) {  // HTTPS
    httpCode = http.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        JsonDocument doc;
        deserializeJson(doc, http.getStream());

        pcCpuTemp = doc["cpuTemp"];
        pcCpuLoad = doc["cpuLoad"];
        pcThrottle = (doc["cpuThrottle"] == 1)? true : false;
        pcDownloadRate = doc["downloadRate"];
        pcUploadRate = doc["uploadRate"];
        pcGpuLoad = doc["gpuLoad"];
        pcGpuTemp = doc["gpuTemp"];
        pcMemory = doc["memory"];
        pcFPS = doc["framerate"];
        pcFrameTime = doc["frametime"];

        pcInfoUpdated = true;
        doc.clear();
      }
    } else {
      Serial.printf("[HTTPS] GET... failed PCINFO, error: %s\n", http.errorToString(httpCode).c_str());
    }
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  http.end();
  // return (httpCode == 200);
}

unsigned long myAbs(long val){
  return (val>0)? val : -val;
}

void checkInternet(){ // param : bool force = false
  // if(force)
  //   return internetAvailable = Ping.ping(String(remote_host).c_str(),1);
  // else if(internetAvailable && WiFi.isConnected())
  //   return true;
  // else if(WiFi.isConnected() && (myAbs(millis()-lastInternetRefresh) > checkInternetInterval || !rtc.isrunning())){
  //   lastInternetRefresh = millis();
  //   return internetAvailable = Ping.ping(String(remote_host).c_str(),1);
  // }
  // else
  //   return false;
  if(WiFi.status() == WL_CONNECTED)
    internetAvailable = Ping.ping(String(remote_host).c_str(),1);
  else
    internetAvailable = false;
}

void drawPixelFrame(uint16_t gap){
  for(uint16_t i = gap-1; i < 128; i+=gap)
    for(uint16_t j = gap-1; j < 160; j+=gap){
      tft.drawPixel(i,j,TFT_YELLOW);
    }
}

char tempStore[60];
//default face, includes a bit of everything
void displayDefault(uint16_t color, uint16_t bg, bool refresh){
  //uint16_t color = 0xFD80, bg = TFT_BLACK;
  tft.setTextColor(color, bg);

  if(refresh){
    displayDefaultState = 0;
    // Serial.print("Refresh default display");
  }

  //print update/refresh indicator
  if(updateDot)
    tft.drawPixel(125,2,color);
  else
    tft.drawPixel(125,2,bg);
  updateDot = !updateDot;

  //print date
  if(prevHour != now.hour() || refresh){
    tft.setCursor(2, 3);
    char dateStr[9] = "00/00/00";
    dateStr[0] = '0' + now.day() / 10;
    dateStr[1] = '0' + now.day() % 10;
    dateStr[3] = '0' + now.month() / 10;
    dateStr[4] = '0' + now.month() % 10;
    dateStr[6] = '0' + (now.year() % 100) / 10;
    dateStr[7] = '0' + now.year() % 10;
    tft.loadFont("manrope-semibold16", LittleFS);
    tft.fillRect(2,2,75,14,bg);             //date frame
    tft.print(dateStr);
  
    //print day
    tft.setCursor(84, 3);
    tft.fillRect(83,2,35,14,bg);            //day frame
    tft.print(daysOfTheWeekShort[now.dayOfTheWeek()]);
    tft.unloadFont();

    prevHour = now.hour();
  }

  //print time
  if(prevMinute != now.minute() || refresh){
    char time[6] = "00:00";
    time[0] = '0' + now.hour() / 10;
    time[1] = '0' + now.hour() % 10;
    time[3] = '0' + now.minute() / 10;
    time[4] = '0' + now.minute() % 10;
    tft.loadFont("manrope-regular33", LittleFS);
    tft.setCursor(3, 23);
    tft.fillRect(3,21,89,28,bg);           //time frame
    tft.print(time);
    tft.unloadFont();

    prevMinute = now.minute();
  }

  tft.loadFont("manrope-semibold12", LittleFS);
  //Local Info
  if(sensorUpdated || refresh){
    //tft.loadFont("manrope-semibold12", LittleFS);
    tft.fillRect(67,54,53,36,bg);
    tft.setCursor(68,55);
    tft.printf("%.1f 'C ", tempAHT);
    tft.setCursor(68, 67);
    tft.printf("%.0f mb ", pressureBMP);
    tft.setCursor(68, 79);
    tft.printf("%.1f %% ", humidityAHT);
    tft.fillRect(120, 55, 5, 33, TFT_DARKGREEN);

    if(onBattery)
      tft.fillRect(120,3,5,12,TFT_ORANGE);
    else
      tft.fillRect(120,3,5,12,TFT_DARKGREEN);

    sensorUpdated = false;
  }

  if(internetAvailable){
    if(weatherUpdated || refresh){
      tft.setSwapBytes(true);
      tft.pushImage(98, 23, 25, 25, getIcon(currentIconAPI));
      tft.setSwapBytes(false);

      //Online info
      //tft.loadFont("manrope-semibold12", LittleFS);
      tft.fillRect(2,54,54,36,bg);
      tft.setCursor(3,55);
      tft.printf("%.1f 'C ", currentTempAPI);
      tft.setCursor(3, 67);
      tft.printf("%.0f mb ", currentPressAPI);
      tft.setCursor(3, 79);
      tft.printf("%.1f %% ", currentHumidityAPI);
      tft.fillRect(56, 55, 5, 33, TFT_DARKCYAN);

      weatherUpdated = false;
    }

    // if(displayDefaultState < 0)
    //   displayDefaultState = 0;
    //show notif on song change
    if(spotifyConnection.stateChanged && spotifyConnection.isAvailable){
      tft.fillRect(0, 93, 127, 68, bg);  // description + temps frame
      //tft.loadFont("manrope-semibold12", LittleFS);
      // char temp[50];
      // sprintf(temp,"%s",spotifyConnection.currentSong.artist);
      printSplitString(spotifyConnection.currentSong.artist, 15, 3, 109);
      printSplitString(spotifyConnection.currentSong.song, 20, 3, (tft.getCursorY() > 124)? tft.getCursorY()+4 : 124);
      tft.setSwapBytes(true);
      tft.pushImage(98, 97, 25, 25, spotify);
      tft.setSwapBytes(false);

      spotifyConnection.stateChanged = false;
      // displayDefaultState = -1;
      if(displayDefaultState == -3)
        pcInfoUpdated = true;
      else
        displayDefaultState = 0; //reset weather screen to 0 after spotify update
      return;
    }

    //show PC info
    if(displayDefaultState == -3){
      if(pcInfoUpdated){
        tft.fillRect(0, 93, 127, 68, bg);  // description + temps frame
        tft.setCursor(3,95);
        tft.printf("CPU: %.1f%% / %.0f'C\n", pcCpuLoad, pcCpuTemp);
        tft.setCursor(3,108);
        tft.printf("GPU: %.1f%% / %.0f'C\n", pcGpuLoad, pcGpuTemp);
        tft.setCursor(3,121);
        tft.printf("MEM: %.1f%%\n", pcMemory);
        tft.setCursor(3,134);
        tft.printf("FPS/FTM: %.0f / %.1f\n", pcFPS, pcFrameTime);
        tft.setCursor(3,147);
        tft.printf("DL/UP: %.1f / %.1f", pcDownloadRate, pcUploadRate);

        if(pcThrottle)
          tft.fillRect(120, 98, 5, 5, TFT_RED);
        else
          tft.fillRect(120, 98, 5, 5, TFT_GREEN);
        pcInfoUpdated = false;
      }
      return;
    }

    //today or tomorrow weather description
    else if(displayDefaultState == 0){
      //tft.loadFont("manrope-semibold12", LittleFS);
      tft.setTextColor(color, bg);
      tft.fillRect(0, 93, 127, 68, bg);  // description + temps frame
      if(now.hour() >= 20){
        printSplitString2(tomorrowSummaryAPI,20,3,94); 
        tft.fillRect(120, 136, 5, 5, TFT_GREENYELLOW);
      }
      else{
        printSplitString2(currentSummaryAPI,20,3,94);
        tft.fillRect(120, 136, 5, 5, TFT_RED);
      }
      tft.drawFastHLine(3,143,123,color);     //bottom to description
      tft.setCursor(1,147);
      if(now.hour() >= 20)
        tft.printf("%.1f'C | %.1f'C | %.1f'C", tomorrowMaxTempAPI, tomorrowMinTempAPI, tomorrowFeelsLikeAPI);
      else
        tft.printf("%.1f'C | %.1f'C | %.1f'C", currentMaxTempAPI, currentMinTempAPI, currentFeelsLikeAPI);
      tft.unloadFont();
    }
    // 3hr forecasts
    else if(displayDefaultState >= 1){
      //tft.loadFont("manrope-semibold12", LittleFS);
      tft.fillRect(0, 93, 127, 26, bg);  // description frame
      // char tempStore[50];
      sprintf(tempStore,"In %dhrs : %s - %.1f'C", displayDefaultState*3, forecastHourDescAPI[displayDefaultState-1], forecastHourTempAPI[displayDefaultState-1]);
      printSplitString2(tempStore, 22, 3, 94);

      if(displayDefaultState == 1){
        tft.fillRect(1, 120, 127, 42, bg);  // Rain frame
        tft.drawFastHLine(3, tft.getCursorY()-1, 123, color); // below description

        int index = -1;
        for(int i = 0; i < FORECAST_RANGE; i++)
          if(forecastHourPopAPI[i] >= 0.3f && forecastHourRainAPI[i] >= 0.05f){
            index = i;
            break;
          }
        
        if(index < 0){
          sprintf(tempStore,"No rain in next %d hours", (FORECAST_RANGE-1)*3);
          printSplitString2(tempStore, 20, 3, tft.getCursorY() + 2);
        }
        else{
          sprintf(tempStore,"Rain in next %d hours", (index+1)*3);
          printSplitString2(tempStore, 20, 3, tft.getCursorY() + 2);
          tft.setCursor(3, tft.getCursorY());
          tft.printf("Chance : %.0f %%\n",forecastHourPopAPI[index]*100);
          tft.setCursor(3, tft.getCursorY());
          tft.printf("Amount : %.2f mm",forecastHourRainAPI[index]);
        }
      }
    }
    tft.unloadFont();

    displayDefaultState++;
    if(displayDefaultState>FORECAST_RANGE)
      displayDefaultState = 0;
  }
  // No connnection screen
  else if(displayDefaultState >= 0){
    displayDefaultState = -2; // mark that no connection screen is already there
    //tft.loadFont("manrope-semibold12", LittleFS);
    tft.setTextColor(color, bg);
    tft.setSwapBytes(true);
    tft.pushImage(98,23,25,25,exclamation);
    tft.setSwapBytes(false);
    tft.fillRect(0, 93, 127, 68, bg);  // description + temps frame
    tft.setCursor(3,94);
    tft.print("No Connection !");
    //Serial.print("Here");
  }
  tft.unloadFont();
}

//spotify face, but only the time is updated regularly
void displaySpotify(){
  uint16_t color = 0xFD80, bg = 0x09C3;
  
  if(spotifyConnection.stateChanged){
    tft.fillScreen(bg);
    tft.setTextColor(color, bg, true);
    //tft.loadFont("leelawad12", LittleFS);
    tft.loadFont("manrope-semibold12", LittleFS);
    tft.setTextDatum(BL_DATUM);
    tft.setTextWrap(true);
    tft.setSwapBytes(false);
    //tft.setCursor(0,87);
    uint8_t textStartX = 8, textStartY = 87;

    if(!spotifyConnection.accessTokenSet){
      printSplitString("Spotify not logged in", 19, textStartX, textStartY);
    }
    else if(!spotifyConnection.isAvailable){
      printSplitString("Not connected or Nothing Playing", 19, textStartX, textStartY);
    }
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
      
      printSplitString(spotifyConnection.currentSong.artist, 19, textStartX, textStartY);
      printSplitString(spotifyConnection.currentSong.song, 19, textStartX, tft.getCursorY()+7);

      spotifyConnection.barLength = tft.getCursorY() - textStartY + 3;
      spotifyConnection.lastSongPositionMs = -1.0f;
      spotifyConnection.barPosX = 3;
      spotifyConnection.barPosY = textStartY -3;
      tft.drawFastVLine(3, textStartY - 3, spotifyConnection.barLength, 0x8C8B); //8C8B
      tft.drawFastVLine(4, textStartY - 3, spotifyConnection.barLength, 0x8C8B); //E042

      spotifyConnection.songChanged = false;
    }
    tft.unloadFont();
  }

  if(spotifyConnection.lastSongPositionMs != spotifyConnection.currentSongPositionMs){
    int progress = (int)(spotifyConnection.currentSongPositionMs * 
        spotifyConnection.barLength) / spotifyConnection.currentSong.durationMs;
    
    tft.drawFastVLine(spotifyConnection.barPosX,spotifyConnection.barPosY,progress,0xE042);
    tft.drawFastVLine(spotifyConnection.barPosX+1,spotifyConnection.barPosY,progress,0xE042);
    spotifyConnection.lastSongPositionMs = spotifyConnection.currentSongPositionMs;
  }

  if(prevMinute != now.minute() || spotifyConnection.stateChanged){
    char hour[3] = "16", min[3] = "07";
    hour[0] = '0' + now.hour() / 10;
    hour[1] = '0' + now.hour() % 10;
    min[0] = '0' + now.minute() / 10;
    min[1] = '0' + now.minute() % 10;

    tft.loadFont("manrope-regular40", LittleFS);
    tft.setTextColor(color, bg);

    //Different cursor location required for centering 1 and 7

    tft.fillRect(3, 3, 49, 36, bg);
    tft.setCursor( (hour[0]=='1')? 8 : 4, 4);
    tft.print(hour[0]);
    tft.setCursor((hour[1]=='1')? 32 : (hour[1]=='7')? 31 : 28, 4);
    tft.println(hour[1]);

    tft.fillRect(3, tft.getCursorY()-1, 49, 36, bg);
    tft.setCursor( (min[0]=='1')? 8 : 4, tft.getCursorY());
    tft.print(min[0]);
    tft.setCursor((min[1]=='1')? 32 : (min[1]=='7')? 31 : 28, tft.getCursorY());
    tft.println(min[1]);

    tft.unloadFont();

    // tft.drawFastVLine(16,4,75,TFT_RED);
    // tft.drawFastVLine(40,4,75,TFT_RED);

    prevMinute = now.minute();
  }
  spotifyConnection.stateChanged = false;
}

void displayWeather(uint16_t color, uint16_t bg){
  tft.loadFont("manrope-semibold12", LittleFS);
  tft.setSwapBytes(true);

  //1st Segment
  tft.drawFastVLine(94, 1, 28, color);   //right to time
  tft.drawFastHLine(3, 31, 89, color);   //bottom to time
  tft.drawFastHLine(97, 31, 28, color);   //bottom to icon
  
  tft.setCursor(3,4);
  tft.printf("%.1f'C  %.0f mb\n", forecastHourTempAPI[0], forecastHourPressAPI[0]);
  tft.setCursor(3,18);
  tft.printf("%.0f%%  %.2f mm", forecastHourPopAPI[0]*100, forecastHourRainAPI[0]);
  tft.pushImage(98, 4, 25, 25, getIcon(forecastHourIconAPI[0]));

  //2nd Segment
  tft.drawFastVLine(94, 34, 28, color);   //right to time
  tft.drawFastHLine(3, 64, 89, color);   //bottom to time
  tft.drawFastHLine(97, 64, 28, color);   //bottom to icon

  tft.setCursor(3,37);
  tft.printf("%.1f'C  %.0f mb\n", forecastHourTempAPI[1], forecastHourPressAPI[1]);
  tft.setCursor(3,51);
  tft.printf("%.0f%%  %.2f mm", forecastHourPopAPI[1]*100, forecastHourRainAPI[1]);
  tft.pushImage(98, 37, 25, 25, getIcon(forecastHourIconAPI[1]));
  
  //3rd Segment
  tft.drawFastVLine(94, 67, 28, color);   //right to time
  tft.drawFastHLine(3, 97, 89, color);   //bottom to time
  tft.drawFastHLine(97, 97, 28, color);   //bottom to icon

  tft.setCursor(3,70);
  tft.printf("%.1f'C  %.0f mb\n", forecastHourTempAPI[2], forecastHourPressAPI[2]);
  tft.setCursor(3,84);
  tft.printf("%.0f%%  %.2f mm", forecastHourPopAPI[2]*100, forecastHourRainAPI[2]);
  tft.pushImage(98, 70, 25, 25, getIcon(forecastHourIconAPI[2]));
  
  //4th Segment
  tft.drawFastVLine(94, 100, 28, color);   //right to time
  tft.drawFastHLine(3, 130, 89, color);   //bottom to time
  tft.drawFastHLine(97, 130, 28, color);   //bottom to icon

  tft.setCursor(3,103);
  tft.printf("%.1f'C  %.0f mb\n", forecastHourTempAPI[3], forecastHourPressAPI[3]);
  tft.setCursor(3,117);
  tft.printf("%.0f%%  %.2f mm", forecastHourPopAPI[3]*100, forecastHourRainAPI[3]);
  tft.pushImage(98, 103, 25, 25, getIcon(forecastHourIconAPI[3]));
  
  //5th Segment
  tft.drawFastVLine(94, 133, 28, color);   //right to time

  tft.setCursor(3,133);
  tft.printf("%.1f'C  %.0f mb\n", forecastHourTempAPI[4], forecastHourPressAPI[4]);
  tft.setCursor(3,147);
  tft.printf("%.0f%%  %.2f mm", forecastHourPopAPI[4]*100, forecastHourRainAPI[4]);
  tft.pushImage(98, 133, 25, 25, getIcon(forecastHourIconAPI[4]));

  tft.setSwapBytes(false);
}

void displayOTA(){
  tft.setCursor(0,0);
  tft.println("OTA Enabled");
  Serial.println("OTA Enabled");
}

//////////////////////////////////////////////////////

void refreshSensors(){
  //get fresh data
  refreshBMP();
  refreshAHT();
  checkPower();

  //calculate sum for averages
  sumTempBMP += tempBMP;
  sumPressureBMP += pressureBMP;
  sumHumidityAHT += humidityAHT;
  sumTempAHT += tempAHT;

  sensorReadingCount++;

  sensorUpdated = true;
}

void refreshDisplay(){
  switch (currDisplayFace)
    {
      case DEFAULT_FACE:{
        //colors
        uint16_t color = 0xFD80, bg = TFT_BLACK;
        if(currDisplayFace != prevDisplayFace){
          prevDisplayFace = currDisplayFace;
          tft.fillScreen(TFT_BLACK);
          //tft.fillScreen(TFT_DARKGREY);
          prefs.putChar("lastFace", currDisplayFace);
          //spotifyTimer.setInterval(spotifyLongt);
          //drawPixelFrame(5);

          //drawing the fixed frames
          tft.drawFastVLine(79, 3, 14, color);    //right to date
          tft.drawFastHLine(3, 18, 75, color);    //bottom to date
          tft.drawFastHLine(81, 18, 12, color);   //bottom to day
          tft.drawFastHLine(96, 18, 29, color);   //bottom to day
          tft.drawFastHLine(3, 51, 60, color);   //bottom to time
          tft.drawFastHLine(66, 51, 27, color);   //bottom to time
          tft.drawFastHLine(96, 51, 29, color);   //bottom to icon
          tft.drawFastVLine(94, 21, 29, color);   //right to time
          tft.drawFastVLine(64, 53 , 37, color);  //right to online info
          tft.drawFastHLine(3, 91, 60, color);   //bottom to online
          tft.drawFastHLine(66, 91, 60, color);   //bottom to actual
          displayDefault(color, bg, true);
        }
        else
          displayDefault(color, bg);
        break;
      }

      case WEATHER_SUMMARY_FACE:{
        uint16_t color = 0xFD80, bg = TFT_BLACK;
        if(currDisplayFace != prevDisplayFace){
          prevDisplayFace = currDisplayFace;
          tft.fillScreen(TFT_BLACK);
          prefs.putChar("lastFace", currDisplayFace);
          //spotifyTimer.setInterval(spotifyLongt);
          displayWeather(color, bg);
          //stop pc info update if it was ongoing
          if(pcInfoTimer.isRunning())
            pcInfoTimer.stop();
        }
        break;
      }

      case SPOTIFY_FACE:{
        if(currDisplayFace != prevDisplayFace){
          prevDisplayFace = currDisplayFace;
          tft.fillScreen(0x09C3);
          spotifyConnection.stateChanged = true;
          prefs.putChar("lastFace", currDisplayFace);
          if(onBattery)
            spotifyTimer.setInterval(spotifyShortt*2);
          else
            spotifyTimer.setInterval(spotifyShortt);
          //tft.fillScreen(TFT_BLACK);
        }
        displaySpotify();
        break;
      }

      case OTA_FACE:{
        if(currDisplayFace != prevDisplayFace){
          prevDisplayFace = currDisplayFace;
          if(onBattery)
            spotifyTimer.setInterval(spotifyLongt*3);
          else
            spotifyTimer.setInterval(spotifyLongt);
          tft.fillScreen(TFT_BLACK);
          displayOTA();
        }
        break;
      }
      //Serial.println(ESP.getFreeHeap(), DEC);
    }
}

void refreshSpotify(){
  if(internetAvailable)
    spotifyConnection.getTrackInfo();
}

void updateRDS(){
  if(internetAvailable){
    if(!sendDataToRDS(sumTempBMP/sensorReadingCount,sumPressureBMP/sensorReadingCount,sumTempAHT/sensorReadingCount,sumHumidityAHT/sensorReadingCount)){
      Serial.println("Failed RDS upload");
      checkInternet();
    }
    else{
      sumTempBMP = sumTempAHT = sumHumidityAHT = sumPressureBMP = 0;
      sensorReadingCount = 0;
    }
  }
}

void updateWeather(){
  if(internetAvailable){
    if(!getApiWeatherCurrent()){
      Serial.println("Failed to fetch current weather");
      checkInternet();
    }
    else
      weatherUpdated = true;
  }
}

void hourlyTask(){
  //TODO : the tasks are done in non blocking success/failure, deal with it somehow
  if(hourTaskCount == 0){
      //make hourTask faster if it is called for first time in the hour
      hourlyTimer.setInterval(3000);
    }
  
  if(internetAvailable){
    bool success = false;
    switch(hourTaskCount){
      case 0:
        checkInternet();
        refreshInternetTimer.reset();
        success = true;
        break;
      case 1 :
        success = getApiWeather3HrForecast();
        break;
      case 2 :
        success = getApiWeatherDailyForecast();
        break;
      case 3 :
        if(spotifyConnection.accessTokenSet)
          success = spotifyConnection.refreshAuth();
        break;
      case 4 :
        success = getApiWeatherCurrent();
        weatherTimer.reset();
        break;
    }
    
    if(success){
      Serial.print("Success at hour task : ");
      Serial.println(hourTaskCount);
    }
    else{
      Serial.print("Failure at hour task : ");
      Serial.println(hourTaskCount);
    }

    hourTaskCount++;
    if(hourTaskCount>4){
      hourTaskCount = 0;
      weatherUpdated = true;
      //set timer interval to normal 1 hour time
      hourlyTimer.setInterval((60-now.minute())*60000); //wait till next hour
    }
  }
}

//////////////////////////////////////////////////////
//
//                SETUP
//
//////////////////////////////////////////////////////

void setup(){
  Serial.begin(115200);

  Serial.print(ESP.getResetInfo());
  Serial.print(ESP.getResetReason());

  WiFi.mode(WIFI_STA);
  internetAvailable = connectToWifi(true, 5000);//initiate wifi connection
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

  //initiate TFT display
  tft.init();
  tft.setRotation(rotation);
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setJpgScale(4);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  //setting up button fuctions
  button = OneButton(btnInput, true);
  attachInterrupt(digitalPinToInterrupt(btnInput), checkClicks, CHANGE);

  button.attachClick(singleClick);
  button.attachDoubleClick(doubleClick);
  // button.attachMultiClick(multiClick);
  button.setPressMs(400); // that is the time when LongPressStart is called
  button.attachLongPressStart(longPressStart);
  button.attachLongPressStop(longPressStop);

  // //perhaps use long press instead of double click
  // button.attachDuringLongPress(duringLongPress);
  // button.setLongPressIntervalMs(1200);

  //setup external interrupt from ds1307
  //pinMode(extIntrpt, INPUT_PULLDOWN_16);
  //attachInterrupt(digitalPinToInterrupt(extIntrpt), checkTicks, CHANGE);

  // if(rtc.readSqwPinMode() != DS1307_SquareWave1HZ)
  //   rtc.writeSqwPinMode(DS1307_SquareWave1HZ);

  if(rtc.readSqwPinMode() != DS1307_OFF)
    rtc.writeSqwPinMode(DS1307_OFF);

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
  
  //Setup Arduino OTA
  ArduinoOTA.onStart([]() {
    //detach btn and rtc interrupt
    detachInterrupt(digitalPinToInterrupt(btnInput));
    detachInterrupt(digitalPinToInterrupt(extIntrpt));
    tft.println("Starting update...");

    if(ArduinoOTA.getCommand() == U_FLASH){
      tft.println("Receiving filesystem update...");
      LittleFS.end();
    }
  });
  ArduinoOTA.onEnd([]() {
    tft.println("OTA succeeded");
    tft.println("Rebooting...");
    prefs.putChar("lastFace", 0);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]", error);
    tft.printf("Error %u",error);

    //Reattach interupts
    // attachInterrupt(digitalPinToInterrupt(btnInput), checkTicks, CHANGE);
    attachInterrupt(digitalPinToInterrupt(extIntrpt), checkClicks, CHANGE);
  });
  ArduinoOTA.setHostname("esp8266");
  // ArduinoOTA.setPassword("123");
  ArduinoOTA.begin();

  prefs.begin("spotify");

  //get last display face
  if(prefs.isKey("lastFace")){
    currDisplayFace = prefs.getChar("lastFace");
  }

  //check if refresh token is already present
  if(prefs.isKey("refreshToken")){
    delay(3000); //Delay required otherwise wont reauth for some DAMN reason...
    spotifyConnection.setRefreshToken(prefs.getString("refreshToken"));
  }
  //start server if token could not be refreshed
  if(!spotifyConnection.accessTokenSet){
    server.on("/", handleRoot);      //Which routine to handle at root location
    server.on("/callback", handleCallbackPage);      //Which routine to handle at root location
    server.begin();                  //Start server
    Serial.println("HTTP server started");
  }
  else
    serverOn = false;

  //Setup timer functions
  // baseTimer.setInterval(500);
  // baseTimer.setCallback(checkTicks);
  // baseTimer.start();
  refreshTimeTimer.setInterval(timeUpdatet);
  refreshTimeTimer.setCallback(refreshTimeFromRTC);
  refreshSensorTimer.setInterval(sensorsUpdatet);
  refreshSensorTimer.setCallback(refreshSensors);
  refreshDisplayTimer.setInterval(displayUpdatet);
  refreshDisplayTimer.setCallback(refreshDisplay);
  spotifyTimer.setInterval(spotifyLongt);
  spotifyTimer.setCallback(refreshSpotify);
  rdsTimer.setInterval(rdst); 
  rdsTimer.setCallback(updateRDS); 
  weatherTimer.setInterval(weathert);
  weatherTimer.setCallback(updateWeather);
  hourlyTimer.setCallback(hourlyTask);
  hourlyTimer.setInterval(5000);
  refreshInternetTimer.setInterval(internetUpdatet);
  refreshInternetTimer.setCallback(checkInternet);
  pcInfoTimer.setCallback(getPcInfo);
  // baseTimer.setInterval(5000,1);
  // baseTimer.setCallback(afterBoot);

  TimerManager::instance().start();
  //delay(2000);
  //hourlyTask();
  //updateWeather();
  checkInternet();
}//setup

//////////////////////////////////////////////////////
//
//                LOOP
//
//////////////////////////////////////////////////////

void loop(){
  //TODO : Figure out why refresh token after mdns fails if there is no(substantial) delay

  if(prevOnBattery != onBattery){
    prevOnBattery = onBattery;
    if(onBattery){
      refreshTimeTimer.setInterval(timeUpdatet*3);
      refreshSensorTimer.setInterval(sensorsUpdatet*3);
      refreshDisplayTimer.setInterval(displayUpdatet*2);
      if(currDisplayFace == SPOTIFY_FACE)
        spotifyTimer.setInterval(spotifyShortt*2);
      else
        spotifyTimer.setInterval(spotifyLongt*3);
      rdsTimer.setInterval(rdst*3);
      weatherTimer.setInterval(weathert*3);
      refreshInternetTimer.setInterval(internetUpdatet*3);
    }
    else{
      refreshTimeTimer.setInterval(timeUpdatet);
      refreshSensorTimer.setInterval(sensorsUpdatet);
      refreshDisplayTimer.setInterval(displayUpdatet);
      if(currDisplayFace ==SPOTIFY_FACE)
        spotifyTimer.setInterval(spotifyShortt);
      else
        spotifyTimer.setInterval(spotifyLongt);
      rdsTimer.setInterval(rdst); 
      weatherTimer.setInterval(weathert);
      refreshInternetTimer.setInterval(internetUpdatet);
    }
  }

  MDNS.update();
  if(currDisplayFace != OTA_FACE){
    
    if( !isTimeSetFromNTP && WiFi.status() == WL_CONNECTED) { // TODO: find way to reduce checking rate if rtc is already set
      if(internetAvailable){
        Serial.println(F("Connected to WiFi, updating time from NTP"));
        setRTCfromNTP();
        isTimeSetFromNTP = true;
        refreshInternetTimer.setInterval(internetUpdatet);
      }
      else if(refreshInternetTimer.getElapsedTime()>2000){
        refreshInternetTimer.setInterval(1500);
      }
    }
    
    //handle server client only if network is available AND (spotify token has not been set OR connected to wall power)
    // if(internetAvailable && !(spotifyConnection.accessTokenSet && onBattery))
    if(internetAvailable && !spotifyConnection.accessTokenSet)
      server.handleClient();
    else if(serverOn){
      serverOn = false;
      server.stop();
    }

    TimerManager::instance().update();
  }
  else{
    refreshDisplayTimer.update();
    ArduinoOTA.handle();
  }

  delay(50);
  // baseTimer.update();
  button.tick();
  //Serial.println(ticks);

}

