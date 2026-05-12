"""
algorithm.py - Topological sorting for emulator blocks.

Given a dict of blocks (keyed by their cfg.block_idx), produces a new
ordering [0 .. N-1] that respects data dependencies and gives SET blocks
priority placement right after the block whose output they consume.

Rules
-----
1. A block with **no block-input dependencies** (only user-variable or
   constant inputs) is a *root* and can be placed first.
2. After placing block *B*, any **SET blocks** that read an output of *B*
   are placed immediately after *B* (they are "sinks" — they write back
   to user variables and don't produce outputs consumed by other blocks).
3. After SET blocks, blocks that depend on *B*'s outputs (and whose
   **all** other dependencies have already been placed) are candidates.
   Among candidates we recurse depth-first so chains stay together.
4. **FOR blocks**: the ``chain_len`` child blocks that follow a FOR in
   execution are the blocks whose ``en`` (in_conn[0]) is wired to the
   FOR's first output (detected via inp.instance.my_block).  They (and
   their own SET sinks) are placed immediately after the FOR, before any
   unrelated blocks.  Blocks that *transitively* depend on those chain
   children are also placed inside the FOR body.  After sorting,
   ``chain_len`` is **automatically recomputed** to equal the total number
   of blocks placed inside the loop body.
5. If recursion exhausts but blocks remain, we pick the next candidate
   with the fewest unplaced dependencies and continue.

Dependency information is read directly from ``block.my_dep``
(List[Block]) which is populated automatically by ``Block.add_inputs``
via ``inp.instance.my_block``.  No alias parsing is required.

Public API
----------
    sorted_blocks = topological_sort(blocks_dict)
    # blocks_dict: {original_cfg_block_idx: Block, ...}
    # returns: list[Block] in new execution order

    reindex(sorted_blocks)
    # Mutates block.cfg.block_idx in-place to [0 .. N-1]

    sorted_blocks = sort_and_reindex(blocks_dict)
    # convenience: sort + reindex in one call
"""

from typing import Any, Dict, List, Set
Block = Any


# ============================================================================
# 1.  Helpers
# ============================================================================

def _idx(block) -> int:
    """Return the block's current index."""
    return block.cfg.block_idx


def _is_set_block(block) -> bool:
    """Check if block is a SET block."""
    from Enums import block_types_t
    return block.cfg.block_type == block_types_t.BLOCK_SET.value


def _is_for_block(block) -> bool:
    """Check if block is a FOR block."""
    from Enums import block_types_t
    return block.cfg.block_type == block_types_t.BLOCK_FOR.value


# ============================================================================
# 2.  Dependency extraction  (uses block.my_dep — no alias parsing)
# ============================================================================

def _get_dep_indices(block, blocks: Dict[int, 'Block']) -> Set[int]:
    """
    Return the set of block indices that *block* directly depends on.
    Reads block.my_dep (populated by Block.add_inputs via inp.instance.my_block).
    Only includes indices that are present in *blocks* (guards against
    stale refs to removed blocks).
    """
    return {_idx(dep) for dep in block.my_dep if _idx(dep) in blocks}


# ============================================================================
# 3.  FOR chain-children detection
# ============================================================================

def _get_for_chain_children(for_block, blocks: Dict[int, 'Block']) -> List[int]:
    """
    Return indices of blocks whose EN input (in_conn[0]) is wired to
    for_block's first output, i.e. the loop-body entry blocks.
    Detection uses inp.instance.my_block — no alias matching needed.
    """
    # The FOR block's first output instance
    if not for_block._q_instances:
        return []
    for_eno_inst = for_block._q_instances[0]

    children = []
    for block in blocks.values():
        if _idx(block) == _idx(for_block):
            continue
        if block.in_conn and block.in_conn[0] is not None:
            if block.in_conn[0].instance is for_eno_inst:
                children.append(_idx(block))
    return children


# ============================================================================
# 4.  Topological sort
# ============================================================================

