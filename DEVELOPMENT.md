# XIAO ESP32S3 Security Camera System

## Development Notes

### 2026-01-01 - Major Feature Implementation
- ✅ Implemented motion detection using frame difference algorithm
- ✅ Implemented MJPEG video recording to SD card (30 seconds duration)
- ✅ Implemented real-time MJPEG streaming endpoint
- ✅ Enhanced web interface with live preview and system status
- ✅ Improved state management for concurrent alert and recording
- ✅ Recording saves at 10 FPS to optimize SD card usage
- ✅ Added frame counting and detailed logging

### 2025-10-13 - Initial Project Creation
- Created PlatformIO project structure
- Implemented basic security camera functionality
- Added AI person detection framework (TensorFlow Lite Micro)
- Implemented alert system with buzzer and LED controls
- Added LINE Notify integration
- Created web interface for monitoring
- Implemented video recording framework

### Hardware Configuration
- Board: Seeed Studio XIAO ESP32S3 Sense
- Camera: OV2640 (built-in)
- Storage: microSD card support
- Alert outputs: Buzzer (GPIO 2), LED1 (GPIO 3), LED2 (GPIO 4)

### Software Components
- Framework: Arduino for ESP32
- AI: TensorFlow Lite Micro (placeholder for person detection)
- Web: ESPAsyncWebServer
- Notifications: LINE Notify API
- Storage: SD_MMC for video recording

### Current Implementation Status
- ✅ Basic project structure
- ✅ Camera initialization
- ✅ WiFi connectivity
- ✅ Web server with live streaming endpoint
- ✅ Alert system (buzzer + LED blinking)
- ✅ LINE Messaging API integration
- ✅ Motion detection (frame difference method)
- ✅ Video recording to SD card (MJPEG format)
- ✅ MJPEG streaming
- ✅ Web interface with live preview

### Next Steps
1. Optimize motion detection sensitivity
2. Add advanced AI detection (ESP-WHO or TFLite)
3. Implement recording playback feature
4. Add configuration web interface
5. Optimize power consumption for battery operation
6. Add motion detection zone configuration

### Known Issues
- Motion detection may have false positives - sensitivity tuning needed
- MJPEG file size can grow large - need file rotation
- Memory usage should be monitored during extended operation

### Testing
- Serial output at 115200 baud shows system status
- Web interface available at device IP address
- LINE notifications require valid token configuration
- Alert system can be tested through web interface (planned)