/*
 * Copyright 2018-, altobeam.inc
 */

#include <net/atbm_mac80211.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/bitmap.h>
#include <linux/inetdevice.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#ifdef IPV6_FILTERING
#include <net/if_inet6.h>
#include <net/addrconf.h>
#endif /*IPV6_FILTERING*/

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wep.h"
#include "cfg.h"
#include "debugfs.h"
#include "../sbus.h"
#include "atbm_common.h"
#include "../apollo.h"
#include "../smartconfig.h"
#include "../wsm.h"
#include "../internal_cmd.h"
#include "../sta.h"
#include "rc80211_hmac.h"
#include "../dev_ioctl.h"
#include "twt.h"
#ifdef CONFIG_ATBM_SUPPORT_BLUEZ
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#endif


//#define CONFIG_JUAN_MISC

#define ATBM_WEXT_PROCESS_PARAMS(_start_pos,_length,_val,_process,_err_code,_exit_lable,_status)		\
do{													\
	char const *pos_next = NULL;					\
	pos_next = memchr(_start_pos,ATBM_SPACE,_length);	\
	if(pos_next == NULL) pos_next = memchr(_start_pos,ATBM_TAIL,_length);	\
	if(_process(_start_pos,pos_next - _start_pos,&_val) == _err_code){	\
		atbm_printk_err("%s:line(%d) err\n",__func__,__LINE__);			\
		_status = -EINVAL;											\
		goto _exit_lable;												\
	}															\
	pos_next++;													\
	_length -= (pos_next-_start_pos);										\
	_start_pos = pos_next;												\
}while(0)

#define FREQ_CNT	(10)

#define TARGET_FREQOFFSET_HZ  (7000)



#define HW_CHIP_VERSION_AthenaB (0x24)
#define HW_CHIP_VERSION_AresB (0x49)
#define N_BIT_TO_SIGNED_32BIT(v,n)	(s32)(((v) & BIT(n-1))?((v)|0xffffffff<<n):(v))
#define SIGNED_32BIT_TO_N_BIT(g,n) (g &= (BIT(n)-1))






struct rxstatus{
	u32 GainImb;
	u32 PhaseImb;
	u32 Cfo;
	u32 evm;
	u32  RSSI;
	u32 probcnt;
};

typedef struct PAS_MONITOR {
	u32 rxSuccessUnicastDataFrameCnt[2];
	u32 rxSuccessUnicastCtrlFrameCnt[2];
	u32 rxSuccessUnicastMgmFrameCnt[2];
	u32 rxSuccessMulticastMgmFrameCnt;
	u32 rxSuccessMulticastDataFrameCnt;
	u32 rxSuccessBeaconCnt;
	u32 rxSuccessOtherMgmFrameCnt;
	u32 rxSuccessOtherDataFrameCnt;
	u32 rxSuccessUnicastOther;
	u32 rxSuccessMulticastOther;
	u32 rxSuccessUnicastDataFrameByte[2];
	u32 rxU32NRGAINIMB;
	u32 rxU32NRPHASEIMB;
    u16          Cfo;
    u16			evm;
	u8           Rssi;
	u8           Rcpi;
	short           atbm_probCnt;
	//u32 rxSuccessCnt[2];
}PAS_MONITOR ;


u32 chipversion = 0;

u8 ETF_bStartTx = 0;
u8 ETF_bStartRx = 0;
char ch_and_type[20] = {0};
int g_RxMode = 0;


u8 CodeStart = 0;
u8 CodeEnd = 0;

u8 ucWriteEfuseFlag = 0;

int Atbm_Test_Success = 0;
int atbm_test_rx_cnt = 0;
int txevm_total = 0;

struct efuse_headr efuse_data_etf;
struct rxstatus_signed gRxs_s;
struct etf_test_config etf_config;
extern int g_CertSRRCFlag;
extern int rate_txpower_cfg[RATE_CFG_TXPOWER_ARRY_MAX];
extern int rate_txpower_cfg_5g[RATE_CFG_TXPOWER_ARRY_MAX];

