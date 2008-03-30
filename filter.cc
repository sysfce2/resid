//  ---------------------------------------------------------------------------
//  This file is part of reSID, a MOS6581 SID emulator engine.
//  Copyright (C) 2001  Dag Lem <resid@nimrod.no>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//  ---------------------------------------------------------------------------

#define __FILTER_CC__
#include "filter.h"

// Maximum cutoff frequency is specified as
// FCmax = 2.6e-5/C = 2.6e-5/2200e-12 = 11818.
//
// Measurements indicate a cutoff frequency range of approximately
// 220Hz - 18kHz on a MOS6581 fitted with 470pF capacitors. The function
// mapping FC to cutoff frequency has the shape of the tanh function.
// In contrast, the MOS8580 almost perfectly corresponds with the
// specificaion of a linear mapping from 30Hz to 12kHz.
// 
// The mappings have been measured by feeding the SID with an external
// signal since the chip itself is incapable of generating waveforms of
// higher fundamental frequency than 4kHz. It is best to use the bandpass
// output at full resonance to pick out the cutoff frequency at any given
// FC setting.
//
// The mapping function is specified with spline interpolation points and
// the function values are retrieved via table lookup.
//
// NB! Cutoff frequency characteristics may vary, we have modeled two
// particular Commodore 64s.

fc_point Filter::f0_6581[] =
{
  //  FC      f         FCHI FCLO
  // ----------------------------
  {    0,   220 },   // 0x00
  {  128,   230 },   // 0x10
  {  256,   250 },   // 0x20
  {  384,   300 },   // 0x30
  {  512,   420 },   // 0x40
  {  640,   780 },   // 0x50
  {  768,  1600 },   // 0x60
  {  832,  2300 },   // 0x68
  {  896,  3200 },   // 0x70
  {  960,  4300 },   // 0x78
  {  992,  5000 },   // 0x7c
  { 1008,  5400 },   // 0x7e
  { 1016,  5700 },   // 0x7f
  { 1023,  6000 },   // 0x7f 0x07
  { 1023,  6000 },   // 0x7f 0x07
  { 1024,  4600 },   // 0x80
  { 1024,  4600 },   // 0x80
  { 1032,  4800 },   // 0x81
  { 1056,  5300 },   // 0x84
  { 1088,  6000 },   // 0x88
  { 1120,  6600 },   // 0x8c
  { 1152,  7200 },   // 0x90
  { 1280,  9500 },   // 0xa0
  { 1408, 12000 },   // 0xb0
  { 1536, 14500 },   // 0xc0
  { 1664, 16000 },   // 0xd0
  { 1792, 17100 },   // 0xe0
  { 1920, 17700 },   // 0xf0
  { 2047, 18000 }    // 0xff 0x07
};

fc_point Filter::f0_8580[] =
{
  //  FC      f         FCHI FCLO
  // ----------------------------
  {    0,     0 },   // 0x00
  {  128,   800 },   // 0x10
  {  256,  1600 },   // 0x20
  {  384,  2500 },   // 0x30
  {  512,  3300 },   // 0x40
  {  640,  4100 },   // 0x50
  {  768,  4800 },   // 0x60
  {  896,  5600 },   // 0x70
  { 1024,  6500 },   // 0x80
  { 1152,  7500 },   // 0x90
  { 1280,  8400 },   // 0xa0
  { 1408,  9200 },   // 0xb0
  { 1536,  9800 },   // 0xc0
  { 1664, 10500 },   // 0xd0
  { 1792, 11000 },   // 0xe0
  { 1920, 11700 },   // 0xf0
  { 2047, 12500 }    // 0xff 0x07
};


// ----------------------------------------------------------------------------
// Constructor.
// ----------------------------------------------------------------------------
Filter::Filter()
{
  enable_filter(true);
  set_chip_model(MOS6581);
  reset();
}


// ----------------------------------------------------------------------------
// Enable filter.
// ----------------------------------------------------------------------------
void Filter::enable_filter(bool enable)
{
  enabled = enable;
}


