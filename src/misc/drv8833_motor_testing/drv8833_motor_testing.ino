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

// ---------- Pin assignments  ----------
const int PIN_IN1    = 13;
const int PIN_IN2    = 12;
const int PIN_IN3    = 11;
const int PIN_IN4    = 10;
const int PIN_NFAULT = 9;
const int PIN_SLEEP  = 8;

// ---------- PWM configuration ----------
const int PWM_FREQ_HZ   = 20000;   // 20 kHz — above audible range
const int PWM_RES_BITS  = 8;       // 0-255 duty range
const int PWM_MAX_DUTY  = (1 << PWM_RES_BITS) - 1;

// Current duty cycle remembered per motor (0-255) - higher, faster spinning
int dutyA = 255;
int dutyB = 255;

// ---------- Helpers ----------
void motorA(const char* state, int duty = -1) {
  if (duty >= 0) dutyA = constrain(duty, 0, PWM_MAX_DUTY);

  if (!strcmp(state, "fwd")) {
    analogWrite(PIN_IN1, dutyA);
    analogWrite(PIN_IN2, 0);
  } else if (!strcmp(state, "rev")) {
    analogWrite(PIN_IN1, 0);
    analogWrite(PIN_IN2, dutyA);
  } else if (!strcmp(state, "brake")) {
    // Both HIGH -> both low-side FETs on -> OUT1/OUT2 shorted to GND
    analogWrite(PIN_IN1, PWM_MAX_DUTY);
    analogWrite(PIN_IN2, PWM_MAX_DUTY);
  } else if (!strcmp(state, "coast")) {
    // Both LOW -> all FETs off -> OUT1/OUT2 floating (Hi-Z)
    analogWrite(PIN_IN1, 0);
    analogWrite(PIN_IN2, 0);
  }
}

void motorB(const char* state, int duty = -1) {
  if (duty >= 0) dutyB = constrain(duty, 0, PWM_MAX_DUTY);

  if (!strcmp(state, "fwd")) {
    analogWrite(PIN_IN3, dutyB);
    analogWrite(PIN_IN4, 0);
  } else if (!strcmp(state, "rev")) {
    analogWrite(PIN_IN3, 0);
    analogWrite(PIN_IN4, dutyB);
  } else if (!strcmp(state, "brake")) {
    // Both HIGH -> both low-side FETs on -> OUT3/OUT4 shorted to GND
    analogWrite(PIN_IN3, PWM_MAX_DUTY);
    analogWrite(PIN_IN4, PWM_MAX_DUTY);
  } else if (!strcmp(state, "coast")) {
    // Both LOW -> all FETs off -> OUT3/OUT4 floating (Hi-Z)
    analogWrite(PIN_IN3, 0);
    analogWrite(PIN_IN4, 0);
  }
}

void printHelp() {
  Serial.println();
  Serial.println(F("=== DRV8833 Test Console ==="));
  Serial.println(F("Motor A (OUT1/OUT2):"));
  Serial.println(F("  af   - forward"));
  Serial.println(F("  ar   - reverse"));
  Serial.println(F("  ab   - brake  (OUT1/OUT2 shorted to GND)"));
  Serial.println(F("  ac   - coast  (OUT1/OUT2 floating/Hi-Z)"));
  Serial.println(F("  aNNN - set Motor A duty 0-255, e.g. a128"));
  Serial.println();
  Serial.println(F("Motor B (OUT3/OUT4):"));
  Serial.println(F("  bf   - forward"));
  Serial.println(F("  br   - reverse"));
  Serial.println(F("  bb   - brake  (OUT3/OUT4 shorted to GND)"));
  Serial.println(F("  bc   - coast  (OUT3/OUT4 floating/Hi-Z)"));
  Serial.println(F("  bNNN - set Motor B duty 0-255, e.g. b064"));
  Serial.println();
  Serial.println(F("Chip control:"));
  Serial.println(F("  f    - read nFAULT pin state"));
  Serial.println(F("  s0   - nSLEEP LOW  (put chip to sleep)"));
  Serial.println(F("  s1   - nSLEEP HIGH (wake chip up)"));
  Serial.println(F("  h    - show this help"));
  Serial.println(F("============================"));
  Serial.println();
}

void printFault() {
  int v = digitalRead(PIN_NFAULT);
  Serial.print(F("nFAULT = "));
  Serial.println(v == LOW ? F("LOW  -> FAULT ACTIVE") : F("HIGH -> OK"));
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  pinMode(PIN_NFAULT, INPUT_PULLUP);
  pinMode(PIN_SLEEP, OUTPUT);

  // active driver
  digitalWrite(PIN_SLEEP, HIGH);

  // Start safe: both motors coasting
  motorA("coast");
  motorB("coast");

  printHelp();
  printFault();
}

void loop() {
  static char buf[16];
  static uint8_t idx = 0;

  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (idx == 0) continue; // ignore blank lines
      buf[idx] = '\0';
      idx = 0;

      // --- Parse command ---
      if (buf[0] == 'h') {
        printHelp();
      } else if (buf[0] == 'f') {
        printFault();
      } else if (buf[0] == 's' && buf[1] == '0') {
        digitalWrite(PIN_SLEEP, LOW);
        Serial.println(F("-> nSLEEP LOW (driver sleeping, outputs disabled)"));
      } else if (buf[0] == 's' && buf[1] == '1') {
        digitalWrite(PIN_SLEEP, HIGH);
        Serial.println(F("-> nSLEEP HIGH (driver awake)"));
      } else if (buf[0] == 'x') {
        motorA("coast");
        motorB("coast");
        Serial.println(F("-> All outputs coasting"));
      } else if (buf[0] == 'a' || buf[0] == 'b') {
        char which = buf[0];
        char* rest = buf + 1;

        if (!strcmp(rest, "f")) {
          if (which == 'a') motorA("fwd"); else motorB("fwd");
          Serial.println(F("-> forward"));
        } else if (!strcmp(rest, "r")) {
          if (which == 'a') motorA("rev"); else motorB("rev");
          Serial.println(F("-> reverse"));
        } else if (!strcmp(rest, "b")) {
          if (which == 'a') motorA("brake"); else motorB("brake");
          Serial.println(F("-> brake"));
        } else if (!strcmp(rest, "c")) {
          if (which == 'a') motorA("coast"); else motorB("coast");
          Serial.println(F("-> coast"));
        } else if (isdigit((unsigned char)rest[0])) {
          int val = atoi(rest);
          val = constrain(val, 0, PWM_MAX_DUTY);
          if (which == 'a') {
            dutyA = val;
            Serial.print(F("-> Motor A duty set to "));
          } else {
            dutyB = val;
            Serial.print(F("-> Motor B duty set to "));
          }
          Serial.print(val);
          Serial.print(F(" / "));
          Serial.println(PWM_MAX_DUTY);
          Serial.println(F("   (send f/r again to apply new duty in that direction)"));
        } else {
          Serial.println(F("Unrecognized command. Send 'h' for help."));
        }
      } else {
        Serial.println(F("Unrecognized command. Send 'h' for help."));
      }
    } else if (idx < sizeof(buf) - 1) {
      buf[idx++] = c;
    }
  }
}
