#include "max30102.h"

/*
 * 算法参考 Maxim MAXREFDES117 与 SparkFun MAX3010x PBA beat detector
 * 核心流程：
 *   HR：SparkFun/Maxim PBA 逐样本 beat detector + 4 次 BPM 均值
 *   SpO2：最近 2 秒 Red/IR 窗口，按平均绝对 AC/DC 比值查表
 */

extern I2C_HandleTypeDef hi2c2;

/* 对外数据 */
int32_t heart_rate = 0;
int32_t spo2       = 0;
uint8_t hr_valid   = 0;
uint8_t spo2_valid = 0;

/* 采样环形缓冲（Red / IR 各 4 秒 @ 100Hz）
 * buf_head 指向下一个写入位置；buf_head 也等价于"最早样本索引" */
static uint32_t red_buf[MAX30102_BUF_LEN];
static uint32_t ir_buf[MAX30102_BUF_LEN];
static uint16_t buf_head = 0;
static uint16_t samples_filled     = 0;     // 首次填满计数，< BUF_LEN 时不解算
static uint16_t samples_since_calc = 0;     // 滑窗触发计数
static uint8_t miss_windows        = 0;     // 连续解算失败窗口计数
static uint8_t spo2_miss_windows   = 0;     // 连续血氧解算失败窗口计数

/* Maxim 官方 SpO2 查表：idx = R × 100，覆盖 R = 0.00 ~ 1.83
 * 由多项式 SpO2 = -45.060·R² + 30.354·R + 94.845 采样生成，四舍五入 */
static const uint8_t uch_spo2_table[184] = {
     95,  95,  95,  96,  96,  96,  97,  97,  97,  97,  97,  98,  98,  98,  98,  98,  99,  99,  99,  99,
     99,  99,  99,  99, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
    100, 100, 100, 100,  99,  99,  99,  99,  99,  99,  99,  99,  98,  98,  98,  98,  98,  98,  97,  97,
     97,  97,  96,  96,  96,  96,  95,  95,  95,  94,  94,  94,  93,  93,  93,  92,  92,  92,  91,  91,
     90,  90,  89,  89,  89,  88,  88,  87,  87,  86,  86,  85,  85,  84,  84,  83,  82,  82,  81,  81,
     80,  80,  79,  78,  78,  77,  76,  76,  75,  74,  74,  73,  72,  72,  71,  70,  69,  69,  68,  67,
     66,  66,  65,  64,  63,  62,  62,  61,  60,  59,  58,  57,  56,  56,  55,  54,  53,  52,  51,  50,
     49,  48,  47,  46,  45,  44,  43,  42,  41,  40,  39,  38,  37,  36,  35,  34,  33,  31,  30,  29,
     28,  27,  26,  25,  23,  22,  21,  20,  19,  17,  16,  15,  14,  12,  11,  10,   9,   7,   6,   5,
      3,   2,   1,   0
};

/* --- I2C 原语 --- */

static uint8_t max30102_write_reg(uint8_t reg, uint8_t val)
{
    if (HAL_I2C_Mem_Write(&hi2c2, (MAX30102_ADDR << 1), reg,
                          I2C_MEMADD_SIZE_8BIT, &val, 1, HAL_MAX_DELAY) != HAL_OK)
        return 1;
    return 0;
}

static uint8_t max30102_read_reg(uint8_t reg, uint8_t *val)
{
    if (HAL_I2C_Mem_Read(&hi2c2, (MAX30102_ADDR << 1), reg,
                         I2C_MEMADD_SIZE_8BIT, val, 1, HAL_MAX_DELAY) != HAL_OK)
        return 1;
    return 0;
}

static uint8_t max30102_read_fifo_burst(uint8_t *buf, uint16_t len)
{
    if (HAL_I2C_Mem_Read(&hi2c2, (MAX30102_ADDR << 1), MAX30102_REG_FIFO_DATA,
                         I2C_MEMADD_SIZE_8BIT, buf, len, HAL_MAX_DELAY) != HAL_OK)
        return 1;
    return 0;
}

