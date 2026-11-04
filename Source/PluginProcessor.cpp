#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several SeaSick windows can be open at once. Footswitches drive one of them at a time:
// the one whose panel was last touched, or the next one along when C+D are tapped.
namespace
{
SpinLock focusLock;
Array<SeaSickProcessor*> instances;
std::atomic<SeaSickProcessor*> focused { nullptr };

// set while a message from the footswitch device is being handled on the MIDI thread
thread_local bool fromDeviceNow = false;

// footswitch device messages go to the copy that is learning, else the copy in charge
void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    SeaSickProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}
} // namespace

SeaSickProcessor::SeaSickProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SeaSickState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

SeaSickProcessor::~SeaSickProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void SeaSickProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keeper.prepare (sampleRate);
    setLatencySamples (engine.latencySamples());   // Punch's 1 ms lookahead, always in the line
}

bool SeaSickProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

ss::EngineParams SeaSickProcessor::readParams()
{
    using namespace ParamIDs;
    ss::EngineParams p;
    p.rate = f (rate);
    p.damp = f (damp);
    p.depth = f (depth);
    p.lpg = f (lpg);
    p.mix = f (mix);
    p.volume = f (volume);
    p.floor = f (lpgFloor);
    p.input = idx (input);
    p.splitHz = f (split);
    p.ceilingCents = f (ceiling);
    p.punch = flag (punch);
    p.clean = flag (clean);
    p.active = ! flag (bypass);
    p.seasick = flag (seasick);
    p.swell = flag (swell);
    p.freeze = flag (freeze);
    p.freezeMode = idx (freezeMode);
    p.pluck = flag (pluck);
    return p;
}

void SeaSickProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    for (const auto meta : midi) handleMidi (meta.getMessage(), false);

    const auto p = readParams();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    keeper.pushInput (L, R, n);
    engine.process (L, R, L, R, n, p);
    keeper.pushOutput (L, R, n);
}

// =====================================================================================
// MIDI: footswitches, expression and Keeper. The pedal itself has no MIDI, so there is no
// original CC chart to collide with the footswitch and expression defaults.
// =====================================================================================
void SeaSickProcessor::queue (int kind, int a)
{
    if (fromDeviceNow)
    {
        const auto scope = directFifo.write (1);
        if (scope.blockSize1 > 0) directPending[(size_t) scope.startIndex1] = { kind, a };
        return;
    }
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = { kind, a };
}

void SeaSickProcessor::handleDirectMidi (const MidiMessage& m)
{
    // several MIDI devices can call in on their own threads
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void SeaSickProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setWanted (device);
}

void SeaSickProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    if (! m.isController()) return;
    const int ch = m.getChannel(), cc = m.getControllerNumber(), val = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc);
        return;
    }

    // Straight from the footswitch device, any channel will do: it's the footswitch's own
    // cable, and some footswitches ignore their channel setting. Through Ableton, the
    // channel must match. While the device is read directly, the same messages arriving
    // through Ableton are ignored, so nothing happens twice.
    if (! fromDevice && pedalsDirect()) return;

    int foot = -1;
    for (int k = 0; k < 4 && foot < 0; ++k)
        if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
    const bool keep = foot < 0 && settings.keeperCC >= 0 && (fromDevice || ch == settings.keeperChannel) && cc == settings.keeperCC;
    const bool expression = foot < 0 && ! keep && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
    if (! (foot >= 0 || keep || expression)) return;   // anything else, sustain pedal included, is ignored

    if (! fromDevice)
    {
        // The copy Ableton sends footswitch MIDI to takes charge, unless the copy in
        // charge is fed too. A newly loaded copy with no MIDI track can't hold the pedals.
        hostFeedsPedals.store (true);
        const SpinLock::ScopedLockType l (focusLock);
        auto* fc = focused.load();
        if (fc != this && (fc == nullptr || ! fc->hostFeedsPedals.load())) focused.store (this);
        if (focused.load() != this) return;
    }
    if (foot >= 0) queue (val >= 64 ? FootPress : FootRelease, foot);
    else if (keep) { if (val >= 64) queue (KeepCC, 0); }
    else queue (Expression, val);
}

void SeaSickProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        v01 = jlimit (0.0f, 1.0f, v01);
        if (std::abs (p->getValue() - v01) < 1.0e-5f) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (v01);
        p->endChangeGesture();
    }
}

void SeaSickProcessor::setPlain (const String& id, float plain)
{
    if (auto* p = apvts.getParameter (id)) setNorm (id, p->convertTo0to1 (plain));
}

void SeaSickProcessor::say (const String& s)
{
    uiMessage = s;
    uiMessageAt = Time::getMillisecondCounterHiRes() * 0.001;
}

void SeaSickProcessor::timerCallback() { pump (Time::getMillisecondCounterHiRes() * 0.001); }

void SeaSickProcessor::pump (double now)
{
    clockNow = now;
    // settings changed by another copy, or another Flip Fella plugin: pick them up
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && focused.load() == this) say ("FOOTSWITCHES READY");
        }
    }

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case KeepCC: keepNow(); break;
                case Expression:
                {
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = q.a;
                        calLo = jmin (calLo, q.a);
                        calHi = jmax (calHi, q.a);
                        uiExp.store ((float) q.a / 127.0f);
                        break;
                    }
                    expTargetValue = settings.expressionValue (q.a);
                    if (expSmoothed < 0.0f) expSmoothed = expTargetValue;
                    break;
                }
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " IS CC " + String (cc));
                    }
                    else if (what == LearnKeeper)
                    {
                        settings.keeperChannel = ch;
                        settings.keeperCC = cc;
                        say ("KEEPER IS CC " + String (cc));
                    }
                    else if (what == LearnExpression)
                    {
                        settings.expChannel = ch;
                        settings.expCC = cc;
                        say ("EXPRESSION IS CC " + String (cc));
                    }
                    settings.save();
                    settingsStamp = PedalSettings::file().getLastModificationTime();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    // a remote controller pressing the footswitches through their parameters
    for (int k = 0; k < 4; ++k)
    {
        const bool down = flag (ParamIDs::foot[k]);
        if (down != remoteFoot[(size_t) k])
        {
            remoteFoot[(size_t) k] = down;
            if (down) footswitches.press (k, now);
            else footswitches.release (k, now);
        }
    }
    if (flag (ParamIDs::keeper)) { setPlain (ParamIDs::keeper, 0.0f); keepNow(); }
    if (flag (ParamIDs::calm)) { setPlain (ParamIDs::calm, 0.0f); calm(); }
    if (flag (ParamIDs::reroll)) { setPlain (ParamIDs::reroll, 0.0f); reroll(); }

    // expression, smoothed
    if (expTargetValue >= 0.0f)
    {
        const float alpha = jmap (f (ParamIDs::expSmooth), 1.0f, 0.04f);
        expSmoothed += (expTargetValue - expSmoothed) * alpha;
        const bool settled = std::abs (expTargetValue - expSmoothed) < 0.0005f;
        if (settled) expSmoothed = expTargetValue;
        applyExpression (expSmoothed);
        if (settled) expTargetValue = -1.0f;   // settled: let the knob be moved by hand
    }

    footswitches.tick (now);
    if (rerollAt >= 0.0 && now >= rerollAt) { rerollAt = -1.0; reroll(); }
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            // whichever end the pedal started at is the heel
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            settings.save();
            settingsStamp = PedalSettings::file().getLastModificationTime();
            say ("EXPRESSION CALIBRATED");
        }
        else
            say ("NO MOVEMENT, TRY AGAIN");
    }
}

