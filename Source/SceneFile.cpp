// ---------------------------------------------------------------------------
// SceneFile.cpp — Binary .sime scene serialization.
// ---------------------------------------------------------------------------

#include "SceneFile.h"
#include <fstream>
#include <cstring>
#include "ViewPortComponent.h" // 

namespace
{
    static constexpr char     kMagic[4] = { 'S', 'I', 'M', 'E' };
    static constexpr uint16_t kVersion  = 8;   // v8: adds muteStartSec, muteEndSec

    // Tiny endian-agnostic helpers (no-op on x86 but keeps intent clear)
    template <typename T>
    void writeVal(std::ofstream& f, T v)  { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }

    template <typename T>
    bool readVal(std::ifstream& f, T& v)  { f.read(reinterpret_cast<char*>(&v), sizeof(T)); return f.good(); }
}

bool SceneFile::save(const std::string& path, const std::vector<BlockEntry>& blocks)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;

    // --- Header ---
    f.write(kMagic, 4);
    writeVal<uint16_t>(f, kVersion);
    writeVal<uint32_t>(f, static_cast<uint32_t>(blocks.size()));
    writeVal<uint16_t>(f, 0); // reserved

    // --- Block records ---
    for (const auto& b : blocks)
    {
        writeVal<int32_t>(f, b.serial);
        writeVal<uint8_t>(f, static_cast<uint8_t>(b.blockType));

        writeVal<int32_t>(f, b.pos.x);
        writeVal<int32_t>(f, b.pos.y);
        writeVal<int32_t>(f, b.pos.z);

        writeVal<int32_t>(f, b.soundId);
        writeVal<int32_t>(f, static_cast<int32_t>(b.colour.x * 255.0f));
        writeVal<int32_t>(f, static_cast<int32_t>(b.colour.y * 255.0f));
        writeVal<int32_t>(f, static_cast<int32_t>(b.colour.z * 255.0f));

        uint16_t pathLen = static_cast<uint16_t>(b.customFilePath.size());
        writeVal<uint16_t>(f, pathLen);
        if (pathLen > 0)
            f.write(b.customFilePath.data(), pathLen);

        writeVal<double>(f, b.startTimeSec);
        writeVal<double>(f, b.durationSec);
        writeVal<uint8_t>(f, b.durationLocked ? 1 : 0);

        writeVal<uint32_t>(f, static_cast<uint32_t>(b.timesList.size()));

        for (const auto& t : b.timesList)
        {
            writeVal<double>(f, t.startTimeSec);
            writeVal<double>(f, t.durationSec);
        }

        writeVal<uint8_t>(f, b.hasRecordedMovement ? 1 : 0);
        if (b.hasRecordedMovement)
        {
            writeVal<uint32_t>(f, static_cast<uint32_t>(b.recordedMovement.size()));
            for (const auto& kf : b.recordedMovement)
            {
                writeVal<double>(f, kf.timeSec);
                writeVal<int32_t>(f, kf.position.x);
                writeVal<int32_t>(f, kf.position.y);
                writeVal<int32_t>(f, kf.position.z);
            }
        }

        // --- v2 additions ---
        writeVal<uint8_t>(f, b.isLooping ? 1 : 0);
        writeVal<double>(f, b.loopDurationSec);

        // --- v5 additions: Phase 1 movement controls ---
        writeVal<uint8_t>(f, static_cast<uint8_t>(b.playbackMode));
        writeVal<double>(f, b.movementDurationSec);
        writeVal<int32_t>(f, b.movementYOffset);

        // --- v6 additions ---
        writeVal<uint8_t>(f, b.movementEnabled ? 1 : 0);

        // --- v7 additions ---
        writeVal<uint8_t>(f, b.isMuted  ? 1 : 0);
        writeVal<uint8_t>(f, b.isHidden ? 1 : 0);
        writeVal<double>(f, b.loopBufferSec);

        // --- v8 additions: time-window mute ---
        writeVal<double>(f, b.muteStartSec);
        writeVal<double>(f, b.muteEndSec);
    }

    return f.good();
}

