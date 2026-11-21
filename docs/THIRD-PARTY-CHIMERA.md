# Third-party notice: Chimera Vocal Synth

FROGGY (source/native/studio/sing_text.h and sing_frog.h) uses material from Chimera Vocal Synth, https://github.com/jamesondh/chimera-vocal-synth (commit 0dc13f8):

- the compact letter-to-sound rule set (crates/chimera/src/phonetics/english.rs), ported to C++ in sing_text.h;
- the idea and the vowel formant values (Hillenbrand, Getty, Clark and Wheeler, 1995, as listed in crates/chimera/src/phonetics/vowel_data.rs).

The singing engine itself is our own light formant voice. Chimera's airway network (96 to 192 kHz) is not used.

Chimera's licence, reproduced as required:

    MIT License
    
    Copyright (c) 2026 Jameson Hodge
    
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
