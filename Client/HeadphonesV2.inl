// Included once by Headphones.cpp. V2 owns its wire namespace and never enters
// the V1 serializers/dispatchers. All shared session state is guarded by _propertyMtx.

bool Headphones::_v2SupportsLocked(unsigned char id) const
{
    return _v2Functions[0] && _v2Functions[0]->supports(id);
}

bool Headphones::_v2EqReadyLocked(unsigned char selector) const
{
    const auto cap = _v2EqCapabilities.find(selector);
    const auto available = _v2EqAvailable.find(selector);
    return _conn.getProtocol() == SonyProtocol::V2 && _v2Phase == "ready" &&
        ((selector == 0 && _v2SupportsLocked(0x50)) || (selector == 0x31 && _v2SupportsLocked(0x55))) && cap != _v2EqCapabilities.end() &&
        available != _v2EqAvailable.end() && available->second && _v2EqReports.count(selector) && SonyV2::eqStateFits(cap->second, _v2EqReports.at(selector));
}

V2Snapshot Headphones::getV2Snapshot()
{
    std::lock_guard guard(_propertyMtx);
    V2Snapshot result;
    result.phase = _v2Phase;
    result.protocol = _v2Protocol;
    result.capability = _v2Capability;
    result.functions = _v2Functions;
    for (const auto& [selector, capability] : _v2EqCapabilities) {
        V2EqualizerView view;
        view.capability = capability;
        view.writable = _v2EqReadyLocked(selector);
        if (auto report = _v2EqReports.find(selector); report != _v2EqReports.end()) view.reported = report->second;
        if (auto available = _v2EqAvailable.find(selector); available != _v2EqAvailable.end()) view.available = available->second;
        if (auto value = _v2EqPresets.find(selector); value != _v2EqPresets.end()) {
            view.status = value->second.status;
            view.selectedPreset = view.status == SettingStatus::Requested || view.status == SettingStatus::Acknowledged
                ? value->second.desired : value->second.current;
        }
        if (auto metadata = _v2EqMetadata.find(selector); metadata != _v2EqMetadata.end()) view.metadata = metadata->second;
        view.bandsEditable = _v2CurveReadyLocked(selector);
        if (auto curve = _v2EqCurves.find(selector); curve != _v2EqCurves.end()) {
            view.levels = curve->second.status == SettingStatus::Requested || curve->second.status == SettingStatus::Acknowledged ? curve->second.desired : curve->second.current;
            if (view.status != SettingStatus::Requested && view.status != SettingStatus::Acknowledged && view.status != SettingStatus::Failed)
                view.status = curve->second.status;
        }
        result.equalizers.push_back(std::move(view));
    }
    for (const auto& [selector, cap] : _v2AmbientCapabilities) {
        V2AmbientView view; view.capability = cap; view.writable = _v2AmbientReadyLocked(selector);
        if (auto it = _v2AmbientReports.find(selector); it != _v2AmbientReports.end()) view.reported = it->second;
        if (auto it = _v2AmbientValues.find(selector); it != _v2AmbientValues.end()) {
            view.status = it->second.status;
            view.displayed = view.status == SettingStatus::Requested || view.status == SettingStatus::Acknowledged ? it->second.desired : it->second.current;
        }
        result.ambient.push_back(view);
    }
    result.codec = _v2Codec; result.effect = _v2Effect;
    for (const auto& [selector, cap] : _v2AudioCapabilities) {
        V2AudioView view; view.capability = cap; view.writable = _v2AudioReadyLocked(selector);
        if (auto it = _v2AudioValues.find(selector); it != _v2AudioValues.end()) {
            view.status = it->second.status;
            view.value = view.status == SettingStatus::Requested || view.status == SettingStatus::Acknowledged ? it->second.desired : it->second.current;
        }
        result.audio.push_back(view);
    }
    return result;
}

bool Headphones::setV2EqPreset(unsigned char selector, unsigned char preset)
{
    std::lock_guard guard(_propertyMtx);
    if (selector != 0 || !_v2EqReadyLocked(selector) || !_v2EqCapabilities.at(selector).supports(preset)) return false;
    if (auto curve = _v2EqCurves.find(selector); curve != _v2EqCurves.end() &&
        (curve->second.status == SettingStatus::Requested || curve->second.status == SettingStatus::Acknowledged)) return false;
    _v2EqPresets[selector].setDesired(preset);
    return true;
}

