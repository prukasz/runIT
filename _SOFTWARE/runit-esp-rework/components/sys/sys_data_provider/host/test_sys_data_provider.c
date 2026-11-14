/* Host tests of the provider registry (sys_data_provider.c) on a fake driver: the send in two parts, its limits and
 * flags, the spare rule, open / close, add / remove, up / down and delivery.
 * Run: python test_host/run.py sys_data_provider   (runit-esp/test_host/HOST_TESTS.MD) */
#include <string.h>

#include "cmd.h"
#include "host_assert.h"
#include "host_mocks.h"
#include "sys_data_provider_driver.h"
#include "unity.h"

/* ---- a fake driver: a queue of QUEUE_FRAMES frames, emptied only by the test ------------------------------------- */

#define QUEUE_FRAMES 4u

typedef struct {
  uint8_t data[CMD_FRAME_MAX];
  uint32_t len;
  uint8_t flags;
} frame_t;

typedef struct {
  sys_data_provider_t p;
  frame_t queue[QUEUE_FRAMES];
  uint32_t queued;
  uint32_t sends;           // calls of send
  uint32_t destroyed;       // calls of destroy
  err_h fail;               // the next send returns it (an error of the driver's own)
  bool remove_in_send;      // the next send tries to remove its own provider
  err_h removed_in_send;    // what that remove returned
} fake_t;

static fake_t s_fake[CONFIG_SYS_DATA_PROVIDER_MAX + 1];

static err_h fake_send(sys_data_provider_t* self, const uint8_t* head, uint32_t head_len, const uint8_t* body,
                       uint32_t body_len, uint8_t flags) {
  fake_t* f = self->driver;
  f->sends++;
  if (f->remove_in_send) {
    f->remove_in_send = false;
    f->removed_in_send = sys_data_provider_remove(self->id);
  }
  if (f->fail != NULL) {
    err_h err = f->fail;
    f->fail = NULL;
    return err;
  }
  const bool full = f->queued == QUEUE_FRAMES;
  if (full || sys_data_provider_spare_refused(flags, f->queued, 1, QUEUE_FRAMES)) {
    return ERR_NEW(ERR_PROV_BUSY, .provider = self->id);
  }
  frame_t* fr = &f->queue[f->queued++];
  memcpy(fr->data, head, head_len);
  memcpy(fr->data + head_len, body, body_len);
  fr->len = head_len + body_len;
  fr->flags = flags;
  return NULL;
}

static void fake_destroy(sys_data_provider_t* self) {
  ((fake_t*)self->driver)->destroyed++;
}

// Adds fake `i` as provider `id`, up, with `state` bits beside UP.
static fake_t* fake_add(uint32_t i, uint8_t id, uint8_t state) {
  fake_t* f = &s_fake[i];
  memset(f, 0, sizeof(*f));
  f->p.send = fake_send;
  f->p.destroy = fake_destroy;
  f->p.driver = f;
  f->p.id = id;
  f->p.link = SYS_DATA_PROVIDER_LINK_UART;
  f->p.state = (uint8_t)(state | SYS_DATA_PROVIDER_STATE_UP);
  TEST_ASSERT_NULL(sys_data_provider_add(&f->p));
  return f;
}

/* ---- a user of the rx hook ------------------------------------------------------------------------------------ */

static struct {
  uint32_t calls;
  uint8_t id;
  uint8_t data[16];
  uint32_t len;
  bool changed;  // the last call was up / down (data NULL)
} s_rx;

static void user_rx(void* ctx, uint8_t id, const uint8_t* data, uint32_t len) {
  (void)ctx;
  s_rx.calls++;
  s_rx.id = id;
  s_rx.len = len;
  s_rx.changed = data == NULL;
  if (data != NULL) {
    memcpy(s_rx.data, data, len < sizeof(s_rx.data) ? len : sizeof(s_rx.data));
  }
}

