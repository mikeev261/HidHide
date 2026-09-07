// SPDX-License-Identifier: MIT
#pragma once
#include <Windows.h>
#include <sddl.h>
#include <functional>
#include <memory>
#include "Configuration.h"

namespace HidHide
{
    namespace Channel
    {
        inline constexpr auto OwnerName = L"Global\\HidHide.AppProfiles.Coordinator.v1";
        inline constexpr auto PipeName = L"\\\\.\\pipe\\HidHide.AppProfiles.Configuration.v1";
        using Handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&::CloseHandle)>;
        inline Handle Own(HANDLE h)
        {
            if (!h || h == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open configuration coordination handle");
            return Handle(h, &::CloseHandle);
        }
        inline std::wstring TokenSid(HANDLE token)
        {
            DWORD size{};
            ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
            std::vector<BYTE> bytes(size);
            if (!::GetTokenInformation(token, TokenUser, bytes.data(), size, &size)) throw std::runtime_error("Cannot authenticate configuration client");
            LPWSTR text{};
            if (!::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &text)) throw std::runtime_error("Cannot read user identity");
            std::wstring sid(text);
            ::LocalFree(text);
            return sid;
        }
        inline std::wstring CurrentSid()
        {
            HANDLE token{};
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) throw std::runtime_error("Cannot read current user");
            auto owner = Own(token);
            return TokenSid(owner.get());
        }
        struct Security
        {
            PSECURITY_DESCRIPTOR descriptor{};
            SECURITY_ATTRIBUTES attributes{ sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE };
            explicit Security(std::wstring const& sddl)
            {
                if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
                    throw std::runtime_error("Cannot create configuration permissions");
                attributes.lpSecurityDescriptor = descriptor;
            }
            ~Security() { ::LocalFree(descriptor); }
            Security(Security const&) = delete;
            Security& operator=(Security const&) = delete;
        };
        // Held for manager lifetime, or for one direct CLI transaction. The mutex
        // is machine-wide; another session must contact the owner or fail closed.
        class Lease
        {
            Handle handle{ nullptr, &::CloseHandle };
            bool acquired{};
        public:
            explicit Lease(PCWSTR name = OwnerName)
            {
                Security security(L"D:P(A;;0x00100001;;;AU)(A;;GA;;;SY)(A;;GA;;;BA)");
                handle = Own(::CreateMutexExW(&security.attributes, name, 0, SYNCHRONIZE | MUTEX_MODIFY_STATE));
                auto const result = ::WaitForSingleObject(handle.get(), 0);
                acquired = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
                if (result == WAIT_FAILED) throw std::runtime_error("Cannot acquire configuration ownership");
            }
            ~Lease() { if (acquired) ::ReleaseMutex(handle.get()); }
            bool Acquired() const { return acquired; }
            Lease(Lease const&) = delete;
            Lease& operator=(Lease const&) = delete;
        };

        inline void Transfer(HANDLE pipe, bool writing, void* buffer, DWORD size, DWORD& transferred)
        {
            auto event = Own(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
            OVERLAPPED operation{};
            operation.hEvent = event.get();
            auto ok = writing ? ::WriteFile(pipe, buffer, size, &transferred, &operation)
                              : ::ReadFile(pipe, buffer, size, &transferred, &operation);
            if (!ok)
            {
                if (::GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("Configuration connection failed; refresh before retrying");
                if (::WaitForSingleObject(event.get(), 5000) != WAIT_OBJECT_0)
                {
                    ::CancelIoEx(pipe, &operation);
                    ::GetOverlappedResult(pipe, &operation, &transferred, TRUE);
                    throw std::runtime_error("Configuration request timed out; outcome may be unknown. Refresh before retrying");
                }
                if (!::GetOverlappedResult(pipe, &operation, &transferred, FALSE))
                    throw std::runtime_error("Configuration response failed; refresh before retrying");
            }
        }
        inline std::vector<std::uint8_t> Exchange(std::vector<std::uint8_t> request, PCWSTR pipeName = PipeName)
        {
            if (request.size() > Protocol::MaxBytes) throw std::runtime_error("Configuration request too large");
            if (!::WaitNamedPipeW(pipeName, 1500)) throw std::runtime_error("Configuration coordinator is busy, starting, or owned by another user. Try again shortly");
            auto pipe = Own(::CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
            ULONG pid{};
            if (!::GetNamedPipeServerProcessId(pipe.get(), &pid)) throw std::runtime_error("Cannot authenticate configuration server");
            auto process = Own(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
            HANDLE token{};
            if (!::OpenProcessToken(process.get(), TOKEN_QUERY, &token)) throw std::runtime_error("Cannot authenticate configuration server user");
            auto serverToken = Own(token);
            if (TokenSid(serverToken.get()) != CurrentSid()) throw std::runtime_error("Configuration is owned by another user");
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!::SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr)) throw std::runtime_error("Cannot configure local connection");
            DWORD transferred{};
            Transfer(pipe.get(), true, request.data(), static_cast<DWORD>(request.size()), transferred);
            if (transferred != request.size()) throw std::runtime_error("Incomplete configuration request");
            std::vector<std::uint8_t> response(Protocol::MaxBytes);
            Transfer(pipe.get(), false, response.data(), static_cast<DWORD>(response.size()), transferred);
            response.resize(transferred);
            return response;
        }

        // Polled on the coordinator thread. PIPE_NOWAIT prevents a stalled client
        // from blocking the GUI or extending ownership of a driver transaction.
        class Server
        {
            Handle pipe{ nullptr, &::CloseHandle };
            std::wstring sid{ CurrentSid() };
            bool connected{};
            ULONGLONG deadline{};
            std::vector<std::uint8_t> response;
            bool sent{};
            void Disconnect()
            {
                ::DisconnectNamedPipe(pipe.get());
                connected = false; response.clear(); sent = false;
            }
        public:
            explicit Server(PCWSTR pipeName = PipeName)
            {
                Security security(L"D:P(A;;GA;;;" + sid + L")");
                pipe = Own(::CreateNamedPipeW(pipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
                    1, static_cast<DWORD>(Protocol::MaxBytes), static_cast<DWORD>(Protocol::MaxBytes), 0, &security.attributes));
            }
            void Pump(std::function<std::vector<std::uint8_t>(std::vector<std::uint8_t> const&)> const& handler)
            {
                if (!connected)
                {
                    auto ok = ::ConnectNamedPipe(pipe.get(), nullptr);
                    auto error = ok ? ERROR_SUCCESS : ::GetLastError();
                    if (!ok && error != ERROR_PIPE_CONNECTED) return;
                    connected = true; deadline = ::GetTickCount64() + 4000;
                }
                if (::GetTickCount64() > deadline) { Disconnect(); return; }
                DWORD available{};
                if (!::PeekNamedPipe(pipe.get(), nullptr, 0, nullptr, &available, nullptr)) { Disconnect(); return; }
                if (sent) return; // Client closes after reading; never execute a second command on this connection.
                if (response.empty())
                {
                    if (!available) return;
                    if (available > Protocol::MaxBytes) { Disconnect(); return; }
                    std::vector<std::uint8_t> request(Protocol::MaxBytes);
                    DWORD read{};
                    if (!::ReadFile(pipe.get(), request.data(), static_cast<DWORD>(request.size()), &read, nullptr)) { Disconnect(); return; }
                    request.resize(read);
                    if (!::ImpersonateNamedPipeClient(pipe.get())) { Disconnect(); return; }
                    bool authenticated{};
                    try
                    {
                        HANDLE token{};
                        if (::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &token))
                        {
                            auto clientToken = Own(token);
                            authenticated = TokenSid(clientToken.get()) == sid;
                        }
                    }
                    catch (...) { if (!::RevertToSelf()) std::terminate(); throw; }
                    if (!::RevertToSelf()) std::terminate();
                    if (!authenticated) { Disconnect(); return; }
                    response = handler(request);
                    if (response.empty() || response.size() > Protocol::MaxBytes) { Disconnect(); return; }
                }
                DWORD written{};
                if (!::WriteFile(pipe.get(), response.data(), static_cast<DWORD>(response.size()), &written, nullptr))
                {
                    if (::GetLastError() != ERROR_NO_DATA) Disconnect();
                    return;
                }
                sent = written == response.size();
            }
        };
    }
}
