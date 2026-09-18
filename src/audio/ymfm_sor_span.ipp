// SoR port: channel-major rendering of FM spans with constant registers.
// Spliced into the staged ymfm_fm.ipp by tools/audio_patches.py.
//
// ymfm clocks every channel and then outputs every channel, once per sample.
// Between register writes and periodic prepares, channels share only the
// envelope counter and LFO. Those are recorded per sample first; each channel
// then runs its whole span with its operator state hot. Integer accumulation
// is order independent, so output matches the per-sample path exactly.

// clock_fast() for one span sample, with its common case inline: no SSG-EG
// and no envelope step due, so only the phase advances.
template<class RegisterType>
template<bool Envelope>
inline __attribute__((always_inline)) void fm_operator<RegisterType>::sor_span_step(uint32_t env_counter, int32_t lfo_raw_pm)
{
    if (m_sor_ssg || m_ssg_inverted ||
        (Envelope && !m_sor_env_stable && !((env_counter >> 2) & m_sor_env_mask)))
    {
        clock_fast<Envelope>(env_counter, lfo_raw_pm);
        return;
    }
    // clock_phase(), inline
    uint32_t phase_step = m_cache.phase_step;
    if (phase_step == opdata_cache::PHASE_STEP_DYNAMIC)
        phase_step = m_regs.compute_phase_step(m_choffs, m_opoffs, m_cache, lfo_raw_pm);
    m_phase += phase_step;
}

// True when every operator needs only its phase advance and ordinary
// envelope steps: no SSG-EG, inversion or LFO PM. Those states cannot begin
// inside a constant-register span.
template<class RegisterType>
bool fm_channel<RegisterType>::sor_light() const
{
    for (auto *op : m_op)
        if (op->m_sor_ssg || op->m_ssg_inverted || op->m_cache.phase_step == opdata_cache::PHASE_STEP_DYNAMIC)
            return false;
    return true;
}

