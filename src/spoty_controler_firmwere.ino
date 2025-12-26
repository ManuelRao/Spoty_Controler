#include <base64.h>
#include <ArduinoJson.h>
#include <stdint.h>
#include <stdio.h>

// Include WiFi and http client
#include <ESP8266WiFi.h> 
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <time.h> 
#include <EEPROM.h>
#include <AsyncHTTPRequest_Generic.h>

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// WIFI credentials
char WIFI_SSID[] = "CALCALZON_2.5G";
char PASSWORD[] = "LaSolucion233@";

// Spotify API credentials
#define CLIENT_ID "7dbaacd4aa4d4f668e6782175ef4e4b4"
#define CLIENT_SECRET "32ad905e011046e59976f91aaf5ba254"

String REDIRECT_URI = "http://127.0.0.1:8000/callback";

#define codeVersion "1.3.1"
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

LiquidCrystal_I2C lcd(0x3F,20,4);  

// --- ADDED: Storage Structure ---
struct SpotifyData {
  char refreshToken[512]; // Buffer for the long refresh token
  unsigned long timestamp; // Network time
  uint8_t magic; // Verification byte (0x42)
};

// REMOVED: Broken getValue function

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
        client->setInsecure(); 
        client->setBufferSizes(8192, 1024); // Increased buffer for better SSL handling

        // Initialize async requests with optimal settings for ESP8266
        trackInfoRequest.setDebug(false);
        trackInfoRequest.setTimeout(15); // Longer timeout for SSL handshake
        
        likedStatusRequest.setDebug(false);
        likedStatusRequest.setTimeout(10); 
        
        // Don't set callbacks here - need to wait until spotifyConnection exists
    }

    // Initialize callbacks after spotifyConnection is created
    void initializeCallbacks() {
        trackInfoRequest.onReadyStateChange(trackInfoCallback);
        likedStatusRequest.onReadyStateChange(likedStatusCallback);
    }

    String refreshToken; 
    
    // Async state management
    unsigned long lastTrackInfoRequest = 0;
    unsigned long lastLikedStatusRequest = 0;
    const unsigned long TRACK_INFO_INTERVAL = 2000;
    bool trackInfoReady = true;
    bool likedStatusReady = true;
    String pendingSongId = "";
    int asyncFailureCount = 0;
    bool useAsyncRequests = true;
    unsigned long lastUpdate = 0;
    void update() {
    
        unsigned long currentTime = millis();
        if(isPlaying){
          currentSongPositionMs += (currentTime - lastUpdate);
        }
        lastUpdate = currentTime;

        // Check WiFi connection health for async requests
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("⚠ WiFi disconnected, waiting for reconnection...");
            return;
        }
        
        // Send track info request if ready and time has passed
        if (accessTokenSet && trackInfoReady && (currentTime - lastTrackInfoRequest >= TRACK_INFO_INTERVAL)) {
            if (useAsyncRequests) {
                sendTrackInfoRequest();
            } else {
                getTrackInfoSync();
            }
        }

        // Send liked status request if ready and song ID pending (only if async is working)
        if (useAsyncRequests && likedStatusReady && pendingSongId.length() > 0) {
            sendLikedStatusRequest();
        }        
    }

    void sendTrackInfoRequest() {
        if (!trackInfoReady) return;
        
        // Ensure request is properly reset before reuse
        if (trackInfoRequest.readyState() != 0) {
            Serial.println("Resetting trackInfoRequest");
            // Give it a moment to clean up
            yield();
        }
        
        String url = "https://api.spotify.com/v1/me/player/currently-playing";
        
        Serial.print("Opening async request to: ");
        Serial.println(url);
        
        bool opened = trackInfoRequest.open("GET", url.c_str());
        if (opened) {
            String auth = "Bearer " + accessToken;
            trackInfoRequest.setReqHeader("Authorization", auth.c_str());
            trackInfoRequest.setReqHeader("User-Agent", "ESP8266-SpotifyController/1.3");
            trackInfoRequest.setReqHeader("Accept", "application/json");
            trackInfoRequest.setReqHeader("Cache-Control", "no-cache");
            
            if (trackInfoRequest.send()) {
                trackInfoReady = false;
                lastTrackInfoRequest = millis();
                Serial.println("✓ Track info request sent successfully");
            } else {
                Serial.println("✗ Failed to send track info request");
                asyncFailureCount++;
                trackInfoReady = true; // Reset for retry
            }
        } else {
            asyncFailureCount++;
            Serial.print("✗ Failed to open track info request (failure #");
            Serial.print(asyncFailureCount);
            Serial.println(")");
            
            if (asyncFailureCount >= 5) {
                Serial.println("⚠ Too many async failures, switching to sync mode");
                useAsyncRequests = false;
                getTrackInfoSync();
            } else {
                Serial.println("Retrying in next cycle...");
                trackInfoReady = true; // Allow retry
            }
        }
    }

    void sendLikedStatusRequest() {
        if (!likedStatusReady || pendingSongId.length() == 0) return;
        
        String songId = pendingSongId; // Save before clearing
        pendingSongId = ""; // Clear pending ID immediately
        
        // Ensure request is properly reset before reuse
        if (likedStatusRequest.readyState() != 0) {
            Serial.println("Resetting likedStatusRequest");
            yield();
        }
        
        String url = "https://api.spotify.com/v1/me/tracks/contains?ids=" + songId;
        
        Serial.print("Opening liked status request for song: ");
        Serial.println(songId);
        
        bool opened = likedStatusRequest.open("GET", url.c_str());
        if (opened) {
            String auth = "Bearer " + accessToken;
            likedStatusRequest.setReqHeader("Authorization", auth.c_str());
            likedStatusRequest.setReqHeader("Content-Type", "application/json");
            likedStatusRequest.setReqHeader("User-Agent", "ESP8266-SpotifyController/1.3");
            likedStatusRequest.setReqHeader("Cache-Control", "no-cache");
            
            if (likedStatusRequest.send()) {
                likedStatusReady = false;
                lastLikedStatusRequest = millis();
                Serial.println("✓ Liked status request sent successfully");
            } else {
                Serial.println("✗ Failed to send liked status request");
                likedStatusReady = true; // Reset for next attempt
            }
        } else {
            Serial.println("✗ Failed to open liked status request - skipping");
            likedStatusReady = true; // Reset for next attempt
        }
    }

    // Synchronous fallback method for when async fails
    void getTrackInfoSync() {
        if (!accessTokenSet) return;
        
        Serial.println("Using sync track info request");
        String url = "https://api.spotify.com/v1/me/player/currently-playing";
        https.setTimeout(3000);
        https.begin(*client, url);
        String auth = "Bearer " + accessToken;
        https.addHeader("Authorization", auth);
        https.addHeader("User-Agent", "ESP8266-SpotifyController");
        
        int httpResponseCode = https.GET();
        
        if (httpResponseCode == 200) {
            String response = https.getString();
            processTrackInfoJson(response);
            Serial.println("Sync track info updated");
        } else if (httpResponseCode == 204) {
            Serial.println("Nothing playing (sync)");
        } else {
            Serial.print("Sync track info error: ");
            Serial.println(httpResponseCode);
        }
        
        https.end();
        trackInfoReady = true;
        lastTrackInfoRequest = millis();
        lastUpdateTimeStamp = millis();
    }

    // Method to reset async system and try again
    void resetAsyncSystem() {
        Serial.println("🔄 Resetting async system...");
        asyncFailureCount = 0;
        useAsyncRequests = true;
        trackInfoReady = true;
        likedStatusReady = true;
        pendingSongId = "";
        
        // Reinitialize the requests
        trackInfoRequest.setTimeout(15);
        likedStatusRequest.setTimeout(10);
        
        Serial.println("✓ Async system reset complete");
    }

    // Static callback functions - forward declarations only
    static void trackInfoCallback(void* optParm, AsyncHTTPRequest* request, int readyState);
    static void likedStatusCallback(void* optParm, AsyncHTTPRequest* request, int readyState);

    void processTrackInfoJson(const String& jsonString) {
        JsonDocument filter;
        filter["progress_ms"] = true;
        filter["is_playing"] = true;
        filter["item"]["name"] = true;
        filter["item"]["duration_ms"] = true;
        filter["item"]["uri"] = true;
        filter["item"]["album"]["name"] = true;
        filter["item"]["artists"][0]["name"] = true;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonString, DeserializationOption::Filter(filter));

        if (!error) {
            currentSongPositionMs = doc["progress_ms"];
            currentSong.durationMs = doc["item"]["duration_ms"];
            currentSong.song = doc["item"]["name"].as<String>();
            currentSong.album = doc["item"]["album"]["name"].as<String>();
            currentSong.artist = doc["item"]["artists"][0]["name"].as<String>();
            
            String songUri = doc["item"]["uri"].as<String>();
            if (songUri.length() > 14) {
                currentSong.Id = songUri.substring(14);
            } else {
                currentSong.Id = songUri;
            }

            isPlaying = doc["is_playing"];
            
            // Queue liked status check if song changed
            if (currentSong.Id.length() > 0 && currentSong.Id != currentSong.Id) {
                pendingSongId = currentSong.Id;
            }
            
            lastSongPositionMs = currentSongPositionMs;
        } else {
            Serial.print("JSON parse error: ");
            Serial.println(error.c_str());
        }
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
            JsonDocument doc;
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
        // Ensure we have a token to refresh
        if (refreshToken == "") return false;

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
            JsonDocument doc;
            deserializeJson(doc, response);
            accessToken = String((const char*)doc["access_token"]);
            // Note: Sometimes spotify returns a NEW refresh token, sometimes not.
            if (doc["refresh_token"].is<const char*>()) {
               refreshToken = String((const char*)doc["refresh_token"]);
            }
            tokenExpireTime = doc["expires_in"];
            tokenStartTime = millis();
            accessTokenSet = true;
            Serial.println("Token Refreshed Successfully");
        }else{
            Serial.println("Refresh Failed:");
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
        update();
        return success;
    }

    /*
    bool getTrackInfo(){
        long startTime = millis();
        String url = "https://api.spotify.com/v1/me/player/currently-playing";
        https.useHTTP10(true);
        https.begin(*client,url);
        String auth = "Bearer " + String(accessToken);
        https.addHeader("Authorization",auth);

        int httpResponseCode = https.GET();
        bool success = false;

        if (httpResponseCode == 200) {
            StaticJsonDocument<512> filter;
            filter["progress_ms"] = true;
            filter["is_playing"] = true;
            filter["item"]["name"] = true;
            filter["item"]["duration_ms"] = true;
            filter["item"]["uri"] = true;
            filter["item"]["album"]["name"] = true;
            filter["item"]["artists"][0]["name"] = true;

            DynamicJsonDocument doc(4096);

            DeserializationError error = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));

            if (!error) {
                currentSongPositionMs = doc["progress_ms"];
                currentSong.durationMs = doc["item"]["duration_ms"];
                currentSong.song = doc["item"]["name"].as<String>();
                currentSong.album = doc["item"]["album"]["name"].as<String>();
                currentSong.artist = doc["item"]["artists"][0]["name"].as<String>();
                
                String songUri = doc["item"]["uri"].as<String>();
                // Convert spotify:track:ID to just ID
                if (songUri.length() > 14) {
                    currentSong.Id = songUri.substring(14);
                } else {
                    currentSong.Id = songUri;
                }

                isPlaying = doc["is_playing"];                
                https.end();

                // Fetch liked status
                if (currentSong.Id.length() > 0) {
                    currentSong.isLiked = findLikedStatus(currentSong.Id);
                }
                success = true;
            } else {
                Serial.print("deserializeJson() failed: ");
                Serial.println(error.c_str());
                https.end();
            }
        } else {
            Serial.print("Error getting track info: ");
            Serial.println(httpResponseCode);
            https.end();
        }
        
        if(success){
            lastSongPositionMs = currentSongPositionMs;
        }
        lastUpdateTimeStamp = millis();
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
    */

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
        update();
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
        update();
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
    unsigned long lastUpdateTimeStamp; 

  private:
    std::unique_ptr<BearSSL::WiFiClientSecure> client;
    HTTPClient https;
    String accessToken;

    AsyncHTTPRequest trackInfoRequest;
    AsyncHTTPRequest likedStatusRequest;
};


