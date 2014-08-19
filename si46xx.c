/* 
 * dabpi_ctl - raspberry pi fm/fmhd/dab receiver board control interface
 * Copyright (C) 2014  Bjoern Biesenbach <bjoern@bjoern-b.de>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __arm__
#include <wiringPi.h>
#include <wiringPiSPI.h>
#endif
#include "si46xx.h"
//#include "dab_radio_3_2_7.h"

#define msleep(x) usleep(x*1000)

#ifdef __arm__
#define CS_LOW() digitalWrite(10, LOW)
#define CS_HIGH() digitalWrite(10, HIGH)

#define RESET_LOW() digitalWrite(4, LOW)
#define RESET_HIGH() digitalWrite(4, HIGH)

#define SPI_Write(data,len) wiringPiSPIDataRW(0,data,len)
#else
#define CS_LOW()
#define CS_HIGH()
#define RESET_LOW()
#define RESET_HIGH()
#define SPI_Write(data,len)
#endif
//#define msleep(x) HAL_Delay(x)
//#define SPI_Write(data,len) HAL_SPI_Transmit(&hspi1,data,len,1000)
//#define CS_LOW() GPIOA->BSRRH = SI46XX_PIN_NSS
//#define CS_HIGH() GPIOA->BSRRL = SI46XX_PIN_NSS
//#define RESET_LOW() GPIOB->BSRRH = GPIO_PIN_8
//#define RESET_HIGH() GPIOB->BSRRL = GPIO_PIN_8

void print_hex_str(uint8_t *str, uint16_t len)
{
	uint16_t i;
	for(i=0;i<len;i++){
		printf("%02x",(int)str[i]);
	}
	printf("\r\n");
}

static void si46xx_write_host_load_data(uint8_t cmd,
					const uint8_t *data,
					uint16_t len)
{

	uint8_t zero_data[3];

	zero_data[0] = 0;
	zero_data[1] = 0;
	zero_data[2] = 0;
	CS_LOW();
	SPI_Write(&cmd,1);
	SPI_Write(zero_data,3);
	SPI_Write((uint8_t*)data,len);
	CS_HIGH();
}

static void si46xx_write_data(uint8_t cmd,
				uint8_t *data,
				uint16_t len)
{

	CS_LOW();
	SPI_Write(&cmd,1);
	SPI_Write(data,len);
	CS_HIGH();
}

static void si46xx_read(uint8_t *data, uint8_t cnt)
{
	uint8_t zero = 0;
	CS_HIGH();
	msleep(1); // make sure cs is high for 20us
	CS_LOW();
	SPI_Write(&zero,1);
	SPI_Write(data,cnt);
	CS_HIGH();
	msleep(1); // make sure cs is high for 20us
}

static uint16_t si46xx_read_dynamic(uint8_t *data)
{
	uint8_t zero = 0;
	uint16_t cnt;

	CS_HIGH();
	msleep(1); // make sure cs is high for 20us
	CS_LOW();
	SPI_Write(&zero,1);
	SPI_Write(data,6);
	cnt = ((uint16_t)data[5]<<8) | (uint16_t)data[4];
	if(cnt > 3000) cnt = 0;
	SPI_Write(&data[6],cnt);
	CS_HIGH();
	msleep(1); // make sure cs is high for 20us

	return cnt + 6;
}

static void si46xx_get_sys_state()
{
	uint8_t zero = 0;
	char buf[6];

	si46xx_write_data(SI46XX_GET_SYS_STATE,&zero,1);
	si46xx_read(buf,6);
	printf("si46xx_get_sys_state answer: ");
	print_hex_str(buf,6);
}

static void si46xx_get_part_info(){
	uint8_t zero = 0;
	char buf[22];

	si46xx_write_data(SI46XX_GET_PART_INFO,&zero,1);
	si46xx_read(buf,22);
	printf("si46xx_get_part_info answer: ");
	print_hex_str(buf,22);
}

void si46xx_periodic()
{
	char buf[4];
	si46xx_read(buf,4);
}


void si46xx_dab_start_digital_service(uint32_t service_id,
				      uint32_t comp_id)
{
	uint8_t data[11];
	char buf[5];

	data[0] = 0;
	data[1] = 0;
	data[2] = 0;
	data[3] = service_id & 0xFF;
	data[4] = (service_id >>8) & 0xFF;
	data[5] = (service_id >>16) & 0xFF;
	data[6] = (service_id >>24) & 0xFF;
	data[7] = comp_id & 0xFF;
	data[8] = (comp_id >> 8) & 0xFF;
	data[9] = (comp_id >> 16) & 0xFF;
	data[10] = (comp_id >> 24) & 0xFF;

	si46xx_write_data(SI46XX_DAB_START_DIGITAL_SERVICE,data,11);
	si46xx_read(buf,5);
	//print_hex_str(buf,5);
}

static void si46xx_dab_parse_service_list(uint8_t *data, uint16_t len)
{
	uint16_t remaining_bytes;
	uint16_t pos;
	uint8_t service_num;

	if(len<6) return; // no list available? exit
	if(len >= 9){
		dab_service_list.list_size = data[5]<<8 | data[4];
		dab_service_list.version = data[7]<<8 | data[6];
		dab_service_list.num_services = data[8];
	}
	// 9,10,11 are align pad
	pos = 12;
	if(len <= pos) return; // no services? exit

	remaining_bytes = len - pos;
	service_num = 0;
	// size of one service with one component: 28 byte
	while(service_num < dab_service_list.num_services){
		dab_service_list.services[service_num].service_id =
			data[pos+3]<<24 | data[pos+2]<<16 | data[pos+1]<<8 | data[pos];
		memcpy(dab_service_list.services[service_num].service_label,
			&data[pos+8],16);
//		dab_service_list.services[service_num].service_label[0] = data[pos+8];
//		dab_service_list.services[service_num].service_label[1] = data[pos+9];
//		dab_service_list.services[service_num].service_label[2] = data[pos+10];
//		dab_service_list.services[service_num].service_label[3] = data[pos+11];
//		dab_service_list.services[service_num].service_label[4] = data[pos+12];
//		dab_service_list.services[service_num].service_label[5] = data[pos+13];
//		dab_service_list.services[service_num].service_label[6] = data[pos+14];
//		dab_service_list.services[service_num].service_label[7] = data[pos+15];
//		dab_service_list.services[service_num].service_label[8] = data[pos+16];
//		dab_service_list.services[service_num].service_label[9] = data[pos+17];
//		dab_service_list.services[service_num].service_label[10] = data[pos+18];
//		dab_service_list.services[service_num].service_label[11] = data[pos+19];
//		dab_service_list.services[service_num].service_label[12] = data[pos+20];
//		dab_service_list.services[service_num].service_label[13] = data[pos+21];
//		dab_service_list.services[service_num].service_label[14] = data[pos+22];
//		dab_service_list.services[service_num].service_label[15] = data[pos+23];
		dab_service_list.services[service_num].service_label[16] = '\0';
		dab_service_list.services[service_num].component_id =
			data[pos+25] << 8 | data[pos+24];
		pos +=28;
		service_num++;
	}
}

void si46xx_dab_print_service_list()
{
	uint8_t i;

	printf("List size:     %d\r\n",dab_service_list.list_size);
	printf("List version:  %d\r\n",dab_service_list.version);
	printf("Services:      %d\r\n",dab_service_list.num_services);

	for(i=0;i<dab_service_list.num_services;i++){
		printf("Num: %u  Service ID: %x  Service Name: %s  Component ID: %d\r\n",
			i,
			dab_service_list.services[i].service_id,
			dab_service_list.services[i].service_label,
			dab_service_list.services[i].component_id
			);
	}
}

void si46xx_dab_start_digital_service_num(uint32_t num)
{
	printf("Starting service %s %x %x\r\n", dab_service_list.services[num].service_label,
						dab_service_list.services[num].service_id,
						dab_service_list.services[num].component_id);
	si46xx_dab_start_digital_service(dab_service_list.services[num].service_id,
			dab_service_list.services[num].component_id);
}

int si46xx_dab_get_digital_service_list()
{
	uint8_t zero = 0;
	uint16_t len;
	uint16_t timeout;
	char buf[2047+6];

	printf("si46xx_dab_get_digital_service_list()\r\n");
	timeout = 100;
	while(timeout--){
		si46xx_write_data(SI46XX_DAB_GET_DIGITAL_SERVICE_LIST,&zero,1);
		if((len = si46xx_read_dynamic(buf)) > 6)
			break;
	}
	si46xx_dab_parse_service_list(buf,len);
	return len;
}

void si46xx_dab_set_freq_list(uint8_t num, uint32_t *freq_list)
{
	uint8_t data[7];
	uint8_t i;
	char buf[4];

	printf("si46xx_dab_set_freq_list(): ");
	if(num == 0 || num > 48){
		printf("num must be between 1 and 48\r\n");
		return;
	}

	data[0] = 1; // NUM_FREQS 1-48
	data[1] = 0;
	data[2] = 0;

	for(i=0;i<num;i++){
		data[3+4*i] = freq_list[i] & 0xFF;
		data[4+4*i] = freq_list[i] >> 8;
		data[5+4*i] = freq_list[i] >> 16;
		data[6+4*i] = freq_list[i] >> 24;
	}
	si46xx_write_data(SI46XX_DAB_SET_FREQ_LIST,data,3+4*num);

	si46xx_read(buf,4);
	print_hex_str(buf,4);
}

void si46xx_dab_tune_freq(uint8_t index, uint8_t antcap)
{
	uint8_t data[5];
	char buf[4];

	printf("si46xx_dab_tune_freq(%d): ",index);

	//data[0] = (1<<4) | (1<<0); // force_wb, low side injection
	data[0] = 0;
	data[1] = index;
	data[2] = 0;
	data[3] = antcap;
	data[4] = 0;
	si46xx_write_data(SI46XX_DAB_TUNE_FREQ,data,5);

	si46xx_read(buf,4);
	print_hex_str(buf,4);
}

void si46xx_fm_tune_freq(uint32_t khz, uint16_t antcap)
{
	uint8_t data[5];
	char buf[4];

	printf("si46xx_fm_tune_freq(%d)\r\n",khz);

	//data[0] = (1<<4) | (1<<0); // force_wb, low side injection
	//data[0] = (1<<4)| (1<<3); // force_wb, tune_mode=2
	data[0] = 0;
	data[1] = ((khz/10) & 0xFF);
	data[2] = ((khz/10) >> 8) & 0xFF;
	data[3] = antcap & 0xFF;
	data[4] = 0;
	si46xx_write_data(SI46XX_FM_TUNE_FREQ,data,5);

	si46xx_read(buf,4);
	print_hex_str(buf,4);
}

static void si46xx_load_init()
{
	uint8_t data = 0;
	si46xx_write_data(SI46XX_LOAD_INIT,&data,1);
	msleep(1); // wait 4ms (datasheet)
}

static void store_image(const uint8_t *data, uint32_t len, uint8_t wait_for_int)
{
	uint32_t remaining_bytes = len;
	uint32_t count_to;
	char buf[4];

	si46xx_load_init();
	while(remaining_bytes){
		if(remaining_bytes >= 2048){
			count_to = 2048;
		}else{
			count_to = remaining_bytes;
		}

		si46xx_write_host_load_data(SI46XX_HOST_LOAD, data+(len-remaining_bytes), count_to);
		remaining_bytes -= count_to;
		msleep(1);
	}
	msleep(4); // wait 4ms (datasheet)
	si46xx_read(buf,4);
	msleep(4); // wait 4ms (datasheet)
}

static void store_image_from_file(char *filename, uint8_t wait_for_int)
{
	long remaining_bytes;
	long len;
	uint32_t count_to;
	FILE *fp;
	uint8_t buffer[2048];
	size_t result;
	char buf[4];

	fp = fopen(filename, "rb");
	if(fp == NULL){
		printf("file error %s\r\n",filename);
		return;
	}

	fseek(fp,0, SEEK_END);
	len = ftell(fp);
	remaining_bytes = len;
	rewind(fp);

	si46xx_load_init();
	while(remaining_bytes){
		if(remaining_bytes >= 2048){
			count_to = 2048;
		}else{
			count_to = remaining_bytes;
		}
		result = fread(buffer,1,count_to,fp);
		if(result != count_to){
			printf("file error %s\r\n",filename);
			return;
		}

		si46xx_write_host_load_data(SI46XX_HOST_LOAD, buffer, count_to);
		remaining_bytes -= count_to;
		msleep(1);
	}
	fclose(fp);
	msleep(4); // wait 4ms (datasheet)
	si46xx_read(buf,4);
	msleep(4); // wait 4ms (datasheet)
}

static void si46xx_powerup()
{
	uint8_t data[15];
	char buf[4];

	data[0] = 0x80; // ARG1
	data[1] = (1<<4) | (7<<0); // ARG2 CLK_MODE=0x1 TR_SIZE=0x5
	//data[2] = 0x28; // ARG3 IBIAS=0x28
	data[2] = 0x48; // ARG3 IBIAS=0x28
	data[3] = 0x00; // ARG4 XTAL
	data[4] = 0xF9; // ARG5 XTAL // F8
	data[5] = 0x24; // ARG6 XTAL
	data[6] = 0x01; // ARG7 XTAL 19.2MHz
	data[7] = 0x1F; // ARG8 CTUN
	data[8] = 0x00 | (1<<4); // ARG9
	data[9] = 0x00; // ARG10
	data[10] = 0x00; // ARG11
	data[11] = 0x00; // ARG12
	data[12] = 0x00; // ARG13 IBIAS_RUN
	data[13] = 0x00; // ARG14
	data[14] = 0x00; // ARG15

	si46xx_write_data(SI46XX_POWER_UP,data,15);
	msleep(1); // wait 20us after powerup (datasheet)
	si46xx_read(buf,4);
}

static void si46xx_boot()
{
	uint8_t data = 0;
	char buf[4];

	si46xx_write_data(SI46XX_BOOT,&data,1);
	msleep(300); // 63ms at analog fm, 198ms at DAB
	si46xx_read(buf,4);
}

void si46xx_fm_rsq_status()
{
	uint8_t data = 0;
	char buf[20];

	printf("si46xx_fm_rsq_status()\r\n");
	si46xx_write_data(SI46XX_FM_RSQ_STATUS,&data,1);
	si46xx_read(buf,20);
	print_hex_str(buf,20);
	printf("SNR: %d dB\r\n",(int8_t)buf[10]);
	printf("RSSI: %d dBuV\r\n",(int8_t)buf[9]);
	printf("FREQOFF: %d\r\n",(int8_t)buf[8]*2);
	printf("READANTCAP: %d\r\n",(int8_t)(buf[12]+(buf[13]<<8)));
}

void si46xx_fm_rds_status()
{
	uint8_t data = 0;
	char buf[20];

	printf("si46xx_rds_status()\r\n");
	si46xx_write_data(SI46XX_FM_RDS_STATUS,&data,1);
	si46xx_read(buf,20);
	print_hex_str(buf,20);
	printf("RDSSYNC: %u\r\n",buf[5]&0x02);
}

void si46xx_dab_get_service_linking_info(uint32_t service_id)
{
	uint8_t data[7];
	char buf[24];

	printf("si46xx_dab_get_service_linking_info()\r\n");
	data[0] = 0;
	data[1] = 0;
	data[2] = 0;
	data[3] = (service_id) & 0xFF;
	data[4] = (service_id>>8) & 0xFF;
	data[5] = (service_id>>16) & 0xFF;
	data[6] = (service_id>>24) & 0xFF;
	si46xx_write_data(SI46XX_DAB_GET_SERVICE_LINKING_INFO,data,7);
	si46xx_read(buf,24);
}

void si46xx_dab_digrad_status()
{
	uint8_t data = (1<<3) | 1; // set digrad_ack and stc_ack
	char buf[22];

	printf("si46xx_dab_digrad_status(): ");
	si46xx_write_data(SI46XX_DAB_DIGRAD_STATUS,&data,1);
	si46xx_read(buf,22);
	print_hex_str(buf,22);
	printf("ACQ: %d\r\n",buf[5] & 0x04);
	printf("VALID: %d\r\n",buf[5] & 0x01);
	printf("RSSI: %d\r\n",(int8_t)buf[6]);
	printf("SNR: %d\r\n",(int8_t)buf[7]);
	printf("FIC_QUALITY: %d\r\n",buf[8]);
	printf("CNR %d\r\n",buf[9]);
	printf("FFT_OFFSET %d\r\n",(int8_t)buf[17]);
	printf("ANTCAP: %d\r\n",(buf[19]<<8)+buf[18]);
}

void si46xx_set_property(uint16_t property_id, uint16_t value)
{
	uint8_t data[5];
	char buf[4];

	printf("si46xx_set_property(%d,%d)\r\n",property_id,value);
	data[0] = 0;
	data[1] = property_id & 0xFF;
	data[2] = (property_id >> 8) & 0xFF;
	data[3] = value & 0xFF;
	data[4] = (value >> 8) & 0xFF;
	si46xx_write_data(SI46XX_SET_PROPERTY,data,5);
	si46xx_read(buf,4);
	print_hex_str(buf,4);
}

void si46xx_init()
{
	uint8_t read_data[30];
#ifdef __arm__
	wiringPiSetup();
	pinMode(4, OUTPUT);
	pinMode(10, OUTPUT);
	CS_HIGH();

	if(!wiringPiSPISetup(0,10000000) == -1){ // 10MHz SPI
		printf("setup SPI error\r\n");
	}
#endif

}

void si46xx_init_fm()
{
	uint8_t read_data[30];
	printf("si46xx_init_mode_fm()\r\n");
	/* reset si46xx  */
	RESET_LOW();
	msleep(10);
	RESET_HIGH();
	msleep(10);
	si46xx_powerup(read_data);
	store_image_from_file("firmware/rom00_patch.016.bin",0);

	//store_image(rom00_patch_016_bin,rom00_patch_016_bin_len,0);
	store_image_from_file("firmware/fmhd_radio_3_0_19.bif",0);
	//store_image(fmhd_radio_3_0_19_bif,fmhd_radio_3_0_19_bif_len,0);
	si46xx_boot(read_data);
	si46xx_get_sys_state(read_data);
	si46xx_get_part_info(read_data);
	//CDC_TxString("si46xx_init() done\r\n");
}

void si46xx_init_dab()
{
	uint8_t read_data[30];
	printf("si46xx_init_mode_dab()\r\n");
	/* reset si46xx  */
	RESET_LOW();
	msleep(10);
	RESET_HIGH();
	msleep(10);
	si46xx_powerup(read_data);
	store_image_from_file("firmware/rom00_patch.016.bin",0);
	//store_image(rom00_patch_016_bin,rom00_patch_016_bin_len,0);
	//
	store_image_from_file("firmware/dab_radio_3_2_7.bif",0);
	//store_image(dab_radio_3_2_7_bif,dab_radio_3_2_7_bif_len,0);
	si46xx_boot(read_data);
	si46xx_get_sys_state(read_data);
	si46xx_get_part_info(read_data);
	printf("si46xx_init() done\r\n");
}