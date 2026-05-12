
import struct
from typing import Union
from BlockBase import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from Mem import Mem, Ref


class BlockClock(Block):
    """
    Clock with pseudo-PWM output. Generates a square wave with configurable period and pulse width.
     - Inputs: [EN (bool), PERIOD (ms, optional), WIDTH (ms, optional)]
     - Output: Q (bool)
     - Witdh resolution limited by loop period 
    """
    
    def __init__(self,
                 idx: int,
                 ctx_id,
                 en: Ref,
                 period_ms: Union[int, Ref] = 1000.0,
                 width_ms: Union[int, Ref] = 1,
                 alias: str = None,
                 mem: Mem = None
                 ):

        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_CLOCK, ctx_id=ctx_id, alias=alias, mem=mem)
        
        # Process parameters
        def process_param(arg, default_val):
            if isinstance(arg, (int)):
                return int(arg), None
            elif isinstance(arg, Ref):
                return default_val, arg
            return default_val, None
        
        self.config_period, period_ref = process_param(period_ms, 1000)
        self.config_width, width_ref = process_param(width_ms, 500)
        
        # Inputs: [EN, PERIOD, WIDTH] (pass there NULL if value is constant)
        self.add_inputs([en, period_ref, width_ref])
        
        # Output: Q (Boolean)
        self.add_outputs([(mem_types_t.MEM_B,)])
    
    def pack_data(self) -> list[bytes]:
        """
        Pack clock configuration data.
        
        Format: [BA][block_idx:u16][block_type:u8][packet_id:u8][period:u32][width:u32]
        """
        packets = []
        
        # Config packet (packet_id = 0x01: clock config)
        header = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CFG
        )
        # Pack as uint32_t (4 bytes each)
        payload = struct.pack('<II', self.config_period, self.config_width)
        packets.append(header + payload)
        
        return packets

    

