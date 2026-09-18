/**
 * runIT Web Studio - Client-Side Stream Decoder & Formatter
 * 
 * Implements:
 *  1. Wire packet decoding for enc_sys_errors.h (Class 0x05 / PACKET_HEADER_ERRORS)
 *     - Header (3B): version (0x01), node_count, depth
 *     - Records (5B + payload): payload_len, tag (u16 LE), owner (u16 LE), payload
 *  2. Subsystem owner name mapping (SYS_OWNER_MAP)
 *  3. Error tag name mapping & C struct payload unpackers (SYS_ERROR_VM_MAP)
 *  4. Standalone human-readable description generators (no strings stored in ESP32 flash)
 *  5. Interactive Bi-Lookup: extracts failing obj_id or block_idx for bi-directional linking
 *  6. ESP-IDF pure log parser (LOGI, LOGW, LOGE)
 *  7. Mock packet generator for testing and offline verification
 */

const StreamDecoder = (function () {

  // Stream Categories
  const STREAM_TYPE = {
    ALL: "all",
    ESP_LOG: "esp_log",
    ERRORS: "errors",
    TELEMETRY: "telemetry",
    RAW_HEX: "raw_hex"
  };

  // Subsystem Owners (from sys_error_codes.h & sys_error_vm.h)
  const SYS_OWNERS = {
    0x0001: { name: "OWNER_SYS_BASE", desc: "System Core Base" },
    0x0002: { name: "OWNER_SYS_DEVICE", desc: "Device Manager" },
    0x0003: { name: "OWNER_SYS_IO", desc: "Digital/Analog IO" },
    0x0004: { name: "OWNER_SYS_I2C", desc: "I2C Bus Controller" },
    0x0005: { name: "OWNER_SYS_POWER", desc: "Power & PMU Monitor" },
    0x0006: { name: "OWNER_SYS_BLE", desc: "Bluetooth LE Radio" },
    0x0007: { name: "OWNER_SYS_INTERFACE", desc: "System Interface & Frame Rx/Tx" },
    0x0008: { name: "OWNER_SYS_BUFFERS", desc: "Shared Ring Buffers" },
    0x0009: { name: "OWNER_SYS_ACTIONS", desc: "System Action Pipeline" },
    0xA800: { name: "OWNER_SYS_ERRORS_BASE", desc: "Error Handler Subsystem" },
    0xA801: { name: "OWNER_SYS_ERRORS_CONFIG", desc: "Error Configuration" },
    0xA900: { name: "OWNER_VM_BASE", desc: "VM Core Base" },
    0xA901: { name: "OWNER_VM_STORE", desc: "VM Bump Arena Store" },
    0xA902: { name: "OWNER_VM_OBJ", desc: "VM Object Directory Arena" },
    0xA903: { name: "OWNER_VM_ACCESSOR", desc: "VM Accessor Chain Engine" },
    0xA904: { name: "OWNER_VM_BLOCK", desc: "VM Function Block Execution" },
    0xA905: { name: "OWNER_VM_CODE", desc: "VM Bytecode Engine" },
    0xA906: { name: "OWNER_VM_LOADER", desc: "VM Program Binary Loader" },
    0xA907: { name: "OWNER_DEC_VM_LOADER", desc: "VM Wire Decoder" },
    0xA908: { name: "OWNER_VM_EXEC", desc: "VM Core-1 Supervisor Task" }
  };

  // VM Types String Helper
  const VM_TYPE_NAMES = {
    0: "NONE",
    1: "PTR",
    2: "U8",
    3: "U32",
    4: "I32",
    5: "F",
    6: "B",
    7: "STR"
  };

  const VM_REG_NAMES = {
    0: "object",
    1: "accessor",
    2: "block"
  };

  const VM_INDEX_KIND_NAMES = {
    0: "LITERAL",
    1: "REF",
    2: "NAME"
  };

  const VM_EXPR_MATH_REASONS = {
    0: "divide by zero",
    1: "operand outside domain",
    2: "result not finite"
  };

  // Error Tags Enum Table (Ordered as in SYS_ERROR_MAP in C)
  const ERROR_TAG_DEFINITIONS = {
    // Base Tags
    1: {
      name: "ERR_NULL_PTR",
      unpack: (view, off) => ({}),
      format: (p) => "Null pointer dereference"
    },
    2: {
      name: "ERR_BASE_NO_MEM",
      unpack: (view, off) => ({}),
      format: (p) => "Memory allocation failed (out of system heap)"
    },
    3: {
      name: "ERR_TIMEOUT",
      unpack: (view, off) => ({ ms: view.getUint32(off, true) }),
      format: (p) => `Operation timed out after ${p.ms} ms`
    },

    // VM Core Error Tags
    100: {
      name: "ERR_VM_ALLOC_EXHAUSTED",
      unpack: (view, off) => ({
        requested: view.getUint32(off, true),
        remaining: view.getUint32(off + 4, true)
      }),
      format: (p) => `VM arena exhausted: requested ${p.requested} bytes, only ${p.remaining} remaining`
    },
    101: {
      name: "ERR_VM_ACCESSOR_UNKNOWN_ID",
      unpack: (view, off) => ({ id: view.getUint16(off, true) }),
      format: (p) => `Accessor referenced unknown object id ${p.id}`
    },
    102: {
      name: "ERR_VM_ACCESSOR_OOB",
      unpack: (view, off) => ({
        id: view.getUint16(off, true),
        chain_pos: view.getUint8(off + 2),
        index: view.getUint16(off + 4, true),
        obj_id: view.getUint16(off + 6, true)
      }),
      format: (p) => `Accessor ${p.id}: index ${p.index} out of range at chain position ${p.chain_pos} (obj_id=${p.obj_id})`,
      extractObjId: (p) => p.obj_id
    },
    103: {
      name: "ERR_VM_ACCESSOR_TYPE_MISMATCH",
      unpack: (view, off) => ({
        id: view.getUint16(off, true),
        chain_pos: view.getUint8(off + 2),
        expected: view.getUint8(off + 3),
        actual: view.getUint8(off + 4),
        obj_id: view.getUint16(off + 6, true)
      }),
      format: (p) => {
        const expStr = VM_TYPE_NAMES[p.expected] || `Type#${p.expected}`;
        const actStr = VM_TYPE_NAMES[p.actual] || `Type#${p.actual}`;
        return `Accessor ${p.id}: expected type ${expStr} at chain position ${p.chain_pos}, got ${actStr} (obj_id=${p.obj_id})`;
      },
      extractObjId: (p) => p.obj_id
    },
    104: {
      name: "ERR_VM_ACCESSOR_NULL_OBJ",
      unpack: (view, off) => ({
        id: view.getUint16(off, true),
        chain_pos: view.getUint8(off + 2),
        parent_id: view.getUint16(off + 4, true)
      }),
      format: (p) => `Accessor ${p.id}: chain position ${p.chain_pos} dereferenced a null object (parent_id=${p.parent_id})`,
      extractObjId: (p) => p.parent_id
    },
    105: {
      name: "ERR_VM_ACCESSOR_NOT_MUTABLE",
      unpack: (view, off) => ({
        id: view.getUint16(off, true),
        chain_pos: view.getUint8(off + 2),
        obj_id: view.getUint16(off + 4, true)
      }),
      format: (p) => `Accessor ${p.id}: write rejected at chain position ${p.chain_pos}, target is not mutable / read-only (obj_id=${p.obj_id})`,
      extractObjId: (p) => p.obj_id
    },
    106: {
      name: "ERR_VM_BLOCK_INPUT_UNRESOLVED",
      unpack: (view, off) => ({
        block_idx: view.getUint16(off, true),
        input_idx: view.getUint8(off + 2)
      }),
      format: (p) => `Block #${p.block_idx} input ${p.input_idx} failed to resolve`,
      extractBlockIdx: (p) => p.block_idx
    },
    107: {
      name: "ERR_VM_BLOCK_PIN_MISSING",
      unpack: (view, off) => ({
        block_idx: view.getUint16(off, true),
        pin_id: view.getUint8(off + 2),
        is_out: view.getUint8(off + 3)
      }),
      format: (p) => `Block #${p.block_idx} has no ${p.is_out ? 'output' : 'input'} pin ${p.pin_id}`,
      extractBlockIdx: (p) => p.block_idx
    },
    108: {
      name: "ERR_VM_BLOCK_FAILED",
      unpack: (view, off) => ({
        block_idx: view.getUint16(off, true),
        block_type: view.getUint8(off + 2)
      }),
      format: (p) => `Block #${p.block_idx} (type ${p.block_type}) execution failed`,
      extractBlockIdx: (p) => p.block_idx
    },
    109: {
      name: "ERR_VM_OBJ_NOT_MUTABLE",
      unpack: (view, off) => ({
        obj_id: view.getUint16(off, true)
      }),
      format: (p) => `Object is constant/read-only: write rejected (obj_id=${p.obj_id})`,
      extractObjId: (p) => p.obj_id
    },
    110: {
      name: "ERR_VM_OBJ_OOB",
      unpack: (view, off) => ({
        index: view.getUint16(off, true),
        obj_id: view.getUint16(off + 2, true)
      }),
      format: (p) => `Object index ${p.index} is past the end of its allocated elements (obj_id=${p.obj_id})`,
      extractObjId: (p) => p.obj_id
    },
    111: {
      name: "ERR_VM_OBJ_USR_PROTECTED",
      unpack: (view, off) => ({
        obj_id: view.getUint16(off, true)
      }),
      format: (p) => `Object is protected from user writes (obj_id=${p.obj_id})`,
      extractObjId: (p) => p.obj_id
    },
    112: {
      name: "ERR_VM_REG_DUP",
      unpack: (view, off) => ({
        kind: view.getUint8(off),
        id: view.getUint16(off + 2, true)
      }),
      format: (p) => {
        const kStr = VM_REG_NAMES[p.kind] || `kind#${p.kind}`;
        return `${kStr} registry: id ${p.id} is already bound (duplicate definition)`;
      },
      extractObjId: (p) => (p.kind === 0 ? p.id : null),
      extractBlockIdx: (p) => (p.kind === 2 ? p.id : null)
    },
    113: {
      name: "ERR_VM_EXPR_MATH",
      unpack: (view, off) => ({
        block_idx: view.getUint16(off, true),
        pc: view.getUint16(off + 2, true),
        opcode: view.getUint8(off + 4),
        reason: view.getUint8(off + 5)
      }),
      format: (p) => {
        const rStr = VM_EXPR_MATH_REASONS[p.reason] || `code ${p.reason}`;
        return `Block #${p.block_idx} expression math error at PC ${p.pc} (op 0x${p.opcode.toString(16).toUpperCase()}): ${rStr}`;
      },
      extractBlockIdx: (p) => p.block_idx
    },
    114: {
      name: "ERR_VM_EXEC_BLOCK_HUNG",
      unpack: (view, off) => ({
        block_idx: view.getUint16(off, true),
        ms: view.getUint16(off + 2, true)
      }),
      format: (p) => `Block #${p.block_idx} watchdog timeout: has been executing for over ${p.ms} ms`,
      extractBlockIdx: (p) => p.block_idx
    },
    115: {
      name: "ERR_VM_FOR_BAD_LOOP",
      unpack: (view, off) => ({
        block_idx: view.getUint16(off, true),
        turns: view.getUint32(off + 4, true),
        cap: view.getUint16(off + 8, true),
        reason: view.getUint8(off + 10)
      }),
      format: (p) => `Block #${p.block_idx} loop exceeded budget: ran ${p.turns} turns (cap was ${p.cap})`,
      extractBlockIdx: (p) => p.block_idx
    }
  };

  /**
   * Decodes a binary error packet matching enc_sys_errors.h layout.
   * @param {Uint8Array|ArrayBuffer} rawBuffer 
   * @returns {Object} Decoded packet structure
   */
  function decodeBinaryErrorPacket(rawBuffer) {
    const bytes = rawBuffer instanceof Uint8Array ? rawBuffer : new Uint8Array(rawBuffer);
    if (bytes.length < 3) {
      throw new Error(`Packet truncated: ${bytes.length} bytes is less than min header (3)`);
    }

    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const version = view.getUint8(0);
    const nodeCount = view.getUint8(1);
    const depth = view.getUint8(2);

    if (version !== 1 && version !== 2) throw new Error(`Unsupported error format ${version}`);
    if (version === 2 && bytes.length < 7) throw new Error("Truncated v2 error header");
    const schemaId = version === 2 ? view.getUint32(3, true) : null;
    let offset = version === 2 ? 7 : 3;
    const nodes = [];

    for (let i = 0; i < nodeCount && offset + 5 <= bytes.length; i++) {
      const payloadLen = view.getUint8(offset);
      const tag = view.getUint16(offset + 1, true);
      const owner = view.getUint16(offset + 3, true);
      offset += 5;

      const payloadEnd = offset + payloadLen;
      if (payloadEnd > bytes.length) {
        break;
      }

      const tagDef = (version === 1 ? ERROR_TAG_DEFINITIONS[tag] : null) || {
        name: `ERR_TAG_0x${tag.toString(16).toUpperCase()}`,
        unpack: (v, off, len) => ({ rawBytes: Array.from(bytes.slice(off, off + len)) }),
        format: (p) => `Raw error tag 0x${tag.toString(16).toUpperCase()}`
      };

      const ownerDef = SYS_OWNERS[owner] || {
        name: `OWNER_0x${owner.toString(16).toUpperCase()}`,
        desc: `Subsystem 0x${owner.toString(16).toUpperCase()}`
      };

      let payloadObj = {};
      try {
        payloadObj = tagDef.unpack(view, offset, payloadLen);
      } catch (err) {
        payloadObj = { decodeErr: err.message };
      }

      let description = "";
      try {
        description = tagDef.format(payloadObj);
      } catch (err) {
        description = `Error formatting tag ${tagDef.name}`;
      }

      const relatedObjId = tagDef.extractObjId ? tagDef.extractObjId(payloadObj) : null;
      const relatedBlockIdx = tagDef.extractBlockIdx ? tagDef.extractBlockIdx(payloadObj) : null;

      nodes.push({
        index: i,
        tag: tag,
        tagName: tagDef.name,
        owner: owner,
        ownerName: ownerDef.name,
        ownerDesc: ownerDef.desc,
        payloadLen: payloadLen,
        payload: payloadObj,
        description: description,
        relatedObjId: (relatedObjId !== null && relatedObjId !== 0xFFFF) ? relatedObjId : null,
        relatedBlockIdx: relatedBlockIdx
      });

      offset = payloadEnd;
    }

    return {
      version: version,
      schemaId: schemaId,
      schemaKnown: version === 1,
      nodeCount: nodeCount,
      depth: depth,
      isTruncated: depth > nodeCount || nodes.length < nodeCount,
      nodes: nodes,
      rawHex: Array.from(bytes).map(b => ("00" + b.toString(16).toUpperCase()).slice(-2)).join(" ")
    };
  }

  /**
   * Parses standard ESP-IDF Serial / LOGI format:
   * e.g. "I (12845) [VM]: Core-1 tick cycle 420 pass OK"
   * e.g. "W (13100) [PMU]: Voltage dropped to 3.18V"
   * e.g. "E (14200) [VM_OBJ]: Accessor 2 write rejected on const object #24"
   */
  function parseEspLogLine(rawLine) {
    const str = String(rawLine).trim();
    const match = str.match(/^([IWEVD])\s*\((\d+)\)\s*\[([^\]]+)\]:\s*(.*)$/);

    if (match) {
      const levelChar = match[1];
      const timeMs = parseInt(match[2], 10);
      const tag = match[3];
      const message = match[4];

      let level = "INFO";
      if (levelChar === "W") level = "WARN";
      else if (levelChar === "E") level = "ERROR";
      else if (levelChar === "D") level = "DEBUG";
      else if (levelChar === "V") level = "VERBOSE";

      let referencedObjId = null;
      const objMatch = message.match(/(?:obj_id=|object\s*#|#)(\d+)/i);
      if (objMatch) {
        referencedObjId = parseInt(objMatch[1], 10);
      }

      let referencedBlockIdx = null;
      const blkMatch = message.match(/(?:block\s*#?|blk_id=)(\d+)/i);
      if (blkMatch) {
        referencedBlockIdx = parseInt(blkMatch[1], 10);
      }

      return {
        isFormattedEspLog: true,
        level: level,
        levelChar: levelChar,
        timeMs: timeMs,
        tag: tag,
        message: message,
        referencedObjId: referencedObjId,
        referencedBlockIdx: referencedBlockIdx,
        rawText: str
      };
    }

    return {
      isFormattedEspLog: false,
      level: str.toLowerCase().includes("err") ? "ERROR" : str.toLowerCase().includes("warn") ? "WARN" : "INFO",
      levelChar: "I",
      timeMs: Date.now() % 100000,
      tag: "SYS",
      message: str,
      referencedObjId: null,
      referencedBlockIdx: null,
      rawText: str
    };
  }

  /**
   * Helper to build a binary error packet for simulation & testing.
   */
  function createMockBinaryErrorPacket(scenario, targetObjId = 3) {
    if (scenario === "accessor_oob") {
      return new Uint8Array([
        0x01, 0x01, 0x01, // Header: v=1, count=1, depth=1
        0x08,             // payload_len: 8
        102, 0x00,        // tag: 102 (ERR_VM_ACCESSOR_OOB)
        0x03, 0xA9,       // owner: 0xA903 (OWNER_VM_ACCESSOR)
        0x02, 0x00,       // accessor id = 2
        0x01, 0x00,       // chain_pos = 1
        0x06, 0x00,       // index = 6 (out of bounds)
        targetObjId & 0xFF, (targetObjId >> 8) & 0xFF // obj_id
      ]);
    }

    if (scenario === "not_mutable") {
      return new Uint8Array([
        0x01, 0x01, 0x01,
        0x08,
        105, 0x00,        // ERR_VM_ACCESSOR_NOT_MUTABLE
        0x03, 0xA9,       // OWNER_VM_ACCESSOR
        0x05, 0x00,       // accessor id = 5
        0x00, 0x00,       // chain_pos = 0
        targetObjId & 0xFF, (targetObjId >> 8) & 0xFF, // obj_id
        0x00, 0x00
      ]);
    }

    if (scenario === "math_div_zero") {
      return new Uint8Array([
        0x01, 0x01, 0x01,
        0x08,
        113, 0x00,        // ERR_VM_EXPR_MATH
        0x05, 0xA9,       // OWNER_VM_CODE
        0x02, 0x00,       // block_idx = 2
        0x14, 0x00,       // pc = 20
        0x04,             // opcode = 0x04 (DIV)
        0x00,             // reason = 0 (divide by zero)
        0x00, 0x00
      ]);
    }

    // Default: ERR_VM_ALLOC_EXHAUSTED
    return new Uint8Array([
      0x01, 0x01, 0x01,
      0x08,
      100, 0x00,
      0x01, 0xA9,
      0x00, 0x08, 0x00, 0x00, // requested = 2048
      0x80, 0x00, 0x00, 0x00  // remaining = 128
    ]);
  }

  // =========================================================================
  // VM LOADER PROTOCOL (CLASS 0x04) DECODER & TX FORMATTER
  // =========================================================================

  const VM_EXEC_COMMAND_NAMES = {
    0: "SCAN_MODE",
    1: "ONCE",
    2: "BLOCK_MODE",
    3: "NEXT",
    4: "REWIND",
    5: "NORMAL_MODE",
    6: "PAUSE",
    7: "RESUME",
    8: "RESET"
  };

  /**
   * Decodes an outbound or inbound Class 0x04 VM Loader packet into human-readable details.
   * Matches components/codecs/decoders/dec_vm_loader.h.
   */
  function decodeVmLoaderPacket(bytes) {
    if (!bytes || bytes.length < 2) {
      return { valid: false, error: "Packet too short (< 2 bytes)" };
    }

    const arr = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    const view = new DataView(arr.buffer, arr.byteOffset, arr.byteLength);

    const cls = arr[0];
    if (cls !== 0x04) {
      return { valid: false, error: `Not a Class 0x04 packet (got 0x${cls.toString(16)})` };
    }

    const pktType = arr[1];
    let result = {
      valid: true,
      packetClass: 0x04,
      opcode: pktType,
      opcodeHex: "0x" + ("00" + pktType.toString(16).toUpperCase()).slice(-2),
      name: `Unknown (0x${pktType.toString(16)})`,
      desc: "",
      fields: {}
    };

    switch (pktType) {
      case 0x40: // VM Reset
        result.name = "VM Reset (0x40)";
        result.desc = "Tear down active programs, clear registries and arena, stop execution in fail-closed state";
        break;

      case 0x41: // VM Open
        if (arr.length >= 12) {
          const objCnt = view.getUint16(2, true);
          const accCnt = view.getUint16(4, true);
          const blkCnt = view.getUint16(6, true);
          const totalSize = view.getUint32(8, true);
          result.name = "VM Open Container (0x41)";
          result.desc = `Open container: allocate ${objCnt} objects, ${accCnt} accessors, ${blkCnt} blocks, ${totalSize} bytes bump arena`;
          result.fields = { objCnt, accCnt, blkCnt, totalSize };
        } else {
          result.desc = "VM Open Container: truncated frame";
        }
        break;

      case 0x42: // Add Objects Batch
        if (arr.length >= 3) {
          const n = arr[2];
          result.name = `Add Objects Batch (0x42)`;
          result.desc = `Register batch of ${n} object(s) into VM object arena`;
          result.fields = { count: n };
        }
        break;

      case 0x43: // Seed Initial Data
        if (arr.length >= 3) {
          const n = arr[2];
          result.name = `Seed Initial Data (0x43)`;
          result.desc = `Upload ${n} initial data / runtime override payload chunk(s)`;
          result.fields = { count: n };
        }
        break;

      case 0x44: // Add Accessors Batch
        if (arr.length >= 3) {
          const n = arr[2];
          result.name = `Add Accessors Batch (0x44)`;
          result.desc = `Register ${n} relational accessor descriptor(s)`;
          result.fields = { count: n };
        }
        break;

      case 0x45: // Add Block
        if (arr.length >= 16) {
          const blkId = view.getUint16(2, true);
          const blkIdx = view.getUint16(4, true);
          const blkType = arr[6];
          result.name = `Add Block #${blkId} (0x45)`;
          result.desc = `Attach function block #${blkId} (type ${blkType}) at sequence step ${blkIdx}`;
          result.fields = { blkId, blkIdx, blkType };
        }
        break;

      case 0x47: // Subscribe Live Telemetry
        if (arr.length >= 3) {
          const n = arr[2];
          const ids = [];
          for (let i = 0; i < n && (3 + i * 2 + 2) <= arr.length; i++) {
            ids.push(view.getUint16(3 + i * 2, true));
          }
          result.name = `Subscribe Live Telemetry (0x47)`;
          result.desc = `Subscribe ${n} object(s) for live telemetry streaming: [${ids.map(id => '#' + id).join(', ')}]`;
          result.fields = { count: n, objectIds: ids };
        }
        break;

      case 0x48: // Execution Control
        if (arr.length >= 3) {
          const cmd = arr[2];
          const cmdName = VM_EXEC_COMMAND_NAMES[cmd] || `CMD_${cmd}`;
          result.name = `Execution Control (0x48 ${cmdName})`;
          result.desc = `Trigger VM supervisor execution command: ${cmdName} (${cmd})`;
          result.fields = { command: cmd, commandName: cmdName };
        }
        break;

      default:
        result.desc = `Custom VM Packet opcode 0x${pktType.toString(16)}`;
    }

    return result;
  }

  /**
   * Formats an outbound packet into a Stream Monitor item
   */
  function formatTxStreamItem(packet) {
    const rawBytes = packet.bytes ? new Uint8Array(packet.bytes) : (packet.raw || new Uint8Array(0));
    const decoded = decodeVmLoaderPacket(rawBytes);

    return {
      id: "tx_" + Date.now() + "_" + Math.random().toString(36).substr(2, 4),
      timestamp: new Date().toLocaleTimeString("en-GB", { hour12: false }) + "." + ("00" + (Date.now() % 1000)).slice(-3),
      streamType: STREAM_TYPE.RAW_HEX,
      isTx: true,
      tag: "TX " + (decoded.opcodeHex || "PKT"),
      tagClass: "tag-tx",
      title: packet.name || decoded.name,
      summary: decoded.desc || `Transmitted ${rawBytes.length} bytes to device`,
      hexDump: packet.hex || Array.from(rawBytes).map(b => ("00" + b.toString(16).toUpperCase()).slice(-2)).join(" "),
      byteCount: rawBytes.length,
      decodedInfo: decoded
    };
  }

  return {
    STREAM_TYPE,
    SYS_OWNERS,
    ERROR_TAG_DEFINITIONS,
    decodeBinaryErrorPacket,
    decodeVmLoaderPacket,
    formatTxStreamItem,
    parseEspLogLine,
    createMockBinaryErrorPacket
  };

})();

// Export for module/Node or window
if (typeof module !== "undefined" && module.exports) {
  module.exports = StreamDecoder;
}
