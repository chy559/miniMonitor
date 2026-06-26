#pragma once

#include "app_models.h"

class CodexQuotaClient {
public:
    CodexQuota fetch(bool forceRefresh);
};
