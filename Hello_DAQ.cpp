#include <iostream>
#include <NIDAQmx.h>
#include "INIReader.h"
extern "C" {
#include "ini.h" // 使用 extern "C" 避免名稱修飾
}
#include <map>
#include <string>
#include <vector>

using namespace std;

// 定義錯誤檢查
#define DAQmxErrChk(functionCall) if (DAQmxFailed(error = (functionCall))) {DAQmxGetExtendedErrorInfo(errBuff, 2048); cerr << "DAQmx Error: " << errBuff << endl; goto Error;}

// 回呼函式，用於解析 INI 檔案
static int handler(void* user, const char* section, const char* name, const char* value) {
    // 將解析的 INI 資料儲存到 map 結構中
    map<string, map<string, string>>* ini_data =
        reinterpret_cast<map<string, map<string, string>>*>(user);

    (*ini_data)[section][name] = value;
    return 1;
}

// 函數：取得包含特定關鍵字的區段名稱
vector<string> filterSections(const map<string, map<string, string>>& ini_data, const string& section_keyword) {
    vector<string> result;
    for (const auto& section : ini_data) {
        // 檢查區段名稱是否包含關鍵字
        if (section.first.find(section_keyword) != string::npos) {
            result.push_back(section.first);
        }
    }
    return result;
}

int main(void) {
    int32 error = 0; // 儲存錯誤代碼
    TaskHandle taskHandle = 0; // 定義任務控制代碼
    char errBuff[2048] = { 0 }; // 儲存錯誤訊息的緩衝區
    int32 read; // 實際讀取的數據點數量
    char channelNames[2048] = { 0 }; // 儲存通道名稱
    uInt32 numChans = 0; // 儲存通道數量
    int32 sampRate = 12800; // 預設取樣率

    const char* filename = "API/config.ini"; // INI 檔案名稱
    map<string, map<string, string>> ini_data; // 儲存 INI 檔案的資料

    // 解析 INI 檔案
    if (ini_parse(filename, handler, &ini_data) < 0) {
        cerr << "無法載入API/config.ini檔案: " << filename << endl;
        return 1;
    }

    // 篩選包含 "DAQmxChannel" 的區段
    vector<string> filtered_sections = filterSections(ini_data, "DAQmxChannel");

    try {
        // 創建任務
        DAQmxErrChk(DAQmxCreateTask("", &taskHandle));

        // 遍歷所有包含 "DAQmxChannel" 的區段
        for (const string& section : filtered_sections) {
            // 提取設定值
            string channelType = ini_data[section]["ChanType"]; // 通道類型
            string physicalChannel = ini_data[section]["PhysicalChanName"]; // 實體通道名稱
            double minVal = stod(ini_data[section]["AI.Min"]); // 最小值
            double maxVal = stod(ini_data[section]["AI.Max"]); // 最大值

            // 根據通道類型創建對應的 DAQmx 通道
            if (channelType == "Analog Input") {
                string measType = ini_data[section]["AI.MeasType"]; // 測量類型
                if (measType == "Voltage") {
                    // 創建電壓通道
                    DAQmxErrChk(DAQmxCreateAIVoltageChan(taskHandle, physicalChannel.c_str(), "", DAQmx_Val_Cfg_Default, minVal, maxVal, DAQmx_Val_Volts, NULL));
                }
                /*else if (measType == "Current") {
                    // 創建電流通道（略）
                } else if (measType == "Accelerometer") {
                    // 創建加速計通道（略）
                }*/
            }
        }
        cout << "通道創建完成。" << endl;

        // 處理包含 "DAQmxTask" 的 sections
        vector<string> task_sections = filterSections(ini_data, "DAQmxTask");
        if (!task_sections.empty()) {
            string task_section = task_sections[0]; // 假設只處理第一個 "DAQmxTask" 區段
            sampRate = stod(ini_data[task_section]["SampClk.Rate"]); // 取樣率
            int sampPerChan = stoi(ini_data[task_section]["SampQuant.SampPerChan"]); // 每通道樣本數

            // 配置取樣率
            DAQmxErrChk(DAQmxCfgSampClkTiming(taskHandle, "", sampRate, DAQmx_Val_Rising, DAQmx_Val_ContSamps, sampPerChan));
            cout << "取樣率: " << sampRate << " 設定完成。" << endl;
        }

        // 獲取任務中已添加的通道名稱
        DAQmxErrChk(DAQmxGetTaskChannels(taskHandle, channelNames, sizeof(channelNames)));
        cout << "已添加的通道: " << channelNames << endl;

        // 獲取通道數量
        DAQmxErrChk(DAQmxGetTaskNumChans(taskHandle, &numChans));
        cout << "通道數量: " << numChans << endl;

        // 配置與啟動任務
        DAQmxErrChk(DAQmxStartTask(taskHandle));

        cout << "正在擷取數值... 按 Ctrl+C 終止程式。" << endl;

        // 計算每次讀取的數據組數，確保緩衝區足夠大
        const int sampleRate = sampRate; // 取樣率
        const int numChannels = numChans;   // 通道數量
        const int bufferSize = sampleRate * numChannels; // 緩衝區大小
        std::vector<float64> data(bufferSize); // 動態分配緩衝區

        while (true) {
            int32 read; // 實際讀取到的樣本數量

            // 從 DAQ 設備讀取數據
            DAQmxErrChk(DAQmxReadAnalogF64(
                taskHandle,
                sampleRate,                // 每次讀取樣本數量（每通道）
                10.0,                      // 超時時間（秒）
                DAQmx_Val_GroupByChannel,  // 按通道分組
                data.data(),               // 數據緩衝區
                bufferSize,                // 緩衝區大小
                &read,                     // 實際讀取到的樣本數量
                NULL
            ));

            // 輸出數據，每次輸出一整組
            cout << "讀取到 " << read << " 組數據:" << endl;
            for (int i = 0; i < sampleRate; ++i) {
                for (int ch = 0; ch < numChannels; ++ch) {
                    cout << data[ch * sampleRate + i] << " ";
                }
                cout << endl;
            }
        }
    }
    catch (...) {
        cerr << "發生意外錯誤。" << endl;
        goto Error;
    }

Error:
    if (DAQmxFailed(error)) {
        cerr << "DAQmx 錯誤: " << errBuff << endl;
    }
    if (taskHandle != 0) {
        DAQmxClearTask(taskHandle);
    }
    return error ? 1 : 0;
}