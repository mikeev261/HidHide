// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// HidHideClient.cpp
#include "stdafx.h"
#include "HidHideClient.h"
#include "HidHideClientDlg.h"
#include "FilterDriverProxy.h"
#include "Utils.h"
#include "Logging.h"
#include "ConfigurationChannel.h"
#include "ManagerActivation.h"
#include "ProfileRepository.h"
#include "ProfileRecovery.h"
#include "EditorService.h"
#include <future>

CHidHideClientApp theApp;

BEGIN_MESSAGE_MAP(CHidHideClientApp, CWinApp)
    ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

namespace
{
    class AcceptanceEnforcement : public HidHide::Profiles::IEnforcement
    {
    public:
        enum class ObservationMode { Normal, KnownConflict, Unknown };
        HidHide::Profiles::EnforcementResult Observe() override
        {
            if (m_ObservationMode == ObservationMode::KnownConflict) return { false, true, m_State, L"Injected known conflict", true };
            if (m_ObservationMode == ObservationMode::Unknown) return { false, false, {}, L"Injected unknown read failure" };
            return { true, true, m_State, {} };
        }
        HidHide::Profiles::EnforcementResult Reconcile(HidHide::Profiles::DesiredEnforcement const& desired) override
        {
            if (m_FailReconcile) return { false, false, {}, L"Injected deterministic enforcement failure" };
            m_State = desired; ++m_Writes; return { true, true, m_State, {} };
        }
        HidHide::Profiles::EnforcementResult RestoreBaseline() override { m_State = m_Baseline; return { true, true, m_State, {} }; }
        HidHide::Profiles::EnforcementResult AdoptCurrentAsBaseline() override { ++m_Adoptions; m_RecoveryEvidence.clear(); m_Baseline = m_State; return { true, true, m_State, {} }; }
        std::uint64_t Writes() const noexcept { return m_Writes; }
        std::uint64_t Adoptions() const noexcept { return m_Adoptions; }
        std::vector<std::uint8_t> RecoveryEvidence() const { return m_RecoveryEvidence; }
        void FailReconcile(bool value) noexcept { m_FailReconcile = value; }
        void SetObservationMode(ObservationMode value) noexcept { m_ObservationMode = value; }
        void SetAllowedApplications(std::set<std::filesystem::path> value) { m_State.allowedApplications = std::move(value); }
    private:
        HidHide::Profiles::DesiredEnforcement m_State, m_Baseline;
        std::uint64_t m_Writes{};
        std::uint64_t m_Adoptions{};
        bool m_FailReconcile{};
        ObservationMode m_ObservationMode{ ObservationMode::Normal };
        std::vector<std::uint8_t> m_RecoveryEvidence{ 0x52, 0x45, 0x43, 0x56 };
    };

    class AcceptanceDevices final : public IProfilesDeviceSource
    {
    public:
        bool presentation{};
        std::vector<ProfilesDeviceItem> Enumerate() override
        {
            if(presentation)return {
                {L"HID\\CONCEPT_WHEEL",L"Thrustmaster T300RS — Wheel and buttons",true},
                {L"HID\\CONCEPT_PEDALS",L"Heusinkveld Sprint — Pedals",true},
                {L"HID\\CONCEPT_VJOY",L"vJoy Device — Virtual controller",true}};
            return { { L"HID\\VID_1234&PID_0001\\CONNECTED", L"Connected wheel — individual HID interface", m_Connected.load() },
                { L"HID\\VID_1234&PID_0002\\REMEMBERED", L"Disconnected pedals — individual HID interface", false } };
        }
        void Refresh() override { ++m_Refreshes; if (m_ToggleOnRefresh) m_Connected = !m_Connected.load(); }
        std::uint64_t RefreshCount() const override { return m_Refreshes.load(); }
        void ToggleOnRefresh(bool value) noexcept { m_ToggleOnRefresh = value; }
    private:
        std::atomic_bool m_Connected{ true };
        std::atomic_uint64_t m_Refreshes{};
        bool m_ToggleOnRefresh{};
    };

    bool IsIsolatedRestartRoot(std::filesystem::path const& root)
    {
        wchar_t temporary[MAX_PATH]{}; if (!::GetTempPathW(MAX_PATH, temporary)) return false;
        auto absolute = std::filesystem::absolute(root).lexically_normal().native(); auto prefix = std::filesystem::path(temporary).lexically_normal().native();
        if (absolute.size() <= prefix.size() || _wcsnicmp(absolute.c_str(), prefix.c_str(), prefix.size()) != 0) return false;
        return absolute.find(L"HidHide-Profiles-Restart-Test-") != std::wstring::npos;
    }

    ProfilesDriverTransport IsolatedProfileTestTransport(ProfilesDriverTransport transport)
    {
        auto suffix = std::to_wstring(::GetCurrentProcessId());
        transport.admissionName = L"Local\\HidHide.ProfilesTest.Admission." + suffix;
        transport.barrierName = L"Local\\HidHide.ProfilesTest.Maintenance." + suffix;
        return transport;
    }

