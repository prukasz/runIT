"""Resolve the packet class byte of a decoder header (shared by the generators).

Class bytes are configured in Kconfig (RX_/TX_PACKET_CLASS_<NAME>); a few may
also be defined as <PREFIX>_CLASS_HEADER in a component header. A decoder file
resolves its class directly from such a define, or by matching its own
<PREFIX>_PACKET_LIST(X) macro name against the known class names (exact, or the
longest class name that prefixes it: "SYS_CONTRACTS_INSTALL" -> "SYS_CONTRACTS").
"""
import re
from pathlib import Path
from typing import Dict, Optional, Tuple

PROJECT_ROOT = Path(__file__).parents[2]  # _SOFTWARE folder
COMPONENTS_DIR = PROJECT_ROOT / "components"

CLASS_HEADER_RE = re.compile(r"#define\s+(\w+)_CLASS_HEADER\s+(0x[0-9A-Fa-f]+)")
KCONFIG_CONFIG_RE = re.compile(r"^\s*config\s+(\w+)\s*$")
KCONFIG_DEFAULT_RE = re.compile(r"^\s*default\s+(0x[0-9A-Fa-f]+)")
KCONFIG_CLASS_NAME_RE = re.compile(r"^(?:RX|TX)_PACKET_CLASS_(\w+)$")
PACKET_LIST_DEFINE_RE = re.compile(r"#define\s+(\w+)_PACKET_LIST\(X\)")


def scan_all_class_headers() -> Dict[str, str]:
    """<PREFIX>_CLASS_HEADER defines from every header under components/."""
    found: Dict[str, str] = {}
    for h in COMPONENTS_DIR.rglob("*.h"):
        text = h.read_text(encoding="utf-8", errors="ignore")
        for name, header in CLASS_HEADER_RE.findall(text):
            found[name] = header
    return found


def scan_kconfig_class_headers() -> Dict[str, str]:
    """RX_/TX_PACKET_CLASS_<NAME> defaults from every Kconfig under components/."""
    found: Dict[str, str] = {}
    for kconfig in COMPONENTS_DIR.rglob("Kconfig*"):
        pending_name = None
        for line in kconfig.read_text(encoding="utf-8", errors="ignore").splitlines():
            config = KCONFIG_CONFIG_RE.match(line)
            if config:
                pending_name = config.group(1)
                continue
            if pending_name:
                default = KCONFIG_DEFAULT_RE.match(line)
                if default:
                    class_name = KCONFIG_CLASS_NAME_RE.match(pending_name)
                    if class_name:
                        found[class_name.group(1)] = default.group(1)
                    pending_name = None
    return found


def resolve_class(text: str, all_classes: Dict[str, str]) -> Optional[Tuple[str, str]]:
    """(class_name, class_header) of a decoder file, direct or via its <PREFIX>_PACKET_LIST(X) name."""
    direct = CLASS_HEADER_RE.search(text)
    if direct:
        return direct.group(1), direct.group(2)

    list_match = PACKET_LIST_DEFINE_RE.search(text)
    if not list_match:
        return None
    prefix = list_match.group(1)
    if prefix in all_classes:
        return prefix, all_classes[prefix]

    best_name = None
    for class_name in all_classes:
        if prefix.startswith(class_name) and (best_name is None or len(class_name) > len(best_name)):
            best_name = class_name
    return (best_name, all_classes[best_name]) if best_name else None
