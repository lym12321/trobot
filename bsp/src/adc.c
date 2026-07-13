//
// Created by fish on 2026/1/25.
//

#include "bsp/adc.h"
#include "adc.h"
#include "bsp/def.h"
#include <stdint.h>

static volatile uint16_t val[2];

bsp_status_t bsp_adc_init(void) {
    HAL_StatusTypeDef hal = HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
    if (hal == HAL_OK) hal = HAL_ADC_Start_DMA(&hadc1, (uint32_t *) val, 2);
    return bsp_status_from_hal(hal);
}

float bsp_adc_vbus(void) {
    return (float) val[0] * 3.3f / 65535 * 11.0f;
}
