ESP32 Access Control Production Firmware

Features:
- Wiegand 26 input
- File-based storage (LittleFS)
- REST API
- WebSocket real-time
- Web dashboard
- OTA ready
- Ethernet connectivity
- WiFi with DPP Enrollee (Wi-Fi Easy Connect) provisioning

Build:
idf.py build
idf.py flash monitor

## WiFi DPP Provisioning

The device supports Device Provisioning Protocol (DPP) enrollee mode for
WiFi provisioning. When powered on, the device starts a DPP enrollee that
generates a QR code. Scan the QR code with a DPP configurator (e.g. Android
Wi-Fi Easy Connect) to provision WiFi credentials.

The DPP QR code and WiFi status are accessible via the HTTP API:
- `POST /dpp/bootstrap` - Regenerate the DPP bootstrap/QR code

WiFi status updates are pushed to the web dashboard via WebSocket broadcast.
The web dashboard also includes a WiFi/DPP tab that renders the QR code.
