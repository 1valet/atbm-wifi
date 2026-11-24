/*
 * Received-frame classification and the station receive rules.
 *
 * Each frame format has one decoder: ieee80211_rx_decode_80211() for
 * frames with an 802.11 header and ieee80211_rx_decode_8023() for frames
 * the firmware converted to 802.3, whose 802.11 fields come in the rx
 * status. Both fill the same struct ieee80211_rx_class, and the rules
 * below take only that, so the native path, the 802.3 path and the
 * reorder-timer release apply the same rules to the same fields. The
 * rules follow upstream mac80211's rx.c.
 *
 * This file and rx_class.c use only basic types so that test/rx_class
 * can build them in user space.
 */
#ifndef IEEE80211_RX_CLASS_H
#define IEEE80211_RX_CLASS_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#include <asm/byteorder.h>
#else
#include "rx_class_env.h"
#endif

/* counters per station: one per TID, then the extra non-QoS/mgmt one */
#define IEEE80211_RX_CLASS_TIDS		16
#define IEEE80211_RX_CLASS_COUNTERS	(IEEE80211_RX_CLASS_TIDS + 1)
#define IEEE80211_RX_CLASS_SEQ_CACHE	64

struct ieee80211_rx_class {
	bool data;		/* a data frame */
	bool mgmt;		/* a management frame */
	bool nullfunc;		/* a (QoS) null data frame */
	bool multicast;		/* group addressed (address 1 / destination) */
	bool qos;		/* QoS data, with a TID */
	u8 tid;
	u16 fc;			/* frame control, host order */
	u16 seq_ctrl;		/* sequence control, host order */
	bool retry;		/* the Retry bit */
	bool protected;		/* the Protected Frame bit */
	bool fw_ccmp;		/* the firmware reports a CCMP decryption */
	bool ext_iv;		/* the CCMP header has its Ext IV bit */
	u8 keyidx;		/* key ID from the CCMP header */
	u8 pn[6];		/* packet number, most significant byte first */
};

/* frame: the 802.11 frame from its header; returns 0, or -1 if too short */
int ieee80211_rx_decode_80211(const u8 *frame, unsigned int len, bool fw_ccmp,
			      struct ieee80211_rx_class *c);

/*
 * eth: the 802.3 frame; fc, seq_ctrl, qos_ctrl: the firmware's copies of
 * the 802.11 fields; ccmp_hdr: the 8-byte CCMP header the firmware
 * passed. Returns 0, or -1 if too short.
 */
int ieee80211_rx_decode_8023(const u8 *eth, unsigned int len, u16 fc,
			     u16 seq_ctrl, u16 qos_ctrl, const u8 *ccmp_hdr,
			     bool fw_ccmp, struct ieee80211_rx_class *c);

/* the duplicate-detection counter: the TID for QoS data, else the extra */
u8 ieee80211_rx_class_seqno_idx(const struct ieee80211_rx_class *c);

/*
 * the replay counter: the TID for QoS data, the extra one for management
 * frames and counter 0 for other data
 */
u8 ieee80211_rx_class_security_idx(const struct ieee80211_rx_class *c);

/* only unicast QoS data other than null frames goes through reordering */
bool ieee80211_rx_class_reorder(const struct ieee80211_rx_class *c);

enum ieee80211_rx_dup {
	IEEE80211_RX_DUP_NEW,		/* a new frame */
	IEEE80211_RX_DUP_RETRY,		/* a retry of the last frame: drop */
	IEEE80211_RX_DUP_SEEN,		/* QoS seq seen recently: drop if late */
};

/*
 * Duplicate detection for unicast frames (group frames are never
 * duplicates here), moving the station's counters:
 * last_seq_ctrl/last_fc per seqno_idx as upstream, and the recent
 * sequence cache the reorder buffer uses for QoS data.
 */
enum ieee80211_rx_dup
ieee80211_rx_class_dup(const struct ieee80211_rx_class *c,
		       __le16 *last_seq_ctrl, __le16 *last_fc,
		       u16 (*seq_cache)[IEEE80211_RX_CLASS_SEQ_CACHE]);

/* which key to check a protected frame against */
#define IEEE80211_RX_KEY_NONE	(-1)	/* not protected */
#define IEEE80211_RX_KEY_PTK	(-2)	/* the station's pairwise key */
/* or 0..3: the group key with that index */
int ieee80211_rx_class_key_slot(const struct ieee80211_rx_class *c);

/*
 * Whether a protected frame may be checked as CCMP at all: the firmware
 * reported a CCMP decryption and the header has the Ext IV bit.
 */
bool ieee80211_rx_class_ccmp_ok(const struct ieee80211_rx_class *c);

#endif
