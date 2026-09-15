#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include "controller/LogicalControl.h"

enum class SourceKind { Button, Hat, Axis };

// One physical event that can be bound: a button, a hat direction, or an analog
// axis. `dir` is only meaningful for hats (-1 or +1).
struct PhysicalSource {
    SourceKind kind = SourceKind::Button;
    int code = -1;
    int dir = 0;

    bool operator==(const PhysicalSource& o) const {
        return kind == o.kind && code == o.code && dir == o.dir;
    }
};

struct PhysicalSourceHash {
    std::size_t operator()(const PhysicalSource& s) const {
        std::size_t h = std::hash<int>{}(static_cast<int>(s.kind));
        h ^= std::hash<int>{}(s.code) << 1;
        h ^= std::hash<int>{}(s.dir) << 2;
        return h;
    }
};

inline PhysicalSource buttonSource(int code) { return { SourceKind::Button, code, 0 }; }
inline PhysicalSource axisSource(int code)   { return { SourceKind::Axis, code, 0 }; }
inline PhysicalSource hatSource(int code, int dir) { return { SourceKind::Hat, code, dir }; }

class Mapping {
public:
    Mapping() = default;
    Mapping(Mapping&& other) noexcept;
    Mapping& operator=(Mapping&& other) noexcept;
    Mapping(const Mapping& other);
    Mapping& operator=(const Mapping& other);

    // path = "mappings/<target>/<source>.json", e.g. "mappings/xbox/usb.json"
    static std::string pathFor(const std::string& target, const std::string& source);

    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    // In-memory JSON form — used when a Mapping is nested inside a DeviceProfile
    // rather than owning its own top-level file.
    bool loadFromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;

    // Targets for one physical source (digital and analog sides).
    std::vector<LogicalControl> controlTargets(const PhysicalSource& source) const;
    std::vector<LogicalAxis> axisTargets(const PhysicalSource& source) const;

    // Convenience wrappers (plain digital sources are the common case).
    std::vector<LogicalControl> translateButton(int physicalCode) const;
    std::vector<LogicalAxis> translateButtonAsAxis(int physicalCode) const;
    std::vector<LogicalAxis> translateAxis(int physicalCode) const;
    std::vector<LogicalControl> translateHat(int code, int dir) const;
    std::vector<LogicalAxis> translateHatAsAxis(int code, int dir) const;

    // Binding a target to a source REBINDS it: the logical control/axis is removed
    // from every other source first (replace semantics — one source per target).
    // A single source may still drive many targets (double mapping).
    void addMapping(const PhysicalSource& source, LogicalControl logical);
    void addMapping(const PhysicalSource& source, LogicalAxis axis);

    // Plain digital wrappers.
    void addButtonMapping(int physicalCode, LogicalControl logical);
    void addButtonAsAxisMapping(int physicalCode, LogicalAxis axis);
    void addAxisMapping(int physicalCode, LogicalAxis axis);
    void addHatMapping(int code, int dir, LogicalControl logical);
    void addHatAsAxisMapping(int code, int dir, LogicalAxis axis);

    // Reverse lookup: which physical source currently drives a logical target.
    std::optional<PhysicalSource> sourceFor(LogicalControl logical) const;
    std::optional<PhysicalSource> sourceFor(LogicalAxis axis) const;

    // Unbind a logical target entirely (combo "— none —").
    void clearTarget(LogicalControl logical);
    void clearTarget(LogicalAxis axis);

    void clear();   // drop every mapping (used at the start of a guided walk)

    size_t mappingCount() const;   // total distinctly-bound physical sources

private:
    mutable std::mutex mutex_;
    std::unordered_map<PhysicalSource, std::vector<LogicalControl>, PhysicalSourceHash> controlBySource;
    std::unordered_map<PhysicalSource, std::vector<LogicalAxis>, PhysicalSourceHash> axisBySource;
};