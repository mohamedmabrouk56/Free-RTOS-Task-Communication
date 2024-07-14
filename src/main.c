/*
* This file is part of the ṁOS++ distribution.
*   (https://github.com/micro-os-plus)
* Copyright (c) 2014 Liviu Ionescu.
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use,
* copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom
* the Software is furnished to do so, subject to the following
* conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/

/*==========================================================================
 * 								  Includes
 * =========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include "diag/trace.h"

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"

#define CCM_RAM __attribute__((section(".ccmram")))

/*==========================================================================
 * 								Macro Definition
 * =========================================================================*/
#define TASK_SIZE 1000
#define QUEUE_SIZE 10
#define NUM_SENDERS 3
#define NUM_PERIODS 6
#define MSGSIZE 30
#define RECEIVED_MESSAGES_MAX 1000
#define GET_RANDOM SenderPeriodLowerBoundary[TimerCurrentIndex] + (rand() % (SenderPeriodHigherBoundary[TimerCurrentIndex] - SenderPeriodLowerBoundary[TimerCurrentIndex]))

 /*==========================================================================
  * 							Function Prototype
  * =========================================================================*/
  /* Sender Functions */
static void vSenderTask(void* pvParameters);
static void vSenderTimerCallBack1(TimerHandle_t xTimerSender);
static void vSenderTimerCallBack2(TimerHandle_t xTimerSender);
static void vSenderTimerCallBack3(TimerHandle_t xTimerSender);

/* Reciever Function */
static void vReceiverTask(void* pvParameters);
static void vReceiverTimerCallBack(TimerHandle_t xTimerReciever);

/* Systems Functions */
void vResetSystem(void);
void vInitialSetup(void);

/*==========================================================================
 * 								Global Variables
 * =========================================================================*/
 /* Queue GLobal Variable */
static QueueHandle_t testQueue;

/* Tasks Software Timers */
static TimerHandle_t xSenderTimer[NUM_SENDERS] = { NULL };
static TimerHandle_t xReceiverTimer = NULL;

/* Tasks Semaphores */
static SemaphoreHandle_t Semaphore_Sender[NUM_SENDERS];
static SemaphoreHandle_t Semaphore_Receiver;

/* Timer Boundary */
int TimerCurrentIndex = 0;
int SenderPeriodLowerBoundary[NUM_PERIODS] = { 50, 80, 110, 140, 170, 200 };
int SenderPeriodHigherBoundary[NUM_PERIODS] = { 150, 200, 250, 300, 350, 400 };

/* Counters */
int taskSentMessages[NUM_SENDERS] = { 0 };
int totalSentMessages = 0;
int taskBlockedMessages[NUM_SENDERS] = { 0 };
int totalBlockedMessages = 0;
int totalMessages[NUM_SENDERS] = { 0 };
int totalReceivedMessages = 0;
int TotalCurrentTickTime[NUM_SENDERS] = { 0 };

//
// Semihosting STM32F4 empty sample (trace via DEBUG).
//
// Trace support is enabled by adding the TRACE macro definition.
// By default the trace messages are forwarded to the DEBUG output,
// but can be rerouted to any device or completely suppressed, by
// changing the definitions required in system/src/diag/trace-impl.c
// (currently OS_USE_TRACE_ITM, OS_USE_TRACE_SEMIHOSTING_DEBUG/_STDOUT).
//

 // Sample pragmas to cope with warnings. Please note the related line at
 // the end of this function, used to pop the compiler diagnostics status.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wreturn-type"

/*==========================================================================
 * 									Main
 * =========================================================================*/
 /*
  * Name        : main
  * Description :
  * 1. Initialize the system and create semaphores
  * 2. Create the message queue
  * 3. Create sender and receiver tasks
  * 4. Create and start sender and receiver timers
  * 5. Start the scheduler
  */
