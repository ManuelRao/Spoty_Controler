#include <base64.h>
#include <ArduinoJson.h>
#include <stdint.h>
#include <stdio.h>

// Include WiFi and http client
#include <ESP8266WiFi.h> 
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
// HTTPS Server removed (standard HTTP is sufficient for this approach)
#include <WiFiClientSecureBearSSL.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <time.h> 

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// WIFI credentials
char WIFI_SSID[] = "CALCALZON_2.5G";
char PASSWORD[] = "LaSolucion233@";

// Spotify API credentials
#define CLIENT_ID "7dbaacd4aa4d4f668e6782175ef4e4b4"
#define CLIENT_SECRET "32ad905e011046e59976f91aaf5ba254"

// FIXED: Using Loopback address with port 8000 as requested.
// This allows the ESP IP to change without breaking Spotify Auth.
String REDIRECT_URI = "http://127.0.0.1:8000/callback";

#define codeVersion "1.2.0"
#define EEPROM_SIZE 4095

#if defined(ARDUINO) && ARDUINO >= 100
#define printByte(args)  write(args)
#else
#define printByte(args)  print(args,BYTE)
#endif

uint8_t play[8]  = {0x02,0x06,0x0E,0x1E,0x1E,0x0E,0x06,0x02};
uint8_t pause[8]  = {0x00,0x1B,0x1B,0x1B,0x1B,0x1B,0x1B,0x00};
uint8_t heart[8] = {0x00,0x00,0x0A,0x1F,0x1F,0x0E,0x04,0x00};
uint8_t heartE[8]  = {0x00,0x00,0x0A,0x15,0x11,0x0A,0x04,0x00};
uint8_t endF[8] = {0x00,0x08,0x08,0x1B,0x08,0x08,0x00,0x00};
uint8_t endL[8] = {0x00,0x02,0x0A,0x1E,0x0A,0x02,0x00,0x00};
uint8_t point[8] = {0x00,0x00,0x0E,0x1F,0x0E,0x00,0x00,0x00};
uint8_t starter[8] = {0x00,0x08,0x08,0x0F,0x08,0x08,0x00,0x00};

// REMOVED: Static IP configuration. Now uses DHCP.
  
LiquidCrystal_I2C lcd(0x3F,20,4);  

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

  if(ret_str.length() > 0 && *(ret_str.end()-1) == ','){
    ret_str = ret_str.substring(0,ret_str.length()-1);
  }
  return ret_str;
}

//http response struct
struct httpResponse{
    int responseCode;
    String responseMessage;
};

//song details struct
struct songDetails{
    int durationMs;
    String album;
    String artist;
    String song;
    String Id;
    bool isLiked;
};

//Create spotify connection class
class SpotConn {
public:
    SpotConn(){
        client = std::make_unique<BearSSL::WiFiClientSecure>();
        client->setInsecure(); // Secure Client for OUTGOING requests to Spotify
        // FIXED: Increased buffer size to fix error -5 (Connection Lost) during handshake
        // RX needs to be larger to receive the TLS certificate chain
        client->setBufferSizes(4096, 1024);
    }
    
