// SPDX-License-Identifier: MIT
#pragma once
#include "ConfigurationSession.h"
namespace HidHide
{
    // Live sessions refresh each getter; snapshot sessions stage changes until commit.
    // Driver access is scoped to each operation so the signed exclusive driver does
    // not prevent a running GUI from observing CLI changes.
    class FilterDriverProxy : public ConfigurationSession
    {
    public:
        explicit FilterDriverProxy(bool writeThrough);
        FilterDriverProxy(FilterDriverProxy const&) = delete;
        FilterDriverProxy& operator=(FilterDriverProxy const&) = delete;
        static DWORD DeviceStatus();
    };
}
