
#include <string.h>
#include <stdio.h>

#include "main.h"

#include "canmsg.h"
#include "app_btl.h"
#include "version.h"


/* ID di configurazione */
#define CONF_CANID                    0x2001

#define CAN_RX_QUEUE                  20     /* dimensione della coda dei messaggi in ricezione */
#define MSG_ERROR_TX_LIMIT            10     /* messaggi inviati con errori consecutivi */
#define MSG_ERROR_TO                  2      /* errori generici consecutivi non recuperati entro 10*(MSG_ERROR_XX_LIMIT-1)ms */
#define MSG_RE_SEND_MAX               500    /* numero massimo di tentativi di re-invio del msg */

/* periodo messaggi */
#define MSG_PERIOD_MON_INFO           200     /* ms */
#define MSG_PERIOD_MIN                50      /* ms */


#if LOG_ERROR_EN == 0
#  define printf_err(...)
#else
#  define printf_err(...)     printf(__VA_ARGS__)
#endif



/* CAN Rx MSG CFG: OpCode e check */
#define MSG_OPC_CANID_REC             0x0000
#define MSG_OPC_CANID_SEND            0x0001
#define MSG_OPC_CANID_OFFSET          0x0002
#define MSG_OPC_VELOC                 0x0100

#define MSG_OPC_BOOTLOADER            0x1000

#define HW_CHECK_0                    0x1234
#define HW_CHECK_2                    0x5157
#define HW_CHECK_3                    0xDCCD

/* CAN Rx MSG */
#define MSG_CFG_STATUS                0
#define MSG_OUT_ENABLE                1
#define MSG_CNG_VELOC                 2
#define MSG_HW_VER                    3
#define MSG_FW_VER                    4

/* CAN Tx MSG */
#define MSG_MON_INFO                  0


typedef enum {
	CAN_SPEED_1M = 0,
	CAN_SPEED_800K,
	CAN_SPEED_500K,
	CAN_SPEED_250K,
	CAN_SPEED_125K,
	CAN_SPEED_100K,
	CAN_SPEED_50K,
	CAN_SPEED_20K,
	CAN_SPEED_10K,
	CAN_SPEED_NONE
} can_speed;


typedef struct {
	CAN_RxHeaderTypeDef header;
	uint8_t data[8];
} msg_can_rx;


typedef struct {
	CAN_TxHeaderTypeDef header;
	uint8_t data[8];
} msg_can_tx;


typedef struct {
	can_speed speed;            /* velocita' del can bus */

	/* gestione coda rx e invio messaggi */
	uint8_t send_en;            /* abilitazione all'invio. 0: invio in corso; 1: possibili invio nuovo messaggi; 2: re-invio messaggi */
	uint8_t cfg_en;             /* abilitata alla ricezione dei messaggi di configurazione */
	uint8_t error_glb;          /* errori nel can bus dall'ultima inizializzazione o invio/reicezione corretta */
	uint16_t error_tx;          /* errori di invio dei mesaggi (sono compesati ad ogni invio corretto) */
	uint16_t error_tot;         /* errori totali nel can bus */
	uint16_t rx_queue_in;       /* indice alla coda per i messaggi da inserire nella cosa se rx_queue_in == rx_queue_out la coa e' vuota */
	uint16_t rx_queue_out;      /* indice della coda per i messaggi da estrarre dalla coda se rx_queue_in == rx_queue_out la coa e' vuota */
	msg_can_rx rx_msg_queue[CAN_RX_QUEUE]; /* coda messaggi in ricezione */
	msg_can_tx tx_msg;          /* messaggio in invio */

	/* conteggio dati */
	uint32_t tx;                /* messaggi inviati dal nodo */
	uint32_t rx;                /* messaggi ricevuti per il nodo */
	uint32_t tot_rx;            /* messaggi ricevuto anche non destinati al nodo */

	/* ID di comunizazione */
	uint32_t cfg_id;            /* can id di configurazione */
	uint32_t base;              /* can id di base */
	uint32_t rec_offset;        /* (multiplo) offset per i messaggi in ricezione, a partire dall'ID di base */
 	uint32_t base_send;         /* base del ID per i comandi/msg in ricezione */
	uint32_t send_offset;       /* (multiplo) offset per i messaggi in invio, a partire dall'ID di base d'invio */

	/* gestione messaggi periodici */
	uint8_t periodic_en;         /* abilitazione messaggi periodici */
	uint16_t period_mon_info;    /* periodo in ms dell'invio delle info */
	uint8_t period_mon_info_force     :1; /* forza invio dato */
} candev;


extern CAN_HandleTypeDef hcan;
extern volatile uint8_t can_tick_1ms;

static candev can_dev;


static void CanInit(void);

