#include <WiFi.h>
#include <WiFiUdp.h>

extern "C" {
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
}

// NOTE: Use the example file to build your own variables.h
#include "variables.h"

#ifndef variables_h
#error variables.h needs to be loaded and to include required variables
#endif

const char *wf_networkSSID = WIFI_SSID;
const char *wf_networkPswd = WIFI_SHAREDKEY;

IPAddress lx_broadcastIP(255, 255, 255, 255);

uint8_t lx_targetDevice[] = LIFX_DEVICE_MAC;

unsigned int lx_broadcastPort = 56700;
unsigned int lx_packetBufferLength = 300;
unsigned int lx_addressLength = 6;

unsigned int lx_type_getService = 2;
unsigned int lx_type_stateService = 3;
unsigned int lx_type_getPower = 20;
unsigned int lx_type_setPower = 21;
unsigned int lx_type_statePower = 22;

/*
  NOTE: This struct has been a bit dubious. Conflicting references in official and unofficial examples.
        May be an issue with the output not being little-endian? Needs to be tested.
*/
#pragma pack(push, 1)
typedef struct {
  /* FRAME */
  uint16_t size:16;
  uint16_t protocol:12;
  uint8_t  addressable:1;
  uint8_t  tagged:1;
  uint8_t  origin:2;
  uint32_t source:32;
  /* FRAME ADDRESS */
  uint8_t  reserved_a[8];
  uint8_t  target[6];
  uint8_t  res_required:1;
  uint8_t  ack_required:1;
  uint8_t  reserved_b:6;
  uint8_t  sequence:8;
  /* PROTOCOL */
  uint64_t reserved_c:64;
  uint16_t type:16;
  uint16_t reserved_d:16;
  /* PAYLOAD FOLLOWS */
} lx_header;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  uint16_t level:16;
} lx_payload_setPower;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  uint16_t level:16;
} lx_payload_statePower;
#pragma pack(pop)

unsigned long lx_powerLevel_On = 65535;
unsigned long lx_powerLevel_Off = 0;

boolean wf_connected = false;
int wf_status = WL_DISCONNECTED;
IPAddress wf_localIP;

WiFiUDP lx_udp;

boolean lx_broadcasting = false;

boolean lx_knownPowerState = false;
boolean lx_powerOn = false;

const int btn_pin = 0;
const int led_pin = 5;

int btn_state;
int btn_lastState = HIGH;
unsigned long btn_lastDebounceTime = 0;
unsigned long btn_debounceDelay = 50;

void setup() {
  if (IS_DEBUG) { Serial.begin(115200); Serial.print("\n[setup] SSID: "); Serial.print(WIFI_SSID); Serial.println(""); }
  
  pinMode(btn_pin, INPUT_PULLUP);
  pinMode(led_pin, OUTPUT);

//  NOTE: This raises a `esp_wifi_set_config 969 wifi is not init`
//  wf_setup();
}

void wf_setup() {
   if(esp_wifi_set_ps(WIFI_PS_MODEM) != ESP_OK) { Serial.println("FAIL SET PS"); }
}

void lx_setup() {
  if (lx_broadcasting || wf_status != WL_CONNECTED) { return; }
  lx_udp.stop();
  lx_udp.begin(wf_localIP, lx_broadcastPort);
  lx_broadcasting = true;
  delay(60);
}

void loop() {
  
  if (btn_didChangeState()) {
    if (btn_state == LOW) { // Down
      lx_togglePower();
      digitalWrite(led_pin, HIGH);
    } else { // Up
      digitalWrite(led_pin, LOW);
    }
  }

}

// MARK: - LIFX functions

void lx_togglePower() {
  wf_waitUntilReady();
  if (wf_status != WL_CONNECTED) {
    // TODO Failure state
    return;
  }

  if (IS_DEBUG) { Serial.println("[LIFX] Toggle power."); }
  
  lx_setup();
  
  boolean powerOn = !lx_powerOn;
  
  int sequence = lx_setPower(lx_targetDevice, powerOn);
  
  lx_payload_statePower *response = lx_waitForPowerStateResponse(lx_targetDevice, sequence);

  if (response) {
    Serial.println("[LIFX] Got response from device.");
  }

  lx_powerOn = powerOn;
  
}

int lx_sequence = 1;

