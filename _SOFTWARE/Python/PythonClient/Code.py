import io
import re
import struct
from typing import Dict, List, Optional, Union, TYPE_CHECKING

from Enums import packet_header_t, mem_types_t
from Mem import Mem, Ref, ref_from_str, set_global_mem
from BlockBase import Block

if TYPE_CHECKING:
    from BlockMath import BlockMath
    from BlockLogic import BlockLogic
    from BlockSet import BlockSet
    from BlockFor import BlockFor
    from BlockClock import BlockClock
    from BlockTimer import BlockTimer
    from BlockCounter import BlockCounter
    from BlockInSelector import BlockInSelector
    from BlockQSelector import BlockQSelector
    from BlockLatch import BlockLatch


# ============================================================================
# Context IDs
# ============================================================================
CTX_USER   = 0   # Context 0: User-created variables
CTX_BLOCKS = 1   # Context 1: Block outputs (hidden from API)


# ============================================================================
# Auto-idx counter
# ============================================================================

class _IdxCounter:
    """Auto-incrementing counter (high start so no clash before reindex)."""
    def __init__(self, start: int = 6000):
        self._next = start

    def __call__(self) -> int:
        val = self._next
        self._next += 1
        return val


# ============================================================================
# CODE
# ============================================================================

class Code:
    """
    * ``var()``              — add user variables
    * ``add_math()``         — Math expression block
    * ``add_logic()``        — Logic expression block
    * ``add_set()``          — Set (assign) block
    * ``add_for()``          — For loop block
    * ``add_clock()``        — Clock (square wave) block
    * ``add_timer()``        — Timer (TON/TOF/TP) block
    * ``add_counter()``      — Up/Down counter block
    * ``add_in_selector()``  — Input multiplexer block
    * ``add_q_selector()``   — Output demultiplexer block
    * ``add_latch()``        — SR/RS latch block
    * ``generate()``         — sort → reindex → write hex dump
    """

    def __init__(self):
        self.mem = Mem()
        set_global_mem(self.mem)

        self.blocks: Dict[int, Block] = {}
        self._block_aliases: Dict[str, Block] = {}
        self._idx = _IdxCounter()

    # ====================================================================
    # Variables
    # ====================================================================

    def var(self, var_type: mem_types_t, alias: str,
            data=None, dims: Optional[List[int]] = None):
        """Create a user variable in context 0."""
        self.mem.add_instance(CTX_USER, var_type, alias,
                              data=data, dims=dims)

    # ====================================================================
    # Resolve helpers
    # ====================================================================

    def _resolve(self, value):
        """
        Convert a value to the type expected by block constructors.

        Accepted inputs:
          - ``None``            → ``None``
          - ``int`` / ``float`` → passed through (for config constants)
          - ``Ref``             → passed through
          - ``str``             → checked as block alias (``"blk[N]"``),
                                  otherwise resolved via ``ref_from_str``
        """
        if value is None or isinstance(value, (int, float, Ref)):
            return value
        if isinstance(value, str):
            # Block-output shorthand: "alias[N]" where alias is a known block
            m = re.match(r'^(\w+)\[(\d+)\]$', value)
            if m:
                name, out_idx = m.group(1), int(m.group(2))
                if name in self._block_aliases:
                    return self._block_aliases[name].out[out_idx]
            # General mem reference (scalar, array, nested indices)
            return ref_from_str(value, self.mem)
        raise TypeError(f"Cannot resolve {type(value)} to Ref")

    def _resolve_list(self, values: list) -> list:
        """Resolve every element in *values* via ``_resolve``."""
        return [self._resolve(v) for v in values]

    # ====================================================================
    # Expression parser (Math / Logic)
    # ====================================================================

    def _extract_refs_from_expr(self, expression: str):
        """
        Scan *expression* for ``"quoted"`` variable tokens, resolve each
        via ``ref_from_str`` (or block-alias lookup), and replace with
        ``in_N``.

        Returns ``(rewritten_expression, connections_list)``.

        Supported quoted forms:
          - ``"var"``          → ``Ref("var")``
          - ``"arr[0]"``       → ``Ref("arr")[0]``
          - ``"arr[idx]"``     → ``Ref("arr")[Ref("idx")]``
          - ``"blk_alias[1]"`` → ``block.out[1]`` if *blk_alias* is known
        """
        refs: list = []
        alias_to_in: dict = {}

        def _next_in():
            return len(refs) + 1          # in_1, in_2, …

        def _register(key: str, ref_obj):
            """De-duplicate: same key → same in_N slot."""
            if key not in alias_to_in:
                idx = _next_in()
                alias_to_in[key] = idx
                refs.append(ref_obj)
            return f'in_{alias_to_in[key]}'

        def _resolve_quoted(text: str):
            """Resolve the content of a quoted token, handling block outputs and nested quotes."""
            # Check simple block-output alias first: name[N]
            m = re.match(r'^(\w+)\[(\d+)\]$', text)
            if m:
                name, out_idx = m.group(1), int(m.group(2))
                if name in self._block_aliases:
                    return self._block_aliases[name].out[out_idx]
            
            # Handle nested quotes in array indices: table["f[1]"]
            # Replace quoted strings inside brackets with resolved refs
            if '["' in text:
                # Build the ref step by step
                # Parse base alias
                bracket_pos = text.find('[')
                if bracket_pos == -1:
                    # No brackets, simple variable
                    return ref_from_str(text, self.mem)
                
                base_alias = text[:bracket_pos].strip()
                ref = Ref(base_alias)
                
                # Parse each [...] group
                i = bracket_pos
                while i < len(text) and text[i] == '[':
                    i += 1  # skip '['
                    # Find the content inside brackets
                    if i < len(text) and text[i] == '"':
                        # Quoted index - extract and recursively resolve
                        i += 1  # skip opening "
                        j = i
                        bracket_depth = 0
                        while j < len(text):
                            if text[j] == '[':
                                bracket_depth += 1
                            elif text[j] == ']':
                                if bracket_depth == 0:
                                    break  # End of this index
                                bracket_depth -= 1
                            elif text[j] == '"' and bracket_depth == 0:
                                # Found closing quote
                                break
                            j += 1
                        quoted_content = text[i:j]
                        nested_ref = _resolve_quoted(quoted_content)
                        ref.indices.append((False, nested_ref))
                        i = j + 1  # skip closing "
                        # Skip to ']'
                        while i < len(text) and text[i] in ' \t':
                            i += 1
                        if i < len(text) and text[i] == ']':
                            i += 1  # skip ']'
                    elif i < len(text) and (text[i].isdigit() or text[i] == '-'):
                        # Integer literal
                        j = i
                        if text[j] == '-':
                            j += 1
                        while j < len(text) and text[j].isdigit():
                            j += 1
                        ref.indices.append((True, int(text[i:j])))
                        i = j
                        # Skip to ']'
                        while i < len(text) and text[i] in ' \t':
                            i += 1
                        if i < len(text) and text[i] == ']':
                            i += 1  # skip ']'
                    else:
                        # Unquoted alias - let ref_from_str handle the rest
                        return ref_from_str(text, self.mem)
                
                return ref
            
            # General: ref_from_str handles "var", "arr[0]", "arr[idx]" …
            return ref_from_str(text, self.mem)

        # Character-level scan: find "…" tokens, leave the rest untouched
        out_parts: list = []
        i = 0
        while i < len(expression):
            if expression[i] == '"':
                # Find matching close quote, tracking bracket depth to handle nested quotes
                j = i + 1
                bracket_depth = 0
                while j < len(expression):
                    if expression[j] == '[':
                        bracket_depth += 1
                    elif expression[j] == ']':
                        bracket_depth -= 1
                    elif expression[j] == '"' and bracket_depth == 0:
                        # Found the closing quote at same bracket level
                        break
                    j += 1
                if j >= len(expression):
                    raise ValueError(
                        f"Unterminated quote in expression at position {i}")
                content = expression[i + 1 : j]
                j += 1                               # skip closing "
                ref = _resolve_quoted(content)
                key = f'__expr__{content}'
                out_parts.append(_register(key, ref))
                i = j
            else:
                out_parts.append(expression[i])
                i += 1

        return ''.join(out_parts), refs

    # ====================================================================
    # FOR expression parser
    # ====================================================================

    @staticmethod
    def _parse_for_expr(expr: str):
        """
        Parse C-style ``"i=0; i<10; i+=1"`` into
        ``(start, cond_str, limit, op_str, step)``.

        Numeric tokens are returned as ``float``; anything else as ``str``
        (to be resolved by ``_resolve`` later).
        """
        parts = [p.strip() for p in expr.split(';')]
        if len(parts) != 3:
            raise ValueError(
                f"For expression must have 3 semicolon-separated parts, "
                f"got {len(parts)}: '{expr}'"
            )

        # init: var = value
        init_m = re.match(r'(\w+)\s*=\s*(.+)', parts[0])
        if not init_m:
            raise ValueError(f"Cannot parse init part: '{parts[0]}'")
        start_tok = init_m.group(2).strip()

        # condition: var <op> value
        cond_m = re.match(r'\w+\s*(<=|>=|<|>)\s*(.+)', parts[1])
        if not cond_m:
            raise ValueError(f"Cannot parse condition part: '{parts[1]}'")
        cond_str  = cond_m.group(1)
        limit_tok = cond_m.group(2).strip()

        # step: var <op>= value  OR  var = var <op> value
        step_m = re.match(r'\w+\s*([+\-*/])=\s*(.+)', parts[2])
        if not step_m:
            step_m = re.match(r'\w+\s*=\s*\w+\s*([+\-*/])\s*(.+)', parts[2])
            if not step_m:
                raise ValueError(f"Cannot parse step part: '{parts[2]}'")
        op_str   = step_m.group(1)
        step_tok = step_m.group(2).strip()

        def _to_val(tok):
            if tok.startswith('"') and tok.endswith('"'):
                tok = tok[1:-1]
            try:
                return float(tok)
            except ValueError:
                return tok

        return _to_val(start_tok), cond_str, _to_val(limit_tok), op_str, _to_val(step_tok)

    # ====================================================================
    # Block registration
    # ====================================================================

    def _register_block(self, block: Block,
                        alias: Optional[str] = None) -> Block:
        """Store block by idx; optionally record a named alias."""
        idx = block.cfg.block_idx
        if idx in self.blocks:
            raise ValueError(f"Block with index {idx} already exists")
        if alias is not None:
            if alias in self._block_aliases:
                raise ValueError(f"Block alias '{alias}' already in use")
            self._block_aliases[alias] = block
        self.blocks[idx] = block
        return block

    # ====================================================================
    # Block factory methods
    # ====================================================================

    # ── Math ───────────────────────────────────────────────────────────
    def add_math(self, expression: str,
                 connections=None,
                 en=None,
                 alias: Optional[str] = None) -> 'BlockMath':
        """
        Add a Math expression block.

        *expression* may contain ``"quoted"`` variable names that are
        auto-resolved to ``Ref`` and assigned ``in_N`` slots.
        If *connections* is provided explicitly, the expression must
        already use ``in_N`` placeholders.

        :param expression:   e.g. ``'"temperature" * 2 + "offset"'``
        :param connections:  explicit list of Refs (optional)
        :param en:           enable — ``str``, ``Ref``, or ``None``
        :param alias:        block alias for later reference
        """
        from BlockMath import BlockMath
        en = self._resolve(en)
        if connections is None:
            expression, connections = self._extract_refs_from_expr(expression)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockMath(idx=idx, ctx_id=CTX_BLOCKS,
                          expression=expression,
                          connections=connections or None,
                          en=en, alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── Logic ──────────────────────────────────────────────────────────
    def add_logic(self, expression: str,
                  connections=None,
                  en=None,
                  alias: Optional[str] = None) -> 'BlockLogic':
        """
        Add a Logic expression block.

        Same quoting rules as ``add_math``.

        :param expression:   e.g. ``'"temperature" > 50 && "pressure" < 100'``
        :param connections:  explicit list of Refs (optional)
        :param en:           enable
        :param alias:        block alias
        """
        from BlockLogic import BlockLogic
        en = self._resolve(en)
        if connections is None:
            expression, connections = self._extract_refs_from_expr(expression)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockLogic(idx=idx, ctx_id=CTX_BLOCKS,
                           expression=expression,
                           connections=connections or None,
                           en=en, alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── Set ────────────────────────────────────────────────────────────
    def add_set(self, target, value, en=None,
                alias: Optional[str] = None) -> 'BlockSet':
        """
        Add a Set (assign) block.

        :param target:  where to write (``str`` or ``Ref``)
        :param value:   what to write  (``str`` or ``Ref``)
        :param en:      enable
        :param alias:   block alias
        """
        from BlockSet import BlockSet
        target = self._resolve(target)
        value  = self._resolve(value)
        en     = self._resolve(en)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockSet(idx=idx, ctx_id=CTX_BLOCKS,
                         target=target, value=value, en=en,
                         alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── For ────────────────────────────────────────────────────────────
    def add_for(self, expr: str = None,
                start=0.0, limit=10.0, step=1.0,
                condition=None, operator=None,
                en=None, chain_len: int = 0,
                alias: Optional[str] = None) -> 'BlockFor':
        """
        Add a For-loop block.

        Accepts either a C-style expression string
        (``"i=0; i<10; i+=1"``) or individual parameters.

        :param expr:       C-style loop expression (overrides start/limit/step/condition/operator)
        :param start:      initial value (``float``, ``str``, or ``Ref``)
        :param limit:      loop end limit
        :param step:       iterator step
        :param condition:  ``ForCondition`` enum, string (``"<"``, ``">"`` …), or ``None``
        :param operator:   ``ForOperator`` enum, string (``"+"``, ``"-"`` …), or ``None``
        :param en:         enable
        :param chain_len:  filled automatically by topological sort
        :param alias:      block alias
        """
        from BlockFor import BlockFor, _resolve_condition, _resolve_operator
        en = self._resolve(en)
        if expr is not None:
            start, condition, limit, operator, step = self._parse_for_expr(expr)
        start = self._resolve(start)
        limit = self._resolve(limit)
        step  = self._resolve(step)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockFor(idx=idx, ctx_id=CTX_BLOCKS,
                         chain_len=chain_len,
                         start=start, limit=limit, step=step,
                         condition=_resolve_condition(condition),
                         operator=_resolve_operator(operator),
                         en=en, alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── Clock ──────────────────────────────────────────────────────────
    def add_clock(self, period_ms=1000, width_ms=500, en=None,
                  alias: Optional[str] = None) -> 'BlockClock':
        """
        Add a Clock (square wave) block.

        :param period_ms:  period in ms (``int`` or ``Ref``/``str``)
        :param width_ms:   pulse width in ms (``int`` or ``Ref``/``str``)
        :param en:         enable
        :param alias:      block alias
        """
        from BlockClock import BlockClock
        en        = self._resolve(en)
        period_ms = self._resolve(period_ms)
        width_ms  = self._resolve(width_ms)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockClock(idx=idx, ctx_id=CTX_BLOCKS,
                           en=en, period_ms=period_ms, width_ms=width_ms,
                           alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── Timer ──────────────────────────────────────────────────────────
    def add_timer(self, timer_type=None, pt=1000, en=None, rst=None,
                  alias: Optional[str] = None) -> 'BlockTimer':
        """
        Add a Timer block (TON / TOF / TP).

        :param timer_type:  ``TimerType`` enum or ``None`` (defaults TON)
        :param pt:          preset time in ms (``int``, ``str``, or ``Ref``)
        :param en:          enable
        :param rst:         reset
        :param alias:       block alias
        """
        from BlockTimer import BlockTimer, TimerType
        if timer_type is None:
            timer_type = TimerType.TON
        en  = self._resolve(en)
        rst = self._resolve(rst)
        pt  = self._resolve(pt)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockTimer(idx=idx, ctx_id=CTX_BLOCKS,
                           timer_type=timer_type, pt=pt,
                           en=en, rst=rst,
                           alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── Counter ────────────────────────────────────────────────────────
    def add_counter(self, cu=None, cd=None, reset=None,
                    step=1.0, limit_max=100.0, limit_min=0.0,
                    start_val=0.0, mode=None,
                    alias: Optional[str] = None) -> 'BlockCounter':
        """
        Add an Up/Down Counter block.

        :param cu:         count-up trigger (``str``, ``Ref``, or ``None``)
        :param cd:         count-down trigger
        :param reset:      reset trigger
        :param step:       step value (``float``, ``str``, or ``Ref``)
        :param limit_max:  upper limit
        :param limit_min:  lower limit
        :param start_val:  initial counter value
        :param mode:       ``CounterMode`` enum or ``None`` (defaults ON_RISING)
        :param alias:      block alias
        """
        from BlockCounter import BlockCounter, CounterMode
        if mode is None:
            mode = CounterMode.ON_RISING
        cu        = self._resolve(cu)
        cd        = self._resolve(cd)
        reset     = self._resolve(reset)
        step      = self._resolve(step)
        limit_max = self._resolve(limit_max)
        limit_min = self._resolve(limit_min)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockCounter(idx=idx, ctx_id=CTX_BLOCKS,
                             cu=cu, cd=cd, reset=reset,
                             step=step, limit_max=limit_max,
                             limit_min=limit_min,
                             start_val=start_val, mode=mode,
                             alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── In Selector ────────────────────────────────────────────────────
    def add_in_selector(self, selector, options, en=None,
                        alias: Optional[str] = None) -> 'BlockInSelector':
        """
        Add an Input Selector (multiplexer) block.

        :param selector:  selector index (``str`` or ``Ref``)
        :param options:   list of input references (``str`` or ``Ref``)
        :param en:        enable
        :param alias:     block alias
        """
        from BlockInSelector import BlockInSelector
        selector = self._resolve(selector)
        en       = self._resolve(en)
        options  = self._resolve_list(options)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockInSelector(idx=idx, ctx_id=CTX_BLOCKS,
                                selector=selector, options=options,
                                en=en, alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── Q Selector ─────────────────────────────────────────────────────
    def add_q_selector(self, selector, output_count=1, en=None,
                       alias: Optional[str] = None) -> 'BlockQSelector':
        """
        Add an Output Selector (demultiplexer) block.

        :param selector:      selector index (``str`` or ``Ref``)
        :param output_count:  number of boolean outputs
        :param en:            enable
        :param alias:         block alias
        """
        from BlockQSelector import BlockQSelector
        selector = self._resolve(selector)
        en       = self._resolve(en)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockQSelector(idx=idx, ctx_id=CTX_BLOCKS,
                               selector=selector,
                               output_count=output_count,
                               en=en, alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ── Latch ──────────────────────────────────────────────────────────
    def add_latch(self, set=None, reset=None, en=None,
                  latch_type=None,
                  alias: Optional[str] = None) -> 'BlockLatch':
        """
        Add an SR / RS Latch block.

        :param set:         set input (``str`` or ``Ref``)
        :param reset:       reset input (``str`` or ``Ref``)
        :param en:          enable
        :param latch_type:  ``BlockLatchCfg`` enum or ``None`` (defaults SR)
        :param alias:       block alias
        """
        from BlockLatch import BlockLatch, BlockLatchCfg
        if latch_type is None:
            latch_type = BlockLatchCfg.LATCH_SR
        set_ref   = self._resolve(set)
        reset_ref = self._resolve(reset)
        en        = self._resolve(en)
        idx = self._idx()
        block_alias = alias or str(idx)
        block = BlockLatch(idx=idx, ctx_id=CTX_BLOCKS,
                           set=set_ref, reset=reset_ref, en=en,
                           latch_type=latch_type,
                           alias=block_alias, mem=self.mem)
        return self._register_block(block, alias=alias)

    # ====================================================================
    # Block access
    # ====================================================================

    def get_block(self, idx: int) -> Optional[Block]:
        """Get a block by its current ``cfg.block_idx``."""
        return self.blocks.get(idx)

    def get_blocks_sorted(self) -> List[Block]:
        """Return blocks ordered by current ``cfg.block_idx``."""
        return [self.blocks[i] for i in sorted(self.blocks.keys())]

    @property
    def block_count(self) -> int:
        return len(self.blocks)

    # ====================================================================
    # Subscriptions
    # ====================================================================

    def subscribe(self, *targets: Union[str, Ref, Block]) -> 'SubscriptionBuilder':
        """
        Create a SubscriptionBuilder and subscribe to the given targets.

        Each target can be:
          - a string alias       (``"temperature"``, ``"gains"``)
          - a Ref object         (``ton.out[0]``)
          - a Block object       (subscribes to ALL outputs)
          - a block alias string (``"ton"`` → all outputs of that block)
          - ``"ton[0]"``         (single block output)

        Example::

            sub = code.subscribe("temperature", ton.out[0], clk)
            code.generate("dump.txt", subscriptions=sub)
        """
        from Subscribe import SubscriptionBuilder
        builder = SubscriptionBuilder(self)
        for t in targets:
            if isinstance(t, Block):
                # Block object → subscribe to all outputs
                for q_idx in range(len(t.q_conn)):
                    builder.add(t.out[q_idx])
            elif isinstance(t, str) and t in self._block_aliases:
                # Block alias → subscribe to all outputs
                block = self._block_aliases[t]
                for q_idx in range(len(block.q_conn)):
                    builder.add(block.out[q_idx])
            else:
                ref = self._resolve(t)
                builder.add(ref)
        return builder

    # ====================================================================
    # Sorting pipeline
    # ====================================================================

    def _sort(self):
        """Topological sort + reindex + auto ``chain_len``.  Mutates in-place."""
        from algorithm import sort_and_reindex
        sorted_blocks = sort_and_reindex(self.blocks)
        self.blocks = {b.cfg.block_idx: b for b in sorted_blocks}

    # ====================================================================
    # Packet generation
    # ====================================================================

    def generate_code_cfg_packet(self) -> bytes:
        """``[PACKET_H_CODE_CFG, block_count:u16]``"""
        return struct.pack('<BH',
                           packet_header_t.PACKET_H_CODE_CFG,
                           self.block_count)

    @staticmethod
    def _order_packet(order: 'emu_order_t') -> bytes:
        """Pack a 2-byte little-endian order packet."""
        return struct.pack('<H', order.value)

    def generate_packets(self,
                         subscriptions=None,
                         loop_init: bool = True,
                         loop_start: bool = True) -> List[bytes]:
        """
        Generate all packets in the correct order.

        Data packets are auto-routed by the firmware via their header byte.
        Only ``ORD_EMU_LOOP_INIT`` and ``ORD_EMU_LOOP_START`` are appended
        as 2-byte order packets at the end.
        """
        from Enums import emu_order_t
        packets: List[bytes] = []
        _ord = self._order_packet
        blocks_sorted = self.get_blocks_sorted()

        # ── Memory ──
        packets.extend(self.mem.generate_cfg_packets())
        packets.extend(self.mem.generate_instance_packets())
        packets.extend(self.mem.generate_scalar_data_packets())
        packets.extend(self.mem.generate_array_data_packets())

        # ── Code config ──
        packets.append(self.generate_code_cfg_packet())

        # ── Block headers ──
        for block in blocks_sorted:
            packets.append(block.pack_cfg())

        # ── Block connections (inputs + outputs) ──
        for block in blocks_sorted:
            packets.extend(block.pack_connections())

        # ── Block data ──
        for block in blocks_sorted:
            if hasattr(block, 'pack_data'):
                data_pkts = block.pack_data()
                if data_pkts:
                    packets.extend(data_pkts)

        # ── Subscriptions ──
        if subscriptions is not None:
            init_pkt, add_pkts = subscriptions.build()
            packets.append(init_pkt)
            packets.extend(add_pkts)

        # ── Loop control (only real orders) ──
        if loop_init:
            packets.append(_ord(emu_order_t.ORD_EMU_LOOP_INIT))
        if loop_start:
            packets.append(_ord(emu_order_t.ORD_EMU_LOOP_START))

        return packets

    # ====================================================================
    # Generate to file
    # ====================================================================

    def generate(self,
                 filename: str = "test_dump.txt",
                 sort: bool = True,
                 raw: bool = False,
                 verbose: bool = True,
                 subscriptions=None,
                 loop_init: bool = True,
                 loop_start: bool = True):
        """
        Sort blocks → reindex → write hex dump to *filename*.

        :param filename:       output file path
        :param sort:           run topological sort + reindex (default ``True``)
        :param raw:            if ``True``, write one hex line per packet (no comments)
        :param verbose:        print summary to stdout
        :param subscriptions:  SubscriptionBuilder from ``code.subscribe(...)``
        :param loop_init:      append ``ORD_EMU_LOOP_INIT`` order (default ``True``)
        :param loop_start:     append ``ORD_EMU_LOOP_START`` order (default ``True``)
        """
        if sort:
            self._sort()

        packets = self.generate_packets(subscriptions=subscriptions,
                                        loop_init=loop_init,
                                        loop_start=loop_start)

        with open(filename, "w") as f:
            for pkt in packets:
                f.write(pkt.hex().upper() + "\n")

        if verbose:
            print(f"Generated {len(packets)} packets "
                  f"({self.block_count} blocks) → {filename}")

    def generate_raw_string(self, sort: bool = True) -> str:
        """Return raw hex dump as a single string."""
        if sort:
            self._sort()
        packets = self.generate_packets()
        return "\n".join(pkt.hex().upper() for pkt in packets)

    # ====================================================================
    # Debug helpers
    # ====================================================================

    def print_blocks(self):
        """Print a summary of all registered blocks."""
        from Enums import block_types_t
        print(f"\n{'='*60}")
        print(f"Code: {self.block_count} blocks")
        print('='*60)
        for block in self.get_blocks_sorted():
            btype = block_types_t(block.cfg.block_type).name
            alias_str = block.alias
            deps = [d.alias for d in block.my_dep]
            print(f"  [{block.cfg.block_idx:4d}] {btype:<16s} "
                  f"alias={alias_str!r:<12s}  "
                  f"in={block.cfg.in_cnt} q={block.cfg.q_cnt}  "
                  f"deps={deps}")
        print('='*60)