static void CanSpeedInit(can_speed id)
{
	/* check speed */
	if (id >= CAN_SPEED_NONE)
		return;

	/* deinizializzazione */
	HAL_CAN_Stop(&hcan);
	HAL_CAN_DeInit(&hcan);
	__HAL_RCC_CAN1_FORCE_RESET();
	__HAL_RCC_CAN1_RELEASE_RESET();

	/* impostazione velocita' */
	switch (id) { /* http://www.bittiming.can-wiki.info/ */
	case CAN_SPEED_1M:
		hcan.Init.Prescaler = 4;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_6TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
		break;

	case CAN_SPEED_800K:
		hcan.Init.Prescaler = 3;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_12TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
		break;

	case CAN_SPEED_500K:
		hcan.Init.Prescaler = 4;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_14TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_3TQ;
		break;

	case CAN_SPEED_250K:
		hcan.Init.Prescaler = 9;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
		break;

	case CAN_SPEED_125K:
		hcan.Init.Prescaler = 18;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
		break;

	case CAN_SPEED_100K:
		hcan.Init.Prescaler = 24;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_11TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_3TQ;
		break;

	case CAN_SPEED_50K:
		hcan.Init.Prescaler = 45;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
		break;

	case CAN_SPEED_20K:
		hcan.Init.Prescaler = 120;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_12TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
		break;

	case CAN_SPEED_10K:
		hcan.Init.Prescaler = 225;
		hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
		hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
		hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
		break;

	default:
		break;
	}

	/* inizializzazione */
	HAL_CAN_Init(&hcan);
	CanInit();
}


static void CanReInit(void)
{
	CanSpeedInit(can_dev.speed); /* c'e' anche l'inizializzazione */
}


static short CanControlLoop(uint8_t tick_event) /* tick_event indica che sono trascorsi 10ms */
{
	static unsigned char to_cnt = 0;
	uint32_t error, state;
	short ret;

	if (can_dev.error_glb == 0)
		to_cnt = 0;

	if (tick_event) {
		if (can_dev.error_glb)
			to_cnt++;
	}
	ret = 0;
	/* gestione errori */
	error = HAL_CAN_GetError(&hcan);
	state = HAL_CAN_GetState(&hcan);
	if (error != HAL_CAN_ERROR_NONE || state == HAL_CAN_STATE_ERROR || to_cnt == MSG_ERROR_TO) {
		HAL_CAN_Stop(&hcan);
		HAL_CAN_DeInit(&hcan);

		__HAL_RCC_CAN1_FORCE_RESET();
		__HAL_RCC_CAN1_RELEASE_RESET();

	    /* svuota la coda in ingresso al CAN bus */
	    can_dev.rx_queue_in = can_dev.rx_queue_out = 0;

		HAL_CAN_Init(&hcan);
		CanInit();

		if (can_dev.error_tx >= MSG_ERROR_TX_LIMIT) {
			/* disabilitato l'invio del messaggi */
			can_dev.periodic_en = 0;
			ret = -1;
		}
	}

	return ret;
}


static HAL_StatusTypeDef CnMsgFilterList(uint32_t filter_id_0, uint32_t filter_id_1, uint16_t flt_num, uint8_t rtr)
{
	CAN_FilterTypeDef can_filter = {0};
	uint32_t option = 0x04; /* IDE pag 666 "Filter bank scale configuration - register organization" */

	if (rtr)
		option |= 0x02; /* RTR pag 666 "Filter bank scale configuration - register organization" */

	can_filter.FilterIdHigh = filter_id_0 >> 13;
	can_filter.FilterIdLow = ((filter_id_0<<3) & 0x0FFFF) | option; /* EXID */
	can_filter.FilterMaskIdHigh = filter_id_1 >> 13;
	if (filter_id_0 == filter_id_1)
		option |= 0x02;
	can_filter.FilterMaskIdLow = ((filter_id_1<<3) & 0x0FFFF) | option; /* EXID */
	can_filter.FilterMode = CAN_FILTERMODE_IDLIST;
	can_filter.FilterScale = CAN_FILTERSCALE_32BIT;
	can_filter.FilterBank = flt_num;
	can_filter.SlaveStartFilterBank = flt_num;
	can_filter.FilterActivation = ENABLE;
	can_filter.FilterFIFOAssignment = CAN_RX_FIFO0;

	return HAL_CAN_ConfigFilter(&hcan, &can_filter);
}


