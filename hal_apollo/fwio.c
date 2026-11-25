/*
 * Firmware I/O code for mac80211 altobeam APOLLO drivers
 * *
 * Copyright (c) 2016, altobeam
 * Author:
 *
 * Based on apollo code
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * Based on:
 * ST-Ericsson UMAC CW1200 driver which is
 * Copyright (c) 2010, ST-Ericsson
 * Author: Ajitpal Singh <ajitpal.singh@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/debugfs.h>

#include "apollo.h"
#include "fwio.h"
#include "hwio.h"
#include "sbus.h"
#include "debug.h"
#include "bh.h"
#include "dcxo_dpll.h"

#ifdef ATBM_USE_SAVED_FW
#pragma message("Suspend Save Firmware")
#endif
#ifdef CONFIG_USE_FW_H
#pragma message("Use Firmware.h")

#else

static char *fw = FIRMWARE_DEFAULT_PATH;//thingino: one .bin per build via CONFIG_FW_NAME

char firmware_bin[][36]={
#ifdef USB_BUS
		{"ATBM606x_fw_usb.bin"},
		{"ATBM606x_blecomb_usb.bin"},
		{"ATBM606x_blecomb_cert_usb.bin"},
#else
		{"ATBM606x_fw_sdio.bin"},
		{"ATBM606x_blecomb_sdio.bin"},
		{"ATBM606x_blecomb_cert_sdio.bin"}
#endif
};

char firmware_bin_oceanus[][36]={
#ifdef USB_BUS
		{"ATBM606x_fw_usb_ocea.bin"},
		{"ATBM606x_blecomb_usb_ocea.bin"},
		{"ATBM606x_blecomb_cert_usb_ocea.bin"},
#else
		{"ATBM606x_fw_sdio_ocea.bin"},
		{"ATBM606x_blecomb_sdio_ocea.bin"},
		{"ATBM606x_blecomb_cert_sdio_ocea.bin"}
#endif
};


char firmware_bin_cronuslite[][36]={
#ifdef USB_BUS
		{"ATBM606x_fw_usb_clite.bin"},
		{"ATBM606x_blecomb_usb_clite.bin"},
		{"ATBM606x_blecomb_cert_usb_clite.bin"},
#else
		{"ATBM606x_fw_sdio_clite.bin"},
		{"ATBM606x_blecomb_sdio_clite.bin"},
		{"ATBM606x_blecomb_cert_sdio_clite.bin"}
#endif
};


#pragma message(FIRMWARE_DEFAULT_PATH)
#endif




static struct firmware_altobeam atbm_fw;

//extern void atbm_wifi_bt_comb_set(int status);

void atbm_release_firmware(void)
{
	if(atbm_fw.fw_dccm)
	{
		vfree(atbm_fw.fw_dccm);
		atbm_fw.fw_dccm = NULL;
	}
	if(atbm_fw.fw_iccm)
	{
		vfree(atbm_fw.fw_iccm);
		atbm_fw.fw_iccm = NULL;
	}

		if(atbm_fw.fw_sram)
		{
			vfree(atbm_fw.fw_sram);
			atbm_fw.fw_sram = NULL;
		}

}
int atbm_init_firmware(void)
{
	memset(&atbm_fw,0,sizeof(struct firmware_altobeam));
	return 0;
}

static int atbm_set_firmare(struct firmware_altobeam *fw)
{
#ifdef ATBM_USE_SAVED_FW
	if(!fw || (!fw->fw_dccm&&!fw->fw_iccm))
	{
		atbm_printk_err(KERN_ERR "fw is err\n");
		return -1;
	}

	if(atbm_fw.fw_dccm || atbm_fw.fw_iccm)
	{
		atbm_printk_err("atbm_fw has been set\n");
		return -1;
	}
	atbm_fw.fw_sram = NULL;
	memcpy(&atbm_fw.hdr,&fw->hdr,sizeof(struct firmware_headr));
	
	if(atbm_fw.hdr.iccm_len)
	{
		atbm_fw.fw_iccm = vmalloc(atbm_fw.hdr.iccm_len);
		atbm_printk_err("%s:fw_iccm(%p)\n",__func__,atbm_fw.fw_iccm);
		if(!atbm_fw.fw_iccm)
		{
			atbm_printk_err("alloc atbm_fw.fw_iccm err\n");
			goto err;
		}
		memcpy(atbm_fw.fw_iccm,fw->fw_iccm,atbm_fw.hdr.iccm_len);
	}

	if(atbm_fw.hdr.dccm_len)
	{
		atbm_fw.fw_dccm= vmalloc(atbm_fw.hdr.dccm_len);
		
		atbm_printk_err("%s:fw_dccm(%p)\n",__func__,atbm_fw.fw_dccm);
		if(!atbm_fw.fw_dccm)
		{
			atbm_printk_err("alloc atbm_fw.fw_dccm err\n");
			goto err;
		}
		memcpy(atbm_fw.fw_dccm,fw->fw_dccm,atbm_fw.hdr.dccm_len);
	}


		if(atbm_fw.hdr.sram_len){
			atbm_fw.fw_sram = vmalloc(atbm_fw.hdr.sram_len);
			
			atbm_printk_err("%s:fw_sram(%p)\n",__func__,atbm_fw.fw_sram);
			if(!atbm_fw.fw_sram)
			{
				atbm_printk_err("alloc atbm_fw.fw_sram err\n");
				goto err;
			}
			memcpy(atbm_fw.fw_sram,fw->fw_sram,atbm_fw.hdr.sram_len);
		}
	


	return 0;
err:
	if(atbm_fw.fw_iccm)
	{
		vfree(atbm_fw.fw_iccm);
		atbm_fw.fw_iccm = NULL;
	}

	if(atbm_fw.fw_dccm)
	{
		vfree(atbm_fw.fw_dccm);
		atbm_fw.fw_dccm = NULL;
	}
	

		if(atbm_fw.fw_sram)
		{
			vfree(atbm_fw.fw_sram);
			atbm_fw.fw_sram = NULL;
		}


#endif //#ifndef USB_BUS
	return -1;
}

#define FW_IS_READY	((atbm_fw.fw_dccm != NULL) || (atbm_fw.fw_iccm != NULL))
static int atbm_get_fw(struct firmware_altobeam *fw)
{
	if(!FW_IS_READY)
	{
		return -1;
	}

	memcpy(&fw->hdr,&atbm_fw.hdr,sizeof(struct firmware_headr));
	fw->fw_iccm = atbm_fw.fw_iccm;
	fw->fw_dccm = atbm_fw.fw_dccm;
		fw->fw_sram = atbm_fw.fw_sram;
	return 0;
}

static int atbm_load_firmware_generic(struct atbm_common *priv, u8 *data,u32 size,u32 addr)
{

	int ret=0;
	u32 put = 0;
	u8 *buf = NULL;


	buf = atbm_kmalloc(DOWNLOAD_BLOCK_SIZE*2, GFP_KERNEL | GFP_DMA);
	if (!buf) {
		atbm_dbg(ATBM_APOLLO_DBG_ERROR,
			"%s: can't allocate bootloader buffer.\n", __func__);
		ret = -ENOMEM;
		goto error;
	}
	
#ifndef HW_DOWN_FW
	if(priv->sbus_ops->bootloader_debug_config)
		priv->sbus_ops->bootloader_debug_config(priv->sbus_priv,0);
#endif //#ifndef HW_DOWN_FW

	/*  downloading loop */
	atbm_printk_init( "%s: addr %x: len %x\n",__func__,addr,size);
	for (put = 0; put < size ;put += DOWNLOAD_BLOCK_SIZE) {
		u32 tx_size;


		/* calculate the block size */
		tx_size  = min((size - put),(u32)DOWNLOAD_BLOCK_SIZE);

		memcpy(buf, &data[put], tx_size);

		/* send the block to sram */
		ret = atbm_fw_write(priv,put+addr,buf, tx_size);
		if (ret < 0) {
			atbm_dbg(ATBM_APOLLO_DBG_ERROR,
				"%s: can't write block at line %d.\n",
				__func__, __LINE__);
			goto error;
		}
	} /* End of bootloader download loop */

