#ifndef USER_CONSTANTS
#define USER_CONSTANTS

const char *ssid = "WIFI_SSID",
        *password = "WIFI_PASSWORD",
        *remote_host = "www.google.com"; //this is pinged for connectivity test

const String rdsUrl = "URL",               //URL that points to http endpoint of our lambda function in AWS
        openWeatherUrl = "URL",                                                //Initial permanent part of the Open Weather Map API URL
        rdsApiKey = "API_KEY",                                                 //API key for our lambda API in AWS
        openWeatherApiKey = "API_KEY";                                                 //API key for Open Weather Map

#endif