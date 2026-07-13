//
// Created by fish on 2026/1/6.
//

#pragma once

#include <stdint.h>
#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化板载蜂鸣器
 */
bsp_status_t bsp_buzzer_init();

/**
 * 使蜂鸣器发出指定频率和占空比的声音
 * @param freq 频率 (Hz)
 * @param blank 占空比 (0 <= blank <= 1)
 */
bsp_status_t bsp_buzzer_alarm(float freq, float blank);

/**
 * 关闭蜂鸣器
 */
bsp_status_t bsp_buzzer_quiet();

/**
 * 阻塞式地使蜂鸣器以指定频率和占空比发声持续一段时间
 * @param freq 频率
 * @param blank 占空比
 * @param duration 持续时间 (ms)
 */
bsp_status_t bsp_buzzer_flash(float freq, float blank, uint32_t duration);

#ifdef __cplusplus
}
#endif
