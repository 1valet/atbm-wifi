#include "rx_class.h"

#define FC_FTYPE		0x000c
#define FC_FTYPE_MGMT		0x0000
#define FC_FTYPE_DATA		0x0008
#define FC_STYPE_QOS		0x0080
#define FC_STYPE_NULLFUNC	0x0040
#define FC_TODS			0x0100
#define FC_FROMDS		0x0200
#define FC_RETRY		0x0800
#define FC_PROTECTED		0x4000
#define FC_ORDER		0x8000

#define QOS_TID_MASK		0x000f

static u16 get_le16(const u8 *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

/* the fields both formats carry the same way */
static void decode_fc(u16 fc, u16 seq_ctrl, struct ieee80211_rx_class *c)
{
	c->fc = fc;
	c->seq_ctrl = seq_ctrl;
	c->data = (fc & FC_FTYPE) == FC_FTYPE_DATA;
	c->mgmt = (fc & FC_FTYPE) == FC_FTYPE_MGMT;
	c->qos = c->data && (fc & FC_STYPE_QOS);
	c->nullfunc = c->data && (fc & FC_STYPE_NULLFUNC);
	c->retry = !!(fc & FC_RETRY);
	c->protected = !!(fc & FC_PROTECTED);
}

static void decode_ccmp(const u8 *h, struct ieee80211_rx_class *c)
{
	c->keyidx = h[3] >> 6;
	c->ext_iv = !!(h[3] & 0x20);
	c->pn[0] = h[7];
	c->pn[1] = h[6];
	c->pn[2] = h[5];
	c->pn[3] = h[4];
	c->pn[4] = h[1];
	c->pn[5] = h[0];
}

int ieee80211_rx_decode_80211(const u8 *frame, unsigned int len, bool fw_ccmp,
			      struct ieee80211_rx_class *c)
{
	unsigned int hdrlen = 24;
	u16 fc;

	memset(c, 0, sizeof(*c));
	if (len < 24)
		return -1;
	fc = get_le16(frame);
	decode_fc(fc, get_le16(frame + 22), c);
	c->fw_ccmp = fw_ccmp;
	c->multicast = frame[4] & 0x01;

	if (c->data && (fc & FC_TODS) && (fc & FC_FROMDS))
		hdrlen += 6;
	if (c->qos) {
		if (len < hdrlen + 2)
			return -1;
		c->tid = frame[hdrlen] & QOS_TID_MASK;
		hdrlen += 2;
	}
	/* as ieee80211_hdrlen(): HT Control only in management and QoS data */
	if ((c->mgmt || c->qos) && (fc & FC_ORDER))
		hdrlen += 4;

	if (c->protected) {
		if (len < hdrlen + 8)
			return -1;
		decode_ccmp(frame + hdrlen, c);
	}
	return 0;
}

int ieee80211_rx_decode_8023(const u8 *eth, unsigned int len, u16 fc,
			     u16 seq_ctrl, u16 qos_ctrl, const u8 *ccmp_hdr,
			     bool fw_ccmp, struct ieee80211_rx_class *c)
{
	memset(c, 0, sizeof(*c));
	if (len < 14)
		return -1;
	decode_fc(fc, seq_ctrl, c);
	c->fw_ccmp = fw_ccmp;
	c->multicast = eth[0] & 0x01;
	if (c->qos)
		c->tid = qos_ctrl & QOS_TID_MASK;
	if (c->protected)
		decode_ccmp(ccmp_hdr, c);
	return 0;
}

u8 ieee80211_rx_class_seqno_idx(const struct ieee80211_rx_class *c)
{
	return c->qos ? c->tid : IEEE80211_RX_CLASS_TIDS;
}

u8 ieee80211_rx_class_security_idx(const struct ieee80211_rx_class *c)
{
	if (c->qos)
		return c->tid;
	return c->mgmt ? IEEE80211_RX_CLASS_TIDS : 0;
}

bool ieee80211_rx_class_reorder(const struct ieee80211_rx_class *c)
{
	return c->qos && !c->nullfunc && !c->multicast;
}

enum ieee80211_rx_dup
ieee80211_rx_class_dup(const struct ieee80211_rx_class *c,
		       __le16 *last_seq_ctrl, __le16 *last_fc)
{
	u8 idx = ieee80211_rx_class_seqno_idx(c);
	__le16 sc = cpu_to_le16(c->seq_ctrl);
	__le16 fc = cpu_to_le16(c->fc);

	if (c->multicast)
		return IEEE80211_RX_DUP_NEW;

	/* a retry of the last frame of the same subtype on this counter */
	if (c->retry && last_seq_ctrl[idx] == sc &&
	    (le16_to_cpu(last_fc[idx]) & 0x00fc) == (c->fc & 0x00fc))
		return IEEE80211_RX_DUP_RETRY;
	last_seq_ctrl[idx] = sc;
	last_fc[idx] = fc;
	return IEEE80211_RX_DUP_NEW;
}

/* sn1 before sn2, modulo 4096 */
static bool sn_less(u16 sn1, u16 sn2)
{
	return ((sn1 - sn2) & 0xfff) > 0x800;
}

enum ieee80211_rx_window ieee80211_rx_window(u16 sn, u16 head, bool *started)
{
	if (!*started) {
		if (sn_less(sn, head))
			return IEEE80211_RX_WINDOW_PASS;
		*started = true;
	}
	return sn_less(sn, head) ? IEEE80211_RX_WINDOW_DROP :
				   IEEE80211_RX_WINDOW_REORDER;
}

int ieee80211_rx_class_key_slot(const struct ieee80211_rx_class *c)
{
	if (!c->protected)
		return IEEE80211_RX_KEY_NONE;
	return c->multicast ? c->keyidx : IEEE80211_RX_KEY_PTK;
}

bool ieee80211_rx_class_ccmp_ok(const struct ieee80211_rx_class *c)
{
	return c->protected && c->fw_ccmp && c->ext_iv;
}
