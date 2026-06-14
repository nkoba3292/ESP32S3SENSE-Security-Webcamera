/*
 * XIAO ESP32S3 Sense 防犯カメラシステム
 *
 * 機能概要:
 * - カメラフレームを用いた動体検出
 * - ブザーとLEDによるリアルタイム警告
 * - LINE Messaging API連携による通知送信
 * - SDカードへの映像記録
 * - Webインターフェースでのライブ表示
 *
 * 対応ハードウェア: Seeed Studio XIAO ESP32S3 Sense
 * 作成者: Security Camera System
 * 作成日: 2025-10-13
 */

#include "security_camera.h"
#include "config.h"
#include <Wire.h>
#include "esp_wifi.h"

// Global variables
SystemState current_state = STATE_INITIALIZING;
AlertSystem alert_system = {false, false, false, 0, 0, false};
RecordingSystem recording_system = {false, 0, "", File(), 0, 0};
AsyncWebServer server(web_server_port);
bool sd_card_available = false;  // SDカードの状態を追跡

// AVI writer state (single active recording)
static const int AVI_MAX_INDEX_ENTRIES = 1024;
static uint32_t avi_frame_offsets[AVI_MAX_INDEX_ENTRIES];
static uint32_t avi_frame_sizes[AVI_MAX_INDEX_ENTRIES];
static uint32_t avi_riff_size_pos = 0;
static uint32_t avi_avih_frames_pos = 0;
static uint32_t avi_strh_length_pos = 0;
static uint32_t avi_movi_list_start = 0;
static uint32_t avi_movi_size_pos = 0;
static uint32_t avi_movi_data_start = 0;
static bool avi_header_written = false;

static void writeFourCC(File &file, const char *cc) {
    file.write((const uint8_t *)cc, 4);
}

static void writeLE16(File &file, uint16_t value) {
    uint8_t b[2] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF)
    };
    file.write(b, 2);
}

static void writeLE32(File &file, uint32_t value) {
    uint8_t b[4] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 24) & 0xFF)
    };
    file.write(b, 4);
}

static void patchLE32(File &file, uint32_t pos, uint32_t value) {
    size_t cur = file.position();
    file.seek(pos);
    writeLE32(file, value);
    file.seek(cur);
}

static bool writeAviHeader(File &file, uint16_t width, uint16_t height, uint16_t fps) {
    if (fps == 0) fps = 1;

    writeFourCC(file, "RIFF");
    avi_riff_size_pos = file.position();
    writeLE32(file, 0); // RIFF size placeholder
    writeFourCC(file, "AVI ");

    // LIST 'hdrl'
    writeFourCC(file, "LIST");
    writeLE32(file, 192);
    writeFourCC(file, "hdrl");

    // 'avih'
    writeFourCC(file, "avih");
    writeLE32(file, 56);
    writeLE32(file, 1000000UL / fps); // dwMicroSecPerFrame
    writeLE32(file, 0);               // dwMaxBytesPerSec
    writeLE32(file, 0);               // dwPaddingGranularity
    writeLE32(file, 0x10);            // dwFlags (has index)
    avi_avih_frames_pos = file.position();
    writeLE32(file, 0);               // dwTotalFrames placeholder
    writeLE32(file, 0);               // dwInitialFrames
    writeLE32(file, 1);               // dwStreams
    writeLE32(file, 0);               // dwSuggestedBufferSize
    writeLE32(file, width);           // dwWidth
    writeLE32(file, height);          // dwHeight
    writeLE32(file, 0);               // dwReserved[0]
    writeLE32(file, 0);               // dwReserved[1]
    writeLE32(file, 0);               // dwReserved[2]
    writeLE32(file, 0);               // dwReserved[3]

    // LIST 'strl'
    writeFourCC(file, "LIST");
    writeLE32(file, 116);
    writeFourCC(file, "strl");

    // 'strh'
    writeFourCC(file, "strh");
    writeLE32(file, 56);
    writeFourCC(file, "vids");
    writeFourCC(file, "MJPG");
    writeLE32(file, 0);               // dwFlags
    writeLE16(file, 0);               // wPriority
    writeLE16(file, 0);               // wLanguage
    writeLE32(file, 0);               // dwInitialFrames
    writeLE32(file, 1);               // dwScale
    writeLE32(file, fps);             // dwRate
    writeLE32(file, 0);               // dwStart
    avi_strh_length_pos = file.position();
    writeLE32(file, 0);               // dwLength placeholder
    writeLE32(file, 0);               // dwSuggestedBufferSize
    writeLE32(file, 0xFFFFFFFF);      // dwQuality
    writeLE32(file, 0);               // dwSampleSize
    writeLE16(file, 0);               // rcFrame.left
    writeLE16(file, 0);               // rcFrame.top
    writeLE16(file, width);           // rcFrame.right
    writeLE16(file, height);          // rcFrame.bottom

    // 'strf' (BITMAPINFOHEADER)
    writeFourCC(file, "strf");
    writeLE32(file, 40);
    writeLE32(file, 40);              // biSize
    writeLE32(file, width);           // biWidth
    writeLE32(file, height);          // biHeight
    writeLE16(file, 1);               // biPlanes
    writeLE16(file, 24);              // biBitCount
    writeFourCC(file, "MJPG");       // biCompression
    writeLE32(file, 0);               // biSizeImage
    writeLE32(file, 0);               // biXPelsPerMeter
    writeLE32(file, 0);               // biYPelsPerMeter
    writeLE32(file, 0);               // biClrUsed
    writeLE32(file, 0);               // biClrImportant

    // LIST 'movi'
    avi_movi_list_start = file.position();
    writeFourCC(file, "LIST");
    avi_movi_size_pos = file.position();
    writeLE32(file, 0);               // movi list size placeholder
    writeFourCC(file, "movi");
    avi_movi_data_start = file.position();

    avi_header_written = true;
    return true;
}

static void finalizeAviFile(File &file) {
    if (!avi_header_written) {
        return;
    }

    uint32_t idx1_start = file.position();
    int indexed_frames = recording_system.frame_count;
    if (indexed_frames > AVI_MAX_INDEX_ENTRIES) {
        indexed_frames = AVI_MAX_INDEX_ENTRIES;
    }

    writeFourCC(file, "idx1");
    writeLE32(file, (uint32_t)indexed_frames * 16UL);
    for (int i = 0; i < indexed_frames; i++) {
        writeFourCC(file, "00dc");
        writeLE32(file, 0x10); // key frame
        writeLE32(file, avi_frame_offsets[i]);
        writeLE32(file, avi_frame_sizes[i]);
    }

    uint32_t file_size = file.position();
    uint32_t movi_list_size = idx1_start - (avi_movi_list_start + 8);

    patchLE32(file, avi_avih_frames_pos, (uint32_t)recording_system.frame_count);
    patchLE32(file, avi_strh_length_pos, (uint32_t)recording_system.frame_count);
    patchLE32(file, avi_movi_size_pos, movi_list_size);
    patchLE32(file, avi_riff_size_pos, file_size - 8);

    avi_header_written = false;
}

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
    .xclk_freq_hz = 10000000,  // 10MHz (安定性優先)
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = (framesize_t)camera_frame_size,
    .jpeg_quality = camera_jpeg_quality,
    .fb_count = camera_fb_count,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

