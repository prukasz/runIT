import re
import os
from pathlib import Path
from typing import List, Tuple, Dict, Optional

# ============================================================================
# CONFIGURATION
# ============================================================================

PROJECT_ROOT = Path(__file__).parent.parent.parent  # _SOFTWARE folder
OUTPUT_FILE = Path(__file__).parent / "ConfigTypes.py"

HEADER_SOURCES = [
    PROJECT_ROOT / "components" / "packet_interface"/ "include"/ "interface_commands.h",
    PROJECT_ROOT / "components" / "configs" / "config_power" / "include" / "config_power.h",
    PROJECT_ROOT / "components" / "configs" / "config_io" / "include" / "config_io.h",
    PROJECT_ROOT / "components" / "configs" / "config_sys" / "include" / "config_sys.h"
]

# Maps C types to ctypes equivalents
C_TYPE_MAP = {
    "uint8_t": "ct.c_uint8",
    "uint16_t": "ct.c_uint16",
    "uint32_t": "ct.c_uint32",
    "uint64_t": "ct.c_uint64",
    "int8_t": "ct.c_int8",
    "int16_t": "ct.c_int16",
    "int32_t": "ct.c_int32",
    "int64_t": "ct.c_int64",
    "bool": "ct.c_bool",
    "float": "ct.c_float",
    "double": "ct.c_double",
}


# ============================================================================
# ENUM PARSING
# ============================================================================

class EnumField:
    def __init__(self, name: str, value: Optional[str] = None):
        self.name = name.strip()
        self.value = value.strip() if value else None

class EnumDefinition:
    def __init__(self, name: str, fields: List[EnumField]):
        self.name = name
        self.fields = fields

    def to_python_class(self) -> str:
        lines = [f"class {self.name}(IntEnum):"]
        
        if not self.fields:
            lines.append("    pass")
            lines.append("")
            return "\n".join(lines)
            
        current_val = 0
        for field in self.fields:
            if field.value is not None:
                lines.append(f"    {field.name} = {field.value}")
                try:
                    current_val = int(field.value, 0)
                except ValueError:
                    pass
            else:
                lines.append(f"    {field.name} = {current_val}")
            current_val += 1
            
        lines.append("")
        return "\n".join(lines)

def extract_enum_definition(text: str, start_pos: int) -> Optional[Tuple[EnumDefinition, int]]:
    match = re.search(r'typedef\s+enum\s*\{', text[start_pos:], re.MULTILINE)
    
    if not match:
        return None
        
    enum_start = start_pos + match.start()
    brace_start = start_pos + match.end() - 1
    
    brace_count = 1
    pos = brace_start + 1
    while pos < len(text) and brace_count > 0:
        if text[pos] == '{':
            brace_count += 1
        elif text[pos] == '}':
            brace_count -= 1
        pos += 1
        
    if brace_count != 0:
        return None
        
    enum_body = text[brace_start + 1:pos - 1]
    
    closing_match = re.search(r'}\s*(\w+)\s*;', text[pos - 1:])
    if not closing_match:
        return None
        
    enum_name = closing_match.group(1)
    end_pos = pos - 1 + closing_match.end()
    
    fields = parse_enum_fields(enum_body)
    
    return EnumDefinition(enum_name, fields), end_pos

def parse_enum_fields(body: str) -> List[EnumField]:
    fields = []
    
    body_no_comments = re.sub(r'//.*?$', '', body, flags=re.MULTILINE)
    body_no_comments = re.sub(r'/\*.*?\*/', '', body_no_comments, flags=re.DOTALL)
    
    field_parts = body_no_comments.split(',')
    
    for part in field_parts:
        part = part.strip()
        if not part:
            continue
            
        if '=' in part:
            name, value = part.split('=', 1)
            fields.append(EnumField(name, value))
        else:
            fields.append(EnumField(part))
            
    return fields


# ============================================================================
# STRUCT PARSING
# ============================================================================

class StructField:
    def __init__(self, c_type: str, name: str, bitwidth: Optional[int] = None):
        self.c_type = c_type.strip()
        self.name = name.strip()
        self.bitwidth = bitwidth

    def to_ctypes(self) -> str:
        ct_type = C_TYPE_MAP.get(self.c_type, f"ct.c_uint32  # Unknown: {self.c_type}")
        
        if self.bitwidth:
            return f'("{self.name}", {ct_type}, {self.bitwidth})'
        else:
            return f'("{self.name}", {ct_type})'

    def __repr__(self):
        return f"StructField({self.name}: {self.c_type}, bitwidth={self.bitwidth})"

class StructDefinition:
    def __init__(self, name: str, fields: List[StructField]):
        self.name = name
        self.fields = fields

    def to_python_class(self) -> str:
        lines = [f"class {self.name}(ct.LittleEndianStructure):"]
        
        lines.append("    _pack_ = 1")
        lines.append("    _fields_ = [")
        
        for field in self.fields:
            lines.append(f"        {field.to_ctypes()},")
        
        lines.append("    ]")
        lines.append("")
        
        return "\n".join(lines)