int lx_setPower(uint8_t *target, boolean powerOn) {

  if (IS_DEBUG) { Serial.print("[LIFX] Set Power: "); Serial.println(powerOn); }
  
  lx_header header;
  lx_payload_setPower payload;
  memset(&header, 0, sizeof(header));
  memset(&payload, 0, sizeof(payload));
  
  memcpy(header.target, target, sizeof(uint8_t) * lx_addressLength);

  int sequence = lx_sequence;
  
  header.size = sizeof(lx_header) + sizeof(lx_payload_setPower);
  header.tagged = 0;
  header.addressable = 1;
  header.protocol = 1024;
  header.source = 0;
  header.sequence = sequence;
  header.ack_required = false;
  header.res_required = true;
  header.type = lx_type_setPower;

  if (powerOn) {
    payload.level = lx_powerLevel_On;
  } else {
    payload.level = lx_powerLevel_Off;
  }
  
//  payload.duration = 300;

  lx_udp.beginPacket(lx_broadcastIP, lx_broadcastPort);
  lx_udp.write((uint8_t *)&header, sizeof(lx_header));
  lx_udp.write((uint8_t *)&payload, sizeof(lx_payload_setPower));
  lx_udp.endPacket();

  if (IS_DEBUG) { Serial.println("[LIFX] Sent set power."); }

  lx_sequence++;

  return sequence;
}

unsigned long lx_waitTimeout = 500;

lx_payload_statePower *lx_waitForPowerStateResponse(uint8_t *target, int sequence) {

  unsigned long started = millis();

  lx_payload_statePower *response;
  
  while (millis() - started < lx_waitTimeout) {
    int _length = lx_udp.parsePacket();
    byte _buffer[lx_packetBufferLength];
    
    if (_length && _length < lx_packetBufferLength) {
      lx_udp.read(_buffer, sizeof(_buffer));
      lx_header *packet = (lx_header *)_buffer;
      lx_payload_statePower *payload = (lx_payload_statePower *)(_buffer + sizeof(lx_header));

      if (IS_DEBUG) {
        Serial.print("[Debug] Power state payload ");
        Serial.print(_buffer[_length - 2]); Serial.print(" -- "); Serial.print(_buffer[_length - 1]);
        Serial.print(" -- "); Serial.print(sizeof(lx_payload_statePower)); Serial.println("");
      }

      if (packet->target == target && packet->sequence == sequence) {
        response = payload;
        break;
      }
    }
  }

  return response;
}

// MARK: - WiFi functions

void wf_connectToNetwork(const char *ssid, const char *pwd) {
  WiFi.disconnect(true);
  WiFi.onEvent(wf_handleEvent);
  WiFi.begin(ssid, pwd);
  if(IS_DEBUG) { Serial.print("[WiFi] Connecting to "); Serial.println(ssid); }
}

void wf_handleEvent(WiFiEvent_t event) {
  switch(event) {
    case SYSTEM_EVENT_STA_CONNECTED:
      if(IS_DEBUG) { Serial.println("[WiFi] Connected..."); }
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      if(IS_DEBUG) { Serial.print("[WiFi] IP Address: "); Serial.println(WiFi.localIP()); }
      wf_localIP = WiFi.localIP();
      wf_connected = true;
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      if(IS_DEBUG) { Serial.println("[WiFi] Disconnected"); }
      wf_connected = false;
      break;
  }
  
  wf_status = WiFi.status();
  
}

unsigned long wf_waitTimeoutDelay = 4000;

void wf_waitUntilReady() {
  if (wf_status == WL_CONNECTED) { return; }

  wf_connectToNetwork(wf_networkSSID, wf_networkPswd);
  
  unsigned long connectionTimeout = millis() + wf_waitTimeoutDelay;
  while(wf_status != WL_CONNECTED) {
    if (millis() > connectionTimeout) {
      if (IS_DEBUG) { Serial.println("[WiFi] Wait until ready timed out"); }
      break;
    }
    delay(10);
  }
  
}

// MARK: - Button functions

boolean btn_didChangeState() {
  boolean didChange = false;
  
  int reading = digitalRead(btn_pin);
  if (reading != btn_lastState) {
    btn_lastDebounceTime = millis();
  }
  
  if ((millis() - btn_lastDebounceTime) > btn_debounceDelay) {
    if (reading != btn_state) {
      btn_state = reading;
      didChange = true;
    }
  }

  if (IS_DEBUG && didChange) { Serial.println("[Button] Did change"); }
  
  btn_lastState = reading;
  
  return didChange;
}
