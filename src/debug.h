#include <iomanip>
#include <sstream>
#include <fstream>
#include <chrono>

inline void logToFile(const std::string& function, int line, const std::string& message) {
    static std::ofstream logFile("/tmp/lotus_debug.log", std::ios::app);
    if (!logFile.is_open()) {
        return;
    }

    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    logFile << std::put_time(std::localtime(&time), "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count() << " [" << function << ":" << line << "] " << message
            << std::endl;
    logFile.flush();
}

#ifdef LOTUS_DEBUG
#define LOG(msg) logToFile(__func__, __LINE__, msg)
#else
#define LOG(msg)
#endif
