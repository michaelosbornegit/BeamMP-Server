// BeamMP, the BeamNG.drive multiplayer mod.
// Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
//
// BeamMP Ltd. can be contacted by electronic mail via contact@beammp.com.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "IThreaded.h"
#include "RWMutex.h"
#include "TIoPollThread.h"
#include "TScopedTimer.h"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>

#include "BoostAliases.h"

#ifdef BEAMMP_OBSERVER_TRACE_TEST_ONLY
#include "ObserverTraceCapture.h"
#include "ObserverTraceWriter.h"
#endif

class TClient;
class TNetwork;
class TPPSMonitor;

class TServer final {
public:
    using TClientSet = std::unordered_set<std::shared_ptr<TClient>>;

    TServer(const std::vector<std::string_view>& Arguments);

    void InsertClient(const std::shared_ptr<TClient>& Ptr);
    void RemoveClient(const std::weak_ptr<TClient>&);
    // in Fn, return true to continue, return false to break
    void ForEachClient(const std::function<bool(std::weak_ptr<TClient>)>& Fn);
    size_t ClientCount() const;

    void GlobalParser(const std::weak_ptr<TClient>& Client, std::vector<uint8_t>&& Packet, TPPSMonitor& PPSMonitor, TNetwork& Network, bool udp);
    static void HandleEvent(TClient& c, const std::string& Data);
    RWMutex& GetClientMutex() const { return mClientsMutex; }

    const TScopedTimer UptimeTimer;

