#ifndef SECURITY_CAMERA_H
#define SECURITY_CAMERA_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "esp_camera.h"
#include "SD_MMC.h"
#include "FS.h"
#include "SPIFFS.h"

// Hardware pin definitions for XIAO ESP32S3 SENSE
#define LED_BUILTIN 21
#define BUZZER_PIN 2
#define LED1_PIN 3
#define LED2_PIN 4
#define BUTTON_PIN 0  // Boot button

// Camera configuration for XIAO ESP32S3 SENSE
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// SD Card pins
#define SD_MMC_CMD 38
#define SD_MMC_CLK 39
#define SD_MMC_D0  40

// System configuration
#define DETECTION_INTERVAL_MS 1000  // AI inference interval
#define ALERT_DURATION_MS 10000     // Alert duration (10 seconds)
#define LED_BLINK_INTERVAL_MS 500   // LED blink interval (0.5 seconds)
#define RECORDING_DURATION_MS 30000 // Video recording duration (30 seconds)

// WiFi configuration (change these to your network)
extern const char* wifi_ssid;
extern const char* wifi_password;

// LINE Messaging API configuration
extern const char* line_channel_access_token;
extern const char* line_user_id;

// System states
enum SystemState {
    STATE_INITIALIZING,
    STATE_IDLE,
    STATE_DETECTING,
    STATE_ALERT_ACTIVE,
    STATE_RECORDING,
    STATE_ERROR
};

// Alert system structure
struct AlertSystem {
    bool buzzer_active;
    bool led1_state;
    bool led2_state;
    unsigned long alert_start_time;
    unsigned long last_led_toggle;
    bool alert_triggered;
};

// Recording system structure
struct RecordingSystem {
    bool recording_active;
    unsigned long recording_start_time;
    String current_filename;
    File video_file;
};

// Function declarations
bool initializeSystem();
bool initializeCamera();
bool initializeSDCard();
bool initializeWiFi();
bool initializeAI();
void setupWebServer();

// AI Detection functions
bool detectPerson(camera_fb_t *fb);
void processDetection();

// Alert system functions
void startAlert();
void updateAlert();
void stopAlert();
void updateLEDs();
void controlBuzzer(bool state);

// Recording functions
void startRecording();
void updateRecording(camera_fb_t *fb);
void stopRecording();
String generateFilename();

// Communication functions
void sendLINEMessage(String message);
String getStreamingURL();

// Web server handlers
void handleRoot(AsyncWebServerRequest *request);
void handleStream(AsyncWebServerRequest *request);
void handleCapture(AsyncWebServerRequest *request);

// Utility functions
void printSystemStatus();
void handleError(String error_message);
unsigned long getCurrentTime();

// Global variables (declared as extern, defined in main.cpp)
extern SystemState current_state;
extern AlertSystem alert_system;
extern RecordingSystem recording_system;
extern AsyncWebServer server;
extern camera_config_t camera_config;

#endif // SECURITY_CAMERA_H