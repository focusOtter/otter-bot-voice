// Otter bot voice bench test: ESP32-S3 + MAX98357A amp + INMP441 mic.
// Serial menu (115200 baud): t = tone, m = mic level, l = loopback, c = chat with Devin.
// Push-to-talk is the board's BOOT button.
#include <WiFi.h>
#include <ESP_I2S.h>
#include <ArduinoJson.h>
#include "freertos/stream_buffer.h"
#include "mbedtls/base64.h"
#include "ws_client.h"
#include "root_ca.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.example.h to secrets.h and fill in your Wi-Fi and agent ID"
#endif

// Pin map: everything sits on the left header (J1), away from strapping, USB and PSRAM pins.
constexpr int AMP_BCLK = 5, AMP_LRC = 6, AMP_DIN = 7;
constexpr int MIC_SCK = 15, MIC_WS = 16, MIC_SD = 17;
constexpr int PTT_PIN = 0;  // BOOT button, LOW while held
constexpr int STATUS_LED = 48;  // onboard RGB LED: 48 on v1.0 boards, 38 on v1.1

constexpr uint32_t SAMPLE_RATE = 16000;            // ElevenLabs agent uses pcm_16000 both ways
constexpr size_t FRAME_SAMPLES = SAMPLE_RATE / 10;  // 100 ms frames
constexpr size_t FRAME_BYTES = FRAME_SAMPLES * 2;
constexpr size_t PLAY_BUF_BYTES = SAMPLE_RATE * 2 * 30;  // 30 s of reply audio
constexpr size_t LOOPBACK_SAMPLES = SAMPLE_RATE * 5;

const char *EL_HOST = "api.elevenlabs.io";

I2SClass speaker, mic;
WsClient ws;
StreamBufferHandle_t playBuf, micBuf;

enum Mode { MENU, LEVEL, LOOPBACK, CHAT };
volatile Mode mode = MENU;
volatile int volumePct = 50;       // keep it modest on USB power
volatile int micGainShift = 13;    // 32-bit mic sample >> shift = 16-bit PCM (lower = louder)
volatile bool flushPlayback = false;
volatile int micChannel = -1;      // -1 = auto-detect left/right slot
volatile int32_t micPeak = 0;
volatile float micRmsDb = -120;
volatile bool chatLive = false;

int16_t *loopback = nullptr;
size_t loopbackLen = 0;
bool pttWasDown = false;

bool pttDown() { return digitalRead(PTT_PIN) == LOW; }

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(STATUS_LED, r, g, b);
}

// ---------- audio tasks ----------

void playbackTask(void *) {
  static int16_t buf[256];
  for (;;) {
    if (flushPlayback) {
      size_t n;
      while ((n = xStreamBufferReceive(playBuf, buf, sizeof(buf), 0)) > 0) {}
      flushPlayback = false;
    }
    size_t got = xStreamBufferReceive(playBuf, buf, sizeof(buf), pdMS_TO_TICKS(20));
    if (got == 0) {
      memset(buf, 0, sizeof(buf));
      got = sizeof(buf);
    }
    int vol = volumePct;
    for (size_t i = 0; i < got / 2; i++) buf[i] = (int32_t)buf[i] * vol / 100;
    speaker.write((uint8_t *)buf, got);
  }
}

