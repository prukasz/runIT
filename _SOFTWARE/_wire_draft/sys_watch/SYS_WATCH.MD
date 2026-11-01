# sys_watch — the client and the cached reads (draft, step 4)

What `components/sys/sys_watch/SYS_WATCH.MD` gains in step 4. The watches this board runs for their owners are built there; this is the other side: the watches this board added on other boards. Not built.

The component has two sides: the **watches** this board runs for their owners (built), and the **client** of the watches this board added on other boards, which hands each entry that comes back to the one who asked for it, and answers this board's reads of other boards from them (Cached reads).

## Client

What this board asks of another board's watches goes through the client, so no module handles the WATCHES stream itself:

- `sys_watch_remote_add(board, head, read, cb, ctx, &id)`: the client gives the watch an id of its own (1..255, free on that board for this board), sends ADD as a call (SYS_SESSION.MD "The board's own calls") and keeps `(board, id) → cb`. `head.id` is set by the client.
- Each STREAM frame `[WATCHES][entry]` that comes to this board (the session's stream listener, SYS_SESSION.MD "Streams") goes, by its `src` and the entry's `id`, to that `cb` with the entry's status and data, and the gap in the watch's `seq` since the entry before. An entry no one asked for is dropped.
- The ADD answered ERROR, or the board gone (`sys_session_on_gone`): `cb` is told (status ERROR with the error), the watch is kept, and the client sends its ADD again when that board's session opens again. `sys_watch_remote_remove(board, id)` sends REMOVE and forgets it.
- `cb` runs on the RX task the entry came on: it copies what it keeps and returns, never blocks.

Who uses it: the cached reads (below); the board config, for a value of another board it sends on.

## Cached reads

`sys_watch_read(board, req, max_age_ms, wait, buf, cap, &len)` runs the read command `req` (any read, `[key][dir READ][...]`: a device's pin level, a VM status) on another board, and the client decides by itself whether it takes a copy, makes a watch, or asks. **A read is known by its board and its command bytes**: the same command is the same read, whoever makes it (a remote device's driver, the VM, the board config). The caller knows nothing of watches; a remote device's read (SYS_REMOTE.MD "A call") is one.

```
read (board, req, max_age):
  1. a watch of req is ACTIVE, its copy is at most max_age old and not marked old    → the copy
  2. else, wait:    ask now (sys_session_call_wait), answer                           → the answer
          no wait:  ERR_WATCH_NO_VALUE (no copy yet), ERR_WATCH_STALE (older)
  3. no watch of req, and (wait) req was read before within 2 × max_age,
                          (no wait) req is read at all                              → make one:
       ON_CHANGE every max_age / 2 (at least a tick), max_silence max_age: from now on 1.
  4. req not read for 10 × max_age (at least CONFIG_SYS_WATCH_IDLE_MIN_MS)           → REMOVE it
```

A watch costs the link one to two entries per `max_age` (a change, else the silence), however often the value is read; a request costs two frames per read. So a watch pays when the reads come about every `max_age` or more often, and the windows above are in `max_age`, not fixed: with `max_age` 20 ms a driver reading every millisecond gets a watch at its second read, a read every 4 s never does (it asks: half a frame a second, not the watch's 50 to 100); with `max_age` 5 s a read every 4 s does. The removal's floor keeps a watch from coming and going with every pause of a fast reader.

- `max_age_ms` 0 never takes a copy and never makes a watch: every read asks.
- One watch per board and command, however many read it: its period follows the shortest `max_age_ms` it was read with.
- The client full (`CONFIG_SYS_WATCH_CLIENT_MAX`), or the watch's ADD refused there (`ERR_WATCH_FULL` of that board): no watch, the reads ask (or, without wait, answer `ERR_WATCH_NO_VALUE`); the ADD is tried again at the next read after `max_age`.
- **A write makes the copies old.** A copy may have been read there before a write to the same thing. `sys_watch_stale(board, req, target_len)` (`req` the write's command), called when a write's answer came (a remote device's write does): every copy of a read on that board with the same key and the same `target_len` bytes after the dir byte (for the device key the device's id: 2) is marked old, and one plain read of each goes at once, its answer the new copy. That answer, and every entry after it, was read after the write (Order). Until it comes, step 1 skips the copy.


## Errors

`ERR_WATCH_NO_VALUE{board}` (no copy yet, no wait), `_STALE{board, age_ms}` (the copy older than `max_age_ms`, no wait): `errors/sys_watch_errors.h` here, to join `ERR_SYS_WATCH_MAP`.

## Limits

`CONFIG_SYS_WATCH_CLIENT_MAX` (24) watches this board added on other boards: the cached reads' and the board config's; `CONFIG_SYS_WATCH_IDLE_MIN_MS` (1000): a cached read's watch is removed after 10 × its `max_age_ms` without a read, never sooner than this (`Kconfig` here).

## Needs elsewhere

- **sys_session** (step 3): `sys_session_call` and `sys_session_call_wait` for the client's ADD and REMOVE, the stream listener for `[WATCHES][entry]`, gone peers and the session opening again (SYS_SESSION.MD).
- **sys_remote**: a remote device's read is a cached read (SYS_REMOTE.MD "A call").