    bool getUserCode(String serverCode) {
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
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, response);
            accessToken = String((const char*)doc["access_token"]);
            refreshToken = String((const char*)doc["refresh_token"]);
            tokenExpireTime = doc["expires_in"];
            tokenStartTime = millis();
            accessTokenSet = true;
            Serial.println(accessToken);
            Serial.println(refreshToken);
        }else{
            Serial.println(https.getString());
        }
        // Disconnect from the Spotify API
        https.end();
        return accessTokenSet;
    }
    bool refreshAuth(){
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
            String response = https.getString();
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, response);
            accessToken = String((const char*)doc["access_token"]);
            // refreshToken = doc["refresh_token"];
            tokenExpireTime = doc["expires_in"];
            tokenStartTime = millis();
            accessTokenSet = true;
            Serial.println(accessToken);
            Serial.println(refreshToken);
        }else{
            Serial.println(https.getString());
        }
        // Disconnect from the Spotify API
        https.end();
        return accessTokenSet;
    }


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
    bool getTrackInfo(){
        String url = "https://api.spotify.com/v1/me/player/currently-playing";
        https.useHTTP10(true);
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);
        int httpResponseCode = https.GET();
        bool success = false;
        String songId = "";
        bool refresh = false;
        // Check if the request was successful
        if (httpResponseCode == 200) {
                        
            String currentSongProgress = getValue(https,"progress_ms");
            currentSongPositionMs = currentSongProgress.toFloat();
            
            String albumName = getValue(https,"name");
            String artistName = getValue(https,"name");
            String songDuration = getValue(https,"duration_ms");
            currentSong.durationMs = songDuration.toInt();
            String songName = getValue(https,"name");
            songId = getValue(https,"uri");
            String isPlay = getValue(https, "is_playing");
            isPlaying = isPlay == "true";
            Serial.println(isPlay);
            
            if (songId.length() > 15) {
               songId = songId.substring(15,songId.length()-1); // Basic check to avoid crash
            }
            
            https.end();
            
            if (albumName.length() > 2) currentSong.album = albumName.substring(1,albumName.length()-1);
            if (artistName.length() > 2) currentSong.artist = artistName.substring(1,artistName.length()-1);
            if (songName.length() > 2) currentSong.song = songName.substring(1,songName.length()-1);
            currentSong.Id = songId;
            currentSong.isLiked = findLikedStatus(songId);
            success = true;
        } else {
            Serial.print("Error getting track info: ");
            Serial.println(httpResponseCode);
            https.end();
        }
        
        // Disconnect from the Spotify API
        if(success){
            lastSongPositionMs = currentSongPositionMs;
        }
        return success;
    }
    bool findLikedStatus(String songId){
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

        return success;
    }
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
            Serial.println(response);
            success = true;
        } else {
            Serial.print("Error toggling liked songs: ");
            Serial.println(httpResponseCode);
            String response = https.getString();
            Serial.println(response);
            https.end();
        }

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
        int httpResponseCode = https.DELETE();
        bool success = false;
        // Check if the request was successful
        if (httpResponseCode == 200) {
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

    bool accessTokenSet = false;
    long tokenStartTime;
    int tokenExpireTime;
    songDetails currentSong;
    float currentSongPositionMs;
    float lastSongPositionMs;
    int currVol;
    bool isPlaying = false;
  private:
    std::unique_ptr<BearSSL::WiFiClientSecure> client;
    HTTPClient https;
    String accessToken;
    String refreshToken;
};


WiFiUDP ntpUDP;
// FIXED: Reverted to Standard Web Server (Port 80)
ESP8266WebServer server(80);

SpotConn spotifyConnection;
NTPClient timeClient(ntpUDP, "ar.pool.ntp.org", -10800, 60000); // NTP client

String printEncryptionType(int thisType) {
  // read the encryption type and print out the name:
  switch (thisType) {
    case ENC_TYPE_WEP:
      return("WEP");
      break;
    case ENC_TYPE_TKIP:
      return("WPA");
      break;
    case ENC_TYPE_CCMP:
      return("WPA2");
      break;
    case ENC_TYPE_NONE:
      return("None");
      break;
    case ENC_TYPE_AUTO:
      return("Auto");
      break;
    default:
      return "Unknown";
      break;
  }
}

class LCDmanager { // the one responsable for managing all graphical intefrace
public:
  LCDmanager(){};

  int nameScroll = 0;
  int lastScroll = 0;
  String lastSong = "";
  bool scrolable;

  void drawStart(){ // draws the start up page
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WIRELESS CONTROLLER"); // project name
    lcd.setCursor(0, 1);
    lcd.print(String("version ") + String(codeVersion)); // shows version
    lcd.setCursor(0, 2);
    lcd.print("by: Manuel Rao"); // shows author
  }

  void drawSpotifyConection(){ // draws the spotify setpu page
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Spotify Setup");
    lcd.setCursor(0, 1);
    lcd.print("Connect to IP:");
    lcd.setCursor(0, 2);
    lcd.print(WiFi.localIP()); 
    lcd.setCursor(0, 3);
    lcd.print("For setup");
  }

  void waitForDevice(){ // draws the waiting for device page
    lcd.clear();
    lcd.setCursor(15, 0);
    timeClient.update();
    lcd.print(timeClient.getFormattedTime().substring(0,5));
    lcd.setCursor(0,1);
    lcd.print("waiting for device");
  }


  void showNets(int numNets, int selected){ // it shows all networks
    // Fixed: Cannot use String arrays dynamically like this in C++. 
    // We access the WiFi struct directly.
    
    int showNum = int(selected/2);
    
    // We don't need a local array IDs[], we just use WiFi.SSID(i)

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Networks - " + String(numNets)); // menu name and number of networks
    lcd.setCursor(0, 1);
    // Print first network
    String id1 = WiFi.SSID(showNum);
    lcd.print(String(((selected % 2) == 0)? "- " : "->") + id1); 
    lcd.setCursor(0, 2);
    // Print second network if available
    if (showNum + 1 < numNets) {
       String id2 = WiFi.SSID(showNum + 1);
       lcd.print(String(((selected % 2) != 0)? "- " : "->") + id2); 
    }
    
    lcd.setCursor(0, 3);
    lcd.print("            < E > S"); // shows action bar
  }