void Headphones::_queryV2State()
{
    unsigned long long generation;
    {
        std::lock_guard guard(_propertyMtx);
        generation = _generation;
        _v2Phase = "protocol";
    }
    auto waitReply = [&](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::unique_lock lock(_propertyMtx);
        while (!predicate()) {
            if (_generation != generation || !_conn.isConnected())
                throw RecoverableException("V2 discovery was disconnected", true);
            if (std::chrono::steady_clock::now() >= deadline) {
                _v2Phase = "reply-timeout";
                throw RecoverableException("The headphones did not return V2 discovery information", true);
            }
            _v2Reply.wait_for(lock, std::chrono::milliseconds(50));
        }
    };
    _conn.sendCommand(SonyV2::queryProtocolInfo());
    waitReply([&] { return _v2Protocol.has_value(); });
    {
        std::lock_guard guard(_propertyMtx);
        if (!SonyV2::protocolVersionSupported(_v2Protocol->version) || !_v2Protocol->table1Enabled) {
            _v2Phase = "unsupported-version";
            return; // retain diagnostic identity, no speculative requests or writes
        }
        _v2Phase = "capability";
    }
    _conn.sendCommand(SonyV2::queryCapabilityInfo());
    waitReply([&] { return _v2Capability.has_value(); });
    {
        std::lock_guard guard(_propertyMtx);
        _v2Phase = "functions";
        _enqueueQueryLocked(SonyV2::queryDeviceInfo(1));
        _enqueueQueryLocked(SonyV2::queryDeviceInfo(2));
        _enqueueQueryLocked(SonyV2::querySupportedFunctions());
        if (_v2Protocol->table2Enabled)
            _enqueueQueryLocked(SonyV2::querySupportedFunctions(), DATA_TYPE::DATA_MDR_NO2);
    }
}