static void CanInit(void)
{
	uint16_t ret, val_h, val_l;
	CAN_FilterTypeDef can_filter;
	HAL_StatusTypeDef res;

	/* inizializzazione fifo per la ricezione dei messaggio con i vari ID */
	can_dev.cfg_id = CONF_CANID;
	can_dev.base = 0;
	can_dev.base_send = 0;
	/* default offset */
	can_dev.send_offset = 1;
	can_dev.rec_offset = 1;
	/* recupero dati di impostazione */
	ret = EE_ReadVariable(FLASH_ADDR_CANID_H, &val_h);
	if (ret == 0) {
		ret = EE_ReadVariable(FLASH_ADDR_CANID_L, &val_l);
		if (ret == 0) {
			can_dev.base = val_h;
			can_dev.base = (can_dev.base<<16) | val_l;
			can_dev.base_send = can_dev.base;

			ret = EE_ReadVariable(FLASH_ADDR_CANID_SEND_H, &val_h);
			if (ret == 0) {
				ret = EE_ReadVariable(FLASH_ADDR_CANID_SEND_L, &val_l);
				if (ret == 0) {
					can_dev.base_send = val_h;
					can_dev.base_send = (can_dev.base_send<<16) | val_l;
				}
			}

			/* offset */
			ret = EE_ReadVariable(FLASH_ADDR_CANID_REC_OFFS_H, &val_h);
			if (ret == 0) {
				ret = EE_ReadVariable(FLASH_ADDR_CANID_REC_OFFS_L, &val_l);
				if (ret == 0) {
					can_dev.rec_offset = val_h;
					can_dev.rec_offset = (can_dev.rec_offset<<16) | val_l;
				}
			}
			can_dev.send_offset = can_dev.rec_offset;
			ret = EE_ReadVariable(FLASH_ADDR_CANID_SEND_OFFS_H, &val_h);
			if (ret == 0) {
				ret = EE_ReadVariable(FLASH_ADDR_CANID_SEND_OFFS_L, &val_l);
				if (ret == 0) {
					can_dev.send_offset = val_h;
					can_dev.send_offset = (can_dev.send_offset<<16) | val_l;
				}
			}
		}
	}

	/* msg receive filters */
	res = CnMsgFilterList(can_dev.cfg_id, can_dev.cfg_id, 0, 0);
	if (res == HAL_OK  && can_dev.base != 0) {
		uint32_t flt_id_0, flt_id_1;
		
		flt_id_0 = can_dev.cfg_id;
		flt_id_1 = can_dev.base + can_dev.rec_offset*MSG_CFG_STATUS;
		res = CnMsgFilterList(flt_id_0, flt_id_1, 0, 0);
		if (res == HAL_OK) {
			flt_id_0 = can_dev.base + can_dev.rec_offset*MSG_OUT_ENABLE;
			res = CnMsgFilterList(flt_id_0, flt_id_0, 1, 0);
			if (res == HAL_OK) {
				flt_id_1 = can_dev.base + can_dev.rec_offset*MSG_CNG_VELOC;
				res = CnMsgFilterList(flt_id_0, flt_id_1, 1, 0);
				if (res == HAL_OK) {
					flt_id_0 = can_dev.base + can_dev.rec_offset*MSG_HW_VER;
					res = CnMsgFilterList(flt_id_0, flt_id_0, 2, 1);
					if (res == HAL_OK) {
						flt_id_1 = can_dev.base + can_dev.rec_offset*MSG_FW_VER;
						res = CnMsgFilterList(flt_id_0, flt_id_1, 2, 1);
					}
				}
			}
		}
		if (res == HAL_OK) {
			res = CnMsgFilterList(can_dev.cfg_id, can_dev.cfg_id, 3, 1); /* per rtr */
		}
	}

	if (res != HAL_OK) {
		/* impostazione filtro piglia tutto */
		can_filter.FilterIdHigh = CONF_CANID>>13;  /* vedi pg 827 manuale uC */
		can_filter.FilterIdLow = (CONF_CANID<<3) & 0x0000FFFF;
		can_filter.FilterMaskIdHigh = 0;//0xFFFF;
		can_filter.FilterMaskIdLow = 0;//0xFFF8;
		can_filter.FilterMode = CAN_FILTERMODE_IDMASK;
		can_filter.FilterScale = CAN_FILTERSCALE_32BIT;
		can_filter.FilterBank = 0;
		can_filter.SlaveStartFilterBank = 0;
		can_filter.FilterActivation = ENABLE;
		can_filter.FilterFIFOAssignment = CAN_RX_FIFO0;
		HAL_CAN_ConfigFilter(&hcan, &can_filter);
	}

	if (HAL_CAN_Start(&hcan) != HAL_OK) {
		printf_err("HAL_CAN_Start: FAIL\r\n");
	}

	if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_TX_MAILBOX_EMPTY | CAN_IT_RX_FIFO0_FULL | CAN_IT_ERROR | CAN_IT_BUSOFF | CAN_IT_ERROR_PASSIVE | CAN_IT_ERROR_WARNING | CAN_IT_LAST_ERROR_CODE) != HAL_OK) {
		printf_err("HAL_CAN_ActivateNotification: FAIL\r\n");
	}

    /* reset errore in CAN */
    can_dev.error_glb = 0;

    /* abilita l'invio */
    can_dev.send_en = 1;
}