extern const s8 MCS_LUT_20M[OFDM_20M_INDEX_MAX];
extern u8 MCS_LUT_20M_Modify[OFDM_20M_INDEX_MAX]; 
extern s8 MCS_LUT_20M_delta_mode[OFDM_20M_INDEX_MAX]; 						
extern s8 MCS_LUT_20M_delta_bw[OFDM_20M_INDEX_MAX];

extern const s8 MCS_LUT_40M[OFDM_40M_INDEX_MAX]; 
extern s8 MCS_LUT_40M_Modify[OFDM_40M_INDEX_MAX];
extern s8 MCS_LUT_40M_delta_mode[OFDM_40M_INDEX_MAX]; 
extern s8 MCS_LUT_40M_delta_bw[OFDM_40M_INDEX_MAX]; 
							
extern const char *rate_name[]; 

//-----------cronus
extern const s8 MCS_LUT_20M_Cronus[CRONUS_OFDM_INDEX_MAX];
extern u8 MCS_LUT_20M_Modify_Cronus[CRONUS_OFDM_INDEX_MAX]; 
extern s8 MCS_LUT_20M_delta_mode_Cronus[CRONUS_OFDM_INDEX_MAX]; 						
extern s8 MCS_LUT_20M_delta_bw_Cronus[CRONUS_OFDM_INDEX_MAX];

extern const s8 MCS_LUT_40M_Cronus[CRONUS_OFDM_INDEX_MAX]; 
extern s8 MCS_LUT_40M_Modify_Cronus[CRONUS_OFDM_INDEX_MAX];
extern s8 MCS_LUT_40M_delta_mode_Cronus[CRONUS_OFDM_INDEX_MAX]; 
extern s8 MCS_LUT_40M_delta_bw_Cronus[CRONUS_OFDM_INDEX_MAX]; 
							
extern const char *rate_name_Cronus[]; 


extern int WiFiMode;
extern int OFDMMode;
extern int ChBW;
extern int RateIndex;
extern int g_PacketInterval;
extern int g_DutyCycle;
extern u16 g_ch_idle_ratio;
extern int atbm_Get_MCS_LUT_Offset_Index(int WiFiMode,int OFDMMode,int ChBW,int RateIndex);
extern u8 atbm_DCXOCodeRead(struct atbm_common *hw_priv);
extern int atbm_DCXOCodeWrite(struct atbm_common *hw_priv,int data);
extern unsigned int HW_READ_REG_BIT(unsigned int addr,int endbit,int startbit);
extern void HW_WRITE_REG_BIT(unsigned int addr,unsigned int endBit,unsigned int startBit,unsigned int data );
extern int wsm_start_tx_v2(struct atbm_common *hw_priv, struct ieee80211_vif *vif );
extern int atbm_get_cfo_allow_flag(void);

#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
extern int wsm_send_result(struct atbm_common *hw_priv, struct ieee80211_vif *vif );
#endif
extern u32 atbm_get_chip_crystal_type(struct atbm_common *hw_priv);
#define RATE_CONTROL_UNICAST 1

#define	arraysize(a)	sizeof(a)/sizeof(a[0])
extern int atbm_direct_write_reg_32(struct atbm_common *hw_priv, u32 addr, u32 val);
extern int atbm_direct_read_reg_32(struct atbm_common *hw_priv, u32 addr, u32 *val);
extern int atbm_usb_write_bit(struct atbm_common *hw_priv,u32 addr,u8 endBit,u8 startBit,u32 data );






//config etf test arguments by config_param.txt
static int etf_rx_status_get(struct atbm_common *hw_priv)
{
	int ret = 0;
	int i = 0;
	struct rxstatus rxs; 
	char *extra = NULL;
	struct atbm_vif *vif;

	atbm_printk_err("[%s]:%d\n", __func__, __LINE__);
	if(!(extra = atbm_kmalloc(sizeof(struct rxstatus), GFP_KERNEL)))
	{
		atbm_printk_err("%s:malloc failed\n", __func__);
		return -ENOMEM;	
	}

	atbm_for_each_vif(hw_priv,vif,i){
		if (vif != NULL)
		{
			ret = wsm_read_mib(hw_priv, WSM_MIB_ID_GET_ETF_RX_STATS,
				extra, sizeof(struct rxstatus), vif->if_id);
			break;
		}
	}
	memcpy(&rxs, extra, sizeof(struct rxstatus));

	if(rxs.probcnt == 0)
	{
		atbm_printk_err("%s:rxs.probcnt == 0\n", __func__);
		goto out;
	}

	if(ret == 0)
	{
		gRxs_s.evm				= rxs.evm/rxs.probcnt;
		gRxs_s.RxRSSI			= (s16)N_BIT_TO_SIGNED_32BIT(rxs.RSSI, 8)*4;
		gRxs_s.RxRSSI += etf_config.cableloss;	
	}
	else
	{
		atbm_printk_err("%s:get rx status failed\n", __func__);
	}
out:
	if(extra)
		atbm_kfree(extra);
	return ret;

}

