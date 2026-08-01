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

    this.onConnection(true);

    // The board has no RTC, so it cannot know the hour on its own. Sending
    // this on every connect is what makes night mode work at all.
    await this.syncClock();
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
