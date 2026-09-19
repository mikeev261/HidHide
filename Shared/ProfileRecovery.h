// SPDX-License-Identifier: MIT
#pragma once

#include "Configuration.h"

#include <string>
#include <utility>
#include <vector>

namespace HidHide::ProfileRecovery
{
    inline constexpr wchar_t RuntimeKey[]{ L"Software\\Nefarius Software Solutions e.U.\\HidHide\\AppProfileRuntime" };
    inline constexpr wchar_t LegacyValue[]{ L"TransactionV1" };
    inline constexpr wchar_t OwnedValue[]{ L"TransactionV2" };

    template <typename Exists>
    bool RelevantRecoveryExists(Exists&& exists)
    {
        return exists(OwnedValue) || exists(LegacyValue);
    }

    template <typename Erase>
    void ClearOwnedRecovery(Erase&& erase)
    {
        erase(OwnedValue);
    }

    template <typename Erase>
    void ClearKnownRecoveryAfterExplicitAdoption(Erase&& erase)
    {
        erase(OwnedValue);
        erase(LegacyValue);
    }

    constexpr std::uint32_t SchemaV1{ 1 };
    constexpr std::uint32_t SchemaV2{ 2 };

    struct Record
    {
        DriverConfiguration baseline;
        DriverConfiguration before;
        DriverConfiguration after;
    };

    inline std::vector<std::uint8_t> SerializeV2(std::wstring const& ownerSid, Record const& record)
    {
        Protocol::Writer writer;
        writer.Number(SchemaV2); writer.String(ownerSid);
        writer.DriverState(record.baseline); writer.DriverState(record.before); writer.DriverState(record.after);
        return std::move(writer.data);
    }

    inline Record Parse(std::vector<std::uint8_t> const& bytes, std::wstring const& expectedOwnerSid)
    {
        Protocol::Reader reader(bytes);
        auto const schema = reader.Number();
        if ((schema != SchemaV1 && schema != SchemaV2) || reader.String() != expectedOwnerSid)
            throw std::runtime_error("Recovery record owner or version mismatch");

        Record record;
        if (schema == SchemaV1)
        {
            // V1 embedded an obsolete profile catalog. Parse it for backward
            // compatibility, then intentionally retain only its driver state.
            record.baseline = DriverState(reader.State());
            record.before = DriverState(reader.State());
            record.after = DriverState(reader.State());
        }
        else
        {
            record.baseline = reader.DriverState(); record.before = reader.DriverState(); record.after = reader.DriverState();
        }
        reader.End();
        return record;
    }

    // A recovery journal is authoritative only for global driver settings. Keep
    // the catalog read from the live per-user store even when accepting a V1
    // record that embedded an older catalog.
    inline bool MatchesRecordedDriverState(DriverConfiguration const& current,
        DriverConfiguration const& baseline, DriverConfiguration const& before, DriverConfiguration const& after)
    {
        return current == baseline || current == before || current == after;
    }

    inline Configuration RestoreDriverState(Configuration live, DriverConfiguration const& baseline)
    {
        SetDriverState(live, baseline);
        return live;
    }
}
