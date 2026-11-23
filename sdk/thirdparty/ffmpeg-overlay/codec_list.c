static const AVCodec* const codec_list[] = {
#if CONFIG_MP3FLOAT_DECODER
    &ff_mp3float_decoder,
#endif
#if CONFIG_MP3_DECODER
    &ff_mp3_decoder,
#endif
#if CONFIG_WMAPRO_DECODER
    &ff_wmapro_decoder,
#endif
#if CONFIG_WMAV2_DECODER
    &ff_wmav2_decoder,
#endif
#if CONFIG_XMAFRAMES_DECODER
    &ff_xmaframes_decoder,
#endif
#if CONFIG_WMV3_DECODER
    &ff_wmv3_decoder,
#endif
/* nights-nx (2026-11-22): VC-1 advanced profile (WVC1), NiGHTS's movies; built with WMV3 (vc1dec.c, config_masseffect_wmv3.h). */
#if CONFIG_VC1_DECODER
    &ff_vc1_decoder,
#endif
    NULL};
