/**
 1. 经验默认表（先抄就能跑）

参数	经验默认值	说明
predict_someone_threshold	0.008 ~ 0.015	训练后自动写回，空房间基线 ≈ 0.003；给 2~3 倍安全系数。
predict_someone_sensitivity	0.12 ~ 0.18	官方示例 0.15；房间越大 / 路由器越远 → 适当调大。
predict_move_threshold	0.001 ~ 0.003	训练后自动写回，静止基线 ≈ 0.0003；给 3~5 倍安全系数。
predict_move_sensitivity	0.18 ~ 0.25	官方示例 0.20；想检测微小动作 → 可拉到 0.25。
2. 常见场景快速对照

场景	                    someone_*	    move_*
10 m² 卧室 + 路由器在屋内	 0.010 / 0.15	 0.002 / 0.20
30 m² 客厅 + 路由器隔一堵墙	 0.012 / 0.18	 0.0025 / 0.23
空旷会议室 50 m²	        0.015 / 0.20	0.003 / 0.25
高噪声环境（AP 多、人多）	 0.020 / 0.12	 0.004 / 0.15

3. 调参口诀
阈值（threshold） 越大 → 越难触发 → 减少误报
灵敏度（sensitivity） 越大 → 越容易触发 → 减少漏报

*
*/
#ifndef __DOIT_CSI_H__
#define __DOIT_CSI_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "esp_mac.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_ping.h"
#include "mbedtls/base64.h"
#include "esp_radar.h"

#define CONFIG_SEND_DATA_FREQUENCY 10 /* CSI 发送频率，单位 Hz */
#define RADAR_BUFF_MAX_LEN 25
#define PRE_CALIB_TIME_SEC 10 /* 准备校准倒计时秒数 */

    typedef void (*csi_human_status_cb_t)(bool room_status, bool human_status); // 状态回调函数
    static csi_human_status_cb_t g_human_status_cb = NULL;

    static bool is_pre_calib_running = false;
    static bool is_start_calib_running = false;
    static bool is_csi_radar_running = false;
    static bool last_room_status = false;
    static bool last_human_status = false;

    static uint8_t g_pre_calib_countdown_left = PRE_CALIB_TIME_SEC;

    static uint32_t g_send_data_interval = CONFIG_SEND_DATA_FREQUENCY; /* Ping/NULL 包间隔 */
    static TimerHandle_t g_pre_calib_countdown_timer = NULL;           /* 准备校准定时器*/
    static QueueHandle_t g_csi_info_queue = NULL;                      /* CSI 信息队列 */
    static TaskHandle_t csi_data_print_task_handler = NULL;            /* CSI 数据打印任务句柄 */
    static TaskHandle_t trigger_router_send_data_task_handler = NULL;  /* 触发路由器发送数据任务句柄 */
    // static struct
    // {
    //     struct arg_lit *train_start;
    //     struct arg_lit *train_stop;
    //     struct arg_lit *train_add;
    //     struct arg_str *predict_someone_threshold;
    //     struct arg_str *predict_someone_sensitivity;
    //     struct arg_str *predict_move_threshold;
    //     struct arg_str *predict_move_sensitivity;
    //     struct arg_int *predict_buff_size;
    //     struct arg_int *predict_outliers_number;
    //     struct arg_str *collect_taget;
    //     struct arg_int *collect_number;
    //     struct arg_int *collect_duration;
    //     struct arg_lit *csi_start;
    //     struct arg_lit *csi_stop;
    //     struct arg_str *csi_output_type;
    //     struct arg_str *csi_output_format;
    //     struct arg_int *csi_scale_shift;
    //     struct arg_int *channel_filter;
    //     struct arg_int *send_data_interval;
    //     struct arg_end *end;
    // } radar_args;
    static struct console_input_config
    {
        bool train_start;                  // 是否人工采集
        float predict_someone_threshold;   // 有人阈值,训练结束后由 esp_radar_train_stop() 写回，代表“空房间”时的 wander 基线 + 安全系数。
        float predict_someone_sensitivity; // 有人灵敏度系数,灵敏度：越大越容易触发“有人”。
        float predict_move_threshold;      // 运动阈值,训练结束后由 esp_radar_train_stop() 写回，代表“静止”时的 jitter 基线 + 安全系数。
        float predict_move_sensitivity;    // 运动灵敏度系数,灵敏度：越大越容易触发“运动”。
        uint32_t predict_buff_size;        // 指定了存储雷达数据的缓冲区的大小。这个缓冲区用于存储最近的雷达数据点，以便进行平滑处理和统计分析
        uint32_t predict_outliers_number;  // 需要检测到的异常点（outliers）的最小数量。异常点是指那些超过运动阈值的数据点。
        char collect_taget[16];            // 场景标签
        uint32_t collect_number;
        char csi_output_type[16];   // 子载波类型
        char csi_output_format[16]; // 数值格式
    } g_console_input_config = {
        .train_start = false,
        .predict_someone_threshold = 0.015,
        .predict_someone_sensitivity = 0.05,
        .predict_move_threshold = 0.003,
        .predict_move_sensitivity = 0.15,
        .predict_buff_size = 5,
        .predict_outliers_number = 2,
        .collect_taget = "unknown",
        .csi_output_type = "LLFT",
        .csi_output_format = "decimal"};

    /* ---------- 函数声明 ---------- */

    static void wifi_radar_cb(const wifi_radar_info_t *info, void *ctx);
    static void trigger_router_send_data_task(void *arg);
    void doit_csi_init(void);
    void start_csi_radar(void);
    void stop_csi_radar(void);
    bool start_csi_calib(void);
    void stop_csi_calib(void);

    bool GetIsPreCalibRunning();
    bool GetIsStartCalibRunning();
    bool GetIsCsiRadarRunning();
    void SetIsPreCalibRunning(bool value);
    void SetIsStartCalibRunning(bool value);
    void SetIsCsiRadarRunning(bool value);
    void register_human_status_cb(csi_human_status_cb_t cb);
    void minusSomeoneSensitivity();
    void addSomeoneSensitivity();

#ifdef __cplusplus
}
#endif

#endif