error:
	atbm_kfree(buf);
	return ret;


}
void  atbm_efuse_read_byte(struct atbm_common *priv,u32 byteIndex, u32 *value)
{
	//HW_WRITE_REG(0x16b00000, (byteIndex<<8));
	//*value = HW_READ_REG(0x16b00004);	
	if(priv->sbus_ops && priv->sbus_ops->lock)
		priv->sbus_ops->lock(priv->sbus_priv);
	atbm_direct_write_reg_32(priv,0x16b00000, (byteIndex<<8));
	atbm_direct_read_reg_32(priv,0x16b00004,value);
	if(priv->sbus_ops && priv->sbus_ops->unlock)
		priv->sbus_ops->unlock(priv->sbus_priv);
}

static u32 atbm_efuse_read_bit(struct atbm_common *priv,u32 bitIndex)
{
	u32	efuseBitIndex = bitIndex;
	u32 byteIndex;
	u32 value = 0;

	{
		byteIndex = efuseBitIndex / 8;
		atbm_efuse_read_byte(priv,byteIndex, &value);
	}
	value = value >> (efuseBitIndex % 8);
	value &= 0x1;
	return value;
}
static int atbm_check_chip_config(struct atbm_common *hw_priv)
{
//	hw_priv->loader_ble = 1;
	hw_priv->loader_ble = 0;

	switch(hw_priv->chip_version)
	{

		case CRONUS:
			if(atbm_efuse_read_bit(hw_priv,14) == 1){
				if(atbm_wifi_bt_comb_get() != 0)
					hw_priv->loader_ble = 1;
				
					
				if(atbm_efuse_read_bit(hw_priv,15) == 1){
					atbm_printk_always("not support ldpc & ht40!\n");
					hw_priv->chip_version = CRONUS_NO_HT40_LDPC;
				}else{
					atbm_printk_always("not support ht40!\n");
					hw_priv->chip_version = CRONUS_NO_HT40;
				}
			}else if (atbm_efuse_read_bit(hw_priv,13) == 1){
				atbm_printk_always("not support BLE!\n");
				hw_priv->chip_version = CRONUS_NO_BLE;
				hw_priv->loader_ble = 0;
			}else{
				if(atbm_wifi_bt_comb_get() != 0)
					hw_priv->loader_ble = 1;
			}
			break;

		case OCEANUS:
			if(atbm_efuse_read_bit(hw_priv,14) == 1){
				if(atbm_wifi_bt_comb_get() != 0)
					hw_priv->loader_ble = 1;
				
					
				if(atbm_efuse_read_bit(hw_priv,15) == 1){
					atbm_printk_always("not support ldpc & ht40!\n");
					hw_priv->chip_version = OCEANUS_NO_HT40_LDPC;
				}else{
					atbm_printk_always("not support ht40!\n");
					hw_priv->chip_version = OCEANUS_NO_HT40;
				}
			}else if (atbm_efuse_read_bit(hw_priv,13) == 1){
				atbm_printk_always("not support BLE!\n");
				hw_priv->chip_version = OCEANUS_NO_BLE;
				hw_priv->loader_ble = 0;
			}else{
				if(atbm_wifi_bt_comb_get() != 0)
					hw_priv->loader_ble = 1;
			}

			if(atbm_efuse_read_bit(hw_priv,16) == 1){
				hw_priv->chip_version = OCEANUS_NO_WIFI6;
				atbm_printk_always("not support wifi6\n");
			}
			
			if(atbm_efuse_read_bit(hw_priv,17) == 1)
				hw_priv->mi_ver = 1;

			break;
		case CRONUS_LITE:
			hw_priv->chip_version = CRONUS_LITE;
			/*if(atbm_efuse_read_bit(hw_priv,14) == 1){
				if(atbm_wifi_bt_comb_get() != 0)
					hw_priv->loader_ble = 1;
				
					
				if(atbm_efuse_read_bit(hw_priv,15) == 1){
					atbm_printk_always("not support ldpc & ht40!\n");
					hw_priv->chip_version = CRONUS_LITE_NO_HT40_LDPC;
				}else{
					atbm_printk_always("not support ht40!\n");
					hw_priv->chip_version = CRONUS_LITE_NO_HT40;
				}
			}else if (atbm_efuse_read_bit(hw_priv,13) == 1){
				atbm_printk_always("not support BLE!\n");
				hw_priv->chip_version = CRONUS_LITE_NO_BLE;
				hw_priv->loader_ble = 0;
			}else*/{
				if(atbm_wifi_bt_comb_get() != 0)
					hw_priv->loader_ble = 1;
			}

			if(atbm_efuse_read_bit(hw_priv,17) == 1)
				hw_priv->mi_ver = 1;

			break;
		default:
			atbm_printk_always("atbm_check_chip_config check fail!\n");

	}

#ifdef CONFIG_ATBM_ONLY_COMB_FIRMWARE
		if(hw_priv->chip_version != CRONUS_NO_BLE || hw_priv->chip_version != OCEANUS_NO_BLE ||hw_priv->chip_version != CRONUS_LITE_NO_BLE){
			hw_priv->loader_ble = 1;
		}
#endif	
//#endif 
	return 0;
}

