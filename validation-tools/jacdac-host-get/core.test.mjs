import assert from "node:assert/strict";
import test from "node:test";

import {
  buildCommandPacket,
  crc16,
  parseJacdacFrame,
} from "./core.mjs";

test("crc16 matches the Jacdac-C CCITT implementation", () => {
  assert.equal(crc16(new TextEncoder().encode("123456789")), 0x29b1);
});

test("buildCommandPacket creates a service-1 GET with a self-consistent CRC", () => {
  const packet = buildCommandPacket("0123456789abcdef", 1, 0x1004);

  assert.equal(packet.length, 16);
  assert.deepEqual([...packet.slice(4, 12)], [1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef]);
  assert.equal(packet[2], 4);
  assert.equal(packet[3], 1);
  assert.equal(packet[12], 0);
  assert.equal(packet[13], 1);
  assert.equal(packet[14], 0x04);
  assert.equal(packet[15], 0x10);
  assert.equal(packet[0] | (packet[1] << 8), crc16(packet.slice(2)));
});

test("parseJacdacFrame expands aligned compressed packets", () => {
  const frame = Uint8Array.from([
    0, 0, 12, 0,
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    0, 1, 0x04, 0x10,
    4, 1, 0x01, 0x11, 1, 2, 3, 4,
  ]);
  const crc = crc16(frame.slice(2));
  frame[0] = crc & 0xff;
  frame[1] = crc >> 8;

  const packets = parseJacdacFrame(frame);

  assert.equal(packets.length, 2);
  assert.equal(packets[0].command, 0x1004);
  assert.equal(packets[0].serviceIndex, 1);
  assert.deepEqual([...packets[1].payload], [1, 2, 3, 4]);
  assert.equal(packets[1].command, 0x1101);
  assert.equal(packets[1].crcOk, true);
});
