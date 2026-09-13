// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace HidHide
{
    // UI-thread scheduler: coalesce topology bursts and retain failed work until
    // a later timer tick. A steady stream of notifications cannot defer it forever.
    class PendingDeviceRefresh
    {
    public:
        void Request(std::uint64_t now) noexcept
        {
            if (!m_Pending) m_NextAttempt = now + 500;
            m_Pending = true;
        }
        void Complete() noexcept { m_Pending = false; }

        template<class Refresh>
        void RunIfDue(std::uint64_t now, Refresh refresh)
        {
            if (!m_Pending || now < m_NextAttempt) return;
            m_NextAttempt = now + 500;
            refresh(); // Failure propagates, retaining pending work and the delay.
            Complete();
        }
    private:
        bool m_Pending{};
        std::uint64_t m_NextAttempt{};
    };
}