void Headphones::_applyV2(const CommandSerializer::Message& msg)
{
    std::lock_guard guard(_propertyMtx);
    if (auto info = SonyV2::parseProtocolInfo(msg)) {
        if (_v2Phase == "protocol" && !_v2Protocol) { _v2Protocol = *info; _v2Reply.notify_all(); }
        return;
    }
    if (!_v2Protocol || !SonyV2::protocolVersionSupported(_v2Protocol->version)) return;
    if (auto info = SonyV2::parseCapabilityInfo(msg)) {
        if (_v2Phase == "capability" && !_v2Capability) { _v2Capability = *info; _v2Reply.notify_all(); }
        return;
    }
    if (!_v2Capability) return;
    if (auto functions = SonyV2::parseSupportedFunctions(msg)) {
        const size_t table = msg.dataType == DATA_TYPE::DATA_MDR ? 0 : 1;
        if ((table == 0 && !_v2Protocol->table1Enabled) || (table == 1 && !_v2Protocol->table2Enabled)) return;
        if (_v2Functions[table] && *_v2Functions[table] == *functions) return;
        if (table == 0 && _v2Functions[table]) {
            // Feature changes invalidate old availability before any queued write is sent.
            _v2EqAvailable.clear(); _v2AmbientAvailable.clear(); _v2AudioAvailable.clear();
        }
        _v2Functions[table] = *functions;
        if (table == 0) {
            if (auto cap = SonyV2::connectionAudioCapability(*functions)) _v2AudioCapabilities[0] = *cap;
            for (unsigned char selector : {0, 1}) {
                if (!functions->supports(selector == 0 ? SonyV2::ConnectionPreferenceFunction : SonyV2::UpscalingFunction)) continue;
                if (auto query = SonyV2::queryAudioCapability(selector)) _enqueueQueryLocked(*query);
                _enqueueQueryLocked(SonyV2::queryAudioAvailability(selector));
                _enqueueQueryLocked(SonyV2::queryAudioState(selector));
            }
            if (functions->supports(SonyV2::CodecIndicatorFunction)) _enqueueQueryLocked(SonyV2::queryCodecStatus());
            if (functions->supports(SonyV2::UpscalingIndicatorFunction)) _enqueueQueryLocked(SonyV2::queryUpscalingEffect());
            for (auto [function, selector] : {std::pair<unsigned char, unsigned char>{0x64, 0x13}, {0x65, 0x14}, {0x6a, 0x16}, {0x6b, 0x17}, {0x6d, 0x19}}) {
                if (!functions->supports(function)) continue;
                _enqueueQueryLocked(SonyV2Ambient::queryCapability(selector));
                _enqueueQueryLocked(SonyV2Ambient::queryStatus(selector));
                _enqueueQueryLocked(SonyV2Ambient::queryState(selector));
            }
            if (functions->supports(0x20)) _enqueueQueryLocked(SonyV2::queryBattery());
            for (auto [function, selector] : {std::pair<unsigned char, unsigned char>{0x50, 0}, {0x55, 0x31}}) {
                if (!functions->supports(function)) continue;
                _enqueueQueryLocked(SonyV2::queryEqCapability(selector));
                _enqueueQueryLocked(SonyV2::queryEqBandMetadata(selector));
                _enqueueQueryLocked(SonyV2::queryEqAvailability(selector));
                _enqueueQueryLocked(SonyV2::queryEqState(selector));
            }
        }
        if ((!_v2Protocol->table1Enabled || _v2Functions[0]) && (!_v2Protocol->table2Enabled || _v2Functions[1]))
            _v2Phase = "ready";
        return;
    }
    if (auto info = SonyV2::parseDeviceInfo(msg)) {
        (info->type == 1 ? _reportedModel : _firmwareVersion) = info->value;
        return;
    }
    if (auto battery = SonyV2::parseBattery(msg)) {
        if (_v2SupportsLocked(0x20)) { _batteryLevel = battery->level; _batteryCharging = battery->charging; }
        return;
    }
    auto eqSupported = [&](unsigned char selector) { return (selector == 0 && _v2SupportsLocked(0x50)) || (selector == 0x31 && _v2SupportsLocked(0x55)); };
    if (auto cap = SonyV2::parseEqCapability(msg)) {
        if (eqSupported(cap->selector)) {
            _v2EqCapabilities[cap->selector] = *cap;
            // A report may precede its capability. Re-read to validate count/range against it.
            _enqueueQueryLocked(SonyV2::queryEqState(cap->selector));
        }
        return;
    }
    if (auto available = SonyV2::parseEqAvailability(msg)) {
        if (eqSupported(available->selector)) _v2EqAvailable[available->selector] = available->available;
        return;
    }
    if (auto metadata = SonyV2::parseEqBandMetadata(msg)) {
        if (eqSupported(metadata->selector)) _v2EqMetadata[metadata->selector] = *metadata;
        return;
    }
    if (auto state = SonyV2::parseEqState(msg)) {
        auto cap = _v2EqCapabilities.find(state->selector);
        if (!eqSupported(state->selector) || cap == _v2EqCapabilities.end()) return;
        if (state->levels.size() != cap->second.bands ||
            std::any_of(state->levels.begin(), state->levels.end(), [&](auto value) { return value >= cap->second.levelSteps; })) return;
        if (state->preset && *state->preset != 0xff && !cap->second.supports(*state->preset)) return;
        _v2EqReports[state->selector] = *state;
        _v2EqCurves[state->selector].setState(state->levels);
        if (state->preset) _v2EqPresets[state->selector].setState(*state->preset);
        return;
    }
    auto ambientSupported = [&](unsigned char selector) { return (selector == 0x13 && _v2SupportsLocked(0x64)) || (selector == 0x14 && _v2SupportsLocked(0x65)) || (selector == 0x16 && _v2SupportsLocked(0x6a)) || (selector == 0x17 && _v2SupportsLocked(0x6b)) || (selector == 0x19 && _v2SupportsLocked(0x6d)); };
    if (auto cap = SonyV2Ambient::parseCapability(msg)) {
        if (ambientSupported(cap->selector)) {
            _v2AmbientCapabilities[cap->selector] = *cap;
            _enqueueQueryLocked(SonyV2Ambient::queryState(cap->selector));
        }
        return;
    }
    if (auto available = SonyV2Ambient::parseAvailability(msg)) {
        if (ambientSupported(available->selector)) _v2AmbientAvailable[available->selector] = available->available;
        return;
    }
    if (auto state = SonyV2Ambient::parseState(msg)) {
        if (!ambientSupported(state->selector)) return;
        auto cap = _v2AmbientCapabilities.find(state->selector);
        if (cap == _v2AmbientCapabilities.end()) return;
        auto range = cap->second.range(state->asmId);
        if (!range || (state->mode() == SonyV2Ambient::Mode::Ambient && !range->contains(state->level))) return;
        _v2AmbientReports[state->selector] = *state;
        // Adjustment-in-progress is a sensor update, not confirmation of a write.
        if (state->changeStatus == 1) _v2AmbientValues[state->selector].setState(*state);
        return;
    }
    auto audioSupported = [&](unsigned char selector) { return SonyV2::audioSelector(selector) && _v2SupportsLocked(selector == 0 ? SonyV2::ConnectionPreferenceFunction : SonyV2::UpscalingFunction); };
    if (auto cap = SonyV2::parseAudioCapability(msg)) {
        if (audioSupported(cap->selector)) { _v2AudioCapabilities[cap->selector] = *cap; _enqueueQueryLocked(SonyV2::queryAudioState(cap->selector)); }
        return;
    }
    if (auto available = SonyV2::parseAudioAvailability(msg)) {
        if (audioSupported(available->selector)) _v2AudioAvailable[available->selector] = available->available;
        return;
    }
    if (auto state = SonyV2::parseAudioState(msg)) {
        auto cap = _v2AudioCapabilities.find(state->selector);
        if (audioSupported(state->selector) && cap != _v2AudioCapabilities.end() && SonyV2::audioStateFits(cap->second, *state)) {
            _v2AudioReports[state->selector] = *state; _v2AudioValues[state->selector].setState(state->value);
        }
        return;
    }
    if (auto codec = SonyV2::parseCodecStatus(msg)) { if (_v2SupportsLocked(SonyV2::CodecIndicatorFunction)) _v2Codec = *codec; return; }
    if (auto effect = SonyV2::parseUpscalingEffect(msg)) { if (_v2SupportsLocked(SonyV2::UpscalingIndicatorFunction)) _v2Effect = *effect; return; }
    ProtocolTrace::unused(msg);
}

