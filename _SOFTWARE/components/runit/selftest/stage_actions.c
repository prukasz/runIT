#include "selftest_frame.h"
#include "sys_actions.h"
#include "sys_interface.h"


static int s_static_action_called = 0;

static err_h test_static_action_fn(void) {
  s_static_action_called++;
  return NULL;
}

void test_sys_actions(void) {
  ESP_LOGI(TAG, "-- sys_actions scopes, recording and wire protocol --");
  s_static_action_called = 0;

  err_h zero_err = sys_actions_bind_static(0, test_static_action_fn);
  ck("binding action zero is rejected", zero_err && zero_err->tag == ERR_INVALID_VAL_UI32);
  SE_release(zero_err);
  zero_err = sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, 0);
  ck("static action zero is rejected", zero_err && zero_err->tag == ERR_INVALID_VAL_UI32);
  SE_release(zero_err);
  zero_err = sys_actions_invoke(SYS_ACTION_SCOPE_DYNAMIC, 0);
  ck("dynamic action zero is rejected", zero_err && zero_err->tag == ERR_INVALID_VAL_UI32);
  SE_release(zero_err);
  zero_err = sys_action_record_start(0);
  ck("recording action zero is rejected", zero_err && zero_err->tag == ERR_INVALID_VAL_UI32);
  SE_release(zero_err);
  zero_err = sys_action_remove(0);
  ck("removing action zero is rejected", zero_err && zero_err->tag == ERR_INVALID_VAL_UI32);
  SE_release(zero_err);

  // 1. Static action binding & invocation
  const uint8_t test_slot = 10;
  err_h err = sys_actions_bind_static(test_slot, test_static_action_fn);
  ck("bind static action slot 10", err == NULL);
  SE_release(err);

  err = sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, test_slot);
  ck("invoke static action slot 10 succeeds", err == NULL && s_static_action_called == 1);
  SE_release(err);

  err = sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, 11);
  ck("invoke unbound static slot returns ERR_ACTION_NOT_FOUND", err != NULL && err->tag == ERR_ACTION_NOT_FOUND);
  SE_release(err);

  err = sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, CONFIG_SYS_ACTIONS_STATIC_SLOTS);
  ck("invoke out-of-range static slot returns ERR_ACTION_NOT_FOUND", err != NULL && err->tag == ERR_ACTION_NOT_FOUND);
  SE_release(err);

  // 2. Wire invocation of static action: 0x03 0x00 0x0A
  const uint8_t wire_static_invoke[] = {SYS_ACTIONS_CLASS_HEADER, 0x00, test_slot};
  err = sys_interface_decode(wire_static_invoke, sizeof(wire_static_invoke));
  ck("wire invoke static action 0x0300YY succeeds", err == NULL && s_static_action_called == 2);
  SE_release(err);

  // 3. Dynamic action recording & management
  const uint8_t dyn_id = 42;
  SE_release(sys_action_remove(dyn_id));

  err = sys_action_record_start(dyn_id);
  ck("sys_action_record_start succeeds", err == NULL);
  SE_release(err);

  err = sys_action_record_start(dyn_id + 1);
  ck("concurrent sys_action_record_start returns ERR_ACTION_RECORDING_BUSY", err != NULL && err->tag == ERR_ACTION_RECORDING_BUSY);
  SE_release(err);

  err = sys_action_record_stop();
  ck("sys_action_record_stop succeeds and remembers id", err == NULL);
  SE_release(err);

  err = sys_action_record_stop();
  ck("redundant sys_action_record_stop is safe no-op", err == NULL);
  SE_release(err);

  // 4. Wire management packets
  // Wire record start: 0x03 0x02 <id>
  const uint8_t wire_record_start[] = {SYS_ACTIONS_CLASS_HEADER, 0x02, dyn_id};
  err = sys_interface_decode(wire_record_start, sizeof(wire_record_start));
  ck("wire record start 0x0302YY succeeds", err == NULL);
  SE_release(err);

  // Wire record stop: 0x03 0x03
  const uint8_t wire_record_stop[] = {SYS_ACTIONS_CLASS_HEADER, 0x03};
  err = sys_interface_decode(wire_record_stop, sizeof(wire_record_stop));
  ck("wire record stop 0x0303 succeeds", err == NULL);
  SE_release(err);

  // Wire remove: 0x03 0x04 <id>
  const uint8_t wire_remove[] = {SYS_ACTIONS_CLASS_HEADER, 0x04, dyn_id};
  err = sys_interface_decode(wire_remove, sizeof(wire_remove));
  ck("wire remove dynamic action 0x0304YY succeeds", err == NULL);
  SE_release(err);

  // Wire dynamic invoke after remove should return ERR_ACTION_NOT_FOUND: 0x03 0x01 <id>
  const uint8_t wire_dynamic_invoke[] = {SYS_ACTIONS_CLASS_HEADER, 0x01, dyn_id};
  err = sys_interface_decode(wire_dynamic_invoke, sizeof(wire_dynamic_invoke));
  ck("wire dynamic invoke for removed action returns ERR_ACTION_NOT_FOUND", err != NULL && err->tag == ERR_ACTION_NOT_FOUND);
  SE_release(err);

  // Wire remove all: 0x03 0x05
  const uint8_t wire_remove_all[] = {SYS_ACTIONS_CLASS_HEADER, 0x05};
  err = sys_interface_decode(wire_remove_all, sizeof(wire_remove_all));
  ck("wire remove all 0x0305 succeeds", err == NULL);
  SE_release(err);

  // 5. Invalid scope
  err = sys_actions_invoke(0x05, 1);
  ck("sys_actions_invoke with invalid scope returns error", err != NULL && err->tag == ERR_INVALID_VAL_UI32);
  SE_release(err);

  // Cleanup
  SE_release(sys_actions_bind_static(test_slot, NULL));
}
