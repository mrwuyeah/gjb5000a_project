#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <chrono>
#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>

// 临时存储区参数
// constexpr size_t CAN_SIZE =  24;  //这个就是你有多少路设置多少路参数
constexpr size_t CAN_SIZE =  3;      //目前最多3路
constexpr size_t BUFFER_SIZE = 6000;
constexpr size_t FRAME_SIZE = 100;

// 24路can口
// const std::string INTERFACE_NAMES[CAN_SIZE] = {
//     "can0", "can1", "can2", "can3", "can4", "can5", 
//     "can6", "can7", "can8", "can9", "can10", "can11", 
//     "can12", "can13", "can14", "can15", "can16", "can17", 
//     "can18", "can19", "can20", "can21", "can22", "can23"
// };

// 目前只有三路
const std::string INTERFACE_NAMES[CAN_SIZE] = {"can0", "can1", "can2"};

// 用于处理数据，索引
int write_indices[CAN_SIZE] = {0}; // 写入索引数组
int read_indices[CAN_SIZE] = {0}; // 读取索引数组
int local_write_indices[CAN_SIZE] = {0};// 备份写入索引数组


// 定义大小为24的一维数组
std::array<int64_t, CAN_SIZE> current_Timestamp;   // 用于解析输出指令时间戳
std::array<int64_t, CAN_SIZE> last_Timestamp;      // 用于记录上一个输出指令时间戳
std::array<int32_t, CAN_SIZE> ID;                  // 用于记录每一路解析到当前帧的ID值
std::array<int64_t, CAN_SIZE> remote_Timestamp1;   // 用于记录遥控帧1的时间戳
std::array<int64_t, CAN_SIZE> back_Timestamp1;     // 用于记录反馈帧1的时间戳
std::array<int64_t, CAN_SIZE> remote_Timestamp2;   // 用于记录遥控帧2的时间戳
std::array<int64_t, CAN_SIZE> back_Timestamp2;     // 用于记录反馈帧2的时间戳

// 临时存储区

std::array<std::array<std::array<unsigned char, FRAME_SIZE>, BUFFER_SIZE>, CAN_SIZE> g_packet_buffer; // 全局缓冲区 24 6000 100
std::mutex buffer_mutex; // 互斥锁，用于保护全局缓冲区
std::condition_variable data_condition; // 条件变量，用于通知解析线程
bool running = true; // 控制线程运行的标志

// 第三个线程全局数组，结果数组
constexpr size_t RESULT_BUFFER_SIZE = 6000; // 结果缓冲区大小
constexpr size_t RESULT_FRAME_SIZE = 10; // 每个结果的大小
std::array<std::array<std::array<unsigned char, RESULT_FRAME_SIZE>, RESULT_BUFFER_SIZE>, CAN_SIZE> Result_Buffer; // 结果缓冲区 24 6000 10
std::mutex result_mutex; // 互斥锁，用于保护结果缓冲区


