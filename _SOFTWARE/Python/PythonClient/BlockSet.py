from BlockBase import Block
from Enums import block_types_t, packet_header_t, block_packet_id_t, mem_types_t
from Mem import Mem, Ref
from typing import Union
import struct

class BlockSet(Block):
    """
    SET block - Assigns value to target variable.
    
    Inputs:
        - in_0 (EN): Enable signal - block only executes when EN is true
        - in_1: Source value reference (what to write)
        - in_2: Target reference (where to write)
 
    Outputs:
        - None (no ENO output for SET block)
    
    Usage:
        block = BlockSet(
            idx=1,
            ctx=my_context,
            target=Ref("motor_speed"),
            value=Ref("calculated_speed"),
            en=Ref("enable")
        )
    """
    
    def __init__(self, idx: int, ctx_id, value: Union[Ref, int, float], target: Ref, en: Ref = None, alias: str = None, mem: Mem = None):

        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_SET, ctx_id=ctx_id, alias=alias, mem=mem)
        self.value = value

        # Add inputs: [EN, value, target]
        if isinstance(self.value, (Ref)):
            self.add_inputs([en, value, target])
        else: 
            self.add_inputs([en, None, target])
        
    # TO DO add option to pass constants directly as custom data
    def pack_data(self) -> list[bytes]:
        packets = []
        if isinstance(self.value, (Ref)):
            return packets
        packet = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CONSTANTS
        )
        if isinstance(self.value, (int)):
            packet += struct.pack('<Bi', mem_types_t.MEM_I32, int(self.value))
        elif isinstance(self.value, (float)):
            packet += struct.pack('<Bf', mem_types_t.MEM_F, float(self.value))

        packets.append(packet)
        return packets