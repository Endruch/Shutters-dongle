#include "radio.h"

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#include "somfy_decode.h"

static const uint16_t SYMBOL = 604;
static uint8_t pinTx = 4;
static float mhzSomfy = 433.42, mhzAok = 433.92, mhzNow = 0;

static const float TX_RX_BW_KHZ = 812.50;
static const float TX_DRATE_KBAUD = 250.0;

static void tune(float mhz) {
  if (mhz == mhzNow) return;
  ELECHOUSE_cc1101.setMHZ(mhz);
  mhzNow = mhz;
}

static inline void level(bool high, uint32_t us) {
  digitalWrite(pinTx, high);
  uint32_t start = micros();
  while ((uint32_t)(micros() - start) < us) {
  }
}

uint8_t Radio::chipVersion() {
  ELECHOUSE_cc1101.getCC1101();
  uint8_t ver = ELECHOUSE_cc1101.SpiReadStatus(0x31);
  uint8_t part = ELECHOUSE_cc1101.SpiReadStatus(0x30);
  if (ver == 0x00 || ver == 0xFF || part != 0x00) return 0;
  return ver;
}

bool Radio::begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs, uint8_t gdo0, float somfyMhz, float aokMhz) {
  pinTx = gdo0;
  mhzSomfy = somfyMhz;
  mhzAok = aokMhz;
  ELECHOUSE_cc1101.setSpiPin(sck, miso, mosi, cs);
  ELECHOUSE_cc1101.setGDO0(gdo0);
  ELECHOUSE_cc1101.Init();
  bool present = chipVersion() != 0;
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(TX_RX_BW_KHZ);
  ELECHOUSE_cc1101.setDRate(TX_DRATE_KBAUD);
  tune(mhzSomfy);
  ELECHOUSE_cc1101.setPA(10);
  ELECHOUSE_cc1101.SetTx();
  pinMode(pinTx, OUTPUT);
  digitalWrite(pinTx, LOW);
  return present;
}

void Radio::buildFrame(uint8_t frame[7], uint32_t remoteId, uint16_t rollingCode, uint8_t cmd) {
  frame[0] = 0xA7;
  frame[1] = cmd << 4;
  frame[2] = rollingCode >> 8;
  frame[3] = rollingCode & 0xFF;
  frame[4] = remoteId >> 16;
  frame[5] = remoteId >> 8;
  frame[6] = remoteId;

  uint8_t checksum = 0;
  for (uint8_t i = 0; i < 7; i++) checksum ^= frame[i] ^ (frame[i] >> 4);
  frame[1] |= checksum & 0x0F;

  for (uint8_t i = 1; i < 7; i++) frame[i] ^= frame[i - 1];
}

void Radio::transmit(const uint8_t frame[7], uint8_t repeats, bool firstFrame) {
  tune(mhzSomfy);
  ELECHOUSE_cc1101.SetTx();
  digitalWrite(pinTx, LOW);

  for (uint8_t f = 0; f <= repeats; f++) {
    if (f == 0 && firstFrame) {
      level(HIGH, 9415);
      level(LOW, 89565);
    }
    uint8_t hwSync = (f == 0 && firstFrame) ? 2 : 7;
    for (uint8_t i = 0; i < hwSync; i++) {
      level(HIGH, 4 * SYMBOL);
      level(LOW, 4 * SYMBOL);
    }
    level(HIGH, 4550);
    level(LOW, SYMBOL);

    for (uint8_t b = 0; b < 7; b++) {
      for (int8_t bit = 7; bit >= 0; bit--) {
        if ((frame[b] >> bit) & 1) {
          level(LOW, SYMBOL);
          level(HIGH, SYMBOL);
        } else {
          level(HIGH, SYMBOL);
          level(LOW, SYMBOL);
        }
      }
    }
    level(LOW, 30415);
  }
  digitalWrite(pinTx, LOW);
}

void Radio::transmitAOK(uint32_t remoteId, uint16_t address, uint8_t cmd, uint8_t repeats) {
  uint8_t d[8];
  d[0] = 0xA3;
  d[1] = remoteId >> 16;
  d[2] = remoteId >> 8;
  d[3] = remoteId;
  d[4] = address >> 8;
  d[5] = address & 0xFF;
  d[6] = cmd;
  d[7] = (uint8_t)(d[1] + d[2] + d[3] + d[4] + d[5] + d[6]);

  tune(mhzAok);
  ELECHOUSE_cc1101.SetTx();
  digitalWrite(pinTx, LOW);

  const uint16_t SHORT = 270, LONG = 565;
  for (uint8_t r = 0; r < repeats; r++) {
    level(HIGH, 5300);
    level(LOW, 530);
    for (uint8_t i = 0; i < 65; i++) {
      bool bit = (i == 64) ? true : ((d[i / 8] >> (7 - (i % 8))) & 1);
      if (bit) {
        level(HIGH, LONG);
        level(LOW, SHORT);
      } else {
        level(HIGH, SHORT);
        level(LOW, LONG);
      }
    }
    level(LOW, 5030);
  }
  digitalWrite(pinTx, LOW);
}

#define RING 512
static volatile uint32_t ringT[RING];
static volatile uint8_t ringL[RING];
static volatile uint16_t rHead = 0, rTail = 0;
static SomfyDecoder decoder;

static void IRAM_ATTR onEdge() {
  uint32_t t = (uint32_t)esp_timer_get_time();
  uint8_t l = gpio_get_level((gpio_num_t)pinTx);
  uint16_t n = (rHead + 1) & (RING - 1);
  if (n != rTail) {
    ringT[rHead] = t;
    ringL[rHead] = l;
    rHead = n;
  }
}

void Radio::rxStart() {
  digitalWrite(pinTx, LOW);
  ELECHOUSE_cc1101.setRxBW(200);
  ELECHOUSE_cc1101.setDRate(1.6);
  tune(mhzSomfy);
  ELECHOUSE_cc1101.SetRx();
  pinMode(pinTx, INPUT);
  rHead = rTail = 0;
  decoder.reset();
  attachInterrupt(digitalPinToInterrupt(pinTx), onEdge, CHANGE);
}

void Radio::rxStop() {
  detachInterrupt(digitalPinToInterrupt(pinTx));
  pinMode(pinTx, OUTPUT);
  digitalWrite(pinTx, LOW);
  ELECHOUSE_cc1101.setRxBW(TX_RX_BW_KHZ);
  ELECHOUSE_cc1101.setDRate(TX_DRATE_KBAUD);
  ELECHOUSE_cc1101.SetTx();
}

bool Radio::rxPoll(uint8_t &cmd, uint32_t &id, uint16_t &code) {
  while (rTail != rHead) {
    uint32_t t = ringT[rTail];
    bool l = ringL[rTail];
    rTail = (rTail + 1) & (RING - 1);
    if (decoder.feed(l, t)) {
      cmd = decoder.cmd;
      id = decoder.id;
      code = decoder.code;
      return true;
    }
  }
  return false;
}
