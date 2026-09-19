// SPDX-License-Identifier: MIT
#pragma once
#include "ConfigurationChannel.h"
#include "ConfigurationRequests.h"
#include <optional>
#include <atomic>
#include <future>
#include <thread>

namespace HidHide::Maintenance
{
    inline constexpr auto AdmissionName = L"Global\\HidHide.AppProfiles.Admission.v1";
    inline constexpr auto BarrierName = L"Global\\HidHide.AppProfiles.Maintenance.v1";

    inline bool DurableMarkerExists(HKEY hive, PCWSTR path)
    {
        HKEY key{};
        auto status = ::RegOpenKeyExW(hive, path, 0, KEY_READ | KEY_WOW64_64KEY, &key);
        if (status == ERROR_SUCCESS) { ::RegCloseKey(key); return true; }
        if (status != ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("Cannot verify durable maintenance state; configuration is blocked");
        return false;
    }

    inline bool DurableRestartRequired()
    {
        HKEY key{};
        auto status = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\mikeev261\\HidHide\\Maintenance", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
        if (status == ERROR_FILE_NOT_FOUND) return false;
        if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot inspect durable maintenance state; configuration is blocked");
        DWORD value{}, type{}, size{ sizeof(value) };
        status = ::RegQueryValueExW(key, L"RestartRequired", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
        ::RegCloseKey(key);
        if (status == ERROR_FILE_NOT_FOUND) return false;
        if (status != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(value) || value > 1)
            throw std::runtime_error("Durable maintenance state is invalid; configuration is blocked");
        return value != 0;
    }

    // The barrier is an event whose existence matters, not its signalled state.
    // An elevated setup worker may retain its own handle across an ordinary-user
    // session handoff. No reset/set operation can silently reopen admission.
    inline bool Active(PCWSTR name = BarrierName)
    {
        if (std::wstring(name) == BarrierName)
        {
            // Durable, administrator-owned exclusion survives worker termination
            // and reboot. Presence (including an incomplete marker) blocks writes.
            if (DurableMarkerExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\mikeev261\\HidHide\\Maintenance")) return true;
        }
        auto event = ::OpenEventW(SYNCHRONIZE, FALSE, name);
        if (event) { ::CloseHandle(event); return true; }
        if (::GetLastError() == ERROR_FILE_NOT_FOUND) return false;
        throw std::runtime_error("Cannot verify maintenance barrier; configuration is blocked");
    }

    class Admission
    {
        Channel::Lease lease;
    public:
        explicit Admission(PCWSTR admissionName = AdmissionName, PCWSTR barrierName = BarrierName)
            : lease(admissionName, 250)
        {
            if (!lease.Acquired()) throw std::runtime_error("Configuration admission is busy; retry shortly");
            if (Active(barrierName))
            {
                if (std::wstring(barrierName) == BarrierName && DurableRestartRequired())
                    throw std::runtime_error("HidHide Profiles setup is waiting for restart verification. Restart Windows if you have not already, then run setup again before opening HidHide Profiles");
                throw std::runtime_error("HidHide Profiles setup has not finished. Complete or cancel setup before opening HidHide Profiles");
            }
        }
    };

    class Barrier
    {
        Channel::Handle event{ nullptr, &::CloseHandle };
    public:
        explicit Barrier(PCWSTR admissionName = AdmissionName, PCWSTR barrierName = BarrierName)
        {
            // Serializes barrier creation with admission of all cooperating writes.
            Admission admission(admissionName, barrierName);
            Channel::Security security(L"D:P(A;;0x00100000;;;AU)(A;;GA;;;SY)(A;;GA;;;BA)");
            auto handle = ::CreateEventExW(&security.attributes, barrierName, CREATE_EVENT_MANUAL_RESET, SYNCHRONIZE);
            auto const error = ::GetLastError();
            event = Channel::Own(handle);
            if (error == ERROR_ALREADY_EXISTS) throw std::runtime_error("Another maintenance session is active");
        }
        static Channel::Handle Join(PCWSTR name = BarrierName)
        {
            return Channel::Own(::OpenEventW(SYNCHRONIZE, FALSE, name));
        }
    };

    inline void RequireOrdinaryUser()
    {
        HANDLE token{};
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) throw std::runtime_error("Cannot inspect maintenance user token");
        auto owner = Channel::Own(token);
        TOKEN_ELEVATION elevation{}; DWORD size{};
        if (!::GetTokenInformation(owner.get(), TokenElevation, &elevation, sizeof(elevation), &size))
            throw std::runtime_error("Cannot inspect maintenance elevation");
        if (elevation.TokenIsElevated) throw std::runtime_error("Start maintenance preparation as the ordinary configuration user, before administrator elevation");
    }

    struct Snapshot
    {
        bool driverPresent{};
        Configuration configuration;
        bool storedBaselineAvailable{};
    };

    enum class Control { Handoff, Release };

    // Anonymous stdout pipes do not support overlapped I/O. Keep ownership on
    // the caller thread while a cancellable writer delivers the bounded reply.
    inline void WriteControllerLine(HANDLE output, std::string const& line, DWORD timeoutMs = 5000)
    {
        if (timeoutMs > 5000 || line.size() > Protocol::MaxBytes * 2 + 16)
            throw std::runtime_error("Maintenance output exceeds limit");
        std::string bytes = line + "\n";
        std::atomic<bool> cancelled{ false };
        std::promise<void> completed;
        auto done = completed.get_future();
        bool success = false;
        std::thread writer([&]
        {
            size_t offset = 0;
            while (offset < bytes.size() && !cancelled.load())
            {
                DWORD written{};
                if (!::WriteFile(output, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr) || !written) break;
                offset += written;
            }
            success = offset == bytes.size();
            completed.set_value();
        });
        bool const expired = done.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready;
        if (expired)
        {
            cancelled = true;
            // Repeat cancellation to cover the race between checking cancelled
            // and entering WriteFile. Join before destroying the output buffer.
            do { ::CancelSynchronousIo(writer.native_handle()); }
            while (done.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready);
        }
        writer.join();
        if (expired || !success) throw std::runtime_error("Maintenance controller output failed or timed out");
    }

    inline Control ParseControl(std::string const& text)
    {
        if (text == "handoff") return Control::Handoff;
        if (text == "release") return Control::Release;
        throw std::runtime_error("Invalid maintenance control; expected handoff or release");
    }

    // This is a live exclusion/handshake only, not a durable migration journal.
    // The setup worker must join the barrier, acquire OwnerName after Handoff,
    // revalidate state and create its protected journal before driver changes.
    class Session
    {
        Barrier barrier; // Destroy last, after releasing configuration ownership.
        std::unique_ptr<Channel::Lease> owner;
        Snapshot snapshot;
    public:
        Session(std::function<Configuration()> const& prepareOwner,
                std::function<void()> const& requireNoRecovery,
                std::function<Snapshot()> const& readSnapshot,
                PCWSTR admissionName = AdmissionName, PCWSTR barrierName = BarrierName,
                PCWSTR ownerName = Channel::OwnerName, DWORD timeoutMs = 5000)
            : barrier(admissionName, barrierName), owner(std::make_unique<Channel::Lease>(ownerName))
        {
            std::optional<Configuration> confirmed;
            if (!owner->Acquired())
            {
                confirmed = prepareOwner(); // Authentication and bounded I/O remain in Channel::Exchange.
                owner = std::make_unique<Channel::Lease>(ownerName, timeoutMs);
                if (!owner->Acquired()) throw std::runtime_error("Manager did not release ownership after restoring baseline; maintenance refused");
            }
            requireNoRecovery(); // Always in the initiating user's context.
            snapshot = readSnapshot();
            if (confirmed && (!snapshot.driverPresent || snapshot.configuration != *confirmed))
                throw std::runtime_error("Configuration changed during maintenance handoff; preserve it and retry after resolving ownership");
        }
        Snapshot const& Confirmed() const { return snapshot; }
        void Handoff()
        {
            if (!owner) throw std::runtime_error("Maintenance ownership has already been handed off");
            owner.reset(); // The barrier still excludes ordinary GUI/CLI writers.
        }
    };
}
