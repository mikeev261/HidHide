// SPDX-License-Identifier: MIT
#pragma once
#include "ConfigurationSession.h"
#include "Logging.h"

inline void ReportConfigurationError(std::runtime_error const& error, bool permanentEdit = false)
{
    LOGEXC_AND_CONTINUE;
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
        AfxMessageBox(L"Configuration changed in another application. The latest values will be reloaded; please repeat your edit.", MB_ICONWARNING);
    else
    {
        CString message(L"Could not access or save configuration. Please retry the action.\n\n");
        if (permanentEdit) message += L"Permanent settings may already be saved. The view will reload; app profiles retry applying saved settings. The Enable switch shows the actual hiding state.\n\n";
        message += CString(error.what());
        AfxMessageBox(message, MB_ICONERROR);
    }
}
