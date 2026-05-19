#include "mq2.h"


#define MQ2_SAMPLE_COUNT               30U
#define MQ2_ADC_MAX_VALUE              4095.0f
#define MQ2_ADC_REF_VOLTAGE            3.3f
#define MQ2_SENSOR_CIRCUIT_VOLTAGE     5.0f
#define MQ2_SENSOR_OUTPUT_SCALE        1.0f
#define MQ2_LOAD_RESISTANCE_KOHM       4.7f
#define MQ2_CLEAN_AIR_RS_R0_RATIO      9.83f
#define MQ2_LPG_CURVE_A                574.25f
#define MQ2_LPG_CURVE_B               (-2.222f)
#define MQ2_STARTUP_IGNORE_COUNT       5U
#define MQ2_CALIBRATION_SAMPLE_COUNT   MQ2_STARTUP_IGNORE_COUNT
#define MQ2_TREND_CLEAN_AIR_INDEX      100.0f
#define MQ2_TREND_ALARM_ON_INDEX       180.0f
#define MQ2_TREND_ALARM_OFF_INDEX      130.0f
#define MQ2_TREND_TRIGGER_COUNT        3U
#define MQ2_TREND_RECOVER_COUNT        10U
#define MQ2_MIN_VALID_VOLTAGE          0.01f
#define MQ2_EMA_ALPHA                  0.25f

typedef struct {
    float threshold_index;
    int8_t direction;
    uint8_t samples_required;
    uint8_t next_state;
} mq2_trend_rule_t;

typedef struct {
    float raw_adc;
    float adc_voltage;
    float sensor_voltage;
    float rs_kohm;
    float r0_kohm;
    float rs_r0_ratio;
    float lpg_ppm;
    float trend_index;
    uint8_t valid;
    uint8_t calibrated;
    uint8_t calibration_count;
    float calibration_r0_sum;
} mq2_state_t;

float adc_value = 0;    // ADC 平均原始值
float voltage = 0;      // 电压值
float RS = 0;           // 传感器电阻值
float RL = 4.7;         // 负载电阻值
float R0 = 10.0f;       // 清洁空气校准得到的基准电阻（单位：千欧）
float ppm = 0;          // LPG 曲线折算值，仅作趋势参考

uint32_t dma_buff[MQ2_SAMPLE_COUNT];  // DMA 缓冲区，用于存储 ADC 采样数据

static mq2_state_t mq2_state = {
    0.0f, 0.0f, 0.0f, 0.0f, 10.0f, MQ2_CLEAN_AIR_RS_R0_RATIO,
    0.0f, MQ2_TREND_CLEAN_AIR_INDEX, 0U, 0U, 0U, 0.0f
};
static uint8_t mq2_startup_count = 0U;
static uint8_t mq2_trend_alarm = 0U;
static uint8_t mq2_trend_rule_count = 0U;

static const mq2_trend_rule_t mq2_trend_rules[2] = {
    {MQ2_TREND_ALARM_ON_INDEX,  1, MQ2_TREND_TRIGGER_COUNT, 1U},
    {MQ2_TREND_ALARM_OFF_INDEX, -1, MQ2_TREND_RECOVER_COUNT, 0U}
};

/**
 * @brief       将数值限制为非负，避免异常 ADC 值扩散到显示和遥测
 * @param       value 输入值
 * @retval      非负值
 */
static float mq2_non_negative(float value)
{
    return (value > 0.0f) ? value : 0.0f;
}

/**
 * @brief       对 MQ2 趋势值做一阶低通，降低 DMA 窗口之间的抖动
 * @param       previous 上一次滤波值
 * @param       current  当前计算值
 * @retval      滤波后的趋势值
 */
static float mq2_ema(float previous, float current)
{
    return previous + ((current - previous) * MQ2_EMA_ALPHA);
}

/**
 * @brief       计算 ADC DMA 缓冲区的平均原始值
 * @param       无
 * @retval      12 位 ADC 平均值
 */
static float mq2_average_adc(void)
{
    uint8_t i;
    uint32_t sum = 0UL;

    for (i = 0U; i < MQ2_SAMPLE_COUNT; i++) {
        sum += dma_buff[i];
    }
    return (float)sum / (float)MQ2_SAMPLE_COUNT;
}

