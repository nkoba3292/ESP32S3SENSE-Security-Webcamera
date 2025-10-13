/*
 * XIAO ESP32S3 Sense Security Camera System
 * 
 * Features:
 * - AI-powered person detection using TensorFlow Lite Micro
 * - Real-time alerts with buzzer and LED indicators
 * - LINE Notify integration for instant notifications
 * - Video recording to SD card
 * - Live streaming web interface
 * 
 * Hardware: Seeed Studio XIAO ESP32S3 Sense
 * Author: Security Camera System
 * Date: 2025-10-13
 */

#include "security_camera.h"
#include "config.h"

// Global variables
SystemState current_state = STATE_INITIALIZING;
AlertSystem alert_system = {false, false, false, 0, 0, false};
RecordingSystem recording_system = {false, 0, "", File()};
AsyncWebServer server(web_server_port);

// Camera configuration
camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = camera_frame_size,
    .jpeg_quality = camera_jpeg_quality,
    .fb_count = camera_fb_count,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    .fb_location = CAMERA_FB_IN_PSRAM
};

// Static variables for detection timing
static unsigned long last_detection_time = 0;
static unsigned long last_inference_time = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== XIAO ESP32S3 Security Camera System ===");
    Serial.println("Initializing system...");
    
    // Initialize hardware pins
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED1_PIN, OUTPUT);
    pinMode(LED2_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    // Turn off all outputs initially
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    
    // Initialize system components
    if (!initializeSystem()) {
        handleError("System initialization failed");
        return;
    }
    
    current_state = STATE_IDLE;
    Serial.println("System initialized successfully!");
    Serial.println("Starting person detection...");
    
    // Blink built-in LED to indicate ready
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(200);
        digitalWrite(LED_BUILTIN, LOW);
        delay(200);
    }
}

void loop() {
    unsigned long current_time = millis();
    
    switch (current_state) {
        case STATE_IDLE:
        case STATE_DETECTING:
            // Perform AI inference at regular intervals
            if (current_time - last_inference_time >= DETECTION_INTERVAL_MS) {
                last_inference_time = current_time;
                
                camera_fb_t *fb = esp_camera_fb_get();
                if (fb) {
                    current_state = STATE_DETECTING;
                    
                    // Perform person detection
                    if (detectPerson(fb)) {
                        // Check cooldown period
                        if (current_time - last_detection_time >= detection_cooldown_ms) {
                            last_detection_time = current_time;
                            Serial.println("Person detected! Triggering alert system...");
                            processDetection();
                        }
                    }
                    
                    esp_camera_fb_return(fb);
                    current_state = STATE_IDLE;
                }
            }
            break;
            
        case STATE_ALERT_ACTIVE:
            updateAlert();
            break;
            
        case STATE_RECORDING:
            updateRecording(nullptr);
            break;
            
        case STATE_ERROR:
            // Error state - blink built-in LED
            digitalWrite(LED_BUILTIN, (current_time / 1000) % 2);
            break;
    }
    
    // Handle web server
    delay(loop_delay_ms);
}

bool initializeSystem() {
    Serial.println("Initializing camera...");
    if (!initializeCamera()) {
        return false;
    }
    
    Serial.println("Initializing SD card...");
    if (!initializeSDCard()) {
        Serial.println("Warning: SD card initialization failed. Recording disabled.");
    }
    
    Serial.println("Initializing WiFi...");
    if (!initializeWiFi()) {
        Serial.println("Warning: WiFi initialization failed. Network features disabled.");
    }
    
    Serial.println("Setting up web server...");
    setupWebServer();
    
    Serial.println("Initializing AI model...");
    if (!initializeAI()) {
        Serial.println("Warning: AI model initialization failed. Detection disabled.");
    }
    
    return true;
}

bool initializeCamera() {
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }
    
    // Get camera sensor
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // Improve image quality settings
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 0);       // -2 to 2
        s->set_saturation(s, 0);     // -2 to 2
        s->set_special_effect(s, 0); // 0 to 6 (0-No Effect, 1-Negative, 2-Grayscale...)
        s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
        s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
        s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled
        s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
        s->set_aec2(s, 0);           // 0 = disable , 1 = enable
        s->set_ae_level(s, 0);       // -2 to 2
        s->set_aec_value(s, 300);    // 0 to 1200
        s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
        s->set_agc_gain(s, 0);       // 0 to 30
        s->set_gainceiling(s, (gainceiling_t)0); // 0 to 6
        s->set_bpc(s, 0);            // 0 = disable , 1 = enable
        s->set_wpc(s, 1);            // 0 = disable , 1 = enable
        s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
        s->set_lenc(s, 1);           // 0 = disable , 1 = enable
        s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
        s->set_vflip(s, 0);          // 0 = disable , 1 = enable
        s->set_dcw(s, 1);            // 0 = disable , 1 = enable
        s->set_colorbar(s, 0);       // 0 = disable , 1 = enable
    }
    
    Serial.println("Camera initialized successfully");
    return true;
}

