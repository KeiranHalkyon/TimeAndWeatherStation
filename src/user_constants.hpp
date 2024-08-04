#ifndef USER_CONSTANTS
#define USER_CONSTANTS

const char PROGMEM ssid[] = "SSID",
        password[] = "PASS",
        remote_host[] = "www.google.com"; //this is pinged for connectivity test

const char PROGMEM rdsUrl[] = R"deli(AWS_API_ENDPOINT?tempBMP=%0.2f&pressBMP=%0.2f&tempAHT=%0.2f&humAHT=%0.2f)deli",   //URL that points to http endpoint of our lambda function in AWS with parameters
        openWeatherDailyForecastUrl[] = R"deli(https://api.openweathermap.org/data/3.0/onecall?appid=%s&lat=%s&lon=%s&units=metric&exclude=minutely,current,hourly)deli",   //URL for daily forecast
        openWeatherCurrentUrl[] = R"deli(https://api.openweathermap.org/data/2.5/weather?appid=%s&lat=%s&lon=%s&units=metric)deli",     //URL for current weather data
        openWeather3HrForecastUrl[] = R"deli(https://api.openweathermap.org/data/2.5/forecast?appid=%s&lat=%s&lon=%s&units=metric&cnt=%d)deli",                       //URL for 3 hr forecasts
        rdsApiKey[] = "RDS_API_KEY",                                       //API key for our lambda API in AWS
        openWeatherApiKey[] = "OWM_API_KEY",                               //API key for Open Weather Map
        lat[] = "LATITUDE",              //Our latitude
        lon[] = "LONGITUDE",              //Our longitude
        pcServer[] = "PC_SERVER_URL";    //Our python pc data server

//get and set the following from spotify console
const char PROGMEM CLIENT_ID[] = "SPOTIFY_CLIENT_ID", 
        CLIENT_SECRET[] = "SPOTIFY_CLIENT_SECRET",
        REDIRECT_URI[] = "http://esp8266.local/callback";

#endif