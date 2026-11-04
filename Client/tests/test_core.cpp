#include "TestHarness.h"
#include "TestSupport.h"
#include "AsyncTask.h"
#include "BluetoothAddress.h"
#include "PacketCodec.h"
#include "CommandSerializer.h"
#include <memory>

using replies::hex;

TEST(core_bluetooth_address_format_and_validation)
{
    CHECK_EQ(BluetoothAddress::parse("01:23:45:67:89:AB"), 0x0123456789abULL);
    CHECK_EQ(BluetoothAddress::parse("ff-EE-dd-CC-bb-AA"), 0xffeeddccbbaaULL);
    CHECK_EQ(BluetoothAddress::format(0x0123456789ab), std::string("01:23:45:67:89:ab"));
    for (const auto* bad : {"", "01:23:45:67:89", "01:23:45:67:89:ab:cd", "01:23:45:67:89:gg",
                           "01:23-45:67:89:ab", "+1:23:45:67:89:ab", "01:23:45:67:89:a ", "01 23 45 67 89 ab"})
        CHECK_THROWS(BluetoothAddress::parse(bad), std::invalid_argument);
    CHECK_THROWS(BluetoothAddress::format(1ULL << 48), std::invalid_argument);
    for (unsigned value = 0; value != 256; ++value)
    {
        const auto address = 0x010101010101ULL * value;
        CHECK_EQ(BluetoothAddress::parse(BluetoothAddress::format(address)), address);
    }
}

TEST(core_async_task_retains_an_unconsumed_result)
{
    AsyncTask<int> task;
    CHECK(!task.valid());
    CHECK(!task.ready());
    std::promise<void> release;
    task.setFromAsync([gate = release.get_future()]() mutable { gate.get(); return 42; });
    const bool pending = task.valid() && !task.ready();
    bool rejected = false;
    try { task.setFromAsync([] { return 99; }); }
    catch (const std::logic_error&) { rejected = true; }
    release.set_value();
    CHECK_EQ(task.get(), 42);
    CHECK(pending);
    CHECK(rejected);
    CHECK(!task.valid());
    task.setFromAsync([](std::unique_ptr<int> n) { return *n; }, std::make_unique<int>(7));
    CHECK_EQ(task.get(), 7);
}

TEST(core_async_task_propagates_errors_and_can_be_reused)
{
    AsyncTask<void> task;
    task.setFromAsync([] { throw std::runtime_error("operation failed"); });
    CHECK_THROWS(task.get(), std::runtime_error);
    CHECK(!task.valid());
    int result = 0;
    task.setFromAsync([](int& value) { value = 8; }, std::ref(result));
    task.get();
    CHECK_EQ(result, 8);
}

TEST(core_packet_codec_matches_wire_examples)
{
    CHECK(SonyFrame::encode(hex("10 00"), DATA_TYPE::DATA_MDR, 0) == hex("3e 0c 00 00 00 00 02 10 00 1e 3c"));
    CHECK(SonyFrame::encode(hex("3c 3d 3e"), DATA_TYPE::DATA_MDR, 0) == hex("3e 0c 00 00 00 00 03 3d 2c 3d 2d 3d 2e c6 3c"));
    const auto decoded = SonyFrame::decode(hex("0c 00 00 00 00 03 3d 2c 3d 2d 3d 2e c6"));
    CHECK(decoded.type == DATA_TYPE::DATA_MDR);
    CHECK_EQ(decoded.sequence, 0);
    CHECK(decoded.payload == hex("3c 3d 3e"));
}

TEST(core_packet_codec_rejects_damaged_frames)
{
    CHECK_THROWS(SonyFrame::decode(hex("0c 00 00 00 00 02 10 00 1f")), RecoverableException);
    CHECK_THROWS(SonyFrame::decode(hex("0c 00 00 00 00 03 10 00 1f")), RecoverableException);
    CHECK_THROWS(SonyFrame::decode(hex("0c 00")), std::runtime_error);
    CHECK_THROWS(SonyFrame::unescape(hex("3d")), std::runtime_error);
    CHECK_THROWS(SonyFrame::unescape(hex("3d 00")), std::runtime_error);
    CHECK_THROWS(SonyFrame::encode(Buffer(1020, 0x3d), DATA_TYPE::DATA_MDR, 0), std::runtime_error);
}

TEST(core_packet_codec_handles_every_byte_and_multibyte_lengths)
{
    Buffer payload;
    for (int i = 0; i != 512; ++i) payload.push_back(static_cast<char>(i));
    auto frame = SonyFrame::encode(payload, DATA_TYPE::DATA_MDR_NO2, 1);
    auto decoded = SonyFrame::decode(std::span<const char>(frame).subspan(1, frame.size() - 2));
    CHECK(decoded.payload == payload);
    CHECK(decoded.type == DATA_TYPE::DATA_MDR_NO2);
    CHECK_EQ(decoded.sequence, 1);
}

TEST(core_ambient_serializer_preserves_wire_modes)
{
    using namespace CommandSerializer;
    CHECK(serializeNcAndAsmSetting(NC_ASM_EFFECT::ADJUSTMENT_COMPLETION, NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
        ASM_SETTING_TYPE::LEVEL_ADJUSTMENT, ASM_ID::NORMAL, 0) == hex("68 02 11 01 02 01 00 00"));
    CHECK(serializeNcAndAsmSetting(NC_ASM_EFFECT::OFF, NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
        ASM_SETTING_TYPE::LEVEL_ADJUSTMENT, ASM_ID::NORMAL, static_cast<char>(0xff)) == hex("68 02 00 01 00 01 00 ff"));
    CHECK(serializeVPTSetting(VPT_INQUIRED_TYPE::SOUND_POSITION, 17) == hex("48 02 11"));
    CHECK_THROWS(getDualSingleForAsmLevel(20), std::invalid_argument);
}

TEST(core_feature_commands_match_packet_layouts)
{
    using namespace CommandSerializer;
    CHECK(serializeXM4OptimizeCommand(OPTIMIZER_STATE::OPTIMIZING) == hex("84 01 00 01"));
    CHECK(serializeXM4SpeakToChat(S2C_TOGGLE::ACTIVE) == hex("f8 05 01 01"));
    CHECK(serializeXM4_S2C_Options(2, 1, 3) == hex("fc 05 00 02 01 03"));
    const auto command = serializeMultiPointCommand(static_cast<MULTI_POINT_COMMANDS>(1), "01:23:45:67:89:ab");
    CHECK(Buffer(command.begin(), command.begin() + 4) == hex("3c 01 00 01"));
    CHECK(std::string(command.begin() + 4, command.end()) == "01:23:45:67:89:ab");
}
