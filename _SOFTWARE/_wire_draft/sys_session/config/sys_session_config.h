#pragma once

/**
 * @file sys_session_config.h
 * @brief What a session's frame and its peers are, the one place they are assigned (wire format: never renumber):
 * the version, the ids, the frame byte and kinds, the control ops, the peer kinds and access. Included by
 * sys_session.h; published to the app by templates/generate_enums.py.
 */

#define SYS_SESSION_VERSION 1u  // the frame format with CMD_FRAME_MAX and CMD_HEAD: a session opens only on the same one

/* Ids: a board's place in the project (the app sets it, NVS keeps it), not its identity (the MAC); a client's, given
   by the board without a parent when the client's session opens. */
//#ref-defines session_id @alias Peer ids @prefix SYS_SESSION_ID_
#define SYS_SESSION_ID_DEFAULT      0x00u  //@alias Default @description A board's id until the app sets one; the root's by convention
#define SYS_SESSION_ID_LAST         0xEFu  //@alias Last board @description Boards are 0x00..0xEF
#define SYS_SESSION_ID_CLIENT_FIRST 0xF0u  //@alias First client @description Clients (app, test host) are 0xF0..0xFC
#define SYS_SESSION_ID_CLIENT_LAST  0xFCu  //@alias Last client @description One per client session of the root
#define SYS_SESSION_ID_NONE         0xFDu  //@alias None @description A client before the root gave it an id
#define SYS_SESSION_ID_SELF         0xFEu  //@alias This board @description A command's src when the board runs its own (ROUTER_SRC_SELF); never in a frame's header
#define SYS_SESSION_ID_BROADCAST    0xFFu  //@alias Every board @description Every board (STOP, time); never answered

/* The first byte of every frame: bits 0-1 a request's or a stream's flags, the router's own (ROUTER_FLAG_URGENT,
   _NOREPLY), so a command's flags are this byte & SYS_SESSION_FRAME_FLAGS; bit 2 an answer's status
   (router_status_e); bits 3-5 zero; bits 6-7 the kind. */
//#ref-defines session_frame @alias Frame byte @prefix SYS_SESSION_FRAME_
#define SYS_SESSION_FRAME_FLAGS  (3u << 0)  //@alias Flags @description Bits 0-1: ROUTER_FLAG_URGENT, ROUTER_FLAG_NOREPLY
#define SYS_SESSION_FRAME_STATUS (1u << 2)  //@alias Status @description Bit 2: an answer's router_status_e (OK, ERROR)
#define SYS_SESSION_FRAME_KIND   (3u << 6)  //@alias Kind @description Bits 6-7: sys_session_kind_e

/** @brief What a frame carries: (kind << 6) in its first byte. */
//#ref-enum @alias Frame kind
typedef enum {
  SYS_SESSION_REQUEST = 0,  //@alias Request @description A command for dst; its answer goes back to src
  SYS_SESSION_ANSWER = 1,   //@alias Answer @description The answer to the request src/seq: its status in the frame byte, then data
  SYS_SESSION_STREAM = 2,   //@alias Stream @description One connector frame, to a peer subscribed to the connector
  SYS_SESSION_CONTROL = 3,  //@alias Control @description The session's own: hello, ping, gone
} sys_session_kind_e;

/** @brief A control frame's op: the first byte after its header. */
//#ref-enum @alias Control op
typedef enum {
  SYS_SESSION_HELLO = 1,       //@alias Hello @description Who I am (sys_session_hello_t): opens or updates a session
  SYS_SESSION_PING = 2,        //@alias Ping @description Nothing else to send: the session is alive
  SYS_SESSION_NO_SESSION = 3,  //@alias No session @description A frame before the hello: send your hello again
  SYS_SESSION_GONE = 4,        //@alias Gone @description [count][ids]: peers no longer reached; from the parent, passed down
} sys_session_control_e;

/** @brief Where a provider leads. */
//#ref-enum @alias Peer
typedef enum {
  SYS_SESSION_PARENT = 0,  //@alias Parent @description Towards the root: one at most; a board without one is the root
  SYS_SESSION_CHILD = 1,   //@alias Child @description A board below
  SYS_SESSION_CLIENT = 2,  //@alias Client @description The app, a test host: on a board without a parent only
} sys_session_peer_e;

/** @brief What the commands from a peer may do, checked when a request comes in. */
//#ref-enum @alias Peer access
typedef enum {
  SYS_SESSION_READ = 0,  //@alias Read only @description Reads only: a write is refused (ERR_SESSION_DENIED)
  SYS_SESSION_FULL = 1,  //@alias Full @description Every command
} sys_session_access_e;
