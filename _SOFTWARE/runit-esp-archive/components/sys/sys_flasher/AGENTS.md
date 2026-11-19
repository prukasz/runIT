# sys_flasher

Spec: `SYS_FLASHER.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `ROUTER_KEY_FLASHER` (0x14), the op numbers, `sys_flasher_status_reply_t`, `sys_flasher_state_e`, `sys_flasher_step_e` and the error tags (`ERR_MOD_SYS_FLASHER` = 25) are wire format: never renumber. A change to them or to `contract_flasher.json` runs `sh templates/sync.sh`.
- `rom/` stays pure (no ESP-IDF, no `err_h`): the host tests build it alone. A change there is followed by its test in `host/`.
- The UART is never touched directly: the link is lent by `sys_uart_link_direct` and read and written through sys_uart's DIRECT calls. It is always taken back, also after a failure.
- The job runs on its own task, below the router's: START only checks and wakes it. Nothing it does writes this board's flash.