void Headphones::_pushV2Changes()
{
    // Generic V1 properties never enter the V2 serializers.
    std::vector<unsigned char> audioSelectors;
    { std::lock_guard guard(_propertyMtx); for (const auto& [selector, value] : _v2AudioValues) audioSelectors.push_back(selector); }
    for (auto selector : audioSelectors) {
        Property<int>* value;
        { std::lock_guard guard(_propertyMtx); value = &_v2AudioValues.at(selector); }
        if (_sendPending(*value, [&](int desired) {
            std::optional<Buffer> payload;
            { std::lock_guard guard(_propertyMtx); if (_v2AudioReadyLocked(selector)) payload = SonyV2::setAudioState(_v2AudioCapabilities.at(selector), static_cast<unsigned char>(desired)); }
            if (!payload) throw RecoverableException("The V2 audio setting is not available");
            _conn.sendCommand(*payload);
        })) { std::lock_guard guard(_propertyMtx); _enqueueQueryLocked(SonyV2::queryAudioState(selector)); }
    }
    Property<int>* property = nullptr;
    {
        std::lock_guard guard(_propertyMtx);
        auto it = _v2EqPresets.find(0);
        if (it != _v2EqPresets.end()) property = &it->second;
    }
    if (property && _sendPending(*property, [&](int value) {
        std::optional<Buffer> payload;
        {
            std::lock_guard guard(_propertyMtx);
            if (_v2EqReadyLocked(0)) payload = SonyV2::selectEqPreset(_v2EqCapabilities.at(0), static_cast<unsigned char>(value));
        }
        if (!payload) throw RecoverableException("The V2 equalizer is not available");
        _conn.sendCommand(*payload);
    })) {
        std::lock_guard guard(_propertyMtx);
        _enqueueQueryLocked(SonyV2::queryEqState(0));
    }
    std::vector<unsigned char> curves;
    { std::lock_guard guard(_propertyMtx); for (const auto& [selector, value] : _v2EqCurves) curves.push_back(selector); }
    for (auto selector : curves) {
        Property<std::vector<unsigned char>>* curve;
        { std::lock_guard guard(_propertyMtx); curve = &_v2EqCurves.at(selector); }
        if (_sendPending(*curve, [&](const std::vector<unsigned char>& levels) {
            std::optional<Buffer> payload;
            {
                std::lock_guard guard(_propertyMtx);
                if (_v2CurveReadyLocked(selector)) payload = SonyV2::editEqCurve(_v2EqCapabilities.at(selector), levels);
            }
            if (!payload) throw RecoverableException("The V2 equalizer layout is not ready for editing");
            _conn.sendCommand(*payload);
        })) {
            std::lock_guard guard(_propertyMtx);
            _enqueueQueryLocked(SonyV2::queryEqState(selector));
        }
    }
    std::vector<unsigned char> selectors;
    { std::lock_guard guard(_propertyMtx); for (const auto& [selector, value] : _v2AmbientValues) selectors.push_back(selector); }
    for (auto selector : selectors) {
        Property<SonyV2Ambient::State>* ambient;
        { std::lock_guard guard(_propertyMtx); ambient = &_v2AmbientValues.at(selector); }
        if (_sendPending(*ambient, [&](const SonyV2Ambient::State& wanted) {
            std::optional<Buffer> payload;
            {
                std::lock_guard guard(_propertyMtx);
                if (_v2AmbientReadyLocked(selector)) payload = SonyV2Ambient::buildModeSet(_v2AmbientCapabilities.at(selector),
                    _v2AmbientReports.at(selector), wanted.mode(), wanted.level, wanted.asmId, true);
            }
            if (!payload) throw RecoverableException("The V2 ambient setting is not available");
            _conn.sendCommand(*payload);
        })) {
            std::lock_guard guard(_propertyMtx);
            _enqueueQueryLocked(SonyV2Ambient::queryState(selector));
        }
    }
}

