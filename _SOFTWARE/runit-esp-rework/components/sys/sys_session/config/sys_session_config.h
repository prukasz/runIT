#pragma once

/**
 * @file sys_session_config.h
 * @brief What a session's frame and its peers are, the one place they are assigned (wire format: never renumber):
 * the ids, the frame byte and kinds, the channels, the control ops, the ways and the roles. Included by sys_session.h;
 * published to the app by templates/generate_enums.py.
 */

/* Ids: every peer says its own in its hello and two peers may not have one. A board's is its place in the project (the
   app sets it, NVS keeps it), the app's is reserved, and any other client picks one. */
//#ref-defines session_id @alias Peer ids @prefix SYS_SESSION_ID_
#define SYS_SESSION_ID_DEFAULT      0x00u  //@alias Default @description A board's id until the app sets one
#define SYS_SESSION_ID_LAST         0xEFu  //@alias Last board @description Boards are 0x00..0xEF
#define SYS_SESSION_ID_APP          0xF0u  //@alias App @description The app's id, reserved
#define SYS_SESSION_ID_CLIENT_FIRST 0xF1u  //@alias First other client @description Other clients (a test host) are 0xF1..0xFC; each picks its own
#define SYS_SESSION_ID_CLIENT_LAST  0xFCu  //@alias Last client @description The last client id
#define SYS_SESSION_ID_NONE         0xFDu  //@alias None @description A peer before its hello: no id yet
#define SYS_SESSION_ID_SELF         0xFEu  //@alias This board @description A command's src when the board runs its own (ROUTER_SRC_SELF); never in a frame's header
#define SYS_SESSION_ID_BROADCAST    0xFFu  //@alias Every board @description Every board (STOP, time); never answered

/* The first byte of every frame: bits 0-1 a request's or a stream's flags, the router's own (ROUTER_FLAG_URGENT,
   _NOREPLY), so a command's flags are this byte & SYS_SESSION_FRAME_FLAGS; bit 2 an answer's status
   (router_status_e); bit 3 a PUSH sent without a counter; bits 4-5 zero; bits 6-7 the kind. */
//#ref-defines session_frame @alias Frame byte @prefix SYS_SESSION_FRAME_
#define SYS_SESSION_FRAME_FLAGS     (3u << 0)  //@alias Flags @description Bits 0-1: ROUTER_FLAG_URGENT, ROUTER_FLAG_NOREPLY
#define SYS_SESSION_FRAME_STATUS    (1u << 2)  //@alias Status @description Bit 2: an answer's router_status_e (OK, ERROR)
#define SYS_SESSION_FRAME_UNCOUNTED (1u << 3)  //@alias Uncounted @description Bit 3: a PUSH outside its sender's counters (its table was full): seq 0, no gap known
#define SYS_SESSION_FRAME_KIND      (3u << 6)  //@alias Kind @description Bits 6-7: sys_session_kind_e

/** @brief What a frame carries: (kind << 6) in its first byte. */
//#ref-enum @alias Frame kind
typedef enum {
  SYS_SESSION_REQUEST = 0,  //@alias Request @description A command for dst; its answer goes back to src
  SYS_SESSION_ANSWER = 1,   //@alias Answer @description The answer to the request src/seq: its status in the frame byte, then data
  SYS_SESSION_PUSH = 2,     //@alias Push @description [stream id | channel][payload], never answered: a stream's message, a channel's frame
  SYS_SESSION_CONTROL = 3,  //@alias Control @description The session's own: hello, ping, no session, peer
} sys_session_kind_e;

/* The first byte of a PUSH frame's body: below SYS_SESSION_CHANNEL_FIRST a stream's id, from it a channel of the
   session itself: no stream, no queue, no subscribers; a module sends its frame to the one peer it names
   (sys_session_push) and the board it reaches hands it to its listener (sys_session_on_push); DATA is the session's
   own: the board it reaches sends the data on its provider. */
#define SYS_SESSION_CHANNEL_FIRST 0x80u  // 0x80..0xFF: channels; no stream takes them

//#ref-defines session_channel @alias Session channels @prefix SYS_SESSION_CHANNEL_
#define SYS_SESSION_CHANNEL_WATCHES 0x80u  //@alias Watches @description A watch's entry, to its owner
#define SYS_SESSION_CHANNEL_REMOTE  0x81u  //@alias Remote devices @description A device's events and errors, to its subscribers
#define SYS_SESSION_CHANNEL_DATA    0x82u  //@alias Data @description [to id][from id][data]: data for the provider to id of dst, sent on it as is (SYS_SESSION.MD "Data to a provider")

/** @brief A control frame's op: the first byte after its header. */
//#ref-enum @alias Control op
typedef enum {
  SYS_SESSION_HELLO = 1,       //@alias Hello @description Who I am (sys_session_hello_t): opens or updates a session
  SYS_SESSION_PING = 2,        //@alias Ping @description Nothing else to send: the session is alive
  SYS_SESSION_NO_SESSION = 3,  //@alias No session @description A frame on a session that is not open here: send your hello again
  SYS_SESSION_PEER = 4,        //@alias Peer @description sys_session_peer_t: the root's session with that peer opened or ended; to every other peer
} sys_session_control_e;

/** @brief Where a provider leads, given at ATTACH. */
//#ref-enum @alias Way
typedef enum {
  SYS_SESSION_UP = 0,    //@alias Up @description To a client (the app) or to this board's master: what the hello says (the console, the runIT BLE pair)
  SYS_SESSION_DOWN = 1,  //@alias Down @description To a slave of this board (the link, a BLE link made as a central)
} sys_session_way_e;

/** @brief What the peer of a session is: from its way and its hello. */
//#ref-enum @alias Peer
typedef enum {
  SYS_SESSION_MASTER = 0,  //@alias Master @description This board's master: a board's hello on an UP way; then the board's only session
  SYS_SESSION_SLAVE = 1,   //@alias Slave @description A board on a DOWN way
  SYS_SESSION_CLIENT = 2,  //@alias Client @description A client's hello on an UP way (the app, a test host): on the root only
  SYS_SESSION_UNKNOWN = 3, //@alias Unknown @description An UP way before its hello: master or client is the hello's to say
} sys_session_role_e;
