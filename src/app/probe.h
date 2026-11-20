// Real-session facts for the capability report. Only QtDBus queries — no selection access, no user
// data.
#pragma once

#include "app/capability_report.h"

#include "secrets/secret_store.h"

namespace app {

SessionFacts probeSessionFacts(const secrets::AvailabilityReport &secrets);
secrets::ServiceProbe probeSecretService();

}  // namespace app
