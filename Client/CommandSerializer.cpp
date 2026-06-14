#include "PacketCodec.h"
#include <algorithm>
#include "CommandSerializer.h"


namespace CommandSerializer
{
    Buffer _escapeSpecials(const Buffer& src) { return SonyFrame::escape(src); }

    Buffer _unescapeSpecials(const Buffer& src) { return SonyFrame::unescape(src); }

    unsigned char _sumChecksum(const char* src, size_t size) { return SonyFrame::checksum({src, size}); }

    unsigned char _sumChecksum(const Buffer& src) { return SonyFrame::checksum(src); }

    Buffer packageDataForBt(const Buffer& src, DATA_TYPE type, unsigned int sequence)
    {
        return SonyFrame::encode(src, type, static_cast<unsigned char>(sequence));
    }

    Message unpackBtMessage(const Buffer& src)
    {
        auto frame = SonyFrame::decode(src);
        return {frame.type, frame.sequence, std::move(frame.payload)};
    }

    NC_DUAL_SINGLE_VALUE getDualSingleForAsmLevel(char level)
    {
        // 0xff disables ambient processing; 0 and 1 select the two ANC modes.
        const auto value = static_cast<unsigned char>(level);
        if (value != 0xff && value > 19) throw std::invalid_argument("Ambient level must be 0-19 or disabled");
        switch (value)
        {
        case 0: return NC_DUAL_SINGLE_VALUE::DUAL;
        case 1: return NC_DUAL_SINGLE_VALUE::SINGLE;
        default: return NC_DUAL_SINGLE_VALUE::OFF;
        }
    }

    Buffer serializeNcAndAsmSetting(NC_ASM_EFFECT effect, NC_ASM_SETTING_TYPE ncType,
        ASM_SETTING_TYPE ambientType, ASM_ID focus, char level)
    {
        return {0x68, 0x02, static_cast<char>(effect), static_cast<char>(ncType),
            static_cast<char>(getDualSingleForAsmLevel(level)), static_cast<char>(ambientType),
            static_cast<char>(focus), level};
    }

    Buffer serializeVPTSetting(VPT_INQUIRED_TYPE type, unsigned char preset)
    {
        return {0x48, static_cast<char>(type), static_cast<char>(preset)};
    }

}