  void netMenu(int selected, int mode){ // individual network menu
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(WiFi.SSID(selected)); // Net name
    lcd.setCursor(0, 1);
    
    int encType = WiFi.encryptionType(selected);
    String enc = printEncryptionType(encType);
    
    lcd.print("Encryption: " + enc); // encryption type
    lcd.setCursor(0, 2);
    // decision tree for action menu
    
    // FIXED: C++ cannot switch on Strings. Switched to int/enum logic.
    switch (encType){

    case ENC_TYPE_NONE:
        switch (mode){
        case 0:
            lcd.print("connect"); // connect
            break;
        case 1:
            lcd.print("RSSI" + String(WiFi.RSSI(selected)));
            break;
        default:
            break;}
        break;
    
    case ENC_TYPE_WEP:
        switch (mode){
        case 0:
            lcd.print("connect"); // connect
            break;
        case 1:
            lcd.print("RSSI" + String(WiFi.RSSI(selected)));
            break;
        case 2:
            lcd.print("Key Index: ");
            break;
        case 3: 
            lcd.print("Password");
            break;
        default:
            break;}
        break;
    case ENC_TYPE_TKIP: // WPA
        switch (mode){
        case 0:
            lcd.print("connect"); // connect
            break;
        case 1:
             lcd.print("RSSI" + String(WiFi.RSSI(selected)));
            break;
        case 2:
            lcd.print("password");
            break;
        default:
            break;}
        break; 
    case ENC_TYPE_CCMP: // WPA2
        switch (mode){
        case 0:
            lcd.print("connect"); // connect
            break;
        case 1:
            lcd.print("RSSI" + String(WiFi.RSSI(selected))); // signal strength
            break;
        case 2:
            lcd.print("password"); 
            break;
        default:
            break;}
        break;    
            
    case ENC_TYPE_AUTO:
        switch (mode){
        case 0:
            lcd.print("connect"); // connect
            break;
        case 1:
            lcd.print("RSSI" + String(WiFi.RSSI(selected))); // signal strength
            break;
        case 2:
            lcd.print("password"); 
            break;
        default:
            break;}
        break;
    default:
        break;
    }
    lcd.setCursor(0, 3);
    lcd.print("            < E > Q"); // shows action bar
  }

  void drawKeyboard(String menuText, String writenText, char leter, int selected, bool writing){
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("- " + menuText);
    String writeLine = "";
    scrolable = (writenText.length() >= 18) ? true : false;
    if (scrolable){ // prepares the text to be shown 
        if (scrolable){
            writeLine = writenText.substring(selected-18, selected) + leter;
        } else if(writing){
            writeLine = writenText.substring(0, selected) + leter + writenText.substring(selected+1, 20);
        }
    }
    lcd.setCursor(0, 1);
    lcd.print(writeLine); // writes the actual text
    lcd.setCursor(0, 2);
    String selector = (writing) ? ">-<" : "<->"; // select which cursor to use
    if(selected >= 18){ // print blank spaces to place the cursor in the apropiate space
        for(int i = 0; i<17; i++){
            lcd.print(" ");
        }
    } else {
        for(int i = 0; i<selected-1; i++){
            lcd.print(" ");
        }
    }
    lcd.print(selector);
    lcd.setCursor(0, 3);
    lcd.print(String((writing)? "            < S > U" : "            < W > S")); //menu action bar
  }

