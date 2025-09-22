#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>

using namespace std;

// 一些计算内存大小的函数
inline string printMemorySize(size_t bytes) {
    const double KB = 1024.0;
    const double MB = KB * 1024;
    const double GB = MB * 1024;

    ostringstream oss;
    cout << fixed << setprecision(2);
    oss.precision(2);
    oss.setf(ios::fixed);

    if(bytes < KB){
        cout << bytes << "Bytes";
        oss << bytes << "Bytes";
    }
    else if(bytes < MB){
        cout << bytes / KB << "KB";
        oss << (bytes / KB) << "KB";
    }
    else if(bytes < GB){
        cout << bytes / MB << "MB";
        oss << bytes / MB << "MB";
    }
    else{
        cout << bytes / GB << "GB";
        oss << bytes / GB << "GB";
    }

    return oss.str();
}

inline std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
inline void writeToLog(const std::string& message, const std::string& logFile) {
    std::ofstream logStream(logFile, std::ios::app);
    if (!logStream.is_open()) {
        std::cerr << "Failed to open log file: " << logFile << std::endl;
        return;
    }
    logStream << "[" << getTimestamp() << "] " << message << std::endl;
    logStream.close();
}


