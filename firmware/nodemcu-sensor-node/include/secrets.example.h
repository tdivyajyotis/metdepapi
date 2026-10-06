#pragma once

// Copy this file to include/secrets.h and fill in the values. secrets.h is
// ignored by git. Keep the API token unique to this device.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"
#define DEVICE_API_TOKEN "replace-with-a-long-random-device-token"

// Normal operation uses the Mozilla CA store uploaded to LittleFS. If that
// store cannot be loaded, the firmware falls back to its compiled GTS Root R4.
// Define TLS_ROOT_CA_PEM_OVERRIDE only to replace that fallback. The value must
// be a complete PEM root certificate.
// #define TLS_ROOT_CA_PEM_OVERRIDE R"EOF(
// -----BEGIN CERTIFICATE-----
// ...
// -----END CERTIFICATE-----
// )EOF"

// Development escape hatch only. A value of 1 encrypts traffic but does not
// authenticate the server, so it is vulnerable to a man-in-the-middle attack.
#define ALLOW_INSECURE_TLS 0