void SeaSickProcessor::applyExpression (float v)
{
    uiExp.store (v);
    const float x = jmap (v, f (ParamIDs::expMin), f (ParamIDs::expMax));
    const auto& targets = names::expTargets();
    const int t = jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget));
    if (t == names::ExpDepth) setPlain (ParamIDs::depth, expressionToDepth (x));
    else setNorm (targets[(size_t) t].id, x);
}

// =====================================================================================
// Footswitches. The pedal has one (bypass), so A is that. B is Seasick (tap to latch, hold
// to swell), C freezes the water, D re-rolls it (hold for Pluck). A+B calms everything,
// C+D passes the footswitches on, and holding C+D for a second keeps.
// =====================================================================================
void SeaSickProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD) uiFootPulse.fetch_add (1);

    switch (e)
    {
        case E::ComboHoldCD:
            focused.store (this);   // Keeper on the plugin you were playing
            keepNow();
            return;
        case E::ComboCD:
        {
            if (cDownToggled) { toggle (ParamIDs::freeze); cDownToggled = false; }   // C was half of the pair
            rerollAt = -1.0;                                                          // and so was D
            const SpinLock::ScopedLockType l (focusLock);
            const int i = instances.indexOf (this);
            auto* next = instances.isEmpty() ? nullptr : instances[(i + 1) % instances.size()];
            focused.store (next);
            if (next != nullptr) next->say ("FOOTSWITCHES HERE");
            return;
        }
        case E::ComboAB:
            if (aDownToggled) { toggle (ParamIDs::bypass); aDownToggled = false; }   // A was half of the pair
            calm();
            return;
        case E::ComboHoldAB:
            return;
        default: break;
    }

    if (sw == 0)   // A: bypass, the pedal's own switch. Acts on the press; held, it's momentary
    {
        if (e == E::Down) { toggle (ParamIDs::bypass); aDownToggled = true; say (flag (ParamIDs::bypass) ? "BYPASSED" : "ON"); }
        else if (e == E::Tap) aDownToggled = false;
        else if (e == E::HoldEnd) { toggle (ParamIDs::bypass); aDownToggled = false; }
    }
    else if (sw == 1)   // B: Seasick. Tap latches it; hold swells into it and back
    {
        if (e == E::Tap) { toggle (ParamIDs::seasick); say (flag (ParamIDs::seasick) ? "SEASICK" : "BACK ON DRY LAND"); }
        else if (e == E::HoldStart) { setPlain (ParamIDs::swell, 1.0f); say ("SWELL"); }
        else if (e == E::HoldEnd) setPlain (ParamIDs::swell, 0.0f);
    }
    else if (sw == 2)   // C: Freeze on the press; held, only while held
    {
        if (e == E::Down)
        {
            toggle (ParamIDs::freeze);
            cDownToggled = true;
            cTurnedOn = flag (ParamIDs::freeze);
            say (cTurnedOn ? "FROZEN" : "FLOWING");
        }
        else if (e == E::Tap) cDownToggled = false;
        else if (e == E::HoldEnd) { if (cTurnedOn) setPlain (ParamIDs::freeze, 0.0f); cDownToggled = cTurnedOn = false; }
    }
    else if (sw == 3)   // D: Re-roll, once the pair window has passed; hold for Pluck
    {
        if (e == E::Down) rerollAt = clockNow + FootswitchLogic::kComboWindow;
        else if (e == E::HoldStart) { setPlain (ParamIDs::pluck, 1.0f); say ("PLUCK"); }
        else if (e == E::HoldEnd) setPlain (ParamIDs::pluck, 0.0f);
    }
}

void SeaSickProcessor::footDown (int sw)
{
    focusHere();
    clockNow = Time::getMillisecondCounterHiRes() * 0.001;
    footswitches.press (sw, clockNow);
}

void SeaSickProcessor::footUp (int sw)
{
    clockNow = Time::getMillisecondCounterHiRes() * 0.001;
    footswitches.release (sw, clockNow);
}

void SeaSickProcessor::focusHere() { focused.store (this); }

