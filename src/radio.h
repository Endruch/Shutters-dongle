#pragma once
#include <Arduino.h>

enum SomfyCmd : uint8_t {
  CMD_MY = 0x1,
  CMD_UP = 0x2,
  CMD_MYUP = 0x3,
  CMD_DOWN = 0x4,
  CMD_MYDOWN = 0x5,
  CMD_UPDOWN = 0x6,
  CMD_PROG = 0x8,
};

namespace Radio {

bool begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs, uint8_t gdo0, float somfyMhz, float aokMhz);

uint8_t chipVersion();

void buildFrame(uint8_t frame[7], uint32_t remoteId, uint16_t rollingCode, uint8_t cmd);

void transmit(const uint8_t frame[7], uint8_t repeats, bool firstFrame = true);

void rxStart();
void rxStop();
bool rxPoll(uint8_t &cmd, uint32_t &id, uint16_t &code);

enum AokCmd : uint8_t { AOK_UP = 11, AOK_DOWN = 67, AOK_STOP = 35, AOK_PROGRAM = 83 };
void transmitAOK(uint32_t remoteId, uint16_t address, uint8_t cmd, uint8_t repeats);

}
