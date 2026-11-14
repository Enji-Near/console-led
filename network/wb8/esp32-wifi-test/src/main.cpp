// Join Console-Net and report once a connection is up, then every 5 s:
// which AP (BSSID), channel, signal, DNS and plain internet reachability.
#include <WiFi.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PASS. Gitignored; copy secrets.h.example.

static void report() {
  IPAddress ip;
  bool dns = WiFi.hostByName("openwrt.org", ip);
  WiFiClient c;
  bool net = c.connect(IPAddress(1, 1, 1, 1), 80, 3000);  // no DNS needed
  c.stop();
  Serial.printf("rssi=%d dBm  dns=%s  internet=%s\n", WiFi.RSSI(),
                dns ? "ok" : "FAIL", net ? "ok" : "FAIL");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("\nJoining %s", WIFI_SSID);
  for (int i = 0; WiFi.status() != WL_CONNECTED; i++) {
    if (i == 60) {  // 30 s
      Serial.printf("\nFAILED to join, status=%d\n", WiFi.status());
      return;
    }
    delay(500);
    Serial.print('.');
  }
  Serial.printf("\nCONNECTED ip=%s gw=%s bssid=%s channel=%d\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.BSSIDstr().c_str(), WiFi.channel());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) report();
  else Serial.printf("not connected, status=%d\n", WiFi.status());
  delay(5000);
}
