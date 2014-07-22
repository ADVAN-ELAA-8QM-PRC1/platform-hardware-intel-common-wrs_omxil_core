/*
 * Copyright (C) 2009 Wind River Systems
 *      Author: Ho-Eun Ryu <ho-eun.ryu@windriver.com>
 */

#ifndef __WRS_OMXIL_AUDIO_PARSER
#define ___WRS_OMXIL_AUDIO_PARSER

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MP3
 */

#define MP3_HEADER_VERSION_25           0x0
#define MP3_HEADER_VERSION_2            0x2
#define MP3_HEADER_VERSION_1            0x3

#define MP3_HEADER_LAYER_3              0x1
#define MP3_HEADER_LAYER_2              0x2
#define MP3_HEADER_LAYER_1              0x3

#define MP3_HEADER_CRC_PROTECTED        0x0
#define MP3_HEADER_NOT_PROTECTED        0x1

#define MP3_HEADER_STEREO               0x0
#define MP3_HEADER_JOINT_STEREO         0x1
#define MP3_HEADER_DUAL_CHANNEL         0x2
#define MP3_HEADER_SINGLE_CHANNEL       0x3

int mp3_header_parse(const unsigned char *buffer,
                     int *version, int *layer, int *crc, int *bitrate,
                     int *frequency, int *channel, int *mode_extension);

/* end of MP3 */

/*
 * MP4
 */

int audio_specific_config_parse(const unsigned char *buffer,
                                int *aot, int *frequency, int *channel);

/* end of MP4 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ___WRS_OMXIL_AUDIO_PARSER */