/*
 * Returns -ENODEV for a part older than Cronus (Apollo, Athena, Ares,
 * Hera), which this driver does not support.
 */
int atbm_get_chiptype(struct atbm_common *hw_priv)
{
	u32 chipver = 0;

	atbm_direct_read_reg_32(hw_priv,0x0acc017c,&chipver);
    chipver&=0xff;
	hw_priv->chip_id = chipver;
	hw_priv->chip_version = ARES_B;
	switch(chipver)
	{
		case 0x14:	
			hw_priv->chip_version = APOLLO_F;	
			break;
		case 0x24:	
		case 0x25:	
			//strHwChipFw = ("AthenaB.bin");
			hw_priv->chip_version = ATHENA_B;
			break;
		case 0x45:	
		case 0x46:	
		case 0x47:	
			hw_priv->chip_version = ARES_A;
			break;	
		case 0x49:
			hw_priv->chip_version = ARES_B;	
			break;
		case 0x64:
		case 0x65:
			hw_priv->chip_version = HERA;		
			break;
		case 0xb0:
		case 0xb1:
		case 0xb2:
			hw_priv->chip_version = RHEA;
			break;
		case 0x30:
			hw_priv->chip_version = OCEANUS;
			hw_priv->chip_cat = OCEANUS_CATEGORY;
			atbm_check_chip_config(hw_priv);
			break;
		case 0x5c:
			hw_priv->chip_version = CRONUS_LITE;
			hw_priv->chip_cat = CLITE_CATEGORY;
			atbm_check_chip_config(hw_priv);
			break;
			
		default:
			hw_priv->chip_version = CRONUS;
			hw_priv->chip_cat = CRONUS_CATEGORY;
			//g_wifi_chip_type = ATHENA_B;
			atbm_check_chip_config(hw_priv);
		break;
	}

	atbm_printk_always("%s, chipver=0x%x, g_wifi_chip_type[%d]\n",__func__, chipver,hw_priv->chip_version );
	if (hw_priv->chip_version < CRONUS) {
		atbm_printk_err("chip id 0x%x is not a supported part\n", chipver);
		return -ENODEV;
	}
	return 0;
}