// Static variables for detection timing
static unsigned long last_detection_time = 0;
static unsigned long last_inference_time = 0;
static unsigned long pir_ready_time = 0;
static unsigned long last_sht30_read_time = 0;
static bool sht30_i2c_ready = false;
static float sht30_last_temperature = NAN;
static float sht30_last_humidity = NAN;
static unsigned long last_wifi_status_log = 0;
static const char* wifi_init_phase = "not_started";
static esp_err_t last_camera_init_error = ESP_OK;
static unsigned long last_camera_error_log_time = 0;
static bool pump_debug_active = false;
static unsigned long pump_debug_off_time = 0;
static unsigned long last_pump_force_log_time = 0;

static uint8_t sht30Crc8(const uint8_t *data, int length) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static void powerOnSHT30() {
    if (!sht30_use_gpio_power) {
        return;
    }
    digitalWrite(SHT30_POWER_PIN, HIGH);
    delay(sht30_startup_delay_ms);
}

static void powerOffSHT30() {
    if (!sht30_use_gpio_power) {
        return;
    }
    if (sht30_power_always_on_debug) {
        return;
    }
    digitalWrite(SHT30_POWER_PIN, LOW);
}

static int probeI2CAddress(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission();
}

static void scanI2CBus() {
    Serial.println("[I2C] Scanning bus addresses 0x03-0x77...");
    int foundCount = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        int rc = probeI2CAddress(addr);
        if (rc == 0) {
            Serial.printf("[I2C] Found device at 0x%02X\n", addr);
            foundCount++;
        }
    }
    if (foundCount == 0) {
        Serial.println("[I2C] No devices found on bus");
    }
}

static bool initializeSHT30Debug() {
    if (sht30_use_gpio_power) {
        pinMode(SHT30_POWER_PIN, OUTPUT);
        powerOnSHT30();
    }

    if (!sht30_i2c_ready) {
        pinMode(SHT30_SDA_PIN, INPUT_PULLUP);
        pinMode(SHT30_SCL_PIN, INPUT_PULLUP);
        Wire.begin(SHT30_SDA_PIN, SHT30_SCL_PIN);
        Wire.setClock(50000);
        sht30_i2c_ready = true;
    }

    if (sht30_use_gpio_power) {
        Serial.printf("[SHT30] Power pin GPIO%d ON, I2C SDA=%d SCL=%d, addr=0x%02X\n",
                      SHT30_POWER_PIN, SHT30_SDA_PIN, SHT30_SCL_PIN, sht30_i2c_address);
        if (sht30_power_always_on_debug) {
            Serial.println("[SHT30] Debug mode: power pin is forced ON continuously");
        }
    } else {
        Serial.printf("[SHT30] Direct 3V3 power mode, I2C SDA=%d SCL=%d, addr=0x%02X\n",
                      SHT30_SDA_PIN, SHT30_SCL_PIN, sht30_i2c_address);
        Serial.println("[SHT30] Please wire sensor VCC to 3V3 and GND to GND");
    }
    int rc44 = probeI2CAddress(0x44);
    int rc45 = probeI2CAddress(0x45);
    Serial.printf("[SHT30] Probe 0x44 rc=%d, Probe 0x45 rc=%d (0 means detected)\n", rc44, rc45);
    if (rc44 != 0 && rc45 != 0) {
        scanI2CBus();
    }
    return true;
}

static bool readSHT30(float &temperatureC, float &humidityPercent, uint16_t &rawTemperature, uint16_t &rawHumidity) {
    if (!sht30_i2c_ready) {
        return false;
    }

    powerOnSHT30();

    uint8_t activeAddress = (uint8_t)sht30_i2c_address;
    Wire.beginTransmission(activeAddress);
    Wire.write(0x24);
    Wire.write(0x00);
    int txResult = Wire.endTransmission();
    if (txResult != 0) {
        // Some SHT30 boards are strapped to 0x45. Fallback once for debug ease.
        activeAddress = (activeAddress == 0x44) ? 0x45 : 0x44;
        Wire.beginTransmission(activeAddress);
        Wire.write(0x24);
        Wire.write(0x00);
        txResult = Wire.endTransmission();
    }
    if (txResult != 0) {
        Serial.printf("[SHT30] Command transmission failed rc=%d (addr 0x%02X/0x%02X)\n",
                      txResult,
                      (uint8_t)sht30_i2c_address,
                      (uint8_t)((sht30_i2c_address == 0x44) ? 0x45 : 0x44));
        scanI2CBus();
        powerOffSHT30();
        return false;
    }

    delay(15);

    uint8_t buffer[6];
    int received = Wire.requestFrom(activeAddress, (uint8_t)6);
    if (received != 6) {
        Serial.printf("[SHT30] Unexpected byte count: %d\n", received);
        powerOffSHT30();
        return false;
    }

    for (int i = 0; i < 6; i++) {
        if (!Wire.available()) {
            Serial.println("[SHT30] I2C buffer underrun");
            powerOffSHT30();
            return false;
        }
        buffer[i] = Wire.read();
    }

    if (sht30Crc8(buffer, 2) != buffer[2] || sht30Crc8(buffer + 3, 2) != buffer[5]) {
        Serial.println("[SHT30] CRC mismatch");
        powerOffSHT30();
        return false;
    }

    rawTemperature = (uint16_t(buffer[0]) << 8) | buffer[1];
    rawHumidity = (uint16_t(buffer[3]) << 8) | buffer[4];

    temperatureC = -45.0f + 175.0f * (float)rawTemperature / 65535.0f;
    humidityPercent = 100.0f * (float)rawHumidity / 65535.0f;
    if (humidityPercent < 0.0f) humidityPercent = 0.0f;
    if (humidityPercent > 100.0f) humidityPercent = 100.0f;

    powerOffSHT30();
    return true;
}

static void serviceSHT30Debug(unsigned long currentTime) {
    if (!enable_agriculture_iot) {
        return;
    }

    if (last_sht30_read_time == 0 || currentTime - last_sht30_read_time >= (unsigned long)sensor_read_interval_ms) {
        last_sht30_read_time = currentTime;

        float temperatureC = 0.0f;
        float humidityPercent = 0.0f;
        uint16_t rawTemperature = 0;
        uint16_t rawHumidity = 0;

        Serial.println("[SHT30] Reading sensor...");
        if (readSHT30(temperatureC, humidityPercent, rawTemperature, rawHumidity)) {
            sht30_last_temperature = temperatureC;
            sht30_last_humidity = humidityPercent;
            Serial.printf("[SHT30] rawT=%u rawH=%u | Temp=%.2f C | Hum=%.2f %%\n",
                          rawTemperature, rawHumidity, temperatureC, humidityPercent);
        } else {
            Serial.println("[SHT30] Read failed");
        }
    }
}

static void setPumpGate(bool on, const char* reason) {
    digitalWrite(PUMP_MOSFET_PIN, on ? HIGH : LOW);
    Serial.printf("[PUMP] Gate=%s (%s)\n", on ? "HIGH" : "LOW", reason);
}

static bool startPumpDebugPulse(unsigned long durationMs) {
    if (!enable_pump_control) {
        Serial.println("[PUMP] Pump control disabled in config");
        return false;
    }

    if (durationMs == 0) {
        durationMs = 1000;
    }

    if (durationMs > (unsigned long)pump_max_duration_ms) {
        durationMs = (unsigned long)pump_max_duration_ms;
    }

    setPumpGate(true, "debug pulse start");
    pump_debug_active = true;
    pump_debug_off_time = millis() + durationMs;
    Serial.printf("[PUMP] Auto-off in %lu ms\n", durationMs);
    return true;
}

