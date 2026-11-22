#include "dataset_reader.hpp"
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace {
using View = std::string_view;
View trim(View field) {
    const auto first = field.find_first_not_of(" \r\t");
    if (first == View::npos) return {};
    return field.substr(first, field.find_last_not_of(" \r\t") - first + 1);
}
std::vector<View> split(View row) {
    std::vector<View> fields;
    for (auto comma = row.find(','); comma != View::npos; comma = row.find(',')) {
        fields.push_back(trim(row.substr(0, comma)));
        row.remove_prefix(comma + 1);
    }
    fields.push_back(trim(row));
    return fields;
}
std::uint64_t number(View field, int base, const char* error) {
    if (base == 16 && (field.starts_with("0x") || field.starts_with("0X"))) {
        field.remove_prefix(2);
    }
    if (field.empty()) throw std::invalid_argument(error);
    std::uint64_t value{};
    auto [end, status] = std::from_chars(field.data(), field.data() + field.size(), value, base);
    if (status != std::errc{} || end != field.data() + field.size()) {
        throw std::invalid_argument(error);
    }
    return value;
}
std::int64_t timestamp(View field) {
    constexpr std::uint64_t billion = 1'000'000'000;
    const auto dot = field.find('.');
    const auto seconds = number(field.substr(0, dot), 10, "INVALID_TIMESTAMP");
    std::uint64_t fraction{};
    if (dot != View::npos) {
        const auto digits = field.substr(dot + 1);
        if (digits.size() > 9) throw std::invalid_argument("TIMESTAMP_PRECISION");
        fraction = number(digits, 10, "INVALID_TIMESTAMP");
        for (auto i = digits.size(); i < 9; ++i) fraction *= 10;
    }
    if (seconds > (std::numeric_limits<std::int64_t>::max() - fraction) / billion) {
        throw std::invalid_argument("TIMESTAMP_OVERFLOW");
    }
    return static_cast<std::int64_t>(seconds * billion + fraction);
}
std::optional<bool> flag(View field) {
    if (field == "T" || field == "Attack") return true;
    if (field == "R" || field == "Normal") return false;
    return {};
}
void parse(const std::vector<View>& fields, bool packed, CanFrame& frame,
           std::optional<bool>& label) {
    if (fields.size() < 3 || fields[0].empty() || fields[1].empty() || fields[2].empty()) {
        throw std::invalid_argument("MISSING_FIELDS");
    }
    frame.timestamp_ns = timestamp(fields[0]);
    const auto id = number(fields[1], 16, "INVALID_CAN_ID");
    if (id > 0x1fffffff) throw std::invalid_argument("INVALID_CAN_ID");
    frame.can_id = static_cast<std::uint32_t>(id);
    const auto dlc = number(fields[2], 10, "INVALID_DLC");
    if (dlc > 8) throw std::invalid_argument("INVALID_DLC");
    frame.dlc = static_cast<std::uint8_t>(dlc);

    std::array<std::string, 8> packed_bytes;
    std::vector<View> bytes;
    if (packed) {
        if (fields.size() < 5 || fields[4].empty()) throw std::invalid_argument("MISSING_FIELDS");
        if (fields.size() > 6 || !(label = flag(fields[4]))) {
            throw std::invalid_argument("INVALID_PACKED_RECORD");
        }
        auto field = fields[3];
        if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
            field = field.substr(1, field.size() - 2);
        }
        std::istringstream payload{std::string(field)};
        for (std::string byte; payload >> byte;) {
            if (bytes.size() == 8) throw std::invalid_argument("INVALID_PAYLOAD_LENGTH");
            packed_bytes[bytes.size()] = byte;
            bytes.push_back(packed_bytes[bytes.size()]);
        }
    } else {
        auto end = fields.size();
        while (end > 3 && fields[end - 1].empty()) --end;
        if (end > 3 && (label = flag(fields[end - 1]))) --end;
        if (end > 11) throw std::invalid_argument("INVALID_PAYLOAD_LENGTH");
        bytes.assign(fields.begin() + 3, fields.begin() + end);
    }
    if (bytes.size() < dlc || (packed && bytes.size() != dlc)) {
        throw std::invalid_argument("INVALID_PAYLOAD_LENGTH");
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i >= dlc) {
            if (bytes[i].empty()) continue;
            if (bytes.size() != 8) throw std::invalid_argument("INVALID_PAYLOAD_LENGTH");
        } else if (bytes[i].empty()) {
            throw std::invalid_argument("INVALID_PAYLOAD_LENGTH");
        }
        if (bytes[i].size() != 2) throw std::invalid_argument("INVALID_PAYLOAD_BYTE");
        const auto byte = number(bytes[i], 16, "INVALID_PAYLOAD_BYTE");
        if (i < dlc) frame.payload[i] = static_cast<std::uint8_t>(byte);
    }
}