#ifndef  CONFIG_USE_FW_H
static char * atbm_HwGetChipFw(struct atbm_common *priv)
{

	char * strHwChipFw = NULL;


#if 0 /* thingino: fw name comes from CONFIG_FW_NAME (one .bin per build); chip is auto-detected at runtime, so the per-chip name table is unused */
	switch(priv->chip_version)
	{

	case CRONUS:
	case CRONUS_NO_HT40:
	case CRONUS_NO_HT40_LDPC:	
	case CRONUS_NO_BLE:
		fw=firmware_bin[atbm_wifi_bt_comb_get()];		
		break;

	case OCEANUS:
	case OCEANUS_NO_WIFI6:
		fw=firmware_bin_oceanus[atbm_wifi_bt_comb_get()];
		break;

	case CRONUS_LITE:
	case CRONUS_LITE_NO_HT40:
	case CRONUS_LITE_NO_HT40_LDPC:
	case CRONUS_LITE_NO_BLE:
		fw=firmware_bin_cronuslite[atbm_wifi_bt_comb_get()];
		break;
	default:
		atbm_printk_always("atbm_HwGetChipFw looks check fail!\n");
	}
#endif


	if(fw)
	{
		atbm_printk_always("fw [%s]\n", fw );
	 	return fw;
	}

	return strHwChipFw;
}
#endif

//#define TEST_DCXO_CONFIG move to makefile
#ifndef CONFIG_USE_FW_H
#define USED_FW_FILE
#endif
#ifdef USED_FW_FILE
/*check if fw headr ok*/
static int atbm_fw_checksum(struct firmware_headr * hdr)
{
	return 1;
}