static void servicePumpDebug(unsigned long currentTime) {
    if (!pump_debug_active) {
        return;
    }

    if ((long)(currentTime - pump_debug_off_time) >= 0) {
        setPumpGate(false, "debug pulse timeout");
        pump_debug_active = false;
    }
}

void setup() {
    Serial.begin(115200);
    // USB CDC monitor attach待ち（起動直後のログ取りこぼし対策）
    unsigned long serial_wait_start = millis();
    while (!Serial && (millis() - serial_wait_start) < 2000) {
        delay(10);
    }
    delay(300);
    Serial.println("\n=== XIAO ESP32S3 Security Camera System ===");
    Serial.println("Initializing system...");
    
    // Initialize hardware pins
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED1_PIN, OUTPUT);
    pinMode(LED2_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(PIR_SENSOR_PIN, INPUT);
    pinMode(PUMP_MOSFET_PIN, OUTPUT);
    initializeSHT30Debug();
    
    // Turn off all outputs initially
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    digitalWrite(PUMP_MOSFET_PIN, LOW);
    
    // Test buzzer BEFORE WiFi initialization to avoid interference
    Serial.println("[BUZZER TEST] Testing buzzer (3 beeps)...");
    ledcSetup(0, 2000, 8);  // Channel 0, 2000Hz, 8-bit resolution
    ledcAttachPin(BUZZER_PIN, 0);
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        digitalWrite(LED1_PIN, HIGH);
        digitalWrite(LED2_PIN, HIGH);
        ledcWrite(0, 128);  // 50% duty cycle
        delay(300);
        digitalWrite(LED_BUILTIN, LOW);
        digitalWrite(LED1_PIN, LOW);
        digitalWrite(LED2_PIN, LOW);
        ledcWrite(0, 0);
        delay(200);
    }
    Serial.println("[BUZZER TEST] Test complete\n");
    
    // Initialize system components
    if (!initializeSystem()) {
        handleError("System initialization failed");
        return;
    }
    
    current_state = STATE_IDLE;
    Serial.println("System initialized successfully!");

    if (enable_pump_control && force_pump_on_after_init) {
        setPumpGate(true, "forced continuous run after init");
        pump_debug_active = false;
        Serial.println("[PUMP] Forced continuous ON mode is active");
    }

    // Short beep to signal initialization complete
    ledcSetup(0, buzzer_frequency, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWrite(0, 128);
    delay(100);
    ledcWrite(0, 0);
    ledcDetachPin(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
    
    // Print LINE configuration status
    Serial.println("\n=== LINE Messaging Configuration ===");
    Serial.printf("Messaging Enabled: %s\n", enable_line_messaging ? "YES" : "NO");
    Serial.printf("Channel Token Configured: %s\n", 
                  String(line_channel_access_token) != "YOUR_CHANNEL_ACCESS_TOKEN" ? "YES" : "NO");
    Serial.printf("Channel Token Length: %d characters\n", strlen(line_channel_access_token));
    Serial.printf("User ID Configured: %s\n", 
                  String(line_user_id) != "YOUR_USER_ID" ? "YES" : "NO");
    Serial.printf("User ID: %s\n", line_user_id);
    Serial.printf("User ID Length: %d characters\n", strlen(line_user_id));
    Serial.printf("User ID Format Check: %s\n", 
                  line_user_id[0] == 'U' ? "OK (starts with U)" : "ERROR (should start with U)");
    
    if (enable_line_messaging) {
        if (String(line_channel_access_token) == "YOUR_CHANNEL_ACCESS_TOKEN") {
            Serial.println("⚠️  WARNING: Please configure LINE Channel Access Token in config.h");
        }
        if (String(line_user_id) == "YOUR_USER_ID") {
            Serial.println("⚠️  WARNING: Please configure LINE User ID in config.h");
        }
    }
    Serial.println("===================================\n");
    
    if (enable_pir_monitoring) {
        Serial.println("Starting motion detection...");
    } else {
        Serial.println("PIR monitoring is disabled (WEB manual operation mode)");
    }
    Serial.printf("Access web interface at: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("Test LINE notification at: http://%s/test-line\n", WiFi.localIP().toString().c_str());
    Serial.printf("Check LINE config at: http://%s/line-config\n\n", WiFi.localIP().toString().c_str());
    Serial.printf("PIR sensor pin: GPIO %d\n", PIR_SENSOR_PIN);
    if (enable_pir_monitoring) {
        Serial.printf("PIR warmup: %d ms\n", pir_warmup_ms);
    }
    
    // 最初の検知を即座に開始するため、タイムスタンプを調整
    last_inference_time = millis() - DETECTION_INTERVAL_MS;
    pir_ready_time = millis() + pir_warmup_ms;
    
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
    servicePumpDebug(current_time);

    if (WiFi.status() != WL_CONNECTED && current_time - last_wifi_status_log >= 5000) {
        last_wifi_status_log = current_time;
        Serial.printf("[WiFi] Not connected yet (status=%d, mode=%d, phase=%s, uptime=%lu sec)\n",
                      (int)WiFi.status(), (int)WiFi.getMode(), wifi_init_phase, current_time / 1000);
    }

    if (strcmp(wifi_init_phase, "camera_init_failed") == 0 && current_time - last_camera_error_log_time >= 5000) {
        last_camera_error_log_time = current_time;
        Serial.printf("[CAMERA] init failed persistently, last error=0x%x\n", (unsigned int)last_camera_init_error);
    }

    if (enable_pump_control && force_pump_on_after_init) {
        if (digitalRead(PUMP_MOSFET_PIN) != HIGH) {
            setPumpGate(true, "force mode keep-alive");
        }

        if (current_time - last_pump_force_log_time >= 5000) {
            last_pump_force_log_time = current_time;
            Serial.printf("[PUMP] Force mode monitor pin GPIO%d state=%d\n",
                          PUMP_MOSFET_PIN,
                          digitalRead(PUMP_MOSFET_PIN) == HIGH ? 1 : 0);
        }
    }
    
    // 動体検知を再有効化（カメラ固定設定で変動を抑制）
    switch (current_state) {
        case STATE_IDLE:
        case STATE_DETECTING:
            // Perform PIR detection at regular intervals
            if (!enable_pir_monitoring) {
                current_state = STATE_IDLE;
                break;
            }
            if (current_time - last_inference_time >= DETECTION_INTERVAL_MS) {
                last_inference_time = current_time;
                current_state = STATE_DETECTING;

                if (current_time < pir_ready_time) {
                    unsigned long remain = (pir_ready_time - current_time) / 1000;
                    static unsigned long last_warmup_log = 0;
                    if (current_time - last_warmup_log >= 5000) {
                        last_warmup_log = current_time;
                        Serial.printf("[PIR] Warming up... %lu sec remaining\n", remain);
                    }
                } else {
                    if (detectPerson(nullptr)) {
                        // Check cooldown period
                        if (current_time - last_detection_time >= detection_cooldown_ms) {
                            last_detection_time = current_time;
                            Serial.println("\n🔔🔔🔔 >>> PIR motion detected! Triggering alert system... <<< 🔔🔔🔔");
                            Serial.printf(">>> Next alert available in %d seconds <<<\n\n", detection_cooldown_ms / 1000);
                            processDetection();
                        } else {
                            unsigned long time_until_next = (detection_cooldown_ms - (current_time - last_detection_time)) / 1000;
                            Serial.printf("⏱️ [COOLDOWN] Detection suppressed. Next alert in %lu seconds\n", time_until_next);
                        }
                    }
                }

                if (current_state != STATE_ALERT_ACTIVE && current_state != STATE_RECORDING) {
                    current_state = STATE_IDLE;
                }
            }
            break;
            
        case STATE_ALERT_ACTIVE:
            updateAlert();
            // Check if we're also recording
            if (recording_system.recording_active) {
                updateRecording(nullptr);
            }
            break;
            
        case STATE_RECORDING:
            updateRecording(nullptr);
            // Check if alert is also active
            if (alert_system.alert_triggered) {
                updateAlert();
            }
            // Return to idle when recording finishes
            if (!recording_system.recording_active && !alert_system.alert_triggered) {
                current_state = STATE_IDLE;
            }
            break;
            
        case STATE_ERROR:
            // Error state - blink built-in LED
            digitalWrite(LED_BUILTIN, (current_time / 1000) % 2);
            break;
    }

    serviceSHT30Debug(current_time);
    
    delay(loop_delay_ms);
}

bool initializeSystem() {
    wifi_init_phase = "init_sequence_started";
    Serial.println("Initializing camera...");
    wifi_init_phase = "camera_init";
    if (!initializeCamera()) {
        wifi_init_phase = "camera_init_failed";
        return false;
    }
    
    Serial.println("Initializing SD card...");
    wifi_init_phase = "sd_init";
    sd_card_available = initializeSDCard();
    if (!sd_card_available) {
        Serial.println("Warning: SD card initialization failed. Recording disabled.");
    } else {
        Serial.println("SD card initialized successfully.");
    }
    
    Serial.println("Initializing WiFi...");
    wifi_init_phase = "wifi_init_enter";
    if (!initializeWiFi()) {
        wifi_init_phase = "wifi_init_failed";
        Serial.println("⚠️  WARNING: WiFi initialization failed. Network features disabled.");
        Serial.printf("Please check WiFi SSID '%s' is available\n", wifi_ssid);
    } else {
        wifi_init_phase = "wifi_connected";
        Serial.println("✓ WiFi connected successfully");
    }
    
    Serial.println("Setting up web server...");
    wifi_init_phase = "web_server_setup";
    setupWebServer();
    wifi_init_phase = "system_ready";
    
    return true;
}

bool initializeCamera() {
    // カメラ初期化前にメモリを確保
    Serial.printf("Free heap before camera init: %d bytes\n", ESP.getFreeHeap());
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        last_camera_init_error = err;
        Serial.printf("Camera init failed with error 0x%x\n", err);

        // Fallback: lower memory load and retry once.
        Serial.println("[CAMERA] Retrying with fallback settings (QQVGA, fb_count=1)...");
        camera_config.frame_size = FRAMESIZE_QQVGA;
        camera_config.fb_count = 1;
        delay(200);

        err = esp_camera_init(&camera_config);
        if (err != ESP_OK) {
            last_camera_init_error = err;
            Serial.printf("[CAMERA] Fallback init also failed: 0x%x\n", err);
            return false;
        }

        Serial.println("[CAMERA] Fallback init succeeded");
    }

    last_camera_init_error = ESP_OK;
    
    Serial.printf("Free heap after camera init: %d bytes\n", ESP.getFreeHeap());
    
    // Get camera sensor and configure BEFORE taking any frames
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        Serial.println("[CAMERA] Configuring sensor for better image quality...");
        
        // 基本設定
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 0);       // -2 to 2
        s->set_saturation(s, 0);     // -2 to 2
        s->set_special_effect(s, 0); // 0 = No Effect
        
        // ステップ1: 初期化時に自動調整を有効化して最適値を学習
        Serial.println("[CAMERA] Step 1: Enabling auto-adjustments for initialization...");
        s->set_whitebal(s, 1);       // AWB有効
        s->set_awb_gain(s, 1);       // AWBゲイン有効
        s->set_exposure_ctrl(s, 1);  // 自動露出有効
        s->set_aec2(s, 1);           // AEC DSP有効
        s->set_ae_level(s, 0);       // AEレベル: 0
        s->set_gain_ctrl(s, 1);      // 自動ゲイン有効
        s->set_gainceiling(s, (gainceiling_t)2); // ゲイン上限
        
        // 画質補正を有効化
        s->set_bpc(s, 1);            // 黒点補正
        s->set_wpc(s, 1);            // 白点補正
        s->set_raw_gma(s, 1);        // ガンマ補正
        s->set_lenc(s, 1);           // レンズ補正
        s->set_hmirror(s, 0);        // 水平反転なし
        s->set_vflip(s, 0);          // 垂直反転なし
        s->set_dcw(s, 1);            // ダウンサイズ有効
        s->set_colorbar(s, 0);       // テストパターンなし
        
        Serial.println("[CAMERA] Auto-adjustments enabled");
    }
    
    // センサー設定後、自動調整が安定するまでフレームを取得
    Serial.println("[CAMERA] Step 2: Capturing frames for auto-adjustment stabilization...");
    for (int i = 0; i < 10; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            Serial.printf("  Calibration frame %d: %d bytes\n", i+1, fb->len);
            esp_camera_fb_return(fb);
            delay(300);
        }
    }
    
    // ステップ3: 自動調整を完全に無効化して固定値を設定
    if (s) {
        Serial.println("[CAMERA] Step 3: Disabling ALL auto-adjustments...");
        
        // 自動露出を完全に無効化
        s->set_exposure_ctrl(s, 0);  // 自動露出OFF
        s->set_aec2(s, 0);           // AEC DSP OFF（重要！）
        s->set_aec_value(s, 300);    // 固定露出値
        s->set_ae_level(s, 0);       // AEレベル0
        
        // 自動ゲインを完全に無効化
        s->set_gain_ctrl(s, 0);      // 自動ゲインOFF
        s->set_agc_gain(s, 1);       // 固定ゲイン値（最小値）
        s->set_gainceiling(s, (gainceiling_t)0); // ゲイン上限を最小に
        
        // ホワイトバランスを無効化
        s->set_whitebal(s, 0);       // AWB OFF
        s->set_awb_gain(s, 0);       // AWBゲインOFF
        
        // フレーム安定性のため一部補正は維持、変動の原因は無効化
        s->set_raw_gma(s, 0);        // ガンマ補正OFF（変動の原因）
        s->set_lenc(s, 0);           // レンズ補正OFF（変動の原因）
        
        Serial.println("[CAMERA] All auto-adjustments DISABLED:");
        Serial.println("  - Exposure: FIXED at 300");
        Serial.println("  - AEC2 DSP: OFF");
        Serial.println("  - Gain: FIXED at 1 (minimum)");
        Serial.println("  - AWB: OFF");
        Serial.println("  - Gamma/Lens correction: OFF");
        Serial.println("[CAMERA] Frame should now be stable (<5% variation)");
    }
    
    Serial.println("Camera initialized successfully");
    return true;
}

