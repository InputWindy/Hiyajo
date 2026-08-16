#pragma once

/**
 * Project-side .casset (MCAS) package codec registration.
 *
 * The byte format lives in ResourceCasset.cpp. The core resource system only
 * knows the generic FPackageCodec hook; this function installs the MCAS
 * implementation before any package is loaded.
 */

#include <ResourceSystem.h>

namespace Maho
{

void RegisterCassetPackageCodec(FResourceSystem& System);

} // namespace Maho
