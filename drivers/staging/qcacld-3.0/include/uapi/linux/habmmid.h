#ifndef HABMMID_H
#define HABMMID_H

#define HAB_MMID_CREATE(major, minor) ((major&0xFFFF) | ((minor&0xFF)<<16))

#define MM_AUD_START	100
#define MM_AUD_1	101
#define MM_AUD_2	102
#define MM_AUD_3	103
#define MM_AUD_4	104
#define MM_AUD_END	105

#define MM_CAM_START	200
#define MM_CAM_1		201
#define MM_CAM_2        202
#define MM_CAM_END	    203

#define MM_DISP_START	300
#define MM_DISP_1	301
#define MM_DISP_2	302
#define MM_DISP_3	303
#define MM_DISP_4	304
#define MM_DISP_5	305
#define MM_DISP_END	306

#define MM_GFX_START	400
#define MM_GFX		401
#define MM_GFX_END	402

#define MM_VID_START	500
#define MM_VID		501
#define MM_VID_2	502
#define MM_VID_END	503

#define MM_MISC_START	600
#define MM_MISC		601
#define MM_MISC_END	602

#define MM_QCPE_START	700
#define MM_QCPE_VM1	701
#define MM_QCPE_END	702

#define	MM_CLK_START	800
#define	MM_CLK_VM1 801
#define	MM_CLK_VM2 802
#define	MM_CLK_END 803

#define	MM_FDE_START  900
#define	MM_FDE_1 901
#define	MM_FDE_END 902

#define	MM_BUFFERQ_START  1000
#define	MM_BUFFERQ_1 1001
#define	MM_BUFFERQ_END 1002

#define	MM_DATA_START 1100
#define	MM_DATA_NETWORK_1 1101
#define	MM_DATA_NETWORK_2 1102
#define	MM_DATA_END 1103

#define	MM_HSI2S_START 1200
#define	MM_HSI2S_1 1201
#define	MM_HSI2S_END 1202

#define	MM_ID_MAX	1203

#endif /* HABMMID_H */