bool initializeSDCard() {
    auto cardTypeToString = [](uint8_t cardType) -> const char* {
        switch (cardType) {
            case CARD_MMC: return "MMC";
            case CARD_SD: return "SDSC";
            case CARD_SDHC: return "SDHC/SDXC";
            case CARD_NONE: return "NONE";
            default: return "UNKNOWN";
        }
    };

    Serial.println("[SD] Initializing SPI SD...");
    Serial.printf("[SD] Using CS pin: %d\n", SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD Card Mount Failed");
        sd_card_available = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        sd_card_available = false;
        return false;
    }

    Serial.printf("[SD] Card type: %s (%u)\n", cardTypeToString(cardType), cardType);

    // Create recordings directory (normalize trailing slash for mkdir/exists)
    String recordingDir = recording_path;
    if (recordingDir.length() > 1 && recordingDir.endsWith("/")) {
        recordingDir.remove(recordingDir.length() - 1);
    }
    if (!SD.exists(recordingDir)) {
        if (!SD.mkdir(recordingDir)) {
            Serial.printf("[SD] Failed to create recordings directory: %s\n", recordingDir.c_str());
            sd_card_available = false;
            return false;
        }
    }

    // Basic write test to verify that recording will work later
    File probe = SD.open("/sd_probe.txt", FILE_WRITE);
    if (!probe) {
        Serial.println("[SD] Write test failed: cannot open /sd_probe.txt");
        sd_card_available = false;
        return false;
    }
    probe.println("probe=ok");
    probe.close();
    SD.remove("/sd_probe.txt");
    Serial.println("[SD] Write test passed");

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    sd_card_available = true;
    return true;
}