    bool ExerciseProductionEnforcementAdapter()
    {
        ProfilesDriverTransport defaults;
        if (defaults.admissionName != HidHide::Maintenance::AdmissionName
            || defaults.barrierName != HidHide::Maintenance::BarrierName)
            throw std::runtime_error("production enforcement transport has invalid maintenance names");

        HidHide::Configuration state; std::uint64_t commits{};
        CProfilesEnforcementAdapter adapter(IsolatedProfileTestTransport(ProfilesDriverTransport{
            [&] { return state; },
            [&](auto const& expected, auto const& desired)
            {
                if (HidHide::DriverState(state) != expected) throw std::runtime_error("test transport CAS mismatch");
                HidHide::SetDriverState(state, desired); ++commits;
            }, false }));
        auto observed = adapter.Observe(); if (!observed.success || !observed.observedKnown || commits) return false;
        HidHide::Profiles::DesiredEnforcement desired; desired.hidingEnabled = true; desired.hiddenDevices.emplace(L"HID\\EXACT\\ONE");
        auto applied = adapter.Reconcile(desired); if (!applied.success || !applied.observedKnown || !(applied.observed == desired) || commits != 1) return false;
        auto deduplicated = adapter.Reconcile(desired); if (!deduplicated.success || commits != 1) return false;
        auto restored = adapter.RestoreBaseline(); if (!restored.success || !restored.observedKnown || commits != 2 || state.active || !state.blacklist.empty()) return false;
        state.blacklist.emplace(L"HID\\EXTERNAL\\CHANGE"); auto conflict = adapter.Observe();
        if (conflict.success || !conflict.conflict || !conflict.observedKnown) return false;

        HidHide::Configuration mismatchState; CProfilesEnforcementAdapter mismatch(IsolatedProfileTestTransport(ProfilesDriverTransport{
            [&] { return mismatchState; }, [](auto const&, auto const&) {}, false }));
        auto mismatchResult = mismatch.Reconcile(desired); if (mismatchResult.success || !mismatchResult.observedKnown) return false;
        HidHide::Configuration partialState; bool failPartialOnce{ true }; CProfilesEnforcementAdapter partial(IsolatedProfileTestTransport(ProfilesDriverTransport{
            [&] { return partialState; }, [&](auto const&, auto const& requested) { HidHide::SetDriverState(partialState, requested); if (std::exchange(failPartialOnce, false)) throw std::runtime_error("partial IOCTL"); }, false }));
        auto partialResult = partial.Reconcile(desired);
        if (partialResult.success || !partialResult.observedKnown || !partialResult.conflict || !(partialResult.observed == desired)) return false;
        auto partialRestored = partial.RestoreBaseline();
        if (!partialRestored.success || !partialRestored.observedKnown || partialState.active || !partialState.blacklist.empty()) return false;

        for (bool structurallyValidButMismatched : { false, true })
        {
            HidHide::Configuration blockedState; std::vector<std::uint8_t> stored;
            if (structurallyValidButMismatched)
            {
                HidHide::DriverConfiguration unrelated; unrelated.active = true; unrelated.blacklist.emplace(L"HID\\UNRELATED");
                stored = HidHide::ProfileRecovery::SerializeV2(HidHide::Channel::CurrentSid(), { unrelated, unrelated, unrelated });
            }
            else stored = { 0xff, 0x00, 0x7f };
            auto original = stored; unsigned clears{}, blockedWrites{}; bool failClearOnce{ true };
            CProfilesEnforcementAdapter blocked(IsolatedProfileTestTransport(ProfilesDriverTransport{
                [&] { return blockedState; }, [&](auto const&, auto const&) { ++blockedWrites; }, true,
                [&]() -> std::optional<std::vector<std::uint8_t>> { return stored; }, [&](auto const& bytes) { stored = bytes; },
                [&] { if (std::exchange(failClearOnce, false)) throw std::runtime_error("injected clear failure"); ++clears; stored.clear(); } }));
            auto blockedObservation = blocked.Observe(); if (blockedObservation.success || !blockedObservation.conflict || stored != original || clears) return false;
            auto blockedRestore = blocked.RestoreBaseline(); if (blockedRestore.success || !blockedRestore.conflict || stored != original || clears) return false;
            try { (void)blocked.PrepareMaintenance(); return false; } catch (std::runtime_error const&) {}
            if (stored != original || clears) return false;
            auto failedAdopt = blocked.AdoptCurrentAsBaseline(); if (failedAdopt.success || !failedAdopt.conflict || stored != original || clears || blockedWrites) return false;
            if (blocked.Reconcile(desired).success || blockedWrites) return false;
            auto adopted = blocked.AdoptCurrentAsBaseline(); if (!adopted.success || clears != 1 || !stored.empty() || blockedWrites) return false;
        }
        {
            HidHide::Configuration saveFailureState; std::vector<std::uint8_t> stored; unsigned saveFailureCommits{}, clears{};
            CProfilesEnforcementAdapter saveFailure(IsolatedProfileTestTransport(ProfilesDriverTransport{
                [&] { return saveFailureState; }, [&](auto const&, auto const&) { ++saveFailureCommits; }, true,
                [&]() -> std::optional<std::vector<std::uint8_t>> { return stored.empty() ? std::nullopt : std::optional(stored); },
                [&](auto const&) { stored = { 0xde, 0xad, 0x00, 0xff }; throw std::runtime_error("injected partial recovery-store write"); },
                [&] { ++clears; stored.clear(); } }));
            auto failed = saveFailure.Reconcile(desired);
            if (failed.success || !failed.conflict || saveFailureCommits || stored.empty()) throw std::runtime_error("save-failure recovery latch did not engage");
            if (saveFailure.Observe().success || saveFailure.Reconcile(desired).success || saveFailure.RestoreBaseline().success || saveFailureCommits) throw std::runtime_error("save-failure recovery latch allowed a later operation");
            try { (void)saveFailure.PrepareMaintenance(); return false; } catch (std::runtime_error const&) {}
            if (stored.empty() || clears || saveFailureCommits) throw std::runtime_error("save-failure evidence was not preserved");
            auto adopted = saveFailure.AdoptCurrentAsBaseline(); if (!adopted.success || !stored.empty() || clears != 1 || saveFailureCommits) throw std::runtime_error("save-failure explicit adoption did not clear safely");
        }
        {
            HidHide::Configuration opaqueState; opaqueState.whitelist.emplace(L"\\Device\\DefinitelyMissingVolume\\opaque.exe");
            CProfilesEnforcementAdapter opaque(IsolatedProfileTestTransport(ProfilesDriverTransport{ [&] { return opaqueState; }, [](auto const&, auto const&) {}, false }));
            auto observation = opaque.Observe(); if (observation.success || observation.observedKnown) throw std::runtime_error("opaque Allowed-app observation was called verified");
            auto reconcile = opaque.Reconcile({}); if (reconcile.success || reconcile.observedKnown) throw std::runtime_error("opaque Allowed-app reconciliation was called verified");
        }
        {
            HidHide::Configuration legacyState; bool legacyPending{ true }; unsigned writes{}, clears{};
            std::map<std::wstring, std::vector<std::uint8_t>> evidence{
                { HidHide::ProfileRecovery::LegacyValue, { 1, 2, 3 } }, { L"ForeignValue", { 9 } } };
            CProfilesEnforcementAdapter legacyBlocked(IsolatedProfileTestTransport(ProfilesDriverTransport{
                [&] { return legacyState; }, [&](auto const&, auto const&) { ++writes; }, true,
                []() -> std::optional<std::vector<std::uint8_t>> { return std::nullopt; }, [](auto const&) {}, [] {},
                [&] { return legacyPending; }, [&]
                {
                    ++clears; HidHide::ProfileRecovery::ClearKnownRecoveryAfterExplicitAdoption(
                        [&](wchar_t const* name) { evidence.erase(name); }); legacyPending = false;
                } }));
            if (legacyBlocked.Observe().success || legacyBlocked.Reconcile(desired).success || legacyBlocked.RestoreBaseline().success || writes || clears)
                throw std::runtime_error("legacy recovery evidence did not block the profiles runtime");
            auto adopted = legacyBlocked.AdoptCurrentAsBaseline();
            if (!adopted.success || writes || clears != 1 || evidence.count(HidHide::ProfileRecovery::LegacyValue) || !evidence.count(L"ForeignValue"))
                throw std::runtime_error("explicit adoption did not clear only known recovery evidence");
        }
        {
            HidHide::Configuration opaqueBlockedState; opaqueBlockedState.whitelist.emplace(L"\\Device\\DefinitelyMissingVolume\\opaque.exe");
            bool legacyPending{ true }; unsigned clears{}, writes{}; auto evidence = std::vector<std::uint8_t>{ 4, 5, 6 };
            CProfilesEnforcementAdapter opaqueBlocked(IsolatedProfileTestTransport(ProfilesDriverTransport{
                [&] { return opaqueBlockedState; }, [&](auto const&, auto const&) { ++writes; }, true,
                []() -> std::optional<std::vector<std::uint8_t>> { return std::nullopt; }, [](auto const&) {}, [] {},
                [&] { return legacyPending; }, [&] { ++clears; evidence.clear(); legacyPending = false; } }));
            auto adopted = opaqueBlocked.AdoptCurrentAsBaseline();
            if (adopted.success || !adopted.conflict || clears || writes || evidence != std::vector<std::uint8_t>({ 4, 5, 6 }))
                throw std::runtime_error("opaque blocked adoption destroyed recovery evidence");
            if (opaqueBlocked.Observe().success || opaqueBlocked.Reconcile(desired).success || opaqueBlocked.RestoreBaseline().success || clears || writes)
                throw std::runtime_error("failed opaque adoption unblocked enforcement");
        }
        return true;
    }