// 解析线程，如果处理时间不够可以多搞几个
void parseData() {
    int pack_control = 0; // 辅助控制器解析

    while (running) {
        // std::cout << "在跑了在跑了" << std::endl;  //调试用
        std::this_thread::sleep_for(std::chrono::seconds(1)); // 每1秒运行一次
        bool hasNewData = false;
        // 死循环，直到有新数据
        while (!hasNewData) {
            
            for (size_t i = 0; i < CAN_SIZE; ++i) {
                if (write_indices[i] != read_indices[i]) {
                    hasNewData = true; // 有新数据
                    // std::cout << "晕了" << std::endl;  //调试用
                    break;
                }
            }
        }
        // std::cout << "出来了没" << std::endl;  //调试用
        // 锁住原始数据
        std::unique_lock<std::mutex> lock(buffer_mutex);

        if (hasNewData) {
            // 备份数据
            std::array<std::array<std::array<unsigned char, FRAME_SIZE>, BUFFER_SIZE>, CAN_SIZE> g1_packet_buffer;
            for (size_t i = 0; i < CAN_SIZE; ++i) {
                std::copy(std::begin(g_packet_buffer[i]), std::end(g_packet_buffer[i]), std::begin(g1_packet_buffer[i]));
            }

            // 备份写入索引
            std::copy(std::begin(write_indices), std::end(write_indices), std::begin(local_write_indices));


            // 释放锁
            lock.unlock();

            // 开始循环处理数据
            for (size_t i = 0; i < CAN_SIZE; ++i) {
                std::cout << i << std::endl;    // 调试用
                while (local_write_indices[i] != read_indices[i]) {
                    // 处理数据
                    int packetIndex = read_indices[i];

                    // 校验帧类型：只处理数据帧和遥控帧
                    std::memcpy(&ID[i], &g1_packet_buffer[i][packetIndex][21], 2); // sizeof(ID[i])
                    std::memcpy(&current_Timestamp[i], &g1_packet_buffer[i][packetIndex][2], sizeof(current_Timestamp[i]));

                    // 输出数据
                    if (g1_packet_buffer[i][packetIndex][25] == 1) {
                        std::cout << "Data for packet " << packetIndex << " (遥控帧): ";
                    } else {
                        std::cout << "Data for packet " << packetIndex << " (数据帧): ";
                    }

                    for (int j = 0; j < 34; j++) {
                        std::cout << std::hex << static_cast<int>(g1_packet_buffer[i][packetIndex][j]) << " ";
                    }
                    std::cout << std::endl;

                    // 输出指令解析
                    if (g1_packet_buffer[i][packetIndex][25] == 1 || ID[i] == 0x102 || ID[i] == 0x103) {
                        // 输出间隔时间
                        std::memcpy(&last_Timestamp[i], &g1_packet_buffer[i][pack_control][2], sizeof(last_Timestamp[i]));
                        int64_t timeInterval = (current_Timestamp[i] - last_Timestamp[i]) / 1000; // 转换为微秒
                        std::cout << "时间间隔=" << std::dec << timeInterval << "微秒" << " ";

                        pack_control = packetIndex;

                        if (timeInterval > 200000) {
                            Result_Buffer[i][packetIndex][6] = 3; // 无输出
                            std::cout << "输出指令无输出" << std::endl;
                        } else if (timeInterval < 80000) {
                            Result_Buffer[i][packetIndex][6] = 2; // 发送过快
                            std::cout << "输出指令发送过快" << std::endl;
                        } else if (timeInterval > 120000) {
                            Result_Buffer[i][packetIndex][6] = 1; // 发送过慢
                            std::cout << "输出指令发送过慢" << std::endl;
                        } else {
                            Result_Buffer[i][packetIndex][6] = 0; // 正常
                            std::cout << "输出指令发送正常" << std::endl;
                        }
                    }

                    // 解析转速
                    if (!(g1_packet_buffer[i][packetIndex][25] == 1) && (ID[i] == 0x102 || ID[i] == 0x130)) {
                        uint16_t n_1;
                        std::memcpy(&n_1, &g1_packet_buffer[i][packetIndex][26], sizeof(n_1));
                        // 判断转速是否在3000到5000之间
                        if (n_1 >= 3000 && n_1 <= 5000) {
                            std::cout << "转速解析正常：" << std::dec << n_1 << " rpm" << std::endl;
                        } else {
                            Result_Buffer[i][packetIndex][8] = 1; // 异常
                            std::cout << "转速解析异常：" << std::dec << n_1 << " rpm" << std::endl;
                        }
                    }

                    // 反馈帧响应解析
                    if (g1_packet_buffer[i][packetIndex][25] == 1 || ID[i] == 0x130 || ID[i] == 0x131) {
                        if (g1_packet_buffer[i][packetIndex][25] == 1) {
                            if (ID[i] == 0x130) {
                                std::memcpy(&remote_Timestamp1[i], &g1_packet_buffer[i][packetIndex][2], sizeof(current_Timestamp[i]));
                            }
                            if (ID[i] == 0x131) {
                                std::memcpy(&remote_Timestamp2[i], &g1_packet_buffer[i][packetIndex][2], sizeof(current_Timestamp[i]));
                            }
                        }

                        if (g1_packet_buffer[i][packetIndex][25] != 1 && ID[i] == 0x130) {
                            std::memcpy(&back_Timestamp1[i], &g1_packet_buffer[i][packetIndex][2], sizeof(current_Timestamp[i]));
                            int64_t timeBack1 = (back_Timestamp1[i] - remote_Timestamp1[i]) / 1000; // 转换为微秒
                            std::cout << "反馈时间=" << std::dec << timeBack1 << "微秒" << " ";
                            if (timeBack1 > 5000) {
                                Result_Buffer[i][packetIndex][7] = 2; // 无反馈
                                std::cout << "设备无反馈" << std::endl;
                            } else if (timeBack1 > 500) {
                                Result_Buffer[i][packetIndex][7] = 1; // 反馈较慢
                                std::cout << "设备反馈较慢" << std::endl;
                            } else {
                                Result_Buffer[i][packetIndex][7] = 0; // 正常
                                std::cout << "设备反馈正常" << std::endl;
                            }
                        }
                        if (g1_packet_buffer[i][packetIndex][25] != 1 && ID[i] == 0x131) {
                            std::memcpy(&back_Timestamp2[i], &g1_packet_buffer[i][packetIndex][2], sizeof(current_Timestamp[i]));
                            int64_t timeBack2 = (back_Timestamp2[i] - remote_Timestamp2[i]) / 1000; // 转换为微秒
                            std::cout << "反馈时间=" << std::dec << timeBack2 << "微秒" << " ";
                            if (timeBack2 > 5000) {
                                std::cout << "设备无反馈" << std::endl;
                            } else if (timeBack2 > 400) {
                                std::cout << "设备反馈较慢" << std::endl;
                            } else {
                                std::cout << "设备反馈正常" << std::endl;
                            }
                        }
                    }

                // 更新读取索引
                read_indices[i] = (read_indices[i] + 1) % BUFFER_SIZE; // 更新读取索引
                }
            }
        }
    }
}

