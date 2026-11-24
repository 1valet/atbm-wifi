/*
 * Paired cases for the station receive rules: each frame is built once
 * as an 802.11 frame and once as the firmware's 802.3 conversion of it,
 * and both decodes must give the same classification and rule results.
 */
#include <stdio.h>
#include "rx_class.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		printf("%s:%d: %s: %s\n", __FILE__, __LINE__,		\
		       __func__, #cond);				\
		failures++;						\
	}								\
} while (0)

#define FC_DATA		0x0008
#define FC_QOS_DATA	0x0088
#define FC_QOS_NULL	0x00c8
#define FC_MGMT_ACTION	0x00d0
#define FC_FROMDS	0x0200
#define FC_RETRY	0x0800
#define FC_PROTECTED	0x4000

static const u8 bssid[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const u8 me[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
static const u8 group[6] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb };

struct frame {
	u16 fc;		/* without FromDS, which build_80211 adds for data */
	u16 seq;	/* sequence control */
	u8 tid;
	bool multicast;
	u8 keyidx;
	unsigned long long pn;
};

static void put_le16(u8 *p, u16 v)
{
	p[0] = v & 0xff;
	p[1] = v >> 8;
}

static void build_ccmp(u8 *h, const struct frame *f)
{
	h[0] = f->pn & 0xff;
	h[1] = (f->pn >> 8) & 0xff;
	h[2] = 0;
	h[3] = 0x20 | (f->keyidx << 6);
	h[4] = (f->pn >> 16) & 0xff;
	h[5] = (f->pn >> 24) & 0xff;
	h[6] = (f->pn >> 32) & 0xff;
	h[7] = (f->pn >> 40) & 0xff;
}

/* an AP-to-station 802.11 frame; returns its length */
static unsigned int build_80211(u8 *buf, const struct frame *f)
{
	unsigned int len = 24;
	u16 fc = f->fc;

	if ((fc & 0x000c) == FC_DATA)
		fc |= FC_FROMDS;
	memset(buf, 0, 128);
	put_le16(buf, fc);
	memcpy(buf + 4, f->multicast ? group : me, 6);
	memcpy(buf + 10, bssid, 6);
	memcpy(buf + 16, bssid, 6);
	put_le16(buf + 22, f->seq);
	if (fc & 0x0080 && (fc & 0x000c) == FC_DATA) {
		buf[24] = f->tid;
		len += 2;
	}
	if (fc & FC_PROTECTED) {
		build_ccmp(buf + len, f);
		len += 8;
	}
	return len + 16;	/* payload and MIC */
}

static void decode_both(const struct frame *f, struct ieee80211_rx_class *a,
			struct ieee80211_rx_class *b)
{
	u8 buf[128], eth[64], iv[8] = { 0 };
	unsigned int len = build_80211(buf, f);
	u16 fc = f->fc;

	if ((fc & 0x000c) == FC_DATA)
		fc |= FC_FROMDS;
	CHECK(ieee80211_rx_decode_80211(buf, len, true, a) == 0);

	memset(eth, 0, sizeof(eth));
	memcpy(eth, f->multicast ? group : me, 6);
	memcpy(eth + 6, bssid, 6);
	if (fc & FC_PROTECTED)
		build_ccmp(iv, f);
	CHECK(ieee80211_rx_decode_8023(eth, 60, fc, f->seq, f->tid, iv,
				       true, b) == 0);
}

static void check_same(const struct ieee80211_rx_class *a,
		       const struct ieee80211_rx_class *b)
{
	CHECK(a->data == b->data);
	CHECK(a->multicast == b->multicast);
	CHECK(a->qos == b->qos);
	CHECK(a->tid == b->tid);
	CHECK(a->seq_ctrl == b->seq_ctrl);
	CHECK(a->retry == b->retry);
	CHECK(a->protected == b->protected);
	CHECK(a->keyidx == b->keyidx);
	CHECK(!memcmp(a->pn, b->pn, sizeof(a->pn)));
	CHECK(ieee80211_rx_class_seqno_idx(a) == ieee80211_rx_class_seqno_idx(b));
	CHECK(ieee80211_rx_class_security_idx(a) == ieee80211_rx_class_security_idx(b));
	CHECK(ieee80211_rx_class_reorder(a) == ieee80211_rx_class_reorder(b));
	CHECK(ieee80211_rx_class_key_slot(a) == ieee80211_rx_class_key_slot(b));
	CHECK(ieee80211_rx_class_ccmp_ok(a) == ieee80211_rx_class_ccmp_ok(b));
}

struct counters {
	__le16 last_seq[IEEE80211_RX_CLASS_COUNTERS];
	__le16 last_fc[IEEE80211_RX_CLASS_COUNTERS];
};

/* as station setup does: every row, the non-QoS one included */
static void counters_init(struct counters *c)
{
	memset(c, 0xff, sizeof(*c));
}

/* a frame, then its retransmission, then the next frame, in both formats */
static void check_dup_pair(const struct frame *f, bool expect_dups)
{
	struct counters c8, c3;
	struct ieee80211_rx_class a, b;
	struct frame retry = *f, next = *f;

	retry.fc |= FC_RETRY;
	next.seq += 0x10;
	counters_init(&c8);
	counters_init(&c3);

	decode_both(f, &a, &b);
	CHECK(ieee80211_rx_class_dup(&a, c8.last_seq, c8.last_fc) == IEEE80211_RX_DUP_NEW);
	CHECK(ieee80211_rx_class_dup(&b, c3.last_seq, c3.last_fc) == IEEE80211_RX_DUP_NEW);

	decode_both(&retry, &a, &b);
	CHECK(ieee80211_rx_class_dup(&a, c8.last_seq, c8.last_fc) ==
	      (expect_dups ? IEEE80211_RX_DUP_RETRY : IEEE80211_RX_DUP_NEW));
	CHECK(ieee80211_rx_class_dup(&b, c3.last_seq, c3.last_fc) ==
	      (expect_dups ? IEEE80211_RX_DUP_RETRY : IEEE80211_RX_DUP_NEW));

	decode_both(&next, &a, &b);
	CHECK(ieee80211_rx_class_dup(&a, c8.last_seq, c8.last_fc) == IEEE80211_RX_DUP_NEW);
	CHECK(ieee80211_rx_class_dup(&b, c3.last_seq, c3.last_fc) == IEEE80211_RX_DUP_NEW);
	CHECK(!memcmp(&c8, &c3, sizeof(c8)));
}

static void test_unicast_qos(void)
{
	struct frame f = { FC_QOS_DATA | FC_PROTECTED, 0x0120, 5, false, 0, 0x0102030405ULL };
	struct ieee80211_rx_class a, b;

	decode_both(&f, &a, &b);
	check_same(&a, &b);
	CHECK(a.qos && a.tid == 5);
	CHECK(ieee80211_rx_class_seqno_idx(&a) == 5);
	CHECK(ieee80211_rx_class_security_idx(&a) == 5);
	CHECK(ieee80211_rx_class_reorder(&a));
	CHECK(ieee80211_rx_class_key_slot(&a) == IEEE80211_RX_KEY_PTK);
	CHECK(ieee80211_rx_class_ccmp_ok(&a));
	CHECK(a.pn[5] == 0x05 && a.pn[1] == 0x01);
	check_dup_pair(&f, true);
}

static void test_unicast_non_qos(void)
{
	/* sequence control 0: must not match a fresh station's counters */
	struct frame f = { FC_DATA | FC_PROTECTED, 0x0000, 0, false, 0, 7 };
	struct ieee80211_rx_class a, b;

	decode_both(&f, &a, &b);
	check_same(&a, &b);
	CHECK(!a.qos);
	CHECK(ieee80211_rx_class_seqno_idx(&a) == IEEE80211_RX_CLASS_TIDS);
	CHECK(ieee80211_rx_class_security_idx(&a) == 0);
	CHECK(!ieee80211_rx_class_reorder(&a));
	CHECK(ieee80211_rx_class_key_slot(&a) == IEEE80211_RX_KEY_PTK);
	check_dup_pair(&f, true);
}

static void test_multicast_qos(void)
{
	struct frame f = { FC_QOS_DATA | FC_PROTECTED, 0x0340, 6, true, 1, 99 };
	struct ieee80211_rx_class a, b;

	decode_both(&f, &a, &b);
	check_same(&a, &b);
	/* a group key keeps a counter per TID, as upstream */
	CHECK(ieee80211_rx_class_security_idx(&a) == 6);
	CHECK(!ieee80211_rx_class_reorder(&a));
	CHECK(ieee80211_rx_class_key_slot(&a) == 1);
	/* group frames are never duplicates here */
	check_dup_pair(&f, false);
}

static void test_multicast_non_qos(void)
{
	struct frame f = { FC_DATA | FC_PROTECTED, 0x0050, 0, true, 2, 1234 };
	struct ieee80211_rx_class a, b;

	decode_both(&f, &a, &b);
	check_same(&a, &b);
	CHECK(ieee80211_rx_class_security_idx(&a) == 0);
	CHECK(!ieee80211_rx_class_reorder(&a));
	CHECK(ieee80211_rx_class_key_slot(&a) == 2);
	check_dup_pair(&f, false);
}

static void test_qos_null_and_open(void)
{
	struct frame null = { FC_QOS_NULL, 0x0010, 3, false, 0, 0 };
	struct frame open = { FC_QOS_DATA, 0x0010, 3, false, 0, 0 };
	struct ieee80211_rx_class a, b;

	decode_both(&null, &a, &b);
	check_same(&a, &b);
	CHECK(!ieee80211_rx_class_reorder(&a));

	decode_both(&open, &a, &b);
	check_same(&a, &b);
	CHECK(ieee80211_rx_class_key_slot(&a) == IEEE80211_RX_KEY_NONE);
	CHECK(!ieee80211_rx_class_ccmp_ok(&a));
}

static void test_mgmt(void)
{
	struct frame f = { FC_MGMT_ACTION | FC_PROTECTED, 0x0070, 0, false, 0, 3 };
	u8 buf[128];
	struct ieee80211_rx_class a;

	CHECK(ieee80211_rx_decode_80211(buf, build_80211(buf, &f), true, &a) == 0);
	CHECK(a.mgmt && !a.qos);
	CHECK(ieee80211_rx_class_security_idx(&a) == IEEE80211_RX_CLASS_TIDS);
	CHECK(ieee80211_rx_class_seqno_idx(&a) == IEEE80211_RX_CLASS_TIDS);
}

/*
 * The reorder timer releases buffered frames into the rx handlers, which
 * classify each one again: the result must equal the classification the
 * frame had on arrival, in both formats.
 */
static void test_reorder_release(void)
{
	struct frame f = { FC_QOS_DATA | FC_PROTECTED, 0x0a50, 4, false, 0, 555 };
	struct ieee80211_rx_class arrive8, arrive3, release8, release3;

	decode_both(&f, &arrive8, &arrive3);
	decode_both(&f, &release8, &release3);
	CHECK(!memcmp(&arrive8, &release8, sizeof(arrive8)));
	CHECK(!memcmp(&arrive3, &release3, sizeof(arrive3)));
	check_same(&release8, &release3);
	CHECK(ieee80211_rx_class_security_idx(&release8) == 4);
}

/* the Order bit adds HT Control only to QoS data and management frames */
static void test_order_bit(void)
{
	struct frame f = { FC_DATA | FC_PROTECTED | 0x8000, 0x0010, 0, false, 1, 0x42 };
	struct frame q = { FC_QOS_DATA | FC_PROTECTED | 0x8000, 0x0010, 2, false, 1, 0x43 };
	u8 buf[128];
	struct ieee80211_rx_class a;
	unsigned int len;

	/* non-QoS: no HT Control, the CCMP header follows the 24-byte header */
	len = build_80211(buf, &f);
	CHECK(ieee80211_rx_decode_80211(buf, len, true, &a) == 0);
	CHECK(a.keyidx == 1 && a.pn[5] == 0x42);

	/* QoS: HT Control after the QoS field moves the CCMP header by 4 */
	len = build_80211(buf, &q);
	memmove(buf + 30, buf + 26, len - 26);
	memset(buf + 26, 0, 4);
	CHECK(ieee80211_rx_decode_80211(buf, len + 4, true, &a) == 0);
	CHECK(a.keyidx == 1 && a.pn[5] == 0x43);
}

/*
 * Block Ack start, both sides of the first in-window frame: frames in
 * flight below the starting sequence number pass through until it
 * arrives; after it, frames behind the window are dropped.
 */
static void test_window_start(void)
{
	bool started = false;
	u16 ssn = 100;

	/* in flight before the session: pass through, still not started */
	CHECK(ieee80211_rx_window(97, ssn, &started) == IEEE80211_RX_WINDOW_PASS);
	CHECK(ieee80211_rx_window(99, ssn, &started) == IEEE80211_RX_WINDOW_PASS);
	CHECK(!started);

	/* the first frame at the starting sequence number starts the window */
	CHECK(ieee80211_rx_window(100, ssn, &started) == IEEE80211_RX_WINDOW_REORDER);
	CHECK(started);

	/* now a late frame behind the head is dropped */
	CHECK(ieee80211_rx_window(98, ssn + 1, &started) == IEEE80211_RX_WINDOW_DROP);
	CHECK(ieee80211_rx_window(101, ssn + 1, &started) == IEEE80211_RX_WINDOW_REORDER);

	/* a first frame past the start also starts it */
	started = false;
	CHECK(ieee80211_rx_window(105, ssn, &started) == IEEE80211_RX_WINDOW_REORDER);
	CHECK(started);

	/* modulo 4096: 4095 is behind a window headed at 2 */
	started = true;
	CHECK(ieee80211_rx_window(4095, 2, &started) == IEEE80211_RX_WINDOW_DROP);
	started = false;
	CHECK(ieee80211_rx_window(4095, 2, &started) == IEEE80211_RX_WINDOW_PASS);
}

static void test_short(void)
{
	u8 buf[24] = { 0x88, 0x42 };
	struct ieee80211_rx_class a;

	CHECK(ieee80211_rx_decode_80211(buf, 20, true, &a) != 0);
	/* protected QoS data with no room for the CCMP header */
	CHECK(ieee80211_rx_decode_80211(buf, 24, true, &a) != 0);
	CHECK(ieee80211_rx_decode_8023(buf, 13, FC_DATA, 0, 0, buf, true, &a) != 0);
}

/* an EAPOL (0x888e) data frame: header, LLC/SNAP, EtherType */
static unsigned int build_eapol(u8 *buf, u16 fc, bool a4)
{
	static const u8 snap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
	unsigned int hdrlen = 24;

	memset(buf, 0, 64);
	put_le16(buf, fc | (a4 ? 0x0300 : FC_FROMDS));
	if (a4)
		hdrlen += 6;
	if (fc & 0x0080) {
		hdrlen += 2;
		if (fc & 0x8000)
			hdrlen += 4;
	}
	memcpy(buf + hdrlen, snap, 6);
	buf[hdrlen + 6] = 0x88;
	buf[hdrlen + 7] = 0x8e;
	return hdrlen + 8;
}

static void test_port_control(void)
{
	u8 buf[64];
	unsigned int len;

	len = build_eapol(buf, FC_DATA, false);
	CHECK(ieee80211_rx_class_port_control(buf, len, 0x888e));
	CHECK(!ieee80211_rx_class_port_control(buf, len, 0x0800));
	/* the EtherType must be inside the frame */
	CHECK(!ieee80211_rx_class_port_control(buf, len - 1, 0x888e));

	len = build_eapol(buf, FC_QOS_DATA, false);
	CHECK(ieee80211_rx_class_port_control(buf, len, 0x888e));
	/* QoS data with HT Control: four more header bytes */
	len = build_eapol(buf, FC_QOS_DATA | 0x8000, false);
	CHECK(ieee80211_rx_class_port_control(buf, len, 0x888e));
	/* four-address header */
	len = build_eapol(buf, FC_QOS_DATA, true);
	CHECK(ieee80211_rx_class_port_control(buf, len, 0x888e));

	/* QoS null and management frames carry no EAPOL */
	len = build_eapol(buf, FC_QOS_NULL, false);
	CHECK(!ieee80211_rx_class_port_control(buf, len, 0x888e));
	len = build_eapol(buf, FC_MGMT_ACTION, false);
	CHECK(!ieee80211_rx_class_port_control(buf, len, 0x888e));
	CHECK(!ieee80211_rx_class_port_control(buf, 20, 0x888e));
}

static void test_deauth_for_us(void)
{
	static const u8 other[6] = { 0x02, 0x99, 0x99, 0x99, 0x99, 0x99 };

	CHECK(ieee80211_rx_deauth_for_us(bssid, bssid, bssid, NULL, NULL));
	CHECK(ieee80211_rx_deauth_for_us(bssid, bssid, NULL, bssid, NULL));
	CHECK(ieee80211_rx_deauth_for_us(bssid, bssid, NULL, NULL, bssid));
	/* another BSS, or not from the BSS itself */
	CHECK(!ieee80211_rx_deauth_for_us(other, other, bssid, NULL, NULL));
	CHECK(!ieee80211_rx_deauth_for_us(bssid, other, bssid, NULL, NULL));
	/* nothing associated or in progress */
	CHECK(!ieee80211_rx_deauth_for_us(bssid, bssid, NULL, NULL, NULL));
}

int main(void)
{
	test_unicast_qos();
	test_unicast_non_qos();
	test_multicast_qos();
	test_multicast_non_qos();
	test_qos_null_and_open();
	test_mgmt();
	test_reorder_release();
	test_order_bit();
	test_window_start();
	test_short();
	test_port_control();
	test_deauth_for_us();
	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("all rx_class tests passed\n");
	return 0;
}
