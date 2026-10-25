// FreeRTOS SMP kernel config for the RP2350 (Cortex-M33, secure only).
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configNUMBER_OF_CORES                   2
#define configUSE_CORE_AFFINITY                 1
#define configRUN_MULTIPLE_PRIORITIES           1
#define configUSE_PASSIVE_IDLE_HOOK             0
#define configUSE_TIME_SLICING                  1

#define configENABLE_MPU                        0
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          1
#define configENABLE_FPU                        1
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    16

#define configCPU_CLOCK_HZ                      (clock_get_hz(clk_sys))
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                (256)
#define configUSE_PREEMPTION                    1
#define configUSE_16_BIT_TICKS                  0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_TRACE_FACILITY                0
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_EVENT_GROUPS                  1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xEventGroupSetBitFromISR        1
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            512

#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
// Task stacks and kernel objects only; the application uses malloc.
#define configTOTAL_HEAP_SIZE                   (48 * 1024)
#define configAPPLICATION_ALLOCATED_HEAP        0

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0
#define configASSERT(x)                         do { if (!(x)) { __asm volatile("bkpt #0"); for (;;) {} } } while (0)

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_xTaskGetSchedulerState          1

#endif // FREERTOS_CONFIG_H
