#include "BluetoothWrapper.h"
#include <utility>
BluetoothWrapper::BluetoothWrapper(std::unique_ptr<IBluetoothConnector> connector)
    : _connector(std::move(connector))
{
    if (!_connector) throw std::invalid_argument("Bluetooth connector is required");
}
int BluetoothWrapper::writeFrame(Buffer frame)
{
    size_t offset=0;
    while (offset<frame.size())
    {
        int count=_connector->send(frame.data()+offset,frame.size()-offset);
        if (count<=0 || static_cast<size_t>(count)>frame.size()-offset)
        {
            _connector->disconnect();
            throw ConnectionClosedException("Frame write failed");
        }
        offset+=static_cast<size_t>(count);
    }
    return static_cast<int>(offset);
}
CommandSerializer::Message BluetoothWrapper::readFrame()
{
    Buffer frame;
    bool started=false;
    for (;;)
    {
        char byte;
        if (_connector->recv(&byte,1)!=1) throw ConnectionClosedException("Connection closed during frame read");
        if (byte==START_MARKER) { started=true; frame.clear(); continue; }
        if (!started) continue;
        if (byte==END_MARKER) return CommandSerializer::unpackBtMessage(frame);
        frame.push_back(byte);
        if (frame.size()+2>MAX_BLUETOOTH_MESSAGE_SIZE) throw RecoverableException("Frame is too large",true);
    }
}
int BluetoothWrapper::sendCommand(const Buffer& bytes)
{
    std::lock_guard lock(_mutex);
    int count=writeFrame(CommandSerializer::packageDataForBt(bytes,DATA_TYPE::DATA_MDR,_sequence));
    for (;;)
    {
        auto reply=readFrame();
        if (reply.dataType==DATA_TYPE::ACK && reply.seqNumber==(_sequence^1))
        {
            _sequence=reply.seqNumber;
            return count;
        }
        if (reply.dataType==DATA_TYPE::DATA_MDR || reply.dataType==DATA_TYPE::DATA_MDR_NO2)
            writeFrame(CommandSerializer::packageDataForBt({},DATA_TYPE::ACK,reply.seqNumber^1));
    }
}
CommandSerializer::Message BluetoothWrapper::readMessage() { std::lock_guard lock(_mutex); return readFrame(); }
void BluetoothWrapper::sendAck(unsigned char sequence) { std::lock_guard lock(_mutex); writeFrame(CommandSerializer::packageDataForBt({},DATA_TYPE::ACK,sequence^1)); }
bool BluetoothWrapper::isConnected() noexcept { return _connector->isConnected(); }
void BluetoothWrapper::connect(const std::string& address,SonyProtocol protocol) { std::lock_guard lock(_mutex); _sequence=0; _protocol=protocol; _connector->connect(address,protocol); }
void BluetoothWrapper::disconnect() noexcept { _connector->disconnect(); }
std::vector<BluetoothDevice> BluetoothWrapper::getConnectedDevices() { return _connector->getConnectedDevices(); }