WiFiUDP ntpUDP;
ESP8266WebServer server(80);

SpotConn spotifyConnection;
NTPClient timeClient(ntpUDP, "ar.pool.ntp.org", -10800, 60000); // NTP client

// Implementation of async callback functions
void SpotConn::trackInfoCallback(void* optParm, AsyncHTTPRequest* request, int readyState) {
    if (readyState == readyStateDone) {
        spotifyConnection.trackInfoReady = true;
        int httpCode = request->responseHTTPcode();
        
        Serial.print("Track info callback - HTTP ");
        Serial.print(httpCode);
        Serial.print(" - ");
        
        if (httpCode == 200) {
            String response = request->responseText();
            if (response.length() > 0) {
                spotifyConnection.processTrackInfoJson(response);
                Serial.println("✓ Track info updated successfully");
                // Reset failure count on success
                spotifyConnection.asyncFailureCount = 0;
                if (!spotifyConnection.useAsyncRequests) {
                    Serial.println("✓ Async working again, re-enabling");
                    spotifyConnection.useAsyncRequests = true;
                }
            } else {
                Serial.println("✗ Empty response received");
            }
        } else if (httpCode == 204) {
            Serial.println("✓ Nothing playing");
            // Reset failure count on success (even if no content)
            spotifyConnection.asyncFailureCount = 0;
            if (!spotifyConnection.useAsyncRequests) {
                Serial.println("✓ Async working again, re-enabling");
                spotifyConnection.useAsyncRequests = true;
            }
        } else if (httpCode == 401) {
            Serial.println("✗ Unauthorized - token may be expired");
        } else if (httpCode == -1) {
            Serial.println("✗ Connection failed");
            spotifyConnection.asyncFailureCount++;
        } else {
            Serial.print("✗ Error code: ");
            Serial.println(httpCode);
            if (httpCode < 0) spotifyConnection.asyncFailureCount++;
        }
        
        spotifyConnection.lastUpdateTimeStamp = millis();
    }
}

