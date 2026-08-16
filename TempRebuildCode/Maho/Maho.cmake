# Maho core third-party deps.
#
# Refactor: third-party libraries are now self-contained per plugin via
# <Name>.cmake (spdlog → Log, toml++ → Config, nlohmann/json → Json,
# zstd/zlib → Compress, utfcpp → Text, Jolt → Physics, miniaudio → Audio,
# GLM → Math). Engine core itself has no third-party dependency.
