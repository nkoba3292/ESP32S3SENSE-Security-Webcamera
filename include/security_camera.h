#ifndef SECURITY_CAMERA_H
#define SECURITY_CAMERA_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "esp_camera.h"
#include "SD.h"
#include "SPI.h"
#include "FS.h"
#include "SPIFFS.h"

// Hardware pin definitions for XIAO ESP32S3 SENSE
#ifndef LED_BUILTIN
#define LED_BUILTIN 21
#endif
#define BUZZER_PIN 2   // HW-s08 passive buzzer (3.3V operation - lower volume than 5V)
#define LED1_PIN 3
#define LED2_PIN 4
#define BUTTON_PIN 0  // Boot button
#define PIR_SENSOR_PIN 1  // SB412A output (connect to free GPIO)

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

// SD Card pin (XIAO ESP32S3 Sense expansion board uses SPI SD with CS=GPIO21)
#define SD_CS_PIN 21

// Agriculture IoT Sensor pins (SHT30 & Pump control)
#define SHT30_SDA_PIN 5      // I2C SDA (wire library)
#define SHT30_SCL_PIN 6      // I2C SCL (wire library)
#define SHT30_POWER_PIN 43   // GPIO control for SHT30 VCC (avoid GPIO8 SD-line conflict)
#define PUMP_MOSFET_PIN 44   // GPIO control for pump MOSFET gate (avoid SPI/SD pin conflict)

// System configuration
#define DETECTION_INTERVAL_MS 500  // 500ms間隔で反応速度向上
#define ALERT_DURATION_MS 10000     // Alert duration (10 seconds)
#define LED_BLINK_INTERVAL_MS 150   // LED点滅間隔 (0.15秒で高速点滅)
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
    unsigned long last_frame_time;
    int frame_count;
};

// Function declarations
bool initializeSystem();
bool initializeCamera();
bool initializeSDCard();
bool initializeWiFi();
void setupWebServer();

// Detection functions
bool detectPerson(camera_fb_t *fb);
void processDetection();

// Alert system functions
void startAlert();
void updateAlert();
void stopAlert();
void controlBuzzer(bool state);

// Recording functions
void startRecording();
void updateRecording(camera_fb_t *fb);
void stopRecording();
String generateFilename();
bool saveFrameToSD(camera_fb_t *fb);

// Streaming functions
void handleStream(AsyncWebServerRequest *request);
String getStreamBoundary();

// Communication functions
void sendLINEMessage(String message);
String getStreamingURL();
unsigned long getCurrentTime();

// Web handlers
void handleRoot(AsyncWebServerRequest *request);
void handleCapture(AsyncWebServerRequest *request);
void handleError(String error_message);
void printSystemStatus();
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