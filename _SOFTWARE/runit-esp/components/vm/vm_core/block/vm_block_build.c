#include "vm_block_build.h"
#include "vm_err.h"
#include "vm_blocks.h"

err_h vm_block_create(vm_block_h* out, uint16_t id, const vm_block_cfg_t* cfg) {
  ERR_CHECK(out != NULL, ERR_VM_BAD_ARG);
  ERR_CHECK(cfg != NULL, ERR_VM_BAD_ARG);
  *out = NULL;

  if (cfg->in_cnt > CONFIG_VM_BLOCK_MAX_IN || cfg->q_cnt > CONFIG_VM_BLOCK_MAX_OUT || cfg->en_cnt > CONFIG_VM_BLOCK_MAX_EN) {
    return ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg->block_idx, .in_cnt = cfg->in_cnt, .q_cnt = cfg->q_cnt);
  }
  if ((cfg->in_cnt && !cfg->in_acc_ids) || (cfg->q_cnt && !cfg->out_obj_ids) || (cfg->en_cnt && !cfg->en_acc_ids)) {
    return ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg->block_idx, .in_cnt = cfg->in_cnt, .q_cnt = cfg->q_cnt);
  }

  /* Everything is resolved before anything is allocated, so a block that names a missing
     accessor never gets built. A failure here (or in the check below) is not rolled back: the loader
     aborts the whole load (vm_loader_abort) and the program is discarded. */
  /* NO_ID on an input means the pin is deliberately unwired -- the block uses
     its own constant. Any other id must resolve. */
  for (uint8_t i = 0; i < cfg->in_cnt; i++) {
    if (cfg->in_acc_ids[i] == VM_BLOCK_NO_ID) continue;
    if (!vm_accessor_get_by_id(cfg->in_acc_ids[i])) {
      return ERR_NEW(ERR_VM_BLK_BAD_REF, .blk_id = cfg->block_idx, .ref_id = cfg->in_acc_ids[i], .slot = i, .kind = VM_REF_KIND_IN);
    }
  }
  for (uint8_t i = 0; i < cfg->q_cnt; i++) {
    if (!vm_obj_get_by_id(cfg->out_obj_ids[i])) {
      return ERR_NEW(ERR_VM_BLK_BAD_REF, .blk_id = cfg->block_idx, .ref_id = cfg->out_obj_ids[i], .slot = i, .kind = VM_REF_KIND_OUT);
    }
  }

  /* Every enable source must resolve. Absence is spelled by en_cnt == 0, not
     by a NO_ID entry: a listed source that does not resolve is a malformed
     program, exactly like an unwired numbered pin. */
  for (uint8_t i = 0; i < cfg->en_cnt; i++) {
    if (!vm_accessor_get_by_id(cfg->en_acc_ids[i])) {
      return ERR_NEW(ERR_VM_BLK_BAD_REF, .blk_id = cfg->block_idx, .ref_id = cfg->en_acc_ids[i], .slot = i, .kind = VM_REF_KIND_EN);
    }
  }

  if (cfg->en_mode > VM_BLK_EN_ALL || cfg->on_error > VM_BLK_ERR_CONTINUE) {
    return ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg->block_idx, .in_cnt = cfg->in_cnt, .q_cnt = cfg->q_cnt);
  }

  /* ENO stays optional, and is the one place NO_ID is still meaningful. */
  vm_obj_h eno = NULL;
  if (cfg->eno_obj_id != VM_BLOCK_NO_ID) {
    eno = vm_obj_get_by_id(cfg->eno_obj_id);
    if (!eno || !eno->head.f.mutable || (vm_obj_t_e)eno->head.d.obj_t != VM_OBJ_B || eno->head.payload_size < 1) {
      return ERR_NEW(ERR_VM_BLK_BAD_REF, .blk_id = cfg->block_idx, .ref_id = cfg->eno_obj_id, .slot = 0, .kind = VM_REF_KIND_ENO);
    }
  }

  /* Several blocks may write one object (the last in execution order wins). usr_protected on an output comes from
     the object upload (block-owned objects carry it, a user variable does not); the ENO object is always claimed
     below. A block naming one object twice among its own outputs and ENO is refused: its two writes would be the
     same pass's, ordered by nothing. */
  for (uint8_t i = 0; i < cfg->q_cnt; i++) {
    bool taken = false;
    for (uint8_t j = 0; j < i && !taken; j++) taken = cfg->out_obj_ids[j] == cfg->out_obj_ids[i];
    if (taken) {
      return ERR_NEW(ERR_VM_BLK_OUTPUT_TAKEN, .blk_id = cfg->block_idx, .obj_id = cfg->out_obj_ids[i], .slot = i, .kind = VM_REF_KIND_OUT);
    }
  }
  if (eno) {
    bool taken = false;
    for (uint8_t i = 0; i < cfg->q_cnt && !taken; i++) taken = cfg->out_obj_ids[i] == cfg->eno_obj_id;
    if (taken) {
      return ERR_NEW(ERR_VM_BLK_OUTPUT_TAKEN, .blk_id = cfg->block_idx, .obj_id = cfg->eno_obj_id, .slot = 0, .kind = VM_REF_KIND_ENO);
    }
  }

  size_t total = vm_block_calc_size(cfg->in_cnt, cfg->q_cnt, cfg->en_cnt, cfg->custom_len);
  vm_block_h b = NULL;
  // allocates, zeroes and binds the id in one step -- see vm_store.h
  ERR_TRY(vm_store_alloc((void**)&b, VM_REG_BLK, id, (uint32_t)total));

  b->cfg.block_idx = cfg->block_idx;
  b->cfg.block_type = cfg->block_type;
  b->cfg.in_cnt = cfg->in_cnt;
  b->cfg.q_cnt = cfg->q_cnt;
  b->cfg.en_cnt = cfg->en_cnt;
  b->cfg.en_mode = cfg->en_mode;
  b->cfg.on_error = cfg->on_error;
  b->cfg.custom_len = cfg->custom_len;
  b->cfg.eno = eno;

  /* Pointers, not ids, from here on -- resolving once at load is what removes
     the invalid-id failure mode from every later access. */
  const vm_accessor_t** ins = vm_block_get_inputs(b);
  for (uint8_t i = 0; i < cfg->in_cnt; i++) {
    // NO_ID stays NULL: the pin exists, nothing is wired to it
    ins[i] = (cfg->in_acc_ids[i] == VM_BLOCK_NO_ID) ? NULL : vm_accessor_get_by_id(cfg->in_acc_ids[i]);
  }

  vm_obj_h* outs = vm_block_get_outputs(b);
  for (uint8_t i = 0; i < cfg->q_cnt; i++) outs[i] = vm_obj_get_by_id(cfg->out_obj_ids[i]);

  const vm_accessor_t** ens = vm_block_get_en_list(b);
  for (uint8_t i = 0; i < cfg->en_cnt; i++) ens[i] = vm_accessor_get_by_id(cfg->en_acc_ids[i]);

  if (cfg->custom_len && cfg->custom_data) {
    memcpy(vm_block_get_custom_data(b), cfg->custom_data, cfg->custom_len);
  }

  /* The type's own check needs the built block. A rejected block stays bound but its outputs are
     not claimed below, and the load is aborted, so it never runs. */
  if (!vm_block_verify(b)) {
    return ERR_NEW(ERR_VM_BLK_BAD_SHAPE, .blk_id = cfg->block_idx, .in_cnt = cfg->in_cnt, .q_cnt = cfg->q_cnt);
  }

  /* Only an accepted block claims its ENO: user writes (SET, CLONE) must not fake a block's result. Other outputs
     keep the flag they were uploaded with. */
  if (eno) eno->head.f.usr_protected = 1;

  *out = b;
  return NULL;
}

void vm_block_report_error(err_h cause, vm_block_h b) {
  /* Same mark BLOCK_CALL leaves -- every route a body reports a failure by has
     to set it, or cfg.on_error would be honoured for some failures and not
     others depending on which macro the block author reached for. */
  vm_block_mark_failed(b);
  err_report(ERR_WRAP(cause, ERR_VM_BLOCK_FAILED, .block_idx = b->cfg.block_idx, .block_type = b->cfg.block_type));
}