// ----------------------------------------------------------------------------
// Set chip model.
// ----------------------------------------------------------------------------
void Filter::set_chip_model(chip_model model)
{
  if (model == MOS6581) {
    // The DC offset of each voice is approximately -1/4 of the dynamic
    // range of one voice. This is calculated as follows from results
    // in C= Hacking Issue #20:
    //
    // * The "zero" output level of the mixer at full volume is 5.43V.
    // * Routing one voice to the mixer at full volume yields
    //     5.29V at maximum voice output
    //     5.69V at "zero" voice output
    //     6.34V at minimum voice output
    //   This confirms that the mixer output is inverted.
    // * The DC offset of one voice is -(5.69V - 5.43V) = -0.26V
    // * The dynamic range of one voice is |5.29V - 6.34V| = 1.05V
    // * The DC offset is thus -0.26V/1.05V = -1/4 of the dynamic range.
    //
    // Note that by removing the DC offset, we get the following ranges for
    // one voice:
    //     y > 0: -(5.29V - 5.43V) - (-0.26V) =  0.40V
    //     y < 0: -(6.34V - 5.43V) - (-0.26V) = -0.65V
    // The scaling of the voice amplitude is thus not symmetric about y = 0.
    // The asymmetric scaling happens either in the envelope multiplying
    // D/A converters, in the mixer, or both.
    // NB! This is not modeled.

    voice_DC = -4095*255/4;

    f0_points = f0_6581;
    f0_count = sizeof(f0_6581)/sizeof(*f0_6581);
  }
  else {
    // No DC offsets in MOS8580.
    voice_DC = 0;

    f0_points = f0_8580;
    f0_count = sizeof(f0_8580)/sizeof(*f0_8580);
  }

  // Create mapping from FC to cutoff frequency.
  interpolate(f0_points, f0_points,
	      f0_points + f0_count - 1, f0_points + f0_count - 1,
	      fc_plotter(), 1.0);
}


// ----------------------------------------------------------------------------
// SID reset.
// ----------------------------------------------------------------------------
void Filter::reset()
{
  fc = 0;

  res = 0;

  filtex = 0;

  filt3_filt2_filt1 = 0;

  voice3off = 0;

  hp_bp_lp = 0;

  vol = 0;

  // State of filter.
  Vhp = 0;
  Vbp = 0;
  Vlp = 0;
  Vnf = 0;

  set_w0();
  set_Q();
}


// ----------------------------------------------------------------------------
// Register functions.
// ----------------------------------------------------------------------------
void Filter::writeFC_LO(reg8 fc_lo)
{
  fc = fc & 0x7f8 | fc_lo & 0x007;
  set_w0();
}

void Filter::writeFC_HI(reg8 fc_hi)
{
  fc = (fc_hi << 3) & 0x7f8 | fc & 0x007;
  set_w0();
}

void Filter::writeRES_FILT(reg8 res_filt)
{
  res = (res_filt >> 4) & 0x0f;
  set_Q();

  filtex = res_filt & 0x08;
  filt3_filt2_filt1 = res_filt & 0x07;
}

void Filter::writeMODE_VOL(reg8 mode_vol)
{
  voice3off = mode_vol & 0x80;

  hp_bp_lp = (mode_vol >> 4) & 0x07;

  vol = mode_vol & 0x0f;
}

// Set filter cutoff frequency.
void Filter::set_w0()
{
  const double pi = 3.1415926535897932385;

  // Multiply with 1.048576 to facilitate division by 1 000 000 by right-
  // shifting 20 times (2 ^ 20 = 1048576).
  w0 = sound_sample(2*pi*f0[fc]*1.048576);
}

// Set filter resonance.
void Filter::set_Q()
{
  // Q is controlled linearly by res. Q has approximate range [0.707, 1.7].
  // As resonance is increased, the filter must be clocked more often to keep
  // stable.

  // The coefficient 1024 is dispensed of later by right-shifting 10 times
  // (2 ^ 10 = 1024).
  _1024_div_Q = sound_sample(1024.0/(0.707 + 1.0*res/0x0f));
}

// ----------------------------------------------------------------------------
// Spline functions.
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Return the array of spline interpolation points used to map the FC register
// to filter cutoff frequency.
// ----------------------------------------------------------------------------
void Filter::fc_default(const fc_point*& points, int& count)
{
  points = f0_points;
  count = f0_count;
}

// ----------------------------------------------------------------------------
// Given an array of interpolation points p with n points, the following
// statement will specify a new FC mapping:
//   interpolate(p, p, p + n - 1, p + n - 1, filter.fc_plotter());
// Note that the x range of the interpolation points *must* be [0, 2047].
// ----------------------------------------------------------------------------
PointPlotter<sound_sample> Filter::fc_plotter()
{
  return PointPlotter<sound_sample>(f0);
}
