#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "system_sampler.h"

#include "monitor_logic.h"

#include <windows.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
ULONGLONG fileTimeToUInt64(const FILETIME& ft) {
    ULARGE_INTEGER li{};
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart;
}

} // namespace

class SystemSampler::Impl {
public:
    Impl() {
        queryDiskCounters();
        queryNetworkCounters();
        queryGpuCounters();
        gpuName_ = detectGpuName();
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        processorCount_ = std::max<DWORD>(1, info.dwNumberOfProcessors);
    }

    ~Impl() {
        if (pdhQuery_) {
            PdhCloseQuery(pdhQuery_);
        }
        if (networkQuery_) {
            PdhCloseQuery(networkQuery_);
        }
        if (gpuQuery_) {
            PdhCloseQuery(gpuQuery_);
        }
    }

    Metrics collect() {
        Metrics metrics;
        metrics.cpu = sampleCpu();
        sampleMemory(metrics);
        sampleDisk(metrics);
        sampleNetwork(metrics);
        sampleDiskIo(metrics);
        metrics.gpuName = gpuName_;
        sampleProcesses(metrics);
        sampleGpuProcesses(metrics);
        return metrics;
    }

    void setDiskRoot(const std::wstring& root) {
        const std::wstring next = root.empty() ? L"C:\\" : root;
        if (diskRoot_ == next) {
            return;
        }
        diskRoot_ = next;
        queryDiskCounters();
    }

private:
    CpuSnapshot cpu_;
    NetworkSnapshot network_;
    PDH_HQUERY pdhQuery_ = nullptr;
    PDH_HCOUNTER diskReadCounter_ = nullptr;
    PDH_HCOUNTER diskWriteCounter_ = nullptr;
    PDH_HQUERY networkQuery_ = nullptr;
    PDH_HCOUNTER networkInCounter_ = nullptr;
    PDH_HCOUNTER networkOutCounter_ = nullptr;
    PDH_HQUERY gpuQuery_ = nullptr;
    PDH_HCOUNTER gpuCounter_ = nullptr;
    std::wstring gpuName_;
    DWORD processorCount_ = 1;
    ULONGLONG processSampleTick_ = 0;
    std::map<DWORD, ULONGLONG> previousProcessTimes_;
    std::wstring diskRoot_ = L"C:\\";

    double sampleCpu() {
        FILETIME idleTime{}, kernelTime{}, userTime{};
        if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
            return 0.0;
        }

        CpuSnapshot current{
            fileTimeToUInt64(idleTime),
            fileTimeToUInt64(kernelTime),
            fileTimeToUInt64(userTime),
            true,
        };

