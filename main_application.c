/* Standard includes. */
#include <stdio.h>
#include <conio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

/* Hardware simulator utility functions */
#include "HW_access.h"

/* SERIAL SIMULATOR CHANNEL TO USE */
#define COM_CH (0)

/* TASK PRIORITIES */
#define	TASK_SERIAL_SEND_PRI		(2 + tskIDLE_PRIORITY  )
#define TASK_SERIAL_REC_PRI			(3+ tskIDLE_PRIORITY )
#define	SERVICE_TASK_PRI		(1+ tskIDLE_PRIORITY )

/* TASKS: FORWARD DECLARATIONS */
void prvSerialReceiveTask_0(void* pvParameters);
void prvSerialReceiveTask_1(void* pvParameters);

SemaphoreHandle_t RXC_BS_0, RXC_BS_1;

/* SERIAL SIMULATOR CHANNEL TO USE */
#define COM_CH_0 (0)
#define COM_CH_1 (1)

/* TASKS: FORWARD DECLARATIONS */
void led_bar_tsk(void* pvParameters);
void SerialSend_Task(void* pvParameters);

void vApplicationIdleHook(void);

/* TRASNMISSION DATA - CONSTANT IN THIS APPLICATION */
const char trigger[] = "Pozdrav svima\n";
unsigned volatile t_point;

/* RECEPTION DATA BUFFER */
#define R_BUF_SIZE (32)
uint8_t r_buffer[R_BUF_SIZE];
unsigned volatile r_point;

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */
static const unsigned char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
								0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

/* GLOBAL OS-HANDLES */
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore;
TimerHandle_t tH1;
TimerHandle_t checkIdleCounterTimer;
uint64_t idleHookCounter;

uint32_t OnLED_ChangeInterrupt() {
	// Ovo se desi kad neko pritisne dugme na LED Bar tasterima
	BaseType_t higherPriorityTaskWoken = pdFALSE;

	xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &higherPriorityTaskWoken);

	portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

/* RXC - RECEPTION COMPLETE - INTERRUPT HANDLER */
static uint32_t prvProcessRXCInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	if (get_RXC_status(0) != 0)
		xSemaphoreGiveFromISR(RXC_BS_0, &xHigherPTW);

	if (get_RXC_status(1) != 0)
		xSemaphoreGiveFromISR(RXC_BS_1, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}



void led_bar_tsk(void* pvParams) {

	configASSERT(pvParams);

	for (;;) {
		// Ovo postane dostupno kad neko pritisne taster
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);

		uint8_t tmp;
		get_LED_BAR(5, &tmp);


		configASSERT(!set_LED_BAR(2, ~tmp));
	}
}

void prvSerialReceiveTask_0(void* pvParameters)
{
	uint8_t cc = 0;

	while (1)
	{
		xSemaphoreTake(RXC_BS_0, portMAX_DELAY);
		get_serial_character(COM_CH_0, &cc);
		printf("Kanal 0: %c\n", cc);
		select_7seg_digit(2);
		set_7seg_digit(hexnum[cc >> 4]);
		select_7seg_digit(3);
		set_7seg_digit(hexnum[cc & 0x0F]);
		send_serial_character(COM_CH_0, cc + 2);
	}
}

void prvSerialReceiveTask_1(void* pvParameters)
{
	uint8_t cc = 0;

	while (1)
	{
		xSemaphoreTake(RXC_BS_1, portMAX_DELAY);
		get_serial_character(COM_CH_1, &cc);
		printf("Kanal 1: %c\n", cc);
		select_7seg_digit(0);
		set_7seg_digit(hexnum[cc >> 4]);
		select_7seg_digit(1);
		set_7seg_digit(hexnum[cc & 0x0F]);
		send_serial_character(COM_CH_1, cc + 1);
	}
}

void SerialSend_Task(void* pvParameters)
{
	configASSERT(pvParameters);
	t_point = 0;
	for (;;)
	{
		
		if (t_point > (sizeof(trigger) - 1))
			t_point = 0;
		send_serial_character(0, trigger[t_point]);
		vTaskDelay(1000);
		send_serial_character(1, trigger[t_point++]+1);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
		vTaskDelay(pdMS_TO_TICKS(200)); // kada se koristi vremenski delay }
	}
}

