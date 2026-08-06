#include "mix153060_capture.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace mix153060 {

namespace {

std::string normalize_instrument(const std::string& value) {
    const std::string::size_type dot = value.find('.');
    return dot == std::string::npos ? value : value.substr(0, dot);
}

bool path_is_directory(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool ensure_directory(const std::string& path, std::string* error) {
    if (path.empty()) {
        if (error != 0) {
            *error = "capture directory is empty";
        }
        return false;
    }
    std::string current;
    if (path[0] == '/') {
        current = "/";
    }
    std::size_t begin = path[0] == '/' ? 1 : 0;
    while (begin <= path.size()) {
        const std::size_t slash = path.find('/', begin);
        const std::size_t end = slash == std::string::npos ? path.size() : slash;
        if (end > begin) {
            if (!current.empty() && current[current.size() - 1] != '/') {
                current += '/';
            }
            current += path.substr(begin, end - begin);
            if (!path_is_directory(current) && ::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                if (error != 0) {
                    std::ostringstream message;
                    message << "cannot create capture directory " << current << ": "
                            << std::strerror(errno);
                    *error = message.str();
                }
                return false;
            }
            if (!path_is_directory(current)) {
                if (error != 0) {
                    *error = "capture path exists but is not a directory: " + current;
                }
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        begin = slash + 1;
    }
    return path_is_directory(path);
}

bool file_is_empty(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) != 0 || info.st_size == 0;
}

std::uint64_t monotonic_time_ns() {
    struct timespec value;
    if (::clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(value.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

bool header_contains(const std::string& path, const char* field) {
    std::ifstream input(path.c_str());
    std::string header;
    return input.is_open() && std::getline(input, header) &&
           header.find(field) != std::string::npos;
}

const char* order_kind_name(OrderKind kind) {
    switch (kind) {
    case OrderKind::kMarket:
        return "market";
    case OrderKind::kSelfBest:
        return "self-best";
    case OrderKind::kLimit:
    default:
        return "limit";
    }
}

const char* trade_kind_name(TradeKind kind) {
    return kind == TradeKind::kCancel ? "cancel" : "fill";
}

void append_book_columns(std::ostringstream* row, const Sample& sample) {
    for (std::size_t level = 0; level < 10; ++level) {
        *row << ',' << sample.bid_price[level];
    }
    for (std::size_t level = 0; level < 10; ++level) {
        *row << ',' << sample.ask_price[level];
    }
    for (std::size_t level = 0; level < 10; ++level) {
        *row << ',' << sample.bid_volume[level];
    }
    for (std::size_t level = 0; level < 10; ++level) {
        *row << ',' << sample.ask_volume[level];
    }
}

void append_book_header(std::ostringstream* header) {
    for (std::size_t level = 0; level < 10; ++level) {
        *header << ",bid_price_" << (level + 1);
    }
    for (std::size_t level = 0; level < 10; ++level) {
        *header << ",ask_price_" << (level + 1);
    }
    for (std::size_t level = 0; level < 10; ++level) {
        *header << ",bid_volume_" << (level + 1);
    }
    for (std::size_t level = 0; level < 10; ++level) {
        *header << ",ask_volume_" << (level + 1);
    }
}

}  // namespace

CaptureConfig::CaptureConfig()
    : enabled(false),
      directory(),
      prefix("mix153060"),
      instruments(),
      record_events(true),
      record_samples(true),
      capture_only(false),
      flush_rows(4096),
      flush_interval_ms(1000) {}

Capture::Capture(const CaptureConfig& config)
    : enabled_(config.enabled),
      ready_(!config.enabled),
      record_events_(config.record_events),
      record_samples_(config.record_samples),
      flush_rows_(config.flush_rows == 0 ? 4096 : config.flush_rows),
      flush_interval_ms_(config.flush_interval_ms == 0 ? 1000 : config.flush_interval_ms),
      pending_rows_(0),
      last_flush_mono_ns_(monotonic_time_ns()),
      instruments_(),
      error_(),
      events_path_(),
      samples_path_(),
      market_resolutions_path_(),
      events_buffer_(),
      samples_buffer_(),
      market_resolutions_buffer_(),
      events_file_(),
      samples_file_(),
      market_resolutions_file_() {
    if (!enabled_) {
        return;
    }
    if (!record_events_ && !record_samples_) {
        fail("capture enabled but both events and samples are disabled");
        return;
    }
    if (config.prefix.empty() || config.prefix.find('/') != std::string::npos ||
        config.prefix.find('\\') != std::string::npos) {
        fail("capture prefix must be a non-empty file name");
        return;
    }
    for (std::size_t i = 0; i < config.instruments.size(); ++i) {
        const std::string instrument = normalize_instrument(config.instruments[i]);
        if (!instrument.empty()) {
            instruments_.insert(instrument);
        }
    }
    if (instruments_.empty()) {
        fail("capture requires a non-empty instrument allowlist");
        return;
    }
    if (!ensure_directory(config.directory, &error_)) {
        ready_ = false;
        return;
    }
    events_path_ = config.directory + "/" + config.prefix + "_events.csv";
    samples_path_ = config.directory + "/" + config.prefix + "_samples.csv";
    market_resolutions_path_ = config.directory + "/" + config.prefix +
                               "_market_resolutions.csv";
    if (record_events_) {
        const bool empty = file_is_empty(events_path_);
        if (!empty && !header_contains(events_path_, "source_continuity_valid")) {
            fail("event capture file uses an older header; rotate the output file: " +
                 events_path_);
            return;
        }
        events_file_.open(events_path_.c_str(), std::ios::out | std::ios::app);
        if (!events_file_.is_open()) {
            fail("cannot open event capture file: " + events_path_);
            return;
        }
        if (empty) {
            events_file_ << "instrument,event_type,event_kind,source,framework_receive_time,"
                            "exchange_time_us,local_time_us,app_sequence,price,volume,accepted,"
                            "book_mutation_ns,sample_work_ns,total_runtime_ns,samples_emitted,"
                            "buy_order_id,sell_order_id,source_continuity_valid\n";
            events_file_.flush();
        }
    }
    if (record_samples_) {
        const bool empty = file_is_empty(samples_path_);
        if (!empty && !header_contains(samples_path_, "source_continuity_valid")) {
            fail("sample capture file uses an older header; rotate the output file: " +
                 samples_path_);
            return;
        }
        samples_file_.open(samples_path_.c_str(), std::ios::out | std::ios::app);
        if (!samples_file_.is_open()) {
            fail("cannot open sample capture file: " + samples_path_);
            return;
        }
        if (empty) {
            samples_file_ << "instrument,exchange_time_us,local_time_us,app_sequence,cut_index,"
                             "row_in_stock_day,window_start_exchange_time_us,window_start_app_sequence,"
                             "window_start_cut_index,last_price,mid_price,turnover,volume,"
                             "amount_trigger,time_trigger,change_trigger,source,"
                             "framework_receive_time,raw_prediction,model_latency_ns";
            std::ostringstream header;
            append_book_header(&header);
            samples_file_ << header.str();
            for (std::size_t i = 0; i < kFeatureCount; ++i) {
                samples_file_ << ',' << factor_names()[i];
            }
            samples_file_ << ",source_continuity_valid\n";
            samples_file_.flush();
        }
    }
    {
        const bool empty = file_is_empty(market_resolutions_path_);
        if (!empty && !header_contains(market_resolutions_path_, "resolution_source")) {
            fail("market resolution capture file uses an older header; rotate the output file: " +
                 market_resolutions_path_);
            return;
        }
        market_resolutions_file_.open(market_resolutions_path_.c_str(),
                                      std::ios::out | std::ios::app);
        if (!market_resolutions_file_.is_open()) {
            fail("cannot open market resolution capture file: " + market_resolutions_path_);
            return;
        }
        if (empty) {
            market_resolutions_file_ << "instrument,app_sequence,exchange_time_us,local_time_us,"
                                        "derived_price,volume,buy,resolution_source,source,"
                                        "framework_receive_time,source_continuity_valid\n";
            market_resolutions_file_.flush();
        }
    }
    ready_ = true;
}

Capture::~Capture() {
    flush();
}

bool Capture::ready() const {
    return ready_;
}

bool Capture::enabled_for(const std::string& instrument) const {
    return enabled_ && ready_ && instruments_.find(normalize_instrument(instrument)) != instruments_.end();
}

const std::string& Capture::error() const {
    return error_;
}

const std::string& Capture::events_path() const {
    return events_path_;
}

const std::string& Capture::samples_path() const {
    return samples_path_;
}

const std::string& Capture::market_resolutions_path() const {
    return market_resolutions_path_;
}

void Capture::fail(const std::string& message) {
    ready_ = false;
    error_ = message;
    if (events_file_.is_open()) {
        events_file_.close();
    }
    if (samples_file_.is_open()) {
        samples_file_.close();
    }
    if (market_resolutions_file_.is_open()) {
        market_resolutions_file_.close();
    }
}

void Capture::record_event(const std::string& instrument,
                           const char* event_type,
                           const char* event_kind,
                           std::int64_t app_sequence,
                           std::int64_t exchange_time_us,
                           std::int64_t local_time_us,
                           double price,
                           std::int64_t volume,
                           std::int64_t buy_order_id,
                           std::int64_t sell_order_id,
                           short source,
                           std::int64_t framework_receive_time,
                           const EventTiming& timing,
                           bool accepted,
                           std::size_t samples_emitted,
                           bool source_continuity_valid) {
    if (!enabled_for(instrument) || !record_events_) {
        return;
    }
    char row[1024];
    const int length = std::snprintf(
        row, sizeof(row),
        "%s,%s,%s,%d,%lld,%lld,%lld,%lld,%.17g,%lld,%d,%llu,%llu,%llu,%llu,%lld,%lld,%d\n",
        normalize_instrument(instrument).c_str(), event_type, event_kind,
        static_cast<int>(source), static_cast<long long>(framework_receive_time),
        static_cast<long long>(exchange_time_us), static_cast<long long>(local_time_us),
        static_cast<long long>(app_sequence), price, static_cast<long long>(volume),
        accepted ? 1 : 0, static_cast<unsigned long long>(timing.book_mutation_ns),
        static_cast<unsigned long long>(timing.sample_work_ns),
        static_cast<unsigned long long>(timing.total_runtime_ns),
        static_cast<unsigned long long>(samples_emitted),
        static_cast<long long>(buy_order_id), static_cast<long long>(sell_order_id),
        source_continuity_valid ? 1 : 0);
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(row)) {
        fail("event capture row formatting failed");
        return;
    }
    events_buffer_.append(row, static_cast<std::size_t>(length));
    ++pending_rows_;
    maybe_flush();
}

void Capture::record_order(const std::string& instrument,
                           const OrderEvent& event,
                           short source,
                           std::int64_t framework_receive_time,
                           const EventTiming& timing,
                           bool accepted,
                           std::size_t samples_emitted,
                           bool source_continuity_valid) {
    record_event(instrument, "order", order_kind_name(event.kind), event.app_sequence,
                 event.exchange_time_us, event.local_time_us, event.price, event.volume,
                 event.app_sequence, 0, source, framework_receive_time, timing, accepted,
                 samples_emitted, source_continuity_valid);
}

void Capture::record_trade(const std::string& instrument,
                           const TradeEvent& event,
                           short source,
                           std::int64_t framework_receive_time,
                           const EventTiming& timing,
                           bool accepted,
                           std::size_t samples_emitted,
                           bool source_continuity_valid) {
    record_event(instrument, "trade", trade_kind_name(event.kind), event.app_sequence,
                 event.exchange_time_us, event.local_time_us, event.price, event.volume,
                 event.buy_order_id, event.sell_order_id, source, framework_receive_time,
                 timing, accepted, samples_emitted, source_continuity_valid);
}

void Capture::record_market_resolution(const std::string& instrument,
                                       const OrderEvent& event,
                                       bool from_linked_fill,
                                       short source,
                                       std::int64_t framework_receive_time,
                                       bool source_continuity_valid) {
    if (!enabled_for(instrument) || !market_resolutions_file_.is_open()) {
        return;
    }
    char row[512];
    const int length = std::snprintf(
        row, sizeof(row), "%s,%lld,%lld,%lld,%.17g,%lld,%d,%s,%d,%lld,%d\n",
        normalize_instrument(instrument).c_str(),
        static_cast<long long>(event.app_sequence),
        static_cast<long long>(event.exchange_time_us),
        static_cast<long long>(event.local_time_us), event.price,
        static_cast<long long>(event.volume), event.buy ? 1 : 0,
        from_linked_fill ? "final-linked-fill" : "opposite-best", static_cast<int>(source),
        static_cast<long long>(framework_receive_time), source_continuity_valid ? 1 : 0);
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(row)) {
        fail("market resolution capture row formatting failed");
        return;
    }
    market_resolutions_buffer_.append(row, static_cast<std::size_t>(length));
    ++pending_rows_;
    maybe_flush();
}

void Capture::record_sample(const std::string& instrument,
                            const Sample& sample,
                            short source,
                            std::int64_t framework_receive_time,
                            float raw_prediction,
                            std::uint64_t model_latency_ns,
                            bool source_continuity_valid) {
    if (!enabled_for(instrument) || !record_samples_) {
        return;
    }
    std::ostringstream row;
    row << std::setprecision(17);
    row << normalize_instrument(instrument) << ',' << sample.exchange_time_us << ','
        << sample.local_time_us << ',' << sample.app_sequence << ',' << sample.cut_index
        << ',' << sample.row_in_stock_day << ',' << sample.window_start_exchange_time_us
        << ',' << sample.window_start_app_sequence << ',' << sample.window_start_cut_index
        << ',' << sample.last_price << ',' << sample.mid_price << ',' << sample.turnover
        << ',' << sample.volume << ',' << (sample.amount_trigger ? 1 : 0) << ','
        << (sample.time_trigger ? 1 : 0) << ',' << (sample.change_trigger ? 1 : 0) << ','
        << source << ',' << framework_receive_time << ',' << raw_prediction << ','
        << model_latency_ns;
    append_book_columns(&row, sample);
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        row << ',' << sample.factors[i];
    }
    row << ',' << (source_continuity_valid ? 1 : 0) << '\n';
    samples_buffer_ += row.str();
    ++pending_rows_;
    maybe_flush();
}

void Capture::maybe_flush() {
    const std::uint64_t now = monotonic_time_ns();
    const bool timed = now > last_flush_mono_ns_ &&
        now - last_flush_mono_ns_ >=
            static_cast<std::uint64_t>(flush_interval_ms_) * 1000000ULL;
    if (pending_rows_ >= flush_rows_ || timed) {
        flush();
    }
}

void Capture::flush() {
    if (events_file_.is_open() && !events_buffer_.empty()) {
        events_file_.write(events_buffer_.data(), events_buffer_.size());
        events_file_.flush();
        events_buffer_.clear();
    }
    if (samples_file_.is_open() && !samples_buffer_.empty()) {
        samples_file_.write(samples_buffer_.data(), samples_buffer_.size());
        samples_file_.flush();
        samples_buffer_.clear();
    }
    if (market_resolutions_file_.is_open() && !market_resolutions_buffer_.empty()) {
        market_resolutions_file_.write(market_resolutions_buffer_.data(),
                                       market_resolutions_buffer_.size());
        market_resolutions_file_.flush();
        market_resolutions_buffer_.clear();
    }
    pending_rows_ = 0;
    last_flush_mono_ns_ = monotonic_time_ns();
}

}  // namespace mix153060