void SpotConn::likedStatusCallback(void* optParm, AsyncHTTPRequest* request, int readyState) {
    if (readyState == readyStateDone) {
        spotifyConnection.likedStatusReady = true;
        int httpCode = request->responseHTTPcode();
        
        Serial.print("Liked status callback - HTTP ");
        Serial.print(httpCode);
        Serial.print(" - ");
        
        if (httpCode == 200) {
            String response = request->responseText();
            response.trim();
            spotifyConnection.currentSong.isLiked = (response == "[ true ]");
            Serial.print("✓ Liked status: ");
            Serial.println(spotifyConnection.currentSong.isLiked ? "♥ LIKED" : "♡ not liked");
        } else if (httpCode == 401) {
            Serial.println("✗ Unauthorized - token may be expired");
        } else if (httpCode == -1) {
            Serial.println("✗ Connection failed");
        } else {
            Serial.print("✗ Error code: ");
            Serial.println(httpCode);
        }
    }
}

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

  /*
      ------------------------------------------------------
      LCD manager class for handling the graphical interface
      ------------------------------------------------------
  */

class LCDmanager { // the one responsable for managing all graphical intefrace
public:
  LCDmanager(){};

  int nameScroll = 0;
  int lastScroll = 0;
  String lastSong = "";
  bool scrolable;

