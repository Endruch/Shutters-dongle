#pragma once
#include <stdint.h>

class SomfyDecoder {
 public:
  bool feed(bool levelAfter, uint32_t t) {
    if (!haveEdge_) {
      haveEdge_ = true;
      lastT_ = t;
      return false;
    }
    uint32_t dur = t - lastT_;
    lastT_ = t;
    bool runLevel = !levelAfter;
    return run(runLevel, dur);
  }

  uint8_t cmd = 0;
  uint16_t code = 0;
  uint32_t id = 0;

  void reset() {
    state_ = IDLE;
    haveEdge_ = false;
  }

 private:
  static constexpr uint32_t T = 604;
  enum State { IDLE, SYNC_LOW, DATA } state_ = IDLE;
  bool haveEdge_ = false;
  uint32_t lastT_ = 0;
  uint8_t halves_[112];
  uint8_t nHalves_ = 0;

  static int units(uint32_t dur) {
    if (dur < 400) return 0;
    return (int)((dur + T / 2) / T);
  }

  bool run(bool level, uint32_t dur) {
    switch (state_) {
      case IDLE:
        if (level && dur > 3800 && dur < 5300) state_ = SYNC_LOW;
        return false;
      case SYNC_LOW: {
        int n = units(dur);
        if (level || n < 1 || n > 2) {
          state_ = IDLE;
          return false;
        }
        nHalves_ = 0;
        for (int i = 1; i < n; i++) halves_[nHalves_++] = 0;
        state_ = DATA;
        return false;
      }
      case DATA: {
        int n = units(dur);
        if (n < 1) {
          state_ = IDLE;
          return false;
        }
        if (n > 2 && !(level == false)) {
          state_ = IDLE;
          return false;
        }
        for (int i = 0; i < n && nHalves_ < 112; i++) halves_[nHalves_++] = level ? 1 : 0;
        if (nHalves_ < 112) {
          if (n > 2) state_ = IDLE;
          return false;
        }
        state_ = IDLE;
        return decode();
      }
    }
    return false;
  }

  bool decode() {
    uint8_t f[7] = {0};
    for (int b = 0; b < 56; b++) {
      uint8_t a = halves_[2 * b], c = halves_[2 * b + 1];
      if (a == c) return false;
      uint8_t bit = (a == 0 && c == 1) ? 1 : 0;
      f[b / 8] = (f[b / 8] << 1) | bit;
    }
    for (int i = 6; i >= 1; i--) f[i] ^= f[i - 1];
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++) sum ^= f[i] ^ (f[i] >> 4);
    if ((sum & 0x0F) != 0) return false;
    cmd = f[1] >> 4;
    code = ((uint16_t)f[2] << 8) | f[3];
    id = ((uint32_t)f[4] << 16) | ((uint32_t)f[5] << 8) | f[6];
    return true;
  }
};
