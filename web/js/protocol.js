// The wire contract with the matrix. Mirrors src/protocol.cpp exactly.
//
// Framing: [chunkIndex, chunkTotal] + payload, chunks strictly in order.
// The device resets its buffer on anything out of sequence, so a failed send
// is retried from the top rather than patched up.

export const SERVICE_UUID = '6c6f7665-0001-4d69-6c65-6e61c2a90001';
export const CMD_UUID = '6c6f7665-0002-4d69-6c65-6e61c2a90002';
export const STATE_UUID = '6c6f7665-0003-4d69-6c65-6e61c2a90003';

export const HEADER_SIZE = 2;

// Web Bluetooth does not expose the negotiated MTU, so we guess high and fall
// back. iOS usually settles around 185 bytes; 180 of payload keeps a long
// Polish message to two chunks.
export const PAYLOAD_OPTIMISTIC = 180;

// The floor every BLE stack must support: 23-byte MTU, 3 bytes of ATT header,
// 2 of ours. Slower, but it cannot fail for being too big.
export const PAYLOAD_SAFE = 18;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/**
 * Splits a command object into framed chunks ready to write.
 * @returns {Uint8Array[]}
 */
export function frameCommand(command, payloadSize) {
  const bytes = encoder.encode(JSON.stringify(command));
  const total = Math.max(1, Math.ceil(bytes.length / payloadSize));
  if (total > 255) {
    throw new RangeError('command needs more than 255 chunks');
  }

  const chunks = [];
  for (let i = 0; i < total; i++) {
    const slice = bytes.subarray(i * payloadSize, (i + 1) * payloadSize);
    const frame = new Uint8Array(HEADER_SIZE + slice.length);
    frame[0] = i;
    frame[1] = total;
    frame.set(slice, HEADER_SIZE);
    chunks.push(frame);
  }
  return chunks;
}

/**
 * Reassembles the device's chunked state notifications.
 * Same rules as the firmware: index 0 restarts, anything out of order resets.
 */
export class Reassembler {
  #parts = [];
  #expected = 0;
  #next = 0;
  #active = false;

  /**
   * @param {DataView|Uint8Array} value
   * @returns {string|null} the complete message, or null while waiting
   */
  accept(value) {
    const bytes = value instanceof DataView
      ? new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
      : value;

    if (bytes.length < HEADER_SIZE) {
      this.reset();
      return null;
    }

    const index = bytes[0];
    const total = bytes[1];
    if (total === 0 || index >= total) {
      this.reset();
      return null;
    }

    if (index === 0) {
      this.#parts = [];
      this.#expected = total;
      this.#next = 0;
      this.#active = true;
    } else if (!this.#active || index !== this.#next || total !== this.#expected) {
      this.reset();
      return null;
    }

    this.#parts.push(bytes.slice(HEADER_SIZE));
    this.#next = index + 1;

    if (this.#next < this.#expected) {
      return null;
    }

    const length = this.#parts.reduce((sum, part) => sum + part.length, 0);
    const joined = new Uint8Array(length);
    let offset = 0;
    for (const part of this.#parts) {
      joined.set(part, offset);
      offset += part.length;
    }
    this.reset();
    return decoder.decode(joined);
  }

  reset() {
    this.#parts = [];
    this.#expected = 0;
    this.#next = 0;
    this.#active = false;
  }
}

/** "22:30" -> 1350. Returns null on anything malformed. */
export function parseClockTime(text) {
  const match = /^(\d{2}):(\d{2})$/.exec(text ?? '');
  if (!match) return null;
  const hours = Number(match[1]);
  const minutes = Number(match[2]);
  if (hours > 23 || minutes > 59) return null;
  return hours * 60 + minutes;
}

/** 1350 -> "22:30". */
export function formatClockTime(minutes) {
  const total = ((minutes % 1440) + 1440) % 1440;
  const hh = String(Math.floor(total / 60)).padStart(2, '0');
  const mm = String(total % 60).padStart(2, '0');
  return `${hh}:${mm}`;
}

/** 32 column bytes -> base64, matching the firmware's draw command. */
export function encodeColumns(columns) {
  let binary = '';
  for (const byte of columns) {
    binary += String.fromCharCode(byte & 0xff);
  }
  return btoa(binary);
}

/** base64 -> Uint8Array of column bytes. */
export function decodeColumns(text) {
  try {
    const binary = atob(text ?? '');
    const out = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) {
      out[i] = binary.charCodeAt(i);
    }
    return out;
  } catch {
    return new Uint8Array(0);
  }
}
