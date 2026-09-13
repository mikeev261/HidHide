// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace HidHide
{
    using DeviceInstancePath = std::wstring;
    using DeviceInstancePaths = std::set<DeviceInstancePath>;
    using FullImageName = std::filesystem::path;
    using FullImageNames = std::set<FullImageName>;
    using AppProfiles = std::map<FullImageName, DeviceInstancePaths>;

    struct Configuration
    {
        bool active{};
        bool inverse{};
        DeviceInstancePaths blacklist;
        FullImageNames whitelist;
        AppProfiles profiles;
        bool operator==(Configuration const& other) const
        {
            return active == other.active && inverse == other.inverse && blacklist == other.blacklist
                && whitelist == other.whitelist && profiles == other.profiles;
        }
        bool operator!=(Configuration const& other) const { return !(*this == other); }
    };

    inline Configuration EffectiveConfiguration(Configuration baseline, DeviceInstancePaths const& contributions, bool suspended)
    {
        if (!suspended && !contributions.empty())
        {
            baseline.blacklist.insert(contributions.begin(), contributions.end());
            baseline.active = true;
        }
        return baseline;
    }

    // Versioned, bounded message format. No native structs/pointers cross the pipe.
    namespace Protocol
    {
        constexpr std::uint32_t Version = 1;
        constexpr std::size_t MaxBytes = 1024 * 1024;
        constexpr std::uint32_t MaxEntries = 4096;
        enum class Command : std::uint32_t { Read = 1, Commit = 2, PrepareMaintenance = 3 };
        struct Writer
        {
            std::vector<std::uint8_t> data;
            void Number(std::uint32_t n)
            {
                if (data.size() > MaxBytes - 4) throw std::runtime_error("Configuration exceeds protocol limit");
                for (int i = 0; i < 4; ++i) data.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
            }
            void String(std::wstring const& s)
            {
                if (s.size() > 32767 || s.find(L'\0') != std::wstring::npos) throw std::runtime_error("Invalid configuration string");
                Number(static_cast<std::uint32_t>(s.size()));
                for (auto c : s) Number(static_cast<std::uint32_t>(c));
            }
            template<class T> void Strings(T const& values)
            {
                if (values.size() > MaxEntries) throw std::runtime_error("Too many configuration entries");
                Number(static_cast<std::uint32_t>(values.size()));
                for (auto const& value : values) String(std::filesystem::path(value).native());
            }
            void State(Configuration const& s)
            {
                Number(s.active); Number(s.inverse); Strings(s.blacklist); Strings(s.whitelist);
                if (s.profiles.size() > MaxEntries) throw std::runtime_error("Too many profiles");
                Number(static_cast<std::uint32_t>(s.profiles.size()));
                for (auto const& [path, devices] : s.profiles) { String(path.native()); Strings(devices); }
            }
        };
        struct Reader
        {
            std::vector<std::uint8_t> const& data;
            std::size_t offset{};
            explicit Reader(std::vector<std::uint8_t> const& bytes) : data(bytes)
            {
                if (data.size() > MaxBytes) throw std::runtime_error("Oversized configuration message");
            }
            std::uint32_t Number()
            {
                if (data.size() - offset < 4) throw std::runtime_error("Truncated configuration message");
                std::uint32_t n{};
                for (int i = 0; i < 4; ++i) n |= static_cast<std::uint32_t>(data[offset++]) << (8 * i);
                return n;
            }
            std::uint32_t Count()
            {
                auto n = Number();
                if (n > MaxEntries) throw std::runtime_error("Too many configuration entries");
                return n;
            }
            bool Boolean()
            {
                auto n = Number();
                if (n > 1) throw std::runtime_error("Invalid configuration flag");
                return n != 0;
            }
            std::wstring String()
            {
                auto n = Number();
                if (n > 32767 || n > (data.size() - offset) / 4) throw std::runtime_error("Invalid string length");
                std::wstring value;
                for (std::uint32_t i = 0; i < n; ++i)
                {
                    auto c = Number();
                    if (!c || c > 65535) throw std::runtime_error("Invalid UTF-16 code unit");
                    value.push_back(static_cast<wchar_t>(c));
                }
                return value;
            }
            template<class T> T Strings()
            {
                T result;
                auto count = Count();
                for (std::uint32_t i = 0; i < count; ++i)
                    if (!result.emplace(String()).second) throw std::runtime_error("Duplicate configuration entry");
                return result;
            }
            Configuration State()
            {
                Configuration s;
                s.active = Boolean(); s.inverse = Boolean();
                s.blacklist = Strings<DeviceInstancePaths>(); s.whitelist = Strings<FullImageNames>();
                auto count = Count();
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    auto path = String(); auto devices = Strings<DeviceInstancePaths>();
                    if (!s.profiles.emplace(path, devices).second) throw std::runtime_error("Duplicate profile");
                }
                return s;
            }
            void End() const { if (offset != data.size()) throw std::runtime_error("Trailing configuration data"); }
        };
    }
}