void micTask(void *) {
  static int32_t raw[FRAME_SAMPLES * 2];  // stereo slots, 24-bit data in 32-bit words
  static int16_t pcm[FRAME_SAMPLES];
  for (;;) {
    size_t got = mic.readBytes((char *)raw, sizeof(raw));
    size_t frames = got / 8;
    if (frames == 0) continue;

    if (micChannel < 0) {  // pick whichever slot the mic's L/R pin put it on
      int64_t e[2] = {0, 0};
      for (size_t i = 0; i < frames; i++) {
        e[0] += llabs((int64_t)(raw[2 * i] >> 8));
        e[1] += llabs((int64_t)(raw[2 * i + 1] >> 8));
      }
      if (e[0] > 0 || e[1] > 0) {
        micChannel = e[1] > e[0] ? 1 : 0;
        Serial.printf("Mic found on the %s slot\n", micChannel ? "right" : "left");
      }
    }
    int ch = micChannel < 0 ? 0 : micChannel;

    int shift = micGainShift;
    int32_t peak = 0;
    double sumSq = 0;
    for (size_t i = 0; i < frames; i++) {
      int32_t s = raw[2 * i + ch] >> shift;
      s = constrain(s, -32768, 32767);
      pcm[i] = s;
      peak = max(peak, abs(s));
      sumSq += (double)s * s;
    }
    micPeak = peak;
    micRmsDb = 20 * log10f(sqrtf(sumSq / frames) / 32768.0f + 1e-9f);

    if (mode == LOOPBACK && pttDown() && loopback) {
      size_t n = min(frames, LOOPBACK_SAMPLES - loopbackLen);
      memcpy(loopback + loopbackLen, pcm, n * 2);
      loopbackLen += n;
    } else if (mode == CHAT && chatLive) {
      // Stream real audio while BOOT is held, silence otherwise so the agent can tell the turn ended.
      if (!pttDown()) memset(pcm, 0, frames * 2);
      if (xStreamBufferSpacesAvailable(micBuf) >= frames * 2)  // whole chunk or nothing: keeps samples aligned
        xStreamBufferSend(micBuf, pcm, frames * 2, 0);
    }
  }
}

// ---------- hardware checks ----------

void playTone(float hz, int ms) {
  static int16_t buf[256];
  size_t total = SAMPLE_RATE * ms / 1000;
  float phase = 0, step = 2 * PI * hz / SAMPLE_RATE;
  for (size_t done = 0; done < total;) {
    size_t n = min((size_t)256, total - done);
    for (size_t i = 0; i < n; i++) {
      float env = min(1.0f, min((done + i) / 160.0f, (total - done - i) / 160.0f));  // 10 ms fade
      buf[i] = sinf(phase) * 20000 * env;
      phase += step;
    }
    xStreamBufferSend(playBuf, buf, n * 2, portMAX_DELAY);
    done += n;
  }
}

void runToneTest() {
  Serial.println("\nTone test: you should hear three rising beeps.");
  playTone(440, 400); playTone(0, 150);
  playTone(660, 400); playTone(0, 150);
  playTone(880, 600);
  Serial.println("No sound? Check amp VIN to 5V, GND, BCLK/LRC/DIN, and the speaker on the + and - screw terminals.");
}

void printLevel() {
  int db = micRmsDb;
  int bars = constrain((db + 80) / 2, 0, 40);
  char bar[41];
  memset(bar, '#', bars);
  bar[bars] = 0;
  Serial.printf("mic %4d dBFS  peak %5ld  |%-40s|\n", db, (long)micPeak, bar);
}

// ---------- ElevenLabs conversation ----------

const char *findField(const char *msg, const char *key) {
  const char *p = strstr(msg, key);
  return p ? p + strlen(key) : nullptr;
}