  // Screen buffer to track current display content
  String currentScreen = "";
  int screenWidth = 20;
  int screenHeight = 4;

  // Initialize screen with dimensions and clear display
  void initScreen(int width, int height) {
    screenWidth = width;
    screenHeight = height;
    
    // Clear physical display
    lcd.clear();
    
    // Initialize screen buffer with spaces
    currentScreen = "";
    for (int i = 0; i < screenHeight * screenWidth; i++) {
      currentScreen += " ";
    }
    
    Serial.print("LCD initialized: ");
    Serial.print(width);
    Serial.print("x");
    Serial.println(height);
  }

  void LCDdraw(int cursorx, int cursory, String text){ // draws only new text
    // Boundary checks
    if (cursory >= screenHeight || cursorx >= screenWidth || cursorx < 0 || cursory < 0) {
      return; // Out of bounds
    }
    
    // Calculate starting position in screen buffer
    int startPos = (cursory * screenWidth) + cursorx;
    
    // Only process characters that fit on the screen
    int maxLength = screenWidth - cursorx;
    if (text.length() > maxLength) {
      text = text.substring(0, maxLength);
    }
    
    bool needsUpdate = false;
    String updatedChars = "";
    
    // Check each character for changes
    for (int i = 0; i < text.length(); i++) {
      int bufferPos = startPos + i;
      
      // Safety check for buffer bounds
      if (bufferPos >= currentScreen.length()) {
        break;
      }
      
      char newChar = text.charAt(i);
      char currentChar = currentScreen.charAt(bufferPos);
      
      if (newChar != currentChar) {
        if (!needsUpdate) {
          // First difference found, set cursor position
          lcd.setCursor(cursorx + i, cursory);
          needsUpdate = true;
        }
        
        // Add character to update string
        updatedChars += newChar;
        
        // Update buffer
        currentScreen.setCharAt(bufferPos, newChar);
      } else if (needsUpdate) {
        // We were updating but this char matches, print accumulated changes
        lcd.print(updatedChars);
        updatedChars = "";
        needsUpdate = false;
      }
    }
    
    // Print any remaining changes
    if (needsUpdate && updatedChars.length() > 0) {
      lcd.print(updatedChars);
    }
  }

