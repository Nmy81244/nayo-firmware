#include "system_at32f402_405.h"

void system_clock_config()
{
  crm_reset();

  crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK, TRUE);
  while(crm_flag_get(CRM_HICK_STABLE_FLAG) != SET) {
  }

  // HICK (8 MHz) * 54 / 2 = 216 MHz system clock.
  crm_pll_config(CRM_PLL_SOURCE_HICK, 54, 1, CRM_PLL_FP_2);

  crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
  while(crm_flag_get(CRM_PLL_STABLE_FLAG) != SET) {
  }

  crm_ahb_div_set(CRM_AHB_DIV_1);
  crm_apb1_div_set(CRM_APB1_DIV_2);
  crm_apb2_div_set(CRM_APB2_DIV_2);

  flash_psr_set(FLASH_WAIT_CYCLE_6);

  crm_sysclk_switch(CRM_SCLK_PLL);
  while(crm_sysclk_switch_status_get() != CRM_SCLK_PLL) {
  }
}

void system_init() {
  system_clock_config();
}
