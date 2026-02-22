from BlockBase import Block
from Enums import block_types_t
from Mem import Mem, Ref

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
    
    def __init__(self, idx: int, ctx_id, value: Ref, target: Ref, en: Ref = None, alias: str = None, mem: Mem = None):

        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_SET, ctx_id=ctx_id, alias=alias, mem=mem)
        
        # Add inputs: [EN, value, target]
        self.add_inputs([en, value, target])
        

    def pack_data(self):
        return []