def extract_struct_definition(text: str, start_pos: int) -> Optional[Tuple[StructDefinition, int]]:
    match = re.search(
        r'typedef\s+struct\s+(?:__attribute__\s*\(\s*\(\s*packed\s*\)\s*\)\s*)?(\w*)\s*\{',
        text[start_pos:],
        re.MULTILINE
    )
    
    if not match:
        return None
    
    brace_start = start_pos + match.end() - 1
    
    brace_count = 1
    pos = brace_start + 1
    while pos < len(text) and brace_count > 0:
        if text[pos] == '{':
            brace_count += 1
        elif text[pos] == '}':
            brace_count -= 1
        pos += 1
    
    if brace_count != 0:
        return None
    
    struct_body = text[brace_start + 1:pos - 1]
    
    closing_match = re.search(r'}\s*(\w+)\s*;', text[pos - 1:])
    if not closing_match:
        return None
    
    struct_name = closing_match.group(1)
    end_pos = pos - 1 + closing_match.end()
    
    fields = parse_struct_fields(struct_body)
    
    return StructDefinition(struct_name, fields), end_pos

def parse_struct_fields(body: str) -> List[StructField]:
    fields = []
    
    body_no_comments = re.sub(r'//.*?$', '', body, flags=re.MULTILINE)
    body_no_comments = re.sub(r'/\*.*?\*/', '', body_no_comments, flags=re.DOTALL)
    
    field_lines = body_no_comments.split(';')
    
    for line in field_lines:
        line = line.strip()
        if not line or line.startswith('struct'):
            continue
        
        match = re.match(r'(\w+(?:\s+\w+)*?)\s+(\w+)(?:\s*:\s*(\d+))?$', line)
        
        if match:
            c_type = match.group(1).strip()
            field_name = match.group(2).strip()
            bitwidth = int(match.group(3)) if match.group(3) else None
            
            fields.append(StructField(c_type, field_name, bitwidth))
    
    return fields

def extract_all_definitions(file_path: Path) -> Tuple[List[StructDefinition], List[EnumDefinition]]:
    structs = []
    enums = []
    
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except Exception as e:
        print(f"Warning: Could not read {file_path}: {e}")
        return structs, enums
    
    # Enums
    enum_pattern = r'typedef\s+enum\s*\{'
    pos = 0
    while pos < len(content):
        match = re.search(enum_pattern, content[pos:])
        if not match:
            break
        result = extract_enum_definition(content, pos + match.start())
        if result:
            enums.append(result[0])
            pos += match.start() + (result[1] - (pos + match.start()))
        else:
            pos += match.start() + 1
            
    # Structs
    struct_pattern = r'typedef\s+struct\s+(?:__attribute__\s*\(\s*\(\s*packed\s*\)\s*\)\s*)?(\w*)\s*\{'
    pos = 0
    while pos < len(content):
        match = re.search(struct_pattern, content[pos:])
        if not match:
            break
        result = extract_struct_definition(content, pos + match.start())
        if result:
            structs.append(result[0])
            pos += match.start() + (result[1] - (pos + match.start()))
        else:
            pos += match.start() + 1
    
    return structs, enums


# ============================================================================
# CODE GENERATION
# ============================================================================

def generate_config_types_file(definitions_by_source: Dict[str, Tuple[List[StructDefinition], List[EnumDefinition]]]) -> str:
    lines = [
        'import ctypes as ct',
        'from enum import IntEnum',
        '',
        '',
    ]
    
    for source, (structs, enums) in definitions_by_source.items():
        if not structs and not enums:
            continue
        
        source_name = Path(source).stem
        header = f"# {source_name.upper()} CONFIGURATION STRUCTURES"
        lines.append("# " + "=" * 76)
        lines.append(header)
        lines.append("# " + "=" * 76)
        lines.append("")
        
        for enum in enums:
            lines.append(enum.to_python_class())
            
        for struct in structs:
            lines.append(struct.to_python_class())
    
    return "\n".join(lines)


# ============================================================================
# MAIN
# ============================================================================

def main():
    print("Syncing C config structures and enums to Python...")
    
    definitions_by_source = {}
    total_structs = 0
    total_enums = 0
    
    for header_file in HEADER_SOURCES:
        if not header_file.exists():
            print(f"Skipping missing file: {header_file}")
            continue
        
        print(f"Parsing {header_file.name}...")
        structs, enums = extract_all_definitions(header_file)
        
        if structs or enums:
            definitions_by_source[str(header_file)] = (structs, enums)
            total_structs += len(structs)
            total_enums += len(enums)
            for enum in enums:
                print(f"   Found Enum {enum.name}")
            for struct in structs:
                print(f"   Found Struct {struct.name}")
    
    if total_structs == 0 and total_enums == 0:
        print("No structs or enums found!")
        return False
    
    content = generate_config_types_file(definitions_by_source)
    
    try:
        OUTPUT_FILE.write_text(content)
        print(f"\nGenerated {OUTPUT_FILE}")
        print(f"   Total enums: {total_enums}")
        print(f"   Total structs: {total_structs}")
        return True
    except Exception as e:
        print(f"Error writing file: {e}")
        return False


if __name__ == "__main__":
    success = main()
    exit(0 if success else 1)
