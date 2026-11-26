#pragma once
// Minimal TLS WebSocket client. ElevenLabs sends agent audio as single text
// frames of 150 KB+, larger than common Arduino WebSocket libraries accept,
// so frames are reassembled into a growable PSRAM buffer here.
#include <NetworkClientSecure.h>
#include <esp_random.h>
#include "mbedtls/base64.h"

class WsClient {
 public:
  using TextHandler = void (*)(char *msg, size_t len);

  bool connect(const char *host, const char *path, const char *rootCa, TextHandler onText) {
    onText_ = onText;
    tls_.setCACert(rootCa);
    if (!tls_.connect(host, 443)) return false;

    uint8_t nonce[16];
    for (auto &b : nonce) b = esp_random() & 0xff;
    unsigned char key[32];
    size_t keyLen = 0;
    mbedtls_base64_encode(key, sizeof(key), &keyLen, nonce, sizeof(nonce));
    key[keyLen] = 0;

    tls_.printf("GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
                path, host, key);

    String status = tls_.readStringUntil('\n');
    if (status.indexOf(" 101") < 0) {
      Serial.printf("WebSocket upgrade refused: %s\n", status.c_str());
      while (tls_.available()) Serial.write(tls_.read());
      tls_.stop();
      return false;
    }
    while (true) {  // skip the rest of the response headers
      String line = tls_.readStringUntil('\n');
      if (line.length() <= 1) break;
    }
    open_ = true;
    return true;
  }

  bool connected() { return open_ && tls_.connected(); }

  void close() {
    if (open_ && tls_.connected()) send(0x8, nullptr, 0);
    open_ = false;
    tls_.stop();
  }

  bool sendText(const char *data, size_t len) { return send(0x1, (const uint8_t *)data, len); }

  // Reads every complete frame that has arrived; call often from loop().
  void poll() {
    while (open_ && tls_.available() >= 2) {
      if (!readFrame()) {
        Serial.println("WebSocket read error, closing");
        close();
      }
    }
    if (open_ && !tls_.connected()) open_ = false;
  }

 private:
  NetworkClientSecure tls_;
  TextHandler onText_ = nullptr;
  bool open_ = false;
  uint8_t *rx_ = nullptr;
  size_t rxCap_ = 0, rxLen_ = 0;
  uint8_t *tx_ = nullptr;
  size_t txCap_ = 0;

  bool readExact(uint8_t *dst, size_t n) {
    size_t got = 0;
    uint32_t deadline = millis() + 5000;
    while (got < n && millis() < deadline) {
      int r = tls_.read(dst + got, n - got);
      if (r > 0) {
        got += r;
        deadline = millis() + 5000;
      } else if (!tls_.connected()) {
        return false;
      } else {
        delay(1);
      }
    }
    return got == n;
  }

  bool ensureRx(size_t need) {
    if (need <= rxCap_) return true;
    size_t cap = need + 64 * 1024;
    auto *p = (uint8_t *)ps_realloc(rx_, cap);
    if (!p) return false;
    rx_ = p;
    rxCap_ = cap;
    return true;
  }

  bool readFrame() {
    uint8_t h[2];
    if (!readExact(h, 2)) return false;
    bool fin = h[0] & 0x80;
    uint8_t op = h[0] & 0x0f;
    bool masked = h[1] & 0x80;
    uint64_t len = h[1] & 0x7f;
    if (len == 126) {
      uint8_t e[2];
      if (!readExact(e, 2)) return false;
      len = (e[0] << 8) | e[1];
    } else if (len == 127) {
      uint8_t e[8];
      if (!readExact(e, 8)) return false;
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    uint8_t mask[4] = {0};
    if (masked && !readExact(mask, 4)) return false;
    if (len > 4 * 1024 * 1024) return false;

    if (op >= 0x8) {  // control frame: close, ping, pong
      uint8_t ctrl[125];
      if (len > sizeof(ctrl) || !readExact(ctrl, len)) return false;
      if (op == 0x8) {
        uint16_t code = len >= 2 ? (ctrl[0] << 8) | ctrl[1] : 0;
        Serial.printf("Server closed the conversation (code %u %.*s)\n", code,
                      len > 2 ? (int)len - 2 : 0, (const char *)ctrl + 2);
        close();
      } else if (op == 0x9) {
        send(0xA, ctrl, len);
      }
      return true;
    }

    if (op != 0x0) rxLen_ = 0;  // a new message starts
    if (!ensureRx(rxLen_ + len + 1)) return false;
    if (!readExact(rx_ + rxLen_, len)) return false;
    if (masked)
      for (size_t i = 0; i < len; i++) rx_[rxLen_ + i] ^= mask[i & 3];
    rxLen_ += len;
    if (fin) {
      rx_[rxLen_] = 0;
      if (onText_) onText_((char *)rx_, rxLen_);
      rxLen_ = 0;
    }
    return true;
  }

  bool send(uint8_t op, const uint8_t *data, size_t len) {
    if (!tls_.connected()) return false;
    size_t need = len + 14;
    if (need > txCap_) {
      auto *p = (uint8_t *)ps_realloc(tx_, need);
      if (!p) return false;
      tx_ = p;
      txCap_ = need;
    }
    size_t n = 0;
    tx_[n++] = 0x80 | op;
    if (len < 126) {
      tx_[n++] = 0x80 | len;
    } else if (len < 65536) {
      tx_[n++] = 0x80 | 126;
      tx_[n++] = len >> 8;
      tx_[n++] = len & 0xff;
    } else {
      tx_[n++] = 0x80 | 127;
      for (int i = 7; i >= 0; i--) tx_[n++] = (uint64_t)len >> (8 * i);
    }
    uint32_t m = esp_random();
    uint8_t *mask = tx_ + n;
    memcpy(mask, &m, 4);
    n += 4;
    for (size_t i = 0; i < len; i++) tx_[n + i] = data[i] ^ mask[i & 3];
    n += len;
    return tls_.write(tx_, n) == n;
  }
};