#endif //USED_FW_FILE
#ifdef CONFIG_PM_SLEEP
#pragma message("CONFIG_PM_SLEEP")
int atbm_cache_fw_before_suspend(struct device	 *pdev)
{
#if defined (USED_FW_FILE) && defined(ATBM_USE_SAVED_FW)
	int ret = 0;
	const char *fw_path= fw;
	const struct firmware *firmware = NULL;
	struct firmware_altobeam fw_altobeam;

	memset(&fw_altobeam,0,sizeof(struct firmware_altobeam));
	if(fw_path == NULL){
		goto error2;
	}
	if(FW_IS_READY){
		atbm_printk_err("atbm_fw ready\n");
		goto error2;
	}

	ret = request_firmware(&firmware, fw_path, pdev);
	if(ret){
		atbm_printk_err("request_firmware err\n");
		goto error2;
	}
	if((*(int*)firmware->data == ALTOBEAM_WIFI_HDR_FLAG) || (*(int*)firmware->data == ALTOBEAM_WIFI_HDR_FLAG_V2)){
		memcpy(&fw_altobeam.hdr,firmware->data,sizeof(struct firmware_headr));
		if(atbm_fw_checksum(&fw_altobeam.hdr)==0){
			ret = -1;
			 atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: atbm_fw_checksum fail 11\n", __func__);
			 goto error1;
		}
		fw_altobeam.fw_iccm = (u8 *)firmware->data + sizeof(struct firmware_headr);
		fw_altobeam.fw_dccm = fw_altobeam.fw_iccm + fw_altobeam.hdr.iccm_len;
		if(fw_altobeam.hdr.sram_len){
			fw_altobeam.fw_sram = fw_altobeam.fw_dccm + fw_altobeam.hdr.dccm_len;
		}
		atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: have header,lmac version(%d) iccm_len(%d) dccm_len(%d)\n", __func__,
			fw_altobeam.hdr.version,fw_altobeam.hdr.iccm_len,fw_altobeam.hdr.dccm_len);
	}
	else {
		atbm_printk_err("\n<<ERROR>>firmware version not support [%d]<<<======\n\n",*(int*)firmware->data);
		frame_hexdump("data ",firmware->data,64);
		BUG_ON(1);

	}
	atbm_release_firmware();
	
	memcpy(&atbm_fw.hdr,&fw_altobeam.hdr,sizeof(struct firmware_headr));
	if(atbm_fw.hdr.iccm_len)
	{
		atbm_fw.fw_iccm = vmalloc(atbm_fw.hdr.iccm_len);
		
		if(!atbm_fw.fw_iccm)
		{
			atbm_printk_err( "alloc atbm_fw.fw_iccm err\n");
			goto error1;
		}
		memcpy(atbm_fw.fw_iccm,fw_altobeam.fw_iccm,atbm_fw.hdr.iccm_len);
	}

	if(atbm_fw.hdr.dccm_len)
	{
		atbm_fw.fw_dccm = vmalloc(atbm_fw.hdr.dccm_len);

		if(!atbm_fw.fw_dccm)
		{
			atbm_printk_err("alloc atbm_fw.fw_dccm err\n");
			goto error1;
		}
		memcpy(atbm_fw.fw_dccm,fw_altobeam.fw_dccm,atbm_fw.hdr.dccm_len);
	}
	
		if(atbm_fw.hdr.sram_len){
			atbm_fw.fw_sram = vmalloc(atbm_fw.hdr.sram_len);
			if(!atbm_fw.fw_sram)
			{
				atbm_printk_err("alloc atbm_fw.fw_dccm err\n");
				goto error1;
			}
			memcpy(atbm_fw.fw_sram,fw_altobeam.fw_sram,atbm_fw.hdr.sram_len);		
		}

	
	atbm_printk_always("%s:cached fw\n",__func__);
	release_firmware(firmware);
	return 0;
error1:
	
	atbm_printk_err("%s:error1\n",__func__);
	release_firmware(firmware);
	if(atbm_fw.fw_iccm)
	{
		vfree(atbm_fw.fw_iccm);
		atbm_fw.fw_iccm = NULL;
	}

	if(atbm_fw.fw_dccm)
	{
		vfree(atbm_fw.fw_dccm);
		atbm_fw.fw_dccm = NULL;
	}

		if(atbm_fw.fw_sram)
		{
			vfree(atbm_fw.fw_sram);
			atbm_fw.fw_sram = NULL;		
		}
