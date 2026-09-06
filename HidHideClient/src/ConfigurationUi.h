// SPDX-License-Identifier: MIT
#pragma once
#include "ConfigurationSession.h"
#include "Logging.h"

inline void ReportConfigurationError(std::runtime_error const& error)
{
    LOGEXC_AND_CONTINUE;
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
        AfxMessageBox(L"Configuration changed in another application. The latest values will be reloaded; please repeat your edit.", MB_ICONWARNING);
    else
    {
        CString message(L"Could not access or save configuration. Please retry the action.\n\n");
        message += CString(error.what());
        AfxMessageBox(message, MB_ICONERROR);
    }
}