    // Release-only acceptance seam. It is deliberately limited to an isolated
    // temporary root and named semantic events, and never opens the live driver.
    bool RunEditorAcceptanceHost()
    {
        if (__argc != 3 || _wcsicmp(__wargv[1], L"--profiles-editor-host") != 0) return false;
        std::filesystem::path root;
        try
        {
            HidHide::Editor::RequireOrdinaryUser();
            root = std::filesystem::absolute(__wargv[2]).lexically_normal();
            auto pipeName = HidHide::Editor::FixturePipeName(root);
            if (std::filesystem::exists(root)) throw std::runtime_error("Editor fixture requires a new empty test root");
            std::filesystem::create_directory(root);
            using namespace HidHide::Profiles; using namespace HidHide::Profiles::Json;
            class HostDevices final : public IProfilesDeviceSource
            {
                std::vector<ProfilesDeviceItem> Enumerate() override
                {
                    return {{L"HID\\FIXTURE_WHEEL",L"Fixture steering wheel",true,{L"HID\\FIXTURE_WHEEL"},L"wheel",{{true,1,4},{false,0,0}}},
                        {L"HID\\FIXTURE_PEDALS",L"Fixture pedals",true,{L"HID\\FIXTURE_PEDALS"}},
                        {L"HID\\FIXTURE_VIRTUAL",L"Fixture virtual controller",true,{L"HID\\FIXTURE_VIRTUAL"}}};
                }
            } devices;
            class HostEnforcement final : public AcceptanceEnforcement
            {
                EnforcementResult Reconcile(DesiredEnforcement const& desired) override
                {
                    auto current = Observe();
                    if (current.success && current.observedKnown && current.observed == desired) return current;
                    return AcceptanceEnforcement::Reconcile(desired);
                }
            } enforcement;
            ProfileApplicationService application(root/L"Profiles"); auto initial = application.OpenOrCreate();
            auto settings = initial.snapshot.settings; settings.startWithWindows = false;
            application.ApplySettings(settings,application.SettingsVersion());
            auto gamePath = NormalizeExecutable(root/L"FixtureGame.exe");
            Profile game; game.id = NewStableId(); game.name=L"Fixture Game"; game.kind=Kind::Application; game.executable=gamePath;
            game.rules.push_back({L"HID\\FIXTURE_PEDALS",L"Fixture pedals",Visibility::Hidden});
            application.Apply(game,std::nullopt);
            std::atomic_bool running{};
            CProfilesCoordinator coordinator(enforcement,application.Root(),false,[&]
            {
                return running.load() ? std::vector<ProcessObservation>{{60001,1,L"FixtureGame.exe",gamePath,true}} : std::vector<ProcessObservation>{};
            },{},[] { return false; });
            coordinator.AcceptanceScanNow();
            HidHide::Editor::Service service(application,coordinator,devices);
            HidHide::Channel::Server server(pipeName.c_str(), true);
            {
                std::ofstream ready(root/L"editor-host-ready.json",std::ios::binary|std::ios::trunc);
                ready << "{\"pid\":" << ::GetCurrentProcessId() << ",\"pipe\":" << ToUtf8(Escape(pipeName))
                    << ",\"applicationId\":" << ToUtf8(Escape(game.id)) << ",\"globalId\":" << ToUtf8(Escape(settings.selectedGlobalId)) << "}";
            }
            bool stopping{}; auto deadline = ::GetTickCount64()+30*60*1000; std::uint64_t requests{};
            while (::GetTickCount64()<deadline)
            {
                coordinator.Tick();
                (void)::WaitForSingleObject(server.WakeHandle(), std::min<DWORD>(server.WaitTimeoutMs(), 250));
                server.Pump([&](auto const& bytes)
                {
                    ++requests;
                    try
                    {
                        auto json=Parser(FromUtf8(std::string(bytes.begin(),bytes.end()))).Parse(); auto const& request=AsObject(json);
                        auto command=AsString(Required(request,L"command"));
                        if(command==L"fixture-stop")
                        {
                            stopping=true; std::string result="{\"ok\":true}"; return std::vector<std::uint8_t>(result.begin(),result.end());
                        }
                        if(command==L"fixture-state")
                        {
                            auto result=std::string("{\"ok\":true,\"editorOpen\":")+(service.EditorOpen()?"true":"false")
                                +",\"enginePid\":"+std::to_string(::GetCurrentProcessId())+",\"enforcementWrites\":"+std::to_string(enforcement.Writes())+"}";
                            return std::vector<std::uint8_t>(result.begin(),result.end());
                        }
                        if(command==L"fixture-process")
                        {
                            running=AsBool(Required(request,L"running")); coordinator.AcceptanceScanNow();
                            std::string snapshot="{\"command\":\"snapshot\"}"; return service.Handle({snapshot.begin(),snapshot.end()});
                        }
                    }
                    catch (...) {} // Production handler returns bounded diagnostics.
                    return service.Handle(bytes);
                });
                if(stopping && !server.Connected()) break;
            }
            coordinator.ExitSafely(); auto restored = enforcement.Observe();
            std::ofstream stopped(root/L"editor-host-stopped.json",std::ios::binary|std::ios::trunc);
            stopped << "{\"clean\":" << (stopping?"true":"false") << ",\"requests\":" << requests
                << ",\"enforcementWrites\":" << enforcement.Writes() << ",\"baselineRestored\":"
                << (restored.observedKnown && restored.observed==DesiredEnforcement{}?"true":"false") << "}";
        }
        catch(std::exception const& error)
        {
            // No diagnostics are written outside an already-validated test root.
            try { if(!root.empty()) { (void)HidHide::Editor::FixturePipeName(root); if(std::filesystem::is_directory(root))
                { std::ofstream failed(root/L"editor-host-error.txt",std::ios::binary|std::ios::trunc); failed<<error.what(); } } } catch(...) {}
        }
        return true;
    }

