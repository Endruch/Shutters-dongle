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

// Somfy frames are timed against an absolute schedule with interrupts off on this
// core, so Wi-Fi/TCP work can't stretch a 604 us symbol and corrupt the frame.
static portMUX_TYPE txMux = portMUX_INITIALIZER_UNLOCKED;
static int64_t txDeadline = 0;
static uint32_t txMaxLate = 0;

static inline void IRAM_ATTR level(bool high, uint32_t us) {
  gpio_set_level((gpio_num_t)pinTx, high);
  txDeadline += us;
  int64_t now;
  while ((now = esp_timer_get_time()) < txDeadline) {
  }
  uint32_t late = (uint32_t)(now - txDeadline);
  if (late > txMaxLate) txMaxLate = late;
}

static inline void txBegin() {
  portENTER_CRITICAL(&txMux);
  txDeadline = esp_timer_get_time();
}

static inline void txEnd() { portEXIT_CRITICAL(&txMux); }

uint32_t Radio::lastTxMaxLateUs() { return txMaxLate; }

uint8_t Radio::chipVersion() {
  ELECHOUSE_cc1101.getCC1101();
  uint8_t ver = ELECHOUSE_cc1101.SpiReadStatus(0x31);
  uint8_t part = ELECHOUSE_cc1101.SpiReadStatus(0x30);
  if (ver == 0x00 || ver == 0xFF || part != 0x00) return 0;
  return ver;
}

static void applyBaseConfig() {
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(TX_RX_BW_KHZ);
  ELECHOUSE_cc1101.setDRate(TX_DRATE_KBAUD);
  mhzNow = 0;
  tune(mhzSomfy);
  ELECHOUSE_cc1101.setPA(10);
  ELECHOUSE_cc1101.SetTx();
}

// Somfy TX radio setup copied 1:1 from the working Somfy_MQTT reference
// (ELECHOUSE_CC1101_RCS_DRV: Init(PA10) + SetTx(433.42)). Note the reference adds
// 0.01 MHz in setMHZ and transmits on channel 1 with ~50 kHz spacing, so
// 433.42 really goes out on ~433.48 MHz.
static void refFreq(float mhz, uint8_t &f2, uint8_t &f1, uint8_t &f0) {
  float MHZ = mhz + 0.01;
  float s1 = MHZ / 26;
  int s2 = s1;
  float s5 = 255.0 / 100000000 * ((s1 - s2) * 100000000);
  int s6 = s5;
  int s9 = ((s5 - s6) * 10 >= 5) ? s6 + 1 : s6;
  float s11 = (MHZ - (26 * s2 + 0.1015625 * s9)) / 0.00039675;
  int s12 = s11;
  int s14 = ((s11 - s12) * 10 >= 5) ? s12 + 1 : s12;
  f2 = s2;
  f1 = s9;
  f0 = s14;
}

