"""
Local HTTP Server for runIT Web UI.
Serves static assets and provides REST endpoints for VM program compilation,
object graph manipulation, and BLE hardware upload.
Zero third-party dependencies (uses Python standard library).
"""
import json
import os
import sys
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

# Ensure PythonRunIT root is on sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
PARENT_DIR = SCRIPT_DIR.parent
if str(PARENT_DIR) not in sys.path:
    sys.path.insert(0, str(PARENT_DIR))

try:
    from vm import (
        VMProgram,
        VMType,
        VMFlags,
        VMIndexKind,
        VMExecCommand,
        encode_vm_reset,
        encode_vm_exec,
    )
except ImportError as e:
    print(f"[WARN] Could not import VM modules directly: {e}")

PORT = 8088
WEB_DIR = SCRIPT_DIR


class VMWebHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB_DIR), **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/status":
            self._json_response({"status": "ok", "app": "runIT Web VM Studio", "port": PORT})
        else:
            super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        content_len = int(self.headers.get("Content-Length", 0))
        post_body = self.rfile.read(content_len) if content_len > 0 else b"{}"

        try:
            payload = json.loads(post_body.decode("utf-8")) if post_body else {}
        except Exception:
            payload = {}

        if parsed.path == "/api/compile":
            self._handle_compile(payload)
        else:
            self._json_response({"error": "Endpoint not found"}, status=404)

    def _handle_compile(self, data):
        """Compiles a JSON program declaration into wire packets."""
        try:
            prog_name = data.get("name", "web_program")
            prog = VMProgram(prog_name)
            objects_def = data.get("objects", [])

            # Map temporary client IDs to VMObject instances
            id_map = {}
            for item in objects_def:
                cid = item.get("id")
                t_str = item.get("type", "F").upper()
                vtype = getattr(VMType, t_str, VMType.F)
                count = int(item.get("count", 1))
                init_val = item.get("initial")
                name = item.get("name") or None
                mutable = bool(item.get("mutable", True))
                if vtype == VMType.PTR:
                    init_val = None
                elif isinstance(init_val, str) and vtype != VMType.STR and init_val.strip():
                    cleaned = init_val.replace("[", " ").replace("]", " ")
                    raw_items = [s.strip() for s in cleaned.split(",") if s.strip()]
                    if not raw_items and cleaned.strip():
                        raw_items = cleaned.split()
                    if raw_items:
                        try:
                            if vtype == VMType.F:
                                init_val = [float(x) for x in raw_items]
                            elif vtype in (VMType.U32, VMType.I32, VMType.U8):
                                init_val = [int(float(x)) for x in raw_items]
                            elif vtype == VMType.B:
                                init_val = [x.lower() in ("true", "1", "t") for x in raw_items]
                        except Exception:
                            pass

                if name:
                    obj = prog.add_var(name, type=vtype, count=count, initial=init_val, mutable=mutable)
                else:
                    obj = prog.add_temp(type=vtype, count=count, initial=init_val, mutable=mutable)

                id_map[cid] = obj

                if item.get("subscribe"):
                    prog.subscribe(obj)

            # Resolve tree child relationships (parent_id)
            for item in objects_def:
                cid = item.get("id")
                parent_id = item.get("parent_id")
                if parent_id is not None and parent_id in id_map and cid in id_map:
                    parent_obj = id_map[parent_id]
                    child_obj = id_map[cid]
                    if parent_obj.type == VMType.PTR and child_obj not in parent_obj.children:
                        parent_obj.children.append(child_obj)
                        parent_obj.count = max(1, len(parent_obj.children))

            # Resolve pointer cross-reference links (link_to_id)
            for item in objects_def:
                cid = item.get("id")
                link_target_id = item.get("link_to_id")
                if link_target_id is not None and link_target_id in id_map and cid in id_map:
                    ptr_obj = id_map[cid]
                    target_obj = id_map[link_target_id]
                    if ptr_obj.type == VMType.PTR and target_obj not in ptr_obj.children:
                        ptr_obj.children.append(target_obj)
                        ptr_obj.count = max(1, len(ptr_obj.children))

            compiled = prog.compile(arena_size=int(data.get("arena_size", 2048)))

            res = {
                "success": True,
                "packet_count": len(compiled.packets),
                "total_arena_size": compiled.total_arena_size,
                "dump": compiled.dump_hex(),
                "packets": [
                    {
                        "idx": i + 1,
                        "opcode": p.data[1] if len(p.data) > 1 else 0,
                        "name": p.name,
                        "size": len(p.data),
                        "hex": " ".join(f"{b:02X}" for b in p.data),
                        "bytes": list(p.data),
                    }
                    for i, p in enumerate(compiled.packets)
                ],
            }
            self._json_response(res)
        except Exception as err:
            self._json_response({"success": False, "error": str(err)}, status=400)

    def _json_response(self, data, status=200):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)


def run_server():
    server_address = ("", PORT)
    httpd = ThreadingHTTPServer(server_address, VMWebHandler)
    url = f"http://localhost:{PORT}"
    print(f"=====================================================")
    print(f"  runIT Web Studio running at: {url}")
    print(f"  Serving directory: {WEB_DIR}")
    print(f"=====================================================")
    try:
        webbrowser.open(url)
    except Exception:
        pass
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server.")
        httpd.server_close()


if __name__ == "__main__":
    run_server()
