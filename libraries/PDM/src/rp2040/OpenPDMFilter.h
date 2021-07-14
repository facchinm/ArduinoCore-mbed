/**
 *******************************************************************************
 * @file    OpenPDMFilter.h
 * @author  CL
 * @version V1.0.0
 * @date    9-September-2015
 * @brief   Header file for Open PDM audio software decoding Library.   
 *          This Library is used to decode and reconstruct the audio signal
 *          produced by ST MEMS microphone (MP45Dxxx, MP34Dxxx). 
 *******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT 2018 STMicroelectronics</center></h2>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************
 */


/* Define to prevent recursive inclusion -------------------------------------*/

#ifndef __OPENPDMFILTER_H
#define __OPENPDMFILTER_H

/* Includes ------------------------------------------------------------------*/

#include <stdint.h>


/* Definitions ---------------------------------------------------------------*/

/*
 * Enable to use a Look-Up Table to improve performances while using more FLASH
 * and RAM memory.
 * Note: Without Look-Up Table up to stereo@16KHz configuration is supported.
 */
#define USE_LUT

#define SINCN            3
#define DECIMATION_MAX 128

#define HTONS(A) ((((uint16_t)(A) & 0xff00) >> 8) | \
                 (((uint16_t)(A) & 0x00ff) << 8))
#define RoundDiv(a, b)    (((a)>0)?(((a)+(b)/2)/(b)):(((a)-(b)/2)/(b)))
#define SaturaLH(N, L, H) (((N)<(L))?(L):(((N)>(H))?(H):(N)))


/* Types ---------------------------------------------------------------------*/

typedef struct {
  /* Public */
  float LP_HZ;
  float HP_HZ;
  uint16_t Fs;
  unsigned int nSamples;
  uint8_t In_MicChannels;
  uint8_t Out_MicChannels;
  uint8_t Decimation;
  uint8_t MaxVolume;
  /* Private */
  uint32_t Coef[SINCN];
  uint16_t FilterLen;
  int64_t OldOut, OldIn, OldZ;
  uint16_t LP_ALFA;
  uint16_t HP_ALFA;
  uint16_t bit[5];
  uint16_t byte;
  uint16_t filterGain;
} TPDMFilter_InitStruct;


/* Exported functions ------------------------------------------------------- */

extern uint32_t* div_const;
extern int64_t* sub_const;
#ifdef USE_LUT
extern int32_t (*lut)[DECIMATION_MAX / 8][SINCN];
#endif

constexpr void convolve(uint32_t Signal[/* SignalLen */], unsigned short SignalLen,
              uint32_t Kernel[/* KernelLen */], unsigned short KernelLen,
              uint32_t Result[/* SignalLen + KernelLen - 1 */])
{
  uint16_t n = 0;

  for (n = 0; n < SignalLen + KernelLen - 1; n++)
  {
    unsigned short kmin = 0, kmax = 0, k = 0;
    
    Result[n] = 0;
    
    kmin = (n >= KernelLen - 1) ? n - (KernelLen - 1) : 0;
    kmax = (n < SignalLen - 1) ? n : SignalLen - 1;
    
    for (k = kmin; k <= kmax; k++) {
      Result[n] += Signal[k] * Kernel[n - k];
    }
  }
}

constexpr void Open_PDM_Filter_Init(TPDMFilter_InitStruct *Param)
{
  uint16_t i = 0, j = 0;
  int64_t sum = 0;

  uint32_t _div_const = 0;
  int64_t _sub_const = 0;

  uint32_t _sinc[DECIMATION_MAX * SINCN] = {};
  uint32_t _sinc1[DECIMATION_MAX] = {};
  uint32_t _sinc2[DECIMATION_MAX * 2] = {};
  uint32_t _coef[SINCN][DECIMATION_MAX] = {};
  int32_t _lut[256][DECIMATION_MAX / 8][SINCN] = {};

  uint8_t decimation = Param->Decimation;

  for (i = 0; i < SINCN; i++) {
    Param->Coef[i] = 0;
    Param->bit[i] = 0;
  }
  for (i = 0; i < decimation; i++) {
    _sinc1[i] = 1;
  }

  Param->OldOut = Param->OldIn = Param->OldZ = 0;
  Param->LP_ALFA = (Param->LP_HZ != 0 ? (uint16_t) (Param->LP_HZ * 256 / (Param->LP_HZ + Param->Fs / (2 * 3.14159))) : 0);
  Param->HP_ALFA = (Param->HP_HZ != 0 ? (uint16_t) (Param->Fs * 256 / (2 * 3.14159 * Param->HP_HZ + Param->Fs)) : 0);

  Param->FilterLen = decimation * SINCN;
  _sinc[0] = 0;
  _sinc[decimation * SINCN - 1] = 0;      
  convolve(_sinc1, decimation, _sinc1, decimation, _sinc2);
  convolve(_sinc2, decimation * 2 - 1, _sinc1, decimation, &_sinc[1]);     
  for(j = 0; j < SINCN; j++) {
    for (i = 0; i < decimation; i++) {
      _coef[j][i] = _sinc[j * decimation + i];
      sum += _sinc[j * decimation + i];
    }
  }

  _sub_const = sum >> 1;
  _div_const = _sub_const * Param->MaxVolume / 32768 / Param->filterGain;
  _div_const = (_div_const == 0 ? 1 : _div_const);

#ifdef USE_LUT
  /* Look-Up Table. */
  uint16_t c = 0, d = 0, s = 0;
  for (s = 0; s < SINCN; s++)
  {
    uint32_t *coef_p = &_coef[s][0];
    for (c = 0; c < 256; c++)
      for (d = 0; d < decimation / 8; d++)
        _lut[c][d][s] = ((c >> 7)       ) * coef_p[d * 8    ] +
                        ((c >> 6) & 0x01) * coef_p[d * 8 + 1] +
                        ((c >> 5) & 0x01) * coef_p[d * 8 + 2] +
                        ((c >> 4) & 0x01) * coef_p[d * 8 + 3] +
                        ((c >> 3) & 0x01) * coef_p[d * 8 + 4] +
                        ((c >> 2) & 0x01) * coef_p[d * 8 + 5] +
                        ((c >> 1) & 0x01) * coef_p[d * 8 + 6] +
                        ((c     ) & 0x01) * coef_p[d * 8 + 7];
  }
#endif

  lut = _lut;
  div_const = &_div_const;
  sub_const = &_sub_const;
}

void Open_PDM_Filter_64(uint8_t* data, int16_t* data_out, uint16_t mic_gain, TPDMFilter_InitStruct *init_struct);
void Open_PDM_Filter_128(uint8_t* data, int16_t* data_out, uint16_t mic_gain, TPDMFilter_InitStruct *init_struct);

#endif // __OPENPDMFILTER_H

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