bool initializeWiFi() {
    // TIMEOUT対策: STA再初期化 + 事前スキャンでターゲットAPを固定して接続
    wifi_init_phase = "wifi_off";
    WiFi.mode(WIFI_OFF);
    delay(1000);
    wifi_init_phase = "sta_mode";
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    int targetChannel = 0;
    int targetRssi = -127;
    bool hasTargetBssid = false;
    uint8_t targetBssid[6] = {0};
    String targetBssidStr = "";

    Serial.printf("[WiFi] Scan target SSID: %s\n", wifi_ssid);
    int found = WiFi.scanNetworks(false, true);
    for (int i = 0; i < found; i++) {
        if (WiFi.SSID(i) == String(wifi_ssid) && WiFi.RSSI(i) > targetRssi) {
            targetRssi = WiFi.RSSI(i);
            targetChannel = WiFi.channel(i);
            targetBssidStr = WiFi.BSSIDstr(i);
            const uint8_t *bssidPtr = WiFi.BSSID(i);
            if (bssidPtr != nullptr) {
                memcpy(targetBssid, bssidPtr, 6);
                hasTargetBssid = true;
            }
        }
    }
    WiFi.scanDelete();

    Serial.printf("[WiFi] MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("[WiFi] mode(before begin)=%d status(before begin)=%d\n",
                  (int)WiFi.getMode(), (int)WiFi.status());
    Serial.printf("[WiFi] Connecting to SSID: %s\n", wifi_ssid);
    if (hasTargetBssid) {
        Serial.printf("[WiFi] Lock AP: BSSID=%s CH=%d RSSI=%d dBm\n",
                      targetBssidStr.c_str(), targetChannel, targetRssi);
    } else {
        Serial.println("[WiFi] Target BSSID not found in scan; fallback to normal connect");
    }

    wifi_init_phase = "begin_call";
    if (hasTargetBssid) {
        WiFi.begin(wifi_ssid, wifi_password, targetChannel, targetBssid, true);
    } else {
        WiFi.begin(wifi_ssid, wifi_password);
    }
    wifi_init_phase = "begin_returned";
    Serial.printf("[WiFi] mode(after begin)=%d status(after begin)=%d\n",
                  (int)WiFi.getMode(), (int)WiFi.status());

    Serial.println("Connecting to WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 60) {
        wifi_init_phase = "waiting_for_connect";
        delay(1000);
        Serial.print(".");
        attempts++;
        if (attempts % 10 == 0) {
            Serial.println();
            Serial.printf("Still trying... Attempt %d/60 (Status: %d, Mode: %d)\n",
                          attempts, (int)WiFi.status(), (int)WiFi.getMode());
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifi_init_phase = "connected";
        Serial.println();
        Serial.println("✓ WiFi connected successfully");
        Serial.print("WiFi connected! IP address: ");
        Serial.println(WiFi.localIP());
        Serial.printf("Signal strength (RSSI): %d dBm\n", WiFi.RSSI());
        
        // Initialize time
        configTime(timezone_offset_hours * 3600, 0, ntp_server);
        
        return true;
    } else {
        wifi_init_phase = "failed";
        Serial.println();
        Serial.println("WiFi connection failed");
        Serial.printf("Final WiFi status: %d, mode: %d\n", (int)WiFi.status(), (int)WiFi.getMode());

        // 接続失敗時に周辺アクセスポイント情報を出力して切り分けしやすくする
        Serial.println("Scanning nearby WiFi networks for diagnostics...");
        int found = WiFi.scanNetworks(false, true);
        if (found <= 0) {
            Serial.println("No WiFi networks found");
        } else {
            Serial.printf("Found %d WiFi networks:\n", found);
            for (int i = 0; i < found; i++) {
                Serial.printf("  %2d) SSID: %s | RSSI: %d dBm | Ch: %d | Enc: %d\n",
                              i + 1,
                              WiFi.SSID(i).c_str(),
                              WiFi.RSSI(i),
                              WiFi.channel(i),
                              (int)WiFi.encryptionType(i));
            }
        }

        return false;
    }
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
        json += "\"pump_active\":" + String(pump_debug_active ? "true" : "false") + ",";
        json += "\"pump_pin_state\":" + String(digitalRead(PUMP_MOSFET_PIN) == HIGH ? "1" : "0") + ",";
        json += "\"uptime\":" + String(millis()) + ",";
        json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
        if (!isnan(sht30_last_temperature)) {
            json += "\"temperature\":" + String(sht30_last_temperature, 2) + ",";
            json += "\"humidity\":" + String(sht30_last_humidity, 2);
        } else {
            json += "\"temperature\":null,";
            json += "\"humidity\":null";
        }
        json += "}";
        request->send(200, "application/json", json);
    });

    // Pump debug endpoint: /pump-test?ms=1200
    server.on("/pump-test", HTTP_GET, [](AsyncWebServerRequest *request) {
        unsigned long durationMs = 1000;
        if (request->hasParam("ms")) {
            durationMs = (unsigned long)request->getParam("ms")->value().toInt();
        }

        bool started = startPumpDebugPulse(durationMs);
        if (!started) {
            request->send(503, "application/json", "{\"ok\":false,\"message\":\"pump disabled\"}");
            return;
        }

        String json = "{";
        json += "\"ok\":true,";
        json += "\"requested_ms\":" + String(durationMs) + ",";
        json += "\"capped_max_ms\":" + String(pump_max_duration_ms);
        json += "}";
        request->send(200, "application/json", json);
    });

    // Pump ON endpoint (持続ON)
    server.on("/pump-on", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!enable_pump_control) {
            request->send(503, "application/json", "{\"ok\":false,\"message\":\"pump disabled\"}");
            return;
        }
        setPumpGate(true, "manual on endpoint");
        pump_debug_active = true;
        pump_debug_off_time = millis() + (unsigned long)pump_max_duration_ms;  // 安全タイムアウト
        request->send(200, "application/json", "{\"ok\":true,\"pump_active\":true}");
    });

    // Pump immediate OFF endpoint
    server.on("/pump-off", HTTP_GET, [](AsyncWebServerRequest *request) {
        setPumpGate(false, "manual off endpoint");
        pump_debug_active = false;
        request->send(200, "application/json", "{\"ok\":true,\"pump_active\":false}");
    });
    
    // LINE notification test endpoint
    server.on("/test-line", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("\n>>> Manual LINE notification test triggered from web <<<");
        String testMessage = "【テスト通知】\n";
        testMessage += "これはWebインターフェースからの手動テストです\n";
        testMessage += "時刻: " + String(getCurrentTime()) + " 秒\n";
        testMessage += "IPアドレス: " + WiFi.localIP().toString();
        
        sendLINEMessage(testMessage);
        
        String response = "<html><head><meta charset='UTF-8'></head><body>";
        response += "<h1>LINE通知テスト</h1>";
        response += "<p>テスト通知を送信しました。</p>";
        response += "<p>シリアルモニターでデバッグ情報を確認してください。</p>";
        response += "<p><a href='/'>戻る</a></p>";
        response += "</body></html>";
        request->send(200, "text/html", response);
    });
    
    // LINE config check endpoint
    server.on("/line-config", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<html><head><meta charset='UTF-8'></head><body>";
        html += "<h1>LINE設定確認</h1>";
        html += "<pre>";
        html += "WiFi接続: " + String(WiFi.status() == WL_CONNECTED ? "✓ 接続済み" : "✗ 未接続") + "\n";
        html += "IPアドレス: " + WiFi.localIP().toString() + "\n\n";
        html += "LINE設定:\n";
        html += "- 通知有効: " + String(enable_line_messaging ? "有効" : "無効") + "\n";
        html += "- トークン設定: " + String(String(line_channel_access_token) != "YOUR_CHANNEL_ACCESS_TOKEN" ? "✓ 設定済み" : "✗ 未設定") + "\n";
        html += "- トークン長: " + String(strlen(line_channel_access_token)) + " 文字\n";
        html += "- ユーザーID設定: " + String(String(line_user_id) != "YOUR_USER_ID" ? "✓ 設定済み" : "✗ 未設定") + "\n";
        html += "- ユーザーID: " + String(line_user_id) + "\n";
        html += "- ユーザーID長: " + String(strlen(line_user_id)) + " 文字\n";
        html += "- ユーザーID形式: " + String(line_user_id[0] == 'U' ? "✓ 正常 (Uで始まる)" : "✗ 異常 (Uで始まるべき)") + "\n";
        html += "</pre>";
        html += "<p><a href='/test-line'>LINE通知テスト</a> | <a href='/'>戻る</a></p>";
        html += "</body></html>";
        request->send(200, "text/html", html);
    });
    
    // File list endpoint
    server.on("/files", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<html><head><meta charset='UTF-8'><style>body{font-family:sans-serif;margin:20px;}a{display:block;margin:6px 0;}</style></head><body>";
        html += "<h1>録画ファイル一覧</h1>";
        if (!sd_card_available) {
            html += "<p>SDカード未接続</p>";
        } else {
            File dir = SD.open("/recordings");
            if (!dir) {
                html += "<p>/recordings ディレクトリが見つかりません</p>";
            } else {
                int count = 0;
                while (true) {
                    File entry = dir.openNextFile();
                    if (!entry) break;
                    if (!entry.isDirectory()) {
                        String name = entry.name();
                        String size = String(entry.size() / 1024) + " KB";
                        html += "<a href='/download?f=" + name + "'>" + name + " (" + size + ")</a>";
                        count++;
                    }
                    entry.close();
                }
                dir.close();
                if (count == 0) html += "<p>ファイルなし</p>";
            }
        }
        html += "<p><a href='/'>戻る</a></p></body></html>";
        request->send(200, "text/html", html);
    });

    // File download endpoint
    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("f")) {
            request->send(400, "text/plain", "f parameter required");
            return;
        }
        String filename = request->getParam("f")->value();
        // Security: prevent path traversal
        if (filename.indexOf("..") >= 0 || filename.indexOf("/") >= 0) {
            request->send(400, "text/plain", "Invalid filename");
            return;
        }
        String path = "/recordings/" + filename;
        if (!SD.exists(path)) {
            request->send(404, "text/plain", "File not found");
            return;
        }
        String contentType = "application/octet-stream";
        if (filename.endsWith(".avi")) {
            contentType = "video/x-msvideo";
        } else if (filename.endsWith(".mjpeg")) {
            contentType = "video/x-motion-jpeg";
        }

        AsyncWebServerResponse *response = request->beginResponse(SD, path, contentType, true);
        response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        request->send(response);
    });

    server.begin();
    Serial.println("Web server started");
}

