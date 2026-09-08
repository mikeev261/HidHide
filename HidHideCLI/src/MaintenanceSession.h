// SPDX-License-Identifier: MIT
#pragma once
#include "FilterDriverProxy.h"
#include <iostream>

namespace HidHide
{
    // An internal setup interface, invoked before elevation by the initiating
    // user's controller. No executable/path/SID arguments or commands are accepted.
    inline void RunMaintenanceSession()
    {
        auto const input = ::GetStdHandle(STD_INPUT_HANDLE);
        if (::GetFileType(input) != FILE_TYPE_PIPE || ::GetFileType(::GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_PIPE)
            throw std::runtime_error("Maintenance sessions require redirected setup-controller input and output pipes");
        auto session = FilterDriverProxy::BeginMaintenance();
        Protocol::Writer writer;
        writer.Number(2); // Setup-only READY protocol, independent of configuration IPC.
        writer.String(Channel::CurrentSid());
        writer.Number(session->Confirmed().driverPresent);
        writer.Number(session->Confirmed().driverPresent || session->Confirmed().storedBaselineAvailable);
        writer.State(session->Confirmed().configuration);
        constexpr char digits[] = "0123456789ABCDEF";
        std::string line = "READY ";
        line.reserve(7 + writer.data.size() * 2);
        for (auto byte : writer.data) { line.push_back(digits[byte >> 4]); line.push_back(digits[byte & 15]); }
        Maintenance::WriteControllerLine(::GetStdHandle(STD_OUTPUT_HANDLE), line);

        // Anonymous pipe reads are synchronous. Peek before reading a single byte
        // so an unresponsive controller cannot make the input wait unbounded.
        auto const deadline = ::GetTickCount64() + 30ULL * 60 * 1000;
        std::string command;
        while (::GetTickCount64() < deadline)
        {
            DWORD available{};
            if (!::PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
                throw std::runtime_error("Maintenance controller disconnected before release");
            if (!available) { ::Sleep(10); continue; }
            char character{}; DWORD read{};
            if (!::ReadFile(input, &character, 1, &read, nullptr) || read != 1)
                throw std::runtime_error("Maintenance controller input failed");
            if (character == '\n')
            {
                if (!command.empty() && command.back() == '\r') command.pop_back();
                if (Maintenance::ParseControl(command) == Maintenance::Control::Release) return;
                session->Handoff();
                Maintenance::WriteControllerLine(::GetStdHandle(STD_OUTPUT_HANDLE), "HANDED_OFF");
                command.clear();
            }
            else
            {
                if (command.size() >= 16) throw std::runtime_error("Oversized maintenance control");
                command.push_back(character);
            }
        }
        throw std::runtime_error("Maintenance controller session expired; re-detect before retrying setup");
    }
}
