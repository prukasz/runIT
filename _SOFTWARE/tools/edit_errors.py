from pathlib import Path
p=Path('tools/tests/sys_errors_threads.c'); s=p.read_text().replace('    err_h chain=NULL;', '''    if (SE_is_suspended()) return 3;
    SE_suspend(); SE_suspend(); SE_resume();
    if (!SE_is_suspended()) return 4;
    err_h chain=NULL;''').replace('    SE_release(chain);\n  }','    SE_release(chain);\n    SE_resume();\n    if (SE_is_suspended()) return 5;\n  }'); p.write_text(s)
p=Path('tools/tests/sys_errors_test.c'); s=p.read_text(); s=s.replace('static unsigned hook_calls, hook_corrupt;', 'static unsigned hook_calls, hook_corrupt;\nstatic err_tag_e latched_tag;'); s=s.replace('(void)id; (void)owner; (void)tag; bool first=!latched; latched=true; return first;', '(void)id; (void)owner; bool first=!latched; if (first) latched_tag=tag; latched=true; return first;'); s=s.replace('  // Explicit NONE is a no-op;', '''  // A later critical mention supplies classification/context, only one action.
  latched=false; memset(actions,0,sizeof(actions));
  held=SE_ERR_NEW(ERR_DEV_NO_HANDLE,.dev_id=1);
  held=SE_WRAP_ERR(held,ERR_DEV_SUSPENDED,.dev_id=1);
  SE_push_to_handler(held);
  CHECK(actions[2]==1 && latched_tag==ERR_DEV_NO_HANDLE);
  // An ignored deeper device prevents looking through to later critical mentions.
  latched=false; memset(actions,0,sizeof(actions));
  held=SE_ERR_NEW(ERR_DEV_NO_HANDLE,.dev_id=1);
  held=SE_WRAP_ERR(held,ERR_DEV_DEP_FAILED,.dev_id=0);
  held=SE_WRAP_ERR(held,ERR_DEV_SUSPENDED,.dev_id=1);
  SE_push_to_handler(held);
  CHECK(actions[2]==1 && !latched);
  // Pin tags attribute their device independently of IO domain handling.
  SE_push_to_handler(SE_ERR_NEW(ERR_IO_PIN_UNCONFIGURED,.dev_id=1,.pin_num=3));
  CHECK(actions[2]==2);

  // Explicit NONE is a no-op;'''); p.write_text(s)
p=Path('components/sys_errors/include/sys_error.h'); s=p.read_text().replace('// SE_push_to_handler() already no-ops while suspended','// The handler consumes the chain even while reporting is suspended.'); p.write_text(s)
p=Path('components/system/sys_device/sys_device.c'); s=p.read_text().replace('SE_CHECK_IN_RANGE(actions[i], 0, limit - 1);\n\n', 'SE_CHECK_IN_RANGE(actions[i], 0, limit - 1);\n'); p.write_text(s)
p=Path('tools/tests/test_sys_errors.py'); s=p.read_text().replace("[zig, 'cc', '-shared', '-O1', '-std=gnu17',", "[zig, 'cc', '-shared', '-O1', '-std=gnu17', '-Wno-gnu-variable-sized-type-not-at-end',"); p.write_text(s)