#ifdef CONFIG_ATBM_IWPRIV_USELESS
#endif

/*
return value:
	0: success.
	1:freq offset not ok yet,need next test
	2:dcxo cal max
	3.dcxo cal min
*/
static int Test_FreqOffset_v2(struct atbm_common *hw_priv, u32 *dcxo, int *pfreqErrorHz)
{
	u8 CodeValue,CodeValuebak;
	int b_fail =1;
	int freqErrorHz = 0;
	int targetFreqOffset = TARGET_FREQOFFSET_HZ;
	static int first_cal = 0;

	if(etf_config.freq_ppm != 0)
		targetFreqOffset = etf_config.freq_ppm;

	if((etf_config.default_dcxo != 0)
		&& (etf_config.default_dcxo >= 0)
		&& (etf_config.default_dcxo <= 127)
		&& (first_cal == 0))
	{
		first_cal = 1;
		atbm_printk_debug("wirte default dcxo when calibration firstly\n");
		CodeValue = etf_config.default_dcxo;
		atbm_DCXOCodeWrite(hw_priv,CodeValue);	
	}
	else
	{
		CodeValue = atbm_DCXOCodeRead(hw_priv);	
		atbm_DCXOCodeWrite(hw_priv,CodeValue);	
	}


	atbm_printk_always("CodeValue default:%d\n",CodeValue);

	
	CodeValuebak = CodeValue;

	freqErrorHz = gRxs_s.Cfo;

	//no freqOffset Calibration,
	if(etf_config.noFfreqCaliFalg == 1)
	{
		atbm_printk_always("targetFreqOffset[%d~%d]Hz\n", -targetFreqOffset,targetFreqOffset);
		if ((freqErrorHz > targetFreqOffset) || (freqErrorHz < -targetFreqOffset))
		{
			atbm_printk_always("fail freqError[%d]Hz dcxo[%d]!\n",freqErrorHz,CodeValue);
			b_fail = 2;
		}
		else
		{
			atbm_printk_always("pass freqError[%d]Hz dcxo[%d]!\n",freqErrorHz,CodeValue);
			b_fail = 0;
		}
		*dcxo = CodeValue;
		return b_fail;
	}

	if (freqErrorHz > targetFreqOffset)
	{
		CodeStart = CodeValue;
		CodeValue += (CodeEnd - CodeStart)/2;
		CodeStart = CodeValuebak;

		atbm_printk_always("freqErrorHz[%d] > targetFreqOffset[%d],CodeValue[%d] ,CodeStart[%d], CodeEnd[%d] . \n",
			freqErrorHz,targetFreqOffset,	CodeValue, CodeStart ,CodeEnd );
		
		atbm_DCXOCodeWrite(hw_priv,CodeValue);

		b_fail = 1;
		if (CodeValue >= 126)
		{
			b_fail = 2;
		}
		if (CodeValue >= 0xff)
		{
			b_fail = 2;
		}
	}
	else if ((int)freqErrorHz < -targetFreqOffset)
	{
		CodeEnd = CodeValue;
		CodeValue -= (CodeEnd - CodeStart)/2;
		CodeEnd = CodeValuebak;

		atbm_printk_always("freqErrorHz[%d] < targetFreqOffset[%d],CodeValue[%d] ,CodeStart[%d], CodeEnd[%d] . \n",
			freqErrorHz,targetFreqOffset,	CodeValue, CodeStart ,CodeEnd );
		atbm_DCXOCodeWrite(hw_priv,CodeValue);

		b_fail = 1;
		
		if (CodeValue <= 2)
		{
			b_fail = 3;
		}
		if (0x01 == CodeEnd)
		{
			b_fail = 3;
		}
	}
	else
	{
		first_cal = 0;
		atbm_printk_always("[dcxo PASS]freqErrorKHz[%d] CodeValue[%d]!\n",freqErrorHz/1000,CodeValue);
		b_fail = 0;
		*dcxo = CodeValue;
		*pfreqErrorHz = freqErrorHz;
	}

	if((CodeEnd == CodeStart) ||
		((CodeEnd - CodeStart) == 1) ||
		((CodeEnd - CodeStart) == -1))
	{
		atbm_printk_always("CodeValue[%d] ,CodeStart[%d], CodeEnd[%d] . \n",CodeValue, CodeStart ,CodeEnd);
		b_fail = 2;
	}

	
	return b_fail;

}


