// Web Bluetooth transport.
//
// Requires a secure context: navigator.bluetooth simply does not exist over
// plain http://, which is why this site needs a real certificate rather than
// a self-signed one.
//
// iOS also requires a user gesture to open the device chooser, once per
// session. That cannot be automated away, so connect() must be called
// directly from a tap handler.

import {
  CMD_UUID,
  PAYLOAD_OPTIMISTIC,
  PAYLOAD_SAFE,
  Reassembler,
  SERVICE_UUID,
  STATE_UUID,
  frameCommand,
} from './protocol.js';

// How long the post-selection handshake (GATT connect, service discovery,
// subscribing to notifications) gets before giving up. Deliberately NOT
// applied to requestDevice() itself -- that promise does not resolve until
// she picks a device or cancels, and a real person can reasonably take much
// longer than this to decide. This timeout exists so that if the board's BLE
// stack wedges after she has already chosen it, the button comes back and she
// gets a message instead of the app staring at "Szukam tabliczki…" forever.
const HANDSHAKE_TIMEOUT_MS = 12000;

function withTimeout(promise, ms, message) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => {
      const error = new Error(message);
      error.name = 'TimeoutError';
      reject(error);
    }, ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

export class BleTransport {
  #device = null;
  #command = null;
  #state = null;
  #inbound = new Reassembler();
  #payloadSize = PAYLOAD_OPTIMISTIC;
  #queue = Promise.resolve();

  /** Fired with the parsed state object each time the device reports. */
  onState = () => {};
  /** Fired with true/false as the link comes and goes. */
  onConnection = () => {};

  static get available() {
    return typeof navigator !== 'undefined' && navigator.bluetooth != null;
  }

  static get secureContext() {
    return typeof window !== 'undefined' && window.isSecureContext;
  }

  /**
   * Whether a working Bluetooth radio is actually present and switched on --
   * distinct from `available`, which only checks that the API exists. A
   * desktop with no adapter, or one with Bluetooth off, still has
   * `navigator.bluetooth`; calling requestDevice() there is what triggers the
   * quiet, unrecoverable hang, and getAvailability() is what catches it before
   * that happens.
   *
   * Returns null on browsers old enough not to expose this -- meaning
   * "unknown", not "unavailable". Chrome and Edge have shipped it since 2020;
   * this is close to a floor for anything that supports Web Bluetooth at all.
   */
  static async getAvailability() {
    if (typeof navigator === 'undefined' || navigator.bluetooth?.getAvailability == null) {
      return null;
    }
    try {
      return await navigator.bluetooth.getAvailability();
    } catch {
      return null;
    }
  }

  get connected() {
    return this.#device?.gatt?.connected === true;
  }

  /** Must be called from a user gesture. */
  async connect() {
    this.#device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }],
      optionalServices: [SERVICE_UUID],
    });

    this.#device.addEventListener('gattserverdisconnected', () => {
      this.#inbound.reset();
      this.onConnection(false);
    });

    try {
      await withTimeout(
        this.#handshake(),
        HANDSHAKE_TIMEOUT_MS,
        'connecting to the matrix timed out',
      );
    } catch (error) {
      // Leave no half-open GATT session behind on the way out -- otherwise a
      // retry can find the OS still thinks it is connected to a device that
      // never finished setting up.
      if (this.#device?.gatt?.connected) {
        this.#device.gatt.disconnect();
      }
      throw error;
    }

    this.onConnection(true);

    // The board has no RTC, so it cannot know the hour on its own. Sending
    // this on every connect is what makes night mode work at all.
    await this.syncClock();
  }

  async #handshake() {
    const server = await this.#device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);

    this.#command = await service.getCharacteristic(CMD_UUID);
    this.#state = await service.getCharacteristic(STATE_UUID);

    this.#state.addEventListener('characteristicvaluechanged', (event) => {
      const message = this.#inbound.accept(event.target.value);
      if (message === null) return;
      try {
        this.onState(JSON.parse(message));
      } catch {
        // A malformed state notification is not worth breaking the UI over;
        // the next one will be along shortly.
      }
    });
    await this.#state.startNotifications();
  }

  async disconnect() {
    if (this.#device?.gatt?.connected) {
      this.#device.gatt.disconnect();
    }
  }

  async syncClock() {
    const now = new Date();
    await this.send({
      c: 'time',
      v: Math.floor(now.getTime() / 1000),
      // getTimezoneOffset() counts the other way round from what the firmware
      // wants: it returns +minutes for zones *behind* UTC.
      tz: -now.getTimezoneOffset(),
    });
  }

  /**
   * Sends one command. Calls are queued, so rapid slider drags arrive in the
   * order they happened rather than racing each other.
   */
  send(command) {
    this.#queue = this.#queue.then(() => this.#write(command)).catch((error) => {
      console.warn('send failed', error);
    });
    return this.#queue;
  }

  async #write(command) {
    if (!this.#command) throw new Error('not connected');

    try {
      await this.#writeChunks(command, this.#payloadSize);
    } catch (error) {
      if (this.#payloadSize === PAYLOAD_SAFE) throw error;
      // Almost certainly the MTU is smaller than we hoped. Drop to the size
      // every BLE stack must support and retry the whole message -- the device
      // discards a partial sequence when a new index 0 arrives, so this is
      // safe to do mid-message.
      console.warn('retrying at the minimum MTU');
      this.#payloadSize = PAYLOAD_SAFE;
      await this.#writeChunks(command, this.#payloadSize);
    }
  }

  async #writeChunks(command, payloadSize) {
    for (const chunk of frameCommand(command, payloadSize)) {
      await this.#command.writeValueWithResponse(chunk);
    }
  }
}
