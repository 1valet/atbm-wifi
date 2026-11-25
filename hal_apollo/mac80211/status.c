/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2008-2010	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <net/atbm_mac80211.h>
#include "ieee80211_i.h"
#include "rate.h"
#include "mesh.h"
#include "wme.h"

//EXPORT_SYMBOL(ieee80211_tx_status_irqsafe);
static void ieee80211_frame_acked(struct sta_info *sta, struct sk_buff *skb)
{
#if defined (CONFIG_ATBM_SMPS)
	struct atbm_ieee80211_mgmt *mgmt = (void *) skb->data;
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	
	if (ieee80211_is_action(mgmt->frame_control) &&
	    sdata->vif.type == NL80211_IFTYPE_STATION &&
	    mgmt->u.action.category == ATBM_WLAN_CATEGORY_HT &&
	    mgmt->u.action.u.ht_smps.action == WLAN_HT_ACTION_SMPS) {
		/*
		 * This update looks racy, but isn't -- if we come
		 * here we've definitely got a station that we're
		 * talking to, and on a managed interface that can
		 * only be the AP. And the only other place updating
		 * this variable is before we're associated.
		 */
		switch (mgmt->u.action.u.ht_smps.smps_control) {
		case WLAN_HT_SMPS_CONTROL_DYNAMIC:
			sta->sdata->u.mgd.ap_smps = IEEE80211_SMPS_DYNAMIC;
			break;
		case WLAN_HT_SMPS_CONTROL_STATIC:
			sta->sdata->u.mgd.ap_smps = IEEE80211_SMPS_STATIC;
			break;
		case WLAN_HT_SMPS_CONTROL_DISABLED:
		default: /* shouldn't happen since we don't send that */
			sta->sdata->u.mgd.ap_smps = IEEE80211_SMPS_OFF;
			break;
		}

		ieee80211_queue_work(&local->hw, &local->recalc_smps);
	}
#endif
}



static int ieee80211_tx_radiotap_len(struct ieee80211_tx_info *info)
{
	int len = sizeof(struct ieee80211_radiotap_header);
	/* IEEE80211_RADIOTAP_TX_FLAGS */
	len += 2;

	/* IEEE80211_RADIOTAP_DATA_RETRIES */
	len += 1;
	return len;
}

static void ieee80211_add_tx_radiotap_header(struct ieee80211_supported_band
					     *sband, struct sk_buff *skb,
					     int retry_count, int rtap_len)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_radiotap_header *rthdr;
	unsigned char *pos;
	u16 txflags;

	rthdr = (struct ieee80211_radiotap_header *) atbm_skb_push(skb, rtap_len);

	memset(rthdr, 0, rtap_len);
	rthdr->it_len = cpu_to_le16(rtap_len);
	rthdr->it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_TX_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_DATA_RETRIES));
	pos = (unsigned char *)(rthdr + 1);

	/*
	 * XXX: Once radiotap gets the bitmap reset thing the vendor
	 *	extensions proposal contains, we can actually report
	 *	the whole set of tries we did.
	 */
	/* IEEE80211_RADIOTAP_TX_FLAGS */
	txflags = 0;
	if (!(info->flags & IEEE80211_TX_STAT_ACK) &&
	    !is_multicast_ether_addr(hdr->addr1))
		txflags |= IEEE80211_RADIOTAP_F_TX_FAIL;

	put_unaligned_le16(txflags, pos);
	pos += 2;

	/* IEEE80211_RADIOTAP_DATA_RETRIES */
	/* for now report the total retry_count */
	*pos = retry_count;
	pos++;
}

/*
 * Use a static threshold for now, best value to be determined
 * by testing ...
 * Should it depend on:
 *  - on # of retransmissions
 *  - current throughput (higher value for higher tpt)?
 */
