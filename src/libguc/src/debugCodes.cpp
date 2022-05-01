#include "debugCodes.h"

#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/registryManager.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
  TF_DEBUG_ENVIRONMENT_SYMBOL(GUC, "GUC debug logging");
}

PXR_NAMESPACE_CLOSE_SCOPE
