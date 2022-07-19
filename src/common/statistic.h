/*
 * author:   krayecho Yx <532820040@qq.com>
 * date:     202001112
 * brief:    log components
 */

#ifndef STATISTIC_H
#define STATISTIC_H

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "common/common_time.h"

class Logger;

template <class T>
class AverageLoggerTerm {
   public:
    AverageLoggerTerm(const std::string& name, uint32_t max_record, Logger* logger);
    void Add(T value);
    std::string m_name;
    uint32_t m_record_frequency;
    uint32_t m_index;
    T m_sum;
    Logger* m_logger;
    Cycles* clock;
};

template <class T>
class TimeAverageLoggerTerm {
   public:
    TimeAverageLoggerTerm(const std::string& name, uint64_t record_duration, Logger* logger);
    void Start();
    void Add(T value);
    void SetLoggerInterval(uint64_t m_record_duration);

   private:
    void DoOutput();
    std::string m_name;
    uint64_t m_record_duration;
    T m_sum;
    Logger* m_logger;
    uint64_t last_cycle;
};

class Logger {
   public:
    Logger();
    void SetLoggerName(const std::string& logger_name);
    const std::string& GetLoggerName();
    std::ofstream m_output;
    std::string m_logger_name;
    void AssureOpen();
};

template <class T>
AverageLoggerTerm<T>::AverageLoggerTerm(const std::string& name, uint32_t max_record, Logger* logger)
    : m_name(name), m_record_frequency(max_record), m_index(0), m_sum(0), m_logger(logger) {}

template <class T>
void AverageLoggerTerm<T>::Add(T value) {
    m_sum += value;
    ++m_index;
    if (unlikely(m_index == m_record_frequency)) {
        m_logger->m_output << m_name << "\t" << m_sum / m_index << std::endl;
        m_sum = 0;
        m_index = 0;
    }
}

template <class T>
TimeAverageLoggerTerm<T>::TimeAverageLoggerTerm(const std::string& name, uint64_t record_duration, Logger* logger)
    : m_name(name), m_record_duration(record_duration), m_sum(0), m_logger(logger) {
    Cycles::init();
};

template <class T>
void TimeAverageLoggerTerm<T>::Start() {
    auto log_thread = std::thread(&TimeAverageLoggerTerm<T>::DoOutput, this);
    pthread_setname(log_thread.native_handle(), "log-thread");
    log_thread.detach();
};

template <class T>
void TimeAverageLoggerTerm<T>::Add(T value) {
    m_sum += value;
};

template <class T>
void TimeAverageLoggerTerm<T>::DoOutput() {
    m_logger->AssureOpen();
    while (true) {
        uint64_t now = Cycles::rdtsc();
        double time = Cycles::to_microseconds(now - last_cycle);
        m_logger->m_output << m_name << "\t" << m_sum * 8.0 / time / 1000 << std::endl;
        m_sum = 0;
        last_cycle = now;
        usleep(m_record_duration);
    }
};

template <class T>
void TimeAverageLoggerTerm<T>::SetLoggerInterval(uint64_t value) {
    m_record_duration = value;
}

inline Logger::Logger(){};
inline void Logger::SetLoggerName(const std::string& logger_name) {
    m_logger_name = logger_name;
    if (m_output.is_open()) {
        m_output.close();
    }
    AssureOpen();
}

inline const std::string& Logger::GetLoggerName() { return m_logger_name; };

inline void Logger::AssureOpen() {
    if (m_output.is_open()) {
        return;
    }
    m_output.open(m_logger_name, std::ios::out);
}

#endif /* COMMON_UTIL_H */