bool SceneFile::load(const std::string& path, std::vector<BlockEntry>& outBlocks)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // --- Header ---
    char magic[4];
    f.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return false;

    uint16_t version = 0;
    if (!readVal(f, version) || version > kVersion) return false;

    uint32_t blockCount = 0;
    if (!readVal(f, blockCount)) return false;

    uint16_t reserved = 0;
    readVal(f, reserved); // discard

    // --- Block records ---
    outBlocks.clear();
    outBlocks.reserve(blockCount);

    for (uint32_t i = 0; i < blockCount; ++i)
    {
        BlockEntry b;

        if (!readVal<int32_t>(f, b.serial)) return false;

        uint8_t bt = 0;
        if (!readVal(f, bt)) return false;
        b.blockType = static_cast<BlockType>(bt);

        if (!readVal<int32_t>(f, b.pos.x)) return false;
        if (!readVal<int32_t>(f, b.pos.y)) return false;
        if (!readVal<int32_t>(f, b.pos.z)) return false;

        if (!readVal<int32_t>(f, b.soundId)) return false;

        // --- v3: colour stored here — immediately after soundId (matches save order) ---
        // NOTE: v1/v2 files did not have this field; derive colour from block type instead.
        if (version >= 3)
        {
            int32_t r = 0, g = 0, bl = 0;
            if (!readVal<int32_t>(f, r))  return false;
            if (!readVal<int32_t>(f, g))  return false;
            if (!readVal<int32_t>(f, bl)) return false;
            b.colour = Vec3f {
                juce::jlimit(0.0f, 1.0f, r / 255.0f),
                juce::jlimit(0.0f, 1.0f, g / 255.0f),
                juce::jlimit(0.0f, 1.0f, bl / 255.0f)
            };
        }
        else
        {
            b.colour = b.getBlockColor(b.blockType, b.soundId);
        }

        uint16_t pathLen = 0;
        if (!readVal(f, pathLen)) return false;
        if (pathLen > 0)
        {
            b.customFilePath.resize(pathLen);
            f.read(&b.customFilePath[0], pathLen);
            if (!f.good()) return false;
        }

        if (!readVal(f, b.startTimeSec)) return false;
        if (!readVal(f, b.durationSec))  return false;

        uint8_t dl = 0;
        if (!readVal(f, dl)) return false;
        b.durationLocked = (dl != 0);
        
        if (version >= 4)
        {
            uint32_t timeCount = 0;

            if (!readVal(f, timeCount))
                return false;

            b.timesList.resize(timeCount);

            for (uint32_t t = 0; t < timeCount; ++t)
            {
                if (!readVal(f, b.timesList[t].startTimeSec))
                    return false;

                if (!readVal(f, b.timesList[t].durationSec))
                    return false;
            }
        }

        uint8_t hm = 0;
        if (!readVal(f, hm)) return false;
        b.hasRecordedMovement = (hm != 0);

        if (b.hasRecordedMovement)
        {
            uint32_t kfCount = 0;
            if (!readVal(f, kfCount)) return false;

            b.recordedMovement.resize(kfCount);
            for (uint32_t k = 0; k < kfCount; ++k)
            {
                auto& kf = b.recordedMovement[k];
                if (!readVal(f, kf.timeSec))    return false;
                if (!readVal(f, kf.position.x)) return false;
                if (!readVal(f, kf.position.y)) return false;
                if (!readVal(f, kf.position.z)) return false;
            }
        }

        // --- v2 additions (isLooping + loopDurationSec, not present in v1) ---
        if (version >= 2)
        {
            uint8_t lp = 0;
            if (!readVal(f, lp)) return false;
            b.isLooping = (lp != 0);

            if (!readVal(f, b.loopDurationSec)) return false;
        }
        else
        {
            b.isLooping       = false;
            b.loopDurationSec = 4.0;
        }

        // --- v5 additions: playbackMode / movementDurationSec / movementYOffset ---
        if (version >= 5)
        {
            uint8_t pm = 0;
            if (!readVal(f, pm)) return false;
            b.playbackMode = static_cast<BlockPlaybackMode>(pm);

            if (!readVal(f, b.movementDurationSec)) return false;

            int32_t yo = 0;
            if (!readVal(f, yo)) return false;
            b.movementYOffset = yo;
        }
        else
        {
            // Map the legacy isLooping flag into the new mode enum.
            b.playbackMode        = b.isLooping ? BlockPlaybackMode::Loop
                                                : BlockPlaybackMode::Natural;
            b.movementDurationSec = 0.0;
            b.movementYOffset     = 0;
        }

        if (version >= 6)
        {
            uint8_t me = 1;
            if (!readVal(f, me)) return false;
            b.movementEnabled = (me != 0);
        }
        else
        {
            // Older files: movement plays whenever a path was saved.
            b.movementEnabled = b.hasRecordedMovement;
        }

        if (version >= 7)
        {
            uint8_t mu = 0, hd = 0;
            if (!readVal(f, mu)) return false;
            if (!readVal(f, hd)) return false;
            if (!readVal(f, b.loopBufferSec)) return false;
            b.isMuted  = (mu != 0);
            b.isHidden = (hd != 0);
        }

        if (version >= 8)
        {
            if (!readVal(f, b.muteStartSec)) return false;
            if (!readVal(f, b.muteEndSec))   return false;
        }

        b.resetPlaybackState();
        outBlocks.push_back(std::move(b));
    }

    return f.good() || f.eof();
}