/* --- SparkFun / Maxim PBA beat detector + AC/DC SpO2 --- */

#define MAX30102_SAMPLE_RATE_HZ         100
#define MAX30102_HR_MIN                 40
#define MAX30102_HR_MAX                 180
#define MAX30102_SPO2_MIN               85
#define MAX30102_SPO2_MAX               100
#define MAX30102_SPO2_WINDOW            200
#define MAX30102_RATE_SIZE              4
#define MAX30102_HOLD_MISSES            8
#define MAX30102_IR_SCALE_SHIFT         3
#define MAX30102_PBA_AC_MIN             20
#define MAX30102_PBA_AC_MAX             2000
#define MAX30102_SPO2_AC_MIN            40
#define MAX30102_DC_SATURATION          250000U

static int16_t pba_ir_ac_max = 20;
static int16_t pba_ir_ac_min = -20;
static int16_t pba_ir_ac_current = 0;
static int16_t pba_ir_ac_previous = 0;
static int16_t pba_ir_ac_signal_min = 0;
static int16_t pba_ir_ac_signal_max = 0;
static int16_t pba_ir_average = 0;
static uint8_t pba_positive_edge = 0;
static uint8_t pba_negative_edge = 0;
static int32_t pba_ir_avg_reg = 0;
static int16_t pba_cbuf[32];
static uint8_t pba_offset = 0;
static uint32_t sample_counter = 0;
static uint32_t last_beat_sample = 0;
static int32_t rate_buf[MAX30102_RATE_SIZE];
static uint8_t rate_index = 0;
static uint8_t rate_count = 0;

static const uint16_t pba_fir_coeffs[12] = {
    172, 321, 579, 927, 1360, 1858,
    2390, 2916, 3391, 3768, 4012, 4096
};

static uint16_t window_back_index(uint16_t back)
{
    uint16_t offset = (uint16_t)((back + 1) % MAX30102_BUF_LEN);
    return (buf_head >= offset) ? (buf_head - offset) : (MAX30102_BUF_LEN + buf_head - offset);
}

static void pba_reset(void)
{
    pba_ir_ac_max = 20;
    pba_ir_ac_min = -20;
    pba_ir_ac_current = 0;
    pba_ir_ac_previous = 0;
    pba_ir_ac_signal_min = 0;
    pba_ir_ac_signal_max = 0;
    pba_ir_average = 0;
    pba_positive_edge = 0;
    pba_negative_edge = 0;
    pba_ir_avg_reg = 0;
    memset(pba_cbuf, 0, sizeof(pba_cbuf));
    pba_offset = 0;
    last_beat_sample = 0;
    rate_index = 0;
    rate_count = 0;
    memset(rate_buf, 0, sizeof(rate_buf));
}

static void max30102_clear_result(void)
{
    heart_rate = 0;
    spo2 = 0;
    hr_valid = 0;
    spo2_valid = 0;
    miss_windows = 0;
    spo2_miss_windows = 0;
    sample_counter = 0;
    pba_reset();
}

static int32_t smooth_value(int32_t old_val, int32_t new_val, int32_t weight)
{
    return (old_val * weight + new_val + (weight / 2)) / (weight + 1);
}

static int16_t pba_average_dc_estimator(int32_t *p, uint16_t x)
{
    *p += ((((int32_t)x << 15) - *p) >> 4);
    return (int16_t)(*p >> 15);
}

static int32_t pba_mul16(int16_t x, int16_t y)
{
    return (int32_t)x * (int32_t)y;
}

static int16_t pba_low_pass_fir_filter(int16_t din)
{
    pba_cbuf[pba_offset] = din;
    int32_t z = pba_mul16((int16_t)pba_fir_coeffs[11], pba_cbuf[(pba_offset - 11) & 0x1F]);

    for (uint8_t i = 0; i < 11; i++) {
        z += pba_mul16((int16_t)pba_fir_coeffs[i],
                       (int16_t)(pba_cbuf[(pba_offset - i) & 0x1F] +
                                 pba_cbuf[(pba_offset - 22 + i) & 0x1F]));
    }

    pba_offset = (uint8_t)((pba_offset + 1) & 0x1F);
    return (int16_t)(z >> 15);
}

