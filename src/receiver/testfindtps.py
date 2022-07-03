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

specfname=f'{options.spectrum_path}/19.2E/2022-06-28_01:13:49_H_dish0_adapter0_spectrum.dat'
tpfname=f'{options.spectrum_path}/19.2E/2022-06-28_01:13:49_H_dish0_adapter0_peaks.dat.off'


spec = np.atleast_2d(np.loadtxt(specfname))
tpsgt = np.atleast_2d(np.loadtxt(tpfname))
peak_freq, peak_sr = find_spectral_peaks(spec[:,0], spec[:,1])
np.savetxt(f'{options.spectrum_path}/19.2E/2022-06-28_01:13:49_H_dish0_adapter0_peaks.dat', np.vstack([peak_freq, peak_sr]).T)


print(tpsgt[2])
print(peak_freq[2], peak_sr[2])