bool initializeSDCard() {
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD Card Mount Failed");
        return false;
    }
    
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return false;
    }
    
    // Create recordings directory
    if (!SD_MMC.exists(recording_path)) {
        SD_MMC.mkdir(recording_path);
    }
    
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    
    return true;
}

bool initializeWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(1000);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("WiFi connected! IP address: ");
        Serial.println(WiFi.localIP());
        
        // Initialize time
        configTime(timezone_offset_hours * 3600, 0, ntp_server);
        
        return true;
    } else {
        Serial.println("WiFi connection failed");
        return false;
    }
}

bool initializeAI() {
    // TODO: Initialize TensorFlow Lite Micro model
    // This is a placeholder for AI model initialization
    Serial.println("AI model initialization - TODO: Implement TFLite model loading");
    return true;
}

void setupWebServer() {
    // Root page
    server.on("/", HTTP_GET, handleRoot);
    
    // Streaming endpoint
    server.on("/stream", HTTP_GET, handleStream);
    
    // Capture endpoint
    server.on("/capture", HTTP_GET, handleCapture);
    
    // System status endpoint
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{";
        json += "\"state\":\"" + String(current_state) + "\",";
        json += "\"alert_active\":" + String(alert_system.alert_triggered ? "true" : "false") + ",";
        json += "\"recording_active\":" + String(recording_system.recording_active ? "true" : "false") + ",";
        json += "\"uptime\":" + String(millis()) + ",";
        json += "\"free_heap\":" + String(ESP.getFreeHeap());
        json += "}";
        request->send(200, "application/json", json);
    });
    
    server.begin();
    Serial.println("Web server started");
}

bool detectPerson(camera_fb_t *fb) {
    // TODO: Implement actual AI inference using TensorFlow Lite Micro
    // This is a placeholder that simulates detection
    
    // For now, randomly trigger detection for testing (remove this in production)
    static int detection_counter = 0;
    detection_counter++;
    
    // Simulate detection every 30 inference cycles (for testing)
    if (detection_counter % 30 == 0) {
        return true;
    }
    
    return false;
}

void processDetection() {
    current_state = STATE_ALERT_ACTIVE;
    startAlert();
    
    // Send LINE message
    if (enable_line_messaging && WiFi.status() == WL_CONNECTED) {
        String message = "【防犯通知】人物を検知しました\n";
        message += "時刻: " + String(getCurrentTime()) + "\n";
        message += "映像ストリーミング: " + getStreamingURL();
        sendLINEMessage(message);
    }
    
    // Start recording
    if (enable_recording) {
        startRecording();
    }
}

void startAlert() {
    alert_system.alert_triggered = true;
    alert_system.alert_start_time = millis();
    alert_system.last_led_toggle = millis();
    alert_system.buzzer_active = enable_buzzer;
    alert_system.led1_state = true;
    alert_system.led2_state = false;
    
    // Start buzzer
    if (enable_buzzer) {
        controlBuzzer(true);
    }
    
    // Set initial LED states
    if (enable_led_alerts) {
        digitalWrite(LED1_PIN, alert_system.led1_state);
        digitalWrite(LED2_PIN, alert_system.led2_state);
    }
    
    Serial.println("Alert system activated");
}

void updateAlert() {
    unsigned long current_time = millis();
    
    // Check if alert duration has elapsed
    if (current_time - alert_system.alert_start_time >= ALERT_DURATION_MS) {
        stopAlert();
        return;
    }
    
    // Update LED blinking
    if (enable_led_alerts && 
        current_time - alert_system.last_led_toggle >= LED_BLINK_INTERVAL_MS) {
        alert_system.last_led_toggle = current_time;
        
        // Toggle LEDs in opposite phases
        alert_system.led1_state = !alert_system.led1_state;
        alert_system.led2_state = !alert_system.led2_state;
        
        digitalWrite(LED1_PIN, alert_system.led1_state);
        digitalWrite(LED2_PIN, alert_system.led2_state);
    }
}