void SeaSickProcessor::calm()
{
    for (auto* id : { ParamIDs::seasick, ParamIDs::swell, ParamIDs::freeze, ParamIDs::pluck }) setPlain (id, 0.0f);
    uiCalmPulse.fetch_add (1);
    say ("CALM");
}

void SeaSickProcessor::reroll()
{
    engine.reroll();
    say ("RE-ROLL");
}

// =====================================================================================
// Footswitch learn and expression calibration
// =====================================================================================
void SeaSickProcessor::learnFootswitch (int sw)
{
    learnTarget.store (sw);
    say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw)));
}

void SeaSickProcessor::learnExpression()
{
    learnTarget.store (LearnExpression);
    say ("MOVE THE EXPRESSION PEDAL");
}

void SeaSickProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = Time::getMillisecondCounterHiRes() * 0.001 + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void SeaSickProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

void SeaSickProcessor::setCurve (int c)
{
    settings.curve = jlimit (0, 2, c);
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

// =====================================================================================
// Keeper
// =====================================================================================
void SeaSickProcessor::keepNow()
{
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "SeaSick");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("recreates", "Fairfield Circuitry Shallow Water K-Field Modulator");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    e->setProperty ("preset", "none");
    double bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    e->setProperty ("tempo", bpm);
    e->setProperty ("tempoFromHost", bpm > 0.0);

    // what you'd hear described: the moment, in words
    const float wobble = engine.uiWobble.load();
    e->setProperty ("seaState", names::seaState (wobble));
    e->setProperty ("wobbleCents", wobble);
    e->setProperty ("bendNowCents", engine.uiCents.load());
    e->setProperty ("gateOpen", engine.uiGate.load());
    e->setProperty ("gateCutoffHz", engine.uiCutoff.load());
    e->setProperty ("seasickAmount", engine.uiSeasick.load());
    e->setProperty ("frozen", engine.uiFrozen.load());

    DynamicObject::Ptr exp = new DynamicObject();
    const auto& targets = names::expTargets();
    exp->setProperty ("target", targets[(size_t) jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget))].label);
    exp->setProperty ("position", uiExp.load());
    exp->setProperty ("movedSecondsAgo", Time::getMillisecondCounterHiRes() * 0.001 - expMovedAt);
    e->setProperty ("expression", var (exp.get()));

    // every parameter, hidden ones included, as value and as shown on the panel
    DynamicObject::Ptr params = new DynamicObject();
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
        {
            if (rp->getParameterID() == ParamIDs::keeper) continue;
            DynamicObject::Ptr pv = new DynamicObject();
            pv->setProperty ("value", rp->convertFrom0to1 (rp->getValue()));
            pv->setProperty ("shown", rp->getCurrentValueAsText());
            params->setProperty (rp->getParameterID(), var (pv.get()));
        }
    e->setProperty ("parameters", var (params.get()));

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File SeaSickProcessor::exportKeepers()
{
    const auto file = keeper.exportZip();
    if (file.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); file.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return file;
}

void SeaSickProcessor::openKeepersFolder()
{
    Keeper::folder().createDirectory();
    Keeper::folder().startAsProcess();
}

void SeaSickProcessor::learnKeeper()
{
    learnTarget.store (LearnKeeper);
    say ("PRESS YOUR KEEPER SWITCH");
}

void SeaSickProcessor::setKeeperLive (bool live)
{
    settings.keeperLive = live;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

// =====================================================================================
// State
// =====================================================================================
void SeaSickProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void SeaSickProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
    // nothing held down or frozen survives a reload
    for (auto* id : { ParamIDs::foot[0], ParamIDs::foot[1], ParamIDs::foot[2], ParamIDs::foot[3],
                      ParamIDs::keeper, ParamIDs::calm, ParamIDs::reroll, ParamIDs::swell, ParamIDs::pluck, ParamIDs::freeze })
        setPlain (id, 0.0f);
}

AudioProcessorEditor* SeaSickProcessor::createEditor() { return new SeaSickEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SeaSickProcessor(); }