// n samples of fm_channel::clock_fast() and output_4op_fixed<Algorithm> with
// operator state held in locals. Envelope steps fall on samples whose counter
// has low bits 0 and are due when (T & mask) == 0; the stepped operator's
// cached attenuation and power table are then reloaded. Arithmetic is identical.
template<class RegisterType>
template<int Algorithm, bool AM, bool Output>
uint32_t fm_channel<RegisterType>::sor_render_fast(int32_t *__restrict acc, uint32_t n, uint32_t env, const uint8_t *am)
{
    static_assert(RegisterType::WAVEFORMS == 1 && Algorithm >= 0 && Algorithm < 8);
    static_assert(RegisterType::EG_CLOCK_DIVIDER == 3);
    // s_algorithm_ops from output_4op(): op2in | op3in << 1 | op4in << 4 | op1out << 7 | op2out << 8 | op3out << 9
    constexpr auto encode = [](uint32_t op2in, uint32_t op3in, uint32_t op4in, uint32_t op1out, uint32_t op2out, uint32_t op3out)
        { return op2in | (op3in << 1) | (op4in << 4) | (op1out << 7) | (op2out << 8) | (op3out << 9); };
    constexpr uint32_t table[8] = {encode(1,2,3, 0,0,0), encode(0,5,3, 0,0,0), encode(0,2,6, 0,0,0), encode(1,0,7, 0,0,0),
                                   encode(1,0,3, 0,1,0), encode(1,1,1, 0,1,1), encode(1,0,0, 0,1,1), encode(0,0,0, 1,1,1)};
    constexpr uint32_t ops = table[Algorithm];
    constexpr int32_t rshift = 5, clipmax = 256, clipmin = -clipmax - 1;
    struct Lane { fm_operator<RegisterType> *op; const int16_t *power; uint32_t mask; bool quiet, stepping; };
    Lane lane[4];
    auto reload = [&](Lane &l)
    {
        l.power = l.op->m_sor_power;
        // compute_volume() early-out
        l.quiet = l.op->m_env_attenuation > l.op->EG_QUIET || l.op->m_sor_attenuation >= 832;
        l.stepping = !l.op->m_sor_env_stable;
        l.mask = l.op->m_sor_env_mask;
    };
    for (unsigned k = 0; k < 4; k++) { lane[k].op = m_op[k]; reload(lane[k]); }
    const uint16_t *const wave = m_op[0]->m_cache.waveform;
    uint32_t ph0 = m_op[0]->m_phase, ph1 = m_op[1]->m_phase, ph2 = m_op[2]->m_phase, ph3 = m_op[3]->m_phase;
    const uint32_t st0 = m_op[0]->m_cache.phase_step, st1 = m_op[1]->m_cache.phase_step,
                   st2 = m_op[2]->m_cache.phase_step, st3 = m_op[3]->m_cache.phase_step;
    const uint32_t am_shift = m_sor_am_shift, feedback = m_sor_feedback, pan = m_sor_pan;
    int32_t fb0 = m_feedback[0], fb1 = m_feedback[1], fbin = m_feedback_in;
    auto volume = [&](const Lane &l, uint32_t phase, uint32_t am_offset) -> int32_t
    {
        if (l.quiet)
            return 0;
        const uint32_t sin_attenuation = wave[phase & (RegisterType::WAVEFORM_LENGTH - 1)];
        if (AM && l.op->m_sor_am && am_offset)
            return sor_power_at(std::min<uint32_t>(l.op->m_sor_attenuation + am_offset, 0x3ff) << 2)[sin_attenuation];
        return l.power[sin_attenuation];
    };
    for (uint32_t i = 0; i < n; i++)
    {
        // Same envelope-counter sequence as fm_engine_base::clock().
        if (bitfield(++env, 0, 2) == RegisterType::EG_CLOCK_DIVIDER)
        {
            env += 4 - RegisterType::EG_CLOCK_DIVIDER;
            const uint32_t step = env >> 2;
            for (auto &l : lane)
                if (l.stepping && !(step & l.mask))
                {
                    l.op->clock_envelope(step);
                    l.op->sor_cache_attenuation();
                    reload(l);
                }
        }
        // fm_channel::clock_fast(): feedback history, then each operator's phase
        fb0 = fb1;
        fb1 = int16_t(fbin);
        ph0 += st0; ph1 += st1; ph2 += st2; ph3 += st3;
        if (!Output)
            continue;

        // output_4op_fixed<Algorithm>()
        const uint32_t am_offset = AM ? (uint32_t(am[i]) << 1) >> am_shift : 0;
        int32_t opmod = feedback != 0 ? (int32_t(int16_t(fb0)) + int32_t(int16_t(fb1))) >> (10 - feedback) : 0;
        int16_t opout[8];
        opout[0] = 0;
        opout[1] = int16_t(fbin = int16_t(volume(lane[0], (ph0 >> 10) + opmod, am_offset)));
        int32_t sum0 = 4, sum1 = 4;   // dac_discontinuity(0) when nothing is added
        if (pan != 0)
        {
            opmod = opout[bitfield(ops, 0, 1)] >> 1;
            opout[2] = volume(lane[1], (ph1 >> 10) + opmod, am_offset);
            opout[5] = opout[1] + opout[2];
            opmod = opout[bitfield(ops, 1, 3)] >> 1;
            opout[3] = volume(lane[2], (ph2 >> 10) + opmod, am_offset);
            opout[6] = opout[1] + opout[3];
            opout[7] = opout[2] + opout[3];
            opmod = opout[bitfield(ops, 4, 3)] >> 1;
            int32_t result = volume(lane[3], (ph3 >> 10) + opmod, am_offset) >> rshift;
            if (bitfield(ops, 7) != 0)
                result = clamp(result + (opout[1] >> rshift), clipmin, clipmax);
            if (bitfield(ops, 8) != 0)
                result = clamp(result + (opout[2] >> rshift), clipmin, clipmax);
            if (bitfield(ops, 9) != 0)
                result = clamp(result + (opout[3] >> rshift), clipmin, clipmax);
            const int32_t left = (pan & 1) ? result : 0, right = (pan & 2) ? result : 0;
            sum0 = left < 0 ? left - 3 : left + 4;
            sum1 = right < 0 ? right - 3 : right + 4;
        }
        acc[i * 2] += sum0;
        acc[i * 2 + 1] += sum1;
    }
    m_op[0]->m_phase = ph0; m_op[1]->m_phase = ph1; m_op[2]->m_phase = ph2; m_op[3]->m_phase = ph3;
    m_feedback[0] = int16_t(fb0);
    m_feedback[1] = int16_t(fb1);
    m_feedback_in = int16_t(fbin);
    return env;
}