// 总结线程
void summarizeResults() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(10)); // 每十秒总结一次

        // 锁定结果缓冲区以进行安全访问
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            for (size_t i = 0; i < CAN_SIZE; i++) {
                std::cout << "第 " << i+1 << " 路结果汇总是：" << std::endl;

                int count0 = 0, count1 = 0, count2 = 0, count3 = 0;
                int noFeedbackCount = 0, slowFeedbackCount = 0;
                int errorCount = 0;

                for (size_t j = 0; j < BUFFER_SIZE; j++) {
                    // 统计第六个字节
                    switch (Result_Buffer[i][j][6]) {
                        case 0: count0++; break;
                        case 1: count1++; break;
                        case 2: count2++; break;
                        case 3: count3++; break;
                    }

                    // 统计第七个字节
                    switch (Result_Buffer[i][j][7]) {
                        case 1: slowFeedbackCount++; break;
                        case 2: noFeedbackCount++; break;
                    }

                    // 统计第八个字节
                    if (Result_Buffer[i][j][8] == 1) {
                        errorCount++;
                    }
                }

                // 输出统计结果
                std::cout << "输出指令发送过快次数: " << count2 << std::endl;
                std::cout << "输出指令发送过慢次数: " << count1 << std::endl;
                std::cout << "无指令发送次数: " << count3 << std::endl;
                std::cout << "设备无反馈次数: " << noFeedbackCount << std::endl;
                std::cout << "反馈超过500微秒次数: " << slowFeedbackCount << std::endl;
                std::cout << "指令解析错误次数: " << errorCount << std::endl;
            }
            
        }
        
    }
}