def topological_sort(blocks: Dict[int, 'Block']) -> List['Block']:
    """
    Produce a list of blocks in valid execution order.

    After sorting, every FOR block's ``chain_len`` is **auto-updated** to
    match the number of blocks actually placed inside its loop body.

    :param blocks: dict  {original_cfg_block_idx: Block}
    :return: list[Block] in topological (execution) order
    """
    deps: Dict[int, Set[int]] = {
        _idx(b): _get_dep_indices(b, blocks) for b in blocks.values()
    }

    placed_set: Set[int] = set()
    result: List['Block'] = []

    # Key = for_block idx,  Value = list of child indices placed inside body
    for_chain_members: Dict[int, List[int]] = {}
    _for_scope_stack: List[int] = []

    def _is_ready(idx: int) -> bool:
        return deps[idx].issubset(placed_set)

    def _place(idx: int):
        if idx in placed_set:
            return
        placed_set.add(idx)
        result.append(blocks[idx])
        if _for_scope_stack:
            for_chain_members[_for_scope_stack[-1]].append(idx)

    def _place_block_and_followers(idx: int):
        if idx in placed_set:
            return
        _place(idx)
        block = blocks[idx]
        # Consumers come directly from block.have_me — no map lookup needed
        cons = [_idx(c) for c in block.have_me if _idx(c) in blocks]

        # Partition: SET sinks vs. regular dependents
        set_sinks = [c for c in cons
                     if _is_set_block(blocks[c]) and c not in placed_set and _is_ready(c)]
        regular   = [c for c in cons
                     if not _is_set_block(blocks[c]) and c not in placed_set]

        # 1) Place SET sinks immediately (they produce no outputs)
        for s in set_sinks:
            _place(s)

        # 2) FOR block: open scope, place entire loop body inside it
        if _is_for_block(block):
            for_chain_members[idx] = []
            _for_scope_stack.append(idx)

            # 2a) Direct EN-children (in_conn[0].instance is for.q_instances[0])
            for child_idx in _get_for_chain_children(block, blocks):
                if child_idx not in placed_set and _is_ready(child_idx):
                    _place_block_and_followers(child_idx)

            # 2b) Other consumers of FOR outputs (e.g. iterator out[1])
            for c in regular:
                if c not in placed_set and _is_ready(c):
                    _place_block_and_followers(c)

            _for_scope_stack.pop()
        else:
            # 3) Depth-first into ready regular consumers
            for c in regular:
                if c not in placed_set and _is_ready(c):
                    _place_block_and_followers(c)

    # --- Main loop: pick roots until all placed ---
    while len(placed_set) < len(blocks):
        candidates = [_idx(b) for b in blocks.values() if _idx(b) not in placed_set]
        if not candidates:
            break

        # Prefer blocks with fewest unplaced dependencies (roots = 0)
        candidates.sort(key=lambda i: len(deps[i] - placed_set))

        best = candidates[0]
        # Place any outstanding deps first (handles circular / missing blocks)
        for d in sorted(deps[best] - placed_set):
            if d in blocks and d not in placed_set:
                _place_block_and_followers(d)

        _place_block_and_followers(best)

    # --- Auto-update chain_len for every FOR block ---
    for for_idx, members in for_chain_members.items():
        blocks[for_idx].chain_len = len(members)

    return result


# ============================================================================
# 5.  Re-index
# ============================================================================

def reindex(sorted_blocks: List['Block']):
    """
    Mutate ``block.cfg.block_idx`` in-place so the sorted list has indices
    ``0, 1, 2, …, N-1``.

    NOTE: This is a destructive operation — use *after* sorting and
    *before* packing packets.
    """
    for new_idx, block in enumerate(sorted_blocks):
        block.cfg.block_idx = new_idx


# ============================================================================
# 6.  Convenience: sort + reindex in one call
# ============================================================================

def sort_and_reindex(blocks: Dict[int, 'Block']) -> List['Block']:
    """
    Topologically sort blocks and reassign cfg.block_idx to 0..N-1.

    :param blocks: dict {original_cfg_block_idx: Block}
    :return: list[Block] with updated cfg.block_idx values
    """
    sorted_blocks = topological_sort(blocks)
    reindex(sorted_blocks)
    return sorted_blocks