#define STA_LOST_PKT_THRESHOLD	50
static void ieee80211_tx_8023_status(struct ieee80211_local* local,struct sk_buff* skb)
{
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	struct sta_info* sta;

	if(info->control.sta){
		sta = container_of(info->control.sta, struct sta_info, sta);
		if(info->flags & IEEE80211_TX_STATUS_EOSP){
			clear_sta_flag(sta, WLAN_STA_SP);
		}
	}
	
	atbm_dev_kfree_skb(skb);
}

/*
 * TX status for frames user space sends through mgmt_tx, as upstream
 * mac80211 does it: ieee80211_attach_ack_skb() keeps a copy of the frame
 * with cfg80211's cookie under an ID, and the frame carries the ID in
 * its tx info. The status path, a drop (ieee80211_free_txskb()) or an
 * interface flush retires the ID once and reports the copy.
 */
struct ieee80211_ack_skb_cb {
	u64 cookie;
};

int ieee80211_attach_ack_skb(struct ieee80211_local *local,
			     struct sk_buff *skb, u64 cookie)
{
	struct sk_buff *ack_skb;
	unsigned long flags;
	int id;

	ack_skb = atbm_skb_copy(skb, GFP_ATOMIC);
	if (!ack_skb)
		return -ENOMEM;
	((struct ieee80211_ack_skb_cb *)ack_skb->cb)->cookie = cookie;

	spin_lock_irqsave(&local->ack_status_lock, flags);
	id = idr_alloc(&local->ack_status_frames, ack_skb, 1, 0x100, GFP_ATOMIC);
	spin_unlock_irqrestore(&local->ack_status_lock, flags);
	if (id < 0) {
		atbm_kfree_skb(ack_skb);
		return -EBUSY;
	}

	IEEE80211_SKB_CB(skb)->ack_frame_id = id;
	return 0;
}

static void ieee80211_report_ack_frame(struct sk_buff *ack_skb, bool acked)
{
	if (ack_skb->dev && ack_skb->dev->ieee80211_ptr)
		cfg80211_mgmt_tx_status(ack_skb->dev->ieee80211_ptr,
			((struct ieee80211_ack_skb_cb *)ack_skb->cb)->cookie,
			ack_skb->data, ack_skb->len, acked, GFP_ATOMIC);
	atbm_kfree_skb(ack_skb);
}

/* retire the frame's ID, if it still has one, and report its status */
void ieee80211_report_ack_skb(struct ieee80211_local *local,
			      struct ieee80211_tx_info *info, bool acked)
{
	struct sk_buff *ack_skb;
	unsigned long flags;

	if (!(info->flags & IEEE80211_TX_INTFL_NL80211_FRAME_TX) ||
	    !info->ack_frame_id)
		return;

	spin_lock_irqsave(&local->ack_status_lock, flags);
	ack_skb = idr_remove(&local->ack_status_frames, info->ack_frame_id);
	spin_unlock_irqrestore(&local->ack_status_lock, flags);
	info->ack_frame_id = 0;

	if (ack_skb)
		ieee80211_report_ack_frame(ack_skb, acked);
}

/* free a frame that will get no TX status, reporting it not acknowledged */
void ieee80211_free_txskb(struct ieee80211_local *local, struct sk_buff *skb)
{
	ieee80211_report_ack_skb(local, IEEE80211_SKB_CB(skb), false);
	atbm_dev_kfree_skb(skb);
}

/*
 * The interface is down (sdata), or the device is freed (NULL): report
 * its outstanding frames as not acknowledged. A status for one of them
 * arriving later finds its ID retired.
 */
void ieee80211_flush_ack_frames(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata)
{
	struct sk_buff *ack_skb;
	unsigned long flags;
	int id;

	for (;;) {
		spin_lock_irqsave(&local->ack_status_lock, flags);
		idr_for_each_entry(&local->ack_status_frames, ack_skb, id) {
			if (!sdata || ack_skb->dev == sdata->dev)
				break;
		}
		if (ack_skb)
			idr_remove(&local->ack_status_frames, id);
		spin_unlock_irqrestore(&local->ack_status_lock, flags);
		if (!ack_skb)
			break;
		if (sdata)
			ieee80211_report_ack_frame(ack_skb, false);
		else
			atbm_kfree_skb(ack_skb);
	}
}