static int atbm_freqoffset_save_efuse(struct atbm_common *hw_priv,struct rxstatus_signed rxs_s,u32 dcxo)
{
	int ret = 0;
	int iResult=0;
	//struct atbm_vif *vif;
	struct efuse_headr efuse_d,efuse_bak;
	
	
	//u8 buff[512];

	memset(&efuse_d,0,sizeof(struct efuse_headr));
	memset(&efuse_bak,0,sizeof(struct efuse_headr));

	

	//tmp = atbm_DCXOCodeRead(hw_priv);printk("tmp %d\n"tmp);	
	if(ucWriteEfuseFlag)
	{
		atbm_printk_always("ucWriteEfuseFlag :%d\n",ucWriteEfuseFlag);
		wsm_get_efuse_data(hw_priv,(void *)&efuse_d,sizeof(struct efuse_headr));

		if(efuse_d.version == 0)
		{
			//The first time efuse is written,all the data should be written, 
			//The production test only modifies part of the value, so efuse cannot be written.
			iResult = -3;
			goto FEEQ_ERR;
		}

		if(efuse_d.dcxo_trim == dcxo) // old dcxo equal new dcxo, no need to write efuse.
		{
			atbm_printk_always(" old dcxo equal new dcxo, no need to write efuse.\n");
			iResult = 0;
			goto FEEQ_ERR;
		}
		efuse_d.dcxo_trim = dcxo;
		//write amc address
		if(etf_config.writemacflag == 1){
			memcpy(efuse_d.mac, etf_config.writemac, 6);
		}
		/*
		*LMC_STATUS_CODE__EFUSE_VERSION_CHANGE	failed because efuse version change  
		*LMC_STATUS_CODE__EFUSE_FIRST_WRITE, 		failed because efuse by first write   
		*LMC_STATUS_CODE__EFUSE_PARSE_FAILED,		failed because efuse data wrong, cannot be parase
		*LMC_STATUS_CODE__EFUSE_FULL,				failed because efuse have be writen full
		*/
		ret = wsm_efuse_change_data_cmd(hw_priv, &efuse_d,0);
		if (ret == LMC_STATUS_CODE__EFUSE_FIRST_WRITE)
		{
			iResult = -3;
		}else if (ret == LMC_STATUS_CODE__EFUSE_PARSE_FAILED)
		{
			iResult = -4;
		}else if (ret == LMC_STATUS_CODE__EFUSE_FULL)
		{
			iResult = -5;
		}else if (ret == LMC_STATUS_CODE__EFUSE_VERSION_CHANGE)
		{
			iResult = -6;
		}else
		{
			iResult = 0;
		}
		frame_hexdump("efuse_d", (u8 *)&efuse_d, sizeof(struct efuse_headr));
		wsm_get_efuse_data(hw_priv,(void *)&efuse_bak, sizeof(struct efuse_headr));
		frame_hexdump("efuse_bak", (u8 *)&efuse_bak, sizeof(struct efuse_headr));
		
		if(memcmp((void *)&efuse_bak,(void *)&efuse_d, sizeof(struct efuse_headr)) !=0)
		{
			iResult = -2;
		}else
		{
			iResult = 0;
		}
		
	}

	
FEEQ_ERR:	
	
	/*sprintf(buff, "cfo:%d,evm:%d,gainImb:%d, phaseImb:%d,dcxo:%d,result:%d (0:OK; -1:FreqOffset Error; -2:efuse hard error;"
		" -3:efuse no written; -4:efuse anaysis failed; -5:efuse full; -6:efuse version change)",
	rxs_s.Cfo,
	rxs_s.evm,
	rxs_s.GainImb,
	rxs_s.PhaseImb,
	dcxo,
	iResult
	);*/

	//if((ret = copy_to_user(wrqu->data.pointer, buff, strlen(buff))) != 0){
	//	return -EINVAL;
	//}

	return iResult;
}


