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

class Logger {
   public:
    Logger();
    void SetLoggerName(const std::string& logger_name);
    const std::string& GetLoggerName();
    std::ofstream m_output;
    std::string m_logger_name;
    void AssureOpen();
};

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

template <class T_SUM, class T>
class LoggerTerm {
   public:
    LoggerTerm(const std::string& name, Logger* logger) : m_sum(0), m_name(name), m_logger(logger) {}
    virtual void Add(T value) {
        m_sum += value;
        if (unlikely(should_flush())) {
            flush();
        }
        m_sum = 0;
    };

   protected:
    virtual bool should_flush() = 0;
    virtual void flush() = 0;
    T_SUM m_sum;
    std::string m_name;
    Logger* m_logger;
};

template <class T_SUM, class T>
class NumericLoggerTerm : public LoggerTerm<T_SUM, T> {
   public:
    using LoggerTerm<T_SUM, T>::LoggerTerm;

   protected:
    virtual bool should_flush() override { return false; }
    virtual void flush() override {
        LoggerTerm<T_SUM, T>::m_logger->m_output << LoggerTerm<T_SUM, T>::m_name << "\t" << LoggerTerm<T_SUM, T>::m_sum << "\n";
        LoggerTerm<T_SUM, T>::m_logger->m_output << std::endl;
    };
};

template <class T_SUM, class T>
class ContainerLoggerTerm : public LoggerTerm<T_SUM, T> {
   public:
    using LoggerTerm<T_SUM, T>::LoggerTerm;

   protected:
    virtual bool should_flush() { return false; }
    virtual void flush() {
        for (auto& i = LoggerTerm<T_SUM, T>::m_sum.begin(); i != LoggerTerm<T_SUM, T>::m_sum.end(); i++) {
            LoggerTerm<T_SUM, T>::m_logger->m_output << LoggerTerm<T_SUM, T>::m_name << "\t" << *i << "\n";
        }
        LoggerTerm<T_SUM, T>::m_logger->m_output << std::endl;
    };
};

/*
template <class T>
class StructedLoggerTermElement {
   public:
    StructedLoggerTermElement& operator+=(StructedLoggerTermElement& rhs) {}

   private:
    uint8_t max_struct_elements;
    std::vector<T> struct_elements;
};

template <class T>
class StructedLoggerTerm {
   public:
    LoggerTerm(const std::string& name, Logger* logger) : m_name(name), m_logger(logger){};
    virtual void Add(T value) = 0;

   public:
    std::v
};
*/

template <class T>
class AverageLoggerTerm : public NumericLoggerTerm<T, T> {
   public:
    AverageLoggerTerm(const std::string& name, uint32_t max_record, Logger* logger);
    void Add(T value) override;

   protected:
    uint32_t m_record_frequency;
    uint32_t m_index;
    T m_sum;

   private:
    virtual bool should_flush() override { return false; }
    virtual void flush() override {}
};

template <class T>
AverageLoggerTerm<T>::AverageLoggerTerm(const std::string& name, uint32_t max_record, Logger* logger)
    : NumericLoggerTerm<T, T>(name, logger), m_record_frequency(max_record), m_index(0), m_sum(0) {}

template <class T>
void AverageLoggerTerm<T>::Add(T value) {
    m_sum += value;
    ++m_index;
    if (unlikely(m_index == m_record_frequency)) {
        LoggerTerm<T, T>::m_logger->m_output << LoggerTerm<T, T>::m_name << "\t" << m_sum / m_index << "\n";
        m_sum = 0;
        m_index = 0;
    }
}

template <class T>
class TimeAverageLoggerTerm : public NumericLoggerTerm<T, T> {
   public:
    TimeAverageLoggerTerm(const std::string& name, uint64_t record_duration, Logger* logger);
    void Start();
    void Add(T value) override;
    void SetLoggerInterval(uint64_t m_record_duration);

   private:
    virtual bool should_flush() override { return false; }
    virtual void flush() override {}

    void DoOutput();
    uint64_t m_record_duration;
    T m_sum;
    uint64_t last_cycle;
};

template <class T>
TimeAverageLoggerTerm<T>::TimeAverageLoggerTerm(const std::string& name, uint64_t record_duration, Logger* logger)
    : m_record_duration(record_duration), m_sum(0), NumericLoggerTerm<T, T>(name, logger) {
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
    NumericLoggerTerm<T, T>::m_logger->AssureOpen();
    while (true) {
        uint64_t now = Cycles::rdtsc();
        double time = Cycles::to_microseconds(now - last_cycle);
        NumericLoggerTerm<T, T>::m_logger->m_output << NumericLoggerTerm<T, T>::m_name << "\t" << m_sum * 8.0 / time / 1000 << std::endl;
        m_sum = 0;
        last_cycle = now;
        Cycles::sleep(m_record_duration);
    }
};

template <class T>
void TimeAverageLoggerTerm<T>::SetLoggerInterval(uint64_t value) {
    m_record_duration = value;
}

template <class T_SUM, class T>
class OriginalLoggerTerm : public ContainerLoggerTerm<T_SUM, T> {
   public:
    OriginalLoggerTerm(const std::string& name, uint64_t max_size, Logger* logger);
    void Add(T value) override;

    void Flush() { flush(); };
    void SetFlushTime(uint32_t);

   protected:
    virtual bool should_flush() override { return ContainerLoggerTerm<T_SUM, T>::m_sum.size() == max_flushtime; }
    virtual void flush() override {
        for (auto& i : ContainerLoggerTerm<T_SUM, T>::m_sum) {
            ContainerLoggerTerm<T_SUM, T>::m_logger->m_output << ContainerLoggerTerm<T_SUM, T>::m_name << "\t" << *i << "\n";
        }
        ContainerLoggerTerm<T_SUM, T>::m_logger->m_output << std::endl;
        m_records.clear();
    };

   private:
    std::vector<T_SUM> m_records;
    uint32_t max_flushtime;
};

template <class T_SUM, class T>
OriginalLoggerTerm<T_SUM, T>::OriginalLoggerTerm(const std::string& name, uint64_t max_size, Logger* logger)
    : LoggerTerm<T_SUM, T>(name, logger), max_flushtime(UINT32_MAX) {
    m_records.resize(max_size);
};

template <class T_SUM, class T>
void OriginalLoggerTerm<T_SUM, T>::Add(T value) {
    m_records.push_back(value);
    if (unlikely(m_records.size() == max_flushtime)) {
        flush();
    }
}

template <class T_SUM, class T>
void OriginalLoggerTerm<T_SUM, T>::SetFlushTime(uint32_t flush_time) {
    max_flushtime = flush_time;
}

#endif /* COMMON_UTIL_H */