int main(int argc, char* argv[])
{
	/* Creates semaphore */
	vInitialSetup();
	trace_printf("Start running");
	/* Create the Queue */
	testQueue = xQueueCreate(QUEUE_SIZE, sizeof(char) * MSGSIZE);
	if (testQueue != NULL)
	{
		/* Create Sender Tasks */
		xTaskCreate(vSenderTask, "Sender1", TASK_SIZE, (void*)(0), 1, NULL);
		xTaskCreate(vSenderTask, "Sender2", TASK_SIZE, (void*)(1), 1, NULL);
		xTaskCreate(vSenderTask, "Sender3", TASK_SIZE, (void*)(2), 2, NULL);
		/* Create Receiver Task */
		xTaskCreate(vReceiverTask, "Reciever1", TASK_SIZE, NULL, 3, NULL);

		/* Create Sender Timers */
		int RandomSender1Period = GET_RANDOM;
		TotalCurrentTickTime[0] += RandomSender1Period;
		xSenderTimer[0] = xTimerCreate("Sender1Timer", (pdMS_TO_TICKS(RandomSender1Period)), pdTRUE, (void*)(0), vSenderTimerCallBack1);
		int RandomSender2Period = GET_RANDOM;
		TotalCurrentTickTime[1] += RandomSender2Period;
		xSenderTimer[1] = xTimerCreate("Sender2Timer", (pdMS_TO_TICKS(RandomSender2Period)), pdTRUE, (void*)(1), vSenderTimerCallBack2);
		int RandomSender3Period = GET_RANDOM;
		TotalCurrentTickTime[2] += RandomSender3Period;
		xSenderTimer[2] = xTimerCreate("Sender3Timer", (pdMS_TO_TICKS(RandomSender3Period)), pdTRUE, (void*)(2), vSenderTimerCallBack3);
		/* Create Receiver Timer */
		xReceiverTimer = xTimerCreate("RecieverTimer", (pdMS_TO_TICKS(100)), pdTRUE, (void*)(3), vReceiverTimerCallBack);

		/* Status Variable to check if timer started */
		BaseType_t xTimer1Started = pdFAIL, xTimer2Started = pdFAIL, xTimer3Started = pdFAIL, xTimer4Started = pdFAIL;
		/* Check if The Software Timers Created Successfully */
		if ((xSenderTimer[0] != NULL) && (xSenderTimer[1] != NULL) && (xSenderTimer[2] != NULL) && (xReceiverTimer != NULL))
		{
			/* Update Timer Starting Status */
			xTimer1Started = xTimerStart(xSenderTimer[0], 0);
			xTimer2Started = xTimerStart(xSenderTimer[1], 0);
			xTimer3Started = xTimerStart(xSenderTimer[2], 0);
			xTimer4Started = xTimerStart(xReceiverTimer, 0);
		}

		/* Check if Timer Started Then Start Scheduler */
		if (xTimer1Started == pdPASS && xTimer2Started == pdPASS && xTimer3Started == pdPASS && xTimer4Started == pdPASS)
		{
			vTaskStartScheduler();
		}

	}
	else
	{
		/* Queue not Created */
	}
	return 0;
}

#pragma GCC diagnostic pop