bool detectPerson(camera_fb_t *fb) {
    (void)fb;

    static bool prev_triggered = false;
    static unsigned long last_status_log = 0;

    int raw_value = digitalRead(PIR_SENSOR_PIN);
    bool triggered = pir_active_high ? (raw_value == HIGH) : (raw_value == LOW);

    // 立ち上がりのみを検知イベントとして扱う（連続HIGHでの再通知防止）
    bool rising_edge_detected = (triggered && !prev_triggered);
    prev_triggered = triggered;

    unsigned long now = millis();
    if (now - last_status_log >= 5000) {
        last_status_log = now;
        Serial.printf("[PIR] Raw:%d Triggered:%s\n", raw_value, triggered ? "YES" : "NO");
    }

    return rising_edge_detected;
}

void processDetection() {
    current_state = STATE_ALERT_ACTIVE;
    
    Serial.println("\n========================================");
    Serial.println("🚨 PERSON DETECTED - ALERT TRIGGERED 🚨");
    Serial.println("========================================");
    Serial.printf("Time: %lu seconds since boot\n", getCurrentTime());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Streaming URL: %s\n", getStreamingURL().c_str());
    
    startAlert();
    
    // Send LINE message (only if enabled)
    if (enable_line_messaging && WiFi.status() == WL_CONNECTED) {
        String message = "【防犯通知】人物を検知しました\n";
        message += "時刻: " + String(getCurrentTime()) + "\n";
        message += "映像ストリーミング: " + getStreamingURL();
        sendLINEMessage(message);
    } else {
        Serial.println("[DEBUG] LINE messaging disabled - notification skipped");
    }
    
    // Start recording if SD card is available
    if (enable_recording && sd_card_available) {
        startRecording();
    } else {
        Serial.println("[DEBUG] Recording disabled or SD card not available");
    }
    
    Serial.println("========================================\n");
}

void startAlert() {
    alert_system.alert_triggered = true;
    alert_system.alert_start_time = millis();
    alert_system.last_led_toggle = millis();
    alert_system.buzzer_active = enable_buzzer;
    alert_system.led1_state = true;
    alert_system.led2_state = false;
    
    Serial.println("[ALERT] Starting alert system...");
    
    // Start buzzer
    if (enable_buzzer) {
        Serial.println("[BUZZER] Attempting to activate buzzer...");
        Serial.printf("[BUZZER] Pin: %d, Frequency: %d Hz\n", BUZZER_PIN, buzzer_frequency);
        controlBuzzer(true);
        Serial.println("[BUZZER] Buzzer activation command sent");
    } else {
        Serial.println("[BUZZER] Buzzer disabled in config");
    }
    
    // Set initial LED states
    if (enable_led_alerts) {
        digitalWrite(LED1_PIN, alert_system.led1_state);
        digitalWrite(LED2_PIN, alert_system.led2_state);
        Serial.printf("[LED] LED1: %s, LED2: %s\n", 
                     alert_system.led1_state ? "ON" : "OFF",
                     alert_system.led2_state ? "ON" : "OFF");
    } else {
        Serial.println("[LED] LED alerts disabled in config");
    }
    
    Serial.println("[ALERT] Alert system activated");
}

