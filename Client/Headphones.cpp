#include "Headphones.h"
#include "CommandSerializer.h"

#include <stdexcept>

Headphones::Headphones(BluetoothWrapper& conn) : _conn(conn)
{
}

void Headphones::setAmbientSoundControl(bool value)
{
    std::scoped_lock lock(_propertyMtx);
    _ambientSoundControl.desired = value;
}

bool Headphones::getAmbientSoundControl()
{
    std::scoped_lock lock(_propertyMtx);
    return _ambientSoundControl.current;
}

bool Headphones::isFocusOnVoiceAvailable()
{
    std::scoped_lock lock(_propertyMtx);
    return _asmLevel.current > MINIMUM_VOICE_FOCUS_STEP && _ambientSoundControl.current;
}

void Headphones::setFocusOnVoice(bool value)
{
    std::scoped_lock lock(_propertyMtx);
    _focusOnVoice.desired = value;
}

bool Headphones::getFocusOnVoice()
{
    std::scoped_lock lock(_propertyMtx);
    return _focusOnVoice.current;
}

bool Headphones::isSetAsmLevelAvailable()
{
    return getAmbientSoundControl();
}

void Headphones::setAsmLevel(int value)
{
    std::scoped_lock lock(_propertyMtx);
    _asmLevel.desired = value;
}

int Headphones::getAsmLevel()
{
    std::scoped_lock lock(_propertyMtx);
    return _asmLevel.current;
}

void Headphones::setSurroundPosition(SOUND_POSITION_PRESET value)
{
    std::scoped_lock lock(_propertyMtx);
    _surroundPosition.desired = value;
}

SOUND_POSITION_PRESET Headphones::getSurroundPosition()
{
    std::scoped_lock lock(_propertyMtx);
    return _surroundPosition.current;
}

void Headphones::setVptType(int value)
{
    std::scoped_lock lock(_propertyMtx);
    _vptType.desired = value;
}

int Headphones::getVptType()
{
    std::scoped_lock lock(_propertyMtx);
    return _vptType.current;
}

bool Headphones::isChanged()
{
	return !(this->_ambientSoundControl.isFulfilled() && this->_asmLevel.isFulfilled() && this->_focusOnVoice.isFulfilled() && this->_surroundPosition.isFulfilled() && this->_vptType.isFulfilled());
}

void Headphones::setChanges()
{
    {
        bool enabled, voice, pending;
        int level;
        {
            std::lock_guard lock(_propertyMtx);
            pending = !_ambientSoundControl.isFulfilled() || !_focusOnVoice.isFulfilled() || !_asmLevel.isFulfilled();
            enabled = _ambientSoundControl.desired;
            voice = _focusOnVoice.desired;
            level = _asmLevel.desired;
        }
        if (pending)
        {
            _conn.sendCommand(CommandSerializer::serializeNcAndAsmSetting(
                enabled ? NC_ASM_EFFECT::ADJUSTMENT_COMPLETION : NC_ASM_EFFECT::OFF,
                NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT, ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
                voice ? ASM_ID::VOICE : ASM_ID::NORMAL, static_cast<char>(enabled ? level : 0xff)));
            std::lock_guard lock(_propertyMtx);
            _ambientSoundControl.current = enabled;
            _focusOnVoice.current = voice;
            _asmLevel.current = level;
        }
    }

    {
        bool pending;
        int effect;
        SOUND_POSITION_PRESET position, previousPosition;
        {
            std::lock_guard lock(_propertyMtx);
            pending = !_vptType.isFulfilled() || !_surroundPosition.isFulfilled();
            effect = _vptType.desired;
            position = _surroundPosition.desired;
            previousPosition = _surroundPosition.current;
        }
        if (pending)
        {
            const bool usePosition = effect == 0 &&
                (position != SOUND_POSITION_PRESET::OFF || previousPosition != SOUND_POSITION_PRESET::OFF);
            _conn.sendCommand(CommandSerializer::serializeVPTSetting(
                usePosition ? VPT_INQUIRED_TYPE::SOUND_POSITION : VPT_INQUIRED_TYPE::VPT,
                static_cast<unsigned char>(usePosition ? static_cast<int>(position) : effect)));
            std::lock_guard lock(_propertyMtx);
            _vptType.current = effect;
            _surroundPosition.current = position;
        }
    }
}