static uint8_t pba_check_for_beat(uint32_t ir_sample)
{
    uint16_t scaled = (uint16_t)(ir_sample >> MAX30102_IR_SCALE_SHIFT);
    uint8_t beat_detected = 0;

    pba_ir_ac_previous = pba_ir_ac_current;
    pba_ir_average = pba_average_dc_estimator(&pba_ir_avg_reg, scaled);
    pba_ir_ac_current = pba_low_pass_fir_filter((int16_t)(scaled - (uint16_t)pba_ir_average));

    if (pba_ir_ac_previous < 0 && pba_ir_ac_current >= 0) {
        pba_ir_ac_max = pba_ir_ac_signal_max;
        pba_ir_ac_min = pba_ir_ac_signal_min;
        pba_positive_edge = 1;
        pba_negative_edge = 0;
        pba_ir_ac_signal_max = 0;

        int16_t amplitude = (int16_t)(pba_ir_ac_max - pba_ir_ac_min);
        if (amplitude > MAX30102_PBA_AC_MIN && amplitude < MAX30102_PBA_AC_MAX)
            beat_detected = 1;
    }

    if (pba_ir_ac_previous > 0 && pba_ir_ac_current <= 0) {
        pba_positive_edge = 0;
        pba_negative_edge = 1;
        pba_ir_ac_signal_min = 0;
    }

    if (pba_positive_edge && pba_ir_ac_current > pba_ir_ac_previous)
        pba_ir_ac_signal_max = pba_ir_ac_current;

    if (pba_negative_edge && pba_ir_ac_current < pba_ir_ac_previous)
        pba_ir_ac_signal_min = pba_ir_ac_current;

    return beat_detected;
}

static void max30102_update_heart_rate(uint32_t ir_sample)
{
    sample_counter++;

    if (ir_sample < MAX30102_FINGER_THRESHOLD) {
        pba_reset();
        return;
    }

    if (!pba_check_for_beat(ir_sample))
        return;

    if (last_beat_sample != 0) {
        uint32_t delta = sample_counter - last_beat_sample;
        const uint32_t min_delta = (MAX30102_SAMPLE_RATE_HZ * 60U) / MAX30102_HR_MAX;
        const uint32_t max_delta = (MAX30102_SAMPLE_RATE_HZ * 60U) / MAX30102_HR_MIN;

        if (delta >= min_delta && delta <= max_delta) {
            int32_t bpm = (int32_t)((MAX30102_SAMPLE_RATE_HZ * 60U + (delta / 2U)) / delta);
            rate_buf[rate_index] = bpm;
            rate_index = (uint8_t)((rate_index + 1) % MAX30102_RATE_SIZE);
            if (rate_count < MAX30102_RATE_SIZE)
                rate_count++;

            if (rate_count >= 2) {
                int32_t sum = 0;
                for (uint8_t i = 0; i < rate_count; i++)
                    sum += rate_buf[i];

                int32_t avg = sum / rate_count;
                heart_rate = hr_valid ? smooth_value(heart_rate, avg, 2) : avg;
                hr_valid = 1;
                miss_windows = 0;
            }
        }
    }

    last_beat_sample = sample_counter;
}

static void max30102_expire_stale_heart_rate(void)
{
    const uint32_t max_delta = (MAX30102_SAMPLE_RATE_HZ * 60U) / MAX30102_HR_MIN;

    if (hr_valid && last_beat_sample != 0 &&
        (sample_counter - last_beat_sample) > (max_delta * 2U)) {
        heart_rate = 0;
        hr_valid = 0;
        rate_index = 0;
        rate_count = 0;
        memset(rate_buf, 0, sizeof(rate_buf));
    }
}