  void drawMusic(){ // draws the main music screen

    lcd.clear();
    lcd.setCursor(15, 0);
    timeClient.update(); // shows the time
    
    // UPDATED: Used timeClient.getHours() instead of hour() from TimeLib
    int h = timeClient.getHours();
    int m = timeClient.getMinutes();
    String ti = String((h < 10)? "0"+String(h) : String(h))+":"+String((m < 10)? "0"+String(m) : String(m));
    lcd.print(ti);

    // shows song name
    lcd.setCursor(0,1);
    int nameLen = spotifyConnection.currentSong.song.length();
    if(lastSong != spotifyConnection.currentSong.song){
      if(nameLen > 20){
        scrolable = true;
        nameScroll = 0;
      }else{
        scrolable = false;
      }
      lastSong = spotifyConnection.currentSong.song;
    }

    //scrolling
    String displayName = spotifyConnection.currentSong.song + "-("+spotifyConnection.currentSong.artist+")";
    if(scrolable){
      lcd.print(displayName.substring(nameScroll, nameScroll+20));
      int steps = int((millis()-LastUpdate)/1500);
      if(nameScroll + 20 == nameLen){nameScroll = 0;}else if(nameScroll + 20 + steps <= nameLen){nameScroll += steps;}else{nameScroll++;}
      LastUpdate = millis();
    }else{
      lcd.print(spotifyConnection.currentSong.song);
    }

    //progress bar
    lcd.setCursor(0, 2);
    float progress = float(spotifyConnection.currentSongPositionMs)/float(spotifyConnection.currentSong.durationMs);
    Serial.println(spotifyConnection.currentSongPositionMs);
    Serial.println(spotifyConnection.currentSong.durationMs);
    Serial.println(progress);
    int bars = floor(progress*18)-1;
    Serial.println(bars);
    bool l_finisher = (int(round(progress*36))%2==1)? true : false;
    lcd.printByte(2);
    for(int i = 0; i <= bars; i ++){
      lcd.printByte(7);
    }
    lcd.printByte((l_finisher)? 5 : 6);
    String bar = "";
    for(int i = 0; i < (16 - bars); i ++){
      bar = bar + "-";
    }
    lcd.print(bar);
    lcd.printByte(6);

    // show progress time and total song length
    lcd.setCursor(0, 3);
    int progressMinsIn = floor(spotifyConnection.currentSongPositionMs/60000);
    String progressMins = String((progressMinsIn < 10) ? "0" + String(progressMinsIn) : String(progressMinsIn));
    int progressSecIn = floor(spotifyConnection.currentSongPositionMs/1000) - progressMinsIn*60;
    String progressSec = String((progressSecIn < 10) ? "0" + String(progressSecIn) : String(progressSecIn));
    lcd.print(progressMins + ":" + progressSec + "/");
    int durationMinsIn = floor(spotifyConnection.currentSong.durationMs/60000);
    String durationMins = String((durationMinsIn < 10) ? "0" + String(durationMinsIn) : String(durationMinsIn));
    int durationSecIn = floor(spotifyConnection.currentSong.durationMs/1000) - durationMinsIn*60;
    String durationSec = String((durationSecIn < 10) ? "0" + String(durationSecIn) : String(durationSecIn));
    lcd.print(durationMins + ":" + durationSec);
    lcd.print("  < "); //backtrack
    spotifyConnection.isPlaying ? lcd.printByte(0) : lcd.printByte(1); //pause/play button
    lcd.print(" > "); //skip button
    spotifyConnection.currentSong.isLiked ? lcd.printByte(3) : lcd.printByte(4);
  }

  int LastUpdate = 0;
};

bool buttonStates[] = {1,1,1,1};
int debounceDelay = 20;
unsigned long debounceTimes[] = {0,0,0,0};
int buttonPins[] = {D5,D6,D7,D8};
int oldvolume = 100;
int volume = 100;

long timeLoop;
long refreshLoop;
bool serverOn = true;

LCDmanager LCDm;

//web pages
// FIXED: HTML now split into PROGMEM chunks to avoid giant stack allocation or snprintf buffer overflows.
// This is the "Chunked Send" method which is extremely memory efficient on ESP8266.