    bool RunEditorSelfTest()
    {
        if (__argc != 3 || _wcsicmp(__wargv[1], L"--profiles-editor-self-test") != 0) return false;
        auto root = std::filesystem::absolute(__wargv[2]).lexically_normal();
        if (!IsIsolatedRestartRoot(root) || std::filesystem::exists(root)) return true;
        std::filesystem::create_directories(root);
        std::ofstream report(root / L"editor-self-test.txt", std::ios::binary | std::ios::trunc);
        try
        {
            using namespace HidHide::Profiles; using namespace HidHide::Profiles::Json;
            AcceptanceEnforcement enforcement; AcceptanceDevices devices;
            ProfileApplicationService application(root / L"Profiles"); application.OpenOrCreate();
            CProfilesCoordinator coordinator(enforcement, application.Root(), false, [] { return std::vector<ProcessObservation>{}; }, {}, [] { return false; });
            HidHide::Editor::Service service(application, coordinator, devices);
            auto request = [&](std::string const& json, bool expectedOk = true)
            {
                auto bytes = service.Handle({json.begin(),json.end()}); auto value = Parser(FromUtf8(std::string(bytes.begin(),bytes.end()))).Parse();
                if (AsBool(Required(AsObject(value),L"ok")) != expectedOk) throw std::runtime_error("Editor response did not match expected outcome: " + std::string(bytes.begin(),bytes.end()));
                return value;
            };
            auto versionJson = [](SavedVersion const& version)
            { return "{\"revision\":" + std::to_string(version.revision) + ",\"hash\":" + ToUtf8(Escape(version.sha256)) + "}"; };
            request("{\"command\":\"snapshot\"}");
            auto id = coordinator.Snapshot().settings.selectedGlobalId;
            auto profile = coordinator.Snapshot().profiles.at(id); auto before = application.Version(id); auto settings = coordinator.Snapshot().settings; auto settingsVersion = application.SettingsVersion();
            auto settingsHash = Sha256(ReadBytes(application.Root()/L"settings.json"));
            auto newResult = request("{\"command\":\"new\",\"kind\":\"global\",\"name\":\"Detached Global\"}");
            auto newId = AsString(Required(AsObject(Required(AsObject(newResult),L"profile")),L"id"));
            if (std::filesystem::exists(application.Root()/(newId+L".json")) || settingsHash != Sha256(ReadBytes(application.Root()/L"settings.json"))) throw std::runtime_error("New profile persisted before Apply");
            profile.name = L"Editor CAS checked"; profile.rules.push_back({L"HID\\VID_1234&PID_0001\\CONNECTED",L"Fixture wheel",Visibility::Hidden});
            auto apply = "{\"command\":\"apply\",\"profile\":"+SerializeProfile(profile)+",\"expected\":"+versionJson(before)+",\"settings\":"+SerializeSettings(settings)+",\"expectedSettings\":"+versionJson(settingsVersion)+"}";
            auto saved = request(apply);
            if (!AsBool(Required(AsObject(saved),L"saved"))) throw std::runtime_error("Apply did not save");
            auto after = application.Version(id); request(apply,false);
            if (application.Version(id).sha256 != after.sha256) throw std::runtime_error("Stale Apply overwrote the saved profile");
            report << "PASS: detached new, CAS apply and stale rejection\n";
            auto exportPath = root/L"export.json"; request("{\"command\":\"export\",\"id\":"+ToUtf8(Escape(id))+",\"path\":"+ToUtf8(Escape(exportPath.native()))+"}");
            auto imported = request("{\"command\":\"import\",\"path\":"+ToUtf8(Escape(exportPath.native()))+"}");
            auto const& importedProfile = AsObject(Required(AsObject(imported),L"profile"));
            if (AsBool(Required(importedProfile,L"enabled")) || AsString(Required(importedProfile,L"id")) == id || application.Version(id).sha256 != after.sha256) throw std::runtime_error("Import was not a disabled detached copy");
            report << "PASS: saved export and detached disabled import\n";
            enforcement.FailReconcile(true); settings = coordinator.Snapshot().settings; settings.paused = true;
            auto paused = request("{\"command\":\"settings\",\"settings\":"+SerializeSettings(settings)+",\"expectedSettings\":"+versionJson(application.SettingsVersion())+"}");
            if (!AsBool(Required(AsObject(paused),L"saved")) || AsBool(Required(AsObject(paused),L"applied"))) throw std::runtime_error("Save and failed enforcement were conflated");
            auto failedHash = application.SettingsVersion().sha256; enforcement.FailReconcile(false); request("{\"command\":\"retry\"}");
            if (application.SettingsVersion().sha256 != failedHash) throw std::runtime_error("Retry rewrote settings");
            report << "PASS: saved-but-not-applied and zero-write retry\n";
            request("{\"command\":\"unrecognized\"}",false); request("{broken",false);
            auto oversized = service.Handle(std::vector<std::uint8_t>(HidHide::Protocol::MaxBytes+1,'x'));
            if (AsBool(Required(AsObject(Parser(FromUtf8(std::string(oversized.begin(),oversized.end()))).Parse()),L"ok"))) throw std::runtime_error("Oversized request was accepted");
            report << "PASS: malformed, unknown and oversized requests rejected\n";
            enforcement.SetObservationMode(AcceptanceEnforcement::ObservationMode::Unknown);
            auto unknown = request("{\"command\":\"snapshot\"}");
            auto const& snapshot = AsObject(Required(AsObject(unknown),L"snapshot"));
            if (AsBool(Required(snapshot,L"verified"))) throw std::runtime_error("Unknown driver state was presented as verified");
            for (auto const& device : AsArray(Required(snapshot,L"devices"))) if (AsString(Required(AsObject(device),L"current")) != L"Unknown") throw std::runtime_error("Unknown device state was presented as visible");
            report << "PASS: unknown observed state remains unknown\n";
            enforcement.SetAllowedApplications({root/L"FixtureFeeder.exe"});
            enforcement.SetObservationMode(AcceptanceEnforcement::ObservationMode::KnownConflict); coordinator.ObserveEnforcement();
            auto adoptHash = application.SettingsVersion().sha256;
            auto adoption = request("{\"command\":\"adopt\",\"expectedSettings\":"+versionJson(application.SettingsVersion())+"}");
            if (!AsBool(Required(AsObject(adoption),L"needsApply")) || !coordinator.AdoptionAwaitingSave()
                || application.SettingsVersion().sha256 != adoptHash || enforcement.Adoptions() != 1) throw std::runtime_error("Driver adoption did not stage Allowed apps without JSON writes");
            request("{\"command\":\"abandon-adoption\"}");
            if (coordinator.AdoptionAwaitingSave() || application.SettingsVersion().sha256 != adoptHash) throw std::runtime_error("Adoption discard modified the saved catalog");
            enforcement.SetObservationMode(AcceptanceEnforcement::ObservationMode::Normal);
            report << "PASS: explicit driver adoption and discard preserve JSON\n";
            auto backupPath=root/L"Backup";
            request("{\"command\":\"backup\",\"path\":"+ToUtf8(Escape(backupPath.native()))+"}");
            request("{\"command\":\"restore\",\"path\":"+ToUtf8(Escape(backupPath.native()))+"}");
            if (coordinator.Snapshot().profiles.at(id).name != L"Editor CAS checked") throw std::runtime_error("Backup restore changed the saved profile");
            report << "PASS: backup and restore through the production service\n";
            auto pipeName = L"\\\\.\\pipe\\HidHide.Profiles.Editor.Test." + NewStableId();
            HidHide::Channel::Server pipe(pipeName.c_str(), true);
            auto pending = std::async(std::launch::async, [&]
            {
                std::string query="{\"command\":\"snapshot\"}";
                return HidHide::Channel::Exchange({query.begin(),query.end()},pipeName.c_str());
            });
            auto deadline=::GetTickCount64()+10000;
            while(pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && ::GetTickCount64()<deadline)
            { pipe.Pump([&](auto const& bytes){return service.Handle(bytes);}); ::Sleep(10); }
            auto pipeResponse=pending.get();
            if (!AsBool(Required(AsObject(Parser(FromUtf8(std::string(pipeResponse.begin(),pipeResponse.end()))).Parse()),L"ok"))) throw std::runtime_error("Authenticated editor pipe failed");
            report << "PASS: bounded same-user named-pipe exchange\n";
            {
                std::ofstream malformed(application.Root()/L"settings.json",std::ios::binary|std::ios::trunc); malformed<<"{invalid";
            }
            auto broken = request("{\"command\":\"snapshot\"}");
            auto const& brokenSnapshot=AsObject(Required(AsObject(broken),L"snapshot"));
            if (AsArray(Required(brokenSnapshot,L"repositoryIssues")).empty()
                || !std::holds_alternative<std::nullptr_t>(Required(brokenSnapshot,L"settingsVersion").data)
                || ReadBytes(application.Root()/L"settings.json") != "{invalid") throw std::runtime_error("Repository diagnostics were hidden or rewrote invalid evidence");
            report << "PASS: invalid repository is inspectable, blocked and preserved\nALL PASSED\n";
        }
        catch (std::exception const& error) { report << "FAILED: " << error.what() << '\n'; }
        return true;
    }