/**
 * @brief       根据负载电阻上的电压计算 MQ2 敏感体电阻
 * @param       sensor_voltage 负载电阻电压
 * @retval      传感器敏感体电阻，单位 kΩ
 */
static float mq2_compute_rs(float sensor_voltage)
{
    return ((MQ2_SENSOR_CIRCUIT_VOLTAGE * MQ2_LOAD_RESISTANCE_KOHM) / sensor_voltage) -
           MQ2_LOAD_RESISTANCE_KOHM;
}

/**
 * @brief       更新对外可观察的历史全局量，便于 SWD 断点和旧调用方查看
 * @param       无
 * @retval      无
 */
static void mq2_publish_state(void)
{
    adc_value = mq2_state.raw_adc;
    voltage = mq2_state.adc_voltage;
    RS = mq2_state.rs_kohm;
    RL = MQ2_LOAD_RESISTANCE_KOHM;
    R0 = mq2_state.r0_kohm;
    ppm = mq2_state.lpg_ppm;
}

/**
 * @brief       采集一次 DMA 均值并换算基础电学量
 * @param       无
 * @retval      1 有效，0 无效
 */
static uint8_t mq2_measure(void)
{
    mq2_state.raw_adc = mq2_average_adc();
    mq2_state.adc_voltage = (mq2_state.raw_adc / MQ2_ADC_MAX_VALUE) * MQ2_ADC_REF_VOLTAGE;
    mq2_state.sensor_voltage = mq2_state.adc_voltage * MQ2_SENSOR_OUTPUT_SCALE;

    mq2_state.valid = (mq2_state.sensor_voltage > MQ2_MIN_VALID_VOLTAGE) ? 1U : 0U;
    if (mq2_state.valid == 0U) {
        mq2_state.rs_kohm = 0.0f;
        mq2_state.rs_r0_ratio = 0.0f;
        mq2_state.lpg_ppm = 0.0f;
        mq2_state.trend_index = mq2_ema(mq2_state.trend_index, 0.0f);
        mq2_publish_state();
        return 0U;
    }

    mq2_state.rs_kohm = mq2_non_negative(mq2_compute_rs(mq2_state.sensor_voltage));
    mq2_publish_state();
    return 1U;
}

/**
 * @brief       上电清洁空气窗口内校准 R0，当前未使用 Flash 持久化
 * @param       无
 * @retval      1 已完成校准，0 仍在校准期
 */
static uint8_t mq2_update_calibration(void)
{
    float r0_sample;

    if (mq2_state.calibrated != 0U) {
        return 1U;
    }
    if (mq2_state.valid == 0U) {
        return 0U;
    }

    r0_sample = mq2_state.rs_kohm / MQ2_CLEAN_AIR_RS_R0_RATIO;
    if (r0_sample > 0.0f) {
        mq2_state.calibration_r0_sum += r0_sample;
        mq2_state.calibration_count++;
    }
    if (mq2_state.calibration_count < MQ2_CALIBRATION_SAMPLE_COUNT) {
        mq2_state.trend_index = mq2_ema(mq2_state.trend_index, MQ2_TREND_CLEAN_AIR_INDEX);
        mq2_state.lpg_ppm = 0.0f;
        mq2_publish_state();
        return 0U;
    }

    mq2_state.r0_kohm = mq2_state.calibration_r0_sum / (float)mq2_state.calibration_count;
    mq2_state.calibrated = 1U;
    mq2_publish_state();
    return 1U;
}

/**
 * @brief       由 Rs/R0 计算 LPG 折算值与归一化趋势指数
 * @param       无
 * @retval      无
 */