const char mainPagePart1[] PROGMEM = R"=====(
<HTML>
    <HEAD>
        <TITLE>Spotify Controller</TITLE>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <style>
            body { font-family: sans-serif; padding: 20px; text-align: center; }
            input[type=text] { width: 100%; padding: 10px; margin: 10px 0; }
            input[type=submit] { padding: 10px 20px; background: #1DB954; color: white; border: none; border-radius: 5px; cursor: pointer; }
            .step { margin-bottom: 20px; text-align: left; background: #f0f0f0; padding: 15px; border-radius: 8px; }
        </style>
    </HEAD>
    <BODY>
        <CENTER>
            <H2>Spotify Setup</H2>
            
            <div class="step">
                <b>Step 1:</b><br>
                Click the link below. Log in to Spotify. <br>
                <i>Note: The page will fail to load (localhost refused/broken). This is expected!</i>
                <br><br>
                <a href="https://accounts.spotify.com/authorize?response_type=code&client_id=)=====";

// Note: Between Part 1 and Part 2, we will inject the Client ID, Redirect URI, etc.

const char mainPagePart2[] PROGMEM = R"=====(
&scope=user-modify-playback-state user-read-currently-playing user-read-playback-state user-library-modify user-library-read" target="_blank">1. Log in to Spotify</a>
            </div>

            <div class="step">
                <b>Step 2:</b><br>
                Look at your browser's address bar on the broken page.<br>
                Copy the code after <b>?code=</b>
            </div>

            <div class="step">
                <b>Step 3:</b><br>
                Paste the code below and click Submit:
                <form action="/callback" method="get">
                    <input type="text" name="code" placeholder="Paste code here...">
                    <br>
                    <input type="submit" value="Submit Code">
                </form>
            </div>
        </CENTER>
    </BODY>
</HTML>
)=====";

const char errorPage[] PROGMEM = R"=====(
<HTML>
    <HEAD>
        <TITLE>Error</TITLE>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
    </HEAD>
    <BODY>
        <CENTER>
            <H2>Error</H2>
            <p>Code invalid or missing.</p>
            <a href="/">Go Back</a>
        </CENTER>
    </BODY>
</HTML>
)=====";

void handleRoot() { // handless HTTP main server for spotify auth
    Serial.println("handling root");
    
    // Chunked sending to save memory
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", ""); // Send headers only
    
    // Send Part 1
    server.sendContent_P(mainPagePart1);
    
    // Send Dynamic Link Parts
    server.sendContent(CLIENT_ID);
    server.sendContent("&redirect_uri=");
    server.sendContent(REDIRECT_URI);
    
    // Send Part 2
    server.sendContent_P(mainPagePart2);
    
    // Terminate the chunked connection
    server.sendContent("");
}

void handleCallbackPage() { // handless call back page but it can also act as root for auth
    if(!spotifyConnection.accessTokenSet){
        if (server.arg("code") == ""){     //Parameter not found
            server.send(200, "text/html", errorPage); // Send error web page directly from PROGMEM
        }else{     //Parameter found
            if(spotifyConnection.getUserCode(server.arg("code"))){ // send auth complete web page
                server.send(200,"text/html","Spotify setup complete Auth refresh in :"+String(spotifyConnection.tokenExpireTime));
            }else{
                server.send(200, "text/html", errorPage); // Send error web page directly from PROGMEM
            }
        }
    }else{
        server.send(200,"text/html","Spotify setup complete"); // send spotify setup already complete page
    }
}

// pin manager, its called whenever a button is pressed(DO NOT TOUCH UNLES YOU KNOW WHAT UR DOING)
volatile int pinCalled;
volatile bool call = false;
void ICACHE_RAM_ATTR pinManager(){
  call = true;
  for(int i = 0; i < 5; i ++){
    int reading = digitalRead(buttonPins[i]);
    if(reading == true && i != 4){  
      switch (i){
      case 0:
          pinCalled = 0;
          break;
      case 1:
          pinCalled = 1;
          break;
      case 2:
          pinCalled = 2;
          break;
      case 3:
          pinCalled = 3;
          break;
      
      default:
          break;
      }
    }
  }
}

void netMenu(){
    int numNets = WiFi.scanNetworks();
    int selected = 0;
    bool selectedSet = false;
    bool q = false;
    bool f = false;
    LCDm.showNets(numNets, selected);
    while(!f){
        q = true; // Fixed missing semicolon
        while(!selectedSet and !q){
            if(call){
                switch (pinCalled){
                    case 0:
                        selected = (selected == 0) ? numNets - 1 : selected - 1;
                        LCDm.showNets(numNets, selected);
                        break;
                    case 1:
                        selectedSet = true;
                        LCDm.netMenu(selected, 0); // Fixed argument count (need mode)
                        break;
                    case 2:
                        selected = (selected == numNets - 1) ? 0 : selected + 1;
                        LCDm.showNets(numNets, selected);
                        break;
                    case 3:
                        q = true;
                        break;
                    default:
                        break;
                }
                call = false;
            }
        }

        while(selectedSet){
            if(call){
                switch (pinCalled){
                    case 0:
                        
                        break;
                    case 1:
                        
                        break;
                    case 2:
                        
                        break;
                    case 3:
                        selectedSet = false;
                        break;
                    default:
                        break;
                }
                call = false;
            }
        }
    }
}
void manageWifiConnection(){ // manages all wifi connection setup to be able to connect to any network type 
    //wifi selection mode
    netMenu();
    

}