static void max30102_compute_spo2(int32_t *sp, uint8_t *sp_ok)
{
    *sp_ok = 0;
    if (samples_filled < MAX30102_SPO2_WINDOW)
        return;

    uint64_t ir_sum = 0;
    uint64_t red_sum = 0;
    for (uint16_t i = 0; i < MAX30102_SPO2_WINDOW; i++) {
        uint16_t k = window_back_index(i);
        ir_sum += ir_buf[k];
        red_sum += red_buf[k];
    }

    uint32_t ir_dc = (uint32_t)(ir_sum / MAX30102_SPO2_WINDOW);
    uint32_t red_dc = (uint32_t)(red_sum / MAX30102_SPO2_WINDOW);
    if (ir_dc < MAX30102_FINGER_THRESHOLD || ir_dc > MAX30102_DC_SATURATION || red_dc == 0)
        return;

    uint64_t ir_ac_sum = 0;
    uint64_t red_ac_sum = 0;
    for (uint16_t i = 0; i < MAX30102_SPO2_WINDOW; i++) {
        uint16_t k = window_back_index(i);
        int32_t ir_delta = (int32_t)ir_buf[k] - (int32_t)ir_dc;
        int32_t red_delta = (int32_t)red_buf[k] - (int32_t)red_dc;
        ir_ac_sum += (uint32_t)((ir_delta < 0) ? -ir_delta : ir_delta);
        red_ac_sum += (uint32_t)((red_delta < 0) ? -red_delta : red_delta);
    }

    uint32_t ir_ac = (uint32_t)(ir_ac_sum / MAX30102_SPO2_WINDOW);
    uint32_t red_ac = (uint32_t)(red_ac_sum / MAX30102_SPO2_WINDOW);
    if (ir_ac < MAX30102_SPO2_AC_MIN || red_ac == 0)
        return;

    int64_t num = (int64_t)red_ac * (int64_t)ir_dc * 100;
    int64_t den = (int64_t)ir_ac * (int64_t)red_dc;
    if (den <= 0)
        return;

    int32_t r = (int32_t)(num / den);
    if (r > 2 && r < 184) {
        int32_t spo2_calc = uch_spo2_table[r];
        if (spo2_calc >= MAX30102_SPO2_MIN && spo2_calc <= MAX30102_SPO2_MAX) {
            *sp = spo2_calc;
            *sp_ok = 1;
        }
    }
}

static uint32_t max30102_recent_ir_mean(uint16_t count)
{
    if (count == 0)
        return 0;

    uint64_t ir_sum = 0;
    for (uint16_t i = 0; i < count; i++)
        ir_sum += ir_buf[window_back_index(i)];

    return (uint32_t)(ir_sum / count);
}

/**
 * @brief   MAX30102 初始化：复位 → 自检 → 配置 SpO2 模式
 * @retval  0 成功，1 Part ID 不匹配或 I2C 失败
 */
uint8_t max30102_init(void)
{
    uint8_t part_id = 0;

    /* 软复位（MODE.RESET=1）*/
    if (max30102_write_reg(MAX30102_REG_MODE_CONFIG, 0x40)) return 1;
    HAL_Delay(50);

    if (max30102_read_reg(MAX30102_REG_PART_ID, &part_id)) return 1;
    if (part_id != MAX30102_PART_ID_VALUE) return 1;

    /* 清 FIFO 指针 */
    max30102_write_reg(MAX30102_REG_FIFO_WR_PTR, 0x00);
    max30102_write_reg(MAX30102_REG_OVF_COUNTER, 0x00);
    max30102_write_reg(MAX30102_REG_FIFO_RD_PTR, 0x00);

    /* FIFO_CONFIG: SMP_AVE=000, FIFO_ROLLOVER_EN=1, FIFO_A_FULL=0x0F */
    max30102_write_reg(MAX30102_REG_FIFO_CONFIG, 0x1F);

    /* MODE: SpO2 模式（Red + IR） */
    max30102_write_reg(MAX30102_REG_MODE_CONFIG, 0x03);

    /* SPO2_CONFIG: ADC_RGE=01(4096nA), SR=011(100Hz), LED_PW=11(411us/18-bit) */
    max30102_write_reg(MAX30102_REG_SPO2_CONFIG, 0x27);

    /* LED 电流约 7mA（0x24 × 0.2mA）*/
    max30102_write_reg(MAX30102_REG_LED1_PA, 0x24);
    max30102_write_reg(MAX30102_REG_LED2_PA, 0x24);

    /* 关闭所有中断（纯轮询）*/
    max30102_write_reg(MAX30102_REG_INT_ENABLE_1, 0x00);
    max30102_write_reg(MAX30102_REG_INT_ENABLE_2, 0x00);

    buf_head           = 0;
    samples_filled     = 0;
    samples_since_calc = 0;
    max30102_clear_result();
    return 0;
}