/**************************************************************************
**
** NAME         LMC_FM_GetATBMIe
**
** PARAMETERS:  pElements  -> Pointer to the Ie list
**              Length     -> Size of the Ie List
**              
** RETURNS:     Pointer to element if found or 0 otherwise.
**
** DESCRIPTION  Searches for ATBM test element  from a given IE list.
** 
**************************************************************************/
static u8* LMC_FM_GetATBMIe(u8 *pElements,u16 Length)
{
  u8     ATBMIeOui[3]   = ATBM_OUI	;
  
  struct ATBM_TEST_IE  *Atbm_Ie;
	//dump_mem(pElements,Length);

   if(Length > sizeof(struct ATBM_TEST_IE)){
		pElements += Length-sizeof(struct ATBM_TEST_IE);
		Atbm_Ie =(struct ATBM_TEST_IE  *) pElements;
		/*
		DBG_Printf("Atbm_Ie->oui_type %x,Atbm_Ie->oui %x %x,size %x\n",
			Atbm_Ie->oui_type,
			Atbm_Ie->oui[2],
			ATBMIeOui[2],
			sizeof(struct ATBM_TEST_IE));
		
		dump_mem(pElements,16);*/

		 if(pElements[0]== D11_WIFI_ELT_ID){
			 if((memcmp(Atbm_Ie->oui,ATBMIeOui,3)==0)&&
			 	(Atbm_Ie->oui_type== WIFI_ATBM_IE_OUI_TYPE) ){
				return pElements;
			}
		 }
   }

  return (u8 *)NULL  ;
}//end LMC_FM_GetP2PIe()

static int etf_v2_compare_test_result(void)
{	
	if((etf_config.txpwrmax == 0) && (etf_config.txpwrmin == 0))
	{
		etf_config.txpwrmax = 65536;
		if((efuse_data_etf.specific & 0x1))//outerPA(6038)
		{



			etf_config.txpwrmin = -60+etf_config.cableloss;	
		}
		else
		{
			etf_config.txpwrmin = -84+etf_config.cableloss;	
		}
		atbm_printk_wext("Use default Txrssimin threshold[-84+120]:%d\n", etf_config.txpwrmin);
	}

	if((etf_config.txevmthreshold != 0) && (gRxs_s.txevm > etf_config.txevmthreshold))
	{
		atbm_printk_err("Test txevm:%d > threshold txevm:%d\n", gRxs_s.txevm, etf_config.txevmthreshold);
		return 1;
	}

	if((etf_config.rxevmthreshold != 0) && (gRxs_s.evm > etf_config.rxevmthreshold))
	{
		atbm_printk_err("Test rxevm:%d > threshold rxevm:%d\n", gRxs_s.evm, etf_config.rxevmthreshold);
		return 2;
	}
	if((etf_config.txpwrmax != 0) && (etf_config.txpwrmin != 0) &&
		((gRxs_s.TxRSSI > etf_config.txpwrmax) ||
		(gRxs_s.TxRSSI < etf_config.txpwrmin)))
	{
		atbm_printk_err("Test txpower:%d,txpowermax:%d, txpowermin:%d\n",
			gRxs_s.TxRSSI, etf_config.txpwrmax, etf_config.txpwrmin);
		return 3;
	}
	if((etf_config.rxpwrmax != 0) && (etf_config.rxpwrmin!= 0) &&
		((gRxs_s.RxRSSI > etf_config.rxpwrmax) ||
		(gRxs_s.RxRSSI < etf_config.rxpwrmin)))
	{
		atbm_printk_err("Test rxpower:%d,rxpowermax:%d, rxpowermin:%d\n",
			gRxs_s.RxRSSI, etf_config.rxpwrmax, etf_config.rxpwrmin);
		return 4;
	}

	return 0;
}

