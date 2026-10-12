# Prompt-to-chain demo and two optimizer fixes

2026-10-12. First end-to-end run of the full pipeline on Linux: Jev assembles a
chain, CLAP tunes it, and the plugin's C++ DSP renders the result. Six prompts on
the 10 s guitar clip (`web/public/original-guitar.wav`), 300 iterations, seed 0,
MS-CLAP on CPU.

Reproduce: `inference/.venv/bin/python inference/diagnostics/prompt_demo.py
--match-loudness --regularization 1`, then `export_demo_web.py <out>` to publish to
the landing page.

## Finding 1: the optimizer won by turning things up

With the original loss, "crunchy and aggressive" left drive at 0, boosted four EQ
bands by 12 to 15 dB at Q 0.1, and set the compressor ratio to 1. Output peaked at
+29 dBFS. Four of six prompts peaked above 0 dBFS. CLAP's score tracks level, so
raising level was the cheapest way to lower the loss.

Fix: `match_loudness` scales the processed audio to the input's RMS before
embedding, so level changes earn nothing. All six peaks fell to between -28 and
+3 dBFS. The losses barely moved (for example, crunchy went from 0.714 to 0.741),
so the loudness gains were not buying much semantic progress. The option is off by
default, and the golden result is unchanged with it off. The plugin now sends it,
along with `parameter_regularization: 1`.

## Finding 2: distortion never engaged

Drive stayed at 0 dB in every chain. Below about 20 dB the waveshaper is nearly
linear (`amount = 0.04 * 10^(drive/20)`), so the gradient is flat at the old
default. Starting the same crunchy prompt at drive 30 dB, the optimizer moved it to
47 dB and reached a lower loss (0.688 against 0.741). Starting at 18 dB, it stayed
at 17.

Fix: new distortion instances default to drive 24 dB and mix 0.5. Jev only adds
distortion when the prompt calls for it, so starting audibly engaged is the honest
default. Distortion parity fixtures and the golden result were regenerated.

## Results (match_loudness, regularization 1)

| prompt | chain | CLAP loss |
|---|---|---|
| warm and roomy, like a small jazz club | eq → distortion → compressor → reverb | 0.798 → 0.454 |
| huge and spacious, like a cathedral | eq → reverb | 0.943 → 0.573 |
| crunchy and aggressive | distortion → eq → compressor | 0.841 → 0.740 |
| bright and airy | eq → compressor → reverb | 0.771 → 0.618 |
| an old telephone | eq → distortion → compressor → reverb | 0.902 → 0.775 |
| lo-fi, like a worn cassette tape | eq → distortion → compressor → reverb | 0.755 → 0.562 |

Assembly takes about 0.3 s per prompt. Optimization takes about 50 s on CPU (CUDA
was unavailable that day because of a driver/library mismatch).

## Open issues

- "Old telephone" and "lo-fi" cut the lows but *boost* the highs. A real phone line
  or cassette rolls the highs off. This is CLAP's judgment, not an optimizer bug,
  and it is the clearest case for the InstructFX2FX-style LLM parameter
  initialization.
- With regularization 1, drive stays near its start (22 to 26 dB). A multi-start
  run, keeping the best loss, would let crunchy prompts reach high drive without
  pulling every distortion upward.
- Every prompt renders from a single clip. A drum loop and a vocal would show
  whether the chains generalize.
