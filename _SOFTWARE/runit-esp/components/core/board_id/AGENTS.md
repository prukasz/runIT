# board_id

Spec: `BOARD_ID.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- ESP-IDF is named in `esp/` only, behind `internal/board_id_port.h`; `board_id.c` keeps the id through `nvs_store`.
- The id, the boot id and the build are read once, at `board_id_init`; a saved id is the board's from the next start, never under way.
- The id's range is the header's sources (`CMD_SRC_*`, cmd.h): 0 or a board's.