void etf_v2_scan_end(struct atbm_common *hw_priv, struct ieee80211_vif *vif )
{
	int ret = 0;
	int result = 0;//(0:OK; -1:FreqOffset Error; -2:Write efuse Failed;-3:efuse not write;-4:rx fail)
	u32 dcxo = 0;
	int freqErrorHz;
	int ErrCode = -1;

	if(hw_priv->etf_test_v2 == 0)
	{
		atbm_test_rx_cnt = 0;
	}
	else
	{
		ret = etf_rx_status_get(hw_priv);
		if(ret != 0)
		{
			atbm_test_rx_cnt = 0;
			atbm_printk_wext("%s:etf_rx_status_get failed,ret:%d\n", __func__, ret);
		}
	}
	msleep(10);

	if(atbm_test_rx_cnt <= 5){
		memset(&gRxs_s, 0, sizeof(struct rxstatus_signed));
		//up(&hw_priv->scan.lock);
		//hw_priv->etf_test_v2 = 0;
		atbm_printk_always("etf rx data[%d] less than 5 packet\n",atbm_test_rx_cnt);
		ErrCode = -7;
		goto Error;
	}
	
	gRxs_s.TxRSSI += etf_config.cableloss;
	gRxs_s.txevm = txevm_total/atbm_test_rx_cnt;
	
	atbm_printk_always("Average: Cfo:%d,TxRSSI:%d,RxRSSI:%d,txevm:%d,rxevm:%d\n",	
	gRxs_s.Cfo,
	gRxs_s.TxRSSI,
	gRxs_s.RxRSSI,
	gRxs_s.txevm,
	gRxs_s.evm
	);
	
	if(etf_config.chip_crystal_type != 2)
	{
		if(etf_config.freq_ppm != 0)
			result = Test_FreqOffset_v2(hw_priv,&dcxo,&freqErrorHz);
		else
		{
			dcxo = efuse_data_etf.dcxo_trim;
			atbm_printk_always("Not need to Calibrate FreqOffset\n");
			result = 0;
			goto success;
		}
	}
	else
	{
		dcxo = efuse_data_etf.dcxo_trim;
		atbm_printk_always("Crystal,Not need to Calibrate FreqOffset\n");
		result = 0;
		goto success;
	}
	if(result == 1)
	{
		//start next scan
		atbm_printk_always("start next scan\n");

		//mutex_lock(&hw_priv->conf_mutex);
		//wsm_stop_tx(hw_priv);
		//mutex_unlock(&hw_priv->conf_mutex);

		msleep(100);
		txevm_total = 0;
		atbm_test_rx_cnt = 0;
		wsm_start_tx_v2(hw_priv,vif);
	}
	else  if(result == 0)  //etf dcxo success
	{
success:
		if((ErrCode = etf_v2_compare_test_result()) != 0)
			goto Error;
		atbm_printk_always("etf test success \n");
		gRxs_s.result = atbm_freqoffset_save_efuse(hw_priv,gRxs_s,dcxo);

		gRxs_s.dcxo = dcxo;
		gRxs_s.valid = 1;
		up(&hw_priv->scan.lock);
		hw_priv->etf_test_v2 = 0;
		//del_timer_sync(&hw_priv->etf_expire_timer);
#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
		Atbm_Test_Success = 1;
		wsm_send_result(hw_priv,vif);
#endif
		
	}else
	{
Error:
		gRxs_s.result = ErrCode;
		gRxs_s.dcxo = dcxo;
		gRxs_s.valid = 1;
		atbm_printk_always("etf test Fail \n");
		up(&hw_priv->scan.lock);
		hw_priv->etf_test_v2 = 0;
		//del_timer_sync(&hw_priv->etf_expire_timer);
#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
		Atbm_Test_Success = -1;
		wsm_send_result(hw_priv,vif);
#endif

	}
	atbm_test_rx_cnt = 0;
	txevm_total = 0;
	//ETF_bStartTx = 0;
}

