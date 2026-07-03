#pragma once
#include <cstdint>

namespace RE
{
    struct Vector3
    {
        float x;
        float y;
        float z;

        Vector3 operator-(const Vector3& other) const { return { x - other.x, y - other.y, z - other.z }; }
        Vector3 operator+(const Vector3& other) const { return { x + other.x, y + other.y, z + other.z }; }
        Vector3 operator*(float s) const { return { x * s, y * s, z * s }; }
        float Length() const { return sqrtf(x * x + y * y + z * z); }
        float LengthXZ() const { return sqrtf(x * x + z * z); }
    };
    static_assert(sizeof(Vector3) == 0xC);

    struct InputValue
    {
        float valueA;
        float valueB;
        bool  bIsPressed;
    };

    struct Player
    {
        uint64_t unk00;
        uint64_t unk08;
        uint64_t unk10;
        uint64_t unk18;
        uint64_t unk20;
        uint64_t unk28;
        uint64_t unk30;
        int16_t  playerId_38;
    };

    enum CameraModeFlags : uint32_t
    {
        kCombat        = 1 << 0,
        kUnk2          = 1 << 1,
        kTactical      = 1 << 2,
        kUnk8          = 1 << 3,
        kUnk10         = 1 << 4,
        kUnk20         = 1 << 5,
        kUnk40         = 1 << 6,
        kUnk80         = 1 << 7,
        kMouseRotation = 1 << 8
    };

    // Reverse-engineered camera definition (settings/limits)
    // Based on ersh1/BG3_NativeCameraTweaks RE work (GPL-3.0)
    struct CameraDefinition
    {
        float float_00;
        float float_04;
        float float_08;
        float float_0C;
        float float_10;
        float float_14;
        float float_18;
        float float_1C;
        float float_20;
        float unkDefaultZoom_24;
        float maxZoom_28;
        float minZoom_2C;
        float altMaxZoomController_30;
        float altMinZoomController_34;
        float float_38;
        float float_3C;
        float float_40;
        float float_44;
        float pitchAdjustSpeedA_48;
        float float_4C;
        float float_50;
        float unkDefaultZoom_54;
        float maxZoomUnk_58;
        float minZoomUnk_5C;
        float unkDefaultZoom_60;
        float camHorizontalOffsetMult_64;
        float camVerticalOffsetMult_68;
        float float_6C;
        float altCamHorizontalOffsetMultController_70;
        float altCamVerticalOffsetMultController_74;
        float float_78;
        float float_7C;
        float float_80;
        float fovClose_84;
        float fovFar_88;
        float fovCloseAlt_8C;
        float fovFarAlt_90;
        float float_94;
        float float_98;
        float float_9C;
        float float_A0;
        float float_A4;
        float float_A8;
        float zoomSpeed_AC;
        float scrollSpeed_B0;
        float float_B4;
        float float_B8;
        float float_BC;
        float float_C0;
        float tactDefaultZoom_C4;
        float tactMinZoom_C8;
        float tactMaxZoom_CC;
        float tacticalFov_D0;
        float maxCamDistanceFromRoot_D4;
        float float_D8;
        float float_DC;
        float float_E0;
        float float_E4;
        float float_E8;
        float float_EC;
        float pitchAdjustSpeedB_F0;
        float pitchAdjustSpeedC_F4;
        float float_F8;
        float float_FC;
        float float_100;
        float float_104;
        float float_108;
        float float_10C;
        float float_110;
        float float_114;
        float float_118;
        float float_11C;
        float float_120;
        float float_124;
        float float_128;
        float float_12C;
        float float_130;
        uint32_t unk134;
        float float_138;
        float float_13C;
        float float_140;
        float float_144;
        float float_148;
        float float_14C;
        uint64_t unk150;
        float float_158;
        float float_15C;
        float pitchFar_160;
        float pitchClose_164;
        float pitchCombatFar_168;
        float pitchCombatClose_16C;
        float tacticalPitchFar_170;
        float tacticalPitchClose_174;
        float pitchFarAlt_178;
        float pitchCloseAlt_17C;
        uint32_t float_180;
        float float_184;
        float float_188;
        float float_18C;
    };
    static_assert(offsetof(CameraDefinition, fovClose_84) == 0x84);
    static_assert(offsetof(CameraDefinition, pitchAdjustSpeedB_F0) == 0xF0);

