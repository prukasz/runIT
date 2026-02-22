import struct
from enum import IntEnum

from BlockBase import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from Mem import Mem, Ref

class BlockLatchCfg(IntEnum):
    """Configuration options for BlockLatch - matches C block_latch_cfg_t"""
    LATCH_SR = 0  # S=1,R=0 -> Q=1; S=0,R=1 -> Q=0; S=R=0 -> hold; S=R=1 -> invalid
    LATCH_RS = 1  # R=1,S=0 -> Q=0; R=0,S=1 -> Q=1; R=S=0 -> hold; R=S=1 -> invalid

class BlockLatch(Block):
    def __init__(self,
                 idx: int,
                 ctx_id,
                 set: Ref,
                 reset: Ref,
                 en: Ref = None,
                 alias: str = None,
                 mem: Mem = None,
                 latch_type: BlockLatchCfg = BlockLatchCfg.LATCH_SR):
        
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_LATCH, ctx_id=ctx_id, alias=alias, mem=mem)
        
        self.latch_type = latch_type
        # in_0 = EN, in_1 = selector
        self.add_inputs([en, set, reset])
        
        self.add_outputs([(mem_types_t.MEM_B,)])
    
    # No custom data packets needed - C code handles the switch logic
    def pack_data(self) -> list[bytes]:
        packets = []
        
        # Config packet (packet_id = 0x01: LATCH config)
        header = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CFG
        )
        payload = struct.pack('<B', self.latch_type << 1)
        packets.append(header + payload)
        
        return packets