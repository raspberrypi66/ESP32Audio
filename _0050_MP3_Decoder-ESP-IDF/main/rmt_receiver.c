#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/rmt.h>
#include "rmt_receiver.h"
#include "web_radio.h"

#define MARK   0
#define SPACE  1

// Clock divisor 
#define CLK_DIV 100

#define RMT_BASE_CLK (APB_CLK_FREQ/CLK_DIV)

// Number of clock ticks that represent 10us.  10 us = 1/100th msec.
#define TICK_10_US 	(APB_CLK_FREQ / CLK_DIV / 100000)
#define TICK_125_US (APB_CLK_FREQ / CLK_DIV / 8000)

// set filter threshold 60 us
#define FILTER_THRESHOLD 	(TICK_10_US * 3)

// set idle threshold 20 ms
#define IDLE_THRESHOLD 		(TICK_10_US * 100 * 20)

static char tag[] = "rmt_receiver";
static RingbufHandle_t ringBuf;


static void dumpStatus(rmt_channel_t channel) 
{
   bool loop_en;
   uint8_t div_cnt;
   uint8_t memNum;
   bool lowPowerMode;
   rmt_mem_owner_t owner;
   uint16_t idleThreshold;
   uint32_t status;
   rmt_source_clk_t srcClk;

   rmt_get_tx_loop_mode(channel, &loop_en);
   rmt_get_clk_div(channel, &div_cnt);
   rmt_get_mem_block_num(channel, &memNum);
   rmt_get_mem_pd(channel, &lowPowerMode);
   rmt_get_memory_owner(channel, &owner);
   rmt_get_rx_idle_thresh(channel, &idleThreshold);
   rmt_get_status(channel, &status);
   rmt_get_source_clk(channel, &srcClk);

   ESP_LOGI(tag, "Status for RMT channel %d", channel);
   ESP_LOGI(tag, "- Loop enabled: %d", loop_en);
   ESP_LOGI(tag, "- Clock divisor: %d", div_cnt);
   ESP_LOGI(tag, "- Number of memory blocks: %d", memNum);
   ESP_LOGI(tag, "- Low power mode: %d", lowPowerMode);
   ESP_LOGI(tag, "- Memory owner: %s", owner==RMT_MEM_OWNER_TX?"TX":"RX");
   ESP_LOGI(tag, "- Idle threshold: %d", idleThreshold);
   ESP_LOGI(tag, "- Status: %d", status);
   ESP_LOGI(tag, "- Source clock: %s", srcClk==RMT_BASECLK_APB?"APB (80MHz)":"1MHz");
}

// panasonic: raw data mark-space in usec 
// [POWER] 
// 2500	3479	1750	417	438	417	1312	417	438	417	438	417	438	417	438	417	438	417	438	417	438	417	438	417	438	417	438	417	438	417	1312	417	438	417	438	417	438	417	438	417	438	417	438	396	438	417	438	417	438	417	1312	417	438	417	438	417	438	417	438	417	438	417	438	417	438	417	438	417	1312	417	438	417	1312	417	1312	417	1312	417	1312	417	438	417	438	417	1312	417	438	417	1312	417	1312	417	1312	417	1312	417	438	417	1312	417	4979	
#define PANASONIC_BITS          	48
#define PANASONIC_HDR_MARK    		3502
#define PANASONIC_HDR_SPACE   		1750
#define PANASONIC_BIT_MARK     		502
#define PANASONIC_ONE_SPACE   		1244
#define PANASONIC_ZERO_SPACE   		400

#define PANASONIC_HDR_MARK_TICK    	(PANASONIC_HDR_MARK * TICK_10_US / 10)
#define PANASONIC_HDR_SPACE_TICK   	(PANASONIC_HDR_SPACE * TICK_10_US / 10)
#define PANASONIC_BIT_MARK_TICK     (PANASONIC_BIT_MARK * TICK_10_US / 10)
#define PANASONIC_ONE_SPACE_TICK   	(PANASONIC_ONE_SPACE * TICK_10_US / 10)
#define PANASONIC_ZERO_SPACE_TICK	(PANASONIC_ZERO_SPACE * TICK_10_US / 10)

#define EXCESS_US    				100
#define EXCESS_TICK    				(EXCESS_US * TICK_10_US / 10)


bool MATCH (uint32_t measured_tick,  uint32_t desired_tick)
{
	//ESP_LOGI(tag, "[MATCH_MARK] measured_tick: %d, desired_tick: %d, EXCESS_TICK: %d", measured_tick, desired_tick, EXCESS_TICK);
	return ((measured_tick < (desired_tick + EXCESS_TICK)) && (measured_tick > (desired_tick - EXCESS_TICK)));
}

