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
#include <functional>
#include <memory>
#include <mutex>
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
    [[nodiscard]] bool TryPopObserverTraceForTest(beammp::observer::RawStoredPoseV1& output) noexcept { return mObserverTraceCapture->TryPopForTest(output); }
    void HandlePositionForTest(TClient& client, const std::string& packet) { HandlePosition(client, packet); }
    [[nodiscard]] bool StartObserverTraceForTest(beammp::observer::TraceCaptureConfiguration configuration, std::string_view partialFilename, std::uint64_t traceStartMonoNs) {
        if (mObserverTraceRuntime) return false;
        auto runtime = std::make_unique<beammp::observer::TraceCaptureRuntime>(*mObserverTraceCapture, std::move(configuration), 256, 4096);
        if (!runtime->StartForTest(partialFilename, traceStartMonoNs)) return false;
        mObserverTraceRuntime = std::move(runtime);
        mObserverTraceFinalized = false;
        return true;
    }
    void StopObserverTraceForTest() noexcept {
        if (!mObserverTraceRuntime) return;
        mObserverTraceRuntime->StopAndJoin();
        mObserverTraceFinalized = mObserverTraceRuntime->Finalized();
        mObserverTraceRuntime.reset();
    }
    [[nodiscard]] bool ObserverTraceRunningForTest() const noexcept { return mObserverTraceRuntime != nullptr; }
    [[nodiscard]] bool ObserverTraceFinalizedForTest() const noexcept { return mObserverTraceFinalized; }
#endif

private:
    TIoPollThread mIoCtxPoller;
#ifdef BEAMMP_OBSERVER_TRACE_TEST_ONLY
    // Heap-owned at startup: never put the 4 MiB fixed queue on main's stack
    // and never allocate from a packet producer.
    std::unique_ptr<beammp::observer::ObserverTraceCapture> mObserverTraceCapture;
    std::unique_ptr<beammp::observer::TraceCaptureRuntime> mObserverTraceRuntime;
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
