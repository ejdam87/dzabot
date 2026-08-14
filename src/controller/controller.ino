#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

#define VRX_PIN 2   // ADC pin connected to VRx (change to match your wiring)
#define VRY_PIN 1   // ADC pin connected to VRy
#define SW_PIN  0   // Digital pin connected to SW (button)


// --- Wireless communication protocol -----
uint8_t receiverMAC[] = {0xFC, 0x01, 0x2C, 0xCA, 0xA3, 0xC0};

void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sent OK" : "Send Fail");
}

typedef struct joystick_command {
  bool left;
  bool right;
  bool forward;
  bool backward;
  bool brake;
} joystick_command;

joystick_command joystick_data;


// ---------- Joystick calibration & thresholds ----------
int centerX = 3800;
int centerY = 3600;

const int CENTER_TOLERANCE = 200;  // no motion if within this of center
const int EDGE_HIGH = 4095;        // expected max reading
const int EDGE_LOW  = 50;          // expected min reading
const int EDGE_TOLERANCE = 100;    // how close to the edge counts as "deflected"

void calibrateJoystick() {
  long sumX = 0, sumY = 0;
  const int samples = 20;
  for (int i = 0; i < samples; i++) {
    sumX += analogRead(VRX_PIN);
    sumY += analogRead(VRY_PIN);
    delay(5);
  }
  centerX = sumX / samples;
  centerY = sumY / samples;

  Serial.print(F("Calibrated center -> X: "));
  Serial.print(centerX);
  Serial.print(F(" | Y: "));
  Serial.println(centerY);
}

// Returns +1 if deflected toward EDGE_HIGH, -1 if deflected toward
// EDGE_LOW, 0 if near center or in the ambiguous zone between.
int axisDirection(int raw, int center) {
  if (abs(raw - center) <= CENTER_TOLERANCE) return 0;
  if (abs(raw - EDGE_HIGH) <= EDGE_TOLERANCE) return 1;
  if (abs(raw - EDGE_LOW)  <= EDGE_TOLERANCE) return -1;
  return 0;  // in between center and edge — treat as no motion
}

void readJoystick() {
  int sw = digitalRead(SW_PIN);
  int dx = analogRead(VRX_PIN);
  int dy = analogRead(VRY_PIN);

  int xDir = axisDirection(dx, centerX);
  int yDir = axisDirection(dy, centerY);

  joystick_data.left     = (xDir == -1);
  joystick_data.right    = (xDir == 1);
  joystick_data.forward  = (yDir == -1);
  joystick_data.backward = (yDir == 1);
  joystick_data.brake    = (sw == LOW);

  // --- Debug print ---
  Serial.print(F("L:"));  Serial.print(joystick_data.left);
  Serial.print(F(" R:")); Serial.print(joystick_data.right);
  Serial.print(F(" F:")); Serial.print(joystick_data.forward);
  Serial.print(F(" B:")); Serial.print(joystick_data.backward);
  Serial.print(F(" BRK:")); Serial.print(joystick_data.brake);
  Serial.print(F(" | raw X:")); Serial.print(dx);
  Serial.print(F(" Y:")); Serial.println(dy);
}



void setup() {
  Serial.begin(115200);

  // --- ESP-NOW init
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Sender Channel: ");
  Serial.println(WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);

  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
  // ---

  // --- Joystick init
  pinMode(VRX_PIN, INPUT);
  pinMode(VRY_PIN, INPUT);
  pinMode(SW_PIN, INPUT_PULLUP);   // SW is usually active LOW, needs pull-up

  calibrateJoystick();
  // ---

}

void loop() {
  readJoystick();

  esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)&joystick_data, sizeof(joystick_data));

  /*
  if (result == ESP_OK) {
    Serial.println("Send queued successfully");
  } else {
    Serial.print("Send error: ");
    Serial.println(esp_err_to_name(result));
  }
  */

  delay(10);
}
