#ifndef CONFIG_H
#define CONFIG_H

// WiFi Settings (Update these with your network credentials)
const char* wifi_ssid = "YOUR_WIFI_SSID";
const char* wifi_password = "YOUR_WIFI_PASSWORD";

// LINE Messaging API Settings (Get from LINE Developers Console)
const char* line_channel_access_token = "YOUR_CHANNEL_ACCESS_TOKEN";
const char* line_user_id = "YOUR_USER_ID";  // Target user ID to send messages to

// Device Settings
const char* device_name = "XIAO-ESP32S3-SecurityCam";
const char* device_hostname = "xiaocam";

// Camera Settings
const int camera_frame_size = FRAMESIZE_SVGA;  // 800x600
const int camera_jpeg_quality = 12;  // 0-63, lower means higher quality
const int camera_fb_count = 2;

// AI Detection Settings
const float detection_threshold = 0.7;  // Confidence threshold for person detection
const int detection_cooldown_ms = 5000;  // Minimum time between detections

// Recording Settings
const String recording_path = "/recordings/";
const String recording_format = ".avi";
const int max_recording_files = 50;  // Maximum number of recording files to keep

// Alert Settings
const int buzzer_frequency = 2000;  // Buzzer frequency in Hz
const bool enable_buzzer = true;
const bool enable_led_alerts = true;
const bool enable_line_messaging = true;
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

#endif // CONFIG_H