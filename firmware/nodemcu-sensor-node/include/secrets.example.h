#pragma once

// Copy this file to include/secrets.h and fill in the values. secrets.h is
// ignored by git. Keep the API token unique to this device.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"
#define DEVICE_API_TOKEN "replace-with-a-long-random-device-token"

// For an HTTPS endpoint, paste the PEM root CA that validates the endpoint.
// Cloudflare may use different public CAs depending on your certificate setup,
// so obtain this from the certificate chain actually served by your hostname.
#define TLS_ROOT_CA_PEM R"EOF(
-----BEGIN CERTIFICATE-----
paste-root-ca-here
-----END CERTIFICATE-----
)EOF"

// Development escape hatch only. A value of 1 encrypts traffic but does not
// authenticate the server, so it is vulnerable to a man-in-the-middle attack.
#define ALLOW_INSECURE_TLS 0