static void somfyRefTxSetup() {
  static uint8_t pa[8] = {0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t f2, f1, f0;
  refFreq(mhzSomfy, f2, f1, f0);
  ELECHOUSE_cc1101.SpiStrobe(0x30);  // SRES
  delay(1);
  ELECHOUSE_cc1101.SpiWriteReg(0x0B, 0x06);  // FSCTRL1
  ELECHOUSE_cc1101.SpiWriteReg(0x0C, 0x00);  // FSCTRL0
  ELECHOUSE_cc1101.SpiWriteReg(0x0D, f2);    // FREQ2
  ELECHOUSE_cc1101.SpiWriteReg(0x0E, f1);    // FREQ1
  ELECHOUSE_cc1101.SpiWriteReg(0x0F, f0);    // FREQ0
  ELECHOUSE_cc1101.SpiWriteBurstReg(0x3E, pa, 8);
  ELECHOUSE_cc1101.SpiWriteReg(0x10, 0x07);  // MDMCFG4
  ELECHOUSE_cc1101.SpiWriteReg(0x11, 0x93);  // MDMCFG3
  ELECHOUSE_cc1101.SpiWriteReg(0x12, 0x30);  // MDMCFG2: ASK/OOK
  ELECHOUSE_cc1101.SpiWriteReg(0x13, 0x00);  // MDMCFG1
  ELECHOUSE_cc1101.SpiWriteReg(0x14, 0xF8);  // MDMCFG0
  ELECHOUSE_cc1101.SpiWriteReg(0x0A, 0x01);  // CHANNR
  ELECHOUSE_cc1101.SpiWriteReg(0x15, 0x15);  // DEVIATN
  ELECHOUSE_cc1101.SpiWriteReg(0x21, 0x56);  // FREND1
  ELECHOUSE_cc1101.SpiWriteReg(0x22, 0x11);  // FREND0
  ELECHOUSE_cc1101.SpiWriteReg(0x18, 0x18);  // MCSM0
  ELECHOUSE_cc1101.SpiWriteReg(0x19, 0x16);  // FOCCFG
  ELECHOUSE_cc1101.SpiWriteReg(0x1A, 0x1C);  // BSCFG
  ELECHOUSE_cc1101.SpiWriteReg(0x1B, 0xC7);  // AGCCTRL2
  ELECHOUSE_cc1101.SpiWriteReg(0x1C, 0x00);  // AGCCTRL1
  ELECHOUSE_cc1101.SpiWriteReg(0x1D, 0xB2);  // AGCCTRL0
  ELECHOUSE_cc1101.SpiWriteReg(0x23, 0xE9);  // FSCAL3
  ELECHOUSE_cc1101.SpiWriteReg(0x24, 0x2A);  // FSCAL2
  ELECHOUSE_cc1101.SpiWriteReg(0x25, 0x00);  // FSCAL1
  ELECHOUSE_cc1101.SpiWriteReg(0x26, 0x1F);  // FSCAL0
  ELECHOUSE_cc1101.SpiWriteReg(0x29, 0x59);  // FSTEST
  ELECHOUSE_cc1101.SpiWriteReg(0x2C, 0x81);  // TEST2
  ELECHOUSE_cc1101.SpiWriteReg(0x2D, 0x35);  // TEST1
  ELECHOUSE_cc1101.SpiWriteReg(0x2E, 0x09);  // TEST0
  ELECHOUSE_cc1101.SpiWriteReg(0x00, 0x0D);  // IOCFG2
  ELECHOUSE_cc1101.SpiWriteReg(0x02, 0x0D);  // IOCFG0
  ELECHOUSE_cc1101.SpiWriteReg(0x07, 0x04);  // PKTCTRL1
  ELECHOUSE_cc1101.SpiWriteReg(0x08, 0x32);  // PKTCTRL0: async serial
  ELECHOUSE_cc1101.SpiWriteReg(0x09, 0x00);  // ADDR
  ELECHOUSE_cc1101.SpiWriteReg(0x06, 0x3D);  // PKTLEN
  ELECHOUSE_cc1101.SpiStrobe(0x35);          // STX
  delay(1);
}

bool Radio::begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs, uint8_t gdo0, float somfyMhz, float aokMhz) {
  pinTx = gdo0;
  mhzSomfy = somfyMhz;
  mhzAok = aokMhz;
  ELECHOUSE_cc1101.setSpiPin(sck, miso, mosi, cs);
  ELECHOUSE_cc1101.setGDO0(gdo0);
  ELECHOUSE_cc1101.Init();
  bool present = chipVersion() != 0;
  applyBaseConfig();
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
  digitalWrite(pinTx, LOW);
  somfyRefTxSetup();

  txMaxLate = 0;
  for (uint8_t f = 0; f <= repeats; f++) {
    if (f == 0 && firstFrame) {
      txBegin();
      level(HIGH, 9415);
      level(LOW, 89565);
      txEnd();
    }
    txBegin();
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
    gpio_set_level((gpio_num_t)pinTx, LOW);
    txEnd();
    delayMicroseconds(30415);
  }
  digitalWrite(pinTx, LOW);
  applyBaseConfig();
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
    txBegin();
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
    txEnd();
    delayMicroseconds(5030);
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
