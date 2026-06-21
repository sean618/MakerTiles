import numpy as np
from scipy.signal import firwin

# ==== FIR Design Parameters ====
num_taps = 16           # Number of filter taps (length)
fs = 32000              # Sampling rate in Hz
cutoff = 7000           # Cutoff frequency in Hz for anti-aliasing

# ==== Design the FIR Filter ====
# Use firwin to design a low-pass filter with a Hamming window
taps = firwin(num_taps, cutoff, window='hamming', fs=fs)

# ==== Normalize to Ensure Unity Gain (preserve DC level) ====
taps /= np.sum(taps)

# ==== Convert to Q15 Format ====
# Q15 range is [-32768, 32767], so scale by 32767
taps_q15 = np.round(taps * 32767).astype(np.int16)

# ==== Output ====
# Show the Q15 values and verify the sum
print("Q15 coefficients:")
print(taps_q15.tolist())

print("\nSum of Q15 coefficients:", np.sum(taps_q15))

print("\nFloating-point coefficients:")
print(taps.tolist())
