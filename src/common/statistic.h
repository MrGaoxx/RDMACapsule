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
    LoggerTerm(const std::string& name, Logger* logger) : m_sum(), m_name(name), m_logger(logger) {}
    virtual void Add(T& value) = 0;
    virtual void Add(T&& value) = 0;

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
    virtual void Add(T&& value) override {
        LoggerTerm<T_SUM, T>::m_sum += value;
        if (unlikely(should_flush())) {
            flush();
        }
        LoggerTerm<T_SUM, T>::m_sum = 0;
    };
    virtual void Add(T& value) override { Add(static_cast<T&&>(value)); };

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
    virtual void Add(T&& value) {
        LoggerTerm<T_SUM, T>::m_sum.emplace_back(value);
        if (unlikely(should_flush())) {
            flush();
        }
    }
    virtual void Add(T& value) {
        LoggerTerm<T_SUM, T>::m_sum.push_back(value);
        if (unlikely(should_flush())) {
            flush();
        }
    }
    virtual bool should_flush() { return false; }
    virtual void flush() {
        for (auto i = LoggerTerm<T_SUM, T>::m_sum.begin(); i != LoggerTerm<T_SUM, T>::m_sum.end(); i++) {
            LoggerTerm<T_SUM, T>::m_logger->m_output << LoggerTerm<T_SUM, T>::m_name << "\t" << *i << "\n";
        }
        LoggerTerm<T_SUM, T>::m_logger->m_output << std::endl;
    };
};

template <class T>
class AverageLoggerTerm : public NumericLoggerTerm<T, T> {
   public:
    AverageLoggerTerm(const std::string& name, uint32_t max_record, Logger* logger);
    void Add(T&& value) override;
    void Add(T& value) override { Add(static_cast<T&&>(value)); }

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
void AverageLoggerTerm<T>::Add(T&& value) {
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
    void Add(T&& value) override;
    void Add(T& value) override { Add(static_cast<T&&>(value)); }
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
void TimeAverageLoggerTerm<T>::Add(T&& value) {
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
    void Flush() { flush(); };
    void SetFlushTime(uint32_t);
    using ContainerLoggerTerm<T_SUM, T>::Add;

   protected:
    virtual bool should_flush() override { return false; }
    virtual void flush() override {
        ContainerLoggerTerm<T_SUM, T>::m_logger->m_output << ContainerLoggerTerm<T_SUM, T>::m_sum << std::endl;
        ContainerLoggerTerm<T_SUM, T>::m_sum.clear();
    };

   private:
    uint32_t max_flushtime;
};

template <class T_SUM, class T>
OriginalLoggerTerm<T_SUM, T>::OriginalLoggerTerm(const std::string& name, uint64_t max_size, Logger* logger)
    : ContainerLoggerTerm<T_SUM, T>(name, logger), max_flushtime(UINT32_MAX) {
    ContainerLoggerTerm<T_SUM, T>::m_sum.resize(max_size);
};

template <class T_SUM, class T>
void OriginalLoggerTerm<T_SUM, T>::SetFlushTime(uint32_t flush_time) {
    max_flushtime = flush_time;
}

struct TimeRecordTerm {
    uint64_t id_;
    uint8_t index_;
    uint64_t timestamp_;
};

struct TimeRecords {
    std::unordered_map<uint64_t, std::vector<uint64_t>> records;
    void push_back(TimeRecordTerm& term) {
        if (unlikely(records.count(term.id_) == 0)) {
            records.insert(std::pair(term.id_, std::vector<uint64_t>(64, 0)));
        }
        records[term.id_][term.index_] = term.timestamp_;
    };
    void emplace_back(TimeRecordTerm& term) { push_back(term); }
    void resize(std::size_t size) {}
    std::size_t size() { return records.size(); }
    void clear() { records.clear(); }
    decltype(records.begin()) begin() { return records.begin(); }
    decltype(records.end()) end() { return records.end(); }
};

inline std::ostream& operator<<(std::ostream& os, std::pair<uint64_t, std::vector<uint64_t>> record) {
    os << "id: " << record.first << " timestamps:";
    for (std::size_t i = 0; i < record.second.size(); i++) {
        os << " " << record.second[i];
    }
    os << std::endl;
    return os;
};

inline std::ostream& operator<<(std::ostream& os, TimeRecords& trs) {
    for (auto& record : trs.records) {
        os << "id: " << record.first << " timestamps:";
        for (std::size_t i = 0; i < record.second.size(); i++) {
            os << " " << record.second[i];
        }
        os << "\n";
    }
    os << std::endl;
    return os;
};
enum TimeRecordType { APP_SEND_BEFORE = 0, POST_SEND, APP_SEND_AFTER, POLLED_CQE, SEND_CB };

// Logger clientLogger;
// OriginalLoggerTerm<TimeRecords, TimeRecordTerm> clientTimeRecords;

#endif /* COMMON_UTIL_H */