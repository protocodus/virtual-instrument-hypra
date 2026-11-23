"""Maps JE-8086 part A settings (index, count) onto TranceraRenderEvents parameter names."""
U=lambda i,n:i
B=lambda i,n:i-64
E=lambda i,n:i
MAP={
 'a_osc_waveform':('osc1.wave',E),'a_osc1_ctrl_1':('osc1.ctrl1',U),'a_osc1_ctrl_2':('osc1.ctrl2',U),
 'a_osc_balance':('balance',B),'a_ring_modulator':('ring',E),'a_cross_mod_depth':('xmod',U),
 'a_osc2_sync':('osc2.sync',E),'a_osc2_range':('osc2.range',lambda i,n:i-25),'a_osc2_fine_wide':('osc2.fine',lambda i,n:i-50),
 'a_lfo1_env_dest':('dest',E),'a_osc_lfo1_depth':('pitchlfo',B),'a_pitch_env_depth':('pitchenv',B),
 'a_pitch_env_attack':('pitchenv.a',U),'a_pitch_env_decay':('pitchenv.d',U),
 'a_flt_type':('filter.type',E),'a_cutoff_slope':('filter.steep',E),'a_flt_cutoff_freq':('cutoff',U),'a_flt_resonance':('resonance',U),
 'a_flt_key_follow':('keyfollow',B),'a_flt_lfo1_depth':('filterlfo',B),'a_flt_env_depth':('envdepth',B),
 'a_flt_env_attack':('fenv.a',U),'a_flt_env_decay':('fenv.d',U),'a_flt_env_sustain':('fenv.s',U),'a_flt_env_release':('fenv.r',U),
 'a_amp_level':('level',U),'a_amp_lfo1_depth':('amplfo',B),'a_lfo1_mod':('pan',E),
 'a_amp_env_attack':('amp.a',U),'a_amp_env_decay':('amp.d',U),'a_amp_env_sustain':('amp.s',U),'a_amp_env_release':('amp.r',U),
 'a_lfo1_waveform':('lfo1.wave',E),'a_lfo1_rate':('lfo1.rate',U),'a_lfo1_fade':('lfo1.fade',U),'a_lfo2_rate':('lfo2.rate',U),
 'a_pitch_lfo2_depth':('lfo2.pitch',B),'a_flt_lfo2_depth':('lfo2.filter',B),'a_amp_lfo2_depth':('lfo2.amp',B),
 'a_tone_ctrl_bass':('bass',B),'a_tone_ctrl_treble':('treble',B),'a_chorus_type':('chorus.type',E),'a_multi_fx_level':('chorus',U),
 'a_delay_type':('delay.type',E),'a_delay_time':('delay.time',U),'a_delay_feedback':('delay.feedback',U),'a_delay_level':('delay',U),
 'a_portamento':('portamento',E),'a_portamento_time':('portamento.time',U),'a_mono':('mono',E),'a_legato':('legato',E),
 'a_oscillator_shift':('oscshift',lambda i,n:i-2),'a_unison':('unison',E),
}
VEL={'a_v_lfo1_rate':'vel.lfo1Rate','a_v_lfo1_fade':'vel.lfo1Fade','a_v_lfo2_rate':'vel.lfo2Rate','a_v_cross_mod_depth':'vel.xmod','a_v_osc_balance':'vel.balance','a_v_pitch_lfo1_depth':'vel.pitchLfo','a_v_pitch_lfo2_depth':'vel.pitchLfo2','a_v_pitchenv_depth':'vel.pitchEnvDepth','a_v_pitch_env_atk':'vel.pitchAttack','a_v_pitch_env_dcy':'vel.pitchDecay','a_v_osc1_control_1':'vel.osc1Ctrl1','a_v_osc1_control_2':'vel.osc1Ctrl2','a_v_osc2_range':'vel.osc2Range','a_v_osc2_fine_wide':'vel.osc2Fine','a_v_osc2_control_1':'vel.osc2Ctrl1','a_v_osc2_control_2':'vel.osc2Ctrl2','a_v_cutoff_freq':'vel.cutoff','a_v_flt_resonance':'vel.resonance','a_v_key_follow':'vel.keyFollow','a_v_flt_lfo1':'vel.filterLfo','a_v_flt_lfo2':'vel.filterLfo2','a_v_flt_env_dpth':'vel.envDepth','a_v_flt_env_attack':'vel.filterAttack','a_v_flt_env_decay':'vel.filterDecay','a_v_flt_env_sustain':'vel.filterSustain','a_v_flt_env_rel':'vel.filterRelease','a_v_amp_level':'vel.level','a_v_amp_lfo1_dpth':'vel.ampLfo','a_v_amp_lfo2_dpth':'vel.ampLfo2','a_v_amp_env_attack':'vel.ampAttack','a_v_amp_env_decay':'vel.ampDecay','a_v_amp_env_sustain':'vel.ampSustain','a_v_amp_env_rel':'vel.ampRelease','a_v_tone_ctrl_bass':'vel.bass','a_v_tone_ctrl_treble':'vel.treble','a_v_multifx_level':'vel.multiFx','a_v_delay_time':'vel.delayTime','a_v_delay_feedbk':'vel.delayFeedback','a_v_delay_level':'vel.delayLevel','a_v_portamento':'vel.portamento'}
def events(params,matrix=False):
    ev=[]
    if matrix and 'a_velocity' in params:
        ev.append(f"0 set vel.on {1 if params['a_velocity'][2] in ('ON','True') else 0}")
        for k,n in VEL.items():
            if k in params:ev.append(f'0 set {n} {int(float(params[k][2]))}')
    for k,(name,f) in MAP.items():
        if k in params:
            i,n=params[k][0],params[k][1];ev.append(f'0 set {name} {f(i,n)}')
    i=params['a_osc2_waveform'][0]
    ev.append(f'0 set osc2.wave {min(i,2)}')   # NOISE (JP-8080) falls back to triangle
    if i==0:
        ev.append(f"0 set osc2.pw {params['a_osc2_ctrl1'][0]}");ev.append(f"0 set osc2.pwm {params['a_osc2_ctrl2'][0]}")
    return ev
def name(params):
    return ''.join(chr(params[f'a_patchname{i}'][0]+32) for i in range(1,17)).strip()
def gain_db(params):
    return 6.0*params['a_patch_gain'][0]
