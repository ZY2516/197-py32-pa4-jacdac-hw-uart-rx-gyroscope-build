export function crc16(data) {
  let crc = 0xffff;
  for (const byte of data) {
    let x = ((crc >>> 8) ^ byte) & 0xff;
    x ^= x >>> 4;
    crc = ((crc << 8) ^ (x << 12) ^ (x << 5) ^ x) & 0xffff;
  }
  return crc;
}

function bytesFromHex(value) {
  if (!/^[0-9a-f]{16}$/i.test(value)) {
    throw new Error(`expected a 16-hex-digit Jacdac device identifier, got ${value}`);
  }
  return Uint8Array.from(value.match(/../g), part => Number.parseInt(part, 16));
}

export function buildCommandPacket(deviceId, serviceIndex, command, payload = new Uint8Array()) {
  if (serviceIndex < 0 || serviceIndex > 0x3f) {
    throw new Error(`invalid service index ${serviceIndex}`);
  }
  if (payload.length > 236) {
    throw new Error(`payload too large: ${payload.length}`);
  }

  const packet = new Uint8Array(16 + payload.length);
  packet[2] = payload.length + 4;
  packet[3] = 0x01;
  packet.set(bytesFromHex(deviceId), 4);
  packet[12] = payload.length;
  packet[13] = serviceIndex;
  packet[14] = command & 0xff;
  packet[15] = command >>> 8;
  packet.set(payload, 16);

  const crc = crc16(packet.slice(2));
  packet[0] = crc & 0xff;
  packet[1] = crc >>> 8;
  return packet;
}

export function toHex(data) {
  return [...data].map(byte => byte.toString(16).padStart(2, "0")).join("");
}

export function parseJacdacFrame(frame) {
  const expectedLength = 12 + (frame[2] ?? 0);
  if (frame.length < expectedLength || expectedLength < 16) {
    throw new Error(`invalid Jacdac frame length ${frame.length}; expected ${expectedLength}`);
  }

  const data = frame.slice(0, expectedLength);
  const storedCrc = data[0] | (data[1] << 8);
  const crcOk = crc16(data.slice(2)) === storedCrc;
  const deviceId = toHex(data.slice(4, 12));
  const flags = data[3];
  const packets = [];

  for (let offset = 12; offset < expectedLength;) {
    const payloadSize = data[offset];
    const packetSize = payloadSize + 4;
    if (offset + packetSize > expectedLength) {
      throw new Error(`compressed packet at ${offset} exceeds Jacdac frame`);
    }
    packets.push({
      command: data[offset + 2] | (data[offset + 3] << 8),
      crc: storedCrc,
      crcOk,
      deviceId,
      flags,
      payload: data.slice(offset + 4, offset + packetSize),
      serviceIndex: data[offset + 1] & 0x3f,
    });
    offset += (packetSize + 3) & ~3;
  }

  return packets;
}