/* Every slot freed between tests: closed, the board's own flag dropped, removed. */
void setUp(void) {
  host_log_clear();
  memset(&s_rx, 0, sizeof(s_rx));
}

void tearDown(void) {
  for (uint32_t i = 0; i < sizeof(s_fake) / sizeof(s_fake[0]); i++) {
    fake_t* f = &s_fake[i];
    if (f->p.send != NULL && f->p.driver == f) {  // added or not, up or down: a remove of what is not there is NOT_FOUND
      err_free(sys_data_provider_close(f->p.id));
      f->p.state &= (uint8_t)~SYS_DATA_PROVIDER_STATE_SYSTEM;
      err_free(sys_data_provider_remove(f->p.id));
    }
    memset(f, 0, sizeof(*f));
  }
}

#define ASSERT_ERR(tag, expr)       \
  do {                              \
    err_h e_ = (expr);              \
    TEST_ASSERT_ERR_TAG((tag), e_); \
    err_free(e_);                   \
  } while (0)

/* ---- the send in two parts ------------------------------------------------------------------------------------ */

static void test_two_parts_reach_the_driver_as_one_frame(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t head[] = {0xA1, 0xA2, 0xA3};
  const uint8_t body[] = {0x01, 0x02};
  TEST_ASSERT_NULL(sys_data_provider_send(7, head, sizeof(head), body, sizeof(body), SYS_DATA_PROVIDER_SEND_URGENT));
  TEST_ASSERT_EQUAL_UINT32(1, f->queued);
  const uint8_t want[] = {0xA1, 0xA2, 0xA3, 0x01, 0x02};
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), f->queue[0].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f->queue[0].data, sizeof(want));
  TEST_ASSERT_EQUAL_HEX8(SYS_DATA_PROVIDER_SEND_URGENT, f->queue[0].flags);  // the flags pass as they are
}