static void CanSendData(uint8_t msg_id, machine_status *machine, uint16_t param)
{
	uint16_t can_data[5] = {0};
	uint8_t *data = (uint8_t *)can_data;
	uint8_t send = 1;
	uint32_t mbx;
	CAN_TxHeaderTypeDef header = {0};

	header.StdId = 0x00;
	header.ExtId = can_dev.base_send + can_dev.send_offset*msg_id;
	header.IDE = CAN_ID_EXT;
	header.RTR = CAN_RTR_DATA;
	header.TransmitGlobalTime = DISABLE;

	switch (msg_id) {
	case MSG_MON_INFO:
		header.DLC = 6;
		can_dev.period_mon_info_force = 0;
		/* abilitazioni */
		if (machine->switch_on)
			data[0] |= 0x01;
		if (machine->enable_power)
			data[0] |= 0x02;
		/* errori */
		if (machine->error_ov_uv)
			data[1] |= 0x01;
		if (machine->error_overcurrent)
			data[1] |= 0x02;
		if (machine->error_temp_sens_1)
			data[1] |= 0x04;
		if (machine->error_temp_sens_2)
			data[1] |= 0x08;
		if (machine->error_th)
			data[1] |= 0x10;

		/* corrente */
		can_data[1] = machine->i;

		/* temperature */
		data[4] = machine->t_a;
		data[5] = machine->t_b;
		break;

	default:
		send = 0;
		break;
	}

	if (send) {
		memcpy(&can_dev.tx_msg.header, &header, sizeof(CAN_TxHeaderTypeDef));
		memcpy(can_dev.tx_msg.data, data, 8);
		if (HAL_CAN_AddTxMessage(&hcan, &header, data, &mbx) != HAL_OK) {
			can_dev.error_tot++;
			can_dev.error_tx++;
		}
		else {
			can_dev.send_en = 0;
		}
	}
}


static void CanSendCfgData(uint8_t cmd_id)
{
	uint16_t can_data[5] = {0};
	uint8_t *data = (uint8_t *)can_data;
	uint8_t send = 1;
	uint32_t mbx;
	CAN_TxHeaderTypeDef header = {0};

	header.StdId = 0x00;
	header.ExtId = can_dev.cfg_id;
	header.IDE = CAN_ID_EXT;
	header.RTR = CAN_RTR_DATA;
	header.TransmitGlobalTime = DISABLE;

	data[0] = cmd_id & 0x000000FF;
	data[1] = (cmd_id>>8) & 0x000000FF;

	switch (cmd_id) {
	case MSG_OPC_CANID_REC:
		header.DLC = 6;
		data[2] = can_dev.base & 0x000000FF;
		data[3] = (can_dev.base>>8) & 0x000000FF;
		data[4] = (can_dev.base>>16) & 0x000000FF;
		data[5] = (can_dev.base>>24) & 0x000000FF;
		break;

	case MSG_OPC_CANID_SEND:
		header.DLC = 6;
		data[2] = can_dev.base_send & 0x000000FF;
		data[3] = (can_dev.base_send>>8) & 0x000000FF;
		data[4] = (can_dev.base_send>>16) & 0x000000FF;
		data[5] = (can_dev.base_send>>24) & 0x000000FF;
		break;

	case MSG_OPC_CANID_OFFSET:
		header.DLC = 6;
		data[2] = can_dev.rec_offset & 0x000000FF;
		data[3] = (can_dev.rec_offset>>8) & 0x000000FF;
		data[4] = (can_dev.rec_offset>>16) & 0x000000FF;
		data[5] = (can_dev.rec_offset>>24) & 0x000000FF;
		break;

	default:
		send = 0;
		break;
	}

	if (send) {
		memcpy(&can_dev.tx_msg.header, &header, sizeof(CAN_TxHeaderTypeDef));
		memcpy(can_dev.tx_msg.data, data, 8);
		if (HAL_CAN_AddTxMessage(&hcan, &header, data, &mbx) != HAL_OK) {
			can_dev.error_tot++;
			can_dev.error_tx++;
		}
		else {
			can_dev.send_en = 0;
		}
	}
}