void onAgentMessage(char *msg, size_t len) {
  // Audio frames are huge; pull the base64 out by hand instead of building a JSON tree.
  if (const char *b64 = findField(msg, "\"audio_base_64\":\"")) {
    const char *end = strchr(b64, '"');
    if (!end) return;
    size_t outLen = 0;
    auto *dst = (unsigned char *)msg;  // decode in place: output is always shorter than input
    if (mbedtls_base64_decode(dst, len, &outLen, (const unsigned char *)b64, end - b64) != 0) return;
    if (pttDown()) return;  // you're talking over it: drop it
    setLed(0, 0, 40);
    for (size_t off = 0; off < outLen;) {
      size_t sent = xStreamBufferSend(playBuf, dst + off, outLen - off, pdMS_TO_TICKS(2000));
      if (sent == 0) break;
      off += sent;
    }
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, msg, len)) return;
  const char *type = doc["type"] | "";
  if (!strcmp(type, "ping")) {
    char pong[64];
    int n = snprintf(pong, sizeof(pong), "{\"type\":\"pong\",\"event_id\":%ld}",
                     (long)(doc["ping_event"]["event_id"] | 0));
    ws.sendText(pong, n);
  } else if (!strcmp(type, "conversation_initiation_metadata")) {
    auto md = doc["conversation_initiation_metadata_event"];
    Serial.printf("Connected. conversation %s (in %s, out %s)\n", md["conversation_id"] | "?",
                  md["user_input_audio_format"] | "?", md["agent_output_audio_format"] | "?");
    Serial.println("Hold BOOT, talk, then let go. Press q to hang up.");
  } else if (!strcmp(type, "user_transcript")) {
    Serial.printf("you:   %s\n", doc["user_transcription_event"]["user_transcript"] | "");
  } else if (!strcmp(type, "agent_response")) {
    Serial.printf("devin: %s\n", doc["agent_response_event"]["agent_response"] | "");
  } else if (!strcmp(type, "interruption")) {
    flushPlayback = true;
  }
}

bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.printf("Joining Wi-Fi \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // power save adds latency to every audio packet
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWi-Fi failed. Is it 2.4 GHz, and is the antenna snapped on?");
    return false;
  }
  Serial.printf("\nWi-Fi OK, IP %s, signal %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

void startChat() {
  mode = CHAT;
  if (!ensureWifi()) { mode = MENU; return; }
  Serial.println("Calling Devin...");
  setLed(40, 20, 0);
  String path = String("/v1/convai/conversation?agent_id=") + ELEVENLABS_AGENT_ID;
  if (!ws.connect(EL_HOST, path.c_str(), ROOT_CA, onAgentMessage)) {
    Serial.println("Couldn't reach ElevenLabs. Check the agent ID in secrets.h.");
    setLed(40, 0, 0);
    mode = MENU;
    return;
  }
  const char *init = "{\"type\":\"conversation_initiation_client_data\"}";
  ws.sendText(init, strlen(init));
  xStreamBufferReset(micBuf);
  chatLive = true;
}

void stopChat() {
  chatLive = false;
  ws.close();
  flushPlayback = true;
  setLed(0, 0, 0);
}

void chatLoop() {
  ws.poll();
  if (!ws.connected()) {
    stopChat();
    Serial.println("Conversation ended. Press c (or BOOT) to call again.");
    mode = MENU;
    return;
  }
  bool down = pttDown();
  if (down && !pttWasDown) {
    flushPlayback = true;  // barge in: stop Devin mid-sentence
    setLed(0, 40, 0);
  } else if (!down && pttWasDown) {
    setLed(20, 20, 20);
  }
  pttWasDown = down;

  static uint8_t pcm[FRAME_BYTES];
  static char json[FRAME_BYTES * 4 / 3 + 64];
  while (xStreamBufferBytesAvailable(micBuf) >= FRAME_BYTES) {
    xStreamBufferReceive(micBuf, pcm, FRAME_BYTES, 0);
    size_t n = 0;
    const char *head = "{\"user_audio_chunk\":\"";
    size_t headLen = strlen(head);
    memcpy(json, head, headLen);
    mbedtls_base64_encode((unsigned char *)json + headLen, sizeof(json) - headLen - 3, &n, pcm, FRAME_BYTES);
    memcpy(json + headLen + n, "\"}", 2);
    ws.sendText(json, headLen + n + 2);
  }
}

// ---------- menu ----------

void printMenu() {
  Serial.println("\n=== Otter bot voice bench test ===");
  Serial.println("  t  tone test (speaker + amp)");
  Serial.println("  m  mic level meter (10 s)");
  Serial.println("  l  loopback: hold BOOT, talk, release to hear yourself");
  Serial.println("  c  chat with Devin (or press BOOT here)");
  Serial.println("  +/-  volume     [/]  mic gain     q  back to this menu");
  Serial.printf("  volume %d%%, mic shift %d\n", volumePct, micGainShift);
}

void handleKey(char k) {
  switch (k) {
    case 't': runToneTest(); break;
    case 'm': {
      mode = LEVEL;
      Serial.println("\nMic level: stay quiet for 3 s, then talk, then clap.");
      for (int i = 0; i < 100; i++) { printLevel(); delay(100); }
      mode = MENU;
      Serial.println("Quiet room is usually below -60 dBFS; talking at arm's length -35 to -20.");
      printMenu();
      break;
    }
    case 'l':
      mode = LOOPBACK;
      Serial.println("\nLoopback: hold BOOT and talk (up to 5 s), release to hear it back. q to quit.");
      break;
    case 'c': startChat(); break;
    case '+': volumePct = min(100, volumePct + 10); Serial.printf("volume %d%%\n", volumePct); break;
    case '-': volumePct = max(0, volumePct - 10); Serial.printf("volume %d%%\n", volumePct); break;
    case '[': micGainShift = min(16, micGainShift + 1); Serial.printf("mic shift %d (quieter)\n", micGainShift); break;
    case ']': micGainShift = max(8, micGainShift - 1); Serial.printf("mic shift %d (louder)\n", micGainShift); break;
    case 'q':
      if (mode == CHAT) stopChat();
      mode = MENU;
      printMenu();
      break;
    case '\n': case '\r': break;
    default: printMenu();
  }
}

void loopbackLoop() {
  bool down = pttDown();
  if (down && !pttWasDown) {
    loopbackLen = 0;
    setLed(40, 0, 0);
    Serial.println("recording...");
  } else if (!down && pttWasDown) {
    setLed(0, 0, 0);
    Serial.printf("playing %.1f s back\n", loopbackLen / (float)SAMPLE_RATE);
    for (size_t off = 0; off < loopbackLen * 2;)
      off += xStreamBufferSend(playBuf, (uint8_t *)loopback + off, loopbackLen * 2 - off, portMAX_DELAY);
  }
  pttWasDown = down;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(PTT_PIN, INPUT_PULLUP);
  setLed(0, 0, 0);

  if (!psramFound()) Serial.println("WARNING: no PSRAM. In Arduino IDE set Tools > PSRAM > OPI PSRAM.");
  static StaticStreamBuffer_t playCtl, micCtl;
  auto *playStore = (uint8_t *)ps_malloc(PLAY_BUF_BYTES + 1);
  auto *micStore = (uint8_t *)ps_malloc(FRAME_BYTES * 20 + 1);
  loopback = (int16_t *)ps_malloc(LOOPBACK_SAMPLES * 2);
  if (!playStore || !micStore || !loopback) {
    Serial.println("Out of memory: enable OPI PSRAM and reflash.");
    for (;;) delay(1000);
  }
  playBuf = xStreamBufferCreateStatic(PLAY_BUF_BYTES, 1, playStore, &playCtl);
  micBuf = xStreamBufferCreateStatic(FRAME_BYTES * 20, 1, micStore, &micCtl);

  speaker.setPins(AMP_BCLK, AMP_LRC, AMP_DIN);
  if (!speaker.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH))
    Serial.println("Speaker I2S failed to start");
  mic.setPins(MIC_SCK, MIC_WS, -1, MIC_SD);
  if (!mic.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO))
    Serial.println("Mic I2S failed to start");

  xTaskCreatePinnedToCore(playbackTask, "play", 4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(micTask, "mic", 6144, nullptr, 5, nullptr, 1);
  printMenu();
}

void loop() {
  if (Serial.available()) handleKey(Serial.read());
  switch (mode) {
    case CHAT: chatLoop(); break;
    case LOOPBACK: loopbackLoop(); break;
    case MENU:
      if (pttDown()) {
        while (pttDown()) delay(10);
        startChat();
      }
      break;
    default: break;
  }
  delay(mode == CHAT ? 1 : 10);
}
