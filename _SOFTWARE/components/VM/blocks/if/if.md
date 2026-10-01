# If

A two-way router for the flow of the program. While it is enabled it reads **Condition** and drives one of its two outputs:

- **Yes** when the condition is non-zero,
- **No** when it is zero.

The outputs are not data: they are flow. Wire them to the enables of the blocks that should run on that branch; the branch that is not taken is switched off.

## Pins

| Pin | Meaning |
|---|---|
| Condition (required) | Any number or boolean. Non-zero means yes. |
| Yes, No | Flow outputs. Exactly one is active while the block decides. |
| ENO | Active while the block took a branch. |

## Behaviour

- When the block is disabled, or the condition cannot be read, **neither** branch runs and ENO is off.
- Both outputs are cleared every pass they are not taken, so a block below never keeps running by accident.
- The block has no state.