unsigned long lastNTPUpdate = 0;
const unsigned long NTP_UPDATE_INTERVAL = 600000; // 10 minutes 

// Helper function to debug time
void printTime() {
    Serial.println(timeClient.getFormattedTime());
}

void updateRTCTime() {
  // Update the RTC with the NTP time
  timeClient.update();
  
  // Removed setTime(...) as we aren't using TimeLib.
  // We just rely on timeClient to hold the correct time.

  Serial.print("NTP Time: ");
  printTime();
  Serial.println();
}

void setup(){
  Serial.begin(115200);

  lcd.init();    // initialize the lcd                  
  lcd.backlight();
  // load in the custom chars
  lcd.createChar(0, pause);
  lcd.createChar(1, play);
  lcd.createChar(2, starter);
  lcd.createChar(3, heart);
  lcd.createChar(4, heartE);
  lcd.createChar(5, endF);
  lcd.createChar(6, endL);
  lcd.createChar(7, point);
  lcd.home();
  LCDm.drawStart();



  lcd.setCursor(0, 3);
  lcd.print("connecting to WiFi");
  // REMOVED WiFi.config to allow DHCP (Dynamic IP)
  
  WiFi.begin(WIFI_SSID, PASSWORD); // connect to wifi
  int attempt = 0;

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.println("Conecting to WiFi...");
      lcd.setCursor(19, 3);
      lcd.print(attempt);
      attempt++;

  }

  lcd.setCursor(0, 3);
  lcd.print("connected to WiFi   ");
  Serial.println("Connected to WiFi\n Ip is: ");
  Serial.println(WiFi.localIP());
  // The redirect URI is now fixed to localhost, so we don't need to append IP here
  // REDIRECT_URI = "http://127.0.0.1:8000/callback"; 
  delay(500);
  timeClient.begin(); // initialize the time client
  updateRTCTime(); // Fixed missing semicolon
  lcd.setCursor(0, 3);
  lcd.print("connected to NTP    ");

  server.on("/", handleRoot);                  //Which routine to handle at root location
  server.on("/callback", handleCallbackPage);      //Which routine to handle at root location
  server.begin();                              //Start server
  Serial.println("HTTP server started");           // loging
  lcd.setCursor(0, 3);
  lcd.print("HTTPS server Started");

  for(int i = 0; i < 4; i ++){ // set pin modes and attach interrupts
    pinMode(buttonPins[i], INPUT);
    attachInterrupt(digitalPinToInterrupt(buttonPins[i]), pinManager, RISING);
  }

  delay(250);
  lcd.setCursor(0, 3);
  lcd.print("Start sequence DONE "); // finish message
  delay(500);
  LCDm.drawSpotifyConection(); // wait for spotify auth

}

// int mode = 0; // Mode global variable conflicts with local arguments, better to keep it managed

void loop(){  // main loop

  if (millis() - lastNTPUpdate >= NTP_UPDATE_INTERVAL) { // update RTC time every X minutes
    updateRTCTime();
    lastNTPUpdate = millis();
  }

  if(spotifyConnection.accessTokenSet){
    if(serverOn){ // close server 
        server.close();
        serverOn = false;
    }

    // check if spotify auth token needs to be refreshed
    if((millis() - spotifyConnection.tokenStartTime)/1000 > spotifyConnection.tokenExpireTime){
        Serial.println("refreshing token");
        if(spotifyConnection.refreshAuth()){
            Serial.println("refreshed token");
        }
    }

    if(spotifyConnection.getTrackInfo()){ // gets current playing from spotify
      LCDm.drawMusic();
    }else if(spotifyConnection.currentSong.song == NULL){ // nothing playing
      LCDm.waitForDevice(); 
    }

    if(call){ // manage user interactions
      switch (pinCalled){
        case 0:
            spotifyConnection.skipBack();
            break;
        case 1:
            spotifyConnection.togglePlay();
            break;
        case 2:
            spotifyConnection.skipForward();
            break;
        case 3:
            spotifyConnection.toggleLiked(spotifyConnection.currentSong.Id); // need to change this to cofig menu
            break;
        default:
            break;
      }
      call = false;
    }
    Serial.println(analogRead(int(A0)));
    int volRequest = map(analogRead(A0),0,1023,0,100);
    if(abs(volRequest - spotifyConnection.currVol) > 2){
        spotifyConnection.adjustVolume(volRequest);
    } 
    timeLoop = millis();
  }else{
      server.handleClient();
  }
}