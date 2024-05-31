#ifndef USER_CONSTANTS
#define USER_CONSTANTS

const char PROGMEM ssid[] = "WIFI_SSID",
        password[] = "WIFI_PASSWORD",
        remote_host[] = "www.google.com"; //this is pinged for connectivity test

const char PROGMEM rdsUrl[] = R"deli(https://AWS_URL?tempBMP=%0.2f&pressBMP=%0.2f&tempAHT=%0.2f&humAHT=%0.2f)deli",   //URL that points to http endpoint of our lambda function in AWS with parameters
        openWeatherUrl[] = R"del(https://api.openweathermap.org/data/3.0/onecall?appid=%s&lat=%s&lon=%s&units=metric&exclude=minutely)del",   // OpenWeatherMap 3.0 API URL with parameters
        rdsApiKey[] = "API_KEY",                                       //API key for our lambda API in AWS
        openWeatherApiKey[] = "API_KEY";                                               //API key for Open Weather Map

//get and set the following from spotify console
const char PROGMEM CLIENT_ID[] = "SPOTIFY_CLIENT_ID", 
        CLIENT_SECRET[] = "SPOTIFY_CLIENT_SECRET",
        REDIRECT_URI[] = "SPOTIFY_REDIRECT_URI";

#endif