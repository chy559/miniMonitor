#pragma once

#include "app_models.h"

#include <memory>
#include <string>

class SystemSampler {
public:
    SystemSampler();
    ~SystemSampler();

    SystemSampler(const SystemSampler&) = delete;
    SystemSampler& operator=(const SystemSampler&) = delete;

    Metrics collect();
    void setDiskRoot(const std::wstring& root);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
