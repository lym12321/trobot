//
// Created by fish on 2026/1/25.
//

#ifndef TROBOT_VBUS_H
#define TROBOT_VBUS_H

#ifdef __cplusplus
extern "C" {
#endif

void bsp_adc_init(void);
float bsp_adc_vbus(void);

#ifdef __cplusplus
}
#endif

#endif //TROBOT_VBUS_H