static int CanIdCheck(void)
{
	if (can_dev.base == can_dev.cfg_id ||
		can_dev.base + can_dev.rec_offset*MSG_CFG_STATUS == can_dev.cfg_id ||
		can_dev.base + can_dev.rec_offset*MSG_OUT_ENABLE == can_dev.cfg_id ||
		can_dev.base + can_dev.rec_offset*MSG_CNG_VELOC == can_dev.cfg_id ||
		can_dev.base + can_dev.rec_offset*MSG_HW_VER == can_dev.cfg_id ||
		can_dev.base + can_dev.rec_offset*MSG_FW_VER == can_dev.cfg_id
		) {

		return -1;
	}

	if (can_dev.base_send == can_dev.cfg_id ||
		can_dev.base_send + can_dev.send_offset*MSG_CFG_STATUS == can_dev.cfg_id ||
		can_dev.base_send + can_dev.send_offset*MSG_OUT_ENABLE == can_dev.cfg_id ||
		can_dev.base_send + can_dev.send_offset*MSG_CNG_VELOC == can_dev.cfg_id ||
		can_dev.base_send + can_dev.send_offset*MSG_HW_VER == can_dev.cfg_id ||
		can_dev.base_send + can_dev.send_offset*MSG_FW_VER == can_dev.cfg_id
		) {

		return -1;
	}

	return 0;
}