void updateAlert() {
    unsigned long current_time = millis();
    static int blink_count = 0;  // LED点滅カウンター
    
    // 3セット（6回の状態変化）点滅したら停止
    if (blink_count >= 6) {
        blink_count = 0;
        stopAlert();
        return;
    }
    
    // Check if alert duration has elapsed (バックアップ)
    if (current_time - alert_system.alert_start_time >= ALERT_DURATION_MS) {
        blink_count = 0;
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
        
        blink_count++;
        Serial.printf("🚨 [LED] Blink %d/6 (3 sets)\n", blink_count);
    }
}

void stopAlert() {
    Serial.println("\n[ALERT] Stopping alert system...");
    
    alert_system.alert_triggered = false;
    alert_system.buzzer_active = false;
    
    // Turn off buzzer
    controlBuzzer(false);
    
    // Turn off LEDs
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    Serial.println("[LED] All LEDs turned off");
    
    if (!recording_system.recording_active) {
        current_state = STATE_IDLE;
        Serial.println("[STATE] Returned to IDLE state");
    } else {
        Serial.println("[STATE] Recording still active, maintaining state");
    }
    
    Serial.println("[ALERT] Alert system deactivated\n");
}

void controlBuzzer(bool state) {
    if (state) {
        // Generate PWM tone for buzzer
        Serial.printf("[BUZZER] Starting - Freq: %d Hz, Duty: 50%%\n", buzzer_frequency);
        ledcSetup(0, buzzer_frequency, 8);
        ledcAttachPin(BUZZER_PIN, 0);
        ledcWrite(0, 128); // 50% duty cycle
    } else {
        Serial.println("[BUZZER] Stopping");
        ledcWrite(0, 0);
        ledcDetachPin(BUZZER_PIN);
        digitalWrite(BUZZER_PIN, LOW);
    }
}

void startRecording() {
    if (!sd_card_available) {
        Serial.println("Recording skipped: SD card not available");
        return;
    }
    
    recording_system.recording_active = true;
    recording_system.recording_start_time = millis();
    recording_system.last_frame_time = millis();
    recording_system.frame_count = 0;
    recording_system.current_filename = generateFilename();
    avi_header_written = false;
    
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
    
    unsigned long frame_interval_ms = 100;
    if (recording_fps > 0) {
        frame_interval_ms = 1000UL / (unsigned long)recording_fps;
    }

    // Capture frames at configured FPS during recording
    if (recording_system.recording_active && 
        current_time - recording_system.last_frame_time >= frame_interval_ms) {
        
        recording_system.last_frame_time = current_time;
        
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame) {
            if (saveFrameToSD(frame)) {
                recording_system.frame_count++;
            }
            esp_camera_fb_return(frame);
        }
    }
}