        double usage = 0.0;
        if (cpu_.valid) {
            const auto idleDelta = current.idle - cpu_.idle;
            const auto kernelDelta = current.kernel - cpu_.kernel;
            const auto userDelta = current.user - cpu_.user;
            const auto total = kernelDelta + userDelta;
            if (total > 0) {
                usage = 1.0 - (static_cast<double>(idleDelta) / static_cast<double>(total));
            }
        }
        cpu_ = current;
        return std::clamp(usage, 0.0, 1.0);
    }

    void sampleMemory(Metrics& metrics) {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status)) {
            return;
        }
        metrics.memoryTotal = status.ullTotalPhys;
        metrics.memoryUsed = status.ullTotalPhys - status.ullAvailPhys;
        metrics.memory = static_cast<double>(metrics.memoryUsed) / static_cast<double>(status.ullTotalPhys);
    }

    void sampleDisk(Metrics& metrics) {
        ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFreeBytes{};
        if (!GetDiskFreeSpaceExW(diskRoot_.c_str(), &freeBytes, &totalBytes, &totalFreeBytes)) {
            return;
        }
        metrics.diskRoot = diskRoot_;
        metrics.diskTotal = totalBytes.QuadPart;
        metrics.diskUsed = totalBytes.QuadPart - totalFreeBytes.QuadPart;
        metrics.disk = static_cast<double>(metrics.diskUsed) / static_cast<double>(metrics.diskTotal);
    }

    void sampleNetwork(Metrics& metrics) {
        if (sampleNetworkPdh(metrics)) {
            return;
        }

        unsigned long long inBytes = 0;
        unsigned long long outBytes = 0;
        DWORD tableSize = 0;
        if (GetIfTable(nullptr, &tableSize, FALSE) == ERROR_INSUFFICIENT_BUFFER && tableSize > 0) {
            std::vector<BYTE> buffer(tableSize);
            auto table = reinterpret_cast<MIB_IFTABLE*>(buffer.data());
            if (GetIfTable(table, &tableSize, FALSE) == NO_ERROR) {
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const auto& row = table->table[i];
                    if (row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL || row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
                        continue;
                    }
                    inBytes += row.dwInOctets;
                    outBytes += row.dwOutOctets;
                }
            }
        }

        NetworkSnapshot current{inBytes, outBytes, GetTickCount64(), true};
        if (network_.valid && current.tick > network_.tick) {
            const double seconds = static_cast<double>(current.tick - network_.tick) / 1000.0;
            const unsigned long long inDelta = monitor_logic::counterDelta32(current.inBytes, network_.inBytes);
            const unsigned long long outDelta = monitor_logic::counterDelta32(current.outBytes, network_.outBytes);
            metrics.netDown = static_cast<double>(inDelta) / seconds;
            metrics.netUp = static_cast<double>(outDelta) / seconds;
        }
        network_ = current;
        const double peak = 20.0 * 1024.0 * 1024.0;
        metrics.network = std::clamp((metrics.netDown + metrics.netUp) / peak, 0.0, 1.0);
    }

    void queryNetworkCounters() {
        if (PdhOpenQueryW(nullptr, 0, &networkQuery_) != ERROR_SUCCESS) {
            networkQuery_ = nullptr;
            return;
        }

        using AddEnglishCounter = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
        HMODULE pdh = LoadLibraryW(L"pdh.dll");
        AddEnglishCounter addEnglishCounter = nullptr;
        if (pdh) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            addEnglishCounter = reinterpret_cast<AddEnglishCounter>(GetProcAddress(pdh, "PdhAddEnglishCounterW"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        }
        const auto addCounter = addEnglishCounter ? addEnglishCounter : PdhAddCounterW;

        const auto inStatus = addCounter(
            networkQuery_, L"\\Network Interface(*)\\Bytes Received/sec", 0, &networkInCounter_);
        const auto outStatus = addCounter(
            networkQuery_, L"\\Network Interface(*)\\Bytes Sent/sec", 0, &networkOutCounter_);
        if (pdh) {
            FreeLibrary(pdh);
        }

        if (inStatus != ERROR_SUCCESS || outStatus != ERROR_SUCCESS) {
            PdhCloseQuery(networkQuery_);
            networkQuery_ = nullptr;
            networkInCounter_ = nullptr;
            networkOutCounter_ = nullptr;
            return;
        }
        PdhCollectQueryData(networkQuery_);
    }

    bool sampleNetworkPdh(Metrics& metrics) {
        if (!networkQuery_ || !networkInCounter_ || !networkOutCounter_) {
            return false;
        }
        if (PdhCollectQueryData(networkQuery_) != ERROR_SUCCESS) {
            return false;
        }

        double inBytes = 0.0;
        double outBytes = 0.0;
        if (!sumFormattedCounterArray(networkInCounter_, inBytes) ||
            !sumFormattedCounterArray(networkOutCounter_, outBytes)) {
            return false;
        }

        metrics.netDown = std::max(0.0, inBytes);
        metrics.netUp = std::max(0.0, outBytes);
        const double peak = 20.0 * 1024.0 * 1024.0;
        metrics.network = std::clamp((metrics.netDown + metrics.netUp) / peak, 0.0, 1.0);
        return true;
    }

    bool sumFormattedCounterArray(PDH_HCOUNTER counter, double& total) {
        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bufferSize == 0 || itemCount == 0) {
            return false;
        }

        std::vector<BYTE> buffer(bufferSize);
        auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS) {
            return false;
        }

        total = 0.0;
        for (DWORD i = 0; i < itemCount; ++i) {
            if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !items[i].szName) {
                continue;
            }
            std::wstring name = items[i].szName;
            std::transform(name.begin(), name.end(), name.begin(), [](wchar_t ch) {
                return static_cast<wchar_t>(towlower(ch));
            });
            if (name.find(L"loopback") != std::wstring::npos) {
                continue;
            }
            total += std::max(0.0, items[i].FmtValue.doubleValue);
        }
        return true;
    }

    void queryDiskCounters() {
        if (pdhQuery_) {
            PdhCloseQuery(pdhQuery_);
            pdhQuery_ = nullptr;
            diskReadCounter_ = nullptr;
            diskWriteCounter_ = nullptr;
        }
        if (PdhOpenQueryW(nullptr, 0, &pdhQuery_) != ERROR_SUCCESS) {
            pdhQuery_ = nullptr;
            return;
        }

        using AddEnglishCounter = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
        HMODULE pdh = LoadLibraryW(L"pdh.dll");
        AddEnglishCounter addEnglishCounter = nullptr;
        if (pdh) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            addEnglishCounter = reinterpret_cast<AddEnglishCounter>(GetProcAddress(pdh, "PdhAddEnglishCounterW"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        }
        const auto addCounter = addEnglishCounter ? addEnglishCounter : PdhAddCounterW;

        std::wstring instance = diskRoot_.size() >= 2 ? diskRoot_.substr(0, 2) : L"C:";
        const std::wstring readPath = L"\\LogicalDisk(" + instance + L")\\Disk Read Bytes/sec";
        const std::wstring writePath = L"\\LogicalDisk(" + instance + L")\\Disk Write Bytes/sec";
        const auto readStatus = addCounter(
            pdhQuery_, readPath.c_str(), 0, &diskReadCounter_);
        const auto writeStatus = addCounter(
            pdhQuery_, writePath.c_str(), 0, &diskWriteCounter_);
        if (pdh) {
            FreeLibrary(pdh);
        }

        if (readStatus != ERROR_SUCCESS || writeStatus != ERROR_SUCCESS) {
            PdhCloseQuery(pdhQuery_);
            pdhQuery_ = nullptr;
            diskReadCounter_ = nullptr;
            diskWriteCounter_ = nullptr;
            return;
        }
        PdhCollectQueryData(pdhQuery_);
    }

    void queryGpuCounters() {
        if (PdhOpenQueryW(nullptr, 0, &gpuQuery_) != ERROR_SUCCESS) {
            gpuQuery_ = nullptr;
            return;
        }

        using AddEnglishCounter = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
        HMODULE pdh = LoadLibraryW(L"pdh.dll");
        AddEnglishCounter addEnglishCounter = nullptr;
        if (pdh) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            addEnglishCounter = reinterpret_cast<AddEnglishCounter>(GetProcAddress(pdh, "PdhAddEnglishCounterW"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        }
        const auto addCounter = addEnglishCounter ? addEnglishCounter : PdhAddCounterW;

        const auto status = addCounter(gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_);
        if (pdh) {
            FreeLibrary(pdh);
        }
        if (status != ERROR_SUCCESS) {
            PdhCloseQuery(gpuQuery_);
            gpuQuery_ = nullptr;
            gpuCounter_ = nullptr;
            return;
        }
        PdhCollectQueryData(gpuQuery_);
    }

    void sampleDiskIo(Metrics& metrics) {
        if (!pdhQuery_) {
            return;
        }
        if (PdhCollectQueryData(pdhQuery_) != ERROR_SUCCESS) {
            return;
        }

        PDH_FMT_COUNTERVALUE value{};
        if (PdhGetFormattedCounterValue(diskReadCounter_, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS &&
            value.CStatus == ERROR_SUCCESS) {
            metrics.diskRead = std::max(0.0, value.doubleValue);
        }
        if (PdhGetFormattedCounterValue(diskWriteCounter_, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS &&
            value.CStatus == ERROR_SUCCESS) {
            metrics.diskWrite = std::max(0.0, value.doubleValue);
        }
    }

    std::wstring detectGpuName() {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index) {
            if ((device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0) {
                return device.DeviceString;
            }
            device.cb = sizeof(device);
        }
        device.cb = sizeof(device);
        if (EnumDisplayDevicesW(nullptr, 0, &device, 0)) {
            return device.DeviceString;
        }
        return L"N/A";
    }

    void sampleProcesses(Metrics& metrics) {
        std::vector<ProcessRow> rows;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        const double elapsed = processSampleTick_ > 0 && now > processSampleTick_
            ? static_cast<double>(now - processSampleTick_) / 1000.0
            : 0.0;
        std::map<DWORD, ULONGLONG> currentTimes;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                PROCESS_MEMORY_COUNTERS pmc{};
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
                if (process) {
                    FILETIME createTime{}, exitTime{}, kernelTime{}, userTime{};
                    ULONGLONG procTime = 0;
                    if (GetProcessTimes(process, &createTime, &exitTime, &kernelTime, &userTime)) {
                        procTime = fileTimeToUInt64(kernelTime) + fileTimeToUInt64(userTime);
                        currentTimes[entry.th32ProcessID] = procTime;
                    }
                    if (GetProcessMemoryInfo(process, &pmc, sizeof(pmc))) {
                        double cpu = 0.0;
                        auto previous = previousProcessTimes_.find(entry.th32ProcessID);
                        if (elapsed > 0.0 && previous != previousProcessTimes_.end() && procTime >= previous->second) {
                            cpu = ((static_cast<double>(procTime - previous->second) / 10000000.0) / elapsed) *
                                  100.0 / static_cast<double>(processorCount_);
                        }
                        rows.push_back({entry.szExeFile, entry.th32ProcessID, pmc.WorkingSetSize, cpu, -1.0});
                    }
                    CloseHandle(process);
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);

        previousProcessTimes_ = std::move(currentTimes);
        processSampleTick_ = now;

        metrics.topMemory = rows;
        std::sort(metrics.topMemory.begin(), metrics.topMemory.end(), [](const ProcessRow& a, const ProcessRow& b) {
            return a.memory > b.memory;
        });
        if (metrics.topMemory.size() > 3) {
            metrics.topMemory.resize(3);
        }

        metrics.topCpu = std::move(rows);
        std::sort(metrics.topCpu.begin(), metrics.topCpu.end(), [](const ProcessRow& a, const ProcessRow& b) {
            return a.cpu > b.cpu;
        });
        if (metrics.topCpu.size() > 3) {
            metrics.topCpu.resize(3);
        }
    }

    void sampleGpuProcesses(Metrics& metrics) {
        if (!gpuQuery_ || !gpuCounter_) {
            return;
        }
        if (PdhCollectQueryData(gpuQuery_) != ERROR_SUCCESS) {
            return;
        }

        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bufferSize == 0 || itemCount == 0) {
            return;
        }

        std::vector<BYTE> buffer(bufferSize);
        auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        status = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS) {
            return;
        }

        std::vector<std::pair<std::uint32_t, double>> samples;
        samples.reserve(itemCount);
        for (DWORD i = 0; i < itemCount; ++i) {
            if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !items[i].szName) {
                continue;
            }
            const double usage = std::max(0.0, items[i].FmtValue.doubleValue);

            std::wstring instance = items[i].szName;
            size_t pidPos = instance.find(L"pid_");
            if (pidPos == std::wstring::npos) {
                continue;
            }
            pidPos += 4;
            DWORD pid = 0;
            while (pidPos < instance.size() && iswdigit(instance[pidPos])) {
                pid = pid * 10 + static_cast<DWORD>(instance[pidPos] - L'0');
                ++pidPos;
            }
            samples.emplace_back(pid, usage);
        }
        const auto aggregate = monitor_logic::aggregateGpuSamples(samples);
        metrics.gpu = aggregate.overall / 100.0;

        for (const auto& [rawPid, gpu] : aggregate.byProcess) {
            if (gpu <= 0.05) {
                continue;
            }
            ProcessRow row;
            row.pid = static_cast<DWORD>(rawPid);
            row.gpu = gpu;
            row.name = processName(row.pid);
            metrics.topGpu.push_back(row);
        }
        std::sort(metrics.topGpu.begin(), metrics.topGpu.end(), [](const ProcessRow& a, const ProcessRow& b) {
            return a.gpu > b.gpu;
        });
        if (metrics.topGpu.size() > 3) {
            metrics.topGpu.resize(3);
        }
    }

    std::wstring processName(DWORD pid) {
        std::wstring name = L"pid " + std::to_wstring(pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) {
            return name;
        }
        wchar_t buffer[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
            std::wstring path(buffer, size);
            size_t slash = path.find_last_of(L"\\/");
            name = slash == std::wstring::npos ? path : path.substr(slash + 1);
        }
        CloseHandle(process);
        return name;
    }
};

SystemSampler::SystemSampler() : impl_(std::make_unique<Impl>()) {}

SystemSampler::~SystemSampler() = default;

Metrics SystemSampler::collect() {
    return impl_->collect();
}

void SystemSampler::setDiskRoot(const std::wstring& root) {
    impl_->setDiskRoot(root);
}