void etf_v2_scan_rx(struct atbm_common *hw_priv,struct sk_buff *skb,u8 rssi )
{

	s32 Cfo = 0;
	s32  RSSI = 0;
	s32 tmp = 0;
	s16 txevm  = 0;
	struct ATBM_TEST_IE  *Atbm_Ie = NULL;
	u8 *data = (u8 *)skb->data + offsetof(struct atbm_ieee80211_mgmt, u.probe_resp.variable);
	int len = skb->len - offsetof(struct atbm_ieee80211_mgmt, u.probe_resp.variable);
	Atbm_Ie = (struct ATBM_TEST_IE  *)LMC_FM_GetATBMIe(data,len);
	
	if((Atbm_Ie)
#ifdef ATBM_PRODUCT_TEST_USE_FEATURE_ID
		&& (Atbm_Ie->featureid == etf_config.featureid)
#endif
		)
	{
		tmp				= Atbm_Ie->result[1];
		tmp				= (s32)N_BIT_TO_SIGNED_32BIT(tmp, 16);
		if(Atbm_Ie->resverd & BIT(0))
			Cfo = (s32)(((tmp*12207)/160));//6431 as golden
		else
			Cfo = (s32)(((tmp*12207)/10));	//6421 as golden 
		txevm				= (s16)N_BIT_TO_SIGNED_32BIT(Atbm_Ie->result[2], 16);
		RSSI			= (s16)N_BIT_TO_SIGNED_32BIT(Atbm_Ie->result[3], 10);
		
		if( RSSI < etf_config.rssifilter)
		{
			atbm_printk_always("[%d]: Cfo:%d,TxRSSI:%d,rssifilter[%d] rx dump packet,throw......\n",
			atbm_test_rx_cnt,	
			Cfo,
			RSSI,
			etf_config.rssifilter
			);
			return;
		}

		if(txevm < etf_config.txevm)
		{
			if(atbm_test_rx_cnt == 0)
			{		
				gRxs_s.Cfo = Cfo;
				//gRxs_s.evm = evm;
				gRxs_s.TxRSSI = RSSI;
			}else
			{

				gRxs_s.Cfo = (gRxs_s.Cfo*3 + Cfo )/4;
				//gRxs_s.evm = evm;
				gRxs_s.TxRSSI = RSSI;
				//gRxs_s.TxRSSI = (gRxs_s.TxRSSI*3*10 + RSSI*10 +5)/40;

			}

			atbm_printk_always("[%d]: Cfo1:%d, Cfo:%d,TxRSSI:%d,txevm:%d\n",
			atbm_test_rx_cnt,
			tmp,
			Cfo,
			RSSI,txevm
			);

			//printk("etf_v2_scan_rx %d,cnt %d,[0x%x,0x%x,0x%x,0x%x,0x%x]\n",Atbm_Ie->test_type,atbm_test_rx_cnt,
			//	Atbm_Ie->result[0],Atbm_Ie->result[1],Atbm_Ie->result[2],Atbm_Ie->result[3],Atbm_Ie->result[3]);
			txevm_total += txevm;
			atbm_test_rx_cnt++;
		}
		
	}

}

/*
start return  1
stop return 0
*/
int ETF_Test_is_Start(void){
	if(ETF_bStartTx || ETF_bStartRx)
		return 1;
	return 0;
}

void ETF_Test_Init(void){
	ETF_bStartTx = 0;
	ETF_bStartRx = 0;
}

void etf_param_init(struct atbm_common *hw_priv)
{
	atbm_test_rx_cnt = 0;
	txevm_total = 0;

	ucWriteEfuseFlag = 0;

	CodeStart = DCXO_CODE_MINI;
	CodeEnd = DCXO_CODE_MAX;

	memset(&etf_config, 0, sizeof(struct etf_test_config));
	memset(&gRxs_s, 0, sizeof(struct rxstatus_signed));

	chipversion = atbm_get_chip_version(hw_priv);
	atbm_printk_wext("chipversion:0x%x\n", chipversion);
}