bool decodePanasonic (rmt_item32_t *data, int numItems, decode_results_t *results)
{
	if(numItems < 50)
		return false;
	
	uint64_t decode = 0;
    uint32_t offset = 0;
	
	//ESP_LOGI(tag, "HDR");

    if (!MATCH(data[offset].duration0, PANASONIC_HDR_MARK_TICK ))  return false ;
    if (!MATCH(data[offset].duration1, PANASONIC_HDR_SPACE_TICK))  return false ;
	
	offset++;

    for (int i = 0;  i < PANASONIC_BITS;  i++) 
	{
		//ESP_LOGI(tag, "offset: %d", offset);
        if (!MATCH(data[offset].duration0, PANASONIC_BIT_MARK_TICK))  return false ;

        if      (MATCH(data[offset].duration1, PANASONIC_ONE_SPACE_TICK ))  decode = (decode << 1) | 1 ;
        else if (MATCH(data[offset].duration1, PANASONIC_ZERO_SPACE_TICK))  decode = (decode << 1) | 0 ;
        else return false ;
        offset++;
    }

    results->value       = (uint32_t)decode;
    results->address     = (uint16_t)(decode >> 32);
    results->bits        = PANASONIC_BITS;

    return true;
}



bool isInRange(rmt_item32_t item, int lowDuration, int highDuration, int tolerance) {
	uint32_t lowValue = item.duration0 * 10 / TICK_10_US;
	uint32_t highValue = item.duration1 * 10 / TICK_10_US;
	/*
	ESP_LOGI(tag, "lowValue=%d, highValue=%d, lowDuration=%d, highDuration=%d",
		lowValue, highValue, lowDuration, highDuration);
	*/
	if (lowValue < (lowDuration - tolerance) || lowValue > (lowDuration + tolerance) ||
			(highValue != 0 &&
			(highValue < (highDuration - tolerance) || highValue > (highDuration + tolerance)))) {
		return false;
	}
	return true;
}

bool NEC_is0(rmt_item32_t item) {
	return isInRange(item, 560, 560, 100);
}

bool NEC_is1(rmt_item32_t item) {
	return isInRange(item, 560, 1690, 100);
}

void decodeNEC(rmt_item32_t *data, int numItems) {
	if (!isInRange(data[0], 9000, 4500, 200)) {
		//ESP_LOGI(tag, "Not an NEC");
		return;
	}
	int i;
	uint8_t address = 0, notAddress = 0, command = 0, notCommand = 0;
	int accumCounter = 0;
	uint8_t accumValue = 0;
	for (i=1; i<numItems; i++) {
		if (NEC_is0(data[i])) {
			//ESP_LOGI(tag, "%d: 0", i);
			accumValue = accumValue >> 1;
		} else if (NEC_is1(data[i])) {
			//ESP_LOGI(tag, "%d: 1", i);
			accumValue = (accumValue >> 1) | 0x80;
		} else {
			//ESP_LOGI(tag, "Unknown");
		}
		if (accumCounter == 7) {
			accumCounter = 0;
			//ESP_LOGI(tag, "Byte: 0x%.2x", accumValue);
			if (i==8) {
				address = accumValue;
			} else if (i==16) {
				notAddress = accumValue;
			} else if (i==24) {
				command = accumValue;
			} else if (i==32) {
				notCommand = accumValue;
			}
			accumValue = 0;
		} else {
			accumCounter++;
		}
	}
	//ESP_LOGI(tag, "Address: 0x%.2x, NotAddress: 0x%.2x", address, notAddress ^ 0xff);
	if (address != (notAddress ^ 0xff) || command != (notCommand ^ 0xff)) {
		//ESP_LOGI(tag, "Data mis match");
		return;
	}
	//ESP_LOGI(tag, "Address: 0x%.2x, Command: 0x%.2x", address, command);
}