void vApplicationMallocFailedHook(void)
{
	/* Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for (;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char* pcTaskName)
{
	(void)pcTaskName;
	(void)pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected. */
	for (;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook(void)
{
	volatile size_t xFreeStackSpace;

	/* This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amout of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if (xFreeStackSpace > 100)
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}

void vApplicationTickHook(void) {
}

StaticTask_t xIdleTaskTCB CCM_RAM;
StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] CCM_RAM;

void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer, StackType_t** ppxIdleTaskStackBuffer, uint32_t* pulIdleTaskStackSize) {
	/* Pass out a pointer to the StaticTask_t structure in which the Idle task's
	state will be stored. */
	*ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

	/* Pass out the array that will be used as the Idle task's stack. */
	*ppxIdleTaskStackBuffer = uxIdleTaskStack;

	/* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
	Note that, as the array is necessarily of type StackType_t,
	configMINIMAL_STACK_SIZE is specified in words, not bytes. */
	*pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t xTimerTaskTCB CCM_RAM;
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH] CCM_RAM;

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t** ppxTimerTaskTCBBuffer, StackType_t** ppxTimerTaskStackBuffer, uint32_t* pulTimerTaskStackSize) {
	*ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
	*ppxTimerTaskStackBuffer = uxTimerTaskStack;
	*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/*==========================================================================
 * 							Function Defination
 * =========================================================================*/

/*
 * Name        : vSenderTask
 * Description :
 * 1. Wait for semaphore to be taken
 * 2. Get the current tick count
 * 3. Write and send a message to the queue
 * 4. Update message statistics
 * 5. Change the period of the timer
 * Inputs      : void* pvParameters - Task index passed as a parameter
 * Outputs     : NONE
 */
static void vSenderTask(void* pvParameters)
{
	/* Message Variable */
	char msg[MSGSIZE];
	int currentTickTime;
	int SenderTaskIndex = (int)pvParameters;

	while (1)
	{
		/* Wait indefinitely until the semaphore for this sender task is taken */
		if (xSemaphoreTake(Semaphore_Sender[SenderTaskIndex], portMAX_DELAY))
		{
			/* Get the current tick count */
			currentTickTime = xTaskGetTickCount();
			sprintf(msg, "Send Time is %lu", currentTickTime);

			/* Try to send the message to the queue */
			if (xQueueSend(testQueue, &msg, 0) == pdPASS)
			{
				taskSentMessages[SenderTaskIndex]++;
				totalSentMessages++;
			}
			else
			{
				taskBlockedMessages[SenderTaskIndex]++;
				totalBlockedMessages++;
			}
			totalMessages[SenderTaskIndex]++;
		}

		/* Generate a Random sender period */
		int RandomSenderPeriod = GET_RANDOM;
		TotalCurrentTickTime[SenderTaskIndex] += RandomSenderPeriod;

		/* Change the Period of the timer associated with this sender task to the Measured Random Period */
		xTimerChangePeriod(xSenderTimer[SenderTaskIndex], pdMS_TO_TICKS(RandomSenderPeriod), 0);
	}
}

/*
 * Name        : vSenderTimerCallBack1
 * Description : Callback function for the first sender timer
 * Inputs      : TimerHandle_t xTimerSender - Timer handle
 * Outputs     : NONE
 */
static void vSenderTimerCallBack1(TimerHandle_t xTimerSender)
{
	xSemaphoreGive(Semaphore_Sender[0]);
	/* trace_printf("callback1"); */
}

/*
 * Name        : vSenderTimerCallBack2
 * Description : Callback function for the second sender timer
 * Inputs      : TimerHandle_t xTimerSender - Timer handle
 * Outputs     : NONE
 */
static void vSenderTimerCallBack2(TimerHandle_t xTimerSender)
{
	xSemaphoreGive(Semaphore_Sender[1]);
	/* trace_printf("callback2"); */
}

/*
 * Name        : vSenderTimerCallBack3
 * Description : Callback function for the third sender timer
 * Inputs      : TimerHandle_t xTimerSender - Timer handle
 * Outputs     : NONE
 */
static void vSenderTimerCallBack3(TimerHandle_t xTimerSender)
{
	xSemaphoreGive(Semaphore_Sender[2]);
	/* trace_printf("callback3"); */
}


/*
 * Name        : vReceiverTask
 * Description :
 * 1. Wait for semaphore to be taken
 * 2. Recieve a message from the queue
 * 3. Print the message and update statistics
 * 4. Check if the maximum number of received messages has been reached and reset the system if so
 * Inputs      : void* pvParameters - Unused in this function
 * Outputs     : NONE
 */
static void vReceiverTask(void* pvParameters)
{
	/* Message Variable */
	char msg[MSGSIZE];

	while (1)
	{
		if (xSemaphoreTake(Semaphore_Receiver, portMAX_DELAY))
		{
			if (xQueueReceive(testQueue, msg, 0) == pdPASS)
			{
				/* Print received message */
				trace_printf("%s\n", msg);
				totalReceivedMessages++;
			}

			/* Check if the maximum number of received messages has been reached */
			if (totalReceivedMessages >= RECEIVED_MESSAGES_MAX)
			{
				vResetSystem();
			}
		}
	}
}



/*
 * Name        : vReceiverTimerCallBack
 * Description : Callback function for the receiver timer
 * Inputs      : TimerHandle_t xTimerReceiver - Timer handle
 * Outputs     : NONE
 */
static void vReceiverTimerCallBack(TimerHandle_t xTimerReceiver)
{
	xSemaphoreGive(Semaphore_Receiver);
}



/*
 * Name        : vResetSystem
 * Description :
 * 1. Print all counters
 * 2. Reset counters
 * 3. Reset the queue to start a new period
 * 4. Choose the next period boundaries
 * 5. Check if all periods are done and stop all timers
 * Inputs      : NONE
 * Outputs     : NONE
 */
void vResetSystem(void)
{
	/* Print All Counters */
	trace_printf("Total Sent Messages: %d\n", totalSentMessages);
	trace_printf("Total Blocked Messages: %d\n", totalBlockedMessages);
	trace_printf("Total Messages: %d\n", totalMessages[0] + totalMessages[1] + totalMessages[2]);
	trace_printf("Total Received Messages: %d\n", totalReceivedMessages);

	for (int i = 0; i < NUM_SENDERS; i++)
	{
		trace_printf("Avg Time of Task %d : %d of Period %d \n", i + 1, (TotalCurrentTickTime[i] / totalMessages[i]), TimerCurrentIndex);
		trace_printf("Sender Task %d: Sent %d, Blocked %d\n", i + 1, taskSentMessages[i], taskBlockedMessages[i]);

		/* Reset task-specific counters */
		taskSentMessages[i] = 0;
		taskBlockedMessages[i] = 0;
		TotalCurrentTickTime[i] = 0;
		totalMessages[i] = 0;
	}

	/* Reset Total Counters */
	totalSentMessages = 0;
	totalBlockedMessages = 0;
	totalReceivedMessages = 0;

	/* Reset Queue to Start New Period */
	xQueueReset(testQueue);

	/* Choose Next Period Boundaries */
	TimerCurrentIndex++;

	/* Check if all Periods Are Done */
	if (TimerCurrentIndex >= NUM_PERIODS)
	{
		/* Stop All Timers */
		xTimerStop(xSenderTimer[0], 0);
		xTimerStop(xSenderTimer[1], 0);
		xTimerStop(xSenderTimer[2], 0);
		xTimerStop(xReceiverTimer, 0);

		/* Ending Program */
		trace_printf("Game Over\n");
		exit(0);
	}
}

/*
 * Name        : vInitialSetup
 * Description :
 * 1. Create semaphores for sender and receiver tasks
 * Inputs      : NONE
 * Outputs     : NONE
 */
void vInitialSetup(void)
{
	for (int i = 0; i < NUM_SENDERS; i++)
	{
		/* Creating Sender Task Semaphores */
		Semaphore_Sender[i] = xSemaphoreCreateBinary();
	}

	/* Creating Receiver Task Semaphore */
	Semaphore_Receiver = xSemaphoreCreateBinary();
}