static void TimerCallback( TimerHandle_t xTimer ) {

	configASSERT(xTimer);

	static uint8_t bdt = 0;
	configASSERT(!set_LED_BAR(2, 0x00));//sve LEDovke iskljucene
	configASSERT(!set_LED_BAR(3, 0xF0));// gornje 4 LEDovke ukljucene
	configASSERT(!set_LED_BAR(0, bdt)); // ukljucena LED-ovka se pomera od dole ka gore
	//samo da apdejtujem kod
	bdt <<= 1;
	if (bdt == 0)
		bdt = 1;
	
	for (size_t i = 0; i < 1000; i++)
	{
		for (size_t j = 0; j < 1000; j++)
		{
			size_t a = i;
			a = a;
		}
	}

}

static void checkIdleCountTimerFun(const TimerHandle_t xTimer) {

	configASSERT(xTimer);

	static uint8_t avg_counter = 0;
	static uint64_t cnt_sum = 0;

	
	cnt_sum += idleHookCounter;
	idleHookCounter = 0;


	if (avg_counter++ == 9) {
		//printf("Prosecni IdleHook counter je: %lld\n", cnt_sum / 10);

		// Idle Hook bez dodatnih taskova je: ~2500000

		uint64_t average = cnt_sum / 10;

		float odnos = (float)average / 2500000;
		if (odnos > 1) {
			odnos = 1;
		}
		int procenat = (int)((float)odnos * 100);

		printf("Zauzetost procesora u je: %d\n", 100-procenat);

		cnt_sum = 0;
		avg_counter = 0;
	}

}

/* MAIN - SYSTEM STARTUP POINT */
void main_demo(void)
{
	
	configASSERT(init_LED_comm() == 0);
	init_serial_uplink(COM_CH); // inicijalizacija serijske TX na kanalu 0
	init_serial_downlink(COM_CH);// inicijalizacija serijske RX na kanalu 0
	init_serial_uplink(1); // inicijalizacija serijske TX na kanalu 0
	init_serial_downlink(1);// ini
	
	init_7seg_comm();

	/* ON INPUT CHANGE INTERRUPT HANDLER */
	//vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);
	/* SERIAL RECEPTION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);

	/* Create LED interrapt semaphore */
	LED_INT_BinarySemaphore = xSemaphoreCreateBinary();
	RXC_BinarySemaphore = xSemaphoreCreateBinary();
	/* led bar TASK */
	configASSERT(xTaskCreate(led_bar_tsk, "ST", configMINIMAL_STACK_SIZE, (void*)1, SERVICE_TASK_PRI, NULL));

	/* SERIAL RECEIVER TASK */
	xTaskCreate(prvSerialReceiveTask_0, "SR0", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_REC_PRI, NULL);

	/* SERIAL RECEIVER TASK */
	xTaskCreate(prvSerialReceiveTask_1, "SR1", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_REC_PRI, NULL);
	/* Create TBE semaphores - serial transmit comm */
	RXC_BS_0 = xSemaphoreCreateBinary();
	RXC_BS_1 = xSemaphoreCreateBinary();
	r_point = 0;
	/* SERIAL TRANSMITTER TASK */
	configASSERT(xTaskCreate(SerialSend_Task, "STx", configMINIMAL_STACK_SIZE, (void*)1, TASK_SERIAL_SEND_PRI, NULL));

	//// Timers
	//tH1 = xTimerCreate(
	//	"Timer LED",
	//	pdMS_TO_TICKS(500),
	//	pdTRUE,
	//	0,
	//	TimerCallback
	//);
	//configASSERT(tH1);
	//configASSERT(xTimerStart(tH1, 0));

	// Timers
	checkIdleCounterTimer = xTimerCreate(
		"Timer Check Idle Count",
		pdMS_TO_TICKS(100),
		pdTRUE,
		0,
		checkIdleCountTimerFun
	);
	configASSERT(checkIdleCounterTimer);
	configASSERT(
		xTimerStart(checkIdleCounterTimer, 0)
	);


	vTaskStartScheduler();

	for (;;) {}
}

void vApplicationIdleHook(void) {

	idleHookCounter++;
}