    // Live camera object (runtime state).
    // Layout taken verbatim from current ersh1/BG3_NativeCameraTweaks.
    // NOTE: an earlier version of this file had a bogus 4-byte pad after
    // desiredCameraRootPos which shifted every field between 0x30 and 0xF0
    // (desiredZoom was read at 0x60 instead of 0x5C, cameraModeFlags at 0xAC
    // instead of 0xA8) - that made all camera writes land in dead fields.
    struct CameraObject
    {
        uint64_t unk00;                  // 0x00
        uint64_t unk08;                  // 0x08
        uint64_t unk10;                  // 0x10
        Vector3  cameraRootPos;          // 0x18 - current root position
        Vector3  desiredCameraRootPos;   // 0x24 - where camera wants to go
        uint32_t unk30;                  // 0x30
        uint32_t unk34;                  // 0x34
        uint32_t unk38;                  // 0x38
        Vector3  unkCameraRootPosA;      // 0x3C
        Vector3  unkCameraRootPosB;      // 0x48
        float    currentZoomA;           // 0x54
        float    currentZoomB;           // 0x58
        float    desiredZoom;            // 0x5C
        float    float_60;               // 0x60
        float    float_64;               // 0x64
        float    float_68;               // 0x68
        float    fltmax_6C;              // 0x6C
        Vector3  cameraRotation;         // 0x70
        Vector3  prevCameraRotation;     // 0x7C
        uint32_t unk88;                  // 0x88
        uint32_t unk8C;                  // 0x8C
        uint32_t unk90;                  // 0x90
        float    horizontalPanDelta;     // 0x94
        float    verticalPanDelta;       // 0x98
        float    currentAngleDelta;      // 0x9C
        float    mouseRotationDelta;     // 0xA0
        float    zoomDelta;              // 0xA4
        CameraModeFlags cameraModeFlags; // 0xA8
        float    angle;                  // 0xAC - camera yaw in degrees
        uint32_t unkB0;                  // 0xB0
        uint32_t unkB4;                  // 0xB4
        uint32_t unkB8;                  // 0xB8
        uint32_t unkBC;                  // 0xBC
        float    rotationSpeed;          // 0xC0
        Vector3  cameraPos;              // 0xC4 - final world position
        float    verticalOffset;         // 0xD0
        float    float_D4;               // 0xD4
        float    float_D8;               // 0xD8
        float    verticalOffsetNegated;  // 0xDC
        float    float_E0;               // 0xE0
        float    float_E4;               // 0xE4
        float    verticalOffset_E8;      // 0xE8 (+4 bytes alignment pad to 0xF0)
        uint64_t unkF0;                  // 0xF0
        uint64_t unkF8;                  // 0xF8
        uint64_t unk100;
        uint64_t unk108;
        uint64_t unk110;
        uint64_t unk118;
        uint64_t unk120;
        uint64_t unk128;
        uint64_t unk130;
        uint32_t unk138;                 // 0x138
        uint32_t unkZoom_13C;            // 0x13C
        float    timer_140;              // 0x140
        uint32_t unk144;                 // 0x144
        uint64_t unk148;                 // 0x148
        Vector3  cameraPos_150;          // 0x150 - another camera position
        float    prevZoom_15C;           // 0x15C
        float    currentZoom_160;        // 0x160 - current interpolated zoom
        float    currentPitch_164;       // 0x164 - current pitch angle
        uint64_t unk168[15];
        float    unk1E0;
        bool     bool_1E4;
        uint8_t  unk1E5;
        uint16_t unk1E6;
        uint64_t unk1E8;
        uint8_t  unk1F0;
        uint8_t  unk1F1;
        uint16_t unk1F2;
        uint32_t unk1F4;
        uint64_t unk1F8[11];
        uint32_t unk250;
        uint32_t unk254;
    };
    static_assert(offsetof(CameraObject, cameraRootPos) == 0x18);
    static_assert(offsetof(CameraObject, desiredCameraRootPos) == 0x24);
    static_assert(offsetof(CameraObject, desiredZoom) == 0x5C);
    static_assert(offsetof(CameraObject, cameraModeFlags) == 0xA8);
    static_assert(offsetof(CameraObject, unkF0) == 0xF0);
    static_assert(offsetof(CameraObject, unkZoom_13C) == 0x13C);
    static_assert(offsetof(CameraObject, currentZoom_160) == 0x160);

    struct UnkObject
    {
        void*         unk00;
        uint64_t      unk08;
        Player*       currentPlayer;          // 0x10
        uint64_t      unk18;
        void*         unk20;
        void*         unk28;
        CameraObject* currentCameraObject;    // 0x30
        void*         unk38;
        CameraObject* currentCameraObject2;   // 0x40
        uint64_t      unk48;
        void*         unk50;
        // ... more fields
    };

    struct FloorLevelStruct
    {
        float floorLevel;
        bool  unk04;
        bool  unk05;
        uint16_t unk06;
        bool  unk08;
    };
}
