"""Small, checked adaptations of the pinned BSD-licensed ymfm source."""
def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned ymfm source changed; review single-channel patch')
    return text.replace(old, new)


def patch(name, text):
    if name == 'ymfm_fm.h':
        text=replace_once(text,'namespace ymfm\n{','namespace ymfm\n{\nconst int16_t *sor_power_at(uint32_t attenuation);')
        text=replace_once(text,'int32_t compute_volume(uint32_t phase, uint32_t am_offset) const;', 'inline __attribute__((always_inline)) int32_t compute_volume(uint32_t phase, uint32_t am_offset) const;')
        old='\tvoid output(output_data &output, uint32_t rshift, int32_t clipmax, uint32_t chanmask) const;'
        text=replace_once(text, old, old+'\n\tvoid output_single(output_data &output, uint32_t rshift, int32_t clipmax, uint32_t chnum) const;')
        text=replace_once(text, '\tuint32_t m_phase;                      // current phase value (10.10 format)',
            '\tuint32_t m_phase;                      // current phase value (10.10 format)\n\tconst int16_t *m_sor_power=nullptr;\n'
            '\tuint32_t m_sor_attenuation = 0, m_sor_env_mask = 0;\n\tbool m_sor_am = false, m_sor_ssg = false, m_sor_env_stable = false;\n'
            '\tvoid sor_cache_attenuation() { uint32_t v=m_env_attenuation >> m_cache.eg_shift; '
            'if (RegisterType::EG_HAS_SSG && m_ssg_inverted) v=(0x200-v)&0x3ff; '
            'm_sor_attenuation=v+m_cache.total_level; m_sor_power=sor_power_at(std::min<uint32_t>(m_sor_attenuation,1023)<<2); '
            'unsigned rate=m_cache.eg_rate[m_env_state], shift=rate>>2; '
            'm_sor_env_mask=shift<11?(1u<<(11-shift))-1:0; '
            'if ((m_env_state==EG_ATTACK && m_env_attenuation==0) || '
            '(m_env_state==EG_DECAY && m_env_attenuation>=m_cache.eg_sustain) || '
            'm_env_state==EG_DEPRESS || m_env_state==EG_REVERB || '
            '(RegisterType::EG_HAS_REVERB && m_env_state==EG_RELEASE && m_env_attenuation>=0xc0)) m_sor_env_mask=0; '

            'm_sor_env_stable=!m_sor_ssg && !m_ssg_inverted && '
            '((m_env_state==EG_ATTACK && m_env_attenuation!=0 && (rate==0 || rate>=62)) || '
            '(m_env_state==EG_DECAY && m_env_attenuation<m_cache.eg_sustain && rate==0) || '
            '(m_env_state==EG_SUSTAIN && (rate==0 || m_env_attenuation==0x3ff)) || '
            '(m_env_state==EG_RELEASE && !RegisterType::EG_HAS_REVERB && (rate==0 || m_env_attenuation==0x3ff))); }')
        text=replace_once(text,'\tuint32_t phase() const { return m_phase >> 10; }',
            '\tuint32_t phase() const { return m_phase >> 10; }\n'
            '\tbool sor_quiet() const { return ((m_env_state==EG_RELEASE && m_env_attenuation>=EG_QUIET) || (m_sor_env_stable && m_sor_attenuation>=832)) && !RegisterType::EG_HAS_REVERB && !m_sor_ssg && !m_ssg_inverted && m_cache.phase_step!=opdata_cache::PHASE_STEP_DYNAMIC; }\n'
            '\tvoid sor_advance_quiet(uint32_t n, uint32_t first_env, uint32_t last_env);\n\tbool sor_silent() const { return m_env_attenuation>EG_QUIET || m_sor_attenuation>=832; }')
        text=replace_once(text,'\t// return a reference to our registers\n\tRegisterType &regs() const { return m_regs; }\n\n\t// simple getters for debugging\n\tfm_operator',
            '''\tbool sor_quiet() const { for(auto op:m_op) if(op && !op->sor_quiet()) return false; return true; }
    void sor_mark_zero_output(bool active) {m_sor_silent_active=active;}
    void sor_advance_quiet(uint32_t n, uint32_t first_env, uint32_t last_env) {
        if(!n)return;
        if(m_sor_silent_active) {
            m_feedback[0]=n==1?m_feedback[1]:n==2?m_feedback_in:0;
            m_feedback[1]=n==1?m_feedback_in:0;m_feedback_in=0;
        } else {m_feedback[0]=n==1?m_feedback[1]:m_feedback_in; m_feedback[1]=m_feedback_in;}
        for(auto op:m_op)if(op)op->sor_advance_quiet(n,first_env,last_env);
    }
\t// return a reference to our registers
\tRegisterType &regs() const { return m_regs; }

\t// simple getters for debugging
\tfm_operator''')
        text=replace_once(text,'\tuint32_t m_active_channels;',
            '\tuint32_t m_sor_quiet_mask=0, m_sor_quiet_ticks[CHANNELS]{}, m_sor_env_before[CHANNELS]{};\n'
            '\tvoid sor_flush_quiet() { for(unsigned c=0;c<CHANNELS;c++) { m_channel[c]->sor_advance_quiet(m_sor_quiet_ticks[c],m_sor_env_before[c],m_env_counter); m_sor_quiet_ticks[c]=0; } }\n'
            '\tuint32_t m_active_channels;')
        text=replace_once(text,'fm_channel<RegisterType> *debug_channel(uint32_t index) const { return m_channel[index].get(); }',
            'fm_channel<RegisterType> *debug_channel(uint32_t index) const { const_cast<fm_engine_base *>(this)->sor_flush_quiet(); return m_channel[index].get(); }')
        text=replace_once(text,'fm_operator<RegisterType> *debug_operator(uint32_t index) const { return m_operator[index].get(); }',
            'fm_operator<RegisterType> *debug_operator(uint32_t index) const { const_cast<fm_engine_base *>(this)->sor_flush_quiet(); return m_operator[index].get(); }')
        old='\tvoid output_4op(output_data &output, uint32_t rshift, int32_t clipmax) const;'
        text=replace_once(text,old,old+'\n\ttemplate<int Algorithm> void output_4op_fixed(output_data &output, uint32_t rshift, int32_t clipmax) const;')
        text=replace_once(text,'\tmutable int16_t m_feedback_in;',
            '\tbool m_sor_silent_active=false;\n\tuint8_t m_sor_algorithm=0, m_sor_feedback=0, m_sor_pan=0, m_sor_am_shift=7;\n\tmutable int16_t m_feedback_in;')
        for output in range(4):
            text=replace_once(text,'m_regs.ch_output_'+str(output)+'(choffs)', '(m_sor_pan & '+str(1<<output)+')')
        old='\tvoid clock(uint32_t env_counter, int32_t lfo_raw_pm);'
        assert text.count(old)==2
        text=text.replace(old, '\ttemplate<bool Envelope> void clock_fast(uint32_t env_counter, int32_t lfo_raw_pm);\n'
            '\tvoid clock(uint32_t e, int32_t l) { if ((e&3)==0) clock_fast<true>(e,l); else clock_fast<false>(e,l); }')
        return text
    if name == 'ymfm_fm.ipp':
        marker='template<class RegisterType>\nvoid fm_engine_base<RegisterType>::output('
        implementation='''// SoR port: YM2612 requests one channel at a time for DAC discontinuity.
// Avoid a channel-mask scan without changing any operator or mixing arithmetic.
template<class RegisterType>
void fm_engine_base<RegisterType>::output_single(output_data &result, uint32_t rshift, int32_t clipmax, uint32_t chnum) const
{
    assert(chnum < CHANNELS);
    uint32_t mask = 1U << chnum;
    if (m_regs.rhythm_enable() || YMFM_DEBUG_LOG_WAVFILES)
    {
        output(result, rshift, clipmax, mask);
        return;
    }
    if (!(mask & debug::GLOBAL_FM_CHANNEL_MASK & m_active_channels))
        return;
    if (m_channel[chnum]->is4op())
        m_channel[chnum]->output_4op(result, rshift, clipmax);
    else
        m_channel[chnum]->output_2op(result, rshift, clipmax);
}

'''
        text=replace_once(text, marker, implementation+marker)
        text=replace_once(text, '\tm_regs.cache_operator_data(m_choffs, m_opoffs, m_cache);',
            '\tm_regs.cache_operator_data(m_choffs, m_opoffs, m_cache);\n'
            '\tm_sor_am=m_regs.op_lfo_am_enable(m_opoffs);\n\tm_sor_ssg=m_regs.op_ssg_eg_enable(m_opoffs);')
        text=replace_once(text, '\tm_keyon_live &= ~(1 << KEYON_CSM);',
            '\tm_keyon_live &= ~(1 << KEYON_CSM);\n\tsor_cache_attenuation();')
        start=text.index('void fm_operator<RegisterType>::clock(uint32_t env_counter')
        end=text.index('//  compute_volume',start)
        part=text[start:end]
        part=part.replace('if (m_regs.op_ssg_eg_enable(m_opoffs))','bool refresh=m_sor_ssg || m_ssg_inverted || bitfield(env_counter, 0, 2)==0;\n\tif (m_sor_ssg)')
        part=part.replace('\t// clock the phase','\tif (refresh) sor_cache_attenuation();\n\n\t// clock the phase')
        text=text[:start]+part+text[end:]
        text=replace_once(text, 'uint32_t env_attenuation = envelope_attenuation(am_offset) << 2;',
            'uint32_t env_attenuation = std::min<uint32_t>(m_sor_attenuation + (m_sor_am ? am_offset : 0), 0x3ff) << 2;')
        # Decide the envelope phase once per chip sample rather than 24 times.
        for cls in ('fm_operator','fm_channel'):
            old='template<class RegisterType>\nvoid '+cls+'<RegisterType>::clock(uint32_t env_counter, int32_t lfo_raw_pm)'
            text=replace_once(text,old,'template<class RegisterType>\ntemplate<bool Envelope>\nvoid '+cls+'<RegisterType>::clock_fast(uint32_t env_counter, int32_t lfo_raw_pm)')
        text=replace_once(text,'bool refresh=m_sor_ssg || m_ssg_inverted || bitfield(env_counter, 0, 2)==0;',
            'bool envelope_due=Envelope && !m_sor_env_stable && (m_sor_ssg || !((env_counter>>2)&m_sor_env_mask));\n\tbool refresh=m_sor_ssg || m_ssg_inverted || envelope_due;')
        text=replace_once(text,'if (bitfield(env_counter, 0, 2) == 0)','if (envelope_due)')
        text=replace_once(text,'m_op[opnum]->clock(env_counter, lfo_raw_pm);','m_op[opnum]->template clock_fast<Envelope>(env_counter, lfo_raw_pm);')
        old='''for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
\t\tif (bitfield(chanmask, chnum))
\t\t\tm_channel[chnum]->clock(m_env_counter, lfo_raw_pm);'''
        text=replace_once(text,old,'''if ((m_env_counter&3)==0) {
        for (uint32_t chnum=0;chnum<CHANNELS;chnum++) if (bitfield(chanmask,chnum))
            m_channel[chnum]->template clock_fast<true>(m_env_counter,lfo_raw_pm);
    } else {
        for (uint32_t chnum=0;chnum<CHANNELS;chnum++) if (bitfield(chanmask,chnum))
            m_channel[chnum]->template clock_fast<false>(m_env_counter,lfo_raw_pm);
    }''')
        text=replace_once(text,'bool fm_channel<RegisterType>::prepare()\n{',
            '''bool fm_channel<RegisterType>::prepare()
{
    m_sor_algorithm=m_regs.ch_algorithm(m_choffs);
    m_sor_feedback=m_regs.ch_feedback(m_choffs);
    m_sor_pan=(RegisterType::OUTPUTS==1 || m_regs.ch_output_0(m_choffs)?1:0) |
        (m_regs.ch_output_1(m_choffs)?2:0) | (m_regs.ch_output_2(m_choffs)?4:0) | (m_regs.ch_output_3(m_choffs)?8:0);
    m_sor_am_shift=(1u<<(m_regs.ch_lfo_am_sens(m_choffs)^3))-1;''')
        marker='template<class RegisterType>\nvoid fm_channel<RegisterType>::output_4op(output_data &output, uint32_t rshift, int32_t clipmax) const'
        wrapper=marker+''' {
    switch(m_sor_algorithm) {
        case 0: output_4op_fixed<0>(output,rshift,clipmax);break;
        case 1: output_4op_fixed<1>(output,rshift,clipmax);break;
        case 2: output_4op_fixed<2>(output,rshift,clipmax);break;
        case 3: output_4op_fixed<3>(output,rshift,clipmax);break;
        case 4: output_4op_fixed<4>(output,rshift,clipmax);break;
        case 5: output_4op_fixed<5>(output,rshift,clipmax);break;
        case 6: output_4op_fixed<6>(output,rshift,clipmax);break;
        case 7: output_4op_fixed<7>(output,rshift,clipmax);break;
        default: output_4op_fixed<-1>(output,rshift,clipmax);break;
    }
}

template<class RegisterType>
template<int Algorithm>
void fm_channel<RegisterType>::output_4op_fixed(output_data &output, uint32_t rshift, int32_t clipmax) const'''
        text=replace_once(text,marker,wrapper)
        a=text.index('void fm_channel<RegisterType>::output_4op_fixed(')
        b=text.index('//  output_rhythm_ch6',a)
        part=text[a:b].replace('m_regs.ch_feedback(m_choffs)','m_sor_feedback').replace('m_regs.ch_output_any(m_choffs)','m_sor_pan')
        part=part.replace('s_algorithm_ops[m_regs.ch_algorithm(m_choffs)]','s_algorithm_ops[Algorithm < 0 ? m_sor_algorithm : Algorithm]')
        part=part.replace('m_regs.lfo_am_offset(m_choffs)', 'm_sor_am_shift==7?0:m_regs.lfo_am_offset(m_choffs)')
        text=text[:a]+part+text[b:]
        text=replace_once(text,'void fm_engine_base<RegisterType>::reset()\n{',
            'void fm_engine_base<RegisterType>::reset()\n{\n    m_sor_quiet_mask=0; for(auto &n:m_sor_quiet_ticks)n=0;')
        text=replace_once(text,'void fm_engine_base<RegisterType>::save_restore(ymfm_saved_state &state)\n{',
            'void fm_engine_base<RegisterType>::save_restore(ymfm_saved_state &state)\n{\n    if(state.saving())sor_flush_quiet(); else {m_sor_quiet_mask=0;for(auto &n:m_sor_quiet_ticks)n=0;}')
        text=replace_once(text,'\t\t// reassign operators to channels if dynamic',
            '\t\tsor_flush_quiet();\n\t\t// reassign operators to channels if dynamic')
        text=replace_once(text,'\t\t// reset the modified channels and prepare count',
            '''        m_sor_quiet_mask=0;
        for(unsigned c=0;c<CHANNELS;c++) if ((chanmask&(1u<<c)) && m_channel[c]->sor_quiet()) {
            m_sor_quiet_mask|=1u<<c;
            m_channel[c]->sor_mark_zero_output(bool(m_active_channels&(1u<<c)));
            m_active_channels&=~(1u<<c);
        }
\t\t// reset the modified channels and prepare count''')
        text=replace_once(text,'uint32_t fm_engine_base<RegisterType>::clock(uint32_t chanmask)\n{',
            'uint32_t fm_engine_base<RegisterType>::clock(uint32_t chanmask)\n{\n    const uint32_t sor_env_before=m_env_counter;')
        for phase in ('true','false'):
            old='m_channel[chnum]->template clock_fast<'+phase+'>(m_env_counter,lfo_raw_pm);'
            text=replace_once(text,old,'{ if (m_sor_quiet_mask&(1u<<chnum)) { if(!m_sor_quiet_ticks[chnum]++)m_sor_env_before[chnum]=sor_env_before; } else '+old+' }')
        marker='template<class RegisterType>\nbool fm_operator<RegisterType>::prepare()'
        text=replace_once(text,marker,'''// Inactive release envelopes are linear, with an eight-step increment
// pattern. Sum that pattern at cache boundaries instead of clocking silence.
template<class RegisterType>
void fm_operator<RegisterType>::sor_advance_quiet(uint32_t n, uint32_t first_env, uint32_t last_env)
{
    if(!n)return;
    m_phase+=m_cache.phase_step*n;
    if(m_env_state==EG_RELEASE && m_env_attenuation<0x3ff) {
        unsigned rate=m_cache.eg_rate[EG_RELEASE],shift=rate>>2;
        unsigned period=shift<11?1u<<(11-shift):1;
        unsigned first=first_env>>2;
        unsigned last=first+(((last_env>>2)-first)&0x3fffffff);
        unsigned begin=first/period,end=last/period,count=end-begin;
        unsigned sum=0,cycle=0;
        for(unsigned i=0;i<8;i++)cycle+=attenuation_increment(rate,i);
        sum=(count/8)*cycle;
        for(unsigned i=0;i<(count&7);i++)sum+=attenuation_increment(rate,(begin+1+i)&7);
        m_env_attenuation=std::min<unsigned>(0x3ff,m_env_attenuation+sum);
    }
    sor_cache_attenuation();
}

'''+marker)
        text=replace_once(text,'if (m_env_attenuation > EG_QUIET)',
            'if (m_env_attenuation > EG_QUIET || m_sor_attenuation >= 832)')
        # Signed exponential table combines sign and variable shift in one load.
        # 32 KiB replaces the rejected multi-megabyte phase/attenuation table.
        start=text.index('inline uint32_t attenuation_to_volume(')
        stop=text.index('\n}',start)+2
        import re
        mant=[(int(v,16)|0x400)<<2 for v in re.findall(r'X\(0x([0-9a-f]+)\)',text[start:stop])]
        assert len(mant)==256
        values=[(mant[i&255]>>(i>>8) if i<3328 else 0) for i in range(8192)]
        values+= [-v for v in values]
        table='alignas(32) static const int16_t sor_signed_power[16384] = {\n'+',\n'.join(','.join(str(v) for v in values[i:i+32]) for i in range(0,len(values),32))+'\n};\n'
        text=text[:stop]+'\n'+table+'\ninline const int16_t *sor_power_at(uint32_t attenuation) { return sor_signed_power+attenuation; }\n'+text[stop:]
        old='int32_t result = attenuation_to_volume((sin_attenuation & 0x7fff) + env_attenuation);'
        text=replace_once(text,old,'int32_t result = (m_sor_am && am_offset ? sor_power_at(env_attenuation) : m_sor_power)[sin_attenuation];')
        text=replace_once(text,'return bitfield(sin_attenuation, 15) ? -result : result;','return result;')
        # Keep this small hot operator calculation inline on SH-4. Arithmetic is unchanged.
        text=replace_once(text, 'int32_t fm_operator<RegisterType>::compute_volume(uint32_t phase, uint32_t am_offset) const',
            'inline __attribute__((always_inline)) int32_t fm_operator<RegisterType>::compute_volume(uint32_t phase, uint32_t am_offset) const')
        return text
    if name == 'ymfm_opn.h':
        marker='class ym2612'
        offset=text.index(marker)
        end=text.index("\n};",offset)
        tail=text[offset:end]
        old='void generate(output_data *output, uint32_t numsamples = 1);'
        tail=replace_once(tail,old,old+'\n\tvoid dac_component(output_data &output);\n'
            '\tuint64_t (*profile_clock)()=nullptr; uint64_t profile_clocking=0,profile_output=0;\n'
            '\tuint32_t workload() { uint32_t result=0; for(unsigned i=0;i<fm_engine::OPERATORS;i++) {'
            'auto op=m_fm.debug_operator(i); result+=(op->debug_cache().phase_step==opdata_cache::PHASE_STEP_DYNAMIC); '
            'result+=op->regs().op_ssg_eg_enable(op->opoffs())?256:0; result+=op->debug_eg_attenuation()<=0x380?65536:0; result+=op->sor_silent()?0:16777216; } return result; }')
        return text[:offset]+tail+text[end:]
    if name == 'ymfm_opn.cpp':
        text=replace_once(text,'abs_sin_attenuation(index) | (bitfield(index, 9) << 15)', 'abs_sin_attenuation(index) | (bitfield(index, 9) << 13)')
        marker='void ym2612::generate(output_data *output, uint32_t numsamples)'
        component="""// SoR port: expose the DAC stem for synchronized AICA hardware mixing.
void ym2612::dac_component(output_data &output)
{
    output.clear();
    if (!m_dac_enable) return;
    int32_t value = dac_discontinuity(int16_t(m_dac_data << 7) >> 7);
    output.data[0] = m_fm.regs().ch_output_0(0x102) ? value : dac_discontinuity(0);
    output.data[1] = m_fm.regs().ch_output_1(0x102) ? value : dac_discontinuity(0);
    for (int c = 0; c < 2; c++) output.data[c] = (output.data[c] * 128) * 64 / (6 * 65);
}

"""
        text=replace_once(text,marker,component+marker)
        text=replace_once(text, 'm_fm.output(temp.clear(), 5, 256, 1 << chan);',
                            'm_fm.output_single(temp.clear(), 5, 256, chan);')
        a=text.index('void ym2612::generate(');b=text.index('\n}',a)+2
        body=text[a:b].replace('\t\t// clock the system', '\t\tauto t0=profile_clock?profile_clock():0;\n\t\t// clock the system')
        body=body.replace('m_fm.clock(fm_engine::ALL_CHANNELS);', 'm_fm.clock(fm_engine::ALL_CHANNELS);\n\t\tauto t1=profile_clock?profile_clock():0;')
        pos=body.rfind('\n\t}')
        body=body[:pos]+'\n\t\tif(profile_clock){profile_clocking+=t1-t0;profile_output+=profile_clock()-t1;}'+body[pos:]
        return text[:a]+body+text[b:]
    return text
