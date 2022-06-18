#!/usr/bin/python3
import sys
import os
import numpy as np
sys.path.insert(0, '../../x86_64/target/lib64/')
sys.path.insert(0, '../../build/src/receiver')
sys.path.insert(0, '../../build/src/stackstring/')
sys.path.insert(0, '../../gui/')
from pyspectrum import  find_spectral_peaks
from neumodvb.config import options

specfname=f'{options.spectrum_path}/18.0W/2022-06-03_15:16_H_dish1_spectrum.dat'
tpfname=f'{options.spectrum_path}/18.0W/2022-06-03_15:16_H_dish1_peaks.dat'


spec = np.atleast_2d(np.loadtxt(specfname))
tpsgt = np.atleast_2d(np.loadtxt(tpfname))
peak_freq, peak_sr = find_spectral_peaks(spec[:,0], spec[:,1])

print(tpsgt[2])
print(peak_freq[2], peak_sr[2])