    bool RunProfileRestartWorker()
    {
        if (__argc != 7 || _wcsicmp(__wargv[1], L"--profile-restart-test") != 0) return false;
        std::filesystem::path root(__wargv[3]); if (!IsIsolatedRestartRoot(root)) return true;
        HANDLE ready = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, __wargv[4]); HANDLE command = ::OpenEventW(SYNCHRONIZE, FALSE, __wargv[5]); HANDLE completed = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, __wargv[6]);
        if (!ready || !command || !completed) { if (ready) ::CloseHandle(ready); if (command) ::CloseHandle(command); if (completed) ::CloseHandle(completed); return true; }
        try
        {
            if (_wcsicmp(__wargv[2], L"hold") == 0)
            {
                {
                    HidHide::Profiles::WriterLease lease(root); ::SetEvent(ready);
                    if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("lease release command was not received");
                }
                ::SetEvent(completed); ::Sleep(INFINITE);
            }

            AcceptanceEnforcement enforcement; AcceptanceDevices devices; auto phase = std::wstring(__wargv[2]);
            bool presentationPhase=phase.rfind(L"presentation-",0)==0;
            if(presentationPhase)
            {
                devices.presentation=true;
                HidHide::Profiles::ProfileRepository setup(root);auto loaded=setup.OpenOrCreate();auto global=loaded.snapshot.profiles.at(loaded.snapshot.settings.selectedGlobalId);
                global.name=L"Everyday gaming";setup.Apply(global,setup.Version(global.id));
                HidHide::Profiles::Profile second;second.id=HidHide::Profiles::NewStableId();second.name=L"Sim rig only";second.kind=HidHide::Profiles::Kind::Global;setup.Apply(second,std::nullopt);
                int priority=100;
                for(auto name:{L"F1 25",L"Le Mans Ultimate",L"Assetto Corsa"})
                {
                    HidHide::Profiles::Profile profile;profile.id=HidHide::Profiles::NewStableId();profile.name=name;profile.kind=HidHide::Profiles::Kind::Application;profile.priority=priority--;profile.enabled=true;
                    profile.executable=HidHide::Profiles::NormalizeExecutable(root/(profile.name==L"F1 25"?L"F1_25.exe":profile.name+L".exe"));
                    {std::ofstream file(profile.executable);file<<"presentation fixture only";}
                    profile.rules={{L"HID\\CONCEPT_WHEEL",L"Thrustmaster T300RS",HidHide::Profiles::Visibility::Hidden},
                        {L"HID\\CONCEPT_PEDALS",L"Heusinkveld Sprint",HidHide::Profiles::Visibility::Visible},
                        {L"HID\\CONCEPT_XBOX",L"Xbox Wireless Controller",HidHide::Profiles::Visibility::Hidden}};
                    setup.Apply(profile,std::nullopt);
                }
            }
            auto performancePhase = phase.rfind(L"perf-", 0) == 0; std::atomic_bool syntheticRunning{ phase != L"perf-churn" };
            std::atomic_uint64_t processScans{}, matchingObservations{}, syntheticTransitions{}, deviceNotifications{};
            std::vector<std::filesystem::path> syntheticExecutables;
            if (performancePhase)
            {
                HidHide::Profiles::ProfileRepository setup(root); auto loaded = setup.OpenOrCreate();
                auto profileCount = phase == L"perf-empty" ? 0u : 20u;
                for (unsigned index{}; index < profileCount; ++index)
                {
                    auto executablePath = HidHide::Profiles::NormalizeExecutable(root / (L"SyntheticGame" + std::to_wstring(index) + L".exe")); syntheticExecutables.push_back(executablePath);
                    { std::ofstream executable(executablePath, std::ios::binary); executable << "isolated performance fixture"; }
                    HidHide::Profiles::Profile profile; profile.id = HidHide::Profiles::NewStableId(); profile.name = L"Synthetic workload";
                    profile.kind = HidHide::Profiles::Kind::Application; profile.enabled = true; profile.priority = static_cast<std::int32_t>(100 - index); profile.executable = executablePath;
                    profile.rules.push_back({ L"HID\\VID_1234&PID_0001\\CONNECTED", L"Synthetic wheel", HidHide::Profiles::Visibility::Hidden });
                    setup.Apply(std::move(profile), std::nullopt);
                }
            }
            if (_wcsicmp(__wargv[2], L"conflict-fresh") == 0)
                enforcement.SetObservationMode(AcceptanceEnforcement::ObservationMode::KnownConflict);
            if (_wcsicmp(__wargv[2], L"unknown-fresh") == 0)
                enforcement.SetObservationMode(AcceptanceEnforcement::ObservationMode::Unknown);
            std::atomic_bool failProcessScans{}; std::atomic_int liveProcessMode{};
            if (phase == L"known-observation-allowed-change")
            {
                auto feeder = HidHide::Profiles::NormalizeExecutable(root / L"ObservedFeeder.exe");
                { std::ofstream executable(feeder, std::ios::binary); executable << "representable observed feeder"; }
            }
            if (phase == L"live-status")
            {
                HidHide::Profiles::ProfileRepository setup(root); auto loaded = setup.OpenOrCreate();
                for (auto const& item : std::vector<std::pair<std::wstring, std::int32_t>>{ { L"Status high", 100 }, { L"Status low", 10 } })
                {
                    auto executable = HidHide::Profiles::NormalizeExecutable(root / (item.first + L".exe"));
                    { std::ofstream file(executable, std::ios::binary); file << "status fixture"; }
                    HidHide::Profiles::Profile profile; profile.id = HidHide::Profiles::NewStableId(); profile.name = item.first;
                    profile.kind = HidHide::Profiles::Kind::Application; profile.enabled = true; profile.priority = item.second; profile.executable = executable;
                    setup.Apply(std::move(profile), std::nullopt);
                }
            }
            if (phase == L"adoption-changed-lifecycle")
            {
                HidHide::Profiles::ProfileRepository setup(root); (void)setup.OpenOrCreate();
                auto executable = HidHide::Profiles::NormalizeExecutable(root / L"NavigationTarget.exe");
                { std::ofstream file(executable, std::ios::binary); file << "navigation fixture"; }
                HidHide::Profiles::Profile profile; profile.id = HidHide::Profiles::NewStableId(); profile.name = L"Navigation target";
                profile.kind = HidHide::Profiles::Kind::Application; profile.enabled = false; profile.executable = executable;
                setup.Apply(std::move(profile), std::nullopt);
            }
            CProfilesCoordinator::ProcessSource processSource = [&]
            {
                if(presentationPhase){auto executable=HidHide::Profiles::NormalizeExecutable(root/L"F1_25.exe");return std::vector<HidHide::Profiles::ProcessObservation>{{4242,1,executable.filename().native(),executable,true}};}
                if (failProcessScans) throw std::runtime_error("injected process discovery failure");
                ++processScans;
                if (phase == L"repository-domain")
                {
                    auto executable = HidHide::Profiles::NormalizeExecutable(root / L"F1_25.exe");
                    if (std::filesystem::exists(executable)) return std::vector<HidHide::Profiles::ProcessObservation>{ { 4244, 4, executable.filename().native(), executable, true } };
                }
                if (phase == L"live-status")
                {
                    auto observation = [&](DWORD pid, std::wstring const& name)
                    {
                        auto executable = HidHide::Profiles::NormalizeExecutable(root / (name + L".exe"));
                        return HidHide::Profiles::ProcessObservation{ pid, pid, executable.filename().native(), executable, true };
                    };
                    std::vector<HidHide::Profiles::ProcessObservation> result;
                    auto mode = liveProcessMode.load();
                    if (mode == 1 || mode == 2) result.push_back(observation(5101, L"Status high"));
                    if (mode == 2 || mode == 3) result.push_back(observation(5102, L"Status low"));
                    return result;
                }
                if ((phase != L"perf-one-match" && phase != L"perf-competing" && phase != L"perf-churn") || !syntheticRunning.load() || syntheticExecutables.empty())
                    return std::vector<HidHide::Profiles::ProcessObservation>{};
                std::vector<HidHide::Profiles::ProcessObservation> result{ { 4242, 2, syntheticExecutables[0].filename().native(), syntheticExecutables[0], true } };
                if (phase == L"perf-competing" && syntheticExecutables.size() > 1)
                    result.push_back({ 4243, 3, syntheticExecutables[1].filename().native(), syntheticExecutables[1], true });
                matchingObservations.fetch_add(result.size());
                return result;
            };
            ProfilesAcceptanceContext acceptance{ root, enforcement, devices, (performancePhase||presentationPhase) ? processSource : CProfilesCoordinator::ProcessSource([] { return std::vector<HidHide::Profiles::ProcessObservation>{}; }) };
            if (phase == L"repository-domain") { acceptance.processes = processSource; acceptance.failProcessScans = &failProcessScans; }
            if (phase == L"live-status") { acceptance.processes = processSource; acceptance.liveProcessMode = &liveProcessMode; }
            acceptance.driverConflictMode = [&](bool conflict) { enforcement.SetObservationMode(conflict ? AcceptanceEnforcement::ObservationMode::KnownConflict : AcceptanceEnforcement::ObservationMode::Normal); };
            acceptance.adoptionCount = [&] { return enforcement.Adoptions(); };
            acceptance.recoveryEvidence = [&] { return enforcement.RecoveryEvidence(); };
            acceptance.failReconcile = [&](bool fail) { enforcement.FailReconcile(fail); };
            acceptance.setAllowedApplications = [&](std::set<std::filesystem::path> allowed) { enforcement.SetAllowedApplications(std::move(allowed)); };
            acceptance.observationMode = [&](int mode)
            {
                enforcement.SetObservationMode(mode == 1 ? AcceptanceEnforcement::ObservationMode::KnownConflict
                    : mode == 2 ? AcceptanceEnforcement::ObservationMode::Unknown : AcceptanceEnforcement::ObservationMode::Normal);
            };
            acceptance.devicePipeline = phase == L"perf-reconnect" || phase == L"device-coalescing" || phase == L"device-conflict-propagation";
            devices.ToggleOnRefresh(phase == L"perf-reconnect");
            if (_wcsicmp(__wargv[2], L"startup-failure") == 0) acceptance.startupIntegration = [](bool) -> std::wstring { throw std::runtime_error("injected Run-key failure"); };
            if (_wcsicmp(__wargv[2], L"adapter") == 0) acceptance.maintenanceSource = []() -> bool { throw std::runtime_error("injected maintenance inspection failure"); };
            CHidHideClientDlg dialog(nullptr, acceptance); AfxGetApp()->m_pMainWnd = &dialog;
            if (!dialog.Create(IDD_DIALOG_APPLICATION)) throw std::runtime_error("real profile dialog could not start");
            auto accessibilityPhase = _wcsicmp(__wargv[2], L"accessibility") == 0;
            auto performanceShow = phase == L"perf-editor" || phase == L"perf-tray" ? SW_SHOW : SW_SHOWMINIMIZED;
            dialog.ShowWindow(performancePhase ? performanceShow : (accessibilityPhase||presentationPhase) ? SW_SHOW : SW_HIDE); dialog.UpdateWindow();
            bool trayPath{}; if (phase == L"perf-tray") { trayPath = dialog.AcceptanceEnterTrayMode(); if (!trayPath) throw std::runtime_error("real tray icon path was unavailable"); }
            if (_wcsicmp(__wargv[2], L"apply") == 0)
            {
                // Two semantic barriers let the parent prove that opening and
                // editing a draft changed no catalog bytes before Apply.
                ::SetEvent(ready);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("draft-stage command was not received");
                ::ResetEvent(command); ::ResetEvent(ready);
                if (!dialog.AcceptanceStageProfile()) throw std::runtime_error("real profile editor could not stage the acceptance draft");
                ::SetEvent(completed); ::SetEvent(ready);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("apply command was not received");
                if (!dialog.AcceptanceApply()) throw std::runtime_error("real Apply command path failed");
                ::SetEvent(ready); ::SetEvent(completed);
            }
            else if (_wcsicmp(__wargv[2], L"reload") == 0)
            {
                if (!dialog.AcceptanceLoaded()) throw std::runtime_error("real editor and coordinator did not reload the saved profile");
                if (!dialog.AcceptanceSearchSelectsOtherProfile()) throw std::runtime_error("real search control could not filter and select another profile");
                ::SetEvent(ready); ::SetEvent(completed);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("clean shutdown command was not received");
                dialog.DestroyWindow();
                return true;
            }
            else if (_wcsicmp(__wargv[2], L"invalid") == 0)
            {
                if (!dialog.AcceptanceRepositoryBlocked() || !dialog.AcceptanceBlockedCommandsSafe() || enforcement.Writes() != 0)
                    throw std::runtime_error("invalid repository was not presented safely");
                ::SetEvent(ready); ::SetEvent(completed);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("invalid-repository shutdown command was not received");
                dialog.DestroyWindow();
                return true;
            }
            else if (_wcsicmp(__wargv[2], L"unknown-fresh") == 0 || _wcsicmp(__wargv[2], L"conflict-fresh") == 0)
            {
                if (!dialog.AcceptanceRepositoryBlocked() || !dialog.AcceptanceBlockedCommandsSafe() || enforcement.Writes() != 0)
                    throw std::runtime_error("unknown initial observation did not open the blocked recovery view");
                for (auto const& entry : std::filesystem::directory_iterator(root))
                    if (entry.is_regular_file() && entry.path().extension() == L".json") throw std::runtime_error("unknown initial observation wrote repository JSON");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("unknown-fresh shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"events") == 0)
            {
                enforcement.FailReconcile(true);
                if (!dialog.AcceptanceExerciseZeroWriteEvents()) throw std::runtime_error("automatic and draft-only events wrote the repository");
                ::SetEvent(ready); ::SetEvent(completed);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("zero-write shutdown command was not received");
                dialog.DestroyWindow();
                return true;
            }
            else if (_wcsicmp(__wargv[2], L"adapter") == 0)
            {
                if (!ExerciseProductionEnforcementAdapter()) throw std::runtime_error("production enforcement adapter conformance failed");
                if (!dialog.AcceptanceMaintenanceFailure()) throw std::runtime_error("maintenance inspection failure did not remain fail-closed in the running UI");
                ::SetEvent(ready); ::SetEvent(completed);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("adapter-test shutdown command was not received");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"restore") == 0)
            {
                auto backup = std::filesystem::path(root.native() + L"-backup");
                if (!dialog.AcceptanceRestoreBackup(backup) || enforcement.Writes() != 1)
                    throw std::runtime_error("validated backup did not immediately recover and apply");
                ::SetEvent(ready); ::SetEvent(completed);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("restore-test shutdown command was not received");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"prompts") == 0)
            {
                if (!dialog.AcceptanceDirtyPromptSemantics()) throw std::runtime_error("Apply/Discard/Cancel prompt behavior failed");
                ::SetEvent(ready); ::SetEvent(completed);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("prompt-test shutdown command was not received");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"known-observation") == 0 || _wcsicmp(__wargv[2], L"known-observation-allowed-change") == 0 || _wcsicmp(__wargv[2], L"unknown-observation") == 0
                || _wcsicmp(__wargv[2], L"verified-observation") == 0)
            {
                auto expectedKnown = _wcsicmp(__wargv[2], L"known-observation") == 0 || _wcsicmp(__wargv[2], L"known-observation-allowed-change") == 0;
                auto changedAllowedApplications = _wcsicmp(__wargv[2], L"known-observation-allowed-change") == 0;
                auto expectedVerified = _wcsicmp(__wargv[2], L"verified-observation") == 0;
                if (!expectedVerified)
                {
                    if (!dialog.AcceptanceObservationKnown(true, true)) throw std::runtime_error("verified observation precondition failed");
                    if (changedAllowedApplications) enforcement.SetAllowedApplications({ HidHide::Profiles::NormalizeExecutable(root / L"ObservedFeeder.exe") });
                    enforcement.SetObservationMode(expectedKnown ? AcceptanceEnforcement::ObservationMode::KnownConflict : AcceptanceEnforcement::ObservationMode::Unknown);
                }
                if (!dialog.AcceptanceObservationKnown(expectedKnown || expectedVerified, expectedVerified)) throw std::runtime_error("observed-state tri-state presentation failed");
                if (expectedKnown && !dialog.AcceptanceMainDriverConflictAction(true, changedAllowedApplications)) throw std::runtime_error("main-window driver conflict adoption action failed");
                ::SetEvent(ready); ::SetEvent(completed);
                if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("observation-test shutdown command was not received");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"startup-failure") == 0)
            {
                if (!dialog.AcceptanceStartupFailure()) throw std::runtime_error("startup integration blocked saved policy publication");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("startup-failure shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"enforcement-failure") == 0)
            {
                enforcement.FailReconcile(true);
                if (!dialog.AcceptanceSavedEnforcementFailure()) throw std::runtime_error("saved-but-enforcement-failed UI semantics were incorrect");
                enforcement.FailReconcile(false);
                if (!dialog.AcceptanceRetryActivation()) throw std::runtime_error("retry activation did not apply without rewriting JSON");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("enforcement-failure shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"hidden-refresh") == 0)
            {
                if (!dialog.AcceptanceHiddenPresentationRefresh()) throw std::runtime_error("hidden/minimized profile presentation did not refresh on restore");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("hidden-refresh shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"device-coalescing") == 0)
            {
                if (!dialog.AcceptanceDeviceBurstCoalesced()) throw std::runtime_error("profile device notifications were not bounded and coalesced");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("device-coalescing shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"repository-domain") == 0)
            {
                if (!dialog.AcceptanceVerificationInvalidation()) throw std::runtime_error("saved/external policy snapshot was presented as verified before fresh observation");
                if (!dialog.AcceptanceRepositoryDiagnosticsDoNotAdoptDriver()) throw std::runtime_error("repository diagnostics crossed the driver conflict boundary");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("repository-domain shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"live-status") == 0)
            {
                if (!dialog.AcceptanceLiveProcessStatus()) throw std::runtime_error("live process status rows did not track coordinator notifications");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("live-status shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"adoption-race") == 0)
            {
                if (!dialog.AcceptanceAdoptionRepositoryRace()) throw std::runtime_error("repository change during confirmation crossed the adoption boundary");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("adoption-race shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"adoption-retry") == 0)
            {
                if (!dialog.AcceptanceAdoptionRetryAfterFailure()) throw std::runtime_error("failed equal-Allowed adoption did not expose a safe retry");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("adoption-retry shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"global-status") == 0)
            {
                if (!dialog.AcceptanceGlobalStatusSemantics()) throw std::runtime_error("Global fallback/manual/paused UI and tray semantics were not distinct");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("global-status shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"adoption-changed-lifecycle") == 0)
            {
                if (!dialog.AcceptanceChangedAdoptionDraftLifecycle()) throw std::runtime_error("changed Allowed-app adoption draft lifecycle failed");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("adoption lifecycle shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if (_wcsicmp(__wargv[2], L"device-conflict-propagation") == 0 || _wcsicmp(__wargv[2], L"selection-unknown-propagation") == 0)
            {
                if (!dialog.AcceptanceObservationPropagation(phase == L"selection-unknown-propagation")) throw std::runtime_error("observed-state change did not propagate through the production presentation path");
                ::SetEvent(ready); ::SetEvent(completed); if (::WaitForSingleObject(command, 10000) != WAIT_OBJECT_0) throw std::runtime_error("observation propagation shutdown command missing");
                dialog.DestroyWindow(); return true;
            }
            else if(presentationPhase)
            {
                ProfilesView::OverrideThemeForAcceptance(phase.find(L"system")!=std::wstring::npos?-1:phase.find(L"dark")!=std::wstring::npos?1:0);
                dialog.SendMessageW(ProfilesView::ThemeChangedMessage);
                UINT dpi=phase.find(L"150")!=std::wstring::npos?144:phase.find(L"125")!=std::wstring::npos?120:phase.find(L"200")!=std::wstring::npos?192:96;
                bool minimum=phase.find(L"minimum")!=std::wstring::npos;
                if(!dialog.AcceptancePresentation(phase.find(L"clean")==std::wstring::npos,dpi,minimum?1040:1280,minimum?680:800))throw std::runtime_error("presentation validation failed");
                dialog.ShowWindow(SW_SHOW);dialog.RedrawWindow(nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
                ::SetEvent(ready);::SetEvent(completed);MSG message{};
                for(;;){auto wait=::MsgWaitForMultipleObjects(1,&command,FALSE,30000,QS_ALLINPUT);if(wait==WAIT_OBJECT_0)break;if(wait==WAIT_TIMEOUT)continue;
                    while(::PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(!AfxGetApp()->PreTranslateMessage(&message)){::TranslateMessage(&message);::DispatchMessageW(&message);}}}
                dialog.DestroyWindow();return true;
            }
            else if (accessibilityPhase)
            {
                ::SetEvent(ready); ::SetEvent(completed); MSG message{};
                for (;;)
                {
                    auto wait = ::MsgWaitForMultipleObjects(1, &command, FALSE, 10000, QS_ALLINPUT);
                    if (wait == WAIT_OBJECT_0) break; if (wait != WAIT_OBJECT_0 + 1) throw std::runtime_error("accessibility-test shutdown command missing");
                    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
                }
                dialog.DestroyWindow(); return true;
            }
            else if (performancePhase)
            {
                std::atomic_bool generatorStop{}; std::thread generator;
                if (phase == L"perf-churn") generator = std::thread([&] { while (!generatorStop) { syntheticRunning = !syntheticRunning.load(); ++syntheticTransitions; ::Sleep(137); } });
                if (phase == L"perf-reconnect") generator = std::thread([&] { while (!generatorStop) { dialog.AcceptanceNotifyDeviceChange(); ++deviceNotifications; ::Sleep(100); } });
                ::SetEvent(ready); ::SetEvent(completed); MSG message{};
                for (;;)
                {
                    auto wait = ::MsgWaitForMultipleObjects(1, &command, FALSE, 10000, QS_ALLINPUT);
                    if (wait == WAIT_OBJECT_0) break; if (wait == WAIT_TIMEOUT) continue;
                    if (wait != WAIT_OBJECT_0 + 1) { generatorStop = true; if (generator.joinable()) generator.join(); throw std::runtime_error("performance-test shutdown command missing"); }
                    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
                }
                generatorStop = true; if (generator.joinable()) generator.join();
                std::ofstream counters(root / L".performance-counters.txt", std::ios::binary | std::ios::trunc);
                counters << "{\"processScans\":" << processScans.load() << ",\"matchingObservations\":" << matchingObservations.load()
                    << ",\"syntheticTransitions\":" << syntheticTransitions.load() << ",\"deviceNotifications\":" << deviceNotifications.load()
                    << ",\"deviceRefreshes\":" << devices.RefreshCount() << ",\"trayPath\":" << (trayPath ? "true" : "false")
                    << ",\"trayIconAccepted\":" << (dialog.AcceptanceTrayIconAccepted() ? "true" : "false") << "}";
                counters.close(); dialog.DestroyWindow(); return true;
            }
            else throw std::runtime_error("unknown restart phase");
            MSG message{}; while (::GetMessageW(&message, nullptr, 0, 0) > 0) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        }
        catch (std::exception const& error)
        {
            std::ofstream diagnostic(root / L".acceptance-error.txt", std::ios::binary | std::ios::trunc); diagnostic << error.what(); diagnostic.close(); ::SetEvent(completed);
        }
        catch (...) { ::SetEvent(completed); }
        ::CloseHandle(ready); ::CloseHandle(command); ::CloseHandle(completed); return true;
    }

    HHOOK s_hHook;

    // Alter message box labels and detach from the window activiation notification
    LRESULT CALLBACK LocalizedMessageBoxCBTProc(_In_ INT code, _In_ WPARAM wParam, _In_ LPARAM lParam)
    {
        // Act during window activitation
        if (HCBT_ACTIVATE == code)
        {
            TRACE_ALWAYS(L"");
            auto dlg{ reinterpret_cast<HWND>(wParam) };

            // Alter labels
            if (nullptr != ::GetDlgItem(dlg, IDOK))     ::SetDlgItemTextW(dlg, IDOK,     HidHide::StringTable(IDS_STATIC_MESSAGEBOX_OK).c_str());
            if (nullptr != ::GetDlgItem(dlg, IDCANCEL)) ::SetDlgItemTextW(dlg, IDCANCEL, HidHide::StringTable(IDS_STATIC_MESSAGEBOX_CANCEL).c_str());
            if (nullptr != ::GetDlgItem(dlg, IDRETRY))  ::SetDlgItemTextW(dlg, IDRETRY,  HidHide::StringTable(IDS_STATIC_MESSAGEBOX_RETRY).c_str());
            if (nullptr != ::GetDlgItem(dlg, IDIGNORE)) ::SetDlgItemTextW(dlg, IDIGNORE, HidHide::StringTable(IDS_STATIC_MESSAGEBOX_IGNORE).c_str());
            if (nullptr != ::GetDlgItem(dlg, IDABORT))  ::SetDlgItemTextW(dlg, IDABORT,  HidHide::StringTable(IDS_STATIC_MESSAGEBOX_ABORT).c_str());
            if (nullptr != ::GetDlgItem(dlg, IDYES))    ::SetDlgItemTextW(dlg, IDYES,    HidHide::StringTable(IDS_STATIC_MESSAGEBOX_YES).c_str());
            if (nullptr != ::GetDlgItem(dlg, IDNO))     ::SetDlgItemTextW(dlg, IDNO,     HidHide::StringTable(IDS_STATIC_MESSAGEBOX_NO).c_str());

            // Fire-once so detach again
            ::UnhookWindowsHookEx(s_hHook);
        }

        // Allow other hooks to act too
        ::CallNextHookEx(s_hHook, code, wParam, lParam);
        return (0);
    }

    // Show message box with localized buttons
    INT WINAPI LocalizedMessageBox(_In_ UINT resourceId, _In_ UINT type)
    {
        TRACE_ALWAYS(L"");

        // Attach hook (fire-once)
        s_hHook = ::SetWindowsHookExW(WH_CBT, &LocalizedMessageBoxCBTProc, 0, ::GetCurrentThreadId());
        return (::MessageBoxExW(::AfxGetApp()->GetMainWnd()->m_hWnd, HidHide::StringTable(resourceId).c_str(), HidHide::StringTable(IDS_DIALOG_APPLICATION).c_str(), type, LANG_USER_DEFAULT));
    }
}

// Register the ETW logging and tracing providers
NTSTATUS WINAPI LogRegisterProviders() noexcept
{
    try
    {
        EventRegisterNefarius_HidHide_Client();
        EventRegisterNefarius_Drivers_HidHideClient();

        // The define for BldProductVersion is passed from the project file to the source code via a define
        ::LogEvent(ETW(Started), L"%s", _L(BldProductVersion));
        return (STATUS_SUCCESS);
    }
    catch (...)
    {
        DBG_AND_RETURN_NTSTATUS("LogRegisterProviders", STATUS_UNHANDLED_EXCEPTION);
    }
}

// Unregister the ETW logging and tracing providers
NTSTATUS WINAPI LogUnregisterProviders() noexcept
{
    try
    {
        ::LogEvent(ETW(Stopped), L"");
        EventUnregisterNefarius_Drivers_HidHideClient();
        EventUnregisterNefarius_HidHide_Client();
        return (STATUS_SUCCESS);
    }
    catch (...)
    {
        DBG_AND_RETURN_NTSTATUS("LogUnregisterProviders", STATUS_UNHANDLED_EXCEPTION);
    }
}

CHidHideClientApp::CHidHideClientApp() noexcept
{
    ::LogRegisterProviders();
}

CHidHideClientApp::~CHidHideClientApp()
{
    ::LogUnregisterProviders();
}

BOOL CHidHideClientApp::InitInstance()
{
    TRACE_ALWAYS(L"");

    // The editor bridge serves one or more requests with no driver or profile
    // ownership. Run before OLE, the dialog, startup integration, or any lease.
    if (HidHide::Editor::RunRequestBridge()) return FALSE;

    // Initialize OLE library
    AfxOleInit();

    // Initialize the common controls .dll first
    INITCOMMONCONTROLSEX initCommonControlsEx;
    initCommonControlsEx.dwSize = sizeof(initCommonControlsEx);
    initCommonControlsEx.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&initCommonControlsEx);

    // Initialize the application instance
    CWinApp::InitInstance();

    // Initialize COM services
    AfxEnableControlContainer();

    // The isolated acceptance process uses the same real dialog, page,
    // coordinator, repository view and Apply message map as production.  Only
    // process/device/enforcement adapters and the repository root are replaced.
    if (RunEditorAcceptanceHost()) return FALSE;
    if (RunEditorSelfTest()) return FALSE;
    if (RunProfileRestartWorker()) return FALSE;

    // Create the shell manager, in case the dialog contains any shell tree view or shell list view controls
    std::unique_ptr<CShellManager> const shellManager{ std::make_unique<CShellManager>() };

    // Activate "Windows Native" visual manager for enabling themes in MFC controls
    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

    // We can't do anything when the control device isn't present so allow for a retry on failure
    bool startHidden{};
    for (int index = 1; index < __argc; index++)
        startHidden = startHidden || (0 == _wcsicmp(__wargv[index], L"--background"));

    // Keep ownership until the modal dialog and its profile manager are destroyed.
    // A namespace-static lease would be destroyed before the earlier global app
    // object, making any release from the app destructor a use-after-destruction.
    std::unique_ptr<HidHide::Channel::Lease> owner;
    try
    {
        HidHide::Editor::RequireOrdinaryUser();
        HidHide::Maintenance::Admission admission;
        owner = std::make_unique<HidHide::Channel::Lease>();
    }
    catch (std::exception const& error)
    {
        if (!startHidden) ::MessageBoxA(nullptr, error.what(), "HidHide ownership", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    if (!owner->Acquired())
    {
        if (!startHidden && !HidHide::ManagerActivation::ShowExisting(WM_HIDHIDE_SHOW_MANAGER))
            ::MessageBoxW(nullptr, L"The configuration coordinator is already running but could not be opened in this Windows session. It may be starting or owned by another session. Try again shortly or open it from its tray icon.", L"HidHide Profiles", MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    CHidHideClientDlg dlg(nullptr, startHidden);
    m_pMainWnd = &dlg;

    // We use exception handling so catch it at top-level and bail out
    try
    {
        // Keep retrying when the device is unavailable
        while (true)
        {
            if (auto const deviceStatus{ HidHide::FilterDriverProxy::DeviceStatus() }; (ERROR_SUCCESS == deviceStatus))
            {
                // Let Electron load its local renderer while the native owner
                // verifies recovery, repository and driver state. It will show
                // Connecting until this dialog creates the authenticated pipe.
                if (!startHidden)
                {
                    try { HidHide::Editor::Launch(true); dlg.MarkEditorPrelaunched(); }
                    catch (...) {} // The regular dialog launch reports a missing editor.
                }
                if (-1 == dlg.DoModal()) THROW_WIN32_LAST_ERROR;
                break;
            }
            else
            {
                TRACE_ALWAYS(L"");
                // Electron owns visible startup diagnostics. A background engine
                // must never leave a hidden retry dialog waiting for interaction.
                if (startHidden) break;
                if (IDRETRY != LocalizedMessageBox(((ERROR_ACCESS_DENIED == deviceStatus) ? IDS_STATIC_MESSAGEBOX_IN_USE : IDS_STATIC_MESSAGEBOX_PRESENT), (MB_RETRYCANCEL | MB_ICONEXCLAMATION)))
                {
                    break;
                }
            }
        }
    }
    catch (std::exception const& error)
    {
        LOGEXC_AND_CONTINUE;
        std::string message = "HidHide Profiles could not start or continue:\n\n";
        message += error.what();
        if (!startHidden) ::MessageBoxA(nullptr, message.c_str(), "HidHide Profiles", MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        LOGEXC_AND_CONTINUE;
        if (!startHidden) LocalizedMessageBox(IDS_STATIC_MESSAGEBOX_EXCEPTION, (MB_OK | MB_ICONERROR));
    }

    // Don't start the application's message pump as we are done already
    return (FALSE);
}