    io_context& IoCtx() { return mIoCtxPoller.IoCtx(); }

#ifdef BEAMMP_OBSERVER_TRACE_TEST_ONLY
    // Narrow test seam for proving the production post-store boundary without
    // exposing it in a normal build.
    void SetObserverTraceEnabledForTest(const bool enabled) noexcept { mObserverTraceCapture->SetEnabledForTest(enabled); }
    void ForceObserverTraceNonLockFreeForTest() noexcept { mObserverTraceCapture->ForceNonLockFreeForTest(); }
    void ForceObserverTraceWriterFaultForTest() noexcept {
        if (mObserverTraceRuntime) mObserverTraceRuntime->RequestWriterFaultForTest();
    }
    void DelayObserverTraceFinalizationForTest(const std::chrono::milliseconds delay) noexcept {
        if (mObserverTraceRuntime) mObserverTraceRuntime->DelayFinalizationForTest(delay);
    }
    [[nodiscard]] bool TryPopObserverTraceForTest(beammp::observer::RawStoredPoseV1& output) noexcept { return mObserverTraceCapture->TryPopForTest(output); }
    [[nodiscard]] bool HandleObserverTracePoseForTest(const std::int32_t playerId, const std::int32_t vehicleId, const std::string_view pose, const std::uint64_t acceptedMonoNs) noexcept {
        return mObserverTraceCapture->TryCaptureStoredPose(playerId, vehicleId, pose, acceptedMonoNs);
    }
    void HandlePositionForTest(TClient& client, const std::string& packet) { HandlePosition(client, packet); }
    [[nodiscard]] bool StartObserverTraceForTest(beammp::observer::TraceCaptureConfiguration configuration, std::string_view partialFilename, std::uint64_t traceStartMonoNs) {
        if (mObserverTraceRuntime) return false;
        auto runtime = std::make_unique<beammp::observer::TraceCaptureRuntime>(*mObserverTraceCapture, configuration, 256, 4096);
        if (!runtime->StartForTest(partialFilename, traceStartMonoNs)) return false;
        mObserverTraceConfiguration = std::move(configuration);
        mObserverTraceRuntime = std::move(runtime);
        mObserverTraceFinalized = false;
        return true;
    }
    void StopObserverTraceForTest() noexcept {
        ShutdownObserverTraceForTest();
    }
    // Called by the application shutdown handler while this server remains
    // alive. It disables packet producers before joining the worker and is
    // safe if explicit runtime control already stopped the trace.
    void ShutdownObserverTraceForTest(const std::chrono::milliseconds deadline = std::chrono::seconds(5)) noexcept {
        if (!mObserverTraceRuntime) return;
        mObserverTraceRuntime->StopAndJoin(deadline);
        mObserverTraceFinalized = mObserverTraceRuntime->Finalized();
        mObserverTraceRuntime.reset();
    }
    [[nodiscard]] bool ObserverTraceRunningForTest() const noexcept { return mObserverTraceRuntime != nullptr; }
    [[nodiscard]] bool ObserverTraceFinalizedForTest() const noexcept {
        return mObserverTraceFinalized || (mObserverTraceRuntime && mObserverTraceRuntime->Finalized());
    }
    // The console command delegates to this narrow, test-build-only control
    // boundary. `off` is deliberately just the producer kill switch: worker
    // draining and file finalization remain asynchronous.
    [[nodiscard]] std::string RunObserverTraceCommandForTest(const std::string_view command) noexcept {
        if (command == "status") {
            const auto metrics = mObserverTraceCapture->MetricsForTest();
            return std::string("observertrace ") + (mObserverTraceCapture->IsEnabled() ? "enabled " : "disabled ")
                + (mObserverTraceRuntime ? "running" : "idle")
                + " accepted=" + std::to_string(metrics.QueuedSuccesses)
                + " pending=" + std::to_string(metrics.Pending)
                + " invalid_id=" + std::to_string(mObserverTraceCapture->InvalidIds())
                + " invalid_payload=" + std::to_string(mObserverTraceCapture->InvalidPayloads())
                + " oversize=" + std::to_string(mObserverTraceCapture->Oversize())
                + " evicted=" + std::to_string(metrics.EvictedOldest)
                + " contention_drop=" + std::to_string(metrics.ContentionDrops)
                + (mObserverTraceRuntime && mObserverTraceRuntime->WriterFaulted() ? " writer=faulted" : "");
        }
        if (command == "off") {
            mObserverTraceCapture->Disable();
            return "observertrace disabled";
        }
        if (command == "on") {
            if (mObserverTraceRuntime) {
                if (!mObserverTraceRuntime->Finalized()) return "observertrace unavailable";
                mObserverTraceRuntime->StopAndJoin();
                mObserverTraceFinalized = mObserverTraceRuntime->Finalized();
                mObserverTraceRuntime.reset();
            }
            if (!mObserverTraceConfiguration) return "observertrace unavailable";
            const auto traceStartMonoNs = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            const auto partialFilename = "beammp-accepted-pose-on-" + std::to_string(traceStartMonoNs) + ".ndjson.part";
            return StartObserverTraceForTest(*mObserverTraceConfiguration, partialFilename, traceStartMonoNs)
                ? "observertrace enabled"
                : "observertrace unavailable";
        }
        return "observertrace invalid command";
    }
    [[nodiscard]] bool ObserverTraceCaptureEnabledForTest() const noexcept { return mObserverTraceCapture->IsEnabled(); }
#endif

private:
    TIoPollThread mIoCtxPoller;
#ifdef BEAMMP_OBSERVER_TRACE_TEST_ONLY
    // Heap-owned at startup: never put the 4 MiB fixed queue on main's stack
    // and never allocate from a packet producer.
    std::unique_ptr<beammp::observer::ObserverTraceCapture> mObserverTraceCapture;
    std::unique_ptr<beammp::observer::TraceCaptureRuntime> mObserverTraceRuntime;
    std::optional<beammp::observer::TraceCaptureConfiguration> mObserverTraceConfiguration;
    bool mObserverTraceFinalized {};
#endif
    TClientSet mClients;
    mutable RWMutex mClientsMutex;
    static void ParseVehicle(TClient& c, const std::string& Pckt, TNetwork& Network);
    static bool ShouldSpawn(TClient& c, const std::string& CarJson, int ID);
    static bool IsUnicycle(TClient& c, const std::string& CarJson);
    static void Apply(TClient& c, int VID, const std::string& pckt);
    void HandlePosition(TClient& c, const std::string& Packet);
};

struct BufferView {
    uint8_t* Data { nullptr };
    size_t Size { 0 };
    const uint8_t* data() const { return Data; }
    uint8_t* data() { return Data; }
    size_t size() const { return Size; }
};