void stopRecording() {
    recording_system.recording_active = false;
    
    if (recording_system.video_file) {
        finalizeAviFile(recording_system.video_file);
        recording_system.video_file.close();
    }
    
    Serial.printf("Recording stopped: %s (%d frames)\n", 
                  recording_system.current_filename.c_str(), 
                  recording_system.frame_count);

    // Short beep to signal recording complete
    ledcSetup(0, buzzer_frequency, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWrite(0, 128);
    delay(100);
    ledcWrite(0, 0);
    ledcDetachPin(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);

    if (!alert_system.alert_triggered) {
        current_state = STATE_IDLE;
    }
}

bool saveFrameToSD(camera_fb_t *fb) {
    if (!fb || !recording_system.recording_active) {
        return false;
    }
    
    // Create or append to AVI(MJPEG) file
    if (!recording_system.video_file) {
        recording_system.video_file = SD.open(recording_system.current_filename, FILE_WRITE);
        if (!recording_system.video_file) {
            Serial.println("Failed to open file for recording");
            return false;
        }
        if (!writeAviHeader(recording_system.video_file, fb->width, fb->height, (uint16_t)recording_fps)) {
            Serial.println("Failed to write AVI header");
            recording_system.video_file.close();
            return false;
        }
        Serial.println("Recording file opened successfully (AVI)");
    }

    uint32_t chunk_start = recording_system.video_file.position();
    writeFourCC(recording_system.video_file, "00dc");
    writeLE32(recording_system.video_file, fb->len);
    recording_system.video_file.write(fb->buf, fb->len);

    // AVI chunks are word-aligned
    if (fb->len & 1) {
        recording_system.video_file.write((uint8_t)0x00);
    }

    if (recording_system.frame_count < AVI_MAX_INDEX_ENTRIES) {
        avi_frame_offsets[recording_system.frame_count] = chunk_start - avi_movi_data_start;
        avi_frame_sizes[recording_system.frame_count] = fb->len;
    }
    
    return true;
}

String generateFilename() {
    time_t now = time(0);
    struct tm *timeinfo = localtime(&now);
    
    char filename[50];
    strftime(filename, sizeof(filename), "rec_%Y%m%d_%H%M%S", timeinfo);

    return recording_path + String(filename) + recording_format;
}

void sendLINEMessage(String message) {
    Serial.println("\n=== LINE Message Debug ===");
    
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: WiFi not connected!");
        Serial.printf("WiFi Status: %d\n", WiFi.status());
        return;
    }
    Serial.println("✓ WiFi connected: " + WiFi.localIP().toString());
    
    // Check LINE settings
    if (String(line_channel_access_token) == "YOUR_CHANNEL_ACCESS_TOKEN" || 
        strlen(line_channel_access_token) < 10) {
        Serial.println("ERROR: LINE Channel Access Token not configured!");
        Serial.println("Please update config.h with your actual token");
        return;
    }
    Serial.println("✓ Channel Access Token configured (length: " + String(strlen(line_channel_access_token)) + ")");
    
    if (String(line_user_id) == "YOUR_USER_ID" || 
        strlen(line_user_id) < 10 || 
        line_user_id[0] != 'U') {
        Serial.println("ERROR: LINE User ID not configured or invalid!");
        Serial.println("User ID should start with 'U' and be 33 characters long");
        Serial.printf("Current User ID: %s (length: %d)\n", line_user_id, strlen(line_user_id));
        return;
    }
    Serial.printf("✓ User ID configured: %s\n", line_user_id);
    
    // Check enable flag
    if (!enable_line_messaging) {
        Serial.println("WARNING: LINE messaging is disabled in config.h");
        return;
    }
    Serial.println("✓ LINE messaging enabled");
    
    // Prepare HTTP request
    HTTPClient http;
    http.setTimeout(10000); // 10 second timeout
    
    Serial.println("Connecting to LINE API...");
    if (!http.begin("https://api.line.me/v2/bot/message/push")) {
        Serial.println("ERROR: Failed to begin HTTP connection");
        return;
    }
    Serial.println("✓ HTTP connection established");
    
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(line_channel_access_token));
    Serial.println("✓ Headers set");
    
    // Create JSON payload
    JsonDocument doc;
    doc["to"] = line_user_id;
    
    JsonArray messages = doc["messages"].to<JsonArray>();
    JsonObject textMessage = messages.add<JsonObject>();
    textMessage["type"] = "text";
    textMessage["text"] = message;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.println("JSON Payload:");
    Serial.println(jsonString);
    Serial.printf("Payload size: %d bytes\n", jsonString.length());
    
    // Send POST request
    Serial.println("Sending POST request...");
    int httpResponseCode = http.POST(jsonString);
    
    Serial.printf("HTTP Response Code: %d\n", httpResponseCode);
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Response body:");
        Serial.println(response);
        
        if (httpResponseCode == 200) {
            Serial.println("✓✓✓ LINE message sent successfully! ✓✓✓");
        } else if (httpResponseCode == 400) {
            Serial.println("ERROR: Bad Request (400) - Check JSON format and User ID");
        } else if (httpResponseCode == 401) {
            Serial.println("ERROR: Unauthorized (401) - Check Channel Access Token");
        } else if (httpResponseCode == 403) {
            Serial.println("ERROR: Forbidden (403) - Check permissions and User ID");
        } else if (httpResponseCode == 429) {
            Serial.println("ERROR: Rate Limit (429) - Too many requests");
        } else {
            Serial.printf("ERROR: Unexpected response code: %d\n", httpResponseCode);
        }
    } else {
        Serial.printf("ERROR: HTTP POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    
    http.end();
    Serial.println("=== LINE Message Debug End ===\n");
}

String getStreamingURL() {
    if (WiFi.status() == WL_CONNECTED) {
        return "http://" + WiFi.localIP().toString() + "/stream";
    }
    return "Stream unavailable";
}

void handleRoot(AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>XIAO ESP32S3 防犯カメラ</title>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;}";
    html += "h1{color:#333;}";
    html += ".container{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
    html += ".button{display:inline-block;padding:10px 20px;margin:5px;background:#007bff;color:white;text-decoration:none;border-radius:4px;}";
    html += ".button:hover{background:#0056b3;}";
    html += ".status{margin:20px 0;padding:15px;background:#e9ecef;border-radius:4px;}";
    html += "img{max-width:100%;border:2px solid #ddd;border-radius:4px;margin:10px 0;}";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>🎥 XIAO ESP32S3 防犯カメラシステム</h1>";
    html += "<div class='status'>";
    html += "<p><strong>状態:</strong> " + String(current_state == STATE_IDLE ? "待機中" : 
                                                   current_state == STATE_DETECTING ? "検出中" :
                                                   current_state == STATE_ALERT_ACTIVE ? "アラート作動中" :
                                                   current_state == STATE_RECORDING ? "録画中" : "初期化中") + "</p>";
    html += "<p><strong>アラート:</strong> " + String(alert_system.alert_triggered ? "作動中" : "停止") + "</p>";
    html += "<p><strong>録画:</strong> " + String(recording_system.recording_active ? "録画中" : "停止") + "</p>";
    if (!isnan(sht30_last_temperature)) {
        html += "<p><strong>温度:</strong> " + String(sht30_last_temperature, 2) + " ℃</p>";
        html += "<p><strong>湿度:</strong> " + String(sht30_last_humidity, 2) + " %</p>";
    } else {
        html += "<p><strong>温湿度:</strong> 取得待ち</p>";
    }
    html += "<p><strong>稼働時間:</strong> " + String(millis() / 1000) + " 秒</p>";
    html += "<p><strong>空きメモリ:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";
    html += "</div>";
    html += "<h2>操作</h2>";
    html += "<a href='/stream' class='button'>📹 ライブストリーミング</a>";
    html += "<a href='/capture' class='button'>📸 静止画キャプチャ</a>";
    html += "<a href='/status' class='button'>📊 システム状態 (JSON)</a>";
    html += "<br><br>";
    // ポンプ制御UI
    if (enable_pump_control) {
        bool pump_on = (digitalRead(PUMP_MOSFET_PIN) == HIGH);
        html += "<h2>💧 ポンプ制御</h2>";
        html += "<div class='status' style='background:" + String(pump_on ? "#d4edda" : "#f8d7da") + ";'>";
        html += "<p><strong>ポンプ状態:</strong> " + String(pump_on ? "<span style='color:#155724;'>ON (動作中)</span>" : "<span style='color:#721c24;'>OFF (停止)</span>") + "</p>";
        html += "</div>";
        html += "<button onclick=\"fetch('/pump-on').then(()=>location.reload())\" class='button' style='background:#28a745;'>💧 ポンプ ON</button>";
        html += "<button onclick=\"fetch('/pump-off').then(()=>location.reload())\" class='button' style='background:#dc3545;'>⛔ ポンプ OFF</button>";
        html += "<p style='font-size:0.85em;color:#666;'>※ ONにすると最大" + String(pump_max_duration_ms / 1000) + "秒で自動停止します</p>";
    }
    html += "<h2>🔧 デバッグ機能</h2>";
    html += "<a href='/line-config' class='button' style='background:#28a745;'>🔍 LINE設定確認</a>";
    html += "<a href='/test-line' class='button' style='background:#ffc107;color:#000;'>📱 LINE通知テスト</a>";
    html += "<h2>ライブ映像プレビュー</h2>";
    html += "<img src='/capture' id='preview' />";
    html += "<script>setInterval(()=>{document.getElementById('preview').src='/capture?t='+Date.now()},2000);</script>";
    html += "</div></body></html>";
    
    request->send(200, "text/html", html);
}

void handleStream(AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>カメラストリーミング</title>";
    html += "<style>";
    html += "body{font-family:Arial;margin:20px;text-align:center;background:#000;}";
    html += ".video-container{position:relative;display:inline-block;max-width:100%;}";
    html += "img{max-width:100%;border:2px solid #333;display:block;}";
    html += ".timestamp{position:absolute;top:10px;left:10px;";
    html += "background:rgba(0,0,0,0.7);color:#fff;padding:5px 10px;";
    html += "font-family:monospace;font-size:14px;border-radius:3px;}";
    html += ".info{color:#fff;margin-top:10px;}";
    html += "a{color:#4CAF50;text-decoration:none;}";
    html += "</style></head><body>";
    html += "<h1 style='color:#fff;'>リアルタイムカメラ映像</h1>";
    html += "<div class='video-container'>";
    html += "<img id='stream' src='/capture'/>";
    html += "<div class='timestamp' id='timestamp'></div>";
    html += "</div>";
    html += "<div class='info'>約1fps（1秒間隔）で更新 - 動体検知優先</div>";
    html += "<p><a href='/'>← 戻る</a></p>";
    html += "<script>";
    html += "function updateTime(){";
    html += "const now=new Date();";
    html += "const y=now.getFullYear();";
    html += "const m=String(now.getMonth()+1).padStart(2,'0');";
    html += "const d=String(now.getDate()).padStart(2,'0');";
    html += "const h=String(now.getHours()).padStart(2,'0');";
    html += "const min=String(now.getMinutes()).padStart(2,'0');";
    html += "const s=String(now.getSeconds()).padStart(2,'0');";
    html += "document.getElementById('timestamp').textContent=y+'/'+m+'/'+d+' '+h+':'+min+':'+s;";
    html += "}";
    html += "setInterval(function(){";
    html += "document.getElementById('stream').src='/capture?t='+Date.now();";
    html += "updateTime();";
    html += "},1000);"; // 1000ms = 1fps（動体検知との競合回避）;
    html += "updateTime();";
    html += "</script>";
    html += "</body></html>";
    
    request->send(200, "text/html", html);
}

void handleCapture(AsyncWebServerRequest *request) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        AsyncWebServerResponse *response = request->beginResponse(200, "image/jpeg", fb->buf, fb->len);
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