static void test_either_part_may_be_empty(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  const uint8_t b[] = {0x22, 0x33};
  TEST_ASSERT_NULL(sys_data_provider_send(7, a, sizeof(a), NULL, 0, 0));
  TEST_ASSERT_NULL(sys_data_provider_send(7, NULL, 0, b, sizeof(b), 0));
  TEST_ASSERT_EQUAL_UINT32(2, f->queued);
  TEST_ASSERT_EQUAL_UINT32(1, f->queue[0].len);
  TEST_ASSERT_EQUAL_HEX8(0x11, f->queue[0].data[0]);
  TEST_ASSERT_EQUAL_UINT32(2, f->queue[1].len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(b, f->queue[1].data, 2);
}

static void test_an_empty_frame_or_a_part_without_bytes_is_refused(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  ASSERT_ERR(ERR_CMD_BAD_ARG, sys_data_provider_send(7, NULL, 0, NULL, 0, 0));
  ASSERT_ERR(ERR_CMD_BAD_ARG, sys_data_provider_send(7, a, 0, a, 0, 0));
  ASSERT_ERR(ERR_CMD_BAD_ARG, sys_data_provider_send(7, NULL, 1, a, 1, 0));
  ASSERT_ERR(ERR_CMD_BAD_ARG, sys_data_provider_send(7, a, 1, NULL, 1, 0));
  TEST_ASSERT_EQUAL_UINT32(0, f->sends);
}

static void test_a_frame_past_the_limit_is_refused_never_cut(void) {
  fake_t* f = fake_add(0, 7, 0);
  static uint8_t big[CMD_FRAME_MAX + 1];
  TEST_ASSERT_NULL(sys_data_provider_send(7, big, CMD_HEAD, big, CMD_DATA_MAX, 0));  // exactly the limit
  TEST_ASSERT_EQUAL_UINT32(CMD_FRAME_MAX, f->queue[0].len);
  err_h e = sys_data_provider_send(7, big, CMD_HEAD, big, CMD_DATA_MAX + 1, 0);
  TEST_ASSERT_ERR_TAG(ERR_PROV_TOO_LONG, e);
  TEST_ASSERT_EQUAL_UINT16(CMD_FRAME_MAX + 1, ERR_DATA(e, ERR_PROV_TOO_LONG)->len);
  TEST_ASSERT_EQUAL_UINT16(CMD_FRAME_MAX, ERR_DATA(e, ERR_PROV_TOO_LONG)->max);
  err_free(e);
  ASSERT_ERR(ERR_PROV_TOO_LONG, sys_data_provider_send(7, NULL, 0, big, UINT32_MAX, 0));  // one part alone
  ASSERT_ERR(ERR_PROV_TOO_LONG, sys_data_provider_send(7, big, UINT32_MAX, big, UINT32_MAX, 0));  // the sum wraps
  TEST_ASSERT_EQUAL_UINT32(1, f->sends);  // the driver saw only the first
}

static void test_no_provider_or_one_down_is_not_sent(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  ASSERT_ERR(ERR_PROV_NOT_FOUND, sys_data_provider_send(8, a, 1, NULL, 0, 0));
  sys_data_provider_set_up(&f->p, false);
  ASSERT_ERR(ERR_PROV_DOWN, sys_data_provider_send(7, a, 1, NULL, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0, f->sends);
}

static void test_busy_passes_as_it_is_other_driver_errors_are_wrapped(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  f->queued = QUEUE_FRAMES;
  ASSERT_ERR(ERR_PROV_BUSY, sys_data_provider_send(7, a, 1, NULL, 0, SYS_DATA_PROVIDER_SEND_NOWAIT));
  f->queued = 0;
  f->fail = ERR_NEW(ERR_CMD_BAD_LEN, .got = 1, .need = 2);
  err_h e = sys_data_provider_send(7, a, 1, NULL, 0, 0);
  TEST_ASSERT_ERR_TAG(ERR_PROV_SEND, e);
  TEST_ASSERT_EQUAL_UINT8(7, ERR_DATA(e, ERR_PROV_SEND)->provider);
  TEST_ASSERT_NOT_NULL(e->next);
  TEST_ASSERT_EQUAL_HEX32(ERR_CMD_BAD_LEN, e->next->tag);
  err_free(e);
}

static void test_a_provider_is_not_removed_while_its_driver_sends(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t a[] = {0x11};
  f->remove_in_send = true;
  TEST_ASSERT_NULL(sys_data_provider_send(7, a, 1, NULL, 0, 0));
  TEST_ASSERT_ERR_TAG(ERR_PROV_IN_USE, f->removed_in_send);
  err_free(f->removed_in_send);
  TEST_ASSERT_EQUAL_UINT32(0, f->destroyed);
  TEST_ASSERT_NULL(sys_data_provider_remove(7));  // the send is over
  TEST_ASSERT_EQUAL_UINT32(1, f->destroyed);
}

/* ---- SPARE: half of a queue kept for what may not be lost ----------------------------------------------------- */

static void test_spare_is_refused_past_half_the_queue(void) {
  TEST_ASSERT_FALSE(sys_data_provider_spare_refused(0, 3, 1, 4));                              // not SPARE
  TEST_ASSERT_FALSE(sys_data_provider_spare_refused(SYS_DATA_PROVIDER_SEND_SPARE, 0, 1, 4));
  TEST_ASSERT_FALSE(sys_data_provider_spare_refused(SYS_DATA_PROVIDER_SEND_SPARE, 1, 1, 4));   // to half: taken
  TEST_ASSERT_TRUE(sys_data_provider_spare_refused(SYS_DATA_PROVIDER_SEND_SPARE, 2, 1, 4));    // past half
  TEST_ASSERT_TRUE(sys_data_provider_spare_refused(SYS_DATA_PROVIDER_SEND_SPARE, 0, 600, 1024));  // bytes: too big
  TEST_ASSERT_FALSE(sys_data_provider_spare_refused(SYS_DATA_PROVIDER_SEND_SPARE, 100, 412, 1024));
  TEST_ASSERT_TRUE(sys_data_provider_spare_refused(SYS_DATA_PROVIDER_SEND_SPARE, 0, 1, 1));    // one frame: none spare
}

static void test_spare_frames_leave_room_for_the_answers(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t push[] = {0x50};
  const uint8_t answer[] = {0x41};
  const uint8_t spare = SYS_DATA_PROVIDER_SEND_SPARE | SYS_DATA_PROVIDER_SEND_NOWAIT;
  TEST_ASSERT_NULL(sys_data_provider_send(7, push, 1, NULL, 0, spare));
  TEST_ASSERT_NULL(sys_data_provider_send(7, push, 1, NULL, 0, spare));
  ASSERT_ERR(ERR_PROV_BUSY, sys_data_provider_send(7, push, 1, NULL, 0, spare));  // half the queue is theirs
  TEST_ASSERT_NULL(sys_data_provider_send(7, answer, 1, NULL, 0, SYS_DATA_PROVIDER_SEND_NOWAIT));
  TEST_ASSERT_NULL(sys_data_provider_send(7, answer, 1, NULL, 0, SYS_DATA_PROVIDER_SEND_NOWAIT));
  ASSERT_ERR(ERR_PROV_BUSY, sys_data_provider_send(7, answer, 1, NULL, 0, SYS_DATA_PROVIDER_SEND_NOWAIT));  // full
  TEST_ASSERT_EQUAL_UINT32(QUEUE_FRAMES, f->queued);
}

/* ---- the rx hook: one owner ----------------------------------------------------------------------------------- */

static void test_the_rx_hook_has_one_owner(void) {
  fake_add(0, 7, 0);
  TEST_ASSERT_NULL(sys_data_provider_open(7, user_rx, NULL));
  TEST_ASSERT_TRUE((sys_data_provider_state(7) & SYS_DATA_PROVIDER_STATE_OPEN) != 0);
  ASSERT_ERR(ERR_PROV_IN_USE, sys_data_provider_open(7, user_rx, NULL));
  ASSERT_ERR(ERR_PROV_IN_USE, sys_data_provider_remove(7));  // open: not removed
  TEST_ASSERT_NULL(sys_data_provider_close(7));
  TEST_ASSERT_NULL(sys_data_provider_open(7, user_rx, NULL));  // free again
  ASSERT_ERR(ERR_PROV_NOT_FOUND, sys_data_provider_open(9, user_rx, NULL));
  ASSERT_ERR(ERR_PROV_NOT_FOUND, sys_data_provider_close(9));
}

static void test_a_provider_without_rx_is_opened_only_without_a_hook(void) {
  fake_add(0, 7, SYS_DATA_PROVIDER_STATE_NO_RX);
  ASSERT_ERR(ERR_PROV_NO_RX, sys_data_provider_open(7, user_rx, NULL));
  TEST_ASSERT_NULL(sys_data_provider_open(7, NULL, NULL));  // its owner reads from its driver
}

static void test_up_down_is_told_once_per_change_and_frames_reach_the_owner(void) {
  fake_t* f = fake_add(0, 7, 0);
  const uint8_t in[] = {0x01, 0x02, 0x03};
  sys_data_provider_deliver(&f->p, in, sizeof(in));  // no owner: dropped
  TEST_ASSERT_EQUAL_UINT32(0, s_rx.calls);
  TEST_ASSERT_NULL(sys_data_provider_open(7, user_rx, NULL));
  sys_data_provider_set_up(&f->p, true);  // up already: nothing
  TEST_ASSERT_EQUAL_UINT32(0, s_rx.calls);
  sys_data_provider_set_up(&f->p, false);
  TEST_ASSERT_EQUAL_UINT32(1, s_rx.calls);
  TEST_ASSERT_TRUE(s_rx.changed);
  TEST_ASSERT_EQUAL_UINT32(0, s_rx.len);
  TEST_ASSERT_FALSE(sys_data_provider_is_up(7));
  sys_data_provider_set_up(&f->p, true);
  TEST_ASSERT_EQUAL_UINT32(2, s_rx.calls);
  TEST_ASSERT_EQUAL_UINT32(1, s_rx.len);
  sys_data_provider_deliver(&f->p, in, sizeof(in));
  TEST_ASSERT_EQUAL_UINT32(3, s_rx.calls);
  TEST_ASSERT_FALSE(s_rx.changed);
  TEST_ASSERT_EQUAL_UINT8(7, s_rx.id);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(in, s_rx.data, sizeof(in));
}

/* ---- add and remove ------------------------------------------------------------------------------------------- */

static void test_ids_are_unique_and_the_slots_are_counted(void) {
  fake_add(0, 1, 0);
  fake_t* twin = &s_fake[1];
  twin->p = (sys_data_provider_t){.send = fake_send, .driver = twin, .id = 1};
  ASSERT_ERR(ERR_PROV_EXISTS, sys_data_provider_add(&twin->p));
  memset(twin, 0, sizeof(*twin));
  for (uint32_t i = 1; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    fake_add(i, (uint8_t)(10 + i), 0);
  }
  fake_t* extra = &s_fake[CONFIG_SYS_DATA_PROVIDER_MAX];
  extra->p = (sys_data_provider_t){.send = fake_send, .driver = extra, .id = 99};
  ASSERT_ERR(ERR_PROV_FULL, sys_data_provider_add(&extra->p));
  memset(extra, 0, sizeof(*extra));
  sys_data_provider_t no_send = {.id = 50};
  ASSERT_ERR(ERR_CMD_BAD_ARG, sys_data_provider_add(&no_send));
}

static void test_the_boards_own_provider_stays_others_go_through_destroy(void) {
  fake_t* own = fake_add(0, 1, SYS_DATA_PROVIDER_STATE_SYSTEM);
  fake_t* made = fake_add(1, 2, 0);
  ASSERT_ERR(ERR_PROV_SYSTEM, sys_data_provider_remove(1));
  TEST_ASSERT_EQUAL_UINT32(0, own->destroyed);
  TEST_ASSERT_NULL(sys_data_provider_remove(2));
  TEST_ASSERT_EQUAL_UINT32(1, made->destroyed);
  TEST_ASSERT_EQUAL_UINT8(0, sys_data_provider_state(2));  // its id finds nothing
  ASSERT_ERR(ERR_PROV_NOT_FOUND, sys_data_provider_remove(2));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_two_parts_reach_the_driver_as_one_frame);
  RUN_TEST(test_either_part_may_be_empty);
  RUN_TEST(test_an_empty_frame_or_a_part_without_bytes_is_refused);
  RUN_TEST(test_a_frame_past_the_limit_is_refused_never_cut);
  RUN_TEST(test_no_provider_or_one_down_is_not_sent);
  RUN_TEST(test_busy_passes_as_it_is_other_driver_errors_are_wrapped);
  RUN_TEST(test_a_provider_is_not_removed_while_its_driver_sends);
  RUN_TEST(test_spare_is_refused_past_half_the_queue);
  RUN_TEST(test_spare_frames_leave_room_for_the_answers);
  RUN_TEST(test_the_rx_hook_has_one_owner);
  RUN_TEST(test_a_provider_without_rx_is_opened_only_without_a_hook);
  RUN_TEST(test_up_down_is_told_once_per_change_and_frames_reach_the_owner);
  RUN_TEST(test_ids_are_unique_and_the_slots_are_counted);
  RUN_TEST(test_the_boards_own_provider_stays_others_go_through_destroy);
  return UNITY_END();
}