static void ieee80211_tx_80211_status(struct ieee80211_local* local,struct sk_buff* skb)
{
	struct sk_buff* skb2;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	struct sta_info* sta = NULL;
	struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)skb->data;
	bool acked;
	u16 type;
	bool send_to_cooked;
	struct net_device* prev_dev = NULL;
	int rtap_len;
	struct ieee80211_supported_band* sband;
	struct ieee80211_sub_if_data *sdata;
	
	acked = !!(info->flags & IEEE80211_TX_STAT_ACK);
	
	if(info->control.sta)
		sta = container_of(info->control.sta, struct sta_info, sta);
	
	if (!(info->flags & IEEE80211_TX_CTL_INJECTED) && acked && sta)
		ieee80211_frame_acked(sta, skb);
	
#ifdef ATBM_AP_SME
	if ((info->flags & IEEE80211_TX_AP_HANDLE_STATUS) && sta) {
		if (!ieee80211_ap_sme_tx_mgmt_status(sta->sdata, skb)) {
			rcu_read_unlock();
			return;
		}
	}
#endif
	
	type = le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_FTYPE;
	
	ieee80211_report_ack_skb(local, info,
				 !!(info->flags & IEEE80211_TX_STAT_ACK));

	/* this was a transmitted frame, but now we want to reuse it */
	atbm_skb_orphan(skb);

	/* Need to make a copy before skb->cb gets cleared */
	send_to_cooked = !!(info->flags & IEEE80211_TX_CTL_INJECTED) || (type != IEEE80211_FTYPE_DATA);

	/*
	 * This is a bit racy but we can avoid a lot of work
	 * with this test...
	 */
	if (!local->monitors && (!send_to_cooked || !local->cooked_mntrs)) {
		atbm_dev_kfree_skb(skb);
		return;
	}

//	atbm_printk_err("TX Status[%x][%d][%d]\n", hdr->frame_control, !!(info->flags & IEEE80211_TX_STAT_ACK), skb->len);
	/* send frame to monitor interfaces now */
	rtap_len = ieee80211_tx_radiotap_len(info);
	if (WARN_ON_ONCE(atbm_skb_headroom(skb) < rtap_len)) {
		atbm_dev_kfree_skb(skb);
		return;
	}	
	sband = local->hw.wiphy->bands[info->band];
	ieee80211_add_tx_radiotap_header(sband, skb, 0, rtap_len);

	/* XXX: is this sufficient for BPF? */
	atbm_skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));

	
	list_for_each_entry_rcu(sdata, &local->interfaces, list){
		if (sdata->vif.type == NL80211_IFTYPE_MONITOR) {
			if (!ieee80211_sdata_running(sdata))
				continue;

			if ((sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) &&
				!send_to_cooked)
				continue;

			if (prev_dev) {
				skb2 = atbm_skb_clone(skb, GFP_ATOMIC);
				if (skb2) {
					skb2->dev = prev_dev;
					atbm_netif_rx(skb2);
				}
			}

			prev_dev = sdata->dev;
		}
	}
	if (prev_dev) {
		skb->dev = prev_dev;
		atbm_netif_rx(skb);
		skb = NULL;
	}
	atbm_dev_kfree_skb(skb);
}

void ieee80211_tx_status(struct ieee80211_hw* hw, struct sk_buff* skb)
{
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	
	if(info->flags & IEEE80211_TX_CTL_8023){
		ieee80211_tx_8023_status(hw_to_local(hw), skb);
	}else {
		ieee80211_tx_80211_status(hw_to_local(hw), skb);
	}
}

void ieee80211_sta_rate_status(struct ieee80211_sta* pubsta)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	rate_control_tx_hmac(sta->local,sta);
}
