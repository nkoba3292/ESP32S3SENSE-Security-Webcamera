#ifndef CONFIG_H
#define CONFIG_H

// WiFi Settings (Update these with your network credentials)
const char* wifi_ssid = "Rakuten-8A14";
const char* wifi_password = "7RH6L3B655";

// LINE Messaging API Settings (Get from LINE Developers Console)
// IMPORTANT: Replace these with your actual values!
// Channel Access Token: Get from Messaging API settings (長いトークン文字列)
// User ID: Must start with 'U' and be 33 characters long
// See docs/LINE_MESSAGING_API_SETUP.md for detailed setup instructions
// Debug endpoints: http://[device-ip]/line-config and /test-line
const char* line_channel_access_token = "mpqSwAO+SALBKrM7vcWkcvBVZwjBA58y32vF1jt/Emt0ey8RUFcUKbeuWkkXcXiYf9iXDIBDNEGkdlrxc2sCdtn42+dKVvLpvSbdyfhlo2g7vVsrBoGDD037cyPsDII41t9pnR0xgtwMeep4wq9oFAdB04t89/1O/w1cDnyilFU=";
const char* line_user_id = "U0eda9ffb4f6c88ba966addf0f73c7e6f";  // Target user ID to send messages to

// Device Settings
const char* device_name = "XIAO-ESP32S3-SecurityCam";
const char* device_hostname = "xiaocam";

// Camera Settings
const int camera_frame_size = FRAMESIZE_QVGA;  // 320x240 (最も安定)
const int camera_jpeg_quality = 12;  // 0-63, lower = better quality (12=高品質)
const int camera_fb_count = 2;  // 2つのバッファで動体検知と配信を両立

// Motion Detection Settings
const int detection_cooldown_ms = 0;  // クールダウン無効（毎回通知）
const bool pir_active_high = true;    // SB412Aは通常HIGHで検知
const int pir_warmup_ms = 30000;      // 起動直後の安定化待ち時間
const bool enable_pir_monitoring = false; // falseでPIR監視停止（WEB操作のみ運用）

// Motion Detection Fine-tuning
const int motion_diff_threshold = 40;      // Pixel difference threshold
const int motion_sample_threshold = 50;    // Minimum changed samples to trigger
const int motion_min_change_percent = 35;  // Fallback threshold if baseline not established
const bool ignore_frame_size_change = true; // Ignore JPEG compression size variance (recommended: true)

// Recording Settings
const String recording_path = "/recordings/";
const String recording_format = ".avi";
const int max_recording_files = 50;  // Maximum number of recording files to keep
const int recording_fps = 6;  // Target FPS for recording

// Alert Settings
const int buzzer_frequency = 2000;  // HW-s08 passive buzzer frequency in Hz
const bool enable_buzzer = true;
const bool enable_led_alerts = true;
const bool enable_line_messaging = true;   // Enable LINE notifications
const bool enable_recording = true;

// System Settings
const int watchdog_timeout_ms = 30000;  // Watchdog timeout
const bool enable_serial_debug = true;
const int loop_delay_ms = 100;  // Main loop delay

// Web Server Settings
const int web_server_port = 80;
const String stream_path = "/stream";
const String capture_path = "/capture";

// Power Management Settings
const bool enable_power_saving = false;
const int cpu_frequency_mhz = 240;  // CPU frequency

// Time Zone Settings (JST = GMT+9)
const int timezone_offset_hours = 9;
const char* ntp_server = "pool.ntp.org";

// Agriculture IoT Settings (SHT30 & Pump)
const int sht30_i2c_address = 0x44;    // SHT30 I2C address (0x44 with ADDR=Low, 0x45 with ADDR=High)
const int sht30_startup_delay_ms = 50; // SHT30 startup time after power-on
const bool sht30_use_gpio_power = false; // false: use direct 3V3 supply, true: power from GPIO pin
                                         // NOTE: GPIO43 is UART0 TX - do NOT use as power pin while Serial is active
const bool sht30_power_always_on_debug = true; // Keep sensor power ON continuously for wiring/voltage debug
const int sensor_read_interval_ms = 60000;  // Read interval (60 seconds)
const int pump_max_duration_ms = 30000;     // Maximum pump ON duration (30 seconds) - safety cutoff
const bool enable_agriculture_iot = true;   // Enable agriculture IoT module
const bool enable_pump_control = true;      // Enable pump control
const bool force_pump_on_after_init = false; // Force pump gate HIGH continuously after initialization
const int soil_moisture_threshold = 50;     // Threshold for automatic irrigation (placeholder)

#endif // CONFIG_H