template<class RegisterType>
template<int Algorithm>
void fm_channel<RegisterType>::sor_render_span(int32_t *__restrict acc, uint32_t n, uint32_t env,
    const int8_t *pm, const uint8_t *am, bool output_enabled)
{
    fm_operator<RegisterType> *op0 = m_op[0], *op1 = m_op[1], *op2 = m_op[2], *op3 = m_op[3];
    const uint32_t am_shift = m_sor_am_shift;
    if (sor_light())
    {
        if (!output_enabled)
            sor_render_fast<Algorithm, false, false>(acc, n, env, am);
        else if (am_shift == 7)
            sor_render_fast<Algorithm, false, true>(acc, n, env, am);
        else
            sor_render_fast<Algorithm, true, true>(acc, n, env, am);
        return;
    }
    // SSG-EG, inversion or LFO PM: the full per-sample operator clock.
    for (uint32_t i = 0; i < n; i++)
    {
        // Same envelope-counter sequence as fm_engine_base::clock().
        if (bitfield(++env, 0, 2) == RegisterType::EG_CLOCK_DIVIDER)
            env += 4 - RegisterType::EG_CLOCK_DIVIDER;
        m_feedback[0] = m_feedback[1];
        m_feedback[1] = m_feedback_in;
        if ((env & 3) == 0)
        {
            op0->template sor_span_step<true>(env, pm[i]);
            op1->template sor_span_step<true>(env, pm[i]);
            op2->template sor_span_step<true>(env, pm[i]);
            op3->template sor_span_step<true>(env, pm[i]);
        }
        else
        {
            op0->template sor_span_step<false>(env, pm[i]);
            op1->template sor_span_step<false>(env, pm[i]);
            op2->template sor_span_step<false>(env, pm[i]);
            op3->template sor_span_step<false>(env, pm[i]);
        }
        if (!output_enabled)
            continue;
        output_data temp;
        temp.clear();
        output_4op_fixed<Algorithm>(temp, 5, 256, am_shift == 7 ? 0 : (uint32_t(am[i]) << 1) >> am_shift);
        // OPN2 DAC discontinuity, as in ym2612::generate().
        acc[i * 2] += temp.data[0] < 0 ? temp.data[0] - 3 : temp.data[0] + 4;
        acc[i * 2 + 1] += temp.data[1] < 0 ? temp.data[1] - 3 : temp.data[1] + 4;
    }
}

template<class RegisterType>
uint32_t fm_engine_base<RegisterType>::sor_render_span(int32_t *__restrict acc, uint32_t n, uint32_t output_channels)
{
    static_assert(CHANNELS == 6 && RegisterType::OUTPUTS == 2 && RegisterType::EG_CLOCK_DIVIDER == 3,
        "SoR span renderer targets OPN2");
    assert(!sor_prepare_due() && n <= sor_span_limit() && n <= SOR_SPAN_MAX);
    int8_t pm[SOR_SPAN_MAX];
    uint8_t am[SOR_SPAN_MAX];
    const uint32_t env_before = m_env_counter;
    for (uint32_t i = 0; i < n; i++)
    {
        pm[i] = int8_t(m_regs.clock_noise_and_lfo());
        am[i] = m_regs.sor_lfo_am();
    }
    m_total_clocks += n;
    m_prepare_count += n;

    // Channels that output nothing still contribute dac_discontinuity(0) = 4.
    uint32_t constant_outputs = 0;
    for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
    {
        const bool output_enabled = chnum < output_channels &&
            bitfield(debug::GLOBAL_FM_CHANNEL_MASK & m_active_channels, chnum);
        if (chnum < output_channels && !output_enabled)
            constant_outputs++;
        if (m_sor_quiet_mask & (1u << chnum))
        {
            if (!m_sor_quiet_ticks[chnum])
                m_sor_env_before[chnum] = env_before;
            m_sor_quiet_ticks[chnum] += n;
            continue;
        }
        auto &channel = *m_channel[chnum];
        switch (channel.sor_algorithm())
        {
            case 0: channel.template sor_render_span<0>(acc, n, env_before, pm, am, output_enabled); break;
            case 1: channel.template sor_render_span<1>(acc, n, env_before, pm, am, output_enabled); break;
            case 2: channel.template sor_render_span<2>(acc, n, env_before, pm, am, output_enabled); break;
            case 3: channel.template sor_render_span<3>(acc, n, env_before, pm, am, output_enabled); break;
            case 4: channel.template sor_render_span<4>(acc, n, env_before, pm, am, output_enabled); break;
            case 5: channel.template sor_render_span<5>(acc, n, env_before, pm, am, output_enabled); break;
            case 6: channel.template sor_render_span<6>(acc, n, env_before, pm, am, output_enabled); break;
            default: channel.template sor_render_span<7>(acc, n, env_before, pm, am, output_enabled); break;
        }
    }

    // Leave the shared envelope counter where n calls to clock() would.
    for (uint32_t i = 0; i < n; i++)
        if (bitfield(++m_env_counter, 0, 2) == RegisterType::EG_CLOCK_DIVIDER)
            m_env_counter += 4 - RegisterType::EG_CLOCK_DIVIDER;
    return constant_outputs;
}
