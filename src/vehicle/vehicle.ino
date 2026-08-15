/*
 * Controlling Car via DRV8833
 * -------------------------------------------
 *
 * Truth table per H-bridge (A: IN1/IN2/OUT1/OUT2, same for B):
 *   IN1  IN2  | OUT1  OUT2  | Function
 *   ---------------------------------
 *    0    0   |  Hi-Z  Hi-Z | Coast (outputs floating — undefined/
 *                              noisy multimeter reading is normal)
 *    1    0   |   H    L    | Forward (duty-cycle on IN1 sets speed)
 *    0    1   |   L    H    | Reverse (duty-cycle on IN2 sets speed)
 *    1    1   |   L    L    | Brake (both outputs shorted to GND —
 *                              NOT to VCC — via the low-side FETs)
 *
 * nFAULT is open-drain, so it needs a pull-up (external, or the
 * ESP32's internal pull-up as configured below) — idles HIGH,
 * drops LOW during an overcurrent/overtemp/undervoltage fault.
 */

// Wireless communication
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"          // controlling camera
#include <WiFiClientSecure.h>    // http(s)
#include <HTTPClient.h>          // http(s)
#include "camera_pin_config.hpp" // camera configuration


// ---------- Pin assignments  ----------
const int PIN_IN1    = 1;
const int PIN_IN2    = 42;
const int PIN_IN3    = 47;
const int PIN_IN4    = 48;
const int PIN_NFAULT = 35;
const int PIN_SLEEP  = 41;

// ---------- PWM configuration ----------
const int PWM_FREQ_HZ   = 20000;   // 20 kHz — above audible range
const int PWM_RES_BITS  = 8;       // 0-255 duty range
const int PWM_MAX_DUTY  = (1 << PWM_RES_BITS) - 1;
const int PWM_PIVOT_DUTY = 200; // dedicated pivot-turn duty — needs more torque than straight driving
const int PWM_COMMON_DUTY = 150;

// ---------- Motor identifiers ----------
enum Motor { MOTOR_A = 0, MOTOR_B = 1 };

// Per-motor IN pin pairs, indexed by Motor enum
const int motorPinIn1[2] = { PIN_IN1, PIN_IN3 };
const int motorPinIn2[2] = { PIN_IN2, PIN_IN4 };

// Current duty cycle remembered per motor (0-255) - higher, faster spinning
int motorDuty[2] = { PWM_COMMON_DUTY, PWM_COMMON_DUTY };


// ---------- Helpers ----------
// which: MOTOR_A or MOTOR_B
// state:  "fwd" | "rev" | "brake" | "coast"
// duty:   0-255, or -1 to reuse the last duty set for this motor
void motor(Motor which, const char* state, int duty = -1) {
  int in1 = motorPinIn1[which];
  int in2 = motorPinIn2[which];

  if (duty >= 0) motorDuty[which] = constrain(duty, 0, PWM_MAX_DUTY);
  int d = motorDuty[which];

  if (!strcmp(state, "fwd")) {
    analogWrite(in1, d);
    analogWrite(in2, 0);
  } else if (!strcmp(state, "rev")) {
    analogWrite(in1, 0);
    analogWrite(in2, d);
  } else if (!strcmp(state, "brake")) {
    // Both HIGH -> both low-side FETs on -> outputs shorted to GND
    analogWrite(in1, PWM_MAX_DUTY);
    analogWrite(in2, PWM_MAX_DUTY);
  } else if (!strcmp(state, "coast")) {
    // Both LOW -> all FETs off -> outputs floating (Hi-Z)
    analogWrite(in1, 0);
    analogWrite(in2, 0);
  }
}


void printFault() {
  int v = digitalRead(PIN_NFAULT);
  Serial.print(F("nFAULT = "));
  Serial.println(v == LOW ? F("LOW  -> FAULT ACTIVE") : F("HIGH -> OK"));
}

// ----- Camera ------
WebServer server(80);

void handleRoot() {
  const char* html =
    "<html><body style='margin:0;background:#111;text-align:center;'>"
    "<h3 style='color:#eee;font-family:sans-serif;'>Vehicle Camera</h3>"
    "<img src=\"/photo\" style=\"max-width:100%;\">"
    "</body></html>";
  server.send(200, "text/html", html);
}

void handlePhoto() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);

  esp_camera_fb_return(fb);
}

void initWebServer() {
  server.on("/", handleRoot);
  server.on("/photo", handlePhoto);
  server.begin();
  Serial.println("Web server started");
}