bool Headphones::_v2AmbientReadyLocked(unsigned char selector) const
{
    auto cap = _v2AmbientCapabilities.find(selector);
    auto report = _v2AmbientReports.find(selector);
    auto available = _v2AmbientAvailable.find(selector);
    return _conn.getProtocol() == SonyProtocol::V2 && _v2Phase == "ready" &&
        cap != _v2AmbientCapabilities.end() && report != _v2AmbientReports.end() &&
        report->second.changeStatus == 1 && cap->second.range(report->second.asmId) &&
        (report->second.mode() != SonyV2Ambient::Mode::Ambient || cap->second.range(report->second.asmId)->contains(report->second.level)) && available != _v2AmbientAvailable.end() && available->second &&
        ((selector == 0x13 && _v2SupportsLocked(0x64)) || (selector == 0x14 && _v2SupportsLocked(0x65)) || (selector == 0x16 && _v2SupportsLocked(0x6a)) || (selector == 0x17 && _v2SupportsLocked(0x6b)) || (selector == 0x19 && _v2SupportsLocked(0x6d)));
}
bool Headphones::setV2Ambient(unsigned char selector, SonyV2Ambient::Mode mode, int level, bool voice)
{
    std::lock_guard guard(_propertyMtx);
    if (!_v2AmbientReadyLocked(selector)) return false;
    auto payload = SonyV2Ambient::buildModeSet(_v2AmbientCapabilities.at(selector), _v2AmbientReports.at(selector), mode, level, voice ? 1 : 0, true);
    if (!payload) return false;
    (*payload)[0] = 0x67; // GET/SET share this verified parameter representation
    const auto desired = SonyV2Ambient::parseState({DATA_TYPE::DATA_MDR, 0, *payload});
    if (!desired) return false;
    _v2AmbientValues[selector].setDesired(*desired);
    return true;
}

