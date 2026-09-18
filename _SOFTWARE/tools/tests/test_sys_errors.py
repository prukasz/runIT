"""Run real error core/dispatch/device policy as freestanding WASM under Node.
Requires Zig (zig cc) and Node; overrides: ZIG, NODE.
An optional local toolchain can be installed with:
  python -m pip install --target build/error_test_compiler ziglang
No firmware flashing or downloaded dependencies.
"""
from pathlib import Path
import os, subprocess, shutil

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build' / 'error_host_tests'
OUT.mkdir(parents=True, exist_ok=True)
STUBS = {
 'stdio.h': '#include <stddef.h>\nint snprintf(char*, size_t, const char*, ...);\n',
 'string.h': '#include <stddef.h>\nvoid *memset(void*, int, size_t);\nvoid *memcpy(void*, const void*, size_t);\n',
 'stdlib.h': '#include <stddef.h>\nvoid *malloc(size_t);\nvoid *calloc(size_t,size_t);\nvoid free(void*);\n',
 'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\nconst char *esp_err_to_name(int);\n',
 'esp_log.h': '#pragma once\ntypedef int esp_log_level_t;\n#define ESP_LOG_INFO 3\n#define ESP_LOG_WARN 2\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOG_LEVEL(...) ((void)0)\n',
 'sdkconfig.h': '#define CONFIG_SYS_DEVICE_MAX_ID 255\n#define CONFIG_SYS_ACTIONS_ID_SPACE 64\n#define CONFIG_SYS_ACTIONS_STATIC_SLOTS 16\n',
 'utils.h': '', 'sys_io.h': '',
 'sys_error_log.h': '#include "sys_error.h"\nvoid se_log_init(void);\nerr_h SE_send(err_h);\n',
 'sys_actions.h': '#include "sys_error.h"\n#define SYS_ACTION_SCOPE_STATIC 0\n#define SYS_ACTION_SCOPE_DYNAMIC 1\nerr_h sys_actions_invoke(unsigned,unsigned);\n',
 'vm_exec.h': '#include "sys_error.h"\nbool vm_exec_fault_latch(uint8_t,uint32_t,err_tag_e);\nerr_h vm_exec_stop(void);\n',
}
for name, body in STUBS.items(): (OUT / name).write_text(body)
zig = os.environ.get('ZIG') or shutil.which('zig') or str(ROOT/'build/error_test_compiler/ziglang/zig.exe')
node = os.environ.get('NODE') or shutil.which('node') or 'C:/Program Files/nodejs/node.exe'
includes = [OUT, ROOT/'components/sys_errors/include', ROOT/'components/sys_errors/codes',
            ROOT/'components/devices/devices/include', ROOT/'components/VM/core/errors',
            ROOT/'components/codecs/encoders', ROOT/'components/runit']
includes += list((ROOT/'components/system').glob('*/include'))
sources = ['components/sys_errors/sys_error.c', 'components/sys_errors/sys_error_handler.c',
           'components/system/sys_device/sys_device.c', 'tools/tests/sys_errors_test.c']
objects = []
for i, source in enumerate(sources):
    obj = OUT / f'{i}.o'
    subprocess.run([zig, 'cc', '-target', 'wasm32-freestanding', '-ffreestanding', '-fno-builtin', '-D__thread=',
                    '-std=gnu17', '-O1', '-Wall', '-Werror', '-Wno-unused-variable',
                    '-Wno-gnu-variable-sized-type-not-at-end',
                    *[f'-I{x}' for x in includes], '-c', str(ROOT/source), '-o', str(obj)], check=True)
    objects.append(str(obj))
module = OUT/'tests.wasm'
subprocess.run([zig, 'cc', '-target', 'wasm32-freestanding', '-nostdlib',
                '-Wl,--no-entry', '-Wl,--export=run_tests', '-Wl,--export=checks_run',
                *objects, '-o', str(module)], check=True)
subprocess.run([node, '-e', '''const fs = require('fs');
WebAssembly.instantiate(fs.readFileSync(process.argv[1])).then(({instance}) => {
 const line = instance.exports.run_tests();
 if(line) { console.error('FAIL sys_errors_test.c line '+line); process.exit(1); }
 console.log('PASS: '+instance.exports.checks_run()+' error ownership/policy assertions');
});''', str(module)], check=True)

# Native concurrent allocator test: same implementation, real host threads.
import ctypes
from concurrent.futures import ThreadPoolExecutor
library = OUT / ('pool.dll' if os.name == 'nt' else 'pool.so')
subprocess.run([zig, 'cc', '-shared', '-O1', '-std=gnu17', '-Wno-gnu-variable-sized-type-not-at-end',
                *([] if os.name == 'nt' else ['-fPIC']),
                *[f'-I{x}' for x in includes],
                str(ROOT/'components/sys_errors/sys_error.c'),
                str(ROOT/'tools/tests/sys_errors_threads.c'), '-o', str(library)], check=True)
pool = ctypes.CDLL(str(library))
pool.stress_pool.argtypes = [ctypes.c_uint]
pool.stress_pool.restype = ctypes.c_int
with ThreadPoolExecutor(max_workers=4) as workers:
    results = list(workers.map(pool.stress_pool, range(4)))
assert results == [0, 0, 0, 0], results
print('PASS: 40,000 concurrent chain allocation/validation/release cycles')

subprocess.run([node, str(ROOT/'tools/tests/error_decoder_test.js')], check=True)