error2:
	atbm_printk_err("%s:error2\n",__func__);
	return ret;
#else
	return 0;
#endif//
}
#endif
/* the firmware image, obtained and checked before the chip is touched */
struct atbm_fw_image {
	const struct firmware *file;
	struct firmware_altobeam alt;
};

/*
 * Get the firmware image and check it: the file (or the built-in image),
 * its header flag, and that the ICCM, DCCM and SRAM images fit in it.
 * Touches nothing on the chip.
 */
static int atbm_fw_image_get(struct atbm_common *priv, struct atbm_fw_image *img)
{
	struct firmware_altobeam *fw_altobeam = &img->alt;
#ifdef USED_FW_FILE
	const char *fw_path = atbm_HwGetChipFw(priv);
	const struct firmware *firmware = NULL;
	size_t avail;
	int ret;
#endif

	memset(img, 0, sizeof(*img));
	fw_altobeam->chip_catogery = priv->chip_cat;
	if (FW_IS_READY)
		return atbm_get_fw(fw_altobeam);

#ifdef USED_FW_FILE
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"%s:FW FILE = %s\n",__func__,fw_path);
	ret = request_firmware(&firmware, fw_path, priv->pdev);
	if (ret) {
		atbm_printk_err("%s: can't load firmware file %s (%d)\n",
				__func__, fw_path, ret);
		return ret;
	}
	ret = -EINVAL;
	if (firmware->size < sizeof(struct firmware_headr)) {
		atbm_printk_err("%s: %s is too short (%zu)\n", __func__,
				fw_path, firmware->size);
		goto bad_fw;
	}
	if ((*(int*)firmware->data != ALTOBEAM_WIFI_HDR_FLAG) &&
	    (*(int*)firmware->data != ALTOBEAM_WIFI_HDR_FLAG_V2)) {
		atbm_printk_err("\n<<ERROR>>firmware version not support [%d]<<<======\n\n",*(int*)firmware->data);
		goto bad_fw;
	}
	avail = firmware->size - sizeof(struct firmware_headr);
	memcpy(&fw_altobeam->hdr,firmware->data,sizeof(struct firmware_headr));
	if (atbm_fw_checksum(&fw_altobeam->hdr) == 0) {
		atbm_printk_err("%s: atbm_fw_checksum fail\n", __func__);
		goto bad_fw;
	}
	/* The three images follow the header back to back. */
	if (fw_altobeam->hdr.iccm_len > avail ||
	    fw_altobeam->hdr.dccm_len > avail - fw_altobeam->hdr.iccm_len ||
	    fw_altobeam->hdr.sram_len > avail - fw_altobeam->hdr.iccm_len -
				       fw_altobeam->hdr.dccm_len) {
		atbm_printk_err("%s: %s: iccm %u dccm %u sram %u exceed %zu bytes\n",
				__func__, fw_path,
				fw_altobeam->hdr.iccm_len,
				fw_altobeam->hdr.dccm_len,
				fw_altobeam->hdr.sram_len, avail);
		goto bad_fw;
	}
	fw_altobeam->fw_iccm = (u8 *)firmware->data + sizeof(struct firmware_headr);
	fw_altobeam->fw_dccm = fw_altobeam->fw_iccm + fw_altobeam->hdr.iccm_len;
	if (fw_altobeam->hdr.sram_len) {
		fw_altobeam->fw_sram = fw_altobeam->fw_dccm + fw_altobeam->hdr.dccm_len;
		if (*(int*)firmware->data == ALTOBEAM_WIFI_HDR_FLAG)
			fw_altobeam->hdr.sram_addr = DOWNLOAD_BLE_SRAM_ADDR;
	}
	atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: have header,lmac version(%d) iccm_len(%d) dccm_len(%d),sram_len(%d),fwsize(%zu),hdrsize(%zu),cat(%d)\n",
			__func__,
			fw_altobeam->hdr.version,
			fw_altobeam->hdr.iccm_len,
			fw_altobeam->hdr.dccm_len,
			fw_altobeam->hdr.sram_len,
			firmware->size,
			sizeof(struct firmware_headr),
			fw_altobeam->chip_catogery);
	img->file = firmware;
	atbm_set_firmare(fw_altobeam);
	return 0;
bad_fw:
	release_firmware(firmware);
	return ret;
