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

// Samples before any operator needs more than a phase advance: no SSG-EG,
// inversion or LFO PM, and no envelope step due. Envelope steps fall on
// samples whose counter has low bits 0; step T is due when (T & mask) == 0.
template<class RegisterType>
uint32_t fm_channel<RegisterType>::sor_fast_run(uint32_t env, uint32_t limit) const
{
    const uint32_t first = 2 - (env & 3);   // first sample with low bits 0
    const uint32_t next_step = (env >> 2) + 1;
    for (auto *op : m_op)
    {
        if (op->m_sor_ssg || op->m_ssg_inverted || op->m_cache.phase_step == opdata_cache::PHASE_STEP_DYNAMIC)
            return 0;
        if (!op->m_sor_env_stable)
            limit = std::min(limit, first + 3 * ((0u - next_step) & op->m_sor_env_mask));
    }
    return limit;
}

// n samples of clock + output_4op_fixed<Algorithm> with constant attenuation,
// power tables and phase steps held in locals. Arithmetic is identical.
template<class RegisterType>
template<int Algorithm, bool AM>
void fm_channel<RegisterType>::sor_render_fast(int32_t *__restrict acc, uint32_t n, const uint8_t *am)
{
    static_assert(RegisterType::WAVEFORMS == 1 && Algorithm >= 0 && Algorithm < 8);
    // s_algorithm_ops from output_4op(): op2in | op3in << 1 | op4in << 4 | op1out << 7 | op2out << 8 | op3out << 9
    constexpr auto encode = [](uint32_t op2in, uint32_t op3in, uint32_t op4in, uint32_t op1out, uint32_t op2out, uint32_t op3out)
        { return op2in | (op3in << 1) | (op4in << 4) | (op1out << 7) | (op2out << 8) | (op3out << 9); };
    constexpr uint32_t table[8] = {encode(1,2,3, 0,0,0), encode(0,5,3, 0,0,0), encode(0,2,6, 0,0,0), encode(1,0,7, 0,0,0),
                                   encode(1,0,3, 0,1,0), encode(1,1,1, 0,1,1), encode(1,0,0, 0,1,1), encode(0,0,0, 1,1,1)};
    constexpr uint32_t ops = table[Algorithm];
    constexpr int32_t rshift = 5, clipmax = 256, clipmin = -clipmax - 1;
    fm_operator<RegisterType> *const o0 = m_op[0], *const o1 = m_op[1], *const o2 = m_op[2], *const o3 = m_op[3];
    const uint16_t *const wave = o0->m_cache.waveform;
    uint32_t ph0 = o0->m_phase, ph1 = o1->m_phase, ph2 = o2->m_phase, ph3 = o3->m_phase;
    const uint32_t st0 = o0->m_cache.phase_step, st1 = o1->m_cache.phase_step,
                   st2 = o2->m_cache.phase_step, st3 = o3->m_cache.phase_step;
    const int16_t *const pw0 = o0->m_sor_power, *const pw1 = o1->m_sor_power,
                  *const pw2 = o2->m_sor_power, *const pw3 = o3->m_sor_power;
    // compute_volume() early-out, fixed while no envelope step is due.
    const bool q0 = o0->m_env_attenuation > o0->EG_QUIET || o0->m_sor_attenuation >= 832;
    const bool q1 = o1->m_env_attenuation > o1->EG_QUIET || o1->m_sor_attenuation >= 832;
    const bool q2 = o2->m_env_attenuation > o2->EG_QUIET || o2->m_sor_attenuation >= 832;
    const bool q3 = o3->m_env_attenuation > o3->EG_QUIET || o3->m_sor_attenuation >= 832;
    const uint32_t am_shift = m_sor_am_shift, feedback = m_sor_feedback, pan = m_sor_pan;
    int32_t fb0 = m_feedback[0], fb1 = m_feedback[1], fbin = m_feedback_in;
    auto volume = [&](bool quiet, const fm_operator<RegisterType> *op, const int16_t *power, uint32_t phase, uint32_t am_offset) -> int32_t
    {
        if (quiet)
            return 0;
        const uint32_t sin_attenuation = wave[phase & (RegisterType::WAVEFORM_LENGTH - 1)];
        if (AM && op->m_sor_am && am_offset)
            return sor_power_at(std::min<uint32_t>(op->m_sor_attenuation + am_offset, 0x3ff) << 2)[sin_attenuation];
        return power[sin_attenuation];
    };
    for (uint32_t i = 0; i < n; i++)
    {
        // fm_channel::clock_fast(): feedback history, then each operator's phase
        fb0 = fb1;
        fb1 = int16_t(fbin);
        ph0 += st0; ph1 += st1; ph2 += st2; ph3 += st3;

        // output_4op_fixed<Algorithm>()
        const uint32_t am_offset = AM ? (uint32_t(am[i]) << 1) >> am_shift : 0;
        int32_t opmod = feedback != 0 ? (int32_t(int16_t(fb0)) + int32_t(int16_t(fb1))) >> (10 - feedback) : 0;
        int16_t opout[8];
        opout[0] = 0;
        opout[1] = int16_t(fbin = int16_t(volume(q0, o0, pw0, (ph0 >> 10) + opmod, am_offset)));
        int32_t sum0 = 4, sum1 = 4;   // dac_discontinuity(0) when nothing is added
        if (pan != 0)
        {
            opmod = opout[bitfield(ops, 0, 1)] >> 1;
            opout[2] = volume(q1, o1, pw1, (ph1 >> 10) + opmod, am_offset);
            opout[5] = opout[1] + opout[2];
            opmod = opout[bitfield(ops, 1, 3)] >> 1;
            opout[3] = volume(q2, o2, pw2, (ph2 >> 10) + opmod, am_offset);
            opout[6] = opout[1] + opout[3];
            opout[7] = opout[2] + opout[3];
            opmod = opout[bitfield(ops, 4, 3)] >> 1;
            int32_t result = volume(q3, o3, pw3, (ph3 >> 10) + opmod, am_offset) >> rshift;
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
    o0->m_phase = ph0; o1->m_phase = ph1; o2->m_phase = ph2; o3->m_phase = ph3;
    m_feedback[0] = int16_t(fb0);
    m_feedback[1] = int16_t(fb1);
    m_feedback_in = int16_t(fbin);
}

template<class RegisterType>
template<int Algorithm>
void fm_channel<RegisterType>::sor_render_span(int32_t *__restrict acc, uint32_t n, uint32_t env,
    const int8_t *pm, const uint8_t *am, bool output_enabled)
{
    fm_operator<RegisterType> *op0 = m_op[0], *op1 = m_op[1], *op2 = m_op[2], *op3 = m_op[3];
    const uint32_t am_shift = m_sor_am_shift;
    for (uint32_t i = 0; i < n; i++)
    {
        if (output_enabled)
        {
            // Run up to the next sample needing a full operator clock.
            if (uint32_t run = sor_fast_run(env, n - i))
            {
                if (am_shift == 7)
                    sor_render_fast<Algorithm, false>(acc + i * 2, run, am + i);
                else
                    sor_render_fast<Algorithm, true>(acc + i * 2, run, am + i);
                const uint32_t steps = (env & 3) + run;
                env = (((env >> 2) + steps / 3) << 2) | (steps % 3);
                i += run - 1;
                continue;
            }
        }
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