static void mq2_update_concentration(void)
{
    float raw_index;
    float raw_ppm;

    if ((mq2_state.r0_kohm <= 0.0f) || (mq2_state.rs_kohm <= 0.0f)) {
        mq2_state.rs_r0_ratio = 0.0f;
        mq2_state.lpg_ppm = 0.0f;
        mq2_state.trend_index = mq2_ema(mq2_state.trend_index, 0.0f);
        mq2_publish_state();
        return;
    }

    mq2_state.rs_r0_ratio = mq2_state.rs_kohm / mq2_state.r0_kohm;
    raw_index = (MQ2_CLEAN_AIR_RS_R0_RATIO / mq2_state.rs_r0_ratio) * MQ2_TREND_CLEAN_AIR_INDEX;
    raw_ppm = MQ2_LPG_CURVE_A * (float)pow(mq2_state.rs_r0_ratio, MQ2_LPG_CURVE_B);

    mq2_state.trend_index = mq2_ema(mq2_state.trend_index, mq2_non_negative(raw_index));
    mq2_state.lpg_ppm = mq2_non_negative(raw_ppm);
    mq2_publish_state();
}

/**
 * @brief       判断趋势值是否满足当前状态的迁移条件
 * @param       rule      当前状态对应的迁移规则
 * @param       value_index 最近一次 MQ2 趋势指数
 * @retval      1 满足条件，0 不满足
 */
static uint8_t mq2_trend_rule_matches(const mq2_trend_rule_t *rule, float value_index)
{
    return (((value_index - rule->threshold_index) * (float)rule->direction) >= 0.0f) ? 1U : 0U;
}

/**
 * @brief       根据 MQ2 趋势值更新迟滞报警状态
 * @param       value_index 最近一次 MQ2 趋势指数
 * @retval      无
 */
static void mq2_update_trend_alarm(float value_index)
{
    const mq2_trend_rule_t *rule;

    if (mq2_startup_count < MQ2_STARTUP_IGNORE_COUNT) {
        mq2_startup_count++;
        return;
    }

    rule = &mq2_trend_rules[mq2_trend_alarm ? 1U : 0U];
    if (mq2_trend_rule_matches(rule, value_index) == 0U) {
        mq2_trend_rule_count = 0U;
        return;
    }

    if (mq2_trend_rule_count < rule->samples_required) {
        mq2_trend_rule_count++;
    }
    if (mq2_trend_rule_count >= rule->samples_required) {
        mq2_trend_alarm = rule->next_state;
        mq2_trend_rule_count = 0U;
    }
}

/**
 * @brief MQ2 传感器任务函数(推荐，基于 DMA 的方法)
 *  
 * @param None
 * @retval None
 */
void mq2_task(void)
{
    if (mq2_measure() == 0U) {
        mq2_update_trend_alarm(mq2_state.trend_index);
        return;
    }
    if (mq2_update_calibration() == 0U) {
        mq2_update_trend_alarm(mq2_state.trend_index);
        return;
    }

    mq2_update_concentration();
    mq2_update_trend_alarm(mq2_state.trend_index);
}

/**
 * @brief       获取最近一次 MQ2 LPG 曲线折算值
 * @param       无
 * @retval      LPG 折算 ppm，仅作趋势参考
 */
float mq2_get_ppm(void)
{
    return ppm;
}

/**
 * @brief       获取 MQ2 归一化趋势指数，清洁空气约为 100
 * @param       无
 * @retval      趋势指数
 */
float mq2_get_trend_index(void)
{
    return mq2_state.trend_index;
}

/**
 * @brief       获取最近一次 Rs/R0 比值
 * @param       无
 * @retval      Rs/R0
 */
float mq2_get_rs_r0_ratio(void)
{
    return mq2_state.rs_r0_ratio;
}

/**
 * @brief       获取最近一次 ADC 电压
 * @param       无
 * @retval      ADC 电压
 */
float mq2_get_voltage(void)
{
    return mq2_state.adc_voltage;
}

/**
 * @brief       获取当前 R0 校准值
 * @param       无
 * @retval      R0，单位 kΩ
 */
float mq2_get_r0(void)
{
    return mq2_state.r0_kohm;
}

/**
 * @brief       判断 MQ2 是否已完成上电清洁空气校准
 * @param       无
 * @retval      1 已校准，0 未完成
 */
uint8_t mq2_is_calibrated(void)
{
    return mq2_state.calibrated;
}

/**
 * @brief       获取 MQ2 趋势异常报警状态
 * @param       无
 * @retval      1 已报警，0 未报警
 */
uint8_t mq2_is_trend_alarm(void)
{
    return mq2_trend_alarm;
}
