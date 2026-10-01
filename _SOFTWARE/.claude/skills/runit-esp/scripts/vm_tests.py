"""VM black-box tests over the UART / USB command link (class 0x04).

    python vm_tests.py COM4

Covers: the fail-closed loader (any load failure discards the program), the incomplete-upload check
at start, a failed open freeing the old pool, pause / resume not reporting PERIODIC overruns, the
slow-motion packet (0x49), and that suspend_all / resume_all leave the logs quiet.
Exit code 1 when a check fails. Needs the board's firmware with the VM; resets the board first.
"""
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else __file__.rsplit("/", 1)[0])
import runit_link as rl  # noqa: E402

CLS_VM = 4
OBJ_B = struct.pack("<H", 0) + bytes([1, 0, 0x06, 0x01])  # id 0, 1 byte, type B, mutable
PERIODIC_500MS = (struct.pack("<HHBBBBBBHH", 0, 0, 13, 0, 0, 0, 0, 1, 16, 0)  # block 0, type 13, no pins, on_error continue
                  + struct.pack("<IBBHQ", 500, 0, 0, 0, 0))                   # period 500, ms, flags 0, pad, next_ms 0
OPEN_1_0_1 = struct.pack("<HHHI", 1, 0, 1, 512)  # 1 object, 0 accessors, 1 block, 512 bytes

failed = []


def check(name, cond, detail=""):
    print(f"  {'ok  ' if cond else 'FAIL'} {name}{(' -- ' + detail) if detail and not cond else ''}")
    if not cond:
        failed.append(name)


def pkt(link, hdr, body=b"", wait=0.4):
    mark = link.mark()
    r = link.request(CLS_VM, hdr, body, timeout=2.0)
    time.sleep(wait)
    logs = [l for l in link.logs_since(mark) if "sys_error_log" in l]
    return r, logs


def has_tag(logs, tag):
    return any(f"tag={tag} " in l for l in logs)


def err_tag(r):
    return r.err[0] if r is not None and r.err else None


def load_program(link):
    """open + one B object + one PERIODIC 500 ms block; False if any step failed."""
    for hdr, body in ((0x41, OPEN_1_0_1), (0x42, bytes([1]) + OBJ_B), (0x45, PERIODIC_500MS)):
        r, _ = pkt(link, hdr, body, 0.1)
        if r is None or not r.ok:
            return False
    return True


def main(port):
    with rl.open_link(port, reset=True, echo_logs=False) as link:
        print("fail-closed loader")
        pkt(link, 0x41, OPEN_1_0_1)
        pkt(link, 0x42, bytes([1]) + OBJ_B)
        r, logs = pkt(link, 0x48, bytes([5]))  # start with the block missing
        check("start with a missing block fails INCOMPLETE", err_tag(r) == "ERR_VM_LOAD_INCOMPLETE", str(r))
        check("... and discards the program (ABORTED)", has_tag(logs, "ERR_VM_LOAD_ABORTED"))
        r, _ = pkt(link, 0x45, PERIODIC_500MS)
        check("a packet after the abort is refused (BAD_STATE)", err_tag(r) == "ERR_VM_LOAD_BAD_STATE", str(r))

        pkt(link, 0x41, OPEN_1_0_1)
        r, logs = pkt(link, 0x42, bytes([1, 0, 0, 1]))  # truncated record
        check("a truncated record fails SHORT_RECORD", err_tag(r) == "ERR_VM_LOAD_SHORT_RECORD", str(r))
        check("... and discards the program (ABORTED)", has_tag(logs, "ERR_VM_LOAD_ABORTED"))
        r, _ = pkt(link, 0x42, bytes([1]) + OBJ_B)
        check("the next packet is refused (BAD_STATE)", err_tag(r) == "ERR_VM_LOAD_BAD_STATE", str(r))

        pkt(link, 0x41, OPEN_1_0_1)
        pkt(link, 0x42, bytes([1]) + OBJ_B)
        r, logs = pkt(link, 0x41, struct.pack("<HHHI", 1, 0, 1, 10_000_000))
        check("a too big open fails TOO_BIG", err_tag(r) == "ERR_VM_LOAD_TOO_BIG", str(r))
        check("... and discards the loaded program too (ABORTED)", has_tag(logs, "ERR_VM_LOAD_ABORTED"))
        r, _ = pkt(link, 0x42, bytes([1]) + OBJ_B)
        check("the old program is gone (BAD_STATE)", err_tag(r) == "ERR_VM_LOAD_BAD_STATE", str(r))

        r1, _ = pkt(link, 0x41, struct.pack("<HHHI", 1, 0, 1, 100000))
        r2, _ = pkt(link, 0x41, struct.pack("<HHHI", 1, 0, 1, 100000))
        check("two big opens in a row (the old pool is freed first)", r1 and r1.ok and r2 and r2.ok, f"{r1} / {r2}")
        pkt(link, 0x48, bytes([8]))

        print("running program")
        check("a good program uploads", load_program(link))
        r, _ = pkt(link, 0x48, bytes([5]))
        check("start", r is not None and r.ok, str(r))
        time.sleep(1.0)
        r, logs = pkt(link, 0x42, bytes([1]) + OBJ_B)
        check("a load packet while running is refused (LOAD_RUNNING)", err_tag(r) == "ERR_VM_LOAD_RUNNING", str(r))
        check("... without discarding the running program", not has_tag(logs, "ERR_VM_LOAD_ABORTED"))

        print("pause / resume (G-10)")
        pkt(link, 0x48, bytes([6]))
        time.sleep(3.0)
        mark = link.mark()
        pkt(link, 0x48, bytes([7]), 3.0)
        check("no PERIODIC overrun after resume", not [l for l in link.logs_since(mark) if "PERIODIC_OVERRUN" in l])
        pkt(link, 0x48, bytes([8]))

        print("slow motion (0x49)")
        counts = {}
        for k in (1, 10):
            r, _ = pkt(link, 0x49, struct.pack("<H", k))
            check(f"factor {k} accepted", r is not None and r.ok, str(r))
            load_program(link)
            pkt(link, 0x47, bytes([1]) + struct.pack("<H", 0))  # telemetry on the ENO object
            pkt(link, 0x48, bytes([5]))
            mark = link.mark()
            time.sleep(11.0)
            counts[k] = len(link.telemetry_since(mark))
            pkt(link, 0x48, bytes([8]))
        print(f"  telemetry frames in 11 s: x1 = {counts[1]}, x10 = {counts[10]}")
        check("x10 ticks about 10 times less often", counts[10] > 0 and 6 <= counts[1] / counts[10] <= 16, f"{counts}")
        for bad in (0, 1001):
            r, _ = pkt(link, 0x49, struct.pack("<H", bad))
            check(f"factor {bad} refused", err_tag(r) == "ERR_INVALID_VAL_UI32", str(r))
        pkt(link, 0x49, struct.pack("<H", 1))

        print("suspend_all / resume_all")
        mark = link.mark()
        r1 = link.request(1, 0x14, b"", timeout=2.0)  # sys_device suspend_all
        time.sleep(1.5)
        r2 = link.request(1, 0x15, b"", timeout=2.0)  # sys_device resume_all
        time.sleep(1.5)
        quiet = [l for l in link.logs_since(mark) if "sys_error_log" in l and "] owner=" in l]
        check("suspend_all / resume_all answer OK", r1 and r1.ok and r2 and r2.ok, f"{r1} / {r2}")
        check("... and raise no error lines", not quiet, "\n".join(quiet[:4]))

    print("FAILED: " + ", ".join(failed) if failed else "all passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "COM4"))
