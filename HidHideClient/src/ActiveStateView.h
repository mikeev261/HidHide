// SPDX-License-Identifier: MIT
#pragma once

namespace HidHide
{
    // Read applied state, never pending profile intent. A failed read leaves both
    // the control and its optimistic-concurrency expectation untouched.
    template<class Driver, class Render>
    void SynchronizeActiveState(Driver& driver, bool& displayed, Render render)
    {
        bool const active = driver.GetActive();
        if (active == displayed) return;
        render(active);
        displayed = active;
    }
}
