#ifndef __WEXT_CFG_H__
#define __WEXT_CFG_H__

/*
start return  1
stop return 0
*/
int ETF_Test_is_Start(void);

struct atbm_common;
struct ieee80211_vif;
struct sk_buff;
void etf_v2_scan_end(struct atbm_common *hw_priv, struct ieee80211_vif *vif);
void etf_v2_scan_rx(struct atbm_common *hw_priv, struct sk_buff *skb, u8 rssi);
void etf_param_init(struct atbm_common *hw_priv);


#endif /*__WEXT_CFG_H__*/

