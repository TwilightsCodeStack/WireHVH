#pragma once

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace Config {
    // === Combat ===
    inline bool bAimAssist = false;
    inline bool bDrawFov = false;
    inline float fAimFov = 30.0f;
    inline float fAimSmooth = 5.0f;
    inline bool bAntiAim = false;
    inline float fPitchAngle = 89.0f;
    inline float fYawAngle = 0.0f;
    inline bool bSpin = false;
    inline float fSpinSpeed = 180.0f;

    // === Triggerbot ===
    inline bool bTriggerbot = false;
    inline int iTriggerDelay = 50;
    inline bool bTriggerTeam = false;

    // === Visuals — ESP ===
    inline bool bEsp = true;
    inline bool bEspBox = true;
    inline bool bEspHealth = true;
    inline bool bEspName = true;
    inline bool bEspSkeleton = true;
    inline bool bEspDistance = false;
    inline bool bEspSnaplines = false;

    // === Visuals — Glow / Chams ===
    inline bool bGlowEnabled = false;
    inline bool bChamsEnabled = false;
    inline bool bChamsXQZ = false;
    inline float fChamsVisibleColor[3] = {0.6f, 0.3f, 1.0f};
    inline float fChamsHiddenColor[3] = {0.2f, 0.1f, 0.5f};

    // === Visuals — Advanced ===
    inline bool bNoFlashEnabled = false;
    inline bool bNoScopeEnabled = false;
    inline bool bThirdPersonEnabled = false;
    inline float fThirdPersonDistance = 120.0f;

    // === Movement ===
    inline bool bBunnyHop = false;
    inline bool bWalkbot = false;
    inline float fWalkbotAimSmooth = 0.18f;
    inline float fWalkbotShootFov = 6.0f;
    inline int iWalkbotWanderMs = 2200;
    inline bool bWalkbotAutoFire = true;
    inline bool bWalkbotBindBhop = true;

    // === Utility / Farm ===
    inline bool bAutoFarm = false;
    inline bool bAutoRespawn = true;
    inline bool bAntiAfk = true;

    // === UI ===
    inline bool bShowMenu = true;
    inline bool bLiveSceneBackground = false;
    inline int iActiveTab = 0;

    inline bool Save(const std::string& path) {
        std::ofstream out(path, std::ios::trunc);
        if (!out)
            return false;

        out << "bAimAssist=" << bAimAssist << '\n'
            << "bDrawFov=" << bDrawFov << '\n'
            << "fAimFov=" << fAimFov << '\n'
            << "fAimSmooth=" << fAimSmooth << '\n'
            << "bTriggerbot=" << bTriggerbot << '\n'
            << "iTriggerDelay=" << iTriggerDelay << '\n'
            << "bTriggerTeam=" << bTriggerTeam << '\n'
            << "bAntiAim=" << bAntiAim << '\n'
            << "fPitchAngle=" << fPitchAngle << '\n'
            << "fYawAngle=" << fYawAngle << '\n'
            << "bSpin=" << bSpin << '\n'
            << "fSpinSpeed=" << fSpinSpeed << '\n'
            << "bEsp=" << bEsp << '\n'
            << "bEspBox=" << bEspBox << '\n'
            << "bEspHealth=" << bEspHealth << '\n'
            << "bEspName=" << bEspName << '\n'
            << "bEspSkeleton=" << bEspSkeleton << '\n'
            << "bEspDistance=" << bEspDistance << '\n'
            << "bEspSnaplines=" << bEspSnaplines << '\n'
            << "bGlowEnabled=" << bGlowEnabled << '\n'
            << "bChamsEnabled=" << bChamsEnabled << '\n'
            << "bChamsXQZ=" << bChamsXQZ << '\n'
            << "fChamsVisibleR=" << fChamsVisibleColor[0] << '\n'
            << "fChamsVisibleG=" << fChamsVisibleColor[1] << '\n'
            << "fChamsVisibleB=" << fChamsVisibleColor[2] << '\n'
            << "fChamsHiddenR=" << fChamsHiddenColor[0] << '\n'
            << "fChamsHiddenG=" << fChamsHiddenColor[1] << '\n'
            << "fChamsHiddenB=" << fChamsHiddenColor[2] << '\n'
            << "bNoFlashEnabled=" << bNoFlashEnabled << '\n'
            << "bNoScopeEnabled=" << bNoScopeEnabled << '\n'
            << "bThirdPersonEnabled=" << bThirdPersonEnabled << '\n'
            << "fThirdPersonDistance=" << fThirdPersonDistance << '\n'
            << "bBunnyHop=" << bBunnyHop << '\n'
            << "bWalkbot=" << bWalkbot << '\n'
            << "fWalkbotAimSmooth=" << fWalkbotAimSmooth << '\n'
            << "fWalkbotShootFov=" << fWalkbotShootFov << '\n'
            << "iWalkbotWanderMs=" << iWalkbotWanderMs << '\n'
            << "bWalkbotAutoFire=" << bWalkbotAutoFire << '\n'
            << "bWalkbotBindBhop=" << bWalkbotBindBhop << '\n'
            << "bAutoFarm=" << bAutoFarm << '\n'
            << "bAutoRespawn=" << bAutoRespawn << '\n'
            << "bAntiAfk=" << bAntiAfk << '\n'
            << "bLiveSceneBackground=" << bLiveSceneBackground << '\n';
        return out.good();
    }

    inline bool Load(const std::string& path) {
        std::ifstream in(path);
        if (!in)
            return false;

        const auto parseBool = [](const std::string& text, bool& target) {
            std::istringstream stream(text);
            int value = 0;
            if (!(stream >> value))
                return false;
            target = value != 0;
            return true;
        };
        const auto parseInt = [](const std::string& text, int& target) {
            std::istringstream stream(text);
            return static_cast<bool>(stream >> target);
        };
        const auto parseFloat = [](const std::string& text, float& target) {
            std::istringstream stream(text);
            return static_cast<bool>(stream >> target);
        };

        std::string key;
        while (std::getline(in, key, '=')) {
            std::string value;
            if (!std::getline(in, value))
                break;
            bool ok = true;
            if (key == "bAimAssist") ok = parseBool(value, bAimAssist);
            else if (key == "bDrawFov") ok = parseBool(value, bDrawFov);
            else if (key == "fAimFov") ok = parseFloat(value, fAimFov);
            else if (key == "fAimSmooth") ok = parseFloat(value, fAimSmooth);
            else if (key == "bTriggerbot") ok = parseBool(value, bTriggerbot);
            else if (key == "iTriggerDelay") ok = parseInt(value, iTriggerDelay);
            else if (key == "bTriggerTeam") ok = parseBool(value, bTriggerTeam);
            else if (key == "bAntiAim") ok = parseBool(value, bAntiAim);
            else if (key == "fPitchAngle") ok = parseFloat(value, fPitchAngle);
            else if (key == "fYawAngle") ok = parseFloat(value, fYawAngle);
            else if (key == "bSpin") ok = parseBool(value, bSpin);
            else if (key == "fSpinSpeed") ok = parseFloat(value, fSpinSpeed);
            else if (key == "bEsp") ok = parseBool(value, bEsp);
            else if (key == "bEspBox") ok = parseBool(value, bEspBox);
            else if (key == "bEspHealth") ok = parseBool(value, bEspHealth);
            else if (key == "bEspName") ok = parseBool(value, bEspName);
            else if (key == "bEspSkeleton") ok = parseBool(value, bEspSkeleton);
            else if (key == "bEspDistance") ok = parseBool(value, bEspDistance);
            else if (key == "bEspSnaplines") ok = parseBool(value, bEspSnaplines);
            else if (key == "bGlowEnabled") ok = parseBool(value, bGlowEnabled);
            else if (key == "bChamsEnabled") ok = parseBool(value, bChamsEnabled);
            else if (key == "bChamsXQZ") ok = parseBool(value, bChamsXQZ);
            else if (key == "fChamsVisibleR") ok = parseFloat(value, fChamsVisibleColor[0]);
            else if (key == "fChamsVisibleG") ok = parseFloat(value, fChamsVisibleColor[1]);
            else if (key == "fChamsVisibleB") ok = parseFloat(value, fChamsVisibleColor[2]);
            else if (key == "fChamsHiddenR") ok = parseFloat(value, fChamsHiddenColor[0]);
            else if (key == "fChamsHiddenG") ok = parseFloat(value, fChamsHiddenColor[1]);
            else if (key == "fChamsHiddenB") ok = parseFloat(value, fChamsHiddenColor[2]);
            else if (key == "bNoFlashEnabled") ok = parseBool(value, bNoFlashEnabled);
            else if (key == "bNoScopeEnabled") ok = parseBool(value, bNoScopeEnabled);
            else if (key == "bThirdPersonEnabled") ok = parseBool(value, bThirdPersonEnabled);
            else if (key == "fThirdPersonDistance") ok = parseFloat(value, fThirdPersonDistance);
            else if (key == "bBunnyHop") ok = parseBool(value, bBunnyHop);
            else if (key == "bWalkbot") ok = parseBool(value, bWalkbot);
            else if (key == "fWalkbotAimSmooth") ok = parseFloat(value, fWalkbotAimSmooth);
            else if (key == "fWalkbotShootFov") ok = parseFloat(value, fWalkbotShootFov);
            else if (key == "iWalkbotWanderMs") ok = parseInt(value, iWalkbotWanderMs);
            else if (key == "bWalkbotAutoFire") ok = parseBool(value, bWalkbotAutoFire);
            else if (key == "bWalkbotBindBhop") ok = parseBool(value, bWalkbotBindBhop);
            else if (key == "bAutoFarm") ok = parseBool(value, bAutoFarm);
            else if (key == "bAutoRespawn") ok = parseBool(value, bAutoRespawn);
            else if (key == "bAntiAfk") ok = parseBool(value, bAntiAfk);
            else if (key == "bLiveSceneBackground") ok = parseBool(value, bLiveSceneBackground);
            if (!ok)
                return false;
        }
        fAimFov = std::clamp(fAimFov, 1.0f, 45.0f);
        fAimSmooth = std::clamp(fAimSmooth, 1.0f, 20.0f);
        iTriggerDelay = std::clamp(iTriggerDelay, 0, 1000);
        fPitchAngle = std::clamp(fPitchAngle, -89.0f, 89.0f);
        fYawAngle = std::clamp(fYawAngle, -180.0f, 180.0f);
        fSpinSpeed = std::clamp(fSpinSpeed, 1.0f, 1440.0f);
        fThirdPersonDistance = std::clamp(fThirdPersonDistance, 32.0f, 300.0f);
        fWalkbotAimSmooth = std::clamp(fWalkbotAimSmooth, 0.01f, 1.0f);
        fWalkbotShootFov = std::clamp(fWalkbotShootFov, 0.1f, 45.0f);
        iWalkbotWanderMs = std::clamp(iWalkbotWanderMs, 100, 30000);
        for (int channel = 0; channel < 3; ++channel) {
            fChamsVisibleColor[channel] = std::clamp(fChamsVisibleColor[channel], 0.0f, 1.0f);
            fChamsHiddenColor[channel] = std::clamp(fChamsHiddenColor[channel], 0.0f, 1.0f);
        }
        return true;
    }
}
