// SPDX-License-Identifier: MIT
#pragma once
#include "Configuration.h"
#include <functional>

namespace HidHide::Protocol
{
    inline std::vector<std::uint8_t> Dispatch(
        std::vector<std::uint8_t> const& request,
        std::function<Configuration()> const& read,
        std::function<void(Configuration const&, Configuration const&, bool)> const& commit,
        std::function<Configuration()> const& prepareMaintenance)
    {
        Writer response; response.Number(Version);
        try
        {
            Reader reader(request);
            if (reader.Number() != Version) throw std::runtime_error("Unsupported coordinator protocol");
            auto const command = static_cast<Command>(reader.Number());
            Configuration confirmed;
            if (command == Command::Read) { reader.End(); confirmed = read(); }
            else if (command == Command::Commit)
            {
                auto expected = reader.State(); auto desired = reader.State(); auto disable = reader.Boolean(); reader.End();
                if (!commit) throw std::runtime_error("Coordinator is not ready");
                commit(expected, desired, disable); confirmed = std::move(desired);
            }
            else if (command == Command::PrepareMaintenance)
            {
                reader.End(); // No commands, paths, user identities or mutation data accepted.
                if (!prepareMaintenance) throw std::runtime_error("Coordinator does not support maintenance preparation");
                confirmed = prepareMaintenance();
            }
            else throw std::runtime_error("Unknown configuration command");
            response.Number(0); response.State(confirmed);
        }
        catch (std::exception const& error)
        {
            response = {}; response.Number(Version); response.Number(1);
            std::string message(error.what()); response.String(std::wstring(message.begin(), message.end()));
        }
        return response.data;
    }

    inline Configuration ReadReply(std::vector<std::uint8_t> const& response)
    {
        Reader reader(response);
        if (reader.Number() != Version) throw std::runtime_error("Unsupported coordinator protocol");
        if (reader.Boolean())
        {
            auto error = reader.String(); reader.End();
            std::string message; for (auto c : error) message.push_back(c < 128 ? static_cast<char>(c) : '?');
            throw std::runtime_error(message);
        }
        auto state = reader.State(); reader.End(); return state;
    }
}