void parse_normal_log(View row, CanFrame& frame, std::optional<bool>& label) {
    std::istringstream input{std::string(row)};
    std::string timestamp_key, time, id_key, id, channel, dlc_key, dlc;
    if (!(input >> timestamp_key >> time >> id_key >> id >> channel >> dlc_key >> dlc)
        || timestamp_key != "Timestamp:" || id_key != "ID:" || dlc_key != "DLC:") {
        throw std::invalid_argument("INVALID_NORMAL_LOG_RECORD");
    }
    std::array<std::string, 8> payload;
    std::vector<View> fields{time, id, dlc};
    for (std::string byte; input >> byte;) {
        const auto index = fields.size() - 3;
        if (index == payload.size()) throw std::invalid_argument("INVALID_PAYLOAD_LENGTH");
        payload[index] = byte;
        fields.push_back(payload[index]);
    }
    if (fields.size() - 3 != number(dlc, 10, "INVALID_DLC")) {
        throw std::invalid_argument("INVALID_PAYLOAD_LENGTH");
    }
    parse(fields, false, frame, label);
}
}

DataQuality stream_hcrl(const std::string& path,
    const std::function<void(CanFrame, std::optional<FrameLabel>)>& consume) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open capture: " + path);
    DataQuality quality;
    std::unordered_set<std::uint32_t> ids;
    std::optional<std::int64_t> last_timestamp;
    bool packed = false;
    bool requires_label = false;
    bool normal_log = false;
    std::size_t line = 0;
    for (std::string row; std::getline(input, row);) {
        ++line;
        if (line == 1 && row.starts_with("\xef\xbb\xbf")) row.erase(0, 3);
        if (line == 1) normal_log = trim(row).starts_with("Timestamp:");
        const auto fields = normal_log ? std::vector<View>{} : split(row);
        if (line == 1 && !normal_log && (fields[0] == "Timestamp" || fields[0] == "timestamp")) {
            packed = fields.size() >= 5 && fields[1] == "Arbitration_ID";
            for (const auto field : fields) {
                if (field == "Flag" || field == "flag" || field == "Class") requires_label = true;
            }
            continue;
        }
        CanFrame frame;
        std::optional<bool> label;
        try {
            if (normal_log) parse_normal_log(row, frame, label);
            else parse(fields, packed, frame, label);
            if (requires_label && !label) throw std::invalid_argument("MISSING_LABEL");
        } catch (const std::invalid_argument& error) {
            ++quality.malformed;
            quality.issues.push_back({line, error.what()});
            continue;
        }
        if (last_timestamp && frame.timestamp_ns < *last_timestamp) {
            ++quality.regressions;
            quality.issues.push_back({line, "TIMESTAMP_REGRESSION"});
            continue;
        }
        last_timestamp = frame.timestamp_ns;
        frame.sequence = quality.valid++;
        ids.insert(frame.can_id);
        std::optional<FrameLabel> frame_label;
        if (label) frame_label = FrameLabel{frame.sequence, *label};
        consume(frame, frame_label);
    }
    if (!input.eof()) throw std::runtime_error("Error reading capture: " + path);
    quality.unique_ids = ids.size();
    return quality;
}

Dataset read_hcrl(const std::string& path) {
    Dataset dataset;
    dataset.quality = stream_hcrl(path, [&](CanFrame frame, std::optional<FrameLabel> label) {
        dataset.frames.push_back(frame);
        if (label) dataset.labels.push_back(*label);
    });
    return dataset;
}