static void CanCommandExec(msg_can_rx *msg, machine_status *machine)
{
	static uint8_t save_speed = 0; /* 0: nulla; 1: attesa conferma; */
	int8_t save_speed_ack = 0; /* indica che e' arrivato un messaggio alla nuova vel (conferma cambio di vel) */
	uint16_t *cmd, opc;
	uint32_t mbx;
	app_btl *share_app = (app_btl *)APP_BTL_SHARE_ADDR;

	cmd = (uint16_t *)msg->data;

	/* se in configurazione */
	if (can_dev.cfg_en) { /* elaborazione dei comandi di configurazione */
		if (msg->header.RTR != CAN_RTR_DATA) { /* request */
			save_speed_ack = 1;
			if (msg->header.ExtId == can_dev.cfg_id) { /* richiesta configurazione CANID */
				static uint8_t rtr_resp = 0; /* indica il dato da inviare alla prossima request */
				switch (rtr_resp) {
				default:
					rtr_resp = 0;
					/* NON METTERE IL BREAK! */
				case 0:
					CanSendCfgData(MSG_OPC_CANID_REC);
					rtr_resp++;
					break;

				case 1:
					CanSendCfgData(MSG_OPC_CANID_SEND);
					rtr_resp++;
					break;

				case 2:
					CanSendCfgData(MSG_OPC_CANID_OFFSET);
					rtr_resp = 0;
					break;
				}
			}
		}
		else if (msg->header.ExtId == can_dev.cfg_id) {
			if (msg->header.DLC > 1) {
				opc = cmd[0];
				if (opc == MSG_OPC_CANID_REC && cmd[3] == HW_CHECK_3 && msg->header.DLC == 8) { /* configurazione CANID */
					uint32_t tmp;
					
					save_speed_ack = 1;
					/* configurazione CANID */
					can_dev.base = cmd[2];
					can_dev.base = (can_dev.base<<16) | cmd[1];
					/* controllo che non si utilizzi l'ID di configurazione */
					tmp = can_dev.base_send;
					if (can_dev.base_send == 0) {
						can_dev.base_send = can_dev.base;
					}
					if (CanIdCheck() != 0) {
						can_dev.base = 0;
						can_dev.base_send = tmp;
					}
					/* scrittura indirizzo CAN */
					FLASH_Unlock();
					EE_WriteVariable(FLASH_ADDR_CANID_H, ((can_dev.base>>16) & 0x0000FFFF));
					EE_WriteVariable(FLASH_ADDR_CANID_L, (can_dev.base & 0x0000FFFF));
					if (tmp == 0) {
						EE_WriteVariable(FLASH_ADDR_CANID_SEND_H, ((can_dev.base_send>>16) & 0x0000FFFF));
						EE_WriteVariable(FLASH_ADDR_CANID_SEND_L, (can_dev.base_send & 0x0000FFFF));
					}
					FLASH_Lock();
					CanReInit();
				}
				else if (opc == MSG_OPC_CANID_SEND && cmd[3] == HW_CHECK_3 && msg->header.DLC == 8) { /* configurazione CANID SEND */
					save_speed_ack = 1;
					/* configurazione CANID SEND */
					can_dev.base_send = cmd[2];
					can_dev.base_send = (can_dev.base_send<<16) | cmd[1];
					/* controllo che non si utilizzi l'ID di configurazione */
					if (CanIdCheck() != 0)
						can_dev.base_send = can_dev.base;
					/* scrittura indirizzo CAN */
					FLASH_Unlock();
					EE_WriteVariable(FLASH_ADDR_CANID_SEND_H, ((can_dev.base_send>>16) & 0x0000FFFF));
					EE_WriteVariable(FLASH_ADDR_CANID_SEND_L, (can_dev.base_send & 0x0000FFFF));
					FLASH_Lock();
					CanReInit();
				}
				else if (opc == MSG_OPC_CANID_OFFSET && cmd[3] == HW_CHECK_3 && msg->header.DLC == 8) { /* configurazione CANID OFFSET */
					save_speed_ack = 1;
					/* configurazione CANID OFFSET */
					can_dev.rec_offset = cmd[2];
					can_dev.rec_offset = (can_dev.rec_offset<<16) | cmd[1];
					/* controllo che non si utilizzi l'ID di configurazione */
					if (CanIdCheck() != 0)
						can_dev.rec_offset = 1;
					/* scrittura indirizzo CAN */
					FLASH_Unlock();
					EE_WriteVariable(FLASH_ADDR_CANID_SEND_OFFS_H, ((can_dev.rec_offset>>16) & 0x0000FFFF));
					EE_WriteVariable(FLASH_ADDR_CANID_SEND_OFFS_L, (can_dev.rec_offset & 0x0000FFFF));
					EE_WriteVariable(FLASH_ADDR_CANID_REC_OFFS_H, ((can_dev.rec_offset>>16) & 0x0000FFFF));
					EE_WriteVariable(FLASH_ADDR_CANID_REC_OFFS_L, (can_dev.rec_offset & 0x0000FFFF));
					FLASH_Lock();
					can_dev.send_offset = can_dev.rec_offset;
					CanReInit();
				}
				else if (opc == MSG_OPC_VELOC && cmd[2] == HW_CHECK_3 && msg->header.DLC == 6) { /* cambio velocita' */
					if (can_dev.speed == cmd[1]) {
						save_speed_ack = 1;
					}
					else {
						save_speed = 1;
						if (cmd[1] < CAN_SPEED_NONE) {
							can_dev.speed = cmd[1];
							CanSpeedInit(can_dev.speed);
						}
					}
				}
				else if (opc == MSG_OPC_BOOTLOADER && cmd[1] == HW_CHECK_0 && cmd[2] == HW_CHECK_2 && msg->header.DLC == 6) {
					if (share_app->head_code == APP_BTL_HEAD_CODE && *(&(share_app->head_code)+share_app->offset) == APP_BTL_TAIL_CODE) {
						machine->bootloader = 1;
					}
				}
			}
		}
		if (save_speed_ack == 1 && save_speed == 1) {
			save_speed = 0;
			/* scrittura indirizzo CAN */
			FLASH_Unlock();
			EE_WriteVariable(FLASH_ADDR_SPEED_ID, can_dev.speed);
			FLASH_Lock();
		}
	}

	/* richieste generiche */
	if (msg->header.RTR != CAN_RTR_DATA) { /* request */
		if (msg->header.ExtId == can_dev.base + can_dev.rec_offset*MSG_HW_VER) { /* richiesta versione HW */
			char hw_ver[25]; /* dim di app_btl brd_name */
			
			/* invio risposta */
			memset(&can_dev.tx_msg, 0, sizeof(msg_can_tx));
			can_dev.tx_msg.header.StdId = 0x00;
			can_dev.tx_msg.header.ExtId = can_dev.base + can_dev.rec_offset*MSG_HW_VER;
			can_dev.tx_msg.header.IDE = CAN_ID_EXT;
			can_dev.tx_msg.header.RTR = CAN_RTR_DATA;
			can_dev.tx_msg.header.TransmitGlobalTime = DISABLE;

			can_dev.tx_msg.header.DLC = 8;
			memset(can_dev.tx_msg.data, '\0', 8);

			memset(hw_ver, '\0', sizeof(hw_ver));
			if (share_app->head_code == APP_BTL_HEAD_CODE && *(&(share_app->head_code)+share_app->offset) == APP_BTL_TAIL_CODE) {
				sprintf(hw_ver, (char *)share_app->brd_name);
			}
			else {
				sprintf(hw_ver, "HW.dev");
			}
			memcpy(can_dev.tx_msg.data, hw_ver, 8);

			if (HAL_CAN_AddTxMessage(&hcan, &can_dev.tx_msg.header, can_dev.tx_msg.data, &mbx) != HAL_OK) {
				can_dev.error_tot++;
				can_dev.error_tx++;
			}
			else {
				can_dev.send_en = 0;
			}
		}
		else if (msg->header.ExtId == can_dev.base + can_dev.rec_offset*MSG_FW_VER) { /* richiesta versione HW */
			/* invio risposta */
			memset(&can_dev.tx_msg, 0, sizeof(msg_can_tx));
			can_dev.tx_msg.header.StdId = 0x00;
			can_dev.tx_msg.header.ExtId = can_dev.base + can_dev.rec_offset*MSG_FW_VER;
			can_dev.tx_msg.header.IDE = CAN_ID_EXT;
			can_dev.tx_msg.header.RTR = CAN_RTR_DATA;
			can_dev.tx_msg.header.TransmitGlobalTime = DISABLE;

			can_dev.tx_msg.header.DLC = 8;
			cmd = (uint16_t *)can_dev.tx_msg.data;
			cmd[0] = VER_CODE;
			cmd[1] = VER_MAJ;
			cmd[2] = VER_MIN;
			cmd[3] = VER_PATCH;
			if (HAL_CAN_AddTxMessage(&hcan, &can_dev.tx_msg.header, can_dev.tx_msg.data, &mbx) != HAL_OK) {
				can_dev.error_tot++;
				can_dev.error_tx++;
			}
			else {
				can_dev.send_en = 0;
			}
		}
	}

	/* only data frame */
	if (msg->header.RTR != CAN_RTR_DATA)
		return;
	
#if 0
	if (can_dev.base == 0) { /* comandi abilitati se e solo se il CANID e' 0 (in produzone) */
		if (msg->header.ExtId == can_dev.cfg_id) {
			opc = cmd[0];
		}
		return;
	}
#endif

	/* gestione dei comandi */
	save_speed_ack = 1;
	if (msg->header.ExtId == can_dev.base + can_dev.rec_offset*MSG_CFG_STATUS && msg->header.DLC == 2) {
	    if (cmd[0] >= MSG_PERIOD_MIN || cmd[0] == 0)
	    	can_dev.period_mon_info = cmd[0];
	    can_dev.periodic_en = 1;
	}
	else if (msg->header.ExtId == can_dev.base + can_dev.rec_offset*MSG_OUT_ENABLE && msg->header.DLC == 4) {
		if (cmd[0] & 0x0001) {
			machine->enable_power = 1;
			/* reset degli errori */
			machine->error_overcurrent = 0;
			machine->error_th = 0;
		}
		else {
			machine->enable_power = 0;
		}
		if (cmd[1] & 0x0001) {
			machine->switch_on = 1;
		}
		else {
			machine->switch_on = 0;
		}
	    can_dev.periodic_en = 1;
	}
	else if (msg->header.ExtId == can_dev.base + can_dev.rec_offset*MSG_CNG_VELOC && msg->header.DLC == 2) { /* cambio velocita' */
		if (can_dev.speed != cmd[0] && cmd[0] < CAN_SPEED_NONE) { /*  && cmd[0] >= CAN_SPEED_1M */
			save_speed = 1;
			save_speed_ack = 0;
			can_dev.speed = cmd[0];
		    CanSpeedInit(can_dev.speed); /* c'e' anche l'inizializzazione */
		}
	}
	else {
		save_speed_ack = 0;
	}

	if (save_speed_ack == 1 && save_speed == 1) {
		save_speed = 0;
		/* scrittura indirizzo CAN */
		FLASH_Unlock();
		EE_WriteVariable(FLASH_ADDR_SPEED_ID, can_dev.speed);
		FLASH_Lock();
	}
}