// 存取数据函数
void process_can_data(const struct can_frame & frame, int interface_index) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    int packetIndex = write_indices[interface_index] % BUFFER_SIZE; // 确保索引在范围内

    if (static_cast<size_t>(write_indices[interface_index]) < BUFFER_SIZE) {   // 有个警告，转换一下类型
        g_packet_buffer[interface_index][packetIndex][25] = (frame.can_id & CAN_RTR_FLAG) ? 1 : 0; // 设置帧类型

        std::memset(g_packet_buffer[interface_index][packetIndex].data() + 1, 0, 2); // 清空第1、2个字节

        std::cout << interface_index << ":"<< std::hex << (frame.can_id & CAN_EFF_MASK) << std::endl;     // 调试用，后期要删除

        auto currentTimestamp = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTimestamp.time_since_epoch()).count(); //获取当前时间戳

        std::memcpy(&g_packet_buffer[interface_index][packetIndex][2], &timestamp, sizeof(timestamp));  //从第2个字节开始存储时间戳

        uint32_t frameID = frame.can_id & CAN_EFF_MASK;
        std::memcpy(&g_packet_buffer[interface_index][packetIndex][21], &frameID, sizeof(frameID));   //从22个字节开始存储ID值

        if (!(frame.can_id & CAN_RTR_FLAG)) {
            // 判断剩下的长度还够不够用，一般没问题，因为我申请了100个字节，26是之前存储时间戳和ID剩下来的
            if (frame.can_dlc <= (FRAME_SIZE - 26)) {
                std::memcpy(&g_packet_buffer[interface_index][packetIndex][26], frame.data, frame.can_dlc);
                g_packet_buffer[interface_index][packetIndex][26 + frame.can_dlc] = '\0';
            } else {
                std::cerr << "错误，存储字节数不够" << interface_index << std::endl;
            }
        }


        
        // ！！！！！！！超级超级无敌重要的一个索引在这里更新！！！！！！！！！ wo无语了，打这么多感叹号忘记累积了。。。。。。。
        write_indices[interface_index] = (write_indices[interface_index] + 1)% 6000; // 更新写入索引，循环利用
        std::cout << write_indices[interface_index]<<std::endl; //调试用

    } else {
        std::cerr << "Buffer full for interface " << interface_index << std::endl;
    }
}


// 主函数
int main() {
    struct ifreq ifr = {0};
    struct sockaddr_can can_addr = {0};
    int can_sockets[CAN_SIZE]; // 存储所有CAN套接字
    struct can_frame frame;

    // 创建所有 CAN 套接字
    for (size_t i = 0; i < CAN_SIZE; i++) 
    {
        can_sockets[i] = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (can_sockets[i] < 0) {
            perror("socket error");
            exit(EXIT_FAILURE);
        }

        // 指定绑定设备
        std::strcpy(ifr.ifr_name, INTERFACE_NAMES[i].c_str()); // 指定设备
        ioctl(can_sockets[i], SIOCGIFINDEX, &ifr);             // 绑定设备
        can_addr.can_family = AF_CAN;
        can_addr.can_ifindex = ifr.ifr_ifindex;

        // 将设备与套接字进行绑定
        if (bind(can_sockets[i], reinterpret_cast<struct sockaddr*>(&can_addr), sizeof(can_addr)) < 0) {  
            perror("bind error");
            close(can_sockets[i]);
            exit(EXIT_FAILURE);
        }
    }

    // 启动解析线程
    std::thread parserThread(parseData);

    // 启动结果总结线程
    std::thread summaryThread(summarizeResults);

    while (true) {
        fd_set read_fds;     // fd_set 是一个用于表示文件描述符集合的结构，用于select,监视多个文件描述符
        FD_ZERO(&read_fds);  
        int max_fd = -1;



        // 将所有套接字添加到 fd_set 中
        for (size_t i = 0; i < CAN_SIZE; i++) {     // 
            FD_SET(can_sockets[i], &read_fds);   //  FD_SET 是一个宏，用于将一个文件描述符添加到 fd_set 集合中。
            if (can_sockets[i] > max_fd) {
                max_fd = can_sockets[i];
            }
        }

        int ret = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
        if (ret < 0) {
            perror("Select");
            break;
        }

        // 检查每个套接字是否有数据可读
        for (size_t i = 0; i < CAN_SIZE; i++) {
            if (FD_ISSET(can_sockets[i], &read_fds)) {     // &read_fds是一个指针，指向文件描述符
                read(can_sockets[i], &frame, sizeof(frame));
                process_can_data(frame, i);

            }
        }
    }


    //这里已经是循环外面了！！！
    // 关闭所有套接字
    for (size_t i = 0; i < CAN_SIZE; i++) {
        close(can_sockets[i]);
    }

    // 关闭套接字
    running = false; // 停止解析线程
    data_condition.notify_all(); // 通知解析线程退出
    parserThread.join(); // 等待解析线程结束
    summaryThread.join(); // 等待总结线程结束

    return EXIT_SUCCESS;
}