void initCamera() {
	Serial.println("Camera init starting...");

	camera_config_t config;

	// --- 1 byte parallel bus
	config.pin_d0 = Y2_GPIO_NUM;
	config.pin_d1 = Y3_GPIO_NUM;
	config.pin_d2 = Y4_GPIO_NUM;
	config.pin_d3 = Y5_GPIO_NUM;
	config.pin_d4 = Y6_GPIO_NUM;
	config.pin_d5 = Y7_GPIO_NUM;
	config.pin_d6 = Y8_GPIO_NUM;
	config.pin_d7 = Y9_GPIO_NUM;
	// ---

	// --- synchronization, timing, communication
	config.ledc_channel = LEDC_CHANNEL_0;
	config.ledc_timer = LEDC_TIMER_0;
	config.pin_href = HREF_GPIO_NUM;
	config.pin_xclk = XCLK_GPIO_NUM;
	config.pin_pclk = PCLK_GPIO_NUM;
	config.pin_vsync = VSYNC_GPIO_NUM;
	config.pin_sscb_sda = SIOD_GPIO_NUM;
	config.pin_sccb_scl = SIOC_GPIO_NUM;
	config.pin_pwdn = PWDN_GPIO_NUM;
	config.pin_reset = RESET_GPIO_NUM;
	config.xclk_freq_hz = 10000000;
	// ---

	config.pixel_format = PIXFORMAT_JPEG;
	config.fb_location = CAMERA_FB_IN_PSRAM;

	if (psramFound()) {
		config.frame_size = FRAMESIZE_VGA; // bigger resolution
		config.jpeg_quality = 12; // 0 (best) - 63 (worst)
		config.fb_count = 2; // double frame buffer
		config.fb_location = CAMERA_FB_IN_PSRAM; // use PSRAM
	} else {
		config.frame_size = FRAMESIZE_VGA; // smaller resolution
		config.jpeg_quality = 15; // 0 (best) - 63 (worst)
		config.fb_count = 1; // one frame buffer
		config.fb_location = CAMERA_FB_IN_DRAM;
	}

	config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // capture new frame only when older is processed

	Serial.println("Calling esp_camera_init...");
	esp_err_t err = esp_camera_init(&config);

	Serial.print("Camera init result: ");
	Serial.println(esp_err_to_name(err));

	if (err != ESP_OK) {
		Serial.println("Camera INIT FAILED (pinout or sensor issue)");
		return;
	}

	Serial.println("Camera INIT SUCCESS");
}

// ---- Wireless communication protocol --------
typedef struct joystick_command {
  bool left;
  bool right;
  bool forward;
  bool backward;
  bool brake;
} joystick_command;

joystick_command joystick_data;

void printJoystickCommand() {
  Serial.print(F("L:"));  Serial.print(joystick_data.left);
  Serial.print(F(" R:")); Serial.print(joystick_data.right);
  Serial.print(F(" F:")); Serial.print(joystick_data.forward);
  Serial.print(F(" B:")); Serial.print(joystick_data.backward);
  Serial.print(F(" BRK:")); Serial.println(joystick_data.brake);
}

// ---------- Speed tuning ----------
const float TURN_SPEED_PIVOT_RATE = 0.6; // inner wheel speed (w.r.t outer wheel) for a gentle pivot (outer is full)

void applyJoystickCommand() {
  // Brake takes priority over everything else
  if (joystick_data.brake) {
    motor(MOTOR_A, "brake");
    motor(MOTOR_B, "brake");
    return;
  }

  bool turning = joystick_data.left || joystick_data.right;
  bool driving = joystick_data.forward || joystick_data.backward;

  if (!turning && !driving) {
    // Nothing pressed -> coast
    motor(MOTOR_A, "coast");
    motor(MOTOR_B, "coast");
    return;
  }

  if (driving && !turning) {
    // Straight forward/backward, both wheels equal speed
    const char* dir = joystick_data.forward ? "fwd" : "rev";
    motor(MOTOR_A, dir, PWM_COMMON_DUTY);
    motor(MOTOR_B, dir, PWM_COMMON_DUTY);
    return;
  }

  if (turning && !driving) {
    // Pivot turn in place: wheels spin opposite directions
    if (joystick_data.left) {
      motor(MOTOR_A, "rev", PWM_PIVOT_DUTY);
      motor(MOTOR_B, "fwd", PWM_PIVOT_DUTY);
    } else { // right
      motor(MOTOR_A, "fwd", PWM_PIVOT_DUTY);
      motor(MOTOR_B, "rev", PWM_PIVOT_DUTY);
    }
    return;
  }

  // Driving + turning together: curve by slowing the inner wheel,
  // keeping the outer wheel at full speed, both same direction.
  const char* dir = joystick_data.forward ? "fwd" : "rev";
  if (joystick_data.left) {
    motor(MOTOR_A, dir, PWM_COMMON_DUTY * TURN_SPEED_PIVOT_RATE); // inner (left) wheel slower
    motor(MOTOR_B, dir, PWM_COMMON_DUTY);  // outer (right) wheel faster
  } else { // right
    motor(MOTOR_A, dir, PWM_COMMON_DUTY);  // outer (left) wheel faster
    motor(MOTOR_B, dir, PWM_COMMON_DUTY * TURN_SPEED_PIVOT_RATE); // inner (right) wheel slower
  }
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *bytes_received, int len) {
  memcpy(&joystick_data, bytes_received, sizeof(joystick_data));
  // Serial.println("Received");
  // printJoystickCommand();
  applyJoystickCommand();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // Wireless setup
  WiFi.mode(WIFI_AP_STA);                    // AP for camera clients + STA for ESP-NOW
  WiFi.softAP("VehicleCamera", "password", 1);  // force AP onto channel 1
  delay(100);                                // give WiFi driver time to initialize before reading MAC
  WiFi.disconnect();
  WiFi.setSleep(false);

  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  initCamera();
  initWebServer();

  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Receiver Channel: ");
  Serial.println(WiFi.channel());

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);
  esp_now_register_recv_cb(onReceive);

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  pinMode(PIN_NFAULT, INPUT_PULLUP);
  pinMode(PIN_SLEEP, OUTPUT);

  // active driver
  digitalWrite(PIN_SLEEP, HIGH);

  // Start safe: both motors coasting
  motor(MOTOR_A, "coast");
  motor(MOTOR_B, "coast");

  printFault();
}

void loop() {
  server.handleClient();
}
