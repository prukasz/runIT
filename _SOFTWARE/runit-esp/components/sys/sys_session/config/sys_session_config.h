#pragma once

/**
 * @file sys_session_config.h
 * @brief What a session's frame and its peers are, the one place they are assigned (wire format: never renumber):
 * the version, the ids, the frame byte and kinds, the control ops and the peer kinds. Included by
 * sys_session.h; published to the app by templates/generate_enums.py.
 */

/* The frame format: a session opens only between peers of the same one. */
//#ref-defines session_version @alias Session version @prefix SYS_SESSION_VERSION
#define SYS_SESSION_VERSION 2u  //@alias Version @description The frame format with CMD_FRAME_MAX and CMD_HEAD

/* Ids: every peer says its own in its hello and two peers may not have one. A board's is its place in the project (the
   app sets it, NVS keeps it), the app's is reserved, and any other client picks one. */
//#ref-defines session_id @alias Peer ids @prefix SYS_SESSION_ID_
#define SYS_SESSION_ID_DEFAULT      0x00u  //@alias Default @description A board's id until the app sets one; the master's by convention
#define SYS_SESSION_ID_LAST         0xEFu  //@alias Last board @description Boards are 0x00..0xEF
#define SYS_SESSION_ID_APP          0xF0u  //@alias App @description The app's id, reserved
#define SYS_SESSION_ID_CLIENT_FIRST 0xF1u  //@alias First other client @description Other clients (a test host) are 0xF1..0xFC; each picks its own
#define SYS_SESSION_ID_CLIENT_LAST  0xFCu  //@alias Last client @description The last client id
#define SYS_SESSION_ID_NONE         0xFDu  //@alias None @description A peer before its hello: no id yet
#define SYS_SESSION_ID_SELF         0xFEu  //@alias This board @description A command's src when the board runs its own (ROUTER_SRC_SELF); never in a frame's header
#define SYS_SESSION_ID_BROADCAST    0xFFu  //@alias Every board @description Every board (STOP, time); never answered

/* The first byte of every frame: bits 0-1 a request's or a stream's flags, the router's own (ROUTER_FLAG_URGENT,
   _NOREPLY), so a command's flags are this byte & SYS_SESSION_FRAME_FLAGS; bit 2 an answer's status
   (router_status_e); bits 2-5 zero otherwise; bits 6-7 the kind. */
//#ref-defines session_frame @alias Frame byte @prefix SYS_SESSION_FRAME_
#define SYS_SESSION_FRAME_FLAGS  (3u << 0)  //@alias Flags @description Bits 0-1: ROUTER_FLAG_URGENT, ROUTER_FLAG_NOREPLY
#define SYS_SESSION_FRAME_STATUS (1u << 2)  //@alias Status @description Bit 2: an answer's router_status_e (OK, ERROR)
#define SYS_SESSION_FRAME_KIND   (3u << 6)  //@alias Kind @description Bits 6-7: sys_session_kind_e

/** @brief What a frame carries: (kind << 6) in its first byte. */
//#ref-enum @alias Frame kind
typedef enum {
  SYS_SESSION_REQUEST = 0,  //@alias Request @description A command for dst; its answer goes back to src
  SYS_SESSION_ANSWER = 1,   //@alias Answer @description The answer to the request src/seq: its status in the frame byte, then data
  SYS_SESSION_PUSH = 2,     //@alias Push @description [stream id | channel][payload], never answered: a stream's message, a channel's frame
  SYS_SESSION_CONTROL = 3,  //@alias Control @description The session's own: hello, ping, gone
} sys_session_kind_e;

/* The first byte of a PUSH frame's body: below SYS_SESSION_CHANNEL_FIRST a stream's id (sys_stream_config.h), from
   it a channel of the session itself: no stream, no queue, no subscribers; a module sends its frame to the one peer it
   names (sys_session_push) and the board it reaches hands it to its listener (sys_session_on_push); DATA is the
   session's own: the board it reaches sends the data on its provider. */
#define SYS_SESSION_CHANNEL_FIRST 0x80u  // 0x80..0xFF: channels; no stream takes them

//#ref-defines session_channel @alias Session channels @prefix SYS_SESSION_CHANNEL_
#define SYS_SESSION_CHANNEL_WATCHES 0x80u  //@alias Watches @description A watch's entry, to its owner (SYS_WATCH.MD "Entries")
#define SYS_SESSION_CHANNEL_REMOTE  0x81u  //@alias Remote devices @description A device's events and errors, to its subscribers (SYS_REMOTE.MD)
#define SYS_SESSION_CHANNEL_DATA    0x82u  //@alias Data @description [to id][from id][data]: data for the provider to id of dst, sent on it as is (SYS_SESSION.MD "Data to a provider")

/** @brief A control frame's op: the first byte after its header. */
//#ref-enum @alias Control op
typedef enum {
  SYS_SESSION_HELLO = 1,       //@alias Hello @description Who I am (sys_session_hello_t): opens or updates a session
  SYS_SESSION_PING = 2,        //@alias Ping @description Nothing else to send: the session is alive
  SYS_SESSION_NO_SESSION = 3,  //@alias No session @description A frame before the hello: send your hello again
  SYS_SESSION_GONE = 4,        //@alias Gone @description [client]: a client of the master no longer reached; to every slave
} sys_session_control_e;

/** @brief Where a provider leads. */
//#ref-enum @alias Peer
typedef enum {
  SYS_SESSION_MASTER = 0,  //@alias Master @description The board's master: its only session; a board without one is the master
  SYS_SESSION_SLAVE = 1,   //@alias Slave @description A board of this master
  SYS_SESSION_CLIENT = 2,  //@alias Client @description The app, a test host: on the master only
} sys_session_peer_e;