void stopAlert() {
    alert_system.alert_triggered = false;
    alert_system.buzzer_active = false;
    
    // Turn off buzzer
    controlBuzzer(false);
    
    // Turn off LEDs
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    
    current_state = STATE_IDLE;
    Serial.println("Alert system deactivated");
}

void controlBuzzer(bool state) {
    if (state) {
        // Generate PWM tone for buzzer
        ledcSetup(0, buzzer_frequency, 8);
        ledcAttachPin(BUZZER_PIN, 0);
        ledcWrite(0, 128); // 50% duty cycle
    } else {
        ledcWrite(0, 0);
        ledcDetachPin(BUZZER_PIN);
        digitalWrite(BUZZER_PIN, LOW);
    }
}

void startRecording() {
    recording_system.recording_active = true;
    recording_system.recording_start_time = millis();
    recording_system.current_filename = generateFilename();
    
    current_state = STATE_RECORDING;
    Serial.println("Recording started: " + recording_system.current_filename);
}

void updateRecording(camera_fb_t *fb) {
    unsigned long current_time = millis();
    
    // Check if recording duration has elapsed
    if (current_time - recording_system.recording_start_time >= RECORDING_DURATION_MS) {
        stopRecording();
        return;
    }
    
    // TODO: Implement actual video recording to SD card
    // This would involve writing frames to a video file format
}

void stopRecording() {
    recording_system.recording_active = false;
    
    if (recording_system.video_file) {
        recording_system.video_file.close();
    }
    
    current_state = STATE_IDLE;
    Serial.println("Recording stopped: " + recording_system.current_filename);
}

String generateFilename() {
    time_t now = time(0);
    struct tm *timeinfo = localtime(&now);
    
    char filename[50];
    strftime(filename, sizeof(filename), "rec_%Y%m%d_%H%M%S.avi", timeinfo);
    
    return recording_path + String(filename);
}

void sendLINEMessage(String message) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    http.begin("https://api.line.me/v2/bot/message/push");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(line_channel_access_token));
    
    // Create JSON payload
    DynamicJsonDocument doc(1024);
    doc["to"] = line_user_id;
    
    JsonArray messages = doc.createNestedArray("messages");
    JsonObject textMessage = messages.createNestedObject();
    textMessage["type"] = "text";
    textMessage["text"] = message;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode == 200) {
        Serial.println("LINE message sent successfully");
    } else {
        Serial.printf("LINE message failed with code: %d\n", httpResponseCode);
        String response = http.getString();
        Serial.println("Response: " + response);
    }
    
    http.end();
}

String getStreamingURL() {
    if (WiFi.status() == WL_CONNECTED) {
        return "http://" + WiFi.localIP().toString() + "/stream";
    }
    return "Stream unavailable";
}

void handleRoot(AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head><title>Security Camera</title></head>";
    html += "<body><h1>XIAO ESP32S3 Security Camera</h1>";
    html += "<p><a href='/stream'>Live Stream</a></p>";
    html += "<p><a href='/capture'>Capture Image</a></p>";
    html += "<p><a href='/status'>System Status</a></p>";
    html += "</body></html>";
    
    request->send(200, "text/html", html);
}

void handleStream(AsyncWebServerRequest *request) {
    // TODO: Implement MJPEG streaming
    request->send(501, "text/plain", "Streaming not implemented yet");
}

void handleCapture(AsyncWebServerRequest *request) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "image/jpeg", fb->buf, fb->len);
        response->addHeader("Content-Disposition", "inline; filename=capture.jpg");
        request->send(response);
        esp_camera_fb_return(fb);
    } else {
        request->send(500, "text/plain", "Camera capture failed");
    }
}

void handleError(String error_message) {
    Serial.println("ERROR: " + error_message);
    current_state = STATE_ERROR;
    
    // Send error notification if possible
    if (WiFi.status() == WL_CONNECTED && enable_line_messaging) {
        sendLINEMessage("【システムエラー】" + error_message);
    }
}

unsigned long getCurrentTime() {
    return millis() / 1000;
}

void printSystemStatus() {
    Serial.println("\n=== System Status ===");
    Serial.println("State: " + String(current_state));
    Serial.println("Alert Active: " + String(alert_system.alert_triggered));
    Serial.println("Recording Active: " + String(recording_system.recording_active));
    Serial.println("Free Heap: " + String(ESP.getFreeHeap()));
    Serial.println("Uptime: " + String(millis() / 1000) + " seconds");
    Serial.println("====================\n");
}