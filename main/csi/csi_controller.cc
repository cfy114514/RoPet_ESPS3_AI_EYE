/*
 * @Description:
 * @Author: cjs丶
 * @Date: 2025-07-30 16:55:18
 * @LastEditTime: 2025-08-04 11:41:55
 * @LastEditors: cjs丶
 */

/*
    CSI控制器
*/

#include <esp_log.h>

#include "csi_controller.h"
#include "board.h"
#include "mcp_server.h"
#include "doit_csi.h"

#define TAG "CSIController"

CSIController::CSIController()
{
    RegisterMcpTools();
    ESP_LOGI(TAG, "CSI控制器已初始化并注册MCP工具");

    // 初始化事件组
    csi_event_group_ = xEventGroupCreate();

    /* 2. 校准定时器初始化 */
    g_start_calib_countdown_timer = xTimerCreate("start_calib_countdown", pdMS_TO_TICKS(1000), pdTRUE, NULL, calib_done_cb);

    register_human_status_cb(
        [](bool room_status, bool human_status)
        {
            auto &app = Application::GetInstance();
            app.AbortSpeaking(kAbortReasonNone);
            if (room_status)
            {
                if (human_status)
                {
                    ESP_LOGI(TAG, "检测到有人活动");
                    auto &app = Application::GetInstance();
                    app.SendChatText("有人在移动，问他动来动去是什么意思，是不是想跟你聊天");
                }
                else
                {
                    ESP_LOGI(TAG, "检测到有人静止");
                    auto &app = Application::GetInstance();
                    app.SendChatText("有人静止不动了，跟他搭讪，询问是不是需要陪他聊天");
                }
            }
            else
            {
                ESP_LOGI(TAG, "无人状态");
                auto &app = Application::GetInstance();

                app.SendChatText("用户走了，自言自语说话，表示对用户的想念，希望他快点回来");
            }
        });

    xTaskCreate(CSI_Task, "CSI_Task", 4096, this, 5, NULL); // csi任务
}

CSIController::~CSIController()
{
    // ESP_LOGI(TAG, "CSI控制器已初始化并注册MCP工具");
}

void CSIController::Init()
{
    doit_csi_init(); // 初始化CSI
}

void CSIController::RegisterMcpTools()
{
    auto &mcp_server = McpServer::GetInstance();

    ESP_LOGI(TAG, "开始注册Eye MCP工具...");
    mcp_server.AddTool(
        // "self.radar.calibration",
        // "雷达校准，当收到用户说开始校准时，大模型大模型开始10秒倒计时，大模型回复内容：‘准备开始校准’，其他的什么都不要回复！，只需要回复单引号的内容即可，如果该函数执行时间过长，大模型也不要发消息！",
        "self.radar.calibration",
        "雷达校准，当收到用户说开始校准时，调用该函数",
        PropertyList(),
        [this](const PropertyList &properties) -> ReturnValue
        {
            // 强制进入空闲，避免大模型打断
            ForceIdle();
            // stop_csi_radar(); // 关闭雷达
            csi_state_ = STATE_PRE_CALIB_ING;
            xEventGroupWaitBits(csi_event_group_, STATE_MACHINE_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
            start_csi_radar(); // 启动雷达
            return true;
        });

    // mcp_server.AddTool("self.get_someone_sensitivity",
    //                    "用于获取有人状态的灵敏度,回答当前的有人状态灵敏度的数值",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        return GetsomeoneSensit();
    //                    });

    ESP_LOGI(TAG, "Eye MCP工具注册完成");
}

float CSIController::GetsomeoneSensit()
{
    return g_console_input_config.predict_someone_sensitivity;
}

void CSIController::ForceIdle()
{
    auto state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateListening)
    {
        Application::GetInstance().ToggleChatState();
    }
    else if (state == kDeviceStateSpeaking)
    {
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
        Application::GetInstance().Schedule(
            []()
            { Application::GetInstance().AbortSpeaking(kAbortReasonNone); });
        Application::GetInstance().Close();
    }
}