static void task_watchRingbuf(void *ignore) {
	size_t itemSize;
	//ESP_LOGI(tag, "Watching ringbuf: %d", TICK_10_US);
	while(1) 
	{
		gpio_set_level(GPIO_NUM_17, 0);
		void *data = xRingbufferReceive(ringBuf, &itemSize, portMAX_DELAY);
		gpio_set_level(GPIO_NUM_17, 1);
		int item32Size = itemSize / sizeof(rmt_item32_t);
		rmt_item32_t *item32 = (rmt_item32_t *)data;
		// ESP_LOGI(tag, "Got an ringbuf item!  item size:%d, item32_t size:%d", itemSize, item32Size);

		// rmt_item32_t *p = (rmt_item32_t *)data;
		// for (int i=0; i<item32Size; i++) 
		// {
			// // ESP_LOGI(tag, "[0]: %d-%d us, [1]: %d-%d us", p->level0, p->duration0 * 10 / TICK_10_US, p->level1, p->duration1 * 10 / TICK_10_US);
			// ESP_LOGI(tag, "M: %d us, S: %d us", p->duration0 * 10 / TICK_10_US, p->duration1 * 10 / TICK_10_US);
			// p++;
		// }

		//decodeNEC(item32, item32Size);
		
		decode_results_t results;
		if(decodePanasonic(item32, item32Size, &results))
		{
			//ESP_LOGI(tag, "[results] address: 0x%.4x, value: 0x%.8x, bits: %d", results.address, results.value, results.bits);
			
			web_radio_command_t command = C_BEGIN_COMMAND;
			
			switch(results.value)
			{
				case PANASONIC_ch_up:
					command = C_PREV;
					break;
					
				case PANASONIC_ch_down:
					command = C_NEXT;
					break;
					
				case PANASONIC_vol_up:
					command = C_VOL_UP;
					break;
					
				case PANASONIC_vol_down:
					command = C_VOL_DOWN;
					break;
					
				case PANASONIC_ok:
					command = C_T_PLAY_STOP;
					break;
					

				case PANASONIC_0:
					command = C_LIST0;
					break;
				case PANASONIC_1:
					command = C_LIST1;
					break;
				case PANASONIC_2:
					command = C_LIST2;
					break;
				case PANASONIC_3:
					command = C_LIST3;
					break;
				case PANASONIC_4:
					command = C_LIST4;
					break;
				case PANASONIC_5:
					command = C_LIST5;
					break;
				case PANASONIC_6:
					command = C_LIST6;
					break;
				case PANASONIC_7:
					command = C_LIST7;
					break;
				case PANASONIC_8:
					command = C_LIST8;
					break;
				case PANASONIC_9:
					command = C_LIST9;
					break;
									
				default:
					break;
			}
			
			if(command != C_BEGIN_COMMAND)
			{
				//ESP_LOGI(tag, "wrap value: 0x%.8x to command: 0x%.2x", results.value, command);		
				web_radio_command(command);
			}
			else
			{
				ESP_LOGI(tag, "not wrap value: 0x%.8x", results.value);		
			}
		}
		else
		{
			ESP_LOGI(tag, "Not a Panasonic");
		}
		vRingbufferReturnItem(ringBuf, data);
	}
	vTaskDelete(NULL);
}

void runRmt() {
	ESP_LOGI(tag, ">> runRmtTest");
	
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO21
    io_conf.pin_bit_mask = (1 << GPIO_NUM_21);
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
	
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = (1 << GPIO_NUM_17);
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);

	rmt_config_t config;
	config.rmt_mode = RMT_MODE_RX;
	config.channel = RMT_CHANNEL_0;
	config.gpio_num = 21;
	config.mem_block_num = 2;
	config.rx_config.filter_en = 1;
	config.rx_config.filter_ticks_thresh = FILTER_THRESHOLD; 
	config.rx_config.idle_threshold = IDLE_THRESHOLD;
	config.clk_div = CLK_DIV;

	ESP_ERROR_CHECK(rmt_config(&config));
	ESP_ERROR_CHECK(rmt_driver_install(config.channel, 5000, 0));
	rmt_get_ringbuf_handler(RMT_CHANNEL_0, &ringBuf);
	dumpStatus(config.channel);
	xTaskCreatePinnedToCore(&task_watchRingbuf, "task_watchRingbuf", 2048, NULL, 5, NULL, 0);
	rmt_rx_start(RMT_CHANNEL_0, 1);
	//ESP_LOGI(tag, "FILTER_THRESHOLD: %d, IDLE_THRESHOLD: %d", FILTER_THRESHOLD, IDLE_THRESHOLD);
	//ESP_LOGI(tag, "FILTER_THRESHOLD: %d, IDLE_THRESHOLD: %d", config.rx_config.filter_ticks_thresh, config.rx_config.idle_threshold);
	//ESP_LOGI(tag, "<< runRmtTest");
}



void start_task_rmt_receiver() {
	runRmt();
}