/**
 * @brief   MAX30102 周期任务（50ms 调用一次）
 * @note    读取 FIFO 新样本写入环形缓冲；缓冲填满后每 STRIDE 个新样本
 *          调用 Maxim 风格算法（翻转找谷 + 4 点 MA + 谷间 R 比值中位数 + 184 表）
 */
void max30102_task(void)
{
    uint8_t wr = 0, rd = 0;
    if (max30102_read_reg(MAX30102_REG_FIFO_WR_PTR, &wr)) return;
    if (max30102_read_reg(MAX30102_REG_FIFO_RD_PTR, &rd)) return;

    uint8_t samples = (wr - rd) & 0x1F;                 // FIFO 深度 32
    if (samples == 0) return;

    uint8_t raw[192];                                    // 32 × 6 上限
    if (samples > 32) samples = 32;
    uint16_t len = (uint16_t)samples * 6;

    if (max30102_read_fifo_burst(raw, len)) return;

    for (uint8_t i = 0; i < samples; i++) {
        uint32_t red = ((uint32_t)raw[i * 6 + 0] << 16) |
                       ((uint32_t)raw[i * 6 + 1] << 8)  |
                        (uint32_t)raw[i * 6 + 2];
        uint32_t ir  = ((uint32_t)raw[i * 6 + 3] << 16) |
                       ((uint32_t)raw[i * 6 + 4] << 8)  |
                        (uint32_t)raw[i * 6 + 5];
        red &= 0x3FFFF;                                  // 保留 18-bit
        ir  &= 0x3FFFF;

        red_buf[buf_head] = red;
        ir_buf[buf_head]  = ir;
        buf_head = (uint16_t)((buf_head + 1) % MAX30102_BUF_LEN);

        if (samples_filled < MAX30102_BUF_LEN) samples_filled++;
        samples_since_calc++;

        max30102_update_heart_rate(ir);
    }

    if (samples_since_calc < MAX30102_CALC_STRIDE) return;
    samples_since_calc = 0;

    uint16_t mean_count = samples_filled < MAX30102_SPO2_WINDOW ?
                          samples_filled : MAX30102_SPO2_WINDOW;
    uint32_t ir_mean = max30102_recent_ir_mean(mean_count);

    if (ir_mean < MAX30102_FINGER_THRESHOLD) {
        max30102_clear_result();
        printf("max30102: no finger\r\n");
        return;
    }

    max30102_expire_stale_heart_rate();

    int32_t sp = 0;
    uint8_t sp_ok = 0;
    max30102_compute_spo2(&sp, &sp_ok);
    if (sp_ok) {
        spo2 = spo2_valid ? smooth_value(spo2, sp, 3) : sp;
        spo2_valid = 1;
        spo2_miss_windows = 0;
        miss_windows = 0;
    } else {
        if (spo2_miss_windows < MAX30102_HOLD_MISSES)
            spo2_miss_windows++;
        if (spo2_miss_windows >= MAX30102_HOLD_MISSES) {
            spo2 = 0;
            spo2_valid = 0;
        }
    }

    if (!hr_valid && !spo2_valid) {
        if (miss_windows < MAX30102_HOLD_MISSES)
            miss_windows++;

        if (miss_windows >= MAX30102_HOLD_MISSES)
            max30102_clear_result();
    }

    printf("hr:%d bpm  spo2:%d%%  (hr_ok=%d spo2_ok=%d)\r\n",
           (int)heart_rate, (int)spo2, (int)hr_valid, (int)spo2_valid);
}
