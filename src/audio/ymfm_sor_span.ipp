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

template<class RegisterType>
template<int Algorithm>
void fm_channel<RegisterType>::sor_render_span(int32_t *__restrict acc, uint32_t n, uint32_t env,
    const int8_t *pm, const uint8_t *am, bool output_enabled)
{
    fm_operator<RegisterType> *op0 = m_op[0], *op1 = m_op[1], *op2 = m_op[2], *op3 = m_op[3];
    const uint32_t am_shift = m_sor_am_shift;
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