bool Headphones::_v2CurveReadyLocked(unsigned char selector) const
{
    if (!SonyV2::eqSelector(selector) || !_v2EqReadyLocked(selector) || _v2EqCapabilities.at(selector).levelSteps % 2 == 0) return false;
    const auto metadata = _v2EqMetadata.find(selector);
    return metadata != _v2EqMetadata.end() && SonyV2::eqBandMetadataFits(_v2EqCapabilities.at(selector), metadata->second);
}
bool Headphones::setV2EqBand(unsigned char selector, size_t index, int signedStep)
{
    std::lock_guard guard(_propertyMtx);
    if (!_v2CurveReadyLocked(selector)) return false;
    const auto& cap = _v2EqCapabilities.at(selector);
    if (index >= cap.bands) return false;
    auto preset = _v2EqPresets.find(selector);
    if (preset != _v2EqPresets.end() && (preset->second.status == SettingStatus::Requested || preset->second.status == SettingStatus::Acknowledged)) return false;
    const int raw = signedStep + (cap.levelSteps - 1) / 2;
    if (raw < 0 || raw >= cap.levelSteps) return false;
    auto& curve = _v2EqCurves[selector];
    auto levels = curve.status == SettingStatus::Requested || curve.status == SettingStatus::Acknowledged ? curve.desired : _v2EqReports.at(selector).levels;
    if (levels.size() != cap.bands) return false;
    levels[index] = static_cast<unsigned char>(raw);
    curve.setDesired(levels);
    return true;
}

void Headphones::refreshV2Values()
{
    std::lock_guard guard(_propertyMtx);
    if (_conn.getProtocol() != SonyProtocol::V2 || !_v2Capability) return;
    if (_v2SupportsLocked(0x20)) _enqueueQueryLocked(SonyV2::queryBattery());
    for (unsigned char selector : {0, 1}) {
        if (!_v2SupportsLocked(selector == 0 ? SonyV2::ConnectionPreferenceFunction : SonyV2::UpscalingFunction)) continue;
        _enqueueQueryLocked(SonyV2::queryAudioAvailability(selector)); _enqueueQueryLocked(SonyV2::queryAudioState(selector));
    }
    if (_v2SupportsLocked(SonyV2::CodecIndicatorFunction)) _enqueueQueryLocked(SonyV2::queryCodecStatus());
    if (_v2SupportsLocked(SonyV2::UpscalingIndicatorFunction)) _enqueueQueryLocked(SonyV2::queryUpscalingEffect());
    for (const auto& [selector, cap] : _v2EqCapabilities) {
        if (!_v2SupportsLocked(selector == 0 ? 0x50 : 0x55)) continue;
        _enqueueQueryLocked(SonyV2::queryEqAvailability(selector));
        _enqueueQueryLocked(SonyV2::queryEqState(selector));
    }
    for (auto [function, selector] : {std::pair<unsigned char, unsigned char>{0x64, 0x13}, {0x65, 0x14}, {0x6a, 0x16}, {0x6b, 0x17}, {0x6d, 0x19}}) {
        if (!_v2SupportsLocked(function)) continue;
        _enqueueQueryLocked(SonyV2Ambient::queryStatus(selector));
        _enqueueQueryLocked(SonyV2Ambient::queryState(selector));
    }
}

bool Headphones::_v2AudioReadyLocked(unsigned char selector) const
{
    auto cap = _v2AudioCapabilities.find(selector);
    auto report = _v2AudioReports.find(selector);
    auto available = _v2AudioAvailable.find(selector);
    return _conn.getProtocol() == SonyProtocol::V2 && _v2Phase == "ready" && SonyV2::audioSelector(selector) &&
        _v2SupportsLocked(selector == 0 ? SonyV2::ConnectionPreferenceFunction : SonyV2::UpscalingFunction) &&
        cap != _v2AudioCapabilities.end() && report != _v2AudioReports.end() && SonyV2::audioStateFits(cap->second, report->second) &&
        available != _v2AudioAvailable.end() && available->second;
}
bool Headphones::setV2Audio(unsigned char selector, unsigned char value)
{
    std::lock_guard guard(_propertyMtx);
    if (!_v2AudioReadyLocked(selector) || !SonyV2::setAudioState(_v2AudioCapabilities.at(selector), value)) return false;
    _v2AudioValues[selector].setDesired(value); return true;
}