void CanMsgInit(void)
{
	uint16_t ret, val;

	/* CAN inizializzazione */
	can_dev.speed = CAN_SPEED_250K; /* default */
	ret = EE_ReadVariable(FLASH_ADDR_SPEED_ID, &val);
	if (ret == 0 && val < CAN_SPEED_NONE) {
		can_dev.speed = val;
	}

    /* svuota la coda in ingresso al CAN bus */
    can_dev.rx_queue_in = can_dev.rx_queue_out = 0;

    /* presisposizione periodicita' messaggi */
    can_dev.period_mon_info = MSG_PERIOD_MON_INFO;

    CanSpeedInit(can_dev.speed); /* c'e' anche l'inizializzazione */
}


void CanMsgEnableForce(void)
{
	can_dev.period_mon_info_force = 1;
}


static int16_t OutsAreDisable(machine_status *machine)
{
	if (machine->switch_on || machine->enable_power)
		return 0;
	return 1;
}


int8_t CanMsgManager(uint8_t tick, machine_status *machine) /* tick va ad 1 ogni 10ms */
{
	static uint16_t tick_cnt_mon_info;
	static uint16_t led_err_on;
	int8_t ret = 0;
	uint32_t mbx, dt, dt_max, dt_id;

/*
	if (tick) { // 10ms
		if (led_err_on) {
			if (led_err_on % 10 == 0) {
				HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
			}
			led_err_on--;
		}
		else {
			HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
		}
	}

*/
	if (CanControlLoop(tick) != 0) {
		/* errore nel can bus, disabilitazione di tutte le uscite */
		led_err_on = 100; /* lampeggia per 100*10ms */
		ret = -1;
	}

	/* elaborazione dei messaggi in cosa */
	if (can_dev.rx_queue_in != can_dev.rx_queue_out && can_dev.send_en == 1) { /* messaggio ricevuto e possibilita di inviare una risposta */
		CanCommandExec(&can_dev.rx_msg_queue[can_dev.rx_queue_out], machine);
		can_dev.rx_queue_out = (can_dev.rx_queue_out + 1)%CAN_RX_QUEUE;
	}

	/* verifica se si puo' andare in configurazione */
	if (can_dev.periodic_en == 0 && OutsAreDisable(machine) == 1) {
		can_dev.cfg_en = 1;
	}
	else {
		can_dev.cfg_en = 0;
	}

	/* gestione re-invii pacchetti */
	if (can_dev.send_en == 2) {
		static uint16_t re_send = 0;
		
		if (re_send == MSG_RE_SEND_MAX) {
			/* re-iniziliazzazione CANbus */
			re_send = 0;
			printf_err("Re-Send Error\r\n");
			CanReInit();
			can_dev.send_en = 2; /* per imporre il re-invio del medesimo pacchetto */
		}
		else {
			if (HAL_CAN_AddTxMessage(&hcan, &can_dev.tx_msg.header, can_dev.tx_msg.data, &mbx) != HAL_OK) {
				can_dev.error_tot++;
				can_dev.error_tx++;
			}
			else {
				re_send++;
				can_dev.send_en = 0;
			}
		}
	}

	/* gestione messaggi periodici */
	if (can_dev.periodic_en == 0) {
		tick_cnt_mon_info = 0;

		return ret;
	}

 	if (can_tick_1ms == 0) {
 		return ret;
 	}

 	dt = can_tick_1ms;
 	can_tick_1ms = 0;

	tick_cnt_mon_info += dt;

	/* invio messaggi */
	dt_max = dt_id = 0; /* da usare per dare priorita' a messaggi piu' in ritardo */
	if (tick_cnt_mon_info >= can_dev.period_mon_info || can_dev.period_mon_info_force) {
		dt_max = tick_cnt_mon_info - can_dev.period_mon_info;
		dt_id = 1;
	}

	switch (dt_id) {
	default:
		break;

	case 1:
		tick_cnt_mon_info = 0;
		CanSendData(MSG_MON_INFO, machine, 0);
		break;
	}

	return ret;
}