#else //USED_FW_FILE
#ifdef ATBM_ADD_CRERT_FIRMWARE
	if(atbm_wifi_bt_comb_get() == 2){
		atbm_load_wifi_bt_cert_firmware(fw_altobeam);
		atbm_printk_err("\n======>>> load WIFI BLE COMB CERT firmware <<<======\n\n");
	}
	else
#endif
	{
		atbm_printk_err("\n======>>> load WIFI%s firmware <<<======\n\n",atbm_wifi_bt_comb_get()?"+BLE":"");
		atbm_load_wifi_bt_firmware(fw_altobeam);
	}
	atbm_set_firmare(fw_altobeam);
	return 0;
#endif //USED_FW_FILE
}

static void atbm_fw_image_put(struct atbm_fw_image *img)
{
	if (img->file)
		release_firmware(img->file);
	img->file = NULL;
}

#ifdef CONFIG_ATBM_TEST_RESTART
/* test builds: fail the next firmware download (test_restart=3) */
int atbm_test_fail_download;
#endif

static int atbm_start_load_firmware(struct atbm_common *priv,
				    struct firmware_altobeam *fw_altobeam)
{
	int ret;

#ifdef CONFIG_ATBM_TEST_RESTART
	if (xchg(&atbm_test_fail_download, 0)) {
		atbm_printk_err("test_restart: failing this firmware download\n");
		return -EIO;
	}
#endif
	atbm_dbg(ATBM_APOLLO_DBG_ERROR,"START DOWNLOAD ICCM=========\n");
	ret = atbm_load_firmware_generic(priv,fw_altobeam->fw_iccm,fw_altobeam->hdr.iccm_len,DOWNLOAD_ITCM_ADDR);
	if(ret<0)
		return ret;

	if (fw_altobeam->hdr.dccm_len > 0xd000)
		fw_altobeam->hdr.dccm_len = 0xd000;

	atbm_dbg(ATBM_APOLLO_DBG_ERROR,"START DOWNLOAD DCCM=========\n");
	ret = atbm_load_firmware_generic(priv,fw_altobeam->fw_dccm,fw_altobeam->hdr.dccm_len,DOWNLOAD_DTCM_ADDR);
	if(ret<0)
		return ret;

	if(fw_altobeam->hdr.sram_len){
		atbm_dbg(ATBM_APOLLO_DBG_ERROR,"START DOWNLOAD SRAM=========\n");
		ret = atbm_load_firmware_generic(priv, fw_altobeam->fw_sram, fw_altobeam->hdr.sram_len, fw_altobeam->hdr.sram_addr);
		if(ret<0)
			return ret;
	}

	atbm_dbg(ATBM_APOLLO_DBG_MSG, "FIRMWARE DOWNLOAD SUCCESS\n");
	return 0;
}

/*
 * Load the firmware. The image is obtained and checked first; only then,
 * with reset_first (probe), is the chip reset, immediately before the
 * download. *touched, if given, says whether the chip was touched, so a
 * caller knows whether a failure left the chip reset.
 */
int atbm_load_firmware(struct atbm_common *hw_priv, bool reset_first, bool *touched)
{
	struct atbm_fw_image img;
	int ret;

	if (touched)
		*touched = false;
	ret = atbm_get_chiptype(hw_priv);
	if (ret)
		return ret;
	ret = atbm_fw_image_get(hw_priv, &img);
	if (ret)
		return ret;

	if (touched)
		*touched = true;
	if (reset_first && hw_priv->sbus_ops->pre_download_reset)
		hw_priv->sbus_ops->pre_download_reset(hw_priv->sbus_priv);

	atbm_printk_err("firmware download: start\n");
	ret = atbm_before_load_firmware(hw_priv);
	if(ret <0)
		goto out;
	atbm_printk_init("atbm_start_load_firmware++\n");
	ret = atbm_start_load_firmware(hw_priv, &img.alt);
	if(ret <0)
		goto out;
	atbm_printk_init("atbm_after_load_firmware++\n");
	ret = atbm_after_load_firmware(hw_priv);
	if(ret <0)
		goto out;
	ret =0;
	atbm_printk_err("firmware download: done\n");
out:
	if (ret)
		atbm_printk_err("firmware download: failed (%d)\n", ret);
	atbm_fw_image_put(&img);
	return ret;
}

