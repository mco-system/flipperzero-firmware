#include <api-hal-os.h>
#include <api-hal-os-timer.h>
#include <api-hal-power.h>

#include <FreeRTOS.h>
#include <cmsis_os.h>

#define API_HAL_OS_CLK_FREQUENCY 32768
#define API_HAL_OS_TICK_PER_SECOND 1024
#define API_HAL_OS_CLK_PER_TICK (API_HAL_OS_CLK_FREQUENCY / API_HAL_OS_TICK_PER_SECOND)
#define API_HAL_OS_TICK_PER_EPOCH (API_HAL_OS_TIMER_MAX / API_HAL_OS_CLK_PER_TICK)
#define API_HAL_OS_MAX_SLEEP (API_HAL_OS_TICK_PER_EPOCH - 1)

#ifdef API_HAL_OS_DEBUG
#include <stm32wbxx_ll_gpio.h>
#define LED_GREEN_PORT GPIOA
#define LED_GREEN_PIN LL_GPIO_PIN_7
#endif

typedef struct {
    // Tick counters
    volatile uint32_t in_sleep;
    volatile uint32_t in_awake;
    // Error counters
    volatile uint32_t sleep_error;
    volatile uint32_t awake_error;
} ApiHalOs;

ApiHalOs api_hal_os = {
    .in_sleep = 0,
    .in_awake = 0,
    .sleep_error = 0,
    .awake_error = 0,
};

void api_hal_os_init() {
    api_hal_os_timer_init();
    LL_DBGMCU_APB1_GRP2_FreezePeriph(LL_DBGMCU_APB1_GRP2_LPTIM2_STOP);

    LL_LPTIM_EnableIT_CMPM(API_HAL_OS_TIMER);
    LL_LPTIM_EnableIT_ARRM(API_HAL_OS_TIMER);

    LL_LPTIM_SetAutoReload(API_HAL_OS_TIMER, API_HAL_OS_TIMER_MAX);
    LL_LPTIM_SetCompare(API_HAL_OS_TIMER, API_HAL_OS_CLK_PER_TICK);

    LL_LPTIM_StartCounter(API_HAL_OS_TIMER, LL_LPTIM_OPERATING_MODE_CONTINUOUS);
}

void LPTIM2_IRQHandler(void) {
    // Autoreload
    const bool arrm_flag = LL_LPTIM_IsActiveFlag_ARRM(API_HAL_OS_TIMER);
    if(arrm_flag) {
        LL_LPTIM_ClearFLAG_ARRM(API_HAL_OS_TIMER);
    }
    if(LL_LPTIM_IsActiveFlag_CMPM(API_HAL_OS_TIMER)) {
        LL_LPTIM_ClearFLAG_CMPM(API_HAL_OS_TIMER);

        // Store important value
        uint16_t cnt = api_hal_os_timer_get_cnt();
        uint16_t cmp = api_hal_os_timer_get_cmp();
        uint16_t current_tick = cnt / API_HAL_OS_CLK_PER_TICK;
        uint16_t compare_tick = cmp / API_HAL_OS_CLK_PER_TICK;

        // Calculate error
        // happens when HAL or other high priority IRQ takes our time
        int32_t error = (int32_t)compare_tick - current_tick;
        api_hal_os.awake_error += ((error>0) ? error : -error);

        // Calculate and set next tick 
        uint16_t next_tick = current_tick + 1;
        api_hal_os_timer_set_cmp(next_tick * API_HAL_OS_CLK_PER_TICK);

        // Notify OS
        api_hal_os.in_awake ++;
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
            xPortSysTickHandler();
        }
    }
}

static inline uint32_t api_hal_os_sleep(TickType_t expected_idle_ticks) {
    // Store important value before going to sleep
    const uint16_t before_cnt = api_hal_os_timer_get_cnt();
    const uint16_t before_tick = before_cnt / API_HAL_OS_CLK_PER_TICK;

    // Calculate and set next wakeup compare value
    const uint16_t expected_cnt = (before_tick + expected_idle_ticks - 2) * API_HAL_OS_CLK_PER_TICK;
    api_hal_os_timer_set_cmp(expected_cnt);

    HAL_SuspendTick();
    // Go to stop2 mode
#ifdef API_HAL_OS_DEBUG
    LL_GPIO_SetOutputPin(LED_GREEN_PORT, LED_GREEN_PIN);
#endif
    api_hal_power_deep_sleep();
#ifdef API_HAL_OS_DEBUG
    LL_GPIO_ResetOutputPin(LED_GREEN_PORT, LED_GREEN_PIN);
#endif

    HAL_ResumeTick();

    // Spin till we are in timer safe zone
    while(!api_hal_os_timer_is_safe()) {}

    // Store current counter value, calculate current tick
    const uint16_t after_cnt = api_hal_os_timer_get_cnt();
    const uint16_t after_tick = after_cnt / API_HAL_OS_CLK_PER_TICK;

    // Store and clear interrupt flags
    // we don't want handler to be called after renabling IRQ
    bool arrm_flag = LL_LPTIM_IsActiveFlag_ARRM(API_HAL_OS_TIMER);

    // Calculate and set next wakeup compare value
    const uint16_t next_cmp = (after_tick + 1) * API_HAL_OS_CLK_PER_TICK;
    api_hal_os_timer_set_cmp(next_cmp);

    // Calculate ticks count spent in sleep and perform sanity checks
    int32_t completed_ticks = arrm_flag ? (int32_t)before_tick - after_tick : (int32_t)after_tick - before_tick;

    return completed_ticks;
}

void vPortSuppressTicksAndSleep(TickType_t expected_idle_ticks) {
    if (!api_hal_power_deep_available()) {
        return;
    }

    // Limit mount of ticks to maximum that timer can count
    if (expected_idle_ticks > API_HAL_OS_MAX_SLEEP) {
        expected_idle_ticks = API_HAL_OS_MAX_SLEEP;
    }

    // Stop IRQ handling, no one should disturb us till we finish 
    __disable_irq();

    // Confirm OS that sleep is still possible
    // And check if timer is in safe zone
    // (8 clocks till any IRQ event or ongoing synchronization)
    if (eTaskConfirmSleepModeStatus() == eAbortSleep
        || !api_hal_os_timer_is_safe()) {
        __enable_irq();
        return;
    }

    uint32_t completed_ticks = api_hal_os_sleep(expected_idle_ticks);
    assert(completed_ticks >= 0);

    // Reenable IRQ
    __enable_irq();

    // Notify system about time spent in sleep
    if (completed_ticks > 0) {
        api_hal_os.in_sleep += completed_ticks;
        if (completed_ticks > expected_idle_ticks) {
            // We are late, count error
            api_hal_os.sleep_error += (completed_ticks - expected_idle_ticks);
            // Freertos is not happy when we overleep
            // But we are not going to tell her
            vTaskStepTick(expected_idle_ticks);
        } else {
            vTaskStepTick(completed_ticks);
        }
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName) {
    asm("bkpt 1");
    while(1) {};
}