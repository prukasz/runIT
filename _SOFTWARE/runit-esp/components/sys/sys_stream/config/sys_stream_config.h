#pragma once

/**
 * @file sys_stream_config.h
 * @brief The system streams, a stream's direction, flags and targets: the one place they are assigned (wire format:
 * never renumber). Included by sys_stream.h; published to the app by templates/generate_enums.py.
 */

/* The stream id byte, the first of a STREAM frame's body (SYS_SESSION.MD "Streams"): 0x00 is invalid; 0x01..0x0F the
   board's own streams (created by the app layer, runit_board_cfg.c; ERRORS and EVENTS urgent); 0x10..0x7F user
   streams; from SYS_SESSION_CHANNEL_FIRST (0x80) the session's channels, which no stream takes. */
//#ref-defines stream @alias Streams @prefix SYS_STREAM_
#define SYS_STREAM_TELEMETRY 0x02u  //@alias Telemetry @description Telemetry: the VM's subscribed values
#define SYS_STREAM_LOGS      0x03u  //@alias Logs @description The log lines
#define SYS_STREAM_ERRORS    0x04u  //@alias Errors @description The error records (urgent)
#define SYS_STREAM_EVENTS    0x06u  //@alias Events @description The event notifications (urgent)

/* The ranges of the stream id byte. */
//#ref-defines stream_range @alias Stream ids @prefix SYS_STREAM_ID_
#define SYS_STREAM_ID_USER_FIRST 0x10u  //@alias First user stream @description User streams are 0x10..0x7F
#define SYS_STREAM_ID_LAST       0x7Fu  //@alias Last stream @description From 0x80 the byte is a session channel

/** @brief A stream's direction, set at create: which way it carries. A device both ways is two streams. */
//#ref-enum @alias Stream direction
typedef enum {
  SYS_STREAM_OUT = 0,  //@alias Out @description The board sends (sys_stream_send): to its providers, peers, callbacks
  SYS_STREAM_IN = 1,   //@alias In @description What comes in on its providers: to its peers and callbacks
} sys_stream_dir_e;

/* A stream's flags, set at create. URGENT: its frames go to the front of every provider's queue. */
//#ref-defines stream_flag @alias Stream flags @prefix SYS_STREAM_FLAG_
#define SYS_STREAM_FLAG_URGENT (1u << 0)  //@alias Urgent @description Its frames go first on every way out

/** @brief A stream's target: one list of the three kinds. */
//#ref-enum @alias Stream target
typedef enum {
  SYS_STREAM_TARGET_PROVIDER = 0,  //@alias Provider @description A binding: OUT sends the payload there as is, IN takes what comes in there
  SYS_STREAM_TARGET_PEER = 1,      //@alias Peer @description A subscriber: [stream id][payload] in a STREAM frame of its session
  SYS_STREAM_TARGET_CALLBACK = 2,  //@alias Callback @description The board's own code (a listener), on the stream task
} sys_stream_target_e;

/* A stream's state, in LIST. */
//#ref-defines stream_state @alias Stream state @prefix SYS_STREAM_STATE_
#define SYS_STREAM_STATE_SYSTEM (1u << 0)  //@alias System @description The board's own: not removed
