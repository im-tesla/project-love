#include "ble_service.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"

namespace {

NimBLECharacteristic *g_state = nullptr;

// Guards everything below that is written from the NimBLE task and read from
// loop().
SemaphoreHandle_t g_lock = nullptr;

protocol::Reassembler g_inbound;
std::string g_pending;
bool g_hasPending = false;

volatile bool g_connected = false;
volatile bool g_justConnected = false;

// Until the phone negotiates something larger, BLE's floor is 23 bytes: 3 for
// the ATT header, 2 for our chunk header, leaving 18 bytes of payload.
volatile uint16_t g_mtu = 23;

struct Guard {
  Guard() { xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(g_lock); }
};

size_t payloadPerChunk() {
  const uint16_t mtu = g_mtu;
  const int usable = static_cast<int>(mtu) - 3 - static_cast<int>(protocol::kHeaderSize);
  return usable > 1 ? static_cast<size_t>(usable) : 1;
}

struct LoveServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override {
    (void)server;
    (void)info;
    g_connected = true;
    g_justConnected = true;
    Serial.println(F("[ble] connected"));
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &info, int reason) override {
    (void)server;
    (void)info;
    g_connected = false;
    g_mtu = 23;
    {
      Guard guard;
      g_inbound.reset();  // a half-sent message is not coming now
    }
    Serial.printf("[ble] disconnected (reason %d), advertising again\n", reason);
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo &info) override {
    (void)info;
    g_mtu = mtu;
    Serial.printf("[ble] MTU %u -> %u byte chunks\n", mtu,
                  static_cast<unsigned>(payloadPerChunk()));
  }
};

struct LoveCommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &info) override {
    (void)info;
    const NimBLEAttValue value = characteristic->getValue();

    Guard guard;
    const auto result = g_inbound.accept(value.data(), value.size(), millis());
    switch (result) {
      case protocol::Reassembler::Result::Complete:
        g_pending.assign(g_inbound.message(), g_inbound.messageLength());
        g_hasPending = true;
        break;
      case protocol::Reassembler::Result::Invalid:
        Serial.println(F("[ble] discarded an out-of-sequence chunk"));
        break;
      case protocol::Reassembler::Result::Overflow:
        Serial.println(F("[ble] message too large, discarded"));
        break;
      case protocol::Reassembler::Result::NeedMore:
        break;
    }
  }
};

LoveServerCallbacks g_serverCallbacks;
LoveCommandCallbacks g_commandCallbacks;

}  // namespace

void BleService::begin() {
  if (g_lock == nullptr) {
    g_lock = xSemaphoreCreateMutex();
  }

  NimBLEDevice::init(LOVE_DEVICE_NAME);
  // Ask for the largest MTU we can; iOS typically settles around 185, which
  // turns a long Polish message from a dozen chunks into two.
  NimBLEDevice::setMTU(517);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&g_serverCallbacks);
  // Keep advertising after a disconnect so she never has to power-cycle it.
  server->advertiseOnDisconnect(true);

  NimBLEService *service = server->createService(LOVE_SERVICE_UUID);

  NimBLECharacteristic *command = service->createCharacteristic(
      LOVE_CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  command->setCallbacks(&g_commandCallbacks);

  g_state = service->createCharacteristic(
      LOVE_STATE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  service->start();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(LOVE_SERVICE_UUID);
  advertising->setName(LOVE_DEVICE_NAME);
  advertising->enableScanResponse(true);
  advertising->start();

  Serial.printf("[ble] advertising as \"%s\"\n", LOVE_DEVICE_NAME);
}

bool BleService::takeCommand(std::string &out, uint32_t nowMs) {
  Guard guard;
  g_inbound.expire(nowMs);
  if (!g_hasPending) {
    return false;
  }
  out.swap(g_pending);
  g_pending.clear();
  g_hasPending = false;
  return true;
}

bool BleService::connected() const { return g_connected; }

bool BleService::consumeJustConnected() {
  if (!g_justConnected) {
    return false;
  }
  g_justConnected = false;
  return true;
}

void BleService::notifyState(const Settings &settings, bool hasClock) {
  if (g_state == nullptr || !g_connected) {
    return;
  }

  static char buffer[protocol::kStateBufferSize];
  const size_t length = protocol::serialiseState(settings, hasClock, buffer, sizeof(buffer));
  if (length == 0) {
    Serial.println(F("[ble] state did not fit its buffer -- not sent"));
    return;
  }

  const size_t perChunk = payloadPerChunk();
  const size_t total = (length + perChunk - 1) / perChunk;
  if (total > 255) {
    Serial.println(F("[ble] state needs more than 255 chunks -- not sent"));
    return;
  }

  uint8_t frame[protocol::kHeaderSize + 512];
  for (size_t index = 0; index < total; index++) {
    const size_t offset = index * perChunk;
    const size_t size = (offset + perChunk <= length) ? perChunk : (length - offset);

    frame[0] = static_cast<uint8_t>(index);
    frame[1] = static_cast<uint8_t>(total);
    memcpy(frame + protocol::kHeaderSize, buffer + offset, size);

    g_state->setValue(frame, protocol::kHeaderSize + size);
    g_state->notify();

    // The phone's notification queue is short; without a breath between
    // chunks the tail of a long state update is silently dropped.
    delay(12);
  }
}