  // Clear a specific line efficiently
  void clearLine(int line) {
    if (line >= 0 && line < screenHeight) {
      String spaces = "";
      for (int i = 0; i < screenWidth; i++) {
        spaces += " ";
      }
      LCDdraw(0, line, spaces);
    }
  }

  // Clear entire screen and reset buffer
  void clearScreen() {
    initScreen(screenWidth, screenHeight);
  }

  void drawStart(){ // draws the start up page
    // Clear the screen buffer and physical display
    initScreen(screenWidth, screenHeight);
    
    // Use efficient drawing
    LCDdraw(0, 0, "WIRELESS CONTROLLER"); // project name
    LCDdraw(0, 1, String("version ") + String(codeVersion)); // shows version
    LCDdraw(0, 2, "by: Manuel Rao"); // shows author
  }

  void drawSpotifyConection(){ // draws the spotify setpu page
    initScreen(screenWidth, screenHeight);
    
    LCDdraw(0, 0, "Spotify Setup");
    LCDdraw(0, 1, "Connect to IP:");
    LCDdraw(0, 2, WiFi.localIP().toString()); 
    LCDdraw(0, 3, "For setup");
  }

  void waitForDevice(){ // draws the waiting for device page
    // Don't clear entire screen, just update what's needed
    timeClient.update();
    LCDdraw(15, 0, timeClient.getFormattedTime().substring(0,5));
    LCDdraw(0, 1, "waiting for device  "); // Extra spaces to clear any previous text
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

    // Volume and time line
    String volLine = "vol: " + String(spotifyConnection.currVol) + "%";
    timeClient.update(); // shows the time
    
    // UPDATED: Used timeClient.getHours() instead of hour() from TimeLib
    int h = timeClient.getHours();
    int m = timeClient.getMinutes();
    String ti = String((h < 10)? "0"+String(h) : String(h))+":"+String((m < 10)? "0"+String(m) : String(m));
    
    // Create complete top line with volume and time
    String topLine = volLine;
    // Pad with spaces to position time at column 15
    while (topLine.length() < 15) {
      topLine += " ";
    }
    topLine += ti;
    
    // Ensure top line is exactly 20 characters
    while (topLine.length() < 20) {
      topLine += " ";
    }
    if (topLine.length() > 20) {
      topLine = topLine.substring(0, 20);
    }
    
    LCDdraw(0, 0, topLine);

    // shows song name
    String displayName = spotifyConnection.currentSong.song + "- ("+spotifyConnection.currentSong.artist+")";
    int nameLen = displayName.length();
    if(lastSong != spotifyConnection.currentSong.song){
      if(nameLen > 20){
        scrolable = true;
        nameScroll = 0;
      }else{
        scrolable = false;
        for(int i =  0; i < 20 - nameLen; i++){
          displayName += " ";
        }
      }
      lastSong = spotifyConnection.currentSong.song;
    }
    
    String songLine = "";
    if(scrolable){
      songLine = displayName.substring(nameScroll, nameScroll+20);
      int steps = int((millis()-LastUpdate)/1000);
      if(nameScroll + 20 == nameLen){nameScroll = 0;}else if(nameScroll + 20 + steps <= nameLen){nameScroll += steps;}else{nameScroll++;}
      if(steps > 0) LastUpdate = millis();
    }else{
      songLine = displayName;
    }
    
    // Ensure song line is exactly 20 characters
    while (songLine.length() < 20) {
      songLine += " ";
    }
    if (songLine.length() > 20) {
      songLine = songLine.substring(0, 20);
    }
    
    LCDdraw(0, 1, songLine);

    //progress bar
    float progress = 0.0;
    int bars = 0;
    
    if (spotifyConnection.currentSong.durationMs > 0) {
        progress = float(spotifyConnection.currentSongPositionMs)/float(spotifyConnection.currentSong.durationMs);
        bars = floor(progress*18)-1;
    } else {
        // Fallback for when data is missing
        progress = 0;
        bars = 0;
    }
    
    bool l_finisher = (int(round(progress*36))%2==1)? true : false;
    String progressLine = "";
    progressLine += char(2); // starter char
    
    // Safety clamp for bars to avoid loop overflow
    if (bars > 16) bars = 16;
    if (bars < 0) bars = -1;

    for(int i = 0; i <= bars; i ++){
      progressLine += char(7); // point char
    }
    progressLine += char((l_finisher)? 5 : 6); // end char
    
    for(int i = 0; i < (16 - bars); i ++){
      progressLine += "-";
    }
    progressLine += char(6); // end char
    
    // Ensure progress line is exactly 20 characters
    while (progressLine.length() < 20) {
      progressLine += " ";
    }
    if (progressLine.length() > 20) {
      progressLine = progressLine.substring(0, 20);
    }
    
    LCDdraw(0, 2, progressLine);

    // show progress time and total song length
    int progressMinsIn = floor(spotifyConnection.currentSongPositionMs/60000);
    String progressMins = String((progressMinsIn < 10) ? "0" + String(progressMinsIn) : String(progressMinsIn));
    int progressSecIn = floor(spotifyConnection.currentSongPositionMs/1000) - progressMinsIn*60;
    String progressSec = String((progressSecIn < 10) ? "0" + String(progressSecIn) : String(progressSecIn));
    
    int durationMinsIn = floor(spotifyConnection.currentSong.durationMs/60000);
    String durationMins = String((durationMinsIn < 10) ? "0" + String(durationMinsIn) : String(durationMinsIn));
    int durationSecIn = floor(spotifyConnection.currentSong.durationMs/1000) - durationMinsIn*60;
    String durationSec = String((durationSecIn < 10) ? "0" + String(durationSecIn) : String(durationSecIn));
    
    String timeLine = progressMins + ":" + progressSec + "/" + durationMins + ":" + durationSec;

    while(timeLine.length() < 13){
      timeLine += " ";
    }
    timeLine += "< "; //backtrack
    timeLine += char(spotifyConnection.isPlaying ? 0 : 1); //pause/play button
    timeLine += " > "; //skip button
    timeLine += char(spotifyConnection.currentSong.isLiked ? 3 : 4); // heart
    
    
    
    LCDdraw(0, 3, timeLine);
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
// FIXED: Updated HTML to provide instructions and a Paste Box for the Manual Loopback method
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
            char page[500];
            snprintf_P(page, 500, errorPage);
            server.send(200, "text/html", String(page)); //Send web page
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
void IRAM_ATTR pinManager(){
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
        yield();
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

// --- ADDED: Helper functions for storage ---
void saveCredentials() {
  if (spotifyConnection.refreshToken.length() > 0 && spotifyConnection.refreshToken.length() < 512) {
    SpotifyData data;
    memset(&data, 0, sizeof(SpotifyData));
    spotifyConnection.refreshToken.toCharArray(data.refreshToken, 512);
    data.timestamp = timeClient.getEpochTime();
    data.magic = 0x42;
    
    // Write struct
    EEPROM.put(0, data);
    EEPROM.commit();
    Serial.println("Data saved to EEPROM");
    Serial.print("data length: ");
    Serial.println(sizeof(data));
  }
}

bool loadCredentials() {
  SpotifyData data;
  EEPROM.get(0, data);
  if (data.magic == 0x42) {
    Serial.println("Found valid credentials in EEPROM");
    spotifyConnection.refreshToken = String(data.refreshToken);
    // Seed time if NTP hasn't updated yet (optional)
    return true;
  }
  return false;
}


/*
-----------------------------------------------  
----------------- Setup -----------------------
-----------------------------------------------
*/

void setup(){
  Serial.begin(115200);
  // ADDED: Initialize EEPROM
  EEPROM.begin(1024);

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
  
  WiFi.begin(WIFI_SSID, PASSWORD); 
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

  delay(500);
  timeClient.begin(); 
  updateRTCTime(); 
  lcd.setCursor(0, 3);
  lcd.print("connected to NTP    ");

  // Initialize async callbacks after spotifyConnection exists
  spotifyConnection.initializeCallbacks();
  
  Serial.println("🚀 Async HTTP system initialized");
  Serial.print("📡 WiFi signal strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("🌐 Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  bool credentialsLoaded = loadCredentials();
  
  if (credentialsLoaded) {
      Serial.println("Attempting to refresh token using stored credentials...");
      if (spotifyConnection.refreshAuth()) {
          Serial.println("Refresh successful!");
          serverOn = false; // Skip setup server
          LCDm.drawMusic();
      } else {
          Serial.println("Refresh failed. Starting setup server.");
          serverOn = true;
          LCDm.drawSpotifyConection();
      }
  } else {
      Serial.println("No credentials found. Starting setup server.");
      serverOn = true;
      LCDm.drawSpotifyConection();
  }

  // Only start server if needed
  if (serverOn) {
      server.on("/", handleRoot);                  //Which routine to handle at root location
      server.on("/callback", handleCallbackPage);      //Which routine to handle at root location
      server.begin();                              //Start server
      Serial.println("HTTP server started");           // loging
      lcd.setCursor(0, 3);
      lcd.print("HTTPS server Started");
  }

  for(int i = 0; i < 4; i ++){ // set pin modes and attach interrupts
    pinMode(buttonPins[i], INPUT);
    attachInterrupt(digitalPinToInterrupt(buttonPins[i]), pinManager, RISING);
  }

  delay(250);
  if (!serverOn) {
      // If we skipped setup, just show "Ready" briefly
      lcd.setCursor(0, 3);
      lcd.print("Start sequence DONE "); 
      delay(500);
      LCDm.clearScreen();
  }

}

// int mode = 0; // Mode global variable conflicts with local arguments, better to keep it managed

void loop(){  // main loop

  if (millis() - lastNTPUpdate >= NTP_UPDATE_INTERVAL) { 
    updateRTCTime();
    lastNTPUpdate = millis();
  }

  if(spotifyConnection.accessTokenSet){
    if(serverOn){ // close server 
        server.close();
        serverOn = false;
        // Save immediately after successful first setup
        saveCredentials();
    }

    // check if spotify auth token needs to be refreshed
    if((millis() - spotifyConnection.tokenStartTime)/1000 > (unsigned long)spotifyConnection.tokenExpireTime){
        Serial.println("refreshing token");
        if(spotifyConnection.refreshAuth()){
            Serial.println("refreshed token");
            saveCredentials();
        }
    }

    spotifyConnection.update(); // gets current playing from spotify

    //update display
    static unsigned long lastDisplayUpdate = 0;
        if (millis() - lastDisplayUpdate >= 250) {
            if(spotifyConnection.currentSong.song.length() > 0){
                LCDm.drawMusic();
            } else {
                LCDm.waitForDevice(); 
            }
            lastDisplayUpdate = millis();
        }

    if(call){ // manage user interactions
      switch (pinCalled){
        case 0: spotifyConnection.skipBack(); break;
        case 1: spotifyConnection.togglePlay(); break;
        case 2: spotifyConnection.skipForward(); break;
        case 3: spotifyConnection.toggleLiked(spotifyConnection.currentSong.Id);  break;
      }
      call = false;
    }

    // handle volume adjustment
    int volRequest = map(analogRead(A0), 0, 1023, 0, 100);
    if (abs(volRequest - spotifyConnection.currVol) > 2) {
        spotifyConnection.adjustVolume(volRequest);
        spotifyConnection.lastTrackInfoRequest += 300; // delay next track info request
    }
    timeLoop = millis();
  } else {
      if (serverOn) {
          server.handleClient();
      }
  }
  yield();
}