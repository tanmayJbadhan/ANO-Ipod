#include <Arduino.h>

// --- GPIO Definitions ---
const int TACH_PIN = 4;   // Yellow wire
const int PWM_PIN = 13;   // Brown wire
const int FLOW_PIN = 6;   // Flow sensor Vout

// --- PWM Settings ---
const int pwmFreq = 25000;    
const int pwmChannel = 0;     
const int pwmResolution = 8;  

// --- Advanced PID + Feed-Forward Variables ---
float setpoint = 0.15;
float Kp = 250.0;    
float Ki = 80.0;     
float Kd = 15.0;     
float Kf = 400.0;    // Feed-Forward base

float lastError = 0;
float integralError = 0;
float currentPWM = 0;
unsigned long lastPIDTime = 0;

// --- Tachometer Variables ---
volatile unsigned int pulseCount = 0;
unsigned int rpm = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Interrupt Service Routine
void IRAM_ATTR countPulses() {
  portENTER_CRITICAL_ISR(&mux);
  pulseCount++;
  portEXIT_CRITICAL_ISR(&mux);
}

// --- Flow Sensor Interpolation Function ---
float calculateFlowRate(float voltage) {
  if (voltage <= 0.50) return 0.0;
  else if (voltage <= 1.60) return 0.0 + (voltage - 0.50) * (0.25 - 0.0) / (1.60 - 0.50);
  else if (voltage <= 2.10) return 0.25 + (voltage - 1.60) * (0.50 - 0.25) / (2.10 - 1.60);
  else if (voltage <= 2.31) return 0.50 + (voltage - 2.10) * (0.75 - 0.50) / (2.31 - 2.10);
  else if (voltage <= 2.50) return 0.75 + (voltage - 2.31) * (1.00 - 0.75) / (2.50 - 2.31);
  else return 1.00;
}

void setup() {
  Serial.begin(115200);
  
  // 1. Tachometer Setup
  pinMode(TACH_PIN, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(TACH_PIN), countPulses, FALLING);
  
  // 2. PWM Setup
  ledcSetup(pwmChannel, pwmFreq, pwmResolution);
  ledcAttachPin(PWM_PIN, pwmChannel);
  
  Serial.println("System Ready - Advanced Flow Control Active");
}

void loop() {
  unsigned long currentTime = millis();
  
  // High-speed 100ms control loop
  if (currentTime - lastPIDTime >= 100) { 
    float dt = (currentTime - lastPIDTime) / 1000.0;

    // A. Sensing
    int rawADC = analogRead(FLOW_PIN);
    float voltage = (rawADC / 4095.0) * 3.3;
    float currentFlow = calculateFlowRate(voltage);
    float error = setpoint - currentFlow;

    // B. PID + Feed-Forward Math
    float pOut = Kp * error;
    integralError = constrain(integralError + (error * dt), -30, 30);
    float iOut = Ki * integralError;
    float dOut = Kd * ((error - lastError) / dt);
    float ffOut = setpoint * Kf; 

    // C. Actuation
    currentPWM = constrain(ffOut + pOut + iOut + dOut, 0, 255);
    ledcWrite(pwmChannel, (int)currentPWM);

    // D. RPM Calculation
    portENTER_CRITICAL(&mux);
    unsigned int pulses = pulseCount;
    pulseCount = 0;
    portEXIT_CRITICAL(&mux);
    rpm = pulses * 300; 

    // E. Debugging (Prints every 500ms to avoid Serial lag)
    if (currentTime % 500 < 100) {
      Serial.printf("Flow: %.3f L/min | PWM: %d | RPM: %d\n", currentFlow, (int)currentPWM, rpm);
    }

    lastError = error;
    lastPIDTime = currentTime;
  }
}