void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
	uint32_t err;

	__HAL_RCC_CAN1_FORCE_RESET();
	__HAL_RCC_CAN1_RELEASE_RESET();

	err = HAL_CAN_GetError(hcan);
	if ((err & (HAL_CAN_ERROR_TX_ALST0 | HAL_CAN_ERROR_TX_TERR0 | HAL_CAN_ERROR_TX_ALST1 | HAL_CAN_ERROR_TX_TERR1 | HAL_CAN_ERROR_TX_ALST2 | HAL_CAN_ERROR_TX_TERR2))  &&  HAL_CAN_GetState(hcan) == HAL_CAN_STATE_READY) {
		can_dev.send_en = 2; /* riabilitazione re-invio */
	}
	else {
		if ((err & (HAL_CAN_ERROR_ACK | HAL_CAN_ERROR_BOF | HAL_CAN_ERROR_BR | HAL_CAN_ERROR_BD)) != 0) {
			can_dev.error_tx++;
			can_dev.send_en = 1; /* riabilitazione invio */
		}
		can_dev.error_glb++;
		can_dev.error_tot++;
	}
}


void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];

	if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, data) == HAL_OK) {
		if ((can_dev.rx_queue_in+1)%CAN_RX_QUEUE != can_dev.rx_queue_out && header.IDE == CAN_ID_EXT) {
			uint32_t cmd_id;
			
			/* filtro messaggi destinati al nodo */
			cmd_id = header.ExtId;
			if (cmd_id == can_dev.cfg_id || (can_dev.base != 0 && (
					cmd_id == can_dev.base + can_dev.rec_offset*MSG_CFG_STATUS ||
					cmd_id == can_dev.base + can_dev.rec_offset*MSG_OUT_ENABLE ||
					cmd_id == can_dev.base + can_dev.rec_offset*MSG_CNG_VELOC ||
					cmd_id == can_dev.base + can_dev.rec_offset*MSG_HW_VER    ||
					cmd_id == can_dev.base + can_dev.rec_offset*MSG_FW_VER

					))) {
				can_dev.rx++;

				memcpy(&can_dev.rx_msg_queue[can_dev.rx_queue_in].header, &header, sizeof(CAN_RxHeaderTypeDef));
				memcpy(can_dev.rx_msg_queue[can_dev.rx_queue_in].data, data, sizeof(data));
				can_dev.rx_queue_in = (can_dev.rx_queue_in + 1) % CAN_RX_QUEUE;
			}
		}

		can_dev.error_glb = 0;
		can_dev.tot_rx++;
	}
}


void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan)
{
	static unsigned long old_tx_error = 0;

	can_dev.send_en = 1;
	can_dev.tx++;

	if (can_dev.error_tx && can_dev.error_tx == old_tx_error) /* il bus ha ricominciato a funzionare */
		can_dev.error_tx--;

	old_tx_error = can_dev.error_tx;
	can_dev.error_glb = 0;
}


void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *hcan)
{
	can_dev.send_en = 2; /* riabilitazione re-invio */
}
